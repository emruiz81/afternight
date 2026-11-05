// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The Channel Composition window: three selectors and a colour space on the GUI thread, and one admitted operation
/// on the thread pool that reads the components under their leases, composes them through the engine and hands the
/// result to the GUI thread, which opens its view beside the first component.

#include "stdafx.hpp"
#include "ChannelCompositionProcess.h"
#include "app/FileCompanions.h"
#include "controls/IAppContext.h"
#include "controls/ImageSelectorTooltipBuilder.h"
#include "controls/ImageViewControl.h"
#include "controls/ProcessCompanions.h"
#include "controls/ProgressDialogControl.h"
#include "processes/CombinationProcessSupport.h"
#include "processes/MaskProcessUtils.h"
#include "processes/PointwiseProcessUtils.h"
#include "processes/ProcessCommonUtils.h"
#include "processes/core/LinearChannelMatch.h"
#include "session/IEngineSessionProvider.h"
#include "session/ImageReads.h"

#include <AfterNightConsumerRuntime/EngineSession.h>

#include <AfterNightEngine/Algorithms/Color/ColorTransform.h>
#include <AfterNightEngine/Algorithms/Normalization/GlobalNormalization.h>
#include <AfterNightEngine/Algorithms/Normalization/NormalizationMethod.h>
#include <AfterNightEngine/Algorithms/Statistics/BasicStatistics.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/ImageExtent.h>
#include <AfterNightEngine/Image/ChannelTransfer.h>
#include <AfterNightEngine/Image/ConversionPolicy.h>
#include <AfterNightEngine/Image/Image.h>
#include <AfterNightEngine/Image/ImageConversion.h>
#include <AfterNightEngine/Image/ImageTransfer.h>
#include <AfterNightEngine/Image/SampleTraits.h>
#include <AfterNightEngine/Image/ValidityDescriptor.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <AfterNightPlatform/Logging.h>

#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QPointer>
#include <QPushButton>
#include <QThreadPool>

#include <atomic>
#include <cstdint>
#include <functional>
#include <stop_token>
#include <utility>
#include <vector>

namespace composition_detail = an::ui::processes::channel_composition_detail;

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

struct SpaceEntry
{
    composition_detail::ColourSpace space;
    const char* label;
    const char* icon;
    const char* imageName;
    std::array<const char*, 3u> components;
};

constexpr std::array<SpaceEntry, 8u> kSpaces{{
    {composition_detail::ColourSpace::Rgb, "RGB", "rgb", "RGB", {"Red", "Green", "Blue"}},
    {composition_detail::ColourSpace::CieXyz, "CIE XYZ", "cielab", "CIE XYZ", {"X", "Y", "Z"}},
    {composition_detail::ColourSpace::CieLab, "CIE L*a*b*", "cielab", "CIE L*a*b", {"L", "a", "b"}},
    {composition_detail::ColourSpace::CieLch, "CIE L*c*h*", "cielab", "CIE L*c*h", {"L", "c", "h"}},
    {composition_detail::ColourSpace::Hsl, "HSL", "hsl", "HSL", {"H", "S", "L"}},
    {composition_detail::ColourSpace::Hsv, "HSV", "hsv", "HSV", {"H", "S", "V"}},
    {composition_detail::ColourSpace::Hsi, "HSI", "hsv", "HSI", {"H", "S", "I"}},
    {composition_detail::ColourSpace::YCbCr, "YCbCr", "cielab", "YCbCr", {"Y", "Cb", "Cr"}},
}};

[[nodiscard]] const SpaceEntry& entryOf(composition_detail::ColourSpace space) noexcept
{
    for (const SpaceEntry& entry : kSpaces)
    {
        if (entry.space == space)
        {
            return entry;
        }
    }
    return kSpaces.front();
}

/// A representation's rank, lowest first: a narrower before a wider, a signed before an unsigned of the same width, an
/// integer before a floating one.
[[nodiscard]] int rankOf(an::SampleType type) noexcept
{
    switch (type)
    {
    case an::SampleType::Int8:
        return 0;
    case an::SampleType::UInt8:
        return 1;
    case an::SampleType::Int16:
        return 2;
    case an::SampleType::UInt16:
        return 3;
    case an::SampleType::Int32:
        return 4;
    case an::SampleType::UInt32:
        return 5;
    case an::SampleType::Float32:
        return 6;
    case an::SampleType::Float64:
    default:
        return 7;
    }
}

/// One component's unit-fraction plane of the working representation, leased and bound.
struct ComponentPlane
{
    an::consumer::ImageAsset image;
    an::consumer::AssetReadLease lease;
    std::unique_ptr<process_utils::BoundImage> bound;
};

[[nodiscard]] an::Result<std::unique_ptr<ComponentPlane>> leaseComponent(const an::ui::ExecutionScope& scope,
                                                                         an::consumer::ImageAsset&& image)
{
    an::Result<an::consumer::AssetReadLease> lease = image.read(scope.execution().stopToken());
    if (!lease.hasValue())
    {
        return lease.status();
    }
    auto plane = std::make_unique<ComponentPlane>(
        ComponentPlane{.image = std::move(image), .lease = std::move(*lease), .bound = nullptr});
    an::Result<std::unique_ptr<process_utils::BoundImage>> bound =
        process_utils::BoundImage::bind(an::ui::ProcessSource::of(plane->lease));
    if (!bound.hasValue())
    {
        return bound.status();
    }
    plane->bound = std::move(*bound);
    return plane;
}

