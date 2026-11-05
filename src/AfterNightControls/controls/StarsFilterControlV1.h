// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightControlsExport.h"

#include <QtWidgets/QMainWindow>
#include "StarsFilterControlV1UiBuilder.h"

#include "StarsFilterWindow.h"
#include "controls/QSliderSpinBox.h"

// The stars-filter window of its star profiler: its layout offers intensity, quality and radius - the fitted amplitude,
// the brightness-weighted roundness figure and half the mean full width.
class AFTERNIGHTCONTROLS_API StarsFilterControlV1 : public StarsFilterWindow
{
public:
    StarsFilterControlV1(StarsFilterControlHandler* parent);

    // The window's controls, for the cases that drive it.
    [[nodiscard]] const an::ui::controls::StarsFilterControlV1UiElements& ui() const;

    ~StarsFilterControlV1();

private:
    QSliderDoubleSpinBoxUPtr intensityMinControl;
    QSliderDoubleSpinBoxUPtr intensityMaxControl;
    QSliderDoubleSpinBoxUPtr qualityMinControl;
    QSliderDoubleSpinBoxUPtr qualityMaxControl;
    QSliderDoubleSpinBoxUPtr radiusMinControl;
    QSliderDoubleSpinBoxUPtr radiusMaxControl;
    QSliderDoubleSpinBoxUPtr randomSelectionControl;

    an::ui::controls::StarsFilterControlV1UiElements m_ui;
};
