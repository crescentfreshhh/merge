#pragma once

#include "Instrument.h"
#include "core/Dsp.h"
#include <array>

namespace merge
{

/** Sample player used for drum pads (one-shot) and the chromatic Keys channel (gated). */
class SamplerInstrument : public EventSplittingInstrument<SamplerInstrument>
{
public:
    SamplerInstrument();

    juce::String getTypeId() const override { return "sampler"; }
    void prepare (double sampleRate, int maxBlockSize) override;
    void allNotesOff() override;
    Ptr clone() const override;

    bool isOneShot() const noexcept override { return ! gate.getBool(); }
    SampleBuffer::Ptr getSample() const override { return uiSample; }
    void setSample (SampleBuffer::Ptr s) override;

    void setRootNote (int n) noexcept { rootNote.store (n); }
    int getRootNote() const noexcept { return rootNote.load(); }

    juce::var saveState() const override;
    void loadState (const juce::var& v) override;

    void renderVoices (juce::AudioBuffer<float>& out, int start, int num);
    void handleMidi (const juce::MidiMessage& m);

    Param& tune;
    Param& fine;
    Param& attack;
    Param& release;
    Param& gate;
    Param& reverse;
    Param& startPos;
    Param& endPos;
    Param& velocitySens;
    Param& mono;
    Param& glide;

private:
    struct Voice
    {
        SampleBuffer::Ptr buffer;
        bool active = false;
        int note = 0;
        double pos = 0, inc = 1, targetInc = 1, glideCoef = 0;
        double endPos = 0, startPos = 0;
        bool reverse = false;
        float gain = 1;
        float env = 0, envInc = 0;
        int stage = 0; // 0 attack, 1 sustain, 2 release
        float releaseDec = 0;
        juce::uint32 age = 0;
    };

    void startVoice (int note, float velocity);
    void stopNote (int note);
    void choke();

    std::atomic<SampleBuffer*> sample { nullptr };
    SampleBuffer::Ptr uiSample;
    std::atomic<int> rootNote { 60 };
    double sampleRate = 44100.0;
    std::array<Voice, 24> voices;
    juce::uint32 voiceCounter = 0;
};

/** Classic 808-style sub bass: sine with pitch punch, glide and drive. Monophonic. */
class Bass808Instrument : public EventSplittingInstrument<Bass808Instrument>
{
public:
    Bass808Instrument();
    juce::String getTypeId() const override { return "bass808"; }
    void prepare (double sampleRate, int maxBlockSize) override;
    void allNotesOff() override;
    Ptr clone() const override;

    void renderVoices (juce::AudioBuffer<float>& out, int start, int num);
    void handleMidi (const juce::MidiMessage& m);

    Param& decay;
    Param& glide;
    Param& punch;
    Param& drive;
    Param& tone;

private:
    double sampleRate = 44100.0;
    double phase = 0, freq = 55, targetFreq = 55, punchEnv = 0;
    float amp = 0, velocity = 0;
    bool active = false;
    int currentNote = -1;
    std::vector<int> heldNotes;
    dsp::Svf toneFilter;
};

/** Two-oscillator subtractive poly synth. */
class SynthInstrument : public EventSplittingInstrument<SynthInstrument>
{
public:
    SynthInstrument();
    juce::String getTypeId() const override { return "synth"; }
    void prepare (double sampleRate, int maxBlockSize) override;
    void allNotesOff() override;
    Ptr clone() const override;

    void renderVoices (juce::AudioBuffer<float>& out, int start, int num);
    void handleMidi (const juce::MidiMessage& m);

    Param& wave1;
    Param& wave2;
    Param& detune;
    Param& oscMix;
    Param& cutoff;
    Param& resonance;
    Param& envAmount;
    Param& attack;
    Param& decay;
    Param& sustain;
    Param& release;
    Param& glide;

private:
    struct Voice
    {
        bool active = false;
        int note = 0;
        float velocity = 0;
        dsp::BlepOsc o1, o2, o3;
        dsp::Svf filterL;
        dsp::Adsr ampEnv, filterEnv;
        double freq = 0, targetFreq = 0;
        juce::uint32 age = 0;
    };
    double sampleRate = 44100.0;
    std::array<Voice, 12> voices;
    juce::uint32 counter = 0;
    double lastFreq = 0;
};

/** Factory for built-in instruments by type id. */
Instrument::Ptr createBuiltInInstrument (const juce::String& typeId);

} // namespace merge
