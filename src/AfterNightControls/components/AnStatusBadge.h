// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightControlsExport.h"

#include <QColor>
#include <QLabel>

namespace an::ui
{

class AFTERNIGHTCONTROLS_API AnStatusBadge : public QLabel
{
public:
    enum class Status
    {
        Success,
        Warning,
        Error,
        Info
    };

    explicit AnStatusBadge(const QString& text = QString(), Status status = Status::Info, QWidget* parent = nullptr);
    ~AnStatusBadge() override = default;

    void setStatus(Status status);
    [[nodiscard]] Status status() const;

    void refreshTheme();

    /// A tag high under the Observatory profile.
    [[nodiscard]] QSize sizeHint() const override;
    [[nodiscard]] QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    Status m_status = Status::Info;
    QColor m_bgColor;
    QColor m_borderColor;
    QColor m_toneColor;
    int m_radius = 4;
};

} // namespace an::ui
