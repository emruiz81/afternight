// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// What the viewer composes around its views: the comparison view's difference and overlay, the mask selector's list,
/// and the coordinator that makes a view around a document.

#include "Test_ControlsViewer/ViewerTestContext.h"

#include "app/ViewCreationCoordinator.h"
#include "controls/ImageMaskSelectorControl.h"
#include "controls/ImageViewControlManager.h"
#include "widgets/ComparisonViewWidget.h"

#include <AfterNightConsumerRuntime/OperationScope.h>

#include <QComboBox>
#include <QThreadPool>

#include <gtest/gtest.h>

#include <cstdint>
#include <optional>
#include <vector>

namespace
{

using namespace an::tests::viewer;

class ComparisonViewTest : public ::testing::Test
{
protected:
    void open(std::uint32_t width = 2u)
    {
        m_context = openViewerContext(width);
        ASSERT_NE(m_context, nullptr);
        m_installed.emplace(*m_context);
    }

    void TearDown() override
    {
        m_installed.reset();
        m_context.reset();
    }

    [[nodiscard]] const an::consumer::EngineSession& session() const
    {
        return m_context->engineSession();
    }

    /// Turns the event loop until the comparison and both its views are idle.
    [[nodiscard]] static bool settleComparison(const an::ui::ComparisonViewWidget& widget)
    {
        const auto idle = [&widget]() {
            return widget.isComparisonIdle() && widget.leftControl()->isWorkIdle() &&
                   widget.rightControl()->isWorkIdle();
        };
        if (!pumpUntil(idle))
        {
            return false;
        }
        pumpQuietly();
        return idle();
    }

