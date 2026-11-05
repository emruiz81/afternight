// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The viewer's display operations over the engine: presentation, channel extraction, the chrominance view, the mask
/// overlay, the lens's counts and the pixel readout.

#include "widgets/ViewPresentation.h"

#include "session/DisplayProxy.h"

#include <AfterNightEngine/Algorithms/Geometry/Geometry.h>
#include <AfterNightEngine/Algorithms/Statistics/BasicStatistics.h>
#include <AfterNightEngine/Algorithms/Statistics/Histogram.h>
#include <AfterNightEngine/Core/EngineLimits.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/ImageExtent.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Image/ConversionPolicy.h>
#include <AfterNightEngine/Image/Image.h>
#include <AfterNightEngine/Image/ImageView.h>
#include <AfterNightEngine/Image/SampleConversion.h>
#include <AfterNightEngine/Image/SampleTraits.h>
#include <AfterNightEngine/Image/ValidityMask.h>
#include <AfterNightEngine/Image/ValidityMaskView.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <functional>
#include <span>
#include <sstream>
#include <type_traits>
#include <utility>
#include <variant>

namespace an::ui
{

namespace
{

/// An image as the view reads it: its description, restated when its domain has no full scale, and where its samples
/// and validity are; and, for a region cut from an image whose validity is a mask, the cut it owns. Built in place,
/// because the validity view points into it.
struct ViewSource
{
    std::optional<an::ImageDescriptor> descriptor{};
    const void* samples = nullptr;
    std::optional<an::ImageLayout> layout{};
    std::optional<an::ValidityMaskView<const std::uint8_t>> maskView{};
    an::KernelValidityView validity{};
    bool restated = false;
    std::optional<an::consumer::AnyImage> ownedImage{};
    std::optional<an::ValidityMask> ownedMask{};

