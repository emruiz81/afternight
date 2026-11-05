// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - reading, writing, resolving and removing an image list's manifest.
//
// A manifest is read whole into engine-context memory, bounded by `kMaxImageListManifestBytes`, in
// transfers of one mebibyte with a cancellation check before each, and then parsed; a file that
// shrinks while it is read is a truncated container rather than a shorter document. Its attributes are
// interpreted leniently - one that is absent or unreadable is zero or false -
// except that a stated version other than the one this engine reads is refused, and every frame path
// must be a bounded UTF-8 path. A manifest is written through a sequence output, so it appears under its
// name whole or not at all.

#include <AfterNightEngine/IO/ImageListManifest.h>

#include <AfterNightEngine/IO/SequenceError.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include "core/Utf8.h"
#include "io/sequences/AccountedBlock.h"
#include "io/sequences/ImageListFormat.h"
#include "io/sequences/SequenceFile.h"
#include "io/sequences/SequenceInternal.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <filesystem>
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

/// How much of a manifest one read transfers.
constexpr std::uint64_t kReadChunkBytes = 1048576u;

Status checkEngine(const EngineContext& engine, const ExecutionContext& execution)
{
    if (!engine.valid())
        return Status::failure(ErrorCode::NotInitialized);
    return sequence_detail::checkContext(engine.id(), execution);
}

Status pathLimit(const char* message, const char* parameter)
{
    return Status::failure(ErrorCode::LimitExceeded,
                           {.message = message,
                            .operation = "image-list",
                            .domain = ErrorDomain::Io,
                            .context = {StatusContextField::ofText(ContextKey::Parameter, parameter)}});
}

std::uint64_t unsignedOf(const std::optional<std::string>& text) noexcept
{
    return text ? image_list_detail::readUnsigned(*text) : 0u;
}

Result<ImageListManifest> interpret(image_list_detail::ParsedDocument parsed, std::string_view path)
{
    using image_list_detail::DocumentProblem;
    switch (parsed.problem)
    {
    case DocumentProblem::None:
        break;
    case DocumentProblem::NotWellFormed:
        return manifestFailure(ErrorCode::InvalidData, SequenceFailure::MalformedContainer,
                               "the manifest is not a well-formed document of the grammar a manifest is read with",
                               "document", path);
    case DocumentProblem::NoManifest:
        return manifestFailure(ErrorCode::InvalidData, SequenceFailure::MalformedContainer,
                               "the document holds no top-level sequence element", "sequence", path);
    case DocumentProblem::TooManyFrames:
        return manifestFailure(ErrorCode::LimitExceeded, SequenceFailure::UnsupportedEncoding,
                               "the manifest lists more frames than one list holds", "frame", path,
                               {StatusContextField::ofCount(ContextKey::Limit, kMaxImageListManifestFrames)});
    }
    image_list_detail::ManifestElement& element = parsed.manifest;
    if (element.version &&
        image_list_detail::readInteger(*element.version) != static_cast<std::int32_t>(kImageListManifestVersion))
        return manifestFailure(ErrorCode::Unsupported, SequenceFailure::UnsupportedEncoding,
                               "the manifest names a version this engine does not read", "version", path);
    ImageListManifest manifest;
    manifest.timestampSeconds = unsignedOf(element.timestamp);
    manifest.width = unsignedOf(element.width);
    manifest.height = unsignedOf(element.height);
    manifest.channels = unsignedOf(element.channels);
    manifest.mosaiced = element.mosaiced && image_list_detail::readBoolean(*element.mosaiced);
    manifest.framesPerSecond = element.framesPerSecond ? image_list_detail::readReal(*element.framesPerSecond) : 0.0;
    manifest.framePaths.reserve(element.framePaths.size());
    for (std::size_t index = 0u; index < element.framePaths.size(); ++index)
    {
        std::optional<std::string>& framePath = element.framePaths[index];
        if (!framePath || !isBoundaryPath(*framePath))
            return manifestFailure(ErrorCode::InvalidData, SequenceFailure::MalformedContainer,
                                   "a frame's path is absent, empty, not UTF-8, holds a NUL or exceeds the path bound",
                                   "path", path, {StatusContextField::ofCount(ContextKey::Requested, index)});
        manifest.framePaths.push_back(std::move(*framePath));
    }
    return manifest;
}
} // namespace

