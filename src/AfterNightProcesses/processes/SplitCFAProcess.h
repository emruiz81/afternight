// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightProcessesExport.h"

#include "processes/CfaProcessSupport.h"
#include "processes/GeometryProcessSupport.h"
#include "processes/SplitCFAProcessUiBuilder.h"
#include "controls/ImageProcessControl.h"

#include <string>

class ImageViewControl;

/// The Split CFA window: takes its view's mosaic, or every frame of its sequence, apart into its four filter-array
/// planes, or extracts its measured H-alpha, its measured green, or the compatibility H-alpha and OIII pair, each as
/// new views. The mosaic's arrangement and phase decide which filter each plane holds; a tabulated arrangement is
/// refused by name. The dialog is read on the GUI thread when Apply is pressed; the extraction runs on a worker.
class AFTERNIGHTPROCESSES_API SplitCFAProcess : public ImageProcessControl
{
public:
    SplitCFAProcess(ImageViewControl* control);

    static std::string getProcessNameStatic();
    static bool requiresExclusiveAccessStatic();

    /// What the dialog extracts.
    enum class Method : std::uint8_t
    {
        SplitChannels,
        HydrogenAlpha,
        HydrogenAlphaAndOxygenIII,
        Green
    };

    /// What the dialog asks for.
    struct Settings
    {
        Method method = Method::SplitChannels;
        an::NarrowbandExtractionScaling scaling = an::NarrowbandExtractionScaling::Native;
    };

    /// What the dialog asks for now. GUI thread.
    [[nodiscard]] Settings settings() const;

    ~SplitCFAProcess();

protected:
    /// Called with a run's operation once it is admitted; nothing in the product.
    [[nodiscard]] virtual an::ui::processes::AdmissionHook admissionHook() const
    {
        return {};
    }

private:
    void refreshTheme() override;

    void on_dialog_box_button_clicked(QAbstractButton* button);

    void on_methodSelector_currentTextChanged(const QString& text);

private:
    void initialize();

    void executeImageExtraction();
    void finishRun(const an::ui::processes::GeometryRunOutcome& outcome);

    bool isViewControlSupported(const ImageViewControl* ctrl) override;

    an::ui::processes::SplitCFAProcessUiElements m_ui;
};
