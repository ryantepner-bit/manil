// MANDELBROT core test-suite (console). Exit code 0 = all passed.
#include <juce_events/juce_events.h>
#include "Core/ProjectModel.h"
#include "Core/Scales.h"
#include "Fractal/MandelMath.h"
#include "Generative/Generator.h"
#include "Audio/Engine.h"
#include "Audio/Exporter.h"
#include "UI/Composer.h"
#include "Live/LiveSequencer.h"
#include "UI/ArrangementView.h"
#include "UI/LivePage.h"
#include <set>

static int failures = 0, checks = 0;
#define CHECK(cond) do { ++checks; if (! (cond)) { ++failures; std::cerr << "FAIL " << __FILE__ << ":" << __LINE__ << "  " #cond "\n"; } } while (0)

static void testScales()
{
    auto d = Scales::degrees (Scales::Dorian, 0);
    CHECK (d.size() == 7);
    CHECK (Scales::degreeToPitch (0, 9, 4, d) == 69);          // A4
    CHECK (Scales::degreeToPitch (7, 9, 4, d) == 81);          // octave up
    CHECK (Scales::degreeToPitch (-1, 9, 4, d) == 67);         // G below
    CHECK (Scales::contains (66, 9, Scales::Dorian, 0));       // F# in A dorian
    CHECK (! Scales::contains (65, 9, Scales::Dorian, 0));
    CHECK (Scales::snap (65, 9, Scales::Dorian, 0) == 64 || Scales::snap (65, 9, Scales::Dorian, 0) == 66);
    CHECK (Scales::degrees (Scales::WholeTone, 0).size() == 6);
    CHECK (Scales::degrees (Scales::Custom, 0b000010010001).size() == 3);
    CHECK (Scales::noteName (60) == "C4");
}

static void testFractal()
{
    auto a = MandelMath::iterate (0.0, 0.0, 500);
    CHECK (! a.escaped);
    auto b = MandelMath::iterate (1.0, 1.0, 500);
    CHECK (b.escaped && b.iterations < 12);
    CHECK (MandelMath::detectPeriod (0.0, 0.0, 2000) == 1);
    CHECK (MandelMath::detectPeriod (-1.0, 0.0, 2000) == 2);
    CHECK (MandelMath::detectPeriod (-0.1225, 0.7449, 4000) == 3);
    auto f = MandelMath::analyse (-0.743643887, 0.131825904, 0.01, 500);
    CHECK (f.contour.size() == 64 && f.energy.size() == 64);
    CHECK (f.entropy >= 0.0 && f.entropy <= 1.0);
    CHECK (f.boundary > 0.3);
    auto f2 = MandelMath::analyse (-0.743643887, 0.131825904, 0.01, 500);
    CHECK (f.hash() == f2.hash());
    auto wt = MandelMath::buildWavetable (-0.7436, 0.1318, 8, 512);
    CHECK (wt.size() == 8 * 512);
    float peak = 0; for (auto v : wt) peak = std::max (peak, std::abs (v));
    CHECK (peak > 0.9f && peak <= 1.0001f);
}

static bool sameNotes (const std::vector<NoteData>& a, const std::vector<NoteData>& b)
{
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (a[i].pitch != b[i].pitch || std::abs (a[i].start - b[i].start) > 1e-9 || a[i].velocity != b[i].velocity) return false;
    return true;
}

