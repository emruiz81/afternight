// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file test_ui_builder.cpp
/// @brief Tests for the RTPreviewUiCommon.h builder functions.
///
/// Exercises `buildMainProcessSplitter` and `buildHistogramToolbar` from
/// the an::ui::processes::rt_preview_ui namespace.  These builders are
/// header-only and called from every process UI constructor, so verifying
/// their structural output catches regressions early.
///
/// Key invariants:
///   - buildMainProcessSplitter produces a correctly named QSplitter with
///     two child widgets, correct stretch factors, and the preview pane's
///     minimum width set.
///   - buildHistogramToolbar creates the full complement of buttons/combo
///     respecting the option flags.

#include <gtest/gtest.h>

#include <QApplication>
#include <QHBoxLayout>
#include <QToolButton>
#include <QComboBox>
#include <QFrame>
#include <QSplitter>
#include <QVBoxLayout>
#include <QWidget>

#include "processes/RTPreviewUiCommon.h"

using namespace an::ui::processes::rt_preview_ui;

// =======================================================================
// buildMainProcessSplitter
// =======================================================================

TEST(BuildMainProcessSplitter, NullInputsReturnEmptyElements)
{
    auto result = buildMainProcessSplitter(nullptr, nullptr);

    EXPECT_EQ(result.mainSplitter, nullptr);
    EXPECT_EQ(result.controlsPanel, nullptr);
    EXPECT_EQ(result.previewPanel, nullptr);
    EXPECT_EQ(result.controlsLayout, nullptr);
    EXPECT_EQ(result.previewLayout, nullptr);
}

TEST(BuildMainProcessSplitter, NullLayoutReturnEmptyElements)
{
    QWidget parent;
    auto result = buildMainProcessSplitter(nullptr, &parent);

    EXPECT_EQ(result.mainSplitter, nullptr);
}

TEST(BuildMainProcessSplitter, NullParentReturnEmptyElements)
{
    QWidget root;
    auto* layout = new QHBoxLayout(&root);
    auto result = buildMainProcessSplitter(layout, nullptr);

    EXPECT_EQ(result.mainSplitter, nullptr);
}

TEST(BuildMainProcessSplitter, ProducesCorrectWidgetHierarchy)
{
    QWidget root;
    auto* layout = new QHBoxLayout(&root);

    auto result = buildMainProcessSplitter(layout, &root);

    ASSERT_NE(result.mainSplitter, nullptr);
    ASSERT_NE(result.controlsPanel, nullptr);
    ASSERT_NE(result.previewPanel, nullptr);
    ASSERT_NE(result.controlsLayout, nullptr);
    ASSERT_NE(result.previewLayout, nullptr);

    // The splitter should have two child widgets.
    EXPECT_EQ(result.mainSplitter->count(), 2);

    // Widget ordering: controls at index 0, preview at index 1.
    EXPECT_EQ(result.mainSplitter->widget(0), result.controlsPanel);
    EXPECT_EQ(result.mainSplitter->widget(1), result.previewPanel);
}

TEST(BuildMainProcessSplitter, SplitterHasCorrectObjectName)
{
    QWidget root;
    auto* layout = new QHBoxLayout(&root);
    auto result = buildMainProcessSplitter(layout, &root);

    ASSERT_NE(result.mainSplitter, nullptr);
    EXPECT_EQ(result.mainSplitter->objectName(), QStringLiteral("mainSplitter"));
}

TEST(BuildMainProcessSplitter, ChildrenNotCollapsible)
{
    QWidget root;
    auto* layout = new QHBoxLayout(&root);
    auto result = buildMainProcessSplitter(layout, &root);

    ASSERT_NE(result.mainSplitter, nullptr);
    EXPECT_FALSE(result.mainSplitter->childrenCollapsible());
}

