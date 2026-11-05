// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include <QtCore/QtCore>
#include <QtGui/QtGui>
#include <QtWidgets/QtWidgets>
#include "components/AnCardWidget.h"
#include "controls/IAppContext.h"
#include "processes/ImageIntegration/core/ParallelExecutionUtils.h"
#include "theme/AnDesignTokens.h"

namespace an::ui::imageintegration
{

class StackingTabUiElements
{
public:
    QVBoxLayout* verticalLayout = nullptr;
    an::ui::AnCardWidget* grpIntegrationGroups = nullptr;
    QVBoxLayout* verticalLayout_2 = nullptr;
    QTableWidget* tblIntegrationGroups = nullptr;
    QHBoxLayout* horizontalLayoutStackingGroups = nullptr;
    QCheckBox* chkGroupBySession = nullptr;
    QCheckBox* chkGroupByCustomTag = nullptr;
    QCheckBox* chkGroupByExposure = nullptr;
    QDoubleSpinBox* spinExposureTolerance = nullptr;
    QHBoxLayout* horizontalLayoutGroupActions = nullptr;
    QPushButton* btnStackingSettings = nullptr;
    QPushButton* btnRunIntegrationGroup = nullptr;
    an::ui::AnCardWidget* grpExecutionOptions = nullptr;
    QVBoxLayout* verticalLayout_3 = nullptr;
    QHBoxLayout* horizontalLayoutOptions = nullptr;
    QCheckBox* chkGenerate64Bit = nullptr;
    QCheckBox* chkReprocessExisting = nullptr;
    QCheckBox* chkOpenIntegratedFiles = nullptr;
    QCheckBox* chkCombineRgbImages = nullptr;
    QCheckBox* chkSaveCalibratedFrames = nullptr;
    QCheckBox* chkLimitMemory = nullptr;
    QSpinBox* spinMaxMemory = nullptr;
    QLabel* lblMaxParallelCores = nullptr;
    QSpinBox* spinMaxParallelCores = nullptr;
    QHBoxLayout* stackingActionsLayout = nullptr;
    QPushButton* btnRunIntegration = nullptr;

