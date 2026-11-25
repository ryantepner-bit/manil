#include "LiveSequencer.h"
#include "../Core/ProjectModel.h"
#include "../Core/Scales.h"
#include "../Fractal/MandelMath.h"
#include "../Audio/AudioPool.h"

namespace Live
{
    juce::StringArray partNames() { return { "-", "Drums", "Bass", "Chords", "Lead", "Arp", "Break" }; }

    // ------------------------------------------------------------------ helpers (audio-thread safe)
    static inline bool euclid (int pulses, int steps, int i)
    {
        if (pulses <= 0) return false;
        if (pulses >= steps) return true;
        return ((i * pulses) % steps) < pulses;
    }

    static inline int pitchOf (const Frame& f, int degree, int octave)
    {
        const int n = juce::jmax (1, f.numDegs);
        const int oct = (int) std::floor ((double) degree / n);
        const int idx = degree - oct * n;
        return juce::jlimit (0, 127, 12 * (octave + 1) + f.root + f.degs[idx] + 12 * oct);
    }

    static inline DSP::Rng rngFor (const Frame& f, long long step, int part)
    {
        return DSP::Rng (DSP::hash32 (f.seed ^ DSP::hash32 ((juce::uint32) (step * 7919) + (juce::uint32) part * 104729u)) | 1u);
    }

    /** Swing: delay the off-beat 16ths (and the 32nds around them proportionally). */
    static inline double swingShift (double beat, float swing)
    {
        if (swing <= 0.0f) return 0.0;
        const double p = std::fmod (beat, 0.5) / 0.5;            // position inside the 8th
        const double tri = p < 0.5 ? p * 2.0 : (1.0 - p) * 2.0;   // 0 on the 8th, 1 on the off-16th
        return swing * 0.125 * tri;
    }

    // ------------------------------------------------------------------ frame publication (message thread)
    void Sequencer::setFrame (std::shared_ptr<Frame> f)
    {
        auto old = owner;
        owner = f;
        frame.store (f.get(), std::memory_order_release);
        DeferredDeleter::get().retire (old);
    }

    // ------------------------------------------------------------------ audio thread
    void Sequencer::emit (int part, int offset, int note, float vel, double beat)
    {
        if (eventCount[part] < maxEventsPerPart)
            events[part][eventCount[part]++] = { juce::jlimit (0, juce::jmax (0, blockLen - 1), offset), note, vel };
        int s1, n1, s2, n2;
        monitorFifo.prepareToWrite (1, s1, n1, s2, n2);
        if (n1 + n2 > 0)
        {
            monitorData[(size_t) (n1 > 0 ? s1 : s2)] = { part, note, (int) std::round (vel * 127.0f), beat };
            monitorFifo.finishedWrite (1);
        }
    }

    void Sequencer::noteOn (int part, int note, int vel, int offset, double beat, double lengthBeats)
    {
        auto& pend = pending[(size_t) part];
        int& np = numPending[(size_t) part];
        // retrigger of a sounding note: end it first
        for (int i = 0; i < np; ++i)
            if (pend[(size_t) i].note == note)
            {
                emit (part, offset, note, 0.0f, beat);
                pend[(size_t) i] = pend[(size_t) --np];
                break;
            }
        if (np >= (int) pend.size())
        {
            emit (part, offset, pend[0].note, 0.0f, beat);
            pend[0] = pend[(size_t) --np];
        }
        emit (part, offset, note, juce::jlimit (1, 127, vel) / 127.0f, beat);
        pend[(size_t) np++] = { beat + juce::jmax (0.02, lengthBeats), note };
    }

    void Sequencer::flush()
    {
        for (auto& c : eventCount) c = 0;
        blockLen = 1;
        for (int p = 1; p < NumParts; ++p)
        {
            for (int i = 0; i < numPending[(size_t) p]; ++i) emit (p, 0, pending[(size_t) p][(size_t) i].note, 0.0f, clock);
            numPending[(size_t) p] = 0;
        }
    }

