// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Where drizzle's samples come from: the reader interface the executor pulls full-width bands
/// through, the decoder buffers it lends, and a zero-copy reader over frames already in memory.
///
/// `DrizzleFrameSource` is the one seam between a decoder and the executor. The executor asks it for
/// one frame's complete rows or for one full-width band, according to the reader's fixed
/// `access()`, and lends it `DrizzleReadBuffers` allocated from the context; the reader returns a
/// `DrizzleFrameData` that either points into those buffers or borrows storage the caller declared
/// resident in the planning request. Reads are sequential, a returned band stays valid only until
/// the next read begins, and an exception a reader throws is translated at the executor's callback
/// boundary. There is no per-sample virtual call: a band is read once and then walked by stride.
///
/// `drizzleSourceRows` cuts a window from a band without copying, and `ResidentDrizzleFrameSource`
/// uses it to serve caller-owned frames.
#pragma once

#include <AfterNightEngine/Algorithms/Drizzle/DrizzleAccumulation.h>

namespace an
{

/// One full-width decoded band. Layouts are owned values; all pixels and bits are borrowed.
/// Keep the bytes immutable until the next source read begins. Auxiliary planes have the same rows.
struct DrizzleFrameData
{
    DrizzleSampleBand pixels; ///< Stored representation and layout, without implicit normalization.
    std::optional<ValidityMaskView<const std::uint8_t>> mask{}; ///< Exact per-plane band mask when requested.
    std::optional<DrizzleSampleBand> pixelWeights{}; ///< Optional finite non-negative dimensionless weights.
    std::optional<DrizzleSampleBand> flatWeights{}; ///< Optional finite positive dimensionless flat weights.
};

/// Decoder destinations owned by the executor. Each nonempty span starts on a 64-byte boundary.
/// Payload spans fit packed planar rows at the model's representation. A source may instead return
/// borrowed resident storage; its residency must be declared in the planning request.
struct DrizzleReadBuffers
{
    std::span<std::byte> pixels{}; ///< Stored sample bytes for the requested band.
    std::span<std::byte> mask{}; ///< Packed per-plane validity bytes, empty unless requested.
    std::span<std::byte> pixelWeights{}; ///< Packed auxiliary sample bytes, empty unless requested.
    std::span<std::byte> flatWeights{}; ///< Packed flat sample bytes, empty unless requested.
    std::span<std::byte> workspace{}; ///< Reader's declared extra workspace; never grows during a read.
};

/// A sequential frame reader; no two reads are in flight and no borrowed band survives the next read.
/// The caller owns the source. Exceptions from read are translated at the executor's callback boundary.
class DrizzleFrameSource
{
public:
    /// Destroys a derived reader through its interface.
    virtual ~DrizzleFrameSource() = default;
    /// The smallest unit this reader can decode, fixed throughout an execution.
    /// @return WholeFrame or Region; unknown values are refused before reading.
    [[nodiscard]] virtual DrizzleSourceAccess access() const noexcept = 0;
    /// Reads exactly one requested full-width band, including any mask and auxiliary planes.
    /// @param execution cancellation and diagnostics; a long decoder must check cancellation itself.
    /// @param frameIndex source position in the ordered sequence.
    /// @param firstRow first global source row; zero for a whole-frame reader.
    /// @param rows requested height; the complete source height for whole-frame access.
    /// @param buffers bounded, disjoint writable storage, reusable after this band's consumption.
    /// @return borrowed band, or a typed failure; malformed descriptions are refused before accumulation.
    [[nodiscard]] virtual Result<DrizzleFrameData> read(const ExecutionContext& execution, std::uint32_t frameIndex,
                                                        std::uint32_t firstRow, std::uint32_t rows,
                                                        const DrizzleReadBuffers& buffers) = 0;
};

/// Cuts a full-width window without copying samples, preserving every row and plane stride.
/// @param frame source band; its bytes must outlive the returned borrow.
/// @param firstRow relative row within frame, not a global source offset.
/// @param rows positive window height.
/// @return the checked window, including its mask and auxiliary planes; `ErrorCode::OutOfRange` for
///         an empty window or one past the band; `ErrorCode::InvalidArgument` for short, misaligned
///         or wrongly sized samples, or a sample or mask window that exceeds its storage;
///         `ErrorCode::IncompatibleArguments` when the mask or an auxiliary plane does not cover the
///         band; or `ErrorCode::LimitExceeded` when the window's extent or strides cannot be
///         represented. No buffer is changed and no storage is allocated.
[[nodiscard]] AFTERNIGHTENGINE_API Result<DrizzleFrameData>
drizzleSourceRows(const DrizzleFrameData& frame, std::uint32_t firstRow, std::uint32_t rows);

/// A zero-copy reader of caller-owned resident records and samples; creation allocates nothing.
/// The records and all borrowed bytes outlive the source. Independent sources may read them concurrently.
class ResidentDrizzleFrameSource final : public DrizzleFrameSource
{
public:
    /// Checks resident frame geometry and creates a borrowed reader.
    /// @param frames ordered complete frames, with matching representation and geometry.
    /// @param access whether to offer complete frames or row windows.
    /// @return the reader; `ErrorCode::EmptySelection` for no frames; `ErrorCode::LimitExceeded` for
    ///         more than `kMaxDrizzleFrames`; `ErrorCode::InvalidArgument` for an unknown access;
    ///         `ErrorCode::IncompatibleArguments` for frames of different extents or
    ///         representations; or any refusal `drizzleSourceRows` reports for a frame's full window.
    [[nodiscard]] static AFTERNIGHTENGINE_API Result<ResidentDrizzleFrameSource>
    create(std::span<const DrizzleFrameData> frames, DrizzleSourceAccess access = DrizzleSourceAccess::WholeFrame);
    /// The source's fixed access choice.
    /// @return the access supplied at construction.
    [[nodiscard]] DrizzleSourceAccess access() const noexcept override
    {
        return m_access;
    }
    /// Borrows requested rows without using decoder buffers.
    /// @param execution cancellation checked before returning the borrow.
    /// @param frameIndex index below the retained record count.
    /// @param firstRow first source row.
    /// @param rows requested row count, complete height under WholeFrame.
    /// @param buffers unused decoder storage.
    /// @return the row window; `ErrorCode::NotInitialized` for an uninitialized execution;
    ///         `ErrorCode::Cancelled` or `ErrorCode::DeadlineExceeded`; `ErrorCode::OutOfRange` for a
    ///         frame index past the records; `ErrorCode::InvalidArgument` for a partial request to a
    ///         whole-frame reader; or any refusal `drizzleSourceRows` reports.
    [[nodiscard]] AFTERNIGHTENGINE_API Result<DrizzleFrameData> read(const ExecutionContext& execution,
                                                                     std::uint32_t frameIndex, std::uint32_t firstRow,
                                                                     std::uint32_t rows,
                                                                     const DrizzleReadBuffers& buffers) override;

private:
    /// Retains only a borrow of checked records.
    /// @param frames immutable records kept alive by the caller.
    /// @param access the validated access choice.
    ResidentDrizzleFrameSource(std::span<const DrizzleFrameData> frames, DrizzleSourceAccess access) noexcept
        : m_frames(frames), m_access(access)
    {
    }
    std::span<const DrizzleFrameData> m_frames; ///< Borrowed complete frames.
    DrizzleSourceAccess m_access; ///< Fixed source contract.
};

} // namespace an
