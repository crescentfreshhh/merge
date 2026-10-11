#include "LibraryDb.h"
#include "engine/Project.h"
#include <sqlite3.h>

namespace merge
{

namespace
{
    struct Stmt
    {
        Stmt (sqlite3* db, const char* sql) { sqlite3_prepare_v2 (db, sql, -1, &s, nullptr); }
        ~Stmt() { sqlite3_finalize (s); }
        bool ok() const { return s != nullptr; }
        void bind (int i, juce::int64 v) { sqlite3_bind_int64 (s, i, v); }
        void bind (int i, int v) { sqlite3_bind_int (s, i, v); }
        void bind (int i, double v) { sqlite3_bind_double (s, i, v); }
        void bind (int i, const juce::String& v) { sqlite3_bind_text (s, i, v.toRawUTF8(), -1, SQLITE_TRANSIENT); }
        void bindBlob (int i, const juce::MemoryBlock& b) { sqlite3_bind_blob (s, i, b.getData(), (int) b.getSize(), SQLITE_TRANSIENT); }
        bool step() { return sqlite3_step (s) == SQLITE_ROW; }
        bool run() { const int r = sqlite3_step (s); return r == SQLITE_DONE || r == SQLITE_ROW; }
        juce::int64 i64 (int c) { return sqlite3_column_int64 (s, c); }
        int i32 (int c) { return sqlite3_column_int (s, c); }
        double dbl (int c) { return sqlite3_column_double (s, c); }
        juce::String str (int c)
        {
            auto* t = reinterpret_cast<const char*> (sqlite3_column_text (s, c));
            return t != nullptr ? juce::String::fromUTF8 (t) : juce::String();
        }
        juce::MemoryBlock blob (int c)
        {
            const void* d = sqlite3_column_blob (s, c);
            const int n = sqlite3_column_bytes (s, c);
            return d != nullptr && n > 0 ? juce::MemoryBlock (d, (size_t) n) : juce::MemoryBlock();
        }
        sqlite3_stmt* s = nullptr;
    };