    ViewSource() = default;
    ViewSource(const ViewSource&) = delete;
    ViewSource& operator=(const ViewSource&) = delete;
};

/// The description a view reads an image under: a physical or sensor-code domain, which has no full scale to show,
/// read as a normalized linear fraction.
[[nodiscard]] std::optional<an::ImageDescriptor> viewDescriptor(const an::ImageDescriptor& stored, bool& restated)
{
    restated = false;
    const an::NumericDomain domain = stored.numericDomain();
    if (domain != an::NumericDomain::PhysicalScaled && domain != an::NumericDomain::RawSensorCode)
    {
        return stored;
    }
    an::ImageDescriptorRequest request = stored.toRequest();
    request.numericDomain = an::NumericDomain::NormalizedLinear;
    restated = true;
    return an::ImageDescriptor::create(request);
}

/// Fills a view source from the whole of an image under a read lease, or says why it cannot.
[[nodiscard]] an::Status describeLeased(const an::consumer::AssetReadLease& lease, ViewSource& out)
{
    if (!lease.valid())
    {
        return an::Status::failure(an::ErrorCode::NotInitialized, "the view's image is under no lease");
    }
    an::Status described = an::Status::success();
    lease.visit([&](const auto& image) {
        const std::optional<an::ImageDescriptor> stored = image.descriptor();
        const auto view = image.view();
        if (!stored.has_value() || !view.has_value())
        {
            described = an::Status::failure(an::ErrorCode::NotInitialized, "the view's image holds nothing");
            return;
        }
        out.descriptor = viewDescriptor(*stored, out.restated);
        if (!out.descriptor.has_value())
        {
            described = an::Status::failure(an::ErrorCode::IncompatibleArguments,
                                            "the view's image has no description as a fraction of full scale");
            return;
        }
        out.samples = view->data();
        out.layout = view->layout();
        out.validity.descriptor = image.metadata().semantics().validity();
    });
    if (described.failed())
    {
        return described;
    }
    if (const an::ValidityMask* mask = lease.validity(); mask != nullptr)
    {
        out.maskView = mask->view();
        out.validity.mask = out.maskView.has_value() ? &*out.maskView : nullptr;
    }
    return an::Status::success();
}

/// Fills a view source from a display proxy of an image.
[[nodiscard]] an::Status describeProxy(const DisplayProxy& proxy, ViewSource& out)
{
    out.descriptor = viewDescriptor(proxy.descriptor(), out.restated);
    if (!out.descriptor.has_value())
    {
        return an::Status::failure(an::ErrorCode::IncompatibleArguments,
                                   "the view's image has no description as a fraction of full scale");
    }
    out.samples = proxy.samples();
    out.layout = proxy.layout();
    out.validity = proxy.validity();
    return an::Status::success();
}

/// The part of a source's pixels a region of the image covers, for a source reduced by a factor: the region divided by
/// the factor and rounded outward, clipped to the source; nothing when the region is outside it.
[[nodiscard]] std::optional<ViewRegion> regionOfSource(const ViewRegion& region, std::uint32_t factor,
                                                       std::uint32_t width, std::uint32_t height) noexcept
{
    if (region.width == 0u || region.height == 0u)
    {
        return std::nullopt;
    }
    const std::uint64_t left = region.x / factor;
    const std::uint64_t top = region.y / factor;
    const std::uint64_t right =
        (std::min)(std::uint64_t{width}, (std::uint64_t{region.x} + region.width + factor - 1u) / factor);
    const std::uint64_t bottom =
        (std::min)(std::uint64_t{height}, (std::uint64_t{region.y} + region.height + factor - 1u) / factor);
    if (left >= right || top >= bottom)
    {
        return std::nullopt;
    }
    return ViewRegion{.x = static_cast<std::uint32_t>(left),
                      .y = static_cast<std::uint32_t>(top),
                      .width = static_cast<std::uint32_t>(right - left),
                      .height = static_cast<std::uint32_t>(bottom - top)};
}

/// Narrows a source to a region of its pixels, already clipped to it.
///
/// A source whose validity is decided by value is the same samples through a narrower layout. A source whose validity
/// is a mask has no narrower view of its mask - the bits of a region starting off a byte boundary would each have to
/// shift - so the engine crops its samples and its mask into an image and a mask the source then owns.
[[nodiscard]] an::Status narrowTo(const an::EngineContext& engine, const an::ExecutionContext& execution,
                                  ViewSource& source, const ViewRegion& region)
{
    const an::ImageDescriptor& descriptor = *source.descriptor;
    const an::ImageLayout& layout = *source.layout;
    const std::uint32_t channels = descriptor.extent().channelCount();
    if (region.x == 0u && region.y == 0u && region.width == descriptor.extent().width() &&
        region.height == descriptor.extent().height())
    {
        return an::Status::success();
    }
    an::ImageDescriptorRequest request = descriptor.toRequest();
    const std::optional<an::ImageExtent> extent =
        an::ImageExtent::create(region.width, region.height, channels, engine.limits());
    if (!extent.has_value())
    {
        return an::Status::failure(an::ErrorCode::EmptySelection, "the region has no extent");
    }
    request.extent = *extent;
    if (source.validity.mask == nullptr)
    {
        const std::optional<std::uint64_t> offset = layout.sampleByteOffset(0u, region.y, region.x, 0u);
        const std::optional<an::ImageLayout> narrowed =
            an::ImageLayout::createWithStrides(an::ImageLayoutRequest{.extent = *extent,
                                                                      .elementSizeBytes = layout.elementSizeBytes(),
                                                                      .samplesPerPixel = layout.samplesPerPixel(),
                                                                      .planeCount = layout.planeCount(),
                                                                      .rowAlignmentBytes = layout.elementSizeBytes()},
                                               layout.rowStrideBytes(), layout.planeStrideBytes(), engine.limits());
        if (!offset.has_value() || !narrowed.has_value())
        {
            return an::Status::failure(an::ErrorCode::EmptySelection, "the region has no view of the image");
        }
        source.descriptor = an::ImageDescriptor::create(request);
        source.samples = static_cast<const std::uint8_t*>(source.samples) + *offset;
        source.layout = *narrowed;
        if (!source.descriptor.has_value())
        {
            return an::Status::failure(an::ErrorCode::IncompatibleArguments, "the region has no description");
        }
        return an::Status::success();
    }

    const an::Result<an::GeometryPlan> plan = an::planCrop(
        descriptor, an::CropRequest{.x = region.x, .y = region.y, .width = region.width, .height = region.height});
    if (!plan.hasValue())
    {
        return plan.status();
    }
    an::Result<an::ValidityMask> mask = an::ValidityMask::create(engine, *extent);
    if (!mask.hasValue())
    {
        return mask.status();
    }
    an::Status cropped = an::Status::failure(an::ErrorCode::Internal, "the region was not cropped");
    const bool dispatched = an::dispatchSampleType(descriptor.sampleType(), [&]<typename SampleT>() {
        an::Result<an::Image<SampleT>> image = an::Image<SampleT>::create(
            engine, plan->destinationDescriptor(), an::StorageArrangement::Planar, an::StorageInitialization::Zeroed);
        std::optional<an::ValidityMaskView<std::uint8_t>> bits = mask->view();
        if (!image.hasValue() || !bits.has_value())
        {
            cropped = image.hasValue() ? an::Status::failure(an::ErrorCode::NotInitialized, "the cut's mask is empty")
                                       : image.status();
            return;
        }
        {
            std::optional<an::ImageWriteAccess<SampleT>> access = image->write();
            if (!access.has_value())
            {
                cropped = an::Status::failure(an::ErrorCode::NotInitialized, "the cut holds nothing");
                return;
            }
            const an::Result<an::GeometryResult> moved = an::transformImageGeometry(
                execution, descriptor.sampleType(), source.samples, layout, access->view().data(),
                access->view().layout(), *plan,
                an::GeometrySelection{.validity = source.validity.descriptor, .mask = source.validity.mask}, *bits);
            if (!moved.hasValue())
            {
                cropped = moved.status();
                return;
            }
        }
        source.ownedImage.emplace(std::move(*image));
        const auto view = std::get<an::Image<SampleT>>(*source.ownedImage).view();
        source.samples = view->data();
        source.layout = view->layout();
        cropped = an::Status::success();
    });
    if (!dispatched)
    {
        return an::Status::failure(an::ErrorCode::Unsupported, "the image's representation is not one of the eight");
    }
    if (cropped.failed())
    {
        return cropped;
    }
    source.descriptor = an::ImageDescriptor::create(request);
    if (!source.descriptor.has_value())
    {
        return an::Status::failure(an::ErrorCode::IncompatibleArguments, "the region has no description");
    }
    source.ownedMask.emplace(std::move(*mask));
    source.maskView = std::as_const(*source.ownedMask).view();
    source.validity =
        an::KernelValidityView{.descriptor = *an::ValidityDescriptor::create({.rule = an::ValidityRule::ExplicitMask}),
                               .mask = source.maskView.has_value() ? &*source.maskView : nullptr};
    return an::Status::success();
}

/// The colour basis the channel scopes read a source's red, green and blue as: linear for a source that says it is
/// linear, sRGB-encoded otherwise.
[[nodiscard]] an::ColorEncoding sourceEncodingOf(const an::ImageDescriptor& descriptor) noexcept
{
    an::ColorEncoding encoding{};
    encoding.schema = an::ChannelSchema::Rgb;
    encoding.transfer = descriptor.colorSpace() == an::ColorSpace::LinearRgb ? an::ColorTransferFunction::Linear
                                                                             : an::ColorTransferFunction::Srgb;
    return encoding;
}

[[nodiscard]] bool isColour(const an::ImageDescriptor& descriptor) noexcept
{
    return descriptor.channelSchema() == an::ChannelSchema::Rgb ||
           descriptor.channelSchema() == an::ChannelSchema::Rgba;
}

/// One working plane: an engine image of one channel, with what it is described as.
struct WorkingPlane
{
    an::consumer::AnyImage image;
    an::ImageDescriptor descriptor;
    an::KernelValidityView validity{};
};

/// Extracts one channel scope of a source into a working plane the engine allocates.
[[nodiscard]] an::Result<WorkingPlane> extractPlane(const an::EngineContext& engine,
                                                    const an::ExecutionContext& execution, const ViewSource& source,
                                                    an::ChannelScope scope)
{
    const an::Result<an::ChannelScopePlan> plan =
        an::planChannelScope(an::ChannelScopeRequest{.scope = scope,
                                                     .sourceEncoding = sourceEncodingOf(*source.descriptor),
                                                     .units = an::ChannelScopeUnits::UnitInterval});
    if (!plan.hasValue())
    {
        return plan.status();
    }
    const an::Result<an::ImageDescriptor> planeDescriptor = an::channelScopePlaneDescriptor(*plan, *source.descriptor);
    if (!planeDescriptor.hasValue())
    {
        return planeDescriptor.status();
    }
    an::Result<WorkingPlane> made = an::Status::failure(an::ErrorCode::Internal, "no plane was extracted");
    const bool dispatched = an::dispatchSampleType(planeDescriptor->sampleType(), [&]<typename SampleT>() {
        an::Result<an::Image<SampleT>> plane = an::Image<SampleT>::create(
            engine, *planeDescriptor, an::StorageArrangement::Planar, an::StorageInitialization::Uninitialized);
        if (!plane.hasValue())
        {
            made = plane.status();
            return;
        }
        an::Result<an::ChannelScopeResult> extracted = an::Status::failure(an::ErrorCode::Internal, "not extracted");
        {
            std::optional<an::ImageWriteAccess<SampleT>> access = plane->write();
            if (!access.has_value())
            {
                made = an::Status::failure(an::ErrorCode::NotInitialized, "the working plane holds nothing");
                return;
            }
            extracted = an::extractChannelScope(execution, *plan,
                                                an::ChannelScopeSource{.descriptor = &*source.descriptor,
                                                                       .samples = source.samples,
                                                                       .layout = &*source.layout,
                                                                       .validity = source.validity},
                                                access->view().data(), access->view().layout());
        }
        if (!extracted.hasValue())
        {
            made = extracted.status();
            return;
        }
        WorkingPlane working{an::consumer::AnyImage(std::move(*plane)), *planeDescriptor, {}};
        if (an::channelScopeEncoding(scope) == an::ChannelSchema::Rgb)
        {
            // A stored channel keeps the source's own rule; a mask with a plane per channel gives it that plane.
            working.validity.descriptor = source.validity.descriptor;
            if (source.validity.mask != nullptr)
            {
                working.validity.mask = source.validity.mask;
                const std::uint32_t channel = an::channelScopeComponent(scope);
                working.validity.maskPlane = source.validity.mask->planeCount() > channel ? channel : 0u;
            }
        }
        else
        {
            // A component the scope could not convert is a quiet NaN.
            working.validity.descriptor = *an::ValidityDescriptor::create({.rule = an::ValidityRule::FiniteOnly});
        }
        made = std::move(working);
    });
    if (!dispatched)
    {
        return an::Status::failure(an::ErrorCode::Unsupported, "the channel's representation is not one of the eight");
    }
    return made;
}

/// The samples, layout and description of a working plane, as a presentation reads them.
template <typename BodyT> decltype(auto) withPlane(const WorkingPlane& plane, BodyT&& body)
{
    return std::visit(
        [&](const auto& image) -> decltype(auto) {
            const auto view = image.view();
            return std::forward<BodyT>(body)(view.has_value() ? static_cast<const void*>(view->data()) : nullptr,
                                             view.has_value() ? std::optional<an::ImageLayout>(view->layout())
                                                              : std::optional<an::ImageLayout>{});
        },
        plane.image);
}

/// Composes CIE a* and b* planes into a three-channel image: a* as red, b* as green and a black blue.
template <typename SampleT>
[[nodiscard]] an::Result<WorkingPlane> composeChrominance(const an::EngineContext& engine, const WorkingPlane& a,
                                                          const WorkingPlane& b)
{
    an::ImageDescriptorRequest request = a.descriptor.toRequest();
    const std::optional<an::ImageExtent> extent =
        an::ImageExtent::create(request.extent.width(), request.extent.height(), 3u, engine.limits());
    if (!extent.has_value())
    {
        return an::Status::failure(an::ErrorCode::InvalidArgument, "the chrominance view has no extent");
    }
    request.extent = *extent;
    request.channelSchema = an::ChannelSchema::Rgb;
    request.colorSpace = an::ColorSpace::Unspecified;
    const std::optional<an::ImageDescriptor> descriptor = an::ImageDescriptor::create(request);
    if (!descriptor.has_value())
    {
        return an::Status::failure(an::ErrorCode::IncompatibleArguments, "the chrominance view has no description");
    }
    an::Result<an::Image<SampleT>> composed = an::Image<SampleT>::create(
        engine, *descriptor, an::StorageArrangement::Planar, an::StorageInitialization::Zeroed);
    if (!composed.hasValue())
    {
        return composed.status();
    }
    const auto* aImage = std::get_if<an::Image<SampleT>>(&a.image);
    const auto* bImage = std::get_if<an::Image<SampleT>>(&b.image);
    if (aImage == nullptr || bImage == nullptr)
    {
        return an::Status::failure(an::ErrorCode::Internal, "the chrominance planes are of another representation");
    }
    const auto aView = aImage->view();
    const auto bView = bImage->view();
    {
        std::optional<an::ImageWriteAccess<SampleT>> access = composed->write();
        if (!access.has_value() || !aView.has_value() || !bView.has_value())
        {
            return an::Status::failure(an::ErrorCode::NotInitialized, "the chrominance view holds nothing");
        }
        const auto& target = access->view();
        for (std::uint32_t y = 0u; y < target.height(); ++y)
        {
            const auto aRow = aView->row(0u, y);
            const auto bRow = bView->row(0u, y);
            const auto red = target.row(0u, y);
            const auto green = target.row(1u, y);
            if (!aRow.has_value() || !bRow.has_value() || !red.has_value() || !green.has_value())
            {
                return an::Status::failure(an::ErrorCode::Internal, "a chrominance row is out of range");
            }
            std::copy(aRow->begin(), aRow->end(), red->begin());
            std::copy(bRow->begin(), bRow->end(), green->begin());
        }
    }
    WorkingPlane working{an::consumer::AnyImage(std::move(*composed)), *descriptor, {}};
    working.validity.descriptor = *an::ValidityDescriptor::create({.rule = an::ValidityRule::FiniteOnly});
    return working;
}

/// What a presentation reads of one channel choice: the source itself, a plane extracted from it, or the composed
/// chrominance, which the working plane, when there is one, owns.
struct ChannelSource
{
    const an::ImageDescriptor* descriptor = nullptr;
    const void* samples = nullptr;
    std::optional<an::ImageLayout> layout{};
    an::KernelValidityView validity{};
    std::optional<WorkingPlane> plane{};
};

/// The channel choice of a source, as a presentation or an analysis reads it.
[[nodiscard]] an::Status channelOf(const an::EngineContext& engine, const an::ExecutionContext& execution,
                                   const ViewSource& source, ViewChannel channel, ChannelSource& out)
{
    if (channel.chrominance)
    {
        an::Result<WorkingPlane> a = extractPlane(engine, execution, source, an::ChannelScope::CieA);
        if (!a.hasValue())
        {
            return a.status();
        }
        an::Result<WorkingPlane> b = extractPlane(engine, execution, source, an::ChannelScope::CieB);
        if (!b.hasValue())
        {
            return b.status();
        }
        an::Result<WorkingPlane> composed = a->descriptor.sampleType() == an::SampleType::Float64
                                                ? composeChrominance<double>(engine, *a, *b)
                                                : composeChrominance<float>(engine, *a, *b);
        if (!composed.hasValue())
        {
            return composed.status();
        }
        out.plane.emplace(std::move(*composed));
    }
    else if (channel.scope != an::ChannelScope::Whole)
    {
        an::Result<WorkingPlane> plane = extractPlane(engine, execution, source, channel.scope);
        if (!plane.hasValue())
        {
            return plane.status();
        }
        out.plane.emplace(std::move(*plane));
    }
    if (!out.plane.has_value())
    {
        out.descriptor = &*source.descriptor;
        out.samples = source.samples;
        out.layout = source.layout;
        out.validity = source.validity;
        return an::Status::success();
    }
    out.descriptor = &out.plane->descriptor;
    out.validity = out.plane->validity;
    withPlane(*out.plane, [&](const void* samples, const std::optional<an::ImageLayout>& layout) {
        out.samples = samples;
        out.layout = layout;
        return 0;
    });
    if (out.samples == nullptr || !out.layout.has_value())
    {
        return an::Status::failure(an::ErrorCode::NotInitialized, "the working plane holds nothing");
    }
    return an::Status::success();
}

/// The request a view presents through: an output it paints, and no colour balance of one channel, which the engine
/// refuses.
[[nodiscard]] an::DisplayPresentationRequest viewRequest(an::DisplayPresentationRequest request,
                                                         const an::ImageDescriptor& descriptor) noexcept
{
    if (request.output != an::PresentationOutput::Interleaved8 && request.output != an::PresentationOutput::Rgba8 &&
        request.output != an::PresentationOutput::Bgra8)
    {
        request.output = an::PresentationOutput::Interleaved8;
    }
    if (descriptor.extent().channelCount() < 3u)
    {
        // A balance is a colour operation; the engine refuses it on one channel.
        request.neutralize = false;
        request.calibrate = false;
    }
    return request;
}

/// Whether a presentation measures anything of what it presents: a stretch's statistics or a colour balance.
[[nodiscard]] bool measures(const an::DisplayPresentationRequest& request) noexcept
{
    return request.stretch == an::PresentationStretch::Automatic || request.neutralize || request.calibrate;
}

/// Measures what a presentation of a channel source would present through.
[[nodiscard]] an::Result<an::DisplayPresentationAnalysis> analyzeChannel(const an::ExecutionContext& execution,
                                                                         const ChannelSource& source,
                                                                         const an::DisplayPresentationRequest& asked)
{
    const an::Result<an::DisplayPresentationPlan> plan =
        an::planDisplayPresentation(viewRequest(asked, *source.descriptor));
    if (!plan.hasValue())
    {
        return plan.status();
    }
    return an::analyzeDisplayPresentation(execution, *plan,
                                          an::DisplayPresentationSource{.descriptor = source.descriptor,
                                                                        .samples = source.samples,
                                                                        .layout = &*source.layout,
                                                                        .validity = source.validity});
}

/// Measures the whole image for a presentation of part of it, or nothing when it is not asked for.
using WholeAnalysis = std::function<an::Result<an::DisplayPresentationAnalysis>()>;

/// Runs the plan the settings select over a channel source, into display bytes.
[[nodiscard]] an::Result<ViewPresentationOutcome>
presentSource(const an::EngineContext& engine, const an::ExecutionContext& execution, const ChannelSource& channel,
              const ViewPresentationSettings& settings, const an::DisplayPresentationAnalysis* reuse,
              an::consumer::ConsumerMemory* memory, const WholeAnalysis& measureWhole)
{
    const an::ImageDescriptor& descriptor = *channel.descriptor;
    const an::DisplayPresentationRequest request = viewRequest(settings.request, descriptor);
    const an::DisplayPresentationSource source{.descriptor = &descriptor,
                                               .samples = channel.samples,
                                               .layout = &*channel.layout,
                                               .validity = channel.validity};

    ViewPresentationOutcome outcome;
    std::optional<an::DisplayPresentationAnalysis> whole;
    const an::DisplayPresentationAnalysis* through = reuse;
    if (through == nullptr && measureWhole && measures(request))
    {
        const std::chrono::steady_clock::time_point begun = std::chrono::steady_clock::now();
        an::Result<an::DisplayPresentationAnalysis> measured = measureWhole();
        outcome.wholeMeasureTime =
            std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - begun);
        if (!measured.hasValue())
        {
            return measured.status();
        }
        whole = *measured;
        through = &*whole;
        outcome.measuredWhole = true;
    }

