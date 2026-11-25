#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "AppState.h"
#include "Composer.h"
#include "Theme.h"
#include "TransportBar.h"
#include "ArrangementView.h"
#include "FractalView.h"
#include "PianoRoll.h"
#include "GeneratorPage.h"
#include "SynthPage.h"
#include "DrumPage.h"
#include "MixerPage.h"
#include "AudioEditorPage.h"

class MainComponent : public juce::Component,
                      public juce::ApplicationCommandTarget,
                      public juce::MenuBarModel,
                      private juce::Timer
{
public:
    enum Commands
    {
        cmdNewDemo = 0x2000, cmdNewEmpty, cmdOpen, cmdSave, cmdSaveAs, cmdExportAudio, cmdExportMidi, cmdAudioSettings, cmdQuit,
        cmdUndo, cmdRedo, cmdDuplicate, cmdDelete, cmdSplit,
        cmdPlay, cmdStop, cmdRecord, cmdLoop, cmdHome,
        cmdAddSynth, cmdAddDrums, cmdAddAudio, cmdImportAudio, cmdAutoGenerators, cmdGenerateAll,
        cmdWorkspace1, cmdWorkspace2, cmdWorkspace3, cmdWorkspace4, cmdWorkspace5, cmdWorkspace6, cmdWorkspace7, cmdWorkspace8,
        cmdShortcuts
    };

    MainComponent (Engine& engine, juce::ApplicationProperties& props);
    ~MainComponent() override;

    void resized() override;
    void paint (juce::Graphics&) override;

    // project management
    void newProject (bool demo);
    bool openProject (const juce::File& f);
    bool saveProject (bool saveAs);
    bool hasUnsavedChanges() const;
    juce::String windowTitle() const;
    std::function<void (const juce::String&)> onTitleChanged;

    // ApplicationCommandTarget
    juce::ApplicationCommandTarget* getNextCommandTarget() override { return nullptr; }
    void getAllCommands (juce::Array<juce::CommandID>&) override;
    void getCommandInfo (juce::CommandID, juce::ApplicationCommandInfo&) override;
    bool perform (const InvocationInfo&) override;
    juce::ApplicationCommandManager& getCommandManager() { return commands; }

    // MenuBarModel
    juce::StringArray getMenuBarNames() override;
    juce::PopupMenu getMenuForIndex (int, const juce::String&) override;
    void menuItemSelected (int, int) override {}

    /** Headless UI test: render every workspace to PNG files in 'dir'. */
    void captureScreenshots (const juce::File& dir);

private:
    void timerCallback() override;
    void setProject (juce::ValueTree p);
    void showAudioSettings();
    void showExportDialog (bool midi);
    void buildDemoContent();

    Engine& engine;
    juce::ApplicationProperties& props;
    AppState state;
    Composer composer;
    MandelLookAndFeel lnf;
    juce::ApplicationCommandManager commands;
    juce::MenuBarComponent menuBar { this };
    TransportBar transport;
    juce::TabbedComponent tabs { juce::TabbedButtonBar::TabsAtTop };
    ArrangementView arrangement;
    FractalPage fractal;
    PianoRollPage pianoRoll;
    GeneratorPage generators;
    SynthPage synth;
    DrumPage drums;
    MixerPage mixer;
    AudioEditorPage audioEditor;
    juce::TooltipWindow tooltips { this, 600 };
    std::unique_ptr<juce::FileChooser> chooser;
    struct DirtyListener : juce::ValueTree::Listener
    {
        bool modified = false;
        void valueTreePropertyChanged (juce::ValueTree& t, const juce::Identifier&) override
        { if (! t.hasType (IDs::FRACTAL) && ! t.hasType (IDs::KEY) && ! (t.hasType (IDs::TRACK) && false)) modified = true; }
        void valueTreeChildAdded (juce::ValueTree& p, juce::ValueTree&) override { if (! p.hasType (IDs::PATH)) modified = true; }
        void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override { modified = true; }
        void valueTreeChildOrderChanged (juce::ValueTree&, int, int) override { modified = true; }
    } dirty;
    juce::String lastTitle;
    juce::uint32 lastAutosave = 0;
};
