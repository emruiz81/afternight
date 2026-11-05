// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Drizzle coordinates: the scalar forward and inverse point maps, exact geometry identity, and
/// reusable preparations of one frame's geometry, owned by an engine context and optionally cached.
///
/// `mapDrizzlePoint` is the oracle. It composes the descriptor's source SIP, registration transform,
/// post-translation and scale in Float64, exactly as `DrizzleMapDescriptor` states, and every
/// generated coordinate map is tested against it. `mapDrizzlePointInverse` undoes that composition
/// and checks its answer by mapping it forward again. `DrizzleGeometryPlan` compiles a descriptor
/// once, keeping the nonzero forward SIP terms in their summation order, into state allocated from
/// the context and shared through an intrusive count, so a map generator reads prepared arithmetic
/// instead of dispatching on the models at every pixel. `DrizzleGeometryCache` keeps a bounded
/// number of those preparations for a caller that drizzles the same geometry repeatedly; it never
/// keeps a map or an image result.
///
/// The arithmetic is scalar Float64 because fitted polynomial conditioning and inverse residuals
/// decide the precision. A matrix written for integer pixel centres is conjugated by the two
/// half-pixel translations at its ingress, and its polynomial origins and reference pixel are shifted
/// with it; copying it numerically would give a different rotation or projective map.
#pragma once

#include <AfterNightEngine/Algorithms/Drizzle/DrizzleMapDescriptor.h>

#include <cstdint>

namespace an
{

class ExecutionContext;
class DrizzleGeometryAccess;
class DrizzleGeometryState;

/// Evaluates the complete forward map in scalar Float64, without quantizing to a coordinate plane.
/// Uses the descriptor's integer-centre sampling convention, including for fractional positions
/// outside the source. Thread-safe and reentrant. Float64 protects astrometric coordinates.
/// @param descriptor the checked geometry, including source SIP and post-translation.
/// @param source the finite position in source sampling pixels.
/// @return the destination position, with no canvas clipping; `ErrorCode::InvalidArgument` for a
///         non-finite source; `ErrorCode::OutOfRange` when the source SIP overflows; or
///         `ErrorCode::InvalidData` for a projective pole, an overflowing registration polynomial or
///         a scaled result that is not finite.
[[nodiscard]] AFTERNIGHTENGINE_API Result<PixelCoordinate> mapDrizzlePoint(const DrizzleMapDescriptor& descriptor,
                                                                           const PixelCoordinate& source);

/// Reverses scale, translation, registration and source SIP, in that order, in Float64.
/// Registration uses its authoritative Newton seed; SIP uses its inverse coefficients when
/// present and Newton otherwise. An approximate inverse is checked against the complete forward
/// composition: residual above 0.01 destination pixels is refused. Non-injective models may
/// select a different preimage. Thread-safe, reentrant; no coordinate-plane allocation.
/// @param descriptor the same geometry as the forward map.
/// @param destination a finite destination sampling position.
/// @return the source position; `ErrorCode::InvalidArgument` for a non-finite destination;
///         `ErrorCode::OutOfRange` when the source SIP overflows during its inverse; or
///         `ErrorCode::InvalidData` for a singular or nonconvergent registration or SIP inverse, or a
///         round trip that fails or misses the destination by more than 0.01 pixels.
[[nodiscard]] AFTERNIGHTENGINE_API Result<PixelCoordinate>
mapDrizzlePointInverse(const DrizzleMapDescriptor& descriptor, const PixelCoordinate& destination);

/// Exact geometry identity, including all owned coefficients and both extents.
/// No hash collision can alias two preparations. This deliberately excludes image generations,
/// validity and weights: only coordinates are cached, never image results. Allocation-free.
/// @param left one descriptor.
/// @param right the other descriptor.
/// @return true when every mapping fact is equal, including optional SIP presence.
[[nodiscard]] AFTERNIGHTENGINE_API bool sameDrizzleGeometry(const DrizzleMapDescriptor& left,
                                                            const DrizzleMapDescriptor& right) noexcept;

/// Shared immutable preparation of one geometry, with context-budgeted storage and a context lease.
/// Contains compiled sparse forward SIP terms and the owned descriptor. Copies share that state;
/// immutable reads through separate handles are thread-safe. No image samples or maps are retained.
class DrizzleGeometryPlan
{
public:
    /// Empty handle, including the state left by a move.
    DrizzleGeometryPlan() noexcept = default;
    /// Releases one reference; the final reference returns all bytes and the context lease.
    AFTERNIGHTENGINE_API ~DrizzleGeometryPlan();
    /// Shares immutable preparation without allocating.
    /// @param other the handle to share.
    AFTERNIGHTENGINE_API DrizzleGeometryPlan(const DrizzleGeometryPlan& other) noexcept;
    /// Shares another preparation, releasing this handle's previous reference.
    /// @param other the handle to share.
    /// @return this handle.
    AFTERNIGHTENGINE_API DrizzleGeometryPlan& operator=(const DrizzleGeometryPlan& other) noexcept;
    /// Transfers a reference, leaving the source empty.
    /// @param other the handle to move.
    AFTERNIGHTENGINE_API DrizzleGeometryPlan(DrizzleGeometryPlan&& other) noexcept;
    /// Releases the old reference and transfers another, leaving its source empty.
    /// @param other the handle to move; self-assignment is harmless.
    /// @return this handle.
    AFTERNIGHTENGINE_API DrizzleGeometryPlan& operator=(DrizzleGeometryPlan&& other) noexcept;

