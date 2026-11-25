#pragma once
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <atomic>
#include <map>
#include "Snapshot.h"
#include "Synth.h"
#include "DrumMachine.h"
#include "Effects.h"

std::shared_ptr<WavetableData> wavetableFromSample (const SampleData& d);
void applySynthSources (SynthEngine& synth, const juce::ValueTree& synthNode);

struct ChannelState
{
    std::atomic<float> volDb { 0.0f }, pan { 0.0f };
    std::atomic<float> sends[MixP::numSends] { 0, 0, 0, 0 };
    std::atomic<bool> mute { false }, solo { false };
    std::atomic<float> peakL { 0.0f }, peakR { 0.0f };
    std::atomic<float> dispL { 0.0f }, dispR { 0.0f };   // published peaks for meters (message thread)
    void publish() { dispL.store (peakL.exchange (0.0f)); dispR.store (peakR.exchange (0.0f)); }
    DSP::Smoother gL, gR;   // audio thread only
    void readFrom (const juce::ValueTree& v);
    void meter (const float* L, const float* R, int n);
};

class TrackProcessor
{
public:
    TrackProcessor (int trackId, const juce::String& trackType);
    void prepare (double sr, int block);
    void syncFrom (const juce::ValueTree& track);   // message thread: copy all params
    int id;
    juce::String type;
    ChannelState ch;
    InsertChain inserts;
    std::unique_ptr<SynthEngine> synth;
    std::unique_ptr<DrumEngine> drums;
    std::atomic<bool> armed { false };
    juce::AudioBuffer<float> buf;
    std::vector<NoteEvent> scratch;
    std::array<juce::uint8, 128> held {};
    std::array<juce::uint8, 128> suppressed {};
    float synthOverrides[SynthP::NumParams];
    float fxOverrides[MixP::numInserts][4];
};

class BusProcessor
{
public:
    explicit BusProcessor (int idx) : index (idx) {}
    void prepare (double sr, int block) { inserts.prepare (sr, block); buf.setSize (2, block); buf.clear(); ch.gL.setTime (0.02f, (float) sr); ch.gR.setTime (0.02f, (float) sr); }
    int index;
    ChannelState ch;
    InsertChain inserts;
    juce::AudioBuffer<float> buf;
};

/**
 * Real-time audio engine. The message thread edits the ValueTree; the engine
 * listens, mirrors continuous parameters into atomics and rebuilds an
 * immutable playback snapshot that is swapped atomically. The audio callback
 * never locks, allocates or touches the disk.
 */
