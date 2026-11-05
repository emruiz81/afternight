// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The Image Blend window: the dialog's selectors and transfers snapshotted on the GUI thread, and one engine
/// composition on the run's worker, written in the target's representation and recorded as a combination of the two
/// images - or, with no blend image chosen, the target under its own stretch alone.

#include "stdafx.hpp"
#include "ImageBlendProcess.h"
#include "controls/ImageViewControl.h"
#include "controls/ImageSelectorTooltipBuilder.h"
#include "controls/ProcessButtonIcons.h"
#include "controls/QSliderSpinBox.h"
#include "processes/CombinationProcessSupport.h"
#include "processes/PointwiseProcessUtils.h"
#include "processes/ImageBlendProcessUiBuilder.h"
#include "processes/ProcessCommonUtils.h"
#include "processes/StretchProcessSupport.h"

#include <AfterNightEngine/Algorithms/Pointwise/Stretch.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <AfterNightPlatform/Logging.h>

#include <QSignalBlocker>

#include <array>
#include <cstdint>
#include <utility>

namespace
{

/// One entry of the mode selector: its label and the mode it names, in the menu's order.
struct ModeEntry
{
    const char* label;
    an::BlendCompositionMode mode;
};

constexpr std::array<ModeEntry, 27u> kModes{{
    {"Replace", an::BlendCompositionMode::Replace},
    {"Darken/Min", an::BlendCompositionMode::Darken},
    {"Multiply", an::BlendCompositionMode::Multiply},
    {"Color Burn", an::BlendCompositionMode::ColorBurn},
    {"Linear Burn", an::BlendCompositionMode::LinearBurn},
    {"Darker Color", an::BlendCompositionMode::DarkerColor},
    {"Lighten/Max", an::BlendCompositionMode::Lighten},
    {"Screen", an::BlendCompositionMode::Screen},
    {"Color Dodge", an::BlendCompositionMode::ColorDodge},
    {"Linear Dodge/Add", an::BlendCompositionMode::LinearDodge},
    {"Lighter Color", an::BlendCompositionMode::LighterColor},
    {"Overlay", an::BlendCompositionMode::Overlay},
    {"Soft Light", an::BlendCompositionMode::SoftLight},
    {"Hard Light", an::BlendCompositionMode::HardLight},
    {"Vivid Light", an::BlendCompositionMode::VividLight},
    {"Linear Light", an::BlendCompositionMode::LinearLight},
    {"Pin Light", an::BlendCompositionMode::PinLight},
    {"Difference", an::BlendCompositionMode::Difference},
    {"Exclusion", an::BlendCompositionMode::Exclusion},
    {"Subtract", an::BlendCompositionMode::Subtract},
    {"Divide", an::BlendCompositionMode::Divide},
    {"Power", an::BlendCompositionMode::Power},
    {"Arctan", an::BlendCompositionMode::ArcTangent},
    {"Hue", an::BlendCompositionMode::Hue},
    {"Saturation", an::BlendCompositionMode::Saturation},
    {"Color", an::BlendCompositionMode::Color},
    {"Lightness", an::BlendCompositionMode::Lightness},
}};

/// The groups the menu separates: the index of the first entry of each group after the first.
constexpr std::array<std::size_t, 6u> kGroupStarts{1u, 6u, 11u, 17u, 21u, 23u};

struct FilterEntry
{
    const char* label;
    an::BlendPreFilter filter;
};

constexpr std::array<FilterEntry, 5u> kFilters{{
    {"None", an::BlendPreFilter::None},
    {"Gaussian Blur", an::BlendPreFilter::GaussianBlur},
    {"High Pass", an::BlendPreFilter::HighPass},
    {"Hue", an::BlendPreFilter::ColorizeLightness},
    {"Color", an::BlendPreFilter::ColorizeChannels},
}};

/// Whether a transfer is the identity: shadows at zero, highlights at one, midtones at the half.
[[nodiscard]] bool isIdentityTransfer(const double shadows, const double midtones, const double highlights) noexcept
{
    return shadows == 0.0 && highlights == 1.0 && midtones == 0.5;
}

[[nodiscard]] an::Status refusal(an::ErrorCode code, std::string message)
{
    return an::Status::failure(code, std::move(message));
}

} // namespace

