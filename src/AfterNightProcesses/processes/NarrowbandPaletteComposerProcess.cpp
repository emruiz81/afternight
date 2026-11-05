// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The Narrowband Palette Composer window: the five source selectors, the palette and the intensities snapshotted
/// on the GUI thread; the sources prepared as aligned unit-fraction planes and one palette composed on the run's
/// worker; and the whole grid prepared and composed the same way on the thread pool, shown when it lands under a
/// generation the next request moves.

#include "stdafx.hpp"
#include "NarrowbandPaletteComposerProcess.h"

#include "controls/AutostretchApply.h"
#include "controls/IAppContext.h"
#include "controls/ImageSelectorTooltipBuilder.h"
#include "controls/ImageViewControl.h"
#include "controls/ProcessButtonIcons.h"
#include "processes/CombinationProcessSupport.h"
#include "processes/NarrowbandPaletteComposerPreviewGridWidget.h"
#include "processes/MaskProcessUtils.h"
#include "processes/PointwiseProcessUtils.h"
#include "processes/ProcessCommonUtils.h"
#include "session/IEngineSessionProvider.h"
#include "session/ImageReads.h"
#include "theme/AnDesignTokens.h"
#include "widgets/waitingspinnerwidget.h"

#include <AfterNightConsumerRuntime/EngineSession.h>
#include <AfterNightConsumerRuntime/OperationScope.h>

#include <AfterNightEngine/Algorithms/Geometry/Geometry.h>
#include <AfterNightEngine/Algorithms/Pointwise/Arithmetic.h>
#include <AfterNightEngine/Algorithms/Pointwise/PointwiseExecution.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/ImageExtent.h>
#include <AfterNightEngine/Image/ChannelTransfer.h>
#include <AfterNightEngine/Image/ConversionPolicy.h>
#include <AfterNightEngine/Image/Image.h>
#include <AfterNightEngine/Image/ImageConversion.h>
#include <AfterNightEngine/Image/ValidityMask.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <AfterNightPlatform/Logging.h>

#include <QApplication>
#include <QBoxLayout>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QImage>
#include <QPointer>
#include <QSignalBlocker>
#include <QSplitter>
#include <QThreadPool>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <utility>
#include <vector>

