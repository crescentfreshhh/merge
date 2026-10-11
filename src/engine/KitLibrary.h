#pragma once

#include "Project.h"
#include <optional>

namespace merge
{

struct KitPad
{
    int pad = 1;
    juce::String name;
    juce::File file;
    int choke = 0;
    int root = 60;
};

struct KitInfo
{
    juce::String name, genre, keyName, source, license;
    double bpm = 0.0, swing = 50.0;
    int order = 1000;
    juce::File dir;
    std::vector<KitPad> pads;
    int patternLength = 16;
    std::map<int, juce::String> steps; // pad number (1-16) -> step string ("x...")
};

struct InstrumentSample
{
    juce::String name;
    juce::File file;
    int root = 60;
};

/** Finds kits (folders with kit.json) in the factory library, bundled packs and the user's folder. */
class KitLibrary
{
public:
    void scan();
    const std::vector<KitInfo>& getKits() const noexcept { return kits; }
    const KitInfo* find (const juce::String& name) const;
    const std::vector<InstrumentSample>& getInstrumentSamples() const noexcept { return instruments; }

    static std::optional<KitInfo> readKit (const juce::File& kitJson, const juce::String& source);
    /** Turns any folder of audio files into a kit (first 16 files, sorted by name). */
    static KitInfo kitFromFolder (const juce::File& dir);

    /** Loads the kit's sounds onto pads 1-16 and optionally its starter pattern. */
    static void applyKit (Project&, const KitInfo&, bool includePattern, int patternIndex);

private:
    std::vector<KitInfo> kits;
    std::vector<InstrumentSample> instruments;
};

/** Genre-aware random pattern generator ("Generate" button, Logic Drummer-style). */
std::map<int, juce::String> generatePattern (const juce::String& genre, juce::Random& rng);

} // namespace merge
