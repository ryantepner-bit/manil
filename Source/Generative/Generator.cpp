#include "Generator.h"
#include <algorithm>
#include <cmath>

namespace Gen
{
    juce::StringArray roleNames() { return { "Melody", "Bass", "Chords", "Arp", "Texture", "Breaks" }; }

    // ------------------------------------------------------------------ Motif (de)serialisation
    juce::String Motif::serialise() const
    {
        juce::StringArray a;
        a.add (juce::String (length, 4));
        for (auto& n : notes)
            a.add (juce::String (n.degree) + ":" + juce::String (n.onset, 4) + ":" + juce::String (n.duration, 4) + ":" + juce::String (n.velocity));
        return a.joinIntoString (";");
    }

    Motif Motif::parse (const juce::String& s)
    {
        Motif m;
        auto parts = juce::StringArray::fromTokens (s, ";", "");
        if (parts.isEmpty()) return m;
        m.length = juce::jmax (0.25, parts[0].getDoubleValue());
        for (int i = 1; i < parts.size(); ++i)
        {
            auto f = juce::StringArray::fromTokens (parts[i], ":", "");
            if (f.size() < 4) continue;
            m.notes.push_back ({ f[0].getIntValue(), f[1].getDoubleValue(), f[2].getDoubleValue(), f[3].getIntValue() });
        }
        return m;
    }

    // ------------------------------------------------------------------ traits
    MusicalTraits MusicalTraits::from (const FractalFeatures& f, const Settings& s)
    {
        MusicalTraits t;
        // attracting-cycle period => tuplet feel (irregular subdivisions)
        const int p = f.period;
        if (p == 3 || p == 6 || p == 9 || p == 12) t.subdivision = 3;
        else if (p == 5 || p == 10 || p == 15)     t.subdivision = 5;
        else if (p == 7 || p == 14)                t.subdivision = 7;
        else if (p > 0 && p % 2 == 0)              t.subdivision = 4;
        else t.subdivision = f.entropy > 0.75 ? 6 : 4;
        if (s.complexity < 0.25f && t.subdivision > 4) t.subdivision = 4;

        t.irregular = (f.boundary * s.complexity) > 0.3;
        t.syncopation = (float) juce::jlimit (0.0, 1.0, 0.25 * f.chaos + 0.75 * f.boundary) * (0.4f + 0.6f * s.complexity);
        t.densityScale = (float) (0.55 + 0.6 * f.roughness + 0.25 * (1.0 - f.insideFraction));
        static const int cycles[] = { 3, 5, 7, 9, 11, 13, 5, 7 };
        t.polyCycle = p > 2 ? juce::jlimit (3, 13, p | 1) : cycles[(int) (f.angle * 7.99)];
        t.chordsPerBar = f.entropy > 0.6 && s.complexity > 0.4f ? 2 : 1;
        t.consonance = (float) juce::jlimit (0.0, 1.0, 0.35 + 0.65 * f.insideFraction - 0.3 * f.chaos + 0.2);
        t.legato = (float) juce::jlimit (0.1, 1.0, 1.0 - f.roughness * 0.7);
        t.accent = (float) juce::jlimit (0.0, 1.0, 0.3 + f.meanEscape * 0.7);
        return t;
    }

    juce::String MusicalTraits::describe() const
    {
        juce::String s;
        s << "grid 1/" << subdivision << (irregular ? " (irregular)" : "")
          << "  sync " << juce::String (syncopation, 2)
          << "  poly " << polyCycle
          << "  chords/bar " << chordsPerBar
          << "  consonance " << juce::String (consonance, 2);
        return s;
    }

    // ------------------------------------------------------------------ helpers
    static int gridForBeat (const MusicalTraits& tr, int beat, juce::Random& rng)
    {
        if (! tr.irregular) return tr.subdivision;
        static const int choices[] = { 2, 3, 4, 4, 5, 6, 7 };
        juce::ignoreUnused (beat);
        return rng.nextFloat() < 0.55f ? tr.subdivision : choices[rng.nextInt (7)];
    }

