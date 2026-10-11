#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <type_traits>

namespace merge
{

/** Wait-free single-producer / single-consumer ring buffer for trivially
    copyable messages. Used to pass events between the UI, MIDI and audio
    threads without locks or allocation. */
template <typename T, size_t Capacity>
class SpscQueue
{
    static_assert (std::is_trivially_copyable_v<T>, "SpscQueue only carries trivially copyable types");
    static_assert ((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of two");

public:
    bool push (const T& item) noexcept
    {
        const auto w = writePos.load (std::memory_order_relaxed);
        const auto r = readPos.load (std::memory_order_acquire);
        if (w - r >= Capacity)
            return false;
        slots[w & (Capacity - 1)] = item;
        writePos.store (w + 1, std::memory_order_release);
        return true;
    }

    bool pop (T& item) noexcept
    {
        const auto r = readPos.load (std::memory_order_relaxed);
        const auto w = writePos.load (std::memory_order_acquire);
        if (r == w)
            return false;
        item = slots[r & (Capacity - 1)];
        readPos.store (r + 1, std::memory_order_release);
        return true;
    }

    bool isEmpty() const noexcept
    {
        return readPos.load (std::memory_order_acquire) == writePos.load (std::memory_order_acquire);
    }

private:
    std::array<T, Capacity> slots {};
    alignas (64) std::atomic<size_t> writePos { 0 };
    alignas (64) std::atomic<size_t> readPos { 0 };
};

} // namespace merge
