// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightProcessesExport.h"

#include "controls/QSliderSpinBox.h"
#include "controls/TypedProcessControl.h"
#include "processes/ColorizeProcessUiBuilder.h"

class ImageViewControl;

struct ColorizeParams
{
    int method = 0; // 0: HSV, 1: HSI
    double hue = 0.0;
    double saturation = 0.0;
    bool useLightness = false;
};

/// A fixed hue and saturation given to an image, whose intensity each pixel keeps as HSV value or HSI intensity: a grey
/// image becomes a colour one, a colour image's channels are each colorized from their own intensity, and a colour
/// image colorized from its CIE lightness becomes the colour of that lightness. The result is always red, green and
/// blue in the source's representation.
class AFTERNIGHTPROCESSES_API ColorizeProcess : public an::ui::TypedProcessControl<ColorizeParams>
{
public:
    ColorizeProcess(ImageViewControl* control);

    static std::string getProcessNameStatic();
    static bool requiresExclusiveAccessStatic();

    void onImageUpdated(ImageViewControl* ctrl) override;
    void onViewControlChanged(ImageViewControl* oldCtrl, ImageViewControl* newCtrl) override;

    ~ColorizeProcess();

protected:
    an::Status executeTypedProcessImpl(const an::ui::ProcessSource& source, an::ui::ProcessDestination& destination,
                                       const an::ui::ProcessRun& run, const ColorizeParams& params,
                                       an::ui::ExecutionScope& scope) override;
    [[nodiscard]] an::ui::ProcessStep describeTypedProcessStep(const ColorizeParams& params) const override;

private:
    void on_hueChanged(int value);
    void on_saturationChanged(int value);

    void on_method_currentTextChanged(const QString& str);
    void on_useLightness_clicked(Qt::CheckState state);

    bool isPreviewEnabled() const override;
    void enablePreview(bool enabled) override;

    void resetSettings() override;

    void settingsUpdatedImpl() override;
    void enableProcessControlsImpl(bool enabled) override;
    void refreshTheme() override;
    [[nodiscard]] ColorizeParams getParams() const override;

    bool isViewControlSupported(const ImageViewControl* ctrl) override;

    an::ui::processes::ColorizeProcessUiElements m_ui;

    QSliderDoubleSpinBoxUPtr hueControl;
    QSliderDoubleSpinBoxUPtr saturationControl;
};