    std::unique_ptr<ViewerTestContext> m_context;
    std::optional<InstalledContext> m_installed;
};

// Through the view: two equal signed images showed the lowest code as their difference.
TEST_F(ComparisonViewTest, TheDifferenceIsShownOverTheLeftImageWhichIsNeverReplaced)
{
    open();
    an::ui::ComparisonViewWidget widget;
    const auto ramp = [](std::uint32_t x, std::uint32_t y, std::uint32_t) {
        return static_cast<std::int16_t>(-30000 + 2000 * static_cast<int>(x) + 300 * static_cast<int>(y));
    };
    std::optional<an::consumer::ImageAsset> left = makeAsset<std::int16_t>(session(), 8u, 6u, Channels::Grey, ramp);
    std::optional<an::consumer::ImageAsset> right = makeAsset<std::int16_t>(session(), 8u, 6u, Channels::Grey, ramp);
    ASSERT_TRUE(left.has_value() && right.has_value());
    const std::uint64_t leftIdentity = left->identity();
    widget.setLeftImage(std::move(*left));
    widget.setRightImage(std::move(*right));
    ASSERT_TRUE(settleComparison(widget));

    widget.setMode(an::ui::ComparisonViewMode::Difference);
    ASSERT_TRUE(settleComparison(widget));
    ImageViewControl* shown = widget.leftControl();
    ASSERT_TRUE(shown->hasAlternateImage());
    EXPECT_TRUE(shown->getViewWidget()->isAlternateImageRenderEnabled());
    EXPECT_EQ(shown->document().asset()->identity(), leftIdentity) << "the left image stays the left view's own";
    const std::shared_ptr<const an::consumer::ImageAsset> difference = shown->alternateAsset();
    for (std::uint32_t y = 0u; y < 6u; ++y)
    {
        for (std::uint32_t x = 0u; x < 8u; ++x)
        {
            const std::optional<float> sample = sampleOf<float>(*difference, x, y);
            ASSERT_TRUE(sample.has_value());
            EXPECT_EQ(*sample, 0.0f) << x << "," << y;
        }
    }

    widget.setMode(an::ui::ComparisonViewMode::SideBySide);
    ASSERT_TRUE(settleComparison(widget));
    EXPECT_FALSE(shown->hasAlternateImage());
    EXPECT_FALSE(shown->getViewWidget()->isAlternateImageRenderEnabled());
}

TEST_F(ComparisonViewTest, AComparisonDestroyedWhileItsDifferenceIsOnItsWayKeepsNothing)
{
    open(1u);
    {
        auto widget = std::make_unique<an::ui::ComparisonViewWidget>();
        const auto flat = [](std::uint32_t, std::uint32_t, std::uint32_t) { return 0.5f; };
        std::optional<an::consumer::ImageAsset> left = makeAsset<float>(session(), 8u, 8u, Channels::Grey, flat);
        std::optional<an::consumer::ImageAsset> right = makeAsset<float>(session(), 8u, 8u, Channels::Grey, flat);
        ASSERT_TRUE(left.has_value() && right.has_value());
        widget->setLeftImage(std::move(*left));
        widget->setRightImage(std::move(*right));
        ASSERT_TRUE(settleComparison(*widget));
        std::optional<an::consumer::OperationScope> gate = admitted(session());
        ASSERT_TRUE(gate.has_value());
        widget->setMode(an::ui::ComparisonViewMode::Difference);
        pumpQuietly();
        widget.reset();
    }
    QThreadPool::globalInstance()->waitForDone();
    pumpQuietly();
    EXPECT_TRUE(m_context->outstanding().none());
    const an::Status shutdown = m_context->shutdownSession();
    EXPECT_TRUE(shutdown.ok()) << shutdown.message();
}

// A four-channel image passed the selector's colour check over a grey image.
TEST_F(ComparisonViewTest, TheMaskSelectorListsOnlyMasksTheOverlayCanShow)
{
    open();
    const auto constant = [](std::uint32_t, std::uint32_t, std::uint32_t) { return 1.0f; };
    ViewHandle target = makeView("target");
    ViewHandle grey = makeView("grey");
    ViewHandle rgba = makeView("rgba");
    ViewHandle rgb = makeView("rgb");
    ViewHandle small = makeView("small");
    std::optional<an::ui::ImageDocument> targetDocument =
        makeDocument<float>(session(), 4u, 4u, Channels::Grey, constant);
    std::optional<an::ui::ImageDocument> greyDocument =
        makeDocument<float>(session(), 4u, 4u, Channels::Grey, constant);
    std::optional<an::ui::ImageDocument> rgbaDocument =
        makeDocument<float>(session(), 4u, 4u, Channels::Rgba, constant);
    std::optional<an::ui::ImageDocument> rgbDocument = makeDocument<float>(session(), 4u, 4u, Channels::Rgb, constant);
    std::optional<an::ui::ImageDocument> smallDocument =
        makeDocument<float>(session(), 2u, 2u, Channels::Grey, constant);
    ASSERT_TRUE(targetDocument && greyDocument && rgbaDocument && rgbDocument && smallDocument);
    target->setDocument(std::move(*targetDocument));
    grey->setDocument(std::move(*greyDocument));
    rgba->setDocument(std::move(*rgbaDocument));
    rgb->setDocument(std::move(*rgbDocument));
    small->setDocument(std::move(*smallDocument));

    auto* selector = new ImageMaskSelectorControl(nullptr, target.get());
    const QList<QComboBox*> combos = selector->findChildren<QComboBox*>();
    ASSERT_EQ(combos.size(), 1);
    QStringList listed;
    for (int index = 0; index < combos.front()->count(); ++index)
    {
        listed.push_back(combos.front()->itemText(index));
    }
    EXPECT_EQ(listed, QStringList{QStringLiteral("grey")});
    delete selector;
}

TEST_F(ComparisonViewTest, TheCoordinatorMakesAViewThatOwnsTheDocumentAndRefusesAClosedOne)
{
    open();
    ImageViewControlManager manager;
    an::ui::ViewCreationCoordinator::Dependencies dependencies;
    dependencies.imageViewControlManager = &manager;
    an::ui::ViewCreationCoordinator coordinator(dependencies);

    std::optional<an::ui::ImageDocument> document = makeDocument<float>(
        session(), 4u, 4u, Channels::Grey, [](std::uint32_t, std::uint32_t, std::uint32_t) { return 0.5f; },
        an::ui::DocumentNames{.displayName = QStringLiteral("made")});
    ASSERT_TRUE(document.has_value());
    const std::uint64_t identity = document->identity();
    ViewHandle view(coordinator.createNewImageViewControlFromImage(nullptr, std::move(*document), nullptr, "made here",
                                                                   ViewControlMode::st_preview, nullptr, false, false));
    ASSERT_NE(view, nullptr);
    ASSERT_TRUE(settle(*view));
    EXPECT_EQ(view->document().identity(), identity);
    EXPECT_EQ(view->getUniqueName(), "made");

    std::optional<an::ui::ImageDocument> closed = makeDocument<float>(
        session(), 4u, 4u, Channels::Grey, [](std::uint32_t, std::uint32_t, std::uint32_t) { return 0.5f; });
    ASSERT_TRUE(closed.has_value());
    closed->close();
    EXPECT_EQ(coordinator.createNewImageViewControlFromImage(nullptr, std::move(*closed), nullptr, "",
                                                             ViewControlMode::st_preview, nullptr, false, false),
              nullptr);
    EXPECT_TRUE(closed->valid()) << "a document no view is made for stays with its caller";

    std::optional<an::ui::ImageDocument> notSequence = makeDocument<float>(
        session(), 4u, 4u, Channels::Grey, [](std::uint32_t, std::uint32_t, std::uint32_t) { return 0.5f; });
    ASSERT_TRUE(notSequence.has_value());
    EXPECT_EQ(coordinator.createNewImageViewControlFromSequence(nullptr, std::move(*notSequence), nullptr, false, "",
                                                                ViewControlMode::st_preview, nullptr, false, false),
              nullptr);
    EXPECT_TRUE(notSequence->valid());
}

} // namespace
