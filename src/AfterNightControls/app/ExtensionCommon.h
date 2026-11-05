// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

// Shared inline utilities used across the extension subsystem
// (ExtensionManager, ExtensionEnvironmentManager, ExtensionHostManager,
// SharedBufferManager, AfterNightExtensionHost).

#include <QUuid>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

namespace an::ui::extensions
{

// Generates a random UUID string suitable for session IDs, request IDs,
// buffer IDs, endpoint names, and similar opaque tokens.
inline std::string generateOpaqueToken()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
}

// Checks whether a filesystem path refers to an existing regular file.
// Uses error_code overloads so it never throws.
inline bool isRegularFile(const std::filesystem::path& path)
{
    std::error_code ec;
    return std::filesystem::exists(path, ec) && std::filesystem::is_regular_file(path, ec);
}

// Returns true if value starts with "http://" or "https://".
inline bool isUrlLike(const std::string& value)
{
    return value.rfind("https://", 0) == 0 || value.rfind("http://", 0) == 0;
}

// Reads the entire contents of a file into a std::string.
// Returns an empty string on failure.
inline std::string readFileText(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open())
    {
        return {};
    }

    std::ostringstream stream;
    stream << in.rdbuf();
    return stream.str();
}

inline std::filesystem::path extensionHostManagedRootPath()
{
    return std::filesystem::temp_directory_path() / "afternight-extension-host";
}

inline std::filesystem::path extensionHostSessionRootPath(const std::string_view session_id)
{
    return extensionHostManagedRootPath() / (session_id.empty() ? std::string("session") : std::string(session_id));
}

// The directory a full host lends its native window's preview frames from, named after the session: the buffers its
// frames reuse stay there while the window is open.
inline std::filesystem::path extensionHostPreviewFramesRootPath(const std::string_view session_id)
{
    return extensionHostSessionRootPath(std::string(session_id) + ".preview");
}

inline std::filesystem::path bundled_extension_host_executable_for_app_dir(const std::filesystem::path& app_dir)
{
#if defined(_WIN32)
    const std::vector<std::filesystem::path> candidates = {app_dir / "AfterNightExtensionHost_d.exe",
                                                           app_dir / "AfterNightExtensionHost.exe"};
#else
    const std::vector<std::filesystem::path> candidates = {app_dir / "AfterNightExtensionHost_d",
                                                           app_dir / "AfterNightExtensionHost"};
#endif

    for (const auto& candidate : candidates)
    {
        if (isRegularFile(candidate))
        {
            return candidate.lexically_normal();
        }
    }

    return {};
}

inline std::filesystem::path bundled_extension_host_lite_executable_for_app_dir(const std::filesystem::path& app_dir)
{
#if defined(_WIN32)
    const std::vector<std::filesystem::path> candidates = {app_dir / "AfterNightExtensionHostLite_d.exe",
                                                           app_dir / "AfterNightExtensionHostLite.exe"};
#else
    const std::vector<std::filesystem::path> candidates = {app_dir / "AfterNightExtensionHostLite_d",
                                                           app_dir / "AfterNightExtensionHostLite"};
#endif

    for (const auto& candidate : candidates)
    {
        if (isRegularFile(candidate))
        {
            return candidate.lexically_normal();
        }
    }

    return {};
}

} // namespace an::ui::extensions

// Constant-time byte comparison.  Returns true when the two byte sequences
// are equal.  The runtime is independent of the contents (safe for secrets).
// Defined outside the namespace to match the utility-header style.
namespace an::ui::extensions
{

inline bool constantTimeEqual(const void* a, const void* b, const std::size_t len) noexcept
{
    const auto* lhs = static_cast<const volatile std::uint8_t*>(a);
    const auto* rhs = static_cast<const volatile std::uint8_t*>(b);
    volatile std::uint8_t diff = 0;
    for (std::size_t i = 0; i < len; ++i)
    {
        diff |= lhs[i] ^ rhs[i];
    }
    return diff == 0;
}

inline bool constantTimeStringEqual(const std::string& a, const std::string& b) noexcept
{
    if (a.size() != b.size())
    {
        return false;
    }
    return constantTimeEqual(a.data(), b.data(), a.size());
}

} // namespace an::ui::extensions
