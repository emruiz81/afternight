// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The memory a consumer holds that the engine does not count: allowances per category, admitted before allocation.
///
/// The engine accounts for every byte it allocates - images, masks, validity planes, scratch - against its context's
/// budget. A consumer holds more than that: undo copies, snapshots waiting for the history writer, derived results
/// kept for reuse, and the buffers a viewer paints from. None of it is the engine's to count, and none of it may grow
/// without a bound either. This header gives each of those kinds of memory an allowance of its own and a single
/// accountant that admits a request before the caller allocates, so that a refusal happens before the allocation it
/// would have prevented rather than after the machine is out of memory.
///
/// Two rules shape it. **Eviction precedes refusal**: when a request does not fit, the category's evictor - registered
/// by whatever owns entries it can drop, such as a cache - is asked for the shortfall first, and the request is
/// refused only when what remains still does not fit. **A required buffer is admitted and counted**: the buffer a
/// visible view is painting right now cannot be refused without blanking the view, so it is admitted beyond the
/// allowance and the overrun is counted where a person can see it, instead of being hidden or refused.
///
/// What the allowances never cover is stated as plainly: engine allocations stay the engine's, and memory a library
/// allocates on its own - a toolkit's image, an interpreter's array, a mapped file, a codec's workspace - is measured
/// separately and never presented as covered here.
#pragma once

#include <AfterNightConsumerRuntime/ConsumerRuntimeExport.h>

#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <utility>

namespace an::consumer
{

namespace detail
{
class MemoryState;
} // namespace detail

/// The kinds of consumer memory that have an allowance of their own.
enum class MemoryCategory : std::uint8_t
{
    /// Undo steps held in memory.
    HistoryCopies,

    /// Snapshots taken for an undo step and not yet written by the history writer.
    PendingSnapshots,

    /// Results kept so a later request with the same key need not compute them again.
    DerivedCaches,

    /// The buffers views paint from.
    DisplayBuffers
};

/// How many categories `MemoryCategory` has.
inline constexpr std::size_t kMemoryCategoryCount = 4u;

/// A category's name, for messages and records.
///
/// @param category the category.
/// @return a static string; "unknown" for a value outside the enumeration.
[[nodiscard]] AFTERNIGHTCONSUMERRUNTIME_API const char* memoryCategoryName(MemoryCategory category) noexcept;

/// An allowance that bounds nothing.
inline constexpr std::uint64_t kUnlimitedAllowance = (std::numeric_limits<std::uint64_t>::max)();

/// One mebibyte.
inline constexpr std::uint64_t kMebibyte = 1024ull * 1024ull;

/// One gibibyte.
inline constexpr std::uint64_t kGibibyte = 1024ull * kMebibyte;

/// What one category may hold.
struct CategoryAllowance
{
    /// The bytes the category's reservations may hold together.
    std::uint64_t bytes = kUnlimitedAllowance;

    /// How many reservations the category may hold at once; zero states no count.
    std::uint32_t maxEntries = 0u;

    /// Whether one request larger than `bytes` is admitted when the category holds nothing. A snapshot of the image
    /// being edited must always fit, or every edit of an image larger than the allowance would fail.
    bool admitsOneOversized = false;
};

/// The session's consumer allowances. The defaults are the proposed session defaults: snapshots pending for the
/// history writer within the larger of one gibibyte and one copy of the image being edited, at most two at once;
/// derived results within one gibibyte; view buffers within 512 mebibytes. Undo copies have no byte allowance of
/// their own: they are bounded by the history's depth, which the history's owner enforces.
struct MemoryAllowances
{
    /// Undo steps held in memory.
    CategoryAllowance historyCopies{};

    /// Snapshots waiting for the history writer.
    CategoryAllowance pendingSnapshots{.bytes = kGibibyte, .maxEntries = 2u, .admitsOneOversized = true};

    /// Results kept for reuse. The caches that hold them keep their own entry counts.
    CategoryAllowance derivedCaches{.bytes = kGibibyte};

    /// Buffers views paint from.
    CategoryAllowance displayBuffers{.bytes = 512u * kMebibyte};

