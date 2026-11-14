// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the output transaction: reserving a pending payload, encoding into it, and
// proving, flushing and publishing it.
//
// Every failure after the reservation removes the pending payload and releases the lease, so a
// refused or cancelled transaction leaves nothing behind. When the destination's adapter can stream
// into a handle, the reservation creates the payload and keeps that handle through the encode, the
// size check, the readback and the flush; otherwise the reservation is an empty file the encoder
// opens by name and the commit reopens. Everything the report owns is built before the rename, so
// nothing that can fail runs after the payload has been published.
//
// A reserved image is the other way to fill the payload: the adapter writes the container with room
// for the samples and keeps it open, row bands are written into it from any number of threads under
// one record of which rows hold their samples, and the commit finishes and closes it before the size
// check, so the rest of the commit is the encoder's path unchanged. A row write that fails does not
// remove the payload under the other writers: it marks the image failed, and the abort that follows
// removes it.

#include <AfterNightEngine/IO/OutputTransaction.h>

#include <AfterNightEngine/Core/ExceptionBoundary.h>
#include <AfterNightEngine/Core/ImageLayout.h>
#include <AfterNightEngine/IO/CodecError.h>
#include <AfterNightEngine/IO/CodecIO.h>
#include <AfterNightEngine/IO/CodecMetadataPolicy.h>
#include <AfterNightEngine/IO/CodecRegistry.h>
#include <AfterNightEngine/IO/PendingOutput.h>
#include <AfterNightEngine/Image/RawFrame.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <AfterNightEngine/Runtime/MemoryResource.h>
#include <AfterNightEngine/Runtime/TraceSink.h>