static void testGenerator()
{
    Gen::Settings s; s.seed = 42; s.bars = 4;
    auto f = MandelMath::analyse (-0.7436, 0.1318, 0.02, 400);
    for (int role = 0; role < Gen::NumRoles; ++role)
    {
        s.role = role;
        auto r1 = Gen::generate (s, f, {}, 0);
        auto r2 = Gen::generate (s, f, {}, 0);
        if (role == Gen::Breaks)
        {
            CHECK (r1.lanes.size() >= 4);
            CHECK (Model::encodeSteps (r1.lanes[0].data) == Model::encodeSteps (r2.lanes[0].data));
            int hits = 0; for (auto& st : r1.lanes[0].data) hits += st.velocity > 0;
            CHECK (hits > 0);
        }
        else
        {
            CHECK (! r1.notes.empty());
            CHECK (sameNotes (r1.notes, r2.notes));      // determinism
            for (auto& n : r1.notes) { CHECK (n.start >= 0 && n.start < 16.0 + 1e-9); CHECK (Scales::contains (n.pitch, s.root, s.scale, 0)); }
        }
    }
    s.role = Gen::Melody;
    auto base = Gen::generate (s, f, {}, 0);
    auto otherSeed = s; otherSeed.seed = 43;
    CHECK (! sameNotes (base.notes, Gen::generate (otherSeed, f, {}, 0).notes));
    // motif preservation: mutation keeps most of the motif's degrees
    s.mutation = 0.2f;
    auto evolved = Gen::generate (s, MandelMath::analyse (-0.7440, 0.1320, 0.02, 400), base.motif, 1, 0.1f);
    int same = 0;
    const size_t cmp = std::min (base.motif.notes.size(), evolved.motif.notes.size());
    for (size_t i = 0; i < cmp; ++i) same += base.motif.notes[i].degree == evolved.motif.notes[i].degree;
    CHECK (cmp > 0 && same >= (int) (cmp * 0.5));
    auto m2 = Gen::Motif::parse (base.motif.serialise());
    CHECK (m2.notes.size() == base.motif.notes.size());
}

static void testModel()
{
    auto p = Model::createDefaultProject();
    juce::UndoManager um;
    auto t = Model::findTrack (p, 2);
    CHECK (t.isValid());
    auto clip = Model::addMidiClip (t, 0, 4, &um);
    um.beginNewTransaction();
    Model::addNote (clip, { 60, 0.0, 1.0, 100, 1.0f }, &um);
    CHECK (Model::readNotes (clip).size() == 1);
    um.undo();
    CHECK (Model::readNotes (clip).empty());
    um.redo();
    CHECK (Model::readNotes (clip).size() == 1);

    auto f = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("mandel_test.mandelbrot");
    CHECK (Model::save (p, f));
    juce::String err;
    auto loaded = Model::load (f, err);
    CHECK (loaded.isValid());
    CHECK (loaded.isEquivalentTo (p));

    auto steps = Model::decodeSteps ("100:50:2,0:100:1", 4);
    CHECK (steps.size() == 4 && steps[0].velocity == 100 && steps[0].probability == 50 && steps[0].ratchet == 2);
}

static void testSnapshotEvents()
{
    auto p = Model::createDefaultProject();
    auto drums = Model::findTrack (p, 1);
    auto clip = Model::addPatternClip (drums, 0, 4, nullptr);
    auto lane = Model::laneFor (clip, 0);
    std::vector<StepData> st (16); st[0] = { 120, 100, 1 }; st[4] = { 100, 100, 3 };
    Model::writeSteps (lane, st, nullptr);
    auto lane2 = Model::laneFor (clip, 8);
    lane2.setProperty (IDs::steps, 5, nullptr);
    std::vector<StepData> st2 (5); st2[0] = { 90, 100, 1 };
    Model::writeSteps (lane2, st2, nullptr);
    auto ev = Snap::eventsForTrack (drums);
    int kickOn = 0, glitchOn = 0;
    for (auto& e : ev) { if (e.velocity > 0 && e.note == 36) ++kickOn; if (e.velocity > 0 && e.note == 44) ++glitchOn; }
    CHECK (kickOn == 4);     // 4 beats @ 16ths: step0 once + step4 ratchet x3
    CHECK (glitchOn == 4);   // 5-step polyrhythmic lane over 16 steps: 0,5,10,15
}

