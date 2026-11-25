#pragma once
#include <vector>
#include <memory>
#include "AudioPool.h"
#include "../Core/ProjectModel.h"

class TrackProcessor;
class BusProcessor;

/** Immutable playback data built on the message thread and read by the audio thread. */
namespace Snap
{
    struct Event
    {
        double beat = 0.0;
        int note = 60;
        float velocity = 0.0f;     // 0 => note off
        float probability = 1.0f;
        juce::uint32 key = 0;      // stable per-note id (for deterministic probability)
    };

    struct AudioRegion
    {
        double startBeat = 0, endBeat = 0;
        double sourceOffsetSec = 0;
        const SampleData* data = nullptr;
        float gain = 1.0f;
        double fadeInBeats = 0, fadeOutBeats = 0;
    };

    enum AutoKind { AutoVol = 0, AutoPan, AutoSend, AutoFx, AutoSynth };

    struct AutoLane
    {
        int kind = AutoVol;
        int index = 0;      // send index / fx slot*4+param / synth param index
        std::vector<std::pair<double, float>> points;  // beat, normalised 0..1 (sorted)
        float valueAt (double beat) const;
    };

    struct Track
    {
        int id = 0;
        TrackProcessor* proc = nullptr;
        std::vector<Event> events;            // sorted by beat; offs before ons at equal beats
        std::vector<AudioRegion> regions;
        std::vector<AutoLane> automation;
        int output = -1;                      // -1 = master, else bus index
    };

    struct Snapshot
    {
        std::vector<Track> tracks;
        std::vector<BusProcessor*> buses;
        double tempo = 120.0;
        int tsNum = 4;
        bool loopOn = false;
        double loopStart = 0, loopEnd = 16;
        double endBeat = 0;
        std::vector<std::shared_ptr<const SampleData>> keepAlive;
    };

    /** Pure helpers (also used by the MIDI exporter and tests). */
    void appendMidiClipEvents (const juce::ValueTree& clip, std::vector<Event>& out);
    void appendPatternClipEvents (const juce::ValueTree& clip, std::vector<Event>& out);
    void sortEvents (std::vector<Event>& ev);
    std::vector<Event> eventsForTrack (const juce::ValueTree& track);
}
