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
#include <QtWidgets/QGridLayout>
#include <QtWidgets/QGroupBox>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QHeaderView>
#include <QtWidgets/QLabel>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QSlider>
#include <QtWidgets/QSpacerItem>
#include <QtWidgets/QSpinBox>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QWidget>
#include "theme/AnDesignTokens.h"
#include "widgets/TableWidget.h"

namespace an::ui::processes
{

class StarProfilerProcessV2UiElements
{
public:
    QWidget* verticalLayoutWidget;
    QVBoxLayout* verticalLayout;
    QGroupBox* detectedStarsGroupBox;
    QVBoxLayout* detectedStarsVerticalLayout;
    TableWidget* tableWidget;
    QHBoxLayout* horizontalLayout_7;
    QPushButton* loadProfileButton;
    QPushButton* saveProfileButton;
    QPushButton* starsFilterButton;
    QPushButton* createMaskButton;
    QSpacerItem* horizontalSpacer_5;
    QPushButton* deleteStarButton;
    QPushButton* clearProfileButton;
    QHBoxLayout* horizontalLayout_6;
    QLabel* label_10;
    QLabel* detectedStarsLabel;
    QSpacerItem* horizontalSpacer_6;
    QCheckBox* centerSelectedStar;
    QDoubleSpinBox* zoomDetectedStars;
    QGroupBox* starDetectionGroupBox;
    QVBoxLayout* starDetectionVerticalLayout;
    QGridLayout* gridLayout;
    QLabel* label;
    QLabel* label_2;
    QSpinBox* radiusValue;
    QSlider* thresholdSlider;
    QSlider* radiusSlider;
    QDoubleSpinBox* thresholdValue;
    QGroupBox* amplitudeRangeGroupBox;
    QVBoxLayout* amplitudeRangeVerticalLayout;
    QGridLayout* gridLayout1;
    QLabel* label_3;
    QDoubleSpinBox* thresholdMinimumValue;
    QSlider* thresholdMinimumSlider;
    QLabel* label_4;
    QDoubleSpinBox* thresholdMaximumValue;
    QSlider* thresholdMaximumSlider;
    QHBoxLayout* horizontalLayout_2;
    QLabel* label_5;
    QDoubleSpinBox* roundnessThresholdValue;
    QSlider* roundnessThresholdSlider;
    QGroupBox* roundnessRangeGroupBox;
    QVBoxLayout* roundnessRangeVerticalLayout;
    QHBoxLayout* horizontalLayout;
    QLabel* label_6;
    QDoubleSpinBox* roundnessThresholdMaximumValue;
    QSlider* roundnessThresholdMaximumSlider;
    QHBoxLayout* horizontalLayout_3;
    QLabel* label_7;
    QSpinBox* convergenceValue;
    QSpacerItem* horizontalSpacer_2;
    QCheckBox* relaxPSFChecks;
    QSpacerItem* horizontalSpacer;
    QHBoxLayout* horizontalLayout_4;
    QLabel* label_8;
    QComboBox* profileTypeSelector;
    QLabel* label_9;
    QDoubleSpinBox* minimumBetaValue;
    QSpacerItem* horizontalSpacer_3;
    QHBoxLayout* horizontalLayout_5;
    QPushButton* detectStarsButton;
    QCheckBox* useLightness;
    QSpacerItem* horizontalSpacer_4;
    QPushButton* resetButton;
    QDialogButtonBox* buttonBox;

