// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"

#include "chrome/StatusLine.h"

#include "components/AnChip.h"
#include "components/AnIconButton.h"
#include "theme/AnDesignTokens.h"
#include "theme/ComponentParts.h"
#include "widgets/ImageStatusTexts.h"

#include <QComboBox>
#include <QEvent>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QPainter>

namespace an::ui::observatory
{

// ---- StatusText

StatusText::StatusText(const bool mono, QWidget* parent) : QWidget(parent), m_mono(mono)
{
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
}

void StatusText::setText(const QString& text, const Tone tone)
{
    if (text == m_text && tone == m_tone)
    {
        return;
    }
    m_text = text;
    m_tone = tone;
    updateGeometry();
    update();
}

const QString& StatusText::text() const noexcept
{
    return m_text;
}

StatusText::Tone StatusText::tone() const noexcept
{
    return m_tone;
}

QFont StatusText::textFont() const
{
    const an::ui::DesignTokens& t = an::ui::tokens();
    QFont font = m_mono ? t.typography.monospace : t.typography.body;
    font.setPixelSize(t.metrics.fontSmall);
    return font;
}

QSize StatusText::sizeHint() const
{
    const QFontMetrics metrics(textFont());
    return {m_text.isEmpty() ? 0 : metrics.horizontalAdvance(m_text), metrics.height()};
}

QSize StatusText::minimumSizeHint() const
{
    const QFontMetrics metrics(textFont());
    return {0, metrics.height()};
}

void StatusText::paintEvent(QPaintEvent* /*event*/)
{
    if (m_text.isEmpty())
    {
        return;
    }
    const an::ui::DesignTokens& t = an::ui::tokens();
    QPainter painter(this);
    const QFont font = textFont();
    painter.setFont(font);
    painter.setPen(m_tone == Tone::Secondary ? t.colors.textSecondary : t.colors.textTertiary);
    const QString shown = QFontMetrics(font).elidedText(m_text, Qt::ElideRight, width());
    painter.drawText(rect(), Qt::AlignLeft | Qt::AlignVCenter, shown);
}

void StatusText::changeEvent(QEvent* event)
{
    if (event != nullptr && (event->type() == QEvent::StyleChange || event->type() == QEvent::PaletteChange))
    {
        updateGeometry();
        update();
    }
    QWidget::changeEvent(event);
}

// ---- StatusLine

StatusLine::StatusLine(QWidget* parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("observatoryStatusLine"));
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_row = new QHBoxLayout(this);
    // The hairline above takes the first row of pixels.
    m_row->setContentsMargins(an::ui::parts::kStatusLinePadding, 1, an::ui::parts::kStatusLinePadding, 0);
    m_row->setSpacing(an::ui::parts::kStatusLineGap);

    m_selector = new QComboBox(this);
    m_selector->setObjectName(QStringLiteral("observatoryViewSelector"));
    m_selector->setAccessibleName(tr("Active image"));
    // The line holds no more than the extra-small control's height.
    m_selector->setProperty(an::ui::parts::kControlHeightProperty,
                            QString::fromLatin1(an::ui::parts::kControlHeightExtraSmall));
    m_selector->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    m_selector->setToolTip(tr("The active image"));
    m_selector->hide();
    m_facts = new StatusText(true, this);
    m_facts->setObjectName(QStringLiteral("observatoryStatusFacts"));
    m_zoom = new StatusText(true, this);
    m_zoom->setObjectName(QStringLiteral("observatoryStatusZoom"));
    m_rightAscension = new StatusText(true, this);
    m_rightAscension->setObjectName(QStringLiteral("observatoryStatusRightAscension"));
    m_declination = new StatusText(true, this);
    m_declination->setObjectName(QStringLiteral("observatoryStatusDeclination"));

