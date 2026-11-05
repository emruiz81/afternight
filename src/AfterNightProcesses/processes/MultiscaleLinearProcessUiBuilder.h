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
#include <QtWidgets/QFormLayout>
#include <QtWidgets/QGroupBox>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QHeaderView>
#include <QtWidgets/QLabel>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QScrollArea>
#include <QtWidgets/QSlider>
#include <QtWidgets/QSplitter>
#include <QtWidgets/QSpacerItem>
#include <QtWidgets/QSpinBox>
#include <QtWidgets/QTreeWidget>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QWidget>

#include "theme/AnDesignTokens.h"

#include <AfterNightEngine/Algorithms/Multiscale/MultiscaleTransform.h>

namespace an::ui::processes
{

class MultiscaleLinearProcessUiElements
{
public:
    QHBoxLayout* horizontalLayoutMain;
    QSplitter* mainSplitter;
    QWidget* leftPanelWidget;
    QVBoxLayout* leftPanelLayout;
    QGroupBox* transformSettingsGroup;
    QFormLayout* transformFormLayout;
    QLabel* labelAlgorithm;
    QComboBox* algorithmComboBox;
    QLabel* labelLayers;
    QSpinBox* numLayersSpinBox;
    QLabel* labelSpacing;
    QComboBox* spacingComboBox;
    QLabel* labelLinearScale;
    QSpinBox* linearScaleSpinBox;
    QLabel* labelScaling;
    QComboBox* scalingFunctionComboBox;
    QLabel* labelAmount;
    QHBoxLayout* amountLayout;
    QSlider* amountSlider;
    QDoubleSpinBox* amountSpinBox;
    QGroupBox* layerTreeGroup;
    QVBoxLayout* layerTreeLayout;
    QTreeWidget* layerTreeWidget;
    QHBoxLayout* layerActionsLayout;
    QPushButton* enableAllLayersButton;
    QPushButton* disableAllLayersButton;
    QPushButton* resetBiasesButton;
    QGroupBox* globalSettingsGroup;
    QVBoxLayout* globalSettingsOuterLayout;
    QScrollArea* globalSettingsScrollArea;
    QWidget* globalSettingsScrollContent;
    QVBoxLayout* globalSettingsLayout;
    QGroupBox* linearMaskGroup;
    QFormLayout* linearMaskFormLayout;
    QLabel* labelLinearMaskAmplification;
    QHBoxLayout* linearMaskAmplificationLayout;
    QSlider* linearMaskAmplificationSlider;
    QSpinBox* linearMaskAmplificationSpinBox;
    QLabel* labelLinearMaskSmoothness;
    QHBoxLayout* linearMaskSmoothnessLayout;
    QSlider* linearMaskSmoothnessSlider;
    QDoubleSpinBox* linearMaskSmoothnessSpinBox;
    QHBoxLayout* linearMaskOptionsLayout;
    QCheckBox* linearMaskPreviewCheckBox;
    QCheckBox* linearMaskInvertCheckBox;
    QGroupBox* ksigmaGroup;
    QFormLayout* ksigmaFormLayout;
    QLabel* labelKsigmaK;
    QHBoxLayout* ksigmaKLayout;
    QSlider* ksigmaKSlider;
    QDoubleSpinBox* ksigmaKSpinBox;
    QLabel* labelKsigmaAmount;
    QHBoxLayout* ksigmaAmountLayout;
    QSlider* ksigmaAmountSlider;
    QDoubleSpinBox* ksigmaAmountSpinBox;
    QHBoxLayout* ksigmaOptionsLayout;
    QCheckBox* ksigmaSoftCheckBox;
    QCheckBox* ksigmaUseMrsCheckBox;
    QGroupBox* deringingGroup;
    QFormLayout* deringingFormLayout;
    QLabel* labelDeringingDark;
    QHBoxLayout* deringingDarkLayout;
    QSlider* deringingDarkSlider;
    QDoubleSpinBox* deringingDarkSpinBox;
    QLabel* labelDeringingBright;
    QHBoxLayout* deringingBrightLayout;
    QSlider* deringingBrightSlider;
    QDoubleSpinBox* deringingBrightSpinBox;
    QCheckBox* deringingOutputMapsCheckBox;
    QGroupBox* lstfGroup;
    QFormLayout* lstfFormLayout;
    QLabel* labelLstfMode;
    QComboBox* lstfModeComboBox;
    QLabel* labelLstfBreakpoint;
    QHBoxLayout* lstfBreakpointLayout;
    QSlider* lstfBreakpointSlider;
    QDoubleSpinBox* lstfBreakpointSpinBox;
    QGroupBox* dreGroup;
    QFormLayout* dreFormLayout;
    QLabel* labelDreLow;
    QHBoxLayout* dreLowLayout;
    QSlider* dreLowSlider;
    QDoubleSpinBox* dreLowSpinBox;
    QLabel* labelDreHigh;
    QHBoxLayout* dreHighLayout;
    QSlider* dreHighSlider;
    QDoubleSpinBox* dreHighSpinBox;
    QWidget* rightPanelWidget;
    QVBoxLayout* rightPanelLayout;
    QVBoxLayout* verticalLayout_2;
    QWidget* widget;
    QGroupBox* layerInspectorGroup;
    QVBoxLayout* layerInspectorLayout;
    QHBoxLayout* layerHeaderLayout;
    QCheckBox* layerEnabledCheckBox;
    QLabel* layerNameLabel;
    QSpacerItem* layerHeaderSpacer;
    QPushButton* copyToAllButton;
    QPushButton* resetCurrentLayerButton;
    QHBoxLayout* biasLayout;
    QLabel* labelBias;
    QSlider* biasSlider;
    QDoubleSpinBox* biasSpinBox;
    QGroupBox* layerNrGroup;
    QFormLayout* layerNrFormLayout;
    QLabel* labelNrThreshold;
    QHBoxLayout* nrThresholdLayout;
    QSlider* nrThresholdSlider;
    QDoubleSpinBox* nrThresholdSpinBox;
    QLabel* labelNrAmount;
    QHBoxLayout* nrAmountLayout;
    QSlider* nrAmountSlider;
    QDoubleSpinBox* nrAmountSpinBox;
    QLabel* labelNrIterations;
    QSpinBox* nrIterationsSpinBox;
    QFormLayout* targetFormLayout;
    QLabel* labelTarget;
    QComboBox* targetImageSelector;
    QLabel* labelChannel;
    QComboBox* channelSelector;
    QHBoxLayout* previewLayout;
    QCheckBox* preview;
    QComboBox* previewModeComboBox;
    QSpacerItem* previewSpacer;
    QDialogButtonBox* buttonBox;

