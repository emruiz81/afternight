// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The multiscale linear transform window's work over layer planes: the transform, the per-layer and global
/// shrinkage, the linear mask, the deringing and its maps, the residual's transfer, the dynamic-range extension and the
/// previews, each an engine operation or a PixelMath expression.

#include "stdafx.hpp"

#include "processes/MultiscaleComposition.h"

#include "processes/MaskProcessUtils.h"
#include "processes/PointwiseProcessUtils.h"

#include <AfterNightEngine/Algorithms/Kernel/HistogramOrderKernel.h>
#include <AfterNightEngine/Algorithms/PixelMath/PixelMathExecution.h>
#include <AfterNightEngine/Algorithms/PixelMath/PixelMathParser.h>
#include <AfterNightEngine/Algorithms/PixelMath/PixelMathProgram.h>
#include <AfterNightEngine/Algorithms/PixelMath/PixelMathResolution.h>
#include <AfterNightEngine/Algorithms/Pointwise/Arithmetic.h>
#include <AfterNightEngine/Algorithms/Pointwise/PointwiseExecution.h>
#include <AfterNightEngine/Algorithms/Spatial/Gaussian.h>
#include <AfterNightEngine/Algorithms/Statistics/BasicStatistics.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/ImageExtent.h>
#include <AfterNightEngine/Image/Image.h>
#include <AfterNightEngine/Image/ImageConversion.h>
#include <AfterNightEngine/Image/ImageView.h>
#include <AfterNightEngine/Image/StorageArrangement.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <initializer_list>
#include <string>
#include <string_view>
#include <utility>

namespace process_utils::multiscale
{

namespace
{

/// The median of the absolute coefficients over this is the deviation of normal noise.
constexpr float kMedianAbsoluteToDeviation = 0.67448975f;

/// How many times the multiresolution support refines a noise estimate.
constexpr int kMultiresolutionIterations = 3;

/// The units of the noise threshold: twenty make one deviation.
constexpr float kNoiseThresholdUnit = 0.05f;

/// What a positive bias is multiplied by before it is added to one.
constexpr float kStarletBiasGain = 6.0f;
constexpr float kLinearFinestBiasGain = 8.0f;
constexpr float kLinearBiasGain = 6.0f;

/// The edge strength the deringing is normalized by is this quantile of the edges, and an edge map whose quantile and
/// maximum are both below the least norm is not normalized - and not used to dering the result.
constexpr float kDeringingEdgeQuantile = 0.90f;
constexpr float kDeringingLeastNorm = 1e-3f;

/// The noise of white noise through each layer's filter is measured on an impulse at the centre of a square of this
/// many pixels a side, or of four times the deepest scale up to the largest.
constexpr std::uint32_t kImpulseLeastSide = 512u;
constexpr std::uint32_t kImpulseLargestSide = 1024u;

/// The logarithmic transfers' scales: log10(1 + 9 v) and ln(1 + (e - 1) v), with e - 1 written to eight digits.
constexpr double kLogarithmicScale = 9.0;
constexpr float kNaturalLogarithmicScale = 1.7182818f;

/// A preview of a layer scales its coefficients by this times its bias.
constexpr float kLayerPreviewVisibility = 5.0f;

[[nodiscard]] an::Status refusal(an::ErrorCode code, std::string message)
{
    return an::Status::failure(code, std::move(message));
}

/// One single-channel Float32 plane a run reads or writes.
struct Plane
{
    float* data = nullptr;
    const an::ImageLayout* layout = nullptr;
};

/// A PixelMath expression over named single-channel Float32 planes of one extent, compiled once and run over any
/// planes of that extent; the first name is the target, whose extent the result has.
class PlaneExpression
{
public:
    [[nodiscard]] static an::Result<PlaneExpression>
    compile(std::string_view source, std::initializer_list<const char*> names,
            std::initializer_list<std::pair<const char*, double>> constants, const an::ImageExtent& extent)
    {
        const an::PixelMathParse parsed = an::parsePixelMath(source);
        if (an::Status status = parsed.status(); status.failed())
        {
            return status;
        }
        an::PixelMathBindings bindings;
        std::vector<std::string> bound;
        for (const char* name : names)
        {
            if (an::Status status = bindings.bindImage(
                    name, an::PixelMathImageDeclaration{.extent = extent,
                                                        .sampleType = an::SampleType::Float32,
                                                        .numericDomain = an::NumericDomain::NormalizedLinear});
                status.failed())
            {
                return status;
            }
            bound.emplace_back(name);
        }
        if (an::Status status = bindings.bindMetaSymbol(an::PixelMathMetaSymbol::Target, bound.front());
            status.failed())
        {
            return status;
        }
        for (const auto& [name, value] : constants)
        {
            if (an::Status status = bindings.declareConstant(name, value); status.failed())
            {
                return status;
            }
        }
        const an::PixelMathResolution resolved = an::resolvePixelMath(parsed.tree(), bindings);
        if (an::Status status = resolved.status(); status.failed())
        {
            return status;
        }
        an::Result<an::PixelMathProgram> program = an::compilePixelMath(resolved);
        if (!program.hasValue())
        {
            return program.status();
        }
        PlaneExpression expression;
        expression.m_program = std::move(*program);
        // The program binds its images in an order of its own; each is one of the names, by its binding slot.
        for (const std::uint32_t slot : resolved.imageSlots())
        {
            const std::string_view name = bindings.imageName(slot);
            const auto found = std::find(bound.begin(), bound.end(), name);
            if (found == bound.end())
            {
                return refusal(an::ErrorCode::Internal, "an expression reads an image it was not given");
            }
            expression.m_order.push_back(static_cast<std::size_t>(found - bound.begin()));
        }
        return expression;
    }

