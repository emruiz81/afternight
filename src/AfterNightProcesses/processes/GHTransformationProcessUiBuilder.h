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
#include <QtWidgets/QFormLayout>
#include <QtWidgets/QFrame>
#include <QtWidgets/QGridLayout>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QScrollArea>
#include <QtWidgets/QSlider>
#include <QtWidgets/QSpacerItem>
#include <QtWidgets/QToolButton>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QWidget>

#include "processes/RTPreviewUiCommon.h"
#include "theme/AnDesignTokens.h"

namespace an::ui::processes
{

class GHTransformationProcessUiElements
{
public:
    QWidget* horizontalLayoutWidget_4;
    QHBoxLayout* horizontalLayoutMain;
    QVBoxLayout* verticalLayout;
    QHBoxLayout* previewChartLayout;
    QScrollArea* previewChartScrollArea;
    QWidget* scrollAreaWidgetContents;
    QHBoxLayout* originalChartLayout;
    QScrollArea* originalChartScrollArea;
    QWidget* scrollAreaWidgetContents_4;
    QHBoxLayout* histogramSettingsLayout_2;
    QToolButton* zoomIn;
    QToolButton* zoomOut;
    QToolButton* zoomReset;
    QFrame* line_3;
    QToolButton* gridButton;
    QToolButton* transferCurve;
    QToolButton* showClippingInfo;
    QToolButton* localChannelNormalization;
    QToolButton* showHistogramAreas;
    QFrame* line_4;
    QComboBox* plotResolution;
    QSpacerItem* horizontalSpacer_2;
    QHBoxLayout* channelSelectorLayout_2;
    QLabel* label_4;
    QComboBox* channelSelector2;
    QCheckBox* humanWeightedLuminance;
    QLabel* colourClipLabel;
    QComboBox* colourClipSelector;
    QLabel* lumCoeffSourceLabel;
    QComboBox* lumCoeffSourceSelector;
    QLabel* lumCoeffLabel;
    QDoubleSpinBox* lumCoeffRValue;
    QDoubleSpinBox* lumCoeffGValue;
    QDoubleSpinBox* lumCoeffBValue;
    QSpacerItem* horizontalSpacer_6;
    QHBoxLayout* horizontalLayout;
    QLabel* label;
    QComboBox* transformationTypeSelector;
    QCheckBox* invert;
    QCheckBox* linkedStf;
    QHBoxLayout* horizontalLayout_2;
    QGridLayout* gridLayout;
    QLabel* label_9;
    QDoubleSpinBox* stretchFactorValue;
    QSlider* stretchFactorSlider;
    QLabel* label_11;
    QDoubleSpinBox* localIntensityValue;
    QSlider* localIntensitySlider;
    QLabel* label_13;
    QDoubleSpinBox* symmetryPointValue;
    QSlider* symmetryPointSlider;
    QLabel* label_2;
    QDoubleSpinBox* protectShadowsValue;
    QSlider* protectShadowsSlider;
    QLabel* label_5;
    QDoubleSpinBox* protectHighlightsValue;
    QSlider* protectHighlightsSlider;
    QLabel* blackPointLabel;
    QDoubleSpinBox* blackPointValue;
    QSlider* blackPointSlider;
    QLabel* whitePointLabel;
    QDoubleSpinBox* whitePointValue;
    QSlider* whitePointSlider;
    QHBoxLayout* horizontalLayout_5;
    QLabel* label_6;
    QPushButton* spFromImage;
    QPushButton* spFromImageArea;
    QPushButton* spFromHistogram;
    QLabel* spText;
    QSpacerItem* verticalSpacer;
    QFormLayout* formLayout;
    QLabel* label_3;
    QComboBox* targetImageSelector;
    QHBoxLayout* horizontalLayout_4;
    QCheckBox* preview;
    QSpacerItem* horizontalSpacer;
    QHBoxLayout* horizontalLayout_3;
    QDialogButtonBox* buttonBox;
    QVBoxLayout* verticalLayout_2;
    QWidget* widget;

