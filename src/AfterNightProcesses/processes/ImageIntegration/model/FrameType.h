// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/**
 * @file FrameType.h
 * @brief Frame type enumeration and utility functions for ImageIntegration.
 *
 * This header defines the core FrameType enum used throughout the integration
 * pipeline to categorize calibration and light frames. It provides conversion
 * utilities for serialization, UI display, and tab/index mapping.
 *
 * @see FrameMetadata, FrameCollection, IntegrationDataModel
 */

#include <QString>
#include <optional>
#include <verdigris/wobjectdefs.h>

/**
 * @brief Frame categories used by the Image Integration model.
 *
 * Each astronomical imaging session typically captures four types of frames:
 * - **Bias**: Zero-length exposures to capture readout noise
 * - **Dark**: Long exposures with shutter closed for thermal noise
 * - **Flat**: Exposures of uniform light source for vignetting correction
 * - **Light**: Actual target exposures containing the astronomical object
 *
 * The order of enum values is significant and matches the UI tab order.
 */
enum class FrameType
{
    Bias, ///< Bias/offset calibration frames (zero exposure)
    Dark, ///< Dark calibration frames (thermal noise)
    Flat, ///< Flat-field calibration frames (vignetting/dust)
    Light ///< Light frames (science data)
};

W_REGISTER_ARGTYPE(FrameType)

/**
 * @brief Convert a frame type to its stable string representation.
 * @param type The FrameType to convert.
 * @return Uppercase string identifier (e.g., "BIAS", "DARK", "FLAT", "LIGHT").
 * @note Used for JSON serialization and FITS keyword compatibility.
 */
[[nodiscard]] inline QString toString(FrameType type)
{
    switch (type)
    {
    case FrameType::Bias:
        return QStringLiteral("BIAS");
    case FrameType::Dark:
        return QStringLiteral("DARK");
    case FrameType::Flat:
        return QStringLiteral("FLAT");
    case FrameType::Light:
        return QStringLiteral("LIGHT");
    }

    return QStringLiteral("LIGHT");
}

/**
 * @brief Parse a frame type from a string (case-insensitive).
 * @param value String representation of the frame type.
 * @return The corresponding FrameType, or std::nullopt if parsing fails.
 * @note Accepts "BIAS", "DARK", "FLAT", "LIGHT" in any case combination.
 */
[[nodiscard]] inline std::optional<FrameType> frameTypeFromString(const QString& value)
{
    if (value.compare(QStringLiteral("BIAS"), Qt::CaseInsensitive) == 0)
    {
        return FrameType::Bias;
    }
    if (value.compare(QStringLiteral("DARK"), Qt::CaseInsensitive) == 0)
    {
        return FrameType::Dark;
    }
    if (value.compare(QStringLiteral("FLAT"), Qt::CaseInsensitive) == 0)
    {
        return FrameType::Flat;
    }
    if (value.compare(QStringLiteral("LIGHT"), Qt::CaseInsensitive) == 0)
    {
        return FrameType::Light;
    }

    return std::nullopt;
}

/** @brief Total number of frame types in the FrameType enum. */
constexpr int kFrameTypeCount = 4;

/**
 * @brief Convert a FrameType to its zero-based tab index.
 * @param type The FrameType to convert.
 * @return Zero-based index: Bias=0, Dark=1, Flat=2, Light=3.
 * @note Used for QTabWidget indexing and array access.
 */
[[nodiscard]] constexpr int indexFor(FrameType type) noexcept
{
    switch (type)
    {
    case FrameType::Bias:
        return 0;
    case FrameType::Dark:
        return 1;
    case FrameType::Flat:
        return 2;
    case FrameType::Light:
        return 3;
    }
    return 3;
}

/**
 * @brief Convert a zero-based tab index to its corresponding FrameType.
 * @param index Zero-based index (0-3).
 * @return Corresponding FrameType; defaults to Light for invalid indices.
 * @note Inverse of indexFor(). Used for tab selection handling.
 */
[[nodiscard]] constexpr FrameType frameTypeForIndex(int index) noexcept
{
    switch (index)
    {
    case 0:
        return FrameType::Bias;
    case 1:
        return FrameType::Dark;
    case 2:
        return FrameType::Flat;
    case 3:
        return FrameType::Light;
    default:
        return FrameType::Light;
    }
}
