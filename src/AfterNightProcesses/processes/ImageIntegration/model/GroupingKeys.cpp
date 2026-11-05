// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "GroupingKeys.h"

#include <QDate>
#include <QJsonObject>
#include <QStringList>

#include <cmath>
#include <utility>

namespace
{
const QString kMissingValue = QStringLiteral("-");
const QString kUnknownObject = QStringLiteral("(Unknown Object)");
constexpr int kGroupKeyJsonVersion = 1;

QString trimTrailingZeros(QString value)
{
    while (value.contains(QLatin1Char('.')) && value.endsWith(QLatin1Char('0')))
    {
        value.chop(1);
    }

    if (value.endsWith(QLatin1Char('.')))
    {
        value.chop(1);
    }

    return value;
}

QString formatDouble(double value, int precision = 3)
{
    return trimTrailingZeros(QString::number(value, 'f', precision));
}

QString formatOptionalInt(const std::optional<int>& value)
{
    return value.has_value() ? QString::number(value.value()) : kMissingValue;
}

QString formatOptionalDouble(const std::optional<double>& value)
{
    return value.has_value() ? formatDouble(value.value()) : kMissingValue;
}

QString normalizeText(const QString& value)
{
    return value.isEmpty() ? kMissingValue : value;
}

QString normalizeObjectName(const QString& value)
{
    const QString trimmed = value.trimmed();
    if (trimmed.isEmpty() || trimmed == kMissingValue)
    {
        return kUnknownObject;
    }

    return trimmed;
}

QString formatTemperature(double value, double tolerance)
{
    if (tolerance > 0.0)
    {
        const double snapped = std::round(value / tolerance) * tolerance;
        return formatDouble(snapped, 2);
    }

    return formatDouble(value, 2);
}

QString formatExposure(double value, double tolerance)
{
    if (tolerance > 0.0)
    {
        const double snapped = std::round(value / tolerance) * tolerance;
        return formatDouble(snapped, 3);
    }

    return formatDouble(value, 3);
}

GroupKey buildGroupKey(const QStringList& parts)
{
    return GroupKey{parts.join(QLatin1Char('|'))};
}
} // namespace

QString GroupKey::toString() const
{
    return value;
}

QString GroupKey::toCompactString() const
{
    // Keep the underlying serialized key stable, but avoid showing placeholder
    // tokens ("-") in any user-facing contexts.
    const QStringList parts = value.split(QLatin1Char('|'), Qt::KeepEmptyParts);

    QStringList kept;
    kept.reserve(parts.size());

    for (const QString& part : parts)
    {
        const QString trimmed = part.trimmed();
        if (trimmed.isEmpty() || trimmed == kMissingValue)
        {
            continue;
        }
        kept << trimmed;
    }

    return kept.join(QLatin1Char('|'));
}

