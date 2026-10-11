#pragma once

#include "Widgets.h"
#include "app/AppContext.h"

namespace merge
{

/** Logo, Make/Mix switch, transport, tempo, swing, key, pattern, meters and the main menu. */
class TopBar : public juce::Component, private juce::ChangeListener, private juce::Timer
{
public:
    explicit TopBar (AppContext& ctx);
    ~TopBar() override;
    void paint (juce::Graphics&) override;
    void resized() override;

    std::function<void()> onMenu;
    std::function<void()> onSave;
    juce::TextButton& getMenuButton() noexcept { return menuButton; }
    void refresh();

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override { refresh(); }
    void timerCallback() override;

    AppContext& ctx;
    PillButton makeBtn { "MAKE", theme::make, false }, mixBtn { "MIX", theme::mix, false };
    PillButton playBtn { "Play", theme::good, false }, stopBtn { "Stop", theme::text, false };
    PillButton recBtn { "Rec", theme::rec }, metroBtn { "Click", theme::make };
    ValueBox bpm { "BPM", 40.0, 240.0, 0.1, 1 }, swing { "Swing", 50.0, 75.0, 1.0, 0 };
    juce::ComboBox keyBox, patternBox;
    PillButton songBtn { "SONG", theme::make }, followBtn { "Sync to decks", theme::mix };
    LevelMeter meter;
    juce::Label cpu;
    juce::TextButton saveBtn { "Save" }, menuButton { "Menu" };
};

} // namespace merge
