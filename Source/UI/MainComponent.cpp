#include "MainComponent.h"
#include "../Audio/Exporter.h"

MainComponent::MainComponent (Engine& e, juce::ApplicationProperties& p)
    : engine (e), props (p), state (e), composer (state), transport (state),
      livePage (state, composer), arrangement (state, composer), pianoRoll (state), generators (state, composer),
      synth (state, composer), drums (state, composer), mixer (state), audioEditor (state)
{
    juce::LookAndFeel::setDefaultLookAndFeel (&lnf);
    setLookAndFeel (&lnf);
    addAndMakeVisible (menuBar);
    addAndMakeVisible (transport);

    tabs.setTabBarDepth (36);
    tabs.setOutline (0);
    const auto bg = Theme::bg;
    tabs.addTab ("Live", bg, &livePage, false);
    tabs.addTab ("Arrangement", bg, &arrangement, false);
    tabs.addTab ("Piano Roll", bg, &pianoRoll, false);
    tabs.addTab ("Generative", bg, &generators, false);
    tabs.addTab ("Synthesizer", bg, &synth, false);
    tabs.addTab ("Drum Machine", bg, &drums, false);
    tabs.addTab ("Mixer", bg, &mixer, false);
    tabs.addTab ("Audio Editor", bg, &audioEditor, false);
    addAndMakeVisible (tabs);
    state.showWorkspace = [this] (int i) { tabs.setCurrentTabIndex (i); };

    commands.registerAllCommandsForTarget (this);
    commands.setFirstCommandTarget (this);
    addKeyListener (commands.getKeyMappings());
    setWantsKeyboardFocus (true);

    engine.recordingFolder = state.audioFolder().getChildFile ("Recordings");
    engine.onRecordingFinished = [this] (int trackId, juce::File f, double startBeat, double lengthBeats)
    {
        juce::MessageManager::callAsync ([this, trackId, f, startBeat, lengthBeats]
        {
            auto t = Model::findTrack (state.project, trackId);
            if (! t.isValid()) return;
            state.beginEdit ("Record audio");
            state.selectClip (Model::addAudioClip (t, f, startBeat, lengthBeats, &state.undo));
            if (state.setStatus) state.setStatus ("Recorded " + f.getFileName());
        });
    };

    // always start in a fresh live project: open it and the fractal plays (File > Open for saved work)
    newProject (true);
    setSize (1500, 920);
    // open it and it plays: live mode starts straight away
    if ((bool) Model::liveNode (state.project).getProperty (IDs::liveOn, false)) engine.play();
    startTimerHz (4);
}

MainComponent::~MainComponent()
{
    if (state.project.isValid()) state.project.removeListener (&dirty);
    engine.stop();
    removeKeyListener (commands.getKeyMappings());
    setLookAndFeel (nullptr);
    juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
}

void MainComponent::paint (juce::Graphics& g) { g.fillAll (Theme::bg); }

void MainComponent::resized()
{
    auto r = getLocalBounds();
    menuBar.setBounds (r.removeFromTop (24));
    transport.setBounds (r.removeFromTop (56));
    tabs.setBounds (r);
}

// ---------------------------------------------------------------------------- project management
void MainComponent::setProject (juce::ValueTree p)
{
    engine.stop();
    engine.stop();
    if (state.project.isValid()) state.project.removeListener (&dirty);
    state.undo.clearUndoHistory();
    state.selectedClip = {};
    state.selectedTrack = {};
    state.project = p;
    engine.setProject (p);
    engine.setPosition (0.0);
    composer.setProject (p);
    transport.setProject (p);
    livePage.setProject (p);
    arrangement.setProject (p);
    pianoRoll.setProject (p);
    generators.setProject (p);
    synth.setProject (p);
    drums.setProject (p);
    mixer.setProject (p);
    audioEditor.setProject (p);
    engine.recordingFolder = state.audioFolder().getChildFile ("Recordings");
    auto firstTrack = Model::tracks (p).getChild (0);
    state.selectTrack (firstTrack);
    dirty.modified = false;
    p.addListener (&dirty);
    if (onTitleChanged) onTitleChanged (windowTitle());
}

