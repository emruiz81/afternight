// AfterNightEngine - the layered narrowband colour mapper: one fused composition pass and the
// frame passes that need a rank.
//
// A map runs in up to four steps, all over the same shared admission rule. The modifier medians are
// exact ranks of each median-subtracting layer's modifier, taken first because the composition reads
// them. The composition pass is one scheduled row walk that stretches, modifies, colourizes and
// screen-blends every included layer, resolving each layer's clamped parameters and HSV weights once
// rather than per pixel. Background neutralization then measures each output channel's median,
// normalized MAD and sigma-window median, and applies the gain and offset it reports. The optional
// output stretch last moves the luminance median with one midtones transfer.
//
// The rank steps share one context-accounted block of doubles sized to the frame: two planes when
// the background is measured, because the MAD needs a copy beside the values it ranks, one plane for
// a modifier median or the output stretch alone, and none for a purely pointwise map. Every formula
// and constant is NBColorMapper's. When a layer aliases a post-processed destination, one byte per
// pixel preserves admission before composition changes the source values.
//
// Derived from NBColorMapper 2.2, a PixInsight script by Mike Cranfield of Cosmic Photons -
// Copyright (C) 2023, 2024 Mike Cranfield, GNU General Public License version 3 - and distributed
// under the same licence. The steps above keep the script's layer model, formulas and constants;
// `Algorithms/Color/NarrowbandColorMap.h` states what is this engine's, and
// `docs/dev/NBCOLORMAPPER_PROVENANCE.md` records the snapshot that was reviewed.

// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <AfterNightEngine/Algorithms/Color/NarrowbandColorMap.h>

#include <AfterNightEngine/Core/EngineLimits.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Image/ImageSemantics.h>
#include <AfterNightEngine/Image/OperationTransition.h>
#include <AfterNightEngine/Runtime/BackendDispatch.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <AfterNightEngine/Runtime/TaskScheduler.h>

#include "algorithms/color/NarrowbandInternal.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string>

namespace an
{
namespace
{

using narrowband_detail::failure;

constexpr const char* kChannelCountOperation = "an::narrowbandColorMapChannelCount";
constexpr const char* kDescriptorOperation = "an::narrowbandColorMapOutputDescriptor";
constexpr const char* kMapOperation = "an::mapNarrowbandColor";
constexpr const char* kTransitionOperation = "an::narrowbandColorMapTransition";
constexpr double kMinimumMidtones = 1.0e-6;
constexpr double kMaximumMidtones = 1.0 - kMinimumMidtones;
constexpr double kNormalizedMadScale = 1.4826;

/// What validating a layer list established, shared by every entry point.
struct LayerAnalysis
{
    /// Whether each layer is named as another layer's modifier.
    std::array<bool, kMaximumNarrowbandColorLayers> referenced{};

    /// Layers that take part in the screen blend.
    std::uint32_t included = 0u;

    /// Distinct layers named as modifiers.
    std::uint32_t modifierCount = 0u;

    /// One for a neutral map, three when any included layer produces colour.
    std::uint32_t outputChannels = 1u;

    /// The layers' shared width.
    std::uint32_t width = 0u;

