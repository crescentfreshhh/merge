#pragma once

#include "Widgets.h"

namespace merge
{

/** First-run tour: four short cards over the app. */
class Onboarding : public juce::Component
{
public:
    Onboarding()
    {
        next.onClick = [this]
        {
            if (++step >= (int) cards.size()) finish();
            else refresh();
        };
        skip.onClick = [this] { finish(); };
        addAndMakeVisible (next);
        addAndMakeVisible (skip);
        refresh();
    }

    std::function<void()> onFinished;

    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colours::black.withAlpha (0.6f));
        auto card = cardBounds().toFloat();
        g.setColour (theme::bg1);
        g.fillRoundedRectangle (card, 10.0f);
        g.setColour (step < 2 ? theme::make : theme::mix);
        g.fillRoundedRectangle (card.removeFromTop (6.0f), 3.0f);
        auto r = cardBounds().reduced (28, 24);
        r.removeFromTop (8);
        g.setColour (theme::textDim);
        g.setFont (theme::font (12.0f, true));
        g.drawText ("STEP " + juce::String (step + 1) + " OF " + juce::String ((int) cards.size()), r.removeFromTop (18), juce::Justification::left);
        g.setColour (theme::text);
        g.setFont (theme::font (24.0f, true));
        g.drawText (cards[(size_t) step].first, r.removeFromTop (40), juce::Justification::left);
        g.setColour (theme::text.withAlpha (0.85f));
        g.setFont (theme::font (15.0f));
        g.drawFittedText (cards[(size_t) step].second, r.removeFromTop (150), juce::Justification::topLeft, 8);
    }

    void resized() override
    {
        auto r = cardBounds().reduced (28, 20).removeFromBottom (36);
        next.setBounds (r.removeFromRight (120));
        r.removeFromRight (10);
        skip.setBounds (r.removeFromRight (100));
    }

    void mouseDown (const juce::MouseEvent&) override {}

private:
    juce::Rectangle<int> cardBounds() const { return getLocalBounds().withSizeKeepingCentre (560, 330); }

    void refresh()
    {
        next.setButtonText (step + 1 == (int) cards.size() ? "Let's go" : "Next");
        repaint();
    }

    void finish()
    {
        setVisible (false);
        if (onFinished) onFinished();
    }

    int step = 0;
    PillButton next { "Next", theme::make, false }, skip { "Skip tour", theme::text, false };
    const std::vector<std::pair<juce::String, juce::String>> cards {
        { "Make a beat in seconds", "A kit and its groove are already loaded. Press Space to play.\n\n"
                                    "Play the pads with your keyboard: Z X C V, A S D F, Q W E R, 1 2 3 4 (or a MIDI controller). "
                                    "Click steps in the grid, hit Generate for a fresh pattern, and swap kits on the left." },
        { "Go deeper", "Add an 808, synth, keys or a VST3 instrument with + Channel and draw melodies in the Piano Roll "
                       "(it can draw whole chords in your key). Arrange patterns in Song, add effects in Mixer, "
                       "then Export or Send to Mix." },
        { "Mix like a DJ", "Press Tab (or MIX). Add your music folder, drag tracks onto the decks, hit SYNC. "
                           "Every track is analysed for BPM, beat grid, key (Camelot) and loudness. "
                           "Use hot cues, loops, rolls, FX and the isolator EQ." },
        { "Merge them", "STEMS splits a track into drums, bass, vocals and melody. FLIP chops a loop (or one stem) onto your pads "
                        "at the track's tempo. In the mixer, the BEAT channel plays your patterns live as a remix deck, synced "
                        "to the master deck. Record your set and reopen it as stems." },
    };
};

} // namespace merge
