#include "ArrangementView.h"
#include "../Live/LiveSequencer.h"

static const juce::Identifier autoParamId ("autoParam");

// ============================================================================ TrackHeader
TrackHeader::TrackHeader (AppState& s, juce::ValueTree t) : track (t), state (s)
{
    name.setText (track[IDs::name].toString(), juce::dontSendNotification);
    name.setEditable (false, true, false);
    name.setFont (Theme::font (13.0f, true));
    name.onTextChange = [this] { state.beginEdit ("Rename track"); track.setProperty (IDs::name, name.getText(), &state.undo); };
    name.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (name);
    mute.bind (track, IDs::mute, &state.undo);
    solo.bind (track, IDs::solo, &state.undo);
    arm.bind (track, IDs::arm, &state.undo);
    autoBtn.bind (track, IDs::showAuto, &state.undo);
    mute.setTooltip ("Mute"); solo.setTooltip ("Solo"); arm.setTooltip ("Record arm (audio tracks record input)"); autoBtn.setTooltip ("Show automation lane");
    for (auto* b : { &mute, &solo, &autoBtn }) addAndMakeVisible (b);
    if (track[IDs::type].toString() == "audio") addAndMakeVisible (arm);

    autoIds = AutoTargets::allIds (track[IDs::type].toString() == "synth");
    for (int i = 0; i < autoIds.size(); ++i) autoParam.addItem (AutoTargets::label (autoIds[i]), i + 1);
    const int cur = autoIds.indexOf (track.getProperty (autoParamId, "vol").toString());
    autoParam.setSelectedItemIndex (juce::jmax (0, cur), juce::dontSendNotification);
    autoParam.onChange = [this] { track.setProperty (autoParamId, autoIds[autoParam.getSelectedItemIndex()], nullptr); };
    addChildComponent (autoParam);
}

void TrackHeader::paint (juce::Graphics& g)
{
    const bool sel = state.selectedTrack == track;
    auto r = getLocalBounds();
    g.setColour (sel ? Theme::panelLight : Theme::panel);
    g.fillRect (r);
    g.setColour (juce::Colour ((juce::uint32) (juce::int64) track[IDs::colour]));
    g.fillRect (r.removeFromLeft (4));
    g.setColour (Theme::outline);
    g.drawHorizontalLine (getHeight() - 1, 0, (float) getWidth());
    g.setColour (Theme::textDim);
    g.setFont (Theme::font (10.0f));
    const auto type = track[IDs::type].toString().toUpperCase();
    g.drawText (type, getLocalBounds().reduced (10, 4).withHeight ((int) track.getProperty (IDs::height, 64) - 8).removeFromBottom (14).removeFromLeft (60), juce::Justification::centredLeft);
    const int lp = (int) track.getProperty (IDs::livePart, 0);
    if (lp > 0)
    {
        g.setColour (Theme::good.withAlpha (0.8f));
        g.setFont (Theme::font (10.0f, true));
        g.drawText ("LIVE " + Live::partNames()[lp].toUpperCase(), getLocalBounds().reduced (10, 6).removeFromTop (18), juce::Justification::centredRight);
    }
}

void TrackHeader::resized()
{
    const int trackH = (int) track.getProperty (IDs::height, 64);
    auto r = getLocalBounds().withHeight (trackH).reduced (8, 6);
    r.removeFromLeft (2);
    auto top = r.removeFromTop (20);
    name.setBounds (top);
    auto btns = r.removeFromBottom (20).withTrimmedLeft (60);
    for (auto* b : std::initializer_list<juce::Component*> { &mute, &solo, &arm, &autoBtn })
    {
        b->setBounds (btns.removeFromLeft (26));
        btns.removeFromLeft (3);
    }
    autoParam.setVisible ((bool) track[IDs::showAuto]);
    autoParam.setBounds (8, trackH + 6, getWidth() - 16, 22);
}

void TrackHeader::mouseDown (const juce::MouseEvent& e)
{
    state.selectTrack (track);
    if (e.mods.isPopupMenu())
    {
        juce::PopupMenu m;
        m.addItem (1, "Rename");
        m.addItem (2, "Move up");
        m.addItem (3, "Move down");
        m.addItem (4, "Taller");
        m.addItem (5, "Shorter");
        m.addSeparator();
        m.addItem (9, "Delete track");
        m.showMenuAsync (juce::PopupMenu::Options(), [this] (int r)
        {
            auto parent = track.getParent();
            const int idx = parent.indexOf (track);
            if (r == 1) name.showEditor();
            if (r == 2 && idx > 0) { state.beginEdit ("Move track"); parent.moveChild (idx, idx - 1, &state.undo); }
            if (r == 3 && idx < parent.getNumChildren() - 1) { state.beginEdit ("Move track"); parent.moveChild (idx, idx + 1, &state.undo); }
            if (r == 4) track.setProperty (IDs::height, juce::jmin (200, (int) track[IDs::height] + 24), nullptr);
            if (r == 5) track.setProperty (IDs::height, juce::jmax (40, (int) track[IDs::height] - 24), nullptr);
            if (r == 9)
            {
                auto& st = state;
                auto t = track;
                st.beginEdit ("Delete track");
                if (st.selectedTrack == t) st.selectTrack ({});
                auto gens = st.project.getChildWithName (IDs::GENERATORS);
                for (int i = gens.getNumChildren(); --i >= 0;)
                    if ((int) gens.getChild (i)[IDs::track] == (int) t[IDs::id]) gens.removeChild (i, &st.undo);
                parent.removeChild (t, &st.undo);   // 'this' is deleted during this call - touch nothing after
            }
        });
    }
    else if (e.getNumberOfClicks() == 2) name.showEditor();
}

