#pragma once
#include "Engine.h"

namespace Exporter
{
    /** Writes a multitrack Standard MIDI File (type 1, 960 PPQ). */
    bool exportMidi (const juce::ValueTree& project, const juce::File& file, juce::String& error);

    struct AudioJob
    {
        juce::File folderOrFile;   // file for mixdown, folder for stems
        double sampleRate = 44100.0;
        int bits = 24;
        bool stems = false;
        bool loopOnly = false;
    };

    /** Blocking; run on a background thread. */
    bool exportAudio (Engine& engine, const juce::ValueTree& project, const AudioJob& job,
                      std::function<bool (float)> progress, juce::String& error, juce::StringArray& writtenFiles);
}
