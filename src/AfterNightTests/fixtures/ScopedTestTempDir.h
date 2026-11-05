// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

// ScopedTestTempDir.h
// Redirects the process-wide temporary directory into a short, owned directory that is
// removed again when the guard goes out of scope.
//
// AFTERNIGHT_TEST_STORAGE_ROOT only redirects the application data/cache locations
// (see AfterNightControls/app/ApplicationStorage.h); it does not affect the system
// temporary directory. Everything the tests and the engine code under test write
// through QDir::tempPath() / std::filesystem::temp_directory_path() - registration
// spill files, calibrated-stack spill files, drizzle scratch data - therefore lands in
// the real user %TEMP% and is never cleaned up. Pointing TEMP, TMP and TMPDIR at a
// directory this guard owns puts all of it somewhere that is actually reclaimed.
//
// The target sits directly under the system temp directory with a deliberately short
// name rather than nested inside the storage root. Storage roots are long, and this
// codebase runs close enough to Windows' MAX_PATH that nesting temp inside one pushes
// deep paths -- virtual environment trees, and tar entry names bounded by the 100-byte
// ustar name field -- past their limits. The runner scripts sweep the same ANtmp-*
// pattern, so a crashed run's scratch is still reclaimed.
//
// IMPORTANT: construct this BEFORE any QCoreApplication/QApplication object. Qt
// resolves the temporary-directory location lazily and caches it, so a redirection
// performed after Qt has looked it up does not take effect.

#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <string>
#include <system_error>
#include <thread>

namespace an::test
{

/// RAII redirection of the process-wide temporary directory, restored on destruction.
/// Usable as a plain local in main(); non-copyable and non-movable.
class scoped_test_temp_dir
{
public:
    explicit scoped_test_temp_dir(const char* test_name)
    {
        std::error_code error;
        const std::filesystem::path base = std::filesystem::temp_directory_path(error);
        if (error || base.empty())
        {
            report_failure("locate the system temporary directory", base, error);
            return;
        }

        // Keep this name as short as uniqueness allows: every character here is subtracted
        // from the path budget of everything written underneath.
        const auto salt =
            static_cast<unsigned long long>(std::chrono::high_resolution_clock::now().time_since_epoch().count());
        for (int attempt = 0; attempt < 16; ++attempt)
        {
            const std::string directory_name =
                "ANtmp-" + std::to_string(salt % 100000ULL) + "-" + std::to_string(attempt);
            const std::filesystem::path candidate = base / directory_name;

            std::error_code create_error;
            if (std::filesystem::create_directory(candidate, create_error))
            {
                m_owns_directory = true;
                apply(candidate);
                return;
            }
        }

        std::fprintf(stderr, "ScopedTestTempDir: failed to create a private temporary directory for %s\n",
                     test_name ? test_name : "the test suite");
    }

    ~scoped_test_temp_dir()
    {
        restore_environment();

        if (!m_owns_directory || m_path.empty())
        {
            return;
        }

        // Child processes the suite spawned (python, extension hosts) may still be exiting,
        // and Windows keeps a file undeletable until the last handle closes. A blocked
        // remove_all aborts at the first locked entry and so costs almost nothing, which
        // makes a generous retry window cheap: only the attempt that actually succeeds does
        // the real work. If every attempt loses the race the tree is reported, and the
        // runner scripts reclaim it by its ANtmp-* name on a later run.
        std::error_code error;
        for (int attempt = 0; attempt < 12; ++attempt)
        {
            if (attempt > 0)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(250 * attempt));
            }

            error.clear();
            std::filesystem::remove_all(removable_path(m_path), error);
            if (!error && !std::filesystem::exists(m_path))
            {
                return;
            }
        }

