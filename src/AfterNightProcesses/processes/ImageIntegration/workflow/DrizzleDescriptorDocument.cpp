// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The workflow's drizzle descriptor documents and dense map payloads.

#include "stdafx.hpp"
#include "processes/ImageIntegration/workflow/DrizzleDescriptorDocument.h"

#include <AfterNightPlatform/Sha256.h>

#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>

#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <optional>

namespace ImageIntegration
{

namespace
{

constexpr qsizetype kEngineIdentityBytes = static_cast<qsizetype>(sizeof(an::DrizzleMapIdentity::bytes));

[[nodiscard]] QString normalizationSpaceKey(const an::DrizzleNormalizationSpace space)
{
    return space == an::DrizzleNormalizationSpace::OutputPixels ? QStringLiteral("output-pixels")
                                                                : QStringLiteral("source-pixels");
}

[[nodiscard]] bool extent(const QJsonValue& value, std::uint32_t& out)
{
    if (!value.isDouble())
    {
        return false;
    }
    const double number = value.toDouble();
    if (!std::isfinite(number) || std::floor(number) != number || number < 1.0 ||
        number > static_cast<double>(kMaxDrizzleSourceSide))
    {
        return false;
    }
    out = static_cast<std::uint32_t>(number);
    return true;
}

[[nodiscard]] QString fileSha256(const QString& path)
{
    const std::optional<std::array<std::uint8_t, 32>> digest =
        an::platform::sha256OfFile(std::filesystem::path(path.toStdWString()));
    return digest.has_value() ? QString::fromStdString(an::platform::sha256Hex(*digest)) : QString();
}

} // namespace

an::DrizzleMappingRequest drizzleMappingRequestOf(const DrizzleDescriptorDocument& document)
{
    an::DrizzleMappingRequest request;
    request.transform = document.transform;
    request.postTranslation = document.postTranslation;
    request.sourceDistortion = document.resolvedSourceDistortion;
    return request;
}

QByteArray formatDrizzleDescriptor(const DrizzleDescriptorDocument& document)
{
    QJsonObject mapping;
    mapping.insert(QStringLiteral("transform"),
                   QJsonDocument::fromJson(
                       stage_utils::serializeTransformJson(document.transform, document.sourceDistortion).toUtf8())
                       .object());
    mapping.insert(QStringLiteral("postTranslation"), stage_utils::pointToJsonArray(document.postTranslation));
    if (document.resolvedSourceDistortion.has_value())
    {
        mapping.insert(QStringLiteral("resolvedSourceDistortion"),
                       stage_utils::sipDistortionToJson(*document.resolvedSourceDistortion));
    }

    QJsonObject object;
    object.insert(QStringLiteral("frameId"), document.frameId);
    object.insert(QStringLiteral("sourceWidth"), static_cast<double>(document.sourceWidth));
    object.insert(QStringLiteral("sourceHeight"), static_cast<double>(document.sourceHeight));
    object.insert(QStringLiteral("outputWidth"), static_cast<double>(document.outputWidth));
    object.insert(QStringLiteral("outputHeight"), static_cast<double>(document.outputHeight));
    object.insert(QStringLiteral("scale"), document.scale);
    object.insert(QStringLiteral("mapping"), mapping);
    object.insert(QStringLiteral("sampleConvention"), QStringLiteral("integer-pixel-centres"));
    object.insert(QStringLiteral("invalidCoordinate"), QStringLiteral("paired-non-finite"));
    object.insert(QStringLiteral("normalizationSpace"), normalizationSpaceKey(document.normalizationSpace));
    object.insert(QStringLiteral("engineIdentity"), QString::fromLatin1(document.engineIdentity.toBase64()));
    if (document.densePayload.has_value())
    {
        QJsonObject dense;
        dense.insert(QStringLiteral("fileName"), document.densePayload->fileName);
        dense.insert(QStringLiteral("length"), QString::number(document.densePayload->length));
        dense.insert(QStringLiteral("sha256"), document.densePayload->sha256);
        object.insert(QStringLiteral("densePayload"), dense);
    }
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

std::optional<DrizzleDescriptorDocument> parseDrizzleDescriptor(const QByteArray& payload, QString& error)
{
    QJsonParseError parseError{};
    const QJsonDocument json = QJsonDocument::fromJson(payload, &parseError);
    if (parseError.error != QJsonParseError::NoError || !json.isObject())
    {
        error = QStringLiteral("The drizzle descriptor is not readable.");
        return std::nullopt;
    }
    const QJsonObject object = json.object();
    DrizzleDescriptorDocument document;
    document.frameId = object.value(QStringLiteral("frameId")).toString();
    if (document.frameId.trimmed().isEmpty())
    {
        error = QStringLiteral("The drizzle descriptor names no light.");
        return std::nullopt;
    }
    if (!extent(object.value(QStringLiteral("sourceWidth")), document.sourceWidth) ||
        !extent(object.value(QStringLiteral("sourceHeight")), document.sourceHeight) ||
        !extent(object.value(QStringLiteral("outputWidth")), document.outputWidth) ||
        !extent(object.value(QStringLiteral("outputHeight")), document.outputHeight))
    {
        error = QStringLiteral("The drizzle descriptor's extents are invalid.");
        return std::nullopt;
    }
    document.scale = object.value(QStringLiteral("scale")).toDouble(std::numeric_limits<double>::quiet_NaN());
    if (!std::isfinite(document.scale) || document.scale <= 0.0)
    {
        error = QStringLiteral("The drizzle descriptor's scale is invalid.");
        return std::nullopt;
    }
    if (object.value(QStringLiteral("sampleConvention")).toString() != QStringLiteral("integer-pixel-centres") ||
        object.value(QStringLiteral("invalidCoordinate")).toString() != QStringLiteral("paired-non-finite"))
    {
        error = QStringLiteral("The drizzle descriptor states a convention this version does not read.");
        return std::nullopt;
    }
    const QString space = object.value(QStringLiteral("normalizationSpace")).toString();
    if (space == QStringLiteral("source-pixels"))
    {
        document.normalizationSpace = an::DrizzleNormalizationSpace::SourcePixels;
    }
    else if (space == QStringLiteral("output-pixels"))
    {
        document.normalizationSpace = an::DrizzleNormalizationSpace::OutputPixels;
    }
    else
    {
        error = QStringLiteral("The drizzle descriptor's normalization space is invalid.");
        return std::nullopt;
    }

    const QJsonObject mapping = object.value(QStringLiteral("mapping")).toObject();
    const QByteArray transformJson =
        QJsonDocument(mapping.value(QStringLiteral("transform")).toObject()).toJson(QJsonDocument::Compact);
    const auto record = stage_utils::parseTransformRecord(QString::fromUtf8(transformJson), error);
    if (!record.has_value())
    {
        return std::nullopt;
    }
    document.transform = record->transform;
    document.sourceDistortion = record->provenance;
    if (!stage_utils::jsonArrayToPoint(mapping.value(QStringLiteral("postTranslation")), document.postTranslation))
    {
        error = QStringLiteral("The drizzle descriptor's offset is invalid.");
        return std::nullopt;
    }
    if (mapping.contains(QStringLiteral("resolvedSourceDistortion")))
    {
        an::SipDistortion resolved;
        if (!stage_utils::sipDistortionFromJson(mapping.value(QStringLiteral("resolvedSourceDistortion")).toObject(),
                                                resolved, &error))
        {
            return std::nullopt;
        }
        document.resolvedSourceDistortion = resolved;
    }

    document.engineIdentity = QByteArray::fromBase64(
        object.value(QStringLiteral("engineIdentity")).toString().toLatin1(), QByteArray::AbortOnBase64DecodingErrors);
    if (!document.engineIdentity.isEmpty() && document.engineIdentity.size() != kEngineIdentityBytes)
    {
        error = QStringLiteral("The drizzle descriptor's identity is not the engine's.");
        return std::nullopt;
    }

    if (object.contains(QStringLiteral("densePayload")))
    {
        const QJsonObject dense = object.value(QStringLiteral("densePayload")).toObject();
        DrizzleDensePayloadReference reference;
        reference.fileName = dense.value(QStringLiteral("fileName")).toString();
        bool lengthParsed = false;
        reference.length = dense.value(QStringLiteral("length")).toString().toULongLong(&lengthParsed);
        reference.sha256 = dense.value(QStringLiteral("sha256")).toString();
        if (reference.fileName.trimmed().isEmpty() || reference.fileName.contains(QLatin1Char('/')) ||
            reference.fileName.contains(QLatin1Char('\\')) || !lengthParsed || reference.sha256.size() != 64)
        {
            error = QStringLiteral("The drizzle descriptor's dense payload reference is invalid.");
            return std::nullopt;
        }
        document.densePayload = std::move(reference);
    }
    return document;
}

QString drizzleDescriptorIdentity(const DrizzleDescriptorDocument& document)
{
    if (document.engineIdentity.isEmpty())
    {
        return {};
    }
    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(document.engineIdentity);
    hash.addData(document.frameId.toUtf8());
    return QString::fromLatin1(hash.result().toHex());
}

DocumentEnvelope drizzleDescriptorEnvelope(const DrizzleDescriptorDocument& document)
{
    DocumentEnvelope envelope;
    envelope.documentType = QString::fromLatin1(kDrizzleDescriptorDocumentType);
    envelope.schemaVersion = kDrizzleDescriptorSchemaVersion;
    envelope.identity = drizzleDescriptorIdentity(document);
    envelope.payload = formatDrizzleDescriptor(document);
    return envelope;
}

std::optional<DrizzleDescriptorDocument> readDrizzleDescriptor(const QString& path, QString& error)
{
    DocumentReadRequest request;
    request.documentType = QString::fromLatin1(kDrizzleDescriptorDocumentType);
    request.maximumSchemaVersion = kDrizzleDescriptorSchemaVersion;
    request.maxPayloadBytes = 4ull * 1024ull * 1024ull;
    const DocumentReadResult read = readDocument(path, request);
    if (!read.ok())
    {
        error = read.message;
        return std::nullopt;
    }
    auto document = parseDrizzleDescriptor(read.envelope.payload, error);
    if (document.has_value() && read.envelope.identity != drizzleDescriptorIdentity(*document))
    {
        error = QStringLiteral("The drizzle descriptor's envelope does not carry its identity.");
        return std::nullopt;
    }
    return document;
}

std::optional<std::uint64_t> drizzleDensePayloadSampleBytes(const std::uint32_t sourceWidth,
                                                            const std::uint32_t sourceHeight)
{
    if (sourceWidth == 0u || sourceHeight == 0u || sourceWidth > kMaxDrizzleSourceSide ||
        sourceHeight > kMaxDrizzleSourceSide)
    {
        return std::nullopt;
    }
    return static_cast<std::uint64_t>(sourceWidth) * sourceHeight * 2u * sizeof(float);
}

std::optional<DocumentEnvelope> drizzleDensePayloadEnvelope(const DrizzleDescriptorDocument& document,
                                                            const QByteArray& samples)
{
    const auto expected = drizzleDensePayloadSampleBytes(document.sourceWidth, document.sourceHeight);
    const QString identity = drizzleDescriptorIdentity(document);
    if (!expected.has_value() || static_cast<std::uint64_t>(samples.size()) != *expected || identity.isEmpty())
    {
        return std::nullopt;
    }
    DocumentEnvelope envelope;
    envelope.documentType = QString::fromLatin1(kDrizzleDensePayloadDocumentType);
    envelope.schemaVersion = kDrizzleDensePayloadSchemaVersion;
    envelope.identity = identity;
    envelope.payload = samples;
    return envelope;
}

std::optional<QByteArray> readDrizzleDensePayload(const QString& path, const DrizzleDescriptorDocument& document,
                                                  QString& error)
{
    if (!document.densePayload.has_value())
    {
        error = QStringLiteral("The drizzle descriptor names no dense payload.");
        return std::nullopt;
    }
    const auto expected = drizzleDensePayloadSampleBytes(document.sourceWidth, document.sourceHeight);
    if (!expected.has_value())
    {
        error = QStringLiteral("The drizzle descriptor's source extent is invalid.");
        return std::nullopt;
    }
    const QFileInfo info(path);
    if (!info.isFile() || static_cast<std::uint64_t>(info.size()) != document.densePayload->length)
    {
        error = QStringLiteral("The dense payload is not the length its descriptor states.");
        return std::nullopt;
    }
    if (fileSha256(path) != document.densePayload->sha256)
    {
        error = QStringLiteral("The dense payload is not the one its descriptor states.");
        return std::nullopt;
    }
    DocumentReadRequest request;
    request.documentType = QString::fromLatin1(kDrizzleDensePayloadDocumentType);
    request.maximumSchemaVersion = kDrizzleDensePayloadSchemaVersion;
    request.expectedIdentity = drizzleDescriptorIdentity(document);
    request.maxPayloadBytes = *expected;
    const DocumentReadResult read = readDocument(path, request);
    if (!read.ok())
    {
        error = read.message;
        return std::nullopt;
    }
    if (static_cast<std::uint64_t>(read.envelope.payload.size()) != *expected)
    {
        error = QStringLiteral("The dense payload's samples do not cover the source extent.");
        return std::nullopt;
    }
    return read.envelope.payload;
}

} // namespace ImageIntegration
