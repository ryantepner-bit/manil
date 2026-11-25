#pragma once
#include "AppState.h"
#include "Composer.h"
#include "Widgets.h"

class TrackHeader : public juce::Component
{
public:
    TrackHeader (AppState& s, juce::ValueTree t);
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    juce::ValueTree track;
private:
    AppState& state;
    juce::Label name;
    PropToggle mute { "M", Theme::warn }, solo { "S", Theme::good }, arm { "R", Theme::danger }, autoBtn { "A", Theme::accent3 };
    juce::ComboBox autoParam;
    juce::StringArray autoIds;
};

class ArrangementView : public juce::Component,
                        public juce::FileDragAndDropTarget,
                        private juce::ValueTree::Listener,
                        private juce::Timer,
                        private juce::ChangeListener,
                        private juce::ScrollBar::Listener
{
public:
    ArrangementView (AppState& s, Composer& c);
    ~ArrangementView() override;
    void setProject (juce::ValueTree p);

    void paint (juce::Graphics&) override;
    void paintBody (juce::Graphics&);
    void paintBodyOver (juce::Graphics&);
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool keyPressed (const juce::KeyPress&) override;

    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray&, int x, int y) override;

    // commands (also used by the main menu)
    void duplicateSelected();
    void deleteSelected();
    void splitSelectedAtPlayhead();
    void addTrack (const juce::String& type);
    void importAudio();
    void addAudioFileToTrack (const juce::File& f, juce::ValueTree track, double beat);

private:
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override { repaint(); }
    void valueTreeChildAdded (juce::ValueTree& p, juce::ValueTree&) override { structureChanged (p); }
    void valueTreeChildRemoved (juce::ValueTree& p, juce::ValueTree&, int) override { structureChanged (p); }
    void valueTreeChildOrderChanged (juce::ValueTree& p, int, int) override { structureChanged (p); }
    void structureChanged (juce::ValueTree& parent);
    void timerCallback() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override { repaint(); }
    void scrollBarMoved (juce::ScrollBar*, double) override;

    void rebuildHeaders();
    void layoutHeaders();
    juce::Rectangle<int> timelineArea() const;
    float beatToX (double beat) const;
    double xToBeat (float x) const;
    double snap (double beat) const;
    int trackRowHeight (const juce::ValueTree& t) const;
    juce::ValueTree trackAtY (int y, bool& inAutomation, juce::Rectangle<int>& rowArea) const;
    juce::Rectangle<int> trackRow (const juce::ValueTree& t, bool automationPart) const;
    juce::ValueTree clipAt (juce::Point<int> p) const;
    juce::Rectangle<float> clipBounds (const juce::ValueTree& clip) const;
    void paintClip (juce::Graphics&, const juce::ValueTree& clip, juce::Rectangle<float> r, juce::Colour col) const;
    void paintAutomation (juce::Graphics&, const juce::ValueTree& track, juce::Rectangle<int> r) const;
    juce::String autoParamOf (const juce::ValueTree& track) const;
    void clipMenu (juce::ValueTree clip);
    void openEditorFor (juce::ValueTree clip);
    double contentEnd() const;
    void updateScrollbars();

    struct Body : juce::Component
    {
        explicit Body (ArrangementView& o) : owner (o) { setWantsKeyboardFocus (true); }
        ArrangementView& owner;
        void paint (juce::Graphics& g) override { owner.paintBody (g); }
        void paintOverChildren (juce::Graphics& g) override { owner.paintBodyOver (g); }
        void mouseDown (const juce::MouseEvent& e) override { grabKeyboardFocus(); owner.mouseDown (e); }
        void mouseDrag (const juce::MouseEvent& e) override { owner.mouseDrag (e); }
        void mouseUp (const juce::MouseEvent& e) override { owner.mouseUp (e); }
        void mouseMove (const juce::MouseEvent& e) override { owner.mouseMove (e); }
        void mouseDoubleClick (const juce::MouseEvent& e) override { owner.mouseDoubleClick (e); }
        void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& d) override { owner.mouseWheelMove (e, d); }
        bool keyPressed (const juce::KeyPress& k) override { return owner.keyPressed (k); }
    };

    AppState& state;
    Composer& composer;
    juce::ValueTree project;
    Body body { *this };
    juce::OwnedArray<TrackHeader> headers;
    juce::TextButton addSynth { "+ Synth" }, addDrums { "+ Drums" }, addAudio { "+ Audio" }, importBtn { "Import WAV" }, genAllBtn { "Generate All" };
    juce::ComboBox snapBox;
    juce::Slider zoom;
    juce::ScrollBar hScroll { false }, vScroll { true };

    double pixelsPerBeat = 24.0, scrollBeats = 0.0;
    int scrollY = 0;
    static constexpr int headerW = 220, rulerH = 30, toolbarH = 38, autoH = 60;

    enum class Drag { None, MoveClip, ResizeClip, TrimClipStart, Loop, Playhead, AutoPoint };
    Drag drag = Drag::None;
    juce::ValueTree dragClip, dragPoint, dragAutoTrack;
    double dragOrigStart = 0, dragOrigLength = 0, dragOrigOffset = 0, dragDownBeat = 0;
    bool dragCopied = false;
    std::unique_ptr<juce::FileChooser> chooser;
};
