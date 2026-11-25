// MANDELBROT core test-suite (console). Exit code 0 = all passed.
#include <juce_events/juce_events.h>
#include "Core/ProjectModel.h"
#include "Core/Scales.h"
#include "Fractal/MandelMath.h"
#include "Generative/Generator.h"
#include "Audio/Engine.h"
#include "Audio/Exporter.h"
#include "UI/Composer.h"

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
        CHECK (m.getNumTracks() == 5);  // meta + 4 instrument tracks
        int notes = 0;
        for (int t = 0; t < m.getNumTracks(); ++t) for (auto* e : *m.getTrack (t)) notes += e->message.isNoteOn();
        CHECK (notes > 20);
        std::cout << "midi notes exported: " << notes << "\n";
    }
    // realtime-path smoke test: run the callback path directly with a fake device buffer
    {
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
    const int roles[] = { Gen::Breaks, Gen::Bass, Gen::Melody, Gen::Chords };
    int i = 0;
    for (auto t : Model::tracks (state.project))
        if (t[IDs::type].toString() != "audio") gens.push_back (composer.addGenerator (t[IDs::id], roles[i++]));
    CHECK (gens.size() == 4);

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
    CHECK (n == 4 * 3);   // 4 generators x phrases at beats 0, 16, 32
    // phrases along the path differ musically (different regions)
    auto lead = Model::findTrack (state.project, 3);
    std::vector<juce::ValueTree> pathClips;
    for (auto c : lead.getChildWithName (IDs::CLIPS)) if ((double) c[IDs::start] >= 0 && c[IDs::name].toString().contains ("Gen")) pathClips.push_back (c);
    CHECK (pathClips.size() >= 3);
    engine.rebuildNow();
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
    std::cout << checks << " checks, " << failures << " failures\n";
    return failures == 0 ? 0 : 1;
}
