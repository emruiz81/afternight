// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// The dock decorations under the Observatory profile: the sizes of a tab strip and of its parts, where a tab's parts
/// lie, and the painters the decorations (`controls/KDDockWidgetsHelper.h`) and the dock style
/// (`theme/KddwProxyStyle.h`) hand over to under that profile. Every colour, size and font is read from the tokens in
/// force when a part is measured or painted. Under the Classic profile nothing here runs.

#include "AfterNightControlsExport.h"

#include <QFont>
#include <QIcon>
#include <QMargins>
#include <QRect>
#include <QString>

#include <cstdint>

class QPainter;

namespace an::ui
{

/// Whether the dock decorations draw the Observatory look: the profile in force is the Observatory one.
[[nodiscard]] AFTERNIGHTCONTROLS_API bool observatoryDockDecorations();

/// The dock library's separator under the Observatory profile: groups lie two pixels apart, on the dock area's ground.
inline constexpr int kObservatoryDockSeparator = 2;

/// The side of a tab's close button under the Observatory profile; the application's style sizes the button from it.
inline constexpr int kObservatoryDockTabClose = 16;

/// A tab strip's height: the strip of the density, or the smaller strip of a floating image window.
[[nodiscard]] AFTERNIGHTCONTROLS_API int observatoryDockStripHeight(bool floatingWindow);

/// The side of a button at the strip's right (the tab list, float or dock, close the group).
[[nodiscard]] AFTERNIGHTCONTROLS_API int observatoryDockButtonSide();

/// What a tab shows.
struct ObservatoryDockTab
{
    QString name; ///< The view's full name, without the mark of unsaved changes.
    bool modified = false; ///< The image has unsaved changes: a star follows the name.
    QIcon icon; ///< The view's icon, or none.
    bool adorned = false; ///< The tab carries an adornment after its name.
    QIcon adornmentIcon; ///< The adornment's icon, or none.
    bool closable = true; ///< The tab has a close button at its end.
};

/// How a tab is drawn.
struct ObservatoryDockTabState
{
    bool current = false; ///< The tab is its strip's current one.
    bool activeGroup = true; ///< The strip belongs to the group of the active view.
    bool hovered = false;
    bool enabled = true;
};

/// What a tab strip tells the dock style of its tabs: the style asks the strip it paints for the tab it paints.
class AFTERNIGHTCONTROLS_API ObservatoryDockTabSource
{
public:
    ObservatoryDockTabSource() = default;
    ObservatoryDockTabSource(const ObservatoryDockTabSource&) = delete;
    ObservatoryDockTabSource& operator=(const ObservatoryDockTabSource&) = delete;
    ObservatoryDockTabSource(ObservatoryDockTabSource&&) = delete;
    ObservatoryDockTabSource& operator=(ObservatoryDockTabSource&&) = delete;
    virtual ~ObservatoryDockTabSource();

