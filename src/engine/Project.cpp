#include "Project.h"
#include "Instruments.h"
#include "PluginHost.h"
#include "core/Paths.h"

namespace merge
{

juce::String channelKindName (ChannelKind k)
{
    switch (k)
    {
        case ChannelKind::Sampler: return "sampler";
        case ChannelKind::Keys:    return "keys";
        case ChannelKind::Bass808: return "bass808";
        case ChannelKind::Synth:   return "synth";
        case ChannelKind::Plugin:  return "plugin";
    }
    return "sampler";
}

static ChannelKind channelKindFromName (const juce::String& s)
{
    if (s == "keys") return ChannelKind::Keys;
    if (s == "bass808") return ChannelKind::Bass808;
    if (s == "synth") return ChannelKind::Synth;
    if (s == "plugin") return ChannelKind::Plugin;
    return ChannelKind::Sampler;
}

//==============================================================================
static const char* noteNames[] { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };

juce::String MusicalKey::toString() const
{
    if (root < 0) return "No key";
    return juce::String (noteNames[root % 12]) + (minor ? " minor" : " major");
}

juce::String MusicalKey::toCamelot() const
{
    if (root < 0) return "-";
    const int majorRoot = minor ? (root + 3) % 12 : root;
    const int n = ((majorRoot * 7) % 12 + 7) % 12 + 1;
    return juce::String (n) + (minor ? "A" : "B");
}

MusicalKey MusicalKey::fromIndex (int index24)
{
    MusicalKey k;
    if (index24 < 0 || index24 >= 24) return k;
    k.root = index24 % 12;
    k.minor = index24 >= 12;
    return k;
}

MusicalKey MusicalKey::fromString (const juce::String& input)
{
    MusicalKey k;
    auto s = input.trim();
    if (s.isEmpty()) return k;

    // Camelot notation, e.g. "8A"
    if (s.containsOnly ("0123456789ABab") && (s.endsWithIgnoreCase ("A") || s.endsWithIgnoreCase ("B")))
    {
        const int n = s.dropLastCharacters (1).getIntValue();
        if (n >= 1 && n <= 12)
        {
            const bool isMinor = s.endsWithIgnoreCase ("A");
            for (int r = 0; r < 12; ++r)
            {
                MusicalKey c { r, isMinor };
                if (c.toCamelot() == juce::String (n) + (isMinor ? "A" : "B"))
                    return c;
            }
        }
    }

    static const std::pair<const char*, int> roots[] {
        { "C#", 1 }, { "Db", 1 }, { "D#", 3 }, { "Eb", 3 }, { "F#", 6 }, { "Gb", 6 }, { "G#", 8 }, { "Ab", 8 },
        { "A#", 10 }, { "Bb", 10 }, { "C", 0 }, { "D", 2 }, { "E", 4 }, { "F", 5 }, { "G", 7 }, { "A", 9 }, { "B", 11 } };
    for (auto& [name, value] : roots)
    {
        if (s.startsWith (name))
        {
            k.root = value;
            auto rest = s.substring ((int) strlen (name)).trim().toLowerCase();
            k.minor = rest.startsWith ("m") && ! rest.startsWith ("maj");
            return k;
        }
    }
    return k;
}

bool MusicalKey::isInScale (int midiNote) const noexcept
{
    if (root < 0) return true;
    static const bool majorScale[12] { 1, 0, 1, 0, 1, 1, 0, 1, 0, 1, 0, 1 };
    static const bool minorScale[12] { 1, 0, 1, 1, 0, 1, 0, 1, 1, 0, 1, 0 };
    const int degree = ((midiNote - root) % 12 + 12) % 12;
    return minor ? minorScale[degree] : majorScale[degree];
}

//==============================================================================
bool PatternData::isEmpty() const
{
    for (auto& [id, s] : steps)
        for (auto v : s)
            if (v > 0) return false;
    for (auto& [id, n] : notes)
        if (! n.empty()) return false;
    return true;
}

juce::String Project::patternName (int index)
{
    return juce::String::charToString ((juce::juce_wchar) ('A' + juce::jlimit (0, 25, index)));
}

juce::Colour Project::colourForIndex (int index)
{
    static const juce::uint32 palette[] { 0xffff7a1a, 0xffffb020, 0xfff2d43d, 0xff9be15d, 0xff3fd28a, 0xff19d3c5,
                                          0xff3fa9f5, 0xff6c7cff, 0xffb37bff, 0xffff6bd6, 0xffff5a5f, 0xffe0e0e0 };
    return juce::Colour (palette[(size_t) (index % 12)]);
}

Project::Project()
{
    resetToDefault();
}

Project::~Project() = default;

void Project::resetToDefault()
{
    channels.clear();
    clips.clear();
    audioClips.clear();
    for (auto& fx : masterInserts) fx = nullptr;
    patterns.assign (kNumPatterns, {});
    for (int i = 0; i < kNumPatterns; ++i)
        patterns[(size_t) i].name = patternName (i);
    lanes.clear();
    for (int i = 0; i < 8; ++i)
        lanes.push_back ({ "Track " + juce::String (i + 1), false });
    nextChannelId = 1;
    bpm = 120.0;
    swing = 50.0;
    key = {};
    name = "Untitled";
    kitName.clear();
    genre.clear();
    file = juce::File();
    undoStack.clear();
    redoStack.clear();
    for (int i = 0; i < kNumPads; ++i)
        addChannel (ChannelKind::Sampler, "Pad " + juce::String (i + 1));
    clips.push_back ({ 0, 0, 0.0, 4.0, 0.0 });
}

void Project::setAudioConfig (double sr, int bs)
{
    sampleRate = sr;
    blockSize = bs;
    for (auto& c : channels)
    {
        if (c.strip != nullptr && c.strip->instrument != nullptr)
            prepareObject (*c.strip->instrument);
        for (auto& fx : c.inserts)
            if (fx != nullptr) prepareObject (*fx);
    }
    for (auto& fx : masterInserts)
        if (fx != nullptr) prepareObject (*fx);
}

void Project::prepareObject (Instrument& i) const { i.prepare (sampleRate, blockSize); }
void Project::prepareObject (Effect& e) const { e.prepare (sampleRate, blockSize); }

Instrument::Ptr Project::makeInstrument (ChannelKind kind) const
{
    switch (kind)
    {
        case ChannelKind::Sampler:
            return createBuiltInInstrument ("sampler");
        case ChannelKind::Keys:
        {
            auto inst = createBuiltInInstrument ("sampler");
            if (auto* s = dynamic_cast<SamplerInstrument*> (inst.get()))
            {
                s->gate.set (1.0f);
                s->release.set (350.0f);
                s->attack.set (2.0f);
            }
            return inst;
        }
        case ChannelKind::Bass808: return createBuiltInInstrument ("bass808");
        case ChannelKind::Synth:   return createBuiltInInstrument ("synth");
        case ChannelKind::Plugin:  return nullptr;
    }
    return nullptr;
}

ChannelModel* Project::findChannel (int id)
{
    for (auto& c : channels)
        if (c.id == id) return &c;
    return nullptr;
}

int Project::indexOfChannel (int id) const
{
    for (size_t i = 0; i < channels.size(); ++i)
        if (channels[i].id == id) return (int) i;
    return -1;
}

ChannelModel& Project::addChannel (ChannelKind kind, const juce::String& channelName, Instrument::Ptr custom)
{
    ChannelModel c;
    c.id = nextChannelId++;
    c.name = channelName;
    c.kind = kind;
    c.colour = colourForIndex ((int) channels.size());
    auto inst = custom != nullptr ? custom : makeInstrument (kind);
    if (inst != nullptr)
        prepareObject (*inst);
    c.strip = new ChannelStrip (inst);
    ReleasePool::get().add (c.strip);
    c.stepNote = kind == ChannelKind::Bass808 ? 36 : 60;
    channels.push_back (std::move (c));
    return channels.back();
}

void Project::removeChannel (int id)
{
    channels.erase (std::remove_if (channels.begin(), channels.end(), [id] (const ChannelModel& c) { return c.id == id; }),
                    channels.end());
    for (auto& p : patterns)
    {
        p.steps.erase (id);
        p.notes.erase (id);
    }
}

void Project::setChannelSample (int id, SampleBuffer::Ptr sample, const juce::String& ref, int rootNote)
{
    if (auto* c = findChannel (id))
    {
        if (c->strip != nullptr && c->strip->instrument != nullptr)
        {
            c->strip->instrument->setSample (sample);
            if (auto* s = dynamic_cast<SamplerInstrument*> (c->strip->instrument.get()))
                s->setRootNote (rootNote);
        }
        c->sampleRef = ref;
        if (c->kind == ChannelKind::Sampler)
            c->stepNote = rootNote;
        if (sample != nullptr && (c->name.startsWith ("Pad ") || c->kind != ChannelKind::Sampler || c->name.isEmpty()))
            c->name = sample->getName().fromFirstOccurrenceOf (" ", false, false).isNotEmpty()
                          && sample->getName().substring (0, 2).containsOnly ("0123456789")
                        ? sample->getName().fromFirstOccurrenceOf (" ", false, false)
                        : sample->getName();
    }
}

void Project::setInsert (int channelId, int slot, Effect::Ptr fx)
{
    if (slot < 0 || slot >= kNumInserts) return;
    if (fx != nullptr) prepareObject (*fx);
    if (channelId < 0)
        masterInserts[(size_t) slot] = fx;
    else if (auto* c = findChannel (channelId))
        c->inserts[(size_t) slot] = fx;
}

Effect::Ptr Project::getInsert (int channelId, int slot) const
{
    if (slot < 0 || slot >= kNumInserts) return nullptr;
    if (channelId < 0) return masterInserts[(size_t) slot];
    for (auto& c : channels)
        if (c.id == channelId) return c.inserts[(size_t) slot];
    return nullptr;
}

//==============================================================================
int Project::getStep (int pi, int channelId, int step) const
{
    const auto& p = patterns[(size_t) juce::jlimit (0, kNumPatterns - 1, pi)];
    auto it = p.steps.find (channelId);
    if (it == p.steps.end() || step < 0 || step >= (int) it->second.size())
        return 0;
    return it->second[(size_t) step];
}

void Project::setStep (int pi, int channelId, int step, int velocity)
{
    auto& p = pattern (pi);
    if (step < 0 || step >= 64) return;
    auto& s = p.steps[channelId];
    if ((int) s.size() < juce::jmax (p.lengthSteps, step + 1))
        s.resize ((size_t) juce::jmax (p.lengthSteps, step + 1), 0);
    s[(size_t) step] = (juce::uint8) juce::jlimit (0, 127, velocity);
}

void Project::setPatternLength (int pi, int steps)
{
    auto& p = pattern (pi);
    p.lengthSteps = juce::jlimit (1, 64, steps);
    for (auto& [id, s] : p.steps)
        s.resize ((size_t) p.lengthSteps, 0);
}

void Project::clearPattern (int pi)
{
    auto& p = pattern (pi);
    p.steps.clear();
    p.notes.clear();
}

void Project::setNotes (int pi, int channelId, std::vector<Note> n)
{
    std::sort (n.begin(), n.end(), [] (const Note& a, const Note& b) { return a.start < b.start; });
    pattern (pi).notes[channelId] = std::move (n);
}

const std::vector<Note>* Project::getNotes (int pi, int channelId) const
{
    const auto& p = patterns[(size_t) juce::jlimit (0, kNumPatterns - 1, pi)];
    auto it = p.notes.find (channelId);
    return it == p.notes.end() ? nullptr : &it->second;
}

void Project::setStepsFromString (int pi, int channelId, const juce::String& str)
{
    auto& p = pattern (pi);
    auto& s = p.steps[channelId];
    s.assign ((size_t) p.lengthSteps, 0);
    for (int i = 0; i < str.length() && i < p.lengthSteps; ++i)
    {
        const auto ch = str[i];
        s[(size_t) i] = ch == 'X' ? 127 : (ch == 'x' ? 100 : (ch == 'o' ? 64 : 0));
    }
}

void Project::applyRecorded (const RecordedNote& r)
{
    if (r.channel < 0 || r.channel >= (int) channels.size())
        return;
    auto& ch = channels[(size_t) r.channel];
    auto& p = pattern (r.pattern);
    const int len = p.lengthSteps;
    const bool oneShot = ch.strip != nullptr && ch.strip->instrument != nullptr && ch.strip->instrument->isOneShot();
    if (oneShot && ch.kind == ChannelKind::Sampler)
    {
        const int step = ((int) std::lround (r.startBeat / kStepBeats)) % len;
        setStep (r.pattern, ch.id, step, juce::jlimit (1, 127, (int) std::lround (r.velocity * 127.0f)));
    }
    else
    {
        const double grid = kStepBeats;
        Note n;
        n.start = std::fmod (std::round (r.startBeat / grid) * grid, p.lengthBeats());
        n.length = juce::jmax (grid, std::round (r.lengthBeats / grid) * grid);
        n.pitch = r.note;
        n.velocity = r.velocity;
        auto list = p.notes[ch.id];
        list.erase (std::remove_if (list.begin(), list.end(), [&n] (const Note& o)
                    { return o.pitch == n.pitch && std::abs (o.start - n.start) < 1.0e-6; }), list.end());
        list.push_back (n);
        setNotes (r.pattern, ch.id, std::move (list));
    }
}

double Project::getSongLengthBeats() const
{
    double end = 0.0;
    for (auto& c : clips) end = juce::jmax (end, c.start + c.length);
    for (auto& a : audioClips) end = juce::jmax (end, a.start + a.length);
    return juce::jmax (4.0, std::ceil (end / 4.0) * 4.0);
}

//==============================================================================
CompiledPattern Project::compilePattern (const PatternData& p) const
{
    CompiledPattern cp;
    cp.lengthBeats = p.lengthBeats();
    for (size_t ci = 0; ci < channels.size(); ++ci)
    {
        const auto& ch = channels[ci];
        if (auto it = p.steps.find (ch.id); it != p.steps.end())
        {
            const auto& steps = it->second;
            const int len = juce::jmin ((int) steps.size(), p.lengthSteps);
            const bool oneShot = ch.strip != nullptr && ch.strip->instrument != nullptr && ch.strip->instrument->isOneShot();
            for (int s = 0; s < len; ++s)
            {
                if (steps[(size_t) s] == 0) continue;
                CompiledEvent e;
                e.beat = s * kStepBeats;
                e.channel = (int) ci;
                e.note = ch.stepNote;
                e.velocity = steps[(size_t) s] / 127.0f;
                e.swing = (s % 2) == 1;
                if (oneShot)
                {
                    e.length = kStepBeats;
                }
                else
                {
                    // legato steps: hold until just before the next active step
                    int gap = 1;
                    while (gap < len && steps[(size_t) ((s + gap) % len)] == 0) ++gap;
                    e.length = gap * kStepBeats * 0.97;
                }
                cp.events.push_back (e);
            }
        }
        if (auto it = p.notes.find (ch.id); it != p.notes.end())
        {
            for (auto& n : it->second)
            {
                if (n.start >= cp.lengthBeats) continue;
                CompiledEvent e;
                e.beat = juce::jmax (0.0, n.start);
                e.length = juce::jmax (1.0 / 64.0, n.length);
                e.channel = (int) ci;
                e.note = juce::jlimit (0, 127, n.pitch);
                e.velocity = juce::jlimit (0.0f, 1.0f, n.velocity);
                const double inPair = std::fmod (e.beat, 0.5);
                e.swing = std::abs (inPair - 0.25) < 1.0e-6;
                cp.events.push_back (e);
            }
        }
    }
    std::stable_sort (cp.events.begin(), cp.events.end(),
                      [] (const CompiledEvent& a, const CompiledEvent& b) { return a.beat < b.beat; });
    return cp;
}

std::unique_ptr<BeatSnapshot> Project::buildSnapshot() const
{
    auto snap = std::make_unique<BeatSnapshot>();
    for (auto& c : channels)
    {
        if ((int) snap->channels.size() >= kMaxChannels) break;
        snap->channels.push_back (c.strip);
        snap->stepNotes.push_back (c.stepNote);
        snap->inserts.push_back (c.inserts);
    }
    snap->masterInserts = masterInserts;
    for (auto& p : patterns)
        snap->patterns.push_back (compilePattern (p));
    for (auto& c : clips)
        snap->clips.push_back ({ c.pattern, c.lane, c.start, c.length, c.offset });
    for (auto& a : audioClips)
        if (a.sample != nullptr)
            snap->audioClips.push_back ({ a.sample, a.lane, a.start, a.length, a.offsetSeconds, a.gain });
    for (auto& l : lanes)
        snap->laneMuted.push_back (l.muted);
    snap->songLengthBeats = getSongLengthBeats();
    return snap;
}

void Project::changed (bool structural)
{
    if (structural && onPublish)
        onPublish (buildSnapshot());
    sendChangeMessage();
}

//==============================================================================
static juce::String stepsToHex (const std::vector<juce::uint8>& s)
{
    juce::String out;
    for (auto v : s)
        out << juce::String::toHexString ((int) v).paddedLeft ('0', 2);
    return out;
}

static std::vector<juce::uint8> stepsFromHex (const juce::String& h)
{
    std::vector<juce::uint8> s;
    for (int i = 0; i + 1 < h.length(); i += 2)
        s.push_back ((juce::uint8) h.substring (i, i + 2).getHexValue32());
    return s;
}

juce::var Project::effectToVar (const Effect::Ptr& fx) const
{
    if (fx == nullptr) return {};
    auto* o = new juce::DynamicObject();
    o->setProperty ("type", fx->getTypeId());
    o->setProperty ("state", fx->saveState());
    o->setProperty ("bypass", fx->bypassed.load());
    return juce::var (o);
}

Effect::Ptr Project::effectFromVar (const juce::var& v) const
{
    if (! v.isObject()) return nullptr;
    const auto type = v["type"].toString();
    Effect::Ptr fx;
    if (type == "plugin")
    {
        if (pluginHost != nullptr)
            fx = pluginHost->createPluginEffect (v["state"], sampleRate, blockSize);
    }
    else
    {
        fx = createBuiltInEffect (type);
        if (fx != nullptr)
            fx->loadState (v["state"]);
    }
    if (fx != nullptr)
    {
        fx->bypassed.store ((bool) v["bypass"]);
        fx->prepare (sampleRate, blockSize);
    }
    return fx;
}

juce::var Project::channelToVar (const ChannelModel& c) const
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("id", c.id);
    o->setProperty ("name", c.name);
    o->setProperty ("kind", channelKindName (c.kind));
    o->setProperty ("colour", c.colour.toString());
    o->setProperty ("stepNote", c.stepNote);
    o->setProperty ("sample", c.sampleRef);
    if (c.strip != nullptr)
    {
        o->setProperty ("volume", c.strip->volume.load());
        o->setProperty ("pan", c.strip->pan.load());
        o->setProperty ("mute", c.strip->mute.load());
        o->setProperty ("solo", c.strip->solo.load());
        o->setProperty ("choke", c.strip->chokeGroup.load());
        if (c.strip->instrument != nullptr)
            o->setProperty ("instrument", c.strip->instrument->saveState());
    }
    juce::Array<juce::var> ins;
    for (auto& fx : c.inserts)
        ins.add (effectToVar (fx));
    o->setProperty ("inserts", ins);
    return juce::var (o);
}