// ============================================================================ ArrangementView
ArrangementView::ArrangementView (AppState& s, Composer& c) : state (s), composer (c)
{
    addAndMakeVisible (body);
    for (auto* b : { &addSynth, &addDrums, &addAudio, &importBtn, &genAllBtn }) addAndMakeVisible (b);
    addSynth.onClick = [this] { addTrack ("synth"); };
    addDrums.onClick = [this] { addTrack ("drums"); };
    addAudio.onClick = [this] { addTrack ("audio"); };
    importBtn.onClick = [this] { importAudio(); };
    genAllBtn.setColour (juce::TextButton::buttonColourId, Theme::accent3.withAlpha (0.35f));
    genAllBtn.setTooltip ("Run every generator at the current fractal probe and write clips at the playhead");
    genAllBtn.onClick = [this]
    {
        if (project.getChildWithName (IDs::GENERATORS).getNumChildren() == 0) { if (state.setStatus) state.setStatus ("No generators yet: add them in the Generative workspace"); return; }
        composer.generateAll (snap (state.engine.getPosition()));
    };
    snapBox.addItemList ({ "Snap: Bar", "Snap: Beat", "Snap: 1/8", "Snap: 1/16", "Snap: Off" }, 1);
    snapBox.setSelectedItemIndex (1, juce::dontSendNotification);
    snapBox.onChange = [this]
    {
        static const double vals[] = { -1.0, 1.0, 0.5, 0.25, 0.0 };
        state.snapBeats = vals[snapBox.getSelectedItemIndex()];
    };
    addAndMakeVisible (snapBox);
    zoom.setSliderStyle (juce::Slider::LinearHorizontal);
    zoom.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    zoom.setRange (4.0, 200.0);
    zoom.setSkewFactorFromMidPoint (30.0);
    zoom.setValue (pixelsPerBeat, juce::dontSendNotification);
    zoom.onValueChange = [this] { pixelsPerBeat = zoom.getValue(); updateScrollbars(); repaint(); };
    addAndMakeVisible (zoom);
    addAndMakeVisible (hScroll);
    addAndMakeVisible (vScroll);
    hScroll.addListener (this);
    vScroll.addListener (this);
    state.selectionChanged.addChangeListener (this);
    startTimerHz (30);
}

ArrangementView::~ArrangementView()
{
    state.selectionChanged.removeChangeListener (this);
    if (project.isValid()) project.removeListener (this);
}

void ArrangementView::setProject (juce::ValueTree p)
{
    if (project.isValid()) project.removeListener (this);
    project = p;
    project.addListener (this);
    scrollBeats = 0; scrollY = 0;
    rebuildHeaders();
    updateScrollbars();
    repaint();
}

void ArrangementView::structureChanged (juce::ValueTree& parent)
{
    if (parent.hasType (IDs::TRACKS)) rebuildHeaders();
    if (state.selectedClip.isValid() && ! state.selectedClip.getParent().isValid()) state.selectClip ({});
    if (state.selectedTrack.isValid() && ! state.selectedTrack.getParent().isValid()) state.selectTrack ({});
    updateScrollbars();
    repaint();
}

void ArrangementView::rebuildHeaders()
{
    headers.clear();
    for (auto t : Model::tracks (project))
    {
        auto* h = headers.add (new TrackHeader (state, t));
        body.addAndMakeVisible (h);
    }
    layoutHeaders();
}

void ArrangementView::layoutHeaders()
{
    for (auto* h : headers)
    {
        auto row = trackRow (h->track, false);
        const int total = trackRowHeight (h->track);
        h->setBounds (0, row.getY(), headerW, total);
        h->resized();
    }
}

int ArrangementView::trackRowHeight (const juce::ValueTree& t) const
{
    return (int) t.getProperty (IDs::height, 64) + ((bool) t[IDs::showAuto] ? autoH : 0);
}

juce::Rectangle<int> ArrangementView::trackRow (const juce::ValueTree& t, bool automationPart) const
{
    int y = rulerH - scrollY;
    for (auto tr : Model::tracks (project))
    {
        const int h = (int) tr.getProperty (IDs::height, 64);
        if (tr == t)
        {
            if (automationPart) return { headerW, y + h, body.getWidth() - headerW, (bool) tr[IDs::showAuto] ? autoH : 0 };
            return { headerW, y, body.getWidth() - headerW, h };
        }
        y += trackRowHeight (tr);
    }
    return {};
}

juce::ValueTree ArrangementView::trackAtY (int y, bool& inAuto, juce::Rectangle<int>& rowArea) const
{
    int yy = rulerH - scrollY;
    for (auto tr : Model::tracks (project))
    {
        const int h = (int) tr.getProperty (IDs::height, 64);
        const int total = trackRowHeight (tr);
        if (y >= yy && y < yy + total)
        {
            inAuto = y >= yy + h;
            rowArea = inAuto ? juce::Rectangle<int> (headerW, yy + h, body.getWidth() - headerW, autoH) : juce::Rectangle<int> (headerW, yy, body.getWidth() - headerW, h);
            return tr;
        }
        yy += total;
    }
    inAuto = false;
    return {};
}

juce::Rectangle<int> ArrangementView::timelineArea() const { return body.getLocalBounds().withTrimmedLeft (headerW).withTrimmedTop (rulerH); }
float ArrangementView::beatToX (double beat) const { return (float) (headerW + (beat - scrollBeats) * pixelsPerBeat); }
double ArrangementView::xToBeat (float x) const { return scrollBeats + (x - headerW) / pixelsPerBeat; }

