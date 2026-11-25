#include "ProjectModel.h"
#include "../Live/LiveSequencer.h"

namespace Model
{
    juce::Colour trackColour (int index)
    {
        static const juce::uint32 cols[] = { 0xff5ad1ff, 0xffff6b9d, 0xffb38cff, 0xff6bffb8, 0xffffc85a,
                                             0xff5a7dff, 0xffff8a5a, 0xff9dff5a, 0xffd15aff, 0xff5affea };
        return juce::Colour (cols[(size_t) (index % 10)]);
    }

    int allocateId (juce::ValueTree project)
    {
        const int idv = (int) project.getProperty (IDs::nextId, 1);
        project.setProperty (IDs::nextId, idv + 1, nullptr); // ids are never undone (keeps them unique)
        return idv;
    }

    juce::ValueTree tracks (juce::ValueTree p) { return p.getOrCreateChildWithName (IDs::TRACKS, nullptr); }
    juce::ValueTree buses (juce::ValueTree p) { return p.getOrCreateChildWithName (IDs::BUSES, nullptr); }
    juce::ValueTree master (juce::ValueTree p) { return p.getOrCreateChildWithName (IDs::MASTER, nullptr); }

    juce::ValueTree createInsertsNode()
    {
        juce::ValueTree ins (IDs::INSERTS);
        for (int i = 0; i < MixP::numInserts; ++i)
        {
            juce::ValueTree s (IDs::SLOT);
            s.setProperty (IDs::fx, 0, nullptr);
            s.setProperty (IDs::bypass, false, nullptr);
            for (int k = 0; k < 4; ++k)
                s.setProperty (juce::Identifier ("p" + juce::String (k)), 0.5f, nullptr);
            ins.appendChild (s, nullptr);
        }
        return ins;
    }

    juce::ValueTree createSynthNode()
    {
        juce::ValueTree s (IDs::SYNTH);
        for (auto& d : SynthP::defs())
            s.setProperty (d.id, d.defV, nullptr);
        return s;
    }

    juce::ValueTree createPadNode (int index)
    {
        juce::ValueTree pad (IDs::PAD);
        pad.setProperty (IDs::id, index, nullptr);
        static const char* names[] = { "Kick", "Snare", "Closed Hat", "Open Hat", "Clap", "Low Tom",
                                       "Hi Tom", "Rim", "Glitch", "Zap", "Noise", "Sub" };
        pad.setProperty (IDs::name, names[index % 12], nullptr);
        pad.setProperty (IDs::sample, "", nullptr);
        pad.setProperty (IDs::synthKind, index % 12, nullptr);
        for (auto& d : PadP::defs())
            pad.setProperty (d.id, d.defV, nullptr);
        if (index == 2) pad.setProperty ("choke", 1, nullptr), pad.setProperty ("decay", 0.12f, nullptr);
        if (index == 3) pad.setProperty ("choke", 1, nullptr), pad.setProperty ("decay", 0.6f, nullptr);
        return pad;
    }

    juce::ValueTree createDrumKitNode()
    {
        juce::ValueTree kit (IDs::DRUMKIT);
        for (int i = 0; i < PadP::numPads; ++i)
            kit.appendChild (createPadNode (i), nullptr);
        return kit;
    }

    static void initChannelProps (juce::ValueTree ch, float volDb)
    {
        ch.setProperty (IDs::volume, volDb, nullptr);
        ch.setProperty (IDs::pan, 0.0f, nullptr);
        ch.setProperty (IDs::mute, false, nullptr);
        ch.setProperty (IDs::solo, false, nullptr);
        ch.setProperty (IDs::output, -1, nullptr);
        for (int i = 0; i < MixP::numSends; ++i)
            ch.setProperty (juce::Identifier ("send" + juce::String (i)), 0.0f, nullptr);
        ch.appendChild (createInsertsNode(), nullptr);
    }

