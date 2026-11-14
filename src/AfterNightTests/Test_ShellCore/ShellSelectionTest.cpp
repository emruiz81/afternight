// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The choice of a shell at start: the command line, then the environment, then the setting, then the default, each
/// taken only when it names a registered shell.

#include "ShellCoreTestSupport.h"

#include "controls/settings/SettingsDefaults.h"
#include "controls/settings/SettingsKeys.h"
#include "core/ShellRegistry.h"
#include "core/ShellSelection.h"

#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QProcess>
#include <QProcessEnvironment>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>

namespace
{

using an::tests::shellcore::MemorySettings;
using an::tests::shellcore::registryOf;
using an::ui::shell::ShellChoice;
using an::ui::shell::ShellChoiceSource;
using an::ui::shell::ShellRegistry;

const QString kProgram = QStringLiteral("AfterNight");

// The variable that tells this executable, started again by the case below, that it is the started copy.
constexpr char kChildVariable[] = "AFTERNIGHT_TEST_SHELL_SELECTION_CHILD";

[[nodiscard]] QProcessEnvironment environmentNaming(const QString& id)
{
    QProcessEnvironment environment;
    environment.insert(QString::fromLatin1(an::ui::shell::ShellSelection::kEnvironmentVariable), id);
    return environment;
}

[[nodiscard]] QString chosenId(const ShellChoice& choice)
{
    return choice.descriptor != nullptr ? choice.descriptor->id : QString();
}

// The fixture carries the group's name, so the product's class is named in full inside it.
class ShellSelection : public ::testing::Test
{
protected:
    [[nodiscard]] ShellChoice resolve(const QStringList& arguments, const QProcessEnvironment& environment = {})
    {
        return an::ui::shell::ShellSelection::resolve(arguments, environment, &m_settings, m_registry);
    }

    void setShellSetting(const QString& id)
    {
        ASSERT_TRUE(m_settings.upsert(QString::fromLatin1(SettingsKeys::INTERFACE_SHELL), id));
    }

