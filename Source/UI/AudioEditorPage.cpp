#include "AudioEditorPage.h"

// ============================================================================ WaveEditor
WaveEditor::WaveEditor (AppState& s) : state (s) { startTimerHz (25); }

void WaveEditor::setData (std::shared_ptr<const SampleData> d)
{
    const bool same = data != nullptr && d != nullptr && data->path == d->path;
    data = d;
    if (! same) { selStart = selEnd = 0; cursor = 0; zoomToFit(); }
    else { viewEnd = juce::jmin (viewEnd, length()); repaint(); }
}

void WaveEditor::paint (juce::Graphics& g)
{
    g.fillAll (Theme::bg);
    if (data == nullptr)
    {
        g.setColour (Theme::textDim);
        g.drawText ("Select an audio clip in the Arrangement (double-click it) or open a file", getLocalBounds(), juce::Justification::centred);
        return;
    }
    // time grid
    const double span = viewEnd - viewStart;
    double step = 0.001;
    while (span / step > 20) step *= (std::fmod (std::log10 (step) + 10.0, 1.0) < 0.1 ? 5.0 : 2.0);
    g.setFont (Theme::font (10.0f));
    for (double t = std::floor (viewStart / step) * step; t < viewEnd; t += step)
    {
        const float x = timeToX (t);
        g.setColour (Theme::grid);
        g.drawVerticalLine ((int) x, 0, (float) getHeight());
        g.setColour (Theme::textDim);
        g.drawText (juce::String (t, step < 0.1 ? 3 : 2) + "s", (int) x + 3, 2, 60, 12, juce::Justification::centredLeft);
    }
    if (regionStart >= 0)
    {
        g.setColour (Theme::accent.withAlpha (0.06f));
        g.fillRect (juce::Rectangle<float> (timeToX (regionStart), 0, timeToX (regionEnd) - timeToX (regionStart), (float) getHeight()));
    }
    const int chans = data->buffer.getNumChannels();
    auto area = getLocalBounds().toFloat().withTrimmedTop (16);
    for (int c = 0; c < chans; ++c)
    {
        auto lane = area.removeFromTop (area.getHeight() / (chans - c));
        // per-channel waveform: draw from the right channel via a temporary view
        if (c == 0) WaveformCache::draw (g, *data, lane.reduced (0, 4), viewStart, viewEnd, Theme::accent.withAlpha (0.85f));
        else
        {
            // second channel drawn directly (no cache) at the visible resolution
            const float mid = lane.getCentreY(), half = lane.getHeight() * 0.45f;
            g.setColour (Theme::accent3.withAlpha (0.85f));
            for (int x = 0; x < getWidth(); ++x)
            {
                const int s0 = juce::jlimit (0, data->numFrames() - 1, (int) (xToTime ((float) x) * data->sampleRate));
                const int s1 = juce::jlimit (s0 + 1, data->numFrames(), (int) (xToTime ((float) x + 1) * data->sampleRate) + 1);
                float lo = 0, hi = 0;
                const int stride = juce::jmax (1, (s1 - s0) / 64);
                for (int i = s0; i < s1; i += stride) { const float v = data->buffer.getSample (1, i); lo = juce::jmin (lo, v); hi = juce::jmax (hi, v); }
                g.drawVerticalLine (x, mid - hi * half, mid - lo * half + 1.0f);
            }
        }
        g.setColour (Theme::outline);
        g.drawHorizontalLine ((int) lane.getCentreY(), 0, (float) getWidth());
    }
    if (selEnd > selStart)
    {
        g.setColour (Theme::warn.withAlpha (0.18f));
        g.fillRect (juce::Rectangle<float> (timeToX (selStart), 0, timeToX (selEnd) - timeToX (selStart), (float) getHeight()));
        g.setColour (Theme::warn);
        g.drawVerticalLine ((int) timeToX (selStart), 0, (float) getHeight());
        g.drawVerticalLine ((int) timeToX (selEnd), 0, (float) getHeight());
    }
    g.setColour (Theme::text);
    g.drawVerticalLine ((int) timeToX (cursor), 0, (float) getHeight());
    const double pp = state.engine.getPreviewPosition();
    if (pp >= 0)
    {
        g.setColour (Theme::accent2);
        g.drawVerticalLine ((int) timeToX (pp / data->sampleRate), 0, (float) getHeight());
    }
}

