// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/**
 * @file ScopedTempPath.h
 * @brief RAII ownership for temporary files and directories created by the test suites.
 *
 * The rules this header exists to enforce:
 *  - a temporary path is removed on the failing path (early ASSERT return, thrown exception) just
 *    as reliably as on the passing one, and
 *  - a removal that fails is reported instead of being swallowed into a discarded std::error_code.
 *
 * Deliberately Qt-free so the engine-only test executables can use it too.
 */

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace an::tests::fixtures
{

/// Removes @p path recursively and reports a failure rather than discarding the error.
inline void removeTempPathOrFail(const std::filesystem::path& path)
{
    if (path.empty())
    {
        return;
    }

    std::error_code exists_error;
    if (!std::filesystem::exists(path, exists_error))
    {
        return;
    }

    std::error_code remove_error;
    std::filesystem::remove_all(path, remove_error);
    if (remove_error)
    {
        ADD_FAILURE() << "Failed to remove test temporary path '" << path.string() << "': " << remove_error.message();
    }
}

/// A name no other process, and no other run of this process, can collide with.
[[nodiscard]] inline std::string uniqueTempName(const std::string& prefix)
{
    static std::atomic_uint64_t next_id{0};

    const auto stamp = static_cast<std::uint64_t>(std::chrono::high_resolution_clock::now().time_since_epoch().count());
    return prefix + "-" + std::to_string(stamp) + "-" + std::to_string(next_id.fetch_add(1, std::memory_order_relaxed));
}

/**
 * @brief Owns a temporary file or directory and removes it when the owner goes out of scope.
 *
 * Move-only, so ownership is always unambiguous.
 */
class ScopedTempPath final
{
public:
    ScopedTempPath() = default;

    explicit ScopedTempPath(std::filesystem::path path) : m_path(std::move(path)) {}

    ScopedTempPath(const ScopedTempPath&) = delete;
    ScopedTempPath& operator=(const ScopedTempPath&) = delete;

    ScopedTempPath(ScopedTempPath&& other) noexcept : m_path(std::exchange(other.m_path, std::filesystem::path{})) {}

    ScopedTempPath& operator=(ScopedTempPath&& other) noexcept
    {
        if (this != &other)
        {
            removeTempPathOrFail(m_path);
            m_path = std::exchange(other.m_path, std::filesystem::path{});
        }
        return *this;
    }

    ~ScopedTempPath()
    {
        removeTempPathOrFail(m_path);
    }

    [[nodiscard]] const std::filesystem::path& path() const
    {
        return m_path;
    }

    [[nodiscard]] std::filesystem::path filePath(const std::string& name) const
    {
        return m_path / name;
    }

    /// Gives up ownership without removing anything.
    std::filesystem::path release()
    {
        return std::exchange(m_path, std::filesystem::path{});
    }

private:
    std::filesystem::path m_path;
};

/// Creates a process-unique directory under the system temporary root and returns its owner.
[[nodiscard]] inline ScopedTempPath makeScopedTempDir(const std::string& prefix)
{
    std::error_code temp_root_error;
    const std::filesystem::path temp_root = std::filesystem::temp_directory_path(temp_root_error);
    if (temp_root_error)
    {
        ADD_FAILURE() << "Failed to resolve the system temporary directory: " << temp_root_error.message();
        return {};
    }

    const std::filesystem::path directory = temp_root / uniqueTempName(prefix);

    std::error_code create_error;
    std::filesystem::create_directories(directory, create_error);
    if (create_error)
    {
        ADD_FAILURE() << "Failed to create test temporary directory '" << directory.string()
                      << "': " << create_error.message();
        return {};
    }

    return ScopedTempPath(directory);
}

namespace detail
{

/// Removes the roots that individual tests cannot own once the last test in the process has run.
class TempPathCleanupEnvironment final : public ::testing::Environment
{
public:
    void add(std::filesystem::path path)
    {
        m_paths.push_back(std::move(path));
    }

    void TearDown() override
    {
        for (const auto& path : m_paths)
        {
            removeTempPathOrFail(path);
        }
        m_paths.clear();
    }

    ~TempPathCleanupEnvironment() override
    {
        // Last-resort net for the runs that never reach TearDown (--gtest_list_tests, a filter that
        // matches nothing). Silent by design: gtest is already tearing down at this point.
        for (const auto& path : m_paths)
        {
            std::error_code error;
            std::filesystem::remove_all(path, error);
        }
    }

private:
    std::vector<std::filesystem::path> m_paths;
};

[[nodiscard]] inline TempPathCleanupEnvironment& tempPathCleanupEnvironment()
{
    static TempPathCleanupEnvironment* environment = []() {
        auto* created = new TempPathCleanupEnvironment();
        ::testing::AddGlobalTestEnvironment(created);
        return created;
    }();
    return *environment;
}

} // namespace detail

/**
 * @brief Creates a process-unique directory that gtest removes after the last test in the process.
 *
 * For roots shared by many free TEST() bodies, where no single test can own the tree. The removal
 * runs whether the suite passed or failed; only a hard crash escapes it.
 */
[[nodiscard]] inline std::filesystem::path makeSuiteTempRoot(const std::string& prefix)
{
    ScopedTempPath directory = makeScopedTempDir(prefix);
    std::filesystem::path path = directory.release();
    if (!path.empty())
    {
        detail::tempPathCleanupEnvironment().add(path);
    }
    return path;
}

} // namespace an::tests::fixtures
