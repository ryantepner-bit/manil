#include "Effects.h"

void InsertSlot::prepare (double sampleRate, int)
{
    sr = sampleRate;
    const size_t dlSize = (size_t) (sr * 4.0) + 4;
    dlL.assign (dlSize, 0.0f); dlR.assign (dlSize, 0.0f);
    const size_t chSize = (size_t) (sr * 0.06) + 4;
    chL.assign (chSize, 0.0f); chR.assign (chSize, 0.0f);
    const size_t stSize = (size_t) (sr * 3.0) + 4;
    stL.assign (stSize, 0.0f); stR.assign (stSize, 0.0f);
    reverb.setSampleRate (sr);
    dlTime.setTime (0.08f, (float) sr);
    reset();
}

void InsertSlot::reset()
{
    fl.reset(); fr.reset();
    lpL = lpR = 0; crushHoldL = crushHoldR = 0; crushCount = 0;
    std::fill (dlL.begin(), dlL.end(), 0.0f); std::fill (dlR.begin(), dlR.end(), 0.0f);
    std::fill (chL.begin(), chL.end(), 0.0f); std::fill (chR.begin(), chR.end(), 0.0f);
    std::fill (stL.begin(), stL.end(), 0.0f); std::fill (stR.begin(), stR.end(), 0.0f);
    dlW = chW = stW = 0; dlToneL = dlToneR = 0;
    reverb.reset();
    compEnv = 0.0f; stSlice = -1; stRemaining = 0;
}

float InsertSlot::readFrac (const std::vector<float>& buf, float pos) const
{
    const int size = (int) buf.size();
    while (pos < 0) pos += (float) size;
    const int i0 = (int) pos % size;
    const int i1 = (i0 + 1) % size;
    const float fr = pos - std::floor (pos);
    return buf[(size_t) i0] + (buf[(size_t) i1] - buf[(size_t) i0]) * fr;
}

