// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// The design tokens as they are recorded: every combination the interface settings can choose - two themes, four
// contrast profiles, five accent families, six surface tints - resolved as the application resolves it and compared,
// token by token, with the record beside this suite. A change to the resolution that moves a value of any combination
// fails here, naming the combination and the token, until the record's entry says the change was meant.

#include "fixtures/BaselineRecord.h"
#include "theme/AnDesignTokens.h"

#include <QColor>
#include <QFont>
#include <QString>
#include <QStringList>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <utility>

namespace
{

using an::tests::fixtures::baselineEntry;
using an::tests::fixtures::BaselineRecord;

constexpr const char* kRecordPath = "src/AfterNightTests/Test_ThemeSystem/data/TokenBaseline.txt";

// The section of what no choice changes: the spacing, the radii and the type scale.
constexpr const char* kSharedSection = "every combination";

constexpr std::array<std::pair<an::ui::ThemeContrastProfile, const char*>, 4u> kContrastProfiles{{
    {an::ui::ThemeContrastProfile::High, "High"},
    {an::ui::ThemeContrastProfile::Balanced, "Balanced"},
    {an::ui::ThemeContrastProfile::Soft, "Soft"},
    {an::ui::ThemeContrastProfile::Oled, "Oled"},
}};

constexpr std::array<std::pair<an::ui::ThemeAccentFamily, const char*>, 5u> kAccentFamilies{{
    {an::ui::ThemeAccentFamily::Blue, "Blue"},
    {an::ui::ThemeAccentFamily::Teal, "Teal"},
    {an::ui::ThemeAccentFamily::Amber, "Amber"},
    {an::ui::ThemeAccentFamily::Amethyst, "Amethyst"},
    {an::ui::ThemeAccentFamily::Rose, "Rose"},
}};

constexpr std::array<std::pair<an::ui::ThemeSurfaceTintFamily, const char*>, 6u> kSurfaceTints{{
    {an::ui::ThemeSurfaceTintFamily::Neutral, "Neutral"},
    {an::ui::ThemeSurfaceTintFamily::Ocean, "Ocean"},
    {an::ui::ThemeSurfaceTintFamily::Mint, "Mint"},
    {an::ui::ThemeSurfaceTintFamily::Amethyst, "Amethyst"},
    {an::ui::ThemeSurfaceTintFamily::Rose, "Rose"},
    {an::ui::ThemeSurfaceTintFamily::Amber, "Amber"},
}};

QStringList recordPreamble()
{
    return {
        QStringLiteral("The design tokens of AfterNight 1.1, for every combination the interface settings can choose:"),
        QStringLiteral("two themes, four contrast profiles, five accent families and six surface tints."),
        QString(),
        QStringLiteral("[every combination] holds what no choice changes: the spacing, the radii and the type scale,"),
        QStringLiteral("the fonts stated against the body font, which is the host's. Every other section is one"),
        QStringLiteral("combination - [theme contrast accent tint] - and holds its colours as #AARRGGBB: the colour"),
        QStringLiteral("roles, the surface layers and the material fallbacks, resolved with the native backdrop off."),
        QString(),
        QStringLiteral("TokenBaseline.ClassicValuesAreRecorded in Test_ThemeSystem compares the running resolution"),
        QStringLiteral(
            "with this record. An entry is edited by the change that means to move its value; the record is"),
        QStringLiteral("not written again whole."),
    };
}

// A colour as the record holds it: alpha first, upper case; a colour never set says so.
QString recorded(const QColor& color)
{
    return color.isValid() ? color.name(QColor::HexArgb).toUpper() : QStringLiteral("unset");
}

// The colours of one resolved token set, a token an entry, in the order the token structures declare them.
QStringList colorEntries(const an::ui::DesignTokens& resolved)
{
    const an::ui::ColorTokens& c = resolved.colors;
    const an::ui::LayerTokens& l = resolved.layers;
    const an::ui::MaterialTokens& m = resolved.materials;
    const std::array<std::pair<const char*, const QColor*>, 39u> colors{{
        {"colors.bgPrimary", &c.bgPrimary},
        {"colors.bgSurface", &c.bgSurface},
        {"colors.bgCard", &c.bgCard},
        {"colors.bgCardPrimary", &c.bgCardPrimary},
        {"colors.bgCardSecondary", &c.bgCardSecondary},
        {"colors.bgInput", &c.bgInput},
        {"colors.bgHover", &c.bgHover},
        {"colors.textPrimary", &c.textPrimary},
        {"colors.textSecondary", &c.textSecondary},
        {"colors.textTertiary", &c.textTertiary},
        {"colors.textAccent", &c.textAccent},
        {"colors.textOnAccent", &c.textOnAccent},
        {"colors.borderPrimary", &c.borderPrimary},
        {"colors.borderSubtle", &c.borderSubtle},
        {"colors.borderFocus", &c.borderFocus},
        {"colors.statusSuccess", &c.statusSuccess},
        {"colors.statusWarning", &c.statusWarning},
        {"colors.statusError", &c.statusError},
        {"colors.statusInfo", &c.statusInfo},
        {"colors.statusSuccessSoft", &c.statusSuccessSoft},
        {"colors.statusWarningSoft", &c.statusWarningSoft},
        {"colors.statusErrorSoft", &c.statusErrorSoft},
        {"colors.statusInfoSoft", &c.statusInfoSoft},
        {"colors.accentPrimary", &c.accentPrimary},
        {"colors.accentHover", &c.accentHover},
        {"colors.accentPressed", &c.accentPressed},
        {"colors.accentDisabled", &c.accentDisabled},
        {"colors.accentPinned", &c.accentPinned},
        {"colors.cardSurfaceTint", &c.cardSurfaceTint},
        {"colors.cardSurfaceBorderTint", &c.cardSurfaceBorderTint},
        {"colors.shadowOverlay", &c.shadowOverlay},
        {"layers.surface0", &l.surface0},
        {"layers.surface1", &l.surface1},
        {"layers.surface2", &l.surface2},
        {"layers.surface3", &l.surface3},
        {"layers.surface4", &l.surface4},
        {"materials.solidSurface", &m.solidSurface},
        {"materials.micaSurface", &m.micaSurface},
        {"materials.acrylicSurface", &m.acrylicSurface},
    }};
    QStringList entries;
    for (const auto& [name, color] : colors)
    {
        entries.append(baselineEntry({QString::fromLatin1(name), recorded(*color)}));
    }
    return entries;
}

// A font's size against the body font's: the points it adds or takes.
QString sizeAgainstBody(const QFont& font, const QFont& body)
{
    const int added = font.pointSize() - body.pointSize();
    if (added == 0)
    {
        return QStringLiteral("the body's size");
    }
    return QStringLiteral("the body's size %1 %2")
        .arg(added > 0 ? QStringLiteral("plus") : QStringLiteral("less"))
        .arg(added > 0 ? added : -added);
}

// The caption's size against the body font's: a point less and never under eight, whatever the host's body font is.
QString captionSizeAgainstBody(const QFont& caption, const QFont& body)
{
    return caption.pointSize() == (std::max)(8, body.pointSize() - 1)
               ? QStringLiteral("the body's size less 1, and 8 at least")
               : sizeAgainstBody(caption, body);
}

// A font of the type scale against the body font: its size, its weight when it is not the body's, and its family and
// style hint when they are not the body's. The body font is the host's, so nothing of it is recorded.
QString fontEntry(const char* name, const QFont& font, const QFont& body, const QString& size)
{
    const QString weight = font.weight() == body.weight() ? QStringLiteral("the body's weight")
                                                          : QStringLiteral("weight %1").arg(int(font.weight()));
    const QString family = font.family() == body.family() ? QStringLiteral("the body's family")
                                                          : QStringLiteral("family ") + font.family();
    QString hint = QStringLiteral("the body's style hint");
    if (font.styleHint() != body.styleHint())
    {
        hint = font.styleHint() == QFont::Monospace ? QStringLiteral("style hint Monospace")
                                                    : QStringLiteral("style hint %1").arg(int(font.styleHint()));
    }
    return baselineEntry({QString::fromLatin1(name), size, weight, family, hint});
}

// What no choice changes: the spacing, the radii and the type scale of one resolved token set.
QStringList sharedEntries(const an::ui::DesignTokens& resolved)
{
    const an::ui::SpacingTokens& s = resolved.spacing;
    const an::ui::RadiusTokens& r = resolved.radius;
    const an::ui::TypographyTokens& t = resolved.typography;
    QStringList entries;
    const auto number = [&entries](const char* name, int value) {
        entries.append(baselineEntry({QString::fromLatin1(name), QString::number(value)}));
    };
    number("spacing.xs", s.xs);
    number("spacing.sm", s.sm);
    number("spacing.md", s.md);
    number("spacing.lg", s.lg);
    number("spacing.xl", s.xl);
    number("radius.none", r.none);
    number("radius.sm", r.sm);
    number("radius.md", r.md);
    number("radius.lg", r.lg);
    number("radius.xl", r.xl);
    entries.append(fontEntry("typography.heading1", t.heading1, t.body, sizeAgainstBody(t.heading1, t.body)));
    entries.append(fontEntry("typography.heading2", t.heading2, t.body, sizeAgainstBody(t.heading2, t.body)));
    entries.append(fontEntry("typography.heading3", t.heading3, t.body, sizeAgainstBody(t.heading3, t.body)));
    entries.append(fontEntry("typography.caption", t.caption, t.body, captionSizeAgainstBody(t.caption, t.body)));
    entries.append(fontEntry("typography.monospace", t.monospace, t.body, sizeAgainstBody(t.monospace, t.body)));
    return entries;
}

// The interface's choices as they were, put back when the case ends.
class ChoicesRestored final
{
public:
    ChoicesRestored()
        : m_backdrop(an::ui::materialBackdropMode()), m_contrast(an::ui::themeContrastProfile()),
          m_accent(an::ui::themeAccentFamily()), m_tint(an::ui::themeSurfaceTintFamily())
    {
    }

