// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The integration workflow's registration records, read and written in engine pixel coordinates.

#include "stdafx.hpp"
#include "processes/ImageIntegration/workflow/stages/StageParsingUtils.h"

#include "processes/ImageIntegration/model/FrameId.h"

#include <AfterNightEngine/Core/Status.h>

#include <QCryptographicHash>
#include <QDataStream>
#include <QFileInfo>
#include <QJsonDocument>
#include <QStringList>

#include <algorithm>
#include <cmath>
#include <limits>

namespace stage_utils
{

namespace
{

constexpr int kSipTableSize = static_cast<int>(an::kSipCoefficientRowCount);

/// A name the record writes for the pixel convention it holds, so a schema 5 record states what it means.
constexpr char kEnginePixelConvention[] = "engine-pixel-centres-at-half";

[[nodiscard]] QString describe(const an::Status& status)
{
    return QString::fromUtf8(status.message());
}

[[nodiscard]] QJsonArray matrixToJson(const std::array<double, 9>& matrix)
{
    QJsonArray rows;
    for (int row = 0; row < 3; ++row)
    {
        QJsonArray columns;
        for (int column = 0; column < 3; ++column)
        {
            columns.append(matrix[static_cast<std::size_t>(row * 3 + column)]);
        }
        rows.append(columns);
    }
    return rows;
}

[[nodiscard]] bool matrixFromJson(const QJsonValue& value, std::array<double, 9>& out, QString& error)
{
    const QJsonArray rows = value.toArray();
    if (rows.size() != 3)
    {
        error = QStringLiteral("Transform matrix is invalid.");
        return false;
    }
    for (int row = 0; row < 3; ++row)
    {
        const QJsonArray columns = rows.at(row).toArray();
        if (columns.size() != 3)
        {
            error = QStringLiteral("Transform matrix is invalid.");
            return false;
        }
        for (int column = 0; column < 3; ++column)
        {
            if (!columns.at(column).isDouble() || !std::isfinite(columns.at(column).toDouble()))
            {
                error = QStringLiteral("Transform matrix contains a non-numeric or non-finite coefficient.");
                return false;
            }
            out[static_cast<std::size_t>(row * 3 + column)] = columns.at(column).toDouble();
        }
    }
    return true;
}

[[nodiscard]] QJsonArray sipTableToJson(const an::SipPolynomial& polynomial)
{
    QJsonArray rows;
    for (int i = 0; i < kSipTableSize; ++i)
    {
        QJsonArray columns;
        for (int j = 0; j < kSipTableSize; ++j)
        {
            columns.append(polynomial.coefficients[static_cast<std::size_t>(i)][static_cast<std::size_t>(j)]);
        }
        rows.append(columns);
    }
    return rows;
}

[[nodiscard]] bool sipTableFromJson(const QJsonValue& value, an::SipPolynomial& out)
{
    const QJsonArray rows = value.toArray();
    if (rows.size() != kSipTableSize)
    {
        return false;
    }
    for (int i = 0; i < kSipTableSize; ++i)
    {
        const QJsonArray columns = rows.at(i).toArray();
        if (columns.size() != kSipTableSize)
        {
            return false;
        }
        for (int j = 0; j < kSipTableSize; ++j)
        {
            if (!columns.at(j).isDouble() || !std::isfinite(columns.at(j).toDouble()))
            {
                return false;
            }
            out.coefficients[static_cast<std::size_t>(i)][static_cast<std::size_t>(j)] = columns.at(j).toDouble();
        }
    }
    return true;
}

[[nodiscard]] bool integralNumber(const QJsonValue& value, int& out)
{
    if (!value.isDouble())
    {
        return false;
    }
    const double number = value.toDouble();
    if (!std::isfinite(number) || std::floor(number) != number || number < -1.0e6 || number > 1.0e6)
    {
        return false;
    }
    out = static_cast<int>(number);
    return true;
}

void streamSip(QDataStream& stream, const an::SipDistortion& sip)
{
    const auto addComponent = [&stream](const an::SipPolynomial& component) {
        stream << static_cast<qint32>(component.order);
        for (const auto& row : component.coefficients)
        {
            for (const double coefficient : row)
            {
                stream << coefficient;
            }
        }
    };
    stream << sip.referencePixel().x << sip.referencePixel().y;
    addComponent(sip.forwardX());
    addComponent(sip.forwardY());
    addComponent(sip.inverseX());
    addComponent(sip.inverseY());
}

[[nodiscard]] std::vector<QString> sortedKeys(const std::unordered_map<QString, an::RegistrationTransform>& transforms)
{
    std::vector<QString> keys;
    keys.reserve(transforms.size());
    for (const auto& entry : transforms)
    {
        keys.push_back(entry.first);
    }
    std::sort(keys.begin(), keys.end());
    return keys;
}

[[nodiscard]] std::optional<SourceDistortionProvenance> provenanceFromRecord(const QJsonObject& object, QString& error)
{
    std::optional<an::SipDistortion> model;
    if (object.contains(QStringLiteral("sourceDistortion")))
    {
        an::SipDistortion parsed;
        if (!sipDistortionFromJson(object.value(QStringLiteral("sourceDistortion")).toObject(), parsed, &error))
        {
            if (error.isEmpty())
            {
                error = QStringLiteral("Source distortion payload is invalid.");
            }
            return std::nullopt;
        }
        model = parsed;
    }

    const auto policy =
        sourceDistortionPolicyFromKey(object.value(QStringLiteral("sourceDistortionPolicy")).toString());
    if (!policy.has_value())
    {
        error = QStringLiteral("Source distortion policy is missing or invalid.");
        return std::nullopt;
    }
    SourceDistortionProvenance provenance{*policy, model};
    if (!provenance.consistent())
    {
        error = *policy == SourceDistortionPolicy::ExplicitModel
                    ? QStringLiteral("ExplicitModel requires a source distortion payload.")
                    : QStringLiteral("Source distortion policy contradicts the supplied model payload.");
        return std::nullopt;
    }
    return provenance;
}

/// A format 5 record's transform, in engine coordinates.
[[nodiscard]] std::optional<an::RegistrationTransform> engineTransformFromRecord(const QJsonObject& object,
                                                                                 QString& error)
{
    if (object.value(QStringLiteral("pixelConvention")).toString() != QLatin1String(kEnginePixelConvention))
    {
        error = QStringLiteral("Transform record does not state the engine's pixel convention.");
        return std::nullopt;
    }
    an::RegistrationTransformRequest request;
    const auto linearKind = registrationTransformKindFromKey(object.value(QStringLiteral("linearKind")).toString());
    if (!linearKind.has_value() || !an::isLinearRegistrationTransformKind(*linearKind))
    {
        error = QStringLiteral("Transform linear kind is invalid.");
        return std::nullopt;
    }
    request.linearKind = *linearKind;
    if (!matrixFromJson(object.value(QStringLiteral("matrix")), request.sourceToReference, error))
    {
        return std::nullopt;
    }
    if (object.contains(QStringLiteral("polynomial")))
    {
        const QJsonObject polynomial = object.value(QStringLiteral("polynomial")).toObject();
        int order = 0;
        if (!integralNumber(polynomial.value(QStringLiteral("order")), order) ||
            order < static_cast<int>(an::kMinimumRegistrationPolynomialOrder) ||
            order > static_cast<int>(an::kMaximumRegistrationPolynomialOrder))
        {
            error = QStringLiteral("Polynomial transform order is invalid.");
            return std::nullopt;
        }
        request.polynomial.order = static_cast<std::uint32_t>(order);
        const int terms = static_cast<int>(an::registrationPolynomialTermCount(request.polynomial.order));
        const QJsonArray x = polynomial.value(QStringLiteral("x")).toArray();
        const QJsonArray y = polynomial.value(QStringLiteral("y")).toArray();
        if (x.size() != terms || y.size() != terms)
        {
            error = QStringLiteral("Polynomial transform coefficient count is invalid.");
            return std::nullopt;
        }
        for (int term = 0; term < terms; ++term)
        {
            if (!x.at(term).isDouble() || !y.at(term).isDouble())
            {
                error = QStringLiteral("Polynomial transform contains a non-numeric coefficient.");
                return std::nullopt;
            }
            request.polynomial.x[static_cast<std::size_t>(term)] = x.at(term).toDouble();
            request.polynomial.y[static_cast<std::size_t>(term)] = y.at(term).toDouble();
        }
        if (!jsonArrayToPoint(polynomial.value(QStringLiteral("normalizationOrigin")),
                              request.polynomial.normalizationOrigin) ||
            !jsonArrayToPoint(polynomial.value(QStringLiteral("normalizationScale")),
                              request.polynomial.normalizationScale))
        {
            error = QStringLiteral("Polynomial transform normalization metadata is invalid.");
            return std::nullopt;
        }
    }
    const auto kind = registrationTransformKindFromKey(object.value(QStringLiteral("kind")).toString());
    auto transform = an::RegistrationTransform::create(request);
    if (!transform)
    {
        error = QStringLiteral("Transform is not a registration model: %1").arg(describe(transform.status()));
        return std::nullopt;
    }
    if (!kind.has_value() || *kind != transform.value().kind())
    {
        error = QStringLiteral("Transform kind contradicts its payload.");
        return std::nullopt;
    }
    return transform.value();
}

} // namespace

SourceDistortionProvenance SourceDistortionProvenance::inheritMetadata()
{
    return {SourceDistortionPolicy::InheritMetadata, std::nullopt};
}

SourceDistortionProvenance SourceDistortionProvenance::explicitNone()
{
    return {SourceDistortionPolicy::ExplicitNone, std::nullopt};
}

SourceDistortionProvenance SourceDistortionProvenance::explicitModel(const an::SipDistortion& sipModel)
{
    return {SourceDistortionPolicy::ExplicitModel, sipModel};
}

bool SourceDistortionProvenance::consistent() const noexcept
{
    return model.has_value() == (policy == SourceDistortionPolicy::ExplicitModel);
}

QString sourceDistortionPolicyKey(const SourceDistortionPolicy policy)
{
    switch (policy)
    {
    case SourceDistortionPolicy::InheritMetadata:
        return QStringLiteral("InheritMetadata");
    case SourceDistortionPolicy::ExplicitNone:
        return QStringLiteral("ExplicitNone");
    case SourceDistortionPolicy::ExplicitModel:
        return QStringLiteral("ExplicitModel");
    }
    return {};
}

std::optional<SourceDistortionPolicy> sourceDistortionPolicyFromKey(const QString& key)
{
    if (key == QStringLiteral("InheritMetadata"))
        return SourceDistortionPolicy::InheritMetadata;
    if (key == QStringLiteral("ExplicitNone"))
        return SourceDistortionPolicy::ExplicitNone;
    if (key == QStringLiteral("ExplicitModel"))
        return SourceDistortionPolicy::ExplicitModel;
    return std::nullopt;
}

QString registrationTransformKindKey(const an::RegistrationTransformKind kind)
{
    switch (kind)
    {
    case an::RegistrationTransformKind::Similarity:
        return QStringLiteral("similarity");
    case an::RegistrationTransformKind::Affine:
        return QStringLiteral("affine");
    case an::RegistrationTransformKind::Projective:
        return QStringLiteral("homography");
    case an::RegistrationTransformKind::Polynomial2:
        return QStringLiteral("polynomial2");
    case an::RegistrationTransformKind::Polynomial3:
        return QStringLiteral("polynomial3");
    case an::RegistrationTransformKind::Polynomial4:
        return QStringLiteral("polynomial4");
    }
    return {};
}

std::optional<an::RegistrationTransformKind> registrationTransformKindFromKey(const QString& key)
{
    const QString normalized = key.trimmed().toLower();
    if (normalized == QStringLiteral("similarity"))
        return an::RegistrationTransformKind::Similarity;
    if (normalized == QStringLiteral("affine"))
        return an::RegistrationTransformKind::Affine;
    if (normalized == QStringLiteral("homography"))
        return an::RegistrationTransformKind::Projective;
    if (normalized == QStringLiteral("polynomial2"))
        return an::RegistrationTransformKind::Polynomial2;
    if (normalized == QStringLiteral("polynomial3"))
        return an::RegistrationTransformKind::Polynomial3;
    if (normalized == QStringLiteral("polynomial4"))
        return an::RegistrationTransformKind::Polynomial4;
    return std::nullopt;
}

QJsonArray pointToJsonArray(const an::PixelCoordinate point)
{
    QJsonArray out;
    out.append(point.x);
    out.append(point.y);
    return out;
}

bool jsonArrayToPoint(const QJsonValue& value, an::PixelCoordinate& out)
{
    const QJsonArray coordinates = value.toArray();
    if (coordinates.size() != 2 || !coordinates.at(0).isDouble() || !coordinates.at(1).isDouble())
    {
        return false;
    }
    out.x = coordinates.at(0).toDouble();
    out.y = coordinates.at(1).toDouble();
    return std::isfinite(out.x) && std::isfinite(out.y);
}

QJsonObject sipDistortionToJson(const an::SipDistortion& sip)
{
    const auto component = [](const an::SipPolynomial& polynomial) {
        QJsonObject object;
        object.insert(QStringLiteral("order"), static_cast<int>(polynomial.order));
        object.insert(QStringLiteral("coefficients"), sipTableToJson(polynomial));
        return object;
    };
    QJsonObject object;
    object.insert(QStringLiteral("referencePixel"), pointToJsonArray(sip.referencePixel()));
    object.insert(QStringLiteral("A"), component(sip.forwardX()));
    object.insert(QStringLiteral("B"), component(sip.forwardY()));
    object.insert(QStringLiteral("AP"), component(sip.inverseX()));
    object.insert(QStringLiteral("BP"), component(sip.inverseY()));
    return object;
}

bool sipDistortionFromJson(const QJsonObject& object, an::SipDistortion& out, QString* error)
{
    an::SipDistortionRequest request;
    const auto component = [](const QJsonValue& value, an::SipPolynomial& polynomial) {
        const QJsonObject componentObject = value.toObject();
        int order = 0;
        if (!integralNumber(componentObject.value(QStringLiteral("order")), order) || order < 0)
        {
            return false;
        }
        polynomial.order = static_cast<std::uint32_t>(order);
        return sipTableFromJson(componentObject.value(QStringLiteral("coefficients")), polynomial);
    };
    if (!jsonArrayToPoint(object.value(QStringLiteral("referencePixel")), request.referencePixel) ||
        !component(object.value(QStringLiteral("A")), request.forwardX) ||
        !component(object.value(QStringLiteral("B")), request.forwardY) ||
        !component(object.value(QStringLiteral("AP")), request.inverseX) ||
        !component(object.value(QStringLiteral("BP")), request.inverseY))
    {
        if (error != nullptr)
        {
            *error = QStringLiteral("Source distortion payload is invalid.");
        }
        return false;
    }
    auto sip = an::SipDistortion::create(request);
    if (!sip)
    {
        if (error != nullptr)
        {
            *error = QStringLiteral("Source distortion is not a distortion model: %1").arg(describe(sip.status()));
        }
        return false;
    }
    out = sip.value();
    return true;
}

QString serializeTransformJson(const an::RegistrationTransform& transform, const SourceDistortionProvenance& provenance)
{
    QJsonObject object;
    object.insert(QStringLiteral("schemaVersion"), kRegistrationTransformSchemaVersion);
    object.insert(QStringLiteral("pixelConvention"), QString::fromLatin1(kEnginePixelConvention));
    object.insert(QStringLiteral("kind"), registrationTransformKindKey(transform.kind()));
    object.insert(QStringLiteral("linearKind"), registrationTransformKindKey(transform.linearKind()));
    object.insert(QStringLiteral("matrix"), matrixToJson(transform.matrix()));
    if (!transform.isLinear())
    {
        const an::RegistrationPolynomial& polynomial = transform.polynomial();
        const int terms = static_cast<int>(an::registrationPolynomialTermCount(polynomial.order));
        QJsonArray x;
        QJsonArray y;
        for (int term = 0; term < terms; ++term)
        {
            x.append(polynomial.x[static_cast<std::size_t>(term)]);
            y.append(polynomial.y[static_cast<std::size_t>(term)]);
        }
        QJsonObject polynomialObject;
        polynomialObject.insert(QStringLiteral("order"), static_cast<int>(polynomial.order));
        polynomialObject.insert(QStringLiteral("x"), x);
        polynomialObject.insert(QStringLiteral("y"), y);
        polynomialObject.insert(QStringLiteral("normalizationOrigin"),
                                pointToJsonArray(polynomial.normalizationOrigin));
        polynomialObject.insert(QStringLiteral("normalizationScale"), pointToJsonArray(polynomial.normalizationScale));
        object.insert(QStringLiteral("polynomial"), polynomialObject);
    }
    object.insert(QStringLiteral("sourceDistortionPolicy"), sourceDistortionPolicyKey(provenance.policy));
    if (provenance.model.has_value())
    {
        object.insert(QStringLiteral("sourceDistortion"), sipDistortionToJson(*provenance.model));
    }
    return QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact));
}

