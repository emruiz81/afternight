// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// What a stack is asked to do, stated as small checked values.
///
/// This header says *what* is asked: the combination, the rejection with its own family's
/// parameters, the weighting, the normalization, the input's description and the output's, as
/// separate small values with one closed rule table over them. `StackModel::check` is that table -
/// which combination admits which rejection and which weighting, which parameters each family reads
/// and what range each accepts, which numeric domain a combination needs and what the output may be
/// - and it is a constant expression, so a request can be proved at compile time and refused by name
/// at run time. How a stack runs - passes, tiles, precision, scratch, the backend - is the planner's
/// decision, and the planner takes a checked model rather than a request.
///
/// Three requests are refused by name. Weights offered to a combination that cannot weight - a
/// median, an extremum, the auto-adaptive mean - are `WeightingNotAdmitted` rather than ignored. A
/// rejection over fewer than three frames, which cannot estimate a dispersion, is
/// `RejectionNeedsMoreFrames`. And a threshold read by the selected family outside the range that
/// family defines is refused.

#pragma once

#include <AfterNightEngine/Algorithms/Stacking/StackRequest.h>
#include <AfterNightEngine/Algorithms/Stacking/StackModelError.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Image/ComputePrecision.h>
#include <AfterNightEngine/Image/OperationEffect.h>

#include <limits>

namespace an
{

/// What a stack does to the image it produces.
///
/// It writes samples; it restates the description, because the output representation is its
/// own choice; it restates what the samples mean, because a combination of many frames is a new
/// processing step; it preserves an astrometric solution, because every frame was placed on the
/// reference frame's grid and the reference's solution still maps that grid; it drops the source
/// header cards, because no one file is the frame the result came from; it withdraws any output
/// decision, because the representation may have changed; and it regenerates validity as an
/// explicit mask, because a stack knows exactly which output pixels received a sample and which
/// received none, and a mask is the only description that can say so pixel by pixel.
inline constexpr OperationEffect kStackOperationEffect{
    .samples = SampleEffect::Writes,
    .description = DescriptionEffect::Restates,
    .semantics = SemanticEffect::Restates,
    .solution = SolutionEffect::Preserves,
    .codecMetadata = CodecMetadataEffect::Drops,
    .outputIntent = OutputIntentEffect::Withdraws,
    .validity = ValidityTransition{
        .policy = ValidityPolicy::Regenerate,
        .produced = *ValidityDescriptor::create(ValidityDescriptorRequest{.rule = ValidityRule::ExplicitMask})}};

/// A stack request that has been checked, and the facts the planner reads from it.
///
/// There is no way to build one from a request `check` refuses: the constructor is private and
/// `create` is the only way in. So a planner holding a `StackModel` knows the combination admits
/// the rejection and the weighting it was given, that every parameter the selected families read
/// is in range, that the reference frame exists, and that the output can be described. What it
/// does not know is anything about the frames themselves - their validity, their weights' values,
/// their memory - which arrive with the frames.
///
/// Trivially copyable and usable in a constant expression.
class StackModel
{
public:
    /// Whether a request describes a stack, and if not, why not.
    ///
    /// The order is a contract. Every enumerator is checked for membership first, so a rule is
    /// never applied to a value it cannot interpret; then the frame count and the reference; then
    /// the combination's row against the rejection, the weighting and the local weighting, each
    /// with the parameters its selected family reads; then the normalization scope against its
    /// method; then the domain the combination needs; and last the output. A request with two
    /// problems reports the same one on every build.
    ///
    /// Thread-safe, allocation-free, and usable in a constant expression.
    ///
    /// @param request the stack to check.
    /// @return `StackModelError::None` when a model can be built; otherwise the first rule that
    ///         refused.
    [[nodiscard]] static constexpr StackModelError check(const StackRequest& request) noexcept
    {
        if (!isStackCombination(request.combination))
        {
            return StackModelError::UnknownCombination;
        }
        if (!isStackRejection(request.rejection.method))
        {
            return StackModelError::UnknownRejection;
        }
        if (!isStackWeighting(request.weighting.method))
        {
            return StackModelError::UnknownWeighting;
        }
        if (!isStarCountScaling(request.weighting.starCount.scaling))
        {
            return StackModelError::UnknownStarCountScaling;
        }
        if (!isStackNormalizationScope(request.normalization.scope))
        {
            return StackModelError::UnknownNormalizationScope;
        }
        if (request.normalization.globalMethod.has_value() &&
            !isNormalizationMethod(*request.normalization.globalMethod))
        {
            return StackModelError::UnknownNormalizationMethod;
        }
        if (!isScaleEstimator(request.normalization.scaleEstimator))
        {
            return StackModelError::UnknownScaleEstimator;
        }
        if (!isRejectionMapForm(request.output.rejectionMaps))
        {
            return StackModelError::UnknownRejectionMapForm;
        }
        if (!isSampleType(request.output.sampleType))
        {
            return StackModelError::UnknownOutputSampleType;
        }

        if (request.input.frameCount == 0u)
        {
            return StackModelError::NoFrames;
        }
        if (request.input.frameCount > kMaxStackFrames)
        {
            return StackModelError::TooManyFrames;
        }
        if (request.normalization.referenceFrame >= request.input.frameCount)
        {
            return StackModelError::ReferenceFrameOutOfRange;
        }

        const StackCombinationCapabilities capabilities = stackCombinationCapabilities(request.combination);
        if (request.rejection.method != StackRejection::None)
        {
            if (!capabilities.admitsRejection)
            {
                return StackModelError::RejectionNotAdmitted;
            }
            if (request.input.frameCount < kMinimumRejectionFrames)
            {
                return StackModelError::RejectionNeedsMoreFrames;
            }
            const StackModelError rejection = checkRejection(request.rejection);
            if (rejection != StackModelError::None)
            {
                return rejection;
            }
        }

        if (request.weighting.method != StackWeighting::Uniform && !capabilities.admitsWeighting)
        {
            return StackModelError::WeightingNotAdmitted;
        }
        const StackModelError weighting = checkWeighting(request.weighting);
        if (weighting != StackModelError::None)
        {
            return weighting;
        }

        if (request.localWeighting.enabled)
        {
            if (!capabilities.admitsWeighting)
            {
                return StackModelError::LocalWeightingNotAdmitted;
            }
            const StackModelError local = checkLocalWeighting(request.localWeighting);
            if (local != StackModelError::None)
            {
                return local;
            }
        }

        if (stackNormalizationScopeUsesGlobalMethod(request.normalization.scope))
        {
            if (!request.normalization.globalMethod.has_value())
            {
                return StackModelError::GlobalNormalizationMethodMissing;
            }
        }
        else if (request.normalization.globalMethod.has_value())
        {
            return StackModelError::GlobalNormalizationMethodUnused;
        }

        if (capabilities.requiresNormalizedLinearDomain &&
            request.input.descriptor.numericDomain() != NumericDomain::NormalizedLinear)
        {
            return StackModelError::CombinationNeedsNormalizedLinearDomain;
        }

        if (!isFloatingSampleType(request.output.sampleType))
        {
            return StackModelError::OutputSampleTypeNotFloating;
        }
        if (request.output.rejectionMaps != RejectionMapForm::None && request.rejection.method == StackRejection::None)
        {
            return StackModelError::RejectionMapsWithoutRejection;
        }
        return StackModelError::None;
    }