void MainComponent::buildDemoContent()
{
    // a generator per instrument track (Generative workspace); the music itself comes from Live mode
    auto p = state.project;
    const int roles[] = { Gen::Bass, Gen::Chords, Gen::Melody, Gen::Arp };
    int si = 0;
    for (auto t : Model::tracks (p))
    {
        const auto type = t[IDs::type].toString();
        if (type == "audio") continue;
        auto g = composer.addGenerator (t[IDs::id], type == "drums" ? Gen::Breaks : roles[si++ % 4]);
        g.setProperty (IDs::seed, 1000 + si * 17, nullptr);
    }
    state.undo.clearUndoHistory();
    dirty.modified = false;
}

void MainComponent::newProject (bool demo)
{
    state.projectFile = juce::File();
    setProject (demo ? Model::createDefaultProject() : Model::createEmptyProject());
    if (! demo)
    {
        Model::addTrack (state.project, "drums", "Drums", nullptr);
        Model::addTrack (state.project, "synth", "Synth", nullptr);
        Model::addTrack (state.project, "audio", "Audio", nullptr);
        Model::addBus (state.project, "Bus 1", nullptr);
        state.undo.clearUndoHistory();
        state.selectTrack (Model::tracks (state.project).getChild (0));
    }
    else buildDemoContent();
    dirty.modified = false;
    if (onTitleChanged) onTitleChanged (windowTitle());
    if ((bool) Model::liveNode (state.project).getProperty (IDs::liveOn, false)) engine.play();
}

bool MainComponent::openProject (const juce::File& f)
{
    juce::String err;
    auto p = Model::load (f, err);
    if (! p.isValid())
    {
        juce::AlertWindow::showAsync (juce::MessageBoxOptions().withIconType (juce::MessageBoxIconType::WarningIcon).withTitle ("Open failed").withMessage (err).withButton ("OK", 1), nullptr);
        return false;
    }
    state.projectFile = f;
    setProject (p);
    props.getUserSettings()->setValue ("lastProject", f.getFullPathName());
    return true;
}

bool MainComponent::saveProject (bool saveAs)
{
    if (saveAs || state.projectFile == juce::File())
    {
        chooser = std::make_unique<juce::FileChooser> ("Save project", state.projectFile != juce::File() ? state.projectFile
                         : juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("MANDELBROT").getChildFile ("Untitled.mandelbrot"), "*.mandelbrot");
        chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting,
            [this] (const juce::FileChooser& fc)
            {
                auto f = fc.getResult();
                if (f == juce::File()) return;
                f = f.withFileExtension ("mandelbrot");
                f.getParentDirectory().createDirectory();
                state.projectFile = f;
                state.project.setProperty (IDs::name, f.getFileNameWithoutExtension(), nullptr);
                saveProject (false);
            });
        return false;
    }
    if (! Model::save (state.project, state.projectFile))
    {
        juce::AlertWindow::showAsync (juce::MessageBoxOptions().withIconType (juce::MessageBoxIconType::WarningIcon).withTitle ("Save failed").withMessage ("Could not write " + state.projectFile.getFullPathName()).withButton ("OK", 1), nullptr);
        return false;
    }
    dirty.modified = false;
    props.getUserSettings()->setValue ("lastProject", state.projectFile.getFullPathName());
    props.saveIfNeeded();
    engine.recordingFolder = state.audioFolder().getChildFile ("Recordings");
    if (state.setStatus) state.setStatus ("Saved " + state.projectFile.getFullPathName());
    if (onTitleChanged) onTitleChanged (windowTitle());
    return true;
}

bool MainComponent::hasUnsavedChanges() const { return dirty.modified; }

juce::String MainComponent::windowTitle() const
{
    return "MANDELBROT " + juce::String (JUCE_APPLICATION_VERSION_STRING) + "  -  " + (state.projectFile != juce::File() ? state.projectFile.getFileNameWithoutExtension() : juce::String ("Untitled")) + (dirty.modified ? " *" : "");
}

void MainComponent::timerCallback()
{
    const auto t = windowTitle();
    if (t != lastTitle) { lastTitle = t; if (onTitleChanged) onTitleChanged (t); }
    // autosave every 2 minutes (never overwrites the real project file)
    const auto now = juce::Time::getMillisecondCounter();
    if (dirty.modified && now - lastAutosave > 120000)
    {
        lastAutosave = now;
        auto f = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("MANDELBROT").getChildFile ("autosave.mandelbrot");
        f.getParentDirectory().createDirectory();
        Model::save (state.project, f);
    }
}