    /// Runs the expression with each name bound to the plane at its position, writing `destination`, which may be the
    /// target itself when the expression reads no neighbour.
    [[nodiscard]] an::Status run(const an::ExecutionContext& execution, std::initializer_list<Plane> planes,
                                 const Plane& destination) const
    {
        const std::vector<Plane> given(planes);
        std::vector<an::PixelMathImageView> images;
        images.reserve(m_order.size());
        for (const std::size_t index : m_order)
        {
            images.push_back(an::PixelMathImageView{
                .sampleType = an::SampleType::Float32, .samples = given[index].data, .layout = given[index].layout});
        }
        an::PixelMathExecutionRequest request;
        request.images = images;
        request.destination = an::PixelMathMutableImageView{
            .sampleType = an::SampleType::Float32, .samples = destination.data, .layout = destination.layout};
        const an::Result<an::PixelMathExecutionResult> ran = an::executePixelMath(execution, m_program, request);
        return ran.hasValue() ? an::Status::success() : ran.status();
    }

private:
    an::PixelMathProgram m_program;
    std::vector<std::size_t> m_order;
};

/// A planar Float32 image owned by a run, of one channel or of an image's channels.
struct Planes
{
    an::Image<float> image;
    float* data = nullptr;
    std::optional<an::ImageLayout> layout;

