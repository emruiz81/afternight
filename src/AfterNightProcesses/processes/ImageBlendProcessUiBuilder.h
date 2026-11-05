// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "processes/RTPreviewUiCommon.h"
#include "theme/AnDesignTokens.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLayout>
#include <QPushButton>
#include <QScrollArea>
#include <QSizePolicy>
#include <QSlider>
#include <QSpacerItem>
#include <QString>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidget>

namespace an::ui::processes
{

struct ImageBlendProcessUiElements
{
    QHBoxLayout* horizontalLayoutMain = nullptr;
    QVBoxLayout* previewLayout = nullptr;

    QScrollArea* chartScrollArea = nullptr;
    QToolButton* zoomIn = nullptr;
    QToolButton* zoomOut = nullptr;
    QToolButton* zoomReset = nullptr;
    QToolButton* gridButton = nullptr;
    QToolButton* showClippingInfo = nullptr;
    QToolButton* localChannelNormalization = nullptr;
    QToolButton* showHistogramAreas = nullptr;
    QToolButton* showHistogram = nullptr;
    QComboBox* plotResolution = nullptr;

    QGroupBox* verticalGroupBox1 = nullptr;
    QComboBox* targetImageSelector = nullptr;
    QGroupBox* srcImageMTFGroupBox = nullptr;
    QDoubleSpinBox* srcImageBlackPointValue = nullptr;
    QSlider* srcImageBlackPointSlider = nullptr;
    QDoubleSpinBox* srcImageHighlightsValue = nullptr;
    QSlider* srcImageHighlightsSlider = nullptr;
    QDoubleSpinBox* srcImageMidtonesValue = nullptr;
    QSlider* srcImageMidtonesSlider = nullptr;

    QGroupBox* verticalGroupBox2 = nullptr;
    QComboBox* secondImageSelector = nullptr;
    QGroupBox* blendImageMTFGroupBox = nullptr;
    QDoubleSpinBox* imageBlendBlackPointValue = nullptr;
    QSlider* imageBlendBlackPointSlider = nullptr;
    QDoubleSpinBox* imageBlendHighlightsValue = nullptr;
    QSlider* imageBlendHighlightsSlider = nullptr;
    QDoubleSpinBox* imageBlendMidtonesValue = nullptr;
    QSlider* imageBlendMidtonesSlider = nullptr;

    QGroupBox* filterGroupBox = nullptr;
    QComboBox* filterSelector = nullptr;
    QLabel* filterRadiusLabel = nullptr;
    QDoubleSpinBox* filterRadiusValue = nullptr;
    QSlider* filterRadiusSlider = nullptr;
    QLabel* filterSaturationLabel = nullptr;
    QDoubleSpinBox* filterSaturationValue = nullptr;
    QSlider* filterSaturationSlider = nullptr;
    QLabel* filterHueLabel = nullptr;
    QDoubleSpinBox* filterHueValue = nullptr;
    QSlider* filterHueSlider = nullptr;

    QLabel* opacityLabel = nullptr;
    QDoubleSpinBox* opacityValue = nullptr;
    QSlider* opacitySlider = nullptr;

    QGroupBox* blendParametersGroupBox = nullptr;
    QComboBox* blendMode = nullptr;
    QCheckBox* preserveLightness = nullptr;