#include "core/Utf8.h"
#include "io/output/FileSystem.h"
#include "io/registry/PreparedCodecWrite.h"
#include "io/registry/ReservedCodecWrite.h"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <system_error>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace an
{

// The operation owns retry and cancellation policy; FileSystem only classifies native failures.
Status publishOutputFile(OutputFile& file, const ExecutionContext& execution, const std::filesystem::path& destination,
                         const char* operation)
{
#ifdef _WIN32
    const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(250);
#endif
    std::error_code error;
    for (;;)
    {
        if (const Status stopped = execution.checkCancellation(); stopped.failed())
            return stopped;
#ifdef _WIN32
        if (error && std::chrono::steady_clock::now() >= until)
            break;
#endif
        file.publish(destination, error);
        if (!error)
            return Status::success();
#ifdef _WIN32
        // Readers such as scanners may briefly omit FILE_SHARE_DELETE. Other errors are permanent
        // for this attempt; never retry missing paths, malformed names, or unrelated I/O failures.
        const bool transient = isTransientPublicationError(error);
        if (!transient || std::chrono::steady_clock::now() >= until)
            break;
        execution.reportTrace(TraceEventKind::Counter, "io.output", "publish-retry", static_cast<double>(error.value()),
                              "platform-code");
        if (const Status stopped = execution.checkCancellation(); stopped.failed())
            return stopped;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
#else
        break;
#endif
    }
    return Status::failure(
        ErrorCode::ExternalFailure,
        {.message = "the pending payload could not replace the destination",
         .operation = operation,
         .domain = ErrorDomain::Io,
         .context = {StatusContextField::ofCount(ContextKey::CodecReason,
                                                 static_cast<std::uint64_t>(CodecFailure::PublishFailure)),
                     StatusContextField::ofText(ContextKey::Path, utf8FromOutputPath(destination)),
                     StatusContextField::ofNumber(ContextKey::LibraryCode, error.value())}});
}

/// Everything a reserved transaction holds. Defined only here; the public handle owns it.
class OutputTransactionState
{
public:
    const EngineContext* engine = nullptr;
    const ExecutionContext* execution = nullptr;
    EngineContextLease lease;
    std::string destination;
    std::string pending;
    std::filesystem::path destinationPath;
    std::filesystem::path pendingPath;
    std::unique_ptr<OutputFile> reservedOutput;
    CodecFormat format = CodecFormat::Unknown;
    std::optional<OutputIntent> intent;
    std::optional<ArtifactIdentity> identity;
    OutputDurability durability = OutputDurability::FlushBeforePublish;
    OutputHeaderProlog headerProlog = OutputHeaderProlog::Specified;
    OutputByteSink* sink = nullptr;
    std::uint64_t bytesWritten = 0u;
    bool destinationExisted = false;
    bool encoded = false;
    bool committed = false;
    bool aborted = false;
    /// The room a reservation wrote, until the commit finishes it; null otherwise.
    std::unique_ptr<CodecReservedRows> reservedRows;
    /// The reserved image's width, channels, element size and arrangement, which every band is held to.
    std::uint32_t reservedWidth = 0u;
    std::uint32_t reservedChannels = 0u;
    std::uint32_t reservedElementBytes = 0u;
    StorageArrangement reservedArrangement = StorageArrangement::Planar;
    /// Guards the two members below, which concurrent row writes share.
    std::mutex rowsMutex;
    /// One entry per image row: nonzero once a band holding it has been handed to the adapter.
    std::vector<std::uint8_t> rowsWritten;
    /// Whether a row write failed in the adapter, which refuses every later write and the commit.
    bool rowsFailed = false;
};

namespace
{
/// The chunk the pending payload is read back in for a byte sink.
constexpr std::uint64_t kSinkChunkBytes = 65536u;
constexpr std::uint64_t kSinkChunkAlignment = 64u;

Status outputFailure(ErrorCode code, CodecFailure reason, CodecFormat format, const char* operation,
                     const char* message, std::string_view path, std::vector<StatusContextField> extra = {},
                     Status cause = {})
{
    std::vector<StatusContextField> context{
        StatusContextField::ofCount(ContextKey::CodecFormat, static_cast<std::uint64_t>(format)),
        StatusContextField::ofCount(ContextKey::CodecReason, static_cast<std::uint64_t>(reason)),
        StatusContextField::ofText(ContextKey::Path, std::string(path))};
    for (auto& field : extra)
        context.push_back(std::move(field));
    return Status::failure(
        code, {.message = message, .operation = operation, .domain = ErrorDomain::Io, .context = std::move(context)},
        std::move(cause));
}

Status argumentFailure(const char* operation, const char* message, const char* parameter)
{
    return Status::failure(ErrorCode::InvalidArgument,
                           {.message = message,
                            .operation = operation,
                            .domain = ErrorDomain::Io,
                            .context = {StatusContextField::ofText(ContextKey::Parameter, parameter)}});
}

template <typename CallableT> auto guarded(const char* operation, CallableT&& body) -> std::invoke_result_t<CallableT>
{
    auto result =
        evaluateAcrossBoundary({"OutputTransaction", operation, ErrorDomain::Io}, std::forward<CallableT>(body));
    if (!result.hasValue())
        return result.status();
    return std::move(*result);
}

/// The intent the encoder receives: the caller's, with the identity cards appended and any
/// identity card the caller stated withdrawn in favour of the stated identity.
Result<OutputIntent> effectiveIntent(const OutputIntent& intent, const ArtifactIdentity* identity, CodecFormat format)
{
    if (identity == nullptr)
        return intent;
    OutputIntentRequest request = intent.toRequest();
    const CodecNamespace origin = codecNamespaceOf(format);
    std::vector<CodecCard> cards;
    for (auto& card : request.cards)
        if (!(card.origin() == origin && isArtifactIdentityKeyword(card.keyword())))
            cards.push_back(std::move(card));
    Result<std::vector<CodecCard>> stamp = artifactIdentityCards(*identity, origin);
    if (!stamp.hasValue())
        return stamp.status();
    for (auto& card : *stamp)
        cards.push_back(std::move(card));
    request.cards = std::move(cards);
    return OutputIntent::create(request);
}

void removePending(OutputTransactionState& state) noexcept
{
    // The reserved image holds the payload open, so it is released before the file is removed.
    state.reservedRows.reset();
    state.reservedOutput.reset();
    std::error_code ignored;
    (void)std::filesystem::remove(state.pendingPath, ignored);
}

Status reservedRowsFailure(ErrorCode code, const char* message, const char* parameter)
{
    return Status::failure(code, {.message = message,
                                  .operation = "OutputTransaction::writeRows",
                                  .domain = ErrorDomain::Io,
                                  .context = {StatusContextField::ofText(ContextKey::Parameter, parameter)}});
}

/// Ends the transaction without publishing: the pending payload goes and the lease is released.
void abandon(OutputTransactionState& state) noexcept
{
    if (state.committed || state.aborted)
        return;
    removePending(state);
    state.lease.release();
    state.aborted = true;
    state.encoded = false;
}

Status shortWrite(const OutputTransactionState& state, std::uint64_t available, const char* message)
{
    return outputFailure(ErrorCode::InvalidData, CodecFailure::ShortWrite, state.format, "OutputTransaction::commit",
                         message, state.pending,
                         {StatusContextField::ofCount(ContextKey::Requested, state.bytesWritten),
                          StatusContextField::ofCount(ContextKey::Available, available)});
}

/// Reads the pending payload back in bounded chunks and shows each to the sink.
Result<std::uint64_t> observePayload(OutputTransactionState& state, const OutputFile& file)
{
    MemoryResource* resource = state.engine->memoryResource();
    if (resource == nullptr)
        return Status::failure(ErrorCode::NotInitialized);
    Result<void*> buffer = resource->allocate(kSinkChunkBytes, kSinkChunkAlignment);
    if (!buffer.hasValue())
        return buffer.status();
    const auto release = [resource](void* bytes) noexcept {
        resource->deallocate(bytes, kSinkChunkBytes, kSinkChunkAlignment);
    };
    const std::unique_ptr<void, decltype(release)> owned(*buffer, release);
    std::uint64_t observed = 0u;
    auto* chunk = static_cast<std::byte*>(*buffer);
    for (;;)
    {
        if (const Status stopped = state.execution->checkCancellation(); stopped.failed())
            return stopped;
        std::error_code error;
        const auto got = file.read(chunk, kSinkChunkBytes, error);
        if (error)
            return shortWrite(state, observed, "the pending payload could not be read back in full");
        if (got == 0u)
            break;
        state.sink->observe(std::span<const std::byte>(chunk, got));
        observed += got;
    }
    return observed;
}

Result<ProvenanceEntry> commitRecord(const OutputTransactionState& state)
{
    std::string detail = "format=";
    detail += codecFormatName(state.format);
    detail += "; bytes=" + std::to_string(state.bytesWritten);
    if (state.identity)
    {
        detail += "; artifact=" + state.identity->artifactId();
        if (!state.identity->runId().empty())
            detail += "; run=" + state.identity->runId();
    }
    const std::string omitted = "; destination=<" + std::to_string(state.destination.size()) + " bytes omitted>";
    if (detail.size() + 15u + state.destination.size() <= kMaxProvenanceTextBytes)
        detail += "; destination=" + state.destination;
    else
        detail += omitted;
    return ProvenanceEntry::create("OutputTransaction::commit", std::move(detail));
}
} // namespace

OutputTransaction::OutputTransaction() noexcept = default;

OutputTransaction::OutputTransaction(std::unique_ptr<OutputTransactionState> state) noexcept : m_state(std::move(state))
{
}

OutputTransaction::~OutputTransaction()
{
    abort();
}

OutputTransaction::OutputTransaction(OutputTransaction&& other) noexcept : m_state(std::move(other.m_state)) {}

OutputTransaction& OutputTransaction::operator=(OutputTransaction&& other) noexcept
{
    if (this != &other)
    {
        abort();
        m_state = std::move(other.m_state);
    }
    return *this;
}

Result<OutputTransaction> OutputTransaction::begin(const EngineContext& engine, const ExecutionContext& execution,
                                                   const OutputRequest& request)
{
    return guarded("begin", [&]() -> Result<OutputTransaction> {
        constexpr const char* kOperation = "OutputTransaction::begin";
        if (!engine.valid() || !execution.valid())
            return Status::failure(ErrorCode::NotInitialized);
        if (engine.id() != execution.engineContextId())
            return Status::failure(ErrorCode::IncompatibleArguments,
                                   {.message = "the execution belongs to another engine context",
                                    .operation = kOperation,
                                    .domain = ErrorDomain::Io});
        if (const Status stopped = execution.checkCancellation(); stopped.failed())
            return stopped;
        if (!isOutputDurability(request.durability))
            return argumentFailure(kOperation, "the durability is not one this build knows", "durability");
        if (!isOutputReplacePolicy(request.replacePolicy))
            return argumentFailure(kOperation, "the replace policy is not one this build knows", "replacePolicy");
        if (!isOutputHeaderProlog(request.headerProlog))
            return argumentFailure(kOperation, "the header prolog is not one this build knows", "headerProlog");
        if (!isBoundaryPath(request.destinationPath))
            return argumentFailure(kOperation, "a destination path is nonempty bounded UTF-8 without NUL",
                                   "destinationPath");
        if (!isCodecFormat(request.format))
            return outputFailure(ErrorCode::InvalidArgument, CodecFailure::UnknownFormat, request.format, kOperation,
                                 "the format is not one this build knows", request.destinationPath);
        CodecFormat format = request.format;
        if (format == CodecFormat::Unknown)
            format = codecFormatFromPath(request.destinationPath);
        if (format == CodecFormat::Unknown)
            return outputFailure(ErrorCode::Unsupported, CodecFailure::UnknownFormat, format, kOperation,
                                 "neither the request nor the destination's extension names a format",
                                 request.destinationPath);
        const CodecRegistry* registry = engine.codecRegistry();
        const std::optional<CodecCapabilities> capabilities =
            registry != nullptr ? registry->capabilities(format) : std::nullopt;
        if (!capabilities)
            return outputFailure(ErrorCode::Unsupported, CodecFailure::Unavailable, format, kOperation,
                                 "this engine context has no adapter for the destination's format",
                                 request.destinationPath);
        if (capabilities->encodeSamples == 0u)
            return outputFailure(ErrorCode::Unsupported, CodecFailure::UnsupportedOperation, format, kOperation,
                                 "the destination's adapter advertises no encoder", request.destinationPath);
        if (request.identity != nullptr && request.intent == nullptr)
            return Status::failure(ErrorCode::IncompatibleArguments,
                                   {.message = "an artifact identity travels in the output intent's cards, and the "
                                               "raw encoding carries none",
                                    .operation = kOperation,
                                    .domain = ErrorDomain::Io,
                                    .context = {StatusContextField::ofText(ContextKey::Parameter, "identity")}});
        auto state = std::make_unique<OutputTransactionState>();
        state->engine = &engine;
        state->execution = &execution;
        state->destination = std::string(request.destinationPath);
        state->format = format;
        state->durability = request.durability;
        state->headerProlog = request.headerProlog;
        state->sink = request.byteSink;
        if (request.identity != nullptr)
            state->identity = *request.identity;
        if (request.intent != nullptr)
        {
            Result<OutputIntent> intent = effectiveIntent(*request.intent, request.identity, format);
            if (!intent.hasValue())
                return intent.status();
            state->intent = std::move(*intent);
        }
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
        std::error_code error;
        state->destinationExisted = std::filesystem::exists(state->destinationPath, error);
        if (state->destinationExisted && request.replacePolicy == OutputReplacePolicy::RefuseExisting)
            return outputFailure(ErrorCode::InvalidState, CodecFailure::ExistingDestination, format, kOperation,
                                 "the destination exists and the request refuses to replace it", state->destination);
        std::error_code reservationError;
        if (supportsPreparedCodecWrite(engine, format))
        {
            state->reservedOutput =
                std::make_unique<OutputFile>(state->pendingPath, OutputFileMode::Create, state->sink != nullptr);
            reservationError = state->reservedOutput->openError();
        }
        else
            reservationError = std::error_code(reservePendingFile(state->pendingPath), std::generic_category());
        if (reservationError == std::errc::file_exists)
            return outputFailure(ErrorCode::Busy, CodecFailure::PendingConflict, format, kOperation,
                                 "the pending sibling exists: another transaction is writing this destination, or an "
                                 "abandoned payload was never swept",
                                 state->pending);
        else if (reservationError)
            return outputFailure(ErrorCode::ExternalFailure, CodecFailure::DestinationUnavailable, format, kOperation,
                                 "the pending sibling could not be created beside the destination", state->pending,
                                 {StatusContextField::ofNumber(ContextKey::LibraryCode, reservationError.value())});
        Result<EngineContextLease> lease = engine.acquireLease(0u);
        if (!lease.hasValue())
        {
            removePending(*state);
            return lease.status();
        }
        state->lease = std::move(*lease);
        return OutputTransaction(std::move(state));
    });
}

Status OutputTransaction::encode(const RawFrame& frame, const ImageMetadata& metadata)
{
    Status outcome;
    const Status crossed = callAcrossBoundary({"OutputTransaction", "encode", ErrorDomain::Io}, [&] {
        if (!valid() || m_state->encoded || m_state->reservedRows)
        {
            outcome = Status::failure(
                ErrorCode::InvalidState,
                {.message = "encode needs a reserved transaction that has neither encoded nor reserved an image yet",
                 .operation = "OutputTransaction::encode",
                 .domain = ErrorDomain::Io});
            return;
        }
        OutputTransactionState& state = *m_state;
        if (const Status stopped = state.execution->checkCancellation(); stopped.failed())
        {
            abandon(state);
            outcome = stopped;
            return;
        }
        Result<ImageMetadata> reduced = outputMetadata(metadata, state.format);
        if (!reduced.hasValue())
        {
            abandon(state);
            outcome = reduced.status();
            return;
        }
        const CodecEncodeRequest request{.pendingPath = state.pending,
                                         .format = state.format,
                                         .intent = state.intent ? &*state.intent : nullptr,
                                         .headerProlog = state.headerProlog};
        Result<CodecEncodeReport> report =
            encodePreparedImage(*state.engine, *state.execution, request, frame, *reduced, state.reservedOutput.get());
        if (!report.hasValue())
        {
            abandon(state);
            outcome = report.status();
            return;
        }
        state.bytesWritten = report->bytesWritten;
        state.encoded = true;
        outcome = Status::success();
    });
    if (crossed.failed())
    {
        if (m_state)
            abandon(*m_state);
        return crossed;
    }
    return outcome;
}

Status OutputTransaction::reserve(const RawFrameDescriptor& descriptor, StorageArrangement arrangement,
                                  const ImageMetadata& metadata)
{
    Status outcome;
    const Status crossed = callAcrossBoundary({"OutputTransaction", "reserve", ErrorDomain::Io}, [&] {
        if (!valid() || m_state->encoded || m_state->reservedRows)
        {
            outcome = Status::failure(
                ErrorCode::InvalidState,
                {.message = "reserve needs a reserved transaction that has neither encoded nor reserved an image yet",
                 .operation = "OutputTransaction::reserve",
                 .domain = ErrorDomain::Io});
            return;
        }
        OutputTransactionState& state = *m_state;
        if (const Status stopped = state.execution->checkCancellation(); stopped.failed())
        {
            abandon(state);
            outcome = stopped;
            return;
        }
        Result<ImageMetadata> reduced = outputMetadata(metadata, state.format);
        if (!reduced.hasValue())
        {
            abandon(state);
            outcome = reduced.status();
            return;
        }
        const CodecEncodeRequest request{.pendingPath = state.pending,
                                         .format = state.format,
                                         .intent = state.intent ? &*state.intent : nullptr,
                                         .headerProlog = state.headerProlog};
        Result<std::unique_ptr<CodecReservedRows>> rows =
            reservePreparedImage(*state.engine, *state.execution, request, descriptor, arrangement, *reduced);
        if (!rows.hasValue())
        {
            abandon(state);
            outcome = rows.status();
            return;
        }
        const ImageExtent& extent = descriptor.extent();
        state.reservedWidth = extent.width();
        state.reservedChannels = extent.channelCount();
        state.reservedElementBytes = descriptor.sampleFormat().sizeBytes;
        // One channel is planar whatever was stated, which is how a layout of it is named.
        state.reservedArrangement = extent.channelCount() == 1u ? StorageArrangement::Planar : arrangement;
        state.rowsWritten.assign(extent.height(), std::uint8_t{0u});
        state.rowsFailed = false;
        state.reservedRows = std::move(*rows);
        outcome = Status::success();
    });
    if (crossed.failed())
    {
        if (m_state)
            abandon(*m_state);
        return crossed;
    }
    return outcome;
}

Status OutputTransaction::writeRows(std::uint32_t firstRow, const ImageLayout& layout,
                                    std::span<const std::byte> samples)
{
    if (!reserved())
        return reservedRowsFailure(ErrorCode::InvalidState, "writeRows needs a transaction holding a reserved image",
                                   "transaction");
    OutputTransactionState& state = *m_state;
    bool claimed = false;
    Status outcome;
    const Status crossed = callAcrossBoundary({"OutputTransaction", "writeRows", ErrorDomain::Io}, [&] {
        if (const Status stopped = state.execution->checkCancellation(); stopped.failed())
        {
            outcome = stopped;
            return;
        }
        const ImageExtent& band = layout.extent();
        const std::uint32_t rows = band.height();
        const auto height = static_cast<std::uint32_t>(state.rowsWritten.size());
        if (rows == 0u || firstRow >= height || rows > height - firstRow)
        {
            outcome = Status::failure(ErrorCode::OutOfRange,
                                      {.message = "the band's rows do not lie inside the reserved image",
                                       .operation = "OutputTransaction::writeRows",
                                       .domain = ErrorDomain::Io,
                                       .context = {StatusContextField::ofText(ContextKey::Parameter, "firstRow"),
                                                   StatusContextField::ofCount(ContextKey::Requested, firstRow),
                                                   StatusContextField::ofCount(ContextKey::Limit, height)}});
            return;
        }
        if (band.width() != state.reservedWidth || band.channelCount() != state.reservedChannels ||
            layout.elementSizeBytes() != state.reservedElementBytes ||
            storageArrangementOf(layout) != state.reservedArrangement)
        {
            outcome = reservedRowsFailure(ErrorCode::IncompatibleArguments,
                                          "the band's layout is not the reserved image's width, channels, element "
                                          "size and arrangement over its rows",
                                          "layout");
            return;
        }
        if (samples.size() < layout.totalBytes())
        {
            outcome = reservedRowsFailure(ErrorCode::IncompatibleArguments, "samples is shorter than the band's layout",
                                          "samples");
            return;
        }
        {
            // The rows are claimed before the adapter writes them, so two writers of one row meet a refusal rather
            // than each other, and a band that overlaps a written one claims nothing.
            const std::lock_guard lock(state.rowsMutex);
            if (state.rowsFailed)
            {
                outcome = reservedRowsFailure(ErrorCode::InvalidState, "a row write of the reserved image has failed",
                                              "transaction");
                return;
            }
            const auto first = state.rowsWritten.begin() + firstRow;
            if (const auto taken = std::find(first, first + rows, std::uint8_t{1u}); taken != first + rows)
            {
                outcome = Status::failure(
                    ErrorCode::InvalidArgument,
                    {.message = "a row of the band has been written already; each row of a reserved image is written "
                                "once",
                     .operation = "OutputTransaction::writeRows",
                     .domain = ErrorDomain::Io,
                     .context = {
                         StatusContextField::ofText(ContextKey::Parameter, "firstRow"),
                         StatusContextField::ofCount(ContextKey::Requested,
                                                     static_cast<std::uint64_t>(taken - state.rowsWritten.begin()))}});
                return;
            }
            std::fill(first, first + rows, std::uint8_t{1u});
            claimed = true;
        }
        outcome = writeReservedRows(*state.execution, state.format, state.pending, *state.reservedRows, firstRow,
                                    layout, samples);
    });
    const Status result = crossed.failed() ? crossed : outcome;
    if (result.failed() && claimed)
    {
        // Rows handed to the adapter may be partly written, and a payload with such rows is never published.
        const std::lock_guard lock(state.rowsMutex);
        state.rowsFailed = true;
    }
    return result;
}

bool OutputTransaction::reserved() const noexcept
{
    return valid() && m_state->reservedRows != nullptr;
}

Result<OutputReport> OutputTransaction::commit()
{
    auto outcome = guarded("commit", [&]() -> Result<OutputReport> {
        constexpr const char* kOperation = "OutputTransaction::commit";
        if (!valid() || (!m_state->encoded && !m_state->reservedRows))
            return Status::failure(ErrorCode::InvalidState,
                                   {.message = "commit needs a transaction whose encoder has run or whose reserved "
                                               "image has been written",
                                    .operation = kOperation,
                                    .domain = ErrorDomain::Io});
        OutputTransactionState& state = *m_state;
        const auto fail = [&](Status status) -> Result<OutputReport> {
            abandon(state);
            return status;
        };
        if (const Status stopped = state.execution->checkCancellation(); stopped.failed())
            return fail(stopped);
        if (state.reservedRows)
        {
            // A reserved image is published only whole: every row written, then the payload finished and closed, so
            // the size check below reads the file the adapter completed.
            if (state.rowsFailed)
                return fail(
                    Status::failure(ErrorCode::InvalidState, {.message = "a row write of the reserved image failed",
                                                              .operation = kOperation,
                                                              .domain = ErrorDomain::Io}));
            const auto missing = std::find(state.rowsWritten.begin(), state.rowsWritten.end(), std::uint8_t{0u});
            if (missing != state.rowsWritten.end())
                return fail(Status::failure(
                    ErrorCode::InvalidState,
                    {.message = "a row of the reserved image was never written",
                     .operation = kOperation,
                     .domain = ErrorDomain::Io,
                     .context = {StatusContextField::ofCount(
                         ContextKey::Requested, static_cast<std::uint64_t>(missing - state.rowsWritten.begin()))}}));
            Result<std::uint64_t> finished =
                finishReservedRows(*state.execution, state.format, state.pending, *state.reservedRows);
            if (!finished.hasValue())
                return fail(finished.status());
            state.reservedRows.reset();
            state.bytesWritten = *finished;
            state.encoded = true;
        }
        const bool flush = state.durability == OutputDurability::FlushBeforePublish;
        // Streamed codecs have already written every byte synchronously. Inspect and optionally
        // read/flush that same reservation, then check its close before publishing by name.
        std::optional<OutputFile> reopened;
        if (!state.reservedOutput)
            reopened.emplace(state.pendingPath, flush ? OutputFileMode::Flush : OutputFileMode::Inspect,
                             state.sink != nullptr);
        OutputFile& file = state.reservedOutput ? *state.reservedOutput : *reopened;
        std::error_code error;
        const std::uint64_t size = file.size(error);
        if (error)
            return fail(shortWrite(state, 0u, "the pending payload is missing after the encoder reported success"));
        if (state.bytesWritten == 0u || size != state.bytesWritten)
            return fail(shortWrite(state, static_cast<std::uint64_t>(size),
                                   "the pending payload does not hold the bytes the encoder reported"));
        std::uint64_t observed = 0u;
        if (state.sink != nullptr)
        {
            if (file.rewind() != 0)
                return fail(shortWrite(state, 0u, "the pending payload could not be positioned for readback"));
            Result<std::uint64_t> seen = observePayload(state, file);
            if (!seen.hasValue())
                return fail(seen.status());
            observed = *seen;
            if (observed != state.bytesWritten)
                return fail(shortWrite(state, observed, "the pending payload could not be read back in full"));
        }
        if (flush)
            if (const int flushed = file.flush(); flushed != 0)
                return fail(outputFailure(ErrorCode::ExternalFailure, CodecFailure::FlushFailure, state.format,
                                          kOperation, "the pending payload could not be flushed to stable storage",
                                          state.pending,
                                          {StatusContextField::ofNumber(ContextKey::LibraryCode, flushed)}));
        if (const Status stopped = state.execution->checkCancellation(); stopped.failed())
            return fail(stopped);
        // Build every owning part of the answer and the directory path before publication.
        // Once the rename succeeds, only nonthrowing operations may remain.
        Result<ProvenanceEntry> record = commitRecord(state);
        if (!record.hasValue())
            return fail(record.status());
        const auto directory = flush ? state.destinationPath.parent_path() : std::filesystem::path{};
        OutputReport report;
        report.bytesWritten = state.bytesWritten;
        report.observedBytes = observed;
        report.flushed = flush;
        report.replacedExisting = state.destinationExisted;
        report.intent = state.intent;
        report.transition.outputIntent = state.intent;
        report.transition.record = std::move(*record);
        static_assert(std::is_nothrow_move_constructible_v<OutputReport>);
        static_assert(std::is_nothrow_move_constructible_v<Result<OutputReport>>);
        if (const Status stopped = state.execution->checkCancellation(); stopped.failed())
            return fail(stopped);
        if (state.reservedOutput)
        {
            if (const int closed = file.close(); closed != 0)
                return fail(outputFailure(ErrorCode::ExternalFailure, CodecFailure::ShortWrite, state.format,
                                          kOperation, "the pending payload could not be closed", state.pending,
                                          {StatusContextField::ofNumber(ContextKey::LibraryCode, closed)}));
        }
        if (const Status published = publishOutputFile(file, *state.execution, state.destinationPath, kOperation);
            published.failed())
        {
            if (published.code() != ErrorCode::ExternalFailure)
                return fail(published);
            std::vector<StatusContextField> extra;
            if (const auto* code = published.findContext(ContextKey::LibraryCode))
                extra.push_back(*code);
            return fail(outputFailure(ErrorCode::ExternalFailure, CodecFailure::PublishFailure, state.format,
                                      kOperation, "the pending payload could not replace the destination",
                                      state.destination, std::move(extra), published));
        }
        state.committed = true;
        state.reservedOutput.reset();
        state.lease.release();
        if (flush)
            syncDirectoryBestEffort(directory);
        return report;
    });
    if (!outcome.hasValue() && m_state)
        abandon(*m_state);
    return outcome;
}

void OutputTransaction::abort() noexcept
{
    if (m_state)
        abandon(*m_state);
}

bool OutputTransaction::valid() const noexcept
{
    return m_state && !m_state->committed && !m_state->aborted;
}

bool OutputTransaction::encoded() const noexcept
{
    return valid() && m_state->encoded;
}

bool OutputTransaction::committed() const noexcept
{
    return m_state && m_state->committed;
}

std::string_view OutputTransaction::destinationPath() const noexcept
{
    return m_state ? std::string_view(m_state->destination) : std::string_view{};
}

std::string_view OutputTransaction::pendingPath() const noexcept
{
    return m_state ? std::string_view(m_state->pending) : std::string_view{};
}

CodecFormat OutputTransaction::format() const noexcept
{
    return m_state ? m_state->format : CodecFormat::Unknown;
}

bool supportsReservedImageOutput(const EngineContext& engine, CodecFormat format) noexcept
{
    return engine.valid() && supportsReservedCodecWrite(engine, format);
}

Result<OutputReport> writeImage(const EngineContext& engine, const ExecutionContext& execution,
                                const OutputRequest& request, const RawFrame& frame, const ImageMetadata& metadata)
{
    Result<OutputTransaction> transaction = OutputTransaction::begin(engine, execution, request);
    if (!transaction.hasValue())
        return transaction.status();
    if (const Status encoded = transaction->encode(frame, metadata); encoded.failed())
        return encoded;
    return transaction->commit();
}

} // namespace an
