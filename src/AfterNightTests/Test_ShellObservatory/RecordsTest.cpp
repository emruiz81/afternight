// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// The records of the interface the running application is held to, read over the Observatory window: what the process
// registry states of every process, and the key sequences the Session Console and a view answer. The window is started
// first, because a descriptor is named by the menu entry that binds it.

#include "Test_ShellObservatory/InventoryRecords.h"
#include "Test_ShellObservatory/ObservatoryWindowCase.h"
#include "fixtures/AppTestSupport.h"
#include "fixtures/BaselineRecord.h"

#include "chrome/ConsoleDrawer.h"

#include "controls/IAppContext.h"
#include "controls/ImageViewControl.h"
#include "controls/ViewControlMode.h"
#include "session/ImageDocument.h"

// The console's header names the logger's levels without including it.
#include <AfterNightPlatform/Logging.h>

#include "controls/LogViewerControl.h"

#include "app/ProcessRegistry.h"

#include <QCoreApplication>
#include <QDir>
#include <QEvent>
#include <QFileInfo>
#include <QString>
#include <QStringList>
#include <QWidget>

#include <gtest/gtest.h>

#include <cstddef>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace
{

using an::tests::observatory::ObservatoryWindowCase;

/// Lets the window settle: posted work delivered, the deletions done, the layouts applied.
void settle()
{
    for (int round = 0; round < 3; ++round)
    {
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QCoreApplication::processEvents();
    }
}

// The repository the suite was built from: the first folder at or above the working directory, the suite's own folder
// or this source file that holds both `src` and `build`. Empty when there is none.
QString repositoryRoot()
{
    const QStringList seeds = {QDir::currentPath(), QCoreApplication::applicationDirPath(),
                               QFileInfo(QString::fromUtf8(__FILE__)).absolutePath()};
    for (const QString& seed : seeds)
    {
        QDir folder(seed);
        do
        {
            if (QFileInfo::exists(folder.filePath(QStringLiteral("src"))) &&
                QFileInfo::exists(folder.filePath(QStringLiteral("build"))))
            {
                return folder.absolutePath();
            }
        } while (folder.cdUp());
    }
    return {};
}

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

// The registry's cases run over the Observatory window, started once for them.
class ProcessRegistry : public ObservatoryWindowCase
{
};

// Every process the application registers is the one the record holds, in the registry's order: its id, its names,
// its category, its launch mode, its menu action, its guards, its outputs and its help page. A process renamed,
// moved to another category, retired or added fails here until the record's entry says so.
TEST_F(ProcessRegistry, InventoryMatchesTheRecord)
{
    an::tests::fixtures::BaselineRecord record(QString::fromLatin1(an::tests::app::kProcessInventoryPath),
                                               an::tests::app::processInventoryPreamble());

    EXPECT_TRUE(record.matches(QString::fromLatin1(an::tests::app::kProcessesSection),
                               an::tests::app::processInventoryEntries()));
}

// Every process of the application states the page of the user guide that Help opens from its task, and the guide has
// that page: the built guide names a page as its source with `.html` for `.md`. An extension's process has no page of
// the guide.
TEST_F(ProcessRegistry, EveryProcessStatesAPageTheUserGuideHas)
{
    const QString root = repositoryRoot();
    ASSERT_FALSE(root.isEmpty()) << "the repository's root was not found";
    const QDir pages(QDir(root).filePath(QStringLiteral("docs/user-guide/docs/en")));
    ASSERT_TRUE(pages.exists()) << pages.absolutePath().toStdString();

    std::size_t stated = 0u;
    for (const an::ui::process_registry::ProcessDescriptor& descriptor :
         an::ui::process_registry::ProcessRegistry::instance().all())
    {
        if (descriptor.is_extension_process)
        {
            continue;
        }
        const QString helpPath = QString::fromStdString(descriptor.helpPath);
        ASSERT_TRUE(helpPath.endsWith(QStringLiteral(".html")))
            << descriptor.id << " states the page '" << descriptor.helpPath << "'";
        const QString source = helpPath.chopped(5) + QStringLiteral(".md");
        EXPECT_TRUE(QFileInfo(pages.filePath(source)).isFile())
            << descriptor.id << " states " << descriptor.helpPath << ", and the guide has no " << source.toStdString();
        ++stated;
    }
    EXPECT_GT(stated, 0u);
}

// The keys' cases run over the Observatory window, started once for them; each ends with the Session Console closed.
class Shortcuts : public ObservatoryWindowCase
{
protected:
    void TearDown() override
    {
        window().setSessionConsoleVisible(false);
        settle();
        ObservatoryWindowCase::TearDown();
    }
};

// The key sequences the Session Console and a view answer are the ones the record holds: every shortcut an action or a
// shortcut object of the console carries, and every key press a view takes for itself in each of its modes - an
// image's view, a process's real-time preview, a list's preview. A shortcut added, removed or moved to another key
// fails here until the record's entry says so.
TEST_F(Shortcuts, MatchTheRecord)
{
    an::tests::fixtures::BaselineRecord record(QString::fromLatin1(an::tests::app::kShortcutInventoryPath),
                                               an::tests::app::shortcutInventoryPreamble());

    // The Session Console is made when it is first shown, so it is shown, in the window's drawer.
    window().setSessionConsoleVisible(true);
    settle();
    LogViewerControl* const console = window().consoleDrawer()->console();
    ASSERT_NE(console, nullptr) << "the Session Console did not open in the window's drawer";
    EXPECT_TRUE(record.matches(QString::fromLatin1(an::tests::app::kConsoleShortcutsSection),
                               an::tests::app::shortcutEntries(*console, QStringLiteral("the console"))));

    // An image's view, made as the application makes one: its keys act on a view the window holds.
    std::optional<an::ui::ImageDocument> document =
        an::tests::app::filledDocument(session(), QStringLiteral("keys"), 0.25f);
    ASSERT_TRUE(document.has_value());
    ImageViewControl* const view = an::ui::appContext()->getViewFactory()->createNewImageViewControlFromImage(
        nullptr, std::move(*document), nullptr, "keys");
    ASSERT_NE(view, nullptr);
    const QStringList standard = an::tests::app::viewShortcutEntries(*view);
    closeWhatTheKeysOpened(*view);
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
