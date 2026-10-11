#include "SampleBuffer.h"

namespace merge
{

SampleBuffer::Ptr SampleBuffer::create (juce::AudioBuffer<float>&& data, double sampleRate,
                                        const juce::String& name, const juce::File& file)
{
    Ptr p (new SampleBuffer (std::move (data), sampleRate, name, file));
    ReleasePool::get().add (p);
    return p;
}

juce::AudioFormatManager& audioFormats()
{
    static juce::AudioFormatManager* manager = []
    {
        auto* m = new juce::AudioFormatManager();
        m->registerBasicFormats();
        return m;
    }();
    return *manager;
}

SampleBuffer::Ptr loadSampleFile (const juce::File& file, double maxSeconds)
{
    std::unique_ptr<juce::AudioFormatReader> reader (audioFormats().createReaderFor (file));
    if (reader == nullptr || reader->lengthInSamples <= 0)
        return nullptr;

    auto frames = reader->lengthInSamples;
    if (maxSeconds > 0)
        frames = juce::jmin<juce::int64> (frames, (juce::int64) (maxSeconds * reader->sampleRate));
    if (frames > std::numeric_limits<int>::max() - 16)
        return nullptr;

    const int channels = (int) juce::jlimit<unsigned int> (1u, 2u, reader->numChannels);
    juce::AudioBuffer<float> data (channels, (int) frames);
    reader->read (&data, 0, (int) frames, 0, true, channels > 1);
    return SampleBuffer::create (std::move (data), reader->sampleRate,
                                 file.getFileNameWithoutExtension(), file);
}

SampleCache& SampleCache::get()
{
    static SampleCache c;
    return c;
}

SampleBuffer::Ptr SampleCache::load (const juce::File& file)
{
    const auto key = file.getFullPathName();
    {
        const juce::ScopedLock sl (lock);
        auto it = cache.find (key);
        if (it != cache.end())
            return it->second;
    }
    auto buffer = loadSampleFile (file);
    if (buffer != nullptr)
    {
        const juce::ScopedLock sl (lock);
        cache[key] = buffer;
    }
    return buffer;
}

void SampleCache::clear()
{
    const juce::ScopedLock sl (lock);
    cache.clear();
}

bool writeAudioFile (const juce::File& file, const juce::AudioBuffer<float>& audio, double sampleRate,
                     int bitDepth, int numSamples)
{
    if (numSamples < 0)
        numSamples = audio.getNumSamples();

    file.getParentDirectory().createDirectory();
    file.deleteFile();

    std::unique_ptr<juce::AudioFormat> format;
    const auto ext = file.getFileExtension().toLowerCase();
    int quality = 0;
    if (ext == ".flac")
    {
        format = std::make_unique<juce::FlacAudioFormat>();
        bitDepth = juce::jmin (bitDepth, 24);
    }
    else if (ext == ".ogg")
    {
        format = std::make_unique<juce::OggVorbisAudioFormat>();
        bitDepth = 16;
        quality = 8;
    }
    else
    {
        format = std::make_unique<juce::WavAudioFormat>();
    }

    std::unique_ptr<juce::OutputStream> stream (file.createOutputStream());
    if (stream == nullptr)
        return false;

    auto options = juce::AudioFormatWriterOptions()
                       .withSampleRate (sampleRate)
                       .withNumChannels (audio.getNumChannels())
                       .withBitsPerSample (bitDepth)
                       .withQualityOptionIndex (quality);
    auto writer = format->createWriterFor (stream, options);
    if (writer == nullptr)
        return false;
    return writer->writeFromAudioSampleBuffer (audio, 0, numSamples);
}

} // namespace merge
