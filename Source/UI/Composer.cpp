#include "Composer.h"
#include "../Live/LiveSequencer.h"
#include <tuple>

Composer::Composer (AppState& s) : state (s) { startTimerHz (20); }
Composer::~Composer() { stopTimer(); }

void Composer::setProject (juce::ValueTree p)
{
    project = p;
    recordingPath = {};
    replayPath = {};
    lastLiveFeatures.clear();
    auto fr = project.getChildWithName (IDs::FRACTAL);
    setProbe (fr.getProperty (IDs::probeX, -0.7436), fr.getProperty (IDs::probeY, 0.1318), (double) fr.getProperty (IDs::scaleF, 0.02) * 0.25, true);
    viewCx = fr.getProperty (IDs::cx, -0.5); viewCy = fr.getProperty (IDs::cy, 0.0); viewW = fr.getProperty (IDs::scaleF, 3.2);
    history.clear(); openNotes.clear();
    liveDriftAndPublish();
}

int Composer::beatsPerBarInt() const { return juce::jmax (1, (int) std::round (state.beatsPerBar())); }

void Composer::setProbe (double cx, double cy, double scale, bool immediate)
{
    px = cx; py = cy; pscale = juce::jmax (1e-14, scale);
    featuresDirty = true;
    if (project.isValid())
    {
        auto fr = project.getChildWithName (IDs::FRACTAL);
        fr.setProperty (IDs::probeX, cx, nullptr);
        fr.setProperty (IDs::probeY, cy, nullptr);
    }
    if (immediate) updateFeatures();
}

void Composer::updateFeatures()
{
    featuresDirty = false;
    lastFeatureTime = juce::Time::getMillisecondCounter();
    const int iters = project.isValid() ? juce::jlimit (100, 3000, (int) project.getChildWithName (IDs::FRACTAL).getProperty (IDs::iters, 400)) : 400;
    state.features = MandelMath::analyse (px, py, pscale, iters);
    auto& m = state.engine.getGlobalMods();
    m.fractalA.store ((float) state.features.escape);
    m.fractalB.store ((float) state.features.boundary);
    state.featuresChanged.sendChangeMessage();
}

juce::ValueTree Composer::findClip (int clipId) const
{
    for (auto t : Model::tracks (project))
        for (auto c : t.getChildWithName (IDs::CLIPS))
            if ((int) c[IDs::id] == clipId) return c;
    return {};
}

juce::ValueTree Composer::addGenerator (int trackId, int role)
{
    auto g = Gen::createGeneratorNode (project, trackId, role);
    g.setProperty (IDs::cx, px, nullptr);
    g.setProperty (IDs::cy, py, nullptr);
    state.beginEdit ("Add generator");
    project.getOrCreateChildWithName (IDs::GENERATORS, nullptr).appendChild (g, &state.undo);
    return g;
}

juce::ValueTree Composer::generate (juce::ValueTree gen, double atBeat, juce::ValueTree intoClip, bool evolve)
{
    FractalFeatures f = state.features;
    if ((bool) gen.getProperty (IDs::lockPoint, false))
        f = MandelMath::analyse ((double) gen[IDs::cx], (double) gen[IDs::cy], pscale, 400);
    return generate (gen, atBeat, f, intoClip, evolve);
}

