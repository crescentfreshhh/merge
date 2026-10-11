#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include "core/Params.h"
#include "core/ReleasePool.h"

namespace merge
{

struct EffectContext
{
    double bpm = 120.0;
    bool playing = false;
};

/** An insert effect (built-in DSP or hosted plugin). Runs on the audio thread. */
class Effect : public juce::ReferenceCountedObject
{
public:
    using Ptr = juce::ReferenceCountedObjectPtr<Effect>;

    virtual ~Effect() = default;
    virtual juce::String getTypeId() const = 0;
    virtual juce::String getName() const = 0;
    virtual void prepare (double sampleRate, int maxBlockSize) = 0;
    virtual void process (juce::AudioBuffer<float>& buffer, int numSamples, const EffectContext& ctx) = 0;
    virtual void reset() {}
    virtual Ptr clone() const = 0;
    virtual juce::var saveState() const { return params.toVar(); }
    virtual void loadState (const juce::var& v) { params.fromVar (v); }

    ParamSet params;
    std::atomic<bool> bypassed { false };
    double preparedRate = 0.0;
};

/** Built-in effect type ids, in menu order. */
juce::StringArray builtInEffectTypes();
juce::String builtInEffectName (const juce::String& typeId);
Effect::Ptr createBuiltInEffect (const juce::String& typeId);

} // namespace merge