void WaveEditor::mouseDown (const juce::MouseEvent& e)
{
    if (data == nullptr) return;
    downTime = juce::jlimit (0.0, length(), xToTime (e.position.x));
    if (e.mods.isShiftDown() && selEnd > selStart)
    {
        if (std::abs (downTime - selStart) < std::abs (downTime - selEnd)) selStart = downTime; else selEnd = downTime;
        downTime = std::abs (downTime - selStart) < 1e-9 ? selEnd : selStart;
    }
    else { cursor = downTime; selStart = selEnd = downTime; }
    if (onSelectionChanged) onSelectionChanged();
    repaint();
}

void WaveEditor::mouseDrag (const juce::MouseEvent& e)
{
    if (data == nullptr) return;
    const double t = juce::jlimit (0.0, length(), xToTime (e.position.x));
    selStart = juce::jmin (downTime, t);
    selEnd = juce::jmax (downTime, t);
    if (onSelectionChanged) onSelectionChanged();
    repaint();
}

void WaveEditor::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& d)
{
    if (data == nullptr) return;
    const double span = viewEnd - viewStart;
    if (e.mods.isCommandDown() || std::abs (d.deltaY) > std::abs (d.deltaX))
    {
        const double anchor = xToTime (e.position.x);
        const double ns = juce::jlimit (0.002, length(), span * std::pow (0.85, d.deltaY * 5.0));
        const double frac = (anchor - viewStart) / span;
        viewStart = juce::jlimit (0.0, juce::jmax (0.0, length() - ns), anchor - frac * ns);
        viewEnd = viewStart + ns;
    }
    else
    {
        const double sh = -d.deltaX * span;
        viewStart = juce::jlimit (0.0, juce::jmax (0.0, length() - span), viewStart + sh);
        viewEnd = viewStart + span;
    }
    repaint();
}

// ============================================================================ page
AudioEditorPage::AudioEditorPage (AppState& s) : state (s), wave (s)
{
    addAndMakeVisible (wave);
    wave.onSelectionChanged = [this] { updateInfo(); };
    info.setFont (Theme::mono (12.0f));
    addAndMakeVisible (info);
    for (auto* b : { &openBtn, &toArr, &play, &stop, &fit, &trim, &normalize, &reverse, &fadeIn, &fadeOut, &gainUp, &gainDown, &silence, &del, &crop })
        addAndMakeVisible (b);
    openBtn.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser> ("Open audio file", juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this] (const juce::FileChooser& fc) { if (fc.getResult().existsAsFile()) { clip = {}; openFile (fc.getResult()); } });
    };
    toArr.onClick = [this]
    {
        if (! file.existsAsFile()) return;
        auto track = state.selectedTrack;
        if (! track.isValid() || track[IDs::type].toString() != "audio")
            for (auto t : Model::tracks (project)) if (t[IDs::type].toString() == "audio") { track = t; break; }
        state.beginEdit ("Add audio clip");
        if (! track.isValid()) track = Model::addTrack (project, "audio", file.getFileNameWithoutExtension(), &state.undo);
        auto d = wave.getData();
        const double beats = d->numFrames() / d->sampleRate * state.tempo() / 60.0;
        state.selectClip (Model::addAudioClip (track, file, std::floor (state.engine.getPosition()), beats, &state.undo));
    };
    play.setColour (juce::TextButton::buttonColourId, Theme::good.withAlpha (0.25f));
    play.onClick = [this]
    {
        auto d = wave.getData();
        if (d == nullptr) return;
        const auto s0 = (juce::int64) ((wave.selEnd > wave.selStart ? wave.selStart : wave.cursor) * d->sampleRate);
        const auto s1 = wave.selEnd > wave.selStart ? (juce::int64) (wave.selEnd * d->sampleRate) : (juce::int64) -1;
        state.engine.previewSample (d, s0, s1);
    };
    stop.onClick = [this] { state.engine.stopPreview(); };
    fit.onClick = [this] { wave.zoomToFit(); };
    trim.onClick = [this]
    {
        if (! clip.isValid() || wave.selEnd <= wave.selStart) { if (state.setStatus) state.setStatus ("Select a region of an arrangement clip first"); return; }
        state.beginEdit ("Trim clip");
        const double secPerBeat = 60.0 / state.tempo();
        const double oldOffset = clip.getProperty (IDs::offset, 0.0);
        const double newStart = (double) clip[IDs::start] + (wave.selStart - oldOffset) / secPerBeat;
        clip.setProperty (IDs::offset, wave.selStart, &state.undo);
        clip.setProperty (IDs::start, juce::jmax (0.0, newStart), &state.undo);
        clip.setProperty (IDs::length, (wave.selEnd - wave.selStart) / secPerBeat, &state.undo);
    };
    normalize.onClick = [this] { applyOp (Op::Normalize); };
    reverse.onClick = [this] { applyOp (Op::Reverse); };
    fadeIn.onClick = [this] { applyOp (Op::FadeIn); };
    fadeOut.onClick = [this] { applyOp (Op::FadeOut); };
    gainUp.onClick = [this] { applyOp (Op::GainUp); };
    gainDown.onClick = [this] { applyOp (Op::GainDown); };
    silence.onClick = [this] { applyOp (Op::Silence); };
    del.onClick = [this] { applyOp (Op::Delete); };
    crop.onClick = [this] { applyOp (Op::Crop); };
    state.selectionChanged.addChangeListener (this);
}

