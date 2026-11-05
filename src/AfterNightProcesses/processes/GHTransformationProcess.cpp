// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "GHTransformationProcess.h"
#include "controls/ImageViewControl.h"

#include "processes/ProcessCommonUtils.h"
#include "controls/ProcessButtonIcons.h"
#include "theme/AnDesignTokens.h"
#include "widgets/ChannelDisplayStrategy.h"
#include "widgets/HistogramCompanions.h"

#include <AfterNightConsumerRuntime/OperationScope.h>

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Image/Image.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include <QSignalBlocker>

#include <array>
#include <cmath>
#include <span>
#include <type_traits>
#include <utility>
#include <variant>

namespace
{

using an::ui::processes::HistogramComponent;
using an::ui::processes::ScreenTransferStatistics;
using an::ui::processes::StretchRecord;

/// The name the processing-state cards record the stretch under.
constexpr const char* kProcessingOperation = "ght-stretch";

[[nodiscard]] an::Status refusal(an::ErrorCode code, std::string message)
{
    return an::Status::failure(code, std::move(message));
}

/// The transformation list's entries, in the order the list shows them, with the text each shows.
struct TransformationChoice
{
    GhtTransformation transformation;
    const char* label;
};

constexpr std::array<TransformationChoice, 6u> kTransformations{{
    {GhtTransformation::GeneralizedHyperbolic, "Generalized Hyperbolic"},
    {GhtTransformation::Histogram, "Histogram Transformation"},
    {GhtTransformation::Arcsinh, "Arcsinh"},
    {GhtTransformation::Linear, "Linear Stretch"},
    {GhtTransformation::Inversion, "Image Inversion"},
    {GhtTransformation::ScreenTransfer, "STF Transformation"},
}};

/// The channel list's entries for a colour image, in the order the list shows them.
struct ChannelChoice
{
    GhtChannel channel;
    const char* label;
    const char* icon;
};

constexpr std::array<ChannelChoice, 7u> kChannels{{
    {GhtChannel::Rgb, "RGB", "rgb"},
    {GhtChannel::Red, "Red", "red_circle"},
    {GhtChannel::Green, "Green", "green_circle"},
    {GhtChannel::Blue, "Blue", "blue_circle"},
    {GhtChannel::Lightness, "Lightness", "cielab"},
    {GhtChannel::Saturation, "Saturation", "hsv"},
    {GhtChannel::Colour, "Colour", "cielab"},
}};

/// Whether a channel choice stretches a colour component rather than the stored channels.
[[nodiscard]] constexpr bool isComponent(GhtChannel channel) noexcept
{
    return channel == GhtChannel::Lightness || channel == GhtChannel::Saturation || channel == GhtChannel::Colour;
}

/// The component a channel choice's histogram shows beside the channels.
[[nodiscard]] HistogramComponent componentOf(GhtChannel channel) noexcept
{
    switch (channel)
    {
    case GhtChannel::Lightness:
        return HistogramComponent::Lightness;
    case GhtChannel::Saturation:
        return HistogramComponent::Saturation;
    case GhtChannel::Colour:
        return HistogramComponent::Luminance;
    default:
        return HistogramComponent::None;
    }
}

/// The history's name of a stretch.
[[nodiscard]] std::string stretchTypeName(const GHTransformationParams& params)
{
    switch (params.transformation)
    {
    case GhtTransformation::GeneralizedHyperbolic:
        return params.invert ? "Generalized Hyperbolic (Inverse)" : "Generalized Hyperbolic";
    case GhtTransformation::Arcsinh:
        return params.invert ? "Arcsinh (Inverse)" : "Arcsinh";
    case GhtTransformation::Linear:
        return "Linear Stretch";
    case GhtTransformation::Histogram:
        return params.invert ? "Histogram Transformation (Inverse)" : "Histogram Transformation";
    case GhtTransformation::Inversion:
        return "Image Inversion";
    case GhtTransformation::ScreenTransfer:
        return "STF Transformation";
    }
    return "Unknown";
}

/// The history's name of a channel choice.
[[nodiscard]] std::string channelName(const GHTransformationParams& params)
{
    switch (params.channel)
    {
    case GhtChannel::Saturation:
        return "Saturation";
    case GhtChannel::Lightness:
        return "Lightness";
    case GhtChannel::Colour:
        return "Colour";
    case GhtChannel::Red:
        return "Red";
    case GhtChannel::Green:
        return "Green";
    case GhtChannel::Blue:
        return "Blue";
    case GhtChannel::Rgb:
        break;
    }
    return "RGB";
}

/// The history's and the lineage's account of a run's parameters.
[[nodiscard]] std::string describeParameters(const GHTransformationParams& params)
{
    return fmt::format("Transformation: {}, Channel: {}, Stretch factor: {:.4f}, Local Intensity: {:.4f}, "
                       "Symmetry Point: {:.4f}, Protect Shadows: {:.4f}, Protect Highlights: {:.4f}, "
                       "Black Point: {:.4f}, White Point: {:.4f}",
                       stretchTypeName(params), channelName(params), params.stretchFactor, params.localIntensity,
                       params.symmetryPoint, params.protectShadows, params.protectHighlights, params.blackPoint,
                       params.whitePoint);
}

/// The engine's curve a transformation and its inversion name.
[[nodiscard]] an::StretchFunction functionOf(const GHTransformationParams& params) noexcept
{
    switch (params.transformation)
    {
    case GhtTransformation::GeneralizedHyperbolic:
        return params.invert ? an::StretchFunction::InverseGeneralizedHyperbolic
                             : an::StretchFunction::GeneralizedHyperbolic;
    case GhtTransformation::Arcsinh:
        return params.invert ? an::StretchFunction::InverseAsinh : an::StretchFunction::Asinh;
    case GhtTransformation::Linear:
        return an::StretchFunction::Linear;
    case GhtTransformation::Histogram:
        return params.invert ? an::StretchFunction::InverseHistogram : an::StretchFunction::Histogram;
    case GhtTransformation::Inversion:
        return an::StretchFunction::Invert;
    case GhtTransformation::ScreenTransfer:
        return an::StretchFunction::ScreenTransfer;
    }
    return an::StretchFunction::GeneralizedHyperbolic;
}

/// What the dialog shows of its preview: the counts, and the statistics a screen transfer was derived from.
struct PreviewCounts
{
    an::ui::HistogramDisplayData counts;
    std::shared_ptr<const ScreenTransferStatistics> statistics;
};

/// The statistics of the dialog's target for a curve that needs them, from the dialog's cache or measured through the
/// work's lease on the target and kept.
[[nodiscard]] an::Result<std::shared_ptr<const ScreenTransferStatistics>>
targetStatistics(const an::ui::processes::DialogWork& work, const GHTransformationParams& params)
{
    if (params.transformation != GhtTransformation::ScreenTransfer || !params.target || !params.statistics)
    {
        return std::shared_ptr<const ScreenTransferStatistics>();
    }
    if (std::shared_ptr<const ScreenTransferStatistics> kept = params.statistics->find(params.target->revision()))
    {
        return kept;
    }
    if (!work.lease.has_value())
    {
        return refusal(an::ErrorCode::NotInitialized, "the dialog's image is not read");
    }
    const an::consumer::AssetReadLease* lease = &*work.lease;
    an::Result<ScreenTransferStatistics> measured = an::ui::processes::measureScreenTransferStatistics(
        work.scope.engine(), work.scope.execution(), lease->image(), lease->validity());
    if (!measured.hasValue())
    {
        return measured.status();
    }
    auto shared = std::make_shared<const ScreenTransferStatistics>(std::move(*measured));
    params.statistics->store(lease->revision(), shared);
    return std::shared_ptr<const ScreenTransferStatistics>(shared);
}

/// A working copy of an image stretched as the dialog asks.
} // namespace

