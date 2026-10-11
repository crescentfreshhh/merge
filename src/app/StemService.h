#pragma once

#include "dj/TrackData.h"
#include <juce_events/juce_events.h>

namespace merge
{

class AppContext;

/** Splits tracks into stems in the background, caches them on disk, attaches them to decks,
    and implements Flip (chop a deck's stems onto the beatmaker pads). */
class StemService : private juce::Timer
{
public:
    explicit StemService (AppContext& ctx);
    ~StemService() override;

    void separateDeck (int deck);
    void separateTrack (juce::int64 trackId);
    /** Called after a track loads: attaches cached stems if we have them. */
    void attachCached (int deck);
    void flip (int deck);
    void flipWith (int deck, int source); // source: -1 = full mix, 0..3 = stem

    static juce::File cacheFolderFor (const juce::File& track);

private:
    void timerCallback() override;
    void startJob (int deck, TrackData::Ptr track, juce::File file);

    AppContext& ctx;
    juce::ThreadPool pool { 1 };
    struct Progress { std::atomic<float> value { -1.0f }; std::atomic<int> generation { 0 }; };
    std::array<Progress, 4> progress;
    std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>> (true);
};

} // namespace merge