    void setupUi(QWidget* GHTransformationProcess)
    {
        const auto& t = an::ui::tokens();
        if (GHTransformationProcess->objectName().isEmpty())
            GHTransformationProcess->setObjectName("GHTransformationProcess");
        GHTransformationProcess->resize(1372, 1032);
        GHTransformationProcess->setMinimumSize(QSize(460, 760));
        GHTransformationProcess->setSizePolicy(QSizePolicy::Policy::Preferred, QSizePolicy::Policy::Preferred);
        auto* mainLayout = new QVBoxLayout(GHTransformationProcess);
        mainLayout->setObjectName("mainLayout");
        mainLayout->setContentsMargins(t.spacing.md, t.spacing.md, t.spacing.md, t.spacing.md);
        mainLayout->setSpacing(0);
        horizontalLayoutWidget_4 = new QWidget(GHTransformationProcess);
        horizontalLayoutWidget_4->setObjectName("horizontalLayoutWidget_4");
        mainLayout->addWidget(horizontalLayoutWidget_4);
        horizontalLayoutMain = new QHBoxLayout(horizontalLayoutWidget_4);
        horizontalLayoutMain->setSpacing(t.spacing.md);
        horizontalLayoutMain->setObjectName("horizontalLayoutMain");
        horizontalLayoutMain->setSizeConstraint(QLayout::SizeConstraint::SetDefaultConstraint);
        horizontalLayoutMain->setContentsMargins(t.spacing.md, t.spacing.md, t.spacing.md, t.spacing.md);
        verticalLayout = new QVBoxLayout();
        verticalLayout->setObjectName("verticalLayout");
        verticalLayout->setSizeConstraint(QLayout::SizeConstraint::SetDefaultConstraint);
        verticalLayout->setContentsMargins(-1, t.spacing.md, -1, -1);
        previewChartLayout = new QHBoxLayout();
        previewChartLayout->setObjectName("previewChartLayout");
        previewChartLayout->setContentsMargins(0, 0, 0, 0);
        previewChartScrollArea = new QScrollArea(horizontalLayoutWidget_4);
        previewChartScrollArea->setObjectName("previewChartScrollArea");
        previewChartScrollArea->setMinimumSize(QSize(510, 250));
        previewChartScrollArea->setWidgetResizable(true);
        scrollAreaWidgetContents = new QWidget();
        scrollAreaWidgetContents->setObjectName("scrollAreaWidgetContents");
        scrollAreaWidgetContents->setMinimumSize(QSize(508, 248));
        previewChartScrollArea->setWidget(scrollAreaWidgetContents);

        previewChartLayout->addWidget(previewChartScrollArea);

        verticalLayout->addLayout(previewChartLayout);

        originalChartLayout = new QHBoxLayout();
        originalChartLayout->setObjectName("originalChartLayout");
        originalChartLayout->setSizeConstraint(QLayout::SizeConstraint::SetDefaultConstraint);
        originalChartLayout->setContentsMargins(0, 0, 0, 0);
        originalChartScrollArea = new QScrollArea(horizontalLayoutWidget_4);
        originalChartScrollArea->setObjectName("originalChartScrollArea");
        originalChartScrollArea->setMinimumSize(QSize(510, 250));
        originalChartScrollArea->setWidgetResizable(true);
        scrollAreaWidgetContents_4 = new QWidget();
        scrollAreaWidgetContents_4->setObjectName("scrollAreaWidgetContents_4");
        scrollAreaWidgetContents_4->setMinimumSize(QSize(508, 248));
        originalChartScrollArea->setWidget(scrollAreaWidgetContents_4);

        originalChartLayout->addWidget(originalChartScrollArea);

        verticalLayout->addLayout(originalChartLayout);

        histogramSettingsLayout_2 = new QHBoxLayout();
        histogramSettingsLayout_2->setObjectName("histogramSettingsLayout_2");
        histogramSettingsLayout_2->setContentsMargins(t.spacing.md, -1, t.spacing.md, -1);
        zoomIn = new QToolButton(horizontalLayoutWidget_4);
        zoomIn->setObjectName("zoomIn");
        rt_preview_ui::initToolbarButton(zoomIn,
                                         QCoreApplication::translate("GHTransformationProcess", "Zoom in", nullptr),
                                         QStringLiteral("zoom_in"));

        histogramSettingsLayout_2->addWidget(zoomIn);

        zoomOut = new QToolButton(horizontalLayoutWidget_4);
        zoomOut->setObjectName("zoomOut");
        rt_preview_ui::initToolbarButton(zoomOut,
                                         QCoreApplication::translate("GHTransformationProcess", "Zoom out", nullptr),
                                         QStringLiteral("zoom_out"));

        histogramSettingsLayout_2->addWidget(zoomOut);

        zoomReset = new QToolButton(horizontalLayoutWidget_4);
        zoomReset->setObjectName("zoomReset");
        rt_preview_ui::initToolbarButton(zoomReset,
                                         QCoreApplication::translate("GHTransformationProcess", "Zoom reset", nullptr),
                                         QStringLiteral("zoom_fit"));
        zoomReset->setCheckable(false);

        histogramSettingsLayout_2->addWidget(zoomReset);

        line_3 = new QFrame(horizontalLayoutWidget_4);
        line_3->setObjectName("line_3");
        line_3->setFrameShape(QFrame::Shape::VLine);
        line_3->setFrameShadow(QFrame::Shadow::Sunken);

        histogramSettingsLayout_2->addWidget(line_3);

        gridButton = new QToolButton(horizontalLayoutWidget_4);
        gridButton->setObjectName("gridButton");
        rt_preview_ui::initToolbarButton(gridButton,
                                         QCoreApplication::translate("GHTransformationProcess", "Show grid", nullptr),
                                         QStringLiteral("grid"));
        gridButton->setCheckable(true);
        gridButton->setChecked(true);

        histogramSettingsLayout_2->addWidget(gridButton);

        transferCurve = new QToolButton(horizontalLayoutWidget_4);
        transferCurve->setObjectName("transferCurve");
        rt_preview_ui::initToolbarButton(
            transferCurve, QCoreApplication::translate("GHTransformationProcess", "Show transfer curve", nullptr),
            QStringLiteral("stat_line"));
        transferCurve->setCheckable(true);
        transferCurve->setChecked(true);

        histogramSettingsLayout_2->addWidget(transferCurve);

        showClippingInfo = new QToolButton(horizontalLayoutWidget_4);
        showClippingInfo->setObjectName("showClippingInfo");
        rt_preview_ui::initToolbarButton(
            showClippingInfo, QCoreApplication::translate("GHTransformationProcess", "Show clipping info", nullptr),
            QStringLiteral("info"));
        showClippingInfo->setCheckable(true);
        showClippingInfo->setChecked(true);

        histogramSettingsLayout_2->addWidget(showClippingInfo);

        localChannelNormalization = new QToolButton(horizontalLayoutWidget_4);
        localChannelNormalization->setObjectName("localChannelNormalization");
        rt_preview_ui::initToolbarButton(
            localChannelNormalization,
            QCoreApplication::translate("GHTransformationProcess", "Absolute scales", nullptr),
            QStringLiteral("scale"));
        localChannelNormalization->setCheckable(true);
        localChannelNormalization->setChecked(false);

        histogramSettingsLayout_2->addWidget(localChannelNormalization);

        showHistogramAreas = new QToolButton(horizontalLayoutWidget_4);
        showHistogramAreas->setObjectName("showHistogramAreas");
        rt_preview_ui::initToolbarButton(
            showHistogramAreas, QCoreApplication::translate("GHTransformationProcess", "Draw histogram areas", nullptr),
            QStringLiteral("area_chart"));
        showHistogramAreas->setCheckable(true);

        histogramSettingsLayout_2->addWidget(showHistogramAreas);

        line_4 = new QFrame(horizontalLayoutWidget_4);
        line_4->setObjectName("line_4");
        line_4->setFrameShape(QFrame::Shape::VLine);
        line_4->setFrameShadow(QFrame::Shadow::Sunken);

        histogramSettingsLayout_2->addWidget(line_4);

        plotResolution = new QComboBox(horizontalLayoutWidget_4);
        plotResolution->setObjectName("plotResolution");

        histogramSettingsLayout_2->addWidget(plotResolution);

        horizontalSpacer_2 = new QSpacerItem(40, 20, QSizePolicy::Policy::Expanding, QSizePolicy::Policy::Minimum);

        histogramSettingsLayout_2->addItem(horizontalSpacer_2);

        verticalLayout->addLayout(histogramSettingsLayout_2);

        channelSelectorLayout_2 = new QHBoxLayout();
        channelSelectorLayout_2->setObjectName("channelSelectorLayout_2");
        channelSelectorLayout_2->setContentsMargins(t.spacing.md, -1, t.spacing.md, -1);
        label_4 = new QLabel(horizontalLayoutWidget_4);
        label_4->setObjectName("label_4");

        channelSelectorLayout_2->addWidget(label_4);

        channelSelector2 = new QComboBox(horizontalLayoutWidget_4);
        channelSelector2->setObjectName("channelSelector2");
        channelSelector2->setMinimumWidth(150);
        channelSelector2->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);

