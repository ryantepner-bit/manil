#include "DrumMachine.h"

DrumEngine::DrumEngine()
{
    for (int i = 0; i < PadP::numPads; ++i)
    {
        for (int k = 0; k < PadP::NumParams; ++k)
            pads[(size_t) i].params[k].store (PadP::defs()[(size_t) k].defV);
        setPadSample (i, synthesiseDrum (i));
    }
}

void DrumEngine::prepare (double sampleRate, int)
{
    sr = sampleRate;
    allNotesOff();
}

void DrumEngine::allNotesOff()
{
    for (auto& pv : voices) for (auto& v : pv) v.active = false;
}

void DrumEngine::setPadSample (int padIndex, std::shared_ptr<const SampleData> s)
{
    auto& p = pad (padIndex);
    auto old = p.owner;
    p.owner = s;
    p.sample.store (s.get(), std::memory_order_release);
    DeferredDeleter::get().retire (std::const_pointer_cast<SampleData> (old));
}

void DrumEngine::choke (int group, int exceptPad)
{
    if (group <= 0) return;
    for (int i = 0; i < PadP::numPads; ++i)
        if (i != exceptPad && (int) pads[(size_t) i].params[PadP::choke].load() == group)
            for (auto& v : voices[(size_t) i])
                if (v.active) v.envMul = std::min (v.envMul, 0.995f); // fast fade (no click)
}

void DrumEngine::trigger (int padIndex, float velocity)
{
    auto& p = pads[(size_t) padIndex];
    const auto* data = p.sample.load (std::memory_order_acquire);
    if (data == nullptr || data->numFrames() < 2) return;
    choke ((int) p.params[PadP::choke].load(), padIndex);
    auto& v = voices[(size_t) padIndex][(size_t) nextVoice[(size_t) padIndex]];
    nextVoice[(size_t) padIndex] = (nextVoice[(size_t) padIndex] + 1) % voicesPerPad;

    const float s0 = juce::jlimit (0.0f, 1.0f, p.params[PadP::sliceStart].load());
    const float s1 = juce::jlimit (0.0f, 1.0f, p.params[PadP::sliceEnd].load());
    const double lo = juce::jmin (s0, s1) * (data->numFrames() - 1), hi = juce::jmax (s0, s1) * (data->numFrames() - 1);
    v.data = data;
    v.startF = lo; v.endF = juce::jmax (lo + 1.0, hi);
    v.reverse = p.params[PadP::reverse].load() > 0.5f;
    v.pos = v.reverse ? v.endF : v.startF;
    v.inc = std::exp2 (p.params[PadP::tune].load() / 12.0) * data->sampleRate / sr;
    v.vel = velocity;
    v.age = 0;
    v.env = 1.0f;
    const float decay = juce::jmax (0.01f, p.params[PadP::decay].load());
    v.envMul = std::exp (-6.9f / (decay * (float) sr)); // -60 dB after 'decay' seconds
    v.fL.reset(); v.fR.reset();
    v.holdCount = 0;
    v.active = true;
}