    const auto run = [&](auto&& plan, auto&& layoutOf, auto&& present) -> an::Status {
        const an::Result<an::ImageLayoutRequest> wanted = layoutOf(plan, descriptor);
        if (!wanted.hasValue())
        {
            return wanted.status();
        }
        an::Result<std::shared_ptr<PresentedImage>> bytes =
            request.output == an::PresentationOutput::Bgra8
                ? PresentedImage::allocate(engine, *wanted, PresentedFormat::Bgra8Premultiplied, memory)
                : PresentedImage::allocate(engine, *wanted, memory);
        if (!bytes.hasValue())
        {
            return bytes.status();
        }
        an::Result<an::DisplayPresentationResult> presented =
            present(plan, (*bytes)->data(), (*bytes)->layout(), through);
        if (!presented.hasValue() && through != nullptr &&
            presented.status().code() == an::ErrorCode::IncompatibleArguments)
        {
            // An analysis taken under other decisions is set aside rather than trusted: a part of the image is
            // presented through the whole image's own, the whole image measures itself.
            if (measureWhole && !outcome.measuredWhole && measures(request))
            {
                const std::chrono::steady_clock::time_point begun = std::chrono::steady_clock::now();
                an::Result<an::DisplayPresentationAnalysis> measured = measureWhole();
                outcome.wholeMeasureTime =
                    std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - begun);
                if (!measured.hasValue())
                {
                    return measured.status();
                }
                whole = *measured;
                outcome.measuredWhole = true;
                presented = present(plan, (*bytes)->data(), (*bytes)->layout(), &*whole);
            }
            else
            {
                presented = present(plan, (*bytes)->data(), (*bytes)->layout(), nullptr);
            }
        }
        if (!presented.hasValue())
        {
            return presented.status();
        }
        outcome.image = std::move(*bytes);
        outcome.analysis = presented->analysis;
        outcome.analysisReused = presented->analysisReused && reuse != nullptr && !outcome.measuredWhole;
        return an::Status::success();
    };