    static int contourToDegree (float c, int range) { return (int) std::round ((c - 0.5f) * (float) range); }

    Motif deriveMotif (const Settings& s, const FractalFeatures& f, juce::Random& rng)
    {
        const auto tr = MusicalTraits::from (f, s);
        Motif m;
        const int motifBars = s.complexity > 0.6f ? 2 : 1;
        m.length = (double) (motifBars * s.beatsPerBar);
        const int range = s.role == Bass ? 5 : (int) (6 + s.complexity * 9);
        int ci = 0;
        const float dens = juce::jlimit (0.03f, 0.95f, s.density * tr.densityScale);
        for (int beat = 0; beat < (int) m.length; ++beat)
        {
            const int grid = gridForBeat (tr, beat, rng);
            for (int k = 0; k < grid; ++k)
            {
                const double onset = beat + k / (double) grid;
                const bool strong = (k == 0);
                float pHit = dens * (0.35f + 0.65f * f.energy[(size_t) (ci % 64)]);
                if (strong) pHit += (1.0f - tr.syncopation) * 0.35f;
                else        pHit += tr.syncopation * 0.25f * (k % 2 == 1 ? 1.0f : 0.4f);
                if (m.notes.empty() && beat == 0 && k == 0) pHit = 1.0f; // always anchor the motif
                if (rng.nextFloat() < pHit)
                {
                    MotifNote n;
                    n.degree = contourToDegree (f.contour[(size_t) (ci % 64)], range);
                    const double step = 1.0 / grid;
                    n.duration = step * (tr.legato > 0.6f ? 1.0 + std::floor (f.energy[(size_t) ((ci + 7) % 64)] * 3.0) : 0.6 + 0.4 * tr.legato);
                    n.onset = onset;
                    n.velocity = juce::jlimit (30, 127, (int) (64 + 40 * f.energy[(size_t) ((ci + 3) % 64)] + (strong ? 18 * tr.accent : 0) + rng.nextInt (12) - 6));
                    m.notes.push_back (n);
                }
                ++ci;
            }
        }
        // remove overlaps: shorten notes that run into the next onset (monophonic motif)
        for (size_t i = 0; i + 1 < m.notes.size(); ++i)
            m.notes[i].duration = juce::jmin (m.notes[i].duration, m.notes[i + 1].onset - m.notes[i].onset);
        if (! m.notes.empty())
            m.notes.back().duration = juce::jmin (m.notes.back().duration, m.length - m.notes.back().onset);
        return m;
    }

