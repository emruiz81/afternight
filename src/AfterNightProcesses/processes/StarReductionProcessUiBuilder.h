// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "processes/RTPreviewUiCommon.h"
#include "theme/AnDesignTokens.h"

#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSize>
#include <QSizePolicy>
#include <QSpinBox>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>

namespace an::ui::processes
{

struct StarReductionProcessUiElements
{
    QHBoxLayout* horizontalLayoutMain = nullptr;
    QVBoxLayout* previewLayout = nullptr;

    QComboBox* targetImageSelector = nullptr;
    QComboBox* starlessImageSelector = nullptr;
    QComboBox* starlessGeneratorSelector = nullptr;
    QPushButton* generateStarlessView = nullptr;

    QPushButton* initializeSmallStarProtection = nullptr;
    QCheckBox* enableSmallStarProtection = nullptr;

    QComboBox* method = nullptr;
    QDoubleSpinBox* stretchFactor = nullptr;
    QComboBox* mode = nullptr;
    QSpinBox* iterations = nullptr;

    QCheckBox* reduceTargetImage = nullptr;
    QCheckBox* createNewImage = nullptr;
    QLineEdit* newImageId = nullptr;
    QCheckBox* createStarsImage = nullptr;
    QCheckBox* enableReverseStretch = nullptr;
    QLineEdit* starImageId = nullptr;

