// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include <algorithm>
#include <cmath>

#include <QtCore/QtCore>
#include <QtGui/QtGui>
#include <QtWidgets/QtWidgets>
#include "components/AnCollapsibleSection.h"
#include "processes/ImageIntegration/core/RegistrationSettingsUtils.h"
#include "theme/AnDesignTokens.h"

namespace an::ui::imageintegration
{

inline constexpr int kRegistrationSettingsDialogMinimumWidth = 620;

inline int registrationSettingsDialogWidthHint(QWidget* widget)
{
    if (!widget)
    {
        return 0;
    }

    if (QLayout* layout = widget->layout())
    {
        layout->invalidate();
        layout->activate();
    }

    widget->updateGeometry();
    return (std::max)(widget->minimumSizeHint().width(), widget->sizeHint().width());
}

inline void applyRegistrationSettingsDialogScrollAwareMinimumWidth(QDialog* dialog, QVBoxLayout* verticalLayoutMain,
                                                                   QVBoxLayout* verticalLayoutSettingsContent,
                                                                   QScrollArea* settingsScrollArea,
                                                                   an::ui::AnCollapsibleSection* registrationGroupBox,
                                                                   an::ui::AnCollapsibleSection* alignmentGroupBox,
                                                                   QPushButton* advancedSettingsButton)
{
    if (!dialog)
    {
        return;
    }

    dialog->ensurePolished();
    if (verticalLayoutMain)
    {
        verticalLayoutMain->invalidate();
        verticalLayoutMain->activate();
    }
    if (verticalLayoutSettingsContent)
    {
        verticalLayoutSettingsContent->invalidate();
        verticalLayoutSettingsContent->activate();
    }

    int scrollBarWidth = 0;
    if (settingsScrollArea)
    {
        scrollBarWidth = (std::max)(scrollBarWidth, settingsScrollArea->style()->pixelMetric(
                                                        QStyle::PM_ScrollBarExtent, nullptr, settingsScrollArea));
        if (settingsScrollArea->verticalScrollBar())
        {
            scrollBarWidth = (std::max)(scrollBarWidth, settingsScrollArea->verticalScrollBar()->sizeHint().width());
        }
    }

    int requiredContentWidth = 0;
    QWidget* scrollContentWidthSources[] = {
        settingsScrollArea ? settingsScrollArea->widget() : nullptr,
        registrationGroupBox,
        registrationGroupBox ? registrationGroupBox->contentWidget() : nullptr,
        alignmentGroupBox,
        alignmentGroupBox ? alignmentGroupBox->contentWidget() : nullptr,
        advancedSettingsButton,
    };
    for (QWidget* widget : scrollContentWidthSources)
    {
        requiredContentWidth = (std::max)(requiredContentWidth, registrationSettingsDialogWidthHint(widget));
    }

    const QMargins mainMargins = verticalLayoutMain ? verticalLayoutMain->contentsMargins() : QMargins{};
    const int scrollFrameWidth = settingsScrollArea ? settingsScrollArea->frameWidth() * 2 : 0;
    const int scrollAwareMinimumWidth =
        mainMargins.left() + mainMargins.right() + scrollFrameWidth + scrollBarWidth + requiredContentWidth;
    const int minimumDialogWidth = (std::max)(kRegistrationSettingsDialogMinimumWidth,
                                              (std::max)(dialog->minimumSizeHint().width(), scrollAwareMinimumWidth));
    dialog->setMinimumWidth(minimumDialogWidth);
    dialog->resize((std::max)(dialog->width(), minimumDialogWidth), dialog->height());
}

class RegistrationSettingsDialogUiElements
{
public:
    QVBoxLayout* verticalLayout_main = nullptr;
    QLabel* titleLabel = nullptr;
    QLabel* groupLabel = nullptr;
    QFrame* separator1 = nullptr;
    QScrollArea* settingsScrollArea = nullptr;
    QWidget* settingsContentWidget = nullptr;
    QVBoxLayout* verticalLayout_settingsContent = nullptr;
    an::ui::AnCollapsibleSection* registrationGroupBox = nullptr;
    QVBoxLayout* verticalLayout_registration = nullptr;
    QHBoxLayout* horizontalLayout_minStarPairs = nullptr;
    QLabel* label_minStarPairs = nullptr;
    QSpinBox* minStarPairsSpinBox = nullptr;
    QHBoxLayout* horizontalLayout_maxStarPairs = nullptr;
    QLabel* label_maxStarPairs = nullptr;
    QSpinBox* maxStarPairsSpinBox = nullptr;
    QHBoxLayout* horizontalLayout_referenceImage = nullptr;
    QLabel* label_referenceImage = nullptr;
    QComboBox* referenceImageComboBox = nullptr;
    QPushButton* autoAssignButton = nullptr;
    QHBoxLayout* horizontalLayout_registrationMethod = nullptr;
    QLabel* label_registrationMethod = nullptr;
    QComboBox* registrationMethodComboBox = nullptr;
    QHBoxLayout* horizontalLayout_transformation = nullptr;
    QLabel* label_transformation = nullptr;
    QComboBox* transformationComboBox = nullptr;
    QLabel* transformSummaryLabel = nullptr;
    QHBoxLayout* horizontalLayout_manualPoints = nullptr;
    QLabel* manualPointsSummaryLabel = nullptr;
    QPushButton* editManualPointsButton = nullptr;
    an::ui::AnCollapsibleSection* alignmentGroupBox = nullptr;
    QVBoxLayout* verticalLayout_alignment = nullptr;
    QRadioButton* alignModeStandard = nullptr;
    QRadioButton* alignModeMosaic = nullptr;
    QRadioButton* alignModeIntersection = nullptr;
    QHBoxLayout* horizontalLayout_interpolation = nullptr;
    QLabel* label_interpolation = nullptr;
    QComboBox* interpolationComboBox = nullptr;
    QPushButton* advancedSettingsButton = nullptr;
    QWidget* advancedSettingsPanel = nullptr;
    QVBoxLayout* verticalLayout_advancedPanel = nullptr;
    an::ui::AnCollapsibleSection* starDetectionGroupBox = nullptr;
    QVBoxLayout* verticalLayout_starDetection = nullptr;
    QGridLayout* gridLayout_radiusThreshold = nullptr;
    QLabel* label_radius = nullptr;
    QSpinBox* radiusSpinBox = nullptr;
    QSlider* radiusSlider = nullptr;
    QLabel* label_threshold = nullptr;
    QDoubleSpinBox* thresholdSpinBox = nullptr;
    QSlider* thresholdSlider = nullptr;
    an::ui::AnCollapsibleSection* amplitudeRangeGroupBox = nullptr;
    QCheckBox* chkEnableAmplitudeRange = nullptr;
    QVBoxLayout* verticalLayout_amplitudeRange = nullptr;
    QGridLayout* gridLayout_amplitudeRange = nullptr;
    QLabel* label_thresholdMinimum = nullptr;
    QDoubleSpinBox* thresholdMinimumSpinBox = nullptr;
    QSlider* thresholdMinimumSlider = nullptr;
    QLabel* label_thresholdMaximum = nullptr;
    QDoubleSpinBox* thresholdMaximumSpinBox = nullptr;
    QSlider* thresholdMaximumSlider = nullptr;
    QHBoxLayout* horizontalLayout_roundness = nullptr;
    QLabel* label_roundnessThreshold = nullptr;
    QDoubleSpinBox* roundnessThresholdSpinBox = nullptr;
    QSlider* roundnessThresholdSlider = nullptr;
    an::ui::AnCollapsibleSection* roundnessRangeGroupBox = nullptr;
    QCheckBox* chkEnableRoundnessRange = nullptr;
    QVBoxLayout* verticalLayout_roundnessRange = nullptr;
    QHBoxLayout* horizontalLayout_roundnessMaximum = nullptr;
    QLabel* label_roundnessMaximum = nullptr;
    QDoubleSpinBox* roundnessThresholdMaximumSpinBox = nullptr;
    QSlider* roundnessThresholdMaximumSlider = nullptr;
    QHBoxLayout* horizontalLayout_convergence = nullptr;
    QLabel* label_convergence = nullptr;
    QSpinBox* convergenceSpinBox = nullptr;
    QCheckBox* relaxPSFChecksCheckBox = nullptr;
    QHBoxLayout* horizontalLayout_profileType = nullptr;
    QLabel* label_profileType = nullptr;
    QComboBox* profileTypeComboBox = nullptr;
    QLabel* label_minimumBeta = nullptr;
    QDoubleSpinBox* minimumBetaSpinBox = nullptr;
    an::ui::AnCollapsibleSection* miscGroupBox = nullptr;
    QVBoxLayout* verticalLayout_miscellaneous = nullptr;
    QHBoxLayout* horizontalLayout_cfaAndDebayer = nullptr;
    QLabel* label_cfaPattern = nullptr;
    QComboBox* cfaPatternComboBox = nullptr;
    QLabel* label_debayerMethod = nullptr;
    QComboBox* debayerMethodComboBox = nullptr;
    QFrame* separator2 = nullptr;
    QHBoxLayout* horizontalLayout_buttons = nullptr;
    QPushButton* resetButton = nullptr;
    QPushButton* applyToAllButton = nullptr;
    QPushButton* cancelButton = nullptr;
    QPushButton* okButton = nullptr;