    int Sequencer::chordToneNear (const Frame& f, int degree) const
    {
        const int n = juce::jmax (1, f.numDegs);
        int best = degree, bestDist = 1000;
        for (int o = -3; o <= 3; ++o)
            for (int k = 0; k < chordSize; ++k)
            {
                const int cand = chordDegs[(size_t) k] + o * n;
                const int d = std::abs (cand - degree);
                if (d < bestDist) { bestDist = d; best = cand; }
            }
        return best;
    }

    void Sequencer::chordFor (const Frame& f, long long bar)
    {
        if (bar == chordBar) return;
        chordBar = bar;
        int harmonicRhythm = (f.entropy > 0.55f || f.complexity > 0.7f) ? 1 : 2;
        if (f.style == Ambient) harmonicRhythm = 4;
        if (f.chordSpeed > 0) harmonicRhythm = f.chordSpeed;
        chordLenBars = harmonicRhythm;
        if (bar % harmonicRhythm != 0 && prevChordRoot >= 0) return;
        static const int consonant[] = { 0, 5, 3, 4 };
        static const int colourful[] = { 0, 5, 3, 4, 1, 2, 6 };
        const bool simple = f.style == Ambient || (f.insideFraction > 0.5f && f.entropy < 0.5f && f.complexity < 0.6f);
        const int* pool = simple ? consonant : colourful;
        const int poolSize = simple ? 4 : 7;
        const int i = (int) ((bar * 5 + 3) % juce::jmax (1, f.orbitCount));
        int pick = juce::jlimit (0, poolSize - 1, (int) (f.angle[i] * (float) poolSize));
        int r = pool[pick];
        if (bar % (8 * harmonicRhythm > 32 ? 16 : 8) == 0) r = 0;     // come home regularly
        else if (r == prevChordRoot) r = pool[(pick + 1) % poolSize];
        if (f.numDegs < 5) r = r % juce::jmax (1, f.numDegs);
        chordRoot = r;
        prevChordRoot = r;
        lastChordChange = bar;
        chordSize = (f.style == Ambient || f.entropy > 0.45f || f.complexity > 0.6f) ? 4 : 3;
        for (int k = 0; k < 4; ++k) chordDegs[(size_t) k] = r + 2 * k;
        currentChord.store (r);
    }

