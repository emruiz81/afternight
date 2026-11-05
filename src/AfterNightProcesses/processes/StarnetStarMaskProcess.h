// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightProcessesExport.h"

#include "processes/ExternalToolSupport.h"
#include "processes/GeometryProcessSupport.h"
#include "processes/StarnetStarMaskProcessUiBuilder.h"
#include "controls/ImageProcessControl.h"
#include "controls/QSliderSpinBox.h"

#include <QString>

#include <string>

class ImageViewControl;

/// The StarNet star mask window: hands its view's image - or every frame of its sequence - to the StarNet2 program
/// through the engine's hand-off, stretched linked when asked, and opens the star mask the program writes as a new
/// view beside the target: the tool's own answer, reduced to its lightness and selected between two limits when
/// asked. The dialog is read on the GUI thread when OK is pressed; the program and its answer run on a worker behind
/// a progress window whose Cancel kills the program.
class AFTERNIGHTPROCESSES_API StarnetStarMaskProcess : public ImageProcessControl
{
public:
    StarnetStarMaskProcess(ImageViewControl* control);

    static std::string getProcessNameStatic();
    static bool requiresExclusiveAccessStatic();

    /// What the dialog asks for.
    struct Settings
    {
        /// The tile stride, even, from 2 to 512.
        int stride = 256;

        /// Whether the program upsamples twice before it works.
        bool upsample = false;

        /// Whether the program's highlight protection stays on.
        bool highlightProtection = true;

        /// A weights file of the person's own, or empty for the program's.
        QString weightsPath;

        /// Whether the image is stretched, linked, before the program sees it.
        bool preStretch = false;

        /// How the mask is shaped once it is back.
        an::ui::processes::StarMaskShaping shaping;
    };

    /// What the dialog asks for now. GUI thread.
    [[nodiscard]] Settings settings() const;

    /// What a run of these settings asks of the program.
    [[nodiscard]] static an::ui::processes::ExternalToolRequest toolRequest(const Settings& settings,
                                                                            const QString& executablePath);

    /// Runs the program over the view, as OK does.
    void startExecution();

    ~StarnetStarMaskProcess();

protected:
    /// Called with a run's operation once it is admitted; nothing in the product.
    [[nodiscard]] virtual an::ui::processes::AdmissionHook admissionHook() const
    {
        return {};
    }

private:
    void on_dialog_box_button_clicked(QAbstractButton* button);

    void on_customStrideValueChanged(int value);

    void on_stridePresetChanged(int index);

    void on_browseWeightsButtonPushed();

    void on_lightnessClicked(Qt::CheckState state);
    void on_binaryMaskClicked(Qt::CheckState state);

    void on_lowerLimitChanged(double value);
    void on_upperLimitChanged(double value);
    void on_smoothnessChanged(double value);

    void on_configureButtonPushed();

private:
    void refreshTheme() override;

    void initialize();

    void enableActions();

    void disableActions(const QString& reason = {});

    [[nodiscard]] int currentStride() const;

    void finishRun(const an::ui::processes::GeometryRunOutcome& outcome);

    QString m_StarnetFullPath;

    QSliderDoubleSpinBoxUPtr lowerLimitControl;
    QSliderDoubleSpinBoxUPtr upperLimitControl;
    QSliderDoubleSpinBoxUPtr smoothnessControl;

    an::ui::processes::StarnetStarMaskProcessUiElements m_ui;
};
