// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include <QtCore/QVariant>
#include <QtWidgets/QAbstractButton>
#include <QtWidgets/QApplication>
#include <QtWidgets/QCheckBox>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QDialogButtonBox>
#include <QtWidgets/QGridLayout>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QSizePolicy>
#include <QtWidgets/QSpacerItem>
#include <QtWidgets/QSpinBox>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QWidget>

#include "processes/FixedDialogUiSizing.h"

#include "theme/AnDesignTokens.h"

namespace an::ui::processes
{

class StarnetProcessUiElements
{
public:
    QDialogButtonBox* buttonBox;
    QCheckBox* upsamplex2;
    QCheckBox* preStretchLinearImage;
    QCheckBox* generateStarMask;
    QLabel* starExtractionModeLabel;
    QComboBox* starExtractionMode;
    QLabel* starOutputModeLabel;
    QComboBox* starOutputMode;
    QLabel* stridePresetLabel;
    QComboBox* stridePreset;
    QSpinBox* customStrideValue;
    QCheckBox* highlightProtection;
    QLabel* weightsPathLabel;
    QLineEdit* weightsPath;
    QPushButton* browseWeights;
    QPushButton* configureStarnet;
    QHBoxLayout* configureLayout;
    QSpacerItem* configureSpacer;
    QLabel* label_3;
    QComboBox* targetImageSelector;
    QLabel* label;

