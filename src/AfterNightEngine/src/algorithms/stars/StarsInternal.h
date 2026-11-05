// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - failure spellings and small helpers shared by the star operations.
//
// Private to the star domain. It exists so that the translation units of this domain report their
// failures the same way - same domain, same context keys, same operation naming - rather than each
// inventing its own, and so that the handful of numeric helpers several of them need is one text
// rather than one per file.

#pragma once

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/Status.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

/// The star domain's private helpers: the failure builders every star operation reports through,
/// the shared numeric checks and median, and the detector's sky measurement.
namespace an::stars_detail
{

/// A failure of the star domain.
///
/// @param operation the entry point, spelled as it is in the source.
/// @param code why.
/// @param message a sentence for a person.
/// @return the status.
[[nodiscard]] inline Status failure(const char* operation, ErrorCode code, const char* message)
{
    return Status::failure(code,
                           StatusDetail{.message = message, .operation = operation, .domain = ErrorDomain::Algorithms});
}

/// A failure of this domain naming the parameter at fault.
///
/// @param operation the entry point, spelled as it is in the source.
/// @param code why.
/// @param message a sentence for a person.
/// @param parameter the parameter's name.
/// @return the status.
[[nodiscard]] inline Status parameterFailure(const char* operation, ErrorCode code, const char* message,
                                             const char* parameter)
{
    return Status::failure(code,
                           StatusDetail{.message = message,
                                        .operation = operation,
                                        .domain = ErrorDomain::Algorithms,
                                        .context = {StatusContextField::ofText(ContextKey::Parameter, parameter)}});
}

/// An `ErrorCode::OutOfRange` failure carrying the value the caller stated and the limit it crossed.
///
/// @param operation the entry point, spelled as it is in the source.
/// @param message a sentence for a person.
/// @param parameter the parameter's name.
/// @param requested what the caller asked for.
/// @param limit what the operation allows.
/// @return the status.
[[nodiscard]] inline Status rangeFailure(const char* operation, const char* message, const char* parameter,
                                         std::uint64_t requested, std::uint64_t limit)
{
    return Status::failure(ErrorCode::OutOfRange,
                           StatusDetail{.message = message,
                                        .operation = operation,
                                        .domain = ErrorDomain::Algorithms,
                                        .context = {StatusContextField::ofText(ContextKey::Parameter, parameter),
                                                    StatusContextField::ofCount(ContextKey::Requested, requested),
                                                    StatusContextField::ofCount(ContextKey::Limit, limit)}});
}

/// Whether a value is finite and strictly above zero.
[[nodiscard]] inline bool isFinitePositive(double value) noexcept
{
    return std::isfinite(value) && value > 0.0;
}

/// The exact median of a run, reordering it.
///
/// The average of the two middle values for an even count, which is the engine's median everywhere.
///
/// @param values the run, which is partially reordered.
/// @param count how many, at least one.
/// @return the median.
[[nodiscard]] inline double medianInPlace(double* values, std::size_t count) noexcept
{
    const std::size_t upper = count / 2u;
    std::nth_element(values, values + upper, values + count);
    const double high = values[upper];
    if (count % 2u == 1u)
    {
        return high;
    }
    std::nth_element(values, values + (upper - 1u), values + upper);
    const double low = values[upper - 1u];
    return low + 0.5 * (high - low);
}

} // namespace an::stars_detail
