#pragma once

#include "Instrument.h"
#include "Effect.h"
#include <array>
#include <atomic>
#include <vector>

namespace merge
{

constexpr int kNumPads = 16;
constexpr int kNumInserts = 3;
constexpr int kMaxChannels = 64;
constexpr int kNumPatterns = 16;
constexpr double kStepBeats = 0.25; // a step is a 16th note

/** Per-channel mixer state shared between UI and audio thread. */
class ChannelStrip : public juce::ReferenceCountedObject
{
public:
    using Ptr = juce::ReferenceCountedObjectPtr<ChannelStrip>;

    explicit ChannelStrip (Instrument::Ptr inst) : instrument (std::move (inst)) {}

    Instrument::Ptr instrument;

    std::atomic<float> volume { 0.8f }; // fader position 0..1 (0.8 = unity)
    std::atomic<float> pan { 0.0f };
    std::atomic<bool> mute { false };
    std::atomic<bool> solo { false };
    std::atomic<int> chokeGroup { 0 };

    // audio -> UI feedback
    std::atomic<float> peakL { 0.0f }, peakR { 0.0f };
    std::atomic<juce::uint32> triggerCount { 0 };

    // audio-thread-only smoothing state
    float lastGainL = -1.0f, lastGainR = -1.0f; // < 0: no ramp yet
};

struct Note
{
    double start = 0.0;  // beats from pattern start
    double length = 0.25;
    int pitch = 60;
    float velocity = 0.8f;
};

/** Events compiled from steps and notes, sorted by beat. */
struct CompiledEvent
{
    double beat = 0.0;
    double length = 0.25;
    int channel = 0;
    int note = 60;
    float velocity = 0.8f;
    bool swing = false; // falls on an odd 16th: delayed by the swing amount
};

struct CompiledPattern
{
    double lengthBeats = 4.0;
    std::vector<CompiledEvent> events;
};

struct CompiledClip
{
    int pattern = 0;
    int lane = 0;
    double start = 0.0, length = 4.0, offset = 0.0;
};

struct CompiledAudioClip
{
    SampleBuffer::Ptr sample;
    int lane = 0;
    double start = 0.0, length = 4.0;
    double offsetSeconds = 0.0;
    float gain = 1.0f;
};

/** Everything the audio thread needs to play the project. Immutable once published. */
struct BeatSnapshot
{
    std::vector<ChannelStrip::Ptr> channels;
    std::vector<int> stepNotes; // note each channel's pads/steps trigger
    std::vector<std::array<Effect::Ptr, kNumInserts>> inserts;
    std::array<Effect::Ptr, kNumInserts> masterInserts;
    std::vector<CompiledPattern> patterns;
    std::vector<CompiledClip> clips;
    std::vector<CompiledAudioClip> audioClips;
    std::vector<bool> laneMuted;
    double songLengthBeats = 16.0;
};

/** Transport state written by the UI and read by the audio thread (and vice versa for position). */
struct Transport
{
    std::atomic<bool> playing { false };
    std::atomic<bool> recording { false };
    std::atomic<bool> songMode { false };
    std::atomic<bool> metronome { false };
    std::atomic<bool> loopEnabled { false };
    std::atomic<bool> followClock { false }; // remix-deck mode: tempo & phase follow the DJ master clock
    std::atomic<double> bpm { 120.0 };
    std::atomic<double> swing { 50.0 };      // MPC-style percentage, 50 = straight, 66 = triplet feel
    std::atomic<double> loopStart { 0.0 }, loopEnd { 16.0 };
    std::atomic<int> currentPattern { 0 };
    std::atomic<int> queuedPattern { -1 };   // launched pattern, switches at the next bar
    std::atomic<double> noteRepeat { 0.0 };  // repeat interval in beats, 0 = off
    std::atomic<float> masterVolume { 0.8f };

    // published by the audio thread
    std::atomic<double> positionBeats { 0.0 };
    std::atomic<float> peakL { 0.0f }, peakR { 0.0f };
};

struct BeatEvent
{
    enum Type : int { NoteOn, NoteOff, Play, Stop, Locate, AllNotesOff, Panic };
    Type type = NoteOn;
    int channel = 0;
    int note = 60;
    float velocity = 0.8f;
    double value = 0.0;
};

/** Sent from the audio thread to the UI while recording. */
struct RecordedNote
{
    int channel = 0;
    int pattern = 0;
    int note = 60;
    float velocity = 0.8f;
    double startBeat = 0.0;   // pattern-local, unquantised
    double lengthBeats = 0.25;
};

/** Read-only view of the DJ master clock, used when the beatmaker follows the decks. */
struct ClockSource
{
    std::atomic<bool> valid { false };
    std::atomic<double> bpm { 120.0 };
    std::atomic<double> beatPhase { 0.0 }; // absolute beat position of the master deck
};

} // namespace merge