    an::Status ran = an::Status::success();
    if (settings.subsampleFactor > 1u)
    {
        const an::Result<an::RealtimePreviewDisplayPresentationPlan> plan =
            an::planRealtimePreviewDisplayPresentation(request, settings.subsampleFactor);
        if (!plan.hasValue())
        {
            return plan.status();
        }
        ran = run(
            *plan,
            [](const auto& checked, const an::ImageDescriptor& described) {
                return an::realtimePreviewDisplayPresentationLayout(checked, described);
            },
            [&](const auto& checked, void* destination, const an::ImageLayout& destinationLayout,
                const an::DisplayPresentationAnalysis* analysis) {
                return an::presentImageRealtimePreview(execution, checked, source, destination, destinationLayout,
                                                       analysis);
            });
    }
    else
    {
        const an::Result<an::DisplayPresentationPlan> plan = an::planDisplayPresentation(request);
        if (!plan.hasValue())
        {
            return plan.status();
        }
        ran = run(
            *plan,
            [](const auto& checked, const an::ImageDescriptor& described) {
                return an::displayPresentationLayout(checked, described);
            },
            [&](const auto& checked, void* destination, const an::ImageLayout& destinationLayout,
                const an::DisplayPresentationAnalysis* analysis) {
                return an::presentImage(execution, checked, source, destination, destinationLayout, analysis);
            });
    }
    if (ran.failed())
    {
        return ran;
    }
    return outcome;
}

