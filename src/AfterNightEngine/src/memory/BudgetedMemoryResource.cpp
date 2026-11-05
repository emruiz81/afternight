// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "memory/BudgetedMemoryResource.h"

#include <AfterNightEngine/Core/Alignment.h>
#include <AfterNightEngine/Core/EngineLimits.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Runtime/MemoryResource.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <mutex>
#include <system_error>
#include <thread>
#include <utility>

// Three things in this file are worth stating beside the code rather than only in the header.
//
// **The budget is claimed before the platform is asked, and given back when the platform refuses.**
// The other order - allocate, then check - would either exceed the budget for the duration of the
// check or need the allocation undone anyway, and the undo is the part that has to be right. Here
// the only path that has to unwind is the one where the platform said no, and it is one subtraction.
//
// **A supplied upstream replaces the platform and nothing else.** When an engine context was built
// over a caller's resource, the budget, the alignment contract and the counters stay here and only
// the source of the bytes moves. The choice is fixed at construction, so this resource never mixes
// enveloped platform blocks with a supplied resource's blocks, and its refusal is returned to the
// caller unchanged.
//
// **Zeroing belongs to the platform allocator when it can provide already-zero pages.** `calloc`
// avoids writing a whole frame merely to establish a zero value the platform already guarantees.
// Both paths take the same checked alignment envelope - the request plus `alignment - 1` plus one
// pointer - and keep the pointer `malloc` or `calloc` returned immediately before the aligned
// payload, so `deallocate` finds it without a side table. Only the requested payload bytes are
// charged against the budget; the envelope is not a second allocation of the caller's. There is no
// pool and no global state.