an::Result<an::GeneralizedStretchParameters> ghtStretchParametersOf(const GHTransformationParams& params,
                                                                    const ScreenTransferStatistics* statistics)
{
    an::GeneralizedStretchParameters stretch;
    stretch.function = functionOf(params);
    stretch.amount = std::expm1(params.stretchFactor);
    stretch.bias = params.localIntensity;
    stretch.lowProtection = params.protectShadows;
    stretch.symmetryPoint = params.symmetryPoint;
    stretch.highProtection = params.protectHighlights;
    stretch.blackPoint = params.blackPoint;
    // The white point stays above the black point, as the dialog keeps it.
    stretch.whitePoint =
        params.whitePoint <= params.blackPoint ? (std::min)(1.0, params.blackPoint + 0.0001) : params.whitePoint;
    if (stretch.function != an::StretchFunction::ScreenTransfer)
    {
        return stretch;
    }
    if (statistics == nullptr || statistics->channels.empty())
    {
        return refusal(an::ErrorCode::NotInitialized, "the screen transfer has no statistics of the image");
    }
    const std::span<const an::StretchChannelStatistics> colour(
        statistics->channels.data(), (std::min)(statistics->channels.size(), std::size_t{3u}));
    if (params.linkedStf || colour.size() == 1u)
    {
        const an::Result<an::MidtonesTransferParameters> linked =
            an::deriveLinkedAutoStretch(colour, kGhtScreenTransferShadowsClipping, kGhtScreenTransferTargetBackground);
        if (!linked.hasValue())
        {
            return linked.status();
        }
        stretch.screenTransfer.fill(*linked);
        return stretch;
    }
    std::array<an::MidtonesTransferParameters, 3u> derived{};
    const an::Status unlinked =
        an::deriveUnlinkedAutoStretch(colour, kGhtScreenTransferShadowsClipping, kGhtScreenTransferTargetBackground,
                                      std::span<an::MidtonesTransferParameters>(derived.data(), colour.size()));
    if (unlinked.failed())
    {
        return unlinked;
    }
    for (std::size_t channel = 0u; channel < stretch.screenTransfer.size(); ++channel)
    {
        stretch.screenTransfer[channel] = derived[(std::min)(channel, colour.size() - 1u)];
    }
    return stretch;
}

std::array<double, 3u> ghtLuminanceWeightsOf(const GHTransformationParams& params)
{
    std::array<double, 3u> weights{1.0, 1.0, 1.0};
    if (params.luminanceSource == GhtLuminanceSource::Manual)
    {
        weights = params.luminanceCoefficients;
    }
    for (double& weight : weights)
    {
        weight = (std::max)(0.0, weight);
    }
    const double sum = weights[0] + weights[1] + weights[2];
    if (!(sum > 0.0))
    {
        return {1.0 / 3.0, 1.0 / 3.0, 1.0 / 3.0};
    }
    return {weights[0] / sum, weights[1] / sum, weights[2] / sum};
}

an::Result<an::StretchImageRequest> ghtStretchRequestOf(const GHTransformationParams& params,
                                                        const an::ImageDescriptor& image,
                                                        const ScreenTransferStatistics* statistics)
{
    an::Result<an::GeneralizedStretchParameters> stretch = ghtStretchParametersOf(params, statistics);
    if (!stretch.hasValue())
    {
        return stretch.status();
    }
    an::StretchImageRequest request;
    request.stretch = *stretch;
    request.colorMode = an::StretchColorMode::Independent;
    request.channelEnabled = {true, true, true, false};
    const bool colour = image.extent().channelCount() == 3u;
    if (!colour)
    {
        return request;
    }
    switch (params.channel)
    {
    case GhtChannel::Red:
        request.channelEnabled = {true, false, false, false};
        break;
    case GhtChannel::Green:
        request.channelEnabled = {false, true, false, false};
        break;
    case GhtChannel::Blue:
        request.channelEnabled = {false, false, true, false};
        break;
    case GhtChannel::Lightness:
        request.colorMode = an::StretchColorMode::Lightness;
        break;
    case GhtChannel::Saturation:
        request.colorMode = an::StretchColorMode::Saturation;
        break;
    case GhtChannel::Colour:
        request.colorMode = an::StretchColorMode::Color;
        request.overflow = params.colourClip;
        request.luminanceCoefficients = ghtLuminanceWeightsOf(params);
        break;
    case GhtChannel::Rgb:
        break;
    }
    // A colour mode reads the image's red, green and blue in the encoding they are stored in, as every channel scope
    // of the dialogs does.
    request.colorEncoding = an::ui::processChannelScopeRequest(an::ChannelScope::CieLightness, image).sourceEncoding;
    return request;
}

