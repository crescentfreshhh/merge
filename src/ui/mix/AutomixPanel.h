#pragma once

#include "DeckWidgets.h"

namespace merge
{

class Automix;

/** Automix controls and the queue. */
class AutomixPanel : public juce::Component, private juce::ChangeListener, private juce::ListBoxModel, private juce::Timer
{
public:
    explicit AutomixPanel (AppContext& ctx);
    ~AutomixPanel() override;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;
    int getNumRows() override { return (int) ids.size(); }
    void paintListBoxItem (int row, juce::Graphics&, int w, int h, bool selected) override;
    void listBoxItemClicked (int row, const juce::MouseEvent&) override;

    AppContext& ctx;
    PillButton startStop { "START AUTOMIX", theme::mix, false }, skipBtn { "MIX NOW", theme::mix, false };
    juce::ComboBox length;
    juce::Label stateLabel;
    juce::ListBox list;
    std::vector<juce::int64> ids;
};

} // namespace merge
