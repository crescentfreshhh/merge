#pragma once

#include "BeatTypes.h"
#include "core/RtShared.h"
#include "core/SpscQueue.h"

namespace merge
{

/** The beatmaker's real-time engine: sequencer + channels + mixer.

    Owns no UI state. The message thread publishes BeatSnapshots and pushes
    BeatEvents; the audio thread calls process(). The same class drives offline
    rendering (export, tests). */
class BeatCore
{
public:
    BeatCore();

    Transport transport;

    /** Call before processing starts (allocates). */
    void prepare (double sampleRate, int maxBlockSize);
    double getSampleRate() const noexcept { return sampleRate; }
    int getMaxBlockSize() const noexcept { return maxBlock; }

    /** Message thread. */
    void publish (std::unique_ptr<BeatSnapshot> snapshot) { shared.publish (std::move (snapshot)); }
    void collectGarbage() { shared.collect(); }

    /** UI thread (single producer). */
    bool pushEvent (const BeatEvent& e) { return uiEvents.push (e); }
    /** MIDI thread (single producer). */
    bool pushMidiEvent (const BeatEvent& e) { return midiEvents.push (e); }

    void play()  { pushEvent ({ BeatEvent::Play }); }
    void stop()  { pushEvent ({ BeatEvent::Stop }); }
    void noteOn (int channel, int note, float velocity) { pushEvent ({ BeatEvent::NoteOn, channel, note, velocity }); }
    void noteOff (int channel, int note) { pushEvent ({ BeatEvent::NoteOff, channel, note, 0.0f }); }

    /** UI thread: recorded notes coming back from the audio thread. */
    bool popRecorded (RecordedNote& r) { return recorded.pop (r); }

    void setClockSource (ClockSource* c) noexcept { clock = c; }

    /** Message thread: pause the audio thread's use of instruments/effects (e.g. to re-prepare them after a
        device change). Waits up to timeoutMs for the audio thread to acknowledge. */
    void suspend (int timeoutMs = 250);
    void resume() { suspended.store (false); }

    /** Audio thread: renders numSamples of stereo audio into out (overwrites). */
    void process (juce::AudioBuffer<float>& out, int numSamples);

private:
    void processChunk (juce::AudioBuffer<float>& out, int offset, int n);
    void handleEvent (const BeatEvent& e, BeatSnapshot& snap);
    void scheduleBlock (BeatSnapshot& snap, int n, double spb);
    void emitRange (BeatSnapshot& snap, double b0, double b1, int base, double spb);
    void emitPattern (BeatSnapshot& snap, int patternIndex, double l0, double l1, double zeroBeat,
                      double segStart, int base, double spb);
    void renderAudioClips (BeatSnapshot& snap, double b0, int base, int count, double spb);
    void addNoteOn (BeatSnapshot& snap, int channel, int note, float velocity, int offset);
    void addNoteOff (int channel, int note, int offset);
    void scheduleNoteOff (BeatSnapshot& snap, int channel, int note, juce::int64 atSample);
    void flushNoteOffs (BeatSnapshot& snap, int n);
    void runNoteRepeat (BeatSnapshot& snap, int n, double spb);
    void renderMetronome (juce::AudioBuffer<float>& out, int offset, int n, double spb);
    double patternLength (const BeatSnapshot& snap, int index) const;
    double swingOffsetBeats() const;

    RtShared<BeatSnapshot> shared;
    std::atomic<bool> suspended { false };
    std::atomic<juce::uint32> suspendAck { 0 };
    SpscQueue<BeatEvent, 1024> uiEvents, midiEvents;
    SpscQueue<RecordedNote, 1024> recorded;
    ClockSource* clock = nullptr;

    double sampleRate = 44100.0;
    int maxBlock = 512;
    double position = 0.0;         // beats (pattern-local in PAT mode, song beats in SONG mode)
    juce::int64 sampleCounter = 0; // monotonic
    double repeatClock = 0.0;      // free-running beats for note repeat when stopped
    bool wasPlaying = false;
    double currentBpm = 120.0;

    std::vector<juce::AudioBuffer<float>> channelBuffers;
    std::vector<juce::MidiBuffer> channelMidi;
    juce::AudioBuffer<float> masterBuffer, clipBuffer;

    struct PendingOff { ChannelStrip* strip; int note; juce::int64 at; };
    std::array<PendingOff, 1024> pendingOffs {};
    int numPendingOffs = 0;

    struct Held { float velocity = 0.0f; int note = 60; };
    std::array<Held, kMaxChannels> held {};

    struct RecStart { double beat = -1.0; float velocity = 0.0f; };
    std::array<std::array<RecStart, 128>, kMaxChannels> recStarts {};

    struct Suppress { double beat = -1.0; juce::int64 until = 0; int pattern = -1; };
    std::array<Suppress, kMaxChannels> suppress {};

    // metronome
    double metroPhase = 0.0, metroFreq = 1000.0;
    float metroEnv = 0.0f;
    double lastMetroBeat = -1.0;
    std::array<std::pair<int, bool>, 32> metroTriggers {};
    int numMetro = 0;
};

} // namespace merge