    /// What every category may hold together.
    std::uint64_t totalBytes = kUnlimitedAllowance;
};

/// One category's allowance out of a set of allowances.
///
/// @param allowances the set.
/// @param category the category.
/// @return the category's allowance; an unlimited one for a value outside the enumeration.
[[nodiscard]] AFTERNIGHTCONSUMERRUNTIME_API CategoryAllowance allowanceOf(const MemoryAllowances& allowances,
                                                                          MemoryCategory category) noexcept;

/// Whether a set of allowances can be enforced.
///
/// @param allowances the set.
/// @return success, or `ErrorCode::InvalidArgument` naming the field when a byte allowance or the total is zero.
[[nodiscard]] AFTERNIGHTCONSUMERRUNTIME_API an::Status checkMemoryAllowances(const MemoryAllowances& allowances);

/// How a request meets its allowance.
enum class ReservationKind : std::uint8_t
{
    /// Admitted within the allowance, after eviction, or refused.
    Ordinary,

    /// Admitted whatever the allowance, with what exceeds it counted as an overrun: the buffer a visible view paints
    /// now, which cannot be refused without blanking the view.
    Required
};

/// What one category has held and refused.
struct MemoryCategoryCounters
{
    /// Bytes its reservations hold now.
    std::uint64_t reservedBytes = 0u;

    /// Reservations it holds now.
    std::uint64_t entries = 0u;

    /// The most bytes it has held at once.
    std::uint64_t peakBytes = 0u;

    /// Requests admitted.
    std::uint64_t admitted = 0u;

    /// Requests refused.
    std::uint64_t refused = 0u;

    /// How often its evictor was asked for room.
    std::uint64_t evictionRequests = 0u;

    /// Bytes its evictor reported releasing.
    std::uint64_t evictedBytes = 0u;

    /// Required requests admitted beyond the allowance.
    std::uint64_t overrunAdmissions = 0u;

    /// Bytes held beyond the allowance now, by required requests.
    std::uint64_t overrunBytes = 0u;
};

/// Bytes admitted against a category, returned when this is released or destroyed.
///
/// Move-only: a copy would return the same bytes twice. Safe to release on any thread and after the session that
/// admitted it is gone, because it keeps the accountant's state alive.
class MemoryReservation
{
public:
    /// A reservation that holds nothing.
    MemoryReservation() noexcept = default;

    /// Returns the bytes this holds.
    AFTERNIGHTCONSUMERRUNTIME_API ~MemoryReservation();

    /// Takes over another reservation's bytes, leaving it holding nothing.
    ///
    /// @param other the reservation to take over.
    MemoryReservation(MemoryReservation&& other) noexcept
        : m_state(std::move(other.m_state)), m_category(other.m_category), m_bytes(std::exchange(other.m_bytes, 0u)),
          m_overrun(std::exchange(other.m_overrun, 0u))
    {
    }

    /// Returns this reservation's bytes and takes over another's.
    ///
    /// @param other the reservation to take over.
    /// @return this reservation.
    AFTERNIGHTCONSUMERRUNTIME_API MemoryReservation& operator=(MemoryReservation&& other) noexcept;

    MemoryReservation(const MemoryReservation&) = delete;
    MemoryReservation& operator=(const MemoryReservation&) = delete;

    /// Whether this holds bytes.
    [[nodiscard]] bool valid() const noexcept
    {
        return m_state != nullptr;
    }

    /// The category this was admitted against.
    [[nodiscard]] MemoryCategory category() const noexcept
    {
        return m_category;
    }

    /// The bytes this holds; zero once released.
    [[nodiscard]] std::uint64_t bytes() const noexcept
    {
        return m_bytes;
    }

    /// Returns the bytes now rather than at destruction. Releasing twice returns them once.
    AFTERNIGHTCONSUMERRUNTIME_API void release() noexcept;

private:
    friend class ConsumerMemory;

    MemoryReservation(std::shared_ptr<detail::MemoryState> state, MemoryCategory category, std::uint64_t bytes,
                      std::uint64_t overrun) noexcept
        : m_state(std::move(state)), m_category(category), m_bytes(bytes), m_overrun(overrun)
    {
    }

    std::shared_ptr<detail::MemoryState> m_state;
    MemoryCategory m_category = MemoryCategory::HistoryCopies;
    std::uint64_t m_bytes = 0u;
    std::uint64_t m_overrun = 0u;
};

/// What a category's owner can drop when room is needed: called with the shortfall in bytes, it releases
/// reservations of that category and answers how many bytes it released.
///
/// It runs on the thread that made the request, outside the accountant's lock, and is never run twice at once for one
/// accountant. It must not reserve memory itself.
using MemoryEvictor = std::function<std::uint64_t(std::uint64_t shortfallBytes)>;

/// A registered evictor, removed when this is released or destroyed. Removal waits for a call in progress, so the
/// owner may destroy what the evictor references as soon as this is gone.
class EvictorRegistration
{
public:
    /// A registration that holds nothing.
    EvictorRegistration() noexcept = default;

