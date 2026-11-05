// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "FrameTreeFilter.h"

#include <QFileInfo>
#include <QStringList>

void FrameTreeFilter::setSearchText(const QString& text)
{
    m_searchText = text.trimmed();
}

const QString& FrameTreeFilter::searchText() const noexcept
{
    return m_searchText;
}

void FrameTreeFilter::setExposureRange(double minSeconds, double maxSeconds)
{
    m_exposureMin = minSeconds;
    m_exposureMax = maxSeconds;
}

void FrameTreeFilter::clearExposureRange()
{
    m_exposureMin.reset();
    m_exposureMax.reset();
}

void FrameTreeFilter::setFilterName(const QString& filterName)
{
    m_filterName = filterName.trimmed();
}

const QString& FrameTreeFilter::filterName() const noexcept
{
    return m_filterName;
}

bool FrameTreeFilter::matches(const FrameInfo& frame) const
{
    if (!m_searchText.isEmpty())
    {
        const auto& meta = frame.metadata;
        QStringList fields;
        fields.reserve(16);

        const QString fileName = QFileInfo(frame.displayPath).fileName();
        fields << fileName << frame.displayPath;
        fields << meta.filter();
        fields << frame.effectiveObjectName();
        fields << frame.effectiveSession();
        fields << meta.resolutionString();
        if (!frame.customTag.isEmpty())
        {
            fields << frame.customTag;
        }

        if (meta.isoGain().has_value())
        {
            const QString isoVal = QString::number(meta.isoGain().value());
            fields << isoVal << QStringLiteral("ISO %1").arg(isoVal);
        }
        if (meta.offset().has_value())
        {
            const QString offsetVal = QString::number(meta.offset().value());
            fields << offsetVal << QStringLiteral("Offset %1").arg(offsetVal);
        }
        if (meta.temperatureC().has_value())
        {
            const QString tempVal = QString::number(meta.temperatureC().value(), 'f', 1);
            fields << tempVal << QStringLiteral("Temp %1C").arg(tempVal);
        }

        const QString exposureVal = QString::number(meta.exposureSeconds(), 'f', 3);
        fields << exposureVal << QStringLiteral("%1s").arg(exposureVal);

        const QString haystack = fields.join(QLatin1Char(' '));
        if (!haystack.contains(m_searchText, Qt::CaseInsensitive))
        {
            return false;
        }
    }

    if (m_exposureMin.has_value() && frame.metadata.exposureSeconds() < *m_exposureMin)
    {
        return false;
    }
    if (m_exposureMax.has_value() && frame.metadata.exposureSeconds() > *m_exposureMax)
    {
        return false;
    }

    if (!m_filterName.isEmpty())
    {
        if (frame.metadata.filter().compare(m_filterName, Qt::CaseInsensitive) != 0)
        {
            return false;
        }
    }

    return true;
}

bool FrameTreeFilter::isEmpty() const noexcept
{
    return m_searchText.isEmpty() && !m_exposureMin.has_value() && !m_exposureMax.has_value() && m_filterName.isEmpty();
}
