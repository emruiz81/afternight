// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// The classic window under the developer switch - the Observatory profile, and the proxy style the application's start
// installs for it - with an image open: the Interface page's Night switch puts Night in force at once, and a grab of
// the window then holds no green and no blue. Switched off, the window has its colours again.

#include "ApplicationDriving.h"

#include "controls/ImageViewControl.h"
#include "controls/settings/InterfaceSettingsPage.h"
#include "fixtures/HeadlessQtFixture.h"
#include "fixtures/ScopedStyleProfile.h"
#include "theme/AnDesignTokens.h"
#include "theme/AnProxyStyle.h"
#include "widgets/ImageViewWidget.h"
#include "widgets/PresentationBridge.h"

#include <QAbstractButton>
#include <QApplication>
#include <QImage>
#include <QPalette>
#include <QString>
#include <QStringList>
#include <QWidget>

#include <gtest/gtest.h>

namespace
{

/// Where a picture holds green or blue: how many pixels, and the first few with the widget painted there.
struct Coloured
{
    int pixels = 0;
    QStringList where;
};

Coloured colouredPixels(QWidget& window, const QImage& grabbed)
{
    Coloured coloured;
    const QImage pixels = grabbed.convertToFormat(QImage::Format_ARGB32);
    for (int y = 0; y < pixels.height(); ++y)
    {
        for (int x = 0; x < pixels.width(); ++x)
        {
            const QRgb pixel = pixels.pixel(x, y);
            if (qGreen(pixel) == 0 && qBlue(pixel) == 0)
            {
                continue;
            }
            ++coloured.pixels;
            if (coloured.where.size() < 12)
            {
                const QWidget* const under = window.childAt(x, y);
                coloured.where.append(QStringLiteral("(%1, %2) #%3 in %4 '%5'")
                                          .arg(x)
                                          .arg(y)
                                          .arg(pixel & 0xFFFFFFu, 6, 16, QLatin1Char('0'))
                                          .arg(QString::fromLatin1(under != nullptr ? under->metaObject()->className()
                                                                                    : window.metaObject()->className()))
                                          .arg(under != nullptr ? under->objectName() : window.objectName()));
            }
        }
    }
    return coloured;
}

class NightWindow : public an::tests::app::RealWindowCase
{
};

} // namespace

TEST_F(NightWindow, UnderTheDeveloperSwitchTheNightSwitchLeavesNoGreenOrBlueInTheWindow)
{
    // What the case changes for the process, put back at its end: the style's palette and font, and the choices the
    // page's defaults put in force.
    const QPalette paletteBefore = QApplication::palette();
    const QFont fontBefore = QApplication::font();
    const an::ui::ThemeContrastProfile contrastBefore = an::ui::themeContrastProfile();
    const an::ui::ThemeAccentFamily accentBefore = an::ui::themeAccentFamily();
    const an::ui::ThemeSurfaceTintFamily tintBefore = an::ui::themeSurfaceTintFamily();
    const an::ui::ThemeTintStrength strengthBefore = an::ui::themeTintStrength();
    const an::ui::ThemeDensity densityBefore = an::ui::themeDensity();
    QWidget* const window = s_window;
    ASSERT_NE(window, nullptr);
    const bool wasShown = window->isVisible();
    {
        const an::ui::testing::ScopedStyleProfile observatory(an::ui::StyleProfile::Observatory);
        // What the application's style step installs at start under the switch: the proxy style, which fills the
        // palette from the tokens.
        ASSERT_NE(an::ui::installAnProxyStyle(), nullptr);
        window->resize(1280, 800);
        window->show();
        ImageViewControl* const view = makeView(QStringLiteral("night"), 0.5f);
        ASSERT_NE(view, nullptr);
        ASSERT_TRUE(an::tests::app::processEventsUntil([view]() {
            return view->isWorkIdle() && view->getViewWidget()->isVisible();
        })) << "the image's view never came on screen";

        // The page as Settings shows it, with its Night switch.
        InterfaceSettingsPage page(nullptr);
        page.loadSettings();
        auto* const night = page.findChild<QAbstractButton*>(QStringLiteral("anInterfaceSettingsNight"));
        ASSERT_NE(night, nullptr);
        ASSERT_FALSE(night->isChecked());
        night->click();
        ASSERT_TRUE(an::ui::isNightMode()) << "the switch did not act at once";

        // The views are presented again on their workers and the pictures made on the thread pool.
        ASSERT_TRUE(an::tests::app::processEventsUntil(
            [view]() { return view->isWorkIdle() && view->getViewWidget()->isPresentationIdle(); }));
        QApplication::processEvents();
        // The image is on screen, presented in Night's form: the grab below holds it.
        ImageViewWidget* const shown = view->getViewWidget();
        ASSERT_TRUE(shown->isVisible()) << "the image's view is not on screen";
        const std::shared_ptr<const an::ui::PresentedImage> presented = shown->presentedImage();
        ASSERT_NE(presented, nullptr);
        EXPECT_EQ(presented->pixel(0u, 0u)[0], 0u) << "the view does not present the image in Night's form";
        EXPECT_EQ(presented->pixel(0u, 0u)[1], 0u);
        EXPECT_GT(presented->pixel(0u, 0u)[2], 0u);
        const QImage grabbed = window->grab().toImage();
        ASSERT_FALSE(grabbed.isNull());
        const Coloured coloured = colouredPixels(*window, grabbed);
        EXPECT_EQ(coloured.pixels, 0) << "the window keeps a hue under Night at "
                                      << coloured.where.join(QStringLiteral("; ")).toStdString();

        night->click();
        EXPECT_FALSE(an::ui::nightModeSet());
        ASSERT_TRUE(an::tests::app::processEventsUntil([view]() { return view->isWorkIdle(); }));
        EXPECT_GT(colouredPixels(*window, window->grab().toImage()).pixels, 0)
            << "the window has no colour once Night is off";
    }
    if (!wasShown)
    {
        window->hide();
    }

    // The process's style and palette as they were: the proxy style goes with the style it replaced.
    QApplication* const application = qobject_cast<QApplication*>(QCoreApplication::instance());
    ASSERT_NE(application, nullptr);
    an::tests::fixtures::applyFusionStyle(*application);
    an::ui::registerInstalledAnProxyStyle(nullptr);
    an::ui::setThemeContrastProfile(contrastBefore);
    an::ui::setThemeAccentFamily(accentBefore);
    an::ui::setThemeSurfaceTintFamily(tintBefore);
    an::ui::setThemeTintStrength(strengthBefore);
    an::ui::setThemeDensity(densityBefore);
    QApplication::setPalette(paletteBefore);
    QApplication::setFont(fontBefore);
}
