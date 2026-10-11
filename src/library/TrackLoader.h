#pragma once

#include "LibraryDb.h"
#include "analysis/Analysis.h"
#include "core/SampleBuffer.h"

namespace merge
{

/** Reads tags (ID3v2 for MP3, RIFF INFO / Vorbis comments via JUCE) with a filename fallback. */
TrackInfo readTrackMetadata (const juce::File& file);

/** Decodes a file to a mono buffer at a reduced rate for analysis (keeps memory low for long mixes). */
bool decodeForAnalysis (const juce::File& file, std::vector<float>& mono, double& sampleRate, double& duration,
                        double targetRate = 22050.0);

/** Full analysis of a file on disk (tempo, key, loudness, waveform). */
std::optional<analysis::TrackAnalysis> analyseFile (const juce::File& file, double minBpm = 70.0, double maxBpm = 180.0);

/** Decodes a whole file to stereo at the given rate (resampling if needed) for deck playback. */
SampleBuffer::Ptr decodeForPlayback (const juce::File& file, double targetRate, std::function<bool()> shouldCancel = {});

} // namespace merge
