#include "ProjectModel.h"

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
        p.setProperty (IDs::loopOn, true, nullptr);
        p.setProperty (IDs::loopStart, 0.0, nullptr);
        p.setProperty (IDs::loopEnd, 16.0, nullptr);
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
        fr.setProperty (IDs::cx, -0.743643887, nullptr);
        fr.setProperty (IDs::cy, 0.131825904, nullptr);
        fr.setProperty (IDs::scaleF, 0.02, nullptr);
        fr.setProperty (IDs::iters, 400, nullptr);
        fr.setProperty (IDs::palette, 0, nullptr);
        fr.setProperty (IDs::probeX, -0.7436, nullptr);
        fr.setProperty (IDs::probeY, 0.1318, nullptr);
        p.appendChild (fr, nullptr);
        p.appendChild (juce::ValueTree (IDs::BOOKMARKS), nullptr);
        p.appendChild (juce::ValueTree (IDs::PATHS), nullptr);
        return p;
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

        auto drums = addTrack (p, "drums", "Breaks", nullptr);
        drums.setProperty (IDs::volume, -4.0f, nullptr);
        auto bass = addTrack (p, "synth", "Bass", nullptr);
        auto bs = bass.getChildWithName (IDs::SYNTH);
        bs.setProperty ("cutoff", 600.0f, nullptr);
        bs.setProperty ("subLevel", 0.6f, nullptr);
        bs.setProperty ("ampR", 0.15f, nullptr);
        auto lead = addTrack (p, "synth", "Lead", nullptr);
        auto ls = lead.getChildWithName (IDs::SYNTH);
        ls.setProperty ("engine", 1, nullptr);
        lead.setProperty (IDs::send1, 0.3f, nullptr);
        auto pad = addTrack (p, "synth", "Pad", nullptr);
        auto ps = pad.getChildWithName (IDs::SYNTH);
        ps.setProperty ("engine", 2, nullptr);
        ps.setProperty ("ampA", 0.6f, nullptr);
        ps.setProperty ("ampR", 2.0f, nullptr);
        pad.setProperty (IDs::send0, 0.45f, nullptr);
        pad.setProperty (IDs::volume, -10.0f, nullptr);
        addTrack (p, "audio", "Audio 1", nullptr);
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
