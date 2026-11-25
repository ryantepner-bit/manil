#pragma once
#include "AppState.h"
#include "Composer.h"
#include "Widgets.h"

class MotifView : public juce::Component
{
public:
    void setMotif (const juce::String& s) { motif = s; repaint(); }
    void paint (juce::Graphics&) override;
private:
    juce::String motif;
};

class GeneratorPage : public juce::Component,
                      private juce::ListBoxModel,
                      private juce::ValueTree::Listener,
                      private juce::ChangeListener
{
public:
    GeneratorPage (AppState& s, Composer& c);
    ~GeneratorPage() override;
    void setProject (juce::ValueTree p);
    void resized() override;
    void paint (juce::Graphics&) override;
private:
    int getNumRows() override;
    void paintListBoxItem (int row, juce::Graphics&, int w, int h, bool selected) override;
    void selectedRowsChanged (int row) override;
    void valueTreeChildAdded (juce::ValueTree& p, juce::ValueTree&) override { if (p.hasType (IDs::GENERATORS)) { list.updateContent(); if (list.getSelectedRow() < 0) list.selectRow (0); } }
    void valueTreeChildRemoved (juce::ValueTree& p, juce::ValueTree&, int) override { if (p.hasType (IDs::GENERATORS)) { list.updateContent(); bindSelected(); } }
    void valueTreePropertyChanged (juce::ValueTree& t, const juce::Identifier& p) override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void bindSelected();
    void refreshTracks();
    juce::ValueTree selectedGen() const;

    AppState& state;
    Composer& composer;
    juce::ValueTree project;
    juce::ListBox list { "Generators", this };
    juce::TextButton addBtn { "+ Add generator" }, removeBtn { "Remove" }, autoSetup { "Auto setup (one per track)" };
    juce::ToggleButton liveAll { "Fractal composition mode (live evolution while playing)" };

    ParamChoice role { "Role", {} }, scale { "Scale", {} }, root { "Root", {} };
    juce::ComboBox trackBox;
    ParamKnob density { "Density", 0, 1 }, complexity { "Complexity", 0, 1 }, mutation { "Mutation", 0, 1 },
              bars { "Bars", 1, 16 }, octave { "Octave", 1, 7 }, seed { "Seed", 0, 99999 };
    juce::TextButton dice { "Dice" };
    PropToggle live { "LIVE", Theme::good }, lock { "Lock point", Theme::warn };
    juce::OwnedArray<juce::TextButton> maskBtns;
    juce::TextButton genNew { "Generate new clip at playhead" }, regen { "Regenerate selected clip" },
                     mutate { "Mutate (evolve motif)" }, toProbe { "Point = probe" }, resetMotif { "Forget motif" };
    juce::Label info;
    MotifView motifView;
};
