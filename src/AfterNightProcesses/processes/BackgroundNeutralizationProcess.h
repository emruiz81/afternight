// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightProcessesExport.h"

#include "processes/BackgroundNeutralizationProcessUiBuilder.h"
#include "processes/ColorToolsSupport.h"
#include "processes/GeometryProcessSupport.h"
#include "controls/ImageProcessControl.h"
#include "controls/ImageCropController.h"
#include "controls/QSliderSpinBox.h"

#include <QRect>

#include <memory>
#include <string>

class ImageViewControl;

/// The Background Neutralization window: brings the background of every colour channel of its view's image, or of every
/// frame of its sequence, to one shared level - each channel's median within the dialog's limits, measured over a
/// reference image or a rectangle of it. The dialog and the reference's image are read on the GUI thread when Ok is
/// pressed; the neutralization runs on a worker as an edit the history records, weighed by the view's mask, and the
/// window closes once it is shown.
class AFTERNIGHTPROCESSES_API BackgroundNeutralizationProcess : public ImageProcessControl,
                                                                public ImageAreaSelectorControl
{
public:
    BackgroundNeutralizationProcess(ImageViewControl* control);

    static std::string getProcessNameStatic();
    static bool requiresExclusiveAccessStatic();

    /// What the dialog asks for.
    struct Settings
    {
        /// The reference selector's text: a view's name, or the target's.
        QString referenceName;

        /// The dialog's rectangle of the reference, in its pixels; read only when `regionChecked` says the dialog
        /// names one, the whole reference being measured otherwise.
        QRect region;

        /// Whether the dialog names a rectangle of its own.
        bool regionChecked = false;

        /// The dialog's limits.
        an::ui::processes::FractionRange range{.lowest = 0.0, .highest = 0.1};
    };

    /// What the dialog asks for now. GUI thread.
    [[nodiscard]] Settings settings() const;

    /// The history step a run of these settings over a reference of this name records.
    [[nodiscard]] static std::string stepDescription(const std::string& referenceName, const QRect& region);

    void onImageUpdated(ImageViewControl* ctrl) override;

    void onViewControlChanged(ImageViewControl* oldCtrl, ImageViewControl* newCtrl) override;

    void onViewControlCreated(const ImageViewControl* ctrl) override;

    void onViewControlDestroyed(const ImageViewControl* ctrl) override;

    void onViewControlRenamed(const std::string& newName, const std::string& oldName,
                              const ImageViewControl* ctrl) override;

    void selectionAreaUpdated() override;
    void selectionAreaChanged() override;
    void newSelectionFinished() override;

    ~BackgroundNeutralizationProcess();

protected:
    /// Called with a run's operation once it is admitted; nothing in the product.
    [[nodiscard]] virtual an::ui::processes::AdmissionHook admissionHook() const
    {
        return {};
    }

private:
    void on_dialog_box_button_clicked(QAbstractButton* button);

    void on_backgroundReferenceImageSelector_currentTextChanged(const QString& text);

    void on_backgroundLowerLimitChanged(double value);
    void on_backgroundUpperLimitChanged(double value);

    void on_backgroundRoiGroupBoxClicked(bool on);

    void on_backgroundRoiSelectionButtonClicked(bool checked);

    void on_backgroundRoiLeftValueChanged(int value);
    void on_backgroundRoiTopValueChanged(int value);
    void on_backgroundRoiWidthValueChanged(int value);
    void on_backgroundRoiHeightValueChanged(int value);

private:
    void initialize();
    void refreshTheme() override;

    void executeBackgroundNeutralization();
    void finishRun(const an::ui::processes::GeometryRunOutcome& outcome);
    void enableButtons(bool enabled);

    [[nodiscard]] ImageViewControl* getViewControlFromSelectedImage(const QString& name) const;

    void updateBackgroundReferenceImageData();

    void updateSelectionArea();
    void destroyCropController();
    void resetBackgroundRoiValues();

    void closeEvent(QCloseEvent* event) override;

    bool isViewControlSupported(const ImageViewControl* ctrl) override;

    QSliderDoubleSpinBoxUPtr backgroundLowerLimitControl;
    QSliderDoubleSpinBoxUPtr backgroundUpperLimitControl;

    // 0: No crop
    // 2: Crop background reference img
    int curentCropStatus = 0;
    std::unique_ptr<ImageCropController> m_CropController;
    bool m_IgnoreLastCropMoveEvent = false;

    an::ui::processes::BackgroundNeutralizationProcessUiElements m_ui;
};