namespace
{

[[nodiscard]] an::Status refusal(an::ErrorCode code, std::string message)
{
    return an::Status::failure(code, std::move(message));
}

/// Runs a function on the GUI thread through the event loop, never waited on.
void postToGui(std::function<void()> function)
{
    if (QCoreApplication* application = QCoreApplication::instance())
    {
        QMetaObject::invokeMethod(application, std::move(function), Qt::QueuedConnection);
    }
}

void addNoImageItem(QComboBox* selector)
{
    if (selector)
    {
        selector->addItem(NO_IMAGE_STR);
    }
}

void addImageItem(QComboBox* selector, const ImageViewControl* control)
{
    if (!selector || !control)
    {
        return;
    }
    selector->addItem(QString::fromUtf8(control->getUniqueName()));
    selector->setItemData(selector->count() - 1, an::ui::buildImageSelectorTooltip(control), Qt::ToolTipRole);
}

void restoreSelection(QComboBox* selector, const QString& previous)
{
    if (!selector)
    {
        return;
    }
    const QString selected = previous.isEmpty() ? NO_IMAGE_STR : previous;
    const int index = selector->findText(selected);
    selector->setCurrentText(index >= 0 ? selected : NO_IMAGE_STR);
}

[[nodiscard]] bool supportedComposerView(const ImageViewControl* view)
{
    if (view == nullptr || view->isSequence() || !view->hasImage())
    {
        return false;
    }
    const std::optional<an::ImageDescriptor> described = view->document().asset()->descriptor();
    return described.has_value() &&
           (described->extent().channelCount() == 1u || process_utils::describesColourImage(*described));
}

[[nodiscard]] std::string outputNameFor(const NarrowbandPaletteComposerProcessParams& params)
{
    const std::string palette = an::narrowbandPaletteName(params.palette);
    if (!params.oiii.name.empty())
    {
        return params.oiii.name + " - " + palette + " Narrowband Palette";
    }
    if (!params.ha.name.empty())
    {
        return params.ha.name + " - " + palette + " Narrowband Palette";
    }
    if (!params.sii.name.empty())
    {
        return params.sii.name + " - " + palette + " Narrowband Palette";
    }
    return palette + " Narrowband Palette";
}

[[nodiscard]] QSplitter* findMainSplitter(QBoxLayout* layout)
{
    if (!layout)
    {
        return nullptr;
    }
    for (int i = 0; i < layout->count(); ++i)
    {
        if (auto* item = layout->itemAt(i))
        {
            if (auto* widget = item->widget())
            {
                if (auto* splitter = qobject_cast<QSplitter*>(widget))
                {
                    return splitter;
                }
                if (auto* nested = widget->findChild<QSplitter*>(QStringLiteral("mainSplitter")))
                {
                    return nested;
                }
            }
        }
    }
    if (auto* host = layout->parentWidget())
    {
        return host->findChild<QSplitter*>(QStringLiteral("mainSplitter"));
    }
    return nullptr;
}

void restoreCustomPreviewPane(QSplitter* splitter)
{
    if (!splitter || splitter->count() < 2)
    {
        return;
    }
    splitter->setChildrenCollapsible(false);
    splitter->setCollapsible(1, false);
    if (auto* previewPane = splitter->widget(1))
    {
        previewPane->setMinimumWidth(an::ui::processes::kNarrowbandPaletteComposerMinimumPreviewPaneWidth);
        previewPane->setMaximumWidth(QWIDGETSIZE_MAX);
        previewPane->setVisible(true);
    }
    if (auto* handle = splitter->handle(1))
    {
        handle->setVisible(true);
    }
    const QList<int> sizes = splitter->sizes();
    if (sizes.size() < 2 || sizes[1] <= 0)
    {
        splitter->setSizes({an::ui::processes::kNarrowbandPaletteComposerInitialControlsPaneWidth,
                            an::ui::processes::kNarrowbandPaletteComposerInitialPreviewPaneWidth});
    }
}

// ---- The preparation of the sources, on a worker under an admitted execution.

/// One prepared plane: a Float32 grey image of the run's session, in the unit interval, and its lease.
struct PreparedPlane
{
    an::consumer::ImageAsset image;
    an::consumer::AssetReadLease lease;
    std::unique_ptr<process_utils::BoundImage> bound;
    const an::ImageMetadata* metadata = nullptr;
};

[[nodiscard]] an::Result<std::unique_ptr<PreparedPlane>>
leasePlane(const an::ui::ExecutionScope& scope, an::consumer::ImageAsset&& image, const an::ImageMetadata* metadata)
{
    an::Result<an::consumer::AssetReadLease> lease = image.read(scope.execution().stopToken());
    if (!lease.hasValue())
    {
        return lease.status();
    }
    auto plane = std::make_unique<PreparedPlane>(
        PreparedPlane{.image = std::move(image), .lease = std::move(*lease), .bound = nullptr, .metadata = metadata});
    an::Result<std::unique_ptr<process_utils::BoundImage>> bound =
        process_utils::BoundImage::bind(an::ui::ProcessSource::of(plane->lease));
    if (!bound.hasValue())
    {
        return bound.status();
    }
    plane->bound = std::move(*bound);
    return plane;
}

/// A Float32 grey image described like `reference` at another size, every sample zero.
[[nodiscard]] an::Result<an::consumer::ImageAsset> emptyPlane(const an::ui::ExecutionScope& scope,
                                                              const an::ImageDescriptor& reference, std::uint32_t width,
                                                              std::uint32_t height, std::uint32_t channels,
                                                              an::ImageMetadata metadata)
{
    const std::optional<an::ImageExtent> extent =
        an::ImageExtent::create(width, height, channels, scope.engine().limits());
    if (!extent.has_value())
    {
        return refusal(an::ErrorCode::InvalidArgument, "the plane's size cannot be held");
    }
    an::ImageDescriptorRequest request = reference.toRequest();
    request.extent = *extent;
    request.sampleType = an::SampleType::Float32;
    request.channelSchema = channels == 1u ? an::ChannelSchema::Mono : an::ChannelSchema::Rgb;
    request.colorSpace = an::ColorSpace::Unspecified;
    request.numericDomain = an::NumericDomain::NormalizedLinear;
    request.cfa = an::CfaDescriptor{};
    request.pixelAspectRatio = 1.0;
    const std::optional<an::ImageDescriptor> described = an::ImageDescriptor::create(request);
    if (!described.has_value())
    {
        return refusal(an::ErrorCode::InvalidArgument, "the plane cannot be described");
    }
    an::Result<an::Image<float>> created =
        an::Image<float>::create(scope.engine(), *described, an::StorageArrangement::Planar,
                                 an::StorageInitialization::Zeroed, std::move(metadata));
    if (!created.hasValue())
    {
        return created.status();
    }
    return an::consumer::ImageAsset::adopt(scope.session(), an::consumer::AnyImage(std::move(*created)));
}

/// One channel of a bound image as a Float32 grey image of its own, held to the unit interval.
[[nodiscard]] an::Result<an::consumer::ImageAsset>
channelPlane(const an::ui::ExecutionScope& scope, const process_utils::BoundImage& image, std::uint32_t channel)
{
    an::Result<an::consumer::ImageAsset> plane =
        emptyPlane(scope, image.descriptor(), image.descriptor().extent().width(), image.descriptor().extent().height(),
                   1u, image.metadata());
    if (!plane.hasValue())
    {
        return plane.status();
    }
    an::Result<an::consumer::AssetWriteLease> lease = plane->write(scope.execution().stopToken());
    if (!lease.hasValue())
    {
        return lease.status();
    }
    std::optional<an::ImageWriteAccess<float>> access = lease->write<float>();
    if (!access.has_value())
    {
        return refusal(an::ErrorCode::NotInitialized, "the plane holds nothing");
    }
    if (an::Status extracted =
            an::extractImageChannel(scope.execution(), an::SampleType::Float32, image.samples(), image.layout(),
                                    channel, access->view().data(), access->view().layout());
        extracted.failed())
    {
        return extracted;
    }
    const an::ImageLayout layout = access->view().layout();
    if (an::Status held = process_utils::holdToUnitInterval(scope.engine(), scope.execution(), an::SampleType::Float32,
                                                            access->view().data(), layout);
        held.failed())
    {
        return held;
    }
    access.reset();
    lease->release();
    return plane;
}

/// The equal-weight mean of a colour image's green and blue channels as a Float32 grey image, held to the unit
/// interval: the one-shot-colour estimate of OIII.
[[nodiscard]] an::Result<an::consumer::ImageAsset> greenBlueMean(const an::ui::ExecutionScope& scope,
                                                                 const process_utils::BoundImage& image)
{
    an::Result<an::consumer::ImageAsset> green = channelPlane(scope, image, 1u);
    if (!green.hasValue())
    {
        return green.status();
    }
    an::Result<an::consumer::ImageAsset> blue = channelPlane(scope, image, 2u);
    if (!blue.hasValue())
    {
        return blue.status();
    }
    an::Result<an::consumer::AssetReadLease> blueLease = blue->read(scope.execution().stopToken());
    if (!blueLease.hasValue())
    {
        return blueLease.status();
    }
    an::Result<std::unique_ptr<process_utils::BoundImage>> blueBound =
        process_utils::BoundImage::bind(an::ui::ProcessSource::of(*blueLease));
    if (!blueBound.hasValue())
    {
        return blueBound.status();
    }
    an::Result<an::consumer::AssetWriteLease> lease = green->write(scope.execution().stopToken());
    if (!lease.hasValue())
    {
        return lease.status();
    }
    std::optional<an::ImageWriteAccess<float>> access = lease->write<float>();
    if (!access.has_value())
    {
        return refusal(an::ErrorCode::NotInitialized, "the plane holds nothing");
    }
    const an::Result<an::PointwiseProgram> program =
        an::buildBlendProgram(process_utils::normalizedPlanShape(an::SampleType::Float32, 1u), 0.5);
    if (!program.hasValue())
    {
        return program.status();
    }
    const an::ImageLayout layout = access->view().layout();
    const std::array<an::PointwiseImageView, 2u> sources{
        an::PointwiseImageView{.samples = access->view().data(), .layout = &layout},
        an::PointwiseImageView{.samples = (*blueBound)->samples(), .layout = &(*blueBound)->layout()}};
    const an::Result<an::PointwiseExecutionResult> ran = an::executePointwiseProgram(
        scope.execution(), *program, sources,
        an::PointwiseMutableImageView{.samples = access->view().data(), .layout = &layout}, an::KernelValidityView{});
    if (!ran.hasValue())
    {
        return ran.status();
    }
    access.reset();
    lease->release();
    return green;
}

/// Half of one plane and half of another, into the first: an existing line blended with its one-shot-colour
/// estimate, resized to the existing plane's size when their sizes differ.
[[nodiscard]] an::Result<an::consumer::ImageAsset> blendHalf(const an::ui::ExecutionScope& scope,
                                                             an::consumer::ImageAsset&& existing,
                                                             an::consumer::ImageAsset&& synthetic,
                                                             std::vector<std::string>& warnings, const char* channel,
                                                             bool resizeMismatched);

/// A Float32 grey plane resized to a width and height through the engine's Lanczos interpolation.
[[nodiscard]] an::Result<an::consumer::ImageAsset> resizedPlane(const an::ui::ExecutionScope& scope,
                                                                const an::consumer::ImageAsset& plane,
                                                                std::uint32_t width, std::uint32_t height)
{
    an::Result<an::consumer::AssetReadLease> lease = plane.read(scope.execution().stopToken());
    if (!lease.hasValue())
    {
        return lease.status();
    }
    const an::Image<float>* typed = std::get_if<an::Image<float>>(&lease->image());
    if (typed == nullptr)
    {
        return refusal(an::ErrorCode::IncompatibleArguments, "a prepared plane is Float32");
    }
    const std::optional<an::ImageDescriptor> stored = typed->descriptor();
    const std::optional<an::ImageView<const float>> view = typed->view();
    if (!stored.has_value() || !view.has_value())
    {
        return refusal(an::ErrorCode::NotInitialized, "the plane holds nothing");
    }
    if (stored->extent().width() == width && stored->extent().height() == height)
    {
        an::Result<an::consumer::ImageAsset> same = plane.snapshot(scope.execution().stopToken());
        return same;
    }
    an::ResizeRequest request;
    request.width = width;
    request.height = height;
    request.sampling.interpolation = an::KernelInterpolation::Lanczos4;
    const an::Result<an::GeometryPlan> geometry = an::planResize(*stored, request);
    if (!geometry.hasValue())
    {
        return geometry.status();
    }
    an::Result<an::consumer::ImageAsset> resized = emptyPlane(scope, *stored, width, height, 1u, typed->metadata());
    if (!resized.hasValue())
    {
        return resized.status();
    }
    an::Result<an::ValidityMask> written =
        an::ValidityMask::create(scope.engine(), geometry->destinationDescriptor().extent());
    if (!written.hasValue())
    {
        return written.status();
    }
    std::optional<an::ValidityMaskView<std::uint8_t>> writtenView = written->view();
    if (!writtenView.has_value())
    {
        return refusal(an::ErrorCode::NotInitialized, "the resized plane's mask holds nothing");
    }
    an::GeometrySelection selection;
    an::Result<an::consumer::AssetWriteLease> writeLease = resized->write(scope.execution().stopToken());
    if (!writeLease.hasValue())
    {
        return writeLease.status();
    }
    {
        std::optional<an::ImageWriteAccess<float>> access = writeLease->write<float>();
        if (!access.has_value())
        {
            return refusal(an::ErrorCode::NotInitialized, "the resized plane holds nothing");
        }
        const an::Result<an::GeometryResult> ran = an::transformImageGeometry(
            scope.execution(), an::SampleType::Float32, view->data(), view->layout(), access->view().data(),
            access->view().layout(), *geometry, selection, *writtenView);
        if (!ran.hasValue())
        {
            return ran.status();
        }
        const an::ImageLayout layout = access->view().layout();
        if (an::Status held = process_utils::holdToUnitInterval(scope.engine(), scope.execution(),
                                                                an::SampleType::Float32, access->view().data(), layout);
            held.failed())
        {
            return held;
        }
    }
    writeLease->release();
    return resized;
}

an::Result<an::consumer::ImageAsset> blendHalf(const an::ui::ExecutionScope& scope, an::consumer::ImageAsset&& existing,
                                               an::consumer::ImageAsset&& synthetic, std::vector<std::string>& warnings,
                                               const char* channel, bool resizeMismatched)
{
    const std::optional<an::ImageDescriptor> existingDescribed = existing.descriptor();
    const std::optional<an::ImageDescriptor> syntheticDescribed = synthetic.descriptor();
    if (!existingDescribed.has_value() || !syntheticDescribed.has_value())
    {
        return refusal(an::ErrorCode::NotInitialized, "a plane holds nothing");
    }
    an::consumer::ImageAsset matched = std::move(synthetic);
    if (!process_utils::sameSize(*existingDescribed, *syntheticDescribed))
    {
        if (!resizeMismatched)
        {
            return refusal(an::ErrorCode::IncompatibleArguments,
                           std::string("the synthetic ") + channel +
                               " contribution does not have the selected channel's size, and resizing is off");
        }
        an::Result<an::consumer::ImageAsset> resized =
            resizedPlane(scope, matched, existingDescribed->extent().width(), existingDescribed->extent().height());
        if (!resized.hasValue())
        {
            return resized.status();
        }
        matched = std::move(*resized);
        warnings.push_back(std::string("Resized synthetic ") + channel +
                           " contribution before blending it with the selected channel.");
    }
    an::Result<an::consumer::AssetReadLease> matchedLease = matched.read(scope.execution().stopToken());
    if (!matchedLease.hasValue())
    {
        return matchedLease.status();
    }
    an::Result<std::unique_ptr<process_utils::BoundImage>> matchedBound =
        process_utils::BoundImage::bind(an::ui::ProcessSource::of(*matchedLease));
    if (!matchedBound.hasValue())
    {
        return matchedBound.status();
    }
    an::Result<an::consumer::AssetWriteLease> lease = existing.write(scope.execution().stopToken());
    if (!lease.hasValue())
    {
        return lease.status();
    }
    std::optional<an::ImageWriteAccess<float>> access = lease->write<float>();
    if (!access.has_value())
    {
        return refusal(an::ErrorCode::NotInitialized, "the plane holds nothing");
    }
    const an::Result<an::PointwiseProgram> program =
        an::buildBlendProgram(process_utils::normalizedPlanShape(an::SampleType::Float32, 1u), 0.5);
    if (!program.hasValue())
    {
        return program.status();
    }
    const an::ImageLayout layout = access->view().layout();
    const std::array<an::PointwiseImageView, 2u> sources{
        an::PointwiseImageView{.samples = access->view().data(), .layout = &layout},
        an::PointwiseImageView{.samples = (*matchedBound)->samples(), .layout = &(*matchedBound)->layout()}};
    const an::Result<an::PointwiseExecutionResult> ran = an::executePointwiseProgram(
        scope.execution(), *program, sources,
        an::PointwiseMutableImageView{.samples = access->view().data(), .layout = &layout}, an::KernelValidityView{});
    if (!ran.hasValue())
    {
        return ran.status();
    }
    access.reset();
    lease->release();
    return std::move(existing);
}

/// A source read as a Float32 unit-fraction image of its own channels, stretched when asked.
[[nodiscard]] an::Result<an::consumer::ImageAsset> readSource(const an::ui::ExecutionScope& scope,
                                                              const an::consumer::ImageAsset& image, bool stretch)
{
    an::Result<an::consumer::AssetReadLease> lease = an::ui::readBeside(scope, image, scope.execution().stopToken());
    if (!lease.hasValue())
    {
        return lease.status();
    }
    an::Result<an::consumer::ImageAsset> unit =
        process_utils::unitFloatCopy(scope, an::ui::ProcessSource::of(*lease), an::SampleType::Float32);
    if (!unit.hasValue() || !stretch)
    {
        return unit;
    }
    // The stretch is the automatic screen transfer at a background of 0.25 and a shadows clipping of -2.8, each
    // channel by its own statistics, over the unit copy.
    an::Result<an::consumer::AssetReadLease> unitLease = unit->read(scope.execution().stopToken());
    if (!unitLease.hasValue())
    {
        return unitLease.status();
    }
    an::ui::AutostretchApplySettings settings;
    settings.targetBackground = 0.25;
    settings.shadowsClipping = -2.8;
    settings.linkedChannels = false;
    an::Result<an::consumer::AnyImage> stretched = an::ui::autostretchFrame(
        scope.engine(), scope.execution(), unitLease->image(), unitLease->validity(), settings);
    if (!stretched.hasValue())
    {
        return stretched.status();
    }
    unitLease->release();
    return an::consumer::ImageAsset::adopt(scope.session(), std::move(*stretched));
}

/// A narrowband source as one grey plane: a grey image as it is, a colour one's red channel.
[[nodiscard]] an::Result<an::consumer::ImageAsset> narrowbandPlane(const an::ui::ExecutionScope& scope,
                                                                   const an::consumer::ImageAsset& unit,
                                                                   const char* role, std::vector<std::string>& warnings)
{
    const std::optional<an::ImageDescriptor> described = unit.descriptor();
    if (!described.has_value())
    {
        return refusal(an::ErrorCode::NotInitialized, "a source holds nothing");
    }
    if (described->extent().channelCount() == 1u)
    {
        return unit.snapshot(scope.execution().stopToken());
    }
    warnings.push_back(std::string(role) + " source is RGB; using the red channel to match SetiAstro SASPro.");
    an::Result<an::consumer::AssetReadLease> lease = unit.read(scope.execution().stopToken());
    if (!lease.hasValue())
    {
        return lease.status();
    }
    an::Result<std::unique_ptr<process_utils::BoundImage>> bound =
        process_utils::BoundImage::bind(an::ui::ProcessSource::of(*lease));
    if (!bound.hasValue())
    {
        return bound.status();
    }
    return channelPlane(scope, **bound, 0u);
}

/// The three aligned planes a palette reads, prepared from the sources.
struct PreparedChannels
{
    std::unique_ptr<PreparedPlane> ha;
    std::unique_ptr<PreparedPlane> oiii;
    std::unique_ptr<PreparedPlane> sii;
    std::vector<std::string> warnings;
    std::vector<const an::ImageMetadata*> sources;
};

[[nodiscard]] an::Result<PreparedChannels> prepareChannels(const an::ui::ExecutionScope& scope,
                                                           const NarrowbandPaletteComposerProcessParams& params)
{
    PreparedChannels prepared;
    std::optional<an::consumer::ImageAsset> ha;
    std::optional<an::consumer::ImageAsset> oiii;
    std::optional<an::consumer::ImageAsset> sii;
    const auto readNarrowband = [&](const NarrowbandPaletteSource& source, const char* role,
                                    std::optional<an::consumer::ImageAsset>& into) -> an::Status {
        if (!source.image)
        {
            return an::Status::success();
        }
        an::Result<an::consumer::ImageAsset> unit = readSource(scope, *source.image, params.stretchInputs);
        if (!unit.hasValue())
        {
            return unit.status();
        }
        an::Result<an::consumer::ImageAsset> plane = narrowbandPlane(scope, *unit, role, prepared.warnings);
        if (!plane.hasValue())
        {
            return plane.status();
        }
        into.emplace(std::move(*plane));
        return an::Status::success();
    };
    if (an::Status read = readNarrowband(params.ha, "Ha", ha); read.failed())
    {
        return read;
    }
    if (an::Status read = readNarrowband(params.oiii, "OIII", oiii); read.failed())
    {
        return read;
    }
    if (an::Status read = readNarrowband(params.sii, "SII", sii); read.failed())
    {
        return read;
    }

    // A one-shot-colour image synthesizes a line from its red channel and OIII from its green and blue.
    const auto readOsc = [&](const NarrowbandPaletteSource& source, const char* role, const char* lineName,
                             std::optional<an::consumer::ImageAsset>& line) -> an::Status {
        if (!source.image)
        {
            return an::Status::success();
        }
        an::Result<an::consumer::ImageAsset> unit = readSource(scope, *source.image, params.stretchInputs);
        if (!unit.hasValue())
        {
            return unit.status();
        }
        const std::optional<an::ImageDescriptor> described = unit->descriptor();
        if (!described.has_value())
        {
            return refusal(an::ErrorCode::NotInitialized, "a source holds nothing");
        }
        an::Result<an::consumer::AssetReadLease> lease = unit->read(scope.execution().stopToken());
        if (!lease.hasValue())
        {
            return lease.status();
        }
        an::Result<std::unique_ptr<process_utils::BoundImage>> bound =
            process_utils::BoundImage::bind(an::ui::ProcessSource::of(*lease));
        if (!bound.hasValue())
        {
            return bound.status();
        }
        an::Result<an::consumer::ImageAsset> red = channelPlane(scope, **bound, 0u);
        if (!red.hasValue())
        {
            return red.status();
        }
        an::Result<an::consumer::ImageAsset> mean =
            described->extent().channelCount() == 1u ? channelPlane(scope, **bound, 0u) : greenBlueMean(scope, **bound);
        if (!mean.hasValue())
        {
            return mean.status();
        }
        if (described->extent().channelCount() == 1u)
        {
            prepared.warnings.push_back(std::string(role) +
                                        " source is mono; broadcasting it to RGB before OSC synthesis.");
        }
        if (line.has_value())
        {
            an::Result<an::consumer::ImageAsset> blended = blendHalf(
                scope, std::move(*line), std::move(*red), prepared.warnings, lineName, params.resizeMismatched);
            if (!blended.hasValue())
            {
                return blended.status();
            }
            line.emplace(std::move(*blended));
        }
        else
        {
            line.emplace(std::move(*red));
        }
        if (oiii.has_value())
        {
            an::Result<an::consumer::ImageAsset> blended = blendHalf(
                scope, std::move(*oiii), std::move(*mean), prepared.warnings, "OIII", params.resizeMismatched);
            if (!blended.hasValue())
            {
                return blended.status();
            }
            oiii.emplace(std::move(*blended));
        }
        else
        {
            oiii.emplace(std::move(*mean));
        }
        return an::Status::success();
    };
    if (an::Status read = readOsc(params.osc1, "OSC1", "Ha", ha); read.failed())
    {
        return read;
    }
    if (an::Status read = readOsc(params.osc2, "OSC2", "SII", sii); read.failed())
    {
        return read;
    }
    if (!oiii.has_value() || (!ha.has_value() && !sii.has_value()))
    {
        return refusal(an::ErrorCode::InvalidArgument,
                       "Narrowband Palette Composer requires OIII plus Ha or SII after input synthesis.");
    }

    // Every plane at the reference's size: H-alpha's, else OIII's, else SII's.
    const an::consumer::ImageAsset& reference = ha.has_value() ? *ha : (oiii.has_value() ? *oiii : *sii);
    const std::optional<an::ImageDescriptor> referenceDescribed = reference.descriptor();
    if (!referenceDescribed.has_value())
    {
        return refusal(an::ErrorCode::NotInitialized, "the reference plane holds nothing");
    }
    const std::uint32_t width = referenceDescribed->extent().width();
    const std::uint32_t height = referenceDescribed->extent().height();
    bool resized = false;
    for (std::optional<an::consumer::ImageAsset>* plane : {&ha, &oiii, &sii})
    {
        if (!plane->has_value())
        {
            continue;
        }
        const std::optional<an::ImageDescriptor> described = (*plane)->descriptor();
        if (!described.has_value() || !process_utils::sameSize(*described, *referenceDescribed))
        {
            if (!params.resizeMismatched)
            {
                return refusal(an::ErrorCode::IncompatibleArguments,
                               "Narrowband Palette Composer channel dimensions do not match and resize-to-reference "
                               "is disabled.");
            }
            an::Result<an::consumer::ImageAsset> fitted = resizedPlane(scope, **plane, width, height);
            if (!fitted.hasValue())
            {
                return fitted.status();
            }
            plane->emplace(std::move(*fitted));
            resized = true;
        }
    }
    if (resized)
    {
        prepared.warnings.push_back(std::string("Resized mismatched channels to ") +
                                    (ha.has_value() ? "Ha" : (oiii.has_value() ? "OIII" : "SII")) +
                                    " reference dimensions.");
    }
    const auto lease = [&](std::optional<an::consumer::ImageAsset>& plane, const an::consumer::ImageAsset* original,
                           std::unique_ptr<PreparedPlane>& into) -> an::Status {
        if (!plane.has_value())
        {
            return an::Status::success();
        }
        an::Result<std::unique_ptr<PreparedPlane>> leased = leasePlane(scope, std::move(*plane), nullptr);
        if (!leased.hasValue())
        {
            return leased.status();
        }
        into = std::move(*leased);
        (void)original;
        return an::Status::success();
    };
    if (an::Status held = lease(ha, nullptr, prepared.ha); held.failed())
    {
        return held;
    }
    if (an::Status held = lease(oiii, nullptr, prepared.oiii); held.failed())
    {
        return held;
    }
    if (an::Status held = lease(sii, nullptr, prepared.sii); held.failed())
    {
        return held;
    }
    return prepared;
}

/// The sources' metadata in the order the result derives from them: OIII first when there is no H-alpha, the
/// reference's grid being the first source's.
[[nodiscard]] std::vector<std::shared_ptr<const an::consumer::ImageAsset>>
sourceAssets(const NarrowbandPaletteComposerProcessParams& params)
{
    std::vector<std::shared_ptr<const an::consumer::ImageAsset>> assets;
    for (const NarrowbandPaletteSource* source : {&params.ha, &params.oiii, &params.sii, &params.osc1, &params.osc2})
    {
        if (source->image)
        {
            assets.push_back(source->image);
        }
    }
    return assets;
}

[[nodiscard]] an::NarrowbandPaletteChannels channelsOf(const PreparedChannels& prepared)
{
    an::NarrowbandPaletteChannels channels;
    if (prepared.ha)
    {
        channels.hydrogenAlpha = {.samples = prepared.ha->bound->samples(), .layout = &prepared.ha->bound->layout()};
    }
    if (prepared.oiii)
    {
        channels.oxygenIII = {.samples = prepared.oiii->bound->samples(), .layout = &prepared.oiii->bound->layout()};
    }
    if (prepared.sii)
    {
        channels.sulfurII = {.samples = prepared.sii->bound->samples(), .layout = &prepared.sii->bound->layout()};
    }
    return channels;
}

[[nodiscard]] const PreparedPlane& referenceOf(const PreparedChannels& prepared)
{
    return prepared.oiii ? *prepared.oiii : (prepared.ha ? *prepared.ha : *prepared.sii);
}

/// A Float32 colour image's samples as an eight-bit picture, through the engine's conversion and repack.
[[nodiscard]] an::Result<QImage> pictureOf(const an::ui::ExecutionScope& scope, const an::ImageDescriptor& described,
                                           const void* samples, const an::ImageLayout& layout)
{
    const std::uint32_t width = described.extent().width();
    const std::uint32_t height = described.extent().height();
    const an::Result<an::ImageDescriptor> bytes = an::describeSamplesAs(described, an::SampleType::UInt8);
    if (!bytes.hasValue())
    {
        return bytes.status();
    }
    an::Result<an::Image<std::uint8_t>> planar =
        an::Image<std::uint8_t>::create(scope.engine(), *bytes, an::StorageArrangement::Planar,
                                        an::StorageInitialization::Uninitialized, an::ImageMetadata{});
    if (!planar.hasValue())
    {
        return planar.status();
    }
    std::optional<an::ImageWriteAccess<std::uint8_t>> access = planar->write();
    if (!access.has_value())
    {
        return refusal(an::ErrorCode::NotInitialized, "the picture holds nothing");
    }
    an::ConversionCounters counters;
    if (an::Status converted =
            an::convertImageSamples(scope.execution(), an::SampleType::Float32, samples, layout, an::SampleType::UInt8,
                                    access->view().data(), access->view().layout(), an::ConversionPolicy{}, counters);
        converted.failed())
    {
        return converted;
    }
    const std::optional<an::ImageLayout> interleaved =
        an::ImageLayout::create(an::ImageLayoutRequest{.extent = described.extent(),
                                                       .elementSizeBytes = 1u,
                                                       .samplesPerPixel = 3u,
                                                       .planeCount = 1u,
                                                       .rowAlignmentBytes = 1u},
                                scope.engine().limits());
    if (!interleaved.has_value())
    {
        return refusal(an::ErrorCode::InvalidArgument, "the picture cannot be laid out");
    }
    std::vector<std::uint8_t> packed(static_cast<std::size_t>(interleaved->totalBytes()));
    if (an::Status repacked = an::repackImageSamples(scope.execution(), an::SampleType::UInt8, access->view().data(),
                                                     access->view().layout(), packed.data(), *interleaved);
        repacked.failed())
    {
        return repacked;
    }
    QImage picture(packed.data(), static_cast<int>(width), static_cast<int>(height),
                   static_cast<qsizetype>(interleaved->rowStrideBytes()), QImage::Format_RGB888);
    return picture.copy();
}

} // namespace

