#include "Widgets.h"
#include <map>

// ============================================================================ ParamKnob
ParamKnob::ParamKnob (const juce::String& l, float minV, float maxV, float skewCentre, const juce::String& suffix) : label (l)
{
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    slider.setRange (minV, maxV, 0.0);
    if (skewCentre > minV && skewCentre < maxV) slider.setSkewFactorFromMidPoint (skewCentre);
    slider.setTextValueSuffix (suffix.isNotEmpty() ? " " + suffix : juce::String());
    slider.setPopupDisplayEnabled (true, true, nullptr);
    slider.setDoubleClickReturnValue (false, 0.0);
    slider.setMouseDragSensitivity (180);
    slider.onDragStart = [this] { if (undo) undo->beginNewTransaction (label); };
    slider.onValueChange = [this] { if (onChange) onChange ((float) slider.getValue()); };
    slider.setNumDecimalPlacesToDisplay (2);
    addAndMakeVisible (slider);
}

void ParamKnob::bind (juce::ValueTree tree, const juce::Identifier& prop, juce::UndoManager* um)
{
    undo = um;
    if (! tree.hasProperty (prop)) tree.setProperty (prop, defaultValue, nullptr);
    slider.getValueObject().referTo (tree.getPropertyAsValue (prop, um));
    slider.setDoubleClickReturnValue (true, defaultValue);
    setEnabled (true);
}

void ParamKnob::unbind()
{
    slider.getValueObject().referTo (juce::Value());
    setEnabled (false);
}

void ParamKnob::resized()
{
    auto r = getLocalBounds();
    r.removeFromBottom (14);
    slider.setBounds (r.withSizeKeepingCentre (juce::jmin (r.getWidth(), r.getHeight()), juce::jmin (r.getWidth(), r.getHeight())));
}

void ParamKnob::paint (juce::Graphics& g)
{
    g.setColour (isEnabled() ? Theme::textDim : Theme::outline);
    g.setFont (Theme::font (10.5f));
    g.drawFittedText (label, getLocalBounds().removeFromBottom (14), juce::Justification::centred, 1);
}

// ============================================================================ ParamChoice
ParamChoice::ParamChoice (const juce::String& l, const juce::StringArray& items) : label (l)
{
    box.addItemList (items, 1);
    box.onChange = [this]
    {
        if (tree.isValid())
        {
            const int v = box.getSelectedItemIndex();
            if ((int) tree[prop] != v)
            {
                if (undo) undo->beginNewTransaction (label);
                tree.setProperty (prop, v, undo);
            }
        }
        if (onChange) onChange (box.getSelectedItemIndex());
    };
    addAndMakeVisible (box);
    listener.owner = this;
}

void ParamChoice::Listener::valueTreePropertyChanged (juce::ValueTree& t, const juce::Identifier& p)
{
    if (t == owner->tree && p == owner->prop) owner->refresh();
}

void ParamChoice::bind (juce::ValueTree t, const juce::Identifier& p, juce::UndoManager* um)
{
    if (tree.isValid()) tree.removeListener (&listener);
    tree = t; prop = p; undo = um;
    tree.addListener (&listener);
    refresh();
    setEnabled (true);
}

void ParamChoice::unbind()
{
    if (tree.isValid()) tree.removeListener (&listener);
    tree = {};
    setEnabled (false);
}

void ParamChoice::refresh()
{
    box.setSelectedItemIndex ((int) tree.getProperty (prop, 0), juce::dontSendNotification);
}

void ParamChoice::resized()
{
    auto r = getLocalBounds();
    if (label.isNotEmpty()) r.removeFromTop (14);
    box.setBounds (r.withSizeKeepingCentre (r.getWidth(), juce::jmin (24, r.getHeight())));
}

void ParamChoice::paint (juce::Graphics& g)
{
    if (label.isEmpty()) return;
    g.setColour (Theme::textDim);
    g.setFont (Theme::font (10.5f));
    g.drawText (label, getLocalBounds().removeFromTop (14), juce::Justification::centredLeft);
}

// ============================================================================ PropToggle
PropToggle::PropToggle (const juce::String& text, juce::Colour onColour) : juce::TextButton (text)
{
    setClickingTogglesState (false);
    setColour (juce::TextButton::buttonOnColourId, onColour);
}

void PropToggle::bind (juce::ValueTree t, const juce::Identifier& p, juce::UndoManager* um)
{
    if (tree.isValid()) tree.removeListener (this);
    tree = t; prop = p; undo = um;
    if (tree.isValid()) tree.addListener (this);
    setToggleState ((bool) tree.getProperty (prop, false), juce::dontSendNotification);
}

void PropToggle::clicked()
{
    if (! tree.isValid()) return;
    if (undo) undo->beginNewTransaction (getButtonText());
    tree.setProperty (prop, ! (bool) tree.getProperty (prop, false), undo);
}

