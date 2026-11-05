// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the developer dump adapter and the reader session the streaming layer shares.
//
// The header is read and written field by field in little-endian order, and the enumerations it
// stores are pinned to their wire codes at compile time, so reordering an enumeration cannot silently
// change the format. Reads use positioned transfers on a handle each read owns - no shared file
// position, no stream buffering - of at most 4 MiB each, and a full-width region is read in strips
// that cover many rows per transfer. A read that holds several whole transfers is shared by as many of
// the operation's workers as it holds, never more than it was admitted, and every worker but the first
// reads through a handle of its own on the same file: Windows makes the transfers made through one
// synchronous handle wait for each other. Writes go through a reserved `CodecOutput` when the output
// transaction supplies one and to the pending path otherwise.

#include <AfterNightEngine/IO/DumpCodec.h>

#include <AfterNightEngine/Core/ExceptionBoundary.h>
#include <AfterNightEngine/IO/CodecError.h>
#include <AfterNightEngine/IO/DumpReader.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <AfterNightEngine/Runtime/TaskScheduler.h>
#include <AfterNightEngine/Runtime/TraceSink.h>

#include "io/registry/PreparedCodecRead.h"
#include "io/registry/PayloadForm.h"
#include "io/registry/PreparedCodecWrite.h"
#include "io/dump/DumpReaderSession.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <fstream>
#include <filesystem>
#include <limits>
#include <memory>
#include <mutex>
#include <cstring>
#include <optional>
#include <string_view>
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

