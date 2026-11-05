// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The Linux deployment guard, run rather than read: with a stand-in `pgrep` and `readlink` ahead of the system's on
/// the path, the guard lets a deployment through when no AfterNight process runs and refuses it, saying so, when one
/// does. Each case's stand-ins live in a scratch directory of its own, removed with the case.

#include <gtest/gtest.h>

#ifndef _WIN32
#include <QByteArray>
#include <QProcess>
#include <QProcessEnvironment>
#include <QString>
#include <QTemporaryDir>
#endif

#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

namespace
{

#ifndef _WIN32
/// The checkout the suite runs in: the first directory above the working directory that holds the guard.
[[nodiscard]] std::filesystem::path findRepoRoot()
{
    std::error_code ec;
    std::filesystem::path current = std::filesystem::current_path(ec);
    while (!ec && !current.empty())
    {
        if (std::filesystem::exists(current / "build" / "scripts" / "linux" / "_ensure_afternight_not_running.sh", ec))
        {
            return current;
        }
        if (current == current.root_path())
        {
            break;
        }
        current = current.parent_path();
    }
    return {};
}

void writeTextFile(const std::filesystem::path& path, const std::string& text)
{
    std::ofstream out(path, std::ios::binary);
    out << text;
}

struct ProcessResult
{
    bool finished = false;
    QProcess::ExitStatus exitStatus = QProcess::CrashExit;
    int exitCode = -1;
    QByteArray stdOut;
    QByteArray stdErr;
};

ProcessResult runGuardWithFakePgrep(const std::filesystem::path& guardScript, const std::filesystem::path& tempRoot,
                                    const std::string& fakePgrepBody, const std::string& fakeExecutableName = {})
{
    const auto fakeBin = tempRoot / "bin";
    std::filesystem::create_directories(fakeBin);

    const auto fakePgrep = fakeBin / "pgrep";
    writeTextFile(fakePgrep, "#!/bin/sh\n" + fakePgrepBody);
    std::filesystem::permissions(fakePgrep,
                                 std::filesystem::perms::owner_exec | std::filesystem::perms::owner_read |
                                     std::filesystem::perms::owner_write,
                                 std::filesystem::perm_options::add);

    if (!fakeExecutableName.empty())
    {
        const auto fakeReadlink = fakeBin / "readlink";
        const std::string fakeReadlinkBody = "#!/bin/sh\n"
                                             "if [ \"$1\" = \"/proc/12345/exe\" ]; then\n"
                                             "    echo \"/tmp/" +
                                             fakeExecutableName +
                                             "\"\n"
                                             "    exit 0\n"
                                             "fi\n"
                                             "exit 1\n";
        writeTextFile(fakeReadlink, fakeReadlinkBody);
        std::filesystem::permissions(fakeReadlink,
                                     std::filesystem::perms::owner_exec | std::filesystem::perms::owner_read |
                                         std::filesystem::perms::owner_write,
                                     std::filesystem::perm_options::add);
    }

    QProcess process;
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("PATH"),
               QString::fromStdString(fakeBin.string()) + QStringLiteral(":") + env.value(QStringLiteral("PATH")));
    process.setProcessEnvironment(env);
    process.setProgram(QString::fromStdString(guardScript.string()));
    process.start();
    ProcessResult result;
    result.finished = process.waitForFinished(10000);
    result.exitStatus = process.exitStatus();
    result.exitCode = process.exitCode();
    result.stdOut = process.readAllStandardOutput();
    result.stdErr = process.readAllStandardError();
    return result;
}
#endif

} // namespace

// With no AfterNight process running, the guard lets the deployment through.
TEST(LinuxDeployGuardScriptTest, AllowsDeployWhenNoAfterNightProcessesAreRunning)
{
#ifdef _WIN32
    GTEST_SKIP() << "Linux deploy guard is Linux-only";
#else
    const auto repoRoot = findRepoRoot();
    ASSERT_FALSE(repoRoot.empty()) << "Could not locate repository root";

    QTemporaryDir scratch;
    ASSERT_TRUE(scratch.isValid());
    ProcessResult process =
        runGuardWithFakePgrep(repoRoot / "build" / "scripts" / "linux" / "_ensure_afternight_not_running.sh",
                              std::filesystem::path(scratch.path().toStdString()), "exit 1\n");
    ASSERT_TRUE(process.finished) << process.stdOut.toStdString() << process.stdErr.toStdString();
    EXPECT_EQ(process.exitStatus, QProcess::NormalExit);
    EXPECT_EQ(process.exitCode, 0) << process.stdOut.toStdString() << process.stdErr.toStdString();
#endif
}

// With an AfterNight process running, the guard refuses the deployment and says why.
TEST(LinuxDeployGuardScriptTest, BlocksDeployWhenAfterNightProcessesAreRunning)
{
#ifdef _WIN32
    GTEST_SKIP() << "Linux deploy guard is Linux-only";
#else
    const auto repoRoot = findRepoRoot();
    ASSERT_FALSE(repoRoot.empty()) << "Could not locate repository root";

    QTemporaryDir scratch;
    ASSERT_TRUE(scratch.isValid());
    ProcessResult process =
        runGuardWithFakePgrep(repoRoot / "build" / "scripts" / "linux" / "_ensure_afternight_not_running.sh",
                              std::filesystem::path(scratch.path().toStdString()),
                              "if [ \"$2\" = \"AfterNight\" ]; then echo 12345; exit 0; fi\nexit 1\n", "AfterNight");
    ASSERT_TRUE(process.finished) << process.stdOut.toStdString() << process.stdErr.toStdString();
    EXPECT_EQ(process.exitStatus, QProcess::NormalExit);
    EXPECT_EQ(process.exitCode, 1) << process.stdOut.toStdString() << process.stdErr.toStdString();
    EXPECT_TRUE(process.stdErr.contains("refusing to deploy"));
#endif
}
