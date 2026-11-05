// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the persistent developer dump reader.
//
// A reader copies its path into memory taken from the engine context, holds a lease on that context,
// and keeps one dump session open. Opening and every read run as one scheduled call inside the
// execution's task arena, so the transfer loop's cancellation checks and any nested work follow the
// operation's grant.

#include <AfterNightEngine/IO/DumpReader.h>

#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <AfterNightEngine/Runtime/MemoryResource.h>

#include "io/dump/DumpReaderSession.h"
#include "runtime/ScheduledCall.h"

#include <cstring>
#include <filesystem>
#include <memory>
#include <span>
#include <utility>

namespace an
{
struct DumpReader::Impl
{
    EngineContextLease lease;
    MemoryResource* memory = nullptr;
    std::span<char> path;
    std::unique_ptr<DumpReaderSession> read;
    ~Impl()
    {
        read.reset();
        if (memory)
            memory->deallocate(path.data(), path.size(), alignof(char));
    }
};
DumpReader::DumpReader() noexcept = default;
DumpReader::~DumpReader() = default;
DumpReader::DumpReader(DumpReader&& other) noexcept = default;
DumpReader& DumpReader::operator=(DumpReader&& other) noexcept = default;
Result<DumpReader> DumpReader::open(const EngineContext& engine, const ExecutionContext& execution,
                                    std::string_view path)
{
    return scheduled_detail::guarded([&]() -> Result<DumpReader> {
        if (!engine.valid() || !execution.valid())
            return Status::failure(ErrorCode::NotInitialized);
        if (engine.id() != execution.engineContextId())
            return Status::failure(ErrorCode::IncompatibleArguments);
        if (path.empty() || path.size() > 131072u || path.find('\0') != std::string_view::npos ||
            !std::filesystem::path(std::u8string_view(reinterpret_cast<const char8_t*>(path.data()), path.size()))
                 .is_absolute())
            return Status::failure(ErrorCode::InvalidArgument);
        if (auto status = execution.checkCancellation(); status.failed())
            return status;
        auto lease = engine.acquireLease();
        if (!lease)
            return lease.status();
        DumpReader reader;
        reader.m_impl = std::make_unique<Impl>();
        reader.m_impl->lease = std::move(*lease);
        reader.m_impl->memory = engine.memoryResource();
        auto allocation = reader.m_impl->memory->allocate(path.size(), alignof(char));
        if (!allocation)
            return allocation.status();
        reader.m_impl->path = {static_cast<char*>(*allocation), path.size()};
        std::memcpy(reader.m_impl->path.data(), path.data(), path.size());
        bool entered = false;
        auto opened = scheduled_detail::call(execution, entered, false,
                                             [&] { return openDumpReaderSession(engine, execution, path); });
        if (!opened)
            return opened.status();
        if (auto status = (*opened)->validateSize(path); status.failed())
            return status;
        reader.m_impl->read = std::move(*opened);
        return reader;
    });
}
const CodecImageInfo* DumpReader::info() const noexcept
{
    return m_impl ? &m_impl->read->info() : nullptr;
}
std::string_view DumpReader::path() const noexcept
{
    return m_impl ? std::string_view(m_impl->path.data(), m_impl->path.size()) : std::string_view{};
}
Result<CodecDecodeResult> DumpReader::read(const EngineContext& engine, const ExecutionContext& execution,
                                           const CodecRegion* region)
{
    return scheduled_detail::guarded([&]() -> Result<CodecDecodeResult> {
        if (!m_impl)
            return Status::failure(ErrorCode::NotInitialized);
        if (!engine.valid() || engine.id() != m_impl->lease.contextId() || !execution.valid() ||
            execution.engineContextId() != engine.id())
            return Status::failure(ErrorCode::IncompatibleArguments);
        bool entered = false;
        return scheduled_detail::call(execution, entered, false, [&] {
            return m_impl->read->decode(engine, execution, {.path = path(), .format = CodecFormat::Dump}, region);
        });
    });
}
DumpIoCounters DumpReader::snapshot() const noexcept
{
    if (!m_impl)
        return {};
    auto counters = m_impl->read->snapshot();
    counters.handleOpens = 1u;
    counters.currentOpenHandles = 1u;
    counters.peakOpenHandles = 1u;
    counters.headerValidations = 1u;
    return counters;
}

} // namespace an
