// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightProcessesExport.h"

#include "processes/GeometryProcessSupport.h"
#include "processes/RotateProcessUiBuilder.h"
#include "controls/ImageProcessControl.h"
#include "controls/ImageCropController.h"
#include "controls/QSliderSpinBox.h"

#include <cstdint>
#include <optional>
#include <string>

class ImageViewControl;

/// The Rotate window: turns its view's image, or every frame of its sequence, clockwise by the angle the dialog states,
/// on its own canvas or one grown to hold it, and optionally crops the turned image. The dialog is read on the GUI
/// thread when Apply is pressed; the rotation runs on a worker as an edit the history records.
class AFTERNIGHTPROCESSES_API RotateProcess : public ImageProcessControl, public ImageAreaSelectorControl
{
public:
    RotateProcess(ImageViewControl* control);

    static std::string getProcessNameStatic();
    static bool requiresExclusiveAccessStatic();

    /// What the dialog asks for.
    struct Settings
    {
        /// The clockwise angle, in degrees.
        double degrees = 0.0;

        /// Whether the canvas grows to hold the turned image.
        bool expandCanvas = false;

        /// The interpolation the choice stands for, Auto resolved.
        an::ui::processes::ResolvedInterpolation interpolation;

        /// The rectangle of the turned image kept, when the dialog crops it.
        std::optional<an::CropRequest> crop;
    };

    /// What the dialog asks for now. GUI thread.
    [[nodiscard]] Settings settings() const;

    /// The history step a rotation of these settings records.
    [[nodiscard]] static std::string stepDescription(const Settings& settings);

    void onImageUpdated(ImageViewControl* ctrl) override;

    void onViewControlChanged(ImageViewControl* oldCtrl, ImageViewControl* newCtrl) override;

    void selectionAreaUpdated() override;
    void selectionAreaChanged() override;
    void newSelectionFinished() override;

    void refreshTheme() override;

    ~RotateProcess();

protected:
    /// Called with a run's operation once it is admitted; nothing in the product.
    [[nodiscard]] virtual an::ui::processes::AdmissionHook admissionHook() const
    {
        return {};
    }

private:
    void on_dialog_box_button_clicked(QAbstractButton* button);

    // Rotate settings
    void on_rotationAngleChanged(double value);

    // Crop settings
    void on_CropChanged(bool checked);
    void on_ResizeImageChanged(Qt::CheckState state);
    void on_targetPosXChanged(int value);
    void on_targetPosYChanged(int value);
    void on_targetWidthChanged(int value);
    void on_targetHeightChanged(int value);

private:
    void initialize();
    void setCropEnabled(bool enabled);
    void refreshSourceImageData();
    void recalculateNewSize();
    void checkChangesPending();
    void updateSelectionArea();

    void executeImageRotate();
    void finishRun(const an::ui::processes::GeometryRunOutcome& outcome);

    void closeEvent(QCloseEvent* event) override;

    QSliderDoubleSpinBoxUPtr rotationAngleControl;

    bool m_IgnoreLastMoveEvent = false;

    std::unique_ptr<ImageCropController> m_CropController;

    an::ui::processes::RotateProcessUiElements m_ui;
};
