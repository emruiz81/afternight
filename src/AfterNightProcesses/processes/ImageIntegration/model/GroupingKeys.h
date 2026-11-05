// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightProcessesExport.h"

/**
 * @file GroupingKeys.h
 * @brief Grouping key types for frame classification and matching.
 *
 * This header defines the hierarchical key types used to organize frames into
 * logical groups. Each frame type (Bias, Dark, Flat, Light) has a specialized
 * key type that captures the relevant metadata for that category.
 *
 * @par Key Hierarchy
 * - GroupKey: Generic string-based composite key
 * - BiasGroupKey: Resolution, ISO/gain, offset, readout mode
 * - DarkGroupKey: Adds exposure and temperature to bias criteria
 * - FlatGroupKey: Adds filter, object, and session to bias criteria
 * - LightGroupKey: Full criteria including all matching fields
 * - CalibrationGroupKey: Used to find matching calibration frames
 * - IntegrationGroupKey: Final stacking group identity
 *
 * @see GroupingEngine, GroupingOptions, FrameInfo
 */

#include "FrameId.h"
#include "FrameMetadata.h"
#include "GroupingOptions.h"

#include <QDateTime>
#include <QHash>
#include <QJsonValue>
#include <QString>

#include <optional>

#include <verdigris/wobjectdefs.h>

/**
 * @brief Composite key used for grouping frames.
 *
 * GroupKey provides a normalized string representation of a frame's group
 * membership. The string format is deterministic and suitable for use as
 * a map key or for serialization.
 */
struct AFTERNIGHTPROCESSES_API GroupKey
{
    QString value;

    [[nodiscard]] QString toString() const;
    // Compact, pipe-separated representation intended for display/logging.
    // Drops placeholder tokens ("-") and empty tokens while preserving ordering.
    [[nodiscard]] QString toCompactString() const;
    [[nodiscard]] static std::optional<GroupKey> fromString(const QString& value);

    [[nodiscard]] QJsonValue toJson() const;
    [[nodiscard]] static std::optional<GroupKey> fromJson(const QJsonValue& value);

    /**
     * @brief Extracts metadata values from a serialized group key.
     *
     * This is the inverse of toGroupKey methods. It extracts original string values
     * for object name and session from the pipe-separated key, which is used
     * for drag-and-drop operations to identify the target metadata.
     */
    static void parseMetadata(const GroupKey& key, FrameType type, const GroupingOptions& options, QString* outObject,
                              QString* outSession);

    /**
     * @brief Creates a human-readable display string showing only enabled grouping fields.
     *
     * Unlike toString() which returns the raw pipe-separated value, this method
     * creates a clean label showing only the fields that are enabled in the
     * grouping options, making it suitable for UI display in tree views.
     *
     * @param options The grouping options determining which fields to display.
     * @param frameType The type of frame (affects which fields are relevant).
     * @return A display-friendly string like "3008x3004 | Blue | M 83"
     */
    [[nodiscard]] QString toDisplayString(const GroupingOptions& options, FrameType frameType) const;
};

[[nodiscard]] AFTERNIGHTPROCESSES_API bool operator==(const GroupKey& lhs, const GroupKey& rhs) noexcept;
[[nodiscard]] AFTERNIGHTPROCESSES_API bool operator!=(const GroupKey& lhs, const GroupKey& rhs) noexcept;
[[nodiscard]] AFTERNIGHTPROCESSES_API bool operator<(const GroupKey& lhs, const GroupKey& rhs) noexcept;

[[nodiscard]] AFTERNIGHTPROCESSES_API size_t qHash(const GroupKey& key, size_t seed = 0) noexcept;

namespace std
{
template <> struct hash<GroupKey>
{
    size_t operator()(const GroupKey& key) const noexcept
    {
        return qHash(key);
    }
};
} // namespace std

/// Frame record used by grouping logic.
struct AFTERNIGHTPROCESSES_API FrameInfo
{
    FrameId id;
    QString displayPath;
    FrameMetadata metadata;
    QString userSession;
    QString userObjectName;
    QString customTag;
    bool excluded = false;
    std::optional<GroupKey> manualGroupKey;

    FrameInfo(FrameId id, QString displayPath, FrameMetadata metadata, QString userSession = {}, QString customTag = {},
              bool excluded = false, std::optional<GroupKey> manualGroupKey = std::nullopt,
              QString userObjectName = {});