        channelSelectorLayout_2->addWidget(channelSelector2);

        humanWeightedLuminance = new QCheckBox(horizontalLayoutWidget_4);
        humanWeightedLuminance->setObjectName("humanWeightedLuminance");

        channelSelectorLayout_2->addWidget(humanWeightedLuminance);

        colourClipLabel = new QLabel(horizontalLayoutWidget_4);
        colourClipLabel->setObjectName("colourClipLabel");

        channelSelectorLayout_2->addWidget(colourClipLabel);

        colourClipSelector = new QComboBox(horizontalLayoutWidget_4);
        colourClipSelector->setObjectName("colourClipSelector");
        colourClipSelector->addItem(QStringLiteral("Clip"));
        colourClipSelector->addItem(QStringLiteral("Rescale"));

        channelSelectorLayout_2->addWidget(colourClipSelector);

        lumCoeffSourceLabel = new QLabel(horizontalLayoutWidget_4);
        lumCoeffSourceLabel->setObjectName("lumCoeffSourceLabel");

        channelSelectorLayout_2->addWidget(lumCoeffSourceLabel);

        lumCoeffSourceSelector = new QComboBox(horizontalLayoutWidget_4);
        lumCoeffSourceSelector->setObjectName("lumCoeffSourceSelector");
        lumCoeffSourceSelector->addItem(QStringLiteral("Default"));
        lumCoeffSourceSelector->addItem(QStringLiteral("Image"));
        lumCoeffSourceSelector->addItem(QStringLiteral("Manual"));

        channelSelectorLayout_2->addWidget(lumCoeffSourceSelector);

