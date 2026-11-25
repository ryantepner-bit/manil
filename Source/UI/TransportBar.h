#pragma once
#include "AppState.h"
#include "Widgets.h"

class TransportBar : public juce::Component, private juce::Timer, private juce::ValueTree::Listener
{
public:
    explicit TransportBar (AppState& s);
    ~TransportBar() override;
    void setProject (juce::ValueTree p);
    void resized() override;
    void paint (juce::Graphics&) override;
private:
    void timerCallback() override;
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;
    void refresh();

    AppState& state;
    juce::ValueTree project;
    juce::TextButton playBtn { "PLAY" }, stopBtn { "STOP" }, recBtn { "REC" };
    PropToggle loopBtn { "LOOP" }, metroBtn { "CLICK", Theme::warn };
    juce::Slider tempo;
    juce::ComboBox tsNum, tsDen;
    juce::Label position, cpu, status;
    LevelMeter meter;
};
