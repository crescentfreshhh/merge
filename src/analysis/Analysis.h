#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include "engine/Project.h"

namespace merge::analysis
{

struct TempoResult
{
    double bpm = 0.0;
    double firstBeatSec = 0.0; // position of a downbeat
    float confidence = 0.0f;
};

struct KeyResult
{
    MusicalKey key;
    float confidence = 0.0f;
};

/** One point of the colour (RGB) waveform: band peaks scaled to 0..255. */
struct WavePoint
{
    juce::uint8 low = 0, mid = 0, high = 0, peak = 0;
};

struct Waveform
{
    double pointsPerSecond = 0.0;
    std::vector<WavePoint> points;

    juce::MemoryBlock toBlob() const;
    static Waveform fromBlob (const juce::MemoryBlock&);
};

struct TrackAnalysis
{
    TempoResult tempo;
    KeyResult key;
    float lufs = -100.0f;
    double durationSec = 0.0;
    Waveform waveform;
};

/** Mono mix of a buffer (averages channels). */
std::vector<float> toMono (const juce::AudioBuffer<float>& audio);

/** Onset-strength envelope (log spectral flux). hopSeconds receives the frame period. */
std::vector<float> onsetEnvelope (const std::vector<float>& mono, double sampleRate, int fftOrder, int hop,
                                  double& hopSeconds, double maxHz = 8000.0, double minHz = 0.0);

/** Tempo + beat grid for (mostly) constant-tempo music. BPM is folded into [minBpm, maxBpm). */
TempoResult detectTempo (const std::vector<float>& mono, double sampleRate, double minBpm = 70.0, double maxBpm = 180.0);

/** Musical key using a chromagram and Krumhansl-Kessler profiles. */
KeyResult detectKey (const std::vector<float>& mono, double sampleRate);

/** Integrated loudness (ITU-R BS.1770 / EBU R128, gated). */
float integratedLoudness (const juce::AudioBuffer<float>& audio, double sampleRate);

/** Low / mid / high band peaks for colour waveforms. */
Waveform computeWaveform (const juce::AudioBuffer<float>& audio, double sampleRate, double pointsPerSecond = 150.0);

/** Transient (hit) positions in samples, for chopping samples onto pads. */
std::vector<int> detectTransients (const std::vector<float>& mono, double sampleRate, float sensitivity = 0.5f,
                                   double minSpacingSec = 0.06);

/** Runs everything a DJ library needs. BPM is folded into [minBpm, maxBpm) (e.g. 88-175 for drum & bass DJs). */
TrackAnalysis analyseTrack (const juce::AudioBuffer<float>& audio, double sampleRate, double minBpm = 70.0, double maxBpm = 180.0);

/** Camelot compatibility: same key, +-1 on the wheel, or relative major/minor. */
bool keysCompatible (const MusicalKey& a, const MusicalKey& b);

} // namespace merge::analysis
