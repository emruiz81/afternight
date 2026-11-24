// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// The toolbar that floats over an image: a view's own actions and choices on a rounded scrim.

#include "AfterNightControlsExport.h"
#include "controls/ViewChrome.h"

#include <QList>
#include <QRect>
#include <QSize>
#include <QString>
#include <QStringList>
#include <QWidget>

#include <verdigris/wobjectdefs.h>

#include <memory>
#include <optional>

class QAbstractButton;
class QAction;
class QEvent;
class QPaintEvent;
class QResizeEvent;

namespace an::ui
{

/// Shows a view's actions and choices (`ViewChromeActions`) on a rounded scrim laid over an image. Its buttons are the
/// view's actions - their states, tool tips and checks are the view's, and a click triggers them - and its choices
/// mirror the view's own: the channel as a field that lists the view's channels, the display mode as two segments.
/// What the view does not offer, or hides, is not shown, and a separator stands only between two groups it shows.
///
/// **The full form** (`metrics.floatingToolbar` high, radius 10, padding 0 5, gap 2), from the left: with more than one
/// view, the shown view's name - a mark, then its full name elided in the middle at most 220 px wide - and a separator;
/// Fit To Window, Zoom Out, the zoom in the monospace (62 px), Zoom In, Zoom 100 %, Maximize; a separator; the channel
/// (104 px), the display mode, Autostretch Settings; a separator; Take snapshot, the statistics overlay, Show
/// Astrometry; a separator; More, which asks its host for the view's menu. **The small form**, on a floating image
/// window (32 px high, radius 8): Fit, Zoom Out, the zoom, Zoom In, a separator, the display mode as one toggle, More.
///
/// It draws the scrim, a 1 px edge under it and nothing outside the scrim's rounded shape. A host places it - over the
/// bottom of the area it floats over (`placeOver`) - and tells it what the view's zoom and name are; it reads nothing
/// of the view but its actions and choices.
class AFTERNIGHTCONTROLS_API AnFloatingToolbar : public QWidget
{
    W_OBJECT(AnFloatingToolbar)

public:
    /// Which toolbar it is.
    enum class Form
    {
        Full,
        Small
    };

    explicit AnFloatingToolbar(QWidget* parent = nullptr);
    ~AnFloatingToolbar() override;

    AnFloatingToolbar(const AnFloatingToolbar&) = delete;
    AnFloatingToolbar& operator=(const AnFloatingToolbar&) = delete;

    void setForm(Form form);
    [[nodiscard]] Form form() const noexcept;

    /// Shows a view's actions and choices; an empty set shows none.
    void setActions(const ViewChromeActions& actions);

    /// The actions and choices it shows.
    [[nodiscard]] const ViewChromeActions& actions() const noexcept;

    /// The full name of the view it shows, and how many views it may show: the name is shown with more than one.
    void setViewName(const QString& name);
    void setViewCount(int count);

    /// The view's zoom in percent, as the view states it (`ImageViewControl::getCurrentZoom`); nothing while it shows
    /// no image.
    void setZoom(std::optional<double> percent);

    /// The zoom as the status bar writes it, "25.36 %", or empty.
    [[nodiscard]] QString zoomText() const;

    /// The parts shown, from the left, by what they are: an action's text, "zoom", "channel", "display mode", "name",
    /// "more", and "|" for a separator.
    [[nodiscard]] QStringList shownParts() const;

    /// The button standing for an action, or null when it shows none for it.
    [[nodiscard]] QAbstractButton* buttonFor(const QAction* action) const;

    /// The parts that mirror a choice of the view: the channel's field, the display mode's segments - or, in the small
    /// form, its toggle - and their segments.
    [[nodiscard]] QAbstractButton* channelField() const;
    [[nodiscard]] QWidget* displayModeSegments() const;
    [[nodiscard]] QAbstractButton* displayModeSegment(int index) const;
    [[nodiscard]] QAbstractButton* displayModeToggle() const;

    /// The More button.
    [[nodiscard]] QAbstractButton* moreButton() const;

    /// The scrim's height in the form it is in, and its rounded shape's radius.
    [[nodiscard]] int barHeight() const;
    [[nodiscard]] int barRadius() const;

    /// Where it sits over an area, in that area's coordinates: centred - at the area's left when it is wider than the
    /// area - its bottom the form's lift above the area's.
    /// Where it would cover a rectangle of `keepClear` - a sequence's player strip at the bottom of its view - it is
    /// lifted until its bar is 8 px above the highest of those it would cover, and never above the area's top.
    [[nodiscard]] QRect placementIn(const QRect& area, const QList<QRect>& keepClear = {}) const;

    /// Moves and sizes it to `placementIn(area, keepClear)` and raises it over its siblings; placed again the same way
    /// when what it shows changes its width.
    void placeOver(const QRect& area, const QList<QRect>& keepClear = {});

    [[nodiscard]] QSize sizeHint() const override;
    [[nodiscard]] QSize minimumSizeHint() const override;

    /// Asked when More is clicked: the host opens the view's menu, at the button.
    void moreRequested() W_SIGNAL(moreRequested);

protected:
    bool event(QEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace an::ui
