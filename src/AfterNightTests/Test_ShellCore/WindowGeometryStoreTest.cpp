// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Where the main window was, kept between runs: its normal place and size and whether it was maximized, written to the
/// interface settings; shown again at that place when the place is on a screen there is now and the person asked for
/// the window to be remembered, never smaller than the window's minimum; maximized when it was, when the place is on
/// no screen, and when nothing was kept.

#include "ShellCoreTestSupport.h"

#include "core/ApplicationCore.h"
#include "services/WindowGeometryStore.h"

#include "controls/GlobalSettingsPersistence.h"
#include "controls/settings/SettingsKeys.h"

#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QList>
#include <QRect>
#include <QSize>
#include <QString>
#include <QTemporaryDir>
#include <QWidget>

namespace
{

using an::tests::shellcore::ScopedStorageRoot;
using an::ui::shell::ApplicationCore;
using an::ui::shell::WindowGeometryStore;

const QList<QRect> kOneScreen{QRect(0, 0, 1920, 1080)};
const QList<QRect> kTwoScreens{QRect(0, 0, 1920, 1080), QRect(1920, 0, 1920, 1080)};
const QSize kMinimum(1200, 720);

class WindowGeometryStoreTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        ASSERT_TRUE(m_root.isValid());
        m_core.openStores();
        ASSERT_NE(m_core.globalSettingsStore(), nullptr);
    }

    void TearDown() override
    {
        m_core.closeStores();
    }

    void keep(const char* key, const QString& value)
    {
        ASSERT_TRUE(m_core.globalSettingsStore()->upsert(QString::fromLatin1(key), value));
    }

    [[nodiscard]] QString kept(const char* key)
    {
        return m_core.globalSettingsStore()->select(QString::fromLatin1(key), QStringLiteral("(none)"));
    }

    /// The settings of a window last seen at `place`, maximized or not, with the setting that asks for it remembered.
    void keepPlace(const QRect& place, bool maximized)
    {
        keep(SettingsKeys::INTERFACE_REMEMBER_WINDOW_SIZE, QStringLiteral("1"));
        keep(SettingsKeys::INTERFACE_WINDOW_GEOMETRY_X, QString::number(place.x()));
        keep(SettingsKeys::INTERFACE_WINDOW_GEOMETRY_Y, QString::number(place.y()));
        keep(SettingsKeys::INTERFACE_WINDOW_GEOMETRY_WIDTH, QString::number(place.width()));
        keep(SettingsKeys::INTERFACE_WINDOW_GEOMETRY_HEIGHT, QString::number(place.height()));
        keep(SettingsKeys::INTERFACE_WINDOW_MAXIMIZED, maximized ? QStringLiteral("1") : QStringLiteral("0"));
    }

    QTemporaryDir m_root;
    ScopedStorageRoot m_scopedRoot{m_root.path()};
    ApplicationCore m_core;
};

} // namespace

// What is kept of a window is its normal place and size, and whether it is maximized.
TEST_F(WindowGeometryStoreTest, KeepsTheNormalPlaceAndWhetherTheWindowIsMaximized)
{
    QWidget window;
    window.setGeometry(QRect(120, 140, 1300, 800));
    window.showNormal();
    QCoreApplication::processEvents();
    const QRect normal = window.normalGeometry();

    WindowGeometryStore(m_core).save(window);

    EXPECT_EQ(kept(SettingsKeys::INTERFACE_WINDOW_MAXIMIZED), QStringLiteral("0"));
    EXPECT_EQ(kept(SettingsKeys::INTERFACE_WINDOW_GEOMETRY_X), QString::number(normal.x()));
    EXPECT_EQ(kept(SettingsKeys::INTERFACE_WINDOW_GEOMETRY_Y), QString::number(normal.y()));
    EXPECT_EQ(kept(SettingsKeys::INTERFACE_WINDOW_GEOMETRY_WIDTH), QString::number(normal.width()));
    EXPECT_EQ(kept(SettingsKeys::INTERFACE_WINDOW_GEOMETRY_HEIGHT), QString::number(normal.height()));

    window.showMaximized();
    QCoreApplication::processEvents();
    WindowGeometryStore(m_core).save(window);
    EXPECT_EQ(kept(SettingsKeys::INTERFACE_WINDOW_MAXIMIZED), QStringLiteral("1"));
}