namespace an::ui::processes::narrowband_palette_detail
{

bool hasRequiredInputs(const NarrowbandPaletteComposerProcessParams& params)
{
    const bool hasOiii = params.oiii.image || params.osc1.image || params.osc2.image;
    const bool hasHaOrSii = params.ha.image || params.sii.image || params.osc1.image || params.osc2.image;
    return hasOiii && hasHaOrSii;
}

} // namespace an::ui::processes::narrowband_palette_detail

/// The grid on its way: the request that started it, the latest asked for, and the generation a landing is checked
/// against.
struct NarrowbandPaletteComposerProcess::Grid
{
    std::atomic<std::uint64_t> generation{0u};
    std::uint64_t shown = 0u;
    bool building = false;
    bool pending = false;
    std::optional<NarrowbandPaletteComposerProcessParams> next;
};

NarrowbandPaletteComposerProcess::NarrowbandPaletteComposerProcess(QWidget* parent)
    : an::ui::TypedProcessControl<NarrowbandPaletteComposerProcessParams>(nullptr, getProcessNameStatic()),
      m_grid(std::make_shared<Grid>())
{
    Q_UNUSED(parent);

    m_ui = an::ui::processes::buildNarrowbandPaletteComposerProcessUi(this);
    setProcessDescription(QStringLiteral(
        "Build SetiAstro-style narrowband palettes from open Ha, OIII, SII, and optional OSC image views."));

    m_SupportTargetViewControlSelection = false;
    m_SupportPerChannelProcessing = false;
    m_FixedLayout = false;
    m_ShowWaitingSpinner = false;
    m_CreatedViewControlsAreNeighbour = true;
    m_SupportMasking = false;
    m_ImagePreviewLayout = m_ui.previewLayout;
    m_MainHorizontalLayout = m_ui.horizontalLayoutMain;

    initializeStandardProcess(m_ui.horizontalLayoutMain, m_ui.previewLayout, nullptr, m_ui.buttonBox, nullptr, nullptr,
                              ImageRTPreviewControl::ProcessInitOptions{.previewAreaSelectionEnabled = false});
    setExecutionAutostretchSupported(true);
    m_DefaultSizePreview = QSize(an::ui::processes::kNarrowbandPaletteComposerInitialWindowWidth,
                                 an::ui::processes::kNarrowbandPaletteComposerInitialWindowHeight);
    m_DefaultSizeNoPreview = m_DefaultSizePreview;

    refreshPaletteGridItems();
    connectUi();
    rebuildImageSelectors();
    resetSettings();
    validateSelections();
    initializeTopLevelProcess();
    refreshTheme();
}

