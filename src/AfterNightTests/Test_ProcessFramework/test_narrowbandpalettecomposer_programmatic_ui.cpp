// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The Narrowband Palette Composer dialog as its builder makes it, and its palette grid and single preview: each keeps
/// the frames it is given and the palette selected, and the single preview tells its owner when the selection changes.

#include <gtest/gtest.h>

#include <QAbstractButton>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QGroupBox>
#include <QImage>
#include <QSlider>
#include <QSpinBox>
#include <QSplitter>
#include <QStackedWidget>
#include <QWidget>

#include <utility>
#include <vector>

#include "processes/NarrowbandPaletteComposerPreviewGridWidget.h"
#include "processes/NarrowbandPaletteComposerProcessUiBuilder.h"
#include "theme/AnDesignTokens.h"

#include <AfterNightEngine/Algorithms/Color/NarrowbandPalette.h>

namespace
{

/// The engine's palettes, as the window hands them to its previews.
std::vector<an::NarrowbandPaletteDescriptor> paletteDescriptors()
{
    const auto palettes = an::narrowbandPalettes();
    return std::vector<an::NarrowbandPaletteDescriptor>(palettes.begin(), palettes.end());
}

void expectHasToolTip(const QWidget* widget)
{
    ASSERT_NE(widget, nullptr);
    EXPECT_FALSE(widget->toolTip().isEmpty()) << widget->objectName().toStdString();
}

} // namespace

