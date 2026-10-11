#include "OfflineRenderer.h"
#include "BeatCore.h"

namespace merge
{

double renderLengthBeats (const Project& project, const RenderOptions& o)
{
    if (o.songMode)
        return project.getSongLengthBeats();
    const auto& pats = project.getPatterns();
    const auto& p = pats[(size_t) juce::jlimit (0, (int) pats.size() - 1, o.pattern)];
    return p.lengthBeats() * juce::jmax (1, o.loops);
}

juce::AudioBuffer<float> renderProject (const Project& project, const RenderOptions& o,
                                        std::function<bool (float)> progress)
{
    auto clone = project.cloneForRender();
    return renderClone (*clone, o, std::move (progress));
}

juce::AudioBuffer<float> renderClone (Project& cloneRef, const RenderOptions& o, std::function<bool (float)> progress)
{
    auto* clone = &cloneRef;
    const auto& project = cloneRef;
    clone->setAudioConfig (o.sampleRate, o.blockSize);

    auto core = std::make_unique<BeatCore>();
    core->prepare (o.sampleRate, o.blockSize);
    core->transport.bpm.store (project.bpm);
    core->transport.swing.store (project.swing);
    core->transport.songMode.store (o.songMode);
    core->transport.currentPattern.store (o.pattern);
    core->transport.loopEnabled.store (false);
    core->transport.masterVolume.store (0.8f); // unity
    core->publish (clone->buildSnapshot());
    core->play();

    const double beats = renderLengthBeats (project, o);
    const auto musicalSamples = (juce::int64) std::llround (beats * 60.0 / project.bpm * o.sampleRate);
    const auto total = musicalSamples + (juce::int64) (o.tailSeconds * o.sampleRate);

    juce::AudioBuffer<float> out (2, (int) total);
    out.clear();
    juce::AudioBuffer<float> block (2, o.blockSize);

    juce::int64 pos = 0;
    while (pos < total)
    {
        int n = (int) juce::jmin<juce::int64> (o.blockSize, total - pos);
        if (pos < musicalSamples)
            n = (int) juce::jmin<juce::int64> (n, musicalSamples - pos);
        else
            core->transport.playing.store (false); // stop sequencing, let tails ring out

        core->process (block, n);
        out.copyFrom (0, (int) pos, block, 0, 0, n);
        out.copyFrom (1, (int) pos, block, 1, 0, n);
        pos += n;

        if (progress && ! progress ((float) pos / (float) total))
            break;
    }
    core->collectGarbage();
    return out;
}

} // namespace merge
