#include "DrumPage.h"
#include "../Generative/Generator.h"

static const juce::StringArray rateNames { "1/4", "1/8", "1/8 T", "1/16", "1/16 x5", "1/16 T", "1/16 x7", "1/32" };

// ============================================================================ StepGrid
StepGrid::StepGrid (AppState& s) : state (s) { startTimerHz (20); }
StepGrid::~StepGrid() { if (clip.isValid()) clip.removeListener (this); }

void StepGrid::setClip (juce::ValueTree c, juce::ValueTree t)
{
    if (clip.isValid()) clip.removeListener (this);
    clip = c; track = t;
    if (clip.isValid()) clip.addListener (this);
    rebuildRowControls();
    repaint();
}

void StepGrid::rebuildRowControls()
{
    stepSliders.clear(); rateBoxes.clear();
    if (! clip.isValid()) return;
    for (int pad = 0; pad < PadP::numPads; ++pad)
    {
        auto lane = Model::laneFor (clip, pad);
        auto* s = stepSliders.add (new juce::Slider (juce::Slider::IncDecButtons, juce::Slider::TextBoxLeft));
        s->setRange (1, 64, 1);
        s->setTextBoxStyle (juce::Slider::TextBoxLeft, false, 30, 18);
        s->setIncDecButtonsMode (juce::Slider::incDecButtonsDraggable_Vertical);
        s->setValue ((int) lane.getProperty (IDs::steps, 16), juce::dontSendNotification);
        s->setTooltip ("Lane length in steps (different lengths = polyrhythm)");
        s->onValueChange = [this, pad, s]
        {
            auto ln = Model::laneFor (clip, pad);
            const int n = (int) s->getValue();
            auto steps = Model::readSteps (ln);
            steps.resize ((size_t) n);
            state.beginEdit ("Lane length");
            ln.setProperty (IDs::steps, n, &state.undo);
            Model::writeSteps (ln, steps, &state.undo);
        };
        addAndMakeVisible (s);
        auto* r = rateBoxes.add (new juce::ComboBox());
        r->addItemList (rateNames, 1);
        r->setSelectedId ((int) lane.getProperty (IDs::rate, 4), juce::dontSendNotification);
        r->setTooltip ("Step rate: tuplets give irregular subdivisions");
        r->onChange = [this, pad, r] { state.beginEdit ("Lane rate"); Model::laneFor (clip, pad).setProperty (IDs::rate, r->getSelectedId(), &state.undo); };
        addAndMakeVisible (r);
    }
    resized();
}

juce::Rectangle<int> StepGrid::rowBounds (int pad) const
{
    const int h = getHeight() / PadP::numPads;
    return { 0, pad * h, getWidth(), h };
}

juce::Rectangle<int> StepGrid::cellsArea (int pad) const { return rowBounds (pad).withTrimmedLeft (nameW + ctrlW).reduced (2, 2); }

int StepGrid::maxSteps() const
{
    int m = 16;
    for (auto lane : Model::patternOf (clip)) m = juce::jmax (m, (int) lane[IDs::steps]);
    return m;
}

void StepGrid::resized()
{
    for (int pad = 0; pad < stepSliders.size(); ++pad)
    {
        auto r = rowBounds (pad).withTrimmedLeft (nameW).withWidth (ctrlW).reduced (2, 3);
        stepSliders[pad]->setBounds (r.removeFromLeft (78));
        rateBoxes[pad]->setBounds (r.reduced (2, 0));
    }
}

