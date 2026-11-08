// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Where an annotated picture's labels and decorations go, in the image's own pixels: every label shaped into the
/// outlines of its letters and set beside its object where it covers no other label, no decoration and no smaller
/// object, or further away on a leader line, or left out; the grid's labels just inside the frame where their lines
/// meet its edges; and the panel, the compass and the scale bar in their corners.
///
/// **A layout is drawn at any size.** Text is shaped once at a fixed size and scaled into place, and every length is
/// the style's fraction of the reference length, so the picture made at full size, a smaller one and the window's
/// preview draw one layout through different scales, and what the preview shows is where the picture puts it.
///
/// **Placement is greedy and deterministic.** Labels are placed in the scene's order - the most prominent object first
/// - each trying the places beside its object in a fixed preference before the farther ones a leader line reaches, and
/// taking the first place that covers nothing it may not; a label that finds none is left out, and the layout says so
/// (`AnnotationLayout::labelled`). The same scene and style always give the same layout.
///
/// Laying out reads fonts through Qt's text engine, which a worker may do while a `QGuiApplication` exists.
#pragma once

#include "AfterNightControlsExport.h"
#include "controls/annotation/AnnotationScene.h"
#include "controls/annotation/AnnotationSettings.h"

#include <QLineF>
#include <QPainterPath>
#include <QPointF>
#include <QRectF>
#include <QString>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

namespace an::ui
{

/// The lengths a style gives a scene, in the image's pixels.
struct AnnotationMetrics
{
    /// The reference length they are fractions of.
    double reference = 0.0;

    /// A label's font size: the height of its em.
    double fontSize = 0.0;

    /// A mark's line width.
    double lineWidth = 0.0;

    /// The grid's line width.
    double gridWidth = 0.0;

    /// The width of the halo stroked around a letter; it follows the text's size alone, never the lines' width.
    double haloWidth = 0.0;

    /// How far the decorations and the grid's labels keep from the frame's edge.
    double margin = 0.0;

    /// How far a label keeps from its object.
    double gap = 0.0;
};

/// How far a reticle's ticks reach from a star, as a multiple of its marker's radius; they start at the radius.
inline constexpr double kAnnotationReticleReach = 1.7;

/// The lengths a style gives a scene of a reference length.
///
/// @param referenceLength the scene's reference length.
/// @param style the style; its text size and line width are held to their ranges.
/// @return the lengths.
[[nodiscard]] AFTERNIGHTCONTROLS_API AnnotationMetrics annotationMetrics(double referenceLength,
                                                                         const AnnotationStyle& style) noexcept;

/// What a label names.
enum class AnnotationLabelRole : std::uint8_t
{
    /// A mark: `source` is its index in the scene's marks.
    Object,

    /// A grid line: `source` is its index in the scene's grid.
    Grid
};

/// One placed label.
struct AnnotationLabel
{
    AnnotationLabelRole role = AnnotationLabelRole::Object;

    /// The index of what it names, as its role says.
    std::size_t source = 0u;

    /// The outlines of its first line's letters: the designation, or the only line.
    QPainterPath primary;

    /// The outlines of its second line's: the name under the designation; empty for a label of one line.
    QPainterPath secondary;

    /// What the label covers: its letters with their halo, or its badge.
    QRectF box;

    /// The line from the label to its object, for a label set away from it.
    std::optional<QLineF> leader;
};

/// The panel's place and its text.
struct AnnotationPanelLayout
{
    /// The panel's box.
    QRectF box;

    /// The corners' radius.
    double radius = 0.0;

    /// The title's letters.
    QPainterPath title;

    /// The credit's.
    QPainterPath credit;

    /// The rule under the title, when facts follow it.
    std::optional<QLineF> rule;

    /// The facts' labels, one a line in a column.
    QPainterPath factLabels;

    /// Their values, in a column beside them.
    QPainterPath factValues;
};

/// The compass's place.
struct AnnotationCompassLayout
{
    /// Where its arrows start.
    QPointF origin;

    /// Where the north arrow ends.
    QPointF northTip;

    /// Where the east arrow ends.
    QPointF eastTip;

    /// The letters at the tips.
    QPainterPath northLetter;
    QPainterPath eastLetter;

    /// What the compass covers.
    QRectF box;
};

/// The scale bar's place.
struct AnnotationScaleBarLayout
{
    /// The bar.
    QLineF bar;

    /// How long the ticks at its ends are.
    double tickLength = 0.0;

    /// Its length's text, centred above it.
    QPainterPath label;

    /// What the bar and its label cover.
    QRectF box;
};

/// Where a scene's labels and decorations go.
struct AnnotationLayout
{
    /// The style's lengths.
    AnnotationMetrics metrics;

    /// The labels placed: the grid's first, then the marks' in the scene's order.
    std::vector<AnnotationLabel> labels;

    /// For each of the scene's marks, whether its label was placed.
    std::vector<bool> labelled;

    std::optional<AnnotationPanelLayout> panel;
    std::optional<AnnotationCompassLayout> compass;
    std::optional<AnnotationScaleBarLayout> scaleBar;
};

/// A mark's label text as the choices say: its first line and its second - the name under the designation - which is
/// empty for a label of one line. A view's overlay that labels a mark on one line draws the first.
///
/// @param mark the mark.
/// @param text the choice of text.
/// @return the two lines.
[[nodiscard]] AFTERNIGHTCONTROLS_API std::pair<QString, QString> annotationLabelLines(const AnnotationMark& mark,
                                                                                      AnnotationLabelText text);

/// Lays a scene out: the decorations in their corners, the grid's labels at the frame's edges and every mark's label
/// where it fits, as the choices and the style say.
///
/// @param scene the scene.
/// @param content the choices of what is shown: the labels' text and which decorations are drawn.
/// @param style the style: the sizes, the typeface, the backing, the leader lines and the corners.
/// @return the layout; an empty one for a scene of no frame.
[[nodiscard]] AFTERNIGHTCONTROLS_API AnnotationLayout layoutAnnotations(const AnnotationScene& scene,
                                                                        const AnnotationContent& content,
                                                                        const AnnotationStyle& style);

} // namespace an::ui
