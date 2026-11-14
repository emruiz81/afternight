// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// Night's form of a colour and of a picture: each colour its luma in red, `(Y, 0, 0)` with `Y = 0.299 R + 0.587 G +
/// 0.114 B` on the 8-bit components rounded to the nearest whole value, its alpha kept. The token pipeline maps every
/// colour so as its last step under Night; a picture is mapped once, off the GUI thread, and kept.
///
/// **A picture is never mapped on the GUI thread.** `nightImage` asked on the GUI thread answers the form it keeps for
/// the picture, or nothing while it is made on the thread pool, and tells the caller on the GUI thread when it is kept;
/// a widget paints its picture's place empty until then. Asked on another thread - a worker that decodes a thumbnail -
/// it makes the form there. The viewer maps its presented bytes on the worker that presents them
/// (`ViewPresentationSettings::night`). `theme/NightImageWidgets.h` lists every widget that paints a picture and how it
/// shows it under Night. `THEME_SYSTEM.md`, "Night", says why.

#include "AfterNightControlsExport.h"

#include <QColor>
#include <QImage>
#include <QPixmap>

#include <cstddef>
#include <cstdint>
#include <functional>

class QObject;

namespace an::ui
{

/// Night's form of a colour: its luma in red, the alpha kept. An invalid colour stays invalid.
[[nodiscard]] AFTERNIGHTCONTROLS_API QColor nightColor(const QColor& colour);

/// A colour as the screen shows it now: under Night its Night form, otherwise itself. For a painter that draws a colour
/// no token holds - a user's choice, a fixed colour of a plot - so that Night leaves no hue on screen.
[[nodiscard]] AFTERNIGHTCONTROLS_API QColor colorAsShown(const QColor& colour);

/// How the bytes of a pixel are ordered, for `toNightInPlace`.
enum class NightPixelOrder : std::uint8_t
{
    /// Blue, green, red and alpha: a 32-bit ARGB word on a little-endian host, as `QImage` holds one.
    Bgra,
    /// Red, green, blue and alpha.
    Rgba,
    /// Red, green and blue.
    Rgb
};

/// Maps display bytes to Night's form where they lie, on the calling thread. Premultiplied bytes stay premultiplied:
/// the luma of premultiplied components is the premultiplied luma.
///
/// @param bytes the first byte of the first row.
/// @param width the pixels in a row.
/// @param height the rows.
/// @param stride the bytes from one row to the next.
/// @param order how a pixel's bytes are ordered.
AFTERNIGHTCONTROLS_API void toNightInPlace(std::uint8_t* bytes, int width, int height, std::ptrdiff_t stride,
                                           NightPixelOrder order);

/// Night's form of a picture, made on the calling thread: the pixel work itself. A picture with alpha is mapped as
/// premultiplied ARGB, one without as RGB32, so a grey picture is shown in red too; the size and the device pixel ratio
/// are the picture's. A widget asks `nightImage` instead, which keeps the form and never maps on the GUI thread.
[[nodiscard]] AFTERNIGHTCONTROLS_API QImage nightForm(const QImage& picture);

/// The Night form of a picture, made once and kept per picture (by `QImage::cacheKey()`, so a copy of the same picture
/// shares its form; a changed picture is another).
///
/// On the GUI thread: the kept form, or a null image while the form is made on the thread pool; when it is kept,
/// `ready` is called on the GUI thread if `context` still lives, and the next call answers it. On another thread: the
/// form, made there when it is not kept.
///
/// @param picture the picture; a null one answers a null image.
/// @param context what `ready` belongs to; when it is destroyed first, `ready` is not called.
/// @param ready what to call when a form made for a call on the GUI thread is kept: a repaint, typically.
/// @return the kept form, or a null image while it is being made.
[[nodiscard]] AFTERNIGHTCONTROLS_API QImage nightImage(const QImage& picture, const QObject* context = nullptr,
                                                       std::function<void()> ready = {});

/// Lets go of every kept form. Night switched off calls it: the forms are kept only while they are shown.
AFTERNIGHTCONTROLS_API void clearNightImages();

/// A picture a widget paints, with its Night form. The widget hands it the picture when it has one and asks it, at each
/// paint, what to paint: the picture, or under Night its Night form, which `nightImage` makes off the GUI thread - a
/// null pixmap until it is kept, when the widget is told to paint again.
class AFTERNIGHTCONTROLS_API NightPicture
{
public:
    /// Holds a picture, and lets go of the Night form of the one before.
    void setPicture(const QPixmap& picture);
    void setPicture(const QImage& picture);

    /// Holds no picture.
    void clear();

    [[nodiscard]] bool isNull() const
    {
        return m_picture.isNull();
    }

    /// The picture as it was handed over, whatever Night is.
    [[nodiscard]] const QPixmap& picture() const
    {
        return m_picture;
    }

    /// What to paint now: the picture; under Night its Night form, or a null pixmap while that is made.
    ///
    /// @param owner the widget that paints it: `repaint` is called on the GUI thread when the form is kept, if it
    ///        still lives.
    /// @param repaint what paints the picture again.
    [[nodiscard]] QPixmap shown(const QObject* owner, const std::function<void()>& repaint) const;

private:
    QPixmap m_picture;
    /// The picture as an image, kept so it is the same picture, under the same key, at every paint.
    QImage m_image;
    /// The Night form as a pixmap, made once it is kept.
    mutable QPixmap m_night;
};

namespace testing
{

/// How many pictures and presentations were mapped to Night's form, on the GUI thread and elsewhere.
struct NightPasses
{
    std::uint64_t onTheGuiThread = 0;
    std::uint64_t elsewhere = 0;
};

/// The passes made since the process started. For the suites, which prove a widget's Night form was made off the GUI
/// thread; the application never reads it.
[[nodiscard]] AFTERNIGHTCONTROLS_API NightPasses nightPasses();

/// How many forms `nightImage` keeps. For the suites.
[[nodiscard]] AFTERNIGHTCONTROLS_API std::size_t keptNightImages();

} // namespace testing

} // namespace an::ui
