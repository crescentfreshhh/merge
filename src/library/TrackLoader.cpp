#include "TrackLoader.h"

namespace merge
{

namespace
{
    juce::String decodeId3Text (const juce::uint8* d, int size)
    {
        if (size <= 1) return {};
        const int enc = d[0];
        ++d; --size;
        juce::String s;
        if (enc == 0) // ISO-8859-1
        {
            for (int i = 0; i < size && d[i] != 0; ++i) s += (juce::juce_wchar) d[i];
        }
        else if (enc == 3) // UTF-8
        {
            int len = 0;
            while (len < size && d[len] != 0) ++len;
            s = juce::String::fromUTF8 (reinterpret_cast<const char*> (d), len);
        }
        else // UTF-16 (1 = with BOM, 2 = big endian)
        {
            bool bigEndian = enc == 2;
            int i = 0;
            if (enc == 1 && size >= 2)
            {
                bigEndian = d[0] == 0xFE && d[1] == 0xFF;
                i = 2;
            }
            for (; i + 1 < size; i += 2)
            {
                const juce::juce_wchar c = bigEndian ? (juce::juce_wchar) ((d[i] << 8) | d[i + 1]) : (juce::juce_wchar) ((d[i + 1] << 8) | d[i]);
                if (c == 0) break;
                s += c;
            }
        }
        return s.trim();
    }

