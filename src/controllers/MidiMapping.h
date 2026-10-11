#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

namespace merge
{

/** One MIDI -> control binding. */
struct MidiBinding
{
    enum class Type { Note, CC };
    enum class Mode { Absolute, Relative, Toggle, Momentary };

    Type type = Type::CC;
    int channel = 1;   // 1-16
    int number = 0;    // note or CC number
    juce::String control;
    Mode mode = Mode::Absolute;

    bool matches (const juce::MidiMessage& m) const noexcept;
    juce::String describe() const;
};

/** A set of bindings, saved as JSON (Documents/Merge/Mappings/<name>.json). */
class MidiMapping
{
public:
    juce::String name { "My controller" };
    std::vector<MidiBinding> bindings;

    const MidiBinding* find (const juce::MidiMessage& m) const;
    void bind (const MidiBinding& b);       // replaces any binding for the same control or the same MIDI source
    void unbindControl (const juce::String& control);
    const MidiBinding* bindingFor (const juce::String& control) const;

    juce::var toVar() const;
    bool fromVar (const juce::var&);
    bool save (const juce::File& f) const;
    bool load (const juce::File& f);

    /** Value for a binding from a message: 0..1 for absolute, signed steps for relative, 1/0 for buttons. */
    static float valueFor (const MidiBinding& b, const juce::MidiMessage& m);
};

} // namespace merge
