// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"

#include "components/AnStageList.h"

#include "theme/AnDesignTokens.h"
#include "theme/ComponentParts.h"
#include "theme/PainterGuard.h"

#include <QFocusEvent>
#include <QFontMetricsF>
#include <QItemSelectionModel>
#include <QPainter>
#include <QStandardItem>
#include <QStandardItemModel>
#include <QStyledItemDelegate>

#include <algorithm>
#include <cmath>

#include <verdigris/wobjectimpl.h>

W_OBJECT_IMPL(an::ui::AnStageList)

namespace
{

using an::ui::StageRow;
using an::ui::StageStatus;

/// What a row's item holds beside its title.
enum StageRole
{
    IdRole = Qt::UserRole + 1,
    LineRole,
    StatusRole,
    ProgressRole
};

QFont titleFont(const an::ui::DesignTokens& t)
{
    return an::ui::parts::sizedFont(t.typography.body, t.metrics.fontBody, QFont::DemiBold);
}

QFont lineFont(const an::ui::DesignTokens& t)
{
    return an::ui::parts::sizedFont(t.typography.monospace, t.metrics.fontCaption);
}

int lineHeight(const QFont& font)
{
    return static_cast<int>(std::ceil(QFontMetricsF(font).height()));
}

/// The state in words, as assistive technology reads it.
QString statusWords(const StageStatus status, const std::optional<int>& progress)
{
    switch (status)
    {
    case StageStatus::Running:
        return progress ? QStringLiteral("running %1 %").arg(*progress) : QStringLiteral("running");
    case StageStatus::Done:
        return QStringLiteral("done");
    case StageStatus::Warning:
        return QStringLiteral("warning");
    case StageStatus::Failed:
        return QStringLiteral("failed");
    case StageStatus::Pending:
    default:
        return QStringLiteral("to come");
    }
}

void fillItem(QStandardItem& item, const StageRow& row)
{
    item.setData(row.title, Qt::DisplayRole);
    item.setData(row.id, IdRole);
    item.setData(row.line, LineRole);
    item.setData(static_cast<int>(row.status), StatusRole);
    item.setData(row.progress ? std::clamp(*row.progress, 0, 100) : -1, ProgressRole);
    item.setData(row.title + QStringLiteral(", ") + statusWords(row.status, row.progress) + QStringLiteral(", ") +
                     row.line,
                 Qt::AccessibleTextRole);
}

StageRow rowOf(const QModelIndex& index)
{
    const int progress = index.data(ProgressRole).toInt();
    return StageRow{index.data(IdRole).toString(), index.data(Qt::DisplayRole).toString(),
                    index.data(LineRole).toString(), static_cast<StageStatus>(index.data(StatusRole).toInt()),
                    progress >= 0 ? std::optional<int>(progress) : std::nullopt};
}

/// A row's height in the tokens in force: its padding, its title and its line, the gap under it, and a progress bar
/// with its gap when it has one.
int rowHeight(const an::ui::DesignTokens& t, const bool withProgress)
{
    using namespace an::ui::parts;
    return 2 * kStageRowPaddingVertical + lineHeight(titleFont(t)) + lineHeight(lineFont(t)) + kStageRowSpacing +
           (withProgress ? kStageProgressGap + kStageProgress : 0);
}

/// A row's width with its title and its line whole.
int naturalWidth(const an::ui::DesignTokens& t, const QModelIndex& index)
{
    using namespace an::ui::parts;
    const int text = (std::max)(textWidth(titleFont(t), index.data(Qt::DisplayRole).toString()),
                                textWidth(lineFont(t), index.data(LineRole).toString()));
    return 2 * kStageRowPaddingHorizontal + kStageMarkBox + kStageRowGap + text;
}

/// Paints a row of a stage list: what the list's class comment says, from the tokens in force.
class StageDelegate final : public QStyledItemDelegate
{
public:
    explicit StageDelegate(an::ui::AnStageList* list) : QStyledItemDelegate(list), m_list(list) {}

    [[nodiscard]] QSize sizeHint(const QStyleOptionViewItem& /*option*/, const QModelIndex& index) const override
    {
        const an::ui::DesignTokens& t = an::ui::tokens();
        // A row spans the list's width, so its fill and its line's room reach the list's edge.
        const int width = m_list->viewport()->width() > 0 ? m_list->viewport()->width() : naturalWidth(t, index);
        return QSize(width, rowHeight(t, index.data(ProgressRole).toInt() >= 0));
    }

    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        using namespace an::ui::parts;
        const an::ui::DesignTokens& t = an::ui::tokens();
        const an::ui::PainterGuard guard(painter);
        painter->setRenderHint(QPainter::Antialiasing, true);
        const StageRow row = rowOf(index);
        const QRectF bounds = QRectF(option.rect).adjusted(0, 0, 0, -kStageRowSpacing);
        const bool selected = option.state & QStyle::State_Selected;
        const qreal radius = t.radius.md;
        if (selected)
        {
            painter->setPen(Qt::NoPen);
            painter->setBrush(t.colors.accentContainer);
            painter->drawRoundedRect(bounds, radius, radius);
        }
        paintStateLayer(*painter, bounds, radius, (option.state & QStyle::State_MouseOver) && !selected, false);
        if ((option.state & QStyle::State_HasFocus) && m_list->hasKeyboardFocus())
        {
            paintFocusRing(*painter, bounds, radius);
        }