bool Project::channelFromVar (const juce::var& v)
{
    const auto kind = channelKindFromName (v["kind"].toString());
    Instrument::Ptr inst;
    if (kind == ChannelKind::Plugin)
    {
        if (pluginHost != nullptr)
            inst = pluginHost->createPluginInstrument (v["instrument"], sampleRate, blockSize);
        if (inst == nullptr)
            return false;
    }
    auto& c = addChannel (kind, v["name"].toString(), inst);
    const int savedId = (int) v["id"];
    if (savedId > 0)
    {
        c.id = savedId;
        nextChannelId = juce::jmax (nextChannelId, savedId + 1);
    }
    if (v.hasProperty ("colour"))
        c.colour = juce::Colour::fromString (v["colour"].toString());
    c.stepNote = v.hasProperty ("stepNote") ? (int) v["stepNote"] : c.stepNote;
    if (c.strip != nullptr)
    {
        c.strip->volume.store ((float) v.getProperty ("volume", 0.8f));
        c.strip->pan.store ((float) v.getProperty ("pan", 0.0f));
        c.strip->mute.store ((bool) v["mute"]);
        c.strip->solo.store ((bool) v["solo"]);
        c.strip->chokeGroup.store ((int) v["choke"]);
        if (c.strip->instrument != nullptr && kind != ChannelKind::Plugin)
            c.strip->instrument->loadState (v["instrument"]);
    }
    c.sampleRef = v["sample"].toString();
    if (c.sampleRef.isNotEmpty() && c.strip != nullptr && c.strip->instrument != nullptr)
    {
        auto f = paths::fromRef (c.sampleRef);
        if (f.existsAsFile())
            c.strip->instrument->setSample (SampleCache::get().load (f));
    }
    if (auto* ins = v["inserts"].getArray())
        for (int i = 0; i < juce::jmin (kNumInserts, ins->size()); ++i)
            c.inserts[(size_t) i] = effectFromVar (ins->getReference (i));
    return true;
}

