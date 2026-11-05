// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The session's recycling resource: the engine's platform path, and the large blocks kept while a preview is open.

#include "RecyclingMemoryResource.h"

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/Status.h>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <new>
#include <system_error>
#include <utility>

namespace an::consumer::detail
{

namespace
{

/// The refusal for a request the caller cannot have meant, as the engine's own resource words it.
[[nodiscard]] an::Status refuseRequest(const char* message, const char* parameter)
{
    return an::Status::failure(
        an::ErrorCode::InvalidArgument,
        an::StatusDetail{.message = message,
                         .operation = "MemoryResource::allocate",
                         .domain = an::ErrorDomain::Memory,
                         .context = {an::StatusContextField::ofText(an::ContextKey::Parameter, parameter)}});
}

[[nodiscard]] bool isPowerOfTwo(std::uint64_t value) noexcept
{
    return value != 0u && (value & (value - 1u)) == 0u;
}

/// The platform's pointer, kept just before an aligned block.
[[nodiscard]] void* originalOf(void* block) noexcept
{
    void* original = nullptr;
    std::memcpy(&original, static_cast<unsigned char*>(block) - sizeof(void*), sizeof(original));
    return original;
}

} // namespace

an::Result<RecyclingMemoryResource*> RecyclingMemoryResource::create() noexcept
{
    auto* const made = new (std::nothrow) RecyclingMemoryResource();
    if (made == nullptr)
    {
        return an::Status::failure(an::ErrorCode::AllocationFailed);
    }
    try
    {
        // Every list operation after this one stays within the capacity, so giving a block back never allocates.
        made->m_kept.reserve(kMaxKeptBlocks);
    }
    catch (const std::bad_alloc&)
    {
        delete made;
        return an::Status::failure(an::ErrorCode::AllocationFailed);
    }
    return made;
}

RecyclingMemoryResource::~RecyclingMemoryResource()
{
    {
        const std::lock_guard<std::mutex> held(m_releaseGuard);
        m_releaseClosing = true;
    }
    m_releaseWake.notify_all();
    if (m_releaser.joinable())
    {
        // The thread returns what is still queued before it ends.
        m_releaser.join();
    }
}

an::Result<void*> RecyclingMemoryResource::allocate(std::uint64_t bytes, std::uint64_t alignment)
{
    if (void* const kept = takeKept(bytes, alignment); kept != nullptr)
    {
        return kept;
    }
    return allocateFromPlatform(bytes, alignment, false);
}

an::Result<void*> RecyclingMemoryResource::allocateZeroed(std::uint64_t bytes, std::uint64_t alignment)
{
    // Clearing resident pages costs less than the faults that hand new ones over already cleared.
    if (void* const kept = takeKept(bytes, alignment); kept != nullptr)
    {
        std::memset(kept, 0, static_cast<std::size_t>(bytes));
        return kept;
    }
    return allocateFromPlatform(bytes, alignment, true);
}

void* RecyclingMemoryResource::takeKept(std::uint64_t bytes, std::uint64_t alignment) noexcept
{
    if (bytes < kRecycledBlockBytes)
    {
        return nullptr;
    }
    const std::lock_guard<std::mutex> held(m_guard);
    // The block kept last is the likeliest to still be in a cache.
    for (std::size_t index = m_kept.size(); index > 0u; --index)
    {
        const KeptBlock kept = m_kept[index - 1u];
        if (kept.bytes == bytes && kept.alignment == alignment)
        {
            m_kept.erase(m_kept.begin() + static_cast<std::ptrdiff_t>(index - 1u));
            m_keptBytes -= bytes;
            ++m_reused;
            m_liveBytes += bytes;
            ++m_liveAllocations;
            m_totalBytes += bytes;
            ++m_totalAllocations;
            m_peakBytes = (std::max)(m_peakBytes, m_liveBytes);
            return kept.block;
        }
    }
    return nullptr;
}

an::Result<void*> RecyclingMemoryResource::allocateFromPlatform(std::uint64_t bytes, std::uint64_t alignment, bool zero)
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
    // A power-of-two alignment is at most 2^63, so the envelope's overhead cannot overflow.
    const std::uint64_t overhead = alignment - 1u + sizeof(void*);
    const std::uint64_t largest = (std::numeric_limits<std::size_t>::max)();
    void* original = nullptr;
    std::size_t total = 0u;
    if (overhead <= largest && bytes <= largest - overhead)
    {
        total = static_cast<std::size_t>(bytes + overhead);
        original = zero ? std::calloc(1u, total) : std::malloc(total);
        if (original == nullptr)
        {
            // Kept blocks are memory nothing uses: they go back before a request is refused.
            std::array<KeptBlock, kMaxKeptBlocks> returned{};
            std::size_t count = 0u;
            {
                const std::lock_guard<std::mutex> held(m_guard);
                count = m_kept.size();
                std::copy(m_kept.cbegin(), m_kept.cend(), returned.begin());
                m_kept.clear();
                m_keptBytes = 0u;
            }
            for (std::size_t index = 0u; index < count; ++index)
            {
                std::free(originalOf(returned[index].block));
            }
            if (count > 0u)
            {
                original = zero ? std::calloc(1u, total) : std::malloc(total);
            }
        }
    }
    if (original == nullptr)
    {
        const std::lock_guard<std::mutex> held(m_guard);
        ++m_deniedAllocations;
        return an::Status::failure(an::ErrorCode::AllocationFailed);
    }
    const auto address = reinterpret_cast<std::uintptr_t>(original) + sizeof(void*);
    const auto aligned = (address + alignment - 1u) & ~static_cast<std::uintptr_t>(alignment - 1u);
    void* const block = reinterpret_cast<void*>(aligned);
    std::memcpy(static_cast<unsigned char*>(block) - sizeof(void*), &original, sizeof(original));
    const std::lock_guard<std::mutex> held(m_guard);
    m_liveBytes += bytes;
    ++m_liveAllocations;
    m_totalBytes += bytes;
    ++m_totalAllocations;
    m_peakBytes = (std::max)(m_peakBytes, m_liveBytes);
    return block;
}

