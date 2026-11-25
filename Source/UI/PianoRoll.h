#pragma once
#include "AppState.h"
#include "Widgets.h"

class PianoRollGrid : public juce::Component, private juce::ValueTree::Listener, private juce::Timer
{
public:
    explicit PianoRollGrid (AppState& s);
    ~PianoRollGrid() override;
    void setClip (juce::ValueTree c);
    juce::ValueTree getClip() const { return clip; }

    double gridBeats = 0.25;
    int scaleRoot = 9, scaleKind = 1;
    bool highlightScale = true;
    bool laneShowsProbability = false;

    void quantizeSelection();
    void transposeSelection (int semis);
    void selectAll();
    void deleteSelection();
    void copySelection();
    void paste();
    void humanize();
    void legato();

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override { repaint(); }
    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override { repaint(); }
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override { pruneSelection(); repaint(); }
    void valueTreeChildOrderChanged (juce::ValueTree&, int, int) override {}
    void timerCallback() override { repaint(); }
    void pruneSelection();

    juce::Rectangle<int> gridArea() const;
    juce::Rectangle<int> laneArea() const;
    float beatToX (double b) const;
    double xToBeat (float x) const;
    float pitchToY (int p) const;
    int yToPitch (float y) const;
    double snapBeat (double b) const;
    juce::Rectangle<float> noteRect (const juce::ValueTree& n) const;
    juce::ValueTree noteAt (juce::Point<float> p, bool& onRightEdge) const;
    void audition (int pitch, int vel);
    bool isSelected (const juce::ValueTree& n) const { return selection.contains (n); }

    AppState& state;
    juce::ValueTree clip;
    juce::Array<juce::ValueTree> selection;
    static constexpr int keyW = 60, laneH = 96;
    double ppb = 96.0, scrollBeat = 0.0;
    float noteH = 13.0f;
    int topPitch = 84;
    double lastLength = 0.25;

    enum class Mode { None, Draw, Move, Resize, Rubber, Lane };
    Mode mode = Mode::None;
    juce::Point<float> downPos;
    double downBeat = 0; int downPitch = 60;
    struct Orig { juce::ValueTree n; NoteData d; };
    std::vector<Orig> origs;
    juce::ValueTree drawNote;
    juce::Rectangle<float> rubber;
    int auditionNote = -1;
    std::vector<NoteData> clipboard;
};

class PianoRollPage : public juce::Component, private juce::ChangeListener
{
public:
    explicit PianoRollPage (AppState& s);
    ~PianoRollPage() override;
    void setProject (juce::ValueTree p);
    void resized() override;
    void paint (juce::Graphics&) override;
private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    AppState& state;
    PianoRollGrid grid;
    juce::Label title;
    juce::ComboBox snapBox, rootBox, scaleBox;
    juce::TextButton quantize { "Quantize" }, up { "+12" }, down { "-12" }, selAll { "Select all" }, del { "Delete" },
                     human { "Humanize" }, leg { "Legato" }, laneMode { "Lane: Velocity" }, newClip { "New clip" };
    juce::ToggleButton scaleHi { "Scale" };
};