juce::var Project::editableStateToVar() const
{
    auto* root = new juce::DynamicObject();
    juce::Array<juce::var> pats;
    for (auto& p : patterns)
    {
        auto* po = new juce::DynamicObject();
        po->setProperty ("name", p.name);
        po->setProperty ("length", p.lengthSteps);
        auto* so = new juce::DynamicObject();
        for (auto& [id, s] : p.steps)
            if (std::any_of (s.begin(), s.end(), [] (juce::uint8 x) { return x > 0; }))
                so->setProperty (juce::String (id), stepsToHex (s));
        po->setProperty ("steps", juce::var (so));
        auto* no = new juce::DynamicObject();
        for (auto& [id, list] : p.notes)
        {
            if (list.empty()) continue;
            juce::Array<juce::var> arr;
            for (auto& n : list)
                arr.add (juce::Array<juce::var> { n.start, n.length, n.pitch, n.velocity });
            no->setProperty (juce::String (id), arr);
        }
        po->setProperty ("notes", juce::var (no));
        pats.add (juce::var (po));
    }
    root->setProperty ("patterns", pats);

    juce::Array<juce::var> cl;
    for (auto& c : clips)
        cl.add (juce::Array<juce::var> { c.pattern, c.lane, c.start, c.length, c.offset });
    root->setProperty ("clips", cl);
    juce::Array<juce::var> ln;
    for (auto& l : lanes)
    {
        auto* lo = new juce::DynamicObject();
        lo->setProperty ("name", l.name);
        lo->setProperty ("muted", l.muted);
        ln.add (juce::var (lo));
    }
    root->setProperty ("lanes", ln);
    return juce::var (root);
}

