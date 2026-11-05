// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include <QtCore/QVariant>
#include <QtWidgets/QAbstractButton>
#include <QtWidgets/QApplication>
#include <QtWidgets/QCheckBox>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QDialog>
#include <QtWidgets/QDialogButtonBox>
#include <QtWidgets/QDoubleSpinBox>
#include <QtWidgets/QGroupBox>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QSpacerItem>
#include <QtWidgets/QSpinBox>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QWidget>

#include "theme/AnDesignTokens.h"

namespace an::ui::controls
{

class ExportSequenceControlUiElements
{
public:
    QWidget* verticalLayoutWidget;
    QVBoxLayout* verticalLayout;
    QGroupBox* generalSettings;
    QVBoxLayout* SettignsLayout;
    QHBoxLayout* horizontalLayout;
    QLabel* label;
    QComboBox* outputFormatSelector;
    QSpacerItem* horizontalSpacer;
    QLabel* label_3;
    QComboBox* codecSelector;
    QHBoxLayout* horizontalLayout_2;
    QLabel* label_2;
    QLineEdit* outputFileName;
    QPushButton* outputFileBrowser;
    QLabel* label_4;
    QDoubleSpinBox* fpsValue;
    QHBoxLayout* horizontalLayout_5;
    QCheckBox* autoStretchCheckBox;
    QCheckBox* debayerCfaCheckBox;
    QGroupBox* resizeGroupBox;
    QVBoxLayout* verticalLayout_2;
    QHBoxLayout* horizontalLayout_3;
    QComboBox* resolutionSelector;
    QSpinBox* widthValue;
    QSpinBox* heightValue;
    QSpacerItem* horizontalSpacer_2;
    QHBoxLayout* horizontalLayout_4;
    QComboBox* fillMode;
    QSpacerItem* horizontalSpacer_3;
    QSpacerItem* verticalSpacer;
    QDialogButtonBox* buttonBox;

