#include "TestHelpers.h"
#include "analysis/Analysis.h"
#include "dj/DjCore.h"
#include "library/LibraryDb.h"

using namespace merge;
using namespace merge::test;

namespace
{
    SampleBuffer::Ptr makeKick (double sr = 44100.0)
    {
        juce::AudioBuffer<float> b (1, (int) (0.3 * sr));
        double ph = 0.0;
        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            const double t = i / sr;
            const double f = 50.0 + 100.0 * std::exp (-t / 0.03);
            ph += 2.0 * juce::MathConstants<double>::pi * f / sr;
            b.setSample (0, i, (float) (std::sin (ph) * std::exp (-t / 0.12) * 0.9));
        }
        return SampleBuffer::create (std::move (b), sr, "kick");
    }

    SampleBuffer::Ptr makeNoiseHit (double decay, float gain, int seed, bool highPass = false, double sr = 44100.0)
    {
        juce::Random rng (seed);
        juce::AudioBuffer<float> b (1, (int) (decay * 6.0 * sr));
        float prev1 = 0.0f, prev2 = 0.0f;
        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            float n = rng.nextFloat() * 2.0f - 1.0f;
            if (highPass) // second difference: a crude 12 dB/oct high-pass, like a real hi-hat
            {
                const float y = n - 2.0f * prev1 + prev2;
                prev2 = prev1;
                prev1 = n;
                n = y * 0.5f;
            }
            b.setSample (0, i, n * gain * (float) std::exp (-(i / sr) / decay));
        }
        return SampleBuffer::create (std::move (b), sr, "noise");
    }

    /** Renders a drum loop at the given tempo with synthetic sounds. */
    juce::AudioBuffer<float> renderBeat (double bpm, int bars, bool fourOnFloor)
    {
        Project p;
        p.setAudioConfig (44100.0, 512);
        p.bpm = bpm;
        auto& ch = p.getChannels();
        p.setChannelSample (ch[0].id, makeKick(), {}, 60);
        p.setChannelSample (ch[1].id, makeNoiseHit (0.08, 0.6f, 1), {}, 60);
        p.setChannelSample (ch[2].id, makeNoiseHit (0.02, 0.35f, 2, true), {}, 60);
        p.setStepsFromString (0, ch[0].id, fourOnFloor ? "x...x...x...x..." : "x.........x.....");
        p.setStepsFromString (0, ch[1].id, "....x.......x...");
        p.setStepsFromString (0, ch[2].id, "x.x.x.x.x.x.x.x.");
        RenderOptions o;
        o.loops = bars;
        o.tailSeconds = 0.0;
        return renderProject (p, o);
    }

    /** Sum of decaying harmonic tones (piano-ish), for key detection. */
    void addChord (juce::AudioBuffer<float>& b, int start, int length, std::initializer_list<int> notes, double sr = 44100.0)
    {
        for (int note : notes)
        {
            const double f = 440.0 * std::pow (2.0, (note - 69) / 12.0);
            for (int i = 0; i < length && start + i < b.getNumSamples(); ++i)
            {
                const double t = i / sr;
                float v = 0.0f;
                for (int h = 1; h <= 4; ++h)
                    v += (float) (std::sin (2.0 * juce::MathConstants<double>::pi * f * h * t) / (h * h));
                v *= 0.12f * (float) std::exp (-t / 1.2);
                b.addSample (0, start + i, v);
                b.addSample (1, start + i, v);
            }
        }
    }

    double dominantFrequency (const juce::AudioBuffer<float>& b, int start, int count, double sr)
    {
        int crossings = 0;
        for (int i = start + 1; i < start + count; ++i)
            if ((b.getSample (0, i - 1) < 0) != (b.getSample (0, i) < 0)) ++crossings;
        return crossings / 2.0 / (count / sr);
    }

    TrackData::Ptr makeTrack (SampleBuffer::Ptr audio, double bpm = 0.0, double firstBeat = 0.0)
    {
        auto t = TrackData::create (std::move (audio));
        t->grid.bpm = bpm;
        t->grid.firstBeatSec = firstBeat;
        return t;
    }

    /** Click track: short full-scale clicks on every beat. */
    SampleBuffer::Ptr makeClickTrack (double bpm, double seconds, double sr = 44100.0)
    {
        juce::AudioBuffer<float> b (2, (int) (seconds * sr));
        b.clear();
        const double spb = 60.0 / bpm * sr;
        for (double pos = 0.0; pos < b.getNumSamples() - 40; pos += spb)
            for (int i = 0; i < 30; ++i)
            {
                b.setSample (0, (int) pos + i, 0.9f);
                b.setSample (1, (int) pos + i, 0.9f);
            }
        return SampleBuffer::create (std::move (b), sr, "clicks");
    }
}

