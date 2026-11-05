// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include <QtCore/QtCore>
#include <QtGui/QtGui>
#include <QtWidgets/QtWidgets>
#include "components/AnCardWidget.h"
#include "components/AnCollapsibleSection.h"
#include "theme/AnDesignTokens.h"

namespace an::ui::imageintegration
{

class RegistrationTabUiElements
{
public:
    QVBoxLayout* verticalLayout = nullptr;
    an::ui::AnCardWidget* grpRegistrationGroups = nullptr;
    QVBoxLayout* verticalLayout_2 = nullptr;
    QTableWidget* tblRegistrationGroups = nullptr;
    QHBoxLayout* registrationGroupActionsLayout = nullptr;
    QPushButton* btnGroupSettings = nullptr;
    an::ui::AnCardWidget* grpFrameQuality = nullptr;
    QVBoxLayout* verticalLayout_3 = nullptr;
    QLabel* lblFrameQualityHelp = nullptr;
    QLabel* lblFrameQualityBanner = nullptr;
    QTableWidget* tblFrameQuality = nullptr;
    QLabel* lblFrameStatusLegend = nullptr;
    QHBoxLayout* frameActionsLayout = nullptr;
    QPushButton* btnSetReference = nullptr;
    QPushButton* btnExcludeSelected = nullptr;
    QPushButton* btnIncludeSelected = nullptr;
    QPushButton* btnResetExcluded = nullptr;
    an::ui::AnCollapsibleSection* grpQualityThresholds = nullptr;
    QGridLayout* thresholdLayout = nullptr;
    QCheckBox* chkMinScore = nullptr;
    QLabel* lblMinScore = nullptr;
    QDoubleSpinBox* spinMinScore = nullptr;
    QCheckBox* chkMinStars = nullptr;
    QLabel* lblMinStars = nullptr;
    QSpinBox* spinMinStars = nullptr;
    QCheckBox* chkMinSnr = nullptr;
    QLabel* lblMinSnr = nullptr;
    QDoubleSpinBox* spinMinSnr = nullptr;
    QCheckBox* chkMaxFwhm = nullptr;
    QLabel* lblMaxFwhm = nullptr;
    QDoubleSpinBox* spinMaxFwhm = nullptr;
    QCheckBox* chkMaxEccentricity = nullptr;
    QLabel* lblMaxEcc = nullptr;
    QDoubleSpinBox* spinMaxEccentricity = nullptr;
    QLabel* lblThresholdPreview = nullptr;
    QPushButton* btnDetectOutliers = nullptr;
    QPushButton* btnApplyThresholds = nullptr;
    QHBoxLayout* registrationActionsLayout = nullptr;
    QPushButton* btnRunRegistration = nullptr;

