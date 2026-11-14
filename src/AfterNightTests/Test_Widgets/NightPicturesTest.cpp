// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Night in the widgets that paint a picture: each shows its picture's Night form - a picture label, a recent file's
// thumbnail, the featured card, a preview - made off the GUI thread and painted once it is kept, so what the widget
// paints holds no green and no blue; the histogram draws its curves red and tells its channels apart by their strokes.

#include "fixtures/ScopedNightMode.h"
#include "fixtures/ScopedStyleProfile.h"
#include "theme/AnDesignTokens.h"
#include "theme/NightImage.h"
#include "widgets/FeaturedFileCard.h"
#include "widgets/HistogramDisplayData.h"
#include "widgets/HistogramWidget.h"
#include "widgets/ImagePreviewWidget.h"
#include "widgets/ImageThumbnailWidget.h"
#include "widgets/NightPictureLabel.h"

#include <QCoreApplication>
#include <QDeadlineTimer>
#include <QEvent>
#include <QImage>
#include <QLabel>
#include <QPixmap>
#include <QThread>

#include <gtest/gtest.h>

#include <functional>
#include <memory>

namespace
{

using an::ui::testing::ScopedNightMode;
using an::ui::testing::ScopedStyleProfile;

/// A picture in many colours: red across, green down, blue fixed.
QPixmap colourful(const int width = 48, const int height = 32)
{
    QImage picture(width, height, QImage::Format_RGB32);
    for (int y = 0; y < height; ++y)
    {
        for (int x = 0; x < width; ++x)
        {
            picture.setPixelColor(x, y, QColor(x * 255 / width, y * 255 / height, 180));
        }
    }
    return QPixmap::fromImage(picture);
}

/// What a widget paints itself, its children and its window's background left out, over black.
QImage paintedAlone(QWidget& widget)
{
    QImage painted(widget.size(), QImage::Format_ARGB32_Premultiplied);
    painted.fill(Qt::black);
    widget.render(&painted, QPoint(), QRegion(), QWidget::RenderFlags());
    return painted;
}

/// How many pixels of a picture hold green or blue.
int colouredPixels(const QImage& picture)
{
    const QImage pixels = picture.convertToFormat(QImage::Format_ARGB32);
    int coloured = 0;
    for (int y = 0; y < pixels.height(); ++y)
    {
        for (int x = 0; x < pixels.width(); ++x)
        {
            const QRgb pixel = pixels.pixel(x, y);
            coloured += qGreen(pixel) != 0 || qBlue(pixel) != 0 ? 1 : 0;
        }
    }
    return coloured;
}

/// How many pixels of a picture are not black.
int litPixels(const QImage& picture)
{
    const QImage pixels = picture.convertToFormat(QImage::Format_RGB32);
    int lit = 0;
    for (int y = 0; y < pixels.height(); ++y)
    {
        for (int x = 0; x < pixels.width(); ++x)
        {
            lit += (pixels.pixel(x, y) & 0x00FFFFFFu) != 0u ? 1 : 0;
        }
    }
    return lit;
}

/// Pumps the GUI thread's events until `done` holds or two seconds pass.
bool pumpUntil(const std::function<bool()>& done)
{
    const QDeadlineTimer deadline(2000);
    while (!done() && !deadline.hasExpired())
    {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        QThread::msleep(1);
    }
    return done();
}

/// Tells a widget what a new application palette tells every widget that takes it, as Night switched does.
void paletteChanged(QWidget& widget)
{
    QEvent changed(QEvent::PaletteChange);
    QCoreApplication::sendEvent(&widget, &changed);
}

} // namespace

// A picture label shows its picture's Night form under Night, made off the GUI thread, and its picture again by day.
TEST(NightPictures, ALabelShowsItsPictureInRed)
{
    const ScopedStyleProfile observatory(an::ui::StyleProfile::Observatory);
    an::ui::NightPictureLabel label;
    label.resize(48, 32);
    label.setPicture(colourful());
    EXPECT_GT(colouredPixels(label.pixmap().toImage()), 0) << "the picture is not shown in its colours by day";

    const an::ui::testing::NightPasses before = an::ui::testing::nightPasses();
    {
        const ScopedNightMode night(true);
        paletteChanged(label);
        ASSERT_TRUE(pumpUntil([&label]() { return !label.pixmap().isNull(); })) << "the Night form never came";
        EXPECT_EQ(colouredPixels(label.pixmap().toImage()), 0);
        EXPECT_GT(litPixels(label.pixmap().toImage()), 0);
        EXPECT_EQ(colouredPixels(paintedAlone(label)), 0);
        EXPECT_EQ(an::ui::testing::nightPasses().onTheGuiThread, before.onTheGuiThread)
            << "the GUI thread mapped the picture";
    }
    paletteChanged(label);
    EXPECT_GT(colouredPixels(label.pixmap().toImage()), 0) << "the colours did not come back by day";
}