/// A zero plane of a representation at the first component's size.
[[nodiscard]] an::Result<an::consumer::ImageAsset>
zeroPlane(const an::ui::ExecutionScope& scope, const an::ImageDescriptor& reference, an::SampleType working)
{
    const an::Result<an::ImageDescriptor> described =
        process_utils::compositionDescriptor(reference, 1u, working, scope.engine().limits());
    if (!described.hasValue())
    {
        return described.status();
    }
    return process_utils::composeImage(scope, *described, an::ImageMetadata{},
                                       [](void*, const an::ImageLayout&) { return an::Status::success(); });
}

/// A component matched to the reference by the normalization domain's scaling match inside the linear match's limits:
/// the ratio of the robust scales and the offset that moves the robust location onto the reference's.
[[nodiscard]] an::Result<an::consumer::ImageAsset> matchedToReference(const an::ui::ExecutionScope& scope,
                                                                      const ComponentPlane& reference,
                                                                      const ComponentPlane& component)
{
    const an::ExecutionContext& execution = scope.execution();
    const an::ui::processes::LinearMatchPlane referencePlane{.samples = reference.bound->samples(),
                                                             .layout = &reference.bound->layout(),
                                                             .descriptor = &reference.bound->descriptor()};
    const an::ui::processes::LinearMatchPlane componentPlane{.samples = component.bound->samples(),
                                                             .layout = &component.bound->layout(),
                                                             .descriptor = &component.bound->descriptor()};
    const an::Result<an::FrameNormalization> normalization =
        an::ui::processes::measureLinearChannelMatch(execution, referencePlane, componentPlane);
    if (!normalization.hasValue())
    {
        return normalization.status();
    }
    an::ImageMetadata metadata = component.bound->metadata();
    return process_utils::composeImage(scope, component.bound->descriptor(), std::move(metadata),
                                       [&](void* samples, const an::ImageLayout& layout) -> an::Status {
                                           return an::ui::processes::applyLinearChannelMatch(
                                               execution, *normalization, componentPlane, samples, layout);
                                       });
}

/// The encoding the composed planes are the components of, and the red, green and blue they become.
struct SpaceEncodings
{
    an::ColorEncoding source;
    an::ColorEncoding destination;
};

[[nodiscard]] SpaceEncodings encodingsOf(composition_detail::ColourSpace space, const an::ImageDescriptor& rgb) noexcept
{
    SpaceEncodings encodings;
    encodings.destination = process_utils::colourReadingOf(rgb);
    an::ColorEncoding& source = encodings.source;
    source = an::ColorEncoding{};
    source.whitePoint = encodings.destination.whitePoint;
    switch (space)
    {
    case composition_detail::ColourSpace::CieXyz:
        source.schema = an::ChannelSchema::Xyz;
        break;
    case composition_detail::ColourSpace::CieLab:
        source.schema = an::ChannelSchema::Lab;
        source.perceptual = an::PerceptualEncoding::UnitPacked;
        break;
    case composition_detail::ColourSpace::CieLch:
        source.schema = an::ChannelSchema::Lch;
        source.perceptual = an::PerceptualEncoding::UnitPacked;
        break;
    case composition_detail::ColourSpace::Hsl:
    case composition_detail::ColourSpace::Hsv:
    case composition_detail::ColourSpace::Hsi:
        source.schema = space == composition_detail::ColourSpace::Hsl   ? an::ChannelSchema::Hsl
                        : space == composition_detail::ColourSpace::Hsv ? an::ChannelSchema::Hsv
                                                                        : an::ChannelSchema::Hsi;
        source.primaries = encodings.destination.primaries;
        source.transfer = encodings.destination.transfer;
        source.transferGamma = encodings.destination.transferGamma;
        source.hue = an::HueConvention::TurnFraction;
        break;
    case composition_detail::ColourSpace::YCbCr:
        source.schema = an::ChannelSchema::YCbCr;
        source.primaries = encodings.destination.primaries;
        source.transfer = encodings.destination.transfer;
        source.transferGamma = encodings.destination.transferGamma;
        source.yCbCr = an::YCbCrVariant::Rec601FullRange;
        break;
    case composition_detail::ColourSpace::Rgb:
    default:
        source = encodings.destination;
        break;
    }
    return encodings;
}

} // namespace