GHTransformationProcess::GHTransformationProcess(ImageViewControl* control)
    : an::ui::TypedProcessControl<GHTransformationParams>(control, getProcessNameStatic()), ImagePointSelectorControl()
{
    m_ui = an::ui::processes::buildGHTransformationProcessUi(this);

    setProcessDescription(QStringLiteral("Apply generalized hyperbolic, arcsinh, or linear stretch transformations "
                                         "with channel-aware histogram controls."));

    initializeStandardProcess(m_ui.horizontalLayoutMain, m_ui.verticalLayout_2, m_ui.preview, m_ui.buttonBox,
                              m_ui.targetImageSelector, nullptr, false, true);

    initializeTargetViewControlComboBox(m_ui.targetImageSelector);

    // Each transformation entry carries the curve it names, which is what a run reads; its text is the dialog's own.
    for (int index = 0; index < m_ui.transformationTypeSelector->count(); ++index)
    {
        for (const TransformationChoice& choice : kTransformations)
        {
            if (m_ui.transformationTypeSelector->itemText(index) == QLatin1String(choice.label))
            {
                m_ui.transformationTypeSelector->setItemData(index, static_cast<int>(choice.transformation));
            }
        }
    }

    QObject::connect(m_ui.channelSelector2, &QComboBox::currentTextChanged, this,
                     &GHTransformationProcess::on_channelSelector2_currentTextChanged);
    QObject::connect(m_ui.plotResolution, &QComboBox::currentTextChanged, this,
                     &GHTransformationProcess::on_plotResolution_currentTextChanged);
    QObject::connect(m_ui.transformationTypeSelector, &QComboBox::currentTextChanged, this,
                     &GHTransformationProcess::on_transformationTypeSelector_currentTextChanged);

    QObject::connect(m_ui.humanWeightedLuminance, &QCheckBox::checkStateChanged, this,
                     &GHTransformationProcess::on_HumanWeightedLuminanceClicked);
    QObject::connect(m_ui.colourClipSelector, &QComboBox::currentTextChanged, this,
                     &GHTransformationProcess::on_colourClipSelector_currentTextChanged);
    QObject::connect(m_ui.lumCoeffSourceSelector, &QComboBox::currentTextChanged, this,
                     &GHTransformationProcess::on_lumCoeffSourceSelector_currentTextChanged);
    QObject::connect(m_ui.lumCoeffRValue, &QDoubleSpinBox::valueChanged, this,
                     &GHTransformationProcess::on_lumCoeffChanged);
    QObject::connect(m_ui.lumCoeffGValue, &QDoubleSpinBox::valueChanged, this,
                     &GHTransformationProcess::on_lumCoeffChanged);
    QObject::connect(m_ui.lumCoeffBValue, &QDoubleSpinBox::valueChanged, this,
                     &GHTransformationProcess::on_lumCoeffChanged);
    QObject::connect(m_ui.invert, &QCheckBox::checkStateChanged, this, &GHTransformationProcess::on_InvertClicked);
    QObject::connect(m_ui.linkedStf, &QCheckBox::checkStateChanged, this,
                     &GHTransformationProcess::on_linkedStfClicked);

    QObject::connect(m_ui.gridButton, &QToolButton::clicked, this, &GHTransformationProcess::on_gridClicked);
    QObject::connect(m_ui.transferCurve, &QToolButton::clicked, this,
                     &GHTransformationProcess::on_transferCurveClicked);
    QObject::connect(m_ui.zoomIn, &QToolButton::clicked, this, &GHTransformationProcess::on_ZoomInButton);
    QObject::connect(m_ui.zoomOut, &QToolButton::clicked, this, &GHTransformationProcess::on_ZoomOutButton);
    QObject::connect(m_ui.zoomReset, &QToolButton::clicked, this, &GHTransformationProcess::on_ZoomResetButton);
    QObject::connect(m_ui.localChannelNormalization, &QToolButton::clicked, this,
                     &GHTransformationProcess::on_LocalChannelNormalization);
    QObject::connect(m_ui.showClippingInfo, &QToolButton::clicked, this,
                     &GHTransformationProcess::on_rt_showClippingInfo);
    QObject::connect(m_ui.showHistogramAreas, &QToolButton::clicked, this,
                     &GHTransformationProcess::on_showHistogramAreas);

    QObject::connect(m_ui.spFromImage, &QPushButton::clicked, this, &GHTransformationProcess::on_spFromImageClicked);
    QObject::connect(m_ui.spFromImageArea, &QPushButton::clicked, this,
                     &GHTransformationProcess::on_spFromImageAreaClicked);
    QObject::connect(m_ui.spFromHistogram, &QPushButton::clicked, this,
                     &GHTransformationProcess::on_spFromHistogramClicked);
    QObject::connect(m_ui.spFromImage, &QPushButton::toggled, this, [this](bool) { updateSpPickerButtonsStyle(); });
    QObject::connect(m_ui.spFromImageArea, &QPushButton::toggled, this, [this](bool) { updateSpPickerButtonsStyle(); });
    QObject::connect(m_ui.spFromHistogram, &QPushButton::toggled, this, [this](bool) { updateSpPickerButtonsStyle(); });

    INIT_D_SPINBOX_CTRL(stretchFactorControl, m_ui.stretchFactorValue, m_ui.stretchFactorSlider,
                        &GHTransformationProcess::on_stretchFactorChanged);
    INIT_D_SPINBOX_CTRL(localIntensityControl, m_ui.localIntensityValue, m_ui.localIntensitySlider,
                        &GHTransformationProcess::on_localIntensityChanged);
    INIT_D_SPINBOX_CTRL(symmetryPointControl, m_ui.symmetryPointValue, m_ui.symmetryPointSlider,
                        &GHTransformationProcess::on_symmetryPointChanged);
    INIT_D_SPINBOX_CTRL(protectShadowsControl, m_ui.protectShadowsValue, m_ui.protectShadowsSlider,
                        &GHTransformationProcess::on_protectShadowsChanged);
    INIT_D_SPINBOX_CTRL(protectHighlightsControl, m_ui.protectHighlightsValue, m_ui.protectHighlightsSlider,
                        &GHTransformationProcess::on_protectHighlightsChanged);
    INIT_D_SPINBOX_CTRL(blackPointControl, m_ui.blackPointValue, m_ui.blackPointSlider,
                        &GHTransformationProcess::on_blackPointChanged);
    INIT_D_SPINBOX_CTRL(whitePointControl, m_ui.whitePointValue, m_ui.whitePointSlider,
                        &GHTransformationProcess::on_whitePointChanged);

    if (const auto iconProvider = process_utils::currentIconProvider())
    {
        m_ui.spFromImage->setIcon(iconProvider->getIcon("color_fill"));
        m_ui.spFromImageArea->setIcon(iconProvider->getIcon("image_linear"));
        m_ui.spFromHistogram->setIcon(iconProvider->getIcon("histogram"));
    }
    updateSpPickerButtonsStyle();

    m_original_histogram_widget = std::make_unique<HistogramGHTWidget>(m_ui.originalChartScrollArea);
    m_original_histogram_widget->setRangeSelectorEnabled(true);
    m_original_histogram_widget->setListener(this);
    m_original_histogram_widget->setGuideLineEnabled(true);
    m_original_histogram_widget->setLocalChannelNormalization(true);
    m_original_histogram_widget->setClippingInfoEnabled(true);
    m_ui.originalChartScrollArea->setWidget(m_original_histogram_widget.get());

    m_preview_histogram_widget = std::make_unique<HistogramGHTWidget>(m_ui.previewChartScrollArea);
    m_preview_histogram_widget->setRangeSelectorEnabled(false);
    m_preview_histogram_widget->setListener(this);
    m_preview_histogram_widget->setTransferCurveEnabled(true);
    m_preview_histogram_widget->setLocalChannelNormalization(true);
    m_preview_histogram_widget->setClippingInfoEnabled(true);
    m_ui.previewChartScrollArea->setWidget(m_preview_histogram_widget.get());

    m_ui.humanWeightedLuminance->setChecked(true);
    m_ui.humanWeightedLuminance->setEnabled(false);

    updateSupportedPlotResolutions();
    {
        const QSignalBlocker blocker(m_ui.plotResolution);
        m_ui.plotResolution->setCurrentText("8-bit (256)");
    }
    m_original_histogram_widget->setHistogramResolution(an::ui::HistogramResolution::Bits8);
    m_preview_histogram_widget->setHistogramResolution(an::ui::HistogramResolution::Bits8);

    updateSupportedImageChannels();

    m_ui.channelSelector2->setCurrentText("RGB");

    updateWindowTitle();

    if (const auto iconProvider = process_utils::currentIconProvider())
    {
        m_ui.zoomIn->setIcon(iconProvider->getIcon("zoom_in"));
        m_ui.zoomOut->setIcon(iconProvider->getIcon("zoom_out"));
        m_ui.zoomReset->setIcon(iconProvider->getIcon("zoom_fit"));
        m_ui.gridButton->setIcon(iconProvider->getIcon("grid"));
        m_ui.transferCurve->setIcon(iconProvider->getIcon("stat_line"));
        m_ui.localChannelNormalization->setIcon(iconProvider->getIcon("scale"));
        const QSize clippingInfoIconSize =
            m_ui.showClippingInfo->iconSize().isValid() ? m_ui.showClippingInfo->iconSize() : QSize(16, 16);
        m_ui.showClippingInfo->setIcon(QIcon(iconProvider->loadSvgFileAsPixmap(
            QStringLiteral(":/AfterNight/icons/SVG/info.svg"), clippingInfoIconSize, iconProvider->getColorNormal())));
        m_ui.showHistogramAreas->setIcon(iconProvider->getIcon("area_chart"));
    }

    m_ui.transformationTypeSelector->setCurrentText("Generalized Hyperbolic");
    m_ui.colourClipSelector->setCurrentText("Clip");
    m_ui.lumCoeffSourceSelector->setCurrentText("Default");
    m_ui.linkedStf->setChecked(false);

    m_ui.localChannelNormalization->setChecked(true);
    QFont spTextFont = an::ui::tokens().typography.heading3;
    spTextFont.setWeight(QFont::Bold);
    m_ui.spText->setFont(spTextFont);
    m_ui.spText->setProperty("anStatusTone", "error");
    m_ui.spText->setAlignment(Qt::AlignCenter);

    m_FixedLayout = false;

    m_DefaultSizeNoPreview.setWidth(700);
    m_DefaultSizeNoPreview.setHeight(900);
    setupPreviewControl(false);

    refreshTheme();
    updateControlVisibility();
    updateHistogramDisplayChannels();

    recalculateOriginal();
}

void GHTransformationProcess::refreshTheme()
{
    ImageProcessControl::refreshTheme();
    if (const auto iconProvider = process_utils::currentIconProvider())
    {
        process_utils::applyDialogButtonBoxIcons(m_ui.buttonBox, iconProvider.get());
    }
}

std::string GHTransformationProcess::getProcessNameStatic()
{
    return "Generalized Hyperbolic Transformation";
}

bool GHTransformationProcess::requiresExclusiveAccessStatic()
{
    return false;
}

bool GHTransformationProcess::isColourTarget() const
{
    if (m_image_view_control == nullptr)
    {
        return false;
    }
    const std::optional<an::ImageDescriptor> descriptor = m_image_view_control->document().descriptor();
    return descriptor.has_value() && descriptor->extent().channelCount() == 3u;
}

GhtTransformation GHTransformationProcess::currentTransformation() const
{
    const QVariant data = m_ui.transformationTypeSelector->currentData();
    return data.isValid() ? static_cast<GhtTransformation>(data.toInt()) : GhtTransformation::GeneralizedHyperbolic;
}

GhtChannel GHTransformationProcess::currentChannel() const
{
    const QVariant data = m_ui.channelSelector2->currentData();
    return data.isValid() ? static_cast<GhtChannel>(data.toInt()) : GhtChannel::Rgb;
}

void GHTransformationProcess::targetChanged()
{
    const bool colourSelector = m_ui.channelSelector2->count() > 1;
    if (colourSelector != isColourTarget())
    {
        updateSupportedImageChannels();
    }
    recalculateOriginal();
}

void GHTransformationProcess::onImageLoaded(ImageViewControl* ctrl)
{
    ImageRTPreviewControl::onImageLoaded(ctrl);
    if (ctrl == m_image_view_control)
    {
        targetChanged();
    }
}

void GHTransformationProcess::onImageUpdated(ImageViewControl* ctrl)
{
    ImageRTPreviewControl::onImageUpdated(ctrl);
    if (ctrl == m_image_view_control)
    {
        targetChanged();
    }
}

void GHTransformationProcess::onViewControlChanged(ImageViewControl* oldCtrl, ImageViewControl* newCtrl)
{
    if (!isViewControlSupported(newCtrl))
    {
        close();
        return;
    }
    ImageRTPreviewControl::onViewControlChanged(oldCtrl, newCtrl);
    targetChanged();

    if (m_ui.spFromImage->isChecked())
    {
        rebuildPointSelectionController();
    }
    else if (m_ui.spFromImageArea->isChecked())
    {
        rebuildAreaSelectionController();
    }
}