void InsertSlot::process (float* L, float* R, int n, const FxContext& ctx, const float* ov)
{
    const int t = type.load (std::memory_order_relaxed);
    if (t != lastType) { if (lastType >= 0) reset(); lastType = t; }
    if (t == FxP::None || bypass.load (std::memory_order_relaxed) || dlL.empty()) return;

    float p[4];
    for (int i = 0; i < 4; ++i)
        p[i] = (ov != nullptr && ov[i] >= 0.0f) ? ov[i] : params[i].load (std::memory_order_relaxed);
    const float mix = p[3];
    const float fsr = (float) sr;

    switch (t)
    {
        case FxP::Filter:
        {
            const float cutoff = 20.0f * std::pow (1000.0f, p[0]);
            const int mode = p[2] < 0.34f ? 0 : (p[2] < 0.67f ? 1 : 2);
            fl.set (cutoff, p[1] * 0.97f, fsr); fr.set (cutoff, p[1] * 0.97f, fsr);
            for (int i = 0; i < n; ++i)
            {
                const float a = fl.process (L[i], mode), b = fr.process (R[i], mode);
                L[i] += (a - L[i]) * mix; R[i] += (b - R[i]) * mix;
            }
            break;
        }
        case FxP::Saturation:
        {
            const float drive = 1.0f + p[0] * 19.0f, bias = (p[2] - 0.5f) * 0.6f;
            const float toneC = 1.0f - std::exp (-DSP::twoPi * (500.0f + p[1] * 15000.0f) / fsr);
            const float comp = 1.0f / std::tanh (drive);
            for (int i = 0; i < n; ++i)
            {
                float a = (std::tanh (L[i] * drive + bias) - std::tanh (bias)) * comp;
                float b = (std::tanh (R[i] * drive + bias) - std::tanh (bias)) * comp;
                lpL += (a - lpL) * toneC; lpR += (b - lpR) * toneC;
                L[i] += (lpL - L[i]) * mix; R[i] += (lpR - R[i]) * mix;
            }
            break;
        }
        case FxP::Distortion:
        {
            const float drive = 1.0f + p[0] * 40.0f;
            const float toneC = 1.0f - std::exp (-DSP::twoPi * (300.0f + p[2] * 12000.0f) / fsr);
            for (int i = 0; i < n; ++i)
            {
                auto shape = [&] (float x)
                {
                    x *= drive;
                    const float clip = std::clamp (x, -1.0f, 1.0f);
                    return (clip * (1.0f - p[1]) + DSP::fold (x) * p[1]) * 0.7f;
                };
                const float a = shape (L[i]), b = shape (R[i]);
                lpL += (a - lpL) * toneC; lpR += (b - lpR) * toneC;
                L[i] += (lpL - L[i]) * mix; R[i] += (lpR - R[i]) * mix;
            }
            break;
        }
        case FxP::Bitcrush:
        {
            const float bits = 16.0f - p[0] * 14.0f;
            const float levels = std::pow (2.0f, bits - 1.0f);
            const int down = 1 + (int) (p[1] * p[1] * 48.0f);
            for (int i = 0; i < n; ++i)
            {
                if (++crushCount >= crushTarget)
                {
                    crushCount = 0;
                    crushTarget = std::max (1, down + (int) (p[2] * rng.bi() * (float) down));
                    crushHoldL = std::round (L[i] * levels) / levels;
                    crushHoldR = std::round (R[i] * levels) / levels;
                }
                L[i] += (crushHoldL - L[i]) * mix; R[i] += (crushHoldR - R[i]) * mix;
            }
            break;
        }
        case FxP::Delay:
        {
            static const float beatsTable[] = { 0.125f, 0.25f, 0.333333f, 0.5f, 0.666667f, 0.75f, 1.0f, 1.5f, 2.0f };
            const float beats = beatsTable[std::clamp ((int) (p[0] * 8.99f), 0, 8)];
            const float target = std::min ((float) dlL.size() - 2.0f, beats * 60.0f / (float) ctx.bpm * fsr);
            dlTime.target = target;
            if (dlTime.cur <= 0.0f) dlTime.snap (target);
            const float fb = p[1] * 0.92f;
            const float toneC = 1.0f - std::exp (-DSP::twoPi * (400.0f + p[2] * 14000.0f) / fsr);
            const int size = (int) dlL.size();
            for (int i = 0; i < n; ++i)
            {
                const float d = dlTime.next();
                const float rp = (float) dlW - d;
                const float yL = readFrac (dlL, rp), yR = readFrac (dlR, rp);
                dlToneL += (yL - dlToneL) * toneC; dlToneR += (yR - dlToneR) * toneC;
                // ping-pong cross feedback
                dlL[(size_t) dlW] = L[i] + dlToneR * fb;
                dlR[(size_t) dlW] = R[i] * 0.6f + dlToneL * fb;
                if (++dlW >= size) dlW = 0;
                L[i] += yL * mix; R[i] += yR * mix;
                L[i] = std::isfinite (L[i]) ? L[i] : 0.0f; R[i] = std::isfinite (R[i]) ? R[i] : 0.0f;
            }
            break;
        }
        case FxP::Reverb:
        {
            juce::Reverb::Parameters rp;
            rp.roomSize = p[0]; rp.damping = p[1]; rp.width = p[2];
            rp.wetLevel = mix; rp.dryLevel = 1.0f - mix * 0.5f; rp.freezeMode = 0.0f;
            reverb.setParameters (rp);
            reverb.processStereo (L, R, n);
            break;
        }
        case FxP::Compressor:
        {
            const float threshDb = -40.0f + p[0] * 40.0f;
            const float ratio = 1.0f + p[1] * 19.0f;
            const float att = std::exp (-1.0f / (0.004f * fsr));
            const float rel = std::exp (-1.0f / ((0.02f + p[2] * 0.5f) * fsr));
            const float makeup = juce::Decibels::decibelsToGain (p[3] * 24.0f);
            for (int i = 0; i < n; ++i)
            {
                const float lvl = std::max (std::abs (L[i]), std::abs (R[i]));
                compEnv = lvl > compEnv ? att * compEnv + (1 - att) * lvl : rel * compEnv + (1 - rel) * lvl;
                const float envDb = juce::Decibels::gainToDecibels (compEnv + 1e-9f);
                const float over = envDb - threshDb;
                const float grDb = over > 0 ? over * (1.0f - 1.0f / ratio) : 0.0f;
                const float g = juce::Decibels::decibelsToGain (-grDb) * makeup;
                L[i] *= g; R[i] *= g;
            }
            break;
        }
        case FxP::Chorus:
        {
            const float rate = 0.05f + p[0] * p[0] * 6.0f;
            const float depth = p[1] * 0.009f * fsr;
            const float base = 0.012f * fsr;
            const float fb = p[2] * 0.8f;
            const int size = (int) chL.size();
            for (int i = 0; i < n; ++i)
            {
                chPhase += rate / fsr; if (chPhase >= 1.0f) chPhase -= 1.0f;
                const float m1 = base + depth * (0.5f + 0.5f * std::sin (DSP::twoPi * chPhase));
                const float m2 = base + depth * (0.5f + 0.5f * std::sin (DSP::twoPi * chPhase + 1.9f));
                const float yL = readFrac (chL, (float) chW - m1), yR = readFrac (chR, (float) chW - m2);
                chL[(size_t) chW] = L[i] + yL * fb; chR[(size_t) chW] = R[i] + yR * fb;
                if (++chW >= size) chW = 0;
                L[i] += (yL - L[i]) * mix * 0.5f; R[i] += (yR - R[i]) * mix * 0.5f;
            }
            break;
        }
        case FxP::Stutter:
        {
            static const double sliceTable[] = { 0.125, 0.25, 0.5, 1.0 };
            const double sliceBeats = sliceTable[std::clamp ((int) (p[0] * 3.99f), 0, 3)];
            const int sliceLen = std::max (16, std::min ((int) stL.size() / 2, (int) (sliceBeats * 60.0 / ctx.bpm * sr)));
            const int repeats = 1 + (int) (p[2] * 7.0f);
            const int size = (int) stL.size();
            const double beatsPerSample = ctx.bpm / 60.0 / sr;
            for (int i = 0; i < n; ++i)
            {
                stL[(size_t) stW] = L[i]; stR[(size_t) stW] = R[i];
                const double beat = ctx.beat + i * beatsPerSample;
                const auto slice = (juce::int64) std::floor (beat / sliceBeats);
                if (ctx.playing && slice != stSlice)
                {
                    stSlice = slice;
                    if (stRemaining <= 0 && rng.uni() < p[1])
                    {
                        stLen = sliceLen;
                        stReadStart = (stW - sliceLen + size) % size;
                        stReadPos = 0;
                        stRemaining = sliceLen * repeats;
                    }
                }
                if (++stW >= size) stW = 0;
                if (stRemaining > 0)
                {
                    const int idx = (stReadStart + (stReadPos % stLen)) % size;
                    // short fade at slice edges to avoid clicks
                    const int ph = stReadPos % stLen;
                    const float edge = std::min (1.0f, std::min ((float) ph, (float) (stLen - ph)) / 64.0f);
                    const float a = stL[(size_t) idx] * edge, b = stR[(size_t) idx] * edge;
                    L[i] += (a - L[i]) * mix; R[i] += (b - R[i]) * mix;
                    ++stReadPos; --stRemaining;
                }
            }
            break;
        }
        default: break;
    }
}
