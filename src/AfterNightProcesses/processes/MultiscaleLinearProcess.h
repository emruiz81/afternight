// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightProcessesExport.h"

#include "processes/MultiscaleLinearProcessUiBuilder.h"
#include "controls/TypedProcessControl.h"
#include "controls/QSliderSpinBox.h"
#include "processes/MaskProcessUtils.h"
#include "processes/MultiscaleComposition.h"

#include <AfterNightEngine/Algorithms/Multiscale/MultiscaleTransform.h>
#include <AfterNightEngine/Image/ImageMetadata.h>

#include <memory>
#include <string>
#include <vector>

/// Preview mode for multiscale transform visualization
enum class MultiscalePreviewMode
{
    ProcessedImage = 0, // Show the fully processed image (default behavior)
    LayerAllChanges, // Show all non-zero transform coefficients (proportional to absolute values)
    LayerIncreasingPixels, // Show only positive transform coefficients
    LayerDecreasingPixels // Show only negative transform coefficients (as absolute values)
};

// Forward declarations
class QTreeWidgetItem;

/// Parameters for a single layer (detail or residual)
struct MultiscaleLayerParams
{
    int index = 0; // Layer index (0 = first detail, N = residual)
    bool isResidual = false;
    bool enabled = true;
    float bias = 0.0f; // Bias where 0 = no change
    bool nrEnabled = false;
    float nrThreshold = 3.0f; // Sigma units
    float nrAmount = 0.5f; // 0..1
    int nrIterations = 1;
};

/// Complete parameter set for multiscale transform execution
struct MultiscaleParams
{
    // Transform settings
    an::MultiscaleAlgorithm algorithm = an::MultiscaleAlgorithm::MultiscaleLinear;
    an::MultiscaleLayerSpacing spacing = an::MultiscaleLayerSpacing::Dyadic;
    int linearScale = 1;
    int numDetailLayers = 6;
    an::MultiscaleScalingFunction scalingFunction = an::MultiscaleScalingFunction::BSpline5;
    double amount = 1.0;

    // Per-layer settings (numDetailLayers + 1 for residual)
    std::vector<MultiscaleLayerParams> layers;

    // Global k-sigma thresholding
    bool ksigmaEnabled = false;
    float ksigmaK = 3.0f;
    float ksigmaAmount = 0.5f;
    bool ksigmaSoft = true;
    bool ksigmaUseMrs = false;

    // Linear mask
    bool linearMaskEnabled = false;
    bool linearMaskPreview = false;
    int linearMaskAmplification = 100;
    float linearMaskSmoothness = 1.0f;
    bool linearMaskInverted = true;

    // Deringing
    bool deringingEnabled = false;
    float deringingDark = 0.1f;
    float deringingBright = 0.0f;
    bool deringingOutputMaps = false;

    // Large-Scale Transfer Function
    process_utils::multiscale::ResidualTransfer lstfMode = process_utils::multiscale::ResidualTransfer::None;
    float lstfBreakpoint = 0.5f;

    // Dynamic Range Extension
    float dreLow = 0.0f;
    float dreHigh = 0.0f;

    // Preview mode (only used during preview, not for final apply)
    MultiscalePreviewMode previewMode = MultiscalePreviewMode::ProcessedImage;
    int selectedLayerIndex = -1; // Layer to visualize in layer preview modes

    /// The target's name, which the deringing maps are named after, and the channel the selector names, read on the
    /// GUI thread.
    std::string targetName;
    std::string channelLabel;
    bool colourTarget = false;

    /// The metadata of the image the run reads, which the deringing maps derive their record from; stated by the run,
    /// not by the window.
    std::shared_ptr<const an::ImageMetadata> sourceMetadata;
};

namespace an::ui::processes::multiscale_detail
{

/// The kernel a scaling selector entry names, in the dialog's order; the B3 spline for an entry outside it.
[[nodiscard]] AFTERNIGHTPROCESSES_API an::MultiscaleScalingFunction scalingFunctionFromIndex(int index) noexcept;

/// What the dialog's values ask the composition for.
[[nodiscard]] AFTERNIGHTPROCESSES_API process_utils::multiscale::TransformRequest
transformRequestOf(const MultiscaleParams& params);

/// The history step's description of the dialog's values.
[[nodiscard]] AFTERNIGHTPROCESSES_API std::string describeTransform(const MultiscaleParams& params);

} // namespace an::ui::processes::multiscale_detail

/// Multiscale Linear Transform: decomposes the channel or colour component the selector names into the layers of the
/// multiscale linear or the starlet transform, reweights, denoises and derings them, compresses the residual and adds
/// them back, through the engine's transform and the presentation steps `processes/MultiscaleComposition.h` composes
/// around it. Its preview can show one layer or the linear mask instead, and an Apply can open the deringing's maps.
class AFTERNIGHTPROCESSES_API MultiscaleLinearProcess : public an::ui::TypedProcessControl<MultiscaleParams>
{
public:
    MultiscaleLinearProcess(ImageViewControl* control);

    static std::string getProcessNameStatic();
    static bool requiresExclusiveAccessStatic();

    void onImageUpdated(ImageViewControl* ctrl) override;
    void onViewControlChanged(ImageViewControl* oldCtrl, ImageViewControl* newCtrl) override;

