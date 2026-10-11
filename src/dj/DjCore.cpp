#include "DjCore.h"
#include "Recorder.h"

namespace merge
{

DjCore::DjCore()
{
    for (int i = 0; i < kMaxDecks; ++i)
        decks.push_back (std::make_unique<Deck> (i));
}

void DjCore::prepare (double sr, int mb)
{
    sampleRate = sr;
    maxBlock = mb;
    for (auto& d : decks)
        d->prepare (sr, mb);
    deckBuffers.resize (kMaxDecks);
    for (auto& b : deckBuffers)
        b.setSize (2, mb);
    fxBuffer.setSize (2, mb);
    for (auto& c : channels)
    {
        c.lp200.setup (false, sr, 200.0);
        c.hp200.setup (true, sr, 200.0);
        c.lp2500.setup (false, sr, 2500.0);
        c.hp2500.setup (true, sr, 2500.0);
        c.ap2500.setup (dsp::Biquad::AllPass, sr, 2500.0, 0.70710678);
        c.lp200.reset(); c.hp200.reset(); c.lp2500.reset(); c.hp2500.reset(); c.ap2500.reset();
        c.filterL.reset(); c.filterR.reset();
        if (auto* fx = c.fx.load())
            fx->prepare (sr, mb);
    }
}

float DjCore::eqGain (float knob, bool kill) noexcept
{
    if (kill) return 0.0f;
    knob = juce::jlimit (0.0f, 1.0f, knob);
    if (knob >= 0.5f)
        return dsp::dbToGain ((knob - 0.5f) * 12.0f); // up to +6 dB
    const float x = knob / 0.5f;
    return x * x * x; // to -inf (full kill at 0)
}

void DjCore::crossfaderGains (float x, float curve, float& a, float& b) noexcept
{
    x = juce::jlimit (-1.0f, 1.0f, x);
    // smooth: dipless constant-power style; sharp: cut near the ends
    const float smoothA = x <= 0.0f ? 1.0f : std::cos (x * juce::MathConstants<float>::halfPi);
    const float smoothB = x >= 0.0f ? 1.0f : std::cos (-x * juce::MathConstants<float>::halfPi);
    const float sharpA = x < 0.9f ? 1.0f : juce::jmax (0.0f, (1.0f - x) / 0.1f);
    const float sharpB = x > -0.9f ? 1.0f : juce::jmax (0.0f, (1.0f + x) / 0.1f);
    a = smoothA + (sharpA - smoothA) * curve;
    b = smoothB + (sharpB - smoothB) * curve;
}

MasterClock DjCore::computeClock()
{
    MasterClock clock;
    const int nd = juce::jlimit (1, kMaxDecks, numDecks.load());
    auto usable = [this] (int i) { return decks[(size_t) i]->isPlaying() && decks[(size_t) i]->hasGrid(); };

    int m = masterDeckPref.load();
    if (m >= nd || (m >= 0 && ! decks[(size_t) m]->hasGrid()))
        m = -1;
    if (m < 0)
    {
        if (autoMaster >= 0 && autoMaster < nd && usable (autoMaster))
            m = autoMaster;
        else
            for (int i = 0; i < nd; ++i)
                if (usable (i) && ! decks[(size_t) i]->sync.load()) { m = i; break; }
        if (m < 0)
            for (int i = 0; i < nd; ++i)
                if (usable (i)) { m = i; break; }
        autoMaster = m;
    }
    if (m >= 0)
    {
        auto& d = *decks[(size_t) m];
        clock.masterDeck = m;
        const double eff = d.effectiveBpm.load();
        clock.bpm = eff > 0 ? eff : d.trackBpm();
        clock.beat = d.audibleBeat();
    }
    currentMasterDeck.store (m);
    clockOut.valid.store (clock.valid() && decks[(size_t) juce::jmax (0, m)]->isPlaying());
    clockOut.bpm.store (clock.bpm);
    clockOut.beatPhase.store (clock.beat);
    return clock;
}

void DjCore::processChannel (int index, juce::AudioBuffer<float>& buf, int n, double bpm)
{
    auto& c = channels[(size_t) index];
    float* L = buf.getWritePointer (0);
    float* R = buf.getWritePointer (1);

    // trim
    const float trim = dsp::dbToGain (c.trimDb.load() + c.autoGainDb.load());
    buf.applyGain (0, n, trim);

    // 3-band isolator EQ (Linkwitz-Riley 4th order, flat when all gains are 1)
    const float tHigh = eqGain (c.eqHigh.load(), c.killHigh.load());
    const float tMid = eqGain (c.eqMid.load(), c.killMid.load());
    const float tLow = eqGain (c.eqLow.load(), c.killLow.load());
    const float sh = c.gHigh, sm = c.gMid, sl = c.gLow;
    for (int ch = 0; ch < 2; ++ch)
    {
        float* x = ch == 0 ? L : R;
        for (int i = 0; i < n; ++i)
        {
            const float t = (float) i / (float) n;
            const float gh = sh + (tHigh - sh) * t, gm = sm + (tMid - sm) * t, gl = sl + (tLow - sl) * t;
            const float low = c.ap2500.tick (c.lp200.tick (x[i], ch), ch);
            const float rest = c.hp200.tick (x[i], ch);
            const float mid = c.lp2500.tick (rest, ch);
            const float high = c.hp2500.tick (rest, ch);
            x[i] = low * gl + mid * gm + high * gh;
        }
    }
    c.gHigh = tHigh; c.gMid = tMid; c.gLow = tLow;

    // filter knob
    const float f = c.filter.load();
    c.lastFilter += (f - c.lastFilter) * 0.3f;
    if (std::abs (c.lastFilter) > 0.02f)
    {
        const bool hp = c.lastFilter > 0.0f;
        const float amt = std::abs (c.lastFilter);
        const float cutoff = hp ? 20.0f * std::pow (2.0f, amt * 10.0f) : 20000.0f * std::pow (2.0f, -amt * 10.0f);
        c.filterL.setup (sampleRate, cutoff, 1.1f);
        c.filterR.setup (sampleRate, cutoff, 1.1f);
        for (int i = 0; i < n; ++i)
        {
            const auto a = c.filterL.tick (L[i]);
            const auto b = c.filterR.tick (R[i]);
            L[i] = hp ? a.high : a.low;
            R[i] = hp ? b.high : b.low;
        }
    }
    else
    {
        c.filterL.reset();
        c.filterR.reset();
    }

    // FX unit
    if (auto* fx = c.fx.load())
    {
        if (c.fxOn.load() && fx->preparedRate > 0)
        {
            fxBuffer.copyFrom (0, 0, buf, 0, 0, n);
            fxBuffer.copyFrom (1, 0, buf, 1, 0, n);
            fx->process (fxBuffer, n, { bpm > 0 ? bpm : 120.0, true });
            const float amt = juce::jlimit (0.0f, 1.0f, c.fxAmount.load());
            for (int ch = 0; ch < 2; ++ch)
            {
                auto* d = buf.getWritePointer (ch);
                auto* w = fxBuffer.getReadPointer (ch);
                for (int i = 0; i < n; ++i)
                    d[i] += (w[i] - d[i]) * amt;
            }
        }
    }

    const float pl = buf.getMagnitude (0, 0, n), pr = buf.getMagnitude (1, 0, n);
    if (pl > c.peakL.load()) c.peakL.store (pl);
    if (pr > c.peakR.load()) c.peakR.store (pr);
}

void DjCore::process (juce::AudioBuffer<float>& master, juce::AudioBuffer<float>& cue, int n)
{
    const auto clock = computeClock();
    const int nd = juce::jlimit (1, kMaxDecks, numDecks.load());

    float xa, xb;
    crossfaderGains (crossfader.load(), crossfaderCurve.load(), xa, xb);
    auto* rec = recorder.load();

    juce::AudioBuffer<float> djMaster; // view into fxBuffer is not safe here; accumulate directly
    for (int i = 0; i < kMaxDecks; ++i)
    {
        auto& buf = deckBuffers[(size_t) i];
        auto& d = *decks[(size_t) i];
        d.process (buf, n, clock);
        if (i >= nd && ! d.isPlaying())
            continue;

        processChannel (i, buf, n, d.effectiveBpm.load());
        auto& c = channels[(size_t) i];

        if (c.cue.load())
        {
            cue.addFrom (0, 0, buf, 0, 0, n);
            cue.addFrom (1, 0, buf, 1, 0, n);
        }

        const float fpos = juce::jlimit (0.0f, 1.0f, c.fader.load());
        const int assign = c.xfAssign.load();
        const float xg = assign == 0 ? xa : (assign == 2 ? xb : 1.0f);
        const float g = fpos * fpos * xg;
        buf.applyGainRamp (0, 0, n, c.gOut, g);
        buf.applyGainRamp (1, 0, n, c.gOut, g);
        c.gOut = g;

        if (rec != nullptr)
            rec->captureDeck (i, buf, n);

        master.addFrom (0, 0, buf, 0, 0, n);
        master.addFrom (1, 0, buf, 1, 0, n);
    }

    const float mv = dsp::faderToGain (masterVolume.load());
    master.applyGain (0, n, mv);
    const float pl = master.getMagnitude (0, 0, n), pr = master.getMagnitude (1, 0, n);
    if (pl > masterPeakL.load()) masterPeakL.store (pl);
    if (pr > masterPeakR.load()) masterPeakR.store (pr);

    // headphones: blend cue bus with master
    const float mix = juce::jlimit (0.0f, 1.0f, cueMix.load());
    const float cv = dsp::faderToGain (cueVolume.load());
    for (int ch = 0; ch < 2; ++ch)
    {
        auto* c = cue.getWritePointer (ch);
        auto* m = master.getReadPointer (ch);
        for (int i = 0; i < n; ++i)
            c[i] = (c[i] * (1.0f - mix) + m[i] * mix) * cv;
    }
}

void DjCore::addBeatToCue (const juce::AudioBuffer<float>& beat, juce::AudioBuffer<float>& cue, int n)
{
    if (! beatCue.load()) return;
    const float cv = dsp::faderToGain (cueVolume.load()) * (1.0f - juce::jlimit (0.0f, 1.0f, cueMix.load()));
    cue.addFrom (0, 0, beat, 0, 0, n, cv);
    cue.addFrom (1, 0, beat, 1, 0, n, cv);
}

} // namespace merge
