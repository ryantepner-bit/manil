#pragma once
#include "AppState.h"
#include "../Generative/Generator.h"
#include <deque>

/**
 * Fractal composition controller: turns the probe location into features,
 * drives live-evolving generators while the transport runs, and records /
 * replays exploration paths as composition automation.
 */
class Composer : private juce::Timer
{
public:
    explicit Composer (AppState& s);
    ~Composer() override;

    void setProject (juce::ValueTree p);

    /** Musical probe location (complex point + neighbourhood scale). */
    void setProbe (double cx, double cy, double scale, bool immediate = false);
    double probeX() const { return px; }
    double probeY() const { return py; }
    double probeScale() const { return pscale; }

    // generators
    juce::ValueTree addGenerator (int trackId, int role);
    /** Generate into a new clip at 'atBeat' (or into 'intoClip' if valid). Returns the clip. */
    juce::ValueTree generate (juce::ValueTree gen, double atBeat, juce::ValueTree intoClip = {}, bool evolve = false);
    juce::ValueTree generate (juce::ValueTree gen, double atBeat, const FractalFeatures& f, juce::ValueTree intoClip, bool evolve);
    void generateAll (double atBeat);
    juce::ValueTree findClip (int clipId) const;

    // exploration paths
    void startRecording();
    void stopRecording();
    bool isRecordingPath() const { return recordingPath.isValid(); }
    void setReplayPath (juce::ValueTree path) { replayPath = path; }
    juce::ValueTree getReplayPath() const { return replayPath; }
    bool interpolatePath (const juce::ValueTree& path, double beat, double& cx, double& cy, double& scale) const;
    /** Writes clips for all generators along the path (phrase by phrase). Returns clip count. */
    int renderPathToArrangement (juce::ValueTree path);

    std::function<void (double cx, double cy, double scale)> onPathMove;   // move the explorer view
    bool liveEnabled = true;

    // ---------------- live generative mode
    struct LiveNote { int part = 0, pitch = 60, velocity = 100; double start = 0, end = -1; };
    const std::deque<LiveNote>& liveHistory() const { return history; }
    /** Copies the last 'bars' bars that the live parts played into MIDI clips after the end of the song. */
    int captureLive (int bars);
    /** The visible fractal area: drift stays inside it. */
    void setViewBounds (double cx, double cy, double width) { viewCx = cx; viewCy = cy; viewW = width; }
    /** Runs one live tick immediately (tests). */
    void liveTickNow() { liveDriftAndPublish(); drainLiveMonitor(); }
    int liveTrackFor (int part) const;

private:
    void timerCallback() override;
    void liveDriftAndPublish();
    void drainLiveMonitor();
    double viewCx = -0.5, viewCy = 0.0, viewW = 3.2;
    double driftAngle = 0.7;
    int insideTicks = 0;
    juce::Random driftRng { 12345 };
    std::deque<LiveNote> history;
    std::map<int, LiveNote> openNotes;
    void updateFeatures();
    void liveTick();
    int beatsPerBarInt() const;

    AppState& state;
    juce::ValueTree project;
    double px = -0.7436, py = 0.1318, pscale = 0.02;
    bool featuresDirty = true;
    juce::uint32 lastFeatureTime = 0;
    juce::ValueTree recordingPath, replayPath;
    double recordStartBeat = 0.0;
    juce::uint32 recordStartMs = 0;
    double lastBeat = 0.0;
    std::map<int, FractalFeatures> lastLiveFeatures;   // per generator id
};
