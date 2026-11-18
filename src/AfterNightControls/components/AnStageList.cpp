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
using an::ui::StageStep;

/// What a row's item holds beside its title.
enum StageRole
{
    IdRole = Qt::UserRole + 1,
    LineRole,
    StatusRole,
    ProgressRole,
    StepNamesRole,
    StepStatusesRole
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
    case StageStatus::Ready:
        return QStringLiteral("ready");
    case StageStatus::OutOfDate:
        return QStringLiteral("out of date");
    case StageStatus::Pending:
    default:
        return QStringLiteral("to come");
    }
}

/// The part a stage is at among its parts, in words: "step 2 of 3: Match"; empty when it says none.
QString stepWords(const std::vector<StageStep>& steps)
{
    for (std::size_t at = 0; at < steps.size(); ++at)
    {
        if (steps[at].status == StageStatus::Running)
        {
            return QStringLiteral("step %1 of %2: %3").arg(at + 1).arg(steps.size()).arg(steps[at].name);
        }
    }
    return {};
}

void fillItem(QStandardItem& item, const StageRow& row)
{
    item.setData(row.title, Qt::DisplayRole);
    item.setData(row.id, IdRole);
    item.setData(row.line, LineRole);
    item.setData(static_cast<int>(row.status), StatusRole);
    item.setData(row.progress ? std::clamp(*row.progress, 0, 100) : -1, ProgressRole);
    item.setData(row.toolTip.isEmpty() ? QVariant() : QVariant(row.toolTip), Qt::ToolTipRole);
    QStringList names;
    QVariantList statuses;
    for (const StageStep& step : row.steps)
    {
        names.append(step.name);
        statuses.append(static_cast<int>(step.status));
    }
    item.setData(names, StepNamesRole);
    item.setData(statuses, StepStatusesRole);
    QString said =
        row.title + QStringLiteral(", ") + statusWords(row.status, row.progress) + QStringLiteral(", ") + row.line;
    if (const QString at = stepWords(row.steps); !at.isEmpty())
    {
        said += QStringLiteral(", ") + at;
    }
    item.setData(said, Qt::AccessibleTextRole);
}

std::vector<StageStep> stepsOf(const QModelIndex& index)
{
    const QStringList names = index.data(StepNamesRole).toStringList();
    const QVariantList statuses = index.data(StepStatusesRole).toList();
    std::vector<StageStep> steps;
    steps.reserve(static_cast<std::size_t>(names.size()));
    for (qsizetype at = 0; at < names.size(); ++at)
    {
        steps.push_back(StageStep{names[at], at < statuses.size() ? static_cast<StageStatus>(statuses[at].toInt())
                                                                  : StageStatus::Pending});
    }
    return steps;
}

StageRow rowOf(const QModelIndex& index)
{
    const int progress = index.data(ProgressRole).toInt();
    return StageRow{index.data(IdRole).toString(),
                    index.data(Qt::DisplayRole).toString(),
                    index.data(LineRole).toString(),
                    static_cast<StageStatus>(index.data(StatusRole).toInt()),
                    progress >= 0 ? std::optional<int>(progress) : std::nullopt,
                    index.data(Qt::ToolTipRole).toString(),
                    stepsOf(index)};
}

/// A row's height in the tokens in force: its padding, its title and its line, a line for each of its parts, the gap
/// under it, and a progress bar with its gap when it has one.
int rowHeight(const an::ui::DesignTokens& t, const QModelIndex& index)
{
    using namespace an::ui::parts;
    const int steps = static_cast<int>(index.data(StepNamesRole).toStringList().size());
    const bool withProgress = index.data(ProgressRole).toInt() >= 0;
    return 2 * kStageRowPaddingVertical + lineHeight(titleFont(t)) + lineHeight(lineFont(t)) +
           steps * (kStageStepGap + lineHeight(lineFont(t))) + kStageRowSpacing +
           (withProgress ? kStageProgressGap + kStageProgress : 0);
}

