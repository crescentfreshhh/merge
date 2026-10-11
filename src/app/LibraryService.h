#pragma once

#include "library/LibraryDb.h"
#include "library/TrackLoader.h"
#include <juce_events/juce_events.h>
#include <set>

namespace merge
{

class AppContext;

/** The DJ library on the message thread: database, folder scanning and background analysis. */
class LibraryService : public juce::ChangeBroadcaster
{
public:
    explicit LibraryService (AppContext& ctx);
    ~LibraryService() override;

    LibraryDb& db() noexcept { return database; }
    const std::vector<TrackInfo>& tracks() const noexcept { return cache; }
    const TrackInfo* find (juce::int64 id) const;
    void refresh();

    void addFolder (const juce::File& folder);
    void addFiles (const juce::StringArray& files);
    void rescanFolders();
    void reanalyse (juce::int64 id);
    void analyseMissing();
    int pendingAnalyses() const noexcept { return pending; }
    bool isScanning() const noexcept { return scanning.load(); }

    /** "Send to Mix": adds a rendered beat with a known tempo/key, in the My Beats crate. */
    juce::int64 addBeat (const juce::File& file, double bpm, int key, const juce::String& title);
    juce::int64 myBeatsCrate();
    juce::int64 automixCrate();

    RekordboxImportResult importRekordbox (const juce::File& xml);

    analysis::Waveform waveformFor (juce::int64 id);
    void setBpmRange (double minBpm, double maxBpm);
    double getMinBpm() const noexcept { return minBpm; }
    double getMaxBpm() const noexcept { return maxBpm; }

    /** Fires on the message thread when a track's analysis lands. */
    std::function<void (juce::int64)> onAnalysed;

    static bool isAudioFile (const juce::File& f);

private:
    void queueAnalysis (juce::int64 id, const juce::File& file, bool keepGrid = false);

    AppContext& ctx;
    LibraryDb database;
    std::vector<TrackInfo> cache;
    std::map<juce::int64, analysis::Waveform> waveforms;
    juce::ThreadPool pool { 2 };
    std::set<juce::int64> inFlight;
    int pending = 0;
    std::atomic<bool> scanning { false };
    std::unique_ptr<juce::Thread> scanThread;
    double minBpm = 70.0, maxBpm = 180.0;
    std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>> (true);
};

} // namespace merge