        lumCoeffLabel = new QLabel(horizontalLayoutWidget_4);
        lumCoeffLabel->setObjectName("lumCoeffLabel");

        channelSelectorLayout_2->addWidget(lumCoeffLabel);

        lumCoeffRValue = new QDoubleSpinBox(horizontalLayoutWidget_4);
        lumCoeffRValue->setObjectName("lumCoeffRValue");
        lumCoeffRValue->setDecimals(4);
        lumCoeffRValue->setRange(0.0000, 10.0000);
        lumCoeffRValue->setSingleStep(0.1000);
        lumCoeffRValue->setValue(1.0000);
        lumCoeffRValue->setMinimumWidth(70);

        channelSelectorLayout_2->addWidget(lumCoeffRValue);

        lumCoeffGValue = new QDoubleSpinBox(horizontalLayoutWidget_4);
        lumCoeffGValue->setObjectName("lumCoeffGValue");
        lumCoeffGValue->setDecimals(4);
        lumCoeffGValue->setRange(0.0000, 10.0000);
        lumCoeffGValue->setSingleStep(0.1000);
        lumCoeffGValue->setValue(1.0000);
        lumCoeffGValue->setMinimumWidth(70);

        channelSelectorLayout_2->addWidget(lumCoeffGValue);

        lumCoeffBValue = new QDoubleSpinBox(horizontalLayoutWidget_4);
        lumCoeffBValue->setObjectName("lumCoeffBValue");
        lumCoeffBValue->setDecimals(4);
        lumCoeffBValue->setRange(0.0000, 10.0000);
        lumCoeffBValue->setSingleStep(0.1000);
        lumCoeffBValue->setValue(1.0000);
        lumCoeffBValue->setMinimumWidth(70);

        channelSelectorLayout_2->addWidget(lumCoeffBValue);

        horizontalSpacer_6 = new QSpacerItem(40, 20, QSizePolicy::Policy::Expanding, QSizePolicy::Policy::Minimum);

        channelSelectorLayout_2->addItem(horizontalSpacer_6);

        channelSelectorLayout_2->setStretch(1, 2);
        channelSelectorLayout_2->setStretch(11, 8);

        verticalLayout->addLayout(channelSelectorLayout_2);

        horizontalLayout = new QHBoxLayout();
        horizontalLayout->setObjectName("horizontalLayout");
        horizontalLayout->setContentsMargins(t.spacing.md, -1, t.spacing.md, -1);
        label = new QLabel(horizontalLayoutWidget_4);
        label->setObjectName("label");

        horizontalLayout->addWidget(label);

        transformationTypeSelector = new QComboBox(horizontalLayoutWidget_4);
        transformationTypeSelector->setObjectName("transformationTypeSelector");
        transformationTypeSelector->setMinimumWidth(150);
        transformationTypeSelector->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        transformationTypeSelector->addItem(QStringLiteral("Generalized Hyperbolic"));
        transformationTypeSelector->addItem(QStringLiteral("Histogram Transformation"));
        transformationTypeSelector->addItem(QStringLiteral("Arcsinh"));
        transformationTypeSelector->addItem(QStringLiteral("Linear Stretch"));
        transformationTypeSelector->addItem(QStringLiteral("Image Inversion"));
        transformationTypeSelector->addItem(QStringLiteral("STF Transformation"));

        horizontalLayout->addWidget(transformationTypeSelector);

        invert = new QCheckBox(horizontalLayoutWidget_4);
        invert->setObjectName("invert");

        horizontalLayout->addWidget(invert);

        linkedStf = new QCheckBox(horizontalLayoutWidget_4);
        linkedStf->setObjectName("linkedStf");

        horizontalLayout->addWidget(linkedStf);

        horizontalLayout->setStretch(1, 2);
        horizontalLayout->setStretch(2, 1);

        verticalLayout->addLayout(horizontalLayout);

        horizontalLayout_2 = new QHBoxLayout();
        horizontalLayout_2->setObjectName("horizontalLayout_2");
        horizontalLayout_2->setContentsMargins(t.spacing.md, -1, t.spacing.md, -1);

        verticalLayout->addLayout(horizontalLayout_2);

        gridLayout = new QGridLayout();
        gridLayout->setObjectName("gridLayout");
        gridLayout->setContentsMargins(t.spacing.md, -1, t.spacing.md, -1);
        label_9 = new QLabel(horizontalLayoutWidget_4);
        label_9->setObjectName("label_9");

        gridLayout->addWidget(label_9, 0, 0, 1, 1);

        stretchFactorValue = new QDoubleSpinBox(horizontalLayoutWidget_4);
        stretchFactorValue->setObjectName("stretchFactorValue");
        stretchFactorValue->setDecimals(4);
        stretchFactorValue->setMaximum(20.000000000000000);
        stretchFactorValue->setSingleStep(0.010000000000000);
        stretchFactorValue->setStepType(QAbstractSpinBox::StepType::DefaultStepType);
        stretchFactorValue->setMinimumWidth(80);

