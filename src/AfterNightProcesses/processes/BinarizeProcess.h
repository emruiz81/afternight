// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightProcessesExport.h"

#include <QtWidgets/QMainWindow>
#include "processes/BinarizeProcessUiBuilder.h"
#include "controls/TypedProcessControl.h"
#include "controls/QSliderSpinBox.h"

class ImageViewControl;

struct BinarizeParams
{
    double rgbk = 0.0;
    double green = 0.0;
    double blue = 0.0;

    int selectionMode = 0;
};

/// Binarize: every sample at or above a threshold becomes its representation's largest value and every other its
/// smallest - one threshold for every channel, or one for each of red, green and blue. The engine's `binarizeImage`
/// compares each stored sample with the threshold stated in that representation's codes.
class AFTERNIGHTPROCESSES_API BinarizeProcess : public an::ui::TypedProcessControl<BinarizeParams>
{
public:
    BinarizeProcess(ImageViewControl* control);

    static std::string getProcessNameStatic();
    static bool requiresExclusiveAccessStatic();

    void onImageLoaded(ImageViewControl* ctrl) override;

    void onImageUpdated(ImageViewControl* ctrl) override;

    void onViewControlChanged(ImageViewControl* oldCtrl, ImageViewControl* newCtrl) override;

    ~BinarizeProcess();

protected:
    an::Status executeTypedProcessImpl(const an::ui::ProcessSource& source, an::ui::ProcessDestination& destination,
                                       const an::ui::ProcessRun& run, const BinarizeParams& params,
                                       an::ui::ExecutionScope& scope) override;
    [[nodiscard]] an::ui::ProcessStep describeTypedProcessStep(const BinarizeParams& params) const override;

private:
    void on_rgbkChanged(double value);
    void on_greenChanged(double value);
    void on_blueChanged(double value);

    void on_selectionMode_currentTextChanged(const QString& text);

    bool isPreviewEnabled() const override;
    void enablePreview(bool enabled) override;

    void refreshTheme() override;

    void resetSettings() override;

    void settingsUpdatedImpl() override;

    void checkImageColorSpace();
    [[nodiscard]] BinarizeParams getParams() const override;

    QSliderDoubleSpinBoxUPtr rgbkControl;
    QSliderDoubleSpinBoxUPtr blueControl;
    QSliderDoubleSpinBoxUPtr greenControl;

    an::ui::processes::BinarizeProcessUiElements m_ui;
};
