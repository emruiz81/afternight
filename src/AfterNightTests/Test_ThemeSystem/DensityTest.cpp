// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Density: the sizes of the interface as tokens. The Observatory profile has two densities, Compact and Comfortable,
// each a column of metrics - the heights of a control, a row, a bar, the font sizes, a panel's padding - and a type
// scale sized by them; the proxy style answers the sizes of the standard widgets from them, and a density changed while
// a window is shown sizes the window again. The Classic profile keeps AfterNight 1.1's sizes and ignores the density.

#include "app/ThemeManager.h"
#include "fixtures/ScopedStyleProfile.h"
#include "theme/AnDesignTokens.h"
#include "theme/AnProxyStyle.h"

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QCoreApplication>
#include <QDeadlineTimer>
#include <QFont>
#include <QHeaderView>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QPalette>
#include <QPushButton>
#include <QScrollBar>
#include <QSpinBox>
#include <QString>
#include <QStringList>
#include <QStyle>
#include <QStyleFactory>
#include <QTabBar>
#include <QTableWidget>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidget>

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <functional>
#include <string>
#include <utility>

namespace
{

using an::ui::DesignTokens;
using an::ui::MetricTokens;
using an::ui::StyleProfile;
using an::ui::ThemeChoices;
using an::ui::ThemeContrastProfile;
using an::ui::ThemeDensity;
using an::ui::ThemeVariant;
using an::ui::testing::ScopedStyleProfile;

constexpr std::array<ThemeDensity, 2u> kDensities{ThemeDensity::Compact, ThemeDensity::Comfortable};
constexpr std::array<ThemeContrastProfile, 4u> kContrasts{ThemeContrastProfile::High, ThemeContrastProfile::Balanced,
                                                          ThemeContrastProfile::Soft, ThemeContrastProfile::Oled};
constexpr std::array<an::ui::ThemeAccentFamily, 5u> kAccents{
    an::ui::ThemeAccentFamily::Blue, an::ui::ThemeAccentFamily::Teal, an::ui::ThemeAccentFamily::Amber,
    an::ui::ThemeAccentFamily::Amethyst, an::ui::ThemeAccentFamily::Rose};
constexpr std::array<an::ui::ThemeSurfaceTintFamily, 6u> kTints{
    an::ui::ThemeSurfaceTintFamily::Neutral, an::ui::ThemeSurfaceTintFamily::Ocean,
    an::ui::ThemeSurfaceTintFamily::Mint,    an::ui::ThemeSurfaceTintFamily::Amethyst,
    an::ui::ThemeSurfaceTintFamily::Rose,    an::ui::ThemeSurfaceTintFamily::Amber};

const char* densityName(const ThemeDensity density)
{
    return density == ThemeDensity::Comfortable ? "comfortable" : "compact";
}

DesignTokens resolved(const StyleProfile profile, const ThemeDensity density,
                      const ThemeVariant theme = ThemeVariant::Dark)
{
    return an::ui::resolveDesignTokens(ThemeChoices{.theme = theme, .profile = profile, .density = density});
}

/// The density's column of the table, as the design states it.
MetricTokens designColumn(const ThemeDensity density)
{
    if (density == ThemeDensity::Comfortable)
    {
        return MetricTokens{.fontBody = 14,
                            .fontSmall = 13,
                            .fontCaption = 12,
                            .control = 36,
                            .controlSmall = 32,
                            .controlXSmall = 26,
                            .controlLarge = 44,
                            .rowTable = 36,
                            .rowDense = 32,
                            .rowKeyValue = 28,
                            .rowSettings = 42,
                            .rowPalette = 34,
                            .barTop = 48,
                            .strip = 40,
                            .statusLine = 28,
                            .rail = 44,
                            .pad = 14,
                            .gap = 10,
                            .sectionHeader = 36,
                            .chip = 26,
                            .tag = 22,
                            .floatingToolbar = 44,
                            .scrollBar = 8};
    }
    return MetricTokens{.fontBody = 13,
                        .fontSmall = 12,
                        .fontCaption = 11,
                        .control = 32,
                        .controlSmall = 28,
                        .controlXSmall = 24,
                        .controlLarge = 40,
                        .rowTable = 30,
                        .rowDense = 28,
                        .rowKeyValue = 24,
                        .rowSettings = 38,
                        .rowPalette = 30,
                        .barTop = 44,
                        .strip = 34,
                        .statusLine = 26,
                        .rail = 44,
                        .pad = 12,
                        .gap = 8,
                        .sectionHeader = 32,
                        .chip = 24,
                        .tag = 20,
                        .floatingToolbar = 40,
                        .scrollBar = 8};
}

/// Each metric by its name, for a failure that says which one moved.
std::array<std::pair<const char*, int>, 23u> named(const MetricTokens& m)
{
    return {{
        {"fontBody", m.fontBody},
        {"fontSmall", m.fontSmall},
        {"fontCaption", m.fontCaption},
        {"control", m.control},
        {"controlSmall", m.controlSmall},
        {"controlXSmall", m.controlXSmall},
        {"controlLarge", m.controlLarge},
        {"rowTable", m.rowTable},
        {"rowDense", m.rowDense},
        {"rowKeyValue", m.rowKeyValue},
        {"rowSettings", m.rowSettings},
        {"rowPalette", m.rowPalette},
        {"barTop", m.barTop},
        {"strip", m.strip},
        {"statusLine", m.statusLine},
        {"rail", m.rail},
        {"pad", m.pad},
        {"gap", m.gap},
        {"sectionHeader", m.sectionHeader},
        {"chip", m.chip},
        {"tag", m.tag},
        {"floatingToolbar", m.floatingToolbar},
        {"scrollBar", m.scrollBar},
    }};
}

void expectMetrics(const MetricTokens& actual, const MetricTokens& expected, const std::string& where)
{
    const auto actualNamed = named(actual);
    const auto expectedNamed = named(expected);
    for (std::size_t i = 0; i < actualNamed.size(); ++i)
    {
        EXPECT_EQ(actualNamed[i].second, expectedNamed[i].second) << where << ": " << actualNamed[i].first;
    }
    EXPECT_TRUE(actual == expected) << where << ": a metric the table does not name differs";
}

/// A font's size in logical pixels: the pixel size it states, or its point size at the 96 logical dots per inch Qt
/// presents to an application.
int logicalPixels(const QFont& font)
{
    if (font.pixelSize() > 0)
    {
        return font.pixelSize();
    }
    return static_cast<int>(std::lround(font.pointSizeF() * 96.0 / 72.0));
}

/// Pumps the event loop until `holds` answers true or a second has passed; answers what `holds` last did.
bool eventually(const std::function<bool()>& holds)
{
    const QDeadlineTimer deadline(1000);
    while (!holds())
    {
        if (deadline.hasExpired())
        {
            return false;
        }
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    }
    return true;
}

/// The proxy style installed for the application under the Observatory profile at a density; the style, palette, font
/// and density the process had are put back, and the profile with them, when the scope ends.
class ObservatoryStyleInstalled final
{
public:
    explicit ObservatoryStyleInstalled(const ThemeDensity density)
        : m_density(an::ui::themeDensity()), m_palette(QApplication::palette()), m_font(QApplication::font()),
          m_styleName(QApplication::style()->name())
    {
        an::ui::setThemeDensity(density);
        m_style = an::ui::installAnProxyStyle();
    }