    ShellRegistry m_registry = registryOf({"classic", "observatory", "nebula"});
    MemorySettings m_settings;
};

// ---- The order: the command line, the environment, the setting, the default.

TEST_F(ShellSelection, TheCommandLineWinsOverTheEnvironmentAndTheSetting)
{
    setShellSetting(QStringLiteral("nebula"));

    const ShellChoice choice =
        resolve({kProgram, QStringLiteral("--shell=observatory")}, environmentNaming(QStringLiteral("classic")));

    EXPECT_EQ(chosenId(choice), QStringLiteral("observatory"));
    EXPECT_EQ(choice.source, ShellChoiceSource::CommandLine);
    EXPECT_TRUE(choice.warnings.isEmpty()) << choice.warnings.join(QLatin1Char('\n')).toStdString();
}

TEST_F(ShellSelection, TheEnvironmentWinsOverTheSetting)
{
    setShellSetting(QStringLiteral("nebula"));

    const ShellChoice choice = resolve({kProgram}, environmentNaming(QStringLiteral("observatory")));

    EXPECT_EQ(chosenId(choice), QStringLiteral("observatory"));
    EXPECT_EQ(choice.source, ShellChoiceSource::Environment);
    EXPECT_TRUE(choice.warnings.isEmpty());
}

TEST_F(ShellSelection, TheSettingWinsOverTheDefault)
{
    setShellSetting(QStringLiteral("nebula"));

    const ShellChoice choice = resolve({kProgram});

    EXPECT_EQ(chosenId(choice), QStringLiteral("nebula"));
    EXPECT_EQ(choice.source, ShellChoiceSource::Setting);
    EXPECT_TRUE(choice.warnings.isEmpty());
}

TEST_F(ShellSelection, TheDefaultIsTheClassicShell)
{
    EXPECT_STREQ(SettingsDefaults::INTERFACE_SHELL, "classic");

    const ShellChoice choice = resolve({kProgram});

    EXPECT_EQ(chosenId(choice), QStringLiteral("classic"));
    EXPECT_EQ(choice.source, ShellChoiceSource::Default);
    EXPECT_TRUE(choice.warnings.isEmpty());
}

TEST_F(ShellSelection, WithNoSettingsStoreTheSettingIsPassedOver)
{
    const ShellChoice choice =
        an::ui::shell::ShellSelection::resolve({kProgram}, QProcessEnvironment(), nullptr, m_registry);

    EXPECT_EQ(chosenId(choice), QStringLiteral("classic"));
    EXPECT_EQ(choice.source, ShellChoiceSource::Default);
    EXPECT_TRUE(choice.warnings.isEmpty());
}

// ---- A source that names no registered shell is passed over, with a warning.

TEST_F(ShellSelection, AnUnknownIdOnTheCommandLineFallsThroughWithAWarning)
{
    const ShellChoice choice =
        resolve({kProgram, QStringLiteral("--shell=aurora")}, environmentNaming(QStringLiteral("observatory")));

    EXPECT_EQ(chosenId(choice), QStringLiteral("observatory"));
    EXPECT_EQ(choice.source, ShellChoiceSource::Environment);
    ASSERT_EQ(choice.warnings.size(), 1);
    EXPECT_TRUE(choice.warnings.front().contains(QStringLiteral("--shell=aurora")))
        << choice.warnings.front().toStdString();
    EXPECT_TRUE(choice.warnings.front().contains(QStringLiteral("classic, observatory, nebula")))
        << choice.warnings.front().toStdString();
}

TEST_F(ShellSelection, AnUnknownIdInTheEnvironmentFallsThroughWithAWarning)
{
    setShellSetting(QStringLiteral("nebula"));

    const ShellChoice choice = resolve({kProgram}, environmentNaming(QStringLiteral("aurora")));

    EXPECT_EQ(chosenId(choice), QStringLiteral("nebula"));
    EXPECT_EQ(choice.source, ShellChoiceSource::Setting);
    ASSERT_EQ(choice.warnings.size(), 1);
    EXPECT_TRUE(choice.warnings.front().contains(QStringLiteral("AFTERNIGHT_SHELL")))
        << choice.warnings.front().toStdString();
    EXPECT_TRUE(choice.warnings.front().contains(QStringLiteral("aurora"))) << choice.warnings.front().toStdString();
}

TEST_F(ShellSelection, AnUnknownIdInTheSettingFallsThroughWithAWarning)
{
    setShellSetting(QStringLiteral("aurora"));

    const ShellChoice choice = resolve({kProgram});

    EXPECT_EQ(chosenId(choice), QStringLiteral("classic"));
    EXPECT_EQ(choice.source, ShellChoiceSource::Default);
    ASSERT_EQ(choice.warnings.size(), 1);
    EXPECT_TRUE(choice.warnings.front().contains(QStringLiteral("interface/shell")))
        << choice.warnings.front().toStdString();
    EXPECT_TRUE(choice.warnings.front().contains(QStringLiteral("aurora"))) << choice.warnings.front().toStdString();
}

TEST_F(ShellSelection, EverySourceThatFallsThroughLeavesItsOwnWarning)
{
    setShellSetting(QStringLiteral("lunar"));

    const ShellChoice choice =
        resolve({kProgram, QStringLiteral("--shell=aurora")}, environmentNaming(QStringLiteral("solar")));

    EXPECT_EQ(chosenId(choice), QStringLiteral("classic"));
    EXPECT_EQ(choice.source, ShellChoiceSource::Default);
    ASSERT_EQ(choice.warnings.size(), 3);
    EXPECT_TRUE(choice.warnings[0].contains(QStringLiteral("aurora")));
    EXPECT_TRUE(choice.warnings[1].contains(QStringLiteral("solar")));
    EXPECT_TRUE(choice.warnings[2].contains(QStringLiteral("lunar")));
}

TEST_F(ShellSelection, AShellOptionWithNoValueFallsThroughWithAWarning)
{
    for (const QString& option : {QStringLiteral("--shell"), QStringLiteral("--shell="), QStringLiteral("--shell=  ")})
    {
        const ShellChoice choice = resolve({kProgram, option}, environmentNaming(QStringLiteral("observatory")));

        EXPECT_EQ(chosenId(choice), QStringLiteral("observatory")) << option.toStdString();
        EXPECT_EQ(choice.source, ShellChoiceSource::Environment) << option.toStdString();
        ASSERT_EQ(choice.warnings.size(), 1) << option.toStdString();
        EXPECT_TRUE(choice.warnings.front().contains(QStringLiteral("no value")))
            << choice.warnings.front().toStdString();
    }
}

TEST_F(ShellSelection, TheValueIsJoinedToTheOptionAndTheNextArgumentIsNotRead)
{
    const ShellChoice choice = resolve({kProgram, QStringLiteral("--shell"), QStringLiteral("observatory")});

    EXPECT_EQ(chosenId(choice), QStringLiteral("classic"));
    EXPECT_EQ(choice.source, ShellChoiceSource::Default);
    ASSERT_EQ(choice.warnings.size(), 1);
    EXPECT_TRUE(choice.warnings.front().contains(QStringLiteral("--shell=<id>")))
        << choice.warnings.front().toStdString();
}

TEST_F(ShellSelection, NothingIsChosenWhenNoSourceNamesARegisteredShell)
{
    m_registry = registryOf({"observatory"});
    setShellSetting(QStringLiteral("aurora"));

    const ShellChoice choice = resolve({kProgram});

    EXPECT_EQ(choice.descriptor, nullptr);
    ASSERT_EQ(choice.warnings.size(), 2);
    EXPECT_TRUE(choice.warnings[1].contains(QStringLiteral("classic"))) << choice.warnings[1].toStdString();
}

// ---- What is read, and how.

TEST_F(ShellSelection, TheFirstArgumentIsTheProgramAndNeverAnOption)
{
    const ShellChoice choice = resolve({QStringLiteral("--shell=observatory")});

    EXPECT_EQ(chosenId(choice), QStringLiteral("classic"));
    EXPECT_EQ(choice.source, ShellChoiceSource::Default);
    EXPECT_TRUE(choice.warnings.isEmpty());

    EXPECT_EQ(chosenId(resolve({})), QStringLiteral("classic"));
}

TEST_F(ShellSelection, TheFirstShellOptionIsTakenAndALaterOneIgnoredWithAWarning)
{
    const ShellChoice choice =
        resolve({kProgram, QStringLiteral("--shell=observatory"), QStringLiteral("--shell=nebula")});

    EXPECT_EQ(chosenId(choice), QStringLiteral("observatory"));
    EXPECT_EQ(choice.source, ShellChoiceSource::CommandLine);
    ASSERT_EQ(choice.warnings.size(), 1);
    EXPECT_TRUE(choice.warnings.front().contains(QStringLiteral("--shell=nebula")))
        << choice.warnings.front().toStdString();
}

TEST_F(ShellSelection, OtherArgumentsAreLeftAlone)
{
    const ShellChoice choice = resolve({kProgram, QStringLiteral("M31.fits"), QStringLiteral("--shells=observatory"),
                                        QStringLiteral("-shell=observatory"), QStringLiteral("--SHELL=observatory"),
                                        QStringLiteral("-platform"), QStringLiteral("windows")});

    EXPECT_EQ(chosenId(choice), QStringLiteral("classic"));
    EXPECT_EQ(choice.source, ShellChoiceSource::Default);
    EXPECT_TRUE(choice.warnings.isEmpty()) << choice.warnings.join(QLatin1Char('\n')).toStdString();
}

TEST_F(ShellSelection, AnEmptyEnvironmentValueOrSettingIsNoChoice)
{
    setShellSetting(QStringLiteral("  "));

    const ShellChoice choice = resolve({kProgram}, environmentNaming(QString()));

    EXPECT_EQ(chosenId(choice), QStringLiteral("classic"));
    EXPECT_EQ(choice.source, ShellChoiceSource::Default);
    EXPECT_TRUE(choice.warnings.isEmpty()) << choice.warnings.join(QLatin1Char('\n')).toStdString();
}

TEST_F(ShellSelection, AValueIsReadWithoutTheSpacesAroundIt)
{
    EXPECT_EQ(chosenId(resolve({kProgram, QStringLiteral("--shell= nebula ")})), QStringLiteral("nebula"));
    // "set AFTERNIGHT_SHELL=observatory " keeps the trailing space on Windows.
    EXPECT_EQ(chosenId(resolve({kProgram}, environmentNaming(QStringLiteral("observatory ")))),
              QStringLiteral("observatory"));
    setShellSetting(QStringLiteral(" nebula"));
    EXPECT_EQ(chosenId(resolve({kProgram})), QStringLiteral("nebula"));
}

TEST_F(ShellSelection, AnIdIsMatchedAsItIsWritten)
{
    const ShellChoice choice = resolve({kProgram, QStringLiteral("--shell=Observatory")});

    EXPECT_EQ(chosenId(choice), QStringLiteral("classic"));
    ASSERT_EQ(choice.warnings.size(), 1);
    EXPECT_TRUE(choice.warnings.front().contains(QStringLiteral("Observatory")));
}

// ---- The arguments are the ones handed to it. The application hands it main's own: on Windows QApplication is
// given a list of its own, so what QCoreApplication::arguments() holds is not what the user typed. This executable
// is started again with the option on its own command line, and the copy proves the option is not read from there.

TEST_F(ShellSelection, ReadsOnlyTheArgumentsItIsHanded)
{
    QTemporaryDir childStorage;
    ASSERT_TRUE(childStorage.isValid());

    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QString::fromLatin1(kChildVariable), QStringLiteral("1"));
    environment.insert(QStringLiteral("AFTERNIGHT_TEST_STORAGE_ROOT"), childStorage.path());
    environment.remove(QString::fromLatin1(an::ui::shell::ShellSelection::kEnvironmentVariable));