    void setupUi(QWidget* MultiscaleLinearProcess)
    {
        const auto& t = an::ui::tokens();
        if (MultiscaleLinearProcess->objectName().isEmpty())
            MultiscaleLinearProcess->setObjectName("MultiscaleLinearProcess");
        MultiscaleLinearProcess->resize(1100, 1091);
        MultiscaleLinearProcess->setMinimumSize(QSize(900, 800));
        horizontalLayoutMain = new QHBoxLayout(MultiscaleLinearProcess);
        horizontalLayoutMain->setSpacing(t.spacing.md);
        horizontalLayoutMain->setObjectName("horizontalLayoutMain");
        horizontalLayoutMain->setContentsMargins(t.spacing.md, t.spacing.md, t.spacing.md, t.spacing.md);
        mainSplitter = new QSplitter(MultiscaleLinearProcess);
        mainSplitter->setObjectName("mainSplitter");
        mainSplitter->setOrientation(Qt::Orientation::Horizontal);
        mainSplitter->setProperty("anSplitterVariant", QStringLiteral("process"));
        mainSplitter->setChildrenCollapsible(false);
        leftPanelWidget = new QWidget();
        leftPanelWidget->setObjectName("leftPanelWidget");
        leftPanelLayout = new QVBoxLayout(leftPanelWidget);
        leftPanelLayout->setSpacing(t.spacing.sm);
        leftPanelLayout->setObjectName("leftPanelLayout");
        leftPanelLayout->setContentsMargins(0, 0, 0, 0);
        transformSettingsGroup = new QGroupBox(leftPanelWidget);
        transformSettingsGroup->setObjectName("transformSettingsGroup");
        transformFormLayout = new QFormLayout(transformSettingsGroup);
        transformFormLayout->setObjectName("transformFormLayout");
        transformFormLayout->setContentsMargins(t.spacing.md, t.spacing.md, t.spacing.md, t.spacing.md);
        labelAlgorithm = new QLabel(transformSettingsGroup);
        labelAlgorithm->setObjectName("labelAlgorithm");

        transformFormLayout->setWidget(0, QFormLayout::ItemRole::LabelRole, labelAlgorithm);

        algorithmComboBox = new QComboBox(transformSettingsGroup);
        algorithmComboBox->setObjectName("algorithmComboBox");

        transformFormLayout->setWidget(0, QFormLayout::ItemRole::FieldRole, algorithmComboBox);

        labelLayers = new QLabel(transformSettingsGroup);
        labelLayers->setObjectName("labelLayers");

        transformFormLayout->setWidget(1, QFormLayout::ItemRole::LabelRole, labelLayers);

        numLayersSpinBox = new QSpinBox(transformSettingsGroup);
        numLayersSpinBox->setObjectName("numLayersSpinBox");
        numLayersSpinBox->setMinimum(1);
        numLayersSpinBox->setMaximum(static_cast<int>(an::kMaxMultiscaleLayers) - 1);
        numLayersSpinBox->setValue(6);

        transformFormLayout->setWidget(1, QFormLayout::ItemRole::FieldRole, numLayersSpinBox);

        labelSpacing = new QLabel(transformSettingsGroup);
        labelSpacing->setObjectName("labelSpacing");

        transformFormLayout->setWidget(2, QFormLayout::ItemRole::LabelRole, labelSpacing);

        spacingComboBox = new QComboBox(transformSettingsGroup);
        spacingComboBox->setObjectName("spacingComboBox");

        transformFormLayout->setWidget(2, QFormLayout::ItemRole::FieldRole, spacingComboBox);

        labelLinearScale = new QLabel(transformSettingsGroup);
        labelLinearScale->setObjectName("labelLinearScale");

        transformFormLayout->setWidget(3, QFormLayout::ItemRole::LabelRole, labelLinearScale);

        linearScaleSpinBox = new QSpinBox(transformSettingsGroup);
        linearScaleSpinBox->setObjectName("linearScaleSpinBox");
        linearScaleSpinBox->setMinimum(1);
        linearScaleSpinBox->setMaximum(16);
        linearScaleSpinBox->setValue(1);

        transformFormLayout->setWidget(3, QFormLayout::ItemRole::FieldRole, linearScaleSpinBox);

        labelScaling = new QLabel(transformSettingsGroup);
        labelScaling->setObjectName("labelScaling");

        transformFormLayout->setWidget(4, QFormLayout::ItemRole::LabelRole, labelScaling);

        scalingFunctionComboBox = new QComboBox(transformSettingsGroup);
        scalingFunctionComboBox->setObjectName("scalingFunctionComboBox");

        transformFormLayout->setWidget(4, QFormLayout::ItemRole::FieldRole, scalingFunctionComboBox);

        labelAmount = new QLabel(transformSettingsGroup);
        labelAmount->setObjectName("labelAmount");

        transformFormLayout->setWidget(5, QFormLayout::ItemRole::LabelRole, labelAmount);

        amountLayout = new QHBoxLayout();
        amountLayout->setObjectName("amountLayout");
        amountSlider = new QSlider(transformSettingsGroup);
        amountSlider->setObjectName("amountSlider");
        amountSlider->setMaximum(1000);
        amountSlider->setValue(1000);
        amountSlider->setOrientation(Qt::Orientation::Horizontal);

        amountLayout->addWidget(amountSlider);

        amountSpinBox = new QDoubleSpinBox(transformSettingsGroup);
        amountSpinBox->setObjectName("amountSpinBox");
        amountSpinBox->setMinimum(0.000000000000000);
        amountSpinBox->setMaximum(1.000000000000000);
        amountSpinBox->setSingleStep(0.050000000000000);
        amountSpinBox->setValue(1.000000000000000);

        amountLayout->addWidget(amountSpinBox);

        transformFormLayout->setLayout(5, QFormLayout::ItemRole::FieldRole, amountLayout);

        leftPanelLayout->addWidget(transformSettingsGroup);

        layerTreeGroup = new QGroupBox(leftPanelWidget);
        layerTreeGroup->setObjectName("layerTreeGroup");
        QSizePolicy sizePolicy(QSizePolicy::Policy::Preferred, QSizePolicy::Policy::Expanding);
        sizePolicy.setHorizontalStretch(0);
        sizePolicy.setVerticalStretch(1);
        sizePolicy.setHeightForWidth(layerTreeGroup->sizePolicy().hasHeightForWidth());
        layerTreeGroup->setSizePolicy(sizePolicy);
        layerTreeLayout = new QVBoxLayout(layerTreeGroup);
        layerTreeLayout->setObjectName("layerTreeLayout");
        layerTreeLayout->setContentsMargins(t.spacing.sm, t.spacing.sm, t.spacing.sm, t.spacing.sm);
        layerTreeWidget = new QTreeWidget(layerTreeGroup);
        layerTreeWidget->setObjectName("layerTreeWidget");
        layerTreeWidget->setMinimumSize(QSize(0, 150));
        layerTreeWidget->setAlternatingRowColors(true);
        layerTreeWidget->setSelectionMode(QAbstractItemView::SelectionMode::SingleSelection);
        layerTreeWidget->setRootIsDecorated(false);

        layerTreeLayout->addWidget(layerTreeWidget);

        layerActionsLayout = new QHBoxLayout();
        layerActionsLayout->setObjectName("layerActionsLayout");
        enableAllLayersButton = new QPushButton(layerTreeGroup);
        enableAllLayersButton->setObjectName("enableAllLayersButton");

        layerActionsLayout->addWidget(enableAllLayersButton);

        disableAllLayersButton = new QPushButton(layerTreeGroup);
        disableAllLayersButton->setObjectName("disableAllLayersButton");

        layerActionsLayout->addWidget(disableAllLayersButton);

        resetBiasesButton = new QPushButton(layerTreeGroup);
        resetBiasesButton->setObjectName("resetBiasesButton");

        layerActionsLayout->addWidget(resetBiasesButton);

        layerTreeLayout->addLayout(layerActionsLayout);

        leftPanelLayout->addWidget(layerTreeGroup);

        globalSettingsGroup = new QGroupBox(leftPanelWidget);
        globalSettingsGroup->setObjectName("globalSettingsGroup");
        sizePolicy.setHeightForWidth(globalSettingsGroup->sizePolicy().hasHeightForWidth());
        globalSettingsGroup->setSizePolicy(sizePolicy);
        globalSettingsOuterLayout = new QVBoxLayout(globalSettingsGroup);
        globalSettingsOuterLayout->setSpacing(0);
        globalSettingsOuterLayout->setObjectName("globalSettingsOuterLayout");
        globalSettingsOuterLayout->setContentsMargins(0, 0, 0, 0);
        globalSettingsScrollArea = new QScrollArea(globalSettingsGroup);
        globalSettingsScrollArea->setObjectName("globalSettingsScrollArea");
        globalSettingsScrollArea->setFrameShape(QFrame::Shape::NoFrame);
        globalSettingsScrollArea->setWidgetResizable(true);
        globalSettingsScrollContent = new QWidget();
        globalSettingsScrollContent->setObjectName("globalSettingsScrollContent");
        globalSettingsScrollContent->setMinimumSize(QSize(480, 623));
        globalSettingsLayout = new QVBoxLayout(globalSettingsScrollContent);
        globalSettingsLayout->setSpacing(t.spacing.xs);
        globalSettingsLayout->setObjectName("globalSettingsLayout");
        globalSettingsLayout->setContentsMargins(t.spacing.sm, t.spacing.sm, t.spacing.sm, t.spacing.sm);
        linearMaskGroup = new QGroupBox(globalSettingsScrollContent);
        linearMaskGroup->setObjectName("linearMaskGroup");
        linearMaskGroup->setCheckable(true);
        linearMaskGroup->setChecked(false);
        linearMaskFormLayout = new QFormLayout(linearMaskGroup);
        linearMaskFormLayout->setObjectName("linearMaskFormLayout");
        labelLinearMaskAmplification = new QLabel(linearMaskGroup);
        labelLinearMaskAmplification->setObjectName("labelLinearMaskAmplification");

        linearMaskFormLayout->setWidget(0, QFormLayout::ItemRole::LabelRole, labelLinearMaskAmplification);

        linearMaskAmplificationLayout = new QHBoxLayout();
        linearMaskAmplificationLayout->setObjectName("linearMaskAmplificationLayout");
        linearMaskAmplificationSlider = new QSlider(linearMaskGroup);
        linearMaskAmplificationSlider->setObjectName("linearMaskAmplificationSlider");
        linearMaskAmplificationSlider->setMinimum(1);
        linearMaskAmplificationSlider->setMaximum(300);
        linearMaskAmplificationSlider->setValue(100);
        linearMaskAmplificationSlider->setOrientation(Qt::Orientation::Horizontal);

        linearMaskAmplificationLayout->addWidget(linearMaskAmplificationSlider);

        linearMaskAmplificationSpinBox = new QSpinBox(linearMaskGroup);
        linearMaskAmplificationSpinBox->setObjectName("linearMaskAmplificationSpinBox");
        linearMaskAmplificationSpinBox->setMinimum(1);
        linearMaskAmplificationSpinBox->setMaximum(300);
        linearMaskAmplificationSpinBox->setValue(100);

        linearMaskAmplificationLayout->addWidget(linearMaskAmplificationSpinBox);

        linearMaskFormLayout->setLayout(0, QFormLayout::ItemRole::FieldRole, linearMaskAmplificationLayout);

        labelLinearMaskSmoothness = new QLabel(linearMaskGroup);
        labelLinearMaskSmoothness->setObjectName("labelLinearMaskSmoothness");

        linearMaskFormLayout->setWidget(1, QFormLayout::ItemRole::LabelRole, labelLinearMaskSmoothness);

        linearMaskSmoothnessLayout = new QHBoxLayout();
        linearMaskSmoothnessLayout->setObjectName("linearMaskSmoothnessLayout");
        linearMaskSmoothnessSlider = new QSlider(linearMaskGroup);
        linearMaskSmoothnessSlider->setObjectName("linearMaskSmoothnessSlider");
        linearMaskSmoothnessSlider->setMaximum(100);
        linearMaskSmoothnessSlider->setValue(10);
        linearMaskSmoothnessSlider->setOrientation(Qt::Orientation::Horizontal);

        linearMaskSmoothnessLayout->addWidget(linearMaskSmoothnessSlider);

        linearMaskSmoothnessSpinBox = new QDoubleSpinBox(linearMaskGroup);
        linearMaskSmoothnessSpinBox->setObjectName("linearMaskSmoothnessSpinBox");
        linearMaskSmoothnessSpinBox->setMinimum(0.000000000000000);
        linearMaskSmoothnessSpinBox->setMaximum(10.000000000000000);
        linearMaskSmoothnessSpinBox->setSingleStep(0.100000000000000);
        linearMaskSmoothnessSpinBox->setValue(1.000000000000000);

        linearMaskSmoothnessLayout->addWidget(linearMaskSmoothnessSpinBox);

        linearMaskFormLayout->setLayout(1, QFormLayout::ItemRole::FieldRole, linearMaskSmoothnessLayout);

        linearMaskOptionsLayout = new QHBoxLayout();
        linearMaskOptionsLayout->setObjectName("linearMaskOptionsLayout");
        linearMaskPreviewCheckBox = new QCheckBox(linearMaskGroup);
        linearMaskPreviewCheckBox->setObjectName("linearMaskPreviewCheckBox");

        linearMaskOptionsLayout->addWidget(linearMaskPreviewCheckBox);

        linearMaskInvertCheckBox = new QCheckBox(linearMaskGroup);
        linearMaskInvertCheckBox->setObjectName("linearMaskInvertCheckBox");
        linearMaskInvertCheckBox->setChecked(true);

        linearMaskOptionsLayout->addWidget(linearMaskInvertCheckBox);

        linearMaskFormLayout->setLayout(2, QFormLayout::ItemRole::SpanningRole, linearMaskOptionsLayout);

        globalSettingsLayout->addWidget(linearMaskGroup);

        ksigmaGroup = new QGroupBox(globalSettingsScrollContent);
        ksigmaGroup->setObjectName("ksigmaGroup");
        ksigmaGroup->setCheckable(true);
        ksigmaGroup->setChecked(false);
        ksigmaFormLayout = new QFormLayout(ksigmaGroup);
        ksigmaFormLayout->setObjectName("ksigmaFormLayout");
        labelKsigmaK = new QLabel(ksigmaGroup);
        labelKsigmaK->setObjectName("labelKsigmaK");

        ksigmaFormLayout->setWidget(0, QFormLayout::ItemRole::LabelRole, labelKsigmaK);

        ksigmaKLayout = new QHBoxLayout();
        ksigmaKLayout->setObjectName("ksigmaKLayout");
        ksigmaKSlider = new QSlider(ksigmaGroup);
        ksigmaKSlider->setObjectName("ksigmaKSlider");
        ksigmaKSlider->setMaximum(100);
        ksigmaKSlider->setValue(30);
        ksigmaKSlider->setOrientation(Qt::Orientation::Horizontal);

        ksigmaKLayout->addWidget(ksigmaKSlider);

        ksigmaKSpinBox = new QDoubleSpinBox(ksigmaGroup);
        ksigmaKSpinBox->setObjectName("ksigmaKSpinBox");
        ksigmaKSpinBox->setMinimum(0.000000000000000);
        ksigmaKSpinBox->setMaximum(10.000000000000000);
        ksigmaKSpinBox->setSingleStep(0.100000000000000);
        ksigmaKSpinBox->setValue(3.000000000000000);

        ksigmaKLayout->addWidget(ksigmaKSpinBox);

        ksigmaFormLayout->setLayout(0, QFormLayout::ItemRole::FieldRole, ksigmaKLayout);

        labelKsigmaAmount = new QLabel(ksigmaGroup);
        labelKsigmaAmount->setObjectName("labelKsigmaAmount");

        ksigmaFormLayout->setWidget(1, QFormLayout::ItemRole::LabelRole, labelKsigmaAmount);

        ksigmaAmountLayout = new QHBoxLayout();
        ksigmaAmountLayout->setObjectName("ksigmaAmountLayout");
        ksigmaAmountSlider = new QSlider(ksigmaGroup);
        ksigmaAmountSlider->setObjectName("ksigmaAmountSlider");
        ksigmaAmountSlider->setMaximum(100);
        ksigmaAmountSlider->setValue(50);
        ksigmaAmountSlider->setOrientation(Qt::Orientation::Horizontal);

        ksigmaAmountLayout->addWidget(ksigmaAmountSlider);

        ksigmaAmountSpinBox = new QDoubleSpinBox(ksigmaGroup);
        ksigmaAmountSpinBox->setObjectName("ksigmaAmountSpinBox");
        ksigmaAmountSpinBox->setMinimum(0.000000000000000);
        ksigmaAmountSpinBox->setMaximum(1.000000000000000);
        ksigmaAmountSpinBox->setSingleStep(0.050000000000000);
        ksigmaAmountSpinBox->setValue(1.000000000000000);

        ksigmaAmountLayout->addWidget(ksigmaAmountSpinBox);

        ksigmaFormLayout->setLayout(1, QFormLayout::ItemRole::FieldRole, ksigmaAmountLayout);

        ksigmaOptionsLayout = new QHBoxLayout();
        ksigmaOptionsLayout->setObjectName("ksigmaOptionsLayout");
        ksigmaSoftCheckBox = new QCheckBox(ksigmaGroup);
        ksigmaSoftCheckBox->setObjectName("ksigmaSoftCheckBox");
        ksigmaSoftCheckBox->setChecked(true);

        ksigmaOptionsLayout->addWidget(ksigmaSoftCheckBox);

        ksigmaUseMrsCheckBox = new QCheckBox(ksigmaGroup);
        ksigmaUseMrsCheckBox->setObjectName("ksigmaUseMrsCheckBox");

        ksigmaOptionsLayout->addWidget(ksigmaUseMrsCheckBox);

        ksigmaFormLayout->setLayout(2, QFormLayout::ItemRole::SpanningRole, ksigmaOptionsLayout);

        globalSettingsLayout->addWidget(ksigmaGroup);

        deringingGroup = new QGroupBox(globalSettingsScrollContent);
        deringingGroup->setObjectName("deringingGroup");
        deringingGroup->setCheckable(true);
        deringingGroup->setChecked(false);
        deringingFormLayout = new QFormLayout(deringingGroup);
        deringingFormLayout->setObjectName("deringingFormLayout");
        labelDeringingDark = new QLabel(deringingGroup);
        labelDeringingDark->setObjectName("labelDeringingDark");

        deringingFormLayout->setWidget(0, QFormLayout::ItemRole::LabelRole, labelDeringingDark);

        deringingDarkLayout = new QHBoxLayout();
        deringingDarkLayout->setObjectName("deringingDarkLayout");
        deringingDarkSlider = new QSlider(deringingGroup);
        deringingDarkSlider->setObjectName("deringingDarkSlider");
        deringingDarkSlider->setMaximum(100);
        deringingDarkSlider->setOrientation(Qt::Orientation::Horizontal);

        deringingDarkLayout->addWidget(deringingDarkSlider);

        deringingDarkSpinBox = new QDoubleSpinBox(deringingGroup);
        deringingDarkSpinBox->setObjectName("deringingDarkSpinBox");
        deringingDarkSpinBox->setMaximum(1.000000000000000);
        deringingDarkSpinBox->setSingleStep(0.050000000000000);

        deringingDarkLayout->addWidget(deringingDarkSpinBox);

        deringingFormLayout->setLayout(0, QFormLayout::ItemRole::FieldRole, deringingDarkLayout);

        labelDeringingBright = new QLabel(deringingGroup);
        labelDeringingBright->setObjectName("labelDeringingBright");

        deringingFormLayout->setWidget(1, QFormLayout::ItemRole::LabelRole, labelDeringingBright);

        deringingBrightLayout = new QHBoxLayout();
        deringingBrightLayout->setObjectName("deringingBrightLayout");
        deringingBrightSlider = new QSlider(deringingGroup);
        deringingBrightSlider->setObjectName("deringingBrightSlider");
        deringingBrightSlider->setMaximum(100);
        deringingBrightSlider->setOrientation(Qt::Orientation::Horizontal);

        deringingBrightLayout->addWidget(deringingBrightSlider);

        deringingBrightSpinBox = new QDoubleSpinBox(deringingGroup);
        deringingBrightSpinBox->setObjectName("deringingBrightSpinBox");
        deringingBrightSpinBox->setMaximum(1.000000000000000);
        deringingBrightSpinBox->setSingleStep(0.050000000000000);

        deringingBrightLayout->addWidget(deringingBrightSpinBox);

        deringingFormLayout->setLayout(1, QFormLayout::ItemRole::FieldRole, deringingBrightLayout);

        deringingOutputMapsCheckBox = new QCheckBox(deringingGroup);
        deringingOutputMapsCheckBox->setObjectName("deringingOutputMapsCheckBox");

        deringingFormLayout->setWidget(2, QFormLayout::ItemRole::SpanningRole, deringingOutputMapsCheckBox);

        globalSettingsLayout->addWidget(deringingGroup);

        lstfGroup = new QGroupBox(globalSettingsScrollContent);
        lstfGroup->setObjectName("lstfGroup");
        lstfFormLayout = new QFormLayout(lstfGroup);
        lstfFormLayout->setObjectName("lstfFormLayout");
        labelLstfMode = new QLabel(lstfGroup);
        labelLstfMode->setObjectName("labelLstfMode");

        lstfFormLayout->setWidget(0, QFormLayout::ItemRole::LabelRole, labelLstfMode);

        lstfModeComboBox = new QComboBox(lstfGroup);
        lstfModeComboBox->setObjectName("lstfModeComboBox");

        lstfFormLayout->setWidget(0, QFormLayout::ItemRole::FieldRole, lstfModeComboBox);

        labelLstfBreakpoint = new QLabel(lstfGroup);
        labelLstfBreakpoint->setObjectName("labelLstfBreakpoint");

        lstfFormLayout->setWidget(1, QFormLayout::ItemRole::LabelRole, labelLstfBreakpoint);

        lstfBreakpointLayout = new QHBoxLayout();
        lstfBreakpointLayout->setObjectName("lstfBreakpointLayout");
        lstfBreakpointSlider = new QSlider(lstfGroup);
        lstfBreakpointSlider->setObjectName("lstfBreakpointSlider");
        lstfBreakpointSlider->setMaximum(100);
        lstfBreakpointSlider->setValue(50);
        lstfBreakpointSlider->setOrientation(Qt::Orientation::Horizontal);

        lstfBreakpointLayout->addWidget(lstfBreakpointSlider);

        lstfBreakpointSpinBox = new QDoubleSpinBox(lstfGroup);
        lstfBreakpointSpinBox->setObjectName("lstfBreakpointSpinBox");
        lstfBreakpointSpinBox->setMinimum(0.100000000000000);
        lstfBreakpointSpinBox->setMaximum(1.000000000000000);
        lstfBreakpointSpinBox->setSingleStep(0.050000000000000);
        lstfBreakpointSpinBox->setValue(1.000000000000000);

        lstfBreakpointLayout->addWidget(lstfBreakpointSpinBox);

        lstfFormLayout->setLayout(1, QFormLayout::ItemRole::FieldRole, lstfBreakpointLayout);

        globalSettingsLayout->addWidget(lstfGroup);

        dreGroup = new QGroupBox(globalSettingsScrollContent);
        dreGroup->setObjectName("dreGroup");
        dreFormLayout = new QFormLayout(dreGroup);
        dreFormLayout->setObjectName("dreFormLayout");
        labelDreLow = new QLabel(dreGroup);
        labelDreLow->setObjectName("labelDreLow");

        dreFormLayout->setWidget(0, QFormLayout::ItemRole::LabelRole, labelDreLow);

        dreLowLayout = new QHBoxLayout();
        dreLowLayout->setObjectName("dreLowLayout");
        dreLowSlider = new QSlider(dreGroup);
        dreLowSlider->setObjectName("dreLowSlider");
        dreLowSlider->setMaximum(100);
        dreLowSlider->setOrientation(Qt::Orientation::Horizontal);

        dreLowLayout->addWidget(dreLowSlider);

        dreLowSpinBox = new QDoubleSpinBox(dreGroup);
        dreLowSpinBox->setObjectName("dreLowSpinBox");
        dreLowSpinBox->setMaximum(1.000000000000000);
        dreLowSpinBox->setSingleStep(0.050000000000000);

        dreLowLayout->addWidget(dreLowSpinBox);

        dreFormLayout->setLayout(0, QFormLayout::ItemRole::FieldRole, dreLowLayout);

        labelDreHigh = new QLabel(dreGroup);
        labelDreHigh->setObjectName("labelDreHigh");

        dreFormLayout->setWidget(1, QFormLayout::ItemRole::LabelRole, labelDreHigh);

        dreHighLayout = new QHBoxLayout();
        dreHighLayout->setObjectName("dreHighLayout");
        dreHighSlider = new QSlider(dreGroup);
        dreHighSlider->setObjectName("dreHighSlider");
        dreHighSlider->setMaximum(100);
        dreHighSlider->setOrientation(Qt::Orientation::Horizontal);

        dreHighLayout->addWidget(dreHighSlider);

        dreHighSpinBox = new QDoubleSpinBox(dreGroup);
        dreHighSpinBox->setObjectName("dreHighSpinBox");
        dreHighSpinBox->setMaximum(1.000000000000000);
        dreHighSpinBox->setSingleStep(0.050000000000000);

        dreHighLayout->addWidget(dreHighSpinBox);

        dreFormLayout->setLayout(1, QFormLayout::ItemRole::FieldRole, dreHighLayout);

        globalSettingsLayout->addWidget(dreGroup);

        globalSettingsScrollArea->setWidget(globalSettingsScrollContent);

        globalSettingsOuterLayout->addWidget(globalSettingsScrollArea);

        leftPanelLayout->addWidget(globalSettingsGroup);

        leftPanelLayout->setStretch(1, 1);
        leftPanelLayout->setStretch(2, 2);

        rightPanelWidget = new QWidget();
        rightPanelWidget->setObjectName("rightPanelWidget");
        rightPanelLayout = new QVBoxLayout(rightPanelWidget);
        rightPanelLayout->setSpacing(t.spacing.sm);
        rightPanelLayout->setObjectName("rightPanelLayout");
        rightPanelLayout->setContentsMargins(0, 0, 0, 0);
        verticalLayout_2 = new QVBoxLayout();
        verticalLayout_2->setObjectName("verticalLayout_2");
        widget = new QWidget(rightPanelWidget);
        widget->setObjectName("widget");
        QSizePolicy sizePolicy1(QSizePolicy::Policy::Expanding, QSizePolicy::Policy::Expanding);
        sizePolicy1.setHorizontalStretch(0);
        sizePolicy1.setVerticalStretch(1);
        sizePolicy1.setHeightForWidth(widget->sizePolicy().hasHeightForWidth());
        widget->setSizePolicy(sizePolicy1);
        widget->setMinimumSize(QSize(400, 300));

        verticalLayout_2->addWidget(widget);

        rightPanelLayout->addLayout(verticalLayout_2);

        layerInspectorGroup = new QGroupBox(rightPanelWidget);
        layerInspectorGroup->setObjectName("layerInspectorGroup");
        layerInspectorGroup->setMinimumSize(QSize(0, 180));
        layerInspectorLayout = new QVBoxLayout(layerInspectorGroup);
        layerInspectorLayout->setObjectName("layerInspectorLayout");
        layerInspectorLayout->setContentsMargins(t.spacing.md, t.spacing.md, t.spacing.md, t.spacing.md);
        layerHeaderLayout = new QHBoxLayout();
        layerHeaderLayout->setObjectName("layerHeaderLayout");
        layerEnabledCheckBox = new QCheckBox(layerInspectorGroup);
        layerEnabledCheckBox->setObjectName("layerEnabledCheckBox");
        layerEnabledCheckBox->setChecked(true);

        layerHeaderLayout->addWidget(layerEnabledCheckBox);

        layerNameLabel = new QLabel(layerInspectorGroup);
        layerNameLabel->setObjectName("layerNameLabel");
        layerNameLabel->setFont(t.typography.heading3);

        layerHeaderLayout->addWidget(layerNameLabel);

        layerHeaderSpacer = new QSpacerItem(40, 20, QSizePolicy::Policy::Expanding, QSizePolicy::Policy::Minimum);

        layerHeaderLayout->addItem(layerHeaderSpacer);

        copyToAllButton = new QPushButton(layerInspectorGroup);
        copyToAllButton->setObjectName("copyToAllButton");

        layerHeaderLayout->addWidget(copyToAllButton);

        resetCurrentLayerButton = new QPushButton(layerInspectorGroup);
        resetCurrentLayerButton->setObjectName("resetCurrentLayerButton");

        layerHeaderLayout->addWidget(resetCurrentLayerButton);

        layerInspectorLayout->addLayout(layerHeaderLayout);

        biasLayout = new QHBoxLayout();
        biasLayout->setObjectName("biasLayout");
        labelBias = new QLabel(layerInspectorGroup);
        labelBias->setObjectName("labelBias");
        labelBias->setMinimumSize(QSize(80, 0));

        biasLayout->addWidget(labelBias);

        biasSlider = new QSlider(layerInspectorGroup);
        biasSlider->setObjectName("biasSlider");
        biasSlider->setMinimum(-1000);
        biasSlider->setMaximum(1000);
        biasSlider->setValue(0);
        biasSlider->setOrientation(Qt::Orientation::Horizontal);

        biasLayout->addWidget(biasSlider);

        biasSpinBox = new QDoubleSpinBox(layerInspectorGroup);
        biasSpinBox->setObjectName("biasSpinBox");
        biasSpinBox->setMinimum(-1.000000000000000);
        biasSpinBox->setMaximum(15.000000000000000);
        biasSpinBox->setSingleStep(0.010000000000000);
        biasSpinBox->setValue(0.000000000000000);

        biasLayout->addWidget(biasSpinBox);

        layerInspectorLayout->addLayout(biasLayout);

        layerNrGroup = new QGroupBox(layerInspectorGroup);
        layerNrGroup->setObjectName("layerNrGroup");
        layerNrGroup->setCheckable(true);
        layerNrGroup->setChecked(false);
        layerNrFormLayout = new QFormLayout(layerNrGroup);
        layerNrFormLayout->setObjectName("layerNrFormLayout");
        labelNrThreshold = new QLabel(layerNrGroup);
        labelNrThreshold->setObjectName("labelNrThreshold");

        layerNrFormLayout->setWidget(0, QFormLayout::ItemRole::LabelRole, labelNrThreshold);

        nrThresholdLayout = new QHBoxLayout();
        nrThresholdLayout->setObjectName("nrThresholdLayout");
        nrThresholdSlider = new QSlider(layerNrGroup);
        nrThresholdSlider->setObjectName("nrThresholdSlider");
        nrThresholdSlider->setMaximum(100);
        nrThresholdSlider->setValue(30);
        nrThresholdSlider->setOrientation(Qt::Orientation::Horizontal);

        nrThresholdLayout->addWidget(nrThresholdSlider);

        nrThresholdSpinBox = new QDoubleSpinBox(layerNrGroup);
        nrThresholdSpinBox->setObjectName("nrThresholdSpinBox");
        nrThresholdSpinBox->setMinimum(0.000000000000000);
        nrThresholdSpinBox->setMaximum(40.000000000000000);
        nrThresholdSpinBox->setSingleStep(0.100000000000000);
        nrThresholdSpinBox->setValue(3.000000000000000);

        nrThresholdLayout->addWidget(nrThresholdSpinBox);

        layerNrFormLayout->setLayout(0, QFormLayout::ItemRole::FieldRole, nrThresholdLayout);

        labelNrAmount = new QLabel(layerNrGroup);
        labelNrAmount->setObjectName("labelNrAmount");

        layerNrFormLayout->setWidget(1, QFormLayout::ItemRole::LabelRole, labelNrAmount);

        nrAmountLayout = new QHBoxLayout();
        nrAmountLayout->setObjectName("nrAmountLayout");
        nrAmountSlider = new QSlider(layerNrGroup);
        nrAmountSlider->setObjectName("nrAmountSlider");
        nrAmountSlider->setMaximum(100);
        nrAmountSlider->setValue(50);
        nrAmountSlider->setOrientation(Qt::Orientation::Horizontal);

        nrAmountLayout->addWidget(nrAmountSlider);

        nrAmountSpinBox = new QDoubleSpinBox(layerNrGroup);
        nrAmountSpinBox->setObjectName("nrAmountSpinBox");
        nrAmountSpinBox->setMinimum(0.000000000000000);
        nrAmountSpinBox->setMaximum(1.000000000000000);
        nrAmountSpinBox->setSingleStep(0.050000000000000);
        nrAmountSpinBox->setValue(1.000000000000000);

        nrAmountLayout->addWidget(nrAmountSpinBox);

        layerNrFormLayout->setLayout(1, QFormLayout::ItemRole::FieldRole, nrAmountLayout);

        labelNrIterations = new QLabel(layerNrGroup);
        labelNrIterations->setObjectName("labelNrIterations");

        layerNrFormLayout->setWidget(2, QFormLayout::ItemRole::LabelRole, labelNrIterations);

        nrIterationsSpinBox = new QSpinBox(layerNrGroup);
        nrIterationsSpinBox->setObjectName("nrIterationsSpinBox");
        nrIterationsSpinBox->setMinimum(1);
        nrIterationsSpinBox->setMaximum(10);
        nrIterationsSpinBox->setValue(1);

        layerNrFormLayout->setWidget(2, QFormLayout::ItemRole::FieldRole, nrIterationsSpinBox);

        layerInspectorLayout->addWidget(layerNrGroup);

        rightPanelLayout->addWidget(layerInspectorGroup);

        targetFormLayout = new QFormLayout();
        targetFormLayout->setObjectName("targetFormLayout");
        labelTarget = new QLabel(rightPanelWidget);
        labelTarget->setObjectName("labelTarget");

        targetFormLayout->setWidget(0, QFormLayout::ItemRole::LabelRole, labelTarget);

        targetImageSelector = new QComboBox(rightPanelWidget);
        targetImageSelector->setObjectName("targetImageSelector");

        targetFormLayout->setWidget(0, QFormLayout::ItemRole::FieldRole, targetImageSelector);

        labelChannel = new QLabel(rightPanelWidget);
        labelChannel->setObjectName("labelChannel");

        targetFormLayout->setWidget(1, QFormLayout::ItemRole::LabelRole, labelChannel);

        channelSelector = new QComboBox(rightPanelWidget);
        channelSelector->setObjectName("channelSelector");

        targetFormLayout->setWidget(1, QFormLayout::ItemRole::FieldRole, channelSelector);

        rightPanelLayout->addLayout(targetFormLayout);

        previewLayout = new QHBoxLayout();
        previewLayout->setObjectName("previewLayout");
        preview = new QCheckBox(rightPanelWidget);
        preview->setObjectName("preview");
        preview->setChecked(true);

        previewLayout->addWidget(preview);

        previewModeComboBox = new QComboBox(rightPanelWidget);
        previewModeComboBox->setObjectName("previewModeComboBox");
        QSizePolicy sizePolicy2(QSizePolicy::Policy::Expanding, QSizePolicy::Policy::Fixed);
        sizePolicy2.setHorizontalStretch(1);
        sizePolicy2.setVerticalStretch(0);
        sizePolicy2.setHeightForWidth(previewModeComboBox->sizePolicy().hasHeightForWidth());
        previewModeComboBox->setSizePolicy(sizePolicy2);

        previewLayout->addWidget(previewModeComboBox);

        previewSpacer = new QSpacerItem(40, 20, QSizePolicy::Policy::Expanding, QSizePolicy::Policy::Minimum);

        previewLayout->addItem(previewSpacer);

        rightPanelLayout->addLayout(previewLayout);

        buttonBox = new QDialogButtonBox(rightPanelWidget);
        buttonBox->setObjectName("buttonBox");
        buttonBox->setStandardButtons(QDialogButtonBox::StandardButton::Apply |
                                      QDialogButtonBox::StandardButton::Close |
                                      QDialogButtonBox::StandardButton::Reset);

        rightPanelLayout->addWidget(buttonBox);

        rightPanelLayout->setStretch(0, 1);

        mainSplitter->addWidget(leftPanelWidget);
        mainSplitter->addWidget(rightPanelWidget);
        mainSplitter->setStretchFactor(0, 0);
        mainSplitter->setStretchFactor(1, 1);
        int previewWidth = MultiscaleLinearProcess->width() - 560;
        if (previewWidth < 1)
        {
            previewWidth = 1;
        }
        mainSplitter->setSizes({560, previewWidth});

        horizontalLayoutMain->addWidget(mainSplitter);

        retranslateUi(MultiscaleLinearProcess);

        QMetaObject::connectSlotsByName(MultiscaleLinearProcess);
    } // setupUi

