// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include <QProxyStyle>

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
class KddwProxyStyle : public QProxyStyle
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

    // ── Polish / Unpolish ───────────────────────────────────────
    void polish(QWidget* widget) override;
    void polish(QPalette& palette) override;

    // ── Theme API ───────────────────────────────────────────────
    /// Refresh token pointer and re-polish all KDDW widgets.
    void refreshThemeTokens();

private:
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
KddwProxyStyle* installKddwProxyStyle(QWidget* dockMainWindow);

/// Returns the currently installed KddwProxyStyle, or nullptr.
[[nodiscard]] KddwProxyStyle* installedKddwProxyStyle();

} // namespace an::ui
