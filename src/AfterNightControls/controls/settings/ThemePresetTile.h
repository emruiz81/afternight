// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightControlsExport.h"
#include "theme/AnDesignTokens.h"

#include <QAbstractButton>
#include <QString>

namespace an::ui
{

/// A colour preset's tile on the Interface page: a miniature of a window in the preset's colours - its surfaces, an
/// accent, two lines of text - with the preset's name under it, checked when it is the preset in force. The miniature
/// is resolved at each paint in the theme and contrast profile in force, so the tiles show each preset as it would look
/// now; the Custom tile shows the parts in force.
class AFTERNIGHTCONTROLS_API ThemePresetTile : public QAbstractButton
{
public:
    ThemePresetTile(ThemePreset preset, const QString& name, QWidget* parent = nullptr);

    [[nodiscard]] ThemePreset preset() const
    {
        return m_preset;
    }

    [[nodiscard]] QSize sizeHint() const override;
    [[nodiscard]] QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    ThemePreset m_preset;
};

/// A theme's tile on the Interface page, the size of a preset's: the same miniature of a window in the light or the
/// dark theme, with the theme's name under it - and, for the theme that follows the system, the dark theme's miniature
/// beside the light one's - checked when it is the theme chosen. It is resolved at each paint in the parts in force.
class AFTERNIGHTCONTROLS_API ThemeModeTile : public QAbstractButton
{
public:
    /// What the tile shows: one theme, or the system's choice of the two.
    enum class Mode
    {
        System,
        Light,
        Dark,
    };

    ThemeModeTile(Mode mode, const QString& name, QWidget* parent = nullptr);

    [[nodiscard]] Mode mode() const
    {
        return m_mode;
    }

    [[nodiscard]] QSize sizeHint() const override;
    [[nodiscard]] QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    Mode m_mode;
};

} // namespace an::ui
