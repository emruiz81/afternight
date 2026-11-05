// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <AfterNightEngine/Runtime/ScratchArena.h>

#include <AfterNightEngine/Core/Alignment.h>
#include <AfterNightEngine/Core/CheckedSize.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Runtime/MemoryResource.h>

#include <cstdint>
#include <limits>
#include <new>
#include <utility>
#include <vector>

// The three decisions behind a bump arena that are not obvious from what it does.
//
// **A block is never given back on its own.** `reset` and `rewind` move the bump pointer and leave
// every block where it is, so the second pass of a loop and the second tile of a stage cost no
// allocation at all. The only things that return memory are `releaseBlocks` and the destructor,
// which is what makes the release rule a property of scope rather than of a heuristic.
//
// **A request larger than the block size gets a block of its own rather than a refusal.** An arena
// that refused would push every caller into asking how big its blocks are, and an arena that grew
// its block size to fit would then hold that much for the rest of the execution. A block sized to
// the request is the answer that neither refuses nor over-reserves; it is kept for reuse like any
// other, and the next request that fits in it uses it.
//
// **A rewind that cannot be honoured rewinds to the beginning.** Passing a marker from before a
// `reset` is a defect in the caller, and the two things this code could do about it are corrupt the
// accounting or be conservative. Being conservative costs a caller a slower run; corrupting the
// accounting costs it a buffer that two things believe they own.

namespace an
{
namespace
{

/// The alignment every block is taken at.
///
/// The largest the engine asks for, so a block can serve a request at any alignment by rounding the
/// offset within it rather than by taking a new one.
constexpr std::uint64_t kBlockAlignmentBytes = kDefaultBufferAlignmentBytes;

/// The refusal for a request the caller cannot have meant.
///
/// @param message what is wrong, for a person.
/// @param parameter which argument, spelled as the caller wrote it.
/// @return an `ErrorCode::InvalidArgument` failure naming the argument.
[[nodiscard]] Status refuseRequest(const char* message, const char* parameter)
{
    return Status::failure(ErrorCode::InvalidArgument,
                           StatusDetail{.message = message,
                                        .operation = "ScratchArena::allocate",
                                        .domain = ErrorDomain::Memory,
                                        .context = {StatusContextField::ofText(ContextKey::Parameter, parameter)}});
}

/// Folds one marker member into its integrity tag.
///
/// This is accidental-corruption detection, not authentication: markers are public value records,
/// and the goal is to reject a stale, cross-arena or changed record instead of trusting it as a
/// bump-pointer position.
///
/// @param hash the tag accumulated so far.
/// @param value the next member.
/// @return the combined tag.
[[nodiscard]] constexpr std::uint64_t foldMarkerValue(std::uint64_t hash, std::uint64_t value) noexcept
{
    constexpr std::uint64_t kMix = 0x9E3779B97F4A7C15ull;
    hash ^= value + kMix + (hash << 6u) + (hash >> 2u);
    return hash;
}

/// Computes a marker's integrity tag from its provenance, position and accounting.
///
/// @param marker the marker members to cover; its existing validation member is ignored.
/// @param key a value unique to the arena that issued it.
/// @return the tag stored in `ScratchMarker::validation`.
[[nodiscard]] constexpr std::uint64_t markerValidation(const ScratchMarker& marker, std::uint64_t key) noexcept
{
    std::uint64_t hash = foldMarkerValue(0xCBF29CE484222325ull, key);
    hash = foldMarkerValue(hash, marker.contextId);
    hash = foldMarkerValue(hash, marker.arenaId);
    hash = foldMarkerValue(hash, marker.epoch);
    hash = foldMarkerValue(hash, marker.block);
    hash = foldMarkerValue(hash, marker.offset);
    hash = foldMarkerValue(hash, marker.liveBytes);
    return foldMarkerValue(hash, marker.liveAllocations);
}

} // namespace

/// One block of an arena, and everything the arena knows.
///
/// Defined here rather than in the header so that the published surface says nothing about how an
/// arena is laid out: a caller sees a handle, and the vector of blocks, the bump pointer and the
/// counters are this file's business.
class ScratchArena::State
{
public:
    /// Builds the state of an arena whose block size has already been validated.
    ///
    /// @param contextHold the type-erased hold that keeps the owning context open.
    /// @param resource the owning context's memory resource.
    /// @param contextId the owning context's identity.
    /// @param blockBytes how much to take at a time, already rounded and non-zero.
    /// @param arenaId this arena's identity within the context.
    State(std::shared_ptr<void> contextHold, MemoryResource& resource, std::uint64_t contextId,
          std::uint64_t blockBytes, std::uint64_t arenaId) noexcept
        : m_contextHold(std::move(contextHold)), m_resource(&resource), m_blockBytes(blockBytes),
          m_contextId(contextId), m_arenaId(arenaId), m_markerKey(foldMarkerValue(m_contextId, m_arenaId))
    {
    }

