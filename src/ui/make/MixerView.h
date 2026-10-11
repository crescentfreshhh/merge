#pragma once

#include "ui/common/Widgets.h"
#include "ui/common/InsertSlots.h"
#include "app/AppContext.h"

namespace merge
{

/** Beatmaker mixer: a strip per channel plus master, with inserts, pan, fader, meters, mute/solo. */
class MixerView : public juce::Component, private juce::ChangeListener, private juce::Timer
{
public:
    explicit MixerView (AppContext& ctx);
    ~MixerView() override;
    void resized() override;
    void paint (juce::Graphics&) override;

private:
    struct Strip;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;
    void rebuild();

    AppContext& ctx;
    juce::Viewport viewport;
    juce::Component content;
    std::vector<std::unique_ptr<Strip>> strips;
    std::unique_ptr<Strip> master;
    std::vector<int> shownIds;
};

} // namespace merge
