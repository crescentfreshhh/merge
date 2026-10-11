#pragma once

#include "BeatTypes.h"
#include <juce_graphics/juce_graphics.h>
#include <map>

namespace merge
{

class PluginHost;

enum class ChannelKind { Sampler, Keys, Bass808, Synth, Plugin };

juce::String channelKindName (ChannelKind);

struct ChannelModel
{
    int id = 0;
    juce::String name;
    ChannelKind kind = ChannelKind::Sampler;
    juce::Colour colour;
    ChannelStrip::Ptr strip;
    std::array<Effect::Ptr, kNumInserts> inserts;
    juce::String sampleRef; // portable path of the loaded sample (sampler / keys)
    int stepNote = 60;      // note the step sequencer triggers

    bool isPad() const noexcept { return kind == ChannelKind::Sampler; }
    bool prefersPianoRoll() const noexcept { return kind != ChannelKind::Sampler; }
};

struct PatternData
{
    juce::String name;
    int lengthSteps = 16;
    std::map<int, std::vector<juce::uint8>> steps; // channel id -> velocity per step (0 = off)
    std::map<int, std::vector<Note>> notes;          // channel id -> piano roll notes

    bool isEmpty() const;
    double lengthBeats() const noexcept { return lengthSteps * kStepBeats; }
};

struct PatternClip
{
    int pattern = 0;
    int lane = 0;
    double start = 0.0, length = 4.0, offset = 0.0;
};

struct AudioClipModel
{
    juce::String sampleRef;
    SampleBuffer::Ptr sample;
    juce::String name;
    int lane = 0;
    double start = 0.0, length = 4.0;
    double offsetSeconds = 0.0;
    float gain = 1.0f;
};

struct Lane
{
    juce::String name;
    bool muted = false;
};

/** Musical key: root 0..11 (C..B), minor flag. root < 0 = no key set. */
struct MusicalKey
{
    int root = -1;
    bool minor = true;

    bool isValid() const noexcept { return root >= 0; }
    juce::String toString() const;
    juce::String toCamelot() const;
    static MusicalKey fromString (const juce::String&);
    static MusicalKey fromIndex (int index24); // 0-11 major, 12-23 minor
    int toIndex() const noexcept { return root < 0 ? -1 : root + (minor ? 12 : 0); }
    bool isInScale (int midiNote) const noexcept;
};

/** The beatmaker document. Lives on the message thread; publishes snapshots to the BeatCore. */
class Project : public juce::ChangeBroadcaster
{
public:
    Project();
    ~Project() override;

    // --- audio config (instruments & effects are prepared with it)
    void setAudioConfig (double sampleRate, int blockSize);
    double getSampleRate() const noexcept { return sampleRate; }
    int getBlockSize() const noexcept { return blockSize; }
    void setPluginHost (PluginHost* h) noexcept { pluginHost = h; }
    PluginHost* getPluginHost() const noexcept { return pluginHost; }

    // --- channels
    std::vector<ChannelModel>& getChannels() noexcept { return channels; }
    const std::vector<ChannelModel>& getChannels() const noexcept { return channels; }
    ChannelModel* findChannel (int id);
    int indexOfChannel (int id) const;
    ChannelModel& addChannel (ChannelKind kind, const juce::String& name, Instrument::Ptr customInstrument = nullptr);
    void removeChannel (int id);
    void setChannelSample (int id, SampleBuffer::Ptr sample, const juce::String& ref, int rootNote = 60);
    void setInsert (int channelId, int slot, Effect::Ptr fx);     // channelId < 0 => master
    Effect::Ptr getInsert (int channelId, int slot) const;
    std::array<Effect::Ptr, kNumInserts>& getMasterInserts() noexcept { return masterInserts; }

    // --- patterns
    std::vector<PatternData>& getPatterns() noexcept { return patterns; }
    const std::vector<PatternData>& getPatterns() const noexcept { return patterns; }
    PatternData& pattern (int index) { return patterns[(size_t) juce::jlimit (0, kNumPatterns - 1, index)]; }
    int getStep (int patternIndex, int channelId, int step) const;
    void setStep (int patternIndex, int channelId, int step, int velocity);
    void setPatternLength (int patternIndex, int steps);
    void clearPattern (int patternIndex);
    void setNotes (int patternIndex, int channelId, std::vector<Note> notes);
    const std::vector<Note>* getNotes (int patternIndex, int channelId) const;
    void setStepsFromString (int patternIndex, int channelId, const juce::String& pattern);
    static juce::String patternName (int index);

    /** Applies a note captured while recording (pads -> nearest step, instruments -> quantised note). */
    void applyRecorded (const RecordedNote& r);

    // --- song
    std::vector<PatternClip>& getClips() noexcept { return clips; }
    std::vector<AudioClipModel>& getAudioClips() noexcept { return audioClips; }
    std::vector<Lane>& getLanes() noexcept { return lanes; }
    double getSongLengthBeats() const;

    // --- global settings
    double bpm = 120.0;
    double swing = 50.0;
    MusicalKey key;
    juce::String name { "Untitled" };
    juce::String kitName;
    juce::String genre;
    juce::File file;

    /** Called after any edit: rebuilds and publishes the snapshot (via callback) and notifies listeners. */
    void changed (bool structural = true);
    std::function<void (std::unique_ptr<BeatSnapshot>)> onPublish;

    std::unique_ptr<BeatSnapshot> buildSnapshot() const;
    CompiledPattern compilePattern (const PatternData&) const;

    // --- persistence
    juce::var toVar() const;
    bool fromVar (const juce::var&);
    bool save (const juce::File&);
    bool load (const juce::File&);
    void resetToDefault();

    /** Deep copy with independent instruments/effects, for offline rendering. */
    std::unique_ptr<Project> cloneForRender() const;

    // --- undo (patterns + song)
    void checkpoint();
    bool undo();
    bool redo();
    bool canUndo() const noexcept { return ! undoStack.empty(); }

    static juce::Colour colourForIndex (int index);

private:
    Instrument::Ptr makeInstrument (ChannelKind) const;
    void prepareObject (Instrument&) const;
    void prepareObject (Effect&) const;
    juce::var editableStateToVar() const;
    void editableStateFromVar (const juce::var&);
    juce::var channelToVar (const ChannelModel&) const;
    bool channelFromVar (const juce::var&);
    Effect::Ptr effectFromVar (const juce::var&) const;
    juce::var effectToVar (const Effect::Ptr&) const;

    std::vector<ChannelModel> channels;
    std::vector<PatternData> patterns;
    std::vector<PatternClip> clips;
    std::vector<AudioClipModel> audioClips;
    std::vector<Lane> lanes;
    std::array<Effect::Ptr, kNumInserts> masterInserts;
    int nextChannelId = 1;
    double sampleRate = 44100.0;
    int blockSize = 512;
    PluginHost* pluginHost = nullptr;

    std::vector<juce::var> undoStack, redoStack;
};

} // namespace merge