    /// Gives every block back.
    ~State()
    {
        releaseBlocks();
    }

    /// Neither copied nor moved: the handle moves, and this stays where it is.
    State(const State&) = delete;

    /// Neither copied nor moved. See the copy constructor.
    State& operator=(const State&) = delete;

    /// Neither copied nor moved. See the copy constructor.
    State(State&&) = delete;

    /// Neither copied nor moved. See the copy constructor.
    State& operator=(State&&) = delete;

    /// Takes scratch, taking a new block when the current one cannot serve it.
    ///
    /// @param bytes how many bytes are needed, already checked to be non-zero.
    /// @param alignment the boundary, already checked to be a power of two.
    /// @return the address, or whatever the resource reported.
    [[nodiscard]] Result<void*> allocate(std::uint64_t bytes, std::uint64_t alignment)
    {
        void* const fromCurrent = tryCurrentBlock(bytes, alignment);
        if (fromCurrent != nullptr)
        {
            return fromCurrent;
        }

        // Blocks past the current one are kept from an earlier pass. One of them may already be
        // large enough, which is what makes a rewound arena free on its second pass even when the
        // pass needs several blocks.
        while (m_currentBlock + 1u < m_blocks.size())
        {
            ++m_currentBlock;
            m_offset = 0u;
            void* const reused = tryCurrentBlock(bytes, alignment);
            if (reused != nullptr)
            {
                return reused;
            }
        }

        const Status taken = takeBlock(bytes, alignment);
        if (taken.failed())
        {
            ++m_deniedAllocations;
            return taken;
        }

        void* const fresh = tryCurrentBlock(bytes, alignment);
        if (fresh == nullptr)
        {
            // A block sized for this request that cannot serve it would mean the size arithmetic
            // above is wrong, which is a defect in this file rather than anything a caller did.
            ++m_deniedAllocations;
            return Status::failure(ErrorCode::Internal,
                                   StatusDetail{.message = "a scratch block sized for a request could not serve it",
                                                .operation = "ScratchArena::allocate",
                                                .domain = ErrorDomain::Memory});
        }
        return fresh;
    }

    /// Where the arena has got to.
    ///
    /// @return the current position.
    [[nodiscard]] ScratchMarker mark() const noexcept
    {
        ScratchMarker marker{.contextId = m_contextId,
                             .arenaId = m_arenaId,
                             .epoch = m_epoch,
                             .block = m_currentBlock,
                             .offset = m_offset,
                             .liveBytes = m_liveBytes,
                             .liveAllocations = m_liveAllocations};
        marker.validation = markerValidation(marker, m_markerKey);
        return marker;
    }

    /// Brings the arena back to a marker, keeping every block.
    ///
    /// @param marker a position this arena produced. One past where it now is, or from a state it
    ///        has been reset out of, brings it back to the beginning instead - the conservative
    ///        answer, because the other one hands the same bytes to two owners.
    void rewind(ScratchMarker marker) noexcept
    {
        const bool wrongOwner = marker.contextId != m_contextId || marker.arenaId != m_arenaId;
        const bool stale = marker.epoch != m_epoch;
        const bool changed = marker.validation != markerValidation(marker, m_markerKey);
        const bool ahead = marker.block > m_currentBlock ||
                           (marker.block == m_currentBlock && marker.offset > m_offset) ||
                           marker.liveBytes > m_liveBytes || marker.liveAllocations > m_liveAllocations;
        if (wrongOwner || stale || changed || ahead)
        {
            reset();
            return;
        }

        m_currentBlock = marker.block;
        m_offset = marker.offset;
        m_liveBytes = marker.liveBytes;
        m_liveAllocations = marker.liveAllocations;
    }

    /// Makes everything available again, keeping every block.
    void reset() noexcept
    {
        m_currentBlock = 0u;
        m_offset = 0u;
        m_liveBytes = 0u;
        m_liveAllocations = 0u;
        if (m_epoch != (std::numeric_limits<std::uint64_t>::max)())
        {
            ++m_epoch;
        }
    }

    /// Gives every block back to the resource.
    void releaseBlocks() noexcept
    {
        for (const Block& block : m_blocks)
        {
            m_resource->deallocate(block.address, block.bytes, kBlockAlignmentBytes);
        }
        m_blocks.clear();
        m_reservedBytes = 0u;
        reset();
    }