namespace an::ui::processes::channel_composition_detail
{

std::optional<ColourSpace> colourSpaceFromLabel(const QString& label)
{
    for (const SpaceEntry& entry : kSpaces)
    {
        if (label == QLatin1String(entry.label))
        {
            return entry.space;
        }
    }
    return std::nullopt;
}

const char* colourSpaceLabel(ColourSpace space) noexcept
{
    return entryOf(space).label;
}

const char* composedImageName(ColourSpace space) noexcept
{
    return entryOf(space).imageName;
}

std::array<const char*, 3u> componentNames(ColourSpace space) noexcept
{
    return entryOf(space).components;
}

std::string compositionDescription(const CompositionRequest& request)
{
    const std::array<const char*, 3u> names = componentNames(request.space);
    const auto initial = [](const char* name) { return std::string(name).substr(0u, name[1] == '\0' ? 1u : 2u); };
    const std::array<std::string, 3u> initials{
        request.space == ColourSpace::Rgb ? "R" : initial(names[0]),
        request.space == ColourSpace::Rgb ? "G" : initial(names[1]),
        request.space == ColourSpace::Rgb ? "B" : initial(names[2]),
    };
    return fmt::format("ChannelComposition: created from image channels {}:'{}' {}:'{} {}:'{}'{}", initials[0],
                       request.names[0], initials[1], request.names[1], initials[2], request.names[2],
                       request.linearMatch ? ", linearly matched" : "");
}

an::Result<an::consumer::ImageAsset> composeChannelImages(const an::ui::ExecutionScope& scope,
                                                          const CompositionRequest& request)
{
    if (request.components[0] == nullptr)
    {
        return refusal(an::ErrorCode::InvalidArgument, "The first channel component is required");
    }
    const std::optional<an::ImageDescriptor> first = request.components[0]->descriptor();
    if (!first.has_value())
    {
        return refusal(an::ErrorCode::NotInitialized, "the first component holds nothing");
    }
    an::SampleType working = an::SampleType::Float32;
    an::SampleType result = first->sampleType();
    std::vector<const an::ImageMetadata*> sources;
    for (const an::ui::ProcessSource* component : request.components)
    {
        if (component == nullptr)
        {
            continue;
        }
        const std::optional<an::ImageDescriptor> described = component->descriptor();
        if (!described.has_value())
        {
            return refusal(an::ErrorCode::NotInitialized, "a component holds nothing");
        }
        if (described->extent().channelCount() != 1u)
        {
            return refusal(an::ErrorCode::IncompatibleArguments, "every component is a grey image");
        }
        if (!process_utils::sameSize(*described, *first))
        {
            return refusal(an::ErrorCode::IncompatibleArguments, "All the images must have the same size");
        }
        if (process_utils::normalizedWorkingType(described->sampleType()) == an::SampleType::Float64)
        {
            working = an::SampleType::Float64;
        }
        if (rankOf(described->sampleType()) > rankOf(result))
        {
            result = described->sampleType();
        }
        sources.push_back(&component->metadata());
    }
    const an::ExecutionContext& execution = scope.execution();

    // Each component as a unit-fraction plane of the working representation; a missing one is zero.
    std::array<std::unique_ptr<ComponentPlane>, 3u> planes;
    for (std::size_t index = 0u; index < 3u; ++index)
    {
        an::Result<an::consumer::ImageAsset> plane =
            request.components[index] != nullptr
                ? process_utils::unitFloatCopy(scope, *request.components[index], working)
                : zeroPlane(scope, *first, working);
        if (!plane.hasValue())
        {
            return plane.status();
        }
        an::Result<std::unique_ptr<ComponentPlane>> leased = leaseComponent(scope, std::move(*plane));
        if (!leased.hasValue())
        {
            return leased.status();
        }
        planes[index] = std::move(*leased);
    }
    if (request.linearMatch)
    {
        for (std::size_t index = 1u; index < 3u; ++index)
        {
            if (request.components[index] == nullptr)
            {
                continue;
            }
            an::Result<an::consumer::ImageAsset> matched = matchedToReference(scope, *planes[0], *planes[index]);
            if (!matched.hasValue())
            {
                return matched.status();
            }
            an::Result<std::unique_ptr<ComponentPlane>> leased = leaseComponent(scope, std::move(*matched));
            if (!leased.hasValue())
            {
                return leased.status();
            }
            planes[index] = std::move(*leased);
        }
    }

    // The planes composed as the space's components and converted to red, green and blue in the working
    // representation.
    const an::Result<an::ImageDescriptor> workingDescribed =
        process_utils::compositionDescriptor(*first, 3u, working, scope.engine().limits());
    if (!workingDescribed.hasValue())
    {
        return workingDescribed.status();
    }
    const SpaceEncodings encodings = encodingsOf(request.space, *workingDescribed);
    std::optional<an::ColorConversionPlan> conversion;
    if (request.space != composition_detail::ColourSpace::Rgb)
    {
        an::Result<an::ColorConversionPlan> planned = an::planColorConversion(encodings.source, encodings.destination);
        if (!planned.hasValue())
        {
            return planned.status();
        }
        conversion.emplace(std::move(*planned));
    }
    if (request.space == composition_detail::ColourSpace::CieXyz)
    {
        // The dialog's XYZ planes are unit components, each the tristimulus over the white's.
        const std::array<double, 3u> white = an::whitePointTristimulus(encodings.source.whitePoint);
        for (std::size_t index = 0u; index < 3u; ++index)
        {
            // The plane's own read lease goes first: a writer waits for every reader.
            planes[index]->bound.reset();
            planes[index]->lease.release();
            an::Result<an::consumer::AssetWriteLease> lease = planes[index]->image.write(execution.stopToken());
            if (!lease.hasValue())
            {
                return lease.status();
            }
            const an::Status scaled = std::visit(
                [&](auto& typed) -> an::Status {
                    auto access = typed.write();
                    if (!access.has_value())
                    {
                        return refusal(an::ErrorCode::NotInitialized, "a plane holds nothing");
                    }
                    const an::ImageLayout layout = access->view().layout();
                    return process_utils::scaleAndBiasInPlace(execution, working, access->view().data(), layout,
                                                              white[index], 0.0);
                },
                const_cast<an::consumer::AnyImage&>(lease->image()));
            if (scaled.failed())
            {
                return scaled;
            }
            lease->release();
            an::Result<an::consumer::AssetReadLease> reread = planes[index]->image.read(execution.stopToken());
            if (!reread.hasValue())
            {
                return reread.status();
            }
            planes[index]->lease = std::move(*reread);
            an::Result<std::unique_ptr<process_utils::BoundImage>> bound =
                process_utils::BoundImage::bind(an::ui::ProcessSource::of(planes[index]->lease));
            if (!bound.hasValue())
            {
                return bound.status();
            }
            planes[index]->bound = std::move(*bound);
        }
    }
    const an::Result<an::ImageDescriptor> described =
        process_utils::compositionDescriptor(*first, 3u, result, scope.engine().limits());
    if (!described.hasValue())
    {
        return described.status();
    }
    const composition_detail::CompositionRequest& composed = request;
    an::Result<an::ImageMetadata> metadata = process_utils::combinedResultMetadata(
        sources, an::kChannelComposeEffect, *described,
        process_utils::CombinationRecord{.stateOperation = "channel-composition",
                                         .engineOperation = request.space == composition_detail::ColourSpace::Rgb
                                                                ? "an::composeImageChannels"
                                                                : "an::convertImageColor",
                                         .detail = compositionDescription(composed),
                                         .linearity = request.space == composition_detail::ColourSpace::Rgb
                                                          ? process_utils::CombinationLinearity::AsSources
                                                          : process_utils::CombinationLinearity::Nonlinear});
    if (!metadata.hasValue())
    {
        return metadata.status();
    }
    const std::array<an::ChannelSource, 3u> channelSources{
        an::ChannelSource{.samples = planes[0]->bound->samples(), .layout = &planes[0]->bound->layout()},
        an::ChannelSource{.samples = planes[1]->bound->samples(), .layout = &planes[1]->bound->layout()},
        an::ChannelSource{.samples = planes[2]->bound->samples(), .layout = &planes[2]->bound->layout()}};
    const auto fill = [&](void* samples, const an::ImageLayout& layout) -> an::Status {
        if (an::Status joined = an::composeImageChannels(execution, working, channelSources, samples, layout);
            joined.failed())
        {
            return joined;
        }
        if (!conversion.has_value())
        {
            return an::Status::success();
        }
        if (an::Status converted =
                an::convertImageColor(execution, *conversion, working, samples, layout, samples, layout);
            converted.failed())
        {
            return converted;
        }
        // Every converted channel is clamped to the unit interval.
        return process_utils::holdToUnitInterval(scope.engine(), execution, working, samples, layout);
    };
    if (result == working)
    {
        return process_utils::composeImage(scope, *described, std::move(*metadata), fill);
    }
    // The composition narrowed once into the highest of the components' representations.
    an::Result<an::consumer::ImageAsset> workingImage =
        process_utils::composeImage(scope, *workingDescribed, an::ImageMetadata{}, fill);
    if (!workingImage.hasValue())
    {
        return workingImage.status();
    }
    an::Result<an::consumer::AssetReadLease> workingLease = workingImage->read(execution.stopToken());
    if (!workingLease.hasValue())
    {
        return workingLease.status();
    }
    an::Result<std::unique_ptr<process_utils::BoundImage>> workingBound =
        process_utils::BoundImage::bind(an::ui::ProcessSource::of(*workingLease));
    if (!workingBound.hasValue())
    {
        return workingBound.status();
    }
    return process_utils::composeImage(
        scope, *described, std::move(*metadata), [&](void* samples, const an::ImageLayout& layout) -> an::Status {
            an::ConversionCounters counters;
            return an::convertImageSamples(execution, working, (*workingBound)->samples(), (*workingBound)->layout(),
                                           result, samples, layout, an::ConversionPolicy{}, counters);
        });
}

} // namespace an::ui::processes::channel_composition_detail

