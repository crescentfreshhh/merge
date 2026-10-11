#pragma once

#include "DeckPanel.h"
#include "DjMixerPanel.h"
#include "LibraryPanel.h"
#include "ui/make/PadGrid.h"

namespace merge
{

class AutomixPanel;

/** The DJ workspace: waveforms, decks, mixer, library and the remix deck. */
class MixView : public juce::Component, private juce::Timer
{
public:
    explicit MixView (AppContext& ctx);
    ~MixView() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void setNumDecks (int n);
    int getNumDecks() const noexcept { return numDecks; }

    std::function<void()> onRecordSet;

private:
    void timerCallback() override;
    void showBottom (int which);

    AppContext& ctx;
    int numDecks = 2;
    std::vector<std::unique_ptr<WaveStrip>> strips;
    std::vector<std::unique_ptr<DeckPanel>> decks;
    DjMixerPanel mixer;
    LibraryPanel library;
    PadGrid remixPads;
    std::array<std::unique_ptr<juce::TextButton>, kNumPatterns> launchButtons;
    std::unique_ptr<AutomixPanel> automix;
    PillButton twoDecks { "2 DECKS", theme::mix, false }, fourDecks { "4 DECKS", theme::mix, false };
    PillButton libTab { "LIBRARY", theme::mix, false }, remixTab { "REMIX DECK", theme::make, false }, automixTab { "AUTOMIX", theme::mix, false };
    juce::ComboBox bpmRange;
    juce::ToggleButton autoGain { "Auto gain" };
    int bottom = 0;
};

} // namespace merge
