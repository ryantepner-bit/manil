#pragma once
#include "AppState.h"
#include "Composer.h"
#include "Widgets.h"

class StepGrid : public juce::Component, private juce::ValueTree::Listener, private juce::Timer
{
public:
    explicit StepGrid (AppState& s);
    ~StepGrid() override;
    void setClip (juce::ValueTree clip, juce::ValueTree track);
    std::function<void (int pad)> onPadSelected;
    int selectedPad = 0;
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
private:
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override { repaint(); }
    void timerCallback() override { repaint(); }
    juce::Rectangle<int> rowBounds (int pad) const;
    juce::Rectangle<int> cellsArea (int pad) const;
    int maxSteps() const;
    bool cellAt (juce::Point<int> p, int& pad, int& step) const;
    void editStep (int pad, int step, std::function<void (StepData&)> fn);
    void rebuildRowControls();

    AppState& state;
    juce::ValueTree clip, track;
    juce::OwnedArray<juce::Slider> stepSliders;
    juce::OwnedArray<juce::ComboBox> rateBoxes;
    static constexpr int nameW = 110, ctrlW = 150;
    int dragPad = -1, dragStep = -1; int dragVel = 100; bool dragPaint = false, dragOn = false;
};

class PadWaveform : public juce::Component
{
public:
    std::shared_ptr<const SampleData> data;
    float start = 0, end = 1;
    void paint (juce::Graphics&) override;
};

class DrumPage : public juce::Component, private juce::ChangeListener, private juce::ValueTree::Listener, private juce::Timer
{
public:
    DrumPage (AppState& s, Composer& c);
    ~DrumPage() override;
    void setProject (juce::ValueTree p);
    void resized() override;
    void paint (juce::Graphics&) override;
private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;
    void valueTreeChildAdded (juce::ValueTree& p, juce::ValueTree&) override { if (p.hasType (IDs::TRACKS) || p.hasType (IDs::CLIPS)) refresh(); }
    void valueTreeChildRemoved (juce::ValueTree& p, juce::ValueTree&, int) override { if (p.hasType (IDs::TRACKS) || p.hasType (IDs::CLIPS)) refresh(); }
    void timerCallback() override;
    void refresh();
    void selectPad (int pad);
    void bindTrack (juce::ValueTree t);
    juce::ValueTree padNode (int pad) const;
    void sliceToPads (const juce::File& f);
    void generateBreak (bool mutate);

    AppState& state;
    Composer& composer;
    juce::ValueTree project, track, clip;
    int selectedPad = 0;
    juce::ComboBox trackBox, clipBox;
    juce::Label title;
    juce::TextButton newPattern { "New pattern" }, genBreak { "Generate breakbeat" }, mutateBreak { "Mutate" }, clearPat { "Clear" };
    juce::Slider swing, genDensity, genComplexity;
    juce::Label swingL, densL, compL;
    StepGrid grid;
    juce::OwnedArray<juce::TextButton> pads;
    juce::OwnedArray<ParamKnob> padKnobs;
    ParamChoice reverseBox { "Direction", { "Forward", "Reverse" } }, chokeBox { "Choke", { "-", "1", "2", "3", "4" } };
    juce::ComboBox synthKind;
    juce::TextButton loadSample { "Load sample" }, clearSample { "Synth sound" }, slice { "Slice break to pads" };
    juce::Label padTitle;
    PadWaveform wave;
    std::unique_ptr<juce::FileChooser> chooser;
    int breakGeneration = 0, breakPress = 0;
};