    QCheckBox* preview = nullptr;
    QDialogButtonBox* buttonBox = nullptr;
};

inline StarReductionProcessUiElements buildStarReductionProcessUi(QWidget* parent)
{
    StarReductionProcessUiElements ui;
    const auto& t = an::ui::tokens();

    if (!parent)
    {
        return ui;
    }

    parent->setObjectName(QStringLiteral("StarReductionProcess"));
    parent->resize(760, 680);
    parent->setMinimumSize(QSize(360, 260));
    parent->setWindowTitle(QStringLiteral("Star Reduction"));

    ui.horizontalLayoutMain = new QHBoxLayout(parent);
    ui.horizontalLayoutMain->setObjectName(QStringLiteral("horizontalLayoutMain"));
    ui.horizontalLayoutMain->setSpacing(t.spacing.sm);
    ui.horizontalLayoutMain->setContentsMargins(t.spacing.sm, t.spacing.sm, t.spacing.sm, t.spacing.sm);

    const auto split = rt_preview_ui::buildMainProcessSplitter(
        ui.horizontalLayoutMain, parent, QMargins(t.spacing.sm, t.spacing.xs, t.spacing.sm, t.spacing.xs));
    QWidget* controlsPanel = split.controlsPanel;
    QVBoxLayout* controlsLayout = split.controlsLayout;
    ui.previewLayout = split.previewLayout;
    controlsLayout->setSpacing(t.spacing.sm);

    auto configureGroupLayout = [&](QGroupBox* group) {
        auto* layout = new QGridLayout(group);
        layout->setSpacing(t.spacing.xs);
        layout->setContentsMargins(t.spacing.xs, t.spacing.xs, t.spacing.xs, t.spacing.xs);
        layout->setColumnStretch(1, 1);
        return layout;
    };

    auto* viewsGroup = new QGroupBox(QStringLiteral("Views"), controlsPanel);
    viewsGroup->setObjectName(QStringLiteral("viewsGroup"));
    auto* viewsLayout = configureGroupLayout(viewsGroup);

    auto* targetLabel = new QLabel(QStringLiteral("Target image"), viewsGroup);
    viewsLayout->addWidget(targetLabel, 0, 0);
    ui.targetImageSelector = new QComboBox(viewsGroup);
    ui.targetImageSelector->setObjectName(QStringLiteral("targetImageSelector"));
    ui.targetImageSelector->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    viewsLayout->addWidget(ui.targetImageSelector, 0, 1, 1, 2);

    auto* starlessLabel = new QLabel(QStringLiteral("Starless image"), viewsGroup);
    viewsLayout->addWidget(starlessLabel, 1, 0);
    ui.starlessImageSelector = new QComboBox(viewsGroup);
    ui.starlessImageSelector->setObjectName(QStringLiteral("starlessImageSelector"));
    ui.starlessImageSelector->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    viewsLayout->addWidget(ui.starlessImageSelector, 1, 1, 1, 2);

    auto* generatorLabel = new QLabel(QStringLiteral("Generator"), viewsGroup);
    viewsLayout->addWidget(generatorLabel, 2, 0);
    ui.starlessGeneratorSelector = new QComboBox(viewsGroup);
    ui.starlessGeneratorSelector->setObjectName(QStringLiteral("starlessGeneratorSelector"));
    ui.starlessGeneratorSelector->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    viewsLayout->addWidget(ui.starlessGeneratorSelector, 2, 1, 1, 2);

    ui.generateStarlessView = new QPushButton(QStringLiteral("Generate starless view"), viewsGroup);
    ui.generateStarlessView->setObjectName(QStringLiteral("generateStarlessView"));
    viewsLayout->addWidget(ui.generateStarlessView, 3, 1, 1, 2);
    controlsLayout->addWidget(viewsGroup);

    auto* maskGroup = new QGroupBox(QStringLiteral("Small star protection"), controlsPanel);
    maskGroup->setObjectName(QStringLiteral("smallStarProtectionGroup"));
    auto* maskLayout = configureGroupLayout(maskGroup);
    ui.initializeSmallStarProtection = new QPushButton(QStringLiteral("Initialise small star protection"), maskGroup);
    ui.initializeSmallStarProtection->setObjectName(QStringLiteral("initializeSmallStarProtection"));
    maskLayout->addWidget(ui.initializeSmallStarProtection, 0, 0, 1, 3);
    ui.enableSmallStarProtection = new QCheckBox(QStringLiteral("Enable small star protection"), maskGroup);
    ui.enableSmallStarProtection->setObjectName(QStringLiteral("enableSmallStarProtection"));
    ui.enableSmallStarProtection->setChecked(false);
    maskLayout->addWidget(ui.enableSmallStarProtection, 1, 0, 1, 3);
    controlsLayout->addWidget(maskGroup);

    auto* paramsGroup = new QGroupBox(QStringLiteral("Star reduction parameters"), controlsPanel);
    paramsGroup->setObjectName(QStringLiteral("parametersGroup"));
    auto* paramsLayout = configureGroupLayout(paramsGroup);
    auto* methodLabel = new QLabel(QStringLiteral("Reduction method"), paramsGroup);
    paramsLayout->addWidget(methodLabel, 0, 0);
    ui.method = new QComboBox(paramsGroup);
    ui.method->setObjectName(QStringLiteral("method"));
    paramsLayout->addWidget(ui.method, 0, 1, 1, 2);

    auto* stretchFactorLabel = new QLabel(QStringLiteral("Stretch factor"), paramsGroup);
    paramsLayout->addWidget(stretchFactorLabel, 1, 0);
    ui.stretchFactor = new QDoubleSpinBox(paramsGroup);
    ui.stretchFactor->setObjectName(QStringLiteral("stretchFactor"));
    ui.stretchFactor->setDecimals(2);
    // The reduction's midtones transfer is defined strictly inside the unit interval.
    ui.stretchFactor->setRange(0.01, 0.99);
    ui.stretchFactor->setSingleStep(0.01);
    ui.stretchFactor->setValue(0.2);
    paramsLayout->addWidget(ui.stretchFactor, 1, 1, 1, 2);

    auto* modeLabel = new QLabel(QStringLiteral("Mode"), paramsGroup);
    paramsLayout->addWidget(modeLabel, 2, 0);
    ui.mode = new QComboBox(paramsGroup);
    ui.mode->setObjectName(QStringLiteral("mode"));
    paramsLayout->addWidget(ui.mode, 2, 1, 1, 2);

    auto* iterationsLabel = new QLabel(QStringLiteral("Iterations"), paramsGroup);
    paramsLayout->addWidget(iterationsLabel, 3, 0);
    ui.iterations = new QSpinBox(paramsGroup);
    ui.iterations->setObjectName(QStringLiteral("iterations"));
    ui.iterations->setRange(1, 3);
    ui.iterations->setValue(1);
    paramsLayout->addWidget(ui.iterations, 3, 1, 1, 2);
    controlsLayout->addWidget(paramsGroup);

    auto* outputGroup = new QGroupBox(QStringLiteral("Output options"), controlsPanel);
    outputGroup->setObjectName(QStringLiteral("outputGroup"));
    auto* outputLayout = configureGroupLayout(outputGroup);
    ui.reduceTargetImage = new QCheckBox(QStringLiteral("Reduce stars on target image"), outputGroup);
    ui.reduceTargetImage->setObjectName(QStringLiteral("reduceTargetImage"));
    outputLayout->addWidget(ui.reduceTargetImage, 0, 0, 1, 3);

    ui.createNewImage = new QCheckBox(QStringLiteral("Create new star-reduced image"), outputGroup);
    ui.createNewImage->setObjectName(QStringLiteral("createNewImage"));
    ui.createNewImage->setChecked(true);
    outputLayout->addWidget(ui.createNewImage, 1, 0, 1, 3);
    auto* newImageIdLabel = new QLabel(QStringLiteral("Image id"), outputGroup);
    outputLayout->addWidget(newImageIdLabel, 2, 0);
    ui.newImageId = new QLineEdit(QStringLiteral("<Auto>"), outputGroup);
    ui.newImageId->setObjectName(QStringLiteral("newImageId"));
    outputLayout->addWidget(ui.newImageId, 2, 1, 1, 2);

    ui.createStarsImage = new QCheckBox(QStringLiteral("Create reduced-stars-only image"), outputGroup);
    ui.createStarsImage->setObjectName(QStringLiteral("createStarsImage"));
    ui.createStarsImage->setChecked(true);
    outputLayout->addWidget(ui.createStarsImage, 3, 0, 1, 3);
    ui.enableReverseStretch = new QCheckBox(QStringLiteral("Enable reverse stretch"), outputGroup);
    ui.enableReverseStretch->setObjectName(QStringLiteral("enableReverseStretch"));
    outputLayout->addWidget(ui.enableReverseStretch, 4, 0, 1, 3);
    auto* starImageIdLabel = new QLabel(QStringLiteral("Stars id"), outputGroup);
    outputLayout->addWidget(starImageIdLabel, 5, 0);
    ui.starImageId = new QLineEdit(QStringLiteral("<Auto>"), outputGroup);
    ui.starImageId->setObjectName(QStringLiteral("starImageId"));
    outputLayout->addWidget(ui.starImageId, 5, 1, 1, 2);
    controlsLayout->addWidget(outputGroup);

    ui.preview = new QCheckBox(QStringLiteral("Show preview"), controlsPanel);
    ui.preview->setObjectName(QStringLiteral("preview"));
    ui.preview->setChecked(true);
    controlsLayout->addWidget(ui.preview);

    ui.buttonBox =
        new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Close | QDialogButtonBox::Reset, controlsPanel);
    ui.buttonBox->setObjectName(QStringLiteral("buttonBox"));
    controlsLayout->addWidget(ui.buttonBox);
    controlsLayout->addStretch(1);

#if QT_CONFIG(tooltip)
    auto setSharedTooltip = [](QWidget* label, QWidget* control, const QString& tooltip) {
        if (label)
        {
            label->setToolTip(tooltip);
        }
        if (control)
        {
            control->setToolTip(tooltip);
        }
    };

