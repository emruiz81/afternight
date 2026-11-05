// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the bounded pool of persistent dump readers and the leases it hands out.
//
// The pool's state is shared between the pool and its leases, so a lease outlives a drained or
// destroyed pool and still returns its slot. One mutex guards the slots and the totals; a file is
// opened outside it, in a slot already marked leased, so a slow open blocks no other acquire. A slot
// is chosen as the idle reader of the same path, or else the idle slot with the oldest release. The
// totals keep what retired readers transferred, and each open reader's own counters are added on top,
// less the baseline a reset took, when a snapshot is read.

#include <AfterNightEngine/IO/DumpReader.h>

#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <AfterNightEngine/Runtime/MemoryResource.h>

#include "runtime/ScheduledCall.h"

#include <algorithm>
#include <condition_variable>
#include <limits>
#include <mutex>
#include <span>
#include <utility>

namespace an
{
namespace
{
void add(std::uint64_t& total, std::uint64_t count) noexcept
{
    total += (std::min)(count, (std::numeric_limits<std::uint64_t>::max)() - total);
}
void addReads(DumpIoCounters& destination, const DumpIoCounters& source, const DumpIoCounters& baseline) noexcept
{
    add(destination.positionedReads, source.positionedReads - baseline.positionedReads);
    add(destination.fullWidthStripReads, source.fullWidthStripReads - baseline.fullWidthStripReads);
    add(destination.rowReads, source.rowReads - baseline.rowReads);
    add(destination.payloadBytes, source.payloadBytes - baseline.payloadBytes);
}
} // namespace

class DumpReaderPoolState
{
public:
    struct Slot
    {
        DumpReader reader;
        DumpIoCounters baseline{};
        std::uint64_t age = 0u;
        bool leased = false;
    };
    explicit DumpReaderPoolState(EngineContextLease lease, MemoryResource& memory)
        : lease(std::move(lease)), memory(memory)
    {
    }
    ~DumpReaderPoolState()
    {
        std::destroy_n(slots.data(), slots.size());
        memory.deallocate(slots.data(), slots.size_bytes(), alignof(Slot));
    }
    Status allocate(std::uint32_t count)
    {
        auto result = memory.allocate(std::uint64_t{count} * sizeof(Slot), alignof(Slot));
        if (!result)
            return result.status();
        slots = {static_cast<Slot*>(*result), count};
        std::uninitialized_value_construct_n(slots.data(), slots.size());
        return Status::success();
    }
    void retire(Slot& slot) noexcept
    {
        if (!slot.reader.info())
            return;
        addReads(counters, slot.reader.snapshot(), slot.baseline);
        slot.reader = {};
        slot.baseline = {};
        add(counters.handleCloses, 1u);
        --counters.currentOpenHandles;
    }
    void release(std::uint32_t index) noexcept
    {
        {
            const std::lock_guard lock(mutex);
            auto& slot = slots[index];
            slot.leased = false;
            add(clock, 1u);
            slot.age = clock;
            if (drained)
                retire(slot);
        }
        changed.notify_all();
    }
    void drain() noexcept
    {
        {
            const std::lock_guard lock(mutex);
            drained = true;
            for (auto& slot : slots)
                if (!slot.leased)
                    retire(slot);
        }
        changed.notify_all();
    }

