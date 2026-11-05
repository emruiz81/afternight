// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The astrometry window's summary of an image's solution: where the field is centred and where the reference pixel
/// points, the scale, the orientation, the field of view, the projection, the equinox the header states, the
/// distortion and the solver - each the solution's own figure, labelled.
///
/// The figures are the engine's: the square-pixel scale (`meanPixelScaleArcsecPerPixel`), the field's centre
/// (`fieldCenter`), the orientation of celestial north (`orientationDegrees`) and the field of view
/// (`fieldOfViewArcmin`).
#pragma once

#include "SexagesimalFormatter.h"
#include "app/PlateSolveTypes.h"

#include <AfterNightEngine/Algorithms/Astrometry/WcsSolution.h>
#include <AfterNightEngine/Image/CodecMetadata.h>

#include <QString>
#include <QStringList>

#include <cmath>
#include <cstddef>
#include <optional>
#include <span>
#include <string_view>

namespace an::ui
{

struct ImageAstrometryInfoSummary
{
    bool hasSolution = false;
    QString statusMessage = QStringLiteral("No astrometric solution");
    QString fieldCenterRa = QStringLiteral("-");
    QString fieldCenterDec = QStringLiteral("-");
    QString crvalRa = QStringLiteral("-");
    QString crvalDec = QStringLiteral("-");
    QString imageScale = QStringLiteral("-");
    QString rotation = QStringLiteral("-");
    QString fieldOfView = QStringLiteral("-");
    QString projection = QStringLiteral("-");
    QString equinox = QStringLiteral("-");
    QString sip = QStringLiteral("None");
    QString solver = QStringLiteral("Unknown");
};

namespace detail
{

[[nodiscard]] inline QString sanitizeFitsText(QString value)
{
    value = value.trimmed();
    if ((value.startsWith(QLatin1Char('\'')) && value.endsWith(QLatin1Char('\''))) ||
        (value.startsWith(QLatin1Char('"')) && value.endsWith(QLatin1Char('"'))))
    {
        if (value.size() >= 2)
        {
            value = value.mid(1, value.size() - 2);
        }
    }

    return value.simplified();
}

/// The text a header card states for a keyword, unquoted; empty when no card states it.
[[nodiscard]] inline QString headerCardText(std::span<const an::CodecCard> cards, const std::string_view keyword)
{
    for (const an::CodecCard& card : cards)
    {
        if (card.origin() == an::CodecNamespace::Fits && card.keyword() == keyword)
        {
            return sanitizeFitsText(QString::fromStdString(card.value()));
        }
    }
    return {};
}

/// How many coefficients of a polynomial component are not zero.
[[nodiscard]] inline int countNonZeroCoefficients(const an::SipPolynomial& component)
{
    int count = 0;
    for (const auto& row : component.coefficients)
    {
        for (const double value : row)
        {
            if (std::abs(value) > 1e-12)
            {
                ++count;
            }
        }
    }
    return count;
}

/// The equinox the cards state, or - for a solution written in the ICRS, which has none - the reference frame.
[[nodiscard]] inline QString formatEquinox(std::span<const an::CodecCard> cards)
{
    const QString raw = headerCardText(cards, "EQUINOX");
    if (raw.isEmpty())
    {
        const QString frame = headerCardText(cards, "RADESYS");
        return frame.isEmpty() ? QStringLiteral("-") : frame;
    }

    bool ok = false;
    const double numeric = raw.toDouble(&ok);
    if (ok)
    {
        return QStringLiteral("J%1").arg(QString::number(numeric, 'f', 1));
    }

    return raw;
}

/// The projection codes the solution is written with: the tangent plane, with the distortion suffix when it carries a
/// polynomial.
[[nodiscard]] inline QString formatProjection(const an::WcsSolution& solution)
{
    const bool distorted = solution.distortion().hasForwardModel() || solution.distortion().hasInverseModel();
    return distorted ? QStringLiteral("RA---TAN-SIP / DEC--TAN-SIP") : QStringLiteral("RA---TAN / DEC--TAN");
}

[[nodiscard]] inline QString formatSipDescription(const an::SipDistortion& sip)
{
    if (!sip.hasForwardModel() && !sip.hasInverseModel())
    {
        return QStringLiteral("None");
    }

    QStringList parts;
    int coefficient_count = 0;
    if (sip.hasForwardModel())
    {
        parts << QStringLiteral("A/B %1/%2").arg(sip.forwardX().order).arg(sip.forwardY().order);
        coefficient_count += countNonZeroCoefficients(sip.forwardX());
        coefficient_count += countNonZeroCoefficients(sip.forwardY());
    }
    if (sip.hasInverseModel())
    {
        parts << QStringLiteral("AP/BP %1/%2").arg(sip.inverseX().order).arg(sip.inverseY().order);
        coefficient_count += countNonZeroCoefficients(sip.inverseX());
        coefficient_count += countNonZeroCoefficients(sip.inverseY());
    }

    return QStringLiteral("%1, %2 coeffs").arg(parts.join(QStringLiteral(", "))).arg(coefficient_count);
}

} // namespace detail

/// A right ascension as the astrometry window shows it: hours, minutes and seconds to a hundredth, with colons.
[[nodiscard]] inline QString formatAstrometryRa(const double ra_deg)
{
    return rightAscensionLabel(ra_deg, 2u, SexagesimalLabelStyle::Colons);
}

/// A declination as the astrometry window shows it: signed degrees, arcminutes and arcseconds to a tenth, with colons.
[[nodiscard]] inline QString formatAstrometryDec(const double dec_deg)
{
    return declinationLabel(dec_deg, 1u, SexagesimalLabelStyle::Colons);
}

[[nodiscard]] inline QString formatAstrometryScale(const double arcsec_per_pixel)
{
    return std::isfinite(arcsec_per_pixel) && arcsec_per_pixel > 0.0
               ? QStringLiteral("%1 arcsec/px").arg(QString::number(arcsec_per_pixel, 'f', 3))
               : QStringLiteral("-");
}

[[nodiscard]] inline QString formatAstrometryRotation(const double degrees)
{
    return std::isfinite(degrees) ? QStringLiteral("%1° (N through E)").arg(QString::number(degrees, 'f', 1))
                                  : QStringLiteral("-");
}

[[nodiscard]] inline QString formatAstrometryFieldOfView(const double width_arcmin, const double height_arcmin)
{
    return (std::isfinite(width_arcmin) && std::isfinite(height_arcmin) && width_arcmin > 0.0 && height_arcmin > 0.0)
               ? QStringLiteral("%1′ × %2′")
                     .arg(QString::number(width_arcmin, 'f', 2), QString::number(height_arcmin, 'f', 2))
               : QStringLiteral("-");
}

/// The summary of an image's solution.
///
/// @param solution the solution the image's cards state, or nothing.
/// @param cards the image's cards, which name the solver and the equinox.
/// @return the summary; one with no solution says so.
[[nodiscard]] inline ImageAstrometryInfoSummary
buildImageAstrometryInfoSummary(const std::optional<an::WcsSolution>& solution, std::span<const an::CodecCard> cards)
{
    ImageAstrometryInfoSummary summary;
    summary.equinox = detail::formatEquinox(cards);

    const QString solver = detail::headerCardText(cards, kPlateSolveSolverKeyword);
    if (!solver.isEmpty())
    {
        summary.solver = solver;
    }

    if (!solution.has_value())
    {
        return summary;
    }

    summary.hasSolution = true;
    summary.statusMessage = QStringLiteral("Astrometric solution available");
    summary.projection = detail::formatProjection(*solution);
    summary.imageScale = formatAstrometryScale(solution->meanPixelScaleArcsecPerPixel());
    summary.rotation = formatAstrometryRotation(solution->orientationDegrees());
    const an::WcsFieldOfView field_of_view = solution->fieldOfViewArcmin();
    summary.fieldOfView = formatAstrometryFieldOfView(field_of_view.widthArcmin, field_of_view.heightArcmin);
    summary.sip = detail::formatSipDescription(solution->distortion());

    if (const an::Result<an::SkyCoordinate> center = solution->fieldCenter(); center.hasValue())
    {
        summary.fieldCenterRa = formatAstrometryRa(center->rightAscensionDegrees);
        summary.fieldCenterDec = formatAstrometryDec(center->declinationDegrees);
    }

    const an::SkyCoordinate reference = solution->referencePoint();
    summary.crvalRa = formatAstrometryRa(reference.rightAscensionDegrees);
    summary.crvalDec = formatAstrometryDec(reference.declinationDegrees);

    return summary;
}

} // namespace an::ui
