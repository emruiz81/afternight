// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <gtest/gtest.h>
#include <QApplication>
#include <QShortcut>
#include <QKeySequence>
#include <QMenuBar>
#include <QMenu>

#include "processes/ImageIntegration/ImageIntegrationProcess.h"

#include "Test_ImageIntegration/ui/ProcessWindowContext.h"

namespace
{
QMenu* findMenu(QMenuBar* bar, const QString& title)
{
    if (!bar)
        return nullptr;
    for (QAction* action : bar->actions())
    {
        if (action && action->text() == title)
        {
            return action->menu();
        }
    }
    return nullptr;
}

QAction* findAction(QMenu* menu, const QString& text)
{
    if (!menu)
        return nullptr;
    for (QAction* action : menu->actions())
    {
        if (action && !action->isSeparator() && action->text() == text)
        {
            return action;
        }
    }
    return nullptr;
}

QAction* findActionWithShortcut(QObject* root, const QKeySequence& seq, const QString& text = QString())
{
    if (!root)
        return nullptr;
    const QList<QAction*> actions = root->findChildren<QAction*>();
    for (QAction* action : actions)
    {
        if (!action)
            continue;
        if (!text.isEmpty() && action->text() != text)
            continue;
        if (action->shortcut() == seq)
        {
            return action;
        }
    }
    return nullptr;
}

void expectMenuHasTooltipsRecursive(QMenu* menu)
{
    ASSERT_NE(menu, nullptr);
    for (QAction* action : menu->actions())
    {
        if (!action || action->isSeparator())
        {
            continue;
        }

        EXPECT_FALSE(action->toolTip().trimmed().isEmpty()) << action->text().toStdString();

        if (QMenu* sub = action->menu())
        {
            expectMenuHasTooltipsRecursive(sub);
        }
    }
}
} // namespace

// The window registers itself and keeps its settings in an application context of the case's own.
class KeyboardActionsTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        ASSERT_TRUE(m_scope.context().valid());
        process = new ImageIntegrationProcess(nullptr);
    }

    void TearDown() override
    {
        delete process;
        process = nullptr;
    }

    an::tests::integration::ScopedProcessWindowContext m_scope;
    ImageIntegrationProcess* process = nullptr;
};

TEST_F(KeyboardActionsTest, MenuBarHasExpectedTopLevelMenus)
{
    auto* bar = process->findChild<QMenuBar*>(QStringLiteral("mainMenuBar"));
    ASSERT_NE(bar, nullptr);

    EXPECT_NE(findMenu(bar, QStringLiteral("Frames")), nullptr);
    EXPECT_NE(findMenu(bar, QStringLiteral("Project")), nullptr);
    EXPECT_NE(findMenu(bar, QStringLiteral("Edit")), nullptr);
    EXPECT_NE(findMenu(bar, QStringLiteral("Run")), nullptr);
    EXPECT_NE(findMenu(bar, QStringLiteral("Navigate")), nullptr);
    EXPECT_NE(findMenu(bar, QStringLiteral("Help")), nullptr);
}

TEST_F(KeyboardActionsTest, AllMenusHaveTooltipsOnAllEntries)
{
    auto* bar = process->findChild<QMenuBar*>(QStringLiteral("mainMenuBar"));
    ASSERT_NE(bar, nullptr);

    for (QAction* action : bar->actions())
    {
        if (!action)
            continue;
        if (QMenu* menu = action->menu())
        {
            expectMenuHasTooltipsRecursive(menu);
        }
    }
}

