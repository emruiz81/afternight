// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightControlsExport.h"

#include <QColor>
#include <QFrame>
#include <QString>

class QLabel;

namespace an::ui
{

class AFTERNIGHTCONTROLS_API AnInfoBanner : public QFrame
{
public:
    enum class Tone
    {
        Info,
        Success,
        Warning,
        Error
    };

    explicit AnInfoBanner(const QString& message = QString(), Tone tone = Tone::Info, QWidget* parent = nullptr);
    ~AnInfoBanner() override = default;

    void setMessage(const QString& message);
    [[nodiscard]] QString message() const;
    [[nodiscard]] QLabel* messageLabel() const;

    void setTone(Tone tone);
    [[nodiscard]] Tone tone() const;

    void refreshTheme();

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QLabel* m_messageLabel = nullptr;
    Tone m_tone = Tone::Info;
    QColor m_bgColor;
    QColor m_borderColor;
    int m_radius = 8;
};

} // namespace an::ui