/// The description of presented bytes as an engine image of one or three UInt8 channels, for a multiply.
[[nodiscard]] std::optional<an::ImageDescriptor> describePresented(const PresentedImage& image,
                                                                   const an::EngineLimits& limits)
{
    if (image.format() == PresentedFormat::Rgba8 || image.format() == PresentedFormat::Bgra8Premultiplied)
    {
        return std::nullopt;
    }
    const std::uint32_t channels = image.format() == PresentedFormat::Grey8 ? 1u : 3u;
    const std::optional<an::ImageExtent> extent =
        an::ImageExtent::create(image.width(), image.height(), channels, limits);
    if (!extent.has_value())
    {
        return std::nullopt;
    }
    return an::ImageDescriptor::create(
        an::ImageDescriptorRequest{.extent = *extent,
                                   .sampleType = an::SampleType::UInt8,
                                   .channelSchema = channels == 1u ? an::ChannelSchema::Mono : an::ChannelSchema::Rgb,
                                   .colorSpace = an::ColorSpace::Unspecified,
                                   .numericDomain = an::NumericDomain::EncodedNonlinear,
                                   .cfa = an::CfaDescriptor{},
                                   .coordinateOrigin = an::CoordinateOrigin::TopLeft,
                                   .pixelAspectRatio = 1.0});
}

/// The value of one stored sample as a fraction of full scale.
template <typename SampleT> [[nodiscard]] double fractionOf(SampleT value) noexcept
{
    return an::convertSample<SampleT, double>(value, an::ConversionPolicy{}).value;
}

/// Which byte of a presented pixel holds a colour channel: blue-first bytes hold red third.
[[nodiscard]] std::uint32_t byteOfChannel(PresentedFormat format, std::uint32_t channel) noexcept
{
    return format == PresentedFormat::Bgra8Premultiplied ? 2u - channel : channel;
}

/// Fills a view source from an image or from its display proxy, whole or narrowed to the part a region of the image
/// covers, and says which part of the image the source covers.
[[nodiscard]] an::Status describeShown(const an::EngineContext& engine, const an::ExecutionContext& execution,
                                       const an::consumer::AssetReadLease& lease, const DisplayProxy* proxy,
                                       std::uint32_t proxyFactor, const std::optional<ViewRegion>& region,
                                       ViewSource& out, ViewRegion& covered)
{
    if (proxyFactor > 1u)
    {
        if (proxy == nullptr || proxy->factor() != proxyFactor || !(proxy->source() == lease.revision()))
        {
            return an::Status::failure(an::ErrorCode::InvalidArgument,
                                       "a presentation at a reduction needs the display proxy of the image it reads");
        }
        if (an::Status described = describeProxy(*proxy, out); described.failed())
        {
            return described;
        }
    }
    else if (an::Status described = describeLeased(lease, out); described.failed())
    {
        return described;
    }
    const std::uint32_t factor = (std::max)(1u, proxyFactor);
    const std::uint32_t width = out.descriptor->extent().width();
    const std::uint32_t height = out.descriptor->extent().height();
    ViewRegion part{.x = 0u, .y = 0u, .width = width, .height = height};
    if (region.has_value())
    {
        const std::optional<ViewRegion> clipped = regionOfSource(*region, factor, width, height);
        if (!clipped.has_value())
        {
            return an::Status::failure(an::ErrorCode::EmptySelection, "the region is outside the image");
        }
        part = *clipped;
        if (an::Status narrowed = narrowTo(engine, execution, out, part); narrowed.failed())
        {
            return narrowed;
        }
    }
    covered = ViewRegion{
        .x = part.x * factor, .y = part.y * factor, .width = part.width * factor, .height = part.height * factor};
    return an::Status::success();
}

} // namespace

