#include "GeneratorPage.h"
#include "../Core/Scales.h"

void MotifView::paint (juce::Graphics& g)
{
    Theme::drawPanel (g, getLocalBounds().toFloat(), "Motif (preserved between generations)");
    auto m = Gen::Motif::parse (motif);
    auto r = getLocalBounds().reduced (12, 24).toFloat();
    if (m.empty()) { g.setColour (Theme::textDim); g.drawText ("no motif yet - generate to create one", r, juce::Justification::centred); return; }
    int lo = 0, hi = 0;
    for (auto& n : m.notes) { lo = juce::jmin (lo, n.degree); hi = juce::jmax (hi, n.degree); }
    const float range = (float) juce::jmax (6, hi - lo + 1);
    for (double b = 0; b <= m.length; b += 1.0)
    {
        const float x = r.getX() + (float) (b / m.length) * r.getWidth();
        g.setColour (Theme::grid);
        g.drawVerticalLine ((int) x, r.getY(), r.getBottom());
    }
    for (auto& n : m.notes)
    {
        const float x = r.getX() + (float) (n.onset / m.length) * r.getWidth();
        const float w = juce::jmax (3.0f, (float) (n.duration / m.length) * r.getWidth() - 1.0f);
        const float y = r.getBottom() - (n.degree - lo + 1) / range * r.getHeight();
        g.setColour (Theme::accent.withAlpha (0.4f + 0.6f * n.velocity / 127.0f));
        g.fillRoundedRectangle (x, y, w, juce::jmax (4.0f, r.getHeight() / range - 1.0f), 2.0f);
    }
}