class AnalysisTests : public juce::UnitTest
{
public:
    AnalysisTests() : juce::UnitTest ("Analysis", "mix") {}

    void runTest() override
    {
        beginTest ("Tempo detection on rendered beats");
        struct TempoCase { double bpm; bool fourOnFloor; double minBpm, maxBpm; };
        // Drum & bass is genuinely ambiguous (87 half-time vs 174); DJs pick a range, Merge offers 88-175.
        for (auto c : std::vector<TempoCase> { { 85.0, false, 70.0, 180.0 }, { 120.0, true, 70.0, 180.0 }, { 128.0, true, 70.0, 180.0 },
                                               { 140.0, false, 70.0, 180.0 }, { 174.0, false, 88.0, 175.0 } })
        {
            const double bpm = c.bpm;
            auto audio = renderBeat (bpm, 16, c.fourOnFloor);
            auto mono = analysis::toMono (audio);
            auto r = analysis::detectTempo (mono, 44100.0, c.minBpm, c.maxBpm);
            expect (std::abs (r.bpm - bpm) <= 0.5, "expected " + juce::String (bpm) + " got " + juce::String (r.bpm, 2));
            // beat phase: kicks start at 0, so the grid should land on a beat
            const double beatSec = 60.0 / bpm;
            double phaseErr = std::fmod (r.firstBeatSec, beatSec);
            phaseErr = juce::jmin (phaseErr, beatSec - phaseErr);
            expect (phaseErr < 0.010, "beat phase error " + juce::String (phaseErr * 1000.0, 1) + " ms at " + juce::String (bpm));
        }

        beginTest ("Key detection on chord progressions");
        {
            struct Case { const char* name; std::vector<std::vector<int>> chords; };
            const std::vector<Case> cases {
                { "C major",  { { 48, 60, 64, 67 }, { 53, 65, 69, 72 }, { 55, 67, 71, 74 }, { 48, 60, 64, 67 } } },
                { "A minor",  { { 45, 57, 60, 64 }, { 50, 62, 65, 69 }, { 52, 64, 68, 71 }, { 45, 57, 60, 64 } } },
                { "F# minor", { { 42, 54, 57, 61 }, { 47, 59, 62, 66 }, { 49, 61, 64, 68 }, { 42, 54, 57, 61 } } },
                { "Eb major", { { 51, 63, 67, 70 }, { 56, 68, 72, 75 }, { 58, 70, 74, 77 }, { 51, 63, 67, 70 } } },
            };
            for (auto& c : cases)
            {
                juce::AudioBuffer<float> b (2, 44100 * 16);
                b.clear();
                for (int rep = 0; rep < 2; ++rep)
                    for (size_t i = 0; i < c.chords.size(); ++i)
                    {
                        const auto& ch = c.chords[i];
                        const int start = (int) ((rep * 4 + i) * 2 * 44100);
                        for (int note : ch)
                            addChord (b, start, 2 * 44100, { note });
                    }
                auto k = analysis::detectKey (analysis::toMono (b), 44100.0);
                expectEquals (k.key.toString(), juce::String (c.name) == "Eb major" ? juce::String ("Eb major") : juce::String (c.name));
            }
        }

        beginTest ("Integrated loudness of a -20 dBFS 1 kHz tone is about -20 LUFS");
        {
            auto s = makeSine (1000.0, 5.0, 44100.0, 0.1f);
            const float lufs = analysis::integratedLoudness (s->getAudio(), 44100.0);
            expect (std::abs (lufs - (-20.0f)) < 0.6f, "LUFS " + juce::String (lufs, 2));
        }

        beginTest ("Transient detection finds every hit");
        {
            std::vector<float> mono (44100 * 4, 0.0f);
            const std::vector<int> hits { 4410, 22050, 30000, 48000, 70000, 100000, 130000, 160000 };
            juce::Random rng (3);
            for (int h : hits)
                for (int i = 0; i < 6000; ++i)
                    mono[(size_t) (h + i)] += (rng.nextFloat() * 2.0f - 1.0f) * 0.8f * std::exp (-i / 900.0f);
            auto found = analysis::detectTransients (mono, 44100.0, 0.5f);
            expectEquals ((int) found.size(), (int) hits.size());
            for (size_t i = 0; i < juce::jmin (found.size(), hits.size()); ++i)
                expect (std::abs (found[i] - hits[i]) < 441, "hit " + juce::String ((int) i) + " at " + juce::String (found[i]));
        }

        beginTest ("Waveform blob round trip");
        {
            auto audio = renderBeat (120.0, 2, true);
            auto w = analysis::computeWaveform (audio, 44100.0);
            expect (w.points.size() > 100);
            auto w2 = analysis::Waveform::fromBlob (w.toBlob());
            expectEquals ((int) w2.points.size(), (int) w.points.size());
            expect (std::abs (w2.pointsPerSecond - w.pointsPerSecond) < 1e-9);
        }

        beginTest ("Camelot compatibility");
        {
            const auto am = MusicalKey::fromString ("A minor");
            expect (analysis::keysCompatible (am, MusicalKey::fromString ("C major")));  // 8A / 8B
            expect (analysis::keysCompatible (am, MusicalKey::fromString ("E minor")));  // 9A
            expect (analysis::keysCompatible (am, MusicalKey::fromString ("D minor")));  // 7A
            expect (! analysis::keysCompatible (am, MusicalKey::fromString ("F# minor")));
        }
    }
};

