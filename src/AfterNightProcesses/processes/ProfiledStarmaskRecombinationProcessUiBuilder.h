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
#include <QtWidgets/QDoubleSpinBox>
#include <QtWidgets/QFormLayout>
#include <QtWidgets/QGridLayout>
#include <QtWidgets/QGroupBox>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QSlider>
#include <QtWidgets/QSpacerItem>
#include <QtWidgets/QSpinBox>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QWidget>

#include "processes/RTPreviewUiCommon.h"
#include "theme/AnDesignTokens.h"

namespace an::ui::processes
{

class ProfiledStarmaskRecombinationProcessUiElements
{
public:
    QWidget* horizontalLayoutWidget_4;
    QHBoxLayout* horizontalLayoutMain;
    QVBoxLayout* verticalLayout;
    QGroupBox* groupBox;
    QWidget* layoutWidget2;
    QGridLayout* gridLayout;
    QLabel* label;
    QComboBox* starlessImage;
    QLabel* label_2;
    QComboBox* starmaskImage;
    QGroupBox* groupBox_2;
    QLabel* label_3;
    QPushButton* openStarsProfileButton;
    QLineEdit* starsProfile;
    QPushButton* starsFilterButton;
    QPushButton* clearProfileButton;
    QCheckBox* showStarProfiles;
    QCheckBox* invertSelection;
    QLabel* filteredStarsLabel;
    QLineEdit* filteredStarsTxt;
    QWidget* layoutWidget1;
    QGridLayout* gridLayout_2;
    QLabel* label_4;
    QDoubleSpinBox* scaleFactorValue;
    QSlider* scaleFactorSlider;
    QLabel* label_5;
    QDoubleSpinBox* tightenEdgesValue;
    QSlider* tightenEdgesSlider;
    QPushButton* reloadStarsProfileButton;
    QGroupBox* maskGenerationGroupBox;
    QWidget* layoutWidget;
    QHBoxLayout* horizontalLayout;
    QLabel* label_6;
    QComboBox* shapeSelector;
    QWidget* layoutWidget_2;
    QGridLayout* gridLayout_3;
    QLabel* label_7;
    QDoubleSpinBox* softenEdgesValue;
    QSlider* softenEdgesSlider;
    QLabel* label_8;
    QDoubleSpinBox* percentPixelIncreaseValue;
    QSlider* percentPixelIncreaseSlider;
    QLabel* label_9;
    QSpinBox* pixelIncreaseValue;
    QSlider* pixelIncreaseSlider;
    QSpacerItem* verticalSpacer;
    QFormLayout* formLayout;
    QHBoxLayout* horizontalLayout_4;
    QCheckBox* preview;
    QSpacerItem* horizontalSpacer;
    QHBoxLayout* horizontalLayout_3;
    QDialogButtonBox* buttonBox;
    QVBoxLayout* verticalLayout_2;
    QWidget* widget;

    void setupUi(QWidget* ProfiledStarmaskRecombinationProcess)
    {
        const auto& t = an::ui::tokens();
        if (ProfiledStarmaskRecombinationProcess->objectName().isEmpty())
            ProfiledStarmaskRecombinationProcess->setObjectName("ProfiledStarmaskRecombinationProcess");
        ProfiledStarmaskRecombinationProcess->resize(1469, 722);
        ProfiledStarmaskRecombinationProcess->setMinimumSize(QSize(400, 200));
        horizontalLayoutWidget_4 = ProfiledStarmaskRecombinationProcess;
        horizontalLayoutMain = new QHBoxLayout(horizontalLayoutWidget_4);
        horizontalLayoutMain->setSpacing(t.spacing.md);
        horizontalLayoutMain->setObjectName("horizontalLayoutMain");
        horizontalLayoutMain->setSizeConstraint(QLayout::SetMinimumSize);
        horizontalLayoutMain->setContentsMargins(t.spacing.md, t.spacing.md, t.spacing.md, t.spacing.md);
        verticalLayout = new QVBoxLayout();
        verticalLayout->setObjectName("verticalLayout");
        verticalLayout->setSizeConstraint(QLayout::SetDefaultConstraint);
        verticalLayout->setContentsMargins(-1, t.spacing.md, -1, -1);
        groupBox = new QGroupBox(horizontalLayoutWidget_4);
        groupBox->setObjectName("groupBox");
        groupBox->setMinimumSize(QSize(580, 100));
        auto groupBoxLayout = new QVBoxLayout(groupBox);
        groupBoxLayout->setSpacing(t.spacing.sm);
        groupBoxLayout->setContentsMargins(t.spacing.md, t.spacing.md, t.spacing.md, t.spacing.md);
        layoutWidget2 = new QWidget(groupBox);
        layoutWidget2->setObjectName("layoutWidget2");
        layoutWidget2->setSizePolicy(QSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred));
        gridLayout = new QGridLayout(layoutWidget2);
        gridLayout->setObjectName("gridLayout");
        gridLayout->setContentsMargins(0, 0, 0, 0);
        label = new QLabel(layoutWidget2);
        label->setObjectName("label");