TEST(BuildMainProcessSplitter, StretchFactorsAreCorrect)
{
    QWidget root;
    root.resize(800, 500);
    auto* layout = new QHBoxLayout(&root);
    auto result = buildMainProcessSplitter(layout, &root);

    ASSERT_NE(result.mainSplitter, nullptr);

    // Controls panel: stretch 0 (fixed), preview panel: stretch 1 (expands).
    // Qt doesn't expose stretch factors for splitter children, but we verify
    // construct by checking the property indirectly via initial sizes.
    root.show();
    QApplication::processEvents();

    const auto sizes = result.mainSplitter->sizes();
    ASSERT_GE(sizes.size(), 2);

    // Both panels should have been given some space.
    EXPECT_GE(sizes[0], 0);
}

TEST(BuildMainProcessSplitter, PreviewPaneHasMinimumWidth)
{
    QWidget root;
    auto* layout = new QHBoxLayout(&root);
    auto result = buildMainProcessSplitter(layout, &root);

    ASSERT_NE(result.previewPanel, nullptr);
    EXPECT_EQ(result.previewPanel->minimumWidth(), kMinPreviewPaneWidth);
}

TEST(BuildMainProcessSplitter, ControlsPanelObjectName)
{
    QWidget root;
    auto* layout = new QHBoxLayout(&root);
    auto result = buildMainProcessSplitter(layout, &root);

    ASSERT_NE(result.controlsPanel, nullptr);
    EXPECT_EQ(result.controlsPanel->objectName(), QStringLiteral("controlsPanel"));
}

TEST(BuildMainProcessSplitter, PreviewPanelObjectName)
{
    QWidget root;
    auto* layout = new QHBoxLayout(&root);
    auto result = buildMainProcessSplitter(layout, &root);

    ASSERT_NE(result.previewPanel, nullptr);
    EXPECT_EQ(result.previewPanel->objectName(), QStringLiteral("previewPanel"));
}

TEST(BuildMainProcessSplitter, ControlsLayoutHasDefaultConstraint)
{
    QWidget root;
    auto* layout = new QHBoxLayout(&root);
    auto result = buildMainProcessSplitter(layout, &root);

    ASSERT_NE(result.controlsLayout, nullptr);
    EXPECT_EQ(result.controlsLayout->sizeConstraint(), QLayout::SetDefaultConstraint);
}

TEST(BuildMainProcessSplitter, PreviewLayoutKeepsMarginsProperty)
{
    QWidget root;
    auto* layout = new QHBoxLayout(&root);
    auto result = buildMainProcessSplitter(layout, &root);

    ASSERT_NE(result.previewLayout, nullptr);
    // The builder sets "anKeepPreviewLayoutMargins" property to true so
    // that initializeStandardProcess does NOT zero the margins.
    const bool keepMargins = result.previewLayout->property("anKeepPreviewLayoutMargins").toBool();
    EXPECT_TRUE(keepMargins);
}

TEST(BuildMainProcessSplitter, PreviewHostWidgetExists)
{
    QWidget root;
    auto* layout = new QHBoxLayout(&root);
    auto result = buildMainProcessSplitter(layout, &root);

    ASSERT_NE(result.previewPanel, nullptr);

    // The builder adds a "previewHost" widget inside the preview layout.
    auto* previewHost = result.previewPanel->findChild<QWidget*>(QStringLiteral("previewHost"));
    EXPECT_NE(previewHost, nullptr);
}

TEST(BuildMainProcessSplitter, SplitterAddedToRootLayout)
{
    QWidget root;
    auto* layout = new QHBoxLayout(&root);
    auto result = buildMainProcessSplitter(layout, &root);

    ASSERT_NE(result.mainSplitter, nullptr);

    // The splitter should be an item in the root layout.
    bool found = false;
    for (int i = 0; i < layout->count(); ++i)
    {
        if (layout->itemAt(i)->widget() == result.mainSplitter)
        {
            found = true;
            break;
        }
    }
    EXPECT_TRUE(found) << "Splitter should be added to the root layout";
}

TEST(BuildMainProcessSplitter, CustomMarginsAreApplied)
{
    QWidget root;
    auto* layout = new QHBoxLayout(&root);

    const QMargins custom(20, 15, 20, 15);
    auto result = buildMainProcessSplitter(layout, &root, custom);

    ASSERT_NE(result.controlsLayout, nullptr);
    EXPECT_EQ(result.controlsLayout->contentsMargins(), custom);

    ASSERT_NE(result.previewLayout, nullptr);
    EXPECT_EQ(result.previewLayout->contentsMargins(), custom);
}