void StepGrid::paint (juce::Graphics& g)
{
    g.fillAll (Theme::panel);
    if (! clip.isValid())
    {
        g.setColour (Theme::textDim);
        g.drawText ("No pattern clip: press 'New pattern' or double-click a drum track in the Arrangement", getLocalBounds(), juce::Justification::centred);
        return;
    }
    // refresh row controls if the lane properties changed externally (generator / undo)
    for (int pad = 0; pad < stepSliders.size(); ++pad)
    {
        auto lane = Model::laneFor (clip, pad);
        if ((int) stepSliders[pad]->getValue() != (int) lane[IDs::steps] && ! stepSliders[pad]->isMouseButtonDown()) stepSliders[pad]->setValue ((int) lane[IDs::steps], juce::dontSendNotification);
        if (rateBoxes[pad]->getSelectedId() != (int) lane[IDs::rate]) rateBoxes[pad]->setSelectedId ((int) lane[IDs::rate], juce::dontSendNotification);
    }
    const int ms = maxSteps();
    const double beat = state.engine.getPosition() - (double) clip[IDs::start];
    const bool inClip = state.engine.isPlaying() && beat >= 0 && beat < (double) clip[IDs::length];
    auto kit = track.getChildWithName (IDs::DRUMKIT);
    for (int pad = 0; pad < PadP::numPads; ++pad)
    {
        auto row = rowBounds (pad);
        auto lane = Model::laneFor (clip, pad);
        const auto steps = Model::readSteps (lane);
        const int n = (int) steps.size();
        const int rate = juce::jmax (1, (int) lane[IDs::rate]);
        g.setColour (pad == selectedPad ? Theme::panelLight : (pad % 2 ? Theme::panel : Theme::panel.brighter (0.02f)));
        g.fillRect (row);
        g.setColour (pad == selectedPad ? Theme::accent : Theme::text);
        g.setFont (Theme::font (12.0f, pad == selectedPad));
        g.drawText (kit.getChild (pad)[IDs::name].toString(), row.withWidth (nameW).reduced (8, 0), juce::Justification::centredLeft);
        auto cells = cellsArea (pad);
        const float cw = (float) cells.getWidth() / ms;
        const int current = inClip ? ((int) std::floor (beat * rate)) % juce::jmax (1, n) : -1;
        for (int k = 0; k < n; ++k)
        {
            auto cr = juce::Rectangle<float> (cells.getX() + k * cw, (float) cells.getY(), cw, (float) cells.getHeight()).reduced (1.5f);
            const auto& st = steps[(size_t) k];
            const bool groupStart = (k % 4) == 0;
            g.setColour (groupStart ? Theme::outline.brighter (0.1f) : Theme::outline);
            g.fillRoundedRectangle (cr, 2.5f);
            if (st.velocity > 0)
            {
                const float alpha = 0.3f + 0.7f * st.probability / 100.0f;
                auto vr = cr.withTrimmedTop (cr.getHeight() * (1.0f - st.velocity / 127.0f));
                g.setColour ((pad == selectedPad ? Theme::accent : Theme::accent3).withAlpha (alpha));
                g.fillRoundedRectangle (vr, 2.5f);
                if (st.ratchet > 1)
                {
                    g.setColour (Theme::bg);
                    for (int r = 1; r < st.ratchet; ++r) g.drawVerticalLine ((int) (cr.getX() + cr.getWidth() * r / st.ratchet), cr.getY(), cr.getBottom());
                }
                if (st.probability < 100 && cw > 22)
                {
                    g.setColour (Theme::text);
                    g.setFont (Theme::font (9.0f));
                    g.drawText (juce::String (st.probability), cr, juce::Justification::centredTop);
                }
            }
            if (k == current) { g.setColour (Theme::accent2); g.drawRoundedRectangle (cr, 2.5f, 1.5f); }
        }
    }
}

bool StepGrid::cellAt (juce::Point<int> p, int& pad, int& step) const
{
    pad = juce::jlimit (0, PadP::numPads - 1, p.y / juce::jmax (1, getHeight() / PadP::numPads));
    auto cells = cellsArea (pad);
    if (! cells.contains (p)) return false;
    step = (int) ((p.x - cells.getX()) / ((float) cells.getWidth() / maxSteps()));
    return step < (int) Model::laneFor (clip, pad).getProperty (IDs::steps, 16);
}

void StepGrid::editStep (int pad, int step, std::function<void (StepData&)> fn)
{
    auto lane = Model::laneFor (clip, pad);
    auto steps = Model::readSteps (lane);
    if (! juce::isPositiveAndBelow (step, (int) steps.size())) return;
    fn (steps[(size_t) step]);
    Model::writeSteps (lane, steps, &state.undo);
}

