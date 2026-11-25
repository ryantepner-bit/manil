#pragma once
#include "AppState.h"
#include "Composer.h"
#include "Widgets.h"
#include "../Audio/BreakEngine.h"

/** The break's waveform with its slice markers: click a slice to hear it, drag markers, double-click to add, right-click to remove. */
class BreakWaveView : public juce::Component, public juce::FileDragAndDropTarget
{
public:
    explicit BreakWaveView (AppState& s) : state (s) {}
    void setTrack (juce::ValueTree t) { track = t; repaint(); }
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    bool isInterestedInFileDrag (const juce::StringArray&) override { return true; }
    void filesDropped (const juce::StringArray& files, int, int) override { if (onFileDropped && ! files.isEmpty()) onFileDropped (juce::File (files[0])); }
    std::function<void (const juce::File&)> onFileDropped;
    std::shared_ptr<const SampleData> data;
private:
    std::vector<float> slices() const;
    void writeSlices (std::vector<float> p, const juce::String& what);
    int markerAt (float x) const;
    int sliceAt (float x) const;
    AppState& state;
    juce::ValueTree track;
    int dragMarker = -1;
};

/**
 * BREAKS: the break chopper (after LMMS's SlicerT). Load any break, it is cut
 * at its transients (or into equal slices), each slice is a pad / a MIDI note
 * from C1 up. Live mode's Break part re-chops it with the fractal.
 */
class BreakPage : public juce::Component, private juce::ValueTree::Listener, private juce::Timer, private juce::ChangeListener
{
public:
    BreakPage (AppState& s, Composer& c);
    ~BreakPage() override;
    void setProject (juce::ValueTree p);
    void resized() override;
    void paint (juce::Graphics&) override;
private:
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;
    void valueTreeChildAdded (juce::ValueTree& p, juce::ValueTree&) override { if (p.hasType (IDs::TRACKS)) refresh(); }
    void valueTreeChildRemoved (juce::ValueTree& p, juce::ValueTree&, int) override { if (p.hasType (IDs::TRACKS)) refresh(); }
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;
    void refresh();
    void bindTrack (juce::ValueTree t);
    void loadBreak (const juce::File& f);
    void chop (int equalCount);
    void writeClip (bool rechop);
    juce::ValueTree breakNode() const { return track.getChildWithName (IDs::BREAK); }

    AppState& state;
    Composer& composer;
    juce::ValueTree project, track;
    juce::Label title, info;
    juce::ComboBox trackBox;
    juce::TextButton loadBtn { "Load break..." }, builtinBtn { "Built-in break" }, addTrackBtn { "+ Break track" };
    juce::TextButton autoBtn { "Auto-chop" }, eq8 { "8" }, eq16 { "16" }, eq32 { "32" };
    juce::TextButton toClip { "Chops to clip" }, rechopClip { "Fractal re-chop to clip" };
    juce::Slider sensitivity;
    juce::Label sensL;
    PropToggle syncBtn { "Sync to tempo", Theme::good };
    ParamKnob bpm { "Break BPM", 60.0f, 200.0f }, tune { "Tune", -12.0f, 12.0f, 0.0f, "st" }, fade { "Fade", 1.0f, 80.0f, 0.0f, "ms" }, gain { "Gain", -24.0f, 6.0f, 0.0f, "dB" };
    BreakWaveView wave;
    juce::OwnedArray<juce::TextButton> pads;
    std::unique_ptr<juce::FileChooser> chooser;
    int lastPlaying = -2;
};
