#include "Exporter.h"

namespace Exporter
{
    bool exportMidi (const juce::ValueTree& project, const juce::File& file, juce::String& error)
    {
        constexpr int ppq = 960;
        juce::MidiFile mf;
        mf.setTicksPerQuarterNote (ppq);
        const double tempo = project.getProperty (IDs::tempo, 120.0);

        juce::MidiMessageSequence meta;
        meta.addEvent (juce::MidiMessage::tempoMetaEvent ((int) std::round (60000000.0 / tempo)), 0);
        meta.addEvent (juce::MidiMessage::timeSignatureMetaEvent ((int) project.getProperty (IDs::tsNum, 4), (int) project.getProperty (IDs::tsDen, 4)), 0);
        meta.addEvent (juce::MidiMessage::textMetaEvent (3, project[IDs::name].toString()), 0);
        mf.addTrack (meta);

        int channel = 1;
        for (auto t : project.getChildWithName (IDs::TRACKS))
        {
            const auto type = t[IDs::type].toString();
            if (type == "audio") continue;
            const int ch = type == "drums" ? 10 : channel;
            if (type != "drums") channel = channel % 9 + 1;
            juce::MidiMessageSequence seq;
            seq.addEvent (juce::MidiMessage::textMetaEvent (3, t[IDs::name].toString()), 0);
            for (auto& e : Snap::eventsForTrack (t))
            {
                const double tick = e.beat * ppq;
                if (e.velocity > 0) seq.addEvent (juce::MidiMessage::noteOn (ch, e.note, juce::jlimit (0.01f, 1.0f, e.velocity)), tick);
                else seq.addEvent (juce::MidiMessage::noteOff (ch, e.note), tick);
            }
            seq.updateMatchedPairs();
            mf.addTrack (seq);
        }

        file.deleteFile();
        juce::FileOutputStream os (file);
        if (! os.openedOk()) { error = "Cannot open " + file.getFullPathName(); return false; }
        if (! mf.writeTo (os, 1)) { error = "MIDI write failed"; return false; }
        return true;
    }

    bool exportAudio (Engine& engine, const juce::ValueTree& project, const AudioJob& job,
                      std::function<bool (float)> progress, juce::String& error, juce::StringArray& written)
    {
        Engine::RenderOptions o;
        o.sampleRate = job.sampleRate;
        o.bitDepth = job.bits;
        if (job.loopOnly)
        {
            o.startBeat = project.getProperty (IDs::loopStart, 0.0);
            o.endBeat = project.getProperty (IDs::loopEnd, 16.0);
        }
        else
        {
            o.startBeat = 0.0;
            o.endBeat = juce::jmax (1.0, Model::projectEndBeat (project));
        }

        if (! job.stems)
        {
            o.file = job.folderOrFile;
            if (! engine.renderOffline (o, progress, error)) return false;
            written.add (o.file.getFullPathName());
            return true;
        }

        auto folder = job.folderOrFile;
        folder.createDirectory();
        auto tracks = project.getChildWithName (IDs::TRACKS);
        const int count = tracks.getNumChildren();
        int index = 0;
        for (auto t : tracks)
        {
            o.onlyTrackId = t[IDs::id];
            auto name = juce::File::createLegalFileName (juce::String (index + 1).paddedLeft ('0', 2) + "_" + t[IDs::name].toString());
            o.file = folder.getChildFile (name + ".wav");
            const int idx = index;
            auto sub = [&] (float p) { return progress ? progress ((idx + p) / (float) count) : true; };
            if (! engine.renderOffline (o, sub, error)) return false;
            written.add (o.file.getFullPathName());
            ++index;
        }
        return true;
    }
}
