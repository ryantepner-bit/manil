#include "BreakPage.h"
#include "../Live/LiveSequencer.h"

// ============================================================================ waveform + markers
std::vector<float> BreakWaveView::slices() const
{
    auto p = BreakEngine::decodeSlices (track.getChildWithName (IDs::BREAK)[IDs::slices].toString());
    if (p.size() < 2)
        if (auto* tp = state.engine.getTrackProcessor (track.isValid() ? (int) track[IDs::id] : -1); tp != nullptr && tp->breaks) p = tp->breaks->getSlices();
    return p;
}

void BreakWaveView::writeSlices (std::vector<float> p, const juce::String& what)
{
    std::sort (p.begin(), p.end());
    p.erase (std::unique (p.begin(), p.end(), [] (float a, float b) { return std::abs (a - b) < 1e-4f; }), p.end());
    if (p.size() < 2) p = { 0.0f, 1.0f };
    p.front() = 0.0f; p.back() = 1.0f;
    state.beginEdit (what);
    track.getChildWithName (IDs::BREAK).setProperty (IDs::slices, BreakEngine::encodeSlices (p), &state.undo);
    repaint();
}

int BreakWaveView::markerAt (float x) const
{
    const auto p = slices();
    for (size_t i = 1; i + 1 < p.size(); ++i)
        if (std::abs (p[i] * getWidth() - x) < 6.0f) return (int) i;
    return -1;
}

int BreakWaveView::sliceAt (float x) const
{
    const auto p = slices();
    const float t = x / juce::jmax (1, getWidth());
    for (size_t i = 0; i + 1 < p.size(); ++i) if (t >= p[i] && t < p[i + 1]) return (int) i;
    return -1;
}

void BreakWaveView::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (Theme::bg);
    g.fillRoundedRectangle (r, 5.0f);
    if (data == nullptr)
    {
        g.setColour (Theme::textDim);
        g.drawText ("Drop a break here or press 'Load break...'", getLocalBounds(), juce::Justification::centred);
        return;
    }
    const auto p = slices();
    int playing = -1;
    if (auto* tp = state.engine.getTrackProcessor ((int) track[IDs::id]); tp != nullptr && tp->breaks) playing = tp->breaks->playingSlice.load();
    for (size_t i = 0; i + 1 < p.size(); ++i)
    {
        const float x0 = p[i] * r.getWidth(), x1 = p[i + 1] * r.getWidth();
        g.setColour ((int) i == playing ? Theme::accent2.withAlpha (0.28f) : (i % 2 ? Theme::panel : Theme::panelLight).withAlpha (0.5f));
        g.fillRect (x0, 0.0f, x1 - x0, r.getHeight());
    }
    WaveformCache::draw (g, *data, r.reduced (2.0f, 6.0f), 0.0, data->numFrames() / data->sampleRate, Theme::accent);
    g.setFont (Theme::font (10.0f, true));
    for (size_t i = 0; i + 1 < p.size(); ++i)
    {
        const float x = p[i] * r.getWidth();
        g.setColour (Theme::warn);
        g.drawVerticalLine ((int) x, 0.0f, r.getHeight());
        g.fillRect (x, 0.0f, 16.0f, 13.0f);
        g.setColour (Theme::bg);
        g.drawText (juce::String ((int) i + 1), juce::Rectangle<float> (x, 0.0f, 16.0f, 13.0f), juce::Justification::centred);
    }
}

void BreakWaveView::mouseMove (const juce::MouseEvent& e)
{
    setMouseCursor (markerAt (e.position.x) >= 0 ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::PointingHandCursor);
}

void BreakWaveView::mouseDown (const juce::MouseEvent& e)
{
    if (! track.isValid() || data == nullptr) return;
    const int m = markerAt (e.position.x);
    if (e.mods.isPopupMenu())
    {
        if (m >= 0) { auto p = slices(); p.erase (p.begin() + m); writeSlices (p, "Remove slice"); }
        return;
    }
    if (m >= 0) { dragMarker = m; state.beginEdit ("Move slice"); return; }
    const int s = sliceAt (e.position.x);
    if (s >= 0) state.engine.sendLiveNote (track[IDs::id], BreakEngine::baseNote + s, 1.0f);
}

