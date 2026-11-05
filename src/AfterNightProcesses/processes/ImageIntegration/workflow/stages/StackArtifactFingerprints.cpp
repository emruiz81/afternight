// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The lights stacking stage's fingerprints.

#include "stdafx.hpp"
#include "processes/ImageIntegration/workflow/stages/StackArtifactFingerprints.h"

#include "processes/ImageIntegration/workflow/ManifestHashing.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QFileInfo>
#include <QJsonDocument>

#include <algorithm>

namespace ImageIntegration::detail
{

QString sha256Hex(const QByteArray& bytes)
{
    return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}

QString sha256Hex(const QJsonObject& obj)
{
    return sha256Hex(QJsonDocument(obj).toJson(QJsonDocument::Compact));
}

QJsonValue canonicalizeJson(const QJsonValue& value)
{
    if (value.isObject())
    {
        const QJsonObject obj = value.toObject();
        QStringList keys = obj.keys();
        std::sort(keys.begin(), keys.end());

        QJsonObject ordered;
        for (const auto& key : keys)
        {
            ordered.insert(key, canonicalizeJson(obj.value(key)));
        }
        return ordered;
    }

    if (value.isArray())
    {
        const QJsonArray array = value.toArray();
        QJsonArray out;
        for (const auto& item : array)
        {
            out.append(canonicalizeJson(item));
        }
        return out;
    }

    return value;
}

QString fingerprintJson(const QJsonObject& obj)
{
    const QJsonObject canonical = canonicalizeJson(obj).toObject();
    const QByteArray json = QJsonDocument(canonical).toJson(QJsonDocument::Compact);
    return sha256Hex(json);
}

QJsonArray frameListToJson(const std::vector<FrameId>& frames)
{
    QJsonArray array;
    for (const auto& frame : frames)
    {
        array.append(frame.toJson());
    }
    return array;
}

InputsFingerprint fingerprintInputs(const std::vector<FrameId>& frames)
{
    InputsFingerprint out;
    for (const auto& frame : frames)
    {
        const QFileInfo info(frame.canonicalPath());
        const bool exists = info.exists();
        if (!exists)
        {
            ++out.missingFiles;
        }
        out.maxMtimeMs = (std::max)(out.maxMtimeMs, exists ? info.lastModified().toMSecsSinceEpoch() : qint64{0});
    }

    out.fingerprint = computeFrameFileStampFingerprint(frames);
    return out;
}

} // namespace ImageIntegration::detail