    QProcess child;
    child.setProcessEnvironment(environment);
    child.setProcessChannelMode(QProcess::MergedChannels);
    child.start(QCoreApplication::applicationFilePath(),
                {QStringLiteral("--gtest_also_run_disabled_tests"),
                 QStringLiteral("--gtest_filter=ShellSelection.DISABLED_OwnCommandLineIsNotRead"),
                 QStringLiteral("--shell=observatory")});
    ASSERT_TRUE(child.waitForStarted(30000)) << child.errorString().toStdString();
    ASSERT_TRUE(child.waitForFinished(120000)) << child.errorString().toStdString();

    const QString output = QString::fromLocal8Bit(child.readAll());
    EXPECT_EQ(child.exitStatus(), QProcess::NormalExit) << output.toStdString();
    EXPECT_EQ(child.exitCode(), 0) << output.toStdString();
    EXPECT_TRUE(output.contains(QStringLiteral("[  PASSED  ] 1 test"))) << output.toStdString();
}

// Run only by the case above, in the copy it starts.
TEST_F(ShellSelection, DISABLED_OwnCommandLineIsNotRead)
{
    if (!qEnvironmentVariableIsSet(kChildVariable))
    {
        GTEST_SKIP() << "run by ShellSelection.ReadsOnlyTheArgumentsItIsHanded in a copy of this executable";
    }
    ASSERT_TRUE(QCoreApplication::arguments().contains(QStringLiteral("--shell=observatory")))
        << QCoreApplication::arguments().join(QLatin1Char(' ')).toStdString();

    const ShellRegistry registry = registryOf({"classic", "observatory"});
    const ShellChoice choice = an::ui::shell::ShellSelection::resolve({QCoreApplication::applicationFilePath()},
                                                                      QProcessEnvironment(), nullptr, registry);

    EXPECT_EQ(chosenId(choice), QStringLiteral("classic"));
    EXPECT_EQ(choice.source, ShellChoiceSource::Default);
}

} // namespace