    void retranslateUi(QWidget* MultiscaleLinearProcess)
    {
        MultiscaleLinearProcess->setWindowTitle(
            QCoreApplication::translate("MultiscaleLinearProcess", "Multiscale Linear Transform", nullptr));
        transformSettingsGroup->setTitle(
            QCoreApplication::translate("MultiscaleLinearProcess", "Transform Settings", nullptr));
        labelAlgorithm->setText(QCoreApplication::translate("MultiscaleLinearProcess", "Algorithm", nullptr));
        labelLayers->setText(QCoreApplication::translate("MultiscaleLinearProcess", "Detail Layers", nullptr));
        labelSpacing->setText(QCoreApplication::translate("MultiscaleLinearProcess", "Layer Spacing", nullptr));
        labelLinearScale->setText(QCoreApplication::translate("MultiscaleLinearProcess", "Linear Scale", nullptr));
        labelScaling->setText(QCoreApplication::translate("MultiscaleLinearProcess", "Scaling Function", nullptr));
        labelAmount->setText(QCoreApplication::translate("MultiscaleLinearProcess", "Amount", nullptr));
        layerTreeGroup->setTitle(QCoreApplication::translate("MultiscaleLinearProcess", "Layers", nullptr));
        QTreeWidgetItem* ___qtreewidgetitem = layerTreeWidget->headerItem();
        ___qtreewidgetitem->setText(3, QCoreApplication::translate("MultiscaleLinearProcess", "NR", nullptr));
        ___qtreewidgetitem->setText(2, QCoreApplication::translate("MultiscaleLinearProcess", "Bias", nullptr));
        ___qtreewidgetitem->setText(1, QCoreApplication::translate("MultiscaleLinearProcess", "Layer", nullptr));
        ___qtreewidgetitem->setText(0, QCoreApplication::translate("MultiscaleLinearProcess", "En", nullptr));
#if QT_CONFIG(tooltip)
        ___qtreewidgetitem->setToolTip(
            3, QCoreApplication::translate("MultiscaleLinearProcess", "Noise Reduction enabled", nullptr));
        ___qtreewidgetitem->setToolTip(
            0, QCoreApplication::translate("MultiscaleLinearProcess", "Enable/Disable layer", nullptr));
#endif // QT_CONFIG(tooltip)
#if QT_CONFIG(tooltip)
        enableAllLayersButton->setToolTip(
            QCoreApplication::translate("MultiscaleLinearProcess", "Enable all layers", nullptr));
#endif // QT_CONFIG(tooltip)
        enableAllLayersButton->setText(QCoreApplication::translate("MultiscaleLinearProcess", "Enable All", nullptr));
#if QT_CONFIG(tooltip)
        disableAllLayersButton->setToolTip(
            QCoreApplication::translate("MultiscaleLinearProcess", "Disable all layers", nullptr));
#endif // QT_CONFIG(tooltip)
        disableAllLayersButton->setText(QCoreApplication::translate("MultiscaleLinearProcess", "Disable All", nullptr));
#if QT_CONFIG(tooltip)
        resetBiasesButton->setToolTip(
            QCoreApplication::translate("MultiscaleLinearProcess", "Reset all layer biases to 0", nullptr));
#endif // QT_CONFIG(tooltip)
        resetBiasesButton->setText(QCoreApplication::translate("MultiscaleLinearProcess", "Reset Biases", nullptr));
        globalSettingsGroup->setTitle(
            QCoreApplication::translate("MultiscaleLinearProcess", "Global Settings", nullptr));
        linearMaskGroup->setTitle(QCoreApplication::translate("MultiscaleLinearProcess", "Linear Mask", nullptr));
        labelLinearMaskAmplification->setText(
            QCoreApplication::translate("MultiscaleLinearProcess", "Amplification", nullptr));
        labelLinearMaskSmoothness->setText(
            QCoreApplication::translate("MultiscaleLinearProcess", "Smoothness", nullptr));
        linearMaskPreviewCheckBox->setText(
            QCoreApplication::translate("MultiscaleLinearProcess", "Preview Mask", nullptr));
        linearMaskInvertCheckBox->setText(QCoreApplication::translate("MultiscaleLinearProcess", "Inverted", nullptr));
        ksigmaGroup->setTitle(QCoreApplication::translate("MultiscaleLinearProcess", "k-Sigma Thresholding", nullptr));
        labelKsigmaK->setText(QCoreApplication::translate("MultiscaleLinearProcess", "Threshold (k)", nullptr));
        labelKsigmaAmount->setText(QCoreApplication::translate("MultiscaleLinearProcess", "Amount", nullptr));
        ksigmaSoftCheckBox->setText(QCoreApplication::translate("MultiscaleLinearProcess", "Soft Threshold", nullptr));
#if QT_CONFIG(tooltip)
        ksigmaUseMrsCheckBox->setToolTip(QCoreApplication::translate(
            "MultiscaleLinearProcess", "Use Multiresolution Support for sigma estimation", nullptr));
#endif // QT_CONFIG(tooltip)
        ksigmaUseMrsCheckBox->setText(QCoreApplication::translate("MultiscaleLinearProcess", "Use MRS", nullptr));
        deringingGroup->setTitle(QCoreApplication::translate("MultiscaleLinearProcess", "Deringing", nullptr));
        labelDeringingDark->setText(QCoreApplication::translate("MultiscaleLinearProcess", "Dark", nullptr));
        labelDeringingBright->setText(QCoreApplication::translate("MultiscaleLinearProcess", "Bright", nullptr));
        deringingOutputMapsCheckBox->setText(
            QCoreApplication::translate("MultiscaleLinearProcess", "Output Deringing Maps", nullptr));
        lstfGroup->setTitle(
            QCoreApplication::translate("MultiscaleLinearProcess", "Large-Scale Transfer Function", nullptr));
        labelLstfMode->setText(QCoreApplication::translate("MultiscaleLinearProcess", "Mode", nullptr));
        labelLstfBreakpoint->setText(QCoreApplication::translate("MultiscaleLinearProcess", "Breakpoint", nullptr));
        dreGroup->setTitle(QCoreApplication::translate("MultiscaleLinearProcess", "Dynamic Range Extension", nullptr));
        labelDreLow->setText(QCoreApplication::translate("MultiscaleLinearProcess", "Low Range", nullptr));
        labelDreHigh->setText(QCoreApplication::translate("MultiscaleLinearProcess", "High Range", nullptr));
        layerInspectorGroup->setTitle(
            QCoreApplication::translate("MultiscaleLinearProcess", "Selected Layer", nullptr));
        layerEnabledCheckBox->setText(QCoreApplication::translate("MultiscaleLinearProcess", "Enabled", nullptr));
        layerNameLabel->setText(QCoreApplication::translate("MultiscaleLinearProcess", "No layer selected", nullptr));
#if QT_CONFIG(tooltip)
        copyToAllButton->setToolTip(QCoreApplication::translate(
            "MultiscaleLinearProcess", "Copy this layer's settings to all other layers", nullptr));
#endif // QT_CONFIG(tooltip)
        copyToAllButton->setText(QCoreApplication::translate("MultiscaleLinearProcess", "Copy to All", nullptr));
        resetCurrentLayerButton->setText(
            QCoreApplication::translate("MultiscaleLinearProcess", "Reset Layer", nullptr));
        labelBias->setText(QCoreApplication::translate("MultiscaleLinearProcess", "Bias", nullptr));
#if QT_CONFIG(tooltip)
        biasSpinBox->setToolTip(QCoreApplication::translate(
            "MultiscaleLinearProcess",
            "PixInsight-style layer bias: the layer is multiplied by (1 + bias). Positive values enhance structures "
            "(and noise); negative values reduce the layer contribution.",
            nullptr));
#endif // QT_CONFIG(tooltip)
        layerNrGroup->setTitle(QCoreApplication::translate("MultiscaleLinearProcess", "Noise Reduction", nullptr));
        labelNrThreshold->setText(
            QCoreApplication::translate("MultiscaleLinearProcess", "Threshold (\317\203)", nullptr));
        labelNrAmount->setText(QCoreApplication::translate("MultiscaleLinearProcess", "Amount", nullptr));
        labelNrIterations->setText(QCoreApplication::translate("MultiscaleLinearProcess", "Iterations", nullptr));
        labelTarget->setText(QCoreApplication::translate("MultiscaleLinearProcess", "Target Image", nullptr));
        labelChannel->setText(QCoreApplication::translate("MultiscaleLinearProcess", "Channel", nullptr));
        preview->setText(QCoreApplication::translate("MultiscaleLinearProcess", "Preview", nullptr));
#if QT_CONFIG(tooltip)
        previewModeComboBox->setToolTip(QCoreApplication::translate(
            "MultiscaleLinearProcess", "Select what to show in the preview window", nullptr));
#endif // QT_CONFIG(tooltip)
    } // retranslateUi
};

inline MultiscaleLinearProcessUiElements buildMultiscaleLinearProcessUi(QWidget* parent)
{
    MultiscaleLinearProcessUiElements ui;
    ui.setupUi(parent);
    return ui;
}

} // namespace an::ui::processes
