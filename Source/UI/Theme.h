#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

namespace Theme
{
    inline const juce::Colour bg         { 0xff0b0d12 };
    inline const juce::Colour panel      { 0xff12151d };
    inline const juce::Colour panelLight { 0xff1a1f2a };
    inline const juce::Colour outline    { 0xff2a3142 };
    inline const juce::Colour grid       { 0xff1c2230 };
    inline const juce::Colour gridStrong { 0xff2c3548 };
    inline const juce::Colour text       { 0xffd6dbe6 };
    inline const juce::Colour textDim    { 0xff7d869a };
    inline const juce::Colour accent     { 0xff5ad1ff };   // cyan
    inline const juce::Colour accent2    { 0xffff5a9d };   // magenta
    inline const juce::Colour accent3    { 0xffb38cff };   // violet
    inline const juce::Colour good       { 0xff6bffb8 };
    inline const juce::Colour warn       { 0xffffc85a };
    inline const juce::Colour danger     { 0xffff5a5a };

    juce::Font font (float h, bool bold = false);
    juce::Font mono (float h);
    void drawPanel (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& title = {});
}

class MandelLookAndFeel : public juce::LookAndFeel_V4
{
public:
    MandelLookAndFeel();
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float start, float end, juce::Slider&) override;
    void drawLinearSlider (juce::Graphics&, int x, int y, int w, int h, float pos, float minPos, float maxPos, juce::Slider::SliderStyle, juce::Slider&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
    void drawComboBox (juce::Graphics&, int w, int h, bool down, int bx, int by, int bw, int bh, juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override { return Theme::font (13.0f); }
    juce::Font getTextButtonFont (juce::TextButton&, int h) override { return Theme::font (juce::jmin (14.0f, h * 0.55f)); }
    juce::Font getLabelFont (juce::Label&) override { return Theme::font (13.0f); }
    void drawTabButton (juce::TabBarButton&, juce::Graphics&, bool over, bool down) override;
    int getTabButtonBestWidth (juce::TabBarButton&, int) override { return 128; }
    void drawTabAreaBehindFrontButton (juce::TabbedButtonBar&, juce::Graphics&, int, int) override {}
};