        const QPointF centre(bounds.left() + kStageRowPaddingHorizontal + kStageMarkBox / 2.0,
                             bounds.top() + kStageRowPaddingVertical + kStageMarkBox / 2.0);
        if (!selected)
        {
            // The line that joins the marks, under them, through the row and the gap after it.
            painter->setPen(QPen(t.colors.borderSubtle, kStageConnector));
            if (index.row() > 0)
            {
                painter->drawLine(QPointF(centre.x(), option.rect.top()), centre);
            }
            if (index.row() + 1 < index.model()->rowCount())
            {
                painter->drawLine(centre, QPointF(centre.x(), option.rect.bottom() + 1.0));
            }
        }
        paintMark(*painter, centre, row.status, t);

        const qreal left = bounds.left() + kStageRowPaddingHorizontal + kStageMarkBox + kStageRowGap;
        const qreal right = bounds.right() - kStageRowPaddingHorizontal;
        const QFont title = titleFont(t);
        const QFont line = lineFont(t);
        const qreal titleTop = bounds.top() + kStageRowPaddingVertical;
        const qreal lineTop = titleTop + lineHeight(title);
        painter->setPen(selected ? t.colors.textOnAccentContainer : t.colors.textPrimary);
        painter->setFont(title);
        painter->drawText(QRectF(left, titleTop, right - left, lineHeight(title)), Qt::AlignLeft | Qt::AlignVCenter,
                          QFontMetricsF(title).elidedText(row.title, Qt::ElideRight, right - left));
        painter->setPen(selected ? t.colors.textOnAccentContainer : t.colors.textSecondary);
        painter->setFont(line);
        painter->drawText(QRectF(left, lineTop, right - left, lineHeight(line)), Qt::AlignLeft | Qt::AlignVCenter,
                          QFontMetricsF(line).elidedText(row.line, Qt::ElideRight, right - left));

        if (row.progress)
        {
            const QRectF track(left, bounds.bottom() - kStageRowPaddingVertical - kStageProgress, right - left,
                               kStageProgress);
            painter->setPen(Qt::NoPen);
            painter->setBrush(t.colors.accentTrack);
            painter->drawRoundedRect(track, kStageProgress / 2.0, kStageProgress / 2.0);
            QRectF done = track;
            done.setWidth(track.width() * (*row.progress) / 100.0);
            painter->setBrush(t.colors.accentPrimary);
            painter->drawRoundedRect(done, kStageProgress / 2.0, kStageProgress / 2.0);
        }
    }

private:
    static void paintMark(QPainter& painter, const QPointF& centre, const StageStatus status,
                          const an::ui::DesignTokens& t)
    {
        using namespace an::ui::parts;
        const qreal r = kStageMark / 2.0;
        const QRectF disc(centre.x() - r, centre.y() - r, kStageMark, kStageMark);
        const QRectF glyph(centre.x() - kStageGlyph / 2.0, centre.y() - kStageGlyph / 2.0, kStageGlyph, kStageGlyph);
        const QColor glyphColour = t.layers.surface1;
        painter.setPen(Qt::NoPen);
        switch (status)
        {
        case StageStatus::Pending: {
            // A ring over the first surface, which hides the joining line inside it.
            const qreal half = kStageMarkBorder / 2.0;
            painter.setBrush(t.layers.surface1);
            painter.setPen(QPen(t.colors.textTertiary, kStageMarkBorder));
            painter.drawEllipse(disc.adjusted(half, half, -half, -half));
            break;
        }
        case StageStatus::Running:
            painter.setBrush(t.colors.accentPrimary);
            painter.drawEllipse(disc);
            painter.setBrush(t.colors.textOnAccent);
            painter.drawEllipse(centre, kStageMarkBorder, kStageMarkBorder);
            break;
        case StageStatus::Done:
            painter.setBrush(t.colors.statusSuccess);
            painter.drawEllipse(disc);
            paintCheckMark(painter, glyph, glyphColour, kStageMarkBorder);
            break;
        case StageStatus::Warning:
            painter.setBrush(t.colors.statusWarning);
            painter.drawEllipse(disc);
            paintExclamationMark(painter, glyph, glyphColour, kStageMarkBorder);
            break;
        case StageStatus::Failed:
            painter.setBrush(t.colors.statusError);
            painter.drawEllipse(disc);
            paintCrossMark(painter, glyph, glyphColour, kStageMarkBorder);
            break;
        }
    }

