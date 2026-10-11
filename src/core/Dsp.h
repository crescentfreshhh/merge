#pragma once

#include <cmath>
#include <algorithm>
#include <juce_core/juce_core.h>

namespace merge::dsp
{

constexpr double pi = 3.14159265358979323846;

inline float dbToGain (float db) noexcept { return db <= -100.0f ? 0.0f : std::pow (10.0f, db * 0.05f); }
inline float gainToDb (float g) noexcept { return g <= 1.0e-5f ? -100.0f : 20.0f * std::log10 (g); }

/** Constant-power pan law, pan in [-1, 1]. */
inline void panGains (float pan, float& left, float& right) noexcept
{
    const float a = (juce::jlimit (-1.0f, 1.0f, pan) + 1.0f) * 0.25f * (float) pi;
    left = std::cos (a) * 1.41421356f;
    right = std::sin (a) * 1.41421356f;
}

/** Fader position (0..1) to gain, with unity at ~0.8 and +6 dB at the top. */
inline float faderToGain (float pos) noexcept
{
    if (pos <= 0.0f) return 0.0f;
    const float db = pos >= 0.8f ? (pos - 0.8f) / 0.2f * 6.0f : -60.0f * std::pow (1.0f - pos / 0.8f, 2.0f);
    return dbToGain (db);
}

inline float softClip (float x) noexcept
{
    if (x > 1.5f) return 1.0f;
    if (x < -1.5f) return -1.0f;
    return x - (4.0f / 27.0f) * x * x * x;
}

/** Zavalishin / Cytomic topology-preserving state variable filter. */
struct Svf
{
    float g = 0, k = 1, a1 = 0, a2 = 0, a3 = 0;
    float ic1 = 0, ic2 = 0;

    void setup (double sampleRate, float cutoff, float q) noexcept
    {
        cutoff = juce::jlimit (10.0f, (float) (sampleRate * 0.49), cutoff);
        g = (float) std::tan (pi * cutoff / sampleRate);
        k = 1.0f / juce::jmax (0.05f, q);
        a1 = 1.0f / (1.0f + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }

    void reset() noexcept { ic1 = ic2 = 0; }

    struct Out { float low, band, high; };

    inline Out tick (float v0) noexcept
    {
        const float v3 = v0 - ic2;
        const float v1 = a1 * ic1 + a2 * v3;
        const float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0f * v1 - ic1;
        ic2 = 2.0f * v2 - ic2;
        return { v2, v1, v0 - k * v1 - v2 };
    }
};

/** Band-limited oscillator using PolyBLEP. */
struct BlepOsc
{
    enum Shape { Saw = 0, Square, Triangle, Sine };
    double phase = 0.0;
    float triState = 0.0f;

    static inline float blep (double t, double dt) noexcept
    {
        if (t < dt) { t /= dt; return (float) (t + t - t * t - 1.0); }
        if (t > 1.0 - dt) { t = (t - 1.0) / dt; return (float) (t * t + t + t + 1.0); }
        return 0.0f;
    }

    inline float tick (int shape, double dt) noexcept
    {
        float v = 0.0f;
        switch (shape)
        {
            case Saw:
                v = (float) (2.0 * phase - 1.0) - blep (phase, dt);
                break;
            case Square:
            case Triangle:
            {
                v = phase < 0.5 ? 1.0f : -1.0f;
                v += blep (phase, dt);
                v -= blep (std::fmod (phase + 0.5, 1.0), dt);
                if (shape == Triangle)
                {
                    triState = (float) (dt * 4.0) * v + (1.0f - (float) dt * 0.5f) * triState;
                    v = triState;
                }
                break;
            }
            default:
                v = (float) std::sin (2.0 * pi * phase);
                break;
        }
        phase += dt;
        if (phase >= 1.0) phase -= 1.0;
        return v;
    }
};

/** Linear ADSR envelope (times in seconds). */
struct Adsr
{
    enum Stage { Idle, Attack, Decay, Sustain, Release };
    Stage stage = Idle;
    float level = 0.0f;
    float attackInc = 1.0f, decayInc = 1.0f, sustain = 1.0f, releaseInc = 1.0f;

    void set (double sr, float a, float d, float s, float r) noexcept
    {
        attackInc = 1.0f / (float) juce::jmax (1.0, a * sr);
        decayInc = 1.0f / (float) juce::jmax (1.0, d * sr);
        sustain = juce::jlimit (0.0f, 1.0f, s);
        releaseInc = 1.0f / (float) juce::jmax (1.0, r * sr);
    }

    void noteOn() noexcept { stage = Attack; }
    void noteOff() noexcept { if (stage != Idle) stage = Release; }
    void kill() noexcept { stage = Idle; level = 0.0f; }
    bool isActive() const noexcept { return stage != Idle; }

