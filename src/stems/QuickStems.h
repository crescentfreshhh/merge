#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <functional>

namespace merge::stems
{

/** Fast DSP stem separation (no AI model needed):
    harmonic/percussive separation (median filtering) + band split + centre extraction.
    Drums = percussive part, Bass = harmonic lows, Vocals = centred harmonic mids, Melody = the rest.
    The four masks always sum to one, so the stems add back up to the original exactly. */
struct Result
{
    std::array<juce::AudioBuffer<float>, 4> stems; // drums, bass, vocals, melody (stereo)
};

bool separate (const juce::AudioBuffer<float>& input, double sampleRate, Result& out,
               std::function<bool (float progress)> progress = nullptr);

} // namespace merge::stems
