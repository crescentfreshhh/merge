#pragma once

#include "ui/common/Widgets.h"
#include "app/AppContext.h"

namespace merge
{

/** Song arrangement: pattern clips and audio clips on lanes, with a loop region. */
class Playlist : public juce::Component,
                 public juce::DragAndDropTarget,
                 public juce::FileDragAndDropTarget,
                 private juce::ChangeListener,
                 private juce::Timer
{
public:
    explicit Playlist (AppContext& ctx);
    ~Playlist() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    bool isInterestedInDragSource (const SourceDetails&) override { return true; }
    void itemDropped (const SourceDetails&) override;
    bool isInterestedInFileDrag (const juce::StringArray&) override { return true; }
    void filesDropped (const juce::StringArray& files, int x, int y) override;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override { repaint(); }
    void timerCallback() override;

    juce::Rectangle<int> patternList() const;
    juce::Rectangle<int> timeline() const;
    juce::Rectangle<int> ruler() const;
    int laneAt (int y) const;
    float beatToX (double b) const;
    double xToBeat (float x) const;
    double snap (double b) const { return std::round (b / snapBeats) * snapBeats; }
    struct ClipHit { bool audio = false; int index = -1; bool rightEdge = false; };
    ClipHit clipAt (juce::Point<int> p) const;
    void addAudioClip (const juce::File& f, juce::Point<int> where);

    AppContext& ctx;
    static constexpr int laneH = 46, headerW = 120, listW = 130, rulerH = 26;
    double pxPerBeat = 18.0;
    double snapBeats = 4.0;
    double scrollBeats = 0.0;
    int selectedPatternForDraw = 0;

    enum class Drag { None, Move, Resize, Loop, Locate };
    Drag drag = Drag::None;
    ClipHit dragClip;
    double dragStartBeat = 0.0, clipStartAtDrag = 0.0, clipLenAtDrag = 0.0;
    int clipLaneAtDrag = 0;
    double loopAnchor = 0.0;
    juce::TextButton zoomIn { "+" }, zoomOut { "-" }, loopBtn { "Loop" };
    juce::ComboBox snapBox;
    double lastPos = -1.0;
};

} // namespace merge
