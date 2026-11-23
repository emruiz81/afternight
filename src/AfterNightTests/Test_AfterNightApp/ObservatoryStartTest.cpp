// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// The application's start with the shell it registers: the Observatory shell started when it is chosen and when
// nothing chooses a shell, and when a source names a shell this version does not have; the core initialized and
// started and the context made before the shell's window is; and the time from the start to the first paint of the
// window, measured.

#include "ObservatoryShell.h"
#include "ObservatoryWindow.h"
#include "app/ApplicationStart.h"
#include "core/ApplicationContext.h"
#include "core/ApplicationCore.h"
#include "core/ApplicationSession.h"
#include "core/IShell.h"
#include "core/ShellRegistry.h"
#include "core/ShellSelection.h"
#include "controls/GlobalSettingsPersistence.h"
#include "controls/IAppContext.h"
#include "controls/settings/SettingsKeys.h"
#include "theme/AnDesignTokens.h"

#include <QApplication>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEvent>
#include <QObject>
#include <QProcessEnvironment>
#include <QStackedWidget>
#include <QThread>
#include <QWidget>

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdio>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace
{

using an::ui::shell::ApplicationCore;
using an::ui::shell::ShellDescriptor;
using an::ui::shell::ShellRegistry;

/// What the core and the context were when a shell's descriptor made the shell.
struct WhenMade
{
    bool made = false;
    bool initialized = false;
    bool started = false;
    bool contextInstalled = false;
    bool sessionOpen = false;
    bool noShellYet = false;
};

/// A registered descriptor that records what the core was when it made its shell, then makes it as it would.
[[nodiscard]] ShellDescriptor recording(ShellDescriptor descriptor, ApplicationCore& core,
                                        const an::ui::ApplicationSession& session, WhenMade& seen)
{
    std::function<std::unique_ptr<an::ui::shell::IShell>(ApplicationCore&)> make = std::move(descriptor.create);
    descriptor.create = [make = std::move(make), &core, &session, &seen](ApplicationCore& given) {
        seen.made = true;
        seen.initialized = core.isInitialized();
        seen.started = core.isStarted();
        seen.contextInstalled = core.context() != nullptr && an::ui::appContext() == core.context();
        seen.sessionOpen = session.isOpen();
        seen.noShellYet = core.shell() == nullptr;
        return make(given);
    };
    return descriptor;
}

} // namespace

// The Observatory shell, chosen on the command line, is started in the application's order: drawn in the Observatory
// profile, its window made over the core and built - Home shown, nothing shown on the screen - the context answering
// with it, and it goes before the session at the end, the context with it.
TEST(ApplicationStart, StartsTheObservatoryShell)
{
    ASSERT_EQ(an::ui::appContext(), nullptr);

    an::ui::ApplicationSession session;
    ApplicationCore core;
    core.setApplicationSession(&session);
    ShellRegistry registry;
    an::ui::registerApplicationShells(registry);
    core.setShellRegistry(&registry);

    std::optional<an::ui::StyleProfile> styledWith;
    an::ui::StartedApplication started = an::ui::startApplication(
        core, registry, {QStringLiteral("AfterNight"), QStringLiteral("--shell=observatory")}, QProcessEnvironment(),
        [&styledWith](const an::ui::StyleProfile profile) { styledWith = profile; });

    ASSERT_NE(started.shell, nullptr);
    ASSERT_NE(started.choice.descriptor, nullptr);
    EXPECT_EQ(started.choice.descriptor->id, QStringLiteral("observatory"));
    EXPECT_EQ(started.choice.source, an::ui::shell::ShellChoiceSource::CommandLine);
    EXPECT_EQ(styledWith, std::optional<an::ui::StyleProfile>(an::ui::StyleProfile::Observatory));
    auto* const observatory = dynamic_cast<an::ui::observatory::ObservatoryShell*>(started.shell.get());
    ASSERT_NE(observatory, nullptr) << "the shell the command line names was not the one made";
    an::ui::observatory::ObservatoryWindow* const window = observatory->window();
    ASSERT_NE(window, nullptr);
    EXPECT_EQ(started.shell->mainWindow(), window);
    EXPECT_FALSE(window->isVisible()) << "a shell is built, not shown, by the start";
    EXPECT_TRUE(window->isHomeShown());
    EXPECT_TRUE(session.isOpen());
    ASSERT_NE(an::ui::appContext(), nullptr);
    EXPECT_EQ(an::ui::appContext()->getMainWindow(), window);
    EXPECT_EQ(an::ui::appContext()->getEngineSessionProvider(), &session);
    // The Interface page's Shell row is given the one registered shell, and so hides itself.
    const std::vector<an::ui::ShellOption> shells =
        an::ui::appContext()->getInterfaceSettingsRuntimeService()->availableShells();
    ASSERT_EQ(shells.size(), 1u);
    EXPECT_EQ(shells[0].id, QStringLiteral("observatory"));

    const an::Status shut = an::ui::stopApplication(std::move(started.shell), session);
    EXPECT_TRUE(shut.ok()) << shut.message();
    EXPECT_EQ(an::ui::appContext(), nullptr);
}

