#pragma once

#include "PadGrid.h"
#include "StepGrid.h"
#include "ChannelInspector.h"
#include "Browser.h"

namespace merge
{

class PianoRoll;
class Playlist;
class MixerView;

/** The beatmaker workspace: browser + Beat / Piano Roll / Song / Mixer tabs. */
class MakeView : public juce::Component, private juce::ChangeListener
{
public:
    explicit MakeView (AppContext& ctx);
    ~MakeView() override;
    void resized() override;
    void paint (juce::Graphics&) override;

    void showTab (int index);
    void openPianoRoll (int channelId);
    PadGrid& getPads() noexcept { return pads; }
    std::function<void()> onSendToMix;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void layoutBeat (juce::Rectangle<int> area);

    AppContext& ctx;
    Browser browser;
    PadGrid pads;
    ChannelInspector inspector;
    StepGrid steps;
    juce::Viewport stepViewport;

    // beat-tab toolbar
    juce::ComboBox lengthBox;
    juce::TextButton generate { "Generate" }, clear { "Clear" }, addChannel { "+ Channel" };
    juce::ComboBox repeatBox;

    std::array<std::unique_ptr<PillButton>, 4> tabButtons;
    PillButton sendToMix { "Send to Mix", theme::mix, false };
    int currentTab = 0;
    std::unique_ptr<PianoRoll> pianoRoll;
    std::unique_ptr<Playlist> playlist;
    std::unique_ptr<MixerView> mixer;
};

} // namespace merge
