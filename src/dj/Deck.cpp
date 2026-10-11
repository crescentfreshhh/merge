#include "Deck.h"

namespace merge
{

juce::String stemName (int stem)
{
    switch (stem)
    {
        case StemDrums:  return "Drums";
        case StemBass:   return "Bass";
        case StemVocals: return "Vocals";
        case StemOther:  return "Melody";
        default:         return {};
    }
}

Deck::Deck (int index) : deckIndex (index)
{
    for (auto& g : stemGain)
        g.store (1.0f);
    stemGainNow.fill (1.0f);
    hotCues.fill (-1.0);
}

void Deck::prepare (double sr, int mb)
{
    sampleRate = sr;
    maxBlock = mb;
    // ~60 ms blocks: lower latency than the library default, still clean on full mixes
    const int block = juce::roundToInt (sr * 0.06);
    stretch.configure (2, block, block / 4);
    stretchIn.setSize (2, mb * 4 + 64);
    stretchOut.setSize (2, mb);
    seekBuf.setSize (2, stretch.seekLength() + 64);
    needsSeek = true;
}

double Deck::wrapLoop (double p) const noexcept
{
    if (loopOn && loopOut > loopIn + 1.0 && p >= loopOut)
        return loopIn + std::fmod (p - loopIn, loopOut - loopIn);
    return p;
}

inline void Deck::readFrame (double p, float& l, float& r) const noexcept
{
    l = r = 0.0f;
    if (track == nullptr || p < 0.0)
        return;
    if (track->hasStems())
    {
        for (int k = 0; k < NumStems; ++k)
        {
            const float g = stemGainNow[(size_t) k];
            if (g <= 0.0001f) continue;
            const auto* s = track->stems[(size_t) k].get();
            const float sl = s->readCubic (0, p);
            l += sl * g;
            r += (s->getNumChannels() > 1 ? s->readCubic (1, p) : sl) * g;
        }
    }
    else if (track->audio != nullptr)
    {
        const auto* a = track->audio.get();
        l = a->readCubic (0, p);
        r = a->getNumChannels() > 1 ? a->readCubic (1, p) : l;
    }
}

double Deck::currentAudibleSample() const noexcept
{
    if (wasStretching)
        return pos - (stretch.inputLatency() + stretch.outputLatency() * ratioNow);
    return pos;
}

double Deck::audibleBeat() const noexcept
{
    if (track == nullptr) return 0.0;
    return track->grid.beatAt (currentAudibleSample() / sampleRate);
}

double Deck::quantizeSec (double sec) const
{
    if (quantize.load() && track != nullptr && track->grid.isValid())
        return track->grid.snap (sec);
    return sec;
}

void Deck::seekTo (double sourceSample, bool)
{
    const double len = track != nullptr && track->audio != nullptr ? (double) track->audio->getNumFrames() : 0.0;
    pos = juce::jlimit (0.0, juce::jmax (0.0, len - 1.0), sourceSample);
    needsSeek = true;
    trackEnded.store (false);
}

void Deck::alignToClock (const MasterClock& clock)
{
    if (! clock.valid() || clock.masterDeck == deckIndex || ! hasGrid())
        return;
    const double mine = audibleBeat();
    double diff = (clock.beat - std::floor (clock.beat)) - (mine - std::floor (mine));
    if (diff > 0.5) diff -= 1.0;
    if (diff < -0.5) diff += 1.0;
    seekTo (pos + diff * track->grid.secondsPerBeat() * sampleRate);
}

void Deck::handle (const DeckCommand& c, const MasterClock& clock)
{
    const double spbSamples = (track != nullptr ? track->grid.secondsPerBeat() : 0.5) * sampleRate;
    switch (c.type)
    {
        case DeckCommand::Load:
            track = TrackData::Ptr (c.track);
            isPlayingNow = false;
            cuePreview = hotCuePreview = false;
            loopOn = false;
            loopIn = loopOut = -1.0;
            hotCues.fill (-1.0);
            cuePoint = (track != nullptr && track->grid.isValid() && track->grid.firstBeatSec >= 0.0 && track->grid.firstBeatSec < 30.0)
                           ? track->grid.firstBeatSec * sampleRate : 0.0;
            seekTo (cuePoint);
            break;
        case DeckCommand::Eject:
            track = nullptr;
            isPlayingNow = false;
            pos = 0.0;
            break;
        case DeckCommand::ReplaceTrack:
            if (c.track != nullptr)
                track = TrackData::Ptr (c.track);
            break;
        case DeckCommand::Play:
            if (track == nullptr) break;
            if (! isPlayingNow && sync.load())
                alignToClock (clock);
            isPlayingNow = true;
            cuePreview = false;
            break;
        case DeckCommand::Pause:
            isPlayingNow = false;
            cuePreview = false;
            break;
        case DeckCommand::TogglePlay:
            if (track == nullptr) break;
            if (! isPlayingNow && sync.load())
                alignToClock (clock);
            isPlayingNow = ! isPlayingNow || cuePreview;
            cuePreview = false;
            break;
        case DeckCommand::CueDown:
            if (track == nullptr) break;
            if (isPlayingNow && ! cuePreview)
            {
                isPlayingNow = false;
                seekTo (cuePoint);
            }
            else
            {
                cuePoint = quantizeSec (pos / sampleRate) * sampleRate;
                seekTo (cuePoint);
                isPlayingNow = true;
                cuePreview = true;
            }
            break;
        case DeckCommand::CueUp:
            if (cuePreview)
            {
                cuePreview = false;
                isPlayingNow = false;
                seekTo (cuePoint);
            }
            break;
        case DeckCommand::Seek:
            seekTo (c.value * sampleRate);
            if (loopOn && (pos < loopIn || pos >= loopOut)) loopOn = false;
            break;
        case DeckCommand::SeekBeats:
            seekTo (pos + c.value * spbSamples);
            break;
        case DeckCommand::SetHotCue:
            if (c.index >= 0 && c.index < (int) hotCues.size())
                hotCues[(size_t) c.index] = c.value * sampleRate;
            break;
        case DeckCommand::ClearHotCue:
            if (c.index >= 0 && c.index < (int) hotCues.size())
                hotCues[(size_t) c.index] = -1.0;
            break;
        case DeckCommand::HotCue:
            if (track != nullptr && c.index >= 0 && c.index < (int) hotCues.size() && hotCues[(size_t) c.index] >= 0.0)
            {
                const double target = hotCues[(size_t) c.index];
                if (loopOn && (target < loopIn || target >= loopOut)) loopOn = false;
                seekTo (target);
                if (! isPlayingNow)
                {
                    isPlayingNow = true;
                    if (sync.load()) alignToClock (clock);
                }
            }
            break;
        case DeckCommand::LoopAuto:
        {
            if (track == nullptr) break;
            const double beats = c.value > 0 ? c.value : loopBeatsDefault.load();
            const double len = beats * spbSamples;
            if (loopOn && std::abs ((loopOut - loopIn) - len) < 1.0)
            {
                loopOn = false;
                break;
            }
            double start = currentAudibleSample();
            if (quantize.load() && hasGrid())
            {
                const double beat = track->grid.beatAt (start / sampleRate);
                start = track->grid.timeOfBeat (std::floor (beat + 1.0e-6)) * sampleRate; // loop from the current beat
            }
            if (loopOn && loopIn >= 0.0)
                start = loopIn; // resizing an active loop keeps its start
            loopIn = start;
            loopOut = start + len;
            loopOn = true;
            if (pos >= loopOut) pos = wrapLoop (pos);
            break;
        }
        case DeckCommand::LoopIn:
            loopIn = quantizeSec (currentAudibleSample() / sampleRate) * sampleRate;
            loopOut = -1.0;
            loopOn = false;
            break;
        case DeckCommand::LoopOut:
            if (loopIn >= 0.0)
            {
                double out = quantizeSec (currentAudibleSample() / sampleRate) * sampleRate;
                if (out <= loopIn + 64.0) out = loopIn + spbSamples;
                loopOut = out;
                loopOn = true;
                if (pos >= loopOut) seekTo (loopIn);
            }
            break;
        case DeckCommand::LoopExit:
            loopOn = false;
            break;
        case DeckCommand::LoopToggle:
            if (loopIn >= 0.0 && loopOut > loopIn)
            {
                loopOn = ! loopOn;
                if (loopOn && (pos < loopIn || pos >= loopOut)) seekTo (loopIn);
            }
            break;
        case DeckCommand::LoopHalve:
        case DeckCommand::LoopDouble:
            if (loopIn >= 0.0 && loopOut > loopIn)
            {
                const double len = (loopOut - loopIn) * (c.type == DeckCommand::LoopHalve ? 0.5 : 2.0);
                if (len >= 32.0)
                {
                    loopOut = loopIn + len;
                    if (loopOn && pos >= loopOut) pos = wrapLoop (pos);
                }
            }
            break;
        case DeckCommand::BeatJump:
        {
            const double delta = c.value * spbSamples;
            seekTo (pos + delta);
            if (loopOn) { loopIn += delta; loopOut += delta; }
            break;
        }
        case DeckCommand::ScratchStart:
            scratching = true;
            scratchSmoothed = isPlayingNow ? ratioNow : 0.0;
            break;
        case DeckCommand::ScratchEnd:
            scratching = false;
            needsSeek = true;
            break;
        case DeckCommand::AlignPhase:
            alignToClock (clock);
            break;
    }
}

void Deck::process (juce::AudioBuffer<float>& out, int n, const MasterClock& clock)
{
    out.clear (0, n);
    DeckCommand c;
    while (commands.pop (c))
        handle (c, clock);

    if (track == nullptr || track->audio == nullptr)
    {
        positionSec.store (0.0);
        playing.store (false);
        effectiveBpm.store (0.0);
        peak.store (0.0f);
        return;
    }

    for (int k = 0; k < NumStems; ++k)
        stemGainNow[(size_t) k] = juce::jlimit (0.0f, 1.5f, stemGain[(size_t) k].load (std::memory_order_relaxed));

    const double length = (double) track->audio->getNumFrames();
    const bool synced = sync.load() && clock.valid() && clock.masterDeck != deckIndex && hasGrid();
    double baseRatio = 1.0 + (double) pitch.load() * (double) pitchRange.load();
    if (synced)
        baseRatio = clock.bpm / track->grid.bpm;
    baseRatio = juce::jlimit (0.25, 4.0, baseRatio);

    double ratio = baseRatio;
    if (synced && isPlayingNow && ! scratching)
    {
        // gentle phase lock towards the master's beat phase
        const double mine = audibleBeat();
        double err = (clock.beat - std::floor (clock.beat)) - (mine - std::floor (mine));
        if (err > 0.5) err -= 1.0;
        if (err < -0.5) err += 1.0;
        const double errSamples = err * track->grid.secondsPerBeat() * sampleRate;
        if (std::abs (errSamples) > sampleRate * 0.25) // way off (e.g. after a seek): jump
            alignToClock (clock);
        else
            ratio += juce::jlimit (-0.03, 0.03, errSamples * 0.15 / juce::jmax (1, n)) ;
    }
    ratio *= 1.0 + (double) nudge.load();
    ratioNow = baseRatio;
    tempoRatio.store (baseRatio);
    effectiveBpm.store (hasGrid() ? track->grid.bpm * baseRatio : 0.0);

    float* L = out.getWritePointer (0);
    float* R = out.getWritePointer (1);

    if (scratching)
    {
        const double target = scratchRate.load();
        const double start = scratchSmoothed;
        scratchSmoothed += (target - scratchSmoothed) * 0.35;
        for (int i = 0; i < n; ++i)
        {
            const double rate = start + (scratchSmoothed - start) * (i / (double) n);
            readFrame (pos, L[i], R[i]);
            pos = juce::jlimit (0.0, length - 1.0, pos + rate);
        }
        wasStretching = false;
        needsSeek = true;
    }
    else if (! isPlayingNow)
    {
        needsSeek = true;
    }
    else if (keylock.load())
    {
        if (needsSeek || ! wasStretching)
        {
            const int seekLen = juce::jmin (stretch.seekLength(), seekBuf.getNumSamples());
            for (int i = 0; i < seekLen; ++i)
            {
                float l, r;
                readFrame (pos - seekLen + i, l, r);
                seekBuf.setSample (0, i, l);
                seekBuf.setSample (1, i, r);
            }
            stretch.seek (seekBuf.getArrayOfReadPointers(), seekLen, ratio);
            needsSeek = false;
            inputFrac = 0.0;
        }
        inputFrac += n * ratio;
        int m = (int) std::floor (inputFrac);
        inputFrac -= m;
        m = juce::jlimit (0, stretchIn.getNumSamples(), m);
        float* il = stretchIn.getWritePointer (0);
        float* ir = stretchIn.getWritePointer (1);
        for (int i = 0; i < m; ++i)
        {
            readFrame (pos, il[i], ir[i]);
            pos = wrapLoop (pos + 1.0);
        }
        stretch.process (stretchIn.getArrayOfReadPointers(), m, out.getArrayOfWritePointers(), n);
        wasStretching = true;
    }
    else
    {
        for (int i = 0; i < n; ++i)
        {
            readFrame (pos, L[i], R[i]);
            pos = wrapLoop (pos + ratio);
        }
        wasStretching = false;
        needsSeek = true;
    }

    if (pos >= length - 1.0 && isPlayingNow && ! scratching)
    {
        isPlayingNow = false;
        trackEnded.store (true);
    }

    positionSec.store (juce::jmax (0.0, currentAudibleSample()) / sampleRate);
    playing.store (isPlayingNow);
    loopActive.store (loopOn);
    loopInSec.store (loopIn >= 0 ? loopIn / sampleRate : -1.0);
    loopOutSec.store (loopOut >= 0 ? loopOut / sampleRate : -1.0);
    cueSec.store (cuePoint / sampleRate);
    peak.store (juce::jmax (peak.load (std::memory_order_relaxed), out.getMagnitude (0, n)));
}

} // namespace merge
