// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include <QtCore/QVariant>
#include <QtGui/QIcon>
#include <QtWidgets/QAbstractButton>
#include <QtWidgets/QApplication>
#include <QtWidgets/QCheckBox>
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
#include "widgets/TableWidget.h"

#include "theme/AnDesignTokens.h"

namespace an::ui::processes
{

class StarProfilerProcessV1UiElements
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
    QSpacerItem* horizontalSpacer;
    QCheckBox* centerSelectedStar;
    QDoubleSpinBox* zoomDetectedStars;
    QGroupBox* starDetectionGroupBox;
    QVBoxLayout* starDetectionVerticalLayout;
    QGridLayout* gridLayout;
    QLabel* label_2;
    QSlider* roundnessToleranceSlider;
    QSlider* detectionThresholdSlider;
    QDoubleSpinBox* roundnessToleranceValue;
    QDoubleSpinBox* detectionThresholdValue;
    QSlider* minSizeSlider;
    QLabel* label_5;
    QLabel* label_6;
    QSlider* maxSizeSlider;
    QLabel* label_7;
    QSpinBox* minSizeValue;
    QSpinBox* maxSizeValue;
    QHBoxLayout* horizontalLayout_5;
    QPushButton* detectStarsButton;
    QSpacerItem* horizontalSpacer_4;
    QPushButton* resetButton;
    QDialogButtonBox* buttonBox;