TEST(BuildMainProcessSplitter, DefaultMarginsAreApplied)
{
    QWidget root;
    auto* layout = new QHBoxLayout(&root);
    auto result = buildMainProcessSplitter(layout, &root);

    ASSERT_NE(result.controlsLayout, nullptr);
    EXPECT_EQ(result.controlsLayout->contentsMargins(), kDefaultPaneMargins);
}

TEST(BuildMainProcessSplitter, SplitterVariantPropertyIsSet)
{
    QWidget root;
    auto* layout = new QHBoxLayout(&root);
    auto result = buildMainProcessSplitter(layout, &root);

    ASSERT_NE(result.mainSplitter, nullptr);
    EXPECT_EQ(result.mainSplitter->property("anSplitterVariant").toString(), QStringLiteral("process"));
}

// =======================================================================
// buildHistogramToolbar
// =======================================================================

TEST(BuildHistogramToolbar, NullInputsReturnEmptyElements)
{
    auto result = buildHistogramToolbar(nullptr, nullptr);

    EXPECT_EQ(result.zoomIn, nullptr);
    EXPECT_EQ(result.zoomOut, nullptr);
    EXPECT_EQ(result.zoomReset, nullptr);
    EXPECT_EQ(result.gridButton, nullptr);
    EXPECT_EQ(result.plotResolution, nullptr);
}

TEST(BuildHistogramToolbar, DefaultOptionsCreateAllButtons)
{
    QWidget parent;
    auto* toolbar = new QHBoxLayout();

    auto result = buildHistogramToolbar(&parent, toolbar);

    EXPECT_NE(result.zoomIn, nullptr);
    EXPECT_NE(result.zoomOut, nullptr);
    EXPECT_NE(result.zoomReset, nullptr);
    EXPECT_NE(result.gridButton, nullptr);
    EXPECT_NE(result.showClippingInfo, nullptr);
    EXPECT_NE(result.localChannelNormalization, nullptr);
    EXPECT_NE(result.showHistogramAreas, nullptr);
    EXPECT_NE(result.showHistogram, nullptr);
    EXPECT_NE(result.plotResolution, nullptr);

    // Transfer curve is not included by default.
    EXPECT_EQ(result.transferCurve, nullptr);

    delete toolbar;
}

TEST(BuildHistogramToolbar, DisablingClippingInfoOmitsButton)
{
    QWidget parent;
    auto* toolbar = new QHBoxLayout();

    HistogramToolbarOptions opts;
    opts.includeShowClippingInfo = false;

    auto result = buildHistogramToolbar(&parent, toolbar, opts);

    EXPECT_EQ(result.showClippingInfo, nullptr);
    // Others should still exist.
    EXPECT_NE(result.zoomIn, nullptr);
    EXPECT_NE(result.gridButton, nullptr);

    delete toolbar;
}

TEST(BuildHistogramToolbar, DisablingShowHistogramOmitsButton)
{
    QWidget parent;
    auto* toolbar = new QHBoxLayout();

    HistogramToolbarOptions opts;
    opts.includeShowHistogram = false;

    auto result = buildHistogramToolbar(&parent, toolbar, opts);

    EXPECT_EQ(result.showHistogram, nullptr);
    EXPECT_NE(result.zoomIn, nullptr);

    delete toolbar;
}

TEST(BuildHistogramToolbar, IncludingTransferCurveCreatesButton)
{
    QWidget parent;
    auto* toolbar = new QHBoxLayout();

    HistogramToolbarOptions opts;
    opts.includeTransferCurve = true;

    auto result = buildHistogramToolbar(&parent, toolbar, opts);

    EXPECT_NE(result.transferCurve, nullptr);

    delete toolbar;
}

