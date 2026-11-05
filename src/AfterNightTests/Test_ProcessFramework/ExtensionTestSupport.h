// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// What the extension cases share: the application's settings kept in an in-memory database, a scratch directory under
/// the suite's temporary directory that is removed when the case ends - passing or failing, deep virtual environment
/// trees included - virtual environments made with a wait that lasts while they are being written, and the main
/// logger's recent records, read from its ring buffer rather than from a file.
#pragma once

#include "fixtures/InMemoryDatabase.h"

#include "controls/GlobalSettingsPersistence.h"

#include <AfterNightPlatform/Logging.h>

#include <QDir>
#include <QElapsedTimer>
#include <QProcess>
#include <QtGlobal>
#include <QString>
#include <QUuid>

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>

namespace an::tests::processes::framework
{

// Windows named-pipe IPC and QProcess::start() have significantly higher and
// more variable latency than Linux Unix domain sockets.  All timing constants
// in the IPC integration tests are multiplied by this factor so the tests
// remain deterministic on Windows without being unnecessarily slow on Linux.
// An instrumented host is slower at every step again, so a sanitizer build
// stretches every timing a further six times. What the leak sanitizer adds to a
// host's exit is longer than any multiple of an ordinary timing; it is the exit
// allowance below.
#ifdef _WIN32
inline constexpr int kIpcPlatformTimingMultiplier = 3;
#else
inline constexpr int kIpcPlatformTimingMultiplier = 1;
#endif
#if defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_THREAD__)
inline constexpr int kIpcTimingMultiplier = kIpcPlatformTimingMultiplier * 6;
#elif defined(__has_feature)
#if __has_feature(address_sanitizer) || __has_feature(thread_sanitizer)
inline constexpr int kIpcTimingMultiplier = kIpcPlatformTimingMultiplier * 6;
#else
inline constexpr int kIpcTimingMultiplier = kIpcPlatformTimingMultiplier;
#endif
#else
inline constexpr int kIpcTimingMultiplier = kIpcPlatformTimingMultiplier;
#endif

#if defined(__SANITIZE_ADDRESS__)
inline constexpr bool kIpcAddressSanitized = true;
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
inline constexpr bool kIpcAddressSanitized = true;
#else
inline constexpr bool kIpcAddressSanitized = false;
#endif
#else
inline constexpr bool kIpcAddressSanitized = false;
#endif

// What a host's exit may take beyond its ordinary time while leak detection is on. A host that ran an interpreter
// spends half a minute and more at exit under the leak sanitizer, reading its whole heap and symbolizing what CPython
// leaves before the suppressions set that aside - one that imported PySide6 past two minutes on bench-linux - which no
// multiplier of an ordinary timing reaches; nothing else makes an exit slow.
inline constexpr int kLeakCheckedHostExitMs = 300000;

// The time added to every wait that ends when a host exits - a host's inspection, a request a host answers as it ends,
// a session's end - and to no wait that holds its whole length to see that nothing happens. Leak detection is on in an
// address-sanitized build on Linux unless ASAN_OPTIONS turns it off; Microsoft's address sanitizer has none.
[[nodiscard]] inline int hostExitAllowanceMs()
{
#ifdef __linux__
    if (kIpcAddressSanitized && !qgetenv("ASAN_OPTIONS").contains("detect_leaks=0"))
    {
        return kLeakCheckedHostExitMs;
    }
#endif
    return 0;
}

// How long a host process a case starts itself - an inspection, a probe - is waited for: Qt's default, stretched as the
// IPC timings are, and the exit allowance.
[[nodiscard]] inline int hostProcessWaitMs()
{
    return 30000 * kIpcTimingMultiplier + hostExitAllowanceMs();
}

/// The application's global settings over an in-memory database of their own, with the table made.
class InMemorySettings
{
public:
    InMemorySettings() : m_settings(m_database.executor())
    {
        EXPECT_TRUE(m_database.isOpen());
        EXPECT_TRUE(m_settings.initializeTable());
    }

    InMemorySettings(const InMemorySettings&) = delete;
    InMemorySettings& operator=(const InMemorySettings&) = delete;

