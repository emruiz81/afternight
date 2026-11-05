// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Which rejection a stack request states, and the parameters of the family that decides.
///
/// `StackRejection` names the families; `StackRejectionRequest` carries the chosen family beside
/// one parameter record per kind of threshold - sigma thresholds, median fractions, the deviate
/// test's fraction and significance - of which the family reads exactly one, as the
/// `stackRejectionUses*` predicates say. Each meaning is its own field with its own range, which
/// `StackModel::check` validates for the selected family alone. The header includes nothing of the
/// model, so these values can be read without the rest of the stacking surface.
#pragma once

#include <AfterNightEngine/Core/Api.h>
#include <cstdint>

namespace an
{

/// The fewest frames a rejection may be asked to judge.
///
/// A dispersion, a fit or a rank over one or two samples cannot separate an outlier from its
/// population, so such a request is refused, and a workflow that asks for rejection on two frames
/// learns that it got none.
inline constexpr std::uint32_t kMinimumRejectionFrames = 3u;

/// The default threshold, in sigma units, on either side of the centre for every sigma family.
inline constexpr double kDefaultSigmaClipThreshold = 2.0;

/// The default fraction of the median a sample may fall below it before the median-relative
/// clipping rejects it.
inline constexpr double kDefaultLowMedianClipFraction = 0.2;

/// The default fraction of the median a sample may rise above it before the median-relative
/// clipping rejects it.
inline constexpr double kDefaultHighMedianClipFraction = 0.1;

/// The default largest fraction of a pixel's samples the generalized extreme Studentized deviate
/// test may reject.
inline constexpr double kDefaultGesdtOutlierFraction = 0.3;

/// The default significance level of the generalized extreme Studentized deviate test.
inline constexpr double kDefaultGesdtSignificance = 0.05;

/// Which samples of one output pixel survive into the combination, and what happens to the rest.
///
/// Every family reads exactly one of the three parameter records in `StackRejectionRequest`;
/// `stackRejectionUsesSigmaThresholds`, `stackRejectionUsesMedianFractions` and
/// `stackRejectionUsesGesdtParameters` say which, and `StackModel::check` validates that record
/// alone.
enum class StackRejection : std::uint8_t
{
    /// Every sample survives.
    None,

    /// Iterated clipping of samples further than the thresholds, in standard deviations, from the
    /// median of the survivors.
    SigmaClipping,

    /// Iterated clipping of samples further than the thresholds from the median, in units of the
    /// unscaled histogram estimate of median absolute deviation used by the adopted estimator.
    MadSigmaClipping,

    /// Sigma clipping whose rejected samples are replaced by the median of the survivors rather
    /// than removed, so every sample still contributes a count.
    MedianSigmaReplacement,

    /// Sigma clipping against a winsorized dispersion. Each pass first refines the standard
    /// deviation by clamping the survivors to 1.5 dispersions about their median and multiplying
    /// the clamped deviation by 1.134, until a refinement moves it by no more than 0.0005 of its
    /// previous value; the request's thresholds apply only to the clipping that follows.
    WinsorizedSigmaClipping,

    /// A line fitted to the sorted samples against their rank; a sample further than the
    /// thresholds, in units of the fit's residual deviation, from that line is rejected.
    LinearFitClipping,

    /// Median-relative clipping. A sample lower than the median by more than the low fraction of
    /// the median, or higher by more than the high fraction of it, is rejected. The name is the
    /// one the family is known by; the rule is not a rank percentile, and the fractions are
    /// fractions of the median rather than of the population.
    PercentileClipping,

