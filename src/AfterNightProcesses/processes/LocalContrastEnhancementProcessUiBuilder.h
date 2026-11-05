// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include <QPushButton>
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

class LocalContrastEnhancementProcessUiElements
{
public:
    QWidget* horizontalLayoutWidget_4;
    QHBoxLayout* horizontalLayoutMain;
    QVBoxLayout* verticalLayout;
    QHBoxLayout* horizontalLayout;
    QScrollArea* chartScrollArea;
    QWidget* scrollAreaWidgetContents;
    QHBoxLayout* histogramSettingsLayout_2;
    QToolButton* zoomIn;
    QToolButton* zoomOut;
    QToolButton* zoomReset;
    QFrame* line_3;
    QToolButton* gridButton;
    QToolButton* showClippingInfo;
    QToolButton* localChannelNormalization;
    QToolButton* showHistogramAreas;
    QToolButton* showHistogram;
    QFrame* line_4;
    QComboBox* plotResolution;
    QSpacerItem* horizontalSpacer_2;
    QGridLayout* gridLayout;
    QSlider* darknessLevelSlider;
    QDoubleSpinBox* amountValue;
    QDoubleSpinBox* weightValue;
    QSlider* lightnessLevelSlider;
    QLabel* label;
    QLabel* label_2;
    QLabel* label_7;
    QDoubleSpinBox* lightnessLevelValue;
    QSlider* amountSlider;
    QLabel* label_5;
    QSlider* spatialRadiusSlider;
    QDoubleSpinBox* darknessLevelValue;
    QSlider* weightSlider;
    QDoubleSpinBox* spatialRadiusValue;
    QLabel* label_6;
    QSpacerItem* verticalSpacer;
    QFormLayout* formLayout;
    QLabel* label_3;
    QComboBox* targetImageSelector;
    QLabel* label_4;
    QComboBox* channelSelector;
    QHBoxLayout* horizontalLayout_4;
    QCheckBox* preview;
    QSpacerItem* horizontalSpacer;
    QHBoxLayout* horizontalLayout_3;
    QDialogButtonBox* buttonBox;
    QVBoxLayout* verticalLayout_2;
    QWidget* widget;

