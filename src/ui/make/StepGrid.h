#pragma once

#include "ui/common/Widgets.h"
#include "app/AppContext.h"

namespace merge
{

/** FL Studio-style channel rack: one row per channel with mute/solo/volume/pan and step cells.
    Instrument channels that use the piano roll show a note preview instead of steps. */
class StepGrid : public juce::Component,
                 public juce::DragAndDropTarget,
                 public juce::FileDragAndDropTarget,
                 private juce::Timer,
                 private juce::ChangeListener
{
public:
    explicit StepGrid (AppContext& ctx);
    ~StepGrid() override;

    static constexpr int rowHeight = 30;
    static constexpr int headerWidth = 250;

    int getIdealHeight() const;
    std::function<void (int channelId)> onOpenPianoRoll;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    bool isInterestedInDragSource (const SourceDetails&) override { return true; }
    void itemDropped (const SourceDetails&) override;
    bool isInterestedInFileDrag (const juce::StringArray&) override { return true; }
    void filesDropped (const juce::StringArray& files, int x, int y) override;

private:
    void timerCallback() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;

    enum class Hit { None, Name, Mute, Solo, Volume, Pan, Step };
    struct HitInfo { Hit what = Hit::None; int row = -1; int step = -1; };
    HitInfo hitTest (juce::Point<int> p) const;
    juce::Rectangle<int> rowBounds (int row) const { return { 0, row * rowHeight, getWidth(), rowHeight }; }
    juce::Rectangle<int> stepArea (int row) const;
    juce::Rectangle<int> stepBounds (int row, int step) const;
    int numSteps() const;
    bool rowUsesNotes (int row) const;
    void showRowMenu (int row);
    void promptRename (int channelId);
    void dropFileOnRow (int row, const juce::File& f);

    AppContext& ctx;
    HitInfo dragHit;
    int paintValue = -1; // velocity being painted by a drag
    float dragStartValue = 0.0f;
    int lastPlayStep = -1;
    std::vector<int> lastPaintedStep;
};

} // namespace merge
