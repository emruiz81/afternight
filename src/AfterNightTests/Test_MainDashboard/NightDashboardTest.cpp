// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Night on the dashboard's pictures: the application's logo in the identity bar and on the welcome, and the welcome's
// art, are shown in their Night forms, made off the GUI thread; what each paints holds no green and no blue.

#include "controls/DashboardIdentityBar.h"
#include "controls/DashboardOnboardingPanel.h"
#include "fixtures/ScopedNightMode.h"
#include "fixtures/ScopedStyleProfile.h"
#include "theme/AnDesignTokens.h"
#include "theme/NightImage.h"

#include <QCoreApplication>
#include <QDeadlineTimer>
#include <QEvent>
#include <QImage>
#include <QLabel>
#include <QThread>

#include <gtest/gtest.h>

#include <functional>

namespace
{

using an::ui::testing::ScopedNightMode;
using an::ui::testing::ScopedStyleProfile;

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

/// Tells a widget what a new application palette tells every widget that takes it, as Night switched does.
void paletteChanged(QWidget& widget)
{
    QEvent changed(QEvent::PaletteChange);
    QCoreApplication::sendEvent(&widget, &changed);
}

/// Whether a label shows a picture, in its Night form: lit, with no green or blue.
bool showsANightPicture(const QLabel& label)
{
    const QImage shown = label.pixmap().toImage();
    return !shown.isNull() && colouredPixels(shown) == 0;
}

} // namespace

// The identity bar's logo, blue by day, is red under Night.
TEST(NightDashboard, TheIdentityBarsLogoIsRed)
{
    const ScopedStyleProfile observatory(an::ui::StyleProfile::Observatory);
    an::ui::DashboardIdentityBar bar;
    auto* const logo = bar.findChild<QLabel*>(QStringLiteral("dashboardIdentityBarIcon"));
    ASSERT_NE(logo, nullptr);
    ASSERT_FALSE(logo->pixmap().isNull());
    EXPECT_GT(colouredPixels(logo->pixmap().toImage()), 0) << "the logo is not blue by day";

    const an::ui::testing::NightPasses before = an::ui::testing::nightPasses();
    const ScopedNightMode night(true);
    paletteChanged(*logo);
    ASSERT_TRUE(pumpUntil([logo]() { return showsANightPicture(*logo); })) << "the logo's Night form never came";
    EXPECT_EQ(an::ui::testing::nightPasses().onTheGuiThread, before.onTheGuiThread);
}

// The welcome's logo and the art behind it are red under Night.
TEST(NightDashboard, TheWelcomesLogoAndArtAreRed)
{
    const ScopedStyleProfile observatory(an::ui::StyleProfile::Observatory);
    const ScopedNightMode night(true);
    an::ui::DashboardOnboardingPanel panel(1.0);
    panel.resize(960, 640);
    auto* const logo = panel.findChild<QLabel*>(QStringLiteral("dashboardOnboardingLogo"));
    ASSERT_NE(logo, nullptr);
    ASSERT_TRUE(pumpUntil([logo]() { return showsANightPicture(*logo); })) << "the logo's Night form never came";

    (void)paintedAlone(panel);
    // The art is asked for at the first paint and painted at the next, once it is kept.
    ASSERT_TRUE(pumpUntil([&panel]() { return an::ui::testing::keptNightImages() >= 2u; }));
    EXPECT_EQ(colouredPixels(paintedAlone(panel)), 0) << "the welcome paints a colour under Night";
}
