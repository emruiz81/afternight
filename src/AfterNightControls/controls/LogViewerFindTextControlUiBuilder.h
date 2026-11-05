// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include <QtCore/QVariant>
#include <QtWidgets/QApplication>
#include <QtWidgets/QCheckBox>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QSpacerItem>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QWidget>

#include "theme/AnDesignTokens.h"

namespace an::ui::controls
{

class LogViewerFindTextControlUiElements
{
public:
    QCheckBox* backward;
    QWidget* widget;
    QHBoxLayout* horizontalLayout;
    QLabel* label;
    QLineEdit* lineEdit;
    QWidget* widget1;
    QHBoxLayout* horizontalLayout_2;
    QHBoxLayout* optionsLayout;
    QCheckBox* caseSensitive;
    QCheckBox* matchWholeWord;
    QCheckBox* regularExpression;
    QWidget* widget2;
    QHBoxLayout* horizontalLayout_3;
    QHBoxLayout* actionsLayout;
    QPushButton* findNextButton;
    QPushButton* clearButton;
    QSpacerItem* horizontalSpacer;
    QPushButton* closeButton;
    QVBoxLayout* mainLayout;

    void setupUi(QWidget* LogViewerFindTextControl)
    {
        if (LogViewerFindTextControl->objectName().isEmpty())
            LogViewerFindTextControl->setObjectName("LogViewerFindTextControl");
        LogViewerFindTextControl->resize(394, 201);
        LogViewerFindTextControl->setMinimumSize(QSize(394, 201));
        LogViewerFindTextControl->setSizePolicy(QSizePolicy::Policy::Preferred, QSizePolicy::Policy::Preferred);
        const auto& t = an::ui::tokens();
        mainLayout = new QVBoxLayout(LogViewerFindTextControl);
        mainLayout->setObjectName("mainLayout");
        mainLayout->setContentsMargins(t.spacing.md, t.spacing.md, t.spacing.md, t.spacing.md);
        mainLayout->setSpacing(t.spacing.md);
        backward = new QCheckBox(LogViewerFindTextControl);
        backward->setObjectName("backward");
        widget = new QWidget(LogViewerFindTextControl);
        widget->setObjectName("widget");
        horizontalLayout = new QHBoxLayout(widget);
        horizontalLayout->setObjectName("horizontalLayout");
        horizontalLayout->setContentsMargins(0, 0, 0, 0);
        horizontalLayout->setSpacing(t.spacing.sm);
        label = new QLabel(widget);
        label->setObjectName("label");

        horizontalLayout->addWidget(label);

        lineEdit = new QLineEdit(widget);
        lineEdit->setObjectName("lineEdit");

        horizontalLayout->addWidget(lineEdit);
        mainLayout->addWidget(widget);

        widget1 = new QWidget(LogViewerFindTextControl);
        widget1->setObjectName("widget1");
        optionsLayout = new QHBoxLayout(widget1);
        optionsLayout->setObjectName("optionsLayout");
        optionsLayout->setContentsMargins(0, 0, 0, 0);
        optionsLayout->setSpacing(t.spacing.md);
        horizontalLayout_2 = optionsLayout;
        caseSensitive = new QCheckBox(widget1);
        caseSensitive->setObjectName("caseSensitive");

        optionsLayout->addWidget(caseSensitive);

        matchWholeWord = new QCheckBox(widget1);
        matchWholeWord->setObjectName("matchWholeWord");

        optionsLayout->addWidget(matchWholeWord);

        regularExpression = new QCheckBox(widget1);
        regularExpression->setObjectName("regularExpression");

        optionsLayout->addWidget(regularExpression);
        mainLayout->addWidget(widget1);
        mainLayout->addWidget(backward);

        widget2 = new QWidget(LogViewerFindTextControl);
        widget2->setObjectName("widget2");
        actionsLayout = new QHBoxLayout(widget2);
        actionsLayout->setObjectName("actionsLayout");
        actionsLayout->setContentsMargins(0, 0, 0, 0);
        actionsLayout->setSpacing(t.spacing.sm);
        horizontalLayout_3 = actionsLayout;
        findNextButton = new QPushButton(widget2);
        findNextButton->setObjectName("findNextButton");

        actionsLayout->addWidget(findNextButton);

        clearButton = new QPushButton(widget2);
        clearButton->setObjectName("clearButton");

        actionsLayout->addWidget(clearButton);

        horizontalSpacer = new QSpacerItem(40, 20, QSizePolicy::Policy::Expanding, QSizePolicy::Policy::Minimum);

        actionsLayout->addItem(horizontalSpacer);

        closeButton = new QPushButton(widget2);
        closeButton->setObjectName("closeButton");

        actionsLayout->addWidget(closeButton);
        mainLayout->addWidget(widget2);

        retranslateUi(LogViewerFindTextControl);

        QMetaObject::connectSlotsByName(LogViewerFindTextControl);
    } // setupUi

    void retranslateUi(QWidget* LogViewerFindTextControl)
    {
        LogViewerFindTextControl->setWindowTitle(
            QCoreApplication::translate("LogViewerFindTextControl", "Dialog", nullptr));
        backward->setText(QCoreApplication::translate("LogViewerFindTextControl", "Backward", nullptr));
        label->setText(QCoreApplication::translate("LogViewerFindTextControl", "Find  ", nullptr));
        caseSensitive->setText(QCoreApplication::translate("LogViewerFindTextControl", "Case sensitive", nullptr));
        matchWholeWord->setText(QCoreApplication::translate("LogViewerFindTextControl", "Match whole word", nullptr));
        regularExpression->setText(
            QCoreApplication::translate("LogViewerFindTextControl", "Regular expression", nullptr));
#if QT_CONFIG(tooltip)
        findNextButton->setToolTip(
            QCoreApplication::translate("LogViewerFindTextControl", "Find next ocurrence", nullptr));
#endif // QT_CONFIG(tooltip)
        findNextButton->setText(QCoreApplication::translate("LogViewerFindTextControl", "Find next", nullptr));
#if QT_CONFIG(tooltip)
        clearButton->setToolTip(
            QCoreApplication::translate("LogViewerFindTextControl", "Clear search results", nullptr));
#endif // QT_CONFIG(tooltip)
        clearButton->setText(QCoreApplication::translate("LogViewerFindTextControl", "Clear", nullptr));
        closeButton->setText(QCoreApplication::translate("LogViewerFindTextControl", "Close", nullptr));
    } // retranslateUi
};

inline LogViewerFindTextControlUiElements buildLogViewerFindTextControlUi(QWidget* parent)
{
    LogViewerFindTextControlUiElements ui{};
    if (!parent)
    {
        return ui;
    }

    ui.setupUi(parent);
    return ui;
}

} // namespace an::ui::controls
