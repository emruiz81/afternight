// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#if defined(_MSC_VER) && defined(_DEBUG)
#include <crtdbg.h>
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace codec_test
{
/// A thread-local fault in the shared Debug CRT, which also observes allocations inside the DLL.
/// MSVC's iterator proxies allocate inside noexcept STL moves; faulting those terminates in the
/// runtime. This campaign excludes that bookkeeping and targets payloads and owning report data.
class AllocationFault
{
public:
    explicit AllocationFault(std::size_t ordinal, const wchar_t* publishedPending = nullptr) noexcept
        : m_ordinal(ordinal), m_pending(publishedPending), m_previous(_CrtSetAllocHook(hook))
    {
        m_active = this;
    }
    ~AllocationFault()
    {
        m_active = nullptr;
        _CrtSetAllocHook(m_previous);
    }
    AllocationFault(const AllocationFault&) = delete;
    AllocationFault& operator=(const AllocationFault&) = delete;
    std::size_t allocations = 0u;
    bool refused = false;

private:
    static int __cdecl hook(int type, void*, std::size_t size, int block, long, const unsigned char*, int)
    {
        auto* self = m_active;
        if (self == nullptr || type != _HOOK_ALLOC || block == _CRT_BLOCK || size == sizeof(std::_Container_proxy))
            return TRUE;
        if (self->m_pending != nullptr && GetFileAttributesW(self->m_pending) != INVALID_FILE_ATTRIBUTES)
            return TRUE;
        ++self->allocations;
        if (self->allocations == self->m_ordinal)
        {
            self->refused = true;
            return FALSE;
        }
        return TRUE;
    }
    inline static thread_local AllocationFault* m_active = nullptr;
    std::size_t m_ordinal;
    const wchar_t* m_pending;
    _CRT_ALLOC_HOOK m_previous;
};

/// The most memory the shared Debug CRT holds at once for this thread while it is alive, each allocation counted from
/// its making to its release, those inside the DLLs included: what a call made on this thread between its
/// construction and its destruction held at its peak, besides what was held before. A caller holds the count to an
/// allocation it knows the call makes, so that work the call handed to another thread cannot pass unseen.
class PeakAllocation
{
public:
    PeakAllocation() noexcept : m_previous(_CrtSetAllocHook(hook))
    {
        m_active = this;
    }
    ~PeakAllocation()
    {
        m_active = nullptr;
        _CrtSetAllocHook(m_previous);
    }
    PeakAllocation(const PeakAllocation&) = delete;
    PeakAllocation& operator=(const PeakAllocation&) = delete;
    std::int64_t liveBytes = 0; ///< What is held now, beyond what was held at the construction.
    std::uint64_t peakBytes = 0u; ///< The most `liveBytes` reached.

private:
    static int __cdecl hook(int type, void* data, std::size_t size, int block, long, const unsigned char*, int)
    {
        auto* self = m_active;
        if (self == nullptr || block == _CRT_BLOCK || block == _IGNORE_BLOCK)
            return TRUE;
        const auto held = [&] {
            return data != nullptr ? static_cast<std::int64_t>(_msize_dbg(data, block)) : std::int64_t{0};
        };
        if (type == _HOOK_ALLOC)
            self->liveBytes += static_cast<std::int64_t>(size);
        else if (type == _HOOK_REALLOC)
            self->liveBytes += static_cast<std::int64_t>(size) - held();
        else if (type == _HOOK_FREE)
            self->liveBytes -= held();
        if (self->liveBytes > 0 && static_cast<std::uint64_t>(self->liveBytes) > self->peakBytes)
            self->peakBytes = static_cast<std::uint64_t>(self->liveBytes);
        return TRUE;
    }
    inline static thread_local PeakAllocation* m_active = nullptr;
    _CRT_ALLOC_HOOK m_previous;
};
} // namespace codec_test
#endif