    /// How many bytes this arena holds from its resource.
    ///
    /// @return the reserved bytes.
    [[nodiscard]] std::uint64_t reservedBytes() const noexcept
    {
        return m_reservedBytes;
    }

    /// Which engine context owns this arena.
    ///
    /// @return the owning context identity.
    [[nodiscard]] std::uint64_t contextId() const noexcept
    {
        return m_contextId;
    }

    /// What this arena has handed out.
    ///
    /// @return the counters.
    [[nodiscard]] MemoryCounters counters() const noexcept
    {
        return MemoryCounters{.liveBytes = m_liveBytes,
                              .liveAllocations = m_liveAllocations,
                              .peakBytes = m_peakBytes,
                              .totalBytes = m_totalBytes,
                              .totalAllocations = m_totalAllocations,
                              .deniedAllocations = m_deniedAllocations};
    }

private:
    /// One block taken from the resource.
    struct Block
    {
        /// Where it starts, aligned to `kBlockAlignmentBytes`.
        void* address = nullptr;

        /// How large it is, which is what the deallocation has to be given back.
        std::uint64_t bytes = 0u;
    };

    /// Hands out from the current block if it can serve the request.
    ///
    /// @param bytes how many bytes are needed.
    /// @param alignment the boundary.
    /// @return the address, or null when the current block cannot serve it.
    [[nodiscard]] void* tryCurrentBlock(std::uint64_t bytes, std::uint64_t alignment) noexcept
    {
        if (m_currentBlock >= m_blocks.size())
        {
            return nullptr;
        }

        const Block& block = m_blocks[static_cast<std::size_t>(m_currentBlock)];

        // The *address* is rounded up, not the offset. A block starts on the widest alignment the
        // engine uses, and rounding the offset alone would satisfy any request at or below that and
        // quietly miss a larger one - which is exactly the request a kernel makes when it wants a
        // page or a wider vector than the engine's own constant describes.
        const std::uint64_t base = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(block.address));
        const CheckedSize alignedAddress = alignUp(CheckedSize{base} + CheckedSize{m_offset}, alignment);
        if (!alignedAddress.isValid())
        {
            return nullptr;
        }

        const std::uint64_t offset = alignedAddress.valueOr(0u) - base;
        const CheckedSize end = CheckedSize{offset} + CheckedSize{bytes};
        if (!end.isValid() || end.valueOr(0u) > block.bytes)
        {
            return nullptr;
        }

        m_offset = end.valueOr(0u);
        m_liveBytes += bytes;
        if (m_liveBytes > m_peakBytes)
        {
            m_peakBytes = m_liveBytes;
        }
        ++m_liveAllocations;
        ++m_totalAllocations;
        m_totalBytes += bytes;
        return static_cast<unsigned char*>(block.address) + offset;
    }

    /// Takes one more block from the resource, large enough for a stated request.
    ///
    /// @param bytes the request the block must be able to serve.
    /// @param alignment the boundary that request needs, which the block must leave room to reach.
    /// @return success, or whatever the resource reported.
    [[nodiscard]] Status takeBlock(std::uint64_t bytes, std::uint64_t alignment)
    {
        // Room for the request plus whatever rounding its alignment may cost inside the block. The
        // block itself starts at the widest alignment the engine uses, so the padding is only ever
        // needed for an alignment above that one.
        const CheckedSize needed = CheckedSize{bytes} + CheckedSize{alignment};
        if (!needed.isValid())
        {
            return Status::failure(
                ErrorCode::LimitExceeded,
                StatusDetail{.message = "a scratch request of that size is not representable",
                             .operation = "ScratchArena::allocate",
                             .domain = ErrorDomain::Memory,
                             .context = {StatusContextField::ofCount(ContextKey::Requested, bytes)}});
        }

        const std::uint64_t wanted = needed.valueOr(0u);
        const std::uint64_t blockBytes = wanted > m_blockBytes ? wanted : m_blockBytes;

        Result<void*> taken = m_resource->allocate(blockBytes, kBlockAlignmentBytes);
        if (!taken.hasValue())
        {
            return taken.status();
        }

        try
        {
            m_blocks.push_back(Block{.address = *taken, .bytes = blockBytes});
        }
        catch (const std::bad_alloc&)
        {
            m_resource->deallocate(*taken, blockBytes, kBlockAlignmentBytes);
            return Status::failure(ErrorCode::AllocationFailed);
        }

        m_reservedBytes += blockBytes;
        m_currentBlock = m_blocks.size() - 1u;
        m_offset = 0u;
        return Status::success();
    }