    void setupUi(QWidget* LocalContrastEnhancementProcess)
    {
        const auto& t = an::ui::tokens();

        if (LocalContrastEnhancementProcess->objectName().isEmpty())
            LocalContrastEnhancementProcess->setObjectName("LocalContrastEnhancementProcess");
        LocalContrastEnhancementProcess->resize(1155, 700);
        LocalContrastEnhancementProcess->setMinimumSize(QSize(300, 200));
        LocalContrastEnhancementProcess->setSizePolicy(QSizePolicy::Policy::Preferred, QSizePolicy::Policy::Preferred);
        auto* mainLayout = new QVBoxLayout(LocalContrastEnhancementProcess);
        mainLayout->setObjectName("mainLayout");
        mainLayout->setContentsMargins(t.spacing.md, t.spacing.md, t.spacing.md, t.spacing.md);
        mainLayout->setSpacing(0);
        horizontalLayoutWidget_4 = new QWidget(LocalContrastEnhancementProcess);
        horizontalLayoutWidget_4->setObjectName("horizontalLayoutWidget_4");
        mainLayout->addWidget(horizontalLayoutWidget_4);
        horizontalLayoutMain = new QHBoxLayout(horizontalLayoutWidget_4);
        horizontalLayoutMain->setSpacing(t.spacing.md);
        horizontalLayoutMain->setObjectName("horizontalLayoutMain");
        horizontalLayoutMain->setSizeConstraint(QLayout::SizeConstraint::SetMinimumSize);
        horizontalLayoutMain->setContentsMargins(t.spacing.md, t.spacing.md, t.spacing.md, t.spacing.md);
        verticalLayout = new QVBoxLayout();
        verticalLayout->setObjectName("verticalLayout");
        verticalLayout->setSizeConstraint(QLayout::SizeConstraint::SetDefaultConstraint);
        verticalLayout->setContentsMargins(-1, t.spacing.md, -1, -1);
        horizontalLayout = new QHBoxLayout();
        horizontalLayout->setObjectName("horizontalLayout");
        chartScrollArea = new QScrollArea(horizontalLayoutWidget_4);
        chartScrollArea->setObjectName("chartScrollArea");
        chartScrollArea->setMinimumSize(QSize(380, 220));
        chartScrollArea->setWidgetResizable(true);
        scrollAreaWidgetContents = new QWidget();
        scrollAreaWidgetContents->setObjectName("scrollAreaWidgetContents");
        scrollAreaWidgetContents->setMinimumSize(QSize(378, 218));
        chartScrollArea->setWidget(scrollAreaWidgetContents);

        horizontalLayout->addWidget(chartScrollArea);

        verticalLayout->addLayout(horizontalLayout);

        histogramSettingsLayout_2 = new QHBoxLayout();
        histogramSettingsLayout_2->setObjectName("histogramSettingsLayout_2");
        const auto toolbar =
            rt_preview_ui::buildHistogramToolbar(horizontalLayoutWidget_4, histogramSettingsLayout_2,
                                                 {.translationContext = "LocalContrastEnhancementProcess"});

        zoomIn = toolbar.zoomIn;
        zoomOut = toolbar.zoomOut;
        zoomReset = toolbar.zoomReset;
        gridButton = toolbar.gridButton;
        showClippingInfo = toolbar.showClippingInfo;
        localChannelNormalization = toolbar.localChannelNormalization;
        showHistogramAreas = toolbar.showHistogramAreas;
        showHistogram = toolbar.showHistogram;
        plotResolution = toolbar.plotResolution;

        verticalLayout->addLayout(histogramSettingsLayout_2);

        gridLayout = new QGridLayout();
        gridLayout->setObjectName("gridLayout");
        darknessLevelSlider = new QSlider(horizontalLayoutWidget_4);
        darknessLevelSlider->setObjectName("darknessLevelSlider");
        darknessLevelSlider->setMinimum(0);
        darknessLevelSlider->setMaximum(400);
        darknessLevelSlider->setValue(100);
        darknessLevelSlider->setOrientation(Qt::Orientation::Horizontal);

        gridLayout->addWidget(darknessLevelSlider, 2, 2, 1, 1);

        amountValue = new QDoubleSpinBox(horizontalLayoutWidget_4);
        amountValue->setObjectName("amountValue");
        amountValue->setMinimum(0.000000000000000);
        amountValue->setMaximum(1.000000000000000);
        amountValue->setSingleStep(0.050000000000000);
        amountValue->setStepType(QAbstractSpinBox::StepType::AdaptiveDecimalStepType);
        amountValue->setValue(1.000000000000000);
        amountValue->setMinimumWidth(80);

        gridLayout->addWidget(amountValue, 4, 1, 1, 1);

        weightValue = new QDoubleSpinBox(horizontalLayoutWidget_4);
        weightValue->setObjectName("weightValue");
        weightValue->setMaximum(5.000000000000000);
        weightValue->setValue(0.500000000000000);
        weightValue->setMinimumWidth(80);

        gridLayout->addWidget(weightValue, 1, 1, 1, 1);

        lightnessLevelSlider = new QSlider(horizontalLayoutWidget_4);
        lightnessLevelSlider->setObjectName("lightnessLevelSlider");
        lightnessLevelSlider->setMinimum(0);
        lightnessLevelSlider->setMaximum(400);
        lightnessLevelSlider->setValue(100);
        lightnessLevelSlider->setOrientation(Qt::Orientation::Horizontal);

        gridLayout->addWidget(lightnessLevelSlider, 3, 2, 1, 1);

        label = new QLabel(horizontalLayoutWidget_4);
        label->setObjectName("label");

        gridLayout->addWidget(label, 0, 0, 1, 1);

        label_2 = new QLabel(horizontalLayoutWidget_4);
        label_2->setObjectName("label_2");

        gridLayout->addWidget(label_2, 1, 0, 1, 1);

        label_7 = new QLabel(horizontalLayoutWidget_4);
        label_7->setObjectName("label_7");

        gridLayout->addWidget(label_7, 2, 0, 1, 1);

        lightnessLevelValue = new QDoubleSpinBox(horizontalLayoutWidget_4);
        lightnessLevelValue->setObjectName("lightnessLevelValue");
        lightnessLevelValue->setMinimum(0.000000000000000);
        lightnessLevelValue->setMaximum(4.000000000000000);
        lightnessLevelValue->setSingleStep(0.050000000000000);
        lightnessLevelValue->setStepType(QAbstractSpinBox::StepType::AdaptiveDecimalStepType);
        lightnessLevelValue->setValue(1.000000000000000);
        lightnessLevelValue->setMinimumWidth(80);

        gridLayout->addWidget(lightnessLevelValue, 3, 1, 1, 1);

        amountSlider = new QSlider(horizontalLayoutWidget_4);
        amountSlider->setObjectName("amountSlider");
        amountSlider->setMinimum(0);
        amountSlider->setMaximum(1000);
        amountSlider->setValue(1000);
        amountSlider->setOrientation(Qt::Orientation::Horizontal);

        gridLayout->addWidget(amountSlider, 4, 2, 1, 1);

        label_5 = new QLabel(horizontalLayoutWidget_4);
        label_5->setObjectName("label_5");

        gridLayout->addWidget(label_5, 3, 0, 1, 1);

        spatialRadiusSlider = new QSlider(horizontalLayoutWidget_4);
        spatialRadiusSlider->setObjectName("spatialRadiusSlider");
        spatialRadiusSlider->setMinimum(300);
        spatialRadiusSlider->setMaximum(2000);
        spatialRadiusSlider->setValue(800);
        spatialRadiusSlider->setOrientation(Qt::Orientation::Horizontal);

        gridLayout->addWidget(spatialRadiusSlider, 0, 2, 1, 1);

        darknessLevelValue = new QDoubleSpinBox(horizontalLayoutWidget_4);
        darknessLevelValue->setObjectName("darknessLevelValue");
        darknessLevelValue->setMinimum(0.000000000000000);
        darknessLevelValue->setMaximum(4.000000000000000);
        darknessLevelValue->setSingleStep(0.050000000000000);
        darknessLevelValue->setStepType(QAbstractSpinBox::StepType::AdaptiveDecimalStepType);
        darknessLevelValue->setValue(1.000000000000000);
        darknessLevelValue->setMinimumWidth(80);

        gridLayout->addWidget(darknessLevelValue, 2, 1, 1, 1);

        weightSlider = new QSlider(horizontalLayoutWidget_4);
        weightSlider->setObjectName("weightSlider");
        weightSlider->setMinimum(0);
        weightSlider->setMaximum(500);
        weightSlider->setValue(50);
        weightSlider->setOrientation(Qt::Orientation::Horizontal);

        gridLayout->addWidget(weightSlider, 1, 2, 1, 1);

        spatialRadiusValue = new QDoubleSpinBox(horizontalLayoutWidget_4);
        spatialRadiusValue->setObjectName("spatialRadiusValue");
        spatialRadiusValue->setMinimum(30.000000000000000);
        spatialRadiusValue->setMaximum(200.000000000000000);
        spatialRadiusValue->setValue(80.000000000000000);
        spatialRadiusValue->setMinimumWidth(80);

        gridLayout->addWidget(spatialRadiusValue, 0, 1, 1, 1);

        label_6 = new QLabel(horizontalLayoutWidget_4);
        label_6->setObjectName("label_6");

        gridLayout->addWidget(label_6, 4, 0, 1, 1);

        verticalLayout->addLayout(gridLayout);

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

        label_4 = new QLabel(horizontalLayoutWidget_4);
        label_4->setObjectName("label_4");

        formLayout->setWidget(1, QFormLayout::ItemRole::LabelRole, label_4);

        channelSelector = new QComboBox(horizontalLayoutWidget_4);
        channelSelector->setObjectName("channelSelector");
        channelSelector->setMinimumWidth(150);

        formLayout->setWidget(1, QFormLayout::ItemRole::FieldRole, channelSelector);

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

        verticalLayout->addLayout(horizontalLayout_3);

        const auto split = rt_preview_ui::buildMainProcessSplitter(horizontalLayoutMain, horizontalLayoutWidget_4);
        split.controlsLayout->addLayout(verticalLayout);

        verticalLayout_2 = new QVBoxLayout();
        verticalLayout_2->setObjectName("verticalLayout_2");
        widget = new QWidget(split.previewPanel);
        widget->setObjectName("widget");

        verticalLayout_2->addWidget(widget);

        split.previewLayout->addLayout(verticalLayout_2);

        retranslateUi(LocalContrastEnhancementProcess);

        QMetaObject::connectSlotsByName(LocalContrastEnhancementProcess);
    } // setupUi

