// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// A frame source that reads nothing and returns deterministic blank frames.
///
/// A dummy source stands in for a real sequence wherever a test, a benchmark or a pipeline needs frames
/// of an exact description without a file: any representation a `RawFrameDescriptor` can state, the
/// ones that are not resident sample types included, in any layout the engine context's limits allow.
/// It involves no file, random generator, cache, global state, conversion or external library.

#pragma once

#include <AfterNightEngine/IO/FrameSource.h>

namespace an
{
/// The sample values a dummy frame holds; row and plane padding is always zero.
enum class DummyFillMode : std::uint8_t
{
    Zero, ///< Every sample byte is zero, whatever the representation and meaning.
    /// A signed integer sample is its representation's minimum, full-scale black in that convention;
    /// every other sample is zero.
    FullScaleBlack
};

/// What a dummy source produces; nothing is allocated until a frame is read.
struct DummySourceRequest
{
    RawFrameDescriptor descriptor; ///< Each frame's exact meaning, representation and byte order.
    ImageLayout layout; ///< Each frame's exact geometry and padding, matching the descriptor.
    std::uint64_t frameCount = 0u; ///< The number of frames, zero included; nothing is allocated or multiplied by it.
    DummyFillMode fillMode = DummyFillMode::Zero; ///< The sample values.
};

/// Opens a source of independent deterministic frames that can seek to any index.
///
/// Each read allocates one zero-filled frame from the engine context, so a frame's samples are owned,
/// writable and shared with no other read or seek, and a frame outlives the source's close. For
/// `FullScaleBlack` over a signed integer representation, the most significant byte of every sample is
/// then set to 0x80 on the operation's scheduler, with a cancellation checkpoint every 4,096 samples.
/// The allocation itself cannot be interrupted: the source's own read checks cancellation only once it
/// returns, and the only earlier check is the one `FrameSource::read` makes before calling the source.
/// Frames carry empty metadata and no timestamps, and the nominal frame rate is one frame per second.
///
/// @param engine the engine context that owns the session and every frame.
/// @param execution an execution of that engine context, borrowed for the call.
/// @param request the description and count; copied.
/// @return the open source; `ErrorCode::NotInitialized` for an engine context or execution that is not
///         valid; `ErrorCode::IncompatibleArguments` for an execution of another engine context, or with
///         `SequenceFailure::DescriptorMismatch` for a layout whose extent or element size differs from
///         the descriptor's; `ErrorCode::InvalidArgument` for a fill mode outside the enumeration;
///         `ErrorCode::LimitExceeded` with `SequenceFailure::DescriptorMismatch` for a layout beyond the
///         engine context's limits; the execution's cancellation; or `ErrorCode::AllocationFailed` when
///         the session cannot be allocated.
[[nodiscard]] AFTERNIGHTENGINE_API Result<FrameSource>
createDummySource(const EngineContext& engine, const ExecutionContext& execution, const DummySourceRequest& request);
} // namespace an
