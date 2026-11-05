// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the Student t quantile behind the generalized ESD rejection test, from GSL.
//
// The generalized extreme Studentized deviate (GESD) test (Rosner, 1983) compares each candidate
// outlier against a critical value built from a Student t quantile. The stacking rejection
// estimators need one such value per possible sample count and nothing else from the library, so
// this private header is the whole of what they see: a number or a typed failure, and no GSL type.
// The library's own header is included only by the implementation, in the one directory the
// include rules allow it in.

#pragma once

#include <AfterNightEngine/Core/Result.h>

#include <cstdint>

namespace an
{
/// The generalized ESD critical value for a column of this many samples.
///
/// Rosner's lambda for the largest remaining deviate: with `n` samples and significance `alpha`,
/// the probability is `p = 1 - alpha / (2 n)`, `t` is the Student t quantile at `p` with `n - 2`
/// degrees of freedom, and the value is `(n - 1) t / sqrt(n (n - 2 + t * t))`. The significance is
/// rounded to `float` before the probability is formed in `double`, as the adopted deviate test
/// rounds it.
///
/// Reentrant. The inputs are checked before GSL sees them, and the library's process-wide error
/// handler is left as it is.
///
/// @param samples how many samples remain in the column; at least three.
/// @param significance the test's significance, strictly between zero and one once rounded to
///        `float`.
/// @return the critical value, narrowed to `float`; a bare `ErrorCode::InvalidArgument` for fewer
///         than three samples or a significance outside that interval; or a bare
///         `ErrorCode::InvalidData` when the value is not finite.
[[nodiscard]] Result<float> studentizedDeviateCritical(std::uint32_t samples, double significance);
} // namespace an
