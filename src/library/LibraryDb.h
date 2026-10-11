#pragma once

#include <juce_core/juce_core.h>
#include <optional>

struct sqlite3;

namespace merge
{

struct TrackInfo
{
    juce::int64 id = -1;
    juce::File file;
    juce::String title, artist, album, genre, comment, source;
    double duration = 0.0, bpm = 0.0, firstBeat = 0.0;
    int key = -1;          // MusicalKey::toIndex(), -1 = unknown
    float lufs = -100.0f;
    int rating = 0;
    bool analyzed = false;
    juce::int64 added = 0;
    int playCount = 0;
};

struct CuePoint
{
    int slot = 0;             // 0-7 hot cues
    double position = 0.0;    // seconds
    int kind = 0;             // 0 = hot cue, 1 = saved loop
    double length = 0.0;      // loop length in seconds
    juce::String label;
    juce::uint32 colour = 0;
};

struct Crate
{
    juce::int64 id = -1;
    juce::String name;
    int kind = 0; // 0 = user crate, 1 = system (My Beats, Automix...)
};

/** The DJ music library (SQLite). Use from one thread at a time (the message thread). */
class LibraryDb
{
public:
    LibraryDb() = default;
    ~LibraryDb();

    bool open (const juce::File& dbFile);
    void close();
    bool isOpen() const noexcept { return db != nullptr; }

    juce::int64 addTrack (const TrackInfo&); // insert or return existing id (by path)
    bool updateTrack (const TrackInfo&);
    bool setAnalysis (juce::int64 id, double bpm, double firstBeat, int key, float lufs, double duration,
                      const juce::MemoryBlock& waveform);
    bool removeTrack (juce::int64 id);
    std::vector<TrackInfo> allTracks();
    std::optional<TrackInfo> getTrack (juce::int64 id);
    std::optional<TrackInfo> findByPath (const juce::File&);
    std::vector<juce::int64> unanalyzed();
    juce::MemoryBlock getWaveform (juce::int64 id);
    void incrementPlayCount (juce::int64 id);
    int countTracks();

    juce::int64 createCrate (const juce::String& name, int kind = 0);
    juce::int64 findCrate (const juce::String& name);
    bool renameCrate (juce::int64 id, const juce::String& name);
    bool deleteCrate (juce::int64 id);
    std::vector<Crate> crates();
    bool addToCrate (juce::int64 crateId, juce::int64 trackId);
    bool removeFromCrate (juce::int64 crateId, juce::int64 trackId);
    std::vector<juce::int64> crateTracks (juce::int64 crateId);

    bool setCue (juce::int64 trackId, const CuePoint&);
    bool clearCue (juce::int64 trackId, int slot);
    std::vector<CuePoint> getCues (juce::int64 trackId);

    bool addFolder (const juce::File&);
    juce::Array<juce::File> folders();

    juce::String lastError() const { return error; }

private:
    bool exec (const char* sql);
    TrackInfo readTrackRow (void* stmt);

    sqlite3* db = nullptr;
    juce::String error;
};

/** Imports a Rekordbox "Export Collection in xml format" file: tracks, BPM/grid, key, hot cues, playlists. */
struct RekordboxImportResult
{
    int tracks = 0, cues = 0, playlists = 0, missing = 0;
};
RekordboxImportResult importRekordboxXml (LibraryDb& db, const juce::File& xmlFile);

} // namespace merge
