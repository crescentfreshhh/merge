#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include "core/Params.h"
#include "core/SampleBuffer.h"

namespace merge
{

/** A sound source on a channel: sampler, synth or hosted plugin.
    render() is called on the audio thread with sample-accurate MIDI. */
class Instrument : public juce::ReferenceCountedObject
{
public:
    using Ptr = juce::ReferenceCountedObjectPtr<Instrument>;

    virtual ~Instrument() = default;

    virtual juce::String getTypeId() const = 0;

    /** Message thread (or before audio starts). */
    virtual void prepare (double sampleRate, int maxBlockSize) = 0;

    /** Audio thread. Adds into a stereo buffer. */
    virtual void render (juce::AudioBuffer<float>& out, const juce::MidiBuffer& midi, int numSamples) = 0;

    /** Audio thread: silence everything immediately (transport stop, panic). */
    virtual void allNotesOff() = 0;

    /** Message thread: a fresh, independent copy for offline rendering. */
    virtual Ptr clone() const = 0;

    virtual juce::var saveState() const { return params.toVar(); }
    virtual void loadState (const juce::var& v) { params.fromVar (v); }

    /** True when note-offs don't matter (one-shot drum sampler). Audio thread safe. */
    virtual bool isOneShot() const noexcept { return false; }

    /** Optional sample (sampler instruments). */
    virtual SampleBuffer::Ptr getSample() const { return nullptr; }
    virtual void setSample (SampleBuffer::Ptr) {}

    ParamSet params;
    double preparedRate = 0.0;
};

/** Helper base for polyphonic instruments that splits the block at MIDI events. */
template <typename Derived>
class EventSplittingInstrument : public Instrument
{
public:
    void render (juce::AudioBuffer<float>& out, const juce::MidiBuffer& midi, int numSamples) override
    {
        auto& self = static_cast<Derived&> (*this);
        int pos = 0;
        for (const auto meta : midi)
        {
            const int t = juce::jlimit (0, numSamples, meta.samplePosition);
            if (t > pos)
            {
                self.renderVoices (out, pos, t - pos);
                pos = t;
            }
            self.handleMidi (meta.getMessage());
        }
        if (pos < numSamples)
            self.renderVoices (out, pos, numSamples - pos);
    }
};

} // namespace merge
