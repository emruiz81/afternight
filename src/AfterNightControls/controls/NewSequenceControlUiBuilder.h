// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include <QtCore/QVariant>
#include <QtGui/QIcon>
#include <QtWidgets/QAbstractButton>
#include <QtWidgets/QApplication>
#include <QtWidgets/QCheckBox>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QDialogButtonBox>
#include <QtWidgets/QDoubleSpinBox>
#include <QtWidgets/QGroupBox>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QHeaderView>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QSpacerItem>
#include <QtWidgets/QTabBar>
#include <QtWidgets/QTabWidget>
#include <QtWidgets/QTextEdit>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QWidget>

#include "controls/ProcessTabStyling.h"
#include "theme/AnDesignTokens.h"

#include "widgets/HistogramWidget.h"
#include "widgets/ImagePreviewWidget.h"
#include "widgets/TableWidget.h"

namespace an::ui::controls
{

class NewSequenceControlUiElements
{
public:
    QWidget* verticalLayoutWidget_2;
    QVBoxLayout* verticalLayout_Main;
    QHBoxLayout* horizontalLayout_4;
    QTabWidget* tabWidget;
    QWidget* tab;
    QWidget* verticalLayoutWidget;
    QVBoxLayout* verticalLayout_Tab1;
    QHBoxLayout* tableHorizontalLayout;
    QVBoxLayout* verticalLayout_2;
    TableWidget* tableWidget;
    QHBoxLayout* horizontalLayout_3;
    QPushButton* addFiles;
    QPushButton* removeSelected;
    QPushButton* moveUpButton;
    QPushButton* moveDownButton;
    QLabel* label_27;
    QLabel* currentFrameLabel;
    QLabel* label;
    QLabel* totalFramesLabel;
    QSpacerItem* horizontalSpacer_2;
    QPushButton* clearAll;
    QVBoxLayout* verticalLayout;
    ImagePreviewWidget* imagePreviewWidget;
    HistogramWidget* histogramWidget;
    QTextEdit* imageSummary;
    QWidget* tab_2;
    QWidget* verticalLayoutWidget_3;
    QVBoxLayout* verticalLayout_Tab2;
    QGroupBox* inputSettingsGroupBox;
    QWidget* verticalLayoutWidget_4;
    QVBoxLayout* inputSettingsVerticalLayout;
    QHBoxLayout* horizontalLayout;
    QCheckBox* debayerCFA;
    QLabel* label_2;
    QComboBox* bayerPatternSelector;
    QSpacerItem* horizontalSpacer;
    QSpacerItem* verticalSpacer_2;
    QGroupBox* outputSettingsGroupBox;
    QWidget* verticalLayoutWidget_5;
    QVBoxLayout* outputSettingsVerticalLayout;
    QHBoxLayout* horizontalLayout_5;
    QLabel* label_3;
    QComboBox* sequenceTypeSelector;
    QSpacerItem* horizontalSpacer_3;
    QHBoxLayout* horizontalLayout_6;
    QLabel* label_4;
    QDoubleSpinBox* fpsValue;
    QSpacerItem* horizontalSpacer_4;
    QHBoxLayout* horizontalLayout_7;
    QLabel* label_5;
    QLineEdit* sequenceNameEdit;
    QSpacerItem* horizontalSpacer_5;
    QSpacerItem* verticalSpacer_3;
    QSpacerItem* verticalSpacer;
    QHBoxLayout* horizontalLayout_2;
    QDialogButtonBox* buttonBox;

