#pragma once

#include "DeckWidgets.h"

namespace merge
{

/** DJ mixer: per-deck trim, 3-band EQ with kills, filter, cue, fader, meters; crossfader; master, headphones; remix-deck channel. */
class DjMixerPanel : public juce::Component, private juce::Timer
{
public:
    explicit DjMixerPanel (AppContext& ctx);
    void paint (juce::Graphics&) override;
    void resized() override;
    void setNumDecks (int n);
    std::function<void()> onRecord;

private:
    void timerCallback() override;

    struct Strip : public juce::Component
    {
        Strip (AppContext& c, int d);
        void resized() override;
        void paint (juce::Graphics&) override;
        AppContext& ctx;
        int deckIndex;
        Knob trim { "TRIM" }, high { "HI" }, mid { "MID" }, low { "LOW" }, filter { "FILTER" };
        PillButton cue { "CUE", theme::warn };
        juce::Slider fader;
        LevelMeter meter;
        juce::ComboBox assign;
    };

    AppContext& ctx;
    std::vector<std::unique_ptr<Strip>> strips;
    int numDecks = 2;
    juce::Slider crossfader;
    PillButton curve { "SCRATCH", theme::mix };
    Knob masterVol { "MASTER", theme::mix }, cueMix { "CUE MIX", theme::mix }, cueVol { "PHONES", theme::mix };
    // remix deck (the beatmaker)
    juce::Slider beatFader;
    PillButton beatCue { "CUE", theme::warn }, beatSync { "SYNC", theme::make }, beatPlay { "PLAY", theme::make, false };
    PillButton recBtn { "REC SET", theme::rec, false };
    LevelMeter masterMeter;
    juce::Label recTime;
};

} // namespace merge
