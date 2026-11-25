#include "LivePage.h"
#include "../Core/Scales.h"
#include "../Live/LiveSequencer.h"
#include "../Audio/SampleLibrary.h"

static juce::Colour partColour (int part)
{
    static const juce::uint32 c[] = { 0xff7d869a, 0xffff6b9d, 0xffffc85a, 0xffb38cff, 0xff5ad1ff, 0xff6bffb8, 0xffff8a5a };
    return juce::Colour (c[juce::jlimit (0, 6, part)]);
}

// ============================================================================ note view
void LiveNoteView::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (Theme::panel);
    g.fillRoundedRectangle (r, 6.0f);
    const double bpb = state.beatsPerBar();
    const double now = state.engine.getLiveBeat();
    const double span = 8.0 * bpb;
    const double t0 = now - span;
    auto area = r.reduced (8.0f, 6.0f).withTrimmedLeft (54.0f);
    const float laneH = area.getHeight() / (Live::NumParts - 1);
    // bars
    for (double b = std::ceil (t0 / bpb) * bpb; b <= now; b += bpb)
    {
        const float x = area.getX() + (float) ((b - t0) / span) * area.getWidth();
        g.setColour (Theme::gridStrong);
        g.drawVerticalLine ((int) x, area.getY(), area.getBottom());
    }
    g.setFont (Theme::font (11.0f, true));
    for (int p = 1; p < Live::NumParts; ++p)
    {
        auto lane = juce::Rectangle<float> (r.getX() + 8, area.getY() + (p - 1) * laneH, 50, laneH);
        g.setColour (partColour (p));
        g.drawText (Live::partNames()[p], lane, juce::Justification::centredLeft);
        g.setColour (Theme::outline.withAlpha (0.5f));
        g.drawHorizontalLine ((int) (area.getY() + p * laneH), area.getX(), area.getRight());
    }
    // pitch range per lane
    int lo[Live::NumParts], hi[Live::NumParts];
    for (int p = 0; p < Live::NumParts; ++p) { lo[p] = 127; hi[p] = 0; }
    const auto& hist = composer.liveHistory();
    for (auto& n : hist) if (n.start >= t0) { lo[n.part] = juce::jmin (lo[n.part], n.pitch); hi[n.part] = juce::jmax (hi[n.part], n.pitch); }
    auto drawNote = [&] (const Composer::LiveNote& n)
    {
        if (n.part <= 0 || n.part >= Live::NumParts) return;
        const double e = n.end < 0 ? now : n.end;
        if (e < t0) return;
        const float x0 = area.getX() + (float) ((juce::jmax (n.start, t0) - t0) / span) * area.getWidth();
        const float x1 = area.getX() + (float) ((e - t0) / span) * area.getWidth();
        const int range = juce::jmax (12, hi[n.part] - lo[n.part] + 1);
        const float y = area.getY() + (n.part - 1) * laneH + laneH - 3.0f - (float) (n.pitch - lo[n.part] + 1) / (float) range * (laneH - 6.0f);
        g.setColour (partColour (n.part).withAlpha (0.35f + 0.65f * n.velocity / 127.0f));
        g.fillRoundedRectangle (x0, y, juce::jmax (2.0f, x1 - x0 - 1.0f), juce::jmax (2.0f, laneH / 14.0f), 1.5f);
    };
    for (auto& n : hist) drawNote (n);
    g.setColour (Theme::accent2);
    g.drawVerticalLine ((int) area.getRight() - 1, area.getY(), area.getBottom());
    if (! state.engine.isPlaying())
    {
        g.setColour (Theme::textDim);
        g.setFont (Theme::font (14.0f));
        g.drawText ("Press PLAY (Space) - the fractal starts composing", area, juce::Justification::centred);
    }
}

// ============================================================================ part row
LivePartRow::LivePartRow (AppState& s, juce::ValueTree n, juce::Colour c)
    : node (n), state (s), colour (c), on (n[IDs::name].toString(), c)
{
    on.bind (node, IDs::liveOn, &state.undo);
    on.setTooltip ("Turn this part on or off");
    addAndMakeVisible (on);
    density.setSliderStyle (juce::Slider::LinearBar);
    density.setRange (0.0, 1.0, 0.01);
    density.setTextValueSuffix (" density");
    density.setNumDecimalPlacesToDisplay (2);
    density.setColour (juce::Slider::trackColourId, c.withAlpha (0.45f));
    density.getValueObject().referTo (node.getPropertyAsValue (IDs::density, &state.undo));
    density.onDragStart = [this] { state.beginEdit ("Density"); };
    density.setTooltip ("How many notes this part plays");
    addAndMakeVisible (density);
    octave.bind (node, IDs::octave, &state.undo);
    channel.bind (node, IDs::midiCh, &state.undo);
    octave.box.setTooltip ("Octave the scale is mapped into");
    channel.box.setTooltip ("MIDI channel for MIDI out");
    addAndMakeVisible (octave);
    addAndMakeVisible (channel);
}