void DrumEngine::process (float* L, float* R, int numSamples, const NoteEvent* events, int numEvents)
{
    int ev = 0;
    const float fsr = (float) sr;
    for (int padIdx = 0; padIdx < PadP::numPads; ++padIdx)
    {
        auto& p = pads[(size_t) padIdx];
        const float cutoff = p.params[PadP::cutoff].load(), reso = p.params[PadP::reso].load();
        for (auto& v : voices[(size_t) padIdx]) { v.fL.set (cutoff, reso, fsr); v.fR.set (cutoff, reso, fsr); }
    }

    // render in segments split at event offsets
    int pos = 0;
    while (pos <= numSamples)
    {
        while (ev < numEvents && events[ev].offset <= pos)
        {
            const int padIdx = events[ev].note - PadP::baseNote;
            if (events[ev].velocity > 0 && padIdx >= 0 && padIdx < PadP::numPads) trigger (padIdx, events[ev].velocity);
            ++ev;
        }
        if (pos == numSamples) break;
        const int next = ev < numEvents ? juce::jmin (numSamples, juce::jmax (pos + 1, events[ev].offset)) : numSamples;

        for (int padIdx = 0; padIdx < PadP::numPads; ++padIdx)
        {
            auto& p = pads[(size_t) padIdx];
            const float gain = juce::Decibels::decibelsToGain (p.params[PadP::gain].load());
            const float pan = p.params[PadP::pan].load();
            const float gl = DSP::panL (pan), gr = DSP::panR (pan);
            const float attack = p.params[PadP::attack].load();
            const float punch = p.params[PadP::punch].load();
            const float drive = p.params[PadP::drive].load();
            const float crush = p.params[PadP::crush].load();
            const bool filterOn = p.params[PadP::cutoff].load() < 19500.0f;
            const int transientLen = (int) (0.012f * fsr);
            const int holdLen = 1 + (int) (crush * crush * 30.0f);
            const float levels = std::pow (2.0f, 15.0f - crush * 12.0f);
            float peak = 0.0f;

            for (auto& v : voices[(size_t) padIdx])
            {
                if (! v.active) continue;
                const auto* d = v.data;
                const int nf = d->numFrames();
                const bool st = d->buffer.getNumChannels() > 1;
                const float* c0 = d->buffer.getReadPointer (0);
                const float* c1 = d->buffer.getReadPointer (st ? 1 : 0);
                for (int i = pos; i < next; ++i)
                {
                    if (v.pos < v.startF || v.pos > v.endF || v.env < 0.0005f) { v.active = false; break; }
                    const int i0 = juce::jlimit (0, nf - 1, (int) v.pos);
                    const int i1 = juce::jmin (nf - 1, i0 + 1);
                    const float fr = (float) (v.pos - (double) (int) v.pos);
                    float a = c0[i0] + (c0[i1] - c0[i0]) * fr;
                    float b = c1[i0] + (c1[i1] - c1[i0]) * fr;
                    // transient shaper: boost the first ~12ms, scale the body
                    float shape = v.age < transientLen ? 1.0f + attack * (1.0f - (float) v.age / (float) transientLen)
                                                       : 1.0f + punch * 0.8f;
                    shape = juce::jmax (0.0f, shape);
                    float g = v.env * v.vel * gain * shape;
                    a *= g; b *= g;
                    if (drive > 0.001f) { const float k = 1.0f + drive * 10.0f; a = std::tanh (a * k) / std::tanh (k) ; b = std::tanh (b * k) / std::tanh (k); }
                    if (crush > 0.001f)
                    {
                        if (++v.holdCount >= holdLen) { v.holdCount = 0; v.holdL = std::round (a * levels) / levels; v.holdR = std::round (b * levels) / levels; }
                        a = v.holdL; b = v.holdR;
                    }
                    if (filterOn) { a = v.fL.process (a, 0); b = v.fR.process (b, 0); }
                    L[i] += a * gl; R[i] += b * gr;
                    peak = juce::jmax (peak, std::abs (a));
                    v.pos += v.reverse ? -v.inc : v.inc;
                    // body decay starts after the transient
                    if (v.age > transientLen / 2) v.env *= v.envMul;
                    ++v.age;
                }
            }
            if (peak > p.meter.load (std::memory_order_relaxed)) p.meter.store (peak, std::memory_order_relaxed);
        }
        pos = next;
    }
}

