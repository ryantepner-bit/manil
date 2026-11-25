#include "SynthPage.h"

using namespace SynthP;

void WavetableDisplay::paint (juce::Graphics& g)
{
    g.setColour (Theme::bg);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 4.0f);
    auto* tp = state.engine.getTrackProcessor (trackId);
    if (tp == nullptr || ! tp->synth) return;
    auto wt = tp->synth->getWavetable();
    if (wt == nullptr || wt->frames == 0) return;
    const float pos = tp->synth->params[wtPos].load();
    auto r = getLocalBounds().toFloat().reduced (6);
    const int frames = wt->frames;
    const int step = juce::jmax (1, frames / 12);
    for (int f = 0; f < frames; f += step)
    {
        const float depth = (float) f / (float) juce::jmax (1, frames - 1);
        const float ox = depth * r.getWidth() * 0.25f, oy = -depth * r.getHeight() * 0.35f;
        auto area = juce::Rectangle<float> (r.getX() + ox, r.getY() + r.getHeight() * 0.35f + oy, r.getWidth() * 0.75f, r.getHeight() * 0.65f);
        juce::Path p;
        const float* d = wt->frame (f);
        for (int i = 0; i < wt->size; i += 16)
        {
            const float x = area.getX() + area.getWidth() * i / wt->size;
            const float y = area.getCentreY() - d[i] * area.getHeight() * 0.45f;
            if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
        }
        const bool current = std::abs (depth - pos) < 0.5f / juce::jmax (1, frames / step);
        g.setColour ((current ? Theme::accent2 : Theme::accent).withAlpha (current ? 1.0f : 0.25f + 0.4f * (1.0f - depth)));
        g.strokePath (p, juce::PathStrokeType (current ? 1.8f : 1.0f));
    }
}