    void setupUi(QWidget* StarProfilerProcessV2)
    {
        const auto& t = an::ui::tokens();
        if (StarProfilerProcessV2->objectName().isEmpty())
            StarProfilerProcessV2->setObjectName("StarProfilerProcessV2");
        StarProfilerProcessV2->resize(760, 870);
        StarProfilerProcessV2->setMinimumSize(QSize(760, 0));
        verticalLayoutWidget = StarProfilerProcessV2;
        verticalLayout = new QVBoxLayout(StarProfilerProcessV2);
        verticalLayout->setSpacing(t.spacing.md);
        verticalLayout->setObjectName("verticalLayout");
        verticalLayout->setContentsMargins(t.spacing.md, t.spacing.md, t.spacing.md, t.spacing.md);
        detectedStarsGroupBox = new QGroupBox(verticalLayoutWidget);
        detectedStarsGroupBox->setObjectName("detectedStarsGroupBox");
        detectedStarsGroupBox->setSizePolicy(QSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding));
        detectedStarsGroupBox->setMinimumHeight(180);
        detectedStarsVerticalLayout = new QVBoxLayout(detectedStarsGroupBox);
        detectedStarsVerticalLayout->setObjectName("detectedStarsVerticalLayout");
        detectedStarsVerticalLayout->setContentsMargins(t.spacing.md, t.spacing.md, t.spacing.md, t.spacing.md);
        tableWidget = new TableWidget(detectedStarsGroupBox);
        tableWidget->setObjectName("tableWidget");
        tableWidget->setEditTriggers(QAbstractItemView::NoEditTriggers);

        detectedStarsVerticalLayout->addWidget(tableWidget);

        horizontalLayout_7 = new QHBoxLayout();
        horizontalLayout_7->setObjectName("horizontalLayout_7");
        loadProfileButton = new QPushButton(detectedStarsGroupBox);
        loadProfileButton->setObjectName("loadProfileButton");

        horizontalLayout_7->addWidget(loadProfileButton);

        saveProfileButton = new QPushButton(detectedStarsGroupBox);
        saveProfileButton->setObjectName("saveProfileButton");

        horizontalLayout_7->addWidget(saveProfileButton);

        starsFilterButton = new QPushButton(detectedStarsGroupBox);
        starsFilterButton->setObjectName("starsFilterButton");

        horizontalLayout_7->addWidget(starsFilterButton);

        createMaskButton = new QPushButton(detectedStarsGroupBox);
        createMaskButton->setObjectName("createMaskButton");

        horizontalLayout_7->addWidget(createMaskButton);

        horizontalSpacer_5 = new QSpacerItem(40, 20, QSizePolicy::Policy::Expanding, QSizePolicy::Policy::Minimum);

        horizontalLayout_7->addItem(horizontalSpacer_5);

        deleteStarButton = new QPushButton(detectedStarsGroupBox);
        deleteStarButton->setObjectName("deleteStarButton");

        horizontalLayout_7->addWidget(deleteStarButton);

        clearProfileButton = new QPushButton(detectedStarsGroupBox);
        clearProfileButton->setObjectName("clearProfileButton");

        horizontalLayout_7->addWidget(clearProfileButton);

        detectedStarsVerticalLayout->addLayout(horizontalLayout_7);

        horizontalLayout_6 = new QHBoxLayout();
        horizontalLayout_6->setObjectName("horizontalLayout_6");
        label_10 = new QLabel(detectedStarsGroupBox);
        label_10->setObjectName("label_10");

        horizontalLayout_6->addWidget(label_10);

        detectedStarsLabel = new QLabel(detectedStarsGroupBox);
        detectedStarsLabel->setObjectName("detectedStarsLabel");

        horizontalLayout_6->addWidget(detectedStarsLabel);

        horizontalSpacer_6 = new QSpacerItem(40, 20, QSizePolicy::Policy::Expanding, QSizePolicy::Policy::Minimum);

        horizontalLayout_6->addItem(horizontalSpacer_6);

        centerSelectedStar = new QCheckBox(detectedStarsGroupBox);
        centerSelectedStar->setObjectName("centerSelectedStar");

        horizontalLayout_6->addWidget(centerSelectedStar);

        zoomDetectedStars = new QDoubleSpinBox(detectedStarsGroupBox);
        zoomDetectedStars->setObjectName("zoomDetectedStars");
        zoomDetectedStars->setMinimum(1.000000000000000);
        zoomDetectedStars->setMaximum(20.000000000000000);
        zoomDetectedStars->setValue(2.000000000000000);

