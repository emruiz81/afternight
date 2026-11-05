// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "MultiscaleLinearProcess.h"
#include "controls/ImageViewControl.h"
#include "processes/PointwiseProcessUtils.h"
#include "processes/ProcessCommonUtils.h"

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Image/Image.h>
#include <AfterNightEngine/Image/ImageMetadata.h>
#include <AfterNightEngine/Image/Provenance.h>

#include <QTreeWidgetItem>

#include <array>
#include <optional>
#include <tuple>
#include <utility>

namespace
{
constexpr int kDefaultDetailLayers = 6;
constexpr double kDefaultDeringingDark = 0.1;
constexpr double kDefaultDeringingBright = 0.0;

using process_utils::multiscale::ResidualTransfer;

QString algorithmToString(an::MultiscaleAlgorithm algorithm)
{
    switch (algorithm)
    {
    case an::MultiscaleAlgorithm::Starlet:
        return "Starlet Transform";
    case an::MultiscaleAlgorithm::MultiscaleLinear:
        return "Multiscale Linear Transform";
    default:
        return "Starlet Transform";
    }
}

an::MultiscaleAlgorithm algorithmFromIndex(int index)
{
    return index == 1 ? an::MultiscaleAlgorithm::MultiscaleLinear : an::MultiscaleAlgorithm::Starlet;
}

/// The scaling selector's entries, in the order the dialog lists them.
constexpr std::array<an::MultiscaleScalingFunction, 15u> kScalingFunctions{
    an::MultiscaleScalingFunction::LinearInterpolation3,
    an::MultiscaleScalingFunction::BSpline5,
    an::MultiscaleScalingFunction::Gaussian5,
    an::MultiscaleScalingFunction::Gaussian7,
    an::MultiscaleScalingFunction::Gaussian9,
    an::MultiscaleScalingFunction::Gaussian11,
    an::MultiscaleScalingFunction::SmallScale3,
    an::MultiscaleScalingFunction::SmallScale4,
    an::MultiscaleScalingFunction::SmallScale5,
    an::MultiscaleScalingFunction::SmallScale6,
    an::MultiscaleScalingFunction::SmallScale8,
    an::MultiscaleScalingFunction::SmallScale12,
    an::MultiscaleScalingFunction::SmallScale16,
    an::MultiscaleScalingFunction::SmallScale24,
    an::MultiscaleScalingFunction::SmallScale32};

// Convert scaling function enum to display string
QString scalingFunctionToString(an::MultiscaleScalingFunction sf)
{
    switch (sf)
    {
    case an::MultiscaleScalingFunction::LinearInterpolation3:
        return "Linear Interpolation (3)";
    case an::MultiscaleScalingFunction::BSpline5:
        return "B3 Spline (5)";
    case an::MultiscaleScalingFunction::SmallScale3:
        return "Small Scale 3 (3)";
    case an::MultiscaleScalingFunction::SmallScale4:
        return "Small Scale 4 (3)";
    case an::MultiscaleScalingFunction::SmallScale5:
        return "Small Scale 5 (3)";
    case an::MultiscaleScalingFunction::SmallScale6:
        return "Small Scale 6 (3)";
    case an::MultiscaleScalingFunction::SmallScale8:
        return "Small Scale 8 (3)";
    case an::MultiscaleScalingFunction::SmallScale12:
        return "Small Scale 12 (3)";
    case an::MultiscaleScalingFunction::SmallScale16:
        return "Small Scale 16 (3)";
    case an::MultiscaleScalingFunction::SmallScale24:
        return "Small Scale 24 (3)";
    case an::MultiscaleScalingFunction::SmallScale32:
        return "Small Scale 32 (3)";
    case an::MultiscaleScalingFunction::Gaussian5:
        return "Gaussian (5)";
    case an::MultiscaleScalingFunction::Gaussian7:
        return "Gaussian (7)";
    case an::MultiscaleScalingFunction::Gaussian9:
        return "Gaussian (9)";
    case an::MultiscaleScalingFunction::Gaussian11:
        return "Gaussian (11)";
    default:
        return "Unknown";
    }
}

// LSTF mode to string
QString lstfModeToString(ResidualTransfer mode)
{
    switch (mode)
    {
    case ResidualTransfer::None:
        return "None";
    case ResidualTransfer::Hyperbolic:
        return "Hyperbolic";
    case ResidualTransfer::Log10:
        return "Log10";
    case ResidualTransfer::NaturalLog:
        return "Natural Log (ln)";
    default:
        return "None";
    }
}

ResidualTransfer lstfModeFromIndex(int index)
{
    switch (index)
    {
    case 1:
        return ResidualTransfer::Hyperbolic;
    case 2:
        return ResidualTransfer::Log10;
    case 3:
        return ResidualTransfer::NaturalLog;
    default:
        return ResidualTransfer::None;
    }
}

/// What a layer preview mode of the dialog shows.
process_utils::multiscale::LayerPreview layerPreviewOf(MultiscalePreviewMode mode)
{
    switch (mode)
    {
    case MultiscalePreviewMode::LayerIncreasingPixels:
        return process_utils::multiscale::LayerPreview::IncreasingPixels;
    case MultiscalePreviewMode::LayerDecreasingPixels:
        return process_utils::multiscale::LayerPreview::DecreasingPixels;
    default:
        return process_utils::multiscale::LayerPreview::AllChanges;
    }
}

} // anonymous namespace