    [[nodiscard]] Plane plane(std::uint32_t channel, std::optional<an::PlaneView<float>>& holder) const
    {
        holder = an::ImageView<float>::createChecked(data, *layout).plane(channel);
        return holder.has_value() ? Plane{holder->data(), &holder->layout()} : Plane{};
    }
};

[[nodiscard]] an::Result<Planes> createPlanes(const an::EngineContext& engine, std::uint32_t width,
                                              std::uint32_t height, std::uint32_t channels,
                                              an::StorageInitialization initialization)
{
    const std::optional<an::ImageExtent> extent = an::ImageExtent::create(width, height, channels, engine.limits());
    if (!extent.has_value() || (channels != 1u && channels != 3u && channels != 4u))
    {
        return refusal(an::ErrorCode::InvalidArgument, "the working planes cannot be described");
    }
    const std::optional<an::ImageDescriptor> described = an::ImageDescriptor::create(
        an::ImageDescriptorRequest{.extent = *extent,
                                   .sampleType = an::SampleType::Float32,
                                   .channelSchema = channels == 1u   ? an::ChannelSchema::Mono
                                                    : channels == 3u ? an::ChannelSchema::Rgb
                                                                     : an::ChannelSchema::Rgba,
                                   .colorSpace = an::ColorSpace::Unspecified,
                                   .numericDomain = an::NumericDomain::NormalizedLinear,
                                   .cfa = an::CfaDescriptor{},
                                   .coordinateOrigin = an::CoordinateOrigin::TopLeft,
                                   .pixelAspectRatio = 1.0});
    if (!described.has_value())
    {
        return refusal(an::ErrorCode::InvalidArgument, "the working planes cannot be described");
    }
    an::Result<an::Image<float>> created = an::Image<float>::create(engine, *described, an::StorageArrangement::Planar,
                                                                    initialization, an::ImageMetadata{});
    if (!created.hasValue())
    {
        return created.status();
    }
    Planes planes{.image = std::move(*created)};
    const std::optional<an::ImageWriteAccess<float>> access = planes.image.write();
    if (!access.has_value())
    {
        return refusal(an::ErrorCode::NotInitialized, "the working planes hold nothing");
    }
    planes.data = access->view().data();
    planes.layout = access->view().layout();
    return planes;
}

/// The deviation of a plane's coefficients around zero: the median of their magnitudes over 0.67448975, refined, when
/// asked, by the multiresolution support - three times, the median of the magnitudes within `support` deviations of
/// zero. Zero for a plane whose median magnitude is zero.
[[nodiscard]] an::Result<double> deviationAroundZero(const an::ExecutionContext& execution,
                                                     const PlaneExpression& magnitude, const Plane& coefficients,
                                                     const Plane& scratch, bool multiresolution, float support)
{
    if (an::Status ran = magnitude.run(execution, {coefficients}, scratch); ran.failed())
    {
        return ran;
    }
    const an::KernelPlaneSource source{
        .sampleType = an::SampleType::Float32, .samples = scratch.data, .layout = scratch.layout};
    const std::array<double, 1u> half{0.5};
    std::array<double, 1u> median{};
    const an::Result<an::PlaneOrderStatistics> selected = an::selectPlaneOrderStatistics(
        execution, source, half, median, an::PlaneOrderRequest{.interpolation = an::QuantileInterpolation::Linear},
        an::KernelValidityView{});
    if (!selected.hasValue())
    {
        return selected.status();
    }
    const float medianMagnitude = static_cast<float>(median[0]);
    if (!(medianMagnitude > 0.0f))
    {
        return 0.0;
    }
    float sigma = medianMagnitude / kMedianAbsoluteToDeviation;
    if (!multiresolution)
    {
        return static_cast<double>(sigma);
    }
    if (!(support > 0.0f))
    {
        support = 3.0f;
    }
    for (int iteration = 0; iteration < kMultiresolutionIterations; ++iteration)
    {
        const float threshold = support * sigma;
        an::KernelPlaneSelection within;
        within.width = scratch.layout->extent().width();
        within.height = scratch.layout->extent().height();
        within.valueWindow = an::KernelValueWindow{.lowest = 0.0, .highest = static_cast<double>(threshold)};
        const an::Result<an::PlaneOrderStatistics> refined = an::selectPlaneOrderStatisticsOver(
            execution, source, half, median, an::PlaneOrderRequest{.interpolation = an::QuantileInterpolation::Linear},
            an::KernelValidityView{}, within);
        if (!refined.hasValue())
        {
            if (refined.status().code() == an::ErrorCode::EmptySelection)
            {
                break;
            }
            return refined.status();
        }
        const float refinedMedian = static_cast<float>(median[0]);
        if (!(refinedMedian > 0.0f))
        {
            break;
        }
        sigma = refinedMedian / kMedianAbsoluteToDeviation;
    }
    return static_cast<double>(sigma);
}

/// What one unit of white noise becomes in each detail layer: the norm of the layer's response to an impulse.
[[nodiscard]] an::Result<std::vector<double>> noiseShares(const an::ui::ExecutionScope& scope,
                                                          const an::MultiscaleTransformPlan& plan)
{
    const std::uint32_t details = plan.detailLayerCount();
    const std::uint32_t deepest = details > 0u ? plan.layerScale(details - 1u) : 1u;
    const std::uint32_t side =
        std::clamp((std::max)(kImpulseLeastSide, 4u * deepest), kImpulseLeastSide, kImpulseLargestSide);
    an::Result<Planes> impulse = createPlanes(scope.engine(), side, side, 1u, an::StorageInitialization::Zeroed);
    if (!impulse.hasValue())
    {
        return impulse.status();
    }
    impulse
        ->data[static_cast<std::size_t>(side / 2u) * (impulse->layout->rowStrideBytes() / sizeof(float)) + side / 2u] =
        1.0f;
    an::Result<an::MultiscaleDecomposition> decomposed = an::decomposeImageMultiscale(
        scope.engine(), scope.execution(), an::SampleType::Float32, impulse->data, *impulse->layout, plan);
    if (!decomposed.hasValue())
    {
        return decomposed.status();
    }
    std::vector<double> shares;
    for (std::uint32_t layer = 0u; layer < details; ++layer)
    {
        const std::optional<an::PlaneView<float>> coefficients = decomposed->layer(0u, layer);
        if (!coefficients.has_value())
        {
            return refusal(an::ErrorCode::Internal, "a layer of the impulse holds nothing");
        }
        const an::Result<an::BasicStatistics> statistics = an::computeImageBasicStatistics(
            scope.execution(), an::SampleType::Float32, coefficients->data(), coefficients->layout(), 0u);
        if (!statistics.hasValue())
        {
            return statistics.status();
        }
        const double count = static_cast<double>(statistics->selection.valid);
        shares.push_back(std::sqrt(statistics->sumSquaredDeviations + count * statistics->mean * statistics->mean));
    }
    return shares;
}

/// The linear mask of one channel, into `mask`: the channel times the amplification, held to the unit interval,
/// inverted when asked, smoothed by the exact Gaussian and held to it again.
[[nodiscard]] an::Status buildLinearMask(const an::ExecutionContext& execution, const TransformRequest& request,
                                         const Plane& channel, const Plane& mask, const Plane& scratch)
{
    const an::Result<an::PointwiseProgram> amplify =
        an::buildScaleBiasProgram(normalizedPlanShape(an::SampleType::Float32, 1u),
                                  static_cast<double>((std::max)(request.linearMaskAmplification, 1)), 0.0,
                                  an::ClampInterval{.low = 0.0, .high = 1.0});
    if (!amplify.hasValue())
    {
        return amplify.status();
    }
    const std::array<an::PointwiseImageView, 1u> sources{
        an::PointwiseImageView{.samples = channel.data, .layout = channel.layout}};
    const an::Result<an::PointwiseExecutionResult> amplified = an::executePointwiseProgram(
        execution, *amplify, sources, an::PointwiseMutableImageView{.samples = mask.data, .layout = mask.layout},
        an::KernelValidityView{});
    if (!amplified.hasValue())
    {
        return amplified.status();
    }
    if (request.linearMaskInverted)
    {
        if (an::Status inverted =
                scaleAndBiasInPlace(execution, an::SampleType::Float32, mask.data, *mask.layout, -1.0, 1.0);
            inverted.failed())
        {
            return inverted;
        }
    }
    if (!(request.linearMaskSmoothness > 0.0))
    {
        return an::Status::success();
    }
    const an::Result<an::GaussianPlan> plan = an::planGaussianBlur(
        an::GaussianRequest{.sigmaX = request.linearMaskSmoothness, .sigmaY = request.linearMaskSmoothness});
    if (!plan.hasValue())
    {
        return plan.status();
    }
    const an::Result<an::SpatialFilterResult> blurred = an::blurImageGaussian(
        execution, an::SampleType::Float32, mask.data, *mask.layout, scratch.data, *scratch.layout, *plan);
    if (!blurred.hasValue())
    {
        return blurred.status();
    }
    const an::Result<an::PointwiseProgram> clamped = an::buildScaleBiasProgram(
        normalizedPlanShape(an::SampleType::Float32, 1u), 1.0, 0.0, an::ClampInterval{.low = 0.0, .high = 1.0});
    if (!clamped.hasValue())
    {
        return clamped.status();
    }
    const std::array<an::PointwiseImageView, 1u> smoothed{
        an::PointwiseImageView{.samples = scratch.data, .layout = scratch.layout}};
    const an::Result<an::PointwiseExecutionResult> held = an::executePointwiseProgram(
        execution, *clamped, smoothed, an::PointwiseMutableImageView{.samples = mask.data, .layout = mask.layout},
        an::KernelValidityView{});
    return held.hasValue() ? an::Status::success() : held.status();
}

/// The working samples as Float32 planes of their own.
[[nodiscard]] an::Result<Planes> float32CopyOf(const an::ui::ExecutionScope& scope, an::SampleType sampleType,
                                               const void* samples, const an::ImageLayout& layout)
{
    an::Result<Planes> copy = createPlanes(scope.engine(), layout.extent().width(), layout.extent().height(),
                                           layout.extent().channelCount(), an::StorageInitialization::Uninitialized);
    if (!copy.hasValue())
    {
        return copy.status();
    }
    an::ConversionCounters counters;
    const an::Status converted =
        an::convertImageSamples(scope.execution(), sampleType, samples, layout, an::SampleType::Float32, copy->data,
                                *copy->layout, an::ConversionPolicy{}, counters);
    if (converted.failed())
    {
        return converted;
    }
    return copy;
}

/// Writes Float32 result planes back into the working samples, leaving every sample the validity rejects as it was.
[[nodiscard]] an::Status writeBack(const an::ui::ExecutionScope& scope, Planes& result, an::SampleType sampleType,
                                   void* samples, const an::ImageLayout& layout, const an::KernelValidityView& validity)
{
    const void* from = result.data;
    const an::ImageLayout* fromLayout = &*result.layout;
    std::optional<an::Image<double>> wide;
    std::optional<an::ImageLayout> wideLayout;
    if (sampleType == an::SampleType::Float64)
    {
        const std::optional<an::ImageDescriptor> described = result.image.descriptor();
        if (!described.has_value())
        {
            return refusal(an::ErrorCode::NotInitialized, "the result holds nothing");
        }
        const an::Result<an::ImageDescriptor> wideDescribed =
            an::describeSamplesAs(*described, an::SampleType::Float64);
        if (!wideDescribed.hasValue())
        {
            return wideDescribed.status();
        }
        an::Result<an::Image<double>> created =
            an::Image<double>::create(scope.engine(), *wideDescribed, an::StorageArrangement::Planar,
                                      an::StorageInitialization::Uninitialized, an::ImageMetadata{});
        if (!created.hasValue())
        {
            return created.status();
        }
        wide.emplace(std::move(*created));
        const std::optional<an::ImageWriteAccess<double>> access = wide->write();
        if (!access.has_value())
        {
            return refusal(an::ErrorCode::NotInitialized, "the result holds nothing");
        }
        wideLayout = access->view().layout();
        an::ConversionCounters counters;
        const an::Status converted = an::convertImageSamples(
            scope.execution(), an::SampleType::Float32, result.data, *result.layout, an::SampleType::Float64,
            access->view().data(), *wideLayout, an::ConversionPolicy{}, counters);
        if (converted.failed())
        {
            return converted;
        }
        from = access->view().data();
        fromLayout = &*wideLayout;
    }
    const an::Result<an::PointwiseProgram> identity =
        an::buildScaleBiasProgram(normalizedPlanShape(sampleType, layout.extent().channelCount()), 1.0, 0.0);
    if (!identity.hasValue())
    {
        return identity.status();
    }
    const std::array<an::PointwiseImageView, 1u> sources{an::PointwiseImageView{.samples = from, .layout = fromLayout}};
    const an::Result<an::PointwiseExecutionResult> written = an::executePointwiseProgram(
        scope.execution(), *identity, sources, an::PointwiseMutableImageView{.samples = samples, .layout = &layout},
        skippingNonFinite(validity));
    return written.hasValue() ? an::Status::success() : written.status();
}

/// What a layer's settings are, the defaults for a layer the request does not list.
[[nodiscard]] LayerSettings settingsOf(const TransformRequest& request, std::uint32_t layer)
{
    return layer < request.layers.size() ? request.layers[layer] : LayerSettings{};
}

} // namespace

an::Result<an::MultiscaleTransformPlan> planTransform(const TransformRequest& request)
{
    an::MultiscaleTransformRequest transform;
    transform.algorithm = request.algorithm;
    transform.scaling = request.scaling;
    transform.spacing = request.spacing;
    transform.linearScale = (std::max)(request.linearScale, 1u);
    transform.layerCount = request.detailLayers + 1u;
    // The smoothings repeat the frame's edge sample.
    transform.borders = an::StencilBorderPolicy{.border = an::KernelBorderMode::Clamp};
    return an::planMultiscaleTransform(transform);
}

double layerWeight(const TransformRequest& request, const an::MultiscaleTransformPlan& plan, std::uint32_t layer)
{
    const LayerSettings settings = settingsOf(request, layer);
    if (!settings.enabled)
    {
        return 0.0;
    }
    float bias = static_cast<float>(settings.bias);
    if (bias > 0.0f)
    {
        if (request.algorithm == an::MultiscaleAlgorithm::MultiscaleLinear)
        {
            const std::uint32_t details = plan.detailLayerCount();
            const std::uint32_t detail = layer < details ? layer : (details > 0u ? details - 1u : 0u);
            bias *= plan.layerScale(detail) <= 1u ? kLinearFinestBiasGain : kLinearBiasGain;
        }
        else
        {
            bias *= kStarletBiasGain;
        }
    }
    return static_cast<double>(1.0f + bias);
}

an::Status transformInPlace(const an::ui::ExecutionScope& scope, an::SampleType sampleType, void* samples,
                            const an::ImageLayout& layout, const an::KernelValidityView& validity,
                            const TransformRequest& request, const DeringingMaps* maps)
{
    const an::ExecutionContext& execution = scope.execution();
    const an::Result<an::MultiscaleTransformPlan> plan = planTransform(request);
    if (!plan.hasValue())
    {
        return plan.status();
    }
    const std::uint32_t width = layout.extent().width();
    const std::uint32_t height = layout.extent().height();
    const std::uint32_t channels = layout.extent().channelCount();
    const std::uint32_t details = plan->detailLayerCount();
    const std::uint32_t residual = plan->residualLayer();
    const std::optional<an::ImageExtent> planeExtent =
        an::ImageExtent::create(width, height, 1u, scope.engine().limits());
    if (!planeExtent.has_value())
    {
        return refusal(an::ErrorCode::InvalidArgument, "a plane of the image cannot be described");
    }

    // The channels as Float32 planes, decomposed once; a sample the validity rejects is substituted at the door.
    an::Result<Planes> original = float32CopyOf(scope, sampleType, samples, layout);
    if (!original.hasValue())
    {
        return original.status();
    }
    const an::KernelValidityView admitted = skippingNonFinite(validity);
    an::Result<an::MultiscaleDecomposition> decomposition = an::decomposeImageMultiscale(
        scope.engine(), execution, an::SampleType::Float32, original->data, *original->layout, *plan,
        an::SpatialSelection{.validity = admitted.descriptor, .mask = admitted.mask});
    if (!decomposition.hasValue())
    {
        return decomposition.status();
    }

    // What the request switches on.
    const bool kSigma = request.kSigma && request.kSigmaAmount > 0.0 && request.kSigmaK > 0.0;
    bool noiseReduction = false;
    for (std::uint32_t layer = 0u; layer < details; ++layer)
    {
        const LayerSettings settings = settingsOf(request, layer);
        noiseReduction =
            noiseReduction || (settings.noiseReduction && settings.noiseAmount > 0.0 && settings.noiseThreshold > 0.0);
    }
    const bool shrinking = noiseReduction || kSigma;
    const bool linearMask = shrinking && request.linearMask;
    const bool deringing = request.deringing && (request.deringingDark > 0.0 || request.deringingBright > 0.0);
    const bool dynamicRange = request.dynamicRangeLow > 0.0 || request.dynamicRangeHigh > 0.0;

    // The expressions, compiled once for the planes' extent.
    an::Result<PlaneExpression> magnitude = PlaneExpression::compile("abs(L)", {"L"}, {}, *planeExtent);
    if (!magnitude.hasValue())
    {
        return magnitude.status();
    }

    // Scratch planes, one channel each.
    an::Result<Planes> scratch = createPlanes(scope.engine(), width, height, 1u, an::StorageInitialization::Zeroed);
    an::Result<Planes> mask = createPlanes(scope.engine(), width, height, 1u, an::StorageInitialization::Zeroed);
    an::Result<Planes> edges = createPlanes(scope.engine(), width, height, 1u, an::StorageInitialization::Zeroed);
    if (!scratch.hasValue() || !mask.hasValue() || !edges.hasValue())
    {
        return !scratch.hasValue() ? scratch.status() : (!mask.hasValue() ? mask.status() : edges.status());
    }
    std::optional<an::PlaneView<float>> scratchView;
    std::optional<an::PlaneView<float>> maskView;
    std::optional<an::PlaneView<float>> edgesView;
    const Plane scratchPlane = scratch->plane(0u, scratchView);
    const Plane maskPlane = mask->plane(0u, maskView);
    const Plane edgesPlane = edges->plane(0u, edgesView);

    // The noise each layer passes, measured once for every channel.
    std::vector<double> shares;
    if (shrinking)
    {
        an::Result<std::vector<double>> measured = noiseShares(scope, *plan);
        if (!measured.hasValue())
        {
            return measured.status();
        }
        shares = std::move(*measured);
    }

    for (std::uint32_t channel = 0u; channel < channels; ++channel)
    {
        if (an::Status stopped = execution.checkCancellation(); stopped.failed())
        {
            return stopped;
        }
        std::optional<an::PlaneView<float>> originalView;
        const Plane originalPlane = original->plane(channel, originalView);

        if (linearMask)
        {
            if (an::Status built = buildLinearMask(execution, request, originalPlane, maskPlane, scratchPlane);
                built.failed())
            {
                return built;
            }
        }

        // The finest layer's deviation, which every layer's noise is a share of.
        double referenceSigma = 0.0;
        double unitSigma = 0.0;
        if (shrinking && details > 0u && !shares.empty() && shares.front() > 0.0)
        {
            std::optional<an::PlaneView<float>> finest = decomposition->layer(channel, 0u);
            if (!finest.has_value())
            {
                return refusal(an::ErrorCode::Internal, "the finest layer holds nothing");
            }
            const float support = kSigma ? static_cast<float>(request.kSigmaK) : 3.0f;
            const an::Result<double> sigma =
                deviationAroundZero(execution, *magnitude, Plane{finest->data(), &finest->layout()}, scratchPlane,
                                    kSigma && request.kSigmaMultiresolutionSupport, support);
            if (!sigma.hasValue())
            {
                return sigma.status();
            }
            if (*sigma > 0.0)
            {
                referenceSigma = *sigma;
                unitSigma = static_cast<double>(static_cast<float>(*sigma) / static_cast<float>(shares.front()));
            }
        }

        // The deringing's edge map: the larger of the two central differences, normalized by its upper decile - or,
        // when that and the largest edge are both too faint to normalize by, left as the differences are.
        if (deringing)
        {
            an::Result<PlaneExpression> edge = PlaneExpression::compile(
                "max(abs(pixel(S, min(x() + 1, w(S) - 1), y()) - pixel(S, max(x() - 1, 0), y())), "
                "abs(pixel(S, x(), min(y() + 1, h(S) - 1)) - pixel(S, x(), max(y() - 1, 0))))",
                {"S"}, {}, *planeExtent);
            if (!edge.hasValue())
            {
                return edge.status();
            }
            if (an::Status ran = edge->run(execution, {originalPlane}, edgesPlane); ran.failed())
            {
                return ran;
            }
            const std::uint64_t count = static_cast<std::uint64_t>(width) * height;
            const auto rank = static_cast<std::uint64_t>(kDeringingEdgeQuantile * static_cast<float>(count - 1u));
            const std::array<double, 1u> quantile{
                count > 1u ? (std::min)(1.0, (static_cast<double>(rank) + 0.5) / static_cast<double>(count - 1u))
                           : 0.0};
            std::array<double, 1u> value{};
            const an::Result<an::PlaneOrderStatistics> selected = an::selectPlaneOrderStatistics(
                execution,
                an::KernelPlaneSource{
                    .sampleType = an::SampleType::Float32, .samples = edgesPlane.data, .layout = edgesPlane.layout},
                quantile, value, an::PlaneOrderRequest{.interpolation = an::QuantileInterpolation::LowerRank},
                an::KernelValidityView{});
            if (!selected.hasValue())
            {
                return selected.status();
            }
            float norm = static_cast<float>(value[0]);
            if (norm < kDeringingLeastNorm)
            {
                norm = static_cast<float>(selected->maximum);
            }
            if (norm >= kDeringingLeastNorm)
            {
                if (an::Status scaled = scaleAndBiasInPlace(execution, an::SampleType::Float32, edgesPlane.data,
                                                            *edgesPlane.layout, static_cast<double>(1.0f / norm), 0.0,
                                                            an::ClampInterval{.low = 0.0, .high = 1.0});
                    scaled.failed())
                {
                    return scaled;
                }
            }
        }

        for (std::uint32_t layer = 0u; layer < details; ++layer)
        {
            std::optional<an::PlaneView<float>> coefficients = decomposition->layer(channel, layer);
            if (!coefficients.has_value())
            {
                return refusal(an::ErrorCode::Internal, "a detail layer holds nothing");
            }
            const Plane layerPlane{coefficients->data(), &coefficients->layout()};
            const LayerSettings settings = settingsOf(request, layer);
            const bool layerNoise =
                settings.noiseReduction && settings.noiseAmount > 0.0 && settings.noiseThreshold > 0.0;
            if (layerNoise || kSigma)
            {
                double sigma = 0.0;
                if (unitSigma > 0.0 && layer < shares.size() && shares[layer] > 0.0)
                {
                    sigma = static_cast<double>(static_cast<float>(unitSigma) * static_cast<float>(shares[layer]));
                }
                else
                {
                    const an::Result<double> measured =
                        deviationAroundZero(execution, *magnitude, layerPlane, scratchPlane, false, 3.0f);
                    if (!measured.hasValue())
                    {
                        return measured.status();
                    }
                    sigma = *measured;
                }

                // One shrinkage, through the engine for the whole layer, or weighed by the linear mask pixel by pixel.
                auto shrink = [&](double threshold, double amount, bool soft) -> an::Status {
                    amount = std::clamp(amount, 0.0, 1.0);
                    if (!(threshold > 0.0) || !(amount > 0.0))
                    {
                        return an::Status::success();
                    }
                    if (!linearMask)
                    {
                        const an::Result<an::MultiscaleShrinkResult> shrunk = an::shrinkMultiscaleLayer(
                            execution, *decomposition, channel, layer,
                            an::MultiscaleShrinkRequest{.shrinkage = soft ? an::MultiscaleShrinkage::Soft
                                                                          : an::MultiscaleShrinkage::Hard,
                                                        .threshold = threshold,
                                                        .amount = amount});
                        return shrunk.hasValue() ? an::Status::success() : shrunk.status();
                    }
                    an::Result<PlaneExpression> masked =
                        soft ? PlaneExpression::compile(
                                   "L + a * clamp(M, 0, 1) * (iif(L > t, L - t, iif(L < -t, L + t, 0)) - L)",
                                   {"L", "M"}, {{"a", amount}, {"t", threshold}}, *planeExtent)
                             : PlaneExpression::compile("L + a * clamp(M, 0, 1) * (iif(abs(L) < t, 0, L) - L)",
                                                        {"L", "M"}, {{"a", amount}, {"t", threshold}}, *planeExtent);
                    if (!masked.hasValue())
                    {
                        return masked.status();
                    }
                    return masked->run(execution, {layerPlane, maskPlane}, layerPlane);
                };

                if (layerNoise)
                {
                    const float base = referenceSigma > 0.0
                                           ? (std::max)(static_cast<float>(referenceSigma), static_cast<float>(sigma))
                                           : static_cast<float>(sigma);
                    const float threshold = static_cast<float>(settings.noiseThreshold) * kNoiseThresholdUnit * base;
                    for (int iteration = 0; iteration < (std::max)(settings.noiseIterations, 1); ++iteration)
                    {
                        if (an::Status shrunk = shrink(static_cast<double>(threshold), settings.noiseAmount, true);
                            shrunk.failed())
                        {
                            return shrunk;
                        }
                    }
                }
                if (kSigma)
                {
                    const float threshold = static_cast<float>(request.kSigmaK) * static_cast<float>(sigma);
                    if (an::Status shrunk =
                            shrink(static_cast<double>(threshold), request.kSigmaAmount, request.kSigmaSoft);
                        shrunk.failed())
                    {
                        return shrunk;
                    }
                }
            }

            // Every detail layer is attenuated at the edges, the attenuation added to the maps first.
            if (deringing)
            {
                if (maps != nullptr && maps->layout != nullptr)
                {
                    std::optional<an::PlaneView<float>> darkView =
                        an::ImageView<float>::createChecked(maps->dark, *maps->layout).plane(channel);
                    std::optional<an::PlaneView<float>> brightView =
                        an::ImageView<float>::createChecked(maps->bright, *maps->layout).plane(channel);
                    if (!darkView.has_value() || !brightView.has_value())
                    {
                        return refusal(an::ErrorCode::IncompatibleArguments, "the deringing maps are not the image's");
                    }
                    const Plane dark{darkView->data(), &darkView->layout()};
                    const Plane bright{brightView->data(), &brightView->layout()};
                    an::Result<PlaneExpression> darkMap = PlaneExpression::compile(
                        "Dm + D * E * max(0, -L)", {"Dm", "E", "L"}, {{"D", request.deringingDark}}, *planeExtent);
                    an::Result<PlaneExpression> brightMap = PlaneExpression::compile(
                        "Bm + B * E * max(0, L)", {"Bm", "E", "L"}, {{"B", request.deringingBright}}, *planeExtent);
                    if (!darkMap.hasValue() || !brightMap.hasValue())
                    {
                        return !darkMap.hasValue() ? darkMap.status() : brightMap.status();
                    }
                    if (an::Status ran = darkMap->run(execution, {dark, edgesPlane, layerPlane}, dark); ran.failed())
                    {
                        return ran;
                    }
                    if (an::Status ran = brightMap->run(execution, {bright, edgesPlane, layerPlane}, bright);
                        ran.failed())
                    {
                        return ran;
                    }
                }
                an::Result<PlaneExpression> attenuate = PlaneExpression::compile(
                    "L + D * E * max(0, -L) - B * E * max(0, L)", {"L", "E"},
                    {{"D", request.deringingDark}, {"B", request.deringingBright}}, *planeExtent);
                if (!attenuate.hasValue())
                {
                    return attenuate.status();
                }
                if (an::Status ran = attenuate->run(execution, {layerPlane, edgesPlane}, layerPlane); ran.failed())
                {
                    return ran;
                }
            }
        }

        // The residual's large scales, compressed after its weight.
        const LayerSettings residualSettings = settingsOf(request, residual);
        if (request.residualTransfer != ResidualTransfer::None && residualSettings.enabled)
        {
            std::optional<an::PlaneView<float>> residualView = decomposition->layer(channel, residual);
            if (!residualView.has_value())
            {
                return refusal(an::ErrorCode::Internal, "the residual holds nothing");
            }
            const Plane residualPlane{residualView->data(), &residualView->layout()};
            const double gain = layerWeight(request, *plan, residual);
            const float breakpoint = std::clamp(
                request.residualBreakpoint > 0.0 ? static_cast<float>(request.residualBreakpoint) : 1.0f, 0.1f, 1.0f);
            const char* expression = request.residualTransfer == ResidualTransfer::Hyperbolic
                                         ? "clamp(R * c, 0, 1) / (clamp(R * c, 0, 1) + b)"
                                     : request.residualTransfer == ResidualTransfer::Log10
                                         ? "log10(1 + k * clamp(R * c, 0, 1)) / log10(1 + k)"
                                         : "ln(1 + n * clamp(R * c, 0, 1))";
            an::Result<PlaneExpression> transfer =
                PlaneExpression::compile(expression, {"R"},
                                         {{"c", gain},
                                          {"b", static_cast<double>(breakpoint)},
                                          {"k", kLogarithmicScale},
                                          {"n", static_cast<double>(kNaturalLogarithmicScale)}},
                                         *planeExtent);
            if (!transfer.hasValue())
            {
                return transfer.status();
            }
            if (an::Status ran = transfer->run(execution, {residualPlane}, residualPlane); ran.failed())
            {
                return ran;
            }
        }
    }

    // The layers added back with their weights, and held to the unit interval when nothing follows that maps it.
    std::vector<an::MultiscaleLayerWeight> weights;
    for (std::uint32_t layer = 0u; layer <= residual; ++layer)
    {
        const bool transferred = layer == residual && request.residualTransfer != ResidualTransfer::None;
        const double weight =
            transferred ? (settingsOf(request, layer).enabled ? 1.0 : 0.0) : layerWeight(request, *plan, layer);
        weights.push_back(an::MultiscaleLayerWeight{.enabled = weight != 0.0, .gain = weight});
    }
    an::Result<Planes> result =
        createPlanes(scope.engine(), width, height, channels, an::StorageInitialization::Zeroed);
    if (!result.hasValue())
    {
        return result.status();
    }
    an::MultiscaleReconstructionRequest reconstruction;
    reconstruction.layers = weights;
    if (!deringing && !dynamicRange)
    {
        reconstruction.clamp = an::KernelValueWindow{.lowest = 0.0, .highest = 1.0};
    }
    const an::Result<an::SpatialFilterResult> rebuilt = an::reconstructImageMultiscale(
        execution, *decomposition, an::SampleType::Float32, result->data, *result->layout, reconstruction);
    if (!rebuilt.hasValue())
    {
        return rebuilt.status();
    }

    // The final deringing against each channel's own edges, then the dynamic-range extension or the unit interval.
    if (deringing || dynamicRange)
    {
        an::Result<PlaneExpression> dering =
            PlaneExpression::compile("P + D * max(0, O - P) * E - B * max(0, P - O) * E", {"P", "O", "E"},
                                     {{"D", request.deringingDark}, {"B", request.deringingBright}}, *planeExtent);
        an::Result<PlaneExpression> edge =
            PlaneExpression::compile("max(abs(pixel(S, min(x() + 1, w(S) - 1), y()) - pixel(S, max(x() - 1, 0), y())), "
                                     "abs(pixel(S, x(), min(y() + 1, h(S) - 1)) - pixel(S, x(), max(y() - 1, 0))))",
                                     {"S"}, {}, *planeExtent);
        if (!dering.hasValue() || !edge.hasValue())
        {
            return !dering.hasValue() ? dering.status() : edge.status();
        }
        for (std::uint32_t channel = 0u; channel < channels; ++channel)
        {
            std::optional<an::PlaneView<float>> originalView;
            std::optional<an::PlaneView<float>> resultView;
            const Plane originalPlane = original->plane(channel, originalView);
            const Plane resultPlane = result->plane(channel, resultView);
            if (deringing)
            {
                if (an::Status ran = edge->run(execution, {originalPlane}, edgesPlane); ran.failed())
                {
                    return ran;
                }
                const std::uint64_t count = static_cast<std::uint64_t>(width) * height;
                const auto rank = static_cast<std::uint64_t>(kDeringingEdgeQuantile * static_cast<float>(count - 1u));
                const std::array<double, 1u> quantile{
                    count > 1u ? (std::min)(1.0, (static_cast<double>(rank) + 0.5) / static_cast<double>(count - 1u))
                               : 0.0};
                std::array<double, 1u> value{};
                const an::Result<an::PlaneOrderStatistics> selected = an::selectPlaneOrderStatistics(
                    execution,
                    an::KernelPlaneSource{
                        .sampleType = an::SampleType::Float32, .samples = edgesPlane.data, .layout = edgesPlane.layout},
                    quantile, value, an::PlaneOrderRequest{.interpolation = an::QuantileInterpolation::LowerRank},
                    an::KernelValidityView{});
                if (!selected.hasValue())
                {
                    return selected.status();
                }
                float norm = static_cast<float>(value[0]);
                if (norm < kDeringingLeastNorm)
                {
                    norm = static_cast<float>(selected->maximum);
                }
                // An image with no edge to speak of is left as the layers made it.
                if (norm >= kDeringingLeastNorm)
                {
                    if (an::Status scaled = scaleAndBiasInPlace(execution, an::SampleType::Float32, edgesPlane.data,
                                                                *edgesPlane.layout, static_cast<double>(1.0f / norm),
                                                                0.0, an::ClampInterval{.low = 0.0, .high = 1.0});
                        scaled.failed())
                    {
                        return scaled;
                    }
                    if (an::Status ran = dering->run(execution, {resultPlane, originalPlane, edgesPlane}, resultPlane);
                        ran.failed())
                    {
                        return ran;
                    }
                }
            }
        }
        const double low = (std::max)(request.dynamicRangeLow, 0.0);
        const double high = (std::max)(request.dynamicRangeHigh, 0.0);
        const double span = static_cast<double>(static_cast<float>(1.0 + high) + static_cast<float>(low));
        const bool extend = dynamicRange && span > 0.0;
        if (an::Status mapped = scaleAndBiasInPlace(execution, an::SampleType::Float32, result->data, *result->layout,
                                                    extend ? 1.0 / span : 1.0, extend ? low / span : 0.0,
                                                    an::ClampInterval{.low = 0.0, .high = 1.0});
            mapped.failed())
        {
            return mapped;
        }
    }
    return writeBack(scope, *result, sampleType, samples, layout, validity);
}

an::Status previewLayerInPlace(const an::ui::ExecutionScope& scope, an::SampleType sampleType, void* samples,
                               const an::ImageLayout& layout, const TransformRequest& request, std::uint32_t layer,
                               LayerPreview mode, double bias)
{
    const an::ExecutionContext& execution = scope.execution();
    const an::Result<an::MultiscaleTransformPlan> plan = planTransform(request);
    if (!plan.hasValue())
    {
        return plan.status();
    }
    if (layer > plan->residualLayer())
    {
        return refusal(an::ErrorCode::OutOfRange, "the transform has no such layer");
    }
    const std::uint32_t width = layout.extent().width();
    const std::uint32_t height = layout.extent().height();
    const std::uint32_t channels = layout.extent().channelCount();
    const std::optional<an::ImageExtent> planeExtent =
        an::ImageExtent::create(width, height, 1u, scope.engine().limits());
    if (!planeExtent.has_value())
    {
        return refusal(an::ErrorCode::InvalidArgument, "a plane of the image cannot be described");
    }
    an::Result<Planes> original = float32CopyOf(scope, sampleType, samples, layout);
    if (!original.hasValue())
    {
        return original.status();
    }
    an::Result<an::MultiscaleDecomposition> decomposition = an::decomposeImageMultiscale(
        scope.engine(), execution, an::SampleType::Float32, original->data, *original->layout, *plan);
    if (!decomposition.hasValue())
    {
        return decomposition.status();
    }
    const char* expression = mode == LayerPreview::AllChanges         ? "clamp(0.5 + L * k, 0, 1)"
                             : mode == LayerPreview::IncreasingPixels ? "clamp(max(0, L) * k, 0, 1)"
                                                                      : "clamp(max(0, -L) * k, 0, 1)";
    an::Result<PlaneExpression> shown = PlaneExpression::compile(
        expression, {"L"}, {{"k", static_cast<double>(static_cast<float>(bias) * kLayerPreviewVisibility)}},
        *planeExtent);
    if (!shown.hasValue())
    {
        return shown.status();
    }
    an::Result<Planes> result =
        createPlanes(scope.engine(), width, height, channels, an::StorageInitialization::Zeroed);
    if (!result.hasValue())
    {
        return result.status();
    }
    for (std::uint32_t channel = 0u; channel < channels; ++channel)
    {
        std::optional<an::PlaneView<float>> coefficients = decomposition->layer(channel, layer);
        std::optional<an::PlaneView<float>> resultView;
        const Plane resultPlane = result->plane(channel, resultView);
        if (!coefficients.has_value())
        {
            return refusal(an::ErrorCode::Internal, "the layer holds nothing");
        }
        if (an::Status ran = shown->run(execution, {Plane{coefficients->data(), &coefficients->layout()}}, resultPlane);
            ran.failed())
        {
            return ran;
        }
    }
    return writeBack(scope, *result, sampleType, samples, layout, an::KernelValidityView{});
}

an::Status previewLinearMaskInPlace(const an::ui::ExecutionScope& scope, an::SampleType sampleType, void* samples,
                                    const an::ImageLayout& layout, const TransformRequest& request)
{
    const std::uint32_t width = layout.extent().width();
    const std::uint32_t height = layout.extent().height();
    const std::uint32_t channels = layout.extent().channelCount();
    an::Result<Planes> original = float32CopyOf(scope, sampleType, samples, layout);
    an::Result<Planes> result =
        createPlanes(scope.engine(), width, height, channels, an::StorageInitialization::Zeroed);
    an::Result<Planes> scratch = createPlanes(scope.engine(), width, height, 1u, an::StorageInitialization::Zeroed);
    if (!original.hasValue() || !result.hasValue() || !scratch.hasValue())
    {
        return !original.hasValue() ? original.status() : (!result.hasValue() ? result.status() : scratch.status());
    }
    std::optional<an::PlaneView<float>> scratchView;
    const Plane scratchPlane = scratch->plane(0u, scratchView);
    for (std::uint32_t channel = 0u; channel < channels; ++channel)
    {
        std::optional<an::PlaneView<float>> originalView;
        std::optional<an::PlaneView<float>> resultView;
        if (an::Status built = buildLinearMask(scope.execution(), request, original->plane(channel, originalView),
                                               result->plane(channel, resultView), scratchPlane);
            built.failed())
        {
            return built;
        }
    }
    return writeBack(scope, *result, sampleType, samples, layout, an::KernelValidityView{});
}

} // namespace process_utils::multiscale