static void testEngineRender()
{
    juce::ScopedJuceInitialiser_GUI init;
    auto p = Model::createDefaultProject();
    auto f = MandelMath::analyse (-0.7436, 0.1318, 0.02, 400);
    // fill every instrument track with generated material
    const int roles[] = { Gen::Breaks, Gen::Bass, Gen::Melody, Gen::Chords };
    int i = 0;
    for (auto t : Model::tracks (p))
    {
        if (t[IDs::type].toString() == "audio") continue;
        Gen::Settings s; s.role = roles[i++ % 4]; s.seed = 7; s.bars = 2;
        auto clip = s.role == Gen::Breaks ? Model::addPatternClip (t, 0, 8, nullptr) : Model::addMidiClip (t, 0, 8, nullptr);
        Gen::applyToClip (clip, Gen::generate (s, f, {}, 0), nullptr);
    }
    // insert effects on the lead + automation on the pad volume
    auto lead = Model::findTrack (p, 3);
    auto slot = lead.getChildWithName (IDs::INSERTS).getChild (0);
    slot.setProperty (IDs::fx, (int) FxP::Stutter, nullptr);
    auto slot2 = lead.getChildWithName (IDs::INSERTS).getChild (1);
    slot2.setProperty (IDs::fx, (int) FxP::Bitcrush, nullptr);
    auto padTrack = Model::findTrack (p, 4);
    auto al = Model::automationLane (padTrack, "vol", true, nullptr);
    for (auto [t, v] : { std::pair { 0.0, 0.2 }, { 8.0, 0.9 } })
    { juce::ValueTree pt (IDs::POINT); pt.setProperty (IDs::t, t, nullptr); pt.setProperty (IDs::val, v, nullptr); al.appendChild (pt, nullptr); }
    // granular engine on the pad
    padTrack.getChildWithName (IDs::SYNTH).setProperty ("engine", 3, nullptr);

    Engine engine;
    engine.setProject (p);
    engine.rebuildNow();

    auto tmp = juce::File::getSpecialLocation (juce::File::tempDirectory);
    for (double sr : { 44100.0, 48000.0 })
        for (int bits : { 16, 24 })
        {
            auto out = tmp.getChildFile ("mandel_render_" + juce::String ((int) sr) + "_" + juce::String (bits) + ".wav");
            juce::String err; juce::StringArray written;
            Exporter::AudioJob job; job.folderOrFile = out; job.sampleRate = sr; job.bits = bits;
            CHECK (Exporter::exportAudio (engine, p, job, nullptr, err, written));
            juce::AudioFormatManager fm; fm.registerBasicFormats();
            std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (out));
            CHECK (r != nullptr);
            if (r != nullptr)
            {
                CHECK ((int) r->sampleRate == (int) sr && (int) r->bitsPerSample == bits && r->numChannels == 2);
                juce::AudioBuffer<float> b (2, (int) r->lengthInSamples);
                r->read (&b, 0, (int) r->lengthInSamples, 0, true, true);
                const float rms = b.getRMSLevel (0, 0, b.getNumSamples());
                bool finite = true;
                for (int c = 0; c < 2; ++c) for (int k = 0; k < b.getNumSamples(); ++k) finite &= std::isfinite (b.getSample (c, k));
                std::cout << "render " << sr << "/" << bits << ": " << r->lengthInSamples << " samples, rms " << rms << " peak " << b.getMagnitude (0, b.getNumSamples()) << "\n";
                CHECK (rms > 0.005f);
                CHECK (finite);
            }
        }
    // stems
    {
        juce::String err; juce::StringArray written;
        Exporter::AudioJob job; job.folderOrFile = tmp.getChildFile ("mandel_stems"); job.stems = true;
        CHECK (Exporter::exportAudio (engine, p, job, nullptr, err, written));
        CHECK (written.size() == Model::tracks (p).getNumChildren());
    }
    // midi
    {
        juce::String err;
        auto mf = tmp.getChildFile ("mandel_test.mid");
        CHECK (Exporter::exportMidi (p, mf, err));
        juce::MidiFile m; juce::FileInputStream is (mf);
        CHECK (m.readFrom (is));
        CHECK (m.getNumTracks() == 6);  // meta + 5 instrument tracks
        int notes = 0;
        for (int t = 0; t < m.getNumTracks(); ++t) for (auto* e : *m.getTrack (t)) notes += e->message.isNoteOn();
        CHECK (notes > 20);
        std::cout << "midi notes exported: " << notes << "\n";
    }
    // realtime-path smoke test: run the callback path directly with a fake device buffer
    {
        p.setProperty (IDs::loopOn, true, nullptr);      // timeline selection 0..16
        p.setProperty (IDs::loopStart, 0.0, nullptr);
        p.setProperty (IDs::loopEnd, 16.0, nullptr);
        engine.play();
        Engine::Transport tr; tr.playing = true;
        std::vector<float> L (512), R (512);
        float peak = 0;
        for (int k = 0; k < 1600; ++k) { engine.processBlock (L.data(), R.data(), nullptr, nullptr, 256, tr, -1, false); for (int j = 0; j < 256; ++j) peak = std::max (peak, std::abs (L[(size_t) j])); }
        CHECK (peak > 0.01f);
        CHECK (tr.loopPass >= 1);  // default loop 0..16 beats at 128 bpm wraps within ~4.6 s
        engine.stop();
    }
}