        gridLayout->addWidget(label, 0, 0, 1, 1);

        starlessImage = new QComboBox(layoutWidget2);
        starlessImage->setObjectName("starlessImage");
        starlessImage->setSizePolicy(QSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed));

        gridLayout->addWidget(starlessImage, 0, 1, 1, 1);

        label_2 = new QLabel(layoutWidget2);
        label_2->setObjectName("label_2");

        gridLayout->addWidget(label_2, 1, 0, 1, 1);

        starmaskImage = new QComboBox(layoutWidget2);
        starmaskImage->setObjectName("starmaskImage");
        starmaskImage->setSizePolicy(QSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed));

        gridLayout->addWidget(starmaskImage, 1, 1, 1, 1);

        gridLayout->setColumnStretch(0, 1);
        gridLayout->setColumnStretch(1, 5);
        groupBoxLayout->addWidget(layoutWidget2);

        verticalLayout->addWidget(groupBox);

        groupBox_2 = new QGroupBox(horizontalLayoutWidget_4);
        groupBox_2->setObjectName("groupBox_2");
        groupBox_2->setMinimumSize(QSize(580, 350));
        auto groupBox2Layout = new QVBoxLayout(groupBox_2);
        groupBox2Layout->setSpacing(t.spacing.md);
        groupBox2Layout->setContentsMargins(t.spacing.md, t.spacing.md, t.spacing.md, t.spacing.md);
        label_3 = new QLabel(groupBox_2);
        label_3->setObjectName("label_3");
        auto profilePathLayout = new QHBoxLayout();
        profilePathLayout->setSpacing(t.spacing.sm);
        profilePathLayout->addWidget(label_3);
        openStarsProfileButton = new QPushButton(groupBox_2);
        openStarsProfileButton->setObjectName("openStarsProfileButton");
        openStarsProfileButton->setMinimumSize(QSize(30, 24));
        openStarsProfileButton->setMaximumSize(QSize(30, 24));
        starsProfile = new QLineEdit(groupBox_2);
        starsProfile->setObjectName("starsProfile");
        starsProfile->setSizePolicy(QSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed));
        profilePathLayout->addWidget(starsProfile, 1);
        profilePathLayout->addWidget(openStarsProfileButton);
        reloadStarsProfileButton = new QPushButton(groupBox_2);
        reloadStarsProfileButton->setObjectName("reloadStarsProfileButton");
        reloadStarsProfileButton->setMinimumSize(QSize(30, 24));
        reloadStarsProfileButton->setMaximumSize(QSize(30, 24));
        profilePathLayout->addWidget(reloadStarsProfileButton);
        starsFilterButton = new QPushButton(groupBox_2);
        starsFilterButton->setObjectName("starsFilterButton");
        clearProfileButton = new QPushButton(groupBox_2);
        clearProfileButton->setObjectName("clearProfileButton");
        clearProfileButton->setMinimumSize(QSize(30, 24));
        clearProfileButton->setMaximumSize(QSize(30, 24));
        profilePathLayout->addWidget(clearProfileButton);
        groupBox2Layout->addLayout(profilePathLayout);

        auto profileActionsLayout = new QHBoxLayout();
        profileActionsLayout->setSpacing(t.spacing.sm);
        showStarProfiles = new QCheckBox(groupBox_2);
        showStarProfiles->setObjectName("showStarProfiles");
        showStarProfiles->setChecked(true);
        invertSelection = new QCheckBox(groupBox_2);
        invertSelection->setObjectName("invertSelection");
        filteredStarsLabel = new QLabel(groupBox_2);
        filteredStarsLabel->setObjectName("filteredStarsLabel");
        profileActionsLayout->addWidget(filteredStarsLabel);
        filteredStarsTxt = new QLineEdit(groupBox_2);
        filteredStarsTxt->setObjectName("filteredStarsTxt");
        filteredStarsTxt->setReadOnly(true);
        filteredStarsTxt->setMaximumWidth(140);
        profileActionsLayout->addWidget(filteredStarsTxt);
        profileActionsLayout->addWidget(starsFilterButton);
        profileActionsLayout->addWidget(showStarProfiles);
        profileActionsLayout->addWidget(invertSelection);
        profileActionsLayout->addStretch();
        groupBox2Layout->addLayout(profileActionsLayout);

        layoutWidget1 = new QWidget(groupBox_2);
        layoutWidget1->setObjectName("layoutWidget1");
        gridLayout_2 = new QGridLayout(layoutWidget1);
        gridLayout_2->setObjectName("gridLayout_2");
        gridLayout_2->setContentsMargins(0, 0, 0, 0);
        label_4 = new QLabel(layoutWidget1);
        label_4->setObjectName("label_4");

        gridLayout_2->addWidget(label_4, 0, 0, 1, 1);

        scaleFactorValue = new QDoubleSpinBox(layoutWidget1);
        scaleFactorValue->setObjectName("scaleFactorValue");
        scaleFactorValue->setMaximum(2.000000000000000);
        scaleFactorValue->setSingleStep(0.050000000000000);
        scaleFactorValue->setValue(1.000000000000000);
        scaleFactorValue->setMinimumWidth(80);

        gridLayout_2->addWidget(scaleFactorValue, 0, 1, 1, 1);

        scaleFactorSlider = new QSlider(layoutWidget1);
        scaleFactorSlider->setObjectName("scaleFactorSlider");
        scaleFactorSlider->setMaximum(2000);
        scaleFactorSlider->setValue(1000);
        scaleFactorSlider->setOrientation(Qt::Horizontal);

        gridLayout_2->addWidget(scaleFactorSlider, 0, 2, 1, 1);

        label_5 = new QLabel(layoutWidget1);
        label_5->setObjectName("label_5");

        gridLayout_2->addWidget(label_5, 1, 0, 1, 1);

        tightenEdgesValue = new QDoubleSpinBox(layoutWidget1);
        tightenEdgesValue->setObjectName("tightenEdgesValue");
        tightenEdgesValue->setMaximum(2.000000000000000);
        tightenEdgesValue->setSingleStep(0.030000000000000);
        tightenEdgesValue->setMinimumWidth(80);

        gridLayout_2->addWidget(tightenEdgesValue, 1, 1, 1, 1);

        tightenEdgesSlider = new QSlider(layoutWidget1);
        tightenEdgesSlider->setObjectName("tightenEdgesSlider");
        tightenEdgesSlider->setMaximum(2000);
        tightenEdgesSlider->setOrientation(Qt::Horizontal);

        gridLayout_2->addWidget(tightenEdgesSlider, 1, 2, 1, 1);

        gridLayout_2->setColumnStretch(0, 1);
        gridLayout_2->setColumnStretch(1, 1);
        gridLayout_2->setColumnStretch(2, 6);
        groupBox2Layout->addWidget(layoutWidget1);
        maskGenerationGroupBox = new QGroupBox(groupBox_2);
        maskGenerationGroupBox->setObjectName("maskGenerationGroupBox");
        auto maskGenerationLayout = new QVBoxLayout(maskGenerationGroupBox);
        maskGenerationLayout->setSpacing(t.spacing.md);
        maskGenerationLayout->setContentsMargins(t.spacing.md, t.spacing.md, t.spacing.md, t.spacing.md);
        layoutWidget = new QWidget(maskGenerationGroupBox);
        layoutWidget->setObjectName("layoutWidget");
        layoutWidget->setSizePolicy(QSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed));
        horizontalLayout = new QHBoxLayout(layoutWidget);
        horizontalLayout->setObjectName("horizontalLayout");
        horizontalLayout->setContentsMargins(0, 0, 0, 0);
        label_6 = new QLabel(layoutWidget);
        label_6->setObjectName("label_6");

        horizontalLayout->addWidget(label_6);

        shapeSelector = new QComboBox(layoutWidget);
        shapeSelector->setObjectName("shapeSelector");

        horizontalLayout->addWidget(shapeSelector);

        horizontalLayout->setStretch(0, 2);
        horizontalLayout->setStretch(1, 3);
        maskGenerationLayout->addWidget(layoutWidget);
        layoutWidget_2 = new QWidget(maskGenerationGroupBox);
        layoutWidget_2->setObjectName("layoutWidget_2");
        layoutWidget_2->setSizePolicy(QSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred));
        gridLayout_3 = new QGridLayout(layoutWidget_2);
        gridLayout_3->setObjectName("gridLayout_3");
        gridLayout_3->setContentsMargins(0, 0, 0, 0);
        label_7 = new QLabel(layoutWidget_2);
        label_7->setObjectName("label_7");

        gridLayout_3->addWidget(label_7, 0, 0, 1, 1);

        softenEdgesValue = new QDoubleSpinBox(layoutWidget_2);
        softenEdgesValue->setObjectName("softenEdgesValue");
        softenEdgesValue->setMaximum(10.000000000000000);
        softenEdgesValue->setSingleStep(0.030000000000000);
        softenEdgesValue->setMinimumWidth(80);

        gridLayout_3->addWidget(softenEdgesValue, 0, 1, 1, 1);

        softenEdgesSlider = new QSlider(layoutWidget_2);
        softenEdgesSlider->setObjectName("softenEdgesSlider");
        softenEdgesSlider->setMaximum(1000);
        softenEdgesSlider->setOrientation(Qt::Horizontal);

        gridLayout_3->addWidget(softenEdgesSlider, 0, 2, 1, 1);

        label_8 = new QLabel(layoutWidget_2);
        label_8->setObjectName("label_8");

        gridLayout_3->addWidget(label_8, 1, 0, 1, 1);

        percentPixelIncreaseValue = new QDoubleSpinBox(layoutWidget_2);
        percentPixelIncreaseValue->setObjectName("percentPixelIncreaseValue");
        percentPixelIncreaseValue->setMaximum(1.000000000000000);
        percentPixelIncreaseValue->setSingleStep(0.030000000000000);
        percentPixelIncreaseValue->setValue(0.500000000000000);
        percentPixelIncreaseValue->setMinimumWidth(80);

        gridLayout_3->addWidget(percentPixelIncreaseValue, 1, 1, 1, 1);

        percentPixelIncreaseSlider = new QSlider(layoutWidget_2);
        percentPixelIncreaseSlider->setObjectName("percentPixelIncreaseSlider");
        percentPixelIncreaseSlider->setMaximum(1000);
        percentPixelIncreaseSlider->setValue(500);
        percentPixelIncreaseSlider->setOrientation(Qt::Horizontal);

        gridLayout_3->addWidget(percentPixelIncreaseSlider, 1, 2, 1, 1);

        label_9 = new QLabel(layoutWidget_2);
        label_9->setObjectName("label_9");

        gridLayout_3->addWidget(label_9, 2, 0, 1, 1);

        pixelIncreaseValue = new QSpinBox(layoutWidget_2);
        pixelIncreaseValue->setObjectName("pixelIncreaseValue");
        pixelIncreaseValue->setMaximum(20);
        pixelIncreaseValue->setMinimumWidth(80);

        gridLayout_3->addWidget(pixelIncreaseValue, 2, 1, 1, 1);

        pixelIncreaseSlider = new QSlider(layoutWidget_2);
        pixelIncreaseSlider->setObjectName("pixelIncreaseSlider");
        pixelIncreaseSlider->setMaximum(20);
        pixelIncreaseSlider->setOrientation(Qt::Horizontal);

        gridLayout_3->addWidget(pixelIncreaseSlider, 2, 2, 1, 1);

        gridLayout_3->setColumnStretch(0, 2);
        gridLayout_3->setColumnStretch(1, 1);
        gridLayout_3->setColumnStretch(2, 7);
        maskGenerationLayout->addWidget(layoutWidget_2);
        groupBox2Layout->addWidget(maskGenerationGroupBox);

        verticalLayout->addWidget(groupBox_2);

        verticalSpacer = new QSpacerItem(20, 40, QSizePolicy::Policy::Minimum, QSizePolicy::Policy::Expanding);

        verticalLayout->addItem(verticalSpacer);

        formLayout = new QFormLayout();
        formLayout->setObjectName("formLayout");

        verticalLayout->addLayout(formLayout);

        horizontalLayout_4 = new QHBoxLayout();
        horizontalLayout_4->setObjectName("horizontalLayout_4");
        preview = new QCheckBox(horizontalLayoutWidget_4);
        preview->setObjectName("preview");
        preview->setChecked(false);

        horizontalLayout_4->addWidget(preview);

        horizontalSpacer = new QSpacerItem(40, 20, QSizePolicy::Policy::Expanding, QSizePolicy::Policy::Minimum);

        horizontalLayout_4->addItem(horizontalSpacer);

        verticalLayout->addLayout(horizontalLayout_4);

        horizontalLayout_3 = new QHBoxLayout();
        horizontalLayout_3->setObjectName("horizontalLayout_3");
        horizontalLayout_3->setSizeConstraint(QLayout::SetDefaultConstraint);
        buttonBox = new QDialogButtonBox(horizontalLayoutWidget_4);
        buttonBox->setObjectName("buttonBox");
        buttonBox->setStandardButtons(QDialogButtonBox::Close | QDialogButtonBox::Ok | QDialogButtonBox::Reset);

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

        retranslateUi(ProfiledStarmaskRecombinationProcess);

        QMetaObject::connectSlotsByName(ProfiledStarmaskRecombinationProcess);
    } // setupUi

    void retranslateUi(QWidget* ProfiledStarmaskRecombinationProcess)
    {
        ProfiledStarmaskRecombinationProcess->setWindowTitle(QCoreApplication::translate(
            "ProfiledStarmaskRecombinationProcess", "Profiled Starmask Recombination", nullptr));
        groupBox->setTitle(
            QCoreApplication::translate("ProfiledStarmaskRecombinationProcess", "Image Composition", nullptr));
        label->setText(QCoreApplication::translate("ProfiledStarmaskRecombinationProcess", "Starless Image", nullptr));
        label_2->setText(QCoreApplication::translate("ProfiledStarmaskRecombinationProcess", "Stars Image", nullptr));
        groupBox_2->setTitle(
            QCoreApplication::translate("ProfiledStarmaskRecombinationProcess", "Stars Profile", nullptr));
        label_3->setText(QCoreApplication::translate("ProfiledStarmaskRecombinationProcess", "Path", nullptr));
#if QT_CONFIG(tooltip)
        openStarsProfileButton->setToolTip(
            QCoreApplication::translate("ProfiledStarmaskRecombinationProcess", "Open stars profile file", nullptr));
#endif // QT_CONFIG(tooltip)
        openStarsProfileButton->setText(
            QCoreApplication::translate("ProfiledStarmaskRecombinationProcess", "...", nullptr));
        starsFilterButton->setText(
            QCoreApplication::translate("ProfiledStarmaskRecombinationProcess", "Stars Filter", nullptr));
#if QT_CONFIG(tooltip)
        clearProfileButton->setToolTip(
            QCoreApplication::translate("ProfiledStarmaskRecombinationProcess", "Unload stars profile", nullptr));
#endif // QT_CONFIG(tooltip)
        clearProfileButton->setText(QString());
        showStarProfiles->setText(
            QCoreApplication::translate("ProfiledStarmaskRecombinationProcess", "Show Star Profiles", nullptr));
        invertSelection->setText(
            QCoreApplication::translate("ProfiledStarmaskRecombinationProcess", "Invert Selection", nullptr));
        filteredStarsLabel->setText(
            QCoreApplication::translate("ProfiledStarmaskRecombinationProcess", "Current Stars", nullptr));
        label_4->setText(QCoreApplication::translate("ProfiledStarmaskRecombinationProcess", "Scale Factor", nullptr));
        label_5->setText(QCoreApplication::translate("ProfiledStarmaskRecombinationProcess", "Tighten Edges", nullptr));
#if QT_CONFIG(tooltip)
        reloadStarsProfileButton->setToolTip(
            QCoreApplication::translate("ProfiledStarmaskRecombinationProcess", "Reload stars profile file", nullptr));
#endif // QT_CONFIG(tooltip)
        reloadStarsProfileButton->setText(QString());
        maskGenerationGroupBox->setTitle(
            QCoreApplication::translate("ProfiledStarmaskRecombinationProcess", "Mask Generation", nullptr));
        label_6->setText(QCoreApplication::translate("ProfiledStarmaskRecombinationProcess", "Shape", nullptr));
        label_7->setText(QCoreApplication::translate("ProfiledStarmaskRecombinationProcess", "Soften Edges", nullptr));
        label_8->setText(
            QCoreApplication::translate("ProfiledStarmaskRecombinationProcess", "% Pixel Increase", nullptr));
        label_9->setText(
            QCoreApplication::translate("ProfiledStarmaskRecombinationProcess", "Pixel Increase", nullptr));
        preview->setText(QCoreApplication::translate("ProfiledStarmaskRecombinationProcess", "Preview", nullptr));
    } // retranslateUi
};

inline ProfiledStarmaskRecombinationProcessUiElements buildProfiledStarmaskRecombinationProcessUi(QWidget* parent)
{
    ProfiledStarmaskRecombinationProcessUiElements ui;
    ui.setupUi(parent);
    return ui;
}

} // namespace an::ui::processes