        gridLayout->addWidget(stretchFactorValue, 0, 1, 1, 1);

        stretchFactorSlider = new QSlider(horizontalLayoutWidget_4);
        stretchFactorSlider->setObjectName("stretchFactorSlider");
        stretchFactorSlider->setMaximum(20000);
        stretchFactorSlider->setOrientation(Qt::Orientation::Horizontal);

        gridLayout->addWidget(stretchFactorSlider, 0, 2, 1, 1);

        label_11 = new QLabel(horizontalLayoutWidget_4);
        label_11->setObjectName("label_11");

        gridLayout->addWidget(label_11, 1, 0, 1, 1);

        localIntensityValue = new QDoubleSpinBox(horizontalLayoutWidget_4);
        localIntensityValue->setObjectName("localIntensityValue");
        localIntensityValue->setDecimals(4);
        localIntensityValue->setMinimum(-5.000000000000000);
        localIntensityValue->setMaximum(15.000000000000000);
        localIntensityValue->setSingleStep(0.010000000000000);
        localIntensityValue->setStepType(QAbstractSpinBox::StepType::DefaultStepType);
        localIntensityValue->setValue(0.000000000000000);
        localIntensityValue->setMinimumWidth(80);

        gridLayout->addWidget(localIntensityValue, 1, 1, 1, 1);

        localIntensitySlider = new QSlider(horizontalLayoutWidget_4);
        localIntensitySlider->setObjectName("localIntensitySlider");
        localIntensitySlider->setMinimum(-5000);
        localIntensitySlider->setMaximum(15000);
        localIntensitySlider->setOrientation(Qt::Orientation::Horizontal);

        gridLayout->addWidget(localIntensitySlider, 1, 2, 1, 1);

        label_13 = new QLabel(horizontalLayoutWidget_4);
        label_13->setObjectName("label_13");

        gridLayout->addWidget(label_13, 2, 0, 1, 1);

        symmetryPointValue = new QDoubleSpinBox(horizontalLayoutWidget_4);
        symmetryPointValue->setObjectName("symmetryPointValue");
        symmetryPointValue->setDecimals(4);
        symmetryPointValue->setMaximum(1.000000000000000);
        symmetryPointValue->setSingleStep(0.010000000000000);
        symmetryPointValue->setStepType(QAbstractSpinBox::StepType::DefaultStepType);
        symmetryPointValue->setValue(0.000000000000000);
        symmetryPointValue->setMinimumWidth(80);

        gridLayout->addWidget(symmetryPointValue, 2, 1, 1, 1);

        symmetryPointSlider = new QSlider(horizontalLayoutWidget_4);
        symmetryPointSlider->setObjectName("symmetryPointSlider");
        symmetryPointSlider->setMaximum(1000);
        symmetryPointSlider->setOrientation(Qt::Orientation::Horizontal);

        gridLayout->addWidget(symmetryPointSlider, 2, 2, 1, 1);

        label_2 = new QLabel(horizontalLayoutWidget_4);
        label_2->setObjectName("label_2");

        gridLayout->addWidget(label_2, 3, 0, 1, 1);

        protectShadowsValue = new QDoubleSpinBox(horizontalLayoutWidget_4);
        protectShadowsValue->setObjectName("protectShadowsValue");
        protectShadowsValue->setDecimals(4);
        protectShadowsValue->setMaximum(1.000000000000000);
        protectShadowsValue->setSingleStep(0.010000000000000);
        protectShadowsValue->setStepType(QAbstractSpinBox::StepType::DefaultStepType);
        protectShadowsValue->setValue(0.000000000000000);
        protectShadowsValue->setMinimumWidth(80);

        gridLayout->addWidget(protectShadowsValue, 3, 1, 1, 1);

        protectShadowsSlider = new QSlider(horizontalLayoutWidget_4);
        protectShadowsSlider->setObjectName("protectShadowsSlider");
        protectShadowsSlider->setMaximum(1000);
        protectShadowsSlider->setOrientation(Qt::Orientation::Horizontal);

        gridLayout->addWidget(protectShadowsSlider, 3, 2, 1, 1);

        label_5 = new QLabel(horizontalLayoutWidget_4);
        label_5->setObjectName("label_5");

        gridLayout->addWidget(label_5, 4, 0, 1, 1);

        protectHighlightsValue = new QDoubleSpinBox(horizontalLayoutWidget_4);
        protectHighlightsValue->setObjectName("protectHighlightsValue");
        protectHighlightsValue->setDecimals(4);
        protectHighlightsValue->setMaximum(1.000000000000000);
        protectHighlightsValue->setSingleStep(0.010000000000000);
        protectHighlightsValue->setStepType(QAbstractSpinBox::StepType::DefaultStepType);
        protectHighlightsValue->setValue(1.000000000000000);
        protectHighlightsValue->setMinimumWidth(80);

        gridLayout->addWidget(protectHighlightsValue, 4, 1, 1, 1);