namespace an::ui::processes::image_blend_detail
{

std::optional<an::BlendCompositionMode> blendModeFromLabel(const QString& label)
{
    for (const ModeEntry& entry : kModes)
    {
        if (label == QLatin1String(entry.label))
        {
            return entry.mode;
        }
    }
    return std::nullopt;
}

std::optional<an::BlendPreFilter> blendFilterFromLabel(const QString& label)
{
    for (const FilterEntry& entry : kFilters)
    {
        if (label == QLatin1String(entry.label))
        {
            return entry.filter;
        }
    }
    return std::nullopt;
}

} // namespace an::ui::processes::image_blend_detail

struct ImageBlendProcess::Impl
{
    bool ignoreSecondSelector = false;
    QSliderDoubleSpinBoxUPtr opacityControl;
    QSliderDoubleSpinBoxSPtr blendImageBlackPointControl;
    QSliderDoubleSpinBoxSPtr blendImageHighlightsControl;
    QSliderDoubleSpinBoxSPtr blendImageMidtonesControl;
    QSliderDoubleSpinBoxSPtr srcImageBlackPointControl;
    QSliderDoubleSpinBoxSPtr srcImageHighlightsControl;
    QSliderDoubleSpinBoxSPtr srcImageMidtonesControl;
    QSliderDoubleSpinBoxSPtr filterRadiusControl;
    QSliderDoubleSpinBoxSPtr filterHueControl;
    QSliderDoubleSpinBoxSPtr filterSaturationControl;
    an::ui::processes::ImageBlendProcessUiElements ui;
};

ImageBlendProcess::ImageBlendProcess(ImageViewControl* control)
    : an::ui::TypedProcessControl<ImageBlendParams>(control, getProcessNameStatic()), m_impl(std::make_unique<Impl>())
{
    m_impl->ui = an::ui::processes::buildImageBlendProcessUi(this);
    setProcessDescription(QStringLiteral(
        "Blend a target image with a secondary image using blend modes, optional filtering, and opacity control."));
    auto& ui = m_impl->ui;
    ui.verticalGroupBox1->setFlat(true);
    ui.srcImageMTFGroupBox->setFlat(true);
    ui.verticalGroupBox2->setFlat(true);
    ui.blendImageMTFGroupBox->setFlat(true);
    ui.filterGroupBox->setFlat(true);
    ui.blendParametersGroupBox->setFlat(true);

    this->setAttribute(Qt::WA_DeleteOnClose);

    m_SupportTargetViewControlSelection = true;
    m_SupportPerChannelProcessing = false;
    m_FixedLayout = false;
    m_ShowWaitingSpinner = false;
    m_ImagePreviewLayout = ui.previewLayout;
    m_MainHorizontalLayout = ui.horizontalLayoutMain;

    initializeStandardProcess(ui.horizontalLayoutMain, ui.previewLayout, ui.preview, ui.buttonBox,
                              ui.targetImageSelector, nullptr,
                              {.supportPerChannel = false, .showWaitingSpinner = false});

    QObject::connect(ui.secondImageSelector, &QComboBox::currentTextChanged, this,
                     &ImageBlendProcess::on_secondImageSelector_currentTextChanged);
    QObject::connect(ui.filterSelector, &QComboBox::currentTextChanged, this,
                     &ImageBlendProcess::on_filterType_currentTextChanged);
    QObject::connect(ui.blendMode, &QComboBox::currentTextChanged, this,
                     &ImageBlendProcess::on_blendMode_currentTextChanged);
    QObject::connect(ui.preserveLightness, &QCheckBox::checkStateChanged, this,
                     &ImageBlendProcess::on_preserve_lightness_clicked);

    const auto changed = [this](double) { settingsUpdated(); };
    INIT_D_SPINBOX_CTRL(m_impl->opacityControl, ui.opacityValue, ui.opacitySlider, changed);
    INIT_D_SPINBOX_CTRL(m_impl->srcImageBlackPointControl, ui.srcImageBlackPointValue, ui.srcImageBlackPointSlider,
                        changed);
    INIT_D_SPINBOX_CTRL(m_impl->srcImageHighlightsControl, ui.srcImageHighlightsValue, ui.srcImageHighlightsSlider,
                        changed);
    INIT_D_SPINBOX_CTRL(m_impl->srcImageMidtonesControl, ui.srcImageMidtonesValue, ui.srcImageMidtonesSlider, changed);
    INIT_D_SPINBOX_CTRL(m_impl->blendImageBlackPointControl, ui.imageBlendBlackPointValue,
                        ui.imageBlendBlackPointSlider, changed);
    INIT_D_SPINBOX_CTRL(m_impl->blendImageHighlightsControl, ui.imageBlendHighlightsValue,
                        ui.imageBlendHighlightsSlider, changed);
    INIT_D_SPINBOX_CTRL(m_impl->blendImageMidtonesControl, ui.imageBlendMidtonesValue, ui.imageBlendMidtonesSlider,
                        changed);
    INIT_D_SPINBOX_CTRL(m_impl->filterRadiusControl, ui.filterRadiusValue, ui.filterRadiusSlider, changed);
    INIT_D_SPINBOX_CTRL(m_impl->filterSaturationControl, ui.filterSaturationValue, ui.filterSaturationSlider, changed);
    INIT_D_SPINBOX_CTRL(m_impl->filterHueControl, ui.filterHueValue, ui.filterHueSlider, changed);

    updateWindowTitle();

    setupHistogramPreview(ui.chartScrollArea, ui.plotResolution, ui.zoomIn, ui.zoomOut, ui.zoomReset, ui.gridButton,
                          ui.localChannelNormalization, ui.showClippingInfo, ui.showHistogram, ui.showHistogramAreas);

    initialize();
    loadImage2List();
    ui.secondImageSelector->setCurrentText(NO_IMAGE_STR);

    setExecutionAutostretchSupported(true);
    setupPreviewControl(false);
    validateParameters();
    refreshTheme();
}