TEST_F(KeyboardActionsTest, FramesMenuHasExpectedActionsAndTooltips)
{
    auto* bar = process->findChild<QMenuBar*>(QStringLiteral("mainMenuBar"));
    ASSERT_NE(bar, nullptr);

    QMenu* frames = findMenu(bar, QStringLiteral("Frames"));
    ASSERT_NE(frames, nullptr);

    auto* addFiles = findAction(frames, QStringLiteral("Add Files..."));
    ASSERT_NE(addFiles, nullptr);
    QMenu* addFilesMenu = addFiles->menu();
    ASSERT_NE(addFilesMenu, nullptr);

    auto* addFolderThis = findAction(frames, QStringLiteral("Add Folder... (This Folder Only)"));
    ASSERT_NE(addFolderThis, nullptr);
    QMenu* addFolderThisMenu = addFolderThis->menu();
    ASSERT_NE(addFolderThisMenu, nullptr);

    auto* addFolderRec = findAction(frames, QStringLiteral("Add Folder... (Recursive)"));
    ASSERT_NE(addFolderRec, nullptr);
    QMenu* addFolderRecMenu = addFolderRec->menu();
    ASSERT_NE(addFolderRecMenu, nullptr);

    EXPECT_NE(findAction(frames, QStringLiteral("Remove Selected")), nullptr);
    EXPECT_NE(findAction(frames, QStringLiteral("Remove All Frames...")), nullptr);
    EXPECT_NE(findAction(frames, QStringLiteral("Go to Group...")), nullptr);

    // Add Files submenu entries
    EXPECT_NE(findAction(addFilesMenu, QStringLiteral("Add Bias...")), nullptr);
    EXPECT_NE(findAction(addFilesMenu, QStringLiteral("Add Darks...")), nullptr);
    EXPECT_NE(findAction(addFilesMenu, QStringLiteral("Add Flats...")), nullptr);
    EXPECT_NE(findAction(addFilesMenu, QStringLiteral("Add Lights...")), nullptr);
    EXPECT_NE(findAction(addFilesMenu, QStringLiteral("Auto-sort by file type...")), nullptr);

    // Add Folder (This Folder Only) submenu entries
    EXPECT_NE(findAction(addFolderThisMenu, QStringLiteral("Add Bias...")), nullptr);
    EXPECT_NE(findAction(addFolderThisMenu, QStringLiteral("Add Darks...")), nullptr);
    EXPECT_NE(findAction(addFolderThisMenu, QStringLiteral("Add Flats...")), nullptr);
    EXPECT_NE(findAction(addFolderThisMenu, QStringLiteral("Add Lights...")), nullptr);
    EXPECT_NE(findAction(addFolderThisMenu, QStringLiteral("Auto-sort by file type...")), nullptr);

    // Add Folder (Recursive) submenu entries
    EXPECT_NE(findAction(addFolderRecMenu, QStringLiteral("Add Bias...")), nullptr);
    EXPECT_NE(findAction(addFolderRecMenu, QStringLiteral("Add Darks...")), nullptr);
    EXPECT_NE(findAction(addFolderRecMenu, QStringLiteral("Add Flats...")), nullptr);
    EXPECT_NE(findAction(addFolderRecMenu, QStringLiteral("Add Lights...")), nullptr);
    EXPECT_NE(findAction(addFolderRecMenu, QStringLiteral("Auto-sort by file type...")), nullptr);

    // Tooltips should be present for every menu entry (including submenus and their actions).
    expectMenuHasTooltipsRecursive(frames);
}

TEST_F(KeyboardActionsTest, ProjectMenuHasExpectedActionsAndTooltips)
{
    auto* bar = process->findChild<QMenuBar*>(QStringLiteral("mainMenuBar"));
    ASSERT_NE(bar, nullptr);

    QMenu* project = findMenu(bar, QStringLiteral("Project"));
    ASSERT_NE(project, nullptr);

    EXPECT_NE(findAction(project, QStringLiteral("Import Project...")), nullptr);
    EXPECT_NE(findAction(project, QStringLiteral("Export Project...")), nullptr);

    auto* presets = findAction(project, QStringLiteral("Presets"));
    ASSERT_NE(presets, nullptr);
    QMenu* presetsMenu = presets->menu();
    ASSERT_NE(presetsMenu, nullptr);

    EXPECT_NE(findAction(presetsMenu, QStringLiteral("Save Preset...")), nullptr);

    auto* loadPreset = findAction(presetsMenu, QStringLiteral("Load Preset..."));
    ASSERT_NE(loadPreset, nullptr);
    ASSERT_NE(loadPreset->menu(), nullptr);

    auto* deletePreset = findAction(presetsMenu, QStringLiteral("Delete Preset..."));
    ASSERT_NE(deletePreset, nullptr);
    ASSERT_NE(deletePreset->menu(), nullptr);

    EXPECT_NE(findAction(project, QStringLiteral("Reset Settings...")), nullptr);

    expectMenuHasTooltipsRecursive(project);
}

