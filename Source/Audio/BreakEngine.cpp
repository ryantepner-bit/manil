#include "BreakEngine.h"
#include "DrumMachine.h"
#include <juce_dsp/juce_dsp.h>

BreakEngine::BreakEngine()
{
    setSlices (equalSlices (8));
}

void BreakEngine::prepare (double sampleRate, int) { sr = sampleRate; allNotesOff(); }

void BreakEngine::allNotesOff() { for (auto& v : voices) v.active = false; playingSlice.store (-1); }

void BreakEngine::setSample (std::shared_ptr<const SampleData> s)
{
    auto old = owner;
    owner = s;
    sample.store (s.get(), std::memory_order_release);
    DeferredDeleter::get().retire (std::const_pointer_cast<SampleData> (old));
}

void BreakEngine::setSlices (std::vector<float> p)
{
    if (p.size() < 2) p = { 0.0f, 1.0f };
    std::sort (p.begin(), p.end());
    p.front() = 0.0f; p.back() = 1.0f;
    if ((int) p.size() > maxSlices + 1) p.resize ((size_t) maxSlices + 1), p.back() = 1.0f;
    auto s = std::make_shared<Slices>();
    s->points = std::move (p);
    auto old = slicesOwner;
    slicesOwner = s;
    slices.store (s.get(), std::memory_order_release);
    DeferredDeleter::get().retire (old);
}

void BreakEngine::trigger (int slice, float vel)
{
    const auto* d = sample.load (std::memory_order_acquire);
    const auto* sl = slices.load (std::memory_order_acquire);
    if (d == nullptr || sl == nullptr || d->numFrames() < 2) return;
    const int n = (int) sl->points.size() - 1;
    if (n <= 0) return;
    slice = ((slice % n) + n) % n;     // more notes than slices: wrap around
    // the new slice cuts the old ones (MPC style): fade them out fast
    const float fadeSamples = juce::jmax (16.0f, fadeMs.load() * 0.001f * (float) sr);
    for (auto& v : voices)
        if (v.active && ! v.releasing) { v.releasing = true; v.fadeStep = 1.0f / fadeSamples; }
    auto& v = voices[(size_t) nextVoice];
    nextVoice = (nextVoice + 1) % (int) voices.size();
    v.active = true;
    v.releasing = false;
    v.fade = 1.0f;
    v.fadeStep = 1.0f / fadeSamples;
    v.vel = vel;
    v.slice = slice;
    v.pos = sl->points[(size_t) slice] * (d->numFrames() - 1);
    v.end = sl->points[(size_t) slice + 1] * (d->numFrames() - 1);
    playingSlice.store (slice);
}

void BreakEngine::process (float* L, float* R, int numSamples, const NoteEvent* events, int numEvents, double tempo)
{
    const auto* d = sample.load (std::memory_order_acquire);
    const float gain = juce::Decibels::decibelsToGain (gainDb.load());
    double speed = std::exp2 (tuneSemis.load() / 12.0);
    if (sync.load()) speed *= juce::jlimit (0.25, 4.0, tempo / juce::jmax (40.0, (double) breakBpm.load()));
    const double inc = d != nullptr ? speed * d->sampleRate / sr : 1.0;
    const float fadeSamples = juce::jmax (16.0f, fadeMs.load() * 0.001f * (float) sr);
    int ev = 0;
    for (int i = 0; i < numSamples; ++i)
    {
        while (ev < numEvents && events[ev].offset <= i)
        {
            const auto& e = events[ev++];
            if (e.velocity > 0.0f && e.note >= baseNote) trigger (e.note - baseNote, e.velocity);
            // note-offs are ignored: a slice plays to its end (one-shot), like a chopped break
        }
        if (d == nullptr) continue;
        const bool stereo = d->buffer.getNumChannels() > 1;
        const float* c0 = d->buffer.getReadPointer (0);
        const float* c1 = d->buffer.getReadPointer (stereo ? 1 : 0);
        const int nf = d->numFrames();
        for (auto& v : voices)
        {
            if (! v.active) continue;
            const int ip = (int) v.pos;
            if (ip + 1 >= nf || v.pos >= v.end) { v.active = false; continue; }
            const float fr = (float) (v.pos - ip);
            // fade out at the end of the slice (no click) and when cut by a new slice
            const float toEnd = (float) ((v.end - v.pos) / inc);
            float g = v.vel * gain * juce::jlimit (0.0f, 1.0f, toEnd / fadeSamples);
            if (v.releasing) { v.fade -= v.fadeStep; if (v.fade <= 0.0f) { v.active = false; continue; } g *= v.fade; }
            L[i] += (c0[ip] + (c0[ip + 1] - c0[ip]) * fr) * g;
            R[i] += (c1[ip] + (c1[ip + 1] - c1[ip]) * fr) * g;
            v.pos += inc;
        }
    }
    bool any = false;
    for (auto& v : voices) any |= v.active && ! v.releasing;
    if (! any) playingSlice.store (-1);
}