// ---------------------------------------------------------------------------- dialogs
void MainComponent::showAudioSettings()
{
    auto* sel = new juce::AudioDeviceSelectorComponent (engine.getDeviceManager(), 0, 2, 2, 2, true, false, true, false);
    sel->setSize (560, 460);
    juce::DialogWindow::LaunchOptions o;
    o.content.setOwned (sel);
    o.dialogTitle = "Audio & MIDI settings";
    o.dialogBackgroundColour = Theme::panel;
    o.useNativeTitleBar = true;
    o.resizable = true;
    o.launchAsync();
}

namespace
{
    struct ExportOptionsComponent : juce::Component
    {
        juce::ComboBox kind, rate, bits, range;
        juce::TextButton go { "Export..." }, cancel { "Cancel" };
        std::function<void()> onGo;
        ExportOptionsComponent()
        {
            kind.addItemList ({ "Stereo mixdown (WAV)", "Individual track stems (WAV)" }, 1); kind.setSelectedItemIndex (0);
            rate.addItemList ({ "44.1 kHz", "48 kHz" }, 1); rate.setSelectedItemIndex (0);
            bits.addItemList ({ "16-bit PCM", "24-bit PCM" }, 1); bits.setSelectedItemIndex (1);
            range.addItemList ({ "Whole song", "Loop region" }, 1); range.setSelectedItemIndex (0);
            for (auto* c : std::initializer_list<juce::Component*> { &kind, &rate, &bits, &range, &go, &cancel }) addAndMakeVisible (c);
            go.onClick = [this] { if (onGo) onGo(); };
            cancel.onClick = [this] { if (auto* dw = findParentComponentOfClass<juce::DialogWindow>()) dw->exitModalState (0); };
            setSize (380, 210);
        }
        void resized() override
        {
            auto r = getLocalBounds().reduced (16);
            for (auto* c : { &kind, &rate, &bits, &range }) { c->setBounds (r.removeFromTop (28)); r.removeFromTop (8); }
            auto b = r.removeFromBottom (30);
            go.setBounds (b.removeFromRight (110)); b.removeFromRight (8); cancel.setBounds (b.removeFromRight (90));
        }
    };

    struct RenderThread : juce::ThreadWithProgressWindow
    {
        RenderThread (Engine& e, juce::ValueTree p, Exporter::AudioJob j) : ThreadWithProgressWindow ("Rendering...", true, true), engine (e), project (p), job (j) {}
        void run() override
        {
            ok = Exporter::exportAudio (engine, project, job, [this] (float pr) { setProgress (pr); return ! threadShouldExit(); }, error, written);
        }
        Engine& engine; juce::ValueTree project; Exporter::AudioJob job;
        bool ok = false; juce::String error; juce::StringArray written;
    };
}

void MainComponent::showExportDialog (bool midi)
{
    engine.stop();
    engine.rebuildNow();
    if (midi)
    {
        chooser = std::make_unique<juce::FileChooser> ("Export multitrack MIDI", juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile (state.project[IDs::name].toString() + ".mid"), "*.mid");
        chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting, [this] (const juce::FileChooser& fc)
        {
            auto f = fc.getResult();
            if (f == juce::File()) return;
            juce::String err;
            if (Exporter::exportMidi (state.project, f.withFileExtension ("mid"), err)) { if (state.setStatus) state.setStatus ("Exported " + f.withFileExtension ("mid").getFullPathName()); }
            else juce::AlertWindow::showAsync (juce::MessageBoxOptions().withTitle ("MIDI export failed").withMessage (err).withButton ("OK", 1), nullptr);
        });
        return;
    }
    auto* opts = new ExportOptionsComponent();
    opts->onGo = [this, opts]
    {
        Exporter::AudioJob job;
        job.stems = opts->kind.getSelectedItemIndex() == 1;
        job.sampleRate = opts->rate.getSelectedItemIndex() == 0 ? 44100.0 : 48000.0;
        job.bits = opts->bits.getSelectedItemIndex() == 0 ? 16 : 24;
        job.loopOnly = opts->range.getSelectedItemIndex() == 1;
        if (auto* dw = opts->findParentComponentOfClass<juce::DialogWindow>()) dw->exitModalState (1);
        const auto base = juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile (state.project[IDs::name].toString());
        chooser = std::make_unique<juce::FileChooser> (job.stems ? "Choose a folder for the stems" : "Export WAV", job.stems ? base.getParentDirectory() : base.withFileExtension ("wav"), job.stems ? "*" : "*.wav");
        const int flags = job.stems ? (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories)
                                    : (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting);
        chooser->launchAsync (flags, [this, job] (const juce::FileChooser& fc) mutable
        {
            auto f = fc.getResult();
            if (f == juce::File()) return;
            job.folderOrFile = job.stems ? f.getChildFile (juce::File::createLegalFileName (state.project[IDs::name].toString()) + "_stems") : f.withFileExtension ("wav");
            auto* t = new RenderThread (engine, state.project, job);
            t->launchThread();
            // poll for completion on the message thread
            struct Poll : juce::Timer { RenderThread* t; AppState* st; void timerCallback() override { if (t->isThreadRunning()) return; stopTimer();
                if (t->ok) { if (st->setStatus) st->setStatus ("Exported " + juce::String (t->written.size()) + " file(s): " + t->written.joinIntoString (", ")); }
                else juce::AlertWindow::showAsync (juce::MessageBoxOptions().withTitle ("Export failed").withMessage (t->error).withButton ("OK", 1), nullptr);
                delete t; delete this; } };
            auto* p = new Poll(); p->t = t; p->st = &state; p->startTimer (200);
        });
    };
    juce::DialogWindow::LaunchOptions o;
    o.content.setOwned (opts);
    o.dialogTitle = "Export audio";
    o.dialogBackgroundColour = Theme::panel;
    o.useNativeTitleBar = true;
    o.launchAsync();
}