void LivePartRow::paint (juce::Graphics& g)
{
    g.setColour (colour);
    g.fillRect (getLocalBounds().removeFromLeft (3).reduced (0, 4));
}

void LivePartRow::resized()
{
    auto r = getLocalBounds().reduced (6, 3);
    on.setBounds (r.removeFromLeft (74)); r.removeFromLeft (6);
    channel.setBounds (r.removeFromRight (66)); r.removeFromRight (4);
    octave.setBounds (r.removeFromRight (64)); r.removeFromRight (6);
    density.setBounds (r);
}

// ============================================================================ page
LivePage::LivePage (AppState& s, Composer& c) : state (s), composer (c), canvas (s, c), notes (s, c)
{
    addAndMakeVisible (canvas);
    addAndMakeVisible (notes);
    canvas.onViewChanged = [this] { viewChanged(); };

    liveOn.setTooltip ("Live generative mode: the fractal plays the tracks");
    addAndMakeVisible (liveOn);
    playBtn.setColour (juce::TextButton::buttonOnColourId, Theme::good);
    playBtn.onClick = [this] { state.engine.togglePlay(); };
    addAndMakeVisible (playBtn);

    root.box.addItemList (Scales::rootNames(), 1);
    auto scales = Scales::names();
    scale.box.addItemList (scales, 1);
    addAndMakeVisible (root);
    addAndMakeVisible (scale);
    drift.setColour (Theme::accent);
    complexity.setColour (Theme::accent3);
    drift.slider.setTooltip ("How fast the probe wanders through the fractal (0 = stay put)");
    complexity.slider.setTooltip ("Simple / consonant  <->  dense / colourful");
    addAndMakeVisible (drift);
    addAndMakeVisible (complexity);

    midiOut.onChange = [this]
    {
        const int i = midiOut.getSelectedItemIndex();
        const auto id = i > 0 ? midiIds[i - 1] : juce::String();
        if (! state.engine.setMidiOutput (id) && state.setStatus) state.setStatus ("Could not open MIDI output");
    };
    midiOut.setTooltip ("Send the live parts to another program (e.g. FL Studio through loopMIDI)");
    addAndMakeVisible (midiOut);

    auto capture = [this] (int bars)
    {
        const int n = composer.captureLive (bars);
        if (state.setStatus)
            state.setStatus (n > 0 ? "Captured " + juce::String (bars) + " bars into " + juce::String (n) + " clips at the end of the Arrangement"
                                   : "Nothing to capture yet - let it play for a few bars");
    };
    capture8.onClick = [capture] { capture (8); };
    capture16.onClick = [capture] { capture (16); };
    capture8.setTooltip ("Keep what you just heard: copies the last bars into the Arrangement as MIDI clips");
    resetView.onClick = [this] { canvas.setView (-0.75, 0.1, 0.6, true); composer.setProbe (-0.7436, 0.1318, 0.15, true); };
    for (auto* b : { &capture8, &capture16, &resetView }) addAndMakeVisible (b);

    info.setFont (Theme::mono (12.0f));
    info.setJustificationType (juce::Justification::topLeft);
    addAndMakeVisible (info);
    info.setTooltip ("Data -> MIDI (always in key): pitch <- orbit angle of z = z^2 + c snapped to the scale; velocity <- orbit radius; "
                     "riffs <- the orbit's cycle inside a bulb; rhythm <- escape speed / period / edge distance; harmony <- orbit sampled per bar; "
                     "lead <- 2-bar phrases (A A' B A''); break <- the break's slices re-ordered by the orbit. Click the fractal to move the probe.");

    // ---- style + sound controls
    styleBox.addItemList (Model::liveStyleNames(), 1);
    styleBox.setTooltip ("IDM: glitchy beats + melody.  Ambient: drones, slow pads, bells, a grain cloud, almost no drums.  Breakbeat: a chopped break at 170.");
    styleBox.onChange = [this]
    {
        if (! project.isValid()) return;
        const int st = styleBox.getSelectedItemIndex();
        if (st == (int) live.getProperty (IDs::style, 0)) return;
        state.beginEdit ("Style: " + styleBox.getText());
        Model::applyLiveStyle (project, st, &state.undo);
        refreshKitBox();
        if (state.setStatus) state.setStatus ("Style " + styleBox.getText() + ": tempo, sounds and parts changed - everything is still editable");
    };
    styleL.setText ("Style", juce::dontSendNotification);
    kitL.setText ("Drum kit", juce::dontSendNotification);
    for (auto* l : { &styleL, &kitL }) { l->setFont (Theme::font (11.0f)); l->setColour (juce::Label::textColourId, Theme::textDim); addAndMakeVisible (l); }
    tempo.setSliderStyle (juce::Slider::LinearBar);
    tempo.setRange (40.0, 220.0, 1.0);
    tempo.setTextValueSuffix (" BPM");
    tempo.setColour (juce::Slider::trackColourId, Theme::accent.withAlpha (0.25f));
    tempo.onDragStart = [this] { state.beginEdit ("Tempo"); };
    kitBox.addItemList (SampleLib::kitNames(), 1);
    kitBox.setTooltip ("Drum kit for the Drums part. Real samples come from your sample folder (any pack works: files are sorted by name)");
    kitBox.onChange = [this]
    {
        auto t = drumTrack();
        if (! t.isValid() || kitBox.getText() == t.getProperty (IDs::kit, "Synth").toString()) return;
        if (kitBox.getText() != "Synth" && SampleLib::get().total() == 0)
        {
            if (state.setStatus) state.setStatus ("No samples found in " + SampleLib::get().folder.getFullPathName() + " - press 'Get free drums' or pick your own sample folder");
            refreshKitBox();
            return;
        }
        state.beginEdit ("Drum kit");
        Model::applyKit (t, kitBox.getText(), (juce::uint32) juce::Random::getSystemRandom().nextInt (1000), &state.undo);
    };
    samplesBtn.setTooltip ("Pick the folder with your drum samples (e.g. your Splice or FL Studio packs). Sub-folders are scanned too");
    samplesBtn.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser> ("Choose your drum sample folder", SampleLib::get().folder);
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories, [this] (const juce::FileChooser& fc)
        {
            if (! fc.getResult().isDirectory()) return;
            SampleLib::setFolder (fc.getResult());
            if (state.setStatus) state.setStatus ("Found " + juce::String (SampleLib::get().total()) + " drum samples in " + fc.getResult().getFullPathName());
            if (SampleLib::get().total() > 0 && drumTrack().isValid()) { state.beginEdit ("Drum kit"); Model::applyKit (drumTrack(), kitBox.getText() == "Synth" ? "Studio" : kitBox.getText(), 1, &state.undo); }
            refreshKitBox();
        });
    };
    downloadBtn.setTooltip ("Downloads ~150 free (CC0 / public domain) drum samples - 808, TR-8, studio and trap kits - into the sample folder (about 5 MB)");
    downloadBtn.onClick = [this]
    {
        downloadBtn.setEnabled (false);
        const auto folder = SampleLib::get().folder;
        SampleLib::downloadFreeLibrary (folder,
            [this] (float p) { downloadBtn.setButtonText ("Downloading " + juce::String ((int) (p * 100)) + "%"); },
            [this, folder] (bool ok, juce::String msg)
            {
                downloadBtn.setEnabled (true);
                downloadBtn.setButtonText ("Get free drums");
                if (state.setStatus) state.setStatus (msg);
                if (! ok) return;
                SampleLib::setFolder (folder);
                if (drumTrack().isValid()) { state.beginEdit ("Drum kit"); Model::applyKit (drumTrack(), kitBox.getText() == "Synth" ? "TR-8" : kitBox.getText(), 1, &state.undo); }
                refreshKitBox();
            });
    };
    swing.slider.setTooltip ("Swing: pushes the off-beat 16ths late");
    space.slider.setTooltip ("Space: how much reverb the live parts get");
    repeat.slider.setTooltip ("Repeat: how often the lead comes back to its motif (high = catchy, low = always new)");
    swing.setColour (Theme::accent2);
    space.setColour (Theme::good);
    repeat.setColour (Theme::accent3);
    for (auto* c : std::initializer_list<juce::Component*> { &styleBox, &kitBox, &tempo, &swing, &space, &repeat, &chordSpeed, &samplesBtn, &downloadBtn })
        addAndMakeVisible (c);
    refreshMidiOuts();
    startTimerHz (30);
}

