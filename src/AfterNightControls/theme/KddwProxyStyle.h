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

/// QProxyStyle specifically designed for the KDDockWidgets subsystem.
///
/// This style is installed on the KDDW main window so that all dock-related
/// child widgets (title bars, tab bars, separators, group frames, floating
/// windows, corner buttons) render consistently with the AnDesignTokens
/// color/spacing/radius system.
///
/// It delegates standard widget painting to the global AnProxyStyle (via the
/// proxy chain) and adds KDDW-specific overrides:
///
///   - CE_DockWidgetTitle  : gradient title-bar background + accent underline
///   - CE_Splitter         : thin separator with hover glow
///   - CE_TabBarTabShape   : KDDW tab shape with forceUnselected support
///   - CE_TabBarTabLabel   : KDDW tab label coloring
///   - PE_PanelButtonTool  : transparent base, rounded hover/press for KDDW btns
///   - PE_Frame            : KDDW group-frame border with token colors
///   - CC_ToolButton       : title-bar button icon rendering
///
/// Usage:
/// @code
///   auto* kddwStyle = an::ui::installKddwProxyStyle(dockMainWindow);
///   // ... on theme change:
///   kddwStyle->refreshThemeTokens();
/// @endcode
///
/// Under the Observatory profile it is also the style of each tab strip of the dock library (`AN_TabBar` sets it, in
/// place of the library's own), so that what the library paints itself - the tabs, and the picture of a tab dragged
/// along its strip - is drawn in that look: the strip tells it what each tab shows (`ObservatoryDockTabSource`), and
/// it places the tab's close button and keeps the library's rule that tabs are not animated. Each override hands a tab
/// strip's element to the Observatory code in one line at its top; what that code leaves falls through to the Classic
/// code, which is otherwise untouched.
class AFTERNIGHTCONTROLS_API KddwProxyStyle : public QProxyStyle
{
public:
    /// Constructs the KDDW proxy style.
    /// @param baseStyle  Optional owned base style. If null, an independent AfterNight base style is created.
    explicit KddwProxyStyle(QStyle* baseStyle = nullptr);
    ~KddwProxyStyle() override;

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
    [[nodiscard]] int pixelMetric(PixelMetric metric, const QStyleOption* option = nullptr,
                                  const QWidget* widget = nullptr) const override;

    [[nodiscard]] QSize sizeFromContents(ContentsType type, const QStyleOption* option, const QSize& contentsSize,
                                         const QWidget* widget = nullptr) const override;

    [[nodiscard]] QRect subElementRect(SubElement element, const QStyleOption* option,
                                       const QWidget* widget = nullptr) const override;

    [[nodiscard]] int styleHint(StyleHint hint, const QStyleOption* option = nullptr, const QWidget* widget = nullptr,
                                QStyleHintReturn* returnData = nullptr) const override;

    // ── Polish / Unpolish ───────────────────────────────────────
    void polish(QWidget* widget) override;
    void polish(QPalette& palette) override;

    // ── Theme API ───────────────────────────────────────────────
    /// Refresh token pointer and re-polish all KDDW widgets.
    void refreshThemeTokens();

private:
    // The Observatory profile's drawing of a dock tab strip. Each answers whether it decided the element; what it
    // leaves is the Classic code's.
    [[nodiscard]] bool drawObservatoryPrimitive(PrimitiveElement element, const QStyleOption* option,
                                                const QWidget* widget) const;
    [[nodiscard]] bool drawObservatoryControl(ControlElement element, const QStyleOption* option, QPainter* painter,
                                              const QWidget* widget) const;
    [[nodiscard]] std::optional<int> observatoryPixelMetric(PixelMetric metric, const QWidget* widget) const;
    [[nodiscard]] std::optional<QRect> observatorySubElementRect(SubElement element, const QStyleOption* option,
                                                                 const QWidget* widget) const;
    [[nodiscard]] std::optional<int> observatoryStyleHint(StyleHint hint, const QWidget* widget) const;

    void drawDockTitleBar(const QStyleOption* option, QPainter* painter, const QWidget* widget) const;
    void drawDockSeparator(const QStyleOption* option, QPainter* painter, const QWidget* widget) const;
    void drawDockTabShape(const QStyleOption* option, QPainter* painter, const QWidget* widget) const;
    void drawDockTabLabel(const QStyleOption* option, QPainter* painter, const QWidget* widget) const;
    void drawDockToolButton(const QStyleOptionComplex* option, QPainter* painter, const QWidget* widget) const;
    void drawDockButtonPanel(const QStyleOption* option, QPainter* painter, const QWidget* widget) const;
    void drawDockGroupFrame(const QStyleOption* option, QPainter* painter, const QWidget* widget) const;

    void setupDockPalette(QPalette& palette) const;

    const DesignTokens* m_tokens = nullptr;
};

/// Install a KddwProxyStyle on @p dockMainWindow. Call once during bootstrap.
/// Returns a non-owning pointer (the widget takes ownership).
AFTERNIGHTCONTROLS_API KddwProxyStyle* installKddwProxyStyle(QWidget* dockMainWindow);

/// Returns the currently installed KddwProxyStyle, or nullptr.
[[nodiscard]] AFTERNIGHTCONTROLS_API KddwProxyStyle* installedKddwProxyStyle();

} // namespace an::ui
