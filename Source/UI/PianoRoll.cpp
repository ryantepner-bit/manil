#include "PianoRoll.h"
#include "../Core/Scales.h"

// ============================================================================ grid
PianoRollGrid::PianoRollGrid (AppState& s) : state (s)
{
    setWantsKeyboardFocus (true);
    startTimerHz (25);
}

PianoRollGrid::~PianoRollGrid() { if (clip.isValid()) clip.removeListener (this); }

void PianoRollGrid::setClip (juce::ValueTree c)
{
    if (clip == c) return;
    if (clip.isValid()) clip.removeListener (this);
    clip = c;
    selection.clear();
    if (clip.isValid())
    {
        clip.addListener (this);
        // centre the view on the clip's notes
        int lo = 127, hi = 0;
        for (auto n : clip) if (n.hasType (IDs::NOTE)) { lo = juce::jmin (lo, (int) n[IDs::p]); hi = juce::jmax (hi, (int) n[IDs::p]); }
        topPitch = hi >= lo ? juce::jlimit (24, 127, hi + 6) : 84;
        scrollBeat = 0.0;
    }
    repaint();
}

void PianoRollGrid::pruneSelection()
{
    for (int i = selection.size(); --i >= 0;)
        if (! selection.getReference (i).getParent().isValid()) selection.remove (i);
}

juce::Rectangle<int> PianoRollGrid::gridArea() const { return getLocalBounds().withTrimmedLeft (keyW).withTrimmedBottom (laneH); }
juce::Rectangle<int> PianoRollGrid::laneArea() const { return getLocalBounds().withTrimmedLeft (keyW).removeFromBottom (laneH); }
float PianoRollGrid::beatToX (double b) const { return (float) (keyW + (b - scrollBeat) * ppb); }
double PianoRollGrid::xToBeat (float x) const { return scrollBeat + (x - keyW) / ppb; }
float PianoRollGrid::pitchToY (int p) const { return (topPitch - p) * noteH; }
int PianoRollGrid::yToPitch (float y) const { return juce::jlimit (0, 127, topPitch - (int) std::floor (y / noteH)); }
double PianoRollGrid::snapBeat (double b) const { return gridBeats > 0 ? std::floor (b / gridBeats + 1e-9) * gridBeats : b; }

juce::Rectangle<float> PianoRollGrid::noteRect (const juce::ValueTree& n) const
{
    const float x0 = beatToX (n[IDs::s]), x1 = beatToX ((double) n[IDs::s] + (double) n[IDs::l]);
    return { x0, pitchToY (n[IDs::p]) + 1.0f, juce::jmax (3.0f, x1 - x0), noteH - 2.0f };
}

juce::ValueTree PianoRollGrid::noteAt (juce::Point<float> p, bool& onRightEdge) const
{
    onRightEdge = false;
    if (! clip.isValid()) return {};
    for (int i = clip.getNumChildren(); --i >= 0;)
    {
        auto n = clip.getChild (i);
        if (! n.hasType (IDs::NOTE)) continue;
        auto r = noteRect (n);
        if (r.expanded (0, 1).contains (p)) { onRightEdge = p.x > r.getRight() - juce::jmin (8.0f, r.getWidth() * 0.35f); return n; }
    }
    return {};
}

void PianoRollGrid::audition (int pitch, int vel)
{
    const int track = Model::trackOfClip (clip).isValid() ? (int) Model::trackOfClip (clip)[IDs::id] : -1;
    if (track < 0) return;
    if (auditionNote >= 0) state.engine.sendLiveNote (track, auditionNote, 0.0f);
    auditionNote = pitch;
    if (pitch >= 0) state.engine.sendLiveNote (track, pitch, vel / 127.0f);
}

