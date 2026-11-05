// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the image-list sink: each frame written as its own file, and the manifest naming them last.
//
// Frames go into the list's frames directory - the manifest's path without `.anseq`, followed by
// `-frames` - named by the request or after the manifest's stem and the frame's index, and each is
// published whole through `writeImage` as it is appended. The manifest is written into the sequence
// output only at `finish`, so a reader never finds a manifest naming a frame that is not there, and it
// states the first frame's extent, channels and mosaic. Room for a frame's record is taken before the
// frame is written, so every published frame is one the sink can find again: an unfinished sink removes
// each of them, and the frames directory when it created it and left it empty.

#include <AfterNightEngine/IO/ImageListManifest.h>

#include <AfterNightEngine/IO/CodecCapabilities.h>
#include <AfterNightEngine/IO/CodecRegistry.h>
#include <AfterNightEngine/IO/SequenceError.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <AfterNightEngine/Runtime/TraceSink.h>

#include "core/Utf8.h"
#include "io/sequences/AccountedBlock.h"
#include "io/sequences/ImageListFormat.h"
#include "io/sequences/SequenceFile.h"
#include "io/sequences/SequenceInternal.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>
#include <system_error>
#include <utility>

namespace an
{
namespace
{
using image_list_detail::argumentFailure;
using image_list_detail::ioFailure;
using image_list_detail::manifestFailure;
using image_list_detail::nativePath;
using sequence_detail::AccountedBlock;

/// Texts kept in the engine context's memory: every text's bytes in one block and each text's end in another,
/// both grown by doubling, so an append that fits allocates nothing.
class TextList
{
public:
    /// Makes room for `count` texts of `bytes` bytes in all, keeping what the list holds.
    Status reserve(const EngineContext& engine, std::uint64_t count, std::uint64_t bytes)
    {
        if (count > m_endCapacity)
        {
            const std::uint64_t capacity = (std::max)({count, m_endCapacity * 2u, std::uint64_t{64u}});
            auto block = AccountedBlock::allocate(engine, capacity * sizeof(std::uint64_t), alignof(std::uint64_t));
            if (!block.hasValue())
                return block.status();
            std::uninitialized_value_construct_n(block->values(), static_cast<std::size_t>(capacity));
            if (m_count != 0u)
                std::memcpy(block->values(), m_ends.values(),
                            static_cast<std::size_t>(m_count * sizeof(std::uint64_t)));
            m_ends = std::move(*block);
            m_endCapacity = capacity;
        }
        if (bytes > m_byteCapacity)
        {
            const std::uint64_t capacity = (std::max)({bytes, m_byteCapacity * 2u, std::uint64_t{4096u}});
            auto block = AccountedBlock::allocate(engine, capacity, 1u);
            if (!block.hasValue())
                return block.status();
            if (m_used != 0u)
                std::memcpy(block->bytes(), m_bytes.bytes(), static_cast<std::size_t>(m_used));
            m_bytes = std::move(*block);
            m_byteCapacity = capacity;
        }
        return Status::success();
    }

    /// Appends a text the list has room for.
    void push(std::string_view text) noexcept
    {
        if (!text.empty())
            std::memcpy(m_bytes.bytes() + m_used, text.data(), text.size());
        m_used += text.size();
        m_ends.values()[m_count++] = m_used;
    }

    [[nodiscard]] std::uint64_t count() const noexcept
    {
        return m_count;
    }

    [[nodiscard]] std::uint64_t bytes() const noexcept
    {
        return m_used;
    }

    [[nodiscard]] std::string_view at(std::uint64_t index) const noexcept
    {
        const std::uint64_t begin = index == 0u ? 0u : m_ends.values()[index - 1u];
        return {reinterpret_cast<const char*>(m_bytes.bytes()) + begin,
                static_cast<std::size_t>(m_ends.values()[index] - begin)};
    }

    void release() noexcept
    {
        m_bytes.release();
        m_ends.release();
        m_count = 0u;
        m_used = 0u;
        m_endCapacity = 0u;
        m_byteCapacity = 0u;
    }

private:
    AccountedBlock m_bytes;
    AccountedBlock m_ends;
    std::uint64_t m_count = 0u;
    std::uint64_t m_used = 0u;
    std::uint64_t m_endCapacity = 0u;
    std::uint64_t m_byteCapacity = 0u;
};

/// Everything a sink decides once, before it writes a byte.
struct SinkPlan
{
    /// The destination's directory with its trailing separator, or empty.
    std::string directory;

    /// The destination's last component without `.anseq`.
    std::string stem;

