// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// The Observatory window's chrome: the menu bar that shows on Alt - hidden at the start, shown and focused by Alt, a
// menu opened by Alt and its letter, hidden again once left, kept by its setting - and the status line, which writes
// what the classic status bar writes for the same view.

#include "ObservatoryWindowCase.h"

#include "chrome/MenuBarOnAlt.h"
#include "chrome/StatusLine.h"

#include "actions/ActionCatalogue.h"
#include "actions/MenuTree.h"
#include "controls/IAppContext.h"
#include "controls/ImageViewControl.h"
#include "controls/settings/SettingsKeys.h"
#include "core/ApplicationCore.h"
#include "widgets/ImageInfoStatusBarWidget.h"

#include <QAction>
#include <QApplication>
#include <QLabel>
#include <QMainWindow>
#include <QMenu>
#include <QMenuBar>
#include <QStackedWidget>
#include <QStatusBar>
#include <QtTest/QTest>

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

namespace an::tests::observatory
{
namespace
{

using MenuBarOnAlt = ObservatoryWindowCase;
using StatusLine = ObservatoryWindowCase;

/// Presses and releases Alt alone on a widget, as a person taps it.
void tapAlt(QWidget& widget)
{
    QTest::keyPress(&widget, Qt::Key_Alt, Qt::AltModifier);
    QTest::keyRelease(&widget, Qt::Key_Alt, Qt::NoModifier);
    QCoreApplication::processEvents();
}

/// Closes every menu open.
void closeMenus()
{
    while (QWidget* const popup = QApplication::activePopupWidget())
    {
        popup->close();
        QCoreApplication::processEvents();
    }
}

// The bar is hidden at the start, and holds the menu tree's menus in their order: File, Edit, View, Image, Process,
// Extensions, Window, Help and About.
TEST_F(MenuBarOnAlt, IsHiddenAtTheStartAndHoldsTheMenuTree)
{
    an::ui::observatory::MenuBarOnAlt& menus = window().menuBarOnAlt();
    ASSERT_NE(menus.bar(), nullptr);
    EXPECT_EQ(window().menuBar(), menus.bar());
    EXPECT_FALSE(menus.isShown());
    EXPECT_FALSE(menus.isAlwaysShown());

    std::vector<std::string> shown;
    for (QAction* top : menus.bar()->actions())
    {
        if (top->menu() != nullptr)
        {
            shown.push_back(top->menu()->objectName().toStdString());
        }
    }
    std::vector<std::string> tree;
    for (const std::string_view id : an::ui::shell::MenuTree::menuBar().topLevel())
    {
        tree.emplace_back(id);
    }
    EXPECT_EQ(shown, tree);
    EXPECT_NE(menus.menu("menuExtensions"), nullptr);
}

// Alt pressed and released alone shows the bar and gives it the focus; Alt again hides it and gives the focus back.
TEST_F(MenuBarOnAlt, AltShowsAndFocusesIt)
{
    an::ui::observatory::MenuBarOnAlt& menus = window().menuBarOnAlt();
    window().centre()->setFocus();
    QWidget* const before = QApplication::focusWidget();
    tapAlt(*window().centre());
    EXPECT_TRUE(menus.isShown());
    EXPECT_EQ(QApplication::focusWidget(), menus.bar());

    tapAlt(*menus.bar());
    EXPECT_FALSE(menus.isShown());
    EXPECT_EQ(QApplication::focusWidget(), before);
}

// Alt and a menu's letter opens that menu at once, the bar shown with it.
TEST_F(MenuBarOnAlt, AltAndALetterOpensItsMenu)
{
    an::ui::observatory::MenuBarOnAlt& menus = window().menuBarOnAlt();
    QMenu* const file = menus.menu("menuFile");
    ASSERT_NE(file, nullptr);
    QTest::keyClick(window().centre(), Qt::Key_F, Qt::AltModifier);
    EXPECT_TRUE(an::tests::app::processEventsUntil([file]() { return file->isVisible(); }, 3000))
        << "Alt+F did not open the File menu";
    EXPECT_TRUE(menus.isShown());
    closeMenus();
    window().centre()->setFocus();
    EXPECT_TRUE(an::tests::app::processEventsUntil([&menus]() { return !menus.isShown(); }, 3000));
}

// Shown by Alt, the bar hides once its menus are closed and the focus is elsewhere.
TEST_F(MenuBarOnAlt, HidesOnceLeft)
{
    an::ui::observatory::MenuBarOnAlt& menus = window().menuBarOnAlt();
    tapAlt(*window().centre());
    ASSERT_TRUE(menus.isShown());
    window().centre()->setFocus();
    EXPECT_TRUE(an::tests::app::processEventsUntil([&menus]() { return !menus.isShown(); }, 3000));
}

/// The menu bar with its setting stored, each case over a start of its own.
class MenuBarKept : public ::testing::Test
{
};

// With the setting kept, the bar is shown from the start, and Alt neither hides it nor leaves it.
TEST_F(MenuBarKept, TheSettingKeepsItShown)
{
    std::unique_ptr<RunningObservatory> running = startObservatory([](an::ui::shell::ApplicationCore& core) {
        (void)core.globalSettings()->upsert(QString::fromLatin1(SettingsKeys::INTERFACE_ALWAYS_SHOW_MENU_BAR),
                                            QStringLiteral("1"));
    });
    ASSERT_NE(running, nullptr);
    an::ui::observatory::MenuBarOnAlt& menus = running->shell->window()->menuBarOnAlt();
    EXPECT_TRUE(menus.isAlwaysShown());
    EXPECT_TRUE(menus.isShown());
    tapAlt(*running->shell->window()->centre());
    EXPECT_TRUE(menus.isShown());
    tapAlt(*menus.bar());
    EXPECT_TRUE(menus.isShown());
    (void)running->core->globalSettings()->upsert(QString::fromLatin1(SettingsKeys::INTERFACE_ALWAYS_SHOW_MENU_BAR),
                                                  QStringLiteral("0"));
    running->shell->window()->close();
    const an::Status shut = stopObservatory(std::move(running));
    EXPECT_TRUE(shut.ok()) << shut.message();
}

/// The classic status bar's texts for the same view, as it shows them.
struct ClassicTexts
{
    QString facts;
    QString zoom;
    QString rightAscension;
    QString declination;
    QString rightAscensionTip;
    QString declinationTip;
};

[[nodiscard]] ClassicTexts classicTexts(ImageViewControl* view, double ra, double dec)
{
    QMainWindow host;
    ImageInfoStatusBarWidget classic;
    classic.initialize(host.statusBar());
    classic.updateImageInfo(view);
    classic.updateCelestialCoords(ra, dec);
    ClassicTexts texts;
    const QList<QLabel*> labels = host.statusBar()->findChildren<QLabel*>();
    for (QLabel* label : labels)
    {
        if (label->objectName() == QLatin1String("imageStatusRaLabel"))
        {
            texts.rightAscension = label->text();
            texts.rightAscensionTip = label->toolTip();
        }
        else if (label->objectName() == QLatin1String("imageStatusDecLabel"))
        {
            texts.declination = label->text();
            texts.declinationTip = label->toolTip();
        }
        else if (texts.facts.isEmpty() && !label->text().contains(QLatin1Char('%')))
        {
            texts.facts = label->text();
        }
        else if (label->text().contains(QLatin1Char('%')))
        {
            texts.zoom = label->text();
        }
    }
    return texts;
}

// For the same view the status line writes what the classic status bar writes: the image's facts, its zoom and the
// position under the cursor, with the same tooltips.
TEST_F(StatusLine, TheTextsEqualTheClassicStatusBars)
{
    ImageViewControl* const view = openView(QStringLiteral("status-line-texts"));
    ASSERT_NE(view, nullptr);
    constexpr double kRa = 83.8221;
    constexpr double kDec = -5.3911;
    an::ui::IStatusBarService* const status = an::ui::appContext()->getStatusBarService();
    ASSERT_NE(status, nullptr);
    status->updateImageInfoInStatusBar();
    status->updateImageCelestialCoordsInStatusBar(kRa, kDec);

    const ClassicTexts expected = classicTexts(view, kRa, kDec);
    an::ui::observatory::StatusLine& line = *window().statusLine();
    ASSERT_FALSE(expected.facts.isEmpty());
    EXPECT_EQ(line.factsText()->text(), expected.facts);
    EXPECT_EQ(line.zoomText()->text(), expected.zoom);
    EXPECT_EQ(line.rightAscensionText()->text(), expected.rightAscension);
    EXPECT_EQ(line.declinationText()->text(), expected.declination);
    EXPECT_EQ(line.rightAscensionText()->toolTip(), expected.rightAscensionTip);
    EXPECT_EQ(line.declinationText()->toolTip(), expected.declinationTip);
    EXPECT_EQ(line.rightAscensionText()->tone(), an::ui::observatory::StatusText::Tone::Secondary);
}

// With no position under the cursor the line says so in the tertiary colour, and with no image it says nothing of one.
TEST_F(StatusLine, ClearedItSaysThereIsNone)
{
    ImageViewControl* const view = openView(QStringLiteral("status-line-cleared"));
    ASSERT_NE(view, nullptr);
    an::ui::IStatusBarService* const status = an::ui::appContext()->getStatusBarService();
    status->updateImageInfoInStatusBar();
    status->updateImageCelestialCoordsInStatusBar(10.0, 20.0);
    status->clearImageCelestialCoordsInStatusBar();
    an::ui::observatory::StatusLine& line = *window().statusLine();
    EXPECT_EQ(line.rightAscensionText()->text(), QStringLiteral("RA -"));
    EXPECT_EQ(line.declinationText()->text(), QStringLiteral("Dec -"));
    EXPECT_EQ(line.rightAscensionText()->tone(), an::ui::observatory::StatusText::Tone::Tertiary);
    EXPECT_FALSE(line.factsText()->text().isEmpty());

    window().clearImageInfo();
    EXPECT_TRUE(line.factsText()->text().isEmpty());
    EXPECT_TRUE(line.zoomText()->text().isEmpty());
}

} // namespace
} // namespace an::tests::observatory
