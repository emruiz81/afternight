// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The Narrowband Palette Composer as a window: the values a run starts from, the palette grid it shows in place of a
/// preview view, the Color Spaces menu entry that launches it through its registry descriptor, and the grid's own
/// painting, which follows the theme's tokens.

#include "Test_ProcessFramework/ProcessWindowCaseSupport.h"

#include "controls/ImageViewControl.h"
#include "controls/TypedProcessControl.h"
#include "processes/NarrowbandPaletteComposerPreviewGridWidget.h"
#include "processes/NarrowbandPaletteComposerProcess.h"
#include "theme/AnDesignTokens.h"

#include <AfterNightEngine/Algorithms/Color/NarrowbandPalette.h>

#include <QApplication>
#include <QColor>
#include <QDialogButtonBox>
#include <QImage>
#include <QPointer>
#include <QPushButton>
#include <QWidget>

#include <gtest/gtest.h>

#include <string>
#include <type_traits>
#include <vector>

namespace
{

using namespace an::tests::processes;
using namespace an::tests::processes::framework;

/// Sets the theme for a case and puts back the one it replaced, passing or failing.
class ScopedTheme
{
public:
    ScopedTheme() : m_replaced(an::ui::themePreference()) {}
    ~ScopedTheme()
    {
        an::ui::setThemePreference(m_replaced);
        an::ui::refreshTokens();
    }
    ScopedTheme(const ScopedTheme&) = delete;
    ScopedTheme& operator=(const ScopedTheme&) = delete;