    juce::ValueTree addTrack (juce::ValueTree project, const juce::String& type, const juce::String& name, juce::UndoManager* um)
    {
        auto ts = tracks (project);
        juce::ValueTree t (IDs::TRACK);
        t.setProperty (IDs::id, allocateId (project), nullptr);
        t.setProperty (IDs::name, name, nullptr);
        t.setProperty (IDs::type, type, nullptr);
        t.setProperty (IDs::colour, (juce::int64) trackColour (ts.getNumChildren()).getARGB(), nullptr);
        t.setProperty (IDs::arm, false, nullptr);
        t.setProperty (IDs::height, 64, nullptr);
        t.setProperty (IDs::showAuto, false, nullptr);
        initChannelProps (t, -6.0f);
        t.appendChild (juce::ValueTree (IDs::CLIPS), nullptr);
        t.appendChild (juce::ValueTree (IDs::AUTOMATION), nullptr);
        if (type == "synth") t.appendChild (createSynthNode(), nullptr);
        if (type == "drums") t.appendChild (createDrumKitNode(), nullptr);
        ts.appendChild (t, um);
        return t;
    }

    juce::ValueTree addBus (juce::ValueTree project, const juce::String& name, juce::UndoManager* um)
    {
        auto bs = buses (project);
        juce::ValueTree b (IDs::BUS);
        b.setProperty (IDs::id, bs.getNumChildren(), nullptr);
        b.setProperty (IDs::name, name, nullptr);
        initChannelProps (b, 0.0f);
        bs.appendChild (b, um);
        return b;
    }

    juce::ValueTree findTrack (juce::ValueTree project, int trackId)
    {
        return tracks (project).getChildWithProperty (IDs::id, trackId);
    }

    juce::ValueTree createEmptyProject()
    {
        juce::ValueTree p (IDs::PROJECT);
        p.setProperty (IDs::version, fileVersion, nullptr);
        p.setProperty (IDs::name, "Untitled", nullptr);
        p.setProperty (IDs::tempo, 128.0, nullptr);
        p.setProperty (IDs::tsNum, 4, nullptr);
        p.setProperty (IDs::tsDen, 4, nullptr);
        p.setProperty (IDs::loopOn, false, nullptr);     // timeline selection (FL style), off by default
        p.setProperty (IDs::loopStart, 0.0, nullptr);
        p.setProperty (IDs::loopEnd, 0.0, nullptr);
        p.setProperty (IDs::songMode, true, nullptr);
        p.setProperty (IDs::nextId, 1, nullptr);
        p.setProperty (IDs::metronome, false, nullptr);
        p.appendChild (juce::ValueTree (IDs::TRACKS), nullptr);
        p.appendChild (juce::ValueTree (IDs::BUSES), nullptr);
        juce::ValueTree m (IDs::MASTER);
        m.setProperty (IDs::volume, 0.0f, nullptr);
        m.setProperty (IDs::pan, 0.0f, nullptr);
        m.appendChild (createInsertsNode(), nullptr);
        p.appendChild (m, nullptr);
        p.appendChild (juce::ValueTree (IDs::GENERATORS), nullptr);
        juce::ValueTree fr (IDs::FRACTAL);
        fr.setProperty (IDs::cx, -0.745, nullptr);
        fr.setProperty (IDs::cy, 0.125, nullptr);
        fr.setProperty (IDs::scaleF, 0.12, nullptr);
        fr.setProperty (IDs::iters, 400, nullptr);
        fr.setProperty (IDs::palette, 0, nullptr);
        fr.setProperty (IDs::probeX, -0.7436, nullptr);
        fr.setProperty (IDs::probeY, 0.1318, nullptr);
        p.appendChild (fr, nullptr);
        p.appendChild (juce::ValueTree (IDs::BOOKMARKS), nullptr);
        p.appendChild (juce::ValueTree (IDs::PATHS), nullptr);
        auto live = Live::createLiveNode();
        live.setProperty (IDs::liveOn, false, nullptr);
        p.appendChild (live, nullptr);
        return p;
    }