class Engine : public juce::AudioIODeviceCallback,
               private juce::ValueTree::Listener,
               private juce::Timer,
               private juce::MidiInputCallback
{
public:
    static constexpr int chunk = 256;

    Engine();
    ~Engine() override;

    void initialiseAudio (const juce::String& savedXml = {});
    juce::AudioDeviceManager& getDeviceManager() { return deviceManager; }

    void setProject (juce::ValueTree project);
    juce::ValueTree getProject() const { return project; }
    void rebuildNow();

    // ---------------- transport (message thread)
    void play();
    void stop();
    void togglePlay() { isPlaying() ? stop() : play(); }
    bool isPlaying() const { return playing.load(); }
    void setPosition (double beat);
    double getPosition() const { return positionBeat.load(); }
    void setRecordEnabled (bool shouldRecord);
    bool isRecordEnabled() const { return recordEnabled.load(); }
    bool isRecording() const { return recordWriter.load() != nullptr; }

    // ---------------- live input (any thread; lock-free FIFO)
    void sendLiveNote (int trackId, int note, float velocity);
    void setLiveTrack (int trackId) { liveTrackId.store (trackId); }

    // ---------------- previews (audio editor / sample browser)
    void previewSample (std::shared_ptr<const SampleData> data, juce::int64 startFrame = 0, juce::int64 endFrame = -1);
    void stopPreview();
    double getPreviewPosition() const { return previewPos.load(); }   // frames, -1 when idle

    // ---------------- processors (message thread)
    TrackProcessor* getTrackProcessor (int trackId);
    BusProcessor* getBusProcessor (int index);
    ChannelState& getMasterChannel() { return masterCh; }
    GlobalMods& getGlobalMods() { return mods; }
    double getSampleRate() const { return sampleRate; }
    float getCpuLoad() const { return (float) deviceManager.getCpuUsage(); }

    // ---------------- offline rendering
    struct RenderOptions
    {
        juce::File file;
        double sampleRate = 44100.0;
        int bitDepth = 24;
        double startBeat = 0.0, endBeat = 16.0;
        double tailSeconds = 2.0;
        int onlyTrackId = -1;      // >=0 => stem of a single track
    };
    /** Blocking; call from a background thread. progress(0..1) returns false to cancel. */
    bool renderOffline (const RenderOptions& opts, std::function<bool (float)> progress, juce::String& error);

    /** Called on the message thread when a recording finished: (trackId, file, startBeat, lengthBeats). */
    std::function<void (int, juce::File, double, double)> onRecordingFinished;
    juce::File recordingFolder;

    // AudioIODeviceCallback
    void audioDeviceIOCallbackWithContext (const float* const* in, int numIn, float* const* out, int numOut, int numSamples,
                                           const juce::AudioIODeviceCallbackContext&) override;
    void audioDeviceAboutToStart (juce::AudioIODevice* device) override;
    void audioDeviceStopped() override;

    /** Core render (used by realtime + offline). */
    struct Transport { bool playing = false; double beat = 0.0; juce::uint32 loopPass = 0; bool wasPlaying = false; bool flush = false; };
    void processBlock (float* L, float* R, const float* inL, const float* inR, int n, Transport& tr, int onlyTrack, bool offline);

private:
    void valueTreePropertyChanged (juce::ValueTree& tree, const juce::Identifier& prop) override;
    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override { dirty = true; }
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override { dirty = true; }
    void valueTreeChildOrderChanged (juce::ValueTree&, int, int) override { dirty = true; }
    void valueTreeRedirected (juce::ValueTree&) override { dirty = true; }
    void timerCallback() override;
    void handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage&) override;

    void prepareAll (double sr);
    void applyPadProperty (TrackProcessor& tp, const juce::ValueTree& pad, const juce::Identifier& prop);
    void startRecordingWriter();
    void finishRecording();

    juce::AudioDeviceManager deviceManager;
    juce::ValueTree project;
    double sampleRate = 44100.0;

    std::map<int, std::shared_ptr<TrackProcessor>> trackProcs;
    std::vector<std::shared_ptr<BusProcessor>> busProcs;
    ChannelState masterCh;
    InsertChain masterInserts;
    juce::AudioBuffer<float> masterBuf, inputBuf;

    std::atomic<Snap::Snapshot*> current { nullptr };
    std::shared_ptr<Snap::Snapshot> currentOwner;
    std::atomic<bool> dirty { true };

    std::atomic<bool> playing { false }, recordEnabled { false };
    std::atomic<double> positionBeat { 0.0 }, requestedPosition { -1.0 };
    Transport rtTransport;
    GlobalMods mods;
    juce::SpinLock renderLock;

    struct Live { int track; int note; float vel; };
    juce::AbstractFifo liveFifo { 1024 };
    std::array<Live, 1024> liveData;
    std::atomic<int> liveTrackId { -1 };

    std::atomic<const SampleData*> previewData { nullptr };
    std::shared_ptr<const SampleData> previewOwner;
    std::atomic<double> previewPos { -1.0 };
    std::atomic<juce::int64> previewEnd { 0 };

    // recording
    juce::TimeSliceThread writerThread { "MANDELBROT recorder" };
    std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> recordOwner;
    std::atomic<juce::AudioFormatWriter::ThreadedWriter*> recordWriter { nullptr };
    juce::File recordFile;
    std::atomic<double> recordStartBeat { 0.0 };
    std::atomic<juce::int64> recordedSamples { 0 };
    int recordTrackId = -1;

    float metroPhase = 0.0f, metroEnv = 0.0f; int metroAccent = 0;
    std::atomic<bool> metronomeOn { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Engine)
};