AudioEditorPage::~AudioEditorPage()
{
    state.selectionChanged.removeChangeListener (this);
    if (project.isValid()) project.removeListener (this);
}

void AudioEditorPage::setProject (juce::ValueTree p)
{
    if (project.isValid()) project.removeListener (this);
    project = p;
    project.addListener (this);
    clip = {}; file = juce::File();
    wave.setData (nullptr);
    updateInfo();
}

void AudioEditorPage::changeListenerCallback (juce::ChangeBroadcaster*)
{
    auto c = state.selectedClip;
    if (c.isValid() && c[IDs::type].toString() == "audio" && c != clip) { clip = c; loadFromClip(); }
}

void AudioEditorPage::valueTreePropertyChanged (juce::ValueTree& t, const juce::Identifier& p)
{
    if (t == clip && (p == IDs::file || p == IDs::offset || p == IDs::length)) loadFromClip();
}

void AudioEditorPage::loadFromClip()
{
    if (! clip.isValid()) return;
    openFile (juce::File (clip[IDs::file].toString()));
    const double off = clip.getProperty (IDs::offset, 0.0);
    wave.regionStart = off;
    wave.regionEnd = off + (double) clip[IDs::length] * 60.0 / state.tempo();
    updateInfo();
}

void AudioEditorPage::openFile (const juce::File& f)
{
    file = f;
    wave.setData (AudioPool::get().load (f));
    if (! clip.isValid()) wave.regionStart = wave.regionEnd = -1;
    updateInfo();
}

void AudioEditorPage::updateInfo()
{
    auto d = wave.getData();
    if (d == nullptr) { info.setText ("No audio loaded", juce::dontSendNotification); return; }
    juce::String t;
    t << file.getFileName() << "   " << juce::String (d->numFrames() / d->sampleRate, 3) << " s   " << (int) d->sampleRate << " Hz   " << d->buffer.getNumChannels() << " ch";
    if (wave.selEnd > wave.selStart) t << "     selection " << juce::String (wave.selStart, 3) << " - " << juce::String (wave.selEnd, 3) << " s";
    if (clip.isValid()) t << "     clip '" << clip[IDs::name].toString() << "'";
    info.setText (t, juce::dontSendNotification);
}

void AudioEditorPage::range (int& s0, int& s1) const
{
    auto d = wave.getData();
    if (wave.selEnd > wave.selStart)
    {
        s0 = juce::jlimit (0, d->numFrames(), (int) (wave.selStart * d->sampleRate));
        s1 = juce::jlimit (s0, d->numFrames(), (int) (wave.selEnd * d->sampleRate));
    }
    else { s0 = 0; s1 = d->numFrames(); }
}