void BreakWaveView::mouseDrag (const juce::MouseEvent& e)
{
    if (dragMarker < 0) return;
    auto p = slices();
    if (dragMarker >= (int) p.size() - 1) return;
    p[(size_t) dragMarker] = juce::jlimit (p[(size_t) dragMarker - 1] + 0.002f, p[(size_t) dragMarker + 1] - 0.002f, e.position.x / (float) getWidth());
    track.getChildWithName (IDs::BREAK).setProperty (IDs::slices, BreakEngine::encodeSlices (p), &state.undo);
    repaint();
}

void BreakWaveView::mouseUp (const juce::MouseEvent&) { dragMarker = -1; }

void BreakWaveView::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (! track.isValid() || markerAt (e.position.x) >= 0) return;
    auto p = slices();
    if ((int) p.size() > BreakEngine::maxSlices) return;
    p.push_back (juce::jlimit (0.001f, 0.999f, e.position.x / (float) getWidth()));
    writeSlices (p, "Add slice");
}

// ============================================================================ page
BreakPage::BreakPage (AppState& s, Composer& c) : state (s), composer (c), wave (s)
{
    title.setFont (Theme::font (15.0f, true));
    addAndMakeVisible (title);
    info.setFont (Theme::font (12.0f));
    info.setColour (juce::Label::textColourId, Theme::textDim);
    info.setJustificationType (juce::Justification::topLeft);
    addAndMakeVisible (info);
    trackBox.onChange = [this] { bindTrack (Model::findTrack (project, trackBox.getSelectedId() - 1)); };
    addAndMakeVisible (trackBox);

    loadBtn.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser> ("Load a drum break", juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this] (const juce::FileChooser& fc) { if (fc.getResult().existsAsFile()) loadBreak (fc.getResult()); });
    };
    builtinBtn.onClick = [this] { loadBreak ({}); };
    addTrackBtn.onClick = [this]
    {
        state.beginEdit ("Add break track");
        auto t = Model::addTrack (project, "break", "Break", &state.undo);
        // the first break track is the one Live mode plays
        bool taken = false;
        for (auto tr : Model::tracks (project)) taken |= (int) tr.getProperty (IDs::livePart, 0) == (int) Live::Break;
        if (! taken) t.setProperty (IDs::livePart, (int) Live::Break, &state.undo);
        refresh();
        bindTrack (t);
    };
    wave.onFileDropped = [this] (const juce::File& f) { loadBreak (f); };
    autoBtn.onClick = [this] { chop (0); };
    eq8.onClick = [this] { chop (8); };
    eq16.onClick = [this] { chop (16); };
    eq32.onClick = [this] { chop (32); };
    autoBtn.setTooltip ("Cut at the hits (transient detection). More sensitivity = more slices");
    toClip.onClick = [this] { writeClip (false); };
    rechopClip.onClick = [this] { writeClip (true); };
    toClip.setTooltip ("Write the slices in their original order as a MIDI clip (play it back as-is, then edit the notes in the piano roll)");
    rechopClip.setTooltip ("Write a re-chopped version decided by the fractal (like the Live Break part) as a MIDI clip");
    sensitivity.setSliderStyle (juce::Slider::LinearHorizontal);
    sensitivity.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    sensitivity.setRange (0.0, 1.0);
    sensitivity.onDragEnd = [this] { if (breakNode().isValid()) { breakNode().setProperty (IDs::sensitivity, sensitivity.getValue(), nullptr); chop (0); } };
    sensL.setText ("Sensitivity", juce::dontSendNotification);
    sensL.setFont (Theme::font (11.0f));
    sensL.setColour (juce::Label::textColourId, Theme::textDim);
    for (auto* b : std::initializer_list<juce::Component*> { &loadBtn, &builtinBtn, &addTrackBtn, &autoBtn, &eq8, &eq16, &eq32, &toClip, &rechopClip, &sensitivity, &sensL, &syncBtn, &bpm, &tune, &fade, &gain, &wave })
        addAndMakeVisible (b);
    syncBtn.setTooltip ("Speed follows the project tempo (break bpm -> project bpm)");
    for (auto* k : { &bpm, &tune, &fade, &gain }) k->setColour (Theme::warn);
    for (int i = 0; i < 16; ++i)
    {
        auto* b = pads.add (new juce::TextButton (juce::String (i + 1)));
        b->onClick = [this, i] { if (track.isValid()) state.engine.sendLiveNote (track[IDs::id], BreakEngine::baseNote + i, 1.0f); };
        addAndMakeVisible (b);
    }
    state.selectionChanged.addChangeListener (this);
    startTimerHz (20);
}