QString GroupKey::toDisplayString(const GroupingOptions& options, FrameType frameType) const
{
    // Parse the pipe-separated values
    const QStringList parts = value.split(QLatin1Char('|'));

    QStringList displayParts;
    displayParts.reserve(8);

    // The order and meaning of parts depends on frame type:
    // Bias: resolution, isoGain, offset, readoutMode, [customTag]
    // Dark: resolution, isoGain, offset, exposure, temperature, [customTag]
    // Flat: resolution, isoGain, offset, filter, object, session, [customTag]
    // Light: resolution, isoGain, offset, exposure, filter, object, session, [customTag]

    auto addIfValid = [&](const QString& val) {
        if (!val.isEmpty() && val != kMissingValue)
        {
            displayParts << val;
        }
    };

    // Resolution is always included if byResolution is enabled (index 0)
    if (options.byResolution && parts.size() > 0)
    {
        addIfValid(parts[0]);
    }

    // ISO/Gain (index 1)
    if (options.byIsoGain && parts.size() > 1)
    {
        const QString isoVal = parts[1];
        if (!isoVal.isEmpty() && isoVal != kMissingValue)
        {
            displayParts << QStringLiteral("ISO %1").arg(isoVal);
        }
    }

    // Offset (index 2)
    if (options.byIsoGain && options.byOffset && parts.size() > 2)
    {
        const QString offsetVal = parts[2];
        if (!offsetVal.isEmpty() && offsetVal != kMissingValue)
        {
            displayParts << QStringLiteral("Offset %1").arg(offsetVal);
        }
    }

    // Frame-type specific fields
    switch (frameType)
    {
    case FrameType::Bias:
        // Bias has: resolution, isoGain, offset, readoutMode, [customTag]
        // CustomTag at index 4 if present
        if (options.byCustomTag && parts.size() > 4)
        {
            addIfValid(parts[4]);
        }
        break;

    case FrameType::Dark:
        // Dark has: resolution, isoGain, offset, exposure, temperature, [customTag]
        if (options.byExposure && parts.size() > 3)
        {
            const QString expVal = parts[3];
            if (!expVal.isEmpty() && expVal != kMissingValue)
            {
                const QString prefix =
                    options.exposureToleranceSeconds > 0.0 ? QStringLiteral("~%1s") : QStringLiteral("%1s");
                displayParts << prefix.arg(expVal);
            }
        }
        if (options.byTemperature && parts.size() > 4)
        {
            const QString tempVal = parts[4];
            if (!tempVal.isEmpty() && tempVal != kMissingValue)
            {
                const QString prefix =
                    options.temperatureToleranceC > 0.0 ? QStringLiteral("Temp ~%1C") : QStringLiteral("Temp %1C");
                displayParts << prefix.arg(tempVal);
            }
        }
        if (options.byCustomTag && parts.size() > 5)
        {
            addIfValid(parts[5]);
        }
        break;

    case FrameType::Flat:
        // Flat has: resolution, isoGain, offset, filter, object, session, [customTag]
        if (options.byFilter && parts.size() > 3)
        {
            addIfValid(parts[3]);
        }
        if (options.byObject && parts.size() > 4)
        {
            addIfValid(parts[4]);
        }
        if (options.bySession && parts.size() > 5)
        {
            addIfValid(parts[5]);
        }
        if (options.byCustomTag && parts.size() > 6)
        {
            addIfValid(parts[6]);
        }
        break;

    case FrameType::Light:
        // Light has: resolution, isoGain, offset, exposure, filter, object, session, [customTag]
        if (options.byExposure && parts.size() > 3)
        {
            const QString expVal = parts[3];
            if (!expVal.isEmpty() && expVal != kMissingValue)
            {
                const QString prefix =
                    options.exposureToleranceSeconds > 0.0 ? QStringLiteral("~%1s") : QStringLiteral("%1s");
                displayParts << prefix.arg(expVal);
            }
        }
        if (options.byFilter && parts.size() > 4)
        {
            addIfValid(parts[4]);
        }
        if (options.byObject && parts.size() > 5)
        {
            addIfValid(parts[5]);
        }
        if (options.bySession && parts.size() > 6)
        {
            addIfValid(parts[6]);
        }
        if (options.byCustomTag && parts.size() > 7)
        {
            addIfValid(parts[7]);
        }
        break;
    }

    // If no parts were added, return a generic label
    if (displayParts.isEmpty())
    {
        return QStringLiteral("Group");
    }

    return displayParts.join(QStringLiteral(" | "));
}

std::optional<GroupKey> GroupKey::fromString(const QString& value)
{
    if (value.trimmed().isEmpty())
    {
        return std::nullopt;
    }

    return GroupKey{value};
}

QJsonValue GroupKey::toJson() const
{
    QJsonObject obj;
    obj.insert(QStringLiteral("v"), kGroupKeyJsonVersion);
    obj.insert(QStringLiteral("value"), value);
    return obj;
}

std::optional<GroupKey> GroupKey::fromJson(const QJsonValue& value)
{
    if (value.isString())
    {
        return GroupKey::fromString(value.toString());
    }

    if (!value.isObject())
    {
        return std::nullopt;
    }

    const QJsonObject obj = value.toObject();
    const QString raw = obj.value(QStringLiteral("value")).toString();
    return GroupKey::fromString(raw);
}