ViewChannel effectiveViewChannel(const an::ImageDescriptor& descriptor, ViewChannel asked) noexcept
{
    if (!isColour(descriptor) || descriptor.isMosaiced())
    {
        return ViewChannel{};
    }
    if (asked.chrominance)
    {
        return ViewChannel{.scope = an::ChannelScope::Whole, .chrominance = true};
    }
    return ViewChannel{.scope = an::isChannelScope(asked.scope) ? asked.scope : an::ChannelScope::Whole};
}

std::string viewChannelKey(ViewChannel channel)
{
    return channel.chrominance ? std::string("chrominance") : std::string(an::channelScopeName(channel.scope));
}

std::string viewPresentationKey(const ViewPresentationSettings& settings)
{
    const an::DisplayPresentationRequest& request = settings.request;
    std::ostringstream text;
    text.precision(17);
    text << "promotion=" << static_cast<int>(request.promotion) << ";neutralize=" << request.neutralize << ';'
         << request.neutralizationBackground.low << ',' << request.neutralizationBackground.high
         << ";calibrate=" << request.calibrate << ';' << request.calibrationBackground.low << ','
         << request.calibrationBackground.high << ';' << request.calibrationWhite.low << ','
         << request.calibrationWhite.high << ";stretch=" << static_cast<int>(request.stretch)
         << ";linked=" << request.linked << ";clip=" << request.shadowsClipping
         << ";target=" << request.targetBackground << ";saturation=" << request.saturation << ','
         << request.saturationBackgroundFactor << ',' << static_cast<int>(request.saturationBackgroundLocation)
         << ";brightness=" << request.brightness << ";contrast=" << request.contrast
         << ";output=" << static_cast<int>(request.output) << ";invalid=" << static_cast<int>(request.invalidPixels)
         << ";channel=" << viewChannelKey(settings.channel) << ";subsample=" << settings.subsampleFactor
         << ";proxy=" << settings.proxyFactor;
    if (settings.region.has_value())
    {
        text << ";region=" << settings.region->x << ',' << settings.region->y << ',' << settings.region->width << ','
             << settings.region->height;
    }
    return text.str();
}

an::Result<ViewPresentationOutcome>
presentForView(const an::EngineContext& engine, const an::ExecutionContext& execution,
               const an::consumer::AssetReadLease& source, const ViewPresentationSettings& settings,
               const an::DisplayPresentationAnalysis* reuse, an::consumer::ConsumerMemory* memory,
               const DisplayProxy* proxy, const WholeMeasurement measurement)
{
    if (!source.valid())
    {
        return an::Status::failure(an::ErrorCode::NotInitialized, "the view's image is under no lease");
    }
    std::optional<an::ImageDescriptor> stored;
    source.visit([&](const auto& image) { stored = image.descriptor(); });
    if (!stored.has_value())
    {
        return an::Status::failure(an::ErrorCode::NotInitialized, "the view's image holds nothing");
    }
    ViewSource shown;
    ViewRegion covered{};
    if (an::Status described =
            describeShown(engine, execution, source, proxy, settings.proxyFactor, settings.region, shown, covered);
        described.failed())
    {
        return described;
    }
    // What the image shows decides the channel: its proxy shows a mosaic as grey, which is how a mosaic is shown.
    const ViewChannel channel = effectiveViewChannel(*stored, settings.channel);
    const bool part = settings.proxyFactor > 1u || settings.region.has_value();

    // A part of the image, or its proxy, is stretched as the whole image is: its statistics are measured on every
    // sample of the image, never on the part, whose statistics are its own, or on the proxy, whose averaging narrows
    // them.
    WholeAnalysis measureWhole;
    if (part && measurement == WholeMeasurement::Refused)
    {
        // A caller that may not measure here - the lens, on the GUI thread - is handed the refusal instead, and never
        // a part measured on itself.
        measureWhole = []() -> an::Result<an::DisplayPresentationAnalysis> {
            return an::Status::failure(an::ErrorCode::IncompatibleArguments,
                                       "the analysis handed was refused, and this presentation measures nothing");
        };
    }
    else if (part)
    {
        measureWhole = [&]() -> an::Result<an::DisplayPresentationAnalysis> {
            ViewSource whole;
            if (an::Status described = describeLeased(source, whole); described.failed())
            {
                return described;
            }
            ChannelSource wholeChannel;
            if (an::Status extracted = channelOf(engine, execution, whole, channel, wholeChannel); extracted.failed())
            {
                return extracted;
            }
            return analyzeChannel(execution, wholeChannel, settings.request);
        };
    }

    ChannelSource presentedChannel;
    if (an::Status extracted = channelOf(engine, execution, shown, channel, presentedChannel); extracted.failed())
    {
        return extracted;
    }
    an::Result<ViewPresentationOutcome> presented =
        presentSource(engine, execution, presentedChannel, settings, reuse, memory, measureWhole);
    if (!presented.hasValue())
    {
        return presented;
    }
    presented->domainRestated = shown.restated;
    presented->channel = channel;
    presented->sourceWidth = stored->extent().width();
    presented->sourceHeight = stored->extent().height();
    if (settings.subsampleFactor > 1u)
    {
        presented->factor = settings.subsampleFactor;
        presented->covered = ViewRegion{.x = 0u,
                                        .y = 0u,
                                        .width = presented->image->width() * settings.subsampleFactor,
                                        .height = presented->image->height() * settings.subsampleFactor};
    }
    else
    {
        presented->factor = (std::max)(1u, settings.proxyFactor);
        presented->covered = covered;
    }
    return presented;
}

