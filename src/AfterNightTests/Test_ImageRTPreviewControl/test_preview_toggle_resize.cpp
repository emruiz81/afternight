// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file test_preview_toggle_resize.cpp
/// @brief Tests for the preview-toggle window-resize behaviour.
///
/// These tests exercise the Qt layout/sizing machinery that
/// ImageRTPreviewControl relies on when hiding and re-showing the preview
/// pane, through the pane helper it uses.  They operate on a standalone
/// QSplitter + QHBoxLayout hierarchy (the same structure that
/// RTPreviewUiCommon.h builds) so that they do not depend on the full
/// process-control class or the AfterNight engine.
///
/// Key invariants under test:
///   1. Hiding the preview pane via maxWidth=0 + setVisible(false) causes
///      the splitter's sizeHint to exclude the pane.
///   2. setFixedSize() can shrink a window past its current
///      minimumSizeHint, whereas resize() cannot.
///   3. After a deferred event-loop pass the size-constraint can be
///      re-established and the layout minimum reflects only the visible
///      children.
///   4. Re-showing the preview pane restores the original splitter
///      proportions and window size.

#include <gtest/gtest.h>

#include <QApplication>
#include <QHBoxLayout>
#include <QSplitter>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

#include "controls/ProcessPreviewLayout.h"

namespace
{

using an::ui::setPreviewPaneVisible;

constexpr int kMinPreviewPaneWidth = an::ui::kProcessMinPreviewPaneWidth;

/// Build a minimal two-pane splitter identical to the structure that
/// RTPreviewUiCommon::buildMainProcessSplitter creates.
struct SplitterFixture
{
    QWidget window;
    QHBoxLayout* rootLayout = nullptr;
    QSplitter* splitter = nullptr;
    QWidget* controlsPanel = nullptr;
    QWidget* previewPanel = nullptr;
    QVBoxLayout* controlsLayout = nullptr;
    QVBoxLayout* previewLayout = nullptr;

    explicit SplitterFixture(int controlsWidth = 300, int previewWidth = 400)
    {
        rootLayout = new QHBoxLayout(&window);
        rootLayout->setSizeConstraint(QLayout::SetMinimumSize);
        rootLayout->setContentsMargins(10, 10, 10, 10);

        splitter = new QSplitter(Qt::Horizontal);
        splitter->setObjectName(QStringLiteral("mainSplitter"));
        splitter->setChildrenCollapsible(false);

        controlsPanel = new QWidget(splitter);
        controlsLayout = new QVBoxLayout(controlsPanel);
        controlsPanel->setMinimumWidth(controlsWidth);

        previewPanel = new QWidget(splitter);
        previewLayout = new QVBoxLayout(previewPanel);
        previewPanel->setMinimumWidth(kMinPreviewPaneWidth);

        splitter->setStretchFactor(0, 0);
        splitter->setStretchFactor(1, 1);
        splitter->setSizes({controlsWidth, previewWidth});

        rootLayout->addWidget(splitter);

        window.resize(controlsWidth + previewWidth + 20, 500);
        window.show();

        // Let Qt process the show event so the layout is settled.
        QApplication::processEvents();
    }
};

} // anonymous namespace

// ---------------------------------------------------------------------------
// T1: Hiding the preview pane drops the splitter's sizeHint width.
// ---------------------------------------------------------------------------
TEST(PreviewToggleResize, HidingPreviewPaneReducesSplitterSizeHint)
{
    SplitterFixture f;

    const int hintBefore = f.splitter->sizeHint().width();

    setPreviewPaneVisible(f.splitter, false);
    QApplication::processEvents();

    const int hintAfter = f.splitter->sizeHint().width();
    EXPECT_LT(hintAfter, hintBefore) << "Splitter sizeHint should shrink when the preview pane is hidden";
}

// ---------------------------------------------------------------------------
// T2: resize() is clamped by minimumSizeHint, but setFixedSize() is not.
// ---------------------------------------------------------------------------
TEST(PreviewToggleResize, ResizeIsClampedButSetFixedSizeIsNot)
{
    SplitterFixture f;
    const int originalWidth = f.window.width();

    // The layout enforces a minimum that includes both panels.
    const int targetWidth = 350; // smaller than the two-panel minimum

    // Plain resize() should be clamped.
    f.window.resize(targetWidth, f.window.height());
    QApplication::processEvents();
    EXPECT_GE(f.window.width(), f.window.minimumSizeHint().width()) << "resize() must not go below minimumSizeHint";

    // setFixedSize() overrides the constraint.
    f.window.setFixedSize(targetWidth, f.window.height());
    QApplication::processEvents();
    EXPECT_EQ(f.window.width(), targetWidth)
        << "setFixedSize() should force the exact width regardless of minimumSizeHint";

    // Unlock.
    f.window.setMinimumSize(0, 0);
    f.window.setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
}

