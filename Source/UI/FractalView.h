#pragma once
#include "AppState.h"
#include "Composer.h"
#include "Widgets.h"

/** Background multi-threaded Mandelbrot renderer with progressive refinement. */
class FractalRenderer : private juce::Thread
{
public:
    struct View { double cx = -0.5, cy = 0.0, scale = 3.0; int w = 0, h = 0, iters = 400, palette = 0; bool supersample = false; };
    FractalRenderer();
    ~FractalRenderer() override;
    void request (const View& v);
    /** Message thread: returns the newest finished image (and its view). */
    bool fetch (juce::Image& img, View& v);
    std::function<void()> onImageReady;    // called on the message thread
    static juce::StringArray paletteNames();
    static juce::Colour paletteColour (int palette, double t);
private:
    void run() override;
    void renderPass (const View& v, int step, juce::Image& img, juce::uint32 version);
    juce::CriticalSection lock;
    View pending, finishedView;
    bool hasPending = false;
    std::atomic<juce::uint32> version { 0 };
    juce::Image finished;
    juce::WaitableEvent wake;
    std::array<juce::Colour, 1024> lut; int lutPalette = -1;
};

class FractalCanvas : public juce::Component, private juce::Timer
{
public:
    FractalCanvas (AppState& s, Composer& c);
    void setView (double cx, double cy, double scale, bool addHistory);
    void setIterations (int it) { view.iters = it; requestRender(); }
    void setPalette (int p) { view.palette = p; requestRender(); }
    void setSupersample (bool b) { view.supersample = b; requestRender(); }
    void back();
    void forward();
    FractalRenderer::View getView() const { return view; }
    std::function<void()> onViewChanged;
    bool autoIterations = true;

    void paint (juce::Graphics&) override;
    void resized() override { requestRender(); }
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseMove (const juce::MouseEvent&) override;

    juce::Point<double> toComplex (juce::Point<float> p) const;
    juce::Point<float> toScreen (double x, double y) const;
private:
    void timerCallback() override;
    void requestRender();
    void pushHistory();
    void applyAutoIterations();

    AppState& state;
    Composer& composer;
    FractalRenderer renderer;
    FractalRenderer::View view, imageView;
    juce::Image image;
    std::vector<FractalRenderer::View> history; int historyPos = -1;
    juce::Point<float> dragStart; FractalRenderer::View dragView;
    bool dragging = false, selecting = false;
    juce::Rectangle<float> selection;
    juce::Point<double> hover;
    juce::uint32 lastWheel = 0; bool wheelPending = false;
};

class FractalPage : public juce::Component, private juce::ChangeListener, private juce::Timer
{
public:
    FractalPage (AppState& s, Composer& c);
    ~FractalPage() override;
    void setProject (juce::ValueTree p);
    void resized() override;
    void paint (juce::Graphics&) override;
private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;
    void refreshLists();
    void viewChanged();

    AppState& state;
    Composer& composer;
    juce::ValueTree project;
    FractalCanvas canvas;
    juce::Label coords, featureText;
    juce::Slider iterSlider;
    juce::ComboBox palette;
    juce::ToggleButton autoIter { "Auto iterations" }, hq { "Supersample (HQ)" }, follow { "Probe follows view" };
    juce::TextButton backBtn { "<" }, fwdBtn { ">" }, resetBtn { "Reset" }, pngBtn { "Save PNG" };
    juce::ComboBox bookmarks; juce::TextButton addBm { "+ Bookmark" }, delBm { "Delete" };
    juce::ComboBox paths; juce::TextButton recPath { "Record path" }, replayPath { "Replay" }, renderPath { "Render path to arrangement" }, delPath { "Delete" };
    juce::TextButton genAll { "Generate all generators here" };
    std::unique_ptr<juce::FileChooser> chooser;
};
