#pragma once
#include <cmath>
#include <cstdint>
#include <algorithm>
#include <juce_core/juce_core.h>

/** Small real-time-safe DSP building blocks (no allocation, no locks). */
namespace DSP
{
    constexpr float twoPi = 6.283185307179586f;

    struct Rng
    {
        uint32_t s = 0x9e3779b9u;
        explicit Rng (uint32_t seed = 0x9e3779b9u) : s (seed ? seed : 1u) {}
        uint32_t next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
        float uni() { return (next() >> 8) * (1.0f / 16777216.0f); }      // 0..1
        float bi() { return uni() * 2.0f - 1.0f; }                         // -1..1
    };

    inline uint32_t hash32 (uint32_t x)
    {
        x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15; x *= 0x846ca68bu; x ^= x >> 16;
        return x;
    }

    inline float midiToHz (float note) { return 440.0f * std::exp2 ((note - 69.0f) / 12.0f); }

    inline float polyBlep (float t, float dt)
    {
        if (t < dt) { t /= dt; return t + t - t * t - 1.0f; }
        if (t > 1.0f - dt) { t = (t - 1.0f) / dt; return t * t + t + t + 1.0f; }
        return 0.0f;
    }

    /** Band-limited classic oscillator. wave: 0 saw, 1 square, 2 tri, 3 sine, 4 noise */
    struct Osc
    {
        float phase = 0.0f, triState = 0.0f;
        Rng rng;
        float process (int wave, float inc)
        {
            inc = std::min (inc, 0.45f);
            float out = 0.0f;
            switch (wave)
            {
                case 0: out = 2.0f * phase - 1.0f - polyBlep (phase, inc); break;
                case 1:
                case 2:
                {
                    float sq = phase < 0.5f ? 1.0f : -1.0f;
                    sq += polyBlep (phase, inc);
                    float p2 = phase + 0.5f; if (p2 >= 1.0f) p2 -= 1.0f;
                    sq -= polyBlep (p2, inc);
                    if (wave == 1) out = sq;
                    else { triState = inc * 4.0f * sq + (1.0f - inc * 0.5f) * triState; out = triState; }
                    break;
                }
                case 3: out = std::sin (twoPi * phase); break;
                default: out = rng.bi(); break;
            }
            phase += inc;
            if (phase >= 1.0f) phase -= 1.0f;
            return out;
        }
    };

    /** TPT state-variable filter (Zavalishin). mode 0 LP, 1 BP, 2 HP */
    struct SVF
    {
        float ic1 = 0, ic2 = 0, g = 0, k = 1, a1 = 0, a2 = 0, a3 = 0;
        void set (float cutoffHz, float reso01, float sr)
        {
            cutoffHz = std::clamp (cutoffHz, 15.0f, sr * 0.45f);
            g = std::tan (juce::MathConstants<float>::pi * cutoffHz / sr);
            k = 2.0f - 1.96f * std::clamp (reso01, 0.0f, 1.0f);
            a1 = 1.0f / (1.0f + g * (g + k)); a2 = g * a1; a3 = g * a2;
        }
        void reset() { ic1 = ic2 = 0; }
        float process (float x, int mode)
        {
            const float v3 = x - ic2;
            const float v1 = a1 * ic1 + a2 * v3;
            const float v2 = ic2 + a2 * ic1 + a3 * v3;
            ic1 = 2.0f * v1 - ic1;
            ic2 = 2.0f * v2 - ic2;
            if (! std::isfinite (ic1) || ! std::isfinite (ic2)) { reset(); return 0.0f; }
            switch (mode)
            {
                case 1: return v1;
                case 2: return x - k * v1 - v2;
                default: return v2;
            }
        }
    };

    /** Linear-segment ADSR with exponential-ish release, per-sample. */
    struct Env
    {
        enum Stage { Idle, Attack, Decay, Sustain, Release };
        Stage stage = Idle;
        float value = 0.0f, a = 0.01f, d = 0.1f, s = 0.7f, r = 0.2f, sr = 44100.0f;
        float releaseStep = 0.0f;
        void set (float A, float D, float S, float R, float sampleRate) { a = A; d = D; s = S; r = R; sr = sampleRate; }
        void noteOn() { stage = Attack; }
        void noteOff() { if (stage != Idle) { stage = Release; releaseStep = value / std::max (1.0f, r * sr); } }
        void kill() { stage = Idle; value = 0.0f; }
        bool active() const { return stage != Idle; }
        float next()
        {
            switch (stage)
            {
                case Attack:
                    value += 1.0f / std::max (1.0f, a * sr);
                    if (value >= 1.0f) { value = 1.0f; stage = Decay; }
                    break;
                case Decay:
                    value -= (1.0f - s) / std::max (1.0f, d * sr);
                    if (value <= s) { value = s; stage = Sustain; }
                    break;
                case Sustain: value = s; break;
                case Release:
                    value -= std::max (releaseStep, 1e-7f);
                    if (value <= 0.0f) { value = 0.0f; stage = Idle; }
                    break;
                default: break;
            }
            return value;
        }
    };

    /** LFO: 0 sine, 1 tri, 2 saw, 3 square, 4 sample&hold. Output -1..1 */
    struct LFO
    {
        float phase = 0.0f, held = 0.0f;
        Rng rng { 12345 };
        float process (int shape, float inc)
        {
            float out;
            switch (shape)
            {
                case 1: out = 1.0f - 4.0f * std::abs (phase - 0.5f); break;
                case 2: out = 2.0f * phase - 1.0f; break;
                case 3: out = phase < 0.5f ? 1.0f : -1.0f; break;
                case 4: out = held; break;
                default: out = std::sin (twoPi * phase); break;
            }
            phase += inc;
            if (phase >= 1.0f) { phase -= 1.0f; held = rng.bi(); }
            return out;
        }
    };

    struct Smoother
    {
        float cur = 0.0f, target = 0.0f, coeff = 0.999f;
        void setTime (float seconds, float sr) { coeff = std::exp (-1.0f / std::max (1.0f, seconds * sr)); }
        void snap (float v) { cur = target = v; }
        float next() { cur = target + (cur - target) * coeff; return cur; }
    };

    inline float softClip (float x) { return std::tanh (x); }

    inline float fold (float x)
    {
        // wavefolder
        x = x * 0.25f + 0.25f;
        x = x - std::floor (x);
        return 4.0f * std::abs (x - 0.5f) - 1.0f;
    }

    inline float panL (float pan) { return std::cos ((pan + 1.0f) * juce::MathConstants<float>::pi * 0.25f); }
    inline float panR (float pan) { return std::sin ((pan + 1.0f) * juce::MathConstants<float>::pi * 0.25f); }
}
