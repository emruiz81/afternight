// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include <QtCore/QVariant>
#include <QtGui/QIcon>
#include <QtWidgets/QAbstractButton>
#include <QtWidgets/QApplication>
#include <QtWidgets/QCheckBox>
#include <QtWidgets/QDialogButtonBox>
#include <QtWidgets/QFontComboBox>
#include <QtWidgets/QGridLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QPlainTextEdit>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QSpacerItem>
#include <QtWidgets/QSpinBox>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QWidget>

#include "theme/AnDesignTokens.h"

namespace an::ui::controls
{

class LogViewerSettingsControlUiElements
{
public:
    QWidget* verticalLayoutWidget;
    QVBoxLayout* verticalLayout;
    QGridLayout* gridLayout;
    QLineEdit* fontHighlightColorEdit_1;
    QPushButton* fontHighlightColorPickerButton;
    QLabel* label_3;
    QLineEdit* fontColorEdit_1;
    QLabel* label_2;
    QPushButton* fontColorPickerButton;
    QLineEdit* backgroundHighlightColorEdit_1;
    QSpinBox* maxLinesValue;
    QLabel* label_7;
    QLabel* label;
    QCheckBox* bold;
    QPushButton* backgroundColorPickerButton;
    QLabel* label_4;
    QLabel* label_6;
    QSpinBox* defaultFontSizeValue;
    QPushButton* backgroundHighlightColorPickerButton;
    QFontComboBox* fontSelector;
    QLineEdit* backgroundColorEdit_1;
    QLabel* label_5;
    QCheckBox* italic;
    QVBoxLayout* verticalLayout_2;
    QPlainTextEdit* textEdit;
    QSpacerItem* verticalSpacer;
    QDialogButtonBox* buttonBox;