ImageBlendProcess::~ImageBlendProcess() = default;

std::string ImageBlendProcess::getProcessNameStatic()
{
    return "Image Blend";
}

bool ImageBlendProcess::requiresExclusiveAccessStatic()
{
    return false;
}

void ImageBlendProcess::refreshTheme()
{
    ImageProcessControl::refreshTheme();
    if (const auto iconProvider = process_utils::currentIconProvider())
    {
        process_utils::applyDialogButtonBoxIcons(m_impl->ui.buttonBox, iconProvider.get());
    }
}

void ImageBlendProcess::onViewControlCreated(const ImageViewControl* ctrl)
{
    ImageRTPreviewControl::onViewControlCreated(ctrl);
    if (isViewControlSupportedCustom(ctrl))
    {
        m_impl->ui.secondImageSelector->addItem(QString::fromUtf8(ctrl->getUniqueName()));
        const int idx = m_impl->ui.secondImageSelector->count() - 1;
        m_impl->ui.secondImageSelector->setItemData(idx, an::ui::buildImageSelectorTooltip(ctrl), Qt::ToolTipRole);
    }
}

void ImageBlendProcess::onViewControlDestroyed(const ImageViewControl* ctrl)
{
    ImageRTPreviewControl::onViewControlDestroyed(ctrl);
    const QString name = QString::fromUtf8(ctrl->getUniqueName());
    const int idx = m_impl->ui.secondImageSelector->findText(name);
    if (idx != -1)
    {
        if (m_impl->ui.secondImageSelector->currentText() == name)
        {
            m_impl->ui.secondImageSelector->setCurrentText(NO_IMAGE_STR);
        }
        m_impl->ui.secondImageSelector->removeItem(idx);
    }
}

