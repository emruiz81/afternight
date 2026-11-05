// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QUrl>
#include <QString>

#ifdef Q_OS_LINUX
#include <QProcessEnvironment>
#endif

#include <AfterNightPlatform/Logging.h>

namespace an::ui
{

// ---------------------------------------------------------------------------
// Internal: sanitised process launcher for Linux
// ---------------------------------------------------------------------------
#ifdef Q_OS_LINUX
namespace detail
{

/// Returns a QProcessEnvironment stripped of app-bundled Qt/library overrides.
/// This prevents xdg-open / dbus-send children from picking up AfterNight's
/// bundled Qt plugins or libraries instead of the system ones.
inline QProcessEnvironment cleanSystemEnvironment()
{
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.remove(QStringLiteral("QT_PLUGIN_PATH"));
    env.remove(QStringLiteral("QT_QPA_PLATFORMTHEME"));
    env.remove(QStringLiteral("LD_LIBRARY_PATH"));
    return env;
}

/// Launch a detached process with the sanitised environment.
inline bool startDetachedClean(const QString& program, const QStringList& arguments)
{
    QProcess process;
    process.setProcessEnvironment(cleanSystemEnvironment());
    process.setProgram(program);
    process.setArguments(arguments);
    return process.startDetached();
}

} // namespace detail
#endif

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

/// Opens a local file with the system's default handler.
///
/// On Linux/KDE, QDesktopServices::openUrl(QUrl::fromLocalFile(...)) routes
/// through the KDE platform plugin (KIO) which can reject file:// URLs for
/// certain MIME types — e.g. HTML files produce a "Could not read file ..."
/// error on stdout while still returning true.  We bypass Qt entirely on
/// Linux and launch xdg-open directly with a sanitised environment (no
/// bundled QT_PLUGIN_PATH / LD_LIBRARY_PATH that would confuse KIO).
inline bool openLocalFile(const QString& filePath)
{
    if (filePath.isEmpty())
    {
        return false;
    }

#ifdef Q_OS_LINUX
    LOG_DEBUG("openLocalFile: using xdg-open for '{}'", filePath.toStdString());
    return detail::startDetachedClean(QStringLiteral("xdg-open"), {filePath});
#else
    return QDesktopServices::openUrl(QUrl::fromLocalFile(filePath));
#endif
}

/// Opens a directory in the system file manager.
///
/// On Windows uses explorer.exe, on macOS uses `open`, on Linux uses
/// xdg-open with a sanitised environment.
inline bool openLocalDirectory(const QString& directoryPath)
{
    if (directoryPath.isEmpty())
    {
        return false;
    }

    const QString absPath = QDir(directoryPath).absolutePath();

#if defined(Q_OS_WIN)
    return QProcess::startDetached(QStringLiteral("explorer.exe"), {QDir::toNativeSeparators(absPath)});
#elif defined(Q_OS_LINUX)
    LOG_DEBUG("openLocalDirectory: using xdg-open for '{}'", absPath.toStdString());
    return detail::startDetachedClean(QStringLiteral("xdg-open"), {absPath});
#elif defined(Q_OS_MAC)
    return QProcess::startDetached(QStringLiteral("open"), {absPath});
#else
    return QDesktopServices::openUrl(QUrl::fromLocalFile(absPath));
#endif
}

/// Reveal a file or directory in the system file manager (select/highlight it).
///
/// On Windows uses `explorer.exe /select,`, on macOS uses `open -R`,
/// on Linux tries the freedesktop FileManager1 D-Bus interface first
/// and falls back to opening the containing directory via xdg-open.
inline bool revealInFileManager(const QString& filePath)
{
    if (filePath.isEmpty())
    {
        return false;
    }

    const QFileInfo fileInfo(filePath);

#if defined(Q_OS_WIN)
    return QProcess::startDetached(QStringLiteral("explorer.exe"),
                                   {QStringLiteral("/select,"), QDir::toNativeSeparators(fileInfo.absoluteFilePath())});
#elif defined(Q_OS_LINUX)
    // Try the freedesktop FileManager1 D-Bus interface to select the item.
    const QString dbusMethod = fileInfo.isDir() ? QStringLiteral("org.freedesktop.FileManager1.ShowFolders")
                                                : QStringLiteral("org.freedesktop.FileManager1.ShowItems");
    if (detail::startDetachedClean(
            QStringLiteral("dbus-send"),
            {QStringLiteral("--session"), QStringLiteral("--dest=org.freedesktop.FileManager1"),
             QStringLiteral("--type=method_call"), QStringLiteral("/org/freedesktop/FileManager1"), dbusMethod,
             QStringLiteral("array:string:file://%1").arg(fileInfo.absoluteFilePath()), QStringLiteral("string:")}))
    {
        return true;
    }
    // Fallback: open the containing directory (or the directory itself).
    const QString target = fileInfo.isDir() ? fileInfo.absoluteFilePath() : fileInfo.absolutePath();
    LOG_DEBUG("revealInFileManager: dbus-send failed, falling back to xdg-open for '{}'", target.toStdString());
    return detail::startDetachedClean(QStringLiteral("xdg-open"), {target});
#elif defined(Q_OS_MAC)
    return QProcess::startDetached(QStringLiteral("open"), {QStringLiteral("-R"), fileInfo.absoluteFilePath()});
#else
    return QDesktopServices::openUrl(QUrl::fromLocalFile(fileInfo.absolutePath()));
#endif
}

} // namespace an::ui
