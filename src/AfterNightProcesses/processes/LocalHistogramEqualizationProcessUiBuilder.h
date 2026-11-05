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
#include <QtWidgets/QSpinBox>
#include <QtWidgets/QToolButton>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QWidget>

#include "processes/RTPreviewUiCommon.h"
#include "theme/AnDesignTokens.h"

namespace an::ui::processes
{

class LocalHistogramEqualizationProcessUiElements
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
    QLabel* label_2;
    QSlider* regularizationSlider;
    QLabel* label_7;
    QSpinBox* radiusValue;
    QSlider* radiusSlider;
    QLabel* label_6;
    QLabel* label_5;
    QSlider* amountSlider;
    QLabel* label;
    QDoubleSpinBox* regularizationValue;
    QDoubleSpinBox* sigmaValue;
    QSlider* sigmaSlider;
    QDoubleSpinBox* amountValue;
    QDoubleSpinBox* strengthValue;
    QSlider* strengthSlider;
    QCheckBox* reduceHalos;
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

    void setupUi(QWidget* LocalHistogramEqualizationProcess)
    {
        const auto& t = an::ui::tokens();

        if (LocalHistogramEqualizationProcess->objectName().isEmpty())
            LocalHistogramEqualizationProcess->setObjectName("LocalHistogramEqualizationProcess");
        LocalHistogramEqualizationProcess->resize(897, 650);
        LocalHistogramEqualizationProcess->setMinimumSize(QSize(300, 200));
        LocalHistogramEqualizationProcess->setSizePolicy(QSizePolicy::Policy::Preferred,
                                                         QSizePolicy::Policy::Preferred);
        auto* mainLayout = new QVBoxLayout(LocalHistogramEqualizationProcess);
        mainLayout->setObjectName("mainLayout");
        mainLayout->setContentsMargins(t.spacing.md, t.spacing.md, t.spacing.md, t.spacing.md);
        mainLayout->setSpacing(0);
        horizontalLayoutWidget_4 = new QWidget(LocalHistogramEqualizationProcess);
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
        scrollAreaWidgetContents->setMinimumSize(QSize(417, 218));
        chartScrollArea->setWidget(scrollAreaWidgetContents);

        horizontalLayout->addWidget(chartScrollArea);

        verticalLayout->addLayout(horizontalLayout);

        histogramSettingsLayout_2 = new QHBoxLayout();
        histogramSettingsLayout_2->setObjectName("histogramSettingsLayout_2");
        const auto toolbar =
            rt_preview_ui::buildHistogramToolbar(horizontalLayoutWidget_4, histogramSettingsLayout_2,
                                                 {.translationContext = "LocalHistogramEqualizationProcess"});

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
        label_2 = new QLabel(horizontalLayoutWidget_4);
        label_2->setObjectName("label_2");

        gridLayout->addWidget(label_2, 1, 0, 1, 1);

        regularizationSlider = new QSlider(horizontalLayoutWidget_4);
        regularizationSlider->setObjectName("regularizationSlider");
        regularizationSlider->setMinimum(0);
        regularizationSlider->setMaximum(320);
        regularizationSlider->setValue(80);
        regularizationSlider->setOrientation(Qt::Orientation::Horizontal);

        gridLayout->addWidget(regularizationSlider, 3, 2, 1, 1);

        label_7 = new QLabel(horizontalLayoutWidget_4);
        label_7->setObjectName("label_7");

        gridLayout->addWidget(label_7, 2, 0, 1, 1);

        radiusValue = new QSpinBox(horizontalLayoutWidget_4);
        radiusValue->setObjectName("radiusValue");
        radiusValue->setMinimum(1);
        radiusValue->setMaximum(16);
        radiusValue->setValue(4);
        radiusValue->setMinimumWidth(80);

        gridLayout->addWidget(radiusValue, 1, 1, 1, 1);

        radiusSlider = new QSlider(horizontalLayoutWidget_4);
        radiusSlider->setObjectName("radiusSlider");
        radiusSlider->setMinimum(1);
        radiusSlider->setMaximum(16);
        radiusSlider->setValue(4);
        radiusSlider->setOrientation(Qt::Orientation::Horizontal);

        gridLayout->addWidget(radiusSlider, 1, 2, 1, 1);

        label_6 = new QLabel(horizontalLayoutWidget_4);
        label_6->setObjectName("label_6");

        gridLayout->addWidget(label_6, 4, 0, 1, 1);

        label_5 = new QLabel(horizontalLayoutWidget_4);
        label_5->setObjectName("label_5");

        gridLayout->addWidget(label_5, 3, 0, 1, 1);

        amountSlider = new QSlider(horizontalLayoutWidget_4);
        amountSlider->setObjectName("amountSlider");
        amountSlider->setMinimum(0);
        amountSlider->setMaximum(1000);
        amountSlider->setValue(1000);
        amountSlider->setOrientation(Qt::Orientation::Horizontal);

        gridLayout->addWidget(amountSlider, 4, 2, 1, 1);

        label = new QLabel(horizontalLayoutWidget_4);
        label->setObjectName("label");

        gridLayout->addWidget(label, 0, 0, 1, 1);

        regularizationValue = new QDoubleSpinBox(horizontalLayoutWidget_4);
        regularizationValue->setObjectName("regularizationValue");
        regularizationValue->setMinimum(0.000000000000000);
        regularizationValue->setMaximum(32.000000000000000);
        regularizationValue->setSingleStep(0.050000000000000);
        regularizationValue->setStepType(QAbstractSpinBox::StepType::AdaptiveDecimalStepType);
        regularizationValue->setValue(8.000000000000000);
        regularizationValue->setMinimumWidth(80);

        gridLayout->addWidget(regularizationValue, 3, 1, 1, 1);

        sigmaValue = new QDoubleSpinBox(horizontalLayoutWidget_4);
        sigmaValue->setObjectName("sigmaValue");
        sigmaValue->setMinimum(0.000000000000000);
        sigmaValue->setMaximum(256.000000000000000);
        sigmaValue->setSingleStep(0.050000000000000);
        sigmaValue->setStepType(QAbstractSpinBox::StepType::AdaptiveDecimalStepType);
        sigmaValue->setValue(100.000000000000000);
        sigmaValue->setMinimumWidth(80);

        gridLayout->addWidget(sigmaValue, 2, 1, 1, 1);

        sigmaSlider = new QSlider(horizontalLayoutWidget_4);
        sigmaSlider->setObjectName("sigmaSlider");
        sigmaSlider->setMinimum(0);
        sigmaSlider->setMaximum(2560);
        sigmaSlider->setValue(1000);
        sigmaSlider->setOrientation(Qt::Orientation::Horizontal);

        gridLayout->addWidget(sigmaSlider, 2, 2, 1, 1);

        amountValue = new QDoubleSpinBox(horizontalLayoutWidget_4);
        amountValue->setObjectName("amountValue");
        amountValue->setMinimum(0.000000000000000);
        amountValue->setMaximum(1.000000000000000);
        amountValue->setSingleStep(0.050000000000000);
        amountValue->setStepType(QAbstractSpinBox::StepType::AdaptiveDecimalStepType);
        amountValue->setValue(1.000000000000000);
        amountValue->setMinimumWidth(80);

        gridLayout->addWidget(amountValue, 4, 1, 1, 1);

        strengthValue = new QDoubleSpinBox(horizontalLayoutWidget_4);
        strengthValue->setObjectName("strengthValue");
        strengthValue->setMaximum(100.000000000000000);
        strengthValue->setValue(75.000000000000000);
        strengthValue->setMinimumWidth(80);

        gridLayout->addWidget(strengthValue, 0, 1, 1, 1);

        strengthSlider = new QSlider(horizontalLayoutWidget_4);
        strengthSlider->setObjectName("strengthSlider");
        strengthSlider->setMinimum(0);
        strengthSlider->setMaximum(1000);
        strengthSlider->setValue(750);
        strengthSlider->setOrientation(Qt::Orientation::Horizontal);

        gridLayout->addWidget(strengthSlider, 0, 2, 1, 1);

        verticalLayout->addLayout(gridLayout);

        reduceHalos = new QCheckBox(horizontalLayoutWidget_4);
        reduceHalos->setObjectName("reduceHalos");
        reduceHalos->setChecked(true);

        verticalLayout->addWidget(reduceHalos);

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

        retranslateUi(LocalHistogramEqualizationProcess);

        QMetaObject::connectSlotsByName(LocalHistogramEqualizationProcess);
    } // setupUi

