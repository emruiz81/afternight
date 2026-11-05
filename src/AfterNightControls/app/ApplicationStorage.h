// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>
#include <QString>
#include <QStringList>
#include <QtGlobal>

namespace an::ui
{

inline constexpr auto kTestStorageRootEnvironmentVariable = "AFTERNIGHT_TEST_STORAGE_ROOT";

[[nodiscard]] inline QString applicationLocalDataPath()
{
    const QString test_storage_root = qEnvironmentVariable(kTestStorageRootEnvironmentVariable).trimmed();
    if (!test_storage_root.isEmpty())
    {
        return QDir::cleanPath(test_storage_root);
    }

    return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
}

[[nodiscard]] inline QString applicationCachePath()
{
    const QString test_storage_root = qEnvironmentVariable(kTestStorageRootEnvironmentVariable).trimmed();
    if (!test_storage_root.isEmpty())
    {
        return QDir(test_storage_root).filePath(QStringLiteral("cache"));
    }

    QString path = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    return path.isEmpty() ? applicationLocalDataPath() : path;
}

// The file the product keeps its interface settings in - window sizes, panel layouts, recent lists, the log viewer's
// preferences - beside its other local data, rather than the platform's native store, which on Windows is the registry
// and which no storage root redirects.
[[nodiscard]] inline QString applicationSettingsFilePath()
{
    return QDir(applicationLocalDataPath()).filePath(QStringLiteral("settings/AfterNight.ini"));
}

// The product's interface settings. Every product source reads and writes them through this, never through a
// default-constructed QSettings or one named by organization and application, both of which are the native store.
[[nodiscard]] inline QSettings applicationSettings()
{
    return QSettings(applicationSettingsFilePath(), QSettings::IniFormat);
}

// The name a store of another format is set aside under, beside itself: its own name with the format it states - or
// "unversioned" for one that states none - before its extension, and a number after that when the name is taken. A
// settings database stating format 1, "afternight_data.db", is set aside as "afternight_data.format1.db", and a second
// one as "afternight_data.format1-2.db".
[[nodiscard]] inline QString setAsideStorePath(const QString& path, const int format)
{
    const QFileInfo info(path);
    const QString stem = info.completeBaseName();
    const QString extension = info.suffix().isEmpty() ? QString() : QStringLiteral(".") + info.suffix();
    const QString stated = format > 0 ? QStringLiteral("format%1").arg(format) : QStringLiteral("unversioned");
    const QDir directory = info.absoluteDir();
    QString candidate = directory.filePath(stem + QLatin1Char('.') + stated + extension);
    for (int number = 2; QFileInfo::exists(candidate); ++number)
    {
        candidate = directory.filePath(stem + QLatin1Char('.') + stated + QLatin1Char('-') + QString::number(number) +
                                       extension);
    }
    return candidate;
}

// Sets a store of another format aside under the name `setAsideStorePath` gives it, with the files SQLite keeps beside
// a database - its write-ahead log, its shared-memory index and its rollback journal - so the store moves whole. A
// store the application reads by itself is never read, written or deleted when it holds another format; it is set
// aside, and a new one is made in its place. Nothing is read or written here, and a store that cannot be moved whole is
// left where it was. Answers the path it was set aside at, or an empty string when it was not moved.
[[nodiscard]] inline QString setAsideStore(const QString& path, const int format)
{
    const QString target = setAsideStorePath(path, format);
    if (!QFile::rename(path, target))
    {
        return {};
    }
    QStringList moved;
    for (const QString& companion : {QStringLiteral("-wal"), QStringLiteral("-shm"), QStringLiteral("-journal")})
    {
        if (!QFileInfo::exists(path + companion))
        {
            continue;
        }
        if (!QFile::rename(path + companion, target + companion))
        {
            // What moved goes back, so the store is whole where it was.
            for (const QString& back : moved)
            {
                (void)QFile::rename(target + back, path + back);
            }
            (void)QFile::rename(target, path);
            return {};
        }
        moved << companion;
    }
    return target;
}

} // namespace an::ui
