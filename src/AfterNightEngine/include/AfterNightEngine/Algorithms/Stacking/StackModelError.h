// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Why a stack request does not describe a stack, as a closed vocabulary.
///
/// `StackModelError` names every rule `StackModel::check` applies, in the order it applies them;
/// `stackModelErrorCode` maps each reason to the coarser `ErrorCode` a caller branches on, and
/// `stackModelErrorName` spells it for a person. The header includes nothing of the model, so a
/// user interface or a log can name a refusal without the rest of the stacking surface.
#pragma once

#include <AfterNightEngine/Core/Api.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <cstdint>

namespace an
{

/// Why a request does not describe a stack this engine can run.
///
/// A closed vocabulary in the shape `Image/DescriptorError.h` uses: `StackModel::check` returns
/// one of these, `makeStackModel` carries it in a typed failure, and `stackModelErrorCode` says
/// which error code a caller branches on. The enumerators are in the order the check applies
/// them, which is a contract: a request with two problems reports the same one on every build.
enum class StackModelError : std::uint8_t
{
    /// The request describes a stack.
    None,

    /// The combination is not one this build defines.
    UnknownCombination,

    /// The rejection is not one this build defines.
    UnknownRejection,

    /// The weighting source is not one this build defines.
    UnknownWeighting,

    /// The star-count scaling is not one this build defines.
    UnknownStarCountScaling,

    /// The normalization scope is not one this build defines.
    UnknownNormalizationScope,

    /// The global normalization method is present and not one this build defines.
    UnknownNormalizationMethod,

    /// The scale estimator is not one this build defines.
    UnknownScaleEstimator,

    /// The rejection-map form is not one this build defines.
    UnknownRejectionMapForm,

    /// The output representation is not one of the eight this engine stores.
    UnknownOutputSampleType,

    /// The frame count is zero.
    NoFrames,

    /// The frame count exceeds `kMaxStackFrames`.
    TooManyFrames,

    /// The normalization reference is not an index below the frame count.
    ReferenceFrameOutOfRange,

    /// A rejection was stated for a combination whose row does not admit one.
    RejectionNotAdmitted,

    /// A rejection was stated over fewer than `kMinimumRejectionFrames` frames.
    RejectionNeedsMoreFrames,

    /// A sigma threshold read by the selected family is not positive and finite.
    SigmaThresholdNotPositive,

    /// A median-relative fraction read by the selected family is not positive and finite.
    MedianFractionNotPositive,

    /// The generalized extreme Studentized deviate test's outlier fraction is not strictly between
    /// zero and one.
    GesdtOutlierFractionOutOfRange,

    /// The generalized extreme Studentized deviate test's significance is not strictly between
    /// zero and one.
    GesdtSignificanceOutOfRange,

    /// A weighting source other than `StackWeighting::Uniform` was stated for a combination whose
    /// row does not admit weights.
    WeightingNotAdmitted,

    /// The floor of a floored source is not in the closed unit interval.
    MinimumWeightOutOfRange,

    /// The exponential star-count scaling's steepness is not positive and finite.
    ExponentialAlphaNotPositive,

    /// A photometric penalty is not finite and non-negative.
    PhotometricPenaltyOutOfRange,

    /// The photometric raw-weight floor is not in the closed unit interval.
    PhotometricMinimumWeightOutOfRange,

    /// Per-channel width weighting was stated for a source other than `StackWeighting::Fwhm`.
    PerChannelFwhmWithoutFwhmWeighting,

    /// A local weighting was enabled for a combination whose row does not admit weights.
    LocalWeightingNotAdmitted,

    /// The local weight window is narrower than `kMinimumLocalWeightWindow`.
    LocalWeightWindowTooSmall,

    /// The local weight window has an even side and therefore no centre pixel.
    LocalWeightWindowNotOdd,

    /// The local weight grid is finer than `kMinimumLocalWeightGridSpacing`.
    LocalWeightGridSpacingTooSmall,

    /// The local weight exponent is not positive and finite.
    LocalWeightSnrExponentNotPositive,

    /// The scope applies a global method and none was named.
    GlobalNormalizationMethodMissing,

    /// A global method was named under a scope that applies none.
    GlobalNormalizationMethodUnused,

    /// The combination needs `NumericDomain::NormalizedLinear` input and the description states
    /// another domain.
    CombinationNeedsNormalizedLinearDomain,

    /// The output representation is an integer one.
    OutputSampleTypeNotFloating,

