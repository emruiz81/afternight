// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include <QMap>
#include <QString>
#include <QStringList>
#include <QTreeWidget>
#include <QTreeWidgetItem>

#include <functional>
#include <map>
#include <vector>

namespace process_utils
{

class TreeWidgetHelper
{
public:
    static void expandAndResizeColumns(QTreeWidget* treeWidget, int extraMargin = 12)
    {
        if (!treeWidget)
        {
            return;
        }

        const bool wasUpdatesEnabled = treeWidget->updatesEnabled();
        treeWidget->setUpdatesEnabled(false);

        treeWidget->expandAll();

        const int columnCount = treeWidget->columnCount();
        for (int i = 0; i < columnCount; ++i)
        {
            if (!treeWidget->isColumnHidden(i))
            {
                treeWidget->resizeColumnToContents(i);
                treeWidget->setColumnWidth(i, treeWidget->columnWidth(i) + extraMargin);
            }
        }

        treeWidget->setUpdatesEnabled(wasUpdatesEnabled);
    }

    static void saveExpandedState(QTreeWidget* treeWidget, QMap<QString, bool>& state, int pathTextColumn = 0)
    {
        state.clear();
        if (!treeWidget)
        {
            return;
        }

        std::function<void(QTreeWidgetItem*)> saveItemState = [&](QTreeWidgetItem* item) {
            if (!item || item->childCount() == 0)
            {
                return;
            }

            state[buildPathKey(item, pathTextColumn)] = item->isExpanded();

            for (int i = 0; i < item->childCount(); ++i)
            {
                saveItemState(item->child(i));
            }
        };

        for (int i = 0; i < treeWidget->topLevelItemCount(); ++i)
        {
            saveItemState(treeWidget->topLevelItem(i));
        }
    }

    static void restoreExpandedState(QTreeWidget* treeWidget, const QMap<QString, bool>& state, int pathTextColumn = 0)
    {
        if (!treeWidget)
        {
            return;
        }

        std::function<void(QTreeWidgetItem*)> restoreItemState = [&](QTreeWidgetItem* item) {
            if (!item || item->childCount() == 0)
            {
                return;
            }

            const QString key = buildPathKey(item, pathTextColumn);
            if (state.contains(key))
            {
                item->setExpanded(state[key]);
            }

            for (int i = 0; i < item->childCount(); ++i)
            {
                restoreItemState(item->child(i));
            }
        };

        for (int i = 0; i < treeWidget->topLevelItemCount(); ++i)
        {
            restoreItemState(treeWidget->topLevelItem(i));
        }
    }

    static int countLeafItems(QTreeWidgetItem* item)
    {
        if (!item)
        {
            return 0;
        }

        const int childCount = item->childCount();
        if (childCount == 0)
        {
            return 1;
        }

        int count = 0;
        for (int i = 0; i < childCount; ++i)
        {
            count += countLeafItems(item->child(i));
        }

        return count;
    }

    static QTreeWidgetItem* getFirstLeafItem(QTreeWidgetItem* item)
    {
        if (!item)
        {
            return nullptr;
        }

        if (item->childCount() == 0)
        {
            return item;
        }

        for (int i = 0; i < item->childCount(); ++i)
        {
            if (auto* leaf = getFirstLeafItem(item->child(i)))
            {
                return leaf;
            }
        }

        return nullptr;
    }

    static void updateGroupNodeWithCount(QTreeWidgetItem* item, int labelColumn = 0)
    {
        if (!item)
        {
            return;
        }

        const int childCount = item->childCount();
        if (childCount <= 0)
        {
            return;
        }

        for (int i = 0; i < childCount; ++i)
        {
            updateGroupNodeWithCount(item->child(i), labelColumn);
        }

        const int leafCount = countLeafItems(item);

        QString currentText = item->text(labelColumn);
        const int parenPos = currentText.indexOf(" (");
        if (parenPos != -1)
        {
            currentText = currentText.left(parenPos);
        }

        const QString imageText = (leafCount == 1) ? QStringLiteral("image") : QStringLiteral("images");
        item->setText(labelColumn, QStringLiteral("%1 (%2 %3)").arg(currentText).arg(leafCount).arg(imageText));
    }