NarrowbandPaletteComposerProcess::~NarrowbandPaletteComposerProcess()
{
    m_grid->generation.fetch_add(1u);
}

std::string NarrowbandPaletteComposerProcess::getProcessNameStatic()
{
    return "Narrowband Palette Composer";
}

bool NarrowbandPaletteComposerProcess::requiresExclusiveAccessStatic()
{
    return false;
}

bool NarrowbandPaletteComposerProcess::isGridBuilding() const noexcept
{
    return m_grid->building || m_grid->pending;
}

std::uint64_t NarrowbandPaletteComposerProcess::gridsShown() const noexcept
{
    return m_grid->shown;
}

bool NarrowbandPaletteComposerProcess::hasGrid() const
{
    return m_ui.paletteGrid != nullptr && m_ui.paletteGrid->hasPreviewImages();
}

void NarrowbandPaletteComposerProcess::connectUi()
{
    const std::array<QComboBox*, 5> selectors = {m_ui.haSourceSelector, m_ui.oiiiSourceSelector, m_ui.siiSourceSelector,
                                                 m_ui.osc1SourceSelector, m_ui.osc2SourceSelector};
    for (auto* selector : selectors)
    {
        QObject::connect(selector, &QComboBox::currentTextChanged, this, [this]() {
            if (!m_rebuildingSelectors)
            {
                handleSettingsChanged();
            }
        });
    }
    QObject::connect(m_ui.linearInputStretch, &QCheckBox::checkStateChanged, this,
                     [this]() { handleSettingsChanged(); });
    QObject::connect(m_ui.resizeMismatched, &QCheckBox::checkStateChanged, this, [this]() { handleSettingsChanged(); });
    QObject::connect(m_ui.previewModeSelector, qOverload<int>(&QComboBox::currentIndexChanged), this,
                     [this](const int index) { handlePreviewModeChanged(index); });
    m_ui.paletteGrid->setSelectionChangedHandler(
        [this](const an::NarrowbandPalette palette) { handlePaletteSelectionChanged(palette); });
    m_ui.singlePreview->setSelectionChangedHandler(
        [this](const an::NarrowbandPalette palette) { handlePaletteSelectionChanged(palette); });
    syncIntensityPair(m_ui.haIntensitySlider, m_ui.haIntensitySpin);
    syncIntensityPair(m_ui.oiiiIntensitySlider, m_ui.oiiiIntensitySpin);
    syncIntensityPair(m_ui.siiIntensitySlider, m_ui.siiIntensitySpin);
}

