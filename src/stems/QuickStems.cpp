#include "QuickStems.h"
#include <juce_dsp/juce_dsp.h>
#include <complex>
#include <deque>

namespace merge::stems
{

namespace
{
    constexpr int fftOrder = 12;
    constexpr int N = 1 << fftOrder; // 4096
    constexpr int hop = N / 4;        // 75% overlap
    constexpr int bins = N / 2 + 1;
    constexpr int timeMedian = 17;    // frames
    constexpr int freqMedian = 17;    // bins
    constexpr int delay = timeMedian / 2;

    float smoothstep (float a, float b, float x)
    {
        const float t = juce::jlimit (0.0f, 1.0f, (x - a) / (b - a));
        return t * t * (3.0f - 2.0f * t);
    }

    struct Frame
    {
        std::vector<std::complex<float>> left, right;
        std::vector<float> mag;
    };
}

bool separate (const juce::AudioBuffer<float>& input, double sr, Result& out, std::function<bool (float)> progress)
{
    const int length = input.getNumSamples();
    if (length <= 0) return false;
    const float* inL = input.getReadPointer (0);
    const float* inR = input.getReadPointer (juce::jmin (1, input.getNumChannels() - 1));

    for (auto& s : out.stems)
    {
        s.setSize (2, length);
        s.clear();
    }

    juce::dsp::FFT fft (fftOrder);
    std::vector<float> window ((size_t) N);
    for (int i = 0; i < N; ++i)
        window[(size_t) i] = 0.5f - 0.5f * std::cos (2.0f * juce::MathConstants<float>::pi * i / (float) N);
    // Hann analysis + Hann synthesis at 75% overlap sums to 1.5
    const float olaGain = 1.0f / 1.5f;

    // per-bin frequency weights
    std::vector<float> bassW ((size_t) bins), vocW ((size_t) bins);
    for (int b = 0; b < bins; ++b)
    {
        const float f = (float) (b * sr / N);
        bassW[(size_t) b] = 1.0f - smoothstep (120.0f, 260.0f, f);
        vocW[(size_t) b] = smoothstep (180.0f, 380.0f, f) * (1.0f - smoothstep (7000.0f, 12000.0f, f));
    }

    // frames start at -N (padding) so every sample is covered by full overlap
    const int firstStart = -N;
    const int numFrames = (length + 2 * N) / hop + 1;
    std::deque<Frame> ring; // the last `timeMedian` frames
    std::vector<float> buf ((size_t) N * 2), tmp ((size_t) timeMedian), fwin ((size_t) freqMedian);
    std::array<std::vector<float>, 8> synth; // 4 stems x 2 channels
    for (auto& s : synth) s.resize ((size_t) N * 2);

    auto analyse = [&] (int start, Frame& fr)
    {
        fr.left.resize ((size_t) bins);
        fr.right.resize ((size_t) bins);
        fr.mag.resize ((size_t) bins);
        for (int ch = 0; ch < 2; ++ch)
        {
            const float* x = ch == 0 ? inL : inR;
            std::fill (buf.begin(), buf.end(), 0.0f);
            for (int i = 0; i < N; ++i)
            {
                const int idx = start + i;
                buf[(size_t) i] = (idx >= 0 && idx < length) ? x[idx] * window[(size_t) i] : 0.0f;
            }
            fft.performRealOnlyForwardTransform (buf.data(), true);
            auto& dst = ch == 0 ? fr.left : fr.right;
            for (int b = 0; b < bins; ++b)
                dst[(size_t) b] = { buf[(size_t) (2 * b)], buf[(size_t) (2 * b + 1)] };
        }
        for (int b = 0; b < bins; ++b)
            fr.mag[(size_t) b] = std::abs (fr.left[(size_t) b] + fr.right[(size_t) b]) * 0.5f;
    };

    auto synthesise = [&] (const Frame& fr, int start, const std::vector<float>& h, const std::vector<float>& p)
    {
        // masks
        std::array<std::vector<float>, 4> masks;
        for (auto& m : masks) m.resize ((size_t) bins);
        for (int b = 0; b < bins; ++b)
        {
            const float hh = h[(size_t) b] * h[(size_t) b], pp = p[(size_t) b] * p[(size_t) b];
            const float perc = pp / (hh + pp + 1.0e-12f);
            const float harm = 1.0f - perc;
            const auto l = fr.left[(size_t) b], r = fr.right[(size_t) b];
            const float centre = 1.0f - std::abs (l - r) / (std::abs (l) + std::abs (r) + 1.0e-9f);
            const float c3 = juce::jlimit (0.0f, 1.0f, centre * centre * centre);
            const float drums = perc;
            const float bass = harm * bassW[(size_t) b];
            const float vox = harm * (1.0f - bassW[(size_t) b]) * vocW[(size_t) b] * c3;
            masks[0][(size_t) b] = drums;
            masks[1][(size_t) b] = bass;
            masks[2][(size_t) b] = vox;
            masks[3][(size_t) b] = juce::jmax (0.0f, 1.0f - drums - bass - vox);
        }
        for (int s = 0; s < 4; ++s)
            for (int ch = 0; ch < 2; ++ch)
            {
                auto& sb = synth[(size_t) (s * 2 + ch)];
                std::fill (sb.begin(), sb.end(), 0.0f);
                const auto& spec = ch == 0 ? fr.left : fr.right;
                for (int b = 0; b < bins; ++b)
                {
                    const auto v = spec[(size_t) b] * masks[(size_t) s][(size_t) b];
                    sb[(size_t) (2 * b)] = v.real();
                    sb[(size_t) (2 * b + 1)] = v.imag();
                }
                fft.performRealOnlyInverseTransform (sb.data());
                float* dst = out.stems[(size_t) s].getWritePointer (ch);
                for (int i = 0; i < N; ++i)
                {
                    const int idx = start + i;
                    if (idx >= 0 && idx < length)
                        dst[idx] += sb[(size_t) i] * window[(size_t) i] * olaGain;
                }
            }
    };

    int ringFirst = 0, lastPushed = -1;
    std::vector<float> harm ((size_t) bins), perc ((size_t) bins);
    for (int f = 0; f < numFrames + delay; ++f)
    {
        if (f < numFrames)
        {
            Frame fr;
            analyse (firstStart + f * hop, fr);
            ring.push_back (std::move (fr));
            lastPushed = f;
            while ((int) ring.size() > timeMedian)
            {
                ring.pop_front();
                ++ringFirst;
            }
        }
        const int c = f - delay;
        if (c < 0) continue;
        if (c >= numFrames) break;

        const auto& centre = ring[(size_t) (c - ringFirst)];
        const int k0 = juce::jmax (ringFirst, c - delay), k1 = juce::jmin (lastPushed, c + delay);
        const int count = k1 - k0 + 1;
        for (int b = 0; b < bins; ++b)
        {
            for (int k = 0; k < count; ++k)
                tmp[(size_t) k] = ring[(size_t) (k0 + k - ringFirst)].mag[(size_t) b];
            std::nth_element (tmp.begin(), tmp.begin() + count / 2, tmp.begin() + count);
            harm[(size_t) b] = tmp[(size_t) (count / 2)];
        }
        for (int b = 0; b < bins; ++b)
        {
            int n = 0;
            for (int k = b - freqMedian / 2; k <= b + freqMedian / 2; ++k)
                if (k >= 0 && k < bins) fwin[(size_t) n++] = centre.mag[(size_t) k];
            std::nth_element (fwin.begin(), fwin.begin() + n / 2, fwin.begin() + n);
            perc[(size_t) b] = fwin[(size_t) (n / 2)];
        }
        synthesise (centre, firstStart + c * hop, harm, perc);

        if (progress && (c % 64) == 0)
            if (! progress ((float) c / (float) numFrames))
                return false;
    }
    if (progress) progress (1.0f);
    return true;
}

} // namespace merge::stems