class DeckTests : public juce::UnitTest
{
public:
    DeckTests() : juce::UnitTest ("DJ decks", "mix") {}

    void runTest() override
    {
        const double sr = 44100.0;
        const int block = 512;

        beginTest ("Keylock keeps the pitch while the tempo changes");
        {
            for (bool keylock : { true, false })
            {
                Deck deck (0);
                deck.prepare (sr, block);
                auto track = makeTrack (makeSine (440.0, 10.0, sr, 0.5f));
                deck.post ({ DeckCommand::Load, 0.0, 0, track.get() });
                deck.keylock.store (keylock);
                deck.pitchRange.store (0.5f);
                deck.pitch.store (0.4f); // +20% tempo
                deck.post ({ DeckCommand::Play });
                MasterClock clock;
                juce::AudioBuffer<float> out (2, block), all (2, block * 200);
                for (int b = 0; b < 200; ++b)
                {
                    deck.process (out, block, clock);
                    all.copyFrom (0, b * block, out, 0, 0, block);
                }
                const double f = dominantFrequency (all, block * 60, block * 120, sr);
                const double expected = keylock ? 440.0 : 528.0;
                expect (std::abs (f - expected) / expected < 0.015, (keylock ? "keylock " : "vinyl ") + juce::String (f, 1) + " Hz");
                expect (std::abs (deck.positionSec.load() - 200.0 * block / sr * 1.2) < 0.2, "position advanced at 1.2x");
            }
        }

        beginTest ("Sync matches tempo and beat phase to the master deck");
        {
            for (bool keylock : { true, false })
            {
                DjCore dj;
                dj.prepare (sr, block);
                auto a = makeTrack (makeClickTrack (120.0, 30.0), 120.0, 0.0);
                auto b = makeTrack (makeClickTrack (126.0, 30.0), 126.0, 0.0);
                dj.deck (0).post ({ DeckCommand::Load, 0.0, 0, a.get() });
                dj.deck (1).post ({ DeckCommand::Load, 0.0, 0, b.get() });
                dj.deck (0).keylock.store (keylock);
                dj.deck (1).keylock.store (keylock);
                dj.deck (0).post ({ DeckCommand::Play });
                juce::AudioBuffer<float> m (2, block), c (2, block);
                for (int i = 0; i < 100; ++i) { m.clear(); c.clear(); dj.process (m, c, block); }
                // start deck B a little off-beat with sync on
                dj.deck (1).post ({ DeckCommand::Seek, 0.21 });
                dj.deck (1).sync.store (true);
                dj.deck (1).post ({ DeckCommand::Play });
                for (int i = 0; i < 400; ++i) { m.clear(); c.clear(); dj.process (m, c, block); }
                expectEquals (dj.currentMasterDeck.load(), 0);
                expect (std::abs (dj.deck (1).effectiveBpm.load() - 120.0) < 0.01, "synced bpm " + juce::String (dj.deck (1).effectiveBpm.load()));
                const double beatA = dj.deck (0).positionSec.load() * 2.0;      // beats at 120
                const double beatB = dj.deck (1).positionSec.load() * 126.0 / 60.0;
                double diff = (beatA - std::floor (beatA)) - (beatB - std::floor (beatB));
                if (diff > 0.5) diff -= 1.0;
                if (diff < -0.5) diff += 1.0;
                const double ms = std::abs (diff) * 500.0;
                expect (ms < 5.0, (keylock ? "keylock" : "vinyl") + juce::String (" phase error ") + juce::String (ms, 2) + " ms");
            }
        }

        beginTest ("Auto loop keeps playback inside the loop");
        {
            Deck deck (0);
            deck.prepare (sr, block);
            auto t = makeTrack (makeClickTrack (120.0, 30.0), 120.0, 0.0);
            deck.post ({ DeckCommand::Load, 0.0, 0, t.get() });
            deck.keylock.store (false);
            deck.post ({ DeckCommand::Seek, 4.2 });
            deck.post ({ DeckCommand::Play });
            deck.post ({ DeckCommand::LoopAuto, 2.0 }); // 2 beats = 1 s, from beat 8 (4.0 s)
            MasterClock clock;
            juce::AudioBuffer<float> out (2, block);
            double maxPos = 0.0, minPos = 1e9;
            for (int i = 0; i < 600; ++i)
            {
                deck.process (out, block, clock);
                if (i > 10) { maxPos = juce::jmax (maxPos, deck.positionSec.load()); minPos = juce::jmin (minPos, deck.positionSec.load()); }
            }
            expect (deck.loopActive.load());
            expect (std::abs (deck.loopInSec.load() - 4.0) < 0.002, "loop in " + juce::String (deck.loopInSec.load()));
            expect (std::abs (deck.loopOutSec.load() - 5.0) < 0.002, "loop out " + juce::String (deck.loopOutSec.load()));
            expect (maxPos <= 5.01 && minPos >= 3.99, "position stayed in loop: " + juce::String (minPos) + " .. " + juce::String (maxPos));
        }

        beginTest ("Hot cues and CDJ cue");
        {
            Deck deck (0);
            deck.prepare (sr, block);
            auto t = makeTrack (makeClickTrack (120.0, 30.0), 120.0, 0.0);
            deck.post ({ DeckCommand::Load, 0.0, 0, t.get() });
            deck.keylock.store (false);
            deck.post ({ DeckCommand::SetHotCue, 10.0, 2 });
            deck.post ({ DeckCommand::HotCue, 0.0, 2 });
            MasterClock clock;
            juce::AudioBuffer<float> out (2, block);
            deck.process (out, block, clock);
            expect (deck.isPlaying());
            expect (std::abs (deck.positionSec.load() - (10.0 + block / sr)) < 0.01);
            deck.post ({ DeckCommand::CueDown }); // playing -> back to cue point (0) and stop
            deck.process (out, block, clock);
            expect (! deck.isPlaying());
            expect (deck.positionSec.load() < 0.01);
        }

        beginTest ("Isolator EQ: kill removes the band, flat passes it");
        {
            for (bool kill : { false, true })
            {
                DjCore dj;
                dj.prepare (sr, block);
                auto t = makeTrack (makeSine (60.0, 5.0, sr, 0.5f));
                dj.deck (0).post ({ DeckCommand::Load, 0.0, 0, t.get() });
                dj.deck (0).keylock.store (false);
                dj.deck (0).post ({ DeckCommand::Play });
                dj.channel (0).killLow.store (kill);
                dj.masterVolume.store (0.8f);
                juce::AudioBuffer<float> m (2, block), c (2, block);
                double sum = 0.0;
                int count = 0;
                for (int i = 0; i < 200; ++i)
                {
                    m.clear(); c.clear();
                    dj.process (m, c, block);
                    if (i > 40) { sum += m.getRMSLevel (0, 0, block); ++count; }
                }
                const double level = juce::Decibels::gainToDecibels (sum / count / (0.5 / std::sqrt (2.0)));
                if (kill) expect (level < -30.0, "killed low band level " + juce::String (level, 1) + " dB");
                else expect (std::abs (level) < 1.0, "flat level " + juce::String (level, 2) + " dB");
            }
        }

        beginTest ("Crossfader curves");
        {
            float a, b;
            DjCore::crossfaderGains (-1.0f, 0.0f, a, b); expect (a > 0.99f && b < 0.01f);
            DjCore::crossfaderGains (0.0f, 0.0f, a, b);  expect (a > 0.99f && b > 0.99f);
            DjCore::crossfaderGains (1.0f, 0.0f, a, b);  expect (a < 0.01f && b > 0.99f);
            DjCore::crossfaderGains (0.5f, 1.0f, a, b);  expect (a > 0.99f && b > 0.99f);
            DjCore::crossfaderGains (0.97f, 1.0f, a, b); expect (a < 0.35f);
        }
    }
};

