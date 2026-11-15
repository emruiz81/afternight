// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// The status line of the Observatory window: the band along the bottom of the centre and the column.

#include "AfterNightShellObservatoryExport.h"

#include <QWidget>

class QEvent;
class QHBoxLayout;
class QPaintEvent;

namespace an::ui::observatory
{

/// The band along the bottom of the window, right of the rail: the first surface with a hairline above it, as high as
/// the density's status line.
class AFTERNIGHTSHELLOBSERVATORY_API StatusLine final : public QWidget
{
public:
    explicit StatusLine(QWidget* parent = nullptr);

    /// The row the line's parts are laid out in.
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
