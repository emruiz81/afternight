// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// A histogram or statistics window closed while its count or measurement waits for the session keeps nothing: the
/// work never reaches the window it outlived, and nothing the window took from the session survives it. The windows
/// and the view they read are compiled into this executable so the freed-memory quarantine, which replaces the global
/// allocation functions, holds every block they free poisoned until the work has finished.

#include "Test_ControlsStatistics/StatisticsTestSupport.h"

#include "Test_ControlsSeams/FreedMemoryQuarantine.h"

#include "controls/ImageHistogramControl.h"
#include "controls/ImageStatisticsControl.h"

#include <AfterNightConsumerRuntime/OperationScope.h>

#include <QThreadPool>

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
using an::tests::statistics::pumpQuietly;
using an::tests::statistics::settle;
using an::tests::statistics::StatisticsTestContext;
using an::tests::statistics::ViewHandle;
using an::tests::viewer::FreedMemoryQuarantine;

/// A sixteen-bit grey image: the first column at zero, the last at full scale, the rest at a thousand times the column.
[[nodiscard]] std::uint16_t columnCode(const std::uint32_t x, std::uint32_t, std::uint32_t)
{
    return static_cast<std::uint16_t>(x == 0u ? 0u : (x == 9u ? 65535u : 1000u * x));
}

class StatisticsCloseTest : public ::testing::Test
{
protected:
    void open(const std::uint32_t width)
    {
        m_context = openStatisticsContext(width);
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

    /// A view showing a grey document of one level.
    [[nodiscard]] ViewHandle levelView(const float level)
    {
        ViewHandle view = makeView("histogram");
        std::optional<an::ui::ImageDocument> document = makeDocument<float>(
            session(), 16u, 8u, Channels::Grey, [level](std::uint32_t, std::uint32_t, std::uint32_t) { return level; });
        EXPECT_TRUE(document.has_value());
        if (document.has_value())
        {
            view->setDocument(std::move(*document));
        }
        return view;
    }

    /// A view showing the column image.
    [[nodiscard]] ViewHandle columnView()
    {
        ViewHandle view = makeView("statistics");
        std::optional<an::ui::ImageDocument> document =
            makeDocument<std::uint16_t>(session(), 10u, 4u, Channels::Grey, columnCode);
        EXPECT_TRUE(document.has_value());
        if (document.has_value())
        {
            view->setDocument(std::move(*document));
        }
        return view;
    }

    std::unique_ptr<StatisticsTestContext> m_context;
    std::optional<InstalledContext> m_installed;
};

} // namespace

TEST_F(StatisticsCloseTest, AHistogramWindowClosedWhileItsCountWaitsKeepsNothing)
{
    open(1u);
    ViewHandle view = levelView(0.5f);
    ASSERT_TRUE(settle(*view));
    {
        FreedMemoryQuarantine quarantine;
        {
            std::optional<an::consumer::OperationScope> gate = an::tests::viewer::admitted(session());
            ASSERT_TRUE(gate.has_value());
            (void)new ImageHistogramControl(view.get());
            pumpQuietly();
            view.reset();
        }
        QThreadPool::globalInstance()->waitForDone();
        pumpQuietly();
        if constexpr (an::tests::viewer::kQuarantineHoldsFreedBlocks)
        {
            EXPECT_GT(quarantine.held(), 0u);
        }
    }

    EXPECT_TRUE(m_context->outstanding().none());
    const an::Status shutdown = m_context->shutdownSession();
    EXPECT_TRUE(shutdown.ok()) << shutdown.message();
}

TEST_F(StatisticsCloseTest, AStatisticsWindowClosedWhileItsMeasurementWaitsKeepsNothing)
{
    open(1u);
    ViewHandle view = columnView();
    ASSERT_TRUE(settle(*view));
    {
        FreedMemoryQuarantine quarantine;
        {
            std::optional<an::consumer::OperationScope> gate = an::tests::viewer::admitted(session());
            ASSERT_TRUE(gate.has_value());
            (void)new ImageStatisticsControl(view.get());
            pumpQuietly();
            view.reset();
        }
        QThreadPool::globalInstance()->waitForDone();
        pumpQuietly();
        if constexpr (an::tests::viewer::kQuarantineHoldsFreedBlocks)
        {
            EXPECT_GT(quarantine.held(), 0u);
        }
    }

    EXPECT_TRUE(m_context->outstanding().none());
    const an::Status shutdown = m_context->shutdownSession();
    EXPECT_TRUE(shutdown.ok()) << shutdown.message();
}
