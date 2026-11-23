// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// The application's start with either of its shells registered: the Observatory shell started when it is chosen and
// when nothing chooses a shell, the core initialized and started and the context made before a shell's window is, for
// either shell, and the time from the start to the first paint of each shell's window, measured.

#include "ClassicShell.h"
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
#include "fixtures/ScopedStyleProfile.h"
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

/// Puts the chosen shell's profile in force in a process whose widgets an earlier case made, as the suites do.
void forceProfile(const an::ui::StyleProfile profile)
{
    an::ui::testing::forceStyleProfile(profile);
}

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

// The Observatory shell, chosen on the command line among both registered shells, is started in the application's
// order: drawn in the Observatory profile, its window made over the core and built - Home shown, nothing shown on
// the screen - the context answering with it, and it goes before the session at the end, the context with it.
TEST(ApplicationStart, StartsTheObservatoryShell)
{
    ASSERT_EQ(an::ui::appContext(), nullptr);
    const an::ui::testing::ScopedStyleProfile restoreProfile(an::ui::styleProfile());

    an::ui::ApplicationSession session;
    ApplicationCore core;
    core.setApplicationSession(&session);
    ShellRegistry registry;
    ASSERT_TRUE(registry.add(an::ui::shell::classicShellDescriptor()));
    ASSERT_TRUE(registry.add(an::ui::observatory::observatoryShellDescriptor()));
    core.setShellRegistry(&registry);

    an::ui::StyleProfile styledWith = an::ui::StyleProfile::Classic;
    an::ui::StartedApplication started =
        an::ui::startApplication(core, registry, {QStringLiteral("AfterNight"), QStringLiteral("--shell=observatory")},
                                 QProcessEnvironment(), [&styledWith](const an::ui::StyleProfile profile) {
                                     styledWith = profile;
                                     forceProfile(profile);
                                 });

    ASSERT_NE(started.shell, nullptr);
    ASSERT_NE(started.choice.descriptor, nullptr);
    EXPECT_EQ(started.choice.descriptor->id, QStringLiteral("observatory"));
    EXPECT_EQ(started.choice.source, an::ui::shell::ShellChoiceSource::CommandLine);
    EXPECT_EQ(styledWith, an::ui::StyleProfile::Observatory);
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
    // The Interface page offers the choice: both registered shells are listed.
    const std::vector<an::ui::ShellOption> shells =
        an::ui::appContext()->getInterfaceSettingsRuntimeService()->availableShells();
    ASSERT_EQ(shells.size(), 2u);
    EXPECT_EQ(shells[0].id, QStringLiteral("classic"));
    EXPECT_EQ(shells[1].id, QStringLiteral("observatory"));

    const an::Status shut = an::ui::stopApplication(std::move(started.shell), session);
    EXPECT_TRUE(shut.ok()) << shut.message();
    EXPECT_EQ(an::ui::appContext(), nullptr);
}

// With nothing on the command line, in the environment or in the settings store, the application starts the
// Observatory shell, among both shells as the application registers them, and writes no choice into the store. The
// classic shell is still the one chosen where a source names it - the command line, the environment, or the setting the
// Interface page's Shell row writes - while it is registered.
TEST(ApplicationStart, DefaultIsObservatory)
{
    using an::ui::shell::ShellChoice;
    using an::ui::shell::ShellChoiceSource;

    ASSERT_EQ(an::ui::appContext(), nullptr);
    const an::ui::testing::ScopedStyleProfile restoreProfile(an::ui::styleProfile());

    an::ui::ApplicationSession session;
    ApplicationCore core;
    core.setApplicationSession(&session);
    ShellRegistry registry;
    ASSERT_TRUE(registry.add(an::ui::shell::classicShellDescriptor()));
    ASSERT_TRUE(registry.add(an::ui::observatory::observatoryShellDescriptor()));
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
    ASSERT_TRUE(store->remove(key));
    for (const auto& [choice, source] :
         {std::pair{byOption, ShellChoiceSource::CommandLine}, std::pair{byVariable, ShellChoiceSource::Environment},
          std::pair{bySetting, ShellChoiceSource::Setting}})
    {
        ASSERT_NE(choice.descriptor, nullptr);
        EXPECT_EQ(choice.descriptor->id, QStringLiteral("classic"));
        EXPECT_EQ(choice.source, source);
        EXPECT_EQ(choice.styleProfile, an::ui::StyleProfile::Classic);
    }

    an::ui::StyleProfile styledWith = an::ui::StyleProfile::Classic;
    an::ui::StartedApplication started = an::ui::startApplication(core, registry, {program}, QProcessEnvironment(),
                                                                  [&styledWith](const an::ui::StyleProfile profile) {
                                                                      styledWith = profile;
                                                                      forceProfile(profile);
                                                                  });

    ASSERT_NE(started.shell, nullptr);
    ASSERT_NE(started.choice.descriptor, nullptr);
    EXPECT_EQ(started.choice.descriptor->id, QStringLiteral("observatory"));
    EXPECT_EQ(started.choice.source, ShellChoiceSource::Default);
    EXPECT_TRUE(started.choice.warnings.isEmpty()) << started.choice.warnings.join(QLatin1Char('\n')).toStdString();
    EXPECT_EQ(styledWith, an::ui::StyleProfile::Observatory);
    EXPECT_NE(dynamic_cast<an::ui::observatory::ObservatoryShell*>(started.shell.get()), nullptr)
        << "the shell made with nothing named is not the Observatory shell";
    EXPECT_EQ(store->selectOptional(key), std::nullopt) << "the start wrote the default into the store";

    const an::Status shut = an::ui::stopApplication(std::move(started.shell), session);
    EXPECT_TRUE(shut.ok()) << shut.message();
    EXPECT_EQ(an::ui::appContext(), nullptr);
}

