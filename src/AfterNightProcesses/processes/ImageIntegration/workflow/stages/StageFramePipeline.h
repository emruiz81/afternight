// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightProcessesExport.h"

/// @file
/// A stage's frames overlapped: the next frame read while this one is computed, and the last one written while this
/// one is computed, each on a thread of its own in an operation of its own.
///
/// The registration and the alignment read, calibrate and - for the alignment - write every light, and most of that is
/// one thread's work: a codec decodes and encodes a file on the thread that asks it, and a flush waits for the device.
/// Done one after the other inside one operation as wide as the run, the run's other threads wait through all of it.
/// The pipeline reads one light ahead and writes one behind, through the session: the run's width is divided between a
/// reader, the stage's own operation and its writers (`pipelineWidths`), so the three together never use more threads
/// than the person allowed, and each admits its operations at its share.
///
/// `ReadAhead` produces the items of a list in order on its thread, at most a stated number ahead of the stage, and
/// hands them over in order. `WriteBehind` takes items the stage is finished with and consumes them on its threads,
/// at most a stated number held at once, in whatever order they finish; the consumer records each outcome under its
/// item's own index. Neither decides anything about a failure: an item carries its outcome, and the stage reads it.
/// Destroying either stops it and waits for the item in hand - a read or a write is never abandoned half done.

#include <algorithm>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <exception>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <stop_token>
#include <thread>
#include <utility>
#include <vector>