    void setupUi(QDialog* RegistrationSettingsDialog)
    {
        const auto& t = an::ui::tokens();
        RegistrationSettingsDialog->setObjectName("RegistrationSettingsDialog");
        RegistrationSettingsDialog->resize(550, 570);
        verticalLayout_main = new QVBoxLayout(RegistrationSettingsDialog);
        verticalLayout_main->setObjectName("verticalLayout_main");
        verticalLayout_main->setContentsMargins(t.spacing.lg, t.spacing.lg, t.spacing.lg, t.spacing.lg);
        verticalLayout_main->setSpacing(t.spacing.lg);
        verticalLayout_main->setObjectName("verticalLayout_main");
        titleLabel = new QLabel(RegistrationSettingsDialog);
        titleLabel->setFont(t.typography.heading3);
        titleLabel->setObjectName("titleLabel");
        titleLabel->setTextFormat(Qt::PlainText);
        verticalLayout_main->addWidget(titleLabel);
        groupLabel = new QLabel(RegistrationSettingsDialog);
        groupLabel->setProperty("anStatusTone", QStringLiteral("secondary"));
        groupLabel->setText("");
        groupLabel->setWordWrap(true);
        groupLabel->setObjectName("groupLabel");
        verticalLayout_main->addWidget(groupLabel);
        separator1 = new QFrame(RegistrationSettingsDialog);
        separator1->setFrameShape(QFrame::Shape::HLine);
        separator1->setFrameShadow(QFrame::Shadow::Sunken);
        separator1->setObjectName("separator1");
        verticalLayout_main->addWidget(separator1);

        settingsScrollArea = new QScrollArea(RegistrationSettingsDialog);
        settingsScrollArea->setObjectName("registrationSettingsScrollArea");
        settingsScrollArea->setFrameShape(QFrame::NoFrame);
        settingsScrollArea->setWidgetResizable(true);
        settingsScrollArea->setSizePolicy(QSizePolicy::Policy::Expanding, QSizePolicy::Policy::Expanding);
        settingsScrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        settingsContentWidget = new QWidget(settingsScrollArea);
        settingsContentWidget->setObjectName("registrationSettingsScrollContent");
        verticalLayout_settingsContent = new QVBoxLayout(settingsContentWidget);
        verticalLayout_settingsContent->setObjectName("verticalLayout_settingsContent");
        verticalLayout_settingsContent->setContentsMargins(0, 0, t.spacing.xs, 0);
        verticalLayout_settingsContent->setSpacing(t.spacing.lg);
        settingsScrollArea->setWidget(settingsContentWidget);
        verticalLayout_main->addWidget(settingsScrollArea, 1);

        registrationGroupBox = new an::ui::AnCollapsibleSection(QString(), settingsContentWidget);
        registrationGroupBox->setObjectName("registrationGroupBox");
        verticalLayout_registration = new QVBoxLayout();
        registrationGroupBox->setContentLayout(verticalLayout_registration);
        verticalLayout_registration->setObjectName("verticalLayout_registration");
        horizontalLayout_minStarPairs = new QHBoxLayout();
        horizontalLayout_minStarPairs->setObjectName("horizontalLayout_minStarPairs");
        label_minStarPairs = new QLabel(registrationGroupBox->contentWidget());
        label_minStarPairs->setMinimumSize(QSize(150, 0));
        label_minStarPairs->setObjectName("label_minStarPairs");
        horizontalLayout_minStarPairs->addWidget(label_minStarPairs);
        minStarPairsSpinBox = new QSpinBox(registrationGroupBox->contentWidget());
        minStarPairsSpinBox->setMinimumSize(QSize(150, 0));
        minStarPairsSpinBox->setMinimum(5);
        minStarPairsSpinBox->setMaximum(10000);
        minStarPairsSpinBox->setProperty("value", 10);
        minStarPairsSpinBox->setObjectName("minStarPairsSpinBox");
        horizontalLayout_minStarPairs->addWidget(minStarPairsSpinBox);
        auto* spacerItem = new QSpacerItem(40, 20, QSizePolicy::Policy::Expanding, QSizePolicy::Policy::Minimum);
        horizontalLayout_minStarPairs->addItem(spacerItem);
        verticalLayout_registration->addLayout(horizontalLayout_minStarPairs);
        horizontalLayout_maxStarPairs = new QHBoxLayout();
        horizontalLayout_maxStarPairs->setObjectName("horizontalLayout_maxStarPairs");
        label_maxStarPairs = new QLabel(registrationGroupBox->contentWidget());
        label_maxStarPairs->setMinimumSize(QSize(150, 0));
        label_maxStarPairs->setObjectName("label_maxStarPairs");
        horizontalLayout_maxStarPairs->addWidget(label_maxStarPairs);
        maxStarPairsSpinBox = new QSpinBox(registrationGroupBox->contentWidget());
        maxStarPairsSpinBox->setMinimumSize(QSize(150, 0));
        maxStarPairsSpinBox->setMinimum(10);
        maxStarPairsSpinBox->setMaximum(20000);
        maxStarPairsSpinBox->setProperty("value", 2000);
        maxStarPairsSpinBox->setObjectName("maxStarPairsSpinBox");
        horizontalLayout_maxStarPairs->addWidget(maxStarPairsSpinBox);
        auto* spacerItem1 = new QSpacerItem(40, 20, QSizePolicy::Policy::Expanding, QSizePolicy::Policy::Minimum);
        horizontalLayout_maxStarPairs->addItem(spacerItem1);
        verticalLayout_registration->addLayout(horizontalLayout_maxStarPairs);
        horizontalLayout_referenceImage = new QHBoxLayout();
        horizontalLayout_referenceImage->setObjectName("horizontalLayout_referenceImage");
        horizontalLayout_referenceImage->setSpacing(t.spacing.sm);
        label_referenceImage = new QLabel(registrationGroupBox->contentWidget());
        label_referenceImage->setMinimumSize(QSize(150, 0));
        label_referenceImage->setMaximumSize(QSize(150, 16777215));
        label_referenceImage->setSizePolicy(QSizePolicy::Policy::Fixed, QSizePolicy::Policy::Preferred);
        label_referenceImage->setObjectName("label_referenceImage");
        horizontalLayout_referenceImage->addWidget(label_referenceImage);
        referenceImageComboBox = new QComboBox(registrationGroupBox->contentWidget());
        referenceImageComboBox->setMinimumSize(QSize(0, 0));
        referenceImageComboBox->setSizePolicy(QSizePolicy::Policy::Expanding, QSizePolicy::Policy::Fixed);
        referenceImageComboBox->setMinimumContentsLength(20);
        referenceImageComboBox->setSizeAdjustPolicy(QComboBox::SizeAdjustPolicy::AdjustToMinimumContentsLengthWithIcon);
        referenceImageComboBox->setEditable(true);
        referenceImageComboBox->setInsertPolicy(QComboBox::InsertPolicy::NoInsert);
        referenceImageComboBox->setObjectName("referenceImageComboBox");
        horizontalLayout_referenceImage->addWidget(referenceImageComboBox);
        autoAssignButton = new QPushButton(registrationGroupBox->contentWidget());
        autoAssignButton->setSizePolicy(QSizePolicy::Policy::Preferred, QSizePolicy::Policy::Fixed);
        autoAssignButton->setObjectName("autoAssignButton");
        horizontalLayout_referenceImage->addWidget(autoAssignButton);
        horizontalLayout_referenceImage->setStretch(1, 1);
        verticalLayout_registration->addLayout(horizontalLayout_referenceImage);
        horizontalLayout_registrationMethod = new QHBoxLayout();
        horizontalLayout_registrationMethod->setObjectName("horizontalLayout_registrationMethod");
        label_registrationMethod = new QLabel(registrationGroupBox->contentWidget());
        label_registrationMethod->setMinimumSize(QSize(150, 0));
        label_registrationMethod->setObjectName("label_registrationMethod");
        horizontalLayout_registrationMethod->addWidget(label_registrationMethod);
        registrationMethodComboBox = new QComboBox(registrationGroupBox->contentWidget());
        registrationMethodComboBox->setMinimumSize(QSize(150, 0));
        registrationMethodComboBox->setObjectName("registrationMethodComboBox");
        horizontalLayout_registrationMethod->addWidget(registrationMethodComboBox);
        auto* spacerItemMethod = new QSpacerItem(40, 20, QSizePolicy::Policy::Expanding, QSizePolicy::Policy::Minimum);
        horizontalLayout_registrationMethod->addItem(spacerItemMethod);
        verticalLayout_registration->addLayout(horizontalLayout_registrationMethod);
        horizontalLayout_transformation = new QHBoxLayout();
        horizontalLayout_transformation->setObjectName("horizontalLayout_transformation");
        label_transformation = new QLabel(registrationGroupBox->contentWidget());
        label_transformation->setMinimumSize(QSize(150, 0));
        label_transformation->setObjectName("label_transformation");
        horizontalLayout_transformation->addWidget(label_transformation);
        transformationComboBox = new QComboBox(registrationGroupBox->contentWidget());
        transformationComboBox->setMinimumSize(QSize(150, 0));
        transformationComboBox->setObjectName("transformationComboBox");
        horizontalLayout_transformation->addWidget(transformationComboBox);
        auto* spacerItem3 = new QSpacerItem(40, 20, QSizePolicy::Policy::Expanding, QSizePolicy::Policy::Minimum);
        horizontalLayout_transformation->addItem(spacerItem3);
        verticalLayout_registration->addLayout(horizontalLayout_transformation);
        transformSummaryLabel = new QLabel(registrationGroupBox->contentWidget());
        transformSummaryLabel->setObjectName("transformSummaryLabel");
        transformSummaryLabel->setWordWrap(true);
        transformSummaryLabel->setProperty("anStatusTone", QStringLiteral("secondary"));
        verticalLayout_registration->addWidget(transformSummaryLabel);
        horizontalLayout_manualPoints = new QHBoxLayout();
        horizontalLayout_manualPoints->setObjectName("horizontalLayout_manualPoints");
        manualPointsSummaryLabel = new QLabel(registrationGroupBox->contentWidget());
        manualPointsSummaryLabel->setObjectName("manualPointsSummaryLabel");
        manualPointsSummaryLabel->setWordWrap(true);
        manualPointsSummaryLabel->setProperty("anStatusTone", QStringLiteral("secondary"));
        horizontalLayout_manualPoints->addWidget(manualPointsSummaryLabel, 1);
        editManualPointsButton = new QPushButton(registrationGroupBox->contentWidget());
        editManualPointsButton->setObjectName("editManualPointsButton");
        horizontalLayout_manualPoints->addWidget(editManualPointsButton);
        verticalLayout_registration->addLayout(horizontalLayout_manualPoints);
        verticalLayout_settingsContent->addWidget(registrationGroupBox);
        alignmentGroupBox = new an::ui::AnCollapsibleSection(QString(), settingsContentWidget);
        alignmentGroupBox->setObjectName("alignmentGroupBox");
        verticalLayout_alignment = new QVBoxLayout();
        alignmentGroupBox->setContentLayout(verticalLayout_alignment);
        verticalLayout_alignment->setObjectName("verticalLayout_alignment");
        alignModeStandard = new QRadioButton(alignmentGroupBox->contentWidget());
        alignModeStandard->setChecked(true);
        alignModeStandard->setObjectName("alignModeStandard");
        verticalLayout_alignment->addWidget(alignModeStandard);
        alignModeMosaic = new QRadioButton(alignmentGroupBox->contentWidget());
        alignModeMosaic->setObjectName("alignModeMosaic");
        verticalLayout_alignment->addWidget(alignModeMosaic);
        alignModeIntersection = new QRadioButton(alignmentGroupBox->contentWidget());
        alignModeIntersection->setObjectName("alignModeIntersection");
        verticalLayout_alignment->addWidget(alignModeIntersection);
        horizontalLayout_interpolation = new QHBoxLayout();
        horizontalLayout_interpolation->setObjectName("horizontalLayout_interpolation");
        label_interpolation = new QLabel(alignmentGroupBox->contentWidget());
        label_interpolation->setMinimumSize(QSize(150, 0));
        label_interpolation->setObjectName("label_interpolation");
        horizontalLayout_interpolation->addWidget(label_interpolation);
        interpolationComboBox = new QComboBox(alignmentGroupBox->contentWidget());
        interpolationComboBox->setMinimumSize(QSize(200, 0));
        interpolationComboBox->setObjectName("interpolationComboBox");
        horizontalLayout_interpolation->addWidget(interpolationComboBox);
        auto* spacerItem4 = new QSpacerItem(40, 20, QSizePolicy::Policy::Expanding, QSizePolicy::Policy::Minimum);
        horizontalLayout_interpolation->addItem(spacerItem4);
        verticalLayout_alignment->addLayout(horizontalLayout_interpolation);
        verticalLayout_settingsContent->addWidget(alignmentGroupBox);
        advancedSettingsButton = new QPushButton(settingsContentWidget);
        advancedSettingsButton->setObjectName("advancedSettingsButton");
        verticalLayout_settingsContent->addWidget(advancedSettingsButton);
        advancedSettingsPanel = new QWidget(settingsContentWidget);
        advancedSettingsPanel->setVisible(false);
        advancedSettingsPanel->setObjectName("advancedSettingsPanel");
        verticalLayout_advancedPanel = new QVBoxLayout(advancedSettingsPanel);
        verticalLayout_advancedPanel->setObjectName("verticalLayout_advancedPanel");
        verticalLayout_advancedPanel->setContentsMargins(0, t.spacing.md, 0, 0);
        verticalLayout_advancedPanel->setSpacing(t.spacing.md);
        verticalLayout_advancedPanel->setObjectName("verticalLayout_advancedPanel");
        starDetectionGroupBox = new an::ui::AnCollapsibleSection(QString(), advancedSettingsPanel);
        starDetectionGroupBox->setObjectName("starDetectionGroupBox");
        verticalLayout_starDetection = new QVBoxLayout();
        starDetectionGroupBox->setContentLayout(verticalLayout_starDetection);
        verticalLayout_starDetection->setContentsMargins(t.spacing.md, t.spacing.md, t.spacing.md, t.spacing.md);
        verticalLayout_starDetection->setObjectName("verticalLayout_starDetection");
        gridLayout_radiusThreshold = new QGridLayout();
        gridLayout_radiusThreshold->setObjectName("gridLayout_radiusThreshold");
        label_radius = new QLabel(starDetectionGroupBox->contentWidget());
        label_radius->setObjectName("label_radius");
        gridLayout_radiusThreshold->addWidget(label_radius, 0, 0, 1, 1);
        // The detector's ranges, as the Star Profiler offers them: a search radius up to its largest, and a roundness
        // threshold and a Moffat exponent from its smallest positive values.
        using ImageIntegration::RegistrationSettingsUtils::kLargestStarSearchRadius;
        using ImageIntegration::RegistrationSettingsUtils::kSmallestMoffatBeta;
        using ImageIntegration::RegistrationSettingsUtils::kSmallestStarRoundness;
        radiusSpinBox = new QSpinBox(starDetectionGroupBox->contentWidget());
        radiusSpinBox->setMinimum(3);
        radiusSpinBox->setMaximum(kLargestStarSearchRadius);
        radiusSpinBox->setProperty("value", 5);
        radiusSpinBox->setObjectName("radiusSpinBox");
        gridLayout_radiusThreshold->addWidget(radiusSpinBox, 0, 1, 1, 1);
        radiusSlider = new QSlider(starDetectionGroupBox->contentWidget());
        radiusSlider->setMinimum(3);
        radiusSlider->setMaximum(kLargestStarSearchRadius);
        radiusSlider->setProperty("value", 5);
        radiusSlider->setOrientation(Qt::Orientation::Horizontal);
        radiusSlider->setObjectName("radiusSlider");
        gridLayout_radiusThreshold->addWidget(radiusSlider, 0, 2, 1, 1);
        label_threshold = new QLabel(starDetectionGroupBox->contentWidget());
        label_threshold->setObjectName("label_threshold");
        gridLayout_radiusThreshold->addWidget(label_threshold, 1, 0, 1, 1);
        thresholdSpinBox = new QDoubleSpinBox(starDetectionGroupBox->contentWidget());
        thresholdSpinBox->setMinimum(0.01);
        thresholdSpinBox->setMaximum(15.0);
        thresholdSpinBox->setSingleStep(0.05);
        thresholdSpinBox->setProperty("value", 1.0);
        thresholdSpinBox->setObjectName("thresholdSpinBox");
        gridLayout_radiusThreshold->addWidget(thresholdSpinBox, 1, 1, 1, 1);
        thresholdSlider = new QSlider(starDetectionGroupBox->contentWidget());
        thresholdSlider->setMinimum(10);
        thresholdSlider->setMaximum(15000);
        thresholdSlider->setProperty("value", 1000);
        thresholdSlider->setOrientation(Qt::Orientation::Horizontal);
        thresholdSlider->setObjectName("thresholdSlider");
        gridLayout_radiusThreshold->addWidget(thresholdSlider, 1, 2, 1, 1);
        verticalLayout_starDetection->addLayout(gridLayout_radiusThreshold);
        amplitudeRangeGroupBox = new an::ui::AnCollapsibleSection(QString(), starDetectionGroupBox->contentWidget());

        amplitudeRangeGroupBox->setObjectName("amplitudeRangeGroupBox");
        verticalLayout_amplitudeRange = new QVBoxLayout();
        amplitudeRangeGroupBox->setContentLayout(verticalLayout_amplitudeRange);
        verticalLayout_amplitudeRange->setContentsMargins(t.spacing.sm, t.spacing.sm, t.spacing.sm, t.spacing.sm);
        verticalLayout_amplitudeRange->setObjectName("verticalLayout_amplitudeRange");

        chkEnableAmplitudeRange = new QCheckBox(amplitudeRangeGroupBox->contentWidget());
        chkEnableAmplitudeRange->setObjectName("chkEnableAmplitudeRange");
        verticalLayout_amplitudeRange->addWidget(chkEnableAmplitudeRange);

        gridLayout_amplitudeRange = new QGridLayout();
        gridLayout_amplitudeRange->setObjectName("gridLayout_amplitudeRange");
        label_thresholdMinimum = new QLabel(amplitudeRangeGroupBox->contentWidget());
        label_thresholdMinimum->setObjectName("label_thresholdMinimum");
        gridLayout_amplitudeRange->addWidget(label_thresholdMinimum, 0, 0, 1, 1);
        thresholdMinimumSpinBox = new QDoubleSpinBox(amplitudeRangeGroupBox->contentWidget());
        thresholdMinimumSpinBox->setMinimumSize(QSize(60, 0));
        thresholdMinimumSpinBox->setMaximumSize(QSize(60, 16777215));
        thresholdMinimumSpinBox->setMinimum(0.0);
        thresholdMinimumSpinBox->setMaximum(0.99);
        thresholdMinimumSpinBox->setSingleStep(0.05);
        thresholdMinimumSpinBox->setProperty("value", 0.0);
        thresholdMinimumSpinBox->setObjectName("thresholdMinimumSpinBox");
        gridLayout_amplitudeRange->addWidget(thresholdMinimumSpinBox, 0, 1, 1, 1);
        thresholdMinimumSlider = new QSlider(amplitudeRangeGroupBox->contentWidget());
        thresholdMinimumSlider->setMinimum(0);
        thresholdMinimumSlider->setMaximum(990);
        thresholdMinimumSlider->setProperty("value", 0);
        thresholdMinimumSlider->setOrientation(Qt::Orientation::Horizontal);
        thresholdMinimumSlider->setObjectName("thresholdMinimumSlider");
        gridLayout_amplitudeRange->addWidget(thresholdMinimumSlider, 0, 2, 1, 1);
        label_thresholdMaximum = new QLabel(amplitudeRangeGroupBox->contentWidget());
        label_thresholdMaximum->setObjectName("label_thresholdMaximum");
        gridLayout_amplitudeRange->addWidget(label_thresholdMaximum, 1, 0, 1, 1);
        thresholdMaximumSpinBox = new QDoubleSpinBox(amplitudeRangeGroupBox->contentWidget());
        thresholdMaximumSpinBox->setMinimumSize(QSize(60, 0));
        thresholdMaximumSpinBox->setMaximumSize(QSize(60, 16777215));
        thresholdMaximumSpinBox->setMinimum(0.01);
        thresholdMaximumSpinBox->setMaximum(1.0);
        thresholdMaximumSpinBox->setSingleStep(0.05);
        thresholdMaximumSpinBox->setProperty("value", 1.0);
        thresholdMaximumSpinBox->setObjectName("thresholdMaximumSpinBox");
        gridLayout_amplitudeRange->addWidget(thresholdMaximumSpinBox, 1, 1, 1, 1);
        thresholdMaximumSlider = new QSlider(amplitudeRangeGroupBox->contentWidget());
        thresholdMaximumSlider->setMinimum(10);
        thresholdMaximumSlider->setMaximum(1000);
        thresholdMaximumSlider->setProperty("value", 1000);
        thresholdMaximumSlider->setOrientation(Qt::Orientation::Horizontal);
        thresholdMaximumSlider->setObjectName("thresholdMaximumSlider");
        gridLayout_amplitudeRange->addWidget(thresholdMaximumSlider, 1, 2, 1, 1);
        gridLayout_amplitudeRange->setColumnStretch(1, 1);
        gridLayout_amplitudeRange->setColumnStretch(2, 8);
        verticalLayout_amplitudeRange->addLayout(gridLayout_amplitudeRange);
        verticalLayout_starDetection->addWidget(amplitudeRangeGroupBox);
        horizontalLayout_roundness = new QHBoxLayout();
        horizontalLayout_roundness->setObjectName("horizontalLayout_roundness");
        label_roundnessThreshold = new QLabel(starDetectionGroupBox->contentWidget());
        label_roundnessThreshold->setObjectName("label_roundnessThreshold");
        horizontalLayout_roundness->addWidget(label_roundnessThreshold);
        roundnessThresholdSpinBox = new QDoubleSpinBox(starDetectionGroupBox->contentWidget());
        roundnessThresholdSpinBox->setMinimumSize(QSize(60, 0));
        roundnessThresholdSpinBox->setMaximumSize(QSize(60, 16777215));
        roundnessThresholdSpinBox->setMinimum(kSmallestStarRoundness);
        roundnessThresholdSpinBox->setMaximum(0.95);
        roundnessThresholdSpinBox->setSingleStep(0.05);
        roundnessThresholdSpinBox->setProperty("value", 0.5);
        roundnessThresholdSpinBox->setObjectName("roundnessThresholdSpinBox");
        horizontalLayout_roundness->addWidget(roundnessThresholdSpinBox);
        roundnessThresholdSlider = new QSlider(starDetectionGroupBox->contentWidget());
        roundnessThresholdSlider->setMinimum(static_cast<int>(std::lround(kSmallestStarRoundness * 1000.0)));
        roundnessThresholdSlider->setMaximum(950);
        roundnessThresholdSlider->setProperty("value", 500);
        roundnessThresholdSlider->setOrientation(Qt::Orientation::Horizontal);
        roundnessThresholdSlider->setObjectName("roundnessThresholdSlider");
        horizontalLayout_roundness->addWidget(roundnessThresholdSlider);
        auto* spacerItem5 = new QSpacerItem(40, 20, QSizePolicy::Policy::Expanding, QSizePolicy::Policy::Minimum);
        horizontalLayout_roundness->addItem(spacerItem5);
        horizontalLayout_roundness->setStretch(1, 1);
        horizontalLayout_roundness->setStretch(2, 8);
        verticalLayout_starDetection->addLayout(horizontalLayout_roundness);
        roundnessRangeGroupBox = new an::ui::AnCollapsibleSection(QString(), starDetectionGroupBox->contentWidget());

        roundnessRangeGroupBox->setObjectName("roundnessRangeGroupBox");
        verticalLayout_roundnessRange = new QVBoxLayout();
        roundnessRangeGroupBox->setContentLayout(verticalLayout_roundnessRange);
        verticalLayout_roundnessRange->setContentsMargins(t.spacing.sm, t.spacing.sm, t.spacing.sm, t.spacing.sm);
        verticalLayout_roundnessRange->setObjectName("verticalLayout_roundnessRange");
        chkEnableRoundnessRange = new QCheckBox(roundnessRangeGroupBox->contentWidget());
        chkEnableRoundnessRange->setObjectName("chkEnableRoundnessRange");
        verticalLayout_roundnessRange->addWidget(chkEnableRoundnessRange);
        horizontalLayout_roundnessMaximum = new QHBoxLayout();
        horizontalLayout_roundnessMaximum->setObjectName("horizontalLayout_roundnessMaximum");
        label_roundnessMaximum = new QLabel(roundnessRangeGroupBox->contentWidget());
        label_roundnessMaximum->setObjectName("label_roundnessMaximum");
        horizontalLayout_roundnessMaximum->addWidget(label_roundnessMaximum);
        roundnessThresholdMaximumSpinBox = new QDoubleSpinBox(roundnessRangeGroupBox->contentWidget());
        roundnessThresholdMaximumSpinBox->setMinimumSize(QSize(60, 0));
        roundnessThresholdMaximumSpinBox->setMaximumSize(QSize(60, 16777215));
        roundnessThresholdMaximumSpinBox->setMinimum(0.01);
        roundnessThresholdMaximumSpinBox->setMaximum(1.0);
        roundnessThresholdMaximumSpinBox->setSingleStep(0.05);
        roundnessThresholdMaximumSpinBox->setProperty("value", 1.0);
        roundnessThresholdMaximumSpinBox->setObjectName("roundnessThresholdMaximumSpinBox");
        horizontalLayout_roundnessMaximum->addWidget(roundnessThresholdMaximumSpinBox);
        roundnessThresholdMaximumSlider = new QSlider(roundnessRangeGroupBox->contentWidget());
        roundnessThresholdMaximumSlider->setMinimum(10);
        roundnessThresholdMaximumSlider->setMaximum(1000);
        roundnessThresholdMaximumSlider->setProperty("value", 1000);
        roundnessThresholdMaximumSlider->setOrientation(Qt::Orientation::Horizontal);
        roundnessThresholdMaximumSlider->setObjectName("roundnessThresholdMaximumSlider");
        horizontalLayout_roundnessMaximum->addWidget(roundnessThresholdMaximumSlider);
        horizontalLayout_roundnessMaximum->setStretch(1, 1);
        horizontalLayout_roundnessMaximum->setStretch(2, 2);
        verticalLayout_roundnessRange->addLayout(horizontalLayout_roundnessMaximum);
        verticalLayout_starDetection->addWidget(roundnessRangeGroupBox);
        horizontalLayout_convergence = new QHBoxLayout();
        horizontalLayout_convergence->setObjectName("horizontalLayout_convergence");
        label_convergence = new QLabel(starDetectionGroupBox->contentWidget());
        label_convergence->setObjectName("label_convergence");
        horizontalLayout_convergence->addWidget(label_convergence);
        convergenceSpinBox = new QSpinBox(starDetectionGroupBox->contentWidget());
        convergenceSpinBox->setMinimumSize(QSize(60, 0));
        convergenceSpinBox->setMaximumSize(QSize(60, 16777215));
        convergenceSpinBox->setMinimum(1);
        convergenceSpinBox->setMaximum(3);
        convergenceSpinBox->setObjectName("convergenceSpinBox");
        horizontalLayout_convergence->addWidget(convergenceSpinBox);
        auto* spacerItem6 = new QSpacerItem(40, 20, QSizePolicy::Policy::Expanding, QSizePolicy::Policy::Minimum);
        horizontalLayout_convergence->addItem(spacerItem6);
        relaxPSFChecksCheckBox = new QCheckBox(starDetectionGroupBox->contentWidget());
        relaxPSFChecksCheckBox->setObjectName("relaxPSFChecksCheckBox");
        horizontalLayout_convergence->addWidget(relaxPSFChecksCheckBox);
        auto* spacerItem7 = new QSpacerItem(40, 20, QSizePolicy::Policy::Expanding, QSizePolicy::Policy::Minimum);
        horizontalLayout_convergence->addItem(spacerItem7);
        horizontalLayout_convergence->setStretch(1, 2);
        horizontalLayout_convergence->setStretch(3, 1);
        horizontalLayout_convergence->setStretch(4, 10);
        verticalLayout_starDetection->addLayout(horizontalLayout_convergence);
        horizontalLayout_profileType = new QHBoxLayout();
        horizontalLayout_profileType->setObjectName("horizontalLayout_profileType");
        label_profileType = new QLabel(starDetectionGroupBox->contentWidget());
        label_profileType->setObjectName("label_profileType");
        horizontalLayout_profileType->addWidget(label_profileType);
        profileTypeComboBox = new QComboBox(starDetectionGroupBox->contentWidget());
        profileTypeComboBox->setMinimumSize(QSize(130, 0));
        profileTypeComboBox->setSizeAdjustPolicy(QComboBox::SizeAdjustPolicy::AdjustToMinimumContentsLengthWithIcon);
        profileTypeComboBox->setMinimumContentsLength(10);
        profileTypeComboBox->setObjectName("profileTypeComboBox");
        horizontalLayout_profileType->addWidget(profileTypeComboBox);
        label_minimumBeta = new QLabel(starDetectionGroupBox->contentWidget());
        label_minimumBeta->setObjectName("label_minimumBeta");
        horizontalLayout_profileType->addWidget(label_minimumBeta);
        minimumBetaSpinBox = new QDoubleSpinBox(starDetectionGroupBox->contentWidget());
        minimumBetaSpinBox->setMinimumSize(QSize(60, 0));
        minimumBetaSpinBox->setMaximumSize(QSize(60, 16777215));
        minimumBetaSpinBox->setMinimum(kSmallestMoffatBeta);
        minimumBetaSpinBox->setMaximum(10.0);
        minimumBetaSpinBox->setSingleStep(0.05);
        minimumBetaSpinBox->setProperty("value", 1.5);
        minimumBetaSpinBox->setObjectName("minimumBetaSpinBox");
        horizontalLayout_profileType->addWidget(minimumBetaSpinBox);
        auto* spacerItem8 = new QSpacerItem(40, 20, QSizePolicy::Policy::Expanding, QSizePolicy::Policy::Minimum);
        horizontalLayout_profileType->addItem(spacerItem8);
        horizontalLayout_profileType->setStretch(1, 1);
        horizontalLayout_profileType->setStretch(4, 4);
        verticalLayout_starDetection->addLayout(horizontalLayout_profileType);
        verticalLayout_advancedPanel->addWidget(starDetectionGroupBox);
        miscGroupBox = new an::ui::AnCollapsibleSection(QString(), advancedSettingsPanel);
        miscGroupBox->setObjectName("miscGroupBox");
        verticalLayout_miscellaneous = new QVBoxLayout();
        miscGroupBox->setContentLayout(verticalLayout_miscellaneous);
        verticalLayout_miscellaneous->setContentsMargins(t.spacing.md, t.spacing.md, t.spacing.md, t.spacing.md);
        verticalLayout_miscellaneous->setSpacing(t.spacing.md);
        verticalLayout_miscellaneous->setObjectName("verticalLayout_miscellaneous");
        horizontalLayout_cfaAndDebayer = new QHBoxLayout();
        horizontalLayout_cfaAndDebayer->setObjectName("horizontalLayout_cfaAndDebayer");
        label_cfaPattern = new QLabel(miscGroupBox->contentWidget());
        label_cfaPattern->setObjectName("label_cfaPattern");
        horizontalLayout_cfaAndDebayer->addWidget(label_cfaPattern);
        cfaPatternComboBox = new QComboBox(miscGroupBox->contentWidget());
        cfaPatternComboBox->setMinimumSize(QSize(120, 0));
        cfaPatternComboBox->setSizeAdjustPolicy(QComboBox::SizeAdjustPolicy::AdjustToMinimumContentsLengthWithIcon);
        cfaPatternComboBox->setMinimumContentsLength(8);
        cfaPatternComboBox->setObjectName("cfaPatternComboBox");
        horizontalLayout_cfaAndDebayer->addWidget(cfaPatternComboBox);
        label_debayerMethod = new QLabel(miscGroupBox->contentWidget());
        label_debayerMethod->setObjectName("label_debayerMethod");
        horizontalLayout_cfaAndDebayer->addWidget(label_debayerMethod);
        debayerMethodComboBox = new QComboBox(miscGroupBox->contentWidget());
        debayerMethodComboBox->setMinimumSize(QSize(150, 0));
        debayerMethodComboBox->setSizeAdjustPolicy(QComboBox::SizeAdjustPolicy::AdjustToMinimumContentsLengthWithIcon);
        debayerMethodComboBox->setMinimumContentsLength(12);
        debayerMethodComboBox->setObjectName("debayerMethodComboBox");
        horizontalLayout_cfaAndDebayer->addWidget(debayerMethodComboBox);
        auto* spacerItem9 = new QSpacerItem(40, 20, QSizePolicy::Policy::Expanding, QSizePolicy::Policy::Minimum);
        horizontalLayout_cfaAndDebayer->addItem(spacerItem9);
        verticalLayout_miscellaneous->addLayout(horizontalLayout_cfaAndDebayer);
        verticalLayout_advancedPanel->addWidget(miscGroupBox);
        verticalLayout_settingsContent->addWidget(advancedSettingsPanel);
        verticalLayout_settingsContent->addStretch(1);
        separator2 = new QFrame(RegistrationSettingsDialog);
        separator2->setFrameShape(QFrame::Shape::HLine);
        separator2->setFrameShadow(QFrame::Shadow::Sunken);
        separator2->setObjectName("separator2");
        verticalLayout_main->addWidget(separator2);
        horizontalLayout_buttons = new QHBoxLayout();
        horizontalLayout_buttons->setSpacing(t.spacing.sm);
        horizontalLayout_buttons->setObjectName("horizontalLayout_buttons");
        resetButton = new QPushButton(RegistrationSettingsDialog);
        resetButton->setObjectName("resetButton");
        horizontalLayout_buttons->addWidget(resetButton);
        applyToAllButton = new QPushButton(RegistrationSettingsDialog);
        applyToAllButton->setObjectName("applyToAllButton");
        horizontalLayout_buttons->addWidget(applyToAllButton);
        auto* spacerItem11 = new QSpacerItem(40, 20, QSizePolicy::Policy::Expanding, QSizePolicy::Policy::Minimum);
        horizontalLayout_buttons->addItem(spacerItem11);
        cancelButton = new QPushButton(RegistrationSettingsDialog);
        cancelButton->setObjectName("cancelButton");
        horizontalLayout_buttons->addWidget(cancelButton);
        okButton = new QPushButton(RegistrationSettingsDialog);
        okButton->setDefault(true);
        okButton->setObjectName("okButton");
        okButton->setProperty("buttonVariant", QStringLiteral("primary"));
        horizontalLayout_buttons->addWidget(okButton);
        verticalLayout_main->addLayout(horizontalLayout_buttons);
        // Expand all cards by default
        registrationGroupBox->setExpanded(true);
        alignmentGroupBox->setExpanded(true);
        starDetectionGroupBox->setExpanded(true);
        amplitudeRangeGroupBox->setExpanded(true);
        roundnessRangeGroupBox->setExpanded(true);
        miscGroupBox->setExpanded(true);

        retranslateUi(RegistrationSettingsDialog);
        const QPointer<QDialog> dialogGuard(RegistrationSettingsDialog);
        const auto applyScrollAwareMinimumWidth =
            [dialogGuard, verticalLayout_main = verticalLayout_main,
             verticalLayout_settingsContent = verticalLayout_settingsContent, settingsScrollArea = settingsScrollArea,
             registrationGroupBox = registrationGroupBox, alignmentGroupBox = alignmentGroupBox,
             advancedSettingsButton = advancedSettingsButton]() {
                if (!dialogGuard)
                {
                    return;
                }
                applyRegistrationSettingsDialogScrollAwareMinimumWidth(
                    dialogGuard.data(), verticalLayout_main, verticalLayout_settingsContent, settingsScrollArea,
                    registrationGroupBox, alignmentGroupBox, advancedSettingsButton);
            };
        applyScrollAwareMinimumWidth();
        QTimer::singleShot(0, RegistrationSettingsDialog, applyScrollAwareMinimumWidth);
        QObject::connect(okButton, &QPushButton::clicked, RegistrationSettingsDialog, &QDialog::accept);
        QObject::connect(cancelButton, &QPushButton::clicked, RegistrationSettingsDialog, &QDialog::reject);
        QMetaObject::connectSlotsByName(RegistrationSettingsDialog);
    }