void PianoRollGrid::paint (juce::Graphics& g)
{
    g.fillAll (Theme::bg);
    auto ga = gridArea();
    if (! clip.isValid())
    {
        g.setColour (Theme::textDim);
        g.setFont (Theme::font (15.0f));
        g.drawText ("Select a MIDI clip in the Arrangement (double-click it), or press 'New clip'.", getLocalBounds(), juce::Justification::centred);
        return;
    }
    const double len = clip[IDs::length];
    const double bpb = state.beatsPerBar();

    // rows
    for (int p = 0; p < 128; ++p)
    {
        const float y = pitchToY (p);
        if (y > ga.getBottom() || y + noteH < 0) continue;
        const bool black = juce::MidiMessage::isMidiNoteBlack (p);
        const bool inScale = Scales::contains (p, scaleRoot, scaleKind, 0xfff);
        juce::Colour c = black ? Theme::bg : Theme::panel.darker (0.2f);
        if (highlightScale && inScale) c = c.brighter (0.07f).interpolatedWith (Theme::accent, 0.035f);
        if (highlightScale && ((p - scaleRoot) % 12 + 12) % 12 == 0) c = c.interpolatedWith (Theme::accent, 0.07f);
        g.setColour (c);
        g.fillRect ((float) keyW, y, (float) ga.getWidth(), noteH);
        g.setColour (Theme::grid.withAlpha (0.6f));
        g.drawHorizontalLine ((int) (y + noteH), (float) keyW, (float) getWidth());
    }
    // vertical grid
    const double step = gridBeats > 0 ? gridBeats : 0.25;
    for (double b = std::floor (scrollBeat / step) * step; beatToX (b) < getWidth(); b += step)
    {
        const bool bar = std::fmod (b + 1e-9, bpb) < 1e-6, beat = std::fmod (b + 1e-9, 1.0) < 1e-6;
        g.setColour (bar ? Theme::gridStrong.brighter (0.2f) : (beat ? Theme::gridStrong : Theme::grid));
        g.drawVerticalLine ((int) beatToX (b), 0.0f, (float) ga.getBottom());
    }
    // clip end
    g.setColour (Theme::bg.withAlpha (0.6f));
    g.fillRect (juce::Rectangle<float> (beatToX (len), 0, (float) getWidth(), (float) ga.getBottom()));
    g.setColour (Theme::accent2.withAlpha (0.7f));
    g.drawVerticalLine ((int) beatToX (len), 0, (float) ga.getBottom());

    // notes
    const auto col = juce::Colour ((juce::uint32) (juce::int64) Model::trackOfClip (clip)[IDs::colour]);
    for (auto n : clip)
    {
        if (! n.hasType (IDs::NOTE)) continue;
        auto r = noteRect (n);
        if (r.getRight() < keyW || r.getX() > getWidth()) continue;
        const float v = (float) (int) n[IDs::v] / 127.0f;
        const float prob = (float) n.getProperty (IDs::pr, 1.0f);
        const bool sel = isSelected (n);
        g.setColour ((sel ? Theme::warn : col).withAlpha (0.35f + 0.65f * v).withMultipliedAlpha (0.4f + 0.6f * prob));
        g.fillRoundedRectangle (r, 2.0f);
        g.setColour (sel ? juce::Colours::white : col.brighter (0.4f));
        g.drawRoundedRectangle (r, 2.0f, sel ? 1.5f : 1.0f);
        if (prob < 0.999f && r.getWidth() > 18)
        {
            g.setColour (Theme::bg);
            g.setFont (Theme::font (9.0f));
            g.drawText (juce::String ((int) (prob * 100)) + "%", r.reduced (2, 0), juce::Justification::centredLeft);
        }
    }
    if (mode == Mode::Rubber)
    {
        g.setColour (Theme::accent.withAlpha (0.15f)); g.fillRect (rubber);
        g.setColour (Theme::accent); g.drawRect (rubber, 1.0f);
    }

    // playhead
    const double ph = state.engine.getPosition() - (double) clip[IDs::start];
    if (ph >= 0 && ph <= len)
    {
        g.setColour (Theme::accent2);
        g.drawVerticalLine ((int) beatToX (ph), 0, (float) getHeight());
    }

    // keyboard
    for (int p = 0; p < 128; ++p)
    {
        const float y = pitchToY (p);
        if (y > ga.getBottom() || y + noteH < 0) continue;
        const bool black = juce::MidiMessage::isMidiNoteBlack (p);
        g.setColour (black ? juce::Colour (0xff20242e) : juce::Colour (0xffc9cfda));
        g.fillRect (0.0f, y, (float) keyW - 1, noteH - 0.5f);
        if (p % 12 == 0)
        {
            g.setColour (Theme::bg);
            g.setFont (Theme::font (10.0f));
            g.drawText (Scales::noteName (p), juce::Rectangle<float> (2, y, keyW - 6.0f, noteH), juce::Justification::centredRight);
        }
    }

    // velocity / probability lane
    auto la = laneArea();
    g.setColour (Theme::panel);
    g.fillRect (la);
    g.setColour (Theme::outline);
    g.drawHorizontalLine (la.getY(), 0, (float) getWidth());
    g.setColour (Theme::panel);
    g.fillRect (0, la.getY(), keyW, la.getHeight());
    g.setColour (Theme::textDim);
    g.setFont (Theme::font (9.5f));
    g.drawText (laneShowsProbability ? "PROB" : "VEL", juce::Rectangle<int> (4, la.getY() + 4, keyW - 6, 14), juce::Justification::centredLeft);
    for (auto n : clip)
    {
        if (! n.hasType (IDs::NOTE)) continue;
        const float x = beatToX (n[IDs::s]);
        if (x < keyW || x > getWidth()) continue;
        const float v = laneShowsProbability ? (float) n.getProperty (IDs::pr, 1.0f) : (int) n[IDs::v] / 127.0f;
        const float h = v * (la.getHeight() - 10);
        g.setColour ((isSelected (n) ? Theme::warn : (laneShowsProbability ? Theme::accent3 : Theme::accent)).withAlpha (0.9f));
        g.fillRect (x, (float) la.getBottom() - h - 2, 3.0f, h);
        g.fillEllipse (x - 2.0f, (float) la.getBottom() - h - 5, 7, 7);
    }
}

