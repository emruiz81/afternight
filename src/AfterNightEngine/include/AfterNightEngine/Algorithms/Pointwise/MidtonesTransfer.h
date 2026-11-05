// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The midtones transfer function, its parameters, and its pseudo-inverse.
///
/// The one curve in this engine that several unrelated families need and none of them owns: a
/// stretch applies it as one of nine, a histogram transform remaps counts through it, and a
/// dark-structure enhancement blends it through a mask. It lives in a header of its own so that
/// asking for those three numbers does not also acquire the pointwise program, the colour
/// transforms and the arithmetic surface - which is what including the stretch family for them
/// costs, and what a header include budget exists to notice.

#pragma once

#include <AfterNightEngine/Core/Api.h>
#include <AfterNightEngine/Core/Result.h>

namespace an
{

/// Parameters of one midtones transfer function.
struct MidtonesTransferParameters
{
    /// Midtones balance in the closed unit interval; one half is linear between the endpoints.
    double midtones = 0.5;

    /// Input mapped to zero, in the closed unit interval.
    double shadows = 0.0;

    /// Input mapped to one, in the closed unit interval; must not be below `shadows`.
    double highlights = 1.0;

    /// Parameters compare by value.
    friend constexpr bool operator==(const MidtonesTransferParameters& left,
                                     const MidtonesTransferParameters& right) noexcept = default;
};

/// Evaluates a checked midtones transfer function.
///
/// Inputs at or below the shadow point map to zero and inputs at or above the highlight point map
/// to one. Between them the input `v` is rescaled to `x = (v - s) / (h - s)` in `(0, 1)` and mapped to
/// `(m - 1) x / ((2m - 1) x - m)`, where `m` is the midtones balance and `s` and `h` the endpoints.
/// Equal endpoints therefore describe a step, without a division by zero. The fraction is evaluated
/// in double multiplied through by `h - s`, as `(1 - m)(v - s) / (m (h - v) + (1 - m)(v - s))`:
/// both distances are positive, so nothing cancels, and a zero result is positive zero.
///
/// Thread-safe, reentrant and allocation-free apart from a failure it describes.
///
/// @param value finite input value.
/// @param parameters the midtone and endpoints.
/// @return a value in `[0, 1]`; `ErrorCode::InvalidArgument` for a parameter that is not finite or
///         lies outside the unit interval, or a highlight point below the shadow point; or
///         `ErrorCode::InvalidData` for a non-finite input.
[[nodiscard]] AFTERNIGHTENGINE_API Result<double>
evaluateMidtonesTransfer(double value, const MidtonesTransferParameters& parameters);

/// Evaluates the checked pseudo-inverse of a midtones transfer function.
///
/// Solved by 64 bisection steps against the forward function, so it is deterministic and inverts
/// the function `evaluateMidtonesTransfer` computes rather than a second formula that could drift
/// from it. An output at or below zero, or equal endpoints,
/// answers `shadows`; an output at or above one answers `highlights`.
///
/// Thread-safe, reentrant and allocation-free apart from a failure it describes.
///
/// @param value finite output of the forward function; a value outside the unit interval is
///        clamped into it.
/// @param parameters the same parameters used by the forward function.
/// @return the corresponding input between `shadows` and `highlights`; the same failures as
///         `evaluateMidtonesTransfer`.
[[nodiscard]] AFTERNIGHTENGINE_API Result<double>
evaluateInverseMidtonesTransfer(double value, const MidtonesTransferParameters& parameters);

} // namespace an
