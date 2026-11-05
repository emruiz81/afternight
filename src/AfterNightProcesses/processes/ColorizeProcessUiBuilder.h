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

class ColorizeProcessUiElements
{
public:
    QWidget* horizontalLayoutWidget_4;
    QHBoxLayout* horizontalLayoutMain;
    QVBoxLayout* verticalLayout;
    QHBoxLayout* horizontalLayout_5;
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
    QHBoxLayout* horizontalLayout_6;
    QLabel* label_5;
    QComboBox* colorizeMethod;
    QSpacerItem* horizontalSpacer_3;
    QHBoxLayout* horizontalLayout;
    QLabel* label;
    QDoubleSpinBox* hueValue;
    QSlider* hueSlider;
    QHBoxLayout* horizontalLayout_2;
    QLabel* label_2;
    QDoubleSpinBox* saturationValue;
    QSlider* saturationSlider;
    QHBoxLayout* horizontalLayout_7;
    QCheckBox* useLightness;
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

    void setupUi(QWidget* ColorizeProcess)
    {
        const auto& t = an::ui::tokens();

        if (ColorizeProcess->objectName().isEmpty())
            ColorizeProcess->setObjectName("ColorizeProcess");
        ColorizeProcess->resize(791, 611);
        ColorizeProcess->setMinimumSize(QSize(300, 200));
        ColorizeProcess->setSizePolicy(QSizePolicy::Policy::Preferred, QSizePolicy::Policy::Preferred);
        auto* mainLayout = new QVBoxLayout(ColorizeProcess);
        mainLayout->setObjectName("mainLayout");
        mainLayout->setContentsMargins(t.spacing.md, t.spacing.md, t.spacing.md, t.spacing.md);
        mainLayout->setSpacing(0);
        horizontalLayoutWidget_4 = new QWidget(ColorizeProcess);
        horizontalLayoutWidget_4->setObjectName("horizontalLayoutWidget_4");
        mainLayout->addWidget(horizontalLayoutWidget_4);
        horizontalLayoutMain = new QHBoxLayout(horizontalLayoutWidget_4);
        horizontalLayoutMain->setSpacing(t.spacing.md);
        horizontalLayoutMain->setObjectName("horizontalLayoutMain");
        horizontalLayoutMain->setSizeConstraint(QLayout::SizeConstraint::SetMinimumSize);
        horizontalLayoutMain->setContentsMargins(0, 0, 0, 0);
        verticalLayout = new QVBoxLayout();
        verticalLayout->setObjectName("verticalLayout");
        verticalLayout->setSizeConstraint(QLayout::SizeConstraint::SetDefaultConstraint);
        verticalLayout->setContentsMargins(-1, t.spacing.md, -1, -1);
        horizontalLayout_5 = new QHBoxLayout();
        horizontalLayout_5->setObjectName("horizontalLayout_5");
        chartScrollArea = new QScrollArea(horizontalLayoutWidget_4);
        chartScrollArea->setObjectName("chartScrollArea");
        chartScrollArea->setMinimumSize(QSize(380, 220));
        chartScrollArea->setWidgetResizable(true);
        scrollAreaWidgetContents = new QWidget();
        scrollAreaWidgetContents->setObjectName("scrollAreaWidgetContents");
        scrollAreaWidgetContents->setMinimumSize(QSize(378, 218));
        chartScrollArea->setWidget(scrollAreaWidgetContents);

        horizontalLayout_5->addWidget(chartScrollArea);

        verticalLayout->addLayout(horizontalLayout_5);

        histogramSettingsLayout_2 = new QHBoxLayout();
        histogramSettingsLayout_2->setObjectName("histogramSettingsLayout_2");
        const auto toolbar = rt_preview_ui::buildHistogramToolbar(horizontalLayoutWidget_4, histogramSettingsLayout_2,
                                                                  {.translationContext = "ColorizeProcess"});

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

        horizontalLayout_6 = new QHBoxLayout();
        horizontalLayout_6->setObjectName("horizontalLayout_6");
        label_5 = new QLabel(horizontalLayoutWidget_4);
        label_5->setObjectName("label_5");
        label_5->setMinimumSize(QSize(60, 0));
        label_5->setMaximumSize(QSize(60, 16777215));

        horizontalLayout_6->addWidget(label_5);

        colorizeMethod = new QComboBox(horizontalLayoutWidget_4);
        colorizeMethod->setObjectName("colorizeMethod");
        colorizeMethod->setMinimumWidth(150);
        colorizeMethod->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);

