#include "TransportBar.h"

TransportBar::TransportBar (AppState& s) : state (s)
{
    for (auto* b : { &playBtn, &stopBtn, &recBtn }) addAndMakeVisible (b);
    addAndMakeVisible (loopBtn);
    addAndMakeVisible (metroBtn);
    playBtn.setColour (juce::TextButton::buttonOnColourId, Theme::good);
    recBtn.setColour (juce::TextButton::buttonOnColourId, Theme::danger);
    playBtn.onClick = [this] { state.engine.togglePlay(); };
    stopBtn.onClick = [this] { state.engine.stop(); };
    recBtn.onClick = [this]
    {
        state.engine.setRecordEnabled (! state.engine.isRecordEnabled());
        bool anyArmed = false;
        for (auto t : Model::tracks (project)) anyArmed |= ((bool) t[IDs::arm] && t[IDs::type].toString() == "audio");
        if (state.engine.isRecordEnabled() && ! anyArmed && state.setStatus)
            state.setStatus ("Record enabled - arm an audio track (R button) to capture input");
    };
    playBtn.setTooltip ("Play / pause (Space)");
    stopBtn.setTooltip ("Stop; press again to return to start");
    recBtn.setTooltip ("Record-enable: armed audio tracks record the audio input while playing");

    tempo.setSliderStyle (juce::Slider::LinearBar);
    tempo.setRange (20.0, 300.0, 0.1);
    tempo.setTextValueSuffix (" BPM");
    tempo.setNumDecimalPlacesToDisplay (1);
    tempo.setColour (juce::Slider::trackColourId, Theme::accent.withAlpha (0.25f));
    tempo.setColour (juce::Slider::textBoxTextColourId, Theme::text);
    tempo.onDragStart = [this] { state.beginEdit ("Tempo"); };
    addAndMakeVisible (tempo);

    for (int i = 1; i <= 16; ++i) tsNum.addItem (juce::String (i), i);
    for (int d : { 2, 4, 8, 16 }) tsDen.addItem (juce::String (d), d);
    tsNum.onChange = [this] { if (project.isValid() && (int) project[IDs::tsNum] != tsNum.getSelectedId()) { state.beginEdit ("Time signature"); project.setProperty (IDs::tsNum, tsNum.getSelectedId(), &state.undo); } };
    tsDen.onChange = [this] { if (project.isValid() && (int) project[IDs::tsDen] != tsDen.getSelectedId()) { state.beginEdit ("Time signature"); project.setProperty (IDs::tsDen, tsDen.getSelectedId(), &state.undo); } };
    addAndMakeVisible (tsNum);
    addAndMakeVisible (tsDen);

    position.setFont (Theme::mono (20.0f));
    position.setColour (juce::Label::textColourId, Theme::accent);
    position.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (position);
    cpu.setFont (Theme::mono (11.0f));
    cpu.setColour (juce::Label::textColourId, Theme::textDim);
    addAndMakeVisible (cpu);
    status.setFont (Theme::font (12.0f));
    status.setColour (juce::Label::textColourId, Theme::textDim);
    addAndMakeVisible (status);
    state.setStatus = [this] (const juce::String& t) { status.setText (t, juce::dontSendNotification); };
    meter.setSource (&state.engine.getMasterChannel().dispL, &state.engine.getMasterChannel().dispR);
    addAndMakeVisible (meter);
    startTimerHz (30);
}

TransportBar::~TransportBar() { if (project.isValid()) project.removeListener (this); }

void TransportBar::setProject (juce::ValueTree p)
{
    if (project.isValid()) project.removeListener (this);
    project = p;
    project.addListener (this);
    tempo.getValueObject().referTo (project.getPropertyAsValue (IDs::tempo, &state.undo));
    loopBtn.bind (project, IDs::loopOn, &state.undo);
    metroBtn.bind (project, IDs::metronome, &state.undo);
    refresh();
}

