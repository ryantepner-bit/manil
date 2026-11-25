#pragma once
#include <atomic>
#include <array>
#include <vector>
#include "DSP.h"
#include "Synth.h"
#include "AudioPool.h"

/**
 * Break chopper instrument (modelled on LMMS's SlicerT).
 *
 * One sample (a drum break) cut into slices. MIDI note 36 plays slice 0,
 * 37 slice 1, ... A new slice cuts the previous one (with a short fade), like
 * chopping on an MPC. With sync on, playback speed follows the project tempo
 * (speed = tempo / break bpm), so a 170 bpm jungle break played at 140 bpm
 * slows down with it.
 */
class BreakEngine
{
public:
    static constexpr int baseNote = 36;
    static constexpr int maxSlices = 64;

    struct Slices { std::vector<float> points; };   // normalised 0..1, first 0, last 1

    BreakEngine();
    void prepare (double sampleRate, int maxBlock);
    void process (float* L, float* R, int numSamples, const NoteEvent* events, int numEvents, double tempo);
    void allNotesOff();

    /** Message thread. */
    void setSample (std::shared_ptr<const SampleData> s);
    void setSlices (std::vector<float> points);
    std::shared_ptr<const SampleData> getSample() const { return owner; }
    std::vector<float> getSlices() const { auto* s = slices.load(); return s != nullptr ? s->points : std::vector<float> {}; }

    std::atomic<float> breakBpm { 140.0f }, gainDb { 0.0f }, tuneSemis { 0.0f }, fadeMs { 12.0f };
    std::atomic<bool> sync { true };
    std::atomic<int> playingSlice { -1 };    // UI

    // ---- analysis helpers (message thread)
    /** Transient detection by spectral flux (as in SlicerT): sensitivity 0..1 (higher = more slices). */
    static std::vector<float> detectSlices (const SampleData& d, float sensitivity, int maxCount = maxSlices);
    static std::vector<float> equalSlices (int count);
    /** Tempo guess from the length (assumes 1, 2, 4 or 8 bars of 4/4), as SlicerT does. */
    static float guessBpm (const SampleData& d);
    /** A built-in two-bar break, synthesised or (if given) played from real drum samples. */
    static std::shared_ptr<SampleData> makeDefaultBreak (double sampleRate, const std::array<std::shared_ptr<const SampleData>, 3>& kickSnareHat);
    static juce::String encodeSlices (const std::vector<float>& p);
    static std::vector<float> decodeSlices (const juce::String& s);

private:
    struct Voice
    {
        bool active = false;
        double pos = 0, end = 0;
        float vel = 1.0f, fade = 1.0f, fadeStep = 0.0f;
        bool releasing = false;
        int slice = -1;
    };
    std::atomic<const SampleData*> sample { nullptr };
    std::shared_ptr<const SampleData> owner;
    std::atomic<Slices*> slices { nullptr };
    std::shared_ptr<Slices> slicesOwner;
    std::array<Voice, 4> voices;
    int nextVoice = 0;
    double sr = 44100.0;
    void trigger (int slice, float vel);
};
