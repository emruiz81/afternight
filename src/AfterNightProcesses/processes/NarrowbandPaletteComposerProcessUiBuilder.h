// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "processes/NarrowbandPaletteComposerPreviewGridWidget.h"
#include "theme/AnDesignTokens.h"
#include "widgets/waitingspinnerwidget.h"

#include <AfterNightEngine/Algorithms/Color/NarrowbandPalette.h>

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QSize>
#include <QSizePolicy>
#include <QSlider>
#include <QSpinBox>
#include <QSplitter>
#include <QStackedWidget>
#include <QVBoxLayout>
#include <QWidget>

namespace an::ui::processes
{

inline constexpr int kNarrowbandPaletteComposerInitialWindowWidth = 1180;
inline constexpr int kNarrowbandPaletteComposerInitialWindowHeight = 860;
inline constexpr int kNarrowbandPaletteComposerMinimumWindowWidth = 880;
inline constexpr int kNarrowbandPaletteComposerMinimumWindowHeight = 720;
inline constexpr int kNarrowbandPaletteComposerInitialControlsPaneWidth = 420;
inline constexpr int kNarrowbandPaletteComposerInitialPreviewPaneWidth = 760;
inline constexpr int kNarrowbandPaletteComposerMinimumPreviewPaneWidth = 240;

struct NarrowbandPaletteComposerProcessUiElements
{
    QHBoxLayout* horizontalLayoutMain = nullptr;
    QScrollArea* controlsScrollArea = nullptr;
    QVBoxLayout* previewLayout = nullptr;

    QGroupBox* sourcesGroup = nullptr;
    QGroupBox* optionsGroup = nullptr;
    QGroupBox* previewModeGroup = nullptr;
    QGroupBox* paletteGroup = nullptr;
    QGroupBox* intensityGroup = nullptr;
    QGroupBox* statusGroup = nullptr;

    QComboBox* haSourceSelector = nullptr;
    QComboBox* oiiiSourceSelector = nullptr;
    QComboBox* siiSourceSelector = nullptr;
    QComboBox* osc1SourceSelector = nullptr;
    QComboBox* osc2SourceSelector = nullptr;

    QCheckBox* linearInputStretch = nullptr;
    QCheckBox* resizeMismatched = nullptr;

    QComboBox* previewModeSelector = nullptr;
    QStackedWidget* previewStack = nullptr;
    NarrowbandPaletteComposerPreviewGridWidget* paletteGrid = nullptr;
    NarrowbandPaletteComposerSinglePreviewWidget* singlePreview = nullptr;
    WaitingSpinnerWidget* paletteGridBusySpinner = nullptr;

    QSlider* haIntensitySlider = nullptr;
    QSlider* oiiiIntensitySlider = nullptr;
    QSlider* siiIntensitySlider = nullptr;
    QSpinBox* haIntensitySpin = nullptr;
    QSpinBox* oiiiIntensitySpin = nullptr;
    QSpinBox* siiIntensitySpin = nullptr;