double ArrangementView::snap (double beat) const
{
    double g = state.snapBeats;
    if (g < 0) g = state.beatsPerBar();
    if (g <= 0) return juce::jmax (0.0, beat);
    return juce::jmax (0.0, std::round (beat / g) * g);
}

juce::Rectangle<float> ArrangementView::clipBounds (const juce::ValueTree& clip) const
{
    auto row = trackRow (Model::trackOfClip (clip), false);
    const float x0 = beatToX (clip[IDs::start]);
    const float x1 = beatToX ((double) clip[IDs::start] + (double) clip[IDs::length]);
    return { x0, (float) row.getY() + 2.0f, juce::jmax (4.0f, x1 - x0), (float) row.getHeight() - 4.0f };
}

juce::ValueTree ArrangementView::clipAt (juce::Point<int> p) const
{
    bool inAuto; juce::Rectangle<int> row;
    auto t = trackAtY (p.y, inAuto, row);
    if (! t.isValid() || inAuto) return {};
    auto clips = t.getChildWithName (IDs::CLIPS);
    for (int i = clips.getNumChildren(); --i >= 0;)
        if (clipBounds (clips.getChild (i)).contains (p.toFloat())) return clips.getChild (i);
    return {};
}

double ArrangementView::contentEnd() const
{
    return juce::jmax (64.0, Model::projectEndBeat (project) + 32.0, (double) project.getProperty (IDs::loopEnd, 16.0) + 16.0);
}

void ArrangementView::updateScrollbars()
{
    const double visibleBeats = (body.getWidth() - headerW) / pixelsPerBeat;
    hScroll.setRangeLimits (0.0, juce::jmax (contentEnd(), scrollBeats + visibleBeats));
    hScroll.setCurrentRange (scrollBeats, visibleBeats, juce::dontSendNotification);
    int total = 0;
    for (auto t : Model::tracks (project)) total += trackRowHeight (t);
    const int visible = body.getHeight() - rulerH;
    vScroll.setRangeLimits (0.0, juce::jmax (total + 40, visible));
    vScroll.setCurrentRange (scrollY, visible, juce::dontSendNotification);
}

void ArrangementView::scrollBarMoved (juce::ScrollBar* sb, double start)
{
    if (sb == &hScroll) scrollBeats = start;
    else { scrollY = (int) start; layoutHeaders(); }
    repaint();
}

void ArrangementView::timerCallback()
{
    // auto-scroll to follow the playhead while playing
    if (state.engine.isPlaying() && drag == Drag::None)
    {
        const double pos = state.engine.getPosition();
        const double visible = (body.getWidth() - headerW) / pixelsPerBeat;
        if (pos > scrollBeats + visible * 0.95 || pos < scrollBeats)
        {
            scrollBeats = juce::jmax (0.0, pos - visible * 0.1);
            updateScrollbars();
        }
    }
    body.repaint();
}

void ArrangementView::resized()
{
    auto r = getLocalBounds();
    auto tb = r.removeFromTop (toolbarH).reduced (6, 6);
    for (auto* b : { &addSynth, &addDrums, &addAudio }) { b->setBounds (tb.removeFromLeft (74)); tb.removeFromLeft (4); }
    tb.removeFromLeft (8);
    importBtn.setBounds (tb.removeFromLeft (96)); tb.removeFromLeft (4);
    genAllBtn.setBounds (tb.removeFromLeft (110)); tb.removeFromLeft (12);
    snapBox.setBounds (tb.removeFromLeft (120)); tb.removeFromLeft (12);
    zoom.setBounds (tb.removeFromLeft (160));
    hScroll.setBounds (r.removeFromBottom (12).withTrimmedLeft (headerW));
    vScroll.setBounds (r.removeFromRight (12));
    body.setBounds (r);
    layoutHeaders();
    updateScrollbars();
}

void ArrangementView::paint (juce::Graphics& g)
{
    g.fillAll (Theme::bg);
    g.setColour (Theme::panel);
    g.fillRect (getLocalBounds().removeFromTop (toolbarH));
}

// ---------------------------------------------------------------------------- painting
juce::String ArrangementView::autoParamOf (const juce::ValueTree& track) const
{
    return track.getProperty (autoParamId, "vol").toString();
}

void ArrangementView::paintBody (juce::Graphics& g)
{
    auto area = body.getLocalBounds();
    g.setColour (Theme::bg);
    g.fillRect (area);
    const double bpb = state.beatsPerBar();
    const auto tl = timelineArea();

    // track backgrounds
    for (auto t : Model::tracks (project))
    {
        auto row = trackRow (t, false);
        g.setColour (state.selectedTrack == t ? Theme::panel.brighter (0.03f) : Theme::panel.darker (0.15f));
        g.fillRect (row);
        g.setColour (Theme::outline.withAlpha (0.6f));
        g.drawHorizontalLine (row.getBottom() - 1, (float) headerW, (float) area.getRight());
        if ((bool) t[IDs::showAuto])
        {
            auto ar = trackRow (t, true);
            g.setColour (Theme::bg.brighter (0.02f));
            g.fillRect (ar);
            g.setColour (Theme::outline.withAlpha (0.6f));
            g.drawHorizontalLine (ar.getBottom() - 1, (float) headerW, (float) area.getRight());
        }
    }

    // grid
    const double firstBeat = std::floor (scrollBeats);
    const double lastBeat = xToBeat ((float) area.getRight()) + 1;
    const bool showBeats = pixelsPerBeat > 10;
    for (double b = firstBeat; b < lastBeat; b += 1.0)
    {
        const bool isBar = std::fmod (b, bpb) < 1e-6;
        if (! isBar && ! showBeats) continue;
        const float x = beatToX (b);
        g.setColour (isBar ? Theme::gridStrong : Theme::grid);
        g.drawVerticalLine ((int) x, (float) tl.getY(), (float) tl.getBottom());
    }

    // loop region shading
    const bool loopOn = project.getProperty (IDs::loopOn, false);
    const float lx0 = beatToX (project.getProperty (IDs::loopStart, 0.0)), lx1 = beatToX (project.getProperty (IDs::loopEnd, 16.0));
    if (loopOn)
    {
        g.setColour (Theme::accent.withAlpha (0.04f));
        g.fillRect (juce::Rectangle<float> (lx0, (float) tl.getY(), lx1 - lx0, (float) tl.getHeight()));
    }

    // clips
    for (auto t : Model::tracks (project))
    {
        const auto col = juce::Colour ((juce::uint32) (juce::int64) t[IDs::colour]);
        for (auto c : t.getChildWithName (IDs::CLIPS))
        {
            auto r = clipBounds (c);
            if (r.getRight() < headerW || r.getX() > area.getRight()) continue;
            paintClip (g, c, r, col);
        }
        if ((bool) t[IDs::showAuto]) paintAutomation (g, t, trackRow (t, true));
    }
}

