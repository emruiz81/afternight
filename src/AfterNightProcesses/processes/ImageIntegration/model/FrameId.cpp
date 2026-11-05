// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "FrameId.h"

#include <QDir>
#include <QFileInfo>
#include <QStringList>

#include <stdexcept>

namespace
{
bool isWindowsDriveAbsolutePath(const QString& path)
{
    if (path.size() < 3)
    {
        return false;
    }

    const QChar drive = path[0];
    if (!drive.isLetter())
    {
        return false;
    }

    return path[1] == QLatin1Char(':') && path[2] == QLatin1Char('/');
}

bool isUncAbsolutePath(const QString& path)
{
    // UNC paths are represented as //server/share/... (Qt normalizes separators to '/').
    return path.startsWith(QStringLiteral("//"));
}

bool isWindowsDriveRoot(const QString& path)
{
    return path.size() == 3 && path[1] == QLatin1Char(':') && path[2] == QLatin1Char('/');
}

bool isUncRoot(const QString& path)
{
    if (!path.startsWith(QStringLiteral("//")))
    {
        return false;
    }

    const QStringList parts = path.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    return parts.size() == 2;
}

bool isRootPath(const QString& path)
{
    if (path == QStringLiteral("/"))
    {
        return true;
    }

    return isWindowsDriveRoot(path) || isUncRoot(path);
}

QString stripLongPathPrefix(const QString& path)
{
    QString normalized = QDir::fromNativeSeparators(path);
    normalized.replace(QLatin1Char('\\'), QLatin1Char('/'));
    const QString longPrefix = QStringLiteral("//?/");

    if (!normalized.startsWith(longPrefix, Qt::CaseInsensitive))
    {
        return normalized;
    }

    const QString remainder = normalized.mid(longPrefix.size());
    if (remainder.startsWith(QStringLiteral("UNC/"), Qt::CaseInsensitive))
    {
        return QStringLiteral("//") + remainder.mid(4);
    }

    return remainder;
}

QString trimTrailingSeparators(QString path)
{
    while (path.endsWith(QLatin1Char('/')) && !isRootPath(path))
    {
        path.chop(1);
    }

    return path;
}

QString normalizeDriveLetter(QString path)
{
    if (path.size() >= 2 && path[1] == QLatin1Char(':'))
    {
        path[0] = path[0].toUpper();
    }

    return path;
}
} // namespace

FrameId::FrameId(const QString& path)
{
    if (path.trimmed().isEmpty())
    {
        throw std::invalid_argument("FrameId path must not be empty.");
    }

    m_canonicalPath = canonicalizePath(path);
    m_identityKey = identityKeyFor(m_canonicalPath);
}

const QString& FrameId::canonicalPath() const noexcept
{
    return m_canonicalPath;
}

QString FrameId::toString() const
{
    return QDir::toNativeSeparators(m_canonicalPath);
}

bool FrameId::isValid() const noexcept
{
    return !m_canonicalPath.isEmpty();
}

QJsonValue FrameId::toJson() const
{
    return QJsonValue(m_canonicalPath);
}

FrameId FrameId::fromJson(const QJsonValue& value)
{
    if (!value.isString())
    {
        throw std::invalid_argument("FrameId JSON value must be a string.");
    }

    return FrameId(value.toString());
}

QString FrameId::canonicalizePath(const QString& path)
{
    QString normalized = stripLongPathPrefix(path);
    normalized.replace(QLatin1Char('\\'), QLatin1Char('/'));
    normalized = QDir::cleanPath(normalized);

    // QFileInfo::absoluteFilePath() treats Windows drive-letter paths like "C:/..." as
    // relative paths on non-Windows platforms, which incorrectly prefixes the current
    // working directory (breaking cross-platform project files and tests).
    const bool windowsLikeAbsolute = isWindowsDriveAbsolutePath(normalized) || isUncAbsolutePath(normalized);

    const QString absolutePath =
        windowsLikeAbsolute ? normalized : QDir::fromNativeSeparators(QFileInfo(normalized).absoluteFilePath());
    QString canonical = trimTrailingSeparators(absolutePath);

    if (isWindowsDriveAbsolutePath(canonical))
    {
        canonical = normalizeDriveLetter(canonical);
    }

    return canonical;
}

QString FrameId::identityKeyFor(const QString& canonicalPath)
{
    // Windows paths should compare case-insensitively even when constructed on non-Windows
    // (e.g. project files created on Windows but opened on Linux).
    if (isWindowsDriveAbsolutePath(canonicalPath) || isUncAbsolutePath(canonicalPath))
    {
        return canonicalPath.toCaseFolded();
    }

    return canonicalPath;
}