std::optional<ParsedTransformRecord> parseTransformRecord(const QString& json, QString& error)
{
    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(json.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
    {
        error = QStringLiteral("Failed to parse transform JSON.");
        return std::nullopt;
    }
    const QJsonObject object = document.object();
    ParsedTransformRecord record;
    // A record is read only at the format the writer writes; one of another format, or one that states none, is never
    // read as this one.
    if (!object.contains(QStringLiteral("schemaVersion")))
    {
        error = QStringLiteral("The transform record states no format version; this version of AfterNight reads "
                               "format %1.")
                    .arg(kRegistrationTransformSchemaVersion);
        return std::nullopt;
    }
    if (!integralNumber(object.value(QStringLiteral("schemaVersion")), record.schemaVersion) ||
        record.schemaVersion != kRegistrationTransformSchemaVersion)
    {
        error = QStringLiteral("The transform record was written in format %1; this version of AfterNight reads format "
                               "%2.")
                    .arg(object.value(QStringLiteral("schemaVersion")).toVariant().toString())
                    .arg(kRegistrationTransformSchemaVersion);
        return std::nullopt;
    }

    const auto transform = engineTransformFromRecord(object, error);
    if (!transform.has_value())
    {
        return std::nullopt;
    }
    record.transform = *transform;

    auto provenance = provenanceFromRecord(object, error);
    if (!provenance.has_value())
    {
        return std::nullopt;
    }
    record.provenance = std::move(*provenance);
    return record;
}

bool parseRegistrationTransformJson(const QString& json, an::RegistrationTransform& out,
                                    SourceDistortionProvenance* provenance, QString& error)
{
    const auto record = parseTransformRecord(json, error);
    if (!record.has_value())
    {
        return false;
    }
    out = record->transform;
    if (provenance != nullptr)
    {
        *provenance = record->provenance;
    }
    return true;
}

bool parseRegistrationTransformJson(const QString& json, an::RegistrationTransform& out, QString& error)
{
    return parseRegistrationTransformJson(json, out, nullptr, error);
}

QByteArray fingerprintRegistrationTransform(const an::RegistrationTransform& transform)
{
    QByteArray data;
    QDataStream stream(&data, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream << static_cast<qint32>(transform.kind()) << static_cast<qint32>(transform.linearKind());
    for (const double coefficient : transform.matrix())
    {
        stream << coefficient;
    }
    if (!transform.isLinear())
    {
        const an::RegistrationPolynomial& polynomial = transform.polynomial();
        stream << static_cast<qint32>(polynomial.order);
        const std::uint32_t terms = an::registrationPolynomialTermCount(polynomial.order);
        for (std::uint32_t term = 0; term < terms; ++term)
        {
            stream << polynomial.x[term] << polynomial.y[term];
        }
        stream << polynomial.normalizationOrigin.x << polynomial.normalizationOrigin.y;
        stream << polynomial.normalizationScale.x << polynomial.normalizationScale.y;
    }
    return data;
}

QByteArray fingerprintSourceDistortion(const SourceDistortionPolicy policy,
                                       const std::optional<an::SipDistortion>& resolvedModel)
{
    QByteArray data;
    QDataStream stream(&data, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream << static_cast<qint8>(policy);
    if (resolvedModel.has_value())
    {
        streamSip(stream, *resolvedModel);
    }
    return data;
}

QByteArray fingerprintSourceDistortion(const SourceDistortionProvenance& provenance)
{
    return fingerprintSourceDistortion(provenance.policy, provenance.model);
}

QString fingerprintTransforms(const std::unordered_map<QString, an::RegistrationTransform>& transforms)
{
    QCryptographicHash hash(QCryptographicHash::Sha256);
    for (const QString& key : sortedKeys(transforms))
    {
        hash.addData(key.toUtf8());
        hash.addData(fingerprintRegistrationTransform(transforms.at(key)));
    }
    return QString::fromLatin1(hash.result().toHex());
}

QString fingerprintSourceDistortionHex(const SourceDistortionProvenance& provenance)
{
    return QString::fromLatin1(
        QCryptographicHash::hash(fingerprintSourceDistortion(provenance), QCryptographicHash::Sha256).toHex());
}

QString fingerprintSourceDistortionHex(const SourceDistortionPolicy policy,
                                       const std::optional<an::SipDistortion>& resolvedModel)
{
    return QString::fromLatin1(
        QCryptographicHash::hash(fingerprintSourceDistortion(policy, resolvedModel), QCryptographicHash::Sha256)
            .toHex());
}

QString fingerprintSourceDistortions(const std::unordered_map<QString, an::RegistrationTransform>& transforms,
                                     const std::unordered_map<QString, SourceDistortionProvenance>& provenanceByFrame)
{
    const SourceDistortionProvenance inherit = SourceDistortionProvenance::inheritMetadata();
    QCryptographicHash hash(QCryptographicHash::Sha256);
    for (const QString& key : sortedKeys(transforms))
    {
        hash.addData(key.toUtf8());
        const auto provenance = provenanceByFrame.find(key);
        hash.addData(fingerprintSourceDistortion(provenance != provenanceByFrame.end() ? provenance->second : inherit));
    }
    return QString::fromLatin1(hash.result().toHex());
}

QString
fingerprintSourceDistortions(const std::unordered_map<QString, an::RegistrationTransform>& transforms,
                             const std::unordered_map<QString, SourceDistortionProvenance>& provenanceByFrame,
                             const std::unordered_map<QString, std::optional<an::SipDistortion>>& resolvedByFrame)
{
    const SourceDistortionProvenance inherit = SourceDistortionProvenance::inheritMetadata();
    QCryptographicHash hash(QCryptographicHash::Sha256);
    for (const QString& key : sortedKeys(transforms))
    {
        hash.addData(key.toUtf8());
        const auto provenanceIt = provenanceByFrame.find(key);
        const SourceDistortionProvenance& provenance =
            provenanceIt != provenanceByFrame.end() ? provenanceIt->second : inherit;
        const auto resolvedIt = resolvedByFrame.find(key);
        const std::optional<an::SipDistortion>& resolved =
            resolvedIt != resolvedByFrame.end() ? resolvedIt->second : provenance.model;
        hash.addData(fingerprintSourceDistortion(provenance.policy, resolved));
    }
    return QString::fromLatin1(hash.result().toHex());
}

bool validateOutputPlaneWcsDescriptor(const OutputPlaneWcsDescriptorInfo& info, const QString& expectedReferenceFrameId,
                                      const QString& expectedAlignmentMode, const int expectedWidth,
                                      const int expectedHeight, const std::optional<bool>& expectedSipCorrection,
                                      QString& error)
{
    const QString sourceKind = info.sourceKind.trimmed().toLower();
    const QString alignmentMode = info.alignmentMode.trimmed().toLower();
    const QString expectedMode = expectedAlignmentMode.trimmed().toLower();
    if (alignmentMode.isEmpty() || alignmentMode != expectedMode)
    {
        error = QStringLiteral("Output-plane WCS alignment mode does not match registration.");
        return false;
    }
    if ((expectedWidth > 0 && info.width != expectedWidth) || (expectedHeight > 0 && info.height != expectedHeight))
    {
        error = QStringLiteral("Output-plane WCS dimensions do not match the registered canvas.");
        return false;
    }
    if (expectedSipCorrection.has_value() && info.sipCorrectionApplied != *expectedSipCorrection)
    {
        error = QStringLiteral("Output-plane WCS SIP policy does not match registration.");
        return false;
    }
    if (sourceKind == QStringLiteral("astrometric_mosaic_canvas"))
    {
        if (expectedMode != QStringLiteral("mosaic") || !info.sourceFrameId.trimmed().isEmpty())
        {
            error = QStringLiteral("Output-plane WCS mosaic source identity is invalid.");
            return false;
        }
        return true;
    }
    if (sourceKind != QStringLiteral("reference_frame") || expectedReferenceFrameId.trimmed().isEmpty() ||
        info.sourceFrameId.trimmed().isEmpty())
    {
        error = QStringLiteral("Output-plane WCS reference source identity is invalid.");
        return false;
    }
    try
    {
        if (FrameId(info.sourceFrameId).identityKey() != FrameId(expectedReferenceFrameId).identityKey())
        {
            error = QStringLiteral("Output-plane WCS source frame does not match the registration reference.");
            return false;
        }
    }
    catch (...)
    {
        error = QStringLiteral("Output-plane WCS reference source identity is invalid.");
        return false;
    }
    return true;
}

bool isOutputWcsKeyword(const QString& key)
{
    const QString normalized = key.trimmed().toUpper();
    static const QStringList exactKeys = {
        QStringLiteral("EQUINOX"),  QStringLiteral("RADESYS"),  QStringLiteral("LONPOLE"), QStringLiteral("LATPOLE"),
        QStringLiteral("WCSAXES"),  QStringLiteral("WCSNAME"),  QStringLiteral("A_ORDER"), QStringLiteral("B_ORDER"),
        QStringLiteral("AP_ORDER"), QStringLiteral("BP_ORDER"), QStringLiteral("A_DMAX"),  QStringLiteral("B_DMAX"),
        QStringLiteral("AP_DMAX"),  QStringLiteral("BP_DMAX"),  QStringLiteral("CROTA1"),  QStringLiteral("CROTA2")};
    if (exactKeys.contains(normalized))
    {
        return true;
    }
    static const QStringList prefixes = {QStringLiteral("CTYPE"), QStringLiteral("CRVAL"), QStringLiteral("CRPIX"),
                                         QStringLiteral("CD1_"),  QStringLiteral("CD2_"),  QStringLiteral("PC"),
                                         QStringLiteral("CDELT"), QStringLiteral("CUNIT"), QStringLiteral("CROTA"),
                                         QStringLiteral("PV"),    QStringLiteral("PS")};
    for (const QString& prefix : prefixes)
    {
        if (normalized.startsWith(prefix))
        {
            return true;
        }
    }
    if (normalized.size() >= 3 &&
        (normalized.startsWith(QStringLiteral("A_")) || normalized.startsWith(QStringLiteral("B_"))))
    {
        return true;
    }
    return normalized.size() >= 4 &&
           (normalized.startsWith(QStringLiteral("AP_")) || normalized.startsWith(QStringLiteral("BP_")));
}

QJsonArray serializeOutputPlaneWcsKeywords(const std::span<const an::CodecCard> cards)
{
    QJsonArray keywords;
    for (const an::CodecCard& card : cards)
    {
        const QString key = QString::fromStdString(card.keyword());
        if (card.origin() != an::CodecNamespace::Fits || !isOutputWcsKeyword(key))
        {
            continue;
        }
        QJsonObject entry;
        entry.insert(QStringLiteral("key"), key);
        entry.insert(QStringLiteral("value"), QString::fromStdString(card.value()));
        entry.insert(QStringLiteral("comment"), QString::fromStdString(card.comment()));
        keywords.append(entry);
    }
    return keywords;
}

QString serializeOutputPlaneWcsJson(const std::span<const an::CodecCard> cards,
                                    const OutputPlaneWcsDescriptorInfo& info)
{
    QJsonObject object;
    object.insert(QStringLiteral("schemaVersion"), kOutputPlaneWcsSchemaVersion);
    object.insert(QStringLiteral("sourceKind"), info.sourceKind);
    object.insert(QStringLiteral("sourceFrameId"), info.sourceFrameId);
    object.insert(QStringLiteral("alignmentMode"), info.alignmentMode);
    object.insert(QStringLiteral("width"), info.width);
    object.insert(QStringLiteral("height"), info.height);
    object.insert(QStringLiteral("sipCorrectionApplied"), info.sipCorrectionApplied);
    object.insert(QStringLiteral("wcsKeywords"), serializeOutputPlaneWcsKeywords(cards));
    return QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact));
}

bool parseOutputPlaneWcsJson(const QString& json, std::vector<an::CodecCard>* outCards,
                             OutputPlaneWcsDescriptorInfo* outInfo, QString& error)
{
    if (json.trimmed().isEmpty())
    {
        error = QStringLiteral("Output-plane WCS descriptor is empty.");
        return false;
    }
    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(json.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
    {
        error = QStringLiteral("Failed to parse output-plane WCS descriptor.");
        return false;
    }
    const QJsonObject object = document.object();
    if (object.value(QStringLiteral("schemaVersion")).toInt(-1) != kOutputPlaneWcsSchemaVersion)
    {
        error = QStringLiteral("Output-plane WCS schema version is not supported.");
        return false;
    }
    const int width = object.value(QStringLiteral("width")).toInt(0);
    const int height = object.value(QStringLiteral("height")).toInt(0);
    if (width <= 0 || height <= 0)
    {
        error = QStringLiteral("Output-plane WCS dimensions are invalid.");
        return false;
    }
    const QJsonArray keywords = object.value(QStringLiteral("wcsKeywords")).toArray();
    if (keywords.isEmpty())
    {
        error = QStringLiteral("Output-plane WCS descriptor contains no WCS keywords.");
        return false;
    }

    std::vector<an::CodecCard> cards;
    cards.reserve(static_cast<std::size_t>(keywords.size()));
    for (const QJsonValue& value : keywords)
    {
        const QJsonObject entry = value.toObject();
        const QString key = entry.value(QStringLiteral("key")).toString().trimmed();
        if (key.isEmpty())
        {
            error = QStringLiteral("Output-plane WCS keyword entry is invalid.");
            return false;
        }
        auto card = an::CodecCard::create(an::CodecNamespace::Fits, key.toStdString(),
                                          entry.value(QStringLiteral("value")).toString().trimmed().toStdString(),
                                          entry.value(QStringLiteral("comment")).toString().trimmed().toStdString());
        if (!card)
        {
            error = QStringLiteral("Output-plane WCS keyword %1 is invalid: %2").arg(key, describe(card.status()));
            return false;
        }
        cards.push_back(std::move(card).value());
    }
    if (outCards != nullptr)
    {
        *outCards = std::move(cards);
    }
    if (outInfo != nullptr)
    {
        outInfo->sourceKind = object.value(QStringLiteral("sourceKind")).toString();
        outInfo->sourceFrameId = object.value(QStringLiteral("sourceFrameId")).toString();
        outInfo->alignmentMode = object.value(QStringLiteral("alignmentMode")).toString();
        outInfo->width = width;
        outInfo->height = height;
        outInfo->sipCorrectionApplied = object.value(QStringLiteral("sipCorrectionApplied")).toBool(false);
    }
    return true;
}

bool supportsEmbeddedWcsOutput(const QString& path)
{
    const QString suffix = QFileInfo(path).suffix().trimmed().toLower();
    return suffix == QStringLiteral("fits") || suffix == QStringLiteral("fit") || suffix == QStringLiteral("fts") ||
           suffix == QStringLiteral("xisf");
}

bool parseKernel(const QString& kernel, an::DrizzleKernel& out)
{
    const auto value =
        ImageIntegration::persistedChoiceFromText(ImageIntegration::PersistedChoice::DrizzleKernel, kernel);
    const auto mapped = value.known ? ImageIntegration::drizzleKernelForKey(value.key) : std::nullopt;
    if (!mapped.has_value())
    {
        return false;
    }
    out = *mapped;
    return true;
}

bool parseBayerPattern(const QString& pattern, std::optional<an::CfaPattern>& out)
{
    const auto value =
        ImageIntegration::persistedChoiceFromText(ImageIntegration::PersistedChoice::CfaPattern, pattern);
    if (!value.known)
    {
        return false;
    }
    out = ImageIntegration::cfaPatternForKey(value.key);
    return true;
}

std::optional<an::DemosaicMethod> parseDemosaicMethod(const QString& method)
{
    const auto value =
        ImageIntegration::persistedChoiceFromText(ImageIntegration::PersistedChoice::DemosaicMethod, method);
    return value.known ? ImageIntegration::demosaicMethodForKey(value.key) : std::nullopt;
}

std::optional<an::KernelInterpolation> parseInterpolation(const QString& interpolation)
{
    const auto value =
        ImageIntegration::persistedChoiceFromText(ImageIntegration::PersistedChoice::Interpolation, interpolation);
    return value.known ? ImageIntegration::interpolationForKey(value.key) : std::nullopt;
}

ImageIntegration::RegistrationCanvasMode parseAlignmentMode(const QString& mode)
{
    const auto value =
        ImageIntegration::persistedChoiceFromText(ImageIntegration::PersistedChoice::AlignmentMode, mode);
    const auto mapped = value.known ? ImageIntegration::canvasModeForKey(value.key) : std::nullopt;
    return mapped.value_or(ImageIntegration::RegistrationCanvasMode::Standard);
}

QString alignmentModeKey(const ImageIntegration::RegistrationCanvasMode mode)
{
    switch (mode)
    {
    case ImageIntegration::RegistrationCanvasMode::Standard:
        return QStringLiteral("standard");
    case ImageIntegration::RegistrationCanvasMode::Mosaic:
        return QStringLiteral("mosaic");
    case ImageIntegration::RegistrationCanvasMode::Intersection:
        return QStringLiteral("intersection");
    }
    return QStringLiteral("standard");
}

} // namespace stage_utils