    void setupUi(QWidget* NewSequenceControl)
    {
        if (NewSequenceControl->objectName().isEmpty())
            NewSequenceControl->setObjectName("NewSequenceControl");
        const auto& t = an::ui::tokens();
        NewSequenceControl->setWindowModality(Qt::NonModal);
        NewSequenceControl->resize(1404, 946);
        NewSequenceControl->setMinimumSize(QSize(530, 540));
        NewSequenceControl->setMaximumSize(QSize(16777215, 16777215));
        NewSequenceControl->setProperty("sizeGripEnabled", QVariant(true));
        auto* rootLayout = new QVBoxLayout(NewSequenceControl);
        rootLayout->setObjectName("rootLayout");
        rootLayout->setContentsMargins(0, 0, 0, 0);
        rootLayout->setSpacing(0);
        verticalLayoutWidget_2 = new QWidget(NewSequenceControl);
        verticalLayoutWidget_2->setObjectName("verticalLayoutWidget_2");
        rootLayout->addWidget(verticalLayoutWidget_2);
        verticalLayout_Main = new QVBoxLayout(verticalLayoutWidget_2);
        verticalLayout_Main->setObjectName("verticalLayout_Main");
        verticalLayout_Main->setContentsMargins(t.spacing.md, 0, t.spacing.md, 0);
        horizontalLayout_4 = new QHBoxLayout();
        horizontalLayout_4->setObjectName("horizontalLayout_4");
        tabWidget = new QTabWidget(verticalLayoutWidget_2);
        tabWidget->setObjectName("tabWidget");
        an::ui::processes::applyStandardProcessTabBarSizing(tabWidget);
        tab = new QWidget();
        tab->setObjectName("tab");
        auto* tab1Layout = new QVBoxLayout(tab);
        tab1Layout->setObjectName("tab1Layout");
        tab1Layout->setContentsMargins(0, 0, 0, 0);
        tab1Layout->setSpacing(0);
        verticalLayoutWidget = new QWidget(tab);
        verticalLayoutWidget->setObjectName("verticalLayoutWidget");
        tab1Layout->addWidget(verticalLayoutWidget);
        verticalLayout_Tab1 = new QVBoxLayout(verticalLayoutWidget);
        verticalLayout_Tab1->setSpacing(t.spacing.md);
        verticalLayout_Tab1->setObjectName("verticalLayout_Tab1");
        verticalLayout_Tab1->setContentsMargins(t.spacing.md, t.spacing.md, t.spacing.sm, t.spacing.md);
        tableHorizontalLayout = new QHBoxLayout();
        tableHorizontalLayout->setObjectName("tableHorizontalLayout");
        verticalLayout_2 = new QVBoxLayout();
        verticalLayout_2->setObjectName("verticalLayout_2");
        tableWidget = new TableWidget(verticalLayoutWidget);
        tableWidget->setObjectName("tableWidget");

        verticalLayout_2->addWidget(tableWidget);

        horizontalLayout_3 = new QHBoxLayout();
        horizontalLayout_3->setObjectName("horizontalLayout_3");
        addFiles = new QPushButton(verticalLayoutWidget);
        addFiles->setObjectName("addFiles");
        addFiles->setProperty("anButtonVariant", QStringLiteral("secondary"));

        horizontalLayout_3->addWidget(addFiles);

        removeSelected = new QPushButton(verticalLayoutWidget);
        removeSelected->setObjectName("removeSelected");
        removeSelected->setProperty("anButtonVariant", QStringLiteral("secondary"));

        horizontalLayout_3->addWidget(removeSelected);

        moveUpButton = new QPushButton(verticalLayoutWidget);
        moveUpButton->setObjectName("moveUpButton");
        moveUpButton->setProperty("anButtonVariant", QStringLiteral("secondary"));

        horizontalLayout_3->addWidget(moveUpButton);

        moveDownButton = new QPushButton(verticalLayoutWidget);
        moveDownButton->setObjectName("moveDownButton");
        moveDownButton->setProperty("anButtonVariant", QStringLiteral("secondary"));

        horizontalLayout_3->addWidget(moveDownButton);

        label_27 = new QLabel(verticalLayoutWidget);
        label_27->setObjectName("label_27");

        horizontalLayout_3->addWidget(label_27);

        currentFrameLabel = new QLabel(verticalLayoutWidget);
        currentFrameLabel->setObjectName("currentFrameLabel");
        currentFrameLabel->setMinimumSize(QSize(45, 0));

        horizontalLayout_3->addWidget(currentFrameLabel);

        label = new QLabel(verticalLayoutWidget);
        label->setObjectName("label");

        horizontalLayout_3->addWidget(label);

        totalFramesLabel = new QLabel(verticalLayoutWidget);
        totalFramesLabel->setObjectName("totalFramesLabel");
        totalFramesLabel->setMinimumSize(QSize(45, 0));

        horizontalLayout_3->addWidget(totalFramesLabel);

        horizontalSpacer_2 = new QSpacerItem(40, 20, QSizePolicy::Policy::Expanding, QSizePolicy::Policy::Minimum);

        horizontalLayout_3->addItem(horizontalSpacer_2);

        clearAll = new QPushButton(verticalLayoutWidget);
        clearAll->setObjectName("clearAll");
        clearAll->setProperty("anButtonVariant", QStringLiteral("danger"));

        horizontalLayout_3->addWidget(clearAll);

        verticalLayout_2->addLayout(horizontalLayout_3);

        tableHorizontalLayout->addLayout(verticalLayout_2);

        verticalLayout = new QVBoxLayout();
        verticalLayout->setObjectName("verticalLayout");
        imagePreviewWidget = new ImagePreviewWidget(verticalLayoutWidget);
        imagePreviewWidget->setObjectName("imagePreviewWidget");

        verticalLayout->addWidget(imagePreviewWidget);

        histogramWidget = new HistogramWidget(verticalLayoutWidget);
        histogramWidget->setObjectName("histogramWidget");

        verticalLayout->addWidget(histogramWidget);

        imageSummary = new QTextEdit(verticalLayoutWidget);
        imageSummary->setObjectName("imageSummary");
        imageSummary->setReadOnly(true);

        verticalLayout->addWidget(imageSummary);

        verticalLayout->setStretch(0, 4);
        verticalLayout->setStretch(1, 2);
        verticalLayout->setStretch(2, 2);

        tableHorizontalLayout->addLayout(verticalLayout);

        tableHorizontalLayout->setStretch(0, 4);
        tableHorizontalLayout->setStretch(1, 3);

        verticalLayout_Tab1->addLayout(tableHorizontalLayout);

        tabWidget->addTab(tab, QString());
        tab_2 = new QWidget();
        tab_2->setObjectName("tab_2");
        auto* tab2Layout = new QVBoxLayout(tab_2);
        tab2Layout->setObjectName("tab2Layout");
        tab2Layout->setContentsMargins(t.spacing.md, t.spacing.md, t.spacing.md, t.spacing.md);
        tab2Layout->setSpacing(0);
        verticalLayoutWidget_3 = new QWidget(tab_2);
        verticalLayoutWidget_3->setObjectName("verticalLayoutWidget_3");
        tab2Layout->addWidget(verticalLayoutWidget_3);
        verticalLayout_Tab2 = new QVBoxLayout(verticalLayoutWidget_3);
        verticalLayout_Tab2->setObjectName("verticalLayout_Tab2");
        verticalLayout_Tab2->setContentsMargins(t.spacing.md, t.spacing.md, t.spacing.md, t.spacing.md);
        inputSettingsGroupBox = new QGroupBox(verticalLayoutWidget_3);
        inputSettingsGroupBox->setObjectName("inputSettingsGroupBox");
        inputSettingsGroupBox->setMinimumSize(QSize(0, 100));
        inputSettingsGroupBox->setFlat(true);
        inputSettingsGroupBox->setProperty("anGroupVariant", QStringLiteral("flat"));
        verticalLayoutWidget_4 = inputSettingsGroupBox;
        inputSettingsVerticalLayout = new QVBoxLayout(inputSettingsGroupBox);
        inputSettingsVerticalLayout->setObjectName("inputSettingsVerticalLayout");
        inputSettingsVerticalLayout->setContentsMargins(t.spacing.sm, t.spacing.sm, t.spacing.sm, t.spacing.sm);
        horizontalLayout = new QHBoxLayout();
        horizontalLayout->setObjectName("horizontalLayout");
        debayerCFA = new QCheckBox(inputSettingsGroupBox);
        debayerCFA->setObjectName("debayerCFA");
        debayerCFA->setChecked(true);

        horizontalLayout->addWidget(debayerCFA);

        label_2 = new QLabel(inputSettingsGroupBox);
        label_2->setObjectName("label_2");

        horizontalLayout->addWidget(label_2);

        bayerPatternSelector = new QComboBox(inputSettingsGroupBox);
        bayerPatternSelector->setObjectName("bayerPatternSelector");

        horizontalLayout->addWidget(bayerPatternSelector);

        horizontalSpacer = new QSpacerItem(40, 20, QSizePolicy::Policy::Expanding, QSizePolicy::Policy::Minimum);

        horizontalLayout->addItem(horizontalSpacer);

        horizontalLayout->setStretch(3, 4);

        inputSettingsVerticalLayout->addLayout(horizontalLayout);

        verticalSpacer_2 = new QSpacerItem(20, 40, QSizePolicy::Policy::Minimum, QSizePolicy::Policy::Expanding);

        inputSettingsVerticalLayout->addItem(verticalSpacer_2);

        verticalLayout_Tab2->addWidget(inputSettingsGroupBox);

        outputSettingsGroupBox = new QGroupBox(verticalLayoutWidget_3);
        outputSettingsGroupBox->setObjectName("outputSettingsGroupBox");
        outputSettingsGroupBox->setMinimumSize(QSize(0, 155));
        outputSettingsGroupBox->setFlat(true);
        outputSettingsGroupBox->setProperty("anGroupVariant", QStringLiteral("flat"));
        verticalLayoutWidget_5 = outputSettingsGroupBox;
        outputSettingsVerticalLayout = new QVBoxLayout(outputSettingsGroupBox);
        outputSettingsVerticalLayout->setObjectName("outputSettingsVerticalLayout");
        outputSettingsVerticalLayout->setContentsMargins(t.spacing.md, t.spacing.md, t.spacing.md, t.spacing.md);
        horizontalLayout_5 = new QHBoxLayout();
        horizontalLayout_5->setObjectName("horizontalLayout_5");
        label_3 = new QLabel(outputSettingsGroupBox);
        label_3->setObjectName("label_3");
        label_3->setMinimumSize(QSize(100, 0));

        horizontalLayout_5->addWidget(label_3);

        sequenceTypeSelector = new QComboBox(outputSettingsGroupBox);
        sequenceTypeSelector->setObjectName("sequenceTypeSelector");
        sequenceTypeSelector->setMinimumSize(QSize(100, 0));

        horizontalLayout_5->addWidget(sequenceTypeSelector);

        horizontalSpacer_3 = new QSpacerItem(40, 20, QSizePolicy::Policy::Expanding, QSizePolicy::Policy::Minimum);

        horizontalLayout_5->addItem(horizontalSpacer_3);

        outputSettingsVerticalLayout->addLayout(horizontalLayout_5);

        horizontalLayout_6 = new QHBoxLayout();
        horizontalLayout_6->setObjectName("horizontalLayout_6");
        label_4 = new QLabel(outputSettingsGroupBox);
        label_4->setObjectName("label_4");
        label_4->setMinimumSize(QSize(100, 0));

        horizontalLayout_6->addWidget(label_4);

        fpsValue = new QDoubleSpinBox(outputSettingsGroupBox);
        fpsValue->setObjectName("fpsValue");
        fpsValue->setMinimum(0.100000000000000);
        fpsValue->setMaximum(200.000000000000000);
        fpsValue->setValue(1.000000000000000);

        horizontalLayout_6->addWidget(fpsValue);

        horizontalSpacer_4 = new QSpacerItem(40, 20, QSizePolicy::Policy::Expanding, QSizePolicy::Policy::Minimum);

        horizontalLayout_6->addItem(horizontalSpacer_4);

        outputSettingsVerticalLayout->addLayout(horizontalLayout_6);

        horizontalLayout_7 = new QHBoxLayout();
        horizontalLayout_7->setObjectName("horizontalLayout_7");
        label_5 = new QLabel(outputSettingsGroupBox);
        label_5->setObjectName("label_5");
        label_5->setMinimumSize(QSize(100, 0));

        horizontalLayout_7->addWidget(label_5);

        sequenceNameEdit = new QLineEdit(outputSettingsGroupBox);
        sequenceNameEdit->setObjectName("sequenceNameEdit");
        sequenceNameEdit->setMinimumSize(QSize(230, 0));

        horizontalLayout_7->addWidget(sequenceNameEdit);

        horizontalSpacer_5 = new QSpacerItem(40, 20, QSizePolicy::Policy::Expanding, QSizePolicy::Policy::Minimum);

        horizontalLayout_7->addItem(horizontalSpacer_5);

        horizontalLayout_7->setStretch(1, 1);
        horizontalLayout_7->setStretch(2, 4);

        outputSettingsVerticalLayout->addLayout(horizontalLayout_7);

        verticalSpacer_3 = new QSpacerItem(20, 40, QSizePolicy::Policy::Minimum, QSizePolicy::Policy::Expanding);

        outputSettingsVerticalLayout->addItem(verticalSpacer_3);

        verticalLayout_Tab2->addWidget(outputSettingsGroupBox);

        verticalSpacer = new QSpacerItem(20, 40, QSizePolicy::Policy::Minimum, QSizePolicy::Policy::Expanding);

        verticalLayout_Tab2->addItem(verticalSpacer);

        verticalLayout_Tab2->setStretch(0, 1);
        verticalLayout_Tab2->setStretch(1, 2);
        verticalLayout_Tab2->setStretch(2, 6);
        tabWidget->addTab(tab_2, QString());

        horizontalLayout_4->addWidget(tabWidget);

        verticalLayout_Main->addLayout(horizontalLayout_4);

        horizontalLayout_2 = new QHBoxLayout();
        horizontalLayout_2->setObjectName("horizontalLayout_2");
        horizontalLayout_2->setContentsMargins(-1, t.spacing.sm, -1, t.spacing.sm);
        buttonBox = new QDialogButtonBox(verticalLayoutWidget_2);
        buttonBox->setObjectName("buttonBox");
        buttonBox->setStandardButtons(QDialogButtonBox::Apply | QDialogButtonBox::Close);

        horizontalLayout_2->addWidget(buttonBox);

        verticalLayout_Main->addLayout(horizontalLayout_2);

        retranslateUi(NewSequenceControl);

        tabWidget->setCurrentIndex(0);

        QMetaObject::connectSlotsByName(NewSequenceControl);
    } // setupUi

