// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightProcessesExport.h"

#include "processes/ColorCalibrationProcessUiBuilder.h"
#include "processes/ColorToolsSupport.h"
#include "processes/GeometryProcessSupport.h"
#include "processes/MaskProcessUtils.h"
#include "controls/ImageProcessControl.h"
#include "controls/ImageCropController.h"
#include "controls/QSliderSpinBox.h"

#include <QRect>

#include <array>
#include <memory>
#include <string>

class ImageViewControl;

/// The Color Calibration window: balances the colour channels of its view's image, or of every frame of its sequence,
/// against a white reference - each channel's mean within the white limits above its background, normalized to the
/// strongest - or a manual white balance, about each channel's background: the background reference's median. Each
/// reference is an image or a rectangle of it. The dialog and the references' images are read on the GUI thread when
/// Apply is pressed; the calibration runs on a worker as an edit the history records, weighed by the view's mask, the
/// masks of the reference samples within each pair of limits open beside the target when asked for, and the window
/// closes once the result is shown.
class AFTERNIGHTPROCESSES_API ColorCalibrationProcess : public ImageProcessControl, public ImageAreaSelectorControl
{
public:
    ColorCalibrationProcess(ImageViewControl* control);

    static std::string getProcessNameStatic();
    static bool requiresExclusiveAccessStatic();

    /// What the dialog asks for.
    struct Settings
    {
        /// The white reference selector's text: a view's name, or the target's.
        QString whiteName;

        /// The white reference's rectangle and whether the dialog names one.
        QRect whiteRegion;
        bool whiteRegionChecked = false;

        /// The white limits.
        an::ui::processes::FractionRange whiteRange{.lowest = 0.0, .highest = 0.9};

        /// Whether the white balance is the dialog's manual one, and its three values.
        bool manual = false;
        std::array<double, 3u> manualWhite{1.0, 1.0, 1.0};

        /// The background reference selector's text, its rectangle and whether the dialog names one.
        QString backgroundName;
        QRect backgroundRegion;
        bool backgroundRegionChecked = false;

        /// The background limits.
        an::ui::processes::FractionRange backgroundRange{.lowest = 0.0, .highest = 0.1};

        /// Whether the masks of the reference samples within the limits are opened.
        bool whiteMask = false;
        bool backgroundMask = false;
    };

    /// What the dialog asks for now. GUI thread.
    [[nodiscard]] Settings settings() const;

    /// The history step a run records: the white reference's name, rectangle and limits or the manual balance, and the
    /// background reference's.
    [[nodiscard]] static std::string stepDescription(const Settings& settings, const std::string& whiteName,
                                                     const QRect& whiteRegion, const std::string& backgroundName,
                                                     const QRect& backgroundRegion);

    void onImageUpdated(ImageViewControl* ctrl) override;

    void onViewControlChanged(ImageViewControl* oldCtrl, ImageViewControl* newCtrl) override;

    void onViewControlCreated(const ImageViewControl* ctrl) override;

    void onViewControlDestroyed(const ImageViewControl* ctrl) override;

    void onViewControlRenamed(const std::string& newName, const std::string& oldName,
                              const ImageViewControl* ctrl) override;

    void selectionAreaUpdated() override;
    void selectionAreaChanged() override;
    void newSelectionFinished() override;

    ~ColorCalibrationProcess();

protected:
    /// Called with a run's operation once it is admitted; nothing in the product.
    [[nodiscard]] virtual an::ui::processes::AdmissionHook admissionHook() const
    {
        return {};
    }

private:
    void on_dialog_box_button_clicked(QAbstractButton* button);

    void on_whiteReferenceImageSelector_currentTextChanged(const QString& text);
    void on_backgroundReferenceImageSelector_currentTextChanged(const QString& text);

    void on_limitChanged(double value);

    void on_mwbGroupBoxClicked(bool on);

    void on_whiteRoiGroupBoxClicked(bool on);
    void on_backgroundRoiGroupBoxClicked(bool on);

    void on_whiteRoiSelectionButtonClicked(bool checked);
    void on_backgroundRoiSelectionButtonClicked(bool checked);

    void on_whiteRoiLeftValueChanged(int value);
    void on_whiteRoiTopValueChanged(int value);
    void on_backgroundRoiLeftValueChanged(int value);
    void on_backgroundRoiTopValueChanged(int value);
    void on_roiSizeChanged(int value);

private:
    void initialize();
    void refreshTheme() override;

    void executeColorCalibration();
    void finishRun(const an::ui::processes::GeometryRunOutcome& outcome, const std::string& targetName);
    void enableButtons(bool enabled);

    [[nodiscard]] ImageViewControl* getViewControlFromSelectedImage(const QString& name) const;

    void addReferenceItem(const ImageViewControl* ctrl);

    void updateWhiteReferenceImageData();
    void updateBackgroundReferenceImageData();

    void updateSelectionArea();
    void updateManualWhiteBalanceAvailability();
    void destroyCropController();
    void resetWhiteRoiValues();
    void resetBackgroundRoiValues();

    void closeEvent(QCloseEvent* event) override;

    bool isViewControlSupported(const ImageViewControl* ctrl) override;

    QSliderDoubleSpinBoxUPtr whiteLowerLimitControl;
    QSliderDoubleSpinBoxUPtr whiteUpperLimitControl;

    QSliderDoubleSpinBoxUPtr backgroundLowerLimitControl;
    QSliderDoubleSpinBoxUPtr backgroundUpperLimitControl;

    QSliderDoubleSpinBoxUPtr mwbRedControl;
    QSliderDoubleSpinBoxUPtr mwbGreenControl;
    QSliderDoubleSpinBoxUPtr mwbBlueControl;

    // 0: No crop
    // 1: Crop white reference img
    // 2: Crop background reference img
    int curentCropStatus = 0;
    std::unique_ptr<ImageCropController> m_CropController;
    bool m_IgnoreLastCropMoveEvent = false;

    /// The masks a run staged for the window to open once its result is shown.
    std::shared_ptr<process_utils::StagedOutputs> m_StagedMasks = std::make_shared<process_utils::StagedOutputs>();

    an::ui::processes::ColorCalibrationProcessUiElements m_ui;
};