void NarrowbandPaletteComposerProcess::syncIntensityPair(QSlider* slider, QSpinBox* spin)
{
    if (!slider || !spin)
    {
        return;
    }
    QObject::connect(slider, &QSlider::valueChanged, spin, &QSpinBox::setValue);
    QObject::connect(spin, qOverload<int>(&QSpinBox::valueChanged), slider, &QSlider::setValue);
    QObject::connect(slider, &QSlider::valueChanged, this, [this]() { handleSettingsChanged(); });
}

void NarrowbandPaletteComposerProcess::refreshTheme()
{
    ImageProcessControl::refreshTheme();
    if (const auto iconProvider = process_utils::currentIconProvider())
    {
        process_utils::applyDialogButtonBoxIcons(m_ui.buttonBox, iconProvider.get());
    }
    if (m_ui.paletteGrid)
    {
        m_ui.paletteGrid->update();
    }
    if (m_ui.singlePreview)
    {
        m_ui.singlePreview->update();
    }
}

void NarrowbandPaletteComposerProcess::onViewControlCreated(const ImageViewControl* ctrl)
{
    ImageRTPreviewControl::onViewControlCreated(ctrl);
    rebuildImageSelectors();
}

void NarrowbandPaletteComposerProcess::onViewControlDestroyed(const ImageViewControl* ctrl)
{
    ImageRTPreviewControl::onViewControlDestroyed(ctrl);
    rebuildImageSelectors();
    handleSettingsChanged();
}

void NarrowbandPaletteComposerProcess::onViewControlRenamed(const std::string& newName, const std::string& oldName,
                                                            const ImageViewControl* ctrl)
{
    ImageRTPreviewControl::onViewControlRenamed(newName, oldName, ctrl);
    rebuildImageSelectors();
    handleSettingsChanged();
}

void NarrowbandPaletteComposerProcess::onViewControlUpdated(const ImageViewControl* ctrl)
{
    ImageRTPreviewControl::onViewControlUpdated(ctrl);
    handleSettingsChanged();
}

bool NarrowbandPaletteComposerProcess::isPreviewEnabled() const
{
    // The grid is the preview; no preview view is made.
    return false;
}

void NarrowbandPaletteComposerProcess::enablePreview(bool enabled)
{
    if (enabled)
    {
        requestGridBuild();
        return;
    }
    settingsUpdatedImpl();
}

