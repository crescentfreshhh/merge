#include "Effect.h"
#include "core/Dsp.h"
#include <juce_dsp/juce_dsp.h>

namespace merge
{

namespace
{
    template <typename T>
    Effect::Ptr cloneWithParams (const Effect& src)
    {
        auto* c = new T();
        c->params.copyFrom (src.params);
        c->bypassed.store (src.bypassed.load());
        return Effect::Ptr (c);
    }

    const juce::StringArray beatDivisionNames { "1/32", "1/16", "1/16 T", "1/8 T", "1/8", "1/8 D", "1/4", "1/4 D", "1/2", "1 bar" };
    const double beatDivisionValues[] { 0.125, 0.25, 1.0 / 6.0, 1.0 / 3.0, 0.5, 0.75, 1.0, 1.5, 2.0, 4.0 };

    double divisionBeats (int index)
    {
        return beatDivisionValues[juce::jlimit (0, beatDivisionNames.size() - 1, index)];
    }

    inline void wetDry (float* dst, const float* wet, float mix, int n)
    {
        for (int i = 0; i < n; ++i)
            dst[i] = dst[i] + (wet[i] - dst[i]) * mix;
    }
}

//==============================================================================
class DelayEffect : public Effect
{
public:
    DelayEffect()
    {
        params.addChoice ("time", "Time", beatDivisionNames, 5);
        params.add ("feedback", "Feedback", 0.0f, 0.95f, 0.4f);
        params.add ("tone", "Tone", 500.0f, 18000.0f, 6000.0f, " Hz", 3000.0f);
        params.add ("mix", "Mix", 0.0f, 1.0f, 0.3f);
        params.addToggle ("pingpong", "Ping-Pong", true);
    }
    juce::String getTypeId() const override { return "delay"; }
    juce::String getName() const override { return "Delay"; }
    Ptr clone() const override { return cloneWithParams<DelayEffect> (*this); }

    void prepare (double sr, int) override
    {
        sampleRate = sr;
        preparedRate = sr;
        line.setSize (2, (int) (sr * 4.5) + 4);
        line.clear();
        writePos = 0;
        currentDelay = -1;
    }

    void reset() override { line.clear(); }

