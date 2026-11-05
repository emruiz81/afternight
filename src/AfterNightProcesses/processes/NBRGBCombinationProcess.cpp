// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The NBRGB Combination window: the narrowband selectors, bandwidths and scales snapshotted on the GUI thread, and
/// one engine operation on the run's worker over the target's planes in fractions of full scale, with each narrowband
/// input read as a unit-fraction copy in the same working representation.

#include "stdafx.hpp"
#include "NBRGBCombinationProcess.h"

#include "controls/ImageSelectorTooltipBuilder.h"
#include "controls/ImageViewControl.h"
#include "controls/ProcessButtonIcons.h"
#include "processes/CombinationProcessSupport.h"
#include "processes/MaskProcessUtils.h"
#include "processes/PointwiseProcessUtils.h"
#include "processes/ProcessCommonUtils.h"

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Image/ChannelTransfer.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include <QSignalBlocker>

#include <cmath>
#include <utility>
#include <vector>

namespace
{

constexpr double kMinimumBandwidthDenominator = 1.0e-6;

[[nodiscard]] an::Status refusal(an::ErrorCode code, std::string message)
{
    return an::Status::failure(code, std::move(message));
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

void restoreSelection(QComboBox* selector, const QString& preferred)
{
    if (!selector)
    {
        return;
    }
    const QString selection = preferred.isEmpty() ? NO_IMAGE_STR : preferred;
    const int index = selector->findText(selection);
    selector->setCurrentText(index >= 0 ? selection : NO_IMAGE_STR);
}

[[nodiscard]] bool channelReady(const an::NarrowbandRgbChannel& channel, const double rgbBandwidth)
{
    if (!channel.enabled)
    {
        return true;
    }
    if (!std::isfinite(channel.bandwidthNanometres) || channel.bandwidthNanometres <= 0.0)
    {
        return false;
    }
    const double denominator = 1.0 - (channel.bandwidthNanometres / rgbBandwidth);
    if (denominator <= kMinimumBandwidthDenominator)
    {
        return false;
    }
    return std::isfinite(channel.scale) && channel.scale >= 0.0;
}

/// One narrowband input read for the run: the lease on the window's snapshot, its unit-fraction copy in the working
/// representation and the lease on that.
struct NarrowbandInput
{
    process_utils::LeasedImage leased;
    an::consumer::ImageAsset copy;
    an::consumer::AssetReadLease copyLease;
    std::unique_ptr<process_utils::BoundImage> bound;
};

/// A grey image shown as three equal channels, the preview's view of a narrowband input.
[[nodiscard]] an::Result<an::consumer::ImageAsset> asColour(const an::ui::ExecutionScope& scope,
                                                            const an::ui::ProcessSource& grey)
{
    const std::optional<an::ImageDescriptor> described = grey.descriptor();
    if (!described.has_value())
    {
        return refusal(an::ErrorCode::NotInitialized, "the image holds nothing");
    }
    if (described->extent().channelCount() == 3u)
    {
        return process_utils::unitFloatCopy(scope, grey, process_utils::normalizedWorkingType(described->sampleType()));
    }
    an::Result<an::consumer::ImageAsset> plane =
        process_utils::unitFloatCopy(scope, grey, process_utils::normalizedWorkingType(described->sampleType()));
    if (!plane.hasValue())
    {
        return plane.status();
    }
    an::Result<an::consumer::AssetReadLease> lease = plane->read(scope.execution().stopToken());
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
    const an::Result<an::ImageDescriptor> colour = process_utils::compositionDescriptor(
        (*bound)->descriptor(), 3u, (*bound)->descriptor().sampleType(), scope.engine().limits());
    if (!colour.hasValue())
    {
        return colour.status();
    }
    an::ImageMetadata metadata = grey.metadata();
    const an::ChannelSource source{.samples = (*bound)->samples(), .layout = &(*bound)->layout()};
    const std::array<an::ChannelSource, 3u> sources{source, source, source};
    return process_utils::composeImage(
        scope, *colour, std::move(metadata), [&](void* samples, const an::ImageLayout& layout) -> an::Status {
            return an::composeImageChannels(scope.execution(), colour->sampleType(), sources, samples, layout);
        });
}

} // namespace

namespace an::ui::processes::nbrgb_detail
{

bool combinationReady(const NBRGBCombinationProcessParams& params, const an::ImageDescriptor& target)
{
    if (!process_utils::describesColourImage(target))
    {
        return false;
    }
    const an::NarrowbandRgbRequest& request = params.engine;
    if (!std::isfinite(request.broadbandBandwidthNanometres) || request.broadbandBandwidthNanometres <= 0.0)
    {
        return false;
    }
    if (!std::isfinite(request.fitLow) || !std::isfinite(request.fitHigh) || request.fitLow < 0.0 ||
        request.fitHigh > 1.0 || request.fitHigh <= request.fitLow)
    {
        return false;
    }
    bool any = false;
    for (std::size_t channel = 0u; channel < 3u; ++channel)
    {
        const bool enabled = request.channels[channel].enabled && params.narrowband[channel] != nullptr;
        if (!enabled)
        {
            continue;
        }
        any = true;
        const std::optional<an::ImageDescriptor> described = params.narrowband[channel]->descriptor();
        if (!described.has_value() || described->extent().channelCount() != 1u ||
            !process_utils::sameSize(*described, target))
        {
            return false;
        }
        if (!channelReady(request.channels[channel], request.broadbandBandwidthNanometres))
        {
            return false;
        }
    }
    return any;
}

} // namespace an::ui::processes::nbrgb_detail

NBRGBCombinationProcess::NBRGBCombinationProcess(ImageViewControl* control)
    : an::ui::TypedProcessControl<NBRGBCombinationProcessParams>(control, getProcessNameStatic())
{
    m_ui = an::ui::processes::buildNBRGBCombinationProcessUi(this);
    setProcessDescription(QStringLiteral("Combine an RGB image with optional mono narrowband channels."));

    m_SupportTargetViewControlSelection = true;
    m_SupportPerChannelProcessing = false;
    m_FixedLayout = false;
    m_ShowWaitingSpinner = false;
    m_CreatedViewControlsAreNeighbour = true;
    m_ImagePreviewLayout = m_ui.previewLayout;
    m_MainHorizontalLayout = m_ui.horizontalLayoutMain;

    initializeStandardProcess(m_ui.horizontalLayoutMain, m_ui.previewLayout, nullptr, m_ui.buttonBox,
                              m_ui.rgbImageSelector, nullptr, ImageRTPreviewControl::ProcessInitOptions{});

    connectUi();
    updateWindowTitle();

    setupHistogramPreview(m_ui.chartScrollArea, m_ui.plotResolution, m_ui.zoomIn, m_ui.zoomOut, m_ui.zoomReset,
                          m_ui.gridButton, m_ui.localChannelNormalization, m_ui.showClippingInfo, m_ui.showHistogram,
                          m_ui.showHistogramAreas);

    rebuildNarrowbandSelectors();
    setExecutionAutostretchSupported(true);
    setupPreviewControl(true, QSize(512, 512), QStringLiteral("NBRGB preview"));
    resetSettings();
    settingsUpdated();
    refreshTheme();
}

NBRGBCombinationProcess::~NBRGBCombinationProcess() = default;

std::string NBRGBCombinationProcess::getProcessNameStatic()
{
    return "NBRGB Combination";
}

bool NBRGBCombinationProcess::requiresExclusiveAccessStatic()
{
    return false;
}

void NBRGBCombinationProcess::connectUi()
{
    const auto update = [this]() {
        if (!m_ignoreSelectorUpdates)
        {
            settingsUpdated();
        }
    };
    for (auto* selector :
         {m_ui.redImageSelector, m_ui.greenImageSelector, m_ui.blueImageSelector, m_ui.previewDisplayMode})
    {
        QObject::connect(selector, &QComboBox::currentTextChanged, this, update);
    }
    for (auto* spin : {m_ui.rgbBandwidth, m_ui.redBandwidth, m_ui.greenBandwidth, m_ui.blueBandwidth, m_ui.redScale,
                       m_ui.greenScale, m_ui.blueScale, m_ui.rejectLow, m_ui.rejectHigh})
    {
        QObject::connect(spin, qOverload<double>(&QDoubleSpinBox::valueChanged), this, update);
    }
    QObject::connect(m_ui.nonLinearRescale, &QCheckBox::checkStateChanged, this, update);
}

void NBRGBCombinationProcess::refreshTheme()
{
    ImageProcessControl::refreshTheme();
    if (const auto iconProvider = process_utils::currentIconProvider())
    {
        process_utils::applyDialogButtonBoxIcons(m_ui.buttonBox, iconProvider.get());
    }
}

void NBRGBCombinationProcess::onViewControlCreated(const ImageViewControl* ctrl)
{
    ImageRTPreviewControl::onViewControlCreated(ctrl);
    if (isNarrowbandViewSupported(ctrl))
    {
        addImageItem(m_ui.redImageSelector, ctrl);
        addImageItem(m_ui.greenImageSelector, ctrl);
        addImageItem(m_ui.blueImageSelector, ctrl);
    }
}

void NBRGBCombinationProcess::onViewControlDestroyed(const ImageViewControl* ctrl)
{
    ImageRTPreviewControl::onViewControlDestroyed(ctrl);
    const QString name = QString::fromUtf8(ctrl->getUniqueName());
    for (auto* selector : {m_ui.redImageSelector, m_ui.greenImageSelector, m_ui.blueImageSelector})
    {
        const int index = selector->findText(name);
        if (index < 0)
        {
            continue;
        }
        if (selector->currentText() == name)
        {
            selector->setCurrentText(NO_IMAGE_STR);
        }
        selector->removeItem(index);
    }
}

void NBRGBCombinationProcess::onViewControlRenamed(const std::string& newName, const std::string& oldName,
                                                   const ImageViewControl* ctrl)
{
    ImageRTPreviewControl::onViewControlRenamed(newName, oldName, ctrl);
    const QString oldNameQt = QString::fromUtf8(oldName);
    const QString newNameQt = QString::fromUtf8(newName);
    for (auto* selector : {m_ui.redImageSelector, m_ui.greenImageSelector, m_ui.blueImageSelector})
    {
        const int index = selector->findText(oldNameQt);
        if (index >= 0)
        {
            selector->setItemText(index, newNameQt);
            selector->setItemData(index, an::ui::buildImageSelectorTooltip(ctrl), Qt::ToolTipRole);
        }
    }
}

void NBRGBCombinationProcess::onViewControlUpdated(const ImageViewControl* ctrl)
{
    ImageRTPreviewControl::onViewControlUpdated(ctrl);
    const QString red = m_ui.redImageSelector->currentText();
    const QString green = m_ui.greenImageSelector->currentText();
    const QString blue = m_ui.blueImageSelector->currentText();
    const QString updatedName = ctrl ? QString::fromUtf8(ctrl->getUniqueName()) : QString();
    const bool targetUpdated = ctrl == m_image_view_control;
    const bool selectedUpdated =
        !updatedName.isEmpty() && (red == updatedName || green == updatedName || blue == updatedName);
    rebuildNarrowbandSelectors(red, green, blue);
    const bool selectionChanged = red != m_ui.redImageSelector->currentText() ||
                                  green != m_ui.greenImageSelector->currentText() ||
                                  blue != m_ui.blueImageSelector->currentText();
    if (targetUpdated || selectedUpdated || selectionChanged)
    {
        settingsUpdated();
    }
}

bool NBRGBCombinationProcess::isPreviewEnabled() const
{
    return true;
}

void NBRGBCombinationProcess::enablePreview(bool /*enabled*/) {}

void NBRGBCombinationProcess::on_targetImageSelector_currentTextChangedInternal(const QString& /*str*/)
{
    rebuildNarrowbandSelectors();
    settingsUpdated();
}

an::Status NBRGBCombinationProcess::executeTypedProcessImpl(const an::ui::ProcessSource& source,
                                                            an::ui::ProcessDestination& destination,
                                                            const an::ui::ProcessRun& run,
                                                            const NBRGBCombinationProcessParams& params,
                                                            an::ui::ExecutionScope& scope)
{
    const std::optional<an::ImageDescriptor> targetDescribed = source.descriptor();
    if (!targetDescribed.has_value())
    {
        return refusal(an::ErrorCode::NotInitialized, "the target holds nothing");
    }
    if (run.isForSequence())
    {
        return refusal(an::ErrorCode::Unsupported, "the NBRGB combination needs a still image");
    }
    // A preview may show one input instead of the combination: the target itself, or a narrowband image as colour.
    if (run.isForPreview() && params.previewDisplay != NBRGBPreviewDisplay::Combination)
    {
        if (params.previewDisplay == NBRGBPreviewDisplay::Rgb)
        {
            return an::Status::success();
        }
        const std::size_t channel = static_cast<std::size_t>(params.previewDisplay) - 2u;
        if (!params.narrowband[channel])
        {
            return an::Status::success();
        }
        an::Result<process_utils::LeasedImage> shown = process_utils::LeasedImage::read(
            scope, *params.narrowband[channel], run, params.previewArea, *targetDescribed);
        if (!shown.hasValue())
        {
            return shown.status();
        }
        an::Result<an::consumer::ImageAsset> colour = asColour(scope, shown->source());
        if (!colour.hasValue())
        {
            return colour.status();
        }
        return destination.replace(std::move(*colour));
    }
    if (!an::ui::processes::nbrgb_detail::combinationReady(params, *targetDescribed))
    {
        return run.isForPreview()
                   ? an::Status::success()
                   : refusal(an::ErrorCode::InvalidArgument,
                             "select at least one grey narrowband image of the target's size, with a bandwidth below "
                             "the RGB bandwidth and a fit interval inside the unit interval");
    }

    // The target in fractions of full scale, worked on in place; each narrowband image as a unit-fraction copy in
    // the same working representation.
    an::Result<process_utils::NormalizedSamples> target =
        process_utils::openNormalizedDestination(scope, source, destination);
    if (!target.hasValue())
    {
        return target.status();
    }
    const an::SampleType working = target->sampleType();
    an::NarrowbandRgbRequest request = params.engine;
    std::vector<std::unique_ptr<NarrowbandInput>> inputs;
    std::vector<const an::ImageMetadata*> sources{&source.metadata()};
    for (std::size_t channel = 0u; channel < 3u; ++channel)
    {
        request.channels[channel].samples = nullptr;
        request.channels[channel].layout = nullptr;
        if (!request.channels[channel].enabled || !params.narrowband[channel])
        {
            request.channels[channel].enabled = false;
            continue;
        }
        an::Result<process_utils::LeasedImage> leased = process_utils::LeasedImage::read(
            scope, *params.narrowband[channel], run, params.previewArea, *targetDescribed);
        if (!leased.hasValue())
        {
            return leased.status();
        }
        an::Result<an::consumer::ImageAsset> copy = process_utils::unitFloatCopy(scope, leased->source(), working);
        if (!copy.hasValue())
        {
            return copy.status();
        }
        an::Result<an::consumer::AssetReadLease> copyLease = copy->read(scope.execution().stopToken());
        if (!copyLease.hasValue())
        {
            return copyLease.status();
        }
        auto input = std::make_unique<NarrowbandInput>(NarrowbandInput{.leased = std::move(*leased),
                                                                       .copy = std::move(*copy),
                                                                       .copyLease = std::move(*copyLease),
                                                                       .bound = nullptr});
        an::Result<std::unique_ptr<process_utils::BoundImage>> bound =
            process_utils::BoundImage::bind(an::ui::ProcessSource::of(input->copyLease));
        if (!bound.hasValue())
        {
            return bound.status();
        }
        input->bound = std::move(*bound);
        request.channels[channel].samples = input->bound->samples();
        request.channels[channel].layout = &input->bound->layout();
        sources.push_back(&input->leased.source().metadata());
        inputs.push_back(std::move(input));
    }
    const an::KernelValidityView validity = process_utils::skippingNonFinite(target->validity());
    const an::Result<an::NarrowbandRgbResult> ran =
        an::combineNarrowbandRgb(scope.execution(), working, target->samples(), target->layout(), target->samples(),
                                 target->layout(), request, validity);
    if (!ran.hasValue())
    {
        return ran.status();
    }
    if (an::Status committed = target->commit(scope.execution()); committed.failed())
    {
        return committed;
    }
    if (run.isForPreview())
    {
        return an::Status::success();
    }
    const std::optional<an::ImageDescriptor> resultDescribed =
        an::ui::ProcessSource::of(destination.asset()).descriptor();
    if (!resultDescribed.has_value())
    {
        return refusal(an::ErrorCode::NotInitialized, "the result holds nothing");
    }
    an::Result<an::ImageMetadata> metadata = process_utils::combinedResultMetadata(
        sources, an::kNarrowbandRgbEffect, *resultDescribed,
        process_utils::CombinationRecord{.stateOperation = "nbrgb-combination",
                                         .engineOperation = "an::combineNarrowbandRgb",
                                         .detail = describeTypedProcessStep(params).description,
                                         .linearity = process_utils::CombinationLinearity::AsSources});
    if (!metadata.hasValue())
    {
        return metadata.status();
    }
    return process_utils::restateMetadata(scope, destination, std::move(*metadata));
}

an::ui::ProcessStep NBRGBCombinationProcess::describeTypedProcessStep(const NBRGBCombinationProcessParams& params) const
{
    std::string channels;
    const char* names[3] = {"R", "G", "B"};
    for (std::size_t channel = 0u; channel < 3u; ++channel)
    {
        if (params.engine.channels[channel].enabled && params.narrowband[channel])
        {
            channels +=
                fmt::format("{}{}: '{}' {:.1f} nm x{:.2f}", channels.empty() ? "" : ", ", names[channel],
                            params.narrowbandNames[channel], params.engine.channels[channel].bandwidthNanometres,
                            params.engine.channels[channel].scale);
        }
    }
    return an::ui::ProcessStep{
        .name = getProcessName(),
        .description = fmt::format("Created NBRGB combination from RGB ({:.1f} nm) and narrowband channels {}; fit "
                                   "{:.3f} to {:.3f}{}",
                                   params.engine.broadbandBandwidthNanometres, channels, params.engine.fitLow,
                                   params.engine.fitHigh, params.engine.rescaleNonlinear ? ", rescaled" : "")};
}

// The narrowband images: other views' images the body reads, leased before the run is admitted.
std::vector<std::shared_ptr<const an::consumer::ImageAsset>>
NBRGBCombinationProcess::typedImagesRead(const NBRGBCombinationProcessParams& params) const
{
    return {params.narrowband.begin(), params.narrowband.end()};
}

void NBRGBCombinationProcess::getBatchExecutionParameters(bool& createNewImage, bool& closeAtEnd,
                                                          bool& executeInBackground, std::string& newImageName)
{
    createNewImage = true;
    closeAtEnd = false;
    executeInBackground = true;
    newImageName = m_image_view_control ? (m_image_view_control->getUniqueName() + " - NBRGB") : "NBRGB";
}

void NBRGBCombinationProcess::resetSettings()
{
    ImageRTPreviewControl::resetSettings();
    m_ui.rgbBandwidth->setValue(100.0);
    m_ui.redBandwidth->setValue(7.0);
    m_ui.greenBandwidth->setValue(8.5);
    m_ui.blueBandwidth->setValue(8.5);
    m_ui.redScale->setValue(1.20);
    m_ui.greenScale->setValue(1.20);
    m_ui.blueScale->setValue(1.20);
    m_ui.rejectLow->setValue(0.0);
    m_ui.rejectHigh->setValue(0.92);
    m_ui.nonLinearRescale->setChecked(false);
    m_ui.previewDisplayMode->setCurrentIndex(static_cast<int>(NBRGBPreviewDisplay::Combination));
}

void NBRGBCombinationProcess::settingsUpdatedImpl()
{
    if (auto* applyButton = m_ui.buttonBox->button(QDialogButtonBox::Apply))
    {
        bool ready = false;
        if (m_image_view_control != nullptr && m_image_view_control->hasImage() && !m_image_view_control->isSequence())
        {
            const std::optional<an::ImageDescriptor> target = m_image_view_control->document().asset()->descriptor();
            ready = target.has_value() && an::ui::processes::nbrgb_detail::combinationReady(getParams(), *target);
        }
        applyButton->setEnabled(ready);
    }
}

NBRGBCombinationProcessParams NBRGBCombinationProcess::getParams() const
{
    NBRGBCombinationProcessParams params;
    params.engine.broadbandBandwidthNanometres = m_ui.rgbBandwidth->value();
    params.engine.fitLow = m_ui.rejectLow->value();
    params.engine.fitHigh = m_ui.rejectHigh->value();
    params.engine.rescaleNonlinear = m_ui.nonLinearRescale->isChecked();
    params.previewDisplay = static_cast<NBRGBPreviewDisplay>(m_ui.previewDisplayMode->currentIndex());
    params.previewArea = previewArea();

    const auto fillChannel = [](QComboBox* selector, QDoubleSpinBox* bandwidth, QDoubleSpinBox* scale,
                                an::NarrowbandRgbChannel& channel,
                                std::shared_ptr<const an::consumer::ImageAsset>& image, std::string& name) {
        channel.enabled = false;
        channel.bandwidthNanometres = bandwidth->value();
        channel.scale = scale->value();
        const QString selected = selector ? selector->currentText() : QString();
        if (selected.isEmpty() || selected == NO_IMAGE_STR)
        {
            return;
        }
        if (auto* control = process_utils::findViewControlByName(selected); control != nullptr && control->hasImage())
        {
            image = control->document().asset();
            name = selected.toStdString();
            channel.enabled = true;
        }
    };
    fillChannel(m_ui.redImageSelector, m_ui.redBandwidth, m_ui.redScale, params.engine.channels[0],
                params.narrowband[0], params.narrowbandNames[0]);
    fillChannel(m_ui.greenImageSelector, m_ui.greenBandwidth, m_ui.greenScale, params.engine.channels[1],
                params.narrowband[1], params.narrowbandNames[1]);
    fillChannel(m_ui.blueImageSelector, m_ui.blueBandwidth, m_ui.blueScale, params.engine.channels[2],
                params.narrowband[2], params.narrowbandNames[2]);
    return params;
}

bool NBRGBCombinationProcess::isViewControlSupported(const ImageViewControl* ctrl)
{
    return ctrl != nullptr && !ctrl->isSequence() && process_utils::holdsColourImage(ctrl);
}

bool NBRGBCombinationProcess::isNarrowbandViewSupported(const ImageViewControl* ctrl) const
{
    if (ctrl == nullptr || ctrl == m_image_view_control || ctrl->isSequence() || !ctrl->hasImage() ||
        m_image_view_control == nullptr || !m_image_view_control->hasImage())
    {
        return false;
    }
    const std::optional<an::ImageDescriptor> candidate = ctrl->document().asset()->descriptor();
    const std::optional<an::ImageDescriptor> target = m_image_view_control->document().asset()->descriptor();
    return candidate.has_value() && target.has_value() && candidate->extent().channelCount() == 1u &&
           process_utils::sameSize(*candidate, *target);
}

void NBRGBCombinationProcess::rebuildNarrowbandSelectors(const QString& preferredRed, const QString& preferredGreen,
                                                         const QString& preferredBlue)
{
    m_ignoreSelectorUpdates = true;
    for (auto* selector : {m_ui.redImageSelector, m_ui.greenImageSelector, m_ui.blueImageSelector})
    {
        selector->clear();
        addNoImageItem(selector);
    }
    for (auto* control : process_utils::listViewControls())
    {
        if (!isNarrowbandViewSupported(control))
        {
            continue;
        }
        addImageItem(m_ui.redImageSelector, control);
        addImageItem(m_ui.greenImageSelector, control);
        addImageItem(m_ui.blueImageSelector, control);
    }
    restoreSelection(m_ui.redImageSelector, preferredRed);
    restoreSelection(m_ui.greenImageSelector, preferredGreen);
    restoreSelection(m_ui.blueImageSelector, preferredBlue);
    m_ignoreSelectorUpdates = false;
}
