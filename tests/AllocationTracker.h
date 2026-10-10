#pragma once

#include <atomic>

namespace evil::test
{
/** While alive, counts every heap allocation and free made by the constructing thread. */
class ScopedAllocationTracking
{
public:
    ScopedAllocationTracking() noexcept;
    ~ScopedAllocationTracking();

    long allocationCount() const noexcept;
    long deallocationCount() const noexcept;

    /** Stop/restart counting (e.g. around set-up work done between audio blocks). */
    static void pause() noexcept;
    static void resume() noexcept;

    ScopedAllocationTracking (const ScopedAllocationTracking&) = delete;
    ScopedAllocationTracking& operator= (const ScopedAllocationTracking&) = delete;
};

/** True when malloc/free themselves are tracked, not just operator new/delete. */
bool interposesCAllocator() noexcept;
} // namespace evil::test