    void setupUi(QWidget* StackingTab)
    {
        const auto& t = an::ui::tokens();
        const auto iconProvider =
            an::ui::appContext() ? an::ui::appContext()->getIconProvider() : an::ui::IIconProviderPtr{};

        StackingTab->setObjectName("StackingTab");
        StackingTab->resize(900, 650);
        verticalLayout = new QVBoxLayout(StackingTab);
        verticalLayout->setContentsMargins(t.spacing.md, t.spacing.md, t.spacing.md, t.spacing.md);
        verticalLayout->setObjectName("verticalLayout");
        grpIntegrationGroups = new an::ui::AnCardWidget(QString(), StackingTab);
        grpIntegrationGroups->setObjectName("grpIntegrationGroups");
        verticalLayout_2 = new QVBoxLayout();
        verticalLayout_2->setContentsMargins(t.spacing.sm, t.spacing.md, t.spacing.sm, t.spacing.sm);
        verticalLayout_2->setObjectName("verticalLayout_2");
        grpIntegrationGroups->setContentLayout(verticalLayout_2);
        verticalLayout_2->setContentsMargins(t.spacing.sm, t.spacing.md, t.spacing.sm, t.spacing.sm);

        // Use contentWidget as parent for child widgets
        QWidget* grpIntegrationGroupsContent = grpIntegrationGroups->contentWidget();

        tblIntegrationGroups = new QTableWidget(grpIntegrationGroupsContent);
        tblIntegrationGroups->setAlternatingRowColors(true);
        tblIntegrationGroups->setSelectionMode(QAbstractItemView::SelectionMode::SingleSelection);
        tblIntegrationGroups->setSelectionBehavior(QAbstractItemView::SelectionBehavior::SelectRows);
        tblIntegrationGroups->setObjectName("tblIntegrationGroups");
        tblIntegrationGroups->setColumnCount(0);
        tblIntegrationGroups->setRowCount(0);
        verticalLayout_2->addWidget(tblIntegrationGroups);
        horizontalLayoutStackingGroups = new QHBoxLayout();
        horizontalLayoutStackingGroups->setObjectName("horizontalLayoutStackingGroups");
        chkGroupBySession = new QCheckBox(grpIntegrationGroupsContent);
        chkGroupBySession->setObjectName("chkGroupBySession");
        horizontalLayoutStackingGroups->addWidget(chkGroupBySession);
        chkGroupByCustomTag = new QCheckBox(grpIntegrationGroupsContent);
        chkGroupByCustomTag->setObjectName("chkGroupByCustomTag");
        horizontalLayoutStackingGroups->addWidget(chkGroupByCustomTag);
        chkGroupByExposure = new QCheckBox(grpIntegrationGroupsContent);
        chkGroupByExposure->setObjectName("chkGroupByExposure");
        horizontalLayoutStackingGroups->addWidget(chkGroupByExposure);
        spinExposureTolerance = new QDoubleSpinBox(grpIntegrationGroupsContent);
        spinExposureTolerance->setEnabled(false);
        spinExposureTolerance->setMinimum(0.0);
        spinExposureTolerance->setMaximum(1000.0);
        spinExposureTolerance->setSingleStep(0.1);
        spinExposureTolerance->setProperty("value", 3.0);
        spinExposureTolerance->setObjectName("spinExposureTolerance");
        horizontalLayoutStackingGroups->addWidget(spinExposureTolerance);
        auto* spacerItem = new QSpacerItem(40, 20, QSizePolicy::Policy::Expanding, QSizePolicy::Policy::Minimum);
        horizontalLayoutStackingGroups->addItem(spacerItem);
        verticalLayout_2->addLayout(horizontalLayoutStackingGroups);
        horizontalLayoutGroupActions = new QHBoxLayout();
        horizontalLayoutGroupActions->setObjectName("horizontalLayoutGroupActions");
        btnStackingSettings = new QPushButton(grpIntegrationGroupsContent);
        btnStackingSettings->setObjectName("btnStackingSettings");
        horizontalLayoutGroupActions->addWidget(btnStackingSettings);
        auto* spacerItem1 = new QSpacerItem(40, 20, QSizePolicy::Policy::Expanding, QSizePolicy::Policy::Minimum);
        horizontalLayoutGroupActions->addItem(spacerItem1);
        auto* spacerItem2 = new QSpacerItem(40, 20, QSizePolicy::Policy::Expanding, QSizePolicy::Policy::Minimum);
        horizontalLayoutGroupActions->addItem(spacerItem2);
        btnRunIntegrationGroup = new QPushButton(grpIntegrationGroupsContent);
        btnRunIntegrationGroup->setEnabled(false);
        QIcon icon;
        if (iconProvider)
        {
            icon = iconProvider->getIcon(QStringLiteral("play"));
        }
        btnRunIntegrationGroup->setIcon(icon);
        btnRunIntegrationGroup->setObjectName("btnRunIntegrationGroup");
        horizontalLayoutGroupActions->addWidget(btnRunIntegrationGroup);
        verticalLayout_2->addLayout(horizontalLayoutGroupActions);
        verticalLayout->addWidget(grpIntegrationGroups);
        grpExecutionOptions = new an::ui::AnCardWidget(QString(), StackingTab);
        grpExecutionOptions->setObjectName("grpExecutionOptions");
        verticalLayout_3 = new QVBoxLayout();
        verticalLayout_3->setContentsMargins(t.spacing.sm, t.spacing.md, t.spacing.sm, t.spacing.sm);
        verticalLayout_3->setObjectName("verticalLayout_3");
        grpExecutionOptions->setContentLayout(verticalLayout_3);
        verticalLayout_3->setContentsMargins(t.spacing.sm, t.spacing.md, t.spacing.sm, t.spacing.sm);

        QWidget* grpExecutionOptionsContent = grpExecutionOptions->contentWidget();

        horizontalLayoutOptions = new QHBoxLayout();
        horizontalLayoutOptions->setObjectName("horizontalLayoutOptions");
        chkGenerate64Bit = new QCheckBox(grpExecutionOptionsContent);
        chkGenerate64Bit->setObjectName("chkGenerate64Bit");
        horizontalLayoutOptions->addWidget(chkGenerate64Bit);
        chkReprocessExisting = new QCheckBox(grpExecutionOptionsContent);
        chkReprocessExisting->setObjectName("chkReprocessExisting");
        horizontalLayoutOptions->addWidget(chkReprocessExisting);
        chkOpenIntegratedFiles = new QCheckBox(grpExecutionOptionsContent);
        chkOpenIntegratedFiles->setChecked(true);
        chkOpenIntegratedFiles->setObjectName("chkOpenIntegratedFiles");
        horizontalLayoutOptions->addWidget(chkOpenIntegratedFiles);
        chkCombineRgbImages = new QCheckBox(grpExecutionOptionsContent);
        chkCombineRgbImages->setEnabled(false);
        chkCombineRgbImages->setObjectName("chkCombineRgbImages");
        horizontalLayoutOptions->addWidget(chkCombineRgbImages);
        chkSaveCalibratedFrames = new QCheckBox(grpExecutionOptionsContent);
        chkSaveCalibratedFrames->setObjectName("chkSaveCalibratedFrames");
        horizontalLayoutOptions->addWidget(chkSaveCalibratedFrames);
        chkLimitMemory = new QCheckBox(grpExecutionOptionsContent);
        chkLimitMemory->setObjectName("chkLimitMemory");
        horizontalLayoutOptions->addWidget(chkLimitMemory);
        spinMaxMemory = new QSpinBox(grpExecutionOptionsContent);
        spinMaxMemory->setEnabled(false);
        spinMaxMemory->setMinimum(512);
        spinMaxMemory->setMaximum(1048576);
        spinMaxMemory->setProperty("value", 2048);
        spinMaxMemory->setObjectName("spinMaxMemory");
        horizontalLayoutOptions->addWidget(spinMaxMemory);
        lblMaxParallelCores = new QLabel(grpExecutionOptionsContent);
        lblMaxParallelCores->setObjectName("lblMaxParallelCores");
        horizontalLayoutOptions->addWidget(lblMaxParallelCores);
        spinMaxParallelCores = new QSpinBox(grpExecutionOptionsContent);
        const int detectedParallelCores = ImageIntegration::core::detectedParallelCoreCount();
        spinMaxParallelCores->setMinimum(1);
        spinMaxParallelCores->setMaximum(detectedParallelCores);
        spinMaxParallelCores->setProperty("value", detectedParallelCores);
        spinMaxParallelCores->setObjectName("spinMaxParallelCores");
        spinMaxParallelCores->setMinimumWidth(spinMaxParallelCores->sizeHint().width());
        horizontalLayoutOptions->addWidget(spinMaxParallelCores);
        auto* spacerItem3 = new QSpacerItem(40, 20, QSizePolicy::Policy::Expanding, QSizePolicy::Policy::Minimum);
        horizontalLayoutOptions->addItem(spacerItem3);
        verticalLayout_3->addLayout(horizontalLayoutOptions);
        verticalLayout->addWidget(grpExecutionOptions);
        stackingActionsLayout = new QHBoxLayout();
        stackingActionsLayout->setObjectName("stackingActionsLayout");
        auto* spacerItem4 = new QSpacerItem(40, 20, QSizePolicy::Policy::Expanding, QSizePolicy::Policy::Minimum);
        stackingActionsLayout->addItem(spacerItem4);
        btnRunIntegration = new QPushButton(StackingTab);
        btnRunIntegration->setEnabled(false);
        if (iconProvider)
        {
            icon = iconProvider->getIcon(QStringLiteral("play"));
        }
        btnRunIntegration->setIcon(icon);
        btnRunIntegration->setObjectName("btnRunIntegration");
        stackingActionsLayout->addWidget(btnRunIntegration);
        verticalLayout->addLayout(stackingActionsLayout);
        retranslateUi(StackingTab);
        QMetaObject::connectSlotsByName(StackingTab);
    }