void NarrowbandPaletteComposerProcess::enableProcessControls(bool enabled)
{
    ImageRTPreviewControl::enableProcessControls(enabled);
    m_ui.controlsScrollArea->setEnabled(enabled);
    m_ui.previewStack->setEnabled(enabled);
    m_ui.buttonBox->button(QDialogButtonBox::Apply)->setEnabled(enabled);
    m_ui.buttonBox->button(QDialogButtonBox::Reset)->setEnabled(enabled);
    m_ui.buttonBox->button(QDialogButtonBox::Close)->setEnabled(enabled);
    if (enabled)
    {
        settingsUpdatedImpl();
    }
}

an::Status NarrowbandPaletteComposerProcess::executeTypedProcessImpl(
    const an::ui::ProcessSource& source, an::ui::ProcessDestination& destination, const an::ui::ProcessRun& run,
    const NarrowbandPaletteComposerProcessParams& params, an::ui::ExecutionScope& scope)
{
    (void)source;
    if (!an::ui::processes::narrowband_palette_detail::hasRequiredInputs(params))
    {
        return run.isForPreview()
                   ? an::Status::success()
                   : refusal(an::ErrorCode::InvalidArgument,
                             "select OIII plus Ha or SII, or one-shot-colour sources that synthesize them");
    }
    an::Result<PreparedChannels> prepared = prepareChannels(scope, params);
    if (!prepared.hasValue())
    {
        return prepared.status();
    }
    const PreparedPlane& reference = referenceOf(*prepared);
    const an::Result<an::ImageDescriptor> described =
        an::narrowbandPaletteOutputDescriptor(reference.bound->descriptor());
    if (!described.hasValue())
    {
        return described.status();
    }
    std::vector<std::shared_ptr<const an::consumer::ImageAsset>> assets = sourceAssets(params);
    std::vector<an::consumer::AssetReadLease> leases;
    std::vector<const an::ImageMetadata*> sources;
    for (const auto& asset : assets)
    {
        an::Result<an::consumer::AssetReadLease> lease =
            an::ui::readBeside(scope, *asset, scope.execution().stopToken());
        if (!lease.hasValue())
        {
            return lease.status();
        }
        leases.push_back(std::move(*lease));
    }
    for (const an::consumer::AssetReadLease& lease : leases)
    {
        sources.push_back(&an::ui::ProcessSource::of(lease).metadata());
    }
    // The result keeps the first source's solution only while that source's pixel grid is the result's: a plane resized
    // to the reference's size, or a reference synthesized from a one-shot-colour source, would have the solution label
    // another grid.
    an::OperationEffect effect = an::kNarrowbandPaletteEffect;
    const std::optional<an::ImageDescriptor> firstDescribed =
        leases.empty() ? std::nullopt : an::ui::ProcessSource::of(leases.front()).descriptor();
    if (!firstDescribed.has_value() || !process_utils::sameSize(*firstDescribed, *described))
    {
        effect.solution = an::SolutionEffect::Invalidates;
    }
    an::Result<an::ImageMetadata> metadata = process_utils::combinedResultMetadata(
        sources, effect, *described,
        process_utils::CombinationRecord{.stateOperation = "narrowband-palette-composer",
                                         .engineOperation = "an::composeNarrowbandPalette",
                                         .detail = describeTypedProcessStep(params).description,
                                         .linearity = process_utils::CombinationLinearity::Nonlinear});
    if (!metadata.hasValue())
    {
        return metadata.status();
    }
    const an::NarrowbandPaletteChannels channels = channelsOf(*prepared);
    const an::NarrowbandPaletteRequest request{
        .palette = params.palette, .intensities = params.intensities, .normalize = true};
    const an::ExecutionContext& execution = scope.execution();
    an::Result<an::consumer::ImageAsset> composed = process_utils::composeImage(
        scope, *described, std::move(*metadata), [&](void* samples, const an::ImageLayout& layout) -> an::Status {
            const an::Result<an::NarrowbandPaletteResult> ran =
                an::composeNarrowbandPalette(execution, an::SampleType::Float32, channels, samples, layout, request);
            return ran.hasValue() ? an::Status::success() : ran.status();
        });
    if (!composed.hasValue())
    {
        return composed.status();
    }
    return destination.replace(std::move(*composed));
}

an::ui::ProcessStep
NarrowbandPaletteComposerProcess::describeTypedProcessStep(const NarrowbandPaletteComposerProcessParams& params) const
{
    return an::ui::ProcessStep{.name = getProcessName(),
                               .description = std::string("Created ") + an::narrowbandPaletteName(params.palette) +
                                              " Narrowband Palette Composer image" +
                                              fmt::format(" (Ha {:.2f}, OIII {:.2f}, SII {:.2f}{}{})",
                                                          params.intensities.hydrogenAlpha,
                                                          params.intensities.oxygenIII, params.intensities.sulfurII,
                                                          params.stretchInputs ? ", stretched inputs" : "",
                                                          params.resizeMismatched ? "" : ", no resize")};
}

// The five sources: other views' images the body reads, leased before the run is admitted.
std::vector<std::shared_ptr<const an::consumer::ImageAsset>>
NarrowbandPaletteComposerProcess::typedImagesRead(const NarrowbandPaletteComposerProcessParams& params) const
{
    return {params.ha.image, params.oiii.image, params.sii.image, params.osc1.image, params.osc2.image};
}

void NarrowbandPaletteComposerProcess::getBatchExecutionParameters(bool& createNewImage, bool& closeAtEnd,
                                                                   bool& executeInBackground, std::string& newImageName)
{
    createNewImage = true;
    closeAtEnd = false;
    executeInBackground = true;
    newImageName = defaultOutputBaseName();
}

void NarrowbandPaletteComposerProcess::onContentLayoutReplaced(QBoxLayout* contentLayout)
{
    m_MainHorizontalLayout = contentLayout;
    if (!m_MainHorizontalLayout)
    {
        return;
    }
    m_MainHorizontalLayout->setSpacing(an::ui::tokens().spacing.xl);
    restoreCustomPreviewPane(findMainSplitter(m_MainHorizontalLayout));
    if (m_ui.paletteGroup)
    {
        m_ui.paletteGroup->setMaximumWidth(QWIDGETSIZE_MAX);
        m_ui.paletteGroup->setVisible(true);
    }
}

void NarrowbandPaletteComposerProcess::resetSettings()
{
    ImageRTPreviewControl::resetSettings();
    m_ui.linearInputStretch->setChecked(true);
    m_ui.resizeMismatched->setChecked(true);
    m_ui.haIntensitySpin->setValue(100);
    m_ui.oiiiIntensitySpin->setValue(100);
    m_ui.siiIntensitySpin->setValue(100);
    if (m_ui.previewModeSelector)
    {
        m_ui.previewModeSelector->setCurrentIndex(0);
    }
    m_syncingPaletteSelection = true;
    if (m_ui.paletteGrid)
    {
        m_ui.paletteGrid->setSelectedPalette(an::NarrowbandPalette::Sho);
    }
    if (m_ui.singlePreview)
    {
        m_ui.singlePreview->setSelectedPalette(an::NarrowbandPalette::Sho);
    }
    m_syncingPaletteSelection = false;
}

void NarrowbandPaletteComposerProcess::settingsUpdatedImpl()
{
    validateSelections();
    const auto params = getParams();
    const bool ready = m_image_view_control && an::ui::processes::narrowband_palette_detail::hasRequiredInputs(params);
    if (auto* applyButton = m_ui.buttonBox->button(QDialogButtonBox::Apply))
    {
        applyButton->setEnabled(ready);
    }
    if (ready)
    {
        const QString palette = QString::fromUtf8(an::narrowbandPaletteName(params.palette));
        if (isGridBuilding())
        {
            m_ui.statusLabel->setText(QStringLiteral("Updating the synchronized palette grid..."));
        }
        else if (hasGrid())
        {
            m_ui.statusLabel->setText(
                QStringLiteral("Palette grid is ready. Apply will create a new %1 RGB image view.").arg(palette));
        }
        else
        {
            m_ui.statusLabel->setText(
                QStringLiteral("Ready to update %1. The palette grid refreshes automatically.").arg(palette));
        }
    }
    else
    {
        m_ui.statusLabel->setText(
            QStringLiteral("Select OIII plus Ha or SII, or use OSC sources that synthesize them."));
        m_ui.warningLabel->clear();
        clearPaletteGridImages();
    }
}

