#pragma once

#include "Project.h"

namespace merge
{

struct RenderOptions
{
    double sampleRate = 44100.0;
    int blockSize = 512;
    bool songMode = false;
    int pattern = 0;
    int loops = 1;
    double tailSeconds = 1.5;
};

/** Renders the project (pattern loops or the whole song) to an in-memory stereo buffer.
    Uses an independent copy of every instrument and effect, so it's safe while the live engine plays. */
juce::AudioBuffer<float> renderProject (const Project& project, const RenderOptions& options,
                                        std::function<bool (float)> progress = nullptr);

/** Same, but renders an already-made clone (Project::cloneForRender, done on the message thread)
    so the heavy lifting can happen on a background thread. */
juce::AudioBuffer<float> renderClone (Project& clone, const RenderOptions& options,
                                      std::function<bool (float)> progress = nullptr);

/** Length in beats of what renderProject() will play (without tail). */
double renderLengthBeats (const Project& project, const RenderOptions& options);

} // namespace merge
