// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The Windows deployment of the bundled extensions, run rather than read: the script copies every extension directory
/// of a checkout into the configuration's binaries and removes a package the checkout no longer holds. It runs over a
/// copy of the script inside a scratch checkout of the case's own, removed with the case. The dependency deployment
/// scripts are read: every library copy they make fails the step when it fails.

#include <gtest/gtest.h>

#ifdef _WIN32
#include <QProcess>
#include <QString>
#include <QTemporaryDir>
#endif

#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>

namespace
{

#ifdef _WIN32
/// The checkout the suite runs in: the first directory above the working directory that holds the deployment script.
[[nodiscard]] std::filesystem::path findRepoRoot()
{
    std::error_code ec;
    std::filesystem::path current = std::filesystem::current_path(ec);
    while (!ec && !current.empty())
    {
        if (std::filesystem::exists(current / "build" / "scripts" / "msvc" / "deploy_extensions.bat", ec))
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
#endif

} // namespace

// Every library the binaries import is required: each vcpkg and KDDockWidgets copy the dependency deployment scripts
// make fails the step when it fails, so a port the triplet does not hold stops the deployment rather than the first
// program that loads it.
TEST(MsvcDeployDependenciesScriptTest, EveryLibraryCopyFailsTheStepWhenItFails)
{
#ifndef _WIN32
    GTEST_SKIP() << "MSVC batch deployment is Windows-only";
#else
    const auto repo_root = findRepoRoot();
    ASSERT_FALSE(repo_root.empty()) << "Could not locate repository root";
    for (const char* name : {"04a-deploy_deps_release.bat", "04b-deploy_deps_debug.bat"})
    {
        std::ifstream in(repo_root / "build" / "scripts" / "msvc" / name, std::ios::binary);
        ASSERT_TRUE(in) << name;
        int copies = 0;
        std::string line;
        while (std::getline(in, line))
        {
            if (!line.empty() && line.back() == '\r')
            {
                line.pop_back();
            }
            const std::size_t first = line.find_first_not_of(" \t");
            if (first == std::string::npos)
            {
                continue;
            }
            const std::string_view text(line.data() + first, line.size() - first);
            if (!text.starts_with("xcopy /Y ..\\..\\..\\vcpkg\\") &&
                !text.starts_with("xcopy /Y ..\\..\\..\\kddockwidgets\\"))
            {
                continue;
            }
            ++copies;
            EXPECT_TRUE(text.ends_with("|| exit /b 1")) << name << " copies a library without failing on it: " << text;
        }
        EXPECT_GT(copies, 0) << name << " copies no vcpkg or KDDockWidgets library";
    }
#endif
}

// The script deploys every extension directory of the checkout into the named configuration's binaries and removes a
// deployed package the checkout no longer holds.
TEST(MsvcDeployExtensionsScriptTest, CopiesBundledExtensionDirectoriesForExplicitConfig)
{
#ifndef _WIN32
    GTEST_SKIP() << "MSVC batch deployment is Windows-only";
#else
    const auto repo_root = findRepoRoot();
    ASSERT_FALSE(repo_root.empty()) << "Could not locate repository root";

    const auto script_source = repo_root / "build" / "scripts" / "msvc" / "deploy_extensions.bat";
    ASSERT_TRUE(std::filesystem::exists(script_source)) << script_source.string();

    QTemporaryDir scratch;
    ASSERT_TRUE(scratch.isValid());
    const std::filesystem::path temp_root(scratch.path().toStdWString());
    const auto temp_script_dir = temp_root / "build" / "scripts" / "msvc";
    const auto temp_script = temp_script_dir / "deploy_extensions.bat";

    std::filesystem::create_directories(temp_script_dir);
    std::filesystem::create_directories(temp_root / "bin" / "msvc" / "release");
    std::filesystem::create_directories(temp_root / "extensions" / "an_echo_demo");
    std::filesystem::create_directories(temp_root / "extensions" / "an_rt_preview_demo");
    std::filesystem::create_directories(temp_root / "bin" / "msvc" / "release" / "extensions" / "stale_package");
    writeTextFile(temp_root / "extensions" / "an_echo_demo" / "extension.json", "{}\n");
    writeTextFile(temp_root / "extensions" / "an_rt_preview_demo" / "extension.json", "{}\n");
    writeTextFile(temp_root / "bin" / "msvc" / "release" / "extensions" / "stale_package" / "extension.json", "{}\n");
    std::filesystem::copy_file(script_source, temp_script, std::filesystem::copy_options::overwrite_existing);

    QProcess process;
    process.setProgram(QStringLiteral("cmd.exe"));
    process.setArguments(
        {QStringLiteral("/c"), QString::fromStdWString(temp_script.wstring()), QStringLiteral("release")});
    process.start();
    ASSERT_TRUE(process.waitForFinished(10000))
        << process.readAllStandardOutput().toStdString() << process.readAllStandardError().toStdString();
    EXPECT_EQ(process.exitStatus(), QProcess::NormalExit);
    EXPECT_EQ(process.exitCode(), 0) << process.readAllStandardOutput().toStdString()
                                     << process.readAllStandardError().toStdString();

    EXPECT_TRUE(std::filesystem::exists(temp_root / "bin" / "msvc" / "release" / "extensions" / "an_echo_demo" /
                                        "extension.json"));
    EXPECT_TRUE(std::filesystem::exists(temp_root / "bin" / "msvc" / "release" / "extensions" / "an_rt_preview_demo" /
                                        "extension.json"));
    EXPECT_FALSE(std::filesystem::exists(temp_root / "bin" / "msvc" / "release" / "extensions" / "stale_package" /
                                         "extension.json"));
#endif
}