void ArrangementView::paintBodyOver (juce::Graphics& g)
{
    auto area = body.getLocalBounds();
    const double bpb = state.beatsPerBar();
    // ruler (drawn over headers so scrolled headers slide beneath it)
    auto ruler = area.removeFromTop (rulerH);
    g.setColour (Theme::panel);
    g.fillRect (ruler);
    g.setColour (Theme::outline);
    g.drawHorizontalLine (ruler.getBottom() - 1, 0, (float) ruler.getRight());
    const bool loopOn = project.getProperty (IDs::loopOn, false);
    const float lx0 = beatToX (project.getProperty (IDs::loopStart, 0.0)), lx1 = beatToX (project.getProperty (IDs::loopEnd, 16.0));
    g.setColour (loopOn ? Theme::accent.withAlpha (0.35f) : Theme::outline.withAlpha (0.5f));
    g.fillRect (juce::Rectangle<float> (juce::jmax ((float) headerW, lx0), 2.0f, juce::jmax (0.0f, lx1 - juce::jmax ((float) headerW, lx0)), 8.0f));
    g.setFont (Theme::font (11.0f));
    const int barStep = pixelsPerBeat * bpb < 40 ? (pixelsPerBeat * bpb < 12 ? 8 : 4) : 1;
    for (double b = std::floor (scrollBeats / bpb) * bpb; beatToX (b) < ruler.getRight(); b += bpb)
    {
        const int bar = (int) std::round (b / bpb);
        if (bar % barStep != 0) continue;
        const float x = beatToX (b);
        if (x < headerW) continue;
        g.setColour (Theme::textDim);
        g.drawVerticalLine ((int) x, 12.0f, (float) rulerH);
        g.drawText (juce::String (bar + 1), (int) x + 3, 10, 40, rulerH - 10, juce::Justification::centredLeft);
    }
    g.setColour (Theme::panel);
    g.fillRect (0, 0, headerW, rulerH);
    g.setColour (Theme::textDim);
    g.drawText ("TRACKS", 10, 0, headerW - 20, rulerH, juce::Justification::centredLeft);

    // playhead
    const float px = beatToX (state.engine.getPosition());
    if (px >= headerW)
    {
        g.setColour (Theme::accent2);
        g.drawVerticalLine ((int) px, 0.0f, (float) body.getHeight());
        juce::Path tri; tri.addTriangle (px - 5, 0, px + 5, 0, px, 8);
        g.fillPath (tri);
    }
}

