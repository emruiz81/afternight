// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightControlsExport.h"

#include <QProxyStyle>
#include <QRect>

#include <optional>

namespace an::ui
{

struct DesignTokens;

/// High-performance QProxyStyle replacement for QSS-based theming.
///
/// Instead of parsing and matching CSS selectors at every paint event,
/// this style draws all standard widgets directly in C++ using the
/// AnDesignTokens color/spacing/radius values. Theme switching simply
/// updates the token pointer and calls polish() once.
///
/// Notable QSS features reproduced:
///   - Rounded-rect backgrounds on QGroupBox, QPushButton, QComboBox,
///     QSpinBox, QDoubleSpinBox, QLineEdit, QTabBar, QScrollBar
///   - Token-driven colors for all states (normal, hover, focus, disabled, pressed)
///   - Custom property selectors: buttonVariant="primary", anStatusTone, etc.
///   - Accent-colored QSlider groove + handle with hover growth
///   - Correctly themed QCheckBox/QRadioButton indicators
///   - QHeaderView, QTreeView/QTableView/QListView item painting
///   - QProgressBar with accent/semantic tone chunks
///   - QToolTip, QMenu, QMenuBar, QToolBar, QToolButton, QStatusBar
///   - QDockWidget title bar
///   - QSplitter handles
class AFTERNIGHTCONTROLS_API AnProxyStyle : public QProxyStyle
{
public:
    explicit AnProxyStyle(QStyle* baseStyle = nullptr);
    ~AnProxyStyle() override;

    // ── Primitives ──────────────────────────────────────────────
    void drawPrimitive(PrimitiveElement element, const QStyleOption* option, QPainter* painter,
                       const QWidget* widget = nullptr) const override;

    // ── Controls ────────────────────────────────────────────────
    void drawControl(ControlElement element, const QStyleOption* option, QPainter* painter,
                     const QWidget* widget = nullptr) const override;

    // ── Complex controls ────────────────────────────────────────
    void drawComplexControl(ComplexControl control, const QStyleOptionComplex* option, QPainter* painter,
                            const QWidget* widget = nullptr) const override;

    // ── Geometry ────────────────────────────────────────────────
    [[nodiscard]] QSize sizeFromContents(ContentsType type, const QStyleOption* option, const QSize& contentsSize,
                                         const QWidget* widget = nullptr) const override;

    [[nodiscard]] QRect subControlRect(ComplexControl control, const QStyleOptionComplex* option, SubControl subControl,
                                       const QWidget* widget = nullptr) const override;

    [[nodiscard]] QRect subElementRect(SubElement element, const QStyleOption* option,
                                       const QWidget* widget = nullptr) const override;

    [[nodiscard]] int pixelMetric(PixelMetric metric, const QStyleOption* option = nullptr,
                                  const QWidget* widget = nullptr) const override;

    [[nodiscard]] int styleHint(StyleHint hint, const QStyleOption* option = nullptr, const QWidget* widget = nullptr,
                                QStyleHintReturn* returnData = nullptr) const override;

    // ── Icons ───────────────────────────────────────────────────
    [[nodiscard]] QIcon standardIcon(StandardPixmap standardIcon, const QStyleOption* option = nullptr,
                                     const QWidget* widget = nullptr) const override;

    // ── Polish / Unpolish ───────────────────────────────────────
    void polish(QWidget* widget) override;
    void polish(QPalette& palette) override;
    void polish(QApplication* app) override;
    void unpolish(QWidget* widget) override;

    // ── Theme API ───────────────────────────────────────────────
    /// Call after changing AnDesignTokens theme to refresh the style.
    void refreshThemeTokens();

protected:
    /// Keeps a menu's surface where the menu is placed: the window holds the margin its shadow is drawn in, and is
    /// moved by that margin when it is shown.
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    // The painters of the standard widgets (AnProxyStyleObservatory.cpp). Each of the three draw calls hands an element
    // to these first; each answers whether it drew the element, and what it leaves is drawn by the style's own
    // code (AnProxyStyle.cpp) or by the base style.
    [[nodiscard]] bool drawObservatoryPrimitive(PrimitiveElement element, const QStyleOption* option, QPainter* painter,
                                                const QWidget* widget) const;
    [[nodiscard]] bool drawObservatoryControl(ControlElement element, const QStyleOption* option, QPainter* painter,
                                              const QWidget* widget) const;
    [[nodiscard]] bool drawObservatoryComplexControl(ComplexControl control, const QStyleOptionComplex* option,
                                                     QPainter* painter, const QWidget* widget) const;
    /// The geometry of a sub-control the painters decide; nothing for the one the style's own code or the base answers.
    [[nodiscard]] std::optional<QRect> observatorySubControlRect(ComplexControl control,
                                                                 const QStyleOptionComplex* option,
                                                                 SubControl subControl, const QWidget* widget) const;
    /// The answer to a style hint the painters decide; nothing for the one the style's own code or the base answers.
    [[nodiscard]] std::optional<int> observatoryStyleHint(StyleHint hint, const QStyleOption* option,
                                                          const QWidget* widget) const;
    /// What the painters set on a widget when it is polished: the translucent window and the margin a menu's or a
    /// tooltip's shadow is drawn in, a tooltip's font.
    void polishObservatory(QWidget* widget);

    void setupPalette(QPalette& palette) const;

    const DesignTokens* m_tokens = nullptr;
};

/// Install the AnProxyStyle as the application style. Call once at startup.
/// Returns a non-owning pointer (Qt takes ownership via QApplication::setStyle).
AFTERNIGHTCONTROLS_API AnProxyStyle* installAnProxyStyle();

/// Returns the currently installed AnProxyStyle, or nullptr if not installed.
[[nodiscard]] AFTERNIGHTCONTROLS_API AnProxyStyle* installedAnProxyStyle();

/// Register an externally created AnProxyStyle instance as the installed style.
/// Use this after loading the style via QStyleFactory::create("afternight") to
/// make installedAnProxyStyle() track the plugin-created instance.
/// The caller is responsible for having already called QApplication::setStyle().
AFTERNIGHTCONTROLS_API void registerInstalledAnProxyStyle(AnProxyStyle* style);

} // namespace an::ui