static void testComposer()
{
    juce::ScopedJuceInitialiser_GUI init;
    Engine engine;
    AppState state (engine);
    state.project = Model::createDefaultProject();
    engine.setProject (state.project);
    Composer composer (state);
    composer.setProject (state.project);
    CHECK (state.features.contour.size() == 64);

    // one generator per instrument track
    std::vector<juce::ValueTree> gens;
    const int roles[] = { Gen::Breaks, Gen::Bass, Gen::Chords, Gen::Melody, Gen::Arp };
    int i = 0;
    for (auto t : Model::tracks (state.project))
        if (t[IDs::type].toString() != "audio") gens.push_back (composer.addGenerator (t[IDs::id], roles[i++]));
    CHECK (gens.size() == 5);

    // generate, then evolve: motif is kept and mutated, generation counts up
    auto clip = composer.generate (gens[2], 0.0);
    CHECK (clip.isValid() && ! Model::readNotes (clip).empty());
    const auto motif0 = Gen::Motif::parse (gens[2][IDs::motif].toString());
    CHECK (! motif0.empty());
    composer.setProbe (-0.7440, 0.1315, 0.004, true);
    composer.generate (gens[2], 0.0, clip, true);
    CHECK ((int) gens[2][IDs::generation] == 1);
    const auto motif1 = Gen::Motif::parse (gens[2][IDs::motif].toString());
    CHECK (std::abs ((int) motif1.notes.size() - (int) motif0.notes.size()) <= 2);

    // Breaks on a synth track is rejected, on the drum track gives a pattern clip
    auto bad = composer.addGenerator (Model::findTrack (state.project, 2)[IDs::id], Gen::Breaks);
    CHECK (! composer.generate (bad, 0.0).isValid());
    auto pat = composer.generate (gens[0], 0.0);
    CHECK (pat[IDs::type].toString() == "pattern");

    // undo removes the generated clip
    const int before = Model::findTrack (state.project, 3).getChildWithName (IDs::CLIPS).getNumChildren();
    state.undo.beginNewTransaction();
    composer.generate (gens[2], 32.0);
    CHECK (Model::findTrack (state.project, 3).getChildWithName (IDs::CLIPS).getNumChildren() == before + 1);
    state.undo.undo();
    CHECK (Model::findTrack (state.project, 3).getChildWithName (IDs::CLIPS).getNumChildren() == before);

    // exploration path -> arrangement (composition automation)
    juce::ValueTree path (IDs::PATH);
    const double keys[][4] = { { 0, -0.7436, 0.1318, 0.02 }, { 16, -0.10, 0.75, 0.05 }, { 32, -1.25, 0.02, 0.1 } };
    for (auto& k : keys)
    {
        juce::ValueTree key (IDs::KEY);
        key.setProperty (IDs::beat, k[0], nullptr); key.setProperty (IDs::cx, k[1], nullptr);
        key.setProperty (IDs::cy, k[2], nullptr); key.setProperty (IDs::scaleF, k[3], nullptr);
        path.appendChild (key, nullptr);
    }
    double cx, cy, sc;
    CHECK (composer.interpolatePath (path, 8.0, cx, cy, sc));
    CHECK (std::abs (cx - (-0.7436 + (-0.10 + 0.7436) * 0.5)) < 1e-9);
    CHECK (sc > 0.02 && sc < 0.05);
    bad.getParent().removeChild (bad, nullptr);
    const int n = composer.renderPathToArrangement (path);
    std::cout << "path render produced " << n << " clips\n";
    CHECK (n == 5 * 3);   // 5 generators x phrases at beats 0, 16, 32
    // phrases along the path differ musically (different regions)
    auto lead = Model::findTrack (state.project, 3);
    std::vector<juce::ValueTree> pathClips;
    for (auto c : lead.getChildWithName (IDs::CLIPS)) if ((double) c[IDs::start] >= 0 && c[IDs::name].toString().contains ("Gen")) pathClips.push_back (c);
    CHECK (pathClips.size() >= 3);
    engine.rebuildNow();
}