    QLabel* statusLabel = nullptr;
    QLabel* warningLabel = nullptr;
    QDialogButtonBox* buttonBox = nullptr;
};

inline QComboBox* makeNarrowbandPaletteComposerSourceSelector(QWidget* parent, const QString& objectName)
{
    auto* selector = new QComboBox(parent);
    selector->setObjectName(objectName);
    selector->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    selector->setMinimumContentsLength(24);
    return selector;
}

inline QSlider* makeNarrowbandPaletteComposerIntensitySlider(QWidget* parent, const QString& objectName)
{
    auto* slider = new QSlider(Qt::Horizontal, parent);
    slider->setObjectName(objectName);
    slider->setRange(0, 200);
    slider->setSingleStep(1);
    slider->setPageStep(10);
    slider->setTracking(false);
    slider->setValue(100);
    return slider;
}

inline QSpinBox* makeNarrowbandPaletteComposerIntensitySpin(QWidget* parent, const QString& objectName)
{
    auto* spin = new QSpinBox(parent);
    spin->setObjectName(objectName);
    spin->setRange(0, 200);
    spin->setSingleStep(1);
    spin->setSuffix(QStringLiteral("%"));
    spin->setValue(100);
    return spin;
}

inline void applyNarrowbandPaletteComposerToolTips(const NarrowbandPaletteComposerProcessUiElements& ui)
{
    if (ui.haSourceSelector)
    {
        ui.haSourceSelector->setToolTip(QStringLiteral(
            "Hydrogen-alpha source. Mono is expected; RGB sources use their red channel to match SetiAstro."));
    }
    if (ui.oiiiSourceSelector)
    {
        ui.oiiiSourceSelector->setToolTip(QStringLiteral(
            "Oxygen III source. The palette build needs OIII plus Ha or SII after optional OSC synthesis."));
    }
    if (ui.siiSourceSelector)
    {
        ui.siiSourceSelector->setToolTip(
            QStringLiteral("Sulfur II source. If SII is absent, Ha can substitute it for palettes that need SII."));
    }
    if (ui.osc1SourceSelector)
    {
        ui.osc1SourceSelector->setToolTip(QStringLiteral(
            "Optional OSC helper source. Red contributes Ha and the average of green and blue contributes OIII."));
    }
    if (ui.osc2SourceSelector)
    {
        ui.osc2SourceSelector->setToolTip(QStringLiteral(
            "Optional OSC helper source. Red contributes SII and the average of green and blue contributes OIII."));
    }
    if (ui.linearInputStretch)
    {
        ui.linearInputStretch->setToolTip(QStringLiteral("Apply AfterNight's unlinked autostretch before palette "
                                                         "mapping. Leave this on for linear narrowband data."));
    }
    if (ui.resizeMismatched)
    {
        ui.resizeMismatched->setToolTip(QStringLiteral(
            "Resize mismatched channels to the selected reference dimensions instead of rejecting the palette build."));
    }
    if (ui.previewModeSelector)
    {
        ui.previewModeSelector->setToolTip(
            QStringLiteral("Choose between the synchronized palette grid and a large single-palette preview."));
    }
    if (ui.paletteGrid)
    {
        ui.paletteGrid->setToolTip(
            QStringLiteral("Select the palette to apply. The synchronized grid updates when controls change."));
    }
    if (ui.singlePreview)
    {
        ui.singlePreview->setToolTip(QStringLiteral(
            "Inspect the selected palette at high resolution and choose palettes from the bottom gallery."));
    }
    if (ui.paletteGridBusySpinner)
    {
        ui.paletteGridBusySpinner->setToolTip(QStringLiteral("The synchronized palette grid is updating."));
    }
    if (ui.haIntensitySlider)
    {
        ui.haIntensitySlider->setToolTip(
            QStringLiteral("Hydrogen-alpha intensity multiplier. 100% keeps the prepared Ha channel unchanged."));
    }
    if (ui.oiiiIntensitySlider)
    {
        ui.oiiiIntensitySlider->setToolTip(QStringLiteral(
            "Oxygen III intensity multiplier. Increase it to push teal and blue signal in the selected palette."));
    }
    if (ui.siiIntensitySlider)
    {
        ui.siiIntensitySlider->setToolTip(QStringLiteral(
            "Sulfur II intensity multiplier. Increase it to strengthen SII-driven red or green palette components."));
    }
    if (ui.haIntensitySpin)
    {
        ui.haIntensitySpin->setToolTip(ui.haIntensitySlider ? ui.haIntensitySlider->toolTip() : QString());
    }
    if (ui.oiiiIntensitySpin)
    {
        ui.oiiiIntensitySpin->setToolTip(ui.oiiiIntensitySlider ? ui.oiiiIntensitySlider->toolTip() : QString());
    }
    if (ui.siiIntensitySpin)
    {
        ui.siiIntensitySpin->setToolTip(ui.siiIntensitySlider ? ui.siiIntensitySlider->toolTip() : QString());
    }
    if (ui.statusLabel)
    {
        ui.statusLabel->setToolTip(QStringLiteral("Readiness summary for the currently selected sources."));
    }
    if (ui.warningLabel)
    {
        ui.warningLabel->setToolTip(QStringLiteral(
            "Non-fatal preparation notes, such as RGB narrowband extraction, OSC mono broadcast, or resizing."));
    }
    if (ui.buttonBox)
    {
        if (auto* apply = ui.buttonBox->button(QDialogButtonBox::Apply))
        {
            apply->setToolTip(QStringLiteral("Create a new RGB image view from the selected palette."));
        }
        if (auto* reset = ui.buttonBox->button(QDialogButtonBox::Reset))
        {
            reset->setToolTip(QStringLiteral("Restore palette, stretch, resize, and intensity defaults."));
        }
        if (auto* close = ui.buttonBox->button(QDialogButtonBox::Close))
        {
            close->setToolTip(QStringLiteral("Close the Narrowband Palette Composer process window."));
        }
    }
}

inline NarrowbandPaletteComposerProcessUiElements buildNarrowbandPaletteComposerProcessUi(QWidget* parent)
{
    NarrowbandPaletteComposerProcessUiElements ui;
    const auto& t = an::ui::tokens();

    parent->setObjectName(QStringLiteral("NarrowbandPaletteComposerProcess"));
    parent->setWindowTitle(QStringLiteral("Narrowband Palette Composer"));
    parent->resize(kNarrowbandPaletteComposerInitialWindowWidth, kNarrowbandPaletteComposerInitialWindowHeight);
    parent->setMinimumSize(kNarrowbandPaletteComposerMinimumWindowWidth, kNarrowbandPaletteComposerMinimumWindowHeight);

    ui.horizontalLayoutMain = new QHBoxLayout(parent);
    ui.horizontalLayoutMain->setObjectName(QStringLiteral("horizontalLayoutMain"));
    ui.horizontalLayoutMain->setContentsMargins(0, 0, 0, 0);
    ui.horizontalLayoutMain->setSpacing(t.spacing.md);

    auto* splitter = new QSplitter(Qt::Horizontal, parent);
    splitter->setObjectName(QStringLiteral("mainSplitter"));
    splitter->setChildrenCollapsible(false);
    ui.horizontalLayoutMain->addWidget(splitter);

    ui.controlsScrollArea = new QScrollArea(parent);
    ui.controlsScrollArea->setObjectName(QStringLiteral("controlsScrollArea"));
    ui.controlsScrollArea->setWidgetResizable(true);
    ui.controlsScrollArea->setFrameShape(QFrame::NoFrame);
    ui.controlsScrollArea->setMinimumWidth(kNarrowbandPaletteComposerInitialControlsPaneWidth);
    ui.controlsScrollArea->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);

