// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The comparison view the blink comparator shows: the two static previews it embeds and owns, whose zooms follow each
/// other to show the same field, and a difference shown only while it is of the images both views show. The blink
/// dialog's case reads its source.

#include <gtest/gtest.h>

#include "Test_ControlsViewer/ViewerTestContext.h"

#include "controls/ImageViewControl.h"
#include "widgets/ComparisonViewWidget.h"
#include "widgets/ImageViewWidget.h"

#include <AfterNightConsumerRuntime/ImageAsset.h>
#include <AfterNightConsumerRuntime/OperationScope.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <utility>

namespace
{

std::filesystem::path findRepoRoot()
{
    std::filesystem::path current = std::filesystem::current_path();
    while (!current.empty())
    {
        if (std::filesystem::exists(current / "src" / "AfterNightApp"))
        {
            return current;
        }
        if (current == current.root_path())
        {
            break;
        }
        current = current.parent_path();
    }

    return {};
}

std::string loadTextFile(const std::filesystem::path& relativePath)
{
    const auto root = findRepoRoot();
    if (root.empty())
    {
        return {};
    }

    std::ifstream file(root / relativePath, std::ios::in | std::ios::binary);
    if (!file)
    {
        return {};
    }

    return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

/// An application context over one engine session, installed for one case and cleared when the case ends: a comparison
/// makes its previews only under one.
class InstalledViewerContext
{
public:
    explicit InstalledViewerContext(std::uint32_t width = 2u) : m_context(an::tests::viewer::openViewerContext(width))
    {
        if (m_context != nullptr)
        {
            m_installed.emplace(*m_context);
        }
    }

    /// Whether the session was opened.
    [[nodiscard]] bool valid() const noexcept
    {
        return m_context != nullptr;
    }

    /// The installed context, which keeps the views the application registers.
    [[nodiscard]] an::tests::viewer::ViewerTestContext& context()
    {
        return *m_context;
    }

    /// The session the context lends.
    [[nodiscard]] const an::consumer::EngineSession& session() const
    {
        return m_context->engineSession();
    }

private:
    std::unique_ptr<an::tests::viewer::ViewerTestContext> m_context;
    std::optional<an::tests::viewer::InstalledContext> m_installed;
};

/// Turns the event loop until the comparison and both its views are idle.
[[nodiscard]] bool settleComparison(const an::ui::ComparisonViewWidget& widget)
{
    const auto idle = [&widget]() {
        return widget.isComparisonIdle() && widget.leftControl()->isWorkIdle() && widget.rightControl()->isWorkIdle();
    };
    if (!an::tests::viewer::pumpUntil(idle))
    {
        return false;
    }
    an::tests::viewer::pumpQuietly();
    return idle();
}

/// A grey Float32 image of one value; nothing, the case failed, when it cannot be made.
[[nodiscard]] std::optional<an::consumer::ImageAsset> flatImage(const an::consumer::EngineSession& session,
                                                                std::uint32_t width, std::uint32_t height, float value)
{
    return an::tests::viewer::makeAsset<float>(session, width, height, an::tests::viewer::Channels::Grey,
                                               [value](std::uint32_t, std::uint32_t, std::uint32_t) { return value; });
}

} // namespace

// The comparison embeds its two previews as static previews of its own, made under a context that lends no view
// factory, and the application never counts them among its views.
TEST(BlinkComparisonViewArchitecture, TheComparisonEmbedsTwoStaticPreviewsTheApplicationNeverRegisters)
{
    InstalledViewerContext installed;
    ASSERT_TRUE(installed.valid());
    an::ui::ComparisonViewWidget widget;

    ImageViewControl* left = widget.leftControl();
    ImageViewControl* right = widget.rightControl();
    ASSERT_NE(left, nullptr);
    ASSERT_NE(right, nullptr);
    EXPECT_NE(left, right);
    for (ImageViewControl* preview : {left, right})
    {
        EXPECT_TRUE(widget.isAncestorOf(preview)) << "a preview is a child widget of the comparison";
        EXPECT_EQ(preview->getViewControlMode(), ViewControlMode::st_preview);
        EXPECT_TRUE(preview->isAutoDebayerCfaImagePreviewsEnabled()) << "a preview shows a mosaic in colour";
        EXPECT_EQ(preview->getViewWidget()->getViewMode(), ImageViewMode::AUTOSTRETCH_MODE);
        EXPECT_EQ(preview->viewHistory(), nullptr) << "a preview keeps no history of its own";
    }
    EXPECT_TRUE(installed.context().getListOfViewControls().empty())
        << "the application counts a preview of the comparison among its views";
}

// Each embedded view hears the other's zoom: zooming either brings the other to the zoom that shows the same field of
// its own image, here twice as wide and twice as tall.
TEST(BlinkComparisonViewArchitecture, ZoomingEitherEmbeddedViewZoomsTheOtherToShowTheSameField)
{
    InstalledViewerContext installed;
    ASSERT_TRUE(installed.valid());
    an::ui::ComparisonViewWidget widget;
    widget.setMode(an::ui::ComparisonViewMode::SideBySide);
    std::optional<an::consumer::ImageAsset> narrowImage = flatImage(installed.session(), 8u, 6u, 0.25f);
    std::optional<an::consumer::ImageAsset> wideImage = flatImage(installed.session(), 16u, 12u, 0.25f);
    ASSERT_TRUE(narrowImage.has_value() && wideImage.has_value());
    widget.setLeftImage(std::move(*narrowImage));
    widget.setRightImage(std::move(*wideImage));
    ASSERT_TRUE(settleComparison(widget));

    ImageViewWidget* left = widget.leftControl()->getViewWidget();
    ImageViewWidget* right = widget.rightControl()->getViewWidget();
    left->scaleImageView(4.0, true);
    left->scaleImageView(6.0, true);
    EXPECT_FLOAT_EQ(widget.leftControl()->getCurrentZoom(), 600.0f);
    EXPECT_FLOAT_EQ(widget.rightControl()->getCurrentZoom(), 300.0f) << "the right view follows the left one's zoom";

    right->scaleImageView(5.0, true);
    EXPECT_FLOAT_EQ(widget.rightControl()->getCurrentZoom(), 500.0f);
    EXPECT_FLOAT_EQ(widget.leftControl()->getCurrentZoom(), 1000.0f) << "and the left view follows the right one's";
}

// A difference asked for over the images both views showed is never shown once the right view shows another image in
// its place; the difference of the images the views show now is.
TEST(BlinkComparisonViewArchitecture, ADifferenceIsShownOnlyWhileItIsOfTheImagesBothViewsShow)
{
    InstalledViewerContext installed(1u);
    ASSERT_TRUE(installed.valid());
    an::ui::ComparisonViewWidget widget;
    std::optional<an::consumer::ImageAsset> leftImage = an::tests::viewer::makeAsset<float>(
        installed.session(), 8u, 6u, an::tests::viewer::Channels::Grey,
        [](std::uint32_t x, std::uint32_t, std::uint32_t) { return 0.1f * static_cast<float>(x); });
    std::optional<an::consumer::ImageAsset> rightImage = flatImage(installed.session(), 8u, 6u, 0.5f);
    // An image of another size, whose difference with the left one cannot be built, and one of the same size.
    std::optional<an::consumer::ImageAsset> otherSize = flatImage(installed.session(), 4u, 3u, 0.5f);
    std::optional<an::consumer::ImageAsset> sameSize = flatImage(installed.session(), 8u, 6u, 0.5f);
    ASSERT_TRUE(leftImage.has_value() && rightImage.has_value() && otherSize.has_value() && sameSize.has_value());
    widget.setLeftImage(std::move(*leftImage));
    widget.setRightImage(std::move(*rightImage));
    ASSERT_TRUE(settleComparison(widget));
    {
        // The session's one thread is held, so the difference of the first two images waits for it while the right
        // view is handed another image.
        std::optional<an::consumer::OperationScope> gate = an::tests::viewer::admitted(installed.session());
        ASSERT_TRUE(gate.has_value());
        widget.setMode(an::ui::ComparisonViewMode::Difference);
        widget.setRightImage(std::move(*otherSize));
        an::tests::viewer::pumpQuietly();
    }
    ASSERT_TRUE(settleComparison(widget));
    EXPECT_FALSE(widget.leftControl()->hasAlternateImage())
        << "a difference with the image the right view no longer shows was shown";
    EXPECT_FALSE(widget.leftControl()->getViewWidget()->isAlternateImageRenderEnabled());

    widget.setRightImage(std::move(*sameSize));
    ASSERT_TRUE(settleComparison(widget));
    ASSERT_TRUE(widget.leftControl()->hasAlternateImage()) << "the difference of the images shown now is shown";
    EXPECT_TRUE(widget.leftControl()->getViewWidget()->isAlternateImageRenderEnabled());
    const std::optional<float> sample =
        an::tests::viewer::sampleOf<float>(*widget.leftControl()->alternateAsset(), 2u, 1u);
    ASSERT_TRUE(sample.has_value());
    EXPECT_NEAR(*sample, 0.3f, 1e-6f);
}

TEST(BlinkComparisonViewArchitecture, BlinkDialogDependsOnComparisonViewWidgetRatherThanImagePreviewPanel)
{
    const std::string header =
        loadTextFile("src/AfterNightProcesses/processes/ImageIntegration/dialogs/BlinkComparatorDialog.h");
    const std::string source =
        loadTextFile("src/AfterNightProcesses/processes/ImageIntegration/dialogs/BlinkComparatorDialog.cpp");

    ASSERT_FALSE(header.empty()) << "Could not load BlinkComparatorDialog.h";
    ASSERT_FALSE(source.empty()) << "Could not load BlinkComparatorDialog.cpp";

    EXPECT_NE(header.find("class ComparisonViewWidget;"), std::string::npos);
    EXPECT_EQ(header.find("class ImagePreviewPanel;"), std::string::npos)
        << "BlinkComparatorDialog should expose no ImagePreviewPanel";
    EXPECT_NE(source.find("#include \"widgets/ComparisonViewWidget.h\""), std::string::npos);
    EXPECT_EQ(source.find("ImagePreviewPanel"), std::string::npos)
        << "BlinkComparatorDialog.cpp should construct and synchronize no ImagePreviewPanel instances";
}
