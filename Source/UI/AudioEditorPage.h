#pragma once
#include "AppState.h"
#include "Widgets.h"

class WaveEditor : public juce::Component, private juce::Timer
{
public:
    explicit WaveEditor (AppState& s);
    void setData (std::shared_ptr<const SampleData> d);
    std::shared_ptr<const SampleData> getData() const { return data; }
    double selStart = 0, selEnd = 0;   // seconds
    double regionStart = -1, regionEnd = -1;   // clip region (seconds) or -1
    double cursor = 0;
    std::function<void()> onSelectionChanged;
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void zoomToFit() { viewStart = 0; viewEnd = length(); repaint(); }
private:
    void timerCallback() override { repaint(); }
    double length() const { return data ? data->numFrames() / data->sampleRate : 1.0; }
    double xToTime (float x) const { return viewStart + (viewEnd - viewStart) * x / juce::jmax (1, getWidth()); }
    float timeToX (double t) const { return (float) ((t - viewStart) / juce::jmax (1e-9, viewEnd - viewStart) * getWidth()); }
    AppState& state;
    std::shared_ptr<const SampleData> data;
    double viewStart = 0, viewEnd = 1, downTime = 0;
};

class AudioEditorPage : public juce::Component, private juce::ChangeListener, private juce::ValueTree::Listener
{
public:
    explicit AudioEditorPage (AppState& s);
    ~AudioEditorPage() override;
    void setProject (juce::ValueTree p);
    void resized() override;
    void paint (juce::Graphics&) override;
private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void valueTreePropertyChanged (juce::ValueTree& t, const juce::Identifier& p) override;
    void loadFromClip();
    void openFile (const juce::File& f);
    void updateInfo();
    enum class Op { Normalize, Reverse, FadeIn, FadeOut, GainUp, GainDown, Silence, Delete, Crop };
    void applyOp (Op op);
    void range (int& s0, int& s1) const;

    AppState& state;
    juce::ValueTree project, clip;
    juce::File file;
    WaveEditor wave;
    juce::Label info;
    juce::TextButton openBtn { "Open file..." }, toArr { "Add to arrangement" }, play { "Play" }, stop { "Stop" }, fit { "Zoom to fit" },
                     trim { "Trim clip to selection" }, normalize { "Normalize" }, reverse { "Reverse" }, fadeIn { "Fade in" }, fadeOut { "Fade out" },
                     gainUp { "+3 dB" }, gainDown { "-3 dB" }, silence { "Silence" }, del { "Delete" }, crop { "Crop" };
    std::unique_ptr<juce::FileChooser> chooser;
};
