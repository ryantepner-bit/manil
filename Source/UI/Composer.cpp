#include "Composer.h"

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
