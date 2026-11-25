#include "Synth.h"
#include "../Fractal/MandelMath.h"

using namespace SynthP;

SynthEngine::SynthEngine()
{
    for (int i = 0; i < NumParams; ++i)
        params[i].store (defs()[(size_t) i].defV);
    // default fractal wavetable
    auto wt = std::make_shared<WavetableData>();
    wt->frames = 16; wt->size = 2048;
    wt->data = MandelMath::buildWavetable (-0.7436, 0.1318, wt->frames, wt->size);
    setWavetable (wt);
    for (int i = 0; i < numVoices; ++i)
    {
        voices[(size_t) i].noise = DSP::Rng ((uint32_t) (1234 + i * 77));
        voices[(size_t) i].grng = DSP::Rng ((uint32_t) (999 + i * 31));
    }
}

void SynthEngine::prepare (double sampleRate, int maxBlock)
{
    sr = sampleRate;
    lfo1Buf.assign ((size_t) juce::jmax (1, maxBlock), 0.0f);
    lfo2Buf.assign ((size_t) juce::jmax (1, maxBlock), 0.0f);
    allNotesOff (true);
}

void SynthEngine::setWavetable (std::shared_ptr<WavetableData> wt)
{
    if (wt == nullptr || wt->frames <= 0) return;
    // default grain source: render the wavetable sweeping across its frames (2 s @ 110 Hz)
    auto g = std::make_shared<SampleData>();
    const int len = 88200;
    g->buffer.setSize (1, len);
    g->sampleRate = 44100.0;
    float ph = 0.0f;
    for (int i = 0; i < len; ++i)
    {
        const float fpos = (float) i / (float) len * (float) (wt->frames - 1);
        const int f0 = (int) fpos; const float ff = fpos - (float) f0;
        const float* a = wt->frame (f0); const float* b = wt->frame (f0 + 1);
        const int idx = (int) (ph * (float) wt->size) % wt->size;
        g->buffer.setSample (0, i, (a[idx] + (b[idx] - a[idx]) * ff) * 0.8f);
        ph += 110.0f / 44100.0f; if (ph >= 1.0f) ph -= 1.0f;
    }
    auto oldWt = wavetableOwner;
    wavetableOwner = wt;
    wavetable.store (wt.get());
    DeferredDeleter::get().retire (oldWt);
    auto oldG = defaultGrainOwner;
    defaultGrainOwner = g;
    if (grainOwner == nullptr) grainSource.store (g.get());
    DeferredDeleter::get().retire (oldG);
}

void SynthEngine::setGrainSource (std::shared_ptr<const SampleData> s)
{
    auto old = grainOwner;
    grainOwner = s;
    grainSource.store (s != nullptr ? s.get() : defaultGrainOwner.get());
    DeferredDeleter::get().retire (std::const_pointer_cast<SampleData> (old));
}

int SynthEngine::activeVoiceCount() const
{
    int c = 0;
    for (auto& v : voices) if (v.active) ++c;
    return c;
}

void SynthEngine::allNotesOff (bool hard)
{
    for (auto& v : voices)
    {
        if (hard) { v.active = false; v.amp.kill(); v.mod.kill(); for (auto& g : v.grains) g.active = false; }
        else { v.amp.noteOff(); v.mod.noteOff(); v.released = true; }
    }
}

void SynthEngine::noteOn (int note, float vel)
{
    Voice* target = nullptr;
    for (auto& v : voices) if (! v.active) { target = &v; break; }
    if (target == nullptr)
    {
        // steal: prefer released voices, then oldest
        juce::uint32 best = 0xffffffff;
        for (auto& v : voices)
        {
            const juce::uint32 score = v.age - (v.released ? 0x40000000u : 0u);
            if (score < best) { best = score; target = &v; }
        }
    }
    auto& v = *target;
    const float glideTime = params[SynthP::glide].load();
    v.active = true; v.released = false;
    v.note = note; v.velocity = vel;
    v.pitchTarget = (float) note;
    v.pitch = glideTime > 0.001f ? lastPitch : (float) note;
    lastPitch = (float) note;
    v.age = ++ageCounter;
    v.random = v.noise.uni();
    v.fmCar = v.fmMod = v.fmFb = 0.0f;
    v.grainTimer = 0.0f;
    for (auto& g : v.grains) g.active = false;
    v.fL.reset(); v.fR.reset();
    v.filterCounter = 0;
    v.amp.noteOn(); v.mod.noteOn();
    if (v.amp.value > 0.0f) v.amp.value *= 0.5f; // soften retrigger of stolen voice
}