    [[nodiscard]] FrameType type() const noexcept;
    [[nodiscard]] QString effectiveSession() const;
    [[nodiscard]] QString effectiveObjectName() const;
};

/// Group key for bias frames.
struct AFTERNIGHTPROCESSES_API BiasGroupKey
{
    QString resolution;
    std::optional<int> isoGain;
    std::optional<int> offset;
    QString readoutMode;
    QString customTag;

    [[nodiscard]] static BiasGroupKey fromFrame(const FrameInfo& frame);
    [[nodiscard]] GroupKey toGroupKey(const GroupingOptions& options) const;
};

/// Group key for dark frames.
struct AFTERNIGHTPROCESSES_API DarkGroupKey
{
    QString resolution;
    std::optional<int> isoGain;
    std::optional<int> offset;
    double exposureSeconds = 0.0;
    std::optional<double> temperatureC;
    QString customTag;

    [[nodiscard]] static DarkGroupKey fromFrame(const FrameInfo& frame);
    [[nodiscard]] GroupKey toGroupKey(const GroupingOptions& options) const;
};

/// Group key for flat frames.
struct AFTERNIGHTPROCESSES_API FlatGroupKey
{
    QString resolution;
    std::optional<int> isoGain;
    std::optional<int> offset;
    QString filter;
    QString objectName;
    QString session;
    QString customTag;

    [[nodiscard]] static FlatGroupKey fromFrame(const FrameInfo& frame);
    [[nodiscard]] GroupKey toGroupKey(const GroupingOptions& options) const;
};

/// Group key for light frames.
struct AFTERNIGHTPROCESSES_API LightGroupKey
{
    QString resolution;
    std::optional<int> isoGain;
    std::optional<int> offset;
    double exposureSeconds = 0.0;
    QString filter;
    QString objectName;
    QString session;
    QString customTag;

    [[nodiscard]] static LightGroupKey fromFrame(const FrameInfo& frame);
    [[nodiscard]] GroupKey toGroupKey(const GroupingOptions& options) const;
};

/// Group key for registration grouping (Resolution + Object).
struct AFTERNIGHTPROCESSES_API RegistrationGroupKey
{
    QString resolution;
    QString objectName;

    [[nodiscard]] static RegistrationGroupKey fromFrame(const FrameInfo& frame);
    [[nodiscard]] QString toString() const;
};

[[nodiscard]] AFTERNIGHTPROCESSES_API bool operator==(const RegistrationGroupKey& lhs,
                                                      const RegistrationGroupKey& rhs) noexcept;
[[nodiscard]] bool operator!=(const RegistrationGroupKey& lhs, const RegistrationGroupKey& rhs) noexcept;
[[nodiscard]] bool operator<(const RegistrationGroupKey& lhs, const RegistrationGroupKey& rhs) noexcept;

/// Group key used to match lights to calibration masters.
struct AFTERNIGHTPROCESSES_API CalibrationGroupKey
{
    LightGroupKey lightKey;

    [[nodiscard]] static CalibrationGroupKey fromLightKey(const LightGroupKey& lightKey);
    [[nodiscard]] bool matchesBias(const BiasGroupKey& biasKey, const GroupingOptions& options) const;
    [[nodiscard]] bool matchesDark(const DarkGroupKey& darkKey, const GroupingOptions& options) const;
    [[nodiscard]] bool matchesFlat(const FlatGroupKey& flatKey, const GroupingOptions& options) const;
};

/// Strong type identifying an integration group.
struct AFTERNIGHTPROCESSES_API IntegrationGroupKey
{
    GroupKey key;

    [[nodiscard]] QString toString() const;
    [[nodiscard]] QString toCompactString() const;
};

[[nodiscard]] AFTERNIGHTPROCESSES_API bool operator==(const IntegrationGroupKey& lhs,
                                                      const IntegrationGroupKey& rhs) noexcept;
[[nodiscard]] bool operator!=(const IntegrationGroupKey& lhs, const IntegrationGroupKey& rhs) noexcept;
[[nodiscard]] bool operator<(const IntegrationGroupKey& lhs, const IntegrationGroupKey& rhs) noexcept;

[[nodiscard]] size_t qHash(const IntegrationGroupKey& key, size_t seed = 0) noexcept;

W_REGISTER_ARGTYPE(GroupKey)
W_REGISTER_ARGTYPE(RegistrationGroupKey)
W_REGISTER_ARGTYPE(IntegrationGroupKey)