    void retranslateUi(QDialog* RegistrationSettingsDialog)
    {
        RegistrationSettingsDialog->setWindowTitle(
            QCoreApplication::translate("RegistrationSettingsDialog", "Registration Settings"));
        titleLabel->setText(QCoreApplication::translate("RegistrationSettingsDialog", "Registration Settings for:"));
        registrationGroupBox->setTitle(QCoreApplication::translate("RegistrationSettingsDialog", "Registration"));
        label_minStarPairs->setText(QCoreApplication::translate("RegistrationSettingsDialog", "Minimum Star Pairs:"));
        label_maxStarPairs->setText(
            QCoreApplication::translate("RegistrationSettingsDialog", "Maximum Stars per Frame:"));
        label_referenceImage->setText(QCoreApplication::translate("RegistrationSettingsDialog", "Reference Image:"));
        autoAssignButton->setToolTip(QCoreApplication::translate(
            "RegistrationSettingsDialog", "Analyze the current group and preselect the best manual reference frame."));
        autoAssignButton->setText(QCoreApplication::translate("RegistrationSettingsDialog", "Best"));
        label_registrationMethod->setText(
            QCoreApplication::translate("RegistrationSettingsDialog", "Registration Method:"));
        label_transformation->setText(QCoreApplication::translate("RegistrationSettingsDialog", "Transform Model:"));
        transformSummaryLabel->setText(QCoreApplication::translate(
            "RegistrationSettingsDialog",
            "Choose a linear transform, a fixed polynomial refinement order, or Auto to let registration keep the "
            "linear model unless polynomial residual refinement is measurably better."));
        manualPointsSummaryLabel->setText(QCoreApplication::translate(
            "RegistrationSettingsDialog", "Manual control points are not used for the current method."));
        editManualPointsButton->setText(QCoreApplication::translate("RegistrationSettingsDialog", "Edit Points..."));
        alignmentGroupBox->setTitle(QCoreApplication::translate("RegistrationSettingsDialog", "Alignment"));
        alignModeStandard->setText(QCoreApplication::translate("RegistrationSettingsDialog", "Standard"));
        alignModeMosaic->setText(QCoreApplication::translate("RegistrationSettingsDialog", "Mosaic"));
        alignModeIntersection->setText(QCoreApplication::translate("RegistrationSettingsDialog", "Intersection"));
        label_interpolation->setText(QCoreApplication::translate("RegistrationSettingsDialog", "Interpolation:"));
        advancedSettingsButton->setText(
            QCoreApplication::translate("RegistrationSettingsDialog", "Advanced Settings..."));
        starDetectionGroupBox->setTitle(QCoreApplication::translate("RegistrationSettingsDialog", "Star Detection"));
        label_radius->setText(QCoreApplication::translate("RegistrationSettingsDialog", "Radius"));
        label_threshold->setText(QCoreApplication::translate("RegistrationSettingsDialog", "Threshold"));
        amplitudeRangeGroupBox->setTitle(QCoreApplication::translate("RegistrationSettingsDialog", "Amplitude Range"));
        chkEnableAmplitudeRange->setText(
            QCoreApplication::translate("RegistrationSettingsDialog", "Enable Amplitude Range"));
        label_thresholdMinimum->setText(QCoreApplication::translate("RegistrationSettingsDialog", "Minimum"));
        label_thresholdMaximum->setText(QCoreApplication::translate("RegistrationSettingsDialog", "Maximum"));
        label_roundnessThreshold->setText(
            QCoreApplication::translate("RegistrationSettingsDialog", "Roundness Threshold"));
        roundnessRangeGroupBox->setTitle(QCoreApplication::translate("RegistrationSettingsDialog", "Roundness Range"));
        chkEnableRoundnessRange->setText(
            QCoreApplication::translate("RegistrationSettingsDialog", "Enable Roundness Range"));
        label_roundnessMaximum->setText(QCoreApplication::translate("RegistrationSettingsDialog", "Maximum"));
        label_convergence->setText(QCoreApplication::translate("RegistrationSettingsDialog", "Convergence"));
        relaxPSFChecksCheckBox->setText(QCoreApplication::translate("RegistrationSettingsDialog", "Relax PSF Checks"));
        label_profileType->setText(QCoreApplication::translate("RegistrationSettingsDialog", "Profile Type   "));
        label_minimumBeta->setText(QCoreApplication::translate("RegistrationSettingsDialog", "Minimum Beta"));
        miscGroupBox->setTitle(QCoreApplication::translate("RegistrationSettingsDialog", "Miscellaneous"));
        label_cfaPattern->setText(QCoreApplication::translate("RegistrationSettingsDialog", "CFA Pattern"));
        label_debayerMethod->setText(QCoreApplication::translate("RegistrationSettingsDialog", "Method"));
        resetButton->setToolTip(
            QCoreApplication::translate("RegistrationSettingsDialog", "Reset all settings to default values"));
        resetButton->setText(QCoreApplication::translate("RegistrationSettingsDialog", "Reset to Defaults"));
        applyToAllButton->setToolTip(QCoreApplication::translate("RegistrationSettingsDialog",
                                                                 "Apply these settings to all registration groups"));
        applyToAllButton->setText(QCoreApplication::translate("RegistrationSettingsDialog", "Apply to All Groups"));
        cancelButton->setText(QCoreApplication::translate("RegistrationSettingsDialog", "Cancel"));
        okButton->setText(QCoreApplication::translate("RegistrationSettingsDialog", "OK"));
    }
};

inline RegistrationSettingsDialogUiElements buildRegistrationSettingsDialogUi(QDialog* parent)
{
    RegistrationSettingsDialogUiElements ui{};
    if (!parent)
    {
        return ui;
    }

    ui.setupUi(parent);
    return ui;
}

} // namespace an::ui::imageintegration