        horizontalLayout_6->addWidget(zoomDetectedStars);

        detectedStarsVerticalLayout->addLayout(horizontalLayout_6);

        verticalLayout->addWidget(detectedStarsGroupBox, 0);

        starDetectionGroupBox = new QGroupBox(verticalLayoutWidget);
        starDetectionGroupBox->setObjectName("starDetectionGroupBox");
        starDetectionVerticalLayout = new QVBoxLayout(starDetectionGroupBox);
        starDetectionVerticalLayout->setSpacing(t.spacing.md);
        starDetectionVerticalLayout->setObjectName("starDetectionVerticalLayout");
        starDetectionVerticalLayout->setContentsMargins(t.spacing.md, t.spacing.md, t.spacing.md, t.spacing.md);
        gridLayout = new QGridLayout();
        gridLayout->setObjectName("gridLayout");
        label = new QLabel(starDetectionGroupBox);
        label->setObjectName("label");

        gridLayout->addWidget(label, 0, 0, 1, 1);

        label_2 = new QLabel(starDetectionGroupBox);
        label_2->setObjectName("label_2");

        gridLayout->addWidget(label_2, 1, 0, 1, 1);

        radiusValue = new QSpinBox(starDetectionGroupBox);
        radiusValue->setObjectName("radiusValue");
        radiusValue->setMinimum(3);
        radiusValue->setMaximum(50);
        radiusValue->setValue(5);
        radiusValue->setMinimumWidth(80);

        gridLayout->addWidget(radiusValue, 0, 1, 1, 1);

        thresholdSlider = new QSlider(starDetectionGroupBox);
        thresholdSlider->setObjectName("thresholdSlider");
        thresholdSlider->setMinimum(10);
        thresholdSlider->setMaximum(15000);
        thresholdSlider->setValue(1000);
        thresholdSlider->setOrientation(Qt::Horizontal);

        gridLayout->addWidget(thresholdSlider, 1, 2, 1, 1);

        radiusSlider = new QSlider(starDetectionGroupBox);
        radiusSlider->setObjectName("radiusSlider");
        radiusSlider->setMinimum(3);
        radiusSlider->setMaximum(50);
        radiusSlider->setValue(5);
        radiusSlider->setOrientation(Qt::Horizontal);

        gridLayout->addWidget(radiusSlider, 0, 2, 1, 1);

        thresholdValue = new QDoubleSpinBox(starDetectionGroupBox);
        thresholdValue->setObjectName("thresholdValue");
        thresholdValue->setMinimum(0.010000000000000);
        thresholdValue->setMaximum(15.000000000000000);
        thresholdValue->setSingleStep(0.050000000000000);
        thresholdValue->setValue(1.000000000000000);
        thresholdValue->setMinimumWidth(80);

        gridLayout->addWidget(thresholdValue, 1, 1, 1, 1);

        starDetectionVerticalLayout->addLayout(gridLayout);

        amplitudeRangeGroupBox = new QGroupBox(starDetectionGroupBox);
        amplitudeRangeGroupBox->setObjectName("amplitudeRangeGroupBox");
        amplitudeRangeGroupBox->setCheckable(true);
        amplitudeRangeGroupBox->setChecked(true);
        amplitudeRangeVerticalLayout = new QVBoxLayout(amplitudeRangeGroupBox);
        amplitudeRangeVerticalLayout->setObjectName("amplitudeRangeVerticalLayout");
        amplitudeRangeVerticalLayout->setSpacing(t.spacing.sm);
        amplitudeRangeVerticalLayout->setContentsMargins(t.spacing.md, t.spacing.sm, t.spacing.md, t.spacing.sm);
        gridLayout1 = new QGridLayout();
        gridLayout1->setObjectName("gridLayout1");
        gridLayout1->setHorizontalSpacing(t.spacing.sm);
        gridLayout1->setVerticalSpacing(t.spacing.sm);
        label_3 = new QLabel(amplitudeRangeGroupBox);
        label_3->setObjectName("label_3");

