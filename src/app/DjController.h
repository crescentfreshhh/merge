#pragma once

#include "dj/DjCore.h"
#include "library/LibraryDb.h"
#include "analysis/Analysis.h"
#include <juce_events/juce_events.h>

namespace merge
{

class AppContext;

/** UI-side model of the decks: what's loaded, hot cues, waveforms, and the actions the UI and controllers call. */
class DjController : public juce::ChangeBroadcaster, private juce::Timer
{
public:
    explicit DjController (AppContext& ctx);
    ~DjController() override;

    struct DeckState
    {
        TrackInfo info;
        TrackData::Ptr track;
        analysis::Waveform waveform;
        std::array<double, 8> hotCues {};
        bool loading = false;
        float stemProgress = -1.0f; // -1 none, 0..1 separating, 2 = ready
        bool stemsHQ = false;
        juce::String status;
    };

    DeckState& state (int deck) { return states[(size_t) juce::jlimit (0, DjCore::kMaxDecks - 1, deck)]; }
    Deck& deck (int d);
    DjChannel& channel (int d);

    void loadTrack (int deck, juce::int64 trackId);
    void loadFile (int deck, const juce::File& file);
    void eject (int deck);
    int firstFreeDeck() const;

    // transport
    void togglePlay (int d);
    void play (int d);
    void pause (int d);
    void cue (int d, bool down);
    void setSync (int d, bool on);
    void setMaster (int d);
    void seekFraction (int d, double fraction);
    void beatJump (int d, double beats);
    void nudge (int d, float amount);

    // cues and loops
    void hotCue (int d, int slot, bool erase = false);
    void loopAuto (int d, double beats);
    void loopIn (int d);
    void loopOut (int d);
    void loopExit (int d);
    void loopHalve (int d);
    void loopDouble (int d);
    void loopRoll (int d, double beats, bool down);

    // scratch / jog
    void scratchStart (int d);
    void scratchMove (int d, double rate);
    void scratchEnd (int d);

    // grid edits
    void doubleBpm (int d);
    void halveBpm (int d);
    void setGridHere (int d);

    // stems
    void setStemGain (int d, int stem, float gain);
    void attachStems (int d, std::array<SampleBuffer::Ptr, NumStems> stems, bool hq);

    void audioConfigChanged (double sampleRate, int blockSize);
    std::function<void (int deck)> onTrackEnded;

private:
    void timerCallback() override;
    void applyLoaded (int deck, TrackInfo info, SampleBuffer::Ptr audio, analysis::Waveform wave, std::optional<analysis::TrackAnalysis> fresh);

    AppContext& ctx;
    std::array<DeckState, DjCore::kMaxDecks> states;
    std::array<int, DjCore::kMaxDecks> loadGeneration {};
    std::array<bool, DjCore::kMaxDecks> wasPlaying {};
    std::array<bool, DjCore::kMaxDecks> rollWasLooping {};
    juce::ThreadPool loader { 2 };
    std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>> (true);
};

} // namespace merge