    juce::ValueTree liveNode (juce::ValueTree project)
    {
        auto l = project.getChildWithName (IDs::LIVE);
        if (! l.isValid())
        {
            l = Live::createLiveNode();
            l.setProperty (IDs::liveOn, false, nullptr);
            project.appendChild (l, nullptr);
        }
        return l;
    }

    juce::StringArray synthPresetNames() { return { "Init", "Bass", "Acid Bass", "Lead", "Soft Pad", "Glass Pad", "Pluck", "Keys", "Bell", "Grain Cloud" }; }

    void applySynthPreset (juce::ValueTree s, const juce::String& preset, juce::UndoManager* um)
    {
        // start from defaults with an empty mod matrix, then apply the preset
        for (auto& d : SynthP::defs()) s.setProperty (d.id, d.defV, um);
        for (int i = 0; i < 6; ++i)
        {
            s.setProperty ("m" + juce::String (i) + "src", 0.0f, um);
            s.setProperty ("m" + juce::String (i) + "dst", 0.0f, um);
            s.setProperty ("m" + juce::String (i) + "amt", 0.0f, um);
        }
        auto set = [&] (const char* id, float v) { s.setProperty (id, v, um); };
        auto mod = [&] (int slot, int src, int dst, float amt)
        {
            s.setProperty ("m" + juce::String (slot) + "src", (float) src, um);
            s.setProperty ("m" + juce::String (slot) + "dst", (float) dst, um);
            s.setProperty ("m" + juce::String (slot) + "amt", amt, um);
        };
        using namespace SynthP;
        set ("osc2Semi", 0.0f); set ("osc2Detune", 7.0f); set ("unisonDetune", 0.0f);
        if (preset == "Bass")
        {
            set ("engine", 0); set ("osc1Wave", 0); set ("osc2Wave", 1); set ("oscMix", 0.3f); set ("subLevel", 0.7f);
            set ("cutoff", 420.0f); set ("reso", 0.18f); set ("filterEnv", 0.45f); set ("keyTrack", 0.4f); set ("drive", 0.25f);
            set ("ampA", 0.002f); set ("ampD", 0.3f); set ("ampS", 0.65f); set ("ampR", 0.08f);
            set ("modA", 0.001f); set ("modD", 0.18f); set ("modS", 0.0f); set ("modR", 0.1f);
            set ("gain", -4.0f);
        }
        else if (preset == "Acid Bass")
        {
            set ("engine", 0); set ("osc1Wave", 0); set ("oscMix", 0.0f); set ("subLevel", 0.3f);
            set ("cutoff", 300.0f); set ("reso", 0.78f); set ("filterEnv", 0.75f); set ("keyTrack", 0.3f); set ("drive", 0.45f);
            set ("ampA", 0.002f); set ("ampD", 0.25f); set ("ampS", 0.5f); set ("ampR", 0.06f);
            set ("modA", 0.001f); set ("modD", 0.16f); set ("modS", 0.0f); set ("modR", 0.08f);
            set ("glide", 0.04f); set ("gain", -7.0f);
            mod (0, srcFractalA, dstCutoff, 0.25f);
        }
        else if (preset == "Lead")
        {
            set ("engine", 0); set ("osc1Wave", 0); set ("osc2Wave", 0); set ("oscMix", 0.5f); set ("osc2Detune", 12.0f); set ("unisonDetune", 0.35f);
            set ("subLevel", 0.0f); set ("cutoff", 2600.0f); set ("reso", 0.22f); set ("filterEnv", 0.35f); set ("keyTrack", 0.5f);
            set ("ampA", 0.004f); set ("ampD", 0.35f); set ("ampS", 0.55f); set ("ampR", 0.25f);
            set ("modA", 0.002f); set ("modD", 0.3f); set ("modS", 0.1f); set ("modR", 0.3f);
            set ("lfo1Rate", 5.2f); set ("gain", -9.0f);
            mod (0, srcLfo1, dstPitch, 0.006f);
            mod (1, srcVelocity, dstCutoff, 0.25f);
        }
        else if (preset == "Soft Pad")
        {
            set ("engine", 0); set ("osc1Wave", 0); set ("osc2Wave", 2); set ("oscMix", 0.45f); set ("osc2Detune", 9.0f); set ("unisonDetune", 0.6f);
            set ("subLevel", 0.0f); set ("cutoff", 1500.0f); set ("reso", 0.1f); set ("filterEnv", 0.15f); set ("keyTrack", 0.3f);
            set ("ampA", 0.7f); set ("ampD", 1.0f); set ("ampS", 0.8f); set ("ampR", 2.2f);
            set ("modA", 1.2f); set ("modD", 1.5f); set ("modS", 0.5f); set ("modR", 2.0f);
            set ("lfo1Rate", 0.25f); set ("gain", -14.0f);
            mod (0, srcLfo1, dstCutoff, 0.12f);
        }
        else if (preset == "Glass Pad")
        {
            set ("engine", 2); set ("wtPos", 0.25f); set ("subLevel", 0.0f);
            set ("cutoff", 4200.0f); set ("reso", 0.12f); set ("filterEnv", 0.1f);
            set ("ampA", 0.9f); set ("ampD", 1.0f); set ("ampS", 0.8f); set ("ampR", 2.6f);
            set ("lfo1Rate", 0.15f); set ("gain", -15.0f);
            mod (0, srcLfo1, dstWtPos, 0.25f);
            mod (1, srcFractalA, dstWtPos, 0.2f);
        }
        else if (preset == "Pluck")
        {
            set ("engine", 1); set ("fmRatio", 2.0f); set ("fmIndex", 1.6f); set ("fmFeedback", 0.05f); set ("fmCarrier", 1.0f); set ("subLevel", 0.0f);
            set ("cutoff", 5000.0f); set ("reso", 0.1f); set ("filterEnv", 0.2f);
            set ("ampA", 0.001f); set ("ampD", 0.22f); set ("ampS", 0.0f); set ("ampR", 0.18f);
            set ("modA", 0.001f); set ("modD", 0.12f); set ("modS", 0.0f); set ("modR", 0.1f);
            set ("gain", -9.0f);
            mod (0, srcModEnv, dstFmIndex, 0.35f);
        }
        else if (preset == "Keys")
        {
            set ("engine", 1); set ("fmRatio", 1.0f); set ("fmIndex", 1.2f); set ("fmFeedback", 0.0f); set ("fmCarrier", 1.0f); set ("subLevel", 0.0f);
            set ("cutoff", 6000.0f); set ("reso", 0.05f); set ("filterEnv", 0.0f);
            set ("ampA", 0.002f); set ("ampD", 1.2f); set ("ampS", 0.25f); set ("ampR", 0.5f);
            set ("modA", 0.001f); set ("modD", 0.6f); set ("modS", 0.1f); set ("modR", 0.4f);
            set ("gain", -10.0f);
            mod (0, srcModEnv, dstFmIndex, 0.2f);
            mod (1, srcVelocity, dstFmIndex, 0.15f);
        }
        else if (preset == "Bell")
        {
            set ("engine", 1); set ("fmRatio", 3.5f); set ("fmIndex", 2.5f); set ("fmFeedback", 0.0f); set ("fmCarrier", 1.0f); set ("subLevel", 0.0f);
            set ("cutoff", 9000.0f); set ("reso", 0.0f); set ("filterEnv", 0.0f);
            set ("ampA", 0.001f); set ("ampD", 1.8f); set ("ampS", 0.0f); set ("ampR", 1.2f);
            set ("modA", 0.001f); set ("modD", 1.0f); set ("modS", 0.0f); set ("modR", 0.8f);
            set ("gain", -12.0f);
            mod (0, srcModEnv, dstFmIndex, 0.3f);
        }
        else if (preset == "Grain Cloud")
        {
            set ("engine", 3); set ("grainSize", 120.0f); set ("grainDensity", 24.0f); set ("grainPos", 0.4f); set ("grainSpray", 0.35f);
            set ("cutoff", 5000.0f); set ("reso", 0.1f); set ("filterEnv", 0.0f);
            set ("ampA", 0.4f); set ("ampD", 0.8f); set ("ampS", 0.7f); set ("ampR", 1.5f);
            set ("lfo1Rate", 0.2f); set ("gain", -10.0f);
            mod (0, srcLfo1, dstGrainPos, 0.3f);
        }
        else // Init: clean saw
        {
            set ("engine", 0); set ("osc1Wave", 0); set ("oscMix", 0.0f); set ("subLevel", 0.0f);
            set ("cutoff", 3000.0f); set ("reso", 0.1f); set ("filterEnv", 0.2f);
            set ("gain", -10.0f);
        }
        s.setProperty ("preset", preset, um);
    }

