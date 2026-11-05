// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Where the suite writes: the application's local data is the harness's storage root, the process-wide temporary
/// directory - Qt's and the standard library's alike - is the short directory the harness owns and removes, and the
/// extension environments and shared buffers the application keeps live under the storage root, so nothing a case or
/// the code under test writes lands in the user's tree or is left in the system's temporary directory; and the
/// interface settings file, under the application's local data.

#include "fixtures/harness/TestHarness.h"

#include "app/ApplicationStorage.h"
#include "app/ExtensionEnvironmentManager.h"
#include "app/SharedBufferManager.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QString>

#include <gtest/gtest.h>

#include <filesystem>
#include <optional>
#include <string>
#include <system_error>

namespace
{

[[nodiscard]] std::filesystem::path normalized(const std::filesystem::path& path)
{
    std::error_code ec;
    const std::filesystem::path canonical = std::filesystem::weakly_canonical(path, ec);
    return (ec ? path : canonical).lexically_normal();
}

[[nodiscard]] std::filesystem::path pathOf(const QString& path)
{
    return std::filesystem::path(path.toStdWString());
}

} // namespace

// The application's local data is the storage root the harness set for the suite, and that root exists.
TEST(ApplicationStorageIsolation, UsesOwnedTemporaryDirectory)
{
    const std::optional<std::string> expected_root =
        an::tests::harness::environmentValue(an::tests::harness::kStorageRootVariable);
    ASSERT_TRUE(expected_root.has_value()) << "the harness sets the suite's storage root";

    const QString storage_root = an::ui::applicationLocalDataPath();
    EXPECT_TRUE(QDir::isAbsolutePath(storage_root));
    EXPECT_EQ(normalized(pathOf(storage_root)), normalized(std::filesystem::path(*expected_root)));
    EXPECT_TRUE(QFileInfo(storage_root).isDir());

    // The process-wide temporary directory is redirected to a directory the harness owns and removes, so every
    // temporary file the suite or the code under test writes goes with it rather than staying in the system's.
    const std::filesystem::path qt_temporary = pathOf(QDir::tempPath());
    EXPECT_EQ(qt_temporary.filename().string().rfind("ANtmp-", 0), 0u)
        << "the temporary directory was not redirected: " << qt_temporary.string();
    std::error_code ec;
    EXPECT_EQ(normalized(std::filesystem::temp_directory_path(ec)), normalized(qt_temporary))
        << "the standard library and Qt name different temporary directories";
    EXPECT_TRUE(std::filesystem::is_directory(qt_temporary, ec));

    // What the application keeps for extensions lives under the storage root.
    const std::filesystem::path storage_root_path = normalized(pathOf(storage_root));
    EXPECT_EQ(normalized(an::ui::extensions::user_extension_environments_root().parent_path()), storage_root_path);
    EXPECT_EQ(normalized(an::ui::extensions::SharedBufferManager::defaultSharedBufferRootPath().parent_path()),
              storage_root_path);
}

// The product's interface settings are a file under the application's local data - never the platform's native store,
// the registry on Windows, which no storage root redirects - so the suite's settings are the storage root's.
TEST(ApplicationSettingsFile, LivesUnderTheApplicationsLocalData)
{
    const QString path = an::ui::applicationSettingsFilePath();
    EXPECT_EQ(normalized(pathOf(QFileInfo(path).absolutePath())),
              normalized(pathOf(QDir(an::ui::applicationLocalDataPath()).filePath(QStringLiteral("settings")))));

    QSettings settings = an::ui::applicationSettings();
    EXPECT_EQ(settings.format(), QSettings::IniFormat);
    EXPECT_EQ(normalized(pathOf(settings.fileName())), normalized(pathOf(path)));

    settings.setValue(QStringLiteral("probe/value"), 7);
    settings.sync();
    EXPECT_EQ(settings.status(), QSettings::NoError);
    EXPECT_TRUE(QFileInfo::exists(path));
    settings.remove(QStringLiteral("probe"));
}
