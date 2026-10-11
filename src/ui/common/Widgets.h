#pragma once

#include "Theme.h"
#include "core/Params.h"
#include "core/SampleBuffer.h"
#include <atomic>
#include <functional>

namespace merge
{

/** Rotary knob with a caption underneath and the value shown while dragging. */
class Knob : public juce::Component
{
public:
    Knob (const juce::String& caption = {}, juce::Colour accent = theme::make);

    juce::Slider slider;
    juce::String caption;
    std::function<juce::String (double)> formatter;
    std::function<void (double)> onChange;
    std::function<void()> onRightClick;

    void setRange (double min, double max, double def, double skewCentre = 0.0);
    void setTooltip (const juce::String& t) { slider.setTooltip (t); }
    void mouseDown (const juce::MouseEvent& e) override { if (e.mods.isPopupMenu() && onRightClick) onRightClick(); }
    void setValue (double v, juce::NotificationType n = juce::dontSendNotification) { slider.setValue (v, n); }
    double getValue() const { return slider.getValue(); }
    void setAccent (juce::Colour c);

    /** Binds to a realtime Param (two-way, polled). */
    void bind (Param* p);
    void refreshFromParam();

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    Param* param = nullptr;
};

/** Numeric value you drag up/down or double-click to type (BPM, swing...). */
class ValueBox : public juce::Component
{
public:
    ValueBox (const juce::String& caption, double min, double max, double step, int decimals);

    void setValue (double v, bool notify = false);
    double getValue() const noexcept { return value; }
    std::function<void (double)> onChange;
    juce::String suffix;
    juce::Colour accent = theme::text;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    juce::String caption;
    double value = 0, minV, maxV, stepV, dragStart = 0;
    int decimals;
    std::unique_ptr<juce::TextEditor> editor;
};

/** Stereo peak meter that reads (and decays) atomics written by the audio thread. */
class LevelMeter : public juce::Component, private juce::Timer
{
public:
    LevelMeter (std::atomic<float>* left = nullptr, std::atomic<float>* right = nullptr, bool vertical = true);
    void setSources (std::atomic<float>* left, std::atomic<float>* right);
    void paint (juce::Graphics&) override;

private:
    void timerCallback() override;
    std::atomic<float>* srcL;
    std::atomic<float>* srcR;
    float levelL = 0, levelR = 0, holdL = 0, holdR = 0;
    bool vertical;
};

/** Toggle-able pill button with an accent colour. */
class PillButton : public juce::TextButton
{
public:
    PillButton (const juce::String& text, juce::Colour onColour = theme::make, bool toggle = true);
};

/** Draws the waveform of a SampleBuffer (cached min/max overview). */
class WaveformThumb : public juce::Component
{
public:
    void setSample (SampleBuffer::Ptr s);
    void setRegion (float start, float end) { regionStart = start; regionEnd = end; repaint(); }
    juce::Colour colour = theme::make;
    void paint (juce::Graphics&) override;
    void resized() override { rebuild(); }

private:
    void rebuild();
    SampleBuffer::Ptr sample;
    std::vector<std::pair<float, float>> peaks;
    float regionStart = 0.0f, regionEnd = 1.0f;
};

/** Auto-generated knob panel for a ParamSet (instrument or effect settings). */
class ParamPanel : public juce::Component, private juce::Timer
{
public:
    ParamPanel (ParamSet& params, juce::Colour accent = theme::make, int knobSize = 64);
    void resized() override;
    int getIdealWidth (int maxPerRow = 8) const;
    int getIdealHeight (int width) const;

private:
    void timerCallback() override;
    std::vector<std::unique_ptr<Knob>> knobs;
    std::vector<std::unique_ptr<juce::ComboBox>> combos;
    std::vector<Param*> comboParams;
    std::vector<std::unique_ptr<PillButton>> toggles;
    std::vector<Param*> toggleParams;
    int knobSize;
};

/** Resolves the file being dragged from Merge's browser ("file:<path>" or a FileTreeComponent). */
juce::File fileFromDrag (const juce::DragAndDropTarget::SourceDetails& details);

/** Small helper: shows a popup menu and calls back with the chosen item id. */
void showMenu (juce::PopupMenu menu, juce::Component* target, std::function<void (int)> callback);

} // namespace merge