        protectHighlightsSlider = new QSlider(horizontalLayoutWidget_4);
        protectHighlightsSlider->setObjectName("protectHighlightsSlider");
        protectHighlightsSlider->setMaximum(1000);
        protectHighlightsSlider->setValue(1000);
        protectHighlightsSlider->setOrientation(Qt::Orientation::Horizontal);

        gridLayout->addWidget(protectHighlightsSlider, 4, 2, 1, 1);

        blackPointLabel = new QLabel(horizontalLayoutWidget_4);
        blackPointLabel->setObjectName("blackPointLabel");

        gridLayout->addWidget(blackPointLabel, 5, 0, 1, 1);

        blackPointValue = new QDoubleSpinBox(horizontalLayoutWidget_4);
        blackPointValue->setObjectName("blackPointValue");
        blackPointValue->setDecimals(4);
        blackPointValue->setMaximum(1.000000000000000);
        blackPointValue->setSingleStep(0.010000000000000);
        blackPointValue->setStepType(QAbstractSpinBox::StepType::DefaultStepType);
        blackPointValue->setValue(0.000000000000000);
        blackPointValue->setMinimumWidth(80);

        gridLayout->addWidget(blackPointValue, 5, 1, 1, 1);

        blackPointSlider = new QSlider(horizontalLayoutWidget_4);
        blackPointSlider->setObjectName("blackPointSlider");
        blackPointSlider->setMaximum(1000);
        blackPointSlider->setOrientation(Qt::Orientation::Horizontal);

        gridLayout->addWidget(blackPointSlider, 5, 2, 1, 1);

        whitePointLabel = new QLabel(horizontalLayoutWidget_4);
        whitePointLabel->setObjectName("whitePointLabel");

        gridLayout->addWidget(whitePointLabel, 6, 0, 1, 1);

        whitePointValue = new QDoubleSpinBox(horizontalLayoutWidget_4);
        whitePointValue->setObjectName("whitePointValue");
        whitePointValue->setDecimals(4);
        whitePointValue->setMaximum(1.000000000000000);
        whitePointValue->setSingleStep(0.010000000000000);
        whitePointValue->setStepType(QAbstractSpinBox::StepType::DefaultStepType);
        whitePointValue->setValue(1.000000000000000);
        whitePointValue->setMinimumWidth(80);

        gridLayout->addWidget(whitePointValue, 6, 1, 1, 1);

        whitePointSlider = new QSlider(horizontalLayoutWidget_4);
        whitePointSlider->setObjectName("whitePointSlider");
        whitePointSlider->setMaximum(1000);
        whitePointSlider->setValue(1000);
        whitePointSlider->setOrientation(Qt::Orientation::Horizontal);

        gridLayout->addWidget(whitePointSlider, 6, 2, 1, 1);

        verticalLayout->addLayout(gridLayout);

        horizontalLayout_5 = new QHBoxLayout();
        horizontalLayout_5->setObjectName("horizontalLayout_5");
        horizontalLayout_5->setContentsMargins(t.spacing.md, -1, t.spacing.md, -1);
        label_6 = new QLabel(horizontalLayoutWidget_4);
        label_6->setObjectName("label_6");

        horizontalLayout_5->addWidget(label_6);

        spFromImage = new QPushButton(horizontalLayoutWidget_4);
        spFromImage->setObjectName("spFromImage");
        spFromImage->setCheckable(true);

        horizontalLayout_5->addWidget(spFromImage);

        spFromImageArea = new QPushButton(horizontalLayoutWidget_4);
        spFromImageArea->setObjectName("spFromImageArea");
        spFromImageArea->setCheckable(true);

        horizontalLayout_5->addWidget(spFromImageArea);

        spFromHistogram = new QPushButton(horizontalLayoutWidget_4);
        spFromHistogram->setObjectName("spFromHistogram");
        spFromHistogram->setCheckable(true);

        horizontalLayout_5->addWidget(spFromHistogram);

        horizontalLayout_5->addItem(
            new QSpacerItem(0, 0, QSizePolicy::Policy::Expanding, QSizePolicy::Policy::Minimum));

        verticalLayout->addLayout(horizontalLayout_5);

        spText = new QLabel(horizontalLayoutWidget_4);
        spText->setObjectName("spText");
        spText->setAlignment(Qt::AlignCenter);

        auto* spTextLayout = new QHBoxLayout();
        spTextLayout->setObjectName("spTextLayout");
        spTextLayout->setContentsMargins(t.spacing.md, 0, t.spacing.md, 0);
        spTextLayout->addWidget(spText);

        verticalLayout->addLayout(spTextLayout);

        verticalSpacer = new QSpacerItem(20, 40, QSizePolicy::Policy::Minimum, QSizePolicy::Policy::Expanding);

        verticalLayout->addItem(verticalSpacer);

        formLayout = new QFormLayout();
        formLayout->setObjectName("formLayout");
        label_3 = new QLabel(horizontalLayoutWidget_4);
        label_3->setObjectName("label_3");

        formLayout->setWidget(0, QFormLayout::ItemRole::LabelRole, label_3);

