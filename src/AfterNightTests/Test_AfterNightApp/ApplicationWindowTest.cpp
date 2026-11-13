// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// The application's main window, made as the application makes it: initialized with the session the
// caller owns, which it opens from the settings store's limits; its context lending that session, the RAW policy and
// the output intents its settings store publishes; its view factory handing documents to the views it makes, from the
// GUI thread or from a worker that never waits for it; its listeners; and its teardown, in the application's order.

#include "app/AfterNightApp.h"

#include "AppTestSupport.h"
#include "InventoryRecords.h"
#include "app/AfterNightAppListener.h"
#include "app/ApplicationSession.h"
#include "app/FilePolicies.h"
#include "controls/IAppContext.h"
#include "controls/ImageViewControl.h"
#include "controls/ViewControlMode.h"
#include "controls/settings/ExtensionManagerPage.h"
#include "controls/settings/ProcessingLimits.h"
#include "session/IEngineSessionProvider.h"
#include "session/ImageDocument.h"

// The console's header names the logger's levels without including it.
#include <AfterNightPlatform/Logging.h>

#include "controls/LogViewerControl.h"

#include <AfterNightEngine/IO/CodecFormat.h>
#include <AfterNightEngine/IO/CodecRegistry.h>
#include <AfterNightEngine/IO/SequenceDescriptor.h>
#include <AfterNightEngine/Image/OutputIntent.h>

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QAction>
#include <QFileInfo>
#include <QString>
#include <QStringList>
#include <QThread>
#include <QWidget>

#include <gtest/gtest.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace
{

// Runs the event loop until `done` holds or five seconds have passed; answers whether it held.
bool processEventsUntil(const std::function<bool()>& done)
{
    QElapsedTimer timer;
    timer.start();
    while (!done() && timer.elapsed() < 5000)
    {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QThread::msleep(5);
    }
    return done();
}

an::ui::SettingReader settingReaderOf(an::ui::IGlobalSettingsProvider* settings)
{
    return [settings](const QString& key, const QString& fallback) { return settings->select(key, fallback); };
}

struct LifecycleCounters
{
    int created = 0;
    int renamed = 0;
    int destroyed = 0;
};

class LifecycleListener final : public AfterNightAppListener
{
public:
    explicit LifecycleListener(LifecycleCounters* counters)
        : AfterNightAppListener(an::ui::appContext() ? an::ui::appContext()->getAppEventBus() : nullptr),
          m_counters(counters)
    {
    }

    void onViewControlCreated(const ImageViewControl*) override
    {
        ++m_counters->created;
    }

    void onViewControlRenamed(const std::string&, const std::string&, const ImageViewControl*) override
    {
        ++m_counters->renamed;
    }

    void onViewControlDestroyed(const ImageViewControl*) override
    {
        ++m_counters->destroyed;
    }

private:
    LifecycleCounters* m_counters = nullptr;
};

} // namespace

class ApplicationWindow : public ::testing::Test
{
protected:
    // One main window for the suite, as the application has one for its run: made and initialized before the first
    // case, torn down after the last in the application's order.
    static void SetUpTestSuite()
    {
        s_session = std::make_unique<an::ui::ApplicationSession>();
        s_window = AfterNightApp::getInstance();
        s_window->initialize(*s_session);
    }

    static void TearDownTestSuite()
    {
        const an::Status shut = an::ui::shutDownApplication(*s_session, []() { AfterNightApp::destroyInstance(); });
        EXPECT_EQ(AfterNightApp::instanceOrNull(), nullptr);
        EXPECT_EQ(an::ui::appContext(), nullptr) << "the window left its context installed after it went";
        EXPECT_TRUE(shut.ok()) << "the session did not shut down cleanly after the window went: " << shut.message();
        s_session.reset();
        s_window = nullptr;
    }

    [[nodiscard]] static an::ui::IAppContext& context()
    {
        an::ui::IAppContext* const installed = an::ui::appContext();
        EXPECT_NE(installed, nullptr);
        return *installed;
    }

