#pragma once

#include "ui/common/Widgets.h"
#include "app/AppContext.h"

namespace merge
{

/** MPC-style 4x4 pads (pad 1 bottom-left). Click, keyboard (1-4/Q-R/A-F/Z-V) or MIDI notes 36-51. */
class PadGrid : public juce::Component,
                public juce::FileDragAndDropTarget,
                public juce::DragAndDropTarget,
                private juce::Timer,
                private juce::ChangeListener
{
public:
    explicit PadGrid (AppContext& ctx, bool compact = false);
    ~PadGrid() override;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

    bool isInterestedInFileDrag (const juce::StringArray&) override { return true; }
    void filesDropped (const juce::StringArray& files, int x, int y) override;
    bool isInterestedInDragSource (const SourceDetails&) override { return true; }
    void itemDropped (const SourceDetails&) override;
    void itemDragEnter (const SourceDetails&) override { dragActive = true; repaint(); }
    void itemDragExit (const SourceDetails&) override { dragActive = false; repaint(); }

    /** Key label for a pad index, e.g. "Q". */
    static juce::String keyForPad (int pad);
    /** Pad index for a key char, -1 if none. */
    static int padForKey (juce::juce_wchar key);

    void loadFileOntoPad (int pad, const juce::File& file);

private:
    void timerCallback() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override { repaint(); }
    int padAt (juce::Point<int> p) const;
    juce::Rectangle<int> padBounds (int pad) const;
    void showPadMenu (int pad);

    AppContext& ctx;
    bool compact;
    std::array<float, kNumPads> flash {};
    std::array<juce::uint32, kNumPads> lastTrigger {};
    int pressedPad = -1;
    bool dragActive = false;
};

} // namespace merge
