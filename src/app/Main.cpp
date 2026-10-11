#include <juce_gui_extra/juce_gui_extra.h>
#include "ui/MainComponent.h"
#include "ui/common/MergeLookAndFeel.h"
#include "core/ReleasePool.h"
#include "core/SampleBuffer.h"

namespace merge
{

class MainWindow : public juce::DocumentWindow
{
public:
    explicit MainWindow (const juce::String& name)
        : juce::DocumentWindow (name, theme::bg0, juce::DocumentWindow::allButtons)
    {
        setUsingNativeTitleBar (true);
        content = new MainComponent();
        setContentOwned (content, true);
        setResizable (true, true);
        setResizeLimits (1100, 700, 10000, 10000);
       #if JUCE_IOS || JUCE_ANDROID
        setFullScreen (true);
       #else
        centreWithSize (getWidth(), getHeight());
       #endif
        setVisible (true);
    }

    void closeButtonPressed() override
    {
        juce::JUCEApplication::getInstance()->systemRequestedQuit();
    }

    MainComponent* content = nullptr;
};

class MergeApplication : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return "Merge"; }
    const juce::String getApplicationVersion() override { return MERGE_VERSION_STRING; }
    bool moreThanOneInstanceAllowed() override { return false; }

    void initialise (const juce::String& commandLine) override
    {
        juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);
        window = std::make_unique<MainWindow> ("Merge");
        openFromCommandLine (commandLine);
    }

    void shutdown() override
    {
        window.reset();
        merge::SampleCache::get().clear();
        merge::ReleasePool::get().releaseAll();
        juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
    }

    void systemRequestedQuit() override
    {
        if (window == nullptr || window->content == nullptr || window->content->quitRequested())
            quit();
    }

    void anotherInstanceStarted (const juce::String& commandLine) override
    {
        openFromCommandLine (commandLine);
    }

private:
    void openFromCommandLine (const juce::String& commandLine)
    {
        auto arg = commandLine.unquoted().trim();
        if (arg.isNotEmpty() && juce::File::isAbsolutePath (arg) && window != nullptr && window->content != nullptr)
            window->content->openFile (juce::File (arg));
    }

    MergeLookAndFeel lookAndFeel;
    std::unique_ptr<MainWindow> window;
};

} // namespace merge

START_JUCE_APPLICATION (merge::MergeApplication)