void ArrangementView::paintClip (juce::Graphics& g, const juce::ValueTree& c, juce::Rectangle<float> r, juce::Colour col) const
{
    const bool sel = state.selectedClip == c;
    const auto type = c[IDs::type].toString();
    g.setColour (col.withAlpha (sel ? 0.42f : 0.26f));
    g.fillRoundedRectangle (r, 4.0f);
    g.setColour (sel ? Theme::text : col.withAlpha (0.8f));
    g.drawRoundedRectangle (r.reduced (0.5f), 4.0f, sel ? 1.6f : 1.0f);
    auto header = r.removeFromTop (14.0f);
    g.setColour (col.withAlpha (0.55f));
    g.fillRoundedRectangle (header.reduced (1.0f), 3.0f);
    g.setColour (Theme::bg);
    g.setFont (Theme::font (10.5f, true));
    g.drawText (c[IDs::name].toString(), header.reduced (4, 0), juce::Justification::centredLeft, true);

    const double start = c[IDs::start], len = c[IDs::length];
    const auto inner = r.reduced (2.0f, 2.0f);
    g.saveState();
    g.reduceClipRegion (inner.toNearestInt());
    if (type == "midi")
    {
        int lo = 127, hi = 0;
        for (auto n : c) if (n.hasType (IDs::NOTE)) { lo = juce::jmin (lo, (int) n[IDs::p]); hi = juce::jmax (hi, (int) n[IDs::p]); }
        if (hi >= lo)
        {
            const float range = (float) juce::jmax (12, hi - lo + 1);
            g.setColour (col.brighter (0.5f));
            for (auto n : c)
            {
                if (! n.hasType (IDs::NOTE)) continue;
                const float x0 = beatToX (start + (double) n[IDs::s]), x1 = beatToX (start + juce::jmin (len, (double) n[IDs::s] + (double) n[IDs::l]));
                const float y = inner.getBottom() - ((int) n[IDs::p] - lo + 1) / range * inner.getHeight();
                g.fillRect (x0, y, juce::jmax (1.5f, x1 - x0 - 0.5f), juce::jmax (1.5f, inner.getHeight() / range - 0.5f));
            }
        }
    }
    else if (type == "pattern")
    {
        auto pat = c.getChildWithName (IDs::PATTERN);
        const float rowH = inner.getHeight() / PadP::numPads;
        g.setColour (col.brighter (0.6f));
        for (auto lane : pat)
        {
            const int pad = lane[IDs::pad];
            const int steps = juce::jmax (1, (int) lane[IDs::steps]), rate = juce::jmax (1, (int) lane[IDs::rate]);
            const auto data = Model::readSteps (lane);
            const int total = (int) (len * rate);
            for (int k = 0; k < total; ++k)
            {
                const auto& st = data[(size_t) (k % steps)];
                if (st.velocity <= 0) continue;
                const float x = beatToX (start + k / (double) rate);
                g.setOpacity (0.35f + 0.65f * st.probability / 100.0f);
                g.fillRect (x, inner.getY() + pad * rowH, juce::jmax (1.5f, (float) pixelsPerBeat / rate - 1.0f), juce::jmax (1.0f, rowH - 0.5f));
            }
        }
    }
    else if (type == "audio")
    {
        if (auto d = AudioPool::get().load (juce::File (c[IDs::file].toString())))
        {
            const double secPerBeat = 60.0 / state.tempo();
            const double off = c.getProperty (IDs::offset, 0.0);
            // only draw the visible portion
            const float vx0 = juce::jmax (inner.getX(), (float) headerW), vx1 = juce::jmin (inner.getRight(), (float) body.getWidth());
            if (vx1 > vx0)
            {
                const double b0 = xToBeat (vx0) - start, b1 = xToBeat (vx1) - start;
                WaveformCache::draw (g, *d, { vx0, inner.getY(), vx1 - vx0, inner.getHeight() }, off + b0 * secPerBeat, off + b1 * secPerBeat, col.brighter (0.5f));
            }
        }
        else
        {
            g.setColour (Theme::danger);
            g.drawText ("missing: " + juce::File (c[IDs::file].toString()).getFileName(), inner, juce::Justification::centred);
        }
    }
    g.restoreState();
}

void ArrangementView::paintAutomation (juce::Graphics& g, const juce::ValueTree& track, juce::Rectangle<int> r) const
{
    auto lane = Model::automationLane (const_cast<juce::ValueTree&> (track), autoParamOf (track), false, nullptr);
    const auto rr = r.toFloat().reduced (0, 6);
    g.setColour (Theme::textDim.withAlpha (0.5f));
    g.setFont (Theme::font (10.0f));
    g.drawText (AutoTargets::label (autoParamOf (track)) + (lane.isValid() && lane.getNumChildren() > 0 ? "" : "  (click to add points)"),
                r.reduced (6, 2).withTrimmedLeft (0), juce::Justification::topLeft);
    if (! lane.isValid() || lane.getNumChildren() == 0) return;
    std::vector<std::pair<double, float>> pts;
    for (auto p : lane) pts.push_back ({ (double) p[IDs::t], (float) p[IDs::val] });
    std::sort (pts.begin(), pts.end());
    juce::Path path;
    auto toPt = [&] (const std::pair<double, float>& p) { return juce::Point<float> (beatToX (p.first), rr.getBottom() - p.second * rr.getHeight()); };
    path.startNewSubPath ({ (float) headerW, toPt (pts.front()).y });
    for (auto& p : pts) path.lineTo (toPt (p));
    path.lineTo ({ (float) r.getRight(), toPt (pts.back()).y });
    g.setColour (Theme::accent3);
    g.strokePath (path, juce::PathStrokeType (1.5f));
    for (auto& p : pts) { auto q = toPt (p); g.fillEllipse (q.x - 3.5f, q.y - 3.5f, 7, 7); }
}

// ---------------------------------------------------------------------------- mouse
void ArrangementView::mouseMove (const juce::MouseEvent& e)
{
    if (e.eventComponent != &body) return;
    auto clip = clipAt (e.getPosition());
    if (clip.isValid())
    {
        auto r = clipBounds (clip);
        if (e.position.x > r.getRight() - 6) body.setMouseCursor (juce::MouseCursor::LeftRightResizeCursor);
        else if (e.position.x < r.getX() + 6 && clip[IDs::type].toString() == "audio") body.setMouseCursor (juce::MouseCursor::LeftRightResizeCursor);
        else body.setMouseCursor (juce::MouseCursor::DraggingHandCursor);
    }
    else body.setMouseCursor (juce::MouseCursor::NormalCursor);
}