    juce::ValueTree createDefaultProject()
    {
        auto p = createEmptyProject();
        auto rev = addBus (p, "Reverb Bus", nullptr);
        auto ins = rev.getChildWithName (IDs::INSERTS).getChild (0);
        ins.setProperty (IDs::fx, (int) FxP::Reverb, nullptr);
        for (int k = 0; k < 4; ++k) ins.setProperty (juce::Identifier ("p" + juce::String (k)), FxP::defaultValue (FxP::Reverb, k), nullptr);
        ins.setProperty (IDs::p3, 1.0f, nullptr);
        auto del = addBus (p, "Delay Bus", nullptr);
        auto dins = del.getChildWithName (IDs::INSERTS).getChild (0);
        dins.setProperty (IDs::fx, (int) FxP::Delay, nullptr);
        for (int k = 0; k < 4; ++k) dins.setProperty (juce::Identifier ("p" + juce::String (k)), FxP::defaultValue (FxP::Delay, k), nullptr);
        dins.setProperty (IDs::p3, 1.0f, nullptr);

        // one track per live part: open the app and it plays
        auto drums = addTrack (p, "drums", "Drums", nullptr);
        drums.setProperty (IDs::volume, -5.0f, nullptr);
        drums.setProperty (IDs::livePart, (int) Live::Drums, nullptr);
        auto bass = addTrack (p, "synth", "Bass", nullptr);
        applySynthPreset (bass.getChildWithName (IDs::SYNTH), "Bass", nullptr);
        bass.setProperty (IDs::livePart, (int) Live::Bass, nullptr);
        bass.setProperty (IDs::volume, -5.0f, nullptr);
        auto chords = addTrack (p, "synth", "Chords", nullptr);
        applySynthPreset (chords.getChildWithName (IDs::SYNTH), "Soft Pad", nullptr);
        chords.setProperty (IDs::livePart, (int) Live::Chords, nullptr);
        chords.setProperty (IDs::send0, 0.4f, nullptr);
        chords.setProperty (IDs::volume, -6.0f, nullptr);
        auto lead = addTrack (p, "synth", "Lead", nullptr);
        applySynthPreset (lead.getChildWithName (IDs::SYNTH), "Lead", nullptr);
        lead.setProperty (IDs::livePart, (int) Live::Lead, nullptr);
        lead.setProperty (IDs::send1, 0.25f, nullptr);
        lead.setProperty (IDs::send0, 0.15f, nullptr);
        lead.setProperty (IDs::volume, -7.0f, nullptr);
        auto arp = addTrack (p, "synth", "Arp", nullptr);
        applySynthPreset (arp.getChildWithName (IDs::SYNTH), "Pluck", nullptr);
        arp.setProperty (IDs::livePart, (int) Live::Arp, nullptr);
        arp.setProperty (IDs::send1, 0.3f, nullptr);
        arp.setProperty (IDs::volume, -9.0f, nullptr);
        addTrack (p, "audio", "Audio 1", nullptr);
        liveNode (p).setProperty (IDs::liveOn, true, nullptr);
        return p;
    }