/// A composition on its way: what it reads, the stop the window and the dialog share, and what it came to.
struct ChannelCompositionProcess::Run
{
    bool running = false;
    std::uint64_t published = 0u;
    std::uint64_t failed = 0u;
    std::stop_source stop;
};

ChannelCompositionProcess::ChannelCompositionProcess(QWidget* parent)
    : TopLevelProcessBaseControl(getProcessNameStatic()), AfterNightAppListener(process_utils::currentAppEventBus()),
      m_run(std::make_unique<Run>())
{
    Q_UNUSED(parent);

    setFixedSizeWindow(true);

    m_ui = an::ui::processes::buildChannelCompositionProcessUi(this);

    QObject::connect(m_ui.buttonBox, &QDialogButtonBox::clicked, this,
                     &ChannelCompositionProcess::on_dialog_box_button_clicked);
    for (QComboBox* selector : {m_ui.redChannel, m_ui.greenChannel, m_ui.blueChannel})
    {
        QObject::connect(selector, &QComboBox::currentTextChanged, this,
                         [this](const QString&) { validateSelectedImages(); });
    }
    QObject::connect(m_ui.colorSpace, &QComboBox::currentTextChanged, this,
                     &ChannelCompositionProcess::on_colorSpace_currentTextChanged);

    setWindowTitle("Channel Composition");
    setProcessDescription(QStringLiteral(
        "Compose a color image from grayscale channel sources using selectable target color-space mappings."));

    const auto iconManager = process_utils::currentIconProvider();
    for (const SpaceEntry& entry : kSpaces)
    {
        m_ui.colorSpace->addItem(iconManager ? iconManager->getIcon(entry.icon) : QIcon(), QLatin1String(entry.label));
    }
    m_ui.colorSpace->setCurrentText(QLatin1String(kSpaces.front().label));

    initialize();
    initializeTopLevelProcess();
}