    /// Prepares geometry after rechecking its extents against the execution's limits.
    /// Checks cancellation before allocation and publication; no threads are claimed by the plan.
    /// @param execution the initialized context, allocator and cancellation policy.
    /// @param descriptor the geometry to own.
    /// @return the plan; `ErrorCode::NotInitialized` for an uninitialized execution;
    ///         `ErrorCode::LimitExceeded` when an extent exceeds the execution's limits;
    ///         `ErrorCode::Cancelled` or `ErrorCode::DeadlineExceeded`; or the memory resource's
    ///         refusal, carrying requested, available and limit bytes. A failure retains no allocation.
    [[nodiscard]] static AFTERNIGHTENGINE_API Result<DrizzleGeometryPlan>
    create(const ExecutionContext& execution, const DrizzleMapDescriptor& descriptor);

    /// Whether this handle owns a preparation.
    /// @return false for an empty or moved-from handle.
    [[nodiscard]] bool valid() const noexcept
    {
        return m_state != nullptr;
    }
    /// The complete owned geometry.
    /// @return a borrow valid through this handle's lifetime, or null when empty.
    [[nodiscard]] AFTERNIGHTENGINE_API const DrizzleMapDescriptor* descriptor() const noexcept;
    /// The context bytes retained by the shared preparation, counted once regardless of copies.
    /// @return zero when empty; otherwise the complete state allocation in bytes.
    [[nodiscard]] AFTERNIGHTENGINE_API std::uint64_t residentBytes() const noexcept;

private:
    friend class DrizzleGeometryAccess;
    /// Adopts one already-counted reference.
    /// @param state the initialized, context-owned preparation.
    explicit DrizzleGeometryPlan(DrizzleGeometryState* state) noexcept : m_state(state) {}
    DrizzleGeometryState* m_state = nullptr; ///< Intrusively counted implementation state.
};

/// Hard retention limits for an operation-independent geometry cache.
struct DrizzleGeometryCacheOptions
{
    std::uint32_t maximumEntries = 8u; ///< Capacity in [1,256]; never grows after construction.
    std::uint64_t maximumBytes = 1024u * 1024u; ///< Ceiling including cache state, slots and retained plans.
};

/// Cache accounting, available in every build; event counts saturate rather than wrap.
struct DrizzleGeometryCacheCounters
{
    std::uint64_t hits = 0u; ///< Exact geometry reuses.
    std::uint64_t misses = 0u; ///< Lookup requests needing preparation, including failed preparation.
    std::uint64_t evictions = 0u; ///< Least-recently-used entries removed by successful lookups.
    std::uint64_t retainedBytes = 0u; ///< Cache state, fixed slot array and entries currently retained.
    std::uint32_t entries = 0u; ///< Occupied slots.
    std::uint32_t capacity = 0u; ///< Slots that fit both stated ceilings.
};

/// Bounded least-recently-used cache of immutable geometry plans, owned by a caller, never global.
/// Lookup, clear and counter reads are serialized. Generated maps are never cached. Returned plans
/// remain valid after eviction or cache destruction; those external references remain charged to
/// the context, outside this cache's retention ceiling. A miss prepares before eviction, so a
/// transient context-budget refusal preserves every existing entry. Count prepared state in the
/// drizzle working-set request when retaining this cache across an execution.
class DrizzleGeometryCache
{
public:
    /// Empty handle.
    DrizzleGeometryCache() noexcept = default;
    /// Releases entries, fixed slots and the context lease.
    AFTERNIGHTENGINE_API ~DrizzleGeometryCache();
    /// Transfers exclusive ownership; concurrent operations must have finished first.
    /// @param other the cache to move, left empty.
    AFTERNIGHTENGINE_API DrizzleGeometryCache(DrizzleGeometryCache&& other) noexcept;
    /// Releases the old cache and transfers another.
    /// @param other the cache to move, left empty; self-assignment is harmless.
    /// @return this cache.
    AFTERNIGHTENGINE_API DrizzleGeometryCache& operator=(DrizzleGeometryCache&& other) noexcept;
    /// A cache has one owner; use separate plan handles for shared immutable access.
    DrizzleGeometryCache(const DrizzleGeometryCache&) = delete;
    /// A cache has one owner.
    DrizzleGeometryCache& operator=(const DrizzleGeometryCache&) = delete;

