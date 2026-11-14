// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"

#include "components/AnProgressCard.h"

#include "theme/AnDesignTokens.h"

#include <verdigris/wobjectimpl.h>

W_OBJECT_IMPL(an::ui::AnProgressCard)

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPaintEvent>
#include <QPalette>
#include <QProgressBar>
#include <QVBoxLayout>

#include <algorithm>

namespace an::ui
{

AnProgressCard::AnProgressCard(const QString& title, QWidget* parent) : QFrame(parent)
{
    setObjectName(QStringLiteral("anProgressCard"));
    setFrameShape(QFrame::NoFrame);

    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(8);

    auto* headerLayout = new QHBoxLayout();
    headerLayout->setContentsMargins(0, 0, 0, 0);
    headerLayout->setSpacing(8);
    rootLayout->addLayout(headerLayout);

    m_titleLabel = new QLabel(this);
    m_titleLabel->setObjectName(QStringLiteral("anProgressCardTitle"));
    headerLayout->addWidget(m_titleLabel, 1);

    m_percentageLabel = new QLabel(this);
    m_percentageLabel->setObjectName(QStringLiteral("anProgressCardPercentage"));
    m_percentageLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    headerLayout->addWidget(m_percentageLabel);

    m_progressBar = new QProgressBar(this);
    m_progressBar->setObjectName(QStringLiteral("anProgressCardProgressBar"));
    m_progressBar->setTextVisible(false);
    m_progressBar->setRange(0, 100);
    m_progressBar->setValue(0);
    rootLayout->addWidget(m_progressBar);

    m_etaLabel = new QLabel(this);
    m_etaLabel->setObjectName(QStringLiteral("anProgressCardEta"));
    rootLayout->addWidget(m_etaLabel);

    setTitle(title);
    setEtaText(QString());
    updatePercentageLabel();
    refreshTheme();
}

void AnProgressCard::setTitle(const QString& title)
{
    if (!m_titleLabel)
    {
        return;
    }

    m_titleLabel->setText(title.trimmed());
}

QString AnProgressCard::title() const
{
    return m_titleLabel ? m_titleLabel->text() : QString();
}

QLabel* AnProgressCard::titleLabel() const
{
    return m_titleLabel;
}

void AnProgressCard::setProgressRange(const int minimum, const int maximum)
{
    if (!m_progressBar)
    {
        return;
    }

    const int boundedMin = (std::min)(minimum, maximum);
    const int boundedMax = (std::max)(minimum, maximum);
    m_rangeMinimum = boundedMin;
    m_rangeMaximum = boundedMax;
    if (!m_busy)
    {
        m_progressBar->setRange(boundedMin, boundedMax);
    }
    updatePercentageLabel();
}

void AnProgressCard::setBusy(const bool busy)
{
    if (!m_progressBar || busy == m_busy)
    {
        return;
    }
    m_busy = busy;
    // A range of nothing is Qt's busy indicator; leaving it restores the range the card was given.
    if (busy)
    {
        m_progressBar->setRange(0, 0);
    }
    else
    {
        m_progressBar->setRange(m_rangeMinimum, m_rangeMaximum);
    }
    updatePercentageLabel();
}

bool AnProgressCard::isBusy() const noexcept
{
    return m_busy;
}

void AnProgressCard::setProgressValue(const int value)
{
    if (!m_progressBar)
    {
        return;
    }

    m_progressBar->setValue(value);
    updatePercentageLabel();
}

int AnProgressCard::progressValue() const
{
    return m_progressBar ? m_progressBar->value() : 0;
}

int AnProgressCard::progressMaximum() const
{
    return m_progressBar ? m_progressBar->maximum() : 0;
}

QProgressBar* AnProgressCard::progressBar() const
{
    return m_progressBar;
}

QLabel* AnProgressCard::percentageLabel() const
{
    return m_percentageLabel;
}

void AnProgressCard::setEtaText(const QString& etaText)
{
    if (!m_etaLabel)
    {
        return;
    }

    m_etaLabel->setText(etaText.trimmed());
    updateEtaVisibility();
}

QString AnProgressCard::etaText() const
{
    return m_etaLabel ? m_etaLabel->text() : QString();
}

QLabel* AnProgressCard::etaLabel() const
{
    return m_etaLabel;
}

void AnProgressCard::refreshTheme()
{
    if (!m_titleLabel || !m_percentageLabel || !m_etaLabel || !m_progressBar)
    {
        return;
    }

    refreshTokens();
    const auto& tokenSet = tokens();

    if (auto* rootLayout = qobject_cast<QVBoxLayout*>(layout()))
    {
        rootLayout->setContentsMargins(tokenSet.spacing.md, tokenSet.spacing.md, tokenSet.spacing.md,
                                       tokenSet.spacing.md);
        rootLayout->setSpacing(tokenSet.spacing.sm);
    }

    m_titleLabel->setFont(tokenSet.typography.heading3);
    m_percentageLabel->setFont(tokenSet.typography.caption);
    m_etaLabel->setFont(tokenSet.typography.caption);

    // Store card visual parameters
    const bool observatory = styleProfile() == StyleProfile::Observatory;
    m_bgColor = tokenSet.colors.bgCardPrimary;
    // Under the Observatory profile a card is a tonal container, with no border.
    m_borderColor = observatory ? QColor() : tokenSet.colors.borderPrimary;
    m_radius = tokenSet.radius.lg;
    // Its bar is a thin track, a gap high: the style draws a bar without text as a track across its middle.
    m_progressBar->setMaximumHeight(observatory ? tokenSet.metrics.gap : QWIDGETSIZE_MAX);

    // Label colors via palette
    QPalette titlePal = m_titleLabel->palette();
    titlePal.setColor(QPalette::WindowText, tokenSet.colors.textPrimary);
    m_titleLabel->setPalette(titlePal);

    QPalette secondaryPal = m_percentageLabel->palette();
    secondaryPal.setColor(QPalette::WindowText, tokenSet.colors.textSecondary);
    m_percentageLabel->setPalette(secondaryPal);
    m_etaLabel->setPalette(secondaryPal);

    // Re-polish progress bar so the proxy style handles it
    m_progressBar->style()->unpolish(m_progressBar);
    m_progressBar->style()->polish(m_progressBar);
    m_progressBar->update();

    update();
}

void AnProgressCard::paintEvent(QPaintEvent* /*event*/)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const auto& tokenSet = tokens();
    const QColor bg = m_bgColor.isValid() ? m_bgColor : tokenSet.colors.bgCardPrimary;
    const QColor border = m_borderColor.isValid() ? m_borderColor : tokenSet.colors.borderPrimary;
    const int r = m_radius > 0 ? m_radius : tokenSet.radius.lg;

    if (!m_borderColor.isValid())
    {
        painter.setPen(Qt::NoPen);
        painter.setBrush(bg);
        painter.drawRoundedRect(QRectF(rect()), r, r);
        return;
    }
    painter.setPen(QPen(border, 1));
    painter.setBrush(bg);
    painter.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), r, r);
}

void AnProgressCard::updatePercentageLabel()
{
    if (!m_progressBar || !m_percentageLabel)
    {
        return;
    }

    if (m_busy)
    {
        // A share that is not known is not shown as none done.
        m_percentageLabel->clear();
        return;
    }

    const int minimum = m_progressBar->minimum();
    const int maximum = m_progressBar->maximum();
    const int value = m_progressBar->value();
    const int range = maximum - minimum;

    int percentage = 0;
    if (range > 0)
    {
        const int boundedValue = std::clamp(value, minimum, maximum);
        percentage = ((boundedValue - minimum) * 100) / range;
    }

    m_percentageLabel->setText(QStringLiteral("%1%").arg(percentage));
}

void AnProgressCard::updateEtaVisibility()
{
    if (!m_etaLabel)
    {
        return;
    }

    m_etaLabel->setVisible(!m_etaLabel->text().isEmpty());
}

} // namespace an::ui