namespace an
{
namespace
{
// Wire fields are encoded individually. Neither C++ layout nor host endian is a file format.
constexpr std::size_t kHeaderBytes = 128u;
constexpr std::uint64_t kTransferBytes = 4u * 1024u * 1024u;
constexpr std::array<unsigned char, 8> kMagic{'A', 'N', 'D', 'U', 'M', 'P', '\r', '\n'};
using Header = std::array<unsigned char, kHeaderBytes>;

// Version 1 stores these codes. Refuse a future enum reorder until its wire mapping is explicit.
template <class Enum, std::size_t Count> constexpr bool wireCodes(const std::array<Enum, Count>& values)
{
    for (std::size_t i = 0u; i < Count; ++i)
        if (static_cast<std::size_t>(values[i]) != i)
            return false;
    return true;
}
static_assert(wireCodes(std::array{RawSampleClass::UnsignedInteger, RawSampleClass::SignedInteger,
                                   RawSampleClass::FloatingPoint, RawSampleClass::Opaque}));
static_assert(wireCodes(std::array{ByteOrder::Native, ByteOrder::Little, ByteOrder::Big}));
static_assert(wireCodes(std::array{ChannelSchema::Mono, ChannelSchema::Rgb, ChannelSchema::Rgba, ChannelSchema::Xyz,
                                   ChannelSchema::Lab, ChannelSchema::Lch, ChannelSchema::Hsl, ChannelSchema::Hsv,
                                   ChannelSchema::Hsi, ChannelSchema::YCbCr, ChannelSchema::Named}));
static_assert(wireCodes(std::array{ColorSpace::Unspecified, ColorSpace::LinearRgb, ColorSpace::DisplayRgb,
                                   ColorSpace::CieXyz, ColorSpace::CieLab, ColorSpace::NarrowbandMapping}));
static_assert(wireCodes(std::array{NumericDomain::NormalizedLinear, NumericDomain::EncodedNonlinear,
                                   NumericDomain::RawSensorCode, NumericDomain::PhysicalScaled}));
static_assert(wireCodes(std::array{CoordinateOrigin::TopLeft, CoordinateOrigin::BottomLeft}));
static_assert(wireCodes(std::array{CfaPattern::None, CfaPattern::Rggb, CfaPattern::Bggr, CfaPattern::Grbg,
                                   CfaPattern::Gbrg, CfaPattern::XTrans}));
static_assert(wireCodes(std::array{CfaColor::Red, CfaColor::Green, CfaColor::Blue}));

std::uint64_t get(const Header& header, std::size_t offset, std::size_t count)
{
    std::uint64_t result = 0u;
    for (std::size_t i = 0u; i < count; ++i)
        result |= std::uint64_t{header[offset + i]} << (i * 8u);
    return result;
}
void put(Header& header, std::size_t offset, std::size_t count, std::uint64_t value)
{
    for (std::size_t i = 0u; i < count; ++i)
        header[offset + i] = static_cast<unsigned char>(value >> (i * 8u));
}
Status failure(CodecFailure reason, std::string_view path, ErrorCode code = ErrorCode::InvalidData)
{
    return codecFailure(code, reason, CodecFormat::Dump, "developer dump", path);
}
Result<CodecImageInfo> parseHeader(const Header& h, const EngineLimits& limits, std::string_view path)
{
    if (!std::equal(kMagic.begin(), kMagic.end(), h.begin()) || get(h, 8u, 4u) != 1u ||
        get(h, 12u, 4u) != kHeaderBytes || get(h, 38u, 2u) != 0u ||
        std::any_of(h.begin() + 116, h.end(), [](auto b) { return b != 0u; }))
        return failure(CodecFailure::MetadataUnreadable, path);
    const auto extent =
        ImageExtent::create(static_cast<std::uint32_t>(get(h, 16u, 4u)), static_cast<std::uint32_t>(get(h, 20u, 4u)),
                            static_cast<std::uint32_t>(get(h, 24u, 4u)), limits);
    if (!extent)
        return failure(CodecFailure::MetadataUnreadable, path, ErrorCode::LimitExceeded);
    const RawSampleFormat format{h[28], static_cast<RawSampleClass>(h[29])};
    const auto order = static_cast<ByteOrder>(h[30]);
    // Wider samples name a physical byte order, never the writer's native order implicitly.
    if (isRawByteOrderMeaningful(format) && order == ByteOrder::Native)
        return failure(CodecFailure::MetadataUnreadable, path);
    const auto pattern = static_cast<CfaPattern>(h[35]);
    std::optional<CfaDescriptor> cfa;
    if (pattern == CfaPattern::XTrans)
    {
        CfaColorEntries entries{};
        for (std::size_t i = 0u; i < 36u; ++i)
            entries[i] = static_cast<CfaColor>(h[80u + i]);
        cfa = CfaDescriptor::create(entries, h[36], h[37]);
    }
    else
    {
        if (std::any_of(h.begin() + 80, h.begin() + 116, [](auto b) { return b != 0u; }))
            return failure(CodecFailure::MetadataUnreadable, path);
        cfa = CfaDescriptor::create(pattern, h[36], h[37]);
    }
    if (!cfa)
        return failure(CodecFailure::MetadataUnreadable, path);
    const auto descriptor =
        RawFrameDescriptor::create({*extent, format, order, static_cast<ChannelSchema>(h[31]),
                                    static_cast<ColorSpace>(h[32]), static_cast<NumericDomain>(h[33]), *cfa,
                                    static_cast<CoordinateOrigin>(h[34]), std::bit_cast<double>(get(h, 40u, 8u))});
    if (!descriptor)
        return failure(CodecFailure::MetadataUnreadable, path);
    const auto layout = ImageLayout::create({*extent, format.sizeBytes, static_cast<std::uint32_t>(get(h, 48u, 4u)),
                                             static_cast<std::uint32_t>(get(h, 52u, 4u)), 1u},
                                            limits);
    if (!layout || layout->rowStrideBytes() != get(h, 56u, 8u) || layout->planeStrideBytes() != get(h, 64u, 8u) ||
        layout->totalBytes() != get(h, 72u, 8u) ||
        layout->totalBytes() > static_cast<std::uint64_t>((std::numeric_limits<std::streamoff>::max)()) - kHeaderBytes)
        return failure(CodecFailure::MetadataUnreadable, path);
    return CodecImageInfo{*descriptor, *layout};
}
Header makeHeader(const RawFrameDescriptor& d, const ImageLayout& layout)
{
    Header h{};
    std::copy(kMagic.begin(), kMagic.end(), h.begin());
    put(h, 8u, 4u, 1u);
    put(h, 12u, 4u, kHeaderBytes);
    put(h, 16u, 4u, d.extent().width());
    put(h, 20u, 4u, d.extent().height());
    put(h, 24u, 4u, d.extent().channelCount());
    h[28] = d.sampleFormat().sizeBytes;
    h[29] = static_cast<unsigned char>(d.sampleFormat().sampleClass);
    auto order = d.byteOrder();
    if (isRawByteOrderMeaningful(d.sampleFormat()) && order == ByteOrder::Native)
        order = nativeByteOrder();
    h[30] = static_cast<unsigned char>(order);
    h[31] = static_cast<unsigned char>(d.channelSchema());
    h[32] = static_cast<unsigned char>(d.colorSpace());
    h[33] = static_cast<unsigned char>(d.numericDomain());
    h[34] = static_cast<unsigned char>(d.coordinateOrigin());
    h[35] = static_cast<unsigned char>(d.cfa().pattern());
    h[36] = static_cast<unsigned char>(d.cfa().originX());
    h[37] = static_cast<unsigned char>(d.cfa().originY());
    put(h, 40u, 8u, std::bit_cast<std::uint64_t>(d.pixelAspectRatio()));
    put(h, 48u, 4u, layout.samplesPerPixel());
    put(h, 52u, 4u, layout.planeCount());
    put(h, 56u, 8u, layout.rowStrideBytes());
    put(h, 64u, 8u, layout.planeStrideBytes());
    put(h, 72u, 8u, layout.totalBytes());
    if (d.cfa().hasFilterTable())
        for (std::size_t i = 0u; i < 36u; ++i)
            h[80u + i] = static_cast<unsigned char>(d.cfa().filterTable().entries()[i]);
    return h;
}

// Each reader owns its handle. Positioned transfers bypass stream conversion and separate seeks.
class DumpInput
{
public:
    DumpInput() = default;
    DumpInput(const DumpInput&) = delete;
    DumpInput& operator=(const DumpInput&) = delete;
    bool open(std::string_view path)
    {
        const auto native =
            std::filesystem::path(std::u8string_view(reinterpret_cast<const char8_t*>(path.data()), path.size()));
#if defined(_WIN32)
        m_handle = CreateFileW(native.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                               nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_RANDOM_ACCESS, nullptr);
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
    /// Opens a second handle on the file `other` holds, for a worker reading beside it.
    ///
    /// It is opened through `other` rather than by name, so it can only be the same file.
    ///
    /// @param other an input that opened successfully.
    /// @return whether the handle opened.
    bool reopen(const DumpInput& other)
    {
#if defined(_WIN32)
        m_handle = ReOpenFile(other.m_handle, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                              FILE_FLAG_RANDOM_ACCESS);
        m_bytes = other.m_bytes;
        return m_handle != INVALID_HANDLE_VALUE;
#else
        m_handle = ::fcntl(other.m_handle, F_DUPFD_CLOEXEC, 0);
        m_bytes = other.m_bytes;
        return m_handle >= 0;
#endif
    }
    ~DumpInput()
    {
#if defined(_WIN32)
        if (m_handle != INVALID_HANDLE_VALUE)
            CloseHandle(m_handle);
#else
        if (m_handle >= 0)
            ::close(m_handle);
#endif
    }
    std::uint64_t size() const noexcept
    {
        return m_bytes;
    }
    Status readAt(void* destination, std::size_t count, std::uint64_t offset, const ExecutionContext& execution,
                  std::string_view path)
    {
#if defined(_WIN32)
        if (const auto stopped = execution.checkCancellation(); stopped.failed())
            return stopped;
        // The handle is synchronous; OVERLAPPED supplies the offset without a separate seek call.
        OVERLAPPED position{};
        position.Offset = static_cast<DWORD>(offset);
        position.OffsetHigh = static_cast<DWORD>(offset >> 32u);
        DWORD received = 0u;
        if (count > MAXDWORD || !ReadFile(m_handle, destination, static_cast<DWORD>(count), &received, &position) ||
            received != count)
            return failure(CodecFailure::PayloadUnreadable, path);
#else
        auto* bytes = static_cast<char*>(destination);
        while (count != 0u)
        {
            if (const auto stopped = execution.checkCancellation(); stopped.failed())
                return stopped;
            const auto read = ::pread(m_handle, bytes, count, static_cast<off_t>(offset));
            if (read < 0 && errno == EINTR)
                continue;
            if (read <= 0)
                return failure(CodecFailure::PayloadUnreadable, path);
            bytes += read;
            count -= static_cast<std::size_t>(read);
            offset += static_cast<std::uint64_t>(read);
        }
#endif
        return {};
    }

private:
#if defined(_WIN32)
    HANDLE m_handle = INVALID_HANDLE_VALUE;
#else
    int m_handle = -1;
#endif
    std::uint64_t m_bytes = 0u;
};

class DumpOutput
{
public:
    explicit DumpOutput(const std::filesystem::path& path, std::uint64_t fileBytes, CodecOutput* output)
        : m_output(output)
    {
        if (m_output != nullptr)
        {
            m_output->reserve(fileBytes);
            return;
        }
#if defined(_WIN32)
        m_handle = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS,
                               FILE_ATTRIBUTE_NORMAL, nullptr);
        if (m_handle != INVALID_HANDLE_VALUE &&
            fileBytes <= static_cast<std::uint64_t>((std::numeric_limits<LONGLONG>::max)()))
        {
            // Reserve the known extent once instead of extending the allocation at every
            // transfer. This is only a hint: writes still detect a full disk or a short transfer.
            FILE_ALLOCATION_INFO allocation{};
            allocation.AllocationSize.QuadPart = static_cast<LONGLONG>(fileBytes);
            (void)SetFileInformationByHandle(m_handle, FileAllocationInfo, &allocation, sizeof(allocation));
            FILE_END_OF_FILE_INFO extent{};
            extent.EndOfFile.QuadPart = static_cast<LONGLONG>(fileBytes);
            (void)SetFileInformationByHandle(m_handle, FileEndOfFileInfo, &extent, sizeof(extent));
        }
#else
        (void)fileBytes;
        m_stream.rdbuf()->pubsetbuf(nullptr, 0);
        m_stream.open(path, std::ios::binary | std::ios::trunc);
#endif
    }
    DumpOutput(const DumpOutput&) = delete;
    DumpOutput& operator=(const DumpOutput&) = delete;
    ~DumpOutput()
    {
        if (!m_closed)
            (void)close();
    }
    bool isOpen() const noexcept
    {
        if (m_output != nullptr)
            return true;
#if defined(_WIN32)
        return m_handle != INVALID_HANDLE_VALUE;
#else
        return m_stream.is_open();
#endif
    }
    bool write(const void* bytes, std::size_t count)
    {
        if (m_output != nullptr)
            return m_output->write({static_cast<const std::byte*>(bytes), count});
#if defined(_WIN32)
        DWORD written = 0u;
        return count <= MAXDWORD && WriteFile(m_handle, bytes, static_cast<DWORD>(count), &written, nullptr) &&
               written == count;
#else
        return static_cast<bool>(m_stream.write(static_cast<const char*>(bytes), static_cast<std::streamsize>(count)));
#endif
    }
    bool close()
    {
        m_closed = true;
        if (m_output != nullptr)
            return true;
#if defined(_WIN32)
        if (m_handle == INVALID_HANDLE_VALUE)
            return false;
        const bool closed = CloseHandle(m_handle) != 0;
        m_handle = INVALID_HANDLE_VALUE;
        return closed;
#else
        m_stream.close();
        return static_cast<bool>(m_stream);
#endif
    }

private:
    CodecOutput* m_output = nullptr;
#if defined(_WIN32)
    HANDLE m_handle = INVALID_HANDLE_VALUE;
#else
    std::ofstream m_stream;
#endif
    bool m_closed = false;
};

// One preparation retains one open file across preflight and decode.
class DumpRead final : public DumpReaderSession
{
public:
    DumpRead(std::unique_ptr<DumpInput> file, CodecImageInfo info, std::uint64_t fileBytes, std::uint64_t headerBytes)
        : m_file(std::move(file)), m_info(std::move(info)), m_fileBytes(fileBytes), m_headerBytes(headerBytes)
    {
    }
    const CodecImageInfo& info() const noexcept override
    {
        return m_info;
    }
    Status validateSize(std::string_view path) const override
    {
        return m_fileBytes == m_headerBytes + m_info.decodedLayout.totalBytes()
                   ? Status{}
                   : failure(CodecFailure::PayloadUnreadable, path);
    }
    DumpIoCounters snapshot() const noexcept override
    {
        const std::lock_guard lock(m_counterMutex);
        return m_counters;
    }
    Result<CodecDecodeResult> decode(const EngineContext& engine, const ExecutionContext& execution,
                                     const CodecReadRequest& request, const CodecRegion* region) override
    {
        if (const auto checked = validateSize(request.path); checked.failed())
            return checked;
        auto description = m_info.descriptor.toRequest();
        const auto source = m_info.decodedLayout;
        const auto extent = source.extent();
        const auto area = region ? *region : CodecRegion{0u, 0u, extent.width(), extent.height()};
        if (area.width == 0u || area.height == 0u || area.width > extent.width() || area.height > extent.height() ||
            area.x > extent.width() - area.width || area.y > extent.height() - area.height)
            return Status::failure(ErrorCode::OutOfRange);
        description.extent = *ImageExtent::create(area.width, area.height, extent.channelCount(), engine.limits());
        if (description.cfa.isMosaiced())
        {
            const auto side = cfaPatternBlockSize(description.cfa.pattern());
            const auto x = (description.cfa.originX() + area.x % side) % side;
            const auto y = (description.cfa.originY() + area.y % side) % side;
            description.cfa = description.cfa.hasFilterTable()
                                  ? *CfaDescriptor::create(description.cfa.filterTable().entries(), x, y)
                                  : *CfaDescriptor::create(description.cfa.pattern(), x, y);
        }
        const auto layout = *ImageLayout::create(
            {description.extent, source.elementSizeBytes(), source.samplesPerPixel(), source.planeCount(), 1u},
            engine.limits());
        auto buffer = SampleBuffer::createOwned(engine, layout, StorageInitialization::Uninitialized);
        if (!buffer.hasValue())
            return buffer.status();
        auto frame = RawFrame::create(*RawFrameDescriptor::create(description), layout, std::move(*buffer));
        if (!frame.hasValue())
            return frame.status();
        auto bytes = *frame->mutableBytes();
        // A full-width rectangle is contiguous within each plane: one read serves many rows.
        const bool fullWidth = area.width == extent.width();
        const std::uint64_t rowBytes = layout.rowStrideBytes();
        const std::uint64_t rowsPerRead = fullWidth ? (std::max)(std::uint64_t{1}, kTransferBytes / rowBytes) : 1u;
        const std::uint64_t readsPerPlane = (std::uint64_t{area.height} + rowsPerRead - 1u) / rowsPerRead;
        const std::uint64_t readCount = readsPerPlane * source.planeCount();
        std::atomic<std::uint64_t> transfers{0u};
        const auto readOne = [&](DumpInput& file, std::uint64_t index) -> Status {
            const std::uint64_t p = index / readsPerPlane;
            const std::uint64_t y = index % readsPerPlane * rowsPerRead;
            auto count = (std::min)(rowsPerRead, area.height - y) * rowBytes;
            auto offset = m_headerBytes + p * source.planeStrideBytes() + (area.y + y) * source.rowStrideBytes() +
                          std::uint64_t{area.x} * source.samplesPerPixel() * source.elementSizeBytes();
            auto* destination = bytes.data() + p * layout.planeStrideBytes() + y * rowBytes;
            while (count != 0u)
            {
                if (const auto stopped = execution.checkCancellation(); stopped.failed())
                    return stopped;
                const auto chunk = (std::min)(count, kTransferBytes);
                if (const auto read =
                        file.readAt(destination, static_cast<std::size_t>(chunk), offset, execution, request.path);
                    read.failed())
                    return read;
                {
                    const std::lock_guard lock(m_counterMutex);
                    if (m_counters.positionedReads != (std::numeric_limits<std::uint64_t>::max)())
                        ++m_counters.positionedReads;
                    m_counters.payloadBytes +=
                        (std::min)(chunk, (std::numeric_limits<std::uint64_t>::max)() - m_counters.payloadBytes);
                }
                offset += chunk;
                transfers.fetch_add(1u, std::memory_order_relaxed);
                count -= chunk;
                destination += chunk;
            }
            const std::lock_guard lock(m_counterMutex);
            auto& counter = fullWidth ? m_counters.fullWidthStripReads : m_counters.rowReads;
            if (counter != (std::numeric_limits<std::uint64_t>::max)())
                ++counter;
            return {};
        };
        // Copying out of the file cache and faulting in fresh pages is work a worker can share, but
        // each worker costs a handle: one joins for every whole transfer the read holds.
        const std::uint64_t workers =
            (std::min)({std::uint64_t{(std::max)(1u, execution.admittedConcurrency())},
                        (std::max)(std::uint64_t{1}, layout.totalBytes() / kTransferBytes), readCount});
        if (workers == 1u)
        {
            for (std::uint64_t index = 0u; index < readCount; ++index)
                if (const auto read = readOne(*m_file, index); read.failed())
                    return read;
        }
        else
        {
            // Worker slot k reads the k-th of `workers` contiguous runs of reads; slots are dense,
            // and no two concurrent calls share one, so each slot keeps the handle it opened.
            std::vector<std::unique_ptr<DumpInput>> handles(static_cast<std::size_t>(workers));
            const Status shared = parallelForWorkers(
                execution, workers, {.bytesPerItem = kTransferBytes}, [&](WorkerTask task) -> Status {
                    DumpInput* file = m_file.get();
                    if (task.workerSlot != 0u)
                    {
                        auto& own = handles[task.workerSlot];
                        if (!own)
                        {
                            own = std::make_unique<DumpInput>();
                            if (!own->reopen(*m_file))
                                return failure(CodecFailure::PayloadUnreadable, request.path,
                                               ErrorCode::ExternalFailure);
                        }
                        file = own.get();
                    }
                    for (std::uint64_t run = task.range.begin; run < task.range.end; ++run)
                        for (std::uint64_t index = run * readCount / workers; index < (run + 1u) * readCount / workers;
                             ++index)
                            if (const auto read = readOne(*file, index); read.failed())
                                return read;
                    return Status{};
                });
            if (shared.failed())
                return shared;
        }
        execution.reportTrace(TraceEventKind::Counter, "dump", "payloadReadCalls",
                              static_cast<double>(transfers.load(std::memory_order_relaxed)), "calls");
        execution.reportTrace(TraceEventKind::Counter, "dump", "payloadBytesRead",
                              static_cast<double>(layout.totalBytes()), "bytes");
        execution.reportTrace(TraceEventKind::Counter, "dump", "payloadReadWorkers", static_cast<double>(workers),
                              "workers");
        return CodecDecodeResult{std::move(*frame), {}};
    }

private:
    std::unique_ptr<DumpInput> m_file;
    CodecImageInfo m_info;
    std::uint64_t m_fileBytes;
    std::uint64_t m_headerBytes;
    mutable std::mutex m_counterMutex;
    DumpIoCounters m_counters{};
};
Result<std::unique_ptr<DumpRead>> openDump(const EngineContext& engine, const ExecutionContext& execution,
                                           const CodecReadRequest& request)
{
    if (const auto stopped = execution.checkCancellation(); stopped.failed())
        return stopped;
    if (request.imageIndex != 0u)
        return Status::failure(ErrorCode::OutOfRange);
    auto file = std::make_unique<DumpInput>();
    if (!file->open(request.path))
        return failure(CodecFailure::MetadataUnreadable, request.path, ErrorCode::ExternalFailure);
    const auto end = file->size();
    if (end < kHeaderBytes)
        return failure(CodecFailure::MetadataUnreadable, request.path);
    Header header{};
    if (const auto read = file->readAt(header.data(), kHeaderBytes, 0u, execution, request.path); read.failed())
        return failure(CodecFailure::MetadataUnreadable, request.path, read.code());
    auto info = parseHeader(header, engine.limits(), request.path);
    if (!info.hasValue())
        return info.status();
    return std::make_unique<DumpRead>(std::move(file), std::move(*info), static_cast<std::uint64_t>(end), kHeaderBytes);
}
class DumpCodec final : public PreparingImageCodec, public StreamingImageCodec
{
public:
    CodecCapabilities capabilities() const override
    {
        return {CodecFormat::Dump, (1u << 20u) - 1u, (1u << 20u) - 1u, 7u, 7u, 65535u, true};
    }
    Result<std::unique_ptr<PreparedCodecRead>> prepareRead(const EngineContext& engine,
                                                           const ExecutionContext& execution,
                                                           const CodecReadRequest& request) const override
    {
        auto read = openDump(engine, execution, request);
        if (!read.hasValue())
            return read.status();
        return std::unique_ptr<PreparedCodecRead>(std::move(*read));
    }
    Result<CodecImageInfo> probe(const EngineContext& engine, const ExecutionContext& execution,
                                 const CodecReadRequest& request) const override
    {
        auto read = openDump(engine, execution, request);
        if (!read.hasValue())
            return read.status();
        if (request.validatePayload)
            if (const auto checked = (*read)->validateSize(request.path); checked.failed())
                return checked;
        // Exact size proves structural completeness, not disk-sector readability: decode is still required.
        return (*read)->info();
    }
    Result<CodecDecodeResult> decode(const EngineContext& engine, const ExecutionContext& execution,
                                     const CodecReadRequest& request) const override
    {
        return decodePart(engine, execution, request, nullptr);
    }
    Result<CodecDecodeResult> decodeRegion(const EngineContext& engine, const ExecutionContext& execution,
                                           const CodecReadRequest& request, const CodecRegion& region) const override
    {
        return decodePart(engine, execution, request, &region);
    }
    Result<CodecEncodeReport> encode(const EngineContext& engine, const ExecutionContext& execution,
                                     const CodecEncodeRequest& request, const RawFrame& frame,
                                     const ImageMetadata& metadata) const override
    {
        return encodeToFile(engine, execution, request, frame, metadata, nullptr);
    }
    Result<CodecEncodeReport> encodeTo(const EngineContext& engine, const ExecutionContext& execution,
                                       const CodecEncodeRequest& request, const RawFrame& frame,
                                       const ImageMetadata& metadata, CodecOutput& output) const override
    {
        return encodeToFile(engine, execution, request, frame, metadata, &output);
    }

private:
    Result<CodecEncodeReport> encodeToFile(const EngineContext& engine, const ExecutionContext& execution,
                                           const CodecEncodeRequest& request, const RawFrame& frame,
                                           const ImageMetadata& metadata, CodecOutput* output) const
    {
        if (request.intent && (request.intent->compression() != OutputCompression::None ||
                               request.intent->scaling() != OutputScaling::None || !request.intent->cards().empty() ||
                               statesPayloadForm(request.intent)))
            return failure(CodecFailure::UnsupportedOperation, request.pendingPath, ErrorCode::Unsupported);
        if (metadata.semantics() != ImageSemantics{})
            return failure(CodecFailure::InvalidMetadata, request.pendingPath, ErrorCode::Unsupported);
        const auto source = *frame.layout();
        const auto packed = ImageLayout::create(
            {source.extent(), source.elementSizeBytes(), source.samplesPerPixel(), source.planeCount(), 1u},
            engine.limits());
        if (!packed)
            return Status::failure(ErrorCode::LimitExceeded);
        const auto header = makeHeader(*frame.descriptor(), *packed);
        if (const auto stopped = execution.checkCancellation(); stopped.failed())
            return stopped;
        DumpOutput file(std::filesystem::path(std::u8string_view(
                            reinterpret_cast<const char8_t*>(request.pendingPath.data()), request.pendingPath.size())),
                        kHeaderBytes + packed->totalBytes(), output);
        if (!file.isOpen())
            return failure(CodecFailure::DestinationUnavailable, request.pendingPath, ErrorCode::ExternalFailure);
        if (!file.write(header.data(), header.size()))
            return failure(CodecFailure::ShortWrite, request.pendingPath, ErrorCode::ExternalFailure);
        for (std::uint32_t p = 0u; p < source.planeCount(); ++p)
            for (std::uint32_t y = 0u; y < source.extent().height();)
            {
                const auto rows = source.rowStrideBytes() == packed->rowStrideBytes()
                                      ? std::uint64_t{source.extent().height() - y}
                                      : 1u;
                auto count = rows * packed->rowStrideBytes();
                auto* data = frame.bytes().data() + p * source.planeStrideBytes() + y * source.rowStrideBytes();
                while (count != 0u)
                {
                    if (const auto stopped = execution.checkCancellation(); stopped.failed())
                        return stopped;
                    const auto chunk = (std::min)(count, kTransferBytes);
                    if (!file.write(data, static_cast<std::size_t>(chunk)))
                        return failure(CodecFailure::ShortWrite, request.pendingPath, ErrorCode::ExternalFailure);
                    count -= chunk;
                    data += chunk;
                }
                y += static_cast<std::uint32_t>(rows);
            }
        if (!file.close())
            return failure(CodecFailure::ShortWrite, request.pendingPath, ErrorCode::ExternalFailure);
        return CodecEncodeReport{kHeaderBytes + packed->totalBytes()};
    }

private:
    Result<CodecDecodeResult> decodePart(const EngineContext& engine, const ExecutionContext& execution,
                                         const CodecReadRequest& request, const CodecRegion* region) const
    {
        auto read = openDump(engine, execution, request);
        if (!read.hasValue())
            return read.status();
        return (*read)->decode(engine, execution, request, region);
    }
};
} // namespace
Result<std::unique_ptr<DumpReaderSession>>
openDumpReaderSession(const EngineContext& engine, const ExecutionContext& execution, std::string_view path)
{
    auto opened = openDump(engine, execution, {.path = path, .format = CodecFormat::Dump});
    if (!opened)
        return opened.status();
    return std::unique_ptr<DumpReaderSession>(std::move(*opened));
}

Result<std::shared_ptr<const ImageCodec>> createDumpCodec()
{
    return evaluateAcrossBoundary({"dump", "createDumpCodec", ErrorDomain::Io},
                                  []() -> std::shared_ptr<const ImageCodec> { return std::make_shared<DumpCodec>(); });
}
} // namespace an