    void set(an::ui::ThemePreference preference) const
    {
        an::ui::setThemePreference(preference);
        an::ui::refreshTokens();
    }

private:
    an::ui::ThemePreference m_replaced;
};

/// What a grid with no palettes yet shows at a point, painted now.
QColor paintedAt(an::ui::processes::NarrowbandPaletteComposerPreviewGridWidget& grid, const QPoint& point)
{
    QImage image(grid.size(), QImage::Format_ARGB32);
    image.fill(Qt::transparent);
    grid.render(&image);
    return image.pixelColor(point);
}

// A run starts from the composer's own palette, SHO, every line at full intensity, each prepared plane stretched and
// a plane of another size resized to the reference; the window is a process of the typed framework, whose runs read
// these values as one snapshot.
TEST(NarrowbandPaletteComposerProcessContracts, ARunStartsFromTheShoPaletteAtFullIntensity)
{
    static_assert(std::is_base_of_v<an::ui::TypedProcessControl<NarrowbandPaletteComposerProcessParams>,
                                    NarrowbandPaletteComposerProcess>);
    const NarrowbandPaletteComposerProcessParams params;
    EXPECT_EQ(params.palette, an::NarrowbandPalette::Sho);
    EXPECT_TRUE(params.stretchInputs);
    EXPECT_TRUE(params.resizeMismatched);
    EXPECT_DOUBLE_EQ(params.intensities.hydrogenAlpha, 1.0);
    EXPECT_DOUBLE_EQ(params.intensities.oxygenIII, 1.0);
    EXPECT_DOUBLE_EQ(params.intensities.sulfurII, 1.0);
    EXPECT_TRUE(params.outputName.empty());
    EXPECT_EQ(params.ha.image, nullptr);
    EXPECT_EQ(params.oiii.image, nullptr);
}

// The window shows its palette grid where a process shows its preview view, and keeps it in sight once it is shown:
// it makes no preview view of its own and offers no Preview button, because the grid follows the inputs by itself;
// with no inputs chosen Apply is not offered.
TEST(NarrowbandPaletteComposerProcessContracts, TheWindowShowsItsPaletteGridInPlaceOfAPreviewView)
{
    QPointer<QWidget> window(new NarrowbandPaletteComposerProcess());
    const AtCaseEnd cleanup([&window]() {
        if (!window.isNull())
        {
            (void)pumpUntil([&window]() { return window.isNull() || windowIdle(*window); });
            delete window.data();
        }
        QCoreApplication::processEvents();
    });
    window->show();
    pumpQuietly();

    // The grid declares no meta-object of its own, so it is found as a widget by its name.
    auto* grid = dynamic_cast<an::ui::processes::NarrowbandPaletteComposerPreviewGridWidget*>(
        window->findChild<QWidget*>(QStringLiteral("paletteGrid")));
    ASSERT_NE(grid, nullptr);
    EXPECT_TRUE(grid->isVisible()) << "the grid went out of sight once the window was shown";
    EXPECT_FALSE(grid->hasPreviewImages());
    EXPECT_TRUE(window->findChildren<ImageViewControl*>().isEmpty()) << "the window made a preview view";
    EXPECT_EQ(window->findChild<QWidget*>(QStringLiteral("previewButton")), nullptr);
    auto* box = widgetNamed<QDialogButtonBox>(*window, "buttonBox");
    ASSERT_NE(box, nullptr);
    ASSERT_NE(box->button(QDialogButtonBox::Apply), nullptr);
    EXPECT_FALSE(box->button(QDialogButtonBox::Apply)->isEnabled()) << "Apply is offered with no inputs";
    window->close();
}

// The Process menu's Color Spaces menu offers the composer, and its entry asks the launcher for the composer's own
// registry descriptor.
TEST(NarrowbandPaletteComposerProcessContracts, TheColorSpacesMenuLaunchesTheComposerThroughItsDescriptor)
{
    ProcessMenus menus;
    QAction* entry = menus.entry(QStringLiteral("Color Spaces"), QStringLiteral("Narrowband Palette Composer"));
    ASSERT_NE(entry, nullptr);
    EXPECT_EQ(entry, menus.action("actionNarrowbandPaletteComposer"));
    ASSERT_TRUE(entry->isEnabled());
    entry->trigger();
    EXPECT_EQ(menus.launched, std::vector<std::string>{"color.actionNarrowbandPaletteComposer"});
}

// The grid paints its own background and its placeholder from the theme's tokens, read when it paints: the surface
// and the placeholder card are the dark theme's colours under it and the light theme's once the theme changes.
TEST(NarrowbandPaletteComposerProcessContracts, TheGridPaintsWithTheThemesTokensAndFollowsATheme)
{
    const ScopedTheme theme;
    an::ui::processes::NarrowbandPaletteComposerPreviewGridWidget grid;
    grid.setPaletteDescriptors(
        std::vector<an::NarrowbandPaletteDescriptor>(an::narrowbandPalettes().begin(), an::narrowbandPalettes().end()));
    grid.resize(400, 300);

    const auto& t = an::ui::tokens();
    const QPoint corner(1, 1);
    const QPoint card(t.spacing.xl + t.radius.md + 2, grid.height() / 2);

    theme.set(an::ui::ThemePreference::Dark);
    const QColor darkSurface = paintedAt(grid, corner);
    const QColor darkCard = paintedAt(grid, card);
    const QColor expectedDarkSurface = an::ui::tokens().colors.bgSurface;
    const QColor expectedDarkCard = an::ui::tokens().colors.bgCard;

    theme.set(an::ui::ThemePreference::Light);
    const QColor lightSurface = paintedAt(grid, corner);
    const QColor lightCard = paintedAt(grid, card);
    const QColor expectedLightSurface = an::ui::tokens().colors.bgSurface;
    const QColor expectedLightCard = an::ui::tokens().colors.bgCard;

    ASSERT_NE(expectedDarkSurface.rgb(), expectedLightSurface.rgb()) << "the two themes share their surface";
    EXPECT_NE(darkSurface.rgb(), lightSurface.rgb()) << "the grid's surface did not follow the theme";
    EXPECT_NE(darkCard.rgb(), lightCard.rgb()) << "the grid's placeholder did not follow the theme";
    if (expectedDarkSurface.alpha() == 255 && expectedLightSurface.alpha() == 255)
    {
        EXPECT_EQ(darkSurface.rgb(), expectedDarkSurface.rgb());
        EXPECT_EQ(lightSurface.rgb(), expectedLightSurface.rgb());
    }
    if (expectedDarkCard.alpha() == 255 && expectedLightCard.alpha() == 255)
    {
        EXPECT_EQ(darkCard.rgb(), expectedDarkCard.rgb());
        EXPECT_EQ(lightCard.rgb(), expectedLightCard.rgb());
    }
}

} // namespace