void ImageBlendProcess::onViewControlRenamed(const std::string& newName, const std::string& oldName,
                                             const ImageViewControl* ctrl)
{
    ImageRTPreviewControl::onViewControlRenamed(newName, oldName, ctrl);
    const QString oldNameQt = QString::fromUtf8(oldName);
    const int idx = m_impl->ui.secondImageSelector->findText(oldNameQt);
    if (idx != -1)
    {
        const QString current = m_impl->ui.secondImageSelector->currentText();
        const QString newNameQt = QString::fromUtf8(newName);
        m_impl->ignoreSecondSelector = true;
        m_impl->ui.secondImageSelector->setItemText(idx, newNameQt);
        m_impl->ui.secondImageSelector->setItemData(idx, an::ui::buildImageSelectorTooltip(ctrl), Qt::ToolTipRole);
        if (current == oldNameQt)
        {
            m_impl->ui.secondImageSelector->setCurrentText(newNameQt);
        }
        m_impl->ignoreSecondSelector = false;
    }
}

void ImageBlendProcess::onViewControlUpdated(const ImageViewControl* ctrl)
{
    const QString selected = m_impl->ui.secondImageSelector->currentText();
    {
        const QSignalBlocker blocker(m_impl->ui.secondImageSelector);
        loadImage2List();
        if (m_impl->ui.secondImageSelector->findText(selected) != -1)
        {
            m_impl->ui.secondImageSelector->setCurrentText(selected);
        }
        else
        {
            m_impl->ui.secondImageSelector->setCurrentText(NO_IMAGE_STR);
        }
    }
    validateParameters();
    if (ctrl == m_image_view_control || ctrl->getUniqueName() == selected.toUtf8().toStdString())
    {
        settingsUpdated();
    }
    ImageRTPreviewControl::onViewControlUpdated(ctrl);
}

void ImageBlendProcess::on_secondImageSelector_currentTextChanged(const QString& /*str*/)
{
    if (m_impl->ignoreSecondSelector)
    {
        return;
    }
    validateParameters();
    settingsUpdated();
}

void ImageBlendProcess::on_blendMode_currentTextChanged(const QString& /*str*/)
{
    validateParameters();
    settingsUpdated();
}

void ImageBlendProcess::on_filterType_currentTextChanged(const QString& str)
{
    const std::optional<an::BlendPreFilter> filter = an::ui::processes::image_blend_detail::blendFilterFromLabel(str);
    const bool radius = filter == an::BlendPreFilter::GaussianBlur || filter == an::BlendPreFilter::HighPass;
    const bool colour =
        filter == an::BlendPreFilter::ColorizeLightness || filter == an::BlendPreFilter::ColorizeChannels;
    for (QWidget* widget :
         {static_cast<QWidget*>(m_impl->ui.filterRadiusSlider), static_cast<QWidget*>(m_impl->ui.filterRadiusValue),
          static_cast<QWidget*>(m_impl->ui.filterRadiusLabel)})
    {
        widget->setVisible(radius);
    }
    for (QWidget* widget :
         {static_cast<QWidget*>(m_impl->ui.filterSaturationSlider),
          static_cast<QWidget*>(m_impl->ui.filterSaturationValue),
          static_cast<QWidget*>(m_impl->ui.filterSaturationLabel), static_cast<QWidget*>(m_impl->ui.filterHueSlider),
          static_cast<QWidget*>(m_impl->ui.filterHueValue), static_cast<QWidget*>(m_impl->ui.filterHueLabel)})
    {
        widget->setVisible(colour);
    }
    settingsUpdated();
}

void ImageBlendProcess::on_preserve_lightness_clicked(Qt::CheckState state)
{
    // The kept lightness costs a colour conversion each way, so a dragged slider waits for its release.
    enableSlidersTracking(state != Qt::Checked);
    settingsUpdated();
}

void ImageBlendProcess::on_targetImageSelector_currentTextChangedInternal(const QString& /*str*/)
{
    {
        const QSignalBlocker blocker(m_impl->ui.secondImageSelector);
        loadImage2List();
        m_impl->ui.secondImageSelector->setCurrentText(NO_IMAGE_STR);
    }
    validateParameters();
    settingsUpdated();
}

