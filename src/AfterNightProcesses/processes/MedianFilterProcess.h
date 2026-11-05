// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightProcessesExport.h"

#include "controls/TypedProcessControl.h"
#include "controls/QSliderSpinBox.h"
#include "processes/MedianFilterProcessUiBuilder.h"

class ImageViewControl;

struct MedianFilterParams
{
    int kernelSize = 0;
    int iterations = 0;
    double modulation = 0.0;
};

class AFTERNIGHTPROCESSES_API MedianFilterProcess : public an::ui::TypedProcessControl<MedianFilterParams>
{
public:
    MedianFilterProcess(ImageViewControl* control);

    static std::string getProcessNameStatic();
    static bool requiresExclusiveAccessStatic();

    ~MedianFilterProcess();

    void refreshTheme() override;

private:
    void on_kernelSize_currentTextChanged(const QString& text);

    void on_iterationsChanged(int value);
    void on_modulationChanged(double value);

private:
    bool isPreviewEnabled() const override;
    void enablePreview(bool enabled) override;
    an::Status executeTypedProcessIteration(const an::ChannelScopeInput& input, const an::ChannelScopeOutput& output,
                                            const an::ui::ProcessRun& run, const MedianFilterParams& params,
                                            an::ui::ExecutionScope& scope) override;
    an::Status executeTypedProcessImpl(const an::ui::ProcessSource& source, an::ui::ProcessDestination& destination,
                                       const an::ui::ProcessRun& run, const MedianFilterParams& params,
                                       an::ui::ExecutionScope& scope) override;
    [[nodiscard]] an::ui::ProcessStep describeTypedProcessStep(const MedianFilterParams& params) const override;
    [[nodiscard]] an::ui::PreviewScaling previewScaling() const override;
    [[nodiscard]] std::uint32_t typedPreviewRegionMargin(const MedianFilterParams& params) const override;

    void resetSettings() override;

    void settingsUpdatedImpl() override;
    void enableProcessControlsImpl(bool enabled) override;

    void initialize();

    ProgressDialogControl* getProgressDialogControl(const bool forSequence) override;
    [[nodiscard]] MedianFilterParams getParams() const override;

    an::ui::processes::MedianFilterProcessUiElements m_ui;
    QSliderSpinBoxUPtr iterationsControl;
    QSliderDoubleSpinBoxUPtr modulationControl;
};
