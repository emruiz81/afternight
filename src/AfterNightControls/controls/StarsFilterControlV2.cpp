// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "StarsFilterControlV2.h"

StarsFilterControlV2::StarsFilterControlV2(StarsFilterControlHandler* parent) : StarsFilterWindow(parent)
{
    m_ui = an::ui::controls::buildStarsFilterControlV2Ui(this);

    // Minimize button.. disabled for now
    // setWindowFlags(windowFlags() | Qt::WindowMinimizeButtonHint);

    INIT_D_SPINBOX_CTRL(magMinControl, m_ui.magMinValue, m_ui.magMinSlider, [this](double) { settingsUpdated(); });
    INIT_D_SPINBOX_CTRL(magMaxControl, m_ui.magMaxValue, m_ui.magMaxSlider, [this](double) { settingsUpdated(); });
    INIT_D_SPINBOX_CTRL(fwhmxMinControl, m_ui.fwhmxMinValue, m_ui.fwhmxMinSlider,
                        [this](double) { settingsUpdated(); });
    INIT_D_SPINBOX_CTRL(fwhmxMaxControl, m_ui.fwhmxMaxValue, m_ui.fwhmxMaxSlider,
                        [this](double) { settingsUpdated(); });
    INIT_D_SPINBOX_CTRL(fwhmyMinControl, m_ui.fwhmyMinValue, m_ui.fwhmyMinSlider,
                        [this](double) { settingsUpdated(); });
    INIT_D_SPINBOX_CTRL(fwhmyMaxControl, m_ui.fwhmyMaxValue, m_ui.fwhmyMaxSlider,
                        [this](double) { settingsUpdated(); });
    INIT_D_SPINBOX_CTRL(randomSelectionControl, m_ui.randomSelectionValue, m_ui.randomSelectionSlider,
                        [this](double) { settingsUpdated(); });

    Controls controls;
    controls.ranges.push_back(RangeControls{.field = an::ui::StarFilterField::Magnitude,
                                            .lower = magMinControl.get(),
                                            .upper = magMaxControl.get(),
                                            .brightnessOrder = true});
    controls.ranges.push_back(RangeControls{.field = an::ui::StarFilterField::FullWidthX,
                                            .lower = fwhmxMinControl.get(),
                                            .upper = fwhmxMaxControl.get(),
                                            .brightnessOrder = false});
    controls.ranges.push_back(RangeControls{.field = an::ui::StarFilterField::FullWidthY,
                                            .lower = fwhmyMinControl.get(),
                                            .upper = fwhmyMaxControl.get(),
                                            .brightnessOrder = false});
    controls.randomSelection = randomSelectionControl.get();
    controls.preview = m_ui.previewCheck;
    controls.includeNeighbours = m_ui.includeNeighborsCheck;
    controls.filteredStars = m_ui.filteredStarsTxt;
    controls.buttons = m_ui.buttonBox;
    attachControls(std::move(controls));
}

const an::ui::controls::StarsFilterControlV2UiElements& StarsFilterControlV2::ui() const
{
    return m_ui;
}

StarsFilterControlV2::~StarsFilterControlV2() = default;