/// A row's width with its title, its line and its parts whole.
int naturalWidth(const an::ui::DesignTokens& t, const QModelIndex& index)
{
    using namespace an::ui::parts;
    int text = (std::max)(textWidth(titleFont(t), index.data(Qt::DisplayRole).toString()),
                          textWidth(lineFont(t), index.data(LineRole).toString()));
    for (const QString& step : index.data(StepNamesRole).toStringList())
    {
        text = (std::max)(text, kStageStepMark + kStageStepTextGap + textWidth(lineFont(t), step));
    }
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
        return QSize(width, rowHeight(t, index));
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

        // The stage's parts, under its line: a small mark, then the part's name.
        qreal stepTop = lineTop + lineHeight(line);
        for (const StageStep& step : row.steps)
        {
            stepTop += kStageStepGap;
            paintStepMark(*painter, QPointF(left + kStageStepMark / 2.0, stepTop + lineHeight(line) / 2.0), step.status,
                          selected, t);
            const qreal textLeft = left + kStageStepMark + kStageStepTextGap;
            const bool at = step.status == StageStatus::Running;
            painter->setPen(selected ? t.colors.textOnAccentContainer
                                     : (at ? t.colors.textPrimary : t.colors.textSecondary));
            painter->setFont(line);
            painter->drawText(QRectF(textLeft, stepTop, right - textLeft, lineHeight(line)),
                              Qt::AlignLeft | Qt::AlignVCenter,
                              QFontMetricsF(line).elidedText(step.name, Qt::ElideRight, right - textLeft));
            stepTop += lineHeight(line);
        }

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
    /// The mark of a part of a stage: a check for one done, the accent's dot for the one the stage is at, a ring for
    /// one to come.
    static void paintStepMark(QPainter& painter, const QPointF& centre, const StageStatus status, const bool selected,
                              const an::ui::DesignTokens& t)
    {
        using namespace an::ui::parts;
        const qreal r = kStageStepMark / 2.0;
        const QRectF box(centre.x() - r, centre.y() - r, kStageStepMark, kStageStepMark);
        painter.setBrush(Qt::NoBrush);
        if (status == StageStatus::Done)
        {
            paintCheckMark(painter, box, selected ? t.colors.textOnAccentContainer : t.colors.statusSuccess,
                           kStageMarkBorder);
        }
        else if (status == StageStatus::Running)
        {
            painter.setPen(Qt::NoPen);
            painter.setBrush(selected ? t.colors.textOnAccentContainer : t.colors.accentPrimary);
            painter.drawEllipse(box);
        }
        else
        {
            const qreal half = kStageMarkBorder / 2.0;
            painter.setPen(QPen(selected ? t.colors.textOnAccentContainer : t.colors.textTertiary, kStageMarkBorder));
            painter.drawEllipse(box.adjusted(half, half, -half, -half));
        }
        painter.setPen(Qt::NoPen);
        painter.setBrush(Qt::NoBrush);
    }

    static void paintMark(QPainter& painter, const QPointF& centre, const StageStatus status,
                          const an::ui::DesignTokens& t)
    {
        using namespace an::ui::parts;
        const qreal r = kStageMark / 2.0;
        const QRectF disc(centre.x() - r, centre.y() - r, kStageMark, kStageMark);
        const QRectF glyph(centre.x() - kStageGlyph / 2.0, centre.y() - kStageGlyph / 2.0, kStageGlyph, kStageGlyph);
        const QColor glyphColour = t.layers.surface1;
        painter.setPen(Qt::NoPen);
        // A ring over the first surface, which hides the joining line inside it.
        const auto ring = [&painter, &disc, &t](const QColor& colour) {
            const qreal half = kStageMarkBorder / 2.0;
            painter.setBrush(t.layers.surface1);
            painter.setPen(QPen(colour, kStageMarkBorder));
            painter.drawEllipse(disc.adjusted(half, half, -half, -half));
            painter.setPen(Qt::NoPen);
        };
        switch (status)
        {
        case StageStatus::Pending:
            ring(t.colors.textTertiary);
            break;
        case StageStatus::Ready:
            // Hollow, with the run's dot: it waits to run.
            ring(t.colors.accentPrimary);
            painter.setBrush(t.colors.accentPrimary);
            painter.drawEllipse(centre, kStageMarkBorder, kStageMarkBorder);
            break;
        case StageStatus::OutOfDate:
            // Hollow, with the check of a stage that was done.
            ring(t.colors.statusWarning);
            paintCheckMark(painter, glyph.adjusted(1.0, 1.0, -1.0, -1.0), t.colors.statusWarning, kStageMarkBorder);
            break;
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
        height += rowHeight(t, at);
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