    // ------------------------------------------------------------------ clips
    static juce::ValueTree makeClip (juce::ValueTree track, const juce::String& type, double start, double length)
    {
        auto project = track.getParent().getParent();
        juce::ValueTree c (IDs::CLIP);
        c.setProperty (IDs::id, project.isValid() ? allocateId (project) : juce::Random::getSystemRandom().nextInt(), nullptr);
        c.setProperty (IDs::type, type, nullptr);
        c.setProperty (IDs::start, start, nullptr);
        c.setProperty (IDs::length, length, nullptr);
        c.setProperty (IDs::name, track[IDs::name].toString(), nullptr);
        return c;
    }

    juce::ValueTree addMidiClip (juce::ValueTree track, double start, double length, juce::UndoManager* um)
    {
        auto c = makeClip (track, "midi", start, length);
        track.getOrCreateChildWithName (IDs::CLIPS, nullptr).appendChild (c, um);
        return c;
    }

    juce::ValueTree addPatternClip (juce::ValueTree track, double start, double length, juce::UndoManager* um)
    {
        auto c = makeClip (track, "pattern", start, length);
        juce::ValueTree pat (IDs::PATTERN);
        pat.setProperty (IDs::swing, 0.0f, nullptr);
        for (int i = 0; i < PadP::numPads; ++i)
        {
            juce::ValueTree lane (IDs::LANE);
            lane.setProperty (IDs::pad, i, nullptr);
            lane.setProperty (IDs::steps, 16, nullptr);
            lane.setProperty (IDs::rate, 4, nullptr);
            lane.setProperty (IDs::data, encodeSteps (std::vector<StepData> (16)), nullptr);
            pat.appendChild (lane, nullptr);
        }
        c.appendChild (pat, nullptr);
        c.setProperty (IDs::name, "Pattern", nullptr);
        track.getOrCreateChildWithName (IDs::CLIPS, nullptr).appendChild (c, um);
        return c;
    }