    [[nodiscard]] GlobalSettingsPersistence& repository() noexcept
    {
        return m_settings;
    }

private:
    InMemoryDatabase m_database;
    GlobalSettingsPersistence m_settings;
};

/// A path Windows accepts past `MAX_PATH`: a virtual environment's tree goes well past it, and a removal without the
/// prefix stops at the first entry it cannot name.
[[nodiscard]] inline std::filesystem::path removablePath(const std::filesystem::path& path)
{
#ifdef _WIN32
    std::error_code ec;
    std::filesystem::path absolute = std::filesystem::absolute(path, ec);
    if (ec)
    {
        absolute = path;
    }
    const std::wstring native = absolute.make_preferred().wstring();
    if (native.rfind(L"\\\\?\\", 0) == 0)
    {
        return absolute;
    }
    return std::filesystem::path(L"\\\\?\\" + native);
#else
    return path;
#endif
}

/// Removes a tree, retrying while a child process the case started may still hold a file in it. True when nothing is
/// left.
inline bool removeTree(const std::filesystem::path& path)
{
    if (path.empty())
    {
        return true;
    }
    std::error_code ec;
    for (int attempt = 0; attempt < 8; ++attempt)
    {
        if (attempt > 0)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(100 * attempt));
        }
        ec.clear();
        std::filesystem::remove_all(removablePath(path), ec);
        if (!std::filesystem::exists(path, ec))
        {
            return true;
        }
    }
    return false;
}

/// A directory of the case's own under the suite's temporary directory, removed when the case ends, passing or failing.
/// The suite's temporary directory is the harness's, so nothing lands in the system's.
class ScratchDirectory
{
public:
    explicit ScratchDirectory(std::string_view purpose = "scratch")
    {
        const QString name = QStringLiteral("an-%1-%2")
                                 .arg(QString::fromUtf8(purpose.data(), static_cast<qsizetype>(purpose.size())))
                                 .arg(QUuid::createUuid().toString(QUuid::Id128).left(8));
        m_path = std::filesystem::path(QDir(QDir::tempPath()).filePath(name).toStdWString());
        std::error_code ec;
        std::filesystem::create_directories(m_path, ec);
        EXPECT_FALSE(ec) << "the scratch directory " << m_path.string() << " could not be made: " << ec.message();
    }

    ~ScratchDirectory()
    {
        EXPECT_TRUE(removeTree(m_path)) << "the scratch directory " << m_path.string() << " was left behind";
    }

    ScratchDirectory(const ScratchDirectory&) = delete;
    ScratchDirectory& operator=(const ScratchDirectory&) = delete;

    [[nodiscard]] const std::filesystem::path& path() const noexcept
    {
        return m_path;
    }

private:
    std::filesystem::path m_path;
};

/// Removes a path when the case ends, passing or failing.
struct RemovePathOnExit
{
    std::filesystem::path path;
    ~RemovePathOnExit()
    {
        (void)removeTree(path);
    }
};

/// How long `python -m venv` may write nothing into the environment it makes before a case gives up on it. The tool
/// installs pip into the environment, which takes ten seconds on an idle machine and outlasts QProcess's default
/// 30 s on one a parallel build or another suite loads, so a case waits for as long as the tool keeps writing.
inline constexpr std::chrono::seconds kVirtualEnvironmentStallTimeout{120};

/// The longest `python -m venv` may run, writing or not.
inline constexpr std::chrono::minutes kVirtualEnvironmentTimeout{15};

/// What a tree holds, as a progress check compares it: how many entries and how many bytes of files. An entry that
/// goes while it is read is passed over.
[[nodiscard]] inline std::pair<std::uintmax_t, std::uintmax_t> treeFootprint(const std::filesystem::path& root)
{
    std::uintmax_t entries = 0u;
    std::uintmax_t bytes = 0u;
    std::error_code ec;
    for (std::filesystem::recursive_directory_iterator
             it(removablePath(root), std::filesystem::directory_options::skip_permission_denied, ec),
         end;
         !ec && it != end; it.increment(ec))
    {
        ++entries;
        std::error_code sizeError;
        if (it->is_regular_file(sizeError))
        {
            const std::uintmax_t size = it->file_size(sizeError);
            bytes += sizeError ? 0u : size;
        }
    }
    return {entries, bytes};
}