    /// The destination without `.anseq`, followed by `-frames`.
    std::string framesDirectory;

    /// The frame format's first extension.
    std::string extension;

    CodecFormat format = CodecFormat::Fits; ///< The container every frame is written as.
    std::optional<OutputIntent> intent; ///< The output intent every frame is written under, if any.
    OutputDurability durability = OutputDurability::FlushBeforePublish; ///< How each frame is made durable.
    std::uint64_t timestampSeconds = 0u; ///< The manifest's timestamp, in seconds.
    double framesPerSecond = 0.0; ///< The descriptor's nominal rate, or zero when it states none.
};

class ImageListSinkBackend final : public FrameSinkBackend
{
public:
    ImageListSinkBackend(SequenceFile file, SequenceOutputClaim claim, SinkPlan plan, TextList names,
                         AccountedBlock buffer)
        : m_file(std::move(file)), m_claim(std::move(claim)), m_pendingPath(m_claim.pendingPath()),
          m_plan(std::move(plan)), m_names(std::move(names)), m_buffer(std::move(buffer))
    {
    }

    ~ImageListSinkBackend() override
    {
        (void)abandon();
        m_claim.release();
    }

    ImageListSinkBackend(const ImageListSinkBackend&) = delete;
    ImageListSinkBackend& operator=(const ImageListSinkBackend&) = delete;

    Result<SequenceWriteResult> append(const EngineContext& engine, const ExecutionContext& execution,
                                       const SequenceFrame& frame) override
    {
        const std::uint64_t index = m_frames.count();
        if (index >= kMaxImageListManifestFrames)
            return manifestFailure(ErrorCode::LimitExceeded, SequenceFailure::UnsupportedEncoding,
                                   "a manifest lists at most as many frames as one list holds", "frameCount",
                                   m_pendingPath);
        std::string relative = m_plan.stem;
        relative.append("-frames/");
        if (index < m_names.count())
            relative.append(m_names.at(index));
        else
        {
            relative.append(m_plan.stem);
            relative.push_back('-');
            relative.append(std::to_string(index));
        }
        relative.append(m_plan.extension);
        const std::string destination = m_plan.directory + relative;
        if (destination.size() > kMaxBoundaryPathBytes)
            return manifestFailure(ErrorCode::LimitExceeded, SequenceFailure::UnsupportedEncoding,
                                   "a frame's path would exceed the path bound", "frameNames", {},
                                   {StatusContextField::ofCount(ContextKey::Requested, index)});
        // Room for the record is taken before the frame exists, so a published frame is always one the
        // sink can find again to remove.
        if (const Status reserved = m_frames.reserve(engine, index + 1u, m_frames.bytes() + relative.size());
            reserved.failed())
            return reserved;
        if (const Status ready = ensureDirectory(); ready.failed())
            return ready;
        const OutputRequest request{.destinationPath = destination,
                                    .format = m_plan.format,
                                    .intent = m_plan.intent ? &*m_plan.intent : nullptr,
                                    .durability = m_plan.durability,
                                    .replacePolicy = OutputReplacePolicy::ReplaceExisting};
        auto written = writeImage(engine, execution, request, frame.frame, frame.metadata);
        if (!written.hasValue())
            return written.status();
        m_frames.push(relative);
        if (index == 0u)
        {
            const RawFrameDescriptor described = *frame.frame.descriptor();
            m_facts.width = described.extent().width();
            m_facts.height = described.extent().height();
            m_facts.channels = described.extent().channelCount();
            m_facts.mosaiced = described.cfa().isMosaiced();
        }
        execution.reportTrace(TraceEventKind::Counter, "image-list", "frame-bytes-written",
                              static_cast<double>(written->bytesWritten), "bytes");
        return SequenceWriteResult{m_frames.count(), 0u};
    }

    Result<SequenceWriteResult> finish(const ExecutionContext& execution) override
    {
        if (const Status stopped = execution.checkCancellation(); stopped.failed())
            return stopped;
        image_list_detail::ManifestFacts facts = m_facts;
        facts.timestampSeconds = m_plan.timestampSeconds;
        facts.framesPerSecond = m_plan.framesPerSecond;
        const std::uint64_t frames = m_frames.count();
        image_list_detail::ManifestWriter writer(m_file, m_pendingPath, std::move(m_buffer));
        if (const Status written = writer.head(execution, facts, frames == 0u); written.failed())
            return written;
        for (std::uint64_t index = 0u; index < frames; ++index)
            if (const Status written = writer.frame(execution, m_frames.at(index)); written.failed())
                return written;
        if (frames != 0u)
            if (const Status written = writer.tail(execution); written.failed())
                return written;
        if (const Status flushed = writer.flush(execution); flushed.failed())
            return flushed;
        if (const int error = m_file.close(); error != 0)
            return ioFailure(error, "the manifest could not be closed", m_pendingPath);
        m_finished = true;
        m_frames.release();
        m_names.release();
        // The manifest is complete and closed, so its output may commit it.
        m_claim.complete();
        m_claim.release();
        return SequenceWriteResult{frames, writer.bytes()};
    }