namespace an::ui::processes::multiscale_detail
{

an::MultiscaleScalingFunction scalingFunctionFromIndex(int index) noexcept
{
    if (index >= 0 && index < static_cast<int>(kScalingFunctions.size()))
    {
        return kScalingFunctions[static_cast<std::size_t>(index)];
    }
    return an::MultiscaleScalingFunction::BSpline5;
}

process_utils::multiscale::TransformRequest transformRequestOf(const MultiscaleParams& params)
{
    process_utils::multiscale::TransformRequest request;
    request.algorithm = params.algorithm;
    request.scaling = params.scalingFunction;
    request.spacing = params.spacing;
    request.linearScale = static_cast<std::uint32_t>((std::max)(params.linearScale, 1));
    request.detailLayers = static_cast<std::uint32_t>((std::max)(params.numDetailLayers, 1));
    for (const MultiscaleLayerParams& layer : params.layers)
    {
        request.layers.push_back(
            process_utils::multiscale::LayerSettings{.enabled = layer.enabled,
                                                     .bias = static_cast<double>(layer.bias),
                                                     .noiseReduction = layer.nrEnabled && !layer.isResidual,
                                                     .noiseThreshold = static_cast<double>(layer.nrThreshold),
                                                     .noiseAmount = static_cast<double>(layer.nrAmount),
                                                     .noiseIterations = layer.nrIterations});
    }
    request.kSigma = params.ksigmaEnabled;
    request.kSigmaK = static_cast<double>(params.ksigmaK);
    request.kSigmaAmount = static_cast<double>(params.ksigmaAmount);
    request.kSigmaSoft = params.ksigmaSoft;
    request.kSigmaMultiresolutionSupport = params.ksigmaUseMrs;
    request.linearMask = params.linearMaskEnabled;
    request.linearMaskAmplification = params.linearMaskAmplification;
    request.linearMaskSmoothness = static_cast<double>(params.linearMaskSmoothness);
    request.linearMaskInverted = params.linearMaskInverted;
    request.deringing = params.deringingEnabled;
    request.deringingDark = static_cast<double>(params.deringingDark);
    request.deringingBright = static_cast<double>(params.deringingBright);
    request.residualTransfer = params.lstfMode;
    request.residualBreakpoint = static_cast<double>(params.lstfBreakpoint);
    request.dynamicRangeLow = static_cast<double>(params.dreLow);
    request.dynamicRangeHigh = static_cast<double>(params.dreHigh);
    return request;
}

std::string describeTransform(const MultiscaleParams& params)
{
    const std::string spacing = params.spacing == an::MultiscaleLayerSpacing::Dyadic
                                    ? std::string("Dyadic")
                                    : fmt::format("Linear (scale {})", params.linearScale);
    const std::string algorithm = params.algorithm == an::MultiscaleAlgorithm::MultiscaleLinear
                                      ? "Multiscale Linear Transform"
                                      : "Starlet Transform";
    std::string description =
        fmt::format("Algorithm: {}, Layers: {}, Spacing: {}", algorithm, params.numDetailLayers, spacing);
    if (params.algorithm == an::MultiscaleAlgorithm::Starlet)
    {
        description += fmt::format(", Scaling: {}", scalingFunctionToString(params.scalingFunction).toStdString());
    }
    if (params.ksigmaEnabled)
    {
        description += fmt::format(", k-sigma: k={:.2f}, amt={:.2f}{}{}", params.ksigmaK, params.ksigmaAmount,
                                   params.ksigmaSoft ? ", soft" : "", params.ksigmaUseMrs ? ", mrs" : "");
    }
    if (params.linearMaskEnabled)
    {
        description += fmt::format(", linear mask: amp={}, smooth={:.2f}{}", params.linearMaskAmplification,
                                   params.linearMaskSmoothness, params.linearMaskInverted ? ", inverted" : "");
    }
    if (params.deringingEnabled)
    {
        description += fmt::format(", dering: dark={:.2f}, bright={:.2f}{}", params.deringingDark,
                                   params.deringingBright, params.deringingOutputMaps ? ", maps" : "");
    }
    if (params.lstfMode != ResidualTransfer::None)
    {
        description += fmt::format(", LSTF: {}", lstfModeToString(params.lstfMode).toStdString());
        if (params.lstfMode == ResidualTransfer::Hyperbolic)
        {
            description += fmt::format(" (bp={:.2f})", params.lstfBreakpoint);
        }
    }
    if (params.dreLow > 0.0f || params.dreHigh > 0.0f)
    {
        description += fmt::format(", DRE: low={:.2f}, high={:.2f}", params.dreLow, params.dreHigh);
    }
    if (params.colourTarget)
    {
        description += " in channel: " + params.channelLabel;
    }
    return description;
}

} // namespace an::ui::processes::multiscale_detail

namespace detail = an::ui::processes::multiscale_detail;

