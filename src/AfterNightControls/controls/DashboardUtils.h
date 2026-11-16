// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file DashboardUtils.h
/// Lightweight, dependency-light helpers shared across dashboard-related code.
/// Keep includes minimal so that adding this header does not pull in heavy Qt or
/// project headers.

#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QFont>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QWidget>

#include "controls/DashboardPreferences.h"
#include "theme/AnDesignTokens.h"

#include <algorithm>
#include <cmath>
#include <deque>
#include <vector>

namespace DashboardUtils
{

/// Scale a base pixel value by a DPI factor, clamping to a minimum.
[[nodiscard]] inline int scaledPixels(qreal scale, int value, int minimum = 0)
{
    return (std::max)(minimum, static_cast<int>(std::lround(static_cast<double>(value) * scale)));
}

/// Return a system font scaled by @p scale with the given weight and style.
[[nodiscard]] inline QFont scaledSystemFont(const QWidget* widget, qreal scale, QFont::Weight weight = QFont::Normal,
                                            bool italic = false)
{
    const auto& typography = an::ui::tokens().typography;
    QFont font = typography.body;
    if (scale >= 1.35)
    {
        font = typography.heading2;
    }
    else if (scale >= 1.15)
    {
        font = typography.heading3;
    }
    else if (scale <= 0.9)
    {
        font = typography.caption;
    }

    if (widget)
    {
        font.setStyleHint(widget->font().styleHint());
    }
    font.setWeight(weight);
    font.setItalic(italic);
    return font;
}

/// Normalize a recent-file path: strip BOMs, trim, resolve file:// URLs,
/// and clean the resulting local path.  Returns an empty string on failure.
[[nodiscard]] inline QString normalizeRecentFilePath(const QString& inputPath)
{
    QString sanitizedPath = inputPath;
    while (!sanitizedPath.isEmpty() && sanitizedPath.front() == QChar::ByteOrderMark)
    {
        sanitizedPath.remove(0, 1);
    }

    QString trimmedPath = sanitizedPath.trimmed();
    while (!trimmedPath.isEmpty() && trimmedPath.front() == QChar::ByteOrderMark)
    {
        trimmedPath.remove(0, 1);
    }
    trimmedPath = trimmedPath.trimmed();
    if (trimmedPath.isEmpty())
    {
        return trimmedPath;
    }

    auto cleanLocalPath = [](const QString& localPath) -> QString {
        return localPath.isEmpty() ? localPath : QDir::cleanPath(localPath);
    };

    const QUrl parsedUrl(trimmedPath);
    if (parsedUrl.isValid() && parsedUrl.isLocalFile())
    {
        return cleanLocalPath(parsedUrl.toLocalFile());
    }

    if (trimmedPath.startsWith(QStringLiteral("file://")))
    {
        const QUrl userInputUrl = QUrl::fromUserInput(trimmedPath);
        if (userInputUrl.isValid() && userInputUrl.isLocalFile())
        {
            return cleanLocalPath(userInputUrl.toLocalFile());
        }
    }

    return cleanLocalPath(trimmedPath);
}

/// The recent files as the dashboard shows them: those whose path or name holds the search, case aside, in the sort's
/// order - most recent first, by name, or by type and then name - with the pinned files first, each group keeping that
/// order. The dashboard's grid and Home's recent files order them so.
///
/// @param recentFiles the recent files, most recent first.
/// @param search the search; an empty one keeps every file.
/// @param sortMode a `DashboardPreferences::RECENT_SORT_*`.
/// @param pinned the pinned files, normalized.
[[nodiscard]] inline QStringList visibleRecentFileOrder(const std::deque<QString>& recentFiles, const QString& search,
                                                        const int sortMode, const QSet<QString>& pinned)
{
    std::vector<QString> shown;
    shown.reserve(recentFiles.size());
    for (const QString& filePath : recentFiles)
    {
        if (search.isEmpty() || filePath.contains(search, Qt::CaseInsensitive) ||
            QFileInfo(filePath).fileName().contains(search, Qt::CaseInsensitive))
        {
            shown.push_back(filePath);
        }
    }

    if (sortMode == DashboardPreferences::RECENT_SORT_NAME)
    {
        std::stable_sort(shown.begin(), shown.end(), [](const QString& lhs, const QString& rhs) {
            return QString::localeAwareCompare(QFileInfo(lhs).fileName(), QFileInfo(rhs).fileName()) < 0;
        });
    }
    else if (sortMode == DashboardPreferences::RECENT_SORT_TYPE)
    {
        std::stable_sort(shown.begin(), shown.end(), [](const QString& lhs, const QString& rhs) {
            const QFileInfo left(lhs);
            const QFileInfo right(rhs);
            const QString leftSuffix = left.suffix().toLower();
            const QString rightSuffix = right.suffix().toLower();
            if (leftSuffix != rightSuffix)
            {
                return leftSuffix < rightSuffix;
            }
            return QString::localeAwareCompare(left.fileName(), right.fileName()) < 0;
        });
    }

    std::stable_partition(shown.begin(), shown.end(), [&pinned](const QString& filePath) {
        const QString normalized = normalizeRecentFilePath(filePath);
        return !normalized.isEmpty() && pinned.contains(normalized);
    });
    return QStringList(shown.cbegin(), shown.cend());
}

/// Whether a path names a file on another machine: a Windows network path in either spelling - `\\server\share\...`
/// as Windows writes it, or `//server/share/...` as `QDir::cleanPath` and a file URL's local path spell it - its long
/// form `\\?\UNC\server\...`, or a file URL that names a host other than `localhost`. A long-path or device prefix to
/// a local drive (`\\?\C:\...`, `\\.\...`), a local path and a relative one are not.
[[nodiscard]] inline bool isNetworkPath(const QString& path)
{
    QString text = path.trimmed();
    while (!text.isEmpty() && text.front() == QChar::ByteOrderMark)
    {
        text.remove(0, 1);
    }
    if (text.startsWith(QStringLiteral("file:"), Qt::CaseInsensitive))
    {
        const QUrl url(text);
        if (!url.isValid() || !url.isLocalFile())
        {
            return false;
        }
        if (!url.host().isEmpty() && url.host().compare(QStringLiteral("localhost"), Qt::CaseInsensitive) != 0)
        {
            return true;
        }
        // A local file's URL names this machine or none; Qt turns a named host into a network path, so the local
        // file is read with the host taken out.
        QUrl local(url);
        local.setHost(QString());
        text = local.toLocalFile();
    }
    const auto separator = [](const QChar c) { return c == u'\\' || c == u'/'; };
    if (text.size() < 3 || !separator(text[0]) || !separator(text[1]) || separator(text[2]))
    {
        return false;
    }
    if ((text[2] == u'?' || text[2] == u'.') && text.size() > 3 && separator(text[3]))
    {
        // A Win32 namespace prefix: only its UNC form names another machine.
        const QString rest = text.mid(4);
        return rest.size() > 3 && rest.startsWith(QStringLiteral("UNC"), Qt::CaseInsensitive) && separator(rest[3]);
    }
    return true;
}

} // namespace DashboardUtils
