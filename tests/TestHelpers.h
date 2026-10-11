#pragma once

#include "engine/Project.h"
#include "engine/OfflineRenderer.h"
#include "engine/BeatCore.h"
#include "core/SampleBuffer.h"

namespace merge::test
{

/** A single full-scale impulse followed by silence. */
inline SampleBuffer::Ptr makeClick (int length = 64, double sr = 44100.0)
{
    juce::AudioBuffer<float> b (1, length);
    b.clear();
    b.setSample (0, 0, 1.0f);
    return SampleBuffer::create (std::move (b), sr, "click");
}

inline SampleBuffer::Ptr makeConstant (float value, int length, double sr = 44100.0)
{
    juce::AudioBuffer<float> b (1, length);
    for (int i = 0; i < length; ++i)
        b.setSample (0, i, value);
    return SampleBuffer::create (std::move (b), sr, "dc");
}

inline SampleBuffer::Ptr makeSine (double freq, double seconds, double sr = 44100.0, float amp = 0.5f)
{
    juce::AudioBuffer<float> b (2, (int) (seconds * sr));
    for (int i = 0; i < b.getNumSamples(); ++i)
    {
        const float v = amp * (float) std::sin (2.0 * juce::MathConstants<double>::pi * freq * i / sr);
        b.setSample (0, i, v);
        b.setSample (1, i, v);
    }
    return SampleBuffer::create (std::move (b), sr, "sine");
}

/** Sample indices where |x| rises above threshold after being below it. */
inline std::vector<int> findOnsets (const juce::AudioBuffer<float>& b, float threshold = 0.5f, int channel = 0)
{
    std::vector<int> onsets;
    bool above = false;
    for (int i = 0; i < b.getNumSamples(); ++i)
    {
        const bool now = std::abs (b.getSample (channel, i)) > threshold;
        if (now && ! above) onsets.push_back (i);
        above = now;
    }
    return onsets;
}

/** A project with pad 1 playing a click (unity gain, centre pan). */
inline std::unique_ptr<Project> makeClickProject (double bpm = 120.0)
{
    auto p = std::make_unique<Project>();
    p->setAudioConfig (44100.0, 512);
    p->bpm = bpm;
    auto& ch = p->getChannels()[0];
    p->setChannelSample (ch.id, makeClick(), {}, 60);
    ch.strip->volume.store (0.8f); // unity
    return p;
}

inline double rms (const juce::AudioBuffer<float>& b, int ch, int start, int num)
{
    double s = 0.0;
    num = juce::jmin (num, b.getNumSamples() - start);
    if (num <= 0) return 0.0;
    for (int i = 0; i < num; ++i)
        s += b.getSample (ch, start + i) * b.getSample (ch, start + i);
    return std::sqrt (s / num);
}

} // namespace merge::test