// With nothing on the command line, in the environment or in the settings store, the application starts the
// Observatory shell and writes no choice into the store. A source that names the classic shell, which this version no
// longer has - a store an earlier build of the line wrote may - names no registered shell: it is passed over with a
// warning, and the Observatory shell starts all the same; the store is left as it was.
TEST(ApplicationStart, DefaultIsObservatory)
{
    using an::ui::shell::ShellChoice;
    using an::ui::shell::ShellChoiceSource;

    ASSERT_EQ(an::ui::appContext(), nullptr);

    an::ui::ApplicationSession session;
    ApplicationCore core;
    core.setApplicationSession(&session);
    ShellRegistry registry;
    an::ui::registerApplicationShells(registry);
    core.setShellRegistry(&registry);
    core.openStores();
    GlobalSettingsPersistence* const store = core.globalSettingsStore();
    ASSERT_NE(store, nullptr);
    const QString key = QString::fromLatin1(SettingsKeys::INTERFACE_SHELL);
    ASSERT_EQ(store->selectOptional(key), std::nullopt) << "an earlier case left a shell in the store";

    const QString program = QStringLiteral("AfterNight");
    const auto choose = [&](const QStringList& arguments, const QProcessEnvironment& environment) {
        return an::ui::shell::ShellSelection::resolve(arguments, environment, core.globalSettings(), registry);
    };
    QProcessEnvironment namingClassic;
    namingClassic.insert(QString::fromLatin1(an::ui::shell::ShellSelection::kEnvironmentVariable),
                         QStringLiteral("classic"));
    const ShellChoice byOption = choose({program, QStringLiteral("--shell=classic")}, QProcessEnvironment());
    const ShellChoice byVariable = choose({program}, namingClassic);
    ASSERT_TRUE(store->upsert(key, QStringLiteral("classic")));
    const ShellChoice bySetting = choose({program}, QProcessEnvironment());
    EXPECT_EQ(store->selectOptional(key), std::optional<QString>(QStringLiteral("classic")))
        << "the choice wrote over the store";
    ASSERT_TRUE(store->remove(key));
    for (const ShellChoice& choice : {byOption, byVariable, bySetting})
    {
        ASSERT_NE(choice.descriptor, nullptr);
        EXPECT_EQ(choice.descriptor->id, QStringLiteral("observatory"));
        EXPECT_EQ(choice.source, ShellChoiceSource::Default);
        EXPECT_EQ(choice.styleProfile, an::ui::StyleProfile::Observatory);
        ASSERT_EQ(choice.warnings.size(), 1);
        EXPECT_TRUE(choice.warnings.front().contains(QStringLiteral("classic")))
            << choice.warnings.front().toStdString();
    }

    std::optional<an::ui::StyleProfile> styledWith;
    an::ui::StartedApplication started = an::ui::startApplication(core, registry, {program}, QProcessEnvironment(),
                                                                  [&styledWith](const an::ui::StyleProfile profile) {
                                                                      styledWith = profile;
                                                                  });

    ASSERT_NE(started.shell, nullptr);
    ASSERT_NE(started.choice.descriptor, nullptr);
    EXPECT_EQ(started.choice.descriptor->id, QStringLiteral("observatory"));
    EXPECT_EQ(started.choice.source, ShellChoiceSource::Default);
    EXPECT_TRUE(started.choice.warnings.isEmpty()) << started.choice.warnings.join(QLatin1Char('\n')).toStdString();
    EXPECT_EQ(styledWith, std::optional<an::ui::StyleProfile>(an::ui::StyleProfile::Observatory));
    EXPECT_NE(dynamic_cast<an::ui::observatory::ObservatoryShell*>(started.shell.get()), nullptr)
        << "the shell made with nothing named is not the Observatory shell";
    EXPECT_EQ(store->selectOptional(key), std::nullopt) << "the start wrote the default into the store";

    const an::Status shut = an::ui::stopApplication(std::move(started.shell), session);
    EXPECT_TRUE(shut.ok()) << shut.message();
    EXPECT_EQ(an::ui::appContext(), nullptr);
}

// The application registers one shell, the Observatory shell: the one every run starts, drawn in the Observatory
// profile, and the only one the Interface page's Shell row could list - so the row hides itself.
TEST(ApplicationStart, OnlyTheObservatoryShellIsRegistered)
{
    ShellRegistry registry;
    an::ui::registerApplicationShells(registry);

    EXPECT_EQ(registry.ids().join(QStringLiteral(", ")).toStdString(), "observatory");
    const ShellDescriptor* const observatory = registry.find(QStringLiteral("observatory"));
    ASSERT_NE(observatory, nullptr);
    EXPECT_EQ(observatory->styleProfile, an::ui::StyleProfile::Observatory);
    EXPECT_EQ(observatory->displayName, QStringLiteral("Observatory"));
}