void AudioEditorPage::applyOp (Op op)
{
    auto d = wave.getData();
    if (d == nullptr) return;
    int s0, s1;
    range (s0, s1);
    if (s1 <= s0) return;
    const bool hasSel = wave.selEnd > wave.selStart;
    if ((op == Op::Delete || op == Op::Crop) && ! hasSel) { if (state.setStatus) state.setStatus ("Select a region first"); return; }

    // destructive edits write a new file (originals are never overwritten), so undo restores the previous file
    juce::AudioBuffer<float> buf;
    const int chans = d->buffer.getNumChannels();
    if (op == Op::Delete)
    {
        buf.setSize (chans, juce::jmax (1, d->numFrames() - (s1 - s0)));
        for (int c = 0; c < chans; ++c)
        {
            buf.copyFrom (c, 0, d->buffer, c, 0, s0);
            buf.copyFrom (c, s0, d->buffer, c, s1, d->numFrames() - s1);
        }
    }
    else if (op == Op::Crop)
    {
        buf.setSize (chans, s1 - s0);
        for (int c = 0; c < chans; ++c) buf.copyFrom (c, 0, d->buffer, c, s0, s1 - s0);
    }
    else
    {
        buf.makeCopyOf (d->buffer);
        const int n = s1 - s0;
        for (int c = 0; c < chans; ++c)
        {
            float* x = buf.getWritePointer (c, s0);
            switch (op)
            {
                case Op::Normalize: { const float pk = buf.getMagnitude (s0, n); if (pk > 0) juce::FloatVectorOperations::multiply (x, 0.98f / pk, n); break; }
                case Op::Reverse: std::reverse (x, x + n); break;
                case Op::FadeIn: for (int i = 0; i < n; ++i) x[i] *= (float) i / n; break;
                case Op::FadeOut: for (int i = 0; i < n; ++i) x[i] *= 1.0f - (float) i / n; break;
                case Op::GainUp: juce::FloatVectorOperations::multiply (x, 1.4125f, n); break;
                case Op::GainDown: juce::FloatVectorOperations::multiply (x, 0.7079f, n); break;
                case Op::Silence: juce::FloatVectorOperations::clear (x, n); break;
                default: break;
            }
        }
        if (op == Op::Normalize)
        {
            // normalise linked across channels
            buf.makeCopyOf (d->buffer);
            const float pk = buf.getMagnitude (s0, n);
            if (pk > 0) buf.applyGain (s0, n, 0.98f / pk);
        }
    }

    auto folder = state.audioFolder();
    folder.createDirectory();
    const juce::String baseName = file.getFileNameWithoutExtension().upToLastOccurrenceOf ("_edit", false, false);
    auto out = folder.getNonexistentChildFile (baseName + "_edit", ".wav");
    auto writer = makeWavWriter (out, d->sampleRate, chans, 24);
    if (writer == nullptr || ! writer->writeFromAudioSampleBuffer (buf, 0, buf.getNumSamples())) { if (state.setStatus) state.setStatus ("Could not write " + out.getFullPathName()); return; }
    writer.reset();

    static const char* names[] = { "Normalize", "Reverse", "Fade in", "Fade out", "Gain +3dB", "Gain -3dB", "Silence", "Delete", "Crop" };
    state.beginEdit (names[(int) op]);
    if (clip.isValid())
    {
        const double secPerBeat = 60.0 / state.tempo();
        clip.setProperty (IDs::file, out.getFullPathName(), &state.undo);
        if (op == Op::Delete || op == Op::Crop)
        {
            const double newLenSec = buf.getNumSamples() / d->sampleRate;
            const double off = clip.getProperty (IDs::offset, 0.0);
            clip.setProperty (IDs::offset, op == Op::Crop ? 0.0 : juce::jmin (off, newLenSec), &state.undo);
            clip.setProperty (IDs::length, juce::jmin ((double) clip[IDs::length], newLenSec / secPerBeat), &state.undo);
        }
    }
    else openFile (out);
    if (state.setStatus) state.setStatus (juce::String (names[(int) op]) + " -> " + out.getFileName());
}

void AudioEditorPage::paint (juce::Graphics& g)
{
    g.fillAll (Theme::bg);
    g.setColour (Theme::panel);
    g.fillRect (getLocalBounds().removeFromTop (82));
}

void AudioEditorPage::resized()
{
    auto r = getLocalBounds().reduced (8);
    auto row1 = r.removeFromTop (30);
    for (auto* b : { &openBtn, &toArr, &play, &stop, &fit, &trim })
    { b->setBounds (row1.removeFromLeft (b == &toArr || b == &trim ? 150 : 90)); row1.removeFromLeft (6); }
    r.removeFromTop (6);
    auto row2 = r.removeFromTop (30);
    for (auto* b : { &normalize, &reverse, &fadeIn, &fadeOut, &gainUp, &gainDown, &silence, &del, &crop })
    { b->setBounds (row2.removeFromLeft (84)); row2.removeFromLeft (6); }
    r.removeFromTop (14);
    info.setBounds (r.removeFromTop (20));
    wave.setBounds (r);
}