// ---------------------------------------------------------------------------- commands
void MainComponent::getAllCommands (juce::Array<juce::CommandID>& c)
{
    for (int i = cmdNewDemo; i <= cmdShortcuts; ++i) c.add (i);
}

void MainComponent::getCommandInfo (juce::CommandID id, juce::ApplicationCommandInfo& info)
{
    using KP = juce::KeyPress;
    const auto cmd = juce::ModifierKeys::commandModifier, shift = juce::ModifierKeys::shiftModifier;
    switch (id)
    {
        case cmdNewDemo: info.setInfo ("New generative project", "Template with tracks + generators + a generated loop", "File", 0); info.addDefaultKeypress ('n', cmd); break;
        case cmdNewEmpty: info.setInfo ("New empty project", {}, "File", 0); info.addDefaultKeypress ('n', cmd | shift); break;
        case cmdOpen: info.setInfo ("Open...", {}, "File", 0); info.addDefaultKeypress ('o', cmd); break;
        case cmdSave: info.setInfo ("Save", {}, "File", 0); info.addDefaultKeypress ('s', cmd); break;
        case cmdSaveAs: info.setInfo ("Save as...", {}, "File", 0); info.addDefaultKeypress ('s', cmd | shift); break;
        case cmdExportAudio: info.setInfo ("Export audio (WAV / stems)...", {}, "File", 0); info.addDefaultKeypress ('r', cmd); break;
        case cmdExportMidi: info.setInfo ("Export multitrack MIDI...", {}, "File", 0); info.addDefaultKeypress ('m', cmd | shift); break;
        case cmdAudioSettings: info.setInfo ("Audio & MIDI settings...", {}, "File", 0); info.addDefaultKeypress (',', cmd); break;
        case cmdQuit: info.setInfo ("Quit", {}, "File", 0); info.addDefaultKeypress ('q', cmd); break;
        case cmdUndo: info.setInfo ("Undo " + state.undo.getUndoDescription(), {}, "Edit", 0); info.setActive (state.undo.canUndo()); info.addDefaultKeypress ('z', cmd); break;
        case cmdRedo: info.setInfo ("Redo " + state.undo.getRedoDescription(), {}, "Edit", 0); info.setActive (state.undo.canRedo()); info.addDefaultKeypress ('y', cmd); info.addDefaultKeypress ('z', cmd | shift); break;
        case cmdDuplicate: info.setInfo ("Duplicate clip", {}, "Edit", 0); info.setActive (state.selectedClip.isValid()); break;
        case cmdDelete: info.setInfo ("Delete clip", {}, "Edit", 0); info.setActive (state.selectedClip.isValid()); break;
        case cmdSplit: info.setInfo ("Split clip at playhead", {}, "Edit", 0); info.setActive (state.selectedClip.isValid()); break;
        case cmdPlay: info.setInfo ("Play / Pause", {}, "Transport", 0); info.addDefaultKeypress (KP::spaceKey, 0); break;
        case cmdStop: info.setInfo ("Stop / Return", {}, "Transport", 0); info.addDefaultKeypress (KP::returnKey, 0); break;
        case cmdRecord: info.setInfo ("Record enable", {}, "Transport", 0); info.setTicked (engine.isRecordEnabled()); break;
        case cmdLoop: info.setInfo ("Pattern / Song mode", "PAT loops the selected clip, SONG plays the whole arrangement", "Transport", 0); info.setTicked (! (bool) state.project.getProperty (IDs::songMode, true)); info.addDefaultKeypress ('l', 0); break;
        case cmdHome: info.setInfo ("Go to start", {}, "Transport", 0); info.addDefaultKeypress (KP::homeKey, 0); break;
        case cmdAddSynth: info.setInfo ("Add synth track", {}, "Track", 0); info.addDefaultKeypress ('t', cmd); break;
        case cmdAddDrums: info.setInfo ("Add drum track", {}, "Track", 0); break;
        case cmdAddAudio: info.setInfo ("Add audio track", {}, "Track", 0); break;
        case cmdImportAudio: info.setInfo ("Import audio file...", {}, "Track", 0); info.addDefaultKeypress ('i', cmd); break;
        case cmdAutoGenerators: info.setInfo ("Auto-setup generators", "One generator per instrument track", "Generate", 0); break;
        case cmdGenerateAll: info.setInfo ("Generate all at playhead", {}, "Generate", 0); info.addDefaultKeypress ('g', cmd); break;
        case cmdShortcuts: info.setInfo ("Keyboard shortcuts", {}, "Help", 0); break;
        default:
            if (id >= cmdWorkspace1 && id <= cmdWorkspace8)
            {
                const int i = id - cmdWorkspace1;
                info.setInfo (tabs.getTabNames()[i], {}, "View", 0);
                info.addDefaultKeypress (KP::F1Key + i, 0);
                info.setTicked (tabs.getCurrentTabIndex() == i);
            }
            break;
    }
}

