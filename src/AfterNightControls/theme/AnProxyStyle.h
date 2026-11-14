// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightControlsExport.h"

#include <QProxyStyle>
#include <QRect>

#include <cstdint>
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
    /// Under the Observatory profile, keeps a menu's surface where the menu is placed: the window holds the margin its
    /// shadow is drawn in, and is moved by that margin when it is shown.
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    // The Observatory profile's drawing (AnProxyStyleObservatory.cpp). Each of the three draw calls hands an element
    // to these first under that profile; each answers whether it drew the element, and what it leaves is drawn by the
    // Classic code below. Under the Classic profile none of them runs.
    [[nodiscard]] bool drawObservatoryPrimitive(PrimitiveElement element, const QStyleOption* option, QPainter* painter,
                                                const QWidget* widget) const;
    [[nodiscard]] bool drawObservatoryControl(ControlElement element, const QStyleOption* option, QPainter* painter,
                                              const QWidget* widget) const;
    [[nodiscard]] bool drawObservatoryComplexControl(ComplexControl control, const QStyleOptionComplex* option,
                                                     QPainter* painter, const QWidget* widget) const;
    /// The Observatory profile's geometry of a sub-control its painters decide; nothing for the Classic answer.
    [[nodiscard]] std::optional<QRect> observatorySubControlRect(ComplexControl control,
                                                                 const QStyleOptionComplex* option,
                                                                 SubControl subControl, const QWidget* widget) const;
    /// The Observatory profile's answer to a style hint its painters decide; nothing for the Classic answer.
    [[nodiscard]] std::optional<int> observatoryStyleHint(StyleHint hint, const QStyleOption* option,
                                                          const QWidget* widget) const;
    /// What the Observatory profile sets on a widget when it is polished: the translucent window and the margin a
    /// menu's or a tooltip's shadow is drawn in, a tooltip's font.
    void polishObservatory(QWidget* widget);

    // Helpers for drawing specific widget types
    void drawGroupBox(const QStyleOption* option, QPainter* painter, const QWidget* widget) const;
    void drawPushButton(const QStyleOption* option, QPainter* painter, const QWidget* widget) const;
    void drawPushButtonLabel(const QStyleOption* option, QPainter* painter, const QWidget* widget) const;
    void drawComboBox(const QStyleOptionComplex* option, QPainter* painter, const QWidget* widget) const;
    void drawSpinBox(const QStyleOptionComplex* option, QPainter* painter, const QWidget* widget) const;
    void drawSlider(const QStyleOptionComplex* option, QPainter* painter, const QWidget* widget) const;
    void drawScrollBar(const QStyleOptionComplex* option, QPainter* painter, const QWidget* widget) const;
    void drawProgressBar(const QStyleOption* option, QPainter* painter, const QWidget* widget) const;
    void drawProgressBarContents(const QStyleOption* option, QPainter* painter, const QWidget* widget) const;
    void drawTabBarTab(const QStyleOption* option, QPainter* painter, const QWidget* widget) const;
    void drawHeaderSection(const QStyleOption* option, QPainter* painter, const QWidget* widget) const;
    void drawMenuItem(const QStyleOption* option, QPainter* painter, const QWidget* widget) const;
    void drawToolButton(const QStyleOption* option, QPainter* painter, const QWidget* widget) const;
    void drawCheckBoxIndicator(const QStyleOption* option, QPainter* painter, const QWidget* widget) const;
    void drawRadioButtonIndicator(const QStyleOption* option, QPainter* painter, const QWidget* widget) const;
    void drawItemViewItem(const QStyleOption* option, QPainter* painter, const QWidget* widget) const;
    void drawLineEditFrame(const QStyleOption* option, QPainter* painter, const QWidget* widget) const;
    void drawToolTip(const QStyleOption* option, QPainter* painter, const QWidget* widget) const;
    void drawMenuBarItem(const QStyleOption* option, QPainter* painter, const QWidget* widget) const;
    void drawSplitterHandle(const QStyleOption* option, QPainter* painter, const QWidget* widget) const;

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

namespace testing
{
/// How many elements the Observatory profile's painters have drawn since the process started. For the suites, which
/// prove the Classic profile draws through its own code alone; the application never reads it.
[[nodiscard]] AFTERNIGHTCONTROLS_API std::uint64_t observatoryPaints();
} // namespace testing

} // namespace an::ui
