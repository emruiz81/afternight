// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightProcessesExport.h"

#include "processes/ExternalToolSupport.h"
#include "processes/GeometryProcessSupport.h"
#include "processes/StarnetProcessUiBuilder.h"
#include "controls/ImageProcessControl.h"

#include <QString>

#include <functional>
#include <string>

class ImageViewControl;

/// The StarNet window: hands its view's image - or every frame of its sequence - to the StarNet2 program through the
/// engine's hand-off and opens the starless image it answers, and the star mask and the star layer when asked, as new
/// views beside the target. The dialog is read on the GUI thread when OK is pressed; the hand-off, the program and
/// its answers run on a worker behind a progress window whose Cancel kills the program.
class AFTERNIGHTPROCESSES_API StarnetProcess : public ImageProcessControl
{
public:
    using StarlessCreatedCallback = std::function<void(ImageViewControl*)>;

    StarnetProcess(ImageViewControl* control);

    static std::string getProcessNameStatic();
    static bool requiresExclusiveAccessStatic();
    static bool isConfigured();

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

        /// Whether the image is stretched channel by channel before the program sees it, and the answers brought back
        /// through the inverse.
        bool preStretch = false;

        /// Whether a star output is asked for beside the starless image, as the dialog asks by default.
        bool generateStarOutput = true;

        /// Which star output: "mask", "unscreen" or "both".
        QString starOutputMode;
    };

    /// What the dialog asks for now. GUI thread.
    [[nodiscard]] Settings settings() const;

    /// What a run of these settings asks of the program: the tool, how its arguments are spelled, the pre-stretch and
    /// the answers it reads back.
    [[nodiscard]] static an::ui::processes::ExternalToolRequest toolRequest(const Settings& settings,
                                                                            const QString& executablePath);

    /// Whether a star output mode asks for the subtractive mask, and whether for the unscreened star layer.
    [[nodiscard]] static bool requestsMaskOutput(const QString& mode);
    [[nodiscard]] static bool requestsUnscreenOutput(const QString& mode);

    /// Opens the window as the star reduction asks for a starless view: the unscreened star layer selected and no
    /// star output, the starless view handed to the callback once it is shown, and the window closed after.
    void configureIntegratedStarlessGeneration(StarlessCreatedCallback callback);

    /// Runs the program over the view, as OK does.
    void startExecution();

    ~StarnetProcess();

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

    void on_generateStarMaskClicked(Qt::CheckState state);

    void on_browseWeightsButtonPushed();

    void on_configureButtonPushed();

private:
    void refreshTheme() override;

    void initialize();

    void enableActions();

    void disableActions(const QString& reason = {});

    [[nodiscard]] int currentStride() const;

    void finishRun(const an::ui::processes::GeometryRunOutcome& outcome);

    QString m_StarnetFullPath;
    StarlessCreatedCallback m_StarlessCreatedCallback;
    bool m_CloseAfterIntegratedGeneration = false;

    an::ui::processes::StarnetProcessUiElements m_ui;
};
