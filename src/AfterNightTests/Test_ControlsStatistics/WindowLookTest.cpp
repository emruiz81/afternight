// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The Statistics and the Histogram windows under the Observatory profile: each has a heading that says the image it
/// measures, a footer whose rightmost button is the primary one, text that meets the contrast rule, nothing clipped as
/// it opens or at its least size, and opens within its screen's available area - in both densities.

#include "Test_ControlsStatistics/StatisticsTestSupport.h"

#include "components/AnWindowHeading.h"
#include "controls/ImageHistogramControl.h"
#include "controls/ImageStatisticsControl.h"
#include "fixtures/WindowLook.h"

#include <QLabel>

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <optional>

namespace
{

using an::tests::statistics::Channels;
using an::tests::statistics::InstalledContext;
using an::tests::statistics::makeDocument;
using an::tests::statistics::makeView;
using an::tests::statistics::openStatisticsContext;
using an::tests::statistics::StatisticsTestContext;
using an::tests::statistics::ViewHandle;

/// The suite's context and a view of a colour image, installed for a case.
class WindowLook : public ::testing::Test
{
protected:
    void SetUp() override
    {
        m_context = openStatisticsContext(2u);
        ASSERT_NE(m_context, nullptr);
        m_installed.emplace(*m_context);
        m_view = makeView("M31_Lum");
        std::optional<an::ui::ImageDocument> document = makeDocument<float>(
            m_context->engineSession(), 32u, 24u, Channels::Rgb, [](std::uint32_t x, std::uint32_t, std::uint32_t c) {
                return 0.1f * static_cast<float>(c + 1u) + 0.01f * x;
            });
        ASSERT_TRUE(document.has_value());
        m_view->setDocument(std::move(*document));
    }

    void TearDown() override
    {
        m_view.reset();
        m_installed.reset();
        m_context.reset();
    }

    std::unique_ptr<StatisticsTestContext> m_context;
    std::optional<InstalledContext> m_installed;
    ViewHandle m_view;
};

/// What the heading of a window of a view says over its title: where it belongs, and the view's image.
[[nodiscard]] QString eyebrowOf(QWidget& window)
{
    const auto* const heading = window.findChild<an::ui::AnWindowHeading*>(QStringLiteral("anWindowHeading"));
    return heading != nullptr ? heading->eyebrow() : QString();
}

} // namespace

TEST_F(WindowLook, Statistics)
{
    const an::tests::look::WindowLookMeasured measured = an::tests::look::expectWindowLook([this]() -> QWidget* {
        auto* const window = new ImageStatisticsControl(m_view.get());
        if (an::ui::windowsTakeHeadings())
        {
            EXPECT_EQ(eyebrowOf(*window), QStringLiteral("Analysis · M31_Lum"));
        }
        return window;
    });
    AN_WINDOW_LOOK_REPORT(measured);
}

TEST_F(WindowLook, Histogram)
{
    const an::tests::look::WindowLookMeasured measured = an::tests::look::expectWindowLook([this]() -> QWidget* {
        auto* const window = new ImageHistogramControl(m_view.get());
        if (an::ui::windowsTakeHeadings())
        {
            EXPECT_EQ(eyebrowOf(*window), QStringLiteral("Analysis · M31_Lum"));
        }
        return window;
    });
    AN_WINDOW_LOOK_REPORT(measured);
}

// A window's heading follows the image's name, as its title does.
TEST_F(WindowLook, AHeadingFollowsTheImagesName)
{
    const an::ui::testing::ObservatoryStyleScope scope(an::ui::testing::observatoryLooks()[0]);
    auto* const window = new ImageStatisticsControl(m_view.get());
    ASSERT_EQ(eyebrowOf(*window), QStringLiteral("Analysis · M31_Lum"));
    m_view->setUniqueName("M31_Lum_stretched");
    EXPECT_EQ(window->windowTitle(), QStringLiteral("Statistics <M31_Lum_stretched>"));
    EXPECT_EQ(eyebrowOf(*window), QStringLiteral("Analysis · M31_Lum_stretched"));
    an::tests::look::letGo(window);
}
