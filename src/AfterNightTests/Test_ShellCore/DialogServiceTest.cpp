// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The messages the application shows: each over the window of the widget that asked, or over the shell's window when
/// none did; a question answers what was chosen; one asked from a worker is shown on the GUI thread; and the settings
/// are opened where the shell says.

#include "ShellCoreViewSupport.h"

#include "core/ApplicationCore.h"
#include "core/ApplicationEvents.h"
#include "services/DialogService.h"

#include <gtest/gtest.h>

#include <QAbstractButton>
#include <QMessageBox>
#include <QPointer>
#include <QString>
#include <QWidget>

#include <atomic>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace
{

using an::tests::shellcore::answerWhenShown;
using an::tests::shellcore::pumpUntil;
using an::tests::shellcore::RecordingShell;
using an::ui::shell::ApplicationCore;
using an::ui::shell::DialogService;

/// What a case saw of the message box it answered.
struct Seen
{
    QPointer<QWidget> parent;
    bool shown = false;
    QString title;
    QString text;
    QMessageBox::Icon icon = QMessageBox::NoIcon;
    std::thread::id thread;
};

/// Records the next message box and closes it by `button`.
void answerNextBox(Seen& seen, const QMessageBox::StandardButton button = QMessageBox::Ok)
{
    answerWhenShown<QMessageBox>([&seen, button](QMessageBox& box) {
        seen.shown = true;
        seen.parent = box.parentWidget();
        seen.title = box.windowTitle();
        seen.text = box.text();
        seen.icon = box.icon();
        seen.thread = std::this_thread::get_id();
        if (QAbstractButton* chosen = box.button(button))
        {
            chosen->click();
            return;
        }
        box.reject();
    });
}

class DialogServiceTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        m_core.setShell(&m_shell);
    }

    void TearDown() override
    {
        m_core.setShell(nullptr);
    }

    RecordingShell m_shell;
    ApplicationCore m_core;
};

TEST_F(DialogServiceTest, AMessageNoWidgetAskedForIsShownOverTheShellsWindow)
{
    Seen seen;
    answerNextBox(seen);
    m_core.dialogService().notifyErrorMessage(nullptr, QStringLiteral("Open"), QStringLiteral("the file is gone"));

    ASSERT_TRUE(seen.shown);
    EXPECT_EQ(seen.parent.data(), m_shell.mainWindow());
    EXPECT_EQ(seen.title, QStringLiteral("Open"));
    EXPECT_EQ(seen.text, QStringLiteral("the file is gone"));
    EXPECT_EQ(seen.icon, QMessageBox::Critical);
}

TEST_F(DialogServiceTest, AMessageAWidgetAskedForIsShownOverThatWidgetsWindow)
{
    QWidget window;
    auto* field = new QWidget(&window);

    Seen warning;
    answerNextBox(warning);
    m_core.dialogService().notifyWarnMessage(field, QStringLiteral("Save"), QStringLiteral("nothing to save"));
    ASSERT_TRUE(warning.shown);
    EXPECT_EQ(warning.parent.data(), &window);
    EXPECT_EQ(warning.icon, QMessageBox::Warning);

    Seen information;
    answerNextBox(information);
    m_core.dialogService().notifyInfoMessage(&window, QStringLiteral("Settings"), QStringLiteral("moved aside"));
    ASSERT_TRUE(information.shown);
    EXPECT_EQ(information.parent.data(), &window);
    EXPECT_EQ(information.icon, QMessageBox::Information);
}

TEST_F(DialogServiceTest, WithNoShellAMessageBelongsToNoWindow)
{
    m_core.setShell(nullptr);
    Seen seen;
    answerNextBox(seen);
    m_core.dialogService().notifyErrorMessage(nullptr, QStringLiteral("Start"), QStringLiteral("no window yet"));

    ASSERT_TRUE(seen.shown);
    EXPECT_EQ(seen.parent.data(), nullptr);
}

TEST_F(DialogServiceTest, AQuestionAnswersWhatWasChosen)
{
    int answer = -1;
    Seen yes;
    answerNextBox(yes, QMessageBox::Yes);
    m_core.dialogService().messageBoxYesNo(nullptr, QStringLiteral("Close"), QStringLiteral("discard the edits?"), true,
                                           &answer);
    ASSERT_TRUE(yes.shown);
    EXPECT_EQ(answer, QMessageBox::Yes);
    EXPECT_EQ(yes.icon, QMessageBox::Warning);
    EXPECT_EQ(yes.parent.data(), m_shell.mainWindow());

    Seen no;
    answerNextBox(no, QMessageBox::No);
    m_core.dialogService().messageBoxYesNo(nullptr, QStringLiteral("Open"), QStringLiteral("demosaic it?"), false,
                                           &answer);
    ASSERT_TRUE(no.shown);
    EXPECT_EQ(answer, QMessageBox::No);
    EXPECT_EQ(no.icon, QMessageBox::Question);
}

TEST_F(DialogServiceTest, AMessageAskedFromAWorkerIsShownOnTheGuiThread)
{
    an::ui::shell::ServiceEventTarget target(m_core, {});
    m_core.setEventTarget(&target);

    Seen seen;
    answerNextBox(seen);
    std::thread worker([this]() {
        m_core.dialogService().notifyInfoMessageAsync(nullptr, QStringLiteral("Run"), QStringLiteral("done"));
    });
    worker.join();
    EXPECT_FALSE(seen.shown) << "the worker waited for the message to be answered";

    EXPECT_TRUE(pumpUntil([&seen]() { return seen.shown; }));
    EXPECT_EQ(seen.thread, std::this_thread::get_id());
    EXPECT_EQ(seen.parent.data(), m_shell.mainWindow());
    m_core.setEventTarget(nullptr);
}

TEST_F(DialogServiceTest, TheSettingsAndTheExtensionsOpenWhereTheShellSays)
{
    std::vector<std::string> opened;
    DialogService::Hooks hooks;
    hooks.openSettingsAt = [&opened](const QString& page) { opened.push_back("settings " + page.toStdString()); };
    hooks.openExtensionsAt = [&opened](const QString& page) { opened.push_back("extensions " + page.toStdString()); };
    m_core.dialogService().setHooks(std::move(hooks));

    m_core.dialogService().openSettingsAt(QStringLiteral("External Tools"));
    m_core.dialogService().openExtensionsAt(QStringLiteral("Installed"));

    EXPECT_EQ(opened, (std::vector<std::string>{"settings External Tools", "extensions Installed"}));

    // With nothing to open them, asking opens nothing and fails nothing.
    m_core.dialogService().setHooks({});
    m_core.dialogService().openSettingsAt(QStringLiteral("General"));
    EXPECT_EQ(opened.size(), 2u);
}

} // namespace