    inline float tick() noexcept
    {
        switch (stage)
        {
            case Attack:
                level += attackInc;
                if (level >= 1.0f) { level = 1.0f; stage = Decay; }
                break;
            case Decay:
                level -= decayInc * (1.0f - sustain);
                if (level <= sustain) { level = sustain; stage = Sustain; }
                break;
            case Sustain:
                break;
            case Release:
                level -= releaseInc;
                if (level <= 0.0f) { level = 0.0f; stage = Idle; }
                break;
            default:
                break;
        }
        return level;
    }
};

} // namespace merge::dsp

namespace merge::dsp
{

/** RBJ cookbook biquad with per-channel state (DF2T). Coefficients can be
    recomputed on the audio thread without allocating. */
struct Biquad
{
    enum Type { LowPass, HighPass, BandPass, Peak, LowShelf, HighShelf, AllPass };
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    float z1[2] { 0, 0 }, z2[2] { 0, 0 };

    void setup (Type type, double sr, double freq, double q, double gainDb = 0.0) noexcept
    {
        freq = juce::jlimit (5.0, sr * 0.49, freq);
        const double w0 = 2.0 * pi * freq / sr;
        const double cw = std::cos (w0), sw = std::sin (w0);
        const double alpha = sw / (2.0 * juce::jmax (0.01, q));
        const double A = std::pow (10.0, gainDb / 40.0);
        double B0 = 1, B1 = 0, B2 = 0, A0 = 1, A1 = 0, A2 = 0;
        switch (type)
        {
            case LowPass:  B0 = (1 - cw) / 2; B1 = 1 - cw; B2 = (1 - cw) / 2; A0 = 1 + alpha; A1 = -2 * cw; A2 = 1 - alpha; break;
            case HighPass: B0 = (1 + cw) / 2; B1 = -(1 + cw); B2 = (1 + cw) / 2; A0 = 1 + alpha; A1 = -2 * cw; A2 = 1 - alpha; break;
            case BandPass: B0 = alpha; B1 = 0; B2 = -alpha; A0 = 1 + alpha; A1 = -2 * cw; A2 = 1 - alpha; break;
            case AllPass:  B0 = 1 - alpha; B1 = -2 * cw; B2 = 1 + alpha; A0 = 1 + alpha; A1 = -2 * cw; A2 = 1 - alpha; break;
            case Peak:     B0 = 1 + alpha * A; B1 = -2 * cw; B2 = 1 - alpha * A; A0 = 1 + alpha / A; A1 = -2 * cw; A2 = 1 - alpha / A; break;
            case LowShelf:
            {
                const double s = 2 * std::sqrt (A) * alpha;
                B0 = A * ((A + 1) - (A - 1) * cw + s); B1 = 2 * A * ((A - 1) - (A + 1) * cw); B2 = A * ((A + 1) - (A - 1) * cw - s);
                A0 = (A + 1) + (A - 1) * cw + s; A1 = -2 * ((A - 1) + (A + 1) * cw); A2 = (A + 1) + (A - 1) * cw - s;
                break;
            }
            case HighShelf:
            {
                const double s = 2 * std::sqrt (A) * alpha;
                B0 = A * ((A + 1) + (A - 1) * cw + s); B1 = -2 * A * ((A - 1) + (A + 1) * cw); B2 = A * ((A + 1) + (A - 1) * cw - s);
                A0 = (A + 1) - (A - 1) * cw + s; A1 = 2 * ((A - 1) - (A + 1) * cw); A2 = (A + 1) - (A - 1) * cw - s;
                break;
            }
        }
        b0 = (float) (B0 / A0); b1 = (float) (B1 / A0); b2 = (float) (B2 / A0);
        a1 = (float) (A1 / A0); a2 = (float) (A2 / A0);
    }

    void reset() noexcept { z1[0] = z1[1] = z2[0] = z2[1] = 0; }

    inline float tick (float x, int ch) noexcept
    {
        const float y = b0 * x + z1[ch];
        z1[ch] = b1 * x - a1 * y + z2[ch];
        z2[ch] = b2 * x - a2 * y;
        return y;
    }

    void process (float* data, int n, int ch) noexcept
    {
        for (int i = 0; i < n; ++i)
            data[i] = tick (data[i], ch);
    }
};

/** 4th-order Linkwitz-Riley section (two cascaded Butterworth biquads). */
struct LinkwitzRiley
{
    Biquad a, b;
    void setup (bool highPass, double sr, double freq) noexcept
    {
        const auto t = highPass ? Biquad::HighPass : Biquad::LowPass;
        a.setup (t, sr, freq, 0.70710678);
        b.setup (t, sr, freq, 0.70710678);
    }
    void reset() noexcept { a.reset(); b.reset(); }
    inline float tick (float x, int ch) noexcept { return b.tick (a.tick (x, ch), ch); }
};

} // namespace merge::dsp
