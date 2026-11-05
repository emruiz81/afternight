// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightProcessesExport.h"

#include "processes/HighPassFilterProcessUiBuilder.h"
#include "controls/TypedProcessControl.h"
#include "controls/QSliderSpinBox.h"

#include <readerwriterqueue/readerwriterqueue.h>

class ImageViewControl;

struct HighPassFilterProcessParams
{
    double radius = 0.0;
};

class AFTERNIGHTPROCESSES_API HighPassFilterProcess : public an::ui::TypedProcessControl<HighPassFilterProcessParams>
{
public:
    HighPassFilterProcess(ImageViewControl* control);

    static std::string getProcessNameStatic();
    static bool requiresExclusiveAccessStatic();

    ~HighPassFilterProcess();

private:
    void on_radiusChanged(double value);

    bool isPreviewEnabled() const override;
    void enablePreview(bool enabled) override;
    an::Status executeTypedProcessIteration(const an::ChannelScopeInput& input, const an::ChannelScopeOutput& output,
                                            const an::ui::ProcessRun& run, const HighPassFilterProcessParams& params,
                                            an::ui::ExecutionScope& scope) override;
    an::Status executeTypedProcessImpl(const an::ui::ProcessSource& source, an::ui::ProcessDestination& destination,
                                       const an::ui::ProcessRun& run, const HighPassFilterProcessParams& params,
                                       an::ui::ExecutionScope& scope) override;
    [[nodiscard]] an::ui::ProcessStep
    describeTypedProcessStep(const HighPassFilterProcessParams& params) const override;
    [[nodiscard]] an::ui::PreviewScaling previewScaling() const override;
    [[nodiscard]] std::uint32_t typedPreviewRegionMargin(const HighPassFilterProcessParams& params) const override;

    void resetSettings() override;

    void settingsUpdatedImpl() override;
    void enableProcessControlsImpl(bool enabled) override;
    [[nodiscard]] HighPassFilterProcessParams getParams() const override;

    QSliderDoubleSpinBoxSPtr radiusControl;

    an::ui::processes::HighPassFilterProcessUiElements m_ui;
};
