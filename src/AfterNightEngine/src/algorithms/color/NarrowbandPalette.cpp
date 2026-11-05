// AfterNightEngine - the twelve emission-line palettes over prepared monochrome channels.
//
// Validation resolves the substitution once: an absent H-alpha or SII channel is replaced by the
// other before any pixel is read, so the per-pixel code always reads three channels and one flag
// says whether Foraxx takes its two-channel form. The composition is then a scheduled reduction
// whose body writes each admitted pixel's mapped, clamped values and folds them into the frame
// maximum, so the pixels are mapped once; a second pass divides by that maximum only when
// normalization changes anything. The maximum is taken over the values as stored, after narrowing,
// so normalizing a Float32 result divides by a number the destination holds.
//
// Derived from the narrowband palette process of Seti Astro Suite Pro by Franklin Marek, GPL-3.0,
// and distributed under the same licence: the substitution, the twelve equations, the clamp and
// the normalization are SASPro's;
// `Algorithms/Color/NarrowbandPalette.h` states what is this engine's, and
// `docs/dev/NARROWBAND_PALETTE_COMPOSER_PROVENANCE.md` records the snapshot that was reviewed.

// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <AfterNightEngine/Algorithms/Color/NarrowbandPalette.h>

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
#include <span>
#include <string>

namespace an
{
namespace
{

using narrowband_detail::failure;

constexpr const char* kComposeOperation = "an::composeNarrowbandPalette";
constexpr const char* kGridOperation = "an::composeNarrowbandPaletteGrid";
constexpr const char* kDescriptorOperation = "an::narrowbandPaletteOutputDescriptor";
constexpr const char* kTransitionOperation = "an::narrowbandPaletteTransition";

constexpr std::array<NarrowbandPaletteDescriptor, 12u> kPaletteCatalog{{
    {NarrowbandPalette::Sho, "SHO", "Sulfur, hydrogen-alpha, and oxygen mapped to RGB."},
    {NarrowbandPalette::Hoo, "HOO", "Hydrogen-alpha in red with oxygen in green and blue."},
    {NarrowbandPalette::Hso, "HSO", "Hydrogen-alpha, sulfur, and oxygen mapped to RGB."},
    {NarrowbandPalette::Hos, "HOS", "Hydrogen-alpha, oxygen, and sulfur mapped to RGB."},
    {NarrowbandPalette::Oss, "OSS", "Oxygen in red with sulfur in green and blue."},
    {NarrowbandPalette::Ohh, "OHH", "Oxygen in red with hydrogen-alpha in green and blue."},
    {NarrowbandPalette::Osh, "OSH", "Oxygen, sulfur, and hydrogen-alpha mapped to RGB."},
    {NarrowbandPalette::Ohs, "OHS", "Oxygen, hydrogen-alpha, and sulfur mapped to RGB."},
    {NarrowbandPalette::Hss, "HSS", "Hydrogen-alpha in red with sulfur in green and blue."},
    {NarrowbandPalette::Realistic1, "Realistic1", "SetiAstro realistic blend with Ha/SII red and OIII blue."},
    {NarrowbandPalette::Realistic2, "Realistic2", "SetiAstro realistic blend weighted toward OIII blue."},
    {NarrowbandPalette::Foraxx, "Foraxx", "SetiAstro Foraxx narrowband blend."},
}};

/// The three channels a palette reads once substitution is resolved, and what was substituted.
struct PreparedSources
{
    /// H-alpha, or SII standing in for it.
    NarrowbandPaletteChannel hydrogenAlpha{};

    /// OIII, always the caller's.
    NarrowbandPaletteChannel oxygenIII{};

    /// SII, or H-alpha standing in for it.
    NarrowbandPaletteChannel sulfurII{};

    /// Whether SII stands in for an absent H-alpha.
    bool substitutedHydrogenAlpha = false;

    /// Whether H-alpha stands in for an absent SII.
    bool substitutedSulfurII = false;