    void setupUi(QWidget* RegistrationTab)
    {
        const auto& t = an::ui::tokens();
        RegistrationTab->setObjectName("RegistrationTab");
        RegistrationTab->resize(900, 600);
        verticalLayout = new QVBoxLayout(RegistrationTab);
        verticalLayout->setContentsMargins(t.spacing.md, t.spacing.md, t.spacing.md, t.spacing.md);
        verticalLayout->setObjectName("verticalLayout");
        grpRegistrationGroups = new an::ui::AnCardWidget(QString(), RegistrationTab);
        grpRegistrationGroups->setObjectName("grpRegistrationGroups");

        QWidget* grpRegistrationGroupsContent = grpRegistrationGroups->contentWidget();
        verticalLayout_2 = new QVBoxLayout(grpRegistrationGroupsContent);
        verticalLayout_2->setContentsMargins(t.spacing.sm, t.spacing.md, t.spacing.sm, t.spacing.sm);
        verticalLayout_2->setObjectName("verticalLayout_2");
        grpRegistrationGroups->setContentLayout(verticalLayout_2);

        tblRegistrationGroups = new QTableWidget(grpRegistrationGroupsContent);
        tblRegistrationGroups->setSelectionBehavior(QAbstractItemView::SelectionBehavior::SelectRows);
        tblRegistrationGroups->setSelectionMode(QAbstractItemView::SelectionMode::SingleSelection);
        tblRegistrationGroups->setAlternatingRowColors(true);
        tblRegistrationGroups->setObjectName("tblRegistrationGroups");
        tblRegistrationGroups->setColumnCount(0);
        tblRegistrationGroups->setRowCount(0);
        verticalLayout_2->addWidget(tblRegistrationGroups);
        registrationGroupActionsLayout = new QHBoxLayout();
        registrationGroupActionsLayout->setObjectName("registrationGroupActionsLayout");
        btnGroupSettings = new QPushButton(grpRegistrationGroups->contentWidget());
        btnGroupSettings->setEnabled(false);
        btnGroupSettings->setObjectName("btnGroupSettings");
        registrationGroupActionsLayout->addWidget(btnGroupSettings);
        auto* spacerItem = new QSpacerItem(40, 20, QSizePolicy::Policy::Expanding, QSizePolicy::Policy::Minimum);
        registrationGroupActionsLayout->addItem(spacerItem);
        verticalLayout_2->addLayout(registrationGroupActionsLayout);
        verticalLayout->addWidget(grpRegistrationGroups);
        grpFrameQuality = new an::ui::AnCardWidget(QString(), RegistrationTab);
        grpFrameQuality->setObjectName("grpFrameQuality");
        verticalLayout_3 = new QVBoxLayout();
        verticalLayout_3->setObjectName("verticalLayout_3");
        grpFrameQuality->setContentLayout(verticalLayout_3);
        verticalLayout_3->setContentsMargins(t.spacing.sm, t.spacing.md, t.spacing.sm, t.spacing.sm);
        verticalLayout_3->setObjectName("verticalLayout_3");
        lblFrameQualityHelp = new QLabel(grpFrameQuality->contentWidget());
        lblFrameQualityHelp->setWordWrap(true);
        lblFrameQualityHelp->setObjectName("lblFrameQualityHelp");
        verticalLayout_3->addWidget(lblFrameQualityHelp);
        lblFrameQualityBanner = new QLabel(grpFrameQuality->contentWidget());
        lblFrameQualityBanner->setVisible(false);
        lblFrameQualityBanner->setWordWrap(true);
        lblFrameQualityBanner->setObjectName("lblFrameQualityBanner");
        verticalLayout_3->addWidget(lblFrameQualityBanner);
        tblFrameQuality = new QTableWidget(grpFrameQuality->contentWidget());
        tblFrameQuality->setSelectionBehavior(QAbstractItemView::SelectionBehavior::SelectRows);
        tblFrameQuality->setSelectionMode(QAbstractItemView::SelectionMode::ExtendedSelection);
        tblFrameQuality->setAlternatingRowColors(true);
        tblFrameQuality->setObjectName("tblFrameQuality");
        tblFrameQuality->setColumnCount(0);
        tblFrameQuality->setRowCount(0);
        verticalLayout_3->addWidget(tblFrameQuality);
        lblFrameStatusLegend = new QLabel(grpFrameQuality->contentWidget());
        lblFrameStatusLegend->setObjectName("lblFrameStatusLegend");
        verticalLayout_3->addWidget(lblFrameStatusLegend);
        frameActionsLayout = new QHBoxLayout();
        frameActionsLayout->setObjectName("frameActionsLayout");
        btnSetReference = new QPushButton(grpFrameQuality->contentWidget());
        btnSetReference->setEnabled(false);
        btnSetReference->setObjectName("btnSetReference");
        frameActionsLayout->addWidget(btnSetReference);
        btnExcludeSelected = new QPushButton(grpFrameQuality->contentWidget());
        btnExcludeSelected->setEnabled(false);
        btnExcludeSelected->setObjectName("btnExcludeSelected");
        frameActionsLayout->addWidget(btnExcludeSelected);
        btnIncludeSelected = new QPushButton(grpFrameQuality->contentWidget());
        btnIncludeSelected->setEnabled(false);
        btnIncludeSelected->setObjectName("btnIncludeSelected");
        frameActionsLayout->addWidget(btnIncludeSelected);
        btnResetExcluded = new QPushButton(grpFrameQuality->contentWidget());
        btnResetExcluded->setEnabled(false);
        btnResetExcluded->setObjectName("btnResetExcluded");
        frameActionsLayout->addWidget(btnResetExcluded);
        auto* spacerItem1 = new QSpacerItem(40, 20, QSizePolicy::Policy::Expanding, QSizePolicy::Policy::Minimum);
        frameActionsLayout->addItem(spacerItem1);
        verticalLayout_3->addLayout(frameActionsLayout);
        grpQualityThresholds = new an::ui::AnCollapsibleSection(QString(), grpFrameQuality->contentWidget());
        grpQualityThresholds->setObjectName("grpQualityThresholds");
        thresholdLayout = new QGridLayout();
        grpQualityThresholds->setContentLayout(thresholdLayout);
        thresholdLayout->setContentsMargins(t.spacing.sm, t.spacing.md, t.spacing.sm, t.spacing.sm);
        thresholdLayout->setHorizontalSpacing(t.spacing.md);
        thresholdLayout->setVerticalSpacing(t.spacing.sm);
        thresholdLayout->setObjectName("thresholdLayout");

        const auto addThresholdControl = [&](QCheckBox*& checkBox, QLabel*& label, QWidget* editor,
                                             const QString& checkObjectName, const QString& labelObjectName,
                                             const int row, const int col) {
            checkBox = new QCheckBox(grpQualityThresholds->contentWidget());
            checkBox->setObjectName(checkObjectName);
            thresholdLayout->addWidget(checkBox, row, col);
            label = new QLabel(grpQualityThresholds->contentWidget());
            label->setObjectName(labelObjectName);
            thresholdLayout->addWidget(label, row, col + 1);
            editor->setEnabled(false);
            thresholdLayout->addWidget(editor, row, col + 2);
        };

        spinMinScore = new QDoubleSpinBox(grpQualityThresholds->contentWidget());
        spinMinScore->setMinimum(0.0);
        spinMinScore->setMaximum(1000.0);
        spinMinScore->setProperty("value", 0.0);
        spinMinScore->setSingleStep(1.0);
        spinMinScore->setDecimals(2);
        spinMinScore->setObjectName("spinMinScore");
        addThresholdControl(chkMinScore, lblMinScore, spinMinScore, QStringLiteral("chkMinScore"),
                            QStringLiteral("lblMinScore"), 0, 0);

        spinMinStars = new QSpinBox(grpQualityThresholds->contentWidget());
        spinMinStars->setMinimum(0);
        spinMinStars->setMaximum(100000);
        spinMinStars->setProperty("value", 0);
        spinMinStars->setObjectName("spinMinStars");
        addThresholdControl(chkMinStars, lblMinStars, spinMinStars, QStringLiteral("chkMinStars"),
                            QStringLiteral("lblMinStars"), 0, 3);

        spinMinSnr = new QDoubleSpinBox(grpQualityThresholds->contentWidget());
        spinMinSnr->setMinimum(0.0);
        spinMinSnr->setMaximum(100000.0);
        spinMinSnr->setProperty("value", 0.0);
        spinMinSnr->setSingleStep(1.0);
        spinMinSnr->setDecimals(2);
        spinMinSnr->setObjectName("spinMinSnr");
        addThresholdControl(chkMinSnr, lblMinSnr, spinMinSnr, QStringLiteral("chkMinSnr"), QStringLiteral("lblMinSnr"),
                            0, 6);

        spinMaxFwhm = new QDoubleSpinBox(grpQualityThresholds->contentWidget());
        spinMaxFwhm->setMinimum(0.0);
        spinMaxFwhm->setMaximum(100.0);
        spinMaxFwhm->setProperty("value", 0.0);
        spinMaxFwhm->setSingleStep(0.1);
        spinMaxFwhm->setDecimals(2);
        spinMaxFwhm->setObjectName("spinMaxFwhm");
        addThresholdControl(chkMaxFwhm, lblMaxFwhm, spinMaxFwhm, QStringLiteral("chkMaxFwhm"),
                            QStringLiteral("lblMaxFwhm"), 1, 0);

        spinMaxEccentricity = new QDoubleSpinBox(grpQualityThresholds->contentWidget());
        spinMaxEccentricity->setMinimum(0.0);
        spinMaxEccentricity->setMaximum(1.0);
        spinMaxEccentricity->setProperty("value", 0.0);
        spinMaxEccentricity->setSingleStep(0.05);
        spinMaxEccentricity->setDecimals(2);
        spinMaxEccentricity->setObjectName("spinMaxEccentricity");
        addThresholdControl(chkMaxEccentricity, lblMaxEcc, spinMaxEccentricity, QStringLiteral("chkMaxEccentricity"),
                            QStringLiteral("lblMaxEcc"), 1, 3);

        lblThresholdPreview = new QLabel(grpQualityThresholds->contentWidget());
        lblThresholdPreview->setText("");
        lblThresholdPreview->setAlignment(Qt::AlignmentFlag::AlignRight | Qt::AlignmentFlag::AlignTrailing |
                                          Qt::AlignmentFlag::AlignVCenter);
        lblThresholdPreview->setObjectName("lblThresholdPreview");
        thresholdLayout->addWidget(lblThresholdPreview, 1, 6, 1, 3);

        auto* thresholdActionsLayout = new QHBoxLayout();
        thresholdActionsLayout->setObjectName("thresholdActionsLayout");
        thresholdActionsLayout->addStretch(1);
        btnDetectOutliers = new QPushButton(grpQualityThresholds->contentWidget());
        btnDetectOutliers->setObjectName("btnDetectOutliers");
        thresholdActionsLayout->addWidget(btnDetectOutliers);
        btnApplyThresholds = new QPushButton(grpQualityThresholds->contentWidget());
        btnApplyThresholds->setObjectName("btnApplyThresholds");
        thresholdActionsLayout->addWidget(btnApplyThresholds);
        thresholdLayout->addLayout(thresholdActionsLayout, 2, 0, 1, 9);
        thresholdLayout->setColumnStretch(8, 1);
        grpQualityThresholds->setExpanded(false);
        verticalLayout_3->addWidget(grpQualityThresholds);
        verticalLayout->addWidget(grpFrameQuality);
        registrationActionsLayout = new QHBoxLayout();
        registrationActionsLayout->setObjectName("registrationActionsLayout");
        auto* spacerItem3 = new QSpacerItem(40, 20, QSizePolicy::Policy::Expanding, QSizePolicy::Policy::Minimum);
        registrationActionsLayout->addItem(spacerItem3);
        btnRunRegistration = new QPushButton(RegistrationTab);
        btnRunRegistration->setEnabled(false);
        btnRunRegistration->setObjectName("btnRunRegistration");
        registrationActionsLayout->addWidget(btnRunRegistration);
        verticalLayout->addLayout(registrationActionsLayout);
        retranslateUi(RegistrationTab);
        QMetaObject::connectSlotsByName(RegistrationTab);
    }

