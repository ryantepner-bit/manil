#pragma once
#include "AppState.h"
#include "Composer.h"
#include "Widgets.h"
#include "FractalView.h"

/** Scrolling view of what the live parts just played (one lane per part). */
class LiveNoteView : public juce::Component
{
public:
    LiveNoteView (AppState& s, Composer& c) : state (s), composer (c) {}
    void paint (juce::Graphics&) override;
private:
    AppState& state;
    Composer& composer;
};

/** One row of part controls: on/off, density, octave, MIDI channel. */
class LivePartRow : public juce::Component
{
public:
    LivePartRow (AppState& s, juce::ValueTree partNode, juce::Colour colour);
    void resized() override;
    void paint (juce::Graphics&) override;
    juce::ValueTree node;
private:
    AppState& state;
    juce::Colour colour;
    PropToggle on;
    juce::Slider density;
    ParamChoice octave { {}, { "Oct 0", "Oct 1", "Oct 2", "Oct 3", "Oct 4", "Oct 5", "Oct 6", "Oct 7" } };
    ParamChoice channel { {}, { "Ch 1", "Ch 2", "Ch 3", "Ch 4", "Ch 5", "Ch 6", "Ch 7", "Ch 8", "Ch 9", "Ch 10", "Ch 11", "Ch 12", "Ch 13", "Ch 14", "Ch 15", "Ch 16" } };
};

/**
 * LIVE: the main screen. Opens playing. The fractal probe wanders the boundary
 * of the set (or goes where you click) and its orbit is mapped onto the chosen
 * key and scale for drums, bass, chords, lead and arp.
 */
class LivePage : public juce::Component, private juce::Timer, private juce::ValueTree::Listener
{
public:
    LivePage (AppState& s, Composer& c);
    ~LivePage() override;
    void setProject (juce::ValueTree p);
    void resized() override;
    void paint (juce::Graphics&) override;
private:
    void timerCallback() override;
    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override {}
    void refreshMidiOuts();
    void viewChanged();

    AppState& state;
    Composer& composer;
    juce::ValueTree project, live;
    FractalCanvas canvas;
    LiveNoteView notes;
    PropToggle liveOn { "LIVE", Theme::good };
    juce::TextButton playBtn { "PLAY" };
    ParamChoice root { "Key", {} }, scale { "Scale", {} };
    ParamKnob drift { "Drift", 0, 1 }, complexity { "Complexity", 0, 1 };
    juce::OwnedArray<LivePartRow> rows;
    juce::ComboBox midiOut;
    juce::StringArray midiIds;
    juce::TextButton capture8 { "Capture last 8 bars" }, capture16 { "Capture last 16 bars" }, resetView { "Reset view" };
    juce::Label info, mapping;
};
