// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "controls/annotation/AnnotationLayout.h"

#include <QFont>
#include <QFontMetricsF>
#include <QTransform>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numbers>
#include <unordered_map>
#include <utility>

namespace an::ui
{
namespace
{

/// The lengths of a style at its defaults, as fractions of the reference length.
constexpr double kFontFraction = 0.014;
constexpr double kLineFraction = 0.0011;
constexpr double kMarginFraction = 0.018;

/// The size text is shaped at, in pixels, before it is scaled to its place.
constexpr int kShapeSize = 100;

/// A label's second line, as a fraction of its first's size.
constexpr double kSecondLineScale = 0.82;

/// A grid label's size, as a fraction of a label's.
constexpr double kGridLabelScale = 0.86;

/// The panel's title, credit and facts, as fractions of a label's size.
constexpr double kTitleScale = 1.45;
constexpr double kCreditScale = 0.92;
constexpr double kFactScale = 0.8;

/// The compass's arm, as a multiple of a label's size.
constexpr double kCompassArm = 3.6;

/// An object whose box spans more than this fraction of the frame on a side is labelled inside itself.
constexpr double kLargeObjectFraction = 0.35;

/// A mark whose box spans less than this many labels' sizes is one a label must not cover.
constexpr double kSmallMarkSizes = 4.0;

/// Shapes text into the outlines of its letters at a size, from one typeface.
class TextShaper
{
public:
    explicit TextShaper(const QString& family)
    {
        for (const bool strong : {false, true})
        {
            QFont font;
            if (!family.trimmed().isEmpty())
            {
                font.setFamily(family);
            }
            font.setPixelSize(kShapeSize);
            font.setWeight(strong ? QFont::DemiBold : QFont::Normal);
            font.setHintingPreference(QFont::PreferNoHinting);
            font.setStyleStrategy(QFont::PreferOutline);
            font.setKerning(true);
            m_fonts[strong ? 1 : 0] = font;
        }
    }

    /// One line of text, its baseline at zero and its first advance at zero.
    struct Line
    {
        QPainterPath path;
        double width = 0.0;
        double ascent = 0.0;
        double descent = 0.0;

        [[nodiscard]] double height() const noexcept
        {
            return ascent + descent;
        }
    };

    [[nodiscard]] Line shape(const QString& text, const double size, const bool strong) const
    {
        Line line;
        if (text.isEmpty() || !(size > 0.0))
        {
            return line;
        }
        const QFont& font = m_fonts[strong ? 1 : 0];
        const QFontMetricsF metrics(font);
        const double scale = size / kShapeSize;
        QPainterPath path;
        path.addText(QPointF(0.0, 0.0), font, text);
        line.path = QTransform::fromScale(scale, scale).map(path);
        line.width = metrics.horizontalAdvance(text) * scale;
        line.ascent = metrics.ascent() * scale;
        line.descent = metrics.descent() * scale;
        return line;
    }

private:
    std::array<QFont, 2u> m_fonts;
};

/// The boxes placed so far, kept in cells so a test reads only the boxes near the box it tests.
class Occupancy
{
public:
    Occupancy(const QRectF& frame, const double cell) : m_frame(frame), m_cell((std::max)(1.0, cell))
    {
        m_columns = (std::max)(1, static_cast<int>(std::ceil(frame.width() / m_cell)));
        m_rows = (std::max)(1, static_cast<int>(std::ceil(frame.height() / m_cell)));
        m_cells.resize(static_cast<std::size_t>(m_columns) * static_cast<std::size_t>(m_rows));
    }

    void add(const QRectF& box, const std::size_t owner = std::numeric_limits<std::size_t>::max())
    {
        const std::size_t index = m_boxes.size();
        m_boxes.push_back(Entry{.box = box, .owner = owner});
        visit(box, [this, index](std::vector<std::uint32_t>& cell) {
            cell.push_back(static_cast<std::uint32_t>(index));
            return false;
        });
    }

    /// Whether a box meets any box placed.
    [[nodiscard]] bool intersects(const QRectF& box) const
    {
        bool found = false;
        visit(box, [this, &box, &found](const std::vector<std::uint32_t>& cell) {
            for (const std::uint32_t index : cell)
            {
                if (m_boxes[index].box.intersects(box))
                {
                    found = true;
                    return true;
                }
            }
            return false;
        });
        return found;
    }