    // ------------------------------------------------------------------ lead: phrases, not random notes
    void Sequencer::buildPhrase (const Frame& f, long long idx, int stepsPerBar)
    {
        phraseIndex = idx;
        const bool ambient = f.style == Ambient;
        phraseBars = ambient ? 4 : 2;
        const int len = juce::jmin (phraseSteps, phraseBars * stepsPerBar);
        const float dens = juce::jlimit (0.0f, 1.0f, f.parts[Lead].density);
        const int form = (int) (idx % 4);                      // A A' B A''
        auto rng = rngFor (f, idx * 31 + 7, Lead);
        const int count = juce::jmax (1, f.orbitCount);
        const int n = juce::jmax (1, f.numDegs);

        auto makeRhythm = [&] (std::array<PhraseNote, phraseSteps>& out)
        {
            for (auto& p : out) p = {};
            int onsets = ambient ? 2 + (int) (dens * 3.0f) : 3 + (int) (dens * 7.0f + f.complexity * 2.0f);
            onsets = juce::jlimit (2, len / 2, onsets);
            const int rot = (f.period > 0 ? f.period : (int) (f.escape * 5.0f)) % 3;
            for (int s = 0; s < len; ++s)
            {
                const int k = (s + len - rot) % len;
                out[(size_t) s].on = euclid (onsets, len, k);
            }
            out[0].on = true;                                  // phrases start on the downbeat...
            if (! ambient) for (int s = len - 3; s < len; ++s) out[(size_t) s].on = false;   // ...and breathe at the end
            // durations: until the next onset (legato), capped
            for (int s = 0; s < len; ++s)
            {
                if (! out[(size_t) s].on) continue;
                int d = 1;
                while (s + d < len && ! out[(size_t) (s + d)].on) ++d;
                out[(size_t) s].len = juce::jlimit (1, ambient ? 24 : 6, d);
                out[(size_t) s].vel = (s % stepsPerBar == 0 ? 100 : (s % 4 == 0 ? 90 : 78));
            }
        };
        auto makeContour = [&] (std::array<PhraseNote, phraseSteps>& out, int startDeg, int salt)
        {
            int deg = startDeg, lastLeap = 0, j = (int) ((idx * 13 + salt) % count);
            for (int s = 0; s < len; ++s)
            {
                if (! out[(size_t) s].on) continue;
                const float a0 = f.angle[(size_t) (j % count)], a1 = f.angle[(size_t) ((j + 1) % count)];
                ++j;
                float d = a1 - a0;
                if (d > 0.5f) d -= 1.0f; else if (d < -0.5f) d += 1.0f;
                const float m = std::abs (d);
                int move = m < 0.08f ? 0 : (m < 0.22f ? 1 : (m < 0.38f ? 2 : 4));
                if (ambient) move = juce::jmin (move, 2);
                if (std::abs (lastLeap) >= 3) move = 1, d = lastLeap > 0 ? -1.0f : 1.0f;   // after a leap, step back
                const int step = d >= 0 ? move : -move;
                if (s != 0) deg += step;
                lastLeap = step;
                if (s % 8 == 0) deg = chordToneNear (f, deg);  // chord tones on the strong beats
                deg = juce::jlimit (-2, n + 5, deg);           // a singable range: about an octave and a half
                out[(size_t) s].degree = deg;
                out[(size_t) s].vel += (int) (f.radius[(size_t) (j % count)] * 18.0f);
            }
        };
        const bool keep = rng.uni() < 0.35f + 0.65f * juce::jlimit (0.0f, 1.0f, f.repeat);
        if (form == 0 || ! haveMotif || (form != 2 && ! keep))
        {
            makeRhythm (motif);
            makeContour (motif, chordToneNear (f, chordRoot + 2), 0);
            haveMotif = true;
        }
        phrase = motif;
        if (form == 1 || form == 3)
        {
            // variation: same rhythm, the second half answers differently
            std::array<PhraseNote, phraseSteps> alt = motif;
            makeContour (alt, motif[0].degree + (form == 1 ? 1 : -1), 17 + form);
            for (int s = len / 2; s < len; ++s) phrase[(size_t) s] = alt[(size_t) s];
        }
        else if (form == 2)
        {
            // contrast: new contour a bit higher, same rhythmic feel
            makeContour (phrase, chordToneNear (f, chordRoot + 4), 41);
        }
        if (form == 3)
        {
            // resolve: the last note lands on the chord root and rings
            for (int s = len - 1; s >= 0; --s)
                if (phrase[(size_t) s].on)
                {
                    phrase[(size_t) s].degree = chordToneNear (f, chordRoot);
                    phrase[(size_t) s].len = juce::jmax (phrase[(size_t) s].len, ambient ? 16 : 4);
                    break;
                }
        }
    }

    void Sequencer::stepLead (const Frame& f, int s, long long bar, double stepBeat, int offset, int bpb, double stepLen)
    {
        const int stepsPerBar = juce::jmax (1, (int) std::llround (bpb / stepLen));
        const int pb = f.style == Ambient ? 4 : 2;
        const long long idx = (long long) std::floor ((double) bar / pb);
        if (idx != phraseIndex) buildPhrase (f, idx, stepsPerBar);
        const int pos = (int) ((bar - idx * pb) * stepsPerBar + s);
        if (pos < 0 || pos >= phraseSteps) return;
        auto pn = phrase[(size_t) pos];
        if (! pn.on) return;
        int deg = pn.degree;
        if (s % 8 == 0) deg = chordToneNear (f, deg);        // the chord may have moved under the phrase
        lastDegree[(size_t) Lead] = deg;
        const double len = stepLen * pn.len * (f.style == Ambient ? 1.0 : 0.92);
        noteOn (Lead, pitchOf (f, deg, f.parts[Lead].octave), juce::jlimit (40, 127, pn.vel - (f.style == Ambient ? 25 : 0)), offset, stepBeat, len);
    }