    EngineContextLease lease;
    MemoryResource& memory;
    std::span<Slot> slots;
    std::mutex mutex;
    std::condition_variable_any changed;
    DumpIoCounters counters{};
    std::uint64_t clock = 0u;
    bool drained = false;
};

DumpReaderLease::DumpReaderLease() noexcept = default;
DumpReaderLease::~DumpReaderLease()
{
    if (m_state)
        m_state->release(m_slot);
}
DumpReaderLease::DumpReaderLease(DumpReaderLease&& other) noexcept = default;
DumpReaderLease& DumpReaderLease::operator=(DumpReaderLease&& other) noexcept
{
    if (this != &other)
    {
        if (m_state)
            m_state->release(m_slot);
        m_state = std::move(other.m_state);
        m_slot = other.m_slot;
    }
    return *this;
}
DumpReader* DumpReaderLease::reader() const noexcept
{
    return m_state ? &m_state->slots[m_slot].reader : nullptr;
}
DumpReaderPool::DumpReaderPool() noexcept = default;
DumpReaderPool::~DumpReaderPool()
{
    (void)drain();
}
DumpReaderPool::DumpReaderPool(DumpReaderPool&& other) noexcept = default;
DumpReaderPool& DumpReaderPool::operator=(DumpReaderPool&& other) noexcept
{
    if (this != &other)
    {
        (void)drain();
        m_state = std::move(other.m_state);
    }
    return *this;
}
Result<DumpReaderPool> DumpReaderPool::create(const EngineContext& engine, std::uint32_t maxOpenHandles)
{
    return scheduled_detail::guarded([&]() -> Result<DumpReaderPool> {
        if (!engine.valid())
            return Status::failure(ErrorCode::NotInitialized);
        if (maxOpenHandles == 0u || maxOpenHandles > 4096u)
            return Status::failure(ErrorCode::InvalidArgument);
        auto lease = engine.acquireLease();
        if (!lease)
            return lease.status();
        DumpReaderPool pool;
        pool.m_state = std::make_shared<DumpReaderPoolState>(std::move(*lease), *engine.memoryResource());
        if (auto status = pool.m_state->allocate(maxOpenHandles); status.failed())
            return status;
        return pool;
    });
}
Result<DumpReaderLease> DumpReaderPool::acquire(const EngineContext& engine, const ExecutionContext& execution,
                                                std::string_view path)
{
    return scheduled_detail::guarded([&]() -> Result<DumpReaderLease> {
        if (!m_state)
            return Status::failure(ErrorCode::NotInitialized);
        const auto state = m_state;
        if (!engine.valid() || !execution.valid() || engine.id() != state->lease.contextId() ||
            engine.id() != execution.engineContextId())
            return Status::failure(ErrorCode::IncompatibleArguments);
        if (auto status = execution.checkCancellation(); status.failed())
            return status;
        std::unique_lock lock(state->mutex);
        auto available = [&] {
            return state->drained ||
                   std::any_of(state->slots.begin(), state->slots.end(), [](const auto& slot) { return !slot.leased; });
        };
        if (!available())
        {
            if (execution.deadline())
                state->changed.wait_until(lock, execution.stopToken(), *execution.deadline(), available);
            else
                state->changed.wait(lock, execution.stopToken(), available);
        }
        if (auto status = execution.checkCancellation(); status.failed())
            return status;
        if (state->drained)
            return Status::failure(ErrorCode::InvalidState);
        std::size_t index = state->slots.size();
        for (std::size_t i = 0u; i < state->slots.size(); ++i)
        {
            const auto& slot = state->slots[i];
            if (slot.leased)
                continue;
            if (slot.reader.info() && slot.reader.path() == path)
            {
                index = i;
                break;
            }
            if (index == state->slots.size() || slot.age < state->slots[index].age)
                index = i;
        }
        auto& slot = state->slots[index];
        const bool hit = slot.reader.info() && slot.reader.path() == path;
        if (!hit)
        {
            if (slot.reader.info())
            {
                state->retire(slot);
                add(state->counters.poolEvictions, 1u);
            }
            add(state->counters.poolMisses, 1u);
        }
        else
            add(state->counters.poolHits, 1u);
        slot.leased = true;
        // Even an exception/failure while opening returns this reserved slot to the pool.
        DumpReaderLease held;
        held.m_state = state;
        held.m_slot = static_cast<std::uint32_t>(index);
        lock.unlock();
        if (!hit)
        {
            auto opened = DumpReader::open(engine, execution, path);
            if (!opened)
                return opened.status();
            lock.lock();
            slot.reader = std::move(*opened);
            add(state->counters.handleOpens, 1u);
            add(state->counters.headerValidations, 1u);
            ++state->counters.currentOpenHandles;
            state->counters.peakOpenHandles =
                (std::max)(state->counters.peakOpenHandles, state->counters.currentOpenHandles);
            const bool drained = state->drained;
            lock.unlock();
            if (drained)
                return Status::failure(ErrorCode::InvalidState);
        }
        return held;
    });
}
Status DumpReaderPool::drain() noexcept
{
    if (m_state)
        m_state->drain();
    return Status::success();
}
std::uint32_t DumpReaderPool::maxOpenHandles() const noexcept
{
    return m_state ? static_cast<std::uint32_t>(m_state->slots.size()) : 0u;
}
DumpIoCounters DumpReaderPool::snapshot() const noexcept
{
    if (!m_state)
        return {};
    const std::lock_guard lock(m_state->mutex);
    auto counters = m_state->counters;
    for (const auto& slot : m_state->slots)
        if (slot.reader.info())
            addReads(counters, slot.reader.snapshot(), slot.baseline);
    return counters;
}
Status DumpReaderPool::resetCounters() noexcept
{
    if (!m_state)
        return Status::failure(ErrorCode::NotInitialized);
    const std::lock_guard lock(m_state->mutex);
    for (const auto& slot : m_state->slots)
        if (slot.leased)
            return Status::failure(ErrorCode::Busy);
    const auto open = m_state->counters.currentOpenHandles;
    m_state->counters = {.handleOpens = open, .currentOpenHandles = open, .peakOpenHandles = open};
    for (auto& slot : m_state->slots)
        slot.baseline = slot.reader.snapshot();
    return Status::success();
}
} // namespace an
