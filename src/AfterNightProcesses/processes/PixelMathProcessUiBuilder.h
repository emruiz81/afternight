// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include <QtCore/QVariant>
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
#include <QtWidgets/QPlainTextEdit>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QSpacerItem>
#include <QtWidgets/QSpinBox>
#include <QtWidgets/QTabWidget>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QWidget>
#include "controls/PixelMathEditControl.h"
#include "controls/ProcessTabStyling.h"
#include "theme/AnDesignTokens.h"
#include "widgets/TableWidget.h"

namespace an::ui::processes
{

class PixelMathProcessUiElements
{
public:
    QVBoxLayout* mainLayout;
    QWidget* verticalLayoutWidget;
    QVBoxLayout* verticalLayout;
    QHBoxLayout* horizontalLayout;
    QTabWidget* tabWidget;
    QWidget* tab_1;
    QHBoxLayout* horizontalLayoutRGBK;
    PixelMathEditControl* rgbkTextEdit;
    QVBoxLayout* verticalLayout_2;
    TableWidget* functionsTableWidget1;
    TableWidget* operatorsTableWidget1;
    TableWidget* symbolsTableWidget1;
    QWidget* tab_2;
    QHBoxLayout* horizontalLayoutG;
    PixelMathEditControl* greenTextEdit;
    QVBoxLayout* verticalLayout_3;
    TableWidget* functionsTableWidget2;
    TableWidget* operatorsTableWidget2;
    TableWidget* symbolsTableWidget2;
    QWidget* tab_3;
    QHBoxLayout* horizontalLayoutB;
    PixelMathEditControl* blueTextEdit;
    QVBoxLayout* verticalLayout_4;
    TableWidget* functionsTableWidget3;
    TableWidget* operatorsTableWidget3;
    TableWidget* symbolsTableWidget3;
    QWidget* tab_4;
    QHBoxLayout* horizontalLayoutA;
    PixelMathEditControl* alphaTextEdit;
    QVBoxLayout* verticalLayout_6;
    TableWidget* functionsTableWidget4;
    TableWidget* operatorsTableWidget4;
    TableWidget* symbolsTableWidget4;
    QWidget* tab_5;
    QVBoxLayout* verticalLayoutSymbols;
    QLabel* label_9;
    QPlainTextEdit* localSymbolsTextEdit;
    QLabel* label_10;
    QPlainTextEdit* globalSymbolsTextEdit;
    QWidget* tab_6;
    QVBoxLayout* verticalLayoutSummary;
    PixelMathEditControl* summaryTextEdit;
    QHBoxLayout* horizontalLayout_2;
    QCheckBox* useSingleRGBKExpression;
    QSpacerItem* horizontalSpacer;
    QPushButton* importButton;
    QPushButton* exportButton;
    QHBoxLayout* horizontalLayout_3;
    QGroupBox* groupBox;
    QCheckBox* rescaleResult;
    QComboBox* outputModeSelector;
    QHBoxLayout* horizontalLayout_6;
    QCheckBox* singleThreaded;
    QCheckBox* processIn64Bits;
    QCheckBox* codeOptimization;
    QSpacerItem* horizontalSpacer_3;
    QHBoxLayout* horizontalLayout_7;
    QLabel* label;
    QDoubleSpinBox* lowerBoundValue;
    QHBoxLayout* horizontalLayout_8;
    QLabel* label_2;
    QDoubleSpinBox* upperBoundValue;
    QHBoxLayout* horizontalLayout_9;
    QLabel* label_3;
    QLineEdit* newImadeIdEdit;
    QSpacerItem* horizontalSpacer_4;
    QHBoxLayout* horizontalLayout_10;
    QLabel* label_4;
    QSpinBox* widthValue;
    QLabel* label_5;
    QSpinBox* heightValue;
    QLabel* label_6;
    QComboBox* colorSpaceSelector;
    QLabel* label_8;
    QComboBox* colorFormatSelector;
    QHBoxLayout* horizontalLayout_5;
    QLabel* label_7;
    QComboBox* targetImageSelector;
    QSpacerItem* horizontalSpacer_2;
    QHBoxLayout* horizontalLayout_4;
    QDialogButtonBox* buttonBox;

