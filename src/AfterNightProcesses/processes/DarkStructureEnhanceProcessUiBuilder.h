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
#include <QtWidgets/QGridLayout>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QSlider>
#include <QtWidgets/QSizePolicy>
#include <QtWidgets/QSpacerItem>
#include <QtWidgets/QSpinBox>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QWidget>

#include "processes/RTPreviewUiCommon.h"
#include "theme/AnDesignTokens.h"

namespace an::ui::processes
{

class DarkStructureEnhanceProcessUiElements
{
public:
    QVBoxLayout* mainLayout;
    QWidget* horizontalLayoutWidget;
    QHBoxLayout* horizontalLayoutMain;
    QVBoxLayout* controlsLayout;
    QGridLayout* parameterGrid;
    QLabel* layersLabel;
    QSpinBox* layersValue;
    QSlider* layersSlider;
    QLabel* scalingFunctionLabel;
    QComboBox* scalingFunctionComboBox;
    QLabel* amountLabel;
    QDoubleSpinBox* amountValue;
    QSlider* amountSlider;
    QLabel* iterationsLabel;
    QSpinBox* iterationsValue;
    QSlider* iterationsSlider;
    QLabel* previewModeLabel;
    QComboBox* previewModeComboBox;
    QFormLayout* targetFormLayout;
    QLabel* targetImageLabel;
    QComboBox* targetImageSelector;
    QLabel* channelLabel;
    QComboBox* channelSelector;
    QHBoxLayout* outputLayout;
    QCheckBox* preview;
    QCheckBox* createMaskImage;
    QSpacerItem* outputSpacer;
    QHBoxLayout* buttonLayout;
    QDialogButtonBox* buttonBox;
    QVBoxLayout* previewLayout;
    QWidget* widget;

    void setupUi(QWidget* DarkStructureEnhanceProcess)
    {
        const auto& t = an::ui::tokens();

        if (DarkStructureEnhanceProcess->objectName().isEmpty())
        {
            DarkStructureEnhanceProcess->setObjectName("DarkStructureEnhanceProcess");
        }
        DarkStructureEnhanceProcess->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);

        mainLayout = new QVBoxLayout(DarkStructureEnhanceProcess);
        mainLayout->setObjectName("mainLayout");
        mainLayout->setContentsMargins(t.spacing.md, t.spacing.md, t.spacing.md, t.spacing.md);
        mainLayout->setSpacing(0);

        horizontalLayoutWidget = new QWidget(DarkStructureEnhanceProcess);
        horizontalLayoutWidget->setObjectName("horizontalLayoutWidget");
        mainLayout->addWidget(horizontalLayoutWidget);

        horizontalLayoutMain = new QHBoxLayout(horizontalLayoutWidget);
        horizontalLayoutMain->setObjectName("horizontalLayoutMain");
        horizontalLayoutMain->setSizeConstraint(QLayout::SetMinimumSize);
        horizontalLayoutMain->setContentsMargins(t.spacing.md, t.spacing.md, t.spacing.md, t.spacing.md);
        horizontalLayoutMain->setSpacing(t.spacing.md);

        controlsLayout = new QVBoxLayout();
        controlsLayout->setObjectName("controlsLayout");
        controlsLayout->setContentsMargins(-1, t.spacing.md, -1, -1);
        controlsLayout->setSpacing(t.spacing.md);

        parameterGrid = new QGridLayout();
        parameterGrid->setObjectName("parameterGrid");
        parameterGrid->setHorizontalSpacing(t.spacing.md);
        parameterGrid->setVerticalSpacing(t.spacing.sm);

        layersLabel = new QLabel(horizontalLayoutWidget);
        layersLabel->setObjectName("layersLabel");
        parameterGrid->addWidget(layersLabel, 0, 0, 1, 1);

        layersValue = new QSpinBox(horizontalLayoutWidget);
        layersValue->setObjectName("layersValue");
        layersValue->setMinimum(7);
        // Removing more layers needs a starlet deeper than the engine's: eleven asks for taps 1,024 pixels apart.
        layersValue->setMaximum(10);
        layersValue->setValue(8);
        parameterGrid->addWidget(layersValue, 0, 1, 1, 1);

        layersSlider = new QSlider(horizontalLayoutWidget);
        layersSlider->setObjectName("layersSlider");
        layersSlider->setMinimum(7);
        layersSlider->setMaximum(10);
        layersSlider->setValue(8);
        layersSlider->setOrientation(Qt::Horizontal);
        parameterGrid->addWidget(layersSlider, 0, 2, 1, 1);

        scalingFunctionLabel = new QLabel(horizontalLayoutWidget);
        scalingFunctionLabel->setObjectName("scalingFunctionLabel");
        parameterGrid->addWidget(scalingFunctionLabel, 1, 0, 1, 1);

        scalingFunctionComboBox = new QComboBox(horizontalLayoutWidget);
        scalingFunctionComboBox->setObjectName("scalingFunctionComboBox");
        parameterGrid->addWidget(scalingFunctionComboBox, 1, 1, 1, 2);

