// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightControlsExport.h"

#include <QColor>
#include <QFrame>
#include <QString>
#include <QStringList>

#include <functional>
#include <vector>

class QLabel;
class QPushButton;

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

    /// An act the banner offers after its message - Restart, Close - a button that does what it says.
    struct Action
    {
        QString text;
        std::function<void()> act;
    };

    /// The acts the banner offers, in order, after its message; none takes them away.
    void setActions(std::vector<Action> actions);

    /// The texts of the acts the banner offers now, in order.
    [[nodiscard]] QStringList actionTexts() const;

    void refreshTheme();

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QLabel* m_messageLabel = nullptr;
    std::vector<QPushButton*> m_actionButtons;
    Tone m_tone = Tone::Info;
    QColor m_bgColor;
    int m_radius = 8;
};

} // namespace an::ui
