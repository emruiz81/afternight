// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// A scoped refusal of allocations in the shared MSVC Debug CRT, including engine DLL allocations.
// Include in one translation unit per suite. Assertions run after the scope restores the heap.
#pragma once

#include <cstddef>
#include <limits>

#if defined(_MSC_VER) && defined(_DEBUG)
#include <crtdbg.h>
#endif

namespace an::test
{
class HeapFailure
{
public:
#if defined(_MSC_VER) && defined(_DEBUG)
    static constexpr bool kAvailable = true;
    explicit HeapFailure(std::size_t after) noexcept
    {
        m_remaining = after;
        m_refused = 0u;
        m_previous = _CrtSetAllocHook(&hook);
        m_active = true;
    }
    ~HeapFailure()
    {
        m_active = false;
        _CrtSetAllocHook(m_previous);
    }
    [[nodiscard]] std::size_t refused() const noexcept
    {
        return m_refused;
    }

private:
    static int hook(int kind, void*, std::size_t size, int block, long, const unsigned char*, int) noexcept
    {
        // MSVC's checked-iterator proxy is allocated even inside noexcept STL constructors.
        // Keep that Debug-only bookkeeping available; refuse the subject's storage allocations.
        if (!m_active || kind == _HOOK_FREE || block == _CRT_BLOCK || size <= 2u * sizeof(void*))
            return 1;
        if (m_remaining != 0u)
        {
            --m_remaining;
            return 1;
        }
        ++m_refused;
        return 0;
    }
    inline static thread_local bool m_active = false;
    inline static thread_local std::size_t m_remaining = 0u;
    inline static thread_local std::size_t m_refused = 0u;
    _CRT_ALLOC_HOOK m_previous = nullptr;
#else
    static constexpr bool kAvailable = false;
    explicit HeapFailure(std::size_t) noexcept {}
    [[nodiscard]] std::size_t refused() const noexcept
    {
        return 0u;
    }
#endif
    HeapFailure(const HeapFailure&) = delete;
    HeapFailure& operator=(const HeapFailure&) = delete;
};
} // namespace an::test