ChannelCompositionProcess::~ChannelCompositionProcess()
{
    m_run->stop.request_stop();
}

std::string ChannelCompositionProcess::getProcessNameStatic()
{
    return "Channel Composition";
}

void ChannelCompositionProcess::refreshTheme()
{
    TopLevelProcessBaseControl::refreshTheme();
}

bool ChannelCompositionProcess::isRunning() const noexcept
{
    return m_run->running;
}

std::uint64_t ChannelCompositionProcess::runsPublished() const noexcept
{
    return m_run->published;
}

std::uint64_t ChannelCompositionProcess::runsFailed() const noexcept
{
    return m_run->failed;
}

void ChannelCompositionProcess::requestStop()
{
    m_run->stop.request_stop();
}

bool ChannelCompositionProcess::isComponentViewSupported(const ImageViewControl* ctrl) const
{
    if (ctrl == nullptr || !ctrl->hasImage())
    {
        return false;
    }
    const std::optional<an::ImageDescriptor> described = ctrl->document().asset()->descriptor();
    return described.has_value() && described->extent().channelCount() == 1u;
}

void ChannelCompositionProcess::addViewToSelectors(const ImageViewControl* ctrl)
{
    const QString imageName = QString::fromUtf8(ctrl->getUniqueName());
    for (QComboBox* selector : {m_ui.redChannel, m_ui.greenChannel, m_ui.blueChannel})
    {
        if (selector->findText(imageName) == -1)
        {
            selector->addItem(imageName);
            selector->setItemData(selector->count() - 1, an::ui::buildImageSelectorTooltip(ctrl), Qt::ToolTipRole);
        }
    }
}

void ChannelCompositionProcess::removeViewFromSelectors(const QString& name)
{
    for (QComboBox* selector : {m_ui.redChannel, m_ui.greenChannel, m_ui.blueChannel})
    {
        const int idx = selector->findText(name);
        if (idx != -1)
        {
            if (selector->currentText() == name)
            {
                selector->setCurrentText(NO_IMAGE_STR);
            }
            selector->removeItem(idx);
        }
    }
}

void ChannelCompositionProcess::onViewControlCreated(const ImageViewControl* ctrl)
{
    if (isComponentViewSupported(ctrl))
    {
        addViewToSelectors(ctrl);
    }
}

void ChannelCompositionProcess::onViewControlDestroyed(const ImageViewControl* ctrl)
{
    removeViewFromSelectors(QString::fromUtf8(ctrl->getUniqueName()));
    validateSelectedImages();
}

void ChannelCompositionProcess::onViewControlRenamed(const std::string& newName, const std::string& oldName,
                                                     const ImageViewControl* ctrl)
{
    const QString newImageName = QString::fromUtf8(newName);
    const QString oldImageName = QString::fromUtf8(oldName);
    for (QComboBox* selector : {m_ui.redChannel, m_ui.greenChannel, m_ui.blueChannel})
    {
        const int idx = selector->findText(oldImageName);
        if (idx != -1)
        {
            selector->setItemText(idx, newImageName);
            selector->setItemData(idx, an::ui::buildImageSelectorTooltip(ctrl), Qt::ToolTipRole);
        }
    }
    validateSelectedImages();
}

void ChannelCompositionProcess::onViewControlUpdated(const ImageViewControl* ctrl)
{
    if (isComponentViewSupported(ctrl))
    {
        addViewToSelectors(ctrl);
    }
    else
    {
        removeViewFromSelectors(QString::fromUtf8(ctrl->getUniqueName()));
    }
    validateSelectedImages();
}

void ChannelCompositionProcess::on_colorSpace_currentTextChanged(const QString& text)
{
    const std::optional<composition_detail::ColourSpace> space = composition_detail::colourSpaceFromLabel(text);
    if (!space.has_value())
    {
        return;
    }
    const std::array<const char*, 3u> names = composition_detail::componentNames(*space);
    m_ui.channel0Label->setText(QLatin1String(names[0]));
    m_ui.channel1Label->setText(QLatin1String(names[1]));
    m_ui.channel2Label->setText(QLatin1String(names[2]));
}