namespace an
{
namespace
{

#if defined(__SANITIZE_ADDRESS__)
/// Whether this build runs under the address sanitizer, which has to see a block freed when it is given back.
constexpr bool kAddressSanitized = true;
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
constexpr bool kAddressSanitized = true;
#else
constexpr bool kAddressSanitized = false;
#endif
#else
constexpr bool kAddressSanitized = false;
#endif

/// The refusal for a request the caller cannot have meant.
///
/// @param message what is wrong, for a person.
/// @param parameter which argument, spelled as the caller wrote it.
/// @return an `ErrorCode::InvalidArgument` failure naming the argument.
[[nodiscard]] Status refuseRequest(const char* message, const char* parameter)
{
    return Status::failure(ErrorCode::InvalidArgument,
                           StatusDetail{.message = message,
                                        .operation = "MemoryResource::allocate",
                                        .domain = ErrorDomain::Memory,
                                        .context = {StatusContextField::ofText(ContextKey::Parameter, parameter)}});
}

} // namespace

BudgetedMemoryResource::~BudgetedMemoryResource()
{
    {
        const std::lock_guard<std::mutex> held(m_releaseGuard);
        m_releaseClosing = true;
    }
    m_releaseWake.notify_all();
    if (m_releaser.joinable())
    {
        m_releaser.join();
    }
}

Result<void*> BudgetedMemoryResource::allocate(std::uint64_t bytes, std::uint64_t alignment)
{
    return allocateInitialized(bytes, alignment, false);
}

Result<void*> BudgetedMemoryResource::allocateZeroed(std::uint64_t bytes, std::uint64_t alignment)
{
    return allocateInitialized(bytes, alignment, true);
}

Result<void*> BudgetedMemoryResource::allocateInitialized(std::uint64_t bytes, std::uint64_t alignment, bool zero)
{
    if (bytes == 0u)
    {
        const std::lock_guard<std::mutex> held(m_guard);
        ++m_deniedAllocations;
        return refuseRequest("an allocation of no bytes is a mistake at the call site rather than a cheap request",
                             "bytes");
    }
    if (!isPowerOfTwo(alignment))
    {
        const std::lock_guard<std::mutex> held(m_guard);
        ++m_deniedAllocations;
        return refuseRequest("an alignment that is not a power of two is not an alignment", "alignment");
    }

    std::uint64_t available = 0u;
    bool admitted = false;
    {
        const std::lock_guard<std::mutex> held(m_guard);
        const std::uint64_t committed = m_liveBytes + m_reservedBytes;
        if (!fitsMemoryBudget(m_limits, committed, bytes))
        {
            ++m_deniedAllocations;
            available = m_limits.memoryBudgetBytes > committed ? m_limits.memoryBudgetBytes - committed : 0u;
        }
        else
        {
            m_reservedBytes += bytes;
            admitted = true;
        }
    }
    if (!admitted)
    {
        return Status::failure(
            ErrorCode::MemoryBudgetExceeded,
            StatusDetail{.message = "the request does not fit the memory budget this engine is configured with",
                         .operation = "MemoryResource::allocate",
                         .domain = ErrorDomain::Memory,
                         .context = {StatusContextField::ofCount(ContextKey::Requested, bytes),
                                     StatusContextField::ofCount(ContextKey::Available, available),
                                     StatusContextField::ofCount(ContextKey::Limit, m_limits.memoryBudgetBytes)}});
    }

    if (m_upstream != nullptr)
    {
        // A supplied resource honours the alignment itself, so there is no envelope and no retained
        // original pointer: every block of this resource then comes from there and goes back there.
        // Its refusal is returned as it was given rather than restated, because a caller that stated
        // a reason for refusing is the one that has to recognise the answer.
        Result<void*> supplied =
            zero ? m_upstream->allocateZeroed(bytes, alignment) : m_upstream->allocate(bytes, alignment);
        if (!supplied.hasValue())
        {
            const std::lock_guard<std::mutex> held(m_guard);
            m_reservedBytes -= bytes;
            ++m_deniedAllocations;
            return supplied.status();
        }
        {
            const std::lock_guard<std::mutex> held(m_guard);
            m_reservedBytes -= bytes;
            m_liveBytes += bytes;
            ++m_liveAllocations;
            m_totalBytes += bytes;
            ++m_totalAllocations;
            if (m_liveBytes > m_peakBytes)
            {
                m_peakBytes = m_liveBytes;
            }
        }
        return *supplied;
    }

    // Power-of-two alignment is at most 2^63, so this overhead sum cannot overflow uint64_t.
    const auto overhead = alignment - 1u + sizeof(void*);
    const auto maximum = (std::numeric_limits<std::size_t>::max)();
    void* block = nullptr;
    if (overhead <= maximum && bytes <= maximum - overhead)
    {
        const auto total = static_cast<std::size_t>(bytes + overhead);
        void* const original = zero ? std::calloc(1u, total) : std::malloc(total);
        if (original != nullptr)
        {
            const auto address = reinterpret_cast<std::uintptr_t>(original) + sizeof(void*);
            const auto aligned = (address + alignment - 1u) & ~(alignment - 1u);
            block = reinterpret_cast<void*>(static_cast<std::uintptr_t>(aligned));
            std::memcpy(static_cast<unsigned char*>(block) - sizeof(void*), &original, sizeof(original));
        }
    }
    if (block == nullptr)
    {
        const std::lock_guard<std::mutex> held(m_guard);
        m_reservedBytes -= bytes;
        ++m_deniedAllocations;
        return Status::failure(ErrorCode::AllocationFailed);
    }

    {
        const std::lock_guard<std::mutex> held(m_guard);
        m_reservedBytes -= bytes;
        m_liveBytes += bytes;
        ++m_liveAllocations;
        m_totalBytes += bytes;
        ++m_totalAllocations;
        if (m_liveBytes > m_peakBytes)
        {
            m_peakBytes = m_liveBytes;
        }
    }
    return block;
}

void BudgetedMemoryResource::deallocate(void* pointer, std::uint64_t bytes, std::uint64_t alignment) noexcept
{
    if (pointer == nullptr)
    {
        return;
    }

    if (m_upstream != nullptr)
    {
        m_upstream->deallocate(pointer, bytes, alignment);
    }
    else
    {
        void* original = nullptr;
        std::memcpy(&original, static_cast<unsigned char*>(pointer) - sizeof(void*), sizeof(original));
        const bool deferred = !kAddressSanitized && bytes >= kDeferredReleaseBytes &&
                              m_limits.memoryBudgetBytes == kDefaultMemoryBudgetBytes && deferRelease(original, bytes);
        if (!deferred)
        {
            std::free(original);
        }
    }
    const std::lock_guard<std::mutex> held(m_guard);
    m_liveBytes -= bytes;
    --m_liveAllocations;
}

bool BudgetedMemoryResource::deferRelease(void* original, std::uint64_t bytes) noexcept
{
    {
        const std::lock_guard<std::mutex> held(m_releaseGuard);
        if (m_releaseClosing ||
            bytes > kDeferredReleaseLimitBytes - (std::min)(m_deferredBytes, kDeferredReleaseLimitBytes))
        {
            return false;
        }
        try
        {
            if (!m_releaser.joinable())
            {
                m_releaser = std::thread([this]() { releaseDeferred(); });
            }
            m_deferred.push_back(DeferredBlock{.original = original, .bytes = bytes});
        }
        catch (const std::system_error&)
        {
            return false;
        }
        catch (const std::bad_alloc&)
        {
            return false;
        }
        m_deferredBytes += bytes;
    }
    m_releaseWake.notify_one();
    return true;
}

void BudgetedMemoryResource::releaseDeferred() noexcept
{
    std::unique_lock<std::mutex> held(m_releaseGuard);
    for (;;)
    {
        m_releaseWake.wait(held, [this]() { return !m_deferred.empty() || m_releaseClosing; });
        if (m_deferred.empty())
        {
            return;
        }
        const DeferredBlock block = m_deferred.front();
        m_deferred.pop_front();
        held.unlock();
        std::free(block.original);
        held.lock();
        m_deferredBytes -= block.bytes;
    }
}

MemoryCounters BudgetedMemoryResource::counters() const noexcept
{
    const std::lock_guard<std::mutex> held(m_guard);
    return MemoryCounters{.liveBytes = m_liveBytes,
                          .liveAllocations = m_liveAllocations,
                          .peakBytes = m_peakBytes,
                          .totalBytes = m_totalBytes,
                          .totalAllocations = m_totalAllocations,
                          .deniedAllocations = m_deniedAllocations};
}

} // namespace an