    void setupUi(QWidget* StarProfilerProcessV1)
    {
        const auto& t = an::ui::tokens();

        if (StarProfilerProcessV1->objectName().isEmpty())
            StarProfilerProcessV1->setObjectName("StarProfilerProcessV1");
        StarProfilerProcessV1->resize(700, 730);
        StarProfilerProcessV1->setMinimumSize(QSize(700, 730));
        verticalLayoutWidget = StarProfilerProcessV1;
        verticalLayout = new QVBoxLayout(StarProfilerProcessV1);
        verticalLayout->setSpacing(t.spacing.md);
        verticalLayout->setObjectName("verticalLayout");
        verticalLayout->setContentsMargins(t.spacing.md, t.spacing.md, t.spacing.md, t.spacing.md);
        detectedStarsGroupBox = new QGroupBox(verticalLayoutWidget);
        detectedStarsGroupBox->setObjectName("detectedStarsGroupBox");
        detectedStarsVerticalLayout = new QVBoxLayout(detectedStarsGroupBox);
        detectedStarsVerticalLayout->setObjectName("detectedStarsVerticalLayout");
        detectedStarsVerticalLayout->setContentsMargins(t.spacing.md, t.spacing.md, t.spacing.md, t.spacing.md);
        tableWidget = new TableWidget(detectedStarsGroupBox);
        tableWidget->setObjectName("tableWidget");
        tableWidget->setMinimumHeight(90);
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

        horizontalSpacer = new QSpacerItem(40, 20, QSizePolicy::Policy::Expanding, QSizePolicy::Policy::Minimum);

        horizontalLayout_6->addItem(horizontalSpacer);

        centerSelectedStar = new QCheckBox(detectedStarsGroupBox);
        centerSelectedStar->setObjectName("centerSelectedStar");

        horizontalLayout_6->addWidget(centerSelectedStar);

        zoomDetectedStars = new QDoubleSpinBox(detectedStarsGroupBox);
        zoomDetectedStars->setObjectName("zoomDetectedStars");
        zoomDetectedStars->setMinimum(1.000000000000000);
        zoomDetectedStars->setMaximum(20.000000000000000);
        zoomDetectedStars->setValue(2.000000000000000);

        horizontalLayout_6->addWidget(zoomDetectedStars);

        horizontalLayout_6->setStretch(2, 1);

        detectedStarsVerticalLayout->addLayout(horizontalLayout_6);

        verticalLayout->addWidget(detectedStarsGroupBox);

        starDetectionGroupBox = new QGroupBox(verticalLayoutWidget);
        starDetectionGroupBox->setObjectName("starDetectionGroupBox");
        starDetectionVerticalLayout = new QVBoxLayout(starDetectionGroupBox);
        starDetectionVerticalLayout->setSpacing(t.spacing.md);
        starDetectionVerticalLayout->setObjectName("starDetectionVerticalLayout");
        starDetectionVerticalLayout->setContentsMargins(t.spacing.md, t.spacing.md, t.spacing.md, t.spacing.md);
        gridLayout = new QGridLayout();
        gridLayout->setObjectName("gridLayout");
        label_2 = new QLabel(starDetectionGroupBox);
        label_2->setObjectName("label_2");

        gridLayout->addWidget(label_2, 0, 0, 1, 1);

        roundnessToleranceSlider = new QSlider(starDetectionGroupBox);
        roundnessToleranceSlider->setObjectName("roundnessToleranceSlider");
        roundnessToleranceSlider->setMinimum(0);
        roundnessToleranceSlider->setMaximum(10000);
        roundnessToleranceSlider->setValue(2000);
        roundnessToleranceSlider->setOrientation(Qt::Horizontal);

        gridLayout->addWidget(roundnessToleranceSlider, 2, 2, 1, 1);

        detectionThresholdSlider = new QSlider(starDetectionGroupBox);
        detectionThresholdSlider->setObjectName("detectionThresholdSlider");
        detectionThresholdSlider->setMinimum(0);
        detectionThresholdSlider->setMaximum(1000);
        detectionThresholdSlider->setValue(100);
        detectionThresholdSlider->setOrientation(Qt::Horizontal);

        gridLayout->addWidget(detectionThresholdSlider, 0, 2, 1, 1);

        roundnessToleranceValue = new QDoubleSpinBox(starDetectionGroupBox);
        roundnessToleranceValue->setObjectName("roundnessToleranceValue");
        roundnessToleranceValue->setMinimum(0.000000000000000);
        roundnessToleranceValue->setMaximum(10.000000000000000);
        roundnessToleranceValue->setSingleStep(0.010000000000000);
        roundnessToleranceValue->setValue(2.000000000000000);
        roundnessToleranceValue->setMinimumWidth(80);

        gridLayout->addWidget(roundnessToleranceValue, 2, 1, 1, 1);

        detectionThresholdValue = new QDoubleSpinBox(starDetectionGroupBox);
        detectionThresholdValue->setObjectName("detectionThresholdValue");
        detectionThresholdValue->setMinimum(0.010000000000000);
        detectionThresholdValue->setMaximum(1.000000000000000);
        detectionThresholdValue->setSingleStep(0.010000000000000);
        detectionThresholdValue->setValue(0.100000000000000);
        detectionThresholdValue->setMinimumWidth(80);

        gridLayout->addWidget(detectionThresholdValue, 0, 1, 1, 1);

        minSizeSlider = new QSlider(starDetectionGroupBox);
        minSizeSlider->setObjectName("minSizeSlider");
        minSizeSlider->setMinimum(0);
        minSizeSlider->setMaximum(99);
        minSizeSlider->setValue(2);
        minSizeSlider->setOrientation(Qt::Horizontal);

        gridLayout->addWidget(minSizeSlider, 3, 2, 1, 1);

        label_5 = new QLabel(starDetectionGroupBox);
        label_5->setObjectName("label_5");

        gridLayout->addWidget(label_5, 2, 0, 1, 1);

        label_6 = new QLabel(starDetectionGroupBox);
        label_6->setObjectName("label_6");

        gridLayout->addWidget(label_6, 3, 0, 1, 1);

        maxSizeSlider = new QSlider(starDetectionGroupBox);
        maxSizeSlider->setObjectName("maxSizeSlider");
        maxSizeSlider->setMinimum(0);
        maxSizeSlider->setMaximum(99);
        maxSizeSlider->setValue(25);
        maxSizeSlider->setOrientation(Qt::Horizontal);

        gridLayout->addWidget(maxSizeSlider, 6, 2, 1, 1);

        label_7 = new QLabel(starDetectionGroupBox);
        label_7->setObjectName("label_7");

        gridLayout->addWidget(label_7, 6, 0, 1, 1);

        minSizeValue = new QSpinBox(starDetectionGroupBox);
        minSizeValue->setObjectName("minSizeValue");
        minSizeValue->setValue(2);
        minSizeValue->setMinimumWidth(80);

        gridLayout->addWidget(minSizeValue, 3, 1, 1, 1);

        maxSizeValue = new QSpinBox(starDetectionGroupBox);
        maxSizeValue->setObjectName("maxSizeValue");
        maxSizeValue->setMaximum(99);
        maxSizeValue->setValue(25);
        maxSizeValue->setMinimumWidth(80);

        gridLayout->addWidget(maxSizeValue, 6, 1, 1, 1);

        starDetectionVerticalLayout->addLayout(gridLayout);

        horizontalLayout_5 = new QHBoxLayout();
        horizontalLayout_5->setObjectName("horizontalLayout_5");
        horizontalLayout_5->setContentsMargins(-1, t.spacing.md, -1, -1);
        detectStarsButton = new QPushButton(starDetectionGroupBox);
        detectStarsButton->setObjectName("detectStarsButton");
        detectStarsButton->setProperty("anButtonVariant", QStringLiteral("primary"));

        horizontalLayout_5->addWidget(detectStarsButton);

        horizontalSpacer_4 = new QSpacerItem(40, 20, QSizePolicy::Policy::Expanding, QSizePolicy::Policy::Minimum);

        horizontalLayout_5->addItem(horizontalSpacer_4);

        resetButton = new QPushButton(starDetectionGroupBox);
        resetButton->setObjectName("resetButton");

        horizontalLayout_5->addWidget(resetButton);

        starDetectionVerticalLayout->addLayout(horizontalLayout_5);

        verticalLayout->addWidget(starDetectionGroupBox);

        buttonBox = new QDialogButtonBox(verticalLayoutWidget);
        buttonBox->setObjectName("buttonBox");
        buttonBox->setStandardButtons(QDialogButtonBox::Close);

        verticalLayout->addWidget(buttonBox);

        retranslateUi(StarProfilerProcessV1);

        QMetaObject::connectSlotsByName(StarProfilerProcessV1);
    } // setupUi