GeneratorPage::GeneratorPage (AppState& s, Composer& c) : state (s), composer (c)
{
    list.setRowHeight (44);
    list.setColour (juce::ListBox::backgroundColourId, Theme::panel);
    addAndMakeVisible (list);
    for (auto* b : { &addBtn, &removeBtn, &autoSetup }) addAndMakeVisible (b);
    addBtn.onClick = [this]
    {
        juce::PopupMenu m;
        int i = 1;
        for (auto t : Model::tracks (project))
        {
            if (t[IDs::type].toString() == "audio") continue;
            juce::PopupMenu sub;
            for (int r = 0; r < Gen::NumRoles; ++r) sub.addItem (i * 100 + r, Gen::roleNames()[r]);
            m.addSubMenu (t[IDs::name].toString(), sub);
            ++i;
        }
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&addBtn), [this] (int res)
        {
            if (res <= 0) return;
            int i = 1;
            for (auto t : Model::tracks (project))
            {
                if (t[IDs::type].toString() == "audio") continue;
                if (i == res / 100) { composer.addGenerator (t[IDs::id], res % 100); list.selectRow (list.getModel()->getNumRows() - 1); return; }
                ++i;
            }
        });
    };
    removeBtn.onClick = [this]
    {
        auto g = selectedGen();
        if (g.isValid()) { state.beginEdit ("Remove generator"); g.getParent().removeChild (g, &state.undo); bindSelected(); }
    };
    autoSetup.onClick = [this]
    {
        // one generator per instrument track with a sensible role
        state.beginEdit ("Auto setup generators");
        int synthIndex = 0;
        for (auto t : Model::tracks (project))
        {
            const auto type = t[IDs::type].toString();
            if (type == "audio") continue;
            bool has = false;
            for (auto g : project.getChildWithName (IDs::GENERATORS)) has |= (int) g[IDs::track] == (int) t[IDs::id];
            if (has) continue;
            const int roles[] = { Gen::Bass, Gen::Melody, Gen::Chords, Gen::Arp, Gen::Texture };
            const int role = type == "drums" ? Gen::Breaks : roles[synthIndex++ % 5];
            composer.addGenerator (t[IDs::id], role);
        }
        list.updateContent();
        if (list.getSelectedRow() < 0) list.selectRow (0);
    };
    liveAll.setToggleState (composer.liveEnabled, juce::dontSendNotification);
    liveAll.onClick = [this] { composer.liveEnabled = liveAll.getToggleState(); };
    addAndMakeVisible (liveAll);

    juce::StringArray roles = Gen::roleNames();
    role.box.addItemList (roles, 1);
    scale.box.addItemList (Scales::names(), 1);
    root.box.addItemList (Scales::rootNames(), 1);
    for (auto* p : { &role, &scale, &root }) addAndMakeVisible (p);
    trackBox.onChange = [this]
    {
        auto g = selectedGen();
        if (g.isValid() && trackBox.getSelectedId() > 0) { state.beginEdit ("Generator track"); g.setProperty (IDs::track, trackBox.getSelectedId() - 1, &state.undo); }
    };
    addAndMakeVisible (trackBox);
    bars.slider.setRange (1, 16, 1); octave.slider.setRange (1, 7, 1); seed.slider.setRange (0, 99999, 1);
    seed.slider.setNumDecimalPlacesToDisplay (0); bars.slider.setNumDecimalPlacesToDisplay (0); octave.slider.setNumDecimalPlacesToDisplay (0);
    for (auto* k : { &density, &complexity, &mutation, &bars, &octave, &seed }) addAndMakeVisible (k);
    density.setColour (Theme::accent); complexity.setColour (Theme::accent3); mutation.setColour (Theme::accent2);
    dice.onClick = [this] { auto g = selectedGen(); if (g.isValid()) { state.beginEdit ("Seed"); g.setProperty (IDs::seed, juce::Random::getSystemRandom().nextInt (99999), &state.undo); } };
    addAndMakeVisible (dice);
    live.setTooltip ("While playing, this generator evolves its clip from the fractal location (loop on) or writes new phrases ahead along the timeline (loop off)");
    lock.setTooltip ("Use the generator's stored point instead of the live probe");
    addAndMakeVisible (live);
    addAndMakeVisible (lock);
    for (int i = 0; i < 12; ++i)
    {
        auto* b = maskBtns.add (new juce::TextButton (Scales::rootNames()[i]));
        b->setClickingTogglesState (false);
        b->onClick = [this, i]
        {
            auto g = selectedGen();
            if (! g.isValid()) return;
            state.beginEdit ("Custom scale");
            const int mask = (int) g.getProperty (IDs::customMask, 0xfff) ^ (1 << i);
            g.setProperty (IDs::customMask, mask, &state.undo);
            g.setProperty (IDs::scale, (int) Scales::Custom, &state.undo);
        };
        addAndMakeVisible (b);
    }

    genNew.setColour (juce::TextButton::buttonColourId, Theme::accent.withAlpha (0.3f));
    genNew.onClick = [this]
    {
        auto g = selectedGen();
        if (! g.isValid()) return;
        state.beginEdit ("Generate");
        const double at = std::floor (state.engine.getPosition() / state.beatsPerBar()) * state.beatsPerBar();
        state.selectClip (composer.generate (g, at));
    };
    regen.onClick = [this]
    {
        auto g = selectedGen();
        if (! g.isValid()) return;
        auto clip = state.selectedClip;
        if (! clip.isValid() || (int) Model::trackOfClip (clip)[IDs::id] != (int) g[IDs::track]) clip = composer.findClip ((int) g.getProperty ("clip", -1));
        if (! clip.isValid()) { if (state.setStatus) state.setStatus ("Select a clip on the generator's track first"); return; }
        state.beginEdit ("Regenerate");
        composer.generate (g, clip[IDs::start], clip, false);
        state.selectClip (clip);
    };
    mutate.onClick = [this]
    {
        auto g = selectedGen();
        if (! g.isValid()) return;
        auto clip = composer.findClip ((int) g.getProperty ("clip", -1));
        state.beginEdit ("Mutate");
        composer.generate (g, clip.isValid() ? (double) clip[IDs::start] : 0.0, clip, true);
    };
    toProbe.onClick = [this]
    {
        auto g = selectedGen();
        if (! g.isValid()) return;
        state.beginEdit ("Generator point");
        g.setProperty (IDs::cx, composer.probeX(), &state.undo);
        g.setProperty (IDs::cy, composer.probeY(), &state.undo);
    };
    resetMotif.onClick = [this] { auto g = selectedGen(); if (g.isValid()) { state.beginEdit ("Forget motif"); g.setProperty (IDs::motif, "", &state.undo); } };
    for (auto* b : { &genNew, &regen, &mutate, &toProbe, &resetMotif }) addAndMakeVisible (b);

    info.setFont (Theme::mono (11.5f));
    info.setJustificationType (juce::Justification::topLeft);
    addAndMakeVisible (info);
    addAndMakeVisible (motifView);
    state.featuresChanged.addChangeListener (this);
}

GeneratorPage::~GeneratorPage()
{
    state.featuresChanged.removeChangeListener (this);
    if (project.isValid()) project.removeListener (this);
}

void GeneratorPage::setProject (juce::ValueTree p)
{
    if (project.isValid()) project.removeListener (this);
    project = p;
    project.addListener (this);
    list.updateContent();
    list.selectRow (0);
    bindSelected();
}

