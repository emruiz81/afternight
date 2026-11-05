// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightProcessesExport.h"

#include <QtWidgets/QMainWindow>
#include "processes/TotalVariationDenoiseProcessUiBuilder.h"
#include "controls/TypedProcessControl.h"
#include "controls/QSliderSpinBox.h"

#include <readerwriterqueue/readerwriterqueue.h>

class ImageViewControl;

struct TotalVariationDenoiseParams
{
    int algorithmSelector = 0;
    double weight = 0.0;
    double eps = 0.0;
    int maxIterations = 0;

    bool isotropic = false;
};

class AFTERNIGHTPROCESSES_API TotalVariationDenoiseProcess
    : public an::ui::TypedProcessControl<TotalVariationDenoiseParams>
{
public:
    TotalVariationDenoiseProcess(ImageViewControl* control);

    static std::string getProcessNameStatic();
    static bool requiresExclusiveAccessStatic();

    ~TotalVariationDenoiseProcess();

private:
    void on_weightChanged(double value);
    void on_epsChanged(double value);
    void on_maxIterationsChanged(int value);

    void on_algorithmSelector_currentTextChanged(const QString& str);

private:
    bool isPreviewEnabled() const override;
    void enablePreview(bool enabled) override;
    an::Status executeTypedProcessIteration(const an::ChannelScopeInput& input, const an::ChannelScopeOutput& output,
                                            const an::ui::ProcessRun& run, const TotalVariationDenoiseParams& params,
                                            an::ui::ExecutionScope& scope) override;
    an::Status executeTypedProcessImpl(const an::ui::ProcessSource& source, an::ui::ProcessDestination& destination,
                                       const an::ui::ProcessRun& run, const TotalVariationDenoiseParams& params,
                                       an::ui::ExecutionScope& scope) override;
    [[nodiscard]] an::ui::ProcessStep
    describeTypedProcessStep(const TotalVariationDenoiseParams& params) const override;

    void settingsUpdatedImpl() override;
    void enableProcessControlsImpl(bool enabled) override;

    void resetSettings() override;

    void refreshTheme() override;
    ProgressDialogControl* getProgressDialogControl(const bool forSequence) override;
    [[nodiscard]] TotalVariationDenoiseParams getParams() const override;

    QSliderDoubleSpinBoxUPtr weightControl;
    QSliderDoubleSpinBoxUPtr epsControl;
    QSliderSpinBoxUPtr maxIterationsControl;

    an::ui::processes::TotalVariationDenoiseProcessUiElements m_ui;
};