    m_row->addWidget(m_selector, 0);
    m_row->addWidget(m_facts, 0);
    m_row->addWidget(m_zoom, 0);
    m_row->addStretch(1);
    m_row->addWidget(m_rightAscension, 0);
    m_row->addWidget(m_declination, 0);

    m_readout = new an::ui::AnIconButton(QStringLiteral("panel_right"), tr("Show or hide the readout"), this);
    m_readout->setObjectName(QStringLiteral("observatoryReadoutButton"));
    m_row->addWidget(m_readout, 0);
    m_console = new an::ui::AnChip(tr("Console"), this);
    m_console->setObjectName(QStringLiteral("observatoryConsoleChip"));
    m_console->setToolTip(tr("Open the Session Console"));
    m_row->addWidget(m_console, 0);
    clearCelestialCoordinates();
}

QHBoxLayout* StatusLine::row() const noexcept
{
    return m_row;
}

void StatusLine::showImageInfo(ImageViewControl* view)
{
    const QString facts = an::ui::imageFactsStatusText(view);
    if (facts.isEmpty())
    {
        clearImageInfo();
        return;
    }
    m_facts->setText(facts);
    showZoom(view);
}

void StatusLine::showZoom(ImageViewControl* view)
{
    const QString zoom = an::ui::zoomStatusText(view);
    if (zoom.isEmpty())
    {
        clearImageInfo();
        return;
    }
    m_zoom->setText(zoom);
}

void StatusLine::showCelestialCoordinates(const double raDegrees, const double decDegrees)
{
    m_rightAscension->setText(an::ui::rightAscensionStatusText(raDegrees));
    m_declination->setText(an::ui::declinationStatusText(decDegrees));
    m_rightAscension->setToolTip(an::ui::rightAscensionStatusToolTip(raDegrees));
    m_declination->setToolTip(an::ui::declinationStatusToolTip(decDegrees));
}

void StatusLine::clearCelestialCoordinates()
{
    m_rightAscension->setText(QStringLiteral("RA -"), StatusText::Tone::Tertiary);
    m_declination->setText(QStringLiteral("Dec -"), StatusText::Tone::Tertiary);
    m_rightAscension->setToolTip(QString());
    m_declination->setToolTip(QString());
}

void StatusLine::clearImageInfo()
{
    m_facts->setText(QString());
    m_zoom->setText(QString());
    clearCelestialCoordinates();
}

StatusText* StatusLine::factsText() const noexcept
{
    return m_facts;
}

StatusText* StatusLine::zoomText() const noexcept
{
    return m_zoom;
}

StatusText* StatusLine::rightAscensionText() const noexcept
{
    return m_rightAscension;
}

StatusText* StatusLine::declinationText() const noexcept
{
    return m_declination;
}

QComboBox* StatusLine::viewSelector() const noexcept
{
    return m_selector;
}

an::ui::AnIconButton* StatusLine::readoutButton() const noexcept
{
    return m_readout;
}

an::ui::AnChip* StatusLine::consoleChip() const noexcept
{
    return m_console;
}

QSize StatusLine::sizeHint() const
{
    return {QWidget::sizeHint().width(), an::ui::tokens().metrics.statusLine};
}

QSize StatusLine::minimumSizeHint() const
{
    return {0, an::ui::tokens().metrics.statusLine};
}

void StatusLine::paintEvent(QPaintEvent* /*event*/)
{
    const an::ui::DesignTokens& t = an::ui::tokens();
    QPainter painter(this);
    painter.fillRect(rect(), t.layers.surface1);
    // The hairline above the line, in its first row of pixels.
    painter.fillRect(QRect(0, 0, width(), 1), t.colors.borderSubtle);
}

void StatusLine::changeEvent(QEvent* event)
{
    // A change of density resizes the line: its height is the density's.
    if (event != nullptr && (event->type() == QEvent::StyleChange || event->type() == QEvent::PaletteChange))
    {
        updateGeometry();
        update();
    }
    QWidget::changeEvent(event);
}

} // namespace an::ui::observatory
