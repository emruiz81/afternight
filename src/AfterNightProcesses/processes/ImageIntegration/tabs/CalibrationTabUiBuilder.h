// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include <QtCore/QtCore>
#include <QtGui/QtGui>
#include <QtWidgets/QtWidgets>
#include "theme/AnDesignTokens.h"

namespace an::ui::imageintegration
{

class CalibrationTabUiElements
{
public:
    QHBoxLayout* horizontalLayout = nullptr;
    QSplitter* mainSplitter = nullptr;
    QScrollArea* scrollArea = nullptr;
    QWidget* scrollAreaWidgetContents = nullptr;
    QVBoxLayout* panelLayout = nullptr;
    QScrollArea* rightPanelScrollArea = nullptr;
    QWidget* rightPanelContents = nullptr;
    QVBoxLayout* rightPanelLayout = nullptr;

    void setupUi(QWidget* CalibrationTab)
    {
        const auto& t = an::ui::tokens();

        CalibrationTab->setObjectName("CalibrationTab");
        CalibrationTab->resize(900, 600);
        horizontalLayout = new QHBoxLayout(CalibrationTab);
        horizontalLayout->setContentsMargins(t.spacing.md, t.spacing.md, t.spacing.md, t.spacing.md);
        horizontalLayout->setObjectName("horizontalLayout");
        mainSplitter = new QSplitter(CalibrationTab);
        mainSplitter->setOrientation(Qt::Orientation::Horizontal);
        mainSplitter->setChildrenCollapsible(true);
        mainSplitter->setObjectName("mainSplitter");
        scrollArea = new QScrollArea(mainSplitter);
        scrollArea->setWidgetResizable(true);
        QSizePolicy sizePolicy(QSizePolicy::Policy::Expanding, QSizePolicy::Policy::Expanding);
        sizePolicy.setHorizontalStretch(1);
        sizePolicy.setVerticalStretch(0);
        sizePolicy.setHeightForWidth(scrollArea->sizePolicy().hasHeightForWidth());
        scrollArea->setSizePolicy(sizePolicy);
        scrollArea->setObjectName("scrollArea");
        scrollAreaWidgetContents = new QWidget();
        scrollAreaWidgetContents->setObjectName("scrollAreaWidgetContents");
        panelLayout = new QVBoxLayout(scrollAreaWidgetContents);
        panelLayout->setObjectName("panelLayout");
        auto* spacerItem = new QSpacerItem(20, 40, QSizePolicy::Policy::Minimum, QSizePolicy::Policy::Expanding);
        panelLayout->addItem(spacerItem);
        scrollArea->setWidget(scrollAreaWidgetContents);
        rightPanelScrollArea = new QScrollArea(mainSplitter);
        rightPanelScrollArea->setMinimumSize(QSize(300, 0));
        rightPanelScrollArea->setMaximumSize(QSize(400, 16777215));
        rightPanelScrollArea->setWidgetResizable(true);
        rightPanelScrollArea->setObjectName("rightPanelScrollArea");
        rightPanelContents = new QWidget();
        rightPanelContents->setObjectName("rightPanelContents");
        rightPanelLayout = new QVBoxLayout(rightPanelContents);
        rightPanelLayout->setContentsMargins(t.spacing.sm, t.spacing.sm, t.spacing.sm, t.spacing.sm);
        rightPanelLayout->setObjectName("rightPanelLayout");
        rightPanelScrollArea->setWidget(rightPanelContents);
        horizontalLayout->addWidget(mainSplitter);
        retranslateUi(CalibrationTab);
        QMetaObject::connectSlotsByName(CalibrationTab);
    }

    void retranslateUi(QWidget* CalibrationTab)
    {
        Q_UNUSED(CalibrationTab);
    }
};

inline CalibrationTabUiElements buildCalibrationTabUi(QWidget* parent)
{
    CalibrationTabUiElements ui{};
    if (!parent)
    {
        return ui;
    }

    ui.setupUi(parent);
    return ui;
}

} // namespace an::ui::imageintegration