TEST_F(KeyboardActionsTest, ShortcutsAreConfiguredOnMenuActions)
{
    auto* bar = process->findChild<QMenuBar*>(QStringLiteral("mainMenuBar"));
    ASSERT_NE(bar, nullptr);

    QMenu* frames = findMenu(bar, QStringLiteral("Frames"));
    ASSERT_NE(frames, nullptr);

    auto* addFilesAction = findAction(frames, QStringLiteral("Add Files..."));
    ASSERT_NE(addFilesAction, nullptr);
    QMenu* addFilesMenu = addFilesAction->menu();
    ASSERT_NE(addFilesMenu, nullptr);
    auto* addFilesAutoSort = findAction(addFilesMenu, QStringLiteral("Auto-sort by file type..."));
    ASSERT_NE(addFilesAutoSort, nullptr);
    EXPECT_EQ(addFilesAutoSort->shortcut(), QKeySequence(QStringLiteral("Ctrl+O")));

    auto* addFolderRecAction = findAction(frames, QStringLiteral("Add Folder... (Recursive)"));
    ASSERT_NE(addFolderRecAction, nullptr);
    QMenu* addFolderRecMenu = addFolderRecAction->menu();
    ASSERT_NE(addFolderRecMenu, nullptr);
    auto* addFolderRecAutoSort = findAction(addFolderRecMenu, QStringLiteral("Auto-sort by file type..."));
    ASSERT_NE(addFolderRecAutoSort, nullptr);
    EXPECT_EQ(addFolderRecAutoSort->shortcut(), QKeySequence(QStringLiteral("Ctrl+Shift+O")));

    EXPECT_NE(findActionWithShortcut(process, QKeySequence::Delete, QStringLiteral("Remove Selected")), nullptr);
    EXPECT_NE(findActionWithShortcut(process, QKeySequence(QStringLiteral("Ctrl+Shift+Delete")),
                                     QStringLiteral("Remove All Frames...")),
              nullptr);
    EXPECT_NE(findActionWithShortcut(process, QKeySequence(QStringLiteral("Ctrl+G")), QStringLiteral("Go to Group...")),
              nullptr);

    EXPECT_NE(
        findActionWithShortcut(process, QKeySequence(QStringLiteral("Ctrl+I")), QStringLiteral("Import Project...")),
        nullptr);
    EXPECT_NE(
        findActionWithShortcut(process, QKeySequence(QStringLiteral("Ctrl+E")), QStringLiteral("Export Project...")),
        nullptr);
    EXPECT_NE(findActionWithShortcut(process, QKeySequence(QStringLiteral("Ctrl+Enter")),
                                     QStringLiteral("Execute Integration")),
              nullptr);

    // HelpContents is typically F1.
    EXPECT_NE(findActionWithShortcut(process, QKeySequence::HelpContents, QStringLiteral("Keyboard Shortcuts")),
              nullptr);
}

TEST_F(KeyboardActionsTest, NavigateMenuRegistersCtrl1ThroughCtrl8)
{
    for (int i = 1; i <= 8; ++i)
    {
        const QKeySequence seq(QStringLiteral("Ctrl+%1").arg(i));
        EXPECT_NE(findActionWithShortcut(process, seq), nullptr) << seq.toString().toStdString();
    }
}

TEST_F(KeyboardActionsTest, RefreshShortcutIsRegisteredAsQShortcut)
{
    const QList<QShortcut*> shortcuts = process->findChildren<QShortcut*>();
    bool foundF5 = false;
    for (QShortcut* s : shortcuts)
    {
        if (s && s->key() == QKeySequence(Qt::Key_F5))
        {
            foundF5 = true;
            break;
        }
    }
    EXPECT_TRUE(foundF5);
}