// The shell's descriptor makes it over a core that is initialized and started: the application context is made and
// installed, the session is open, the installed extensions are known, and no shell is the core's yet.
TEST(ApplicationStart, TheCoreIsStartedBeforeAWindowIsMade)
{
    ASSERT_EQ(an::ui::appContext(), nullptr);
    an::ui::ApplicationSession session;
    ApplicationCore core;
    core.setApplicationSession(&session);
    WhenMade seen;
    ShellRegistry registry;
    ASSERT_TRUE(registry.add(recording(an::ui::observatory::observatoryShellDescriptor(), core, session, seen)));

    an::ui::StartedApplication started =
        an::ui::startApplication(core, registry, {QStringLiteral("AfterNight")}, QProcessEnvironment(), {});

    ASSERT_NE(started.shell, nullptr);
    ASSERT_NE(started.choice.descriptor, nullptr);
    EXPECT_EQ(started.choice.descriptor->id, QStringLiteral("observatory"));
    EXPECT_TRUE(seen.made);
    EXPECT_TRUE(seen.initialized) << "the shell was made over a core not yet initialized";
    EXPECT_TRUE(seen.started) << "the shell was made over a core not yet started";
    EXPECT_TRUE(seen.contextInstalled) << "the shell was made before the application context existed";
    EXPECT_TRUE(seen.sessionOpen) << "the shell was made before the session was open";
    EXPECT_TRUE(seen.noShellYet) << "a shell was the core's before the chosen one was made";
    EXPECT_EQ(an::ui::appContext()->getMainWindow(), started.shell->mainWindow());

    const an::Status shut = an::ui::stopApplication(std::move(started.shell), session);
    EXPECT_TRUE(shut.ok()) << shut.message();
    EXPECT_EQ(an::ui::appContext(), nullptr);
}

namespace
{

/// Sees the first paint of a widget.
class FirstPaint final : public QObject
{
public:
    bool painted = false;

protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (event != nullptr && event->type() == QEvent::Paint)
        {
            painted = true;
        }
        return QObject::eventFilter(watched, event);
    }
};

/// The milliseconds from the call of the start to the first paint of the shell's window once it is shown.
[[nodiscard]] double startToFirstPaint()
{
    an::ui::ApplicationSession session;
    ApplicationCore core;
    core.setApplicationSession(&session);
    ShellRegistry registry;
    an::ui::registerApplicationShells(registry);
    core.setShellRegistry(&registry);

    QElapsedTimer timer;
    timer.start();
    an::ui::StartedApplication started =
        an::ui::startApplication(core, registry, {QStringLiteral("AfterNight")}, QProcessEnvironment(), {});
    QWidget* const window = started.shell->mainWindow();
    FirstPaint first;
    window->installEventFilter(&first);
    window->setAttribute(Qt::WA_DontShowOnScreen, true);
    window->resize(1920, 1080);
    window->show();
    while (!first.painted && timer.elapsed() < 30000)
    {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
    }
    const double elapsed = static_cast<double>(timer.nsecsElapsed()) / 1.0e6;
    window->removeEventFilter(&first);
    (void)window->close();
    (void)an::ui::stopApplication(std::move(started.shell), session);
    return first.painted ? elapsed : -1.0;
}

} // namespace

// The time from the call of the application's start to the first paint of the Observatory window, the median of seven
// rounds. Measured, never asserted, and only when AFTERNIGHT_TEST_TIMINGS=1 asks for it.
TEST(Timing, StartToFirstPaint)
{
    if (qEnvironmentVariable("AFTERNIGHT_TEST_TIMINGS") != QStringLiteral("1"))
    {
        GTEST_SKIP() << "a time is measured only when AFTERNIGHT_TEST_TIMINGS=1 asks for it";
    }
    // A first start warms what any start loads once for the process.
    (void)startToFirstPaint();

    constexpr int kRounds = 7;
    std::vector<double> rounds;
    for (int round = 0; round < kRounds; ++round)
    {
        rounds.push_back(startToFirstPaint());
    }
    std::sort(rounds.begin(), rounds.end());
    const double medianMs = rounds[rounds.size() / 2];
    ASSERT_GT(rounds.front(), 0.0) << "the Observatory window was never painted";
    std::printf("\nStart to first paint, median of %d rounds: %.1f ms (%.1f to %.1f)\n", kRounds, medianMs,
                rounds.front(), rounds.back());
    ::testing::Test::RecordProperty("observatoryMs", std::to_string(medianMs));
}
