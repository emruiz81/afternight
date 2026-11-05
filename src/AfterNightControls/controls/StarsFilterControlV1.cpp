// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "StarsFilterControlV1.h"

StarsFilterControlV1::StarsFilterControlV1(StarsFilterControlHandler* parent) : StarsFilterWindow(parent)
{
    m_ui = an::ui::controls::buildStarsFilterControlV1Ui(this);

    // Minimize button.. disabled for now
    // setWindowFlags(windowFlags() | Qt::WindowMinimizeButtonHint);

    INIT_D_SPINBOX_CTRL(intensityMinControl, m_ui.intensityMinValue, m_ui.intensityMinSlider,
                        [this](double) { settingsUpdated(); });
    INIT_D_SPINBOX_CTRL(intensityMaxControl, m_ui.intensityMaxValue, m_ui.intensityMaxSlider,
                        [this](double) { settingsUpdated(); });
    INIT_D_SPINBOX_CTRL(qualityMinControl, m_ui.qualityMinValue, m_ui.qualityMinSlider,
                        [this](double) { settingsUpdated(); });
    INIT_D_SPINBOX_CTRL(qualityMaxControl, m_ui.qualityMaxValue, m_ui.qualityMaxSlider,
                        [this](double) { settingsUpdated(); });
    INIT_D_SPINBOX_CTRL(radiusMinControl, m_ui.radiusMinValue, m_ui.radiusMinSlider,
                        [this](double) { settingsUpdated(); });
    INIT_D_SPINBOX_CTRL(radiusMaxControl, m_ui.radiusMaxValue, m_ui.radiusMaxSlider,
                        [this](double) { settingsUpdated(); });
    INIT_D_SPINBOX_CTRL(randomSelectionControl, m_ui.randomSelectionValue, m_ui.randomSelectionSlider,
                        [this](double) { settingsUpdated(); });

    Controls controls;
    controls.ranges.push_back(RangeControls{.field = an::ui::StarFilterField::Amplitude,
                                            .lower = intensityMinControl.get(),
                                            .upper = intensityMaxControl.get(),
                                            .brightnessOrder = false});
    controls.ranges.push_back(RangeControls{.field = an::ui::StarFilterField::Circularity,
                                            .lower = qualityMinControl.get(),
                                            .upper = qualityMaxControl.get(),
                                            .brightnessOrder = false});
    controls.ranges.push_back(RangeControls{.field = an::ui::StarFilterField::Radius,
                                            .lower = radiusMinControl.get(),
                                            .upper = radiusMaxControl.get(),
                                            .brightnessOrder = false});
    controls.randomSelection = randomSelectionControl.get();
    controls.preview = m_ui.previewCheck;
    controls.includeNeighbours = m_ui.includeNeighborsCheck;
    controls.filteredStars = m_ui.filteredStarsTxt;
    controls.buttons = m_ui.buttonBox;
    attachControls(std::move(controls));
}

const an::ui::controls::StarsFilterControlV1UiElements& StarsFilterControlV1::ui() const
{
    return m_ui;
}

StarsFilterControlV1::~StarsFilterControlV1() = default;