void PianoRollGrid::mouseMove (const juce::MouseEvent& e)
{
    bool edge;
    auto n = noteAt (e.position, edge);
    setMouseCursor (n.isValid() ? (edge ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::DraggingHandCursor) : juce::MouseCursor::NormalCursor);
}

void PianoRollGrid::mouseDown (const juce::MouseEvent& e)
{
    grabKeyboardFocus();
    if (! clip.isValid()) return;
    downPos = e.position;
    downBeat = xToBeat (e.position.x);
    downPitch = yToPitch (e.position.y);
    mode = Mode::None;

    if (e.position.x < keyW && e.position.y < gridArea().getBottom()) { audition (downPitch, 100); return; }

    if (laneArea().contains (e.getPosition()))
    {
        mode = Mode::Lane;
        state.beginEdit (laneShowsProbability ? "Edit probability" : "Edit velocity");
        mouseDrag (e);
        return;
    }

    bool edge;
    auto n = noteAt (e.position, edge);
    if (n.isValid())
    {
        if (e.mods.isPopupMenu() || e.mods.isAltDown())
        {
            state.beginEdit ("Delete note");
            clip.removeChild (n, &state.undo);
            return;
        }
        if (e.mods.isShiftDown()) { if (isSelected (n)) selection.removeFirstMatchingValue (n); else selection.add (n); }
        else if (! isSelected (n)) { selection.clear(); selection.add (n); }
        state.beginEdit (edge ? "Resize notes" : "Move notes");
        if (e.mods.isCommandDown() && ! edge)
        {
            // ctrl-drag duplicates the selection, then drags the copies
            juce::Array<juce::ValueTree> copies;
            for (auto& s : selection) copies.add (Model::addNote (clip, Model::readNote (s), &state.undo));
            selection = copies;
        }
        origs.clear();
        for (auto& s : selection) origs.push_back ({ s, Model::readNote (s) });
        mode = edge ? Mode::Resize : Mode::Move;
        audition ((int) n[IDs::p], (int) n[IDs::v]);
        repaint();
        return;
    }

    if (e.mods.isPopupMenu()) return;
    if (e.mods.isShiftDown() || e.mods.isCommandDown())
    {
        mode = Mode::Rubber;
        rubber = {};
        if (! e.mods.isShiftDown()) selection.clear();
        return;
    }
    // draw a new note
    selection.clear();
    state.beginEdit ("Draw note");
    NoteData d;
    d.pitch = downPitch;
    d.start = juce::jmax (0.0, snapBeat (downBeat));
    d.length = gridBeats > 0 ? juce::jmax (gridBeats, lastLength) : lastLength;
    d.velocity = 100;
    if (d.start >= (double) clip[IDs::length]) return;
    drawNote = Model::addNote (clip, d, &state.undo);
    selection.add (drawNote);
    origs = { { drawNote, d } };
    mode = Mode::Draw;
    audition (d.pitch, 100);
}