juce::ValueTree Composer::generate (juce::ValueTree gen, double atBeat, const FractalFeatures& f, juce::ValueTree intoClip, bool evolve)
{
    auto track = Model::findTrack (project, (int) gen[IDs::track]);
    if (! track.isValid() || track[IDs::type].toString() == "audio") return {};
    auto s = Gen::settingsFromTree (gen, beatsPerBarInt());
    if (s.role == Gen::Breaks && track[IDs::type].toString() != "drums")
    {
        if (state.setStatus) state.setStatus ("The Breaks role needs a drum track - pick another role or target");
        return {};
    }
    // every press gives a new variation (the old behaviour returned the same thing at the same place)
    const int variation = (int) gen.getProperty (IDs::variation, 0) + 1;
    gen.setProperty (IDs::variation, variation, nullptr);
    s.seed = s.seed * 1000003 + variation * 7919;
    const int generation = (int) gen.getProperty (IDs::generation, 0) + (evolve ? 1 : 0);
    Gen::Motif prev = evolve ? Gen::Motif::parse (gen[IDs::motif].toString()) : Gen::Motif();
    float evolveAmount = 0.0f;
    if (evolve)
    {
        auto it = lastLiveFeatures.find ((int) gen[IDs::id]);
        if (it != lastLiveFeatures.end())
        {
            const auto& a = it->second;
            evolveAmount = (float) juce::jlimit (0.0, 1.0, std::abs (a.escape - f.escape) + std::abs (a.boundary - f.boundary)
                                                 + std::abs (a.insideFraction - f.insideFraction) + std::abs (a.entropy - f.entropy));
        }
    }
    lastLiveFeatures[(int) gen[IDs::id]] = f;
    auto result = Gen::generate (s, f, prev, generation, evolveAmount);

    const double len = s.bars * state.beatsPerBar();
    const bool pattern = (s.role == Gen::Breaks);
    auto clip = intoClip;
    if (! clip.isValid() || (clip[IDs::type].toString() == "pattern") != pattern)
    {
        // never stack a new clip on top of an existing one: move to the first free spot
        atBeat = Model::freeSpot (track, atBeat, len, state.beatsPerBar());
        clip = pattern ? Model::addPatternClip (track, atBeat, len, &state.undo)
                       : Model::addMidiClip (track, atBeat, len, &state.undo);
        clip.setProperty (IDs::name, gen[IDs::name].toString(), &state.undo);
    }
    Gen::applyToClip (clip, result, &state.undo);
    if (! pattern && track[IDs::type].toString() == "drums")
    {
        // fold melodic material onto the 12 pads
        auto notes = Model::readNotes (clip);
        for (auto& n : notes) n.pitch = PadP::baseNote + ((n.pitch % 12) + 12) % 12;
        Model::setNotes (clip, notes, &state.undo);
    }
    gen.setProperty (IDs::motif, result.motif.serialise(), &state.undo);
    gen.setProperty (IDs::generation, generation, &state.undo);
    gen.setProperty (IDs::cx, f.cx, &state.undo);
    gen.setProperty (IDs::cy, f.cy, &state.undo);
    gen.setProperty ("clip", (int) clip[IDs::id], &state.undo);
    if (state.setStatus) state.setStatus (result.summary);
    return clip;
}

void Composer::generateAll (double atBeat)
{
    state.beginEdit ("Generate all");
    for (auto g : project.getChildWithName (IDs::GENERATORS))
        generate (g, atBeat);
}

// ---------------------------------------------------------------------------- paths
void Composer::startRecording()
{
    if (recordingPath.isValid()) return;
    auto paths = project.getOrCreateChildWithName (IDs::PATHS, nullptr);
    juce::ValueTree p (IDs::PATH);
    p.setProperty (IDs::name, "Path " + juce::String (paths.getNumChildren() + 1), nullptr);
    p.setProperty (IDs::id, Model::allocateId (project), nullptr);
    state.beginEdit ("Record exploration");
    paths.appendChild (p, &state.undo);
    recordingPath = p;
    recordStartBeat = state.engine.isPlaying() ? state.engine.getPosition() : 0.0;
    recordStartMs = juce::Time::getMillisecondCounter();
}

void Composer::stopRecording()
{
    if (recordingPath.isValid() && recordingPath.getNumChildren() < 2)
        recordingPath.getParent().removeChild (recordingPath, nullptr);
    recordingPath = {};
}