// ---------------------------------------------------------------------------- live generative mode
struct LiveRig
{
    Engine engine;
    AppState state { engine };
    std::unique_ptr<Composer> composer;
    Engine::Transport tr;
    std::vector<float> L = std::vector<float> (256), R = std::vector<float> (256);
    float peak = 0.0f; bool finite = true;
    LiveRig()
    {
        state.project = Model::createDefaultProject();
        engine.setProject (state.project);
        composer = std::make_unique<Composer> (state);
        composer->setProject (state.project);
    }
    juce::AudioBuffer<float>* record = nullptr; int recPos = 0;
    void run (int blocks)
    {
        for (int k = 0; k < blocks; ++k)
        {
            tr.playing = engine.isPlaying();
            engine.processBlock (L.data(), R.data(), nullptr, nullptr, 256, tr, -1, false);
            if (record != nullptr && recPos + 256 <= record->getNumSamples())
            { record->copyFrom (0, recPos, L.data(), 256); record->copyFrom (1, recPos, R.data(), 256); recPos += 256; }
            for (int j = 0; j < 256; ++j) { peak = std::max (peak, std::abs (L[(size_t) j])); finite &= std::isfinite (L[(size_t) j]) && std::isfinite (R[(size_t) j]); }
            if (k % 8 == 7) composer->liveTickNow();     // ~46 ms, like the 20 Hz timer
        }
        composer->liveTickNow();
    }
    int blocksForBars (double bars) const { return (int) std::ceil (bars * 4.0 * 60.0 / state.tempo() * engine.getSampleRate() / 256.0); }
};