void ChannelCompositionProcess::initialize()
{
    for (QComboBox* selector : {m_ui.redChannel, m_ui.greenChannel, m_ui.blueChannel})
    {
        selector->addItem(NO_IMAGE_STR);
    }
    for (auto* viewControl : process_utils::listViewControls())
    {
        if (isComponentViewSupported(viewControl))
        {
            addViewToSelectors(viewControl);
        }
    }
    for (QComboBox* selector : {m_ui.redChannel, m_ui.greenChannel, m_ui.blueChannel})
    {
        selector->setCurrentText(NO_IMAGE_STR);
    }
    m_ui.buttonBox->button(QDialogButtonBox::Ok)->setEnabled(false);
}

void ChannelCompositionProcess::validateSelectedImages()
{
    const QString redStr = m_ui.redChannel->currentText();
    m_ui.buttonBox->button(QDialogButtonBox::Ok)
        ->setEnabled(!m_run->running && redStr != NO_IMAGE_STR && !redStr.isEmpty());
}

void ChannelCompositionProcess::finishRun(an::Status outcome, bool published)
{
    m_run->running = false;
    if (published)
    {
        ++m_run->published;
    }
    else
    {
        ++m_run->failed;
    }
    m_ui.buttonBox->button(QDialogButtonBox::Close)->setEnabled(true);
    validateSelectedImages();
    if (outcome.failed())
    {
        if (outcome.code() != an::ErrorCode::Cancelled)
        {
            const std::string text =
                fmt::format("An error occurred when executing the Channel Composition process:\n{}", outcome.message());
            LOG_WARNING("{}", text);
            process_utils::notifyErrorMessage(this, "Error running Channel Composition", QString::fromUtf8(text));
        }
        return;
    }
    if (published)
    {
        close();
    }
}

