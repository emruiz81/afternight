// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightProcessesExport.h"

#include "processes/CfaProcessSupport.h"
#include "processes/DebayerProcessUiBuilder.h"
#include "processes/GeometryProcessSupport.h"
#include "controls/ImageProcessControl.h"

#include <string>

class ImageViewControl;

/// The Debayer window: demosaics its view's mosaic, or every frame of its sequence, by the method the dialog names
/// under the filter array it names - the image's own for "From metadata", which a mosaic that states none refuses - and
/// opens the colour image, its three channels, or both, as new views. The dialog is read on the GUI thread when Apply
/// is pressed; the demosaic runs on a worker.
class AFTERNIGHTPROCESSES_API DebayerProcess : public ImageProcessControl
{
public:
    DebayerProcess(ImageViewControl* control);

    static std::string getProcessNameStatic();
    static bool requiresExclusiveAccessStatic();

    /// What the dialog asks for.
    struct Settings
    {
        /// The method, and the dialog's text for it.
        an::DemosaicMethod method = an::DemosaicMethod::Rcd;
        QString methodLabel;

        /// The filter array the choice names.
        an::ui::BayerPatternChoice pattern;

        /// What the run opens.
        an::ui::processes::DebayerOutput output = an::ui::processes::DebayerOutput::Combined;
    };

    /// What the dialog asks for now. GUI thread.
    [[nodiscard]] Settings settings() const;

    /// The text the pattern choice that reads the image's own filter array shows.
    [[nodiscard]] static QString fromMetadataText();

    ~DebayerProcess();

protected:
    /// Called with a run's operation once it is admitted; nothing in the product.
    [[nodiscard]] virtual an::ui::processes::AdmissionHook admissionHook() const
    {
        return {};
    }

private:
    void refreshTheme() override;

    void on_dialog_box_button_clicked(QAbstractButton* button);

private:
    void initialize();

    void executeImageDebayer();
    void finishRun(const an::ui::processes::GeometryRunOutcome& outcome);

    an::ui::processes::DebayerProcessUiElements m_ui;
};