    juce::ValueTree addAudioClip (juce::ValueTree track, const juce::File& f, double start, double lengthBeats, juce::UndoManager* um)
    {
        auto c = makeClip (track, "audio", start, lengthBeats);
        c.setProperty (IDs::file, f.getFullPathName(), nullptr);
        c.setProperty (IDs::offset, 0.0, nullptr);
        c.setProperty (IDs::gain, 0.0f, nullptr);
        c.setProperty (IDs::fadeIn, 0.0, nullptr);
        c.setProperty (IDs::fadeOut, 0.0, nullptr);
        c.setProperty (IDs::name, f.getFileNameWithoutExtension(), nullptr);
        track.getOrCreateChildWithName (IDs::CLIPS, nullptr).appendChild (c, um);
        return c;
    }

    juce::ValueTree duplicateClip (juce::ValueTree clip, double newStart, juce::UndoManager* um)
    {
        auto copy = clip.createCopy();
        auto project = trackOfClip (clip).getParent().getParent();
        copy.setProperty (IDs::id, allocateId (project), nullptr);
        copy.setProperty (IDs::start, newStart, nullptr);
        clip.getParent().appendChild (copy, um);
        return copy;
    }

    juce::ValueTree trackOfClip (juce::ValueTree clip) { return clip.getParent().getParent(); }

    double freeSpot (const juce::ValueTree& track, double start, double length, double bpb)
    {
        bool moved = true;
        for (int guard = 0; moved && guard < 1024; ++guard)
        {
            moved = false;
            for (auto c : track.getChildWithName (IDs::CLIPS))
            {
                const double cs = c[IDs::start], ce = cs + (double) c[IDs::length];
                if (start < ce - 1e-6 && start + length > cs + 1e-6)
                {
                    start = std::ceil (ce / bpb - 1e-9) * bpb;
                    moved = true;
                }
            }
        }
        return start;
    }