// The dialog opens at its size with its sources, its palette grid showing no palette yet and SHO selected, its
// intensities and its preparation, and a tooltip on every control.
TEST(NarrowbandPaletteComposerProgrammaticUiTest, ConstructorDefaultsMatchNativeProcessContract)
{
    const auto& t = an::ui::tokens();

    QWidget host;
    const auto ui = an::ui::processes::buildNarrowbandPaletteComposerProcessUi(&host);

    ASSERT_NE(ui.horizontalLayoutMain, nullptr);
    ASSERT_NE(ui.controlsScrollArea, nullptr);
    ASSERT_NE(ui.previewLayout, nullptr);
    ASSERT_NE(ui.sourcesGroup, nullptr);
    ASSERT_NE(ui.optionsGroup, nullptr);
    ASSERT_NE(ui.paletteGroup, nullptr);
    ASSERT_NE(ui.intensityGroup, nullptr);
    ASSERT_NE(ui.statusGroup, nullptr);
    ASSERT_NE(ui.haSourceSelector, nullptr);
    ASSERT_NE(ui.oiiiSourceSelector, nullptr);
    ASSERT_NE(ui.siiSourceSelector, nullptr);
    ASSERT_NE(ui.osc1SourceSelector, nullptr);
    ASSERT_NE(ui.osc2SourceSelector, nullptr);
    ASSERT_NE(ui.linearInputStretch, nullptr);
    ASSERT_NE(ui.resizeMismatched, nullptr);
    ASSERT_NE(ui.previewModeSelector, nullptr);
    ASSERT_NE(ui.previewStack, nullptr);
    ASSERT_NE(ui.paletteGrid, nullptr);
    ASSERT_NE(ui.singlePreview, nullptr);
    ASSERT_NE(ui.paletteGridBusySpinner, nullptr);
    ASSERT_NE(ui.buttonBox, nullptr);

    EXPECT_EQ(host.objectName(), QStringLiteral("NarrowbandPaletteComposerProcess"));
    EXPECT_EQ(host.windowTitle(), QStringLiteral("Narrowband Palette Composer"));
    EXPECT_GE(host.width(), an::ui::processes::kNarrowbandPaletteComposerInitialWindowWidth);
    EXPECT_GE(host.height(), an::ui::processes::kNarrowbandPaletteComposerInitialWindowHeight);
    EXPECT_GE(host.minimumWidth(), an::ui::processes::kNarrowbandPaletteComposerMinimumWindowWidth);
    EXPECT_GE(host.minimumHeight(), an::ui::processes::kNarrowbandPaletteComposerMinimumWindowHeight);
    EXPECT_EQ(ui.horizontalLayoutMain->spacing(), t.spacing.md);

    EXPECT_EQ(ui.sourcesGroup->title(), QStringLiteral("Sources"));
    EXPECT_EQ(ui.optionsGroup->title(), QStringLiteral("Preparation"));
    EXPECT_EQ(ui.paletteGroup->title(), QStringLiteral("Palette Grid"));
    EXPECT_EQ(ui.intensityGroup->title(), QStringLiteral("Adjust Selected Palette"));
    EXPECT_EQ(ui.statusGroup->title(), QStringLiteral("Status"));
    EXPECT_EQ(ui.previewModeSelector->count(), 2);
    EXPECT_EQ(ui.previewModeSelector->itemText(0), QStringLiteral("Palette Grid"));
    EXPECT_EQ(ui.previewModeSelector->itemText(1), QStringLiteral("Single Preview"));
    EXPECT_EQ(ui.previewModeSelector->currentIndex(), 0);
    EXPECT_EQ(ui.previewStack->count(), 2);
    EXPECT_EQ(ui.previewStack->currentIndex(), 0);

    EXPECT_TRUE(ui.linearInputStretch->isChecked());
    EXPECT_TRUE(ui.resizeMismatched->isChecked());
    EXPECT_EQ(ui.haIntensitySlider->minimum(), 0);
    EXPECT_EQ(ui.haIntensitySlider->maximum(), 200);
    EXPECT_EQ(ui.haIntensitySlider->value(), 100);
    EXPECT_FALSE(ui.haIntensitySlider->hasTracking());
    EXPECT_FALSE(ui.oiiiIntensitySlider->hasTracking());
    EXPECT_FALSE(ui.siiIntensitySlider->hasTracking());
    EXPECT_EQ(ui.oiiiIntensitySpin->suffix(), QStringLiteral("%"));
    EXPECT_EQ(ui.siiIntensitySpin->value(), 100);
    EXPECT_EQ(ui.paletteGrid->objectName(), QStringLiteral("paletteGrid"));
    EXPECT_FALSE(ui.paletteGrid->hasPreviewImages());
    EXPECT_EQ(ui.paletteGrid->placeholderText(), QStringLiteral("Select sources to update the palette grid"));
    EXPECT_EQ(ui.paletteGrid->selectedPalette(), an::NarrowbandPalette::Sho);
    EXPECT_EQ(ui.singlePreview->objectName(), QStringLiteral("singlePreview"));
    EXPECT_FALSE(ui.singlePreview->hasPreviewImages());
    EXPECT_EQ(ui.singlePreview->placeholderText(), QStringLiteral("Select sources to update the palette grid"));
    EXPECT_EQ(ui.singlePreview->selectedPalette(), an::NarrowbandPalette::Sho);
    EXPECT_EQ(ui.paletteGridBusySpinner->objectName(), QStringLiteral("paletteGridBusySpinner"));
    EXPECT_FALSE(ui.paletteGridBusySpinner->isSpinning());

    auto* splitter = host.findChild<QSplitter*>(QStringLiteral("mainSplitter"));
    ASSERT_NE(splitter, nullptr);
    ASSERT_EQ(splitter->count(), 2);
    auto* previewPane = splitter->widget(1);
    ASSERT_NE(previewPane, nullptr);
    EXPECT_EQ(previewPane->objectName(), QStringLiteral("previewPanel"));
    host.show();
    QApplication::processEvents();
    const QList<int> splitterSizes = splitter->sizes();
    ASSERT_GE(splitterSizes.size(), 2);
    EXPECT_GT(splitterSizes[1], 0);
    EXPECT_TRUE(previewPane->isVisibleTo(&host));

    expectHasToolTip(ui.haSourceSelector);
    expectHasToolTip(ui.oiiiSourceSelector);
    expectHasToolTip(ui.siiSourceSelector);
    expectHasToolTip(ui.osc1SourceSelector);
    expectHasToolTip(ui.osc2SourceSelector);
    expectHasToolTip(ui.linearInputStretch);
    expectHasToolTip(ui.resizeMismatched);
    expectHasToolTip(ui.previewModeSelector);
    expectHasToolTip(ui.paletteGrid);
    expectHasToolTip(ui.singlePreview);
    expectHasToolTip(ui.paletteGridBusySpinner);
    expectHasToolTip(ui.haIntensitySlider);
    expectHasToolTip(ui.oiiiIntensitySlider);
    expectHasToolTip(ui.siiIntensitySlider);
    expectHasToolTip(ui.haIntensitySpin);
    expectHasToolTip(ui.oiiiIntensitySpin);
    expectHasToolTip(ui.siiIntensitySpin);
    expectHasToolTip(ui.statusLabel);
    expectHasToolTip(ui.warningLabel);

    EXPECT_NE(ui.buttonBox->button(QDialogButtonBox::Apply), nullptr);
    EXPECT_NE(ui.buttonBox->button(QDialogButtonBox::Close), nullptr);
    EXPECT_NE(ui.buttonBox->button(QDialogButtonBox::Reset), nullptr);
    expectHasToolTip(ui.buttonBox->button(QDialogButtonBox::Apply));
    expectHasToolTip(ui.buttonBox->button(QDialogButtonBox::Close));
    expectHasToolTip(ui.buttonBox->button(QDialogButtonBox::Reset));
}