LivePage::~LivePage() { if (project.isValid()) project.removeListener (this); }

void LivePage::refreshMidiOuts()
{
    midiIds.clear();
    midiOut.clear (juce::dontSendNotification);
    midiOut.addItem ("MIDI out: off", 1);
    int i = 2;
    const auto current = state.engine.getMidiOutputId();
    int sel = 1;
    for (auto& d : juce::MidiOutput::getAvailableDevices())
    {
        midiIds.add (d.identifier);
        midiOut.addItem ("MIDI out: " + d.name, i);
        if (d.identifier == current) sel = i;
        ++i;
    }
    midiOut.setSelectedId (sel, juce::dontSendNotification);
}

juce::ValueTree LivePage::drumTrack() const
{
    for (auto t : Model::tracks (project))
        if ((int) t.getProperty (IDs::livePart, 0) == (int) Live::Drums && t[IDs::type].toString() == "drums") return t;
    return {};
}

void LivePage::refreshKitBox()
{
    const auto k = drumTrack().getProperty (IDs::kit, "Synth").toString();
    const int i = SampleLib::kitNames().indexOf (k);
    kitBox.setSelectedItemIndex (juce::jmax (0, i), juce::dontSendNotification);
    downloadBtn.setVisible (SampleLib::get().total() == 0);
}

