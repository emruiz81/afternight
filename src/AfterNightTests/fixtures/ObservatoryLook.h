// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// The looks a render case of the Observatory profile runs under - dark, light and a tinted preset, each at both
/// densities - the proxy style installed in one of them for a scope, and the picture of a painting read in logical
/// coordinates at a device pixel ratio.

#include "fixtures/ScopedStyleProfile.h"
#include "theme/AnDesignTokens.h"
#include "theme/AnProxyStyle.h"

#include <QApplication>
#include <QColor>
#include <QFont>
#include <QImage>
#include <QPainter>
#include <QPalette>
#include <QPoint>
#include <QSize>
#include <QString>
#include <QStyle>
#include <QStyleFactory>
#include <QWidget>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <string>

namespace an::ui::testing
{

/// A look a case runs under: a theme, a colour preset and a density.
struct ObservatoryLook
{
    const char* name = "";
    ThemePreference theme = ThemePreference::Dark;
    ThemePreset preset = ThemePreset::SlateBlue;
    ThemeDensity density = ThemeDensity::Compact;
};

/// Dark, light and the Observatory preset (an Ocean tint at the Strong strength under an Amber accent), each at both
/// densities.
inline const std::array<ObservatoryLook, 6u>& observatoryLooks()
{
    static const std::array<ObservatoryLook, 6u> looks{{
        {"dark, compact", ThemePreference::Dark, ThemePreset::SlateBlue, ThemeDensity::Compact},
        {"light, compact", ThemePreference::Light, ThemePreset::SlateBlue, ThemeDensity::Compact},
        {"tinted, compact", ThemePreference::Dark, ThemePreset::Observatory, ThemeDensity::Compact},
        {"dark, comfortable", ThemePreference::Dark, ThemePreset::SlateBlue, ThemeDensity::Comfortable},
        {"light, comfortable", ThemePreference::Light, ThemePreset::SlateBlue, ThemeDensity::Comfortable},
        {"tinted, comfortable", ThemePreference::Dark, ThemePreset::Observatory, ThemeDensity::Comfortable},
    }};
    return looks;
}

/// The device pixel ratios a render case paints at.
inline constexpr std::array<qreal, 4u> kRenderRatios{1.0, 1.25, 1.5, 2.0};

/// The proxy style installed as the application's style under a profile - the Observatory one unless said - in a look,
/// for a scope. Everything it changes is put back when it goes: the profile, the theme and the user's choices, Night,
/// the application's style, palette and font.
class ObservatoryStyleScope final
{
public:
    explicit ObservatoryStyleScope(const ObservatoryLook& look, const StyleProfile profile = StyleProfile::Observatory)
        : m_profile(profile), m_preference(themePreference()), m_contrast(themeContrastProfile()),
          m_accent(themeAccentFamily()), m_tint(themeSurfaceTintFamily()), m_strength(themeTintStrength()),
          m_density(themeDensity()), m_night(nightModeSet()), m_palette(QApplication::palette()),
          m_font(QApplication::font()), m_styleName(QApplication::style()->name())
    {
        setNightMode(false);
        setThemeContrastProfile(ThemeContrastProfile::Balanced);
        applyLook(look);
        m_style = installAnProxyStyle();
    }

    ~ObservatoryStyleScope()
    {
        setNightMode(m_night);
        setThemePreference(m_preference);
        setThemeContrastProfile(m_contrast);
        setThemeAccentFamily(m_accent);
        setThemeSurfaceTintFamily(m_tint);
        setThemeTintStrength(m_strength);
        setThemeDensity(m_density);
        // The application's style goes with the one that replaces it.
        QApplication::setStyle(QStyleFactory::create(m_styleName));
        registerInstalledAnProxyStyle(nullptr);
        QApplication::setPalette(m_palette);
        QApplication::setFont(m_font);
    }

    ObservatoryStyleScope(const ObservatoryStyleScope&) = delete;
    ObservatoryStyleScope& operator=(const ObservatoryStyleScope&) = delete;
    ObservatoryStyleScope(ObservatoryStyleScope&&) = delete;
    ObservatoryStyleScope& operator=(ObservatoryStyleScope&&) = delete;