    [[nodiscard]] static const an::consumer::EngineSession& session()
    {
        return s_session->engineSession();
    }

    // Closes a view the case made and lets the window delete it.
    static void closeView(ImageViewControl* view)
    {
        if (view != nullptr)
        {
            s_window->removeImageViewControl(view, true);
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
            QCoreApplication::processEvents();
        }
    }

    static inline std::unique_ptr<an::ui::ApplicationSession> s_session;
    static inline AfterNightApp* s_window = nullptr;
};

TEST_F(ApplicationWindow, InitializingOpensTheSessionTheCallerOwnsFromTheStoredLimits)
{
    ASSERT_TRUE(s_session->isOpen());

    an::ui::IAppContext& installed = context();
    EXPECT_EQ(installed.getMainWindow(), s_window);
    an::ui::IEngineSessionProvider* const provider = installed.getEngineSessionProvider();
    ASSERT_NE(provider, nullptr);
    EXPECT_EQ(&provider->engineSession(), &s_session->engineSession());
    EXPECT_EQ(s_window->engineSessionProvider(), provider);

    // Nothing waits for the next start: the session runs with what the store states.
    const an::ui::ProcessingLimits stored =
        an::ui::readProcessingLimits(settingReaderOf(installed.getGlobalSettings()));
    const an::ui::ProcessingLimitsState state = an::ui::processingLimitsState(provider->engineSession(), stored);
    EXPECT_FALSE(state.threadsPendingRestart);
    EXPECT_FALSE(state.memoryPendingRestart);
    EXPECT_GE(state.width, 1u);
}

TEST_F(ApplicationWindow, TheRawPolicyItLendsIsTheOneItsSettingsStorePublishes)
{
    an::ui::IAppContext& installed = context();
    an::ui::IRawOpenPolicyService* const service = installed.getRawOpenPolicyService();
    ASSERT_NE(service, nullptr);
    const std::shared_ptr<const an::ui::RawOpenPolicy> before = service->rawOpenPolicy();
    ASSERT_NE(before, nullptr);
    const an::ui::RawOpenSettings original = before->settings();
    EXPECT_EQ(original, an::ui::readRawOpenSettings(settingReaderOf(installed.getGlobalSettings())));

    an::ui::RawOpenSettings changed = original;
    changed.autoRotate = !original.autoRotate;
    changed.whiteBalance = an::ui::RawWhiteBalanceSetting::Custom;
    changed.customGains = {2.0, 1.0, 1.5};
    const an::Status published = service->setRawOpenSettings(changed);
    ASSERT_TRUE(published.ok()) << published.message();

    const std::shared_ptr<const an::ui::RawOpenPolicy> after = service->rawOpenPolicy();
    ASSERT_NE(after, nullptr);
    EXPECT_NE(after.get(), before.get());
    EXPECT_EQ(after->settings(), changed);
    EXPECT_EQ(an::ui::readRawOpenSettings(settingReaderOf(installed.getGlobalSettings())), changed);
    // A read already on its way keeps the policy it was submitted under.
    EXPECT_EQ(before->settings(), original);

    ASSERT_TRUE(service->setRawOpenSettings(original).ok());
}

