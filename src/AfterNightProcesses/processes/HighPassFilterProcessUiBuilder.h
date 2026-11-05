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

class HighPassFilterProcessUiElements
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
    QHBoxLayout* horizontalLayout;
    QLabel* label;
    QDoubleSpinBox* radiusValue;
    QSlider* radiusSlider;
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

    void setupUi(QWidget* HighPassFilterProcess)
    {
        const auto& t = an::ui::tokens();

        if (HighPassFilterProcess->objectName().isEmpty())
            HighPassFilterProcess->setObjectName("HighPassFilterProcess");
        HighPassFilterProcess->resize(791, 611);
        HighPassFilterProcess->setMinimumSize(QSize(300, 200));
        HighPassFilterProcess->setSizePolicy(QSizePolicy::Policy::Preferred, QSizePolicy::Policy::Preferred);
        auto* mainLayout = new QVBoxLayout(HighPassFilterProcess);
        mainLayout->setObjectName("mainLayout");
        mainLayout->setContentsMargins(t.spacing.md, t.spacing.md, t.spacing.md, t.spacing.md);
        mainLayout->setSpacing(0);
        horizontalLayoutWidget_4 = new QWidget(HighPassFilterProcess);
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
                                                                  {.translationContext = "HighPassFilterProcess"});

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

        horizontalLayout = new QHBoxLayout();
        horizontalLayout->setObjectName("horizontalLayout");
        label = new QLabel(horizontalLayoutWidget_4);
        label->setObjectName("label");

        horizontalLayout->addWidget(label);

        radiusValue = new QDoubleSpinBox(horizontalLayoutWidget_4);
        radiusValue->setObjectName("radiusValue");
        radiusValue->setMinimum(0.100000000000000);
        radiusValue->setMaximum(1000.000000000000000);
        radiusValue->setSingleStep(0.500000000000000);
        radiusValue->setValue(10.000000000000000);
        radiusValue->setMinimumWidth(80);

        horizontalLayout->addWidget(radiusValue);

        radiusSlider = new QSlider(horizontalLayoutWidget_4);
        radiusSlider->setObjectName("radiusSlider");
        radiusSlider->setMinimumSize(QSize(120, 0));
        radiusSlider->setMinimum(0);
        radiusSlider->setMaximum(100);
        radiusSlider->setOrientation(Qt::Orientation::Horizontal);

        horizontalLayout->addWidget(radiusSlider);

        verticalLayout->addLayout(horizontalLayout);

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
        split.controlsLayout->addLayout(verticalLayout);

        verticalLayout_2 = new QVBoxLayout();
        verticalLayout_2->setObjectName("verticalLayout_2");
        widget = new QWidget(split.previewPanel);
        widget->setObjectName("widget");

        verticalLayout_2->addWidget(widget);

        split.previewLayout->addLayout(verticalLayout_2);

        retranslateUi(HighPassFilterProcess);

        QMetaObject::connectSlotsByName(HighPassFilterProcess);
    } // setupUi

    void retranslateUi(QWidget* HighPassFilterProcess)
    {
        HighPassFilterProcess->setWindowTitle(
            QCoreApplication::translate("HighPassFilterProcess", "High Pass Filter", nullptr));
#if QT_CONFIG(tooltip)
        zoomIn->setToolTip(QCoreApplication::translate("HighPassFilterProcess", "Zoom in", nullptr));
#endif // QT_CONFIG(tooltip)
        zoomIn->setText(QString());
#if QT_CONFIG(tooltip)
        zoomOut->setToolTip(QCoreApplication::translate("HighPassFilterProcess", "Zoom out", nullptr));
#endif // QT_CONFIG(tooltip)
        zoomOut->setText(QString());
#if QT_CONFIG(tooltip)
        zoomReset->setToolTip(QCoreApplication::translate("HighPassFilterProcess", "Zoom reset", nullptr));
#endif // QT_CONFIG(tooltip)
        zoomReset->setText(QString());
#if QT_CONFIG(tooltip)
        gridButton->setToolTip(QCoreApplication::translate("HighPassFilterProcess", "Show grid", nullptr));
#endif // QT_CONFIG(tooltip)
        gridButton->setText(QString());
#if QT_CONFIG(tooltip)
        showClippingInfo->setToolTip(
            QCoreApplication::translate("HighPassFilterProcess", "Show clipping info", nullptr));
#endif // QT_CONFIG(tooltip)
        showClippingInfo->setText(QString());
#if QT_CONFIG(tooltip)
        localChannelNormalization->setToolTip(
            QCoreApplication::translate("HighPassFilterProcess", "Absolute scales", nullptr));
#endif // QT_CONFIG(tooltip)
        localChannelNormalization->setText(QString());
#if QT_CONFIG(tooltip)
        showHistogramAreas->setToolTip(
            QCoreApplication::translate("HighPassFilterProcess", "Draw histogram areas", nullptr));
#endif // QT_CONFIG(tooltip)
        showHistogramAreas->setText(QString());
#if QT_CONFIG(tooltip)
        showHistogram->setToolTip(QCoreApplication::translate("HighPassFilterProcess", "Show histogram", nullptr));
#endif // QT_CONFIG(tooltip)
        showHistogram->setText(QString());
#if QT_CONFIG(tooltip)
        plotResolution->setToolTip(QCoreApplication::translate("HighPassFilterProcess", "Plot resolution", nullptr));
#endif // QT_CONFIG(tooltip)
        label->setText(QCoreApplication::translate("HighPassFilterProcess", "Radius", nullptr));
        label_3->setText(QCoreApplication::translate("HighPassFilterProcess", "Target Image ", nullptr));
        preview->setText(QCoreApplication::translate("HighPassFilterProcess", "Preview", nullptr));

        radiusValue->setToolTip(QCoreApplication::translate(
            "HighPassFilterProcess", "High-pass blur radius used to isolate fine detail.", nullptr));
        radiusSlider->setToolTip(QCoreApplication::translate(
            "HighPassFilterProcess", "High-pass blur radius used to isolate fine detail.", nullptr));
        targetImageSelector->setToolTip(
            QCoreApplication::translate("HighPassFilterProcess", "Choose the open image to process.", nullptr));
        preview->setToolTip(
            QCoreApplication::translate("HighPassFilterProcess", "Enable or disable live preview updates.", nullptr));

        if (auto* btn = buttonBox->button(QDialogButtonBox::Close))
            btn->setToolTip(QCoreApplication::translate("HighPassFilterProcess",
                                                        "Close this dialog without applying any changes.", nullptr));
        if (auto* btn = buttonBox->button(QDialogButtonBox::Reset))
            btn->setToolTip(QCoreApplication::translate("HighPassFilterProcess",
                                                        "Reset all parameters to their default values.", nullptr));
    } // retranslateUi
};

inline HighPassFilterProcessUiElements buildHighPassFilterProcessUi(QWidget* parent)
{
    HighPassFilterProcessUiElements ui{};
    if (!parent)
    {
        return ui;
    }

    ui.setupUi(parent);
    return ui;
}

} // namespace an::ui::processes