void StepGrid::mouseDown (const juce::MouseEvent& e)
{
    if (! clip.isValid()) return;
    int pad, step;
    const auto p = e.getPosition();
    if (p.x < nameW)
    {
        pad = juce::jlimit (0, PadP::numPads - 1, p.y / juce::jmax (1, getHeight() / PadP::numPads));
        selectedPad = pad;
        if (onPadSelected) onPadSelected (pad);
        if (track.isValid()) { state.engine.sendLiveNote (track[IDs::id], PadP::baseNote + pad, 0.9f); state.engine.sendLiveNote (track[IDs::id], PadP::baseNote + pad, 0.0f); }
        repaint();
        return;
    }
    if (! cellAt (p, pad, step)) return;
    const auto st = Model::readSteps (Model::laneFor (clip, pad))[(size_t) step];
    if (e.mods.isPopupMenu())
    {
        juce::PopupMenu m, prob, rat;
        for (int v : { 100, 90, 75, 50, 33, 25, 10 }) prob.addItem (100 + v, juce::String (v) + "%", true, st.probability == v);
        for (int r = 1; r <= 4; ++r) rat.addItem (200 + r, r == 1 ? juce::String ("off") : "x" + juce::String (r), true, st.ratchet == r);
        m.addSubMenu ("Probability", prob);
        m.addSubMenu ("Ratchet", rat);
        m.addItem (300, "Fill lane every 2"); m.addItem (301, "Fill lane every 4"); m.addItem (302, "Clear lane"); m.addItem (303, "Shift lane right"); m.addItem (304, "Randomise probability");
        m.showMenuAsync (juce::PopupMenu::Options(), [this, pad, step] (int r)
        {
            if (r == 0) return;
            state.beginEdit ("Edit steps");
            if (r >= 100 && r <= 200) editStep (pad, step, [r] (StepData& s) { s.probability = r - 100; if (s.velocity == 0) s.velocity = 100; });
            else if (r > 200 && r < 300) editStep (pad, step, [r] (StepData& s) { s.ratchet = r - 200; if (s.velocity == 0) s.velocity = 100; });
            else
            {
                auto lane = Model::laneFor (clip, pad);
                auto steps = Model::readSteps (lane);
                juce::Random rnd ((juce::int64) step * 31 + pad);
                for (size_t k = 0; k < steps.size(); ++k)
                {
                    if (r == 300) steps[k].velocity = (k % 2 == 0) ? 100 : 0;
                    if (r == 301) steps[k].velocity = (k % 4 == 0) ? 110 : 0;
                    if (r == 302) steps[k] = {};
                    if (r == 304 && steps[k].velocity > 0) steps[k].probability = 30 + rnd.nextInt (71);
                }
                if (r == 303 && ! steps.empty()) std::rotate (steps.rbegin(), steps.rbegin() + 1, steps.rend());
                Model::writeSteps (lane, steps, &state.undo);
            }
        });
        return;
    }
    state.beginEdit ("Edit steps");
    if (e.mods.isShiftDown() && st.velocity > 0)
    {
        static const int cycle[] = { 100, 75, 50, 25 };
        int next = 100;
        for (int i = 0; i < 4; ++i) if (st.probability >= cycle[i]) { next = cycle[(i + 1) % 4]; break; }
        editStep (pad, step, [next] (StepData& s) { s.probability = next; });
        return;
    }
    if (e.mods.isAltDown() && st.velocity > 0) { editStep (pad, step, [] (StepData& s) { s.ratchet = s.ratchet % 4 + 1; }); return; }
    dragPad = pad; dragStep = step;
    dragOn = st.velocity == 0;
    dragPaint = true;
    dragVel = st.velocity > 0 ? st.velocity : 100;
    editStep (pad, step, [this] (StepData& s) { s = dragOn ? StepData { 100, 100, 1 } : StepData {}; });
    selectedPad = pad;
    if (onPadSelected) onPadSelected (pad);
}

void StepGrid::mouseDrag (const juce::MouseEvent& e)
{
    if (! clip.isValid() || dragPad < 0) return;
    if (! dragOn && std::abs (e.getDistanceFromDragStartY()) > 3 && std::abs (e.getDistanceFromDragStartX()) < 6)
    {
        // vertical drag on an existing step = velocity (re-enable it)
        const int v = juce::jlimit (1, 127, dragVel - e.getDistanceFromDragStartY());
        editStep (dragPad, dragStep, [v] (StepData& s) { s.velocity = v; if (s.probability <= 0) s.probability = 100; });
        return;
    }
    int pad, step;
    if (cellAt (e.getPosition(), pad, step) && pad == dragPad && step != dragStep)
    {
        dragStep = step;
        editStep (pad, step, [this] (StepData& s) { s = dragOn ? StepData { 100, 100, 1 } : StepData {}; });
    }
}

