#include "StemService.h"
#include "AppContext.h"
#include "DjController.h"
#include "LibraryService.h"
#include "core/Paths.h"
#include "stems/QuickStems.h"
#include "library/TrackLoader.h"
#include "ui/common/Widgets.h"

namespace merge
{

static const char* stemFileNames[] { "drums", "bass", "vocals", "melody" };

StemService::StemService (AppContext& c) : ctx (c)
{
    ctx.stemsRequest = [this] (int deck) { separateDeck (deck); };
    ctx.flipRequest = [this] (int deck) { flip (deck); };
    ctx.stemsForTrack = [this] (juce::int64 id) { separateTrack (id); };
    startTimerHz (8);
}

StemService::~StemService()
{
    alive->store (false);
    stopTimer();
    pool.removeAllJobs (true, 20000);
}

juce::File StemService::cacheFolderFor (const juce::File& track)
{
    const auto key = track.getFullPathName() + "|" + juce::String (track.getSize()) + "|"
                     + juce::String (track.getLastModificationTime().toMilliseconds());
    return paths::cacheDir().getChildFile ("stems").getChildFile (juce::String::toHexString (key.hashCode64()));
}

void StemService::attachCached (int deck)
{
    auto& s = ctx.decks->state (deck);
    if (s.track == nullptr) return;
    auto folder = cacheFolderFor (s.info.file);
    if (! folder.getChildFile ("melody.flac").existsAsFile()) return;
    auto track = s.track;
    auto file = s.info.file;
    const double sr = track->sampleRate();
    auto aliveFlag = alive;
    pool.addJob ([this, aliveFlag, deck, track, folder, sr]
    {
        std::array<SampleBuffer::Ptr, NumStems> stems;
        for (int k = 0; k < NumStems; ++k)
        {
            stems[(size_t) k] = decodeForPlayback (folder.getChildFile (juce::String (stemFileNames[k]) + ".flac"), sr);
            if (stems[(size_t) k] == nullptr) return;
        }
        juce::MessageManager::callAsync ([this, aliveFlag, deck, track, stems]
        {
            if (! aliveFlag->load()) return;
            if (ctx.decks->state (deck).track.get() != track.get()) return; // a different track was loaded meanwhile
            ctx.decks->attachStems (deck, stems, false);
        });
    });
}

void StemService::separateDeck (int deck)
{
    auto& s = ctx.decks->state (deck);
    if (s.track == nullptr || s.track->hasStems()) return;
    if (s.stemProgress >= 0.0f && s.stemProgress < 1.5f) return; // already running
    startJob (deck, s.track, s.info.file);
}

void StemService::startJob (int deck, TrackData::Ptr track, juce::File file)
{
    auto& p = progress[(size_t) deck];
    const int gen = ++p.generation;
    p.value.store (0.0f);
    ctx.decks->state (deck).stemProgress = 0.0f;
    ctx.setStatus ("Splitting stems for " + track->title + "...");
    auto aliveFlag = alive;
    auto* prog = &p;
    pool.addJob ([this, aliveFlag, deck, gen, track, file, prog]
    {
        stems::Result result;
        const bool ok = stems::separate (track->audio->getAudio(), track->sampleRate(), result, [aliveFlag, prog, gen] (float v)
        {
            prog->value.store (v);
            return aliveFlag->load() && prog->generation.load() == gen;
        });
        if (! ok || ! aliveFlag->load()) return;

        std::array<SampleBuffer::Ptr, NumStems> buffers;
        for (int k = 0; k < NumStems; ++k)
            buffers[(size_t) k] = SampleBuffer::create (std::move (result.stems[(size_t) k]), track->sampleRate(), stemName (k));

        juce::MessageManager::callAsync ([this, aliveFlag, deck, track, buffers]
        {
            if (! aliveFlag->load()) return;
            progress[(size_t) deck].value.store (-1.0f);
            if (ctx.decks->state (deck).track.get() == track.get())
            {
                ctx.decks->attachStems (deck, buffers, false);
                ctx.setStatus ("Stems ready on deck " + juce::String::charToString ((juce::juce_wchar) ('A' + deck))
                               + " - use the STEMS pads to mute drums, bass, vocals or melody.");
            }
        });

        // cache to disk for next time (FLAC keeps it small)
        auto folder = cacheFolderFor (file);
        folder.createDirectory();
        for (int k = 0; k < NumStems; ++k)
            writeAudioFile (folder.getChildFile (juce::String (stemFileNames[k]) + ".flac"), buffers[(size_t) k]->getAudio(),
                            buffers[(size_t) k]->getSampleRate(), 24);
    });
}

void StemService::separateTrack (juce::int64 trackId)
{
    auto* t = ctx.library->find (trackId);
    if (t == nullptr) return;
    const auto file = t->file;
    if (cacheFolderFor (file).getChildFile ("melody.flac").existsAsFile())
    {
        ctx.setStatus ("Stems for " + t->title + " are already cached.");
        return;
    }
    const double sr = ctx.engine.getSampleRate();
    const auto title = t->title;
    auto aliveFlag = alive;
    ctx.setStatus ("Splitting stems for " + title + " in the background...");
    pool.addJob ([this, aliveFlag, file, sr, title]
    {
        auto audio = decodeForPlayback (file, sr);
        if (audio == nullptr) return;
        stems::Result result;
        if (! stems::separate (audio->getAudio(), sr, result, [aliveFlag] (float) { return aliveFlag->load(); })) return;
        auto folder = cacheFolderFor (file);
        folder.createDirectory();
        for (int k = 0; k < NumStems; ++k)
            writeAudioFile (folder.getChildFile (juce::String (stemFileNames[k]) + ".flac"), result.stems[(size_t) k], sr, 24);
        juce::MessageManager::callAsync ([this, aliveFlag, title]
        {
            if (aliveFlag->load()) ctx.setStatus ("Stems for " + title + " are ready (cached).");
        });
    });
}

void StemService::timerCallback()
{
    bool changed = false;
    for (int d = 0; d < 4; ++d)
    {
        const float v = progress[(size_t) d].value.load();
        auto& s = ctx.decks->state (d);
        if (v >= 0.0f && std::abs (s.stemProgress - v) > 0.01f)
        {
            s.stemProgress = v;
            changed = true;
        }
    }
    if (changed) ctx.decks->sendChangeMessage();
}

void StemService::flip (int deck)
{
    auto& s = ctx.decks->state (deck);
    if (s.track == nullptr)
    {
        ctx.setStatus ("Load a track on the deck first, then Flip chops it onto the pads.");
        return;
    }
    juce::PopupMenu m;
    m.addSectionHeader ("Flip onto the pads (16 slices)");
    m.addItem (1, "Full mix");
    const bool hasStems = s.track->hasStems();
    for (int k = 0; k < NumStems; ++k)
        m.addItem (10 + k, stemName (k) + (hasStems ? juce::String() : juce::String (" (splits stems first)")));
    showMenu (m, nullptr, [this, deck, hasStems] (int r)
    {
        if (r == 1) flipWith (deck, -1);
        else if (r >= 10)
        {
            if (hasStems) flipWith (deck, r - 10);
            else
            {
                separateDeck (deck);
                ctx.setStatus ("Splitting stems first - press FLIP again when they're ready.");
            }
        }
    });
}

void StemService::flipWith (int deck, int source)
{
    auto& s = ctx.decks->state (deck);
    auto track = s.track;
    if (track == nullptr) return;
    auto buffer = source >= 0 && track->hasStems() ? track->stems[(size_t) source] : track->audio;
    if (buffer == nullptr) return;
    const double sr = buffer->getSampleRate();
    const auto& grid = track->grid;
    const double bpm = grid.isValid() ? grid.bpm : 120.0;
    const double spb = 60.0 / bpm;

    // region: the active loop, or 4 bars from the start of the current bar
    auto& dk = ctx.decks->deck (deck);
    double start, length;
    if (dk.loopActive.load() && dk.loopOutSec.load() > dk.loopInSec.load())
    {
        start = dk.loopInSec.load();
        length = dk.loopOutSec.load() - start;
    }
    else
    {
        const double pos = dk.positionSec.load();
        start = grid.isValid() ? grid.timeOfBeat (std::floor (grid.beatAt (pos) / 4.0) * 4.0) : pos;
        start = juce::jmax (0.0, start);
        length = 16.0 * spb;
    }
    const int startSample = (int) (start * sr);
    const int total = juce::jmin ((int) (length * sr), buffer->getNumFrames() - startSample);
    if (total <= kNumPads) return;

    const auto folder = paths::userSamplesDir().getChildFile ("Flips")
                            .getChildFile (juce::File::createLegalFileName (track->title + " " + (source >= 0 ? stemName (source) : juce::String ("Mix"))
                                                                            + " " + juce::Time::getCurrentTime().formatted ("%H%M%S")));
    folder.createDirectory();
    const int sliceLen = total / kNumPads;
    auto& chans = ctx.project.getChannels();
    ctx.project.checkpoint();
    for (int i = 0; i < kNumPads && i < (int) chans.size(); ++i)
    {
        juce::AudioBuffer<float> slice (2, sliceLen);
        for (int ch = 0; ch < 2; ++ch)
            slice.copyFrom (ch, 0, buffer->getAudio(), juce::jmin (ch, buffer->getNumChannels() - 1), startSample + i * sliceLen, sliceLen);
        // tiny fades so slices don't click
        const int fade = juce::jmin (64, sliceLen / 4);
        slice.applyGainRamp (0, fade, 0.0f, 1.0f);
        slice.applyGainRamp (sliceLen - fade, fade, 1.0f, 0.0f);
        const auto file = folder.getChildFile (juce::String (i + 1).paddedLeft ('0', 2) + " Slice.wav");
        writeAudioFile (file, slice, sr, 24);
        auto& ch = chans[(size_t) i];
        if (ch.kind != ChannelKind::Sampler) continue;
        ctx.project.setChannelSample (ch.id, SampleCache::get().load (file), paths::toRef (file), 60);
        ch.name = "Slice " + juce::String (i + 1);
        ch.strip->chokeGroup.store (0);
    }

    // a pattern that replays the loop in order, ready to rearrange
    int target = 0;
    for (int p = 0; p < kNumPatterns; ++p)
        if (ctx.project.getPatterns()[(size_t) p].isEmpty()) { target = p; break; }
    const int stepsPerSlice = juce::jmax (1, (int) std::round (length / spb * 4.0 / kNumPads));
    ctx.project.clearPattern (target);
    ctx.project.setPatternLength (target, juce::jlimit (16, 64, stepsPerSlice * kNumPads));
    for (int i = 0; i < kNumPads; ++i)
        ctx.project.setStep (target, chans[(size_t) i].id, i * stepsPerSlice, 100);
    ctx.project.bpm = bpm;
    ctx.project.swing = 50.0;
    ctx.project.kitName = "Flip: " + track->title;
    ctx.project.key = track->key;
    ctx.syncTransportFromProject();
    ctx.selectPattern (target);
    ctx.projectEdited();
    ctx.setStatus ("Flipped " + (source >= 0 ? stemName (source) : juce::String ("the mix")) + " of " + track->title + " onto the pads ("
                   + juce::String (bpm, 1) + " BPM, pattern " + Project::patternName (target) + "). Switch to MAKE to play it.");
}

} // namespace merge