// ---------------------------------------------------------------------------- analysis
std::vector<float> BreakEngine::equalSlices (int count)
{
    count = juce::jlimit (1, maxSlices, count);
    std::vector<float> p;
    for (int i = 0; i <= count; ++i) p.push_back ((float) i / (float) count);
    return p;
}

std::vector<float> BreakEngine::detectSlices (const SampleData& d, float sensitivity, int maxCount)
{
    const int n = d.numFrames();
    if (n < 2048) return equalSlices (1);
    constexpr int order = 9, win = 1 << order;
    juce::dsp::FFT fft (order);
    std::vector<float> mono ((size_t) n);
    float peak = 1e-9f;
    for (int i = 0; i < n; ++i) { mono[(size_t) i] = 0.5f * (d.get (0, i) + d.get (1, i)); peak = juce::jmax (peak, std::abs (mono[(size_t) i])); }
    for (auto& x : mono) x /= peak;

    // spectral flux per window (only increases count, so decays do not trigger)
    std::vector<float> flux, prev ((size_t) win / 2, 0.0f), buf ((size_t) win * 2);
    std::vector<int> pos;
    const int hop = win / 2;
    for (int i = 0; i + win < n; i += hop)
    {
        std::fill (buf.begin(), buf.end(), 0.0f);
        for (int k = 0; k < win; ++k)
            buf[(size_t) k] = mono[(size_t) (i + k)] * (0.5f - 0.5f * std::cos (DSP::twoPi * (float) k / (float) win));
        fft.performFrequencyOnlyForwardTransform (buf.data());
        float f = 0.0f;
        for (int k = 1; k < win / 2; ++k) { const float diff = buf[(size_t) k] - prev[(size_t) k]; if (diff > 0) f += diff; prev[(size_t) k] = buf[(size_t) k]; }
        flux.push_back (f);
        pos.push_back (i);
    }
    if (flux.empty()) return equalSlices (1);
    // adaptive threshold: local median + sensitivity-scaled margin
    const float s = juce::jlimit (0.0f, 1.0f, sensitivity);
    const float margin = 2.2f - 1.8f * s;
    const int minGap = (int) (d.sampleRate * (0.11 - 0.07 * s));     // no slices closer than ~40-110 ms
    std::vector<float> p { 0.0f };
    int last = 0;
    for (size_t i = 1; i + 1 < flux.size(); ++i)
    {
        std::vector<float> w;
        for (int k = -8; k <= 8; ++k) { const int j = (int) i + k; if (j >= 0 && j < (int) flux.size()) w.push_back (flux[(size_t) j]); }
        std::nth_element (w.begin(), w.begin() + (long) w.size() / 2, w.end());
        const float med = w[w.size() / 2];
        const bool localMax = flux[i] >= flux[i - 1] && flux[i] >= flux[i + 1];
        if (localMax && flux[i] > med * margin + 0.02f && pos[i] - last > minGap)
        {
            // move the cut back to the quiet point just before the attack (zero crossing nearby)
            int at = pos[i];
            for (int k = 0; k < hop && at > 1; ++k, --at) if ((mono[(size_t) at] >= 0) != (mono[(size_t) at - 1] >= 0)) break;
            p.push_back ((float) at / (float) n);
            last = pos[i];
            if ((int) p.size() > maxCount) break;
        }
    }
    p.push_back (1.0f);
    return p;
}

float BreakEngine::guessBpm (const SampleData& d)
{
    const double secs = d.numFrames() / juce::jmax (1.0, d.sampleRate);
    if (secs <= 0.1) return 120.0f;
    double bpm = 240.0 / secs;          // one bar of 4/4
    while (bpm < 80.0) bpm *= 2.0;
    while (bpm > 180.0) bpm *= 0.5;
    return (float) bpm;
}

