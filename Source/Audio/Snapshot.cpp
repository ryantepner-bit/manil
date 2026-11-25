#include "Snapshot.h"
#include <algorithm>

namespace Snap
{
    float AutoLane::valueAt (double beat) const
    {
        if (points.empty()) return 0.0f;
        if (beat <= points.front().first) return points.front().second;
        if (beat >= points.back().first) return points.back().second;
        auto it = std::upper_bound (points.begin(), points.end(), beat, [] (double b, const auto& p) { return b < p.first; });
        const auto& b = *it; const auto& a = *(it - 1);
        const double t = (beat - a.first) / juce::jmax (1e-9, b.first - a.first);
        return (float) (a.second + (b.second - a.second) * t);
    }

    void appendMidiClipEvents (const juce::ValueTree& clip, std::vector<Event>& out)
    {
        const double cs = clip[IDs::start], cl = clip[IDs::length];
        const auto clipId = (juce::uint32) (int) clip[IDs::id];
        juce::uint32 idx = 0;
        for (auto n : clip)
        {
            if (! n.hasType (IDs::NOTE)) continue;
            const auto d = Model::readNote (n);
            ++idx;
            if (d.start >= cl || d.start < 0) continue;
            const double on = cs + d.start, off = cs + juce::jmin (cl, d.start + d.length);
            const juce::uint32 key = clipId * 65537u + idx;
            out.push_back ({ on, d.pitch, d.velocity / 127.0f, d.probability, key });
            out.push_back ({ off, d.pitch, 0.0f, 1.0f, key });
        }
    }

    void appendPatternClipEvents (const juce::ValueTree& clip, std::vector<Event>& out)
    {
        const double cs = clip[IDs::start], cl = clip[IDs::length];
        const auto pat = clip.getChildWithName (IDs::PATTERN);
        if (! pat.isValid()) return;
        const float swing = (float) pat.getProperty (IDs::swing, 0.0f);
        const auto clipId = (juce::uint32) (int) clip[IDs::id];
        for (auto lane : pat)
        {
            const int pad = lane[IDs::pad];
            const int steps = juce::jlimit (1, 64, (int) lane.getProperty (IDs::steps, 16));
            const int rate = juce::jlimit (1, 8, (int) lane.getProperty (IDs::rate, 4));
            const auto data = Model::readSteps (lane);
            const double stepLen = 1.0 / rate;
            const int total = (int) std::floor (cl * rate + 1e-6);
            for (int k = 0; k < total; ++k)
            {
                const auto& st = data[(size_t) (k % steps)];
                if (st.velocity <= 0) continue;
                double t = k * stepLen;
                if (rate % 2 == 0 && (k % 2) == 1) t += swing * stepLen * 0.66; // swing on off-steps
                const int rat = juce::jlimit (1, 4, st.ratchet);
                for (int r = 0; r < rat; ++r)
                {
                    const double on = cs + t + r * stepLen / rat;
                    if (on >= cs + cl) break;
                    const double len = juce::jmin (stepLen / rat * 0.9, cs + cl - on);
                    const juce::uint32 key = clipId * 65537u + (juce::uint32) (pad * 4096 + k * 4 + r);
                    const float vel = (st.velocity / 127.0f) * (r == 0 ? 1.0f : 0.8f);
                    out.push_back ({ on, 36 + pad, vel, st.probability / 100.0f, key });
                    out.push_back ({ on + len, 36 + pad, 0.0f, 1.0f, key });
                }
            }
        }
    }

    void sortEvents (std::vector<Event>& ev)
    {
        std::stable_sort (ev.begin(), ev.end(), [] (const Event& a, const Event& b)
        {
            if (a.beat != b.beat) return a.beat < b.beat;
            return (a.velocity == 0.0f) && (b.velocity != 0.0f); // offs first
        });
    }

    std::vector<Event> eventsForTrack (const juce::ValueTree& track)
    {
        std::vector<Event> ev;
        for (auto clip : track.getChildWithName (IDs::CLIPS))
        {
            const auto type = clip[IDs::type].toString();
            if (type == "midi") appendMidiClipEvents (clip, ev);
            else if (type == "pattern") appendPatternClipEvents (clip, ev);
        }
        sortEvents (ev);
        return ev;
    }
}
