// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "components/AnImageFactsCard.h"

#include "components/AnIconButton.h"
#include "theme/AnDesignTokens.h"
#include "theme/ComponentParts.h"

#include <QFontMetricsF>
#include <QPainter>
#include <QPaintEvent>
#include <QSizePolicy>
#include <QStringList>
#include <QVBoxLayout>

#include <verdigris/wobjectimpl.h>

#include <algorithm>
#include <utility>
#include <vector>

W_OBJECT_IMPL(an::ui::AnImageFactsCard)

namespace an::ui
{

/// Keys and values, a key-value row each: the key in the small text's secondary colour at the left, the value in the
/// primary text at the right, a figure in the monospace.
class KeyValueRows final : public QWidget
{
public:
    struct Row
    {
        QString key;
        QString value;
        bool figure = false;
    };

    explicit KeyValueRows(QWidget* parent) : QWidget(parent)
    {
        setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    }

    void setRows(std::vector<Row> rows)
    {
        m_rows = std::move(rows);
        QStringList described;
        for (const Row& row : m_rows)
        {
            described.append(row.key + QStringLiteral(" ") + row.value);
        }
        setAccessibleDescription(described.join(QStringLiteral(", ")));
        updateGeometry();
        update();
    }

    [[nodiscard]] const std::vector<Row>& rows() const noexcept
    {
        return m_rows;
    }

    [[nodiscard]] QSize sizeHint() const override
    {
        const int row = tokens().metrics.rowKeyValue;
        return QSize(row * 8, row * static_cast<int>(m_rows.size()));
    }

    [[nodiscard]] QSize minimumSizeHint() const override
    {
        return QSize(0, sizeHint().height());
    }

protected:
    void paintEvent(QPaintEvent* /*event*/) override
    {
        const DesignTokens& t = tokens();
        QPainter painter(this);
        const int height = t.metrics.rowKeyValue;
        const QFont key = parts::sizedFont(t.typography.body, t.metrics.fontSmall);
        const QFont plain = parts::sizedFont(t.typography.body, t.metrics.fontSmall);
        const QFont figure = parts::sizedFont(t.typography.monospace, t.metrics.fontSmall);
        for (std::size_t index = 0u; index < m_rows.size(); ++index)
        {
            const Row& row = m_rows[index];
            const QRect line(0, static_cast<int>(index) * height, width(), height);
            const int keyWidth = parts::textWidth(key, row.key);
            painter.setFont(key);
            painter.setPen(t.colors.textSecondary);
            painter.drawText(QRect(line.left(), line.top(), keyWidth, height), Qt::AlignLeft | Qt::AlignVCenter,
                             row.key);
            const QFont& valueFont = row.figure ? figure : plain;
            const int room = (std::max)(0, line.width() - keyWidth - t.metrics.gap);
            painter.setFont(valueFont);
            painter.setPen(t.colors.textPrimary);
            painter.drawText(QRect(line.right() + 1 - room, line.top(), room, height),
                             Qt::AlignRight | Qt::AlignVCenter,
                             QFontMetricsF(valueFont).elidedText(row.value, Qt::ElideMiddle, room));
        }
    }

private:
    std::vector<Row> m_rows;
};

AnImageFactsCard::AnImageFactsCard(QWidget* parent) : AnReadoutCard(tr("Image"), parent)
{
    m_fitsHeader = addHeaderTool(QStringLiteral("header"), tr("FITS Header (Ctrl+Shift+H)"));
    m_exif = addHeaderTool(QStringLiteral("camera"), tr("EXIF Viewer (Ctrl+Shift+X)"));
    m_summary = addHeaderTool(QStringLiteral("summary"), tr("Summary (Ctrl+Shift+M)"));
    m_rows = new KeyValueRows(body());
    bodyLayout()->addWidget(m_rows);
    refill();

    QObject::connect(m_fitsHeader, &QAbstractButton::clicked, this, [this]() { fitsHeaderRequested(); });
    QObject::connect(m_exif, &QAbstractButton::clicked, this, [this]() { exifRequested(); });
    QObject::connect(m_summary, &QAbstractButton::clicked, this, [this]() { summaryRequested(); });
    QObject::connect(this, &AnReadoutCard::foldedChanged, this, [this](bool) { refill(); });
}

AnImageFactsCard::~AnImageFactsCard() = default;

void AnImageFactsCard::setFacts(const ImageFactsReadout& facts)
{
    if (m_facts == facts)
    {
        return;
    }
    m_facts = facts;
    refill();
}

const ImageFactsReadout& AnImageFactsCard::facts() const noexcept
{
    return m_facts;
}

QString AnImageFactsCard::value(const QString& key) const
{
    for (const KeyValueRows::Row& row : m_rows->rows())
    {
        if (row.key == key)
        {
            return row.value;
        }
    }
    return QString();
}

QWidget* AnImageFactsCard::rows() const noexcept
{
    return m_rows;
}

AnIconButton* AnImageFactsCard::fitsHeaderButton() const noexcept
{
    return m_fitsHeader;
}

AnIconButton* AnImageFactsCard::exifButton() const noexcept
{
    return m_exif;
}

AnIconButton* AnImageFactsCard::summaryButton() const noexcept
{
    return m_summary;
}

void AnImageFactsCard::refill()
{
    const auto shown = [](const QString& text) { return text.isEmpty() ? QStringLiteral("-") : text; };
    m_rows->setRows({{tr("Dimensions"), shown(m_facts.dimensions), true},
                     {tr("Format"), shown(m_facts.format), false},
                     {tr("Size in memory"), shown(m_facts.sizeInMemory), true},
                     {tr("Channels"), shown(m_facts.channels), false}});
    setSummary(isFolded() ? m_facts.summary : QString());
}

} // namespace an::ui