    setSharedTooltip(targetLabel, ui.targetImageSelector,
                     QCoreApplication::translate(
                         "StarReductionProcess",
                         "Choose the image whose stars will be reduced. The target should be the original image, with "
                         "the same dimensions and channel layout as the selected starless image."));
    setSharedTooltip(
        starlessLabel, ui.starlessImageSelector,
        QCoreApplication::translate(
            "StarReductionProcess",
            "Choose a starless version of the target image. Star Reduction uses this as the background "
            "reference, so it should come from the same target and still preserve the nebula/galaxy data."));
    setSharedTooltip(generatorLabel, ui.starlessGeneratorSelector,
                     QCoreApplication::translate(
                         "StarReductionProcess",
                         "Choose the process used to create a starless helper image. Native StarNet2 is listed when "
                         "configured, and compatible extensions can register here too."));
    ui.generateStarlessView->setToolTip(QCoreApplication::translate(
        "StarReductionProcess",
        "Open the selected generator so you can configure its settings. After it finishes, the generated starless view "
        "is selected automatically when it is compatible with the target."));

    ui.initializeSmallStarProtection->setToolTip(QCoreApplication::translate(
        "StarReductionProcess",
        "Create a protection mask from the target and starless image. The mask helps preserve faint, tiny stars that "
        "can disappear when strong star reduction is applied."));
    ui.enableSmallStarProtection->setToolTip(QCoreApplication::translate(
        "StarReductionProcess", "Use the generated small-star protection mask while applying or previewing the "
                                "reduction. Protected pixels keep "
                                "more of the original target image."));