        horizontalLayout_6->addWidget(colorizeMethod);

        horizontalSpacer_3 = new QSpacerItem(40, 20, QSizePolicy::Policy::Expanding, QSizePolicy::Policy::Minimum);

        horizontalLayout_6->addItem(horizontalSpacer_3);

        verticalLayout->addLayout(horizontalLayout_6);

        horizontalLayout = new QHBoxLayout();
        horizontalLayout->setObjectName("horizontalLayout");
        label = new QLabel(horizontalLayoutWidget_4);
        label->setObjectName("label");
        label->setMinimumSize(QSize(60, 0));

        horizontalLayout->addWidget(label);

        hueValue = new QDoubleSpinBox(horizontalLayoutWidget_4);
        hueValue->setObjectName("hueValue");
        hueValue->setMinimumSize(QSize(80, 0));
        hueValue->setMaximum(360.000000000000000);

        horizontalLayout->addWidget(hueValue);

        hueSlider = new QSlider(horizontalLayoutWidget_4);
        hueSlider->setObjectName("hueSlider");
        hueSlider->setMinimumSize(QSize(120, 0));
        hueSlider->setMinimum(-150);
        hueSlider->setMaximum(150);
        hueSlider->setOrientation(Qt::Orientation::Horizontal);
        hueSlider->setMinimumWidth(150);

        horizontalLayout->addWidget(hueSlider);

        verticalLayout->addLayout(horizontalLayout);

        horizontalLayout_2 = new QHBoxLayout();
        horizontalLayout_2->setObjectName("horizontalLayout_2");
        label_2 = new QLabel(horizontalLayoutWidget_4);
        label_2->setObjectName("label_2");
        label_2->setMinimumSize(QSize(60, 0));

        horizontalLayout_2->addWidget(label_2);

        saturationValue = new QDoubleSpinBox(horizontalLayoutWidget_4);
        saturationValue->setObjectName("saturationValue");
        saturationValue->setMinimumSize(QSize(80, 0));
        saturationValue->setMaximum(1.000000000000000);
        saturationValue->setSingleStep(0.010000000000000);
        saturationValue->setValue(1.000000000000000);

        horizontalLayout_2->addWidget(saturationValue);

        saturationSlider = new QSlider(horizontalLayoutWidget_4);
        saturationSlider->setObjectName("saturationSlider");
        saturationSlider->setEnabled(true);
        saturationSlider->setMinimumSize(QSize(120, 0));
        saturationSlider->setMinimum(-100);
        saturationSlider->setMaximum(100);
        saturationSlider->setOrientation(Qt::Orientation::Horizontal);
        saturationSlider->setMinimumWidth(150);

        horizontalLayout_2->addWidget(saturationSlider);

        verticalLayout->addLayout(horizontalLayout_2);

        horizontalLayout_7 = new QHBoxLayout();
        horizontalLayout_7->setObjectName("horizontalLayout_7");
        useLightness = new QCheckBox(horizontalLayoutWidget_4);
        useLightness->setObjectName("useLightness");

        horizontalLayout_7->addWidget(useLightness);

        verticalLayout->addLayout(horizontalLayout_7);

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

        verticalLayout->addLayout(horizontalLayout_3);

        const auto split = rt_preview_ui::buildMainProcessSplitter(horizontalLayoutMain, horizontalLayoutWidget_4);
        split.controlsLayout->setContentsMargins(t.spacing.lg, t.spacing.sm, t.spacing.lg, t.spacing.sm);
        split.controlsLayout->addLayout(verticalLayout);

        verticalLayout_2 = new QVBoxLayout();
        verticalLayout_2->setObjectName("verticalLayout_2");
        widget = new QWidget(split.previewPanel);
        widget->setObjectName("widget");

        verticalLayout_2->addWidget(widget);

        split.previewLayout->addLayout(verticalLayout_2);

        retranslateUi(ColorizeProcess);