        gridLayout1->addWidget(label_3, 0, 0, 1, 1);

        thresholdMinimumValue = new QDoubleSpinBox(amplitudeRangeGroupBox);
        thresholdMinimumValue->setObjectName("thresholdMinimumValue");
        thresholdMinimumValue->setMinimum(0.000000000000000);
        thresholdMinimumValue->setMaximum(0.990000000000000);
        thresholdMinimumValue->setSingleStep(0.050000000000000);
        thresholdMinimumValue->setValue(0.000000000000000);
        thresholdMinimumValue->setMinimumWidth(80);

        gridLayout1->addWidget(thresholdMinimumValue, 0, 1, 1, 1);

        thresholdMinimumSlider = new QSlider(amplitudeRangeGroupBox);
        thresholdMinimumSlider->setObjectName("thresholdMinimumSlider");
        thresholdMinimumSlider->setMinimum(0);
        thresholdMinimumSlider->setMaximum(990);
        thresholdMinimumSlider->setValue(0);
        thresholdMinimumSlider->setOrientation(Qt::Horizontal);

        gridLayout1->addWidget(thresholdMinimumSlider, 0, 2, 1, 1);

        label_4 = new QLabel(amplitudeRangeGroupBox);
        label_4->setObjectName("label_4");

        gridLayout1->addWidget(label_4, 1, 0, 1, 1);

        thresholdMaximumValue = new QDoubleSpinBox(amplitudeRangeGroupBox);
        thresholdMaximumValue->setObjectName("thresholdMaximumValue");
        thresholdMaximumValue->setMinimum(0.010000000000000);
        thresholdMaximumValue->setMaximum(1.000000000000000);
        thresholdMaximumValue->setSingleStep(0.050000000000000);
        thresholdMaximumValue->setValue(1.000000000000000);
        thresholdMaximumValue->setMinimumWidth(80);

        gridLayout1->addWidget(thresholdMaximumValue, 1, 1, 1, 1);

        thresholdMaximumSlider = new QSlider(amplitudeRangeGroupBox);
        thresholdMaximumSlider->setObjectName("thresholdMaximumSlider");
        thresholdMaximumSlider->setMinimum(10);
        thresholdMaximumSlider->setMaximum(1000);
        thresholdMaximumSlider->setValue(1000);
        thresholdMaximumSlider->setOrientation(Qt::Horizontal);

        gridLayout1->addWidget(thresholdMaximumSlider, 1, 2, 1, 1);

        gridLayout1->setColumnStretch(1, 1);
        gridLayout1->setColumnStretch(2, 8);

        amplitudeRangeVerticalLayout->addLayout(gridLayout1);

        starDetectionVerticalLayout->addWidget(amplitudeRangeGroupBox);

        horizontalLayout_2 = new QHBoxLayout();
        horizontalLayout_2->setObjectName("horizontalLayout_2");
        label_5 = new QLabel(starDetectionGroupBox);
        label_5->setObjectName("label_5");

        horizontalLayout_2->addWidget(label_5);

        roundnessThresholdValue = new QDoubleSpinBox(starDetectionGroupBox);
        roundnessThresholdValue->setObjectName("roundnessThresholdValue");
        roundnessThresholdValue->setMinimum(0.000000000000000);
        roundnessThresholdValue->setMaximum(0.950000000000000);
        roundnessThresholdValue->setSingleStep(0.050000000000000);
        roundnessThresholdValue->setValue(0.500000000000000);
        roundnessThresholdValue->setMinimumWidth(80);

        horizontalLayout_2->addWidget(roundnessThresholdValue);