    void setupUi(QWidget* ExportSequenceControl)
    {
        if (ExportSequenceControl->objectName().isEmpty())
            ExportSequenceControl->setObjectName("ExportSequenceControl");
        ExportSequenceControl->resize(500, 380);
        ExportSequenceControl->setSizePolicy(QSizePolicy::Policy::Preferred, QSizePolicy::Policy::Preferred);
        ExportSequenceControl->setMinimumSize(QSize(500, 380));
        const auto& t = an::ui::tokens();
        auto* mainLayout = new QVBoxLayout(ExportSequenceControl);
        mainLayout->setObjectName("mainLayout");
        mainLayout->setContentsMargins(t.spacing.md, t.spacing.md, t.spacing.md, t.spacing.md);
        mainLayout->setSpacing(t.spacing.md);
        verticalLayoutWidget = new QWidget(ExportSequenceControl);
        verticalLayoutWidget->setObjectName("verticalLayoutWidget");
        mainLayout->addWidget(verticalLayoutWidget);
        verticalLayout = new QVBoxLayout(verticalLayoutWidget);
        verticalLayout->setObjectName("verticalLayout");
        verticalLayout->setContentsMargins(t.spacing.xs, t.spacing.xs, t.spacing.xs, t.spacing.xs);
        generalSettings = new QGroupBox(verticalLayoutWidget);
        generalSettings->setObjectName("generalSettings");
        SettignsLayout = new QVBoxLayout(generalSettings);
        SettignsLayout->setObjectName("SettignsLayout");
        SettignsLayout->setContentsMargins(-1, t.spacing.md, -1, -1);
        horizontalLayout = new QHBoxLayout();
        horizontalLayout->setObjectName("horizontalLayout");
        label = new QLabel(generalSettings);
        label->setObjectName("label");
        label->setMinimumSize(QSize(100, 0));

        horizontalLayout->addWidget(label);

        outputFormatSelector = new QComboBox(generalSettings);
        outputFormatSelector->setObjectName("outputFormatSelector");
        outputFormatSelector->setMinimumSize(QSize(100, 0));

        horizontalLayout->addWidget(outputFormatSelector);

        horizontalSpacer = new QSpacerItem(40, 20, QSizePolicy::Policy::Expanding, QSizePolicy::Policy::Minimum);

        horizontalLayout->addItem(horizontalSpacer);

        label_3 = new QLabel(generalSettings);
        label_3->setObjectName("label_3");
        label_3->setMinimumSize(QSize(0, 0));
        label_3->setMaximumSize(QSize(50, 16777215));

        horizontalLayout->addWidget(label_3);

        codecSelector = new QComboBox(generalSettings);
        codecSelector->setObjectName("codecSelector");
        codecSelector->setMinimumSize(QSize(100, 0));

        horizontalLayout->addWidget(codecSelector);

        SettignsLayout->addLayout(horizontalLayout);

        horizontalLayout_2 = new QHBoxLayout();
        horizontalLayout_2->setObjectName("horizontalLayout_2");
        label_2 = new QLabel(generalSettings);
        label_2->setObjectName("label_2");
        label_2->setMinimumSize(QSize(100, 0));

        horizontalLayout_2->addWidget(label_2);

        outputFileName = new QLineEdit(generalSettings);
        outputFileName->setObjectName("outputFileName");
        outputFileName->setReadOnly(true);

        horizontalLayout_2->addWidget(outputFileName);

        outputFileBrowser = new QPushButton(generalSettings);
        outputFileBrowser->setObjectName("outputFileBrowser");
        outputFileBrowser->setMaximumSize(QSize(30, 16777215));

        horizontalLayout_2->addWidget(outputFileBrowser);

        label_4 = new QLabel(generalSettings);
        label_4->setObjectName("label_4");

        horizontalLayout_2->addWidget(label_4);

        fpsValue = new QDoubleSpinBox(generalSettings);
        fpsValue->setObjectName("fpsValue");
        fpsValue->setMinimumSize(QSize(60, 0));
        fpsValue->setMinimum(1.000000000000000);
        fpsValue->setMaximum(60.000000000000000);
        fpsValue->setSingleStep(0.300000000000000);

        horizontalLayout_2->addWidget(fpsValue);

        SettignsLayout->addLayout(horizontalLayout_2);

        horizontalLayout_5 = new QHBoxLayout();
        horizontalLayout_5->setObjectName("horizontalLayout_5");
        autoStretchCheckBox = new QCheckBox(generalSettings);
        autoStretchCheckBox->setObjectName("autoStretchCheckBox");

        horizontalLayout_5->addWidget(autoStretchCheckBox);

        debayerCfaCheckBox = new QCheckBox(generalSettings);
        debayerCfaCheckBox->setObjectName("debayerCfaCheckBox");

        horizontalLayout_5->addWidget(debayerCfaCheckBox);

        SettignsLayout->addLayout(horizontalLayout_5);

        resizeGroupBox = new QGroupBox(generalSettings);
        resizeGroupBox->setObjectName("resizeGroupBox");
        resizeGroupBox->setMinimumSize(QSize(0, 120));
        resizeGroupBox->setCheckable(true);
        resizeGroupBox->setChecked(false);
        verticalLayout_2 = new QVBoxLayout(resizeGroupBox);
        verticalLayout_2->setObjectName("verticalLayout_2");
        horizontalLayout_3 = new QHBoxLayout();
        horizontalLayout_3->setObjectName("horizontalLayout_3");
        resolutionSelector = new QComboBox(resizeGroupBox);
        resolutionSelector->setObjectName("resolutionSelector");
        resolutionSelector->setMinimumSize(QSize(115, 0));
        resolutionSelector->setMaximumSize(QSize(115, 16777215));

        horizontalLayout_3->addWidget(resolutionSelector);

        widthValue = new QSpinBox(resizeGroupBox);
        widthValue->setObjectName("widthValue");
        widthValue->setMinimumSize(QSize(80, 0));
        widthValue->setMaximumSize(QSize(80, 16777215));

        horizontalLayout_3->addWidget(widthValue);

        heightValue = new QSpinBox(resizeGroupBox);
        heightValue->setObjectName("heightValue");
        heightValue->setMinimumSize(QSize(80, 0));
        heightValue->setMaximumSize(QSize(80, 16777215));

        horizontalLayout_3->addWidget(heightValue);

        horizontalSpacer_2 = new QSpacerItem(40, 20, QSizePolicy::Policy::Expanding, QSizePolicy::Policy::Minimum);

        horizontalLayout_3->addItem(horizontalSpacer_2);

        verticalLayout_2->addLayout(horizontalLayout_3);

        horizontalLayout_4 = new QHBoxLayout();
        horizontalLayout_4->setObjectName("horizontalLayout_4");
        fillMode = new QComboBox(resizeGroupBox);
        fillMode->setObjectName("fillMode");
        fillMode->setMinimumSize(QSize(115, 0));
        fillMode->setMaximumSize(QSize(16777215, 16777215));

        horizontalLayout_4->addWidget(fillMode);

        horizontalSpacer_3 = new QSpacerItem(40, 20, QSizePolicy::Policy::Expanding, QSizePolicy::Policy::Minimum);

        horizontalLayout_4->addItem(horizontalSpacer_3);

        verticalLayout_2->addLayout(horizontalLayout_4);

        SettignsLayout->addWidget(resizeGroupBox);

        verticalSpacer = new QSpacerItem(20, 40, QSizePolicy::Policy::Minimum, QSizePolicy::Policy::Expanding);

        SettignsLayout->addItem(verticalSpacer);

        verticalLayout->addWidget(generalSettings);

        buttonBox = new QDialogButtonBox(verticalLayoutWidget);
        buttonBox->setObjectName("buttonBox");
        buttonBox->setStandardButtons(QDialogButtonBox::StandardButton::Cancel | QDialogButtonBox::StandardButton::Ok |
                                      QDialogButtonBox::StandardButton::Reset);

        verticalLayout->addWidget(buttonBox);

        retranslateUi(ExportSequenceControl);

        QMetaObject::connectSlotsByName(ExportSequenceControl);
    } // setupUi

