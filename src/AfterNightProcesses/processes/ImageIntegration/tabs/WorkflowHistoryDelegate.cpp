// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "WorkflowHistoryDelegate.h"
#include "WorkflowHistoryModel.h"
#include "RunHistoryThumbnailCache.h"
#include "theme/AnDesignTokens.h"

#include <verdigris/wobjectimpl.h>

#include <QApplication>
#include <QPainter>
#include <QMouseEvent>
#include <QHoverEvent>
#include <QHelpEvent>
#include <QDateTime>
#include <QAbstractItemView>
#include <QToolTip>

W_OBJECT_IMPL(WorkflowHistoryDelegate)

namespace
{
int historyPadding()
{
    return an::ui::tokens().spacing.sm;
}

int thumbnailSize()
{
    return an::ui::tokens().spacing.xl * 2;
}

int historyButtonWidth()
{
    return an::ui::tokens().spacing.xl * 3;
}

int historyButtonHeight()
{
    return an::ui::tokens().spacing.xl;
}

int workflowRowHeight()
{
    return an::ui::tokens().spacing.xl * 2;
}

int groupRowHeight()
{
    return thumbnailSize() + historyPadding() * 2;
}

[[nodiscard]] QRect aspectFitRect(const QSize& sourceSize, const QRect& bounds)
{
    if (!sourceSize.isValid() || sourceSize.isEmpty() || bounds.isEmpty())
    {
        return {};
    }

    const QSize fittedSize = sourceSize.scaled(bounds.size(), Qt::KeepAspectRatio);
    return QRect(QPoint(bounds.left() + (bounds.width() - fittedSize.width()) / 2,
                        bounds.top() + (bounds.height() - fittedSize.height()) / 2),
                 fittedSize);
}

enum class HistoryButtonId : int
{
    None = 0,
    OpenAll,
    Report,
    SaveReport,
    Log,
    Rerun,
    DeleteArchive,
    OpenMaster,
    CompareMaster
};

struct ButtonInfo
{
    HistoryButtonId id = HistoryButtonId::None;
    QRect rect;
    QString text;
    QString tooltip;
    bool enabled = true;
};

struct ButtonVisualStyle
{
    QColor fill;
    QColor border;
    QColor text;
};

struct RowTextColors
{
    QColor primary;
    QColor secondary;
};

[[nodiscard]] QColor selectedTextColorForBackground(const QColor& background, const QColor& preferred)
{
    const auto& tokenSet = an::ui::tokens();
    return an::ui::bestContrastTextColor(background, preferred, tokenSet.colors.textOnAccent, QColor(0, 0, 0), 4.5);
}

[[nodiscard]] QColor effectiveSelectionBackground(const QStyleOptionViewItem& option)
{
    const QColor selectionFill = option.palette.color(QPalette::Highlight);
    const QColor baseColor = option.palette.color(QPalette::Base).isValid() ? option.palette.color(QPalette::Base)
                                                                            : option.palette.color(QPalette::Window);
    return an::ui::compositeOver(selectionFill, baseColor);
}

[[nodiscard]] RowTextColors resolvedRowTextColors(const QStyleOptionViewItem& option)
{
    if (!(option.state & QStyle::State_Selected))
    {
        const QColor primary = option.palette.color(QPalette::Text);
        QColor secondary = option.palette.color(QPalette::PlaceholderText);
        if (!secondary.isValid())
        {
            secondary = primary.lighter(150);
        }
        return {primary, secondary};
    }

    const auto& tokenSet = an::ui::tokens();
    const QColor selectionBg = effectiveSelectionBackground(option);
    return {selectedTextColorForBackground(selectionBg, tokenSet.colors.textOnAccent),
            selectedTextColorForBackground(selectionBg, tokenSet.colors.textSecondary)};
}

[[nodiscard]] QColor rowBackground(const QStyleOptionViewItem& option)
{
    if (option.state & QStyle::State_Selected)
    {
        return effectiveSelectionBackground(option);
    }
    return option.palette.color(QPalette::Base).isValid() ? option.palette.color(QPalette::Base)
                                                          : option.palette.color(QPalette::Window);
}

[[nodiscard]] ButtonVisualStyle resolvedButtonVisualStyle(const QStyleOptionViewItem& option, const bool hovered,
                                                          const bool pressed, const bool enabled)
{
    const auto& tokenSet = an::ui::tokens();
    if (!enabled)
    {
        return {tokenSet.colors.bgSurface, tokenSet.colors.borderSubtle, tokenSet.colors.textTertiary};
    }

    const bool dark = an::ui::isDarkTheme();
    const bool selected = option.state & QStyle::State_Selected;

    const QColor rowBg = rowBackground(option);

    QColor neutralOverlay = tokenSet.colors.bgInput;
    neutralOverlay.setAlpha(selected ? (dark ? 230 : 240) : 255);
    const QColor baseFill = selected ? an::ui::compositeOver(neutralOverlay, rowBg) : tokenSet.colors.bgInput;

    QColor hoverOverlay = tokenSet.colors.accentPrimary;
    hoverOverlay.setAlpha(dark ? 86 : 72);
    QColor pressedOverlay = tokenSet.colors.accentPrimary;
    pressedOverlay.setAlpha(dark ? 132 : 114);

    const QColor hoverFill = an::ui::compositeOver(hoverOverlay, baseFill);
    const QColor pressedFill = an::ui::compositeOver(pressedOverlay, baseFill);

    QColor fill = baseFill;
    if (pressed)
    {
        fill = pressedFill;
    }
    else if (hovered)
    {
        fill = hoverFill;
    }

    QColor border = tokenSet.colors.borderPrimary;
    if (hovered || pressed)
    {
        border = tokenSet.colors.borderFocus;
    }
    else if (selected)
    {
        QColor subtleBorder = tokenSet.colors.borderPrimary;
        subtleBorder.setAlpha(dark ? 190 : 176);
        border = an::ui::compositeOver(subtleBorder, rowBg);
    }

    const QColor preferredText = (hovered || pressed) ? tokenSet.colors.textOnAccent : tokenSet.colors.textPrimary;
    const QColor text = selectedTextColorForBackground(fill, preferredText);
    return {fill, border, text};
}

void paintButton(QPainter* painter, const QStyleOptionViewItem& option, const ButtonInfo& button, const bool hovered,
                 const bool pressed)
{
    const auto& tokenSet = an::ui::tokens();
    const ButtonVisualStyle style = resolvedButtonVisualStyle(option, hovered, pressed, button.enabled);

    painter->setPen(QPen(style.border, 1));
    painter->setBrush(style.fill);
    painter->drawRoundedRect(button.rect, tokenSet.radius.sm, tokenSet.radius.sm);
    painter->setPen(style.text);
    painter->drawText(button.rect, Qt::AlignCenter, button.text);
}

[[nodiscard]] QRect buttonRect(const QRect& itemRect, int rightOffset)
{
    const int buttonWidth = historyButtonWidth();
    const int buttonHeight = historyButtonHeight();
    return QRect(itemRect.right() - rightOffset - buttonWidth, itemRect.top() + (itemRect.height() - buttonHeight) / 2,
                 buttonWidth, buttonHeight);
}

// Returns the visible workflow-header buttons in draw order (left-to-right from the right edge).
[[nodiscard]] std::vector<ButtonInfo> workflowButtons(const QModelIndex& index, const QRect& itemRect)
{
    std::vector<ButtonInfo> buttons;
    int offset = historyPadding();

    buttons.push_back({HistoryButtonId::OpenAll, buttonRect(itemRect, offset), WorkflowHistoryDelegate::tr("Open All"),
                       WorkflowHistoryDelegate::tr("Open all stacked master files from this run")});
    offset += historyButtonWidth() + historyPadding();

    const bool hasStoredReport = index.data(WorkflowHistoryModel::StoredReportRole).toBool();
    if (hasStoredReport || !index.data(WorkflowHistoryModel::ReportPathRole).toString().isEmpty())
    {
        buttons.push_back({HistoryButtonId::Report, buttonRect(itemRect, offset), WorkflowHistoryDelegate::tr("Report"),
                           WorkflowHistoryDelegate::tr("View the detailed workflow report")});
        offset += historyButtonWidth() + historyPadding();
    }

    // The single file is made of the report the archive keeps, so a run that keeps none is not offered it.
    if (hasStoredReport)
    {
        buttons.push_back(
            {HistoryButtonId::SaveReport, buttonRect(itemRect, offset), WorkflowHistoryDelegate::tr("Save As..."),
             WorkflowHistoryDelegate::tr("Save Report As One File: the report with its pictures inside it, in one "
                                         "file that opens on its own")});
        offset += historyButtonWidth() + historyPadding();
    }

    if (!index.data(WorkflowHistoryModel::LogPathRole).toString().isEmpty())
    {
        buttons.push_back({HistoryButtonId::Log, buttonRect(itemRect, offset), WorkflowHistoryDelegate::tr("Log"),
                           WorkflowHistoryDelegate::tr("View the execution log")});
        offset += historyButtonWidth() + historyPadding();
    }

    if (!index.data(WorkflowHistoryModel::ManifestPathRole).toString().isEmpty())
    {
        buttons.push_back({HistoryButtonId::Rerun, buttonRect(itemRect, offset), WorkflowHistoryDelegate::tr("Re-run"),
                           WorkflowHistoryDelegate::tr("Re-run this workflow with the same settings")});
        offset += historyButtonWidth() + historyPadding();
    }

    if (!index.data(WorkflowHistoryModel::RunDirPathRole).toString().isEmpty())
    {
        buttons.push_back({HistoryButtonId::DeleteArchive, buttonRect(itemRect, offset),
                           WorkflowHistoryDelegate::tr("Delete"),
                           WorkflowHistoryDelegate::tr("Delete this workflow archive from disk")});
    }

    return buttons;
}

[[nodiscard]] std::vector<ButtonInfo> groupButtons(const QModelIndex& index, const QRect& itemRect)
{
    std::vector<ButtonInfo> buttons;
    int offset = historyPadding();

    if (!index.data(WorkflowHistoryModel::OutputPathRole).toString().isEmpty())
    {
        buttons.push_back({HistoryButtonId::OpenMaster, buttonRect(itemRect, offset),
                           WorkflowHistoryDelegate::tr("Open"),
                           WorkflowHistoryDelegate::tr("Open this stacked master file")});

        if (index.data(WorkflowHistoryModel::StatusRole).toString() == QStringLiteral("success"))
        {
            const bool canCompare = index.data(WorkflowHistoryModel::CanCompareMasterRole).toBool();
            offset += historyButtonWidth() + historyPadding();
            buttons.push_back(
                {HistoryButtonId::CompareMaster, buttonRect(itemRect, offset), WorkflowHistoryDelegate::tr("Compare"),
                 canCompare ? WorkflowHistoryDelegate::tr("Compare this master with matching masters from other runs")
                            : WorkflowHistoryDelegate::tr("No matching stacked masters were found for this group"),
                 canCompare});
        }
    }

    return buttons;
}

[[nodiscard]] std::vector<ButtonInfo> buttonsForIndex(const QModelIndex& index, const QRect& itemRect)
{
    return index.data(WorkflowHistoryModel::IsGroupItemRole).toBool() ? groupButtons(index, itemRect)
                                                                      : workflowButtons(index, itemRect);
}

[[nodiscard]] HistoryButtonId buttonAtPosition(const std::vector<ButtonInfo>& buttons, const QPoint& point,
                                               const bool includeDisabled = false)
{
    for (const auto& button : buttons)
    {
        if (button.rect.contains(point) && (button.enabled || includeDisabled))
        {
            return button.id;
        }
    }
    return HistoryButtonId::None;
}

[[nodiscard]] HistoryButtonId hoveredButtonFromCursor(const QModelIndex& index, const QStyleOptionViewItem& option)
{
    if (!option.widget)
    {
        return HistoryButtonId::None;
    }

    const QPoint localPos = option.widget->mapFromGlobal(QCursor::pos());
    if (!option.rect.contains(localPos))
    {
        return HistoryButtonId::None;
    }

    return buttonAtPosition(buttonsForIndex(index, option.rect), localPos);
}
} // namespace

