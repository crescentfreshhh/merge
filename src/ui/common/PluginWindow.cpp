#include "PluginWindow.h"

namespace merge
{

juce::OwnedArray<PluginWindow>& PluginWindow::windows()
{
    static juce::OwnedArray<PluginWindow> w;
    return w;
}

PluginWindow::PluginWindow (juce::AudioPluginInstance& p)
    : juce::DocumentWindow (p.getName(), theme::bg1, juce::DocumentWindow::closeButton), plugin (p)
{
    setUsingNativeTitleBar (true);
    if (auto* editor = p.createEditorIfNeeded())
        setContentOwned (editor, true);
    else
        setContentOwned (new juce::GenericAudioProcessorEditor (p), true);
    setResizable (true, false);
    centreWithSize (juce::jmax (300, getWidth()), juce::jmax (200, getHeight()));
    setVisible (true);
}

void PluginWindow::show (juce::AudioPluginInstance& plugin)
{
    for (auto* w : windows())
        if (&w->plugin == &plugin)
        {
            w->toFront (true);
            return;
        }
    windows().add (new PluginWindow (plugin));
}

void PluginWindow::closeAllFor (juce::AudioPluginInstance& plugin)
{
    for (int i = windows().size(); --i >= 0;)
        if (&windows()[i]->plugin == &plugin)
            windows().remove (i);
}

void PluginWindow::closeAll()
{
    windows().clear();
}

void PluginWindow::closeButtonPressed()
{
    juce::MessageManager::callAsync ([this] { windows().removeObject (this); });
}

} // namespace merge