TEST_F(ApplicationWindow, TheOutputIntentItBuildsIsTheOneTheExportSettingsGive)
{
    an::ui::IAppContext& installed = context();
    an::ui::IOutputIntentProvider* const provider = installed.getOutputIntentProvider();
    ASSERT_NE(provider, nullptr);
    an::Result<an::consumer::SessionHold> hold = session().hold();
    ASSERT_TRUE(hold.hasValue());
    const std::optional<an::ImageDescriptor> source = an::tests::app::monoFloatDescriptor(hold->engine(), 16u, 8u);
    ASSERT_TRUE(source.has_value());
    const an::Result<std::shared_ptr<const an::CodecRegistry>> registry = an::ui::createProductCodecRegistry();
    ASSERT_TRUE(registry.hasValue());

    for (const an::CodecFormat container : {an::CodecFormat::Fits, an::CodecFormat::Xisf, an::CodecFormat::Tiff,
                                            an::CodecFormat::Png, an::CodecFormat::Jpeg})
    {
        const an::Result<an::OutputIntent> built = provider->buildOutputIntent(*source, container);
        const an::Result<an::OutputIntent> expected = an::ui::buildExportIntent(
            an::ui::readExportSettings(settingReaderOf(installed.getGlobalSettings())), **registry, *source, container);
        ASSERT_TRUE(built.hasValue()) << built.status().message();
        ASSERT_TRUE(expected.hasValue()) << expected.status().message();
        EXPECT_EQ(*built, *expected);
    }

    // The intent is the container's: a PNG is written compressed where a FITS file is not.
    const an::Result<an::OutputIntent> fits = provider->buildOutputIntent(*source, an::CodecFormat::Fits);
    const an::Result<an::OutputIntent> png = provider->buildOutputIntent(*source, an::CodecFormat::Png);
    ASSERT_TRUE(fits.hasValue());
    ASSERT_TRUE(png.hasValue());
    EXPECT_NE(*fits, *png);
}

TEST_F(ApplicationWindow, TheViewFactoryHandsTheDocumentToTheViewItMakes)
{
    std::optional<an::ui::ImageDocument> document = an::tests::app::filledDocument(session(), "factory");
    ASSERT_TRUE(document.has_value());
    const std::uint64_t identity = document->identity();

    ImageViewControl* const view = context().getViewFactory()->createNewImageViewControlFromImage(
        nullptr, std::move(*document), nullptr, "made by the factory");

    ASSERT_NE(view, nullptr);
    EXPECT_FALSE(document->valid());
    EXPECT_EQ(view->document().identity(), identity);
    EXPECT_TRUE(view->document().isOpen());
    EXPECT_TRUE(view->hasImage());
    closeView(view);
}

TEST_F(ApplicationWindow, ADocumentNoViewIsMadeForStaysWithTheCaller)
{
    std::optional<an::ui::ImageDocument> document = an::tests::app::filledDocument(session(), "closed");
    ASSERT_TRUE(document.has_value());
    const std::uint64_t identity = document->identity();
    document->close();

    ImageViewControl* const view =
        context().getViewFactory()->createNewImageViewControlFromImage(nullptr, std::move(*document), nullptr);

    EXPECT_EQ(view, nullptr);
    ASSERT_TRUE(document->valid());
    EXPECT_EQ(document->identity(), identity);
}

TEST_F(ApplicationWindow, AWorkerHandsADocumentToTheGuiThreadWithoutWaitingForIt)
{
    std::optional<an::ui::ImageDocument> document = an::tests::app::filledDocument(session(), "from a worker");
    ASSERT_TRUE(document.has_value());
    const std::uint64_t identity = document->identity();
    const std::size_t viewsBefore = context().getViewControlManager()->getListOfViewControls().size();

    std::atomic_bool finished = false;
    std::atomic_bool workerReturned = false;
    std::thread worker([&]() {
        s_window->createNewImageViewControlFromImageAsync(nullptr, std::move(*document), nullptr, "made on a worker",
                                                          ViewControlMode::standard, nullptr, false, true, &finished);
        workerReturned = true;
    });
    worker.join();

    // The worker returned while the GUI thread had not yet run the event: nothing it did waited for that thread.
    EXPECT_TRUE(workerReturned.load());
    EXPECT_FALSE(finished.load());

    ASSERT_TRUE(processEventsUntil([&finished]() { return finished.load(); }));
    ImageViewControl* made = nullptr;
    for (ImageViewControl* view : context().getViewControlManager()->getListOfViewControls())
    {
        if (view != nullptr && view->document().identity() == identity)
        {
            made = view;
        }
    }
    ASSERT_NE(made, nullptr);
    EXPECT_EQ(context().getViewControlManager()->getListOfViewControls().size(), viewsBefore + 1u);
    closeView(made);
}