class LibraryTests : public juce::UnitTest
{
public:
    LibraryTests() : juce::UnitTest ("Library", "mix") {}

    void runTest() override
    {
        auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("merge-lib-test");
        dir.deleteRecursively();
        dir.createDirectory();

        beginTest ("SQLite tracks, analysis, crates and cues");
        {
            LibraryDb db;
            expect (db.open (dir.getChildFile ("library.db")));
            TrackInfo t;
            t.file = dir.getChildFile ("song.wav");
            t.title = "Song";
            t.artist = "Artist";
            const auto id = db.addTrack (t);
            expect (id > 0);
            expectEquals (db.addTrack (t), id); // no duplicates
            expectEquals ((int) db.unanalyzed().size(), 1);
            analysis::Waveform w;
            w.pointsPerSecond = 100.0;
            w.points.resize (10);
            expect (db.setAnalysis (id, 124.0, 0.12, 9 + 12, -9.5f, 210.0, w.toBlob()));
            auto got = db.getTrack (id);
            expect (got.has_value());
            expectEquals (got->bpm, 124.0);
            expectEquals (got->key, 21);
            expect (got->analyzed);
            expectEquals ((int) analysis::Waveform::fromBlob (db.getWaveform (id)).points.size(), 10);
            const auto crate = db.createCrate ("Warmup");
            expect (db.addToCrate (crate, id));
            expectEquals ((int) db.crateTracks (crate).size(), 1);
            expect (db.setCue (id, { 3, 42.5, 0, 0.0, "Drop", 0xffff0000 }));
            auto cues = db.getCues (id);
            expectEquals ((int) cues.size(), 1);
            expectEquals (cues[0].slot, 3);
            expectEquals (cues[0].position, 42.5);
            expect (db.removeTrack (id));
            expectEquals (db.countTracks(), 0);
            expectEquals ((int) db.crateTracks (crate).size(), 0);
        }

        beginTest ("Rekordbox XML import");
        {
            auto music = dir.getChildFile ("My Music");
            music.createDirectory();
            auto track = music.getChildFile ("Deep Tune.wav");
            track.replaceWithText ("x");
            auto url = juce::URL (track).toString (false);
            const auto location = "file://localhost" + url.fromFirstOccurrenceOf ("file://", false, false);
            juce::String xml;
            xml << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<DJ_PLAYLISTS Version=\"1.0.0\"><PRODUCT Name=\"rekordbox\"/>"
                << "<COLLECTION Entries=\"1\"><TRACK TrackID=\"7\" Name=\"Deep Tune\" Artist=\"Someone\" Genre=\"House\" TotalTime=\"300\""
                << " AverageBpm=\"122.00\" Tonality=\"Am\" Rating=\"204\" Location=\"" << location << "\">"
                << "<TEMPO Inizio=\"0.150\" Bpm=\"122.00\" Metro=\"4/4\" Battito=\"1\"/>"
                << "<POSITION_MARK Name=\"Intro\" Type=\"0\" Start=\"0.150\" Num=\"0\" Red=\"40\" Green=\"226\" Blue=\"20\"/>"
                << "<POSITION_MARK Name=\"\" Type=\"0\" Start=\"60.0\" Num=\"-1\"/>"
                << "<POSITION_MARK Name=\"Drop\" Type=\"0\" Start=\"94.5\" Num=\"2\" Red=\"255\" Green=\"0\" Blue=\"0\"/>"
                << "</TRACK></COLLECTION><PLAYLISTS><NODE Type=\"0\" Name=\"ROOT\" Count=\"1\">"
                << "<NODE Name=\"Friday\" Type=\"1\" KeyType=\"0\" Entries=\"1\"><TRACK Key=\"7\"/></NODE></NODE></PLAYLISTS></DJ_PLAYLISTS>";
            auto xmlFile = dir.getChildFile ("rekordbox.xml");
            xmlFile.replaceWithText (xml);

            LibraryDb db;
            expect (db.open (dir.getChildFile ("rb.db")));
            auto r = importRekordboxXml (db, xmlFile);
            expectEquals (r.tracks, 1);
            expectEquals (r.cues, 2);
            expectEquals (r.playlists, 1);
            auto t = db.findByPath (track);
            expect (t.has_value(), "track path resolved");
            if (t)
            {
                expectEquals (t->bpm, 122.0);
                expectEquals (t->firstBeat, 0.15);
                expectEquals (MusicalKey::fromIndex (t->key).toString(), juce::String ("A minor"));
                expectEquals (t->rating, 4);
                expectEquals ((int) db.getCues (t->id).size(), 2);
                expectEquals ((int) db.crateTracks (db.findCrate ("Friday")).size(), 1);
            }
        }
        dir.deleteRecursively();
    }
};