    /// A model for this request, where it describes a stack.
    ///
    /// Thread-safe, allocation-free, and usable in a constant expression.
    ///
    /// @param request the stack to model.
    /// @return the model, or an empty optional exactly when `check` would name a reason. Nothing
    ///         is allocated and nothing is mutated on refusal.
    [[nodiscard]] static constexpr std::optional<StackModel> create(const StackRequest& request) noexcept
    {
        if (check(request) != StackModelError::None)
        {
            return std::nullopt;
        }
        return StackModel{request};
    }

    /// The request this model was checked from.
    ///
    /// @return the request, unchanged.
    [[nodiscard]] constexpr const StackRequest& request() const noexcept
    {
        return m_request;
    }

    /// The combination's capability row.
    ///
    /// @return the row `check` applied.
    [[nodiscard]] constexpr StackCombinationCapabilities capabilities() const noexcept
    {
        return stackCombinationCapabilities(m_request.combination);
    }

    /// Whether some samples may not survive into the combination.
    ///
    /// @return true when the rejection is not `StackRejection::None`.
    [[nodiscard]] constexpr bool usesRejection() const noexcept
    {
        return m_request.rejection.method != StackRejection::None;
    }

    /// Whether frames carry weights other than one.
    ///
    /// @return true when the weighting source is not `StackWeighting::Uniform`.
    [[nodiscard]] constexpr bool usesFrameWeights() const noexcept
    {
        return m_request.weighting.method != StackWeighting::Uniform;
    }