bool MainComponent::perform (const InvocationInfo& info)
{
    auto confirmDiscard = [this] (std::function<void()> fn)
    {
        if (! hasUnsavedChanges()) { fn(); return; }
        juce::AlertWindow::showAsync (juce::MessageBoxOptions().withIconType (juce::MessageBoxIconType::QuestionIcon).withTitle ("Unsaved changes")
                                          .withMessage ("Discard unsaved changes to the current project?").withButton ("Discard", 1).withButton ("Cancel", 0),
                                      [fn] (int r) { if (r == 1) fn(); });
    };
    switch (info.commandID)
    {
        case cmdNewDemo: confirmDiscard ([this] { newProject (true); }); break;
        case cmdNewEmpty: confirmDiscard ([this] { newProject (false); }); break;
        case cmdOpen:
            confirmDiscard ([this]
            {
                chooser = std::make_unique<juce::FileChooser> ("Open project", juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("MANDELBROT"), "*.mandelbrot");
                chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this] (const juce::FileChooser& fc) { if (fc.getResult().existsAsFile()) openProject (fc.getResult()); });
            });
            break;
        case cmdSave: saveProject (false); break;
        case cmdSaveAs: saveProject (true); break;
        case cmdExportAudio: showExportDialog (false); break;
        case cmdExportMidi: showExportDialog (true); break;
        case cmdAudioSettings: showAudioSettings(); break;
        case cmdQuit: juce::JUCEApplication::getInstance()->systemRequestedQuit(); break;
        case cmdUndo: state.undo.undo(); break;
        case cmdRedo: state.undo.redo(); break;
        case cmdDuplicate: arrangement.duplicateSelected(); break;
        case cmdDelete: arrangement.deleteSelected(); break;
        case cmdSplit: arrangement.splitSelectedAtPlayhead(); break;
        case cmdPlay: engine.togglePlay(); break;
        case cmdStop: engine.stop(); break;
        case cmdRecord: engine.setRecordEnabled (! engine.isRecordEnabled()); break;
        case cmdLoop: state.project.setProperty (IDs::songMode, ! (bool) state.project.getProperty (IDs::songMode, true), nullptr); break;
        case cmdHome: engine.setPosition (0.0); break;
        case cmdAddSynth: arrangement.addTrack ("synth"); break;
        case cmdAddDrums: arrangement.addTrack ("drums"); break;
        case cmdAddAudio: arrangement.addTrack ("audio"); break;
        case cmdImportAudio: arrangement.importAudio(); break;
        case cmdAutoGenerators: tabs.setCurrentTabIndex (AppState::Generative); break;
        case cmdGenerateAll: composer.generateAll (std::floor (engine.getPosition() / state.beatsPerBar()) * state.beatsPerBar()); break;
        case cmdShortcuts:
            juce::AlertWindow::showAsync (juce::MessageBoxOptions().withTitle ("Shortcuts").withButton ("OK", 1).withMessage (
                "Space  play / stop (returns to where you started)      L  PAT / SONG mode      Home  start\n"
                "Ctrl+Z / Ctrl+Y  undo / redo      Ctrl+S  save      Ctrl+R  export audio\n"
                "F1..F8  workspaces      Ctrl+G  generate all at playhead\n\n"
                "Live: click the fractal to move the probe, wheel zoom, drag pan, Capture keeps the last bars\n"
                "Arrangement: drag clips (Ctrl-drag copies), drag right edge to resize, double-click empty to create,\n"
                "  Ctrl+D duplicate, Ctrl+E split, Delete, drag on ruler = loop selection (click ruler clears), Ctrl+wheel zoom\n"
                "Piano roll: click to draw, drag to move/resize, Shift/Ctrl-drag select, arrows transpose, Q quantize,\n"
                "  Alt/right-click delete, Ctrl+C/V, Ctrl+wheel zoom, Alt+wheel row height\n"
                "Fractal: Shift-drag = analyse region, Ctrl+Shift-drag = zoom to region"), nullptr);
            break;
        default:
            if (info.commandID >= cmdWorkspace1 && info.commandID <= cmdWorkspace8) tabs.setCurrentTabIndex (info.commandID - cmdWorkspace1);
            else return false;
    }
    return true;
}