an::Result<an::DisplayPresentationAnalysis> analyzeForView(const an::EngineContext& engine,
                                                           const an::ExecutionContext& execution,
                                                           const an::consumer::AssetReadLease& source,
                                                           const ViewPresentationSettings& settings)
{
    ViewSource read;
    if (an::Status described = describeLeased(source, read); described.failed())
    {
        return described;
    }
    // The channel and the request as presentForView reads them, so the analysis is of what a presentation of the
    // channel shown measures - a presentation of one channel refuses one of the whole colour image - and is taken under
    // the plan that presentation runs.
    ChannelSource channel;
    if (an::Status extracted =
            channelOf(engine, execution, read, effectiveViewChannel(*read.descriptor, settings.channel), channel);
        extracted.failed())
    {
        return extracted;
    }
    return analyzeChannel(execution, channel, settings.request);
}

an::Result<std::shared_ptr<const PresentedImage>>
renderViewMaskOverlay(const an::EngineContext& engine, const an::ExecutionContext& execution,
                      const an::consumer::AssetReadLease& mask, const ViewMaskOverlaySettings& settings,
                      const PresentedImage* shown, an::consumer::ConsumerMemory* memory, const DisplayProxy* maskProxy)
{
    ViewSource read;
    ViewRegion covered{};
    if (an::Status described =
            describeShown(engine, execution, mask, maskProxy, settings.proxyFactor, settings.region, read, covered);
        described.failed())
    {
        return described;
    }
    const an::Result<an::MaskOverlayPlan> plan = an::planMaskOverlay(
        an::MaskOverlayRequest{.mode = settings.mode, .tint = settings.tint, .invertMask = settings.invert});
    if (!plan.hasValue())
    {
        return plan.status();
    }

    // Which part of the mask is shown: a stored channel under a view of that channel, the intensity under a multiply
    // of one shown channel, or the whole mask.
    std::optional<an::ChannelScope> part;
    const bool colourMask = isColour(*read.descriptor) && !read.descriptor->isMosaiced();
    const an::ChannelScope shownScope =
        settings.shownChannel.chrominance ? an::ChannelScope::Whole : settings.shownChannel.scope;
    if (colourMask && an::channelScopeEncoding(shownScope) == an::ChannelSchema::Rgb)
    {
        part = shownScope;
    }
    else if (colourMask && settings.mode == an::MaskOverlayMode::Multiply && shown != nullptr &&
             shown->format() == PresentedFormat::Grey8)
    {
        part = an::ChannelScope::HsiIntensity;
    }

    std::optional<WorkingPlane> plane;
    const an::ImageDescriptor* maskDescriptor = &*read.descriptor;
    const void* maskSamples = read.samples;
    std::optional<an::ImageLayout> maskLayout = read.layout;
    an::KernelValidityView maskValidity = read.validity;
    if (part.has_value())
    {
        an::Result<WorkingPlane> extracted = extractPlane(engine, execution, read, *part);
        if (!extracted.hasValue())
        {
            return extracted.status();
        }
        plane.emplace(std::move(*extracted));
        maskDescriptor = &plane->descriptor;
        withPlane(*plane, [&](const void* samples, const std::optional<an::ImageLayout>& layout) {
            maskSamples = samples;
            maskLayout = layout;
            return 0;
        });
        maskValidity = plane->validity;
        if (plane->descriptor.numericDomain() != an::NumericDomain::NormalizedLinear &&
            plane->descriptor.numericDomain() != an::NumericDomain::EncodedNonlinear)
        {
            return an::Status::failure(an::ErrorCode::IncompatibleArguments,
                                       "the mask's channel is not a fraction of full scale");
        }
    }
    if (maskSamples == nullptr || !maskLayout.has_value())
    {
        return an::Status::failure(an::ErrorCode::NotInitialized, "the mask holds nothing");
    }

    std::optional<an::ImageDescriptor> shownDescriptor;
    std::optional<an::MaskOverlaySource> shownSource;
    if (settings.mode == an::MaskOverlayMode::Multiply)
    {
        if (shown == nullptr)
        {
            return an::Status::failure(an::ErrorCode::InvalidArgument, "a multiply needs the image the view shows");
        }
        shownDescriptor = describePresented(*shown, engine.limits());
        if (!shownDescriptor.has_value())
        {
            return an::Status::failure(an::ErrorCode::IncompatibleArguments,
                                       "a multiply needs the view's grey or RGB bytes, not four bytes a pixel");
        }
        shownSource = an::MaskOverlaySource{
            .descriptor = &*shownDescriptor, .samples = shown->data(), .layout = &shown->layout(), .validity = {}};
    }

    const an::Result<an::ImageLayoutRequest> wanted = an::maskOverlayLayout(*plan, *maskDescriptor);
    if (!wanted.hasValue())
    {
        return wanted.status();
    }
    an::Result<std::shared_ptr<PresentedImage>> layer = PresentedImage::allocate(engine, *wanted, memory);
    if (!layer.hasValue())
    {
        return layer.status();
    }
    const an::Result<an::MaskOverlayResult> rendered = an::renderMaskOverlay(
        execution, *plan,
        an::MaskOverlaySource{
            .descriptor = maskDescriptor, .samples = maskSamples, .layout = &*maskLayout, .validity = maskValidity},
        shownSource.has_value() ? &*shownSource : nullptr, (*layer)->data(), (*layer)->layout());
    if (!rendered.hasValue())
    {
        return rendered.status();
    }
    return std::shared_ptr<const PresentedImage>(std::move(*layer));
}

