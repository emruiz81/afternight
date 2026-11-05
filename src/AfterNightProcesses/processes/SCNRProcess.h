// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightProcessesExport.h"

#include "processes/ColorToolsSupport.h"
#include "processes/GeometryProcessSupport.h"
#include "processes/SCNRProcessUiBuilder.h"
#include "controls/ImageProcessControl.h"
#include "controls/QSliderSpinBox.h"

#include <AfterNightEngine/Algorithms/NoiseReduction/NoiseReduction.h>

#include <memory>
#include <string>

class ImageViewControl;

/// The SCNR window: suppresses the green of its view's colour image, or of every frame of its sequence, by one of four
/// protection methods. The dialog is read on the GUI thread when Apply is pressed; the suppression runs on a worker as
/// an edit the history records, weighed by the view's mask, and the window stays open for another run.
class AFTERNIGHTPROCESSES_API SCNRProcess : public ImageProcessControl
{
public:
    SCNRProcess(ImageViewControl* control);

    static std::string getProcessNameStatic();
    static bool requiresExclusiveAccessStatic();

    /// What the dialog asks for.
    struct Settings
    {
        /// The protection method's text, as the dialog lists it.
        QString protection;

        /// What the suppression runs with.
        an::ui::processes::GreenSuppression suppression;
    };

    /// What the dialog asks for now. GUI thread.
    [[nodiscard]] Settings settings() const;

    /// The history step a run of these settings records.
    [[nodiscard]] static std::string stepDescription(const Settings& settings);

    ~SCNRProcess();

protected:
    /// Called with a run's operation once it is admitted; nothing in the product.
    [[nodiscard]] virtual an::ui::processes::AdmissionHook admissionHook() const
    {
        return {};
    }

private:
    void on_dialog_box_button_clicked(QAbstractButton* button);
    void on_protectionType_currentTextChanged(const QString& text);

    void on_amountChanged(double value);

private:
    void initialize();
    void refreshTheme() override;

    void executeSCNR();
    void finishRun(const an::ui::processes::GeometryRunOutcome& outcome);

    bool isViewControlSupported(const ImageViewControl* ctrl) override;

    QSliderDoubleSpinBoxUPtr amountControl;

    an::ui::processes::SCNRProcessUiElements m_ui;
};
