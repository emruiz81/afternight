// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The scalar oracle of the pointwise family: one affine map applied sample by sample.
///
/// `referencePointwiseAffineKernel` is the scalar oracle of `KernelOperation::PointwiseAffine`. It
/// evaluates one Float64 multiply and one add per selected sample and narrows once, walks one
/// Float32 plane in row-major order on the calling thread, and writes nothing for a sample its
/// validity rule rejects. Fused pointwise programs are a different operation, with their own
/// executor, in `Algorithms/Pointwise/`.

#pragma once

#include <AfterNightEngine/Algorithms/Kernel/ReferenceKernel.h>
#include <AfterNightEngine/Core/Api.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Image/PlaneView.h>

namespace an
{

/// The two coefficients of the reference affine pointwise operation.
struct AffineKernelParameters
{
    /// Multiplier applied first, in Float64.
    double scale = 1.0;

    /// Offset added second, in Float64, before narrowing once to Float32.
    double bias = 0.0;
};

/// Applies `Float32(Float64(input) * scale + bias)` independently to selected samples.
///
/// Exact in-place execution is supported; partial overlap is not. Invalid inputs leave their
/// destination samples untouched. Coefficients must be finite. Cancellation is checked before
/// output and once per row, so a mid-run stop may leave a row-major prefix written.
///
/// @param execution the operation. Must hold a valid engine-context lease.
/// @param source the single-sample input plane.
/// @param destination the equally shaped output plane.
/// @param parameters the finite scale and bias.
/// @param validity the input rule and optional mask, borrowed for the call.
/// @return the counts; `ErrorCode::InvalidArgument` for a non-finite coefficient, checked right
///         after the execution; or any refusal `referenceTransferKernel` documents for the
///         execution, the two planes, the mask, an overlap, or a stop.
[[nodiscard]] AFTERNIGHTENGINE_API Result<KernelRunResult>
referencePointwiseAffineKernel(const ExecutionContext& execution, PlaneView<const float> source,
                               PlaneView<float> destination, const AffineKernelParameters& parameters,
                               const KernelValidityView& validity);

} // namespace an