void GHTransformationProcess::onProtectShadowsChanged(double shadows)
{
    m_IgnoreLastValueChangeForRangeSelectors = true;
    if (currentTransformation() == GhtTransformation::Linear)
    {
        if (blackPointControl->getValue() != shadows)
            blackPointControl->setValue(shadows);
    }
    else if (protectShadowsControl->getValue() != shadows)
    {
        protectShadowsControl->setValue(shadows);
    }

    m_IgnoreLastValueChangeForRangeSelectors = false;

    recalculateHistogramPreview();
    settingsUpdated();
}

void GHTransformationProcess::onSymmetryPointChanged(double midtones)
{
    if (currentTransformation() == GhtTransformation::Linear)
    {
        return;
    }

    m_IgnoreLastValueChangeForRangeSelectors = true;
    if (symmetryPointControl->getValue() != midtones)
        symmetryPointControl->setValue(midtones);

    m_IgnoreLastValueChangeForRangeSelectors = false;

    if (m_ui.spFromHistogram->isChecked())
    {
        m_ui.spFromHistogram->setChecked(false);
        m_preview_histogram_widget->setSPPickerEnabled(false);
        m_ui.spText->setText("");
    }
    else if (m_ui.spFromImage->isChecked())
    {
        m_ui.spFromImage->setChecked(false);
        clearPointSelectionController();
        m_ui.spText->setText("");
    }
    else if (m_ui.spFromImageArea->isChecked())
    {
        m_ui.spFromImageArea->setChecked(false);
        clearAreaSelectionController();
        m_ui.spText->setText("");
    }

    recalculateHistogramPreview();
    settingsUpdated();
}

void GHTransformationProcess::onProtectHightlightsChanged(double highlights)
{
    m_IgnoreLastValueChangeForRangeSelectors = true;
    if (currentTransformation() == GhtTransformation::Linear)
    {
        if (whitePointControl->getValue() != highlights)
            whitePointControl->setValue(highlights);
    }
    else if (protectHighlightsControl->getValue() != highlights)
    {
        protectHighlightsControl->setValue(highlights);
    }

    m_IgnoreLastValueChangeForRangeSelectors = false;

    recalculateHistogramPreview();
    settingsUpdated();
}

void GHTransformationProcess::onSettingsUpdated()
{
    settingsUpdated();
}

void GHTransformationProcess::onSPPickerUpdated(double value)
{
    setPickedSymmetryPoint(value);
}

void GHTransformationProcess::onAlternateImageEnabled(ImageViewControl* ctrl)
{
    // Both the original and the preview histograms are drawn; the alternate image changes neither.
}

void GHTransformationProcess::onAlternateImageDisabled(ImageViewControl* ctrl)
{
    // Both the original and the preview histograms are drawn; the alternate image changes neither.
}

void GHTransformationProcess::onImageDisplayChannelChanged(ImageViewControl* ctrl, int mode)
{
    if (isPreviewEnabled())
    {
        if (isComponent(currentChannel()))
        {
            updateHistogramDisplayChannels();
            return;
        }

        ImageCompositionMode cMode = (ImageCompositionMode)mode;
        if (cMode == ImageCompositionMode::r_mode)
        {
            m_original_histogram_widget->setDisplayChannels(true, false, false);
            m_preview_histogram_widget->setDisplayChannels(true, false, false);
        }
        else if (cMode == ImageCompositionMode::g_mode)
        {
            m_original_histogram_widget->setDisplayChannels(false, true, false);
            m_preview_histogram_widget->setDisplayChannels(false, true, false);
        }
        else if (cMode == ImageCompositionMode::b_mode)
        {
            m_original_histogram_widget->setDisplayChannels(false, false, true);
            m_preview_histogram_widget->setDisplayChannels(false, false, true);
        }
        else
        {
            m_original_histogram_widget->setDisplayChannels(true, true, true);
            m_preview_histogram_widget->setDisplayChannels(true, true, true);
        }
    }
}

an::GeneralizedStretchParameters GHTransformationProcess::stretchParameters()
{
    // The curve drawn is the one a run takes; a screen transfer with no statistics yet draws the identity.
    const an::Result<an::GeneralizedStretchParameters> stretch =
        ghtStretchParametersOf(getParams(), m_CurveStatistics.get());
    if (stretch.hasValue())
    {
        return *stretch;
    }
    an::GeneralizedStretchParameters identity;
    identity.function = an::StretchFunction::ScreenTransfer;
    return identity;
}

std::uint32_t GHTransformationProcess::transferCurveChannel() const
{
    switch (currentChannel())
    {
    case GhtChannel::Green:
        return 1u;
    case GhtChannel::Blue:
        return 2u;
    default:
        return 0u;
    }
}

void GHTransformationProcess::updateControlVisibility()
{
    const GhtTransformation transform = currentTransformation();
    const GhtChannel channel = currentChannel();
    const bool isLinear = transform == GhtTransformation::Linear;
    const bool isImageInversion = transform == GhtTransformation::Inversion;
    const bool isStf = transform == GhtTransformation::ScreenTransfer;
    const bool isGhs = transform == GhtTransformation::GeneralizedHyperbolic;
    const bool usesD = isGhs || transform == GhtTransformation::Histogram || transform == GhtTransformation::Arcsinh;
    const bool usesSpRange = usesD;
    const bool isColour = channel == GhtChannel::Colour;
    const bool manualLum = m_ui.lumCoeffSourceSelector->currentText() == "Manual";

    m_ui.label_9->setVisible(usesD);
    m_ui.stretchFactorValue->setVisible(usesD);
    m_ui.stretchFactorSlider->setVisible(usesD);
    stretchFactorControl->setEnabled(usesD && isEnabled());

    m_ui.label_11->setVisible(isGhs);
    m_ui.localIntensityValue->setVisible(isGhs);
    m_ui.localIntensitySlider->setVisible(isGhs);
    localIntensityControl->setEnabled(isGhs && isEnabled());

    m_ui.label_13->setVisible(usesSpRange);
    m_ui.symmetryPointValue->setVisible(usesSpRange);
    m_ui.symmetryPointSlider->setVisible(usesSpRange);
    m_ui.label_2->setVisible(usesSpRange);
    m_ui.protectShadowsValue->setVisible(usesSpRange);
    m_ui.protectShadowsSlider->setVisible(usesSpRange);
    m_ui.label_5->setVisible(usesSpRange);
    m_ui.protectHighlightsValue->setVisible(usesSpRange);
    m_ui.protectHighlightsSlider->setVisible(usesSpRange);
    symmetryPointControl->setEnabled(usesSpRange && isEnabled());
    protectShadowsControl->setEnabled(usesSpRange && isEnabled());
    protectHighlightsControl->setEnabled(usesSpRange && isEnabled());

    m_ui.blackPointLabel->setVisible(isLinear);
    m_ui.blackPointValue->setVisible(isLinear);
    m_ui.blackPointSlider->setVisible(isLinear);
    m_ui.whitePointLabel->setVisible(isLinear);
    m_ui.whitePointValue->setVisible(isLinear);
    m_ui.whitePointSlider->setVisible(isLinear);
    blackPointControl->setEnabled(isLinear && isEnabled());
    whitePointControl->setEnabled(isLinear && isEnabled());

    m_ui.label_6->setVisible(usesSpRange);
    m_ui.spFromImage->setVisible(usesSpRange);
    m_ui.spFromImageArea->setVisible(usesSpRange);
    m_ui.spFromHistogram->setVisible(usesSpRange);
    m_ui.spText->setVisible(usesSpRange);

    const bool invertible = !isImageInversion && !isStf && !isLinear;
    m_ui.invert->setEnabled(invertible && isEnabled());
    if (!invertible && m_ui.invert->isChecked())
    {
        m_ui.invert->setChecked(false);
    }

    m_ui.linkedStf->setVisible(isStf);
    m_ui.linkedStf->setEnabled(isStf && isEnabled());

    m_ui.humanWeightedLuminance->setVisible(false);
    m_ui.colourClipLabel->setVisible(isColour);
    m_ui.colourClipSelector->setVisible(isColour);
    m_ui.lumCoeffSourceLabel->setVisible(isColour);
    m_ui.lumCoeffSourceSelector->setVisible(isColour);
    m_ui.lumCoeffLabel->setVisible(isColour && manualLum);
    m_ui.lumCoeffRValue->setVisible(isColour && manualLum);
    m_ui.lumCoeffGValue->setVisible(isColour && manualLum);
    m_ui.lumCoeffBValue->setVisible(isColour && manualLum);
    m_ui.colourClipSelector->setEnabled(isColour && isEnabled());
    m_ui.lumCoeffSourceSelector->setEnabled(isColour && isEnabled());
    m_ui.lumCoeffRValue->setEnabled(isColour && manualLum && isEnabled());
    m_ui.lumCoeffGValue->setEnabled(isColour && manualLum && isEnabled());
    m_ui.lumCoeffBValue->setEnabled(isColour && manualLum && isEnabled());

    if (m_original_histogram_widget)
    {
        if (isLinear)
        {
            const double bp = blackPointControl->getValue();
            const double wp = whitePointControl->getValue();
            m_original_histogram_widget->setValues(bp, (bp + wp) * 0.5, wp);
        }
        else
        {
            m_original_histogram_widget->setValues(protectShadowsControl->getValue(), symmetryPointControl->getValue(),
                                                   protectHighlightsControl->getValue());
        }
    }
}