BreakPage::~BreakPage()
{
    state.selectionChanged.removeChangeListener (this);
    if (project.isValid()) project.removeListener (this);
}

void BreakPage::setProject (juce::ValueTree p)
{
    if (project.isValid()) project.removeListener (this);
    project = p;
    project.addListener (this);
    track = {};
    refresh();
}

void BreakPage::refresh()
{
    trackBox.clear (juce::dontSendNotification);
    juce::ValueTree first;
    for (auto t : Model::tracks (project))
        if (t[IDs::type].toString() == "break") { trackBox.addItem (t[IDs::name].toString(), (int) t[IDs::id] + 1); if (! first.isValid()) first = t; }
    if (! track.isValid() || ! track.getParent().isValid()) track = first;
    bindTrack (track);
}

void BreakPage::bindTrack (juce::ValueTree t)
{
    track = t;
    const bool ok = track.isValid();
    if (ok) trackBox.setSelectedId ((int) track[IDs::id] + 1, juce::dontSendNotification);
    auto b = breakNode();
    if (ok)
    {
        syncBtn.bind (b, IDs::sync, &state.undo);
        bpm.bind (b, IDs::bpm, &state.undo);
        tune.bind (b, IDs::tune, &state.undo);
        fade.bind (b, IDs::fade, &state.undo);
        gain.bind (b, IDs::gain, &state.undo);
        sensitivity.setValue ((double) b.getProperty (IDs::sensitivity, 0.5f), juce::dontSendNotification);
    }
    else for (auto* k : { &bpm, &tune, &fade, &gain }) k->unbind();
    for (auto* c : std::initializer_list<juce::Component*> { &loadBtn, &builtinBtn, &autoBtn, &eq8, &eq16, &eq32, &toClip, &rechopClip, &syncBtn, &sensitivity })
        c->setEnabled (ok);
    wave.setTrack (track);
    wave.data = nullptr;
    if (auto* tp = state.engine.getTrackProcessor (ok ? (int) track[IDs::id] : -1); tp != nullptr && tp->breaks) wave.data = tp->breaks->getSample();
    if (ok && wave.data == nullptr) wave.data = b[IDs::file].toString().isNotEmpty() ? AudioPool::get().load (juce::File (b[IDs::file].toString())) : Engine::defaultBreak();
    const auto file = b[IDs::file].toString();
    title.setText (ok ? "BREAK CHOPPER  /  " + track[IDs::name].toString() + "   " + (file.isNotEmpty() ? juce::File (file).getFileName() : juce::String ("(built-in break)"))
                      : "No break track - press '+ Break track'", juce::dontSendNotification);
    const bool isLive = ok && (int) track.getProperty (IDs::livePart, 0) == (int) Live::Break;
    info.setText (juce::String ("Click a slice (or a pad) to hear it. Drag the yellow markers, double-click to add one, right-click to remove one.\n")
                  + "Slices play from MIDI note C1 (36) upwards, so the piano roll and the Arrangement can sequence them.\n"
                  + (isLive ? "Live mode plays this break with the fractal: pick the Breakbeat style or switch the Break part on on the Live page."
                            : "Live mode plays the first break track; this one is for the Arrangement."), juce::dontSendNotification);
    wave.repaint();
    resized();
}

void BreakPage::loadBreak (const juce::File& f)
{
    if (! track.isValid()) return;
    std::shared_ptr<const SampleData> d = f == juce::File() ? Engine::defaultBreak() : AudioPool::get().load (f);
    if (d == nullptr) { if (state.setStatus) state.setStatus ("Could not read " + f.getFileName()); return; }
    state.beginEdit ("Load break");
    auto b = breakNode();
    const auto sl = BreakEngine::detectSlices (*d, (float) b.getProperty (IDs::sensitivity, 0.5f));
    b.setProperty (IDs::file, f == juce::File() ? juce::String() : f.getFullPathName(), &state.undo);
    b.setProperty (IDs::bpm, BreakEngine::guessBpm (*d), &state.undo);
    b.setProperty (IDs::slices, BreakEngine::encodeSlices (sl), &state.undo);
    if (state.setStatus) state.setStatus ("Loaded " + (f == juce::File() ? juce::String ("the built-in break") : f.getFileName()) + ": " + juce::String ((int) sl.size() - 1)
                                          + " slices, about " + juce::String (BreakEngine::guessBpm (*d), 1) + " bpm (change Break BPM if it is off)");
    bindTrack (track);
}

