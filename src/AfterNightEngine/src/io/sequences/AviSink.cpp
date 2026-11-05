// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the uncompressed AVI sink, appending frames into the RIFF sections of a pending output.
//
// Everything that can refuse - the layout, the descriptor, the two memory blocks - is decided before
// the pending payload is opened, and the header is then written once with empty counts. Frames follow
// in order. When a section fills, its standard index is written after its frames (the first section's
// `idx1` index too), a later section's opening bytes are rewritten in place with its final sizes, and
// the section's master index entry is recorded in the header held in memory. `finish` writes that
// header over the first one, closes the file and only then completes the output's claim, so a
// container is committed whole or not at all; an unfinished one is truncated to nothing.
//
// The container stores rows bottom-up as blue, green and red padded to four bytes. A grey frame that
// already has that shape is written straight from its own bytes; any other is converted row by row
// through a scratch block. Every write goes out in transfers of at most `kTransferBytes` with a
// cancellation check before each, and the sink reports trace counters but no progress, because the
// caller counts frames.

#include <AfterNightEngine/IO/AviSequence.h>

#include <AfterNightEngine/IO/SequenceOutput.h>
#include <AfterNightEngine/Runtime/TraceSink.h>

#include "io/sequences/AccountedBlock.h"
#include "io/sequences/AviLayout.h"
#include "io/sequences/SequenceFile.h"
#include "io/sequences/SequenceInternal.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>

namespace an
{
namespace
{
using avi_detail::Layout;
using sequence_detail::AccountedBlock;

/// The fewest bytes of scratch a sink keeps, so an index batch always holds many entries.
constexpr std::uint64_t kMinimumScratchBytes = 64u * 1024u;

class AviSinkBackend final : public FrameSinkBackend
{
public:
    AviSinkBackend(SequenceFile file, SequenceOutputClaim claim, const Layout& layout, AccountedBlock header,
                   AccountedBlock scratch)
        : m_file(std::move(file)), m_claim(std::move(claim)), m_pendingPath(m_claim.pendingPath()), m_layout(layout),
          m_header(std::move(header)), m_scratch(std::move(scratch)), m_position(layout.headerBytes)
    {
    }

    ~AviSinkBackend() override
    {
        (void)discard();
        m_claim.release();
    }

    AviSinkBackend(const AviSinkBackend&) = delete;
    AviSinkBackend& operator=(const AviSinkBackend&) = delete;

    Result<SequenceWriteResult> append(const EngineContext&, const ExecutionContext& execution,
                                       const SequenceFrame& frame) override
    {
        if (m_frames >= m_layout.maximumFrames)
            return avi_detail::aviFailure(ErrorCode::LimitExceeded, SequenceFailure::UnsupportedEncoding,
                                          "the master index has no room for the RIFF section another frame needs",
                                          "frameCount", m_pendingPath,
                                          {StatusContextField::ofCount(ContextKey::Requested, m_frames + 1u),
                                           StatusContextField::ofCount(ContextKey::Limit, m_layout.maximumFrames)});
        const ImageLayout layout = *frame.frame.layout();
        if (!storable(layout))
            return avi_detail::aviFailure(ErrorCode::Unsupported, SequenceFailure::UnsupportedEncoding,
                                          "an uncompressed AVI stores one plane of samples or three planes of RGB",
                                          "layout", m_pendingPath);
        if (m_sectionFrames == sectionCapacity())
        {
            if (const Status closed = closeSection(execution); closed.failed())
                return closed;
            if (const Status opened = openSection(execution); opened.failed())
                return opened;
        }
        if (const Status written = writeFrame(execution, frame.frame, layout); written.failed())
            return written;
        ++m_frames;
        ++m_sectionFrames;
        execution.reportTrace(TraceEventKind::Counter, "avi", "frame-bytes-written",
                              static_cast<double>(avi_detail::chunkBytes(m_layout)), "bytes");
        return SequenceWriteResult{m_frames, m_position};
    }

    Result<SequenceWriteResult> finish(const ExecutionContext& execution) override
    {
        if (const Status stopped = execution.checkCancellation(); stopped.failed())
            return stopped;
        if (const Status closed = closeSection(execution); closed.failed())
            return closed;
        const avi_detail::HeaderCounts counts{m_frames, m_firstSectionFrames, m_firstSectionBytes, m_sectionsInUse};
        avi_detail::encodeHeader(headerBytes(), m_layout, counts);
        if (const int error = m_file.writeAt(headerBytes(), 0u); error != 0)
            return avi_detail::ioFailure(error, "the AVI header could not be written", m_pendingPath);
        if (const int error = m_file.close(); error != 0)
            return avi_detail::ioFailure(error, "the AVI container could not be closed", m_pendingPath);
        m_finished = true;
        m_header.release();
        m_scratch.release();
        // The payload is complete and closed, so its output may commit it.
        m_claim.complete();
        m_claim.release();
        return SequenceWriteResult{m_frames, m_position};
    }