    void retranslateUi(QWidget* LocalContrastEnhancementProcess)
    {
        LocalContrastEnhancementProcess->setWindowTitle(
            QCoreApplication::translate("LocalContrastEnhancementProcess", "Local Contrast Enhancement", nullptr));
#if QT_CONFIG(tooltip)
        zoomIn->setToolTip(QCoreApplication::translate("LocalContrastEnhancementProcess", "Zoom in", nullptr));
#endif // QT_CONFIG(tooltip)
        zoomIn->setText(QString());
#if QT_CONFIG(tooltip)
        zoomOut->setToolTip(QCoreApplication::translate("LocalContrastEnhancementProcess", "Zoom out", nullptr));
#endif // QT_CONFIG(tooltip)
        zoomOut->setText(QString());
#if QT_CONFIG(tooltip)
        zoomReset->setToolTip(QCoreApplication::translate("LocalContrastEnhancementProcess", "Zoom reset", nullptr));
#endif // QT_CONFIG(tooltip)
        zoomReset->setText(QString());
#if QT_CONFIG(tooltip)
        gridButton->setToolTip(QCoreApplication::translate("LocalContrastEnhancementProcess", "Show grid", nullptr));
#endif // QT_CONFIG(tooltip)
        gridButton->setText(QString());
#if QT_CONFIG(tooltip)
        showClippingInfo->setToolTip(
            QCoreApplication::translate("LocalContrastEnhancementProcess", "Show clipping info", nullptr));
#endif // QT_CONFIG(tooltip)
        showClippingInfo->setText(QString());
#if QT_CONFIG(tooltip)
        localChannelNormalization->setToolTip(
            QCoreApplication::translate("LocalContrastEnhancementProcess", "Absolute scales", nullptr));
#endif // QT_CONFIG(tooltip)
        localChannelNormalization->setText(QString());
#if QT_CONFIG(tooltip)
        showHistogramAreas->setToolTip(
            QCoreApplication::translate("LocalContrastEnhancementProcess", "Draw histogram areas", nullptr));
#endif // QT_CONFIG(tooltip)
        showHistogramAreas->setText(QString());
#if QT_CONFIG(tooltip)
        showHistogram->setToolTip(
            QCoreApplication::translate("LocalContrastEnhancementProcess", "Show histogram", nullptr));
#endif // QT_CONFIG(tooltip)
        showHistogram->setText(QString());
#if QT_CONFIG(tooltip)
        plotResolution->setToolTip(
            QCoreApplication::translate("LocalContrastEnhancementProcess", "Plot resolution", nullptr));
#endif // QT_CONFIG(tooltip)
        label->setText(QCoreApplication::translate("LocalContrastEnhancementProcess", "Spatial Radius", nullptr));
        label_2->setText(QCoreApplication::translate("LocalContrastEnhancementProcess", "Weight", nullptr));
        label_7->setText(QCoreApplication::translate("LocalContrastEnhancementProcess", "Darkness Level", nullptr));
        label_5->setText(QCoreApplication::translate("LocalContrastEnhancementProcess", "Lightness Level", nullptr));
        label_6->setText(QCoreApplication::translate("LocalContrastEnhancementProcess", "Amount", nullptr));
        label_3->setText(QCoreApplication::translate("LocalContrastEnhancementProcess", "Target Image ", nullptr));
        label_4->setText(QCoreApplication::translate("LocalContrastEnhancementProcess", "Channel", nullptr));
        preview->setText(QCoreApplication::translate("LocalContrastEnhancementProcess", "Preview", nullptr));

        spatialRadiusValue->setToolTip(QCoreApplication::translate(
            "LocalContrastEnhancementProcess", "Spatial radius for local contrast computation.", nullptr));
        spatialRadiusSlider->setToolTip(QCoreApplication::translate(
            "LocalContrastEnhancementProcess", "Spatial radius for local contrast computation.", nullptr));
        weightValue->setToolTip(QCoreApplication::translate(
            "LocalContrastEnhancementProcess", "Edge weighting strength in local contrast enhancement.", nullptr));
        weightSlider->setToolTip(QCoreApplication::translate(
            "LocalContrastEnhancementProcess", "Edge weighting strength in local contrast enhancement.", nullptr));
        darknessLevelValue->setToolTip(
            QCoreApplication::translate("LocalContrastEnhancementProcess", "Dark-tone boost factor.", nullptr));
        darknessLevelSlider->setToolTip(
            QCoreApplication::translate("LocalContrastEnhancementProcess", "Dark-tone boost factor.", nullptr));
        lightnessLevelValue->setToolTip(
            QCoreApplication::translate("LocalContrastEnhancementProcess", "Light-tone boost factor.", nullptr));
        lightnessLevelSlider->setToolTip(
            QCoreApplication::translate("LocalContrastEnhancementProcess", "Light-tone boost factor.", nullptr));
        amountValue->setToolTip(QCoreApplication::translate("LocalContrastEnhancementProcess",
                                                            "Blend amount for this process result.", nullptr));
        amountSlider->setToolTip(QCoreApplication::translate("LocalContrastEnhancementProcess",
                                                             "Blend amount for this process result.", nullptr));
        targetImageSelector->setToolTip(QCoreApplication::translate("LocalContrastEnhancementProcess",
                                                                    "Choose the open image to process.", nullptr));
        channelSelector->setToolTip(QCoreApplication::translate(
            "LocalContrastEnhancementProcess",
            "Select the channel to process, or RGB to process all channels together.", nullptr));
        preview->setToolTip(QCoreApplication::translate("LocalContrastEnhancementProcess",
                                                        "Enable or disable live preview updates.", nullptr));

        if (auto* btn = buttonBox->button(QDialogButtonBox::Close))
            btn->setToolTip(QCoreApplication::translate("LocalContrastEnhancementProcess",
                                                        "Close this dialog without applying any changes.", nullptr));
        if (auto* btn = buttonBox->button(QDialogButtonBox::Reset))
            btn->setToolTip(QCoreApplication::translate("LocalContrastEnhancementProcess",
                                                        "Reset all parameters to their default values.", nullptr));
    } // retranslateUi
};

inline LocalContrastEnhancementProcessUiElements buildLocalContrastEnhancementProcessUi(QWidget* parent)
{
    LocalContrastEnhancementProcessUiElements ui{};
    if (!parent)
    {
        return ui;
    }

    ui.setupUi(parent);
    return ui;
}

} // namespace an::ui::processes