    Motif mutate (const Motif& src, const Settings& s, const FractalFeatures& f, float amount, juce::Random& rng)
    {
        Motif m = src;
        if (m.notes.empty()) return deriveMotif (s, f, rng);
        amount = juce::jlimit (0.0f, 1.0f, amount);
        const int ops = (int) std::round (amount * (float) m.notes.size() * 0.6f);
        const int range = s.role == Bass ? 5 : (int) (6 + s.complexity * 9);
        for (int o = 0; o < ops; ++o)
        {
            const int which = rng.nextInt (7);
            const int i = rng.nextInt ((int) m.notes.size());
            auto& n = m.notes[(size_t) i];
            switch (which)
            {
                case 0: n.degree += rng.nextBool() ? 1 : -1; break;                               // step
                case 1: n.degree = contourToDegree (f.contour[(size_t) ((i * 5) % 64)], range); break; // pull toward new region
                case 2: { const int j = rng.nextInt ((int) m.notes.size()); std::swap (n.degree, m.notes[(size_t) j].degree); break; }
                case 3: n.degree = -n.degree; break;                                               // local inversion
                case 4: n.velocity = juce::jlimit (30, 127, n.velocity + rng.nextInt (41) - 20); break;
                case 5: // rhythmic displacement by a 16th, kept inside the motif
                {
                    if (i == 0) break; // keep the downbeat anchor
                    const double shift = (rng.nextBool() ? 0.25 : -0.25);
                    const double prev = i > 0 ? m.notes[(size_t) i - 1].onset : 0.0;
                    const double next = (size_t) i + 1 < m.notes.size() ? m.notes[(size_t) i + 1].onset : m.length;
                    const double no = n.onset + shift;
                    if (no > prev + 0.01 && no < next - 0.01) n.onset = no;
                    break;
                }
                case 6: n.duration = juce::jmax (0.0625, n.duration * (rng.nextBool() ? 0.5 : 1.5)); break;
            }
        }
        // occasionally add/remove a note when mutation is strong
        if (amount > 0.5f && rng.nextFloat() < amount - 0.4f && m.notes.size() > 3)
            m.notes.erase (m.notes.begin() + 1 + rng.nextInt ((int) m.notes.size() - 1));
        std::sort (m.notes.begin(), m.notes.end(), [] (auto& a, auto& b) { return a.onset < b.onset; });
        for (size_t i = 0; i + 1 < m.notes.size(); ++i)
            m.notes[i].duration = juce::jmin (m.notes[i].duration, m.notes[i + 1].onset - m.notes[i].onset);
        if (! m.notes.empty())
            m.notes.back().duration = juce::jmin (m.notes.back().duration, m.length - m.notes.back().onset);
        return m;
    }

    std::vector<int> progression (const Settings& s, const FractalFeatures& f, const MusicalTraits& tr, juce::Random& rng)
    {
        const int count = juce::jmax (1, s.bars * tr.chordsPerBar);
        std::vector<int> prog;
        int cur = 0;
        // consonant regions favour functional root motion (4ths/5ths), chaotic ones favour steps/tritones
        static const int functional[] = { 3, 4, -3, 5, 2, -2 };
        static const int colour[] = { 1, -1, 2, 6, -2, 3 };
        for (int i = 0; i < count; ++i)
        {
            prog.push_back (cur);
            const float c = f.contour[(size_t) ((i * 9 + 4) % 64)];
            const int idx = juce::jlimit (0, 5, (int) (c * 6.0f));
            const int mv = rng.nextFloat() < tr.consonance ? functional[idx] : colour[idx];
            cur = ((cur + mv) % 7 + 7) % 7;
            if (i == count - 2 && tr.consonance > 0.5f) cur = 4; // dominant before cycle back
        }
        return prog;
    }

    // ------------------------------------------------------------------ chord voicing
    static std::vector<int> chordPitches (int rootDegree, const Settings& s, const MusicalTraits& tr, const std::vector<int>& degs, int octave)
    {
        std::vector<int> stack { 0, 2, 4 };
        if (s.complexity > 0.35f) stack.push_back (6);       // 7th
        if (s.complexity > 0.7f) stack.push_back (8);        // 9th
        if (tr.consonance < 0.4f) stack[1] = 3;              // sus4 colour in unstable regions
        std::vector<int> out;
        for (auto st : stack)
            out.push_back (Scales::degreeToPitch (rootDegree + st, s.root, octave, degs));
        // spread voicing: drop the second voice an octave when complexity is high
        if (out.size() >= 4 && s.complexity > 0.55f) out[1] -= 12;
        return out;
    }

    static void addNote (std::vector<NoteData>& out, int pitch, double start, double len, int vel, float prob = 1.0f)
    {
        NoteData n; n.pitch = juce::jlimit (0, 127, pitch); n.start = start; n.length = juce::jmax (0.03125, len);
        n.velocity = juce::jlimit (1, 127, vel); n.probability = prob;
        out.push_back (n);
    }

