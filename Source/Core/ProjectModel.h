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

    // clips
    juce::ValueTree addMidiClip (juce::ValueTree track, double start, double length, juce::UndoManager* um);
    juce::ValueTree addPatternClip (juce::ValueTree track, double start, double length, juce::UndoManager* um);
    juce::ValueTree addAudioClip (juce::ValueTree track, const juce::File& f, double start, double lengthBeats, juce::UndoManager* um);
    juce::ValueTree duplicateClip (juce::ValueTree clip, double newStart, juce::UndoManager* um);
    juce::ValueTree trackOfClip (juce::ValueTree clip);

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
