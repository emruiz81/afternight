// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// A view closed while its work is on its way keeps nothing: neither a file read waiting for the session nor an edit
/// whose image is being presented reaches the view it outlived, and nothing the view took from the session survives
/// it. The view is compiled into this executable so the freed-memory quarantine, which replaces the global allocation
/// functions, holds every block it frees poisoned until the work has finished - a completion that reached the closed
/// view would fault rather than read what the view held.

#include "Test_ControlsSeams/FreedMemoryQuarantine.h"
#include "Test_ControlsViewer/ViewerTestContext.h"

#include <AfterNightConsumerRuntime/OperationScope.h>

#include <QTemporaryDir>
#include <QThreadPool>

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <optional>

namespace
{

using namespace an::tests::viewer;

/// A grey Float32 image whose sample at (x, y) is (x + 10 y) / 100.
[[nodiscard]] std::optional<an::ui::ImageDocument> numberedDocument(const an::consumer::EngineSession& session,
                                                                    std::uint32_t width, std::uint32_t height)
{
    return makeDocument<float>(
        session, width, height, Channels::Grey,
        [](std::uint32_t x, std::uint32_t y, std::uint32_t) { return static_cast<float>(x + 10u * y) / 100.0f; });
}

class ViewerCloseTest : public ::testing::Test
{
protected:
    void open(std::uint32_t width)
    {
        m_context = openViewerContext(width);
        ASSERT_NE(m_context, nullptr);
        m_installed.emplace(*m_context);
    }

    [[nodiscard]] const an::consumer::EngineSession& session() const
    {
        return m_context->engineSession();
    }

    void TearDown() override
    {
        m_installed.reset();
        m_context.reset();
    }

    std::unique_ptr<ViewerTestContext> m_context;
    std::optional<InstalledContext> m_installed;
    QTemporaryDir m_directory;
};

} // namespace

TEST_F(ViewerCloseTest, AViewClosedWhileAReadIsOnItsWayKeepsNothing)
{
    open(1u);
    const QString path = m_directory.filePath(QStringLiteral("frame.fits"));
    {
        std::optional<an::consumer::ImageAsset> asset = makeAsset<float>(
            session(), 4u, 4u, Channels::Grey, [](std::uint32_t, std::uint32_t, std::uint32_t) { return 0.5f; });
        ASSERT_TRUE(asset.has_value());
        writeFitsFile(session(), *asset, path);
    }
    ViewHandle view = makeView("closing");
    {
        FreedMemoryQuarantine quarantine;
        {
            std::optional<an::consumer::OperationScope> gate = admitted(session());
            ASSERT_TRUE(gate.has_value());
            view->loadImageFromFile(path.toStdString());
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

TEST_F(ViewerCloseTest, AViewClosedWhileItsImageIsBeingPresentedKeepsNothing)
{
    open(1u);
    ViewHandle view = makeView("closing");
    std::optional<an::ui::ImageDocument> document = numberedDocument(session(), 8u, 8u);
    ASSERT_TRUE(document.has_value());
    {
        FreedMemoryQuarantine quarantine;
        {
            std::optional<an::consumer::OperationScope> gate = admitted(session());
            ASSERT_TRUE(gate.has_value());
            view->setDocument(std::move(*document));
            view->horizontalMirror();
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