void LivePage::valueTreePropertyChanged (juce::ValueTree& t, const juce::Identifier& p)
{
    if (t == live && p == IDs::space) Model::applySpace (project, (float) live.getProperty (IDs::space, 0.35f), nullptr);
    if (t == live && p == IDs::style) styleBox.setSelectedItemIndex ((int) live.getProperty (IDs::style, 0), juce::dontSendNotification);
    if (t.hasType (IDs::TRACK) && p == IDs::kit) refreshKitBox();
}

void LivePage::setProject (juce::ValueTree p)
{
    if (project.isValid()) project.removeListener (this);
    project = p;
    project.addListener (this);
    live = Model::liveNode (project);
    liveOn.bind (live, IDs::liveOn, &state.undo);
    root.bind (live, IDs::root, &state.undo);
    scale.bind (live, IDs::scale, &state.undo);
    drift.bind (live, IDs::drift, &state.undo);
    complexity.bind (live, IDs::complexity, &state.undo);
    swing.bind (live, IDs::swing, &state.undo);
    space.bind (live, IDs::space, &state.undo);
    repeat.bind (live, IDs::repeat, &state.undo);
    chordSpeed.bind (live, IDs::chordSpeed, &state.undo);
    tempo.getValueObject().referTo (project.getPropertyAsValue (IDs::tempo, &state.undo));
    styleBox.setSelectedItemIndex ((int) live.getProperty (IDs::style, 0), juce::dontSendNotification);
    refreshKitBox();
    rows.clear();
    for (auto lp : live)
        if (lp.hasType (IDs::LPART)) addAndMakeVisible (rows.add (new LivePartRow (state, lp, partColour ((int) lp[IDs::id]))));
    // read the saved view first: the canvas callbacks below write the view back into the project
    auto fr = project.getChildWithName (IDs::FRACTAL);
    const double cx = fr.getProperty (IDs::cx, -0.745), cy = fr.getProperty (IDs::cy, 0.125), sc = fr.getProperty (IDs::scaleF, 0.12);
    const double px = composer.probeX(), py = composer.probeY();
    canvas.setPalette ((int) fr.getProperty (IDs::palette, 0));
    canvas.setView (cx, cy, sc, true);
    composer.setProbe (px, py, sc * 0.25, true);
    resized();
}

void LivePage::viewChanged()
{
    auto v = canvas.getView();
    composer.setViewBounds (v.cx, v.cy, v.scale);
    if (project.isValid())
    {
        auto fr = project.getChildWithName (IDs::FRACTAL);
        fr.setProperty (IDs::cx, v.cx, nullptr); fr.setProperty (IDs::cy, v.cy, nullptr);
        fr.setProperty (IDs::scaleF, v.scale, nullptr);
    }
    // keep the probe's neighbourhood proportional to the zoom (zooming in = finer detail)
    composer.setProbe (composer.probeX(), composer.probeY(), v.scale * 0.25);
}