bool ImageBlendProcess::isPreviewEnabled() const
{
    return m_impl->ui.preview->isChecked();
}

void ImageBlendProcess::enablePreview(bool enabled)
{
    m_impl->ui.preview->setChecked(enabled);
}

void ImageBlendProcess::settingsUpdatedImpl() {}

an::Status ImageBlendProcess::executeTypedProcessImpl(const an::ui::ProcessSource& source,
                                                      an::ui::ProcessDestination& destination,
                                                      const an::ui::ProcessRun& run, const ImageBlendParams& params,
                                                      an::ui::ExecutionScope& scope)
{
    // Without a blend image there is nothing to blend, and the result is the target under its own stretch, as earlier
    // releases applied it alone: the composition's own transfer of the target - one screen transfer on each of the
    // first three channels - and nothing else. The identity leaves the target as it is.
    if (!params.second)
    {
        if (isIdentityTransfer(params.srcImageBlackPoint, params.srcImageMidtones, params.srcImageHighlights))
        {
            return an::Status::success();
        }
        an::Result<an::consumer::AnyImage> working =
            an::ui::processes::makeStretchWorkingCopy(scope.engine(), scope.execution(), destination.asset().image());
        if (!working.hasValue())
        {
            return working.status();
        }
        const an::MidtonesTransferParameters transfer{.midtones = params.srcImageMidtones,
                                                      .shadows = params.srcImageBlackPoint,
                                                      .highlights = params.srcImageHighlights};
        an::StretchImageRequest stretch;
        stretch.stretch.function = an::StretchFunction::ScreenTransfer;
        stretch.stretch.screenTransfer = {transfer, transfer, transfer};
        stretch.colorMode = an::StretchColorMode::Independent;
        stretch.channelEnabled = {true, true, true, false};
        std::optional<an::ValidityMaskView<const std::uint8_t>> maskView;
        const an::KernelValidityView validity =
            an::ui::processes::stretchValidity(source.metadata(), source.validity(), maskView);
        if (an::Status stretched =
                an::ui::processes::stretchWorkingImage(scope.engine(), scope.execution(), *working, stretch, validity);
            stretched.failed())
        {
            return stretched;
        }
        const an::ui::processes::StretchRecord record{.effect = an::kStretchImageEffect,
                                                      .operation = "an::stretchImage",
                                                      .detail = describeTypedProcessStep(params).description,
                                                      .processingOperation = "image-blend"};
        return an::ui::processes::publishStretchResult(scope.session(), scope.engine(), scope.execution(), source,
                                                       *working, QRect(), record, destination);
    }
    const std::optional<an::ImageDescriptor> targetDescribed = source.descriptor();
    if (!targetDescribed.has_value())
    {
        return refusal(an::ErrorCode::NotInitialized, "the target holds nothing");
    }
    an::Result<process_utils::LeasedImage> second =
        process_utils::LeasedImage::read(scope, *params.second, run, params.previewArea, *targetDescribed);
    if (!second.hasValue())
    {
        return second.status();
    }
    const an::ui::ProcessSource blendSource = second->source();
    const std::optional<an::ImageDescriptor> blendDescribed = blendSource.descriptor();
    if (!blendDescribed.has_value())
    {
        return refusal(an::ErrorCode::NotInitialized, "the blend image holds nothing");
    }
    if (!process_utils::sameSize(*targetDescribed, *blendDescribed))
    {
        return refusal(an::ErrorCode::IncompatibleArguments, "the blend image does not have the target's size");
    }

    an::BlendCompositionRequest request;
    request.mode = params.mode;
    request.targetTransfer = an::MidtonesTransferParameters{.midtones = params.srcImageMidtones,
                                                            .shadows = params.srcImageBlackPoint,
                                                            .highlights = params.srcImageHighlights};
    request.blendTransfer = an::MidtonesTransferParameters{.midtones = params.blendImageMidtones,
                                                           .shadows = params.blendImageBlackPoint,
                                                           .highlights = params.blendImageHighlights};
    request.filter = params.filter;
    request.filterSigma = params.filterRadius;
    request.filterHueDegrees = params.filterHue;
    request.filterSaturation = params.filterSaturation;
    request.preserveLightness = params.preserveLightness;
    request.opacity = params.opacity;
    // An integer image is blended in fractions of full scale through a working copy and narrowed back.
    const an::SampleType working = process_utils::compositionWorkingType(targetDescribed->sampleType());
    request.colorEncoding = process_utils::colourReadingOf(*targetDescribed);
    request.outputSampleType = working;
    const an::Result<an::BlendCompositionPlan> plan = an::planBlendComposition(request);
    if (!plan.hasValue())
    {
        return plan.status();
    }

    an::Result<std::unique_ptr<process_utils::CompositionSource>> target =
        process_utils::bindForComposition(scope, source, working, process_utils::PhysicalReading::AsFractions);
    if (!target.hasValue())
    {
        return target.status();
    }
    an::Result<std::unique_ptr<process_utils::CompositionSource>> blend =
        process_utils::bindForComposition(scope, blendSource, working, process_utils::PhysicalReading::AsFractions);
    if (!blend.hasValue())
    {
        return blend.status();
    }
    const std::uint32_t channels = an::blendCompositionChannels(*plan, *targetDescribed, *blendDescribed);
    const an::Result<an::ImageDescriptor> described = process_utils::compositionDescriptor(
        *targetDescribed, channels, targetDescribed->sampleType(), scope.engine().limits());
    if (!described.hasValue())
    {
        return described.status();
    }
    const std::array<const an::ImageMetadata*, 2u> sources{&source.metadata(), &blendSource.metadata()};
    an::Result<an::ImageMetadata> metadata = process_utils::combinedResultMetadata(
        sources, an::kBlendCompositionEffect, *described,
        process_utils::CombinationRecord{.stateOperation = "image-blend",
                                         .engineOperation = "an::composeBlend",
                                         .detail = describeTypedProcessStep(params).description,
                                         .linearity = process_utils::CombinationLinearity::Nonlinear});
    if (!metadata.hasValue())
    {
        return metadata.status();
    }
    const an::BlendCompositionSource targetBound{.descriptor = &(*target)->bound->descriptor(),
                                                 .samples = (*target)->bound->samples(),
                                                 .layout = &(*target)->bound->layout(),
                                                 .validity = (*target)->bound->validity()};
    const an::BlendCompositionSource blendBound{.descriptor = &(*blend)->bound->descriptor(),
                                                .samples = (*blend)->bound->samples(),
                                                .layout = &(*blend)->bound->layout(),
                                                .validity = (*blend)->bound->validity()};
    const an::ExecutionContext& execution = scope.execution();
    an::Result<an::consumer::ImageAsset> composed = process_utils::composeNarrowed(
        scope, *described, std::move(*metadata), [&](void* samples, const an::ImageLayout& layout) -> an::Status {
            const an::Result<an::BlendCompositionResult> ran =
                an::composeBlend(execution, *plan, targetBound, blendBound, samples, layout);
            return ran.hasValue() ? an::Status::success() : ran.status();
        });
    if (!composed.hasValue())
    {
        return composed.status();
    }
    return destination.replace(std::move(*composed));
}

