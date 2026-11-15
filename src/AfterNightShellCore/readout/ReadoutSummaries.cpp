// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "readout/ReadoutSummaries.h"

#include "app/PlateSolveTypes.h"
#include "controls/ImageAstrometryInfoFormatter.h"

#include <AfterNightEngine/Image/SampleType.h>

#include <cmath>
#include <cstdint>

namespace an::ui::shell
{
namespace
{

/// The status line's name of a sample type.
[[nodiscard]] QString statusLineSampleName(const an::SampleType type)
{
    switch (type)
    {
    case an::SampleType::Int8:
        return QStringLiteral("8 bit signed integer");
    case an::SampleType::UInt8:
        return QStringLiteral("8 bit unsigned integer");
    case an::SampleType::Int16:
        return QStringLiteral("16 bit signed integer");
    case an::SampleType::UInt16:
        return QStringLiteral("16 bit unsigned integer");
    case an::SampleType::Int32:
        return QStringLiteral("32 bit signed integer");
    case an::SampleType::UInt32:
        return QStringLiteral("32 bit unsigned integer");
    case an::SampleType::Float32:
        return QStringLiteral("32 bit floating point");
    case an::SampleType::Float64:
        return QStringLiteral("64 bit floating point");
    }
    return QString();
}

/// The same, in short.
[[nodiscard]] QString shortSampleTypeName(const an::SampleType type)
{
    QString name = statusLineSampleName(type);
    name.replace(QStringLiteral(" floating point"), QStringLiteral(" float"));
    name.replace(QStringLiteral(" unsigned integer"), QStringLiteral(" uint"));
    name.replace(QStringLiteral(" signed integer"), QStringLiteral(" int"));
    return name;
}

} // namespace

an::ui::AstrometryReadout astrometryReadout(const std::optional<an::WcsSolution>& solution, const QString& solver)
{
    an::ui::AstrometryReadout readout;
    if (!solution.has_value())
    {
        return readout;
    }
    readout.solved = true;
    readout.solver = solver;
    if (const an::Result<an::SkyCoordinate> centre = solution->fieldCenter(); centre.hasValue())
    {
        readout.rightAscension = an::ui::formatAstrometryRa(centre->rightAscensionDegrees);
        readout.declination = an::ui::formatAstrometryDec(centre->declinationDegrees);
    }
    const double scale = solution->meanPixelScaleArcsecPerPixel();
    readout.scale = std::isfinite(scale) ? QString::number(scale, 'f', 3) : QStringLiteral("-");
    const double rotation = solution->orientationDegrees();
    readout.rotation = std::isfinite(rotation) ? QString::number(rotation, 'f', 2) : QStringLiteral("-");
    const an::SipDistortion& sip = solution->distortion();
    if (sip.hasForwardModel())
    {
        readout.distortion = QStringLiteral("SIP %1/%2").arg(sip.forwardX().order).arg(sip.forwardY().order);
    }
    else if (sip.hasInverseModel())
    {
        readout.distortion = QStringLiteral("SIP %1/%2").arg(sip.inverseX().order).arg(sip.inverseY().order);
    }
    return readout;
}

QString solverNamed(const std::span<const an::CodecCard> cards)
{
    return an::ui::detail::headerCardText(cards, an::ui::kPlateSolveSolverKeyword);
}

QString compactAstrometryLine(const an::ui::AstrometryReadout& readout)
{
    if (!readout.solved)
    {
        return QString();
    }
    QString line = readout.scale + QStringLiteral("″/px");
    if (!readout.distortion.isEmpty())
    {
        line += QStringLiteral(" · ") + readout.distortion;
    }
    return line;
}

an::ui::ImageFactsReadout imageFactsReadout(const an::ImageDescriptor& descriptor)
{
    const std::uint32_t width = descriptor.extent().width();
    const std::uint32_t height = descriptor.extent().height();
    const std::uint32_t channels = descriptor.extent().channelCount();
    const double bytes = static_cast<double>(width) * static_cast<double>(height) * static_cast<double>(channels) *
                         static_cast<double>(an::sampleTypeSizeBytes(descriptor.sampleType()));
    const double megabytes = bytes / 1024.0 / 1024.0;
    const QString size = megabytes > 1024.0 ? QStringLiteral("%1 GB").arg(QString::number(megabytes / 1024.0, 'f', 2))
                                            : QStringLiteral("%1 MB").arg(QString::number(megabytes, 'f', 2));

    an::ui::ImageFactsReadout facts;
    facts.dimensions = QStringLiteral("%1 × %2").arg(width).arg(height);
    facts.format = statusLineSampleName(descriptor.sampleType());
    facts.sizeInMemory = size;
    facts.channels = channels >= 3u ? QStringLiteral("RGB") : QStringLiteral("Grayscale");
    facts.summary = QStringLiteral("%1x%2 · %3 · %4")
                        .arg(width)
                        .arg(height)
                        .arg(shortSampleTypeName(descriptor.sampleType()), size);
    return facts;
}

} // namespace an::ui::shell