TEST_F(ApplicationWindow, AWorkersDocumentNoViewIsMadeForIsClosedByTheEventThatCarriedIt)
{
    std::optional<an::ui::ImageDocument> document = an::tests::app::filledDocument(session(), "refused");
    ASSERT_TRUE(document.has_value());
    // A sequence view of a document that states no sequence is refused.
    const std::shared_ptr<const an::consumer::ImageAsset> asset = document->asset();
    const std::size_t viewsBefore = context().getViewControlManager()->getListOfViewControls().size();

    std::atomic_bool finished = false;
    std::thread worker([&]() {
        s_window->createNewImageViewControlFromSequenceAsync(nullptr, std::move(*document), nullptr, false, "",
                                                             ViewControlMode::standard, nullptr, false, true,
                                                             &finished);
    });
    worker.join();

    ASSERT_TRUE(processEventsUntil([&finished]() { return finished.load(); }));
    EXPECT_EQ(context().getViewControlManager()->getListOfViewControls().size(), viewsBefore);
    // Nothing keeps the document's image but this case once the event is gone: the event let the document go.
    EXPECT_TRUE(processEventsUntil([&asset]() { return asset.use_count() == 1; })) << asset.use_count();
}

TEST_F(ApplicationWindow, AListenerHearsOfViewsThroughItsContextUntilItGoes)
{
    LifecycleCounters persistent;
    LifecycleListener persistentListener(&persistent);
    ImageViewControl* view = nullptr;
    {
        LifecycleCounters transient;
        LifecycleListener transientListener(&transient);

        std::optional<an::ui::ImageDocument> document = an::tests::app::filledDocument(session(), "listened");
        ASSERT_TRUE(document.has_value());
        view = context().getViewFactory()->createNewImageViewControlFromImage(nullptr, std::move(*document), nullptr);
        ASSERT_NE(view, nullptr);
        const std::string oldName = view->getUniqueName();
        s_window->notifyViewControlRenamed(oldName + "-renamed", oldName, view);

        EXPECT_EQ(transient.created, 1);
        EXPECT_EQ(transient.renamed, 1);
    }

    closeView(view);

    EXPECT_EQ(persistent.created, 1);
    EXPECT_EQ(persistent.renamed, 1);
    EXPECT_EQ(persistent.destroyed, 1);
}

TEST_F(ApplicationWindow, ATemporarySequenceIsNamedForItsContainerInTheSessionsTemporaryDirectory)
{
    an::ui::ITempFileService* const temporary = context().getTempFileService();
    ASSERT_NE(temporary, nullptr);

    const QString ser = QString::fromStdString(temporary->createTempFilePathForNewSequence(an::SequenceFormat::Ser));
    const QString list =
        QString::fromStdString(temporary->createTempFilePathForNewSequence(an::SequenceFormat::ImageList));

    EXPECT_EQ(QFileInfo(ser).suffix().toLower(), QStringLiteral("ser"));
    EXPECT_NE(ser, list);
    EXPECT_TRUE(QDir::cleanPath(ser).startsWith(QDir::cleanPath(temporary->getTempDirPath()))) << ser.toStdString();
    EXPECT_TRUE(temporary->isTempFile(ser.toStdString()));
}

TEST_F(ApplicationWindow, TheExtensionsPageShowsTheExtensionIconTheApplicationLends)
{
    ASSERT_NE(context().getIconProvider(), nullptr);
    EXPECT_FALSE(context().getIconProvider()->getIcon(QStringLiteral("extension")).isNull());

    an::ui::ExtensionManagerPage page(nullptr);

    EXPECT_FALSE(page.pageIcon().isNull());
}

TEST_F(ApplicationWindow, TheSingleWindowIsTheOneTheContextIsInstalledFor)
{
    EXPECT_EQ(AfterNightApp::getInstance(), s_window);
    EXPECT_EQ(AfterNightApp::instanceOrNull(), s_window);
    EXPECT_EQ(context().getMainWindow(), s_window);
}