ViewDisplayGeometry viewDisplayGeometry(std::uint32_t width, std::uint32_t height, double scale,
                                        double devicePixelRatio, const std::optional<ViewRegion>& shown)
{
    ViewDisplayGeometry geometry;
    if (width == 0u || height == 0u)
    {
        return geometry;
    }
    const double ratio = std::isfinite(devicePixelRatio) && devicePixelRatio > 0.0 ? devicePixelRatio : 1.0;
    const double deviceScale = scale * ratio;
    if (std::isfinite(deviceScale) && deviceScale > 0.0 && deviceScale < 1.0)
    {
        // The largest whole factor that leaves the proxy no smaller than what is painted; the small allowance keeps a
        // scale of exactly one over n at n despite its rounding.
        const double reduction = std::floor(1.0 / deviceScale + 1.0e-9);
        const double largest = static_cast<double>(largestDisplayProxyFactor(width, height));
        geometry.proxyFactor = static_cast<std::uint32_t>((std::min)((std::max)(reduction, 1.0), largest));
    }
    if (!shown.has_value() || shown->width == 0u || shown->height == 0u)
    {
        return geometry;
    }
    // What is shown, with half its size again each way, aligned outward to whole blocks of the factor and kept to the
    // blocks the proxy holds.
    const std::uint64_t factor = geometry.proxyFactor;
    const std::uint64_t marginX = shown->width / 2u;
    const std::uint64_t marginY = shown->height / 2u;
    const std::uint64_t coveredWidth = (width / factor) * factor;
    const std::uint64_t coveredHeight = (height / factor) * factor;
    const std::uint64_t left = (shown->x > marginX ? shown->x - marginX : 0u) / factor * factor;
    const std::uint64_t top = (shown->y > marginY ? shown->y - marginY : 0u) / factor * factor;
    const std::uint64_t right =
        (std::min)(coveredWidth, (std::uint64_t{shown->x} + shown->width + marginX + factor - 1u) / factor * factor);
    const std::uint64_t bottom =
        (std::min)(coveredHeight, (std::uint64_t{shown->y} + shown->height + marginY + factor - 1u) / factor * factor);
    if (left >= right || top >= bottom || (left == 0u && top == 0u && right >= coveredWidth && bottom >= coveredHeight))
    {
        return geometry;
    }
    geometry.region = ViewRegion{.x = static_cast<std::uint32_t>(left),
                                 .y = static_cast<std::uint32_t>(top),
                                 .width = static_cast<std::uint32_t>(right - left),
                                 .height = static_cast<std::uint32_t>(bottom - top)};
    return geometry;
}

std::optional<PixelReadout> readPixel(const an::consumer::AssetReadLease& image, std::int64_t x,
                                      std::int64_t y) noexcept
{
    if (!image.valid())
    {
        return std::nullopt;
    }
    std::optional<PixelReadout> readout;
    image.visit([&](const auto& typed) {
        using SampleT = typename std::remove_cvref_t<decltype(typed)>::Sample;
        const auto view = typed.view();
        const std::optional<an::ImageDescriptor> descriptor = typed.descriptor();
        if (!view.has_value() || !descriptor.has_value())
        {
            return;
        }
        const std::uint32_t width = view->width();
        const std::uint32_t height = view->height();
        const auto column = static_cast<std::uint32_t>(std::clamp<std::int64_t>(x, 0, std::int64_t{width} - 1));
        const auto row = static_cast<std::uint32_t>(std::clamp<std::int64_t>(y, 0, std::int64_t{height} - 1));
        const std::uint32_t stored = view->channelCount();
        const std::uint32_t colour = descriptor->channelSchema() == an::ChannelSchema::Rgba ? 3u : stored;
        PixelReadout read;
        read.channels = (std::min)(colour >= 3u ? 3u : 1u, stored);
        const bool planar = view->planeCount() == stored;
        for (std::uint32_t channel = 0u; channel < read.channels; ++channel)
        {
            const SampleT* sample =
                planar ? view->sampleAt(channel, column, row, 0u) : view->sampleAt(0u, column, row, channel);
            read.values[channel] = sample != nullptr ? fractionOf(*sample) : 0.0;
        }
        readout = read;
    });
    return readout;
}

PixelReadout readPresentedPixel(const PresentedImage& image, std::int64_t x, std::int64_t y) noexcept
{
    PixelReadout read;
    if (image.width() == 0u || image.height() == 0u)
    {
        return read;
    }
    const auto column = static_cast<std::uint32_t>(std::clamp<std::int64_t>(x, 0, std::int64_t{image.width()} - 1));
    const auto row = static_cast<std::uint32_t>(std::clamp<std::int64_t>(y, 0, std::int64_t{image.height()} - 1));
    const std::uint8_t* pixel = image.pixel(column, row);
    read.channels = image.format() == PresentedFormat::Grey8 ? 1u : 3u;
    for (std::uint32_t channel = 0u; channel < read.channels && pixel != nullptr; ++channel)
    {
        read.values[channel] = static_cast<double>(pixel[byteOfChannel(image.format(), channel)]) / 255.0;
    }
    return read;
}

an::Result<LensHistogram> countPresentedRegion(const an::ExecutionContext& execution, const PresentedImage& image,
                                               std::uint32_t originX, std::uint32_t originY, std::uint32_t width,
                                               std::uint32_t height)
{
    if (originX >= image.width() || originY >= image.height() || width == 0u || height == 0u)
    {
        return an::Status::failure(an::ErrorCode::EmptySelection, "the lens's region is outside the presented image");
    }
    an::StatisticsSelection selection;
    selection.region = an::StatisticsRegion{.originX = originX,
                                            .originY = originY,
                                            .width = (std::min)(width, image.width() - originX),
                                            .height = (std::min)(height, image.height() - originY)};
    LensHistogram histogram;
    histogram.channels = image.format() == PresentedFormat::Grey8 ? 1u : 3u;
    for (std::uint32_t channel = 0u; channel < histogram.channels; ++channel)
    {
        const an::Result<an::ImageHistogram> counted = an::computeImageHistogram(
            execution, an::SampleType::UInt8, image.data(), image.layout(), byteOfChannel(image.format(), channel),
            selection,
            an::ImageHistogramRequest{.domain = an::HistogramDomain::Stated, .lowest = 0.0, .highest = 256.0},
            std::span<std::uint64_t>(histogram.counts[channel]));
        if (!counted.hasValue())
        {
            return counted.status();
        }
    }
    return histogram;
}

double presentationBrightnessOffset(int level) noexcept
{
    return static_cast<double>(level) / 255.0;
}

double presentationContrastFactor(int level) noexcept
{
    const double value = static_cast<double>(level);
    return 259.0 * (value + 255.0) / (255.0 * (259.0 - value));
}

} // namespace an::ui