namespace stage_utils
{

/// How a pipelined stage divides the run's width between its reader, its own operation and its writers.
struct PipelineWidths
{
    /// Whether the run is wide enough to overlap anything; at three threads or fewer every frame is read, computed
    /// and written in one operation, as wide as the run.
    bool overlapped = false;
    /// The width of each read the reader admits.
    std::uint32_t reader = 0u;
    /// The width of the stage's own operation per frame.
    std::uint32_t compute = 0u;
    /// How many writers run at once, each admitting writes one thread wide.
    std::uint32_t writers = 0u;
};

/// Divides a run's width: a reader of an eighth of it (one to four threads), one writer - two from sixteen threads -
/// when the stage writes, and the rest for the stage's own operation.
///
/// @param runWidth the threads the run may use.
/// @param writes whether the stage writes its frames.
/// @return the division, `overlapped` false below four threads.
[[nodiscard]] AFTERNIGHTPROCESSES_API PipelineWidths pipelineWidths(std::uint32_t runWidth, bool writes) noexcept;

/// Produces the items of a list on threads of its own, at most a stated number ahead of the one taking them, and hands
/// them over in order.
///
/// One producer reads ahead. Several suit a step that cannot use the threads it is given - each producer claims the
/// next index, and an item finished out of order waits for the ones before it.
///
/// @tparam ItemT what one index produces; movable and default-constructible, a default item standing for none. It
///         carries its own outcome.
template <typename ItemT> class ReadAhead
{
public:
    /// Produces the item of one index; called on a producer's thread, once per index. With one producer the indices
    /// come in order; with several, each producer takes the next one not yet claimed. The token stops when the
    /// read-ahead is destroyed early; a produce that sees it may return an item that says so.
    using Produce = std::function<ItemT(std::size_t index, std::stop_token stop)>;

    /// Starts producing on one thread.
    ///
    /// @param count how many items the list has.
    /// @param depth how many produced items may wait to be taken; at least one.
    /// @param produce what produces one.
    ReadAhead(std::size_t count, std::size_t depth, Produce produce) : ReadAhead(count, depth, 1u, std::move(produce))
    {
    }

    /// Starts producing on `producers` threads; at most `depth + producers - 1` items are produced or being produced
    /// ahead of the one taking them.
    ///
    /// @param count how many items the list has.
    /// @param depth how many produced items may wait to be taken; at least one.
    /// @param producers how many items are produced at once; at least one.
    /// @param produce what produces one.
    ReadAhead(std::size_t count, std::size_t depth, std::size_t producers, Produce produce)
        : m_itemCount(count), m_ahead((std::max)(std::size_t{1}, depth) + (std::max)(std::size_t{1}, producers) - 1u),
          m_produce(std::move(produce))
    {
        const std::size_t threads = (std::max)(std::size_t{1}, producers);
        m_workers.reserve(threads);
        for (std::size_t index = 0u; index < threads; ++index)
        {
            m_workers.emplace_back([this](std::stop_token stop) { run(stop); });
        }
    }

    /// Stops producing and waits for the items in hand.
    ~ReadAhead()
    {
        for (std::jthread& worker : m_workers)
        {
            worker.request_stop();
        }
        m_changed.notify_all();
    }

    ReadAhead(const ReadAhead&) = delete;
    ReadAhead& operator=(const ReadAhead&) = delete;

    /// The next item in order, waiting for it to be produced. Rethrows what producing an item threw.
    ///
    /// @return the item; a default item once every index has been taken.
    [[nodiscard]] ItemT take()
    {
        std::unique_lock lock(m_mutex);
        return takeLocked(lock);
    }

    /// The item of `index`, waiting until every item before it has been taken and it has been produced: for consumers
    /// on several threads, each knowing the index it takes. Rethrows what producing an item threw.
    ///
    /// @param index the item's index.
    /// @return the item; a default item for an index past the list.
    [[nodiscard]] ItemT take(std::size_t index)
    {
        std::unique_lock lock(m_mutex);
        m_changed.wait(lock, [this, index] { return m_taken >= index || m_failure != nullptr; });
        if (m_taken != index)
        {
            if (m_failure != nullptr)
            {
                std::rethrow_exception(m_failure);
            }
            return ItemT{};
        }
        return takeLocked(lock);
    }

private:
    ItemT takeLocked(std::unique_lock<std::mutex>& lock)
    {
        m_changed.wait(lock, [this] {
            return m_taken >= m_itemCount || m_failure != nullptr || m_ready.find(m_taken) != m_ready.end();
        });
        const auto found = m_ready.find(m_taken);
        if (found == m_ready.end())
        {
            if (m_failure != nullptr && m_taken < m_itemCount)
            {
                std::rethrow_exception(m_failure);
            }
            return ItemT{};
        }
        ItemT item = std::move(found->second);
        m_ready.erase(found);
        ++m_taken;
        lock.unlock();
        m_changed.notify_all();
        return item;
    }

    void run(std::stop_token stop)
    {
        for (;;)
        {
            std::size_t index = 0u;
            {
                std::unique_lock lock(m_mutex);
                m_changed.wait(lock, stop, [this] { return m_next >= m_itemCount || m_next < m_taken + m_ahead; });
                if (stop.stop_requested() || m_next >= m_itemCount || m_failure != nullptr)
                {
                    return;
                }
                index = m_next++;
            }
            try
            {
                ItemT item = m_produce(index, stop);
                {
                    const std::lock_guard lock(m_mutex);
                    m_ready.emplace(index, std::move(item));
                }
            }
            catch (...)
            {
                {
                    const std::lock_guard lock(m_mutex);
                    if (m_failure == nullptr)
                    {
                        m_failure = std::current_exception();
                    }
                }
                m_changed.notify_all();
                return;
            }
            m_changed.notify_all();
        }
    }

    const std::size_t m_itemCount;
    const std::size_t m_ahead;
    Produce m_produce;
    std::mutex m_mutex;
    std::condition_variable_any m_changed;
    std::map<std::size_t, ItemT> m_ready;
    std::size_t m_next = 0u;
    std::size_t m_taken = 0u;
    std::exception_ptr m_failure;
    // Last, so that they start once everything they read exists and are joined before any of it goes.
    std::vector<std::jthread> m_workers;
};

/// Consumes items the stage hands over on threads of its own, at most `capacity` held at once.
///
/// @tparam ItemT what is written; movable.
template <typename ItemT> class WriteBehind
{
public:
    /// Consumes one item; called on one of the writers' threads, for every item submitted, in no stated order. It must
    /// not throw.
    using Consume = std::function<void(ItemT&& item)>;

    /// Starts the writers.
    ///
    /// @param writers how many items are consumed at once; at least one.
    /// @param capacity how many submitted items may be held at once, waiting or being consumed; at least `writers`.
    /// @param consume what consumes one.
    WriteBehind(std::size_t writers, std::size_t capacity, Consume consume)
        : m_capacity((std::max)((std::max)(std::size_t{1}, writers), capacity)), m_consume(std::move(consume))
    {
        const std::size_t count = (std::max)(std::size_t{1}, writers);
        m_workers.reserve(count);
        for (std::size_t index = 0u; index < count; ++index)
        {
            m_workers.emplace_back([this] { run(); });
        }
    }

    /// Consumes everything submitted, then stops the writers.
    ~WriteBehind()
    {
        {
            const std::lock_guard lock(m_mutex);
            m_closing = true;
        }
        m_changed.notify_all();
        for (std::thread& worker : m_workers)
        {
            worker.join();
        }
    }

    WriteBehind(const WriteBehind&) = delete;
    WriteBehind& operator=(const WriteBehind&) = delete;

    /// Hands an item over, waiting while the write-behind holds as many as it may.
    ///
    /// @param item the item.
    void submit(ItemT item)
    {
        {
            std::unique_lock lock(m_mutex);
            m_changed.wait(lock, [this] { return m_queued.size() + m_inFlight < m_capacity; });
            m_queued.push_back(std::move(item));
        }
        m_changed.notify_all();
    }

    /// Waits until every item submitted so far has been consumed.
    void drain()
    {
        std::unique_lock lock(m_mutex);
        m_changed.wait(lock, [this] { return m_queued.empty() && m_inFlight == 0u; });
    }

private:
    void run()
    {
        for (;;)
        {
            std::optional<ItemT> item;
            {
                std::unique_lock lock(m_mutex);
                m_changed.wait(lock, [this] { return !m_queued.empty() || m_closing; });
                if (m_queued.empty())
                {
                    return;
                }
                item.emplace(std::move(m_queued.front()));
                m_queued.pop_front();
                ++m_inFlight;
            }
            m_consume(std::move(*item));
            {
                const std::lock_guard lock(m_mutex);
                --m_inFlight;
            }
            m_changed.notify_all();
        }
    }

    const std::size_t m_capacity;
    Consume m_consume;
    std::mutex m_mutex;
    std::condition_variable m_changed;
    std::deque<ItemT> m_queued;
    std::size_t m_inFlight = 0u;
    bool m_closing = false;
    // Last, so that they start once everything they read exists.
    std::vector<std::thread> m_workers;
};

} // namespace stage_utils
