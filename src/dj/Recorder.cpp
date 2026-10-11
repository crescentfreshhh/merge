#include "Recorder.h"

namespace merge
{

Recorder::Recorder()
{
    thread.startThread();
}

Recorder::~Recorder()
{
    stop();
    thread.stopThread (2000);
}

std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> Recorder::makeWriter (const juce::File& f)
{
    f.deleteFile();
    std::unique_ptr<juce::OutputStream> stream (f.createOutputStream());
    if (stream == nullptr)
        return nullptr;
    juce::WavAudioFormat wav;
    auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions()
                                                   .withSampleRate (sampleRate)
                                                   .withNumChannels (2)
                                                   .withBitsPerSample (24));
    if (writer == nullptr)
        return nullptr;
    return std::make_unique<juce::AudioFormatWriter::ThreadedWriter> (writer.release(), thread, 1 << 17);
}

bool Recorder::start (const juce::File& f, double sr, bool perDeck, int numDecks)
{
    stop();
    folder = f;
    folder.createDirectory();
    sampleRate = sr;
    deckFiles.clear();

    auto m = makeWriter (folder.getChildFile ("master.wav"));
    if (m == nullptr)
        return false;
    std::array<std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter>, kMaxDeckStreams> d;
    if (perDeck)
        for (int i = 0; i < juce::jmin (numDecks, kMaxDeckStreams); ++i)
        {
            auto file = folder.getChildFile ("deck-" + juce::String (i + 1) + ".wav");
            d[(size_t) i] = makeWriter (file);
            deckFiles.add (file);
        }

    const juce::SpinLock::ScopedLockType sl (lock);
    master = std::move (m);
    decks = std::move (d);
    samplesWritten.store (0);
    recording.store (true);
    return true;
}

void Recorder::stop()
{
    std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> m;
    std::array<std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter>, kMaxDeckStreams> d;
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        recording.store (false);
        m = std::move (master);
        d = std::move (decks);
    }
    // writers flush and close in their destructors (outside the lock)
}

void Recorder::capture (const juce::AudioBuffer<float>& buffer, int n)
{
    if (! recording.load()) return;
    const juce::SpinLock::ScopedTryLockType tl (lock);
    if (! tl.isLocked() || master == nullptr) return;
    master->write (buffer.getArrayOfReadPointers(), n);
    samplesWritten.fetch_add (n);
}

void Recorder::captureDeck (int deck, const juce::AudioBuffer<float>& buffer, int n)
{
    if (! recording.load() || deck < 0 || deck >= kMaxDeckStreams) return;
    const juce::SpinLock::ScopedTryLockType tl (lock);
    if (! tl.isLocked() || decks[(size_t) deck] == nullptr) return;
    decks[(size_t) deck]->write (buffer.getArrayOfReadPointers(), n);
}

} // namespace merge