static void testLive()
{
    juce::ScopedJuceInitialiser_GUI init;
    LiveRig rig;
    CHECK (rig.engine.isLiveOn());
    CHECK (rig.engine.getLive().hasFrame());
    const double x0 = rig.composer->probeX(), y0 = rig.composer->probeY();
    rig.engine.play();
    juce::AudioBuffer<float> rec (2, rig.blocksForBars (16) * 256);
    rig.record = &rec;
    rig.run (rig.blocksForBars (16));
    rig.record = nullptr;
    if (auto w = makeWavWriter (juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("mandel_live.wav"), rig.engine.getSampleRate(), 2, 16))
        w->writeFromAudioSampleBuffer (rec, 0, rig.recPos);
    const auto& hist = rig.composer->liveHistory();
    std::cout << "live: " << hist.size() << " notes in 16 bars, peak " << rig.peak << ", probe moved to " << rig.composer->probeX() << ", " << rig.composer->probeY() << "\n";
    CHECK (rig.peak > 0.02f && rig.peak < 1.5f);
    CHECK (rig.finite);
    CHECK (rig.composer->probeX() != x0 || rig.composer->probeY() != y0);     // drift
    int perPart[Live::NumParts] = {};
    bool inKey = true;
    std::set<int> leadPitches;
    std::vector<int> leadEarly, leadLate;
    const double bpb = rig.state.beatsPerBar();
    for (auto& n : hist)
    {
        perPart[n.part]++;
        if (n.part != Live::Drums && ! Scales::contains (n.pitch, 9, Scales::Dorian, 0xfff)) inKey = false;
        if (n.part == Live::Lead)
        {
            leadPitches.insert (n.pitch);
            if (n.start >= 2 * bpb && n.start < 4 * bpb) leadEarly.push_back (n.pitch);
            if (n.start >= 12 * bpb && n.start < 14 * bpb) leadLate.push_back (n.pitch);
        }
    }
    for (int p = 1; p < Live::NumParts; ++p) { std::cout << "  " << Live::partNames()[p] << ": " << perPart[p] << "\n"; CHECK (perPart[p] > 0); }
    CHECK (inKey);                                   // everything mapped onto A dorian
    CHECK (leadPitches.size() >= 4);
    CHECK (leadEarly != leadLate);                   // always evolving
    CHECK (perPart[Live::Drums] > 16 * 4);

    // key change applies immediately
    auto live = Model::liveNode (rig.state.project);
    live.setProperty (IDs::root, 0, nullptr);
    live.setProperty (IDs::scale, (int) Scales::PentatonicMinor, nullptr);
    rig.composer->liveTickNow();
    const auto before = rig.composer->liveHistory().size();
    rig.run (rig.blocksForBars (4));
    bool inPenta = true; int counted = 0;
    const auto& h2 = rig.composer->liveHistory();
    for (size_t i = before; i < h2.size(); ++i)
        if (h2[i].part != Live::Drums && h2[i].start > rig.engine.getLiveBeat() - 3 * bpb) { ++counted; inPenta &= Scales::contains (h2[i].pitch, 0, Scales::PentatonicMinor, 0xfff); }
    CHECK (counted > 0 && inPenta);

    // part off -> silent part
    for (auto lp : live) if ((int) lp[IDs::id] == Live::Lead) lp.setProperty (IDs::liveOn, false, nullptr);
    rig.composer->liveTickNow();
    const double offFrom = rig.engine.getLiveBeat();
    rig.run (rig.blocksForBars (2));
    int leadAfter = 0;
    for (auto& n : rig.composer->liveHistory()) if (n.part == Live::Lead && n.start > offFrom + 0.01) ++leadAfter;
    CHECK (leadAfter == 0);

    // capture into the arrangement
    const int clips = rig.composer->captureLive (8);
    std::cout << "captured clips: " << clips << "\n";
    CHECK (clips >= 4);
    int notesInClips = 0;
    for (auto t : Model::tracks (rig.state.project))
        for (auto c : t.getChildWithName (IDs::CLIPS)) { notesInClips += (int) Model::readNotes (c).size(); CHECK ((double) c[IDs::length] == 8 * bpb); }
    CHECK (notesInClips > 30);
    rig.state.undo.undo();
    CHECK (Model::projectEndBeat (rig.state.project) == 0.0);

    // stop: everything is released
    rig.engine.stop();
    rig.run (rig.blocksForBars (3));
    int voices = 0;
    for (auto t : Model::tracks (rig.state.project))
        if (auto* tp = rig.engine.getTrackProcessor (t[IDs::id]); tp != nullptr && tp->synth) voices += tp->synth->activeVoiceCount();
    CHECK (voices == 0);
}

static void testTransportFL()
{
    juce::ScopedJuceInitialiser_GUI init;
    Engine engine;
    auto p = Model::createDefaultProject();
    Model::liveNode (p).setProperty (IDs::liveOn, false, nullptr);
    engine.setProject (p);
    auto bass = Model::findTrack (p, 2);
    auto clip = Model::addMidiClip (bass, 0.0, 8.0, nullptr);
    Model::addMidiClip (bass, 16.0, 6.0, nullptr);
    engine.rebuildNow();
    // SONG: loops at the end of the song (rounded up to the bar)
    CHECK (engine.isLoopActive() && engine.getLoopStart() == 0.0 && engine.getLoopEnd() == 24.0);
    // timeline selection wins
    p.setProperty (IDs::loopStart, 4.0, nullptr); p.setProperty (IDs::loopEnd, 12.0, nullptr); p.setProperty (IDs::loopOn, true, nullptr);
    CHECK (engine.isLoopActive() && engine.getLoopStart() == 4.0 && engine.getLoopEnd() == 12.0);
    p.setProperty (IDs::loopOn, false, nullptr);
    // PAT: loops the selected clip
    p.setProperty (IDs::songMode, false, nullptr);
    engine.setPatternClip ((int) clip[IDs::id]);
    CHECK (engine.isLoopActive() && engine.getLoopStart() == 0.0 && engine.getLoopEnd() == 8.0);
    // live on: no song loop
    p.setProperty (IDs::songMode, true, nullptr);
    Model::liveNode (p).setProperty (IDs::liveOn, true, nullptr);
    CHECK (! engine.isLoopActive());
    Model::liveNode (p).setProperty (IDs::liveOn, false, nullptr);
    // Space = play / stop and return to where it started
    engine.setPosition (3.0);
    engine.play();
    Engine::Transport tr; tr.playing = true; tr.beat = 3.0;
    std::vector<float> L (256), R (256);
    for (int k = 0; k < 400; ++k) engine.processBlock (L.data(), R.data(), nullptr, nullptr, 256, tr, -1, false);
    CHECK (tr.beat > 3.0 || tr.loopPass > 0);
    engine.stop();
    CHECK (std::abs (engine.getPosition() - 3.0) < 1e-9);
    engine.stop();
    CHECK (engine.getPosition() == 0.0);
    // song loop actually wraps
    Engine::Transport t2; t2.playing = true; t2.beat = 23.0;
    for (int k = 0; k < 200; ++k) engine.processBlock (L.data(), R.data(), nullptr, nullptr, 256, t2, -1, false);
    CHECK (t2.loopPass >= 1 && t2.beat < 24.0);
}