MultiscaleLinearProcess::MultiscaleLinearProcess(ImageViewControl* control)
    : an::ui::TypedProcessControl<MultiscaleParams>(control, getProcessNameStatic())
{
    m_ui = an::ui::processes::buildMultiscaleLinearProcessUi(this);

    initializeStandardProcess(m_ui.horizontalLayoutMain, m_ui.verticalLayout_2, m_ui.preview, m_ui.buttonBox,
                              m_ui.targetImageSelector, m_ui.channelSelector, true, true);

    // Extra connection for preview mode combo box
    QObject::connect(m_ui.preview, &QCheckBox::checkStateChanged, this,
                     [this](Qt::CheckState) { updatePreviewModeComboBoxEnabled(); });

    // Transform settings connections
    QObject::connect(m_ui.algorithmComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
                     &MultiscaleLinearProcess::onAlgorithmChanged);
    QObject::connect(m_ui.spacingComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
                     &MultiscaleLinearProcess::onSpacingChanged);
    QObject::connect(m_ui.linearScaleSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this,
                     &MultiscaleLinearProcess::onLinearScaleChanged);
    QObject::connect(m_ui.numLayersSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this,
                     &MultiscaleLinearProcess::onNumLayersChanged);
    QObject::connect(m_ui.scalingFunctionComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
                     &MultiscaleLinearProcess::onScalingFunctionChanged);

    INIT_D_SPINBOX_CTRL(m_AmountControl, m_ui.amountSpinBox, m_ui.amountSlider,
                        &MultiscaleLinearProcess::onAmountChanged);

    // Layer tree connections
    QObject::connect(m_ui.layerTreeWidget, &QTreeWidget::itemSelectionChanged, this,
                     &MultiscaleLinearProcess::onLayerSelectionChanged);
    QObject::connect(m_ui.layerTreeWidget, &QTreeWidget::itemChanged, this,
                     &MultiscaleLinearProcess::onLayerItemChanged);

    // Layer action buttons
    QObject::connect(m_ui.enableAllLayersButton, &QPushButton::clicked, this,
                     &MultiscaleLinearProcess::onEnableAllLayers);
    QObject::connect(m_ui.disableAllLayersButton, &QPushButton::clicked, this,
                     &MultiscaleLinearProcess::onDisableAllLayers);
    QObject::connect(m_ui.resetBiasesButton, &QPushButton::clicked, this, &MultiscaleLinearProcess::onResetAllBiases);
    QObject::connect(m_ui.copyToAllButton, &QPushButton::clicked, this, &MultiscaleLinearProcess::onCopyLayerToAll);
    QObject::connect(m_ui.resetCurrentLayerButton, &QPushButton::clicked, this,
                     &MultiscaleLinearProcess::onResetCurrentLayer);

    // Layer inspector connections
    QObject::connect(m_ui.layerEnabledCheckBox, &QCheckBox::checkStateChanged, this,
                     &MultiscaleLinearProcess::onLayerEnabledChanged);
    INIT_D_SPINBOX_CTRL(m_BiasControl, m_ui.biasSpinBox, m_ui.biasSlider, &MultiscaleLinearProcess::onLayerBiasChanged);

    QObject::connect(m_ui.layerNrGroup, &QGroupBox::toggled, this, &MultiscaleLinearProcess::onLayerNrEnabledChanged);
    INIT_D_SPINBOX_CTRL(m_NrThresholdControl, m_ui.nrThresholdSpinBox, m_ui.nrThresholdSlider,
                        &MultiscaleLinearProcess::onLayerNrThresholdChanged);
    INIT_D_SPINBOX_CTRL(m_NrAmountControl, m_ui.nrAmountSpinBox, m_ui.nrAmountSlider,
                        &MultiscaleLinearProcess::onLayerNrAmountChanged);
    QObject::connect(m_ui.nrIterationsSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this,
                     &MultiscaleLinearProcess::onLayerNrIterationsChanged);

    // Global k-sigma connections
    QObject::connect(m_ui.ksigmaGroup, &QGroupBox::toggled, this, &MultiscaleLinearProcess::onKsigmaEnabledChanged);
    INIT_D_SPINBOX_CTRL(m_KsigmaKControl, m_ui.ksigmaKSpinBox, m_ui.ksigmaKSlider,
                        &MultiscaleLinearProcess::onKsigmaKChanged);
    INIT_D_SPINBOX_CTRL(m_KsigmaAmountControl, m_ui.ksigmaAmountSpinBox, m_ui.ksigmaAmountSlider,
                        &MultiscaleLinearProcess::onKsigmaAmountChanged);
    QObject::connect(m_ui.ksigmaSoftCheckBox, &QCheckBox::checkStateChanged, this,
                     &MultiscaleLinearProcess::onKsigmaSoftChanged);
    QObject::connect(m_ui.ksigmaUseMrsCheckBox, &QCheckBox::checkStateChanged, this,
                     &MultiscaleLinearProcess::onKsigmaUseMrsChanged);

    // Linear mask connections
    QObject::connect(m_ui.linearMaskGroup, &QGroupBox::toggled, this,
                     &MultiscaleLinearProcess::onLinearMaskEnabledChanged);
    QObject::connect(m_ui.linearMaskPreviewCheckBox, &QCheckBox::checkStateChanged, this,
                     &MultiscaleLinearProcess::onLinearMaskPreviewChanged);
    INIT_SPINBOX_CTRL(m_LinearMaskAmplificationControl, m_ui.linearMaskAmplificationSpinBox,
                      m_ui.linearMaskAmplificationSlider, &MultiscaleLinearProcess::onLinearMaskAmplificationChanged);
    INIT_D_SPINBOX_CTRL(m_LinearMaskSmoothnessControl, m_ui.linearMaskSmoothnessSpinBox,
                        m_ui.linearMaskSmoothnessSlider, &MultiscaleLinearProcess::onLinearMaskSmoothnessChanged);
    QObject::connect(m_ui.linearMaskInvertCheckBox, &QCheckBox::checkStateChanged, this,
                     &MultiscaleLinearProcess::onLinearMaskInvertedChanged);

    // Deringing connections
    QObject::connect(m_ui.deringingGroup, &QGroupBox::toggled, this,
                     &MultiscaleLinearProcess::onDeringingEnabledChanged);
    INIT_D_SPINBOX_CTRL(m_DeringingDarkControl, m_ui.deringingDarkSpinBox, m_ui.deringingDarkSlider,
                        &MultiscaleLinearProcess::onDeringingDarkChanged);
    INIT_D_SPINBOX_CTRL(m_DeringingBrightControl, m_ui.deringingBrightSpinBox, m_ui.deringingBrightSlider,
                        &MultiscaleLinearProcess::onDeringingBrightChanged);
    QObject::connect(m_ui.deringingOutputMapsCheckBox, &QCheckBox::checkStateChanged, this,
                     &MultiscaleLinearProcess::onDeringingOutputMapsChanged);

    // LSTF connections
    QObject::connect(m_ui.lstfModeComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
                     &MultiscaleLinearProcess::onLstfModeChanged);
    INIT_D_SPINBOX_CTRL(m_LstfBreakpointControl, m_ui.lstfBreakpointSpinBox, m_ui.lstfBreakpointSlider,
                        &MultiscaleLinearProcess::onLstfBreakpointChanged);

    // DRE connections
    INIT_D_SPINBOX_CTRL(m_DreLowControl, m_ui.dreLowSpinBox, m_ui.dreLowSlider,
                        &MultiscaleLinearProcess::onDreLowChanged);
    INIT_D_SPINBOX_CTRL(m_DreHighControl, m_ui.dreHighSpinBox, m_ui.dreHighSlider,
                        &MultiscaleLinearProcess::onDreHighChanged);

    // Preview mode connections
    QObject::connect(m_ui.previewModeComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
                     &MultiscaleLinearProcess::onPreviewModeChanged);

    // Setup layouts
    // Hide the placeholder widget - the preview control will be added to verticalLayout_2
    m_ui.widget->hide();

    // Set stretch factors to ensure preview area expands
    m_ui.rightPanelLayout->setStretch(0, 1);
    m_ui.rightPanelLayout->setStretch(1, 0);
    m_ui.rightPanelLayout->setStretch(2, 0);
    m_ui.rightPanelLayout->setStretch(3, 0);
    m_ui.rightPanelLayout->setStretch(4, 0);

    initialize();
    updateWindowTitle();
    setProcessDescription(QStringLiteral(
        "Apply multiscale linear or starlet transforms with per-layer denoise, deringing, and mask controls."));
    settingsUpdated();

    setExecutionAutostretchSupported(true);
    setupPreviewControl(true, QSize(1200, 1200), "Preview: Multiscale Linear Transform");
}

std::string MultiscaleLinearProcess::getProcessNameStatic()
{
    return "Multiscale Linear Transform";
}

bool MultiscaleLinearProcess::requiresExclusiveAccessStatic()
{
    return false;
}

void MultiscaleLinearProcess::onImageUpdated(ImageViewControl* ctrl)
{
    ImageRTPreviewControl::onImageUpdated(ctrl);

    if (ctrl == m_image_view_control && isPreviewEnabled())
    {
        schedulePreviewProcessExecution();
    }
}

void MultiscaleLinearProcess::onViewControlChanged(ImageViewControl* oldCtrl, ImageViewControl* newCtrl)
{
    ImageRTPreviewControl::onViewControlChanged(oldCtrl, newCtrl);

    if (newCtrl == m_image_view_control && isPreviewEnabled())
    {
        schedulePreviewProcessExecution();
    }
}

MultiscaleLinearProcess::~MultiscaleLinearProcess() {}

// Transform settings handlers
void MultiscaleLinearProcess::onSpacingChanged(int index)
{
    updateLinearScaleVisibility();
    settingsUpdated();
}

void MultiscaleLinearProcess::onLinearScaleChanged(int value)
{
    settingsUpdated();
}

