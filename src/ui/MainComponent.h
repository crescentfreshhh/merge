#pragma once

#include "common/TopBar.h"
#include "make/MakeView.h"
#include "mix/MixView.h"
#include "engine/OfflineRenderer.h"
#include "common/Onboarding.h"
#include "app/UpdateChecker.h"

namespace merge
{

class MainComponent : public juce::Component,
                      public juce::DragAndDropContainer,
                      private juce::ChangeListener,
                      private juce::Timer
{
public:
    MainComponent();
    ~MainComponent() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    bool keyStateChanged (bool isKeyDown) override;

    AppContext& getContext() { return *ctx; }
    bool quitRequested();
    void openFile (const juce::File& f);

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;
    void setMode (bool make);
    void showMainMenu();
    void saveProject (bool saveAs);
    void openProjectDialog();
    void exportAudio (bool song, const juce::String& extension);
    void sendToMix();
    void runExport (RenderOptions options, juce::File file, std::function<void (bool ok)> onDone);
    void toggleSetRecording();
    void openSetInMake (const juce::File& folder, double bpm);
    void showSettings();
    void showShortcuts();

    std::unique_ptr<AppContext> ctx;
    std::unique_ptr<TopBar> topBar;
    std::unique_ptr<MakeView> makeView;
    std::unique_ptr<MixView> mixView;
    juce::Label statusBar;
    std::vector<int> heldPadKeys;
    std::shared_ptr<juce::FileChooser> chooser;
    juce::TooltipWindow tooltips { this, 600 };
    std::unique_ptr<Onboarding> onboarding;
    UpdateChecker updates;
    juce::String latestUpdateUrl;
};

} // namespace merge