    // ------------------------------------------------------------------ drums
    void Sequencer::stepDrums (const Frame& f, int s, long long bar, double stepBeat, int offset, DSP::Rng& rng, float cx)
    {
        const float dens = juce::jlimit (0.0f, 1.0f, f.parts[Drums].density);
        const float rad = f.radius[(size_t) (((s + bar * 16) % juce::jmax (1, f.orbitCount)))];
        auto hit = [&] (int pad, int vel) { noteOn (Drums, PadP::baseNote + pad, vel, offset, stepBeat, 0.1); };
        const int s16 = s % 16;
        if (f.style == Ambient)
        {
            // sparse, soft, no backbeat: shaker breaths, a rim here and there, a far-away kick
            if (s16 == 0 && (bar % 2) == 0 && dens > 0.3f) hit (0, 58);
            if ((s16 % 2) == 0 && rng.uni() < 0.12f + 0.5f * dens) hit (10, 30 + (int) (rng.uni() * 30.0f));
            if (euclid (3, 16, (s16 + (int) (bar % 5)) % 16) && rng.uni() < 0.35f * dens) hit (8, 40 + (int) (rad * 25.0f));
            if (s16 == 10 && rng.uni() < 0.25f * dens) hit (7, 45);
            if (s16 == 0 && bar % 8 == 0 && rng.uni() < 0.5f) hit (9, 40);
            return;
        }
        const int kickPulses = 3 + (f.period > 0 ? f.period % 3 : (int) (f.escape * 2.99f));
        if (s16 == 0 || (euclid (kickPulses, 16, s16) && rng.uni() < 0.55f + 0.45f * dens)) hit (0, s16 == 0 ? 120 : 100);
        if (f.style == Breakbeat) { if (s16 == 0 || s16 == 10) hit (11, 100); return; }   // the break carries the groove
        if (s16 == 4 || s16 == 12) hit (1, 112);
        else if ((s16 & 1) && rng.uni() < 0.2f * cx * dens) hit (1, 38);
        const bool hats16 = dens > 0.72f || (f.roughness > 0.5f && dens > 0.5f);
        if ((s16 % 2 == 0 && dens > 0.25f) || (hats16 && rng.uni() < 0.85f))
            hit (2, (s16 % 4 == 2 ? 92 : 66) + (int) (rng.uni() * 18.0f));
        if (s16 == 14 && rng.uni() < 0.55f * dens) hit (3, 80);
        if (s16 == 12 && (bar & 1) && rng.uni() < 0.6f) hit (4, 96);
        if (euclid (5, 16, (s16 + (int) (bar % 3)) % 16) && rng.uni() < 0.15f + 0.6f * f.roughness * dens) hit (7, 76);
        if (rng.uni() < 0.10f * dens * (f.boundary + cx)) hit (8, 70 + (int) (rad * 40.0f));
        if ((bar % 4) == 3 && s16 >= 12)
        {
            if (rng.uni() < 0.45f * (0.3f + cx)) hit (s16 % 2 ? 6 : 5, 90);
            if (s16 == 15 && rng.uni() < 0.7f) hit (9, 100);
        }
    }

