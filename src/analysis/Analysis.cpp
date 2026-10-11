#include "Analysis.h"
#include "core/Dsp.h"
#include <juce_dsp/juce_dsp.h>
#include <numeric>

namespace merge::analysis
{

std::vector<float> toMono (const juce::AudioBuffer<float>& audio)
{
    const int n = audio.getNumSamples(), ch = audio.getNumChannels();
    std::vector<float> mono ((size_t) n, 0.0f);
    if (ch == 0) return mono;
    for (int c = 0; c < ch; ++c)
    {
        const float* d = audio.getReadPointer (c);
        for (int i = 0; i < n; ++i)
            mono[(size_t) i] += d[i];
    }
    const float g = 1.0f / (float) ch;
    for (auto& v : mono) v *= g;
    return mono;
}

std::vector<float> onsetEnvelope (const std::vector<float>& mono, double sr, int fftOrder, int hop, double& hopSeconds,
                                  double maxHz, double minHz)
{
    const int N = 1 << fftOrder;
    hopSeconds = hop / sr;
    const int frames = mono.size() > (size_t) N ? (int) ((mono.size() - (size_t) N) / (size_t) hop) + 1 : 0;
    std::vector<float> env ((size_t) juce::jmax (0, frames), 0.0f);
    if (frames <= 1) return env;

    juce::dsp::FFT fft (fftOrder);
    std::vector<float> window ((size_t) N), buf ((size_t) N * 2), prev ((size_t) N / 2 + 1, 0.0f), cur ((size_t) N / 2 + 1);
    for (int i = 0; i < N; ++i)
        window[(size_t) i] = 0.5f - 0.5f * std::cos (2.0f * juce::MathConstants<float>::pi * i / (float) N);
    const int maxBin = juce::jmax (2, juce::jmin (N / 2, (int) (maxHz / sr * N)));

    for (int f = 0; f < frames; ++f)
    {
        const float* x = mono.data() + (size_t) f * (size_t) hop;
        std::fill (buf.begin(), buf.end(), 0.0f);
        for (int i = 0; i < N; ++i)
            buf[(size_t) i] = x[i] * window[(size_t) i];
        fft.performFrequencyOnlyForwardTransform (buf.data(), true);
        float flux = 0.0f;
        const int minBin = juce::jmax (1, (int) (minHz / sr * N));
        for (int b = minBin; b <= maxBin; ++b)
        {
            cur[(size_t) b] = std::log1p (100.0f * buf[(size_t) b]);
            flux += juce::jmax (0.0f, cur[(size_t) b] - prev[(size_t) b]);
        }
        std::swap (cur, prev);
        env[(size_t) f] = f == 0 ? 0.0f : flux;
    }

    // remove the slowly varying part and half-wave rectify
    const int win = juce::jmax (3, (int) std::round (0.4 / hopSeconds));
    std::vector<float> out (env.size());
    double acc = 0.0;
    std::deque<float> q;
    for (size_t i = 0; i < env.size(); ++i)
    {
        q.push_back (env[i]);
        acc += env[i];
        if ((int) q.size() > win) { acc -= q.front(); q.pop_front(); }
        out[i] = juce::jmax (0.0f, env[i] - (float) (acc / (double) q.size()));
    }
    return out;
}

static double interp (const std::vector<float>& v, double pos)
{
    if (pos < 0) return 0.0;
    const auto i = (size_t) pos;
    if (i + 1 >= v.size()) return i < v.size() ? v[i] : 0.0;
    const double f = pos - (double) i;
    return v[i] * (1.0 - f) + v[i + 1] * f;
}

/** Best-phase comb score for a given beat period (frames). */
static double combScore (const std::vector<float>& env, double period, double& bestPhase)
{
    double best = -1.0;
    const int phases = juce::jmax (8, (int) std::ceil (period * 2.0));
    for (int p = 0; p < phases; ++p)
    {
        const double phase = p * period / phases;
        double s = 0.0;
        for (double t = phase; t < (double) env.size(); t += period)
            s += interp (env, t);
        if (s > best)
        {
            best = s;
            bestPhase = phase;
        }
    }
    return best;
}

TempoResult detectTempo (const std::vector<float>& mono, double sr, double minBpm, double maxBpm)
{
    TempoResult r;
    if (mono.size() < (size_t) (sr * 4.0))
        return r;

    double hopSec = 0.0;
    const int hop = juce::jmax (64, (int) std::round (sr * 0.0058)); // ~5.8 ms frames
    auto env = onsetEnvelope (mono, sr, 10, hop, hopSec);
    if (env.size() < 64) return r;

    // 1. autocorrelation over a wide tempo range
    const int minLag = (int) std::floor (60.0 / 240.0 / hopSec);
    const int maxLag = (int) std::ceil (60.0 / 50.0 / hopSec);
    std::vector<double> ac ((size_t) maxLag * 4 + 2, 0.0);
    for (int lag = minLag; lag < (int) ac.size(); ++lag)
    {
        double s = 0.0;
        for (size_t i = (size_t) lag; i < env.size(); ++i)
            s += env[i] * env[i - (size_t) lag];
        ac[(size_t) lag] = s / (double) (env.size() - (size_t) lag);
    }

    // 2. comb over multiples + a preference for musically common tempi
    double bestScore = -1.0;
    int bestLag = minLag;
    for (int lag = minLag; lag <= maxLag; ++lag)
    {
        double s = ac[(size_t) lag];
        if (2 * lag < (int) ac.size()) s += 0.5 * ac[(size_t) (2 * lag)];
        if (4 * lag < (int) ac.size()) s += 0.25 * ac[(size_t) (4 * lag)];
        const double bpm = 60.0 / (lag * hopSec);
        const double prior = std::exp (-0.5 * std::pow (std::log2 (bpm / 120.0) / 1.0, 2.0));
        s *= prior;
        if (s > bestScore) { bestScore = s; bestLag = lag; }
    }

    // 3. fold into the requested range
    double period = bestLag;
    double bpm = 60.0 / (period * hopSec);
    while (bpm < minBpm) { bpm *= 2.0; period /= 2.0; }
    while (bpm >= maxBpm) { bpm /= 2.0; period *= 2.0; }

    // 4. refine the period with a full-length comb (precision matters for beat grids)
    double phase = 0.0;
    double bestPeriod = period, bestComb = -1.0;
    for (double p = period * 0.985; p <= period * 1.015; p += period * 0.0005)
    {
        double ph = 0.0;
        const double s = combScore (env, p, ph) * p; // normalise by number of beats
        if (s > bestComb) { bestComb = s; bestPeriod = p; phase = ph; }
    }
    for (double p = bestPeriod - period * 0.0005; p <= bestPeriod + period * 0.0005; p += period * 0.00005)
    {
        double ph = 0.0;
        const double s = combScore (env, p, ph) * p;
        if (s > bestComb) { bestComb = s; bestPeriod = p; phase = ph; }
    }

    bpm = 60.0 / (bestPeriod * hopSec);

    // Octave check (85 vs 170, 87 vs 174): the real tempo has a backbeat - snares on beats 2 and 4.
    // Put a grid on the kick drum and look at where snare-band onsets land within groups of four beats.
    {
        const bool canDouble = bpm * 2.0 < maxBpm;
        const bool canHalve = bpm / 2.0 >= minBpm;
        if (canDouble || canHalve)
        {
            double hs = 0.0;
            const auto envKick = onsetEnvelope (mono, sr, 10, hop, hs, 150.0);
            const auto envSnare = onsetEnvelope (mono, sr, 10, hop, hs, 3000.0, 250.0);
            auto backbeat = [&] (double p)
            {
                // On the true tempo, kicks sit on beats 1 & 3 and snares on 2 & 4 (or the other way round
                // if the grid starts on beat 2). The grid phase comes from the full onset envelope.
                double ph = 0.0;
                combScore (env, p, ph);
                double sPos[4] {}, kPos[4] {};
                for (int i = 0; i < 4; ++i)
                    for (double t = ph + i * p; t < (double) envSnare.size(); t += 4.0 * p)
                    {
                        sPos[i] += interp (envSnare, t);
                        kPos[i] += interp (envKick, t);
                    }
                const double sTotal = sPos[0] + sPos[1] + sPos[2] + sPos[3] + 1.0e-9;
                const double kTotal = kPos[0] + kPos[1] + kPos[2] + kPos[3] + 1.0e-9;
                auto pairScore = [&] (int sa, int sb, int ka, int kb)
                {
                    const double snares = (sPos[sa] + sPos[sb]) / sTotal;
                    const double kicks = (kPos[ka] + kPos[kb]) / kTotal;
                    const double presence = juce::jmin (sPos[sa], sPos[sb]) / juce::jmax (1.0e-9, juce::jmax (sPos[sa], sPos[sb]));
                    return snares * kicks * presence;
                };
                return juce::jmax (pairScore (1, 3, 0, 2), pairScore (0, 2, 1, 3));
            };
            const double slowP = canDouble ? bestPeriod : bestPeriod * 2.0;
            const double fastP = slowP * 0.5;
            const double bSlow = backbeat (slowP), bFast = backbeat (fastP);
            double chosen = bestPeriod;
            if (bFast >= 0.3 && bFast > bSlow * 1.3) chosen = fastP;
            else if (bSlow >= 0.3 && bSlow > bFast * 1.3) chosen = slowP;
            if (std::abs (chosen - bestPeriod) > 1.0e-9)
            {
                bestPeriod = chosen;
                combScore (env, bestPeriod, phase);
                bpm = 60.0 / (bestPeriod * hopSec);
            }
        }
    }

    if (std::abs (bpm - std::round (bpm)) < 0.08)
    {
        bpm = std::round (bpm);
        bestPeriod = 60.0 / (bpm * hopSec);
        combScore (env, bestPeriod, phase);
    }

    // 5. downbeat: of the four beats in a bar, pick the one with the strongest onsets
    double bestBar = -1.0;
    int downbeat = 0;
    for (int b = 0; b < 4; ++b)
    {
        double s = 0.0;
        for (double t = phase + b * bestPeriod; t < (double) env.size(); t += bestPeriod * 4.0)
            s += interp (env, t);
        if (s > bestBar) { bestBar = s; downbeat = b; }
    }

    // onset frames are centred half a window after the frame start
    const double frameCentre = (1 << 10) * 0.5 / sr;
    double first = (phase + downbeat * bestPeriod) * hopSec + frameCentre - hopSec * 0.5;
    const double beatSec = 60.0 / bpm;
    while (first - beatSec * 4.0 >= 0.0) first -= beatSec * 4.0;
    while (first < 0.0) first += beatSec;

    // confidence: how peaky the comb is versus the average
    double mean = 0.0;
    for (auto v : env) mean += v;
    mean /= (double) env.size();
    const double beats = (double) env.size() / bestPeriod;
    r.confidence = (float) juce::jlimit (0.0, 1.0, (bestComb / bestPeriod / juce::jmax (1.0, beats)) / (mean * 4.0 + 1e-9));
    r.bpm = bpm;
    r.firstBeatSec = first;
    return r;
}

KeyResult detectKey (const std::vector<float>& mono, double sr)
{
    KeyResult result;
    if (mono.size() < (size_t) (sr * 2.0))
        return result;

    const int order = sr > 60000 ? 15 : 14;
    const int N = 1 << order;
    const int hop = N / 2;
    juce::dsp::FFT fft (order);
    std::vector<float> window ((size_t) N), buf ((size_t) N * 2);
    for (int i = 0; i < N; ++i)
        window[(size_t) i] = 0.5f - 0.5f * std::cos (2.0f * juce::MathConstants<float>::pi * i / (float) N);

    // bin -> pitch class weights (55 Hz .. 2 kHz)
    struct BinMap { int bin; int pc; float w; };
    std::vector<BinMap> map;
    for (int b = 1; b < N / 2; ++b)
    {
        const double f = b * sr / N;
        if (f < 55.0 || f > 2000.0) continue;
        const double pitch = 69.0 + 12.0 * std::log2 (f / 440.0);
        const double nearest = std::round (pitch);
        const double dist = std::abs (pitch - nearest);
        if (dist > 0.4) continue;
        map.push_back ({ b, ((int) nearest % 12 + 12) % 12, (float) (1.0 - dist / 0.4) });
    }

    std::array<double, 12> chroma {};
    for (size_t start = 0; start + (size_t) N <= mono.size(); start += (size_t) hop)
    {
        std::fill (buf.begin(), buf.end(), 0.0f);
        for (int i = 0; i < N; ++i)
            buf[(size_t) i] = mono[start + (size_t) i] * window[(size_t) i];
        fft.performFrequencyOnlyForwardTransform (buf.data(), true);
        std::array<double, 12> frame {};
        for (auto& m : map)
            frame[(size_t) m.pc] += m.w * buf[(size_t) m.bin];
        double sum = 0.0;
        for (auto v : frame) sum += v;
        if (sum <= 1e-9) continue;
        for (int i = 0; i < 12; ++i)
            chroma[(size_t) i] += std::sqrt (frame[(size_t) i] / sum); // normalised per frame, compressed
    }

    static const double majorProfile[12] { 6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88 };
    static const double minorProfile[12] { 6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17 };

    auto correlate = [&chroma] (const double* profile, int root)
    {
        double mx = 0, my = 0;
        for (int i = 0; i < 12; ++i) { mx += chroma[(size_t) ((i + root) % 12)]; my += profile[i]; }
        mx /= 12; my /= 12;
        double sxy = 0, sxx = 0, syy = 0;
        for (int i = 0; i < 12; ++i)
        {
            const double x = chroma[(size_t) ((i + root) % 12)] - mx, y = profile[i] - my;
            sxy += x * y; sxx += x * x; syy += y * y;
        }
        return sxx > 0 && syy > 0 ? sxy / std::sqrt (sxx * syy) : 0.0;
    };

    double best = -2.0, second = -2.0;
    for (int root = 0; root < 12; ++root)
        for (int minor = 0; minor < 2; ++minor)
        {
            const double c = correlate (minor ? minorProfile : majorProfile, root);
            if (c > best)
            {
                second = best;
                best = c;
                result.key = { root, minor == 1 };
            }
            else if (c > second)
            {
                second = c;
            }
        }
    result.confidence = (float) juce::jlimit (0.0, 1.0, (best - second) * 5.0);
    return result;
}

float integratedLoudness (const juce::AudioBuffer<float>& audio, double sr)
{
    const int n = audio.getNumSamples();
    if (n < (int) (sr * 0.4)) return -100.0f;
    // K-weighting: high shelf (+4 dB @ 1.68 kHz) then high-pass (38 Hz)
    std::vector<double> power ((size_t) n, 0.0);
    for (int ch = 0; ch < juce::jmin (2, audio.getNumChannels()); ++ch)
    {
        dsp::Biquad shelf, hpf;
        shelf.setup (dsp::Biquad::HighShelf, sr, 1681.97, 0.7071, 4.0);
        hpf.setup (dsp::Biquad::HighPass, sr, 38.13, 0.5003);
        const float* d = audio.getReadPointer (ch);
        for (int i = 0; i < n; ++i)
        {
            const float y = hpf.tick (shelf.tick (d[i], 0), 0);
            power[(size_t) i] += (double) y * y;
        }
    }
    const int block = (int) (sr * 0.4), step = (int) (sr * 0.1);
    std::vector<double> blocks;
    double running = 0.0;
    for (int i = 0; i < block; ++i) running += power[(size_t) i];
    for (int start = 0; start + block <= n; start += step)
    {
        if (start > 0)
        {
            for (int i = start - step; i < start; ++i) running -= power[(size_t) i];
            for (int i = start + block - step; i < start + block; ++i) running += power[(size_t) i];
        }
        blocks.push_back (running / block);
    }
    auto lufsOf = [] (double ms) { return -0.691 + 10.0 * std::log10 (juce::jmax (1e-12, ms)); };
    std::vector<double> gated;
    for (auto b : blocks)
        if (lufsOf (b) > -70.0) gated.push_back (b);
    if (gated.empty()) return -100.0f;
    const double absMean = std::accumulate (gated.begin(), gated.end(), 0.0) / (double) gated.size();
    const double relGate = lufsOf (absMean) - 10.0;
    double sum = 0.0;
    int count = 0;
    for (auto b : gated)
        if (lufsOf (b) > relGate) { sum += b; ++count; }
    return count > 0 ? (float) lufsOf (sum / count) : -100.0f;
}

juce::MemoryBlock Waveform::toBlob() const
{
    juce::MemoryBlock mb;
    juce::MemoryOutputStream out (mb, false);
    out.writeDouble (pointsPerSecond);
    out.writeInt ((int) points.size());
    out.write (points.data(), points.size() * sizeof (WavePoint));
    return mb;
}

Waveform Waveform::fromBlob (const juce::MemoryBlock& mb)
{
    Waveform w;
    if (mb.getSize() < 12) return w;
    juce::MemoryInputStream in (mb, false);
    w.pointsPerSecond = in.readDouble();
    const int count = in.readInt();
    if (count <= 0 || (size_t) count * sizeof (WavePoint) > mb.getSize()) return w;
    w.points.resize ((size_t) count);
    in.read (w.points.data(), (int) (w.points.size() * sizeof (WavePoint)));
    return w;
}

Waveform computeWaveform (const juce::AudioBuffer<float>& audio, double sr, double pps)
{
    Waveform w;
    w.pointsPerSecond = pps;
    const int n = audio.getNumSamples();
    const int per = juce::jmax (1, (int) std::round (sr / pps));
    w.pointsPerSecond = sr / per;
    const auto mono = toMono (audio);
    dsp::Biquad lowLp, highHp, midLp, midHp;
    lowLp.setup (dsp::Biquad::LowPass, sr, 200.0, 0.7071);
    highHp.setup (dsp::Biquad::HighPass, sr, 3000.0, 0.7071);
    midLp.setup (dsp::Biquad::LowPass, sr, 3000.0, 0.7071);
    midHp.setup (dsp::Biquad::HighPass, sr, 200.0, 0.7071);

    std::vector<std::array<float, 4>> raw;
    raw.reserve ((size_t) (n / per + 1));
    float maxes[4] { 1e-6f, 1e-6f, 1e-6f, 1e-6f };
    for (int start = 0; start < n; start += per)
    {
        std::array<float, 4> p {};
        const int end = juce::jmin (n, start + per);
        for (int i = start; i < end; ++i)
        {
            const float x = mono[(size_t) i];
            p[0] = juce::jmax (p[0], std::abs (lowLp.tick (x, 0)));
            p[1] = juce::jmax (p[1], std::abs (midHp.tick (midLp.tick (x, 0), 0)));
            p[2] = juce::jmax (p[2], std::abs (highHp.tick (x, 0)));
            p[3] = juce::jmax (p[3], std::abs (x));
        }
        for (int k = 0; k < 4; ++k) maxes[k] = juce::jmax (maxes[k], p[(size_t) k]);
        raw.push_back (p);
    }
    w.points.resize (raw.size());
    const float overall = maxes[3];
    for (size_t i = 0; i < raw.size(); ++i)
    {
        auto toByte = [] (float v, float norm) { return (juce::uint8) juce::jlimit (0, 255, (int) std::lround (255.0f * v / norm)); };
        w.points[i] = { toByte (raw[i][0], overall), toByte (raw[i][1], overall), toByte (raw[i][2], overall), toByte (raw[i][3], overall) };
    }
    return w;
}

std::vector<int> detectTransients (const std::vector<float>& mono, double sr, float sensitivity, double minSpacingSec)
{
    std::vector<int> hits;
    double hopSec = 0.0;
    const int hop = juce::jmax (32, (int) std::round (sr * 0.0029));
    auto env = onsetEnvelope (mono, sr, 9, hop, hopSec);
    if (env.empty()) return hits;
    const float peak = *std::max_element (env.begin(), env.end());
    if (peak <= 0.0f) return hits;
    const float threshold = peak * (0.05f + 0.5f * (1.0f - juce::jlimit (0.0f, 1.0f, sensitivity)));
    const int minSpacing = juce::jmax (1, (int) std::round (minSpacingSec / hopSec));
    int last = -minSpacing;
    for (int i = 1; i + 1 < (int) env.size(); ++i)
    {
        const float v = env[(size_t) i];
        if (v >= threshold && v >= env[(size_t) i - 1] && v > env[(size_t) i + 1] && i - last >= minSpacing)
        {
            // the flux peak comes when the hit is inside the window: map to the window start + a little
            const int sample = juce::jmax (0, (int) (i * hop + (1 << 9) * 0.5) - hop);
            hits.push_back (sample);
            last = i;
        }
    }
    // refine each hit to the actual rise in amplitude (search back a few ms for the quiet point)
    for (auto& h : hits)
    {
        const int from = juce::jmax (0, h - (int) (sr * 0.01));
        const int to = juce::jmin ((int) mono.size() - 1, h + (int) (sr * 0.01));
        float peakAmp = 0.0f;
        for (int i = from; i <= to; ++i) peakAmp = juce::jmax (peakAmp, std::abs (mono[(size_t) i]));
        for (int i = from; i <= to; ++i)
            if (std::abs (mono[(size_t) i]) > peakAmp * 0.25f) { h = juce::jmax (0, i - (int) (sr * 0.001)); break; }
    }
    return hits;
}

TrackAnalysis analyseTrack (const juce::AudioBuffer<float>& audio, double sr, double minBpm, double maxBpm)
{
    TrackAnalysis a;
    a.durationSec = audio.getNumSamples() / sr;
    const auto mono = toMono (audio);
    a.tempo = detectTempo (mono, sr, minBpm, maxBpm);
    a.key = detectKey (mono, sr);
    a.lufs = integratedLoudness (audio, sr);
    a.waveform = computeWaveform (audio, sr);
    return a;
}

bool keysCompatible (const MusicalKey& a, const MusicalKey& b)
{
    if (! a.isValid() || ! b.isValid()) return true;
    auto num = [] (const MusicalKey& k) { return k.toCamelot().dropLastCharacters (1).getIntValue(); };
    const int na = num (a), nb = num (b);
    if (a.minor == b.minor)
    {
        const int d = std::abs (na - nb);
        return d == 0 || d == 1 || d == 11;
    }
    return na == nb;
}

} // namespace merge::analysis