Result<ImageListManifest> readImageListManifest(const EngineContext& engine, const ExecutionContext& execution,
                                                std::string_view path)
{
    return sequence_detail::guarded([&]() -> Result<ImageListManifest> {
        if (const Status checked = checkEngine(engine, execution); checked.failed())
            return checked;
        if (!isBoundaryPath(path))
            return argumentFailure("a manifest path is nonempty bounded UTF-8 without NUL", "path");
        if (const Status stopped = execution.checkCancellation(); stopped.failed())
            return stopped;
        SequenceFile file;
        if (const int error = file.openForReading(nativePath(path)); error != 0)
            return ioFailure(error, "the manifest could not be opened", path);
        std::uint64_t bytes = 0u;
        if (const int error = file.size(bytes); error != 0)
            return ioFailure(error, "the manifest's size could not be read", path);
        if (bytes > kMaxImageListManifestBytes)
            return manifestFailure(ErrorCode::LimitExceeded, SequenceFailure::UnsupportedEncoding,
                                   "the manifest is larger than this engine reads", "path", path,
                                   {StatusContextField::ofCount(ContextKey::Requested, bytes),
                                    StatusContextField::ofCount(ContextKey::Limit, kMaxImageListManifestBytes)});
        auto block = sequence_detail::AccountedBlock::allocate(engine, bytes, 1u);
        if (!block.hasValue())
            return block.status();
        for (std::uint64_t offset = 0u; offset < bytes;)
        {
            if (const Status stopped = execution.checkCancellation(); stopped.failed())
                return stopped;
            const auto count = static_cast<std::size_t>((std::min)(bytes - offset, kReadChunkBytes));
            std::size_t received = 0u;
            if (const int error = file.readAt(block->bytes() + offset, count, offset, received); error != 0)
                return ioFailure(error, "the manifest could not be read", path);
            if (received == 0u)
                return manifestFailure(ErrorCode::InvalidData, SequenceFailure::TruncatedContainer,
                                       "the manifest ended before the size it had when it was opened", "path", path,
                                       {StatusContextField::ofCount(ContextKey::Requested, bytes),
                                        StatusContextField::ofCount(ContextKey::Available, offset)});
            offset += received;
        }
        (void)file.close();
        if (const Status stopped = execution.checkCancellation(); stopped.failed())
            return stopped;
        const std::string_view text(reinterpret_cast<const char*>(block->bytes()), static_cast<std::size_t>(bytes));
        image_list_detail::ParsedDocument parsed =
            image_list_detail::parseManifestDocument(text, kMaxImageListManifestFrames);
        block->release();
        auto manifest = interpret(std::move(parsed), path);
        if (!manifest.hasValue())
            return manifest.status();
        if (const Status stopped = execution.checkCancellation(); stopped.failed())
            return stopped;
        return manifest;
    });
}

Result<SequenceOutputReport> writeImageListManifest(const EngineContext& engine, const ExecutionContext& execution,
                                                    const SequenceOutputRequest& request,
                                                    const ImageListManifest& manifest)
{
    return sequence_detail::guarded([&]() -> Result<SequenceOutputReport> {
        if (const Status checked = checkEngine(engine, execution); checked.failed())
            return checked;
        if (manifest.framePaths.size() > kMaxImageListManifestFrames)
            return manifestFailure(ErrorCode::LimitExceeded, SequenceFailure::UnsupportedEncoding,
                                   "a manifest lists at most as many frames as one list holds", "framePaths", {},
                                   {StatusContextField::ofCount(ContextKey::Requested, manifest.framePaths.size()),
                                    StatusContextField::ofCount(ContextKey::Limit, kMaxImageListManifestFrames)});
        if (!std::isfinite(manifest.framesPerSecond))
            return argumentFailure("a manifest's frame rate is finite", "framesPerSecond");
        for (std::size_t index = 0u; index < manifest.framePaths.size(); ++index)
            if (!isBoundaryPath(manifest.framePaths[index]))
                return argumentFailure("a frame path is nonempty bounded UTF-8 without NUL", "framePaths",
                                       {StatusContextField::ofCount(ContextKey::Requested, index)});
        auto buffer = sequence_detail::AccountedBlock::allocate(engine, image_list_detail::kManifestBufferBytes, 1u);
        if (!buffer.hasValue())
            return buffer.status();
        auto output = SequenceOutput::begin(engine, execution, request);
        if (!output.hasValue())
            return output.status();
        auto claim = output->claimWriter();
        if (!claim.hasValue())
            return claim.status();
        const std::string pendingPath(claim->pendingPath());
        SequenceFile file;
        if (const int error = file.openForWriting(nativePath(pendingPath)); error != 0)
            return ioFailure(error, "the pending manifest could not be opened", pendingPath);
        image_list_detail::ManifestWriter writer(file, pendingPath, std::move(*buffer));
        const image_list_detail::ManifestFacts facts{.timestampSeconds = manifest.timestampSeconds,
                                                     .width = manifest.width,
                                                     .height = manifest.height,
                                                     .channels = manifest.channels,
                                                     .mosaiced = manifest.mosaiced,
                                                     .framesPerSecond = manifest.framesPerSecond};
        const bool empty = manifest.framePaths.empty();
        if (const Status written = writer.head(execution, facts, empty); written.failed())
            return written;
        for (const std::string& framePath : manifest.framePaths)
            if (const Status written = writer.frame(execution, framePath); written.failed())
                return written;
        if (!empty)
            if (const Status written = writer.tail(execution); written.failed())
                return written;
        if (const Status flushed = writer.flush(execution); flushed.failed())
            return flushed;
        if (const int error = file.close(); error != 0)
            return ioFailure(error, "the pending manifest could not be closed", pendingPath);
        claim->complete();
        claim->release();
        return output->commit(execution, SequenceWriteResult{manifest.framePaths.size(), writer.bytes()});
    });
}

