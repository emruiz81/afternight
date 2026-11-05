// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// What a top-level process window does when the store of its settings fails it: a read that fails restores nothing,
/// a size stored larger than the screen is held to the screen, and a write that fails still lets the window close.

#include "Test_ProcessFramework/FrameworkCaseSupport.h"

#include "controls/TopLevelProcessBaseControl.h"
#include "controls/settings/SettingsKeys.h"

#include <QGuiApplication>
#include <QPointer>
#include <QScreen>
#include <QTest>
#include <QVBoxLayout>

#include <gtest/gtest.h>

#include <string>

namespace
{

using namespace an::tests::processes;
using framework::ScopedAppContext;
using framework::WindowSettingsContext;

/// A top-level process window that stores one text as its settings and counts the settings it restores.
class PersistenceAwareProcessShell final : public TopLevelProcessBaseControl
{
public:
    PersistenceAwareProcessShell() : TopLevelProcessBaseControl("PersistenceAwareProcessShell")
    {
        new QVBoxLayout(this);
        resize(320, 220);
    }

    [[nodiscard]] int deserializeCount() const
    {
        return m_deserializeCount;
    }

    [[nodiscard]] QString lastDeserializedSettings() const
    {
        return m_lastDeserializedSettings;
    }

protected:
    std::string serialize_json_settings() override
    {
        return "persisted-shell-json";
    }

    void deserialize_json_settings(const std::string& json_settings) override
    {
        ++m_deserializeCount;
        m_lastDeserializedSettings = QString::fromStdString(json_settings);
    }

private:
    int m_deserializeCount = 0;
    QString m_lastDeserializedSettings;
};

/// Turns the event loop, deferred deletions included, until a window is gone or the patience runs out.
[[nodiscard]] bool waitUntilDeleted(const QPointer<QWidget>& window)
{
    for (int turn = 0; turn < 200 && !window.isNull(); ++turn)
    {
        QTest::qWait(10);
    }
    return window.isNull();
}

} // namespace

// A window whose read of its stored settings fails keeps its own size and restores no settings.
TEST(ProcessPersistenceCriticalPathTest, InitializeSkipsRestoreWhenSelectFails)
{
    WindowSettingsContext settings(framework::suiteContext());
    const ScopedAppContext installed(settings);
    settings.upsert(QString::fromUtf8(SettingsKeys::INTERFACE_REMEMBER_WINDOW_SIZE), QStringLiteral("1"));
    settings.upsert(QStringLiteral("PersistenceAwareProcessShell"), Qt::WindowNoState, 900, 700,
                    QStringLiteral("should-not-apply"));
    settings.failWindowReads = true;

    auto* process = new PersistenceAwareProcessShell();
    const QPointer<QWidget> window(process);
    const QSize originalSize = process->size();
    process->initializeTopLevelProcess();
    EXPECT_EQ(process->size(), originalSize);
    EXPECT_EQ(process->deserializeCount(), 0);

    settings.failWindowReads = false;
    process->close();
    EXPECT_TRUE(waitUntilDeleted(window));
}

// A size stored larger than the screen is held to the screen's available area, and the settings are restored once.
TEST(ProcessPersistenceCriticalPathTest, RestoreClampsOversizedGeometryToAvailableScreen)
{
    WindowSettingsContext settings(framework::suiteContext());
    const ScopedAppContext installed(settings);
    settings.upsert(QString::fromUtf8(SettingsKeys::INTERFACE_REMEMBER_WINDOW_SIZE), QStringLiteral("1"));
    settings.upsert(QStringLiteral("PersistenceAwareProcessShell"), Qt::WindowNoState, 100000, 100000,
                    QStringLiteral("restored-json"));

    auto* process = new PersistenceAwareProcessShell();
    const QPointer<QWidget> window(process);
    process->initializeTopLevelProcess();

    QScreen* screen = QGuiApplication::primaryScreen();
    ASSERT_NE(screen, nullptr);
    const QSize available = screen->availableGeometry().size();
    EXPECT_LE(process->size().width(), available.width());
    EXPECT_LE(process->size().height(), available.height());
    EXPECT_EQ(process->deserializeCount(), 1);
    EXPECT_EQ(process->lastDeserializedSettings(), QStringLiteral("restored-json"));

    process->close();
    EXPECT_TRUE(waitUntilDeleted(window));
}

// A window whose write of its settings fails still closes and is deleted, having asked once, and nothing is stored.
TEST(ProcessPersistenceCriticalPathTest, CloseDoesNotCrashWhenPersistenceWriteFails)
{
    WindowSettingsContext settings(framework::suiteContext());
    const ScopedAppContext installed(settings);
    settings.upsert(QString::fromUtf8(SettingsKeys::INTERFACE_REMEMBER_WINDOW_SIZE), QStringLiteral("1"));
    settings.failWindowWrites = true;

    auto* process = new PersistenceAwareProcessShell();
    const QPointer<QWidget> window(process);
    process->close();
    EXPECT_TRUE(waitUntilDeleted(window));
    EXPECT_EQ(settings.windowWrites, 1);
    EXPECT_FALSE(settings.stored(QStringLiteral("PersistenceAwareProcessShell")).has_value());
}