void SynthEngine::noteOff (int note)
{
    for (auto& v : voices)
        if (v.active && ! v.released && v.note == note)
        {
            v.amp.noteOff(); v.mod.noteOff(); v.released = true;
        }
}

void SynthEngine::process (float* L, float* R, int numSamples, const NoteEvent* events, int numEvents,
                           const float* ov, const GlobalMods& mods)
{
    float p[NumParams];
    for (int i = 0; i < NumParams; ++i)
        p[i] = (ov != nullptr && ov[i] > -1e29f) ? ov[i] : params[i].load (std::memory_order_relaxed);

    const int lfoN = juce::jmin (numSamples, (int) lfo1Buf.size());
    const float inc1 = p[lfo1Rate] / (float) sr, inc2 = p[lfo2Rate] / (float) sr;
    for (int i = 0; i < lfoN; ++i)
    {
        lfo1Buf[(size_t) i] = lfo1.process ((int) p[lfo1Shape], inc1);
        lfo2Buf[(size_t) i] = lfo2.process ((int) p[lfo2Shape], inc2);
    }

    for (auto& v : voices)
    {
        v.amp.set (p[ampA], p[ampD], p[ampS], p[ampR], (float) sr);
        v.mod.set (p[modA], p[modD], p[modS], p[modR], (float) sr);
    }

    int pos = 0, ev = 0;
    while (pos < numSamples)
    {
        while (ev < numEvents && events[ev].offset <= pos)
        {
            if (events[ev].velocity > 0) noteOn (events[ev].note, events[ev].velocity);
            else noteOff (events[ev].note);
            ++ev;
        }
        const int next = ev < numEvents ? juce::jmin (numSamples, juce::jmax (pos + 1, events[ev].offset)) : numSamples;
        blockOffset = pos;
        for (auto& v : voices)
            if (v.active) renderVoice (v, L + pos, R + pos, pos, next - pos, p, mods);
        pos = next;
    }
    while (ev < numEvents) { if (events[ev].velocity > 0) noteOn (events[ev].note, events[ev].velocity); else noteOff (events[ev].note); ++ev; }
}