void BreakPage::chop (int equalCount)
{
    if (! track.isValid() || wave.data == nullptr) return;
    state.beginEdit ("Chop break");
    const auto sl = equalCount > 0 ? BreakEngine::equalSlices (equalCount) : BreakEngine::detectSlices (*wave.data, (float) sensitivity.getValue());
    breakNode().setProperty (IDs::slices, BreakEngine::encodeSlices (sl), &state.undo);
    if (state.setStatus) state.setStatus ("Chopped into " + juce::String ((int) sl.size() - 1) + " slices");
    wave.repaint();
}

void BreakPage::writeClip (bool rechop)
{
    if (! track.isValid() || wave.data == nullptr) return;
    const auto sl = BreakEngine::decodeSlices (breakNode()[IDs::slices].toString()).size() >= 2 ? BreakEngine::decodeSlices (breakNode()[IDs::slices].toString())
                                                                                              : BreakEngine::detectSlices (*wave.data, 0.5f);
    const double secs = wave.data->numFrames() / wave.data->sampleRate;
    const double beats = std::round (secs * (double) breakNode().getProperty (IDs::bpm, 140.0f) / 60.0 * 4.0) / 4.0;
    const double bpb = state.beatsPerBar();
    const double clipLen = juce::jmax (bpb, std::ceil (beats / bpb) * bpb) * (rechop ? 2.0 : 1.0);
    std::vector<NoteData> notes;
    const int n = (int) sl.size() - 1;
    if (! rechop)
    {
        // like SlicerT's "copy MIDI": every slice at its own position, original order
        for (int i = 0; i < n; ++i)
        {
            NoteData d; d.pitch = BreakEngine::baseNote + i; d.start = sl[(size_t) i] * beats; d.length = juce::jmax (1.0 / 16.0, (sl[(size_t) i + 1] - sl[(size_t) i]) * beats);
            d.velocity = 110; notes.push_back (d);
        }
    }
    else
    {
        // fractal re-chop: walk 16ths, keep / swap / stutter slices by the orbit at the probe
        const auto& f = state.features;
        juce::Random r ((juce::int64) f.hash());
        for (double b = 0.0; b < clipLen - 1e-6; b += 0.25)
        {
            const double pos = std::fmod (b, beats);
            int orig = -1;
            for (int i = 0; i < n; ++i) if (std::abs (sl[(size_t) i] * beats - pos) < 0.13) { orig = i; break; }
            const bool fill = std::fmod (b, bpb * 2) >= bpb * 2 - 1.0;
            const float chop = (float) juce::jlimit (0.1, 0.9, 0.2 + f.chaos * 0.5 + f.roughness * 0.3);
            const int idx = (int) (std::floor (b * 4.0)) % juce::jmax (1, (int) f.contour.size());
            const int pick = juce::jlimit (0, n - 1, (int) (f.contour.empty() ? r.nextFloat() * n : f.contour[(size_t) idx] * n));
            NoteData d; d.length = 0.25; d.velocity = 100;
            if (fill && r.nextFloat() < chop) { d.pitch = BreakEngine::baseNote + pick; d.start = b; notes.push_back (d); d.start = b + 0.125; d.velocity = 80; notes.push_back (d); continue; }
            if (orig < 0) continue;
            d.pitch = BreakEngine::baseNote + (r.nextFloat() < chop * 0.7f ? pick : orig);
            d.start = b;
            notes.push_back (d);
        }
    }
    state.beginEdit (rechop ? "Fractal re-chop" : "Chops to clip");
    const double at = Model::freeSpot (track, std::floor (state.engine.getPosition() / bpb) * bpb, clipLen, bpb);
    auto clip = Model::addMidiClip (track, at, clipLen, &state.undo);
    clip.setProperty (IDs::name, rechop ? "Re-chop" : "Chops", &state.undo);
    Model::setNotes (clip, notes, &state.undo);
    state.selectClip (clip);
    if (state.setStatus) state.setStatus ("Wrote " + juce::String ((int) notes.size()) + " slice notes to the Arrangement (bar " + juce::String ((int) (at / bpb) + 1) + ")");
}