    auto* controlsPanel = new QWidget(ui.controlsScrollArea);
    controlsPanel->setObjectName(QStringLiteral("controlsPanel"));
    auto* controlsLayout = new QVBoxLayout(controlsPanel);
    controlsLayout->setContentsMargins(t.spacing.sm, t.spacing.sm, t.spacing.sm, t.spacing.sm);
    controlsLayout->setSpacing(t.spacing.md);

    ui.sourcesGroup = new QGroupBox(QStringLiteral("Sources"), controlsPanel);
    auto* sourcesLayout = new QFormLayout(ui.sourcesGroup);
    sourcesLayout->setContentsMargins(t.spacing.md, t.spacing.md, t.spacing.md, t.spacing.md);
    sourcesLayout->setSpacing(t.spacing.sm);
    ui.haSourceSelector =
        makeNarrowbandPaletteComposerSourceSelector(ui.sourcesGroup, QStringLiteral("haSourceSelector"));
    ui.oiiiSourceSelector =
        makeNarrowbandPaletteComposerSourceSelector(ui.sourcesGroup, QStringLiteral("oiiiSourceSelector"));
    ui.siiSourceSelector =
        makeNarrowbandPaletteComposerSourceSelector(ui.sourcesGroup, QStringLiteral("siiSourceSelector"));
    ui.osc1SourceSelector =
        makeNarrowbandPaletteComposerSourceSelector(ui.sourcesGroup, QStringLiteral("osc1SourceSelector"));
    ui.osc2SourceSelector =
        makeNarrowbandPaletteComposerSourceSelector(ui.sourcesGroup, QStringLiteral("osc2SourceSelector"));
    sourcesLayout->addRow(QStringLiteral("Ha"), ui.haSourceSelector);
    sourcesLayout->addRow(QStringLiteral("OIII"), ui.oiiiSourceSelector);
    sourcesLayout->addRow(QStringLiteral("SII"), ui.siiSourceSelector);
    sourcesLayout->addRow(QStringLiteral("OSC1"), ui.osc1SourceSelector);
    sourcesLayout->addRow(QStringLiteral("OSC2"), ui.osc2SourceSelector);
    controlsLayout->addWidget(ui.sourcesGroup);