bool Composer::interpolatePath (const juce::ValueTree& path, double beat, double& cx, double& cy, double& scale) const
{
    const int n = path.getNumChildren();
    if (n == 0) return false;
    auto key = [&path] (int i) { return path.getChild (i); };
    if (beat <= (double) key (0)[IDs::beat] || n == 1)
    { cx = key (0)[IDs::cx]; cy = key (0)[IDs::cy]; scale = key (0)[IDs::scaleF]; return true; }
    for (int i = 1; i < n; ++i)
    {
        const double b1 = key (i)[IDs::beat];
        if (beat <= b1)
        {
            const double b0 = key (i - 1)[IDs::beat];
            const double t = (beat - b0) / juce::jmax (1e-9, b1 - b0);
            cx = (double) key (i - 1)[IDs::cx] + ((double) key (i)[IDs::cx] - (double) key (i - 1)[IDs::cx]) * t;
            cy = (double) key (i - 1)[IDs::cy] + ((double) key (i)[IDs::cy] - (double) key (i - 1)[IDs::cy]) * t;
            const double s0 = key (i - 1)[IDs::scaleF], s1 = key (i)[IDs::scaleF];
            scale = std::exp (std::log (s0) + (std::log (s1) - std::log (s0)) * t);   // zoom interpolates geometrically
            return true;
        }
    }
    cx = key (n - 1)[IDs::cx]; cy = key (n - 1)[IDs::cy]; scale = key (n - 1)[IDs::scaleF];
    return true;
}

int Composer::renderPathToArrangement (juce::ValueTree path)
{
    if (! path.isValid() || path.getNumChildren() < 1) return 0;
    const double endBeat = path.getChild (path.getNumChildren() - 1)[IDs::beat];
    const int iters = juce::jlimit (100, 2000, (int) project.getChildWithName (IDs::FRACTAL).getProperty (IDs::iters, 400));
    state.beginEdit ("Render path to arrangement");
    int count = 0;
    for (auto g : project.getChildWithName (IDs::GENERATORS))
    {
        const double phrase = juce::jlimit (1, 16, (int) g.getProperty (IDs::bars, 4)) * state.beatsPerBar();
        lastLiveFeatures.erase ((int) g[IDs::id]);
        g.setProperty (IDs::motif, "", &state.undo);
        for (double b = 0.0; b <= endBeat + 1e-6; b += phrase)
        {
            double cx, cy, sc;
            interpolatePath (path, b, cx, cy, sc);
            const auto f = MandelMath::analyse (cx, cy, sc * 0.25, iters);
            if (generate (g, b, f, {}, b > 0.0).isValid()) ++count;
        }
    }
    return count;
}

// ---------------------------------------------------------------------------- timer
void Composer::timerCallback()
{
    if (! project.isValid()) return;
    const auto now = juce::Time::getMillisecondCounter();
    if (featuresDirty && now - lastFeatureTime > 60) updateFeatures();

    const bool playing = state.engine.isPlaying();
    const double beat = state.engine.getPosition();

    // record exploration keys at ~10 Hz
    if (recordingPath.isValid() && (now % 100) < 50)
    {
        const double b = playing ? beat - recordStartBeat
                                 : (now - recordStartMs) / 1000.0 * state.tempo() / 60.0;
        auto last = recordingPath.getNumChildren() > 0 ? recordingPath.getChild (recordingPath.getNumChildren() - 1) : juce::ValueTree();
        const bool moved = ! last.isValid() || (double) last[IDs::cx] != px || (double) last[IDs::cy] != py || (double) last[IDs::scaleF] != pscale * 4.0;
        if ((moved || b - (double) last[IDs::beat] > 1.0) && (! last.isValid() || b > (double) last[IDs::beat]))
        {
            juce::ValueTree k (IDs::KEY);
            k.setProperty (IDs::beat, juce::jmax (0.0, b), nullptr);
            k.setProperty (IDs::cx, px, nullptr);
            k.setProperty (IDs::cy, py, nullptr);
            k.setProperty (IDs::scaleF, pscale * 4.0, nullptr);
            recordingPath.appendChild (k, nullptr);
        }
    }

    // replay path as composition automation
    if (replayPath.isValid() && playing && ! recordingPath.isValid())
    {
        double cx, cy, sc;
        if (interpolatePath (replayPath, beat, cx, cy, sc))
        {
            if (onPathMove) onPathMove (cx, cy, sc);
            setProbe (cx, cy, sc * 0.25);
        }
    }

    if (playing && liveEnabled) liveTick();
    lastBeat = beat;
    liveDriftAndPublish();
    drainLiveMonitor();
}

// ---------------------------------------------------------------------------- live mode
int Composer::liveTrackFor (int part) const
{
    for (auto t : Model::tracks (project))
        if ((int) t.getProperty (IDs::livePart, 0) == part) return t[IDs::id];
    return -1;
}