void GroupKey::parseMetadata(const GroupKey& key, FrameType type, const GroupingOptions& options, QString* outObject,
                             QString* outSession)
{
    const QStringList parts = key.value.split(QLatin1Char('|'));

    if (outObject)
        *outObject = QString();
    if (outSession)
        *outSession = QString();

    if (type == FrameType::Light)
    {
        if (parts.size() > 5 && options.byObject && parts[5] != kMissingValue && parts[5] != kUnknownObject)
        {
            if (outObject)
                *outObject = parts[5];
        }
        if (parts.size() > 6 && options.bySession && parts[6] != kMissingValue)
        {
            if (outSession)
                *outSession = parts[6];
        }
    }
    else if (type == FrameType::Flat)
    {
        if (parts.size() > 4 && options.byObject && parts[4] != kMissingValue && parts[4] != kUnknownObject)
        {
            if (outObject)
                *outObject = parts[4];
        }
        if (parts.size() > 5 && options.bySession && parts[5] != kMissingValue)
        {
            if (outSession)
                *outSession = parts[5];
        }
    }
}

bool operator==(const GroupKey& lhs, const GroupKey& rhs) noexcept
{
    return lhs.value == rhs.value;
}

bool operator!=(const GroupKey& lhs, const GroupKey& rhs) noexcept
{
    return !(lhs == rhs);
}

bool operator<(const GroupKey& lhs, const GroupKey& rhs) noexcept
{
    return lhs.value < rhs.value;
}

size_t qHash(const GroupKey& key, size_t seed) noexcept
{
    return qHash(key.value, seed);
}

FrameInfo::FrameInfo(FrameId id, QString displayPath, FrameMetadata metadata, QString userSession, QString customTag,
                     bool excluded, std::optional<GroupKey> manualGroupKey, QString userObjectName)
    : id(std::move(id)), displayPath(std::move(displayPath)), metadata(std::move(metadata)),
      userSession(std::move(userSession)), userObjectName(std::move(userObjectName)), customTag(std::move(customTag)),
      excluded(excluded), manualGroupKey(std::move(manualGroupKey))
{
}

FrameType FrameInfo::type() const noexcept
{
    return metadata.type();
}

QString FrameInfo::effectiveSession() const
{
    if (!userSession.isEmpty())
    {
        return userSession;
    }

    if (!metadata.capturedAtUtc().isValid())
    {
        return QString();
    }

    return metadata.capturedAtUtc().date().toString(QStringLiteral("yyyy-MM-dd"));
}

QString FrameInfo::effectiveObjectName() const
{
    if (!userObjectName.trimmed().isEmpty())
    {
        return userObjectName;
    }

    return metadata.objectName();
}

BiasGroupKey BiasGroupKey::fromFrame(const FrameInfo& frame)
{
    BiasGroupKey key;
    key.resolution = frame.metadata.resolutionString();
    key.isoGain = frame.metadata.isoGain();
    key.offset = frame.metadata.offset();
    key.readoutMode = frame.metadata.readoutMode();
    key.customTag = frame.customTag;
    return key;
}

GroupKey BiasGroupKey::toGroupKey(const GroupingOptions& options) const
{
    QStringList parts;
    parts.reserve(options.byCustomTag ? 5 : 4);
    parts << normalizeText(resolution);
    if (options.byIsoGain)
    {
        parts << formatOptionalInt(isoGain);
    }
    else
    {
        parts << kMissingValue;
    }

    if (options.byIsoGain && options.byOffset)
    {
        parts << formatOptionalInt(offset);
    }
    else
    {
        parts << kMissingValue;
    }

    // Readout mode is intentionally not part of grouping (kept as a placeholder for stable key shape).
    parts << kMissingValue;
    if (options.byCustomTag)
    {
        parts << normalizeText(customTag);
    }

    return buildGroupKey(parts);
}

DarkGroupKey DarkGroupKey::fromFrame(const FrameInfo& frame)
{
    DarkGroupKey key;
    key.resolution = frame.metadata.resolutionString();
    key.isoGain = frame.metadata.isoGain();
    key.offset = frame.metadata.offset();
    key.exposureSeconds = frame.metadata.exposureSeconds();
    key.temperatureC = frame.metadata.temperatureC();
    key.customTag = frame.customTag;
    return key;
}