        roundnessThresholdSlider = new QSlider(starDetectionGroupBox);
        roundnessThresholdSlider->setObjectName("roundnessThresholdSlider");
        roundnessThresholdSlider->setMinimum(0);
        roundnessThresholdSlider->setMaximum(950);
        roundnessThresholdSlider->setValue(500);
        roundnessThresholdSlider->setOrientation(Qt::Horizontal);

        horizontalLayout_2->addWidget(roundnessThresholdSlider);

        horizontalLayout_2->setStretch(1, 1);
        horizontalLayout_2->setStretch(2, 8);

        starDetectionVerticalLayout->addLayout(horizontalLayout_2);

        roundnessRangeGroupBox = new QGroupBox(starDetectionGroupBox);
        roundnessRangeGroupBox->setObjectName("roundnessRangeGroupBox");
        roundnessRangeGroupBox->setCheckable(true);
        roundnessRangeVerticalLayout = new QVBoxLayout(roundnessRangeGroupBox);
        roundnessRangeVerticalLayout->setObjectName("roundnessRangeVerticalLayout");
        roundnessRangeVerticalLayout->setSpacing(t.spacing.sm);
        roundnessRangeVerticalLayout->setContentsMargins(t.spacing.md, t.spacing.sm, t.spacing.md, t.spacing.sm);
        horizontalLayout = new QHBoxLayout();
        horizontalLayout->setObjectName("horizontalLayout");
        label_6 = new QLabel(roundnessRangeGroupBox);
        label_6->setObjectName("label_6");

        horizontalLayout->addWidget(label_6);

        roundnessThresholdMaximumValue = new QDoubleSpinBox(roundnessRangeGroupBox);
        roundnessThresholdMaximumValue->setObjectName("roundnessThresholdMaximumValue");
        roundnessThresholdMaximumValue->setMinimum(0.010000000000000);
        roundnessThresholdMaximumValue->setMaximum(1.000000000000000);
        roundnessThresholdMaximumValue->setSingleStep(0.050000000000000);
        roundnessThresholdMaximumValue->setValue(1.000000000000000);
        roundnessThresholdMaximumValue->setMinimumWidth(80);

        horizontalLayout->addWidget(roundnessThresholdMaximumValue);

        roundnessThresholdMaximumSlider = new QSlider(roundnessRangeGroupBox);
        roundnessThresholdMaximumSlider->setObjectName("roundnessThresholdMaximumSlider");
        roundnessThresholdMaximumSlider->setMinimum(10);
        roundnessThresholdMaximumSlider->setMaximum(1000);
        roundnessThresholdMaximumSlider->setValue(1000);
        roundnessThresholdMaximumSlider->setOrientation(Qt::Horizontal);

        horizontalLayout->addWidget(roundnessThresholdMaximumSlider);

        horizontalLayout->setStretch(1, 1);
        horizontalLayout->setStretch(2, 8);

        roundnessRangeVerticalLayout->addLayout(horizontalLayout);

        starDetectionVerticalLayout->addWidget(roundnessRangeGroupBox);

        horizontalLayout_3 = new QHBoxLayout();
        horizontalLayout_3->setObjectName("horizontalLayout_3");
        label_7 = new QLabel(starDetectionGroupBox);
        label_7->setObjectName("label_7");

        horizontalLayout_3->addWidget(label_7);

        convergenceValue = new QSpinBox(starDetectionGroupBox);
        convergenceValue->setObjectName("convergenceValue");
        convergenceValue->setMinimum(1);
        convergenceValue->setMaximum(3);
        convergenceValue->setMinimumWidth(80);

        horizontalLayout_3->addWidget(convergenceValue);

        horizontalSpacer_2 = new QSpacerItem(40, 20, QSizePolicy::Policy::Expanding, QSizePolicy::Policy::Minimum);

        horizontalLayout_3->addItem(horizontalSpacer_2);

        relaxPSFChecks = new QCheckBox(starDetectionGroupBox);
        relaxPSFChecks->setObjectName("relaxPSFChecks");

        horizontalLayout_3->addWidget(relaxPSFChecks);

