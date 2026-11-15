// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// The icons the rail, the top bar and the strips draw that the product had not: each is in the application's
// resources, is loaded by the icon manager with the others and is tinted by it in the normal colour - none takes a
// status colour - and draws a mark of a weight that reads at 20 px.

#include "app/IconManager.h"

#include <QColor>
#include <QIcon>
#include <QImage>
#include <QPixmap>
#include <QSize>
#include <QString>
#include <QStringList>

#include <gtest/gtest.h>

#include <cstdlib>

namespace
{

/// The icons, by the names a part asks the icon provider for.
const QStringList& shellIcons()
{
    static const QStringList names{
        QStringLiteral("home"),         QStringLiteral("images"),     QStringLiteral("layers"),
        QStringLiteral("moon"),         QStringLiteral("window"),     QStringLiteral("chevron_right"),
        QStringLiteral("chevron_down"), QStringLiteral("tune"),       QStringLiteral("image"),
        QStringLiteral("information"),  QStringLiteral("more_horiz"), QStringLiteral("open_in_new"),
        QStringLiteral("panel_right"),
    };
    return names;
}

} // namespace

TEST(ShellIcons, EachIsLoadedAndTintedInTheNormalColour)
{
    const QColor normal(200, 120, 40);
    const QColor disabled(90, 90, 90);
    IconManager manager;
    manager.setColors(normal, disabled);
    manager.setSemanticColors(QColor(0, 0, 255), QColor(255, 255, 0), QColor(255, 0, 0), QColor(0, 255, 255),
                              QColor(0, 255, 0));
    manager.loadIcons(QStringLiteral(":/AfterNight/icons/SVG/"));
    for (const QString& name : shellIcons())
    {
        SCOPED_TRACE(name.toStdString());
        ASSERT_TRUE(manager.hasIcon(name));
        const QImage image =
            manager.getIcon(name).pixmap(QSize(20, 20)).toImage().convertToFormat(QImage::Format_ARGB32);
        int opaque = 0;
        int foreign = 0;
        for (int y = 0; y < image.height(); ++y)
        {
            for (int x = 0; x < image.width(); ++x)
            {
                const QColor pixel = image.pixelColor(x, y);
                if (pixel.alpha() < 128)
                {
                    continue;
                }
                ++opaque;
                if (std::abs(pixel.red() - normal.red()) > 8 || std::abs(pixel.green() - normal.green()) > 8 ||
                    std::abs(pixel.blue() - normal.blue()) > 8)
                {
                    ++foreign;
                }
            }
        }
        EXPECT_GE(opaque, 20) << "the icon draws too little to be read at 20 px";
        EXPECT_LE(opaque, 300) << "the icon fills its square instead of drawing a mark";
        EXPECT_EQ(foreign, 0) << "a pixel is not the normal colour";

        const QImage off = manager.getIcon(name)
                               .pixmap(QSize(20, 20), QIcon::Disabled)
                               .toImage()
                               .convertToFormat(QImage::Format_ARGB32);
        bool disabledSeen = false;
        for (int y = 0; y < off.height() && !disabledSeen; ++y)
        {
            for (int x = 0; x < off.width() && !disabledSeen; ++x)
            {
                const QColor pixel = off.pixelColor(x, y);
                disabledSeen = pixel.alpha() >= 128 && std::abs(pixel.red() - disabled.red()) <= 8 &&
                               std::abs(pixel.blue() - disabled.blue()) <= 8;
            }
        }
        EXPECT_TRUE(disabledSeen) << "the disabled form is not in the disabled colour";
    }
}
