// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The LRGB Combination window: the luminance selector and the weights snapshotted on the GUI thread, and one engine
/// composition on the run's worker, written in the target's representation and recorded as a combination of the two
/// images.

#include "stdafx.hpp"
#include "LRGBCombinationProcess.h"
#include "controls/ImageSelectorTooltipBuilder.h"
#include "controls/ImageViewControl.h"
#include "controls/ProcessButtonIcons.h"
#include "processes/CombinationProcessSupport.h"
#include "processes/MaskProcessUtils.h"
#include "processes/PointwiseProcessUtils.h"
#include "processes/ProcessCommonUtils.h"

#include <AfterNightEngine/Algorithms/Color/LrgbCombination.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include <QSignalBlocker>

#include <array>
#include <utility>

namespace
{

[[nodiscard]] an::Status refusal(an::ErrorCode code, std::string message)
{
    return an::Status::failure(code, std::move(message));
}

} // namespace

LRGBCombinationProcess::LRGBCombinationProcess(ImageViewControl* control)
    : an::ui::TypedProcessControl<LRGBCombinationParams>(control, getProcessNameStatic())
{
    m_ui = an::ui::processes::buildLRGBCombinationProcessUi(this);
    setProcessDescription(QStringLiteral(
        "Combine RGB chrominance with a dedicated luminance source using adjustable channel and MTF weights."));

    m_SupportTargetViewControlSelection = true;
    m_SupportPerChannelProcessing = false;
    m_FixedLayout = false;
    m_ShowWaitingSpinner = false;
    m_ImagePreviewLayout = m_ui.previewLayout;
    m_MainHorizontalLayout = m_ui.horizontalLayoutMain;

    initializeStandardProcess(m_ui.horizontalLayoutMain, m_ui.previewLayout, nullptr, m_ui.buttonBox,
                              m_ui.rgbImageSelector, nullptr,
                              {.supportPerChannel = false, .showWaitingSpinner = false});

    QObject::connect(m_ui.luminanceImageSelector, &QComboBox::currentTextChanged, this, [this](const QString&) {
        if (!m_ignoreLuminanceSelector)
        {
            settingsUpdated();
        }
    });

    const auto changed = [this](double) { settingsUpdated(); };
    INIT_D_SPINBOX_CTRL(lightnessWeightControl, m_ui.lightnessWeightValue, m_ui.lightnessWeightSlider, changed);
    INIT_D_SPINBOX_CTRL(redWeightControl, m_ui.redWeightValue, m_ui.redWeightSlider, changed);
    INIT_D_SPINBOX_CTRL(greenWeightControl, m_ui.greenWeightValue, m_ui.greenWeightSlider, changed);
    INIT_D_SPINBOX_CTRL(blueWeightControl, m_ui.blueWeightValue, m_ui.blueWeightSlider, changed);
    INIT_D_SPINBOX_CTRL(lightnessMTFControl, m_ui.lightnessMTFValue, m_ui.lightnessMTFSlider, changed);
    INIT_D_SPINBOX_CTRL(saturationMTFControl, m_ui.saturationMTFValue, m_ui.saturationMTFSlider, changed);

    updateWindowTitle();

    setupHistogramPreview(m_ui.chartScrollArea, m_ui.plotResolution, m_ui.zoomIn, m_ui.zoomOut, m_ui.zoomReset,
                          m_ui.gridButton, m_ui.localChannelNormalization, m_ui.showClippingInfo, m_ui.showHistogram,
                          m_ui.showHistogramAreas);

    loadImage2List();
    m_ui.luminanceImageSelector->setCurrentText(NO_IMAGE_STR);

    setExecutionAutostretchSupported(true);
    setupPreviewControl(false);
    settingsUpdated();

    refreshTheme();
}

LRGBCombinationProcess::~LRGBCombinationProcess() = default;

std::string LRGBCombinationProcess::getProcessNameStatic()
{
    return "LRGB Combination";
}

bool LRGBCombinationProcess::requiresExclusiveAccessStatic()
{
    return false;
}

void LRGBCombinationProcess::refreshTheme()
{
    ImageProcessControl::refreshTheme();
    if (const auto iconProvider = process_utils::currentIconProvider())
    {
        process_utils::applyDialogButtonBoxIcons(m_ui.buttonBox, iconProvider.get());
    }
}

