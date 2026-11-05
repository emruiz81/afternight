// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The consumer memory accountant: per-category allowances admitted before allocation, eviction before refusal, and
/// required buffers admitted and counted as overruns.

#include <AfterNightConsumerRuntime/ConsumerMemory.h>

#include <AfterNightEngine/Core/ErrorCode.h>

#include <algorithm>
#include <array>
#include <mutex>
#include <string>
#include <utility>

namespace an::consumer
{

namespace detail
{

/// What an accountant and every reservation and registration it handed out share: a plain record behind the
/// accountant's own interface, so its fields are named as a value struct's are.
class MemoryState
{
public:
    /// One registered evictor and the token that removes it.
    struct Evictor
    {
        std::uint64_t token = 0u;
        MemoryEvictor evictor{};
    };

    explicit MemoryState(const MemoryAllowances& initial) : allowances(initial) {}

    /// Guards the allowances, the counters and the total.
    std::mutex mutex;

    /// Serializes evictions with each other and with changes of the registered evictors, so removing an evictor waits
    /// for a call in progress. Never taken while `mutex` is held.
    std::mutex evictionMutex;

    /// The allowances in force. Guarded by `mutex`.
    MemoryAllowances allowances;

    /// Per category. Guarded by `mutex`.
    std::array<MemoryCategoryCounters, kMemoryCategoryCount> counters{};

    /// Every category's reserved bytes together. Guarded by `mutex`.
    std::uint64_t totalBytes = 0u;

    /// Per category. Guarded by `evictionMutex`.
    std::array<Evictor, kMemoryCategoryCount> evictors{};

    /// The token the next registration is given. Guarded by `evictionMutex`.
    std::uint64_t nextToken = 1u;

    /// Returns a reservation's bytes.
    void release(MemoryCategory category, std::uint64_t bytes, std::uint64_t overrun) noexcept
    {
        const std::lock_guard<std::mutex> lock(mutex);
        MemoryCategoryCounters& held = counters[static_cast<std::size_t>(category)];
        held.reservedBytes -= (std::min)(held.reservedBytes, bytes);
        held.overrunBytes -= (std::min)(held.overrunBytes, overrun);
        held.entries -= held.entries > 0u ? 1u : 0u;
        totalBytes -= (std::min)(totalBytes, bytes);
    }