    Status close() override
    {
        const int error = discard();
        m_claim.release();
        if (error != 0)
            return avi_detail::ioFailure(error, "the unfinished AVI container could not be discarded", m_pendingPath);
        return Status::success();
    }

private:
    /// Discards an unfinished container down to nothing and closes it, reporting only a platform code, so
    /// the destructor can call it without building a failure. The output's reservation stands.
    int discard() noexcept
    {
        m_header.release();
        m_scratch.release();
        if (m_finished || !m_file.isOpen())
            return 0;
        const int truncated = m_file.truncate();
        const int closed = m_file.close();
        return truncated != 0 ? truncated : closed;
    }

    [[nodiscard]] std::span<std::byte> headerBytes() const noexcept
    {
        return {m_header.bytes(), static_cast<std::size_t>(m_header.size())};
    }

    [[nodiscard]] bool storable(const ImageLayout& layout) const noexcept
    {
        if (m_layout.bytesPerPixel == 1u)
            return layout.planeCount() == 1u && layout.samplesPerPixel() == 1u;
        return (layout.planeCount() == 1u && layout.samplesPerPixel() == 3u) ||
               (layout.planeCount() == 3u && layout.samplesPerPixel() == 1u);
    }

    [[nodiscard]] std::uint64_t sectionCapacity() const noexcept
    {
        return m_section == 0u ? m_layout.firstSectionFrames : m_layout.laterSectionFrames;
    }

    /// Writes bytes at the current position in transfers of at most the transfer bound, checking the
    /// operation's cancellation before each.
    Status writeTransfers(const ExecutionContext& execution, std::span<const std::byte> bytes)
    {
        while (!bytes.empty())
        {
            if (const Status stopped = execution.checkCancellation(); stopped.failed())
                return stopped;
            const auto chunk = static_cast<std::size_t>(
                (std::min)(static_cast<std::uint64_t>(bytes.size()), avi_detail::kTransferBytes));
            if (const int error = m_file.writeAt(bytes.first(chunk), m_position); error != 0)
                return avi_detail::ioFailure(error, "the AVI container could not be written", m_pendingPath);
            m_position += chunk;
            execution.reportTrace(TraceEventKind::Counter, "avi", "transfer-bytes", static_cast<double>(chunk),
                                  "bytes");
            bytes = bytes.subspan(chunk);
        }
        return Status::success();
    }

    /// Writes `totalBytes` of fixed-size records encoded batch by batch into the scratch block.
    template <typename EncodeT>
    Status writeRecords(const ExecutionContext& execution, std::uint64_t totalBytes, std::uint64_t recordBytes,
                        EncodeT encode)
    {
        const std::uint64_t batch = (m_scratch.size() / recordBytes) * recordBytes;
        std::uint64_t done = 0u;
        while (done < totalBytes)
        {
            const auto now = static_cast<std::size_t>((std::min)(batch, totalBytes - done));
            const std::span<std::byte> target(m_scratch.bytes(), now);
            encode(target, done / recordBytes);
            if (const Status written = writeTransfers(execution, target); written.failed())
                return written;
            done += now;
        }
        return Status::success();
    }

