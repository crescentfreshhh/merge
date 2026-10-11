#include "TopBar.h"

namespace merge
{

TopBar::TopBar (AppContext& c) : ctx (c), meter (&c.engine.masterPeakL, &c.engine.masterPeakR, false)
{
    for (auto* b : { &makeBtn, &mixBtn })
        addAndMakeVisible (*b);
    makeBtn.onClick = [this] { if (ctx.onModeChange) ctx.onModeChange (true); };
    mixBtn.onClick = [this] { if (ctx.onModeChange) ctx.onModeChange (false); };

    playBtn.onClick = [this] { ctx.togglePlay(); refresh(); };
    playBtn.setTooltip ("Play / pause the beat (Space)");
    stopBtn.onClick = [this] { ctx.stop(); refresh(); };
    recBtn.setTooltip ("Record pads and keys into the pattern while playing");
    recBtn.onClick = [this] { ctx.engine.beat().transport.recording.store (recBtn.getToggleState()); };
    metroBtn.setTooltip ("Metronome");
    metroBtn.onClick = [this] { ctx.engine.beat().transport.metronome.store (metroBtn.getToggleState()); };
    for (auto* b : { &playBtn, &stopBtn, &recBtn, &metroBtn })
        addAndMakeVisible (*b);

    bpm.accent = theme::make;
    bpm.onChange = [this] (double v) { ctx.setBpm (v); };
    swing.suffix = "%";
    swing.onChange = [this] (double v) { ctx.setSwing (v); };
    addAndMakeVisible (bpm);
    addAndMakeVisible (swing);

    keyBox.addItem ("No key", 1);
    for (int i = 0; i < 24; ++i)
    {
        const auto k = MusicalKey::fromIndex ((i % 2 == 0) ? 12 + i / 2 : i / 2);
        keyBox.addItem (k.toString() + "  (" + k.toCamelot() + ")", 2 + k.toIndex());
    }
    keyBox.setTooltip ("Project key: highlights the scale in the piano roll and matches samples");
    keyBox.onChange = [this]
    {
        ctx.project.key = MusicalKey::fromIndex (keyBox.getSelectedId() - 2);
        ctx.projectEdited (false);
    };
    addAndMakeVisible (keyBox);

    for (int i = 0; i < kNumPatterns; ++i)
        patternBox.addItem ("Pattern " + Project::patternName (i), i + 1);
    patternBox.onChange = [this] { ctx.selectPattern (patternBox.getSelectedId() - 1); };
    patternBox.setTooltip ("Current pattern (in Mix mode it launches on the next bar)");
    addAndMakeVisible (patternBox);

    songBtn.setTooltip ("Play the song arrangement instead of the current pattern");
    songBtn.onClick = [this] { ctx.setSongMode (songBtn.getToggleState()); };
    addAndMakeVisible (songBtn);
    followBtn.setTooltip ("Lock the beat's tempo and phase to the DJ master deck");
    followBtn.onClick = [this] { ctx.engine.beat().transport.followClock.store (followBtn.getToggleState()); };
    addAndMakeVisible (followBtn);

    addAndMakeVisible (meter);
    cpu.setFont (theme::font (11.0f));
    cpu.setColour (juce::Label::textColourId, theme::textDim);
    cpu.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (cpu);
    saveBtn.onClick = [this] { if (onSave) onSave(); };
    saveBtn.setTooltip ("Save project (Ctrl+S)");
    addAndMakeVisible (saveBtn);
    menuButton.onClick = [this] { if (onMenu) onMenu(); };
    addAndMakeVisible (menuButton);

    ctx.addChangeListener (this);
    ctx.selectionChanged.addChangeListener (this);
    startTimerHz (8);
    refresh();
}

TopBar::~TopBar()
{
    ctx.removeChangeListener (this);
    ctx.selectionChanged.removeChangeListener (this);
}

void TopBar::refresh()
{
    const auto& t = ctx.engine.beat().transport;
    makeBtn.setToggleState (ctx.makeMode, juce::dontSendNotification);
    mixBtn.setToggleState (! ctx.makeMode, juce::dontSendNotification);
    playBtn.setToggleState (t.playing.load(), juce::dontSendNotification);
    playBtn.setButtonText (t.playing.load() ? "Pause" : "Play");
    recBtn.setToggleState (t.recording.load(), juce::dontSendNotification);
    metroBtn.setToggleState (t.metronome.load(), juce::dontSendNotification);
    songBtn.setToggleState (t.songMode.load(), juce::dontSendNotification);
    followBtn.setToggleState (t.followClock.load(), juce::dontSendNotification);
    followBtn.setVisible (! ctx.makeMode);
    bpm.setValue (t.followClock.load() ? t.bpm.load() : ctx.project.bpm);
    swing.setValue (ctx.project.swing);
    keyBox.setSelectedId (ctx.project.key.isValid() ? 2 + ctx.project.key.toIndex() : 1, juce::dontSendNotification);
    const int shownPattern = t.queuedPattern.load() >= 0 ? t.queuedPattern.load() : t.currentPattern.load();
    patternBox.setSelectedId (shownPattern + 1, juce::dontSendNotification);
    repaint();
}

void TopBar::timerCallback()
{
    cpu.setText ("CPU " + juce::String ((int) std::round (ctx.engine.getCpuLoad() * 100.0)) + "%", juce::dontSendNotification);
    const auto& t = ctx.engine.beat().transport;
    if (playBtn.getToggleState() != t.playing.load() || (t.followClock.load() && std::abs (bpm.getValue() - t.bpm.load()) > 0.05)
        || patternBox.getSelectedId() != t.currentPattern.load() + 1)
        refresh();
}

void TopBar::paint (juce::Graphics& g)
{
    g.fillAll (theme::bg1);
    g.setColour (theme::line);
    g.drawHorizontalLine (getHeight() - 1, 0.0f, (float) getWidth());
    auto logo = juce::Rectangle<float> (14.0f, 0.0f, 96.0f, (float) getHeight() - 10.0f);
    juce::ColourGradient grad (theme::make, logo.getX(), 0.0f, theme::mix, logo.getRight(), 0.0f, false);
    g.setGradientFill (grad);
    g.setFont (theme::font (24.0f, true));
    g.drawText ("MERGE", logo, juce::Justification::centredLeft);
    g.setColour (theme::textFaint);
    g.setFont (theme::font (10.5f));
    const auto name = ctx.project.name + (ctx.project.kitName.isNotEmpty() ? "  /  " + ctx.project.kitName : juce::String());
    g.drawFittedText (name, juce::Rectangle<int> (14, getHeight() - 17, 96, 12), juce::Justification::centredLeft, 1, 0.7f);
}

void TopBar::resized()
{
    auto r = getLocalBounds().reduced (8, 8);
    r.removeFromLeft (108);
    makeBtn.setBounds (r.removeFromLeft (64));
    r.removeFromLeft (4);
    mixBtn.setBounds (r.removeFromLeft (64));
    r.removeFromLeft (18);
    playBtn.setBounds (r.removeFromLeft (62));
    r.removeFromLeft (4);
    stopBtn.setBounds (r.removeFromLeft (52));
    r.removeFromLeft (4);
    recBtn.setBounds (r.removeFromLeft (46));
    r.removeFromLeft (4);
    metroBtn.setBounds (r.removeFromLeft (52));
    r.removeFromLeft (14);
    bpm.setBounds (r.removeFromLeft (76));
    r.removeFromLeft (6);
    swing.setBounds (r.removeFromLeft (64));
    r.removeFromLeft (10);

    menuButton.setBounds (r.removeFromRight (64));
    r.removeFromRight (6);
    saveBtn.setBounds (r.removeFromRight (56));
    r.removeFromRight (10);
    cpu.setBounds (r.removeFromRight (64));
    meter.setBounds (r.removeFromRight (110).reduced (0, 6));
    r.removeFromRight (10);

    auto mid = r.reduced (0, 2);
    patternBox.setBounds (mid.removeFromLeft (110));
    mid.removeFromLeft (6);
    songBtn.setBounds (mid.removeFromLeft (60));
    mid.removeFromLeft (6);
    keyBox.setBounds (mid.removeFromLeft (juce::jmin (150, mid.getWidth())));
    mid.removeFromLeft (6);
    if (mid.getWidth() > 60)
        followBtn.setBounds (mid.removeFromLeft (juce::jmin (120, mid.getWidth())));
}

} // namespace merge
