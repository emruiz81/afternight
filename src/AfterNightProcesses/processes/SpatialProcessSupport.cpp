// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"

#include "processes/SpatialProcessSupport.h"

#include "controls/ImageViewControl.h"

#include <QComboBox>
#include <QVariant>

#include <AfterNightEngine/Algorithms/Pointwise/Arithmetic.h>
#include <AfterNightEngine/Algorithms/Pointwise/PointwiseExecution.h>
#include <AfterNightEngine/Algorithms/Spatial/Gaussian.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/IO/ProcessingStateCards.h>
#include <AfterNightEngine/Core/ImageExtent.h>
#include <AfterNightEngine/Image/ConversionPolicy.h>
#include <AfterNightEngine/Image/ImageConversion.h>
#include <AfterNightEngine/Image/ImageStorage.h>
#include <AfterNightEngine/Image/ImageTransfer.h>
#include <AfterNightEngine/Image/SampleTraits.h>
#include <AfterNightEngine/Image/StorageArrangement.h>
#include <AfterNightEngine/Image/ValidityDescriptor.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>

namespace process_utils
{

namespace
{

/// The mask view of one plane of a mask with several, described at the plane's own extent.
[[nodiscard]] std::optional<an::ValidityMaskView<const std::uint8_t>>
maskPlaneOf(const an::ValidityMaskView<const std::uint8_t>& mask, std::uint32_t plane, const an::ImageExtent& extent)
{
    if (plane >= mask.planeCount())
    {
        return std::nullopt;
    }
    const std::uint64_t offset = static_cast<std::uint64_t>(plane) * mask.planeStrideBytes();
    const std::uint64_t spanned = mask.spannedByteCount();
    if (offset >= spanned)
    {
        return std::nullopt;
    }
    const std::span<const std::uint8_t> bits(mask.data() + offset, static_cast<std::size_t>(spanned - offset));
    return an::ValidityMaskView<const std::uint8_t>::create(bits, extent, mask.rowStrideBytes(),
                                                            mask.planeStrideBytes());
}

/// Whether a view shows a colour image, whose selector offers a channel to run on.
[[nodiscard]] bool showsColour(const ImageViewControl* target)
{
    if (target == nullptr)
    {
        return false;
    }
    const std::optional<an::ImageDescriptor> described = target->imageDescriptor();
    return described.has_value() && (described->channelSchema() == an::ChannelSchema::Rgb ||
                                     described->channelSchema() == an::ChannelSchema::Rgba);
}

} // namespace

UnitScale unitScaleOf(an::SampleType type) noexcept
{
    UnitScale scale;
    (void)an::dispatchSampleType(type, [&scale]<typename SampleT>() {
        if constexpr (!std::is_floating_point_v<SampleT>)
        {
            scale.offset = static_cast<double>((std::numeric_limits<SampleT>::lowest)());
            scale.span = static_cast<double>((std::numeric_limits<SampleT>::max)()) - scale.offset;
        }
    });
    return scale;
}

std::optional<an::ImageLayout> leadingPlanesLayout(const an::ImageLayout& layout, std::uint32_t planes,
                                                   const an::EngineLimits& limits)
{
    if (layout.samplesPerPixel() != 1u || planes == 0u || planes > layout.planeCount())
    {
        return std::nullopt;
    }
    const std::optional<an::ImageExtent> extent =
        an::ImageExtent::create(layout.extent().width(), layout.extent().height(), planes, limits);
    if (!extent.has_value())
    {
        return std::nullopt;
    }
    return an::ImageLayout::createWithStrides(an::ImageLayoutRequest{.extent = *extent,
                                                                     .elementSizeBytes = layout.elementSizeBytes(),
                                                                     .samplesPerPixel = 1u,
                                                                     .planeCount = planes,
                                                                     .rowAlignmentBytes = 1u},
                                              layout.rowStrideBytes(), layout.planeStrideBytes(), limits);
}

an::Status runOverUnitFloat(const an::EngineContext& engine, const an::ExecutionContext& execution, an::SampleType type,
                            void* samples, const an::ImageLayout& layout, const UnitFloatOperation& operation)
{
    if (layout.samplesPerPixel() != 1u)
    {
        return an::Status::failure(an::ErrorCode::IncompatibleArguments,
                                   "a unit-interval working copy is taken of planar samples");
    }
    const std::uint32_t width = layout.extent().width();
    const std::uint32_t height = layout.extent().height();
    const std::uint64_t tightRow = static_cast<std::uint64_t>(width) * sizeof(float);
    if (type == an::SampleType::Float32 && layout.rowStrideBytes() == tightRow &&
        layout.planeStrideBytes() == tightRow * height)
    {
        return operation(static_cast<float*>(samples), layout);
    }
    const std::optional<an::ImageLayout> tight =
        an::ImageLayout::createWithStrides(an::ImageLayoutRequest{.extent = layout.extent(),
                                                                  .elementSizeBytes = sizeof(float),
                                                                  .samplesPerPixel = 1u,
                                                                  .planeCount = layout.planeCount(),
                                                                  .rowAlignmentBytes = 1u},
                                           tightRow, tightRow * height, engine.limits());
    if (!tight.has_value())
    {
        return an::Status::failure(an::ErrorCode::OutOfRange, "the working copy cannot be laid out");
    }
    an::Result<an::ImageStorage<float>> work =
        an::ImageStorage<float>::create(engine, *tight, an::StorageInitialization::Uninitialized);
    if (!work.hasValue())
    {
        return work.status();
    }
    an::ConversionCounters counters;
    an::Status converted = an::convertImageSamples(execution, type, samples, layout, an::SampleType::Float32,
                                                   work->data(), *tight, an::ConversionPolicy{}, counters);
    if (converted.failed())
    {
        return converted;
    }
    if (an::Status ran = operation(work->data(), *tight); ran.failed())
    {
        return ran;
    }
    return an::convertImageSamples(execution, an::SampleType::Float32, work->data(), *tight, type, samples, layout,
                                   an::ConversionPolicy{}, counters);
}

namespace
{

/// The plan a Gaussian blur of a sigma runs, and how many passes of it: the fewest whose share of the sigma - the sigma
/// over the square root of their count, so the variances add up to the sigma's square - one kernel reaches.
struct ComposedGaussian
{
    an::GaussianPlan plan;
    std::uint32_t passes = 1u;
};

/// The widest radius of a plan.
[[nodiscard]] std::uint32_t radiusOf(const an::GaussianPlan& plan) noexcept
{
    return (std::max)(plan.horizontalRadius(), plan.verticalRadius());
}

/// The fewest passes of kernels no wider than a radius that compose a sigma.
[[nodiscard]] an::Result<ComposedGaussian> planPassesWithin(double sigma, an::KernelBorderMode border,
                                                            std::uint32_t widest)
{
    for (std::uint32_t passes = 1u; passes <= 4096u; ++passes)
    {
        const double share = sigma / std::sqrt(static_cast<double>(passes));
        const an::Result<an::GaussianPlan> plan =
            an::planGaussianBlur(an::GaussianRequest{.sigmaX = share, .sigmaY = share, .borders = {.border = border}});
        if (!plan.hasValue() && plan.status().code() != an::ErrorCode::OutOfRange)
        {
            return plan.status();
        }
        if (plan.hasValue() && radiusOf(*plan) <= widest)
        {
            return ComposedGaussian{.plan = *plan, .passes = passes};
        }
    }
    return an::Status::failure(an::ErrorCode::OutOfRange, "no composition of exact passes reaches this sigma");
}

/// The passes the exact Gaussian of a sigma runs over a plane of an extent. One plan where the stencil family's widest
/// kernel reaches the sigma, which either of the engine's paths runs. Beyond it, over a plane mirrored without its edge
/// sample, the fewest passes whose kernel the compiled symmetric path runs over that plane - no wider than the engine's
/// widest exact kernel and under half of each side - which is one pass for every sigma a dialog offers over a frame of
/// ordinary size, where kernels the stencil family reaches would take the sigma's square over a thousand. Otherwise the
/// fewest passes of kernels the stencil family reaches.
[[nodiscard]] an::Result<ComposedGaussian> planExactGaussian(double sigma, an::KernelBorderMode border,
                                                             std::uint32_t width, std::uint32_t height)
{
    const an::Result<ComposedGaussian> narrow = planPassesWithin(sigma, border, an::kMaxStencilRadius);
    if (!narrow.hasValue() || narrow->passes == 1u || border != an::KernelBorderMode::ReflectExcludingEdge)
    {
        return narrow;
    }
    const std::uint32_t half = (std::min)(width, height) / 2u;
    const std::uint32_t symmetric = (std::min)(an::kMaxGaussianRadius, half == 0u ? 0u : half - 1u);
    if (symmetric <= an::kMaxStencilRadius)
    {
        return narrow;
    }
    const an::Result<ComposedGaussian> wide = planPassesWithin(sigma, border, symmetric);
    return wide.hasValue() && wide->passes < narrow->passes ? wide : narrow;
}

/// Runs the passes of an exact composition over tight planar Float32 samples, the last landing in the destination.
[[nodiscard]] an::Status runPasses(const an::EngineContext& engine, const an::ExecutionContext& execution,
                                   const float* source, float* destination, const an::ImageLayout& layout,
                                   const ComposedGaussian& composed)
{
    const an::GaussianPlan& plan = composed.plan;
    const std::uint32_t passes = composed.passes;
    if (passes == 1u)
    {
        return process_utils::statusOf(
            an::blurImageGaussian(execution, an::SampleType::Float32, source, layout, destination, layout, plan));
    }
    an::Result<an::ImageStorage<float>> between =
        an::ImageStorage<float>::create(engine, layout, an::StorageInitialization::Uninitialized);
    if (!between.hasValue())
    {
        return between.status();
    }
    // The passes alternate between the destination and the working plane, so the last one lands in the destination.
    const float* read = source;
    for (std::uint32_t pass = 0u; pass < passes; ++pass)
    {
        execution.reportProgress(pass, passes, "Gaussian blur");
        float* const write = ((passes - 1u - pass) % 2u == 0u) ? destination : between->data();
        const an::Status blurred = process_utils::statusOf(
            an::blurImageGaussian(execution, an::SampleType::Float32, read, layout, write, layout, plan));
        if (blurred.failed())
        {
            return blurred;
        }
        read = write;
    }
    execution.reportProgress(passes, passes, "Gaussian blur");
    return an::Status::success();
}

/// Blurs tight planar Float32 samples with the exact Gaussian at any sigma, in the passes `planExactGaussian` chooses
/// for their plane. A kernel wider than the stencil family's runs only on the engine's compiled symmetric rows, which
/// a processor below AVX2 and an operation held to another instruction set do not have, and needs a plane of its own;
/// where the engine refuses it either, the narrow passes, which the general path runs within its bands on any
/// processor, run instead, from the source, which no pass writes.
[[nodiscard]] an::Status blurAnySigma(const an::EngineContext& engine, const an::ExecutionContext& execution,
                                      const float* source, float* destination, const an::ImageLayout& layout,
                                      double sigma, an::KernelBorderMode border)
{
    const an::Result<ComposedGaussian> composed =
        planExactGaussian(sigma, border, layout.extent().width(), layout.extent().height());
    if (!composed.hasValue())
    {
        return composed.status();
    }
    const an::Status ran = runPasses(engine, execution, source, destination, layout, *composed);
    const bool wideRefused =
        radiusOf(composed->plan) > an::kMaxStencilRadius &&
        (ran.code() == an::ErrorCode::OutOfRange || ran.code() == an::ErrorCode::MemoryBudgetExceeded ||
         ran.code() == an::ErrorCode::AllocationFailed);
    if (!wideRefused)
    {
        return ran;
    }
    const an::Result<ComposedGaussian> narrow = planPassesWithin(sigma, border, an::kMaxStencilRadius);
    if (!narrow.hasValue())
    {
        return narrow.status();
    }
    return runPasses(engine, execution, source, destination, layout, *narrow);
}

/// Whether the stencil family's widest kernel reaches a sigma: the sigma beyond which an interactive preview blurs by
/// the real-time preview's boxes, the exact kernel's cost per pixel growing with the sigma and theirs not.
[[nodiscard]] bool stencilKernelReaches(double sigma, an::KernelBorderMode border)
{
    const an::Result<an::GaussianPlan> plan =
        an::planGaussianBlur(an::GaussianRequest{.sigmaX = sigma, .sigmaY = sigma, .borders = {.border = border}});
    return plan.hasValue() && radiusOf(*plan) <= an::kMaxStencilRadius;
}

/// Blurs tight planar Float32 samples as a kind of blur asks: not at all for a sigma the engine builds no kernel for,
/// by the real-time preview's boxes for an interactive preview's sigma beyond the stencil family's widest kernel, and
/// by the exact Gaussian otherwise.
[[nodiscard]] an::Status blurByKind(const an::EngineContext& engine, const an::ExecutionContext& execution,
                                    const float* source, float* destination, const an::ImageLayout& layout,
                                    double sigma, an::KernelBorderMode border, BlurKind kind)
{
    if (!(sigma > an::kMinimumGaussianSigma))
    {
        an::ConversionCounters counters;
        return an::convertImageSamples(execution, an::SampleType::Float32, source, layout, an::SampleType::Float32,
                                       destination, layout, an::ConversionPolicy{}, counters);
    }
    if (kind == BlurKind::InteractivePreview && !stencilKernelReaches(sigma, border))
    {
        const an::Result<an::GaussianPlan> boxes = an::planRealtimePreviewGaussianBlur(
            an::RealtimePreviewGaussianRequest{.sigmaX = sigma, .sigmaY = sigma, .borders = {.border = border}});
        if (!boxes.hasValue())
        {
            return boxes.status();
        }
        return statusOf(an::blurImageRealtimePreviewGaussian(execution, an::SampleType::Float32, source, layout,
                                                             destination, layout, *boxes));
    }
    return blurAnySigma(engine, execution, source, destination, layout, sigma, border);
}

/// Runs a pointwise program over tight planar Float32 samples in place, with a second source when it takes one.
[[nodiscard]] an::Status runInPlace(const an::ExecutionContext& execution,
                                    const an::Result<an::PointwiseProgram>& program, float* samples,
                                    const float* second, const an::ImageLayout& layout)
{
    if (!program.hasValue())
    {
        return program.status();
    }
    const std::array<an::PointwiseImageView, 2u> sources{an::PointwiseImageView{.samples = samples, .layout = &layout},
                                                         an::PointwiseImageView{.samples = second, .layout = &layout}};
    return process_utils::statusOf(an::executePointwiseProgram(
        execution, *program, std::span<const an::PointwiseImageView>(sources.data(), second != nullptr ? 2u : 1u),
        an::PointwiseMutableImageView{.samples = samples, .layout = &layout}, an::KernelValidityView{}));
}

} // namespace

an::Status blurGaussian(const an::EngineContext& engine, const an::ExecutionContext& execution,
                        const an::ChannelScopeInput& input, const an::ChannelScopeOutput& output, double sigma,
                        an::KernelBorderMode border, BlurKind kind)
{
    const an::SampleType type = input.descriptor->sampleType();
    const an::Result<an::GaussianPlan> plan =
        an::planGaussianBlur(an::GaussianRequest{.sigmaX = sigma, .sigmaY = sigma, .borders = {.border = border}});
    if (plan.hasValue() && radiusOf(*plan) <= an::kMaxStencilRadius)
    {
        // A Float64 plane is blurred where it lies, as is a plane under a validity rule or a mask, which the engine's
        // blur reads, and a Float32 plane whose rows are packed tight, which the engine's compiled kernels read. A
        // Float32 plane of padded rows is blurred on a tight working copy, so the arithmetic is the compiled kernels'
        // whatever the rows' padding - a part of an image cut for a preview is padded where the image is not. An
        // integer plane with every sample valid is blurred on the unit-interval Float32 working copy a composed blur
        // takes, and narrowed back to the nearest code: over integer samples the engine's blur takes its generic path,
        // many times the compiled kernels' cost, and the working copy holds each code exactly and each tap's share of
        // one well within the rounding back.
        const PlaneSelection selection(input);
        const auto tightRows = [](const an::ImageLayout& layout) {
            return layout.rowStrideBytes() == std::uint64_t{layout.extent().width()} * sizeof(float);
        };
        const bool whereItLies =
            type == an::SampleType::Float64 ||
            (type == an::SampleType::Float32 && tightRows(*input.layout) && tightRows(*output.layout));
        const bool everySampleValid = selection.selection().mask == nullptr &&
                                      selection.selection().validity.rule() == an::ValidityRule::AllValid;
        if (whereItLies || !everySampleValid || output.layout->samplesPerPixel() != 1u)
        {
            return statusOf(an::blurImageGaussian(execution, type, input.samples, *input.layout, output.samples,
                                                  *output.layout, *plan, selection.selection()));
        }
    }
    else if (!plan.hasValue() && plan.status().code() != an::ErrorCode::OutOfRange)
    {
        return plan.status();
    }
    if (output.layout->samplesPerPixel() != 1u)
    {
        return an::Status::failure(an::ErrorCode::IncompatibleArguments, "a Gaussian blur's output is planar");
    }
    return runOverUnitFloat(
        engine, execution, type, output.samples, *output.layout,
        [&](float* samples, const an::ImageLayout& layout) -> an::Status {
            an::Result<an::ImageStorage<float>> blur =
                an::ImageStorage<float>::create(engine, layout, an::StorageInitialization::Uninitialized);
            if (!blur.hasValue())
            {
                return blur.status();
            }
            if (an::Status blurred = blurByKind(engine, execution, samples, blur->data(), layout, sigma, border, kind);
                blurred.failed())
            {
                return blurred;
            }
            an::ConversionCounters counters;
            return an::convertImageSamples(execution, an::SampleType::Float32, blur->data(), layout,
                                           an::SampleType::Float32, samples, layout, an::ConversionPolicy{}, counters);
        });
}

an::Status filterDetail(const an::EngineContext& engine, const an::ExecutionContext& execution,
                        const an::ChannelScopeInput& input, const an::ChannelScopeOutput& output,
                        const DetailRequest& request)
{
    const an::SampleType type = input.descriptor->sampleType();
    const UnitScale unit = unitScaleOf(type);
    const PlaneSelection selection(input);
    // A plane every sample of which is valid, blurred under the border the compiled Gaussian mirrors at, is composed
    // over fractions of full scale at every sigma, so the engine's compiled Float32 kernels blur it as they blur the
    // Gaussian blur's: each pair of taps folded once and summed in Float32, where the detail filter's one plan
    // accumulates every tap in Float64 through the general stencil.
    const bool everySampleValid =
        selection.selection().mask == nullptr && selection.selection().validity.rule() == an::ValidityRule::AllValid;
    const bool compiledBlur = everySampleValid && request.border == an::KernelBorderMode::ReflectExcludingEdge;
    if (!compiledBlur && request.amount >= 0.0 && request.sigma > an::kMinimumGaussianSigma)
    {
        const an::Result<an::DetailFilterPlan> plan = an::planDetailFilter(an::DetailFilterRequest{
            .method = request.method,
            .sigmaX = request.sigma,
            .sigmaY = request.sigma,
            .amount = request.amount,
            .offset =
                request.method == an::DetailFilterMethod::HighPass ? unit.offset + request.offset * unit.span : 0.0,
            .lowest = unit.offset,
            .highest = unit.offset + unit.span,
            .borders = {.border = request.border}});
        if (plan.hasValue())
        {
            return statusOf(an::filterImageDetail(execution, type, input.samples, *input.layout, output.samples,
                                                  *output.layout, *plan, selection.selection()));
        }
        if (plan.status().code() != an::ErrorCode::OutOfRange)
        {
            return plan.status();
        }
    }

    // Composed over fractions of full scale: every plane the compiled kernels blur, and what one plan cannot take.
    if (output.layout->samplesPerPixel() != 1u)
    {
        return an::Status::failure(an::ErrorCode::IncompatibleArguments, "a detail filter's output is planar");
    }
    return runOverUnitFloat(
        engine, execution, type, output.samples, *output.layout, [&](float* samples, const an::ImageLayout& layout) {
            an::Result<an::ImageStorage<float>> blur =
                an::ImageStorage<float>::create(engine, layout, an::StorageInitialization::Uninitialized);
            if (!blur.hasValue())
            {
                return blur.status();
            }
            if (an::Status blurred = blurByKind(engine, execution, samples, blur->data(), layout, request.sigma,
                                                request.border, request.blur);
                blurred.failed())
            {
                return blurred;
            }
            const an::PointwisePlanShape shape{.channelCount = layout.planeCount()};
            const an::ClampInterval unitInterval{.low = 0.0, .high = 1.0};
            if (request.method == an::DetailFilterMethod::HighPass)
            {
                if (an::Status ran = runInPlace(
                        execution, an::buildBinaryArithmeticProgram(shape, an::BinaryArithmeticOperation::Subtract),
                        samples, blur->data(), layout);
                    ran.failed())
                {
                    return ran;
                }
                return runInPlace(execution, an::buildScaleBiasProgram(shape, 1.0, request.offset, unitInterval),
                                  samples, nullptr, layout);
            }
            // The blur less the source lands where the blur was, and is taken back out of the source at the amount:
            // `source - amount * (blur - source)`, which is `source + amount * (source - blur)`.
            if (an::Status ran = runInPlace(
                    execution, an::buildBinaryArithmeticProgram(shape, an::BinaryArithmeticOperation::Subtract),
                    blur->data(), samples, layout);
                ran.failed())
            {
                return ran;
            }
            if (an::Status ran = runInPlace(
                    execution,
                    an::buildBinaryArithmeticProgram(shape, an::BinaryArithmeticOperation::Subtract, request.amount),
                    samples, blur->data(), layout);
                ran.failed())
            {
                return ran;
            }
            return runInPlace(execution, an::buildClampProgram(shape, unitInterval), samples, nullptr, layout);
        });
}

std::uint32_t gaussianReach(double sigma, an::KernelBorderMode border, BlurKind kind)
{
    if (!(sigma > an::kMinimumGaussianSigma))
    {
        return 0u;
    }
    // An interactive preview's sigma beyond the stencil family's kernel is blurred by the boxes, which reach the sum
    // of their radii.
    if (kind == BlurKind::InteractivePreview && !stencilKernelReaches(sigma, border))
    {
        const an::Result<an::GaussianPlan> boxes = an::planRealtimePreviewGaussianBlur(
            an::RealtimePreviewGaussianRequest{.sigmaX = sigma, .sigmaY = sigma, .borders = {.border = border}});
        return boxes.hasValue() ? radiusOf(*boxes) : 0u;
    }
    const an::Result<ComposedGaussian> composed = planExactGaussian(
        sigma, border, (std::numeric_limits<std::uint32_t>::max)(), (std::numeric_limits<std::uint32_t>::max)());
    if (!composed.hasValue())
    {
        return 0u;
    }
    const std::uint64_t radius = std::uint64_t{radiusOf(composed->plan)} * std::uint64_t{composed->passes};
    return static_cast<std::uint32_t>((std::min)(radius, std::uint64_t{(std::numeric_limits<std::uint32_t>::max)()}));
}

std::uint32_t detailReach(const DetailRequest& request)
{
    // The engine's detail filter's one plan blurs by the same kernel `gaussianReach` reads for a sigma the stencil
    // family's kernel reaches, and a composition by the passes it adds up.
    return gaussianReach(request.sigma, request.border, request.blur);
}

std::string channelStepSuffix(const ImageViewControl* target, const QComboBox* selector)
{
    if (selector == nullptr || !showsColour(target))
    {
        return std::string();
    }
    return " in channel: " + selector->currentText().toStdString();
}

void selectWholeImageChannel(const ImageViewControl* target, QComboBox* selector)
{
    if (selector == nullptr || !showsColour(target))
    {
        return;
    }
    const int whole = selector->findData(QVariant(static_cast<int>(an::ChannelScope::Whole)));
    if (whole >= 0)
    {
        selector->setCurrentIndex(whole);
    }
}

PlaneSelection::PlaneSelection(const an::ChannelScopeInput& input, NonFiniteSamples nonFinite) noexcept
{
    m_selection.validity = input.validity.descriptor;
    // Only a floating sample can be other than a finite number: an integer plane read under the finite-only rule reads
    // every sample it reads under its own, and the all-valid rule leaves the engine its faster paths - the median's
    // sorting network among them.
    const bool floating = input.descriptor != nullptr && (input.descriptor->sampleType() == an::SampleType::Float32 ||
                                                          input.descriptor->sampleType() == an::SampleType::Float64);
    if (nonFinite == NonFiniteSamples::MeanNothing && floating && m_selection.validity.isAllValid())
    {
        m_selection.validity =
            an::ValidityDescriptor::create(an::ValidityDescriptorRequest{.rule = an::ValidityRule::FiniteOnly})
                .value_or(m_selection.validity);
    }
    m_selection.mask = input.validity.mask;
    // A plane the scope extracted from an image whose mask has one plane per channel reads its own channel's plane;
    // the filter, seeing one channel, would read the first.
    if (input.validity.mask != nullptr && input.layout != nullptr && input.validity.mask->planeCount() > 1u &&
        input.layout->extent().channelCount() == 1u)
    {
        m_plane = maskPlaneOf(*input.validity.mask, input.validity.maskPlane, input.layout->extent());
        m_selection.mask = m_plane.has_value() ? &*m_plane : nullptr;
        if (!m_plane.has_value())
        {
            // A plane that cannot be read is no selection the filter can honour; it refuses a mask rule with no mask.
            m_selection.mask = nullptr;
        }
    }
}

LeadingPlanesSelection::LeadingPlanesSelection(const an::SpatialSelection& whole, std::uint32_t planes,
                                               const an::ImageExtent& extent) noexcept
    : m_selection(whole)
{
    if (whole.mask == nullptr || whole.mask->planeCount() <= planes)
    {
        return;
    }
    const std::span<const std::uint8_t> bits(whole.mask->data(),
                                             static_cast<std::size_t>(whole.mask->spannedByteCount()));
    m_planes = an::ValidityMaskView<const std::uint8_t>::create(bits, extent, whole.mask->rowStrideBytes(),
                                                                whole.mask->planeStrideBytes());
    m_selection.mask = m_planes.has_value() ? &*m_planes : nullptr;
}

an::Status markProcessResultNonlinear(an::ui::ProcessDestination& destination, std::string_view operation)
{
    if (!destination.valid())
    {
        return an::Status::failure(an::ErrorCode::NotInitialized, "the run has no result to record a state on");
    }
    an::consumer::AssetWriteLease& result = destination.asset();
    const std::optional<an::ImageDescriptor> described =
        std::visit([](const auto& typed) { return typed.descriptor(); }, result.image());
    if (!described.has_value())
    {
        return an::Status::failure(an::ErrorCode::NotInitialized, "the run's result holds nothing");
    }
    const an::ImageMetadata& metadata =
        std::visit([](const auto& typed) -> const an::ImageMetadata& { return typed.metadata(); }, result.image());
    const an::ProcessingStateRecord state =
        an::nonlinearState(an::readProcessingStateCards(metadata.codec()), *described, operation);
    an::Result<an::CodecMetadata> codec = an::withProcessingStateCards(metadata.codec(), state);
    if (!codec.hasValue())
    {
        return codec.status();
    }
    an::Result<an::ProvenanceEntry> entry =
        an::ProvenanceEntry::create(std::string(operation), "the samples left nonlinear");
    if (!entry.hasValue())
    {
        return entry.status();
    }
    // The cards are stated where the result is: its samples stay where the process wrote them.
    return result.transition(an::MetadataTransition{.codec = std::move(*codec), .record = std::move(*entry)});
}

} // namespace process_utils
