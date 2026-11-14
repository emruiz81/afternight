// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// What the small parts of `components/` are made of beyond the design tokens: the sizes that are no metric of a
/// density - a switch's track and knob, a tag's corner, a chip's padding - and the few things each of them paints the
/// same way: a font at a size of the metrics, a state layer over a hovered or pressed part, the focus ring a keyboard
/// focus draws. A part takes its colours from `tokens()` where it paints and its sizes from `tokens().metrics` in its
/// sizeHint(); these constants are the rest, decided here so that no component writes a size of its own
/// (`THEME_SYSTEM.md`, "Small parts").

#include "AfterNightControlsExport.h"

#include <QColor>
#include <QFont>
#include <QRectF>

class QIcon;
class QPainter;
class QString;

namespace an::ui::parts
{

// A tag: a small square-cornered mark of a tone.
inline constexpr int kTagRadius = 4;
inline constexpr int kTagPadding = 7;
inline constexpr int kTagIcon = 12;
inline constexpr int kTagGap = 4;

// A key hint: the keys of a shortcut in the monospace, in a small outlined box.
inline constexpr int kKeyHintRadius = 4;
inline constexpr int kKeyHintPadding = 5;
/// How much of the text's colour a key hint on a coloured part draws its outline in.
inline constexpr qreal kKeyHintOnColourOutline = 0.38;

// A chip: a pill, its text the caption, with an optional icon before it and a count after it.
inline constexpr int kChipPadding = 10;
inline constexpr int kChipPaddingBeforeIcon = 8;
inline constexpr int kChipIcon = 13;
inline constexpr int kChipGap = 6;

// A switch: a track 32 x 18 whose knob grows from 10 px off to 14 px on, where it carries a check.
inline constexpr int kSwitchWidth = 32;
inline constexpr int kSwitchHeight = 18;
inline constexpr int kSwitchKnobOff = 10;
inline constexpr int kSwitchKnobOn = 14;
inline constexpr int kSwitchBorder = 2;
inline constexpr int kSwitchLabelGap = 8;

// A segmented control: segments that share one outline; the chosen one carries a check.
inline constexpr int kSegmentPadding = 12;
inline constexpr int kSegmentPaddingSmall = 7;
inline constexpr int kSegmentCheck = 12;
inline constexpr int kSegmentCheckGap = 6;

// A label beside or above its field.
inline constexpr int kFieldLabelGapAbove = 4;

// A breadcrumb: its parts with a chevron between them.
inline constexpr int kBreadcrumbChevron = 12;
inline constexpr int kBreadcrumbGap = 6;

// A transient message: a dark raised bar with an icon, a text, a detail and one action.
inline constexpr int kSnackbarIcon = 16;
inline constexpr int kSnackbarPaddingStart = 14;
inline constexpr int kSnackbarPaddingEnd = 6;
inline constexpr int kSnackbarGap = 10;
inline constexpr int kSnackbarActionPadding = 10;
/// How much of its colour a transient message's detail is drawn in.
inline constexpr qreal kSnackbarDetailOpacity = 0.72;

// A stage list's row: a status mark in a box, a title over one line, an optional progress.
inline constexpr int kStageMark = 14;
inline constexpr int kStageMarkBox = 24;
inline constexpr int kStageMarkBorder = 2;
inline constexpr int kStageGlyph = 10;
inline constexpr int kStageRowPaddingVertical = 7;
inline constexpr int kStageRowPaddingHorizontal = 10;
inline constexpr int kStageRowGap = 10;
inline constexpr int kStageRowSpacing = 4;
inline constexpr int kStageProgress = 3;
inline constexpr int kStageProgressGap = 4;
inline constexpr int kStageConnector = 2;

// What every part draws the same way.
inline constexpr int kFocusRingWidth = 2;
/// The stroke of a glyph a part draws itself: a check, a cross, a chevron.
inline constexpr qreal kGlyphStroke = 1.5;
inline constexpr qreal kHoverLayer = 0.08;
inline constexpr qreal kPressedLayer = 0.12;
inline constexpr qreal kDisabledOpacity = 0.38;

/// A font of the type scale at a size of the metrics, in logical pixels, set as points at Qt's 96 logical dots per
/// inch as the type scale is set, at a weight.
[[nodiscard]] AFTERNIGHTCONTROLS_API QFont sizedFont(const QFont& base, int logicalPixels,
                                                     QFont::Weight weight = QFont::Normal);

/// The exact width of a text in a font, rounded up: the room a part gives a text it may later elide, so that the
/// text it was measured for is never elided (`horizontalAdvance` rounds to the nearest pixel, which with fractional
/// advances is less than the text needs).
[[nodiscard]] AFTERNIGHTCONTROLS_API int textWidth(const QFont& font, const QString& text);

/// A colour at a share of its own opacity.
[[nodiscard]] AFTERNIGHTCONTROLS_API QColor atShare(const QColor& colour, qreal share);

/// The text colour for a background: `preferred` when it reaches `minimumRatio` against it, otherwise white or black,
/// whichever stands out more - or `preferred` when neither does better.
[[nodiscard]] AFTERNIGHTCONTROLS_API QColor contrastingTextColor(const QColor& background, const QColor& preferred,
                                                                 double minimumRatio);

/// Lays the state colour over a part's shape - 8 % while hovered, 12 % while pressed, nothing otherwise.
AFTERNIGHTCONTROLS_API void paintStateLayer(QPainter& painter, const QRectF& part, qreal radius, bool hovered,
                                            bool pressed);

/// The ring a part draws while it holds the keyboard's focus: 2 px of the focus colour at the part's edge, inside it,
/// as the proxy style draws a control's - a widget paints nothing outside its own rectangle.
AFTERNIGHTCONTROLS_API void paintFocusRing(QPainter& painter, const QRectF& part, qreal radius);

/// Whether a focus reason is the keyboard's - a part draws its ring only then.
[[nodiscard]] AFTERNIGHTCONTROLS_API bool isKeyboardFocusReason(Qt::FocusReason reason);

/// An icon drawn in one colour in a square - its shape kept, its colours replaced - at the painter's device pixel
/// ratio, as a part draws an icon in the colour of its text.
AFTERNIGHTCONTROLS_API void paintIcon(QPainter& painter, const QIcon& icon, const QRectF& square, const QColor& colour);

/// A check mark stroked in a square, as a chosen segment, a switch's knob and a finished stage draw it.
AFTERNIGHTCONTROLS_API void paintCheckMark(QPainter& painter, const QRectF& square, const QColor& colour, qreal width);

/// A cross stroked in a square, as a failed stage draws it.
AFTERNIGHTCONTROLS_API void paintCrossMark(QPainter& painter, const QRectF& square, const QColor& colour, qreal width);

/// An exclamation mark in a square, as a stage that ended with a warning draws it.
AFTERNIGHTCONTROLS_API void paintExclamationMark(QPainter& painter, const QRectF& square, const QColor& colour,
                                                 qreal width);

/// A chevron pointing right stroked in a square, as a breadcrumb draws it between its parts.
AFTERNIGHTCONTROLS_API void paintChevronRight(QPainter& painter, const QRectF& square, const QColor& colour,
                                              qreal width);

} // namespace an::ui::parts
