// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "FrameMetadata.h"

FrameMetadata::FrameMetadata(FrameType type, const QSize& dimensions, double exposureSeconds,
                             std::optional<double> temperatureC, std::optional<int> isoGain, std::optional<int> offset,
                             const QString& filter, const QString& objectName, const QDateTime& capturedAtUtc,
                             const QString& readoutMode)
    : m_type(type), m_dimensions(dimensions), m_exposureSeconds(exposureSeconds), m_temperatureC(temperatureC),
      m_isoGain(isoGain), m_offset(offset), m_filter(filter), m_objectName(objectName), m_capturedAtUtc(capturedAtUtc),
      m_readoutMode(readoutMode)
{
}

FrameType FrameMetadata::type() const noexcept
{
    return m_type;
}

const QSize& FrameMetadata::dimensions() const noexcept
{
    return m_dimensions;
}

double FrameMetadata::exposureSeconds() const noexcept
{
    return m_exposureSeconds;
}

std::optional<double> FrameMetadata::temperatureC() const noexcept
{
    return m_temperatureC;
}

std::optional<int> FrameMetadata::isoGain() const noexcept
{
    return m_isoGain;
}

std::optional<int> FrameMetadata::offset() const noexcept
{
    return m_offset;
}

const QString& FrameMetadata::filter() const noexcept
{
    return m_filter;
}

const QString& FrameMetadata::objectName() const noexcept
{
    return m_objectName;
}

const QDateTime& FrameMetadata::capturedAtUtc() const noexcept
{
    return m_capturedAtUtc;
}

const QString& FrameMetadata::readoutMode() const noexcept
{
    return m_readoutMode;
}

QString FrameMetadata::resolutionString() const
{
    return QStringLiteral("%1x%2").arg(m_dimensions.width()).arg(m_dimensions.height());
}