void BreakPage::valueTreePropertyChanged (juce::ValueTree& t, const juce::Identifier& p)
{
    if (t.hasType (IDs::BREAK) && t.getParent() == track)
    {
        if (p == IDs::file) juce::MessageManager::callAsync ([sp = juce::Component::SafePointer<BreakPage> (this)] { if (sp) sp->bindTrack (sp->track); });
        else wave.repaint();
    }
    if (t.hasType (IDs::TRACK) && p == IDs::name) refresh();
}

void BreakPage::changeListenerCallback (juce::ChangeBroadcaster*)
{
    if (state.selectedTrack.isValid() && state.selectedTrack[IDs::type].toString() == "break" && state.selectedTrack != track) bindTrack (state.selectedTrack);
}

void BreakPage::timerCallback()
{
    int playing = -1;
    if (auto* tp = state.engine.getTrackProcessor (track.isValid() ? (int) track[IDs::id] : -1); tp != nullptr && tp->breaks)
    {
        playing = tp->breaks->playingSlice.load();
        if (wave.data != tp->breaks->getSample() && tp->breaks->getSample() != nullptr) { wave.data = tp->breaks->getSample(); wave.repaint(); }
    }
    if (playing != lastPlaying)
    {
        lastPlaying = playing;
        for (int i = 0; i < pads.size(); ++i) pads[i]->setColour (juce::TextButton::buttonColourId, i == playing ? Theme::accent2.withAlpha (0.7f) : Theme::panelLight);
        wave.repaint();
    }
    const int n = (int) BreakEngine::decodeSlices (breakNode()[IDs::slices].toString()).size() - 1;
    for (int i = 0; i < pads.size(); ++i) pads[i]->setAlpha (n <= 0 || i < n ? 1.0f : 0.35f);
}

void BreakPage::paint (juce::Graphics& g)
{
    g.fillAll (Theme::bg);
    auto r = getLocalBounds().reduced (8);
    r.removeFromTop (40);
    Theme::drawPanel (g, r.removeFromBottom (170).toFloat(), "Slices");
}

void BreakPage::resized()
{
    auto r = getLocalBounds().reduced (8);
    auto top = r.removeFromTop (34);
    title.setBounds (top.removeFromLeft (420));
    trackBox.setBounds (top.removeFromLeft (150).reduced (0, 3)); top.removeFromLeft (8);
    loadBtn.setBounds (top.removeFromLeft (110).reduced (0, 2)); top.removeFromLeft (4);
    builtinBtn.setBounds (top.removeFromLeft (110).reduced (0, 2)); top.removeFromLeft (4);
    addTrackBtn.setBounds (top.removeFromRight (110).reduced (0, 2));
    r.removeFromTop (6);
    auto bottom = r.removeFromBottom (170).reduced (12, 24);
    r.removeFromBottom (6);
    auto controls = r.removeFromBottom (86);
    r.removeFromBottom (6);
    wave.setBounds (r);

    auto row = controls.removeFromTop (30);
    sensL.setBounds (row.removeFromLeft (70));
    sensitivity.setBounds (row.removeFromLeft (150)); row.removeFromLeft (6);
    autoBtn.setBounds (row.removeFromLeft (90)); row.removeFromLeft (10);
    for (auto* b : { &eq8, &eq16, &eq32 }) { b->setBounds (row.removeFromLeft (40)); row.removeFromLeft (4); }
    row.removeFromLeft (16);
    syncBtn.setBounds (row.removeFromLeft (120)); row.removeFromLeft (16);
    toClip.setBounds (row.removeFromRight (180)); row.removeFromRight (6);
    rechopClip.setBounds (row.removeFromRight (180));
    auto knobs = controls;
    for (auto* k : { &bpm, &tune, &fade, &gain }) { k->setBounds (knobs.removeFromLeft (74)); }
    knobs.removeFromLeft (16);
    info.setBounds (knobs);

    const int pw = bottom.getWidth() / 16;
    for (int i = 0; i < pads.size(); ++i) pads[i]->setBounds (bottom.getX() + i * pw, bottom.getY(), pw, bottom.getHeight());
    for (auto* p : pads) p->setBounds (p->getBounds().reduced (3));
}
