#include "Instruments.h"

namespace merge
{

//==============================================================================
SamplerInstrument::SamplerInstrument()
    : tune (params.add ("tune", "Tune", -24.0f, 24.0f, 0.0f, " st")),
      fine (params.add ("fine", "Fine", -100.0f, 100.0f, 0.0f, " ct")),
      attack (params.add ("attack", "Attack", 0.0f, 1000.0f, 0.0f, " ms", 50.0f)),
      release (params.add ("release", "Release", 5.0f, 4000.0f, 200.0f, " ms", 300.0f)),
      gate (params.addToggle ("gate", "Gate", false)),
      reverse (params.addToggle ("reverse", "Reverse", false)),
      startPos (params.add ("start", "Start", 0.0f, 1.0f, 0.0f)),
      endPos (params.add ("end", "End", 0.0f, 1.0f, 1.0f)),
      velocitySens (params.add ("velsens", "Velocity", 0.0f, 1.0f, 0.7f)),
      mono (params.addToggle ("mono", "Mono", false)),
      glide (params.add ("glide", "Glide", 0.0f, 1000.0f, 0.0f, " ms", 100.0f))
{
}

void SamplerInstrument::prepare (double sr, int)
{
    sampleRate = sr;
    preparedRate = sr;
}

void SamplerInstrument::setSample (SampleBuffer::Ptr s)
{
    uiSample = s;
    sample.store (s.get(), std::memory_order_release);
}

juce::var SamplerInstrument::saveState() const
{
    auto v = params.toVar();
    v.getDynamicObject()->setProperty ("root", rootNote.load());
    return v;
}

void SamplerInstrument::loadState (const juce::var& v)
{
    params.fromVar (v);
    if (v.hasProperty ("root"))
        rootNote.store ((int) v["root"]);
}

Instrument::Ptr SamplerInstrument::clone() const
{
    auto* c = new SamplerInstrument();
    c->params.copyFrom (params);
    c->setRootNote (getRootNote());
    c->setSample (uiSample);
    return Ptr (c);
}

void SamplerInstrument::allNotesOff()
{
    for (auto& v : voices)
    {
        v.active = false;
        v.buffer = nullptr;
    }
}

void SamplerInstrument::choke()
{
    const float dec = 1.0f / (float) juce::jmax (1.0, 0.004 * sampleRate);
    for (auto& v : voices)
        if (v.active)
        {
            v.stage = 2;
            v.releaseDec = juce::jmax (v.releaseDec, dec);
        }
}

void SamplerInstrument::startVoice (int note, float velocity)
{
    auto* s = sample.load (std::memory_order_acquire);
    if (s == nullptr || s->getNumFrames() < 2)
        return;

    const bool isMono = mono.getBool();
    Voice* v = nullptr;
    double previousInc = 0.0;

    if (isMono)
    {
        for (auto& cand : voices)
            if (cand.active && cand.stage != 2)
                previousInc = std::abs (cand.inc);
        for (auto& cand : voices)
            if (cand.active)
            {
                cand.stage = 2;
                cand.releaseDec = 1.0f / (float) juce::jmax (1.0, 0.003 * sampleRate);
            }
    }

    for (auto& cand : voices)
        if (! cand.active) { v = &cand; break; }
    if (v == nullptr)
    {
        v = &voices[0];
        for (auto& cand : voices)
            if (cand.age < v->age) v = &cand;
    }

    const int frames = s->getNumFrames();
    const double semis = note - rootNote.load() + tune.get() + fine.get() * 0.01;
    const double rate = (s->getSampleRate() / sampleRate) * std::pow (2.0, semis / 12.0);

    double st = juce::jlimit (0.0, 1.0, (double) startPos.get()) * frames;
    double en = juce::jlimit (0.0, 1.0, (double) endPos.get()) * frames;
    if (en < st + 2) en = juce::jmin ((double) frames, st + 2);

    v->buffer = SampleBuffer::Ptr (s);
    v->active = true;
    v->note = note;
    v->reverse = reverse.getBool();
    v->startPos = st;
    v->endPos = en;
    v->pos = v->reverse ? en - 1.0 : st;
    v->targetInc = v->reverse ? -rate : rate;
    const double glideMs = glide.get();
    if (isMono && glideMs > 0.5 && previousInc > 0.0)
    {
        v->inc = v->reverse ? -previousInc : previousInc;
        v->glideCoef = 1.0 - std::exp (-1.0 / (glideMs * 0.001 * sampleRate / 4.0));
    }
    else
    {
        v->inc = v->targetInc;
        v->glideCoef = 0.0;
    }
    const float sens = velocitySens.get();
    v->gain = (1.0f - sens) + sens * velocity;
    const double attackSamples = attack.get() * 0.001 * sampleRate;
    v->env = attackSamples < 1.0 ? 1.0f : 0.0f;
    v->envInc = attackSamples < 1.0 ? 0.0f : (float) (1.0 / attackSamples);
    v->stage = 0;
    v->releaseDec = 0.0f;
    v->age = ++voiceCounter;
}

void SamplerInstrument::stopNote (int note)
{
    if (! gate.getBool())
        return;
    const float dec = 1.0f / (float) juce::jmax (1.0, release.get() * 0.001 * sampleRate);
    for (auto& v : voices)
        if (v.active && v.note == note && v.stage != 2)
        {
            v.stage = 2;
            v.releaseDec = dec;
        }
}

void SamplerInstrument::handleMidi (const juce::MidiMessage& m)
{
    if (m.isNoteOn())
        startVoice (m.getNoteNumber(), m.getFloatVelocity());
    else if (m.isNoteOff())
        stopNote (m.getNoteNumber());
    else if (m.isAllSoundOff() || m.isAllNotesOff())
        choke();
}

void SamplerInstrument::renderVoices (juce::AudioBuffer<float>& out, int start, int num)
{
    float* L = out.getWritePointer (0, start);
    float* R = out.getWritePointer (1, start);

    for (auto& v : voices)
    {
        if (! v.active)
            continue;

        const auto* buf = v.buffer.get();
        const bool stereo = buf->getNumChannels() > 1;

        for (int i = 0; i < num; ++i)
        {
            if (v.stage == 0 && v.envInc > 0.0f)
            {
                v.env += v.envInc;
                if (v.env >= 1.0f) { v.env = 1.0f; v.stage = 1; }
            }
            else if (v.stage == 2)
            {
                v.env -= v.releaseDec;
                if (v.env <= 0.0f) { v.active = false; break; }
            }

            const float g = v.gain * v.env;
            const float l = buf->readCubic (0, v.pos);
            const float r = stereo ? buf->readCubic (1, v.pos) : l;
            L[i] += l * g;
            R[i] += r * g;

            if (v.glideCoef > 0.0)
                v.inc += (v.targetInc - v.inc) * v.glideCoef;
            v.pos += v.inc;

            if ((! v.reverse && v.pos >= v.endPos) || (v.reverse && v.pos < v.startPos))
            {
                v.active = false;
                break;
            }
        }

        if (! v.active)
            v.buffer = nullptr;
    }
}

//==============================================================================
Bass808Instrument::Bass808Instrument()
    : decay (params.add ("decay", "Decay", 0.1f, 6.0f, 1.6f, " s", 1.5f)),
      glide (params.add ("glide", "Glide", 0.0f, 500.0f, 60.0f, " ms", 80.0f)),
      punch (params.add ("punch", "Punch", 0.0f, 1.0f, 0.5f)),
      drive (params.add ("drive", "Drive", 0.0f, 1.0f, 0.35f)),
      tone (params.add ("tone", "Tone", 200.0f, 12000.0f, 3000.0f, " Hz", 2000.0f))
{
}

void Bass808Instrument::prepare (double sr, int)
{
    sampleRate = sr;
    preparedRate = sr;
    toneFilter.reset();
}

Instrument::Ptr Bass808Instrument::clone() const
{
    auto* c = new Bass808Instrument();
    c->params.copyFrom (params);
    return Ptr (c);
}

void Bass808Instrument::allNotesOff()
{
    active = false;
    amp = 0.0f;
    heldNotes.clear();
    currentNote = -1;
}

void Bass808Instrument::handleMidi (const juce::MidiMessage& m)
{
    if (m.isNoteOn())
    {
        const int note = m.getNoteNumber();
        const double f = juce::MidiMessage::getMidiNoteInHertz (note);
        const bool legato = active && ! heldNotes.empty() && glide.get() > 0.5f;
        if (heldNotes.size() < 16)
            heldNotes.push_back (note);
        targetFreq = f;
        velocity = m.getFloatVelocity();
        if (! legato)
        {
            freq = f;
            phase = 0.0;
            punchEnv = 1.0;
        }
        amp = 1.0f;
        active = true;
        currentNote = note;
    }
    else if (m.isNoteOff())
    {
        const int note = m.getNoteNumber();
        heldNotes.erase (std::remove (heldNotes.begin(), heldNotes.end(), note), heldNotes.end());
        if (note == currentNote)
        {
            if (! heldNotes.empty())
            {
                currentNote = heldNotes.back();
                targetFreq = juce::MidiMessage::getMidiNoteInHertz (currentNote);
            }
            else
            {
                currentNote = -1; // let it ring out with a faster tail
            }
        }
    }
    else if (m.isAllSoundOff() || m.isAllNotesOff())
    {
        allNotesOff();
    }
}

void Bass808Instrument::renderVoices (juce::AudioBuffer<float>& out, int start, int num)
{
    if (! active)
        return;

    float* L = out.getWritePointer (0, start);
    float* R = out.getWritePointer (1, start);

    const double decaySeconds = decay.get();
    const float ringDecay = (float) std::exp (-6.9 / (decaySeconds * sampleRate));
    const float releaseDecay = (float) std::exp (-6.9 / (0.08 * sampleRate));
    const double glideCoef = glide.get() < 0.5f ? 1.0 : 1.0 - std::exp (-1.0 / (glide.get() * 0.001 * sampleRate / 4.0));
    const double punchDecay = std::exp (-1.0 / (0.012 * sampleRate));
    const float punchAmt = punch.get();
    const float drv = 1.0f + drive.get() * 6.0f;
    const float norm = 1.0f / std::tanh (drv);
    toneFilter.setup (sampleRate, tone.get(), 0.7f);

    for (int i = 0; i < num; ++i)
    {
        freq += (targetFreq - freq) * glideCoef;
        const double f = freq * (1.0 + punchAmt * punchEnv * 1.5);
        punchEnv *= punchDecay;
        phase += f / sampleRate;
        if (phase >= 1.0) phase -= 1.0;
        float s = (float) std::sin (2.0 * dsp::pi * phase);
        s = std::tanh (s * drv) * norm;
        s = toneFilter.tick (s).low;
        const float v = s * amp * (0.35f + 0.65f * velocity);
        L[i] += v;
        R[i] += v;
        amp *= (currentNote < 0 ? releaseDecay : ringDecay);
        if (amp < 1.0e-4f)
        {
            active = false;
            amp = 0.0f;
            break;
        }
    }
}

//==============================================================================
SynthInstrument::SynthInstrument()
    : wave1 (params.addChoice ("wave1", "Osc 1", { "Saw", "Square", "Triangle", "Sine" }, 0)),
      wave2 (params.addChoice ("wave2", "Osc 2", { "Saw", "Square", "Triangle", "Sine" }, 1)),
      detune (params.add ("detune", "Detune", 0.0f, 50.0f, 9.0f, " ct")),
      oscMix (params.add ("mix", "Osc Mix", 0.0f, 1.0f, 0.4f)),
      cutoff (params.add ("cutoff", "Cutoff", 40.0f, 18000.0f, 2400.0f, " Hz", 1500.0f)),
      resonance (params.add ("reso", "Resonance", 0.5f, 8.0f, 1.2f)),
      envAmount (params.add ("envamt", "Filter Env", 0.0f, 1.0f, 0.35f)),
      attack (params.add ("attack", "Attack", 0.001f, 3.0f, 0.005f, " s", 0.3f)),
      decay (params.add ("decay", "Decay", 0.01f, 3.0f, 0.4f, " s", 0.4f)),
      sustain (params.add ("sustain", "Sustain", 0.0f, 1.0f, 0.6f)),
      release (params.add ("release", "Release", 0.01f, 5.0f, 0.35f, " s", 0.5f)),
      glide (params.add ("glide", "Glide", 0.0f, 0.5f, 0.0f, " s", 0.1f))
{
}

void SynthInstrument::prepare (double sr, int)
{
    sampleRate = sr;
    preparedRate = sr;
}

Instrument::Ptr SynthInstrument::clone() const
{
    auto* c = new SynthInstrument();
    c->params.copyFrom (params);
    return Ptr (c);
}

void SynthInstrument::allNotesOff()
{
    for (auto& v : voices)
    {
        v.active = false;
        v.ampEnv.kill();
        v.filterEnv.kill();
    }
}

void SynthInstrument::handleMidi (const juce::MidiMessage& m)
{
    if (m.isNoteOn())
    {
        Voice* v = nullptr;
        for (auto& c : voices)
            if (! c.active) { v = &c; break; }
        if (v == nullptr)
        {
            v = &voices[0];
            for (auto& c : voices)
                if (c.age < v->age) v = &c;
        }
        v->active = true;
        v->note = m.getNoteNumber();
        v->velocity = m.getFloatVelocity();
        v->targetFreq = juce::MidiMessage::getMidiNoteInHertz (v->note);
        v->freq = (glide.get() > 0.001f && lastFreq > 0) ? lastFreq : v->targetFreq;
        lastFreq = v->targetFreq;
        v->ampEnv.set (sampleRate, attack.get(), decay.get(), sustain.get(), release.get());
        v->filterEnv.set (sampleRate, attack.get() * 0.5f, decay.get(), 0.0f, release.get());
        v->ampEnv.noteOn();
        v->filterEnv.noteOn();
        v->filterL.reset();
        v->age = ++counter;
    }
    else if (m.isNoteOff())
    {
        for (auto& v : voices)
            if (v.active && v.note == m.getNoteNumber())
            {
                v.ampEnv.noteOff();
                v.filterEnv.noteOff();
            }
    }
    else if (m.isAllSoundOff() || m.isAllNotesOff())
    {
        for (auto& v : voices)
        {
            v.ampEnv.noteOff();
            v.filterEnv.noteOff();
        }
    }
}

void SynthInstrument::renderVoices (juce::AudioBuffer<float>& out, int start, int num)
{
    float* L = out.getWritePointer (0, start);
    float* R = out.getWritePointer (1, start);
    const int w1 = wave1.getChoice(), w2 = wave2.getChoice();
    const double det = std::pow (2.0, detune.get() / 1200.0);
    const float mixAmt = oscMix.get();
    const float baseCut = cutoff.get();
    const float q = resonance.get();
    const float envAmt = envAmount.get();
    const double glideCoef = glide.get() < 0.001f ? 1.0 : 1.0 - std::exp (-1.0 / (glide.get() * sampleRate / 4.0));

    for (auto& v : voices)
    {
        if (! v.active)
            continue;
        for (int i = 0; i < num; ++i)
        {
            v.freq += (v.targetFreq - v.freq) * glideCoef;
            if ((i & 15) == 0)
            {
                const float fe = v.filterEnv.level;
                v.filterL.setup (sampleRate, baseCut * std::pow (2.0f, envAmt * 5.0f * fe), q);
            }
            const double dt = v.freq / sampleRate;
            const float a = v.o1.tick (w1, dt);
            const float b = v.o2.tick (w2, dt * det);
            const float c = v.o3.tick (w1, dt / det);
            const float s = (a * 0.6f + c * 0.4f) * (1.0f - mixAmt) + b * mixAmt;
            const float env = v.ampEnv.tick();
            v.filterEnv.tick();
            const float y = v.filterL.tick (s).low * env * (0.3f + 0.7f * v.velocity) * 0.35f;
            L[i] += y;
            R[i] += y;
            if (! v.ampEnv.isActive())
            {
                v.active = false;
                break;
            }
        }
    }
}

//==============================================================================
Instrument::Ptr createBuiltInInstrument (const juce::String& typeId)
{
    Instrument::Ptr p;
    if (typeId == "sampler") p = new SamplerInstrument();
    else if (typeId == "bass808") p = new Bass808Instrument();
    else if (typeId == "synth") p = new SynthInstrument();
    if (p != nullptr)
        ReleasePool::get().add (p);
    return p;
}

} // namespace merge