void TransportBar::refresh()
{
    tsNum.setSelectedId ((int) project.getProperty (IDs::tsNum, 4), juce::dontSendNotification);
    tsDen.setSelectedId ((int) project.getProperty (IDs::tsDen, 4), juce::dontSendNotification);
}

void TransportBar::valueTreePropertyChanged (juce::ValueTree& t, const juce::Identifier& p)
{
    if (t == project && (p == IDs::tsNum || p == IDs::tsDen)) refresh();
}

void TransportBar::timerCallback()
{
    const double beat = state.engine.getPosition();
    const double bpb = state.beatsPerBar();
    const int bar = (int) std::floor (beat / bpb) + 1;
    const double inBar = beat - (bar - 1) * bpb;
    const int b = (int) std::floor (inBar) + 1;
    const int tick = (int) ((inBar - std::floor (inBar)) * 960.0);
    const double secs = beat * 60.0 / state.tempo();
    position.setText (juce::String (bar).paddedLeft (' ', 3) + "." + juce::String (b) + "." + juce::String (tick).paddedLeft ('0', 3)
                      + "   " + juce::String ((int) secs / 60) + ":" + juce::String (std::fmod (secs, 60.0), 2).paddedLeft ('0', 5), juce::dontSendNotification);
    playBtn.setToggleState (state.engine.isPlaying(), juce::dontSendNotification);
    recBtn.setToggleState (state.engine.isRecordEnabled(), juce::dontSendNotification);
    recBtn.setButtonText (state.engine.isRecording() ? "REC *" : "REC");
    auto* dev = state.engine.getDeviceManager().getCurrentAudioDevice();
    cpu.setText (dev != nullptr ? "CPU " + juce::String (state.engine.getCpuLoad() * 100.0f, 1) + "%  " + juce::String ((int) dev->getCurrentSampleRate()) + " Hz / " + juce::String (dev->getCurrentBufferSizeSamples())
                                : "NO AUDIO DEVICE", juce::dontSendNotification);
}

void TransportBar::paint (juce::Graphics& g)
{
    g.setColour (Theme::panel);
    g.fillRect (getLocalBounds());
    g.setColour (Theme::outline);
    g.drawHorizontalLine (getHeight() - 1, 0, (float) getWidth());
    auto r = getLocalBounds();
    g.setFont (Theme::font (18.0f, true));
    g.setColour (Theme::accent);
    g.drawText ("MANDELBROT", r.removeFromLeft (150).reduced (12, 0), juce::Justification::centredLeft);
    g.setColour (Theme::textDim);
    g.setFont (Theme::font (10.0f));
    g.drawText ("z = z^2 + c", juce::Rectangle<int> (12, getHeight() - 16, 140, 12), juce::Justification::centredLeft);
}

void TransportBar::resized()
{
    auto r = getLocalBounds().reduced (6, 8);
    r.removeFromLeft (150);
    playBtn.setBounds (r.removeFromLeft (64)); r.removeFromLeft (4);
    stopBtn.setBounds (r.removeFromLeft (64)); r.removeFromLeft (4);
    recBtn.setBounds (r.removeFromLeft (64)); r.removeFromLeft (10);
    loopBtn.setBounds (r.removeFromLeft (56)); r.removeFromLeft (4);
    metroBtn.setBounds (r.removeFromLeft (56)); r.removeFromLeft (12);
    position.setBounds (r.removeFromLeft (230)); r.removeFromLeft (10);
    tempo.setBounds (r.removeFromLeft (120)); r.removeFromLeft (8);
    tsNum.setBounds (r.removeFromLeft (52)); r.removeFromLeft (2);
    tsDen.setBounds (r.removeFromLeft (52)); r.removeFromLeft (12);
    meter.setBounds (r.removeFromRight (18));
    r.removeFromRight (8);
    cpu.setBounds (r.removeFromRight (170));
    status.setBounds (r);
}
