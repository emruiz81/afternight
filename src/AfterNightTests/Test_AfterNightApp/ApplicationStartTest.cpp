// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// The application's start and end in their order, each case over a shell of its own: the stores open first, a shell is
// chosen among the registered ones - from the command line, the environment, the setting, or the default - the style
// is installed before the first widget, and the chosen shell is made over the core and built; the application shows it,
// and at its end asks it to keep its geometry before it goes with the session. Each case makes, starts and destroys a
// shell of its own.

#include "ClassicShell.h"
#include "app/ApplicationStart.h"
#include "core/ApplicationCore.h"
#include "core/ApplicationSession.h"
#include "core/IShell.h"
#include "core/ShellRegistry.h"
#include "core/ShellSelection.h"

#include "controls/GlobalSettingsPersistence.h"
#include "controls/IAppContext.h"
#include "controls/settings/SettingsKeys.h"

#include <QApplication>
#include <QProcessEnvironment>
#include <QString>
#include <QStringList>
#include <QWidget>

#include <gtest/gtest.h>

#include <memory>
#include <utility>

namespace
{

using an::ui::shell::ApplicationCore;
using an::ui::shell::IShell;
using an::ui::shell::IStatusDisplay;
using an::ui::shell::IViewHost;
using an::ui::shell::ShellChoiceSource;
using an::ui::shell::ShellDescriptor;
using an::ui::shell::ShellRegistry;

/// A shell that makes a plain window and records, in the log it is given, what the application asks of it.
class RecordingShell final : public IShell, public IViewHost, public IStatusDisplay
{
public:
    RecordingShell(QString id, QStringList& log) : m_id(std::move(id)), m_log(log)
    {
        m_log.append(m_id + QStringLiteral(" made"));
    }

    ~RecordingShell() override
    {
        m_log.append(m_id + QStringLiteral(" destroyed"));
    }

    RecordingShell(const RecordingShell&) = delete;
    RecordingShell& operator=(const RecordingShell&) = delete;

    void build() override
    {
        m_window = std::make_unique<QWidget>();
        m_log.append(m_id + QStringLiteral(" built"));
    }
    QWidget* mainWindow() override
    {
        return m_window.get();
    }
    IViewHost& viewHost() override
    {
        return *this;
    }
    IStatusDisplay& statusDisplay() override
    {
        return *this;
    }
    an::ui::IProcessPresenter* processPresenter() override
    {
        return nullptr;
    }
    void setSessionConsoleVisible(bool /*visible*/) override {}
    bool isSessionConsoleVisible() const override
    {
        return false;
    }
    void refreshTheme() override {}
    void showWithSavedGeometry() override
    {
        m_log.append(m_id + QStringLiteral(" shown"));
    }
    void saveGeometry() override
    {
        m_log.append(m_id + QStringLiteral(" saved its geometry"));
    }

    void place(ImageViewControl& /*view*/, const an::ui::shell::ViewPlacement& /*placement*/) override {}
    void activate(ImageViewControl& /*view*/) override {}
    void viewAdded(ImageViewControl& /*view*/, an::ui::shell::ViewOrigin /*origin*/) override {}
    void viewRemoved(ImageViewControl& /*view*/) override {}
    void viewRenamed(ImageViewControl& /*view*/, const std::string& /*previousName*/) override {}
    void maximize(ImageViewControl& /*view*/) override {}
    void restore(ImageViewControl& /*view*/) override {}
    bool isMaximizedViewActive() const override
    {
        return false;
    }
    an::ui::shell::DockTarget dockTarget(const ImageViewControl* /*view*/) override
    {
        return {};
    }
    void showHome() override {}
    void showViews() override {}

    void showImageInfo(ImageViewControl* /*view*/) override {}
    void showZoom(ImageViewControl* /*view*/) override {}
    void showCelestialCoordinates(double /*raDegrees*/, double /*decDegrees*/) override {}
    void clearCelestialCoordinates() override {}
    void clearImageInfo() override {}

private:
    QString m_id;
    QStringList& m_log;
    std::unique_ptr<QWidget> m_window;
};

ShellDescriptor recordingDescriptor(const QString& id, QStringList& log)
{
    return ShellDescriptor{
        .id = id,
        .displayName = id,
        .create = [id, &log](ApplicationCore&) -> std::unique_ptr<IShell> {
            return std::make_unique<RecordingShell>(id, log);
        },
    };
}

/// Two recording shells, `classic` - the default - and `second`.
ShellRegistry recordingShells(QStringList& log)
{
    ShellRegistry registry;
    EXPECT_TRUE(registry.add(recordingDescriptor(QStringLiteral("classic"), log)));
    EXPECT_TRUE(registry.add(recordingDescriptor(QStringLiteral("second"), log)));
    return registry;
}

} // namespace