// A window last seen at a place on a screen there is now is shown there, at its size.
TEST_F(WindowGeometryStoreTest, RestoresThePlaceWhenItIsOnAScreen)
{
    keepPlace(QRect(2100, 80, 1400, 900), false);

    const WindowGeometryStore::Placement placement =
        WindowGeometryStore(m_core).restoredPlacement(kTwoScreens, kMinimum);

    EXPECT_FALSE(placement.maximized);
    EXPECT_EQ(placement.geometry, QRect(2100, 80, 1400, 900));
}

// A place on a screen that is gone - the window was last on a second screen no longer plugged in - is not used: the
// window is shown maximized.
TEST_F(WindowGeometryStoreTest, ShowsMaximizedWhenThePlaceIsOnAScreenThatIsGone)
{
    keepPlace(QRect(2100, 80, 1400, 900), false);

    EXPECT_TRUE(WindowGeometryStore(m_core).restoredPlacement(kOneScreen, kMinimum).maximized);
    EXPECT_TRUE(WindowGeometryStore::intersectsAnyScreen(QRect(100, 100, 900, 700), kTwoScreens));
    EXPECT_TRUE(WindowGeometryStore::intersectsAnyScreen(QRect(2500, 100, 900, 700), kTwoScreens));
    EXPECT_FALSE(WindowGeometryStore::intersectsAnyScreen(QRect(-5000, -5000, 200, 200), kTwoScreens));
}

// A window that was maximized is shown maximized.
TEST_F(WindowGeometryStoreTest, ShowsMaximizedWhenItWasMaximized)
{
    keepPlace(QRect(100, 100, 1400, 900), true);

    EXPECT_TRUE(WindowGeometryStore(m_core).restoredPlacement(kOneScreen, kMinimum).maximized);
}

// A window is never restored smaller than its minimum; a larger size is kept.
TEST_F(WindowGeometryStoreTest, NeverRestoresSmallerThanTheMinimum)
{
    keepPlace(QRect(100, 100, 640, 480), false);

    const WindowGeometryStore::Placement placement =
        WindowGeometryStore(m_core).restoredPlacement(kOneScreen, kMinimum);

    EXPECT_FALSE(placement.maximized);
    EXPECT_EQ(placement.geometry, QRect(QPoint(100, 100), kMinimum));
    EXPECT_EQ(WindowGeometryStore::clampedToMinimum(QSize(1536, 864), kMinimum), QSize(1536, 864));
    EXPECT_EQ(WindowGeometryStore::clampedToMinimum(QSize(1536, 600), kMinimum), QSize(1536, 720));
}

// Nothing kept, or the person asked for the window not to be remembered: it is shown maximized.
TEST_F(WindowGeometryStoreTest, ShowsMaximizedWhenNothingIsKeptOrTheSettingDoesNotAskForIt)
{
    EXPECT_TRUE(WindowGeometryStore(m_core).restoredPlacement(kOneScreen, kMinimum).maximized);

    keepPlace(QRect(100, 100, 1400, 900), false);
    keep(SettingsKeys::INTERFACE_REMEMBER_WINDOW_SIZE, QStringLiteral("0"));
    EXPECT_TRUE(WindowGeometryStore(m_core).restoredPlacement(kOneScreen, kMinimum).maximized);
}

// Showing a window places it as the store says: at its place, or maximized.
TEST_F(WindowGeometryStoreTest, ShowsTheWindowWhereItWas)
{
    keepPlace(QRect(100, 120, 1400, 900), false);
    QWidget window;
    window.setMinimumSize(kMinimum);

    WindowGeometryStore(m_core).show(window, kOneScreen);
    QCoreApplication::processEvents();

    EXPECT_TRUE(window.isVisible());
    EXPECT_FALSE(window.isMaximized());
    EXPECT_EQ(window.geometry(), QRect(100, 120, 1400, 900));

    keep(SettingsKeys::INTERFACE_WINDOW_MAXIMIZED, QStringLiteral("1"));
    QWidget maximized;
    WindowGeometryStore(m_core).show(maximized, kOneScreen);
    QCoreApplication::processEvents();
    EXPECT_TRUE(maximized.isMaximized());
}
