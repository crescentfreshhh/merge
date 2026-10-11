#include "BeatCore.h"
#include "core/Dsp.h"

namespace merge
{

BeatCore::BeatCore() = default;

void BeatCore::prepare (double sr, int maxBlockSize)
{
    sampleRate = sr;
    maxBlock = juce::jmax (32, maxBlockSize);
    channelBuffers.resize (kMaxChannels);
    channelMidi.resize (kMaxChannels);
    for (auto& b : channelBuffers)
        b.setSize (2, maxBlock);
    for (auto& m : channelMidi)
        m.ensureSize (4096);
    masterBuffer.setSize (2, maxBlock);
    clipBuffer.setSize (2, maxBlock);
}

double BeatCore::swingOffsetBeats() const
{
    // MPC swing: the second 16th of each 8th-note pair lands at swing% of the pair.
    const double s = juce::jlimit (50.0, 75.0, transport.swing.load());
    return (s - 50.0) / 100.0 * 0.5;
}

double BeatCore::patternLength (const BeatSnapshot& snap, int index) const
{
    if (index < 0 || index >= (int) snap.patterns.size())
        return 4.0;
    return juce::jmax (kStepBeats, snap.patterns[(size_t) index].lengthBeats);
}

void BeatCore::process (juce::AudioBuffer<float>& out, int numSamples)
{
    int offset = 0;
    while (offset < numSamples)
    {
        const int n = juce::jmin (maxBlock, numSamples - offset);
        processChunk (out, offset, n);
        offset += n;
    }
}

void BeatCore::addNoteOn (BeatSnapshot& snap, int channel, int note, float velocity, int offset)
{
    if (channel < 0 || channel >= (int) snap.channels.size() || channel >= kMaxChannels)
        return;
    auto& strip = *snap.channels[(size_t) channel];
    offset = juce::jlimit (0, maxBlock - 1, offset);

    const int group = strip.chokeGroup.load (std::memory_order_relaxed);
    if (group > 0)
        for (size_t o = 0; o < snap.channels.size() && o < (size_t) kMaxChannels; ++o)
            if ((int) o != channel && snap.channels[o]->chokeGroup.load (std::memory_order_relaxed) == group)
                channelMidi[o].addEvent (juce::MidiMessage::allSoundOff (1), offset);

    channelMidi[(size_t) channel].addEvent (juce::MidiMessage::noteOn (1, juce::jlimit (0, 127, note),
                                                                        juce::jlimit (0.0f, 1.0f, velocity)), offset);
    strip.triggerCount.fetch_add (1, std::memory_order_relaxed);
}

void BeatCore::addNoteOff (int channel, int note, int offset)
{
    if (channel < 0 || channel >= kMaxChannels)
        return;
    channelMidi[(size_t) channel].addEvent (juce::MidiMessage::noteOff (1, juce::jlimit (0, 127, note)),
                                            juce::jlimit (0, maxBlock - 1, offset));
}

void BeatCore::scheduleNoteOff (BeatSnapshot& snap, int channel, int note, juce::int64 atSample)
{
    if (numPendingOffs >= (int) pendingOffs.size() || channel < 0 || channel >= (int) snap.channels.size())
        return;
    pendingOffs[(size_t) numPendingOffs++] = { snap.channels[(size_t) channel].get(), note, atSample };
}

void BeatCore::flushNoteOffs (BeatSnapshot& snap, int n)
{
    const auto end = sampleCounter + n;
    for (int i = 0; i < numPendingOffs;)
    {
        auto& p = pendingOffs[(size_t) i];
        if (p.at < end)
        {
            for (size_t c = 0; c < snap.channels.size() && c < (size_t) kMaxChannels; ++c)
                if (snap.channels[c].get() == p.strip)
                {
                    addNoteOff ((int) c, p.note, (int) juce::jmax<juce::int64> (0, p.at - sampleCounter));
                    break;
                }
            pendingOffs[(size_t) i] = pendingOffs[(size_t) --numPendingOffs];
        }
        else
        {
            ++i;
        }
    }
}

void BeatCore::handleEvent (const BeatEvent& e, BeatSnapshot& snap)
{
    const int numCh = juce::jmin ((int) snap.channels.size(), kMaxChannels);
    switch (e.type)
    {
        case BeatEvent::NoteOn:
        {
            if (e.channel < 0 || e.channel >= numCh) break;
            const int note = e.note >= 0 ? e.note : (e.channel < (int) snap.stepNotes.size() ? snap.stepNotes[(size_t) e.channel] : 60);
            addNoteOn (snap, e.channel, note, e.velocity, 0);
            held[(size_t) e.channel] = { e.velocity, note };

            if (transport.playing.load() && transport.recording.load())
            {
                const int pat = transport.currentPattern.load();
                const double L = patternLength (snap, pat);
                const double local = std::fmod (juce::jmax (0.0, position), L);
                auto& strip = *snap.channels[(size_t) e.channel];
                if (strip.instrument != nullptr && strip.instrument->isOneShot())
                {
                    recorded.push ({ e.channel, pat, note, e.velocity, local, kStepBeats });
                    // If the hit rounds forward to a step that hasn't played yet, don't double-trigger it.
                    const double q = std::round (local / kStepBeats) * kStepBeats;
                    if (q > local + 1.0e-6)
                    {
                        const double spb = sampleRate * 60.0 / currentBpm;
                        suppress[(size_t) e.channel] = { std::fmod (q, L), sampleCounter + (juce::int64) ((q - local + 0.5 * kStepBeats) * spb), pat };
                    }
                }
                else
                {
                    recStarts[(size_t) e.channel][(size_t) juce::jlimit (0, 127, note)] = { local, e.velocity };
                }
            }
            break;
        }
        case BeatEvent::NoteOff:
        {
            if (e.channel < 0 || e.channel >= numCh) break;
            const int note = e.note >= 0 ? e.note : (e.channel < (int) snap.stepNotes.size() ? snap.stepNotes[(size_t) e.channel] : 60);
            addNoteOff (e.channel, note, 0);
            if (held[(size_t) e.channel].note == note)
                held[(size_t) e.channel].velocity = 0.0f;
            auto& rs = recStarts[(size_t) e.channel][(size_t) juce::jlimit (0, 127, note)];
            if (rs.beat >= 0.0)
            {
                const int pat = transport.currentPattern.load();
                const double L = patternLength (snap, pat);
                double end = std::fmod (juce::jmax (0.0, position), L);
                double len = end - rs.beat;
                if (len <= 0.0) len += L;
                recorded.push ({ e.channel, pat, note, rs.velocity, rs.beat, juce::jmax (1.0 / 32.0, len) });
                rs.beat = -1.0;
            }
            break;
        }
        case BeatEvent::Play:
        {
            if (! transport.playing.load())
            {
                position = transport.songMode.load() && transport.loopEnabled.load() ? transport.loopStart.load() : 0.0;
                if (e.value > 0.0) position = e.value;
                lastMetroBeat = -1.0;
            }
            transport.playing.store (true);
            break;
        }
        case BeatEvent::Stop:
        {
            transport.playing.store (false);
            position = 0.0;
            numPendingOffs = 0;
            for (auto& ch : snap.channels)
                if (ch->instrument != nullptr)
                    ch->instrument->allNotesOff();
            for (auto& h : held) h.velocity = 0.0f;
            for (auto& row : recStarts)
                for (auto& r : row) r.beat = -1.0;
            break;
        }
        case BeatEvent::Locate:
            position = juce::jmax (0.0, e.value);
            break;
        case BeatEvent::AllNotesOff:
        case BeatEvent::Panic:
            numPendingOffs = 0;
            for (auto& ch : snap.channels)
                if (ch->instrument != nullptr)
                    ch->instrument->allNotesOff();
            break;
    }
}

void BeatCore::emitPattern (BeatSnapshot& snap, int pi, double l0, double l1, double zeroBeat,
                            double segStart, int base, double spb)
{
    if (pi < 0 || pi >= (int) snap.patterns.size() || l1 <= l0)
        return;
    const auto& pat = snap.patterns[(size_t) pi];
    const double L = juce::jmax (kStepBeats, pat.lengthBeats);
    const double swingOff = swingOffsetBeats();
    const auto k0 = (juce::int64) std::floor (l0 / L);
    const auto k1 = (juce::int64) std::floor ((l1 - 1.0e-9) / L);

    for (auto k = k0; k <= k1; ++k)
    {
        const double a = juce::jmax (l0, k * L) - k * L;
        const double b = juce::jmin (l1, (k + 1) * L) - k * L;
        auto it = std::lower_bound (pat.events.begin(), pat.events.end(), a - swingOff - 1.0e-6,
                                    [] (const CompiledEvent& ev, double v) { return ev.beat < v; });
        for (; it != pat.events.end() && it->beat < b; ++it)
        {
            const auto& ev = *it;
            double eff = ev.beat + (ev.swing ? swingOff : 0.0);
            eff = juce::jmin (eff, L - 1.0e-6);
            if (eff < a || eff >= b)
                continue;
            if (ev.channel >= (int) snap.channels.size() || ev.channel >= kMaxChannels)
                continue;

            auto& sup = suppress[(size_t) ev.channel];
            if (sup.pattern == pi && std::abs (sup.beat - ev.beat) < 1.0e-6 && sampleCounter < sup.until)
            {
                sup.pattern = -1;
                continue;
            }

            const double songBeat = zeroBeat + k * L + eff;
            const int offset = juce::jmax (base, base + (int) std::floor ((songBeat - segStart) * spb + 1.0e-6));
            addNoteOn (snap, ev.channel, ev.note, ev.velocity, offset);
            const auto lenSamples = (juce::int64) std::llround (ev.length * spb);
            scheduleNoteOff (snap, ev.channel, ev.note, sampleCounter + offset + juce::jmax<juce::int64> (1, lenSamples));
        }
    }
}

void BeatCore::emitRange (BeatSnapshot& snap, double b0, double b1, int base, double spb)
{
    if (transport.songMode.load())
    {
        for (const auto& clip : snap.clips)
        {
            if (clip.lane >= 0 && clip.lane < (int) snap.laneMuted.size() && snap.laneMuted[(size_t) clip.lane])
                continue;
            const double end = clip.start + clip.length;
            if (end <= b0 || clip.start >= b1)
                continue;
            const double l0 = juce::jmax (b0, clip.start) - clip.start + clip.offset;
            const double l1 = juce::jmin (b1, end) - clip.start + clip.offset;
            emitPattern (snap, clip.pattern, l0, l1, clip.start - clip.offset, b0, base, spb);
        }
    }
    else
    {
        emitPattern (snap, transport.currentPattern.load(), b0, b1, 0.0, b0, base, spb);
    }
}

void BeatCore::renderAudioClips (BeatSnapshot& snap, double b0, int base, int count, double spb)
{
    if (! transport.songMode.load() || snap.audioClips.empty() || count <= 0)
        return;
    const double bps = 1.0 / spb;
    const double b1 = b0 + count * bps;
    const double secondsPerBeat = 60.0 / currentBpm;
    float* L = clipBuffer.getWritePointer (0);
    float* R = clipBuffer.getWritePointer (1);

    for (const auto& clip : snap.audioClips)
    {
        if (clip.sample == nullptr) continue;
        if (clip.lane >= 0 && clip.lane < (int) snap.laneMuted.size() && snap.laneMuted[(size_t) clip.lane])
            continue;
        const double end = clip.start + clip.length;
        if (end <= b0 || clip.start >= b1)
            continue;
        const auto* s = clip.sample.get();
        const bool stereo = s->getNumChannels() > 1;
        for (int i = 0; i < count; ++i)
        {
            const double beat = b0 + i * bps;
            if (beat < clip.start || beat >= end)
                continue;
            const double t = (beat - clip.start) * secondsPerBeat + clip.offsetSeconds;
            const double src = t * s->getSampleRate();
            if (src >= s->getNumFrames())
                continue;
            const float l = s->readLinear (0, src);
            const float r = stereo ? s->readLinear (1, src) : l;
            L[base + i] += l * clip.gain;
            R[base + i] += r * clip.gain;
        }
    }
}

void BeatCore::scheduleBlock (BeatSnapshot& snap, int n, double spb)
{
    const double bps = 1.0 / spb;
    int done = 0;
    int guard = 0;
    double emitFrom = -1.0; // after a wrap, also emit events in [wrapTo, position) (sub-sample overshoot)

    while (done < n && guard++ < 64)
    {
        const bool song = transport.songMode.load();
        double wrapAt, wrapTo = 0.0;
        if (song)
        {
            if (transport.loopEnabled.load() && transport.loopEnd.load() > transport.loopStart.load() + kStepBeats)
            {
                wrapAt = transport.loopEnd.load();
                wrapTo = transport.loopStart.load();
            }
            else
            {
                wrapAt = juce::jmax (4.0, snap.songLengthBeats);
            }
        }
        else
        {
            wrapAt = patternLength (snap, transport.currentPattern.load());
        }

        const int queued = transport.queuedPattern.load();
        double barBoundary = 1.0e12;
        if (! song && queued >= 0)
            barBoundary = (std::floor (position / 4.0 + 1.0e-9) + 1.0) * 4.0;

        if (position >= wrapAt)
            position = wrapTo + std::fmod (position - wrapAt, juce::jmax (kStepBeats, wrapAt - wrapTo));

        const double b0 = position;
        const double remaining = (n - done) * bps;
        const double boundary = juce::jmin (wrapAt, barBoundary);
        const bool hits = b0 + remaining >= boundary;
        const double segEnd = hits ? boundary : b0 + remaining;
        int segSamples = hits ? (int) std::ceil ((segEnd - b0) * spb - 1.0e-9) : n - done;
        segSamples = juce::jlimit (0, n - done, segSamples);

        emitRange (snap, emitFrom >= 0.0 ? juce::jmin (emitFrom, b0) : b0, segEnd, done, spb);
        emitFrom = -1.0;
        renderAudioClips (snap, b0, done, segSamples, spb);
        for (double k = std::ceil (b0 - 1.0e-9); k < segEnd && numMetro < (int) metroTriggers.size(); k += 1.0)
            metroTriggers[(size_t) numMetro++] = { done + (int) std::floor ((k - b0) * spb + 1.0e-6),
                                                   std::fmod (k, 4.0) < 0.5 };
        done += segSamples;
        position = b0 + segSamples * bps;

        if (hits)
        {
            const double overshoot = juce::jmax (0.0, position - boundary);
            if (barBoundary <= wrapAt && queued >= 0)
            {
                transport.currentPattern.store (queued);
                transport.queuedPattern.store (-1);
                position = overshoot;
                emitFrom = 0.0;
            }
            else
            {
                position = wrapTo + overshoot;
                emitFrom = wrapTo;
                if (! song && queued >= 0)
                {
                    transport.currentPattern.store (queued);
                    transport.queuedPattern.store (-1);
                }
            }
        }
    }
}

void BeatCore::runNoteRepeat (BeatSnapshot& snap, int n, double spb)
{
    const double interval = transport.noteRepeat.load();
    const double bps = 1.0 / spb;
    const bool playing = transport.playing.load();
    const double start = playing ? position : repeatClock;

    if (interval > 0.0)
    {
        const double end = start + n * bps;
        double t = std::ceil (start / interval - 1.0e-9) * interval;
        for (; t < end; t += interval)
        {
            if (t <= start + 1.0e-9)
                continue; // the hit at press time was played by the NoteOn itself
            const int offset = (int) std::floor ((t - start) * spb);
            for (int c = 0; c < (int) snap.channels.size() && c < kMaxChannels; ++c)
                if (held[(size_t) c].velocity > 0.0f)
                {
                    addNoteOn (snap, c, held[(size_t) c].note, held[(size_t) c].velocity, offset);
                    addNoteOff (c, held[(size_t) c].note, juce::jmin (maxBlock - 1, offset + (int) (interval * spb * 0.5)));
                }
        }
    }
    repeatClock += n * bps;
}

void BeatCore::renderMetronome (juce::AudioBuffer<float>& out, int offset, int n, double)
{
    if (! transport.metronome.load() || ! transport.playing.load())
    {
        metroEnv = 0.0f;
        return;
    }
    float* L = out.getWritePointer (0, offset);
    float* R = out.getWritePointer (1, offset);
    const float decay = (float) std::exp (-1.0 / (0.025 * sampleRate));
    int next = 0;
    for (int i = 0; i < n; ++i)
    {
        while (next < numMetro && metroTriggers[(size_t) next].first <= i)
        {
            metroFreq = metroTriggers[(size_t) next].second ? 1600.0 : 1000.0;
            metroEnv = 0.5f;
            metroPhase = 0.0;
            ++next;
        }
        if (metroEnv > 1.0e-4f)
        {
            const float v = (float) std::sin (2.0 * dsp::pi * metroPhase) * metroEnv;
            metroPhase += metroFreq / sampleRate;
            metroEnv *= decay;
            L[i] += v;
            R[i] += v;
        }
    }
}

void BeatCore::processChunk (juce::AudioBuffer<float>& out, int offset, int n)
{
    auto* snap = shared.acquire();
    for (int ch = 0; ch < out.getNumChannels(); ++ch)
        out.clear (ch, offset, n);

    if (snap == nullptr)
    {
        BeatEvent e;
        while (uiEvents.pop (e)) {}
        while (midiEvents.pop (e)) {}
        return;
    }

    const int numCh = juce::jmin ((int) snap->channels.size(), kMaxChannels);
    for (int c = 0; c < numCh; ++c)
        channelMidi[(size_t) c].clear();

    // tempo (optionally following the DJ master clock)
    double bpm = juce::jlimit (20.0, 400.0, transport.bpm.load());
    const bool follow = transport.followClock.load() && clock != nullptr && clock->valid.load();
    if (follow)
    {
        bpm = juce::jlimit (20.0, 400.0, clock->bpm.load());
        transport.bpm.store (bpm);
    }
    currentBpm = bpm;
    const double spb = sampleRate * 60.0 / bpm;

    BeatEvent e;
    while (uiEvents.pop (e)) handleEvent (e, *snap);
    while (midiEvents.pop (e)) handleEvent (e, *snap);

    const bool playing = transport.playing.load();
    if (playing && follow && ! transport.songMode.load())
    {
        const double L = patternLength (*snap, transport.currentPattern.load());
        position = std::fmod (juce::jmax (0.0, clock->beatPhase.load()), L);
    }

    clipBuffer.clear (0, n);
    numMetro = 0;
    flushNoteOffs (*snap, n); // note-offs due from earlier blocks go before new note-ons
    runNoteRepeat (*snap, n, spb);
    if (playing)
        scheduleBlock (*snap, n, spb);
    flushNoteOffs (*snap, n);

    // channels
    bool anySolo = false;
    for (int c = 0; c < numCh; ++c)
        anySolo = anySolo || snap->channels[(size_t) c]->solo.load (std::memory_order_relaxed);

    masterBuffer.clear (0, n);
    EffectContext ctx { bpm, playing };
    for (int c = 0; c < numCh; ++c)
    {
        auto& strip = *snap->channels[(size_t) c];
        auto& buf = channelBuffers[(size_t) c];
        buf.clear (0, n);
        if (strip.instrument != nullptr)
            strip.instrument->render (buf, channelMidi[(size_t) c], n);

        if (c < (int) snap->inserts.size())
            for (auto& fx : snap->inserts[(size_t) c])
                if (fx != nullptr && ! fx->bypassed.load (std::memory_order_relaxed))
                    fx->process (buf, n, ctx);

        const bool audible = ! strip.mute.load (std::memory_order_relaxed)
                             && (! anySolo || strip.solo.load (std::memory_order_relaxed));
        float gl = 0.0f, gr = 0.0f;
        if (audible)
        {
            dsp::panGains (strip.pan.load (std::memory_order_relaxed), gl, gr);
            const float g = dsp::faderToGain (strip.volume.load (std::memory_order_relaxed));
            gl *= g;
            gr *= g;
        }
        if (strip.lastGainL < 0.0f) { strip.lastGainL = gl; strip.lastGainR = gr; }
        buf.applyGainRamp (0, 0, n, strip.lastGainL, gl);
        buf.applyGainRamp (1, 0, n, strip.lastGainR, gr);
        strip.lastGainL = gl;
        strip.lastGainR = gr;

        const float pl = buf.getMagnitude (0, 0, n), pr = buf.getMagnitude (1, 0, n);
        if (pl > strip.peakL.load (std::memory_order_relaxed)) strip.peakL.store (pl, std::memory_order_relaxed);
        if (pr > strip.peakR.load (std::memory_order_relaxed)) strip.peakR.store (pr, std::memory_order_relaxed);

        masterBuffer.addFrom (0, 0, buf, 0, 0, n);
        masterBuffer.addFrom (1, 0, buf, 1, 0, n);
    }

    masterBuffer.addFrom (0, 0, clipBuffer, 0, 0, n);
    masterBuffer.addFrom (1, 0, clipBuffer, 1, 0, n);

    for (auto& fx : snap->masterInserts)
        if (fx != nullptr && ! fx->bypassed.load (std::memory_order_relaxed))
            fx->process (masterBuffer, n, ctx);

    masterBuffer.applyGain (0, n, dsp::faderToGain (transport.masterVolume.load()));

    const float ml = masterBuffer.getMagnitude (0, 0, n), mr = masterBuffer.getMagnitude (1, 0, n);
    if (ml > transport.peakL.load()) transport.peakL.store (ml);
    if (mr > transport.peakR.load()) transport.peakR.store (mr);

    out.copyFrom (0, offset, masterBuffer, 0, 0, n);
    if (out.getNumChannels() > 1)
        out.copyFrom (1, offset, masterBuffer, 1, 0, n);

    renderMetronome (out, offset, n, spb);

    sampleCounter += n;
    wasPlaying = playing;
    transport.positionBeats.store (position);
}

} // namespace merge
