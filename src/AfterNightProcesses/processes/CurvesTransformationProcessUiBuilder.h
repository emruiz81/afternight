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
#include <QtWidgets/QFormLayout>
#include <QtWidgets/QFrame>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QScrollArea>
#include <QtWidgets/QSizePolicy>
#include <QtWidgets/QSpacerItem>
#include <QtWidgets/QToolButton>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QWidget>

#include "processes/RTPreviewUiCommon.h"
#include "theme/AnDesignTokens.h"

namespace an::ui::processes
{

class CurvesTransformationProcessUiElements
{
public:
    QWidget* horizontalLayoutWidget_4;
    QHBoxLayout* horizontalLayoutMain;
    QVBoxLayout* verticalLayout;
    QHBoxLayout* curvesEditorLayout_2;
    QScrollArea* curvesEditorScrollArea;
    QWidget* scrollAreaWidgetContents;
    QHBoxLayout* histogramSettingsLayout;
    QToolButton* zoomIn;
    QToolButton* zoomOut;
    QToolButton* zoomReset;
    QFrame* line_3;
    QToolButton* gridButton;
    QToolButton* guideLine;
    QToolButton* showHistogramAreas;
    QToolButton* histogramButton;
    QFrame* line_4;
    QSpacerItem* horizontalSpacer_4;
    QHBoxLayout* channelSelectorLayout;
    QLabel* label_2;
    QComboBox* interpolationSelector;
    QSpacerItem* horizontalSpacer_5;
    QSpacerItem* verticalSpacer;
    QFormLayout* formLayout;
    QLabel* label_3;
    QComboBox* targetImageSelector;
    QLabel* label_4;
    QComboBox* channelSelector2;
    QHBoxLayout* horizontalLayout_4;
    QCheckBox* preview;
    QSpacerItem* horizontalSpacer;
    QHBoxLayout* horizontalLayout_3;
    QDialogButtonBox* buttonBox;
    QVBoxLayout* verticalLayout_2;
    QWidget* widget;