std::shared_ptr<SampleData> DrumEngine::synthesiseDrum (int kind, double rate)
{
    auto d = std::make_shared<SampleData>();
    d->sampleRate = rate;
    const float fsr = (float) rate;
    DSP::Rng rng ((uint32_t) (4242 + kind * 101));
    auto make = [&] (float seconds) { d->buffer.setSize (1, (int) (seconds * fsr)); d->buffer.clear(); return d->buffer.getWritePointer (0); };
    const float pi2 = DSP::twoPi;
    switch (kind % 12)
    {
        case 0: // kick
        {
            auto* w = make (0.7f); float ph = 0;
            for (int i = 0; i < d->numFrames(); ++i)
            {
                const float t = i / fsr;
                const float f = 45.0f + 180.0f * std::exp (-t * 30.0f);
                ph += f / fsr;
                w[i] = std::tanh (1.6f * std::sin (pi2 * ph) * std::exp (-t * 5.5f)) + (t < 0.004f ? rng.bi() * 0.4f * (1 - t / 0.004f) : 0.0f);
            }
            break;
        }
        case 1: // snare
        {
            auto* w = make (0.4f); float ph = 0; DSP::SVF f; f.set (5000, 0.2f, fsr);
            for (int i = 0; i < d->numFrames(); ++i)
            {
                const float t = i / fsr;
                ph += (180.0f + 60.0f * std::exp (-t * 40)) / fsr;
                w[i] = 0.55f * std::sin (pi2 * ph) * std::exp (-t * 20) + 0.7f * f.process (rng.bi(), 1) * std::exp (-t * 11);
            }
            break;
        }
        case 2: case 3: // hats
        {
            const bool open = (kind % 12) == 3;
            auto* w = make (open ? 0.8f : 0.2f); DSP::SVF f; f.set (8000, 0.3f, fsr);
            float ph[6] = {}; const float freqs[6] = { 205.3f, 304.4f, 369.6f, 522.7f, 540.0f, 800.0f };
            for (int i = 0; i < d->numFrames(); ++i)
            {
                const float t = i / fsr;
                float m = 0; for (int k = 0; k < 6; ++k) { ph[k] += freqs[k] * 4.0f / fsr; ph[k] -= std::floor (ph[k]); m += ph[k] < 0.5f ? 1.0f : -1.0f; }
                w[i] = f.process (m * 0.12f + rng.bi() * 0.4f, 2) * std::exp (-t * (open ? 6.0f : 45.0f));
            }
            break;
        }
        case 4: // clap
        {
            auto* w = make (0.5f); DSP::SVF f; f.set (1500, 0.4f, fsr);
            for (int i = 0; i < d->numFrames(); ++i)
            {
                const float t = i / fsr;
                float e = 0;
                for (int k = 0; k < 3; ++k) { const float tk = t - k * 0.011f; if (tk >= 0 && tk < 0.011f) e = juce::jmax (e, std::exp (-tk * 180)); }
                e = juce::jmax (e, t > 0.022f ? std::exp (-(t - 0.022f) * 14) * 0.8f : 0.0f);
                w[i] = f.process (rng.bi(), 1) * e * 1.5f;
            }
            break;
        }
        case 5: case 6: // toms
        {
            auto* w = make (0.6f); float ph = 0;
            const float base = (kind % 12) == 5 ? 95.0f : 160.0f;
            for (int i = 0; i < d->numFrames(); ++i)
            {
                const float t = i / fsr;
                ph += (base + base * 0.8f * std::exp (-t * 25)) / fsr;
                w[i] = std::sin (pi2 * ph) * std::exp (-t * 7.0f) * 0.9f;
            }
            break;
        }
        case 7: // rim
        {
            auto* w = make (0.12f); DSP::SVF f; f.set (2200, 0.85f, fsr);
            for (int i = 0; i < d->numFrames(); ++i) { const float t = i / fsr; w[i] = f.process (i < 40 ? rng.bi() : 0.0f, 1) * 3.0f * std::exp (-t * 60); }
            break;
        }
        case 8: // glitch: bit-stepped blips
        {
            auto* w = make (0.15f); float ph = 0;
            for (int i = 0; i < d->numFrames(); ++i)
            {
                const float t = i / fsr;
                const float f = (i / 300) % 2 ? 1800.0f : 650.0f;
                ph += f / fsr; ph -= std::floor (ph);
                w[i] = (ph < 0.5f ? 0.6f : -0.6f) * std::exp (-t * 25) * (((i / 64) % 3) ? 1.0f : 0.2f);
            }
            break;
        }
        case 9: // zap
        {
            auto* w = make (0.25f); float ph = 0;
            for (int i = 0; i < d->numFrames(); ++i)
            {
                const float t = i / fsr;
                ph += (3000.0f * std::exp (-t * 40) + 80.0f) / fsr;
                w[i] = std::sin (pi2 * ph) * std::exp (-t * 14) * 0.8f;
            }
            break;
        }
        case 10: // noise burst
        {
            auto* w = make (0.3f); DSP::SVF f; f.set (3000, 0.6f, fsr);
            for (int i = 0; i < d->numFrames(); ++i) { const float t = i / fsr; f.set (300.0f + 9000.0f * std::exp (-t * 12), 0.6f, fsr); w[i] = f.process (rng.bi(), 1) * std::exp (-t * 9); }
            break;
        }
        default: // sub drop
        {
            auto* w = make (1.0f); float ph = 0;
            for (int i = 0; i < d->numFrames(); ++i)
            {
                const float t = i / fsr;
                ph += (38.0f + 30.0f * std::exp (-t * 6)) / fsr;
                w[i] = std::sin (pi2 * ph) * std::exp (-t * 2.5f) * 0.9f;
            }
            break;
        }
    }
    d->path = "synth:" + juce::String (kind);
    return d;
}
