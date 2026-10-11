#pragma once

#include "SpscQueue.h"
#include <atomic>
#include <memory>

namespace merge
{

/** Publishes immutable snapshots from the message thread to the audio thread.

    The message thread builds a new T and publishes it; the audio thread picks
    up the newest one at the start of each block with acquire(). Old snapshots
    are handed back through a lock-free queue and deleted on the message thread
    by collect(), so the audio thread never allocates or frees memory. */
template <typename T>
class RtShared
{
public:
    RtShared() = default;
    RtShared (const RtShared&) = delete;
    RtShared& operator= (const RtShared&) = delete;

    ~RtShared()
    {
        delete pending.exchange (nullptr);
        delete current;
        collect();
    }

    /** Message thread. */
    void publish (std::unique_ptr<T> next)
    {
        T* stale = pending.exchange (next.release(), std::memory_order_acq_rel);
        delete stale; // never seen by the audio thread
        collect();
    }

    /** Audio thread: returns the newest snapshot (may be nullptr before the first publish). */
    T* acquire() noexcept
    {
        if (T* p = pending.exchange (nullptr, std::memory_order_acq_rel))
        {
            if (current != nullptr && ! retired.push (current))
                leaked.fetch_add (1); // extremely unlikely: GC hasn't run for 512 publishes
            current = p;
        }
        return current;
    }

    /** Audio thread: the snapshot picked up by the last acquire(). */
    T* get() const noexcept { return current; }

    /** Message thread: frees snapshots the audio thread has finished with. */
    void collect()
    {
        T* p = nullptr;
        while (retired.pop (p))
            delete p;
    }

private:
    std::atomic<T*> pending { nullptr };
    T* current = nullptr;
    SpscQueue<T*, 512> retired;
    std::atomic<int> leaked { 0 };
};

} // namespace merge