void Composer::liveDriftAndPublish()
{
    if (! project.isValid()) return;
    auto live = Model::liveNode (project);
    const float drift = (float) live.getProperty (IDs::drift, 0.35f);
    const bool on = (bool) live.getProperty (IDs::liveOn, false);
    if (on && state.engine.isPlaying() && drift > 0.001f)
    {
        // wander along the boundary of the set: that is where the orbits are musically interesting.
        auto quality = [] (double x, double y)
        {
            if (MandelMath::inMainBulbs (x, y)) return 2.0;          // deep inside the big bulbs: dull, avoid
            const auto r = MandelMath::iterate (x, y, 300);
            return r.escaped ? std::log1p (r.smooth) / std::log1p (300.0) : 1.0;
        };
        // pscale = a quarter of the visible width: at drift 0.35 the probe crosses the view in ~20 s
        const double step = pscale * (0.002 + 0.03 * drift * drift + 0.012 * drift);
        driftAngle += (driftRng.nextDouble() - 0.5) * 0.5;
        auto at = [&] (double a) { return std::make_pair (px + std::cos (a) * step, py + std::sin (a) * step); };
        auto [nx, ny] = at (driftAngle);
        double q = quality (nx, ny);
        const bool stuckInside = insideTicks > 160;          // ~8 s inside a small bulb: head for the edge
        const bool outsideView = std::abs (nx - viewCx) > viewW * 0.45 || std::abs (ny - viewCy) > viewW * 0.3;
        if (q < 0.35 || q > 1.5 || (stuckInside && q >= 1.0) || outsideView)
        {
            double bestA = driftAngle, bestScore = -1e9;
            for (int k = 0; k < 12; ++k)
            {
                const double a = driftAngle + k * juce::MathConstants<double>::twoPi / 12.0;
                auto [x, y] = at (a);
                const double qq = quality (x, y);
                // best: just outside the edge (long orbits); small bulbs are ok for a while; big bulbs/far out are not
                double score = qq > 1.5 ? -1.5 : (qq >= 1.0 ? (stuckInside ? -1.0 : 0.3) : 1.0 - std::abs (qq - 0.8) * 2.0);
                score -= 2.0 * (std::abs (x - viewCx) / viewW + std::abs (y - viewCy) / viewW);
                if (score > bestScore) { bestScore = score; bestA = a; }
            }
            driftAngle = bestA;
            std::tie (nx, ny) = at (driftAngle);
            q = quality (nx, ny);
        }
        insideTicks = q >= 1.0 ? insideTicks + 1 : 0;
        if (q > 1.5)
        {
            // still in a main bulb (e.g. the user clicked there): step straight out towards the edge
            const double r = std::sqrt (nx * nx + ny * ny);
            if (r > 1e-9) { nx += nx / r * step * 3.0; ny += ny / r * step * 3.0; }
        }
        px = nx; py = ny;
        featuresDirty = true;
        auto fr = project.getChildWithName (IDs::FRACTAL);
        fr.setProperty (IDs::probeX, px, nullptr);
        fr.setProperty (IDs::probeY, py, nullptr);
    }
    // the Break track's slices (from the engine, which holds the loaded break)
    Live::BreakInfo bi;
    const Live::BreakInfo* bip = nullptr;
    if (auto* tp = state.engine.getTrackProcessor (liveTrackFor (Live::Break)); tp != nullptr && tp->breaks)
        if (auto d = tp->breaks->getSample())
        {
            bi.slicePoints = tp->breaks->getSlices();
            const double secs = d->numFrames() / juce::jmax (1.0, d->sampleRate);
            bi.lengthBeats = std::round (secs * (double) tp->breaks->breakBpm.load() / 60.0 * 4.0) / 4.0;   // to the 16th
            bip = &bi;
        }
    auto frame = std::make_shared<Live::Frame>();
    Live::buildFrame (*frame, px, py, pscale, live, bip);
    state.engine.getLive().setFrame (frame);
}

