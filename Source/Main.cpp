#include <juce_gui_basics/juce_gui_basics.h>
#include "UI/MainComponent.h"

class MandelbrotApplication : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return JUCE_APPLICATION_NAME_STRING; }
    const juce::String getApplicationVersion() override { return JUCE_APPLICATION_VERSION_STRING; }
    bool moreThanOneInstanceAllowed() override { return true; }

    void initialise (const juce::String& commandLine) override
    {
        juce::PropertiesFile::Options o;
        o.applicationName = "MANDELBROT";
        o.filenameSuffix = ".settings";
        o.folderName = "MANDELBROT";
        o.osxLibrarySubFolder = "Application Support";
        props.setStorageParameters (o);

        auto args = juce::StringArray::fromTokens (commandLine, true);
        const int shotIdx = args.indexOf ("--screenshot");
        screenshotDir = shotIdx >= 0 ? juce::File (args[shotIdx + 1].unquoted()) : juce::File();
        if (screenshotDir != juce::File()) props.getUserSettings()->setValue ("lastProject", juce::String());

        engine = std::make_unique<Engine>();
        engine->initialiseAudio (props.getUserSettings()->getValue ("audioDevice"));
        window = std::make_unique<MainWindow> (*engine, props);

        if (screenshotDir != juce::File())
        {
            shotTimer.app = this;
            shotTimer.startTimer (1500);
        }
    }

    void shutdown() override
    {
        shotTimer.stopTimer();
        if (engine != nullptr)
            if (auto xml = engine->getDeviceManager().createStateXml())
                props.getUserSettings()->setValue ("audioDevice", xml->toString());
        props.saveIfNeeded();
        window.reset();
        engine.reset();
    }

    void systemRequestedQuit() override
    {
        if (window != nullptr && window->content->hasUnsavedChanges() && screenshotDir == juce::File())
        {
            juce::AlertWindow::showAsync (juce::MessageBoxOptions().withIconType (juce::MessageBoxIconType::QuestionIcon).withTitle ("Quit MANDELBROT")
                                              .withMessage ("The project has unsaved changes. Quit anyway?").withButton ("Quit", 1).withButton ("Cancel", 0),
                                          [] (int r) { if (r == 1) quit(); });
            return;
        }
        quit();
    }

    void anotherInstanceStarted (const juce::String&) override {}

    struct MainWindow : public juce::DocumentWindow
    {
        MainWindow (Engine& e, juce::ApplicationProperties& p)
            : DocumentWindow ("MANDELBROT", Theme::bg, DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar (true);
            content = new MainComponent (e, p);
            content->onTitleChanged = [this] (const juce::String& t) { setName (t); };
            setContentOwned (content, true);
            setResizable (true, true);
            setResizeLimits (1100, 700, 10000, 10000);
            centreWithSize (juce::jmin (1600, getParentMonitorArea().getWidth() - 40), juce::jmin (960, getParentMonitorArea().getHeight() - 60));
            setVisible (true);
            setName (content->windowTitle());
            content->grabKeyboardFocus();
        }
        void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }
        MainComponent* content = nullptr;
    };

private:
    /** Headless UI test: --screenshot <dir> renders every workspace to PNG and quits. */
    struct ShotTimer : juce::Timer
    {
        MandelbrotApplication* app = nullptr;
        void timerCallback() override
        {
            stopTimer();   // stop first: the capture runs a nested message loop
            app->window->content->captureScreenshots (app->screenshotDir);
            juce::JUCEApplication::quit();
        }
    } shotTimer;

    juce::ApplicationProperties props;
    std::unique_ptr<Engine> engine;
    std::unique_ptr<MainWindow> window;
    juce::File screenshotDir;
};

START_JUCE_APPLICATION (MandelbrotApplication)
