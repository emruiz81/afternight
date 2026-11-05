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

class CLAHEProcessUiElements
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
    QLabel* label;
    QSpinBox* gridSizeValue;
    QSlider* gridSizeSlider;
    QLabel* label_2;
    QDoubleSpinBox* contrastLimitValue;
    QSlider* contrastLimitSlider;
    QLabel* label_5;
    QDoubleSpinBox* amountValue;
    QSlider* amountSlider;
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

    void setupUi(QWidget* CLAHEProcess)
    {
        if (CLAHEProcess->objectName().isEmpty())
            CLAHEProcess->setObjectName("CLAHEProcess");
        const auto& t = an::ui::tokens();
        CLAHEProcess->resize(746, 595);
        CLAHEProcess->setMinimumSize(QSize(300, 200));
        CLAHEProcess->setSizePolicy(QSizePolicy::Policy::Preferred, QSizePolicy::Policy::Preferred);
        auto* mainLayout = new QVBoxLayout(CLAHEProcess);
        mainLayout->setObjectName("mainLayout");
        mainLayout->setContentsMargins(t.spacing.md, t.spacing.md, t.spacing.md, t.spacing.md);
        mainLayout->setSpacing(0);
        horizontalLayoutWidget_4 = new QWidget(CLAHEProcess);
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
        const auto toolbar = rt_preview_ui::buildHistogramToolbar(horizontalLayoutWidget_4, histogramSettingsLayout_2,
                                                                  {.translationContext = "CLAHEProcess"});

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
        gridLayout->setVerticalSpacing(t.spacing.sm);
        gridLayout->setContentsMargins(0, 0, 0, 0);
        label = new QLabel(horizontalLayoutWidget_4);
        label->setObjectName("label");

        gridLayout->addWidget(label, 0, 0, 1, 1);

        gridSizeValue = new QSpinBox(horizontalLayoutWidget_4);
        gridSizeValue->setObjectName("gridSizeValue");
        gridSizeValue->setMinimum(1);
        gridSizeValue->setMaximum(256);
        gridSizeValue->setValue(8);
        gridSizeValue->setMinimumWidth(80);

        gridLayout->addWidget(gridSizeValue, 0, 1, 1, 1);

        gridSizeSlider = new QSlider(horizontalLayoutWidget_4);
        gridSizeSlider->setObjectName("gridSizeSlider");
        gridSizeSlider->setMinimum(1);
        gridSizeSlider->setMaximum(256);
        gridSizeSlider->setValue(8);
        gridSizeSlider->setOrientation(Qt::Orientation::Horizontal);

        gridLayout->addWidget(gridSizeSlider, 0, 2, 1, 1);

        label_2 = new QLabel(horizontalLayoutWidget_4);
        label_2->setObjectName("label_2");

        gridLayout->addWidget(label_2, 1, 0, 1, 1);

        contrastLimitValue = new QDoubleSpinBox(horizontalLayoutWidget_4);
        contrastLimitValue->setObjectName("contrastLimitValue");
        contrastLimitValue->setMinimum(1.000000000000000);
        contrastLimitValue->setMaximum(64.000000000000000);
        contrastLimitValue->setSingleStep(0.500000000000000);
        contrastLimitValue->setStepType(QAbstractSpinBox::StepType::AdaptiveDecimalStepType);
        contrastLimitValue->setValue(2.000000000000000);
        contrastLimitValue->setMinimumWidth(80);

        gridLayout->addWidget(contrastLimitValue, 1, 1, 1, 1);

        contrastLimitSlider = new QSlider(horizontalLayoutWidget_4);
        contrastLimitSlider->setObjectName("contrastLimitSlider");
        contrastLimitSlider->setMinimum(10);
        contrastLimitSlider->setMaximum(640);
        contrastLimitSlider->setValue(20);
        contrastLimitSlider->setOrientation(Qt::Orientation::Horizontal);

        gridLayout->addWidget(contrastLimitSlider, 1, 2, 1, 1);

        label_5 = new QLabel(horizontalLayoutWidget_4);
        label_5->setObjectName("label_5");

        gridLayout->addWidget(label_5, 2, 0, 1, 1);

        amountValue = new QDoubleSpinBox(horizontalLayoutWidget_4);
        amountValue->setObjectName("amountValue");
        amountValue->setMinimum(0.000000000000000);
        amountValue->setMaximum(1.000000000000000);
        amountValue->setSingleStep(0.050000000000000);
        amountValue->setStepType(QAbstractSpinBox::StepType::AdaptiveDecimalStepType);
        amountValue->setValue(1.000000000000000);
        amountValue->setMinimumWidth(80);

        gridLayout->addWidget(amountValue, 2, 1, 1, 1);

        amountSlider = new QSlider(horizontalLayoutWidget_4);
        amountSlider->setObjectName("amountSlider");
        amountSlider->setMinimum(0);
        amountSlider->setMaximum(1000);
        amountSlider->setValue(1000);
        amountSlider->setOrientation(Qt::Orientation::Horizontal);

        gridLayout->addWidget(amountSlider, 2, 2, 1, 1);

        gridLayout->setColumnStretch(1, 1);
        gridLayout->setColumnStretch(2, 6);

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

        retranslateUi(CLAHEProcess);

        QMetaObject::connectSlotsByName(CLAHEProcess);
    } // setupUi

    void retranslateUi(QWidget* CLAHEProcess)
    {
        CLAHEProcess->setWindowTitle(QCoreApplication::translate("CLAHEProcess", "CLAHE", nullptr));
#if QT_CONFIG(tooltip)
        zoomIn->setToolTip(QCoreApplication::translate("CLAHEProcess", "Zoom in", nullptr));
#endif // QT_CONFIG(tooltip)
        zoomIn->setText(QString());
#if QT_CONFIG(tooltip)
        zoomOut->setToolTip(QCoreApplication::translate("CLAHEProcess", "Zoom out", nullptr));
#endif // QT_CONFIG(tooltip)
        zoomOut->setText(QString());
#if QT_CONFIG(tooltip)
        zoomReset->setToolTip(QCoreApplication::translate("CLAHEProcess", "Zoom reset", nullptr));
#endif // QT_CONFIG(tooltip)
        zoomReset->setText(QString());
#if QT_CONFIG(tooltip)
        gridButton->setToolTip(QCoreApplication::translate("CLAHEProcess", "Show grid", nullptr));
#endif // QT_CONFIG(tooltip)
        gridButton->setText(QString());
#if QT_CONFIG(tooltip)
        showClippingInfo->setToolTip(QCoreApplication::translate("CLAHEProcess", "Show clipping info", nullptr));
#endif // QT_CONFIG(tooltip)
        showClippingInfo->setText(QString());
#if QT_CONFIG(tooltip)
        localChannelNormalization->setToolTip(QCoreApplication::translate("CLAHEProcess", "Absolute scales", nullptr));
#endif // QT_CONFIG(tooltip)
        localChannelNormalization->setText(QString());
#if QT_CONFIG(tooltip)
        showHistogramAreas->setToolTip(QCoreApplication::translate("CLAHEProcess", "Draw histogram areas", nullptr));
#endif // QT_CONFIG(tooltip)
        showHistogramAreas->setText(QString());
#if QT_CONFIG(tooltip)
        showHistogram->setToolTip(QCoreApplication::translate("CLAHEProcess", "Show histogram", nullptr));
#endif // QT_CONFIG(tooltip)
        showHistogram->setText(QString());
#if QT_CONFIG(tooltip)
        plotResolution->setToolTip(QCoreApplication::translate("CLAHEProcess", "Plot resolution", nullptr));
#endif // QT_CONFIG(tooltip)
        label->setText(QCoreApplication::translate("CLAHEProcess", "Grid Size", nullptr));
        label_2->setText(QCoreApplication::translate("CLAHEProcess", "Contrast Limit", nullptr));
        label_5->setText(QCoreApplication::translate("CLAHEProcess", "Amount", nullptr));
        label_3->setText(QCoreApplication::translate("CLAHEProcess", "Target Image ", nullptr));
        label_4->setText(QCoreApplication::translate("CLAHEProcess", "Channel", nullptr));
        preview->setText(QCoreApplication::translate("CLAHEProcess", "Preview", nullptr));

        gridSizeValue->setToolTip(
            QCoreApplication::translate("CLAHEProcess", "Tile grid size used by CLAHE.", nullptr));
        gridSizeSlider->setToolTip(
            QCoreApplication::translate("CLAHEProcess", "Tile grid size used by CLAHE.", nullptr));
        contrastLimitValue->setToolTip(QCoreApplication::translate(
            "CLAHEProcess", "Contrast clipping limit for local histogram equalization.", nullptr));
        contrastLimitSlider->setToolTip(QCoreApplication::translate(
            "CLAHEProcess", "Contrast clipping limit for local histogram equalization.", nullptr));
        amountValue->setToolTip(
            QCoreApplication::translate("CLAHEProcess", "Blend amount for this process result.", nullptr));
        amountSlider->setToolTip(
            QCoreApplication::translate("CLAHEProcess", "Blend amount for this process result.", nullptr));
        targetImageSelector->setToolTip(
            QCoreApplication::translate("CLAHEProcess", "Choose the open image to process.", nullptr));
        channelSelector->setToolTip(QCoreApplication::translate(
            "CLAHEProcess", "Select the channel to process, or RGB to process all channels together.", nullptr));
        preview->setToolTip(
            QCoreApplication::translate("CLAHEProcess", "Enable or disable live preview updates.", nullptr));

        if (auto* btn = buttonBox->button(QDialogButtonBox::Close))
            btn->setToolTip(QCoreApplication::translate("CLAHEProcess",
                                                        "Close this dialog without applying any changes.", nullptr));
        if (auto* btn = buttonBox->button(QDialogButtonBox::Reset))
            btn->setToolTip(
                QCoreApplication::translate("CLAHEProcess", "Reset all parameters to their default values.", nullptr));
    } // retranslateUi
};

inline CLAHEProcessUiElements buildCLAHEProcessUi(QWidget* parent)
{
    CLAHEProcessUiElements ui{};
    if (!parent)
    {
        return ui;
    }

    ui.setupUi(parent);
    return ui;
}

} // namespace an::ui::processes