        horizontalSpacer = new QSpacerItem(40, 20, QSizePolicy::Policy::Expanding, QSizePolicy::Policy::Minimum);

        horizontalLayout_3->addItem(horizontalSpacer);

        horizontalLayout_3->setStretch(1, 2);
        horizontalLayout_3->setStretch(2, 2);
        horizontalLayout_3->setStretch(3, 1);
        horizontalLayout_3->setStretch(4, 10);

        starDetectionVerticalLayout->addLayout(horizontalLayout_3);

        horizontalLayout_4 = new QHBoxLayout();
        horizontalLayout_4->setObjectName("horizontalLayout_4");
        label_8 = new QLabel(starDetectionGroupBox);
        label_8->setObjectName("label_8");

        horizontalLayout_4->addWidget(label_8);

        profileTypeSelector = new QComboBox(starDetectionGroupBox);
        profileTypeSelector->setObjectName("profileTypeSelector");

        horizontalLayout_4->addWidget(profileTypeSelector);

        label_9 = new QLabel(starDetectionGroupBox);
        label_9->setObjectName("label_9");

        horizontalLayout_4->addWidget(label_9);

        minimumBetaValue = new QDoubleSpinBox(starDetectionGroupBox);
        minimumBetaValue->setObjectName("minimumBetaValue");
        minimumBetaValue->setMaximum(10.000000000000000);
        minimumBetaValue->setSingleStep(0.050000000000000);
        minimumBetaValue->setValue(1.500000000000000);
        minimumBetaValue->setMinimumWidth(80);

        horizontalLayout_4->addWidget(minimumBetaValue);

        horizontalSpacer_3 = new QSpacerItem(40, 20, QSizePolicy::Policy::Expanding, QSizePolicy::Policy::Minimum);

        horizontalLayout_4->addItem(horizontalSpacer_3);

        horizontalLayout_4->setStretch(1, 1);
        horizontalLayout_4->setStretch(4, 4);

        starDetectionVerticalLayout->addLayout(horizontalLayout_4);

        horizontalLayout_5 = new QHBoxLayout();
        horizontalLayout_5->setObjectName("horizontalLayout_5");
        horizontalLayout_5->setContentsMargins(-1, t.spacing.md, -1, -1);
        detectStarsButton = new QPushButton(starDetectionGroupBox);
        detectStarsButton->setObjectName("detectStarsButton");
        detectStarsButton->setProperty("anButtonVariant", QStringLiteral("primary"));

        horizontalLayout_5->addWidget(detectStarsButton);

        useLightness = new QCheckBox(starDetectionGroupBox);
        useLightness->setObjectName("useLightness");

        horizontalLayout_5->addWidget(useLightness);

        horizontalSpacer_4 = new QSpacerItem(40, 20, QSizePolicy::Policy::Expanding, QSizePolicy::Policy::Minimum);

        horizontalLayout_5->addItem(horizontalSpacer_4);

        resetButton = new QPushButton(starDetectionGroupBox);
        resetButton->setObjectName("resetButton");

        horizontalLayout_5->addWidget(resetButton);

        starDetectionVerticalLayout->addLayout(horizontalLayout_5);

        verticalLayout->addWidget(starDetectionGroupBox, 0);

        buttonBox = new QDialogButtonBox(verticalLayoutWidget);
        buttonBox->setObjectName("buttonBox");
        buttonBox->setStandardButtons(QDialogButtonBox::Close);

        verticalLayout->addWidget(buttonBox);

        verticalLayout->setStretch(0, 1);
        verticalLayout->setStretch(1, 0);
        verticalLayout->setStretch(2, 0);

        StarProfilerProcessV2->setMinimumSize(QSize(760, 700));

        retranslateUi(StarProfilerProcessV2);

        QMetaObject::connectSlotsByName(StarProfilerProcessV2);
    } // setupUi