void LivePage::timerCallback()
{
    notes.repaint();
    playBtn.setToggleState (state.engine.isPlaying(), juce::dontSendNotification);
    playBtn.setButtonText (state.engine.isPlaying() ? "STOP" : "PLAY");
    if (! project.isValid()) return;
    const auto& f = state.features;
    const int rootPc = (int) live.getProperty (IDs::root, 9);
    const auto degs = Scales::degrees ((int) live.getProperty (IDs::scale, 1), (int) live.getProperty (IDs::customMask, 0xfff));
    const int cd = state.engine.getLive().currentChord.load();
    const int chordPc = degs.empty() ? rootPc : (rootPc + degs[(size_t) (cd % (int) degs.size())]) % 12;
    static const char* roman[] = { "I", "II", "III", "IV", "V", "VI", "VII" };
    juce::String t;
    t << "Key    " << Scales::rootNames()[rootPc] << " " << Scales::name ((int) live.getProperty (IDs::scale, 1)) << "\n"
      << "Chord  " << roman[cd % 7] << "  (" << Scales::rootNames()[chordPc] << ")\n"
      << "Probe  " << juce::String (composer.probeX(), 6) << ", " << juce::String (composer.probeY(), 6) << "\n"
      << (f.inside ? "inside the set" : "outside the set") << (f.period > 0 ? "   period " + juce::String (f.period) + " riff" : juce::String()) << "\n"
      << "edge " << juce::String (f.boundary, 2) << "   chaos " << juce::String (f.chaos, 2) << "   entropy " << juce::String (f.entropy, 2);
    info.setText (t, juce::dontSendNotification);
}

void LivePage::paint (juce::Graphics& g)
{
    g.fillAll (Theme::bg);
    auto r = getLocalBounds().reduced (6);
    r.removeFromBottom (156);
    Theme::drawPanel (g, r.removeFromRight (430).toFloat(), "Live");
}

void LivePage::resized()
{
    auto r = getLocalBounds().reduced (6);
    notes.setBounds (r.removeFromBottom (150));
    r.removeFromBottom (6);
    auto side = r.removeFromRight (430).reduced (12, 8);
    r.removeFromRight (6);
    canvas.setBounds (r);

    side.removeFromTop (18);
    auto top = side.removeFromTop (30);
    playBtn.setBounds (top.removeFromLeft (100)); top.removeFromLeft (6);
    liveOn.setBounds (top.removeFromLeft (70)); top.removeFromLeft (6);
    resetView.setBounds (top);
    side.removeFromTop (6);
    auto styleRow = side.removeFromTop (26);
    styleL.setBounds (styleRow.removeFromLeft (38));
    styleBox.setBounds (styleRow.removeFromLeft (130)); styleRow.removeFromLeft (8);
    tempo.setBounds (styleRow);
    side.removeFromTop (4);
    auto keyRow = side.removeFromTop (42);
    root.setBounds (keyRow.removeFromLeft (70)); keyRow.removeFromLeft (6);
    scale.setBounds (keyRow.removeFromLeft (150)); keyRow.removeFromLeft (6);
    chordSpeed.setBounds (keyRow);
    side.removeFromTop (2);
    auto knobs = side.removeFromTop (70);
    const int kw = knobs.getWidth() / 5;
    for (auto* k : { &drift, &complexity, &swing, &space, &repeat }) k->setBounds (knobs.removeFromLeft (kw));
    side.removeFromTop (2);
    auto kitRow = side.removeFromTop (26);
    kitL.setBounds (kitRow.removeFromLeft (56));
    kitBox.setBounds (kitRow.removeFromLeft (110)); kitRow.removeFromLeft (6);
    samplesBtn.setBounds (kitRow.removeFromLeft (118)); kitRow.removeFromLeft (4);
    downloadBtn.setBounds (kitRow);
    side.removeFromTop (6);
    for (auto* row : rows) row->setBounds (side.removeFromTop (28));
    side.removeFromTop (6);
    auto mo = side.removeFromTop (26);
    midiOut.setBounds (mo);
    side.removeFromTop (6);
    auto cap = side.removeFromTop (26);
    capture8.setBounds (cap.removeFromLeft (cap.getWidth() / 2).reduced (2, 0));
    capture16.setBounds (cap.reduced (2, 0));
    side.removeFromTop (6);
    info.setBounds (side);
}