        QMetaObject::connectSlotsByName(ColorizeProcess);
    } // setupUi

    void retranslateUi(QWidget* ColorizeProcess)
    {
        ColorizeProcess->setWindowTitle(QCoreApplication::translate("ColorizeProcess", "Colorize", nullptr));
#if QT_CONFIG(tooltip)
        zoomIn->setToolTip(QCoreApplication::translate("ColorizeProcess", "Zoom in", nullptr));
#endif // QT_CONFIG(tooltip)
        zoomIn->setText(QString());
#if QT_CONFIG(tooltip)
        zoomOut->setToolTip(QCoreApplication::translate("ColorizeProcess", "Zoom out", nullptr));
#endif // QT_CONFIG(tooltip)
        zoomOut->setText(QString());
#if QT_CONFIG(tooltip)
        zoomReset->setToolTip(QCoreApplication::translate("ColorizeProcess", "Zoom reset", nullptr));
#endif // QT_CONFIG(tooltip)
        zoomReset->setText(QString());
#if QT_CONFIG(tooltip)
        gridButton->setToolTip(QCoreApplication::translate("ColorizeProcess", "Show grid", nullptr));
#endif // QT_CONFIG(tooltip)
        gridButton->setText(QString());
#if QT_CONFIG(tooltip)
        showClippingInfo->setToolTip(QCoreApplication::translate("ColorizeProcess", "Show clipping info", nullptr));
#endif // QT_CONFIG(tooltip)
        showClippingInfo->setText(QString());
#if QT_CONFIG(tooltip)
        localChannelNormalization->setToolTip(
            QCoreApplication::translate("ColorizeProcess", "Absolute scales", nullptr));
#endif // QT_CONFIG(tooltip)
        localChannelNormalization->setText(QString());
#if QT_CONFIG(tooltip)
        showHistogramAreas->setToolTip(QCoreApplication::translate("ColorizeProcess", "Draw histogram areas", nullptr));
#endif // QT_CONFIG(tooltip)
        showHistogramAreas->setText(QString());
#if QT_CONFIG(tooltip)
        showHistogram->setToolTip(QCoreApplication::translate("ColorizeProcess", "Show histogram", nullptr));
#endif // QT_CONFIG(tooltip)
        showHistogram->setText(QString());
#if QT_CONFIG(tooltip)
        plotResolution->setToolTip(QCoreApplication::translate("ColorizeProcess", "Plot resolution", nullptr));
#endif // QT_CONFIG(tooltip)
        label_5->setText(QCoreApplication::translate("ColorizeProcess", "Method", nullptr));
        label->setText(QCoreApplication::translate("ColorizeProcess", "Hue", nullptr));
        label_2->setText(QCoreApplication::translate("ColorizeProcess", "Saturation", nullptr));
        useLightness->setText(QCoreApplication::translate("ColorizeProcess", "Use lightness", nullptr));
        label_3->setText(QCoreApplication::translate("ColorizeProcess", "Target Image ", nullptr));
        preview->setText(QCoreApplication::translate("ColorizeProcess", "Preview", nullptr));

        colorizeMethod->setToolTip(
            QCoreApplication::translate("ColorizeProcess", "Choose the colorization model.", nullptr));
        hueValue->setToolTip(QCoreApplication::translate("ColorizeProcess", "Target hue angle in degrees.", nullptr));
        hueSlider->setToolTip(QCoreApplication::translate("ColorizeProcess", "Target hue angle in degrees.", nullptr));
        saturationValue->setToolTip(
            QCoreApplication::translate("ColorizeProcess", "Target saturation scaling factor.", nullptr));
        saturationSlider->setToolTip(
            QCoreApplication::translate("ColorizeProcess", "Target saturation scaling factor.", nullptr));
        useLightness->setToolTip(QCoreApplication::translate(
            "ColorizeProcess", "When enabled for RGB images, preserves perceived lightness during colorization.",
            nullptr));
        targetImageSelector->setToolTip(
            QCoreApplication::translate("ColorizeProcess", "Choose the open image to process.", nullptr));
        preview->setToolTip(
            QCoreApplication::translate("ColorizeProcess", "Enable or disable live preview updates.", nullptr));

        if (auto* btn = buttonBox->button(QDialogButtonBox::Close))
            btn->setToolTip(QCoreApplication::translate("ColorizeProcess",
                                                        "Close this dialog without applying any changes.", nullptr));
        if (auto* btn = buttonBox->button(QDialogButtonBox::Reset))
            btn->setToolTip(QCoreApplication::translate("ColorizeProcess",
                                                        "Reset all parameters to their default values.", nullptr));
    } // retranslateUi
};

inline ColorizeProcessUiElements buildColorizeProcessUi(QWidget* parent)
{
    ColorizeProcessUiElements ui{};
    if (!parent)
    {
        return ui;
    }

    ui.setupUi(parent);
    return ui;
}

} // namespace an::ui::processes
