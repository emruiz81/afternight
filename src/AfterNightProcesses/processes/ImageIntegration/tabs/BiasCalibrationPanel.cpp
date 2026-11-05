// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "BiasCalibrationPanel.h"

#include "BaseCalibrationPanel.h"
#include "CalibrationGroupTable.h"
#include "CalibrationPanelUtils.h"
#include "processes/ImageIntegration/model/CalibrationMatcher.h"
#include "processes/ImageIntegration/model/IntegrationDataModel.h"
#include "processes/ImageIntegration/model/UndoCommands.h"
#include "processes/ImageIntegration/model/GroupingKeys.h"
#include "processes/ImageIntegration/controls/StackingSettingsDialog.h"
#include "app/FileDialogUtils.h"
#include "processes/ProcessCommonUtils.h"

#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QMenu>
#include <QPushButton>
#include <QToolButton>
#include <QVBoxLayout>
#include <QTableWidgetItem>
#include <QUndoStack>

#include <verdigris/wobjectimpl.h>

W_OBJECT_IMPL(BiasCalibrationPanel)

namespace
{
struct SelectedGroup
{
    GroupKey key;
    QString label;
    bool stackingAvailable = true;
};

std::optional<SelectedGroup> selectedGroupFromTable(const QTableWidget* table)
{
    if (!table)
    {
        return std::nullopt;
    }

    const auto selectedRows = table->selectionModel() ? table->selectionModel()->selectedRows() : QModelIndexList{};
    if (selectedRows.isEmpty())
    {
        return std::nullopt;
    }

    const int row = selectedRows.front().row();
    const QTableWidgetItem* item = table->item(row, 0);
    if (!item)
    {
        return std::nullopt;
    }

    const auto groupKey = GroupKey::fromString(item->data(Qt::UserRole).toString());
    if (!groupKey)
    {
        return std::nullopt;
    }

    SelectedGroup result;
    result.key = *groupKey;
    result.label = item->text();
    result.stackingAvailable = item->data(Qt::UserRole + 1).toBool();
    return result;
}
} // namespace

BiasCalibrationPanel::BiasCalibrationPanel(IntegrationDataModel* model, QWidget* parent, QUndoStack* undoStack)
    : BaseCalibrationPanel(model, parent, undoStack)
{
    initializePanelChrome(QStringLiteral("biasCalibrationPanel"), tr("BIAS Calibration Groups"));

    setupUi();
    updateTable();
}

