// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - writing, probing and reading an image snapshot.
//
// A write measures, encodes and hashes the metadata, reserves a pending sibling through the output domain's
// `OutputFile` - the one the output transaction writes - and streams the sections into it in transfers of at most
// 4 MiB, hashing each transfer of samples as it goes out while it is still in cache; the header goes first with no
// sample checksum and is rewritten in place last. The pending file's size is proved, it is flushed when asked,
// closed, and renamed over the destination by `publishOutputFile`; every failure after the reservation removes it. A
// read checks the version before anything else, then the header, then each section's checksum as it arrives, into
// memory the engine context grants. Samples are copied as bytes, which is a little-endian copy on every platform this
// engine builds for; the static assertion below is where that assumption is written down.

#include <AfterNightEngine/IO/ImageSnapshot.h>

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/ExceptionBoundary.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/IO/CodecError.h>
#include <AfterNightEngine/IO/PendingOutput.h>
#include <AfterNightEngine/Image/SampleBuffer.h>
#include <AfterNightEngine/Image/ValidityMaskView.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <AfterNightEngine/Runtime/MemoryResource.h>
#include <AfterNightEngine/Runtime/TaskScheduler.h>
#include <AfterNightEngine/Runtime/TraceSink.h>

#include "core/Utf8.h"
#include "io/output/FileSystem.h"
#include "io/output/SnapshotFormat.h"

#include <algorithm>
#include <bit>
#include <cstring>
#include <filesystem>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <system_error>
#include <type_traits>
#include <utility>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
// File I/O needs no GUI declarations from the Windows SDK.
#ifndef NOGDI
#define NOGDI
#endif
#ifndef NOUSER
#define NOUSER
#endif
#ifndef NOSERVICE
#define NOSERVICE
#endif
#ifndef NOMCX
#define NOMCX
#endif
#ifndef NOIME
#define NOIME
#endif
#include <Windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

static_assert(std::endian::native == std::endian::little,
              "a snapshot stores samples little-endian and copies them as bytes; a big-endian host needs a swap");