    void retranslateUi(QWidget* StarProfilerProcessV2)
    {
        StarProfilerProcessV2->setWindowTitle(
            QCoreApplication::translate("StarProfilerProcessV2", "Star Profiler", nullptr));
        detectedStarsGroupBox->setTitle(
            QCoreApplication::translate("StarProfilerProcessV2", "Detected Stars", nullptr));
        loadProfileButton->setText(QCoreApplication::translate("StarProfilerProcessV2", "Load Profile", nullptr));
        saveProfileButton->setText(QCoreApplication::translate("StarProfilerProcessV2", "Save Profile", nullptr));
        starsFilterButton->setText(QCoreApplication::translate("StarProfilerProcessV2", "Stars Filter", nullptr));
        createMaskButton->setText(QCoreApplication::translate("StarProfilerProcessV2", "Create Mask", nullptr));
#if QT_CONFIG(tooltip)
        deleteStarButton->setToolTip(
            QCoreApplication::translate("StarProfilerProcessV2", "Remove selected star", nullptr));
#endif // QT_CONFIG(tooltip)
        deleteStarButton->setText(QString());
        clearProfileButton->setText(QCoreApplication::translate("StarProfilerProcessV2", "Clear", nullptr));
        label_10->setText(QCoreApplication::translate("StarProfilerProcessV2", "Detected Stars: ", nullptr));
        detectedStarsLabel->setText(QCoreApplication::translate("StarProfilerProcessV2", "0", nullptr));
        centerSelectedStar->setText(
            QCoreApplication::translate("StarProfilerProcessV2", "Center Selected Star", nullptr));
        zoomDetectedStars->setPrefix(QCoreApplication::translate("StarProfilerProcessV2", "x", nullptr));
        zoomDetectedStars->setSuffix(QString());
        starDetectionGroupBox->setTitle(
            QCoreApplication::translate("StarProfilerProcessV2", "Star Detection", nullptr));
        label->setText(QCoreApplication::translate("StarProfilerProcessV2", "Radius", nullptr));
        label_2->setText(QCoreApplication::translate("StarProfilerProcessV2", "Threshold", nullptr));
        amplitudeRangeGroupBox->setTitle(
            QCoreApplication::translate("StarProfilerProcessV2", "Amplitude Range", nullptr));
        label_3->setText(QCoreApplication::translate("StarProfilerProcessV2", "Minumum", nullptr));
        label_4->setText(QCoreApplication::translate("StarProfilerProcessV2", "Maximum", nullptr));
        label_5->setText(QCoreApplication::translate("StarProfilerProcessV2", "Roundness Threshold", nullptr));
        roundnessRangeGroupBox->setTitle(
            QCoreApplication::translate("StarProfilerProcessV2", "Roundness Range", nullptr));
        label_6->setText(QCoreApplication::translate("StarProfilerProcessV2", "Maximum", nullptr));
        label_7->setText(QCoreApplication::translate("StarProfilerProcessV2", "Convergence", nullptr));
        relaxPSFChecks->setText(QCoreApplication::translate("StarProfilerProcessV2", "Relax PSF Checks", nullptr));
        label_8->setText(QCoreApplication::translate("StarProfilerProcessV2", "Profile Type   ", nullptr));
        label_9->setText(QCoreApplication::translate("StarProfilerProcessV2", "Minimum Beta", nullptr));
        detectStarsButton->setText(QCoreApplication::translate("StarProfilerProcessV2", "Detect Stars", nullptr));
        useLightness->setText(QCoreApplication::translate("StarProfilerProcessV2", "Use Lightness", nullptr));
        resetButton->setText(QCoreApplication::translate("StarProfilerProcessV2", "Reset", nullptr));
    } // retranslateUi
};

inline StarProfilerProcessV2UiElements buildStarProfilerProcessV2Ui(QWidget* parent)
{
    StarProfilerProcessV2UiElements ui;
    ui.setupUi(parent);
    return ui;
}

} // namespace an::ui::processes