    /// Whether a local weight surface is applied.
    ///
    /// @return true when the local weighting is enabled.
    [[nodiscard]] constexpr bool usesLocalWeights() const noexcept
    {
        return m_request.localWeighting.enabled;
    }

    /// Whether any rejection map is written.
    ///
    /// @return true when the output request names a map form other than `RejectionMapForm::None`.
    [[nodiscard]] constexpr bool producesRejectionMaps() const noexcept
    {
        return m_request.output.rejectionMaps != RejectionMapForm::None;
    }

    /// The width the stack accumulates in.
    ///
    /// Float64 for every combination and every input representation: partial sums over many
    /// frames outgrow the precision of the samples going into them, which is the approved
    /// `Float64Justification::StackingAccumulation`. The output is narrowed once, at the end,
    /// to `StackOutputRequest::sampleType`.
    ///
    /// @return `ComputePrecision::Float64`.
    [[nodiscard]] constexpr ComputePrecision accumulationPrecision() const noexcept
    {
        return ComputePrecision::Float64;
    }

    /// Why the stack accumulates in Float64.
    ///
    /// @return `Float64Justification::StackingAccumulation`.
    [[nodiscard]] constexpr Float64Justification accumulationJustification() const noexcept
    {
        return Float64Justification::StackingAccumulation;
    }

    /// What the stack promises about the values it writes.
    ///
    /// A combined value is a statistic of its samples, not one of them, so it is normalized or
    /// lossy under every input representation; no stored code value round-trips through a stack.
    ///
    /// @return `OutputPrecisionContract::NormalizedOrLossy`.
    [[nodiscard]] constexpr OutputPrecisionContract outputContract() const noexcept
    {
        return OutputPrecisionContract::NormalizedOrLossy;
    }

    /// The description of the combined image.
    ///
    /// The input description with the representation replaced by the requested output
    /// representation and nothing else changed: the extent, schema, basis, domain, filter array,
    /// origin and aspect are the frames'. Always describable, because a floating representation
    /// carries every domain an input could have had, and `check` proved the rest.
    ///
    /// @return the output descriptor.
    [[nodiscard]] constexpr ImageDescriptor outputDescriptor() const noexcept
    {
        ImageDescriptorRequest output = m_request.input.descriptor.toRequest();
        output.sampleType = m_request.output.sampleType;
        return *ImageDescriptor::create(output);
    }

    /// Whether two models were checked from equal requests.
    ///
    /// Not `noexcept`, because the request's comparison is not.
    ///
    /// @param left one model.
    /// @param right the other.
    /// @return true when the requests are equal.
    [[nodiscard]] friend constexpr bool operator==(const StackModel& left, const StackModel& right) = default;

private:
    /// Builds the value once the request has been checked. Private, so that every model in
    /// existence has been through `check`.
    ///
    /// @param request the checked request.
    constexpr explicit StackModel(const StackRequest& request) noexcept : m_request(request) {}

    /// Whether a value is a finite number, without leaving a constant expression.
    ///
    /// `std::isfinite` is not usable here, so the comparisons do the work: not-a-number fails the
    /// first because every comparison against it is false, and the infinities fail one of the
    /// other two.
    ///
    /// @param value the value to test.
    /// @return true when the value is finite.
    [[nodiscard]] static constexpr bool isFinite(double value) noexcept
    {
        return value == value && value <= (std::numeric_limits<double>::max)() &&
               value >= std::numeric_limits<double>::lowest();
    }

    /// Whether a value is a finite number greater than zero.
    ///
    /// @param value the value to test.
    /// @return true when the value is positive and finite.
    [[nodiscard]] static constexpr bool isPositiveFinite(double value) noexcept
    {
        return isFinite(value) && value > 0.0;
    }

    /// Whether a value lies in the closed unit interval.
    ///
    /// @param value the value to test.
    /// @return true when the value is between zero and one inclusive.
    [[nodiscard]] static constexpr bool isUnitInterval(double value) noexcept
    {
        return value >= 0.0 && value <= 1.0;
    }

    /// Whether a value lies strictly inside the unit interval.
    ///
    /// @param value the value to test.
    /// @return true when the value is above zero and below one.
    [[nodiscard]] static constexpr bool isOpenUnitInterval(double value) noexcept
    {
        return value > 0.0 && value < 1.0;
    }