GroupKey DarkGroupKey::toGroupKey(const GroupingOptions& options) const
{
    QStringList parts;
    parts.reserve(options.byCustomTag ? 6 : 5);
    parts << normalizeText(resolution);
    if (options.byIsoGain)
    {
        parts << formatOptionalInt(isoGain);
    }
    else
    {
        parts << kMissingValue;
    }

    if (options.byIsoGain && options.byOffset)
    {
        parts << formatOptionalInt(offset);
    }
    else
    {
        parts << kMissingValue;
    }
    if (options.byExposure)
    {
        parts << formatExposure(exposureSeconds, options.exposureToleranceSeconds);
    }
    else
    {
        parts << kMissingValue;
    }
    if (options.byTemperature)
    {
        if (temperatureC.has_value())
        {
            parts << formatTemperature(temperatureC.value(), options.temperatureToleranceC);
        }
        else
        {
            parts << kMissingValue;
        }
    }
    else
    {
        parts << kMissingValue;
    }
    if (options.byCustomTag)
    {
        parts << normalizeText(customTag);
    }

    return buildGroupKey(parts);
}

FlatGroupKey FlatGroupKey::fromFrame(const FrameInfo& frame)
{
    FlatGroupKey key;
    key.resolution = frame.metadata.resolutionString();
    key.isoGain = frame.metadata.isoGain();
    key.offset = frame.metadata.offset();
    key.filter = frame.metadata.filter();
    key.objectName = frame.effectiveObjectName();
    key.session = frame.effectiveSession();
    key.customTag = frame.customTag;
    return key;
}

GroupKey FlatGroupKey::toGroupKey(const GroupingOptions& options) const
{
    QStringList parts;
    parts.reserve(options.byCustomTag ? 7 : 6);
    parts << normalizeText(resolution);
    if (options.byIsoGain)
    {
        parts << formatOptionalInt(isoGain);
    }
    else
    {
        parts << kMissingValue;
    }
    if (options.byIsoGain && options.byOffset)
    {
        parts << formatOptionalInt(offset);
    }
    else
    {
        parts << kMissingValue;
    }
    if (options.byFilter)
    {
        parts << normalizeText(filter);
    }
    else
    {
        parts << kMissingValue;
    }
    if (options.byObject)
    {
        parts << normalizeObjectName(objectName);
    }
    else
    {
        parts << kMissingValue;
    }
    if (options.bySession)
    {
        parts << normalizeText(session);
    }
    else
    {
        parts << kMissingValue;
    }
    if (options.byCustomTag)
    {
        parts << normalizeText(customTag);
    }

    return buildGroupKey(parts);
}

LightGroupKey LightGroupKey::fromFrame(const FrameInfo& frame)
{
    LightGroupKey key;
    key.resolution = frame.metadata.resolutionString();
    key.isoGain = frame.metadata.isoGain();
    key.offset = frame.metadata.offset();
    key.exposureSeconds = frame.metadata.exposureSeconds();
    key.filter = frame.metadata.filter();
    key.objectName = frame.effectiveObjectName();
    key.session = frame.effectiveSession();
    key.customTag = frame.customTag;
    return key;
}

RegistrationGroupKey RegistrationGroupKey::fromFrame(const FrameInfo& frame)
{
    RegistrationGroupKey key;
    key.resolution = normalizeText(frame.metadata.resolutionString());
    key.objectName = normalizeObjectName(frame.effectiveObjectName());
    return key;
}

QString RegistrationGroupKey::toString() const
{
    return QStringLiteral("%1 | %2").arg(resolution, objectName);
}

bool operator==(const RegistrationGroupKey& lhs, const RegistrationGroupKey& rhs) noexcept
{
    return lhs.resolution == rhs.resolution && lhs.objectName == rhs.objectName;
}

bool operator!=(const RegistrationGroupKey& lhs, const RegistrationGroupKey& rhs) noexcept
{
    return !(lhs == rhs);
}

bool operator<(const RegistrationGroupKey& lhs, const RegistrationGroupKey& rhs) noexcept
{
    if (lhs.resolution != rhs.resolution)
    {
        return lhs.resolution < rhs.resolution;
    }
    return lhs.objectName < rhs.objectName;
}