static AnalysisTests analysisTests;
static DeckTests deckTests;
static LibraryTests libraryTests;

#include "stems/QuickStems.h"

class StemTests : public juce::UnitTest
{
public:
    StemTests() : juce::UnitTest ("Stems", "mix") {}

    void runTest() override
    {
        beginTest ("Quick Stems add back up to the original");
        auto mix = renderBeat (120.0, 4, true);
        // add a centred tone (vocal-ish) and a sub bass
        for (int i = 0; i < mix.getNumSamples(); ++i)
        {
            const float t = i / 44100.0f;
            const float v = 0.15f * std::sin (2.0f * juce::MathConstants<float>::pi * 440.0f * t)
                          + 0.2f * std::sin (2.0f * juce::MathConstants<float>::pi * 55.0f * t);
            mix.addSample (0, i, v);
            mix.addSample (1, i, v);
        }
        stems::Result r;
        expect (stems::separate (mix, 44100.0, r));
        double err = 0.0, ref = 0.0;
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < mix.getNumSamples(); ++i)
            {
                float sum = 0.0f;
                for (auto& s : r.stems) sum += s.getSample (ch, i);
                const float d = sum - mix.getSample (ch, i);
                err += d * d;
                ref += mix.getSample (ch, i) * mix.getSample (ch, i);
            }
        const double snr = 10.0 * std::log10 (ref / juce::jmax (1e-20, err));
        expect (snr > 60.0, "reconstruction SNR " + juce::String (snr, 1) + " dB");

