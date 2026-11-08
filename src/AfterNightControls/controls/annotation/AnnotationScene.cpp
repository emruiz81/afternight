// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "controls/annotation/AnnotationScene.h"

#include "SexagesimalFormatter.h"
#include "controls/CatalogAnnotationDisplayModule.h"
#include "controls/CoordinateGridDisplayModule.h"
#include "controls/ImageAstrometryInfoFormatter.h"

#include <QCoreApplication>
#include <QHash>
#include <QPainterPath>
#include <QRectF>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <numbers>
#include <unordered_map>
#include <utility>

namespace an::ui
{
namespace
{

[[nodiscard]] QString tr(const char* text)
{
    return QCoreApplication::translate("AnnotationScene", text);
}

/// How many samples a grid line is projected through, end to end.
constexpr int kGridSamples = 480;

/// How many samples of the frame's edges and interior the field's extent is measured from.
constexpr int kEdgeSamples = 48;
constexpr int kInteriorSamples = 9;

/// Two entries of different catalogues closer than this are one object, whatever they are called: the catalogues state
/// one position for an object they share.
constexpr double kSamePositionDegrees = 0.1 / 60.0;

/// Two entries of different catalogues of one common name are one object within this, or within the larger's size.
constexpr double kSameNameDegrees = 0.5;

/// A point object's marker radius, as a fraction of the reference length.
constexpr double kPointRadius = 0.0055;

/// A star's marker radius at magnitude zero, as a fraction of the reference length, before its magnitude shrinks it.
constexpr double kStarRadius = 0.0045;

/// How far a corner's directions are taken from the corner, as a fraction of the reference length.
constexpr double kCornerInset = 0.09;

/// The deep-sky catalogue a designation belongs to, in the order a mark prefers it.
enum class Catalogue : std::uint8_t
{
    Messier,
    Ngc,
    Ic,
    Caldwell,
    Other
};

[[nodiscard]] char lowered(const char c) noexcept
{
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

/// Whether a text holds another, letters compared without their case.
[[nodiscard]] bool containsFolded(const std::string_view text, const std::string_view part) noexcept
{
    if (part.empty() || part.size() > text.size())
    {
        return part.empty();
    }
    for (std::size_t start = 0u; start + part.size() <= text.size(); ++start)
    {
        bool equal = true;
        for (std::size_t index = 0u; index < part.size(); ++index)
        {
            if (lowered(text[start + index]) != lowered(part[index]))
            {
                equal = false;
                break;
            }
        }
        if (equal)
        {
            return true;
        }
    }
    return false;
}

[[nodiscard]] Catalogue catalogueOf(const QString& designation)
{
    const QString id = designation.trimmed();
    if (id.startsWith(QLatin1String("NGC"), Qt::CaseInsensitive))
    {
        return Catalogue::Ngc;
    }
    if (id.startsWith(QLatin1String("IC"), Qt::CaseInsensitive))
    {
        return Catalogue::Ic;
    }
    const auto numbered = [&id](const QChar prefix) {
        return id.size() >= 2 && id.front().toUpper() == prefix && (id.at(1).isSpace() || id.at(1).isDigit());
    };
    if (numbered(QLatin1Char('M')))
    {
        return Catalogue::Messier;
    }
    if (numbered(QLatin1Char('C')))
    {
        return Catalogue::Caldwell;
    }
    return Catalogue::Other;
}

[[nodiscard]] bool catalogueChosen(const Catalogue catalogue, const AnnotationDeepSkySelection& selection) noexcept
{
    switch (catalogue)
    {
    case Catalogue::Messier:
        return selection.messier;
    case Catalogue::Caldwell:
        return selection.caldwell;
    case Catalogue::Ngc:
        return selection.ngc;
    case Catalogue::Ic:
        return selection.ic;
    case Catalogue::Other:
        break;
    }
    return selection.ngc || selection.ic;
}

[[nodiscard]] bool kindChosen(const AnnotationMarkKind kind, const AnnotationDeepSkySelection& selection) noexcept
{
    switch (kind)
    {
    case AnnotationMarkKind::Galaxy:
        return selection.galaxies;
    case AnnotationMarkKind::Nebula:
        return selection.nebulae;
    case AnnotationMarkKind::PlanetaryNebula:
        return selection.planetaryNebulae;
    case AnnotationMarkKind::OpenCluster:
        return selection.openClusters;
    case AnnotationMarkKind::GlobularCluster:
        return selection.globularClusters;
    case AnnotationMarkKind::OtherObject:
    case AnnotationMarkKind::Star:
        break;
    }
    return selection.otherObjects;
}

/// A coordinate label's marks as a typesetter writes them: a minus sign, primes and double primes.
[[nodiscard]] QString typeset(QString text)
{
    text.replace(QLatin1Char('\''), QChar(0x2032));
    text.replace(QLatin1Char('"'), QChar(0x2033));
    if (text.startsWith(QLatin1Char('-')))
    {
        text[0] = QChar(0x2212);
    }
    return text;
}

[[nodiscard]] QString declinationGridLabel(const double declination, const double spacing)
{
    if (spacing >= 1.0 - 1.0e-9)
    {
        const long whole = std::lround(std::abs(declination));
        return typeset(
            QStringLiteral("%1%2°").arg(declination < -1.0e-9 ? QLatin1Char('-') : QLatin1Char('+')).arg(whole));
    }
    if (spacing >= (1.0 / 60.0) - 1.0e-9)
    {
        return typeset(declinationMinutesLabel(declination));
    }
    return typeset(declinationLabel(declination, 0u));
}

[[nodiscard]] QString rightAscensionGridLabel(const double rightAscension, const double spacing)
{
    if (spacing >= 15.0 - 1.0e-9)
    {
        const long hours = std::lround(an::normalizeRightAscensionDegrees(rightAscension) / 15.0) % 24;
        return QStringLiteral("%1h").arg(hours, 2, 10, QLatin1Char('0'));
    }
    if (spacing >= 0.25 - 1.0e-9)
    {
        return rightAscensionMinutesLabel(rightAscension);
    }
    return rightAscensionLabel(rightAscension, 0u);
}

/// The steps a grid takes, in arcseconds of declination or seconds of time.
constexpr std::array<double, 22> kDeclinationSteps{1.0,     2.0,     5.0,     10.0,    15.0,    20.0,    30.0,   60.0,
                                                   120.0,   300.0,   600.0,   900.0,   1200.0,  1800.0,  3600.0, 7200.0,
                                                   10800.0, 18000.0, 36000.0, 54000.0, 72000.0, 108000.0};
constexpr std::array<double, 19> kTimeSteps{1.0,    2.0,    5.0,     10.0,    15.0,   20.0,   30.0,
                                            60.0,   120.0,  300.0,   600.0,   900.0,  1200.0, 1800.0,
                                            3600.0, 7200.0, 10800.0, 14400.0, 21600.0};

[[nodiscard]] int targetLines(const AnnotationGridDensity density) noexcept
{
    switch (density)
    {
    case AnnotationGridDensity::Coarse:
        return 4;
    case AnnotationGridDensity::Fine:
        return 10;
    case AnnotationGridDensity::Normal:
        break;
    }
    return 6;
}

/// Where a segment between two points enters and leaves a rectangle, as fractions along it.
struct ClippedSegment
{
    double enter = 0.0;
    double leave = 1.0;
};

/// The Liang-Barsky clip of the segment from `a` to `b` against a rectangle.
[[nodiscard]] std::optional<ClippedSegment> clipSegment(const QPointF a, const QPointF b, const QRectF& frame) noexcept
{
    const double dx = b.x() - a.x();
    const double dy = b.y() - a.y();
    double enter = 0.0;
    double leave = 1.0;
    const std::array<double, 4> p{-dx, dx, -dy, dy};
    const std::array<double, 4> q{a.x() - frame.left(), frame.right() - a.x(), a.y() - frame.top(),
                                  frame.bottom() - a.y()};
    for (std::size_t index = 0u; index < 4u; ++index)
    {
        if (p[index] == 0.0)
        {
            if (q[index] < 0.0)
            {
                return std::nullopt;
            }
            continue;
        }
        const double t = q[index] / p[index];
        if (p[index] < 0.0)
        {
            enter = (std::max)(enter, t);
        }
        else
        {
            leave = (std::min)(leave, t);
        }
        if (enter > leave)
        {
            return std::nullopt;
        }
    }
    return ClippedSegment{.enter = enter, .leave = leave};
}

[[nodiscard]] AnnotationEdge edgeOf(const QPointF point, const QRectF& frame) noexcept
{
    const double left = std::abs(point.x() - frame.left());
    const double right = std::abs(point.x() - frame.right());
    const double top = std::abs(point.y() - frame.top());
    const double bottom = std::abs(point.y() - frame.bottom());
    const double nearest = (std::min)({left, right, top, bottom});
    if (nearest == bottom)
    {
        return AnnotationEdge::Bottom;
    }
    if (nearest == left)
    {
        return AnnotationEdge::Left;
    }
    if (nearest == top)
    {
        return AnnotationEdge::Top;
    }
    return AnnotationEdge::Right;
}

/// The parts of a projected polyline inside the frame, and where it crosses the frame's edges.
void clipIntoFrame(const std::vector<std::optional<QPointF>>& points, const QRectF& frame, AnnotationGridLine& line)
{
    QPolygonF current;
    const auto flush = [&current, &line]() {
        if (current.size() >= 2)
        {
            line.segments.push_back(std::move(current));
        }
        current = QPolygonF();
    };
    for (std::size_t index = 1u; index < points.size(); ++index)
    {
        const std::optional<QPointF>& from = points[index - 1u];
        const std::optional<QPointF>& to = points[index];
        if (!from.has_value() || !to.has_value())
        {
            flush();
            continue;
        }
        const std::optional<ClippedSegment> clipped = clipSegment(*from, *to, frame);
        if (!clipped.has_value())
        {
            flush();
            continue;
        }
        const QPointF delta = *to - *from;
        const QPointF start = *from + delta * clipped->enter;
        const QPointF end = *from + delta * clipped->leave;
        if (clipped->enter > 0.0)
        {
            flush();
            line.crossings.push_back({.point = start, .edge = edgeOf(start, frame)});
        }
        if (current.isEmpty())
        {
            current << start;
        }
        current << end;
        if (clipped->leave < 1.0)
        {
            line.crossings.push_back({.point = end, .edge = edgeOf(end, frame)});
            flush();
        }
    }
    flush();
}

[[nodiscard]] std::optional<QPointF> projected(const an::WcsSolution& solution, const double rightAscension,
                                               const double declination)
{
    const an::Result<an::PixelCoordinate> pixel = solution.worldToPixel(
        an::SkyCoordinate{.rightAscensionDegrees = an::normalizeRightAscensionDegrees(rightAscension),
                          .declinationDegrees = std::clamp(declination, -90.0, 90.0)});
    if (!pixel.hasValue() || !std::isfinite(pixel->x) || !std::isfinite(pixel->y))
    {
        return std::nullopt;
    }
    return QPointF(pixel->x, pixel->y);
}

/// The sky the frame covers: the right ascension span about the centre's and the declination span, each pole the
/// frame holds opening the right ascension to the whole circle.
struct FieldExtent
{
    double rightAscensionStart = 0.0;
    double rightAscensionEnd = 0.0;
    double declinationStart = 0.0;
    double declinationEnd = 0.0;
};

[[nodiscard]] std::optional<FieldExtent> fieldExtentOf(const an::WcsSolution& solution, const QRectF& frame)
{
    const an::Result<an::SkyCoordinate> centre = solution.fieldCenter();
    if (!centre.hasValue())
    {
        return std::nullopt;
    }
    double raLow = 0.0;
    double raHigh = 0.0;
    double decLow = centre->declinationDegrees;
    double decHigh = centre->declinationDegrees;
    const auto include = [&](const double x, const double y) {
        const an::Result<an::SkyCoordinate> sky = solution.pixelToWorld(an::PixelCoordinate{.x = x, .y = y});
        if (!sky.hasValue())
        {
            return;
        }
        const double offset =
            detail::shortestAngularDistanceDegrees(centre->rightAscensionDegrees, sky->rightAscensionDegrees);
        raLow = (std::min)(raLow, offset);
        raHigh = (std::max)(raHigh, offset);
        decLow = (std::min)(decLow, sky->declinationDegrees);
        decHigh = (std::max)(decHigh, sky->declinationDegrees);
    };
    for (int index = 0; index <= kEdgeSamples; ++index)
    {
        const double t = static_cast<double>(index) / kEdgeSamples;
        include(frame.left() + frame.width() * t, frame.top());
        include(frame.left() + frame.width() * t, frame.bottom());
        include(frame.left(), frame.top() + frame.height() * t);
        include(frame.right(), frame.top() + frame.height() * t);
    }
    for (int row = 0; row < kInteriorSamples; ++row)
    {
        for (int column = 0; column < kInteriorSamples; ++column)
        {
            include(frame.left() + frame.width() * (column + 0.5) / kInteriorSamples,
                    frame.top() + frame.height() * (row + 0.5) / kInteriorSamples);
        }
    }
    FieldExtent extent{.rightAscensionStart = centre->rightAscensionDegrees + raLow,
                       .rightAscensionEnd = centre->rightAscensionDegrees + raHigh,
                       .declinationStart = decLow,
                       .declinationEnd = decHigh};
    const auto poleInside = [&](const double declination) {
        const std::optional<QPointF> pole = projected(solution, 0.0, declination);
        return pole.has_value() && frame.contains(*pole);
    };
    if (poleInside(90.0))
    {
        extent.declinationEnd = 90.0;
        extent.rightAscensionStart = 0.0;
        extent.rightAscensionEnd = 360.0;
    }
    if (poleInside(-90.0))
    {
        extent.declinationStart = -90.0;
        extent.rightAscensionStart = 0.0;
        extent.rightAscensionEnd = 360.0;
    }
    return extent;
}

[[nodiscard]] std::vector<AnnotationGridLine> buildGrid(const an::WcsSolution& solution, const QRectF& frame,
                                                        const AnnotationContent& content)
{
    std::vector<AnnotationGridLine> lines;
    const std::optional<FieldExtent> extent = fieldExtentOf(solution, frame);
    if (!extent.has_value())
    {
        return lines;
    }
    const double decSpan = extent->declinationEnd - extent->declinationStart;
    const double raSpan = extent->rightAscensionEnd - extent->rightAscensionStart;
    if (!(decSpan > 0.0) || !(raSpan > 0.0))
    {
        return lines;
    }
    const double decStep = annotationGridSpacingDegrees(decSpan, false, content.gridDensity);
    const double raStep = annotationGridSpacingDegrees(raSpan, true, content.gridDensity);

    // Lines of constant declination, sampled along the right ascension the frame spans and a step past it each way.
    const double raFrom = extent->rightAscensionStart - raStep;
    const double raTo = extent->rightAscensionEnd + raStep;
    for (double declination = std::ceil(extent->declinationStart / decStep) * decStep;
         declination <= extent->declinationEnd + 1.0e-9; declination += decStep)
    {
        if (std::abs(declination) >= 90.0 - 1.0e-9)
        {
            continue;
        }
        AnnotationGridLine line;
        line.rightAscension = false;
        line.coordinateDegrees = declination;
        std::vector<std::optional<QPointF>> points;
        points.reserve(static_cast<std::size_t>(kGridSamples) + 1u);
        for (int index = 0; index <= kGridSamples; ++index)
        {
            const double t = static_cast<double>(index) / kGridSamples;
            points.push_back(projected(solution, raFrom + (raTo - raFrom) * t, declination));
        }
        clipIntoFrame(points, frame, line);
        if (!line.segments.empty())
        {
            line.label = declinationGridLabel(declination, decStep);
            lines.push_back(std::move(line));
        }
    }

    // Lines of constant right ascension, sampled along the declination the frame spans and a step past it each way.
    const double decFrom = (std::max)(-89.9999, extent->declinationStart - decStep);
    const double decTo = (std::min)(89.9999, extent->declinationEnd + decStep);
    const bool wholeCircle = raSpan >= 360.0 - 1.0e-9;
    const double raFirst = std::ceil(extent->rightAscensionStart / raStep) * raStep;
    const double raLast = wholeCircle ? extent->rightAscensionStart + 360.0 - raStep * 0.5 : extent->rightAscensionEnd;
    for (double rightAscension = raFirst; rightAscension <= raLast + 1.0e-9; rightAscension += raStep)
    {
        AnnotationGridLine line;
        line.rightAscension = true;
        line.coordinateDegrees = an::normalizeRightAscensionDegrees(rightAscension);
        std::vector<std::optional<QPointF>> points;
        points.reserve(static_cast<std::size_t>(kGridSamples) + 1u);
        for (int index = 0; index <= kGridSamples; ++index)
        {
            const double t = static_cast<double>(index) / kGridSamples;
            points.push_back(projected(solution, rightAscension, decFrom + (decTo - decFrom) * t));
        }
        clipIntoFrame(points, frame, line);
        if (!line.segments.empty())
        {
            line.label = rightAscensionGridLabel(rightAscension, raStep);
            lines.push_back(std::move(line));
        }
    }
    return lines;
}

/// A deep-sky entry that passed the choices, before entries of one object become one mark.
struct Candidate
{
    const an::CatalogEntry* entry = nullptr;
    Catalogue catalogue = Catalogue::Other;
    AnnotationMarkKind kind = AnnotationMarkKind::OtherObject;
    CatalogAnnotationProjectedShape shape;
    double majorPixels = 0.0;
    QString designation;
    QString name;
};

[[nodiscard]] bool touchesFrame(const CatalogAnnotationProjectedShape& shape, const QRectF& frame)
{
    if (frame.contains(shape.center))
    {
        return true;
    }
    if (shape.outline.size() < 3)
    {
        return false;
    }
    for (const QPointF& point : shape.outline)
    {
        if (frame.contains(point))
        {
            return true;
        }
    }
    QPainterPath outline;
    outline.addPolygon(shape.outline);
    outline.closeSubpath();
    return outline.intersects(frame);
}

[[nodiscard]] bool coversFrame(const QPolygonF& outline, const QRectF& frame)
{
    if (outline.size() < 3)
    {
        return false;
    }
    return outline.containsPoint(frame.topLeft(), Qt::OddEvenFill) &&
           outline.containsPoint(frame.topRight(), Qt::OddEvenFill) &&
           outline.containsPoint(frame.bottomLeft(), Qt::OddEvenFill) &&
           outline.containsPoint(frame.bottomRight(), Qt::OddEvenFill);
}

[[nodiscard]] double deepSkyPriority(const Catalogue catalogue, const bool named, const double majorPixels,
                                     const std::optional<double> magnitude, const double reference)
{
    double base = 15.0;
    switch (catalogue)
    {
    case Catalogue::Messier:
        base = 100.0;
        break;
    case Catalogue::Caldwell:
        base = 80.0;
        break;
    case Catalogue::Ngc:
        base = named ? 60.0 : 30.0;
        break;
    case Catalogue::Ic:
        base = named ? 60.0 : 20.0;
        break;
    case Catalogue::Other:
        base = named ? 60.0 : 15.0;
        break;
    }
    double size = 0.0;
    if (majorPixels > 0.0 && reference > 0.0)
    {
        size = 20.0 * std::clamp(std::log2(1.0 + majorPixels / (0.01 * reference)) / 4.0, 0.0, 1.0);
    }
    double brightness = 0.0;
    if (magnitude.has_value())
    {
        brightness = 10.0 * std::clamp((15.0 - *magnitude) / 12.0, 0.0, 1.0);
    }
    return base + size + brightness;
}

[[nodiscard]] std::optional<double> measuredMagnitude(const an::CatalogEntry& entry)
{
    if (!entry.magnitude.has_value() || !std::isfinite(*entry.magnitude) || *entry.magnitude >= 90.0)
    {
        return std::nullopt;
    }
    return entry.magnitude;
}

[[nodiscard]] QString foldedName(const QString& name)
{
    return name.trimmed().toCaseFolded();
}

/// The cell of a position grid a point falls in, for the entries at one position.
[[nodiscard]] std::uint64_t cellKey(const QPointF point, const double cell)
{
    const auto column = static_cast<std::int64_t>(std::floor(point.x() / cell));
    const auto row = static_cast<std::int64_t>(std::floor(point.y() / cell));
    return (static_cast<std::uint64_t>(row) << 32u) ^ static_cast<std::uint32_t>(column);
}

void addDeepSky(const AnnotationSceneSources& sources, const AnnotationContent& content, const QRectF& frame,
                const double reference, std::vector<AnnotationMark>& marks)
{
    const an::WcsSolution& solution = *sources.solution;
    const AnnotationDeepSkySelection& selection = content.deepSky;
    const double arcsecondsPerPixel = solution.meanPixelScaleArcsecPerPixel();
    const bool everyMagnitude = selection.magnitudeLimit >= kAnnotationNoMagnitudeLimit;

    std::vector<Candidate> candidates;
    candidates.reserve(sources.deepSky.size());
    for (const an::CatalogEntry& entry : sources.deepSky)
    {
        Candidate candidate;
        candidate.entry = &entry;
        candidate.designation = QString::fromStdString(entry.identifier).trimmed();
        candidate.name = QString::fromStdString(entry.commonName).trimmed();
        candidate.catalogue = catalogueOf(candidate.designation);
        candidate.kind = annotationKindOfType(entry.objectType);
        if (!catalogueChosen(candidate.catalogue, selection) || !kindChosen(candidate.kind, selection))
        {
            continue;
        }
        const bool named = !candidate.name.isEmpty();
        const bool notable =
            named || candidate.catalogue == Catalogue::Messier || candidate.catalogue == Catalogue::Caldwell;
        if (selection.namedOnly && !notable)
        {
            continue;
        }
        const std::optional<double> magnitude = measuredMagnitude(entry);
        if (magnitude.has_value())
        {
            if (!everyMagnitude && *magnitude > selection.magnitudeLimit)
            {
                continue;
            }
        }
        else if (!selection.unmeasured && !named)
        {
            continue;
        }
        if (entry.extent.has_value() && entry.extent->majorAxisArcminutes > 0.0 && arcsecondsPerPixel > 0.0)
        {
            candidate.majorPixels = entry.extent->majorAxisArcminutes * 60.0 / arcsecondsPerPixel;
        }
        if (!notable && candidate.majorPixels > 0.0 && candidate.majorPixels < selection.minimumSize * reference)
        {
            continue;
        }
        candidate.shape = detail::projectCatalogObjectAnnotation(solution, entry, 1.0, 72);
        if (!candidate.shape.is_valid() || !touchesFrame(candidate.shape, frame))
        {
            continue;
        }
        candidates.push_back(std::move(candidate));
    }

    // The preferred designation first, so the entries that name an object again merge into it.
    std::stable_sort(candidates.begin(), candidates.end(), [](const Candidate& left, const Candidate& right) {
        return static_cast<int>(left.catalogue) < static_cast<int>(right.catalogue);
    });

    struct Kept
    {
        Candidate candidate;
        QString name;
    };
    std::vector<Kept> kept;
    kept.reserve(candidates.size());
    std::unordered_map<std::uint64_t, std::vector<std::size_t>> byCell;
    QHash<QString, std::vector<std::size_t>> byName;
    const double samePositionPixels =
        arcsecondsPerPixel > 0.0 ? kSamePositionDegrees * 3600.0 / arcsecondsPerPixel : 1.0;
    const double cell = (std::max)(1.0, samePositionPixels);
    for (Candidate& candidate : candidates)
    {
        const an::SkyCoordinate& position = candidate.entry->coordinate;
        std::optional<std::size_t> same;
        const auto sameObject = [&](const std::size_t index, const double within) {
            const Kept& other = kept[index];
            return other.candidate.catalogue != candidate.catalogue &&
                   an::angularSeparationDegrees(other.candidate.entry->coordinate, position) <= within;
        };
        if (!candidate.name.isEmpty())
        {
            const auto found = byName.find(foldedName(candidate.name));
            if (found != byName.end())
            {
                for (const std::size_t index : found.value())
                {
                    const Kept& other = kept[index];
                    const double larger =
                        (std::max)(other.candidate.majorPixels, candidate.majorPixels) * arcsecondsPerPixel / 3600.0;
                    if (sameObject(index, (std::max)(kSameNameDegrees, larger)))
                    {
                        same = index;
                        break;
                    }
                }
            }
        }
        if (!same.has_value())
        {
            const QPointF centre = candidate.shape.center;
            for (int dy = -1; dy <= 1 && !same.has_value(); ++dy)
            {
                for (int dx = -1; dx <= 1 && !same.has_value(); ++dx)
                {
                    const auto found = byCell.find(cellKey(centre + QPointF(dx * cell, dy * cell), cell));
                    if (found == byCell.end())
                    {
                        continue;
                    }
                    for (const std::size_t index : found->second)
                    {
                        if (sameObject(index, kSamePositionDegrees))
                        {
                            same = index;
                            break;
                        }
                    }
                }
            }
        }
        if (same.has_value())
        {
            Kept& other = kept[*same];
            if (other.name.isEmpty() && !candidate.name.isEmpty())
            {
                other.name = candidate.name;
            }
            if (other.candidate.shape.outline.size() < 3 && candidate.shape.outline.size() >= 3)
            {
                other.candidate.shape.outline = candidate.shape.outline;
                other.candidate.majorPixels = candidate.majorPixels;
            }
            continue;
        }
        const std::size_t index = kept.size();
        byCell[cellKey(candidate.shape.center, cell)].push_back(index);
        if (!candidate.name.isEmpty())
        {
            byName[foldedName(candidate.name)].push_back(index);
        }
        QString name = candidate.name;
        kept.push_back(Kept{.candidate = std::move(candidate), .name = std::move(name)});
    }

    for (Kept& object : kept)
    {
        AnnotationMark mark;
        mark.kind = object.candidate.kind;
        mark.center = object.candidate.shape.center;
        if (object.candidate.shape.outline.size() >= 3)
        {
            mark.outline = QPolygonF(object.candidate.shape.outline);
            mark.coversFrame = coversFrame(mark.outline, frame);
        }
        else
        {
            mark.radius = kPointRadius * reference;
        }
        mark.designation = object.candidate.designation;
        mark.name = object.name;
        mark.magnitude = measuredMagnitude(*object.candidate.entry);
        mark.priority = deepSkyPriority(object.candidate.catalogue, !object.name.isEmpty(),
                                        object.candidate.majorPixels, mark.magnitude, reference);
        marks.push_back(std::move(mark));
    }
}

void addStars(const AnnotationSceneSources& sources, const AnnotationContent& content, const QRectF& frame,
              const double reference, std::vector<AnnotationMark>& marks)
{
    for (const an::CatalogEntry& star : sources.stars)
    {
        const std::optional<double> magnitude = measuredMagnitude(star);
        if (!magnitude.has_value() || *magnitude > content.stars.magnitudeLimit)
        {
            continue;
        }
        const AnnotationStarNaming naming = content.stars.naming;
        const QString name = QString::fromStdString(star.commonName).trimmed();
        QString designation = naming == AnnotationStarNaming::ProperNames
                                  ? QString()
                                  : QString::fromStdString(star.designation).trimmed();
        if (naming == AnnotationStarNaming::CatalogueNumbers && name.isEmpty() && designation.isEmpty())
        {
            designation = QString::fromStdString(star.identifier).trimmed();
        }
        const bool named = !name.isEmpty() || !designation.isEmpty();
        if (!named && !content.stars.markUnnamed)
        {
            continue;
        }
        const std::optional<QPointF> centre =
            projected(*sources.solution, star.coordinate.rightAscensionDegrees, star.coordinate.declinationDegrees);
        if (!centre.has_value() || !frame.contains(*centre))
        {
            continue;
        }
        AnnotationMark mark;
        mark.kind = AnnotationMarkKind::Star;
        mark.center = *centre;
        mark.radius = kStarRadius * reference * std::clamp(1.6 - 0.15 * *magnitude, 0.7, 1.6);
        mark.designation = designation;
        mark.name = name;
        mark.magnitude = magnitude;
        mark.markOnly = !named;
        // A named star before a designated one, and each brighter before fainter; a star marked alone after them all.
        const double brightness = 15.0 * std::clamp((kAnnotationFaintestStar - *magnitude) / 8.0, 0.0, 1.0);
        mark.priority = (!named ? 0.0 : (name.isEmpty() ? 25.0 : 65.0)) + brightness;
        marks.push_back(std::move(mark));
    }
}

[[nodiscard]] std::optional<AnnotationSkyAxes> axesAt(const an::WcsSolution& solution, const QPointF origin)
{
    const an::Result<an::SkyCoordinate> sky =
        solution.pixelToWorld(an::PixelCoordinate{.x = origin.x(), .y = origin.y()});
    const an::Result<an::SkyCoordinate> along =
        solution.pixelToWorld(an::PixelCoordinate{.x = origin.x() + 100.0, .y = origin.y()});
    if (!sky.hasValue() || !along.hasValue())
    {
        return std::nullopt;
    }
    const double arcsecondsPerPixel = an::angularSeparationDegrees(*sky, *along) * 3600.0 / 100.0;
    if (!(arcsecondsPerPixel > 0.0) || !std::isfinite(arcsecondsPerPixel))
    {
        return std::nullopt;
    }
    // A step of some twenty pixels' worth of sky, toward north - away from it past the pole - and toward east.
    const double step = 20.0 * arcsecondsPerPixel / 3600.0;
    const double declination = sky->declinationDegrees;
    const double rightAscension = sky->rightAscensionDegrees;
    const bool pastPole = declination + step > 89.9999;
    const std::optional<QPointF> northward =
        projected(solution, rightAscension, pastPole ? declination - step : declination + step);
    const double cosine = (std::max)(1.0e-6, std::cos(declination * std::numbers::pi / 180.0));
    const std::optional<QPointF> eastward = projected(solution, rightAscension + step / cosine, declination);
    if (!northward.has_value() || !eastward.has_value())
    {
        return std::nullopt;
    }
    const auto unit = [](const QPointF vector) -> std::optional<QPointF> {
        const double length = std::hypot(vector.x(), vector.y());
        if (!(length > 0.0) || !std::isfinite(length))
        {
            return std::nullopt;
        }
        return vector / length;
    };
    const std::optional<QPointF> north = unit(pastPole ? origin - *northward : *northward - origin);
    const std::optional<QPointF> east = unit(*eastward - origin);
    if (!north.has_value() || !east.has_value())
    {
        return std::nullopt;
    }
    return AnnotationSkyAxes{
        .origin = origin, .north = *north, .east = *east, .arcsecondsPerPixel = arcsecondsPerPixel};
}

[[nodiscard]] std::optional<double> cardDouble(std::span<const an::CodecCard> cards, const std::string_view keyword)
{
    const QString text = detail::headerCardText(cards, keyword);
    if (text.isEmpty())
    {
        return std::nullopt;
    }
    bool ok = false;
    const double value = text.toDouble(&ok);
    return ok && std::isfinite(value) ? std::optional<double>(value) : std::nullopt;
}

[[nodiscard]] QString numberText(const double value, const int decimals)
{
    QString text = QString::number(value, 'f', decimals);
    if (text.contains(QLatin1Char('.')))
    {
        while (text.endsWith(QLatin1Char('0')))
        {
            text.chop(1);
        }
        if (text.endsWith(QLatin1Char('.')))
        {
            text.chop(1);
        }
    }
    return text;
}

[[nodiscard]] QString durationText(const double seconds)
{
    if (seconds >= 3600.0)
    {
        const auto total = static_cast<long>(std::lround(seconds / 60.0));
        const long hours = total / 60;
        const long minutes = total % 60;
        return minutes == 0 ? tr("%1 h").arg(hours) : tr("%1 h %2 min").arg(hours).arg(minutes);
    }
    if (seconds >= 60.0)
    {
        const auto total = static_cast<long>(std::lround(seconds));
        const long minutes = total / 60;
        const long rest = total % 60;
        return rest == 0 ? tr("%1 min").arg(minutes) : tr("%1 min %2 s").arg(minutes).arg(rest);
    }
    return tr("%1 s").arg(numberText(seconds, seconds < 10.0 ? 2 : 1));
}

[[nodiscard]] QString fieldAngleText(const double arcminutes)
{
    return arcminutes >= 60.0 ? QStringLiteral("%1°").arg(QString::number(arcminutes / 60.0, 'f', 2))
                              : QStringLiteral("%1%2").arg(QString::number(arcminutes, 'f', 1)).arg(QChar(0x2032));
}

[[nodiscard]] AnnotationPanelText panelTextOf(const AnnotationSceneSources& sources, const AnnotationContent& content,
                                              const std::vector<AnnotationMark>& marks)
{
    AnnotationPanelText text;
    const std::span<const an::CodecCard> cards = sources.cards;
    const an::WcsSolution& solution = *sources.solution;
    text.title = content.title.trimmed();
    if (text.title.isEmpty())
    {
        text.title = detail::headerCardText(cards, "OBJECT");
    }
    if (text.title.isEmpty())
    {
        for (const AnnotationMark& mark : marks)
        {
            if (mark.kind != AnnotationMarkKind::Star && !mark.name.isEmpty())
            {
                text.title = mark.name;
                break;
            }
        }
    }
    text.credit = content.credit.trimmed();

    const AnnotationPanelFacts& facts = content.facts;
    const auto add = [&text](const QString& label, const QString& value) {
        if (!value.trimmed().isEmpty())
        {
            text.facts.push_back(AnnotationFact{.label = label, .value = value.trimmed()});
        }
    };
    if (facts.coordinates)
    {
        if (const an::Result<an::SkyCoordinate> centre = solution.fieldCenter(); centre.hasValue())
        {
            add(tr("Center"), QStringLiteral("%1   %2").arg(rightAscensionLabel(centre->rightAscensionDegrees, 0u),
                                                            typeset(declinationLabel(centre->declinationDegrees, 0u))));
        }
    }
    if (facts.field)
    {
        const an::WcsFieldOfView field = solution.fieldOfViewArcmin();
        const double scale = solution.meanPixelScaleArcsecPerPixel();
        if (field.widthArcmin > 0.0 && field.heightArcmin > 0.0)
        {
            add(tr("Field"), QStringLiteral("%1 × %2  ·  %3%4/px")
                                 .arg(fieldAngleText(field.widthArcmin), fieldAngleText(field.heightArcmin),
                                      QString::number(scale, 'f', scale < 10.0 ? 2 : 1))
                                 .arg(QChar(0x2033)));
        }
        add(tr("Rotation"), QStringLiteral("%1°").arg(QString::number(solution.orientationDegrees(), 'f', 1)));
    }
    if (facts.optics)
    {
        QStringList parts;
        const QString telescope = detail::headerCardText(cards, "TELESCOP");
        if (!telescope.isEmpty())
        {
            parts << telescope;
        }
        const std::optional<double> focal = cardDouble(cards, "FOCALLEN");
        if (focal.has_value() && *focal > 0.0)
        {
            QString optics = tr("%1 mm").arg(numberText(*focal, 0));
            std::optional<double> ratio = cardDouble(cards, "FOCRATIO");
            if (!ratio.has_value())
            {
                if (const std::optional<double> aperture = cardDouble(cards, "APTDIA");
                    aperture.has_value() && *aperture > 0.0)
                {
                    ratio = *focal / *aperture;
                }
            }
            if (ratio.has_value() && *ratio > 0.0)
            {
                optics += QStringLiteral(" f/%1").arg(numberText(*ratio, 1));
            }
            parts << optics;
        }
        add(tr("Optics"), parts.join(QStringLiteral("  ·  ")));
    }
    if (facts.camera)
    {
        add(tr("Camera"), detail::headerCardText(cards, "INSTRUME"));
    }
    if (facts.filter)
    {
        add(tr("Filter"), detail::headerCardText(cards, "FILTER"));
    }
    if (facts.date)
    {
        QString date = detail::headerCardText(cards, "DATE-OBS");
        if (date.size() >= 10 && date.at(4) == QLatin1Char('-'))
        {
            date = date.left(10);
        }
        add(tr("Date"), date);
    }
    if (facts.integration)
    {
        const std::optional<double> total = cardDouble(cards, "TOTEXP");
        const std::optional<double> exposure = cardDouble(cards, "EXPTIME");
        std::optional<double> frames = cardDouble(cards, "STACKCNT");
        if (!frames.has_value())
        {
            frames = cardDouble(cards, "NCOMBINE");
        }
        const bool stacked = frames.has_value() && *frames > 1.0;
        const std::optional<double> seconds = total.has_value() ? total : exposure;
        if (seconds.has_value() && *seconds > 0.0)
        {
            QString value = durationText(*seconds);
            if (stacked)
            {
                value += QStringLiteral("  ·  ") + tr("%1 frames").arg(numberText(*frames, 0));
            }
            add(stacked || total.has_value() ? tr("Integration") : tr("Exposure"), value);
        }
    }
    return text;
}

} // namespace

AnnotationMarkKind annotationKindOfType(const std::string_view objectType) noexcept
{
    if (containsFolded(objectType, "planetary"))
    {
        return AnnotationMarkKind::PlanetaryNebula;
    }
    if (containsFolded(objectType, "globular"))
    {
        return AnnotationMarkKind::GlobularCluster;
    }
    if (containsFolded(objectType, "nebul") || containsFolded(objectType, "supernova") ||
        containsFolded(objectType, "remnant"))
    {
        return AnnotationMarkKind::Nebula;
    }
    if (containsFolded(objectType, "galax"))
    {
        return AnnotationMarkKind::Galaxy;
    }
    if (containsFolded(objectType, "cluster"))
    {
        return AnnotationMarkKind::OpenCluster;
    }
    return AnnotationMarkKind::OtherObject;
}

AnnotationScene buildAnnotationScene(const AnnotationSceneSources& sources, const AnnotationContent& content)
{
    AnnotationScene scene;
    if (sources.solution == nullptr)
    {
        return scene;
    }
    const an::WcsSolution& solution = *sources.solution;
    scene.imageSize = QSize(static_cast<int>(solution.imageWidth()), static_cast<int>(solution.imageHeight()));
    scene.referenceLength = annotationReferenceLength(scene.imageSize);
    if (scene.referenceLength <= 0.0)
    {
        return scene;
    }
    const QRectF frame(0.0, 0.0, scene.imageSize.width(), scene.imageSize.height());

    if (content.layers.grid)
    {
        scene.grid = buildGrid(solution, frame, content);
    }
    if (content.layers.deepSky)
    {
        addDeepSky(sources, content, frame, scene.referenceLength, scene.marks);
    }
    if (content.layers.stars)
    {
        addStars(sources, content, frame, scene.referenceLength, scene.marks);
    }
    std::stable_sort(scene.marks.begin(), scene.marks.end(),
                     [](const AnnotationMark& left, const AnnotationMark& right) {
                         if (left.priority != right.priority)
                         {
                             return left.priority > right.priority;
                         }
                         return left.designation < right.designation;
                     });

    const double inset =
        (std::min)(kCornerInset * scene.referenceLength, 0.5 * (std::min)(frame.width(), frame.height()));
    const std::array<QPointF, 4u> corners{
        QPointF(frame.left() + inset, frame.top() + inset), QPointF(frame.right() - inset, frame.top() + inset),
        QPointF(frame.left() + inset, frame.bottom() - inset), QPointF(frame.right() - inset, frame.bottom() - inset)};
    for (std::size_t index = 0u; index < corners.size(); ++index)
    {
        scene.cornerAxes[index] = axesAt(solution, corners[index]);
    }

    if (content.layers.infoPanel)
    {
        scene.panel = panelTextOf(sources, content, scene.marks);
    }
    return scene;
}

double annotationGridSpacingDegrees(const double spanDegrees, const bool rightAscension,
                                    const AnnotationGridDensity density) noexcept
{
    const double wanted = (std::max)(0.0, spanDegrees) * 3600.0 / static_cast<double>(targetLines(density));
    const double unit = rightAscension ? 15.0 : 1.0;
    const auto pick = [wanted, unit](const auto& steps) {
        for (const double step : steps)
        {
            if (step * unit >= wanted * 0.8)
            {
                return step * unit / 3600.0;
            }
        }
        return steps.back() * unit / 3600.0;
    };
    return rightAscension ? pick(kTimeSteps) : pick(kDeclinationSteps);
}

double annotationScaleBarArcseconds(const double frameWidthArcseconds) noexcept
{
    constexpr std::array<double, 21> kLengths{1.0,    2.0,    5.0,    10.0,    15.0,    20.0,    30.0,
                                              60.0,   120.0,  300.0,  600.0,   900.0,   1200.0,  1800.0,
                                              3600.0, 5400.0, 7200.0, 10800.0, 18000.0, 36000.0, 72000.0};
    if (!(frameWidthArcseconds > 0.0) || !std::isfinite(frameWidthArcseconds))
    {
        return 0.0;
    }
    const double wanted = frameWidthArcseconds / 6.0;
    double best = kLengths.front();
    double bestDistance = std::numeric_limits<double>::infinity();
    for (const double length : kLengths)
    {
        if (length > frameWidthArcseconds * 0.4)
        {
            break;
        }
        const double distance = std::abs(std::log(length / wanted));
        if (distance < bestDistance)
        {
            bestDistance = distance;
            best = length;
        }
    }
    return best;
}

QString annotationAngleText(const double arcseconds)
{
    if (arcseconds >= 3600.0)
    {
        return QStringLiteral("%1°").arg(numberText(arcseconds / 3600.0, 1));
    }
    if (arcseconds >= 60.0)
    {
        return QStringLiteral("%1%2").arg(numberText(arcseconds / 60.0, 1)).arg(QChar(0x2032));
    }
    return QStringLiteral("%1%2").arg(numberText(arcseconds, 1)).arg(QChar(0x2033));
}

} // namespace an::ui