    void retranslateUi(QWidget* StarProfilerProcessV1)
    {
        StarProfilerProcessV1->setWindowTitle(
            QCoreApplication::translate("StarProfilerProcessV1", "Star Profiler V1", nullptr));
        detectedStarsGroupBox->setTitle(
            QCoreApplication::translate("StarProfilerProcessV1", "Detected Stars", nullptr));
        loadProfileButton->setText(QCoreApplication::translate("StarProfilerProcessV1", "Load Profile", nullptr));
        saveProfileButton->setText(QCoreApplication::translate("StarProfilerProcessV1", "Save Profile", nullptr));
        starsFilterButton->setText(QCoreApplication::translate("StarProfilerProcessV1", "Stars Filter", nullptr));
        createMaskButton->setText(QCoreApplication::translate("StarProfilerProcessV1", "Create Mask", nullptr));
#if QT_CONFIG(tooltip)
        deleteStarButton->setToolTip(
            QCoreApplication::translate("StarProfilerProcessV1", "Remove selected star", nullptr));
#endif // QT_CONFIG(tooltip)
        deleteStarButton->setText(QString());
        clearProfileButton->setText(QCoreApplication::translate("StarProfilerProcessV1", "Clear", nullptr));
        label_10->setText(QCoreApplication::translate("StarProfilerProcessV1", "Detected Stars: ", nullptr));
        detectedStarsLabel->setText(QCoreApplication::translate("StarProfilerProcessV1", "0", nullptr));
        centerSelectedStar->setText(
            QCoreApplication::translate("StarProfilerProcessV1", "Center Selected Star", nullptr));
        zoomDetectedStars->setPrefix(QCoreApplication::translate("StarProfilerProcessV1", "x", nullptr));
        zoomDetectedStars->setSuffix(QString());
        starDetectionGroupBox->setTitle(
            QCoreApplication::translate("StarProfilerProcessV1", "Star Detection", nullptr));
        label_2->setText(QCoreApplication::translate("StarProfilerProcessV1", "Detection Threshold", nullptr));
        label_5->setText(QCoreApplication::translate("StarProfilerProcessV1", "Roundness Tolerance", nullptr));
        label_6->setText(QCoreApplication::translate("StarProfilerProcessV1", "Min Size", nullptr));
        label_7->setText(QCoreApplication::translate("StarProfilerProcessV1", "Max Size", nullptr));
        detectStarsButton->setText(QCoreApplication::translate("StarProfilerProcessV1", "Detect Stars", nullptr));
        resetButton->setText(QCoreApplication::translate("StarProfilerProcessV1", "Reset", nullptr));
    } // retranslateUi
};

inline StarProfilerProcessV1UiElements buildStarProfilerProcessV1Ui(QWidget* parent)
{
    StarProfilerProcessV1UiElements ui;
    ui.setupUi(parent);
    return ui;
}

} // namespace an::ui::processes