void PianoRollGrid::mouseDrag (const juce::MouseEvent& e)
{
    if (! clip.isValid()) return;
    const double beat = xToBeat (e.position.x);
    const double clipLen = clip[IDs::length];
    switch (mode)
    {
        case Mode::Draw:
        {
            auto d = origs[0].d;
            d.length = juce::jmax (gridBeats > 0 ? gridBeats : 1.0 / 64.0, snapBeat (beat) + (gridBeats > 0 ? gridBeats : 0.0) - d.start);
            d.length = juce::jmin (d.length, clipLen - d.start);
            Model::writeNote (drawNote, d, &state.undo);
            lastLength = d.length;
            break;
        }
        case Mode::Move:
        {
            double db = beat - downBeat;
            if (gridBeats > 0 && ! e.mods.isAltDown()) db = std::round (db / gridBeats) * gridBeats;
            const int dp = yToPitch (e.position.y) - downPitch;
            for (auto& o : origs)
            {
                auto d = o.d;
                d.start = juce::jlimit (0.0, juce::jmax (0.0, clipLen - 1.0 / 64.0), o.d.start + db);
                d.pitch = juce::jlimit (0, 127, o.d.pitch + dp);
                Model::writeNote (o.n, d, &state.undo);
            }
            if (! origs.empty() && dp != 0) audition (juce::jlimit (0, 127, origs[0].d.pitch + dp), origs[0].d.velocity);
            break;
        }
        case Mode::Resize:
        {
            double db = beat - downBeat;
            if (gridBeats > 0) db = std::round (db / gridBeats) * gridBeats;
            for (auto& o : origs)
            {
                auto d = o.d;
                d.length = juce::jlimit (1.0 / 64.0, clipLen - d.start, o.d.length + db);
                Model::writeNote (o.n, d, &state.undo);
                lastLength = d.length;
            }
            break;
        }
        case Mode::Rubber:
        {
            rubber = juce::Rectangle<float> (downPos, e.position);
            juce::Array<juce::ValueTree> sel;
            for (auto n : clip) if (n.hasType (IDs::NOTE) && rubber.intersects (noteRect (n))) sel.add (n);
            selection = sel;
            break;
        }
        case Mode::Lane:
        {
            auto la = laneArea();
            const float v = juce::jlimit (0.0f, 1.0f, ((float) la.getBottom() - 2 - e.position.y) / (la.getHeight() - 10));
            // affect notes near the cursor (or all selected notes under the x range)
            for (auto n : clip)
            {
                if (! n.hasType (IDs::NOTE)) continue;
                const float x = beatToX (n[IDs::s]);
                const bool near = std::abs (x - e.position.x) < 6.0f;
                if (! near) continue;
                if (! selection.isEmpty() && ! isSelected (n)) continue;
                if (laneShowsProbability) n.setProperty (IDs::pr, std::round (v * 20.0f) / 20.0f, &state.undo);
                else n.setProperty (IDs::v, juce::jlimit (1, 127, (int) (v * 127)), &state.undo);
            }
            break;
        }
        default: break;
    }
    repaint();
}

void PianoRollGrid::mouseUp (const juce::MouseEvent&)
{
    mode = Mode::None;
    drawNote = {};
    origs.clear();
    audition (-1, 0);
    repaint();
}