    void retranslateUi(QWidget* NewSequenceControl)
    {
        NewSequenceControl->setWindowTitle(QCoreApplication::translate("NewSequenceControl", "New Sequence", nullptr));
        addFiles->setText(QCoreApplication::translate("NewSequenceControl", "Add Files", nullptr));
        removeSelected->setText(QCoreApplication::translate("NewSequenceControl", "Remove Selected", nullptr));
#if QT_CONFIG(tooltip)
        moveUpButton->setToolTip(QCoreApplication::translate("NewSequenceControl", "Move frame up", nullptr));
#endif // QT_CONFIG(tooltip)
        moveUpButton->setText(QString());
#if QT_CONFIG(tooltip)
        moveDownButton->setToolTip(QCoreApplication::translate("NewSequenceControl", "Move frame down", nullptr));
#endif // QT_CONFIG(tooltip)
        moveDownButton->setText(QString());
        label_27->setText(QCoreApplication::translate("NewSequenceControl", "Current Frame: ", nullptr));
        currentFrameLabel->setText(QString());
        label->setText(QCoreApplication::translate("NewSequenceControl", "Total Frames:", nullptr));
        totalFramesLabel->setText(QString());
        clearAll->setText(QCoreApplication::translate("NewSequenceControl", "Clear All", nullptr));
        tabWidget->setTabText(tabWidget->indexOf(tab),
                              QCoreApplication::translate("NewSequenceControl", "Main Settings", nullptr));
        inputSettingsGroupBox->setTitle(QCoreApplication::translate("NewSequenceControl", "Input Settings", nullptr));
        debayerCFA->setText(
            QCoreApplication::translate("NewSequenceControl", "Debayer CFA images            ", nullptr));
        label_2->setText(QCoreApplication::translate("NewSequenceControl", "Bayer Pattern", nullptr));
        outputSettingsGroupBox->setTitle(QCoreApplication::translate("NewSequenceControl", "Output Settings", nullptr));
        label_3->setText(QCoreApplication::translate("NewSequenceControl", "Sequence Type", nullptr));
        label_4->setText(QCoreApplication::translate("NewSequenceControl", "FPS                    ", nullptr));
        label_5->setText(QCoreApplication::translate("NewSequenceControl", "Sequence Name", nullptr));
        tabWidget->setTabText(tabWidget->indexOf(tab_2),
                              QCoreApplication::translate("NewSequenceControl", "Advanced Settings", nullptr));
    } // retranslateUi
};

inline NewSequenceControlUiElements buildNewSequenceControlUi(QWidget* parent)
{
    NewSequenceControlUiElements ui{};
    if (!parent)
    {
        return ui;
    }

    ui.setupUi(parent);
    return ui;
}

} // namespace an::ui::controls