    /// The generalized extreme Studentized deviate test: up to a stated fraction of the samples
    /// are taken as candidates, one extreme at a time, and every candidate up to the last whose
    /// statistic exceeds its critical value at the stated significance is removed - including
    /// earlier candidates that did not exceed theirs.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    GeneralizedEsdTest
};

/// Whether a value names a rejection this build defines.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param rejection the value to test.
/// @return true when the value is one of the enumerators.
[[nodiscard]] constexpr bool isStackRejection(StackRejection rejection) noexcept
{
    return static_cast<std::uint8_t>(rejection) <= static_cast<std::uint8_t>(StackRejection::GeneralizedEsdTest);
}

/// Whether a rejection family reads `StackRejectionRequest::sigma`.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param rejection the family to test.
/// @return true for the four sigma families and the linear fit, whose thresholds are deviations.
[[nodiscard]] constexpr bool stackRejectionUsesSigmaThresholds(StackRejection rejection) noexcept
{
    return rejection == StackRejection::SigmaClipping || rejection == StackRejection::MadSigmaClipping ||
           rejection == StackRejection::MedianSigmaReplacement ||
           rejection == StackRejection::WinsorizedSigmaClipping || rejection == StackRejection::LinearFitClipping;
}

/// Whether a rejection family reads `StackRejectionRequest::medianFractions`.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param rejection the family to test.
/// @return true for `PercentileClipping` alone.
[[nodiscard]] constexpr bool stackRejectionUsesMedianFractions(StackRejection rejection) noexcept
{
    return rejection == StackRejection::PercentileClipping;
}

/// Whether a rejection family reads `StackRejectionRequest::gesdt`.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param rejection the family to test.
/// @return true for `GeneralizedEsdTest` alone.
[[nodiscard]] constexpr bool stackRejectionUsesGesdtParameters(StackRejection rejection) noexcept
{
    return rejection == StackRejection::GeneralizedEsdTest;
}

/// Whether a rejection family replaces values in its column or dispersion estimate.
///
/// Median replacement changes the gathered column and retains its survivors. Winsorized clipping
/// clamps only the working copy used to estimate dispersion, then removes rejected samples.
/// Neither needs a separate replacement buffer.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param rejection the family to test.
/// @return true for `MedianSigmaReplacement` and `WinsorizedSigmaClipping`.
[[nodiscard]] constexpr bool stackRejectionReplacesSamples(StackRejection rejection) noexcept
{
    return rejection == StackRejection::MedianSigmaReplacement || rejection == StackRejection::WinsorizedSigmaClipping;
}

/// The rejection's own name.
///
/// Thread-safe and allocation-free.
///
/// @param rejection the rejection to name. A value outside the enumeration is named
///        `"UnknownStackRejection"`.
/// @return a null-terminated string literal compiled into this library, which outlives every caller
///         and is never freed.
[[nodiscard]] AFTERNIGHTENGINE_API const char* stackRejectionName(StackRejection rejection) noexcept;

/// The two thresholds of a sigma family, in standard deviations of whatever dispersion the family
/// estimates, or in residual deviations for the linear fit.
struct SigmaClipThresholds
{
    /// How far below the centre a sample may sit and survive. Positive and finite.
    double low = kDefaultSigmaClipThreshold;

    /// How far above the centre a sample may sit and survive. Positive and finite.
    double high = kDefaultSigmaClipThreshold;

    /// Whether two records state the same thresholds.
    ///
    /// @param left one record.
    /// @param right the other.
    /// @return true when both thresholds are equal.
    [[nodiscard]] friend constexpr bool operator==(const SigmaClipThresholds& left,
                                                   const SigmaClipThresholds& right) noexcept = default;
};

/// The two fractions of the median-relative clipping.
struct MedianClipFractions
{
    /// The fraction of the median a sample may fall below it and survive. Positive and finite.
    double low = kDefaultLowMedianClipFraction;

    /// The fraction of the median a sample may rise above it and survive. Positive and finite.
    double high = kDefaultHighMedianClipFraction;

    /// Whether two records state the same fractions.
    ///
    /// @param left one record.
    /// @param right the other.
    /// @return true when both fractions are equal.
    [[nodiscard]] friend constexpr bool operator==(const MedianClipFractions& left,
                                                   const MedianClipFractions& right) noexcept = default;
};

/// The two parameters of the generalized extreme Studentized deviate test.
struct GesdtParameters
{
    /// The largest fraction of a pixel's samples the test may reject. Strictly between zero and
    /// one.
    double maximumOutlierFraction = kDefaultGesdtOutlierFraction;

    /// The significance level of each step of the test. Strictly between zero and one.
    double significance = kDefaultGesdtSignificance;

    /// Whether two records state the same parameters.
    ///
    /// @param left one record.
    /// @param right the other.
    /// @return true when both parameters are equal.
    [[nodiscard]] friend constexpr bool operator==(const GesdtParameters& left,
                                                   const GesdtParameters& right) noexcept = default;
};

/// Which samples survive, and the parameters of the family that decides.
///
/// Only the record the selected family reads is validated and read; the other two are inert.
/// A request therefore carries one meaningful record, and a family's parameters can never be
/// mistaken for another's because they are not the same field.
struct StackRejectionRequest
{
    /// The family.
    StackRejection method = StackRejection::None;

    /// Read by the four sigma families and the linear fit.
    SigmaClipThresholds sigma{};

    /// Read by the median-relative clipping.
    MedianClipFractions medianFractions{};

    /// Read by the generalized extreme Studentized deviate test.
    GesdtParameters gesdt{};

    /// Whether two requests state the same rejection.
    ///
    /// Every record participates, inert or not, so two requests that differ in a record the family
    /// does not read compare unequal; a comparison that hid the difference would make it harder to
    /// find rather than impossible to make.
    ///
    /// @param left one request.
    /// @param right the other.
    /// @return true when every member is equal.
    [[nodiscard]] friend constexpr bool operator==(const StackRejectionRequest& left,
                                                   const StackRejectionRequest& right) noexcept = default;
};

} // namespace an