void PianoRollGrid::mouseDoubleClick (const juce::MouseEvent& e)
{
    bool edge;
    auto n = noteAt (e.position, edge);
    if (n.isValid()) { state.beginEdit ("Delete note"); selection.removeFirstMatchingValue (n); clip.removeChild (n, &state.undo); }
}

void PianoRollGrid::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& d)
{
    if (e.mods.isCommandDown())
    {
        const double anchor = xToBeat (e.position.x);
        ppb = juce::jlimit (16.0, 600.0, ppb * std::pow (1.15, d.deltaY * 6.0));
        scrollBeat = juce::jmax (0.0, anchor - (e.position.x - keyW) / ppb);
    }
    else if (e.mods.isAltDown())
        noteH = juce::jlimit (6.0f, 30.0f, noteH + d.deltaY * 8.0f);
    else if (e.mods.isShiftDown() || std::abs (d.deltaX) > std::abs (d.deltaY))
        scrollBeat = juce::jmax (0.0, scrollBeat - (d.deltaX + (e.mods.isShiftDown() ? d.deltaY : 0)) * 300.0 / ppb);
    else
        topPitch = juce::jlimit ((int) (gridArea().getHeight() / noteH), 127, topPitch + (int) std::round (d.deltaY * 12.0f));
    repaint();
}

bool PianoRollGrid::keyPressed (const juce::KeyPress& k)
{
    if (k == juce::KeyPress::deleteKey || k == juce::KeyPress::backspaceKey) { deleteSelection(); return true; }
    if (k == juce::KeyPress::upKey) { transposeSelection (k.getModifiers().isShiftDown() ? 12 : 1); return true; }
    if (k == juce::KeyPress::downKey) { transposeSelection (k.getModifiers().isShiftDown() ? -12 : -1); return true; }
    if (k == juce::KeyPress ('a', juce::ModifierKeys::commandModifier, 0)) { selectAll(); return true; }
    if (k == juce::KeyPress ('c', juce::ModifierKeys::commandModifier, 0)) { copySelection(); return true; }
    if (k == juce::KeyPress ('v', juce::ModifierKeys::commandModifier, 0)) { paste(); return true; }
    if (k == juce::KeyPress ('q', 0, 0)) { quantizeSelection(); return true; }
    return false;
}

void PianoRollGrid::quantizeSelection()
{
    if (! clip.isValid() || gridBeats <= 0) return;
    state.beginEdit ("Quantize");
    auto targets = selection;
    if (targets.isEmpty()) for (auto n : clip) if (n.hasType (IDs::NOTE)) targets.add (n);
    for (auto& n : targets)
    {
        auto d = Model::readNote (n);
        d.start = std::round (d.start / gridBeats) * gridBeats;
        d.length = juce::jmax (gridBeats, std::round (d.length / gridBeats) * gridBeats);
        Model::writeNote (n, d, &state.undo);
    }
}

void PianoRollGrid::transposeSelection (int semis)
{
    if (selection.isEmpty()) return;
    state.beginEdit ("Transpose");
    for (auto& n : selection) n.setProperty (IDs::p, juce::jlimit (0, 127, (int) n[IDs::p] + semis), &state.undo);
}

void PianoRollGrid::selectAll()
{
    selection.clear();
    for (auto n : clip) if (n.hasType (IDs::NOTE)) selection.add (n);
    repaint();
}

void PianoRollGrid::deleteSelection()
{
    if (selection.isEmpty()) return;
    state.beginEdit ("Delete notes");
    auto sel = selection;
    selection.clear();
    for (auto& n : sel) clip.removeChild (n, &state.undo);
}

void PianoRollGrid::copySelection()
{
    clipboard.clear();
    double minStart = 1e9;
    for (auto& n : selection) minStart = juce::jmin (minStart, (double) n[IDs::s]);
    for (auto& n : selection) { auto d = Model::readNote (n); d.start -= minStart; clipboard.push_back (d); }
}