    // ------------------------------------------------------------------ Breaks
    static Result generateBreaks (const Settings& s, const FractalFeatures& f, juce::Random& rng)
    {
        Result r;
        const auto tr = MusicalTraits::from (f, s);
        const int barSteps = s.beatsPerBar * 4;
        const int bars = juce::jlimit (1, 4, s.bars);
        const int steps = juce::jmin (64, barSteps * bars);

        // break templates (16 steps): kick, snare
        static const char* kicks[] = { "x.x.......xx....", "x.........x.....", "x..x..x...x..x..", "x......x..x.....", "x.x...x.x..x...." };
        static const char* snares[] = { "....x..x.x..x..x", "....x.......x...", "....x.......x..x", "....x..x....x...", "..x...x...x...x." };
        const int tpl = (int) (f.angle * 4.99);

        auto lane = [&] (int pad, int st, int rate) { PatternLaneOut l; l.pad = pad; l.steps = st; l.rate = rate; l.data.resize ((size_t) st); return l; };
        auto kick = lane (0, steps, 4), snare = lane (1, steps, 4), ch = lane (2, steps, 4), oh = lane (3, steps, 4);

        for (int i = 0; i < steps; ++i)
        {
            const int b = i % 16;
            const int bar = i / barSteps;
            const bool lastBar = bar == bars - 1;
            if (kicks[tpl][b] == 'x') kick.data[(size_t) i] = { 120 - rng.nextInt (15), 100, 1 };
            if (snares[tpl][b] == 'x') snare.data[(size_t) i] = { 115 - rng.nextInt (12), 100, 1 };
            // syncopated displacement of kicks
            if (kick.data[(size_t) i].velocity > 0 && b != 0 && rng.nextFloat() < tr.syncopation * 0.4f && i + 1 < steps)
            { kick.data[(size_t) i + 1] = kick.data[(size_t) i]; kick.data[(size_t) i] = {}; }
            // ghost snares
            if (snare.data[(size_t) i].velocity == 0 && rng.nextFloat() < 0.12f + 0.25f * s.complexity * tr.syncopation)
                snare.data[(size_t) i] = { 35 + rng.nextInt (25), 55 + rng.nextInt (40), 1 };
            // fills / rolls at the end of the phrase
            if (lastBar && b >= 12 && rng.nextFloat() < s.complexity * 0.8f)
                snare.data[(size_t) i] = { 70 + rng.nextInt (40), 90, 1 + rng.nextInt (juce::jlimit (1, 4, 1 + (int) (f.chaos * 4))) };
            // hats
            const float hatDens = juce::jlimit (0.1f, 1.0f, s.density * tr.densityScale);
            if ((b % 2 == 0 && rng.nextFloat() < 0.4f + hatDens * 0.6f) || (b % 2 == 1 && rng.nextFloat() < hatDens * 0.75f))
            {
                const int rat = rng.nextFloat() < f.chaos * s.complexity * 0.6f ? 2 + rng.nextInt (3) : 1;
                ch.data[(size_t) i] = { 50 + (int) (f.energy[(size_t) (i % 64)] * 60), b % 4 == 0 ? 100 : 60 + rng.nextInt (40), rat };
            }
            if (b == 14 && rng.nextFloat() < 0.5f) { oh.data[(size_t) i] = { 80, 80, 1 }; ch.data[(size_t) i] = {}; }
        }

        r.lanes = { kick, snare, ch, oh };

        // polyrhythmic / irregular lanes: glitch & zap & rim on odd cycles and tuplet rates
        const int cycA = tr.polyCycle;
        const int cycB = juce::jlimit (3, 15, (tr.polyCycle + 2 + (int) (f.entropy * 4)) | 1);
        auto glitch = lane (8, cycA, tr.subdivision > 4 ? tr.subdivision : 3);
        auto zap = lane (9, cycB, 4);
        auto rim = lane (7, 5 + 2 * (int) (f.roughness * 2), tr.subdivision == 3 ? 3 : 4);
        for (int i = 0; i < glitch.steps; ++i)
            if (rng.nextFloat() < 0.25f + 0.5f * s.complexity * f.contour[(size_t) (i * 3 % 64)])
                glitch.data[(size_t) i] = { 60 + rng.nextInt (60), 35 + rng.nextInt (60), rng.nextFloat() < f.chaos * 0.5f ? 3 : 1 };
        zap.data[0] = { 90, 70, 1 };
        for (int i = 1; i < zap.steps; ++i)
            if (rng.nextFloat() < 0.15f * s.complexity) zap.data[(size_t) i] = { 70, 40, 1 };
        rim.data[0] = { 75, 100, 1 };
        if (rim.steps > 3) rim.data[(size_t) (rim.steps / 2 + 1)] = { 60, 60, 1 };
        if (s.complexity > 0.3f) r.lanes.push_back (glitch);
        if (s.complexity > 0.5f) r.lanes.push_back (zap);
        if (tr.syncopation > 0.3f) r.lanes.push_back (rim);
        // sub reinforcement on kicks in deep regions
        if (f.insideFraction > 0.3)
        {
            auto sub = lane (11, steps, 4);
            for (int i = 0; i < steps; ++i)
                if (kick.data[(size_t) i].velocity > 0 && (i % barSteps) == 0) sub.data[(size_t) i] = { 100, 100, 1 };
            r.lanes.push_back (sub);
        }
        r.swing = juce::jlimit (0.0f, 0.6f, (float) (f.meanEscape * 0.4 * (1.0 - s.complexity * 0.5)));
        r.summary = "Breaks: template " + juce::String (tpl) + ", poly " + juce::String (cycA) + "/" + juce::String (cycB) + ", " + tr.describe();
        return r;
    }