std::shared_ptr<SampleData> BreakEngine::makeDefaultBreak (double rate, const std::array<std::shared_ptr<const SampleData>, 3>& kit)
{
    const double bpm = 140.0, stepSec = 60.0 / bpm / 4.0;
    const int steps = 32;
    auto out = std::make_shared<SampleData>();
    out->sampleRate = rate;
    out->path = "built-in break";
    out->buffer.setSize (2, (int) (steps * stepSec * rate) + 1);
    out->buffer.clear();
    std::array<std::shared_ptr<const SampleData>, 3> src;
    for (int k = 0; k < 3; ++k) src[(size_t) k] = kit[(size_t) k] != nullptr ? kit[(size_t) k] : std::shared_ptr<const SampleData> (DrumEngine::synthesiseDrum (k, rate));
    // a two-bar funk/breakbeat pattern: 0 = kick, 1 = snare, 2 = hat, value = velocity
    struct Hit { int step, drum; float vel; };
    const Hit hits[] = {
        { 0, 0, 1.0f }, { 4, 1, 0.95f }, { 7, 1, 0.3f }, { 9, 1, 0.25f }, { 10, 0, 0.85f }, { 12, 1, 0.95f }, { 15, 1, 0.3f },
        { 16, 0, 1.0f }, { 18, 0, 0.7f }, { 20, 1, 0.95f }, { 23, 1, 0.3f }, { 25, 0, 0.6f }, { 26, 0, 0.8f }, { 28, 1, 0.95f }, { 30, 1, 0.35f }, { 31, 1, 0.5f } };
    auto add = [&] (const SampleData& s, double atSec, float vel, float panL, float panR, double maxSec = 10.0)
    {
        const int start = juce::jmax (0, (int) (atSec * rate));
        if (start >= out->buffer.getNumSamples()) return;
        const double ratio = s.sampleRate / rate;
        const int len = juce::jmin ((int) (s.numFrames() / ratio), out->buffer.getNumSamples() - start, (int) (maxSec * rate));
        const int fadeLen = juce::jmax (1, (int) (0.02 * rate));
        for (int i = 0; i < len; ++i)
        {
            const double sp = i * ratio;
            const int ip = (int) sp;
            if (ip + 1 >= s.numFrames()) break;
            const float fr = (float) (sp - ip);
            const float l = s.get (0, ip) + (s.get (0, ip + 1) - s.get (0, ip)) * fr;
            const float r = s.get (1, ip) + (s.get (1, ip + 1) - s.get (1, ip)) * fr;
            const float fo = juce::jmin (1.0f, (float) (len - i) / (float) fadeLen);
            out->buffer.addSample (0, start + i, l * vel * panL * fo);
            out->buffer.addSample (1, start + i, r * vel * panR * fo);
        }
    };
    DSP::Rng rng (777);
    for (auto& h : hits) add (*src[(size_t) h.drum], h.step * stepSec + rng.bi() * 0.004, h.vel, 1.0f, 0.92f, h.drum == 0 ? 0.35 : 0.3);
    for (int s = 0; s < steps; s += 2) add (*src[2], s * stepSec + rng.bi() * 0.003, (s % 4 == 0 ? 0.32f : 0.2f) + rng.uni() * 0.06f, 0.85f, 1.0f, 0.09);
    // a little room + glue: short stereo feedback delay and soft clip
    const int dl = (int) (0.021 * rate), dr = (int) (0.029 * rate);
    for (int i = juce::jmax (dl, dr); i < out->buffer.getNumSamples(); ++i)
    {
        out->buffer.addSample (0, i, 0.1f * out->buffer.getSample (1, i - dr));
        out->buffer.addSample (1, i, 0.1f * out->buffer.getSample (0, i - dl));
    }
    float pk = 1e-6f;
    for (int c = 0; c < 2; ++c) for (int i = 0; i < out->buffer.getNumSamples(); ++i) pk = juce::jmax (pk, std::abs (out->buffer.getSample (c, i)));
    for (int c = 0; c < 2; ++c) for (int i = 0; i < out->buffer.getNumSamples(); ++i) out->buffer.setSample (c, i, out->buffer.getSample (c, i) / pk * 0.9f);
    return out;
}

juce::String BreakEngine::encodeSlices (const std::vector<float>& p)
{
    juce::StringArray a;
    for (auto v : p) a.add (juce::String (v, 5));
    return a.joinIntoString (",");
}

std::vector<float> BreakEngine::decodeSlices (const juce::String& s)
{
    std::vector<float> p;
    for (auto& t : juce::StringArray::fromTokens (s, ",", "")) if (t.isNotEmpty()) p.push_back (juce::jlimit (0.0f, 1.0f, t.getFloatValue()));
    return p;
}
