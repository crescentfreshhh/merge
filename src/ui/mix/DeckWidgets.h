#pragma once

#include "ui/common/Widgets.h"
#include "app/AppContext.h"
#include "app/DjController.h"

namespace merge
{

/** Draws a colour (low/mid/high) waveform for a time range. */
void drawWaveform (juce::Graphics& g, const analysis::Waveform& w, juce::Rectangle<float> area, double startSec,
                   double endSec, float alpha = 1.0f);

/** Zoomed, scrolling waveform with the playhead in the middle, beat grid, cues and loop. Drag to scratch. */
class WaveStrip : public juce::Component, private juce::Timer
{
public:
    WaveStrip (AppContext& ctx, int deck);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    double secondsVisible = 8.0;

private:
    void timerCallback() override { repaint(); }
    AppContext& ctx;
    int deckIndex;
    double lastDragX = 0.0, lastDragTime = 0.0;
};

/** Whole-track overview: click to jump. */
class OverviewWave : public juce::Component, private juce::Timer
{
public:
    OverviewWave (AppContext& ctx, int deck);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent& e) override { mouseDown (e); }

private:
    void timerCallback() override { repaint(); }
    AppContext& ctx;
    int deckIndex;
    juce::Image cache;
    juce::int64 cachedTrack = -2;
    int cachedWidth = 0;
};

/** Platter: drag around it to scratch, shows rotation. */
class JogWheel : public juce::Component, private juce::Timer
{
public:
    JogWheel (AppContext& ctx, int deck);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    void timerCallback() override;
    float angleAt (juce::Point<float> p) const;
    AppContext& ctx;
    int deckIndex;
    float lastAngle = 0.0f;
    double lastTime = 0.0;
    bool scratching = false;
};

} // namespace merge
