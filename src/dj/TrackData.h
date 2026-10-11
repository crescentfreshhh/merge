#pragma once

#include "core/SampleBuffer.h"
#include "engine/Project.h"
#include <array>

namespace merge
{

struct BeatGrid
{
    double bpm = 0.0;          // 0 = unknown
    double firstBeatSec = 0.0; // time of a downbeat
    bool isValid() const noexcept { return bpm > 1.0; }
    double secondsPerBeat() const noexcept { return bpm > 0 ? 60.0 / bpm : 0.5; }
    double beatAt (double seconds) const noexcept { return (seconds - firstBeatSec) / secondsPerBeat(); }
    double timeOfBeat (double beat) const noexcept { return firstBeatSec + beat * secondsPerBeat(); }
    double snap (double seconds) const noexcept { return isValid() ? timeOfBeat (std::round (beatAt (seconds))) : seconds; }
};

enum Stem { StemDrums = 0, StemBass, StemVocals, StemOther, NumStems };
juce::String stemName (int stem);

/** A decoded track ready to play on a deck. Immutable; swapped atomically. */
class TrackData : public juce::ReferenceCountedObject
{
public:
    using Ptr = juce::ReferenceCountedObjectPtr<TrackData>;

    static Ptr create (SampleBuffer::Ptr audio)
    {
        Ptr p (new TrackData());
        p->audio = std::move (audio);
        ReleasePool::get().add (p);
        return p;
    }

    /** Copy with stems attached (keeps everything else). */
    Ptr withStems (std::array<SampleBuffer::Ptr, NumStems> s) const
    {
        Ptr p (new TrackData (*this));
        p->stems = std::move (s);
        ReleasePool::get().add (p);
        return p;
    }

    bool hasStems() const noexcept { return stems[0] != nullptr; }
    double lengthSeconds() const noexcept { return audio != nullptr ? audio->getLengthSeconds() : 0.0; }
    double sampleRate() const noexcept { return audio != nullptr ? audio->getSampleRate() : 44100.0; }

    SampleBuffer::Ptr audio;
    std::array<SampleBuffer::Ptr, NumStems> stems;
    BeatGrid grid;
    MusicalKey key;
    juce::String title, artist;
    juce::int64 trackId = -1;
    float autoGainDb = 0.0f;
    juce::File file;

private:
    TrackData() = default;
    TrackData (const TrackData& other)
        : juce::ReferenceCountedObject(), audio (other.audio), stems (other.stems), grid (other.grid), key (other.key),
          title (other.title), artist (other.artist), trackId (other.trackId), autoGainDb (other.autoGainDb), file (other.file) {}
};

} // namespace merge
