// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file test_anonymous_helpers.cpp
/// @brief Tests for the dialog-layout helpers ImageRTPreviewControl lays out
///        its process windows with.
///
/// The helpers live in controls/ProcessPreviewLayout.h and are called
/// directly.
///
/// Helpers covered:
///   - setPreviewPaneVisible   (toggle preview pane in a QSplitter)
///   - collapsePreviewPane     (redistribute splitter space)
///   - findMainProcessSplitter (locate the main splitter in a layout)
///   - sanitizeMaxSize          (clamp bad max-size values)

#include <gtest/gtest.h>

#include <QApplication>
#include <QHBoxLayout>
#include <QSplitter>
#include <QVBoxLayout>
#include <QWidget>

#include <limits>

#include "controls/ProcessPreviewLayout.h"

namespace
{

using an::ui::collapsePreviewPane;
using an::ui::findMainProcessSplitter;
using an::ui::sanitizeMaxSize;
using an::ui::setPreviewPaneVisible;

constexpr int MIN_PREVIEW_PANE_WIDTH = an::ui::kProcessMinPreviewPaneWidth;

// -----------------------------------------------------------------------
// Fixture: minimal two-pane splitter inside a top-level window.
// -----------------------------------------------------------------------
struct TwoPaneSplitter
{
    QWidget window;
    QHBoxLayout* layout = nullptr;
    QSplitter* splitter = nullptr;
    QWidget* leftPane = nullptr;
    QWidget* rightPane = nullptr;

    TwoPaneSplitter()
    {
        layout = new QHBoxLayout(&window);

        splitter = new QSplitter(Qt::Horizontal);
        splitter->setObjectName(QStringLiteral("mainSplitter"));
        splitter->setChildrenCollapsible(false);

        leftPane = new QWidget(splitter);
        leftPane->setMinimumWidth(200);

        rightPane = new QWidget(splitter);
        rightPane->setMinimumWidth(MIN_PREVIEW_PANE_WIDTH);

        splitter->setSizes({300, 400});
        layout->addWidget(splitter);

        window.resize(720, 500);
        window.show();
        QApplication::processEvents();
    }
};

} // anonymous namespace

// =======================================================================
// setPreviewPaneVisible
// =======================================================================

TEST(SetPreviewPaneVisible, NullSplitterDoesNotCrash)
{
    EXPECT_NO_FATAL_FAILURE(setPreviewPaneVisible(nullptr, true));
    EXPECT_NO_FATAL_FAILURE(setPreviewPaneVisible(nullptr, false));
}

TEST(SetPreviewPaneVisible, SinglePaneSplitterIsIgnored)
{
    QWidget root;
    auto* splitter = new QSplitter(Qt::Horizontal, &root);
    auto* onlyChild = new QWidget(splitter);
    onlyChild->setMinimumWidth(100);

    root.show();
    QApplication::processEvents();

    // Should be a no-op (count < 2).
    EXPECT_NO_FATAL_FAILURE(setPreviewPaneVisible(splitter, false));
    EXPECT_TRUE(onlyChild->isVisible());
}

TEST(SetPreviewPaneVisible, HideSetsMaxWidthToZero)
{
    TwoPaneSplitter f;

    setPreviewPaneVisible(f.splitter, false);
    QApplication::processEvents();

    EXPECT_EQ(f.rightPane->maximumWidth(), 0);
    EXPECT_EQ(f.rightPane->minimumWidth(), 0);
    EXPECT_FALSE(f.rightPane->isVisible());
}

TEST(SetPreviewPaneVisible, ShowRestoresMinimumWidth)
{
    TwoPaneSplitter f;

    setPreviewPaneVisible(f.splitter, false);
    QApplication::processEvents();

    setPreviewPaneVisible(f.splitter, true);
    QApplication::processEvents();

    EXPECT_EQ(f.rightPane->minimumWidth(), MIN_PREVIEW_PANE_WIDTH);
    EXPECT_EQ(f.rightPane->maximumWidth(), QWIDGETSIZE_MAX);
    EXPECT_TRUE(f.rightPane->isVisible());
}