juce::StringArray MainComponent::getMenuBarNames() { return { "File", "Edit", "Transport", "Track", "Generate", "View", "Help" }; }

juce::PopupMenu MainComponent::getMenuForIndex (int index, const juce::String&)
{
    juce::PopupMenu m;
    auto add = [&] (int c) { m.addCommandItem (&commands, c); };
    switch (index)
    {
        case 0: add (cmdNewDemo); add (cmdNewEmpty); add (cmdOpen); m.addSeparator(); add (cmdSave); add (cmdSaveAs); m.addSeparator(); add (cmdExportAudio); add (cmdExportMidi); m.addSeparator(); add (cmdAudioSettings); m.addSeparator(); add (cmdQuit); break;
        case 1: add (cmdUndo); add (cmdRedo); m.addSeparator(); add (cmdDuplicate); add (cmdSplit); add (cmdDelete); break;
        case 2: add (cmdPlay); add (cmdStop); add (cmdHome); add (cmdRecord); add (cmdLoop); break;
        case 3: add (cmdAddSynth); add (cmdAddDrums); add (cmdAddAudio); m.addSeparator(); add (cmdImportAudio); break;
        case 4: add (cmdGenerateAll); add (cmdAutoGenerators); break;
        case 5: for (int c = cmdWorkspace1; c <= cmdWorkspace8; ++c) add (c); break;
        case 6: add (cmdShortcuts); break;
        default: break;
    }
    return m;
}

// ---------------------------------------------------------------------------- screenshot test mode
void MainComponent::captureScreenshots (const juce::File& dir)
{
    dir.createDirectory();
    for (int i = 0; i < tabs.getNumTabs(); ++i)
    {
        tabs.setCurrentTabIndex (i);
        if (i == AppState::PianoRoll || i == AppState::Drums)
        {
            // show real content: select the first clip of the right kind
            for (auto t : Model::tracks (state.project))
                for (auto c : t.getChildWithName (IDs::CLIPS))
                    if (c[IDs::type].toString() == (i == AppState::PianoRoll ? "midi" : "pattern")) { state.selectClip (c); goto found; }
            found:;
        }
        // let async renders / timers settle
        const auto until = juce::Time::getMillisecondCounter() + (i == AppState::Live ? 3000u : 600u);
        while (juce::Time::getMillisecondCounter() < until)
            juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
        auto img = createComponentSnapshot (getLocalBounds(), true, 1.0f);
        juce::FileOutputStream os (dir.getChildFile (juce::String (i + 1) + "_" + tabs.getTabNames()[i].replaceCharacter (' ', '_') + ".png"));
        if (os.openedOk()) { os.setPosition (0); os.truncate(); juce::PNGImageFormat().writeImageToStream (img, os); }
    }
}
