#pragma once

#include "Deck.h"

namespace merge
{

class Recorder;

/** One mixer channel strip of the DJ mixer (per deck). */
struct DjChannel
{
    std::atomic<float> trimDb { 0.0f };
    std::atomic<float> autoGainDb { 0.0f };
    std::atomic<float> eqHigh { 0.5f }, eqMid { 0.5f }, eqLow { 0.5f }; // 0 = kill, 0.5 = flat, 1 = +6 dB
    std::atomic<bool> killHigh { false }, killMid { false }, killLow { false };
    std::atomic<float> filter { 0.0f };   // -1 low-pass ... 0 off ... +1 high-pass
    std::atomic<float> fader { 1.0f };
    std::atomic<int> xfAssign { 1 };      // 0 = A, 1 = thru, 2 = B
    std::atomic<bool> cue { false };
    std::atomic<float> fxAmount { 0.5f };
    std::atomic<bool> fxOn { false };
    std::atomic<Effect*> fx { nullptr };  // owned by the UI (ReleasePool keeps it alive)
    std::atomic<float> peakL { 0.0f }, peakR { 0.0f };

    // audio-thread state
    dsp::LinkwitzRiley lp200, hp200, lp2500, hp2500;
    dsp::Biquad ap2500;
    dsp::Svf filterL, filterR;
    float lastFilter = 0.0f;
    float gHigh = 1, gMid = 1, gLow = 1, gOut = 1;
};

/** The DJ side: decks + mixer + master clock. */
class DjCore
{
public:
    static constexpr int kMaxDecks = 4;

    DjCore();

    void prepare (double sampleRate, int maxBlock);

    /** Audio thread: adds decks into master/cue (both stereo, already cleared by the caller). */
    void process (juce::AudioBuffer<float>& master, juce::AudioBuffer<float>& cue, int n);
    /** Audio thread: routes the beatmaker into the headphones when its cue button is on. */
    void addBeatToCue (const juce::AudioBuffer<float>& beat, juce::AudioBuffer<float>& cue, int n);

    Deck& deck (int i) noexcept { return *decks[(size_t) juce::jlimit (0, kMaxDecks - 1, i)]; }
    DjChannel& channel (int i) noexcept { return channels[(size_t) juce::jlimit (0, kMaxDecks - 1, i)]; }

    std::atomic<float> crossfader { 0.0f };     // -1 = A ... +1 = B
    std::atomic<float> crossfaderCurve { 0.0f }; // 0 = smooth, 1 = sharp (scratch)
    std::atomic<float> masterVolume { 0.8f };
    std::atomic<float> cueMix { 0.3f };          // 0 = cue only, 1 = master only
    std::atomic<float> cueVolume { 0.8f };
    std::atomic<bool> beatCue { false };
    std::atomic<int> masterDeckPref { -1 };      // -1 = automatic
    std::atomic<int> numDecks { 2 };
    std::atomic<Recorder*> recorder { nullptr };

    /** Read by the beatmaker (remix deck / follow mode). */
    ClockSource clockOut;
    std::atomic<int> currentMasterDeck { -1 };
    std::atomic<float> masterPeakL { 0.0f }, masterPeakR { 0.0f };

    double getSampleRate() const noexcept { return sampleRate; }

    /** Gain for an EQ knob value (0..1). */
    static float eqGain (float knob, bool kill) noexcept;
    /** Crossfader gains for side A / B. */
    static void crossfaderGains (float x, float curve, float& a, float& b) noexcept;

private:
    void processChannel (int index, juce::AudioBuffer<float>& buf, int n, double bpm);
    MasterClock computeClock();

    std::vector<std::unique_ptr<Deck>> decks;
    std::array<DjChannel, kMaxDecks> channels;
    std::vector<juce::AudioBuffer<float>> deckBuffers;
    juce::AudioBuffer<float> fxBuffer;
    double sampleRate = 44100.0;
    int maxBlock = 512;
    int autoMaster = -1;
};

} // namespace merge