    ~MultiscaleLinearProcess();

private:
    // Transform settings handlers
    void onSpacingChanged(int index);
    void onLinearScaleChanged(int value);
    void onNumLayersChanged(int value);
    void onAlgorithmChanged(int index);
    void onScalingFunctionChanged(int index);
    void onAmountChanged(double value);

    // Layer tree handlers
    void onLayerSelectionChanged();
    void onLayerItemChanged(QTreeWidgetItem* item, int column);

    // Layer inspector handlers
    void onLayerEnabledChanged(Qt::CheckState state);
    void onLayerBiasChanged(double value);
    void onLayerNrEnabledChanged(int state);
    void onLayerNrThresholdChanged(double value);
    void onLayerNrAmountChanged(double value);
    void onLayerNrIterationsChanged(int value);

    // Global k-sigma handlers
    void onKsigmaEnabledChanged(int state);
    void onKsigmaKChanged(double value);
    void onKsigmaAmountChanged(double value);
    void onKsigmaSoftChanged(Qt::CheckState state);
    void onKsigmaUseMrsChanged(Qt::CheckState state);

    // Linear mask handlers
    void onLinearMaskEnabledChanged(int state);
    void onLinearMaskPreviewChanged(Qt::CheckState state);
    void onLinearMaskAmplificationChanged(int value);
    void onLinearMaskSmoothnessChanged(double value);
    void onLinearMaskInvertedChanged(Qt::CheckState state);

    // Deringing handlers
    void onDeringingEnabledChanged(int state);
    void onDeringingDarkChanged(double value);
    void onDeringingBrightChanged(double value);
    void onDeringingOutputMapsChanged(Qt::CheckState state);

    // LSTF handlers
    void onLstfModeChanged(int index);
    void onLstfBreakpointChanged(double value);

    // DRE handlers
    void onDreLowChanged(double value);
    void onDreHighChanged(double value);

    // Preview mode handler
    void onPreviewModeChanged(int index);
    void updatePreviewModeComboBoxEnabled();

    // Layer actions
    void onEnableAllLayers();
    void onDisableAllLayers();
    void onResetAllBiases();
    void onCopyLayerToAll();
    void onResetCurrentLayer();

protected:
    an::Status executeTypedProcessIteration(const an::ChannelScopeInput& input, const an::ChannelScopeOutput& output,
                                            const an::ui::ProcessRun& run, const MultiscaleParams& params,
                                            an::ui::ExecutionScope& scope) override;
    an::Status executeTypedProcessImpl(const an::ui::ProcessSource& source, an::ui::ProcessDestination& destination,
                                       const an::ui::ProcessRun& run, const MultiscaleParams& params,
                                       an::ui::ExecutionScope& scope) override;
    [[nodiscard]] an::ui::ProcessStep describeTypedProcessStep(const MultiscaleParams& params) const override;

private:
    bool isPreviewEnabled() const override;
    void enablePreview(bool enabled) override;
    void executeProcessStarted(RTPreviewMode previewMode, bool sequenceMode) override;
    void executeProcessCompletedImpl(RTPreviewMode previewMode, bool sequenceMode) override;

    void settingsUpdatedImpl() override;
    void enableProcessControlsImpl(bool enabled) override;

    void previewControlStarted() override;
    void previewControlClosed() override;

    void resetSettings() override;

    void on_channelSelector_currentTextChangedInternal(const QString& text) override;

    void initialize();
    void initializeScalingFunctions();
    void rebuildLayerTree();
    void updateLayerInspector();
    void updateAlgorithmDependentControls();
    void updateLinearScaleVisibility();
    void updateLstfBreakpointVisibility();
    void updateGlobalSettingsEnableState();

    ProgressDialogControl* getProgressDialogControl(const bool forSequence) override;
    [[nodiscard]] MultiscaleParams getParams() const override;
    double getTypedModulationFromParams(const MultiscaleParams& params) override;

    // Slider-spinbox controls
    QSliderDoubleSpinBoxUPtr m_AmountControl;
    QSliderDoubleSpinBoxUPtr m_BiasControl;
    QSliderDoubleSpinBoxUPtr m_NrThresholdControl;
    QSliderDoubleSpinBoxUPtr m_NrAmountControl;
    QSliderDoubleSpinBoxUPtr m_KsigmaKControl;
    QSliderDoubleSpinBoxUPtr m_KsigmaAmountControl;
    QSliderSpinBoxUPtr m_LinearMaskAmplificationControl;
    QSliderDoubleSpinBoxUPtr m_LinearMaskSmoothnessControl;
    QSliderDoubleSpinBoxUPtr m_DeringingDarkControl;
    QSliderDoubleSpinBoxUPtr m_DeringingBrightControl;
    QSliderDoubleSpinBoxUPtr m_LstfBreakpointControl;
    QSliderDoubleSpinBoxUPtr m_DreLowControl;
    QSliderDoubleSpinBoxUPtr m_DreHighControl;

    // Per-layer parameters storage
    std::vector<MultiscaleLayerParams> m_LayerParams;

    // Currently selected layer index (-1 = none)
    int m_SelectedLayerIndex = -1;

    // Flag to prevent recursive updates
    bool m_UpdatingLayerInspector = false;
    bool m_UpdatingLayerTree = false;

    /// The deringing maps an Apply made, waiting to be opened once the result lands.
    process_utils::StagedOutputs m_StagedOutputs;

    an::ui::processes::MultiscaleLinearProcessUiElements m_ui;
};