void PianoRollGrid::paste()
{
    if (clipboard.empty() || ! clip.isValid()) return;
    state.beginEdit ("Paste notes");
    double at = state.engine.getPosition() - (double) clip[IDs::start];
    if (at < 0 || at >= (double) clip[IDs::length]) at = 0;
    at = snapBeat (at);
    selection.clear();
    for (auto d : clipboard) { d.start += at; if (d.start < (double) clip[IDs::length]) selection.add (Model::addNote (clip, d, &state.undo)); }
}

void PianoRollGrid::humanize()
{
    if (! clip.isValid()) return;
    state.beginEdit ("Humanize");
    juce::Random r (1234);
    auto targets = selection;
    if (targets.isEmpty()) for (auto n : clip) if (n.hasType (IDs::NOTE)) targets.add (n);
    for (auto& n : targets)
    {
        auto d = Model::readNote (n);
        d.start = juce::jmax (0.0, d.start + (r.nextDouble() - 0.5) * 0.03);
        d.velocity = juce::jlimit (1, 127, d.velocity + r.nextInt (21) - 10);
        Model::writeNote (n, d, &state.undo);
    }
}

void PianoRollGrid::legato()
{
    if (! clip.isValid()) return;
    state.beginEdit ("Legato");
    auto notes = Model::readNotes (clip);
    std::vector<juce::ValueTree> nodes;
    for (auto n : clip) if (n.hasType (IDs::NOTE)) nodes.push_back (n);
    std::vector<size_t> idx (notes.size());
    for (size_t i = 0; i < idx.size(); ++i) idx[i] = i;
    std::sort (idx.begin(), idx.end(), [&] (size_t a, size_t b) { return notes[a].start < notes[b].start; });
    for (size_t k = 0; k < idx.size(); ++k)
    {
        double next = (double) clip[IDs::length];
        for (size_t j = k + 1; j < idx.size(); ++j) if (notes[idx[j]].start > notes[idx[k]].start + 1e-6) { next = notes[idx[j]].start; break; }
        auto d = notes[idx[k]];
        d.length = juce::jmax (1.0 / 64.0, next - d.start);
        Model::writeNote (nodes[idx[k]], d, &state.undo);
    }
}

// ============================================================================ page
PianoRollPage::PianoRollPage (AppState& s) : state (s), grid (s)
{
    addAndMakeVisible (grid);
    title.setFont (Theme::font (14.0f, true));
    addAndMakeVisible (title);
    snapBox.addItemList ({ "1/4", "1/8", "1/16", "1/32", "1/8 T", "1/16 T", "1/5", "1/7", "Off" }, 1);
    snapBox.setSelectedItemIndex (2, juce::dontSendNotification);
    snapBox.onChange = [this]
    {
        static const double v[] = { 1.0, 0.5, 0.25, 0.125, 1.0 / 3.0, 1.0 / 6.0, 0.2, 1.0 / 7.0, 0.0 };
        grid.gridBeats = v[snapBox.getSelectedItemIndex()];
        grid.repaint();
    };
    rootBox.addItemList (Scales::rootNames(), 1);
    rootBox.setSelectedItemIndex (9, juce::dontSendNotification);
    rootBox.onChange = [this] { grid.scaleRoot = rootBox.getSelectedItemIndex(); grid.repaint(); };
    scaleBox.addItemList (Scales::names(), 1);
    scaleBox.setSelectedItemIndex (1, juce::dontSendNotification);
    scaleBox.onChange = [this] { grid.scaleKind = scaleBox.getSelectedItemIndex(); grid.repaint(); };
    scaleHi.setToggleState (true, juce::dontSendNotification);
    scaleHi.onClick = [this] { grid.highlightScale = scaleHi.getToggleState(); grid.repaint(); };
    for (auto* c : std::initializer_list<juce::Component*> { &snapBox, &rootBox, &scaleBox, &scaleHi }) addAndMakeVisible (c);

    quantize.onClick = [this] { grid.quantizeSelection(); };
    up.onClick = [this] { grid.transposeSelection (12); };
    down.onClick = [this] { grid.transposeSelection (-12); };
    selAll.onClick = [this] { grid.selectAll(); };
    del.onClick = [this] { grid.deleteSelection(); };
    human.onClick = [this] { grid.humanize(); };
    leg.onClick = [this] { grid.legato(); };
    laneMode.onClick = [this]
    {
        grid.laneShowsProbability = ! grid.laneShowsProbability;
        laneMode.setButtonText (grid.laneShowsProbability ? "Lane: Probability" : "Lane: Velocity");
        grid.repaint();
    };
    newClip.onClick = [this]
    {
        auto t = state.selectedTrack;
        if (! t.isValid() || t[IDs::type].toString() == "audio")
            for (auto tr : Model::tracks (state.project)) if (tr[IDs::type].toString() == "synth") { t = tr; break; }
        if (! t.isValid()) return;
        state.beginEdit ("New clip");
        const double bpb = state.beatsPerBar();
        const double at = Model::freeSpot (t, std::floor (state.engine.getPosition() / bpb) * bpb, bpb * 2, bpb);
        state.selectClip (Model::addMidiClip (t, at, bpb * 2, &state.undo));
    };
    for (auto* b : { &quantize, &up, &down, &selAll, &del, &human, &leg, &laneMode, &newClip }) addAndMakeVisible (b);
    state.selectionChanged.addChangeListener (this);
}

