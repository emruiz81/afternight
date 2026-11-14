// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The file dialogs the application opens: each over the window of the widget that asked, or over the shell's window
/// when none did; opened in the folder its context remembers, or the one opened last, and remembering where the person
/// went; and drawn by the system or by Qt as the settings say.

#include "ShellCoreTestSupport.h"
#include "ShellCoreViewSupport.h"

#include "core/ApplicationCore.h"
#include "services/FileDialogService.h"

#include "controls/FolderTrackerPersistence.h"
#include "controls/GlobalSettingsPersistence.h"
#include "controls/settings/SettingsKeys.h"

#include <gtest/gtest.h>

#include <QDialog>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QLineEdit>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QWidget>

namespace
{

using an::tests::shellcore::answerWhenShown;
using an::tests::shellcore::RecordingShell;
using an::tests::shellcore::ScopedStorageRoot;
using an::ui::shell::ApplicationCore;

/// What a case saw of the file dialog it answered.
struct Seen
{
    bool shown = false;
    QPointer<QWidget> parent;
    QString directory;
    bool builtIn = false;
};

/// The same folder however it is spelled.
[[nodiscard]] QString canonical(const QString& path)
{
    return QDir(path).canonicalPath();
}

class FileDialogServiceTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        ASSERT_TRUE(m_root.isValid());
        ASSERT_TRUE(m_files.isValid());
        m_core.openStores();
        ASSERT_TRUE(m_core.storesOpen());
        m_core.setShell(&m_shell);
    }

    void TearDown() override
    {
        m_core.setShell(nullptr);
        m_core.closeStores();
    }

    /// Records the next file dialog; it then goes to `goTo`, when given, and accepts `file` there, or is cancelled.
    void answerNextDialog(Seen& seen, const QString& goTo = {}, const QString& file = {})
    {
        answerWhenShown<QFileDialog>([&seen, goTo, file](QFileDialog& dialog) {
            seen.shown = true;
            seen.parent = dialog.parentWidget();
            seen.directory = dialog.directory().absolutePath();
            seen.builtIn = dialog.testOption(QFileDialog::DontUseNativeDialog);
            if (file.isEmpty())
            {
                dialog.reject();
                return;
            }
            // The name is typed as the person types it: the dialog does not take a selected file into a field that
            // has the focus.
            dialog.setDirectory(goTo);
            auto* name = dialog.findChild<QLineEdit*>(QStringLiteral("fileNameEdit"));
            EXPECT_NE(name, nullptr);
            if (name != nullptr)
            {
                name->setText(file);
            }
            static_cast<QDialog&>(dialog).done(QDialog::Accepted);
        });
    }

    /// A folder of the case's own, made under its files, and a file in it.
    [[nodiscard]] QString folderWithFile(const QString& name, const QString& file)
    {
        const QString folder = QDir(m_files.path()).absoluteFilePath(name);
        EXPECT_TRUE(QDir().mkpath(folder));
        QFile made(QDir(folder).absoluteFilePath(file));
        EXPECT_TRUE(made.open(QIODevice::WriteOnly));
        made.write("x");
        return folder;
    }

    QTemporaryDir m_root;
    ScopedStorageRoot m_scopedRoot{m_root.path()};
    QTemporaryDir m_files;
    RecordingShell m_shell;
    ApplicationCore m_core;
};

TEST_F(FileDialogServiceTest, ADialogNoWidgetAskedForOpensOverTheShellsWindow)
{
    Seen seen;
    answerNextDialog(seen);
    const QStringList chosen =
        m_core.fileDialogService().selectFiles(nullptr, QStringLiteral("Choose"), QString(), QStringList());

    ASSERT_TRUE(seen.shown);
    EXPECT_TRUE(chosen.isEmpty()) << "a cancelled dialog chose nothing";
    EXPECT_EQ(seen.parent.data(), m_shell.mainWindow());
}

