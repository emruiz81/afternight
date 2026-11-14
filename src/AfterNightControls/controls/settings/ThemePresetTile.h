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

} // namespace an::ui
