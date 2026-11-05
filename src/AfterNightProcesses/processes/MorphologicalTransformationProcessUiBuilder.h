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
#include <QtWidgets/QGroupBox>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QSlider>
#include <QtWidgets/QSpacerItem>
#include <QtWidgets/QSpinBox>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QWidget>

#include "controls/IAppContext.h"
#include "theme/AnDesignTokens.h"

namespace an::ui::processes
{

struct MorphologicalTransformationProcessUiElements
{
    QHBoxLayout* horizontalLayoutMain = nullptr;
    QVBoxLayout* previewLayout = nullptr;
    QGroupBox* groupBox = nullptr;
    QComboBox* operatorSelector = nullptr;
    QSpinBox* iterationsValue = nullptr;
    QDoubleSpinBox* amountValue = nullptr;
    QSlider* amountSlider = nullptr;
    QDoubleSpinBox* selectionValue = nullptr;
    QSlider* selectionSlider = nullptr;

    QGroupBox* groupBox_2 = nullptr;
    QWidget* seHost = nullptr; // Host for custom widget
    QComboBox* seSizeSelector = nullptr;
    QPushButton* seInvertButton = nullptr;
    QPushButton* seCircularButton = nullptr;
    QPushButton* seDiamondButton = nullptr;
    QPushButton* seCrossButton = nullptr;
    QPushButton* seDiagonalButton = nullptr;
    QPushButton* seClearButton = nullptr;
    QPushButton* seFillButton = nullptr;