    /// Allocates a fixed slot array, narrowing capacity to the byte ceiling without growing later.
    /// @param execution the initialized owning context and cancellation policy.
    /// @param options both retention ceilings; enough bytes for at least one entry are required.
    /// @return the cache; `ErrorCode::NotInitialized` for an uninitialized execution;
    ///         `ErrorCode::InvalidArgument` for an entry ceiling outside [1,256];
    ///         `ErrorCode::MemoryBudgetExceeded`, carrying requested, available and limit bytes, when
    ///         `maximumBytes` cannot hold the cache state and one entry; cancellation; or the memory
    ///         resource's refusal. Both control and slot bytes are context-accounted.
    [[nodiscard]] static AFTERNIGHTENGINE_API Result<DrizzleGeometryCache>
    create(const ExecutionContext& execution, const DrizzleGeometryCacheOptions& options = {});
    /// Finds or prepares geometry, checking cancellation even on a hit. Lookups are serialized,
    /// including the preparation of a miss, which happens before any entry is evicted.
    /// @param execution an operation belonging to the cache's engine context.
    /// @param descriptor the complete geometry identity.
    /// @return a shared plan; `ErrorCode::NotInitialized` for an empty cache or an uninitialized
    ///         execution; `ErrorCode::IncompatibleArguments` for an execution of another engine
    ///         context; cancellation; or any failure of `DrizzleGeometryPlan::create`. A failure
    ///         evicts nothing and publishes no partially prepared plan.
    [[nodiscard]] AFTERNIGHTENGINE_API Result<DrizzleGeometryPlan> get(const ExecutionContext& execution,
                                                                       const DrizzleMapDescriptor& descriptor);
    /// Releases cached references while retaining the fixed slots and cumulative counters.
    /// @return success, or `ErrorCode::NotInitialized` for an empty cache.
    [[nodiscard]] AFTERNIGHTENGINE_API Status clear();
    /// Reads a consistent snapshot under the cache lock.
    /// @return all zeroes for an empty cache; otherwise current retention and lifetime counts.
    [[nodiscard]] AFTERNIGHTENGINE_API DrizzleGeometryCacheCounters counters() const noexcept;

private:
    class State;
    /// Adopts the context-owned cache state.
    /// @param state the initialized allocation.
    explicit DrizzleGeometryCache(State* state) noexcept : m_state(state) {}
    State* m_state = nullptr; ///< Exclusive implementation ownership.
};

} // namespace an
