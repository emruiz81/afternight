// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include <QCryptographicHash>
#include <QFileInfo>
#include <QString>

namespace ImageIntegration
{
[[nodiscard]] inline QString sanitizeFilename(QString value)
{
    static const QString invalid = QStringLiteral("<>:\"/\\|?*");
    for (const auto& ch : invalid)
    {
        value.replace(ch, QLatin1Char('_'));
    }

    value.replace(QLatin1Char(' '), QLatin1Char('_'));
    value.replace(QLatin1Char('|'), QLatin1Char('_'));
    value = value.trimmed();

    while (value.contains(QStringLiteral("__")))
    {
        value.replace(QStringLiteral("__"), QStringLiteral("_"));
    }

    return value.isEmpty() ? QStringLiteral("group") : value;
}

[[nodiscard]] inline QString stableShortHash(const QString& value, int length = 8)
{
    const QByteArray digest = QCryptographicHash::hash(value.toUtf8(), QCryptographicHash::Sha256).toHex();
    const int safeLength = qBound(4, length, static_cast<int>(digest.size()));
    return QString::fromLatin1(digest.left(safeLength));
}

[[nodiscard]] inline QString appendSuffixBeforeExtension(const QString& filename, const QString& suffix)
{
    if (filename.trimmed().isEmpty() || suffix.trimmed().isEmpty())
    {
        return filename;
    }

    const QFileInfo info(filename);
    const QString ext = info.suffix();
    const QString base = info.completeBaseName();

    if (ext.isEmpty())
    {
        return base + suffix;
    }

    return base + suffix + QLatin1Char('.') + ext;
}

[[nodiscard]] inline bool isFitsExtension(const QString& extension)
{
    const QString normalized = extension.trimmed().toLower();
    return normalized == QStringLiteral("fit") || normalized == QStringLiteral("fits") ||
           normalized == QStringLiteral("fts");
}

[[nodiscard]] inline QString makeFitsIntermediateOutputFilename(const QString& inputPath, const QString& suffix)
{
    const QFileInfo info(inputPath);
    const QString base = info.completeBaseName();
    const QString ext = info.suffix();

    if (isFitsExtension(ext))
    {
        return base + suffix + QLatin1Char('.') + ext;
    }

    return base + suffix + QStringLiteral(".fits");
}

[[nodiscard]] inline QString makeCalibratedOutputFilename(const QString& inputPath,
                                                          const QString& suffix = QStringLiteral("_cal"))
{
    return makeFitsIntermediateOutputFilename(inputPath, suffix);
}
} // namespace ImageIntegration