void SynthEngine::renderVoice (Voice& v, float* L, float* R, int start, int n, const float* p, const GlobalMods& mods)
{
    const int engineType = (int) p[engine];
    const float fsr = (float) sr;
    const float glideCoeff = p[glide] > 0.001f ? std::exp (-1.0f / (p[glide] * fsr * 0.3f)) : 0.0f;
    const auto* wt = wavetable.load (std::memory_order_acquire);
    const auto* gs = grainSource.load (std::memory_order_acquire);
    const float gain = juce::Decibels::decibelsToGain (p[SynthP::gain]) * 0.35f;
    const int ftype = (int) p[filterType];
    const float fA = mods.fractalA.load (std::memory_order_relaxed), fB = mods.fractalB.load (std::memory_order_relaxed);

    for (int i = 0; i < n; ++i)
    {
        if (! v.amp.active()) { v.active = false; return; }
        const float ampEnv = v.amp.next();
        const float modEnv = v.mod.next();
        const int bi = juce::jmin (start + i, (int) lfo1Buf.size() - 1);

        // ---------------- modulation matrix
        float mPitch = 0, mCut = 0, mRes = 0, mWt = 0, mFm = 0, mGPos = 0, mGSize = 0, mAmp = 0, mPan = 0, mMix = 0, mDrive = 0;
        for (int s = 0; s < 6; ++s)
        {
            const int src = (int) p[m0src + s * 3];
            const int dst = (int) p[m0dst + s * 3];
            const float amt = p[m0amt + s * 3];
            if (src == srcNone || dst == dstNone || amt == 0.0f) continue;
            float sv = 0.0f;
            switch (src)
            {
                case srcLfo1: sv = lfo1Buf[(size_t) bi]; break;
                case srcLfo2: sv = lfo2Buf[(size_t) bi]; break;
                case srcModEnv: sv = modEnv; break;
                case srcVelocity: sv = v.velocity; break;
                case srcNote: sv = (v.pitch - 60.0f) / 48.0f; break;
                case srcRandom: sv = v.random * 2.0f - 1.0f; break;
                case srcFractalA: sv = fA * 2.0f - 1.0f; break;
                case srcFractalB: sv = fB * 2.0f - 1.0f; break;
                default: break;
            }
            const float m = sv * amt;
            switch (dst)
            {
                case dstPitch: mPitch += m * 24.0f; break;
                case dstCutoff: mCut += m * 5.0f; break;
                case dstReso: mRes += m; break;
                case dstWtPos: mWt += m; break;
                case dstFmIndex: mFm += m * 10.0f; break;
                case dstGrainPos: mGPos += m; break;
                case dstGrainSize: mGSize += m; break;
                case dstAmp: mAmp += m; break;
                case dstPan: mPan += m; break;
                case dstOscMix: mMix += m; break;
                case dstDrive: mDrive += m; break;
                default: break;
            }
        }

        // ---------------- pitch
        v.pitch = v.pitchTarget + (v.pitch - v.pitchTarget) * glideCoeff;
        const float note = v.pitch + mPitch;
        const float hz = DSP::midiToHz (note);
        const float inc = hz / fsr;

        // ---------------- oscillator engines
        float mono = 0.0f, outL = 0.0f, outR = 0.0f;
        bool stereo = false;
        switch (engineType)
        {
            case 0: // subtractive
            {
                const float det = std::exp2 ((p[osc2Semi] + p[osc2Detune] * 0.01f + p[unisonDetune] * 0.15f) / 12.0f);
                const float a = v.o1.process ((int) p[osc1Wave], inc);
                const float b = v.o2.process ((int) p[osc2Wave], inc * det);
                const float sb = v.sub.process (3, inc * 0.5f);
                const float mix = juce::jlimit (0.0f, 1.0f, p[oscMix] + mMix);
                mono = a * (1.0f - mix) + b * mix + sb * p[subLevel] + v.noise.bi() * p[noiseLevel];
                break;
            }
            case 1: // FM (2-op + feedback)
            {
                const float idx = juce::jmax (0.0f, p[fmIndex] + mFm);
                const float modOut = std::sin (DSP::twoPi * v.fmMod + v.fmFb * p[fmFeedback] * 3.0f);
                v.fmFb = modOut;
                const float car = std::sin (DSP::twoPi * v.fmCar + idx * modOut);
                v.fmMod += inc * p[fmRatio]; v.fmMod -= std::floor (v.fmMod);
                v.fmCar += inc * p[fmCarrier]; v.fmCar -= std::floor (v.fmCar);
                mono = car + v.sub.process (3, inc * 0.5f) * p[subLevel] + v.noise.bi() * p[noiseLevel] * 0.5f;
                break;
            }
            case 2: // wavetable
            {
                if (wt != nullptr && wt->size > 0)
                {
                    const float pos = juce::jlimit (0.0f, 1.0f, p[wtPos] + mWt) * (float) (wt->frames - 1);
                    const int f0 = (int) pos; const float ff = pos - (float) f0;
                    // warp: bend the phase (phase distortion)
                    float ph = v.wtPhase;
                    const float w = p[wtWarp];
                    if (w > 0.0f) ph = std::pow (ph, 1.0f + w * 3.0f);
                    const float x = ph * (float) wt->size;
                    const int i0 = (int) x % wt->size, i1 = (i0 + 1) % wt->size;
                    const float fr = x - std::floor (x);
                    const float* a = wt->frame (f0); const float* b = wt->frame (f0 + 1);
                    const float sa = a[i0] + (a[i1] - a[i0]) * fr, sb = b[i0] + (b[i1] - b[i0]) * fr;
                    mono = sa + (sb - sa) * ff;
                    v.wtPhase += juce::jmin (inc, 0.45f); if (v.wtPhase >= 1.0f) v.wtPhase -= 1.0f;
                }
                mono += v.sub.process (3, inc * 0.5f) * p[subLevel];
                break;
            }
            default: // granular
            {
                stereo = true;
                if (gs != nullptr && gs->numFrames() > 16)
                {
                    const int srcLen = gs->numFrames();
                    const float density = juce::jmax (1.0f, p[grainDensity]);
                    v.grainTimer -= 1.0f;
                    if (v.grainTimer <= 0.0f)
                    {
                        v.grainTimer += fsr / density;
                        for (auto& g : v.grains)
                            if (! g.active)
                            {
                                const float sizeMs = juce::jlimit (5.0f, 800.0f, p[grainSize] * (1.0f + mGSize * 2.0f));
                                const float posN = juce::jlimit (0.0f, 1.0f, p[grainPos] + mGPos + v.grng.bi() * p[grainSpray] * 0.5f);
                                g.active = true; g.age = 0;
                                g.len = juce::jmax (8, (int) (sizeMs * 0.001f * fsr));
                                g.pos = posN * (float) (srcLen - 2);
                                const float pr = p[grainPitchRand] * v.grng.bi();
                                g.inc = std::exp2 ((note - 60.0f + pr) / 12.0f) * (float) (gs->sampleRate / sr);
                                const float pan = juce::jlimit (-1.0f, 1.0f, v.grng.bi() * p[grainSpray]);
                                g.panL = DSP::panL (pan); g.panR = DSP::panR (pan);
                                break;
                            }
                    }
                    for (auto& g : v.grains)
                    {
                        if (! g.active) continue;
                        const float wnd = 0.5f - 0.5f * std::cos (DSP::twoPi * (float) g.age / (float) g.len);
                        float rp = g.pos;
                        while (rp >= (float) (srcLen - 1)) rp -= (float) (srcLen - 1);
                        const int i0 = (int) rp; const float fr = rp - (float) i0;
                        const float s0 = gs->get (0, i0), s1 = gs->get (0, juce::jmin (srcLen - 1, i0 + 1));
                        const float smp = (s0 + (s1 - s0) * fr) * wnd;
                        outL += smp * g.panL; outR += smp * g.panR;
                        g.pos += g.inc;
                        if (++g.age >= g.len) g.active = false;
                    }
                    outL *= 0.5f; outR *= 0.5f;
                }
                break;
            }
        }

        // ---------------- drive + filter
        const float drv = juce::jlimit (0.0f, 1.0f, p[drive] + mDrive);
        if (v.filterCounter-- <= 0)
        {
            v.filterCounter = 16;
            const float oct = p[filterEnv] * modEnv * 6.0f + mCut + p[keyTrack] * (note - 60.0f) / 12.0f;
            const float fc = p[cutoff] * std::exp2 (oct);
            const float res = juce::jlimit (0.0f, 0.98f, p[reso] + mRes);
            v.fL.set (fc, res, fsr); v.fR.set (fc, res, fsr);
        }
        const float driveGain = 1.0f + drv * 8.0f;
        const float level = ampEnv * (0.4f + 0.6f * v.velocity) * gain * juce::jlimit (0.0f, 2.0f, 1.0f + mAmp);
        const float pan = juce::jlimit (-1.0f, 1.0f, mPan);
        if (! stereo)
        {
            if (drv > 0.001f) mono = std::tanh (mono * driveGain) / std::tanh (driveGain) ;
            const float y = v.fL.process (mono, ftype) * level;
            L[i] += y * DSP::panL (pan) * 1.414f;
            R[i] += y * DSP::panR (pan) * 1.414f;
        }
        else
        {
            if (drv > 0.001f) { outL = std::tanh (outL * driveGain) / std::tanh (driveGain); outR = std::tanh (outR * driveGain) / std::tanh (driveGain); }
            L[i] += v.fL.process (outL, ftype) * level * DSP::panL (pan) * 1.414f;
            R[i] += v.fR.process (outR, ftype) * level * DSP::panR (pan) * 1.414f;
        }
    }
}
