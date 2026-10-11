#include "LibraryService.h"
#include "AppContext.h"
#include "core/Paths.h"

namespace merge
{

bool LibraryService::isAudioFile (const juce::File& f)
{
    return f.hasFileExtension ("wav;aif;aiff;flac;ogg;mp3;m4a;aac;wma");
}

LibraryService::LibraryService (AppContext& c) : ctx (c)
{
    database.open (paths::libraryDatabase());
    minBpm = ctx.settings().getDoubleValue ("minBpm", 70.0);
    maxBpm = ctx.settings().getDoubleValue ("maxBpm", 180.0);
    myBeatsCrate();
    automixCrate();
    refresh();
    analyseMissing();
    if (! database.folders().isEmpty())
        rescanFolders(); // pick up new files in watched folders
}

LibraryService::~LibraryService()
{
    alive->store (false);
    if (scanThread != nullptr) scanThread->stopThread (3000);
    pool.removeAllJobs (true, 10000);
}

void LibraryService::refresh()
{
    cache = database.allTracks();
    sendChangeMessage();
}

const TrackInfo* LibraryService::find (juce::int64 id) const
{
    for (auto& t : cache)
        if (t.id == id) return &t;
    return nullptr;
}

juce::int64 LibraryService::myBeatsCrate() { return database.createCrate ("My Beats", 1); }
juce::int64 LibraryService::automixCrate() { return database.createCrate ("Automix Queue", 1); }

void LibraryService::setBpmRange (double lo, double hi)
{
    minBpm = lo;
    maxBpm = hi;
    ctx.settings().setValue ("minBpm", lo);
    ctx.settings().setValue ("maxBpm", hi);
}

void LibraryService::addFiles (const juce::StringArray& files)
{
    int added = 0;
    for (auto& path : files)
    {
        juce::File f (path);
        if (f.isDirectory()) { addFolder (f); continue; }
        if (! isAudioFile (f)) continue;
        auto info = readTrackMetadata (f);
        info.source = "Import";
        const auto id = database.addTrack (info);
        if (id >= 0)
        {
            ++added;
            if (auto t = database.getTrack (id); t && ! t->analyzed)
                queueAnalysis (id, f);
        }
    }
    if (added > 0)
    {
        refresh();
        ctx.setStatus ("Added " + juce::String (added) + " track(s) to the library - analysing BPM and key...");
    }
}

void LibraryService::addFolder (const juce::File& folder)
{
    database.addFolder (folder);
    rescanFolders();
}

void LibraryService::rescanFolders()
{
    if (scanning.load()) return;
    scanning.store (true);
    const auto folders = database.folders();
    auto aliveFlag = alive;

    struct ScanThread : public juce::Thread
    {
        ScanThread (juce::Array<juce::File> f, std::function<void (std::vector<TrackInfo>)> done)
            : juce::Thread ("Merge library scan"), folders (std::move (f)), onDone (std::move (done)) {}
        void run() override
        {
            std::vector<TrackInfo> found;
            for (auto& folder : folders)
                for (const auto& entry : juce::RangedDirectoryIterator (folder, true, "*", juce::File::findFiles))
                {
                    if (threadShouldExit()) return;
                    const auto f = entry.getFile();
                    if (LibraryService::isAudioFile (f))
                        found.push_back (readTrackMetadata (f));
                }
            onDone (std::move (found));
        }
        juce::Array<juce::File> folders;
        std::function<void (std::vector<TrackInfo>)> onDone;
    };

    if (scanThread != nullptr) scanThread->stopThread (2000);
    scanThread = std::make_unique<ScanThread> (folders, [this, aliveFlag] (std::vector<TrackInfo> found)
    {
        juce::MessageManager::callAsync ([this, aliveFlag, found = std::move (found)]
        {
            if (! aliveFlag->load()) return;
            int added = 0;
            for (auto t : found)
            {
                if (database.findByPath (t.file)) continue;
                t.source = "Folder";
                if (database.addTrack (t) >= 0) ++added;
            }
            scanning.store (false);
            refresh();
            analyseMissing();
            ctx.setStatus ("Library scan finished: " + juce::String (added) + " new track(s), " + juce::String ((int) cache.size()) + " in total.");
        });
    });
    scanThread->startThread (juce::Thread::Priority::low);
}

void LibraryService::analyseMissing()
{
    for (auto id : database.unanalyzed())
        if (auto t = database.getTrack (id))
            queueAnalysis (id, t->file, t->bpm > 0 && t->source == "Rekordbox");
}

void LibraryService::reanalyse (juce::int64 id)
{
    if (auto t = database.getTrack (id))
        queueAnalysis (id, t->file);
}

void LibraryService::queueAnalysis (juce::int64 id, const juce::File& file, bool keepGrid)
{
    if (inFlight.count (id) > 0 || ! file.existsAsFile()) return;
    inFlight.insert (id);
    ++pending;
    sendChangeMessage();
    auto aliveFlag = alive;
    const double lo = minBpm, hi = maxBpm;
    pool.addJob ([this, aliveFlag, id, file, keepGrid, lo, hi]
    {
        auto result = analyseFile (file, lo, hi);
        juce::MessageManager::callAsync ([this, aliveFlag, id, keepGrid, result = std::move (result)]
        {
            if (! aliveFlag->load()) return;
            inFlight.erase (id);
            --pending;
            if (result)
            {
                double bpm = result->tempo.bpm, first = result->tempo.firstBeatSec;
                int key = result->key.key.toIndex();
                if (auto existing = database.getTrack (id))
                {
                    if (keepGrid && existing->bpm > 0) { bpm = existing->bpm; first = existing->firstBeat; }
                    if (existing->source == "Merge" && existing->bpm > 0) { bpm = existing->bpm; first = existing->firstBeat; }
                    if (keepGrid && existing->key >= 0) key = existing->key;
                }
                database.setAnalysis (id, bpm, first, key, result->lufs, result->durationSec, result->waveform.toBlob());
                waveforms[id] = result->waveform;
            }
            else
            {
                database.setAnalysis (id, 0.0, 0.0, -1, -100.0f, 0.0, {});
            }
            refresh();
            if (onAnalysed) onAnalysed (id);
        });
    });
}

juce::int64 LibraryService::addBeat (const juce::File& file, double bpm, int key, const juce::String& title)
{
    TrackInfo t = readTrackMetadata (file);
    t.title = title;
    t.artist = "Me (Merge)";
    t.bpm = bpm;
    t.firstBeat = 0.0;
    t.key = key;
    t.source = "Merge";
    t.genre = "My Beats";
    auto id = database.addTrack (t);
    if (id >= 0)
    {
        t.id = id;
        database.updateTrack (t);
        database.addToCrate (myBeatsCrate(), id);
        queueAnalysis (id, file); // waveform + loudness (keeps the known grid)
    }
    refresh();
    return id;
}

RekordboxImportResult LibraryService::importRekordbox (const juce::File& xml)
{
    auto r = importRekordboxXml (database, xml);
    refresh();
    analyseMissing();
    return r;
}

analysis::Waveform LibraryService::waveformFor (juce::int64 id)
{
    if (auto it = waveforms.find (id); it != waveforms.end())
        return it->second;
    auto w = analysis::Waveform::fromBlob (database.getWaveform (id));
    if (! w.points.empty())
        waveforms[id] = w;
    return w;
}

} // namespace merge