// ---------------------------------------------------------------------------
// T3: The full hide-resize-defer cycle produces the correct window width.
// ---------------------------------------------------------------------------
TEST(PreviewToggleResize, HideAndResizeCycleProducesCorrectWidth)
{
    SplitterFixture f(300, 400);

    const QList<int> splitterSizes = f.splitter->sizes();
    const int leftWidth = splitterSizes.isEmpty() ? 300 : splitterSizes[0];
    const int shellExtra = (std::max)(0, f.window.width() - f.splitter->width());

    // --- Hide preview ---
    setPreviewPaneVisible(f.splitter, false);

    // Drop constraint + setFixedSize (mirrors production code).
    f.rootLayout->setSizeConstraint(QLayout::SetDefaultConstraint);
    f.window.setFixedSize(leftWidth + shellExtra, f.window.height());
    QApplication::processEvents();

    EXPECT_EQ(f.window.width(), leftWidth + shellExtra) << "Window should be controls-only width after hiding preview";

    // Unlock (mirrors the deferred QTimer::singleShot(0, ...)).
    f.window.setMinimumSize(0, 0);
    f.window.setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
    QApplication::processEvents();

    EXPECT_EQ(f.window.width(), leftWidth + shellExtra) << "Window should stay at controls-only width after unlock";
}

// ---------------------------------------------------------------------------
// T4: Re-showing the preview pane and restoring sizes works.
// ---------------------------------------------------------------------------
TEST(PreviewToggleResize, ShowPreviewRestoresOriginalSize)
{
    SplitterFixture f(300, 400);

    const QSize originalSize = f.window.size();
    const QList<int> originalSplitterSizes = f.splitter->sizes();
    const int leftWidth = originalSplitterSizes.isEmpty() ? 300 : originalSplitterSizes[0];
    const int shellExtra = (std::max)(0, f.window.width() - f.splitter->width());

    // --- Hide ---
    setPreviewPaneVisible(f.splitter, false);
    f.rootLayout->setSizeConstraint(QLayout::SetDefaultConstraint);
    f.window.setFixedSize(leftWidth + shellExtra, f.window.height());
    QApplication::processEvents();
    f.window.setMinimumSize(0, 0);
    f.window.setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
    QApplication::processEvents();

    // --- Show ---
    f.rootLayout->setSizeConstraint(QLayout::SetMinimumSize);
    setPreviewPaneVisible(f.splitter, true);
    f.splitter->setSizes(originalSplitterSizes);
    f.window.resize(originalSize);
    QApplication::processEvents();

    EXPECT_GE(f.window.width(), originalSize.width() - 2)
        << "Window should be restored to (approximately) the original size";

    // The preview pane should be visible and have non-zero width.
    EXPECT_TRUE(f.splitter->widget(1)->isVisible());
    EXPECT_GT(f.splitter->sizes().at(1), 0) << "Preview pane should have non-zero width after restore";
}

// ---------------------------------------------------------------------------
// T5: Layout minimum is correctly reduced after the hide + event-loop pass.
// ---------------------------------------------------------------------------
TEST(PreviewToggleResize, LayoutMinimumReflectsHiddenPane)
{
    SplitterFixture f(300, 400);

    // While both panels are visible, the layout minimum includes both.
    const int minBefore = f.rootLayout->minimumSize().width();

    // Hide the preview, drop constraint, settle.
    setPreviewPaneVisible(f.splitter, false);
    f.rootLayout->setSizeConstraint(QLayout::SetDefaultConstraint);
    f.window.setMinimumSize(0, 0);
    f.window.setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
    QApplication::processEvents();

    // Re-apply SetMinimumSize — now it should use the reduced minimum.
    f.rootLayout->setSizeConstraint(QLayout::SetMinimumSize);
    f.rootLayout->invalidate();
    f.rootLayout->activate();
    QApplication::processEvents();

    const int minAfter = f.rootLayout->minimumSize().width();
    EXPECT_LT(minAfter, minBefore) << "Layout minimum should be smaller with the preview pane hidden";
}

// ---------------------------------------------------------------------------
// T6: Multiple hide/show cycles produce stable sizes.
// ---------------------------------------------------------------------------
TEST(PreviewToggleResize, MultipleToggleCyclesAreStable)
{
    SplitterFixture f(300, 400);

    const QSize originalSize = f.window.size();
    const QList<int> originalSplitterSizes = f.splitter->sizes();
    const int leftWidth = originalSplitterSizes.isEmpty() ? 300 : originalSplitterSizes[0];
    const int shellExtra = (std::max)(0, f.window.width() - f.splitter->width());

    for (int i = 0; i < 3; ++i)
    {
        // Hide
        setPreviewPaneVisible(f.splitter, false);
        f.rootLayout->setSizeConstraint(QLayout::SetDefaultConstraint);
        f.window.setFixedSize(leftWidth + shellExtra, f.window.height());
        QApplication::processEvents();
        f.window.setMinimumSize(0, 0);
        f.window.setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
        QApplication::processEvents();

        EXPECT_EQ(f.window.width(), leftWidth + shellExtra)
            << "Cycle " << i << ": window should be controls-only width";

        // Show
        f.rootLayout->setSizeConstraint(QLayout::SetMinimumSize);
        setPreviewPaneVisible(f.splitter, true);
        f.splitter->setSizes(originalSplitterSizes);
        f.window.resize(originalSize);
        QApplication::processEvents();

        EXPECT_GE(f.window.width(), originalSize.width() - 2)
            << "Cycle " << i << ": window should be restored to original width";
    }
}
