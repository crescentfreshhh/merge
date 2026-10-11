#include "MidiMapping.h"

namespace merge
{

bool MidiBinding::matches (const juce::MidiMessage& m) const noexcept
{
    if (m.getChannel() != channel) return false;
    if (type == Type::Note)
        return m.isNoteOnOrOff() && m.getNoteNumber() == number;
    return m.isController() && m.getControllerNumber() == number;
}

juce::String MidiBinding::describe() const
{
    return (type == Type::Note ? "Note " + juce::MidiMessage::getMidiNoteName (number, true, true, 3) : "CC " + juce::String (number))
           + " / ch " + juce::String (channel);
}

const MidiBinding* MidiMapping::find (const juce::MidiMessage& m) const
{
    for (auto& b : bindings)
        if (b.matches (m)) return &b;
    return nullptr;
}

void MidiMapping::bind (const MidiBinding& nb)
{
    bindings.erase (std::remove_if (bindings.begin(), bindings.end(), [&nb] (const MidiBinding& b)
    {
        return b.control == nb.control || (b.type == nb.type && b.channel == nb.channel && b.number == nb.number);
    }), bindings.end());
    bindings.push_back (nb);
}

void MidiMapping::unbindControl (const juce::String& control)
{
    bindings.erase (std::remove_if (bindings.begin(), bindings.end(), [&control] (const MidiBinding& b) { return b.control == control; }),
                    bindings.end());
}

const MidiBinding* MidiMapping::bindingFor (const juce::String& control) const
{
    for (auto& b : bindings)
        if (b.control == control) return &b;
    return nullptr;
}

static const char* modeNames[] { "absolute", "relative", "toggle", "momentary" };

juce::var MidiMapping::toVar() const
{
    auto* root = new juce::DynamicObject();
    root->setProperty ("format", "merge-midi-mapping");
    root->setProperty ("name", name);
    juce::Array<juce::var> arr;
    for (auto& b : bindings)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("type", b.type == MidiBinding::Type::Note ? "note" : "cc");
        o->setProperty ("channel", b.channel);
        o->setProperty ("number", b.number);
        o->setProperty ("control", b.control);
        o->setProperty ("mode", modeNames[(int) b.mode]);
        arr.add (juce::var (o));
    }
    root->setProperty ("bindings", arr);
    return juce::var (root);
}

bool MidiMapping::fromVar (const juce::var& v)
{
    if (v["format"].toString() != "merge-midi-mapping") return false;
    name = v.getProperty ("name", "Controller").toString();
    bindings.clear();
    if (auto* arr = v["bindings"].getArray())
        for (auto& o : *arr)
        {
            MidiBinding b;
            b.type = o["type"].toString() == "note" ? MidiBinding::Type::Note : MidiBinding::Type::CC;
            b.channel = juce::jlimit (1, 16, (int) o.getProperty ("channel", 1));
            b.number = juce::jlimit (0, 127, (int) o["number"]);
            b.control = o["control"].toString();
            const auto mode = o["mode"].toString();
            for (int i = 0; i < 4; ++i)
                if (mode == modeNames[i]) b.mode = (MidiBinding::Mode) i;
            if (b.control.isNotEmpty()) bindings.push_back (b);
        }
    return true;
}

bool MidiMapping::save (const juce::File& f) const
{
    f.getParentDirectory().createDirectory();
    return f.replaceWithText (juce::JSON::toString (toVar()));
}

bool MidiMapping::load (const juce::File& f)
{
    return fromVar (juce::JSON::parse (f));
}

float MidiMapping::valueFor (const MidiBinding& b, const juce::MidiMessage& m)
{
    if (b.type == MidiBinding::Type::Note)
        return m.isNoteOn() ? 1.0f : 0.0f;
    const int v = m.getControllerValue();
    if (b.mode == MidiBinding::Mode::Relative)
        return (float) (v >= 64 ? v - 128 : v); // two's complement: 1..63 = +, 65..127 = -
    if (b.mode == MidiBinding::Mode::Toggle || b.mode == MidiBinding::Mode::Momentary)
        return v >= 64 ? 1.0f : 0.0f;
    return v / 127.0f;
}

} // namespace merge
