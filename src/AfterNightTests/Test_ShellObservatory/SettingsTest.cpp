// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The Settings dialog in the Observatory shell: its pages in this shell's order, Projects after General; the Interface
/// page's choices acting on the window at once - an accent repaints it, "Always show the menu bar" shows its bar - kept
/// by OK and put back by Cancel; and the Shell row offering every shell the application registered.

#include "Test_ShellObservatory/ObservatoryWindowCase.h"

#include "chrome/MenuBarOnAlt.h"

#include "actions/ActionCatalogue.h"
#include "actions/ActionIds.h"
#include "controls/SettingsDialog.h"
#include "controls/settings/SettingsDefaults.h"
#include "controls/settings/SettingsKeys.h"
#include "controls/settings/SettingsPage.h"
#include "core/ShellRegistry.h"
#include "theme/AnDesignTokens.h"

#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QColor>
#include <QComboBox>
#include <QCoreApplication>
#include <QGroupBox>
#include <QMessageBox>
#include <QPalette>
#include <QPushButton>
#include <QTimer>

#include <gtest/gtest.h>

#include <functional>
#include <memory>
#include <optional>

namespace an::tests::observatory
{

namespace
{

using Settings = ObservatoryWindowCase;

constexpr int kWaitMs = 10000;

/// Lets the window settle: posted work delivered, the deletions done, the layouts applied.
void settle()
{
    for (int round = 0; round < 3; ++round)
    {
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QCoreApplication::processEvents();
    }
}

/// Opens the Settings as the catalogue's Settings action does, and acts on the dialog from inside the loop it runs; the
/// act closes it. Whether it was shown.
bool inTheSettings(an::ui::shell::ApplicationCore& core, const std::function<void(SettingsDialog&)>& act)
{
    bool shown = false;
    const std::shared_ptr<an::tests::app::WindowPoll> poll =
        an::tests::app::answerWhenShown([&shown, &act](QWidget* widget) {
            auto* const dialog = qobject_cast<SettingsDialog*>(widget);
            if (dialog == nullptr)
            {
                return false;
            }
            shown = true;
            act(*dialog);
            if (dialog->isVisible())
            {
                dialog->reject();
            }
            return true;
        });
    QAction* const settings = core.actions().action(an::ui::shell::ids::kSettings);
    EXPECT_NE(settings, nullptr);
    if (settings != nullptr)
    {
        settings->trigger();
    }
    const bool answered = an::tests::app::processEventsUntil([&shown]() { return shown; }, kWaitMs);
    poll->ended = true;
    settle();
    return answered;
}

/// Cancels a dialog whose page has unsaved changes: its question is answered with Discard.
void cancelDiscarding(SettingsDialog& dialog)
{
    auto* const poll = new QTimer(qApp);
    poll->setInterval(10);
    QObject::connect(poll, &QTimer::timeout, poll, [poll]() {
        if (auto* const box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget()))
        {
            if (QAbstractButton* const discard = box->button(QMessageBox::Discard))
            {
                discard->click();
            }
            poll->deleteLater();
        }
    });
    poll->start();
    dialog.reject();
}

/// Presses the dialog's OK.
void pressOk(SettingsDialog& dialog)
{
    auto* const ok = dialog.findChild<QPushButton*>(QStringLiteral("settingsOkButton"));
    EXPECT_NE(ok, nullptr);
    if (ok != nullptr)
    {
        ok->click();
    }
}

/// A control of the dialog's Interface page, shown.
template <typename T> T* onTheInterfacePage(SettingsDialog& dialog, const char* name)
{
    dialog.setCurrentPage(QStringLiteral("Interface"));
    settle();
    return dialog.findChild<T*>(QString::fromLatin1(name));
}

} // namespace

// This shell's Settings list General, Projects, Interface, Processing, Raw Images, Plate Solving, Catalogs & Data,
// Export, External Tools and Logging, and its Interface page offers "Always show the menu bar", since this shell shows
// its menu bar on Alt.
TEST_F(Settings, ListThePagesInTheShellsOrder)
{
    QStringList pages;
    bool menuBarRow = false;
    ASSERT_TRUE(inTheSettings(core(), [&pages, &menuBarRow](SettingsDialog& dialog) {
        for (int index = 0; index < dialog.pageCount(); ++index)
        {
            pages.append(dialog.page(index)->pageTitle());
        }
        if (auto* const row = onTheInterfacePage<QAbstractButton>(dialog, "anInterfaceSettingsAlwaysShowMenuBar"))
        {
            menuBarRow = row->isVisibleTo(&dialog);
        }
    })) << "Settings opened no dialog";
    EXPECT_EQ(pages,
              (QStringList{QStringLiteral("General"), QStringLiteral("Projects"), QStringLiteral("Interface"),
                           QStringLiteral("Processing"), QStringLiteral("Raw Images"), QStringLiteral("Plate Solving"),
                           QStringLiteral("Catalogs & Data"), QStringLiteral("Export"),
                           QStringLiteral("External Tools"), QStringLiteral("Logging")}));
    EXPECT_TRUE(menuBarRow) << "the Interface page does not offer to keep the menu bar shown";
}