an::ui::ProcessStep ImageBlendProcess::describeTypedProcessStep(const ImageBlendParams& params) const
{
    if (!params.second)
    {
        return an::ui::ProcessStep{
            .name = getProcessName(),
            .description = "Base stretch: shadows " + QString::number(params.srcImageBlackPoint, 'f', 3).toStdString() +
                           ", midtones " + QString::number(params.srcImageMidtones, 'f', 3).toStdString() +
                           ", highlights " + QString::number(params.srcImageHighlights, 'f', 3).toStdString()};
    }
    return an::ui::ProcessStep{.name = getProcessName(),
                               .description =
                                   params.blendMode.toStdString() + " with " + params.secondImageName.toStdString()};
}

// The second image: other views' images the body reads, leased before the run is admitted.
std::vector<std::shared_ptr<const an::consumer::ImageAsset>>
ImageBlendProcess::typedImagesRead(const ImageBlendParams& params) const
{
    return {params.second};
}

void ImageBlendProcess::resetSettings()
{
    ImageRTPreviewControl::resetSettings();
    m_impl->ui.preserveLightness->setChecked(false);
    m_impl->ui.blendMode->setCurrentText(QLatin1String(kModes.front().label));
    m_impl->srcImageBlackPointControl->setValue(0.0);
    m_impl->srcImageHighlightsControl->setValue(1.0);
    m_impl->srcImageMidtonesControl->setValue(0.5);
    m_impl->blendImageBlackPointControl->setValue(0.0);
    m_impl->blendImageHighlightsControl->setValue(1.0);
    m_impl->blendImageMidtonesControl->setValue(0.5);
    m_impl->opacityControl->setValue(1.0);
    m_impl->ui.filterSelector->setCurrentText(QLatin1String(kFilters.front().label));
    m_impl->filterRadiusControl->setValue(10.0);
    m_impl->filterSaturationControl->setValue(1.0);
    m_impl->filterHueControl->setValue(0.0);
    enableSlidersTracking(true);
}