    ~ObservatoryStyleInstalled()
    {
        an::ui::setThemeDensity(m_density);
        // The application's style goes with the one that replaces it.
        QApplication::setStyle(QStyleFactory::create(m_styleName));
        an::ui::registerInstalledAnProxyStyle(nullptr);
        QApplication::setPalette(m_palette);
        QApplication::setFont(m_font);
    }

    ObservatoryStyleInstalled(const ObservatoryStyleInstalled&) = delete;
    ObservatoryStyleInstalled& operator=(const ObservatoryStyleInstalled&) = delete;

    [[nodiscard]] an::ui::AnProxyStyle* style() const
    {
        return m_style;
    }

private:
    ScopedStyleProfile m_profile{StyleProfile::Observatory};
    ThemeDensity m_density;
    QPalette m_palette;
    QFont m_font;
    QString m_styleName;
    an::ui::AnProxyStyle* m_style = nullptr;
};

/// The density in force put back when the scope ends.
class DensityRestored final
{
public:
    DensityRestored() : m_density(an::ui::themeDensity()) {}
    ~DensityRestored()
    {
        an::ui::setThemeDensity(m_density);
    }
    DensityRestored(const DensityRestored&) = delete;
    DensityRestored& operator=(const DensityRestored&) = delete;

private:
    ThemeDensity m_density;
};

} // namespace

