#include "AutomixPanel.h"
#include "app/Automix.h"
#include "app/LibraryService.h"

namespace merge
{

AutomixPanel::AutomixPanel (AppContext& c) : ctx (c)
{
    startStop.onClick = [this]
    {
        if (ctx.automix->isRunning()) ctx.automix->stop();
        else ctx.automix->start();
    };
    skipBtn.onClick = [this] { ctx.automix->skip(); };
    skipBtn.setTooltip ("Start the next transition now");
    length.addItem ("8 beat mixes", 8);
    length.addItem ("16 beat mixes", 16);
    length.addItem ("32 beat mixes", 32);
    length.addItem ("64 beat mixes", 64);
    length.setSelectedId (16, juce::dontSendNotification);
    length.onChange = [this] { ctx.automix->transitionBeats = length.getSelectedId(); };
    stateLabel.setFont (theme::font (13.0f, true));
    stateLabel.setColour (juce::Label::textColourId, theme::mix);
    list.setModel (this);
    list.setRowHeight (28);
    for (juce::Component* comp : { (juce::Component*) &startStop, (juce::Component*) &skipBtn, (juce::Component*) &length,
                                   (juce::Component*) &stateLabel, (juce::Component*) &list })
        addAndMakeVisible (comp);
    ctx.automix->addChangeListener (this);
    ctx.library->addChangeListener (this);
    startTimerHz (2);
    changeListenerCallback (nullptr);
}

AutomixPanel::~AutomixPanel()
{
    ctx.automix->removeChangeListener (this);
    ctx.library->removeChangeListener (this);
}

void AutomixPanel::timerCallback()
{
    stateLabel.setText (ctx.automix->getState(), juce::dontSendNotification);
}

void AutomixPanel::changeListenerCallback (juce::ChangeBroadcaster*)
{
    ids = ctx.automix->queue();
    list.updateContent();
    list.repaint();
    startStop.setButtonText (ctx.automix->isRunning() ? "STOP AUTOMIX" : "START AUTOMIX");
    startStop.setToggleState (ctx.automix->isRunning(), juce::dontSendNotification);
    stateLabel.setText (ctx.automix->getState(), juce::dontSendNotification);
}

void AutomixPanel::paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool selected)
{
    if (row >= (int) ids.size()) return;
    g.fillAll (selected ? theme::bg3 : (row % 2 ? theme::bg1 : theme::bg1.brighter (0.03f)));
    const auto* t = ctx.library->find (ids[(size_t) row]);
    if (t == nullptr) return;
    g.setColour (theme::textDim);
    g.setFont (theme::font (12.0f, true));
    g.drawText (juce::String (row + 1), 8, 0, 24, h, juce::Justification::centredLeft);
    g.setColour (theme::text);
    g.drawText (t->title + "  -  " + t->artist, 36, 0, w - 200, h, juce::Justification::centredLeft);
    g.setColour (theme::textDim);
    g.drawText (juce::String (t->bpm, 1) + " BPM   " + MusicalKey::fromIndex (t->key).toCamelot(), w - 160, 0, 150, h, juce::Justification::centredRight);
}

void AutomixPanel::listBoxItemClicked (int row, const juce::MouseEvent& e)
{
    if (! e.mods.isPopupMenu() || row >= (int) ids.size()) return;
    const auto id = ids[(size_t) row];
    juce::PopupMenu m;
    m.addItem (1, "Remove from queue");
    showMenu (m, &list, [this, id] (int r)
    {
        if (r == 1)
        {
            ctx.library->db().removeFromCrate (ctx.library->automixCrate(), id);
            ctx.library->refresh();
        }
    });
}

void AutomixPanel::paint (juce::Graphics& g)
{
    g.setColour (theme::bg1);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 6.0f);
    if (ids.empty())
    {
        g.setColour (theme::textFaint);
        g.setFont (theme::font (13.0f));
        g.drawText ("The Automix queue is empty. In the library, right-click tracks > Add to Automix queue.",
                    list.getBounds(), juce::Justification::centred);
    }
}

void AutomixPanel::resized()
{
    auto r = getLocalBounds().reduced (10);
    auto top = r.removeFromTop (30);
    startStop.setBounds (top.removeFromLeft (150));
    top.removeFromLeft (6);
    skipBtn.setBounds (top.removeFromLeft (100));
    top.removeFromLeft (6);
    length.setBounds (top.removeFromLeft (140));
    top.removeFromLeft (12);
    stateLabel.setBounds (top);
    r.removeFromTop (8);
    list.setBounds (r);
}

} // namespace merge
