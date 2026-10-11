#include "DjMixerPanel.h"

namespace merge
{

DjMixerPanel::Strip::Strip (AppContext& c, int d) : ctx (c), deckIndex (d)
{
    auto& ch = ctx.dj.channel (d);
    const auto col = theme::deck (d);
    for (auto* k : { &trim, &high, &mid, &low, &filter })
    {
        k->setAccent (col);
        addAndMakeVisible (*k);
    }
    trim.setRange (-12.0, 12.0, 0.0);
    trim.formatter = [] (double v) { return juce::String (v, 1) + " dB"; };
    trim.onChange = [&ch] (double v) { ch.trimDb.store ((float) v); };
    auto eqFormatter = [] (double v)
    {
        if (v <= 0.001) return juce::String ("KILL");
        return juce::Decibels::toString (juce::Decibels::gainToDecibels (DjCore::eqGain ((float) v, false)), 1);
    };
    for (auto* k : { &high, &mid, &low })
    {
        k->setRange (0.0, 1.0, 0.5);
        k->formatter = eqFormatter;
        k->setTooltip ("Double-click to reset, right-click to kill");
    }
    high.onChange = [&ch] (double v) { ch.eqHigh.store ((float) v); };
    mid.onChange = [&ch] (double v) { ch.eqMid.store ((float) v); };
    low.onChange = [&ch] (double v) { ch.eqLow.store ((float) v); };
    high.onRightClick = [this, &ch] { ch.killHigh.store (! ch.killHigh.load()); repaint(); };
    mid.onRightClick = [this, &ch] { ch.killMid.store (! ch.killMid.load()); repaint(); };
    low.onRightClick = [this, &ch] { ch.killLow.store (! ch.killLow.load()); repaint(); };
    filter.setRange (-1.0, 1.0, 0.0);
    filter.formatter = [] (double v) { return std::abs (v) < 0.02 ? juce::String ("OFF") : (v < 0 ? "LPF " : "HPF ") + juce::String ((int) std::lround (std::abs (v) * 100)) + "%"; };
    filter.onChange = [&ch] (double v) { ch.filter.store ((float) v); };
    cue.onClick = [this, &ch] { ch.cue.store (cue.getToggleState()); };
    cue.setTooltip ("Send to headphones");
    addAndMakeVisible (cue);
    fader.setSliderStyle (juce::Slider::LinearVertical);
    fader.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    fader.setRange (0.0, 1.0);
    fader.setValue (1.0, juce::dontSendNotification);
    fader.setColour (juce::Slider::trackColourId, col);
    fader.onValueChange = [this, &ch] { ch.fader.store ((float) fader.getValue()); };
    addAndMakeVisible (fader);
    meter.setSources (&ch.peakL, &ch.peakR);
    addAndMakeVisible (meter);
    assign.addItem ("A", 1);
    assign.addItem ("-", 2);
    assign.addItem ("B", 3);
    assign.setSelectedId (d % 2 == 0 ? 1 : 3, juce::dontSendNotification);
    ch.xfAssign.store (d % 2 == 0 ? 0 : 2);
    assign.onChange = [this, &ch] { ch.xfAssign.store (assign.getSelectedId() - 1); };
    assign.setTooltip ("Crossfader side");
    addAndMakeVisible (assign);
}

void DjMixerPanel::Strip::paint (juce::Graphics& g)
{
    g.setColour (theme::deck (deckIndex).withAlpha (0.8f));
    g.setFont (theme::font (13.0f, true));
    g.drawText (juce::String::charToString ((juce::juce_wchar) ('A' + deckIndex)), getLocalBounds().removeFromTop (16), juce::Justification::centred);
    auto& ch = ctx.dj.channel (deckIndex);
    // kill indicators
    auto drawKill = [&g] (juce::Rectangle<int> r, bool on) { if (on) { g.setColour (theme::rec); g.fillEllipse (r.removeFromTop (6).withSizeKeepingCentre (6, 6).toFloat()); } };
    drawKill (high.getBounds().translated (0, -6), ch.killHigh.load());
    drawKill (mid.getBounds().translated (0, -6), ch.killMid.load());
    drawKill (low.getBounds().translated (0, -6), ch.killLow.load());
}

void DjMixerPanel::Strip::resized()
{
    auto r = getLocalBounds();
    r.removeFromTop (16);
    // two columns: EQ on the left, trim / filter / cue on the right
    const int kh = juce::jlimit (38, 52, (r.getHeight() - 120) / 3);
    auto knobs = r.removeFromTop (kh * 3);
    auto left = knobs.removeFromLeft (knobs.getWidth() / 2);
    auto right = knobs;
    for (auto* k : { &high, &mid, &low })
        k->setBounds (left.removeFromTop (kh).withSizeKeepingCentre (juce::jmin (left.getWidth(), kh + 8), kh));
    trim.setBounds (right.removeFromTop (kh).withSizeKeepingCentre (juce::jmin (right.getWidth(), kh + 8), kh));
    filter.setBounds (right.removeFromTop (kh).withSizeKeepingCentre (juce::jmin (right.getWidth(), kh + 8), kh));
    cue.setBounds (right.removeFromTop (kh).withSizeKeepingCentre (juce::jmin (right.getWidth() - 6, 56), 24));
    r.removeFromTop (4);
    assign.setBounds (r.removeFromBottom (22).reduced (4, 0));
    r.removeFromBottom (4);
    meter.setBounds (r.removeFromRight (8));
    fader.setBounds (r);
}

DjMixerPanel::DjMixerPanel (AppContext& c) : ctx (c), masterMeter (&c.dj.masterPeakL, &c.dj.masterPeakR, false)
{
    for (int d = 0; d < DjCore::kMaxDecks; ++d)
    {
        auto s = std::make_unique<Strip> (ctx, d);
        addChildComponent (*s);
        strips.push_back (std::move (s));
    }
    crossfader.setSliderStyle (juce::Slider::LinearHorizontal);
    crossfader.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    crossfader.setRange (-1.0, 1.0);
    crossfader.setValue (0.0, juce::dontSendNotification);
    crossfader.setDoubleClickReturnValue (true, 0.0);
    crossfader.setColour (juce::Slider::trackColourId, theme::mix);
    crossfader.onValueChange = [this] { ctx.dj.crossfader.store ((float) crossfader.getValue()); };
    addAndMakeVisible (crossfader);
    curve.setTooltip ("Sharp crossfader curve for scratching");
    curve.onClick = [this] { ctx.dj.crossfaderCurve.store (curve.getToggleState() ? 1.0f : 0.0f); };
    addAndMakeVisible (curve);
    masterVol.setRange (0.0, 1.0, 0.8);
    masterVol.onChange = [this] (double v) { ctx.dj.masterVolume.store ((float) v); };
    cueMix.setRange (0.0, 1.0, 0.3);
    cueMix.onChange = [this] (double v) { ctx.dj.cueMix.store ((float) v); };
    cueMix.setTooltip ("Headphones: cue <-> master");
    cueVol.setRange (0.0, 1.0, 0.8);
    cueVol.onChange = [this] (double v) { ctx.dj.cueVolume.store ((float) v); };
    for (auto* k : { &masterVol, &cueMix, &cueVol })
        addAndMakeVisible (*k);

    beatFader.setSliderStyle (juce::Slider::LinearHorizontal);
    beatFader.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    beatFader.setRange (0.0, 1.0);
    beatFader.setValue (1.0, juce::dontSendNotification);
    beatFader.setColour (juce::Slider::trackColourId, theme::make);
    beatFader.setTooltip ("Remix deck: your beat's level in the mix");
    beatFader.onValueChange = [this] { ctx.engine.beatLevel.store ((float) (beatFader.getValue() * beatFader.getValue())); };
    addAndMakeVisible (beatFader);
    beatCue.onClick = [this] { ctx.dj.beatCue.store (beatCue.getToggleState()); };
    beatSync.setTooltip ("Lock the beat to the master deck's tempo and phase");
    beatSync.onClick = [this] { ctx.engine.beat().transport.followClock.store (beatSync.getToggleState()); };
    beatPlay.onClick = [this] { ctx.togglePlay(); };
    recBtn.onClick = [this] { if (onRecord) onRecord(); };
    recBtn.setTooltip ("Record your set (master + each deck) to Documents/Merge/Recordings");
    recTime.setFont (theme::font (11.0f, true));
    recTime.setColour (juce::Label::textColourId, theme::rec);
    recTime.setJustificationType (juce::Justification::centred);
    for (juce::Component* b : { (juce::Component*) &beatCue, (juce::Component*) &beatSync, (juce::Component*) &beatPlay,
                                (juce::Component*) &recBtn, (juce::Component*) &masterMeter, (juce::Component*) &recTime })
        addAndMakeVisible (b);
    setNumDecks (2);
    startTimerHz (10);
}

void DjMixerPanel::setNumDecks (int n)
{
    numDecks = n;
    for (int d = 0; d < DjCore::kMaxDecks; ++d)
        strips[(size_t) d]->setVisible (d < n);
    resized();
}

void DjMixerPanel::timerCallback()
{
    beatPlay.setToggleState (ctx.isPlaying(), juce::dontSendNotification);
    beatSync.setToggleState (ctx.engine.beat().transport.followClock.load(), juce::dontSendNotification);
    recBtn.setToggleState (ctx.recorder.isRecording(), juce::dontSendNotification);
    if (ctx.recorder.isRecording())
    {
        const int secs = (int) ctx.recorder.getSecondsRecorded();
        recTime.setText (juce::String (secs / 60) + ":" + juce::String (secs % 60).paddedLeft ('0', 2), juce::dontSendNotification);
    }
    else
    {
        recTime.setText ({}, juce::dontSendNotification);
    }
}

void DjMixerPanel::paint (juce::Graphics& g)
{
    g.setColour (theme::bg1);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 6.0f);
    g.setColour (theme::textFaint);
    g.setFont (theme::font (10.0f, true));
    g.drawText ("BEAT (REMIX DECK)", beatFader.getBounds().translated (0, -12).withHeight (11), juce::Justification::centredLeft);
}

