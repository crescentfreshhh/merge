#pragma once

#include <juce_audio_devices/juce_audio_devices.h>
#include "BeatCore.h"
#include "core/SpscQueue.h"

namespace merge
{

class DjCore;
class Recorder;

/** A raw MIDI message passed from the MIDI thread to the message thread (controller mapping). */
struct MidiPacket
{
    juce::uint8 data[3] {};
    int size = 0;
    juce::juce_wchar sourceHash = 0;
};

/** Owns the audio device and mixes the beatmaker, the DJ decks and previews into the output.
    Output 1/2 = master, output 3/4 = headphone cue (when the interface has them). */
class AudioEngine : public juce::AudioIODeviceCallback,
                    public juce::MidiInputCallback
{
public:
    AudioEngine();
    ~AudioEngine() override;

    void initialise (const juce::XmlElement* savedState);
    void shutdown();
    std::unique_ptr<juce::XmlElement> saveState() const;

    juce::AudioDeviceManager& getDeviceManager() noexcept { return deviceManager; }
    BeatCore& beat() noexcept { return beatCore; }
    const BeatCore& beat() const noexcept { return beatCore; }

    void setDjCore (DjCore* dj) noexcept { djCore.store (dj); }
    void setRecorder (Recorder* r) noexcept { recorder.store (r); }

    double getSampleRate() const noexcept { return sampleRate.load(); }
    int getBlockSize() const noexcept { return blockSize.load(); }
    int getNumOutputChannels() const noexcept { return numOutputs.load(); }
    float getCpuLoad() const { return (float) deviceManager.getCpuUsage(); }

    /** Called on the message thread after the device (re)starts. */
    std::function<void (double sampleRate, int blockSize)> onAudioConfigChanged;

    /** Where non-pad MIDI notes go (the selected channel), -1 = ignore. */
    std::atomic<int> keyboardChannel { -1 };
    /** Pads listen to notes 36-51 (MPC/Maschine default). */
    std::atomic<bool> padsFromMidi { true };
    /** Mix level of the beatmaker when in Mix mode (remix deck fader). */
    std::atomic<float> beatLevel { 1.0f };
    /** Split cue: mono cue left, mono master right on a stereo-only device. */
    std::atomic<bool> splitCue { false };
    std::atomic<float> masterPeakL { 0.0f }, masterPeakR { 0.0f };

    /** Message thread: MIDI that isn't routed straight to the pads (for controller mapping / learn). */
    bool popMidiForUi (MidiPacket& p) { return uiMidi.pop (p); }
    /** If set, all incoming MIDI is offered to this callback first on the MIDI thread; return true to consume. */
    std::function<bool (const juce::MidiMessage&)> midiFilter;

    /** Audition a sample through the master output (browser preview). */
    void preview (SampleBuffer::Ptr sample, float gain = 0.8f);
    void stopPreview();

    /** Panic: stop all notes. */
    void panic();

private:
    void audioDeviceIOCallbackWithContext (const float* const* in, int numIn, float* const* out, int numOut,
                                           int numSamples, const juce::AudioIODeviceCallbackContext&) override;
    void audioDeviceAboutToStart (juce::AudioIODevice*) override;
    void audioDeviceStopped() override;
    void handleIncomingMidiMessage (juce::MidiInput* source, const juce::MidiMessage& message) override;

    juce::AudioDeviceManager deviceManager;
    BeatCore beatCore;
    std::atomic<DjCore*> djCore { nullptr };
    std::atomic<Recorder*> recorder { nullptr };

    std::atomic<double> sampleRate { 44100.0 };
    std::atomic<int> blockSize { 512 };
    std::atomic<int> numOutputs { 2 };

    juce::AudioBuffer<float> beatBuffer, masterBuffer, cueBuffer;

    juce::SpinLock midiLock; // several MIDI devices may call back on different threads
    SpscQueue<MidiPacket, 1024> uiMidi;

    std::atomic<SampleBuffer*> previewSample { nullptr };
    SampleBuffer::Ptr previewRef;
    double previewPos = 0.0;
    std::atomic<bool> previewRestart { false };
    std::atomic<float> previewGain { 0.8f };
};

} // namespace merge
