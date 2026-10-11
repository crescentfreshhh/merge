#include "AudioEngine.h"
#include "dj/DjCore.h"
#include "dj/Recorder.h"
#include "core/Dsp.h"

namespace merge
{

AudioEngine::AudioEngine() = default;

AudioEngine::~AudioEngine()
{
    shutdown();
}

void AudioEngine::initialise (const juce::XmlElement* savedState)
{
    const auto error = deviceManager.initialise (0, 4, savedState, true);
    if (error.isNotEmpty())
        DBG ("Audio device error: " << error);

    // enable every MIDI input by default the first time
    if (savedState == nullptr)
        for (auto& d : juce::MidiInput::getAvailableDevices())
            deviceManager.setMidiInputDeviceEnabled (d.identifier, true);

    deviceManager.addMidiInputDeviceCallback ({}, this);
    deviceManager.addAudioCallback (this);
}

void AudioEngine::shutdown()
{
    deviceManager.removeAudioCallback (this);
    deviceManager.removeMidiInputDeviceCallback ({}, this);
    deviceManager.closeAudioDevice();
}

std::unique_ptr<juce::XmlElement> AudioEngine::saveState() const
{
    return deviceManager.createStateXml();
}

void AudioEngine::preview (SampleBuffer::Ptr sample, float gain)
{
    previewRef = sample;
    previewGain.store (gain);
    previewSample.store (sample.get());
    previewRestart.store (true);
}

void AudioEngine::stopPreview()
{
    previewSample.store (nullptr);
}

void AudioEngine::panic()
{
    beatCore.pushEvent ({ BeatEvent::Panic });
}

void AudioEngine::audioDeviceAboutToStart (juce::AudioIODevice* device)
{
    const double sr = device->getCurrentSampleRate();
    const int bs = device->getCurrentBufferSizeSamples();
    const int maxBlock = juce::jmax (bs, 2048);
    sampleRate.store (sr);
    blockSize.store (maxBlock);
    numOutputs.store (device->getActiveOutputChannels().countNumberOfSetBits());

    beatCore.prepare (sr, maxBlock);
    if (auto* dj = djCore.load())
        dj->prepare (sr, maxBlock);
    beatBuffer.setSize (2, maxBlock);
    masterBuffer.setSize (2, maxBlock);
    cueBuffer.setSize (2, maxBlock);

    juce::MessageManager::callAsync ([this, sr, maxBlock]
    {
        if (onAudioConfigChanged)
            onAudioConfigChanged (sr, maxBlock);
    });
}

void AudioEngine::audioDeviceStopped() {}

void AudioEngine::audioDeviceIOCallbackWithContext (const float* const* in, int numIn, float* const* out, int numOut,
                                                    int numSamples, const juce::AudioIODeviceCallbackContext&)
{
    juce::ScopedNoDenormals noDenormals;
    int offset = 0;
    while (offset < numSamples)
    {
        const int n = juce::jmin (numSamples - offset, beatBuffer.getNumSamples());
        if (n <= 0) break;

        masterBuffer.clear (0, n);
        cueBuffer.clear (0, n);

        // DJ decks first: they drive the master clock the beatmaker may follow
        auto* dj = djCore.load();
        if (dj != nullptr)
            dj->process (masterBuffer, cueBuffer, n);

        beatCore.process (beatBuffer, n);
        const float bl = beatLevel.load();
        masterBuffer.addFrom (0, 0, beatBuffer, 0, 0, n, bl);
        masterBuffer.addFrom (1, 0, beatBuffer, 1, 0, n, bl);
        if (dj != nullptr)
            dj->addBeatToCue (beatBuffer, cueBuffer, n);

        // browser preview
        if (auto* p = previewSample.load())
        {
            if (previewRestart.exchange (false))
                previewPos = 0.0;
            const double inc = p->getSampleRate() / sampleRate.load();
            const float g = previewGain.load();
            const bool stereo = p->getNumChannels() > 1;
            for (int i = 0; i < n; ++i)
            {
                if (previewPos >= p->getNumFrames()) { previewSample.store (nullptr); break; }
                const float l = p->readLinear (0, previewPos) * g;
                const float r = stereo ? p->readLinear (1, previewPos) * g : l;
                masterBuffer.addSample (0, i, l);
                masterBuffer.addSample (1, i, r);
                cueBuffer.addSample (0, i, l);
                cueBuffer.addSample (1, i, r);
                previewPos += inc;
            }
        }

        // safety soft clip on the master
        for (int ch = 0; ch < 2; ++ch)
        {
            auto* d = masterBuffer.getWritePointer (ch);
            for (int i = 0; i < n; ++i)
                d[i] = dsp::softClip (d[i]);
        }
        const float pl = masterBuffer.getMagnitude (0, 0, n), pr = masterBuffer.getMagnitude (1, 0, n);
        if (pl > masterPeakL.load()) masterPeakL.store (pl);
        if (pr > masterPeakR.load()) masterPeakR.store (pr);

        if (auto* rec = recorder.load())
            rec->capture (masterBuffer, n);

        const bool split = splitCue.load() && numOut < 4;
        for (int ch = 0; ch < numOut; ++ch)
        {
            if (out[ch] == nullptr) continue;
            float* dst = out[ch] + offset;
            if (split && ch < 2)
            {
                const auto& src = ch == 0 ? cueBuffer : masterBuffer;
                for (int i = 0; i < n; ++i)
                    dst[i] = 0.5f * (src.getSample (0, i) + src.getSample (1, i));
            }
            else if (ch < 2)
            {
                juce::FloatVectorOperations::copy (dst, masterBuffer.getReadPointer (ch), n);
            }
            else if (ch < 4)
            {
                const auto* c = cueBuffer.getReadPointer (ch - 2);
                for (int i = 0; i < n; ++i)
                    dst[i] = dsp::softClip (c[i]);
            }
            else
            {
                juce::FloatVectorOperations::clear (dst, n);
            }
        }
        offset += n;
    }
    juce::ignoreUnused (in, numIn);
}

void AudioEngine::handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage& m)
{
    const juce::SpinLock::ScopedLockType sl (midiLock);

    if (midiFilter && midiFilter (m))
        return;

    if (m.isNoteOnOrOff())
    {
        const int note = m.getNoteNumber();
        if (padsFromMidi.load() && note >= 36 && note < 36 + kNumPads)
        {
            const int pad = note - 36;
            if (m.isNoteOn())
                beatCore.pushMidiEvent ({ BeatEvent::NoteOn, pad, -1, m.getFloatVelocity() });
            else
                beatCore.pushMidiEvent ({ BeatEvent::NoteOff, pad, -1, 0.0f });
            MidiPacket p;
            p.size = juce::jmin (3, m.getRawDataSize());
            std::memcpy (p.data, m.getRawData(), (size_t) p.size);
            uiMidi.push (p); // also let the UI light the pads / learn
            return;
        }
        const int ch = keyboardChannel.load();
        if (ch >= 0)
        {
            if (m.isNoteOn())
                beatCore.pushMidiEvent ({ BeatEvent::NoteOn, ch, note, m.getFloatVelocity() });
            else
                beatCore.pushMidiEvent ({ BeatEvent::NoteOff, ch, note, 0.0f });
        }
    }

    if (m.getRawDataSize() <= 3)
    {
        MidiPacket p;
        p.size = m.getRawDataSize();
        std::memcpy (p.data, m.getRawData(), (size_t) p.size);
        uiMidi.push (p);
    }
}

} // namespace merge
