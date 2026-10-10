// Counts heap operations made by threads that opt in, so tests can prove the
// audio path never allocates or frees. operator new/delete are replaced
// everywhere; on glibc the C allocator is interposed too, which also catches
// Eigen, JUCE HeapBlock and other direct malloc users.

#include "AllocationTracker.h"

#include <algorithm>
#include <cstdlib>
#include <new>

namespace evil::test {
namespace {
// Constant-initialised, so touching it inside malloc never allocates.
thread_local bool tracking = false;
std::atomic<long> allocations{0};
std::atomic<long> deallocations{0};

inline void countAllocation() noexcept {
  if (tracking)
    allocations.fetch_add(1, std::memory_order_relaxed);
}

inline void countDeallocation(void *p) noexcept {
  if (tracking && p != nullptr)
    deallocations.fetch_add(1, std::memory_order_relaxed);
}
} // namespace

ScopedAllocationTracking::ScopedAllocationTracking() noexcept {
  allocations = 0;
  deallocations = 0;
  tracking = true;
}

ScopedAllocationTracking::~ScopedAllocationTracking() { tracking = false; }

long ScopedAllocationTracking::allocationCount() const noexcept {
  return allocations.load();
}
long ScopedAllocationTracking::deallocationCount() const noexcept {
  return deallocations.load();
}

void ScopedAllocationTracking::pause() noexcept { tracking = false; }
void ScopedAllocationTracking::resume() noexcept { tracking = true; }

bool interposesCAllocator() noexcept {
#if defined(__GLIBC__)
  return true;
#else
  return false;
#endif
}
} // namespace evil::test

#if defined(__GLIBC__)
extern "C" {
void *__libc_malloc(size_t);
void *__libc_calloc(size_t, size_t);
void *__libc_realloc(void *, size_t);
void __libc_free(void *);
void *__libc_memalign(size_t, size_t);

void *malloc(size_t size) {
  evil::test::countAllocation();
  return __libc_malloc(size);
}

void *calloc(size_t n, size_t size) {
  evil::test::countAllocation();
  return __libc_calloc(n, size);
}

void *realloc(void *p, size_t size) {
  evil::test::countAllocation();
  return __libc_realloc(p, size);
}

void free(void *p) {
  evil::test::countDeallocation(p);
  __libc_free(p);
}

void *memalign(size_t alignment, size_t size) {
  evil::test::countAllocation();
  return __libc_memalign(alignment, size);
}

void *aligned_alloc(size_t alignment, size_t size) {
  evil::test::countAllocation();
  return __libc_memalign(alignment, size);
}

int posix_memalign(void **out, size_t alignment, size_t size) {
  evil::test::countAllocation();
  void *p = __libc_memalign(alignment, size);
  if (p == nullptr)
    return 12; // ENOMEM
  *out = p;
  return 0;
}
}

// With the C allocator interposed, new/delete only need to route through it.
void *operator new(std::size_t size) {
  if (void *p = std::malloc(size == 0 ? 1 : size))
    return p;
  throw std::bad_alloc();
}
void *operator new[](std::size_t size) { return operator new(size); }
void *operator new(std::size_t size, std::align_val_t a) {
  void *p = nullptr;
  if (posix_memalign(&p, std::max<std::size_t>((std::size_t)a, sizeof(void *)),
                     size == 0 ? 1 : size) != 0)
    throw std::bad_alloc();
  return p;
}
void *operator new[](std::size_t size, std::align_val_t a) {
  return operator new(size, a);
}
void operator delete(void *p) noexcept { std::free(p); }
void operator delete[](void *p) noexcept { std::free(p); }
void operator delete(void *p, std::size_t) noexcept { std::free(p); }
void operator delete[](void *p, std::size_t) noexcept { std::free(p); }
void operator delete(void *p, std::align_val_t) noexcept { std::free(p); }
void operator delete[](void *p, std::align_val_t) noexcept { std::free(p); }
void operator delete(void *p, std::size_t, std::align_val_t) noexcept {
  std::free(p);
}
void operator delete[](void *p, std::size_t, std::align_val_t) noexcept {
  std::free(p);
}

#else // Other platforms: count C++ allocations only.

void *operator new(std::size_t size) {
  evil::test::countAllocation();
  if (void *p = std::malloc(size == 0 ? 1 : size))
    return p;
  throw std::bad_alloc();
}
void *operator new[](std::size_t size) { return operator new(size); }
void operator delete(void *p) noexcept {
  evil::test::countDeallocation(p);
  std::free(p);
}
void operator delete[](void *p) noexcept { operator delete(p); }
void operator delete(void *p, std::size_t) noexcept { operator delete(p); }
void operator delete[](void *p, std::size_t) noexcept { operator delete(p); }
#endif