juce::ValueTree GeneratorPage::selectedGen() const
{
    return project.getChildWithName (IDs::GENERATORS).getChild (list.getSelectedRow());
}

int GeneratorPage::getNumRows() { return project.getChildWithName (IDs::GENERATORS).getNumChildren(); }

void GeneratorPage::paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool selected)
{
    auto gen = project.getChildWithName (IDs::GENERATORS).getChild (row);
    if (! gen.isValid()) return;
    auto track = Model::findTrack (project, gen[IDs::track]);
    g.setColour (selected ? Theme::panelLight : Theme::panel);
    g.fillRect (0, 0, w, h);
    g.setColour (track.isValid() ? juce::Colour ((juce::uint32) (juce::int64) track[IDs::colour]) : Theme::textDim);
    g.fillRect (0, 0, 4, h);
    g.setColour (Theme::text);
    g.setFont (Theme::font (13.0f, true));
    g.drawText (Gen::roleNames()[(int) gen[IDs::role]] + "  ->  " + (track.isValid() ? track[IDs::name].toString() : juce::String ("(missing track)")), 12, 4, w - 16, 18, juce::Justification::centredLeft);
    g.setColour (Theme::textDim);
    g.setFont (Theme::font (11.0f));
    g.drawText (Scales::rootNames()[(int) gen[IDs::root]] + " " + Scales::name ((int) gen[IDs::scale]) + "   " + juce::String ((int) gen[IDs::bars]) + " bars   gen #" + juce::String ((int) gen[IDs::generation])
                + ((bool) gen[IDs::live] ? "   LIVE" : ""), 12, 22, w - 16, 16, juce::Justification::centredLeft);
    g.setColour (Theme::outline);
    g.drawHorizontalLine (h - 1, 0, (float) w);
}

void GeneratorPage::selectedRowsChanged (int) { bindSelected(); }

void GeneratorPage::refreshTracks()
{
    trackBox.clear (juce::dontSendNotification);
    for (auto t : Model::tracks (project))
        if (t[IDs::type].toString() != "audio") trackBox.addItem (t[IDs::name].toString(), (int) t[IDs::id] + 1);
    auto g = selectedGen();
    if (g.isValid()) trackBox.setSelectedId ((int) g[IDs::track] + 1, juce::dontSendNotification);
}

void GeneratorPage::bindSelected()
{
    auto g = selectedGen();
    const bool ok = g.isValid();
    for (auto* c : std::initializer_list<juce::Component*> { &role, &scale, &root, &trackBox, &density, &complexity, &mutation, &bars, &octave, &seed, &dice, &live, &lock, &genNew, &regen, &mutate, &toProbe, &resetMotif })
        c->setEnabled (ok);
    for (auto* b : maskBtns) b->setEnabled (ok);
    refreshTracks();
    if (! ok) { info.setText ("Add a generator: each one turns the fractal's characteristics into music for one track.", juce::dontSendNotification); motifView.setMotif ({}); return; }
    role.bind (g, IDs::role, &state.undo);
    scale.bind (g, IDs::scale, &state.undo);
    root.bind (g, IDs::root, &state.undo);
    density.bind (g, IDs::density, &state.undo);
    complexity.bind (g, IDs::complexity, &state.undo);
    mutation.bind (g, IDs::mutation, &state.undo);
    bars.bind (g, IDs::bars, &state.undo);
    octave.bind (g, IDs::octave, &state.undo);
    seed.bind (g, IDs::seed, &state.undo);
    live.bind (g, IDs::live, &state.undo);
    lock.bind (g, IDs::lockPoint, &state.undo);
    const int mask = g.getProperty (IDs::customMask, 0xfff);
    const auto degs = Scales::degrees ((int) g[IDs::scale], mask);
    for (int i = 0; i < 12; ++i)
    {
        const bool in = std::find (degs.begin(), degs.end(), i) != degs.end();
        maskBtns[i]->setToggleState (in, juce::dontSendNotification);
        maskBtns[i]->setButtonText (Scales::rootNames()[((int) g[IDs::root] + i) % 12]);
    }
    motifView.setMotif (g[IDs::motif].toString());
    changeListenerCallback (nullptr);
}