    /// A type-erased zero-concurrency lease that prevents the owning context from shutting down
    /// first.
    std::shared_ptr<void> m_contextHold;

    /// Where blocks come from. Kept alive by `m_contextHold`.
    MemoryResource* m_resource;

    /// How much to take at a time, unless one request needs more.
    std::uint64_t m_blockBytes;

    /// Which engine context owns this arena.
    std::uint64_t m_contextId;

    /// This arena's identity within its context.
    std::uint64_t m_arenaId;

    /// Per-arena input to marker integrity tags.
    std::uint64_t m_markerKey;

    /// Which reset/release lifetime current markers belong to. Zero is reserved for empty markers.
    std::uint64_t m_epoch = 1u;

    /// Every block this arena has taken, in the order it took them.
    std::vector<Block> m_blocks;

    /// Which block the bump pointer is in.
    std::uint64_t m_currentBlock = 0u;

    /// How far into that block it has got.
    std::uint64_t m_offset = 0u;

    /// How much this arena holds from its resource, idle or not.
    std::uint64_t m_reservedBytes = 0u;

    /// How much has been handed out since the last reset or rewind.
    std::uint64_t m_liveBytes = 0u;

    /// How many requests are outstanding since the last reset or rewind.
    std::uint64_t m_liveAllocations = 0u;

    /// The most ever outstanding at once, which survives a reset.
    std::uint64_t m_peakBytes = 0u;

    /// How many bytes have ever been handed out.
    std::uint64_t m_totalBytes = 0u;

    /// How many requests have ever been served.
    std::uint64_t m_totalAllocations = 0u;

    /// How many requests were refused.
    std::uint64_t m_deniedAllocations = 0u;
};

ScratchArena::ScratchArena() noexcept = default;

ScratchArena::~ScratchArena() = default;

ScratchArena::ScratchArena(ScratchArena&& other) noexcept = default;

ScratchArena& ScratchArena::operator=(ScratchArena&& other) noexcept = default;

ScratchArena::ScratchArena(std::unique_ptr<State> state) noexcept : m_state(std::move(state)) {}

Result<ScratchArena> ScratchArena::createBound(std::shared_ptr<void> contextHold, MemoryResource& resource,
                                               std::uint64_t contextId, std::uint64_t arenaId, std::uint64_t blockBytes)
{
    try
    {
        return ScratchArena{std::make_unique<State>(std::move(contextHold), resource, contextId, blockBytes, arenaId)};
    }
    catch (const std::bad_alloc&)
    {
        return Status::failure(ErrorCode::AllocationFailed);
    }
}

bool ScratchArena::valid() const noexcept
{
    return m_state != nullptr;
}

std::uint64_t ScratchArena::contextId() const noexcept
{
    return m_state != nullptr ? m_state->contextId() : 0u;
}

Result<void*> ScratchArena::allocate(std::uint64_t bytes, std::uint64_t alignment)
{
    if (m_state == nullptr)
    {
        return Status::failure(ErrorCode::NotInitialized,
                               StatusDetail{.message = "this arena holds no memory resource to take scratch from",
                                            .operation = "ScratchArena::allocate",
                                            .domain = ErrorDomain::Memory});
    }
    if (bytes == 0u)
    {
        return refuseRequest("scratch of no bytes is a mistake at the call site rather than a cheap request", "bytes");
    }
    if (!isPowerOfTwo(alignment))
    {
        return refuseRequest("an alignment that is not a power of two is not an alignment", "alignment");
    }

    return m_state->allocate(bytes, alignment);
}

ScratchMarker ScratchArena::mark() const noexcept
{
    return m_state != nullptr ? m_state->mark() : ScratchMarker{};
}

void ScratchArena::rewind(ScratchMarker marker) noexcept
{
    if (m_state != nullptr)
    {
        m_state->rewind(marker);
    }
}

void ScratchArena::reset() noexcept
{
    if (m_state != nullptr)
    {
        m_state->reset();
    }
}

void ScratchArena::releaseBlocks() noexcept
{
    if (m_state != nullptr)
    {
        m_state->releaseBlocks();
    }
}

std::uint64_t ScratchArena::reservedBytes() const noexcept
{
    return m_state != nullptr ? m_state->reservedBytes() : 0u;
}

MemoryCounters ScratchArena::counters() const noexcept
{
    return m_state != nullptr ? m_state->counters() : MemoryCounters{};
}

} // namespace an
