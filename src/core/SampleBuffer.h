#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include "ReleasePool.h"

namespace merge
{

/** Immutable, reference-counted audio data shared between threads. */
class SampleBuffer : public juce::ReferenceCountedObject
{
public:
    using Ptr = juce::ReferenceCountedObjectPtr<SampleBuffer>;

    /** Creates a buffer and registers it with the ReleasePool. */
    static Ptr create (juce::AudioBuffer<float>&& data, double sampleRate,
                       const juce::String& name = {}, const juce::File& file = {});

    const juce::AudioBuffer<float>& getAudio() const noexcept { return audio; }
    double getSampleRate() const noexcept { return sampleRate; }
    int getNumFrames() const noexcept { return audio.getNumSamples(); }
    int getNumChannels() const noexcept { return audio.getNumChannels(); }
    double getLengthSeconds() const noexcept { return sampleRate > 0 ? audio.getNumSamples() / sampleRate : 0.0; }
    const juce::String& getName() const noexcept { return name; }
    const juce::File& getFile() const noexcept { return file; }

    /** Reads one channel with cubic (Hermite) interpolation. Channel index is
        clamped, so a mono buffer feeds both sides of a stereo voice. */
    inline float readCubic (int channel, double position) const noexcept
    {
        const int n = audio.getNumSamples();
        if (n == 0) return 0.0f;
        const float* d = audio.getReadPointer (juce::jmin (channel, audio.getNumChannels() - 1));
        const int i = (int) std::floor (position);
        const float f = (float) (position - i);
        auto at = [d, n] (int k) { return (k >= 0 && k < n) ? d[k] : 0.0f; };
        const float xm1 = at (i - 1), x0 = at (i), x1 = at (i + 1), x2 = at (i + 2);
        const float c1 = 0.5f * (x1 - xm1);
        const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
        const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
        return ((c3 * f + c2) * f + c1) * f + x0;
    }

    inline float readLinear (int channel, double position) const noexcept
    {
        const int n = audio.getNumSamples();
        if (n == 0) return 0.0f;
        const float* d = audio.getReadPointer (juce::jmin (channel, audio.getNumChannels() - 1));
        const int i = (int) std::floor (position);
        if (i < 0 || i >= n) return 0.0f;
        const float f = (float) (position - i);
        const float b = (i + 1 < n) ? d[i + 1] : 0.0f;
        return d[i] + f * (b - d[i]);
    }

private:
    SampleBuffer (juce::AudioBuffer<float>&& data, double sr, const juce::String& n, const juce::File& f)
        : audio (std::move (data)), sampleRate (sr), name (n), file (f) {}

    juce::AudioBuffer<float> audio;
    double sampleRate = 44100.0;
    juce::String name;
    juce::File file;
};

/** Shared audio format manager with every format Merge can decode. */
juce::AudioFormatManager& audioFormats();

/** Decodes a whole file into memory. Returns nullptr on failure. */
SampleBuffer::Ptr loadSampleFile (const juce::File& file, double maxSeconds = 0.0);

/** Process-wide cache so the same file is only decoded once. */
class SampleCache
{
public:
    static SampleCache& get();
    SampleBuffer::Ptr load (const juce::File& file);
    void clear();

private:
    juce::CriticalSection lock;
    std::map<juce::String, SampleBuffer::Ptr> cache;
};

/** Writes a buffer as 24-bit WAV (or FLAC/OGG based on extension). */
bool writeAudioFile (const juce::File& file, const juce::AudioBuffer<float>& audio, double sampleRate,
                     int bitDepth = 24, int numSamples = -1);

} // namespace merge