NarrowbandPaletteComposerProcessParams NarrowbandPaletteComposerProcess::getParams() const
{
    NarrowbandPaletteComposerProcessParams params;
    params.palette = selectedPalette();
    params.stretchInputs = m_ui.linearInputStretch->isChecked();
    params.resizeMismatched = m_ui.resizeMismatched->isChecked();
    params.intensities.hydrogenAlpha = static_cast<double>(m_ui.haIntensitySpin->value()) / 100.0;
    params.intensities.oxygenIII = static_cast<double>(m_ui.oiiiIntensitySpin->value()) / 100.0;
    params.intensities.sulfurII = static_cast<double>(m_ui.siiIntensitySpin->value()) / 100.0;
    params.ha = sourceFromSelector(m_ui.haSourceSelector);
    params.oiii = sourceFromSelector(m_ui.oiiiSourceSelector);
    params.sii = sourceFromSelector(m_ui.siiSourceSelector);
    params.osc1 = sourceFromSelector(m_ui.osc1SourceSelector);
    params.osc2 = sourceFromSelector(m_ui.osc2SourceSelector);
    params.outputName = outputNameFor(params);
    return params;
}

bool NarrowbandPaletteComposerProcess::isViewControlSupported(const ImageViewControl* ctrl)
{
    return supportedComposerView(ctrl);
}

void NarrowbandPaletteComposerProcess::rebuildImageSelectors()
{
    const QString ha = m_ui.haSourceSelector->currentText();
    const QString oiii = m_ui.oiiiSourceSelector->currentText();
    const QString sii = m_ui.siiSourceSelector->currentText();
    const QString osc1 = m_ui.osc1SourceSelector->currentText();
    const QString osc2 = m_ui.osc2SourceSelector->currentText();

    m_rebuildingSelectors = true;
    const std::array<QComboBox*, 5> selectors = {m_ui.haSourceSelector, m_ui.oiiiSourceSelector, m_ui.siiSourceSelector,
                                                 m_ui.osc1SourceSelector, m_ui.osc2SourceSelector};
    for (auto* selector : selectors)
    {
        selector->clear();
        addNoImageItem(selector);
    }
    for (auto* control : process_utils::listViewControls())
    {
        if (!isViewControlSupported(control))
        {
            continue;
        }
        for (auto* selector : selectors)
        {
            addImageItem(selector, control);
        }
    }
    restoreSelection(m_ui.haSourceSelector, ha);
    restoreSelection(m_ui.oiiiSourceSelector, oiii);
    restoreSelection(m_ui.siiSourceSelector, sii);
    restoreSelection(m_ui.osc1SourceSelector, osc1);
    restoreSelection(m_ui.osc2SourceSelector, osc2);
    m_rebuildingSelectors = false;
}

void NarrowbandPaletteComposerProcess::validateSelections()
{
    // The reference view - OIII, then H-alpha, then SII, then a one-shot-colour source - is the window's target: the
    // result is opened beside it and carries its grid.
    ImageViewControl* reference = nullptr;
    const std::array<QComboBox*, 5> preferred = {m_ui.oiiiSourceSelector, m_ui.haSourceSelector, m_ui.siiSourceSelector,
                                                 m_ui.osc1SourceSelector, m_ui.osc2SourceSelector};
    for (auto* selector : preferred)
    {
        const QString text = selector ? selector->currentText() : QString();
        if (text.isEmpty() || text == NO_IMAGE_STR)
        {
            continue;
        }
        reference = process_utils::findViewControlByName(text);
        if (reference)
        {
            break;
        }
    }
    if (reference != nullptr && reference != m_image_view_control)
    {
        changeTargetViewControl(reference);
    }
}

void NarrowbandPaletteComposerProcess::refreshPaletteGridItems()
{
    m_syncingPaletteSelection = true;
    const std::span<const an::NarrowbandPaletteDescriptor> palettes = an::narrowbandPalettes();
    const std::vector<an::NarrowbandPaletteDescriptor> descriptors(palettes.begin(), palettes.end());
    if (m_ui.paletteGrid)
    {
        m_ui.paletteGrid->setPaletteDescriptors(descriptors);
        m_ui.paletteGrid->setSelectedPalette(an::NarrowbandPalette::Sho);
    }
    if (m_ui.singlePreview)
    {
        m_ui.singlePreview->setPaletteDescriptors(descriptors);
        m_ui.singlePreview->setSelectedPalette(an::NarrowbandPalette::Sho);
    }
    m_syncingPaletteSelection = false;
}

void NarrowbandPaletteComposerProcess::clearPaletteGridImages()
{
    if (m_ui.paletteGrid)
    {
        m_ui.paletteGrid->clearPreviewImages();
    }
    if (m_ui.singlePreview)
    {
        m_ui.singlePreview->clearPreviewImages();
    }
}

void NarrowbandPaletteComposerProcess::handleSettingsChanged()
{
    validateSelections();
    const auto params = getParams();
    if (!m_image_view_control || !an::ui::processes::narrowband_palette_detail::hasRequiredInputs(params))
    {
        // A grid on its way shows nothing that no longer applies.
        m_grid->generation.fetch_add(1u);
        m_grid->pending = false;
        m_grid->next.reset();
        settingsUpdatedImpl();
        return;
    }
    settingsUpdatedImpl();
    requestGridBuild();
}

void NarrowbandPaletteComposerProcess::handlePreviewModeChanged(const int index)
{
    const int pageIndex = index == 1 ? 1 : 0;
    if (m_ui.previewStack)
    {
        m_ui.previewStack->setCurrentIndex(pageIndex);
    }
    if (m_ui.paletteGroup)
    {
        m_ui.paletteGroup->setTitle(pageIndex == 1 ? QStringLiteral("Single Preview") : QStringLiteral("Palette Grid"));
    }
}

void NarrowbandPaletteComposerProcess::handlePaletteSelectionChanged(const an::NarrowbandPalette palette)
{
    if (m_syncingPaletteSelection)
    {
        return;
    }
    m_syncingPaletteSelection = true;
    if (m_ui.paletteGrid)
    {
        m_ui.paletteGrid->setSelectedPalette(palette);
    }
    if (m_ui.singlePreview)
    {
        m_ui.singlePreview->setSelectedPalette(palette);
    }
    m_syncingPaletteSelection = false;
    validateSelections();
    settingsUpdatedImpl();
}

void NarrowbandPaletteComposerProcess::requestGridBuild()
{
    validateSelections();
    const auto params = getParams();
    if (!m_image_view_control || !an::ui::processes::narrowband_palette_detail::hasRequiredInputs(params))
    {
        const QString message =
            QStringLiteral("Preview needs OIII plus Ha or SII, or OSC sources that synthesize those channels.");
        m_ui.statusLabel->setText(message);
        m_ui.warningLabel->clear();
        return;
    }
    // The latest request wins: while a grid builds, the next starts when it lands, with the values held then.
    m_grid->next = params;
    m_grid->pending = true;
    if (!m_grid->building)
    {
        startGridBuild();
    }
}