void LRGBCombinationProcess::onViewControlCreated(const ImageViewControl* ctrl)
{
    ImageRTPreviewControl::onViewControlCreated(ctrl);
    if (isLuminanceViewSupported(ctrl))
    {
        m_ui.luminanceImageSelector->addItem(QString::fromUtf8(ctrl->getUniqueName()));
        const int idx = m_ui.luminanceImageSelector->count() - 1;
        m_ui.luminanceImageSelector->setItemData(idx, an::ui::buildImageSelectorTooltip(ctrl), Qt::ToolTipRole);
    }
}

void LRGBCombinationProcess::onViewControlDestroyed(const ImageViewControl* ctrl)
{
    ImageRTPreviewControl::onViewControlDestroyed(ctrl);
    const QString name = QString::fromUtf8(ctrl->getUniqueName());
    const int idx = m_ui.luminanceImageSelector->findText(name);
    if (idx != -1)
    {
        if (m_ui.luminanceImageSelector->currentText() == name)
        {
            m_ui.luminanceImageSelector->setCurrentText(NO_IMAGE_STR);
        }
        m_ui.luminanceImageSelector->removeItem(idx);
    }
}

void LRGBCombinationProcess::onViewControlRenamed(const std::string& newName, const std::string& oldName,
                                                  const ImageViewControl* ctrl)
{
    ImageRTPreviewControl::onViewControlRenamed(newName, oldName, ctrl);
    const QString oldNameQt = QString::fromUtf8(oldName);
    const int idx = m_ui.luminanceImageSelector->findText(oldNameQt);
    if (idx != -1)
    {
        const QString current = m_ui.luminanceImageSelector->currentText();
        const QString newNameQt = QString::fromUtf8(newName);
        m_ignoreLuminanceSelector = true;
        m_ui.luminanceImageSelector->setItemText(idx, newNameQt);
        m_ui.luminanceImageSelector->setItemData(idx, an::ui::buildImageSelectorTooltip(ctrl), Qt::ToolTipRole);
        if (current == oldNameQt)
        {
            m_ui.luminanceImageSelector->setCurrentText(newNameQt);
        }
        m_ignoreLuminanceSelector = false;
    }
}

void LRGBCombinationProcess::onViewControlUpdated(const ImageViewControl* ctrl)
{
    ImageRTPreviewControl::onViewControlUpdated(ctrl);
    const QString selected = m_ui.luminanceImageSelector->currentText();
    {
        const QSignalBlocker blocker(m_ui.luminanceImageSelector);
        loadImage2List();
        if (m_ui.luminanceImageSelector->findText(selected) != -1)
        {
            m_ui.luminanceImageSelector->setCurrentText(selected);
        }
        else
        {
            m_ui.luminanceImageSelector->setCurrentText(NO_IMAGE_STR);
        }
    }
    settingsUpdated();
}

bool LRGBCombinationProcess::isPreviewEnabled() const
{
    return true;
}

void LRGBCombinationProcess::enablePreview(bool /*enabled*/) {}

void LRGBCombinationProcess::on_targetImageSelector_currentTextChangedInternal(const QString& /*str*/)
{
    {
        const QSignalBlocker blocker(m_ui.luminanceImageSelector);
        loadImage2List();
        m_ui.luminanceImageSelector->setCurrentText(NO_IMAGE_STR);
    }
    settingsUpdated();
}

