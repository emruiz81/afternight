// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include <QtWidgets/QApplication>
#include <QtWidgets/QDialog>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QSpacerItem>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QWidget>

#include "components/AnProgressCard.h"
#include "theme/AnDesignTokens.h"

namespace an::ui::controls
{

/// UI elements for ProgressDialogControl, modernized to use AnProgressCard.
class ProgressDialogControlUiElements
{
public:
    QVBoxLayout* verticalLayout = nullptr;
    /// Primary progress card (always visible): task label + progress bar + ETA.
    an::ui::AnProgressCard* progressCard = nullptr;
    /// Sequence progress card (two-step mode only): overall sequence progress.
    an::ui::AnProgressCard* sequenceProgressCard = nullptr;
    QPushButton* cancelButton = nullptr;

    void setupUi(QWidget* parent)
    {
        const auto& t = an::ui::tokens();
        verticalLayout = new QVBoxLayout(parent);
        verticalLayout->setContentsMargins(t.spacing.lg, t.spacing.lg, t.spacing.lg, t.spacing.lg);
        verticalLayout->setSpacing(t.spacing.md);

        // Sequence (overall) progress — two-step mode only; hidden until shown
        sequenceProgressCard =
            new an::ui::AnProgressCard(QCoreApplication::translate("ProgressDialogControl", "Overall"), parent);
        sequenceProgressCard->setObjectName(QStringLiteral("sequenceProgressCard"));
        verticalLayout->addWidget(sequenceProgressCard);

        // Primary (current operation) progress
        progressCard =
            new an::ui::AnProgressCard(QCoreApplication::translate("ProgressDialogControl", "Processing..."), parent);
        progressCard->setObjectName(QStringLiteral("progressCard"));
        verticalLayout->addWidget(progressCard);

        verticalLayout->addStretch(1);

        // Cancel button — right-aligned
        auto* btnRow = new QHBoxLayout();
        btnRow->addStretch(1);
        cancelButton = new QPushButton(QCoreApplication::translate("ProgressDialogControl", "Cancel"), parent);
        cancelButton->setObjectName(QStringLiteral("cancelButton"));
        btnRow->addWidget(cancelButton);
        verticalLayout->addLayout(btnRow);
    }
};

inline ProgressDialogControlUiElements buildProgressDialogControlUi(QWidget* parent)
{
    ProgressDialogControlUiElements ui{};
    if (!parent)
    {
        return ui;
    }

    ui.setupUi(parent);
    return ui;
}

} // namespace an::ui::controls
