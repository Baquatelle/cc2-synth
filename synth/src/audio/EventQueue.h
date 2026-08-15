#pragma once

#include <array>
#include <atomic>
#include <cstddef>

/// Single-producer / single-consumer lock-free ring buffer.
///
/// This is the *only* channel by which the UI thread tells the audio thread to
/// start or stop a note, and it exists because of a hard constraint: the audio
/// callback must never block. Taking a mutex there risks priority inversion --
/// the OS can preempt the UI thread while it holds the lock, and the audio thread
/// then misses its deadline and the output glitches.
template <typename T, std::size_t Capacity> class EventQueue
{
  public:
    static_assert(Capacity >= 2, "capacity must be at least 2");
    static_assert((Capacity & (Capacity - 1)) == 0, "capacity must be a power of two");

    /// Producer side. Returns false if the queue is full (event dropped).
    bool push(const T& aItem)
    {
        const std::size_t write = mWriteIndex.load(std::memory_order_relaxed);
        const std::size_t next  = (write + 1) & kMask;

        // Full when advancing would collide with the read cursor.
        if (next == mReadIndex.load(std::memory_order_acquire))
        {
            return false;
        }

        mBuffer[write] = aItem;

        // Release: the slot write above must be visible before the new index is.
        mWriteIndex.store(next, std::memory_order_release);
        return true;
    }

    /// Consumer side. Returns false when empty.
    bool pop(T& aOutItem)
    {
        const std::size_t read = mReadIndex.load(std::memory_order_relaxed);
        if (read == mWriteIndex.load(std::memory_order_acquire))
        {
            return false;
        }

        aOutItem = mBuffer[read];
        mReadIndex.store((read + 1) & kMask, std::memory_order_release);
        return true;
    }

    bool empty() const
    {
        return mReadIndex.load(std::memory_order_acquire) == mWriteIndex.load(std::memory_order_acquire);
    }

  private:
    static constexpr std::size_t kMask = Capacity - 1;

    std::array<T, Capacity>  mBuffer{};
    std::atomic<std::size_t> mWriteIndex{0};
    std::atomic<std::size_t> mReadIndex{0};
};
