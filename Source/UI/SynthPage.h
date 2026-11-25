#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "AppState.h"
#include "Composer.h"
#include "Widgets.h"

class WavetableDisplay : public juce::Component, private juce::Timer
{
public:
    explicit WavetableDisplay (AppState& s) : state (s) { startTimerHz (10); }
    int trackId = -1;
    void paint (juce::Graphics&) override;
private:
    void timerCallback() override { repaint(); }
    AppState& state;
};

class SynthPage : public juce::Component, private juce::ChangeListener, private juce::MidiKeyboardState::Listener, private juce::ValueTree::Listener
{
public:
    SynthPage (AppState& s, Composer& c);
    ~SynthPage() override;
    void setProject (juce::ValueTree p);
    void resized() override;
    void paint (juce::Graphics&) override;
private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void handleNoteOn (juce::MidiKeyboardState*, int, int note, float vel) override;
    void handleNoteOff (juce::MidiKeyboardState*, int, int note, float) override;
    void valueTreeChildAdded (juce::ValueTree& p, juce::ValueTree&) override { if (p.hasType (IDs::TRACKS)) refreshTrackList(); }
    void valueTreeChildRemoved (juce::ValueTree& p, juce::ValueTree&, int) override { if (p.hasType (IDs::TRACKS)) refreshTrackList(); }
    void valueTreePropertyChanged (juce::ValueTree& t, const juce::Identifier& p) override;
    void refreshTrackList();
    void bindTrack (juce::ValueTree t);
    void updateVisibility();
    juce::Component* comp (int index) { return comps[(size_t) index]; }

    AppState& state;
    Composer& composer;
    juce::ValueTree project, track;
    juce::ComboBox trackBox;
    juce::Label title;
    std::vector<juce::Component*> comps;               // index = SynthP::Index
    juce::OwnedArray<juce::Component> owned;
    juce::TextButton wtFromFractal { "Wavetable from fractal probe" }, wtLoad { "Load WAV wavetable" }, grainLoad { "Load WAV for grains" }, grainClear { "Use wavetable" };
    juce::TextButton init { "Init patch" }, randomize { "Mutate patch" };
    juce::ComboBox presetBox;
    WavetableDisplay wtDisplay;
    juce::MidiKeyboardState keyState;
    juce::MidiKeyboardComponent keyboard { keyState, juce::MidiKeyboardComponent::horizontalKeyboard };
    std::unique_ptr<juce::FileChooser> chooser;
    struct Section { juce::String title; juce::Rectangle<int> bounds; };
    std::vector<Section> sections;
};