TEST(SetPreviewPaneVisible, HandleVisibilityFollowsPane)
{
    TwoPaneSplitter f;

    auto* handle = f.splitter->handle(1);
    ASSERT_NE(handle, nullptr);

    setPreviewPaneVisible(f.splitter, false);
    QApplication::processEvents();
    EXPECT_FALSE(handle->isVisible());

    setPreviewPaneVisible(f.splitter, true);
    QApplication::processEvents();
    EXPECT_TRUE(handle->isVisible());
}

TEST(SetPreviewPaneVisible, DoubleHideIsIdempotent)
{
    TwoPaneSplitter f;

    setPreviewPaneVisible(f.splitter, false);
    setPreviewPaneVisible(f.splitter, false);
    QApplication::processEvents();

    EXPECT_EQ(f.rightPane->maximumWidth(), 0);
    EXPECT_FALSE(f.rightPane->isVisible());
}

TEST(SetPreviewPaneVisible, DoubleShowIsIdempotent)
{
    TwoPaneSplitter f;

    setPreviewPaneVisible(f.splitter, true);
    setPreviewPaneVisible(f.splitter, true);
    QApplication::processEvents();

    EXPECT_EQ(f.rightPane->minimumWidth(), MIN_PREVIEW_PANE_WIDTH);
    EXPECT_TRUE(f.rightPane->isVisible());
}

// =======================================================================
// collapsePreviewPane
// =======================================================================

TEST(CollapsePreviewPane, NullSplitterDoesNotCrash)
{
    EXPECT_NO_FATAL_FAILURE(collapsePreviewPane(nullptr));
}

TEST(CollapsePreviewPane, SinglePaneSplitterIsIgnored)
{
    QWidget root;
    auto* splitter = new QSplitter(Qt::Horizontal, &root);
    new QWidget(splitter);

    root.show();
    QApplication::processEvents();

    EXPECT_NO_FATAL_FAILURE(collapsePreviewPane(splitter));
}

TEST(CollapsePreviewPane, GivesAllSpaceToLeftPane)
{
    TwoPaneSplitter f;

    // Pre-condition: both panes have space.
    const QList<int> before = f.splitter->sizes();
    ASSERT_GT(before.size(), 1);
    ASSERT_GT(before[1], 0);

    collapsePreviewPane(f.splitter);
    QApplication::processEvents();

    const QList<int> after = f.splitter->sizes();
    ASSERT_GE(after.size(), 2);

    // The total should be redistributed: right pane gets 0,
    // left pane gets the full total.
    const int totalBefore = before[0] + before[1];
    EXPECT_EQ(after[0], totalBefore);
    EXPECT_EQ(after[1], 0);
}

// =======================================================================
// findMainProcessSplitter
// =======================================================================

TEST(FindMainProcessSplitter, NullLayoutReturnsNull)
{
    EXPECT_EQ(findMainProcessSplitter(nullptr), nullptr);
}

TEST(FindMainProcessSplitter, FindsDirectChildSplitter)
{
    QWidget root;
    auto* layout = new QHBoxLayout(&root);
    auto* splitter = new QSplitter(Qt::Horizontal);
    layout->addWidget(splitter);

    root.show();
    QApplication::processEvents();

    EXPECT_EQ(findMainProcessSplitter(layout), splitter);
}

TEST(FindMainProcessSplitter, FindsNamedNestedSplitterInsideWrapper)
{
    QWidget root;
    auto* layout = new QHBoxLayout(&root);

    // Wrapper widget containing the named splitter as a child.
    auto* wrapper = new QWidget();
    auto* innerLayout = new QVBoxLayout(wrapper);
    auto* splitter = new QSplitter(Qt::Horizontal);
    splitter->setObjectName(QStringLiteral("mainSplitter"));
    innerLayout->addWidget(splitter);

    layout->addWidget(wrapper);

    root.show();
    QApplication::processEvents();

    EXPECT_EQ(findMainProcessSplitter(layout), splitter);
}