    void setupUi(QWidget* StarnetProcess)
    {
        const auto& t = an::ui::tokens();

        if (StarnetProcess->objectName().isEmpty())
            StarnetProcess->setObjectName("StarnetProcess");
        StarnetProcess->resize(520, 460);
        StarnetProcess->setMinimumSize(QSize(520, 460));

        auto* rootLayout = new QVBoxLayout(StarnetProcess);
        rootLayout->setObjectName("verticalLayout");
        rootLayout->setContentsMargins(t.spacing.md, t.spacing.md, t.spacing.md, t.spacing.md);
        rootLayout->setSpacing(t.spacing.md);
        rootLayout->setSizeConstraint(QLayout::SetFixedSize);

        auto* topOptionsLayout = new QVBoxLayout();
        topOptionsLayout->setSpacing(t.spacing.sm);

        upsamplex2 = new QCheckBox(StarnetProcess);
        upsamplex2->setObjectName("upsamplex2");
        upsamplex2->setEnabled(true);
        topOptionsLayout->addWidget(upsamplex2);

        preStretchLinearImage = new QCheckBox(StarnetProcess);
        preStretchLinearImage->setObjectName("preStretchLinearImage");
        preStretchLinearImage->setEnabled(true);
        topOptionsLayout->addWidget(preStretchLinearImage);

        highlightProtection = new QCheckBox(StarnetProcess);
        highlightProtection->setObjectName("highlightProtection");
        highlightProtection->setChecked(true);
        topOptionsLayout->addWidget(highlightProtection);

        generateStarMask = new QCheckBox(StarnetProcess);
        generateStarMask->setObjectName("generateStarMask");
        generateStarMask->setChecked(true);
        topOptionsLayout->addWidget(generateStarMask);

        auto* parameterGrid = new QGridLayout();
        parameterGrid->setObjectName("parameterGrid");
        parameterGrid->setHorizontalSpacing(t.spacing.sm);
        parameterGrid->setVerticalSpacing(t.spacing.sm);
        parameterGrid->setColumnStretch(1, 1);
        parameterGrid->setRowMinimumHeight(0, fixed_dialog_ui::kFieldControlMinHeight);
        parameterGrid->setRowMinimumHeight(1, fixed_dialog_ui::kFieldControlMinHeight);
        parameterGrid->setRowMinimumHeight(2, fixed_dialog_ui::kFieldControlMinHeight);

        starOutputModeLabel = new QLabel(StarnetProcess);
        starOutputModeLabel->setObjectName("starOutputModeLabel");
        parameterGrid->addWidget(starOutputModeLabel, 0, 0, Qt::AlignVCenter);

        starOutputMode = new QComboBox(StarnetProcess);
        starOutputMode->setObjectName("starOutputMode");
        starOutputMode->setMinimumWidth(150);
        starOutputMode->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        parameterGrid->addWidget(starOutputMode, 0, 1, 1, 2);
        starExtractionModeLabel = starOutputModeLabel;
        starExtractionMode = starOutputMode;

        stridePresetLabel = new QLabel(StarnetProcess);
        stridePresetLabel->setObjectName("stridePresetLabel");
        parameterGrid->addWidget(stridePresetLabel, 1, 0, Qt::AlignVCenter);

        stridePreset = new QComboBox(StarnetProcess);
        stridePreset->setObjectName("stridePreset");
        stridePreset->setMinimumWidth(150);
        stridePreset->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        parameterGrid->addWidget(stridePreset, 1, 1);

        customStrideValue = new QSpinBox(StarnetProcess);
        customStrideValue->setObjectName("customStrideValue");
        customStrideValue->setEnabled(false);
        customStrideValue->setReadOnly(false);
        customStrideValue->setMinimum(2);
        customStrideValue->setMaximum(512);
        customStrideValue->setSingleStep(2);
        customStrideValue->setValue(256);
        customStrideValue->setMinimumWidth(80);
        parameterGrid->addWidget(customStrideValue, 1, 2);

        weightsPathLabel = new QLabel(StarnetProcess);
        weightsPathLabel->setObjectName("weightsPathLabel");
        parameterGrid->addWidget(weightsPathLabel, 2, 0, Qt::AlignVCenter);

        weightsPath = new QLineEdit(StarnetProcess);
        weightsPath->setObjectName("weightsPath");
        weightsPath->setClearButtonEnabled(true);
        weightsPath->setMinimumWidth(160);
        weightsPath->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        parameterGrid->addWidget(weightsPath, 2, 1);

        browseWeights = new QPushButton(StarnetProcess);
        browseWeights->setObjectName("browseWeights");
        browseWeights->setMinimumHeight(32);
        parameterGrid->addWidget(browseWeights, 2, 2);
        topOptionsLayout->addLayout(parameterGrid);

        rootLayout->addLayout(topOptionsLayout);

        auto* targetLayout = new QHBoxLayout();
        targetLayout->setSpacing(t.spacing.sm);

        label_3 = new QLabel(StarnetProcess);
        label_3->setObjectName("label_3");
        targetLayout->addWidget(label_3);

        targetImageSelector = new QComboBox(StarnetProcess);
        targetImageSelector->setObjectName("targetImageSelector");
        targetImageSelector->setMinimumWidth(150);
        targetImageSelector->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        targetLayout->addWidget(targetImageSelector);
        targetLayout->setStretch(1, 1);

        rootLayout->addLayout(targetLayout);

        label = new QLabel(StarnetProcess);
        label->setObjectName("label");
        label->setFont(t.typography.heading3);
        label->setAlignment(Qt::AlignLeading | Qt::AlignLeft | Qt::AlignTop);
        label->setWordWrap(true);
        rootLayout->addWidget(label);

        rootLayout->addStretch(1);

        configureStarnet = new QPushButton(StarnetProcess);
        configureStarnet->setObjectName("configureStarnet");
        configureStarnet->setMinimumHeight(32);
        configureLayout = new QHBoxLayout();
        configureLayout->setObjectName("configureLayout");
        configureLayout->setContentsMargins(0, 0, 0, 0);
        configureLayout->addWidget(configureStarnet);
        configureSpacer = new QSpacerItem(40, 20, QSizePolicy::Policy::Expanding, QSizePolicy::Policy::Minimum);
        configureLayout->addItem(configureSpacer);
        rootLayout->addLayout(configureLayout);

        buttonBox = new QDialogButtonBox(StarnetProcess);
        buttonBox->setObjectName("buttonBox");
        buttonBox->setStandardButtons(QDialogButtonBox::Close | QDialogButtonBox::Ok);
        rootLayout->addWidget(buttonBox);

        fixed_dialog_ui::applyStandardControlHeights(StarnetProcess);

        retranslateUi(StarnetProcess);

        QMetaObject::connectSlotsByName(StarnetProcess);
    } // setupUi

