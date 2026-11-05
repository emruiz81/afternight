// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AutostretchSettingsControlUiBuilder.h"
#include "app/CoalescedGuiUpdate.h"
#include "controls/ImageViewControlListener.h"
#include "widgets/ImageViewWidget.h"
#include "controls/QSliderSpinBox.h"

class AutostretchSettingsControl : public QDialog, public ImageViewControlListener
{
public:
    AutostretchSettingsControl(ImageViewControl* control);

    void onImageRenamed(ImageViewControl* ctrl) override;

    void onImageUpdated(ImageViewControl* ctrl) override;

    // background neutralization & color calibration
    void setColorCalibrationControlsEnabled(bool enabled);

    ~AutostretchSettingsControl();

private:
    void on_dialog_box_button_clicked(QAbstractButton* button);

    void on_targetBackgroundChanged(double value);
    void on_shadowsClippingChanged(double value);
    void on_saturationChanged(double value);
    void on_saturationBackgroundChanged(double value);

    void on_linkedChannelsClicked(Qt::CheckState state);
    void on_neutralizeBackgroundClicked(Qt::CheckState state);
    void on_colorCalibrationClicked(Qt::CheckState state);

    void on_autostretchBannerClicked(Qt::CheckState state);

    void on_applyStretchButtonPushed();

    void on_brightnessChanged(int value);
    void on_contrastChanged(int value);

private:
    void done(int s) override;

    void settingsUpdated();
    void resetSettings();
    void refreshColorActions();

    QSliderDoubleSpinBoxUPtr targetBackgroundControl;
    QSliderDoubleSpinBoxUPtr shadowsClippingControl;
    QSliderDoubleSpinBoxUPtr saturationControl;
    QSliderDoubleSpinBoxUPtr saturationBackgroundControl;
    QSliderSpinBoxUPtr brightnessControl;
    QSliderSpinBoxUPtr contrastControl;

    an::ui::controls::AutostretchSettingsControlUiElements m_ui;

    bool m_IgnoreLastSettingsUpdated = false;

    bool m_ColorCalibrationControlsEnabled = true;

    // The view's display takes the newest settings on the GUI thread, once per event-loop turn.
    an::ui::CoalescedGuiUpdate<AutostretchParams> m_DisplayUpdate;
};