TEST(FindMainProcessSplitter, IgnoresUnnamedNestedSplitter)
{
    // A nested splitter without the "mainSplitter" objectName should only
    // be found if it's a direct child of the layout.  If it's inside a
    // wrapper widget with a different name, findChild looks for
    // "mainSplitter" — so an unnamed one won't match.
    QWidget root;
    auto* layout = new QHBoxLayout(&root);

    auto* wrapper = new QWidget();
    auto* innerLayout = new QVBoxLayout(wrapper);
    auto* splitter = new QSplitter(Qt::Horizontal);
    splitter->setObjectName(QStringLiteral("otherSplitter"));
    innerLayout->addWidget(splitter);

    layout->addWidget(wrapper);

    root.show();
    QApplication::processEvents();

    // The function falls through to the host's findChild, which also
    // needs "mainSplitter" — so this should return nullptr.
    EXPECT_EQ(findMainProcessSplitter(layout), nullptr);
}

TEST(FindMainProcessSplitter, EmptyLayoutReturnsNull)
{
    QWidget root;
    auto* layout = new QHBoxLayout(&root);

    root.show();
    QApplication::processEvents();

    EXPECT_EQ(findMainProcessSplitter(layout), nullptr);
}

TEST(FindMainProcessSplitter, FallbackUsesParentWidgetFindChild)
{
    // If the splitter is not a direct layout item but is a child of the
    // layout's parent widget, findMainProcessSplitter falls back to
    // host->findChild.
    QWidget root;
    auto* layout = new QHBoxLayout(&root);

    // Add a plain widget to the layout (not a splitter).
    layout->addWidget(new QWidget());

    // Add the splitter directly as a QObject child of root (not via
    // the layout), with the correct objectName.
    auto* splitter = new QSplitter(Qt::Horizontal, &root);
    splitter->setObjectName(QStringLiteral("mainSplitter"));
    // Don't add to layout — just reparent.

    root.show();
    QApplication::processEvents();

    EXPECT_EQ(findMainProcessSplitter(layout), splitter);
}

// =======================================================================
// sanitizeMaxSize
// =======================================================================

TEST(SanitizeMaxSize, ValidSizePassesThrough)
{
    const QSize input(800, 600);
    EXPECT_EQ(sanitizeMaxSize(input), input);
}

TEST(SanitizeMaxSize, ZeroWidthClampedToMax)
{
    const QSize input(0, 600);
    const QSize result = sanitizeMaxSize(input);
    EXPECT_EQ(result.width(), QWIDGETSIZE_MAX);
    EXPECT_EQ(result.height(), 600);
}

TEST(SanitizeMaxSize, ZeroHeightClampedToMax)
{
    const QSize input(800, 0);
    const QSize result = sanitizeMaxSize(input);
    EXPECT_EQ(result.width(), 800);
    EXPECT_EQ(result.height(), QWIDGETSIZE_MAX);
}

TEST(SanitizeMaxSize, NegativeValuesClampedToMax)
{
    const QSize input(-1, -100);
    const QSize result = sanitizeMaxSize(input);
    EXPECT_EQ(result.width(), QWIDGETSIZE_MAX);
    EXPECT_EQ(result.height(), QWIDGETSIZE_MAX);
}

TEST(SanitizeMaxSize, QWidgetSizeMaxPassesThrough)
{
    const QSize input(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
    EXPECT_EQ(sanitizeMaxSize(input), input);
}

TEST(SanitizeMaxSize, ExceedingMaxClamped)
{
    // Values > QWIDGETSIZE_MAX get clamped.
    const int overMax = QWIDGETSIZE_MAX + 1;
    const QSize input(overMax, overMax);
    const QSize result = sanitizeMaxSize(input);
    EXPECT_EQ(result.width(), QWIDGETSIZE_MAX);
    EXPECT_EQ(result.height(), QWIDGETSIZE_MAX);
}

TEST(SanitizeMaxSize, MixedValidAndInvalid)
{
    const QSize input(1024, -5);
    const QSize result = sanitizeMaxSize(input);
    EXPECT_EQ(result.width(), 1024);
    EXPECT_EQ(result.height(), QWIDGETSIZE_MAX);
}
