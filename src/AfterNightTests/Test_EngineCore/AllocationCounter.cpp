// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "AllocationCounter.h"

#include <atomic>
#include <cstdlib>
#include <new>

// The global allocation functions, replaced for this executable so that an allocation can be
// counted. Replacement is a language feature rather than a trick, and the whole executable sees
// these - including gtest, which is why a measurement has to be taken around the narrowest possible
// region.
//
// The sized, array and nothrow forms are replaced alongside the plain ones so that no allocation
// reaches a deallocation from a different implementation. The nothrow pair is not optional even
// though nothing here asks for one: memory obtained from `operator new(size, nothrow)` is released
// by the *plain* `operator delete`, so replacing the deallocation while leaving that allocation to
// the library hands one pointer to two implementations. An ordinary build hides it, because the
// library's nothrow form forwards to the replaced throwing one; under a sanitizer, which replaces
// the whole set itself, that pointer reaches free() as a region the sanitizer recorded as `new` and
// the run aborts on alloc-dealloc-mismatch. gtest makes such a request while running a suite, so
// this is not a hypothetical crossing.
//
// The aligned forms are deliberately left alone, and they stay consistent for the reason the
// nothrow forms did not: an aligned request is released through an aligned deallocation, so both
// halves reach the same implementation whichever one that is.
//
// One executable cannot have both this and ThreadSanitizer. That sanitizer's C++ runtime defines
// the same set of functions, strongly, and clang links it with --whole-archive, so it is in
// every link: a program that also replaces them fails to link on ten duplicate symbols.
// AddressSanitizer leaves the replacement to the program, which is why that build works. Under
// ThreadSanitizer this file therefore replaces nothing and counts nothing: the suite links, and the
// campaign's matrix skips it there for this reason. Nothing is lost while nothing here starts a
// worker - there would be no race for it to find. A case that needs workers belongs in a suite that
// does not carry this file.

#if defined(__SANITIZE_THREAD__)
#define AN_ALLOCATION_COUNTER_REPLACES 0
#elif defined(__has_feature)
#if __has_feature(thread_sanitizer)
#define AN_ALLOCATION_COUNTER_REPLACES 0
#endif
#endif
#if !defined(AN_ALLOCATION_COUNTER_REPLACES)
#define AN_ALLOCATION_COUNTER_REPLACES 1
#endif

namespace
{

// Relaxed ordering is enough: nothing is published through this counter, and the only readers are
// the test that made the allocations.
std::atomic<std::uint64_t> g_allocations{0u};

#if AN_ALLOCATION_COUNTER_REPLACES
void* allocateOrNull(std::size_t size) noexcept
{
    // A zero-byte request still has to return a distinct pointer, and malloc is permitted to return
    // null for one, which would be reported as an allocation failure that did not happen.
    void* memory = std::malloc(size == 0u ? 1u : size);
    if (memory != nullptr)
    {
        g_allocations.fetch_add(1u, std::memory_order_relaxed);
    }
    return memory;
}

void* allocate(std::size_t size)
{
    void* memory = allocateOrNull(size);
    if (memory == nullptr)
    {
        throw std::bad_alloc();
    }
    return memory;
}
#endif

} // namespace

#if AN_ALLOCATION_COUNTER_REPLACES

void* operator new(std::size_t size)
{
    return allocate(size);
}

void* operator new[](std::size_t size)
{
    return allocate(size);
}

void operator delete(void* memory) noexcept
{
    std::free(memory);
}

void operator delete[](void* memory) noexcept
{
    std::free(memory);
}

void operator delete(void* memory, std::size_t) noexcept
{
    std::free(memory);
}

void operator delete[](void* memory, std::size_t) noexcept
{
    std::free(memory);
}

void* operator new(std::size_t size, const std::nothrow_t&) noexcept
{
    return allocateOrNull(size);
}

void* operator new[](std::size_t size, const std::nothrow_t&) noexcept
{
    return allocateOrNull(size);
}

// Reached only when a constructor throws inside a nothrow new-expression, which is also the only
// way memory from the two forms above can arrive anywhere other than the plain deallocation.
void operator delete(void* memory, const std::nothrow_t&) noexcept
{
    std::free(memory);
}

void operator delete[](void* memory, const std::nothrow_t&) noexcept
{
    std::free(memory);
}
#endif

namespace testsupport
{

std::uint64_t totalAllocations() noexcept
{
    return g_allocations.load(std::memory_order_relaxed);
}

} // namespace testsupport
