#pragma once
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_events/juce_events.h>
#include <memory>
#include <map>

/** Immutable, fully-loaded audio data shared between message & audio threads. */
struct SampleData
{
    juce::AudioBuffer<float> buffer;  // 1 or 2 channels
    double sampleRate = 44100.0;
    juce::String path;
    int numFrames() const { return buffer.getNumSamples(); }
    float get (int ch, int i) const { return buffer.getSample (juce::jmin (ch, buffer.getNumChannels() - 1), i); }
};

/** Wavetable: frames x size floats. */
struct WavetableData
{
    int frames = 0, size = 0;
    std::vector<float> data;
    const float* frame (int f) const { return data.data() + (size_t) (juce::jlimit (0, frames - 1, f) * size); }
};

/**
 * Objects handed to the audio thread as raw pointers are kept alive here for a
 * grace period after being replaced, so the audio thread never sees a dangling
 * pointer and never has to free memory itself.
 */
class DeferredDeleter : private juce::Timer
{
public:
    static DeferredDeleter& get();
    void retire (std::shared_ptr<void> obj);
    void flushAll();   // only call when audio is stopped
    ~DeferredDeleter() override;
private:
    DeferredDeleter();
    void timerCallback() override;
    struct Item { std::shared_ptr<void> obj; juce::uint32 time; };
    std::vector<Item> items;
    juce::CriticalSection lock; // message-thread only structure; lock for offline render threads
};

/** Creates a PCM WAV writer (16/24/32-bit). Returns nullptr on failure. */
std::unique_ptr<juce::AudioFormatWriter> makeWavWriter (const juce::File& f, double sampleRate, int numChannels, int bits);

/** Cache of loaded audio files (message thread). */
class AudioPool
{
public:
    static AudioPool& get();
    std::shared_ptr<const SampleData> load (const juce::File& f);
    void invalidate (const juce::File& f);
    juce::AudioFormatManager& formats() { return formatManager; }
private:
    AudioPool();
    juce::AudioFormatManager formatManager;
    std::map<juce::String, std::shared_ptr<const SampleData>> cache;
    juce::CriticalSection lock;
};