void NarrowbandPaletteComposerProcess::startGridBuild()
{
    if (!m_grid->pending || !m_grid->next.has_value())
    {
        return;
    }
    auto* context = an::ui::appContext();
    an::ui::IEngineSessionProvider* provider = context ? context->getEngineSessionProvider() : nullptr;
    if (provider == nullptr)
    {
        m_grid->pending = false;
        return;
    }
    const std::shared_ptr<const NarrowbandPaletteComposerProcessParams> params =
        std::make_shared<const NarrowbandPaletteComposerProcessParams>(std::move(*m_grid->next));
    m_grid->next.reset();
    m_grid->pending = false;
    m_grid->building = true;
    const std::uint64_t generation = m_grid->generation.fetch_add(1u) + 1u;
    m_ui.statusLabel->setText(QStringLiteral("Updating palette grid..."));
    m_ui.warningLabel->clear();
    setPaletteGridBusy(true);

    const an::consumer::EngineSession* session = &provider->engineSession();
    QPointer<NarrowbandPaletteComposerProcess> guard(this);
    // The worker holds the grid's state of its own: a window closed while the grid builds frees nothing it reads.
    const std::shared_ptr<Grid> grid = m_grid;
    const std::stop_token stop = windowStopToken();
    QThreadPool::globalInstance()->start([session, params, guard, grid, generation, stop]() {
        // One admitted operation prepares the sources and composes every palette; the pictures land afterwards.
        struct Landing
        {
            std::vector<an::ui::processes::NarrowbandPaletteComposerPreviewFrame> frames;
            QStringList warnings;
            an::Status outcome = an::Status::success();
        };
        auto landing = std::make_shared<Landing>();
        // The sources are other views' images, leased before the grid is admitted, one lease each: an edit of a view
        // holds its writer while it waits to be admitted.
        an::ui::ImageReads reads;
        for (const std::shared_ptr<const an::consumer::ImageAsset>& source : sourceAssets(*params))
        {
            reads.name(source);
        }
        const an::Status taken = reads.take(stop);
        an::Result<an::ui::ExecutionScope> admitted =
            taken.failed()
                ? an::Result<an::ui::ExecutionScope>(taken)
                : an::ui::ExecutionScope::admit(
                      *session,
                      an::ui::ExecutionRequest{
                          .purpose = an::consumer::OperationPurpose::Preview, .stopToken = stop, .reads = &reads},
                      an::ui::ExecutionObserver{});
        if (!admitted.hasValue())
        {
            landing->outcome = admitted.status();
        }
        else if (grid->generation.load() == generation)
        {
            an::ui::ExecutionScope& scope = *admitted;
            an::Result<PreparedChannels> prepared = prepareChannels(scope, *params);
            if (!prepared.hasValue())
            {
                landing->outcome = prepared.status();
            }
            else
            {
                for (const std::string& warning : prepared->warnings)
                {
                    landing->warnings.push_back(QString::fromStdString(warning));
                }
                const PreparedPlane& reference = referenceOf(*prepared);
                const an::Result<an::ImageDescriptor> described =
                    an::narrowbandPaletteOutputDescriptor(reference.bound->descriptor());
                const std::span<const an::NarrowbandPaletteDescriptor> palettes = an::narrowbandPalettes();
                std::vector<an::consumer::ImageAsset> outputs;
                std::vector<an::consumer::AssetWriteLease> leases;
                std::vector<an::NarrowbandPaletteDestination> destinations;
                std::vector<an::NarrowbandPaletteResult> results(palettes.size());
                std::vector<an::ImageLayout> layouts;
                if (!described.hasValue())
                {
                    landing->outcome = described.status();
                }
                else
                {
                    layouts.reserve(palettes.size());
                    for (const an::NarrowbandPaletteDescriptor& palette : palettes)
                    {
                        an::Result<an::consumer::ImageAsset> output =
                            emptyPlane(scope, *described, described->extent().width(), described->extent().height(), 3u,
                                       an::ImageMetadata{});
                        if (!output.hasValue())
                        {
                            landing->outcome = output.status();
                            break;
                        }
                        outputs.push_back(std::move(*output));
                        an::Result<an::consumer::AssetWriteLease> lease = outputs.back().write(stop);
                        if (!lease.hasValue())
                        {
                            landing->outcome = lease.status();
                            break;
                        }
                        leases.push_back(std::move(*lease));
                        std::optional<an::ImageWriteAccess<float>> access = leases.back().write<float>();
                        if (!access.has_value())
                        {
                            landing->outcome = refusal(an::ErrorCode::NotInitialized, "a palette holds nothing");
                            break;
                        }
                        layouts.push_back(access->view().layout());
                        destinations.push_back(an::NarrowbandPaletteDestination{
                            .palette = palette.palette, .samples = access->view().data(), .layout = &layouts.back()});
                    }
                }
                if (landing->outcome.ok())
                {
                    const an::Status composed = an::composeNarrowbandPaletteGrid(
                        scope.execution(), an::SampleType::Float32, channelsOf(*prepared), destinations,
                        params->intensities, results, true);
                    if (composed.failed())
                    {
                        landing->outcome = composed;
                    }
                }
                if (landing->outcome.ok())
                {
                    for (std::size_t index = 0u; index < destinations.size(); ++index)
                    {
                        an::Result<QImage> picture =
                            pictureOf(scope, *described, destinations[index].samples, layouts[index]);
                        if (!picture.hasValue())
                        {
                            landing->outcome = picture.status();
                            break;
                        }
                        landing->frames.push_back(an::ui::processes::NarrowbandPaletteComposerPreviewFrame{
                            .palette = palettes[index].palette,
                            .name = QString::fromUtf8(palettes[index].name),
                            .description = QString::fromUtf8(palettes[index].description),
                            .image = std::move(*picture)});
                    }
                }
            }
            scope.finish(landing->outcome);
        }
        postToGui([guard, grid, generation, landing, params]() {
            if (!guard)
            {
                return;
            }
            NarrowbandPaletteComposerProcess& window = *guard;
            grid->building = false;
            if (grid->generation.load() != generation)
            {
                // Something changed since: the latest request builds now, if there is one; with none, nothing is
                // building, and the window says what its inputs are now.
                window.startGridBuild();
                if (!grid->building)
                {
                    window.setPaletteGridBusy(false);
                    window.settingsUpdatedImpl();
                }
                return;
            }
            window.setPaletteGridBusy(false);
            if (landing->outcome.failed())
            {
                window.m_ui.warningLabel->setText(QString::fromUtf8(landing->outcome.message()));
                window.m_ui.statusLabel->setText(
                    QStringLiteral("Palette grid update failed. Check the status details below."));
                LOG_WARNING("Narrowband Palette Composer: palette grid generation failed: {}",
                            landing->outcome.message());
                window.startGridBuild();
                return;
            }
            window.m_ui.warningLabel->setText(landing->warnings.join(QStringLiteral("\n")));
            window.m_syncingPaletteSelection = true;
            std::vector<an::ui::processes::NarrowbandPaletteComposerPreviewFrame> singleFrames = landing->frames;
            window.m_ui.paletteGrid->setPreviewImages(std::move(landing->frames));
            window.m_ui.paletteGrid->setSelectedPalette(params->palette);
            window.m_ui.singlePreview->setPreviewImages(std::move(singleFrames));
            window.m_ui.singlePreview->setSelectedPalette(params->palette);
            window.m_syncingPaletteSelection = false;
            ++grid->shown;
            window.m_ui.statusLabel->setText(QStringLiteral("Palette grid ready."));
            window.settingsUpdatedImpl();
            window.startGridBuild();
        });
    });
}

void NarrowbandPaletteComposerProcess::setPaletteGridBusy(bool busy)
{
    if (!m_ui.paletteGridBusySpinner)
    {
        return;
    }
    if (busy)
    {
        m_ui.paletteGridBusySpinner->raise();
        m_ui.paletteGridBusySpinner->start();
        return;
    }
    m_ui.paletteGridBusySpinner->stop();
}

an::NarrowbandPalette NarrowbandPaletteComposerProcess::selectedPalette() const
{
    if (m_ui.paletteGrid)
    {
        return m_ui.paletteGrid->selectedPalette();
    }
    return m_ui.singlePreview ? m_ui.singlePreview->selectedPalette() : an::NarrowbandPalette::Sho;
}

bool NarrowbandPaletteComposerProcess::hasRequiredSourceSelection() const
{
    return m_image_view_control && an::ui::processes::narrowband_palette_detail::hasRequiredInputs(getParams());
}

std::string NarrowbandPaletteComposerProcess::defaultOutputBaseName() const
{
    return getParams().outputName;
}

NarrowbandPaletteSource NarrowbandPaletteComposerProcess::sourceFromSelector(const QComboBox* selector) const
{
    NarrowbandPaletteSource source;
    const QString text = selector ? selector->currentText() : QString();
    if (text.isEmpty() || text == NO_IMAGE_STR)
    {
        return source;
    }
    if (auto* control = process_utils::findViewControlByName(text); control != nullptr && control->hasImage())
    {
        source.image = control->document().asset();
        source.name = text.toStdString();
    }
    return source;
}
