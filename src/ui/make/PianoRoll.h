#pragma once

#include "ui/common/Widgets.h"
#include "app/AppContext.h"

namespace merge
{

/** Piano roll for the selected channel in the current pattern. */
class PianoRoll : public juce::Component, private juce::ChangeListener, private juce::Timer, private juce::ScrollBar::Listener
{
public:
    explicit PianoRoll (AppContext& ctx);
    ~PianoRoll() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;
    void scrollBarMoved (juce::ScrollBar*, double) override { repaint(); }

    juce::Rectangle<int> gridArea() const;
    juce::Rectangle<int> velocityArea() const;
    juce::Rectangle<int> keyboardArea() const;
    float beatToX (double beat) const;
    double xToBeat (float x) const;
    int yToPitch (float y) const;
    float pitchToY (int pitch) const;
    double snapBeats() const;
    double snap (double beat) const;
    std::vector<Note> currentNotes() const;
    void commit (std::vector<Note> notes);
    int noteAt (juce::Point<float> p, bool& onRightEdge) const;
    double patternBeats() const;
    void audition (int pitch);
    std::vector<int> chordFor (int root) const;

    AppContext& ctx;
    juce::Label title;
    juce::ComboBox snapBox, chordBox;
    juce::ToggleButton scaleToggle { "Scale" };
    juce::TextButton quantizeBtn { "Quantize" }, clearBtn { "Clear" };
    juce::Slider zoom;
    juce::ScrollBar vscroll { true };

    static constexpr int rowH = 14, keyW = 56, rulerH = 22, velH = 70;
    double pxPerBeat = 80.0;
    double lastLength = 0.25;

    enum class Drag { None, Create, Move, Resize, Velocity };
    Drag drag = Drag::None;
    std::vector<Note> dragNotes;
    int dragIndex = -1;
    double dragBeatOffset = 0.0;
    int dragPitchOffset = 0;
    std::vector<int> selected;
    int auditionNote = -1;
    int playStep = -1;
};

} // namespace merge
