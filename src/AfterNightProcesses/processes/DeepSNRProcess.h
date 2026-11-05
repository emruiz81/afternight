// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightProcessesExport.h"

#include "processes/DeepSNRProcessUiBuilder.h"
#include "processes/ExternalToolSupport.h"
#include "processes/GeometryProcessSupport.h"
#include "controls/ImageProcessControl.h"

#include <QString>

#include <cstdint>
#include <string>

class ImageViewControl;

/// The DeepSNR window: hands its view's image - or every frame of its sequence - to the DeepSNR program through the
/// engine's hand-off and puts the denoised answer in the image's place, as an edit the history records before the
/// view shows it. The dialog is read on the GUI thread when OK is pressed; the hand-off, the program and its answer
/// run on a worker behind a progress window whose Cancel kills the program.
class AFTERNIGHTPROCESSES_API DeepSNRProcess : public ImageProcessControl
{
public:
    DeepSNRProcess(ImageViewControl* control);

    static std::string getProcessNameStatic();
    static bool requiresExclusiveAccessStatic();
    static bool isConfigured();

    /// What the dialog asks for.
    struct Settings
    {
        /// The tile stride, even, from 2 to 512.
        int stride = 480;

        /// A weights file of the person's own, or empty for the program's.
        QString weightsPath;

        /// Whether the image is stretched channel by channel before the program sees it, and the answer brought back
        /// through the inverse.
        bool preStretch = false;
    };

    /// What the dialog asks for now. GUI thread.
    [[nodiscard]] Settings settings() const;

    /// The history step a run of these settings records.
    [[nodiscard]] static std::string stepDescription(const Settings& settings);

    /// The smallest image the program takes, on either side: 512 pixels.
    static constexpr std::uint32_t kMinimumImageSize = 512u;

    /// What a run of these settings asks of the program, which takes no image smaller than `minimumSize` on either
    /// side.
    [[nodiscard]] static an::ui::processes::ExternalToolRequest
    toolRequest(const Settings& settings, const QString& executablePath, std::uint32_t minimumSize = kMinimumImageSize);

    /// Runs the program over the view, as OK does.
    void startExecution();

    ~DeepSNRProcess();

protected:
    /// Called with a run's operation once it is admitted; nothing in the product.
    [[nodiscard]] virtual an::ui::processes::AdmissionHook admissionHook() const
    {
        return {};
    }

    /// The smallest image a run hands the program: the program's own floor in the product.
    [[nodiscard]] virtual std::uint32_t minimumImageSize() const
    {
        return kMinimumImageSize;
    }

private:
    void on_dialog_box_button_clicked(QAbstractButton* button);

    void on_customStrideValueChanged(int value);

    void on_stridePresetChanged(int index);

    void on_browseWeightsButtonPushed();

    void on_configureButtonPushed();

private:
    void refreshTheme() override;

    void initialize();

    void enableActions();

    void disableActions(const QString& reason = {});

    void disableActionsForExecution();

    void finishRun(const an::ui::processes::GeometryRunOutcome& outcome);

    [[nodiscard]] int currentStride() const;

    QString m_DeepSNRFullPath;

    an::ui::processes::DeepSNRProcessUiElements m_ui;
};
