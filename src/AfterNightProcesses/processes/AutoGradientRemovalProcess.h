// Native AfterNight process based on Siril AutoGradientRemoval 1.0.0 by
// Cyril Richard, 2026 (GPL-3.0-or-later). Numerical provenance is recorded in
// docs/dev/AUTO_GRADIENT_REMOVAL_PROVENANCE.md.

// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightProcessesExport.h"

#include "controls/QSliderSpinBox.h"
#include "controls/TypedProcessControl.h"
#include "processes/AutoGradientRemovalProcessUiBuilder.h"
#include "processes/MaskProcessUtils.h"

#include <AfterNightEngine/Algorithms/Background/AutoBackground.h>

#include <QString>

#include <cstdint>
#include <mutex>
#include <string>

class ImageViewControl;

struct AutoGradientRemovalProcessParams
{
    enum class PreviewDisplayMode : uint8_t
    {
        corrected = 0,
        gradient_model,
        protection_mask
    };

    /// The model, the scale, the protection and the correction, in the unit interval every sample is read in.
    an::AutoBackgroundRequest algorithm;

    /// What the preview shows.
    PreviewDisplayMode previewDisplay = PreviewDisplayMode::corrected;

    /// Whether the preview computes at all: it does once its button asked for it and until a setting changes, and shows
    /// the target until then.
    bool previewComputed = false;

    bool outputGradientModel = false;
    bool outputProtectionMask = false;

    /// The target's name, which the outputs are named after, read on the GUI thread.
    std::string targetName;
};

struct AutoGradientRemovalDependentUiState
{
    bool protectionParametersEnabled = false;
    bool protectionMaskOutputEnabled = false;
    bool protectionMaskPreviewEnabled = false;
    bool polynomialDegreeEnabled = false;
};

[[nodiscard]] constexpr AutoGradientRemovalDependentUiState
autoGradientRemovalDependentUiState(const bool controlsEnabled, const bool protectionEnabled,
                                    const bool simplifiedEnabled) noexcept
{
    return {.protectionParametersEnabled = controlsEnabled && protectionEnabled,
            .protectionMaskOutputEnabled = controlsEnabled && protectionEnabled,
            .protectionMaskPreviewEnabled = controlsEnabled && protectionEnabled,
            .polynomialDegreeEnabled = controlsEnabled && simplifiedEnabled};
}

/// Auto Gradient Removal: finds a still image's background with no sample placed by hand - fitting, rejecting the
/// samples that stand above it and fitting again - and subtracts or divides it out, through the engine's
/// `removeImageAutoBackground`. An Apply can open the background model and the structure the fit was kept away from as
/// images of their own; the preview, computed when its button asks, can show either instead of the result.
class AFTERNIGHTPROCESSES_API AutoGradientRemovalProcess
    : public an::ui::TypedProcessControl<AutoGradientRemovalProcessParams>
{
public:
    explicit AutoGradientRemovalProcess(ImageViewControl* control);

    static std::string getProcessNameStatic();
    static bool requiresExclusiveAccessStatic();
    static constexpr bool requiresStillImageStatic() noexcept
    {
        return true;
    }

    ~AutoGradientRemovalProcess() override;

protected:
    an::Status executeTypedProcessImpl(const an::ui::ProcessSource& source, an::ui::ProcessDestination& destination,
                                       const an::ui::ProcessRun& run, const AutoGradientRemovalProcessParams& params,
                                       an::ui::ExecutionScope& scope) override;
    [[nodiscard]] an::ui::ProcessStep
    describeTypedProcessStep(const AutoGradientRemovalProcessParams& params) const override;

private:
    void on_scaleChanged(double value);
    void on_smoothnessChanged(double value);
    void on_downsampleChanged(int index);
    void on_structureProtectionChanged(Qt::CheckState state);
    void on_protectionThresholdChanged(double value);
    void on_protectionAmountChanged(double value);
    void on_simplifiedModelChanged(Qt::CheckState state);
    void on_polynomialDegreeChanged(int value);
    void on_correctionModeChanged(int index);
    void on_previewDisplayChanged(int index);
    void on_previewButtonPushed();
    void on_outputGradientChanged(Qt::CheckState state);
    void on_outputProtectionMaskChanged(Qt::CheckState state);
    void on_previewCheckboxChanged(Qt::CheckState state);

    void onViewControlChanged(ImageViewControl* oldCtrl, ImageViewControl* newCtrl) override;
    bool isViewControlSupported(const ImageViewControl* ctrl) override;

    void initialize();
    void invalidatePreview();
    void syncDependentUiState(bool controlsEnabled);
    void syncPreviewUiState(bool controlsEnabled);

    [[nodiscard]] bool isPreviewEnabled() const override;
    void enablePreview(bool enabled) override;
    void previewControlClosed() override;

    [[nodiscard]] AutoGradientRemovalProcessParams getParams() const override;
    void executeProcessStarted(RTPreviewMode previewMode, bool sequenceMode) override;
    void executeProcessCompletedImpl(RTPreviewMode previewMode, bool sequenceMode) override;
    void settingsUpdatedImpl() override;
    void enableProcessControlsImpl(bool enabled) override;
    void resetSettings() override;
    void refreshTheme() override;
    ProgressDialogControl* getProgressDialogControl(bool forSequence) override;

    QSliderDoubleSpinBoxUPtr scaleControl;
    QSliderDoubleSpinBoxUPtr smoothnessControl;
    QSliderDoubleSpinBoxUPtr protectionThresholdControl;
    QSliderDoubleSpinBoxUPtr protectionAmountControl;
    QSliderSpinBoxUPtr polynomialDegreeControl;

    bool m_previewEnabled = true;

    /// Whether the preview computes: set by its button, cleared by a change of a setting or of the target.
    bool m_previewComputed = false;

    /// The background model and the protection mask an Apply made, and what it warned of, waiting for the result to
    /// land.
    process_utils::StagedOutputs m_StagedOutputs;
    std::mutex m_warningMutex;
    QString m_pendingWarningText;

    an::ui::processes::AutoGradientRemovalProcessUiElements m_ui;
};