    /// The parameters of the selected rejection family, and nothing else.
    ///
    /// @param rejection the rejection request, whose family is already known to be one this
    ///        build defines and not `StackRejection::None`.
    /// @return `StackModelError::None`, or the first parameter out of its range.
    [[nodiscard]] static constexpr StackModelError checkRejection(const StackRejectionRequest& rejection) noexcept
    {
        if (stackRejectionUsesSigmaThresholds(rejection.method))
        {
            if (!isPositiveFinite(rejection.sigma.low) || !isPositiveFinite(rejection.sigma.high))
            {
                return StackModelError::SigmaThresholdNotPositive;
            }
        }
        if (stackRejectionUsesMedianFractions(rejection.method))
        {
            if (!isPositiveFinite(rejection.medianFractions.low) || !isPositiveFinite(rejection.medianFractions.high))
            {
                return StackModelError::MedianFractionNotPositive;
            }
        }
        if (stackRejectionUsesGesdtParameters(rejection.method))
        {
            if (!isOpenUnitInterval(rejection.gesdt.maximumOutlierFraction))
            {
                return StackModelError::GesdtOutlierFractionOutOfRange;
            }
            if (!isOpenUnitInterval(rejection.gesdt.significance))
            {
                return StackModelError::GesdtSignificanceOutOfRange;
            }
        }
        return StackModelError::None;
    }

    /// The parameters of the selected weighting source, and the one flag that names a source.
    ///
    /// @param weighting the weighting request, whose source and scaling are already known to be
    ///        ones this build defines.
    /// @return `StackModelError::None`, or the first parameter out of its range.
    [[nodiscard]] static constexpr StackModelError checkWeighting(const StackWeightingRequest& weighting) noexcept
    {
        if (stackWeightingUsesMinimumWeight(weighting.method) && !isUnitInterval(weighting.minimumWeight))
        {
            return StackModelError::MinimumWeightOutOfRange;
        }
        if (weighting.method == StackWeighting::StarCount &&
            weighting.starCount.scaling == StarCountScaling::Exponential &&
            !isPositiveFinite(weighting.starCount.exponentialAlpha))
        {
            return StackModelError::ExponentialAlphaNotPositive;
        }
        if (weighting.method == StackWeighting::Photometric)
        {
            const PhotometricWeightingRequest& photometric = weighting.photometric;
            if (!isFinite(photometric.uncertaintyPenaltyPerMagnitude) ||
                photometric.uncertaintyPenaltyPerMagnitude < 0.0 || !isFinite(photometric.alignmentPenalty) ||
                photometric.alignmentPenalty < 0.0)
            {
                return StackModelError::PhotometricPenaltyOutOfRange;
            }
            if (!isUnitInterval(photometric.minimumRawWeight))
            {
                return StackModelError::PhotometricMinimumWeightOutOfRange;
            }
        }
        if (weighting.perChannelFwhm && weighting.method != StackWeighting::Fwhm)
        {
            return StackModelError::PerChannelFwhmWithoutFwhmWeighting;
        }
        return StackModelError::None;
    }

    /// The parameters of an enabled local weighting.
    ///
    /// @param local the local weighting request, already known to be enabled.
    /// @return `StackModelError::None`, or the first parameter out of its range.
    [[nodiscard]] static constexpr StackModelError checkLocalWeighting(const LocalWeightingRequest& local) noexcept
    {
        if (local.windowSize < kMinimumLocalWeightWindow)
        {
            return StackModelError::LocalWeightWindowTooSmall;
        }
        if (local.windowSize % 2u == 0u)
        {
            return StackModelError::LocalWeightWindowNotOdd;
        }
        if (local.gridSpacing < kMinimumLocalWeightGridSpacing)
        {
            return StackModelError::LocalWeightGridSpacingTooSmall;
        }
        if (!isPositiveFinite(local.snrExponent))
        {
            return StackModelError::LocalWeightSnrExponentNotPositive;
        }
        return StackModelError::None;
    }

    /// The checked request.
    StackRequest m_request;
};

/// A checked model for a request, or a typed failure saying why there is none.
///
/// The same rules as `StackModel::check`, reported the way an operation reports: the code is
/// `stackModelErrorCode` of the reason, the message names the reason and says it in words, the
/// operation is this function, the domain is `ErrorDomain::Algorithms`, and a refusal about a
/// count or an index carries the requested value and its limit as context fields while one about a
/// parameter carries the parameter's name.
///
/// Thread-safe and reentrant. Allocates only a failure it has to describe.
///
/// @param request the stack to model.
/// @return the model, or the failure.
[[nodiscard]] AFTERNIGHTENGINE_API Result<StackModel> makeStackModel(const StackRequest& request);

} // namespace an