// The stores open before anything else, because a shell is chosen from one of them; the style is installed next, before
// the first widget exists; and the classic shell's window is made last, over the core, and readied with the session.
TEST(ApplicationStart, TheStoresOpenBeforeTheStyleAndTheFirstWidget)
{
    ASSERT_EQ(an::ui::appContext(), nullptr);

    an::ui::ApplicationSession session;
    ApplicationCore core;
    core.setApplicationSession(&session);
    ShellRegistry registry;
    ASSERT_TRUE(registry.add(an::ui::shell::classicShellDescriptor()));
    ASSERT_FALSE(core.storesOpen());
    const qsizetype widgetsBefore = QApplication::allWidgets().size();
    int styled = 0;
    bool storesOpenWhenStyled = false;
    qsizetype widgetsWhenStyled = -1;

    an::ui::StartedApplication started =
        an::ui::startApplication(core, registry, {QStringLiteral("AfterNight")}, QProcessEnvironment(), [&]() {
            ++styled;
            storesOpenWhenStyled = core.storesOpen();
            widgetsWhenStyled = QApplication::allWidgets().size();
        });

    ASSERT_NE(started.shell, nullptr);
    QWidget* const window = started.shell->mainWindow();
    ASSERT_NE(window, nullptr);
    EXPECT_EQ(styled, 1);
    EXPECT_TRUE(storesOpenWhenStyled) << "the style was installed before the stores were open";
    EXPECT_EQ(widgetsWhenStyled, widgetsBefore) << "a widget was made before the style was installed";
    EXPECT_TRUE(session.isOpen());
    ASSERT_NE(an::ui::appContext(), nullptr);
    EXPECT_EQ(an::ui::appContext()->getMainWindow(), window);
    EXPECT_EQ(an::ui::appContext()->getEngineSessionProvider(), &session);

    ASSERT_TRUE(window->close());
    const an::Status shut = an::ui::stopApplication(std::move(started.shell), session);
    EXPECT_TRUE(shut.ok()) << shut.message();
    EXPECT_EQ(an::ui::appContext(), nullptr);
}

// The shell the command line names is made and built - after the stores and the style - shown, asked at the end to keep
// its geometry, and destroyed before the session is shut down; the other registered shell is never made.
TEST(ApplicationStart, StartsTheChosenShell)
{
    QStringList log;
    const ShellRegistry registry = recordingShells(log);
    an::ui::ApplicationSession session;
    ApplicationCore core;
    core.setApplicationSession(&session);

    an::ui::StartedApplication started = an::ui::startApplication(
        core, registry, {QStringLiteral("AfterNight"), QStringLiteral("--shell=second")}, QProcessEnvironment(), [&]() {
            log.append(core.storesOpen() ? QStringLiteral("styled, the stores open") : QStringLiteral("styled"));
        });

    ASSERT_NE(started.shell, nullptr);
    ASSERT_NE(started.choice.descriptor, nullptr);
    EXPECT_EQ(started.choice.descriptor->id, QStringLiteral("second"));
    EXPECT_EQ(started.choice.source, ShellChoiceSource::CommandLine);

    an::ui::showApplication(*started.shell);
    const an::Status shut = an::ui::stopApplication(std::move(started.shell), session);

    EXPECT_TRUE(shut.ok()) << shut.message();
    EXPECT_EQ(log, (QStringList{QStringLiteral("styled, the stores open"), QStringLiteral("second made"),
                                QStringLiteral("second built"), QStringLiteral("second shown"),
                                QStringLiteral("second saved its geometry"), QStringLiteral("second destroyed")}))
        << log.join(QStringLiteral(", ")).toStdString();
}

// With nothing on the command line or in the environment, the shell the settings store names is the one made.
TEST(ApplicationStart, ReadsTheShellFromTheSettingsStore)
{
    QStringList log;
    const ShellRegistry registry = recordingShells(log);
    an::ui::ApplicationSession session;
    ApplicationCore core;
    core.setApplicationSession(&session);
    core.openStores();
    ASSERT_NE(core.globalSettingsStore(), nullptr);
    ASSERT_TRUE(core.globalSettingsStore()->upsert(QString::fromLatin1(SettingsKeys::INTERFACE_SHELL),
                                                   QStringLiteral("second")));

    an::ui::StartedApplication started =
        an::ui::startApplication(core, registry, {QStringLiteral("AfterNight")}, QProcessEnvironment(), {});

    ASSERT_NE(started.choice.descriptor, nullptr);
    EXPECT_EQ(started.choice.descriptor->id, QStringLiteral("second"));
    EXPECT_EQ(started.choice.source, ShellChoiceSource::Setting);
    EXPECT_TRUE(log.contains(QStringLiteral("second built")));
    EXPECT_FALSE(log.contains(QStringLiteral("classic made")));

    // The setting lasts: it is taken back, so no case after this one starts from it.
    ASSERT_TRUE(core.globalSettingsStore()->remove(QString::fromLatin1(SettingsKeys::INTERFACE_SHELL)));
    EXPECT_TRUE(an::ui::stopApplication(std::move(started.shell), session).ok());
}

// A shell no registered shell answers to is passed over with a warning, and the default is made: a mistyped option
// never stops the application from starting.
TEST(ApplicationStart, UnknownShellFallsBack)
{
    QStringList log;
    const ShellRegistry registry = recordingShells(log);
    an::ui::ApplicationSession session;
    ApplicationCore core;
    core.setApplicationSession(&session);

    an::ui::StartedApplication started = an::ui::startApplication(
        core, registry, {QStringLiteral("AfterNight"), QStringLiteral("--shell=nope")}, QProcessEnvironment(), {});

    ASSERT_NE(started.choice.descriptor, nullptr);
    EXPECT_EQ(started.choice.descriptor->id, QStringLiteral("classic"));
    EXPECT_EQ(started.choice.source, ShellChoiceSource::Default);
    ASSERT_FALSE(started.choice.warnings.isEmpty());
    EXPECT_TRUE(started.choice.warnings.front().contains(QStringLiteral("nope")))
        << started.choice.warnings.join(QStringLiteral("; ")).toStdString();
    EXPECT_TRUE(log.contains(QStringLiteral("classic built")));
    EXPECT_TRUE(an::ui::stopApplication(std::move(started.shell), session).ok());
}