void MultiscaleLinearProcess::onNumLayersChanged(int value)
{
    // Resize layer params and rebuild tree
    int totalLayers = value + 1; // detail layers + residual

    // Preserve existing settings where possible
    if (static_cast<int>(m_LayerParams.size()) < totalLayers)
    {
        // Add new layers with default settings
        for (int i = static_cast<int>(m_LayerParams.size()); i < totalLayers; ++i)
        {
            MultiscaleLayerParams newLayer;
            newLayer.index = i;
            newLayer.isResidual = (i == value); // last one is residual
            m_LayerParams.push_back(newLayer);
        }
    }
    else if (static_cast<int>(m_LayerParams.size()) > totalLayers)
    {
        m_LayerParams.resize(totalLayers);
    }

    // Update residual flag
    for (int i = 0; i < totalLayers; ++i)
    {
        m_LayerParams[i].index = i;
        m_LayerParams[i].isResidual = (i == value);
    }

    rebuildLayerTree();

    // Select first layer if none selected
    if (m_SelectedLayerIndex < 0 || m_SelectedLayerIndex >= totalLayers)
    {
        if (m_ui.layerTreeWidget->topLevelItemCount() > 0)
        {
            m_ui.layerTreeWidget->setCurrentItem(m_ui.layerTreeWidget->topLevelItem(0));
        }
    }

    settingsUpdated();
}

void MultiscaleLinearProcess::onAlgorithmChanged(int index)
{
    updateAlgorithmDependentControls();
    settingsUpdated();
}

void MultiscaleLinearProcess::onScalingFunctionChanged(int index)
{
    settingsUpdated();
}

void MultiscaleLinearProcess::onAmountChanged(double value)
{
    settingsUpdated();
}

// Layer tree handlers
void MultiscaleLinearProcess::onLayerSelectionChanged()
{
    auto selectedItems = m_ui.layerTreeWidget->selectedItems();
    if (selectedItems.isEmpty())
    {
        m_SelectedLayerIndex = -1;
    }
    else
    {
        m_SelectedLayerIndex = m_ui.layerTreeWidget->indexOfTopLevelItem(selectedItems.first());
    }

    updateLayerInspector();

    // Trigger m_ui.preview update when layer selection changes (important for layer m_ui.preview modes)
    if (isPreviewEnabled())
    {
        settingsUpdated();
    }
}

void MultiscaleLinearProcess::onLayerItemChanged(QTreeWidgetItem* item, int column)
{
    if (m_UpdatingLayerTree)
        return;

    int layerIndex = m_ui.layerTreeWidget->indexOfTopLevelItem(item);
    if (layerIndex < 0 || layerIndex >= static_cast<int>(m_LayerParams.size()))
        return;

    // Column 0 is the enable checkbox
    if (column == 0)
    {
        bool enabled = (item->checkState(0) == Qt::Checked);
        m_LayerParams[layerIndex].enabled = enabled;

        // Update inspector if this is the selected layer
        if (layerIndex == m_SelectedLayerIndex)
        {
            m_UpdatingLayerInspector = true;
            m_ui.layerEnabledCheckBox->setChecked(enabled);
            m_UpdatingLayerInspector = false;
        }

        settingsUpdated();
    }
}

// Layer inspector handlers
void MultiscaleLinearProcess::onLayerEnabledChanged(Qt::CheckState state)
{
    if (m_UpdatingLayerInspector || m_SelectedLayerIndex < 0)
        return;

    bool enabled = (state == Qt::Checked);
    m_LayerParams[m_SelectedLayerIndex].enabled = enabled;

    // Update tree m_ui.widget
    m_UpdatingLayerTree = true;
    auto item = m_ui.layerTreeWidget->topLevelItem(m_SelectedLayerIndex);
    if (item)
    {
        item->setCheckState(0, enabled ? Qt::Checked : Qt::Unchecked);
    }
    m_UpdatingLayerTree = false;

    settingsUpdated();
}

void MultiscaleLinearProcess::onLayerBiasChanged(double value)
{
    if (m_UpdatingLayerInspector || m_SelectedLayerIndex < 0)
        return;

    m_LayerParams[m_SelectedLayerIndex].bias = static_cast<float>(value);

    // Update tree m_ui.widget bias display
    m_UpdatingLayerTree = true;
    auto item = m_ui.layerTreeWidget->topLevelItem(m_SelectedLayerIndex);
    if (item)
    {
        item->setText(2, QString::number(value, 'f', 2));
    }
    m_UpdatingLayerTree = false;

    settingsUpdated();
}

void MultiscaleLinearProcess::onLayerNrEnabledChanged(int state)
{
    if (m_UpdatingLayerInspector || m_SelectedLayerIndex < 0)
        return;

    // Residual layer doesn't support NR
    if (m_LayerParams[m_SelectedLayerIndex].isResidual)
        return;

    bool enabled = (state != 0); // QGroupBox uses int, not Qt::CheckState directly
    m_LayerParams[m_SelectedLayerIndex].nrEnabled = enabled;

    // Update tree m_ui.widget NR indicator
    m_UpdatingLayerTree = true;
    auto item = m_ui.layerTreeWidget->topLevelItem(m_SelectedLayerIndex);
    if (item)
    {
        item->setText(3, enabled ? "●" : "");
    }
    m_UpdatingLayerTree = false;

    settingsUpdated();
}

void MultiscaleLinearProcess::onLayerNrThresholdChanged(double value)
{
    if (m_UpdatingLayerInspector || m_SelectedLayerIndex < 0)
        return;

    m_LayerParams[m_SelectedLayerIndex].nrThreshold = static_cast<float>(value);
    settingsUpdated();
}

void MultiscaleLinearProcess::onLayerNrAmountChanged(double value)
{
    if (m_UpdatingLayerInspector || m_SelectedLayerIndex < 0)
        return;

    m_LayerParams[m_SelectedLayerIndex].nrAmount = static_cast<float>(value);
    settingsUpdated();
}

void MultiscaleLinearProcess::onLayerNrIterationsChanged(int value)
{
    if (m_UpdatingLayerInspector || m_SelectedLayerIndex < 0)
        return;

    m_LayerParams[m_SelectedLayerIndex].nrIterations = value;
    settingsUpdated();
}

// Global k-sigma handlers
void MultiscaleLinearProcess::onKsigmaEnabledChanged(int state)
{
    updateGlobalSettingsEnableState();
    settingsUpdated();
}

void MultiscaleLinearProcess::onKsigmaKChanged(double value)
{
    settingsUpdated();
}

void MultiscaleLinearProcess::onKsigmaAmountChanged(double value)
{
    settingsUpdated();
}

void MultiscaleLinearProcess::onKsigmaSoftChanged(Qt::CheckState state)
{
    settingsUpdated();
}

void MultiscaleLinearProcess::onKsigmaUseMrsChanged(Qt::CheckState state)
{
    settingsUpdated();
}

// Linear mask handlers
void MultiscaleLinearProcess::onLinearMaskEnabledChanged(int state)
{
    updateGlobalSettingsEnableState();
    settingsUpdated();
}

void MultiscaleLinearProcess::onLinearMaskPreviewChanged(Qt::CheckState state)
{
    settingsUpdated();
}

void MultiscaleLinearProcess::onLinearMaskAmplificationChanged(int value)
{
    settingsUpdated();
}