// The grid keeps the frames it is given and the palette selected, and says what to do once it holds none.
TEST(NarrowbandPaletteComposerProgrammaticUiTest, PreviewGridStoresFullPreviewFramesAndSharedSelection)
{
    an::ui::processes::NarrowbandPaletteComposerPreviewGridWidget grid;
    grid.setPaletteDescriptors(paletteDescriptors());

    std::vector<an::ui::processes::NarrowbandPaletteComposerPreviewFrame> frames;
    frames.push_back(
        {an::NarrowbandPalette::Sho, QStringLiteral("SHO"), QStringLiteral(""), QImage(4, 4, QImage::Format_RGB888)});
    frames.push_back(
        {an::NarrowbandPalette::Hoo, QStringLiteral("HOO"), QStringLiteral(""), QImage(4, 4, QImage::Format_RGB888)});

    grid.setPreviewImages(std::move(frames));

    EXPECT_TRUE(grid.hasPreviewImages());
    EXPECT_EQ(grid.selectedPalette(), an::NarrowbandPalette::Sho);

    grid.setSelectedPalette(an::NarrowbandPalette::Hoo);
    EXPECT_EQ(grid.selectedPalette(), an::NarrowbandPalette::Hoo);

    grid.clearPreviewImages();
    EXPECT_FALSE(grid.hasPreviewImages());
    EXPECT_EQ(grid.placeholderText(), QStringLiteral("Select sources to update the palette grid"));
}

// The single preview keeps the frames it is given, tells its owner the palette selected, and says what to do once it
// holds none.
TEST(NarrowbandPaletteComposerProgrammaticUiTest, SinglePreviewStoresFramesAndSharesSelection)
{
    an::ui::processes::NarrowbandPaletteComposerSinglePreviewWidget preview;
    preview.setPaletteDescriptors(paletteDescriptors());

    std::vector<an::ui::processes::NarrowbandPaletteComposerPreviewFrame> frames;
    frames.push_back(
        {an::NarrowbandPalette::Sho, QStringLiteral("SHO"), QStringLiteral(""), QImage(4, 4, QImage::Format_RGB888)});
    frames.push_back(
        {an::NarrowbandPalette::Hoo, QStringLiteral("HOO"), QStringLiteral(""), QImage(4, 4, QImage::Format_RGB888)});

    an::NarrowbandPalette selected = an::NarrowbandPalette::Sho;
    preview.setSelectionChangedHandler([&selected](an::NarrowbandPalette palette) { selected = palette; });
    preview.setPreviewImages(std::move(frames));

    EXPECT_TRUE(preview.hasPreviewImages());
    EXPECT_EQ(preview.selectedPalette(), an::NarrowbandPalette::Sho);

    preview.setSelectedPalette(an::NarrowbandPalette::Hoo);
    EXPECT_EQ(preview.selectedPalette(), an::NarrowbandPalette::Hoo);
    EXPECT_EQ(selected, an::NarrowbandPalette::Hoo);

    preview.clearPreviewImages();
    EXPECT_FALSE(preview.hasPreviewImages());
    EXPECT_EQ(preview.placeholderText(), QStringLiteral("Select sources to update the palette grid"));
}
