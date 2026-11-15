// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// The top bar of the Observatory window: the band across the window under its title bar.

#include "AfterNightShellObservatoryExport.h"

#include <QWidget>

class QEvent;
class QHBoxLayout;
class QPaintEvent;

namespace an::ui::observatory
{

/// The band across the top of the window: the first surface with a hairline under it, as high as the density's top
/// bar. It lays out what it is given in one row, from the left.
class AFTERNIGHTSHELLOBSERVATORY_API TopBar final : public QWidget
{
public:
    explicit TopBar(QWidget* parent = nullptr);

    /// The row the bar's parts are laid out in.
    [[nodiscard]] QHBoxLayout* row() const noexcept;

    [[nodiscard]] QSize sizeHint() const override;
    [[nodiscard]] QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;
    void changeEvent(QEvent* event) override;

private:
    QHBoxLayout* m_row = nullptr;
};

} // namespace an::ui::observatory