    void readId3 (const juce::File& f, TrackInfo& t)
    {
        juce::FileInputStream in (f);
        if (! in.openedOk()) return;
        juce::uint8 header[10];
        if (in.read (header, 10) != 10 || header[0] != 'I' || header[1] != 'D' || header[2] != '3') return;
        const int version = header[3];
        const int tagSize = (header[6] << 21) | (header[7] << 14) | (header[8] << 7) | header[9];
        if (tagSize <= 0 || tagSize > 8 * 1024 * 1024) return;
        juce::MemoryBlock tag;
        in.readIntoMemoryBlock (tag, tagSize);
        const auto* d = static_cast<const juce::uint8*> (tag.getData());
        int pos = 0;
        while (pos + 10 <= (int) tag.getSize())
        {
            const juce::String id (reinterpret_cast<const char*> (d + pos), 4);
            if (d[pos] == 0) break;
            int size = version >= 4 ? ((d[pos + 4] << 21) | (d[pos + 5] << 14) | (d[pos + 6] << 7) | d[pos + 7])
                                    : ((d[pos + 4] << 24) | (d[pos + 5] << 16) | (d[pos + 6] << 8) | d[pos + 7]);
            pos += 10;
            if (size <= 0 || pos + size > (int) tag.getSize()) break;
            if (id.startsWithChar ('T'))
            {
                const auto text = decodeId3Text (d + pos, size);
                if (id == "TIT2") t.title = text;
                else if (id == "TPE1") t.artist = text;
                else if (id == "TALB") t.album = text;
                else if (id == "TCON") t.genre = text.startsWithChar ('(') ? text.fromFirstOccurrenceOf (")", false, false) : text;
                else if (id == "TBPM") t.bpm = text.getDoubleValue();
                else if (id == "TKEY") t.key = MusicalKey::fromString (text).toIndex();
            }
            pos += size;
        }
    }
}

TrackInfo readTrackMetadata (const juce::File& file)
{
    TrackInfo t;
    t.file = file;
    if (file.hasFileExtension ("mp3"))
        readId3 (file, t);
    if (std::unique_ptr<juce::AudioFormatReader> r (audioFormats().createReaderFor (file)); r != nullptr)
    {
        t.duration = r->sampleRate > 0 ? (double) r->lengthInSamples / r->sampleRate : 0.0;
        auto md = [&r] (const char* a, const char* b = nullptr)
        {
            auto v = r->metadataValues.getValue (a, {});
            if (v.isEmpty() && b != nullptr) v = r->metadataValues.getValue (b, {});
            return v;
        };
        if (t.title.isEmpty()) t.title = md ("INAM", "TITLE");
        if (t.artist.isEmpty()) t.artist = md ("IART", "ARTIST");
        if (t.album.isEmpty()) t.album = md ("IPRD", "ALBUM");
        if (t.genre.isEmpty()) t.genre = md ("IGNR", "GENRE");
    }
    if (t.title.isEmpty())
    {
        const auto name = file.getFileNameWithoutExtension();
        if (name.contains (" - "))
        {
            if (t.artist.isEmpty()) t.artist = name.upToFirstOccurrenceOf (" - ", false, false).trim();
            t.title = name.fromFirstOccurrenceOf (" - ", false, false).trim();
        }
        else
        {
            t.title = name;
        }
    }
    return t;
}

bool decodeForAnalysis (const juce::File& file, std::vector<float>& mono, double& sampleRate, double& duration, double targetRate)
{
    std::unique_ptr<juce::AudioFormatReader> r (audioFormats().createReaderFor (file));
    if (r == nullptr || r->lengthInSamples <= 0 || r->sampleRate <= 0)
        return false;
    duration = (double) r->lengthInSamples / r->sampleRate;
    // integer decimation keeps the timeline exact across chunks (44.1k -> 22.05k, 48k -> 24k, 96k -> 24k)
    const double ratio = juce::jmax (1.0, std::round (r->sampleRate / targetRate));
    targetRate = r->sampleRate / ratio;
    const bool resample = ratio > 1.0;
    sampleRate = resample ? targetRate : r->sampleRate;
    mono.clear();
    mono.reserve ((size_t) (duration * sampleRate) + 16);

    const int chunk = 65536;
    juce::AudioBuffer<float> buf ((int) juce::jlimit (1u, 2u, r->numChannels), chunk);
    std::vector<float> m ((size_t) chunk);
    juce::LagrangeInterpolator interp;
    std::vector<float> out ((size_t) (chunk / juce::jmax (1.0, ratio)) + 16);
    // simple anti-alias low-pass before decimation
    juce::IIRFilter aa;
    if (resample)
        aa.setCoefficients (juce::IIRCoefficients::makeLowPass (r->sampleRate, targetRate * 0.45));

    for (juce::int64 pos = 0; pos < r->lengthInSamples; pos += chunk)
    {
        const int n = (int) juce::jmin<juce::int64> (chunk, r->lengthInSamples - pos);
        r->read (&buf, 0, n, pos, true, buf.getNumChannels() > 1);
        for (int i = 0; i < n; ++i)
        {
            float v = buf.getSample (0, i);
            if (buf.getNumChannels() > 1) v = 0.5f * (v + buf.getSample (1, i));
            m[(size_t) i] = v;
        }
        if (! resample)
        {
            mono.insert (mono.end(), m.begin(), m.begin() + n);
            continue;
        }
        aa.processSamples (m.data(), n);
        const int outCount = (int) std::floor (n / ratio);
        if (outCount <= 0) continue;
        if ((int) out.size() < outCount + 4) out.resize ((size_t) outCount + 4);
        interp.process (ratio, m.data(), out.data(), outCount, n, 0);
        mono.insert (mono.end(), out.begin(), out.begin() + outCount);
    }
    return ! mono.empty();
}

std::optional<analysis::TrackAnalysis> analyseFile (const juce::File& file, double minBpm, double maxBpm)
{
    std::vector<float> mono;
    double sr = 0.0, duration = 0.0;
    if (! decodeForAnalysis (file, mono, sr, duration))
        return std::nullopt;
    analysis::TrackAnalysis a;
    a.durationSec = duration;
    a.tempo = analysis::detectTempo (mono, sr, minBpm, maxBpm);
    a.key = analysis::detectKey (mono, sr);
    float* chans[2] { mono.data(), mono.data() };
    juce::AudioBuffer<float> view (chans, 2, (int) mono.size());
    a.lufs = analysis::integratedLoudness (view, sr);
    juce::AudioBuffer<float> monoView (chans, 1, (int) mono.size());
    a.waveform = analysis::computeWaveform (monoView, sr);
    return a;
}

SampleBuffer::Ptr decodeForPlayback (const juce::File& file, double targetRate, std::function<bool()> shouldCancel)
{
    std::unique_ptr<juce::AudioFormatReader> r (audioFormats().createReaderFor (file));
    if (r == nullptr || r->lengthInSamples <= 0)
        return nullptr;
    const auto frames = r->lengthInSamples;
    if (frames > (juce::int64) std::numeric_limits<int>::max() / 2)
        return nullptr;
    juce::AudioBuffer<float> src (2, (int) frames);
    const int chunk = 1 << 18;
    for (juce::int64 pos = 0; pos < frames; pos += chunk)
    {
        if (shouldCancel && shouldCancel()) return nullptr;
        const int n = (int) juce::jmin<juce::int64> (chunk, frames - pos);
        r->read (&src, (int) pos, n, pos, true, true);
    }
    if (r->numChannels == 1)
        src.copyFrom (1, 0, src, 0, 0, (int) frames);

    if (std::abs (r->sampleRate - targetRate) < 1.0)
        return SampleBuffer::create (std::move (src), r->sampleRate, file.getFileNameWithoutExtension(), file);

    const double ratio = r->sampleRate / targetRate;
    const int outFrames = (int) std::floor ((double) frames / ratio);
    juce::AudioBuffer<float> dst (2, outFrames);
    for (int ch = 0; ch < 2; ++ch)
    {
        juce::LagrangeInterpolator interp;
        interp.process (ratio, src.getReadPointer (ch), dst.getWritePointer (ch), outFrames, (int) frames, 0);
    }
    return SampleBuffer::create (std::move (dst), targetRate, file.getFileNameWithoutExtension(), file);
}

} // namespace merge
