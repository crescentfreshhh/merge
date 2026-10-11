#pragma once

#include "DeckWidgets.h"

namespace merge
{

/** One deck: track info, overview, jog, transport, sync, pitch, performance pads, FX and Flip. */
class DeckPanel : public juce::Component,
                  public juce::DragAndDropTarget,
                  public juce::FileDragAndDropTarget,
                  private juce::ChangeListener,
                  private juce::Timer
{
public:
    DeckPanel (AppContext& ctx, int deck, bool compact = false);
    ~DeckPanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    bool isInterestedInDragSource (const SourceDetails&) override { return true; }
    void itemDropped (const SourceDetails&) override;
    void itemDragEnter (const SourceDetails&) override { dragOver = true; repaint(); }
    void itemDragExit (const SourceDetails&) override { dragOver = false; repaint(); }
    bool isInterestedInFileDrag (const juce::StringArray&) override { return true; }
    void filesDropped (const juce::StringArray& files, int, int) override;

    std::function<void (int deck)> onFlip;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override { refresh(); }
    void timerCallback() override;
    void refresh();
    void setPadMode (int mode);
    void padPressed (int pad, bool down, const juce::ModifierKeys& mods);
    void layoutPads (juce::Rectangle<int> area);

    struct Pad : public juce::Component
    {
        std::function<void (bool, const juce::ModifierKeys&)> onPress;
        juce::String label;
        juce::Colour colour = theme::bg3;
        bool lit = false;
        void paint (juce::Graphics& g) override;
        void mouseDown (const juce::MouseEvent& e) override { if (onPress) onPress (true, e.mods); }
        void mouseUp (const juce::MouseEvent& e) override { if (onPress) onPress (false, e.mods); }
    };

    AppContext& ctx;
    const int deckIndex;
    const bool compact;
    OverviewWave overview;
    JogWheel jog;
    PillButton play { "PLAY", theme::good, false }, cue { "CUE", theme::warn, false };
    PillButton sync { "SYNC", theme::mix }, master { "MASTER", theme::mix }, keylock { "KEY", theme::mix }, quant { "Q", theme::mix };
    juce::TextButton dbl { "x2" }, half { "/2" }, gridHere { "Grid" }, flip { "FLIP" };
    juce::Slider pitch;
    juce::TextButton range { "8%" };
    std::array<PillButton, 4> modeButtons { PillButton ("CUES", theme::mix, false), PillButton ("LOOP", theme::mix, false),
                                            PillButton ("ROLL", theme::mix, false), PillButton ("STEMS", theme::mix, false) };
    std::array<Pad, 8> pads;
    int padMode = 0;
    juce::TextButton jumpBack { "<" }, jumpFwd { ">" }, loopHalf { "/2" }, loopDbl { "x2" }, loopIn { "IN" }, loopOut { "OUT" };
    juce::ComboBox fxBox;
    Knob fxAmount { "", theme::mix };
    PillButton fxOn { "FX", theme::mix };
    Effect::Ptr fx;
    bool dragOver = false;
};

} // namespace merge
