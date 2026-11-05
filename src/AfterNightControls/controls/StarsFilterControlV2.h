// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightControlsExport.h"

#include <QtWidgets/QMainWindow>
#include "StarsFilterControlV2UiBuilder.h"

#include "StarsFilterWindow.h"
#include "controls/QSliderSpinBox.h"

// The stars-filter window of its star profiler: its layout offers magnitude and the two full widths - the instrumental
// magnitude, brightest first, and the major and minor full widths.
class AFTERNIGHTCONTROLS_API StarsFilterControlV2 : public StarsFilterWindow
{
public:
    StarsFilterControlV2(StarsFilterControlHandler* parent);

    // The window's controls, for the cases that drive it.
    [[nodiscard]] const an::ui::controls::StarsFilterControlV2UiElements& ui() const;

    ~StarsFilterControlV2();

private:
    QSliderDoubleSpinBoxUPtr magMinControl;
    QSliderDoubleSpinBoxUPtr magMaxControl;
    QSliderDoubleSpinBoxUPtr fwhmxMinControl;
    QSliderDoubleSpinBoxUPtr fwhmxMaxControl;
    QSliderDoubleSpinBoxUPtr fwhmyMinControl;
    QSliderDoubleSpinBoxUPtr fwhmyMaxControl;
    QSliderDoubleSpinBoxUPtr randomSelectionControl;

    an::ui::controls::StarsFilterControlV2UiElements m_ui;
};