void Composer::drainLiveMonitor()
{
    auto& seq = state.engine.getLive();
    int s1, n1, s2, n2;
    seq.monitorFifo.prepareToRead (seq.monitorFifo.getNumReady(), s1, n1, s2, n2);
    auto handle = [this] (const Live::MonitorEvent& e)
    {
        const int key = e.part * 128 + e.note;
        auto it = openNotes.find (key);
        if (it != openNotes.end())
        {
            it->second.end = e.beat;
            history.push_back (it->second);
            openNotes.erase (it);
        }
        if (e.velocity > 0) openNotes[key] = { e.part, e.note, e.velocity, e.beat, -1.0 };
    };
    for (int i = 0; i < n1; ++i) handle (seq.monitorData[(size_t) (s1 + i)]);
    for (int i = 0; i < n2; ++i) handle (seq.monitorData[(size_t) (s2 + i)]);
    seq.monitorFifo.finishedRead (n1 + n2);
    const double keep = state.engine.getLiveBeat() - 64.0 * state.beatsPerBar();
    while (! history.empty() && history.front().start < keep) history.pop_front();
    if (history.size() > 20000) history.erase (history.begin(), history.begin() + 5000);
}

int Composer::captureLive (int bars)
{
    drainLiveMonitor();
    const double bpb = state.beatsPerBar();
    const double now = state.engine.getLiveBeat();
    const double end = std::floor (now / bpb + 1e-9) * bpb;
    const double start = end - bars * bpb;
    if (end <= 0.0) return 0;
    const double dest = std::ceil (Model::projectEndBeat (project) / bpb - 1e-9) * bpb;
    state.beginEdit ("Capture live");
    int clips = 0;
    for (int p = 1; p < Live::NumParts; ++p)
    {
        auto track = Model::findTrack (project, liveTrackFor (p));
        if (! track.isValid()) continue;
        std::vector<NoteData> notes;
        auto add = [&] (const LiveNote& n)
        {
            if (n.part != p || n.start < start - 1e-6 || n.start >= end) return;
            const double e = n.end < 0 ? end : juce::jmin (end, n.end);
            NoteData d; d.pitch = n.pitch; d.velocity = n.velocity;
            d.start = n.start - start; d.length = juce::jmax (1.0 / 64.0, e - n.start);
            notes.push_back (d);
        };
        for (auto& n : history) add (n);
        for (auto& [k, n] : openNotes) add (n);
        if (notes.empty()) continue;
        auto clip = Model::addMidiClip (track, dest, bars * bpb, &state.undo);
        clip.setProperty (IDs::name, "Live " + Live::partNames()[p], &state.undo);
        Model::setNotes (clip, notes, &state.undo);
        ++clips;
    }
    return clips;
}

void Composer::liveTick()
{
    const double beat = state.engine.getPosition();
    const double bpb = state.beatsPerBar();
    const bool loopOn = (bool) project.getProperty (IDs::loopOn, false);
    const double ls = project.getProperty (IDs::loopStart, 0.0), le = project.getProperty (IDs::loopEnd, 16.0);

    for (auto g : project.getChildWithName (IDs::GENERATORS))
    {
        if (! (bool) g.getProperty (IDs::live, false)) continue;
        const double phrase = juce::jlimit (1, 16, (int) g.getProperty (IDs::bars, 4)) * bpb;
        auto clip = findClip ((int) g.getProperty ("clip", -1));
        if (loopOn && le > ls)
        {
            // evolve the clip inside the loop: regenerate when entering the last bar
            const double trigger = le - bpb;
            if (lastBeat < trigger && beat >= trigger)
            {
                if (! clip.isValid() || (double) clip[IDs::start] < ls || (double) clip[IDs::start] >= le) clip = {};
                state.beginEdit ("Live evolve");
                generate (g, clip.isValid() ? (double) clip[IDs::start] : ls, clip, true);
            }
        }
        else
        {
            // write the composition forward along the timeline, one phrase ahead
            const double nextStart = (std::floor (beat / phrase) + 1.0) * phrase;
            if (lastBeat < nextStart - bpb && beat >= nextStart - bpb)
            {
                bool exists = false;
                auto track = Model::findTrack (project, (int) g[IDs::track]);
                for (auto c : track.getChildWithName (IDs::CLIPS))
                    if (std::abs ((double) c[IDs::start] - nextStart) < 1e-6) exists = true;
                if (! exists) { state.beginEdit ("Live compose"); generate (g, nextStart, {}, true); }
            }
        }
    }
}