void StepGrid::mouseUp (const juce::MouseEvent&) { dragPad = -1; dragPaint = false; }

// ============================================================================ PadWaveform
void PadWaveform::paint (juce::Graphics& g)
{
    g.setColour (Theme::bg);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 4.0f);
    if (data == nullptr) return;
    const double len = data->numFrames() / data->sampleRate;
    WaveformCache::draw (g, *data, getLocalBounds().toFloat().reduced (2), 0.0, len, Theme::accent.withAlpha (0.8f));
    const float x0 = getWidth() * juce::jmin (start, end), x1 = getWidth() * juce::jmax (start, end);
    g.setColour (Theme::bg.withAlpha (0.65f));
    g.fillRect (0.0f, 0.0f, x0, (float) getHeight());
    g.fillRect (x1, 0.0f, getWidth() - x1, (float) getHeight());
    g.setColour (Theme::warn);
    g.drawVerticalLine ((int) x0, 0, (float) getHeight());
    g.drawVerticalLine ((int) x1, 0, (float) getHeight());
}

// ============================================================================ DrumPage
DrumPage::DrumPage (AppState& s, Composer& c) : state (s), composer (c), grid (s)
{
    title.setFont (Theme::font (15.0f, true));
    addAndMakeVisible (title);
    trackBox.onChange = [this] { bindTrack (Model::findTrack (project, trackBox.getSelectedId() - 1)); };
    clipBox.onChange = [this]
    {
        auto clips = track.getChildWithName (IDs::CLIPS);
        int i = 1;
        for (auto c2 : clips) if (c2[IDs::type].toString() == "pattern") { if (i++ == clipBox.getSelectedId()) { state.selectClip (c2); return; } }
    };
    addAndMakeVisible (trackBox);
    addAndMakeVisible (clipBox);
    newPattern.onClick = [this]
    {
        if (! track.isValid()) return;
        state.beginEdit ("New pattern");
        const double bpb = state.beatsPerBar();
        const double at = std::floor (state.engine.getPosition() / bpb) * bpb;
        state.selectClip (Model::addPatternClip (track, at, bpb * 2, &state.undo));
    };
    genBreak.setColour (juce::TextButton::buttonColourId, Theme::accent.withAlpha (0.3f));
    genBreak.onClick = [this] { generateBreak (false); };
    mutateBreak.onClick = [this] { generateBreak (true); };
    clearPat.onClick = [this]
    {
        if (! clip.isValid()) return;
        state.beginEdit ("Clear pattern");
        for (auto lane : Model::patternOf (clip)) Model::writeSteps (lane, std::vector<StepData> ((size_t) (int) lane[IDs::steps]), &state.undo);
    };
    for (auto* b : { &newPattern, &genBreak, &mutateBreak, &clearPat }) addAndMakeVisible (b);
    auto setupSlider = [this] (juce::Slider& sl, juce::Label& l, const juce::String& text, double def)
    {
        sl.setSliderStyle (juce::Slider::LinearHorizontal);
        sl.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        sl.setRange (0.0, 1.0);
        sl.setValue (def, juce::dontSendNotification);
        l.setText (text, juce::dontSendNotification);
        l.setFont (Theme::font (11.0f));
        l.setColour (juce::Label::textColourId, Theme::textDim);
        addAndMakeVisible (sl); addAndMakeVisible (l);
    };
    setupSlider (swing, swingL, "Swing", 0.0);
    swing.setRange (0.0, 0.75);
    swing.onDragStart = [this] { state.beginEdit ("Swing"); };
    swing.onValueChange = [this] { if (clip.isValid()) Model::patternOf (clip).setProperty (IDs::swing, swing.getValue(), &state.undo); };
    setupSlider (genDensity, densL, "Break density", 0.55);
    setupSlider (genComplexity, compL, "Break complexity", 0.5);

    addAndMakeVisible (grid);
    grid.onPadSelected = [this] (int p) { selectPad (p); };

    for (int i = 0; i < PadP::numPads; ++i)
    {
        auto* b = pads.add (new juce::TextButton());
        b->setClickingTogglesState (false);
        b->onClick = [this, i]
        {
            selectPad (i);
            if (track.isValid()) { state.engine.sendLiveNote (track[IDs::id], PadP::baseNote + i, 0.9f); state.engine.sendLiveNote (track[IDs::id], PadP::baseNote + i, 0.0f); }
        };
        addAndMakeVisible (b);
    }
    for (auto& d : PadP::defs())
    {
        if (d.isChoice()) continue;
        auto* k = padKnobs.add (new ParamKnob (d));
        k->setColour (Theme::accent3);
        addAndMakeVisible (k);
    }
    addAndMakeVisible (reverseBox);
    addAndMakeVisible (chokeBox);
    synthKind.addItemList ({ "Synth Kick", "Synth Snare", "Synth Closed Hat", "Synth Open Hat", "Synth Clap", "Synth Low Tom", "Synth Hi Tom", "Synth Rim", "Synth Glitch", "Synth Zap", "Synth Noise", "Synth Sub" }, 1);
    synthKind.onChange = [this]
    {
        auto p = padNode (selectedPad);
        if (! p.isValid()) return;
        state.beginEdit ("Pad sound");
        p.setProperty (IDs::sample, "", &state.undo);
        p.setProperty (IDs::synthKind, synthKind.getSelectedItemIndex(), &state.undo);
        p.setProperty ("sliceStart", 0.0f, &state.undo); p.setProperty ("sliceEnd", 1.0f, &state.undo);
    };
    addAndMakeVisible (synthKind);
    loadSample.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser> ("Load drum sample", juce::File(), "*.wav;*.aif;*.aiff;*.flac");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this] (const juce::FileChooser& fc)
        {
            auto f = fc.getResult();
            auto p = padNode (selectedPad);
            if (! f.existsAsFile() || ! p.isValid()) return;
            if (AudioPool::get().load (f) == nullptr) { if (state.setStatus) state.setStatus ("Unreadable file " + f.getFileName()); return; }
            state.beginEdit ("Load sample");
            p.setProperty (IDs::sample, f.getFullPathName(), &state.undo);
            p.setProperty (IDs::name, f.getFileNameWithoutExtension().substring (0, 14), &state.undo);
            p.setProperty ("sliceStart", 0.0f, &state.undo); p.setProperty ("sliceEnd", 1.0f, &state.undo);
        });
    };
    clearSample.onClick = [this]
    {
        auto p = padNode (selectedPad);
        if (! p.isValid()) return;
        state.beginEdit ("Clear sample");
        p.setProperty (IDs::sample, "", &state.undo);
        p.setProperty ("sliceStart", 0.0f, &state.undo); p.setProperty ("sliceEnd", 1.0f, &state.undo);
    };
    slice.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser> ("Choose a break to slice across the 12 pads", juce::File(), "*.wav;*.aif;*.aiff;*.flac");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this] (const juce::FileChooser& fc) { if (fc.getResult().existsAsFile()) sliceToPads (fc.getResult()); });
    };
    for (auto* b : { &loadSample, &clearSample, &slice }) addAndMakeVisible (b);
    padTitle.setFont (Theme::font (13.0f, true));
    addAndMakeVisible (padTitle);
    addAndMakeVisible (wave);
    state.selectionChanged.addChangeListener (this);
    startTimerHz (20);
}