WorkflowHistoryDelegate::WorkflowHistoryDelegate(QObject* parent) : QStyledItemDelegate(parent) {}

void WorkflowHistoryDelegate::setThumbnailCache(RunHistoryThumbnailCache* cache)
{
    m_thumbnailCache = cache;
}

void WorkflowHistoryDelegate::setView(QAbstractItemView* view)
{
    if (m_viewport)
    {
        m_viewport->removeEventFilter(this);
    }

    m_view = view;
    m_viewport = (m_view ? m_view->viewport() : nullptr);

    m_hoveredIndex = QPersistentModelIndex{};
    m_pressedIndex = QPersistentModelIndex{};
    m_hoveredButtonId = static_cast<int>(HistoryButtonId::None);
    m_pressedButtonId = static_cast<int>(HistoryButtonId::None);
    m_isButtonPressActive = false;

    if (m_viewport)
    {
        m_viewport->installEventFilter(this);
        m_viewport->setMouseTracking(true);
        m_viewport->setAttribute(Qt::WA_Hover, true);
    }
}

QSize WorkflowHistoryDelegate::sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    bool isGroup = index.data(WorkflowHistoryModel::IsGroupItemRole).toBool();
    if (isGroup)
    {
        return QSize(option.rect.width(), groupRowHeight());
    }
    else
    {
        return QSize(option.rect.width(), workflowRowHeight());
    }
}

void WorkflowHistoryDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option,
                                    const QModelIndex& index) const
{
    QStyleOptionViewItem opt = option;
    initStyleOption(&opt, index);

    // Background
    if (opt.state & QStyle::State_Selected)
    {
        painter->fillRect(opt.rect, opt.palette.highlight());
    }
    else if (opt.state & QStyle::State_MouseOver)
    {
        painter->fillRect(opt.rect, opt.palette.alternateBase());
    }

    bool isGroup = index.data(WorkflowHistoryModel::IsGroupItemRole).toBool();
    if (isGroup)
    {
        paintGroupItem(painter, opt, index);
    }
    else
    {
        paintWorkflowHeader(painter, opt, index);
    }
}

void WorkflowHistoryDelegate::paintWorkflowHeader(QPainter* painter, const QStyleOptionViewItem& option,
                                                  const QModelIndex& index) const
{
    painter->save();

    // Data
    QDateTime timestamp = index.data(WorkflowHistoryModel::TimestampRole).toDateTime();
    QString mode = index.data(WorkflowHistoryModel::ModeRole).toString();
    QString status = index.data(WorkflowHistoryModel::StatusRole).toString();
    int masterCount = index.data(WorkflowHistoryModel::MasterCountRole).toInt();
    int duration = index.data(WorkflowHistoryModel::DurationRole).toInt();

    // Status color
    const auto& tokenSet = an::ui::tokens();
    QColor statusColor = tokenSet.colors.textTertiary;
    if (status == "success")
        statusColor = tokenSet.colors.statusSuccess;
    else if (status == "failed")
        statusColor = tokenSet.colors.statusError;
    else if (status == "cancelled")
        statusColor = tokenSet.colors.statusWarning;

    const int padding = historyPadding();
    QRect r = option.rect.adjusted(padding, padding, -padding, -padding);
    const auto buttons = buttonsForIndex(index, option.rect);
    int contentRight = r.right();
    for (const auto& button : buttons)
    {
        contentRight = (std::min)(contentRight, button.rect.left() - padding);
    }

    // Draw status bar/pill
    QRect statusRect(r.left(), r.top(), an::ui::tokens().spacing.xs, r.height());
    painter->fillRect(statusRect, statusColor);

    int x = r.left() + an::ui::tokens().spacing.md;

    // Timestamp
    QString dateStr = timestamp.toString("yyyy-MM-dd HH:mm");
    const RowTextColors textColors = resolvedRowTextColors(option);

    painter->setPen(textColors.primary);
    QFont f = option.font;
    f.setBold(true);
    painter->setFont(f);
    QFontMetrics metrics(f);
    const int dateWidth = metrics.horizontalAdvance(dateStr);
    painter->drawText(x, r.top(), (std::max)(0, contentRight - x), r.height(), Qt::AlignVCenter | Qt::AlignLeft,
                      metrics.elidedText(dateStr, Qt::ElideRight, (std::max)(0, contentRight - x)));

    // Calculate width of date
    x += dateWidth + an::ui::tokens().spacing.lg;

    f.setBold(false);
    painter->setFont(f);
    metrics = QFontMetrics(f);

    // Info string: Mode · Masters · Duration
    QString durationStr;
    if (duration < 60000)
        durationStr = QString("%1s").arg(duration / 1000);
    else
        durationStr = QString("%1m %2s").arg(duration / 60000).arg((duration % 60000) / 1000);

    QString info = QString("%1 · %2 masters · %3").arg(mode).arg(masterCount).arg(durationStr);
    painter->setPen(textColors.secondary);
    painter->drawText(x, r.top(), (std::max)(0, contentRight - x), r.height(), Qt::AlignVCenter | Qt::AlignLeft,
                      metrics.elidedText(info, Qt::ElideRight, (std::max)(0, contentRight - x)));

    for (const auto& button : buttons)
    {
        const HistoryButtonId cursorHoveredId = hoveredButtonFromCursor(index, option);
        const int buttonId = static_cast<int>(button.id);
        const bool hovered =
            (m_hoveredIndex == index && m_hoveredButtonId == buttonId) || (cursorHoveredId == button.id);
        const bool pressed =
            m_isButtonPressActive && m_pressedIndex == index && m_pressedButtonId == buttonId && hovered;
        paintButton(painter, option, button, hovered, pressed);
    }

    painter->restore();
}