    /// What the tab at an index shows.
    [[nodiscard]] virtual ObservatoryDockTab observatoryDockTab(int index) const = 0;
};

/// The margins round the buttons at a strip's right, in the corner the strip's tab widget gives them - as high as the
/// strip less its tabs' base - so that they stand centred in it, the strip's padding after the last.
[[nodiscard]] AFTERNIGHTCONTROLS_API QMargins observatoryDockCornerMargins(int cornerHeight);

/// Where a tab's parts lie inside the tab; a part the tab does not show is empty.
struct ObservatoryDockTabParts
{
    QRect icon;
    QRect name;
    QRect star;
    QRect adornment;
    QRect close;
};

/// The font of a tab's name: the small text, semibold.
[[nodiscard]] AFTERNIGHTCONTROLS_API QFont observatoryDockTabFont();

/// A tab's width with its whole name, or the least it is given: its name cut to twelve characters, or its whole name
/// when shorter.
[[nodiscard]] AFTERNIGHTCONTROLS_API int observatoryDockTabWidth(const ObservatoryDockTab& tab, bool minimum);

/// Where a tab's parts lie in its rectangle: the icon after the padding, then the name, which takes what its neighbours
/// leave, the star, the adornment and the close button.
[[nodiscard]] AFTERNIGHTCONTROLS_API ObservatoryDockTabParts observatoryDockTabParts(const QRect& rect,
                                                                                     const ObservatoryDockTab& tab);

/// The name as a tab shows it in a width: whole, or elided in the middle.
[[nodiscard]] AFTERNIGHTCONTROLS_API QString observatoryDockTabText(const QString& name, int width);

/// The strip's ground under its tabs and buttons, with the hairline under it.
AFTERNIGHTCONTROLS_API void paintObservatoryDockStrip(QPainter& painter, const QRect& strip);

/// A tab: the state colour when hovered, its parts, and the bar under the current one, ten pixels in from its ends.
AFTERNIGHTCONTROLS_API void paintObservatoryDockTab(QPainter& painter, const QRect& rect, const ObservatoryDockTab& tab,
                                                    const ObservatoryDockTabState& state);

/// What a button of the strip or of a title bar shows.
enum class ObservatoryDockGlyph
{
    Close,
    Float,
    Dock,
    TabList,
    Maximize,
    Minimize,
    Pin
};

/// A quiet button: the state colour when hovered or pressed, its glyph in the secondary text (the primary hovered), the
/// whole at the disabled opacity when disabled.
AFTERNIGHTCONTROLS_API void paintObservatoryDockButton(QPainter& painter, const QRect& rect, ObservatoryDockGlyph glyph,
                                                       bool hovered, bool pressed, bool enabled);

/// A tab's close button: a cross in the tertiary text (the primary hovered) over the state colour.
AFTERNIGHTCONTROLS_API void paintObservatoryDockTabClose(QPainter& painter, const QRect& rect, bool hovered,
                                                         bool pressed, bool enabled);

/// A floating window's title bar, as the strip of a floating image window: its ground, its hairline and its title.
AFTERNIGHTCONTROLS_API void paintObservatoryDockTitleBar(QPainter& painter, const QRect& rect, const QString& title,
                                                         const QRect& titleRect);

/// A floating window: its ground and a one-pixel frame, the outline, or the accent while the window is active.
AFTERNIGHTCONTROLS_API void paintObservatoryDockFrame(QPainter& painter, const QRect& rect, bool active);

/// A docked group's ground.
AFTERNIGHTCONTROLS_API void paintObservatoryDockPane(QPainter& painter, const QRect& rect);

/// A separator between groups: the dock area's ground, the accent's line while hovered or dragged.
AFTERNIGHTCONTROLS_API void paintObservatoryDockSeparator(QPainter& painter, const QRect& rect, bool active);

/// The part of a group a drop indicator stands for.
enum class ObservatoryDropSide
{
    Centre,
    Left,
    Top,
    Right,
    Bottom
};

/// A drop indicator's side, and how far apart the centres of the cross's targets are.
[[nodiscard]] AFTERNIGHTCONTROLS_API int observatoryDropTargetSide();
[[nodiscard]] AFTERNIGHTCONTROLS_API int observatoryDropTargetPitch();
/// How far an indicator at the dock area's edge keeps from that edge.
[[nodiscard]] AFTERNIGHTCONTROLS_API int observatoryDropTargetMargin();

/// The part of a target filled to say which half it stands for, the whole of its inside for the centre.
[[nodiscard]] AFTERNIGHTCONTROLS_API QRect observatoryDropTargetHalf(const QRect& target, ObservatoryDropSide side);

/// A drop indicator: a raised square of the third surface with an outline, the half it stands for filled with the
/// accent's line colour, or with the accent itself while the cursor is over it.
AFTERNIGHTCONTROLS_API void paintObservatoryDropTarget(QPainter& painter, const QRect& rect, ObservatoryDropSide side,
                                                       bool hot);

/// The band over the place a dragged view would land: the accent's soft share with a two-pixel accent border.
AFTERNIGHTCONTROLS_API void paintObservatoryDropBand(QPainter& painter, const QRect& rect);

/// The band's border.
inline constexpr int kObservatoryDropBandBorder = 2;

namespace testing
{
/// How many dock parts the Observatory profile's painters have drawn since the process started. For the suites, which
/// prove the Classic profile draws the dock decorations through their own code alone; the application never reads it.
[[nodiscard]] AFTERNIGHTCONTROLS_API std::uint64_t observatoryDockPaints();
} // namespace testing

} // namespace an::ui