    void setupUi(QWidget* PixelMathProcess)
    {
        const auto& t = an::ui::tokens();
        if (PixelMathProcess->objectName().isEmpty())
            PixelMathProcess->setObjectName("PixelMathProcess");
        PixelMathProcess->resize(829, 801);
        PixelMathProcess->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
        mainLayout = new QVBoxLayout(PixelMathProcess);
        mainLayout->setObjectName("mainLayout");
        mainLayout->setContentsMargins(t.spacing.md, t.spacing.md, t.spacing.md, t.spacing.md);
        mainLayout->setSpacing(0);
        verticalLayoutWidget = new QWidget(PixelMathProcess);
        verticalLayoutWidget->setObjectName("verticalLayoutWidget");
        mainLayout->addWidget(verticalLayoutWidget);
        verticalLayout = new QVBoxLayout(verticalLayoutWidget);
        verticalLayout->setObjectName("verticalLayout");
        verticalLayout->setContentsMargins(t.spacing.md, t.spacing.md, t.spacing.md, t.spacing.md);
        horizontalLayout = new QHBoxLayout();
        horizontalLayout->setObjectName("horizontalLayout");
        tabWidget = new QTabWidget(verticalLayoutWidget);
        tabWidget->setObjectName("tabWidget");
        tabWidget->setMinimumSize(QSize(500, 200));
        applyStandardProcessTabBarSizing(tabWidget);
        tab_1 = new QWidget();
        tab_1->setObjectName("tab_1");
        horizontalLayoutRGBK = new QHBoxLayout(tab_1);
        horizontalLayoutRGBK->setObjectName("horizontalLayoutRGBK");
        horizontalLayoutRGBK->setContentsMargins(t.spacing.sm, t.spacing.sm, t.spacing.sm, t.spacing.sm);
        rgbkTextEdit = new PixelMathEditControl(tab_1);
        rgbkTextEdit->setObjectName("rgbkTextEdit");

        horizontalLayoutRGBK->addWidget(rgbkTextEdit);

        verticalLayout_2 = new QVBoxLayout();
        verticalLayout_2->setSpacing(t.spacing.md);
        verticalLayout_2->setObjectName("verticalLayout_2");
        functionsTableWidget1 = new TableWidget(tab_1);
        functionsTableWidget1->setObjectName("functionsTableWidget1");
        functionsTableWidget1->setMinimumSize(QSize(200, 0));
        functionsTableWidget1->setMaximumSize(QSize(200, 16777215));
        functionsTableWidget1->setEditTriggers(QAbstractItemView::EditTrigger::NoEditTriggers);
        functionsTableWidget1->setSelectionMode(QAbstractItemView::SelectionMode::SingleSelection);

        verticalLayout_2->addWidget(functionsTableWidget1);

        operatorsTableWidget1 = new TableWidget(tab_1);
        operatorsTableWidget1->setObjectName("operatorsTableWidget1");
        operatorsTableWidget1->setMinimumSize(QSize(200, 0));
        operatorsTableWidget1->setMaximumSize(QSize(200, 16777215));
        operatorsTableWidget1->setEditTriggers(QAbstractItemView::EditTrigger::NoEditTriggers);

        verticalLayout_2->addWidget(operatorsTableWidget1);

        symbolsTableWidget1 = new TableWidget(tab_1);
        symbolsTableWidget1->setObjectName("symbolsTableWidget1");
        symbolsTableWidget1->setMinimumSize(QSize(200, 0));
        symbolsTableWidget1->setMaximumSize(QSize(200, 16777215));
        symbolsTableWidget1->setEditTriggers(QAbstractItemView::EditTrigger::NoEditTriggers);

        verticalLayout_2->addWidget(symbolsTableWidget1);

        horizontalLayoutRGBK->addLayout(verticalLayout_2);

        tabWidget->addTab(tab_1, QString());
        tab_2 = new QWidget();
        tab_2->setObjectName("tab_2");
        horizontalLayoutG = new QHBoxLayout(tab_2);
        horizontalLayoutG->setObjectName("horizontalLayoutG");
        horizontalLayoutG->setContentsMargins(t.spacing.sm, t.spacing.sm, t.spacing.sm, t.spacing.sm);
        greenTextEdit = new PixelMathEditControl(tab_2);
        greenTextEdit->setObjectName("greenTextEdit");

        horizontalLayoutG->addWidget(greenTextEdit);

        verticalLayout_3 = new QVBoxLayout();
        verticalLayout_3->setSpacing(t.spacing.md);
        verticalLayout_3->setObjectName("verticalLayout_3");
        functionsTableWidget2 = new TableWidget(tab_2);
        functionsTableWidget2->setObjectName("functionsTableWidget2");
        functionsTableWidget2->setMinimumSize(QSize(200, 0));
        functionsTableWidget2->setMaximumSize(QSize(200, 16777215));
        functionsTableWidget2->setEditTriggers(QAbstractItemView::EditTrigger::NoEditTriggers);
        functionsTableWidget2->setSelectionMode(QAbstractItemView::SelectionMode::SingleSelection);

        verticalLayout_3->addWidget(functionsTableWidget2);

        operatorsTableWidget2 = new TableWidget(tab_2);
        operatorsTableWidget2->setObjectName("operatorsTableWidget2");
        operatorsTableWidget2->setMinimumSize(QSize(200, 0));
        operatorsTableWidget2->setMaximumSize(QSize(200, 16777215));
        operatorsTableWidget2->setEditTriggers(QAbstractItemView::EditTrigger::NoEditTriggers);

        verticalLayout_3->addWidget(operatorsTableWidget2);

        symbolsTableWidget2 = new TableWidget(tab_2);
        symbolsTableWidget2->setObjectName("symbolsTableWidget2");
        symbolsTableWidget2->setMinimumSize(QSize(200, 0));
        symbolsTableWidget2->setMaximumSize(QSize(200, 16777215));
        symbolsTableWidget2->setEditTriggers(QAbstractItemView::EditTrigger::NoEditTriggers);

        verticalLayout_3->addWidget(symbolsTableWidget2);

        horizontalLayoutG->addLayout(verticalLayout_3);

        tabWidget->addTab(tab_2, QString());
        tab_3 = new QWidget();
        tab_3->setObjectName("tab_3");
        horizontalLayoutB = new QHBoxLayout(tab_3);
        horizontalLayoutB->setObjectName("horizontalLayoutB");
        horizontalLayoutB->setContentsMargins(t.spacing.sm, t.spacing.sm, t.spacing.sm, t.spacing.sm);
        blueTextEdit = new PixelMathEditControl(tab_3);
        blueTextEdit->setObjectName("blueTextEdit");

        horizontalLayoutB->addWidget(blueTextEdit);

        verticalLayout_4 = new QVBoxLayout();
        verticalLayout_4->setSpacing(t.spacing.md);
        verticalLayout_4->setObjectName("verticalLayout_4");
        functionsTableWidget3 = new TableWidget(tab_3);
        functionsTableWidget3->setObjectName("functionsTableWidget3");
        functionsTableWidget3->setMinimumSize(QSize(200, 0));
        functionsTableWidget3->setMaximumSize(QSize(200, 16777215));
        functionsTableWidget3->setEditTriggers(QAbstractItemView::EditTrigger::NoEditTriggers);
        functionsTableWidget3->setSelectionMode(QAbstractItemView::SelectionMode::SingleSelection);

        verticalLayout_4->addWidget(functionsTableWidget3);

        operatorsTableWidget3 = new TableWidget(tab_3);
        operatorsTableWidget3->setObjectName("operatorsTableWidget3");
        operatorsTableWidget3->setMinimumSize(QSize(200, 0));
        operatorsTableWidget3->setMaximumSize(QSize(200, 16777215));
        operatorsTableWidget3->setEditTriggers(QAbstractItemView::EditTrigger::NoEditTriggers);

        verticalLayout_4->addWidget(operatorsTableWidget3);

        symbolsTableWidget3 = new TableWidget(tab_3);
        symbolsTableWidget3->setObjectName("symbolsTableWidget3");
        symbolsTableWidget3->setMinimumSize(QSize(200, 0));
        symbolsTableWidget3->setMaximumSize(QSize(200, 16777215));
        symbolsTableWidget3->setEditTriggers(QAbstractItemView::EditTrigger::NoEditTriggers);

        verticalLayout_4->addWidget(symbolsTableWidget3);

        horizontalLayoutB->addLayout(verticalLayout_4);

        tabWidget->addTab(tab_3, QString());
        tab_4 = new QWidget();
        tab_4->setObjectName("tab_4");
        horizontalLayoutA = new QHBoxLayout(tab_4);
        horizontalLayoutA->setObjectName("horizontalLayoutA");
        horizontalLayoutA->setContentsMargins(t.spacing.sm, t.spacing.sm, t.spacing.sm, t.spacing.sm);
        alphaTextEdit = new PixelMathEditControl(tab_4);
        alphaTextEdit->setObjectName("alphaTextEdit");

        horizontalLayoutA->addWidget(alphaTextEdit);

        verticalLayout_6 = new QVBoxLayout();
        verticalLayout_6->setSpacing(t.spacing.md);
        verticalLayout_6->setObjectName("verticalLayout_6");
        functionsTableWidget4 = new TableWidget(tab_4);
        functionsTableWidget4->setObjectName("functionsTableWidget4");
        functionsTableWidget4->setMinimumSize(QSize(200, 0));
        functionsTableWidget4->setMaximumSize(QSize(200, 16777215));
        functionsTableWidget4->setEditTriggers(QAbstractItemView::EditTrigger::NoEditTriggers);
        functionsTableWidget4->setSelectionMode(QAbstractItemView::SelectionMode::SingleSelection);

        verticalLayout_6->addWidget(functionsTableWidget4);

        operatorsTableWidget4 = new TableWidget(tab_4);
        operatorsTableWidget4->setObjectName("operatorsTableWidget4");
        operatorsTableWidget4->setMinimumSize(QSize(200, 0));
        operatorsTableWidget4->setMaximumSize(QSize(200, 16777215));
        operatorsTableWidget4->setEditTriggers(QAbstractItemView::EditTrigger::NoEditTriggers);

        verticalLayout_6->addWidget(operatorsTableWidget4);

        symbolsTableWidget4 = new TableWidget(tab_4);
        symbolsTableWidget4->setObjectName("symbolsTableWidget4");
        symbolsTableWidget4->setMinimumSize(QSize(200, 0));
        symbolsTableWidget4->setMaximumSize(QSize(200, 16777215));
        symbolsTableWidget4->setEditTriggers(QAbstractItemView::EditTrigger::NoEditTriggers);

        verticalLayout_6->addWidget(symbolsTableWidget4);

        horizontalLayoutA->addLayout(verticalLayout_6);

        tabWidget->addTab(tab_4, QString());
        tab_5 = new QWidget();
        tab_5->setObjectName("tab_5");
        verticalLayoutSymbols = new QVBoxLayout(tab_5);
        verticalLayoutSymbols->setObjectName("verticalLayoutSymbols");
        verticalLayoutSymbols->setContentsMargins(t.spacing.sm, t.spacing.sm, t.spacing.sm, t.spacing.sm);
        label_9 = new QLabel(tab_5);
        label_9->setObjectName("label_9");

        verticalLayoutSymbols->addWidget(label_9);

        localSymbolsTextEdit = new QPlainTextEdit(tab_5);
        localSymbolsTextEdit->setObjectName("localSymbolsTextEdit");

        verticalLayoutSymbols->addWidget(localSymbolsTextEdit);

        label_10 = new QLabel(tab_5);
        label_10->setObjectName("label_10");

        verticalLayoutSymbols->addWidget(label_10);

        globalSymbolsTextEdit = new QPlainTextEdit(tab_5);
        globalSymbolsTextEdit->setObjectName("globalSymbolsTextEdit");

        verticalLayoutSymbols->addWidget(globalSymbolsTextEdit);

        tabWidget->addTab(tab_5, QString());
        tab_6 = new QWidget();
        tab_6->setObjectName("tab_6");
        verticalLayoutSummary = new QVBoxLayout(tab_6);
        verticalLayoutSummary->setObjectName("verticalLayoutSummary");
        verticalLayoutSummary->setContentsMargins(t.spacing.sm, t.spacing.sm, t.spacing.sm, t.spacing.sm);
        summaryTextEdit = new PixelMathEditControl(tab_6);
        summaryTextEdit->setObjectName("summaryTextEdit");
        summaryTextEdit->setReadOnly(true);

        verticalLayoutSummary->addWidget(summaryTextEdit);

        tabWidget->addTab(tab_6, QString());

        horizontalLayout->addWidget(tabWidget);

        verticalLayout->addLayout(horizontalLayout);

        horizontalLayout_2 = new QHBoxLayout();
        horizontalLayout_2->setObjectName("horizontalLayout_2");
        useSingleRGBKExpression = new QCheckBox(verticalLayoutWidget);
        useSingleRGBKExpression->setObjectName("useSingleRGBKExpression");
        useSingleRGBKExpression->setChecked(true);

        horizontalLayout_2->addWidget(useSingleRGBKExpression);

        horizontalSpacer = new QSpacerItem(40, 20, QSizePolicy::Policy::Expanding, QSizePolicy::Policy::Minimum);

        horizontalLayout_2->addItem(horizontalSpacer);

        importButton = new QPushButton(verticalLayoutWidget);
        importButton->setObjectName("importButton");

        horizontalLayout_2->addWidget(importButton);

        exportButton = new QPushButton(verticalLayoutWidget);
        exportButton->setObjectName("exportButton");

        horizontalLayout_2->addWidget(exportButton);

        verticalLayout->addLayout(horizontalLayout_2);

        horizontalLayout_3 = new QHBoxLayout();
        horizontalLayout_3->setObjectName("horizontalLayout_3");
        groupBox = new QGroupBox(verticalLayoutWidget);
        groupBox->setObjectName("groupBox");
        QVBoxLayout* groupBoxLayout = new QVBoxLayout(groupBox);
        groupBoxLayout->setContentsMargins(t.spacing.md, t.spacing.md, t.spacing.md, t.spacing.md);
        groupBoxLayout->setSpacing(t.spacing.sm);

        horizontalLayout_6 = new QHBoxLayout();
        horizontalLayout_6->setObjectName("horizontalLayout_6");
        singleThreaded = new QCheckBox(groupBox);
        singleThreaded->setObjectName("singleThreaded");
        horizontalLayout_6->addWidget(singleThreaded);
        processIn64Bits = new QCheckBox(groupBox);
        processIn64Bits->setObjectName("processIn64Bits");
        // Every expression is evaluated in 64-bit floating point; the box states it rather than offering a choice.
        processIn64Bits->setChecked(true);
        processIn64Bits->setEnabled(false);
        horizontalLayout_6->addWidget(processIn64Bits);
        codeOptimization = new QCheckBox(groupBox);
        codeOptimization->setObjectName("codeOptimization");
        codeOptimization->setChecked(true);
        horizontalLayout_6->addWidget(codeOptimization);
        horizontalSpacer_3 = new QSpacerItem(40, 20, QSizePolicy::Policy::Expanding, QSizePolicy::Policy::Minimum);
        horizontalLayout_6->addItem(horizontalSpacer_3);
        groupBoxLayout->addLayout(horizontalLayout_6);

        rescaleResult = new QCheckBox(groupBox);
        rescaleResult->setObjectName("rescaleResult");
        groupBoxLayout->addWidget(rescaleResult);

        horizontalLayout_7 = new QHBoxLayout();
        horizontalLayout_7->setObjectName("horizontalLayout_7");
        label = new QLabel(groupBox);
        label->setObjectName("label");
        horizontalLayout_7->addWidget(label);
        lowerBoundValue = new QDoubleSpinBox(groupBox);
        lowerBoundValue->setObjectName("lowerBoundValue");
        lowerBoundValue->setEnabled(false);
        lowerBoundValue->setDecimals(8);
        lowerBoundValue->setMaximum(1.000000000000000);
        lowerBoundValue->setSingleStep(0.010000000000000);
        horizontalLayout_7->addWidget(lowerBoundValue);
        horizontalLayout_7->addItem(
            new QSpacerItem(40, 20, QSizePolicy::Policy::Expanding, QSizePolicy::Policy::Minimum));
        groupBoxLayout->addLayout(horizontalLayout_7);

        horizontalLayout_8 = new QHBoxLayout();
        horizontalLayout_8->setObjectName("horizontalLayout_8");
        label_2 = new QLabel(groupBox);
        label_2->setObjectName("label_2");
        horizontalLayout_8->addWidget(label_2);
        upperBoundValue = new QDoubleSpinBox(groupBox);
        upperBoundValue->setObjectName("upperBoundValue");
        upperBoundValue->setEnabled(false);
        upperBoundValue->setDecimals(8);
        upperBoundValue->setMaximum(1.000000000000000);
        upperBoundValue->setSingleStep(0.010000000000000);
        upperBoundValue->setValue(1.000000000000000);
        horizontalLayout_8->addWidget(upperBoundValue);
        horizontalLayout_8->addItem(
            new QSpacerItem(40, 20, QSizePolicy::Policy::Expanding, QSizePolicy::Policy::Minimum));
        groupBoxLayout->addLayout(horizontalLayout_8);

        {
            QHBoxLayout* hboxOutputMode = new QHBoxLayout();
            outputModeSelector = new QComboBox(groupBox);
            outputModeSelector->addItem(QString());
            outputModeSelector->addItem(QString());
            outputModeSelector->setObjectName("outputModeSelector");
            outputModeSelector->setMinimumWidth(180);
            hboxOutputMode->addWidget(outputModeSelector);
            hboxOutputMode->addItem(
                new QSpacerItem(40, 20, QSizePolicy::Policy::Expanding, QSizePolicy::Policy::Minimum));
            groupBoxLayout->addLayout(hboxOutputMode);
        }

        horizontalLayout_9 = new QHBoxLayout();
        horizontalLayout_9->setObjectName("horizontalLayout_9");
        label_3 = new QLabel(groupBox);
        label_3->setObjectName("label_3");
        horizontalLayout_9->addWidget(label_3);
        newImadeIdEdit = new QLineEdit(groupBox);
        newImadeIdEdit->setObjectName("newImadeIdEdit");
        horizontalLayout_9->addWidget(newImadeIdEdit);
        horizontalSpacer_4 = new QSpacerItem(40, 20, QSizePolicy::Policy::Expanding, QSizePolicy::Policy::Minimum);
        horizontalLayout_9->addItem(horizontalSpacer_4);
        horizontalLayout_9->setStretch(1, 2);
        horizontalLayout_9->setStretch(2, 2);
        groupBoxLayout->addLayout(horizontalLayout_9);

        horizontalLayout_10 = new QHBoxLayout();
        horizontalLayout_10->setObjectName("horizontalLayout_10");
        label_4 = new QLabel(groupBox);
        label_4->setObjectName("label_4");
        label_4->setMinimumSize(QSize(72, 0));
        horizontalLayout_10->addWidget(label_4);
        widthValue = new QSpinBox(groupBox);
        widthValue->setObjectName("widthValue");
        widthValue->setMinimumSize(QSize(70, 0));
        widthValue->setMinimum(0);
        widthValue->setMaximum(12000);
        widthValue->setValue(0);
        horizontalLayout_10->addWidget(widthValue);
        label_5 = new QLabel(groupBox);
        label_5->setObjectName("label_5");
        horizontalLayout_10->addWidget(label_5);
        heightValue = new QSpinBox(groupBox);
        heightValue->setObjectName("heightValue");
        heightValue->setMinimumSize(QSize(70, 0));
        heightValue->setMinimum(0);
        heightValue->setMaximum(12000);
        heightValue->setValue(0);
        horizontalLayout_10->addWidget(heightValue);
        label_6 = new QLabel(groupBox);
        label_6->setObjectName("label_6");
        horizontalLayout_10->addWidget(label_6);
        colorSpaceSelector = new QComboBox(groupBox);
        colorSpaceSelector->addItem(QString());
        colorSpaceSelector->addItem(QString());
        colorSpaceSelector->addItem(QString());
        colorSpaceSelector->addItem(QString());
        colorSpaceSelector->setObjectName("colorSpaceSelector");
        colorSpaceSelector->setMinimumSize(QSize(120, 0));
        horizontalLayout_10->addWidget(colorSpaceSelector);
        label_8 = new QLabel(groupBox);
        label_8->setObjectName("label_8");
        horizontalLayout_10->addWidget(label_8);
        colorFormatSelector = new QComboBox(groupBox);
        colorFormatSelector->addItem(QString());
        colorFormatSelector->addItem(QString());
        colorFormatSelector->addItem(QString());
        colorFormatSelector->addItem(QString());
        colorFormatSelector->addItem(QString());
        colorFormatSelector->addItem(QString());
        colorFormatSelector->setObjectName("colorFormatSelector");
        colorFormatSelector->setMinimumSize(QSize(150, 0));
        horizontalLayout_10->addWidget(colorFormatSelector);
        horizontalLayout_10->addItem(
            new QSpacerItem(20, 20, QSizePolicy::Policy::Expanding, QSizePolicy::Policy::Minimum));
        groupBoxLayout->addLayout(horizontalLayout_10);

        horizontalLayout_3->addWidget(groupBox);

        verticalLayout->addLayout(horizontalLayout_3);

        horizontalLayout_5 = new QHBoxLayout();
        horizontalLayout_5->setObjectName("horizontalLayout_5");
        label_7 = new QLabel(verticalLayoutWidget);
        label_7->setObjectName("label_7");

        horizontalLayout_5->addWidget(label_7);

        targetImageSelector = new QComboBox(verticalLayoutWidget);
        targetImageSelector->setObjectName("targetImageSelector");

        horizontalLayout_5->addWidget(targetImageSelector);

        horizontalSpacer_2 = new QSpacerItem(40, 20, QSizePolicy::Policy::Expanding, QSizePolicy::Policy::Minimum);

        horizontalLayout_5->addItem(horizontalSpacer_2);

        horizontalLayout_5->setStretch(1, 2);
        horizontalLayout_5->setStretch(2, 2);

        verticalLayout->addLayout(horizontalLayout_5);

        horizontalLayout_4 = new QHBoxLayout();
        horizontalLayout_4->setObjectName("horizontalLayout_4");
        buttonBox = new QDialogButtonBox(verticalLayoutWidget);
        buttonBox->setObjectName("buttonBox");
        buttonBox->setStandardButtons(QDialogButtonBox::StandardButton::Apply |
                                      QDialogButtonBox::StandardButton::Close);

        horizontalLayout_4->addWidget(buttonBox);

        verticalLayout->addLayout(horizontalLayout_4);

        retranslateUi(PixelMathProcess);

        tabWidget->setCurrentIndex(0);

        QMetaObject::connectSlotsByName(PixelMathProcess);
    } // setupUi