SynthPage::SynthPage (AppState& s, Composer& c) : state (s), composer (c), wtDisplay (s)
{
    title.setFont (Theme::font (15.0f, true));
    addAndMakeVisible (title);
    trackBox.onChange = [this] { bindTrack (Model::findTrack (project, trackBox.getSelectedId() - 1)); };
    addAndMakeVisible (trackBox);

    comps.resize ((size_t) NumParams, nullptr);
    const auto& d = defs();
    for (int i = 0; i < NumParams; ++i)
    {
        juce::Component* cp;
        if (d[(size_t) i].isChoice()) cp = owned.add (new ParamChoice (i >= m0src ? juce::String() : juce::String (d[(size_t) i].label), d[(size_t) i].choices));
        else
        {
            auto* k = new ParamKnob (d[(size_t) i]);
            if (i >= cutoff && i <= drive) k->setColour (Theme::accent2);
            if (i >= ampA && i <= modR) k->setColour (Theme::accent3);
            if (i >= m0src) k->setColour (Theme::warn);
            cp = owned.add (k);
        }
        comps[(size_t) i] = cp;
        addAndMakeVisible (cp);
    }
    static_cast<ParamChoice*> (comp (engine))->onChange = [this] (int) { updateVisibility(); };

    wtFromFractal.onClick = [this]
    {
        if (! track.isValid()) return;
        auto sn = track.getChildWithName (IDs::SYNTH);
        state.beginEdit ("Fractal wavetable");
        sn.setProperty ("wtFile", "", &state.undo);
        sn.setProperty ("wtCx", composer.probeX(), &state.undo);
        sn.setProperty ("wtCy", composer.probeY(), &state.undo);
        if (state.setStatus) state.setStatus ("Wavetable built from the orbit spectrum around " + juce::String (composer.probeX(), 5) + ", " + juce::String (composer.probeY(), 5));
    };
    auto loadInto = [this] (const juce::String& prop)
    {
        chooser = std::make_unique<juce::FileChooser> ("Choose a WAV", juce::File(), "*.wav;*.aif;*.aiff;*.flac");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this, prop] (const juce::FileChooser& fc)
        {
            auto f = fc.getResult();
            if (! f.existsAsFile() || ! track.isValid()) return;
            if (AudioPool::get().load (f) == nullptr) { if (state.setStatus) state.setStatus ("Could not read " + f.getFileName()); return; }
            state.beginEdit ("Load sample");
            track.getChildWithName (IDs::SYNTH).setProperty (prop, f.getFullPathName(), &state.undo);
        });
    };
    wtLoad.onClick = [loadInto] { loadInto ("wtFile"); };
    grainLoad.onClick = [loadInto] { loadInto ("grainFile"); };
    grainClear.onClick = [this] { if (track.isValid()) { state.beginEdit ("Grain source"); track.getChildWithName (IDs::SYNTH).setProperty ("grainFile", "", &state.undo); } };
    init.onClick = [this]
    {
        if (! track.isValid()) return;
        state.beginEdit ("Init patch");
        auto sn = track.getChildWithName (IDs::SYNTH);
        for (auto& p : defs()) sn.setProperty (p.id, p.defV, &state.undo);
    };
    randomize.onClick = [this]
    {
        // mutate the patch using the fractal features as a deterministic seed
        if (! track.isValid()) return;
        state.beginEdit ("Mutate patch");
        auto sn = track.getChildWithName (IDs::SYNTH);
        juce::Random r ((juce::int64) state.features.hash());
        for (int i = 0; i < NumParams; ++i)
        {
            const auto& p = defs()[(size_t) i];
            if (i == engine || i == SynthP::gain || i >= m0src || p.isChoice()) continue;
            const float n = juce::jlimit (0.0f, 1.0f, p.toNorm ((float) sn.getProperty (p.id, p.defV)) + (r.nextFloat() - 0.5f) * 0.3f);
            sn.setProperty (p.id, p.fromNorm (n), &state.undo);
        }
    };
    for (auto* b : { &wtFromFractal, &wtLoad, &grainLoad, &grainClear, &init, &randomize }) addAndMakeVisible (b);
    addAndMakeVisible (wtDisplay);
    keyboard.setAvailableRange (24, 108);
    keyboard.setLowestVisibleKey (36);
    keyboard.setKeyWidth (18.0f);
    keyState.addListener (this);
    addAndMakeVisible (keyboard);
    state.selectionChanged.addChangeListener (this);
}

SynthPage::~SynthPage()
{
    keyState.removeListener (this);
    state.selectionChanged.removeChangeListener (this);
    if (project.isValid()) project.removeListener (this);
}

void SynthPage::setProject (juce::ValueTree p)
{
    if (project.isValid()) project.removeListener (this);
    project = p;
    project.addListener (this);
    track = {};
    refreshTrackList();
}

void SynthPage::refreshTrackList()
{
    trackBox.clear (juce::dontSendNotification);
    juce::ValueTree first;
    for (auto t : Model::tracks (project))
        if (t[IDs::type].toString() == "synth")
        {
            trackBox.addItem (t[IDs::name].toString(), (int) t[IDs::id] + 1);
            if (! first.isValid()) first = t;
        }
    if (! track.isValid() || ! track.getParent().isValid()) bindTrack (first);
    else trackBox.setSelectedId ((int) track[IDs::id] + 1, juce::dontSendNotification);
}

void SynthPage::changeListenerCallback (juce::ChangeBroadcaster*)
{
    if (state.selectedTrack.isValid() && state.selectedTrack[IDs::type].toString() == "synth" && state.selectedTrack != track)
        bindTrack (state.selectedTrack);
}

void SynthPage::valueTreePropertyChanged (juce::ValueTree& t, const juce::Identifier& p)
{
    if (t.hasType (IDs::TRACK) && p == IDs::name) refreshTrackList();
}

