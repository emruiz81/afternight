// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include <QStylePlugin>

namespace an::ui
{

/// Qt Style Plugin entry point for the AfterNight theme.
///
/// Registers "afternight" with QStyleFactory so both AfterNightApp and
/// AfterNightExtensionHost can install the theme via:
///
///   QApplication::setStyle(QStyleFactory::create("afternight"));
///
/// Initial theme variant is read from the AN_THEME environment variable:
///   AN_THEME=dark  → ThemePreference::Dark
///   AN_THEME=light → ThemePreference::Light
///   (absent)       → ThemePreference::System (palette-based auto-detect)
///
/// Runtime theme refresh (after receiving theme.changed from the host) is
/// performed by calling an::ui::setThemePreference() +
/// installedAnProxyStyle()->refreshThemeTokens() directly, since callers
/// already link libAfterNightControls.
class AnStylePlugin : public QStylePlugin
{
public:
    explicit AnStylePlugin(QObject* parent = nullptr);

    [[nodiscard]] QStyle* create(const QString& key) override;
};

} // namespace an::ui
