// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightProcessesExport.h"

/**
 * @file FrameMetadata.h
 * @brief Metadata container for individual image frames.
 *
 * FrameMetadata encapsulates all extracted header information from FITS/XISF
 * files that is relevant for calibration matching, grouping, and workflow
 * configuration. Fields use std::optional for values that may not be present
 * in all file formats.
 *
 * @see FrameType, GroupingKeys, CalibrationMatcher
 */

#include "FrameType.h"

#include <QDateTime>
#include <QSize>
#include <QString>

#include <optional>

/**
 * @brief Extracted metadata used for grouping and workflow configuration.
 *
 * This class provides read-only access to frame metadata extracted from
 * image file headers. All fields are immutable after construction to
 * ensure thread safety when used in concurrent workflows.
 *
 * @par Grouping Relevance
 * The following fields are used for automatic frame grouping:
 * - dimensions, isoGain, offset (for calibration frame matching)
 * - filter, objectName (for light frame organization)
 * - exposureSeconds (for dark frame matching)
 * - temperatureC (optional, for sensor temperature matching)
 *
 * @par Thread Safety
 * This class is immutable and safe for concurrent read access.
 */
class AFTERNIGHTPROCESSES_API FrameMetadata
{
public:
    /**
     * @brief Construct metadata with all known fields.
     * @param type Frame category (Bias, Dark, Flat, Light).
     * @param dimensions Image pixel dimensions (width x height).
     * @param exposureSeconds Exposure time in seconds.
     * @param temperatureC Sensor temperature in Celsius (may be std::nullopt).
     * @param isoGain ISO or electronic gain value (may be std::nullopt).
     * @param offset Sensor offset/black level (may be std::nullopt).
     * @param filter Filter name (may be empty for unfiltered).
     * @param objectName Target object name (may be empty).
     * @param capturedAtUtc Capture timestamp in UTC.
     * @param readoutMode Camera readout mode identifier (may be empty).
     */
    FrameMetadata(FrameType type, const QSize& dimensions, double exposureSeconds, std::optional<double> temperatureC,
                  std::optional<int> isoGain, std::optional<int> offset, const QString& filter,
                  const QString& objectName, const QDateTime& capturedAtUtc, const QString& readoutMode);

    /** @brief Return the frame type. */
    [[nodiscard]] FrameType type() const noexcept;

    /** @brief Return the pixel dimensions (width x height). */
    [[nodiscard]] const QSize& dimensions() const noexcept;

    /** @brief Return the exposure time in seconds. */
    [[nodiscard]] double exposureSeconds() const noexcept;

    /** @brief Return the sensor temperature in Celsius, if available. */
    [[nodiscard]] std::optional<double> temperatureC() const noexcept;

    /** @brief Return the ISO or gain value, if available. */
    [[nodiscard]] std::optional<int> isoGain() const noexcept;

    /** @brief Return the sensor offset value, if available. */
    [[nodiscard]] std::optional<int> offset() const noexcept;

    /** @brief Return the filter name (may be empty for unfiltered imaging). */
    [[nodiscard]] const QString& filter() const noexcept;

    /** @brief Return the object/target name (may be empty). */
    [[nodiscard]] const QString& objectName() const noexcept;

    /** @brief Return the capture timestamp in UTC. */
    [[nodiscard]] const QDateTime& capturedAtUtc() const noexcept;

    /** @brief Return the readout mode identifier (may be empty). */
    [[nodiscard]] const QString& readoutMode() const noexcept;

    /**
     * @brief Return a formatted resolution string.
     * @return String in format "WIDTHxHEIGHT" (e.g., "6000x4000").
     */
    [[nodiscard]] QString resolutionString() const;

private:
    FrameType m_type;
    QSize m_dimensions;
    double m_exposureSeconds = 0.0;
    std::optional<double> m_temperatureC;
    std::optional<int> m_isoGain;
    std::optional<int> m_offset;
    QString m_filter;
    QString m_objectName;
    QDateTime m_capturedAtUtc;
    QString m_readoutMode;
};
