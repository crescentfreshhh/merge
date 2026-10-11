#include "ReleasePool.h"

namespace merge
{

ReleasePool& ReleasePool::get()
{
    static ReleasePool pool;
    return pool;
}

void ReleasePool::addObject (juce::ReferenceCountedObject* object)
{
    const juce::ScopedLock sl (lock);
    for (auto& e : entries)
        if (e.object.get() == object)
            return;

    entries.push_back ({ juce::ReferenceCountedObjectPtr<juce::ReferenceCountedObject> (object), 0 });

    if (! isTimerRunning() && juce::MessageManager::getInstanceWithoutCreating() != nullptr)
        juce::MessageManager::callAsync ([this] { startTimer (1000); });
}

void ReleasePool::collect()
{
    std::vector<juce::ReferenceCountedObjectPtr<juce::ReferenceCountedObject>> doomed;
    {
        const juce::ScopedLock sl (lock);
        for (auto it = entries.begin(); it != entries.end();)
        {
            if (it->object->getReferenceCount() == 1)
            {
                if (++it->idlePasses >= 2)
                {
                    doomed.push_back (std::move (it->object));
                    it = entries.erase (it);
                    continue;
                }
            }
            else
            {
                it->idlePasses = 0;
            }
            ++it;
        }
    }
    doomed.clear(); // deletes outside the lock
}

int ReleasePool::size() const
{
    const juce::ScopedLock sl (lock);
    return (int) entries.size();
}

} // namespace merge
