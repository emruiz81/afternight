// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// A frame's kind from its cards, its folders and the caller's fallback, in that order.

#include "stdafx.hpp"
#include "FrameDetection.h"

#include <AfterNightEngine/IO/CodecMetadataProjection.h>
#include <AfterNightEngine/IO/FrameTypeCards.h>
#include <AfterNightEngine/Image/CodecMetadata.h>

#include <QFileInfo>
#include <QRegularExpression>

#include <span>
#include <string_view>

namespace ImageIntegration
{

namespace
{

/// The model's kind for the engine's, or nothing for a kind the model does not file.
[[nodiscard]] std::optional<FrameType> modelFrameType(const an::FrameType type)
{
    switch (type)
    {
    case an::FrameType::Bias:
        return FrameType::Bias;
    case an::FrameType::Dark:
        return FrameType::Dark;
    case an::FrameType::Flat:
        return FrameType::Flat;
    case an::FrameType::Light:
        return FrameType::Light;
    case an::FrameType::Unknown:
        break;
    }
    return std::nullopt;
}

/// The kind a folder's name names, or nothing.
[[nodiscard]] std::optional<FrameType> kindOfFolder(const QString& name)
{
    const QString folder = name.toLower();
    if (folder == QStringLiteral("flat") || folder == QStringLiteral("flats"))
    {
        return FrameType::Flat;
    }
    if (folder == QStringLiteral("dark") || folder == QStringLiteral("darks"))
    {
        return FrameType::Dark;
    }
    if (folder == QStringLiteral("bias") || folder == QStringLiteral("biases") || folder == QStringLiteral("offset") ||
        folder == QStringLiteral("offsets"))
    {
        return FrameType::Bias;
    }
    if (folder == QStringLiteral("light") || folder == QStringLiteral("lights"))
    {
        return FrameType::Light;
    }
    return std::nullopt;
}

[[nodiscard]] QString textOf(const std::string& value)
{
    return QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size()));
}

/// The keyword of the card that decided, as the file spells it.
[[nodiscard]] QString keywordOf(const an::FrameTypeCard card)
{
    const std::span<const std::string_view> keywords = an::frameTypeKeywords();
    if (card == an::FrameTypeCard::None || !an::isFrameTypeCard(card))
    {
        return {};
    }
    const std::size_t index = static_cast<std::size_t>(card) - static_cast<std::size_t>(an::FrameTypeCard::ImageType);
    if (index >= keywords.size())
    {
        return {};
    }
    return QString::fromLatin1(keywords[index].data(), static_cast<qsizetype>(keywords[index].size()));
}

} // namespace

std::optional<FrameType> FrameDetection::folderHint(const QString& filePath, QString* folderName)
{
    // Accept both Windows ('\\') and Unix ('/') separators on all platforms: QDir::fromNativeSeparators() converts
    // only the separator native to the platform it runs on.
    QString normalizedPath = filePath;
    normalizedPath.replace(QLatin1Char('\\'), QLatin1Char('/'));
    if (normalizedPath.trimmed().isEmpty())
    {
        return std::nullopt;
    }

    const QString dirPath = QFileInfo(normalizedPath).absolutePath();
    const QStringList parts = dirPath.split(QRegularExpression(R"([/\\])"), Qt::SkipEmptyParts);
    for (qsizetype index = parts.size() - 1; index >= 0; --index)
    {
        if (const std::optional<FrameType> kind = kindOfFolder(parts[index]); kind.has_value())
        {
            if (folderName != nullptr)
            {
                *folderName = parts[index];
            }
            return kind;
        }
    }
    return std::nullopt;
}

FrameTypeDetection FrameDetection::detect(const QString& filePath, const an::CodecMetadata* cards,
                                          const FrameType fallback)
{
    if (cards != nullptr)
    {
        const an::FrameTypeReading reading = an::readFrameTypeCards(*cards);
        if (const std::optional<FrameType> kind = modelFrameType(reading.type); kind.has_value())
        {
            return FrameTypeDetection{*kind, FrameTypeEvidence::Card, keywordOf(reading.card),
                                      textOf(reading.spelling)};
        }
    }

    QString folder;
    if (const std::optional<FrameType> hinted = folderHint(filePath, &folder); hinted.has_value())
    {
        return FrameTypeDetection{*hinted, FrameTypeEvidence::Folder, folder, QString()};
    }
    return FrameTypeDetection{fallback, FrameTypeEvidence::Fallback, QString(), QString()};
}

FrameType FrameDetection::detectType(const QString& filePath, const an::CodecMetadata* cards, const FrameType fallback)
{
    return detect(filePath, cards, fallback).type;
}

} // namespace ImageIntegration