    static void updateAllGroupNodesWithCounts(QTreeWidget* treeWidget, int labelColumn = 0)
    {
        if (!treeWidget)
        {
            return;
        }

        for (int i = 0; i < treeWidget->topLevelItemCount(); ++i)
        {
            updateGroupNodeWithCount(treeWidget->topLevelItem(i), labelColumn);
        }
    }

    static QTreeWidgetItem* findOrCreateResolutionGroup(QTreeWidget* treeWidget, const QString& resolution,
                                                        int resolutionColumn = 1, int labelColumn = 0)
    {
        if (!treeWidget)
        {
            return nullptr;
        }

        for (int i = 0; i < treeWidget->topLevelItemCount(); ++i)
        {
            QTreeWidgetItem* item = treeWidget->topLevelItem(i);
            if (item && item->text(resolutionColumn) == resolution)
            {
                return item;
            }
        }

        auto* resolutionGroup = new QTreeWidgetItem();
        resolutionGroup->setText(labelColumn, QStringLiteral("Resolution: %1").arg(resolution));
        resolutionGroup->setText(resolutionColumn, resolution);
        resolutionGroup->setExpanded(true);
        treeWidget->addTopLevelItem(resolutionGroup);

        return resolutionGroup;
    }

    static QTreeWidgetItem* findOrCreateSubGroup(QTreeWidgetItem* parent, const QString& labelPrefix,
                                                 const QString& value, int columnIndex, int labelColumn = 0)
    {
        if (!parent)
        {
            return nullptr;
        }

        for (int i = 0; i < parent->childCount(); ++i)
        {
            QTreeWidgetItem* item = parent->child(i);
            if (item && item->text(columnIndex) == value)
            {
                return item;
            }
        }

        auto* subGroup = new QTreeWidgetItem(parent);
        subGroup->setText(labelColumn, QStringLiteral("%1: %2").arg(labelPrefix, value));
        subGroup->setText(columnIndex, value);
        subGroup->setExpanded(true);

        return subGroup;
    }

    static QTreeWidgetItem* findTreeItemByGroupCriteria(QTreeWidget* treeWidget, const QString& resolution,
                                                        const std::map<int, QString>& columnCriteria,
                                                        int resolutionColumn = 1)
    {
        if (!treeWidget)
        {
            return nullptr;
        }

        QTreeWidgetItem* currentItem = nullptr;
        for (int i = 0; i < treeWidget->topLevelItemCount(); ++i)
        {
            auto* item = treeWidget->topLevelItem(i);
            if (item && item->text(resolutionColumn) == resolution)
            {
                currentItem = item;
                break;
            }
        }

        if (!currentItem)
        {
            return nullptr;
        }

        for (const auto& [columnIndex, value] : columnCriteria)
        {
            if (value.isEmpty() || value == QStringLiteral("-"))
            {
                continue;
            }

            bool found = false;
            for (int i = 0; i < currentItem->childCount(); ++i)
            {
                auto* child = currentItem->child(i);
                if (child && child->text(columnIndex) == value)
                {
                    currentItem = child;
                    found = true;
                    break;
                }
            }

            if (!found)
            {
                return currentItem;
            }
        }

        return currentItem;
    }

    static void collectLeafFiles(QTreeWidgetItem* node, int column, std::vector<QString>& out)
    {
        if (!node)
        {
            return;
        }

        if (node->childCount() == 0)
        {
            out.push_back(node->text(column));
            return;
        }

        for (int i = 0; i < node->childCount(); ++i)
        {
            collectLeafFiles(node->child(i), column, out);
        }
    }

    static void collectLeafItems(QTreeWidgetItem* node, std::vector<QTreeWidgetItem*>& out)
    {
        if (!node)
        {
            return;
        }

        if (node->childCount() == 0)
        {
            out.push_back(node);
            return;
        }

        for (int i = 0; i < node->childCount(); ++i)
        {
            collectLeafItems(node->child(i), out);
        }
    }

private:
    static QString buildPathKey(const QTreeWidgetItem* item, int pathTextColumn)
    {
        QStringList pathParts;
        const QTreeWidgetItem* current = item;
        while (current)
        {
            pathParts.prepend(current->text(pathTextColumn));
            current = current->parent();
        }

        return pathParts.join('/');
    }
};

} // namespace process_utils
