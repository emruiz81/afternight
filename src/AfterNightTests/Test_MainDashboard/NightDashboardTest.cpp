// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Night on Home's pictures: the application's logo on the welcome and a recent file's picture on its tile are shown in
// their Night forms, made off the GUI thread; what the welcome and the tile paint holds no green and no blue.

#include "controls/HomeRecentGrid.h"
#include "controls/HomeWelcome.h"
#include "fixtures/ScopedNightMode.h"
#include "theme/NightImage.h"

#include <QColor>
#include <QCoreApplication>
#include <QDeadlineTimer>
#include <QImage>
#include <QLabel>
#include <QSize>
#include <QThread>

#include <gtest/gtest.h>

#include <cstddef>
#include <functional>

namespace
{

using an::ui::testing::ScopedNightMode;

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

/// A picture of every hue, green and blue in every pixel.
QImage colourful(const int width, const int height)
{
    QImage picture(width, height, QImage::Format_RGB32);
    for (int y = 0; y < height; ++y)
    {
        for (int x = 0; x < width; ++x)
        {
            picture.setPixelColor(x, y, QColor(x * 255 / width, 40 + y * 215 / height, 180));
        }
    }
    return picture;
}

/// What a widget paints itself, its children and its window's background left out, over black.
QImage paintedAlone(QWidget& widget)
{
    QImage painted(widget.size(), QImage::Format_ARGB32_Premultiplied);
    painted.fill(Qt::black);
    widget.render(&painted, QPoint(), QRegion(), QWidget::RenderFlags());
    return painted;
}

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

/// Whether a label shows a picture, in its Night form: lit, with no green or blue.
bool showsANightPicture(const QLabel& label)
{
    const QImage shown = label.pixmap().toImage();
    return !shown.isNull() && colouredPixels(shown) == 0;
}

} // namespace

// Home's welcome shows the logo in its Night form, and the star field it paints behind is red under Night.
TEST(NightDashboard, TheWelcomesLogoAndStarFieldAreRed)
{
    const ScopedNightMode night(true);
    an::ui::HomeWelcome welcome;
    welcome.resize(1300, 900);
    auto* const logo = welcome.findChild<QLabel*>(QStringLiteral("homeWelcomeLogo"));
    ASSERT_NE(logo, nullptr);
    ASSERT_TRUE(pumpUntil([logo]() { return showsANightPicture(*logo); })) << "the logo's Night form never came";

    const QImage painted = paintedAlone(welcome);
    EXPECT_GT(litPixels(painted), 0) << "the welcome painted nothing";
    EXPECT_EQ(colouredPixels(painted), 0) << "the welcome paints a colour under Night";
}

// A recent file's tile paints its picture in its Night form, made off the GUI thread, and nothing it paints holds
// green or blue.
TEST(NightDashboard, ARecentFilesPictureIsRedOnItsTile)
{
    an::ui::HomeRecentTile tile(QStringLiteral("M42.fits"));
    const QSize size = tile.sizeHint();
    tile.resize(size);
    tile.setPicture(colourful(size.width(), size.height()));
    ASSERT_TRUE(tile.hasPicture());
    EXPECT_GT(colouredPixels(paintedAlone(tile)), 0) << "the picture is not painted in its colours by day";

    const an::ui::testing::NightPasses before = an::ui::testing::nightPasses();
    const std::size_t kept = an::ui::testing::keptNightImages();
    const ScopedNightMode night(true);
    // The Night form is asked for at the first paint and painted once it is kept.
    (void)paintedAlone(tile);
    ASSERT_TRUE(pumpUntil([kept]() { return an::ui::testing::keptNightImages() > kept; }))
        << "the picture's Night form never came";

    const QImage painted = paintedAlone(tile);
    EXPECT_GT(litPixels(painted), 0) << "the tile painted nothing";
    EXPECT_EQ(colouredPixels(painted), 0) << "the tile paints a colour under Night";
    EXPECT_EQ(an::ui::testing::nightPasses().onTheGuiThread, before.onTheGuiThread)
        << "the GUI thread mapped the picture";
}
