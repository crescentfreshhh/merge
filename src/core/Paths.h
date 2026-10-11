#pragma once

#include <juce_core/juce_core.h>

namespace merge::paths
{

/** Documents/Merge - the user's visible workspace (projects, beats, recordings, samples). */
juce::File userDir();
/** Per-user application data (settings, library database, caches). */
juce::File appDataDir();
juce::File cacheDir();

juce::File factorySamplesDir();
juce::File bundledPacksDir();
juce::File userSamplesDir();
juce::File projectsDir();
juce::File beatsDir();
juce::File recordingsDir();
juce::File mappingsDir();
juce::File autosaveDir();
juce::File libraryDatabase();
juce::File modelsDir();

/** All folders the sample browser shows. */
juce::Array<juce::File> sampleRoots();

/** Portable reference for files inside known roots ("factory:Kits/Trap/01 Kick.wav"). */
juce::String toRef (const juce::File& file);
juce::File fromRef (const juce::String& ref);

/** For tests / portable installs. */
void setOverrideRoot (const juce::File& root);

} // namespace merge::paths