void ArrangementView::mouseDown (const juce::MouseEvent& e)
{
    if (e.eventComponent != &body) return;
    drag = Drag::None;
    dragCopied = false;
    const auto p = e.getPosition();
    dragDownBeat = xToBeat (e.position.x);
    if (p.x < headerW) return;

    if (p.y < rulerH)
    {
        if (e.mods.isPopupMenu()) return;
        drag = Drag::Playhead;
        state.engine.setPosition (snap (dragDownBeat));
        return;
    }

    bool inAuto; juce::Rectangle<int> row;
    auto track = trackAtY (p.y, inAuto, row);
    if (! track.isValid()) { state.selectClip ({}); return; }

    if (inAuto)
    {
        auto lane = Model::automationLane (track, autoParamOf (track), true, &state.undo);
        const auto rr = row.toFloat().reduced (0, 6);
        // hit-test existing points
        for (auto pt : lane)
        {
            const juce::Point<float> q (beatToX (pt[IDs::t]), rr.getBottom() - (float) pt[IDs::val] * rr.getHeight());
            if (q.getDistanceFrom (e.position) < 7.0f)
            {
                if (e.mods.isPopupMenu()) { state.beginEdit ("Delete automation point"); lane.removeChild (pt, &state.undo); return; }
                state.beginEdit ("Move automation point");
                drag = Drag::AutoPoint; dragPoint = pt; dragAutoTrack = track;
                return;
            }
        }
        if (e.mods.isPopupMenu()) return;
        state.beginEdit ("Add automation point");
        juce::ValueTree pt (IDs::POINT);
        pt.setProperty (IDs::t, snap (dragDownBeat), nullptr);
        pt.setProperty (IDs::val, juce::jlimit (0.0f, 1.0f, (rr.getBottom() - e.position.y) / rr.getHeight()), nullptr);
        lane.appendChild (pt, &state.undo);
        drag = Drag::AutoPoint; dragPoint = pt; dragAutoTrack = track;
        return;
    }

    auto clip = clipAt (p);
    if (! clip.isValid())
    {
        state.selectTrack (track);
        state.selectClip ({});
        if (e.mods.isPopupMenu())
        {
            juce::PopupMenu m;
            const auto type = track[IDs::type].toString();
            m.addItem (1, type == "audio" ? "Import audio here..." : (type == "drums" ? "New pattern clip here" : "New MIDI clip here"));
            if (type == "drums") m.addItem (2, "New MIDI clip here");
            const double at = snap (dragDownBeat);
            m.showMenuAsync (juce::PopupMenu::Options(), [this, track, at, type] (int r)
            {
                const double len = state.beatsPerBar() * 4;
                if (r == 1 && type == "audio") { chooser = std::make_unique<juce::FileChooser> ("Import audio", juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg");
                    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this, track, at] (const juce::FileChooser& fc) { if (fc.getResult().existsAsFile()) addAudioFileToTrack (fc.getResult(), track, at); }); }
                else if (r == 1) { state.beginEdit ("New clip"); state.selectClip (type == "drums" ? Model::addPatternClip (track, at, len, &state.undo) : Model::addMidiClip (track, at, len, &state.undo)); }
                else if (r == 2) { state.beginEdit ("New clip"); state.selectClip (Model::addMidiClip (track, at, len, &state.undo)); }
            });
        }
        else
            state.engine.setPosition (snap (dragDownBeat));
        return;
    }

    state.selectClip (clip);
    if (e.mods.isPopupMenu()) { clipMenu (clip); return; }
    auto r = clipBounds (clip);
    dragClip = clip;
    dragOrigStart = clip[IDs::start];
    dragOrigLength = clip[IDs::length];
    dragOrigOffset = clip.getProperty (IDs::offset, 0.0);
    state.beginEdit ("Edit clip");
    if (e.position.x > r.getRight() - 6) drag = Drag::ResizeClip;
    else if (e.position.x < r.getX() + 6 && clip[IDs::type].toString() == "audio") drag = Drag::TrimClipStart;
    else drag = Drag::MoveClip;
}

void ArrangementView::mouseDrag (const juce::MouseEvent& e)
{
    if (e.eventComponent != &body) return;
    const double beat = xToBeat (e.position.x);
    const double delta = beat - dragDownBeat;
    switch (drag)
    {
        case Drag::Playhead:
        {
            if (std::abs (e.getDistanceFromDragStartX()) > 4)
            {
                // dragging on the ruler defines the loop region
                const double a = snap (juce::jmin (dragDownBeat, beat)), b = snap (juce::jmax (dragDownBeat, beat));
                if (b - a > 0.2)
                {
                    project.setProperty (IDs::loopStart, a, nullptr);
                    project.setProperty (IDs::loopEnd, b, nullptr);
                    project.setProperty (IDs::loopOn, true, nullptr);
                }
            }
            break;
        }
        case Drag::MoveClip:
        {
            if (e.mods.isCommandDown() && ! dragCopied && std::abs (e.getDistanceFromDragStartX()) > 4)
            {
                // ctrl-drag duplicates: leave a copy behind, keep dragging the original
                Model::duplicateClip (dragClip, dragOrigStart, &state.undo);
                dragCopied = true;
            }
            dragClip.setProperty (IDs::start, snap (dragOrigStart + delta), &state.undo);
            // moving between tracks of the same kind
            bool inAuto; juce::Rectangle<int> row;
            auto target = trackAtY (e.getPosition().y, inAuto, row);
            auto current = Model::trackOfClip (dragClip);
            if (target.isValid() && target != current && ! inAuto)
            {
                const auto ct = dragClip[IDs::type].toString(), tt = target[IDs::type].toString();
                const bool ok = (ct == "audio" && tt == "audio") || (ct == "midi" && tt != "audio") || (ct == "pattern" && tt == "drums");
                if (ok)
                {
                    auto clip = dragClip;
                    current.getChildWithName (IDs::CLIPS).removeChild (clip, &state.undo);
                    target.getOrCreateChildWithName (IDs::CLIPS, nullptr).appendChild (clip, &state.undo);
                    state.selectClip (clip);
                }
            }
            break;
        }
        case Drag::ResizeClip:
        {
            const double minLen = state.snapBeats > 0 ? state.snapBeats : 0.25;
            const double end = juce::jmax (dragOrigStart + minLen, snap (dragOrigStart + dragOrigLength + delta));
            dragClip.setProperty (IDs::length, end - dragOrigStart, &state.undo);
            break;
        }
        case Drag::TrimClipStart:
        {
            const double secPerBeat = 60.0 / state.tempo();
            double ns = snap (dragOrigStart + delta);
            ns = juce::jlimit (dragOrigStart - dragOrigOffset / secPerBeat, dragOrigStart + dragOrigLength - 0.25, ns);
            const double d = ns - dragOrigStart;
            dragClip.setProperty (IDs::start, ns, &state.undo);
            dragClip.setProperty (IDs::length, dragOrigLength - d, &state.undo);
            dragClip.setProperty (IDs::offset, juce::jmax (0.0, dragOrigOffset + d * secPerBeat), &state.undo);
            break;
        }
        case Drag::AutoPoint:
        {
            auto row = trackRow (dragAutoTrack, true).toFloat().reduced (0, 6);
            dragPoint.setProperty (IDs::t, snap (juce::jmax (0.0, beat)), &state.undo);
            dragPoint.setProperty (IDs::val, juce::jlimit (0.0f, 1.0f, (row.getBottom() - e.position.y) / row.getHeight()), &state.undo);
            break;
        }
        default: break;
    }
    repaint();
}