an::Status LRGBCombinationProcess::executeTypedProcessImpl(const an::ui::ProcessSource& source,
                                                           an::ui::ProcessDestination& destination,
                                                           const an::ui::ProcessRun& run,
                                                           const LRGBCombinationParams& params,
                                                           an::ui::ExecutionScope& scope)
{
    // Without a luminance image there is nothing to combine: the destination is the target.
    if (!params.luminance)
    {
        return an::Status::success();
    }
    const std::optional<an::ImageDescriptor> targetDescribed = source.descriptor();
    if (!targetDescribed.has_value())
    {
        return refusal(an::ErrorCode::NotInitialized, "the target holds nothing");
    }
    if (!process_utils::describesColourImage(*targetDescribed))
    {
        return refusal(an::ErrorCode::IncompatibleArguments, "an LRGB combination colours a red, green and blue image");
    }
    an::Result<process_utils::LeasedImage> luminance =
        process_utils::LeasedImage::read(scope, *params.luminance, run, params.previewArea, *targetDescribed);
    if (!luminance.hasValue())
    {
        return luminance.status();
    }
    const an::ui::ProcessSource luminanceSource = luminance->source();
    const std::optional<an::ImageDescriptor> luminanceDescribed = luminanceSource.descriptor();
    if (!luminanceDescribed.has_value())
    {
        return refusal(an::ErrorCode::NotInitialized, "the luminance image holds nothing");
    }
    if (!process_utils::sameSize(*targetDescribed, *luminanceDescribed) ||
        luminanceDescribed->extent().channelCount() != 1u)
    {
        return refusal(an::ErrorCode::IncompatibleArguments,
                       "the luminance image is one channel of the target's width and height");
    }

    an::LrgbCombinationRequest request;
    request.lightnessWeight = params.lightnessWeight;
    request.lightnessMidtones = params.lightnessMTF;
    request.chromaMidtones = params.saturationMTF;
    request.channelWeights = {params.redWeight, params.greenWeight, params.blueWeight};
    // An integer image is combined in fractions of full scale through a working copy and narrowed back; a luminance
    // of another representation is read in its own units.
    const an::SampleType working = process_utils::compositionWorkingType(targetDescribed->sampleType());
    request.colorEncoding = process_utils::colourReadingOf(*targetDescribed);
    request.outputSampleType = working;
    const an::Result<an::LrgbCombinationPlan> plan = an::planLrgbCombination(request);
    if (!plan.hasValue())
    {
        return plan.status();
    }
    an::Result<std::unique_ptr<process_utils::CompositionSource>> colour =
        process_utils::bindForComposition(scope, source, working, process_utils::PhysicalReading::AsFractions);
    if (!colour.hasValue())
    {
        return colour.status();
    }
    an::Result<std::unique_ptr<process_utils::CompositionSource>> lightness =
        process_utils::bindForComposition(scope, luminanceSource, working, process_utils::PhysicalReading::AsFractions);
    if (!lightness.hasValue())
    {
        return lightness.status();
    }
    const an::Result<an::ImageDescriptor> described = process_utils::compositionDescriptor(
        *targetDescribed, 3u, targetDescribed->sampleType(), scope.engine().limits());
    if (!described.hasValue())
    {
        return described.status();
    }
    const bool stretched = params.lightnessMTF != 0.5 || params.saturationMTF != 0.5;
    const std::array<const an::ImageMetadata*, 2u> sources{&source.metadata(), &luminanceSource.metadata()};
    an::Result<an::ImageMetadata> metadata = process_utils::combinedResultMetadata(
        sources, an::kLrgbCombinationEffect, *described,
        process_utils::CombinationRecord{.stateOperation = stretched ? "lrgb-mtf-combination" : "lrgb-combination",
                                         .engineOperation = "an::combineLrgb",
                                         .detail = describeTypedProcessStep(params).description,
                                         .linearity = process_utils::CombinationLinearity::Nonlinear});
    if (!metadata.hasValue())
    {
        return metadata.status();
    }
    const an::LrgbCombinationSource colourBound{.descriptor = &(*colour)->bound->descriptor(),
                                                .samples = (*colour)->bound->samples(),
                                                .layout = &(*colour)->bound->layout(),
                                                .validity = (*colour)->bound->validity()};
    const an::LrgbCombinationSource lightnessBound{.descriptor = &(*lightness)->bound->descriptor(),
                                                   .samples = (*lightness)->bound->samples(),
                                                   .layout = &(*lightness)->bound->layout(),
                                                   .validity = (*lightness)->bound->validity()};
    const an::ExecutionContext& execution = scope.execution();
    an::Result<an::consumer::ImageAsset> composed = process_utils::composeNarrowed(
        scope, *described, std::move(*metadata), [&](void* samples, const an::ImageLayout& layout) -> an::Status {
            const an::Result<an::LrgbCombinationResult> ran =
                an::combineLrgb(execution, *plan, colourBound, lightnessBound, samples, layout);
            return ran.hasValue() ? an::Status::success() : ran.status();
        });
    if (!composed.hasValue())
    {
        return composed.status();
    }
    return destination.replace(std::move(*composed));
}

an::ui::ProcessStep LRGBCombinationProcess::describeTypedProcessStep(const LRGBCombinationParams& params) const
{
    return an::ui::ProcessStep{
        .name = getProcessName(),
        .description = fmt::format("Luminance: '{}', lightness weight {:.2f}, channel weights R {:.2f} G {:.2f} B "
                                   "{:.2f}, lightness midtones {:.2f}, saturation midtones {:.2f}",
                                   params.luminanceImageName.toStdString(), params.lightnessWeight, params.redWeight,
                                   params.greenWeight, params.blueWeight, params.lightnessMTF, params.saturationMTF)};
}