void ImageBlendProcess::previewControlStarted()
{
    if (m_rt_preview_view_control)
    {
        m_rt_preview_view_control->setRTAlternatePreviewLabels("PREVIEW", "BASE IMAGE");
    }
}

bool ImageBlendProcess::isViewControlSupportedCustom(const ImageViewControl* ctrl) const
{
    if (ctrl == nullptr || !ctrl->hasImage() || m_image_view_control == nullptr || !m_image_view_control->hasImage())
    {
        return false;
    }
    const std::optional<an::ImageDescriptor> candidate = ctrl->document().asset()->descriptor();
    const std::optional<an::ImageDescriptor> target = m_image_view_control->document().asset()->descriptor();
    return candidate.has_value() && target.has_value() && process_utils::sameSize(*candidate, *target);
}

void ImageBlendProcess::loadImage2List()
{
    const QSignalBlocker blocker(m_impl->ui.secondImageSelector);
    m_impl->ui.secondImageSelector->clear();
    m_impl->ui.secondImageSelector->addItem(NO_IMAGE_STR);
    for (ImageViewControl* view : process_utils::listViewControls())
    {
        if (view == m_image_view_control || !isViewControlSupportedCustom(view))
        {
            continue;
        }
        m_impl->ui.secondImageSelector->addItem(QString::fromUtf8(view->getUniqueName()));
        const int idx = m_impl->ui.secondImageSelector->count() - 1;
        m_impl->ui.secondImageSelector->setItemData(idx, an::ui::buildImageSelectorTooltip(view), Qt::ToolTipRole);
    }
}

void ImageBlendProcess::initialize()
{
    const QSignalBlocker modes(m_impl->ui.blendMode);
    const QSignalBlocker filters(m_impl->ui.filterSelector);
    std::size_t group = 0u;
    for (std::size_t index = 0u; index < kModes.size(); ++index)
    {
        if (group < kGroupStarts.size() && index == kGroupStarts[group])
        {
            m_impl->ui.blendMode->insertSeparator(m_impl->ui.blendMode->count());
            ++group;
        }
        m_impl->ui.blendMode->addItem(QLatin1String(kModes[index].label));
    }
    for (const FilterEntry& entry : kFilters)
    {
        m_impl->ui.filterSelector->addItem(QLatin1String(entry.label));
    }
    m_impl->ui.blendMode->setCurrentText(QLatin1String(kModes.front().label));
    m_impl->ui.filterSelector->setCurrentText(QLatin1String(kFilters.front().label));
    on_filterType_currentTextChanged(m_impl->ui.filterSelector->currentText());
}