    /// Writes the open section's standard index and, for the first section, the `idx1` index, and states
    /// the section's final sizes.
    Status closeSection(const ExecutionContext& execution)
    {
        const std::uint64_t frames = m_sectionFrames;
        const bool first = m_section == 0u;
        const std::uint64_t movi = first ? m_layout.headerBytes - 4u : m_sectionStart + 20u;
        if (frames != 0u)
        {
            const std::uint64_t indexOffset = m_position;
            std::array<std::byte, avi_detail::kStandardIndexHeaderBytes> indexHeader{};
            avi_detail::encodeStandardIndexHeader(indexHeader, frames, movi);
            if (const Status written = writeTransfers(execution, indexHeader); written.failed())
                return written;
            if (const Status written = writeRecords(
                    execution, frames * avi_detail::kStandardIndexEntryBytes, avi_detail::kStandardIndexEntryBytes,
                    [this](std::span<std::byte> target, std::uint64_t firstFrame) {
                        avi_detail::encodeStandardIndexEntries(target, m_layout, firstFrame);
                    });
                written.failed())
                return written;
            avi_detail::encodeMasterIndexEntry(
                headerBytes(), m_layout, m_section, indexOffset,
                static_cast<std::uint32_t>(avi_detail::kStandardIndexHeaderBytes +
                                           avi_detail::kStandardIndexEntryBytes * frames),
                static_cast<std::uint32_t>(frames));
            ++m_sectionsInUse;
        }
        if (first)
        {
            std::array<std::byte, avi_detail::kIdx1HeaderBytes> idx1Header{};
            avi_detail::encodeIdx1Header(idx1Header, frames);
            if (const Status written = writeTransfers(execution, idx1Header); written.failed())
                return written;
            if (const Status written =
                    writeRecords(execution, frames * avi_detail::kIdx1EntryBytes, avi_detail::kIdx1EntryBytes,
                                 [this](std::span<std::byte> target, std::uint64_t firstFrame) {
                                     avi_detail::encodeIdx1Entries(target, m_layout, firstFrame);
                                 });
                written.failed())
                return written;
            m_firstSectionFrames = frames;
            m_firstSectionBytes = m_position;
        }
        else
        {
            std::array<std::byte, avi_detail::kExtensionSectionHeaderBytes> opening{};
            avi_detail::encodeExtensionSectionHeader(opening, m_layout, frames);
            if (const int error = m_file.writeAt(opening, m_sectionStart); error != 0)
                return avi_detail::ioFailure(error, "an AVI section header could not be written", m_pendingPath);
        }
        execution.reportTrace(TraceEventKind::Counter, "avi", "riff-sections", 1.0, "sections");
        return Status::success();
    }

    /// Starts the next section after the one just closed.
    Status openSection(const ExecutionContext& execution)
    {
        ++m_section;
        m_sectionStart = m_position;
        m_sectionFrames = 0u;
        std::array<std::byte, avi_detail::kExtensionSectionHeaderBytes> opening{};
        avi_detail::encodeExtensionSectionHeader(opening, m_layout, 0u);
        return writeTransfers(execution, opening);
    }

    /// Stores one row: the frame row the stored position shows, as grey or as blue, green and red, with its
    /// padding zeroed.
    void fillRow(std::byte* target, const RawFrame& frame, const ImageLayout& layout, std::uint32_t stored) const
    {
        const std::uint32_t y = m_layout.rowsArriveBottomUp ? stored : m_layout.height - 1u - stored;
        const std::size_t width = m_layout.width;
        const std::size_t samples = width * m_layout.bytesPerPixel;
        if (m_layout.bytesPerPixel == 1u)
        {
            std::memcpy(target, frame.row(0u, y)->data(), width);
        }
        else if (layout.planeCount() == 1u)
        {
            const std::byte* source = frame.row(0u, y)->data();
            for (std::size_t x = 0u; x < width; ++x)
            {
                target[3u * x] = source[3u * x + 2u];
                target[3u * x + 1u] = source[3u * x + 1u];
                target[3u * x + 2u] = source[3u * x];
            }
        }
        else
        {
            const std::byte* red = frame.row(0u, y)->data();
            const std::byte* green = frame.row(1u, y)->data();
            const std::byte* blue = frame.row(2u, y)->data();
            for (std::size_t x = 0u; x < width; ++x)
            {
                target[3u * x] = blue[x];
                target[3u * x + 1u] = green[x];
                target[3u * x + 2u] = red[x];
            }
        }
        if (m_layout.storedRowBytes > samples)
            std::memset(target + samples, 0, m_layout.storedRowBytes - samples);
    }

    Status writeFrame(const ExecutionContext& execution, const RawFrame& frame, const ImageLayout& layout)
    {
        std::array<std::byte, avi_detail::kChunkHeaderBytes> chunkHeader{};
        avi_detail::encodeChunkHeader(chunkHeader, m_layout);
        // Grey rows that arrive bottom-up, need no padding and are stored without row padding are already
        // the container's bytes.
        const bool direct = m_layout.bytesPerPixel == 1u && m_layout.rowsArriveBottomUp &&
                            m_layout.storedRowBytes == m_layout.width && layout.rowStrideBytes() == m_layout.width;
        if (direct)
        {
            if (const Status written = writeTransfers(execution, chunkHeader); written.failed())
                return written;
            return writeTransfers(execution, frame.bytes().first(static_cast<std::size_t>(m_layout.frameBytes)));
        }
        const std::size_t rowBytes = m_layout.storedRowBytes;
        const auto capacity = static_cast<std::size_t>(m_scratch.size());
        std::memcpy(m_scratch.bytes(), chunkHeader.data(), chunkHeader.size());
        std::size_t filled = chunkHeader.size();
        for (std::uint32_t stored = 0u; stored < m_layout.height; ++stored)
        {
            if (filled + rowBytes > capacity)
            {
                if (const Status written = writeTransfers(execution, {m_scratch.bytes(), filled}); written.failed())
                    return written;
                filled = 0u;
            }
            fillRow(m_scratch.bytes() + filled, frame, layout, stored);
            filled += rowBytes;
        }
        return writeTransfers(execution, {m_scratch.bytes(), filled});
    }