GroupKey LightGroupKey::toGroupKey(const GroupingOptions& options) const
{
    QStringList parts;
    parts.reserve(options.byCustomTag ? 8 : 7);
    parts << normalizeText(resolution);
    if (options.byIsoGain)
    {
        parts << formatOptionalInt(isoGain);
    }
    else
    {
        parts << kMissingValue;
    }
    if (options.byIsoGain && options.byOffset)
    {
        parts << formatOptionalInt(offset);
    }
    else
    {
        parts << kMissingValue;
    }
    if (options.byExposure)
    {
        parts << formatExposure(exposureSeconds, options.exposureToleranceSeconds);
    }
    else
    {
        parts << kMissingValue;
    }
    if (options.byFilter)
    {
        parts << normalizeText(filter);
    }
    else
    {
        parts << kMissingValue;
    }
    if (options.byObject)
    {
        parts << normalizeObjectName(objectName);
    }
    else
    {
        parts << kMissingValue;
    }
    if (options.bySession)
    {
        parts << normalizeText(session);
    }
    else
    {
        parts << kMissingValue;
    }
    if (options.byCustomTag)
    {
        parts << normalizeText(customTag);
    }

    return buildGroupKey(parts);
}

CalibrationGroupKey CalibrationGroupKey::fromLightKey(const LightGroupKey& lightKey)
{
    CalibrationGroupKey key;
    key.lightKey = lightKey;
    return key;
}

bool CalibrationGroupKey::matchesBias(const BiasGroupKey& biasKey, const GroupingOptions& options) const
{
    if (biasKey.resolution != lightKey.resolution)
    {
        return false;
    }

    if (options.byIsoGain && biasKey.isoGain != lightKey.isoGain)
    {
        return false;
    }

    if (options.byIsoGain && options.byOffset && biasKey.offset != lightKey.offset)
    {
        return false;
    }

    if (options.byCustomTag && biasKey.customTag != lightKey.customTag)
    {
        return false;
    }

    return true;
}

bool CalibrationGroupKey::matchesDark(const DarkGroupKey& darkKey, const GroupingOptions& options) const
{
    if (darkKey.resolution != lightKey.resolution)
    {
        return false;
    }

    if (options.byIsoGain && darkKey.isoGain != lightKey.isoGain)
    {
        return false;
    }

    if (options.byIsoGain && options.byOffset && darkKey.offset != lightKey.offset)
    {
        return false;
    }

    if (options.byExposure && formatExposure(darkKey.exposureSeconds, options.exposureToleranceSeconds) !=
                                  formatExposure(lightKey.exposureSeconds, options.exposureToleranceSeconds))
    {
        return false;
    }

    if (options.byCustomTag && darkKey.customTag != lightKey.customTag)
    {
        return false;
    }

    return true;
}

bool CalibrationGroupKey::matchesFlat(const FlatGroupKey& flatKey, const GroupingOptions& options) const
{
    if (flatKey.resolution != lightKey.resolution)
    {
        return false;
    }

    if (options.byIsoGain && flatKey.isoGain != lightKey.isoGain)
    {
        return false;
    }

    if (options.byIsoGain && options.byOffset && flatKey.offset != lightKey.offset)
    {
        return false;
    }

    if (options.byFilter && flatKey.filter != lightKey.filter)
    {
        return false;
    }

    if (options.byObject && flatKey.objectName != lightKey.objectName)
    {
        return false;
    }

    if (options.bySession && flatKey.session != lightKey.session)
    {
        return false;
    }

    if (options.byCustomTag && flatKey.customTag != lightKey.customTag)
    {
        return false;
    }

    return true;
}

QString IntegrationGroupKey::toString() const
{
    return key.toString();
}

QString IntegrationGroupKey::toCompactString() const
{
    return key.toCompactString();
}

bool operator==(const IntegrationGroupKey& lhs, const IntegrationGroupKey& rhs) noexcept
{
    return lhs.key == rhs.key;
}

bool operator!=(const IntegrationGroupKey& lhs, const IntegrationGroupKey& rhs) noexcept
{
    return !(lhs == rhs);
}

bool operator<(const IntegrationGroupKey& lhs, const IntegrationGroupKey& rhs) noexcept
{
    return lhs.key < rhs.key;
}

size_t qHash(const IntegrationGroupKey& key, size_t seed) noexcept
{
    return qHash(key.key, seed);
}
