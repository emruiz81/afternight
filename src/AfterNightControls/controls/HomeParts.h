// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// What Home's parts share: a line of text in one of Home's roles, drawn in the type scale and the colours in force;
/// and what a recent file's tile and card say about it - its picture, read from its thumbnail, and its facts, read from
/// the file - read off the GUI thread as the dashboard reads them.

#include "AfterNightControlsExport.h"

#include <QDateTime>
#include <QImage>
#include <QLabel>
#include <QSize>
#include <QString>
#include <verdigris/wobjectdefs.h>

#include <cstdint>
#include <functional>
#include <optional>

class QObject;
class QPaintEvent;
class QPainter;
class QPixmap;
class QRectF;

namespace an::ui
{

/// A line of Home's text, in one of its roles: its font from the type scale at a size of the metrics and its colour
/// from the tokens, both read when the style changes and where it paints, so a theme, Night or a density reaches it.
/// One line elided where it is narrower than its text, or wrapped over as many lines as its width needs.
///
/// `QLabel::text()` is what it shows; assistive technology reads it as static text.
class AFTERNIGHTCONTROLS_API HomeText : public QLabel
{
    W_OBJECT(HomeText)

public:
    /// What a text is on Home.
    enum class Role
    {
        Display, ///< The welcome's title.
        Heading, ///< A page's heading.
        Section, ///< A section's title.
        Strong, ///< A card's or a tile's name.
        Body, ///< A line under a title.
        Secondary, ///< A quieter line.
        Caption, ///< A hint, a small line.
        Caps, ///< A card's eyebrow, in capitals.
        CapsAccent, ///< An eyebrow in the accent.
        Mono ///< Facts: sizes, counts, dates, a version.
    };

    explicit HomeText(Role role, const QString& text = QString(), QWidget* parent = nullptr);

    void setRole(Role role);
    [[nodiscard]] Role role() const noexcept;

    /// Where a line too long is cut; `Qt::ElideNone` lets it take its whole width.
    void setElideMode(Qt::TextElideMode mode);

    /// The font a role is drawn in, now.
    [[nodiscard]] static QFont fontFor(Role role);

    /// The colour a role is drawn in, now.
    [[nodiscard]] static QColor colourFor(Role role);

    /// At least a line high, even with no text, so a part keeps its height while its text is read.
    [[nodiscard]] QSize sizeHint() const override;
    [[nodiscard]] QSize minimumSizeHint() const override;

protected:
    bool event(QEvent* event) override;
    void changeEvent(QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

private:
    /// Takes the role's font, when it is not the one it has.
    void applyRoleFont();

    Role m_role;
    Qt::TextElideMode m_elide = Qt::ElideRight;
};

/// What a recent file's tile says about it beyond its name and picture.
struct HomeRecentFacts
{
    /// An image's width and height, or a sequence's frame's; invalid when unknown.
    QSize size;

    /// How many frames a sequence holds; absent for an image or when the container does not say.
    std::optional<std::uint64_t> frameCount;

    /// When the file last changed; invalid when it is gone.
    QDateTime modified;
};

/// What reads a recent file's facts: run off the GUI thread, never waited for.
using HomeRecentProbe = std::function<HomeRecentFacts(const QString& path)>;

/// Reads a recent file's facts as the dashboard reads them: a sequence's container (`probeDashboardSequence`) or an
/// image's description (`probeDashboardFile`), in the session the context lends, and the file's last change. Background
/// work: a busy session leaves the size and the frames unknown.
///
/// Thread-safe.
[[nodiscard]] AFTERNIGHTCONTROLS_API HomeRecentFacts probeHomeRecentFile(const QString& path);

/// Whether a recent file is a sequence: what `isDashboardSequence` says.
[[nodiscard]] AFTERNIGHTCONTROLS_API bool isHomeSequence(const QString& path);

/// A recent file's type as its tag shows it: its format's name - FITS, XISF, TIFF, JPEG, PNG, SER - or its suffix in
/// capitals.
[[nodiscard]] AFTERNIGHTCONTROLS_API QString homeTypeText(const QString& path);

/// A recent file's facts as one line: its size, a sequence's frames and the date it last changed, each that is known,
/// separated by middle dots - "4432x3840 · 2026-11-06 19:16", "448x448 · 5144 frames · 2026-11-05 03:02".
[[nodiscard]] AFTERNIGHTCONTROLS_API QString homeFactsText(const HomeRecentFacts& facts);

/// Paints a recent file's picture in a place: the well's colour, and over it the picture - already in the form Night
/// shows, as its owner's `NightPicture` hands it - scaled to cover the place and cut to its rounded corners.
///
/// @param painter where.
/// @param place the place.
/// @param shown the picture as it is shown now; a null one leaves the well.
/// @param radius the place's corners.
AFTERNIGHTCONTROLS_API void paintHomePicture(QPainter& painter, const QRectF& place, const QPixmap& shown,
                                             qreal radius);

/// The thumbnail the application keeps of a recent file, through the context: its path, or an empty one.
[[nodiscard]] AFTERNIGHTCONTROLS_API QString homeThumbnailPath(const QString& path);

/// Reads a recent file off the GUI thread - its thumbnail decoded at most at `pictureSize`, and its facts by `probe` -
/// and hands both to `done` on `context`'s thread while `context` lives. Nothing is waited for: the read runs on the
/// thread pool, and the probe is background work.
///
/// @param context the object whose thread `done` runs on; `done` is dropped when it is gone.
/// @param path the recent file.
/// @param thumbnailPath its thumbnail; empty reads no picture.
/// @param pictureSize the most the picture is decoded at, its aspect kept.
/// @param probe what reads the facts; empty reads none.
/// @param done what is given the picture - null when there was none - and the facts.
AFTERNIGHTCONTROLS_API void readHomeRecentFile(QObject* context, const QString& path, const QString& thumbnailPath,
                                               const QSize& pictureSize, const HomeRecentProbe& probe,
                                               const std::function<void(const QImage&, const HomeRecentFacts&)>& done);

} // namespace an::ui