    SequenceFile m_file;
    SequenceOutputClaim m_claim;
    std::string m_pendingPath;
    Layout m_layout;
    AccountedBlock m_header;
    AccountedBlock m_scratch;
    std::uint64_t m_position;
    std::uint64_t m_frames = 0u;
    std::uint32_t m_section = 0u;
    std::uint64_t m_sectionStart = 0u;
    std::uint64_t m_sectionFrames = 0u;
    std::uint64_t m_firstSectionFrames = 0u;
    std::uint64_t m_firstSectionBytes = 0u;
    std::uint32_t m_sectionsInUse = 0u;
    bool m_finished = false;
};
} // namespace

Result<FrameSink> createAviSink(const EngineContext& engine, const ExecutionContext& execution, SequenceOutput& output,
                                const SequenceDescriptor& descriptor, const AviSinkRequest& request)
{
    return sequence_detail::guarded([&]() -> Result<FrameSink> {
        if (!engine.valid())
            return Status::failure(ErrorCode::NotInitialized);
        if (const Status checked = sequence_detail::checkContext(engine.id(), execution); checked.failed())
            return checked;
        if (!output.valid())
            return Status::failure(ErrorCode::InvalidState,
                                   {.message = "an AVI sink writes the pending payload of an uncommitted output",
                                    .operation = "avi",
                                    .domain = ErrorDomain::Io,
                                    .context = {StatusContextField::ofText(ContextKey::Parameter, "output")}});
        if (output.engineContextId() != engine.id())
            return Status::failure(ErrorCode::IncompatibleArguments,
                                   {.message = "the output was reserved for another engine context",
                                    .operation = "avi",
                                    .domain = ErrorDomain::Io,
                                    .context = {StatusContextField::ofText(ContextKey::Parameter, "output")}});
        auto layout = avi_detail::planLayout(descriptor, request);
        if (!layout.hasValue())
            return layout.status();
        // The sequence handle checks these again when it adopts the backend; checking them first keeps every
        // refusal ahead of the first byte written.
        if (const Status checked = sequence_detail::checkDescription(engine, descriptor); checked.failed())
            return checked;
        const std::uint64_t scratchBytes =
            (std::max)({(std::min)(avi_detail::kTransferBytes, avi_detail::chunkBytes(*layout)),
                        std::uint64_t{layout->storedRowBytes} + avi_detail::kChunkHeaderBytes, kMinimumScratchBytes});
        auto header = AccountedBlock::allocate(engine, layout->headerBytes, 1u);
        if (!header.hasValue())
            return header.status();
        auto scratch = AccountedBlock::allocate(engine, scratchBytes, 1u);
        if (!scratch.hasValue())
            return scratch.status();
        if (const Status stopped = execution.checkCancellation(); stopped.failed())
            return stopped;
        auto claim = output.claimWriter();
        if (!claim.hasValue())
            return claim.status();
        const std::string pendingPath(claim->pendingPath());
        SequenceFile file;
        const std::filesystem::path native(
            std::u8string_view(reinterpret_cast<const char8_t*>(pendingPath.data()), pendingPath.size()));
        if (const int error = file.openForWriting(native); error != 0)
            return avi_detail::ioFailure(error, "the pending AVI payload could not be opened", pendingPath);
        const std::span<std::byte> headerBytes(header->bytes(), static_cast<std::size_t>(header->size()));
        std::memset(headerBytes.data(), 0, headerBytes.size());
        avi_detail::encodeHeader(headerBytes, *layout, {0u, 0u, layout->headerBytes, 0u});
        if (const int error = file.writeAt(headerBytes, 0u); error != 0)
        {
            (void)file.truncate();
            return avi_detail::ioFailure(error, "the AVI header could not be written", pendingPath);
        }
        auto backend = std::make_unique<AviSinkBackend>(std::move(file), std::move(*claim), *layout, std::move(*header),
                                                        std::move(*scratch));
        return FrameSink::create(engine, descriptor, std::move(backend));
    });
}
} // namespace an