DrumPage::~DrumPage()
{
    state.selectionChanged.removeChangeListener (this);
    if (project.isValid()) project.removeListener (this);
}

void DrumPage::setProject (juce::ValueTree p)
{
    if (project.isValid()) project.removeListener (this);
    project = p;
    project.addListener (this);
    track = {}; clip = {};
    refresh();
}

juce::ValueTree DrumPage::padNode (int pad) const { return track.getChildWithName (IDs::DRUMKIT).getChild (pad); }

void DrumPage::refresh()
{
    trackBox.clear (juce::dontSendNotification);
    juce::ValueTree first;
    for (auto t : Model::tracks (project))
        if (t[IDs::type].toString() == "drums") { trackBox.addItem (t[IDs::name].toString(), (int) t[IDs::id] + 1); if (! first.isValid()) first = t; }
    if (! track.isValid() || ! track.getParent().isValid()) track = first;
    bindTrack (track);
}

void DrumPage::bindTrack (juce::ValueTree t)
{
    track = t;
    if (track.isValid()) trackBox.setSelectedId ((int) track[IDs::id] + 1, juce::dontSendNotification);
    // choose the clip: selected pattern clip on this track, else first pattern clip
    juce::ValueTree c;
    if (state.selectedClip.isValid() && state.selectedClip[IDs::type].toString() == "pattern" && Model::trackOfClip (state.selectedClip) == track) c = state.selectedClip;
    clipBox.clear (juce::dontSendNotification);
    int i = 1;
    for (auto cl : track.getChildWithName (IDs::CLIPS))
        if (cl[IDs::type].toString() == "pattern")
        {
            clipBox.addItem (cl[IDs::name].toString() + "  @ bar " + juce::String ((int) ((double) cl[IDs::start] / state.beatsPerBar()) + 1), i);
            if (! c.isValid()) c = cl;
            if (cl == c) clipBox.setSelectedId (i, juce::dontSendNotification);
            ++i;
        }
    clip = c;
    grid.setClip (clip, track);
    swing.setValue (clip.isValid() ? (double) Model::patternOf (clip).getProperty (IDs::swing, 0.0) : 0.0, juce::dontSendNotification);
    title.setText (track.isValid() ? "DRUM MACHINE  /  " + track[IDs::name].toString() : "No drum track - add one in the Arrangement", juce::dontSendNotification);
    for (auto* b : { &newPattern, &genBreak, &mutateBreak, &clearPat, &loadSample, &clearSample, &slice }) b->setEnabled (track.isValid());
    selectPad (selectedPad);
}

