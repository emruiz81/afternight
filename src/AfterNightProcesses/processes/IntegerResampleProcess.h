// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightProcessesExport.h"

#include "processes/GeometryProcessSupport.h"
#include "processes/IntegerResampleProcessUiBuilder.h"
#include "controls/ImageProcessControl.h"
#include "controls/ProgressDialogControl.h"

#include <cstdint>
#include <string>

class ImageViewControl;

/// The Integer Resample window: enlarges its view's image, or every frame of its sequence, by an integer factor, each
/// sample repeated, or reduces it by one, each block of samples combined by the reducer the dialog names. The dialog is
/// read on the GUI thread when Apply is pressed; the resample runs on a worker as an edit the history records.
class AFTERNIGHTPROCESSES_API IntegerResampleProcess : public ImageProcessControl
{
public:
    IntegerResampleProcess(ImageViewControl* control);

    static std::string getProcessNameStatic();
    static bool requiresExclusiveAccessStatic();

    /// What the dialog asks for.
    struct Settings
    {
        std::uint32_t factor = 2u;
        bool enlarge = false;
        QString reductionLabel;
        std::uint32_t width = 0u;
        std::uint32_t height = 0u;
    };

    /// What the dialog asks for now. GUI thread.
    [[nodiscard]] Settings settings() const;

    /// The history step a resample of these settings records.
    [[nodiscard]] static std::string stepDescription(const Settings& settings);

    void onImageUpdated(ImageViewControl* ctrl) override;

    void onViewControlChanged(ImageViewControl* oldCtrl, ImageViewControl* newCtrl) override;

    void refreshTheme() override;

    ~IntegerResampleProcess();

protected:
    /// Called with a run's operation once it is admitted; nothing in the product.
    [[nodiscard]] virtual an::ui::processes::AdmissionHook admissionHook() const
    {
        return {};
    }

private:
    void on_dialog_box_button_clicked(QAbstractButton* button);

    void on_resampleFactorChanged(int value);

    void on_downsampleRadioButtonClicked(bool checked);
    void on_upsampleRadioButtonClicked(bool checked);

private:
    void initialize();
    void updateResampleFactor();
    void refreshSourceImageData();
    void recalculateNewSize();
    void checkChangesPending();

    void executeImageResample();
    void finishRun(const an::ui::processes::GeometryRunOutcome& outcome);

    an::ui::processes::IntegerResampleProcessUiElements m_ui;
};