// Whichever shell is chosen, its descriptor makes it over a core that is initialized and started: the application
// context is made and installed, the session is open, the installed extensions are known, and no shell is the core's
// yet.
TEST(ApplicationStart, TheCoreIsStartedBeforeAWindowIsMade)
{
    const an::ui::testing::ScopedStyleProfile restoreProfile(an::ui::styleProfile());
    for (const QString id : {QStringLiteral("classic"), QStringLiteral("observatory")})
    {
        SCOPED_TRACE(id.toStdString());
        ASSERT_EQ(an::ui::appContext(), nullptr);
        an::ui::ApplicationSession session;
        ApplicationCore core;
        core.setApplicationSession(&session);
        WhenMade seen;
        ShellRegistry registry;
        ASSERT_TRUE(registry.add(recording(an::ui::shell::classicShellDescriptor(), core, session, seen)));
        ASSERT_TRUE(registry.add(recording(an::ui::observatory::observatoryShellDescriptor(), core, session, seen)));

        an::ui::StartedApplication started =
            an::ui::startApplication(core, registry, {QStringLiteral("AfterNight"), QStringLiteral("--shell=") + id},
                                     QProcessEnvironment(), forceProfile);

        ASSERT_NE(started.shell, nullptr);
        ASSERT_NE(started.choice.descriptor, nullptr);
        EXPECT_EQ(started.choice.descriptor->id, id);
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
[[nodiscard]] double startToFirstPaint(const QString& id)
{
    an::ui::ApplicationSession session;
    ApplicationCore core;
    core.setApplicationSession(&session);
    ShellRegistry registry;
    (void)registry.add(an::ui::shell::classicShellDescriptor());
    (void)registry.add(an::ui::observatory::observatoryShellDescriptor());
    core.setShellRegistry(&registry);

    QElapsedTimer timer;
    timer.start();
    an::ui::StartedApplication started =
        an::ui::startApplication(core, registry, {QStringLiteral("AfterNight"), QStringLiteral("--shell=") + id},
                                 QProcessEnvironment(), forceProfile);
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

// The time from the call of the application's start to the first paint of the shell's window, each shell in
// alternation, the median of seven rounds: the Observatory shell's within 10 % of the classic shell's. Measured, never
// asserted, and only when AFTERNIGHT_TEST_TIMINGS=1 asks for it.
TEST(Timing, StartToFirstPaint)
{
    if (qEnvironmentVariable("AFTERNIGHT_TEST_TIMINGS") != QStringLiteral("1"))
    {
        GTEST_SKIP() << "a time is measured only when AFTERNIGHT_TEST_TIMINGS=1 asks for it";
    }
    const an::ui::testing::ScopedStyleProfile restoreProfile(an::ui::styleProfile());
    // A first start of each warms what any start loads once for the process.
    (void)startToFirstPaint(QStringLiteral("classic"));
    (void)startToFirstPaint(QStringLiteral("observatory"));

    constexpr int kRounds = 7;
    std::vector<double> classic;
    std::vector<double> observatory;
    for (int round = 0; round < kRounds; ++round)
    {
        classic.push_back(startToFirstPaint(QStringLiteral("classic")));
        observatory.push_back(startToFirstPaint(QStringLiteral("observatory")));
    }
    const auto median = [](std::vector<double> values) {
        std::sort(values.begin(), values.end());
        return values[values.size() / 2];
    };
    const double classicMs = median(classic);
    const double observatoryMs = median(observatory);
    ASSERT_GT(classicMs, 0.0) << "the classic window was never painted";
    ASSERT_GT(observatoryMs, 0.0) << "the Observatory window was never painted";
    std::printf("\nStart to first paint, median of %d rounds in alternation: classic %.1f ms, Observatory %.1f ms "
                "(%+.1f %%)\n",
                kRounds, classicMs, observatoryMs, 100.0 * (observatoryMs - classicMs) / classicMs);
    ::testing::Test::RecordProperty("classicMs", std::to_string(classicMs));
    ::testing::Test::RecordProperty("observatoryMs", std::to_string(observatoryMs));
}
