#pragma once
#include <atomic>
#include <vector>
#include <juce_audio_basics/juce_audio_basics.h>
#include "DSP.h"
#include "../Core/Params.h"

struct FxContext
{
    double bpm = 120.0;
    double beat = 0.0;     // beat position at block start
    bool playing = false;
};

/** One insert slot: every effect type is preallocated so switching type is RT-safe. */
class InsertSlot
{
public:
    std::atomic<int> type { FxP::None };
    std::atomic<bool> bypass { false };
    std::atomic<float> params[4] { 0.5f, 0.5f, 0.5f, 0.5f };

    void prepare (double sampleRate, int maxBlock);
    void reset();
    /** override: per-param automation values (<0 = none) */
    void process (float* L, float* R, int n, const FxContext& ctx, const float* overrides);

private:
    double sr = 44100.0;
    int lastType = -1;
    DSP::SVF fl, fr;
    float lpL = 0, lpR = 0;
    float crushHoldL = 0, crushHoldR = 0; int crushCount = 0, crushTarget = 1;
    DSP::Rng rng { 777 };
    std::vector<float> dlL, dlR; int dlW = 0;
    float dlToneL = 0, dlToneR = 0;
    DSP::Smoother dlTime;
    juce::Reverb reverb;
    float compEnv = 0.0f;
    std::vector<float> chL, chR; int chW = 0; float chPhase = 0.0f;
    std::vector<float> stL, stR; int stW = 0;
    juce::int64 stSlice = -1; int stRemaining = 0; int stReadStart = 0; int stLen = 1; int stReadPos = 0;

    float readFrac (const std::vector<float>& buf, float pos) const;
};

/** A fixed chain of insert slots. */
struct InsertChain
{
    InsertSlot slots[MixP::numInserts];
    void prepare (double sr, int block) { for (auto& s : slots) s.prepare (sr, block); }
    void reset() { for (auto& s : slots) s.reset(); }
    void process (float* L, float* R, int n, const FxContext& ctx, const float (*overrides)[4])
    {
        for (int i = 0; i < MixP::numInserts; ++i)
            slots[i].process (L, R, n, ctx, overrides != nullptr ? overrides[i] : nullptr);
    }
};
