#include "Theme.h"

namespace Theme
{
    juce::Font font (float h, bool bold) { return juce::Font (juce::FontOptions (h, bold ? juce::Font::bold : juce::Font::plain)); }
    juce::Font mono (float h) { return juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), h, juce::Font::plain)); }

    void drawPanel (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& title)
    {
        g.setColour (panel);
        g.fillRoundedRectangle (r, 6.0f);
        g.setColour (outline);
        g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, 1.0f);
        if (title.isNotEmpty())
        {
            g.setColour (textDim);
            g.setFont (font (11.0f, true));
            g.drawText (title.toUpperCase(), r.reduced (10, 4).removeFromTop (16), juce::Justification::centredLeft);
        }
    }
}

MandelLookAndFeel::MandelLookAndFeel()
{
    using namespace Theme;
    setColourScheme ({ bg, panel, panelLight, outline, text, accent, juce::Colours::black, panelLight, text });
    setColour (juce::ResizableWindow::backgroundColourId, bg);
    setColour (juce::Slider::thumbColourId, accent);
    setColour (juce::Slider::rotarySliderFillColourId, accent);
    setColour (juce::Slider::rotarySliderOutlineColourId, outline);
    setColour (juce::Slider::trackColourId, accent);
    setColour (juce::Slider::backgroundColourId, panelLight);
    setColour (juce::Slider::textBoxTextColourId, text);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::Label::textColourId, text);
    setColour (juce::TextButton::buttonColourId, panelLight);
    setColour (juce::TextButton::buttonOnColourId, accent.withAlpha (0.85f));
    setColour (juce::TextButton::textColourOffId, text);
    setColour (juce::TextButton::textColourOnId, juce::Colours::black);
    setColour (juce::ComboBox::backgroundColourId, panelLight);
    setColour (juce::ComboBox::outlineColourId, outline);
    setColour (juce::ComboBox::textColourId, text);
    setColour (juce::ComboBox::arrowColourId, textDim);
    setColour (juce::PopupMenu::backgroundColourId, panel);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, accent.withAlpha (0.25f));
    setColour (juce::PopupMenu::textColourId, text);
    setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::white);
    setColour (juce::ListBox::backgroundColourId, panel);
    setColour (juce::TextEditor::backgroundColourId, panelLight);
    setColour (juce::TextEditor::outlineColourId, outline);
    setColour (juce::TextEditor::textColourId, text);
    setColour (juce::TextEditor::focusedOutlineColourId, accent);
    setColour (juce::TabbedButtonBar::tabTextColourId, textDim);
    setColour (juce::TabbedButtonBar::frontTextColourId, text);
    setColour (juce::TabbedComponent::outlineColourId, juce::Colours::transparentBlack);
    setColour (juce::ScrollBar::thumbColourId, outline);
    setColour (juce::ToggleButton::textColourId, text);
    setColour (juce::ToggleButton::tickColourId, accent);
    setColour (juce::AlertWindow::backgroundColourId, panel);
    setColour (juce::AlertWindow::textColourId, text);
    setColour (juce::CaretComponent::caretColourId, accent);
}

void MandelLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float start, float end, juce::Slider& s)
{
    auto bounds = juce::Rectangle<int> (x, y, w, h).toFloat().reduced (3.0f);
    const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) / 2.0f;
    const auto c = bounds.getCentre();
    const float lineW = juce::jmax (2.0f, radius * 0.16f);
    const float arcR = radius - lineW * 0.5f;
    const auto col = s.findColour (juce::Slider::rotarySliderFillColourId);

    juce::Path bgArc;
    bgArc.addCentredArc (c.x, c.y, arcR, arcR, 0.0f, start, end, true);
    g.setColour (Theme::outline);
    g.strokePath (bgArc, juce::PathStrokeType (lineW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    const bool bipolar = s.getMinimum() < 0 && s.getMaximum() > 0;
    const float zeroPos = bipolar ? start + (end - start) * (float) s.valueToProportionOfLength (0.0) : start;
    const float angle = start + pos * (end - start);
    juce::Path valArc;
    valArc.addCentredArc (c.x, c.y, arcR, arcR, 0.0f, juce::jmin (zeroPos, angle), juce::jmax (zeroPos, angle), true);
    g.setColour (col);
    g.strokePath (valArc, juce::PathStrokeType (lineW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    g.setColour (Theme::panelLight);
    g.fillEllipse (juce::Rectangle<float> (radius * 1.2f, radius * 1.2f).withCentre (c));
    juce::Path ptr;
    const float pl = radius * 0.55f;
    ptr.addRoundedRectangle (-lineW * 0.4f, -pl, lineW * 0.8f, pl * 0.6f, 1.0f);
    g.setColour (Theme::text);
    g.fillPath (ptr, juce::AffineTransform::rotation (angle).translated (c));
}

void MandelLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float, float, juce::Slider::SliderStyle style, juce::Slider& s)
{
    if (style == juce::Slider::LinearVertical)
    {
        auto r = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h);
        auto track = r.withSizeKeepingCentre (6.0f, r.getHeight());
        g.setColour (Theme::panelLight);
        g.fillRoundedRectangle (track, 3.0f);
        g.setColour (s.findColour (juce::Slider::trackColourId).withAlpha (0.7f));
        g.fillRoundedRectangle (track.withTop (pos), 3.0f);
        auto thumb = juce::Rectangle<float> (juce::jmin (28.0f, r.getWidth()), 12.0f).withCentre ({ r.getCentreX(), pos });
        g.setColour (Theme::text);
        g.fillRoundedRectangle (thumb, 3.0f);
        g.setColour (Theme::bg);
        g.drawHorizontalLine ((int) pos, thumb.getX() + 4, thumb.getRight() - 4);
        return;
    }
    if (style == juce::Slider::LinearBar)
    {
        auto r = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h);
        g.setColour (Theme::panelLight);
        g.fillRoundedRectangle (r, 4.0f);
        g.setColour (s.findColour (juce::Slider::trackColourId));
        g.fillRoundedRectangle (r.withRight (pos), 4.0f);
        g.setColour (Theme::outline);
        g.drawRoundedRectangle (r.reduced (0.5f), 4.0f, 1.0f);
        return;
    }
    if (style == juce::Slider::LinearHorizontal)
    {
        auto r = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h);
        auto track = r.withSizeKeepingCentre (r.getWidth(), 4.0f);
        g.setColour (Theme::panelLight);
        g.fillRoundedRectangle (track, 2.0f);
        g.setColour (s.findColour (juce::Slider::trackColourId));
        g.fillRoundedRectangle (track.withRight (pos), 2.0f);
        g.setColour (Theme::text);
        g.fillEllipse (juce::Rectangle<float> (10.0f, 10.0f).withCentre ({ pos, r.getCentreY() }));
        return;
    }
    LookAndFeel_V4::drawLinearSlider (g, x, y, w, h, pos, 0, 0, style, s);
}

void MandelLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour& base, bool over, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    auto c = b.getToggleState() ? b.findColour (juce::TextButton::buttonOnColourId) : base;
    if (down) c = c.brighter (0.2f); else if (over) c = c.brighter (0.08f);
    g.setColour (c);
    g.fillRoundedRectangle (r, 4.0f);
    g.setColour (b.getToggleState() ? c.brighter (0.3f) : Theme::outline);
    g.drawRoundedRectangle (r, 4.0f, 1.0f);
}

void MandelLookAndFeel::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box)
{
    auto r = juce::Rectangle<float> (0, 0, (float) w, (float) h).reduced (0.5f);
    g.setColour (box.findColour (juce::ComboBox::backgroundColourId));
    g.fillRoundedRectangle (r, 4.0f);
    g.setColour (box.hasKeyboardFocus (true) ? Theme::accent.withAlpha (0.6f) : Theme::outline);
    g.drawRoundedRectangle (r, 4.0f, 1.0f);
    juce::Path arrow;
    const float ax = (float) w - 12.0f, ay = (float) h * 0.5f;
    arrow.addTriangle (ax - 4, ay - 2, ax + 4, ay - 2, ax, ay + 3);
    g.setColour (Theme::textDim);
    g.fillPath (arrow);
}

void MandelLookAndFeel::drawTabButton (juce::TabBarButton& b, juce::Graphics& g, bool over, bool)
{
    auto r = b.getLocalBounds().toFloat().reduced (2.0f, 4.0f);
    const bool front = b.isFrontTab();
    if (front)
    {
        g.setColour (Theme::panelLight);
        g.fillRoundedRectangle (r, 5.0f);
        g.setColour (Theme::accent);
        g.fillRoundedRectangle (r.removeFromBottom (2.0f).reduced (10.0f, 0.0f), 1.0f);
    }
    else if (over)
    {
        g.setColour (Theme::panelLight.withAlpha (0.5f));
        g.fillRoundedRectangle (r, 5.0f);
    }
    g.setColour (front ? Theme::text : Theme::textDim);
    g.setFont (Theme::font (13.0f, front));
    g.drawText (b.getButtonText(), b.getLocalBounds(), juce::Justification::centred);
}