    void setupUi(QWidget* LogViewerSettingsControl)
    {
        if (LogViewerSettingsControl->objectName().isEmpty())
            LogViewerSettingsControl->setObjectName("LogViewerSettingsControl");
        LogViewerSettingsControl->resize(700, 400);
        LogViewerSettingsControl->setMinimumSize(QSize(700, 400));
        LogViewerSettingsControl->setSizePolicy(QSizePolicy::Policy::Preferred, QSizePolicy::Policy::Preferred);
        const auto& t = an::ui::tokens();
        auto* mainLayout = new QVBoxLayout(LogViewerSettingsControl);
        mainLayout->setObjectName("mainLayout");
        mainLayout->setContentsMargins(t.spacing.md, t.spacing.md, t.spacing.md, t.spacing.md);
        mainLayout->setSpacing(0);
        verticalLayoutWidget = new QWidget(LogViewerSettingsControl);
        verticalLayoutWidget->setObjectName("verticalLayoutWidget");
        mainLayout->addWidget(verticalLayoutWidget);
        verticalLayout = new QVBoxLayout(verticalLayoutWidget);
        verticalLayout->setObjectName("verticalLayout");
        verticalLayout->setContentsMargins(t.spacing.md, t.spacing.md, t.spacing.md, t.spacing.md);
        gridLayout = new QGridLayout();
        gridLayout->setObjectName("gridLayout");
        fontHighlightColorEdit_1 = new QLineEdit(verticalLayoutWidget);
        fontHighlightColorEdit_1->setObjectName("fontHighlightColorEdit_1");

        gridLayout->addWidget(fontHighlightColorEdit_1, 2, 1, 1, 1);

        fontHighlightColorPickerButton = new QPushButton(verticalLayoutWidget);
        fontHighlightColorPickerButton->setObjectName("fontHighlightColorPickerButton");

        gridLayout->addWidget(fontHighlightColorPickerButton, 2, 2, 1, 1);

        label_3 = new QLabel(verticalLayoutWidget);
        label_3->setObjectName("label_3");

        gridLayout->addWidget(label_3, 0, 3, 1, 1);

        fontColorEdit_1 = new QLineEdit(verticalLayoutWidget);
        fontColorEdit_1->setObjectName("fontColorEdit_1");
        fontColorEdit_1->setMinimumSize(QSize(0, 0));

        gridLayout->addWidget(fontColorEdit_1, 0, 1, 1, 1);

        label_2 = new QLabel(verticalLayoutWidget);
        label_2->setObjectName("label_2");

        gridLayout->addWidget(label_2, 1, 0, 1, 1);

        fontColorPickerButton = new QPushButton(verticalLayoutWidget);
        fontColorPickerButton->setObjectName("fontColorPickerButton");

        gridLayout->addWidget(fontColorPickerButton, 0, 2, 1, 1);

        backgroundHighlightColorEdit_1 = new QLineEdit(verticalLayoutWidget);
        backgroundHighlightColorEdit_1->setObjectName("backgroundHighlightColorEdit_1");

        gridLayout->addWidget(backgroundHighlightColorEdit_1, 3, 1, 1, 1);

        maxLinesValue = new QSpinBox(verticalLayoutWidget);
        maxLinesValue->setObjectName("maxLinesValue");
        maxLinesValue->setMinimum(50);
        maxLinesValue->setMaximum(50000);
        maxLinesValue->setValue(10000);

        gridLayout->addWidget(maxLinesValue, 5, 1, 1, 1);

        label_7 = new QLabel(verticalLayoutWidget);
        label_7->setObjectName("label_7");

        gridLayout->addWidget(label_7, 4, 0, 1, 1);

        label = new QLabel(verticalLayoutWidget);
        label->setObjectName("label");

        gridLayout->addWidget(label, 0, 0, 1, 1);

        bold = new QCheckBox(verticalLayoutWidget);
        bold->setObjectName("bold");

        gridLayout->addWidget(bold, 4, 3, 1, 1);

        backgroundColorPickerButton = new QPushButton(verticalLayoutWidget);
        backgroundColorPickerButton->setObjectName("backgroundColorPickerButton");

        gridLayout->addWidget(backgroundColorPickerButton, 1, 2, 1, 1);

        label_4 = new QLabel(verticalLayoutWidget);
        label_4->setObjectName("label_4");

        gridLayout->addWidget(label_4, 5, 0, 1, 1);

        label_6 = new QLabel(verticalLayoutWidget);
        label_6->setObjectName("label_6");

        gridLayout->addWidget(label_6, 3, 0, 1, 1);

        defaultFontSizeValue = new QSpinBox(verticalLayoutWidget);
        defaultFontSizeValue->setObjectName("defaultFontSizeValue");
        defaultFontSizeValue->setMinimum(4);
        defaultFontSizeValue->setMaximum(30);
        defaultFontSizeValue->setValue(12);

        gridLayout->addWidget(defaultFontSizeValue, 0, 5, 1, 1);

        backgroundHighlightColorPickerButton = new QPushButton(verticalLayoutWidget);
        backgroundHighlightColorPickerButton->setObjectName("backgroundHighlightColorPickerButton");

        gridLayout->addWidget(backgroundHighlightColorPickerButton, 3, 2, 1, 1);

        fontSelector = new QFontComboBox(verticalLayoutWidget);
        fontSelector->setObjectName("fontSelector");

        gridLayout->addWidget(fontSelector, 4, 1, 1, 1);

        backgroundColorEdit_1 = new QLineEdit(verticalLayoutWidget);
        backgroundColorEdit_1->setObjectName("backgroundColorEdit_1");

        gridLayout->addWidget(backgroundColorEdit_1, 1, 1, 1, 1);

        label_5 = new QLabel(verticalLayoutWidget);
        label_5->setObjectName("label_5");

        gridLayout->addWidget(label_5, 2, 0, 1, 1);

        italic = new QCheckBox(verticalLayoutWidget);
        italic->setObjectName("italic");

        gridLayout->addWidget(italic, 4, 4, 1, 1);

        verticalLayout->addLayout(gridLayout);

        verticalLayout_2 = new QVBoxLayout();
        verticalLayout_2->setObjectName("verticalLayout_2");
        textEdit = new QPlainTextEdit(verticalLayoutWidget);
        textEdit->setObjectName("textEdit");

        verticalLayout_2->addWidget(textEdit);

        verticalLayout->addLayout(verticalLayout_2);

        verticalSpacer = new QSpacerItem(20, 40, QSizePolicy::Policy::Minimum, QSizePolicy::Policy::Expanding);

        verticalLayout->addItem(verticalSpacer);

        buttonBox = new QDialogButtonBox(verticalLayoutWidget);
        buttonBox->setObjectName("buttonBox");
        buttonBox->setOrientation(Qt::Horizontal);
        buttonBox->setStandardButtons(QDialogButtonBox::Apply | QDialogButtonBox::Cancel | QDialogButtonBox::Ok |
                                      QDialogButtonBox::Reset);

        verticalLayout->addWidget(buttonBox);

        verticalLayout->setStretch(0, 2);
        verticalLayout->setStretch(2, 1);

        retranslateUi(LogViewerSettingsControl);

        QMetaObject::connectSlotsByName(LogViewerSettingsControl);
    } // setupUi