    // ------------------------------------------------------------------ notes
    NoteData readNote (const juce::ValueTree& n)
    {
        NoteData d;
        d.pitch = (int) n.getProperty (IDs::p, 60);
        d.start = (double) n.getProperty (IDs::s, 0.0);
        d.length = (double) n.getProperty (IDs::l, 0.25);
        d.velocity = (int) n.getProperty (IDs::v, 100);
        d.probability = (float) n.getProperty (IDs::pr, 1.0f);
        return d;
    }

    void writeNote (juce::ValueTree n, const NoteData& d, juce::UndoManager* um)
    {
        n.setProperty (IDs::p, juce::jlimit (0, 127, d.pitch), um);
        n.setProperty (IDs::s, juce::jmax (0.0, d.start), um);
        n.setProperty (IDs::l, juce::jmax (1.0 / 64.0, d.length), um);
        n.setProperty (IDs::v, juce::jlimit (1, 127, d.velocity), um);
        n.setProperty (IDs::pr, juce::jlimit (0.0f, 1.0f, d.probability), um);
    }

    juce::ValueTree addNote (juce::ValueTree clip, const NoteData& d, juce::UndoManager* um)
    {
        juce::ValueTree n (IDs::NOTE);
        writeNote (n, d, nullptr);
        clip.appendChild (n, um);
        return n;
    }

    void setNotes (juce::ValueTree clip, const std::vector<NoteData>& notes, juce::UndoManager* um)
    {
        for (int i = clip.getNumChildren(); --i >= 0;)
            if (clip.getChild (i).hasType (IDs::NOTE))
                clip.removeChild (i, um);
        for (auto& n : notes)
            addNote (clip, n, um);
    }

    std::vector<NoteData> readNotes (const juce::ValueTree& clip)
    {
        std::vector<NoteData> out;
        for (auto c : clip)
            if (c.hasType (IDs::NOTE)) out.push_back (readNote (c));
        return out;
    }

    // ------------------------------------------------------------------ pattern
    juce::ValueTree patternOf (juce::ValueTree clip) { return clip.getChildWithName (IDs::PATTERN); }

    juce::ValueTree laneFor (juce::ValueTree clip, int pad)
    {
        return patternOf (clip).getChildWithProperty (IDs::pad, pad);
    }

    juce::String encodeSteps (const std::vector<StepData>& steps)
    {
        juce::StringArray parts;
        for (auto& s : steps)
            parts.add (juce::String (s.velocity) + ":" + juce::String (s.probability) + ":" + juce::String (s.ratchet));
        return parts.joinIntoString (",");
    }

    std::vector<StepData> decodeSteps (const juce::String& str, int numSteps)
    {
        std::vector<StepData> out ((size_t) juce::jmax (1, numSteps));
        auto parts = juce::StringArray::fromTokens (str, ",", "");
        for (int i = 0; i < (int) out.size() && i < parts.size(); ++i)
        {
            auto f = juce::StringArray::fromTokens (parts[i], ":", "");
            out[(size_t) i].velocity = juce::jlimit (0, 127, f[0].getIntValue());
            out[(size_t) i].probability = f.size() > 1 ? juce::jlimit (0, 100, f[1].getIntValue()) : 100;
            out[(size_t) i].ratchet = f.size() > 2 ? juce::jlimit (1, 4, f[2].getIntValue()) : 1;
        }
        return out;
    }

    std::vector<StepData> readSteps (const juce::ValueTree& lane)
    {
        return decodeSteps (lane[IDs::data].toString(), (int) lane.getProperty (IDs::steps, 16));
    }

    void writeSteps (juce::ValueTree lane, const std::vector<StepData>& steps, juce::UndoManager* um)
    {
        lane.setProperty (IDs::data, encodeSteps (steps), um);
    }