void MultiscaleLinearProcess::onLinearMaskSmoothnessChanged(double value)
{
    settingsUpdated();
}

void MultiscaleLinearProcess::onLinearMaskInvertedChanged(Qt::CheckState state)
{
    settingsUpdated();
}

// Deringing handlers
void MultiscaleLinearProcess::onDeringingEnabledChanged(int state)
{
    updateGlobalSettingsEnableState();
    settingsUpdated();
}

void MultiscaleLinearProcess::onDeringingDarkChanged(double value)
{
    settingsUpdated();
}

void MultiscaleLinearProcess::onDeringingBrightChanged(double value)
{
    settingsUpdated();
}

void MultiscaleLinearProcess::onDeringingOutputMapsChanged(Qt::CheckState state)
{
    settingsUpdated();
}

// LSTF handlers
void MultiscaleLinearProcess::onLstfModeChanged(int index)
{
    updateLstfBreakpointVisibility();
    settingsUpdated();
}

void MultiscaleLinearProcess::onLstfBreakpointChanged(double value)
{
    settingsUpdated();
}

// DRE handlers
void MultiscaleLinearProcess::onDreLowChanged(double value)
{
    settingsUpdated();
}

void MultiscaleLinearProcess::onDreHighChanged(double value)
{
    settingsUpdated();
}

// Preview mode handlers
void MultiscaleLinearProcess::onPreviewModeChanged(int index)
{
    settingsUpdated();
}

void MultiscaleLinearProcess::updatePreviewModeComboBoxEnabled()
{
    m_ui.previewModeComboBox->setEnabled(m_ui.preview->isChecked());
}

// Layer actions
void MultiscaleLinearProcess::onEnableAllLayers()
{
    m_UpdatingLayerTree = true;
    for (size_t i = 0; i < m_LayerParams.size(); ++i)
    {
        m_LayerParams[i].enabled = true;
        auto item = m_ui.layerTreeWidget->topLevelItem(static_cast<int>(i));
        if (item)
        {
            item->setCheckState(0, Qt::Checked);
        }
    }
    m_UpdatingLayerTree = false;

    updateLayerInspector();
    settingsUpdated();
}

void MultiscaleLinearProcess::onDisableAllLayers()
{
    m_UpdatingLayerTree = true;
    for (size_t i = 0; i < m_LayerParams.size(); ++i)
    {
        m_LayerParams[i].enabled = false;
        auto item = m_ui.layerTreeWidget->topLevelItem(static_cast<int>(i));
        if (item)
        {
            item->setCheckState(0, Qt::Unchecked);
        }
    }
    m_UpdatingLayerTree = false;

    updateLayerInspector();
    settingsUpdated();
}

void MultiscaleLinearProcess::onResetAllBiases()
{
    m_UpdatingLayerTree = true;
    for (size_t i = 0; i < m_LayerParams.size(); ++i)
    {
        m_LayerParams[i].bias = 0.0f;
        auto item = m_ui.layerTreeWidget->topLevelItem(static_cast<int>(i));
        if (item)
        {
            item->setText(2, "0.00");
        }
    }
    m_UpdatingLayerTree = false;

    updateLayerInspector();
    settingsUpdated();
}

void MultiscaleLinearProcess::onCopyLayerToAll()
{
    if (m_SelectedLayerIndex < 0 || m_SelectedLayerIndex >= static_cast<int>(m_LayerParams.size()))
        return;

    const auto& sourceLayer = m_LayerParams[m_SelectedLayerIndex];

    m_UpdatingLayerTree = true;
    for (size_t i = 0; i < m_LayerParams.size(); ++i)
    {
        if (static_cast<int>(i) == m_SelectedLayerIndex)
            continue;

        m_LayerParams[i].enabled = sourceLayer.enabled;
        m_LayerParams[i].bias = sourceLayer.bias;

        // Only copy NR settings to non-residual layers
        if (!m_LayerParams[i].isResidual && !sourceLayer.isResidual)
        {
            m_LayerParams[i].nrEnabled = sourceLayer.nrEnabled;
            m_LayerParams[i].nrThreshold = sourceLayer.nrThreshold;
            m_LayerParams[i].nrAmount = sourceLayer.nrAmount;
            m_LayerParams[i].nrIterations = sourceLayer.nrIterations;
        }

        // Update tree m_ui.widget
        auto item = m_ui.layerTreeWidget->topLevelItem(static_cast<int>(i));
        if (item)
        {
            item->setCheckState(0, m_LayerParams[i].enabled ? Qt::Checked : Qt::Unchecked);
            item->setText(2, QString::number(m_LayerParams[i].bias, 'f', 2));
            if (!m_LayerParams[i].isResidual)
            {
                item->setText(3, m_LayerParams[i].nrEnabled ? "●" : "");
            }
        }
    }
    m_UpdatingLayerTree = false;

    settingsUpdated();
}

void MultiscaleLinearProcess::onResetCurrentLayer()
{
    if (m_SelectedLayerIndex < 0 || m_SelectedLayerIndex >= static_cast<int>(m_LayerParams.size()))
        return;

    // Reset layer parameters to defaults
    m_LayerParams[m_SelectedLayerIndex].enabled = true;
    m_LayerParams[m_SelectedLayerIndex].bias = 0.0f;
    m_LayerParams[m_SelectedLayerIndex].nrEnabled = false;
    m_LayerParams[m_SelectedLayerIndex].nrThreshold = 3.0f;
    m_LayerParams[m_SelectedLayerIndex].nrAmount = 0.5f;
    m_LayerParams[m_SelectedLayerIndex].nrIterations = 1;

    // Update tree m_ui.widget
    auto item = m_ui.layerTreeWidget->topLevelItem(m_SelectedLayerIndex);
    if (item)
    {
        item->setCheckState(0, Qt::Checked);
        item->setText(2, "0.00");
        if (!m_LayerParams[m_SelectedLayerIndex].isResidual)
        {
            item->setText(3, "");
        }
    }

    updateLayerInspector();
    settingsUpdated();
}

bool MultiscaleLinearProcess::isPreviewEnabled() const
{
    return m_ui.preview->isChecked();
}

void MultiscaleLinearProcess::enablePreview(bool enabled)
{
    m_ui.preview->setChecked(enabled);
}