    /// Whether Foraxx takes its two-channel form, which is when SII is absent.
    bool twoChannelForaxx = false;
};

[[nodiscard]] bool present(const NarrowbandPaletteChannel& channel) noexcept
{
    return channel.samples != nullptr || channel.layout != nullptr;
}

[[nodiscard]] bool completelyPresent(const NarrowbandPaletteChannel& channel) noexcept
{
    return channel.samples != nullptr && channel.layout != nullptr;
}

[[nodiscard]] Status validateChannel(const NarrowbandPaletteChannel& channel, SampleType sampleType,
                                     const ImageLayout& destinationLayout, void* destination, const char* operation)
{
    if (!completelyPresent(channel))
    {
        return failure(operation, ErrorCode::InvalidArgument,
                       "a present narrowband palette channel needs both samples and a layout");
    }
    const ImageLayout& layout = *channel.layout;
    if (layout.extent().channelCount() != 1u || !narrowband_detail::sameGeometry(layout, destinationLayout))
    {
        return failure(operation, ErrorCode::IncompatibleArguments,
                       "every palette source is one aligned monochrome image");
    }
    if (layout.elementSizeBytes() != sampleTypeSizeBytes(sampleType) ||
        !narrowband_detail::alignedFor(channel.samples, sampleType))
    {
        return failure(operation, ErrorCode::InvalidArgument,
                       "a palette source layout or pointer disagrees with its sample type");
    }
    if (narrowband_detail::overlaps(channel.samples, layout, destination, destinationLayout))
    {
        return failure(operation, ErrorCode::IncompatibleArguments,
                       "a palette source overlaps the writable destination");
    }
    return Status::success();
}

[[nodiscard]] Result<PreparedSources> validateComposition(const ExecutionContext& execution, SampleType sampleType,
                                                          const NarrowbandPaletteChannels& channels, void* destination,
                                                          const ImageLayout& destinationLayout,
                                                          const NarrowbandPaletteRequest& request,
                                                          const KernelValidityView& validity, const char* operation)
{
    if (!execution.valid())
    {
        return failure(operation, ErrorCode::NotInitialized, "a palette composition has no execution context");
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
        return failure(operation, ErrorCode::Unsupported,
                       "palette composition runs on a Float32 or Float64 working image");
    }
    if (destination == nullptr || !narrowband_detail::alignedFor(destination, sampleType) ||
        destinationLayout.elementSizeBytes() != sampleTypeSizeBytes(sampleType))
    {
        return failure(operation, ErrorCode::InvalidArgument,
                       "the palette destination pointer or layout disagrees with its sample type");
    }
    if (destinationLayout.extent().channelCount() != 3u)
    {
        return failure(operation, ErrorCode::IncompatibleArguments, "a palette destination has three channels");
    }
    if (!isNarrowbandPalette(request.palette) || !std::isfinite(request.intensities.hydrogenAlpha) ||
        !std::isfinite(request.intensities.oxygenIII) || !std::isfinite(request.intensities.sulfurII) ||
        request.intensities.hydrogenAlpha < 0.0 || request.intensities.oxygenIII < 0.0 ||
        request.intensities.sulfurII < 0.0)
    {
        return failure(operation, ErrorCode::InvalidArgument,
                       "the palette and every intensity must be finite, known and nonnegative");
    }
    const bool hasHydrogenAlpha = present(channels.hydrogenAlpha);
    const bool hasOxygenIII = present(channels.oxygenIII);
    const bool hasSulfurII = present(channels.sulfurII);
    if (!hasOxygenIII || (!hasHydrogenAlpha && !hasSulfurII))
    {
        return failure(operation, ErrorCode::IncompatibleArguments,
                       "palette composition needs OIII and at least one of H-alpha or SII");
    }
    for (const NarrowbandPaletteChannel* channel : std::array<const NarrowbandPaletteChannel*, 3u>{
             &channels.hydrogenAlpha, &channels.oxygenIII, &channels.sulfurII})
    {
        if (!present(*channel))
        {
            if (channel->samples != nullptr || channel->layout != nullptr)
            {
                return failure(operation, ErrorCode::InvalidArgument,
                               "an absent palette channel has neither samples nor a layout");
            }
            continue;
        }
        if (const Status valid = validateChannel(*channel, sampleType, destinationLayout, destination, operation);
            valid.failed())
        {
            return valid;
        }
    }
    if (const Status valid = narrowband_detail::validateValidity(validity, *channels.oxygenIII.layout, destination,
                                                                 destinationLayout, operation);
        valid.failed())
    {
        return valid;
    }
    PreparedSources prepared{};
    prepared.hydrogenAlpha = hasHydrogenAlpha ? channels.hydrogenAlpha : channels.sulfurII;
    prepared.oxygenIII = channels.oxygenIII;
    prepared.sulfurII = hasSulfurII ? channels.sulfurII : channels.hydrogenAlpha;
    prepared.substitutedHydrogenAlpha = !hasHydrogenAlpha;
    prepared.substitutedSulfurII = !hasSulfurII;
    prepared.twoChannelForaxx = hasHydrogenAlpha && !hasSulfurII;
    return prepared;
}

[[nodiscard]] std::array<double, 3u> mapPalette(NarrowbandPalette palette, double hydrogenAlpha, double oxygenIII,
                                                double sulfurII, bool twoChannelForaxx) noexcept
{
    std::array<double, 3u> result{};
    switch (palette)
    {
    case NarrowbandPalette::Sho:
        result = {sulfurII, hydrogenAlpha, oxygenIII};
        break;
    case NarrowbandPalette::Hoo:
        result = {hydrogenAlpha, oxygenIII, oxygenIII};
        break;
    case NarrowbandPalette::Hso:
        result = {hydrogenAlpha, sulfurII, oxygenIII};
        break;
    case NarrowbandPalette::Hos:
        result = {hydrogenAlpha, oxygenIII, sulfurII};
        break;
    case NarrowbandPalette::Oss:
        result = {oxygenIII, sulfurII, sulfurII};
        break;
    case NarrowbandPalette::Ohh:
        result = {oxygenIII, hydrogenAlpha, hydrogenAlpha};
        break;
    case NarrowbandPalette::Osh:
        result = {oxygenIII, sulfurII, hydrogenAlpha};
        break;
    case NarrowbandPalette::Ohs:
        result = {oxygenIII, hydrogenAlpha, sulfurII};
        break;
    case NarrowbandPalette::Hss:
        result = {hydrogenAlpha, sulfurII, sulfurII};
        break;
    case NarrowbandPalette::Realistic1:
        result = {(hydrogenAlpha + sulfurII) * 0.5, 0.3 * hydrogenAlpha + 0.7 * oxygenIII,
                  0.9 * oxygenIII + 0.1 * hydrogenAlpha};
        break;
    case NarrowbandPalette::Realistic2:
        result = {0.7 * hydrogenAlpha + 0.3 * sulfurII, 0.3 * sulfurII + 0.7 * oxygenIII, oxygenIII};
        break;
    case NarrowbandPalette::Foraxx: {
        const double product = hydrogenAlpha * oxygenIII;
        const double blend = std::pow(product, 1.0 - product);
        if (twoChannelForaxx)
        {
            result = {hydrogenAlpha, blend * hydrogenAlpha + (1.0 - blend) * oxygenIII, oxygenIII};
            break;
        }
        const double clippedOxygenIII = std::clamp(oxygenIII, 1.0e-6, 1.0);
        const double sulfurBlend = std::pow(clippedOxygenIII, 1.0 - clippedOxygenIII);
        result = {sulfurBlend * sulfurII + (1.0 - sulfurBlend) * hydrogenAlpha,
                  blend * hydrogenAlpha + (1.0 - blend) * oxygenIII, oxygenIII};
        break;
    }
    }
    for (double& channel : result)
    {
        channel = std::isfinite(channel) ? std::clamp(channel, 0.0, 1.0) : 0.0;
    }
    return result;
}

template <typename SampleT> struct PixelValues
{
    std::array<double, 3u> mapped{};
    bool admitted = false;
    std::uint64_t nonFinite = 0u;
};

template <typename SampleT>
[[nodiscard]] bool palettePixelAdmitted(const PreparedSources& sources, const KernelValidityView& validity,
                                        std::uint32_t x, std::uint32_t y) noexcept
{
    if (validity.descriptor.isAllValid())
    {
        return true;
    }
    const SampleT hydrogen =
        narrowband_detail::sampleAt<SampleT>(sources.hydrogenAlpha.samples, *sources.hydrogenAlpha.layout, x, y, 0u);
    const SampleT oxygen =
        narrowband_detail::sampleAt<SampleT>(sources.oxygenIII.samples, *sources.oxygenIII.layout, x, y, 0u);
    const SampleT sulfur =
        narrowband_detail::sampleAt<SampleT>(sources.sulfurII.samples, *sources.sulfurII.layout, x, y, 0u);
    return narrowband_detail::admits(validity, hydrogen, x, y) && narrowband_detail::admits(validity, oxygen, x, y) &&
           narrowband_detail::admits(validity, sulfur, x, y);
}

template <typename SampleT>
[[nodiscard]] PixelValues<SampleT> palettePixel(const PreparedSources& sources, const NarrowbandPaletteRequest& request,
                                                const KernelValidityView& validity, std::uint32_t x,
                                                std::uint32_t y) noexcept
{
    const SampleT hydrogenRaw =
        narrowband_detail::sampleAt<SampleT>(sources.hydrogenAlpha.samples, *sources.hydrogenAlpha.layout, x, y, 0u);
    const SampleT oxygenRaw =
        narrowband_detail::sampleAt<SampleT>(sources.oxygenIII.samples, *sources.oxygenIII.layout, x, y, 0u);
    const SampleT sulfurRaw =
        narrowband_detail::sampleAt<SampleT>(sources.sulfurII.samples, *sources.sulfurII.layout, x, y, 0u);
    PixelValues<SampleT> result{};
    result.admitted = validity.descriptor.isAllValid() || (narrowband_detail::admits(validity, hydrogenRaw, x, y) &&
                                                           narrowband_detail::admits(validity, oxygenRaw, x, y) &&
                                                           narrowband_detail::admits(validity, sulfurRaw, x, y));
    if (!result.admitted)
    {
        return result;
    }
    double hydrogen = narrowband_detail::finiteUnit(static_cast<double>(hydrogenRaw), result.nonFinite);
    double oxygen = narrowband_detail::finiteUnit(static_cast<double>(oxygenRaw), result.nonFinite);
    double sulfur = narrowband_detail::finiteUnit(static_cast<double>(sulfurRaw), result.nonFinite);
    hydrogen *= request.intensities.hydrogenAlpha;
    oxygen *= request.intensities.oxygenIII;
    sulfur *= request.intensities.sulfurII;
    result.mapped = mapPalette(request.palette, hydrogen, oxygen, sulfur, sources.twoChannelForaxx);
    return result;
}

[[nodiscard]] double maximumMerge(double left, double right) noexcept
{
    return (std::max)(left, right);
}

template <typename SampleT>
[[nodiscard]] Result<NarrowbandPaletteResult>
composeTyped(const ExecutionContext& execution, const PreparedSources& sources, void* destination,
             const ImageLayout& destinationLayout, const NarrowbandPaletteRequest& request,
             const KernelValidityView& validity)
{
    const std::uint32_t width = destinationLayout.extent().width();
    const std::uint32_t height = destinationLayout.extent().height();
    const TaskCost cost{.bytesPerItem = static_cast<std::uint64_t>(width) * sizeof(SampleT) * 6u,
                        .operationsPerItem = static_cast<std::uint64_t>(width) * 30u};
    std::atomic<std::uint64_t> nonFinite{0u};
    std::atomic<std::uint64_t> admitted{0u};
    const auto reduce = [&](TaskRange range, double& maximum) -> Status {
        std::uint64_t localNonFinite = 0u;
        std::uint64_t localAdmitted = 0u;
        for (std::uint64_t row = range.begin; row < range.end; ++row)
        {
            for (std::uint32_t x = 0u; x < width; ++x)
            {
                const PixelValues<SampleT> pixel =
                    palettePixel<SampleT>(sources, request, validity, x, static_cast<std::uint32_t>(row));
                if (!pixel.admitted)
                {
                    continue;
                }
                ++localAdmitted;
                localNonFinite += pixel.nonFinite;
                for (std::uint32_t channel = 0u; channel < 3u; ++channel)
                {
                    const SampleT stored = static_cast<SampleT>(pixel.mapped[channel]);
                    maximum = (std::max)(maximum, static_cast<double>(stored));
                    narrowband_detail::writeAt<SampleT>(destination, destinationLayout, x,
                                                        static_cast<std::uint32_t>(row), channel, stored);
                }
            }
        }
        nonFinite.fetch_add(localNonFinite, std::memory_order_relaxed);
        admitted.fetch_add(localAdmitted, std::memory_order_relaxed);
        return Status::success();
    };
    const Result<double> maximum = parallelReduce(execution, height, cost, 0.0, reduce, maximumMerge);
    if (!maximum.hasValue())
    {
        return maximum.status();
    }
    if (admitted.load(std::memory_order_relaxed) == 0u)
    {
        return failure(kComposeOperation, ErrorCode::EmptySelection,
                       "a palette composition has no pixel admitted by its shared validity rule");
    }
    const double divisor = request.normalize && *maximum > 0.0 ? *maximum : 1.0;
    const auto write = [&](TaskRange range) -> Status {
        for (std::uint64_t row = range.begin; row < range.end; ++row)
        {
            const std::uint32_t y = static_cast<std::uint32_t>(row);
            for (std::uint32_t x = 0u; x < width; ++x)
            {
                if (!palettePixelAdmitted<SampleT>(sources, validity, x, y))
                {
                    continue;
                }
                for (std::uint32_t channel = 0u; channel < 3u; ++channel)
                {
                    const SampleT value =
                        narrowband_detail::sampleAt<SampleT>(destination, destinationLayout, x, y, channel);
                    narrowband_detail::writeAt<SampleT>(destination, destinationLayout, x, y, channel,
                                                        static_cast<double>(value) / divisor);
                }
            }
        }
        return Status::success();
    };
    if (divisor != 1.0)
    {
        const Status written = parallelFor(execution, height, cost, write);
        if (written.failed())
        {
            return written;
        }
    }
    NarrowbandPaletteResult result{};
    result.run.selection.total = static_cast<std::uint64_t>(width) * height;
    result.run.selection.valid = admitted.load(std::memory_order_relaxed);
    result.run.outputsWritten = result.run.selection.valid * 3u;
    result.maximumBeforeNormalization = *maximum;
    result.nonFiniteSamples = nonFinite.load(std::memory_order_relaxed);
    result.substitutedHydrogenAlpha = sources.substitutedHydrogenAlpha;
    result.substitutedSulfurII = sources.substitutedSulfurII;
    result.plannedWorkers = plannedWorkerCount(execution, height, cost);
    return result;
}

} // namespace

std::span<const NarrowbandPaletteDescriptor> narrowbandPalettes() noexcept
{
    return kPaletteCatalog;
}

const char* narrowbandPaletteName(NarrowbandPalette palette) noexcept
{
    return isNarrowbandPalette(palette) ? kPaletteCatalog[static_cast<std::size_t>(palette)].name
                                        : "UnknownNarrowbandPalette";
}

NarrowbandPalette narrowbandPaletteFromIndex(std::size_t index) noexcept
{
    return index < kPaletteCatalog.size() ? kPaletteCatalog[index].palette : NarrowbandPalette::Foraxx;
}

std::size_t narrowbandPaletteIndex(NarrowbandPalette palette) noexcept
{
    return isNarrowbandPalette(palette) ? static_cast<std::size_t>(palette) : kPaletteCatalog.size() - 1u;
}

Result<ImageDescriptor> narrowbandPaletteOutputDescriptor(const ImageDescriptor& reference)
{
    if (!narrowband_detail::supportedWorkingType(reference.sampleType()) ||
        reference.channelSchema() != ChannelSchema::Mono || reference.cfa().isMosaiced() ||
        (reference.numericDomain() != NumericDomain::NormalizedLinear &&
         reference.numericDomain() != NumericDomain::EncodedNonlinear))
    {
        return failure(kDescriptorOperation, ErrorCode::Unsupported,
                       "a palette reference is a resolved normalized floating monochrome image");
    }
    const std::optional<ImageExtent> extent =
        ImageExtent::create(reference.extent().width(), reference.extent().height(), 3u, defaultEngineLimits());
    if (!extent.has_value())
    {
        return failure(kDescriptorOperation, ErrorCode::LimitExceeded,
                       "the RGB palette extent exceeds the engine limits");
    }
    ImageDescriptorRequest request = reference.toRequest();
    request.extent = *extent;
    request.channelSchema = ChannelSchema::Rgb;
    request.colorSpace = ColorSpace::NarrowbandMapping;
    request.cfa = {};
    const std::optional<ImageDescriptor> result = ImageDescriptor::create(request);
    return result.has_value() ? Result<ImageDescriptor>{*result}
                              : Result<ImageDescriptor>{failure(kDescriptorOperation, ErrorCode::IncompatibleArguments,
                                                                "the palette output description is inconsistent")};
}

Result<NarrowbandPaletteResult> composeNarrowbandPalette(const ExecutionContext& execution, SampleType sampleType,
                                                         const NarrowbandPaletteChannels& channels, void* destination,
                                                         const ImageLayout& destinationLayout,
                                                         const NarrowbandPaletteRequest& request,
                                                         const KernelValidityView& validity)
{
    const Result<PreparedSources> prepared = validateComposition(
        execution, sampleType, channels, destination, destinationLayout, request, validity, kComposeOperation);
    if (!prepared.hasValue())
    {
        return prepared.status();
    }
    return sampleType == SampleType::Float32
               ? composeTyped<float>(execution, *prepared, destination, destinationLayout, request, validity)
               : composeTyped<double>(execution, *prepared, destination, destinationLayout, request, validity);
}

Status composeNarrowbandPaletteGrid(const ExecutionContext& execution, SampleType sampleType,
                                    const NarrowbandPaletteChannels& channels,
                                    std::span<const NarrowbandPaletteDestination> destinations,
                                    const NarrowbandPaletteIntensities& intensities,
                                    std::span<NarrowbandPaletteResult> results, bool normalize,
                                    const KernelValidityView& validity)
{
    if (destinations.empty() || results.size() != destinations.size())
    {
        return failure(kGridOperation, ErrorCode::InvalidArgument,
                       "a palette grid needs one result slot per nonempty destination");
    }
    for (std::size_t index = 0u; index < destinations.size(); ++index)
    {
        const NarrowbandPaletteDestination& destination = destinations[index];
        if (destination.layout == nullptr)
        {
            return failure(kGridOperation, ErrorCode::InvalidArgument, "a palette-grid destination has no layout");
        }
        const NarrowbandPaletteRequest request{
            .palette = destination.palette, .intensities = intensities, .normalize = normalize};
        const Result<PreparedSources> prepared =
            validateComposition(execution, sampleType, channels, destination.samples, *destination.layout, request,
                                validity, kGridOperation);
        if (!prepared.hasValue())
        {
            return prepared.status();
        }
        for (std::size_t previous = 0u; previous < index; ++previous)
        {
            if (narrowband_detail::overlaps(destination.samples, *destination.layout, destinations[previous].samples,
                                            *destinations[previous].layout))
            {
                return failure(kGridOperation, ErrorCode::IncompatibleArguments,
                               "palette-grid destinations overlap each other");
            }
        }
    }
    for (std::size_t index = 0u; index < destinations.size(); ++index)
    {
        const NarrowbandPaletteDestination& destination = destinations[index];
        const NarrowbandPaletteRequest request{
            .palette = destination.palette, .intensities = intensities, .normalize = normalize};
        const Result<NarrowbandPaletteResult> composed = composeNarrowbandPalette(
            execution, sampleType, channels, destination.samples, *destination.layout, request, validity);
        if (!composed.hasValue())
        {
            return composed.status();
        }
        results[index] = *composed;
    }
    return Status::success();
}

Result<MetadataTransition> narrowbandPaletteTransition(const ImageSemantics& source, NarrowbandPalette palette)
{
    if (!isNarrowbandPalette(palette))
    {
        return failure(kTransitionOperation, ErrorCode::InvalidArgument,
                       "the palette transition names an unknown palette");
    }
    return operationTransition(kNarrowbandPaletteEffect,
                               OperationOutcome{.operation = kComposeOperation,
                                                .detail = std::string{"palette="} + narrowbandPaletteName(palette),
                                                .semantics = source});
}

} // namespace an