    // ------------------------------------------------------------------ break: re-chop the break's own slices
    void Sequencer::stepBreak (const Frame& f, long long step, double stepBeat, int offset, int bpb, double stepLen, DSP::Rng& rng)
    {
        if (f.numSlices <= 0) return;
        const float dens = juce::jlimit (0.0f, 1.0f, f.parts[Break].density);
        const double bb = juce::jmax (1.0, (double) f.breakBeats);
        const double posInBreak = std::fmod (std::fmod (stepBeat, bb) + bb, bb);
        const long long bar = (long long) std::floor (stepBeat / bpb + 1e-9);
        const int s32 = (int) (((step % (bpb * 8)) + bpb * 8) % (bpb * 8));
        // which slice of the original break starts here?
        int orig = -1;
        for (int i = 0; i < f.numSlices; ++i)
            if (std::abs (f.sliceBeat[i] - posInBreak) < stepLen * 0.5 + 1e-6) { orig = i; break; }
        // how much to chop: the fractal's chaos + the complexity knob + the density slider
        const float chop = juce::jlimit (0.0f, 1.0f, 0.15f + f.chaos * 0.35f + f.complexity * 0.35f + (dens - 0.5f) * 0.4f);
        const int count = juce::jmax (1, f.orbitCount);
        const float ang = f.angle[(size_t) ((step / 2) % count)], rad = f.radius[(size_t) ((step / 2) % count)];
        const int vel = 96 + (int) (rad * 30.0f);
        const bool fillBar = (bar % 4) == 3 && s32 >= bpb * 6;     // last quarter of every 4th bar
        auto play = [&] (int slice, int v) { lastBreakSlice = slice; noteOn (Break, 36 + slice, v, offset, stepBeat, stepLen * 2.0); };

        if (fillBar && rng.uni() < 0.35f + chop * 0.6f)
        {
            // stutter fill: repeat one slice on 32nds / 16ths, rising
            const int src = (int) (f.angle[(size_t) ((bar * 3) % count)] * (float) f.numSlices) % f.numSlices;
            if ((s32 % 2) == 0 || chop > 0.55f) play (src, 70 + (s32 % (bpb * 2)) * 3);
            return;
        }
        if ((s32 % 2) == 1) return;                                 // the rest runs on 16ths
        if (orig >= 0)
        {
            if (rng.uni() < chop * 0.75f)
            {
                // swap the slice for another one chosen by the orbit
                int alt = juce::jlimit (0, f.numSlices - 1, (int) (ang * (float) f.numSlices));
                if (rng.uni() < 0.25f) alt = lastBreakSlice;       // or repeat the previous hit
                play (alt, vel);
            }
            else if (rng.uni() > 0.1f * (1.0f - dens)) play (orig, vel);
            return;
        }
        // extra hits in the gaps (ghosts), more with density
        if (rng.uni() < (dens - 0.45f) * 0.5f * chop) play ((int) (ang * (float) f.numSlices) % f.numSlices, 60 + (int) (rad * 25.0f));
    }

