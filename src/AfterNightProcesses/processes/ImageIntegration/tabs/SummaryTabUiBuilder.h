// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include <QtCore/QtCore>
#include <QtGui/QtGui>
#include <QtWidgets/QtWidgets>
#include "theme/AnDesignTokens.h"

namespace an::ui::imageintegration
{

class SummaryTabUiElements
{
public:
    QVBoxLayout* mainLayout = nullptr;
    QScrollArea* scrollArea = nullptr;
    QWidget* scrollAreaContents = nullptr;
    QVBoxLayout* scrollContentLayout = nullptr;
    QFrame* statusCardFrame = nullptr;
    QVBoxLayout* statusCardLayout = nullptr;
    QHBoxLayout* statusHeaderLayout = nullptr;
    QLabel* lblStatusIcon = nullptr;
    QVBoxLayout* statusTextLayout = nullptr;
    QLabel* lblStatusTitle = nullptr;
    QLabel* lblStatusSummary = nullptr;
    QWidget* statusIssuesWidget = nullptr;
    QVBoxLayout* statusIssuesLayout = nullptr;
    QFrame* integrationSummaryFrame = nullptr;
    QVBoxLayout* integrationSummaryLayout = nullptr;
    QLabel* lblIntegrationSummaryTitle = nullptr;
    QWidget* integrationReportWidget = nullptr;
    QGridLayout* integrationReportLayout = nullptr;
    QFrame* sessionNotesFrame = nullptr;
    QVBoxLayout* sessionNotesLayout = nullptr;
    QLabel* lblSessionNotesTitle = nullptr;
    QPlainTextEdit* txtSessionNotes = nullptr;
    QLabel* lblNotesHint = nullptr;
    QFrame* runHistoryFrame = nullptr;
    QVBoxLayout* runHistoryLayout = nullptr;
    QLabel* lblRunHistoryTitle = nullptr;
    QListView* listRunHistory = nullptr;
    QFrame* resourceStatusWidget = nullptr;
    QHBoxLayout* resourceStatusLayout = nullptr;
    QLabel* lblResourceStatusIcon = nullptr;
    QLabel* lblResourceStatus = nullptr;
    QWidget* executeFooterWidget = nullptr;
    QVBoxLayout* executeFooterLayout = nullptr;
    QPushButton* btnExecute = nullptr;