void Project::editableStateFromVar (const juce::var& v)
{
    if (auto* pats = v["patterns"].getArray())
    {
        patterns.assign (kNumPatterns, {});
        for (int i = 0; i < kNumPatterns; ++i)
        {
            auto& p = patterns[(size_t) i];
            p.name = patternName (i);
            if (i >= pats->size()) continue;
            const auto& pv = pats->getReference (i);
            p.name = pv.getProperty ("name", patternName (i)).toString();
            p.lengthSteps = juce::jlimit (1, 64, (int) pv.getProperty ("length", 16));
            if (auto* so = pv["steps"].getDynamicObject())
                for (auto& prop : so->getProperties())
                {
                    auto s = stepsFromHex (prop.value.toString());
                    s.resize ((size_t) p.lengthSteps, 0);
                    p.steps[prop.name.toString().getIntValue()] = std::move (s);
                }
            if (auto* no = pv["notes"].getDynamicObject())
                for (auto& prop : no->getProperties())
                {
                    std::vector<Note> list;
                    if (auto* arr = prop.value.getArray())
                        for (auto& nv : *arr)
                            if (nv.size() >= 4)
                                list.push_back ({ (double) nv[0], (double) nv[1], (int) nv[2], (float) nv[3] });
                    p.notes[prop.name.toString().getIntValue()] = std::move (list);
                }
        }
    }
    if (auto* cl = v["clips"].getArray())
    {
        clips.clear();
        for (auto& c : *cl)
            if (c.size() >= 5)
                clips.push_back ({ (int) c[0], (int) c[1], (double) c[2], (double) c[3], (double) c[4] });
    }
    if (auto* ln = v["lanes"].getArray())
    {
        lanes.clear();
        for (auto& l : *ln)
            lanes.push_back ({ l["name"].toString(), (bool) l["muted"] });
    }
}