void GHTransformationProcess::updateHistogramDisplayChannels()
{
    const GhtChannel channel = currentChannel();
    if (isComponent(channel))
    {
        m_original_histogram_widget->setDisplayChannels(false, false, false, true);
        m_preview_histogram_widget->setDisplayChannels(false, false, false, true);
    }
    else if (channel == GhtChannel::Red)
    {
        m_original_histogram_widget->setDisplayChannels(true, false, false);
        m_preview_histogram_widget->setDisplayChannels(true, false, false);
    }
    else if (channel == GhtChannel::Green)
    {
        m_original_histogram_widget->setDisplayChannels(false, true, false);
        m_preview_histogram_widget->setDisplayChannels(false, true, false);
    }
    else if (channel == GhtChannel::Blue)
    {
        m_original_histogram_widget->setDisplayChannels(false, false, true);
        m_preview_histogram_widget->setDisplayChannels(false, false, true);
    }
    else
    {
        m_original_histogram_widget->setDisplayChannels(true, true, true);
        m_preview_histogram_widget->setDisplayChannels(true, true, true);
    }
}

void GHTransformationProcess::onPointSelected(const QPoint& point)
{
    pickSymmetryPoint(QRect(point, QSize(1, 1)));
}

void GHTransformationProcess::selectionAreaUpdated() {}

void GHTransformationProcess::selectionAreaChanged()
{
    auto* samplingControl = getSamplingViewControl();
    if (!samplingControl || !samplingControl->getViewWidget())
    {
        return;
    }
    pickSymmetryPoint(samplingControl->getViewWidget()->getCurrentSelectionArea());
}

void GHTransformationProcess::newSelectionFinished()
{
    selectionAreaChanged();
}

void GHTransformationProcess::pickSymmetryPoint(const QRect& region)
{
    const an::consumer::EngineSession* session = engineSession();
    std::shared_ptr<const an::consumer::ImageAsset> target =
        m_image_view_control != nullptr ? m_image_view_control->document().asset() : nullptr;
    const std::optional<an::ImageDescriptor> descriptor = target ? target->descriptor() : std::nullopt;
    if (session == nullptr || !descriptor.has_value())
    {
        return;
    }
    // The point or the area is read from the target, whichever view it was picked on: both show its pixels.
    const QRect bounds(0, 0, static_cast<int>(descriptor->extent().width()),
                       static_cast<int>(descriptor->extent().height()));
    const QRect picked = region.intersected(bounds);
    if (picked.isEmpty())
    {
        return;
    }
    runThenPublish(
        [session, target, picked]() -> an::Result<double> {
            an::Result<an::ui::processes::DialogWork> work = an::ui::processes::admitDialogWork(*session, target.get());
            if (!work.hasValue())
            {
                return work.status();
            }
            return an::ui::processes::meanOfRegion(*session, work->scope.engine(), work->scope.execution(),
                                                   work->lease->image(), work->lease->validity(), picked);
        },
        [this](an::Result<double> mean) {
            if (mean.hasValue())
            {
                setPickedSymmetryPoint(std::clamp(*mean, 0.0, 1.0));
            }
        });
}

void GHTransformationProcess::setPickedSymmetryPoint(double value)
{
    m_IgnoreLastValueChangeForRangeSelectors = true;
    if (symmetryPointControl->getValue() != value)
    {
        symmetryPointControl->setValue(value);
        m_original_histogram_widget->setSymmetryPointValue(value);
    }

    m_IgnoreLastValueChangeForRangeSelectors = false;

    recalculateHistogramPreview();
    settingsUpdated();
}

void GHTransformationProcess::recalculateOriginal()
{
    m_ui.localChannelNormalization->setEnabled(isColourTarget());
    if (m_OriginalRunning)
    {
        m_OriginalPending = true;
        return;
    }
    const an::consumer::EngineSession* session = engineSession();
    std::shared_ptr<const an::consumer::ImageAsset> target =
        m_image_view_control != nullptr ? m_image_view_control->document().asset() : nullptr;
    if (session == nullptr || !target)
    {
        m_OriginalCounts.reset();
        m_original_histogram_widget->setHistogram(nullptr);
        recalculateHistogramPreview();
        return;
    }
    const GHTransformationParams params = getParams();
    const an::ui::HistogramResolution resolution = m_original_histogram_widget->getHistogramResolution();
    m_OriginalRunning = true;
    runThenPublish(
        [session, target, params, resolution]() -> an::Result<an::ui::HistogramDisplayData> {
            an::Result<an::ui::processes::DialogWork> work = an::ui::processes::admitDialogWork(*session, target.get());
            if (!work.hasValue())
            {
                return work.status();
            }
            return an::ui::processes::countStretchHistogram(work->scope.engine(), work->scope.execution(),
                                                            work->lease->image(), work->lease->validity(), resolution,
                                                            componentOf(params.channel), ghtLuminanceWeightsOf(params));
        },
        [this](an::Result<an::ui::HistogramDisplayData> counted) {
            m_OriginalRunning = false;
            if (m_OriginalPending)
            {
                m_OriginalPending = false;
                recalculateOriginal();
                return;
            }
            m_OriginalCounts = counted.hasValue()
                                   ? std::make_shared<const an::ui::HistogramDisplayData>(std::move(*counted))
                                   : nullptr;
            m_original_histogram_widget->setHistogram(m_OriginalCounts);
            updateHistogramDisplayChannels();
            recalculateHistogramPreview();
        });
}

void GHTransformationProcess::recalculateHistogramPreview()
{
    if (m_PreviewRunning)
    {
        m_PreviewPending = true;
        return;
    }
    const an::consumer::EngineSession* session = engineSession();
    std::shared_ptr<const an::ui::HistogramDisplayData> counts = m_OriginalCounts;
    if (session == nullptr || !counts)
    {
        m_preview_histogram_widget->setHistogram(nullptr);
        return;
    }
    const GHTransformationParams params = getParams();
    const an::ui::HistogramResolution resolution = m_preview_histogram_widget->getHistogramResolution();
    m_PreviewRunning = true;
    runThenPublish(
        [session, counts, params, resolution]() -> an::Result<PreviewCounts> {
            // The target is read for the statistics a screen transfer is decided from and for a colour mode's count.
            an::Result<an::ui::processes::DialogWork> work =
                an::ui::processes::admitDialogWork(*session, params.target.get());
            if (!work.hasValue())
            {
                return work.status();
            }
            const an::consumer::OperationScope* scope = &work->scope;
            an::Result<std::shared_ptr<const ScreenTransferStatistics>> statistics = targetStatistics(*work, params);
            if (!statistics.hasValue())
            {
                return statistics.status();
            }
            PreviewCounts preview;
            preview.statistics = *statistics;
            const std::optional<an::ImageDescriptor> descriptor =
                params.target ? params.target->descriptor() : std::nullopt;
            if (!descriptor.has_value())
            {
                return refusal(an::ErrorCode::NotInitialized, "the dialog has no image");
            }
            an::Result<an::StretchImageRequest> request =
                ghtStretchRequestOf(params, *descriptor, preview.statistics.get());
            if (!request.hasValue())
            {
                return request.status();
            }
            if (request->colorMode == an::StretchColorMode::Independent)
            {
                // A curve on the stored channels moves each count to where its value goes, channel by channel.
                std::array<an::GeneralizedStretchParameters, 3u> stretches{};
                for (std::size_t channel = 0u; channel < stretches.size(); ++channel)
                {
                    stretches[channel] = request->stretch;
                    stretches[channel].screenTransfer.fill(request->stretch.screenTransfer[channel]);
                }
                an::Result<an::ui::HistogramDisplayData> remapped = an::ui::processes::remapHistogramThroughStretch(
                    scope->execution(), *counts, stretches, request->channelEnabled);
                if (!remapped.hasValue())
                {
                    return remapped.status();
                }
                preview.counts = std::move(*remapped);
                return preview;
            }
            // A colour mode moves every channel by what the component does: the target stretched, and counted again.
            const an::consumer::AssetReadLease* lease = &*work->lease;
            an::Result<an::consumer::AnyImage> working =
                an::ui::processes::makeStretchWorkingCopy(scope->engine(), scope->execution(), lease->image());
            if (!working.hasValue())
            {
                return working.status();
            }
            std::optional<an::ValidityMaskView<const std::uint8_t>> maskView;
            const an::KernelValidityView validity = an::ui::processes::stretchValidity(
                std::visit([](const auto& typed) -> const an::ImageMetadata& { return typed.metadata(); },
                           lease->image()),
                lease->validity(), maskView);
            const an::Status stretched = an::ui::processes::stretchWorkingImage(scope->engine(), scope->execution(),
                                                                                *working, *request, validity);
            if (stretched.failed())
            {
                return stretched;
            }
            an::Result<an::ui::HistogramDisplayData> recounted = an::ui::processes::countStretchHistogram(
                scope->engine(), scope->execution(), *working, lease->validity(), resolution,
                componentOf(params.channel), ghtLuminanceWeightsOf(params));
            if (!recounted.hasValue())
            {
                return recounted.status();
            }
            preview.counts = std::move(*recounted);
            return preview;
        },
        [this](an::Result<PreviewCounts> preview) {
            m_PreviewRunning = false;
            if (m_PreviewPending)
            {
                m_PreviewPending = false;
                recalculateHistogramPreview();
                return;
            }
            if (!preview.hasValue())
            {
                m_preview_histogram_widget->setHistogram(nullptr);
                return;
            }
            if (preview->statistics)
            {
                m_CurveStatistics = preview->statistics;
            }
            m_preview_histogram_widget->setHistogram(
                std::make_shared<const an::ui::HistogramDisplayData>(std::move(preview->counts)));
            updateHistogramDisplayChannels();
            m_original_histogram_widget->update();
            m_preview_histogram_widget->update();
        });
}

