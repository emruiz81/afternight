// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightProcessesExport.h"

#include "controls/ImageProcessControl.h"
#include "controls/QSliderSpinBox.h"
#include "processes/ExtractSequenceProcessUiBuilder.h"
#include "processes/GeometryProcessSupport.h"

#include <cstdint>

class QAbstractButton;

class ImageViewControl;

/// The Extract Sequence window: writes a range of its view's sequence's frames, as recorded, into a new sequence of the
/// same container, published whole, and opens it beside the view. The range is read on the GUI thread when Apply is
/// pressed; the frames are copied on a worker behind a progress window whose Cancel stops it.
class AFTERNIGHTPROCESSES_API ExtractSequenceProcess : public ImageProcessControl
{
public:
    ExtractSequenceProcess(ImageViewControl* control);

    static std::string getProcessNameStatic();
    static bool requiresExclusiveAccessStatic();

    /// The frames the dialog names, one-based and inclusive, as it shows them.
    struct Settings
    {
        std::uint64_t firstFrame = 1u;
        std::uint64_t lastFrame = 1u;
    };

    /// What the dialog asks for now. GUI thread.
    [[nodiscard]] Settings settings() const;

    void onImageUpdated(ImageViewControl* ctrl) override;
    void refreshTheme() override;

    ~ExtractSequenceProcess();

protected:
    /// Called with a run's operation once it is admitted; nothing in the product.
    [[nodiscard]] virtual an::ui::processes::AdmissionHook admissionHook() const
    {
        return {};
    }

private:
    void on_dialog_box_button_clicked(QAbstractButton* button);

    void on_startFrameChanged(int value);
    void on_endFrameChanged(int value);

private:
    void updateControls();

    void updateSizeAndFrameCount();

    void executeExtractSequence();
    void finishRun(const an::ui::processes::GeometryRunOutcome& outcome);

    bool isViewControlSupported(const ImageViewControl* ctrl) override;

    QSliderSpinBoxUPtr startFrameControl;
    QSliderSpinBoxUPtr endFrameControl;

    an::ui::processes::ExtractSequenceProcessUiElements m_ui;
};
