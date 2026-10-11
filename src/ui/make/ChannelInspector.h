#pragma once

#include "ui/common/Widgets.h"
#include "ui/common/InsertSlots.h"
#include "app/AppContext.h"

namespace merge
{

/** Settings for the selected pad/channel: sample, volume, pan, instrument params, choke, inserts. */
class ChannelInspector : public juce::Component, private juce::ChangeListener, private juce::Timer
{
public:
    explicit ChannelInspector (AppContext& ctx);
    ~ChannelInspector() override;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override { rebuild(); }
    void timerCallback() override;
    void rebuild();

    AppContext& ctx;
    int shownChannel = -1;
    juce::Label title;
    juce::Label kindLabel;
    WaveformThumb wave;
    Knob volume { "Volume" }, pan { "Pan" };
    juce::ComboBox choke;
    std::unique_ptr<ParamPanel> params;
    std::unique_ptr<InsertSlots> inserts;
    juce::TextButton openPlugin { "Open plugin" };
    juce::String lastSampleName;
};

} // namespace merge