    void setupUi(QWidget* SummaryTab)
    {
        const auto& t = an::ui::tokens();
        SummaryTab->setObjectName("SummaryTab");
        mainLayout = new QVBoxLayout(SummaryTab);
        mainLayout->setContentsMargins(t.spacing.md, t.spacing.md, t.spacing.md, t.spacing.md);
        mainLayout->setSpacing(t.spacing.sm);
        mainLayout->setObjectName("mainLayout");
        scrollArea = new QScrollArea(SummaryTab);
        scrollArea->setFrameShape(QFrame::Shape::NoFrame);
        scrollArea->setWidgetResizable(true);
        scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarPolicy::ScrollBarAlwaysOff);
        scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarPolicy::ScrollBarAsNeeded);
        scrollArea->setObjectName("scrollArea");
        scrollAreaContents = new QWidget();
        scrollAreaContents->setObjectName("scrollAreaContents");
        scrollContentLayout = new QVBoxLayout(scrollAreaContents);
        scrollContentLayout->setContentsMargins(0, 0, 0, 0);
        scrollContentLayout->setSpacing(t.spacing.sm);
        scrollContentLayout->setObjectName("scrollContentLayout");
        statusCardFrame = new QFrame(scrollAreaContents);
        statusCardFrame->setFrameShape(QFrame::Shape::StyledPanel);
        statusCardFrame->setObjectName("statusCardFrame");
        statusCardLayout = new QVBoxLayout(statusCardFrame);
        statusCardLayout->setContentsMargins(t.spacing.sm, t.spacing.md, t.spacing.sm, t.spacing.sm);
        statusCardLayout->setSpacing(t.spacing.md);
        statusCardLayout->setObjectName("statusCardLayout");
        statusHeaderLayout = new QHBoxLayout();
        statusHeaderLayout->setSpacing(t.spacing.md);
        statusHeaderLayout->setObjectName("statusHeaderLayout");
        lblStatusIcon = new QLabel(statusCardFrame);
        lblStatusIcon->setText("");
        lblStatusIcon->setAlignment(Qt::AlignmentFlag::AlignCenter);
        lblStatusIcon->setMinimumWidth(t.spacing.lg * 2);
        lblStatusIcon->setMaximumWidth(t.spacing.lg * 2);
        lblStatusIcon->setMinimumHeight(t.spacing.lg * 2);
        lblStatusIcon->setObjectName("lblStatusIcon");
        statusHeaderLayout->addWidget(lblStatusIcon);
        statusTextLayout = new QVBoxLayout();
        statusTextLayout->setSpacing(t.spacing.xs);
        statusTextLayout->setObjectName("statusTextLayout");
        lblStatusTitle = new QLabel(statusCardFrame);
        lblStatusTitle->setObjectName("lblStatusTitle");
        statusTextLayout->addWidget(lblStatusTitle);
        lblStatusSummary = new QLabel(statusCardFrame);
        lblStatusSummary->setWordWrap(true);
        lblStatusSummary->setObjectName("lblStatusSummary");
        statusTextLayout->addWidget(lblStatusSummary);
        statusHeaderLayout->addLayout(statusTextLayout);
        statusCardLayout->addLayout(statusHeaderLayout);
        statusIssuesWidget = new QWidget(statusCardFrame);
        statusIssuesWidget->setVisible(false);
        statusIssuesWidget->setObjectName("statusIssuesWidget");
        statusIssuesLayout = new QVBoxLayout(statusIssuesWidget);
        statusIssuesLayout->setContentsMargins(0, 0, 0, 0);
        statusIssuesLayout->setSpacing(t.spacing.sm);
        statusIssuesLayout->setObjectName("statusIssuesLayout");
        statusCardLayout->addWidget(statusIssuesWidget);
        scrollContentLayout->addWidget(statusCardFrame);
        integrationSummaryFrame = new QFrame(scrollAreaContents);
        integrationSummaryFrame->setFrameShape(QFrame::Shape::StyledPanel);
        integrationSummaryFrame->setObjectName("integrationSummaryFrame");
        integrationSummaryLayout = new QVBoxLayout(integrationSummaryFrame);
        integrationSummaryLayout->setContentsMargins(t.spacing.sm, t.spacing.md, t.spacing.sm, t.spacing.sm);
        integrationSummaryLayout->setSpacing(t.spacing.sm);
        integrationSummaryLayout->setObjectName("integrationSummaryLayout");
        lblIntegrationSummaryTitle = new QLabel(integrationSummaryFrame);
        lblIntegrationSummaryTitle->setObjectName("lblIntegrationSummaryTitle");
        integrationSummaryLayout->addWidget(lblIntegrationSummaryTitle);
        integrationReportWidget = new QWidget(integrationSummaryFrame);
        integrationReportWidget->setObjectName("integrationReportWidget");
        integrationReportLayout = new QGridLayout(integrationReportWidget);
        integrationReportLayout->setContentsMargins(0, 0, 0, 0);
        integrationReportLayout->setVerticalSpacing(t.spacing.xs);
        integrationReportLayout->setObjectName("integrationReportLayout");
        integrationSummaryLayout->addWidget(integrationReportWidget);
        scrollContentLayout->addWidget(integrationSummaryFrame);
        sessionNotesFrame = new QFrame(scrollAreaContents);
        sessionNotesFrame->setFrameShape(QFrame::Shape::StyledPanel);
        sessionNotesFrame->setObjectName("sessionNotesFrame");
        sessionNotesLayout = new QVBoxLayout(sessionNotesFrame);
        sessionNotesLayout->setContentsMargins(t.spacing.sm, t.spacing.md, t.spacing.sm, t.spacing.sm);
        sessionNotesLayout->setSpacing(t.spacing.sm);
        sessionNotesLayout->setObjectName("sessionNotesLayout");
        lblSessionNotesTitle = new QLabel(sessionNotesFrame);
        lblSessionNotesTitle->setObjectName("lblSessionNotesTitle");
        sessionNotesLayout->addWidget(lblSessionNotesTitle);
        txtSessionNotes = new QPlainTextEdit(sessionNotesFrame);
        QSizePolicy sizePolicy(QSizePolicy::Policy::Expanding, QSizePolicy::Policy::Expanding);
        sizePolicy.setHorizontalStretch(0);
        sizePolicy.setVerticalStretch(1);
        sizePolicy.setHeightForWidth(txtSessionNotes->sizePolicy().hasHeightForWidth());
        txtSessionNotes->setSizePolicy(sizePolicy);
        txtSessionNotes->setMinimumHeight(t.spacing.xl * 3 + t.spacing.sm);
        txtSessionNotes->setObjectName("txtSessionNotes");
        sessionNotesLayout->addWidget(txtSessionNotes);
        lblNotesHint = new QLabel(sessionNotesFrame);
        lblNotesHint->setObjectName("lblNotesHint");
        sessionNotesLayout->addWidget(lblNotesHint);
        scrollContentLayout->addWidget(sessionNotesFrame);
        runHistoryFrame = new QFrame(scrollAreaContents);
        runHistoryFrame->setFrameShape(QFrame::Shape::StyledPanel);
        runHistoryFrame->setObjectName("runHistoryFrame");
        runHistoryFrame->setSizePolicy(QSizePolicy::Policy::Expanding, QSizePolicy::Policy::Expanding);
        runHistoryLayout = new QVBoxLayout(runHistoryFrame);
        runHistoryLayout->setContentsMargins(t.spacing.sm, t.spacing.md, t.spacing.sm, t.spacing.sm);
        runHistoryLayout->setSpacing(t.spacing.sm);
        runHistoryLayout->setObjectName("runHistoryLayout");
        lblRunHistoryTitle = new QLabel(runHistoryFrame);
        lblRunHistoryTitle->setObjectName("lblRunHistoryTitle");
        runHistoryLayout->addWidget(lblRunHistoryTitle);
        listRunHistory = new QListView(runHistoryFrame);
        sizePolicy = QSizePolicy(QSizePolicy::Policy::Expanding, QSizePolicy::Policy::Expanding);
        sizePolicy.setHorizontalStretch(0);
        sizePolicy.setVerticalStretch(1);
        sizePolicy.setHeightForWidth(listRunHistory->sizePolicy().hasHeightForWidth());
        listRunHistory->setSizePolicy(sizePolicy);
        listRunHistory->setMinimumHeight(t.spacing.xl * 4 + t.spacing.xs);
        listRunHistory->setVerticalScrollBarPolicy(Qt::ScrollBarPolicy::ScrollBarAsNeeded);
        listRunHistory->setUniformItemSizes(true);
        listRunHistory->setObjectName("listRunHistory");
        runHistoryLayout->addWidget(listRunHistory, 1);
        scrollContentLayout->addWidget(runHistoryFrame, 1);
        resourceStatusWidget = new QFrame(scrollAreaContents);
        resourceStatusWidget->setFrameShape(QFrame::Shape::StyledPanel);
        resourceStatusWidget->setObjectName("resourceStatusWidget");
        resourceStatusLayout = new QHBoxLayout(resourceStatusWidget);
        resourceStatusLayout->setContentsMargins(t.spacing.sm, t.spacing.sm, t.spacing.sm, t.spacing.sm);
        resourceStatusLayout->setSpacing(t.spacing.sm);
        resourceStatusLayout->setObjectName("resourceStatusLayout");
        lblResourceStatusIcon = new QLabel(resourceStatusWidget);
        lblResourceStatusIcon->setText("");
        lblResourceStatusIcon->setMinimumWidth(t.spacing.xl);
        lblResourceStatusIcon->setObjectName("lblResourceStatusIcon");
        resourceStatusLayout->addWidget(lblResourceStatusIcon);
        lblResourceStatus = new QLabel(resourceStatusWidget);
        lblResourceStatus->setObjectName("lblResourceStatus");
        resourceStatusLayout->addWidget(lblResourceStatus);
        scrollContentLayout->addWidget(resourceStatusWidget);
        scrollArea->setWidget(scrollAreaContents);
        mainLayout->addWidget(scrollArea);
        executeFooterWidget = new QWidget(SummaryTab);
        executeFooterWidget->setObjectName("executeFooterWidget");
        executeFooterLayout = new QVBoxLayout(executeFooterWidget);
        executeFooterLayout->setContentsMargins(0, 0, 0, 0);
        executeFooterLayout->setSpacing(0);
        executeFooterLayout->setObjectName("executeFooterLayout");
        btnExecute = new QPushButton(executeFooterWidget);
        btnExecute->setMinimumHeight(t.spacing.xl + t.spacing.md);
        btnExecute->setCursor(QCursor(Qt::CursorShape::PointingHandCursor));
        btnExecute->setAutoDefault(true);
        btnExecute->setDefault(true);
        btnExecute->setObjectName("btnExecute");
        executeFooterLayout->addWidget(btnExecute);
        mainLayout->addWidget(executeFooterWidget);
        retranslateUi(SummaryTab);
        QMetaObject::connectSlotsByName(SummaryTab);
    }

    void retranslateUi(QWidget* SummaryTab)
    {
        lblStatusTitle->setText(QCoreApplication::translate("SummaryTab", "Ready to Execute"));
        lblStatusSummary->setText(QCoreApplication::translate("SummaryTab", "Checking..."));
        lblIntegrationSummaryTitle->setText(QCoreApplication::translate("SummaryTab", "Integration Summary"));
        lblSessionNotesTitle->setText(QCoreApplication::translate("SummaryTab", "Session Notes"));
        txtSessionNotes->setPlaceholderText(
            QCoreApplication::translate("SummaryTab", "Add notes about this session..."));
        lblNotesHint->setText(
            QCoreApplication::translate("SummaryTab", "Set an output directory to enable session notes."));
        lblRunHistoryTitle->setText(QCoreApplication::translate("SummaryTab", "Run History"));
        lblResourceStatus->setText(QCoreApplication::translate("SummaryTab", "Disk: --"));
        btnExecute->setText(QCoreApplication::translate("SummaryTab", "Execute Integration"));
    }
};

inline SummaryTabUiElements buildSummaryTabUi(QWidget* parent)
{
    SummaryTabUiElements ui{};
    if (!parent)
    {
        return ui;
    }

    ui.setupUi(parent);
    return ui;
}

} // namespace an::ui::imageintegration