        beginTest ("Quick Stems put energy where it belongs");
        auto energy = [] (const juce::AudioBuffer<float>& b) { return b.getRMSLevel (0, 0, b.getNumSamples()); };
        const float drums = energy (r.stems[0]), bass = energy (r.stems[1]), vocals = energy (r.stems[2]);
        logMessage ("drums " + juce::String (drums, 3) + " bass " + juce::String (bass, 3) + " vocals " + juce::String (vocals, 3)
                    + " melody " + juce::String (energy (r.stems[3]), 3));
        expect (bass > 0.08f, "sub bass lands in the bass stem");
        expect (vocals > 0.05f, "centred tone lands in the vocal stem");
        expect (drums > 0.03f, "drums land in the drum stem");
    }
};

static StemTests stemTests;

#include "dj/Recorder.h"

class RecorderTests : public juce::UnitTest
{
public:
    RecorderTests() : juce::UnitTest ("Set recorder", "mix") {}

    void runTest() override
    {
        beginTest ("Records master and decks to WAV without blocking");
        auto folder = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("merge-rec-test");
        folder.deleteRecursively();
        {
            Recorder rec;
            expect (rec.start (folder, 44100.0, true, 2));
            juce::AudioBuffer<float> buf (2, 441);
            for (int i = 0; i < 441; ++i) { buf.setSample (0, i, 0.5f); buf.setSample (1, i, -0.5f); }
            for (int b = 0; b < 100; ++b)
            {
                rec.capture (buf, 441);
                rec.captureDeck (0, buf, 441);
                rec.captureDeck (1, buf, 441);
            }
            expect (std::abs (rec.getSecondsRecorded() - 1.0) < 0.001);
            rec.stop();
        }
        for (auto name : { "master.wav", "deck-1.wav", "deck-2.wav" })
        {
            auto s = loadSampleFile (folder.getChildFile (name));
            expect (s != nullptr, name);
            if (s != nullptr)
            {
                expectEquals (s->getNumFrames(), 44100);
                expect (std::abs (s->getAudio().getSample (0, 1000) - 0.5f) < 0.001f);
            }
        }
        folder.deleteRecursively();
    }
};

static RecorderTests recorderTests;