        amountLabel = new QLabel(horizontalLayoutWidget);
        amountLabel->setObjectName("amountLabel");
        parameterGrid->addWidget(amountLabel, 2, 0, 1, 1);

        amountValue = new QDoubleSpinBox(horizontalLayoutWidget);
        amountValue->setObjectName("amountValue");
        amountValue->setDecimals(2);
        amountValue->setMinimum(0.0);
        amountValue->setMaximum(0.99);
        amountValue->setSingleStep(0.01);
        amountValue->setValue(0.40);
        parameterGrid->addWidget(amountValue, 2, 1, 1, 1);

        amountSlider = new QSlider(horizontalLayoutWidget);
        amountSlider->setObjectName("amountSlider");
        amountSlider->setOrientation(Qt::Horizontal);
        parameterGrid->addWidget(amountSlider, 2, 2, 1, 1);

        iterationsLabel = new QLabel(horizontalLayoutWidget);
        iterationsLabel->setObjectName("iterationsLabel");
        parameterGrid->addWidget(iterationsLabel, 3, 0, 1, 1);

        iterationsValue = new QSpinBox(horizontalLayoutWidget);
        iterationsValue->setObjectName("iterationsValue");
        iterationsValue->setMinimum(1);
        iterationsValue->setMaximum(10);
        iterationsValue->setValue(1);
        parameterGrid->addWidget(iterationsValue, 3, 1, 1, 1);

        iterationsSlider = new QSlider(horizontalLayoutWidget);
        iterationsSlider->setObjectName("iterationsSlider");
        iterationsSlider->setMinimum(1);
        iterationsSlider->setMaximum(10);
        iterationsSlider->setValue(1);
        iterationsSlider->setOrientation(Qt::Horizontal);
        parameterGrid->addWidget(iterationsSlider, 3, 2, 1, 1);

        previewModeLabel = new QLabel(horizontalLayoutWidget);
        previewModeLabel->setObjectName("previewModeLabel");
        parameterGrid->addWidget(previewModeLabel, 4, 0, 1, 1);

        previewModeComboBox = new QComboBox(horizontalLayoutWidget);
        previewModeComboBox->setObjectName("previewModeComboBox");
        parameterGrid->addWidget(previewModeComboBox, 4, 1, 1, 2);

        controlsLayout->addLayout(parameterGrid);

        targetFormLayout = new QFormLayout();
        targetFormLayout->setObjectName("targetFormLayout");
        targetFormLayout->setHorizontalSpacing(t.spacing.md);
        targetFormLayout->setVerticalSpacing(t.spacing.sm);

        targetImageLabel = new QLabel(horizontalLayoutWidget);
        targetImageLabel->setObjectName("targetImageLabel");
        targetFormLayout->setWidget(0, QFormLayout::LabelRole, targetImageLabel);

        targetImageSelector = new QComboBox(horizontalLayoutWidget);
        targetImageSelector->setObjectName("targetImageSelector");
        targetImageSelector->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        targetFormLayout->setWidget(0, QFormLayout::FieldRole, targetImageSelector);

        channelLabel = new QLabel(horizontalLayoutWidget);
        channelLabel->setObjectName("channelLabel");
        targetFormLayout->setWidget(1, QFormLayout::LabelRole, channelLabel);

        channelSelector = new QComboBox(horizontalLayoutWidget);
        channelSelector->setObjectName("channelSelector");
        channelSelector->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        targetFormLayout->setWidget(1, QFormLayout::FieldRole, channelSelector);

        controlsLayout->addLayout(targetFormLayout);

        outputLayout = new QHBoxLayout();
        outputLayout->setObjectName("outputLayout");
        outputLayout->setSpacing(t.spacing.sm);

        preview = new QCheckBox(horizontalLayoutWidget);
        preview->setObjectName("preview");
        preview->setChecked(true);
        outputLayout->addWidget(preview);

        createMaskImage = new QCheckBox(horizontalLayoutWidget);
        createMaskImage->setObjectName("createMaskImage");
        createMaskImage->setChecked(false);
        outputLayout->addWidget(createMaskImage);

        outputSpacer = new QSpacerItem(t.spacing.xl, t.spacing.sm, QSizePolicy::Expanding, QSizePolicy::Minimum);
        outputLayout->addItem(outputSpacer);

        controlsLayout->addLayout(outputLayout);

        buttonLayout = new QHBoxLayout();
        buttonLayout->setObjectName("buttonLayout");
        buttonLayout->setSpacing(t.spacing.sm);
        buttonBox = new QDialogButtonBox(horizontalLayoutWidget);
        buttonBox->setObjectName("buttonBox");
        buttonBox->setStandardButtons(QDialogButtonBox::Apply | QDialogButtonBox::Close | QDialogButtonBox::Reset);
        buttonLayout->addWidget(buttonBox);

        controlsLayout->addLayout(buttonLayout);

        const auto split = rt_preview_ui::buildMainProcessSplitter(horizontalLayoutMain, horizontalLayoutWidget);
        split.controlsLayout->addLayout(controlsLayout);

