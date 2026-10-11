#pragma once

#include "TrackData.h"
#include "engine/Effect.h"
#include "engine/BeatTypes.h"
#include "core/SpscQueue.h"
#include "core/Dsp.h"
#include <signalsmith-stretch/signalsmith-stretch.h>

namespace merge
{

struct DeckCommand
{
    enum Type : int
    {
        Load, Eject, ReplaceTrack,
        Play, Pause, TogglePlay,
        CueDown, CueUp,
        Seek, SeekBeats,
        SetHotCue, ClearHotCue, HotCue,
        LoopAuto, LoopIn, LoopOut, LoopExit, LoopToggle, LoopHalve, LoopDouble,
        BeatJump,
        ScratchStart, ScratchEnd,
        AlignPhase
    };
    Type type = Play;
    double value = 0.0;
    int index = 0;
    TrackData* track = nullptr; // kept alive by the UI + ReleasePool
};

/** Shared clock: the master deck's tempo and beat position. */
struct MasterClock
{
    double bpm = 0.0;
    double beat = 0.0; // absolute beat position of the master deck (from its first beat)
    int masterDeck = -1;
    bool valid() const noexcept { return masterDeck >= 0 && bpm > 0.0; }
};

/** One DJ deck: playback, keylock, sync, cues, loops, scratch, stems. Audio thread only, except the atomics. */
class Deck
{
public:
    explicit Deck (int index);

    void prepare (double sampleRate, int maxBlock);
    bool post (const DeckCommand& c) { return commands.push (c); }

    /** Renders n samples into out (stereo, overwritten). */
    void process (juce::AudioBuffer<float>& out, int n, const MasterClock& clock);

    // --- controls (UI -> audio)
    std::atomic<float> pitch { 0.0f };        // -1..1 of range
    std::atomic<float> pitchRange { 0.08f };  // 0.08 / 0.16 / 0.5
    std::atomic<bool> keylock { true };
    std::atomic<bool> sync { false };
    std::atomic<bool> quantize { true };
    std::atomic<float> nudge { 0.0f };        // temporary rate offset (jog / pitch bend)
    std::atomic<float> scratchRate { 0.0f };  // rate while scratching (can be negative)
    std::array<std::atomic<float>, NumStems> stemGain;
    std::atomic<float> loopBeatsDefault { 4.0f };

    // --- state (audio -> UI)
    std::atomic<double> positionSec { 0.0 };  // audible position
    std::atomic<bool> playing { false };
    std::atomic<double> effectiveBpm { 0.0 };
    std::atomic<double> tempoRatio { 1.0 };
    std::atomic<bool> loopActive { false };
    std::atomic<double> loopInSec { -1.0 }, loopOutSec { -1.0 };
    std::atomic<double> cueSec { 0.0 };
    std::atomic<bool> trackEnded { false };
    std::atomic<float> peak { 0.0f };

    double audibleBeat() const noexcept;     // audio thread
    bool hasGrid() const noexcept { return track != nullptr && track->grid.isValid(); }
    double trackBpm() const noexcept { return track != nullptr ? track->grid.bpm : 0.0; }
    bool isPlaying() const noexcept { return isPlayingNow; }

private:
    void handle (const DeckCommand& c, const MasterClock& clock);
    void seekTo (double sourceSample, bool keepLoop = false);
    void alignToClock (const MasterClock& clock);
    double quantizeSec (double sec) const;
    double currentAudibleSample() const noexcept;
    inline void readFrame (double pos, float& l, float& r) const noexcept;
    double wrapLoop (double pos) const noexcept;

    const int deckIndex;
    SpscQueue<DeckCommand, 256> commands;
    TrackData::Ptr track;
    double sampleRate = 44100.0;
    int maxBlock = 512;

    double pos = 0.0;          // source samples
    double inputFrac = 0.0;    // fractional input samples owed to the stretcher
    bool isPlayingNow = false;
    bool cuePreview = false;
    bool hotCuePreview = false;
    bool scratching = false;
    bool wasStretching = false;
    double cuePoint = 0.0;     // source samples
    std::array<double, 8> hotCues {};
    bool loopOn = false;
    double loopIn = -1.0, loopOut = -1.0;
    double scratchSmoothed = 0.0;
    double ratioNow = 1.0;

    signalsmith::stretch::SignalsmithStretch<float> stretch;
    juce::AudioBuffer<float> stretchIn, stretchOut, seekBuf;
    bool needsSeek = true;
    std::array<float, NumStems> stemGainNow {};
};

} // namespace merge