void WorkflowHistoryDelegate::paintGroupItem(QPainter* painter, const QStyleOptionViewItem& option,
                                             const QModelIndex& index) const
{
    painter->save();
    const RowTextColors textColors = resolvedRowTextColors(option);

    QString groupKey = index.data(WorkflowHistoryModel::GroupKeyRole).toString();
    QString thumbPath = index.data(WorkflowHistoryModel::ThumbnailPathRole).toString();
    qint64 size = index.data(WorkflowHistoryModel::OutputSizeRole).toLongLong();
    int frames = index.data(WorkflowHistoryModel::FrameCountRole).toInt();
    int duration = index.data(WorkflowHistoryModel::DurationRole).toInt();

    const int padding = historyPadding();
    QRect r = option.rect.adjusted(padding, padding, -padding, -padding);
    const auto buttons = buttonsForIndex(index, option.rect);
    int contentRight = r.right();
    for (const auto& button : buttons)
    {
        contentRight = (std::min)(contentRight, button.rect.left() - padding);
    }

    // Thumbnail
    const int thumbSize = thumbnailSize();
    QRect thumbRect(r.left(), r.top(), thumbSize, thumbSize);
    painter->setPen(Qt::NoPen);
    painter->setBrush(an::ui::tokens().colors.bgPrimary);
    painter->drawRect(thumbRect);

    if (!thumbPath.isEmpty() && m_thumbnailCache)
    {
        QImage thumb = m_thumbnailCache->cachedThumbnail(thumbPath);
        if (thumb.isNull())
        {
            m_thumbnailCache->requestThumbnail(thumbPath, thumbRect.size());
            painter->setPen(an::ui::tokens().colors.textPrimary);
            painter->drawText(thumbRect, Qt::AlignCenter, "...");
        }
        else
        {
            const QRect imageRect = aspectFitRect(thumb.size(), thumbRect);
            if (!imageRect.isEmpty())
            {
                painter->drawImage(imageRect, thumb);
            }
        }
    }
    else if (!thumbPath.isEmpty())
    {
        painter->setPen(an::ui::tokens().colors.textPrimary);
        painter->drawText(thumbRect, Qt::AlignCenter, "IMG");
    }

    int x = r.left() + thumbSize + an::ui::tokens().spacing.md;
    int y = r.top();
    int h = r.height() / 2;

    // Group Key (Title)
    QFont f = option.font;
    f.setBold(true);
    painter->setFont(f);
    painter->setPen(textColors.primary);
    QFontMetrics metrics(f);
    painter->drawText(x, y, (std::max)(0, contentRight - x), h, Qt::AlignVCenter | Qt::AlignLeft,
                      metrics.elidedText(groupKey, Qt::ElideMiddle, (std::max)(0, contentRight - x)));

    // Stats
    y += h;
    f.setBold(false);
    painter->setFont(f);
    painter->setPen(textColors.secondary);
    metrics = QFontMetrics(f);

    QString sizeStr = QString::number(size / (1024 * 1024)) + " MB";
    QString info = QString("%1 frames · %2 · %3s").arg(frames).arg(sizeStr).arg(duration / 1000);
    painter->drawText(x, y, (std::max)(0, contentRight - x), h, Qt::AlignTop | Qt::AlignLeft,
                      metrics.elidedText(info, Qt::ElideRight, (std::max)(0, contentRight - x)));

    for (const auto& button : buttons)
    {
        const HistoryButtonId cursorHoveredId = hoveredButtonFromCursor(index, option);
        const int buttonId = static_cast<int>(button.id);
        const bool hovered =
            (m_hoveredIndex == index && m_hoveredButtonId == buttonId) || (cursorHoveredId == button.id);
        const bool pressed =
            m_isButtonPressActive && m_pressedIndex == index && m_pressedButtonId == buttonId && hovered;
        paintButton(painter, option, button, hovered, pressed);
    }

    painter->restore();
}

