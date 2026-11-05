// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightProcessesExport.h"

#include "processes/ColorToolsSupport.h"
#include "processes/GeometryProcessSupport.h"
#include "processes/LinearMatchProcessUiBuilder.h"
#include "controls/ImageProcessControl.h"
#include "controls/QSliderSpinBox.h"

#include <memory>
#include <string>

class ImageViewControl;

/// The Linear Match window: matches its view's image, or every frame of its sequence, to a reference image of the same
/// channel count by the normalization domain's scaling match, measured over the samples within the dialog's limits. The
/// dialog and the reference's image are read on the GUI thread when Ok is pressed; the match runs on a worker as an
/// edit the history records, and the window closes once it is shown.
class AFTERNIGHTPROCESSES_API LinearMatchProcess : public ImageProcessControl
{
public:
    LinearMatchProcess(ImageViewControl* control);

    static std::string getProcessNameStatic();
    static bool requiresExclusiveAccessStatic();

    /// What the dialog asks for.
    struct Settings
    {
        /// The reference view's name, or the no-image text.
        QString referenceName;

        /// The fractions a measured sample lies within.
        an::ui::processes::FractionRange range;
    };

    /// What the dialog asks for now. GUI thread.
    [[nodiscard]] Settings settings() const;

    /// The history step a run over a target of these settings records.
    [[nodiscard]] static std::string stepDescription(const Settings& settings, const std::string& targetName,
                                                     bool sequence);

    void onImageUpdated(ImageViewControl* ctrl) override;

    void onViewControlUpdated(const ImageViewControl* ctrl) override;

    void onViewControlCreated(const ImageViewControl* ctrl) override;

    void onViewControlDestroyed(const ImageViewControl* ctrl) override;

    void onViewControlRenamed(const std::string& newName, const std::string& oldName,
                              const ImageViewControl* ctrl) override;

    void onViewControlChanged(ImageViewControl* oldCtrl, ImageViewControl* newCtrl) override;

    ~LinearMatchProcess();

protected:
    /// Called with a run's operation once it is admitted; nothing in the product.
    [[nodiscard]] virtual an::ui::processes::AdmissionHook admissionHook() const
    {
        return {};
    }

private:
    void on_referenceImageSelector_currentTextChanged(const QString& text);

    void on_rejectLowChanged(double value);
    void on_rejectHighChanged(double value);

    void on_dialog_box_button_clicked(QAbstractButton* button);

private:
    void initialize();
    void refreshTheme() override;
    void executeLinearMatch();
    void finishRun(const an::ui::processes::GeometryRunOutcome& outcome);
    void updateOkButton();

    void rebuildReferenceImageSelector(const QString& preferredSelection = QString());

    [[nodiscard]] ImageViewControl* referenceViewNamed(const QString& name) const;

    [[nodiscard]] bool imageIsCompatible(const ImageViewControl* candidate) const;

    QSliderDoubleSpinBoxUPtr rejectLowControl;

    an::ui::processes::LinearMatchProcessUiElements m_ui;
    QSliderDoubleSpinBoxUPtr rejectHighControl;
};