void RecyclingMemoryResource::deallocate(void* pointer, std::uint64_t bytes, std::uint64_t alignment) noexcept
{
    if (pointer == nullptr)
    {
        return;
    }
    // The blocks kept longest make room for this one: the sizes the open dialogs ask for now are the ones worth
    // keeping. They are taken out of the kept list here and returned once the lock is let go.
    std::array<KeptBlock, kMaxKeptBlocks> pushedOut{};
    std::size_t pushed = 0u;
    bool kept = false;
    {
        const std::lock_guard<std::mutex> held(m_guard);
        ++m_callsInside;
        m_liveBytes -= (std::min)(bytes, m_liveBytes);
        m_liveAllocations -= m_liveAllocations > 0u ? 1u : 0u;
        const auto sameSize = std::count_if(m_kept.cbegin(), m_kept.cend(), [&](const KeptBlock& block) {
            return block.bytes == bytes && block.alignment == alignment;
        });
        const std::uint64_t sizeRoom =
            bytes > 0u ? (std::max)(std::uint64_t{kRecycledBlocksPerSize}, kRecycledBytesPerSize / bytes) : 0u;
        if (!m_detached && bytes >= kRecycledBlockBytes && bytes <= m_boundBytes &&
            static_cast<std::uint64_t>(sameSize) < sizeRoom)
        {
            while (pushed < m_kept.size() &&
                   (m_kept.size() - pushed + 1u > kMaxKeptBlocks || m_keptBytes + bytes > m_boundBytes))
            {
                pushedOut[pushed] = m_kept[pushed];
                m_keptBytes -= m_kept[pushed].bytes;
                ++pushed;
            }
            m_kept.erase(m_kept.begin(), m_kept.begin() + static_cast<std::ptrdiff_t>(pushed));
            // The list never holds more than its capacity, reserved when the resource was made, so this never
            // allocates.
            m_kept.push_back(KeptBlock{.block = pointer, .bytes = bytes, .alignment = alignment});
            m_keptBytes += bytes;
            kept = true;
        }
    }
    for (std::size_t index = 0u; index < pushed; ++index)
    {
        releaseToPlatform(pushedOut[index]);
    }
    if (!kept)
    {
        releaseToPlatform(KeptBlock{.block = pointer, .bytes = bytes, .alignment = alignment});
    }
    finishCall();
}

void RecyclingMemoryResource::finishCall() noexcept
{
    bool last = false;
    {
        const std::lock_guard<std::mutex> held(m_guard);
        --m_callsInside;
        last = m_detached && m_liveAllocations == 0u && m_callsInside == 0u;
    }
    // Nothing holds a block any more and nothing else is inside: nobody can call this resource again.
    if (last)
    {
        delete this;
    }
}