an::Status MultiscaleLinearProcess::executeTypedProcessIteration(const an::ChannelScopeInput& input,
                                                                 const an::ChannelScopeOutput& output,
                                                                 const an::ui::ProcessRun& run,
                                                                 const MultiscaleParams& params,
                                                                 an::ui::ExecutionScope& scope)
{
    const process_utils::multiscale::TransformRequest request = detail::transformRequestOf(params);
    an::Result<process_utils::NormalizedSamples> samples = process_utils::NormalizedSamples::open(
        scope.engine(), scope.execution(), *output.descriptor, output.samples, *output.layout, input.validity);
    if (!samples.hasValue())
    {
        return samples.status();
    }

    // A preview can show the linear mask, or one layer's coefficients, in place of the processed image.
    an::Status ran = an::Status::success();
    if (run.isForPreview() && params.linearMaskEnabled && params.linearMaskPreview)
    {
        ran = process_utils::multiscale::previewLinearMaskInPlace(scope, samples->sampleType(), samples->samples(),
                                                                  samples->layout(), request);
    }
    else if (run.isForPreview() && params.previewMode != MultiscalePreviewMode::ProcessedImage &&
             params.selectedLayerIndex >= 0)
    {
        const auto layer = static_cast<std::size_t>(params.selectedLayerIndex);
        const double bias = layer < params.layers.size() ? static_cast<double>(params.layers[layer].bias) : 0.0;
        ran = process_utils::multiscale::previewLayerInPlace(
            scope, samples->sampleType(), samples->samples(), samples->layout(), request,
            static_cast<std::uint32_t>(layer), layerPreviewOf(params.previewMode), bias);
    }
    else if (!run.isForPreview() && !run.isForSequence() && params.deringingEnabled && params.deringingOutputMaps)
    {
        // An Apply that keeps the deringing's maps writes them into images of their own while it derings.
        const an::Result<an::ImageDescriptor> mapDescribed =
            an::describeSamplesAs(*output.descriptor, an::SampleType::Float32);
        if (!mapDescribed.hasValue())
        {
            return mapDescribed.status();
        }
        std::array<std::optional<an::consumer::ImageAsset>, 2u> mapAssets;
        constexpr std::array<const char*, 2u> kMapOperations{"multiscale-deringing-dark-map",
                                                             "multiscale-deringing-bright-map"};
        for (std::size_t map = 0u; map < mapAssets.size(); ++map)
        {
            an::Result<an::ImageMetadata> metadata = process_utils::derivedImageMetadata(
                params.sourceMetadata ? *params.sourceMetadata : an::ImageMetadata{}, *mapDescribed,
                kMapOperations[map]);
            if (!metadata.hasValue())
            {
                return metadata.status();
            }
            an::Result<an::consumer::ImageAsset> made =
                process_utils::createDerivedImage(scope, *mapDescribed, std::move(*metadata));
            if (!made.hasValue())
            {
                return made.status();
            }
            mapAssets[map].emplace(std::move(*made));
        }
        {
            an::Result<an::consumer::AssetWriteLease> darkLease = mapAssets[0]->write();
            an::Result<an::consumer::AssetWriteLease> brightLease = mapAssets[1]->write();
            if (!darkLease.hasValue() || !brightLease.hasValue())
            {
                return !darkLease.hasValue() ? darkLease.status() : brightLease.status();
            }
            const std::optional<an::ImageWriteAccess<float>> dark = darkLease->write<float>();
            const std::optional<an::ImageWriteAccess<float>> bright = brightLease->write<float>();
            if (!dark.has_value() || !bright.has_value())
            {
                return an::Status::failure(an::ErrorCode::NotInitialized, "the deringing maps cannot be written");
            }
            const an::ImageLayout mapLayout = dark->view().layout();
            const process_utils::multiscale::DeringingMaps maps{
                .dark = dark->view().data(), .bright = bright->view().data(), .layout = &mapLayout};
            ran = process_utils::multiscale::transformInPlace(scope, samples->sampleType(), samples->samples(),
                                                              samples->layout(), samples->validity(), request, &maps);
        }
        if (ran.ok())
        {
            const std::string suffix =
                params.colourTarget && params.channelLabel != "RGB" ? fmt::format(" ({})", params.channelLabel) : "";
            m_StagedOutputs.stage(process_utils::StagedOutput{
                .asset = std::move(*mapAssets[0]),
                .name = QString::fromStdString(fmt::format("{} - Deringing Dark Map{}", params.targetName, suffix)),
                .history = fmt::format("{} - Deringing Dark Map", getProcessNameStatic())});
            m_StagedOutputs.stage(process_utils::StagedOutput{
                .asset = std::move(*mapAssets[1]),
                .name = QString::fromStdString(fmt::format("{} - Deringing Bright Map{}", params.targetName, suffix)),
                .history = fmt::format("{} - Deringing Bright Map", getProcessNameStatic())});
        }
    }
    else
    {
        ran = process_utils::multiscale::transformInPlace(scope, samples->sampleType(), samples->samples(),
                                                          samples->layout(), samples->validity(), request, nullptr);
    }
    if (ran.failed())
    {
        return ran;
    }
    return samples->commit(scope.execution());
}

an::Status MultiscaleLinearProcess::executeTypedProcessImpl(const an::ui::ProcessSource& source,
                                                            an::ui::ProcessDestination& destination,
                                                            const an::ui::ProcessRun& run,
                                                            const MultiscaleParams& params,
                                                            an::ui::ExecutionScope& scope)
{
    // The deringing maps an Apply keeps derive their record from the image the run reads.
    MultiscaleParams withSource = params;
    withSource.sourceMetadata = std::make_shared<const an::ImageMetadata>(source.metadata());
    if (an::Status ran = executeTypedProcessUsingChannels(source, destination, run, withSource, scope); ran.failed())
    {
        return ran;
    }
    if (run.isForPreview())
    {
        return an::Status::success();
    }
    // The transform reweights an image of the same kind, so its record keeps the source's and its lineage names it.
    an::Result<an::ProvenanceEntry> entry =
        an::ProvenanceEntry::create("an::reconstructImageMultiscale", detail::describeTransform(params));
    if (!entry.hasValue())
    {
        return entry.status();
    }
    return destination.asset().transition(an::MetadataTransition{.record = *entry});
}

an::ui::ProcessStep MultiscaleLinearProcess::describeTypedProcessStep(const MultiscaleParams& params) const
{
    return an::ui::ProcessStep{.name = getProcessName(), .description = detail::describeTransform(params)};
}

void MultiscaleLinearProcess::executeProcessStarted(const RTPreviewMode previewMode, const bool sequenceMode)
{
    if (previewMode == RTPreviewMode::NoPreview && !sequenceMode)
    {
        m_StagedOutputs.clear();
    }
}

void MultiscaleLinearProcess::executeProcessCompletedImpl(const RTPreviewMode previewMode, const bool sequenceMode)
{
    if (previewMode != RTPreviewMode::NoPreview || sequenceMode)
    {
        return;
    }
    process_utils::openStagedOutputs(m_StagedOutputs.take(), m_image_view_control);
}

void MultiscaleLinearProcess::settingsUpdatedImpl()
{
    m_ui.buttonBox->button(QDialogButtonBox::Apply)->setEnabled(true);
}