// A recent file's thumbnail is shown in its Night form.
TEST(NightPictures, AThumbnailIsShownInRed)
{
    const ScopedStyleProfile observatory(an::ui::StyleProfile::Observatory);
    ImageThumbnailWidget thumbnail({}, {}, {}, true, nullptr);
    thumbnail.resize(180, 160);
    ASSERT_TRUE(thumbnail.loadImageFromData(QStringLiteral("M42.fits"), colourful(), QSize(4800, 3200)));
    auto* const picture = thumbnail.findChild<QLabel*>(QStringLiteral("dashboardThumbnailImage"));
    ASSERT_NE(picture, nullptr);
    EXPECT_GT(colouredPixels(picture->pixmap().toImage()), 0);

    const ScopedNightMode night(true);
    paletteChanged(thumbnail);
    ASSERT_TRUE(pumpUntil([picture]() { return !picture->pixmap().isNull(); })) << "the Night form never came";
    EXPECT_EQ(colouredPixels(picture->pixmap().toImage()), 0);
    EXPECT_GT(litPixels(picture->pixmap().toImage()), 0);
}

// The featured card paints its picture, and the softened copy over it, in their Night forms, over a surface the tokens
// make red.
TEST(NightPictures, TheFeaturedCardIsPaintedInRed)
{
    const ScopedStyleProfile observatory(an::ui::StyleProfile::Observatory);
    const ScopedNightMode night(true);
    an::ui::FeaturedFileCard card;
    card.resize(320, 180);
    card.setFeaturedFile(QStringLiteral("M42.fits"), QStringLiteral("M42"), QStringLiteral("Ha"), colourful(320, 180),
                         false);
    (void)paintedAlone(card);
    // Asked at the first paint, the forms are kept once made; the card paints them at the next.
    ASSERT_TRUE(pumpUntil([&card]() { return litPixels(paintedAlone(card)) > 320 * 180 / 2; }))
        << "the picture's Night form was never painted";
    EXPECT_EQ(colouredPixels(paintedAlone(card)), 0);
}

// A preview paints its picture's Night form over its background, which is the tokens' and so red.
TEST(NightPictures, APreviewIsPaintedInRed)
{
    const ScopedStyleProfile observatory(an::ui::StyleProfile::Observatory);
    ImagePreviewWidget preview(nullptr);
    preview.resize(96, 64);
    preview.setPixmap(colourful(), true);
    EXPECT_GT(colouredPixels(paintedAlone(preview)), 0);

    const ScopedNightMode night(true);
    paletteChanged(preview);
    (void)paintedAlone(preview);
    ASSERT_TRUE(pumpUntil([&preview]() { return litPixels(paintedAlone(preview)) > 48 * 32 / 2; }));
    EXPECT_EQ(colouredPixels(paintedAlone(preview)), 0);
}

// Under Night every curve is red, so the channels are told apart by their strokes: red solid, green dashed, blue
// dotted. By day every stroke is solid.
TEST(NightPictures, TheHistogramIsRedAndItsChannelsDifferByStroke)
{
    EXPECT_EQ(HistogramWidget::channelStroke(0u, true), Qt::SolidLine);
    EXPECT_EQ(HistogramWidget::channelStroke(1u, true), Qt::DashLine);
    EXPECT_EQ(HistogramWidget::channelStroke(2u, true), Qt::DotLine);
    for (std::size_t channel = 0u; channel < 3u; ++channel)
    {
        EXPECT_EQ(HistogramWidget::channelStroke(channel, false), Qt::SolidLine) << channel;
    }

    const ScopedStyleProfile observatory(an::ui::StyleProfile::Observatory);
    const ScopedNightMode night(true);
    HistogramWidget histogram(nullptr);
    histogram.resize(256, 120);
    auto counts = std::make_shared<an::ui::HistogramDisplayData>();
    for (std::uint64_t channel = 0u; channel < 3u; ++channel)
    {
        an::ui::HistogramDisplayChannel bins;
        for (std::uint64_t bin = 0u; bin < 256u; ++bin)
        {
            bins.bins.push_back((bin * (channel + 3u)) % 97u + 1u);
        }
        counts->channels.push_back(std::move(bins));
    }
    histogram.setHistogram(counts);
    QCoreApplication::processEvents();
    const QImage painted = paintedAlone(histogram);
    EXPECT_EQ(colouredPixels(painted), 0);
    EXPECT_GT(litPixels(painted), 0);
}