    /// Puts another look in force and tells the style, as the application does when the user changes one.
    void setLook(const ObservatoryLook& look)
    {
        applyLook(look);
        m_style->refreshThemeTokens();
    }

    /// Puts Night in force or out of it and tells the style.
    void setNight(const bool night)
    {
        setNightMode(night);
        m_style->refreshThemeTokens();
    }

    [[nodiscard]] AnProxyStyle* style() const
    {
        return m_style;
    }

private:
    static void applyLook(const ObservatoryLook& look)
    {
        setThemePreference(look.theme);
        applyThemePreset(look.preset);
        setThemeDensity(look.density);
    }

    ScopedStyleProfile m_profile;
    ThemePreference m_preference;
    ThemeContrastProfile m_contrast;
    ThemeAccentFamily m_accent;
    ThemeSurfaceTintFamily m_tint;
    ThemeTintStrength m_strength;
    ThemeDensity m_density;
    bool m_night;
    QPalette m_palette;
    QFont m_font;
    QString m_styleName;
    AnProxyStyle* m_style = nullptr;
};

/// A painting rendered at a device pixel ratio over a background, read in logical coordinates.
class RenderedPicture final
{
public:
    RenderedPicture(const QSize logicalSize, const qreal ratio, const QColor& background,
                    const std::function<void(QPainter&)>& paint)
        : m_ratio(ratio), m_image(QSize(static_cast<int>(std::ceil(logicalSize.width() * ratio)),
                                        static_cast<int>(std::ceil(logicalSize.height() * ratio))),
                                  QImage::Format_ARGB32_Premultiplied)
    {
        m_image.setDevicePixelRatio(ratio);
        m_image.fill(background);
        QPainter painter(&m_image);
        paint(painter);
    }

    /// A widget rendered with its children over the background, which stands for what the widget lies on: a widget
    /// rendered alone would otherwise be laid on its window's colour.
    [[nodiscard]] static RenderedPicture ofWidget(QWidget& widget, const qreal ratio, const QColor& background)
    {
        return RenderedPicture(widget.size(), ratio, background, [&widget](QPainter& painter) {
            widget.render(&painter, QPoint(), QRegion(), QWidget::DrawChildren);
        });
    }

    /// The colour of the device pixel under a logical point, opaque.
    [[nodiscard]] QColor at(const qreal x, const qreal y) const
    {
        const int px = std::clamp(static_cast<int>(std::floor(x * m_ratio)), 0, m_image.width() - 1);
        const int py = std::clamp(static_cast<int>(std::floor(y * m_ratio)), 0, m_image.height() - 1);
        return m_image.pixelColor(px, py);
    }

    [[nodiscard]] const QImage& image() const
    {
        return m_image;
    }

    [[nodiscard]] qreal ratio() const
    {
        return m_ratio;
    }

private:
    qreal m_ratio;
    QImage m_image;
};

/// Whether two colours are the same within a step per channel: what a blend's rounding may move.
[[nodiscard]] inline bool sameColour(const QColor& a, const QColor& b, const int tolerance = 2)
{
    return std::abs(a.red() - b.red()) <= tolerance && std::abs(a.green() - b.green()) <= tolerance &&
           std::abs(a.blue() - b.blue()) <= tolerance;
}

/// A colour for a failure message: `#rrggbb`, or `#aarrggbb` when it is translucent.
[[nodiscard]] inline std::string colourName(const QColor& colour)
{
    return colour.name(colour.alpha() < 255 ? QColor::HexArgb : QColor::HexRgb).toStdString();
}

/// A colour laid over a background at an opacity, as a painter lays it.
[[nodiscard]] inline QColor layered(const QColor& colour, const qreal opacity, const QColor& background)
{
    QColor over = colour;
    over.setAlphaF(colour.alphaF() * opacity);
    return compositeOver(over, background);
}

} // namespace an::ui::testing