juce::var Project::toVar() const
{
    auto root = editableStateToVar();
    auto* o = root.getDynamicObject();
    o->setProperty ("format", "merge-project");
    o->setProperty ("version", 1);
    o->setProperty ("name", name);
    o->setProperty ("bpm", bpm);
    o->setProperty ("swing", swing);
    o->setProperty ("key", key.toString());
    o->setProperty ("kit", kitName);
    o->setProperty ("genre", genre);
    juce::Array<juce::var> ch;
    for (auto& c : channels)
        ch.add (channelToVar (c));
    o->setProperty ("channels", ch);
    juce::Array<juce::var> master;
    for (auto& fx : masterInserts)
        master.add (effectToVar (fx));
    o->setProperty ("master", master);
    juce::Array<juce::var> audio;
    for (auto& a : audioClips)
    {
        auto* ao = new juce::DynamicObject();
        ao->setProperty ("sample", a.sampleRef);
        ao->setProperty ("name", a.name);
        ao->setProperty ("lane", a.lane);
        ao->setProperty ("start", a.start);
        ao->setProperty ("length", a.length);
        ao->setProperty ("offset", a.offsetSeconds);
        ao->setProperty ("gain", a.gain);
        audio.add (juce::var (ao));
    }
    o->setProperty ("audioClips", audio);
    return root;
}