    // ------------------------------------------------------------------ automation
    juce::ValueTree automationLane (juce::ValueTree track, const juce::String& paramId, bool create, juce::UndoManager* um)
    {
        auto au = track.getOrCreateChildWithName (IDs::AUTOMATION, nullptr);
        auto lane = au.getChildWithProperty (IDs::param, paramId);
        if (! lane.isValid() && create)
        {
            lane = juce::ValueTree (IDs::ALANE);
            lane.setProperty (IDs::param, paramId, nullptr);
            au.appendChild (lane, um);
        }
        return lane;
    }

    float getFloat (const juce::ValueTree& v, const juce::Identifier& id, float def)
    {
        return (float) v.getProperty (id, def);
    }

    // ------------------------------------------------------------------ persistence
    bool save (const juce::ValueTree& project, const juce::File& f)
    {
        if (auto xml = project.createXml())
            return xml->writeTo (f);
        return false;
    }

    // projects saved before Live mode have no tracks assigned to the live parts, so nothing would play:
    // give each part a track (reuse existing drum / synth tracks, add the missing ones)
    void upgradeForLive (juce::ValueTree v)
    {
        for (auto t : v.getChildWithName (IDs::TRACKS))
            if ((int) t.getProperty (IDs::livePart, 0) > 0) return;
        liveNode (v).setProperty (IDs::liveOn, true, nullptr);
        bool used[Live::NumParts] = {};
        const int synthOrder[] = { Live::Bass, Live::Chords, Live::Lead, Live::Arp };
        int nextSynth = 0;
        for (auto t : v.getChildWithName (IDs::TRACKS))
        {
            const auto type = t[IDs::type].toString();
            if (type == "drums" && ! used[Live::Drums]) { t.setProperty (IDs::livePart, (int) Live::Drums, nullptr); used[Live::Drums] = true; }
            else if (type == "synth" && nextSynth < 4) { const int part = synthOrder[nextSynth++]; t.setProperty (IDs::livePart, part, nullptr); used[part] = true; }
        }
        static const char* presets[] = { "", "", "Bass", "Soft Pad", "Lead", "Pluck" };
        for (int part = 1; part < Live::NumParts; ++part)
        {
            if (used[part]) continue;
            auto t = addTrack (v, part == Live::Drums ? "drums" : "synth", Live::partNames()[part], nullptr);
            if (part != Live::Drums) applySynthPreset (t.getChildWithName (IDs::SYNTH), presets[part], nullptr);
            t.setProperty (IDs::livePart, part, nullptr);
        }
    }

    juce::ValueTree load (const juce::File& f, juce::String& error)
    {
        auto xml = juce::XmlDocument::parse (f);
        if (xml == nullptr) { error = "Could not parse project file"; return {}; }
        auto v = juce::ValueTree::fromXml (*xml);
        if (! v.hasType (IDs::PROJECT)) { error = "Not a MANDELBROT project"; return {}; }
        // forward-fill any missing sections
        for (auto id : { IDs::TRACKS, IDs::BUSES, IDs::GENERATORS, IDs::BOOKMARKS, IDs::PATHS })
            v.getOrCreateChildWithName (id, nullptr);
        if (! v.getChildWithName (IDs::FRACTAL).isValid())
            v.appendChild (createEmptyProject().getChildWithName (IDs::FRACTAL).createCopy(), nullptr);
        if (! v.getChildWithName (IDs::MASTER).isValid())
            v.appendChild (createEmptyProject().getChildWithName (IDs::MASTER).createCopy(), nullptr);
        liveNode (v);
        if (! v.hasProperty (IDs::songMode)) v.setProperty (IDs::songMode, true, nullptr);
        upgradeForLive (v);
        return v;
    }

    double projectEndBeat (const juce::ValueTree& project)
    {
        double end = 0.0;
        for (auto t : project.getChildWithName (IDs::TRACKS))
            for (auto c : t.getChildWithName (IDs::CLIPS))
                end = juce::jmax (end, (double) c[IDs::start] + (double) c[IDs::length]);
        return end;
    }
}