    setSharedTooltip(methodLabel, ui.method,
                     QCoreApplication::translate(
                         "StarReductionProcess",
                         "Select the reduction formula. Transfer and Halo use the stretch factor for PixInsight-style "
                         "curve compression; Star is the general-purpose iterative method; Lightness weighs reduction "
                         "by image lightness so bright structures are handled more gently."));
    setSharedTooltip(stretchFactorLabel, ui.stretchFactor,
                     QCoreApplication::translate(
                         "StarReductionProcess",
                         "Controls the curve strength used by Transfer and Halo methods. Higher values make the "
                         "star/halo compression more assertive; lower values keep the result closer to the target."));
    setSharedTooltip(
        modeLabel, ui.mode,
        QCoreApplication::translate(
            "StarReductionProcess",
            "Controls how much of the iterative Star or Lightness result is used. Strong applies the full "
            "effect, Moderate blends back some original star intensity, and Soft is the gentlest option."));
    setSharedTooltip(
        iterationsLabel, ui.iterations,
        QCoreApplication::translate(
            "StarReductionProcess",
            "Repeats the Star or Lightness reduction step. More iterations reduce stars more strongly, but "
            "can also flatten bright stellar cores if pushed too far."));

    ui.reduceTargetImage->setToolTip(QCoreApplication::translate(
        "StarReductionProcess",
        "Apply the reduction directly to the target view. Use this when you are ready to modify the current image."));
    ui.createNewImage->setToolTip(QCoreApplication::translate(
        "StarReductionProcess",
        "Create a separate star-reduced image instead of, or in place of, editing the target. This is the safest "
        "workflow while tuning settings."));
    setSharedTooltip(newImageIdLabel, ui.newImageId,
                     QCoreApplication::translate(
                         "StarReductionProcess",
                         "Name for the new star-reduced image. Leave <Auto> to use the target name with a descriptive "
                         "suffix."));
    ui.createStarsImage->setToolTip(QCoreApplication::translate(
        "StarReductionProcess",
        "Create an image containing only the stars that were removed or reduced. This is useful for inspection, masks, "
        "or recombining stars later."));
    ui.enableReverseStretch->setToolTip(QCoreApplication::translate(
        "StarReductionProcess",
        "When extracting the reduced-stars-only image, compensate for the stretch curve used by Transfer/Halo style "
        "workflows. Leave it off for the normal unscreen extraction path."));
    setSharedTooltip(
        starImageIdLabel, ui.starImageId,
        QCoreApplication::translate(
            "StarReductionProcess",
            "Name for the reduced-stars-only image. Leave <Auto> to use the target name with a descriptive "
            "stars suffix."));
    ui.preview->setToolTip(QCoreApplication::translate(
        "StarReductionProcess",
        "Show the native real-time preview so you can compare the current settings against the target before applying "
        "them."));

    if (auto* okButton = ui.buttonBox->button(QDialogButtonBox::Ok))
    {
        okButton->setToolTip(
            QCoreApplication::translate("StarReductionProcess", "Run Star Reduction with the current settings."));
    }
    if (auto* resetButton = ui.buttonBox->button(QDialogButtonBox::Reset))
    {
        resetButton->setToolTip(QCoreApplication::translate(
            "StarReductionProcess", "Restore the default Star Reduction settings and output choices."));
    }
    if (auto* closeButton = ui.buttonBox->button(QDialogButtonBox::Close))
    {
        closeButton->setToolTip(
            QCoreApplication::translate("StarReductionProcess", "Close this process without applying changes."));
    }
#endif // QT_CONFIG(tooltip)

    const int controlsWidth = controlsPanel->sizeHint().width() + t.spacing.xl;
    const int previewPaneWidth = (std::max)(rt_preview_ui::kMinPreviewPaneWidth, parent->width() - controlsWidth);
    split.mainSplitter->setSizes({controlsWidth, previewPaneWidth});

    return ui;
}

} // namespace an::ui::processes
