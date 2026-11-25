#include "LivePage.h"
#include "../Core/Scales.h"
#include "../Live/LiveSequencer.h"

static juce::Colour partColour (int part)
{
    static const juce::uint32 c[] = { 0xff7d869a, 0xffff6b9d, 0xffffc85a, 0xffb38cff, 0xff5ad1ff, 0xff6bffb8 };
    return juce::Colour (c[juce::jlimit (0, 5, part)]);
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
    mapping.setFont (Theme::font (11.5f));
    mapping.setColour (juce::Label::textColourId, Theme::textDim);
    mapping.setJustificationType (juce::Justification::topLeft);
    mapping.setText ("Data -> MIDI mapping (always in key):\n"
                     "pitch     <- orbit angle of z = z^2 + c, snapped to the scale\n"
                     "velocity  <- orbit radius |z|\n"
                     "riffs     <- inside a bulb the orbit cycles: period p = p-note riff\n"
                     "rhythm    <- escape speed, period, edge distance (euclidean)\n"
                     "harmony   <- orbit sampled once per bar -> chord in the scale\n"
                     "Click the fractal to move the probe. Wheel = zoom, drag = pan.", juce::dontSendNotification);
    addAndMakeVisible (mapping);
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
    r.removeFromBottom (176);
    Theme::drawPanel (g, r.removeFromRight (380).toFloat(), "Live");
}

void LivePage::resized()
{
    auto r = getLocalBounds().reduced (6);
    notes.setBounds (r.removeFromBottom (170));
    r.removeFromBottom (6);
    auto side = r.removeFromRight (380).reduced (12, 10);
    r.removeFromRight (6);
    canvas.setBounds (r);

    side.removeFromTop (18);
    auto top = side.removeFromTop (34);
    playBtn.setBounds (top.removeFromLeft (110)); top.removeFromLeft (6);
    liveOn.setBounds (top.removeFromLeft (90)); top.removeFromLeft (6);
    resetView.setBounds (top);
    side.removeFromTop (8);
    auto keyRow = side.removeFromTop (44);
    root.setBounds (keyRow.removeFromLeft (80)); keyRow.removeFromLeft (8);
    scale.setBounds (keyRow);
    side.removeFromTop (4);
    auto knobs = side.removeFromTop (78);
    drift.setBounds (knobs.removeFromLeft (knobs.getWidth() / 2));
    complexity.setBounds (knobs);
    side.removeFromTop (4);
    for (auto* row : rows) row->setBounds (side.removeFromTop (32));
    side.removeFromTop (8);
    midiOut.setBounds (side.removeFromTop (26));
    side.removeFromTop (6);
    auto cap = side.removeFromTop (28);
    capture8.setBounds (cap.removeFromLeft (cap.getWidth() / 2).reduced (2, 0));
    capture16.setBounds (cap.reduced (2, 0));
    side.removeFromTop (8);
    info.setBounds (side.removeFromTop (84));
    mapping.setBounds (side);
}