    /// How many boxes a box meets, its owner's own left out.
    [[nodiscard]] int count(const QRectF& box, const std::size_t exceptOwner) const
    {
        std::vector<std::uint32_t> met;
        visit(box, [this, &box, &met, exceptOwner](const std::vector<std::uint32_t>& cell) {
            for (const std::uint32_t index : cell)
            {
                if (m_boxes[index].owner != exceptOwner && m_boxes[index].box.intersects(box))
                {
                    met.push_back(index);
                }
            }
            return false;
        });
        std::sort(met.begin(), met.end());
        return static_cast<int>(std::unique(met.begin(), met.end()) - met.begin());
    }

private:
    struct Entry
    {
        QRectF box;
        std::size_t owner = 0u;
    };

    template <typename VisitT> void visit(const QRectF& box, VisitT&& visitCell)
    {
        const auto [firstColumn, lastColumn, firstRow, lastRow] = span(box);
        for (int row = firstRow; row <= lastRow; ++row)
        {
            for (int column = firstColumn; column <= lastColumn; ++column)
            {
                if (visitCell(m_cells[static_cast<std::size_t>(row) * static_cast<std::size_t>(m_columns) +
                                      static_cast<std::size_t>(column)]))
                {
                    return;
                }
            }
        }
    }

    template <typename VisitT> void visit(const QRectF& box, VisitT&& visitCell) const
    {
        const auto [firstColumn, lastColumn, firstRow, lastRow] = span(box);
        for (int row = firstRow; row <= lastRow; ++row)
        {
            for (int column = firstColumn; column <= lastColumn; ++column)
            {
                if (visitCell(m_cells[static_cast<std::size_t>(row) * static_cast<std::size_t>(m_columns) +
                                      static_cast<std::size_t>(column)]))
                {
                    return;
                }
            }
        }
    }

    [[nodiscard]] std::array<int, 4> span(const QRectF& box) const
    {
        const auto cellOf = [this](const double offset, const int count) {
            return std::clamp(static_cast<int>(std::floor(offset / m_cell)), 0, count - 1);
        };
        return {cellOf(box.left() - m_frame.left(), m_columns), cellOf(box.right() - m_frame.left(), m_columns),
                cellOf(box.top() - m_frame.top(), m_rows), cellOf(box.bottom() - m_frame.top(), m_rows)};
    }