    ui.optionsGroup = new QGroupBox(QStringLiteral("Preparation"), controlsPanel);
    auto* optionsLayout = new QVBoxLayout(ui.optionsGroup);
    optionsLayout->setContentsMargins(t.spacing.md, t.spacing.md, t.spacing.md, t.spacing.md);
    optionsLayout->setSpacing(t.spacing.sm);
    ui.linearInputStretch = new QCheckBox(QStringLiteral("Input autostretch"), ui.optionsGroup);
    ui.linearInputStretch->setObjectName(QStringLiteral("linearInputStretch"));
    ui.linearInputStretch->setChecked(true);
    ui.resizeMismatched = new QCheckBox(QStringLiteral("Match sizes to reference"), ui.optionsGroup);
    ui.resizeMismatched->setObjectName(QStringLiteral("resizeMismatched"));
    ui.resizeMismatched->setChecked(true);
    optionsLayout->addWidget(ui.linearInputStretch);
    optionsLayout->addWidget(ui.resizeMismatched);
    controlsLayout->addWidget(ui.optionsGroup);

    ui.previewModeGroup = new QGroupBox(QStringLiteral("Preview"), controlsPanel);
    auto* previewModeLayout = new QFormLayout(ui.previewModeGroup);
    previewModeLayout->setContentsMargins(t.spacing.md, t.spacing.md, t.spacing.md, t.spacing.md);
    previewModeLayout->setSpacing(t.spacing.sm);
    ui.previewModeSelector = new QComboBox(ui.previewModeGroup);
    ui.previewModeSelector->setObjectName(QStringLiteral("previewModeSelector"));
    ui.previewModeSelector->addItem(QStringLiteral("Palette Grid"));
    ui.previewModeSelector->addItem(QStringLiteral("Single Preview"));
    previewModeLayout->addRow(QStringLiteral("Mode"), ui.previewModeSelector);
    controlsLayout->addWidget(ui.previewModeGroup);

    ui.intensityGroup = new QGroupBox(QStringLiteral("Adjust Selected Palette"), controlsPanel);
    auto* intensityLayout = new QFormLayout(ui.intensityGroup);
    intensityLayout->setContentsMargins(t.spacing.md, t.spacing.md, t.spacing.md, t.spacing.md);
    intensityLayout->setSpacing(t.spacing.sm);

    auto addIntensityRow = [&](const QString& label, QSlider*& slider, QSpinBox*& spin, const QString& sliderName,
                               const QString& spinName) {
        auto* row = new QWidget(ui.intensityGroup);
        auto* rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(0, 0, 0, 0);
        rowLayout->setSpacing(t.spacing.sm);
        slider = makeNarrowbandPaletteComposerIntensitySlider(row, sliderName);
        spin = makeNarrowbandPaletteComposerIntensitySpin(row, spinName);
        rowLayout->addWidget(slider, 1);
        rowLayout->addWidget(spin);
        intensityLayout->addRow(label, row);
    };

    addIntensityRow(QStringLiteral("Ha"), ui.haIntensitySlider, ui.haIntensitySpin, QStringLiteral("haIntensitySlider"),
                    QStringLiteral("haIntensitySpin"));
    addIntensityRow(QStringLiteral("OIII"), ui.oiiiIntensitySlider, ui.oiiiIntensitySpin,
                    QStringLiteral("oiiiIntensitySlider"), QStringLiteral("oiiiIntensitySpin"));
    addIntensityRow(QStringLiteral("SII"), ui.siiIntensitySlider, ui.siiIntensitySpin,
                    QStringLiteral("siiIntensitySlider"), QStringLiteral("siiIntensitySpin"));
    controlsLayout->addWidget(ui.intensityGroup);

