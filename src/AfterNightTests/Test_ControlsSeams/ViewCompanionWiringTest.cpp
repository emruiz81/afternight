// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// What the view tells its companions: each tool it is asked for opened through the one opener, the history window
/// asked of the history, the initial step recorded, the four astrometry overlays a standard view makes with the
/// catalogue overlay's layers and the choices its menu offers - a level of detail, the labels and a palette, kept for
/// the next view - and the stars a detection hands the overlay - observed through the stand-ins the viewer's list
/// registers for its companion seam, which only an executable that compiles the view can put behind it. What the real
/// companions then do is held against the library by the viewer, history, statistics, astrometry and dialogs suites.

#include "Test_ControlsSeams/ViewCompanionsStub.h"
#include "Test_ControlsViewer/ViewerTestContext.h"

#include "controls/annotation/AnnotationSettings.h"
#include "controls/settings/SettingsKeys.h"

#include <QAction>
#include <QMenu>

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace
{

using namespace an::tests::viewer;

/// A submenu of a view's astrometry settings, by the name it carries.
[[nodiscard]] QMenu* choicesMenu(ImageViewControl& view, const char* name)
{
    return view.findChild<QMenu*>(QLatin1String(name));
}

/// The value of a submenu's checked item.
[[nodiscard]] std::optional<int> checkedValue(const QMenu& menu)
{
    for (const QAction* action : menu.actions())
    {
        if (action->isChecked())
        {
            return action->data().toInt();
        }
    }
    return std::nullopt;
}

/// Chooses a submenu's item of a value, as a click does.
[[nodiscard]] bool choose(QMenu& menu, const int value)
{
    for (QAction* action : menu.actions())
    {
        if (action->data().toInt() == value)
        {
            action->trigger();
            return true;
        }
    }
    return false;
}

class ViewCompanionWiringTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        resetCompanionRecord();
        m_context = openViewerContext(2u);
        ASSERT_NE(m_context, nullptr);
        m_installed.emplace(*m_context);
    }

    void TearDown() override
    {
        m_installed.reset();
        m_context.reset();
    }

    std::unique_ptr<ViewerTestContext> m_context;
    std::optional<InstalledContext> m_installed;
};

} // namespace

TEST_F(ViewCompanionWiringTest, ToolsOverlaysAndTheHistoryAreTheCompanionsOwn)
{
    ViewHandle view = makeView("companions");
    std::optional<an::ui::ImageDocument> document = makeDocument<float>(
        m_context->engineSession(), 4u, 4u, Channels::Grey,
        [](std::uint32_t x, std::uint32_t y, std::uint32_t) { return static_cast<float>(x + 10u * y) / 100.0f; });
    ASSERT_TRUE(document.has_value());
    view->setDocument(std::move(*document));
    view->initializeNewImageHistory("Image read from a file");
    ASSERT_TRUE(settle(*view));

    view->executeStatistics();
    view->executeHistogram();
    view->executeFITSHeaderViewer();
    view->executeEXIFViewer();
    view->executeSummaryViewer();
    view->executeAstrometryInfoViewer();
    view->executePlateSolve();
    view->history();
    EXPECT_EQ(companionRecord().toolsOpened.size(), 7u);
    EXPECT_EQ(companionRecord().historyWindows, 1);
    EXPECT_EQ(companionRecord().initialSteps, std::vector<std::string>{"Image read from a file"});
    EXPECT_EQ(companionRecord().overlaysMade.size(), 4u) << "a standard view makes its four astrometry overlays";
    EXPECT_EQ(companionRecord().overlayChoices.size(), 2u) << "the grid and the catalogue overlay are told the choices";
    EXPECT_EQ(companionRecord().overlayLayers.size(), 1u) << "the catalogue overlay is told which layers it draws";

    // An image with no solution has no stars to detect, and the overlay is told so.
    view->runPostPlateSolveStarDetection();
    ASSERT_TRUE(settle(*view));
    EXPECT_EQ(view->overlayStars(), nullptr);
    ASSERT_FALSE(companionRecord().overlayStars.empty());
    EXPECT_EQ(companionRecord().overlayStars.back(), nullptr);
}

