// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The scalar oracle of the transfer family: one Float32 plane copied under a validity rule.
///
/// `referenceTransferKernel` is the scalar oracle of `KernelOperation::TransferCopy`. It walks the
/// plane in row-major order on the calling thread and copies each selected sample as it is, and it
/// writes nothing for a sample the validity rule rejects, so a caller's existing destination value
/// survives there. Conversions between representations and layout moves are separate operations,
/// in `Image/ImageConversion.h`.

#pragma once

#include <AfterNightEngine/Algorithms/Kernel/ReferenceKernel.h>
#include <AfterNightEngine/Core/Api.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Image/PlaneView.h>

namespace an
{

/// Copies a Float32 plane through the authoritative transfer order.
///
/// Padded rows and odd tails are supported. Disjoint buffers and exact in-place identity are legal;
/// partial overlap is refused. Invalid inputs leave their destination samples untouched. A stop is
/// checked before any output and once per row, so a stop observed mid-run leaves the rows before
/// it written.
///
/// @param execution the operation. Must hold a valid engine-context lease.
/// @param source the single-sample input plane.
/// @param destination the equally shaped output plane.
/// @param validity the input rule and optional mask, borrowed for the call.
/// @return the counts; `ErrorCode::NotInitialized` for an execution that holds nothing;
///         `ErrorCode::IncompatibleArguments` for a plane with more than one sample per pixel,
///         planes of different extents, or a packed mask that does not cover the source and its
///         selected bit-plane; `ErrorCode::InvalidArgument` for partial overlap, or a packed mask
///         supplied under a rule that reads none or missing under `ValidityRule::ExplicitMask`; or
///         `ErrorCode::Cancelled` or `ErrorCode::DeadlineExceeded` when the execution stops.
[[nodiscard]] AFTERNIGHTENGINE_API Result<KernelRunResult> referenceTransferKernel(const ExecutionContext& execution,
                                                                                   PlaneView<const float> source,
                                                                                   PlaneView<float> destination,
                                                                                   const KernelValidityView& validity);

} // namespace an
