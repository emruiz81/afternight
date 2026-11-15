// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// The Observatory window's places as a person moves between them: the readout beside an open image, the rail that
// selects a place, the shortcuts with the menu bar hidden, Home's actions, and the place the window keeps for the next
// start.

#include "ObservatoryWindowCase.h"

#include "chrome/MenuBarOnAlt.h"
#include "pages/HomePage.h"

#include "actions/ActionCatalogue.h"
#include "actions/ActionIds.h"
#include "components/AnIconRail.h"
#include "components/AnReadoutColumn.h"
#include "components/RailItem.h"
#include "controls/IAppContext.h"
#include "controls/ImageViewControl.h"
#include "controls/MainDashboard.h"
#include "controls/NewSequenceControl.h"
#include "controls/settings/SettingsKeys.h"
#include "core/ApplicationCore.h"

#include <QApplication>
#include <QFileDialog>
#include <QMenuBar>
#include <QStackedWidget>
#include <QtTest/QTest>

#include <gtest/gtest.h>

#include <algorithm>
#include <memory>

namespace an::tests::observatory
{
namespace
{

using ObservatoryWindow = ObservatoryWindowCase;

/// The item the rail shows under an id; a default item when it shows none.
[[nodiscard]] an::ui::RailItem railItem(const an::ui::AnIconRail& rail, const QString& id)
{
    const std::vector<an::ui::RailItem>& items = rail.items();
    const auto found =
        std::find_if(items.begin(), items.end(), [&id](const an::ui::RailItem& item) { return item.id == id; });
    return found != items.end() ? *found : an::ui::RailItem{};
}

/// Clicks a rail item where a person clicks it.
void clickRail(an::ui::AnIconRail& rail, const char* id)
{
    const QRect place = rail.itemRect(QString::fromLatin1(id));
    ASSERT_TRUE(place.isValid()) << "the rail shows no item " << id;
    QTest::mouseClick(&rail, Qt::LeftButton, Qt::NoModifier, place.center());
    QCoreApplication::processEvents();
}

// An image opened shows the workspace and, beside it, the readout of that image: the column names the view.
TEST_F(ObservatoryWindow, OpenImageShowsTheWorkspaceAndTheReadout)
{
    EXPECT_FALSE(window().rightColumn()->isVisible()) << "the readout shows beside Home with no image open";
    ImageViewControl* const view = openView(QStringLiteral("open-image-shows-the-readout"));
    ASSERT_NE(view, nullptr);
    EXPECT_TRUE(window().isWorkspaceShown());
    EXPECT_TRUE(window().rightColumn()->isVisible());
    ASSERT_NE(window().readoutColumn(), nullptr);
    EXPECT_TRUE(window().rightColumn()->isAncestorOf(window().readoutColumn()));
    EXPECT_TRUE(an::tests::app::processEventsUntil([&]() {
        return window().readoutColumn()->viewName() == QString::fromUtf8(view->getUniqueName());
    })) << "the readout names "
        << window().readoutColumn()->viewName().toStdString();
    // Beside Home too, while the image is open.
    window().showHome();
    EXPECT_TRUE(window().rightColumn()->isVisible());
}

// The rail lists Home, the workspace and Integration at its top and the Session Console and Settings at its foot; a
// click on a place shows it and marks it, the workspace only while an image is open, and the Console's item opens and
// closes the console.
TEST_F(ObservatoryWindow, RailSelectsPlaces)
{
    an::ui::AnIconRail& rail = *window().rail();
    using Window = an::ui::observatory::ObservatoryWindow;
    EXPECT_EQ(railItem(rail, QString::fromLatin1(Window::kRailHome)).section, an::ui::RailSection::Place);
    EXPECT_EQ(railItem(rail, QString::fromLatin1(Window::kRailWorkspace)).section, an::ui::RailSection::Place);
    EXPECT_EQ(railItem(rail, QString::fromLatin1(Window::kRailIntegration)).section, an::ui::RailSection::Place);
    EXPECT_EQ(railItem(rail, QString::fromLatin1(Window::kRailConsole)).section, an::ui::RailSection::Foot);
    EXPECT_EQ(railItem(rail, QString::fromLatin1(Window::kRailSettings)).section, an::ui::RailSection::Foot);
    EXPECT_EQ(railItem(rail, QString::fromLatin1(Window::kRailWorkspace)).text, QStringLiteral("Workspace 1"));
    EXPECT_TRUE(railItem(rail, QString::fromLatin1(Window::kRailHome)).selected);

    // With no image open the workspace is not a place to go to.
    clickRail(rail, Window::kRailWorkspace);
    EXPECT_TRUE(window().isHomeShown());

    ImageViewControl* const view = openView(QStringLiteral("rail-selects-places"));
    ASSERT_NE(view, nullptr);
    EXPECT_TRUE(railItem(rail, QString::fromLatin1(Window::kRailWorkspace)).selected);
    EXPECT_FALSE(railItem(rail, QString::fromLatin1(Window::kRailHome)).selected);

    clickRail(rail, Window::kRailHome);
    EXPECT_TRUE(window().isHomeShown());
    EXPECT_TRUE(railItem(rail, QString::fromLatin1(Window::kRailHome)).selected);
    EXPECT_FALSE(railItem(rail, QString::fromLatin1(Window::kRailWorkspace)).selected);

    clickRail(rail, Window::kRailWorkspace);
    EXPECT_TRUE(window().isWorkspaceShown());
    EXPECT_TRUE(railItem(rail, QString::fromLatin1(Window::kRailWorkspace)).selected);

    clickRail(rail, Window::kRailConsole);
    EXPECT_TRUE(window().isSessionConsoleVisible());
    // The console closes and is deleted later, as a closed dock is: the case delivers the deletion an event loop would.
    clickRail(rail, Window::kRailConsole);
    EXPECT_TRUE(an::tests::app::processEventsUntil([&]() {
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        return !window().isSessionConsoleVisible();
    }));
}

// With the menu bar hidden, a catalogue action's shortcut still reaches what it does: Ctrl+O opens the file dialog.
TEST_F(ObservatoryWindow, ShortcutsWorkWithTheMenuBarHidden)
{
    ASSERT_FALSE(window().menuBarOnAlt().isShown());
    window().activateWindow();
    ASSERT_TRUE(QTest::qWaitForWindowActive(&window()));
    bool asked = false;
    const std::shared_ptr<an::tests::app::WindowPoll> poll = an::tests::app::answerWhenShown([&asked](QWidget* shown) {
        auto* const dialog = qobject_cast<QFileDialog*>(shown);
        if (dialog == nullptr)
        {
            return false;
        }
        asked = true;
        dialog->reject();
        return true;
    });
    QTest::keyClick(window().centre(), Qt::Key_O, Qt::ControlModifier);
    EXPECT_TRUE(an::tests::app::processEventsUntil([&asked]() { return asked; }, 5000))
        << "Ctrl+O did not reach Open with the menu bar hidden";
    poll->ended = true;
    EXPECT_FALSE(window().menuBarOnAlt().isShown());
}

// Home's quick action opens the New Sequence window, as the dashboard always did.
TEST_F(ObservatoryWindow, HomeOpensANewSequence)
{
    ASSERT_TRUE(window().isHomeShown());
    MainDashboard* const dashboard = window().homePage()->dashboard();
    ASSERT_NE(dashboard, nullptr);
    dashboard->newSequenceRequested();
    NewSequenceControl* opened = nullptr;
    EXPECT_TRUE(an::tests::app::processEventsUntil([&opened]() {
        for (QWidget* widget : QApplication::topLevelWidgets())
        {
            if (auto* const control = dynamic_cast<NewSequenceControl*>(widget);
                control != nullptr && control->isVisible())
            {
                opened = control;
            }
        }
        return opened != nullptr;
    }));
    if (opened != nullptr)
    {
        opened->close();
    }
}

// The window keeps where it is for the next start, in the settings store, as the classic window keeps its own.
TEST_F(ObservatoryWindow, KeepsItsPlaceForTheNextStart)
{
    an::ui::IGlobalSettingsProvider* const settings = an::ui::appContext()->getGlobalSettings();
    ASSERT_NE(settings, nullptr);
    // Whatever an earlier window kept is forgotten first, so what is read is this window's place.
    for (const char* key : {SettingsKeys::INTERFACE_WINDOW_GEOMETRY_X, SettingsKeys::INTERFACE_WINDOW_GEOMETRY_WIDTH})
    {
        ASSERT_TRUE(settings->upsert(QString::fromLatin1(key), QStringLiteral("-1")));
    }
    window().keepGeometry();
    const QRect place = window().normalGeometry();
    EXPECT_EQ(settings->select(QString::fromLatin1(SettingsKeys::INTERFACE_WINDOW_GEOMETRY_WIDTH)),
              QString::number(place.width()));
    EXPECT_EQ(settings->select(QString::fromLatin1(SettingsKeys::INTERFACE_WINDOW_GEOMETRY_X)),
              QString::number(place.x()));
}

} // namespace
} // namespace an::tests::observatory