// The luminance: other views' images the body reads, leased before the run is admitted.
std::vector<std::shared_ptr<const an::consumer::ImageAsset>>
LRGBCombinationProcess::typedImagesRead(const LRGBCombinationParams& params) const
{
    return {params.luminance};
}

void LRGBCombinationProcess::resetSettings()
{
    ImageRTPreviewControl::resetSettings();
    lightnessWeightControl->setValue(1.0);
    redWeightControl->setValue(1.0);
    greenWeightControl->setValue(1.0);
    blueWeightControl->setValue(1.0);
    lightnessMTFControl->setValue(0.5);
    saturationMTFControl->setValue(0.5);
}

void LRGBCombinationProcess::settingsUpdatedImpl()
{
    if (auto* applyButton = m_ui.buttonBox->button(QDialogButtonBox::Apply))
    {
        const QString luminanceSelection = m_ui.luminanceImageSelector->currentText();
        const bool hasTarget = m_image_view_control != nullptr && m_image_view_control->hasImage();
        bool hasLuminance = false;
        if (!luminanceSelection.isEmpty() && luminanceSelection != NO_IMAGE_STR)
        {
            auto* luminanceControl = process_utils::findViewControlByName(luminanceSelection);
            hasLuminance = luminanceControl != nullptr && luminanceControl->hasImage();
        }
        applyButton->setEnabled(hasTarget && hasLuminance);
    }
}

bool LRGBCombinationProcess::isViewControlSupported(const ImageViewControl* ctrl)
{
    return process_utils::holdsColourImage(ctrl);
}

bool LRGBCombinationProcess::isLuminanceViewSupported(const ImageViewControl* ctrl) const
{
    if (ctrl == nullptr || ctrl == m_image_view_control || !ctrl->hasImage() || m_image_view_control == nullptr ||
        !m_image_view_control->hasImage())
    {
        return false;
    }
    const std::optional<an::ImageDescriptor> candidate = ctrl->document().asset()->descriptor();
    const std::optional<an::ImageDescriptor> target = m_image_view_control->document().asset()->descriptor();
    return candidate.has_value() && target.has_value() && candidate->extent().channelCount() == 1u &&
           process_utils::sameSize(*candidate, *target);
}

void LRGBCombinationProcess::loadImage2List()
{
    const QSignalBlocker blocker(m_ui.luminanceImageSelector);
    m_ui.luminanceImageSelector->clear();
    m_ui.luminanceImageSelector->addItem(NO_IMAGE_STR);
    for (ImageViewControl* view : process_utils::listViewControls())
    {
        if (isLuminanceViewSupported(view))
        {
            m_ui.luminanceImageSelector->addItem(QString::fromUtf8(view->getUniqueName()));
            const int idx = m_ui.luminanceImageSelector->count() - 1;
            m_ui.luminanceImageSelector->setItemData(idx, an::ui::buildImageSelectorTooltip(view), Qt::ToolTipRole);
        }
    }
}

LRGBCombinationParams LRGBCombinationProcess::getParams() const
{
    LRGBCombinationParams params;
    params.luminanceImageName = m_ui.luminanceImageSelector->currentText();
    if (params.luminanceImageName != NO_IMAGE_STR && !params.luminanceImageName.trimmed().isEmpty())
    {
        if (auto* luminance = process_utils::findViewControlByName(params.luminanceImageName);
            luminance != nullptr && luminance->hasImage())
        {
            params.luminance = luminance->document().asset();
        }
    }
    params.lightnessWeight = lightnessWeightControl->getValue();
    params.redWeight = redWeightControl->getValue();
    params.greenWeight = greenWeightControl->getValue();
    params.blueWeight = blueWeightControl->getValue();
    params.lightnessMTF = lightnessMTFControl->getValue();
    params.saturationMTF = saturationMTFControl->getValue();
    params.previewArea = previewArea();
    return params;
}

void LRGBCombinationProcess::enableProcessControlsImpl(bool enabled)
{
    m_ui.buttonBox->button(QDialogButtonBox::Apply)->setEnabled(enabled);
    m_ui.buttonBox->button(QDialogButtonBox::Close)->setEnabled(enabled);
    m_ui.buttonBox->button(QDialogButtonBox::Reset)->setEnabled(enabled);
    m_ui.luminanceImageSelector->setEnabled(enabled);
    for (const auto& control : {lightnessWeightControl.get(), redWeightControl.get(), greenWeightControl.get(),
                                blueWeightControl.get(), lightnessMTFControl.get(), saturationMTFControl.get()})
    {
        control->setEnabled(enabled);
    }
    if (enabled)
    {
        settingsUpdatedImpl();
    }
}