    QRectF m_frame;
    double m_cell = 1.0;
    int m_columns = 1;
    int m_rows = 1;
    std::vector<std::vector<std::uint32_t>> m_cells;
    std::vector<Entry> m_boxes;
};

/// The padding around a label's letters, as fractions of its size, for each backing.
struct Padding
{
    double horizontal = 0.0;
    double vertical = 0.0;
};

[[nodiscard]] Padding paddingOf(const AnnotationLabelBacking backing) noexcept
{
    switch (backing)
    {
    case AnnotationLabelBacking::Badge:
        return Padding{.horizontal = 0.42, .vertical = 0.2};
    case AnnotationLabelBacking::Plain:
        return Padding{.horizontal = 0.06, .vertical = 0.04};
    case AnnotationLabelBacking::Halo:
        break;
    }
    return Padding{.horizontal = 0.14, .vertical = 0.08};
}

/// A label shaped but not yet placed: its lines laid out about a box whose top-left corner is at the origin.
struct ShapedLabel
{
    QPainterPath primary;
    QPainterPath secondary;
    QSizeF size;
};

[[nodiscard]] ShapedLabel shapeLabel(const TextShaper& shaper, const QString& first, const QString& second,
                                     const double size, const bool strongFirst, const Padding padding)
{
    ShapedLabel label;
    const TextShaper::Line top = shaper.shape(first, size, strongFirst);
    const TextShaper::Line bottom = shaper.shape(second, size * kSecondLineScale, false);
    const double horizontal = padding.horizontal * size;
    const double vertical = padding.vertical * size;
    const double inner = (std::max)(top.width, bottom.width);
    const double lineGap = second.isEmpty() ? 0.0 : 0.06 * size;
    const double height = top.height() + (second.isEmpty() ? 0.0 : lineGap + bottom.height());
    label.size = QSizeF(inner + 2.0 * horizontal, height + 2.0 * vertical);
    const double firstBaseline = vertical + top.ascent;
    label.primary = top.path.translated(horizontal + (inner - top.width) * 0.5, firstBaseline);
    if (!second.isEmpty())
    {
        const double secondBaseline = firstBaseline + top.descent + lineGap + bottom.ascent;
        label.secondary = bottom.path.translated(horizontal + (inner - bottom.width) * 0.5, secondBaseline);
    }
    return label;
}

/// A mark's label text: its first line and its second, as the choices say.
[[nodiscard]] std::pair<QString, QString> labelLines(const AnnotationMark& mark, const AnnotationLabelText text)
{
    const QString& designation = mark.designation;
    const QString& name = mark.name;
    switch (text)
    {
    case AnnotationLabelText::Designation:
        return {designation.isEmpty() ? name : designation, QString()};
    case AnnotationLabelText::Name:
        return {name.isEmpty() ? designation : name, QString()};
    case AnnotationLabelText::DesignationAndName:
        break;
    }
    if (designation.isEmpty())
    {
        return {name, QString()};
    }
    if (name.isEmpty() || name.compare(designation, Qt::CaseInsensitive) == 0)
    {
        return {designation, QString()};
    }
    return {designation, name};
}

/// The box a mark's label is set about.
[[nodiscard]] QRectF anchorOf(const AnnotationMark& mark)
{
    if (mark.outline.size() >= 3)
    {
        return mark.outline.boundingRect();
    }
    return QRectF(mark.center.x() - mark.radius, mark.center.y() - mark.radius, 2.0 * mark.radius, 2.0 * mark.radius);
}

/// The point on a box's edge nearest a point, or the point when the box holds it.
[[nodiscard]] QPointF nearestOnBox(const QRectF& box, const QPointF point)
{
    return QPointF(std::clamp(point.x(), box.left(), box.right()), std::clamp(point.y(), box.top(), box.bottom()));
}

/// Where a leader reaches a mark from a point outside it: its circle or the edge of its outline's box.
[[nodiscard]] QPointF leaderEnd(const AnnotationMark& mark, const QRectF& anchor, const QPointF from)
{
    const QPointF delta = from - mark.center;
    const double length = std::hypot(delta.x(), delta.y());
    if (!(length > 0.0))
    {
        return mark.center;
    }
    if (mark.outline.size() < 3)
    {
        return mark.center + delta * (mark.radius / length);
    }
    return nearestOnBox(anchor, from);
}

enum class Place : std::uint8_t
{
    Top,
    Bottom,
    Right,
    Left,
    TopRight,
    TopLeft,
    BottomRight,
    BottomLeft
};

constexpr std::array kOutlinePlaces{Place::Top,      Place::Bottom,  Place::Right,       Place::Left,
                                    Place::TopRight, Place::TopLeft, Place::BottomRight, Place::BottomLeft};
constexpr std::array kPointPlaces{Place::Right,  Place::TopRight, Place::BottomRight, Place::Top,
                                  Place::Bottom, Place::Left,     Place::TopLeft,     Place::BottomLeft};

[[nodiscard]] QPointF placeCorner(const Place place, const QRectF& anchor, const QSizeF size, const double gap)
{
    const double halfGap = gap * 0.5;
    switch (place)
    {
    case Place::Top:
        return QPointF(anchor.center().x() - size.width() * 0.5, anchor.top() - gap - size.height());
    case Place::Bottom:
        return QPointF(anchor.center().x() - size.width() * 0.5, anchor.bottom() + gap);
    case Place::Right:
        return QPointF(anchor.right() + gap, anchor.center().y() - size.height() * 0.5);
    case Place::Left:
        return QPointF(anchor.left() - gap - size.width(), anchor.center().y() - size.height() * 0.5);
    case Place::TopRight:
        return QPointF(anchor.right() + halfGap, anchor.top() - halfGap - size.height());
    case Place::TopLeft:
        return QPointF(anchor.left() - halfGap - size.width(), anchor.top() - halfGap - size.height());
    case Place::BottomRight:
        return QPointF(anchor.right() + halfGap, anchor.bottom() + halfGap);
    case Place::BottomLeft:
        break;
    }
    return QPointF(anchor.left() - halfGap - size.width(), anchor.bottom() + halfGap);
}

/// The corners' order inside a stack: the panel nearest its corner, then the scale bar, then the compass.
enum class Decoration : std::uint8_t
{
    Panel,
    ScaleBar,
    Compass
};

[[nodiscard]] bool isTop(const AnnotationCorner corner) noexcept
{
    return corner == AnnotationCorner::TopLeft || corner == AnnotationCorner::TopRight;
}

[[nodiscard]] bool isLeft(const AnnotationCorner corner) noexcept
{
    return corner == AnnotationCorner::TopLeft || corner == AnnotationCorner::BottomLeft;
}

/// The boxes stacked in each corner so far, from the corner inward.
class CornerStacks
{
public:
    CornerStacks(const QRectF& frame, const double margin) : m_frame(frame), m_margin(margin)
    {
        m_used.fill(0.0);
    }