    // ------------------------------------------------------------------ dispatcher
    void Sequencer::stepPart (const Frame& f, int part, long long step, double stepBeat, int offset, int bpb, double stepLen)
    {
        const auto& ps = f.parts[part];
        const float dens = juce::jlimit (0.0f, 1.0f, ps.density);
        const int stepsPerBar = (int) std::llround (bpb / stepLen);
        const int s = (int) (((step % stepsPerBar) + stepsPerBar) % stepsPerBar);
        const long long bar = (long long) std::floor (stepBeat / bpb + 1e-9);
        chordFor (f, bar);
        auto rng = rngFor (f, step, part);
        const int count = juce::jmax (1, f.orbitCount);
        const int oi = (int) (((step % count) + count) % count);
        const float ang = f.angle[oi], rad = f.radius[oi];
        const float legato = 1.0f - f.chaos * 0.8f;
        const float cx = juce::jlimit (0.0f, 1.0f, f.chaos * (0.5f + f.complexity));
        const bool ambient = f.style == Ambient;
        const bool chordStart = s == 0 && bar == lastChordChange;
        const int n = juce::jmax (1, f.numDegs);

        switch (part)
        {
            case Drums: stepDrums (f, s, bar, stepBeat, offset, rng, cx); break;
            case Break: stepBreak (f, step, stepBeat, offset, bpb, stepLen, rng); break;
            case Lead: stepLead (f, s, bar, stepBeat, offset, bpb, stepLen); break;
            case Bass:
            {
                if (ambient)
                {
                    // drone: the chord root, held for the whole chord, an octave pop now and then
                    if (! chordStart) break;
                    const double len = chordLenBars * bpb * 0.98;
                    noteOn (part, pitchOf (f, chordRoot, ps.octave), 70 + (int) (rad * 20.0f), offset, stepBeat, len);
                    if (dens > 0.5f) noteOn (part, pitchOf (f, chordRoot + 4, ps.octave), 50, offset, stepBeat, len);
                    break;
                }
                const int bp = 2 + (int) (dens * 5.0f);
                const bool on = s % 16 == 0 || (euclid (bp, 16, (s + 3) % 16) && rng.uni() < 0.85f);
                if (! on) break;
                int deg = chordRoot;
                const float r = rng.uni();
                if (s % 16 != 0 && r < 0.22f) deg += 4;                         // fifth
                else if (s % 16 != 0 && ang < 0.12f) deg += n;                  // octave pop
                const double len = stepLen * juce::jlimit (0.6, 3.5, 1.2 + legato * 1.8);
                noteOn (part, pitchOf (f, deg, ps.octave), 92 + (int) (rad * 30.0f), offset, stepBeat, len);
                break;
            }
            case Chords:
            {
                if (ambient)
                {
                    // slow open-voiced pads: root, fifth, octave, tenth (+ ninth when dense)
                    if (! chordStart) break;
                    const double len = chordLenBars * bpb * 1.02;      // overlap a little into the next chord
                    const int voicing[] = { chordRoot, chordRoot + 4, chordRoot + n, chordRoot + n + 2, chordRoot + n + 1 };
                    const int notes = dens > 0.6f ? 5 : 4;
                    for (int k = 0; k < notes; ++k) noteOn (part, pitchOf (f, voicing[k], ps.octave - 1), 52 + (int) (rad * 14.0f), offset, stepBeat, len);
                    break;
                }
                const bool barStart = s % 16 == 0;
                const bool rehit = (s % 16 == 8 && dens > 0.7f) || (s % 16 == 10 && f.roughness > 0.55f && dens > 0.45f);
                if (! barStart && ! rehit) break;
                const double len = (rehit || dens > 0.7f ? bpb * 0.5 : (double) bpb) * 0.97;
                const int vel = 60 + (int) (rad * 22.0f) + (barStart ? 8 : 0);
                for (int k = 0; k < chordSize; ++k)
                    noteOn (part, pitchOf (f, chordDegs[(size_t) k], ps.octave), vel, offset, stepBeat, len);
                break;
            }
            case Arp:
            {
                if (ambient)
                {
                    // stochastic grain cloud (after Cecilia5's StochGrains): sparse soft notes from the chord,
                    // scattered over three octaves, random length and loudness
                    if (rng.uni() > 0.03f + 0.22f * dens) break;
                    const int k = (int) (rng.uni() * (float) chordSize);
                    const int octave = (int) (ang * 3.0f);
                    const int deg = chordDegs[(size_t) k] + n * octave + (rng.uni() < 0.2f ? 1 : 0);
                    noteOn (part, pitchOf (f, deg, ps.octave - 1), 28 + (int) (rng.uni() * 36.0f), offset, stepBeat, 0.25 + rng.uni() * 1.75);
                    break;
                }
                if (rng.uni() > 0.45f + 0.55f * dens) break;
                const int L = chordSize * 2;
                const int mode = (int) ((bar + f.period) % 3);
                int& ai = arpIndex[(size_t) part];
                int idx;
                if (mode == 0) idx = ai % L;
                else if (mode == 1) idx = L - 1 - (ai % L);
                else idx = juce::jlimit (0, L - 1, (int) (ang * (float) L));
                ++ai;
                const int deg = chordDegs[(size_t) (idx % chordSize)] + n * (idx / chordSize);
                noteOn (part, pitchOf (f, deg, ps.octave), 56 + (int) (rad * 34.0f), offset, stepBeat, stepLen * 0.7);
                break;
            }
            default: break;
        }
    }