    /// Rejection maps were requested and no rejection was stated.
    ///
    /// Deliberately the last enumerator; `kStackModelErrorCount` is derived from it.
    RejectionMapsWithoutRejection
};

/// How many reasons `StackModelError` holds, counting `None`.
///
/// Derived from the last enumerator rather than written out, so it cannot drift from the
/// enumeration while `RejectionMapsWithoutRejection` stays last.
inline constexpr std::size_t kStackModelErrorCount =
    static_cast<std::size_t>(StackModelError::RejectionMapsWithoutRejection) + 1u;

/// Whether a value is one of the reasons this build knows.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param error the value to test.
/// @return true when the value is one of the enumerators.
[[nodiscard]] constexpr bool isStackModelError(StackModelError error) noexcept
{
    return static_cast<std::uint8_t>(error) <=
           static_cast<std::uint8_t>(StackModelError::RejectionMapsWithoutRejection);
}

/// The error code a refusal is reported under.
///
/// Coarser than the reason on purpose - a caller branches on the code and a person reads the
/// reason's name - and fixed here so that `makeStackModel` and a caller that only ran `check`
/// agree about it.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param error the reason to classify.
/// @return `ErrorCode::Ok` for `None`; `ErrorCode::InvalidArgument` for a request member outside
///         its own enumeration - the `Unknown` reasons - or an integer output;
///         `ErrorCode::EmptySelection` for no frames;
///         `ErrorCode::LimitExceeded` for too many; `ErrorCode::OutOfRange` for an index or a
///         parameter outside its range; `ErrorCode::IncompatibleArguments` for two stated things
///         that cannot be used together; and `ErrorCode::Internal` for a value outside this
///         enumeration.
[[nodiscard]] constexpr ErrorCode stackModelErrorCode(StackModelError error) noexcept
{
    switch (error)
    {
    case StackModelError::None:
        return ErrorCode::Ok;
    case StackModelError::UnknownCombination:
    case StackModelError::UnknownRejection:
    case StackModelError::UnknownWeighting:
    case StackModelError::UnknownStarCountScaling:
    case StackModelError::UnknownNormalizationScope:
    case StackModelError::UnknownNormalizationMethod:
    case StackModelError::UnknownScaleEstimator:
    case StackModelError::UnknownRejectionMapForm:
    case StackModelError::UnknownOutputSampleType:
    case StackModelError::OutputSampleTypeNotFloating:
        return ErrorCode::InvalidArgument;
    case StackModelError::NoFrames:
        return ErrorCode::EmptySelection;
    case StackModelError::TooManyFrames:
        return ErrorCode::LimitExceeded;
    case StackModelError::ReferenceFrameOutOfRange:
    case StackModelError::SigmaThresholdNotPositive:
    case StackModelError::MedianFractionNotPositive:
    case StackModelError::GesdtOutlierFractionOutOfRange:
    case StackModelError::GesdtSignificanceOutOfRange:
    case StackModelError::MinimumWeightOutOfRange:
    case StackModelError::ExponentialAlphaNotPositive:
    case StackModelError::PhotometricPenaltyOutOfRange:
    case StackModelError::PhotometricMinimumWeightOutOfRange:
    case StackModelError::LocalWeightWindowTooSmall:
    case StackModelError::LocalWeightWindowNotOdd:
    case StackModelError::LocalWeightGridSpacingTooSmall:
    case StackModelError::LocalWeightSnrExponentNotPositive:
        return ErrorCode::OutOfRange;
    case StackModelError::RejectionNotAdmitted:
    case StackModelError::RejectionNeedsMoreFrames:
    case StackModelError::WeightingNotAdmitted:
    case StackModelError::PerChannelFwhmWithoutFwhmWeighting:
    case StackModelError::LocalWeightingNotAdmitted:
    case StackModelError::GlobalNormalizationMethodMissing:
    case StackModelError::GlobalNormalizationMethodUnused:
    case StackModelError::CombinationNeedsNormalizedLinearDomain:
    case StackModelError::RejectionMapsWithoutRejection:
        return ErrorCode::IncompatibleArguments;
    }
    return ErrorCode::Internal;
}

/// The reason's own name, for a log line, a diagnostic field or a test failure message.
///
/// Thread-safe and allocation-free.
///
/// @param error the reason to name. A value outside the enumeration is named
///        `"UnknownStackModelError"`.
/// @return a null-terminated string literal compiled into this library, which outlives every caller
///         and is never freed.
[[nodiscard]] AFTERNIGHTENGINE_API const char* stackModelErrorName(StackModelError error) noexcept;

} // namespace an
