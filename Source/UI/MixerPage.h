#pragma once
#include "AppState.h"
#include "Widgets.h"

/** One mixer channel strip (track, bus or master). */
class ChannelStrip : public juce::Component
{
public:
    enum Kind { TrackStrip, BusStrip, MasterStrip };
    ChannelStrip (AppState& s, juce::ValueTree node, Kind k, int busCount, std::function<void (juce::ValueTree slot)> editSlot);
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    juce::ValueTree node;
    Kind kind;
private:
    AppState& state;
    juce::Slider fader;
    ParamKnob pan { "Pan", -1, 1 };
    juce::OwnedArray<ParamKnob> sends;
    juce::OwnedArray<juce::ComboBox> slotBoxes;
    juce::OwnedArray<juce::TextButton> slotEdit;
    PropToggle mute { "M", Theme::warn }, solo { "S", Theme::good };
    juce::ComboBox output;
    LevelMeter meter;
    juce::Label dbLabel;
};

/** Editor for the 4 parameters of one insert slot. */
class SlotEditor : public juce::Component
{
public:
    explicit SlotEditor (AppState& s);
    void setSlot (juce::ValueTree slot, const juce::String& ownerName);
    void paint (juce::Graphics&) override;
    void resized() override;
private:
    AppState& state;
    juce::ValueTree slot;
    juce::String owner;
    juce::OwnedArray<ParamKnob> knobs;
    PropToggle bypass { "Bypass", Theme::warn };
};

class MixerPage : public juce::Component, private juce::ValueTree::Listener
{
public:
    explicit MixerPage (AppState& s);
    ~MixerPage() override;
    void setProject (juce::ValueTree p);
    void resized() override;
    void paint (juce::Graphics&) override;
private:
    void valueTreeChildAdded (juce::ValueTree& p, juce::ValueTree&) override { if (p.hasType (IDs::TRACKS) || p.hasType (IDs::BUSES)) rebuildAsync(); }
    void valueTreeChildRemoved (juce::ValueTree& p, juce::ValueTree&, int) override { if (p.hasType (IDs::TRACKS) || p.hasType (IDs::BUSES)) rebuildAsync(); }
    void valueTreeChildOrderChanged (juce::ValueTree& p, int, int) override { if (p.hasType (IDs::TRACKS)) rebuildAsync(); }
    void rebuildAsync() { juce::MessageManager::callAsync ([sp = juce::Component::SafePointer<MixerPage> (this)] { if (sp) sp->rebuild(); }); }
    void valueTreePropertyChanged (juce::ValueTree& t, const juce::Identifier& p) override;
    void rebuild();

    AppState& state;
    juce::ValueTree project;
    juce::Viewport viewport;
    juce::Component strips;
    juce::OwnedArray<ChannelStrip> channelStrips;
    SlotEditor slotEditor;
    juce::TextButton addBus { "+ Bus" };
};