    Status close() override
    {
        const int error = abandon();
        m_claim.release();
        if (error != 0)
            return ioFailure(error, "the unfinished image list could not be removed", m_plan.framesDirectory);
        return Status::success();
    }

private:
    Status ensureDirectory()
    {
        if (m_directoryReady)
            return Status::success();
        const std::filesystem::path directory = nativePath(m_plan.framesDirectory);
        std::error_code error;
        if (std::filesystem::create_directory(directory, error))
            m_directoryCreated = true;
        else if (error)
            return ioFailure(error.value(), "the frames directory could not be created", m_plan.framesDirectory);
        else if (!std::filesystem::is_directory(directory, error))
            return ioFailure(error ? error.value() : static_cast<int>(std::errc::not_a_directory),
                             "the frames directory's name is taken by something that is not a directory",
                             m_plan.framesDirectory);
        m_directoryReady = true;
        return Status::success();
    }

    /// Removes what an unfinished sink published and discards its manifest, reporting only the first platform
    /// code so the destructor can call it without building a failure. Idempotent.
    int abandon() noexcept
    {
        m_buffer.release();
        if (m_finished)
            return 0;
        int first = 0;
        if (m_file.isOpen())
        {
            const int truncated = m_file.truncate();
            const int closed = m_file.close();
            first = truncated != 0 ? truncated : closed;
        }
        try
        {
            for (std::uint64_t index = 0u; index < m_frames.count(); ++index)
            {
                std::error_code error;
                const std::string destination = m_plan.directory + std::string(m_frames.at(index));
                (void)std::filesystem::remove(nativePath(destination), error);
                if (error && first == 0)
                    first = error.value();
            }
            m_frames.release();
            if (m_directoryCreated)
            {
                std::error_code error;
                const std::filesystem::path directory = nativePath(m_plan.framesDirectory);
                // Only a directory this sink created and left empty goes: anything else in it is not the sink's.
                if (std::filesystem::is_empty(directory, error) && !error)
                {
                    (void)std::filesystem::remove(directory, error);
                    if (error && first == 0)
                        first = error.value();
                }
                m_directoryCreated = false;
                m_directoryReady = false;
            }
        }
        catch (...)
        {
            if (first == 0)
                first = static_cast<int>(std::errc::not_enough_memory);
        }
        m_names.release();
        return first;
    }