/// Makes a virtual environment with the interpreter stated, as `python -m venv` makes one, and fails the case when it
/// cannot. The wait lasts while the tool keeps writing the environment - checked every second - and fails only after
/// it has written nothing for `kVirtualEnvironmentStallTimeout`, or has run for `kVirtualEnvironmentTimeout`.
inline void createVirtualEnvironment(const std::filesystem::path& python, const std::filesystem::path& environment_root)
{
    QProcess process;
    process.setProgram(QString::fromStdString(python.string()));
    process.setArguments(
        {QStringLiteral("-m"), QStringLiteral("venv"), QString::fromStdString(environment_root.string())});
    process.start();
    ASSERT_TRUE(process.waitForStarted()) << "could not start " << python.string();

    QElapsedTimer running;
    running.start();
    QElapsedTimer quiet;
    quiet.start();
    std::pair<std::uintmax_t, std::uintmax_t> written = treeFootprint(environment_root);
    while (process.state() != QProcess::NotRunning && !process.waitForFinished(1000))
    {
        const std::pair<std::uintmax_t, std::uintmax_t> now = treeFootprint(environment_root);
        if (now != written)
        {
            written = now;
            quiet.restart();
        }
        const bool stalled = std::chrono::milliseconds(quiet.elapsed()) >= kVirtualEnvironmentStallTimeout;
        const bool overrun = std::chrono::milliseconds(running.elapsed()) >= kVirtualEnvironmentTimeout;
        if (stalled || overrun)
        {
            process.kill();
            process.waitForFinished();
            FAIL() << "python -m venv " << environment_root.string()
                   << (stalled ? " wrote nothing into the environment for " : " was still running after ")
                   << (stalled ? kVirtualEnvironmentStallTimeout.count()
                               : std::chrono::duration_cast<std::chrono::seconds>(kVirtualEnvironmentTimeout).count())
                   << " s; " << written.first << " entries were written";
        }
    }
    ASSERT_EQ(process.exitStatus(), QProcess::NormalExit);
    ASSERT_EQ(process.exitCode(), 0) << process.readAllStandardError().toStdString();
}

/// The variable that makes a full extension host missing beside the suite a failure rather than a skip.
inline constexpr const char* kRequireFullExtensionHostVariable = "AFTERNIGHT_TEST_REQUIRE_EXTENSION_HOST";

/// Whether the run asked for the full extension host to be present.
[[nodiscard]] inline bool fullExtensionHostRequired()
{
    return qEnvironmentVariableIntValue(kRequireFullExtensionHostVariable) == 1;
}

/// What a case that launches the full extension host says when the host is not deployed beside the suite.
inline constexpr const char* kFullExtensionHostAbsent =
    "the full extension host is not deployed beside this suite; set AFTERNIGHT_TEST_REQUIRE_EXTENSION_HOST=1 to make "
    "its absence a failure";

/// Whether a record the main logger kept in its ring buffer holds a fragment of text.
[[nodiscard]] inline bool mainLogContains(const std::string_view fragment)
{
    LOG_FLUSH;
    for (const auto& message : an::platform::recentLogMessages(0u))
    {
        const std::string_view payload(message.payload.data(), message.payload.size());
        if (payload.find(fragment) != std::string_view::npos)
        {
            return true;
        }
    }
    return false;
}

} // namespace an::tests::processes::framework

/// Ends a case that launches the full extension host when the host is not deployed beside the suite: as a skip, or as a
/// failure when the run asked for the host. `host_path` is the host the application directory names, empty when none.
#define AN_REQUIRE_FULL_EXTENSION_HOST(host_path)                                        \
    do                                                                                   \
    {                                                                                    \
        if ((host_path).empty() || !std::filesystem::exists(host_path))                  \
        {                                                                                \
            if (::an::tests::processes::framework::fullExtensionHostRequired())          \
            {                                                                            \
                FAIL() << ::an::tests::processes::framework::kFullExtensionHostAbsent;   \
            }                                                                            \
            GTEST_SKIP() << ::an::tests::processes::framework::kFullExtensionHostAbsent; \
        }                                                                                \
    } while (false)
