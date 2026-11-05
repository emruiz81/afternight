// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightProcessesExport.h"

#include "controls/QSliderSpinBox.h"
#include "controls/TypedProcessControl.h"
#include "processes/DarkStructureEnhanceProcessUiBuilder.h"
#include "processes/MaskProcessUtils.h"

#include <AfterNightEngine/Algorithms/Detail/DarkStructureEnhance.h>
#include <AfterNightEngine/Image/ImageMetadata.h>

#include <memory>
#include <string>
#include <string_view>

class ImageViewControl;

enum class DarkStructureEnhancePreviewMode
{
    ProcessedImage = 0,
    GeneratedMask
};

struct DarkStructureEnhanceParams
{
    int layersToRemove = 8;
    int scalingFunction = 1;
    double amount = 0.4;
    int iterations = 1;
    DarkStructureEnhancePreviewMode previewMode = DarkStructureEnhancePreviewMode::ProcessedImage;
    bool createMaskImage = false;

    /// The name of the target a mask image is named after, read on the GUI thread.
    std::string targetName;

    /// The metadata of the image the run reads, which the mask an Apply keeps derives its record from; stated by the
    /// run, not by the window.
    std::shared_ptr<const an::ImageMetadata> sourceMetadata;
};

namespace an::ui::processes::dark_structure_enhance_detail
{

/// The smoothing kernel the dialog's scaling entry names: its first entry the three-tap linear interpolation, every
/// other the five-tap B3 spline.
[[nodiscard]] AFTERNIGHTPROCESSES_API an::MultiscaleScalingFunction scalingFromIndex(int index) noexcept;

/// The midtones balance the dialog's amount stands for: one half plus half the amount, so the dialog's whole range
/// darkens.
[[nodiscard]] AFTERNIGHTPROCESSES_API double midtonesFromAmount(double amount) noexcept;

/// The engine's request for the dialog's values, in the unit interval the samples are read in.
[[nodiscard]] AFTERNIGHTPROCESSES_API an::DarkStructureRequest buildRequest(const DarkStructureEnhanceParams& params);

/// What a history step adds for the channel a run ran on: " in channel: " and the selector's label for one channel or
/// component of a colour target, nothing for its whole image or a grey one.
[[nodiscard]] AFTERNIGHTPROCESSES_API std::string channelHistorySuffix(std::string_view channelLabel);

} // namespace an::ui::processes::dark_structure_enhance_detail

/// DarkStructureEnhance: darkens the structures darker than the frame's own smooth illumination, through a mask the
/// engine's `enhanceImageDarkStructures` builds from two multiscale smoothings and applies through a midtones transfer,
/// on the channel or colour component the selector names. Its preview can show the mask instead, and an Apply can open
/// the mask it used as an image of its own.
class AFTERNIGHTPROCESSES_API DarkStructureEnhanceProcess
    : public an::ui::TypedProcessControl<DarkStructureEnhanceParams>
{
public:
    explicit DarkStructureEnhanceProcess(ImageViewControl* control);

    static std::string getProcessNameStatic();
    static bool requiresExclusiveAccessStatic();

    ~DarkStructureEnhanceProcess() override;

protected:
    an::Status executeTypedProcessIteration(const an::ChannelScopeInput& input, const an::ChannelScopeOutput& output,
                                            const an::ui::ProcessRun& run, const DarkStructureEnhanceParams& params,
                                            an::ui::ExecutionScope& scope) override;
    an::Status executeTypedProcessImpl(const an::ui::ProcessSource& source, an::ui::ProcessDestination& destination,
                                       const an::ui::ProcessRun& run, const DarkStructureEnhanceParams& params,
                                       an::ui::ExecutionScope& scope) override;
    [[nodiscard]] an::ui::ProcessStep describeTypedProcessStep(const DarkStructureEnhanceParams& params) const override;

private:
    void on_layersChanged(int value);
    void on_amountChanged(double value);
    void on_iterationsChanged(int value);
    void on_scalingFunctionChanged(int index);
    void on_previewModeChanged(int index);
    void on_previewChanged(Qt::CheckState state);
    void on_createMaskImageChanged(Qt::CheckState state);

    bool isPreviewEnabled() const override;
    void enablePreview(bool enabled) override;
    void executeProcessStarted(RTPreviewMode previewMode, bool sequenceMode) override;
    void executeProcessCompletedImpl(RTPreviewMode previewMode, bool sequenceMode) override;

    void refreshTheme() override;
    void resetSettings() override;
    void settingsUpdatedImpl() override;
    void enableProcessControlsImpl(bool enabled) override;
    ProgressDialogControl* getProgressDialogControl(bool forSequence) override;
    [[nodiscard]] DarkStructureEnhanceParams getParams() const override;

    void initialize();
    void updateUiState();

    QSliderSpinBoxUPtr layersControl;
    QSliderDoubleSpinBoxUPtr amountControl;
    QSliderSpinBoxUPtr iterationsControl;

    /// The mask an Apply used, waiting to be opened once the result lands.
    process_utils::StagedOutputs m_StagedOutputs;

    an::ui::processes::DarkStructureEnhanceProcessUiElements m_ui;
};
