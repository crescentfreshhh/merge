#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Theme.h"

namespace merge
{

/** Floating window that shows a hosted plugin's own editor. */
class PluginWindow : public juce::DocumentWindow
{
public:
    static void show (juce::AudioPluginInstance& plugin);
    static void closeAllFor (juce::AudioPluginInstance& plugin);
    static void closeAll();

    void closeButtonPressed() override;

private:
    explicit PluginWindow (juce::AudioPluginInstance& p);
    juce::AudioPluginInstance& plugin;
    static juce::OwnedArray<PluginWindow>& windows();
};

} // namespace merge