    void retranslateUi(QWidget* LogViewerSettingsControl)
    {
        LogViewerSettingsControl->setWindowTitle(
            QCoreApplication::translate("LogViewerSettingsControl", "Dialog", nullptr));
#if QT_CONFIG(tooltip)
        fontHighlightColorPickerButton->setToolTip(
            QCoreApplication::translate("LogViewerSettingsControl", "Select color", nullptr));
#endif // QT_CONFIG(tooltip)
        fontHighlightColorPickerButton->setText(QString());
        label_3->setText(QCoreApplication::translate("LogViewerSettingsControl", "Default font Size", nullptr));
        label_2->setText(QCoreApplication::translate("LogViewerSettingsControl", "Background color", nullptr));
#if QT_CONFIG(tooltip)
        fontColorPickerButton->setToolTip(
            QCoreApplication::translate("LogViewerSettingsControl", "Select color", nullptr));
#endif // QT_CONFIG(tooltip)
        fontColorPickerButton->setText(QString());
        label_7->setText(QCoreApplication::translate("LogViewerSettingsControl", "Font", nullptr));
        label->setText(QCoreApplication::translate("LogViewerSettingsControl", "Font color", nullptr));
        bold->setText(QCoreApplication::translate("LogViewerSettingsControl", "Bold", nullptr));
#if QT_CONFIG(tooltip)
        backgroundColorPickerButton->setToolTip(
            QCoreApplication::translate("LogViewerSettingsControl", "Select color", nullptr));
#endif // QT_CONFIG(tooltip)
        backgroundColorPickerButton->setText(QString());
        label_4->setText(QCoreApplication::translate("LogViewerSettingsControl", "Max lines", nullptr));
        label_6->setText(QCoreApplication::translate("LogViewerSettingsControl", "Background Highlight", nullptr));
        defaultFontSizeValue->setSuffix(QCoreApplication::translate("LogViewerSettingsControl", "pt", nullptr));
#if QT_CONFIG(tooltip)
        backgroundHighlightColorPickerButton->setToolTip(
            QCoreApplication::translate("LogViewerSettingsControl", "Select color", nullptr));
#endif // QT_CONFIG(tooltip)
        backgroundHighlightColorPickerButton->setText(QString());
        label_5->setText(QCoreApplication::translate("LogViewerSettingsControl", "Font Highlight", nullptr));
        italic->setText(QCoreApplication::translate("LogViewerSettingsControl", "Italic", nullptr));
    } // retranslateUi
};

inline LogViewerSettingsControlUiElements buildLogViewerSettingsControlUi(QWidget* parent)
{
    LogViewerSettingsControlUiElements ui{};
    if (!parent)
    {
        return ui;
    }

    ui.setupUi(parent);
    return ui;
}

} // namespace an::ui::controls