void DrumPage::selectPad (int pad)
{
    selectedPad = pad;
    grid.selectedPad = pad;
    grid.repaint();
    auto p = padNode (pad);
    for (int i = 0; i < pads.size(); ++i)
    {
        pads[i]->setButtonText (padNode (i)[IDs::name].toString());
        pads[i]->setToggleState (i == pad, juce::dontSendNotification);
    }
    if (! p.isValid()) { for (auto* k : padKnobs) k->unbind(); return; }
    int ki = 0;
    for (auto& d : PadP::defs())
        if (! d.isChoice()) padKnobs[ki++]->bind (p, d.id, &state.undo);
    reverseBox.bind (p, "reverse", &state.undo);
    chokeBox.bind (p, "choke", &state.undo);
    synthKind.setSelectedItemIndex ((int) p.getProperty (IDs::synthKind, pad), juce::dontSendNotification);
    const auto sample = p[IDs::sample].toString();
    padTitle.setText ("PAD " + juce::String (pad + 1) + "  " + p[IDs::name].toString() + "   " + (sample.isNotEmpty() ? juce::File (sample).getFileName() : juce::String ("(synthesised)")), juce::dontSendNotification);
    if (auto* tp = state.engine.getTrackProcessor (track[IDs::id]); tp != nullptr && tp->drums) wave.data = tp->drums->pad (pad).owner;
    wave.start = (float) p.getProperty ("sliceStart", 0.0f);
    wave.end = (float) p.getProperty ("sliceEnd", 1.0f);
    wave.repaint();
}

void DrumPage::valueTreePropertyChanged (juce::ValueTree& t, const juce::Identifier& prop)
{
    if (t.hasType (IDs::PAD) && t.getParent().getParent() == track)
    {
        if (prop == IDs::sample || prop == IDs::synthKind || prop == IDs::name || prop.toString().startsWith ("slice"))
            juce::MessageManager::callAsync ([sp = juce::Component::SafePointer<DrumPage> (this)] { if (sp) sp->selectPad (sp->selectedPad); });
    }
    if (t.hasType (IDs::TRACK) && prop == IDs::name) refresh();
    if (t.hasType (IDs::PATTERN) && clip.isValid() && t == Model::patternOf (clip) && prop == IDs::swing && ! swing.isMouseButtonDown())
        swing.setValue ((double) t[IDs::swing], juce::dontSendNotification);
}

