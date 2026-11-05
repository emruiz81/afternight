// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The sequence containers the product reads and writes, over the engine's frame sources and sinks: which containers it
/// reads and writes, opening a sequence file as a source of its frames, reading one frame into an image, and writing
/// frames into a container that is published whole or not at all.
///
/// A sequence is not one object that reads, writes, caches and demosaics its frames behind a shared handle, guesses its
/// container from the file's extension inside every reader, writes a half-finished container under its final name, and
/// removes a failed output by asking each format to delete itself. The containers are the engine's: a SER, image-list
/// or video source reads frames that state what they are, and a SER, image-list, AVI or video sink writes into a
/// private output that `an::SequenceOutput` publishes under its name only once the last frame is in. Everything here
/// runs under an execution the caller already holds, so a worker that is admitted once reads, converts and writes every
/// frame without being admitted again.
///
/// **A frame is converted for a container, never by one.** The engine's sinks store what they are handed: SER eight or
/// sixteen unsigned bits of mono, a two-by-two mosaic or RGB; AVI and the video containers eight unsigned bits of mono
/// or RGB, the video containers at an even width and height; an image list each frame as a FITS file of its own, under
/// an intent that converts nothing. `SequenceWriter` converts a frame to the nearest of those at corresponding full
/// scales - an image list's frames to the first frame's representation - crops a video frame's odd last column or row,
/// and stores a mosaic's samples as grey in a container that cannot state its filter array.
///
/// **A frame's facts travel with it.** A SER the writer makes keeps each frame's exposure start in its trailer, and an
/// image list's frames keep their header cards - the exposure, the filter, the date and the world coordinates. A SER's
/// header has no field for a frame rate, so the rate a SER is read at is the one its trailer's timestamps give.
#pragma once

#include "AfterNightControlsExport.h"

#include <AfterNightConsumerRuntime/ImageAsset.h>

#include <AfterNightEngine/Algorithms/Demosaic/Demosaic.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/IO/FfmpegSink.h>
#include <AfterNightEngine/IO/FrameSource.h>
#include <AfterNightEngine/IO/SequenceDescriptor.h>
#include <AfterNightEngine/Image/AcquisitionMetadata.h>

#include <QString>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace an
{
class EngineContext;
class ExecutionContext;
} // namespace an

namespace an::ui
{

/// The demosaic method the product's one demosaic setting names now. GUI thread: it reads the application's settings,
/// and reads RCD - the setting's default - when there are none.
///
/// @return the method; a name the setting does not hold reads as RCD.
[[nodiscard]] AFTERNIGHTCONTROLS_API an::DemosaicMethod demosaicMethodFromSettings();

/// Whether the product reads sequences of a container: SER, image lists and the video containers, but not a FITS frame
/// container or generated frames, which no source of the engine reads from a file.
///
/// @param format the container.
/// @return true when `openSequenceSource` opens it.
[[nodiscard]] AFTERNIGHTCONTROLS_API bool isReadableSequenceContainer(an::SequenceFormat format) noexcept;

/// Whether the product writes sequences of a container: SER, image lists, AVI, MP4 and the Matroska and WebM video
/// containers; a FITS frame container and generated frames are not.
///
/// @param format the container.
/// @return true when a `SequenceWriter` writes it.
[[nodiscard]] AFTERNIGHTCONTROLS_API bool isWritableSequenceContainer(an::SequenceFormat format) noexcept;

/// A sequence file opened as the engine's source of its frames.
struct OpenedSequence
{
    /// The source, at its first frame. It holds a lease on the engine context until it is closed or destroyed, so the
    /// session's context is not replaced while it is kept.
    an::FrameSource source;

    /// What the sequence is: the source's own description, with the frame rate an image list's manifest states, or for
    /// a SER whose source states none the rate its trailer's timestamps give.
    an::SequenceDescriptor descriptor{};

    /// When the sequence was recorded or made, as its file states it: a SER header's UTC start time, or its first
    /// frame's timestamp when the header states none; an image list's creation time. Absent when the file states none.
    std::optional<an::UtcInstant> created{};