bool Project::fromVar (const juce::var& v)
{
    if (v["format"].toString() != "merge-project")
        return false;
    channels.clear();
    nextChannelId = 1;
    for (auto& fx : masterInserts) fx = nullptr;
    name = v.getProperty ("name", "Untitled").toString();
    bpm = juce::jlimit (20.0, 400.0, (double) v.getProperty ("bpm", 120.0));
    swing = juce::jlimit (50.0, 75.0, (double) v.getProperty ("swing", 50.0));
    key = MusicalKey::fromString (v["key"].toString());
    kitName = v["kit"].toString();
    genre = v["genre"].toString();
    if (auto* ch = v["channels"].getArray())
        for (auto& c : *ch)
            channelFromVar (c);
    if (auto* master = v["master"].getArray())
        for (int i = 0; i < juce::jmin (kNumInserts, master->size()); ++i)
            masterInserts[(size_t) i] = effectFromVar (master->getReference (i));
    editableStateFromVar (v);
    audioClips.clear();
    if (auto* audio = v["audioClips"].getArray())
        for (auto& a : *audio)
        {
            AudioClipModel m;
            m.sampleRef = a["sample"].toString();
            m.name = a["name"].toString();
            m.lane = (int) a["lane"];
            m.start = (double) a["start"];
            m.length = (double) a["length"];
            m.offsetSeconds = (double) a["offset"];
            m.gain = (float) a.getProperty ("gain", 1.0f);
            auto f = paths::fromRef (m.sampleRef);
            if (f.existsAsFile())
                m.sample = SampleCache::get().load (f);
            audioClips.push_back (std::move (m));
        }
    undoStack.clear();
    redoStack.clear();
    return true;
}

