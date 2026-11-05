// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the drizzle model's checks, the resolved output description and the
// operation's declared effect.
//
// The checks run in a fixed order and each refusal names the request field it concerns. The output
// description is the input's with the resolved canvas and the requested floating type; a CFA
// source becomes three RGB planes with no filter array, and a peak-normalized sum becomes
// NormalizedLinear unless the source is EncodedNonlinear. The canvas dimensions are computed in
// double and every byte count through `CheckedSize`, so an oversized canvas is refused rather than
// wrapped.

#include <AfterNightEngine/Algorithms/Drizzle/DrizzleModel.h>

#include <AfterNightEngine/Core/CheckedSize.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/Status.h>

#include <cmath>
#include <limits>

namespace an
{
namespace
{

Status refusal(ErrorCode code, const char* parameter, const char* message)
{
    return Status::failure(code,
                           StatusDetail{.message = message,
                                        .operation = "an::DrizzleModel::create",
                                        .domain = ErrorDomain::Algorithms,
                                        .context = {StatusContextField::ofText(ContextKey::Parameter, parameter)}});
}

bool fitsImage(const ImageDescriptor& descriptor, const EngineLimits& limits)
{
    const ImageExtent& extent = descriptor.extent();
    const CheckedSize bytes = CheckedSize{extent.width()} * extent.height() * extent.channelCount() *
                              sampleTypeSizeBytes(descriptor.sampleType());
    return ImageExtent::create(extent.width(), extent.height(), extent.channelCount(), limits).has_value() &&
           bytes.toOffset().has_value() && bytes.valueOr(0u) <= limits.maxImageBytes;
}

Status checkAuxiliary(const ImageDescriptor& source, const std::optional<ImageDescriptor>& auxiliary,
                      const char* parameter, const EngineLimits& limits)
{
    if (!auxiliary.has_value())
        return Status::success();
    const ImageExtent& extent = auxiliary->extent();
    if (extent.width() != source.extent().width() || extent.height() != source.extent().height() ||
        (extent.channelCount() != 1u && extent.channelCount() != source.extent().channelCount()) ||
        auxiliary->coordinateOrigin() != source.coordinateOrigin() ||
        auxiliary->pixelAspectRatio() != source.pixelAspectRatio())
        return refusal(ErrorCode::IncompatibleArguments, parameter, "weight geometry must match the source");
    if ((auxiliary->sampleType() != SampleType::Float32 && auxiliary->sampleType() != SampleType::Float64) ||
        auxiliary->numericDomain() != NumericDomain::NormalizedLinear ||
        auxiliary->cfa().pattern() != CfaPattern::None || auxiliary->colorSpace() != ColorSpace::Unspecified ||
        (extent.channelCount() == 1u ? auxiliary->channelSchema() != ChannelSchema::Mono
                                     : auxiliary->channelSchema() != source.channelSchema()))
        return refusal(ErrorCode::IncompatibleArguments, parameter,
                       "weights must be dimensionless floating planes without a color basis or CFA");
    if (!fitsImage(*auxiliary, limits))
        return refusal(ErrorCode::LimitExceeded, parameter, "weight payload exceeds the image limits");
    return Status::success();
}

} // namespace

const char* drizzleKernelName(DrizzleKernel kernel) noexcept
{
    switch (kernel)
    {
    case DrizzleKernel::Square:
        return "Square";
    case DrizzleKernel::Point:
        return "Point";
    case DrizzleKernel::Turbo:
        return "Turbo";
    case DrizzleKernel::Gaussian:
        return "Gaussian";
    case DrizzleKernel::Lanczos2:
        return "Lanczos2";
    case DrizzleKernel::Lanczos3:
        return "Lanczos3";
    }
    return "UnknownDrizzleKernel";
}

Result<DrizzleModel> DrizzleModel::create(const DrizzleRequest& request, const EngineLimits& limits)
{
    const DrizzleParameters& parameters = request.parameters;
    if (request.output.combination != DrizzleCombination::WeightedMean &&
        request.output.combination != DrizzleCombination::PeakNormalizedSum)
        return refusal(ErrorCode::InvalidArgument, "output.combination", "unknown drizzle combination");
    if (!isDrizzleKernel(parameters.kernel))
        return refusal(ErrorCode::InvalidArgument, "parameters.kernel", "unknown drizzle kernel");
    if (parameters.nonFinite != DrizzleNonFinitePolicy::Reject &&
        parameters.nonFinite != DrizzleNonFinitePolicy::Exclude)
        return refusal(ErrorCode::InvalidArgument, "parameters.nonFinite", "unknown non-finite sample policy");
    if (!std::isfinite(parameters.scale) || parameters.scale < 1.0 || parameters.scale > 4.0)
        return refusal(ErrorCode::InvalidArgument, "parameters.scale", "scale must be finite and in [1,4]");
    if (!std::isfinite(parameters.pixelFraction) || parameters.pixelFraction <= 0.0 || parameters.pixelFraction > 1.0)
        return refusal(ErrorCode::InvalidArgument, "parameters.pixelFraction",
                       "drop fraction must be finite and in (0,1]");
    if (!std::isfinite(parameters.weightScale) || parameters.weightScale <= 0.0)
        return refusal(ErrorCode::InvalidArgument, "parameters.weightScale",
                       "global weight must be positive and finite");
    if (request.frameCount == 0u || request.frameCount > kMaxDrizzleFrames)
        return refusal(ErrorCode::InvalidArgument, "frameCount", "frame count must be in the documented range");
    if (request.output.sampleType != SampleType::Float32 && request.output.sampleType != SampleType::Float64)
        return refusal(ErrorCode::InvalidArgument, "output.sampleType", "drizzle output must be Float32 or Float64");
    if (request.input.coordinateOrigin() != CoordinateOrigin::TopLeft || request.input.extent().channelCount() > 4u)
        return refusal(ErrorCode::Unsupported, "input",
                       "drizzle requires top-left rows and at most four source channels");
    if (!fitsImage(request.input, limits))
        return refusal(ErrorCode::LimitExceeded, "input", "source exceeds the image limits");
    const bool cfa = request.input.cfa().pattern() != CfaPattern::None;
    const std::uint32_t channels = cfa ? 3u : request.input.extent().channelCount();
    for (std::size_t channel = 0u; channel < parameters.channelWeights.size(); ++channel)
    {
        const double weight = parameters.channelWeights[channel];
        if (!std::isfinite(weight) || weight <= 0.0 || !std::isfinite(weight * parameters.weightScale) ||
            weight * parameters.weightScale <= 0.0)
            return refusal(ErrorCode::InvalidArgument, "parameters.channelWeights",
                           "channel and global weight product must be positive and finite");
        if (channel >= channels && weight != 1.0)
            return refusal(ErrorCode::IncompatibleArguments, "parameters.channelWeights",
                           "unused channel weights must equal one");
    }
    if (request.validity.needsMask() != request.maskExtent.has_value() ||
        (request.maskExtent.has_value() && *request.maskExtent != request.input.extent()))
        return refusal(ErrorCode::IncompatibleArguments, "maskExtent",
                       "an explicit mask needs exactly the input extent, and no other rule accepts a mask");
    for (const Status& checked : {checkAuxiliary(request.input, request.pixelWeights, "pixelWeights", limits),
                                  checkAuxiliary(request.input, request.flatWeights, "flatWeights", limits)})
    {
        if (checked.failed())
            return checked;
    }
    const double width = request.output.extent.has_value()
                             ? request.output.extent->width()
                             : std::ceil(static_cast<double>(request.input.extent().width()) * parameters.scale);
    const double height = request.output.extent.has_value()
                              ? request.output.extent->height()
                              : std::ceil(static_cast<double>(request.input.extent().height()) * parameters.scale);
    if (width > limits.maxImageWidth || height > limits.maxImageHeight ||
        width > (std::numeric_limits<std::uint32_t>::max)() || height > (std::numeric_limits<std::uint32_t>::max)())
        return refusal(ErrorCode::LimitExceeded, "output.extent", "scaled canvas exceeds the image dimensions");
    if (request.output.extent.has_value() && request.output.extent->channelCount() != channels)
        return refusal(ErrorCode::IncompatibleArguments, "output.extent",
                       "output channels must match source planes or CFA colour routing");
    const auto extent =
        ImageExtent::create(static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height), channels, limits);
    if (!extent.has_value())
        return refusal(ErrorCode::LimitExceeded, "output.extent", "output extent exceeds the image limits");
    ImageDescriptorRequest output = request.input.toRequest();
    output.extent = *extent;
    output.sampleType = request.output.sampleType;
    if (request.output.combination == DrizzleCombination::PeakNormalizedSum &&
        output.numericDomain != NumericDomain::EncodedNonlinear)
        output.numericDomain = NumericDomain::NormalizedLinear;
    if (cfa)
    {
        output.channelSchema = ChannelSchema::Rgb;
        output.cfa = {};
    }
    const auto descriptor = ImageDescriptor::create(output);
    if (!descriptor.has_value())
        return refusal(ErrorCode::IncompatibleArguments, "output", "the requested output does not describe an image");
    if (!fitsImage(*descriptor, limits))
        return refusal(ErrorCode::LimitExceeded, "output", "output payload exceeds the image byte limit");
    return DrizzleModel{request, *descriptor};
}

OperationEffect drizzleOperationEffect() noexcept
{
    return OperationEffect{.samples = SampleEffect::Writes,
                           .description = DescriptionEffect::Restates,
                           .semantics = SemanticEffect::Restates,
                           .solution = SolutionEffect::Invalidates,
                           .codecMetadata = CodecMetadataEffect::Drops,
                           .outputIntent = OutputIntentEffect::Withdraws,
                           .validity = {.policy = ValidityPolicy::Regenerate,
                                        .produced = *ValidityDescriptor::create({.rule = ValidityRule::ExplicitMask})}};
}

} // namespace an