    ui.statusGroup = new QGroupBox(QStringLiteral("Status"), controlsPanel);
    auto* statusLayout = new QVBoxLayout(ui.statusGroup);
    statusLayout->setContentsMargins(t.spacing.md, t.spacing.md, t.spacing.md, t.spacing.md);
    statusLayout->setSpacing(t.spacing.sm);
    ui.statusLabel = new QLabel(QStringLiteral("Select OIII plus Ha or SII to build palettes."), ui.statusGroup);
    ui.statusLabel->setObjectName(QStringLiteral("statusLabel"));
    ui.statusLabel->setWordWrap(true);
    ui.warningLabel = new QLabel(ui.statusGroup);
    ui.warningLabel->setObjectName(QStringLiteral("warningLabel"));
    ui.warningLabel->setWordWrap(true);
    statusLayout->addWidget(ui.statusLabel);
    statusLayout->addWidget(ui.warningLabel);
    controlsLayout->addWidget(ui.statusGroup);

    ui.buttonBox = new QDialogButtonBox(QDialogButtonBox::Apply | QDialogButtonBox::Close | QDialogButtonBox::Reset,
                                        controlsPanel);
    ui.buttonBox->setObjectName(QStringLiteral("buttonBox"));
    controlsLayout->addWidget(ui.buttonBox);
    controlsLayout->addStretch(1);

    ui.controlsScrollArea->setWidget(controlsPanel);
    splitter->addWidget(ui.controlsScrollArea);

    auto* previewPanel = new QWidget(parent);
    previewPanel->setObjectName(QStringLiteral("previewPanel"));
    auto* previewColumnLayout = new QVBoxLayout(previewPanel);
    previewColumnLayout->setContentsMargins(t.spacing.sm, t.spacing.sm, t.spacing.sm, t.spacing.sm);
    previewColumnLayout->setSpacing(t.spacing.md);

    ui.paletteGroup = new QGroupBox(QStringLiteral("Palette Grid"), previewPanel);
    auto* paletteLayout = new QVBoxLayout(ui.paletteGroup);
    paletteLayout->setContentsMargins(t.spacing.md, t.spacing.md, t.spacing.md, t.spacing.md);
    paletteLayout->setSpacing(t.spacing.sm);
    ui.previewLayout = paletteLayout;
    ui.previewLayout->setProperty("anKeepPreviewLayoutMargins", true);

    ui.previewStack = new QStackedWidget(ui.paletteGroup);
    ui.previewStack->setObjectName(QStringLiteral("previewStack"));

    auto* paletteScrollArea = new QScrollArea(ui.previewStack);
    paletteScrollArea->setObjectName(QStringLiteral("paletteGridScrollArea"));
    paletteScrollArea->setWidgetResizable(true);
    paletteScrollArea->setFrameShape(QFrame::NoFrame);
    ui.paletteGrid = new NarrowbandPaletteComposerPreviewGridWidget(paletteScrollArea);
    ui.paletteGrid->setPaletteDescriptors(
        std::vector<an::NarrowbandPaletteDescriptor>(an::narrowbandPalettes().begin(), an::narrowbandPalettes().end()));
    paletteScrollArea->setWidget(ui.paletteGrid);
    ui.previewStack->addWidget(paletteScrollArea);

    ui.singlePreview = new NarrowbandPaletteComposerSinglePreviewWidget(ui.previewStack);
    ui.singlePreview->setPaletteDescriptors(
        std::vector<an::NarrowbandPaletteDescriptor>(an::narrowbandPalettes().begin(), an::narrowbandPalettes().end()));
    ui.previewStack->addWidget(ui.singlePreview);
    ui.previewStack->setCurrentIndex(0);

    ui.paletteGridBusySpinner = new WaitingSpinnerWidget(ui.previewStack, true, false);
    ui.paletteGridBusySpinner->setObjectName(QStringLiteral("paletteGridBusySpinner"));
    paletteLayout->addWidget(ui.previewStack);
    previewColumnLayout->addWidget(ui.paletteGroup, 1);

    splitter->addWidget(previewPanel);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes(
        {kNarrowbandPaletteComposerInitialControlsPaneWidth, kNarrowbandPaletteComposerInitialPreviewPaneWidth});

    applyNarrowbandPaletteComposerToolTips(ui);
    return ui;
}

} // namespace an::ui::processes