bool Project::save (const juce::File& f)
{
    f.getParentDirectory().createDirectory();
    if (! f.replaceWithText (juce::JSON::toString (toVar(), false)))
        return false;
    file = f;
    name = f.getFileNameWithoutExtension();
    sendChangeMessage();
    return true;
}

bool Project::load (const juce::File& f)
{
    auto v = juce::JSON::parse (f);
    if (! fromVar (v))
        return false;
    file = f;
    name = f.getFileNameWithoutExtension();
    changed();
    return true;
}

std::unique_ptr<Project> Project::cloneForRender() const
{
    auto p = std::make_unique<Project>();
    p->channels.clear();
    p->sampleRate = sampleRate;
    p->blockSize = blockSize;
    p->pluginHost = pluginHost;
    p->bpm = bpm;
    p->swing = swing;
    p->key = key;
    p->patterns = patterns;
    p->clips = clips;
    p->audioClips = audioClips;
    p->lanes = lanes;
    p->nextChannelId = nextChannelId;
    for (auto& c : channels)
    {
        ChannelModel copy = c;
        Instrument::Ptr inst = (c.strip != nullptr && c.strip->instrument != nullptr) ? c.strip->instrument->clone() : nullptr;
        copy.strip = new ChannelStrip (inst);
        copy.strip->volume.store (c.strip->volume.load());
        copy.strip->pan.store (c.strip->pan.load());
        copy.strip->mute.store (c.strip->mute.load());
        copy.strip->solo.store (c.strip->solo.load());
        copy.strip->chokeGroup.store (c.strip->chokeGroup.load());
        for (size_t i = 0; i < kNumInserts; ++i)
            copy.inserts[i] = c.inserts[i] != nullptr ? c.inserts[i]->clone() : nullptr;
        p->channels.push_back (std::move (copy));
    }
    for (size_t i = 0; i < kNumInserts; ++i)
        p->masterInserts[i] = masterInserts[i] != nullptr ? masterInserts[i]->clone() : nullptr;
    return p;
}

//==============================================================================
void Project::checkpoint()
{
    undoStack.push_back (editableStateToVar());
    if (undoStack.size() > 100)
        undoStack.erase (undoStack.begin());
    redoStack.clear();
}

bool Project::undo()
{
    if (undoStack.empty()) return false;
    redoStack.push_back (editableStateToVar());
    editableStateFromVar (undoStack.back());
    undoStack.pop_back();
    changed();
    return true;
}

bool Project::redo()
{
    if (redoStack.empty()) return false;
    undoStack.push_back (editableStateToVar());
    editableStateFromVar (redoStack.back());
    redoStack.pop_back();
    changed();
    return true;
}

} // namespace merge