// ---- The keys the application answers, held to the record beside this suite.

namespace
{

// Closes what a view's keys opened over it - its tool windows are its own children - and restores it when a key
// maximized it.
void closeWhatTheKeysOpened(ImageViewControl& view)
{
    if (view.isMaximized())
    {
        view.switchMaximized();
    }
    for (QWidget* child : view.findChildren<QWidget*>(QString(), Qt::FindDirectChildrenOnly))
    {
        if (child->isWindow())
        {
            delete child;
        }
    }
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QCoreApplication::processEvents();
}

} // namespace

// The key sequences the application answers are the ones the record holds: every shortcut an action or a shortcut
// object of the window carries, those of the Session Console, and every key press a view takes for itself in each of
// its modes - an image's view, a process's real-time preview, a list's preview. A shortcut added, removed or moved to
// another key fails here until the record's entry says so.
TEST_F(ApplicationWindow, ShortcutsMatchTheRecord)
{
    an::tests::fixtures::BaselineRecord record(QString::fromLatin1(an::tests::app::kShortcutInventoryPath),
                                               an::tests::app::shortcutInventoryPreamble());

    // The Session Console is made when it is first shown, so it is shown, and its shortcuts are read apart from the
    // window's: the window's are then the same whether or not a person has opened the console.
    QAction* const consoleEntry = s_window->findChild<QAction*>(QStringLiteral("actionLog_Viewer"));
    ASSERT_NE(consoleEntry, nullptr);
    if (!consoleEntry->isChecked())
    {
        consoleEntry->trigger();
    }
    LogViewerControl* console = nullptr;
    for (QWidget* child : s_window->findChildren<QWidget*>())
    {
        // The console declares no meta-object of its own, so it is told by its type.
        if (auto* found = dynamic_cast<LogViewerControl*>(child))
        {
            console = found;
        }
    }
    ASSERT_NE(console, nullptr) << "the Session Console did not open among the window's children";
    EXPECT_TRUE(record.matches(QString::fromLatin1(an::tests::app::kWindowShortcutsSection),
                               an::tests::app::shortcutEntries(*s_window, QStringLiteral("the window"), console)));
    EXPECT_TRUE(record.matches(QString::fromLatin1(an::tests::app::kConsoleShortcutsSection),
                               an::tests::app::shortcutEntries(*console, QStringLiteral("the console"))));
    consoleEntry->trigger();
    QCoreApplication::processEvents();

    // An image's view, made as the application makes one: its keys act on a view the window holds.
    std::optional<an::ui::ImageDocument> document =
        an::tests::app::filledDocument(session(), QStringLiteral("keys"), 0.25f);
    ASSERT_TRUE(document.has_value());
    ImageViewControl* const view =
        context().getViewFactory()->createNewImageViewControlFromImage(nullptr, std::move(*document), nullptr, "keys");
    ASSERT_NE(view, nullptr);
    const QStringList standard = an::tests::app::viewShortcutEntries(*view);
    closeWhatTheKeysOpened(*view);
    closeView(view);
    EXPECT_TRUE(record.matches(QStringLiteral("view standard"), standard));

    // The two previews, made as a process and a list make theirs: with no image, so a key they take has nothing to
    // act on.
    const std::vector<std::pair<ViewControlMode, QString>> previews = {
        {ViewControlMode::rt_preview, QStringLiteral("view rt_preview")},
        {ViewControlMode::st_preview, QStringLiteral("view st_preview")},
    };
    for (const auto& [mode, section] : previews)
    {
        auto preview = std::make_unique<ImageViewControl>();
        preview->initialize("keys", mode);
        const QStringList taken = an::tests::app::viewShortcutEntries(*preview);
        closeWhatTheKeysOpened(*preview);
        EXPECT_TRUE(record.matches(section, taken));
    }
}
