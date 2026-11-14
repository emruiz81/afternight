// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// The application's teardown in its own order, over a main window of its own: the window cases share one window for
// their suite, so this case makes, uses and destroys another, after or before them, never while theirs exists.

#include "app/AfterNightApp.h"

#include "AppTestSupport.h"
#include "Test_ControlsSeams/FreedMemoryQuarantine.h"
#include "app/ApplicationSession.h"
#include "core/ApplicationCore.h"
#include "controls/IAppContext.h"
#include "controls/ImageViewControl.h"
#include "controls/settings/SettingsKeys.h"
#include "session/ImageDocument.h"

#include <AfterNightConsumerRuntime/EngineSession.h>
#include <AfterNightConsumerRuntime/ImageAsset.h>

// The console's header names the logger's levels without including it.
#include <AfterNightPlatform/Logging.h>

#include "controls/LogViewerControl.h"

#include <QAction>
#include <QPointer>
#include <QThreadPool>
#include <QWidget>

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <future>
#include <memory>
#include <optional>
#include <thread>
#include <utility>

TEST(ApplicationTeardown, TheWindowTakesItsContextDownAndTheSessionShutsDownCleanly)
{
    ASSERT_EQ(AfterNightApp::instanceOrNull(), nullptr);
    ASSERT_EQ(an::ui::appContext(), nullptr);

    an::ui::ApplicationSession session;
    an::ui::shell::ApplicationCore core;
    AfterNightApp* const window = AfterNightApp::createInstance(core);
    window->initialize(session);
    ASSERT_TRUE(session.isOpen());
    an::ui::IAppContext* const context = an::ui::appContext();
    ASSERT_NE(context, nullptr);
    ASSERT_EQ(context->getMainWindow(), window);

    // A view still open when the application closes - with nothing unsaved, so closing asks nothing: its document holds
    // an image of the session until the window closes its views.
    std::optional<an::ui::ImageDocument> document =
        an::tests::app::filledDocument(session.engineSession(), QStringLiteral("open when the application closes"));
    ASSERT_TRUE(document.has_value());
    const std::weak_ptr<const an::consumer::ImageAsset> image = document->asset();
    ImageViewControl* const view = context->getViewFactory()->createNewImageViewControlFromImage(
        nullptr, std::move(*document), nullptr, "", ViewControlMode::standard, nullptr, false, false);
    ASSERT_NE(view, nullptr);
    const QPointer<ImageViewControl> viewGuard(view);

    // The application's exit: the window is closed, which closes every view, and the event loop returns.
    ASSERT_TRUE(window->close());

    const an::Status shut = an::ui::shutDownApplication(session, []() { AfterNightApp::destroyInstance(); });

    EXPECT_EQ(AfterNightApp::instanceOrNull(), nullptr);
    EXPECT_EQ(an::ui::appContext(), nullptr) << "the window left the context it installed behind it";
    EXPECT_TRUE(viewGuard.isNull());
    EXPECT_TRUE(image.expired()) << "the closed view's image is still held: " << image.use_count();
    EXPECT_TRUE(shut.ok()) << "the session did not shut down cleanly after the window went: " << shut.message();
    EXPECT_EQ(session.engineSession().state(), an::consumer::SessionState::Closed);
}

// Quitting with the Session Console open. The console is one of the window's children, which the window's base class
// deletes after the window's own state is gone, so nothing the console signals on its way out may reach that state.
// The window goes under a quarantine that poisons every block this executable frees and keeps it: a handler that read
// the window's state after it went faults, rather than reading what the freed state still held.
TEST(ApplicationTeardown, QuittingWithTheSessionConsoleOpenReadsNothingOfTheWindowAfterItsStateIsGone)
{
    ASSERT_EQ(AfterNightApp::instanceOrNull(), nullptr);
    ASSERT_EQ(an::ui::appContext(), nullptr);

    an::ui::ApplicationSession session;
    an::ui::shell::ApplicationCore core;
    AfterNightApp* const window = AfterNightApp::createInstance(core);
    window->initialize(session);
    ASSERT_TRUE(session.isOpen());
    auto* const console = window->findChild<QAction*>(QStringLiteral("actionLog_Viewer"));
    ASSERT_NE(console, nullptr);
    console->trigger();
    ASSERT_TRUE(console->isChecked());
    // The console declares no meta-object of its own, so it is told by its type.
    const QList<QWidget*> children = window->findChildren<QWidget*>();
    ASSERT_TRUE(std::any_of(children.cbegin(), children.cend(), [](QWidget* child) {
        return dynamic_cast<LogViewerControl*>(child) != nullptr;
    })) << "the Session Console did not open among the window's children";

    ASSERT_TRUE(window->close());
    an::Status shut = an::Status::success();
    {
        const an::tests::viewer::FreedMemoryQuarantine quarantine;
        shut = an::ui::shutDownApplication(session, []() { AfterNightApp::destroyInstance(); });
    }

    EXPECT_EQ(AfterNightApp::instanceOrNull(), nullptr);
    EXPECT_EQ(an::ui::appContext(), nullptr);
    EXPECT_TRUE(shut.ok()) << "the session did not shut down cleanly after the window went: " << shut.message();
}

// A read of the settings still waiting on the thread pool when the application closes - a process window's read of its
// frame rate waits there behind the frames it paces, holding the store it asks - reads the store while it is still
// there: the window lets the pool's work finish before its settings stores go. The read is held until the teardown has
// begun, and the window goes under the quarantine, so a read that reached a store already gone faults.
TEST(ApplicationTeardown, ASettingsReadStillWaitingOnThePoolWhenTheApplicationClosesFindsTheStore)
{
    ASSERT_EQ(AfterNightApp::instanceOrNull(), nullptr);
    ASSERT_EQ(an::ui::appContext(), nullptr);

    an::ui::ApplicationSession session;
    an::ui::shell::ApplicationCore core;
    AfterNightApp* const window = AfterNightApp::createInstance(core);
    window->initialize(session);
    ASSERT_TRUE(session.isOpen());
    an::ui::IAppContext* const context = an::ui::appContext();
    ASSERT_NE(context, nullptr);
    an::ui::IGlobalSettingsProvider* const settings = context->getGlobalSettings();
    ASSERT_NE(settings, nullptr);
    const QString rate = QString::fromLatin1(SettingsKeys::PROCESSING_PREVIEW_FRAME_RATE);
    ASSERT_TRUE(settings->upsert(rate, QStringLiteral("12")));

    std::promise<void> release;
    std::shared_future<void> released = release.get_future().share();
    auto answered = std::make_shared<QString>();
    QThreadPool::globalInstance()->start([settings, released, answered, rate]() {
        released.wait();
        *answered = settings->select(rate, QStringLiteral("30"));
    });

    ASSERT_TRUE(window->close());
    an::Status shut = an::Status::success();
    {
        const an::tests::viewer::FreedMemoryQuarantine quarantine;
        std::thread releaser([&release]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
            release.set_value();
        });
        shut = an::ui::shutDownApplication(session, []() { AfterNightApp::destroyInstance(); });
        releaser.join();
    }

    EXPECT_EQ(*answered, QStringLiteral("12")) << "the read did not find the store it asked";
    EXPECT_EQ(AfterNightApp::instanceOrNull(), nullptr);
    EXPECT_EQ(an::ui::appContext(), nullptr);
    EXPECT_TRUE(shut.ok()) << "the session did not shut down cleanly after the window went: " << shut.message();
}
