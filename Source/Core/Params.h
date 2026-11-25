#pragma once
#include <juce_core/juce_core.h>
#include <vector>

/** Static parameter descriptor tables shared by model, engine and UI. */
struct ParamDef
{
    const char* id;
    const char* label;
    float minV, maxV, defV;
    float skewCentre;               // 0 => linear
    juce::StringArray choices;      // non-empty => discrete choice param
    const char* unit = "";

    bool isChoice() const { return choices.size() > 0; }
    float clamp (float v) const { return juce::jlimit (minV, maxV, v); }
    float toNorm (float v) const;
    float fromNorm (float n) const;
};

namespace SynthP
{
    enum Index
    {
        engine = 0, osc1Wave, osc2Wave, osc2Semi, osc2Detune, oscMix, subLevel, noiseLevel,
        fmRatio, fmIndex, fmFeedback, fmCarrier,
        wtPos, wtWarp,
        grainSize, grainDensity, grainPos, grainSpray, grainPitchRand,
        filterType, cutoff, reso, filterEnv, keyTrack, drive,
        ampA, ampD, ampS, ampR, modA, modD, modS, modR,
        lfo1Rate, lfo1Shape, lfo2Rate, lfo2Shape,
        glide, unisonDetune, gain,
        m0src, m0dst, m0amt, m1src, m1dst, m1amt, m2src, m2dst, m2amt,
        m3src, m3dst, m3amt, m4src, m4dst, m4amt, m5src, m5dst, m5amt,
        NumParams
    };
    const std::vector<ParamDef>& defs();
    int indexOf (const juce::String& id);

    enum ModSrc { srcNone = 0, srcLfo1, srcLfo2, srcModEnv, srcVelocity, srcNote, srcRandom, srcFractalA, srcFractalB, NumSrc };
    enum ModDst { dstNone = 0, dstPitch, dstCutoff, dstReso, dstWtPos, dstFmIndex, dstGrainPos, dstGrainSize, dstAmp, dstPan, dstOscMix, dstDrive, NumDst };
    juce::StringArray srcNames();
    juce::StringArray dstNames();
}

namespace PadP
{
    enum Index { tune = 0, gain, pan, attack, decay, punch, sliceStart, sliceEnd, cutoff, reso, drive, crush, reverse, choke, NumParams };
    const std::vector<ParamDef>& defs();
    int indexOf (const juce::String& id);
    constexpr int numPads = 12;
    constexpr int baseNote = 36;
}

namespace FxP
{
    enum Type { None = 0, Filter, Saturation, Distortion, Bitcrush, Delay, Reverb, Compressor, Chorus, Stutter, NumTypes };
    juce::StringArray typeNames();
    /** Labels for the 4 normalised params of each effect type. */
    juce::StringArray paramLabels (int type);
    float defaultValue (int type, int paramIndex);
}

namespace MixP
{
    constexpr int numSends = 4;
    constexpr int numInserts = 3;
    float dbToGain (float db);
}

/** Automation target ids: "vol", "pan", "send0".."send3", "synth.<id>", "fx<slot>.p<n>" */
namespace AutoTargets
{
    juce::StringArray allIds (bool isSynth);
    juce::String label (const juce::String& id);
    /** Range for the target (for normalised <-> real conversion). */
    void range (const juce::String& id, float& minV, float& maxV);
}