void ChannelCompositionProcess::executeChannelComposition()
{
    if (m_run->running)
    {
        return;
    }
    // What the run reads is chosen here: the components' assets, or their sequences, and the names it records.
    struct Component
    {
        std::string name = "-";
        std::shared_ptr<const an::consumer::ImageAsset> asset;
        std::optional<an::ui::DocumentSequence> sequence;
        ImageViewControl* view = nullptr;
    };
    std::array<Component, 3u> components;
    bool anySequence = false;
    std::size_t index = 0u;
    for (QComboBox* selector : {m_ui.redChannel, m_ui.greenChannel, m_ui.blueChannel})
    {
        const QString text = selector->currentText();
        if (text != NO_IMAGE_STR && !text.isEmpty())
        {
            ImageViewControl* view = process_utils::findViewControlByName(text);
            if (view == nullptr || !view->hasImage())
            {
                process_utils::notifyErrorMessage(
                    this, "Error running Channel Composition",
                    QStringLiteral("The channel component '%1' is not available").arg(text));
                ++m_run->failed;
                return;
            }
            components[index].name = text.toStdString();
            components[index].asset = view->document().asset();
            components[index].sequence = view->document().sequence();
            components[index].view = view;
            anySequence = anySequence || view->isSequence();
        }
        ++index;
    }
    if (!components[0].asset)
    {
        process_utils::notifyErrorMessage(this, "Error running Channel Composition",
                                          QStringLiteral("The first channel component is required"));
        ++m_run->failed;
        return;
    }
    if (anySequence)
    {
        for (const Component& component : components)
        {
            if (component.asset && !component.sequence.has_value())
            {
                process_utils::notifyErrorMessage(
                    this, "Error running Channel Composition",
                    QStringLiteral("All the selected images must be either sequences or images"));
                ++m_run->failed;
                return;
            }
        }
    }
    const std::optional<composition_detail::ColourSpace> space =
        composition_detail::colourSpaceFromLabel(m_ui.colorSpace->currentText());
    if (!space.has_value())
    {
        return;
    }
    auto* context = an::ui::appContext();
    an::ui::IEngineSessionProvider* provider = context ? context->getEngineSessionProvider() : nullptr;
    if (provider == nullptr)
    {
        return;
    }
    const an::consumer::EngineSession* session = &provider->engineSession();

    m_run->running = true;
    m_run->stop = std::stop_source();
    m_ui.buttonBox->button(QDialogButtonBox::Ok)->setEnabled(false);
    m_ui.buttonBox->button(QDialogButtonBox::Close)->setEnabled(false);

    ProgressDialogControl* progress = nullptr;
    if (anySequence)
    {
        progress = new ProgressDialogControl(process_utils::currentMainWindow(), "Channel Composition");
        progress->setText("Executing Channel Composition");
        progress->setProgressBarRange(0, 100);
        progress->show();
    }
    QPointer<ChannelCompositionProcess> guard(this);
    QPointer<ProgressDialogControl> dialog(progress);
    const std::stop_token windowStop = m_run->stop.get_token();
    const std::stop_token dialogStop = progress ? progress->stopToken() : std::stop_token{};
    const bool linearMatch = m_ui.linearMatchCheckBox->isChecked();
    const composition_detail::ColourSpace chosen = *space;
    ImageViewControl* neighbour = components[0].view;
    const std::string tempPath =
        anySequence ? process_utils::createTempSequenceFilePath(components[0].sequence->descriptor.format)
                    : std::string();
    const std::function<void(an::ui::ExecutionScope&)> hook = admissionHook();

    const std::stop_token processStop = windowStopToken();
    QThreadPool::globalInstance()->start([session, components, guard, dialog, windowStop, dialogStop, processStop,
                                          linearMatch, chosen, neighbour, anySequence, tempPath, hook]() {
        // One stop for the run's Stop, the window's going and the dialog's Cancel.
        std::stop_source stop;
        const std::stop_callback windowCallback(windowStop, [&stop]() { stop.request_stop(); });
        const std::stop_callback processCallback(processStop, [&stop]() { stop.request_stop(); });
        const std::stop_callback dialogCallback(dialogStop, [&stop]() { stop.request_stop(); });

        std::shared_ptr<an::consumer::ImageAsset> still;
        std::optional<an::ui::DocumentSequence> written;
        an::Status outcome = an::Status::success();
        // A still composition's components are other views' images, leased before it is admitted, one lease per view
        // however many channels it fills: an edit of a view holds its writer while it waits to be admitted.
        an::ui::ImageReads reads;
        if (!anySequence)
        {
            for (const auto& component : components)
            {
                reads.name(component.asset);
            }
        }
        outcome = reads.take(stop.get_token());
        an::Result<an::ui::ExecutionScope> admitted =
            outcome.failed() ? an::Result<an::ui::ExecutionScope>(outcome)
                             : an::ui::ExecutionScope::admit(
                                   *session,
                                   an::ui::ExecutionRequest{.purpose = an::consumer::OperationPurpose::Apply,
                                                            .stopToken = stop.get_token(),
                                                            .reads = &reads},
                                   an::ui::ExecutionObserver{});
        if (!admitted.hasValue())
        {
            outcome = admitted.status();
        }
        else
        {
            an::ui::ExecutionScope& scope = *admitted;
            if (hook)
            {
                hook(scope);
            }
            composition_detail::CompositionRequest request;
            request.space = chosen;
            request.linearMatch = linearMatch;
            for (std::size_t index = 0u; index < 3u; ++index)
            {
                request.names[index] = components[index].name;
            }
            if (!anySequence)
            {
                std::array<std::optional<an::consumer::AssetReadLease>, 3u> leases;
                std::array<std::optional<an::ui::ProcessSource>, 3u> sources;
                for (std::size_t index = 0u; index < 3u && outcome.ok(); ++index)
                {
                    if (!components[index].asset)
                    {
                        continue;
                    }
                    an::Result<an::consumer::AssetReadLease> lease =
                        an::ui::readBeside(scope, *components[index].asset, scope.execution().stopToken());
                    if (!lease.hasValue())
                    {
                        outcome = lease.status();
                        break;
                    }
                    leases[index].emplace(std::move(*lease));
                    sources[index].emplace(an::ui::ProcessSource::of(*leases[index]));
                    request.components[index] = &*sources[index];
                }
                if (outcome.ok())
                {
                    an::Result<an::consumer::ImageAsset> composed =
                        composition_detail::composeChannelImages(scope, request);
                    if (!composed.hasValue())
                    {
                        outcome = composed.status();
                    }
                    else
                    {
                        still = std::make_shared<an::consumer::ImageAsset>(std::move(*composed));
                    }
                }
            }
            else
            {
                // Three sequences frame by frame, a frame missing from a shorter one a zero plane, written into a new
                // sequence beside the first component's, published only when every frame is written.
                std::array<std::unique_ptr<an::ui::IProcessFrameReader>, 3u> readers;
                std::uint64_t total = 0u;
                for (std::size_t index = 0u; index < 3u && outcome.ok(); ++index)
                {
                    if (!components[index].sequence.has_value())
                    {
                        continue;
                    }
                    an::Result<std::unique_ptr<an::ui::IProcessFrameReader>> reader =
                        an::ui::openProcessFrames(*session, *components[index].sequence, false);
                    if (!reader.hasValue())
                    {
                        outcome = reader.status();
                        break;
                    }
                    readers[index] = std::move(*reader);
                    total = (std::max)(total, readers[index]->frameCount());
                }
                std::unique_ptr<an::ui::IProcessFrameWriter> writer;
                if (outcome.ok())
                {
                    an::Result<std::unique_ptr<an::ui::IProcessFrameWriter>> made = an::ui::createProcessFrameOutput(
                        *session, *components[0].sequence, QString::fromStdString(tempPath));
                    if (!made.hasValue())
                    {
                        outcome = made.status();
                    }
                    else
                    {
                        writer = std::move(*made);
                    }
                }
                for (std::uint64_t frame = 0u; outcome.ok() && readers[0]; ++frame)
                {
                    std::array<std::optional<an::consumer::ImageAsset>, 3u> frames;
                    std::array<std::optional<an::consumer::AssetReadLease>, 3u> leases;
                    std::array<std::optional<an::ui::ProcessSource>, 3u> sources;
                    bool ended = false;
                    for (std::size_t index = 0u; index < 3u; ++index)
                    {
                        request.components[index] = nullptr;
                        if (!readers[index])
                        {
                            continue;
                        }
                        an::Result<std::optional<an::ui::ProcessSequenceFrame>> next =
                            readers[index]->next(scope.engine(), scope.execution());
                        if (!next.hasValue())
                        {
                            outcome = next.status();
                            break;
                        }
                        if (!next->has_value())
                        {
                            ended = ended || index == 0u;
                            continue;
                        }
                        frames[index].emplace(std::move((*next)->asset));
                        an::Result<an::consumer::AssetReadLease> lease =
                            frames[index]->read(scope.execution().stopToken());
                        if (!lease.hasValue())
                        {
                            outcome = lease.status();
                            break;
                        }
                        leases[index].emplace(std::move(*lease));
                        sources[index].emplace(an::ui::ProcessSource::of(*leases[index]));
                        request.components[index] = &*sources[index];
                    }
                    if (outcome.failed() || ended)
                    {
                        break;
                    }
                    an::Result<an::consumer::ImageAsset> composed =
                        composition_detail::composeChannelImages(scope, request);
                    if (!composed.hasValue())
                    {
                        outcome = composed.status();
                        break;
                    }
                    an::Result<an::consumer::AssetReadLease> composedLease =
                        composed->read(scope.execution().stopToken());
                    if (!composedLease.hasValue())
                    {
                        outcome = composedLease.status();
                        break;
                    }
                    outcome = writer->write(scope.engine(), scope.execution(), frame, composedLease->image());
                    if (total > 0u && dialog)
                    {
                        const int percent = static_cast<int>(((frame + 1u) * 100u) / total);
                        postToGui([dialog, percent]() {
                            if (dialog)
                            {
                                dialog->setValue(percent);
                            }
                        });
                    }
                }
                if (outcome.ok() && writer)
                {
                    an::Result<an::ui::DocumentSequence> finished = writer->finish(scope.engine(), scope.execution());
                    if (!finished.hasValue())
                    {
                        outcome = finished.status();
                    }
                    else
                    {
                        written.emplace(std::move(*finished));
                    }
                }
            }
            if (outcome.ok() && stop.stop_requested())
            {
                outcome = refusal(an::ErrorCode::Cancelled, "the composition was stopped");
            }
            scope.finish(outcome);
        }
        std::optional<an::ui::ImageDocument> sequenceDocument;
        if (outcome.ok() && written.has_value())
        {
            an::Result<an::ui::ImageDocument> opened = an::ui::openSequenceDocument(
                *session, written->sourcePath, written->descriptor.format, stop.get_token());
            if (!opened.hasValue())
            {
                outcome = opened.status();
            }
            else
            {
                sequenceDocument.emplace(std::move(*opened));
            }
        }
        auto landing = std::make_shared<
            std::pair<std::shared_ptr<an::consumer::ImageAsset>, std::optional<an::ui::ImageDocument>>>(
            std::move(still), std::move(sequenceDocument));
        postToGui([guard, dialog, outcome, landing, chosen, neighbour, components]() {
            if (dialog)
            {
                dialog->closeAsync();
            }
            if (!guard)
            {
                return;
            }
            bool published = false;
            if (outcome.ok())
            {
                composition_detail::CompositionRequest named;
                named.space = chosen;
                for (std::size_t index = 0u; index < 3u; ++index)
                {
                    named.names[index] = components[index].name;
                }
                const std::string description = composition_detail::compositionDescription(named);
                an::ui::IViewFactory* factory = process_utils::currentViewFactory();
                ImageViewControl* beside =
                    neighbour != nullptr && process_utils::findViewControlByName(
                                                QString::fromStdString(components[0].name)) == neighbour
                        ? neighbour
                        : nullptr;
                if (factory != nullptr && landing->first)
                {
                    an::Result<an::ui::ImageDocument> document = an::ui::ImageDocument::create(
                        std::move(*landing->first),
                        an::ui::DocumentNames{.displayName =
                                                  QLatin1String(composition_detail::composedImageName(chosen))});
                    if (document.hasValue())
                    {
                        published = factory->createNewImageViewControlFromImage(nullptr, std::move(*document), nullptr,
                                                                                description, ViewControlMode::standard,
                                                                                beside) != nullptr;
                    }
                }
                else if (factory != nullptr && landing->second.has_value())
                {
                    published = factory->createNewImageViewControlFromSequence(
                                    nullptr, std::move(*landing->second), nullptr, false, description,
                                    ViewControlMode::standard, beside) != nullptr;
                }
            }
            guard->finishRun(published
                                 ? an::Status::success()
                                 : (outcome.ok() ? refusal(an::ErrorCode::Internal, "no view could be made") : outcome),
                             published);
        });
    });
}

void ChannelCompositionProcess::on_dialog_box_button_clicked(QAbstractButton* button)
{
    QDialogButtonBox* buttonBox = qobject_cast<QDialogButtonBox*>(sender());
    if (!buttonBox)
    {
        return;
    }
    const QDialogButtonBox::StandardButton sb = buttonBox->standardButton(button);
    if (sb == QDialogButtonBox::Ok)
    {
        executeChannelComposition();
    }
    else if (sb == QDialogButtonBox::Close)
    {
        this->close();
    }
}