static void testGenerateVariety()
{
    juce::ScopedJuceInitialiser_GUI init;
    Engine engine;
    AppState state (engine);
    state.project = Model::createDefaultProject();
    engine.setProject (state.project);
    Composer composer (state);
    composer.setProject (state.project);
    auto g = composer.addGenerator (4, Gen::Melody);
    auto c1 = composer.generate (g, 0.0);
    auto c2 = composer.generate (g, 0.0);       // same place, same probe: must still be new
    CHECK (c1.isValid() && c2.isValid() && c1 != c2);
    CHECK ((double) c2[IDs::start] >= (double) c1[IDs::start] + (double) c1[IDs::length] - 1e-9);   // not stacked
    auto n1 = Model::readNotes (c1), n2 = Model::readNotes (c2);
    bool same = n1.size() == n2.size();
    for (size_t i = 0; same && i < n1.size(); ++i) same = n1[i].pitch == n2[i].pitch && n1[i].start == n2[i].start;
    CHECK (! same);
}

// ---------------------------------------------------------------------------- arrangement interaction (simulated mouse)
static juce::MouseEvent mouseAt (juce::Component& c, juce::Point<float> p, juce::ModifierKeys mods = {}, int clicks = 1, juce::Point<float> down = { -1, -1 })
{
    auto src = juce::Desktop::getInstance().getMainMouseSource();
    if (down.x < 0) down = p;
    return juce::MouseEvent (src, p, mods, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, &c, &c, juce::Time::getCurrentTime(), down, juce::Time::getCurrentTime(), clicks, p != down);
}