void BiasCalibrationPanel::setupUi()
{
    m_content = contentWidget();
    auto* contentLayout = new QVBoxLayout();
    contentLayout->setContentsMargins(0, 0, 0, 0);
    setContentLayout(contentLayout);

    auto* footerLayout = new QHBoxLayout();
    m_settingsButton = new QPushButton(tr("Stacking Settings..."), m_content);
    m_settingsButton->setObjectName(QStringLiteral("btnBiasStackingSettings"));
    m_settingsButton->setProperty("anButtonVariant", QStringLiteral("secondary"));
    m_settingsButton->setToolTip(tr("Edit stacking settings for the selected group"));
    m_settingsButton->setEnabled(false);
    QObject::connect(m_settingsButton, &QPushButton::clicked, this, [this]() {
        if (!m_model || !m_table)
        {
            return;
        }

        const auto selected = selectedGroupFromTable(m_table);
        if (!selected || !selected->stackingAvailable)
        {
            return;
        }

        openStackingSettingsDialog(m_model, FrameType::Bias, selected->key, selected->label, this, m_undoStack);
    });
    footerLayout->addWidget(m_settingsButton);
    m_addButton = new QPushButton(tr("+ Add Master"), m_content);
    m_addButton->setProperty("anButtonVariant", QStringLiteral("primary"));
    m_addButton->setToolTip(tr("Add an existing bias master frame"));
    QObject::connect(m_addButton, &QPushButton::clicked, this, [this]() {
        const QString filter = tr("Calibration Masters (*.fit *.fits *.xisf);;All Files (*.*)");
        openCalibrationMasterFileDialog(this, tr("Select Bias Master"), filter, [this](const QString& path) {
            if (!m_model)
            {
                return;
            }
            if (const std::optional<FrameInfo> master = readExternalMaster(path, FrameType::Bias); !master.has_value())
            {
                return;
            }
            else if (m_undoStack)
            {
                m_undoStack->push(new AddExternalMasterCommand(m_model, FrameType::Bias, *master));
            }
            else
            {
                m_model->addExternalMaster(FrameType::Bias, *master);
            }
        });
    });
    footerLayout->addWidget(m_addButton);
    footerLayout->addStretch();
    // footer will be placed after the table so buttons sit at the bottom-left

    m_table = new CalibrationGroupTable(m_content);
    m_table->setObjectName(QStringLiteral("tblBiasCalibration"));
    m_table->setColumnCount(6);
    m_table->setHorizontalHeaderLabels(
        {tr("Group"), tr("Frames"), tr("Master"), tr("Source"), tr("Stacking"), tr("Normalization")});
    contentLayout->addWidget(m_table);
    contentLayout->addLayout(footerLayout);

    bindContentVisibility();

    QObject::connect(m_table, &QTableWidget::cellDoubleClicked, this, [this](int row, int /*col*/) {
        if (!m_model || !m_table)
        {
            return;
        }

        QTableWidgetItem* item = m_table->item(row, 0);
        if (!item)
        {
            return;
        }

        if (!item->data(Qt::UserRole + 1).toBool())
        {
            return;
        }

        const auto group_key = GroupKey::fromString(item->data(Qt::UserRole).toString());
        if (!group_key)
        {
            return;
        }

        openStackingSettingsDialog(m_model, FrameType::Bias, *group_key, item->text(), this, m_undoStack);
    });

    QObject::connect(m_table, &QTableWidget::itemSelectionChanged, this, [this]() {
        if (!m_table)
        {
            return;
        }

        if (m_settingsButton)
        {
            const auto selectedRows =
                m_table->selectionModel() ? m_table->selectionModel()->selectedRows() : QModelIndexList{};
            if (selectedRows.isEmpty())
            {
                m_settingsButton->setEnabled(false);
                m_settingsButton->setToolTip(tr("Select a group to edit stacking settings."));
            }
            else
            {
                const auto selected = selectedGroupFromTable(m_table);
                const bool available = selected.has_value() && selected->stackingAvailable;
                m_settingsButton->setEnabled(available);
                m_settingsButton->setToolTip(available ? tr("Edit stacking settings for the selected group")
                                                       : tr("Not available for master frames (no stacking required)."));
            }
        }

        auto selected = m_table->selectionModel()->selectedRows();
        if (!selected.isEmpty())
        {
            int row = selected.front().row();
            QTableWidgetItem* item = m_table->item(row, 0);
            if (item)
            {
                QString keyStr = item->data(Qt::UserRole).toString();
                if (auto key = GroupKey::fromString(keyStr))
                {
                    emit selectionChanged(*key);
                    return;
                }
            }
        }
        emit selectionChanged(GroupKey{});
    });

    m_table->setContextMenuPolicy(Qt::CustomContextMenu);
    QObject::connect(m_table, &QTableWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        if (!m_model || !m_table)
            return;
        QTableWidgetItem* item = m_table->itemAt(pos);
        if (!item)
            return;

        QTableWidgetItem* groupItem = m_table->item(item->row(), 0);
        if (!groupItem)
            return;

        const auto groupKey = GroupKey::fromString(groupItem->data(Qt::UserRole).toString());
        if (!groupKey)
            return;

        QMenu menu(this);
        auto* settingsAction = menu.addAction(tr("Stacking Settings..."));
        const bool stackingAvailable = groupItem->data(Qt::UserRole + 1).toBool();
        settingsAction->setEnabled(stackingAvailable);
        settingsAction->setToolTip(tr("Edit stacking settings for this group"));
        QObject::connect(settingsAction, &QAction::triggered, this,
                         [this, stackingAvailable, key = *groupKey, label = groupItem->text()]() {
                             if (!stackingAvailable)
                             {
                                 return;
                             }
                             openStackingSettingsDialog(m_model, FrameType::Bias, key, label, this, m_undoStack);
                         });

        auto* goToGroupAction = menu.addAction(tr("Go to Group Definition"));
        QObject::connect(goToGroupAction, &QAction::triggered, this,
                         [this, key = *groupKey]() { emit goToGroupDefinitionRequested(key); });

        menu.addSeparator();
        auto* lightsMenu = menu.addMenu(tr("Lights using this Bias"));

        CalibrationMatcher matcher(m_model->groupingOptions(FrameType::Bias), m_model->groupingOptions(FrameType::Dark),
                                   m_model->groupingOptions(FrameType::Flat), m_model->calibrationMatchingPolicy());
        matcher.setBiasGroups(collectBiasGroups(m_model));
        matcher.setDarkGroups(collectDarkGroups(m_model));
        matcher.setFlatGroups(collectFlatGroups(m_model));
        matcher.setOverrides(m_model->calibrationOverrides());

        const CalibrationMatchingPolicy policy = m_model->calibrationMatchingPolicy();

        int count = 0;
        const QString biasKeyStr = groupKey->toString();
        for (const auto& group : m_model->lightGroups())
        {
            if (group.frames.empty())
                continue;
            const auto lightKey = LightGroupKey::fromFrame(group.frames.front());
            const auto tempC = averageTemperatureC(group.frames);
            const auto darkMatch = matcher.findDark(lightKey, tempC);
            if (!lightTakesBias(darkMatch, policy.subtractBiasFromDark))
            {
                continue;
            }
            if (matcher.findBias(lightKey).matchedGroupKey == biasKeyStr)
            {
                QString label = group.key.toDisplayString(m_model->groupingOptions(FrameType::Light), FrameType::Light);
                auto* action = lightsMenu->addAction(label);
                QObject::connect(action, &QAction::triggered, this,
                                 [this, k = group.key]() { m_model->setSelectedGroupKeys({k}); });
                count++;
            }
        }

        if (count == 0)
        {
            lightsMenu->addAction(tr("(None)"))->setEnabled(false);
        }

        auto* flatsMenu = menu.addMenu(tr("Flats using this Bias"));
        int flatCount = 0;
        for (const auto& group : m_model->flatGroups())
        {
            if (group.frames.empty())
                continue;
            const auto lightKey = LightGroupKey::fromFrame(group.frames.front());
            const auto tempC = averageTemperatureC(group.frames);
            const auto darkMatch = matcher.findDark(lightKey, tempC);
            if (!flatTakesBias(darkMatch))
            {
                continue;
            }
            if (matcher.findBias(lightKey).matchedGroupKey == biasKeyStr)
            {
                QString label = group.key.toDisplayString(m_model->groupingOptions(FrameType::Flat), FrameType::Flat);
                auto* action = flatsMenu->addAction(label);
                QObject::connect(action, &QAction::triggered, this,
                                 [this, k = group.key]() { m_model->setSelectedGroupKeys({k}); });
                flatCount++;
            }
        }
        if (flatCount == 0)
        {
            flatsMenu->addAction(tr("(None)"))->setEnabled(false);
        }

        menu.exec(m_table->viewport()->mapToGlobal(pos));
    });
}