    // ------------------------------------------------------------------ main entry
    Result generate (const Settings& s, const FractalFeatures& f, const Motif& previous, int generationIndex, float evolveAmount)
    {
        juce::Random rng ((juce::int64) ((juce::uint64) s.seed * 6364136223846793005ull ^ f.hash() ^ ((juce::uint64) generationIndex * 1442695040888963407ull) ^ (juce::uint64) (s.role * 977)));
        if (s.role == Breaks) return generateBreaks (s, f, rng);

        Result r;
        const auto tr = MusicalTraits::from (f, s);
        const auto degs = Scales::degrees (s.scale, s.customMask);
        const double totalBeats = s.bars * s.beatsPerBar;

        if (! previous.empty())
            r.motif = mutate (previous, s, f, juce::jlimit (0.0f, 1.0f, s.mutation * (0.35f + evolveAmount)), rng);
        else
            r.motif = deriveMotif (s, f, rng);

        const auto prog = progression (s, f, tr, rng);
        const double chordLen = s.beatsPerBar / (double) tr.chordsPerBar;
        auto chordAt = [&] (double beat) { return prog[(size_t) juce::jlimit (0, (int) prog.size() - 1, (int) (beat / chordLen))]; };

        switch (s.role)
        {
            case Melody:
            {
                for (double off = 0; off < totalBeats - 1e-6; off += r.motif.length)
                {
                    const int rep = (int) (off / r.motif.length);
                    // every other repetition is a light variation (keeps the motif recognisable)
                    Motif var = (rep % 2 == 1) ? mutate (r.motif, s, f, s.mutation * 0.5f, rng) : r.motif;
                    for (auto& n : var.notes)
                    {
                        const double t = off + n.onset;
                        if (t >= totalBeats) break;
                        const int transp = chordAt (t) * (tr.consonance > 0.5f ? 1 : 0);
                        const int pitch = Scales::degreeToPitch (n.degree + transp, s.root, s.octave, degs);
                        addNote (r.notes, pitch, t, juce::jmin (n.duration, totalBeats - t), n.velocity,
                                 s.complexity > 0.6f && n.velocity < 70 ? 0.7f : 1.0f);
                    }
                }
                break;
            }
            case Bass:
            {
                const int oct = juce::jmin (s.octave, 2);
                for (double off = 0; off < totalBeats - 1e-6; off += r.motif.length)
                    for (auto& n : r.motif.notes)
                    {
                        const double t = off + n.onset;
                        if (t >= totalBeats) break;
                        const int chordRoot = chordAt (t);
                        // bass favours chord roots & fifths, motif contour picks between them
                        int deg = chordRoot;
                        const int m = ((n.degree % 3) + 3) % 3;
                        if (m == 1) deg += 4; else if (m == 2 && s.complexity > 0.4f) deg += 7;
                        addNote (r.notes, Scales::degreeToPitch (deg, s.root, oct, degs), t, juce::jmin (n.duration * 0.9, totalBeats - t), n.velocity);
                    }
                break;
            }
            case Chords:
            {
                const bool stabs = s.density > 0.55f;
                for (size_t c = 0; c < prog.size(); ++c)
                {
                    const double t0 = c * chordLen;
                    if (t0 >= totalBeats) break;
                    auto pitches = chordPitches (prog[c], s, tr, degs, s.octave);
                    if (! stabs)
                    {
                        for (auto p : pitches) addNote (r.notes, p, t0, chordLen * 0.98, 70 + rng.nextInt (20));
                    }
                    else
                    {
                        // IDM stabs following the motif rhythm inside the chord span
                        for (auto& n : r.motif.notes)
                        {
                            const double t = t0 + std::fmod (n.onset, chordLen);
                            if (t >= t0 + chordLen || t >= totalBeats) continue;
                            if (rng.nextFloat() > 0.6f) continue;
                            for (auto p : pitches) addNote (r.notes, p, t, juce::jmin (0.3, n.duration), n.velocity - 10);
                        }
                        for (auto p : pitches) addNote (r.notes, p, t0, 0.25, 90);
                    }
                }
                break;
            }
            case Arp:
            {
                double t = 0.0; int idx = 0; int beat = 0;
                while (t < totalBeats - 1e-6)
                {
                    const int grid = gridForBeat (tr, beat, rng);
                    for (int k = 0; k < grid && t < totalBeats - 1e-6; ++k)
                    {
                        auto pitches = chordPitches (chordAt (t), s, tr, degs, s.octave);
                        const float c = f.contour[(size_t) (idx % 64)];
                        int which;
                        if (c < 0.33f) which = idx % (int) pitches.size();                                    // up
                        else if (c < 0.66f) which = (int) pitches.size() - 1 - (idx % (int) pitches.size());   // down
                        else which = rng.nextInt ((int) pitches.size());                                      // random
                        const int octJump = (f.energy[(size_t) (idx % 64)] > 0.7f) ? 12 : 0;
                        const float prob = juce::jlimit (0.2f, 1.0f, s.density * tr.densityScale + 0.3f);
                        addNote (r.notes, pitches[(size_t) which] + octJump, t, 1.0 / grid * 0.8, 70 + (int) (40 * f.energy[(size_t) ((idx + 5) % 64)]), prob);
                        t += 1.0 / grid; ++idx;
                    }
                    ++beat;
                    t = beat; // re-align at each beat
                }
                break;
            }
            case Texture:
            {
                const double minLen = s.beatsPerBar * 0.5;
                double t = 0.0; int i = 0;
                while (t < totalBeats - 1e-6)
                {
                    const double len = juce::jmin (totalBeats - t, minLen * (1.0 + 3.0 * f.energy[(size_t) (i % 64)]));
                    const int deg = contourToDegree (f.contour[(size_t) ((i * 11) % 64)], 14) + chordAt (t);
                    addNote (r.notes, Scales::degreeToPitch (deg, s.root, s.octave + 1, degs), t, len, 50 + rng.nextInt (30), 0.85f);
                    if (s.complexity > 0.5f)
                        addNote (r.notes, Scales::degreeToPitch (deg + 4, s.root, s.octave, degs), t + len * 0.25, len * 0.7, 40 + rng.nextInt (25), 0.6f);
                    t += len * (1.0 - 0.3 * s.density); ++i;
                }
                break;
            }
            default: break;
        }

        std::sort (r.notes.begin(), r.notes.end(), [] (auto& a, auto& b) { return a.start < b.start || (a.start == b.start && a.pitch < b.pitch); });
        r.summary = roleNames()[s.role] + ": " + juce::String ((int) r.notes.size()) + " notes, " + tr.describe();
        return r;
    }

