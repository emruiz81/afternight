// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightControlsExport.h"

#include <QColor>
#include <QFrame>
#include <QString>

#include <verdigris/wobjectdefs.h>

class QLabel;
class QProgressBar;

namespace an::ui
{

class AFTERNIGHTCONTROLS_API AnProgressCard : public QFrame
{
    W_OBJECT(AnProgressCard)
public:
    explicit AnProgressCard(const QString& title = QString(), QWidget* parent = nullptr);
    ~AnProgressCard() override = default;

    void setTitle(const QString& title);
    [[nodiscard]] QString title() const;
    [[nodiscard]] QLabel* titleLabel() const;

    void setProgressRange(int minimum, int maximum);
    void setProgressValue(int value);

    /// Shows work whose share done is not known as a busy bar with no percentage, or - leaving it - the range the card
    /// held before.
    ///
    /// @param busy whether the work's share done is unknown.
    void setBusy(bool busy);

    /// Whether the card shows a busy bar.
    [[nodiscard]] bool isBusy() const noexcept;
    [[nodiscard]] int progressValue() const;
    [[nodiscard]] int progressMaximum() const;
    [[nodiscard]] QProgressBar* progressBar() const;
    [[nodiscard]] QLabel* percentageLabel() const;

    void setEtaText(const QString& etaText);
    [[nodiscard]] QString etaText() const;
    [[nodiscard]] QLabel* etaLabel() const;

    void refreshTheme();

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    void updatePercentageLabel();
    void updateEtaVisibility();

    QLabel* m_titleLabel = nullptr;
    QLabel* m_percentageLabel = nullptr;
    QLabel* m_etaLabel = nullptr;
    QProgressBar* m_progressBar = nullptr;
    bool m_busy = false;
    int m_rangeMinimum = 0;
    int m_rangeMaximum = 100;
    QColor m_bgColor;
    QColor m_borderColor;
    int m_radius = 12;
};

} // namespace an::ui
