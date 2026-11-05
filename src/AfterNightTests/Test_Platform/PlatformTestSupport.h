// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// What the platform suite's cases share: the storage root, a directory per case, and a capturing sink.
#pragma once

#include <AfterNightPlatform/Logging.h>

#include <gtest/gtest.h>
#include <spdlog/sinks/base_sink.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <mutex>
#include <string>
#include <system_error>
#include <vector>

namespace an::platform::test
{

/// The root every case writes below, which `main` creates or the runner supplies.
[[nodiscard]] inline std::filesystem::path storageRoot()
{
#ifdef _WIN32
    char* value = nullptr;
    size_t length = 0;
    std::filesystem::path root;
    if (_dupenv_s(&value, &length, "AFTERNIGHT_TEST_STORAGE_ROOT") == 0 && value != nullptr)
    {
        root = value;
    }
    std::free(value);
    return root;
#else
    const char* value = std::getenv("AFTERNIGHT_TEST_STORAGE_ROOT");
    return value != nullptr ? std::filesystem::path(value) : std::filesystem::path();
#endif
}

/// A fresh directory for the running case, below the storage root and named after the case.
[[nodiscard]] inline std::filesystem::path caseDirectory()
{
    const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
    const auto directory =
        storageRoot() / "cases" / (std::string(info->test_suite_name()) + "." + std::string(info->name()));
    std::error_code error;
    std::filesystem::remove_all(directory, error);
    std::filesystem::create_directories(directory, error);
    return directory;
}

/// A whole file as text, or an empty string when it cannot be read.
[[nodiscard]] inline std::string readFile(const std::filesystem::path& path)
{
    std::ifstream stream(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
}

/// Keeps the payload and level of every record handed to it, unformatted.
class CaptureSink final : public spdlog::sinks::base_sink<std::mutex>
{
public:
    [[nodiscard]] std::vector<std::string> messages()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return m_messages;
    }

    [[nodiscard]] std::vector<spdlog::level::level_enum> levels()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return m_levels;
    }

protected:
    void sink_it_(const spdlog::details::log_msg& message) override
    {
        m_messages.emplace_back(message.payload.data(), message.payload.size());
        m_levels.push_back(message.level);
    }

    void flush_() override {}

private:
    std::vector<std::string> m_messages;
    std::vector<spdlog::level::level_enum> m_levels;
};

} // namespace an::platform::test
