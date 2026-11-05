// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightProcessesExport.h"

#include "processes/GeometryProcessSupport.h"
#include "processes/ResizeProcessUiBuilder.h"
#include "controls/ImageProcessControl.h"

#include <cstdint>
#include <string>

class ImageViewControl;

/// The Resize window: scales its view's image, or every frame of its sequence, to the width and height the dialog
/// states, through the interpolation it names - Auto choosing Lanczos-4 to enlarge and the pixel area relation to
/// reduce, and saying so. The dialog is read on the GUI thread when Apply is pressed; the resize runs on a worker as an
/// edit the history records.
class AFTERNIGHTPROCESSES_API ResizeProcess : public ImageProcessControl
{
public:
    ResizeProcess(ImageViewControl* control);

    static std::string getProcessNameStatic();
    static bool requiresExclusiveAccessStatic();

    /// What the dialog asks for.
    struct Settings
    {
        /// The image's width and height when the dialog read them.
        std::uint32_t originalWidth = 0u;
        std::uint32_t originalHeight = 0u;

        /// The result's.
        std::uint32_t width = 0u;
        std::uint32_t height = 0u;

        /// The interpolation the choice stands for, Auto resolved.
        an::ui::processes::ResolvedInterpolation interpolation;
    };

    /// What the dialog asks for now. GUI thread.
    [[nodiscard]] Settings settings() const;

    /// The history step a resize of these settings records.
    [[nodiscard]] static std::string stepDescription(const Settings& settings);

    void onImageUpdated(ImageViewControl* ctrl) override;

    void onViewControlChanged(ImageViewControl* oldCtrl, ImageViewControl* newCtrl) override;

    ~ResizeProcess();

protected:
    /// Called with a run's operation once it is admitted; nothing in the product.
    [[nodiscard]] virtual an::ui::processes::AdmissionHook admissionHook() const
    {
        return {};
    }

private:
    void on_dialog_box_button_clicked(QAbstractButton* button);

    void on_targetWidthChanged(int value);
    void on_targetHeightChanged(int value);

    void on_targetWidthPercentChanged(double value);
    void on_targetHeightPercentChanged(double value);

    void on_keepAspectRatioClicked(Qt::CheckState state);

    void refreshTheme() override;

private:
    void initialize();
    void refreshSourceImageData();
    void recalculateNewSize();
    void checkChangesPending();

    void executeImageResize();
    void finishRun(const an::ui::processes::GeometryRunOutcome& outcome);

    bool m_IgnoreLastTargetWidthPercent = false;
    bool m_IgnoreLastTargetHeightPercent = false;
    bool m_IgnoreLastTargetWidth = false;
    bool m_IgnoreLastTargetHeight = false;

    an::ui::processes::ResizeProcessUiElements m_ui;
};