    /// For an image list, every file its manifest names, resolved against the manifest, in its order; empty for
    /// another container.
    std::vector<std::string> framePaths{};
};

/// Opens a sequence file as a source of its frames, in the container the caller names.
///
/// SER is read under the convention the files in circulation were written with, counting the complete frames of a
/// recording that stopped before its count was written, and a cyan, yellow and magenta mosaic as grey samples. Its
/// header has no field for a frame rate, so where the source states none the sequence states the rate its trailer's
/// timestamps give - one frame per mean interval between the earliest and the latest instant the trailer names, the
/// interval in whole ticks of 100 nanoseconds; a trailer that names fewer than two distinct instants gives none. An
/// image list is read from its manifest, each frame decoded when it is read. A video is read in presentation order as
/// eight-bit RGB, a stream that states no colour matrix or range converted as BT.601 at limited range, and a palette a
/// recording never sent as grey.
///
/// @param engine the engine context the execution runs in, which owns the source and its frames.
/// @param execution the caller's admitted execution, borrowed for the call.
/// @param path the file.
/// @param format the container.
/// @return the opened sequence; `ErrorCode::Unsupported` for a container `isReadableSequenceContainer` refuses; or what
///         the engine refused - a file it cannot read among it.
[[nodiscard]] AFTERNIGHTCONTROLS_API an::Result<OpenedSequence>
openSequenceSource(const an::EngineContext& engine, const an::ExecutionContext& execution, const QString& path,
                   an::SequenceFormat format);

/// Reads one frame of an opened source into a planar image: the frame at `index`, seeking first when the source would
/// read another, or the next frame when no index is given.
///
/// @param engine the engine context the source was opened in.
/// @param execution the caller's admitted execution.
/// @param source the source.
/// @param index the zero-based frame, or nothing for the next one.
/// @return the frame's index and image, or nothing past the last frame; `ErrorCode::OutOfRange` for an index past the
///         last frame of a source that states its count; or what the engine refused.
[[nodiscard]] AFTERNIGHTCONTROLS_API an::Result<std::optional<std::pair<std::uint64_t, an::consumer::AnyImage>>>
readSequenceFrame(const an::EngineContext& engine, const an::ExecutionContext& execution, an::FrameSource& source,
                  std::optional<std::uint64_t> index);

/// How a sequence is written.
struct SequenceWriteSettings
{
    /// The container.
    an::SequenceFormat container = an::SequenceFormat::Ser;

    /// Frames per second the container states. AVI and the video containers need one; SER and an image list store it
    /// as information.
    an::SequenceRate frameRate{1u, 1u};

    /// The coding a video container encodes with; a WebM container is always VP9.
    an::FfmpegVideoCodec codec = an::FfmpegVideoCodec::H264;

    /// How many frames will be written, when known: stated in the container's header before the first frame.
    std::optional<std::uint64_t> frameCount{};

    /// When an image list was made, in seconds since the Unix epoch; zero states it unknown.
    std::uint64_t createdSeconds = 0u;
};

/// The frame rate the product's frames-per-second controls state, as an exact fraction: a whole rate as itself, and
/// any other to a thousandth of a frame.
///
/// @param framesPerSecond the rate; positive.
/// @return the fraction; one frame a second for a rate that is not positive and finite.
[[nodiscard]] AFTERNIGHTCONTROLS_API an::SequenceRate sequenceRateOf(double framesPerSecond) noexcept;

/// The frames per second a fraction states.
///
/// @param rate the fraction.
/// @return the rate.
[[nodiscard]] AFTERNIGHTCONTROLS_API double framesPerSecondOf(an::SequenceRate rate) noexcept;

/// The container a path names for writing: its extension, read as the engine reads a sequence's, with `mkv` and `webm`
/// written as the video containers they are.
///
/// @param path the file.
/// @return the container, or nothing for a path no writable container is named by.
[[nodiscard]] AFTERNIGHTCONTROLS_API std::optional<an::SequenceFormat>
writableSequenceContainerFor(const QString& path);

/// Writes frames into a sequence container that is published at its path whole or not at all.
///
/// Nothing is written until the first frame: it decides the frame the container describes. Each frame is converted for
/// the container as the file comment says, appended, and counted; `finish` completes the container and publishes it at
/// its path in one step. A writer destroyed or aborted before `finish` succeeds leaves nothing at its path, and nothing
/// beside it - an image list's frames included.
///
/// Move-only. Every call of one writer must come from one thread at a time, under executions of one engine context.
class SequenceWriter
{
public:
    /// A writer of nothing.
    AFTERNIGHTCONTROLS_API SequenceWriter() noexcept;