void MultiscaleLinearProcess::enableProcessControlsImpl(bool enabled)
{
    m_ui.buttonBox->button(QDialogButtonBox::Apply)->setEnabled(enabled);
    m_ui.buttonBox->button(QDialogButtonBox::Close)->setEnabled(enabled);
    m_ui.buttonBox->button(QDialogButtonBox::Reset)->setEnabled(enabled);

    // Enable/disable all controls
    m_ui.transformSettingsGroup->setEnabled(enabled);
    m_ui.layerTreeGroup->setEnabled(enabled);
    m_ui.globalSettingsGroup->setEnabled(enabled);
    m_ui.layerInspectorGroup->setEnabled(enabled);
}

void MultiscaleLinearProcess::previewControlStarted()
{
    // No special initialization needed for the new API
}

void MultiscaleLinearProcess::previewControlClosed()
{
    // No special cleanup needed for the new API
}

void MultiscaleLinearProcess::resetSettings()
{
    initialize();

    if (m_ui.preview->isChecked())
    {
        schedulePreviewProcessExecution();
    }
}

void MultiscaleLinearProcess::on_channelSelector_currentTextChangedInternal(const QString& text)
{
    if (isPreviewEnabled())
    {
        schedulePreviewProcessExecution();
    }
}

void MultiscaleLinearProcess::initialize()
{
    // Initialize algorithm combo box
    m_ui.algorithmComboBox->clear();
    m_ui.algorithmComboBox->addItem(algorithmToString(an::MultiscaleAlgorithm::Starlet));
    m_ui.algorithmComboBox->addItem(algorithmToString(an::MultiscaleAlgorithm::MultiscaleLinear));
    m_ui.algorithmComboBox->setCurrentIndex(1);

    // Initialize spacing combo box
    m_ui.spacingComboBox->clear();
    m_ui.spacingComboBox->addItem("Dyadic");
    m_ui.spacingComboBox->addItem("Linear");
    m_ui.spacingComboBox->setCurrentIndex(0);

    // Initialize scaling function combo box
    initializeScalingFunctions();

    // Initialize m_ui.preview mode combo box
    m_ui.previewModeComboBox->clear();
    m_ui.previewModeComboBox->addItem("Processed Image");
    m_ui.previewModeComboBox->addItem("Layer (All Changes)");
    m_ui.previewModeComboBox->addItem("Layer (Increasing Pixels)");
    m_ui.previewModeComboBox->addItem("Layer (Decreasing Pixels)");
    m_ui.previewModeComboBox->setCurrentIndex(0);
    updatePreviewModeComboBoxEnabled();

    // Initialize LSTF combo box
    m_ui.lstfModeComboBox->clear();
    m_ui.lstfModeComboBox->addItem(lstfModeToString(ResidualTransfer::None));
    m_ui.lstfModeComboBox->addItem(lstfModeToString(ResidualTransfer::Hyperbolic));
    m_ui.lstfModeComboBox->addItem(lstfModeToString(ResidualTransfer::Log10));
    m_ui.lstfModeComboBox->addItem(lstfModeToString(ResidualTransfer::NaturalLog));
    m_ui.lstfModeComboBox->setCurrentIndex(0);

    // Set default values
    m_ui.numLayersSpinBox->setValue(kDefaultDetailLayers);
    m_ui.linearScaleSpinBox->setValue(1);
    m_AmountControl->setValue(1.0);

    // Initialize layer params
    int totalLayers = kDefaultDetailLayers + 1;
    m_LayerParams.clear();
    m_LayerParams.reserve(totalLayers);
    for (int i = 0; i < totalLayers; ++i)
    {
        MultiscaleLayerParams layer;
        layer.index = i;
        layer.isResidual = (i == kDefaultDetailLayers);
        layer.enabled = true;
        layer.bias = 0.0f;
        layer.nrEnabled = false;
        layer.nrThreshold = 3.0f;
        layer.nrAmount = 1.0f;
        layer.nrIterations = 1;
        m_LayerParams.push_back(layer);
    }

    // Reset global settings
    m_ui.ksigmaGroup->setChecked(false);
    m_KsigmaKControl->setValue(3.0);
    m_KsigmaAmountControl->setValue(1.0);
    m_ui.ksigmaSoftCheckBox->setChecked(true);
    m_ui.ksigmaUseMrsCheckBox->setChecked(false);

    m_ui.linearMaskGroup->setChecked(false);
    m_ui.linearMaskPreviewCheckBox->setChecked(false);
    m_LinearMaskAmplificationControl->setValue(100);
    m_LinearMaskSmoothnessControl->setValue(1.0);
    m_ui.linearMaskInvertCheckBox->setChecked(true);

    m_ui.deringingGroup->setChecked(false);
    m_DeringingDarkControl->setValue(kDefaultDeringingDark);
    m_DeringingBrightControl->setValue(kDefaultDeringingBright);
    m_ui.deringingOutputMapsCheckBox->setChecked(false);

    m_LstfBreakpointControl->setValue(0.75);

    m_DreLowControl->setValue(0.0);
    m_DreHighControl->setValue(0.0);

    // Build layer tree
    rebuildLayerTree();

    // Select first layer
    if (m_ui.layerTreeWidget->topLevelItemCount() > 0)
    {
        m_ui.layerTreeWidget->setCurrentItem(m_ui.layerTreeWidget->topLevelItem(0));
    }

    // Update visibility states
    updateAlgorithmDependentControls();
    updateLinearScaleVisibility();
    updateLstfBreakpointVisibility();
    updateGlobalSettingsEnableState();
}

void MultiscaleLinearProcess::initializeScalingFunctions()
{
    m_ui.scalingFunctionComboBox->clear();

    // In the order scalingFunctionFromIndex reads them.
    for (const an::MultiscaleScalingFunction function : kScalingFunctions)
    {
        m_ui.scalingFunctionComboBox->addItem(scalingFunctionToString(function));
    }

    // Default to B3 Spline
    m_ui.scalingFunctionComboBox->setCurrentIndex(1);
}

void MultiscaleLinearProcess::rebuildLayerTree()
{
    m_UpdatingLayerTree = true;

    m_ui.layerTreeWidget->clear();

    int numDetailLayers = m_ui.numLayersSpinBox->value();

    for (size_t i = 0; i < m_LayerParams.size(); ++i)
    {
        const auto& layer = m_LayerParams[i];

        auto item = new QTreeWidgetItem();

        // Column 0: Enable checkbox
        item->setCheckState(0, layer.enabled ? Qt::Checked : Qt::Unchecked);

        // Column 1: Layer name
        if (layer.isResidual)
        {
            item->setText(1, "Residual");
        }
        else
        {
            item->setText(1, QString("Layer %1").arg(i + 1));
        }

        // Column 2: Bias value
        item->setText(2, QString::number(layer.bias, 'f', 2));

        // Column 3: NR indicator (only for detail layers)
        if (!layer.isResidual)
        {
            item->setText(3, layer.nrEnabled ? "●" : "");
        }
        else
        {
            item->setText(3, "—"); // Residual doesn't support NR
        }

        m_ui.layerTreeWidget->addTopLevelItem(item);
    }

    // Adjust column widths
    m_ui.layerTreeWidget->setColumnWidth(0, 30);
    m_ui.layerTreeWidget->setColumnWidth(1, 80);
    m_ui.layerTreeWidget->setColumnWidth(2, 60);
    m_ui.layerTreeWidget->setColumnWidth(3, 30);

    m_UpdatingLayerTree = false;
}

