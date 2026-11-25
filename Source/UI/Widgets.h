#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "Theme.h"
#include "../Core/Params.h"
#include "../Audio/AudioPool.h"

/** Rotary knob bound to a ValueTree float property (undoable, one transaction per drag). */
class ParamKnob : public juce::Component
{
public:
    ParamKnob (const juce::String& label, float minV, float maxV, float skewCentre = 0.0f, const juce::String& suffix = {});
    ParamKnob (const ParamDef& def) : ParamKnob (def.label, def.minV, def.maxV, def.skewCentre, def.unit) { defaultValue = def.defV; }
    void bind (juce::ValueTree tree, const juce::Identifier& prop, juce::UndoManager* um);
    void unbind();
    void setColour (juce::Colour c) { slider.setColour (juce::Slider::rotarySliderFillColourId, c); }
    void resized() override;
    void paint (juce::Graphics&) override;
    juce::Slider slider;
    std::function<void (float)> onChange;
    float defaultValue = 0.0f;
private:
    juce::String label;
    juce::UndoManager* undo = nullptr;
};

/** ComboBox bound to an integer ValueTree property (choice param). */
class ParamChoice : public juce::Component
{
public:
    ParamChoice (const juce::String& label, const juce::StringArray& items);
    void bind (juce::ValueTree tree, const juce::Identifier& prop, juce::UndoManager* um);
    void unbind();
    void resized() override;
    void paint (juce::Graphics&) override;
    juce::ComboBox box;
    std::function<void (int)> onChange;
private:
    juce::String label;
    juce::ValueTree tree;
    juce::Identifier prop;
    juce::UndoManager* undo = nullptr;
    struct Listener : juce::ValueTree::Listener { ParamChoice* owner; void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override; } listener;
    void refresh();
};

/** Toggle bound to a bool property. */
class PropToggle : public juce::TextButton, private juce::ValueTree::Listener
{
public:
    PropToggle (const juce::String& text, juce::Colour onColour = Theme::accent);
    void bind (juce::ValueTree tree, const juce::Identifier& prop, juce::UndoManager* um);
    ~PropToggle() override { if (tree.isValid()) tree.removeListener (this); }
private:
    void clicked() override;
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;
    juce::ValueTree tree;
    juce::Identifier prop;
    juce::UndoManager* undo = nullptr;
};

/** Stereo peak meter polling two atomics. */
class LevelMeter : public juce::Component, private juce::Timer
{
public:
    LevelMeter() { startTimerHz (30); }
    void setSource (std::atomic<float>* l, std::atomic<float>* r) { left = l; right = r; }
    void paint (juce::Graphics&) override;
private:
    void timerCallback() override;
    std::atomic<float>* left = nullptr; std::atomic<float>* right = nullptr;
    float levelL = 0, levelR = 0, holdL = 0, holdR = 0;
};

/** Min/max peak cache for drawing waveforms. */
struct WaveformCache
{
    static const std::vector<std::pair<float, float>>& get (const SampleData& d);   // 4096 bins (message thread)
    static void draw (juce::Graphics&, const SampleData& d, juce::Rectangle<float> area, double startSec, double endSec, juce::Colour c);
};

/** Small helper: header label for sections. */
inline void drawSectionTitle (juce::Graphics& g, juce::Rectangle<int> r, const juce::String& t)
{
    g.setColour (Theme::textDim);
    g.setFont (Theme::font (11.0f, true));
    g.drawText (t.toUpperCase(), r, juce::Justification::centredLeft);
}