    void retranslateUi(QWidget* ExportSequenceControl)
    {
        ExportSequenceControl->setWindowTitle(QCoreApplication::translate("ExportSequenceControl", "Dialog", nullptr));
        generalSettings->setTitle(QString());
        label->setText(QCoreApplication::translate("ExportSequenceControl", "Output Format", nullptr));
        label_3->setText(QCoreApplication::translate("ExportSequenceControl", "Codec", nullptr));
        label_2->setText(QCoreApplication::translate("ExportSequenceControl", "Output File Name", nullptr));
        outputFileBrowser->setText(QCoreApplication::translate("ExportSequenceControl", "...", nullptr));
        label_4->setText(QCoreApplication::translate("ExportSequenceControl", "   FPS", nullptr));
        autoStretchCheckBox->setText(QCoreApplication::translate("ExportSequenceControl", "Autostretch", nullptr));
        debayerCfaCheckBox->setText(QCoreApplication::translate("ExportSequenceControl", "Debayer CFA", nullptr));
        resizeGroupBox->setTitle(QCoreApplication::translate("ExportSequenceControl", "Resize", nullptr));
        widthValue->setSuffix(QString());
    } // retranslateUi
};

inline ExportSequenceControlUiElements buildExportSequenceControlUi(QWidget* parent)
{
    ExportSequenceControlUiElements ui{};
    if (!parent)
    {
        return ui;
    }

    ui.setupUi(parent);
    return ui;
}

} // namespace an::ui::controls