    ~ChoicesRestored()
    {
        an::ui::setMaterialBackdropMode(m_backdrop);
        an::ui::setThemeContrastProfile(m_contrast);
        an::ui::setThemeAccentFamily(m_accent);
        an::ui::setThemeSurfaceTintFamily(m_tint);
        an::ui::refreshTokens();
    }

    ChoicesRestored(const ChoicesRestored&) = delete;
    ChoicesRestored& operator=(const ChoicesRestored&) = delete;

private:
    an::ui::MaterialBackdropMode m_backdrop;
    an::ui::ThemeContrastProfile m_contrast;
    an::ui::ThemeAccentFamily m_accent;
    an::ui::ThemeSurfaceTintFamily m_tint;
};

} // namespace

// Every combination of theme, contrast profile, accent family and surface tint resolves to the values recorded for
// it, and the spacing, the radii and the type scale are the recorded ones under all of them. The native backdrop is
// set off for the comparison: with it on, the material surfaces follow what the host's window system offers.
TEST(TokenBaseline, ClassicValuesAreRecorded)
{
    const ChoicesRestored restored;
    an::ui::setMaterialBackdropMode(an::ui::MaterialBackdropMode::ForceFallback);
    BaselineRecord record(QString::fromLatin1(kRecordPath), recordPreamble());

    // What no choice changes is read from the first combination and held under every other.
    an::ui::setThemeContrastProfile(kContrastProfiles.front().first);
    an::ui::setThemeAccentFamily(kAccentFamilies.front().first);
    an::ui::setThemeSurfaceTintFamily(kSurfaceTints.front().first);
    an::ui::refreshTokens();
    const QStringList shared = sharedEntries(an::ui::resolvedDarkTokens());
    EXPECT_TRUE(record.matches(QString::fromLatin1(kSharedSection), shared));

    int combinations = 0;
    for (const auto& [contrast, contrastName] : kContrastProfiles)
    {
        for (const auto& [accent, accentName] : kAccentFamilies)
        {
            for (const auto& [tint, tintName] : kSurfaceTints)
            {
                an::ui::setThemeContrastProfile(contrast);
                an::ui::setThemeAccentFamily(accent);
                an::ui::setThemeSurfaceTintFamily(tint);
                an::ui::refreshTokens();
                const std::array<std::pair<const char*, const an::ui::DesignTokens*>, 2u> themes{{
                    {"dark", &an::ui::resolvedDarkTokens()},
                    {"light", &an::ui::resolvedLightTokens()},
                }};
                for (const auto& [themeName, resolved] : themes)
                {
                    const QString combination =
                        QStringLiteral("%1 %2 %3 %4")
                            .arg(QString::fromLatin1(themeName), QString::fromLatin1(contrastName),
                                 QString::fromLatin1(accentName), QString::fromLatin1(tintName));
                    EXPECT_TRUE(record.matches(combination, colorEntries(*resolved)));
                    EXPECT_EQ(sharedEntries(*resolved), shared)
                        << combination.toStdString() << " moves a spacing, a radius or a font of the type scale";
                    ++combinations;
                }
            }
        }
    }
    EXPECT_EQ(combinations, 240);
    // A section no combination answers to is a combination the settings no longer offer.
    EXPECT_EQ(record.sections().size(), combinations + 1)
        << "the record holds a section that is no combination the interface settings can choose";
}