void GeneratorPage::valueTreePropertyChanged (juce::ValueTree& t, const juce::Identifier& p)
{
    if (t.hasType (IDs::GEN))
    {
        list.repaint();
        if (t == selectedGen())
        {
            if (p == IDs::motif) motifView.setMotif (t[IDs::motif].toString());
            if (p == IDs::scale || p == IDs::customMask || p == IDs::root || p == IDs::track) bindSelected();
        }
    }
    if (t.hasType (IDs::TRACK) && p == IDs::name) { refreshTracks(); list.repaint(); }
}

void GeneratorPage::changeListenerCallback (juce::ChangeBroadcaster*)
{
    auto g = selectedGen();
    juce::String txt;
    const auto& f = state.features;
    txt << "Fractal probe  " << juce::String (f.cx, 7) << ", " << juce::String (f.cy, 7) << "\n" << f.describe() << "\n";
    if (g.isValid())
    {
        auto s = Gen::settingsFromTree (g, juce::jmax (1, (int) std::round (state.beatsPerBar())));
        txt << "\n" << Gen::MusicalTraits::from (f, s).describe() << "\n";
        txt << "Generation #" << (int) g[IDs::generation] << "   point " << juce::String ((double) g[IDs::cx], 5) << ", " << juce::String ((double) g[IDs::cy], 5)
            << ((bool) g[IDs::lockPoint] ? "  (locked)" : "  (follows probe)");
    }
    info.setText (txt, juce::dontSendNotification);
}

void GeneratorPage::paint (juce::Graphics& g)
{
    g.fillAll (Theme::bg);
    auto r = getLocalBounds().reduced (6);
    Theme::drawPanel (g, r.removeFromLeft (300).toFloat(), "Generators");
    r.removeFromLeft (6);
    Theme::drawPanel (g, r.removeFromTop (330).toFloat(), "Generator settings");
}

void GeneratorPage::resized()
{
    auto r = getLocalBounds().reduced (6);
    auto left = r.removeFromLeft (300).reduced (8, 22);
    left.removeFromTop (2);
    auto lb = left.removeFromBottom (30);
    addBtn.setBounds (lb.removeFromLeft (130)); lb.removeFromLeft (6); removeBtn.setBounds (lb);
    left.removeFromBottom (6);
    autoSetup.setBounds (left.removeFromBottom (28));
    left.removeFromBottom (6);
    list.setBounds (left);
    r.removeFromLeft (6);

    auto top = r.removeFromTop (330).reduced (12, 22);
    auto row1 = top.removeFromTop (42);
    role.setBounds (row1.removeFromLeft (130)); row1.removeFromLeft (8);
    {
        auto tb = row1.removeFromLeft (160);
        tb.removeFromTop (14);
        trackBox.setBounds (tb.withHeight (24));
    }
    row1.removeFromLeft (8);
    scale.setBounds (row1.removeFromLeft (150)); row1.removeFromLeft (8);
    root.setBounds (row1.removeFromLeft (70)); row1.removeFromLeft (16);
    live.setBounds (row1.removeFromLeft (60).withTrimmedTop (14).withHeight (26)); row1.removeFromLeft (6);
    lock.setBounds (row1.removeFromLeft (90).withTrimmedTop (14).withHeight (26));
    top.removeFromTop (8);
    auto maskRow = top.removeFromTop (24);
    const int mw = juce::jmin (44, maskRow.getWidth() / 12);
    for (auto* b : maskBtns) { b->setBounds (maskRow.removeFromLeft (mw).reduced (1, 0)); }
    top.removeFromTop (10);
    auto knobs = top.removeFromTop (86);
    for (auto* k : { &density, &complexity, &mutation, &bars, &octave, &seed }) { k->setBounds (knobs.removeFromLeft (78)); knobs.removeFromLeft (4); }
    dice.setBounds (knobs.removeFromLeft (54).withSizeKeepingCentre (54, 26));
    top.removeFromTop (10);
    auto btns = top.removeFromTop (30);
    genNew.setBounds (btns.removeFromLeft (220)); btns.removeFromLeft (6);
    regen.setBounds (btns.removeFromLeft (180)); btns.removeFromLeft (6);
    mutate.setBounds (btns.removeFromLeft (160)); btns.removeFromLeft (6);
    toProbe.setBounds (btns.removeFromLeft (110)); btns.removeFromLeft (6);
    resetMotif.setBounds (btns.removeFromLeft (110));
    top.removeFromTop (10);
    liveAll.setBounds (top.removeFromTop (24));

    r.removeFromTop (6);
    motifView.setBounds (r.removeFromTop (juce::jmin (180, r.getHeight() / 2)));
    r.removeFromTop (6);
    info.setBounds (r);
}