    SequenceFile m_file;
    SequenceOutputClaim m_claim;
    std::string m_pendingPath;
    SinkPlan m_plan;
    TextList m_names;
    AccountedBlock m_buffer;
    TextList m_frames;
    image_list_detail::ManifestFacts m_facts{};
    bool m_directoryReady = false;
    bool m_directoryCreated = false;
    bool m_finished = false;
};

Status contextFailure(ErrorCode code, const char* message, const char* parameter)
{
    return Status::failure(code, {.message = message,
                                  .operation = "image-list",
                                  .domain = ErrorDomain::Io,
                                  .context = {StatusContextField::ofText(ContextKey::Parameter, parameter)}});
}

Status descriptorFailure(const char* message, const char* parameter)
{
    return manifestFailure(ErrorCode::InvalidArgument, SequenceFailure::InvalidDescriptor, message, parameter, {});
}
} // namespace

Result<FrameSink> createImageListSink(const EngineContext& engine, const ExecutionContext& execution,
                                      SequenceOutput& output, const SequenceDescriptor& descriptor,
                                      const ImageListSinkRequest& request)
{
    return sequence_detail::guarded([&]() -> Result<FrameSink> {
        if (!engine.valid())
            return Status::failure(ErrorCode::NotInitialized);
        if (const Status checked = sequence_detail::checkContext(engine.id(), execution); checked.failed())
            return checked;
        if (!output.valid())
            return contextFailure(ErrorCode::InvalidState,
                                  "an image-list sink writes the pending payload of an uncommitted output", "output");
        if (output.engineContextId() != engine.id())
            return contextFailure(ErrorCode::IncompatibleArguments,
                                  "the output was reserved for another engine context", "output");
        if (!isCodecFormat(request.frameFormat) || codecExtensions(request.frameFormat).empty())
            return argumentFailure("each frame is written as a named container that has an extension", "frameFormat");
        if (!isOutputDurability(request.frameDurability))
            return argumentFailure("the durability is not one this build knows", "frameDurability");
        if (descriptor.format != SequenceFormat::ImageList)
            return descriptorFailure("an image-list sink writes an image list", "format");
        if (descriptor.seekByIndex)
            return descriptorFailure("a sink cannot seek", "seekByIndex");
        if (descriptor.seekByTime)
            return descriptorFailure("a sink cannot seek", "seekByTime");
        if (const Status checked = validateSequenceDescriptor(descriptor); checked.failed())
            return checked;
        if (descriptor.timestampsRequired)
            return manifestFailure(ErrorCode::Unsupported, SequenceFailure::UnsupportedEncoding,
                                   "a manifest stores no timestamps", "timestampsRequired", {});
        if (descriptor.frameCount && *descriptor.frameCount > kMaxImageListManifestFrames)
            return manifestFailure(ErrorCode::LimitExceeded, SequenceFailure::UnsupportedEncoding,
                                   "a manifest lists at most as many frames as one list holds", "frameCount", {});
        const std::string_view destination = output.destinationPath();
        if (!image_list_detail::namesManifest(destination))
            return argumentFailure("an image list's destination is a manifest named with .anseq", "destinationPath");
        if (request.frameNames.size() > kMaxImageListManifestFrames)
            return contextFailure(ErrorCode::LimitExceeded, "more frame names than one list holds", "frameNames");
        std::uint64_t nameBytes = 0u;
        for (std::size_t index = 0u; index < request.frameNames.size(); ++index)
        {
            const std::string_view name = request.frameNames[index];
            if (!isBoundaryPath(name) || name.find_first_of("/\\") != std::string_view::npos || name == "." ||
                name == "..")
                return argumentFailure("a frame name is nonempty bounded UTF-8 without NUL, a separator or a dot "
                                       "directory",
                                       "frameNames", {StatusContextField::ofCount(ContextKey::Requested, index)});
            nameBytes += name.size();
        }
        const CodecRegistry* const registry = engine.codecRegistry();
        const std::optional<CodecCapabilities> capabilities =
            registry != nullptr ? registry->capabilities(request.frameFormat) : std::nullopt;
        if (!capabilities || capabilities->encodeSamples == 0u)
            return contextFailure(ErrorCode::Unsupported, "no codec in the context encodes the frame format",
                                  "frameFormat");
        // The sequence handle checks the description again when it adopts the backend; checking it first keeps
        // every refusal ahead of the claim.
        if (const Status checked = sequence_detail::checkDescription(engine, descriptor); checked.failed())
            return checked;
        TextList names;
        if (const Status reserved = names.reserve(engine, request.frameNames.size(), nameBytes); reserved.failed())
            return reserved;
        for (const std::string& name : request.frameNames)
            names.push(name);
        auto buffer = AccountedBlock::allocate(engine, image_list_detail::kManifestBufferBytes, 1u);
        if (!buffer.hasValue())
            return buffer.status();
        SinkPlan plan;
        plan.directory = std::string(image_list_detail::directoryOf(destination));
        plan.stem = std::string(image_list_detail::stemOf(destination));
        plan.framesDirectory =
            std::string(destination.substr(0u, destination.size() - kImageListManifestExtension.size()));
        plan.framesDirectory.append("-frames");
        plan.extension = std::string(codecExtensions(request.frameFormat).front());
        plan.format = request.frameFormat;
        plan.intent = request.frameIntent;
        plan.durability = request.frameDurability;
        plan.timestampSeconds = request.timestampSeconds;
        if (descriptor.nominalFrameRate)
            plan.framesPerSecond = static_cast<double>(descriptor.nominalFrameRate->numerator) /
                                   static_cast<double>(descriptor.nominalFrameRate->denominator);
        if (const Status stopped = execution.checkCancellation(); stopped.failed())
            return stopped;
        auto claim = output.claimWriter();
        if (!claim.hasValue())
            return claim.status();
        const std::string pendingPath(claim->pendingPath());
        SequenceFile file;
        if (const int error = file.openForWriting(nativePath(pendingPath)); error != 0)
            return ioFailure(error, "the pending manifest could not be opened", pendingPath);
        auto backend = std::make_unique<ImageListSinkBackend>(std::move(file), std::move(*claim), std::move(plan),
                                                              std::move(names), std::move(*buffer));
        return FrameSink::create(engine, descriptor, std::move(backend));
    });
}

} // namespace an
