#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Instrument.h"
#include "Effect.h"

namespace merge
{

/** Owns the plugin format manager and the list of known (scanned) plugins. */
class PluginHost : public juce::ChangeBroadcaster, private juce::Timer
{
public:
    PluginHost();
    ~PluginHost() override;

    juce::AudioPluginFormatManager& getFormatManager() noexcept { return formatManager; }
    juce::KnownPluginList& getKnownPlugins() noexcept { return knownPlugins; }

    /** Starts an incremental scan of the default VST3 folders (message thread, one file per tick). */
    void startScan();
    bool isScanning() const noexcept { return scanner != nullptr; }
    float getScanProgress() const noexcept { return scanProgress; }
    juce::String getCurrentlyScanning() const { return currentFile; }

    std::unique_ptr<juce::AudioPluginInstance> createInstance (const juce::PluginDescription&, double sampleRate,
                                                               int blockSize, juce::String& error);

    std::vector<juce::PluginDescription> getInstruments() const;
    std::vector<juce::PluginDescription> getEffects() const;

    /** Builds an instrument/effect wrapper from saved state (used when loading projects). */
    Instrument::Ptr createPluginInstrument (const juce::var& state, double sampleRate, int blockSize);
    Effect::Ptr createPluginEffect (const juce::var& state, double sampleRate, int blockSize);

    Instrument::Ptr wrapInstrument (std::unique_ptr<juce::AudioPluginInstance>);
    Effect::Ptr wrapEffect (std::unique_ptr<juce::AudioPluginInstance>);

private:
    void timerCallback() override;
    void saveList();

    juce::AudioPluginFormatManager formatManager;
    juce::KnownPluginList knownPlugins;
    std::unique_ptr<juce::PluginDirectoryScanner> scanner;
    float scanProgress = 0.0f;
    juce::String currentFile;
};

/** Hosted plugin used as a channel instrument. */
class PluginInstrument : public Instrument
{
public:
    PluginInstrument (std::unique_ptr<juce::AudioPluginInstance>, PluginHost&);
    ~PluginInstrument() override;

    juce::String getTypeId() const override { return "plugin"; }
    void prepare (double sampleRate, int maxBlockSize) override;
    void render (juce::AudioBuffer<float>& out, const juce::MidiBuffer& midi, int numSamples) override;
    void allNotesOff() override { pendingAllOff.store (true); }
    Ptr clone() const override;
    juce::var saveState() const override;
    void loadState (const juce::var&) override {}

    juce::AudioPluginInstance* getPlugin() const noexcept { return plugin.get(); }

private:
    std::unique_ptr<juce::AudioPluginInstance> plugin;
    PluginHost& host;
    juce::AudioBuffer<float> temp;
    juce::MidiBuffer midiCopy;
    std::atomic<bool> pendingAllOff { false };
    int maxBlock = 0;
};

/** Hosted plugin used as an insert effect. */
class PluginEffect : public Effect
{
public:
    PluginEffect (std::unique_ptr<juce::AudioPluginInstance>, PluginHost&);
    ~PluginEffect() override;

    juce::String getTypeId() const override { return "plugin"; }
    juce::String getName() const override { return plugin != nullptr ? plugin->getName() : juce::String ("Plugin"); }
    void prepare (double sampleRate, int maxBlockSize) override;
    void process (juce::AudioBuffer<float>& buffer, int numSamples, const EffectContext&) override;
    Ptr clone() const override;
    juce::var saveState() const override;
    void loadState (const juce::var&) override {}

    juce::AudioPluginInstance* getPlugin() const noexcept { return plugin.get(); }

private:
    std::unique_ptr<juce::AudioPluginInstance> plugin;
    PluginHost& host;
    juce::AudioBuffer<float> temp;
    juce::MidiBuffer emptyMidi;
    int maxBlock = 0;
};

juce::var pluginStateToVar (juce::AudioPluginInstance&);

} // namespace merge