        previewLayout = new QVBoxLayout();
        previewLayout->setObjectName("previewLayout");
        widget = new QWidget(split.previewPanel);
        widget->setObjectName("widget");
        previewLayout->addWidget(widget);
        split.previewLayout->addLayout(previewLayout);

        retranslateUi(DarkStructureEnhanceProcess);

        QMetaObject::connectSlotsByName(DarkStructureEnhanceProcess);
    }

    void retranslateUi(QWidget* DarkStructureEnhanceProcess)
    {
        DarkStructureEnhanceProcess->setWindowTitle(
            QCoreApplication::translate("DarkStructureEnhanceProcess", "DarkStructureEnhance", nullptr));

        layersLabel->setText(QCoreApplication::translate("DarkStructureEnhanceProcess", "Layers to Remove", nullptr));
        scalingFunctionLabel->setText(
            QCoreApplication::translate("DarkStructureEnhanceProcess", "Scaling Function", nullptr));
        amountLabel->setText(QCoreApplication::translate("DarkStructureEnhanceProcess", "Amount", nullptr));
        iterationsLabel->setText(QCoreApplication::translate("DarkStructureEnhanceProcess", "Iterations", nullptr));
        previewModeLabel->setText(QCoreApplication::translate("DarkStructureEnhanceProcess", "Preview Mode", nullptr));
        targetImageLabel->setText(QCoreApplication::translate("DarkStructureEnhanceProcess", "Target Image", nullptr));
        channelLabel->setText(QCoreApplication::translate("DarkStructureEnhanceProcess", "Channel", nullptr));
        preview->setText(QCoreApplication::translate("DarkStructureEnhanceProcess", "Preview", nullptr));
        createMaskImage->setText(
            QCoreApplication::translate("DarkStructureEnhanceProcess", "Create mask image", nullptr));

        scalingFunctionComboBox->clear();
        scalingFunctionComboBox->addItem(
            QCoreApplication::translate("DarkStructureEnhanceProcess", "3x3 Linear Interpolation", nullptr));
        scalingFunctionComboBox->addItem(
            QCoreApplication::translate("DarkStructureEnhanceProcess", "5x5 B3 Spline", nullptr));
        scalingFunctionComboBox->setCurrentIndex(1);

        previewModeComboBox->clear();
        previewModeComboBox->addItem(
            QCoreApplication::translate("DarkStructureEnhanceProcess", "Processed image", nullptr));
        previewModeComboBox->addItem(
            QCoreApplication::translate("DarkStructureEnhanceProcess", "Generated mask", nullptr));
        previewModeComboBox->setCurrentIndex(0);

        layersValue->setToolTip(QCoreApplication::translate(
            "DarkStructureEnhanceProcess", "Number of starlet layers removed while building the mask.", nullptr));
        layersSlider->setToolTip(layersValue->toolTip());
        scalingFunctionComboBox->setToolTip(QCoreApplication::translate(
            "DarkStructureEnhanceProcess", "Starlet scaling function used by the mask reconstruction steps.", nullptr));
        amountValue->setToolTip(QCoreApplication::translate(
            "DarkStructureEnhanceProcess", "Enhancement amount. PixInsight maps the default 0.40 to midtones 0.70.",
            nullptr));
        amountSlider->setToolTip(amountValue->toolTip());
        iterationsValue->setToolTip(QCoreApplication::translate("DarkStructureEnhanceProcess",
                                                                "Number of masked midtones passes to apply.", nullptr));
        iterationsSlider->setToolTip(iterationsValue->toolTip());
        previewModeComboBox->setToolTip(QCoreApplication::translate(
            "DarkStructureEnhanceProcess", "Choose whether live preview shows the processed image or generated mask.",
            nullptr));
        targetImageSelector->setToolTip(
            QCoreApplication::translate("DarkStructureEnhanceProcess", "Choose the open image to process.", nullptr));
        channelSelector->setToolTip(QCoreApplication::translate(
            "DarkStructureEnhanceProcess", "Select the channel to process, or RGB to process all channels together.",
            nullptr));
        preview->setToolTip(QCoreApplication::translate("DarkStructureEnhanceProcess",
                                                        "Enable or disable live preview updates.", nullptr));
        createMaskImage->setToolTip(QCoreApplication::translate(
            "DarkStructureEnhanceProcess", "Create a separate image view containing the generated mask after Apply.",
            nullptr));

        if (auto* btn = buttonBox->button(QDialogButtonBox::Close))
        {
            btn->setToolTip(QCoreApplication::translate("DarkStructureEnhanceProcess",
                                                        "Close this dialog without applying any changes.", nullptr));
        }
        if (auto* btn = buttonBox->button(QDialogButtonBox::Reset))
        {
            btn->setToolTip(QCoreApplication::translate("DarkStructureEnhanceProcess",
                                                        "Reset all parameters to their default values.", nullptr));
        }
    }
};

inline DarkStructureEnhanceProcessUiElements buildDarkStructureEnhanceProcessUi(QWidget* parent)
{
    DarkStructureEnhanceProcessUiElements ui{};
    if (!parent)
    {
        return ui;
    }

    ui.setupUi(parent);
    return ui;
}

} // namespace an::ui::processes