// Under the Observatory profile each density resolves to its column of the table, in every theme and contrast profile.
TEST(Density, TableMatches)
{
    for (const ThemeDensity density : kDensities)
    {
        for (const ThemeVariant theme : {ThemeVariant::Dark, ThemeVariant::Light})
        {
            for (const ThemeContrastProfile contrast : kContrasts)
            {
                const DesignTokens tokens = an::ui::resolveDesignTokens(ThemeChoices{
                    .theme = theme, .contrast = contrast, .profile = StyleProfile::Observatory, .density = density});
                expectMetrics(tokens.metrics, designColumn(density),
                              std::string(densityName(density)) + (theme == ThemeVariant::Dark ? ", dark" : ", light") +
                                  ", contrast " + std::to_string(static_cast<int>(contrast)));
            }
        }
    }
}

// The Classic profile keeps 1.1's sizes: its control floor and its scroll bar, the sizes of its type scale, and the
// Compact column for what 1.1 has no size for. The density moves none of its tokens, in any combination.
TEST(Density, ClassicIgnoresTheDensity)
{
    const DesignTokens classic = resolved(StyleProfile::Classic, ThemeDensity::Compact);
    MetricTokens expected = designColumn(ThemeDensity::Compact);
    expected.control = 24;
    expected.scrollBar = 12;
    expected.fontBody = logicalPixels(classic.typography.body);
    expected.fontSmall = logicalPixels(classic.typography.caption);
    expected.fontCaption = logicalPixels(classic.typography.caption);
    expectMetrics(classic.metrics, expected, "classic");

    for (const ThemeVariant theme : {ThemeVariant::Dark, ThemeVariant::Light})
    {
        for (const ThemeContrastProfile contrast : kContrasts)
        {
            for (const an::ui::ThemeAccentFamily accent : kAccents)
            {
                for (const an::ui::ThemeSurfaceTintFamily tint : kTints)
                {
                    ThemeChoices choices{.theme = theme, .contrast = contrast, .accent = accent, .tint = tint};
                    const DesignTokens compact = an::ui::resolveDesignTokens(choices);
                    choices.density = ThemeDensity::Comfortable;
                    EXPECT_TRUE(an::ui::resolveDesignTokens(choices) == compact)
                        << "theme " << static_cast<int>(theme) << ", contrast " << static_cast<int>(contrast)
                        << ", accent " << static_cast<int>(accent) << ", tint " << static_cast<int>(tint);
                }
            }
        }
    }

    const DensityRestored restored;
    const ScopedStyleProfile classicInForce(StyleProfile::Classic);
    an::ui::setThemeDensity(ThemeDensity::Compact);
    const DesignTokens before = an::ui::tokens();
    an::ui::setThemeDensity(ThemeDensity::Comfortable);
    EXPECT_TRUE(an::ui::tokens() == before);
}

// The density in force is one of the choices the tokens in force are resolved from.
TEST(Density, TheTokensInForceFollowTheDensity)
{
    const DensityRestored restored;
    const ScopedStyleProfile observatory(StyleProfile::Observatory);
    for (const ThemeDensity density : kDensities)
    {
        an::ui::setThemeDensity(density);
        EXPECT_EQ(an::ui::themeDensity(), density);
        expectMetrics(an::ui::resolvedDarkTokens().metrics, designColumn(density), densityName(density));
        expectMetrics(an::ui::resolvedLightTokens().metrics, designColumn(density), densityName(density));
    }
}