void DrumPage::changeListenerCallback (juce::ChangeBroadcaster*)
{
    auto sel = state.selectedClip;
    if (sel.isValid() && sel[IDs::type].toString() == "pattern") bindTrack (Model::trackOfClip (sel));
    else if (state.selectedTrack.isValid() && state.selectedTrack[IDs::type].toString() == "drums" && state.selectedTrack != track) bindTrack (state.selectedTrack);
}

void DrumPage::timerCallback()
{
    if (auto* tp = state.engine.getTrackProcessor (track.isValid() ? (int) track[IDs::id] : -1); tp != nullptr && tp->drums)
        for (int i = 0; i < pads.size(); ++i)
        {
            const float m = tp->drums->pad (i).meter.exchange (0.0f);
            pads[i]->setColour (juce::TextButton::buttonColourId, Theme::panelLight.interpolatedWith (Theme::accent2, juce::jlimit (0.0f, 0.8f, m * 1.5f)));
        }
}

void DrumPage::generateBreak (bool mutate)
{
    if (! track.isValid()) return;
    state.beginEdit (mutate ? "Mutate break" : "Generate break");
    if (! clip.isValid())
    {
        const double bpb = state.beatsPerBar();
        clip = Model::addPatternClip (track, std::floor (state.engine.getPosition() / bpb) * bpb, bpb * 2, &state.undo);
    }
    Gen::Settings s;
    s.role = Gen::Breaks;
    s.bars = juce::jmax (1, (int) std::round ((double) clip[IDs::length] / state.beatsPerBar()));
    s.beatsPerBar = juce::jmax (1, (int) std::round (state.beatsPerBar()));
    s.density = (float) genDensity.getValue();
    s.complexity = (float) genComplexity.getValue();
    s.seed = (juce::int64) clip[IDs::id] * 7919;
    breakGeneration = mutate ? breakGeneration + 1 : 0;
    auto f = state.features;
    if (mutate)
    {
        // a mutation = same region, new generation index (deterministic variation)
        f.chaos = juce::jlimit (0.0, 1.0, f.chaos + 0.05 * breakGeneration);
    }
    auto r = Gen::generate (s, f, {}, breakGeneration);
    Gen::applyToClip (clip, r, &state.undo);
    state.selectClip (clip);
    if (state.setStatus) state.setStatus (r.summary);
}

void DrumPage::sliceToPads (const juce::File& f)
{
    auto d = AudioPool::get().load (f);
    if (d == nullptr || ! track.isValid()) return;
    // onset detection: energy flux in 256-sample hops, keep the 12 strongest onsets
    const int hop = 256, n = d->numFrames();
    std::vector<float> env;
    for (int i = 0; i + hop < n; i += hop)
    {
        float e = 0;
        for (int k = 0; k < hop; ++k) e += d->get (0, i + k) * d->get (0, i + k);
        env.push_back (std::sqrt (e / hop));
    }
    std::vector<std::pair<float, int>> onsets;
    for (size_t i = 2; i < env.size(); ++i)
    {
        const float flux = env[i] - 0.5f * (env[i - 1] + env[i - 2]);
        if (flux > 0.02f) onsets.push_back ({ flux, (int) i * hop });
    }
    std::sort (onsets.begin(), onsets.end(), [] (auto& a, auto& b) { return a.first > b.first; });
    std::vector<int> picks { 0 };
    const int minGap = (int) (d->sampleRate * 0.06);
    for (auto& o : onsets)
    {
        if ((int) picks.size() >= PadP::numPads) break;
        bool ok = true;
        for (int p : picks) ok &= std::abs (p - o.second) > minGap;
        if (ok) picks.push_back (o.second);
    }
    if ((int) picks.size() < PadP::numPads) // not enough transients: fall back to equal slices
    {
        picks.clear();
        for (int i = 0; i < PadP::numPads; ++i) picks.push_back (n * i / PadP::numPads);
    }
    std::sort (picks.begin(), picks.end());
    state.beginEdit ("Slice to pads");
    for (int i = 0; i < PadP::numPads; ++i)
    {
        auto p = padNode (i);
        const float s0 = (float) picks[(size_t) i] / n;
        const float s1 = i + 1 < PadP::numPads ? (float) picks[(size_t) i + 1] / n : 1.0f;
        p.setProperty (IDs::sample, f.getFullPathName(), &state.undo);
        p.setProperty (IDs::name, "Slice " + juce::String (i + 1), &state.undo);
        p.setProperty ("sliceStart", s0, &state.undo);
        p.setProperty ("sliceEnd", s1, &state.undo);
        p.setProperty ("decay", 4.0f, &state.undo);
        p.setProperty ("choke", 2, &state.undo);   // slices choke each other like a classic break chopper
        p.setProperty ("attack", 0.0f, &state.undo);
    }
    if (state.setStatus) state.setStatus ("Sliced " + f.getFileName() + " into 12 pads (choke group 2). Generate a breakbeat to re-sequence it.");
}