    void setupUi(QWidget* CurvesTransformationProcess)
    {
        if (CurvesTransformationProcess->objectName().isEmpty())
            CurvesTransformationProcess->setObjectName("CurvesTransformationProcess");
        const auto& t = an::ui::tokens();
        CurvesTransformationProcess->resize(908, 582);
        CurvesTransformationProcess->setMinimumSize(QSize(460, 500));
        horizontalLayoutWidget_4 = CurvesTransformationProcess;
        horizontalLayoutWidget_4->setObjectName("horizontalLayoutWidget_4");
        horizontalLayoutMain = new QHBoxLayout(horizontalLayoutWidget_4);
        horizontalLayoutMain->setSpacing(t.spacing.md);
        horizontalLayoutMain->setObjectName("horizontalLayoutMain");
        horizontalLayoutMain->setSizeConstraint(QLayout::SizeConstraint::SetDefaultConstraint);
        horizontalLayoutMain->setContentsMargins(t.spacing.md, t.spacing.md, t.spacing.md, t.spacing.md);
        verticalLayout = new QVBoxLayout();
        verticalLayout->setObjectName("verticalLayout");
        verticalLayout->setSizeConstraint(QLayout::SizeConstraint::SetDefaultConstraint);
        verticalLayout->setContentsMargins(-1, t.spacing.md, -1, -1);
        curvesEditorLayout_2 = new QHBoxLayout();
        curvesEditorLayout_2->setObjectName("curvesEditorLayout_2");
        curvesEditorLayout_2->setContentsMargins(0, 0, 0, 0);
        curvesEditorScrollArea = new QScrollArea(horizontalLayoutWidget_4);
        curvesEditorScrollArea->setObjectName("curvesEditorScrollArea");
        curvesEditorScrollArea->setMinimumSize(QSize(350, 250));
        curvesEditorScrollArea->setWidgetResizable(true);
        scrollAreaWidgetContents = new QWidget();
        scrollAreaWidgetContents->setObjectName("scrollAreaWidgetContents");
        scrollAreaWidgetContents->setMinimumSize(QSize(348, 248));
        curvesEditorScrollArea->setWidget(scrollAreaWidgetContents);

        curvesEditorLayout_2->addWidget(curvesEditorScrollArea);

        verticalLayout->addLayout(curvesEditorLayout_2);

        histogramSettingsLayout = new QHBoxLayout();
        histogramSettingsLayout->setObjectName("histogramSettingsLayout");
        histogramSettingsLayout->setContentsMargins(t.spacing.md, -1, t.spacing.md, -1);
        zoomIn = new QToolButton(horizontalLayoutWidget_4);
        zoomIn->setObjectName("zoomIn");
        rt_preview_ui::initToolbarButton(zoomIn,
                                         QCoreApplication::translate("CurvesTransformationProcess", "Zoom in", nullptr),
                                         QStringLiteral("zoom_in"));

        histogramSettingsLayout->addWidget(zoomIn);

        zoomOut = new QToolButton(horizontalLayoutWidget_4);
        zoomOut->setObjectName("zoomOut");
        rt_preview_ui::initToolbarButton(
            zoomOut, QCoreApplication::translate("CurvesTransformationProcess", "Zoom out", nullptr),
            QStringLiteral("zoom_out"));

        histogramSettingsLayout->addWidget(zoomOut);

        zoomReset = new QToolButton(horizontalLayoutWidget_4);
        zoomReset->setObjectName("zoomReset");
        rt_preview_ui::initToolbarButton(
            zoomReset, QCoreApplication::translate("CurvesTransformationProcess", "Zoom reset", nullptr),
            QStringLiteral("zoom_fit"));
        zoomReset->setCheckable(false);

        histogramSettingsLayout->addWidget(zoomReset);

        line_3 = new QFrame(horizontalLayoutWidget_4);
        line_3->setObjectName("line_3");
        line_3->setFrameShape(QFrame::Shape::VLine);
        line_3->setFrameShadow(QFrame::Shadow::Sunken);

        histogramSettingsLayout->addWidget(line_3);

        gridButton = new QToolButton(horizontalLayoutWidget_4);
        gridButton->setObjectName("gridButton");
        rt_preview_ui::initToolbarButton(
            gridButton, QCoreApplication::translate("CurvesTransformationProcess", "Show grid", nullptr),
            QStringLiteral("grid"));
        gridButton->setCheckable(true);
        gridButton->setChecked(true);

        histogramSettingsLayout->addWidget(gridButton);

        guideLine = new QToolButton(horizontalLayoutWidget_4);
        guideLine->setObjectName("guideLine");
        rt_preview_ui::initToolbarButton(
            guideLine, QCoreApplication::translate("CurvesTransformationProcess", "Show guide", nullptr),
            QStringLiteral("stat_line"));
        guideLine->setCheckable(true);
        guideLine->setChecked(true);

        histogramSettingsLayout->addWidget(guideLine);

        showHistogramAreas = new QToolButton(horizontalLayoutWidget_4);
        showHistogramAreas->setObjectName("showHistogramAreas");
        rt_preview_ui::initToolbarButton(
            showHistogramAreas,
            QCoreApplication::translate("CurvesTransformationProcess", "Draw histogram areas", nullptr),
            QStringLiteral("area_chart"));
        showHistogramAreas->setCheckable(true);

        histogramSettingsLayout->addWidget(showHistogramAreas);

        histogramButton = new QToolButton(horizontalLayoutWidget_4);
        histogramButton->setObjectName("histogramButton");
        rt_preview_ui::initToolbarButton(
            histogramButton, QCoreApplication::translate("CurvesTransformationProcess", "Show histogram", nullptr),
            QStringLiteral("histogram"));
        histogramButton->setCheckable(true);
        histogramButton->setChecked(true);

        histogramSettingsLayout->addWidget(histogramButton);

        line_4 = new QFrame(horizontalLayoutWidget_4);
        line_4->setObjectName("line_4");
        line_4->setFrameShape(QFrame::Shape::VLine);
        line_4->setFrameShadow(QFrame::Shadow::Sunken);

        histogramSettingsLayout->addWidget(line_4);

        horizontalSpacer_4 = new QSpacerItem(40, 20, QSizePolicy::Policy::Expanding, QSizePolicy::Policy::Minimum);

        histogramSettingsLayout->addItem(horizontalSpacer_4);

        verticalLayout->addLayout(histogramSettingsLayout);

        channelSelectorLayout = new QHBoxLayout();
        channelSelectorLayout->setObjectName("channelSelectorLayout");
        channelSelectorLayout->setContentsMargins(t.spacing.md, -1, t.spacing.md, -1);
        label_2 = new QLabel(horizontalLayoutWidget_4);
        label_2->setObjectName("label_2");

        channelSelectorLayout->addWidget(label_2);

        interpolationSelector = new QComboBox(horizontalLayoutWidget_4);
        interpolationSelector->setObjectName("interpolationSelector");
        interpolationSelector->setMinimumWidth(150);
        interpolationSelector->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);

        channelSelectorLayout->addWidget(interpolationSelector);

        horizontalSpacer_5 = new QSpacerItem(40, 20, QSizePolicy::Policy::Expanding, QSizePolicy::Policy::Minimum);

        channelSelectorLayout->addItem(horizontalSpacer_5);

        channelSelectorLayout->setStretch(2, 8);

        verticalLayout->addLayout(channelSelectorLayout);

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

        channelSelector2 = new QComboBox(horizontalLayoutWidget_4);
        channelSelector2->setObjectName("channelSelector2");
        channelSelector2->setMinimumWidth(150);
        channelSelector2->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);

        formLayout->setWidget(1, QFormLayout::ItemRole::FieldRole, channelSelector2);

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
            btn->setToolTip(QCoreApplication::translate("CurvesTransformationProcess",
                                                        "Close this dialog without applying any changes.", nullptr));
        if (auto* btn = buttonBox->button(QDialogButtonBox::Reset))
            btn->setToolTip(QCoreApplication::translate("CurvesTransformationProcess",
                                                        "Reset all parameters to their default values.", nullptr));

