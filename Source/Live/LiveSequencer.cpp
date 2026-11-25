#include "LiveSequencer.h"
#include "../Core/ProjectModel.h"
#include "../Core/Scales.h"
#include "../Fractal/MandelMath.h"
#include "../Audio/AudioPool.h"

namespace Live
{
    juce::StringArray partNames() { return { "-", "Drums", "Bass", "Chords", "Lead", "Arp" }; }

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
        for (int o = -2; o <= 2; ++o)
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
        const int harmonicRhythm = (f.entropy > 0.55f || f.complexity > 0.7f) ? 1 : 2;
        if (bar % harmonicRhythm != 0 && prevChordRoot >= 0) return;
        static const int consonant[] = { 0, 5, 3, 4 };
        static const int colourful[] = { 0, 5, 3, 4, 1, 2, 6 };
        const bool simple = f.insideFraction > 0.5f && f.entropy < 0.5f && f.complexity < 0.6f;
        const int* pool = simple ? consonant : colourful;
        const int poolSize = simple ? 4 : 7;
        const int i = (int) ((bar * 5 + 3) % juce::jmax (1, f.orbitCount));
        int pick = juce::jlimit (0, poolSize - 1, (int) (f.angle[i] * (float) poolSize));
        int r = pool[pick];
        if (bar % 8 == 0) r = 0;                                   // come home every 8 bars
        else if (r == prevChordRoot) r = pool[(pick + 1) % poolSize];
        if (f.numDegs < 5) r = r % juce::jmax (1, f.numDegs);
        chordRoot = r;
        prevChordRoot = r;
        chordSize = (f.entropy > 0.45f || f.complexity > 0.6f) ? 4 : 3;
        for (int k = 0; k < 4; ++k) chordDegs[(size_t) k] = r + 2 * k;
        currentChord.store (r);
    }

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

        switch (part)
        {
            case Drums:
            {
                auto hit = [&] (int pad, int vel) { noteOn (part, PadP::baseNote + pad, vel, offset, stepBeat, 0.1); };
                const int s16 = s % 16;
                const int kickPulses = 3 + (f.period > 0 ? f.period % 3 : (int) (f.escape * 2.99f));
                if (s16 == 0 || (euclid (kickPulses, 16, s16) && rng.uni() < 0.55f + 0.45f * dens)) hit (0, s16 == 0 ? 120 : 100);
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
                break;
            }
            case Bass:
            {
                const int bp = 2 + (int) (dens * 5.0f);
                const bool on = s % 16 == 0 || (euclid (bp, 16, (s + 3) % 16) && rng.uni() < 0.85f);
                if (! on) break;
                int deg = chordRoot;
                const float r = rng.uni();
                if (s % 16 != 0 && r < 0.22f) deg += 4;                         // fifth
                else if (s % 16 != 0 && ang < 0.12f) deg += juce::jmax (1, f.numDegs); // octave pop
                const double len = stepLen * juce::jlimit (0.6, 3.5, 1.2 + legato * 1.8);
                noteOn (part, pitchOf (f, deg, ps.octave), 92 + (int) (rad * 30.0f), offset, stepBeat, len);
                break;
            }
            case Chords:
            {
                const bool barStart = s % 16 == 0;
                const bool rehit = (s % 16 == 8 && dens > 0.7f) || (s % 16 == 10 && f.roughness > 0.55f && dens > 0.45f);
                if (! barStart && ! rehit) break;
                const double len = (rehit || dens > 0.7f ? bpb * 0.5 : (double) bpb) * 0.97;
                const int vel = 60 + (int) (rad * 22.0f) + (barStart ? 8 : 0);
                for (int k = 0; k < chordSize; ++k)
                    noteOn (part, pitchOf (f, chordDegs[(size_t) k], ps.octave), vel, offset, stepBeat, len);
                break;
            }
            case Lead:
            {
                const int pulses = juce::jlimit (1, 15, (int) (3.0f + dens * 9.0f + cx * 2.0f));
                bool on = euclid (pulses, 16, (s + f.period) % 16);
                if (on && rad < 0.04f && rng.uni() < 0.5f) on = false;
                if (! on && rng.uni() < 0.08f * dens * f.boundary) on = true;
                if (! on) break;
                const int span = 7 + (int) (f.complexity * 7.0f);
                int deg = chordRoot + (int) std::round (ang * span) - span / 2;
                if (s % 4 == 0) deg = chordToneNear (f, deg);
                int& last = lastDegree[(size_t) part];
                if (std::abs (deg - last) > 5) deg = last + (deg > last ? 3 : -3);
                deg = juce::jlimit (-7, 11, deg);
                last = deg;
                const double len = stepLen * juce::jlimit (0.45, 2.8, 0.5 + legato * 1.6);
                const int vel = 72 + (int) (rad * 40.0f) + (s % 4 == 0 ? 10 : 0);
                noteOn (part, pitchOf (f, deg, ps.octave), vel, offset, stepBeat, len);
                break;
            }
            case Arp:
            {
                if (rng.uni() > 0.45f + 0.55f * dens) break;
                const int L = chordSize * 2;
                const int mode = (int) ((bar + f.period) % 3);
                int& ai = arpIndex[(size_t) part];
                int idx;
                if (mode == 0) idx = ai % L;
                else if (mode == 1) idx = L - 1 - (ai % L);
                else idx = juce::jlimit (0, L - 1, (int) (ang * (float) L));
                ++ai;
                const int deg = chordDegs[(size_t) (idx % chordSize)] + juce::jmax (1, f.numDegs) * (idx / chordSize);
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
            for (int p = 1; p < NumParts; ++p)
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
                if (p == Arp) rate = (f->period >= 3 && (f->period % 2) == 1) ? 3 : 4;
                const double stepLen = 1.0 / rate;
                const long long first = (long long) std::ceil (clock * rate - 1e-9);
                const long long last = (long long) std::ceil (b1 * rate - 1e-9);
                for (long long k = first; k < last; ++k)
                {
                    const double sb = k * stepLen;
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
        const float dens[] = { 0, 0.6f, 0.5f, 0.5f, 0.55f, 0.5f };
        const int oct[] = { 0, 0, 2, 4, 5, 5 };
        const int ch[] = { 0, 9, 1, 2, 3, 4 };          // 0-based: drums on channel 10
        for (int p = 1; p < NumParts; ++p)
        {
            juce::ValueTree lp (IDs::LPART);
            lp.setProperty (IDs::id, p, nullptr);
            lp.setProperty (IDs::name, partNames()[p], nullptr);
            lp.setProperty (IDs::liveOn, true, nullptr);
            lp.setProperty (IDs::density, dens[p], nullptr);
            lp.setProperty (IDs::octave, oct[p], nullptr);
            lp.setProperty (IDs::midiCh, ch[p], nullptr);
            l.appendChild (lp, nullptr);
        }
        return l;
    }

    void buildFrame (Frame& f, double cx, double cy, double scale, const juce::ValueTree& live)
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
        const auto qx = (juce::int64) std::floor (cx * 2.0e5), qy = (juce::int64) std::floor (cy * 2.0e5);
        f.seed = DSP::hash32 ((juce::uint32) qx * 2654435761u ^ DSP::hash32 ((juce::uint32) qy + 0x9e3779b9u));
    }
}
