#include "ControllerManager.h"
#include "AppContext.h"
#include "DjController.h"
#include "core/Paths.h"

namespace merge
{

ControllerManager::ControllerManager (AppContext& c) : ctx (c)
{
    registerControls();
    auto f = currentMappingFile();
    if (f.existsAsFile())
        mapping.load (f);
    startTimerHz (100);
}

ControllerManager::~ControllerManager()
{
    stopTimer();
}

juce::File ControllerManager::currentMappingFile() const
{
    return paths::mappingsDir().getChildFile (juce::File::createLegalFileName (
        ctx.settings().getValue ("midiMapping", "My controller")) + ".json");
}

void ControllerManager::add (const juce::String& id, const juce::String& name, const juce::String& group,
                             Control::Kind kind, std::function<void (float)> fn)
{
    controls.push_back ({ id, name, group, kind, std::move (fn) });
}

const ControllerManager::Control* ControllerManager::findControl (const juce::String& id) const
{
    for (auto& c : controls)
        if (c.id == id) return &c;
    return nullptr;
}

void ControllerManager::registerControls()
{
    for (int d = 0; d < 4; ++d)
    {
        const auto p = "deck" + juce::String (d + 1) + ".";
        const auto g = "Deck " + juce::String::charToString ((juce::juce_wchar) ('A' + d));
        auto& dc = *ctx.decks;
        add (p + "play", "Play / pause", g, Control::Button, [&dc, d] (float) { dc.togglePlay (d); });
        add (p + "cue", "Cue", g, Control::Momentary, [&dc, d] (float v) { dc.cue (d, v > 0.5f); });
        add (p + "sync", "Sync", g, Control::Button, [&dc, d] (float) { dc.setSync (d, ! dc.deck (d).sync.load()); });
        add (p + "master", "Master", g, Control::Button, [&dc, d] (float) { dc.setMaster (d); });
        add (p + "keylock", "Keylock", g, Control::Button, [&dc, d] (float) { dc.deck (d).keylock.store (! dc.deck (d).keylock.load()); });
        add (p + "pitch", "Tempo fader", g, Control::Continuous, [&dc, d] (float v) { dc.deck (d).pitch.store (v * 2.0f - 1.0f); });
        add (p + "jog", "Jog wheel (pitch bend)", g, Control::Jog, [this, &dc, d] (float steps)
        {
            dc.nudge (d, juce::jlimit (-0.2f, 0.2f, steps * 0.02f));
            lastJog[(size_t) d] = juce::Time::getMillisecondCounter();
        });
        for (int h = 0; h < 8; ++h)
            add (p + "hotcue" + juce::String (h + 1), "Hot cue " + juce::String (h + 1), g, Control::Button, [&dc, d, h] (float) { dc.hotCue (d, h); });
        add (p + "loop", "Auto loop", g, Control::Button, [&dc, d] (float) { dc.loopAuto (d, dc.deck (d).loopBeatsDefault.load()); });
        add (p + "loophalf", "Loop / 2", g, Control::Button, [&dc, d] (float) { dc.loopHalve (d); });
        add (p + "loopdouble", "Loop x 2", g, Control::Button, [&dc, d] (float) { dc.loopDouble (d); });
        add (p + "jumpback", "Beat jump back", g, Control::Button, [&dc, d] (float) { dc.beatJump (d, -dc.deck (d).loopBeatsDefault.load()); });
        add (p + "jumpfwd", "Beat jump forward", g, Control::Button, [&dc, d] (float) { dc.beatJump (d, dc.deck (d).loopBeatsDefault.load()); });
        auto& ch = ctx.dj.channel (d);
        add (p + "volume", "Channel fader", g, Control::Continuous, [&ch] (float v) { ch.fader.store (v); });
        add (p + "trim", "Trim", g, Control::Continuous, [&ch] (float v) { ch.trimDb.store (v * 24.0f - 12.0f); });
        add (p + "eqhigh", "EQ high", g, Control::Continuous, [&ch] (float v) { ch.eqHigh.store (v); });
        add (p + "eqmid", "EQ mid", g, Control::Continuous, [&ch] (float v) { ch.eqMid.store (v); });
        add (p + "eqlow", "EQ low", g, Control::Continuous, [&ch] (float v) { ch.eqLow.store (v); });
        add (p + "filter", "Filter", g, Control::Continuous, [&ch] (float v) { ch.filter.store (v * 2.0f - 1.0f); });
        add (p + "headphone", "Headphone cue", g, Control::Button, [&ch] (float) { ch.cue.store (! ch.cue.load()); });
        add (p + "fxon", "FX on", g, Control::Button, [&ch] (float) { ch.fxOn.store (! ch.fxOn.load()); });
        add (p + "fxamount", "FX amount", g, Control::Continuous, [&ch] (float v) { ch.fxAmount.store (v); });
    }
    add ("mixer.crossfader", "Crossfader", "Mixer", Control::Continuous, [this] (float v) { ctx.dj.crossfader.store (v * 2.0f - 1.0f); });
    add ("mixer.master", "Master volume", "Mixer", Control::Continuous, [this] (float v) { ctx.dj.masterVolume.store (v); });
    add ("mixer.cuemix", "Headphone mix", "Mixer", Control::Continuous, [this] (float v) { ctx.dj.cueMix.store (v); });
    add ("mixer.cuevolume", "Headphone volume", "Mixer", Control::Continuous, [this] (float v) { ctx.dj.cueVolume.store (v); });
    add ("beat.play", "Play / pause beat", "Beatmaker", Control::Button, [this] (float) { ctx.togglePlay(); });
    add ("beat.stop", "Stop beat", "Beatmaker", Control::Button, [this] (float) { ctx.stop(); });
    add ("beat.record", "Record", "Beatmaker", Control::Button, [this] (float)
    {
        auto& t = ctx.engine.beat().transport;
        t.recording.store (! t.recording.load());
        ctx.sendChangeMessage();
    });
    add ("beat.prevpattern", "Previous pattern", "Beatmaker", Control::Button, [this] (float) { ctx.selectPattern (ctx.selectedPattern() - 1); });
    add ("beat.nextpattern", "Next pattern", "Beatmaker", Control::Button, [this] (float) { ctx.selectPattern (ctx.selectedPattern() + 1); });
    add ("beat.level", "Beat level (remix deck)", "Beatmaker", Control::Continuous, [this] (float v) { ctx.engine.beatLevel.store (v * v); });
    add ("beat.bpm", "Beat tempo (60-180)", "Beatmaker", Control::Continuous, [this] (float v) { ctx.setBpm (60.0 + v * 120.0); });
    for (int pad = 0; pad < 16; ++pad)
        add ("beat.pad" + juce::String (pad + 1), "Pad " + juce::String (pad + 1), "Beatmaker", Control::Momentary, [this, pad] (float v)
        {
            if (v > 0.0f) ctx.engine.beat().noteOn (pad, -1, juce::jlimit (0.2f, 1.0f, v));
            else ctx.engine.beat().noteOff (pad, -1);
        });
    add ("app.mode", "Switch Make / Mix", "App", Control::Button, [this] (float) { if (ctx.onModeChange) ctx.onModeChange (! ctx.makeMode); });
}

void ControllerManager::startLearn (const juce::String& controlId)
{
    learning = controlId;
    sendChangeMessage();
}

void ControllerManager::cancelLearn()
{
    learning.clear();
    sendChangeMessage();
}

void ControllerManager::saveMapping()
{
    mapping.save (currentMappingFile());
}

bool ControllerManager::loadMapping (const juce::File& f)
{
    if (! mapping.load (f)) return false;
    ctx.settings().setValue ("midiMapping", mapping.name);
    sendChangeMessage();
    return true;
}

void ControllerManager::dispatch (const juce::MidiMessage& m)
{
    if (! (m.isNoteOnOrOff() || m.isController())) return;
    lastMessage = m.getDescription();

    if (learning.isNotEmpty())
    {
        if (m.isNoteOff()) return; // learn on the press
        if (auto* c = findControl (learning))
        {
            MidiBinding b;
            b.type = m.isNoteOnOrOff() ? MidiBinding::Type::Note : MidiBinding::Type::CC;
            b.channel = m.getChannel();
            b.number = b.type == MidiBinding::Type::Note ? m.getNoteNumber() : m.getControllerNumber();
            b.control = c->id;
            switch (c->kind)
            {
                case Control::Continuous: b.mode = MidiBinding::Mode::Absolute; break;
                case Control::Jog: b.mode = MidiBinding::Mode::Relative; break;
                case Control::Momentary: b.mode = MidiBinding::Mode::Momentary; break;
                case Control::Button: b.mode = MidiBinding::Mode::Toggle; break;
            }
            mapping.bind (b);
            saveMapping();
            ctx.setStatus ("Mapped " + b.describe() + " to " + c->group + " - " + c->name);
        }
        learning.clear();
        sendChangeMessage();
        return;
    }

    auto* b = mapping.find (m);
    if (b == nullptr) return;
    auto* c = findControl (b->control);
    if (c == nullptr || ! c->apply) return;
    const float v = MidiMapping::valueFor (*b, m);
    switch (c->kind)
    {
        case Control::Button:
            if (v > 0.5f) c->apply (1.0f);
            break;
        case Control::Momentary:
            c->apply (b->type == MidiBinding::Type::Note && m.isNoteOn() ? m.getFloatVelocity() : v);
            break;
        case Control::Continuous:
        case Control::Jog:
            c->apply (v);
            break;
    }
}

void ControllerManager::timerCallback()
{
    MidiPacket p;
    bool any = false;
    while (ctx.engine.popMidiForUi (p))
    {
        if (p.size <= 0) continue;
        dispatch (juce::MidiMessage (p.data, p.size));
        any = true;
    }
    // jog nudges are momentary: release after the wheel stops
    const auto now = juce::Time::getMillisecondCounter();
    for (int d = 0; d < 4; ++d)
        if (lastJog[(size_t) d] != 0 && now - lastJog[(size_t) d] > 60)
        {
            ctx.decks->nudge (d, 0.0f);
            lastJog[(size_t) d] = 0;
        }
    if (any && learning.isEmpty())
        sendChangeMessage();
}

} // namespace merge