    /// Removes the evictor.
    AFTERNIGHTCONSUMERRUNTIME_API ~EvictorRegistration();

    /// Takes over another registration.
    ///
    /// @param other the registration to take over.
    EvictorRegistration(EvictorRegistration&& other) noexcept = default;

    /// Removes this registration's evictor and takes over another's.
    ///
    /// @param other the registration to take over.
    /// @return this registration.
    AFTERNIGHTCONSUMERRUNTIME_API EvictorRegistration& operator=(EvictorRegistration&& other) noexcept;

    EvictorRegistration(const EvictorRegistration&) = delete;
    EvictorRegistration& operator=(const EvictorRegistration&) = delete;

    /// Whether an evictor is registered through this.
    [[nodiscard]] bool valid() const noexcept
    {
        return m_state != nullptr;
    }

    /// Removes the evictor now.
    AFTERNIGHTCONSUMERRUNTIME_API void release() noexcept;

private:
    friend class ConsumerMemory;

    EvictorRegistration(std::shared_ptr<detail::MemoryState> state, MemoryCategory category,
                        std::uint64_t token) noexcept
        : m_state(std::move(state)), m_category(category), m_token(token)
    {
    }

    std::shared_ptr<detail::MemoryState> m_state;
    MemoryCategory m_category = MemoryCategory::HistoryCopies;
    std::uint64_t m_token = 0u;
};

/// The accountant of a session's consumer memory.
///
/// Thread-safe. A session owns one; its allowances change only when the session's settings are applied.
class ConsumerMemory
{
public:
    /// An accountant with the given allowances.
    ///
    /// @param allowances what each category may hold. Checked by the caller with `checkMemoryAllowances`.
    AFTERNIGHTCONSUMERRUNTIME_API explicit ConsumerMemory(const MemoryAllowances& allowances = MemoryAllowances{});

    /// Leaves outstanding reservations valid: they keep the accountant's state alive.
    AFTERNIGHTCONSUMERRUNTIME_API ~ConsumerMemory();

    ConsumerMemory(const ConsumerMemory&) = delete;
    ConsumerMemory& operator=(const ConsumerMemory&) = delete;
    ConsumerMemory(ConsumerMemory&&) = delete;
    ConsumerMemory& operator=(ConsumerMemory&&) = delete;

    /// Admits a request against its category before the caller allocates.
    ///
    /// A request that does not fit asks the category's evictor for the shortfall once, then decides again.
    ///
    /// @param category what the memory is for.
    /// @param bytes how many bytes the caller is about to allocate; zero is refused.
    /// @param kind whether the request may be refused.
    /// @return the reservation, or `ErrorCode::InvalidArgument` for zero bytes, or
    ///         `ErrorCode::MemoryBudgetExceeded` carrying the requested, available and limit bytes and naming the
    ///         category, when an ordinary request does not fit after eviction.
    [[nodiscard]] AFTERNIGHTCONSUMERRUNTIME_API an::Result<MemoryReservation>
    reserve(MemoryCategory category, std::uint64_t bytes, ReservationKind kind = ReservationKind::Ordinary);

    /// Registers what a category's owner can drop when room is needed, replacing any evictor that category had.
    ///
    /// @param category the category the evictor releases.
    /// @param evictor the evictor; an empty one is refused.
    /// @return the registration, or `ErrorCode::InvalidArgument` for an empty evictor.
    [[nodiscard]] AFTERNIGHTCONSUMERRUNTIME_API an::Result<EvictorRegistration> registerEvictor(MemoryCategory category,
                                                                                                MemoryEvictor evictor);

    /// What a category holds and has refused.
    ///
    /// @param category the category.
    /// @return its counters.
    [[nodiscard]] AFTERNIGHTCONSUMERRUNTIME_API MemoryCategoryCounters counters(MemoryCategory category) const;

    /// The bytes every category holds together.
    [[nodiscard]] AFTERNIGHTCONSUMERRUNTIME_API std::uint64_t totalReservedBytes() const;

    /// The allowances in force.
    [[nodiscard]] AFTERNIGHTCONSUMERRUNTIME_API MemoryAllowances allowances() const;

    /// Replaces the allowances in force. Reservations already held are kept, even where they exceed the new
    /// allowance; later requests are decided against the new one.
    ///
    /// @param allowances the new allowances, already checked.
    AFTERNIGHTCONSUMERRUNTIME_API void setAllowances(const MemoryAllowances& allowances);

private:
    std::shared_ptr<detail::MemoryState> m_state;
};

} // namespace an::consumer
