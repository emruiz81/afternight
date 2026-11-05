// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the engine's own encoders' way of writing an image whose rows arrive after its container.
//
// Through the public interface an encoder is handed a whole frame. An adapter that implements `ReservingImageCodec`
// writes the container first instead, with room for the samples, and takes the rows into that room as a caller
// computes them, so an image is written once and never held whole. The output transaction's `reserve`, `writeRows`
// and `commit` are the only callers. The extension is private, as the streaming one in `PreparedCodecWrite.h` is: a
// public adapter writes a frame whole.

#pragma once

#include <AfterNightEngine/Image/StorageArrangement.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>

namespace an
{
template <typename T> class Result;
class EngineContext;
class ExecutionContext;
class ImageLayout;
class ImageMetadata;
class RawFrameDescriptor;
class Status;
struct CodecEncodeRequest;
enum class CodecFormat : std::uint8_t;

/// The room a reservation wrote, which takes the image's rows until the payload is finished.
///
/// The transaction checks every band against the image before it is handed over, and hands over each row once.
/// Destroying it releases whatever it holds - the open file among it - and must not throw.
class CodecReservedRows
{
public:
    virtual ~CodecReservedRows() = default;

    /// Writes a band of whole rows where they lie in the payload. Called from several threads at once for bands that
    /// share no row; starts no parallel work of its own.
    /// @param execution the operation the transaction belongs to.
    /// @param firstRow the image row the band begins at, its rows inside the image.
    /// @param layout where the band's samples sit: the image's width, channels, element size and reserved arrangement
    ///        over the band's rows, already checked.
    /// @param samples the band's bytes, at least `layout.totalBytes()` of them.
    /// @return success, or a typed failure after which part of the band may have been written.
    virtual Status writeRows(const ExecutionContext& execution, std::uint32_t firstRow, const ImageLayout& layout,
                             std::span<const std::byte> samples) = 0;

    /// Completes the payload once every row is written - whatever the container states over its samples, such as a
    /// checksum, computed from the rows written - and closes it.
    /// @param execution the operation the transaction belongs to.
    /// @return the complete payload's size in bytes, or a typed failure.
    virtual Result<std::uint64_t> finish(const ExecutionContext& execution) = 0;
};

/// Optional internal encoder extension: a container written with room for its image's samples.
class ReservingImageCodec
{
public:
    virtual ~ReservingImageCodec() = default;

    /// Writes the container into the request's empty pending path with room for the image's samples, refusing an
    /// option it cannot honour before the file is opened.
    /// @param engine the engine context that owns the encoder's workspaces.
    /// @param execution the operation the transaction belongs to.
    /// @param request the pending path and the output intent, borrowed for the call.
    /// @param descriptor what the image is, its byte order the host's.
    /// @param arrangement how its samples are stored, planar or interleaved.
    /// @param metadata the source facts, already reduced for the destination.
    /// @return the room the rows go into, or a typed failure.
    virtual Result<std::unique_ptr<CodecReservedRows>>
    reserve(const EngineContext& engine, const ExecutionContext& execution, const CodecEncodeRequest& request,
            const RawFrameDescriptor& descriptor, StorageArrangement arrangement,
            const ImageMetadata& metadata) const = 0;
};

/// Whether this context's registered adapter for a format implements the reservation.
[[nodiscard]] bool supportsReservedCodecWrite(const EngineContext& engine, CodecFormat format) noexcept;

/// The encode facade's checks, scheduling and exception boundary, for a reservation: the adapter is selected, the
/// image is checked against its capability record and the engine's limits as a frame's description is, and the
/// intent against the descriptor's representation.
[[nodiscard]] Result<std::unique_ptr<CodecReservedRows>>
reservePreparedImage(const EngineContext& engine, const ExecutionContext& execution, const CodecEncodeRequest& request,
                     const RawFrameDescriptor& descriptor, StorageArrangement arrangement,
                     const ImageMetadata& metadata);

/// A reserved image's row write inside the exception boundary, its failure wrapped as the adapter's.
[[nodiscard]] Status writeReservedRows(const ExecutionContext& execution, CodecFormat format, std::string_view path,
                                       CodecReservedRows& rows, std::uint32_t firstRow, const ImageLayout& layout,
                                       std::span<const std::byte> samples);

/// A reserved image's completion inside the execution's arena and the exception boundary.
[[nodiscard]] Result<std::uint64_t> finishReservedRows(const ExecutionContext& execution, CodecFormat format,
                                                       std::string_view path, CodecReservedRows& rows);
} // namespace an
