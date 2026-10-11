#include "ChannelInspector.h"
#include "engine/Instruments.h"
#include "engine/PluginHost.h"
#include "ui/common/PluginWindow.h"

namespace merge
{

ChannelInspector::ChannelInspector (AppContext& c) : ctx (c)
{
    title.setFont (theme::font (17.0f, true));
    kindLabel.setFont (theme::font (11.5f));
    kindLabel.setColour (juce::Label::textColourId, theme::textDim);
    addAndMakeVisible (title);
    addAndMakeVisible (kindLabel);
    addAndMakeVisible (wave);
    addAndMakeVisible (volume);
    addAndMakeVisible (pan);
    volume.setRange (0.0, 1.0, 0.8);
    volume.formatter = [] (double v) { return juce::Decibels::toString (juce::Decibels::gainToDecibels (dsp::faderToGain ((float) v)), 1); };
    pan.setRange (-1.0, 1.0, 0.0);
    pan.formatter = [] (double v) { return std::abs (v) < 0.01 ? juce::String ("C") : (v < 0 ? "L" : "R") + juce::String ((int) std::lround (std::abs (v) * 100)); };
    volume.onChange = [this] (double v) { if (auto* ch = ctx.project.findChannel (shownChannel)) ch->strip->volume.store ((float) v); };
    pan.onChange = [this] (double v) { if (auto* ch = ctx.project.findChannel (shownChannel)) ch->strip->pan.store ((float) v); };
    choke.addItem ("No choke", 1);
    for (int g = 1; g <= 4; ++g) choke.addItem ("Choke group " + juce::String (g), g + 1);
    choke.onChange = [this] { if (auto* ch = ctx.project.findChannel (shownChannel)) ch->strip->chokeGroup.store (choke.getSelectedId() - 1); };
    addAndMakeVisible (choke);
    addChildComponent (openPlugin);
    openPlugin.onClick = [this]
    {
        if (auto* ch = ctx.project.findChannel (shownChannel))
            if (auto* pi = dynamic_cast<PluginInstrument*> (ch->strip->instrument.get()))
                if (auto* p = pi->getPlugin())
                    PluginWindow::show (*p);
    };
    ctx.selectionChanged.addChangeListener (this);
    ctx.addChangeListener (this);
    startTimerHz (10);
    rebuild();
}

ChannelInspector::~ChannelInspector()
{
    ctx.selectionChanged.removeChangeListener (this);
    ctx.removeChangeListener (this);
}

void ChannelInspector::timerCallback()
{
    if (auto* ch = ctx.project.findChannel (shownChannel))
    {
        if (! volume.slider.isMouseButtonDown()) volume.setValue (ch->strip->volume.load());
        if (! pan.slider.isMouseButtonDown()) pan.setValue (ch->strip->pan.load());
        if (auto* s = dynamic_cast<SamplerInstrument*> (ch->strip->instrument.get()))
            wave.setRegion (s->startPos.get(), s->endPos.get());
    }
}

void ChannelInspector::rebuild()
{
    auto* ch = ctx.project.findChannel (ctx.selectedChannelId);
    if (ch == nullptr)
    {
        // the channel was deleted: drop anything that points into it
        params.reset();
        shownChannel = -1;
        if (! ctx.project.getChannels().empty())
            juce::MessageManager::callAsync ([this, safe = juce::Component::SafePointer<ChannelInspector> (this)]
            {
                if (safe != nullptr && ! ctx.project.getChannels().empty())
                    ctx.selectChannel (ctx.project.getChannels().front().id);
            });
        return;
    }

    const auto sample = ch->strip->instrument != nullptr ? ch->strip->instrument->getSample() : nullptr;
    const auto sampleName = sample != nullptr ? sample->getName() : juce::String();
    const bool sameChannel = shownChannel == ch->id;
    shownChannel = ch->id;

    title.setText (ch->name, juce::dontSendNotification);
    juce::String kind;
    switch (ch->kind)
    {
        case ChannelKind::Sampler: kind = "Pad sampler"; break;
        case ChannelKind::Keys:    kind = "Keys sampler - play with a MIDI keyboard or the piano roll"; break;
        case ChannelKind::Bass808: kind = "808 bass synth"; break;
        case ChannelKind::Synth:   kind = "Poly synth"; break;
        case ChannelKind::Plugin:  kind = "Plugin instrument"; break;
    }
    kindLabel.setText (kind + (sample != nullptr ? "  |  " + sampleName + "  " + juce::String (sample->getLengthSeconds(), 2) + " s" : juce::String()),
                       juce::dontSendNotification);
    wave.colour = ch->colour;
    wave.setSample (sample);
    wave.setVisible (ch->kind == ChannelKind::Sampler || ch->kind == ChannelKind::Keys);
    choke.setSelectedId (ch->strip->chokeGroup.load() + 1, juce::dontSendNotification);
    choke.setVisible (ch->kind == ChannelKind::Sampler);
    openPlugin.setVisible (ch->kind == ChannelKind::Plugin);
    volume.setAccent (ch->colour);
    pan.setAccent (ch->colour);

    if (! sameChannel || sampleName != lastSampleName || params == nullptr)
    {
        params.reset();
        if (ch->strip->instrument != nullptr && ! ch->strip->instrument->params.all().empty())
        {
            params = std::make_unique<ParamPanel> (ch->strip->instrument->params, ch->colour, 52);
            addAndMakeVisible (*params);
        }
        if (inserts == nullptr)
        {
            inserts = std::make_unique<InsertSlots> (ctx, ch->id, true);
            addAndMakeVisible (*inserts);
        }
        else
        {
            inserts->setChannel (ch->id);
        }
    }
    lastSampleName = sampleName;
    resized();
    repaint();
}

void ChannelInspector::paint (juce::Graphics& g)
{
    g.setColour (theme::bg1);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 6.0f);
    g.setColour (theme::textFaint);
    g.setFont (theme::font (10.5f, true));
    if (inserts != nullptr)
        g.drawText ("INSERT FX", inserts->getBounds().withY (inserts->getY() - 15).withHeight (14), juce::Justification::left);
}

void ChannelInspector::resized()
{
    auto r = getLocalBounds().reduced (12, 8);
    auto top = r.removeFromTop (24);
    title.setBounds (top);
    kindLabel.setBounds (r.removeFromTop (16));
    r.removeFromTop (6);

    auto right = r.removeFromRight (juce::jmin (210, r.getWidth() / 3));
    auto knobsRow = right.removeFromTop (70);
    volume.setBounds (knobsRow.removeFromLeft (64));
    pan.setBounds (knobsRow.removeFromLeft (64));
    right.removeFromTop (4);
    choke.setBounds (right.removeFromTop (24));
    right.removeFromTop (20);
    if (inserts != nullptr)
        inserts->setBounds (right.removeFromTop (3 * 25));

    r.removeFromRight (10);
    if (wave.isVisible())
    {
        wave.setBounds (r.removeFromTop (juce::jlimit (40, 90, r.getHeight() / 3)));
        r.removeFromTop (8);
    }
    if (openPlugin.isVisible())
    {
        openPlugin.setBounds (r.removeFromTop (28).withWidth (140));
        r.removeFromTop (6);
    }
    if (params != nullptr)
        params->setBounds (r.withHeight (juce::jmin (r.getHeight(), params->getIdealHeight (r.getWidth()))));
}

} // namespace merge