bool WorkflowHistoryDelegate::eventFilter(QObject* watched, QEvent* event)
{
    if (!m_viewport || watched != m_viewport || !m_view)
    {
        return QStyledItemDelegate::eventFilter(watched, event);
    }

    const auto setHoveredState = [this](const QModelIndex& index, const HistoryButtonId id) -> bool {
        const QPersistentModelIndex newIndex =
            (id == HistoryButtonId::None) ? QPersistentModelIndex{} : QPersistentModelIndex(index);
        const int newButtonId = static_cast<int>(id);
        if (m_hoveredIndex == newIndex && m_hoveredButtonId == newButtonId)
        {
            return false;
        }
        m_hoveredIndex = newIndex;
        m_hoveredButtonId = newButtonId;
        return true;
    };

    const auto buttonAtViewportPos = [this](const QPoint& viewportPos, QModelIndex* outIndex) -> HistoryButtonId {
        if (!m_view)
        {
            if (outIndex)
            {
                *outIndex = QModelIndex{};
            }
            return HistoryButtonId::None;
        }

        const QModelIndex index = m_view->indexAt(viewportPos);
        if (!index.isValid())
        {
            if (outIndex)
            {
                *outIndex = QModelIndex{};
            }
            return HistoryButtonId::None;
        }

        const QRect rect = m_view->visualRect(index);
        const HistoryButtonId id = buttonAtPosition(buttonsForIndex(index, rect), viewportPos);
        if (outIndex)
        {
            *outIndex = (id == HistoryButtonId::None) ? QModelIndex{} : index;
        }
        return id;
    };

    if (event->type() == QEvent::MouseMove || event->type() == QEvent::HoverMove)
    {
        QPoint pointerPos;
        Qt::MouseButtons buttonsMask = Qt::NoButton;

        if (event->type() == QEvent::MouseMove)
        {
            auto* e = static_cast<QMouseEvent*>(event);
            pointerPos = e->pos();
            buttonsMask = e->buttons();
        }
        else
        {
            auto* e = static_cast<QHoverEvent*>(event);
            pointerPos = e->position().toPoint();
        }

        if (m_isButtonPressActive && !(buttonsMask & Qt::LeftButton))
        {
            m_isButtonPressActive = false;
            m_pressedIndex = QPersistentModelIndex{};
            m_pressedButtonId = static_cast<int>(HistoryButtonId::None);
        }

        QModelIndex hoveredIndex;
        const HistoryButtonId hoveredId = buttonAtViewportPos(pointerPos, &hoveredIndex);
        if (setHoveredState(hoveredIndex, hoveredId))
        {
            requestRepaint();
        }
        return false;
    }

    if (event->type() == QEvent::HoverLeave || event->type() == QEvent::Leave)
    {
        if (setHoveredState(QModelIndex{}, HistoryButtonId::None))
        {
            requestRepaint();
        }
        return false;
    }

    if (event->type() == QEvent::MouseButtonPress)
    {
        auto* e = static_cast<QMouseEvent*>(event);
        if (e->button() != Qt::LeftButton)
        {
            return QStyledItemDelegate::eventFilter(watched, event);
        }

        QModelIndex pressedIndex;
        const HistoryButtonId pressedId = buttonAtViewportPos(e->pos(), &pressedIndex);
        if (pressedId == HistoryButtonId::None || !pressedIndex.isValid())
        {
            return QStyledItemDelegate::eventFilter(watched, event);
        }

        m_isButtonPressActive = true;
        m_pressedIndex = QPersistentModelIndex(pressedIndex);
        m_pressedButtonId = static_cast<int>(pressedId);
        setHoveredState(pressedIndex, pressedId);
        requestRepaint();
        return true;
    }

    if (event->type() == QEvent::MouseButtonRelease)
    {
        auto* e = static_cast<QMouseEvent*>(event);
        if (e->button() != Qt::LeftButton)
        {
            return QStyledItemDelegate::eventFilter(watched, event);
        }

        QModelIndex releasedIndex;
        const HistoryButtonId releasedId = buttonAtViewportPos(e->pos(), &releasedIndex);
        const int releasedIdInt = static_cast<int>(releasedId);
        const bool activateAction = m_isButtonPressActive && m_pressedIndex.isValid() &&
                                    m_pressedIndex == releasedIndex && m_pressedButtonId == releasedIdInt &&
                                    releasedId != HistoryButtonId::None;
        const bool hadActivePress = m_isButtonPressActive;
        const QModelIndex actionIndex = m_pressedIndex;

        m_isButtonPressActive = false;
        m_pressedIndex = QPersistentModelIndex{};
        m_pressedButtonId = static_cast<int>(HistoryButtonId::None);
        setHoveredState(releasedIndex, releasedId);
        requestRepaint();

        if (activateAction && actionIndex.isValid())
        {
            switch (releasedId)
            {
            case HistoryButtonId::OpenAll:
                emit openAllRequested(actionIndex);
                return true;
            case HistoryButtonId::Report:
                emit reportRequested(actionIndex);
                return true;
            case HistoryButtonId::SaveReport:
                emit saveReportRequested(actionIndex);
                return true;
            case HistoryButtonId::Log:
                emit logRequested(actionIndex);
                return true;
            case HistoryButtonId::Rerun:
                emit rerunRequested(actionIndex);
                return true;
            case HistoryButtonId::DeleteArchive:
                emit deleteRequested(actionIndex);
                return true;
            case HistoryButtonId::OpenMaster:
                emit openMasterRequested(actionIndex);
                return true;
            case HistoryButtonId::CompareMaster:
                emit compareMasterRequested(actionIndex);
                return true;
            case HistoryButtonId::None:
                break;
            }
        }

        if (hadActivePress)
        {
            return true;
        }
        return QStyledItemDelegate::eventFilter(watched, event);
    }

    return QStyledItemDelegate::eventFilter(watched, event);
}

