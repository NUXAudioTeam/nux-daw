/* NUX DAW - application entry point and main window. */
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_utils/juce_audio_utils.h>

#include "MainComponent.h"
#include "Theme.h"

namespace nux
{
//==============================================================================
class NuxMainWindow final : public juce::DocumentWindow
{
public:
    explicit NuxMainWindow (const juce::String& name)
        : juce::DocumentWindow (name,
                                theme::windowBg,
                                juce::DocumentWindow::allButtons)
    {
        setUsingNativeTitleBar (true);
        setContentOwned (new MainComponent(), true);
        setResizable (true, false);
        setResizeLimits (1024, 640, 4096, 2400);
        centreWithSize (getWidth(), getHeight());
        setVisible (true);
    }

    void closeButtonPressed() override
    {
        juce::JUCEApplication::getInstance()->systemRequestedQuit();
    }

    MainComponent* getMainComponent() const
    {
        return dynamic_cast<MainComponent*> (getContentComponent());
    }

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NuxMainWindow)
};

//==============================================================================
class NuxApplication final : public juce::JUCEApplication
{
public:
    NuxApplication() = default;

    const juce::String getApplicationName() override    { return constants::appName; }
    const juce::String getApplicationVersion() override { return constants::versionString; }
    bool moreThanOneInstanceAllowed() override          { return true; }

    void initialise (const juce::String& commandLine) override
    {
        mainWindow = std::make_unique<NuxMainWindow> (constants::appName);

        // Headless smoke test: `NUX DAW --demo-clip <file> [--screenshot <png>]`
        // loads a demo project and renders the UI once, then quits. This keeps the
        // GUI verifiable in CI environments that have no interactive display.
        const auto args = juce::StringArray::fromTokens (commandLine, true);

        if (args.contains ("--demo-clip"))
        {
            const auto fileIndex = args.indexOf ("--demo-clip") + 1;

            if (fileIndex < args.size() && mainWindow != nullptr)
                if (auto* main = mainWindow->getMainComponent())
                    main->loadDemoProjectWithAudioClip (resolvePath (args[fileIndex]));

            const auto pluginIndex = args.indexOf ("--plugin");

            if (pluginIndex > 0 && pluginIndex + 2 < args.size() && mainWindow != nullptr)
            {
                if (auto* main = mainWindow->getMainComponent())
                {
                    const auto message = main->loadPluginIntoDemoTrack (0,
                                                                        resolvePath (args[pluginIndex + 1]),
                                                                        args[pluginIndex + 2].unquoted());
                    juce::Logger::writeToLog ("Plugin load: " + message);
                }
            }

            const auto editorIndex = args.indexOf ("--insert-editor");

            if (editorIndex > 0 && editorIndex + 1 < args.size() && mainWindow != nullptr)
            {
                if (auto* main = mainWindow->getMainComponent())
                {
                    const auto message = main->showInsertEditorForDemo (args[editorIndex + 1], 0, 0);
                    juce::Logger::writeToLog ("Insert editor: " + message);

                    const auto paramIndex = args.indexOf ("--param");

                    if (paramIndex > 0 && paramIndex + 2 < args.size())
                    {
                        const auto index = args[paramIndex + 1].getIntValue();
                        const auto value = args[paramIndex + 2].getDoubleValue();
                        const auto applied = main->setDemoInsertParameter (index, value);
                        juce::Logger::writeToLog ("Insert parameter " + juce::String (index)
                                                    + " = " + juce::String (value)
                                                    + (applied ? " applied" : " FAILED"));
                    }
                }
            }

            const auto shotIndex = args.indexOf ("--screenshot");

            if (shotIndex > 0 && shotIndex + 1 < args.size())
            {
                const auto path = args[shotIndex + 1];

                juce::Timer::callAfterDelay (2500, [this, path]
                {
                    if (mainWindow != nullptr)
                        writeWindowScreenshot (*mainWindow, resolvePath (path));

                    juce::JUCEApplication::quit();
                });
            }
        }
    }

    /** Turns a command line path into a File without ever building one from a relative string. */
    static juce::File resolvePath (const juce::String& rawPath)
    {
        // Windows style quoting can survive into the argument; strip it before use.
        auto path = rawPath.trim().unquoted();

        if (path.isEmpty())
            return {};

        return juce::File::isAbsolutePath (path)
                 ? juce::File (path)
                 : juce::File::getCurrentWorkingDirectory().getChildFile (path);
    }

    static void writeWindowScreenshot (juce::Component& component, const juce::File& target)
    {
        juce::Image image (juce::Image::ARGB, component.getWidth(), component.getHeight(), true);
        {
            juce::Graphics g (image);
            component.paintEntireComponent (g, true);
        }

        target.deleteFile();
        juce::PNGImageFormat png;
        std::unique_ptr<juce::FileOutputStream> stream (target.createOutputStream());
        png.writeImageToStream (image, *stream);
    }

    void shutdown() override
    {
        mainWindow.reset();
    }

    void systemRequestedQuit() override
    {
        if (mainWindow != nullptr)
        {
            if (auto* main = mainWindow->getMainComponent())
            {
                if (! main->confirmCloseProject())
                    return;
            }
        }

        quit();
    }

    void anotherInstanceStarted (const juce::String&) override {}

private:
    std::unique_ptr<NuxMainWindow> mainWindow;
};
}

START_JUCE_APPLICATION (nux::NuxApplication)