void ArrangementView::mouseUp (const juce::MouseEvent& e)
{
    if (e.eventComponent != &body) return;
    if (drag == Drag::Playhead && std::abs (e.getDistanceFromDragStartX()) <= 4)
    {
        // FL style: a click on the ruler moves the playhead and clears the loop selection
        project.setProperty (IDs::loopOn, false, nullptr);
        state.engine.setPosition (snap (dragDownBeat));
    }
    drag = Drag::None;
    dragClip = {};
    dragPoint = {};
    updateScrollbars();
}

void ArrangementView::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (e.eventComponent != &body || e.x < headerW || e.y < rulerH) return;
    auto clip = clipAt (e.getPosition());
    if (clip.isValid()) { openEditorFor (clip); return; }
    bool inAuto; juce::Rectangle<int> row;
    auto track = trackAtY (e.y, inAuto, row);
    if (! track.isValid() || inAuto) return;
    const auto type = track[IDs::type].toString();
    const double at = std::floor (xToBeat (e.position.x) / state.beatsPerBar()) * state.beatsPerBar();
    if (type == "audio") { importAudio(); return; }
    state.beginEdit ("New clip");
    auto c = type == "drums" ? Model::addPatternClip (track, at, state.beatsPerBar() * 2, &state.undo)
                             : Model::addMidiClip (track, at, state.beatsPerBar() * 2, &state.undo);
    state.selectClip (c);
}

void ArrangementView::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& d)
{
    if (e.mods.isCommandDown())
    {
        const double anchor = xToBeat (e.position.x);
        pixelsPerBeat = juce::jlimit (4.0, 200.0, pixelsPerBeat * std::pow (1.15, d.deltaY * 6.0));
        scrollBeats = juce::jmax (0.0, anchor - (e.position.x - headerW) / pixelsPerBeat);
        zoom.setValue (pixelsPerBeat, juce::dontSendNotification);
    }
    else if (e.mods.isShiftDown() || std::abs (d.deltaX) > std::abs (d.deltaY))
        scrollBeats = juce::jmax (0.0, scrollBeats - (d.deltaX + (e.mods.isShiftDown() ? d.deltaY : 0)) * 400.0 / pixelsPerBeat);
    else
    {
        scrollY = juce::jlimit (0, juce::jmax (0, (int) vScroll.getMaximumRangeLimit() - (body.getHeight() - rulerH)), scrollY - (int) (d.deltaY * 200));
        layoutHeaders();
    }
    updateScrollbars();
    repaint();
}

bool ArrangementView::keyPressed (const juce::KeyPress& k)
{
    if (k == juce::KeyPress::deleteKey || k == juce::KeyPress::backspaceKey) { deleteSelected(); return true; }
    if (k == juce::KeyPress ('d', juce::ModifierKeys::commandModifier, 0)) { duplicateSelected(); return true; }
    if (k == juce::KeyPress ('e', juce::ModifierKeys::commandModifier, 0)) { splitSelectedAtPlayhead(); return true; }
    return false;
}

// ---------------------------------------------------------------------------- commands
void ArrangementView::duplicateSelected()
{
    auto c = state.selectedClip;
    if (! c.isValid()) return;
    state.beginEdit ("Duplicate clip");
    state.selectClip (Model::duplicateClip (c, (double) c[IDs::start] + (double) c[IDs::length], &state.undo));
}

void ArrangementView::deleteSelected()
{
    auto c = state.selectedClip;
    if (! c.isValid()) return;
    state.beginEdit ("Delete clip");
    state.selectClip ({});
    c.getParent().removeChild (c, &state.undo);
}

void ArrangementView::splitSelectedAtPlayhead()
{
    auto c = state.selectedClip;
    if (! c.isValid()) return;
    const double pos = state.engine.getPosition();
    const double start = c[IDs::start], len = c[IDs::length];
    if (pos <= start + 0.01 || pos >= start + len - 0.01) return;
    state.beginEdit ("Split clip");
    auto right = Model::duplicateClip (c, pos, &state.undo);
    const double cut = pos - start;
    c.setProperty (IDs::length, cut, &state.undo);
    right.setProperty (IDs::length, len - cut, &state.undo);
    const auto type = c[IDs::type].toString();
    if (type == "audio")
        right.setProperty (IDs::offset, (double) c.getProperty (IDs::offset, 0.0) + cut * 60.0 / state.tempo(), &state.undo);
    else if (type == "midi")
    {
        auto notesL = Model::readNotes (c), notesR = notesL;
        std::vector<NoteData> l, r;
        for (auto& n : notesL) if (n.start < cut) { n.length = juce::jmin (n.length, cut - n.start); l.push_back (n); }
        for (auto& n : notesR) if (n.start >= cut) { n.start -= cut; r.push_back (n); }
        Model::setNotes (c, l, &state.undo);
        Model::setNotes (right, r, &state.undo);
    }
    state.selectClip (right);
}

