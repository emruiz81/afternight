// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the sequence output: reserving a pending payload for a sink, handing it to
// one writer, and proving, flushing and publishing it.
//
// The output and its writer's claim share one small link whose holder is changed only by atomic
// exchanges, so either side may let go first, on any thread: whichever of them lets go last is the one
// that removes an unpublished payload, and a payload a writer still holds open is never removed or
// renamed under it.

#include <AfterNightEngine/IO/SequenceOutput.h>

#include <AfterNightEngine/Core/ExceptionBoundary.h>
#include <AfterNightEngine/IO/CodecError.h>
#include <AfterNightEngine/IO/PendingOutput.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include "core/Utf8.h"
#include "io/output/FileSystem.h"

#include <atomic>
#include <filesystem>
#include <memory>
#include <string>
#include <system_error>
#include <type_traits>
#include <utility>
#include <vector>

namespace an
{

/// Who holds a pending payload right now.
enum class PayloadHolder : std::uint8_t
{
    Output, ///< No writer holds it; the output may be claimed, committed or aborted.
    Writer, ///< A writer holds it; the output refuses to commit and leaves removal to the writer.
    Completed, ///< A writer finished and closed it; the output may commit it.
    Abandoned, ///< The output ended while a writer held it; the writer removes it on release.
    Closed ///< The output committed or removed it; nothing more happens.
};

/// The hand-over between an output and its one writer. Shared, because either may let go first.
class SequenceOutputLink
{
public:
    std::atomic<PayloadHolder> holder{PayloadHolder::Output};
    std::string pending;
    std::filesystem::path pendingPath;
};

/// Everything a reserved output holds. Defined only here; the public handle owns it.
class SequenceOutputState
{
public:
    EngineContextLease lease;
    std::uint64_t contextId = 0u;
    std::string destination;
    std::string pending;
    std::filesystem::path destinationPath;
    std::filesystem::path pendingPath;
    std::shared_ptr<SequenceOutputLink> link;
    OutputDurability durability = OutputDurability::FlushBeforePublish;
    bool destinationExisted = false;
    bool committed = false;
    bool aborted = false;
};

namespace
{
Status outputFailure(ErrorCode code, CodecFailure reason, const char* operation, const char* message,
                     std::string_view path, std::vector<StatusContextField> extra = {})
{
    std::vector<StatusContextField> context{
        StatusContextField::ofCount(ContextKey::CodecReason, static_cast<std::uint64_t>(reason)),
        StatusContextField::ofText(ContextKey::Path, std::string(path))};
    for (auto& field : extra)
        context.push_back(std::move(field));
    return Status::failure(
        code, {.message = message, .operation = operation, .domain = ErrorDomain::Io, .context = std::move(context)});
}

Status argumentFailure(const char* operation, const char* message, const char* parameter)
{
    return Status::failure(ErrorCode::InvalidArgument,
                           {.message = message,
                            .operation = operation,
                            .domain = ErrorDomain::Io,
                            .context = {StatusContextField::ofText(ContextKey::Parameter, parameter)}});
}

Status stateFailure(const char* operation, const char* message)
{
    return Status::failure(ErrorCode::InvalidState,
                           {.message = message, .operation = operation, .domain = ErrorDomain::Io});
}

template <typename CallableT> auto guarded(const char* operation, CallableT&& body) -> std::invoke_result_t<CallableT>
{
    auto result = evaluateAcrossBoundary({"SequenceOutput", operation, ErrorDomain::Io}, std::forward<CallableT>(body));
    if (!result.hasValue())
        return result.status();
    return std::move(*result);
}

/// Ends the output without publishing. The pending payload goes now, or with its writer when one still
/// holds it, and the lease is released either way.
void abandon(SequenceOutputState& state) noexcept
{
    if (state.committed || state.aborted)
        return;
    PayloadHolder expected = PayloadHolder::Writer;
    if (!state.link->holder.compare_exchange_strong(expected, PayloadHolder::Abandoned))
    {
        // No writer holds the payload, and only this output can hand it to one, so nothing can claim it
        // between the exchange above and the removal below.
        state.link->holder.store(PayloadHolder::Closed);
        std::error_code ignored;
        (void)std::filesystem::remove(state.pendingPath, ignored);
    }
    state.lease.release();
    state.aborted = true;
}
} // namespace

SequenceOutputClaim::SequenceOutputClaim() noexcept = default;

SequenceOutputClaim::SequenceOutputClaim(std::shared_ptr<SequenceOutputLink> link) noexcept : m_link(std::move(link)) {}

SequenceOutputClaim::~SequenceOutputClaim()
{
    release();
}

SequenceOutputClaim::SequenceOutputClaim(SequenceOutputClaim&& other) noexcept : m_link(std::move(other.m_link)) {}

SequenceOutputClaim& SequenceOutputClaim::operator=(SequenceOutputClaim&& other) noexcept
{
    if (this != &other)
    {
        release();
        m_link = std::move(other.m_link);
    }
    return *this;
}

bool SequenceOutputClaim::held() const noexcept
{
    return m_link != nullptr;
}

std::string_view SequenceOutputClaim::pendingPath() const noexcept
{
    return m_link ? std::string_view(m_link->pending) : std::string_view{};
}

void SequenceOutputClaim::complete() noexcept
{
    if (!m_link)
        return;
    PayloadHolder expected = PayloadHolder::Writer;
    (void)m_link->holder.compare_exchange_strong(expected, PayloadHolder::Completed);
}

void SequenceOutputClaim::release() noexcept
{
    if (!m_link)
        return;
    const std::shared_ptr<SequenceOutputLink> link = std::move(m_link);
    PayloadHolder expected = PayloadHolder::Writer;
    if (link->holder.compare_exchange_strong(expected, PayloadHolder::Output))
        return;
    if (expected == PayloadHolder::Abandoned)
    {
        link->holder.store(PayloadHolder::Closed);
        std::error_code ignored;
        (void)std::filesystem::remove(link->pendingPath, ignored);
    }
}

SequenceOutput::SequenceOutput() noexcept = default;

SequenceOutput::SequenceOutput(std::unique_ptr<SequenceOutputState> state) noexcept : m_state(std::move(state)) {}

SequenceOutput::~SequenceOutput()
{
    abort();
}

SequenceOutput::SequenceOutput(SequenceOutput&& other) noexcept : m_state(std::move(other.m_state)) {}

SequenceOutput& SequenceOutput::operator=(SequenceOutput&& other) noexcept
{
    if (this != &other)
    {
        abort();
        m_state = std::move(other.m_state);
    }
    return *this;
}

Result<SequenceOutput> SequenceOutput::begin(const EngineContext& engine, const ExecutionContext& execution,
                                             const SequenceOutputRequest& request)
{
    return guarded("begin", [&]() -> Result<SequenceOutput> {
        constexpr const char* kOperation = "SequenceOutput::begin";
        if (!engine.valid() || !execution.valid())
            return Status::failure(ErrorCode::NotInitialized);
        if (engine.id() != execution.engineContextId())
            return Status::failure(ErrorCode::IncompatibleArguments,
                                   {.message = "the execution belongs to another engine context",
                                    .operation = kOperation,
                                    .domain = ErrorDomain::Io});
        if (const Status stopped = execution.checkCancellation(); stopped.failed())
            return stopped;
        if (!isBoundaryPath(request.destinationPath))
            return argumentFailure(kOperation, "a destination path is nonempty bounded UTF-8 without NUL",
                                   "destinationPath");
        if (!isOutputDurability(request.durability))
            return argumentFailure(kOperation, "the durability is not one this build knows", "durability");
        if (!isOutputReplacePolicy(request.replacePolicy))
            return argumentFailure(kOperation, "the replace policy is not one this build knows", "replacePolicy");
        auto state = std::make_unique<SequenceOutputState>();
        state->destination = std::string(request.destinationPath);
        state->durability = request.durability;
        // The image transaction's pending name, so one sweep cleans up after both kinds of output.
        state->pending = state->destination + "." + std::to_string(currentProcessIdentity()) + "-" +
                         std::to_string(engine.id()) + "-" + std::to_string(execution.operationId()) +
                         std::string(kPendingOutputSuffix);
        if (state->pending.size() > kMaxBoundaryPathBytes)
            return Status::failure(
                ErrorCode::LimitExceeded,
                {.message = "the pending sibling's path would exceed the path bound",
                 .operation = kOperation,
                 .domain = ErrorDomain::Io,
                 .context = {StatusContextField::ofCount(ContextKey::Requested, state->pending.size()),
                             StatusContextField::ofCount(ContextKey::Limit, kMaxBoundaryPathBytes)}});
        state->destinationPath = outputPathFromUtf8(state->destination);
        state->pendingPath = outputPathFromUtf8(state->pending);
        state->link = std::make_shared<SequenceOutputLink>();
        state->link->pending = state->pending;
        state->link->pendingPath = state->pendingPath;
        std::error_code existence;
        state->destinationExisted = std::filesystem::exists(state->destinationPath, existence);
        if (state->destinationExisted && request.replacePolicy == OutputReplacePolicy::RefuseExisting)
            return outputFailure(ErrorCode::InvalidState, CodecFailure::ExistingDestination, kOperation,
                                 "the destination exists and the request refuses to replace it", state->destination);
        const std::error_code reservation(reservePendingFile(state->pendingPath), std::generic_category());
        if (reservation == std::errc::file_exists)
            return outputFailure(ErrorCode::Busy, CodecFailure::PendingConflict, kOperation,
                                 "the pending sibling exists: another output is writing this destination, or an "
                                 "abandoned payload was never swept",
                                 state->pending);
        if (reservation)
            return outputFailure(ErrorCode::ExternalFailure, CodecFailure::DestinationUnavailable, kOperation,
                                 "the pending sibling could not be created beside the destination", state->pending,
                                 {StatusContextField::ofNumber(ContextKey::LibraryCode, reservation.value())});
        Result<EngineContextLease> lease = engine.acquireLease(0u);
        if (!lease.hasValue())
        {
            std::error_code ignored;
            (void)std::filesystem::remove(state->pendingPath, ignored);
            return lease.status();
        }
        state->lease = std::move(*lease);
        state->contextId = engine.id();
        return SequenceOutput(std::move(state));
    });
}

Result<SequenceOutputClaim> SequenceOutput::claimWriter()
{
    return guarded("claimWriter", [&]() -> Result<SequenceOutputClaim> {
        constexpr const char* kOperation = "SequenceOutput::claimWriter";
        if (!valid())
            return stateFailure(kOperation, "a writer claims the pending payload of an uncommitted output");
        PayloadHolder expected = PayloadHolder::Output;
        if (!m_state->link->holder.compare_exchange_strong(expected, PayloadHolder::Writer))
            return stateFailure(kOperation, "the pending payload already has a writer");
        return SequenceOutputClaim(m_state->link);
    });
}

Result<SequenceOutputReport> SequenceOutput::commit(const ExecutionContext& execution,
                                                    const SequenceWriteResult& finished)
{
    bool reached = false;
    auto outcome = guarded("commit", [&]() -> Result<SequenceOutputReport> {
        constexpr const char* kOperation = "SequenceOutput::commit";
        if (!valid())
            return stateFailure(kOperation, "commit needs an output holding a pending payload");
        SequenceOutputState& state = *m_state;
        if (!execution.valid() || execution.engineContextId() != state.contextId)
            return Status::failure(ErrorCode::IncompatibleArguments,
                                   {.message = "the execution belongs to another engine context",
                                    .operation = kOperation,
                                    .domain = ErrorDomain::Io});
        // A writer still holding the payload may be extending it, and on a platform where an open file
        // survives a rename it would go on writing into the published name.
        if (state.link->holder.load() == PayloadHolder::Writer)
            return stateFailure(kOperation, "a writer still holds the pending payload");
        reached = true;
        const auto fail = [&](Status status) -> Result<SequenceOutputReport> {
            abandon(state);
            return status;
        };
        if (const Status stopped = execution.checkCancellation(); stopped.failed())
            return fail(stopped);
        const bool flush = state.durability == OutputDurability::FlushBeforePublish;
        OutputFile file(state.pendingPath, flush ? OutputFileMode::Flush : OutputFileMode::Inspect);
        std::error_code error;
        const std::uint64_t size = file.size(error);
        if (error)
            return fail(outputFailure(ErrorCode::InvalidData, CodecFailure::ShortWrite, kOperation,
                                      "the pending payload is missing or cannot be inspected", state.pending,
                                      {StatusContextField::ofCount(ContextKey::Requested, finished.bytesWritten),
                                       StatusContextField::ofCount(ContextKey::Available, 0u),
                                       StatusContextField::ofNumber(ContextKey::LibraryCode, error.value())}));
        if (finished.bytesWritten == 0u || size != finished.bytesWritten)
            return fail(outputFailure(ErrorCode::InvalidData, CodecFailure::ShortWrite, kOperation,
                                      "the pending payload does not hold the bytes the sink reported", state.pending,
                                      {StatusContextField::ofCount(ContextKey::Requested, finished.bytesWritten),
                                       StatusContextField::ofCount(ContextKey::Available, size)}));
        if (flush)
            if (const int flushed = file.flush(); flushed != 0)
                return fail(outputFailure(ErrorCode::ExternalFailure, CodecFailure::FlushFailure, kOperation,
                                          "the pending payload could not be flushed to stable storage", state.pending,
                                          {StatusContextField::ofNumber(ContextKey::LibraryCode, flushed)}));
        if (const Status stopped = execution.checkCancellation(); stopped.failed())
            return fail(stopped);
        // Everything that can allocate precedes the rename; after it only nonthrowing steps remain.
        const std::filesystem::path directory = flush ? state.destinationPath.parent_path() : std::filesystem::path{};
        const SequenceOutputReport report{
            .bytesWritten = size, .flushed = flush, .replacedExisting = state.destinationExisted};
        if (const Status published = publishOutputFile(file, execution, state.destinationPath, kOperation);
            published.failed())
            return fail(published);
        state.link->holder.store(PayloadHolder::Closed);
        state.committed = true;
        state.lease.release();
        if (flush)
            syncDirectoryBestEffort(directory);
        return report;
    });
    if (!outcome.hasValue() && reached && m_state)
        abandon(*m_state);
    return outcome;
}

void SequenceOutput::abort() noexcept
{
    if (m_state)
        abandon(*m_state);
}

bool SequenceOutput::valid() const noexcept
{
    return m_state && !m_state->committed && !m_state->aborted;
}

bool SequenceOutput::committed() const noexcept
{
    return m_state && m_state->committed;
}

std::string_view SequenceOutput::destinationPath() const noexcept
{
    return m_state ? std::string_view(m_state->destination) : std::string_view{};
}

std::string_view SequenceOutput::pendingPath() const noexcept
{
    return m_state ? std::string_view(m_state->pending) : std::string_view{};
}

std::uint64_t SequenceOutput::engineContextId() const noexcept
{
    return valid() ? m_state->contextId : 0u;
}

} // namespace an