// Under the Observatory profile the proxy style sizes the standard widgets from the metrics of the density in force: a
// control (a button, a tool button, a combo box, a spin box, a line edit), a tab, a table's rows and its header, a
// list's rows, a scroll bar and a menu's items.
TEST(Density, ProxyStyleMetricsFollow)
{
    for (const ThemeDensity density : kDensities)
    {
        SCOPED_TRACE(densityName(density));
        const ObservatoryStyleInstalled installed(density);
        const MetricTokens m = an::ui::tokens().metrics;
        ASSERT_EQ(m.control, designColumn(density).control);

        const QPushButton button(QStringLiteral("Apply"));
        EXPECT_EQ(button.sizeHint().height(), m.control);
        EXPECT_GE(button.sizeHint().width(), m.control);

        const QToolButton iconButton;
        EXPECT_EQ(iconButton.sizeHint(), QSize(m.control, m.control)) << "an icon button is a control square";

        QComboBox combo;
        combo.addItem(QStringLiteral("Linear"));
        EXPECT_EQ(combo.sizeHint().height(), m.control);

        const QSpinBox spin;
        EXPECT_EQ(spin.sizeHint().height(), m.control);

        const QLineEdit field;
        EXPECT_EQ(field.sizeHint().height(), m.control);

        QTabBar tabs;
        tabs.addTab(QStringLiteral("M31"));
        EXPECT_EQ(tabs.sizeHint().height(), m.strip);

        QTabBar panelTabs;
        panelTabs.setProperty("anTabCompact", true);
        panelTabs.addTab(QStringLiteral("Stars"));
        EXPECT_EQ(panelTabs.sizeHint().height(), m.controlSmall) << "a compact tab bar is a small control high";

        const QTableWidget table(2, 2);
        EXPECT_EQ(table.verticalHeader()->defaultSectionSize(), m.rowTable);
        EXPECT_EQ(table.horizontalHeader()->sizeHint().height(), m.rowTable);

        QListWidget list;
        list.addItem(QStringLiteral("Light frames"));
        EXPECT_EQ(list.sizeHintForRow(0), m.rowTable);

        const QScrollBar vertical(Qt::Vertical);
        EXPECT_EQ(vertical.sizeHint().width(), m.scrollBar);
        const QScrollBar horizontal(Qt::Horizontal);
        EXPECT_EQ(horizontal.sizeHint().height(), m.scrollBar);

        QMenu menu;
        QAction* const open = menu.addAction(QStringLiteral("Open"));
        EXPECT_EQ(menu.actionGeometry(open).height(), m.rowDense);
    }
}

// A density changed while a window is shown sizes it again: its controls take the new height, the window grows to hold
// them, and the application's font is the new density's.
TEST(Density, ChangeResizesAShownWindow)
{
    const ObservatoryStyleInstalled installed(ThemeDensity::Compact);
    QWidget window;
    auto* const layout = new QVBoxLayout(&window);
    auto* const button = new QPushButton(QStringLiteral("Apply"));
    auto* const field = new QLineEdit;
    // A widget given a font of its own: a change of the application's font does not reach it.
    auto* const ownFont = new QPushButton(QStringLiteral("Keep"));
    ownFont->setFont(QApplication::font());
    layout->addWidget(button);
    layout->addWidget(field);
    layout->addWidget(ownFont);
    window.show();
    const auto heights = [&](const int control) {
        return button->height() == control && field->height() == control && ownFont->height() == control;
    };
    ASSERT_TRUE(eventually([&] { return heights(32); }))
        << "button " << button->height() << ", field " << field->height() << ", own font " << ownFont->height();
    const int compactHeight = window.height();

    an::ui::setThemeDensity(ThemeDensity::Comfortable);
    installed.style()->refreshThemeTokens();

    EXPECT_TRUE(eventually([&] { return heights(36); }))
        << "button " << button->height() << ", field " << field->height() << ", own font " << ownFont->height();
    EXPECT_GT(window.height(), compactHeight);
    EXPECT_EQ(QApplication::font(), an::ui::tokens().typography.body);
    EXPECT_EQ(logicalPixels(QApplication::font()), 14);
}