void PropToggle::valueTreePropertyChanged (juce::ValueTree& t, const juce::Identifier& p)
{
    if (t == tree && p == prop) setToggleState ((bool) tree[prop], juce::dontSendNotification);
}

// ============================================================================ LevelMeter
void LevelMeter::timerCallback()
{
    auto take = [] (std::atomic<float>* a) { return a != nullptr ? a->load() : 0.0f; };
    const float l = take (left), r = take (right);
    levelL = juce::jmax (l, levelL * 0.86f);
    levelR = juce::jmax (r, levelR * 0.86f);
    holdL = juce::jmax (l, holdL * 0.985f);
    holdR = juce::jmax (r, holdR * 0.985f);
    repaint();
}

void LevelMeter::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (Theme::bg);
    g.fillRoundedRectangle (r, 2.0f);
    auto toY = [&r] (float lvl)
    {
        const float db = juce::Decibels::gainToDecibels (lvl, -60.0f);
        return r.getBottom() - r.getHeight() * juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 66.0f);
    };
    auto bar = [&] (juce::Rectangle<float> b, float lvl, float hold)
    {
        const float y = toY (lvl);
        juce::ColourGradient grad (Theme::good, 0, r.getBottom(), Theme::danger, 0, r.getY(), false);
        grad.addColour (0.75, Theme::warn);
        g.setGradientFill (grad);
        g.fillRect (b.withTop (y));
        g.setColour (hold > 1.0f ? Theme::danger : Theme::text);
        g.fillRect (b.withTop (toY (hold)).withHeight (1.5f));
    };
    auto left = r.reduced (1.0f).removeFromLeft (r.getWidth() * 0.5f - 1.0f);
    auto right = r.reduced (1.0f).removeFromRight (r.getWidth() * 0.5f - 1.0f);
    bar (left, levelL, holdL);
    bar (right, levelR, holdR);
}

// ============================================================================ WaveformCache
const std::vector<std::pair<float, float>>& WaveformCache::get (const SampleData& d)
{
    static std::map<juce::String, std::vector<std::pair<float, float>>> cache;
    const auto key = d.path + "|" + juce::String (d.numFrames()) + "|" + juce::String::toHexString ((juce::pointer_sized_int) &d);
    auto it = cache.find (key);
    if (it != cache.end()) return it->second;
    if (cache.size() > 64) cache.clear();
    constexpr int bins = 4096;
    std::vector<std::pair<float, float>> v ((size_t) bins, { 0.0f, 0.0f });
    const int n = d.numFrames();
    for (int b = 0; b < bins; ++b)
    {
        const int s0 = (int) ((juce::int64) n * b / bins), s1 = juce::jmax (s0 + 1, (int) ((juce::int64) n * (b + 1) / bins));
        float lo = 0, hi = 0;
        for (int i = s0; i < s1 && i < n; ++i)
        {
            const float s = d.buffer.getSample (0, i);
            lo = juce::jmin (lo, s); hi = juce::jmax (hi, s);
        }
        v[(size_t) b] = { lo, hi };
    }
    return cache[key] = std::move (v);
}

void WaveformCache::draw (juce::Graphics& g, const SampleData& d, juce::Rectangle<float> area, double startSec, double endSec, juce::Colour c)
{
    if (d.numFrames() < 2 || endSec <= startSec) return;
    const auto& peaks = get (d);
    const double total = d.numFrames() / d.sampleRate;
    const float mid = area.getCentreY(), half = area.getHeight() * 0.48f;
    g.setColour (c);
    const int w = (int) area.getWidth();
    const bool fine = (endSec - startSec) / total * 4096.0 < w * 0.75;  // zoomed in: read samples directly
    for (int x = 0; x < w; ++x)
    {
        const double t0 = startSec + (endSec - startSec) * x / w, t1 = startSec + (endSec - startSec) * (x + 1) / w;
        if (t0 >= total) break;
        float lo = 0, hi = 0;
        if (fine)
        {
            const int s0 = juce::jlimit (0, d.numFrames() - 1, (int) (t0 * d.sampleRate));
            const int s1 = juce::jlimit (s0 + 1, d.numFrames(), (int) (t1 * d.sampleRate) + 1);
            for (int i = s0; i < s1; ++i) { const float s = d.buffer.getSample (0, i); lo = juce::jmin (lo, s); hi = juce::jmax (hi, s); }
        }
        else
        {
            const int b0 = juce::jlimit (0, 4095, (int) (t0 / total * 4096.0));
            const int b1 = juce::jlimit (b0 + 1, 4096, (int) (t1 / total * 4096.0) + 1);
            for (int b = b0; b < b1; ++b) { lo = juce::jmin (lo, peaks[(size_t) b].first); hi = juce::jmax (hi, peaks[(size_t) b].second); }
        }
        g.drawVerticalLine ((int) area.getX() + x, mid - hi * half, mid - lo * half + 1.0f);
    }
}
