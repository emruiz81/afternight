// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightControlsExport.h"

#include <QAbstractButton>
#include <QColor>
#include <QString>

#include <functional>

namespace an::ui
{

/// A colour's swatch on the Interface page - an accent's disc, a surface tint's square - 28 px, filled with the colour
/// it stands for as the theme in force resolves it, asked at each paint so that it follows the theme. Chosen, it
/// carries a check and a ring of the primary text's colour inside its edge; hovered, its outline darkens; with the
/// keyboard's focus, the focus ring. A checkable button named by the colour's name, which its tooltip says too.
class AFTERNIGHTCONTROLS_API ThemeSwatch : public QAbstractButton
{
public:
    enum class Shape
    {
        Disc,
        Square,
    };

    ThemeSwatch(Shape shape, const QString& name, std::function<QColor()> colour, QWidget* parent = nullptr);

    /// The colour as it is painted now.
    [[nodiscard]] QColor colour() const;

    [[nodiscard]] QSize sizeHint() const override;
    [[nodiscard]] QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;
    [[nodiscard]] bool hitButton(const QPoint& position) const override;

private:
    Shape m_shape;
    std::function<QColor()> m_colour;
};

} // namespace an::ui