        targetImageSelector = new QComboBox(horizontalLayoutWidget_4);
        targetImageSelector->setObjectName("targetImageSelector");
        targetImageSelector->setMinimumWidth(150);
        targetImageSelector->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);

        formLayout->setWidget(0, QFormLayout::ItemRole::FieldRole, targetImageSelector);

        verticalLayout->addLayout(formLayout);

        horizontalLayout_4 = new QHBoxLayout();
        horizontalLayout_4->setObjectName("horizontalLayout_4");
        preview = new QCheckBox(horizontalLayoutWidget_4);
        preview->setObjectName("preview");
        preview->setChecked(true);

        horizontalLayout_4->addWidget(preview);

        horizontalSpacer = new QSpacerItem(40, 20, QSizePolicy::Policy::Expanding, QSizePolicy::Policy::Minimum);

        horizontalLayout_4->addItem(horizontalSpacer);

        verticalLayout->addLayout(horizontalLayout_4);

        horizontalLayout_3 = new QHBoxLayout();
        horizontalLayout_3->setObjectName("horizontalLayout_3");
        horizontalLayout_3->setSizeConstraint(QLayout::SizeConstraint::SetDefaultConstraint);
        buttonBox = new QDialogButtonBox(horizontalLayoutWidget_4);
        buttonBox->setObjectName("buttonBox");
        buttonBox->setStandardButtons(QDialogButtonBox::StandardButton::Apply |
                                      QDialogButtonBox::StandardButton::Close |
                                      QDialogButtonBox::StandardButton::Reset);

        horizontalLayout_3->addWidget(buttonBox);

        if (auto* btn = buttonBox->button(QDialogButtonBox::Close))
            btn->setToolTip(QCoreApplication::translate("GHTransformationProcess",
                                                        "Close this dialog without applying any changes.", nullptr));
        if (auto* btn = buttonBox->button(QDialogButtonBox::Reset))
            btn->setToolTip(QCoreApplication::translate("GHTransformationProcess",
                                                        "Reset all parameters to their default values.", nullptr));

        verticalLayout->addLayout(horizontalLayout_3);

        const auto split = rt_preview_ui::buildMainProcessSplitter(horizontalLayoutMain, horizontalLayoutWidget_4);
        split.controlsLayout->addLayout(verticalLayout);

        verticalLayout_2 = new QVBoxLayout();
        verticalLayout_2->setObjectName("verticalLayout_2");
        widget = new QWidget(split.previewPanel);
        widget->setObjectName("widget");
        widget->setMinimumSize(QSize(900, 0));

        verticalLayout_2->addWidget(widget);

        split.previewLayout->addLayout(verticalLayout_2);

        retranslateUi(GHTransformationProcess);