void BiasCalibrationPanel::refresh()
{
    updateTable();
}

void BiasCalibrationPanel::setHighlightedGroupKeys(const QSet<GroupKey>& keys)
{
    if (!m_table)
    {
        return;
    }

    QSet<QString> keyStrings;
    for (const auto& key : keys)
    {
        keyStrings.insert(key.toString());
    }
    m_table->setHighlightedKeys(keyStrings);
}

void BiasCalibrationPanel::updateTable()
{
    if (!m_model || !m_table)
    {
        return;
    }

    m_table->clearEmptyMessage();

    const auto& groups = m_model->biasGroups();
    int rows = 0;
    for (const auto& group : groups)
    {
        if (!group.frames.empty())
        {
            ++rows;
        }
    }

    if (rows == 0)
    {
        m_table->setEmptyMessage(tr("No bias groups available."));
        return;
    }

    m_table->setRowCount(rows);

    const auto& stackingSettings = m_model->stackingSettings(FrameType::Bias);
    int row = 0;
    for (const auto& group : groups)
    {
        if (group.frames.empty())
        {
            continue;
        }

        const auto& groupingOptions = m_model->groupingOptions(FrameType::Bias);
        const QString groupLabel = formatGroupLabel(group.key, tr("[Ungrouped]"), groupingOptions, FrameType::Bias);
        auto* groupItem = new QTableWidgetItem(groupLabel);
        groupItem->setData(Qt::UserRole, group.key.toString());
        groupItem->setToolTip(groupTooltip(FrameType::Bias, group.key, groupingOptions, group.frames));
        m_table->setItem(row, 0, groupItem);

        auto* framesItem = new QTableWidgetItem(QString::number(group.frames.size()));
        framesItem->setTextAlignment(Qt::AlignCenter);
        framesItem->setToolTip(framesCountTooltip(m_model, FrameType::Bias, group.frames));
        m_table->setItem(row, 1, framesItem);

        const FrameInfo* masterFrame = nullptr;
        MasterSource masterSource = MasterSource::Unknown;
        for (const auto& frame : group.frames)
        {
            if (frame.excluded)
            {
                continue;
            }

            if (m_model->isMasterFrame(frame.id))
            {
                masterFrame = &frame;
                masterSource = m_model->masterSource(frame.id).value_or(MasterSource::Unknown);
                break;
            }
        }

        const QString outputDir = m_model ? m_model->outputDirectory() : QString();
        const QString outputFileName =
            calibrationMasterOutputFilename(CalibrationOverrideType::Bias, group.key, groupingOptions, FrameType::Bias);
        const QString masterPath = calibrationMasterOutputPath(m_model, CalibrationOverrideType::Bias, group.key,
                                                               groupingOptions, FrameType::Bias);
        const bool hasAutoGeneratedMaster = !outputDir.trimmed().isEmpty() && QFileInfo::exists(masterPath);

        if (masterFrame)
        {
            const QString fileName = QFileInfo(masterFrame->displayPath).fileName();
            auto* masterItem = new QTableWidgetItem(fileName.isEmpty() ? masterFrame->displayPath : fileName);
            masterItem->setToolTip(masterFrame->id.canonicalPath());
            m_table->setItem(row, 2, masterItem);

            auto* sourceItem = new QTableWidgetItem(masterSourceLabel(masterSource));
            sourceItem->setToolTip(masterSourceTooltip(masterSource));
            m_table->setItem(row, 3, sourceItem);
        }
        else
        {
            if (hasAutoGeneratedMaster)
            {
                const QString fileName = QFileInfo(masterPath).fileName();
                auto* masterItem = new QTableWidgetItem(fileName.isEmpty() ? masterPath : fileName);
                masterItem->setToolTip(masterPath);
                m_table->setItem(row, 2, masterItem);
                auto* sourceItem = new QTableWidgetItem(masterSourceLabel(MasterSource::AutoGenerated));
                sourceItem->setToolTip(masterSourceTooltip(MasterSource::AutoGenerated));
                m_table->setItem(row, 3, sourceItem);
            }
            else
            {
                auto* masterItem = new QTableWidgetItem(outputFileName);
                masterItem->setToolTip(masterPath.trimmed().isEmpty() ? outputFileName : masterPath);
                m_table->setItem(row, 2, masterItem);

                auto* sourceItem = new QTableWidgetItem(tr("Will Generate"));
                sourceItem->setToolTip(masterSourceTooltip(MasterSource::Unknown));
                m_table->setItem(row, 3, sourceItem);
            }
        }

        const bool isExternalMaster = masterFrame != nullptr && (masterSource == MasterSource::UserDefined ||
                                                                 masterSource == MasterSource::AutoDetected);
        groupItem->setData(Qt::UserRole + 1, !isExternalMaster);

        const auto it = stackingSettings.find(group.key);
        const StackingSettings effective = (it != stackingSettings.end())
                                               ? normalizeCalibrationStackingSettings(FrameType::Bias, it->second)
                                               : defaultCalibrationStackingSettings(FrameType::Bias);

        const bool showStacking = group.frames.size() > 1;
        const QString tooltip = stackingSettingsTooltip(m_model, FrameType::Bias, group.key, stackingSettings);

        auto* stackingItem =
            new QTableWidgetItem(showStacking ? stackingMethodColumnText(effective) : QStringLiteral("-"));
        stackingItem->setToolTip(tooltip);
        m_table->setItem(row, 4, stackingItem);

        auto* normalizationItem = new QTableWidgetItem(showStacking ? tr("Disabled") : QStringLiteral("-"));
        normalizationItem->setToolTip(tooltip);
        m_table->setItem(row, 5, normalizationItem);

        ++row;
    }

    m_table->resizeColumnsToContents();
}