// "Always show the menu bar" shows the window's menu bar as it is switched on, and OK keeps it so; switched off, the
// bar hides at once, and Cancel puts it back as it was kept.
TEST_F(Settings, TheMenuBarRowShowsTheBarAtOnceAndOkKeepsIt)
{
    an::ui::observatory::MenuBarOnAlt& bar = window().menuBarOnAlt();
    ASSERT_FALSE(bar.isAlwaysShown());
    ASSERT_FALSE(bar.isShown());
    an::ui::IGlobalSettingsProvider* const settings = core().globalSettings();
    const QString key = QString::fromLatin1(SettingsKeys::INTERFACE_ALWAYS_SHOW_MENU_BAR);

    bool shownAtOnce = false;
    ASSERT_TRUE(inTheSettings(core(), [&](SettingsDialog& dialog) {
        auto* const row = onTheInterfacePage<QAbstractButton>(dialog, "anInterfaceSettingsAlwaysShowMenuBar");
        ASSERT_NE(row, nullptr);
        row->click();
        settle();
        shownAtOnce = bar.isShown();
        pressOk(dialog);
    }));
    EXPECT_TRUE(shownAtOnce) << "the bar did not show as the row was switched on";
    EXPECT_TRUE(bar.isAlwaysShown());
    EXPECT_EQ(settings->select(key, QStringLiteral("0")), QStringLiteral("1")) << "OK did not keep the choice";

    bool hiddenAtOnce = false;
    ASSERT_TRUE(inTheSettings(core(), [&](SettingsDialog& dialog) {
        auto* const row = onTheInterfacePage<QAbstractButton>(dialog, "anInterfaceSettingsAlwaysShowMenuBar");
        ASSERT_NE(row, nullptr);
        EXPECT_TRUE(row->isChecked());
        row->click();
        settle();
        hiddenAtOnce = !bar.isShown();
        cancelDiscarding(dialog);
    }));
    EXPECT_TRUE(hiddenAtOnce) << "the bar did not hide as the row was switched off";
    EXPECT_TRUE(bar.isAlwaysShown()) << "Cancel did not put the bar back as it was kept";
    EXPECT_TRUE(bar.isShown());
    EXPECT_EQ(settings->select(key, QStringLiteral("0")), QStringLiteral("1"));

    bar.setAlwaysShown(false);
    (void)settings->upsert(key, QStringLiteral("0"));
    settle();
}

// An accent chosen on the Interface page repaints the window at once; Cancel puts back the accent that was in force and
// the window's colours with it.
TEST_F(Settings, AnAccentChosenRepaintsTheWindowAtOnce)
{
    const an::ui::ThemeAccentFamily before = an::ui::themeAccentFamily();
    const QColor highlight = window().QWidget::palette().color(QPalette::Highlight);
    const char* const other =
        before == an::ui::ThemeAccentFamily::Rose ? "anInterfaceSettingsAccent_teal" : "anInterfaceSettingsAccent_rose";

    QColor during;
    ASSERT_TRUE(inTheSettings(core(), [&](SettingsDialog& dialog) {
        auto* const disc = onTheInterfacePage<QAbstractButton>(dialog, other);
        ASSERT_NE(disc, nullptr);
        disc->click();
        settle();
        during = window().QWidget::palette().color(QPalette::Highlight);
        cancelDiscarding(dialog);
    }));
    EXPECT_NE(during, highlight) << "the window was not repainted in the accent chosen";
    EXPECT_EQ(an::ui::themeAccentFamily(), before) << "Cancel left the accent chosen in force";
    EXPECT_EQ(window().QWidget::palette().color(QPalette::Highlight), highlight);
}

// The Shell row lists every shell the application registered, by its name; the one chosen is the shell the next start
// makes, kept by OK.
TEST_F(Settings, TheShellRowOffersEveryShellRegistered)
{
    an::ui::shell::ShellRegistry registry;
    const auto made = [](an::ui::shell::ApplicationCore&) -> std::unique_ptr<an::ui::shell::IShell> { return nullptr; };
    ASSERT_TRUE(
        registry.add({.id = QStringLiteral("nebula"), .displayName = QStringLiteral("Nebula"), .create = made}));
    ASSERT_TRUE(registry.add(
        {.id = QStringLiteral("observatory"), .displayName = QStringLiteral("Observatory"), .create = made}));
    core().setShellRegistry(&registry);
    an::ui::IGlobalSettingsProvider* const settings = core().globalSettings();
    const QString key = QString::fromLatin1(SettingsKeys::INTERFACE_SHELL);
    const QString previous = settings->select(key, QString::fromLatin1(SettingsDefaults::INTERFACE_SHELL));
    (void)settings->upsert(key, QStringLiteral("nebula"));

    QStringList offered;
    QString shownChoice;
    ASSERT_TRUE(inTheSettings(core(), [&](SettingsDialog& dialog) {
        auto* const combo = onTheInterfacePage<QComboBox>(dialog, "anInterfaceSettingsShellCombo");
        auto* const group = dialog.findChild<QGroupBox*>(QStringLiteral("anInterfaceSettingsShellGroup"));
        ASSERT_NE(combo, nullptr);
        ASSERT_NE(group, nullptr);
        EXPECT_TRUE(group->isVisibleTo(&dialog));
        for (int index = 0; index < combo->count(); ++index)
        {
            offered.append(combo->itemText(index));
        }
        shownChoice = combo->currentData().toString();
        combo->setCurrentIndex(combo->findData(QStringLiteral("observatory")));
        pressOk(dialog);
    }));
    core().setShellRegistry(nullptr);
    EXPECT_EQ(offered, (QStringList{QStringLiteral("Nebula"), QStringLiteral("Observatory")}));
    EXPECT_EQ(shownChoice, QStringLiteral("nebula")) << "the row does not show the shell stored";
    EXPECT_EQ(settings->select(key, QString()), QStringLiteral("observatory")) << "OK did not keep the shell chosen";
    (void)settings->upsert(key, previous);
}

} // namespace an::tests::observatory