void DjMixerPanel::resized()
{
    auto r = getLocalBounds().reduced (8);
    auto bottom = r.removeFromBottom (160);
    auto knobs = bottom.removeFromTop (58);
    const int kw = knobs.getWidth() / 3;
    masterVol.setBounds (knobs.removeFromLeft (kw));
    cueMix.setBounds (knobs.removeFromLeft (kw));
    cueVol.setBounds (knobs);
    bottom.removeFromTop (18);
    auto beatRow = bottom.removeFromTop (24);
    beatPlay.setBounds (beatRow.removeFromRight (50).reduced (1, 0));
    beatSync.setBounds (beatRow.removeFromRight (50).reduced (1, 0));
    beatCue.setBounds (beatRow.removeFromRight (44).reduced (1, 0));
    beatFader.setBounds (beatRow);
    bottom.removeFromTop (6);
    auto xf = bottom.removeFromTop (26);
    curve.setBounds (xf.removeFromRight (70));
    crossfader.setBounds (xf);
    bottom.removeFromTop (4);
    auto recRow = bottom.removeFromTop (28);
    recBtn.setBounds (recRow.removeFromLeft (84));
    recTime.setBounds (recRow.removeFromLeft (50));
    masterMeter.setBounds (recRow.reduced (2, 8));

    r.removeFromBottom (6);
    const int w = r.getWidth() / juce::jmax (1, numDecks);
    for (int d = 0; d < numDecks; ++d)
        strips[(size_t) d]->setBounds (r.removeFromLeft (w).reduced (2, 0));
}

} // namespace merge