    void process (juce::AudioBuffer<float>& buf, int n, const EffectContext& ctx) override
    {
        if (line.getNumSamples() == 0) return;
        const int size = line.getNumSamples();
        const double target = juce::jlimit (16.0, (double) size - 4.0,
                                            divisionBeats (params.find ("time")->getChoice()) * 60.0 / juce::jmax (20.0, ctx.bpm) * sampleRate);
        if (currentDelay < 0) currentDelay = target;
        const float fb = params.find ("feedback")->get();
        const float mix = params.find ("mix")->get();
        const bool ping = params.find ("pingpong")->getBool();
        toneL.setup (dsp::Biquad::LowPass, sampleRate, params.find ("tone")->get(), 0.707);
        toneR.b0 = toneL.b0; toneR.b1 = toneL.b1; toneR.b2 = toneL.b2; toneR.a1 = toneL.a1; toneR.a2 = toneL.a2;

        float* L = buf.getWritePointer (0);
        float* R = buf.getWritePointer (1);
        float* dl = line.getWritePointer (0);
        float* dr = line.getWritePointer (1);

        for (int i = 0; i < n; ++i)
        {
            currentDelay += (target - currentDelay) * 0.0005;
            double rp = writePos - currentDelay;
            while (rp < 0) rp += size;
            const int i0 = (int) rp;
            const int i1 = (i0 + 1) % size;
            const float f = (float) (rp - i0);
            const float yl = dl[i0] + (dl[i1] - dl[i0]) * f;
            const float yr = dr[i0] + (dr[i1] - dr[i0]) * f;
            const float inL = L[i], inR = R[i];
            const float fl = toneL.tick (yl, 0), fr = toneR.tick (yr, 0);
            if (ping)
            {
                dl[writePos] = (inL + inR) * 0.5f + fr * fb;
                dr[writePos] = fl * fb;
            }
            else
            {
                dl[writePos] = inL + fl * fb;
                dr[writePos] = inR + fr * fb;
            }
            writePos = (writePos + 1) % size;
            L[i] = inL + yl * mix;
            R[i] = inR + yr * mix;
        }
    }

private:
    double sampleRate = 44100;
    juce::AudioBuffer<float> line;
    int writePos = 0;
    double currentDelay = -1;
    dsp::Biquad toneL, toneR;
};

//==============================================================================
class ReverbEffect : public Effect
{
public:
    ReverbEffect()
    {
        params.add ("size", "Size", 0.0f, 1.0f, 0.6f);
        params.add ("damping", "Damping", 0.0f, 1.0f, 0.45f);
        params.add ("width", "Width", 0.0f, 1.0f, 1.0f);
        params.add ("mix", "Mix", 0.0f, 1.0f, 0.25f);
    }
    juce::String getTypeId() const override { return "reverb"; }
    juce::String getName() const override { return "Reverb"; }
    Ptr clone() const override { return cloneWithParams<ReverbEffect> (*this); }
    void prepare (double sr, int) override { reverb.setSampleRate (sr); reverb.reset(); preparedRate = sr; }
    void reset() override { reverb.reset(); }
    void process (juce::AudioBuffer<float>& buf, int n, const EffectContext&) override
    {
        juce::Reverb::Parameters p;
        p.roomSize = params.find ("size")->get();
        p.damping = params.find ("damping")->get();
        p.width = params.find ("width")->get();
        const float mix = params.find ("mix")->get();
        p.wetLevel = mix * 0.6f;
        p.dryLevel = 1.0f - mix * 0.5f;
        reverb.setParameters (p);
        reverb.processStereo (buf.getWritePointer (0), buf.getWritePointer (1), n);
    }

private:
    juce::Reverb reverb;
};

//==============================================================================
class FilterEffect : public Effect
{
public:
    FilterEffect()
    {
        params.addChoice ("mode", "Mode", { "Low-pass", "High-pass", "Band-pass" }, 0);
        params.add ("cutoff", "Cutoff", 20.0f, 20000.0f, 2000.0f, " Hz", 1000.0f);
        params.add ("reso", "Resonance", 0.5f, 10.0f, 0.9f);
    }
    juce::String getTypeId() const override { return "filter"; }
    juce::String getName() const override { return "Filter"; }
    Ptr clone() const override { return cloneWithParams<FilterEffect> (*this); }
    void prepare (double sr, int) override { sampleRate = sr; preparedRate = sr; l.reset(); r.reset(); smoothed = -1; }
    void reset() override { l.reset(); r.reset(); }
    void process (juce::AudioBuffer<float>& buf, int n, const EffectContext&) override
    {
        const int mode = params.find ("mode")->getChoice();
        const float target = params.find ("cutoff")->get();
        if (smoothed < 0) smoothed = target;
        const float q = params.find ("reso")->get();
        float* L = buf.getWritePointer (0);
        float* R = buf.getWritePointer (1);
        for (int start = 0; start < n; start += 32)
        {
            smoothed += (target - smoothed) * 0.25f;
            l.setup (sampleRate, smoothed, q);
            r.setup (sampleRate, smoothed, q);
            const int end = juce::jmin (n, start + 32);
            for (int i = start; i < end; ++i)
            {
                const auto a = l.tick (L[i]);
                const auto b = r.tick (R[i]);
                L[i] = mode == 0 ? a.low : (mode == 1 ? a.high : a.band);
                R[i] = mode == 0 ? b.low : (mode == 1 ? b.high : b.band);
            }
        }
    }

private:
    double sampleRate = 44100;
    dsp::Svf l, r;
    float smoothed = -1;
};

//==============================================================================
class FlangerEffect : public Effect
{
public:
    FlangerEffect()
    {
        params.add ("rate", "Rate", 0.02f, 5.0f, 0.25f, " Hz", 0.5f);
        params.add ("depth", "Depth", 0.0f, 1.0f, 0.7f);
        params.add ("feedback", "Feedback", -0.95f, 0.95f, 0.6f);
        params.add ("mix", "Mix", 0.0f, 1.0f, 0.5f);
    }
    juce::String getTypeId() const override { return "flanger"; }
    juce::String getName() const override { return "Flanger"; }
    Ptr clone() const override { return cloneWithParams<FlangerEffect> (*this); }
    void prepare (double sr, int) override
    {
        sampleRate = sr;
        preparedRate = sr;
        line.setSize (2, (int) (sr * 0.03) + 4);
        line.clear();
        writePos = 0;
    }
    void reset() override { line.clear(); }
    void process (juce::AudioBuffer<float>& buf, int n, const EffectContext&) override
    {
        if (line.getNumSamples() == 0) return;
        const int size = line.getNumSamples();
        const double rate = params.find ("rate")->get();
        const float depth = params.find ("depth")->get();
        const float fb = params.find ("feedback")->get();
        const float mix = params.find ("mix")->get();
        for (int ch = 0; ch < 2; ++ch)
        {
            float* x = buf.getWritePointer (ch);
            float* d = line.getWritePointer (ch);
            double ph = phase + ch * 0.25;
            int wp = writePos;
            for (int i = 0; i < n; ++i)
            {
                const double lfo = 0.5 + 0.5 * std::sin (2.0 * dsp::pi * ph);
                const double delay = (0.0007 + 0.0055 * depth * lfo) * sampleRate;
                double rp = wp - delay;
                while (rp < 0) rp += size;
                const int i0 = (int) rp;
                const int i1 = (i0 + 1) % size;
                const float f = (float) (rp - i0);
                const float y = d[i0] + (d[i1] - d[i0]) * f;
                d[wp] = x[i] + y * fb;
                x[i] = x[i] * (1.0f - mix * 0.5f) + y * mix;
                wp = (wp + 1) % size;
                ph += rate / sampleRate;
            }
        }
        phase = std::fmod (phase + rate * n / sampleRate, 1.0);
        writePos = (writePos + n) % size;
    }

private:
    double sampleRate = 44100, phase = 0;
    juce::AudioBuffer<float> line;
    int writePos = 0;
};

//==============================================================================
class PhaserEffect : public Effect
{
public:
    PhaserEffect()
    {
        params.add ("rate", "Rate", 0.02f, 5.0f, 0.4f, " Hz", 0.5f);
        params.add ("depth", "Depth", 0.0f, 1.0f, 0.7f);
        params.add ("centre", "Centre", 200.0f, 5000.0f, 1000.0f, " Hz", 1000.0f);
        params.add ("feedback", "Feedback", -0.9f, 0.9f, 0.5f);
        params.add ("mix", "Mix", 0.0f, 1.0f, 0.5f);
    }
    juce::String getTypeId() const override { return "phaser"; }
    juce::String getName() const override { return "Phaser"; }
    Ptr clone() const override { return cloneWithParams<PhaserEffect> (*this); }
    void prepare (double sr, int maxBlock) override
    {
        phaser.prepare ({ sr, (juce::uint32) juce::jmax (1, maxBlock), 2 });
        preparedRate = sr;
    }
    void reset() override { phaser.reset(); }
    void process (juce::AudioBuffer<float>& buf, int n, const EffectContext&) override
    {
        if (preparedRate <= 0) return;
        phaser.setRate (params.find ("rate")->get());
        phaser.setDepth (params.find ("depth")->get());
        phaser.setCentreFrequency (params.find ("centre")->get());
        phaser.setFeedback (params.find ("feedback")->get());
        phaser.setMix (params.find ("mix")->get());
        juce::dsp::AudioBlock<float> block (buf);
        auto sub = block.getSubsetChannelBlock (0, 2).getSubBlock (0, (size_t) n);
        phaser.process (juce::dsp::ProcessContextReplacing<float> (sub));
    }

private:
    juce::dsp::Phaser<float> phaser;
};

//==============================================================================
class BitcrushEffect : public Effect
{
public:
    BitcrushEffect()
    {
        params.add ("bits", "Bits", 2.0f, 16.0f, 8.0f);
        params.add ("downsample", "Downsample", 1.0f, 32.0f, 4.0f, "x", 6.0f);
        params.add ("mix", "Mix", 0.0f, 1.0f, 1.0f);
    }
    juce::String getTypeId() const override { return "bitcrush"; }
    juce::String getName() const override { return "Bitcrush"; }
    Ptr clone() const override { return cloneWithParams<BitcrushEffect> (*this); }
    void prepare (double sr, int) override { preparedRate = sr; }
    void process (juce::AudioBuffer<float>& buf, int n, const EffectContext&) override
    {
        const float q = std::pow (2.0f, params.find ("bits")->get() - 1.0f);
        const int ds = juce::jmax (1, (int) params.find ("downsample")->get());
        const float mix = params.find ("mix")->get();
        for (int ch = 0; ch < 2; ++ch)
        {
            float* x = buf.getWritePointer (ch);
            for (int i = 0; i < n; ++i)
            {
                if (counter[ch]++ % ds == 0)
                    held[ch] = std::round (x[i] * q) / q;
                x[i] += (held[ch] - x[i]) * mix;
            }
        }
    }

private:
    float held[2] { 0, 0 };
    int counter[2] { 0, 0 };
};

//==============================================================================
class DistortionEffect : public Effect
{
public:
    DistortionEffect()
    {
        params.add ("drive", "Drive", 0.0f, 1.0f, 0.4f);
        params.add ("tone", "Tone", 500.0f, 16000.0f, 6000.0f, " Hz", 3000.0f);
        params.add ("mix", "Mix", 0.0f, 1.0f, 1.0f);
    }
    juce::String getTypeId() const override { return "distortion"; }
    juce::String getName() const override { return "Distortion"; }
    Ptr clone() const override { return cloneWithParams<DistortionEffect> (*this); }
    void prepare (double sr, int) override { sampleRate = sr; preparedRate = sr; tone.reset(); }
    void process (juce::AudioBuffer<float>& buf, int n, const EffectContext&) override
    {
        const float drive = 1.0f + params.find ("drive")->get() * 30.0f;
        const float norm = 1.0f / std::tanh (drive);
        const float mix = params.find ("mix")->get();
        tone.setup (dsp::Biquad::LowPass, sampleRate, params.find ("tone")->get(), 0.707);
        for (int ch = 0; ch < 2; ++ch)
        {
            float* x = buf.getWritePointer (ch);
            for (int i = 0; i < n; ++i)
            {
                const float y = tone.tick (std::tanh (x[i] * drive) * norm, ch);
                x[i] += (y * 0.8f - x[i]) * mix;
            }
        }
    }

private:
    double sampleRate = 44100;
    dsp::Biquad tone;
};

//==============================================================================
class CompressorEffect : public Effect
{
public:
    CompressorEffect()
    {
        params.add ("threshold", "Threshold", -60.0f, 0.0f, -18.0f, " dB");
        params.add ("ratio", "Ratio", 1.0f, 20.0f, 4.0f, ":1", 4.0f);
        params.add ("attack", "Attack", 0.1f, 100.0f, 10.0f, " ms", 10.0f);
        params.add ("release", "Release", 10.0f, 1000.0f, 120.0f, " ms", 150.0f);
        params.add ("makeup", "Makeup", 0.0f, 24.0f, 4.0f, " dB");
    }
    juce::String getTypeId() const override { return "compressor"; }
    juce::String getName() const override { return "Compressor"; }
    Ptr clone() const override { return cloneWithParams<CompressorEffect> (*this); }
    void prepare (double sr, int maxBlock) override
    {
        comp.prepare ({ sr, (juce::uint32) juce::jmax (1, maxBlock), 2 });
        preparedRate = sr;
    }
    void reset() override { comp.reset(); }
    void process (juce::AudioBuffer<float>& buf, int n, const EffectContext&) override
    {
        if (preparedRate <= 0) return;
        comp.setThreshold (params.find ("threshold")->get());
        comp.setRatio (params.find ("ratio")->get());
        comp.setAttack (params.find ("attack")->get());
        comp.setRelease (params.find ("release")->get());
        juce::dsp::AudioBlock<float> block (buf);
        auto sub = block.getSubsetChannelBlock (0, 2).getSubBlock (0, (size_t) n);
        comp.process (juce::dsp::ProcessContextReplacing<float> (sub));
        sub.multiplyBy (dsp::dbToGain (params.find ("makeup")->get()));
    }

private:
    juce::dsp::Compressor<float> comp;
};

//==============================================================================
class Eq3Effect : public Effect
{
public:
    Eq3Effect()
    {
        params.add ("low", "Low", -15.0f, 15.0f, 0.0f, " dB");
        params.add ("mid", "Mid", -15.0f, 15.0f, 0.0f, " dB");
        params.add ("midfreq", "Mid Freq", 200.0f, 8000.0f, 1000.0f, " Hz", 1000.0f);
        params.add ("high", "High", -15.0f, 15.0f, 0.0f, " dB");
    }
    juce::String getTypeId() const override { return "eq3"; }
    juce::String getName() const override { return "EQ"; }
    Ptr clone() const override { return cloneWithParams<Eq3Effect> (*this); }
    void prepare (double sr, int) override { sampleRate = sr; preparedRate = sr; lo.reset(); mi.reset(); hi.reset(); }
    void process (juce::AudioBuffer<float>& buf, int n, const EffectContext&) override
    {
        lo.setup (dsp::Biquad::LowShelf, sampleRate, 120.0, 0.707, params.find ("low")->get());
        mi.setup (dsp::Biquad::Peak, sampleRate, params.find ("midfreq")->get(), 0.8, params.find ("mid")->get());
        hi.setup (dsp::Biquad::HighShelf, sampleRate, 8000.0, 0.707, params.find ("high")->get());
        for (int ch = 0; ch < 2; ++ch)
        {
            float* x = buf.getWritePointer (ch);
            for (int i = 0; i < n; ++i)
                x[i] = hi.tick (mi.tick (lo.tick (x[i], ch), ch), ch);
        }
    }

private:
    double sampleRate = 44100;
    dsp::Biquad lo, mi, hi;
};

//==============================================================================
/** Beat-synced loop roll: while active, repeats the last slice of audio. */
class BeatRollEffect : public Effect
{
public:
    BeatRollEffect()
    {
        params.addChoice ("length", "Length", beatDivisionNames, 1);
        params.addToggle ("active", "Roll", false);
    }
    juce::String getTypeId() const override { return "roll"; }
    juce::String getName() const override { return "Beat Roll"; }
    Ptr clone() const override { return cloneWithParams<BeatRollEffect> (*this); }
    void prepare (double sr, int) override
    {
        sampleRate = sr;
        preparedRate = sr;
        history.setSize (2, (int) (sr * 9.0));
        history.clear();
        writePos = 0;
        rolling = false;
    }
    void process (juce::AudioBuffer<float>& buf, int n, const EffectContext& ctx) override
    {
        if (history.getNumSamples() == 0) return;
        const int size = history.getNumSamples();
        const bool active = params.find ("active")->getBool();
        const int len = juce::jlimit (64, size / 2,
                                      (int) (divisionBeats (params.find ("length")->getChoice()) * 60.0 / juce::jmax (20.0, ctx.bpm) * sampleRate));
        if (active && ! rolling)
        {
            rolling = true;
            loopStart = writePos;
            loopLen = len;
            playPos = 0;
        }
        else if (! active)
        {
            rolling = false;
        }

        for (int i = 0; i < n; ++i)
        {
            for (int ch = 0; ch < 2; ++ch)
            {
                float* x = buf.getWritePointer (ch);
                if (! rolling || playPos < loopLen)
                    history.setSample (ch, writePos, x[i]);
                if (rolling && playPos >= loopLen)
                {
                    const int idx = (loopStart + (playPos % loopLen)) % size;
                    const int edge = juce::jmin (playPos % loopLen, loopLen - (playPos % loopLen));
                    const float fadeGain = juce::jmin (1.0f, edge / 64.0f);
                    x[i] = history.getSample (ch, idx) * (0.5f + 0.5f * fadeGain);
                }
            }
            if (rolling) ++playPos;
            if (! rolling || playPos <= loopLen)
                writePos = (writePos + 1) % size;
        }
    }

private:
    double sampleRate = 44100;
    juce::AudioBuffer<float> history;
    int writePos = 0, loopStart = 0, loopLen = 0, playPos = 0;
    bool rolling = false;
};

//==============================================================================
class LimiterEffect : public Effect
{
public:
    LimiterEffect()
    {
        params.add ("ceiling", "Ceiling", -12.0f, 0.0f, -0.3f, " dB");
        params.add ("release", "Release", 5.0f, 500.0f, 80.0f, " ms", 60.0f);
    }
    juce::String getTypeId() const override { return "limiter"; }
    juce::String getName() const override { return "Limiter"; }
    Ptr clone() const override { return cloneWithParams<LimiterEffect> (*this); }
    void prepare (double sr, int maxBlock) override
    {
        limiter.prepare ({ sr, (juce::uint32) juce::jmax (1, maxBlock), 2 });
        preparedRate = sr;
    }
    void reset() override { limiter.reset(); }
    void process (juce::AudioBuffer<float>& buf, int n, const EffectContext&) override
    {
        if (preparedRate <= 0) return;
        limiter.setThreshold (params.find ("ceiling")->get());
        limiter.setRelease (params.find ("release")->get());
        juce::dsp::AudioBlock<float> block (buf);
        auto sub = block.getSubsetChannelBlock (0, 2).getSubBlock (0, (size_t) n);
        limiter.process (juce::dsp::ProcessContextReplacing<float> (sub));
    }

private:
    juce::dsp::Limiter<float> limiter;
};

//==============================================================================
juce::StringArray builtInEffectTypes()
{
    return { "eq3", "filter", "compressor", "delay", "reverb", "distortion", "bitcrush", "flanger", "phaser", "roll", "limiter" };
}

juce::String builtInEffectName (const juce::String& typeId)
{
    if (auto fx = createBuiltInEffect (typeId))
        return fx->getName();
    return typeId;
}

Effect::Ptr createBuiltInEffect (const juce::String& id)
{
    Effect::Ptr fx;
    if (id == "delay") fx = new DelayEffect();
    else if (id == "reverb") fx = new ReverbEffect();
    else if (id == "filter") fx = new FilterEffect();
    else if (id == "flanger") fx = new FlangerEffect();
    else if (id == "phaser") fx = new PhaserEffect();
    else if (id == "bitcrush") fx = new BitcrushEffect();
    else if (id == "distortion") fx = new DistortionEffect();
    else if (id == "compressor") fx = new CompressorEffect();
    else if (id == "eq3") fx = new Eq3Effect();
    else if (id == "roll") fx = new BeatRollEffect();
    else if (id == "limiter") fx = new LimiterEffect();
    if (fx != nullptr)
        ReleasePool::get().add (fx);
    return fx;
}

} // namespace merge