// The menu offers an annotated picture's levels of detail, label texts and palettes, and the view's text sizes,
// starting at the standard level by designation in Aurora at the medium size; a choice is handed to the grid and the
// catalogue overlay, and kept for the next view.
TEST_F(ViewCompanionWiringTest, TheMenusChoicesAreHandedToTheOverlaysAndKeptForTheNextView)
{
    ViewHandle view = makeView("choices");
    QMenu* detail = choicesMenu(*view, "astrometryDetailMenu");
    QMenu* labels = choicesMenu(*view, "astrometryLabelsMenu");
    QMenu* palette = choicesMenu(*view, "astrometryPaletteMenu");
    QMenu* textSize = choicesMenu(*view, "astrometryTextSizeMenu");
    ASSERT_NE(detail, nullptr);
    ASSERT_NE(labels, nullptr);
    ASSERT_NE(palette, nullptr);
    ASSERT_NE(textSize, nullptr);
    EXPECT_EQ(detail->actions().size(), 4);
    EXPECT_EQ(labels->actions().size(), 3);
    EXPECT_EQ(palette->actions().size(), static_cast<qsizetype>(an::ui::annotationPalettes().size()));
    EXPECT_EQ(textSize->actions().size(), 3);
    EXPECT_EQ(checkedValue(*detail), static_cast<int>(an::ui::AnnotationDetail::Standard));
    EXPECT_EQ(checkedValue(*labels), static_cast<int>(an::ui::AnnotationLabelText::Designation));
    EXPECT_EQ(checkedValue(*palette), static_cast<int>(an::ui::AnnotationPalette::Aurora));
    EXPECT_EQ(checkedValue(*textSize), static_cast<int>(an::ui::AnnotationViewTextSize::Medium));
    ASSERT_EQ(companionRecord().overlayChoices.size(), 2u);
    EXPECT_EQ(companionRecord().overlayChoices.back().second, an::ui::AnnotationViewChoices{});

    ASSERT_TRUE(choose(*detail, static_cast<int>(an::ui::AnnotationDetail::Complete)));
    ASSERT_TRUE(choose(*palette, static_cast<int>(an::ui::AnnotationPalette::Ember)));
    ASSERT_TRUE(choose(*textSize, static_cast<int>(an::ui::AnnotationViewTextSize::Large)));
    const an::ui::AnnotationViewChoices chosen{.detail = an::ui::AnnotationDetail::Complete,
                                               .palette = an::ui::AnnotationPalette::Ember,
                                               .textSize = an::ui::AnnotationViewTextSize::Large};
    ASSERT_EQ(companionRecord().overlayChoices.size(), 8u) << "each choice is handed to both overlays";
    const std::set<an::ui::AstrometryOverlay> told{companionRecord().overlayChoices[6].first,
                                                   companionRecord().overlayChoices[7].first};
    EXPECT_EQ(told, (std::set<an::ui::AstrometryOverlay>{an::ui::AstrometryOverlay::CoordinateGrid,
                                                         an::ui::AstrometryOverlay::CatalogAnnotations}));
    EXPECT_EQ(companionRecord().overlayChoices[6].second, chosen);
    EXPECT_EQ(companionRecord().overlayChoices[7].second, chosen);
    EXPECT_EQ(checkedValue(*detail), static_cast<int>(an::ui::AnnotationDetail::Complete));
    EXPECT_EQ(checkedValue(*textSize), static_cast<int>(an::ui::AnnotationViewTextSize::Large));
    EXPECT_EQ(an::ui::annotationViewChoicesFromStored(m_context->select(SettingsKeys::ANNOTATION_VIEW_CHOICES)),
              chosen);

    ViewHandle next = makeView("next");
    QMenu* nextDetail = choicesMenu(*next, "astrometryDetailMenu");
    QMenu* nextTextSize = choicesMenu(*next, "astrometryTextSizeMenu");
    ASSERT_NE(nextDetail, nullptr);
    ASSERT_NE(nextTextSize, nullptr);
    EXPECT_EQ(checkedValue(*nextDetail), static_cast<int>(an::ui::AnnotationDetail::Complete));
    EXPECT_EQ(checkedValue(*nextTextSize), static_cast<int>(an::ui::AnnotationViewTextSize::Large));
    EXPECT_EQ(companionRecord().overlayChoices.back().second, chosen);
}

// A view that kept a magnitude limit before it offered levels starts at the level that names about as much; choices
// kept since are read over it.
TEST_F(ViewCompanionWiringTest, ASavedMagnitudeLimitChoosesTheLevelTheMenuStartsAt)
{
    m_context->upsert(SettingsKeys::PLATESOLVE_CATALOG_MAGNITUDE_LIMIT, QStringLiteral("14.0"));
    ViewHandle view = makeView("limit");
    QMenu* detail = choicesMenu(*view, "astrometryDetailMenu");
    ASSERT_NE(detail, nullptr);
    EXPECT_EQ(checkedValue(*detail), static_cast<int>(an::ui::AnnotationDetail::Detailed));
    ASSERT_FALSE(companionRecord().overlayChoices.empty());
    EXPECT_EQ(companionRecord().overlayChoices.back().second.detail, an::ui::AnnotationDetail::Detailed);

    m_context->upsert(SettingsKeys::ANNOTATION_VIEW_CHOICES,
                      an::ui::storedAnnotationViewChoices(
                          an::ui::AnnotationViewChoices{.detail = an::ui::AnnotationDetail::Essential}));
    ViewHandle stored = makeView("stored");
    QMenu* storedDetail = choicesMenu(*stored, "astrometryDetailMenu");
    ASSERT_NE(storedDetail, nullptr);
    EXPECT_EQ(checkedValue(*storedDetail), static_cast<int>(an::ui::AnnotationDetail::Essential));
}