    /// The layers' shared height.
    std::uint32_t height = 0u;
};

[[nodiscard]] Result<LayerAnalysis> analyzeLayers(std::span<const NarrowbandColorLayer> layers, const char* operation)
{
    if (layers.empty() || layers.size() > kMaximumNarrowbandColorLayers)
    {
        return failure(operation, ErrorCode::InvalidArgument,
                       "a narrowband map needs between one and sixty-four layers");
    }
    LayerAnalysis result{};
    for (std::size_t index = 0u; index < layers.size(); ++index)
    {
        const NarrowbandColorLayer& layer = layers[index];
        if (layer.samples == nullptr || layer.layout == nullptr)
        {
            return failure(operation, ErrorCode::InvalidArgument, "every narrowband layer has samples and a layout");
        }
        const std::uint32_t channels = layer.layout->extent().channelCount();
        if (channels != 1u && channels != 3u)
        {
            return failure(operation, ErrorCode::IncompatibleArguments, "a narrowband layer is monochrome or RGB");
        }
        if (index == 0u)
        {
            result.width = layer.layout->extent().width();
            result.height = layer.layout->extent().height();
        }
        else if (layer.layout->extent().width() != result.width || layer.layout->extent().height() != result.height)
        {
            return failure(operation, ErrorCode::IncompatibleArguments,
                           "all narrowband layers have the same pixel extent");
        }
        if (!std::isfinite(layer.hueDegrees) || !std::isfinite(layer.saturation) || !std::isfinite(layer.midtones) ||
            !std::isfinite(layer.blackPoint) || !std::isfinite(layer.modificationFactor) ||
            !isNarrowbandModificationMode(layer.modification))
        {
            return failure(operation, ErrorCode::InvalidArgument, "a narrowband layer parameter is invalid");
        }
        if (layer.modification == NarrowbandModificationMode::None)
        {
            if (layer.modificationSource.has_value())
            {
                return failure(operation, ErrorCode::InvalidArgument,
                               "an unmodified layer names no modification source");
            }
        }
        else
        {
            if (!layer.modificationSource.has_value() || *layer.modificationSource >= layers.size() ||
                *layer.modificationSource == index)
            {
                return failure(operation, ErrorCode::InvalidArgument,
                               "a modified layer names another layer in this request");
            }
            result.referenced[*layer.modificationSource] = true;
        }
        if (layer.included)
        {
            ++result.included;
        }
    }
    if (result.included == 0u)
    {
        return failure(operation, ErrorCode::InvalidArgument, "a narrowband map needs an included layer");
    }
    for (std::size_t index = 0u; index < layers.size(); ++index)
    {
        if (result.referenced[index])
        {
            ++result.modifierCount;
        }
        const NarrowbandColorLayer& layer = layers[index];
        if (!layer.included)
        {
            continue;
        }
        const double saturation = std::clamp(layer.saturation, 0.0, 1.0);
        if (layer.layout->extent().channelCount() == 3u ||
            (!result.referenced[index] && saturation > std::numeric_limits<double>::epsilon()))
        {
            result.outputChannels = 3u;
        }
    }
    return result;
}

[[nodiscard]] Status validateRequest(const NarrowbandColorMapRequest& request, std::uint32_t width,
                                     std::uint32_t height)
{
    if (!std::isfinite(request.targetBackground) || request.targetBackground <= 0.0 || request.targetBackground >= 1.0)
    {
        return failure(kMapOperation, ErrorCode::OutOfRange,
                       "the output-stretch target is finite and strictly between zero and one");
    }
    if (!request.background.enabled)
    {
        return Status::success();
    }
    if (!std::isfinite(request.background.lowerSigmas) || !std::isfinite(request.background.upperSigmas) ||
        request.background.upperSigmas <= request.background.lowerSigmas)
    {
        return failure(kMapOperation, ErrorCode::InvalidArgument, "background sigma limits are finite and ordered");
    }
    if (request.background.region.has_value())
    {
        const NarrowbandColorRegion& region = *request.background.region;
        if (region.width == 0u || region.height == 0u || region.x >= width || region.y >= height ||
            static_cast<std::uint64_t>(region.x) + region.width > width ||
            static_cast<std::uint64_t>(region.y) + region.height > height)
        {
            return failure(kMapOperation, ErrorCode::OutOfRange,
                           "the background region is nonempty and inside the output");
        }
    }
    return Status::success();
}

[[nodiscard]] Status validateInvocation(const ExecutionContext& execution, SampleType sampleType,
                                        std::span<const NarrowbandColorLayer> layers, void* destination,
                                        const ImageLayout& destinationLayout, const NarrowbandColorMapRequest& request,
                                        const KernelValidityView& validity, const LayerAnalysis& analysis)
{
    if (!execution.valid())
    {
        return failure(kMapOperation, ErrorCode::NotInitialized, "a narrowband map has no execution context");
    }
    if (const Status cancelled = execution.checkCancellation(); cancelled.failed())
    {
        return cancelled;
    }
    const Result<BackendSelection> selected = selectBackend(execution);
    if (!selected.hasValue())
    {
        return selected.status();
    }
    if (!narrowband_detail::supportedWorkingType(sampleType))
    {
        return failure(kMapOperation, ErrorCode::Unsupported,
                       "narrowband colour mapping runs on a Float32 or Float64 working image");
    }
    if (destination == nullptr || !narrowband_detail::alignedFor(destination, sampleType) ||
        destinationLayout.elementSizeBytes() != sampleTypeSizeBytes(sampleType))
    {
        return failure(kMapOperation, ErrorCode::InvalidArgument,
                       "the narrowband destination pointer or layout disagrees with its sample type");
    }
    if (destinationLayout.extent().width() != analysis.width ||
        destinationLayout.extent().height() != analysis.height ||
        destinationLayout.extent().channelCount() != analysis.outputChannels)
    {
        return failure(kMapOperation, ErrorCode::IncompatibleArguments,
                       "the destination does not match the map's planned extent and channel count");
    }
    for (const NarrowbandColorLayer& layer : layers)
    {
        if (layer.layout->elementSizeBytes() != sampleTypeSizeBytes(sampleType) ||
            !narrowband_detail::alignedFor(layer.samples, sampleType))
        {
            return failure(kMapOperation, ErrorCode::InvalidArgument,
                           "a narrowband layer pointer or layout disagrees with its sample type");
        }
        if (narrowband_detail::overlaps(layer.samples, *layer.layout, destination, destinationLayout) &&
            !narrowband_detail::exactlyAliases(layer.samples, *layer.layout, destination, destinationLayout))
        {
            return failure(kMapOperation, ErrorCode::IncompatibleArguments,
                           "a narrowband layer partially overlaps its destination");
        }
    }
    if (const Status valid = narrowband_detail::validateValidity(validity, *layers.front().layout, destination,
                                                                 destinationLayout, kMapOperation);
        valid.failed())
    {
        return valid;
    }
    return validateRequest(request, analysis.width, analysis.height);
}

template <typename SampleT>
[[nodiscard]] bool layerAdmitted(const NarrowbandColorLayer& layer, const KernelValidityView& validity, std::uint32_t x,
                                 std::uint32_t y) noexcept
{
    const std::uint32_t channels = layer.layout->extent().channelCount();
    for (std::uint32_t channel = 0u; channel < channels; ++channel)
    {
        const SampleT sample = narrowband_detail::sampleAt<SampleT>(layer.samples, *layer.layout, x, y, channel);
        if (!narrowband_detail::admits(validity, sample, x, y))
        {
            return false;
        }
    }
    return true;
}

// The one admission rule every pass applies: every channel of every included layer, and of the
// modifier of each modified monochrome layer, must pass the validity view. An all-valid rule admits
// every pixel without reading a sample.
template <typename SampleT>
[[nodiscard]] bool mapPixelAdmitted(std::span<const NarrowbandColorLayer> layers, const KernelValidityView& validity,
                                    const LayerAnalysis& analysis, std::uint32_t x, std::uint32_t y,
                                    const std::uint8_t* admission = nullptr) noexcept
{
    if (admission != nullptr)
    {
        return admission[static_cast<std::uint64_t>(y) * analysis.width + x] != 0u;
    }
    if (validity.descriptor.isAllValid())
    {
        return true;
    }
    for (std::size_t index = 0u; index < layers.size(); ++index)
    {
        const NarrowbandColorLayer& layer = layers[index];
        if (!layer.included)
        {
            continue;
        }
        if (!layerAdmitted<SampleT>(layer, validity, x, y))
        {
            return false;
        }
        if (layer.layout->extent().channelCount() == 1u && !analysis.referenced[index] &&
            layer.modification != NarrowbandModificationMode::None &&
            !layerAdmitted<SampleT>(layers[*layer.modificationSource], validity, x, y))
        {
            return false;
        }
    }
    return true;
}

template <typename SampleT>
[[nodiscard]] double baseValue(const NarrowbandColorLayer& layer, std::uint32_t x, std::uint32_t y,
                               std::uint32_t channel, std::uint64_t& nonFinite) noexcept
{
    const SampleT sample = narrowband_detail::sampleAt<SampleT>(layer.samples, *layer.layout, x, y, channel);
    double value = static_cast<double>(sample);
    if (!std::isfinite(value))
    {
        ++nonFinite;
        value = 0.0;
    }
    const double midtones = std::clamp(layer.midtones, kMinimumMidtones, kMaximumMidtones);
    const double blackPoint = std::clamp(layer.blackPoint, 0.0, 1.0);
    return std::clamp(narrowband_detail::midtones(value, midtones, blackPoint), 0.0, 1.0);
}

template <typename SampleT>
[[nodiscard]] double modifierValue(const NarrowbandColorLayer& modifier, std::uint32_t x, std::uint32_t y,
                                   std::uint64_t& nonFinite) noexcept
{
    if (modifier.layout->extent().channelCount() == 1u)
    {
        return baseValue<SampleT>(modifier, x, y, 0u, nonFinite);
    }
    const double red = baseValue<SampleT>(modifier, x, y, 0u, nonFinite);
    const double green = baseValue<SampleT>(modifier, x, y, 1u, nonFinite);
    const double blue = baseValue<SampleT>(modifier, x, y, 2u, nonFinite);
    return 0.2989 * red + 0.5870 * green + 0.1140 * blue;
}

/// `baseValue` for a sample already read, with the layer's clamped midtones and black point.
///
/// @param sample the stored sample.
/// @param midtones the layer's midtones, already clamped.
/// @param blackPoint the layer's black point, already clamped.
/// @param nonFinite incremented when the sample is not finite.
/// @return the stretched value.
template <typename SampleT>
[[nodiscard]] double baseSample(SampleT sample, double midtones, double blackPoint, std::uint64_t& nonFinite) noexcept
{
    double value = static_cast<double>(sample);
    if (!std::isfinite(value))
    {
        ++nonFinite;
        value = 0.0;
    }
    return std::clamp(narrowband_detail::midtones(value, midtones, blackPoint), 0.0, 1.0);
}

/// What the composition resolves once per layer rather than per pixel.
struct LayerConstants
{
    /// The layer's midtones, clamped as `baseValue` clamps them.
    double midtones = 0.5;

