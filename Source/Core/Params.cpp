#include "Params.h"
#include <unordered_map>

float ParamDef::toNorm (float v) const
{
    if (maxV <= minV) return 0.0f;
    const float n = (clamp (v) - minV) / (maxV - minV);
    if (skewCentre > 0.0f && ! isChoice())
    {
        const float skew = std::log (0.5f) / std::log ((skewCentre - minV) / (maxV - minV));
        return std::pow (n, skew);
    }
    return n;
}

float ParamDef::fromNorm (float n) const
{
    n = juce::jlimit (0.0f, 1.0f, n);
    if (skewCentre > 0.0f && ! isChoice())
    {
        const float skew = std::log (0.5f) / std::log ((skewCentre - minV) / (maxV - minV));
        n = std::pow (n, 1.0f / skew);
    }
    float v = minV + n * (maxV - minV);
    if (isChoice()) v = std::round (v);
    return v;
}

static juce::StringArray waveNames() { return { "Saw", "Square", "Triangle", "Sine", "Noise" }; }
static juce::StringArray lfoShapes() { return { "Sine", "Triangle", "Saw", "Square", "S&H" }; }

namespace SynthP
{
    juce::StringArray srcNames() { return { "-", "LFO 1", "LFO 2", "Mod Env", "Velocity", "Note", "Random", "Fractal A", "Fractal B" }; }
    juce::StringArray dstNames() { return { "-", "Pitch", "Cutoff", "Resonance", "WT Position", "FM Index", "Grain Pos", "Grain Size", "Amp", "Pan", "Osc Mix", "Drive" }; }