void RecyclingMemoryResource::releaseToPlatform(const KeptBlock& block, const bool keptBlockLetGo) noexcept
{
    void* const original = originalOf(block.block);
    bool deferred = false;
    if (block.bytes >= kDeferredReleaseBlockBytes)
    {
        {
            const std::lock_guard<std::mutex> held(m_guard);
            deferred = m_deferReleases || keptBlockLetGo;
        }
        if (deferred)
        {
            deferred = false;
            const std::lock_guard<std::mutex> held(m_releaseGuard);
            if (!m_releaseClosing && block.bytes <= kDeferredReleaseLimitBytes - m_releasedBytes)
            {
                try
                {
                    if (!m_releaser.joinable())
                    {
                        m_releaser = std::thread([this]() { releaseDeferred(); });
                    }
                    m_released.push_back(ReleasedBlock{.original = original, .bytes = block.bytes});
                    m_releasedBytes += block.bytes;
                    deferred = true;
                }
                catch (const std::system_error&)
                {
                }
                catch (const std::bad_alloc&)
                {
                }
            }
        }
    }
    if (deferred)
    {
        {
            const std::lock_guard<std::mutex> held(m_guard);
            ++m_releasedLater;
        }
        m_releaseWake.notify_one();
    }
    else
    {
        std::free(original);
    }
}

void RecyclingMemoryResource::releaseDeferred() noexcept
{
    std::unique_lock<std::mutex> held(m_releaseGuard);
    for (;;)
    {
        m_releaseWake.wait(held, [this]() { return !m_released.empty() || m_releaseClosing; });
        if (m_released.empty())
        {
            return;
        }
        const ReleasedBlock block = m_released.front();
        m_released.pop_front();
        held.unlock();
        std::free(block.original);
        held.lock();
        m_releasedBytes -= block.bytes;
    }
}

an::MemoryCounters RecyclingMemoryResource::counters() const noexcept
{
    const std::lock_guard<std::mutex> held(m_guard);
    return an::MemoryCounters{.liveBytes = m_liveBytes,
                              .liveAllocations = m_liveAllocations,
                              .peakBytes = m_peakBytes,
                              .totalBytes = m_totalBytes,
                              .totalAllocations = m_totalAllocations,
                              .deniedAllocations = m_deniedAllocations};
}

void RecyclingMemoryResource::keepBlocks(std::uint64_t boundBytes) noexcept
{
    std::array<KeptBlock, kMaxKeptBlocks> pushedOut{};
    std::size_t pushed = 0u;
    bool detached = false;
    {
        const std::lock_guard<std::mutex> held(m_guard);
        ++m_callsInside;
        detached = m_detached;
        m_boundBytes = m_detached ? 0u : boundBytes;
        while (pushed < m_kept.size() && m_keptBytes > m_boundBytes)
        {
            pushedOut[pushed] = m_kept[pushed];
            m_keptBytes -= m_kept[pushed].bytes;
            ++pushed;
        }
        m_kept.erase(m_kept.begin(), m_kept.begin() + static_cast<std::ptrdiff_t>(pushed));
    }
    // The blocks a closing preview dialog let go of go back on the resource's own thread, never on the dialog's: up to
    // the most a resource keeps, each a large free the caller - the GUI thread - would otherwise wait for. A detached
    // resource's go back where it is, since whatever gives back its last block waits for that thread anyway.
    for (std::size_t index = 0u; index < pushed; ++index)
    {
        releaseToPlatform(pushedOut[index], !detached);
    }
    finishCall();
}

void RecyclingMemoryResource::deferReleases(bool defer) noexcept
{
    const std::lock_guard<std::mutex> held(m_guard);
    m_deferReleases = defer;
}

RecycledBlocks RecyclingMemoryResource::recycled() const noexcept
{
    const std::lock_guard<std::mutex> held(m_guard);
    return RecycledBlocks{.keptBlocks = m_kept.size(),
                          .keptBytes = m_keptBytes,
                          .reusedBlocks = m_reused,
                          .releasedLater = m_releasedLater};
}

void RecyclingMemoryResource::detach() noexcept
{
    {
        const std::lock_guard<std::mutex> held(m_guard);
        m_detached = true;
    }
    // The session is closing: what the releasing thread still holds goes back now, on the thread that closes it, so the
    // thread that gives back the session's last image later waits for none of it. Every block released from now on
    // goes back where it is released.
    {
        const std::lock_guard<std::mutex> held(m_releaseGuard);
        m_releaseClosing = true;
    }
    m_releaseWake.notify_all();
    if (m_releaser.joinable())
    {
        m_releaser.join();
    }
    // Every kept block goes back, and with it, once nothing is outstanding, the resource: nothing here touches it
    // after.
    keepBlocks(0u);
}

} // namespace an::consumer::detail