    /// Its black point, clamped as `baseValue` clamps it.
    double blackPoint = 0.0;

    /// The hue-and-saturation half of its colour.
    narrowband_detail::HsvWeights colour{};
};

template <typename SampleT>
[[nodiscard]] Status
computeModifierMedians(const ExecutionContext& execution, std::span<const NarrowbandColorLayer> layers,
                       const KernelValidityView& validity, const LayerAnalysis& analysis, double* workspace,
                       std::array<double, kMaximumNarrowbandColorLayers>& medians)
{
    const std::uint32_t width = layers.front().layout->extent().width();
    const std::uint32_t height = layers.front().layout->extent().height();
    for (std::size_t index = 0u; index < layers.size(); ++index)
    {
        const NarrowbandColorLayer& target = layers[index];
        if (!target.included || analysis.referenced[index] || target.layout->extent().channelCount() != 1u ||
            target.modification == NarrowbandModificationMode::None || !target.subtractModificationMedian)
        {
            continue;
        }
        const NarrowbandColorLayer& modifier = layers[*target.modificationSource];
        std::uint64_t count = 0u;
        for (std::uint32_t y = 0u; y < height; ++y)
        {
            if ((y & 31u) == 0u)
            {
                const Status cancelled = execution.checkCancellation();
                if (cancelled.failed())
                {
                    return cancelled;
                }
            }
            for (std::uint32_t x = 0u; x < width; ++x)
            {
                if (!layerAdmitted<SampleT>(modifier, validity, x, y))
                {
                    continue;
                }
                std::uint64_t ignored = 0u;
                double value = modifierValue<SampleT>(modifier, x, y, ignored);
                if (target.invertModification)
                {
                    value = 1.0 - value;
                }
                workspace[count++] = value;
            }
        }
        if (count == 0u)
        {
            return failure(kMapOperation, ErrorCode::EmptySelection, "a modifier median has no valid source sample");
        }
        medians[index] = narrowband_detail::exactMedian(workspace, count);
    }
    return Status::success();
}

template <typename SampleT>
[[nodiscard]] Result<NarrowbandColorMapResult>
composeLayers(const ExecutionContext& execution, std::span<const NarrowbandColorLayer> layers, void* destination,
              const ImageLayout& destinationLayout, const KernelValidityView& validity, const LayerAnalysis& analysis,
              const std::array<double, kMaximumNarrowbandColorLayers>& modifierMedians, std::uint8_t* admission)
{
    const std::uint32_t width = analysis.width;
    const std::uint32_t height = analysis.height;
    const TaskCost cost{.bytesPerItem = static_cast<std::uint64_t>(width) * sizeof(SampleT) * (layers.size() + 1u),
                        .operationsPerItem = static_cast<std::uint64_t>(width) * layers.size() * 24u};
    std::array<LayerConstants, kMaximumNarrowbandColorLayers> constants{};
    for (std::size_t index = 0u; index < layers.size(); ++index)
    {
        constants[index].midtones = std::clamp(layers[index].midtones, kMinimumMidtones, kMaximumMidtones);
        constants[index].blackPoint = std::clamp(layers[index].blackPoint, 0.0, 1.0);
        constants[index].colour =
            narrowband_detail::hsvWeights(layers[index].hueDegrees, std::clamp(layers[index].saturation, 0.0, 1.0));
    }
    std::atomic<std::uint64_t> validPixels{0u};
    std::atomic<std::uint64_t> nonFinite{0u};
    const auto body = [&](TaskRange range) -> Status {
        std::uint64_t localValid = 0u;
        std::uint64_t localNonFinite = 0u;
        // Every layer's channel rows, and the destination's, resolved once per row rather than once per
        // sample; they address the samples the checked accessors address.
        std::array<std::array<narrowband_detail::ChannelRowView<const SampleT>, 3u>, kMaximumNarrowbandColorLayers>
            layerRows{};
        std::array<narrowband_detail::ChannelRowView<SampleT>, 3u> outputRows{};
        // The stretched value of one channel of one layer at one pixel, as `baseValue` computes it.
        const auto stretched = [&](std::size_t index, std::uint32_t channel, std::uint32_t x) {
            return baseSample<SampleT>(layerRows[index][channel].at(x), constants[index].midtones,
                                       constants[index].blackPoint, localNonFinite);
        };
        for (std::uint64_t row = range.begin; row < range.end; ++row)
        {
            const std::uint32_t y = static_cast<std::uint32_t>(row);
            for (std::size_t index = 0u; index < layers.size(); ++index)
            {
                const std::uint32_t layerChannels = layers[index].layout->extent().channelCount();
                for (std::uint32_t channel = 0u; channel < layerChannels && channel < 3u; ++channel)
                {
                    layerRows[index][channel] = narrowband_detail::channelRowView<SampleT>(
                        layers[index].samples, *layers[index].layout, channel, y);
                }
            }
            for (std::uint32_t channel = 0u; channel < analysis.outputChannels; ++channel)
            {
                outputRows[channel] =
                    narrowband_detail::channelRowView<SampleT>(destination, destinationLayout, channel, y);
            }
            for (std::uint32_t x = 0u; x < width; ++x)
            {
                const bool admitted = mapPixelAdmitted<SampleT>(layers, validity, analysis, x, y);
                if (admission != nullptr)
                {
                    admission[static_cast<std::uint64_t>(y) * width + x] = static_cast<std::uint8_t>(admitted);
                }
                if (!admitted)
                {
                    continue;
                }
                std::array<double, 3u> product{1.0, 1.0, 1.0};
                for (std::size_t index = 0u; index < layers.size(); ++index)
                {
                    const NarrowbandColorLayer& layer = layers[index];
                    if (!layer.included)
                    {
                        continue;
                    }
                    std::array<double, 3u> color{};
                    if (layer.layout->extent().channelCount() == 3u)
                    {
                        for (std::uint32_t channel = 0u; channel < 3u; ++channel)
                        {
                            color[channel] = stretched(index, channel, x);
                        }
                    }
                    else
                    {
                        double value = stretched(index, 0u, x);
                        if (!analysis.referenced[index] && layer.modification != NarrowbandModificationMode::None)
                        {
                            // `modifierValue`, over the rows resolved for this row.
                            const std::size_t source = *layer.modificationSource;
                            double modulation = 0.0;
                            if (layers[source].layout->extent().channelCount() == 1u)
                            {
                                modulation = stretched(source, 0u, x);
                            }
                            else
                            {
                                const double red = stretched(source, 0u, x);
                                const double green = stretched(source, 1u, x);
                                const double blue = stretched(source, 2u, x);
                                modulation = 0.2989 * red + 0.5870 * green + 0.1140 * blue;
                            }
                            if (layer.invertModification)
                            {
                                modulation = 1.0 - modulation;
                            }
                            if (layer.subtractModificationMedian)
                            {
                                modulation = (std::max)(0.0, modulation - modifierMedians[index]);
                            }
                            modulation *= layer.modificationFactor;
                            switch (layer.modification)
                            {
                            case NarrowbandModificationMode::None:
                                break;
                            case NarrowbandModificationMode::Add:
                                value += modulation;
                                break;
                            case NarrowbandModificationMode::Multiply:
                                value *= modulation;
                                break;
                            case NarrowbandModificationMode::Boost:
                                value *= 1.0 + modulation;
                                break;
                            }
                        }
                        value = std::isfinite(value) ? std::clamp(value, 0.0, 1.0) : 0.0;
                        color = analysis.referenced[index] ? std::array<double, 3u>{value, value, value}
                                                           : narrowband_detail::hsv(constants[index].colour, value);
                    }
                    for (std::uint32_t channel = 0u; channel < analysis.outputChannels; ++channel)
                    {
                        const double component = analysis.outputChannels == 1u ? color[0] : color[channel];
                        product[channel] *= 1.0 - std::clamp(component, 0.0, 1.0);
                    }
                }
                for (std::uint32_t channel = 0u; channel < analysis.outputChannels; ++channel)
                {
                    outputRows[channel].at(x) = static_cast<SampleT>(std::clamp(1.0 - product[channel], 0.0, 1.0));
                }
                ++localValid;
            }
        }
        validPixels.fetch_add(localValid, std::memory_order_relaxed);
        nonFinite.fetch_add(localNonFinite, std::memory_order_relaxed);
        return Status::success();
    };
    const Status written = parallelFor(execution, height, cost, body);
    if (written.failed())
    {
        return written;
    }
    NarrowbandColorMapResult result{};
    result.run.selection.total = static_cast<std::uint64_t>(width) * height;
    result.run.selection.valid = validPixels.load(std::memory_order_relaxed);
    result.run.outputsWritten = result.run.selection.valid * analysis.outputChannels;
    result.includedLayers = analysis.included;
    result.modifierLayers = analysis.modifierCount;
    result.nonFiniteSamples = nonFinite.load(std::memory_order_relaxed);
    result.plannedWorkers = plannedWorkerCount(execution, height, cost);
    return result;
}

// The mapper's background neutralization, per RGB channel of the composed output: the
// median and normalized MAD (MAD times 1.4826) of the admitted finite samples in the region, a sigma
// window around the median clamped to `[0, 1]`, and the median of the samples inside it as the
// level to subtract. The window is clamped, unlike the calibration-sky estimator's, because this is
// the compatibility mapper over normalized samples. When a corrected channel would leave the unit
// interval, all usable channels share one rescale over the joint range, so the correction stays one
// gain and one offset per channel.
template <typename SampleT>
[[nodiscard]] Status
neutralizeBackground(const ExecutionContext& execution, void* destination, const ImageLayout& layout,
                     const NarrowbandBackgroundRequest& request, std::span<const NarrowbandColorLayer> layers,
                     const KernelValidityView& validity, const LayerAnalysis& analysis, double* workspace,
                     NarrowbandColorMapResult& result, const std::uint8_t* admission)
{
    if (!request.enabled || layout.extent().channelCount() != 3u)
    {
        return Status::success();
    }
    const std::uint32_t width = layout.extent().width();
    const std::uint32_t height = layout.extent().height();
    const NarrowbandColorRegion region =
        request.region.value_or(NarrowbandColorRegion{.x = 0u, .y = 0u, .width = width, .height = height});
    std::array<bool, 3u> usable{};
    std::array<double, 3u> minimum{};
    std::array<double, 3u> maximum{};
    for (std::uint32_t channel = 0u; channel < 3u; ++channel)
    {
        std::uint64_t count = 0u;
        for (std::uint32_t y = region.y; y < region.y + region.height; ++y)
        {
            if ((y & 31u) == 0u)
            {
                const Status cancelled = execution.checkCancellation();
                if (cancelled.failed())
                {
                    return cancelled;
                }
            }
            for (std::uint32_t x = region.x; x < region.x + region.width; ++x)
            {
                const SampleT value = narrowband_detail::sampleAt<SampleT>(destination, layout, x, y, channel);
                if (mapPixelAdmitted<SampleT>(layers, validity, analysis, x, y, admission) && std::isfinite(value))
                {
                    workspace[count++] = static_cast<double>(value);
                }
            }
        }
        if (count == 0u)
        {
            ++result.backgroundChannelsSkipped;
            continue;
        }
        std::copy_n(workspace, count, workspace + count);
        const double median = narrowband_detail::exactMedian(workspace + count, count);
        for (std::uint64_t index = 0u; index < count; ++index)
        {
            workspace[count + index] = std::abs(workspace[index] - median);
        }
        const double deviation = narrowband_detail::exactMedian(workspace + count, count) * kNormalizedMadScale;
        const double lower = std::clamp(median + request.lowerSigmas * deviation, 0.0, 1.0);
        const double upper = std::clamp(median + request.upperSigmas * deviation, 0.0, 1.0);
        std::uint64_t selected = 0u;
        for (std::uint64_t index = 0u; index < count; ++index)
        {
            if (workspace[index] >= lower && workspace[index] <= upper)
            {
                workspace[count + selected++] = workspace[index];
            }
        }
        if (selected == 0u)
        {
            ++result.backgroundChannelsSkipped;
            continue;
        }
        result.backgroundLevels[channel] = narrowband_detail::exactMedian(workspace + count, selected);
        minimum[channel] = (std::numeric_limits<double>::max)();
        maximum[channel] = (std::numeric_limits<double>::lowest)();
        for (std::uint32_t y = 0u; y < height; ++y)
        {
            if ((y & 31u) == 0u)
            {
                const Status cancelled = execution.checkCancellation();
                if (cancelled.failed())
                {
                    return cancelled;
                }
            }
            for (std::uint32_t x = 0u; x < width; ++x)
            {
                if (!mapPixelAdmitted<SampleT>(layers, validity, analysis, x, y, admission))
                {
                    continue;
                }
                const double value =
                    static_cast<double>(narrowband_detail::sampleAt<SampleT>(destination, layout, x, y, channel)) -
                    result.backgroundLevels[channel];
                minimum[channel] = (std::min)(minimum[channel], value);
                maximum[channel] = (std::max)(maximum[channel], value);
            }
        }
        if (!request.rescaleHighlights)
        {
            maximum[channel] = 1.0 - result.backgroundLevels[channel];
        }
        usable[channel] = true;
    }
    double globalMinimum = (std::numeric_limits<double>::max)();
    double globalMaximum = (std::numeric_limits<double>::lowest)();
    bool any = false;
    for (std::uint32_t channel = 0u; channel < 3u; ++channel)
    {
        if (usable[channel])
        {
            globalMinimum = (std::min)(globalMinimum, minimum[channel]);
            globalMaximum = (std::max)(globalMaximum, maximum[channel]);
            any = true;
        }
    }
    if (!any)
    {
        return Status::success();
    }
    const double range = globalMaximum - globalMinimum;
    result.backgroundRescaled =
        (globalMinimum < 0.0 || globalMaximum > 1.0) && range > std::numeric_limits<double>::epsilon();
    for (std::uint32_t channel = 0u; channel < 3u; ++channel)
    {
        if (!usable[channel])
        {
            continue;
        }
        if (result.backgroundRescaled && range > std::numeric_limits<double>::epsilon())
        {
            result.backgroundGains[channel] = 1.0 / range;
            result.backgroundOffsets[channel] = -(result.backgroundLevels[channel] + globalMinimum) / range;
        }
        else
        {
            result.backgroundOffsets[channel] = -result.backgroundLevels[channel];
        }
    }
    const TaskCost cost{.bytesPerItem = static_cast<std::uint64_t>(width) * sizeof(SampleT) * 2u,
                        .operationsPerItem = static_cast<std::uint64_t>(width) * 12u};
    const auto body = [&](TaskRange rows) -> Status {
        for (std::uint64_t row = rows.begin; row < rows.end; ++row)
        {
            const std::uint32_t y = static_cast<std::uint32_t>(row);
            for (std::uint32_t x = 0u; x < width; ++x)
            {
                if (!mapPixelAdmitted<SampleT>(layers, validity, analysis, x, y, admission))
                {
                    continue;
                }
                for (std::uint32_t channel = 0u; channel < 3u; ++channel)
                {
                    if (!usable[channel])
                    {
                        continue;
                    }
                    const double value =
                        static_cast<double>(narrowband_detail::sampleAt<SampleT>(destination, layout, x, y, channel)) *
                            result.backgroundGains[channel] +
                        result.backgroundOffsets[channel];
                    narrowband_detail::writeAt<SampleT>(destination, layout, x, y, channel,
                                                        std::clamp(value, 0.0, 1.0));
                }
            }
        }
        return Status::success();
    };
    return parallelFor(execution, height, cost, body);
}

template <typename SampleT>
[[nodiscard]] Status stretchOutput(const ExecutionContext& execution, void* destination, const ImageLayout& layout,
                                   double targetBackground, std::span<const NarrowbandColorLayer> layers,
                                   const KernelValidityView& validity, const LayerAnalysis& analysis, double* workspace,
                                   NarrowbandColorMapResult& result, const std::uint8_t* admission)
{
    const std::uint32_t width = layout.extent().width();
    const std::uint32_t height = layout.extent().height();
    const std::uint32_t channels = layout.extent().channelCount();
    std::uint64_t count = 0u;
    for (std::uint32_t y = 0u; y < height; ++y)
    {
        if ((y & 31u) == 0u)
        {
            const Status cancelled = execution.checkCancellation();
            if (cancelled.failed())
            {
                return cancelled;
            }
        }
        for (std::uint32_t x = 0u; x < width; ++x)
        {
            if (!mapPixelAdmitted<SampleT>(layers, validity, analysis, x, y, admission))
            {
                continue;
            }
            double luminance = 0.0;
            for (std::uint32_t channel = 0u; channel < channels; ++channel)
            {
                luminance += narrowband_detail::sampleAt<SampleT>(destination, layout, x, y, channel);
            }
            workspace[count++] = luminance / channels;
        }
    }
    if (count == 0u)
    {
        return failure(kMapOperation, ErrorCode::EmptySelection, "the output stretch has no valid pixel");
    }
    const double median = narrowband_detail::exactMedian(workspace, count);
    if (median <= std::numeric_limits<double>::epsilon() || median >= 1.0 - std::numeric_limits<double>::epsilon())
    {
        return Status::success();
    }
    const double balance = narrowband_detail::midtones(median, targetBackground, 0.0);
    const TaskCost cost{.bytesPerItem = static_cast<std::uint64_t>(width) * channels * sizeof(SampleT) * 2u,
                        .operationsPerItem = static_cast<std::uint64_t>(width) * channels * 8u};
    const auto body = [&](TaskRange rows) -> Status {
        for (std::uint64_t row = rows.begin; row < rows.end; ++row)
        {
            const std::uint32_t y = static_cast<std::uint32_t>(row);
            for (std::uint32_t x = 0u; x < width; ++x)
            {
                if (!mapPixelAdmitted<SampleT>(layers, validity, analysis, x, y, admission))
                {
                    continue;
                }
                for (std::uint32_t channel = 0u; channel < channels; ++channel)
                {
                    const double value = narrowband_detail::sampleAt<SampleT>(destination, layout, x, y, channel);
                    narrowband_detail::writeAt<SampleT>(destination, layout, x, y, channel,
                                                        narrowband_detail::midtones(value, balance, 0.0));
                }
            }
        }
        return Status::success();
    };
    const Status status = parallelFor(execution, height, cost, body);
    if (status.ok())
    {
        result.outputStretchApplied = true;
    }
    return status;
}

template <typename SampleT>
[[nodiscard]] Result<NarrowbandColorMapResult>
mapTyped(const ExecutionContext& execution, std::span<const NarrowbandColorLayer> layers, void* destination,
         const ImageLayout& destinationLayout, const NarrowbandColorMapRequest& request,
         const KernelValidityView& validity, const LayerAnalysis& analysis)
{
    const std::uint64_t pixels = static_cast<std::uint64_t>(analysis.width) * analysis.height;
    bool needsModifierMedian = false;
    for (std::size_t index = 0u; index < layers.size(); ++index)
    {
        const NarrowbandColorLayer& layer = layers[index];
        needsModifierMedian =
            needsModifierMedian ||
            (layer.included && !analysis.referenced[index] && layer.layout->extent().channelCount() == 1u &&
             layer.modification != NarrowbandModificationMode::None && layer.subtractModificationMedian);
    }
    const std::uint64_t workspacePlanes = request.background.enabled                            ? 2u
                                          : (needsModifierMedian || request.applyOutputStretch) ? 1u
                                                                                                : 0u;
    const bool aliases = std::any_of(layers.begin(), layers.end(),
                                     [&](const NarrowbandColorLayer& layer) { return layer.samples == destination; });
    const bool keepAdmission =
        aliases && !validity.descriptor.isAllValid() && (request.background.enabled || request.applyOutputStretch);
    const std::uint64_t bytesPerPixel = workspacePlanes * sizeof(double) + (keepAdmission ? 1u : 0u);
    if (bytesPerPixel != 0u && pixels > (std::numeric_limits<std::uint64_t>::max)() / bytesPerPixel)
    {
        return failure(kMapOperation, ErrorCode::LimitExceeded, "the map's workspace size overflows");
    }
    const std::uint64_t bytes = pixels * bytesPerPixel;
    narrowband_detail::ContextBlock block;
    if (bytes != 0u)
    {
        if (const Status allocated = block.take(execution, bytes, kMapOperation); allocated.failed())
        {
            return allocated;
        }
    }
    double* const workspace = block.doubles();
    std::uint8_t* const admission =
        keepAdmission ? reinterpret_cast<std::uint8_t*>(workspace + pixels * workspacePlanes) : nullptr;
    std::array<double, kMaximumNarrowbandColorLayers> modifierMedians{};
    if (const Status medians =
            computeModifierMedians<SampleT>(execution, layers, validity, analysis, workspace, modifierMedians);
        medians.failed())
    {
        return medians;
    }
    Result<NarrowbandColorMapResult> composed = composeLayers<SampleT>(
        execution, layers, destination, destinationLayout, validity, analysis, modifierMedians, admission);
    if (!composed.hasValue())
    {
        return composed.status();
    }
    composed->workingBytes = bytes;
    if (const Status background =
            neutralizeBackground<SampleT>(execution, destination, destinationLayout, request.background, layers,
                                          validity, analysis, workspace, *composed, admission);
        background.failed())
    {
        return background;
    }
    if (request.applyOutputStretch)
    {
        if (const Status stretch =
                stretchOutput<SampleT>(execution, destination, destinationLayout, request.targetBackground, layers,
                                       validity, analysis, workspace, *composed, admission);
            stretch.failed())
        {
            return stretch;
        }
    }
    return *composed;
}

} // namespace

const char* narrowbandModificationModeName(NarrowbandModificationMode mode) noexcept
{
    switch (mode)
    {
    case NarrowbandModificationMode::None:
        return "None";
    case NarrowbandModificationMode::Add:
        return "Add";
    case NarrowbandModificationMode::Multiply:
        return "Multiply";
    case NarrowbandModificationMode::Boost:
        return "Boost";
    }
    return "UnknownNarrowbandModificationMode";
}

Result<std::uint32_t> narrowbandColorMapChannelCount(std::span<const NarrowbandColorLayer> layers)
{
    const Result<LayerAnalysis> analysis = analyzeLayers(layers, kChannelCountOperation);
    return analysis.hasValue() ? Result<std::uint32_t>{analysis->outputChannels}
                               : Result<std::uint32_t>{analysis.status()};
}

Result<ImageDescriptor> narrowbandColorMapOutputDescriptor(const ImageDescriptor& reference,
                                                           std::span<const NarrowbandColorLayer> layers)
{
    const Result<LayerAnalysis> analysis = analyzeLayers(layers, kDescriptorOperation);
    if (!analysis.hasValue())
    {
        return analysis.status();
    }
    if (!narrowband_detail::supportedWorkingType(reference.sampleType()) || reference.cfa().isMosaiced() ||
        (reference.channelSchema() != ChannelSchema::Mono && reference.channelSchema() != ChannelSchema::Rgb) ||
        (reference.numericDomain() != NumericDomain::NormalizedLinear &&
         reference.numericDomain() != NumericDomain::EncodedNonlinear) ||
        reference.extent().width() != analysis->width || reference.extent().height() != analysis->height)
    {
        return failure(kDescriptorOperation, ErrorCode::Unsupported,
                       "a color-map reference is a resolved normalized floating mono or RGB image");
    }
    const std::optional<ImageExtent> extent = ImageExtent::create(
        reference.extent().width(), reference.extent().height(), analysis->outputChannels, defaultEngineLimits());
    if (!extent.has_value())
    {
        return failure(kDescriptorOperation, ErrorCode::LimitExceeded,
                       "the color-map output extent exceeds engine limits");
    }
    ImageDescriptorRequest request = reference.toRequest();
    request.extent = *extent;
    request.channelSchema = analysis->outputChannels == 1u ? ChannelSchema::Mono : ChannelSchema::Rgb;
    request.colorSpace = analysis->outputChannels == 1u ? ColorSpace::Unspecified : ColorSpace::NarrowbandMapping;
    request.cfa = {};
    const std::optional<ImageDescriptor> output = ImageDescriptor::create(request);
    return output.has_value() ? Result<ImageDescriptor>{*output}
                              : Result<ImageDescriptor>{failure(kDescriptorOperation, ErrorCode::IncompatibleArguments,
                                                                "the color-map output description is inconsistent")};
}

Result<NarrowbandColorMapResult> mapNarrowbandColor(const ExecutionContext& execution, SampleType sampleType,
                                                    std::span<const NarrowbandColorLayer> layers, void* destination,
                                                    const ImageLayout& destinationLayout,
                                                    const NarrowbandColorMapRequest& request,
                                                    const KernelValidityView& validity)
{
    const Result<LayerAnalysis> analysis = analyzeLayers(layers, kMapOperation);
    if (!analysis.hasValue())
    {
        return analysis.status();
    }
    if (const Status valid = validateInvocation(execution, sampleType, layers, destination, destinationLayout, request,
                                                validity, *analysis);
        valid.failed())
    {
        return valid;
    }
    return sampleType == SampleType::Float32
               ? mapTyped<float>(execution, layers, destination, destinationLayout, request, validity, *analysis)
               : mapTyped<double>(execution, layers, destination, destinationLayout, request, validity, *analysis);
}

Result<MetadataTransition> narrowbandColorMapTransition(const ImageSemantics& source, std::uint32_t layerCount,
                                                        std::uint32_t outputChannels)
{
    if (layerCount == 0u || layerCount > kMaximumNarrowbandColorLayers ||
        (outputChannels != 1u && outputChannels != 3u))
    {
        return failure(kTransitionOperation, ErrorCode::InvalidArgument,
                       "a color-map transition has a valid layer and channel count");
    }
    return operationTransition(kNarrowbandColorMapEffect,
                               OperationOutcome{.operation = kMapOperation,
                                                .detail = std::string{"layers="} + std::to_string(layerCount) +
                                                          ";channels=" + std::to_string(outputChannels),
                                                .semantics = source});
}

} // namespace an
