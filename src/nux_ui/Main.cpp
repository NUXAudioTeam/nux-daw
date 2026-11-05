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

    void initialise (const juce::String&) override
    {
        mainWindow = std::make_unique<NuxMainWindow> (constants::appName);
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
