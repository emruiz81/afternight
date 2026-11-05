// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - name tables and the typed failures of the stacking model.
//
// Every rule of the model is a constant expression in its header, so this unit holds what a
// constant expression cannot: the name of each enumerator for a person, the sentence each refusal
// is read as, the failure record a planner receives, and the one check that walks caller-owned
// spans. None of the switches carries a `default` label, so an enumerator added without a name
// here is a compile failure rather than a blank in somebody's log.

#include <AfterNightEngine/Algorithms/Stacking/StackingModel.h>

#include <AfterNightEngine/Algorithms/Stacking/StackFrameWeighting.h>
#include <AfterNightEngine/Algorithms/Stacking/StackModelError.h>
#include <AfterNightEngine/Algorithms/Stacking/StackRequest.h>
#include <AfterNightEngine/Algorithms/Stacking/StackOutput.h>
#include <AfterNightEngine/Algorithms/Stacking/StackNormalizationParameters.h>
#include <AfterNightEngine/Algorithms/Stacking/StackWeightingParameters.h>
#include <AfterNightEngine/Algorithms/Stacking/StackRejectionParameters.h>
#include <AfterNightEngine/Algorithms/Stacking/StackCombination.h>

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/Status.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <utility>

namespace an
{
namespace
{

constexpr const char* kMakeOperation = "an::makeStackModel";
constexpr const char* kWeightsOperation = "an::checkStackFrameWeights";

/// The sentence a person reads for a refusal, opening with the name a machine reads.
///
/// @param error the reason.
/// @return a string literal; a sentence naming no reason for a value outside the enumeration.
[[nodiscard]] const char* stackModelErrorMessage(StackModelError error) noexcept
{
    switch (error)
    {
    case StackModelError::None:
        return "None: the request describes a stack";
    case StackModelError::UnknownCombination:
        return "UnknownCombination: the combination is not one this build defines";
    case StackModelError::UnknownRejection:
        return "UnknownRejection: the rejection is not one this build defines";
    case StackModelError::UnknownWeighting:
        return "UnknownWeighting: the weighting source is not one this build defines";
    case StackModelError::UnknownStarCountScaling:
        return "UnknownStarCountScaling: the star-count scaling is not one this build defines";
    case StackModelError::UnknownNormalizationScope:
        return "UnknownNormalizationScope: the normalization scope is not one this build defines";
    case StackModelError::UnknownNormalizationMethod:
        return "UnknownNormalizationMethod: the global normalization method is not one this build defines";
    case StackModelError::UnknownScaleEstimator:
        return "UnknownScaleEstimator: the scale estimator is not one this build defines";
    case StackModelError::UnknownRejectionMapForm:
        return "UnknownRejectionMapForm: the rejection-map form is not one this build defines";
    case StackModelError::UnknownOutputSampleType:
        return "UnknownOutputSampleType: the output representation is not one this engine stores";
    case StackModelError::NoFrames:
        return "NoFrames: a stack of no frames has nothing to combine";
    case StackModelError::TooManyFrames:
        return "TooManyFrames: the frame count exceeds the stated ceiling";
    case StackModelError::ReferenceFrameOutOfRange:
        return "ReferenceFrameOutOfRange: the normalization reference is not an index below the frame count";
    case StackModelError::RejectionNotAdmitted:
        return "RejectionNotAdmitted: the combination does not admit a rejection";
    case StackModelError::RejectionNeedsMoreFrames:
        return "RejectionNeedsMoreFrames: a rejection cannot judge fewer than three frames";
    case StackModelError::SigmaThresholdNotPositive:
        return "SigmaThresholdNotPositive: a sigma threshold read by the selected family is not positive and finite";
    case StackModelError::MedianFractionNotPositive:
        return "MedianFractionNotPositive: a median-relative fraction is not positive and finite";
    case StackModelError::GesdtOutlierFractionOutOfRange:
        return "GesdtOutlierFractionOutOfRange: the outlier fraction is not strictly between zero and one";
    case StackModelError::GesdtSignificanceOutOfRange:
        return "GesdtSignificanceOutOfRange: the significance is not strictly between zero and one";
    case StackModelError::WeightingNotAdmitted:
        return "WeightingNotAdmitted: the combination does not admit frame weights";
    case StackModelError::MinimumWeightOutOfRange:
        return "MinimumWeightOutOfRange: the weight floor is not in the closed unit interval";
    case StackModelError::ExponentialAlphaNotPositive:
        return "ExponentialAlphaNotPositive: the exponential scaling's steepness is not positive and finite";
    case StackModelError::PhotometricPenaltyOutOfRange:
        return "PhotometricPenaltyOutOfRange: a photometric penalty is not finite and non-negative";
    case StackModelError::PhotometricMinimumWeightOutOfRange:
        return "PhotometricMinimumWeightOutOfRange: the photometric raw-weight floor is not in the closed unit "
               "interval";
    case StackModelError::PerChannelFwhmWithoutFwhmWeighting:
        return "PerChannelFwhmWithoutFwhmWeighting: per-channel width weighting was stated for another source";
    case StackModelError::LocalWeightingNotAdmitted:
        return "LocalWeightingNotAdmitted: the combination does not admit a local weight surface";
    case StackModelError::LocalWeightWindowTooSmall:
        return "LocalWeightWindowTooSmall: the local weight window is narrower than three pixels";
    case StackModelError::LocalWeightWindowNotOdd:
        return "LocalWeightWindowNotOdd: the local weight window has no centre pixel";
    case StackModelError::LocalWeightGridSpacingTooSmall:
        return "LocalWeightGridSpacingTooSmall: the local weight grid is finer than four pixels";
    case StackModelError::LocalWeightSnrExponentNotPositive:
        return "LocalWeightSnrExponentNotPositive: the local weight exponent is not positive and finite";
    case StackModelError::GlobalNormalizationMethodMissing:
        return "GlobalNormalizationMethodMissing: the scope applies a global method and none was named";
    case StackModelError::GlobalNormalizationMethodUnused:
        return "GlobalNormalizationMethodUnused: a global method was named under a scope that applies none";
    case StackModelError::CombinationNeedsNormalizedLinearDomain:
        return "CombinationNeedsNormalizedLinearDomain: the combination needs normalized linear input";
    case StackModelError::OutputSampleTypeNotFloating:
        return "OutputSampleTypeNotFloating: the output representation must be Float32 or Float64";
    case StackModelError::RejectionMapsWithoutRejection:
        return "RejectionMapsWithoutRejection: rejection maps need a rejection to count";
    }
    return "the stack request was refused for a reason this build cannot name";
}

/// The request field a refusal is about, spelled as a caller would write it.
///
/// @param error the reason.
/// @return the field's path, or null for a refusal that names no single field.
[[nodiscard]] const char* stackModelErrorParameter(StackModelError error) noexcept
{
    switch (error)
    {
    case StackModelError::None:
    case StackModelError::NoFrames:
    case StackModelError::TooManyFrames:
    case StackModelError::RejectionNeedsMoreFrames:
        return nullptr;
    case StackModelError::UnknownCombination:
        return "combination";
    case StackModelError::UnknownRejection:
    case StackModelError::RejectionNotAdmitted:
        return "rejection.method";
    case StackModelError::UnknownWeighting:
    case StackModelError::WeightingNotAdmitted:
        return "weighting.method";
    case StackModelError::UnknownStarCountScaling:
        return "weighting.starCount.scaling";
    case StackModelError::UnknownNormalizationScope:
        return "normalization.scope";
    case StackModelError::UnknownNormalizationMethod:
    case StackModelError::GlobalNormalizationMethodMissing:
    case StackModelError::GlobalNormalizationMethodUnused:
        return "normalization.globalMethod";
    case StackModelError::UnknownScaleEstimator:
        return "normalization.scaleEstimator";
    case StackModelError::UnknownRejectionMapForm:
    case StackModelError::RejectionMapsWithoutRejection:
        return "output.rejectionMaps";
    case StackModelError::UnknownOutputSampleType:
    case StackModelError::OutputSampleTypeNotFloating:
        return "output.sampleType";
    case StackModelError::ReferenceFrameOutOfRange:
        return "normalization.referenceFrame";
    case StackModelError::SigmaThresholdNotPositive:
        return "rejection.sigma";
    case StackModelError::MedianFractionNotPositive:
        return "rejection.medianFractions";
    case StackModelError::GesdtOutlierFractionOutOfRange:
        return "rejection.gesdt.maximumOutlierFraction";
    case StackModelError::GesdtSignificanceOutOfRange:
        return "rejection.gesdt.significance";
    case StackModelError::MinimumWeightOutOfRange:
        return "weighting.minimumWeight";
    case StackModelError::ExponentialAlphaNotPositive:
        return "weighting.starCount.exponentialAlpha";
    case StackModelError::PhotometricPenaltyOutOfRange:
        return "weighting.photometric";
    case StackModelError::PhotometricMinimumWeightOutOfRange:
        return "weighting.photometric.minimumRawWeight";
    case StackModelError::PerChannelFwhmWithoutFwhmWeighting:
        return "weighting.perChannelFwhm";
    case StackModelError::LocalWeightingNotAdmitted:
        return "localWeighting.enabled";
    case StackModelError::LocalWeightWindowTooSmall:
    case StackModelError::LocalWeightWindowNotOdd:
        return "localWeighting.windowSize";
    case StackModelError::LocalWeightGridSpacingTooSmall:
        return "localWeighting.gridSpacing";
    case StackModelError::LocalWeightSnrExponentNotPositive:
        return "localWeighting.snrExponent";
    case StackModelError::CombinationNeedsNormalizedLinearDomain:
        return "input.descriptor";
    }
    return nullptr;
}

/// Builds one failure of the explicit-weights check.
///
/// @param code why the weights were refused.
/// @param message what a person reads.
/// @param context the typed facts, if any.
/// @return the failure.
[[nodiscard]] Status weightsFailure(ErrorCode code, const char* message, std::vector<StatusContextField> context = {})
{
    return Status::failure(code, StatusDetail{.message = message,
                                              .operation = kWeightsOperation,
                                              .domain = ErrorDomain::Algorithms,
                                              .context = std::move(context)});
}

/// The name of one weight, for a failure that has to point at it.
///
/// @param span which span.
/// @param index the position within it.
/// @return `span[index]`, spelled out.
[[nodiscard]] std::string weightName(const char* span, std::size_t index)
{
    std::string name = span;
    name += '[';
    name += std::to_string(index);
    name += ']';
    return name;
}

/// Refuses a span whose length is neither empty nor the expected one.
///
/// @param span the name of the span.
/// @param given its length.
/// @param expected the length the stack needs.
/// @return success when the lengths agree, or the failure.
[[nodiscard]] Status checkWeightSpanLength(const char* span, std::size_t given, std::uint64_t expected)
{
    if (given == expected)
    {
        return Status::success();
    }
    return weightsFailure(ErrorCode::IncompatibleArguments,
                          "the weight span does not have one entry per frame or per frame and channel",
                          {StatusContextField::ofText(ContextKey::Parameter, span),
                           StatusContextField::ofCount(ContextKey::Requested, given),
                           StatusContextField::ofCount(ContextKey::Total, expected)});
}

/// Refuses a weight that is not a finite non-negative number.
///
/// @param span the name of the span.
/// @param values the span.
/// @return success when every value is admissible, or the failure naming the first that is not.
[[nodiscard]] Status checkWeightValues(const char* span, std::span<const double> values)
{
    for (std::size_t index = 0u; index < values.size(); ++index)
    {
        const double value = values[index];
        if (!std::isfinite(value) || value < 0.0)
        {
            return weightsFailure(ErrorCode::InvalidArgument, "a frame weight is not a finite non-negative number",
                                  {StatusContextField::ofText(ContextKey::Parameter, weightName(span, index))});
        }
    }
    return Status::success();
}

} // namespace

const char* stackCombinationName(StackCombination combination) noexcept
{
    switch (combination)
    {
    case StackCombination::Average:
        return "Average";
    case StackCombination::Sum:
        return "Sum";
    case StackCombination::Median:
        return "Median";
    case StackCombination::Minimum:
        return "Minimum";
    case StackCombination::Maximum:
        return "Maximum";
    case StackCombination::AutoAdaptiveWeightedAverage:
        return "AutoAdaptiveWeightedAverage";
    }
    return "UnknownStackCombination";
}

const char* stackRejectionName(StackRejection rejection) noexcept
{
    switch (rejection)
    {
    case StackRejection::None:
        return "None";
    case StackRejection::SigmaClipping:
        return "SigmaClipping";
    case StackRejection::MadSigmaClipping:
        return "MadSigmaClipping";
    case StackRejection::MedianSigmaReplacement:
        return "MedianSigmaReplacement";
    case StackRejection::WinsorizedSigmaClipping:
        return "WinsorizedSigmaClipping";
    case StackRejection::LinearFitClipping:
        return "LinearFitClipping";
    case StackRejection::PercentileClipping:
        return "PercentileClipping";
    case StackRejection::GeneralizedEsdTest:
        return "GeneralizedEsdTest";
    }
    return "UnknownStackRejection";
}

const char* stackWeightingName(StackWeighting weighting) noexcept
{
    switch (weighting)
    {
    case StackWeighting::Uniform:
        return "Uniform";
    case StackWeighting::Explicit:
        return "Explicit";
    case StackWeighting::Noise:
        return "Noise";
    case StackWeighting::StarCount:
        return "StarCount";
    case StackWeighting::Fwhm:
        return "Fwhm";
    case StackWeighting::ImageCount:
        return "ImageCount";
    case StackWeighting::Photometric:
        return "Photometric";
    case StackWeighting::Exposure:
        return "Exposure";
    case StackWeighting::SignalToNoise:
        return "SignalToNoise";
    }
    return "UnknownStackWeighting";
}

const char* starCountScalingName(StarCountScaling scaling) noexcept
{
    switch (scaling)
    {
    case StarCountScaling::Linear:
        return "Linear";
    case StarCountScaling::Quadratic:
        return "Quadratic";
    case StarCountScaling::Exponential:
        return "Exponential";
    case StarCountScaling::RoundnessWeighted:
        return "RoundnessWeighted";
    }
    return "UnknownStarCountScaling";
}

const char* stackNormalizationScopeName(StackNormalizationScope scope) noexcept
{
    switch (scope)
    {
    case StackNormalizationScope::None:
        return "None";
    case StackNormalizationScope::Global:
        return "Global";
    case StackNormalizationScope::Local:
        return "Local";
    case StackNormalizationScope::LocalThenGlobal:
        return "LocalThenGlobal";
    }
    return "UnknownStackNormalizationScope";
}

const char* rejectionMapFormName(RejectionMapForm form) noexcept
{
    switch (form)
    {
    case RejectionMapForm::None:
        return "None";
    case RejectionMapForm::Merged:
        return "Merged";
    case RejectionMapForm::LowAndHigh:
        return "LowAndHigh";
    }
    return "UnknownRejectionMapForm";
}

const char* stackModelErrorName(StackModelError error) noexcept
{
    switch (error)
    {
    case StackModelError::None:
        return "None";
    case StackModelError::UnknownCombination:
        return "UnknownCombination";
    case StackModelError::UnknownRejection:
        return "UnknownRejection";
    case StackModelError::UnknownWeighting:
        return "UnknownWeighting";
    case StackModelError::UnknownStarCountScaling:
        return "UnknownStarCountScaling";
    case StackModelError::UnknownNormalizationScope:
        return "UnknownNormalizationScope";
    case StackModelError::UnknownNormalizationMethod:
        return "UnknownNormalizationMethod";
    case StackModelError::UnknownScaleEstimator:
        return "UnknownScaleEstimator";
    case StackModelError::UnknownRejectionMapForm:
        return "UnknownRejectionMapForm";
    case StackModelError::UnknownOutputSampleType:
        return "UnknownOutputSampleType";
    case StackModelError::NoFrames:
        return "NoFrames";
    case StackModelError::TooManyFrames:
        return "TooManyFrames";
    case StackModelError::ReferenceFrameOutOfRange:
        return "ReferenceFrameOutOfRange";
    case StackModelError::RejectionNotAdmitted:
        return "RejectionNotAdmitted";
    case StackModelError::RejectionNeedsMoreFrames:
        return "RejectionNeedsMoreFrames";
    case StackModelError::SigmaThresholdNotPositive:
        return "SigmaThresholdNotPositive";
    case StackModelError::MedianFractionNotPositive:
        return "MedianFractionNotPositive";
    case StackModelError::GesdtOutlierFractionOutOfRange:
        return "GesdtOutlierFractionOutOfRange";
    case StackModelError::GesdtSignificanceOutOfRange:
        return "GesdtSignificanceOutOfRange";
    case StackModelError::WeightingNotAdmitted:
        return "WeightingNotAdmitted";
    case StackModelError::MinimumWeightOutOfRange:
        return "MinimumWeightOutOfRange";
    case StackModelError::ExponentialAlphaNotPositive:
        return "ExponentialAlphaNotPositive";
    case StackModelError::PhotometricPenaltyOutOfRange:
        return "PhotometricPenaltyOutOfRange";
    case StackModelError::PhotometricMinimumWeightOutOfRange:
        return "PhotometricMinimumWeightOutOfRange";
    case StackModelError::PerChannelFwhmWithoutFwhmWeighting:
        return "PerChannelFwhmWithoutFwhmWeighting";
    case StackModelError::LocalWeightingNotAdmitted:
        return "LocalWeightingNotAdmitted";
    case StackModelError::LocalWeightWindowTooSmall:
        return "LocalWeightWindowTooSmall";
    case StackModelError::LocalWeightWindowNotOdd:
        return "LocalWeightWindowNotOdd";
    case StackModelError::LocalWeightGridSpacingTooSmall:
        return "LocalWeightGridSpacingTooSmall";
    case StackModelError::LocalWeightSnrExponentNotPositive:
        return "LocalWeightSnrExponentNotPositive";
    case StackModelError::GlobalNormalizationMethodMissing:
        return "GlobalNormalizationMethodMissing";
    case StackModelError::GlobalNormalizationMethodUnused:
        return "GlobalNormalizationMethodUnused";
    case StackModelError::CombinationNeedsNormalizedLinearDomain:
        return "CombinationNeedsNormalizedLinearDomain";
    case StackModelError::OutputSampleTypeNotFloating:
        return "OutputSampleTypeNotFloating";
    case StackModelError::RejectionMapsWithoutRejection:
        return "RejectionMapsWithoutRejection";
    }
    return "UnknownStackModelError";
}

Result<StackModel> makeStackModel(const StackRequest& request)
{
    const StackModelError error = StackModel::check(request);
    if (error == StackModelError::None)
    {
        return *StackModel::create(request);
    }

    std::vector<StatusContextField> context;
    if (const char* parameter = stackModelErrorParameter(error); parameter != nullptr)
    {
        context.push_back(StatusContextField::ofText(ContextKey::Parameter, parameter));
    }
    switch (error)
    {
    case StackModelError::TooManyFrames:
        context.push_back(StatusContextField::ofCount(ContextKey::Requested, request.input.frameCount));
        context.push_back(StatusContextField::ofCount(ContextKey::Limit, kMaxStackFrames));
        break;
    case StackModelError::RejectionNeedsMoreFrames:
        context.push_back(StatusContextField::ofCount(ContextKey::Requested, request.input.frameCount));
        context.push_back(StatusContextField::ofCount(ContextKey::Limit, kMinimumRejectionFrames));
        break;
    case StackModelError::ReferenceFrameOutOfRange:
        context.push_back(StatusContextField::ofCount(ContextKey::Requested, request.normalization.referenceFrame));
        context.push_back(StatusContextField::ofCount(ContextKey::Limit, request.input.frameCount));
        break;
    case StackModelError::None:
    case StackModelError::UnknownCombination:
    case StackModelError::UnknownRejection:
    case StackModelError::UnknownWeighting:
    case StackModelError::UnknownStarCountScaling:
    case StackModelError::UnknownNormalizationScope:
    case StackModelError::UnknownNormalizationMethod:
    case StackModelError::UnknownScaleEstimator:
    case StackModelError::UnknownRejectionMapForm:
    case StackModelError::UnknownOutputSampleType:
    case StackModelError::NoFrames:
    case StackModelError::RejectionNotAdmitted:
    case StackModelError::SigmaThresholdNotPositive:
    case StackModelError::MedianFractionNotPositive:
    case StackModelError::GesdtOutlierFractionOutOfRange:
    case StackModelError::GesdtSignificanceOutOfRange:
    case StackModelError::WeightingNotAdmitted:
    case StackModelError::MinimumWeightOutOfRange:
    case StackModelError::ExponentialAlphaNotPositive:
    case StackModelError::PhotometricPenaltyOutOfRange:
    case StackModelError::PhotometricMinimumWeightOutOfRange:
    case StackModelError::PerChannelFwhmWithoutFwhmWeighting:
    case StackModelError::LocalWeightingNotAdmitted:
    case StackModelError::LocalWeightWindowTooSmall:
    case StackModelError::LocalWeightWindowNotOdd:
    case StackModelError::LocalWeightGridSpacingTooSmall:
    case StackModelError::LocalWeightSnrExponentNotPositive:
    case StackModelError::GlobalNormalizationMethodMissing:
    case StackModelError::GlobalNormalizationMethodUnused:
    case StackModelError::CombinationNeedsNormalizedLinearDomain:
    case StackModelError::OutputSampleTypeNotFloating:
    case StackModelError::RejectionMapsWithoutRejection:
        break;
    }

    return Status::failure(stackModelErrorCode(error), StatusDetail{.message = stackModelErrorMessage(error),
                                                                    .operation = kMakeOperation,
                                                                    .domain = ErrorDomain::Algorithms,
                                                                    .context = std::move(context)});
}

Status checkStackFrameWeights(const StackModel& model, const StackFrameWeights& weights)
{
    const StackRequest& request = model.request();
    const std::uint64_t frames = request.input.frameCount;
    const std::uint64_t channels = request.input.descriptor.extent().channelCount();

    if (request.weighting.method != StackWeighting::Explicit)
    {
        if (!weights.perFrame.empty() || !weights.perFrameChannel.empty())
        {
            return weightsFailure(ErrorCode::IncompatibleArguments,
                                  "explicit weights were supplied to a stack whose weighting source derives its own",
                                  {StatusContextField::ofText(ContextKey::Parameter, "weighting.method")});
        }
        return Status::success();
    }

    if (weights.perFrame.empty() && weights.perFrameChannel.empty())
    {
        return weightsFailure(ErrorCode::IncompatibleArguments,
                              "explicit weighting was stated and no weights were supplied");
    }
    if (!weights.perFrame.empty() && !weights.perFrameChannel.empty())
    {
        return weightsFailure(ErrorCode::IncompatibleArguments,
                              "weights are stated once, per frame or per frame and channel, never both");
    }

    if (!weights.perFrame.empty())
    {
        if (Status length = checkWeightSpanLength("perFrame", weights.perFrame.size(), frames); length.failed())
        {
            return length;
        }
        if (Status values = checkWeightValues("perFrame", weights.perFrame); values.failed())
        {
            return values;
        }
        bool anyPositive = false;
        for (const double value : weights.perFrame)
        {
            anyPositive = anyPositive || value > 0.0;
        }
        if (!anyPositive)
        {
            return weightsFailure(ErrorCode::EmptySelection, "no frame weighs more than zero",
                                  {StatusContextField::ofText(ContextKey::Parameter, "perFrame")});
        }
        return Status::success();
    }

    if (Status length = checkWeightSpanLength("perFrameChannel", weights.perFrameChannel.size(), frames * channels);
        length.failed())
    {
        return length;
    }
    if (Status values = checkWeightValues("perFrameChannel", weights.perFrameChannel); values.failed())
    {
        return values;
    }
    for (std::uint64_t channel = 0u; channel < channels; ++channel)
    {
        bool anyPositive = false;
        for (std::uint64_t frame = 0u; frame < frames; ++frame)
        {
            anyPositive =
                anyPositive || weights.perFrameChannel[static_cast<std::size_t>(frame * channels + channel)] > 0.0;
        }
        if (!anyPositive)
        {
            return weightsFailure(ErrorCode::EmptySelection, "no frame weighs more than zero for one channel",
                                  {StatusContextField::ofText(ContextKey::Parameter, "perFrameChannel"),
                                   StatusContextField::ofCount(ContextKey::Requested, channel)});
        }
    }
    return Status::success();
}

} // namespace an
