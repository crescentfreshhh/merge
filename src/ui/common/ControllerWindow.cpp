#include "ControllerWindow.h"
#include "core/Paths.h"

namespace merge
{

ControllerPanel::ControllerPanel (AppContext& c) : ctx (c)
{
    auto& h = table.getHeader();
    h.addColumn ("Section", 1, 110);
    h.addColumn ("Action", 2, 200);
    h.addColumn ("MIDI", 3, 170);
    h.addColumn ("", 4, 70);
    h.addColumn ("", 5, 60);
    table.setModel (this);
    table.setRowHeight (24);
    addAndMakeVisible (table);
    help.setText ("Click Learn, then move a knob, fader or press a button on your controller. Pads also respond to notes 36-51 automatically.",
                  juce::dontSendNotification);
    help.setFont (theme::font (12.0f));
    help.setColour (juce::Label::textColourId, theme::textDim);
    addAndMakeVisible (help);
    last.setFont (theme::font (11.5f, true));
    last.setColour (juce::Label::textColourId, theme::mix);
    addAndMakeVisible (last);
    name.setText (ctx.controllers->getMapping().name, false);
    name.onTextChange = [this] { ctx.controllers->getMapping().name = name.getText(); };
    addAndMakeVisible (name);
    saveBtn.onClick = [this]
    {
        ctx.settings().setValue ("midiMapping", ctx.controllers->getMapping().name);
        ctx.controllers->saveMapping();
        ctx.setStatus ("Saved MIDI mapping \"" + ctx.controllers->getMapping().name + "\"");
    };
    loadBtn.onClick = [this]
    {
        chooser = std::make_shared<juce::FileChooser> ("Load a mapping", paths::mappingsDir(), "*.json");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [this] (const juce::FileChooser& fc)
                              {
                                  if (fc.getResult().existsAsFile() && ctx.controllers->loadMapping (fc.getResult()))
                                  {
                                      name.setText (ctx.controllers->getMapping().name, false);
                                      table.updateContent();
                                      table.repaint();
                                  }
                              });
    };
    openFolder.onClick = [] { paths::mappingsDir().startAsProcess(); };
    for (auto* b : { &saveBtn, &loadBtn, &openFolder })
        addAndMakeVisible (*b);
    ctx.controllers->addChangeListener (this);
    setSize (680, 620);
}

ControllerPanel::~ControllerPanel()
{
    ctx.controllers->cancelLearn();
    ctx.controllers->removeChangeListener (this);
}

void ControllerPanel::show (AppContext& ctx)
{
    juce::DialogWindow::LaunchOptions o;
    o.content.setOwned (new ControllerPanel (ctx));
    o.dialogTitle = "MIDI controller mapping";
    o.dialogBackgroundColour = theme::bg1;
    o.useNativeTitleBar = true;
    o.resizable = true;
    o.launchAsync();
}

void ControllerPanel::changeListenerCallback (juce::ChangeBroadcaster*)
{
    last.setText ("Last MIDI: " + ctx.controllers->getLastMessage(), juce::dontSendNotification);
    table.updateContent();
    table.repaint();
}

int ControllerPanel::getNumRows()
{
    return (int) ctx.controllers->getControls().size();
}

void ControllerPanel::paintRowBackground (juce::Graphics& g, int row, int, int, bool selected)
{
    const auto& c = ctx.controllers->getControls()[(size_t) row];
    const bool learning = ctx.controllers->getLearning() == c.id;
    g.fillAll (learning ? theme::mix.withAlpha (0.25f) : (selected ? theme::bg3 : (row % 2 ? theme::bg1 : theme::bg1.brighter (0.03f))));
}

void ControllerPanel::paintCell (juce::Graphics& g, int row, int col, int w, int h, bool)
{
    const auto& c = ctx.controllers->getControls()[(size_t) row];
    auto* b = ctx.controllers->getMapping().bindingFor (c.id);
    const bool learning = ctx.controllers->getLearning() == c.id;
    g.setFont (theme::font (12.5f, col == 2));
    juce::String text;
    g.setColour (theme::text);
    switch (col)
    {
        case 1: text = c.group; g.setColour (theme::textDim); break;
        case 2: text = c.name; break;
        case 3: text = learning ? juce::String ("move a control...") : (b != nullptr ? b->describe() : juce::String ("-")); g.setColour (b != nullptr ? theme::good : theme::textFaint); break;
        case 4: text = learning ? "Cancel" : "Learn"; g.setColour (theme::mix); break;
        case 5: text = b != nullptr ? "Clear" : ""; g.setColour (theme::textDim); break;
        default: break;
    }
    g.drawText (text, 6, 0, w - 8, h, juce::Justification::centredLeft);
}

void ControllerPanel::cellClicked (int row, int col, const juce::MouseEvent&)
{
    const auto& c = ctx.controllers->getControls()[(size_t) row];
    if (col == 4)
    {
        if (ctx.controllers->getLearning() == c.id) ctx.controllers->cancelLearn();
        else ctx.controllers->startLearn (c.id);
    }
    else if (col == 5)
    {
        ctx.controllers->getMapping().unbindControl (c.id);
        ctx.controllers->saveMapping();
    }
    table.repaint();
}

void ControllerPanel::paint (juce::Graphics& g)
{
    g.fillAll (theme::bg1);
}

void ControllerPanel::resized()
{
    auto r = getLocalBounds().reduced (10);
    auto top = r.removeFromTop (28);
    openFolder.setBounds (top.removeFromRight (130));
    top.removeFromRight (6);
    loadBtn.setBounds (top.removeFromRight (120));
    top.removeFromRight (6);
    saveBtn.setBounds (top.removeFromRight (110));
    top.removeFromRight (10);
    name.setBounds (top);
    r.removeFromTop (6);
    help.setBounds (r.removeFromTop (34));
    last.setBounds (r.removeFromBottom (22));
    table.setBounds (r);
}

} // namespace merge
