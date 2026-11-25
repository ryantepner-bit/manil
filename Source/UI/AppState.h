#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "../Audio/Engine.h"
#include "../Fractal/MandelMath.h"

/** Shared, non-persistent application state handed to every workspace. */
struct AppState
{
    explicit AppState (Engine& e) : engine (e) {}

    Engine& engine;
    juce::ValueTree project;
    juce::UndoManager undo { 3000, 60 };
    juce::File projectFile;

    // selection
    juce::ValueTree selectedTrack, selectedClip;
    juce::ChangeBroadcaster selectionChanged;

    // fractal → music
    FractalFeatures features;               // features at the musical probe point
    juce::ChangeBroadcaster featuresChanged;
    bool probeFollowsView = true;

    // editing
    double snapBeats = 1.0;                 // arrangement snap
    std::function<void (int)> showWorkspace; // index into workspace tabs
    std::function<void (const juce::String&)> setStatus;

    enum Workspace { Arrangement = 0, Fractal, PianoRoll, Generative, Synth, Drums, Mixer, AudioEditor };

    void beginEdit (const juce::String& name = {}) { undo.beginNewTransaction (name); }

    void selectTrack (juce::ValueTree t)
    {
        selectedTrack = t;
        if (selectedClip.isValid() && Model::trackOfClip (selectedClip) != t) selectedClip = {};
        engine.setLiveTrack (t.isValid() ? (int) t[IDs::id] : -1);
        selectionChanged.sendChangeMessage();
    }

    void selectClip (juce::ValueTree c)
    {
        selectedClip = c;
        if (c.isValid()) selectedTrack = Model::trackOfClip (c);
        engine.setLiveTrack (selectedTrack.isValid() ? (int) selectedTrack[IDs::id] : -1);
        selectionChanged.sendChangeMessage();
    }

    juce::File audioFolder() const
    {
        if (projectFile != juce::File())
            return projectFile.getParentDirectory().getChildFile (projectFile.getFileNameWithoutExtension() + "_Audio");
        return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("MANDELBROT").getChildFile ("Audio");
    }

    double beatsPerBar() const { return juce::jmax (1, (int) project.getProperty (IDs::tsNum, 4)) * 4.0 / juce::jmax (1, (int) project.getProperty (IDs::tsDen, 4)); }
    double tempo() const { return project.getProperty (IDs::tempo, 120.0); }
};