GHTransformationProcess::~GHTransformationProcess()
{
    clearAreaSelectionController();
    clearPointSelectionController();
}

void GHTransformationProcess::on_channelSelector2_currentTextChanged(const QString& str)
{
    updateControlVisibility();
    recalculateOriginal();
    settingsUpdated();
}

void GHTransformationProcess::on_plotResolution_currentTextChanged(const QString& str)
{
    const std::optional<an::ui::HistogramResolution> resolution = an::ui::processes::histogramResolutionForLabel(str);
    if (!resolution.has_value())
    {
        return;
    }
    m_preview_histogram_widget->setHistogramResolution(*resolution);
    m_original_histogram_widget->setHistogramResolution(*resolution);
    recalculateOriginal();
}

void GHTransformationProcess::on_transformationTypeSelector_currentTextChanged(const QString& str)
{
    disarmSymmetryPointPickers();
    updateControlVisibility();
    recalculateHistogramPreview();
    settingsUpdated();
}

void GHTransformationProcess::on_HumanWeightedLuminanceClicked(Qt::CheckState state)
{
    recalculateHistogramPreview();
    settingsUpdated();
}

void GHTransformationProcess::on_colourClipSelector_currentTextChanged(const QString& str)
{
    recalculateOriginal();
    settingsUpdated();
}

void GHTransformationProcess::on_lumCoeffSourceSelector_currentTextChanged(const QString& str)
{
    updateControlVisibility();
    recalculateOriginal();
    settingsUpdated();
}

void GHTransformationProcess::on_lumCoeffChanged(double value)
{
    recalculateOriginal();
    settingsUpdated();
}

void GHTransformationProcess::on_linkedStfClicked(Qt::CheckState state)
{
    recalculateHistogramPreview();
    settingsUpdated();
}

void GHTransformationProcess::on_InvertClicked(Qt::CheckState state)
{
    recalculateHistogramPreview();
    settingsUpdated();
}

void GHTransformationProcess::on_stretchFactorChanged(double value)
{
    if (!m_IgnoreLastValueChangeForRangeSelectors)
    {
        recalculateHistogramPreview();
        settingsUpdated();
    }
}

void GHTransformationProcess::on_localIntensityChanged(double value)
{
    if (!m_IgnoreLastValueChangeForRangeSelectors)
    {
        recalculateHistogramPreview();
        settingsUpdated();
    }
}

void GHTransformationProcess::on_symmetryPointChanged(double value)
{
    bool originalIgnoreLastValueChangeForRangeSelectors = m_IgnoreLastValueChangeForRangeSelectors;

    m_IgnoreLastValueChangeForRangeSelectors = true;

    if (protectShadowsControl->getValue() > value)
    {
        protectShadowsControl->setValue(value);
    }

    if (protectHighlightsControl->getValue() < value)
    {
        protectHighlightsControl->setValue(value);
    }

    m_IgnoreLastValueChangeForRangeSelectors = originalIgnoreLastValueChangeForRangeSelectors;
    if (!m_IgnoreLastValueChangeForRangeSelectors)
    {
        m_original_histogram_widget->setSymmetryPointValue(value);
        if (m_ui.spFromHistogram->isChecked())
        {
            m_ui.spFromHistogram->setChecked(false);
            m_preview_histogram_widget->setSPPickerEnabled(false);
            m_ui.spText->setText("");
        }
        else if (m_ui.spFromImage->isChecked())
        {
            m_ui.spFromImage->setChecked(false);
            clearPointSelectionController();
            m_ui.spText->setText("");
        }
        else if (m_ui.spFromImageArea->isChecked())
        {
            m_ui.spFromImageArea->setChecked(false);
            clearAreaSelectionController();
            m_ui.spText->setText("");
        }

        recalculateHistogramPreview();
        settingsUpdated();
    }
}

void GHTransformationProcess::on_protectShadowsChanged(double value)
{
    if (value > symmetryPointControl->getValue())
    {
        value = symmetryPointControl->getValue();
        protectShadowsControl->setValue(value);
    }

    if (!m_IgnoreLastValueChangeForRangeSelectors)
    {
        m_original_histogram_widget->setProtectShadowsValue(value);

        recalculateHistogramPreview();
        settingsUpdated();
    }
}

void GHTransformationProcess::on_protectHighlightsChanged(double value)
{
    if (value < symmetryPointControl->getValue())
    {
        value = symmetryPointControl->getValue();
        protectHighlightsControl->setValue(symmetryPointControl->getValue());
    }

    if (!m_IgnoreLastValueChangeForRangeSelectors)
    {
        m_original_histogram_widget->setProtectHighlightsValue(value);

        recalculateHistogramPreview();
        settingsUpdated();
    }
}

void GHTransformationProcess::on_blackPointChanged(double value)
{
    if (value >= whitePointControl->getValue())
    {
        value = whitePointControl->getValue() - 0.0001;
        blackPointControl->setValue((std::max)(0.0, value));
    }

    if (!m_IgnoreLastValueChangeForRangeSelectors)
    {
        if (currentTransformation() == GhtTransformation::Linear)
        {
            m_original_histogram_widget->setProtectShadowsValue(value);
        }
        recalculateHistogramPreview();
        settingsUpdated();
    }
}

void GHTransformationProcess::on_whitePointChanged(double value)
{
    if (value <= blackPointControl->getValue())
    {
        value = blackPointControl->getValue() + 0.0001;
        whitePointControl->setValue((std::min)(1.0, value));
    }

    if (!m_IgnoreLastValueChangeForRangeSelectors)
    {
        if (currentTransformation() == GhtTransformation::Linear)
        {
            m_original_histogram_widget->setProtectHighlightsValue(value);
        }
        recalculateHistogramPreview();
        settingsUpdated();
    }
}

void GHTransformationProcess::on_gridClicked(bool checked)
{
    m_original_histogram_widget->setGridEnabled(checked);
    m_preview_histogram_widget->setGridEnabled(checked);
}

void GHTransformationProcess::on_transferCurveClicked(bool checked)
{
    m_original_histogram_widget->setTransferCurveEnabled(checked);
}

void GHTransformationProcess::on_ZoomInButton()
{
    m_original_histogram_widget->zoomIn();
    m_preview_histogram_widget->zoomIn();
}

void GHTransformationProcess::on_ZoomOutButton()
{
    m_original_histogram_widget->zoomOut();
    m_preview_histogram_widget->zoomOut();
}

