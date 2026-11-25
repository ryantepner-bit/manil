#pragma once
#include <atomic>
#include <array>
#include "DSP.h"
#include "Synth.h"
#include "AudioPool.h"
#include "../Core/Params.h"

/** 12-pad sample drum machine. Pads respond to MIDI notes 36..47. */
class DrumEngine
{
public:
    static constexpr int voicesPerPad = 4;

    struct Pad
    {
        std::atomic<float> params[PadP::NumParams];
        std::atomic<const SampleData*> sample { nullptr };
        std::shared_ptr<const SampleData> owner;
        std::atomic<float> meter { 0.0f };
    };

    DrumEngine();
    void prepare (double sampleRate, int maxBlock);
    void process (float* L, float* R, int numSamples, const NoteEvent* events, int numEvents);
    void allNotesOff();

    /** Message thread. */
    void setPadSample (int pad, std::shared_ptr<const SampleData> s);
    Pad& pad (int i) { return pads[(size_t) juce::jlimit (0, PadP::numPads - 1, i)]; }

    /** Procedurally synthesised drum sounds (used when no sample is loaded). */
    static std::shared_ptr<SampleData> synthesiseDrum (int kind, double sampleRate = 44100.0);

private:
    struct Voice
    {
        bool active = false;
        double pos = 0.0, inc = 1.0;
        double startF = 0.0, endF = 0.0;
        bool reverse = false;
        float vel = 1.0f;
        int age = 0;
        float env = 1.0f, envMul = 0.9999f;
        DSP::SVF fL, fR;
        float holdL = 0, holdR = 0; int holdCount = 0;
        const SampleData* data = nullptr;
    };

    void trigger (int padIndex, float velocity);
    void choke (int group, int exceptPad);

    double sr = 44100.0;
    std::array<Pad, PadP::numPads> pads;
    std::array<std::array<Voice, voicesPerPad>, PadP::numPads> voices;
    std::array<int, PadP::numPads> nextVoice {};
};