// The type scale under the Observatory profile: the body, the caption, the third heading and the monospace at the
// density's sizes, the first two headings at theirs in both, every heading semibold, tabular figures throughout. The
// Classic profile's type scale is 1.1's in both densities.
TEST(Typography, SizesByDensity)
{
    for (const ThemeDensity density : kDensities)
    {
        SCOPED_TRACE(densityName(density));
        const DesignTokens tokens = resolved(StyleProfile::Observatory, density);
        const an::ui::TypographyTokens& t = tokens.typography;
        const int body = density == ThemeDensity::Comfortable ? 14 : 13;
        const int caption = density == ThemeDensity::Comfortable ? 12 : 11;

        EXPECT_EQ(logicalPixels(t.body), body);
        EXPECT_EQ(logicalPixels(t.caption), caption);
        EXPECT_EQ(logicalPixels(t.heading3), body);
        EXPECT_EQ(logicalPixels(t.monospace), body);
        EXPECT_EQ(logicalPixels(t.heading1), 24);
        EXPECT_EQ(logicalPixels(t.heading2), 17);
        EXPECT_EQ(t.body.weight(), QFont::Normal);
        EXPECT_EQ(t.caption.weight(), QFont::Normal);
        EXPECT_EQ(t.heading1.weight(), QFont::DemiBold);
        EXPECT_EQ(t.heading2.weight(), QFont::DemiBold);
        EXPECT_EQ(t.heading3.weight(), QFont::DemiBold);
        // The interface font is the platform's, the one the Classic profile's body is.
        EXPECT_EQ(t.body.family(), resolved(StyleProfile::Classic, density).typography.body.family());
        EXPECT_EQ(t.monospace.families().value(0), QStringLiteral("Cascadia Mono"));
        EXPECT_EQ(t.monospace.families().value(1), QStringLiteral("Cascadia Code"));
        EXPECT_EQ(t.monospace.families().value(2), QStringLiteral("Consolas"));
        EXPECT_EQ(t.monospace.styleHint(), QFont::Monospace);
        for (const QFont* font : {&t.body, &t.caption, &t.heading1, &t.heading2, &t.heading3, &t.monospace})
        {
            EXPECT_EQ(font->featureValue(QFont::Tag("tnum")), 1u) << "figures are tabular";
        }
        // Set as points, so that a font made from one by its point size keeps working.
        EXPECT_GT(t.body.pointSizeF(), 0.0);
        EXPECT_GT(t.body.pointSize(), 0);
    }

    EXPECT_TRUE(resolved(StyleProfile::Classic, ThemeDensity::Compact).typography ==
                resolved(StyleProfile::Classic, ThemeDensity::Comfortable).typography);
}

// The Observatory profile's radii: small parts 6, controls 8, cards and popups 12, sheets 16. Classic keeps 1.1's.
TEST(ObservatoryRadius, IsTheShapeTable)
{
    for (const ThemeDensity density : kDensities)
    {
        for (const ThemeVariant theme : {ThemeVariant::Dark, ThemeVariant::Light})
        {
            const an::ui::RadiusTokens observatory = resolved(StyleProfile::Observatory, density, theme).radius;
            EXPECT_EQ(observatory.none, 0);
            EXPECT_EQ(observatory.sm, 6);
            EXPECT_EQ(observatory.md, 8);
            EXPECT_EQ(observatory.lg, 12);
            EXPECT_EQ(observatory.xl, 16);
            EXPECT_TRUE(resolved(StyleProfile::Classic, density, theme).radius == an::ui::RadiusTokens{});
        }
    }
}

// The setting's values name the two densities, and anything else is the default, Compact.
TEST(DensitySetting, EachDensityIsStoredByItsName)
{
    for (const ThemeDensity density : kDensities)
    {
        EXPECT_EQ(an::ui::resolveThemeDensity(an::ui::themeDensityId(density)), density);
    }
    EXPECT_EQ(an::ui::themeDensityId(ThemeDensity::Compact), QStringLiteral("compact"));
    EXPECT_EQ(an::ui::themeDensityId(ThemeDensity::Comfortable), QStringLiteral("comfortable"));
    EXPECT_EQ(an::ui::resolveThemeDensity(QStringLiteral(" Comfortable ")), ThemeDensity::Comfortable);
    EXPECT_EQ(an::ui::resolveThemeDensity(QStringLiteral("roomy")), ThemeDensity::Compact);
    EXPECT_EQ(an::ui::resolveThemeDensity(QString()), ThemeDensity::Compact);
}
