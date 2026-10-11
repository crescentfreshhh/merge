#include "DjController.h"
#include "AppContext.h"
#include "LibraryService.h"
#include "library/TrackLoader.h"
#include "StemService.h"

namespace merge
{

DjController::DjController (AppContext& c) : ctx (c)
{
    for (auto& s : states) s.hotCues.fill (-1.0);
    startTimerHz (20);
}

DjController::~DjController()
{
    alive->store (false);
    stopTimer();
    loader.removeAllJobs (true, 5000);
}

Deck& DjController::deck (int d) { return ctx.dj.deck (d); }
DjChannel& DjController::channel (int d) { return ctx.dj.channel (d); }

int DjController::firstFreeDeck() const
{
    const int n = ctx.dj.numDecks.load();
    for (int i = 0; i < n; ++i)
        if (states[(size_t) i].track == nullptr && ! states[(size_t) i].loading) return i;
    for (int i = 0; i < n; ++i)
        if (! ctx.dj.deck (i).playing.load()) return i;
    return 0;
}

void DjController::loadTrack (int d, juce::int64 trackId)
{
    if (auto* t = ctx.library->find (trackId))
    {
        auto info = *t;
        auto& s = state (d);
        s.loading = true;
        s.status = "Loading " + info.title + "...";
        s.info = info;
        sendChangeMessage();

        const int gen = ++loadGeneration[(size_t) d];
        const double sr = ctx.engine.getSampleRate();
        const bool needAnalysis = ! info.analyzed;
        auto wave = ctx.library->waveformFor (trackId);
        const double lo = ctx.library->getMinBpm(), hi = ctx.library->getMaxBpm();
        auto aliveFlag = alive;
        loader.addJob ([this, aliveFlag, d, gen, info, sr, needAnalysis, wave, lo, hi]
        {
            auto audio = decodeForPlayback (info.file, sr, [this, d, gen, aliveFlag] { return ! aliveFlag->load() || loadGeneration[(size_t) d].load() != gen; });
            std::optional<analysis::TrackAnalysis> fresh;
            if (audio != nullptr && needAnalysis)
                fresh = analyseFile (info.file, lo, hi);
            auto waveform = wave;
            if (audio != nullptr && waveform.points.empty() && ! fresh)
                waveform = analysis::computeWaveform (audio->getAudio(), audio->getSampleRate());
            juce::MessageManager::callAsync ([this, aliveFlag, d, gen, info, audio, wave = waveform, fresh]
            {
                if (! aliveFlag->load() || loadGeneration[(size_t) d].load() != gen) return;
                applyLoaded (d, info, audio, wave, fresh);
            });
        });
    }
}

void DjController::loadFile (int d, const juce::File& file)
{
    if (! LibraryService::isAudioFile (file)) return;
    ctx.library->addFiles ({ file.getFullPathName() });
    if (auto t = ctx.library->db().findByPath (file))
        loadTrack (d, t->id);
}

void DjController::applyLoaded (int d, TrackInfo info, SampleBuffer::Ptr audio, analysis::Waveform wave,
                                std::optional<analysis::TrackAnalysis> fresh)
{
    auto& s = state (d);
    s.loading = false;
    if (audio == nullptr)
    {
        s.status = "Couldn't play " + info.file.getFileName();
        ctx.setStatus (s.status);
        sendChangeMessage();
        return;
    }
    if (fresh)
    {
        info.bpm = fresh->tempo.bpm;
        info.firstBeat = fresh->tempo.firstBeatSec;
        info.key = fresh->key.key.toIndex();
        info.lufs = fresh->lufs;
        info.duration = fresh->durationSec;
        info.analyzed = true;
        wave = fresh->waveform;
        ctx.library->db().setAnalysis (info.id, info.bpm, info.firstBeat, info.key, info.lufs, info.duration, wave.toBlob());
        ctx.library->refresh();
    }

    auto track = TrackData::create (audio);
    track->grid.bpm = info.bpm;
    track->grid.firstBeatSec = info.firstBeat;
    track->key = MusicalKey::fromIndex (info.key);
    track->title = info.title;
    track->artist = info.artist;
    track->trackId = info.id;
    track->file = info.file;
    track->autoGainDb = info.lufs > -60.0f ? juce::jlimit (-12.0f, 12.0f, -10.0f - info.lufs) : 0.0f;

    s.info = info;
    s.track = track;
    s.waveform = wave;
    s.stemProgress = -1.0f;
    s.stemsHQ = false;
    s.hotCues.fill (-1.0);
    s.status.clear();
    channel (d).autoGainDb.store (ctx.settings().getBoolValue ("autoGain", true) ? track->autoGainDb : 0.0f);
    for (int k = 0; k < NumStems; ++k) deck (d).stemGain[(size_t) k].store (1.0f);

    deck (d).post ({ DeckCommand::Load, 0.0, 0, track.get() });
    for (auto& c : ctx.library->db().getCues (info.id))
        if (c.slot >= 0 && c.slot < 8 && c.kind == 0)
        {
            s.hotCues[(size_t) c.slot] = c.position;
            deck (d).post ({ DeckCommand::SetHotCue, c.position, c.slot });
        }
    ctx.library->db().incrementPlayCount (info.id);
    if (ctx.stems != nullptr)
        ctx.stems->attachCached (d);
    ctx.setStatus ("Deck " + juce::String::charToString ((juce::juce_wchar) ('A' + d)) + ": " + info.title
                   + (info.bpm > 0 ? "  -  " + juce::String (info.bpm, 2) + " BPM" : juce::String())
                   + (info.key >= 0 ? "  -  " + MusicalKey::fromIndex (info.key).toCamelot() : juce::String()));
    sendChangeMessage();
}

void DjController::eject (int d)
{
    ++loadGeneration[(size_t) d];
    deck (d).post ({ DeckCommand::Eject });
    states[(size_t) d] = {};
    states[(size_t) d].hotCues.fill (-1.0);
    sendChangeMessage();
}

void DjController::togglePlay (int d) { deck (d).post ({ DeckCommand::TogglePlay }); }
void DjController::play (int d) { deck (d).post ({ DeckCommand::Play }); }
void DjController::pause (int d) { deck (d).post ({ DeckCommand::Pause }); }
void DjController::cue (int d, bool down) { deck (d).post ({ down ? DeckCommand::CueDown : DeckCommand::CueUp }); }

void DjController::setSync (int d, bool on)
{
    deck (d).sync.store (on);
    if (on) deck (d).post ({ DeckCommand::AlignPhase });
    sendChangeMessage();
}

void DjController::setMaster (int d)
{
    ctx.dj.masterDeckPref.store (ctx.dj.masterDeckPref.load() == d ? -1 : d);
    sendChangeMessage();
}

void DjController::seekFraction (int d, double fraction)
{
    if (auto& t = state (d).track; t != nullptr)
        deck (d).post ({ DeckCommand::Seek, juce::jlimit (0.0, 1.0, fraction) * t->lengthSeconds() });
}

void DjController::beatJump (int d, double beats) { deck (d).post ({ DeckCommand::BeatJump, beats }); }
void DjController::nudge (int d, float amount) { deck (d).nudge.store (amount); }

void DjController::hotCue (int d, int slot, bool erase)
{
    auto& s = state (d);
    if (s.track == nullptr || slot < 0 || slot >= 8) return;
    if (erase)
    {
        s.hotCues[(size_t) slot] = -1.0;
        deck (d).post ({ DeckCommand::ClearHotCue, 0.0, slot });
        ctx.library->db().clearCue (s.info.id, slot);
    }
    else if (s.hotCues[(size_t) slot] < 0.0)
    {
        double pos = deck (d).positionSec.load();
        if (deck (d).quantize.load() && s.track->grid.isValid())
            pos = s.track->grid.snap (pos);
        s.hotCues[(size_t) slot] = pos;
        deck (d).post ({ DeckCommand::SetHotCue, pos, slot });
        static const juce::uint32 colours[] { 0xffff5a5f, 0xffff7a1a, 0xfff2d43d, 0xff3fd28a, 0xff19d3c5, 0xff3fa9f5, 0xffb37bff, 0xffff6bd6 };
        ctx.library->db().setCue (s.info.id, { slot, pos, 0, 0.0, {}, colours[slot] });
    }
    else
    {
        deck (d).post ({ DeckCommand::HotCue, 0.0, slot });
    }
    sendChangeMessage();
}

void DjController::loopAuto (int d, double beats) { deck (d).post ({ DeckCommand::LoopAuto, beats }); }
void DjController::loopIn (int d) { deck (d).post ({ DeckCommand::LoopIn }); }
void DjController::loopOut (int d) { deck (d).post ({ DeckCommand::LoopOut }); }
void DjController::loopExit (int d) { deck (d).post ({ DeckCommand::LoopExit }); }
void DjController::loopHalve (int d) { deck (d).post ({ DeckCommand::LoopHalve }); }
void DjController::loopDouble (int d) { deck (d).post ({ DeckCommand::LoopDouble }); }

void DjController::loopRoll (int d, double beats, bool down)
{
    // momentary loop: on release, jump to where the track would have been (slip)
    static std::array<double, DjCore::kMaxDecks> startPos {}, startTime {};
    if (down)
    {
        startPos[(size_t) d] = deck (d).positionSec.load();
        startTime[(size_t) d] = juce::Time::getMillisecondCounterHiRes() * 0.001;
        rollWasLooping[(size_t) d] = deck (d).loopActive.load();
        deck (d).post ({ DeckCommand::LoopAuto, beats });
    }
    else
    {
        const double elapsed = juce::Time::getMillisecondCounterHiRes() * 0.001 - startTime[(size_t) d];
        deck (d).post ({ DeckCommand::LoopExit });
        deck (d).post ({ DeckCommand::Seek, startPos[(size_t) d] + elapsed * deck (d).tempoRatio.load() });
    }
}

void DjController::scratchStart (int d)
{
    deck (d).scratchRate.store (0.0f);
    deck (d).post ({ DeckCommand::ScratchStart });
}

void DjController::scratchMove (int d, double rate) { deck (d).scratchRate.store ((float) rate); }
void DjController::scratchEnd (int d) { deck (d).post ({ DeckCommand::ScratchEnd }); }

void DjController::doubleBpm (int d)
{
    auto& s = state (d);
    if (s.track == nullptr || s.info.bpm <= 0) return;
    s.info.bpm *= 2.0;
    s.track = s.track->withStems (s.track->stems);
    s.track->grid.bpm = s.info.bpm;
    deck (d).post ({ DeckCommand::ReplaceTrack, 0.0, 0, s.track.get() });
    ctx.library->db().updateTrack (s.info);
    ctx.library->refresh();
    sendChangeMessage();
}

void DjController::halveBpm (int d)
{
    auto& s = state (d);
    if (s.track == nullptr || s.info.bpm <= 0) return;
    s.info.bpm *= 0.5;
    s.track = s.track->withStems (s.track->stems);
    s.track->grid.bpm = s.info.bpm;
    deck (d).post ({ DeckCommand::ReplaceTrack, 0.0, 0, s.track.get() });
    ctx.library->db().updateTrack (s.info);
    ctx.library->refresh();
    sendChangeMessage();
}

void DjController::setGridHere (int d)
{
    auto& s = state (d);
    if (s.track == nullptr || s.info.bpm <= 0) return;
    const double spb = 60.0 / s.info.bpm;
    s.info.firstBeat = std::fmod (deck (d).positionSec.load(), spb * 4.0);
    s.track = s.track->withStems (s.track->stems);
    s.track->grid.firstBeatSec = s.info.firstBeat;
    deck (d).post ({ DeckCommand::ReplaceTrack, 0.0, 0, s.track.get() });
    ctx.library->db().updateTrack (s.info);
    sendChangeMessage();
}

void DjController::setStemGain (int d, int stem, float gain)
{
    if (stem >= 0 && stem < NumStems)
        deck (d).stemGain[(size_t) stem].store (gain);
}

void DjController::attachStems (int d, std::array<SampleBuffer::Ptr, NumStems> stems, bool hq)
{
    auto& s = state (d);
    if (s.track == nullptr) return;
    s.track = s.track->withStems (std::move (stems));
    s.stemProgress = 2.0f;
    s.stemsHQ = hq;
    deck (d).post ({ DeckCommand::ReplaceTrack, 0.0, 0, s.track.get() });
    sendChangeMessage();
}

void DjController::audioConfigChanged (double sampleRate, int)
{
    // tracks are decoded at the device rate: reload decks if it changed
    for (int d = 0; d < DjCore::kMaxDecks; ++d)
    {
        auto& s = state (d);
        if (s.track != nullptr && std::abs (s.track->sampleRate() - sampleRate) > 1.0 && s.info.id >= 0)
            loadTrack (d, s.info.id);
    }
}

void DjController::timerCallback()
{
    for (int d = 0; d < DjCore::kMaxDecks; ++d)
    {
        const bool playing = deck (d).playing.load();
        if (deck (d).trackEnded.exchange (false) && onTrackEnded)
            onTrackEnded (d);
        if (playing != wasPlaying[(size_t) d])
        {
            wasPlaying[(size_t) d] = playing;
            sendChangeMessage();
        }
    }
}

} // namespace merge
