// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"

#include "pages/HomePage.h"

#include "controls/MainDashboard.h"

namespace an::ui::observatory
{

HomePage::HomePage(QWidget* parent) : QScrollArea(parent)
{
    setObjectName(QStringLiteral("observatoryHomePage"));
    setFrameShape(QFrame::NoFrame);
    setWidgetResizable(true);
    m_dashboard = new MainDashboard(this);
    setWidget(m_dashboard);
    m_dashboard->initialize();
}

MainDashboard* HomePage::dashboard() const noexcept
{
    return m_dashboard;
}

} // namespace an::ui::observatory