    const std::vector<ParamDef>& defs()
    {
        static const std::vector<ParamDef> d = [] {
            const auto srcCount = (float) (NumSrc - 1), dstCount = (float) (NumDst - 1);
            std::vector<ParamDef> v = {
                { "engine", "Engine", 0, 3, 0, 0, { "Subtractive", "FM", "Wavetable", "Granular" } },
                { "osc1Wave", "Osc 1", 0, 4, 0, 0, waveNames() },
                { "osc2Wave", "Osc 2", 0, 4, 1, 0, waveNames() },
                { "osc2Semi", "Osc2 Semi", -24, 24, 7, 0, {}, "st" },
                { "osc2Detune", "Detune", -50, 50, 6, 0, {}, "ct" },
                { "oscMix", "Osc Mix", 0, 1, 0.35f, 0, {} },
                { "subLevel", "Sub", 0, 1, 0.2f, 0, {} },
                { "noiseLevel", "Noise", 0, 1, 0.0f, 0, {} },
                { "fmRatio", "FM Ratio", 0.25f, 16, 2, 2.0f, {} },
                { "fmIndex", "FM Index", 0, 20, 3, 4.0f, {} },
                { "fmFeedback", "Feedback", 0, 1, 0.1f, 0, {} },
                { "fmCarrier", "Carrier", 0.5f, 4, 1, 0, {} },
                { "wtPos", "WT Pos", 0, 1, 0.3f, 0, {} },
                { "wtWarp", "WT Warp", 0, 1, 0.0f, 0, {} },
                { "grainSize", "Grain Size", 5, 400, 80, 60, {}, "ms" },
                { "grainDensity", "Density", 1, 120, 30, 20, {}, "/s" },
                { "grainPos", "Grain Pos", 0, 1, 0.3f, 0, {} },
                { "grainSpray", "Spray", 0, 1, 0.15f, 0, {} },
                { "grainPitchRand", "Pitch Rand", 0, 12, 0.0f, 0, {}, "st" },
                { "filterType", "Filter", 0, 2, 0, 0, { "Low Pass", "Band Pass", "High Pass" } },
                { "cutoff", "Cutoff", 20, 18000, 2400, 1000, {}, "Hz" },
                { "reso", "Resonance", 0, 1, 0.25f, 0, {} },
                { "filterEnv", "Env Amt", -1, 1, 0.35f, 0, {} },
                { "keyTrack", "Key Track", 0, 1, 0.3f, 0, {} },
                { "drive", "Drive", 0, 1, 0.1f, 0, {} },
                { "ampA", "Attack", 0.001f, 5, 0.005f, 0.3f, {}, "s" },
                { "ampD", "Decay", 0.001f, 5, 0.3f, 0.5f, {}, "s" },
                { "ampS", "Sustain", 0, 1, 0.7f, 0, {} },
                { "ampR", "Release", 0.001f, 8, 0.35f, 0.6f, {}, "s" },
                { "modA", "M Attack", 0.001f, 5, 0.01f, 0.3f, {}, "s" },
                { "modD", "M Decay", 0.001f, 5, 0.4f, 0.5f, {}, "s" },
                { "modS", "M Sustain", 0, 1, 0.2f, 0, {} },
                { "modR", "M Release", 0.001f, 8, 0.4f, 0.6f, {}, "s" },
                { "lfo1Rate", "LFO1 Rate", 0.01f, 30, 0.8f, 2.0f, {}, "Hz" },
                { "lfo1Shape", "LFO1 Shape", 0, 4, 0, 0, lfoShapes() },
                { "lfo2Rate", "LFO2 Rate", 0.01f, 30, 4.0f, 2.0f, {}, "Hz" },
                { "lfo2Shape", "LFO2 Shape", 0, 4, 4, 0, lfoShapes() },
                { "glide", "Glide", 0, 1, 0, 0.15f, {}, "s" },
                { "unisonDetune", "Unison", 0, 1, 0.1f, 0, {} },
                { "gain", "Gain", -36, 6, -6, 0, {}, "dB" },
            };
            const char* srcIds[] = { "m0src", "m1src", "m2src", "m3src", "m4src", "m5src" };
            const char* dstIds[] = { "m0dst", "m1dst", "m2dst", "m3dst", "m4dst", "m5dst" };
            const char* amtIds[] = { "m0amt", "m1amt", "m2amt", "m3amt", "m4amt", "m5amt" };
            const float defSrc[] = { 1, 7, 3, 0, 0, 0 };
            const float defDst[] = { 2, 4, 5, 0, 0, 0 };
            const float defAmt[] = { 0.15f, 0.4f, 0.3f, 0, 0, 0 };
            for (int i = 0; i < 6; ++i)
            {
                v.push_back ({ srcIds[i], "Source", 0, srcCount, defSrc[i], 0, srcNames() });
                v.push_back ({ dstIds[i], "Dest", 0, dstCount, defDst[i], 0, dstNames() });
                v.push_back ({ amtIds[i], "Amount", -1, 1, defAmt[i], 0, {} });
            }
            jassert ((int) v.size() == NumParams);
            return v;
        }();
        return d;
    }

    int indexOf (const juce::String& id)
    {
        static const auto map = [] {
            std::unordered_map<std::string, int> m;
            int i = 0;
            for (auto& p : defs()) m[p.id] = i++;
            return m;
        }();
        auto it = map.find (id.toStdString());
        return it == map.end() ? -1 : it->second;
    }
}

namespace PadP
{
    const std::vector<ParamDef>& defs()
    {
        static const std::vector<ParamDef> d = {
            { "tune", "Tune", -24, 24, 0, 0, {}, "st" },
            { "gain", "Gain", -36, 6, -2, 0, {}, "dB" },
            { "pan", "Pan", -1, 1, 0, 0, {} },
            { "attack", "Transient", 0, 1, 0.3f, 0, {} },
            { "decay", "Decay", 0.02f, 4, 1.2f, 0.5f, {}, "s" },
            { "punch", "Sustain", -1, 1, 0, 0, {} },
            { "sliceStart", "Start", 0, 1, 0, 0, {} },
            { "sliceEnd", "End", 0, 1, 1, 0, {} },
            { "cutoff", "Cutoff", 40, 20000, 20000, 1500, {}, "Hz" },
            { "reso", "Reso", 0, 1, 0.1f, 0, {} },
            { "drive", "Drive", 0, 1, 0, 0, {} },
            { "crush", "Crush", 0, 1, 0, 0, {} },
            { "reverse", "Reverse", 0, 1, 0, 0, { "Fwd", "Rev" } },
            { "choke", "Choke", 0, 4, 0, 0, { "-", "1", "2", "3", "4" } },
        };
        return d;
    }

