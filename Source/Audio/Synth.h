#pragma once
#include <atomic>
#include <array>
#include "DSP.h"
#include "AudioPool.h"
#include "../Core/Params.h"

/** A note event inside one audio block. */
struct NoteEvent
{
    int offset = 0;      // sample offset in block
    int note = 60;
    float velocity = 0;  // 0 => note off
};

/** Global (per-engine) modulation sources fed from the fractal explorer. */
struct GlobalMods
{
    std::atomic<float> fractalA { 0.5f }, fractalB { 0.5f };
};

/**
 * MandelSynth: 16-voice polyphonic synth with four engines
 * (subtractive / FM / wavetable / granular), SVF filter, two envelopes,
 * two LFOs and a 6-slot modulation matrix.
 */
class SynthEngine
{
public:
    static constexpr int numVoices = 16;
    static constexpr int maxGrains = 24;

    SynthEngine();
    std::atomic<float> params[SynthP::NumParams];

    void prepare (double sampleRate, int maxBlock);
    void process (float* L, float* R, int numSamples, const NoteEvent* events, int numEvents,
                  const float* paramOverrides, const GlobalMods& mods);
    void allNotesOff (bool hard);

    /** Message thread: swap in new wavetable / granular source (old one is retired). */
    void setWavetable (std::shared_ptr<WavetableData> wt);
    void setGrainSource (std::shared_ptr<const SampleData> s);
    std::shared_ptr<WavetableData> getWavetable() const { return wavetableOwner; }
    std::shared_ptr<const SampleData> getGrainSource() const { return grainOwner; }

    int activeVoiceCount() const;

private:
    struct Grain { bool active = false; float pos = 0, inc = 0; int age = 0, len = 1; float panL = 1, panR = 1; };
    struct Voice
    {
        bool active = false;
        int note = 60; float velocity = 0; float pitch = 60, pitchTarget = 60;
        juce::uint32 age = 0;
        float random = 0;
        DSP::Osc o1, o2, sub;
        DSP::Rng noise;
        float fmCar = 0, fmMod = 0, fmFb = 0;
        float wtPhase = 0;
        Grain grains[maxGrains];
        float grainTimer = 0;
        DSP::Rng grng;
        DSP::SVF fL, fR;
        DSP::Env amp, mod;
        int filterCounter = 0;
        bool released = false;
    };

    void noteOn (int note, float vel);
    void noteOff (int note);
    void renderVoice (Voice& v, float* L, float* R, int start, int n, const float* p, const GlobalMods& mods);

    double sr = 44100.0;
    std::array<Voice, numVoices> voices;
    juce::uint32 ageCounter = 0;
    float lastPitch = 60.0f;
    DSP::LFO lfo1, lfo2;
    std::vector<float> lfo1Buf, lfo2Buf;
    int blockOffset = 0;

    std::atomic<WavetableData*> wavetable { nullptr };
    std::atomic<const SampleData*> grainSource { nullptr };
    std::shared_ptr<WavetableData> wavetableOwner;
    std::shared_ptr<const SampleData> grainOwner;
    std::shared_ptr<SampleData> defaultGrainOwner;
};