    const char* trackColumns = "id, path, title, artist, album, genre, comment, source, duration, bpm, first_beat, key, lufs, rating, analyzed, added, play_count";
}

LibraryDb::~LibraryDb() { close(); }

bool LibraryDb::exec (const char* sql)
{
    char* err = nullptr;
    if (sqlite3_exec (db, sql, nullptr, nullptr, &err) != SQLITE_OK)
    {
        error = err != nullptr ? juce::String (err) : juce::String ("sqlite error");
        sqlite3_free (err);
        return false;
    }
    return true;
}

bool LibraryDb::open (const juce::File& f)
{
    close();
    f.getParentDirectory().createDirectory();
    if (sqlite3_open (f.getFullPathName().toRawUTF8(), &db) != SQLITE_OK)
    {
        error = db != nullptr ? juce::String (sqlite3_errmsg (db)) : juce::String ("cannot open");
        close();
        return false;
    }
    exec ("PRAGMA journal_mode=WAL; PRAGMA foreign_keys=ON;");
    return exec (
        "CREATE TABLE IF NOT EXISTS tracks ("
        " id INTEGER PRIMARY KEY, path TEXT UNIQUE NOT NULL, title TEXT, artist TEXT, album TEXT, genre TEXT,"
        " comment TEXT, source TEXT, duration REAL DEFAULT 0, bpm REAL DEFAULT 0, first_beat REAL DEFAULT 0,"
        " key INTEGER DEFAULT -1, lufs REAL DEFAULT -100, rating INTEGER DEFAULT 0, analyzed INTEGER DEFAULT 0,"
        " added INTEGER DEFAULT 0, play_count INTEGER DEFAULT 0, waveform BLOB);"
        "CREATE TABLE IF NOT EXISTS crates (id INTEGER PRIMARY KEY, name TEXT UNIQUE NOT NULL, kind INTEGER DEFAULT 0);"
        "CREATE TABLE IF NOT EXISTS crate_tracks (crate_id INTEGER REFERENCES crates(id) ON DELETE CASCADE,"
        " track_id INTEGER REFERENCES tracks(id) ON DELETE CASCADE, position INTEGER, PRIMARY KEY (crate_id, track_id));"
        "CREATE TABLE IF NOT EXISTS cues (track_id INTEGER REFERENCES tracks(id) ON DELETE CASCADE, slot INTEGER,"
        " position REAL, kind INTEGER, length REAL, label TEXT, colour INTEGER, PRIMARY KEY (track_id, slot));"
        "CREATE TABLE IF NOT EXISTS folders (path TEXT PRIMARY KEY);");
}

void LibraryDb::close()
{
    if (db != nullptr)
        sqlite3_close (db);
    db = nullptr;
}

TrackInfo LibraryDb::readTrackRow (void* p)
{
    auto& s = *static_cast<Stmt*> (p);
    TrackInfo t;
    t.id = s.i64 (0);
    const auto path = s.str (1);
    t.file = juce::File::isAbsolutePath (path) ? juce::File (path) : juce::File();
    t.title = s.str (2);
    t.artist = s.str (3);
    t.album = s.str (4);
    t.genre = s.str (5);
    t.comment = s.str (6);
    t.source = s.str (7);
    t.duration = s.dbl (8);
    t.bpm = s.dbl (9);
    t.firstBeat = s.dbl (10);
    t.key = s.i32 (11);
    t.lufs = (float) s.dbl (12);
    t.rating = s.i32 (13);
    t.analyzed = s.i32 (14) != 0;
    t.added = s.i64 (15);
    t.playCount = s.i32 (16);
    return t;
}

juce::int64 LibraryDb::addTrack (const TrackInfo& t)
{
    if (db == nullptr) return -1;
    if (auto existing = findByPath (t.file))
        return existing->id;
    Stmt s (db, "INSERT INTO tracks (path, title, artist, album, genre, comment, source, duration, bpm, first_beat, key, lufs,"
                " rating, analyzed, added) VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)");
    if (! s.ok()) return -1;
    s.bind (1, t.file.getFullPathName());
    s.bind (2, t.title);
    s.bind (3, t.artist);
    s.bind (4, t.album);
    s.bind (5, t.genre);
    s.bind (6, t.comment);
    s.bind (7, t.source);
    s.bind (8, t.duration);
    s.bind (9, t.bpm);
    s.bind (10, t.firstBeat);
    s.bind (11, t.key);
    s.bind (12, (double) t.lufs);
    s.bind (13, t.rating);
    s.bind (14, t.analyzed ? 1 : 0);
    s.bind (15, t.added != 0 ? t.added : juce::Time::currentTimeMillis());
    if (! s.run()) return -1;
    return sqlite3_last_insert_rowid (db);
}

bool LibraryDb::updateTrack (const TrackInfo& t)
{
    if (db == nullptr || t.id < 0) return false;
    Stmt s (db, "UPDATE tracks SET title=?, artist=?, album=?, genre=?, comment=?, rating=?, bpm=?, first_beat=?, key=? WHERE id=?");
    s.bind (1, t.title);
    s.bind (2, t.artist);
    s.bind (3, t.album);
    s.bind (4, t.genre);
    s.bind (5, t.comment);
    s.bind (6, t.rating);
    s.bind (7, t.bpm);
    s.bind (8, t.firstBeat);
    s.bind (9, t.key);
    s.bind (10, t.id);
    return s.run();
}

bool LibraryDb::setAnalysis (juce::int64 id, double bpm, double firstBeat, int key, float lufs, double duration,
                             const juce::MemoryBlock& waveform)
{
    if (db == nullptr) return false;
    Stmt s (db, "UPDATE tracks SET bpm=?, first_beat=?, key=?, lufs=?, duration=?, waveform=?, analyzed=1 WHERE id=?");
    s.bind (1, bpm);
    s.bind (2, firstBeat);
    s.bind (3, key);
    s.bind (4, (double) lufs);
    s.bind (5, duration);
    s.bindBlob (6, waveform);
    s.bind (7, id);
    return s.run();
}

bool LibraryDb::removeTrack (juce::int64 id)
{
    if (db == nullptr) return false;
    Stmt s (db, "DELETE FROM tracks WHERE id=?");
    s.bind (1, id);
    return s.run();
}

std::vector<TrackInfo> LibraryDb::allTracks()
{
    std::vector<TrackInfo> out;
    if (db == nullptr) return out;
    Stmt s (db, (juce::String ("SELECT ") + trackColumns + " FROM tracks ORDER BY added DESC").toRawUTF8());
    while (s.step())
        out.push_back (readTrackRow (&s));
    return out;
}

std::optional<TrackInfo> LibraryDb::getTrack (juce::int64 id)
{
    if (db == nullptr) return std::nullopt;
    Stmt s (db, (juce::String ("SELECT ") + trackColumns + " FROM tracks WHERE id=?").toRawUTF8());
    s.bind (1, id);
    if (s.step()) return readTrackRow (&s);
    return std::nullopt;
}

std::optional<TrackInfo> LibraryDb::findByPath (const juce::File& f)
{
    if (db == nullptr) return std::nullopt;
    Stmt s (db, (juce::String ("SELECT ") + trackColumns + " FROM tracks WHERE path=?").toRawUTF8());
    s.bind (1, f.getFullPathName());
    if (s.step()) return readTrackRow (&s);
    return std::nullopt;
}

std::vector<juce::int64> LibraryDb::unanalyzed()
{
    std::vector<juce::int64> out;
    if (db == nullptr) return out;
    Stmt s (db, "SELECT id FROM tracks WHERE analyzed=0 ORDER BY added DESC");
    while (s.step()) out.push_back (s.i64 (0));
    return out;
}

juce::MemoryBlock LibraryDb::getWaveform (juce::int64 id)
{
    if (db == nullptr) return {};
    Stmt s (db, "SELECT waveform FROM tracks WHERE id=?");
    s.bind (1, id);
    return s.step() ? s.blob (0) : juce::MemoryBlock();
}

void LibraryDb::incrementPlayCount (juce::int64 id)
{
    if (db == nullptr) return;
    Stmt s (db, "UPDATE tracks SET play_count = play_count + 1 WHERE id=?");
    s.bind (1, id);
    s.run();
}

int LibraryDb::countTracks()
{
    if (db == nullptr) return 0;
    Stmt s (db, "SELECT COUNT(*) FROM tracks");
    return s.step() ? s.i32 (0) : 0;
}

juce::int64 LibraryDb::createCrate (const juce::String& name, int kind)
{
    if (db == nullptr) return -1;
    if (auto id = findCrate (name); id >= 0) return id;
    Stmt s (db, "INSERT INTO crates (name, kind) VALUES (?, ?)");
    s.bind (1, name);
    s.bind (2, kind);
    return s.run() ? sqlite3_last_insert_rowid (db) : -1;
}

juce::int64 LibraryDb::findCrate (const juce::String& name)
{
    if (db == nullptr) return -1;
    Stmt s (db, "SELECT id FROM crates WHERE name=?");
    s.bind (1, name);
    return s.step() ? s.i64 (0) : -1;
}

bool LibraryDb::renameCrate (juce::int64 id, const juce::String& name)
{
    Stmt s (db, "UPDATE crates SET name=? WHERE id=?");
    s.bind (1, name);
    s.bind (2, id);
    return s.run();
}

bool LibraryDb::deleteCrate (juce::int64 id)
{
    Stmt s (db, "DELETE FROM crates WHERE id=?");
    s.bind (1, id);
    return s.run();
}

std::vector<Crate> LibraryDb::crates()
{
    std::vector<Crate> out;
    if (db == nullptr) return out;
    Stmt s (db, "SELECT id, name, kind FROM crates ORDER BY kind DESC, name COLLATE NOCASE");
    while (s.step()) out.push_back ({ s.i64 (0), s.str (1), s.i32 (2) });
    return out;
}

bool LibraryDb::addToCrate (juce::int64 crateId, juce::int64 trackId)
{
    Stmt s (db, "INSERT OR IGNORE INTO crate_tracks (crate_id, track_id, position)"
                " VALUES (?, ?, (SELECT IFNULL(MAX(position), 0) + 1 FROM crate_tracks WHERE crate_id=?))");
    s.bind (1, crateId);
    s.bind (2, trackId);
    s.bind (3, crateId);
    return s.run();
}

bool LibraryDb::removeFromCrate (juce::int64 crateId, juce::int64 trackId)
{
    Stmt s (db, "DELETE FROM crate_tracks WHERE crate_id=? AND track_id=?");
    s.bind (1, crateId);
    s.bind (2, trackId);
    return s.run();
}

std::vector<juce::int64> LibraryDb::crateTracks (juce::int64 crateId)
{
    std::vector<juce::int64> out;
    if (db == nullptr) return out;
    Stmt s (db, "SELECT track_id FROM crate_tracks WHERE crate_id=? ORDER BY position");
    s.bind (1, crateId);
    while (s.step()) out.push_back (s.i64 (0));
    return out;
}

bool LibraryDb::setCue (juce::int64 trackId, const CuePoint& c)
{
    Stmt s (db, "INSERT OR REPLACE INTO cues (track_id, slot, position, kind, length, label, colour) VALUES (?,?,?,?,?,?,?)");
    s.bind (1, trackId);
    s.bind (2, c.slot);
    s.bind (3, c.position);
    s.bind (4, c.kind);
    s.bind (5, c.length);
    s.bind (6, c.label);
    s.bind (7, (juce::int64) c.colour);
    return s.run();
}

bool LibraryDb::clearCue (juce::int64 trackId, int slot)
{
    Stmt s (db, "DELETE FROM cues WHERE track_id=? AND slot=?");
    s.bind (1, trackId);
    s.bind (2, slot);
    return s.run();
}

std::vector<CuePoint> LibraryDb::getCues (juce::int64 trackId)
{
    std::vector<CuePoint> out;
    if (db == nullptr) return out;
    Stmt s (db, "SELECT slot, position, kind, length, label, colour FROM cues WHERE track_id=? ORDER BY slot");
    s.bind (1, trackId);
    while (s.step())
        out.push_back ({ s.i32 (0), s.dbl (1), s.i32 (2), s.dbl (3), s.str (4), (juce::uint32) s.i64 (5) });
    return out;
}

bool LibraryDb::addFolder (const juce::File& f)
{
    Stmt s (db, "INSERT OR IGNORE INTO folders (path) VALUES (?)");
    s.bind (1, f.getFullPathName());
    return s.run();
}

juce::Array<juce::File> LibraryDb::folders()
{
    juce::Array<juce::File> out;
    if (db == nullptr) return out;
    Stmt s (db, "SELECT path FROM folders");
    while (s.step())
        if (juce::File::isAbsolutePath (s.str (0)))
            out.add (juce::File (s.str (0)));
    return out;
}

//==============================================================================
static juce::File rekordboxLocationToFile (const juce::String& location)
{
    // file://localhost/C:/Music/track.mp3  or  file://localhost/Users/me/Music/track.mp3
    auto path = juce::URL::removeEscapeChars (location.fromFirstOccurrenceOf ("file://localhost", false, true));
    if (path.isEmpty())
        path = juce::URL::removeEscapeChars (location.fromFirstOccurrenceOf ("file://", false, true));
   #if JUCE_WINDOWS
    if (path.startsWithChar ('/') && path.length() > 2 && path[2] == ':')
        path = path.substring (1);
   #endif
    return juce::File::isAbsolutePath (path) ? juce::File (path) : juce::File();
}

RekordboxImportResult importRekordboxXml (LibraryDb& db, const juce::File& xmlFile)
{
    RekordboxImportResult r;
    auto xml = juce::XmlDocument::parse (xmlFile);
    if (xml == nullptr || ! xml->hasTagName ("DJ_PLAYLISTS"))
        return r;

    std::map<juce::String, juce::int64> idMap;
    if (auto* collection = xml->getChildByName ("COLLECTION"))
    {
        for (auto* t : collection->getChildWithTagNameIterator ("TRACK"))
        {
            TrackInfo info;
            info.file = rekordboxLocationToFile (t->getStringAttribute ("Location"));
            if (info.file == juce::File()) continue;
            if (! info.file.existsAsFile()) ++r.missing;
            info.title = t->getStringAttribute ("Name");
            info.artist = t->getStringAttribute ("Artist");
            info.album = t->getStringAttribute ("Album");
            info.genre = t->getStringAttribute ("Genre");
            info.comment = t->getStringAttribute ("Comments");
            info.duration = t->getDoubleAttribute ("TotalTime");
            info.bpm = t->getDoubleAttribute ("AverageBpm");
            info.rating = t->getIntAttribute ("Rating") / 51;
            info.key = MusicalKey::fromString (t->getStringAttribute ("Tonality")).toIndex();
            info.source = "Rekordbox";
            if (auto* tempo = t->getChildByName ("TEMPO"))
            {
                info.firstBeat = tempo->getDoubleAttribute ("Inizio");
                if (info.bpm <= 0) info.bpm = tempo->getDoubleAttribute ("Bpm");
            }
            const auto id = db.addTrack (info);
            if (id < 0) continue;
            info.id = id;
            db.updateTrack (info);
            idMap[t->getStringAttribute ("TrackID")] = id;
            ++r.tracks;

            for (auto* pm : t->getChildWithTagNameIterator ("POSITION_MARK"))
            {
                const int num = pm->getIntAttribute ("Num", -1);
                if (num < 0 || num > 7) continue; // memory cues have Num=-1
                CuePoint c;
                c.slot = num;
                c.position = pm->getDoubleAttribute ("Start");
                c.kind = pm->getIntAttribute ("Type") == 4 ? 1 : 0;
                if (c.kind == 1) c.length = pm->getDoubleAttribute ("End") - c.position;
                c.label = pm->getStringAttribute ("Name");
                c.colour = juce::Colour ((juce::uint8) pm->getIntAttribute ("Red"), (juce::uint8) pm->getIntAttribute ("Green"),
                                         (juce::uint8) pm->getIntAttribute ("Blue")).getARGB();
                db.setCue (id, c);
                ++r.cues;
            }
        }
    }

    std::function<void (juce::XmlElement&, const juce::String&)> walk = [&] (juce::XmlElement& node, const juce::String& prefix)
    {
        for (auto* child : node.getChildWithTagNameIterator ("NODE"))
        {
            const auto name = child->getStringAttribute ("Name");
            if (child->getIntAttribute ("Type") == 1)
            {
                const auto crate = db.createCrate (prefix + name);
                for (auto* t : child->getChildWithTagNameIterator ("TRACK"))
                    if (auto it = idMap.find (t->getStringAttribute ("Key")); it != idMap.end())
                        db.addToCrate (crate, it->second);
                ++r.playlists;
            }
            else
            {
                walk (*child, name == "ROOT" ? juce::String() : prefix + name + " / ");
            }
        }
    };
    if (auto* playlists = xml->getChildByName ("PLAYLISTS"))
        walk (*playlists, {});
    return r;
}

} // namespace merge