void SynthPage::bindTrack (juce::ValueTree t)
{
    track = t;
    keyState.allNotesOff (1);
    const bool ok = track.isValid();
    if (ok) trackBox.setSelectedId ((int) track[IDs::id] + 1, juce::dontSendNotification);
    title.setText (ok ? "SYNTH  /  " + track[IDs::name].toString() : "No synth track - add one in the Arrangement", juce::dontSendNotification);
    wtDisplay.trackId = ok ? (int) track[IDs::id] : -1;
    auto sn = track.getChildWithName (IDs::SYNTH);
    for (int i = 0; i < NumParams; ++i)
    {
        const auto& d = defs()[(size_t) i];
        if (auto* k = dynamic_cast<ParamKnob*> (comp (i))) { if (ok) k->bind (sn, d.id, &state.undo); else k->unbind(); }
        if (auto* c = dynamic_cast<ParamChoice*> (comp (i))) { if (ok) c->bind (sn, d.id, &state.undo); else c->unbind(); }
    }
    for (auto* b : { &wtFromFractal, &wtLoad, &grainLoad, &grainClear, &init, &randomize }) b->setEnabled (ok);
    updateVisibility();
}

void SynthPage::updateVisibility()
{
    const int e = track.isValid() ? (int) track.getChildWithName (IDs::SYNTH).getProperty ("engine", 0) : 0;
    auto vis = [this] (std::initializer_list<int> ids, bool v) { for (int i : ids) comp (i)->setAlpha (v ? 1.0f : 0.35f); };
    vis ({ osc1Wave, osc2Wave, osc2Semi, osc2Detune, oscMix, unisonDetune }, e == 0);
    vis ({ fmRatio, fmIndex, fmFeedback, fmCarrier }, e == 1);
    vis ({ wtPos, wtWarp }, e == 2);
    vis ({ grainSize, grainDensity, grainPos, grainSpray, grainPitchRand }, e == 3);
    repaint();
}

void SynthPage::handleNoteOn (juce::MidiKeyboardState*, int, int note, float vel)
{
    if (track.isValid()) state.engine.sendLiveNote (track[IDs::id], note, vel);
}

void SynthPage::handleNoteOff (juce::MidiKeyboardState*, int, int note, float)
{
    if (track.isValid()) state.engine.sendLiveNote (track[IDs::id], note, 0.0f);
}

void SynthPage::paint (juce::Graphics& g)
{
    g.fillAll (Theme::bg);
    for (auto& s : sections) Theme::drawPanel (g, s.bounds.toFloat(), s.title);
}

