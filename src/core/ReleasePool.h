#pragma once

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>

namespace merge
{

/** Keeps a reference to every object that the audio thread may touch
    (sample buffers, instruments, effects...). The audio thread can take and
    drop references freely because the pool's reference means the count never
    reaches zero there. A timer on the message thread deletes objects that
    nobody else has referenced for two consecutive passes. */
class ReleasePool : private juce::Timer
{
public:
    static ReleasePool& get();

    template <typename T>
    void add (const juce::ReferenceCountedObjectPtr<T>& object)
    {
        if (object != nullptr)
            addObject (object.get());
    }

    /** Frees unused objects. Call from the message thread (or a test thread). */
    void collect();

    int size() const;

    /** At shutdown: drops every reference so objects (e.g. plugins) are destroyed while JUCE is still alive. */
    void releaseAll();

private:
    ReleasePool() = default;
    void addObject (juce::ReferenceCountedObject*);
    void timerCallback() override { collect(); }

    struct Entry
    {
        juce::ReferenceCountedObjectPtr<juce::ReferenceCountedObject> object;
        int idlePasses = 0;
    };

    mutable juce::CriticalSection lock;
    std::vector<Entry> entries;
};

} // namespace merge
