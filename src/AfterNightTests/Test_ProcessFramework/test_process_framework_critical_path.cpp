// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// A top-level process window's remembered geometry: restored, within the screen, when the interface setting asks for
/// window sizes to be remembered, and left alone when it does not - the window's own settings restored either way, and
/// stored again when it closes.

#include "Test_ProcessFramework/FrameworkCaseSupport.h"

#include "controls/TopLevelProcessBaseControl.h"
#include "controls/settings/SettingsKeys.h"

#include <QDialogButtonBox>
#include <QGuiApplication>
#include <QPointer>
#include <QScreen>
#include <QTest>
#include <QVBoxLayout>

#include <gtest/gtest.h>

#include <algorithm>
#include <optional>
#include <string>

namespace
{

using namespace an::tests::processes;
using framework::ScopedAppContext;
using framework::StoredWindow;
using framework::WindowSettingsContext;

/// A top-level process window that stores and restores one text as its settings.
class CriticalPathProcessShell final : public TopLevelProcessBaseControl
{
public:
    CriticalPathProcessShell() : TopLevelProcessBaseControl("CriticalPathProcessShell")
    {
        auto* layout = new QVBoxLayout(this);
        layout->addWidget(
            new QDialogButtonBox(QDialogButtonBox::Apply | QDialogButtonBox::Close | QDialogButtonBox::Reset, this));
        resize(360, 240);
    }

    [[nodiscard]] QString lastDeserializedSettings() const
    {
        return m_lastDeserializedSettings;
    }

protected:
    std::string serialize_json_settings() override
    {
        return "persisted-critical-json";
    }

    void deserialize_json_settings(const std::string& json_settings) override
    {
        m_lastDeserializedSettings = QString::fromStdString(json_settings);
    }

private:
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

// With window sizes remembered, a window opens at the size it stored, held to the screen, and restores its settings;
// closed, it stores its size and its settings.
TEST(ProcessFrameworkCriticalPathTest, RestoresGeometryWhenRememberWindowSizeIsEnabled)
{
    WindowSettingsContext settings(framework::suiteContext());
    const ScopedAppContext installed(settings);
    settings.upsert(QString::fromUtf8(SettingsKeys::INTERFACE_REMEMBER_WINDOW_SIZE), QStringLiteral("1"));
    settings.upsert(QStringLiteral("CriticalPathProcessShell"), Qt::WindowNoState, 640, 420,
                    QStringLiteral("stored-json"));

    auto* process = new CriticalPathProcessShell();
    const QPointer<QWidget> window(process);
    process->initializeTopLevelProcess();

    QSize expectedSize(640, 420);
    if (QScreen* screen = QGuiApplication::primaryScreen())
    {
        const QSize maxSize = screen->availableGeometry().size();
        expectedSize.setWidth((std::min)(expectedSize.width(), maxSize.width()));
        expectedSize.setHeight((std::min)(expectedSize.height(), maxSize.height()));
    }
    EXPECT_EQ(process->size(), expectedSize);
    EXPECT_EQ(process->lastDeserializedSettings(), QStringLiteral("stored-json"));

    process->close();
    ASSERT_TRUE(waitUntilDeleted(window));
    const std::optional<StoredWindow> stored = settings.stored(QStringLiteral("CriticalPathProcessShell"));
    ASSERT_TRUE(stored.has_value());
    EXPECT_GT(stored->width, 0);
    EXPECT_GT(stored->height, 0);
    EXPECT_EQ(stored->json, QStringLiteral("persisted-critical-json"));
}

// With window sizes not remembered, a window keeps its own size but still restores its settings; closed, it stores no
// size and its settings.
TEST(ProcessFrameworkCriticalPathTest, SkipsGeometryRestoreWhenRememberWindowSizeIsDisabled)
{
    WindowSettingsContext settings(framework::suiteContext());
    const ScopedAppContext installed(settings);
    settings.upsert(QString::fromUtf8(SettingsKeys::INTERFACE_REMEMBER_WINDOW_SIZE), QStringLiteral("0"));
    settings.upsert(QStringLiteral("CriticalPathProcessShell"), Qt::WindowNoState, 777, 555,
                    QStringLiteral("stored-json"));

    auto* process = new CriticalPathProcessShell();
    const QPointer<QWidget> window(process);
    const QSize defaultSize = process->size();
    process->initializeTopLevelProcess();
    EXPECT_EQ(process->size(), defaultSize);
    EXPECT_EQ(process->lastDeserializedSettings(), QStringLiteral("stored-json"));

    process->close();
    ASSERT_TRUE(waitUntilDeleted(window));
    const std::optional<StoredWindow> stored = settings.stored(QStringLiteral("CriticalPathProcessShell"));
    ASSERT_TRUE(stored.has_value());
    EXPECT_EQ(stored->windowState, Qt::WindowNoState);
    EXPECT_EQ(stored->width, -1);
    EXPECT_EQ(stored->height, -1);
    EXPECT_EQ(stored->json, QStringLiteral("persisted-critical-json"));
}