void DrumPage::paint (juce::Graphics& g)
{
    g.fillAll (Theme::bg);
    auto r = getLocalBounds().reduced (8);
    r.removeFromTop (40);
    auto bottom = r.removeFromBottom (230);
    Theme::drawPanel (g, bottom.toFloat(), "Pad");
    Theme::drawPanel (g, r.reduced (0, 4).toFloat(), "Step sequencer   click: toggle   drag up/down: velocity   shift: probability   alt: ratchet   right-click: more");
}

void DrumPage::resized()
{
    auto r = getLocalBounds().reduced (8);
    auto top = r.removeFromTop (34);
    title.setBounds (top.removeFromLeft (300));
    trackBox.setBounds (top.removeFromLeft (150).reduced (0, 3)); top.removeFromLeft (6);
    clipBox.setBounds (top.removeFromLeft (170).reduced (0, 3)); top.removeFromLeft (6);
    newPattern.setBounds (top.removeFromLeft (96).reduced (0, 2)); top.removeFromLeft (6);
    clearPat.setBounds (top.removeFromLeft (60).reduced (0, 2)); top.removeFromLeft (12);
    auto sl = [&] (juce::Slider& s, juce::Label& l, int w) { auto a = top.removeFromLeft (w); l.setBounds (a.removeFromTop (13)); s.setBounds (a); top.removeFromLeft (8); };
    sl (swing, swingL, 90);
    sl (genDensity, densL, 100);
    sl (genComplexity, compL, 110);
    genBreak.setBounds (top.removeFromLeft (140).reduced (0, 2)); top.removeFromLeft (4);
    mutateBreak.setBounds (top.removeFromLeft (70).reduced (0, 2));
    r.removeFromTop (6);

    auto bottom = r.removeFromBottom (230).reduced (10, 22);
    grid.setBounds (r.reduced (8, 26).withTrimmedTop (-6));

    auto padGrid = bottom.removeFromLeft (300);
    const int pw = padGrid.getWidth() / 4, ph = padGrid.getHeight() / 3;
    for (int i = 0; i < pads.size(); ++i)
        pads[i]->setBounds (juce::Rectangle<int> (padGrid.getX() + (i % 4) * pw, padGrid.getY() + (2 - i / 4) * ph, pw, ph).reduced (3));   // MPC layout, pad 1 bottom-left
    bottom.removeFromLeft (12);
    auto head = bottom.removeFromTop (24);
    padTitle.setBounds (head.removeFromLeft (360));
    slice.setBounds (head.removeFromRight (150)); head.removeFromRight (6);
    clearSample.setBounds (head.removeFromRight (100)); head.removeFromRight (6);
    loadSample.setBounds (head.removeFromRight (100)); head.removeFromRight (6);
    synthKind.setBounds (head.removeFromRight (150));
    bottom.removeFromTop (6);
    auto knobs = bottom.removeFromTop (80);
    for (auto* k : padKnobs) { k->setBounds (knobs.removeFromLeft (64)); }
    reverseBox.setBounds (knobs.removeFromLeft (90).reduced (4, 14));
    chokeBox.setBounds (knobs.removeFromLeft (70).reduced (4, 14));
    bottom.removeFromTop (6);
    wave.setBounds (bottom);
}
