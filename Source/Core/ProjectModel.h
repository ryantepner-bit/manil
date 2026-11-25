#pragma once
#include <juce_data_structures/juce_data_structures.h>
#include <juce_graphics/juce_graphics.h>
#include "Identifiers.h"
#include "Params.h"

/** One MIDI note in clip-relative beats. */
struct NoteData
{
    int pitch = 60;
    double start = 0.0, length = 0.25;
    int velocity = 100;
    float probability = 1.0f;
};

/** One drum pattern step. */
struct StepData
{
    int velocity = 0;      // 0 = off
    int probability = 100; // percent
    int ratchet = 1;       // 1..4 repeats inside the step
};

/**
 * Thin, stateless helpers around the project ValueTree. The ValueTree is the
 * single source of truth: undo/redo, save/load and UI binding all go through it.
 */
namespace Model
{
    constexpr int fileVersion = 1;

    juce::ValueTree createDefaultProject();
    juce::ValueTree createEmptyProject();

    int allocateId (juce::ValueTree project);

    // tracks
    juce::ValueTree addTrack (juce::ValueTree project, const juce::String& type, const juce::String& name, juce::UndoManager* um);
    juce::ValueTree findTrack (juce::ValueTree project, int trackId);
    juce::ValueTree tracks (juce::ValueTree project);
    juce::ValueTree buses (juce::ValueTree project);
    juce::ValueTree master (juce::ValueTree project);
    juce::ValueTree addBus (juce::ValueTree project, const juce::String& name, juce::UndoManager* um);
    juce::ValueTree createSynthNode();
    juce::ValueTree createDrumKitNode();
    juce::ValueTree createInsertsNode();
    juce::ValueTree createPadNode (int index);
    juce::Colour trackColour (int index);

    // synth presets (sound design starting points that the live parts use)
    juce::StringArray synthPresetNames();
    void applySynthPreset (juce::ValueTree synthNode, const juce::String& preset, juce::UndoManager* um);

    /** Ensures the project has a LIVE node (older files: created with live off). */
    juce::ValueTree liveNode (juce::ValueTree project);

    /** Drum kits from the sample library (SampleLib) or "Synth" for the synthesised sounds. */
    void applyKit (juce::ValueTree drumTrack, const juce::String& kit, juce::uint32 seed, juce::UndoManager* um);

    /** Live styles: switch tempo, sounds, parts and space in one go (everything stays editable). */
    juce::StringArray liveStyleNames();
    void applyLiveStyle (juce::ValueTree project, int style, juce::UndoManager* um);
    /** Reverb send of each live track = its base amount x the Space macro. */
    void applySpace (juce::ValueTree project, float space, juce::UndoManager* um);

    juce::ValueTree createBreakNode();

    // clips
    juce::ValueTree addMidiClip (juce::ValueTree track, double start, double length, juce::UndoManager* um);
    juce::ValueTree addPatternClip (juce::ValueTree track, double start, double length, juce::UndoManager* um);
    juce::ValueTree addAudioClip (juce::ValueTree track, const juce::File& f, double start, double lengthBeats, juce::UndoManager* um);
    juce::ValueTree duplicateClip (juce::ValueTree clip, double newStart, juce::UndoManager* um);
    juce::ValueTree trackOfClip (juce::ValueTree clip);
    /** First start >= 'start' (moved by whole bars) where a clip of 'length' does not overlap another clip. */
    double freeSpot (const juce::ValueTree& track, double start, double length, double beatsPerBar);

    // notes
    juce::ValueTree addNote (juce::ValueTree clip, const NoteData& n, juce::UndoManager* um);
    NoteData readNote (const juce::ValueTree& noteNode);
    void writeNote (juce::ValueTree noteNode, const NoteData& n, juce::UndoManager* um);
    void setNotes (juce::ValueTree clip, const std::vector<NoteData>& notes, juce::UndoManager* um);
    std::vector<NoteData> readNotes (const juce::ValueTree& clip);

    // pattern
    juce::ValueTree patternOf (juce::ValueTree clip);
    juce::ValueTree laneFor (juce::ValueTree clip, int pad);
    std::vector<StepData> readSteps (const juce::ValueTree& lane);
    void writeSteps (juce::ValueTree lane, const std::vector<StepData>& steps, juce::UndoManager* um);
    juce::String encodeSteps (const std::vector<StepData>& steps);
    std::vector<StepData> decodeSteps (const juce::String& s, int numSteps);

    // automation
    juce::ValueTree automationLane (juce::ValueTree track, const juce::String& paramId, bool create, juce::UndoManager* um);

    // generic float property get/set
    float getFloat (const juce::ValueTree& v, const juce::Identifier& id, float def);

    // persistence
    bool save (const juce::ValueTree& project, const juce::File& f);
    juce::ValueTree load (const juce::File& f, juce::String& error);

    double projectEndBeat (const juce::ValueTree& project);
}
