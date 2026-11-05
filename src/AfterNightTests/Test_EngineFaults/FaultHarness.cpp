// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "FaultHarness.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <new>
#include <system_error>
#include <utility>

// Two things in this file are worth stating beside the code.
//
// **The resource takes its memory from the platform directly rather than from anything of the
// engine's.** It sits underneath an engine context, so anything it borrowed from that context would
// be a cycle, and anything it took from a second context would make a sweep's numbers depend on a
// context the scenario never mentioned.
//
// **A failed run is reported and the sweep continues.** A sweep of two hundred points that stopped
// at the first broken one would name that point and say nothing about the other hundred and
// ninety-nine, and the shape of the answer - one point broken, or every point after the fourth
// stage - is most of the diagnosis.

namespace an::faults
{
namespace
{

/// Whether a value is a power of two, which is what an alignment has to be.
///
/// @param value the alignment to test.
/// @return true when it is a positive power of two.
[[nodiscard]] bool isPowerOfTwoAlignment(std::uint64_t value) noexcept
{
    return value != 0u && (value & (value - 1u)) == 0u;
}

/// One environment variable, read the way each compiler's library wants it read.
///
/// @param name the variable.
/// @return its value, or an empty string when it is unset.
[[nodiscard]] std::string environmentValue(const char* name)
{
#ifdef _MSC_VER
    char* value = nullptr;
    std::size_t length = 0u;
    std::string copied;
    if (_dupenv_s(&value, &length, name) == 0 && value != nullptr)
    {
        copied.assign(value);
    }
    std::free(value);
    return copied;
#else
    const char* const value = std::getenv(name);
    return value != nullptr ? std::string(value) : std::string{};
#endif
}

} // namespace

const char* spell(FaultKind kind) noexcept
{
    switch (kind)
    {
    case FaultKind::Allocation:
        return "allocation";
    case FaultKind::Cancellation:
        return "cancellation";
    case FaultKind::InputOutput:
        return "io";
    case FaultKind::External:
        return "external";
    }
    return "unknown";
}

void InjectingMemoryResource::refuseAt(std::uint64_t ordinal) noexcept
{
    const std::lock_guard<std::mutex> held(m_guard);
    m_refuseAt = ordinal;
}

void InjectingMemoryResource::stopAt(std::uint64_t ordinal, std::stop_source& source) noexcept
{
    const std::lock_guard<std::mutex> held(m_guard);
    m_stopAt = ordinal;
    m_stop = &source;
}

Result<void*> InjectingMemoryResource::allocate(std::uint64_t bytes, std::uint64_t alignment)
{
    return serve(bytes, alignment, false);
}

Result<void*> InjectingMemoryResource::allocateZeroed(std::uint64_t bytes, std::uint64_t alignment)
{
    return serve(bytes, alignment, true);
}

Result<void*> InjectingMemoryResource::serve(std::uint64_t bytes, std::uint64_t alignment, bool zero)
{
    if (bytes == 0u || !isPowerOfTwoAlignment(alignment))
    {
        const std::lock_guard<std::mutex> held(m_guard);
        ++m_counters.deniedAllocations;
        return Status::failure(ErrorCode::InvalidArgument, "the injecting resource was asked for an impossible block");
    }

    bool refuse = false;
    std::stop_source* stop = nullptr;
    std::uint64_t ordinal = 0u;
    {
        const std::lock_guard<std::mutex> held(m_guard);
        ordinal = ++m_requests;
        if (m_refuseAt != 0u && ordinal == m_refuseAt)
        {
            refuse = true;
            m_fired = true;
            ++m_counters.deniedAllocations;
        }
        if (m_stopAt != 0u && ordinal == m_stopAt)
        {
            stop = m_stop;
            m_fired = true;
        }
    }
    if (refuse)
    {
        // Spelled as the platform refusing, because that is what an injected fault is pretending to
        // be: the engine must not be able to tell this apart from a machine that has run out.
        return Status::failure(ErrorCode::AllocationFailed,
                               StatusDetail{.message = "the injected fault refused this allocation",
                                            .operation = "an::faults::InjectingMemoryResource::allocate",
                                            .domain = ErrorDomain::Memory,
                                            .context = {StatusContextField::ofCount(ContextKey::Requested, bytes),
                                                        StatusContextField::ofCount(ContextKey::Completed, ordinal)}});
    }
    if (stop != nullptr)
    {
        // Before the memory is handed back, so the operation observes the stop at its own next
        // checkpoint rather than at a boundary this harness chose.
        stop->request_stop();
    }

    const auto size = static_cast<std::size_t>(bytes);
    void* const block = ::operator new(size, std::align_val_t{static_cast<std::size_t>(alignment)}, std::nothrow);
    if (block == nullptr)
    {
        const std::lock_guard<std::mutex> held(m_guard);
        ++m_counters.deniedAllocations;
        return Status::failure(ErrorCode::AllocationFailed);
    }
    if (zero)
    {
        std::fill_n(static_cast<unsigned char*>(block), size, static_cast<unsigned char>(0));
    }

    const std::lock_guard<std::mutex> held(m_guard);
    m_blocks.emplace(block, Block{bytes, alignment});
    m_counters.liveBytes += bytes;
    ++m_counters.liveAllocations;
    m_counters.totalBytes += bytes;
    ++m_counters.totalAllocations;
    m_counters.peakBytes = (std::max)(m_counters.peakBytes, m_counters.liveBytes);
    return block;
}

void InjectingMemoryResource::deallocate(void* pointer, std::uint64_t bytes, std::uint64_t alignment) noexcept
{
    if (pointer == nullptr)
    {
        return;
    }
    {
        const std::lock_guard<std::mutex> held(m_guard);
        const auto entry = m_blocks.find(pointer);
        if (entry == m_blocks.end() || entry->second.bytes != bytes || entry->second.alignment != alignment)
        {
            ++m_mismatched;
            return;
        }
        m_blocks.erase(entry);
        m_counters.liveBytes -= bytes;
        --m_counters.liveAllocations;
    }
    ::operator delete(pointer, std::align_val_t{static_cast<std::size_t>(alignment)});
}

MemoryCounters InjectingMemoryResource::counters() const noexcept
{
    const std::lock_guard<std::mutex> held(m_guard);
    return m_counters;
}

std::uint64_t InjectingMemoryResource::requests() const noexcept
{
    const std::lock_guard<std::mutex> held(m_guard);
    return m_requests;
}

bool InjectingMemoryResource::fired() const noexcept
{
    const std::lock_guard<std::mutex> held(m_guard);
    return m_fired;
}

std::uint64_t InjectingMemoryResource::outstandingBlocks() const noexcept
{
    const std::lock_guard<std::mutex> held(m_guard);
    return static_cast<std::uint64_t>(m_blocks.size());
}

std::uint64_t InjectingMemoryResource::outstandingBytes() const noexcept
{
    const std::lock_guard<std::mutex> held(m_guard);
    return m_counters.liveBytes;
}

std::uint64_t InjectingMemoryResource::mismatchedReleases() const noexcept
{
    const std::lock_guard<std::mutex> held(m_guard);
    return m_mismatched;
}

ScopedDirectory::ScopedDirectory(std::string_view name)
{
    const std::string root = environmentValue("AFTERNIGHT_TEST_STORAGE_ROOT");
    const std::filesystem::path base =
        root.empty() ? std::filesystem::temp_directory_path() : std::filesystem::path(root);
    static std::atomic<std::uint64_t> counter{0u};
    const std::uint64_t unique = counter.fetch_add(1u, std::memory_order_relaxed);
    m_path =
        base / (std::string(name) + "-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
                "-" + std::to_string(unique));
    std::error_code ignored;
    std::filesystem::create_directories(m_path, ignored);
}

ScopedDirectory::~ScopedDirectory()
{
    std::error_code ignored;
    std::filesystem::remove_all(m_path, ignored);
}

const std::filesystem::path& ScopedDirectory::path() const noexcept
{
    return m_path;
}

std::string ScopedDirectory::file(std::string_view name) const
{
    const std::u8string utf8 = (m_path / std::filesystem::path(name)).u8string();
    return {reinterpret_cast<const char*>(utf8.data()), utf8.size()};
}

void TriggerProgressSink::stopAt(std::uint64_t ordinal, std::stop_source& source) noexcept
{
    m_stopAt = ordinal;
    m_stop = &source;
}

void TriggerProgressSink::onProgress(const ProgressReport&) noexcept
{
    const std::uint64_t ordinal = m_reports.fetch_add(1u, std::memory_order_relaxed) + 1u;
    if (m_stopAt != 0u && ordinal == m_stopAt && m_stop != nullptr)
    {
        m_fired.store(true, std::memory_order_relaxed);
        m_stop->request_stop();
    }
}

std::uint64_t TriggerProgressSink::reports() const noexcept
{
    return m_reports.load(std::memory_order_relaxed);
}

bool TriggerProgressSink::fired() const noexcept
{
    return m_fired.load(std::memory_order_relaxed);
}

Sweep::Sweep(const char* label, FaultKind kind) : m_label(label), m_kind(kind) {}

Sweep::~Sweep()
{
    // The line the campaign runner reads. It is printed even for a sweep that failed, because the
    // counts are how a failure is told apart from a scenario that never reached its subject.
    std::printf("an-fault-scenario label=%s kind=%s points=%llu runs=%llu reached=%llu refused=%llu "
                "absorbed=%llu seconds=%.2f\n",
                m_label, spell(m_kind), static_cast<unsigned long long>(m_points),
                static_cast<unsigned long long>(m_runs), static_cast<unsigned long long>(m_reached),
                static_cast<unsigned long long>(m_refused), static_cast<unsigned long long>(m_absorbed),
                static_cast<double>(m_elapsed.count()) / 1000000.0);
    std::fflush(stdout);

    if (!m_ran)
    {
        ADD_FAILURE() << m_label << " declared a sweep and ran nothing";
        return;
    }
    if (m_points == 0u)
    {
        ADD_FAILURE() << m_label << " found no injection point, so it proved nothing about " << spell(m_kind);
    }
    if (m_reached == 0u)
    {
        ADD_FAILURE() << m_label << " swept " << m_points << " point(s) and not one injection fired";
    }
}

void Sweep::engineOptions(EngineContextOptions options)
{
    m_options = std::move(options);
}

void Sweep::pointsFrom(PointSource source) noexcept
{
    m_source = source;
}

void Sweep::accept(ErrorCode code)
{
    m_accepted.push_back(code);
}

void Sweep::destination(std::filesystem::path path)
{
    m_destination = std::move(path);
}

void Sweep::repeat(std::uint32_t times) noexcept
{
    m_repeat = times == 0u ? 1u : times;
}

void Sweep::runBound(std::chrono::milliseconds bound) noexcept
{
    m_bound = bound;
}

void Sweep::run(const Subject& subject)
{
    m_ran = true;

    const RunOutcome census = once(subject, 0u);
    if (census.status.failed())
    {
        ADD_FAILURE() << m_label << ": the census run must succeed before anything is injected, and it answered "
                      << errorCodeName(census.status.code()) << ": " << census.status.message();
        return;
    }
    m_points = census.opportunities;
    if (m_kind == FaultKind::Cancellation)
    {
        // One more point than the census found, and it is the first one: a stop that was already
        // requested before the operation was called at all. Cancellation is injected before start as
        // well as during, because it is the point most likely to be missing a check -
        // an operation that reserves before it first looks at its token does its work anyway.
        ++m_points;
    }
    if (m_points == 0u)
    {
        return;
    }

    for (std::uint64_t point = 1u; point <= m_points; ++point)
    {
        for (std::uint32_t attempt = 0u; attempt < m_repeat; ++attempt)
        {
            ++m_runs;
            const RunOutcome injected = once(subject, point);
            if (!injected.fired)
            {
                // Not a failure on its own: an operation that took a shorter path after an earlier
                // decision may have fewer allocations than the census found. It is counted, and the
                // report line is where a sweep that reached almost nothing becomes visible.
                continue;
            }
            ++m_reached;
            if (injected.status.ok())
            {
                ++m_absorbed;
            }
            else if (accepted(injected.status))
            {
                ++m_refused;
            }
        }
    }
}

RunOutcome Sweep::once(const Subject& subject, std::uint64_t point)
{
    if (!m_destination.empty())
    {
        // Every run starts with an empty directory, including the one after the census published a
        // whole file. Without this the residue check would be about the run before it.
        std::error_code ignored;
        std::filesystem::remove_all(m_destination.parent_path(), ignored);
        std::filesystem::create_directories(m_destination.parent_path(), ignored);
    }

    InjectingMemoryResource memory;
    std::stop_source stop;
    TriggerProgressSink progress;

    EngineContextOptions options = m_options;
    options.memoryResource = &memory;
    Result<EngineContext> engine = EngineContext::create(options);
    if (!engine.hasValue())
    {
        ADD_FAILURE() << m_label << ": the engine context could not be built over the injecting resource: "
                      << engine.status().message();
        return RunOutcome{engine.status(), 0u, false};
    }

    bool stoppedBeforeStart = false;
    if (point != 0u)
    {
        if (m_kind == FaultKind::Allocation)
        {
            memory.refuseAt(point);
        }
        else if (m_kind == FaultKind::Cancellation)
        {
            if (point == 1u)
            {
                stop.request_stop();
                stoppedBeforeStart = true;
            }
            else if (m_source == PointSource::Allocations)
            {
                memory.stopAt(point - 1u, stop);
            }
            else
            {
                progress.stopAt(point - 1u, stop);
            }
        }
    }

    RunContext run{*engine, memory, stop, progress, point, 0u, false};
    Status status{};
    const auto started = std::chrono::steady_clock::now();
    try
    {
        status = subject(run);
    }
    catch (const std::exception& error)
    {
        ADD_FAILURE() << m_label << " at point " << point
                      << ": an expected failure escaped as an exception: " << error.what();
    }
    catch (...)
    {
        ADD_FAILURE() << m_label << " at point " << point << ": an expected failure escaped as an exception";
    }
    const auto span = std::chrono::steady_clock::now() - started;
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(span);
    m_elapsed += std::chrono::duration_cast<std::chrono::microseconds>(span);

    RunOutcome outcome{status, run.opportunities, run.fired};
    if (point == 0u && outcome.opportunities == 0u)
    {
        // The subject did not enumerate its own points, so the harness enumerates them for it.
        outcome.opportunities = m_source == PointSource::Allocations ? memory.requests() : progress.reports();
    }
    if (point != 0u && !outcome.fired)
    {
        outcome.fired =
            stoppedBeforeStart ||
            (m_kind == FaultKind::Cancellation && m_source == PointSource::ProgressReports ? progress.fired()
                                                                                           : memory.fired());
    }

    check(point, status, memory, elapsed);
    return outcome;
}

void Sweep::check(std::uint64_t point, const Status& status, const InjectingMemoryResource& memory,
                  std::chrono::milliseconds elapsed)
{
    const std::string where = std::string(m_label) + " at point " + std::to_string(point);

    // Nothing leaked. The subject destroyed everything it built before returning, so every block
    // this resource handed out has come back - including on the path the injection created.
    EXPECT_EQ(memory.outstandingBlocks(), 0u)
        << where << ": " << memory.outstandingBytes() << " byte(s) were never given back";
    EXPECT_EQ(memory.mismatchedReleases(), 0u) << where
                                               << ": a block was released with a size or an address the "
                                                  "resource never handed out";

    // No deadlock. A run that outlasts its bound is a finding even when it eventually returned,
    // because what a bounded queue does when a producer fails is exactly what this sweep is about.
    EXPECT_LE(elapsed, m_bound) << where << ": the run took " << elapsed.count() << " ms, over its " << m_bound.count()
                                << " ms bound";

    // An operation given a stop that was already requested before it was called must refuse. Later
    // points may legitimately finish before they observe the stop - that is what `absorbed` counts -
    // but an operation that never looks at its token at all would pass every one of those and fail
    // only here, which is why this point is checked rather than counted.
    if (point == 1u && m_kind == FaultKind::Cancellation && status.ok())
    {
        ADD_FAILURE() << where << ": the stop was requested before the operation was called and it ran to "
                      << "completion anyway";
    }

    // The failure is one the scenario expects, rather than whatever fell out.
    if (point != 0u && status.failed() && !accepted(status))
    {
        ADD_FAILURE() << where << ": answered " << errorCodeName(status.code()) << " (root "
                      << errorCodeName(status.rootCause().code())
                      << "), which this scenario does not accept: " << status.message();
    }

    // No half-commit. A destination holds a whole file or no file, and nothing is left beside it.
    if (!m_destination.empty())
    {
        std::error_code ignored;
        const bool exists = std::filesystem::exists(m_destination, ignored);
        if (status.ok())
        {
            EXPECT_TRUE(exists) << where << ": the run succeeded and published nothing";
        }
        else
        {
            EXPECT_FALSE(exists) << where << ": the run failed and the destination exists anyway";
        }
        const std::vector<std::string> left = residue();
        EXPECT_TRUE(left.empty()) << where << ": " << left.size() << " file(s) were left beside the destination, the "
                                  << "first of them " << (left.empty() ? std::string{} : left.front());
    }
}

bool Sweep::accepted(const Status& status) const
{
    const ErrorCode code = status.code();
    const ErrorCode root = status.rootCause().code();
    return std::any_of(m_accepted.begin(), m_accepted.end(),
                       [code, root](ErrorCode accepted) { return accepted == code || accepted == root; });
}

std::vector<std::string> Sweep::residue() const
{
    std::vector<std::string> names;
    std::error_code ignored;
    const std::filesystem::path directory = m_destination.parent_path();
    for (const auto& entry : std::filesystem::directory_iterator(directory, ignored))
    {
        if (entry.path() != m_destination)
        {
            names.push_back(entry.path().filename().string());
        }
    }
    std::sort(names.begin(), names.end());
    return names;
}

std::uint64_t Sweep::points() const noexcept
{
    return m_points;
}

std::uint64_t Sweep::reached() const noexcept
{
    return m_reached;
}

std::uint64_t Sweep::runs() const noexcept
{
    return m_runs;
}

std::uint64_t Sweep::refused() const noexcept
{
    return m_refused;
}

std::uint64_t Sweep::absorbed() const noexcept
{
    return m_absorbed;
}

} // namespace an::faults