    int indexOf (const juce::String& id)
    {
        int i = 0;
        for (auto& p : defs()) { if (id == p.id) return i; ++i; }
        return -1;
    }
}

namespace FxP
{
    juce::StringArray typeNames()
    {
        return { "-", "Filter", "Saturation", "Distortion", "Bitcrush", "Delay", "Reverb", "Compressor", "Chorus", "Stutter" };
    }

    juce::StringArray paramLabels (int type)
    {
        switch (type)
        {
            case Filter:     return { "Cutoff", "Reso", "Mode", "Mix" };
            case Saturation: return { "Drive", "Tone", "Bias", "Mix" };
            case Distortion: return { "Drive", "Fold", "Tone", "Mix" };
            case Bitcrush:   return { "Bits", "Downsample", "Jitter", "Mix" };
            case Delay:      return { "Time", "Feedback", "Tone", "Mix" };
            case Reverb:     return { "Size", "Damp", "Width", "Mix" };
            case Compressor: return { "Threshold", "Ratio", "Release", "Makeup" };
            case Chorus:     return { "Rate", "Depth", "Feedback", "Mix" };
            case Stutter:    return { "Length", "Chance", "Repeats", "Mix" };
            default:         return { "-", "-", "-", "-" };
        }
    }

    float defaultValue (int type, int i)
    {
        static const float table[NumTypes][4] = {
            { 0, 0, 0, 0 },
            { 0.6f, 0.3f, 0.0f, 1.0f },
            { 0.4f, 0.6f, 0.1f, 1.0f },
            { 0.5f, 0.3f, 0.5f, 0.7f },
            { 0.5f, 0.3f, 0.0f, 1.0f },
            { 0.375f, 0.4f, 0.6f, 0.3f },
            { 0.6f, 0.4f, 1.0f, 0.3f },
            { 0.6f, 0.4f, 0.3f, 0.2f },
            { 0.2f, 0.4f, 0.2f, 0.5f },
            { 0.4f, 0.25f, 0.5f, 1.0f },
        };
        return table[juce::jlimit (0, NumTypes - 1, type)][juce::jlimit (0, 3, i)];
    }
}

namespace MixP
{
    float dbToGain (float db) { return db <= -59.9f ? 0.0f : std::pow (10.0f, db / 20.0f); }
}

namespace AutoTargets
{
    juce::StringArray allIds (bool isSynth)
    {
        juce::StringArray ids { "vol", "pan", "send0", "send1", "send2", "send3" };
        for (int s = 0; s < MixP::numInserts; ++s)
            for (int p = 0; p < 4; ++p)
                ids.add ("fx" + juce::String (s) + ".p" + juce::String (p));
        if (isSynth)
            for (auto& d : SynthP::defs())
                if (! d.isChoice()) ids.add ("synth." + juce::String (d.id));
        return ids;
    }

    juce::String label (const juce::String& id)
    {
        if (id == "vol") return "Volume";
        if (id == "pan") return "Pan";
        if (id.startsWith ("send")) return "Send " + juce::String::charToString ((juce::juce_wchar) ('A' + id.getTrailingIntValue()));
        if (id.startsWith ("fx")) return "Insert " + id.substring (2, 3) + " Param " + id.getLastCharacters (1);
        if (id.startsWith ("synth."))
        {
            auto idx = SynthP::indexOf (id.fromFirstOccurrenceOf (".", false, false));
            if (idx >= 0) return juce::String ("Synth ") + SynthP::defs()[(size_t) idx].label;
        }
        return id;
    }

    void range (const juce::String& id, float& minV, float& maxV)
    {
        minV = 0.0f; maxV = 1.0f;
        if (id == "vol") { minV = -60.0f; maxV = 6.0f; }
        else if (id == "pan") { minV = -1.0f; maxV = 1.0f; }
        else if (id.startsWith ("synth."))
        {
            auto idx = SynthP::indexOf (id.fromFirstOccurrenceOf (".", false, false));
            if (idx >= 0) { minV = SynthP::defs()[(size_t) idx].minV; maxV = SynthP::defs()[(size_t) idx].maxV; }
        }
    }
}