    void retranslateUi(QWidget* LocalHistogramEqualizationProcess)
    {
        LocalHistogramEqualizationProcess->setWindowTitle(
            QCoreApplication::translate("LocalHistogramEqualizationProcess", "Local Histogram Equalization", nullptr));
#if QT_CONFIG(tooltip)
        zoomIn->setToolTip(QCoreApplication::translate("LocalHistogramEqualizationProcess", "Zoom in", nullptr));
#endif // QT_CONFIG(tooltip)
        zoomIn->setText(QString());
#if QT_CONFIG(tooltip)
        zoomOut->setToolTip(QCoreApplication::translate("LocalHistogramEqualizationProcess", "Zoom out", nullptr));
#endif // QT_CONFIG(tooltip)
        zoomOut->setText(QString());
#if QT_CONFIG(tooltip)
        zoomReset->setToolTip(QCoreApplication::translate("LocalHistogramEqualizationProcess", "Zoom reset", nullptr));
#endif // QT_CONFIG(tooltip)
        zoomReset->setText(QString());
#if QT_CONFIG(tooltip)
        gridButton->setToolTip(QCoreApplication::translate("LocalHistogramEqualizationProcess", "Show grid", nullptr));
#endif // QT_CONFIG(tooltip)
        gridButton->setText(QString());
#if QT_CONFIG(tooltip)
        showClippingInfo->setToolTip(
            QCoreApplication::translate("LocalHistogramEqualizationProcess", "Show clipping info", nullptr));
#endif // QT_CONFIG(tooltip)
        showClippingInfo->setText(QString());
#if QT_CONFIG(tooltip)
        localChannelNormalization->setToolTip(
            QCoreApplication::translate("LocalHistogramEqualizationProcess", "Absolute scales", nullptr));
#endif // QT_CONFIG(tooltip)
        localChannelNormalization->setText(QString());
#if QT_CONFIG(tooltip)
        showHistogramAreas->setToolTip(
            QCoreApplication::translate("LocalHistogramEqualizationProcess", "Draw histogram areas", nullptr));
#endif // QT_CONFIG(tooltip)
        showHistogramAreas->setText(QString());
#if QT_CONFIG(tooltip)
        showHistogram->setToolTip(
            QCoreApplication::translate("LocalHistogramEqualizationProcess", "Show histogram", nullptr));
#endif // QT_CONFIG(tooltip)
        showHistogram->setText(QString());
#if QT_CONFIG(tooltip)
        plotResolution->setToolTip(
            QCoreApplication::translate("LocalHistogramEqualizationProcess", "Plot resolution", nullptr));
#endif // QT_CONFIG(tooltip)
        label_2->setText(QCoreApplication::translate("LocalHistogramEqualizationProcess", "Radius", nullptr));
        label_7->setText(QCoreApplication::translate("LocalHistogramEqualizationProcess", "Sigma", nullptr));
        label_6->setText(QCoreApplication::translate("LocalHistogramEqualizationProcess", "Amount", nullptr));
        label_5->setText(QCoreApplication::translate("LocalHistogramEqualizationProcess", "Regularization", nullptr));
        label->setText(QCoreApplication::translate("LocalHistogramEqualizationProcess", "Strength (%)", nullptr));
        reduceHalos->setText(QCoreApplication::translate("LocalHistogramEqualizationProcess", "Reduce Halos", nullptr));
        label_3->setText(QCoreApplication::translate("LocalHistogramEqualizationProcess", "Target Image ", nullptr));
        label_4->setText(QCoreApplication::translate("LocalHistogramEqualizationProcess", "Channel", nullptr));
        preview->setText(QCoreApplication::translate("LocalHistogramEqualizationProcess", "Preview", nullptr));

        strengthValue->setToolTip(QCoreApplication::translate(
            "LocalHistogramEqualizationProcess", "Local histogram equalization strength percentage.", nullptr));
        strengthSlider->setToolTip(QCoreApplication::translate(
            "LocalHistogramEqualizationProcess", "Local histogram equalization strength percentage.", nullptr));
        radiusValue->setToolTip(QCoreApplication::translate(
            "LocalHistogramEqualizationProcess", "Neighborhood radius for local histogram analysis.", nullptr));
        radiusSlider->setToolTip(QCoreApplication::translate(
            "LocalHistogramEqualizationProcess", "Neighborhood radius for local histogram analysis.", nullptr));
        sigmaValue->setToolTip(QCoreApplication::translate("LocalHistogramEqualizationProcess",
                                                           "Gaussian sigma used in local weighting.", nullptr));
        sigmaSlider->setToolTip(QCoreApplication::translate("LocalHistogramEqualizationProcess",
                                                            "Gaussian sigma used in local weighting.", nullptr));
        regularizationValue->setToolTip(QCoreApplication::translate(
            "LocalHistogramEqualizationProcess", "Regularization to avoid over-enhancement artifacts.", nullptr));
        regularizationSlider->setToolTip(QCoreApplication::translate(
            "LocalHistogramEqualizationProcess", "Regularization to avoid over-enhancement artifacts.", nullptr));
        amountValue->setToolTip(QCoreApplication::translate("LocalHistogramEqualizationProcess",
                                                            "Blend amount for this process result.", nullptr));
        amountSlider->setToolTip(QCoreApplication::translate("LocalHistogramEqualizationProcess",
                                                             "Blend amount for this process result.", nullptr));
        reduceHalos->setToolTip(QCoreApplication::translate(
            "LocalHistogramEqualizationProcess", "Reduce halo artifacts around strong local contrast edges.", nullptr));
        targetImageSelector->setToolTip(QCoreApplication::translate("LocalHistogramEqualizationProcess",
                                                                    "Choose the open image to process.", nullptr));
        channelSelector->setToolTip(QCoreApplication::translate(
            "LocalHistogramEqualizationProcess",
            "Select the channel to process, or RGB to process all channels together.", nullptr));
        preview->setToolTip(QCoreApplication::translate("LocalHistogramEqualizationProcess",
                                                        "Enable or disable live preview updates.", nullptr));

        if (auto* btn = buttonBox->button(QDialogButtonBox::Close))
            btn->setToolTip(QCoreApplication::translate("LocalHistogramEqualizationProcess",
                                                        "Close this dialog without applying any changes.", nullptr));
        if (auto* btn = buttonBox->button(QDialogButtonBox::Reset))
            btn->setToolTip(QCoreApplication::translate("LocalHistogramEqualizationProcess",
                                                        "Reset all parameters to their default values.", nullptr));
    } // retranslateUi
};

inline LocalHistogramEqualizationProcessUiElements buildLocalHistogramEqualizationProcessUi(QWidget* parent)
{
    LocalHistogramEqualizationProcessUiElements ui{};
    if (!parent)
    {
        return ui;
    }

    ui.setupUi(parent);
    return ui;
}

} // namespace an::ui::processes