void ArrangementView::clipMenu (juce::ValueTree clip)
{
    juce::PopupMenu m;
    const auto type = clip[IDs::type].toString();
    m.addItem (1, "Open editor");
    m.addItem (2, "Duplicate  (Ctrl+D)");
    m.addItem (3, "Split at playhead  (Ctrl+E)");
    m.addItem (4, "Rename...");
    if (type == "pattern") m.addItem (5, "Convert pattern to MIDI clip");
    if (type != "audio") m.addItem (6, "Set loop to clip");
    m.addSeparator();
    m.addItem (9, "Delete");
    m.showMenuAsync (juce::PopupMenu::Options(), [this, clip, type] (int r) mutable
    {
        if (r == 1) openEditorFor (clip);
        if (r == 2) duplicateSelected();
        if (r == 3) splitSelectedAtPlayhead();
        if (r == 4)
        {
            auto* w = new juce::AlertWindow ("Rename clip", {}, juce::MessageBoxIconType::NoIcon);
            w->addTextEditor ("name", clip[IDs::name].toString());
            w->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
            w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
            w->enterModalState (true, juce::ModalCallbackFunction::create ([this, w, clip] (int res) mutable
            {
                if (res == 1) { state.beginEdit ("Rename clip"); clip.setProperty (IDs::name, w->getTextEditorContents ("name"), &state.undo); }
            }), true);
        }
        if (r == 5)
        {
            state.beginEdit ("Convert to MIDI");
            std::vector<Snap::Event> ev;
            Snap::appendPatternClipEvents (clip, ev);
            Snap::sortEvents (ev);
            auto track = Model::trackOfClip (clip);
            auto mc = Model::addMidiClip (track, clip[IDs::start], clip[IDs::length], &state.undo);
            const double cs = clip[IDs::start];
            std::map<juce::uint32, size_t> open;
            std::vector<NoteData> notes;
            for (auto& e : ev)
            {
                if (e.velocity > 0) { NoteData n; n.pitch = e.note; n.start = e.beat - cs; n.velocity = (int) (e.velocity * 127); n.probability = e.probability; n.length = 0.1; open[e.key] = notes.size(); notes.push_back (n); }
                else if (auto it = open.find (e.key); it != open.end()) { notes[it->second].length = juce::jmax (0.02, e.beat - cs - notes[it->second].start); open.erase (it); }
            }
            Model::setNotes (mc, notes, &state.undo);
            clip.getParent().removeChild (clip, &state.undo);
            state.selectClip (mc);
        }
        if (r == 6)
        {
            project.setProperty (IDs::loopStart, clip[IDs::start], nullptr);
            project.setProperty (IDs::loopEnd, (double) clip[IDs::start] + (double) clip[IDs::length], nullptr);
            project.setProperty (IDs::loopOn, true, nullptr);
        }
        if (r == 9) deleteSelected();
    });
}

void ArrangementView::openEditorFor (juce::ValueTree clip)
{
    state.selectClip (clip);
    if (! state.showWorkspace) return;
    const auto type = clip[IDs::type].toString();
    state.showWorkspace (type == "audio" ? AppState::AudioEditor : (type == "pattern" ? AppState::Drums : AppState::PianoRoll));
}

void ArrangementView::addTrack (const juce::String& type)
{
    state.beginEdit ("Add track");
    const int n = Model::tracks (project).getNumChildren() + 1;
    auto t = Model::addTrack (project, type, (type == "synth" ? "Synth " : (type == "drums" ? "Drums " : "Audio ")) + juce::String (n), &state.undo);
    state.selectTrack (t);
}

void ArrangementView::addAudioFileToTrack (const juce::File& f, juce::ValueTree track, double beat)
{
    auto data = AudioPool::get().load (f);
    if (data == nullptr) { if (state.setStatus) state.setStatus ("Could not read " + f.getFileName()); return; }
    state.beginEdit ("Import audio");
    if (! track.isValid() || track[IDs::type].toString() != "audio")
        track = Model::addTrack (project, "audio", f.getFileNameWithoutExtension(), &state.undo);
    const double beats = data->numFrames() / data->sampleRate * state.tempo() / 60.0;
    state.selectClip (Model::addAudioClip (track, f, beat, beats, &state.undo));
}

void ArrangementView::importAudio()
{
    chooser = std::make_unique<juce::FileChooser> ("Import audio", juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::canSelectMultipleItems,
        [this] (const juce::FileChooser& fc)
        {
            auto track = state.selectedTrack;
            double at = snap (state.engine.getPosition());
            for (auto& f : fc.getResults())
            {
                addAudioFileToTrack (f, track, at);
                track = state.selectedTrack;
                if (state.selectedClip.isValid()) at = (double) state.selectedClip[IDs::start] + (double) state.selectedClip[IDs::length];
            }
        });
}

bool ArrangementView::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (auto& f : files)
        if (juce::File (f).hasFileExtension ("wav;aif;aiff;flac;mp3;ogg")) return true;
    return false;
}

void ArrangementView::filesDropped (const juce::StringArray& files, int x, int y)
{
    auto p = body.getLocalPoint (this, juce::Point<int> (x, y));
    bool inAuto; juce::Rectangle<int> row;
    auto track = trackAtY (p.y, inAuto, row);
    double at = snap (xToBeat ((float) p.x));
    for (auto& f : files)
    {
        addAudioFileToTrack (juce::File (f), track, at);
        track = state.selectedTrack;
        if (state.selectedClip.isValid()) at = (double) state.selectedClip[IDs::start] + (double) state.selectedClip[IDs::length];
    }
}