    void retranslateUi(QWidget* StarnetProcess)
    {
        StarnetProcess->setWindowTitle(QCoreApplication::translate("StarnetProcess", "Starnet++", nullptr));
        upsamplex2->setText(QCoreApplication::translate("StarnetProcess", "Upsample x2", nullptr));
        preStretchLinearImage->setText(
            QCoreApplication::translate("StarnetProcess", "Pre-Stretch linear image", nullptr));
        highlightProtection->setText(QCoreApplication::translate("StarnetProcess", "Highlight protection", nullptr));
        generateStarMask->setText(QCoreApplication::translate("StarnetProcess", "Generate Stars Output", nullptr));
        starOutputModeLabel->setText(QCoreApplication::translate("StarnetProcess", "Output", nullptr));
        stridePresetLabel->setText(QCoreApplication::translate("StarnetProcess", "Stride", nullptr));
        weightsPathLabel->setText(QCoreApplication::translate("StarnetProcess", "Weights", nullptr));
        weightsPath->setPlaceholderText(
            QCoreApplication::translate("StarnetProcess", "Default StarNet2_weights.onnx", nullptr));
        browseWeights->setText(QCoreApplication::translate("StarnetProcess", "Browse", nullptr));
        configureStarnet->setText(QCoreApplication::translate("StarnetProcess", "Configure", nullptr));
        label_3->setText(QCoreApplication::translate("StarnetProcess", "Target Image ", nullptr));
        label->setText(QCoreApplication::translate(
            "StarnetProcess",
            "StarNet2 CLI version 2.5.3 or newer is required. Configure the starnet2 executable before running.",
            nullptr));

        // Tooltips
#if QT_CONFIG(tooltip)
        upsamplex2->setToolTip(QCoreApplication::translate(
            "StarnetProcess", "Ask StarNet2 to use its native 2x processing mode for better removal on small stars.",
            nullptr));
        preStretchLinearImage->setToolTip(QCoreApplication::translate(
            "StarnetProcess",
            "Apply an automatic stretch before processing linear images. Starless and unscreen outputs are converted "
            "back after processing.",
            nullptr));
        highlightProtection->setToolTip(
            QCoreApplication::translate("StarnetProcess",
                                        "Keep StarNet2's default highlight-protection pass enabled for saturated and "
                                        "near-saturated bright regions.",
                                        nullptr));
        generateStarMask->setToolTip(QCoreApplication::translate(
            "StarnetProcess", "Create one or more separate star outputs using StarNet2's native CLI outputs.",
            nullptr));
        starOutputMode->setToolTip(QCoreApplication::translate(
            "StarnetProcess",
            "Choose the native StarNet2 star output: subtractive mask, unscreen star layer, or both outputs.",
            nullptr));
        stridePreset->setToolTip(QCoreApplication::translate(
            "StarnetProcess",
            "Choose the StarNet2 tile stride. Standard 256 is the default; 384 is useful for wide-field images with "
            "small stars.",
            nullptr));
        customStrideValue->setToolTip(QCoreApplication::translate(
            "StarnetProcess", "Custom even stride value from 2 to 512 pixels for StarNet2 tile processing.", nullptr));
        weightsPath->setToolTip(QCoreApplication::translate(
            "StarnetProcess",
            "Optional path to a StarNet2 ONNX model checkpoint. Leave empty to use the model bundled with the CLI.",
            nullptr));
        browseWeights->setToolTip(
            QCoreApplication::translate("StarnetProcess", "Select a custom StarNet2 ONNX weights file.", nullptr));
        targetImageSelector->setToolTip(
            QCoreApplication::translate("StarnetProcess", "Image to process with StarNet2.", nullptr));
        configureStarnet->setToolTip(QCoreApplication::translate(
            "StarnetProcess", "Open settings to configure the StarNet2 2.5.3+ executable path.", nullptr));
        if (auto* btn = buttonBox->button(QDialogButtonBox::Close))
            btn->setToolTip(QCoreApplication::translate("StarnetProcess",
                                                        "Close this dialog without applying any changes.", nullptr));
#endif // QT_CONFIG(tooltip)
    } // retranslateUi
};

inline StarnetProcessUiElements buildStarnetProcessUi(QWidget* parent)
{
    StarnetProcessUiElements ui;
    ui.setupUi(parent);
    return ui;
}

inline void selectStarnetUnscreenExtractionMode(StarnetProcessUiElements& ui)
{
    if (!ui.starOutputMode)
    {
        return;
    }

    int unscreen_index = ui.starOutputMode->findData(QStringLiteral("unscreen"));
    if (unscreen_index < 0)
    {
        unscreen_index = ui.starOutputMode->findText(QStringLiteral("Unscreen"));
    }
    if (unscreen_index < 0)
    {
        unscreen_index = ui.starOutputMode->findText(QStringLiteral("Unscreen Star Layer"));
    }
    if (unscreen_index >= 0)
    {
        ui.starOutputMode->setCurrentIndex(unscreen_index);
    }
}

} // namespace an::ui::processes