void ImageBlendProcess::validateParameters()
{
    const QString selected = m_impl->ui.secondImageSelector->currentText();
    ImageViewControl* second =
        selected == NO_IMAGE_STR || selected.isEmpty() ? nullptr : process_utils::findViewControlByName(selected);
    const bool hasSecond = second != nullptr && second->hasImage();
    enableBlendParameters(hasSecond);
    if (auto* apply = m_impl->ui.buttonBox->button(QDialogButtonBox::Apply))
    {
        apply->setEnabled(hasSecond);
    }
    if (!hasSecond)
    {
        return;
    }
    const std::optional<an::BlendCompositionMode> mode =
        an::ui::processes::image_blend_detail::blendModeFromLabel(m_impl->ui.blendMode->currentText());
    const bool targetColour = process_utils::holdsColourImage(m_image_view_control);
    const bool secondColour = process_utils::holdsColourImage(second);
    const bool lightnessMode = mode == an::BlendCompositionMode::Lightness || mode == an::BlendCompositionMode::Color;
    m_impl->ui.preserveLightness->setEnabled(!lightnessMode && (targetColour || secondColour));
}

void ImageBlendProcess::enableBlendParameters(bool enabled)
{
    m_impl->ui.blendImageMTFGroupBox->setEnabled(enabled);
    m_impl->opacityControl->setEnabled(enabled);
    m_impl->ui.opacityLabel->setEnabled(enabled);
    m_impl->ui.blendParametersGroupBox->setEnabled(enabled);
    m_impl->ui.filterGroupBox->setEnabled(enabled);
}

void ImageBlendProcess::enableSlidersTracking(bool enabled)
{
    for (const auto& control :
         {m_impl->srcImageBlackPointControl, m_impl->srcImageHighlightsControl, m_impl->srcImageMidtonesControl,
          m_impl->blendImageBlackPointControl, m_impl->blendImageHighlightsControl, m_impl->blendImageMidtonesControl,
          m_impl->filterRadiusControl, m_impl->filterHueControl, m_impl->filterSaturationControl})
    {
        control->getSlider()->setTracking(enabled);
    }
    m_impl->opacityControl->getSlider()->setTracking(enabled);
}

ImageBlendParams ImageBlendProcess::getParams() const
{
    ImageBlendParams params;
    params.blendMode = m_impl->ui.blendMode->currentText();
    params.mode = an::ui::processes::image_blend_detail::blendModeFromLabel(params.blendMode)
                      .value_or(an::BlendCompositionMode::Replace);
    params.targetImageName = m_impl->ui.targetImageSelector->currentText();
    params.secondImageName = m_impl->ui.secondImageSelector->currentText();
    if (params.secondImageName != NO_IMAGE_STR && !params.secondImageName.trimmed().isEmpty())
    {
        if (auto* second = process_utils::findViewControlByName(params.secondImageName);
            second != nullptr && second->hasImage())
        {
            params.second = second->document().asset();
        }
    }
    params.preserveLightness = m_impl->ui.preserveLightness->isEnabled() && m_impl->ui.preserveLightness->isChecked();
    params.opacity = m_impl->opacityControl->getValue();
    params.srcImageBlackPoint = m_impl->srcImageBlackPointControl->getValue();
    params.srcImageHighlights = m_impl->srcImageHighlightsControl->getValue();
    params.srcImageMidtones = m_impl->srcImageMidtonesControl->getValue();
    params.blendImageBlackPoint = m_impl->blendImageBlackPointControl->getValue();
    params.blendImageHighlights = m_impl->blendImageHighlightsControl->getValue();
    params.blendImageMidtones = m_impl->blendImageMidtonesControl->getValue();
    params.filterType = m_impl->ui.filterSelector->currentText();
    params.filter = an::ui::processes::image_blend_detail::blendFilterFromLabel(params.filterType)
                        .value_or(an::BlendPreFilter::None);
    params.filterRadius = m_impl->filterRadiusControl->getValue();
    params.filterHue = m_impl->filterHueControl->getValue();
    params.filterSaturation = m_impl->filterSaturationControl->getValue();
    params.previewArea = previewArea();
    return params;
}