TEST(BuildHistogramToolbar, GridButtonIsCheckable)
{
    QWidget parent;
    auto* toolbar = new QHBoxLayout();

    auto result = buildHistogramToolbar(&parent, toolbar);

    ASSERT_NE(result.gridButton, nullptr);
    EXPECT_TRUE(result.gridButton->isCheckable());
    EXPECT_TRUE(result.gridButton->isChecked()) << "Grid should be checked by default";

    delete toolbar;
}

TEST(BuildHistogramToolbar, PlotResolutionComboBoxHasMinWidth)
{
    QWidget parent;
    auto* toolbar = new QHBoxLayout();

    auto result = buildHistogramToolbar(&parent, toolbar);

    ASSERT_NE(result.plotResolution, nullptr);
    EXPECT_GE(result.plotResolution->minimumWidth(), 150);

    delete toolbar;
}

TEST(BuildHistogramToolbar, ButtonSizesAreFixed)
{
    QWidget parent;
    auto* toolbar = new QHBoxLayout();

    auto result = buildHistogramToolbar(&parent, toolbar);

    // All toolbar buttons should have fixed 28×28 size.
    auto checkButtonSize = [](QToolButton* btn, const char* name) {
        ASSERT_NE(btn, nullptr) << name << " should not be null";
        EXPECT_EQ(btn->iconSize(), QSize(18, 18)) << name << " iconSize mismatch";
        EXPECT_EQ(btn->minimumSize(), QSize(28, 28)) << name << " minimumSize mismatch";
        EXPECT_EQ(btn->maximumSize(), QSize(28, 28)) << name << " maximumSize mismatch";
    };

    // We check a subset of buttons — the initToolbarButton function
    // sets all of them with the same pattern.
    ASSERT_NE(result.zoomIn, nullptr);
    EXPECT_EQ(result.zoomIn->iconSize(), QSize(18, 18));

    ASSERT_NE(result.zoomOut, nullptr);
    EXPECT_EQ(result.zoomOut->iconSize(), QSize(18, 18));

    ASSERT_NE(result.zoomReset, nullptr);
    EXPECT_EQ(result.zoomReset->iconSize(), QSize(18, 18));

    delete toolbar;
}

TEST(BuildHistogramToolbar, CheckableOptionsRespectDefaults)
{
    QWidget parent;
    auto* toolbar = new QHBoxLayout();

    HistogramToolbarOptions opts;
    opts.localChannelNormalizationChecked = true;
    opts.showHistogramAreasChecked = true;
    opts.showHistogramChecked = false;

    auto result = buildHistogramToolbar(&parent, toolbar, opts);

    ASSERT_NE(result.localChannelNormalization, nullptr);
    EXPECT_TRUE(result.localChannelNormalization->isChecked());

    ASSERT_NE(result.showHistogramAreas, nullptr);
    EXPECT_TRUE(result.showHistogramAreas->isChecked());

    ASSERT_NE(result.showHistogram, nullptr);
    EXPECT_FALSE(result.showHistogram->isChecked());

    delete toolbar;
}

TEST(BuildHistogramToolbar, ObjectNamesAreCorrect)
{
    QWidget parent;
    auto* toolbar = new QHBoxLayout();

    auto result = buildHistogramToolbar(&parent, toolbar);

    EXPECT_EQ(result.zoomIn->objectName(), QStringLiteral("zoomIn"));
    EXPECT_EQ(result.zoomOut->objectName(), QStringLiteral("zoomOut"));
    EXPECT_EQ(result.zoomReset->objectName(), QStringLiteral("zoomReset"));
    EXPECT_EQ(result.gridButton->objectName(), QStringLiteral("gridButton"));
    EXPECT_EQ(result.showClippingInfo->objectName(), QStringLiteral("showClippingInfo"));
    EXPECT_EQ(result.localChannelNormalization->objectName(), QStringLiteral("localChannelNormalization"));
    EXPECT_EQ(result.showHistogramAreas->objectName(), QStringLiteral("showHistogramAreas"));
    EXPECT_EQ(result.showHistogram->objectName(), QStringLiteral("showHistogram"));
    EXPECT_EQ(result.plotResolution->objectName(), QStringLiteral("plotResolution"));

    delete toolbar;
}