    void retranslateUi(QWidget* PixelMathProcess)
    {
        PixelMathProcess->setWindowTitle(QCoreApplication::translate("PixelMathProcess", "Form", nullptr));
        tabWidget->setTabText(tabWidget->indexOf(tab_1),
                              QCoreApplication::translate("PixelMathProcess", "RGB/K", nullptr));
        tabWidget->setTabText(tabWidget->indexOf(tab_2), QCoreApplication::translate("PixelMathProcess", "G", nullptr));
        tabWidget->setTabText(tabWidget->indexOf(tab_3), QCoreApplication::translate("PixelMathProcess", "B", nullptr));
        tabWidget->setTabText(tabWidget->indexOf(tab_4), QCoreApplication::translate("PixelMathProcess", "A", nullptr));
        label_9->setText(
            QCoreApplication::translate("PixelMathProcess", "Local Symbols (Comma separated list)", nullptr));
        label_10->setText(
            QCoreApplication::translate("PixelMathProcess", "Global Symbols (Comma separated list)", nullptr));
        tabWidget->setTabText(tabWidget->indexOf(tab_5),
                              QCoreApplication::translate("PixelMathProcess", "Symbols", nullptr));
        tabWidget->setTabText(tabWidget->indexOf(tab_6),
                              QCoreApplication::translate("PixelMathProcess", "Summary", nullptr));
        useSingleRGBKExpression->setText(
            QCoreApplication::translate("PixelMathProcess", "Use a single RGB/K expression", nullptr));
        importButton->setText(QCoreApplication::translate("PixelMathProcess", "Import", nullptr));
        exportButton->setText(QCoreApplication::translate("PixelMathProcess", "Export", nullptr));
        groupBox->setTitle(QCoreApplication::translate("PixelMathProcess", "Settings", nullptr));
        rescaleResult->setText(QCoreApplication::translate("PixelMathProcess", "Rescale result", nullptr));
        outputModeSelector->setItemText(
            0, QCoreApplication::translate("PixelMathProcess", "Replace target image", nullptr));
        outputModeSelector->setItemText(1,
                                        QCoreApplication::translate("PixelMathProcess", "Create new image", nullptr));

        singleThreaded->setText(QCoreApplication::translate("PixelMathProcess", "Single threaded", nullptr));
        processIn64Bits->setText(QCoreApplication::translate("PixelMathProcess", "Process in 64 bits", nullptr));
        codeOptimization->setText(QCoreApplication::translate("PixelMathProcess", "Code optimization", nullptr));
        label->setText(QCoreApplication::translate("PixelMathProcess", "Lower bound", nullptr));
        label_2->setText(QCoreApplication::translate("PixelMathProcess", "Upper bound", nullptr));
        label_3->setText(QCoreApplication::translate("PixelMathProcess", "New Image Id", nullptr));
        newImadeIdEdit->setText(QCoreApplication::translate("PixelMathProcess", "PIXEL_MATH_IMAGE", nullptr));
        label_4->setText(QCoreApplication::translate("PixelMathProcess", "Width  ", nullptr));
        label_5->setText(QCoreApplication::translate("PixelMathProcess", "Height", nullptr));
        label_6->setText(QCoreApplication::translate("PixelMathProcess", "Color space", nullptr));
        colorSpaceSelector->setItemText(0, QCoreApplication::translate("PixelMathProcess", "As target", nullptr));
        colorSpaceSelector->setItemText(1, QCoreApplication::translate("PixelMathProcess", "Grayscale", nullptr));
        colorSpaceSelector->setItemText(2, QCoreApplication::translate("PixelMathProcess", "RGB", nullptr));
        colorSpaceSelector->setItemText(3, QCoreApplication::translate("PixelMathProcess", "RGBA", nullptr));

        label_8->setText(QCoreApplication::translate("PixelMathProcess", "Color format", nullptr));
        colorFormatSelector->setItemText(0, QCoreApplication::translate("PixelMathProcess", "As target", nullptr));
        colorFormatSelector->setItemText(1, QCoreApplication::translate("PixelMathProcess", "8 bits", nullptr));
        colorFormatSelector->setItemText(2, QCoreApplication::translate("PixelMathProcess", "16 bits", nullptr));
        colorFormatSelector->setItemText(3, QCoreApplication::translate("PixelMathProcess", "32 bits int", nullptr));
        colorFormatSelector->setItemText(4, QCoreApplication::translate("PixelMathProcess", "32 bits float", nullptr));
        colorFormatSelector->setItemText(5, QCoreApplication::translate("PixelMathProcess", "64 bits float", nullptr));

        label_7->setText(QCoreApplication::translate("PixelMathProcess", "Target Image ", nullptr));
    } // retranslateUi
};

inline PixelMathProcessUiElements buildPixelMathProcessUi(QWidget* parent)
{
    PixelMathProcessUiElements ui;
    ui.setupUi(parent);
    return ui;
}

} // namespace an::ui::processes