        QMetaObject::connectSlotsByName(GHTransformationProcess);
    } // setupUi

    void retranslateUi(QWidget* GHTransformationProcess)
    {
        GHTransformationProcess->setWindowTitle(
            QCoreApplication::translate("GHTransformationProcess", "Generalized Hyperbolic Transformation", nullptr));
#if QT_CONFIG(tooltip)
        zoomIn->setToolTip(QCoreApplication::translate("GHTransformationProcess", "Zoom in", nullptr));
#endif // QT_CONFIG(tooltip)
        zoomIn->setText(QString());
#if QT_CONFIG(tooltip)
        zoomOut->setToolTip(QCoreApplication::translate("GHTransformationProcess", "Zoom out", nullptr));
#endif // QT_CONFIG(tooltip)
        zoomOut->setText(QString());
#if QT_CONFIG(tooltip)
        zoomReset->setToolTip(QCoreApplication::translate("GHTransformationProcess", "Zoom reset", nullptr));
#endif // QT_CONFIG(tooltip)
        zoomReset->setText(QString());
#if QT_CONFIG(tooltip)
        gridButton->setToolTip(QCoreApplication::translate("GHTransformationProcess", "Show grid", nullptr));
#endif // QT_CONFIG(tooltip)
        gridButton->setText(QString());
#if QT_CONFIG(tooltip)
        transferCurve->setToolTip(QCoreApplication::translate("GHTransformationProcess", "Transfer curve", nullptr));
#endif // QT_CONFIG(tooltip)
        transferCurve->setText(QString());
#if QT_CONFIG(tooltip)
        showClippingInfo->setToolTip(
            QCoreApplication::translate("GHTransformationProcess", "Show clipping info", nullptr));
#endif // QT_CONFIG(tooltip)
        showClippingInfo->setText(QString());
#if QT_CONFIG(tooltip)
        localChannelNormalization->setToolTip(
            QCoreApplication::translate("GHTransformationProcess", "Absolute scales", nullptr));
#endif // QT_CONFIG(tooltip)
        localChannelNormalization->setText(QString());
#if QT_CONFIG(tooltip)
        showHistogramAreas->setToolTip(
            QCoreApplication::translate("GHTransformationProcess", "Draw histogram areas", nullptr));
#endif // QT_CONFIG(tooltip)
        showHistogramAreas->setText(QString());
#if QT_CONFIG(tooltip)
        plotResolution->setToolTip(QCoreApplication::translate("GHTransformationProcess", "Plot resolution", nullptr));
#endif // QT_CONFIG(tooltip)
#if QT_CONFIG(tooltip)
        channelSelector2->setToolTip(QCoreApplication::translate(
            "GHTransformationProcess", "Select the channel or color component to evaluate and transform.", nullptr));
#endif // QT_CONFIG(tooltip)
        label_4->setText(QCoreApplication::translate("GHTransformationProcess", "Channel", nullptr));
#if QT_CONFIG(tooltip)
        transformationTypeSelector->setToolTip(QCoreApplication::translate(
            "GHTransformationProcess", "Select the stretch model to apply to the selected channel.", nullptr));
#endif // QT_CONFIG(tooltip)
        humanWeightedLuminance->setText(
            QCoreApplication::translate("GHTransformationProcess", "Human weighted luminance", nullptr));
        colourClipLabel->setText(QCoreApplication::translate("GHTransformationProcess", "Colour clip", nullptr));
#if QT_CONFIG(tooltip)
        colourClipSelector->setToolTip(QCoreApplication::translate(
            "GHTransformationProcess", "Choose whether Colour stretches clip highlights or rescale RGB together.",
            nullptr));
#endif // QT_CONFIG(tooltip)
        lumCoeffSourceLabel->setText(
            QCoreApplication::translate("GHTransformationProcess", "Luminance coeffs", nullptr));
#if QT_CONFIG(tooltip)
        lumCoeffSourceSelector->setToolTip(QCoreApplication::translate(
            "GHTransformationProcess", "Choose the coefficient source for Colour channel luminance.", nullptr));
#endif // QT_CONFIG(tooltip)
        lumCoeffLabel->setText(QCoreApplication::translate("GHTransformationProcess", "R/G/B", nullptr));
        label->setText(QCoreApplication::translate("GHTransformationProcess", "Transformation type", nullptr));
        invert->setText(QCoreApplication::translate("GHTransformationProcess", "Invert", nullptr));
        linkedStf->setText(QCoreApplication::translate("GHTransformationProcess", "Linked STF", nullptr));
        label_9->setText(QCoreApplication::translate("GHTransformationProcess", "Stretch factor(In(D+1))", nullptr));
        label_11->setText(QCoreApplication::translate("GHTransformationProcess", "Local intensity (b)", nullptr));
        label_13->setText(QCoreApplication::translate("GHTransformationProcess", "Symmetry point (SP)", nullptr));
        label_2->setText(QCoreApplication::translate("GHTransformationProcess", "Protect shadows (LP)", nullptr));
        label_5->setText(QCoreApplication::translate("GHTransformationProcess", "Protect highlights (HP)", nullptr));
        blackPointLabel->setText(QCoreApplication::translate("GHTransformationProcess", "Black point (BP)", nullptr));
        whitePointLabel->setText(QCoreApplication::translate("GHTransformationProcess", "White point (WP)", nullptr));
        label_6->setText(QCoreApplication::translate("GHTransformationProcess", "SP picker", nullptr));
#if QT_CONFIG(tooltip)
        spFromImage->setToolTip(
            QCoreApplication::translate("GHTransformationProcess", "Pick from image point", nullptr));
#endif // QT_CONFIG(tooltip)
        spFromImage->setText(QString());
#if QT_CONFIG(tooltip)
        spFromImageArea->setToolTip(
            QCoreApplication::translate("GHTransformationProcess", "Pick from image area", nullptr));
#endif // QT_CONFIG(tooltip)
        spFromImageArea->setText(QString());
#if QT_CONFIG(tooltip)
        spFromHistogram->setToolTip(
            QCoreApplication::translate("GHTransformationProcess", "Pick from histogram point", nullptr));
#endif // QT_CONFIG(tooltip)
        spFromHistogram->setText(QString());
        spText->setText(QString());
        label_3->setText(QCoreApplication::translate("GHTransformationProcess", "Target Image ", nullptr));
#if QT_CONFIG(tooltip)
        targetImageSelector->setToolTip(
            QCoreApplication::translate("GHTransformationProcess", "Choose the open image to process.", nullptr));
#endif // QT_CONFIG(tooltip)
        preview->setText(QCoreApplication::translate("GHTransformationProcess", "Preview", nullptr));
#if QT_CONFIG(tooltip)
        preview->setToolTip(
            QCoreApplication::translate("GHTransformationProcess", "Enable or disable live preview updates.", nullptr));
#endif // QT_CONFIG(tooltip)
    } // retranslateUi
};

inline GHTransformationProcessUiElements buildGHTransformationProcessUi(QWidget* parent)
{
    GHTransformationProcessUiElements ui{};
    if (!parent)
    {
        return ui;
    }

    ui.setupUi(parent);
    return ui;
}

} // namespace an::ui::processes
