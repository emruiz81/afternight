// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightProcessesExport.h"

#include "processes/CropProcessUiBuilder.h"
#include "processes/GeometryProcessSupport.h"
#include "controls/ImageProcessControl.h"
#include "controls/ImageCropController.h"

#include <cstdint>
#include <memory>
#include <optional>

class ImageViewControl;

/// The Crop window: trims its view's image, or every frame of its sequence, to a rectangle the dialog or the view's
/// selection states. The rectangle is read on the GUI thread when Apply is pressed; the crop runs on a worker as an
/// edit the history records, and a mosaic keeps the filter under every sample through the crop's phase.
class AFTERNIGHTPROCESSES_API CropProcess : public ImageProcessControl, public ImageAreaSelectorControl
{
public:
    CropProcess(ImageViewControl* control);

    static std::string getProcessNameStatic();
    static bool requiresExclusiveAccessStatic();

    /// The rectangle the dialog states, in the image's pixels.
    struct Settings
    {
        std::uint32_t x = 0u;
        std::uint32_t y = 0u;
        std::uint32_t width = 0u;
        std::uint32_t height = 0u;
    };

    /// What the dialog asks for now. GUI thread.
    [[nodiscard]] Settings settings() const;

    /// The history step a crop of these settings records.
    [[nodiscard]] static std::string stepDescription(const Settings& settings);

    void onImageUpdated(ImageViewControl* ctrl) override;

    void onViewControlChanged(ImageViewControl* oldCtrl, ImageViewControl* newCtrl) override;

    void selectionAreaUpdated() override;
    void selectionAreaChanged() override;
    void newSelectionFinished() override;

    void refreshTheme() override;

    ~CropProcess();

protected:
    /// Called with a run's operation once it is admitted; nothing in the product.
    [[nodiscard]] virtual an::ui::processes::AdmissionHook admissionHook() const
    {
        return {};
    }

private:
    void on_dialog_box_button_clicked(QAbstractButton* button);

    void on_targetPosXChanged(int value);
    void on_targetPosYChanged(int value);
    void on_targetWidthChanged(int value);
    void on_targetHeightChanged(int value);

    void on_newSelectionClicked(bool checked);

private:
    void initialize();
    void refreshSourceImageData();
    void recalculateNewSize();
    void checkChangesPending();
    void updateSelectionArea();

    void executeImageCrop();
    void finishRun(const an::ui::processes::GeometryRunOutcome& outcome);

    void closeEvent(QCloseEvent* event) override;

    an::ui::processes::CropProcessUiElements m_ui;

    bool m_IgnoreLastMoveEvent = false;

    std::unique_ptr<ImageCropController> m_CropController;
};
