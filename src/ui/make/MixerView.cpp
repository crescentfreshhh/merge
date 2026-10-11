#include "MixerView.h"

namespace merge
{

struct MixerView::Strip : public juce::Component
{
    Strip (AppContext& c, int id) : ctx (c), channelId (id), inserts (c, id, true)
    {
        name.setFont (theme::font (12.0f, true));
        name.setJustificationType (juce::Justification::centred);
        addAndMakeVisible (name);
        addAndMakeVisible (inserts);
        pan.setRange (-1.0, 1.0, 0.0);
        addAndMakeVisible (pan);
        fader.setSliderStyle (juce::Slider::LinearVertical);
        fader.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        fader.setRange (0.0, 1.0);
        fader.setDoubleClickReturnValue (true, 0.8);
        addAndMakeVisible (fader);
        addAndMakeVisible (meter);
        mute.setClickingTogglesState (true);
        solo.setClickingTogglesState (true);
        mute.setColour (juce::TextButton::buttonOnColourId, theme::warn);
        solo.setColour (juce::TextButton::buttonOnColourId, theme::good);
        addAndMakeVisible (mute);
        addAndMakeVisible (solo);

        if (id < 0)
        {
            name.setText ("MASTER", juce::dontSendNotification);
            auto& t = ctx.engine.beat().transport;
            fader.setValue (t.masterVolume.load(), juce::dontSendNotification);
            fader.onValueChange = [this] { ctx.engine.beat().transport.masterVolume.store ((float) fader.getValue()); };
            meter.setSources (&t.peakL, &t.peakR);
            pan.setVisible (false);
            mute.setVisible (false);
            solo.setVisible (false);
        }
        else if (auto* ch = ctx.project.findChannel (id))
        {
            name.setText (ch->name, juce::dontSendNotification);
            name.setColour (juce::Label::textColourId, ch->colour);
            auto strip = ch->strip;
            fader.setColour (juce::Slider::trackColourId, ch->colour);
            pan.setAccent (ch->colour);
            fader.setValue (strip->volume.load(), juce::dontSendNotification);
            pan.setValue (strip->pan.load());
            mute.setToggleState (strip->mute.load(), juce::dontSendNotification);
            solo.setToggleState (strip->solo.load(), juce::dontSendNotification);
            fader.onValueChange = [strip, this] { strip->volume.store ((float) fader.getValue()); };
            pan.onChange = [strip] (double v) { strip->pan.store ((float) v); };
            mute.onClick = [strip, this] { strip->mute.store (mute.getToggleState()); };
            solo.onClick = [strip, this] { strip->solo.store (solo.getToggleState()); };
            meter.setSources (&strip->peakL, &strip->peakR);
        }
    }

    void sync()
    {
        if (channelId < 0) return;
        if (auto* ch = ctx.project.findChannel (channelId))
        {
            if (! fader.isMouseButtonDown()) fader.setValue (ch->strip->volume.load(), juce::dontSendNotification);
            if (! pan.slider.isMouseButtonDown()) pan.setValue (ch->strip->pan.load());
            mute.setToggleState (ch->strip->mute.load(), juce::dontSendNotification);
            solo.setToggleState (ch->strip->solo.load(), juce::dontSendNotification);
            name.setText (ch->name, juce::dontSendNotification);
        }
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (channelId < 0 ? theme::bg2 : theme::bg1);
        g.fillRoundedRectangle (getLocalBounds().toFloat().reduced (2.0f), 5.0f);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (6);
        name.setBounds (r.removeFromTop (20));
        inserts.setBounds (r.removeFromTop (3 * 25));
        r.removeFromTop (4);
        if (pan.isVisible()) pan.setBounds (r.removeFromTop (50).withSizeKeepingCentre (48, 50));
        auto ms = r.removeFromBottom (24);
        if (mute.isVisible())
        {
            mute.setBounds (ms.removeFromLeft (ms.getWidth() / 2).reduced (1, 0));
            solo.setBounds (ms.reduced (1, 0));
        }
        r.removeFromBottom (4);
        meter.setBounds (r.removeFromRight (10));
        fader.setBounds (r);
    }

    AppContext& ctx;
    int channelId;
    juce::Label name;
    InsertSlots inserts;
    Knob pan;
    juce::Slider fader;
    LevelMeter meter;
    juce::TextButton mute { "M" }, solo { "S" };
};

MixerView::MixerView (AppContext& c) : ctx (c)
{
    viewport.setViewedComponent (&content, false);
    viewport.setScrollBarsShown (false, true);
    addAndMakeVisible (viewport);
    master = std::make_unique<Strip> (ctx, -1);
    addAndMakeVisible (*master);
    ctx.addChangeListener (this);
    startTimerHz (10);
    rebuild();
}

MixerView::~MixerView()
{
    ctx.removeChangeListener (this);
}

void MixerView::changeListenerCallback (juce::ChangeBroadcaster*)
{
    std::vector<int> ids;
    for (auto& ch : ctx.project.getChannels()) ids.push_back (ch.id);
    if (ids != shownIds) rebuild();
    for (auto& s : strips) s->inserts.refresh();
    master->inserts.refresh();
}

void MixerView::timerCallback()
{
    if (! isVisible()) return;
    for (auto& s : strips) s->sync();
}

void MixerView::rebuild()
{
    strips.clear();
    shownIds.clear();
    for (auto& ch : ctx.project.getChannels())
    {
        shownIds.push_back (ch.id);
        auto s = std::make_unique<Strip> (ctx, ch.id);
        content.addAndMakeVisible (*s);
        strips.push_back (std::move (s));
    }
    resized();
}

void MixerView::paint (juce::Graphics& g)
{
    g.fillAll (theme::bg0);
}

void MixerView::resized()
{
    auto r = getLocalBounds();
    master->setBounds (r.removeFromRight (120));
    r.removeFromRight (6);
    viewport.setBounds (r);
    const int w = 104;
    const int h = r.getHeight() - (viewport.isHorizontalScrollBarShown() ? viewport.getScrollBarThickness() : 0);
    content.setSize ((int) strips.size() * w, juce::jmax (100, h));
    for (size_t i = 0; i < strips.size(); ++i)
        strips[i]->setBounds ((int) i * w, 0, w, content.getHeight());
}

} // namespace merge