void SynthPage::resized()
{
    sections.clear();
    auto r = getLocalBounds().reduced (8);
    auto top = r.removeFromTop (30);
    title.setBounds (top.removeFromLeft (360));
    trackBox.setBounds (top.removeFromLeft (200)); top.removeFromLeft (8);
    init.setBounds (top.removeFromLeft (90)); top.removeFromLeft (4);
    randomize.setBounds (top.removeFromLeft (110));
    r.removeFromTop (6);
    keyboard.setBounds (r.removeFromBottom (70));
    r.removeFromBottom (6);

    const int knobW = 70, knobH = 78;
    auto placeRow = [&] (juce::Rectangle<int>& area, std::initializer_list<int> ids)
    {
        auto row = area.removeFromTop (knobH);
        for (int i : ids)
        {
            const bool choice = defs()[(size_t) i].isChoice();
            auto cell = row.removeFromLeft (choice ? 104 : knobW);
            comp (i)->setBounds (choice ? cell.withSizeKeepingCentre (cell.getWidth() - 6, 42) : cell);
        }
        area.removeFromTop (4);
    };
    auto section = [&] (juce::Rectangle<int> b, const juce::String& t) { sections.push_back ({ t, b }); return b.reduced (10, 22).withTrimmedTop (-4); };

    const int rowH = knobH * 2 + 40;
    auto row1 = r.removeFromTop (rowH);
    r.removeFromTop (6);
    auto row2 = r.removeFromTop (knobH + 40);
    r.removeFromTop (6);
    auto row3 = r;

    // row 1: engine/osc | fm | wavetable | granular
    {
        auto a = section (row1.removeFromLeft (104 * 3 + knobW * 2 + 30), "Engine / Oscillators");
        placeRow (a, { engine, osc1Wave, osc2Wave, osc2Semi, osc2Detune });
        placeRow (a, { oscMix, subLevel, noiseLevel, unisonDetune, glide });
        row1.removeFromLeft (6);
        auto f = section (row1.removeFromLeft (knobW * 2 + 24), "FM");
        placeRow (f, { fmRatio, fmIndex });
        placeRow (f, { fmFeedback, fmCarrier });
        row1.removeFromLeft (6);
        auto g = section (row1.removeFromRight (knobW * 3 + 24), "Granular");
        placeRow (g, { grainSize, grainDensity, grainPos });
        placeRow (g, { grainSpray, grainPitchRand });
        grainLoad.setBounds (g.getX() + knobW * 2 + 4, g.getY() - knobH - 4 + 6, knobW - 4, 30);
        grainClear.setBounds (grainLoad.getBounds().translated (0, 36));
        grainLoad.setButtonText ("Load WAV"); grainClear.setButtonText ("Wavetable");
        row1.removeFromRight (6);
        auto w = section (row1, "Wavetable");
        auto wk = w.removeFromLeft (knobW);
        comp (wtPos)->setBounds (wk.removeFromTop (knobH));
        comp (wtWarp)->setBounds (wk.removeFromTop (knobH));
        auto btns = w.removeFromBottom (28);
        wtFromFractal.setBounds (btns.removeFromLeft (btns.getWidth() * 3 / 5).reduced (2, 0));
        wtLoad.setBounds (btns.reduced (2, 0));
        w.removeFromBottom (4);
        wtDisplay.setBounds (w.reduced (4, 0));
    }
    // row 2: filter | amp env | mod env | lfo | out
    {
        auto f = section (row2.removeFromLeft (104 + knobW * 5 + 24), "Filter");
        placeRow (f, { filterType, cutoff, reso, filterEnv, keyTrack, drive });
        row2.removeFromLeft (6);
        auto a = section (row2.removeFromLeft (knobW * 4 + 24), "Amp envelope");
        placeRow (a, { ampA, ampD, ampS, ampR });
        row2.removeFromLeft (6);
        auto m = section (row2.removeFromLeft (knobW * 4 + 24), "Mod envelope");
        placeRow (m, { modA, modD, modS, modR });
        row2.removeFromLeft (6);
        auto o = section (row2.removeFromRight (knobW + 24), "Out");
        placeRow (o, { SynthP::gain });
        row2.removeFromRight (6);
        auto l = section (row2, "LFOs");
        placeRow (l, { lfo1Rate, lfo1Shape, lfo2Rate, lfo2Shape });
    }
    // row 3: modulation matrix (6 slots in 2 columns)
    {
        auto m = section (row3, "Modulation matrix");
        auto colA = m.removeFromLeft (m.getWidth() / 2).reduced (4, 0);
        auto colB = m.reduced (4, 0);
        const int slotH = juce::jmax (30, juce::jmin (48, colA.getHeight() / 3));
        for (int s = 0; s < 6; ++s)
        {
            auto& col = s < 3 ? colA : colB;
            auto row = col.removeFromTop (slotH);
            comp (m0src + s * 3)->setBounds (row.removeFromLeft (row.getWidth() * 2 / 5).reduced (2, 4));
            comp (m0dst + s * 3)->setBounds (row.removeFromLeft (row.getWidth() * 2 / 3).reduced (2, 4));
            comp (m0amt + s * 3)->setBounds (row.withSizeKeepingCentre (juce::jmin (row.getWidth(), slotH + 14), slotH));
        }
    }
}