    /// Removes an evictor if the token still names it.
    void unregister(MemoryCategory category, std::uint64_t token) noexcept
    {
        const std::lock_guard<std::mutex> lock(evictionMutex);
        Evictor& slot = evictors[static_cast<std::size_t>(category)];
        if (slot.token == token)
        {
            slot = Evictor{};
        }
    }
};

} // namespace detail

namespace
{

[[nodiscard]] bool knownCategory(MemoryCategory category) noexcept
{
    return static_cast<std::size_t>(category) < kMemoryCategoryCount;
}

[[nodiscard]] std::uint64_t saturatingAdd(std::uint64_t left, std::uint64_t right) noexcept
{
    return left > kUnlimitedAllowance - right ? kUnlimitedAllowance : left + right;
}

/// Why an ordinary request does not fit now, or nothing when it does.
enum class Shortfall : std::uint8_t
{
    None,
    Bytes,
    Entries,
    Total
};

[[nodiscard]] Shortfall shortfallOf(const detail::MemoryState& state, MemoryCategory category, std::uint64_t bytes)
{
    const CategoryAllowance allowance = allowanceOf(state.allowances, category);
    const MemoryCategoryCounters& counters = state.counters[static_cast<std::size_t>(category)];
    if (allowance.maxEntries != 0u && counters.entries >= allowance.maxEntries)
    {
        return Shortfall::Entries;
    }
    const bool oversizedAlone = allowance.admitsOneOversized && counters.entries == 0u;
    if (!oversizedAlone && saturatingAdd(counters.reservedBytes, bytes) > allowance.bytes)
    {
        return Shortfall::Bytes;
    }
    if (saturatingAdd(state.totalBytes, bytes) > state.allowances.totalBytes)
    {
        return Shortfall::Total;
    }
    return Shortfall::None;
}

/// How many bytes an evictor has to release for a request to fit.
[[nodiscard]] std::uint64_t bytesWanted(const detail::MemoryState& state, MemoryCategory category, std::uint64_t bytes,
                                        Shortfall shortfall)
{
    const CategoryAllowance allowance = allowanceOf(state.allowances, category);
    const MemoryCategoryCounters& counters = state.counters[static_cast<std::size_t>(category)];
    switch (shortfall)
    {
    case Shortfall::Bytes:
        return saturatingAdd(counters.reservedBytes, bytes) - allowance.bytes;
    case Shortfall::Total:
        return saturatingAdd(state.totalBytes, bytes) - state.allowances.totalBytes;
    case Shortfall::Entries:
        // One entry has to go; its size is the evictor's to know.
        return 1u;
    case Shortfall::None:
        break;
    }
    return 0u;
}

[[nodiscard]] an::Status refusal(const detail::MemoryState& state, MemoryCategory category, std::uint64_t bytes,
                                 Shortfall shortfall)
{
    const CategoryAllowance allowance = allowanceOf(state.allowances, category);
    const MemoryCategoryCounters& counters = state.counters[static_cast<std::size_t>(category)];
    const std::string name = memoryCategoryName(category);
    if (shortfall == Shortfall::Entries)
    {
        return an::Status::failure(
            an::ErrorCode::LimitExceeded,
            an::StatusDetail{
                .message = "the " + name + " allowance already holds its most entries",
                .operation = "ConsumerMemory::reserve",
                .domain = an::ErrorDomain::Memory,
                .context = {an::StatusContextField::ofText(an::ContextKey::Parameter, name),
                            an::StatusContextField::ofCount(an::ContextKey::Requested, 1u),
                            an::StatusContextField::ofCount(an::ContextKey::Limit, allowance.maxEntries)}});
    }
    const bool total = shortfall == Shortfall::Total;
    const std::uint64_t limit = total ? state.allowances.totalBytes : allowance.bytes;
    const std::uint64_t held = total ? state.totalBytes : counters.reservedBytes;
    const std::uint64_t available = limit > held ? limit - held : 0u;
    return an::Status::failure(
        an::ErrorCode::MemoryBudgetExceeded,
        an::StatusDetail{.message = total ? "the consumer memory allowances together cannot hold the request"
                                          : "the " + name + " allowance cannot hold the request",
                         .operation = "ConsumerMemory::reserve",
                         .domain = an::ErrorDomain::Memory,
                         .context = {an::StatusContextField::ofText(an::ContextKey::Parameter, name),
                                     an::StatusContextField::ofCount(an::ContextKey::Requested, bytes),
                                     an::StatusContextField::ofCount(an::ContextKey::Available, available),
                                     an::StatusContextField::ofCount(an::ContextKey::Limit, limit)}});
}

} // namespace

const char* memoryCategoryName(MemoryCategory category) noexcept
{
    switch (category)
    {
    case MemoryCategory::HistoryCopies:
        return "history copies";
    case MemoryCategory::PendingSnapshots:
        return "pending snapshots";
    case MemoryCategory::DerivedCaches:
        return "derived caches";
    case MemoryCategory::DisplayBuffers:
        return "display buffers";
    }
    return "unknown";
}

CategoryAllowance allowanceOf(const MemoryAllowances& allowances, MemoryCategory category) noexcept
{
    switch (category)
    {
    case MemoryCategory::HistoryCopies:
        return allowances.historyCopies;
    case MemoryCategory::PendingSnapshots:
        return allowances.pendingSnapshots;
    case MemoryCategory::DerivedCaches:
        return allowances.derivedCaches;
    case MemoryCategory::DisplayBuffers:
        return allowances.displayBuffers;
    }
    return CategoryAllowance{};
}

an::Status checkMemoryAllowances(const MemoryAllowances& allowances)
{
    const auto zero = [](const char* field) {
        return an::Status::failure(
            an::ErrorCode::InvalidArgument,
            an::StatusDetail{.message = "a memory allowance of zero bytes admits nothing; leave it unlimited instead",
                             .operation = "checkMemoryAllowances",
                             .domain = an::ErrorDomain::Memory,
                             .context = {an::StatusContextField::ofText(an::ContextKey::Parameter, field)}});
    };
    if (allowances.historyCopies.bytes == 0u)
    {
        return zero("memory.historyCopies.bytes");
    }
    if (allowances.pendingSnapshots.bytes == 0u)
    {
        return zero("memory.pendingSnapshots.bytes");
    }
    if (allowances.derivedCaches.bytes == 0u)
    {
        return zero("memory.derivedCaches.bytes");
    }
    if (allowances.displayBuffers.bytes == 0u)
    {
        return zero("memory.displayBuffers.bytes");
    }
    if (allowances.totalBytes == 0u)
    {
        return zero("memory.totalBytes");
    }
    return an::Status::success();
}

MemoryReservation::~MemoryReservation()
{
    release();
}

MemoryReservation& MemoryReservation::operator=(MemoryReservation&& other) noexcept
{
    if (this != &other)
    {
        release();
        m_state = std::move(other.m_state);
        m_category = other.m_category;
        m_bytes = std::exchange(other.m_bytes, 0u);
        m_overrun = std::exchange(other.m_overrun, 0u);
    }
    return *this;
}

void MemoryReservation::release() noexcept
{
    if (m_state != nullptr)
    {
        m_state->release(m_category, m_bytes, m_overrun);
        m_state.reset();
    }
    m_bytes = 0u;
    m_overrun = 0u;
}

EvictorRegistration::~EvictorRegistration()
{
    release();
}

EvictorRegistration& EvictorRegistration::operator=(EvictorRegistration&& other) noexcept
{
    if (this != &other)
    {
        release();
        m_state = std::move(other.m_state);
        m_category = other.m_category;
        m_token = std::exchange(other.m_token, 0u);
    }
    return *this;
}

void EvictorRegistration::release() noexcept
{
    if (m_state != nullptr)
    {
        m_state->unregister(m_category, m_token);
        m_state.reset();
    }
    m_token = 0u;
}

ConsumerMemory::ConsumerMemory(const MemoryAllowances& allowances)
    : m_state(std::make_shared<detail::MemoryState>(allowances))
{
}

ConsumerMemory::~ConsumerMemory() = default;

an::Result<MemoryReservation> ConsumerMemory::reserve(MemoryCategory category, std::uint64_t bytes,
                                                      ReservationKind kind)
{
    if (!knownCategory(category) || bytes == 0u)
    {
        return an::Status::failure(
            an::ErrorCode::InvalidArgument,
            an::StatusDetail{.message = bytes == 0u ? "a reservation of zero bytes holds nothing"
                                                    : "the category is not one of the consumer memory categories",
                             .operation = "ConsumerMemory::reserve",
                             .domain = an::ErrorDomain::Memory,
                             .context = {an::StatusContextField::ofText(an::ContextKey::Parameter,
                                                                        bytes == 0u ? "bytes" : "category")}});
    }
    detail::MemoryState& state = *m_state;
    const auto index = static_cast<std::size_t>(category);

    // Counts an admitted request. Called with the lock held.
    const auto admit = [&](std::uint64_t overrun) {
        MemoryCategoryCounters& held = state.counters[index];
        held.reservedBytes = saturatingAdd(held.reservedBytes, bytes);
        held.peakBytes = (std::max)(held.peakBytes, held.reservedBytes);
        held.overrunBytes = saturatingAdd(held.overrunBytes, overrun);
        held.overrunAdmissions += overrun > 0u ? 1u : 0u;
        ++held.entries;
        ++held.admitted;
        state.totalBytes = saturatingAdd(state.totalBytes, bytes);
        return MemoryReservation{m_state, category, bytes, overrun};
    };

    Shortfall shortfall = Shortfall::None;
    std::uint64_t wanted = 0u;
    {
        const std::lock_guard<std::mutex> lock(state.mutex);
        shortfall = shortfallOf(state, category, bytes);
        if (shortfall == Shortfall::None)
        {
            return admit(0u);
        }
        if (kind == ReservationKind::Required)
        {
            // Admitted whatever the allowance; what exceeds it is the overrun this reservation carries.
            const CategoryAllowance allowance = allowanceOf(state.allowances, category);
            const std::uint64_t after = saturatingAdd(state.counters[index].reservedBytes, bytes);
            const std::uint64_t over = after > allowance.bytes ? after - allowance.bytes : 0u;
            return admit((std::min)(over, bytes));
        }
        wanted = bytesWanted(state, category, bytes, shortfall);
    }

    // Eviction precedes refusal: the category's owner is asked for the shortfall once, outside the counters' lock,
    // because what it releases takes that lock.
    {
        const std::lock_guard<std::mutex> eviction(state.evictionMutex);
        const MemoryEvictor& evictor = state.evictors[index].evictor;
        if (evictor)
        {
            const std::uint64_t released = evictor(wanted);
            const std::lock_guard<std::mutex> lock(state.mutex);
            ++state.counters[index].evictionRequests;
            state.counters[index].evictedBytes = saturatingAdd(state.counters[index].evictedBytes, released);
        }
    }

    const std::lock_guard<std::mutex> lock(state.mutex);
    shortfall = shortfallOf(state, category, bytes);
    if (shortfall == Shortfall::None)
    {
        return admit(0u);
    }
    ++state.counters[index].refused;
    return refusal(state, category, bytes, shortfall);
}

an::Result<EvictorRegistration> ConsumerMemory::registerEvictor(MemoryCategory category, MemoryEvictor evictor)
{
    if (!knownCategory(category) || !evictor)
    {
        return an::Status::failure(
            an::ErrorCode::InvalidArgument,
            an::StatusDetail{.message = !evictor ? "an empty evictor can release nothing"
                                                 : "the category is not one of the consumer memory categories",
                             .operation = "ConsumerMemory::registerEvictor",
                             .domain = an::ErrorDomain::Memory,
                             .context = {an::StatusContextField::ofText(an::ContextKey::Parameter,
                                                                        !evictor ? "evictor" : "category")}});
    }
    const std::lock_guard<std::mutex> lock(m_state->evictionMutex);
    const std::uint64_t token = m_state->nextToken++;
    m_state->evictors[static_cast<std::size_t>(category)] = detail::MemoryState::Evictor{token, std::move(evictor)};
    return EvictorRegistration{m_state, category, token};
}

MemoryCategoryCounters ConsumerMemory::counters(MemoryCategory category) const
{
    if (!knownCategory(category))
    {
        return MemoryCategoryCounters{};
    }
    const std::lock_guard<std::mutex> lock(m_state->mutex);
    return m_state->counters[static_cast<std::size_t>(category)];
}

std::uint64_t ConsumerMemory::totalReservedBytes() const
{
    const std::lock_guard<std::mutex> lock(m_state->mutex);
    return m_state->totalBytes;
}

MemoryAllowances ConsumerMemory::allowances() const
{
    const std::lock_guard<std::mutex> lock(m_state->mutex);
    return m_state->allowances;
}

void ConsumerMemory::setAllowances(const MemoryAllowances& allowances)
{
    const std::lock_guard<std::mutex> lock(m_state->mutex);
    m_state->allowances = allowances;
}

} // namespace an::consumer
