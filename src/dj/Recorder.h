#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <array>
#include <atomic>

namespace merge
{

/** Records the master output (and optionally each deck, post-fader) to 24-bit WAV files
    without blocking the audio thread (ThreadedWriter + background thread). */
class Recorder
{
public:
    static constexpr int kMaxDeckStreams = 4;

    Recorder();
    ~Recorder();

    /** Message thread. Creates <folder>/master.wav (+ deck-1.wav ...). */
    bool start (const juce::File& folder, double sampleRate, bool perDeck, int numDecks);
    void stop();
    bool isRecording() const noexcept { return recording.load(); }
    juce::File getFolder() const { return folder; }
    double getSecondsRecorded() const noexcept { return sampleRate > 0 ? samplesWritten.load() / sampleRate : 0.0; }
    juce::Array<juce::File> getDeckFiles() const { return deckFiles; }
    juce::File getMasterFile() const { return folder.getChildFile ("master.wav"); }

    /** Audio thread. */
    void capture (const juce::AudioBuffer<float>& master, int n);
    void captureDeck (int deck, const juce::AudioBuffer<float>& buffer, int n);

    double bpmAtStart = 0.0;

private:
    std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> makeWriter (const juce::File& f);

    juce::TimeSliceThread thread { "Merge recorder" };
    juce::SpinLock lock;
    std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> master;
    std::array<std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter>, kMaxDeckStreams> decks;
    std::atomic<bool> recording { false };
    std::atomic<juce::int64> samplesWritten { 0 };
    double sampleRate = 44100.0;
    juce::File folder;
    juce::Array<juce::File> deckFiles;
};

} // namespace merge