        report_failure("remove the test temporary directory", m_path, error);
    }

    scoped_test_temp_dir(const scoped_test_temp_dir&) = delete;
    scoped_test_temp_dir& operator=(const scoped_test_temp_dir&) = delete;
    scoped_test_temp_dir(scoped_test_temp_dir&&) = delete;
    scoped_test_temp_dir& operator=(scoped_test_temp_dir&&) = delete;

    /// True when the temporary directory was created and the environment redirected.
    [[nodiscard]] bool valid() const noexcept
    {
        return !m_path.empty();
    }

    [[nodiscard]] const std::filesystem::path& path() const noexcept
    {
        return m_path;
    }

private:
#ifdef _WIN32
    using environment_name = const wchar_t*;
    static constexpr std::array<environment_name, 3> kNames = {L"TEMP", L"TMP", L"TMPDIR"};
    using environment_value = std::wstring;
#else
    using environment_name = const char*;
    static constexpr std::array<environment_name, 3> kNames = {"TEMP", "TMP", "TMPDIR"};
    using environment_value = std::string;
#endif

    void apply(const std::filesystem::path& path)
    {
#ifdef _WIN32
        const std::wstring value = path.wstring();
        for (size_t index = 0; index < kNames.size(); ++index)
        {
            wchar_t* previous = nullptr;
            size_t previous_length = 0;
            if (_wdupenv_s(&previous, &previous_length, kNames[index]) == 0 && previous != nullptr)
            {
                m_previous[index] = previous;
            }
            std::free(previous);

            if (_wputenv_s(kNames[index], value.c_str()) != 0)
            {
                std::fprintf(stderr, "ScopedTestTempDir: failed to redirect the temporary-directory environment\n");
                restore_environment();
                return;
            }
        }
#else
        const std::string value = path.string();
        for (size_t index = 0; index < kNames.size(); ++index)
        {
            if (const char* previous = std::getenv(kNames[index]))
            {
                m_previous[index] = previous;
            }

            if (setenv(kNames[index], value.c_str(), 1) != 0)
            {
                std::fprintf(stderr, "ScopedTestTempDir: failed to redirect the temporary-directory environment\n");
                restore_environment();
                return;
            }
        }
#endif
        m_path = path;
    }

    void restore_environment() noexcept
    {
        for (size_t index = 0; index < kNames.size(); ++index)
        {
#ifdef _WIN32
            // An empty value removes the variable from the Windows environment block.
            _wputenv_s(kNames[index], m_previous[index] ? m_previous[index]->c_str() : L"");
#else
            if (m_previous[index])
            {
                setenv(kNames[index], m_previous[index]->c_str(), 1);
            }
            else
            {
                unsetenv(kNames[index]);
            }
#endif
            m_previous[index].reset();
        }
    }

    // Windows refuses path operations past MAX_PATH unless the path carries the \\?\ prefix.
    // The trees under here (Python virtual environments with PySide6) go well past 260
    // characters, and std::filesystem::remove_all fails on them without it -- which is why
    // these directories have historically survived every attempt to clean them up.
    [[nodiscard]] static std::filesystem::path removable_path(const std::filesystem::path& path)
    {
#ifdef _WIN32
        std::error_code ec;
        std::filesystem::path absolute = std::filesystem::absolute(path, ec);
        if (ec)
        {
            absolute = path;
        }

        std::wstring native = absolute.make_preferred().wstring();
        if (native.rfind(L"\\\\?\\", 0) == 0)
        {
            return absolute;
        }
        return std::filesystem::path(L"\\\\?\\" + native);
#else
        return path;
#endif
    }

    static void report_failure(const char* action, const std::filesystem::path& path, const std::error_code& error)
    {
        std::fprintf(stderr, "ScopedTestTempDir: failed to %s '%s'%s%s\n", action, path.string().c_str(),
                     error ? ": " : "", error ? error.message().c_str() : "");
    }

    std::filesystem::path m_path;
    std::array<std::optional<environment_value>, 3> m_previous;
    bool m_owns_directory = false;
};

} // namespace an::test