void MultiscaleLinearProcess::updateLayerInspector()
{
    m_UpdatingLayerInspector = true;

    bool hasSelection = (m_SelectedLayerIndex >= 0 && m_SelectedLayerIndex < static_cast<int>(m_LayerParams.size()));

    m_ui.layerInspectorGroup->setEnabled(hasSelection);

    if (hasSelection)
    {
        const auto& layer = m_LayerParams[m_SelectedLayerIndex];

        // Update title
        if (layer.isResidual)
        {
            m_ui.layerNameLabel->setText("Residual Layer");
        }
        else
        {
            m_ui.layerNameLabel->setText(QString("Layer %1").arg(m_SelectedLayerIndex + 1));
        }

        // Update controls
        m_ui.layerEnabledCheckBox->setChecked(layer.enabled);
        m_BiasControl->setValue(static_cast<double>(layer.bias));

        // NR section - disabled for residual
        m_ui.layerNrGroup->setEnabled(!layer.isResidual);
        if (!layer.isResidual)
        {
            m_ui.layerNrGroup->setChecked(layer.nrEnabled);
            m_NrThresholdControl->setValue(static_cast<double>(layer.nrThreshold));
            m_NrAmountControl->setValue(static_cast<double>(layer.nrAmount));
            m_ui.nrIterationsSpinBox->setValue(layer.nrIterations);
        }
        else
        {
            m_ui.layerNrGroup->setChecked(false);
        }
    }
    else
    {
        m_ui.layerNameLabel->setText("No layer selected");
    }

    m_UpdatingLayerInspector = false;
}

void MultiscaleLinearProcess::updateAlgorithmDependentControls()
{
    const bool isStarlet = (m_ui.algorithmComboBox->currentIndex() == 0);
    m_ui.labelScaling->setEnabled(isStarlet);
    m_ui.scalingFunctionComboBox->setEnabled(isStarlet);
}

void MultiscaleLinearProcess::updateLinearScaleVisibility()
{
    bool isLinear = (m_ui.spacingComboBox->currentIndex() == 1);
    m_ui.labelLinearScale->setVisible(isLinear);
    m_ui.linearScaleSpinBox->setVisible(isLinear);
}

void MultiscaleLinearProcess::updateLstfBreakpointVisibility()
{
    bool showBreakpoint = (m_ui.lstfModeComboBox->currentIndex() == 1); // Hyperbolic
    m_ui.labelLstfBreakpoint->setVisible(showBreakpoint);
    m_ui.lstfBreakpointSlider->setVisible(showBreakpoint);
    m_ui.lstfBreakpointSpinBox->setVisible(showBreakpoint);
}

void MultiscaleLinearProcess::updateGlobalSettingsEnableState()
{
    // Enable/disable child controls based on group checkbox state
    // This is handled automatically by Qt for checkable group boxes
}

ProgressDialogControl* MultiscaleLinearProcess::getProgressDialogControl(const bool forSequence)
{
    ProgressDialogControl* progress = new ProgressDialogControl(
        this, QString::fromUtf8(fmt::format("{} <{}>", getProcessName(), getParentViewControl()->getUniqueName())),
        forSequence);

    progress->setProgressBarRange(0, 100);
    progress->setValue(0);

    return progress;
}

double MultiscaleLinearProcess::getTypedModulationFromParams(const MultiscaleParams& params)
{
    return params.amount;
}

MultiscaleParams MultiscaleLinearProcess::getParams() const
{
    MultiscaleParams params;

    // Transform settings
    params.algorithm = algorithmFromIndex(m_ui.algorithmComboBox->currentIndex());
    params.spacing = (m_ui.spacingComboBox->currentIndex() == 0) ? an::MultiscaleLayerSpacing::Dyadic
                                                                 : an::MultiscaleLayerSpacing::Linear;
    params.linearScale = m_ui.linearScaleSpinBox->value();
    params.numDetailLayers = m_ui.numLayersSpinBox->value();
    params.scalingFunction = detail::scalingFunctionFromIndex(m_ui.scalingFunctionComboBox->currentIndex());
    params.amount = m_AmountControl->getValue();

    // Copy layer params
    params.layers = m_LayerParams;

    // k-Sigma settings
    params.ksigmaEnabled = m_ui.ksigmaGroup->isChecked();
    params.ksigmaK = static_cast<float>(m_KsigmaKControl->getValue());
    params.ksigmaAmount = static_cast<float>(m_KsigmaAmountControl->getValue());
    params.ksigmaSoft = m_ui.ksigmaSoftCheckBox->isChecked();
    params.ksigmaUseMrs = m_ui.ksigmaUseMrsCheckBox->isChecked();

    // Linear mask settings
    params.linearMaskEnabled = m_ui.linearMaskGroup->isChecked();
    params.linearMaskPreview = m_ui.linearMaskPreviewCheckBox->isChecked();
    params.linearMaskAmplification = m_LinearMaskAmplificationControl->getValue();
    params.linearMaskSmoothness = static_cast<float>(m_LinearMaskSmoothnessControl->getValue());
    params.linearMaskInverted = m_ui.linearMaskInvertCheckBox->isChecked();

    // Deringing settings
    params.deringingEnabled = m_ui.deringingGroup->isChecked();
    params.deringingDark = static_cast<float>(m_DeringingDarkControl->getValue());
    params.deringingBright = static_cast<float>(m_DeringingBrightControl->getValue());
    params.deringingOutputMaps = m_ui.deringingOutputMapsCheckBox->isChecked();

    // LSTF settings
    params.lstfMode = lstfModeFromIndex(m_ui.lstfModeComboBox->currentIndex());
    params.lstfBreakpoint = static_cast<float>(m_LstfBreakpointControl->getValue());

    // DRE settings
    params.dreLow = static_cast<float>(m_DreLowControl->getValue());
    params.dreHigh = static_cast<float>(m_DreHighControl->getValue());

    // Preview mode settings
    params.previewMode = static_cast<MultiscalePreviewMode>(m_ui.previewModeComboBox->currentIndex());
    params.selectedLayerIndex = m_SelectedLayerIndex;

    params.targetName = m_image_view_control != nullptr ? m_image_view_control->getUniqueName() : std::string();
    params.colourTarget = process_utils::holdsColourImage(m_image_view_control);
    params.channelLabel = an::ui::processChannelLabel(selectedChannelScope()).toStdString();

    return params;
}