PianoRollPage::~PianoRollPage() { state.selectionChanged.removeChangeListener (this); }

void PianoRollPage::setProject (juce::ValueTree) { grid.setClip ({}); changeListenerCallback (nullptr); }

void PianoRollPage::changeListenerCallback (juce::ChangeBroadcaster*)
{
    auto c = state.selectedClip;
    if (c.isValid() && c[IDs::type].toString() != "midi") c = {};
    grid.setClip (c);
    if (c.isValid())
    {
        title.setText (Model::trackOfClip (c)[IDs::name].toString() + "  /  " + c[IDs::name].toString(), juce::dontSendNotification);
        // adopt the scale of a generator driving this track, if any
        for (auto g : state.project.getChildWithName (IDs::GENERATORS))
            if ((int) g[IDs::track] == (int) Model::trackOfClip (c)[IDs::id])
            {
                rootBox.setSelectedItemIndex ((int) g[IDs::root]);
                scaleBox.setSelectedItemIndex ((int) g[IDs::scale]);
                break;
            }
    }
    else title.setText ("Piano Roll", juce::dontSendNotification);
}

void PianoRollPage::paint (juce::Graphics& g)
{
    g.fillAll (Theme::bg);
    g.setColour (Theme::panel);
    g.fillRect (getLocalBounds().removeFromTop (40));
}

void PianoRollPage::resized()
{
    auto r = getLocalBounds();
    auto tb = r.removeFromTop (40).reduced (8, 7);
    title.setBounds (tb.removeFromLeft (220));
    snapBox.setBounds (tb.removeFromLeft (80)); tb.removeFromLeft (6);
    quantize.setBounds (tb.removeFromLeft (76)); tb.removeFromLeft (4);
    up.setBounds (tb.removeFromLeft (40)); tb.removeFromLeft (2);
    down.setBounds (tb.removeFromLeft (40)); tb.removeFromLeft (6);
    selAll.setBounds (tb.removeFromLeft (78)); tb.removeFromLeft (4);
    del.setBounds (tb.removeFromLeft (60)); tb.removeFromLeft (4);
    human.setBounds (tb.removeFromLeft (80)); tb.removeFromLeft (4);
    leg.setBounds (tb.removeFromLeft (64)); tb.removeFromLeft (4);
    laneMode.setBounds (tb.removeFromLeft (120)); tb.removeFromLeft (10);
    scaleHi.setBounds (tb.removeFromLeft (64));
    rootBox.setBounds (tb.removeFromLeft (56)); tb.removeFromLeft (4);
    scaleBox.setBounds (tb.removeFromLeft (130)); tb.removeFromLeft (6);
    newClip.setBounds (tb.removeFromRight (84));
    grid.setBounds (r.reduced (4));
}
