// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// What the view tells its companions: each tool it is asked for opened through the one opener, the history window
/// asked of the history, the initial step recorded, the four astrometry overlays a standard view makes with the
/// catalogue overlay's magnitude limit and layers, and the stars a detection hands the overlay - observed through the
/// stand-ins the viewer's list registers for its companion seam, which only an executable that compiles the view can
/// put behind it. What the real companions then do is held against the library by the viewer, history, statistics,
/// astrometry and dialogs suites.

#include "Test_ControlsSeams/ViewCompanionsStub.h"
#include "Test_ControlsViewer/ViewerTestContext.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace
{

using namespace an::tests::viewer;

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
    EXPECT_EQ(companionRecord().magnitudeLimits.size(), 1u) << "the catalogue overlay is told its magnitude limit";
    EXPECT_EQ(companionRecord().overlayLayers.size(), 1u) << "the catalogue overlay is told which layers it draws";

    // An image with no solution has no stars to detect, and the overlay is told so.
    view->runPostPlateSolveStarDetection();
    ASSERT_TRUE(settle(*view));
    EXPECT_EQ(view->overlayStars(), nullptr);
    ASSERT_FALSE(companionRecord().overlayStars.empty());
    EXPECT_EQ(companionRecord().overlayStars.back(), nullptr);
}