    an::ui::AnStageList* m_list;
};

} // namespace

namespace an::ui
{

AnStageList::AnStageList(QWidget* parent) : QListView(parent), m_model(new QStandardItemModel(this))
{
    setModel(m_model);
    setItemDelegate(new StageDelegate(this));
    setSelectionMode(QAbstractItemView::SingleSelection);
    setSelectionBehavior(QAbstractItemView::SelectRows);
    setEditTriggers(QAbstractItemView::NoEditTriggers);
    setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setResizeMode(QListView::Adjust);
    setUniformItemSizes(false);
    setFrameShape(QFrame::NoFrame);
    setMouseTracking(true);
    viewport()->setAttribute(Qt::WA_Hover, true);
    viewport()->setAutoFillBackground(false);
    connect(selectionModel(), &QItemSelectionModel::selectionChanged, this, [this] { onSelectionChanged(); });
}

AnStageList::~AnStageList() = default;

void AnStageList::setRows(const std::vector<StageRow>& rows)
{
    const QString kept = m_selectedId;
    {
        const QSignalBlocker blocker(selectionModel());
        m_model->clear();
        for (const StageRow& row : rows)
        {
            auto* const item = new QStandardItem;
            item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
            fillItem(*item, row);
            m_model->appendRow(item);
        }
    }
    // The stage selected before stays selected when a row still has its id; only a selection lost is told.
    m_settingRows = true;
    setSelectedId(kept);
    m_settingRows = false;
    const QModelIndexList selected = selectionModel()->selectedIndexes();
    const QString id = selected.isEmpty() ? QString() : selected.front().data(IdRole).toString();
    if (id != m_selectedId)
    {
        m_selectedId = id;
        selectedStageChanged(id);
    }
    updateGeometry();
}

void AnStageList::updateRow(const StageRow& row)
{
    for (int index = 0; index < m_model->rowCount(); ++index)
    {
        QStandardItem* const item = m_model->item(index);
        if (item->data(IdRole).toString() == row.id)
        {
            fillItem(*item, row);
            // A row that gains or loses its bar changes height.
            scheduleDelayedItemsLayout();
            updateGeometry();
            return;
        }
    }
}

std::vector<StageRow> AnStageList::rows() const
{
    std::vector<StageRow> rows;
    rows.reserve(static_cast<std::size_t>(m_model->rowCount()));
    for (int index = 0; index < m_model->rowCount(); ++index)
    {
        rows.push_back(rowOf(m_model->index(index, 0)));
    }
    return rows;
}

int AnStageList::rowCount() const
{
    return m_model->rowCount();
}

void AnStageList::setSelectedId(const QString& id)
{
    for (int index = 0; index < m_model->rowCount(); ++index)
    {
        if (!id.isEmpty() && m_model->item(index)->data(IdRole).toString() == id)
        {
            setCurrentIndex(m_model->index(index, 0));
            return;
        }
    }
    clearSelection();
    setCurrentIndex(QModelIndex());
}

QString AnStageList::selectedId() const
{
    return m_selectedId;
}

QSize AnStageList::sizeHint() const
{
    const DesignTokens& t = tokens();
    int width = 0;
    int height = 0;
    for (int index = 0; index < m_model->rowCount(); ++index)
    {
        const QModelIndex at = m_model->index(index, 0);
        width = (std::max)(width, naturalWidth(t, at));
        height += rowHeight(t, at.data(ProgressRole).toInt() >= 0);
    }
    const int frame = 2 * frameWidth();
    return QSize(width + frame, height + frame);
}

bool AnStageList::hasKeyboardFocus() const noexcept
{
    return m_keyboardFocus;
}

void AnStageList::focusInEvent(QFocusEvent* event)
{
    m_keyboardFocus = parts::isKeyboardFocusReason(event->reason());
    QListView::focusInEvent(event);
}

void AnStageList::focusOutEvent(QFocusEvent* event)
{
    m_keyboardFocus = false;
    QListView::focusOutEvent(event);
}

void AnStageList::onSelectionChanged()
{
    if (m_settingRows)
    {
        return;
    }
    const QModelIndexList selected = selectionModel()->selectedIndexes();
    const QString id = selected.isEmpty() ? QString() : selected.front().data(IdRole).toString();
    if (id == m_selectedId)
    {
        return;
    }
    m_selectedId = id;
    selectedStageChanged(id);
}

} // namespace an::ui