    void retranslateUi(QWidget* RegistrationTab)
    {
        grpRegistrationGroups->setTitle(QCoreApplication::translate("RegistrationTab", "Registration Groups"));
        btnGroupSettings->setText(QCoreApplication::translate("RegistrationTab", "Registration Settings"));
        btnGroupSettings->setToolTip(
            QCoreApplication::translate("RegistrationTab", "Configure registration settings for the selected group"));
        grpFrameQuality->setTitle(QCoreApplication::translate("RegistrationTab", "Frame Quality"));
        lblFrameQualityHelp->setText(QCoreApplication::translate(
            "RegistrationTab", "Use this table to pick a registration reference and exclude bad frames. Excluded "
                               "frames won\'t be registered or stacked."));
        lblFrameQualityBanner->setText(QCoreApplication::translate(
            "RegistrationTab", "Frame quality metrics are not available. Run Registration first to compute them."));
        lblFrameStatusLegend->setText(QCoreApplication::translate(
            "RegistrationTab", "★ = Reference frame | ✗ = Excluded | Blue = Reference row | Red = Excluded row"));
        btnSetReference->setText(QCoreApplication::translate("RegistrationTab", "Set as Registration Reference"));
        btnSetReference->setToolTip(QCoreApplication::translate(
            "RegistrationTab", "Use the selected frame as the alignment target for all frames in this registration "
                               "group (same dimensions, channels, and binning). This does not affect normalization."));
        btnExcludeSelected->setText(QCoreApplication::translate("RegistrationTab", "Exclude Selected"));
        btnExcludeSelected->setToolTip(QCoreApplication::translate(
            "RegistrationTab",
            "Mark the selected frame(s) as excluded. Excluded frames are ignored by registration and integration."));
        btnIncludeSelected->setText(QCoreApplication::translate("RegistrationTab", "Include Selected"));
        btnIncludeSelected->setToolTip(QCoreApplication::translate(
            "RegistrationTab",
            "Mark the selected frame(s) as included. Included frames are used by registration and integration."));
        btnResetExcluded->setText(QCoreApplication::translate("RegistrationTab", "Reset Excluded..."));
        btnResetExcluded->setToolTip(QCoreApplication::translate(
            "RegistrationTab", "Re-include all excluded frames in the selected registration group."));
        grpQualityThresholds->setTitle(QCoreApplication::translate("RegistrationTab", "Quality Filters"));
        chkMinScore->setToolTip(
            QCoreApplication::translate("RegistrationTab", "Enable minimum composite score filter"));
        lblMinScore->setText(QCoreApplication::translate("RegistrationTab", "Score ≥"));
        spinMinScore->setToolTip(QCoreApplication::translate(
            "RegistrationTab", "Exclude frames with composite quality score below this value."));
        chkMinStars->setToolTip(QCoreApplication::translate("RegistrationTab", "Enable minimum star count filter"));
        lblMinStars->setText(QCoreApplication::translate("RegistrationTab", "Stars ≥"));
        spinMinStars->setToolTip(
            QCoreApplication::translate("RegistrationTab", "Exclude frames with fewer detected stars."));
        chkMinSnr->setToolTip(QCoreApplication::translate("RegistrationTab", "Enable minimum SNR filter"));
        lblMinSnr->setText(QCoreApplication::translate("RegistrationTab", "SNR ≥"));
        spinMinSnr->setToolTip(
            QCoreApplication::translate("RegistrationTab", "Exclude frames with median star SNR below this value."));
        chkMaxFwhm->setToolTip(QCoreApplication::translate("RegistrationTab", "Enable maximum FWHM filter"));
        lblMaxFwhm->setText(QCoreApplication::translate("RegistrationTab", "FWHM ≤"));
        spinMaxFwhm->setToolTip(
            QCoreApplication::translate("RegistrationTab", "Exclude frames with FWHM above this value."));
        chkMaxEccentricity->setToolTip(
            QCoreApplication::translate("RegistrationTab", "Enable maximum eccentricity filter"));
        lblMaxEcc->setText(QCoreApplication::translate("RegistrationTab", "Ecc ≤"));
        spinMaxEccentricity->setToolTip(
            QCoreApplication::translate("RegistrationTab", "Exclude frames with eccentricity above this value."));
        btnDetectOutliers->setText(QCoreApplication::translate("RegistrationTab", "Suggest Filters..."));
        btnDetectOutliers->setToolTip(QCoreApplication::translate(
            "RegistrationTab",
            "Analyze frame metrics and suggest quality filters (IQR-based) for the selected registration group."));
        btnApplyThresholds->setText(QCoreApplication::translate("RegistrationTab", "Exclude Outliers..."));
        btnApplyThresholds->setToolTip(
            QCoreApplication::translate("RegistrationTab", "Apply the enabled quality filters and mark failing frames "
                                                           "as excluded (affects registration and stacking)."));
        btnRunRegistration->setText(QCoreApplication::translate("RegistrationTab", "Run Registration"));
    }
};

inline RegistrationTabUiElements buildRegistrationTabUi(QWidget* parent)
{
    RegistrationTabUiElements ui{};
    if (!parent)
    {
        return ui;
    }

    ui.setupUi(parent);
    return ui;
}

} // namespace an::ui::imageintegration