    /// Aborts what was not published.
    AFTERNIGHTCONTROLS_API ~SequenceWriter();

    /// Takes over another writer.
    ///
    /// @param other the writer to take over.
    AFTERNIGHTCONTROLS_API SequenceWriter(SequenceWriter&& other) noexcept;

    /// Aborts this writer and takes over another.
    ///
    /// @param other the writer to take over.
    /// @return this writer.
    AFTERNIGHTCONTROLS_API SequenceWriter& operator=(SequenceWriter&& other) noexcept;

    SequenceWriter(const SequenceWriter&) = delete;
    SequenceWriter& operator=(const SequenceWriter&) = delete;

    /// Prepares a writer; touches no file.
    ///
    /// @param path the file the sequence is published at. An image list's path ends in `.anseq`.
    /// @param settings the container and how it is written.
    /// @return the writer; `ErrorCode::Unsupported` for a container `isWritableSequenceContainer` refuses or a video
    ///         path whose extension names no container a sink writes; `ErrorCode::InvalidArgument` for an empty path, a
    ///         frame rate with a zero term or an image list's path that does not end in `.anseq`.
    [[nodiscard]] static AFTERNIGHTCONTROLS_API an::Result<SequenceWriter>
    create(const QString& path, const SequenceWriteSettings& settings);

    /// Appends one frame: converted for the container, and the container begun with the first.
    ///
    /// @param engine the engine context the execution runs in.
    /// @param execution the caller's admitted execution.
    /// @param frame the frame.
    /// @return success, or why the frame was not written - after which only `abort` and destruction remain.
    [[nodiscard]] AFTERNIGHTCONTROLS_API an::Status
    append(const an::EngineContext& engine, const an::ExecutionContext& execution, const an::consumer::AnyImage& frame);

    /// Completes the container and publishes it at its path.
    ///
    /// @param execution the caller's admitted execution.
    /// @return what the published container is: its container, its frame, its count and its rate; or why it was not
    ///         published - `ErrorCode::EmptySelection` when no frame was appended - in which case nothing is left at
    ///         its path.
    [[nodiscard]] AFTERNIGHTCONTROLS_API an::Result<an::SequenceDescriptor>
    finish(const an::ExecutionContext& execution);

    /// Ends the writer without publishing: nothing is left at its path. Idempotent.
    AFTERNIGHTCONTROLS_API void abort() noexcept;

    /// How many frames were appended.
    [[nodiscard]] AFTERNIGHTCONTROLS_API std::uint64_t framesWritten() const noexcept;

    /// Whether `finish` published the container.
    [[nodiscard]] AFTERNIGHTCONTROLS_API bool published() const noexcept;

private:
    struct State;

    explicit SequenceWriter(std::unique_ptr<State> state) noexcept;

    std::unique_ptr<State> m_state;
};

/// Removes a sequence the product wrote from disk: an image list's manifest and the frames directory beside it, or any
/// other container's file. A sequence already gone is removed already; a frame an image list names outside its own
/// frames directory is left where it is.
///
/// @param path the sequence's file.
/// @return success, or why something could not be removed.
AFTERNIGHTCONTROLS_API an::Status removeSequenceFromDisk(const QString& path);

} // namespace an::ui