    /// The top-left corner of the next box of a size in a corner, and the box taken.
    [[nodiscard]] QRectF take(const AnnotationCorner corner, const QSizeF size)
    {
        const auto index = static_cast<std::size_t>(corner);
        const double x = isLeft(corner) ? m_frame.left() + m_margin : m_frame.right() - m_margin - size.width();
        const double offset = m_margin + m_used[index];
        const double y = isTop(corner) ? m_frame.top() + offset : m_frame.bottom() - offset - size.height();
        m_used[index] += size.height() + m_margin * 0.6;
        return QRectF(QPointF(x, y), size);
    }

private:
    QRectF m_frame;
    double m_margin = 0.0;
    std::array<double, 4u> m_used{};
};

[[nodiscard]] std::optional<AnnotationPanelLayout>
layoutPanel(const AnnotationScene& scene, const AnnotationMetrics& metrics, const TextShaper& shaper)
{
    const AnnotationPanelText& text = scene.panel;
    if (text.title.isEmpty() && text.credit.isEmpty() && text.facts.empty())
    {
        return std::nullopt;
    }
    const double font = metrics.fontSize;
    const double padding = 0.9 * font;
    AnnotationPanelLayout panel;
    panel.radius = 0.45 * font;

    double y = padding;
    double width = 0.0;
    if (!text.title.isEmpty())
    {
        TextShaper::Line title = shaper.shape(text.title, font * kTitleScale, true);
        width = (std::max)(width, title.width);
        const double baseline = y + title.ascent;
        y = baseline + title.descent;
        panel.title = title.path.translated(padding, baseline);
    }
    if (!text.credit.isEmpty())
    {
        y += text.title.isEmpty() ? 0.0 : 0.18 * font;
        TextShaper::Line credit = shaper.shape(text.credit, font * kCreditScale, false);
        width = (std::max)(width, credit.width);
        const double baseline = y + credit.ascent;
        y = baseline + credit.descent;
        panel.credit = credit.path.translated(padding, baseline);
    }
    if (!text.facts.empty())
    {
        const bool headed = !text.title.isEmpty() || !text.credit.isEmpty();
        double ruleY = 0.0;
        if (headed)
        {
            y += 0.45 * font;
            ruleY = y;
            y += 0.55 * font;
        }
        const double factSize = font * kFactScale;
        std::vector<TextShaper::Line> labels;
        std::vector<TextShaper::Line> values;
        double labelColumn = 0.0;
        double valueColumn = 0.0;
        for (const AnnotationFact& fact : text.facts)
        {
            labels.push_back(shaper.shape(fact.label, factSize, false));
            values.push_back(shaper.shape(fact.value, factSize, false));
            labelColumn = (std::max)(labelColumn, labels.back().width);
            valueColumn = (std::max)(valueColumn, values.back().width);
        }
        const double columnGap = 0.9 * factSize;
        const double lineHeight = 1.42 * factSize;
        for (std::size_t index = 0u; index < labels.size(); ++index)
        {
            const double baseline = y + labels[index].ascent;
            panel.factLabels.addPath(labels[index].path.translated(padding, baseline));
            panel.factValues.addPath(values[index].path.translated(padding + labelColumn + columnGap, baseline));
            y += lineHeight;
        }
        y -= lineHeight - (labels.back().ascent + labels.back().descent);
        width = (std::max)(width, labelColumn + columnGap + valueColumn);
        if (headed)
        {
            panel.rule = QLineF(padding, ruleY, padding + width, ruleY);
        }
    }
    panel.box = QRectF(0.0, 0.0, width + 2.0 * padding, y + padding);
    return panel;
}

/// Moves a panel laid out at the origin to its box.
void movePanel(AnnotationPanelLayout& panel, const QPointF to)
{
    panel.box.moveTopLeft(to);
    panel.title.translate(to);
    panel.credit.translate(to);
    panel.factLabels.translate(to);
    panel.factValues.translate(to);
    if (panel.rule.has_value())
    {
        panel.rule->translate(to);
    }
}

[[nodiscard]] std::optional<AnnotationScaleBarLayout> layoutScaleBar(const AnnotationScene& scene,
                                                                     const AnnotationMetrics& metrics,
                                                                     const TextShaper& shaper,
                                                                     const AnnotationCorner corner, QSizeF& size)
{
    const std::optional<AnnotationSkyAxes>& axes = scene.cornerAxes[static_cast<std::size_t>(corner)];
    if (!axes.has_value() || !(axes->arcsecondsPerPixel > 0.0))
    {
        return std::nullopt;
    }
    const double frameArcseconds = scene.imageSize.width() * axes->arcsecondsPerPixel;
    const double arcseconds = annotationScaleBarArcseconds(frameArcseconds);
    if (!(arcseconds > 0.0))
    {
        return std::nullopt;
    }
    const double length = arcseconds / axes->arcsecondsPerPixel;
    const TextShaper::Line label = shaper.shape(annotationAngleText(arcseconds), metrics.fontSize * 0.95, true);
    AnnotationScaleBarLayout bar;
    bar.tickLength = 0.7 * metrics.fontSize;
    const double width = (std::max)(length, label.width);
    const double labelGap = 0.3 * metrics.fontSize;
    const double height = label.height() + labelGap + bar.tickLength;
    size = QSizeF(width, height);
    const double barY = label.height() + labelGap + bar.tickLength * 0.5;
    const double barX = (width - length) * 0.5;
    bar.bar = QLineF(barX, barY, barX + length, barY);
    bar.label = label.path.translated((width - label.width) * 0.5, label.ascent);
    bar.box = QRectF(QPointF(0.0, 0.0), size);
    return bar;
}

[[nodiscard]] std::optional<AnnotationCompassLayout> layoutCompass(const AnnotationScene& scene,
                                                                   const AnnotationMetrics& metrics,
                                                                   const TextShaper& shaper,
                                                                   const AnnotationCorner corner, QSizeF& size)
{
    const std::optional<AnnotationSkyAxes>& axes = scene.cornerAxes[static_cast<std::size_t>(corner)];
    if (!axes.has_value())
    {
        return std::nullopt;
    }
    const double arm = kCompassArm * metrics.fontSize;
    const double letterSize = 0.95 * metrics.fontSize;
    const double reach = arm + 1.1 * letterSize;
    size = QSizeF(2.0 * reach, 2.0 * reach);
    AnnotationCompassLayout compass;
    compass.origin = QPointF(reach, reach);
    compass.northTip = compass.origin + axes->north * arm;
    compass.eastTip = compass.origin + axes->east * (arm * 0.72);
    const auto letterAt = [&shaper, letterSize](const QString& letter, const QPointF centre) {
        const TextShaper::Line line = shaper.shape(letter, letterSize, true);
        return line.path.translated(centre.x() - line.width * 0.5, centre.y() + (line.ascent - line.descent) * 0.5);
    };
    compass.northLetter = letterAt(QStringLiteral("N"), compass.northTip + axes->north * (0.75 * letterSize));
    compass.eastLetter = letterAt(QStringLiteral("E"), compass.eastTip + axes->east * (0.75 * letterSize));
    compass.box = QRectF(QPointF(0.0, 0.0), size);
    return compass;
}

void moveCompass(AnnotationCompassLayout& compass, const QPointF to)
{
    compass.origin += to;
    compass.northTip += to;
    compass.eastTip += to;
    compass.northLetter.translate(to);
    compass.eastLetter.translate(to);
    compass.box.moveTopLeft(to);
}

void moveScaleBar(AnnotationScaleBarLayout& bar, const QPointF to)
{
    bar.bar.translate(to);
    bar.label.translate(to);
    bar.box.moveTopLeft(to);
}

/// Places the grid's labels just inside the frame where their lines meet its edges: a declination line's at the left
/// edge, a right ascension line's at the bottom, else where the line meets the frame first.
void placeGridLabels(const AnnotationScene& scene, const AnnotationMetrics& metrics, const TextShaper& shaper,
                     const AnnotationStyle& style, const QRectF& frame, Occupancy& occupied,
                     std::vector<AnnotationLabel>& labels)
{
    const Padding padding = paddingOf(style.labelBacking);
    const double size = metrics.fontSize * kGridLabelScale;
    const double inset = metrics.margin * 0.35;
    const double gap = metrics.gap * 0.6;
    for (std::size_t index = 0u; index < scene.grid.size(); ++index)
    {
        const AnnotationGridLine& line = scene.grid[index];
        if (line.label.isEmpty() || line.crossings.empty())
        {
            continue;
        }
        const std::array<AnnotationEdge, 4> preference =
            line.rightAscension
                ? std::array{AnnotationEdge::Bottom, AnnotationEdge::Left, AnnotationEdge::Top, AnnotationEdge::Right}
                : std::array{AnnotationEdge::Left, AnnotationEdge::Bottom, AnnotationEdge::Right, AnnotationEdge::Top};
        const AnnotationGridLine::Crossing* chosen = nullptr;
        for (const AnnotationEdge edge : preference)
        {
            for (const AnnotationGridLine::Crossing& crossing : line.crossings)
            {
                if (crossing.edge == edge)
                {
                    chosen = &crossing;
                    break;
                }
            }
            if (chosen != nullptr)
            {
                break;
            }
        }
        if (chosen == nullptr)
        {
            continue;
        }
        const ShapedLabel shaped = shapeLabel(shaper, line.label, QString(), size, false, padding);
        QPointF corner;
        const QPointF at = chosen->point;
        switch (chosen->edge)
        {
        case AnnotationEdge::Left:
            corner = QPointF(frame.left() + inset, at.y() - gap - shaped.size.height());
            break;
        case AnnotationEdge::Right:
            corner = QPointF(frame.right() - inset - shaped.size.width(), at.y() - gap - shaped.size.height());
            break;
        case AnnotationEdge::Top:
            corner = QPointF(at.x() + gap, frame.top() + inset);
            break;
        case AnnotationEdge::Bottom:
            corner = QPointF(at.x() + gap, frame.bottom() - inset - shaped.size.height());
            break;
        }
        corner.setX(std::clamp(corner.x(), frame.left() + inset,
                               (std::max)(frame.left() + inset, frame.right() - inset - shaped.size.width())));
        corner.setY(std::clamp(corner.y(), frame.top() + inset,
                               (std::max)(frame.top() + inset, frame.bottom() - inset - shaped.size.height())));
        const QRectF box(corner, shaped.size);
        if (!frame.contains(box) || occupied.intersects(box))
        {
            continue;
        }
        occupied.add(box);
        labels.push_back(AnnotationLabel{.role = AnnotationLabelRole::Grid,
                                         .source = index,
                                         .primary = shaped.primary.translated(corner),
                                         .secondary = QPainterPath(),
                                         .box = box,
                                         .leader = std::nullopt});
    }
}

} // namespace

AnnotationMetrics annotationMetrics(const double referenceLength, const AnnotationStyle& style) noexcept
{
    const double reference = std::isfinite(referenceLength) ? (std::max)(0.0, referenceLength) : 0.0;
    const double textSize = std::clamp(std::isfinite(style.textSize) ? style.textSize : 1.0, kAnnotationMinimumTextSize,
                                       kAnnotationMaximumTextSize);
    const double lineWidth = std::clamp(std::isfinite(style.lineWidth) ? style.lineWidth : 1.0,
                                        kAnnotationMinimumLineWidth, kAnnotationMaximumLineWidth);
    AnnotationMetrics metrics;
    metrics.reference = reference;
    metrics.fontSize = kFontFraction * reference * textSize;
    metrics.lineWidth = kLineFraction * reference * lineWidth;
    metrics.gridWidth = 0.7 * metrics.lineWidth;
    metrics.haloWidth = (std::max)(0.2 * metrics.fontSize, 2.4 * metrics.lineWidth);
    metrics.margin = kMarginFraction * reference;
    metrics.gap = 0.35 * metrics.fontSize;
    return metrics;
}

AnnotationLayout layoutAnnotations(const AnnotationScene& scene, const AnnotationContent& content,
                                   const AnnotationStyle& style)
{
    AnnotationLayout layout;
    layout.labelled.assign(scene.marks.size(), false);
    if (scene.imageSize.isEmpty() || !(scene.referenceLength > 0.0))
    {
        return layout;
    }
    layout.metrics = annotationMetrics(scene.referenceLength, style);
    const AnnotationMetrics& metrics = layout.metrics;
    const QRectF frame(0.0, 0.0, scene.imageSize.width(), scene.imageSize.height());
    const TextShaper shaper(style.fontFamily);
    Occupancy occupied(frame, 4.0 * metrics.fontSize);

    // The decorations first, stacked from their corners inward: they keep their places whatever the labels do.
    CornerStacks stacks(frame, metrics.margin);
    std::array<std::pair<Decoration, AnnotationCorner>, 3> decorations{
        std::pair{Decoration::Panel, style.panelCorner}, std::pair{Decoration::ScaleBar, style.scaleBarCorner},
        std::pair{Decoration::Compass, style.compassCorner}};
    for (const auto& [decoration, corner] : decorations)
    {
        switch (decoration)
        {
        case Decoration::Panel:
            if (content.layers.infoPanel)
            {
                if (std::optional<AnnotationPanelLayout> panel = layoutPanel(scene, metrics, shaper))
                {
                    const QRectF box = stacks.take(corner, panel->box.size());
                    movePanel(*panel, box.topLeft());
                    occupied.add(panel->box);
                    layout.panel = std::move(panel);
                }
            }
            break;
        case Decoration::ScaleBar:
            if (content.layers.scaleBar)
            {
                QSizeF size;
                if (std::optional<AnnotationScaleBarLayout> bar = layoutScaleBar(scene, metrics, shaper, corner, size))
                {
                    const QRectF box = stacks.take(corner, size);
                    moveScaleBar(*bar, box.topLeft());
                    occupied.add(bar->box);
                    layout.scaleBar = std::move(bar);
                }
            }
            break;
        case Decoration::Compass:
            if (content.layers.compass)
            {
                QSizeF size;
                if (std::optional<AnnotationCompassLayout> compass =
                        layoutCompass(scene, metrics, shaper, corner, size))
                {
                    const QRectF box = stacks.take(corner, size);
                    moveCompass(*compass, box.topLeft());
                    occupied.add(compass->box);
                    layout.compass = std::move(compass);
                }
            }
            break;
        }
    }

    if (content.layers.grid && content.gridLabels)
    {
        placeGridLabels(scene, metrics, shaper, style, frame, occupied, layout.labels);
    }

    // The small marks a label must not cover.
    Occupancy marks(frame, 4.0 * metrics.fontSize);
    std::vector<QRectF> anchors;
    anchors.reserve(scene.marks.size());
    for (std::size_t index = 0u; index < scene.marks.size(); ++index)
    {
        anchors.push_back(anchorOf(scene.marks[index]));
        const QRectF& anchor = anchors.back();
        if ((std::max)(anchor.width(), anchor.height()) < kSmallMarkSizes * metrics.fontSize)
        {
            marks.add(anchor, index);
        }
    }

    const Padding padding = paddingOf(style.labelBacking);
    const QRectF inside =
        frame.adjusted(metrics.margin * 0.25, metrics.margin * 0.25, -metrics.margin * 0.25, -metrics.margin * 0.25);
    const double spacing = 0.12 * metrics.fontSize;
    for (std::size_t index = 0u; index < scene.marks.size(); ++index)
    {
        const AnnotationMark& mark = scene.marks[index];
        const auto [first, second] = labelLines(mark, content.labelText);
        if (first.isEmpty())
        {
            continue;
        }
        const ShapedLabel shaped = shapeLabel(shaper, first, second, metrics.fontSize, true, padding);
        const QRectF anchor = anchors[index];
        const bool point = mark.outline.size() < 3;

        struct Choice
        {
            QPointF corner;
            double cost = std::numeric_limits<double>::infinity();
            bool leader = false;
        };
        Choice best;
        const auto consider = [&](const QPointF corner, const double baseCost, const bool leader) {
            const QRectF box(corner, shaped.size);
            if (!inside.contains(box) || occupied.intersects(box.adjusted(-spacing, -spacing, spacing, spacing)))
            {
                return;
            }
            const double cost = baseCost + 6.0 * marks.count(box, index);
            if (cost < best.cost)
            {
                best = Choice{.corner = corner, .cost = cost, .leader = leader};
            }
        };

        const bool huge = mark.coversFrame || anchor.width() > kLargeObjectFraction * frame.width() ||
                          anchor.height() > kLargeObjectFraction * frame.height();
        const bool reachesOut =
            !frame.contains(anchor) && (std::max)(anchor.width(), anchor.height()) > 6.0 * metrics.fontSize;
        if (huge || reachesOut)
        {
            // An object larger than much of the frame, or a large one reaching past it, is labelled inside itself,
            // about its centre or the centre of the part of it the frame shows: beside it is outside the frame.
            const QRectF shown = anchor.intersected(frame);
            const QPointF centre = frame.contains(mark.center) ? mark.center : shown.center();
            const QSizeF size = shaped.size;
            const double base = huge ? 0.0 : 8.0;
            consider(QPointF(centre.x() - size.width() * 0.5, centre.y() - size.height() * 0.5), base, false);
            consider(QPointF(centre.x() - size.width() * 0.5, centre.y() - 1.6 * size.height()), base + 1.0, false);
            consider(QPointF(centre.x() - size.width() * 0.5, centre.y() + 0.6 * size.height()), base + 2.0, false);
        }
        const auto& places = point ? kPointPlaces : kOutlinePlaces;
        for (std::size_t order = 0u; order < places.size(); ++order)
        {
            consider(placeCorner(places[order], anchor, shaped.size, metrics.gap),
                     (huge ? 3.0 : 0.0) + static_cast<double>(order), false);
        }
        if (style.leaderLines && !(best.cost < 6.0))
        {
            // Farther places, joined to the object by a line: three rings about it, each wider ring tried at more
            // angles, since the same angle a ring farther out moves a label farther along.
            const double radius = point ? mark.radius : 0.5 * std::hypot(anchor.width(), anchor.height());
            constexpr std::array<std::pair<double, int>, 3> kRings{{{2.2, 8}, {3.8, 12}, {5.6, 16}}};
            for (std::size_t ring = 0u; ring < kRings.size(); ++ring)
            {
                const auto [heights, steps] = kRings[ring];
                const double distance = radius + metrics.gap + heights * shaped.size.height();
                for (int step = 0; step < steps; ++step)
                {
                    // Right first, then turning up the frame, as a reader's eye looks for it.
                    const double angle = -static_cast<double>(step) * 2.0 * std::numbers::pi / steps;
                    const QPointF centre =
                        mark.center + QPointF(std::cos(angle) * distance, std::sin(angle) * distance);
                    consider(QPointF(centre.x() - shaped.size.width() * 0.5, centre.y() - shaped.size.height() * 0.5),
                             14.0 + 4.0 * static_cast<double>(ring) + step * 0.25, true);
                }
            }
        }
        if (!std::isfinite(best.cost))
        {
            continue;
        }
        const QRectF box(best.corner, shaped.size);
        occupied.add(box);
        AnnotationLabel label{.role = AnnotationLabelRole::Object,
                              .source = index,
                              .primary = shaped.primary.translated(best.corner),
                              .secondary = shaped.secondary.translated(best.corner),
                              .box = box,
                              .leader = std::nullopt};
        if (best.leader)
        {
            const QPointF start = nearestOnBox(box, mark.center);
            label.leader = QLineF(start, leaderEnd(mark, anchor, start));
        }
        layout.labels.push_back(std::move(label));
        layout.labelled[index] = true;
    }
    return layout;
}

} // namespace an::ui