    QComboBox* targetImageSelector = nullptr;
    QComboBox* channelSelector = nullptr;
    QCheckBox* preview = nullptr;
    QDialogButtonBox* buttonBox = nullptr;
};

inline MorphologicalTransformationProcessUiElements buildMorphologicalTransformationProcessUi(QWidget* parent)
{
    MorphologicalTransformationProcessUiElements ui;

    if (!parent)
    {
        return ui;
    }

    parent->setObjectName(QStringLiteral("MorphologicalTransformationProcess"));
    parent->resize(920, 520);
    parent->setWindowTitle(
        QCoreApplication::translate("MorphologicalTransformationProcess", "Morphological Transformation", nullptr));
    const auto& t = an::ui::tokens();

    ui.horizontalLayoutMain = new QHBoxLayout(parent);
    ui.horizontalLayoutMain->setObjectName(QStringLiteral("horizontalLayoutMain"));
    ui.horizontalLayoutMain->setSpacing(t.spacing.md);
    ui.horizontalLayoutMain->setSizeConstraint(QLayout::SetMinimumSize);
    ui.horizontalLayoutMain->setContentsMargins(t.spacing.md, t.spacing.md, t.spacing.md, t.spacing.md);

    auto* controlsLayout = new QVBoxLayout();
    controlsLayout->setObjectName(QStringLiteral("verticalLayout"));
    controlsLayout->setSizeConstraint(QLayout::SetFixedSize);
    controlsLayout->setContentsMargins(0, 0, 0, 0);
    controlsLayout->setSpacing(t.spacing.md);

    // Group 1: Morphological Filter
    ui.groupBox = new QGroupBox(
        QCoreApplication::translate("MorphologicalTransformationProcess", "Morphological Filter", nullptr), parent);
    ui.groupBox->setObjectName(QStringLiteral("groupBox"));
    auto* group1Layout = new QGridLayout(ui.groupBox);
    group1Layout->setVerticalSpacing(t.spacing.sm);
    group1Layout->setHorizontalSpacing(t.spacing.md);
    group1Layout->setContentsMargins(t.spacing.md, t.spacing.md, t.spacing.md, t.spacing.md);

    auto* operatorLabel =
        new QLabel(QCoreApplication::translate("MorphologicalTransformationProcess", "Operator", nullptr), ui.groupBox);
    group1Layout->addWidget(operatorLabel, 0, 0);

    ui.operatorSelector = new QComboBox(ui.groupBox);
    ui.operatorSelector->setObjectName(QStringLiteral("operatorSelector"));
    group1Layout->addWidget(ui.operatorSelector, 0, 1, 1, 2);

    auto* iterationsLabel = new QLabel(
        QCoreApplication::translate("MorphologicalTransformationProcess", "Iterations", nullptr), ui.groupBox);
    group1Layout->addWidget(iterationsLabel, 1, 0);

    ui.iterationsValue = new QSpinBox(ui.groupBox);
    ui.iterationsValue->setObjectName(QStringLiteral("iterationsValue"));
    ui.iterationsValue->setMinimum(1);
    ui.iterationsValue->setMaximum(20);
    ui.iterationsValue->setMinimumWidth(80);
    group1Layout->addWidget(ui.iterationsValue, 1, 1);

    auto* amountLabel =
        new QLabel(QCoreApplication::translate("MorphologicalTransformationProcess", "Amount", nullptr), ui.groupBox);
    group1Layout->addWidget(amountLabel, 2, 0);

    ui.amountValue = new QDoubleSpinBox(ui.groupBox);
    ui.amountValue->setObjectName(QStringLiteral("amountValue"));
    ui.amountValue->setMinimum(0.0);
    ui.amountValue->setMaximum(1.0);
    ui.amountValue->setSingleStep(0.05);
    ui.amountValue->setStepType(QAbstractSpinBox::AdaptiveDecimalStepType);
    ui.amountValue->setValue(1.0);
    ui.amountValue->setMinimumWidth(80);
    group1Layout->addWidget(ui.amountValue, 2, 1);

    ui.amountSlider = new QSlider(Qt::Horizontal, ui.groupBox);
    ui.amountSlider->setObjectName(QStringLiteral("amountSlider"));
    ui.amountSlider->setMinimum(0);
    ui.amountSlider->setMaximum(1000);
    ui.amountSlider->setValue(1000);
    ui.amountSlider->setMinimumWidth(150);
    group1Layout->addWidget(ui.amountSlider, 2, 2);

    auto* selectionLabel = new QLabel(
        QCoreApplication::translate("MorphologicalTransformationProcess", "Selection", nullptr), ui.groupBox);
    group1Layout->addWidget(selectionLabel, 3, 0);

    ui.selectionValue = new QDoubleSpinBox(ui.groupBox);
    ui.selectionValue->setObjectName(QStringLiteral("selectionValue"));
    ui.selectionValue->setMinimum(0.0);
    ui.selectionValue->setMaximum(1.0);
    ui.selectionValue->setSingleStep(0.05);
    ui.selectionValue->setStepType(QAbstractSpinBox::AdaptiveDecimalStepType);
    ui.selectionValue->setValue(0.5);
    ui.selectionValue->setMinimumWidth(80);
    group1Layout->addWidget(ui.selectionValue, 3, 1);

    ui.selectionSlider = new QSlider(Qt::Horizontal, ui.groupBox);
    ui.selectionSlider->setObjectName(QStringLiteral("selectionSlider"));
    ui.selectionSlider->setMinimum(0);
    ui.selectionSlider->setMaximum(1000);
    ui.selectionSlider->setValue(500);
    ui.selectionSlider->setMinimumWidth(150);
    group1Layout->addWidget(ui.selectionSlider, 3, 2);

    controlsLayout->addWidget(ui.groupBox);

    // Group 2: Structuring Element
    ui.groupBox_2 = new QGroupBox(
        QCoreApplication::translate("MorphologicalTransformationProcess", "Structuring Element", nullptr), parent);
    ui.groupBox_2->setObjectName(QStringLiteral("groupBox_2"));

    auto* group2MainLayout = new QHBoxLayout(ui.groupBox_2);
    group2MainLayout->setSpacing(t.spacing.md);
    group2MainLayout->setContentsMargins(t.spacing.md, t.spacing.md, t.spacing.md, t.spacing.md);

    // Left: Custom Widget Host
    ui.seHost = new QWidget(ui.groupBox_2);
    ui.seHost->setObjectName(QStringLiteral("seHost"));
    ui.seHost->setFixedSize(170, 170);
    group2MainLayout->addWidget(ui.seHost);

    // Right: Controls
    auto* group2RightLayout = new QVBoxLayout();
    group2RightLayout->setSpacing(t.spacing.sm);

    // Size Selector
    auto* sizeLayout = new QHBoxLayout();
    auto* sizeLabel =
        new QLabel(QCoreApplication::translate("MorphologicalTransformationProcess", "Size", nullptr), ui.groupBox_2);
    sizeLayout->addWidget(sizeLabel);

    ui.seSizeSelector = new QComboBox(ui.groupBox_2);
    ui.seSizeSelector->setObjectName(QStringLiteral("seSizeSelector"));
    sizeLayout->addWidget(ui.seSizeSelector);
    group2RightLayout->addLayout(sizeLayout);

    // Buttons Grid
    auto* buttonsGrid = new QGridLayout();
    buttonsGrid->setSpacing(t.spacing.xs);

    auto createButton = [&](QPushButton*& btn, const QString& objName, const QString& iconName,
                            const QString& tooltip) {
        btn = new QPushButton(ui.groupBox_2);
        btn->setObjectName(objName);
        if (auto* context = an::ui::appContext())
        {
            if (auto iconProvider = context->getIconProvider())
            {
                btn->setIcon(iconProvider->getIcon(iconName));
            }
        }
        btn->setToolTip(tooltip);
        btn->setFixedSize(32, 24);
    };

    createButton(ui.seClearButton, "seClearButton", "delete",
                 QCoreApplication::translate("MorphologicalTransformationProcess", "Clear grid", nullptr));
    buttonsGrid->addWidget(ui.seClearButton, 0, 0);

    createButton(ui.seFillButton, "seFillButton", "color_fill",
                 QCoreApplication::translate("MorphologicalTransformationProcess", "Fill grid", nullptr));
    buttonsGrid->addWidget(ui.seFillButton, 0, 1);

    createButton(ui.seInvertButton, "seInvertButton", "contrast",
                 QCoreApplication::translate("MorphologicalTransformationProcess", "Invert", nullptr));
    buttonsGrid->addWidget(ui.seInvertButton, 0, 2);

    createButton(ui.seCircularButton, "seCircularButton", "circle",
                 QCoreApplication::translate("MorphologicalTransformationProcess", "Circular structure", nullptr));
    buttonsGrid->addWidget(ui.seCircularButton, 1, 0);

    createButton(ui.seDiamondButton, "seDiamondButton", "diamond",
                 QCoreApplication::translate("MorphologicalTransformationProcess", "Diamond structure", nullptr));
    buttonsGrid->addWidget(ui.seDiamondButton, 1, 1);

    createButton(ui.seCrossButton, "seCrossButton", "add",
                 QCoreApplication::translate("MorphologicalTransformationProcess", "Cross structure", nullptr));
    buttonsGrid->addWidget(ui.seCrossButton, 1, 2);

    createButton(ui.seDiagonalButton, "seDiagonalButton", "close",
                 QCoreApplication::translate("MorphologicalTransformationProcess", "Diagonal structure", nullptr));
    buttonsGrid->addWidget(ui.seDiagonalButton, 1, 3);

    group2RightLayout->addLayout(buttonsGrid);
    group2RightLayout->addStretch();
    group2MainLayout->addLayout(group2RightLayout);

    controlsLayout->addWidget(ui.groupBox_2);
    controlsLayout->addStretch();

    // Target/Channel Selectors
    auto* selectorLayout = new QFormLayout();
    selectorLayout->setObjectName(QStringLiteral("formLayout"));
    selectorLayout->setVerticalSpacing(t.spacing.sm);
    selectorLayout->setHorizontalSpacing(t.spacing.md);

    auto* targetLabel =
        new QLabel(QCoreApplication::translate("MorphologicalTransformationProcess", "Target Image", nullptr), parent);
    selectorLayout->setWidget(0, QFormLayout::LabelRole, targetLabel);

    ui.targetImageSelector = new QComboBox(parent);
    ui.targetImageSelector->setObjectName(QStringLiteral("targetImageSelector"));
    ui.targetImageSelector->setMinimumWidth(150);
    ui.targetImageSelector->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    selectorLayout->setWidget(0, QFormLayout::FieldRole, ui.targetImageSelector);

    auto* channelLabel =
        new QLabel(QCoreApplication::translate("MorphologicalTransformationProcess", "Channel", nullptr), parent);
    selectorLayout->setWidget(1, QFormLayout::LabelRole, channelLabel);

    ui.channelSelector = new QComboBox(parent);
    ui.channelSelector->setObjectName(QStringLiteral("channelSelector"));
    ui.channelSelector->setMinimumWidth(150);
    ui.channelSelector->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    selectorLayout->setWidget(1, QFormLayout::FieldRole, ui.channelSelector);

    controlsLayout->addLayout(selectorLayout);

    // Preview
    auto* previewLayout = new QHBoxLayout();
    previewLayout->setSpacing(t.spacing.md);

    ui.preview =
        new QCheckBox(QCoreApplication::translate("MorphologicalTransformationProcess", "Preview", nullptr), parent);
    ui.preview->setObjectName(QStringLiteral("preview"));
    ui.preview->setChecked(true);
    previewLayout->addWidget(ui.preview);
    previewLayout->addStretch();

    controlsLayout->addLayout(previewLayout);

    // Button Box
    auto* buttonLayout = new QHBoxLayout();
    buttonLayout->setSizeConstraint(QLayout::SetDefaultConstraint);
    buttonLayout->setSpacing(t.spacing.md);

    ui.buttonBox =
        new QDialogButtonBox(QDialogButtonBox::Apply | QDialogButtonBox::Close | QDialogButtonBox::Reset, parent);
    ui.buttonBox->setObjectName(QStringLiteral("buttonBox"));
    buttonLayout->addWidget(ui.buttonBox);

    controlsLayout->addLayout(buttonLayout);

    ui.horizontalLayoutMain->addLayout(controlsLayout);

    // Preview Host
    ui.previewLayout = new QVBoxLayout();
    ui.previewLayout->setObjectName(QStringLiteral("verticalLayout_2"));
    auto* previewHost = new QWidget(parent);
    previewHost->setObjectName(QStringLiteral("widget"));
    ui.previewLayout->addWidget(previewHost);

    ui.horizontalLayoutMain->addLayout(ui.previewLayout);
    ui.horizontalLayoutMain->setStretch(0, 0);
    ui.horizontalLayoutMain->setStretch(1, 1);

    // Tooltips
    ui.operatorSelector->setToolTip(QCoreApplication::translate("MorphologicalTransformationProcess",
                                                                "Select the morphological operator.", nullptr));
    ui.iterationsValue->setToolTip(
        QCoreApplication::translate("MorphologicalTransformationProcess", "Number of iterations to apply.", nullptr));
    ui.amountValue->setToolTip(QCoreApplication::translate(
        "MorphologicalTransformationProcess", "Strength of the transformation (blending with original).", nullptr));
    ui.amountSlider->setToolTip(QCoreApplication::translate(
        "MorphologicalTransformationProcess", "Strength of the transformation (blending with original).", nullptr));
    ui.selectionValue->setToolTip(QCoreApplication::translate(
        "MorphologicalTransformationProcess",
        "Morphological Selection's rank among the samples the element covers: 0 is the minimum, 0.5 the median and 1 "
        "the maximum.",
        nullptr));
    ui.selectionSlider->setToolTip(ui.selectionValue->toolTip());
    ui.seSizeSelector->setToolTip(
        QCoreApplication::translate("MorphologicalTransformationProcess", "Size of the structuring element.", nullptr));
    ui.targetImageSelector->setToolTip(QCoreApplication::translate("MorphologicalTransformationProcess",
                                                                   "Select the target image to process.", nullptr));
    ui.channelSelector->setToolTip(QCoreApplication::translate("MorphologicalTransformationProcess",
                                                               "Select the specific channel to transform.", nullptr));
    ui.preview->setToolTip(QCoreApplication::translate("MorphologicalTransformationProcess",
                                                       "Enable or disable the live preview.", nullptr));

    if (auto* btn = ui.buttonBox->button(QDialogButtonBox::Close))
        btn->setToolTip(QCoreApplication::translate("MorphologicalTransformationProcess",
                                                    "Close this dialog without applying any changes.", nullptr));
    if (auto* btn = ui.buttonBox->button(QDialogButtonBox::Reset))
        btn->setToolTip(QCoreApplication::translate("MorphologicalTransformationProcess",
                                                    "Reset all parameters to their default values.", nullptr));

    return ui;
}

} // namespace an::ui::processes