    void Sequencer::process (int n, double beatsPerSample, int bpb)
    {
        for (auto& c : eventCount) c = 0;
        blockLen = n; bps = beatsPerSample; blockStart = clock;
        const double b1 = clock + n * beatsPerSample;
        auto* f = frame.load (std::memory_order_acquire);
        bpb = juce::jmax (1, bpb);
        if (f != nullptr && enabled.load (std::memory_order_relaxed))
        {
            // the chord is decided by the first part that reaches a bar, so parts must be visited in time order:
            // process bass/chords before the melodic parts within each step by iterating parts in this order
            static const int order[] = { Chords, Bass, Drums, Break, Lead, Arp };
            for (int p : order)
            {
                auto flushUpTo = [&] (double limit, bool inclusive)
                {
                    auto& pend = pending[(size_t) p];
                    int& np = numPending[(size_t) p];
                    for (int i = 0; i < np;)
                    {
                        const double ob = pend[(size_t) i].offBeat;
                        if (inclusive ? ob <= limit + 1e-9 : ob < limit)
                        {
                            emit (p, (int) ((ob - clock) / beatsPerSample), pend[(size_t) i].note, 0.0f, ob);
                            pend[(size_t) i] = pend[(size_t) --np];
                        }
                        else ++i;
                    }
                };
                int rate = 4;
                if (p == Arp) rate = f->style == Ambient ? 8 : ((f->period >= 3 && (f->period % 2) == 1) ? 3 : 4);
                if (p == Break) rate = 8;
                const double stepLen = 1.0 / rate;
                const float sw = (rate == 3) ? 0.0f : f->swing;
                // swing can push a step up to 1/8 beat later: look one step back
                const long long first = (long long) std::ceil ((clock - 0.13) * rate - 1e-9);
                const long long last = (long long) std::ceil (b1 * rate - 1e-9);
                for (long long k = first; k < last; ++k)
                {
                    const double grid = k * stepLen;
                    const double sb = grid + swingShift (grid, sw);
                    if (sb < clock - 1e-9 || sb >= b1) continue;
                    flushUpTo (sb, true);
                    if (f->parts[p].on) stepPart (*f, p, k, sb, (int) ((sb - clock) / beatsPerSample), bpb, stepLen);
                }
                flushUpTo (b1, false);
            }
        }
        clock = b1;
        liveBeat.store (clock, std::memory_order_relaxed);
    }

    // ------------------------------------------------------------------ message thread
    juce::ValueTree createLiveNode()
    {
        juce::ValueTree l (IDs::LIVE);
        l.setProperty (IDs::liveOn, true, nullptr);
        l.setProperty (IDs::root, 9, nullptr);
        l.setProperty (IDs::scale, (int) Scales::Dorian, nullptr);
        l.setProperty (IDs::customMask, 0xfff, nullptr);
        l.setProperty (IDs::drift, 0.35f, nullptr);
        l.setProperty (IDs::complexity, 0.5f, nullptr);
        l.setProperty (IDs::style, (int) IDM, nullptr);
        l.setProperty (IDs::chordSpeed, 0, nullptr);
        l.setProperty (IDs::swing, 0.0f, nullptr);
        l.setProperty (IDs::space, 0.35f, nullptr);
        l.setProperty (IDs::repeat, 0.6f, nullptr);
        const float dens[] = { 0, 0.6f, 0.5f, 0.5f, 0.55f, 0.5f, 0.6f };
        const int oct[] = { 0, 0, 2, 4, 5, 5, 0 };
        const int ch[] = { 0, 9, 1, 2, 3, 4, 5 };          // 0-based: drums on channel 10
        for (int p = 1; p < NumParts; ++p)
        {
            juce::ValueTree lp (IDs::LPART);
            lp.setProperty (IDs::id, p, nullptr);
            lp.setProperty (IDs::name, partNames()[p], nullptr);
            lp.setProperty (IDs::liveOn, p != Break, nullptr);   // the break joins in the Breakbeat style
            lp.setProperty (IDs::density, dens[p], nullptr);
            lp.setProperty (IDs::octave, oct[p], nullptr);
            lp.setProperty (IDs::midiCh, ch[p], nullptr);
            l.appendChild (lp, nullptr);
        }
        return l;
    }