namespace an
{
namespace
{
constexpr const char* kWriteOperation = "an::writeImageSnapshot";
constexpr const char* kReadOperation = "an::readImageSnapshot";
constexpr const char* kProbeOperation = "an::probeImageSnapshot";

/// The largest single transfer, with a cancellation check before each.
constexpr std::uint64_t kTransferBytes = 4u * 1024u * 1024u;

/// How much is transferred between two updates of a running checksum: small enough that the bytes are hashed from
/// the core's own cache right after the transfer that moved them.
constexpr std::uint64_t kHashedTransferBytes = 1024u * 1024u;

/// The alignment of every buffer taken from the engine context.
constexpr std::uint64_t kBufferAlignment = 64u;

/// A refusal carrying its codec reason and the path, as the output transaction's do.
[[nodiscard]] Status snapshotFailure(ErrorCode code, CodecFailure reason, const char* operation, std::string message,
                                     std::string_view path, Status cause = {})
{
    return Status::failure(code,
                           StatusDetail{.message = std::move(message),
                                        .operation = operation,
                                        .domain = ErrorDomain::Io,
                                        .context = {StatusContextField::ofCount(ContextKey::CodecReason,
                                                                                static_cast<std::uint64_t>(reason)),
                                                    StatusContextField::ofText(ContextKey::Path, std::string(path))}},
                           std::move(cause));
}

[[nodiscard]] Status argumentFailure(ErrorCode code, const char* operation, const char* message, const char* parameter)
{
    return Status::failure(code,
                           StatusDetail{.message = message,
                                        .operation = operation,
                                        .domain = ErrorDomain::Io,
                                        .context = {StatusContextField::ofText(ContextKey::Parameter, parameter)}});
}

/// Bytes taken from the engine context's memory resource and given back when the owner goes.
class EngineBuffer
{
public:
    EngineBuffer() = default;
    EngineBuffer(const EngineBuffer&) = delete;
    EngineBuffer& operator=(const EngineBuffer&) = delete;
    ~EngineBuffer()
    {
        if (m_data != nullptr)
            m_resource->deallocate(m_data, m_bytes, kBufferAlignment);
    }
    [[nodiscard]] Status allocate(const EngineContext& engine, std::uint64_t bytes)
    {
        m_resource = engine.memoryResource();
        if (m_resource == nullptr)
            return Status::failure(ErrorCode::NotInitialized);
        Result<void*> taken = m_resource->allocate((std::max)(bytes, std::uint64_t{1}), kBufferAlignment);
        if (!taken.hasValue())
            return taken.status();
        m_data = static_cast<std::byte*>(*taken);
        m_bytes = (std::max)(bytes, std::uint64_t{1});
        m_used = bytes;
        return {};
    }
    [[nodiscard]] std::span<std::byte> bytes() const noexcept
    {
        return {m_data, static_cast<std::size_t>(m_used)};
    }

private:
    MemoryResource* m_resource = nullptr;
    std::byte* m_data = nullptr;
    std::uint64_t m_bytes = 0u;
    std::uint64_t m_used = 0u;
};

/// The file a read owns, read by position through a handle nothing else shares.
class SnapshotInput
{
public:
    SnapshotInput() = default;
    SnapshotInput(const SnapshotInput&) = delete;
    SnapshotInput& operator=(const SnapshotInput&) = delete;
    ~SnapshotInput()
    {
#if defined(_WIN32)
        if (m_handle != INVALID_HANDLE_VALUE)
            CloseHandle(m_handle);
#else
        if (m_handle >= 0)
            ::close(m_handle);
#endif
    }
    [[nodiscard]] bool open(std::string_view path)
    {
        const std::filesystem::path native = outputPathFromUtf8(path);
#if defined(_WIN32)
        m_handle = CreateFileW(native.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                               nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
        if (m_handle == INVALID_HANDLE_VALUE)
            return false;
        LARGE_INTEGER size{};
        if (!GetFileSizeEx(m_handle, &size) || size.QuadPart < 0)
            return false;
        m_bytes = static_cast<std::uint64_t>(size.QuadPart);
#else
        m_handle = ::open(native.c_str(), O_RDONLY | O_CLOEXEC);
        if (m_handle < 0)
            return false;
        struct stat info{};
        if (::fstat(m_handle, &info) != 0 || !S_ISREG(info.st_mode) || info.st_size < 0)
            return false;
        m_bytes = static_cast<std::uint64_t>(info.st_size);
#endif
        return true;
    }
    /// Opens a second handle on the file `other` holds, for a worker reading beside it; opened through `other`
    /// rather than by name, so it can only be the same file.
    [[nodiscard]] bool reopen(const SnapshotInput& other)
    {
#if defined(_WIN32)
        m_handle = ReOpenFile(other.m_handle, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                              FILE_FLAG_SEQUENTIAL_SCAN);
        m_bytes = other.m_bytes;
        return m_handle != INVALID_HANDLE_VALUE;
#else
        m_handle = ::fcntl(other.m_handle, F_DUPFD_CLOEXEC, 0);
        m_bytes = other.m_bytes;
        return m_handle >= 0;
#endif
    }
    [[nodiscard]] std::uint64_t size() const noexcept
    {
        return m_bytes;
    }
    /// Reads exactly `bytes.size()` bytes at an offset, in transfers with a cancellation check before each.
    [[nodiscard]] Status readAt(std::span<std::byte> bytes, std::uint64_t offset, const ExecutionContext& execution,
                                CodecFailure reason, std::string_view path)
    {
        std::byte* destination = bytes.data();
        std::uint64_t remaining = bytes.size();
        while (remaining != 0u)
        {
            if (const Status stopped = execution.checkCancellation(); stopped.failed())
                return stopped;
            const std::uint64_t chunk = (std::min)(remaining, kTransferBytes);
#if defined(_WIN32)
            OVERLAPPED position{};
            position.Offset = static_cast<DWORD>(offset);
            position.OffsetHigh = static_cast<DWORD>(offset >> 32u);
            DWORD received = 0u;
            if (!ReadFile(m_handle, destination, static_cast<DWORD>(chunk), &received, &position) || received != chunk)
                return shortRead(reason, path);
#else
            std::uint64_t done = 0u;
            while (done != chunk)
            {
                const auto read = ::pread(m_handle, destination + done, static_cast<std::size_t>(chunk - done),
                                          static_cast<off_t>(offset + done));
                if (read < 0 && errno == EINTR)
                    continue;
                if (read <= 0)
                    return shortRead(reason, path);
                done += static_cast<std::uint64_t>(read);
            }
#endif
            destination += chunk;
            offset += chunk;
            remaining -= chunk;
        }
        return {};
    }

private:
    [[nodiscard]] static Status shortRead(CodecFailure reason, std::string_view path)
    {
        return snapshotFailure(ErrorCode::InvalidData, reason, kReadOperation, "a read of the snapshot came up short",
                               path);
    }

#if defined(_WIN32)
    HANDLE m_handle = INVALID_HANDLE_VALUE;
#else
    int m_handle = -1;
#endif
    std::uint64_t m_bytes = 0u;
};

/// Unwraps a boundary's result of a result.
template <typename CallableT> auto guarded(const char* operation, CallableT&& body) -> std::invoke_result_t<CallableT>
{
    auto result = evaluateAcrossBoundary({"ImageSnapshot", operation, ErrorDomain::Io}, std::forward<CallableT>(body));
    if (!result.hasValue())
        return result.status();
    return std::move(*result);
}

/// Checks the two contexts a call names, its path and a stop already requested.
[[nodiscard]] Status checkContexts(const EngineContext& engine, const ExecutionContext& execution,
                                   std::string_view path, const char* operation)
{
    if (!engine.valid() || !execution.valid())
        return Status::failure(ErrorCode::NotInitialized,
                               StatusDetail{.message = "a snapshot needs an engine context and an execution that hold "
                                                       "state",
                                            .operation = operation,
                                            .domain = ErrorDomain::Io});
    if (engine.id() != execution.engineContextId())
        return Status::failure(ErrorCode::IncompatibleArguments,
                               StatusDetail{.message = "the execution belongs to another engine context",
                                            .operation = operation,
                                            .domain = ErrorDomain::Io});
    if (!isBoundaryPath(path))
        return argumentFailure(ErrorCode::InvalidArgument, operation, "a path is nonempty bounded UTF-8 without NUL",
                               "path");
    return execution.checkCancellation();
}

/// What a read's header and metadata say, before its samples are read.
struct OpenedSnapshot
{
    SnapshotInput file;
    std::optional<SnapshotHeader> header;
    std::optional<DecodedSnapshotMetadata> metadata;
};

/// Opens a snapshot, checks its version and header, and reads and decodes its metadata.
[[nodiscard]] Status openSnapshot(const EngineContext& engine, const ExecutionContext& execution, std::string_view path,
                                  const char* operation, OpenedSnapshot& opened)
{
    if (!opened.file.open(path))
        return snapshotFailure(ErrorCode::ExternalFailure, CodecFailure::MetadataUnreadable, operation,
                               "the snapshot could not be opened", path);
    const std::uint64_t fileBytes = opened.file.size();
    std::array<std::byte, kSnapshotHeaderBytes> bytes{};
    const std::uint64_t prefix = (std::min)(fileBytes, std::uint64_t{kSnapshotHeaderBytes});
    if (const Status read = opened.file.readAt(std::span(bytes.data(), static_cast<std::size_t>(prefix)), 0u, execution,
                                               CodecFailure::MetadataUnreadable, path);
        read.failed())
        return read;
    // A version this build does not read is refused before any other field of its header is believed.
    if (prefix >= kSnapshotVersionOffset + 4u &&
        std::equal(kSnapshotMagic.begin(), kSnapshotMagic.end(), bytes.begin()))
    {
        std::uint64_t version = 0u;
        for (std::size_t i = 0u; i < 4u; ++i)
            version |= std::uint64_t{std::to_integer<unsigned char>(bytes[kSnapshotVersionOffset + i])} << (8u * i);
        if (version < 1u || version > kImageSnapshotFormatVersion)
            return snapshotFailure(ErrorCode::Unsupported, CodecFailure::MetadataUnreadable, operation,
                                   "the snapshot is of version " + std::to_string(version) +
                                       ", and this build reads versions 1 to " +
                                       std::to_string(kImageSnapshotFormatVersion),
                                   path);
    }
    if (fileBytes < kSnapshotHeaderBytes)
        return snapshotFailure(ErrorCode::InvalidData, CodecFailure::MetadataUnreadable, operation,
                               "the file is shorter than a snapshot's header", path);
    Result<SnapshotHeader> header = parseSnapshotHeader(bytes, fileBytes, engine.limits());
    if (!header.hasValue())
        return snapshotFailure(header.status().code(), CodecFailure::MetadataUnreadable, operation,
                               std::string(header.status().message()), path, header.status());
    if (header->metadataBytes > kMaxImageSnapshotMetadataBytes)
        return snapshotFailure(ErrorCode::InvalidData, CodecFailure::MetadataUnreadable, operation,
                               "the snapshot's metadata section is longer than a snapshot holds", path);
    opened.header = std::move(*header);

    EngineBuffer section;
    if (const Status taken = section.allocate(engine, opened.header->metadataBytes); taken.failed())
        return taken;
    if (const Status read = opened.file.readAt(section.bytes(), kSnapshotHeaderBytes, execution,
                                               CodecFailure::MetadataUnreadable, path);
        read.failed())
        return read;
    if (snapshotChecksum(section.bytes()) != opened.header->metadataChecksum)
        return snapshotFailure(ErrorCode::InvalidData, CodecFailure::MetadataUnreadable, operation,
                               "the snapshot's metadata section fails its checksum", path);
    Result<DecodedSnapshotMetadata> decoded = decodeSnapshotMetadata(section.bytes(), opened.header->version);
    if (!decoded.hasValue())
        return snapshotFailure(ErrorCode::InvalidData, CodecFailure::MetadataUnreadable, operation,
                               std::string(decoded.status().message()), path, decoded.status());
    opened.metadata = std::move(*decoded);
    return {};
}

/// Writes one span through the pending file in bounded transfers, hashing each transfer as it goes out when asked.
[[nodiscard]] Status writeAll(OutputFile& file, std::span<const std::byte> bytes, const ExecutionContext& execution,
                              std::string_view pending, SnapshotBlockChecksum* checksum = nullptr)
{
    while (!bytes.empty())
    {
        if (const Status stopped = execution.checkCancellation(); stopped.failed())
            return stopped;
        const std::size_t chunk = static_cast<std::size_t>(
            (std::min)(std::uint64_t{bytes.size()}, checksum != nullptr ? kHashedTransferBytes : kTransferBytes));
        if (checksum != nullptr)
            checksum->update(bytes.first(chunk));
        if (!file.write(bytes.first(chunk)))
            return snapshotFailure(ErrorCode::ExternalFailure, CodecFailure::ShortWrite, kWriteOperation,
                                   "the pending snapshot could not be written", pending);
        bytes = bytes.subspan(chunk);
    }
    return {};
}

/// The frame's rows, every plane in order, padding excluded.
template <class Visit> void forEachRow(const RawFrame& frame, const ImageLayout& packed, Visit visit)
{
    const ImageLayout source = *frame.layout();
    const std::byte* const base = frame.bytes().data();
    for (std::uint32_t plane = 0u; plane < source.planeCount(); ++plane)
        for (std::uint32_t y = 0u; y < source.extent().height(); ++y)
            visit(std::span<const std::byte>(base + plane * source.planeStrideBytes() + y * source.rowStrideBytes(),
                                             static_cast<std::size_t>(packed.rowStrideBytes())));
}

/// Whether a frame's rows and planes already follow each other with no padding.
[[nodiscard]] bool isPacked(const ImageLayout& source, const ImageLayout& packed) noexcept
{
    return source.rowStrideBytes() == packed.rowStrideBytes() &&
           (source.planeCount() == 1u || source.planeStrideBytes() == packed.planeStrideBytes());
}

/// Removes the pending file of a write that did not publish.
class PendingGuard
{
public:
    explicit PendingGuard(std::filesystem::path path) : m_path(std::move(path)) {}
    PendingGuard(const PendingGuard&) = delete;
    PendingGuard& operator=(const PendingGuard&) = delete;
    ~PendingGuard()
    {
        if (!m_published)
        {
            pendingFile.reset();
            std::error_code ignored;
            (void)std::filesystem::remove(m_path, ignored);
        }
    }
    void published() noexcept
    {
        m_published = true;
    }
    std::unique_ptr<OutputFile> pendingFile;

private:
    std::filesystem::path m_path;
    bool m_published = false;
};

[[nodiscard]] Result<ImageSnapshotWriteReport>
writeSnapshot(const EngineContext& engine, const ExecutionContext& execution, const ImageSnapshotWriteRequest& request,
              const RawFrame& frame, const ImageMetadata& metadata, const ValidityMask* validity)
{
    if (const Status checked = checkContexts(engine, execution, request.destinationPath, kWriteOperation);
        checked.failed())
    {
        if (checked.code() == ErrorCode::InvalidArgument)
            return argumentFailure(ErrorCode::InvalidArgument, kWriteOperation,
                                   "a destination path is nonempty bounded UTF-8 without NUL", "destinationPath");
        return checked;
    }
    if (request.identity == nullptr)
        return argumentFailure(ErrorCode::InvalidArgument, kWriteOperation,
                               "a snapshot records the identity it was written for, and none was given", "identity");
    if (!isOutputDurability(request.durability))
        return argumentFailure(ErrorCode::InvalidArgument, kWriteOperation,
                               "the durability is not one this build knows", "durability");
    if (!isOutputReplacePolicy(request.replacePolicy))
        return argumentFailure(ErrorCode::InvalidArgument, kWriteOperation,
                               "the replace policy is not one this build knows", "replacePolicy");
    const std::optional<RawFrameDescriptor> descriptor = frame.descriptor();
    const std::optional<ImageLayout> source = frame.layout();
    if (!descriptor.has_value() || !source.has_value())
        return argumentFailure(ErrorCode::NotInitialized, kWriteOperation, "the frame holds no samples", "frame");
    if (!descriptor->residentSampleType().has_value())
        return argumentFailure(ErrorCode::Unsupported, kWriteOperation,
                               "a snapshot holds the eight resident sample types, and the frame's is not one", "frame");
    if (descriptor->byteOrder() == ByteOrder::Big && isRawByteOrderMeaningful(descriptor->sampleFormat()))
        return argumentFailure(ErrorCode::InvalidArgument, kWriteOperation,
                               "a snapshot stores samples little-endian, and the frame's are big-endian", "frame");
    const ImageExtent extent = descriptor->extent();
    std::uint32_t validityPlanes = 0u;
    if (validity != nullptr)
    {
        const std::optional<ImageExtent> covered = validity->extent();
        if (!validity->valid() || !covered.has_value())
            return argumentFailure(ErrorCode::NotInitialized, kWriteOperation, "the validity mask holds nothing",
                                   "validity");
        if (covered->width() != extent.width() || covered->height() != extent.height() ||
            (covered->channelCount() != 1u && covered->channelCount() != extent.channelCount()))
            return argumentFailure(ErrorCode::IncompatibleArguments, kWriteOperation,
                                   "the validity mask does not cover the frame with one plane or one per channel",
                                   "validity");
        validityPlanes = covered->channelCount();
    }
    else if (metadata.semantics().validity().needsMask())
        return argumentFailure(ErrorCode::IncompatibleArguments, kWriteOperation,
                               "the metadata's validity rule is a mask, and no mask was given", "validity");

    const std::optional<ImageLayout> packed = ImageLayout::create(
        {extent, source->elementSizeBytes(), source->samplesPerPixel(), source->planeCount(), 1u}, engine.limits());
    if (!packed.has_value())
        return argumentFailure(ErrorCode::LimitExceeded, kWriteOperation,
                               "the frame's packed layout is beyond the engine context's limits", "frame");

    // The metadata, measured, bounded, encoded and hashed.
    const std::uint64_t metadataBytes = encodedSnapshotMetadataBytes(*request.identity, metadata);
    if (metadataBytes > kMaxImageSnapshotMetadataBytes)
        return Status::failure(
            ErrorCode::LimitExceeded,
            StatusDetail{.message = "the metadata is longer than a snapshot holds",
                         .operation = kWriteOperation,
                         .domain = ErrorDomain::Io,
                         .context = {StatusContextField::ofCount(ContextKey::Requested, metadataBytes),
                                     StatusContextField::ofCount(ContextKey::Limit, kMaxImageSnapshotMetadataBytes)}});
    EngineBuffer encoded;
    if (const Status taken = encoded.allocate(engine, metadataBytes); taken.failed())
        return taken;
    encodeSnapshotMetadata(*request.identity, metadata, encoded.bytes());

    // The mask, packed with its pad bits cleared.
    EngineBuffer mask;
    if (validity != nullptr)
    {
        const ValidityMaskView<const std::uint8_t> view = *validity->view();
        const std::uint64_t rowBytes = view.minimumRowBytes();
        if (const Status taken = mask.allocate(engine, rowBytes * view.height() * view.planeCount()); taken.failed())
            return taken;
        std::byte* at = mask.bytes().data();
        const std::uint8_t last = packedMaskLastByteMask(view.width());
        for (std::uint32_t plane = 0u; plane < view.planeCount(); ++plane)
            for (std::uint32_t y = 0u; y < view.height(); ++y)
            {
                const std::span<const std::uint8_t> row = *view.row(plane, y);
                std::memcpy(at, row.data(), row.size());
                at[row.size() - 1u] = static_cast<std::byte>(row.back() & last);
                at += row.size();
            }
    }
    // The samples' checksum is taken as they are written, so the header is written first with none and again last.
    SnapshotHeader header{.fileBytes =
                              kSnapshotHeaderBytes + metadataBytes + packed->totalBytes() + mask.bytes().size(),
                          .metadataBytes = metadataBytes,
                          .sampleBytes = packed->totalBytes(),
                          .validityBytes = mask.bytes().size(),
                          .descriptor = *descriptor,
                          .layout = *packed,
                          .validityPlanes = validityPlanes,
                          .metadataChecksum = snapshotChecksum(encoded.bytes()),
                          .sampleChecksum = 0u,
                          .validityChecksum = validity != nullptr ? snapshotChecksum(mask.bytes()) : 0u};
    std::array<std::byte, kSnapshotHeaderBytes> headerBytes = encodeSnapshotHeader(header);

    // The pending sibling the output transaction would use, so a sweep recovers what a crash leaves.
    const std::string destination(request.destinationPath);
    const std::string pending = destination + "." + std::to_string(currentProcessIdentity()) + "-" +
                                std::to_string(engine.id()) + "-" + std::to_string(execution.operationId()) +
                                std::string(kPendingOutputSuffix);
    if (pending.size() > kMaxBoundaryPathBytes)
        return Status::failure(
            ErrorCode::LimitExceeded,
            StatusDetail{.message = "the pending sibling's path would exceed the path bound",
                         .operation = kWriteOperation,
                         .domain = ErrorDomain::Io,
                         .context = {StatusContextField::ofCount(ContextKey::Requested, pending.size()),
                                     StatusContextField::ofCount(ContextKey::Limit, kMaxBoundaryPathBytes)}});
    const std::filesystem::path destinationPath = outputPathFromUtf8(destination);
    const std::filesystem::path pendingPath = outputPathFromUtf8(pending);
    std::error_code probed;
    const bool existed = std::filesystem::exists(destinationPath, probed);
    if (existed && request.replacePolicy == OutputReplacePolicy::RefuseExisting)
        return snapshotFailure(ErrorCode::InvalidState, CodecFailure::ExistingDestination, kWriteOperation,
                               "the destination exists and the request refuses to replace it", destination);
    if (const Status stopped = execution.checkCancellation(); stopped.failed())
        return stopped;

    Result<EngineContextLease> lease = engine.acquireLease(0u);
    if (!lease.hasValue())
        return lease.status();
    PendingGuard guard(pendingPath);
    guard.pendingFile = std::make_unique<OutputFile>(pendingPath, OutputFileMode::Create);
    OutputFile& file = *guard.pendingFile;
    if (const std::error_code opened = file.openError(); opened)
    {
        // A pending file that already exists is another writer's, and is not this write's to remove.
        if (opened == std::errc::file_exists)
        {
            guard.published();
            return snapshotFailure(ErrorCode::Busy, CodecFailure::PendingConflict, kWriteOperation,
                                   "the pending sibling exists: another write of this destination is running, or an "
                                   "abandoned one was never swept",
                                   pending);
        }
        return Status::failure(
            ErrorCode::ExternalFailure,
            StatusDetail{.message = "the pending sibling could not be created beside the destination",
                         .operation = kWriteOperation,
                         .domain = ErrorDomain::Io,
                         .context = {StatusContextField::ofCount(
                                         ContextKey::CodecReason,
                                         static_cast<std::uint64_t>(CodecFailure::DestinationUnavailable)),
                                     StatusContextField::ofText(ContextKey::Path, pending),
                                     StatusContextField::ofNumber(ContextKey::LibraryCode, opened.value())}});
    }
    file.reserve(header.fileBytes);
    if (const Status written = writeAll(file, headerBytes, execution, pending); written.failed())
        return written;
    if (const Status written = writeAll(file, encoded.bytes(), execution, pending); written.failed())
        return written;
    SnapshotBlockChecksum samples;
    if (isPacked(*source, *packed))
    {
        if (const Status written = writeAll(file, frame.bytes().first(static_cast<std::size_t>(packed->totalBytes())),
                                            execution, pending, &samples);
            written.failed())
            return written;
    }
    else
    {
        // Rows are gathered into transfers rather than written one call each.
        const std::uint64_t rowBytes = packed->rowStrideBytes();
        EngineBuffer staging;
        if (const Status taken =
                staging.allocate(engine, (std::min)(packed->totalBytes(), (std::max)(kTransferBytes, rowBytes)));
            taken.failed())
            return taken;
        std::uint64_t held = 0u;
        Status gathered;
        forEachRow(frame, *packed, [&](std::span<const std::byte> row) {
            if (gathered.failed())
                return;
            if (held + rowBytes > staging.bytes().size())
            {
                gathered =
                    writeAll(file, staging.bytes().first(static_cast<std::size_t>(held)), execution, pending, &samples);
                held = 0u;
            }
            std::memcpy(staging.bytes().data() + held, row.data(), row.size());
            held += rowBytes;
        });
        if (gathered.failed())
            return gathered;
        if (const Status written =
                writeAll(file, staging.bytes().first(static_cast<std::size_t>(held)), execution, pending, &samples);
            written.failed())
            return written;
    }
    if (const Status written = writeAll(file, mask.bytes(), execution, pending); written.failed())
        return written;
    header.sampleChecksum = samples.digest();
    headerBytes = encodeSnapshotHeader(header);
    if (file.rewind() != 0)
        return snapshotFailure(ErrorCode::ExternalFailure, CodecFailure::ShortWrite, kWriteOperation,
                               "the pending snapshot's header could not be rewritten", pending);
    if (const Status written = writeAll(file, headerBytes, execution, pending); written.failed())
        return written;

    std::error_code sized;
    if (file.size(sized) != header.fileBytes || sized)
        return snapshotFailure(ErrorCode::InvalidData, CodecFailure::ShortWrite, kWriteOperation,
                               "the pending snapshot does not hold the bytes written to it", pending);
    const bool flush = request.durability == OutputDurability::FlushBeforePublish;
    if (flush)
        if (const int flushed = file.flush(); flushed != 0)
            return Status::failure(
                ErrorCode::ExternalFailure,
                StatusDetail{
                    .message = "the pending snapshot could not be flushed to stable storage",
                    .operation = kWriteOperation,
                    .domain = ErrorDomain::Io,
                    .context = {StatusContextField::ofCount(ContextKey::CodecReason,
                                                            static_cast<std::uint64_t>(CodecFailure::FlushFailure)),
                                StatusContextField::ofText(ContextKey::Path, pending),
                                StatusContextField::ofNumber(ContextKey::LibraryCode, flushed)}});
    if (const Status stopped = execution.checkCancellation(); stopped.failed())
        return stopped;
    if (const int closed = file.close(); closed != 0)
        return snapshotFailure(ErrorCode::ExternalFailure, CodecFailure::ShortWrite, kWriteOperation,
                               "the pending snapshot could not be closed", pending);
    const std::filesystem::path directory = flush ? destinationPath.parent_path() : std::filesystem::path{};
    if (const Status published = publishOutputFile(file, execution, destinationPath, kWriteOperation);
        published.failed())
    {
        if (published.code() != ErrorCode::ExternalFailure)
            return published;
        return snapshotFailure(ErrorCode::ExternalFailure, CodecFailure::PublishFailure, kWriteOperation,
                               "the pending snapshot could not replace the destination", destination, published);
    }
    guard.published();
    lease->release();
    if (flush)
        syncDirectoryBestEffort(directory);
    execution.reportTrace(TraceEventKind::Counter, "snapshot", "bytesWritten", static_cast<double>(header.fileBytes),
                          "bytes");
    return ImageSnapshotWriteReport{.bytesWritten = header.fileBytes, .flushed = flush, .replacedExisting = existed};
}

[[nodiscard]] Result<ImageSnapshotContents> readSnapshot(const EngineContext& engine, const ExecutionContext& execution,
                                                         std::string_view path)
{
    if (const Status checked = checkContexts(engine, execution, path, kReadOperation); checked.failed())
        return checked;
    OpenedSnapshot opened;
    if (const Status open = openSnapshot(engine, execution, path, kReadOperation, opened); open.failed())
        return open;
    const SnapshotHeader& header = *opened.header;

    Result<SampleBuffer> buffer =
        SampleBuffer::createOwned(engine, header.layout, StorageInitialization::Uninitialized);
    if (!buffer.hasValue())
        return buffer.status();
    const std::span<std::byte> samples = buffer->mutableBytes()->first(static_cast<std::size_t>(header.sampleBytes));
    // Each block is read and hashed while it is still in cache. A read of several blocks is shared by as many of
    // the operation's workers as it has blocks, never more than were admitted, and every worker but the first reads
    // through a handle of its own: one synchronous handle makes its transfers wait for each other.
    const std::uint64_t blocks = (header.sampleBytes + kSnapshotHashBlockBytes - 1u) / kSnapshotHashBlockBytes;
    std::vector<std::uint64_t> digests(static_cast<std::size_t>(blocks));
    const auto readBlock = [&](SnapshotInput& file, std::uint64_t block) -> Status {
        const std::uint64_t offset = block * kSnapshotHashBlockBytes;
        const std::span<std::byte> part =
            samples.subspan(static_cast<std::size_t>(offset),
                            static_cast<std::size_t>((std::min)(kSnapshotHashBlockBytes, header.sampleBytes - offset)));
        SnapshotChecksum checksum;
        for (std::uint64_t done = 0u; done < part.size(); done += kHashedTransferBytes)
        {
            const std::span<std::byte> piece = part.subspan(
                static_cast<std::size_t>(done),
                static_cast<std::size_t>((std::min)(kHashedTransferBytes, std::uint64_t{part.size()} - done)));
            if (const Status got = file.readAt(piece, snapshotSampleOffset(header) + offset + done, execution,
                                               CodecFailure::PayloadUnreadable, path);
                got.failed())
                return got;
            checksum.update(piece);
        }
        digests[static_cast<std::size_t>(block)] = checksum.digest();
        return {};
    };
    const std::uint64_t workers = (std::min)(std::uint64_t{(std::max)(1u, execution.admittedConcurrency())}, blocks);
    if (workers == 1u)
    {
        for (std::uint64_t block = 0u; block < blocks; ++block)
            if (const Status got = readBlock(opened.file, block); got.failed())
                return got;
    }
    else
    {
        std::vector<std::unique_ptr<SnapshotInput>> handles(static_cast<std::size_t>(workers));
        const Status shared = parallelForWorkers(
            execution, workers, {.bytesPerItem = kSnapshotHashBlockBytes}, [&](WorkerTask task) -> Status {
                SnapshotInput* file = &opened.file;
                if (task.workerSlot != 0u)
                {
                    std::unique_ptr<SnapshotInput>& own = handles[task.workerSlot];
                    if (!own)
                    {
                        own = std::make_unique<SnapshotInput>();
                        if (!own->reopen(opened.file))
                            return snapshotFailure(ErrorCode::ExternalFailure, CodecFailure::PayloadUnreadable,
                                                   kReadOperation, "a worker could not open the snapshot", path);
                    }
                    file = own.get();
                }
                for (std::uint64_t run = task.range.begin; run < task.range.end; ++run)
                    for (std::uint64_t block = run * blocks / workers; block < (run + 1u) * blocks / workers; ++block)
                        if (const Status got = readBlock(*file, block); got.failed())
                            return got;
                return Status{};
            });
        if (shared.failed())
            return shared;
    }
    if (combineSnapshotBlockDigests(digests) != header.sampleChecksum)
        return snapshotFailure(ErrorCode::InvalidData, CodecFailure::PayloadUnreadable, kReadOperation,
                               "the snapshot's samples fail their checksum", path);

    std::optional<ValidityMask> validity;
    if (header.validityPlanes != 0u)
    {
        EngineBuffer section;
        if (const Status taken = section.allocate(engine, header.validityBytes); taken.failed())
            return taken;
        if (const Status got = opened.file.readAt(section.bytes(), snapshotValidityOffset(header), execution,
                                                  CodecFailure::PayloadUnreadable, path);
            got.failed())
            return got;
        if (snapshotChecksum(section.bytes()) != header.validityChecksum)
            return snapshotFailure(ErrorCode::InvalidData, CodecFailure::PayloadUnreadable, kReadOperation,
                                   "the snapshot's mask fails its checksum", path);
        const ImageExtent extent = header.descriptor.extent();
        Result<ValidityMask> mask = ValidityMask::create(
            engine, *ImageExtent::create(extent.width(), extent.height(), header.validityPlanes, engine.limits()));
        if (!mask.hasValue())
            return mask.status();
        const ValidityMaskView<std::uint8_t> view = *mask->view();
        const std::uint64_t rowBytes = view.minimumRowBytes();
        const std::uint8_t last = packedMaskLastByteMask(view.width());
        const std::byte* at = section.bytes().data();
        for (std::uint32_t plane = 0u; plane < view.planeCount(); ++plane)
            for (std::uint32_t y = 0u; y < view.height(); ++y)
            {
                const std::span<std::uint8_t> row = *view.row(plane, y);
                std::memcpy(row.data(), at, static_cast<std::size_t>(rowBytes));
                if ((row.back() & static_cast<std::uint8_t>(~last)) != 0u)
                    return snapshotFailure(ErrorCode::InvalidData, CodecFailure::PayloadUnreadable, kReadOperation,
                                           "the snapshot's mask sets a bit past the last pixel of a row", path);
                at += rowBytes;
            }
        validity = std::move(*mask);
    }
    else if (opened.metadata->metadata.semantics().validity().needsMask())
        return snapshotFailure(ErrorCode::InvalidData, CodecFailure::MetadataUnreadable, kReadOperation,
                               "the snapshot's validity rule is a mask, and it holds none", path);

    Result<RawFrame> frame = RawFrame::create(header.descriptor, header.layout, std::move(*buffer));
    if (!frame.hasValue())
        return frame.status();
    execution.reportTrace(TraceEventKind::Counter, "snapshot", "bytesRead", static_cast<double>(header.fileBytes),
                          "bytes");
    return ImageSnapshotContents{.frame = std::move(*frame),
                                 .metadata = std::move(opened.metadata->metadata),
                                 .validity = std::move(validity),
                                 .identity = std::move(opened.metadata->identity)};
}

[[nodiscard]] Result<ImageSnapshotInfo> probeSnapshot(const EngineContext& engine, const ExecutionContext& execution,
                                                      std::string_view path)
{
    if (const Status checked = checkContexts(engine, execution, path, kProbeOperation); checked.failed())
        return checked;
    OpenedSnapshot opened;
    if (const Status open = openSnapshot(engine, execution, path, kProbeOperation, opened); open.failed())
        return open;
    return ImageSnapshotInfo{.formatVersion = opened.header->version,
                             .descriptor = opened.header->descriptor,
                             .layout = opened.header->layout,
                             .validityPlanes = opened.header->validityPlanes,
                             .fileBytes = opened.header->fileBytes,
                             .identity = std::move(opened.metadata->identity)};
}
} // namespace

Result<ImageSnapshotWriteReport> writeImageSnapshot(const EngineContext& engine, const ExecutionContext& execution,
                                                    const ImageSnapshotWriteRequest& request, const RawFrame& frame,
                                                    const ImageMetadata& metadata, const ValidityMask* validity)
{
    return guarded("writeImageSnapshot",
                   [&] { return writeSnapshot(engine, execution, request, frame, metadata, validity); });
}

Result<ImageSnapshotInfo> probeImageSnapshot(const EngineContext& engine, const ExecutionContext& execution,
                                             std::string_view path)
{
    return guarded("probeImageSnapshot", [&] { return probeSnapshot(engine, execution, path); });
}

Result<ImageSnapshotContents> readImageSnapshot(const EngineContext& engine, const ExecutionContext& execution,
                                                std::string_view path)
{
    return guarded("readImageSnapshot", [&] { return readSnapshot(engine, execution, path); });
}

} // namespace an
