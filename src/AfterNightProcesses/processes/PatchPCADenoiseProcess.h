// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightProcessesExport.h"

#include "processes/PatchPCADenoiseProcessUiBuilder.h"
#include "controls/TypedProcessControl.h"
#include "controls/QSliderSpinBox.h"

class ImageViewControl;

struct PatchPCADenoiseParams
{
    double strenght = 0.0;
    int patchSize = 0;
    int lookupSize = 0;
    int spatialSampling = 0;
};

class AFTERNIGHTPROCESSES_API PatchPCADenoiseProcess : public an::ui::TypedProcessControl<PatchPCADenoiseParams>
{
public:
    PatchPCADenoiseProcess(ImageViewControl* control);

    static std::string getProcessNameStatic();
    static bool requiresExclusiveAccessStatic();

    void onImageUpdated(ImageViewControl* ctrl) override;

    ~PatchPCADenoiseProcess();

private:
    void on_strenghtChanged(double value);
    void on_patchSizeChanged(int value);
    void on_lookupSizeChanged(int value);
    void on_spatialSamplingChanged(int value);

private:
    bool isPreviewEnabled() const override;
    void enablePreview(bool enabled) override;
    an::Status executeTypedProcessIteration(const an::ChannelScopeInput& input, const an::ChannelScopeOutput& output,
                                            const an::ui::ProcessRun& run, const PatchPCADenoiseParams& params,
                                            an::ui::ExecutionScope& scope) override;
    an::Status executeTypedProcessImpl(const an::ui::ProcessSource& source, an::ui::ProcessDestination& destination,
                                       const an::ui::ProcessRun& run, const PatchPCADenoiseParams& params,
                                       an::ui::ExecutionScope& scope) override;
    [[nodiscard]] an::ui::ProcessStep describeTypedProcessStep(const PatchPCADenoiseParams& params) const override;

    void resetSettings() override;

    void settingsUpdatedImpl() override;
    void refreshTheme() override;
    void enableProcessControlsImpl(bool enabled) override;

    ProgressDialogControl* getProgressDialogControl(const bool forSequence) override;

    void initialize();
    [[nodiscard]] PatchPCADenoiseParams getParams() const override;

    QSliderDoubleSpinBoxUPtr strenghtControl;
    QSliderSpinBoxUPtr patchSizeControl;
    QSliderSpinBoxUPtr lookupSizeControl;
    QSliderSpinBoxUPtr spatialSamplingControl;

    an::ui::processes::PatchPCADenoiseProcessUiElements m_ui;
};