        verticalLayout->addLayout(horizontalLayout_3);

        const auto split = rt_preview_ui::buildMainProcessSplitter(horizontalLayoutMain, horizontalLayoutWidget_4);
        split.controlsLayout->addLayout(verticalLayout);

        verticalLayout_2 = new QVBoxLayout();
        verticalLayout_2->setObjectName("verticalLayout_2");
        widget = new QWidget(split.previewPanel);
        widget->setObjectName("widget");

        verticalLayout_2->addWidget(widget);

        split.previewLayout->addLayout(verticalLayout_2);

        if (split.mainSplitter)
        {
            const int desiredControlsWidth = 620;
            int previewWidth = CurvesTransformationProcess->width() - desiredControlsWidth;
            if (previewWidth < 1)
            {
                previewWidth = 1;
            }

            split.mainSplitter->setSizes({desiredControlsWidth, previewWidth});
        }

        retranslateUi(CurvesTransformationProcess);

        QMetaObject::connectSlotsByName(CurvesTransformationProcess);
    } // setupUi

    void retranslateUi(QWidget* CurvesTransformationProcess)
    {
        CurvesTransformationProcess->setWindowTitle(
            QCoreApplication::translate("CurvesTransformationProcess", "Curves Transformation", nullptr));
#if QT_CONFIG(tooltip)
        zoomIn->setToolTip(QCoreApplication::translate("CurvesTransformationProcess", "Zoom in", nullptr));
#endif // QT_CONFIG(tooltip)
        zoomIn->setText(QString());
#if QT_CONFIG(tooltip)
        zoomOut->setToolTip(QCoreApplication::translate("CurvesTransformationProcess", "Zoom out", nullptr));
#endif // QT_CONFIG(tooltip)
        zoomOut->setText(QString());
#if QT_CONFIG(tooltip)
        zoomReset->setToolTip(QCoreApplication::translate("CurvesTransformationProcess", "Zoom reset", nullptr));
#endif // QT_CONFIG(tooltip)
        zoomReset->setText(QString());
#if QT_CONFIG(tooltip)
        gridButton->setToolTip(QCoreApplication::translate("CurvesTransformationProcess", "Show grid", nullptr));
#endif // QT_CONFIG(tooltip)
        gridButton->setText(QString());
#if QT_CONFIG(tooltip)
        guideLine->setToolTip(QCoreApplication::translate("CurvesTransformationProcess", "Show guide", nullptr));
#endif // QT_CONFIG(tooltip)
        guideLine->setText(QString());
#if QT_CONFIG(tooltip)
        showHistogramAreas->setToolTip(
            QCoreApplication::translate("CurvesTransformationProcess", "Draw histogram areas", nullptr));
#endif // QT_CONFIG(tooltip)
        showHistogramAreas->setText(QString());
#if QT_CONFIG(tooltip)
        histogramButton->setToolTip(
            QCoreApplication::translate("CurvesTransformationProcess", "Show histogram", nullptr));
#endif // QT_CONFIG(tooltip)
        histogramButton->setText(QString());
        label_2->setText(QCoreApplication::translate("CurvesTransformationProcess", "Interpolation", nullptr));
#if QT_CONFIG(tooltip)
        interpolationSelector->setToolTip(QCoreApplication::translate(
            "CurvesTransformationProcess", "Select the interpolation method used to evaluate the transfer curve.",
            nullptr));
#endif // QT_CONFIG(tooltip)
        label_3->setText(QCoreApplication::translate("CurvesTransformationProcess", "Target Image ", nullptr));
#if QT_CONFIG(tooltip)
        targetImageSelector->setToolTip(
            QCoreApplication::translate("CurvesTransformationProcess", "Choose the open image to process.", nullptr));
#endif // QT_CONFIG(tooltip)
        label_4->setText(QCoreApplication::translate("CurvesTransformationProcess", "Channel", nullptr));
#if QT_CONFIG(tooltip)
        channelSelector2->setToolTip(QCoreApplication::translate(
            "CurvesTransformationProcess", "Select the channel or color space component to edit.", nullptr));
#endif // QT_CONFIG(tooltip)
        preview->setText(QCoreApplication::translate("CurvesTransformationProcess", "Preview", nullptr));
#if QT_CONFIG(tooltip)
        preview->setToolTip(QCoreApplication::translate("CurvesTransformationProcess",
                                                        "Enable or disable live preview updates.", nullptr));
#endif // QT_CONFIG(tooltip)
    } // retranslateUi
};

inline CurvesTransformationProcessUiElements buildCurvesTransformationProcessUi(QWidget* parent)
{
    CurvesTransformationProcessUiElements ui{};
    if (!parent)
    {
        return ui;
    }

    ui.setupUi(parent);
    return ui;
}

} // namespace an::ui::processes