TEST_F(FileDialogServiceTest, ADialogAWidgetAskedForOpensOverThatWidgetsWindow)
{
    QWidget window;
    auto* field = new QWidget(&window);
    Seen seen;
    answerNextDialog(seen);
    (void)m_core.fileDialogService().selectImageFiles(field, QStringLiteral("Open"), QString(), true);

    ASSERT_TRUE(seen.shown);
    EXPECT_EQ(seen.parent.data(), &window);
}

TEST_F(FileDialogServiceTest, ADialogOpensWhereItsContextWasLeftAndRemembersWhereThePersonWent)
{
    const QString kept = folderWithFile(QStringLiteral("kept"), QStringLiteral("a.txt"));
    const QString next = folderWithFile(QStringLiteral("next"), QStringLiteral("b.txt"));
    ASSERT_TRUE(m_core.folderStore()->upsert(QStringLiteral("flats"), kept));

    Seen seen;
    answerNextDialog(seen, next, QStringLiteral("b.txt"));
    const QStringList chosen = m_core.fileDialogService().selectFiles(nullptr, QStringLiteral("Flats"),
                                                                      QStringLiteral("flats"), QStringList());

    ASSERT_TRUE(seen.shown);
    EXPECT_EQ(canonical(seen.directory), canonical(kept));
    ASSERT_EQ(chosen.size(), 1);
    EXPECT_EQ(QFileInfo(chosen.front()).fileName(), QStringLiteral("b.txt"));
    EXPECT_EQ(canonical(m_core.folderStore()->select(QStringLiteral("flats"))), canonical(next));
}

TEST_F(FileDialogServiceTest, ADialogWithNoContextOpensInTheFolderOpenedLast)
{
    const QString last = folderWithFile(QStringLiteral("last"), QStringLiteral("c.txt"));
    m_core.setLastOpenDirectory(last);

    Seen seen;
    answerNextDialog(seen);
    (void)m_core.fileDialogService().selectFiles(nullptr, QStringLiteral("Choose"), QString(), QStringList());

    ASSERT_TRUE(seen.shown);
    EXPECT_EQ(canonical(seen.directory), canonical(last));
}

TEST_F(FileDialogServiceTest, ASavedFileRemembersItsFolderUnderItsContext)
{
    const QString target = folderWithFile(QStringLiteral("saved"), QStringLiteral("d.txt"));

    Seen seen;
    answerNextDialog(seen, target, QStringLiteral("e.txt"));
    const QStringList saved = m_core.fileDialogService().saveFiles(nullptr, QStringLiteral("Save"), QString(),
                                                                   QStringLiteral("exports"), QStringList());

    ASSERT_TRUE(seen.shown);
    ASSERT_EQ(saved.size(), 1);
    EXPECT_EQ(QFileInfo(saved.front()).fileName(), QStringLiteral("e.txt"));
    EXPECT_EQ(canonical(m_core.folderStore()->select(QStringLiteral("exports"))), canonical(target));
}

TEST_F(FileDialogServiceTest, TheSettingChoosesTheSystemsBrowserOrQts)
{
    const QString key = QString::fromLatin1(SettingsKeys::INTERFACE_USE_SYSTEM_FILE_BROWSER);

    ASSERT_TRUE(m_core.globalSettingsStore()->upsert(key, QStringLiteral("0")));
    Seen builtIn;
    answerNextDialog(builtIn);
    (void)m_core.fileDialogService().selectFiles(nullptr, QStringLiteral("Choose"), QString(), QStringList());
    ASSERT_TRUE(builtIn.shown);
    EXPECT_TRUE(builtIn.builtIn);

    ASSERT_TRUE(m_core.globalSettingsStore()->upsert(key, QStringLiteral("1")));
    Seen system;
    answerNextDialog(system);
    (void)m_core.fileDialogService().selectFiles(nullptr, QStringLiteral("Choose"), QString(), QStringList());
    ASSERT_TRUE(system.shown);
    EXPECT_FALSE(system.builtIn);
}

} // namespace