void GHTransformationProcess::on_ZoomResetButton()
{
    m_original_histogram_widget->resetZoom();
    m_preview_histogram_widget->resetZoom();
}

void GHTransformationProcess::on_LocalChannelNormalization(bool checked)
{
    m_original_histogram_widget->setLocalChannelNormalization(checked);
    m_preview_histogram_widget->setLocalChannelNormalization(checked);
}

void GHTransformationProcess::on_rt_showClippingInfo(bool checked)
{
    m_original_histogram_widget->setClippingInfoEnabled(checked);
    m_preview_histogram_widget->setClippingInfoEnabled(checked);
}

void GHTransformationProcess::on_showHistogramAreas(bool checked)
{
    m_original_histogram_widget->setAreaModeEnabled(checked);
    m_preview_histogram_widget->setAreaModeEnabled(checked);
}

void GHTransformationProcess::on_spFromImageClicked(bool checked)
{
    if (checked)
    {
        if (m_ui.spFromHistogram->isChecked())
        {
            m_ui.spFromHistogram->setChecked(false);
            m_preview_histogram_widget->setSPPickerEnabled(false);
        }
        else if (m_ui.spFromImageArea->isChecked())
        {
            m_ui.spFromImageArea->setChecked(false);
            clearAreaSelectionController();
        }

        rebuildPointSelectionController();

        m_ui.spText->setText("** PICK VALUE FROM IMAGE **");
    }
    else
    {
        clearPointSelectionController();
        m_ui.spText->setText("");
    }
}

void GHTransformationProcess::on_spFromImageAreaClicked(bool checked)
{
    if (checked)
    {
        if (m_ui.spFromHistogram->isChecked())
        {
            m_ui.spFromHistogram->setChecked(false);
            m_preview_histogram_widget->setSPPickerEnabled(false);
        }
        else if (m_ui.spFromImage->isChecked())
        {
            m_ui.spFromImage->setChecked(false);
            clearPointSelectionController();
        }

        rebuildAreaSelectionController();

        m_ui.spText->setText("** PICK VALUE FROM IMAGE AREA **");
    }
    else
    {
        clearAreaSelectionController();
        m_ui.spText->setText("");
    }
}

void GHTransformationProcess::on_spFromHistogramClicked(bool checked)
{
    if (checked)
    {
        if (m_ui.spFromImage->isChecked())
        {
            m_ui.spFromImage->setChecked(false);
            clearPointSelectionController();
        }
        else if (m_ui.spFromImageArea->isChecked())
        {
            m_ui.spFromImageArea->setChecked(false);
            clearAreaSelectionController();
        }

        m_preview_histogram_widget->setSPPickerEnabled(true);
        m_ui.spText->setText("** PICK VALUE FROM HISTOGRAM **");
    }
    else
    {
        m_preview_histogram_widget->setSPPickerEnabled(false);
        m_ui.spText->setText("");
    }
}

void GHTransformationProcess::previewControlStarted()
{
    if (m_ui.spFromImage->isChecked())
    {
        rebuildPointSelectionController();
    }
    else if (m_ui.spFromImageArea->isChecked())
    {
        rebuildAreaSelectionController();
    }
}

void GHTransformationProcess::previewControlClosed()
{
    if (m_ui.spFromImage->isChecked())
    {
        rebuildPointSelectionController();
    }
    else if (m_ui.spFromImageArea->isChecked())
    {
        rebuildAreaSelectionController();
    }
}

ImageViewControl* GHTransformationProcess::getSamplingViewControl() const
{
    if (m_rt_preview_view_control)
    {
        return m_rt_preview_view_control;
    }

    return m_image_view_control;
}

void GHTransformationProcess::clearPointSelectionController()
{
    if (m_PointSelectionController)
    {
        m_PointSelectionController->enablePointSelection(false);
        m_PointSelectionController.reset();
    }
}

void GHTransformationProcess::disarmSymmetryPointPickers()
{
    // A picker belongs to the transformation it was armed for: once that changes, its button may be hidden, and a click
    // on the image or the histogram would still change the symmetry point it set.
    if (m_ui.spFromHistogram->isChecked())
    {
        m_ui.spFromHistogram->setChecked(false);
        m_preview_histogram_widget->setSPPickerEnabled(false);
    }
    if (m_ui.spFromImage->isChecked())
    {
        m_ui.spFromImage->setChecked(false);
        clearPointSelectionController();
    }
    if (m_ui.spFromImageArea->isChecked())
    {
        m_ui.spFromImageArea->setChecked(false);
        clearAreaSelectionController();
    }
    m_ui.spText->setText("");
}

void GHTransformationProcess::clearAreaSelectionController()
{
    if (m_AreaSelectionController)
    {
        m_AreaSelectionController->destroy();
        m_AreaSelectionController.reset();
    }
}

void GHTransformationProcess::rebuildPointSelectionController()
{
    clearPointSelectionController();

    auto* samplingControl = getSamplingViewControl();
    if (!samplingControl)
    {
        return;
    }

    m_PointSelectionController =
        std::make_unique<ImagePointSelectionController>(samplingControl->getScrollArea(), this);
    m_PointSelectionController->enablePointSelection(true);
    m_PointSelectionController->setDrawCoords(true);
}

void GHTransformationProcess::rebuildAreaSelectionController()
{
    clearAreaSelectionController();

    auto* samplingControl = getSamplingViewControl();
    if (!samplingControl)
    {
        return;
    }

    m_AreaSelectionController = std::make_unique<ImageCropController>(samplingControl->getScrollArea(), this);
    m_AreaSelectionController->setListenerEnabled(true);
    samplingControl->getViewWidget()->setImageViewSelectionStyle(ImageViewSelectionStyle::AREA_SELECTION_STYLE);
    m_AreaSelectionController->enableOneClickMove(true);
    m_AreaSelectionController->startNewSelection();
}

void GHTransformationProcess::updateSpPickerButtonsStyle()
{
    auto applyVariant = [](QPushButton* button) {
        if (!button)
        {
            return;
        }

        const QString variant = button->isChecked() ? QStringLiteral("primary") : QStringLiteral("secondary");
        if (button->property("anButtonVariant").toString() != variant)
        {
            button->setProperty("anButtonVariant", variant);
            button->style()->unpolish(button);
            button->style()->polish(button);
        }
        button->update();
    };

    applyVariant(m_ui.spFromImage);
    applyVariant(m_ui.spFromImageArea);
    applyVariant(m_ui.spFromHistogram);
}

void GHTransformationProcess::updateSupportedImageChannels()
{
    const GhtChannel previous = currentChannel();
    const QSignalBlocker blocker(m_ui.channelSelector2);
    m_ui.channelSelector2->clear();

    // Each entry carries the choice it names, which is what a run reads; its text is the dialog's own.
    if (!isColourTarget())
    {
        m_ui.channelSelector2->addItem(QString::fromLatin1(an::ui::kProcessGreyscaleChannelLabel),
                                       static_cast<int>(GhtChannel::Rgb));
        m_ui.channelSelector2->setEnabled(false);
        m_ui.humanWeightedLuminance->setEnabled(false);
    }
    else
    {
        m_ui.channelSelector2->setEnabled(true);
        const auto iconProvider = process_utils::currentIconProvider();
        for (const ChannelChoice& choice : kChannels)
        {
            if (iconProvider)
            {
                m_ui.channelSelector2->addItem(iconProvider->getIcon(QString::fromLatin1(choice.icon)),
                                               QString::fromLatin1(choice.label), static_cast<int>(choice.channel));
            }
            else
            {
                m_ui.channelSelector2->addItem(QString::fromLatin1(choice.label), static_cast<int>(choice.channel));
            }
        }

        const int previousIndex = m_ui.channelSelector2->findData(static_cast<int>(previous));
        if (previousIndex >= 0)
        {
            m_ui.channelSelector2->setCurrentIndex(previousIndex);
        }

        m_ui.humanWeightedLuminance->setEnabled(currentChannel() == GhtChannel::Lightness);
    }

    updateControlVisibility();
    updateHistogramDisplayChannels();
}

void GHTransformationProcess::updateSupportedPlotResolutions()
{
    m_ui.plotResolution->clear();
    for (const an::ui::processes::HistogramResolutionChoice& choice : an::ui::processes::histogramResolutionChoices())
    {
        m_ui.plotResolution->addItem(QString::fromLatin1(choice.label));
    }
}

an::ui::ProcessBodyCoverage
GHTransformationProcess::typedProcessBodyCoverage(const GHTransformationParams& params) const
{
    // The stretched working copy, made from the source, replaces the destination.
    return an::ui::ProcessBodyCoverage::WritesEverySample;
}