Result<std::string> resolveImageListFramePath(std::string_view manifestPath, std::string_view framePath)
{
    return sequence_detail::guarded([&]() -> Result<std::string> {
        if (!isBoundaryPath(manifestPath))
            return argumentFailure("a manifest path is nonempty bounded UTF-8 without NUL", "manifestPath");
        if (!isBoundaryPath(framePath))
            return argumentFailure("a frame path is nonempty bounded UTF-8 without NUL", "framePath");
        const std::filesystem::path nativeFrame = nativePath(framePath);
        if (nativeFrame.is_absolute())
            return std::string(framePath);
        if (nativeFrame.has_root_name() || nativeFrame.has_root_directory())
            return argumentFailure("a frame path is absolute or relative without a partial root", "framePath");
        std::string resolved(image_list_detail::directoryOf(manifestPath));
        resolved.append(framePath);
        if (resolved.size() > kMaxBoundaryPathBytes)
            return pathLimit("the frame's path joined to the manifest's directory exceeds the path bound", "framePath");
        return resolved;
    });
}

Result<std::string> imageListFramesDirectory(std::string_view manifestPath)
{
    return sequence_detail::guarded([&]() -> Result<std::string> {
        if (!isBoundaryPath(manifestPath) || !image_list_detail::namesManifest(manifestPath))
            return argumentFailure("a manifest path is bounded UTF-8 whose last component is a stem and .anseq",
                                   "manifestPath");
        std::string directory(manifestPath.substr(0u, manifestPath.size() - kImageListManifestExtension.size()));
        directory.append("-frames");
        if (directory.size() > kMaxBoundaryPathBytes)
            return pathLimit("the frames directory's path exceeds the path bound", "manifestPath");
        return directory;
    });
}

Status removeImageListFromDisk(std::string_view manifestPath)
{
    return sequence_detail::guarded([&]() -> Status {
        auto directory = imageListFramesDirectory(manifestPath);
        if (!directory.hasValue())
            return directory.status();
        std::error_code error;
        const std::filesystem::path frames = nativePath(*directory);
        const std::filesystem::file_status state = std::filesystem::symlink_status(frames, error);
        if (std::filesystem::is_directory(state))
        {
            (void)std::filesystem::remove_all(frames, error);
            if (error)
                return ioFailure(error.value(), "the frames directory could not be removed", *directory);
        }
        else if (std::filesystem::is_symlink(state))
        {
            (void)std::filesystem::remove(frames, error);
            if (error)
                return ioFailure(error.value(), "the frames-directory link could not be removed", *directory);
        }
        else if (error && state.type() != std::filesystem::file_type::not_found)
            return ioFailure(error.value(), "the frames directory could not be inspected", *directory);
        error.clear();
        (void)std::filesystem::remove(nativePath(manifestPath), error);
        if (error)
            return ioFailure(error.value(), "the manifest could not be removed", manifestPath);
        return Status::success();
    });
}

} // namespace an