static void testArrangementUI()
{
    juce::ScopedJuceInitialiser_GUI init;
    if (juce::Desktop::getInstance().getDisplays().getPrimaryDisplay() == nullptr) { std::cout << "arrangement UI test skipped (no display)\n"; return; }
    Engine engine;
    AppState state (engine);
    state.project = Model::createDefaultProject();
    Model::liveNode (state.project).setProperty (IDs::liveOn, false, nullptr);
    engine.setProject (state.project);
    Composer composer (state);
    composer.setProject (state.project);
    ArrangementView view (state, composer);
    view.setProject (state.project);
    view.setSize (1400, 700);
    auto& body = *view.getChildComponent (0);
    const float ppb = 24.0f, headerW = 220.0f, rulerH = 30.0f;
    auto xOf = [&] (double beat) { return headerW + (float) beat * ppb; };
    auto yOfTrack = [&] (int index) { return rulerH + index * 64.0f + 32.0f; };
    auto bass = Model::findTrack (state.project, 2);
    auto clipsOf = [] (juce::ValueTree t) { return t.getChildWithName (IDs::CLIPS); };

    // double-click empty space on a synth track creates a 2-bar MIDI clip at that bar
    view.mouseDoubleClick (mouseAt (body, { xOf (5.0), yOfTrack (1) }, {}, 2));
    CHECK (clipsOf (bass).getNumChildren() == 1);
    auto clip = clipsOf (bass).getChild (0);
    CHECK ((double) clip[IDs::start] == 4.0 && (double) clip[IDs::length] == 8.0);
    CHECK (state.selectedClip == clip);

    // drag the clip body 4 beats to the right
    const juce::Point<float> grab (xOf (6.0), yOfTrack (1));
    view.mouseDown (mouseAt (body, grab));
    view.mouseDrag (mouseAt (body, grab + juce::Point<float> (4 * ppb, 0), {}, 1, grab));
    view.mouseUp (mouseAt (body, grab + juce::Point<float> (4 * ppb, 0), {}, 1, grab));
    CHECK ((double) clip[IDs::start] == 8.0);

    // drag the right edge to make it 4 beats longer
    const juce::Point<float> edge (xOf (16.0) - 2.0f, yOfTrack (1));
    view.mouseDown (mouseAt (body, edge));
    view.mouseDrag (mouseAt (body, edge + juce::Point<float> (4 * ppb, 0), {}, 1, edge));
    view.mouseUp (mouseAt (body, edge + juce::Point<float> (4 * ppb, 0), {}, 1, edge));
    CHECK ((double) clip[IDs::length] == 12.0);

    // drag it down onto the Chords track (same kind)
    auto chords = Model::findTrack (state.project, 3);
    const juce::Point<float> g2 (xOf (10.0), yOfTrack (1));
    view.mouseDown (mouseAt (body, g2));
    view.mouseDrag (mouseAt (body, g2 + juce::Point<float> (0, 64), {}, 1, g2));
    view.mouseUp (mouseAt (body, g2 + juce::Point<float> (0, 64), {}, 1, g2));
    CHECK (clipsOf (chords).getNumChildren() == 1 && clipsOf (bass).getNumChildren() == 0);

    // undo puts it back on the bass track
    state.undo.undo();
    CHECK (clipsOf (bass).getNumChildren() == 1);

    // ruler: drag = loop selection, click = clear selection + move playhead
    const juce::Point<float> r0 (xOf (4.0), 15.0f);
    view.mouseDown (mouseAt (body, r0));
    view.mouseDrag (mouseAt (body, { xOf (12.0), 15.0f }, {}, 1, r0));
    view.mouseUp (mouseAt (body, { xOf (12.0), 15.0f }, {}, 1, r0));
    CHECK ((bool) state.project[IDs::loopOn] && (double) state.project[IDs::loopStart] == 4.0 && (double) state.project[IDs::loopEnd] == 12.0);
    CHECK (engine.isLoopActive() && engine.getLoopStart() == 4.0);
    const juce::Point<float> r1 (xOf (2.0), 15.0f);
    view.mouseDown (mouseAt (body, r1));
    view.mouseUp (mouseAt (body, r1));
    CHECK (! (bool) state.project[IDs::loopOn]);
    CHECK (std::abs (engine.getPosition() - 2.0) < 1e-9);

    // duplicate, split, delete
    state.selectClip (clip);
    view.duplicateSelected();
    CHECK (clipsOf (bass).getNumChildren() == 2);
    engine.setPosition ((double) state.selectedClip[IDs::start] + 4.0);
    view.splitSelectedAtPlayhead();
    CHECK (clipsOf (bass).getNumChildren() == 3);
    view.deleteSelected();
    CHECK (clipsOf (bass).getNumChildren() == 2);

    // add track
    const int before = Model::tracks (state.project).getNumChildren();
    view.addTrack ("synth");
    CHECK (Model::tracks (state.project).getNumChildren() == before + 1);

    // the Live page builds and lays out with this project
    LivePage live (state, composer);
    live.setProject (state.project);
    live.setSize (1400, 800);
    CHECK (live.getNumChildComponents() > 10);
}

int main()
{
    testScales();
    testFractal();
    testGenerator();
    testModel();
    testSnapshotEvents();
    testEngineRender();
    testComposer();
    testLive();
    testTransportFL();
    testGenerateVariety();
    testArrangementUI();
    std::cout << checks << " checks, " << failures << " failures\n";
    return failures == 0 ? 0 : 1;
}