    void retranslateUi(QWidget* StackingTab)
    {
        grpIntegrationGroups->setTitle(QCoreApplication::translate("StackingTab", "Integration Groups"));
        chkGroupBySession->setToolTip(
            QCoreApplication::translate("StackingTab", "Affects final stacks only (not calibration matching)."));
        chkGroupBySession->setText(QCoreApplication::translate("StackingTab", "Group by Session"));
        chkGroupByCustomTag->setToolTip(
            QCoreApplication::translate("StackingTab", "Affects final stacks only (not calibration matching)."));
        chkGroupByCustomTag->setText(QCoreApplication::translate("StackingTab", "Group by Custom Tag"));
        chkGroupByExposure->setToolTip(
            QCoreApplication::translate("StackingTab", "Affects final stacks only (not calibration matching)."));
        chkGroupByExposure->setText(QCoreApplication::translate("StackingTab", "Group by Exposure"));
        spinExposureTolerance->setToolTip(QCoreApplication::translate(
            "StackingTab", "Exposure grouping tolerance for final stacks (not calibration matching)."));
        spinExposureTolerance->setSuffix(QCoreApplication::translate("StackingTab", " s"));
        btnStackingSettings->setText(QCoreApplication::translate("StackingTab", "Stacking Settings..."));
        btnRunIntegrationGroup->setText(QCoreApplication::translate("StackingTab", "Run This Group"));
        grpExecutionOptions->setTitle(QCoreApplication::translate("StackingTab", "Execution Options"));
        chkGenerate64Bit->setText(QCoreApplication::translate("StackingTab", "Generate 64-bit images"));
        chkReprocessExisting->setText(QCoreApplication::translate("StackingTab", "Reprocess existing"));
        chkOpenIntegratedFiles->setToolTip(QCoreApplication::translate(
            "StackingTab",
            "Open stacked master light files in the main image workspace when workflow execution completes."));
        chkOpenIntegratedFiles->setText(QCoreApplication::translate("StackingTab", "Open integrated files"));
        chkCombineRgbImages->setToolTip(QCoreApplication::translate(
            "StackingTab",
            "Combine separate red, green, and blue master light groups from the same registration group into one RGB "
            "master at the end of stacking."));
        chkCombineRgbImages->setText(QCoreApplication::translate("StackingTab", "Combine RGB images"));
        chkSaveCalibratedFrames->setToolTip(QCoreApplication::translate(
            "StackingTab", "Save calibrated light frames to disk for inspection/debugging. If unchecked (default), "
                           "calibration happens in-memory which is faster and saves disk space."));
        chkSaveCalibratedFrames->setText(
            QCoreApplication::translate("StackingTab", "Save calibrated frames to disk (for debugging)"));
        chkLimitMemory->setToolTip(QCoreApplication::translate(
            "StackingTab", "Limit the maximum amount of RAM used during stacking operations. Enabling this will use "
                           "tiled processing to respect the limit."));
        chkLimitMemory->setText(QCoreApplication::translate("StackingTab", "Limit memory usage"));
        spinMaxMemory->setToolTip(
            QCoreApplication::translate("StackingTab", "Maximum memory usage in megabytes (MB)."));
        spinMaxMemory->setSuffix(QCoreApplication::translate("StackingTab", " MB"));
        lblMaxParallelCores->setText(QCoreApplication::translate("StackingTab", "CPU Threads:"));
        spinMaxParallelCores->setToolTip(QCoreApplication::translate(
            "StackingTab", "Maximum CPU threads used by ImageIntegration parallel processing."));
        spinMaxParallelCores->setSuffix(QString());
        btnRunIntegration->setToolTip(QCoreApplication::translate(
            "StackingTab",
            "Run the full integration workflow for the selected group (calibration, registration, stacking)"));
        btnRunIntegration->setText(QCoreApplication::translate("StackingTab", "Run Integration"));
    }
};

inline StackingTabUiElements buildStackingTabUi(QWidget* parent)
{
    StackingTabUiElements ui{};
    if (!parent)
    {
        return ui;
    }

    ui.setupUi(parent);
    return ui;
}

} // namespace an::ui::imageintegration
