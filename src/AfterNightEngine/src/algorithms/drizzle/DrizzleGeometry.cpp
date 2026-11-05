// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - drizzle geometry: the scalar point maps, exact geometry comparison, the
// shared plan's reference counting and the bounded least-recently-used plan cache.
//
// The point maps are the oracle every generated map is tested against, so they compose the public
// astrometry and registration operations exactly as the descriptor states, with no shortcut. The
// inverse is not trusted on its own, because supplied inverse SIP coefficients are an approximation
// and a polynomial registration can have more than one preimage: its answer is mapped forward
// again and refused beyond 0.01 destination pixels.
//
// The cache holds its entries in a fixed slot array allocated at creation. Ages are ranks within
// that array rather than timestamps, so they never wrap: a hit or an insertion makes its entry the
// youngest and ages every entry that was younger by one. A miss prepares the new plan before it
// chooses a victim, so a failed preparation evicts nothing. Every lookup, clear and counter read
// holds the cache's mutex, the preparation of a miss included.

#include <AfterNightEngine/Algorithms/Drizzle/DrizzleGeometry.h>

#include <AfterNightEngine/Core/CheckedSize.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include "algorithms/drizzle/DrizzleGeometryInternal.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <mutex>
#include <utility>

namespace an
{
namespace drizzle_detail
{

Status failure(ErrorCode code, const char* message, const char* operation)
{
    return Status::failure(code,
                           StatusDetail{.message = message, .operation = operation, .domain = ErrorDomain::Algorithms});
}

Status checkGeometry(const ExecutionContext& execution, const DrizzleMapDescriptor& descriptor)
{
    constexpr const char* kOperation = "an::DrizzleGeometryPlan::create";
    if (!execution.valid())
        return failure(ErrorCode::NotInitialized, "geometry requires an initialized execution", kOperation);
    if (const auto stopped = execution.checkCancellation(); stopped.failed())
        return stopped;
    const auto limits = execution.limits();
    for (const auto& extent : {descriptor.sourceExtent(), descriptor.outputExtent()})
        if (!ImageExtent::create(extent.width(), extent.height(), extent.channelCount(), limits).has_value())
            return failure(ErrorCode::LimitExceeded, "geometry exceeds this execution's extent limits", kOperation);
    return Status::success();
}

} // namespace drizzle_detail
namespace
{

using drizzle_detail::failure;

void releasePlan(DrizzleGeometryState* state) noexcept
{
    if (state != nullptr && state->references.fetch_sub(1u, std::memory_order_acq_rel) == 1u)
        drizzle_detail::destroyState(state);
}

void increment(std::uint64_t& value) noexcept
{
    if (value != (std::numeric_limits<std::uint64_t>::max)())
        ++value;
}

Status cacheBudgetRefusal(std::uint64_t requested, std::uint64_t available, std::uint64_t limit)
{
    return Status::failure(ErrorCode::MemoryBudgetExceeded,
                           StatusDetail{.message = "the geometry cache ceiling cannot retain one prepared entry",
                                        .operation = "an::DrizzleGeometryCache::create",
                                        .domain = ErrorDomain::Algorithms,
                                        .context = {StatusContextField::ofCount(ContextKey::Requested, requested),
                                                    StatusContextField::ofCount(ContextKey::Available, available),
                                                    StatusContextField::ofCount(ContextKey::Limit, limit)}});
}

} // namespace

Result<PixelCoordinate> mapDrizzlePoint(const DrizzleMapDescriptor& descriptor, const PixelCoordinate& source)
{
    constexpr const char* kOperation = "an::mapDrizzlePoint";
    if (!std::isfinite(source.x) || !std::isfinite(source.y))
        return failure(ErrorCode::InvalidArgument, "source coordinates must be finite", kOperation);
    PixelCoordinate point{source.x + 0.5, source.y + 0.5};
    const auto& mapping = descriptor.mapping();
    if (mapping.sourceDistortion.has_value())
    {
        auto corrected = applySipForward(*mapping.sourceDistortion, point);
        if (!corrected.hasValue())
            return corrected.status();
        point = *corrected;
    }
    const auto registered = mapRegistrationPoint(mapping.transform, point, mapping.postTranslation);
    if (!registered.hasValue())
        return registered.status();
    point = {registered->x * descriptor.scale() - 0.5, registered->y * descriptor.scale() - 0.5};
    if (!std::isfinite(point.x) || !std::isfinite(point.y))
        return failure(ErrorCode::InvalidData, "scaled coordinates are not finite", kOperation);
    return point;
}

Result<PixelCoordinate> mapDrizzlePointInverse(const DrizzleMapDescriptor& descriptor,
                                               const PixelCoordinate& destination)
{
    constexpr const char* kOperation = "an::mapDrizzlePointInverse";
    if (!std::isfinite(destination.x) || !std::isfinite(destination.y))
        return failure(ErrorCode::InvalidArgument, "destination coordinates must be finite", kOperation);
    const auto& mapping = descriptor.mapping();
    const PixelCoordinate reference{(destination.x + 0.5) / descriptor.scale(),
                                    (destination.y + 0.5) / descriptor.scale()};
    auto inverse = mapRegistrationPointInverse(mapping.transform, reference, mapping.postTranslation);
    if (!inverse.hasValue())
        return inverse.status();
    PixelCoordinate point = inverse->pixel;
    if (mapping.sourceDistortion.has_value())
    {
        auto corrected = applySipInverse(*mapping.sourceDistortion, point);
        if (!corrected.hasValue())
            return corrected.status();
        point = corrected->pixel;
    }
    point.x -= 0.5;
    point.y -= 0.5;
    const auto roundTrip = mapDrizzlePoint(descriptor, point);
    if (!roundTrip.hasValue() || std::hypot(roundTrip->x - destination.x, roundTrip->y - destination.y) > 0.01)
        return failure(ErrorCode::InvalidData, "inverse residual exceeds 0.01 destination pixels", kOperation);
    return point;
}

bool sameDrizzleGeometry(const DrizzleMapDescriptor& left, const DrizzleMapDescriptor& right) noexcept
{
    const auto& a = left.mapping();
    const auto& b = right.mapping();
    return left.sourceExtent() == right.sourceExtent() && left.outputExtent() == right.outputExtent() &&
           left.scale() == right.scale() && a.transform == b.transform && a.postTranslation == b.postTranslation &&
           a.sourceDistortion == b.sourceDistortion;
}

DrizzleGeometryState::DrizzleGeometryState(const ExecutionContext& execution,
                                           const DrizzleMapDescriptor& geometry) noexcept
    : lease(EngineContextState::retain(execution)), memory(execution.memoryResource()), descriptor(geometry)
{
    if (!descriptor.mapping().sourceDistortion.has_value())
        return;
    const auto& sip = *descriptor.mapping().sourceDistortion;
    sipOrder = (std::max)(sip.forwardX().order, sip.forwardY().order);
    sipReference = sip.referencePixel();
    const auto compile = [](const SipPolynomial& polynomial, auto& terms, std::uint32_t& count) {
        for (std::uint32_t i = 0u; i <= polynomial.order; ++i)
            for (std::uint32_t j = 0u; j <= polynomial.order - i; ++j)
                if (polynomial.coefficients[i][j] != 0.0)
                    terms[count++] = {polynomial.coefficients[i][j], i, j};
    };
    compile(sip.forwardX(), horizontal, horizontalCount);
    compile(sip.forwardY(), vertical, verticalCount);
}

DrizzleGeometryPlan::~DrizzleGeometryPlan()
{
    releasePlan(m_state);
}
DrizzleGeometryPlan::DrizzleGeometryPlan(const DrizzleGeometryPlan& other) noexcept : m_state(other.m_state)
{
    if (m_state != nullptr)
        m_state->references.fetch_add(1u, std::memory_order_relaxed);
}
DrizzleGeometryPlan& DrizzleGeometryPlan::operator=(const DrizzleGeometryPlan& other) noexcept
{
    if (this != &other)
    {
        DrizzleGeometryPlan replacement(other);
        std::swap(m_state, replacement.m_state);
    }
    return *this;
}
DrizzleGeometryPlan::DrizzleGeometryPlan(DrizzleGeometryPlan&& other) noexcept
    : m_state(std::exchange(other.m_state, nullptr))
{
}
DrizzleGeometryPlan& DrizzleGeometryPlan::operator=(DrizzleGeometryPlan&& other) noexcept
{
    if (this != &other)
    {
        releasePlan(m_state);
        m_state = std::exchange(other.m_state, nullptr);
    }
    return *this;
}
Result<DrizzleGeometryPlan> DrizzleGeometryPlan::create(const ExecutionContext& execution,
                                                        const DrizzleMapDescriptor& descriptor)
{
    if (const auto checked = drizzle_detail::checkGeometry(execution, descriptor); checked.failed())
        return checked;
    auto memory = execution.memoryResource()->allocate(sizeof(DrizzleGeometryState), alignof(DrizzleGeometryState));
    if (!memory.hasValue())
        return memory.status();
    DrizzleGeometryPlan result{std::construct_at(static_cast<DrizzleGeometryState*>(*memory), execution, descriptor)};
    if (const auto stopped = execution.checkCancellation(); stopped.failed())
        return stopped;
    return result;
}
const DrizzleMapDescriptor* DrizzleGeometryPlan::descriptor() const noexcept
{
    return m_state != nullptr ? &m_state->descriptor : nullptr;
}
std::uint64_t DrizzleGeometryPlan::residentBytes() const noexcept
{
    return m_state != nullptr ? sizeof(DrizzleGeometryState) : 0u;
}

class DrizzleGeometryCache::State
{
public:
    struct Entry
    {
        DrizzleGeometryPlan plan;
        std::uint32_t age = 0u;
    };
    State(const ExecutionContext& execution, std::uint32_t capacity) noexcept
        : lease(EngineContextState::retain(execution)), memory(execution.memoryResource())
    {
        counts.capacity = capacity;
        counts.retainedBytes = sizeof(State) + sizeof(Entry) * capacity;
    }
    ~State()
    {
        if (entries != nullptr)
        {
            std::destroy_n(entries, counts.capacity);
            memory->deallocate(entries, sizeof(Entry) * counts.capacity, alignof(Entry));
        }
    }
    void touch(std::uint32_t selected) noexcept
    {
        // Ages are ranks within a fixed-capacity array, so they never need an epoch that wraps.
        const auto oldAge = entries[selected].age;
        for (std::uint32_t i = 0u; i < counts.capacity; ++i)
            if (i != selected && entries[i].plan.valid() && entries[i].age <= oldAge)
                ++entries[i].age;
        entries[selected].age = 0u;
    }
    EngineContextLease lease;
    MemoryResource* memory;
    Entry* entries = nullptr;
    mutable std::mutex mutex;
    DrizzleGeometryCacheCounters counts{};
};

DrizzleGeometryCache::~DrizzleGeometryCache()
{
    drizzle_detail::destroyState(m_state);
}
DrizzleGeometryCache::DrizzleGeometryCache(DrizzleGeometryCache&& other) noexcept
    : m_state(std::exchange(other.m_state, nullptr))
{
}
DrizzleGeometryCache& DrizzleGeometryCache::operator=(DrizzleGeometryCache&& other) noexcept
{
    if (this != &other)
    {
        drizzle_detail::destroyState(m_state);
        m_state = std::exchange(other.m_state, nullptr);
    }
    return *this;
}
Result<DrizzleGeometryCache> DrizzleGeometryCache::create(const ExecutionContext& execution,
                                                          const DrizzleGeometryCacheOptions& options)
{
    constexpr const char* kOperation = "an::DrizzleGeometryCache::create";
    if (!execution.valid())
        return failure(ErrorCode::NotInitialized, "cache requires an initialized execution", kOperation);
    if (const auto stopped = execution.checkCancellation(); stopped.failed())
        return stopped;
    if (options.maximumEntries == 0u || options.maximumEntries > 256u)
        return failure(ErrorCode::InvalidArgument, "cache entry ceiling must be in [1,256]", kOperation);
    constexpr auto kEntryBytes = sizeof(State::Entry) + sizeof(DrizzleGeometryState);
    if (options.maximumBytes < sizeof(State) + kEntryBytes)
        return cacheBudgetRefusal(sizeof(State) + kEntryBytes, options.maximumBytes, options.maximumBytes);
    const auto capacity = static_cast<std::uint32_t>(
        (std::min<std::uint64_t>)(options.maximumEntries, (options.maximumBytes - sizeof(State)) / kEntryBytes));
    auto allocation = execution.memoryResource()->allocate(sizeof(State), alignof(State));
    if (!allocation.hasValue())
        return allocation.status();
    DrizzleGeometryCache result{std::construct_at(static_cast<State*>(*allocation), execution, capacity)};
    auto entries = execution.memoryResource()->allocate(sizeof(State::Entry) * capacity, alignof(State::Entry));
    if (!entries.hasValue())
        return entries.status();
    result.m_state->entries = static_cast<State::Entry*>(*entries);
    std::uninitialized_value_construct_n(result.m_state->entries, capacity);
    if (const auto stopped = execution.checkCancellation(); stopped.failed())
        return stopped;
    return result;
}
Result<DrizzleGeometryPlan> DrizzleGeometryCache::get(const ExecutionContext& execution,
                                                      const DrizzleMapDescriptor& descriptor)
{
    constexpr const char* kOperation = "an::DrizzleGeometryCache::get";
    if (m_state == nullptr || !execution.valid())
        return failure(ErrorCode::NotInitialized, "cache or execution is empty", kOperation);
    if (m_state->lease.contextId() != execution.engineContextId())
        return failure(ErrorCode::IncompatibleArguments, "cache belongs to another engine context", kOperation);
    std::lock_guard lock(m_state->mutex);
    if (const auto stopped = execution.checkCancellation(); stopped.failed())
        return stopped;
    for (std::uint32_t i = 0u; i < m_state->counts.capacity; ++i)
        if (m_state->entries[i].plan.valid() && sameDrizzleGeometry(*m_state->entries[i].plan.descriptor(), descriptor))
        {
            increment(m_state->counts.hits);
            m_state->touch(i);
            return m_state->entries[i].plan;
        }
    increment(m_state->counts.misses);
    auto prepared = DrizzleGeometryPlan::create(execution, descriptor);
    if (!prepared.hasValue())
        return prepared.status();
    std::uint32_t selected = 0u;
    for (std::uint32_t i = 0u; i < m_state->counts.capacity; ++i)
    {
        if (!m_state->entries[i].plan.valid())
        {
            selected = i;
            break;
        }
        if (m_state->entries[i].age > m_state->entries[selected].age)
            selected = i;
    }
    auto& entry = m_state->entries[selected];
    if (entry.plan.valid())
        increment(m_state->counts.evictions);
    else
    {
        ++m_state->counts.entries;
        m_state->counts.retainedBytes += prepared->residentBytes();
    }
    entry.plan = *prepared;
    entry.age = m_state->counts.capacity;
    m_state->touch(selected);
    return prepared;
}
Status DrizzleGeometryCache::clear()
{
    if (m_state == nullptr)
        return failure(ErrorCode::NotInitialized, "cache is empty", "an::DrizzleGeometryCache::clear");
    std::lock_guard lock(m_state->mutex);
    for (std::uint32_t i = 0u; i < m_state->counts.capacity; ++i)
        m_state->entries[i] = State::Entry{};
    m_state->counts.entries = 0u;
    m_state->counts.retainedBytes = sizeof(State) + sizeof(State::Entry) * m_state->counts.capacity;
    return Status::success();
}
DrizzleGeometryCacheCounters DrizzleGeometryCache::counters() const noexcept
{
    if (m_state == nullptr)
        return {};
    std::lock_guard lock(m_state->mutex);
    return m_state->counts;
}

} // namespace an
