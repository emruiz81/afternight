// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightProcessesExport.h"

#include <QPointer>
#include <QPersistentModelIndex>
#include <QStyledItemDelegate>

#include <verdigris/wobjectdefs.h>

class RunHistoryThumbnailCache;
class QAbstractItemView;
class QWidget;

class AFTERNIGHTPROCESSES_API WorkflowHistoryDelegate : public QStyledItemDelegate
{
    W_OBJECT(WorkflowHistoryDelegate)

public:
    explicit WorkflowHistoryDelegate(QObject* parent = nullptr);
    void setThumbnailCache(RunHistoryThumbnailCache* cache);
    void setView(QAbstractItemView* view);

    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override;
    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override;

    // Action signals for buttons drawn in the delegate
    void openAllRequested(const QModelIndex& index) W_SIGNAL(openAllRequested, index);
    void reportRequested(const QModelIndex& index) W_SIGNAL(reportRequested, index);
    void saveReportRequested(const QModelIndex& index) W_SIGNAL(saveReportRequested, index);
    void logRequested(const QModelIndex& index) W_SIGNAL(logRequested, index);
    void rerunRequested(const QModelIndex& index) W_SIGNAL(rerunRequested, index);
    void deleteRequested(const QModelIndex& index) W_SIGNAL(deleteRequested, index);
    void openMasterRequested(const QModelIndex& index) W_SIGNAL(openMasterRequested, index);
    void compareMasterRequested(const QModelIndex& index) W_SIGNAL(compareMasterRequested, index);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    bool editorEvent(QEvent* event, QAbstractItemModel* model, const QStyleOptionViewItem& option,
                     const QModelIndex& index) override;
    bool helpEvent(QHelpEvent* event, QAbstractItemView* view, const QStyleOptionViewItem& option,
                   const QModelIndex& index) override;

private:
    void paintWorkflowHeader(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const;
    void paintGroupItem(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const;
    void requestRepaint(const QStyleOptionViewItem& option) const;
    void requestRepaint() const;

    RunHistoryThumbnailCache* m_thumbnailCache = nullptr;
    QPointer<QAbstractItemView> m_view;
    QPointer<QWidget> m_viewport;
    QPersistentModelIndex m_hoveredIndex;
    QPersistentModelIndex m_pressedIndex;
    int m_hoveredButtonId = 0;
    int m_pressedButtonId = 0;
    bool m_isButtonPressActive = false;
};