an::Status GHTransformationProcess::executeTypedProcessImpl(const an::ui::ProcessSource& source,
                                                            an::ui::ProcessDestination& destination,
                                                            const an::ui::ProcessRun& run,
                                                            const GHTransformationParams& params,
                                                            an::ui::ExecutionScope& scope)
{
    const an::consumer::EngineSession* session = engineSession();
    if (session == nullptr)
    {
        return refusal(an::ErrorCode::NotInitialized, "no engine session is lent to the generalized stretch");
    }
    const std::optional<an::ImageDescriptor> described = source.descriptor();
    if (!described.has_value())
    {
        return refusal(an::ErrorCode::NotInitialized, "the source holds nothing");
    }
    // A screen transfer is derived from the whole image the run changes; a preview area's from the whole target.
    std::shared_ptr<const ScreenTransferStatistics> statistics;
    if (params.transformation == GhtTransformation::ScreenTransfer)
    {
        auto cache = params.statistics
                         ? params.statistics
                         : std::make_shared<an::ui::processes::StretchStatisticsCache<ScreenTransferStatistics>>();
        an::Result<std::shared_ptr<const ScreenTransferStatistics>> measured =
            an::ui::processes::screenTransferStatisticsFor(scope.engine(), scope.execution(), source, run, params.area,
                                                           params.target, *cache);
        if (!measured.hasValue())
        {
            return measured.status();
        }
        statistics = std::move(*measured);
    }
    const an::Result<an::StretchImageRequest> request = ghtStretchRequestOf(params, *described, statistics.get());
    if (!request.hasValue())
    {
        return request.status();
    }

    std::optional<an::ValidityMaskView<const std::uint8_t>> maskView;
    const an::KernelValidityView validity =
        an::ui::processes::stretchValidity(source.metadata(), source.validity(), maskView);
    const StretchRecord record{.effect = an::kStretchImageEffect,
                               .operation = "an::stretchImage",
                               .detail = describeParameters(params),
                               .processingOperation = kProcessingOperation};
    // The result is made from the source - its codes looked up where that is exact, a working copy stretched otherwise
    // - so the destination it replaces need not start as one; a channel the stretch leaves alone keeps its samples.
    return an::ui::processes::publishStretchOfSource(*session, scope.engine(), scope.execution(), source, *request,
                                                     validity, record, destination);
}

an::ui::ProcessStep GHTransformationProcess::describeTypedProcessStep(const GHTransformationParams& params) const
{
    return an::ui::ProcessStep{.name = getProcessName(), .description = describeParameters(params)};
}

void GHTransformationProcess::resetSettings()
{
    ImageRTPreviewControl::resetSettings();

    m_IgnoreLastValueChangeForRangeSelectors = true;
    stretchFactorControl->setValue(0.0);
    localIntensityControl->setValue(0.0);
    symmetryPointControl->setValue(0.0);
    protectShadowsControl->setValue(0.0);
    protectHighlightsControl->setValue(1.0);
    blackPointControl->setValue(0.0);
    whitePointControl->setValue(1.0);
    m_IgnoreLastValueChangeForRangeSelectors = false;

    m_original_histogram_widget->setValues(0.0, 0.0, 1.0);

    m_original_histogram_widget->resetZoom();
    m_preview_histogram_widget->resetZoom();
    clearPointSelectionController();
    clearAreaSelectionController();
    m_preview_histogram_widget->setSPPickerEnabled(false);
    m_ui.spFromImage->setChecked(false);
    m_ui.spFromImageArea->setChecked(false);
    m_ui.spFromHistogram->setChecked(false);
    updateSpPickerButtonsStyle();
    m_ui.spText->setText("");

    m_ui.humanWeightedLuminance->setChecked(true);
    m_ui.colourClipSelector->setCurrentText("Clip");
    m_ui.lumCoeffSourceSelector->setCurrentText("Default");
    m_ui.lumCoeffRValue->setValue(1.0);
    m_ui.lumCoeffGValue->setValue(1.0);
    m_ui.lumCoeffBValue->setValue(1.0);
    m_ui.linkedStf->setChecked(false);
    m_ui.transformationTypeSelector->setCurrentText("Generalized Hyperbolic");
    {
        const QSignalBlocker blocker(m_ui.channelSelector2);
        const int whole = m_ui.channelSelector2->findData(static_cast<int>(GhtChannel::Rgb));
        m_ui.channelSelector2->setCurrentIndex((std::max)(0, whole));
    }

    m_ui.invert->setChecked(false);
    updateControlVisibility();
    updateHistogramDisplayChannels();

    recalculateOriginal();
    settingsUpdated();
}

bool GHTransformationProcess::isPreviewEnabled() const
{
    return m_ui.preview->isChecked();
}

void GHTransformationProcess::enablePreview(bool enabled)
{
    m_ui.preview->setChecked(enabled);
}

void GHTransformationProcess::settingsUpdatedImpl()
{
    m_ui.buttonBox->button(QDialogButtonBox::Apply)->setEnabled(true);

    m_original_histogram_widget->update();
    m_preview_histogram_widget->update();
}

void GHTransformationProcess::enableProcessControlsImpl(bool enabled)
{
    m_ui.buttonBox->button(QDialogButtonBox::Apply)->setEnabled(enabled);

    m_ui.buttonBox->button(QDialogButtonBox::Close)->setEnabled(enabled);
    m_ui.buttonBox->button(QDialogButtonBox::Reset)->setEnabled(enabled);

    stretchFactorControl->setEnabled(enabled);
    localIntensityControl->setEnabled(enabled);
    symmetryPointControl->setEnabled(enabled);
    protectShadowsControl->setEnabled(enabled);
    protectHighlightsControl->setEnabled(enabled);
    blackPointControl->setEnabled(enabled);
    whitePointControl->setEnabled(enabled);

    m_original_histogram_widget->setEnabled(enabled);
    m_preview_histogram_widget->setEnabled(enabled);

    m_ui.spFromImage->setEnabled(enabled);
    m_ui.spFromImageArea->setEnabled(enabled);
    m_ui.spFromHistogram->setEnabled(enabled);
    m_ui.spText->setEnabled(enabled);

    m_ui.humanWeightedLuminance->setEnabled(enabled);
    m_ui.colourClipSelector->setEnabled(enabled);
    m_ui.lumCoeffSourceSelector->setEnabled(enabled);
    m_ui.lumCoeffRValue->setEnabled(enabled);
    m_ui.lumCoeffGValue->setEnabled(enabled);
    m_ui.lumCoeffBValue->setEnabled(enabled);
    m_ui.linkedStf->setEnabled(enabled);

    m_ui.channelSelector2->setEnabled(enabled && isColourTarget());

    m_ui.invert->setEnabled(enabled);

    m_ui.transformationTypeSelector->setEnabled(enabled);
    updateControlVisibility();
}

GHTransformationParams GHTransformationProcess::getParams() const
{
    GHTransformationParams params;
    params.transformation = currentTransformation();
    params.invert = m_ui.invert->isChecked();
    params.channel = currentChannel();

    params.stretchFactor = stretchFactorControl->getValue();
    params.localIntensity = localIntensityControl->getValue();
    params.protectShadows = protectShadowsControl->getValue();
    params.symmetryPoint = symmetryPointControl->getValue();
    params.protectHighlights = protectHighlightsControl->getValue();
    params.blackPoint = blackPointControl->getValue();
    params.whitePoint = whitePointControl->getValue();

    params.linkedStf = m_ui.linkedStf->isChecked();
    params.colourClip = m_ui.colourClipSelector->currentText() == "Rescale" ? an::StretchOverflowPolicy::Rescale
                                                                            : an::StretchOverflowPolicy::Clip;
    if (m_ui.lumCoeffSourceSelector->currentText() == "Manual")
    {
        params.luminanceSource = GhtLuminanceSource::Manual;
    }
    else if (m_ui.lumCoeffSourceSelector->currentText() == "Image")
    {
        params.luminanceSource = GhtLuminanceSource::Image;
    }
    else
    {
        params.luminanceSource = GhtLuminanceSource::Default;
    }
    params.luminanceCoefficients = {m_ui.lumCoeffRValue->value(), m_ui.lumCoeffGValue->value(),
                                    m_ui.lumCoeffBValue->value()};

    params.target = m_image_view_control != nullptr ? m_image_view_control->document().asset() : nullptr;
    params.area = previewArea();
    params.statistics = m_Statistics;

    return params;
}
