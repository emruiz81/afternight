// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The quarantine and the global allocation functions it needs.
///
/// The sized, array and nothrow forms are replaced with the plain ones, so no block reaches a deallocation of another
/// implementation; the aligned forms are left to the library, whose allocation and deallocation pair with each other.
/// A block is released through `std::free` whichever module allocated it, because every module of this executable
/// allocates from the same C runtime heap. Under the thread sanitizer nothing is replaced, as the header says.

#include "Test_ControlsSeams/FreedMemoryQuarantine.h"

#include <array>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <new>

#include <malloc.h>

#if defined(__SANITIZE_ADDRESS__)
#define AN_FREED_MEMORY_POISONED_BY_SANITIZER 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
#define AN_FREED_MEMORY_POISONED_BY_SANITIZER 1
#endif
#endif
#if defined(AN_FREED_MEMORY_POISONED_BY_SANITIZER)
#include <sanitizer/asan_interface.h>
#endif

namespace
{

constexpr std::size_t kQuarantineCapacity = std::size_t{1} << 16;

/// The quarantine's state; a spin lock, because a lock that allocates cannot guard a deallocation.
struct Quarantine
{
    std::atomic_flag lock;
    bool active = false;
    std::size_t count = 0u;
    std::array<void*, kQuarantineCapacity> blocks{};
};

Quarantine g_Quarantine;

class QuarantineLock
{
public:
    QuarantineLock() noexcept
    {
        while (g_Quarantine.lock.test_and_set(std::memory_order_acquire))
        {
        }
    }
    ~QuarantineLock()
    {
        g_Quarantine.lock.clear(std::memory_order_release);
    }
    QuarantineLock(const QuarantineLock&) = delete;
    QuarantineLock& operator=(const QuarantineLock&) = delete;
};

#if AN_FREED_MEMORY_QUARANTINE_HOLDS
[[nodiscard]] std::size_t usableSize(void* memory) noexcept
{
#if defined(_WIN32)
    return _msize(memory);
#else
    return malloc_usable_size(memory);
#endif
}

void* allocateOrNull(std::size_t size) noexcept
{
    // A zero-byte request still returns a distinct pointer.
    return std::malloc(size == 0u ? 1u : size);
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

void release(void* memory) noexcept
{
    if (memory == nullptr)
    {
        return;
    }
    {
        QuarantineLock lock;
        if (g_Quarantine.active && g_Quarantine.count < kQuarantineCapacity)
        {
#if defined(AN_FREED_MEMORY_POISONED_BY_SANITIZER)
            ASAN_POISON_MEMORY_REGION(memory, usableSize(memory));
#else
            std::memset(memory, an::tests::viewer::kFreedMemoryPoison, usableSize(memory));
#endif
            g_Quarantine.blocks[g_Quarantine.count++] = memory;
            return;
        }
    }
    std::free(memory);
}
#endif

} // namespace

namespace an::tests::viewer
{

FreedMemoryQuarantine::FreedMemoryQuarantine()
{
    QuarantineLock lock;
    g_Quarantine.active = true;
    g_Quarantine.count = 0u;
}

FreedMemoryQuarantine::~FreedMemoryQuarantine()
{
    std::size_t count = 0u;
    {
        QuarantineLock lock;
        g_Quarantine.active = false;
        count = g_Quarantine.count;
        g_Quarantine.count = 0u;
    }
    for (std::size_t index = 0u; index < count; ++index)
    {
#if defined(AN_FREED_MEMORY_POISONED_BY_SANITIZER)
        ASAN_UNPOISON_MEMORY_REGION(g_Quarantine.blocks[index], usableSize(g_Quarantine.blocks[index]));
#endif
        std::free(g_Quarantine.blocks[index]);
        g_Quarantine.blocks[index] = nullptr;
    }
}

std::size_t FreedMemoryQuarantine::held() const noexcept
{
    QuarantineLock lock;
    return g_Quarantine.count;
}

} // namespace an::tests::viewer

#if AN_FREED_MEMORY_QUARANTINE_HOLDS
void* operator new(std::size_t size)
{
    return allocate(size);
}

void* operator new[](std::size_t size)
{
    return allocate(size);
}

void* operator new(std::size_t size, const std::nothrow_t&) noexcept
{
    return allocateOrNull(size);
}

void* operator new[](std::size_t size, const std::nothrow_t&) noexcept
{
    return allocateOrNull(size);
}

void operator delete(void* memory) noexcept
{
    release(memory);
}

void operator delete[](void* memory) noexcept
{
    release(memory);
}

void operator delete(void* memory, std::size_t) noexcept
{
    release(memory);
}

void operator delete[](void* memory, std::size_t) noexcept
{
    release(memory);
}

void operator delete(void* memory, const std::nothrow_t&) noexcept
{
    release(memory);
}

void operator delete[](void* memory, const std::nothrow_t&) noexcept
{
    release(memory);
}
#endif