    // ------------------------------------------------------------------ model glue
    Settings settingsFromTree (const juce::ValueTree& g, int beatsPerBar)
    {
        Settings s;
        s.role = (int) g.getProperty (IDs::role, 0);
        s.scale = (int) g.getProperty (IDs::scale, (int) Scales::Dorian);
        s.root = (int) g.getProperty (IDs::root, 9);
        s.customMask = (int) g.getProperty (IDs::customMask, 0xfff);
        s.seed = (juce::int64) g.getProperty (IDs::seed, 1);
        s.bars = juce::jlimit (1, 16, (int) g.getProperty (IDs::bars, 4));
        s.beatsPerBar = juce::jmax (1, beatsPerBar);
        s.density = (float) g.getProperty (IDs::density, 0.5f);
        s.complexity = (float) g.getProperty (IDs::complexity, 0.5f);
        s.mutation = (float) g.getProperty (IDs::mutation, 0.25f);
        s.octave = (int) g.getProperty (IDs::octave, 4);
        return s;
    }

    juce::ValueTree createGeneratorNode (juce::ValueTree project, int trackId, int role)
    {
        juce::ValueTree g (IDs::GEN);
        g.setProperty (IDs::id, Model::allocateId (project), nullptr);
        g.setProperty (IDs::name, roleNames()[role] + " Gen", nullptr);
        g.setProperty (IDs::track, trackId, nullptr);
        g.setProperty (IDs::role, role, nullptr);
        g.setProperty (IDs::scale, (int) Scales::Dorian, nullptr);
        g.setProperty (IDs::root, 9, nullptr);
        g.setProperty (IDs::customMask, 0xfff, nullptr);
        g.setProperty (IDs::seed, juce::Random::getSystemRandom().nextInt (99999), nullptr);
        g.setProperty (IDs::bars, 4, nullptr);
        g.setProperty (IDs::density, 0.5f, nullptr);
        g.setProperty (IDs::complexity, 0.5f, nullptr);
        g.setProperty (IDs::mutation, 0.25f, nullptr);
        g.setProperty (IDs::octave, role == Bass ? 2 : (role == Chords || role == Texture ? 3 : 4), nullptr);
        g.setProperty (IDs::live, false, nullptr);
        g.setProperty (IDs::lockPoint, false, nullptr);
        g.setProperty (IDs::motif, "", nullptr);
        g.setProperty (IDs::generation, 0, nullptr);
        g.setProperty (IDs::cx, -0.7436, nullptr);
        g.setProperty (IDs::cy, 0.1318, nullptr);
        return g;
    }

    void applyToClip (juce::ValueTree clip, const Result& r, juce::UndoManager* um)
    {
        if (clip[IDs::type].toString() == "pattern")
        {
            auto pat = Model::patternOf (clip);
            pat.setProperty (IDs::swing, r.swing, um);
            for (auto lane : pat)
            {
                lane.setProperty (IDs::steps, 16, um);
                lane.setProperty (IDs::rate, 4, um);
                lane.setProperty (IDs::data, Model::encodeSteps (std::vector<StepData> (16)), um);
            }
            for (auto& l : r.lanes)
            {
                auto lane = pat.getChildWithProperty (IDs::pad, l.pad);
                if (! lane.isValid()) continue;
                lane.setProperty (IDs::steps, l.steps, um);
                lane.setProperty (IDs::rate, l.rate, um);
                lane.setProperty (IDs::data, Model::encodeSteps (l.data), um);
            }
        }
        else
        {
            Model::setNotes (clip, r.notes, um);
        }
    }
}