bool WorkflowHistoryDelegate::editorEvent(QEvent* event, QAbstractItemModel* model, const QStyleOptionViewItem& option,
                                          const QModelIndex& index)
{
    return QStyledItemDelegate::editorEvent(event, model, option, index);
}

void WorkflowHistoryDelegate::requestRepaint(const QStyleOptionViewItem& option) const
{
    if (option.widget)
    {
        auto* widget = const_cast<QWidget*>(option.widget);
        widget->update();
    }
}

void WorkflowHistoryDelegate::requestRepaint() const
{
    if (m_viewport)
    {
        m_viewport->update();
        return;
    }

    if (m_view && m_view->viewport())
    {
        m_view->viewport()->update();
    }
}

bool WorkflowHistoryDelegate::helpEvent(QHelpEvent* event, QAbstractItemView* view, const QStyleOptionViewItem& option,
                                        const QModelIndex& index)
{
    if (event->type() == QEvent::ToolTip && index.isValid())
    {
        const auto buttons = buttonsForIndex(index, option.rect);

        for (const auto& btn : buttons)
        {
            if (btn.rect.contains(event->pos()))
            {
                QToolTip::showText(event->globalPos(), btn.tooltip, view);
                return true;
            }
        }
    }

    return QStyledItemDelegate::helpEvent(event, view, option, index);
}