    void buildFrame (Frame& f, double cx, double cy, double scale, const juce::ValueTree& live, const BreakInfo* brk)
    {
        const auto feat = MandelMath::analyse (cx, cy, scale, 300);
        f.escape = (float) feat.escape; f.boundary = (float) feat.boundary; f.chaos = (float) feat.chaos;
        f.entropy = (float) feat.entropy; f.insideFraction = (float) feat.insideFraction; f.roughness = (float) feat.roughness;
        f.period = feat.period; f.inside = feat.inside;

        // orbit of z -> z^2 + c : the melodic material
        double zx = 0, zy = 0;
        int count = 0;
        // inside a period-3+ bulb: settle onto the attracting cycle -> a p-note riff.
        // main cardioid / period-2 bulb: use the spiral towards the fixed point instead (else one note).
        const int skip = (feat.inside && feat.period >= 3) ? 160 : 0;
        for (int it = 0; it < skip + Frame::maxOrbit && count < Frame::maxOrbit; ++it)
        {
            const double nx = zx * zx - zy * zy + cx, ny = 2.0 * zx * zy + cy;
            zx = nx; zy = ny;
            const double r2 = zx * zx + zy * zy;
            if (r2 > 4.0) break;
            if (it < skip) continue;
            f.angle[count] = (float) ((std::atan2 (zy, zx) + juce::MathConstants<double>::pi) / juce::MathConstants<double>::twoPi);
            f.radius[count] = (float) juce::jmin (1.0, std::sqrt (r2) * 0.5);
            ++count;
        }
        if (count < 8)
        {
            // escapes almost at once: use the neighbourhood contour instead (still location-specific)
            count = juce::jmin ((int) feat.contour.size(), Frame::maxOrbit);
            for (int i = 0; i < count; ++i) { f.angle[i] = feat.contour[(size_t) i]; f.radius[i] = feat.energy[(size_t) i]; }
        }
        f.orbitCount = juce::jmax (1, count);

        f.root = juce::jlimit (0, 11, (int) live.getProperty (IDs::root, 9));
        const auto degs = Scales::degrees ((int) live.getProperty (IDs::scale, (int) Scales::Dorian), (int) live.getProperty (IDs::customMask, 0xfff));
        f.numDegs = juce::jlimit (1, 12, (int) degs.size());
        for (int i = 0; i < f.numDegs; ++i) f.degs[i] = degs[(size_t) i];
        f.complexity = (float) live.getProperty (IDs::complexity, 0.5f);
        f.style = juce::jlimit (0, 2, (int) live.getProperty (IDs::style, 0));
        f.chordSpeed = juce::jlimit (0, 8, (int) live.getProperty (IDs::chordSpeed, 0));
        f.swing = juce::jlimit (0.0f, 0.6f, (float) live.getProperty (IDs::swing, 0.0f));
        f.repeat = juce::jlimit (0.0f, 1.0f, (float) live.getProperty (IDs::repeat, 0.6f));
        for (auto lp : live)
        {
            if (! lp.hasType (IDs::LPART)) continue;
            const int p = (int) lp[IDs::id];
            if (p <= 0 || p >= NumParts) continue;
            f.parts[p].on = (bool) lp.getProperty (IDs::liveOn, true);
            f.parts[p].density = (float) lp.getProperty (IDs::density, 0.5f);
            f.parts[p].octave = (int) lp.getProperty (IDs::octave, 4);
            f.parts[p].midiChannel = juce::jlimit (1, 16, (int) lp.getProperty (IDs::midiCh, p - 1) + 1);
        }
        f.numSlices = 0;
        if (brk != nullptr && brk->slicePoints.size() >= 2)
        {
            f.breakBeats = (float) juce::jmax (0.5, brk->lengthBeats);
            for (size_t i = 0; i + 1 < brk->slicePoints.size() && f.numSlices < Frame::maxSlices; ++i)
                f.sliceBeat[f.numSlices++] = brk->slicePoints[i] * f.breakBeats;
        }
        const auto qx = (juce::int64) std::floor (cx * 2.0e5), qy = (juce::int64) std::floor (cy * 2.0e5);
        f.seed = DSP::hash32 ((juce::uint32) qx * 2654435761u ^ DSP::hash32 ((juce::uint32) qy + 0x9e3779b9u));
    }
}
