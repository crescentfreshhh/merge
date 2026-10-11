#pragma once

#include "controllers/MidiMapping.h"
#include <juce_events/juce_events.h>

namespace merge
{

class AppContext;

/** Every action a MIDI controller can drive, MIDI learn, and dispatch of mapped messages. */
class ControllerManager : public juce::ChangeBroadcaster, private juce::Timer
{
public:
    explicit ControllerManager (AppContext& ctx);
    ~ControllerManager() override;

    struct Control
    {
        enum Kind { Button, Momentary, Continuous, Jog };
        juce::String id, name, group;
        Kind kind = Button;
        std::function<void (float)> apply; // Button: on press, Momentary: 1/0, Continuous: 0..1, Jog: signed steps
    };

    const std::vector<Control>& getControls() const noexcept { return controls; }
    MidiMapping& getMapping() noexcept { return mapping; }

    void startLearn (const juce::String& controlId);
    void cancelLearn();
    juce::String getLearning() const { return learning; }
    juce::String getLastMessage() const { return lastMessage; }

    void saveMapping();
    bool loadMapping (const juce::File& f);
    juce::File currentMappingFile() const;

private:
    void timerCallback() override;
    void add (const juce::String& id, const juce::String& name, const juce::String& group, Control::Kind kind,
              std::function<void (float)> fn);
    void registerControls();
    void dispatch (const juce::MidiMessage& m);
    const Control* findControl (const juce::String& id) const;

    AppContext& ctx;
    std::vector<Control> controls;
    MidiMapping mapping;
    juce::String learning, lastMessage;
    std::array<juce::uint32, 4> lastJog {};
    std::map<juce::String, bool> toggleStates;
};

} // namespace merge