    QCheckBox* preview = nullptr;
    QDialogButtonBox* buttonBox = nullptr;
};

inline ImageBlendProcessUiElements buildImageBlendProcessUi(QWidget* parent)
{
    ImageBlendProcessUiElements ui;
    const auto& t = an::ui::tokens();

    if (!parent)
    {
        return ui;
    }

    parent->setObjectName(QStringLiteral("ImageBlendProcess"));
    parent->resize(800, 700);
    parent->setMinimumSize(QSize(300, 200));
    parent->setWindowTitle(QStringLiteral("Image Blend"));

    ui.horizontalLayoutMain = new QHBoxLayout(parent);
    ui.horizontalLayoutMain->setObjectName(QStringLiteral("horizontalLayoutMain"));
    ui.horizontalLayoutMain->setSpacing(t.spacing.sm);
    ui.horizontalLayoutMain->setSizeConstraint(QLayout::SetMinimumSize);
    ui.horizontalLayoutMain->setContentsMargins(t.spacing.sm, t.spacing.sm, t.spacing.sm, t.spacing.sm);

    const auto split = rt_preview_ui::buildMainProcessSplitter(
        ui.horizontalLayoutMain, parent, QMargins(t.spacing.sm, t.spacing.xs, t.spacing.sm, t.spacing.xs));
    auto* controlsLayout = split.controlsLayout;
    auto* controlsPanel = split.controlsPanel;
    ui.previewLayout = split.previewLayout;
    controlsLayout->setSpacing(t.spacing.xs);

    auto* chartLayout = new QHBoxLayout();
    chartLayout->setObjectName(QStringLiteral("horizontalLayout_5"));

    ui.chartScrollArea = new QScrollArea(controlsPanel);
    ui.chartScrollArea->setObjectName(QStringLiteral("chartScrollArea"));
    ui.chartScrollArea->setMinimumSize(QSize(320, 160));
    ui.chartScrollArea->setWidgetResizable(true);

    auto* scrollAreaWidgetContents = new QWidget();
    scrollAreaWidgetContents->setObjectName(QStringLiteral("scrollAreaWidgetContents"));
    scrollAreaWidgetContents->resize(318, 158);
    ui.chartScrollArea->setWidget(scrollAreaWidgetContents);
    chartLayout->addWidget(ui.chartScrollArea);
    controlsLayout->addLayout(chartLayout);

    auto* histogramSettingsLayout = new QHBoxLayout();
    histogramSettingsLayout->setObjectName(QStringLiteral("histogramSettingsLayout_2"));
    const auto toolbar = rt_preview_ui::buildHistogramToolbar(controlsPanel, histogramSettingsLayout,
                                                              {.translationContext = "ImageBlendProcess"});
    ui.zoomIn = toolbar.zoomIn;
    ui.zoomOut = toolbar.zoomOut;
    ui.zoomReset = toolbar.zoomReset;
    ui.gridButton = toolbar.gridButton;
    ui.showClippingInfo = toolbar.showClippingInfo;
    ui.localChannelNormalization = toolbar.localChannelNormalization;
    ui.showHistogramAreas = toolbar.showHistogramAreas;
    ui.showHistogram = toolbar.showHistogram;
    ui.plotResolution = toolbar.plotResolution;
    controlsLayout->addLayout(histogramSettingsLayout);

    auto makeStretchControls = [&](QGroupBox* groupBox, QDoubleSpinBox*& blackValue, QSlider*& blackSlider,
                                   QDoubleSpinBox*& highlightsValue, QSlider*& highlightsSlider,
                                   QDoubleSpinBox*& midtonesValue, QSlider*& midtonesSlider,
                                   const QString& blackValueName, const QString& blackSliderName,
                                   const QString& highlightsValueName, const QString& highlightsSliderName,
                                   const QString& midtonesValueName, const QString& midtonesSliderName) {
        auto* grid = new QGridLayout(groupBox);
        grid->setSpacing(t.spacing.xs);
        grid->setContentsMargins(t.spacing.xs, t.spacing.xs, t.spacing.xs, t.spacing.xs);
        grid->setColumnStretch(2, 1); // sliders expand

        auto addRow = [&](int row, const QString& labelText, const QString& labelName, const QString& valueName,
                          const QString& sliderName, double value) {
            auto* label = new QLabel(labelText, groupBox);
            label->setObjectName(labelName);
            label->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
            grid->addWidget(label, row, 0);

            auto* spin = new QDoubleSpinBox(groupBox);
            spin->setObjectName(valueName);
            spin->setMinimumWidth(80);
            spin->setDecimals(4);
            spin->setMaximum(1.0);
            spin->setSingleStep(0.01);
            spin->setValue(value);
            grid->addWidget(spin, row, 1);

            auto* slider = new QSlider(Qt::Horizontal, groupBox);
            slider->setObjectName(sliderName);
            grid->addWidget(slider, row, 2);

            return std::pair<QDoubleSpinBox*, QSlider*>{spin, slider};
        };

        auto black =
            addRow(0, QStringLiteral("Black Point"), QStringLiteral("label_8"), blackValueName, blackSliderName, 0.0);
        blackValue = black.first;
        blackSlider = black.second;

        auto highlights = addRow(1, QStringLiteral("Highlights"), QStringLiteral("label_9"), highlightsValueName,
                                 highlightsSliderName, 1.0);
        highlightsValue = highlights.first;
        highlightsSlider = highlights.second;

        auto midtones = addRow(2, QStringLiteral("Midtones"), QStringLiteral("label_10"), midtonesValueName,
                               midtonesSliderName, 0.5);
        midtonesValue = midtones.first;
        midtonesSlider = midtones.second;
    };

    ui.verticalGroupBox1 = new QGroupBox(QStringLiteral("Base image"), parent);
    ui.verticalGroupBox1->setObjectName(QStringLiteral("verticalGroupBox1"));
    auto* baseLayout = new QVBoxLayout(ui.verticalGroupBox1);
    baseLayout->setSpacing(t.spacing.xs);
    baseLayout->setContentsMargins(t.spacing.xs, t.spacing.xs, t.spacing.xs, t.spacing.xs);

    auto* baseImageRow = new QHBoxLayout();
    baseImageRow->setSpacing(t.spacing.sm);
    auto* baseImageLabel = new QLabel(QStringLiteral("Image"), ui.verticalGroupBox1);
    baseImageLabel->setObjectName(QStringLiteral("label_3"));
    baseImageLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    baseImageRow->addWidget(baseImageLabel);

    ui.targetImageSelector = new QComboBox(ui.verticalGroupBox1);
    ui.targetImageSelector->setObjectName(QStringLiteral("targetImageSelector"));
    ui.targetImageSelector->setToolTip(QStringLiteral("Select the base image to blend onto."));
    ui.targetImageSelector->setMinimumWidth(150);
    ui.targetImageSelector->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    baseImageRow->addWidget(ui.targetImageSelector, 1);
    baseLayout->addLayout(baseImageRow);

    ui.srcImageMTFGroupBox = new QGroupBox(QStringLiteral("Stretch"), ui.verticalGroupBox1);
    ui.srcImageMTFGroupBox->setObjectName(QStringLiteral("srcImageMTFGroupBox"));
    makeStretchControls(ui.srcImageMTFGroupBox, ui.srcImageBlackPointValue, ui.srcImageBlackPointSlider,
                        ui.srcImageHighlightsValue, ui.srcImageHighlightsSlider, ui.srcImageMidtonesValue,
                        ui.srcImageMidtonesSlider, QStringLiteral("srcImageBlackPointValue"),
                        QStringLiteral("srcImageBlackPointSlider"), QStringLiteral("srcImageHighlightsValue"),
                        QStringLiteral("srcImageHighlightsSlider"), QStringLiteral("srcImageMidtonesValue"),
                        QStringLiteral("srcImageMidtonesSlider"));
    baseLayout->addWidget(ui.srcImageMTFGroupBox);
    controlsLayout->addWidget(ui.verticalGroupBox1);

    ui.verticalGroupBox2 = new QGroupBox(QStringLiteral("Blend image"), parent);
    ui.verticalGroupBox2->setObjectName(QStringLiteral("verticalGroupBox2"));
    auto* blendLayout = new QVBoxLayout(ui.verticalGroupBox2);
    blendLayout->setSpacing(t.spacing.xs);
    blendLayout->setContentsMargins(t.spacing.xs, t.spacing.xs, t.spacing.xs, t.spacing.xs);

    auto* blendImageRow = new QHBoxLayout();
    blendImageRow->setSpacing(t.spacing.sm);
    auto* blendImageLabel = new QLabel(QStringLiteral("Image"), ui.verticalGroupBox2);
    blendImageLabel->setObjectName(QStringLiteral("label_4"));
    blendImageLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    blendImageRow->addWidget(blendImageLabel);

    ui.secondImageSelector = new QComboBox(ui.verticalGroupBox2);
    ui.secondImageSelector->setObjectName(QStringLiteral("secondImageSelector"));
    ui.secondImageSelector->setToolTip(QStringLiteral("Select the image to blend with the base image."));
    ui.secondImageSelector->setMinimumWidth(150);
    ui.secondImageSelector->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    blendImageRow->addWidget(ui.secondImageSelector, 1);
    blendLayout->addLayout(blendImageRow);

    ui.blendImageMTFGroupBox = new QGroupBox(QStringLiteral("Stretch"), ui.verticalGroupBox2);
    ui.blendImageMTFGroupBox->setObjectName(QStringLiteral("blendImageMTFGroupBox"));
    makeStretchControls(ui.blendImageMTFGroupBox, ui.imageBlendBlackPointValue, ui.imageBlendBlackPointSlider,
                        ui.imageBlendHighlightsValue, ui.imageBlendHighlightsSlider, ui.imageBlendMidtonesValue,
                        ui.imageBlendMidtonesSlider, QStringLiteral("imageBlendBlackPointValue"),
                        QStringLiteral("imageBlendBlackPointSlider"), QStringLiteral("imageBlendHighlightsValue"),
                        QStringLiteral("imageBlendHighlightsSlider"), QStringLiteral("imageBlendMidtonesValue"),
                        QStringLiteral("imageBlendMidtonesSlider"));
    blendLayout->addWidget(ui.blendImageMTFGroupBox);

    ui.filterGroupBox = new QGroupBox(QStringLiteral("Filter"), ui.verticalGroupBox2);
    ui.filterGroupBox->setObjectName(QStringLiteral("filterGroupBox"));
    auto* filterGrid = new QGridLayout(ui.filterGroupBox);
    filterGrid->setSpacing(t.spacing.xs);
    filterGrid->setContentsMargins(t.spacing.xs, t.spacing.xs, t.spacing.xs, t.spacing.xs);
    filterGrid->setColumnStretch(2, 1); // sliders expand

    // Row 0: Filter Type (label + combobox spanning spinbox+slider columns)
    auto* filterTypeLabel = new QLabel(QStringLiteral("Filter Type"), ui.filterGroupBox);
    filterTypeLabel->setObjectName(QStringLiteral("label_2"));
    filterTypeLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    filterGrid->addWidget(filterTypeLabel, 0, 0);

    ui.filterSelector = new QComboBox(ui.filterGroupBox);
    ui.filterSelector->setObjectName(QStringLiteral("filterSelector"));
    ui.filterSelector->setToolTip(QStringLiteral("Select the filter to apply to the blend image before compositing."));
    ui.filterSelector->setMinimumWidth(70);
    filterGrid->addWidget(ui.filterSelector, 0, 1, 1, 2);

    auto addFilterRow = [&](int row, QLabel*& label, QDoubleSpinBox*& value, QSlider*& slider, const QString& labelName,
                            const QString& labelText, const QString& valueName, const QString& sliderName, int decimals,
                            double min, double max, double step, double initial) {
        label = new QLabel(labelText, ui.filterGroupBox);
        label->setObjectName(labelName);
        label->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        filterGrid->addWidget(label, row, 0);

        value = new QDoubleSpinBox(ui.filterGroupBox);
        value->setObjectName(valueName);
        value->setMinimumWidth(80);
        value->setDecimals(decimals);
        value->setMinimum(min);
        value->setMaximum(max);
        value->setSingleStep(step);
        value->setValue(initial);
        filterGrid->addWidget(value, row, 1);

        slider = new QSlider(Qt::Horizontal, ui.filterGroupBox);
        slider->setObjectName(sliderName);
        filterGrid->addWidget(slider, row, 2);
    };

    addFilterRow(1, ui.filterRadiusLabel, ui.filterRadiusValue, ui.filterRadiusSlider,
                 QStringLiteral("filterRadiusLabel"), QStringLiteral("Radius"), QStringLiteral("filterRadiusValue"),
                 QStringLiteral("filterRadiusSlider"), 3, 0.1, 1000.0, 0.01, 10.0);

    addFilterRow(2, ui.filterSaturationLabel, ui.filterSaturationValue, ui.filterSaturationSlider,
                 QStringLiteral("filterSaturationLabel"), QStringLiteral("Saturation"),
                 QStringLiteral("filterSaturationValue"), QStringLiteral("filterSaturationSlider"), 3, 0.0, 1.0, 0.01,
                 1.0);

    addFilterRow(3, ui.filterHueLabel, ui.filterHueValue, ui.filterHueSlider, QStringLiteral("filterHueLabel"),
                 QStringLiteral("Hue"), QStringLiteral("filterHueValue"), QStringLiteral("filterHueSlider"), 3, 0.0,
                 360.0, 0.01, 0.0);

    blendLayout->addWidget(ui.filterGroupBox);

    auto* opacityGrid = new QGridLayout();
    opacityGrid->setSpacing(t.spacing.xs);
    opacityGrid->setColumnStretch(2, 1);

    ui.opacityLabel = new QLabel(QStringLiteral("Opacity"), ui.verticalGroupBox2);
    ui.opacityLabel->setObjectName(QStringLiteral("opacityLabel"));
    ui.opacityLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    opacityGrid->addWidget(ui.opacityLabel, 0, 0);

    ui.opacityValue = new QDoubleSpinBox(ui.verticalGroupBox2);
    ui.opacityValue->setObjectName(QStringLiteral("opacityValue"));
    ui.opacityValue->setToolTip(QStringLiteral("Opacity of the blend layer (0.0 = transparent, 1.0 = fully opaque)."));
    ui.opacityValue->setMinimumWidth(80);
    ui.opacityValue->setDecimals(4);
    ui.opacityValue->setMaximum(1.0);
    ui.opacityValue->setSingleStep(0.05);
    ui.opacityValue->setValue(1.0);
    opacityGrid->addWidget(ui.opacityValue, 0, 1);

    ui.opacitySlider = new QSlider(Qt::Horizontal, ui.verticalGroupBox2);
    ui.opacitySlider->setObjectName(QStringLiteral("opacitySlider"));
    opacityGrid->addWidget(ui.opacitySlider, 0, 2);

    blendLayout->addLayout(opacityGrid);
    controlsLayout->addWidget(ui.verticalGroupBox2);

    ui.blendParametersGroupBox = new QGroupBox(QStringLiteral("Blend parameters"), parent);
    ui.blendParametersGroupBox->setObjectName(QStringLiteral("blendParametersGroupBox"));
    auto* blendParamsGrid = new QGridLayout(ui.blendParametersGroupBox);
    blendParamsGrid->setSpacing(t.spacing.xs);
    blendParamsGrid->setContentsMargins(t.spacing.xs, t.spacing.xs, t.spacing.xs, t.spacing.xs);
    blendParamsGrid->setColumnStretch(1, 1); // combobox expands

    auto* blendModeLabel = new QLabel(QStringLiteral("Blend mode"), ui.blendParametersGroupBox);
    blendModeLabel->setObjectName(QStringLiteral("label"));
    blendModeLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    blendParamsGrid->addWidget(blendModeLabel, 0, 0);

    ui.blendMode = new QComboBox(ui.blendParametersGroupBox);
    ui.blendMode->setObjectName(QStringLiteral("blendMode"));
    ui.blendMode->setToolTip(QStringLiteral("Compositing mode used to combine base and blend images."));
    blendParamsGrid->addWidget(ui.blendMode, 0, 1);

    ui.preserveLightness = new QCheckBox(QStringLiteral("Preserve lightness"), ui.blendParametersGroupBox);
    ui.preserveLightness->setObjectName(QStringLiteral("preserveLightness"));
    ui.preserveLightness->setToolTip(QStringLiteral("Preserve the lightness of the base image after blending."));
    blendParamsGrid->addWidget(ui.preserveLightness, 1, 0, 1, 2);

    controlsLayout->addWidget(ui.blendParametersGroupBox);
    controlsLayout->addItem(new QSpacerItem(0, 0, QSizePolicy::Minimum, QSizePolicy::Expanding));

    auto* previewRow = new QHBoxLayout();
    ui.preview = new QCheckBox(QStringLiteral("Preview"), parent);
    ui.preview->setObjectName(QStringLiteral("preview"));
    ui.preview->setToolTip(QStringLiteral("Enable live preview of the blend result."));
    ui.preview->setChecked(true);
    previewRow->addWidget(ui.preview);
    previewRow->addItem(new QSpacerItem(40, 20, QSizePolicy::Expanding, QSizePolicy::Minimum));
    controlsLayout->addLayout(previewRow);

    auto* buttonRow = new QHBoxLayout();
    buttonRow->setObjectName(QStringLiteral("horizontalLayout_3"));
    buttonRow->setSizeConstraint(QLayout::SetDefaultConstraint);

    ui.buttonBox =
        new QDialogButtonBox(QDialogButtonBox::Close | QDialogButtonBox::Ok | QDialogButtonBox::Reset, parent);
    ui.buttonBox->setObjectName(QStringLiteral("buttonBox"));
    if (auto* btn = ui.buttonBox->button(QDialogButtonBox::Close))
        btn->setToolTip(QStringLiteral("Close this dialog without applying any changes."));
    if (auto* btn = ui.buttonBox->button(QDialogButtonBox::Reset))
        btn->setToolTip(QStringLiteral("Reset all parameters to their default values."));
    if (auto* btn = ui.buttonBox->button(QDialogButtonBox::Ok))
        btn->setToolTip(QStringLiteral("Apply the current blend settings to the image."));
    buttonRow->addWidget(ui.buttonBox);

    controlsLayout->addLayout(buttonRow);

    return ui;
}

} // namespace an::ui::processes
