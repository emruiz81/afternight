// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "FlatCalibrationPanel.h"

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

#include <QBrush>
#include <QColor>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QMenu>
#include <QPushButton>
#include <QTableWidgetItem>
#include <QToolButton>
#include <QVBoxLayout>
#include <QDir>
#include <QUndoStack>

#include <verdigris/wobjectimpl.h>

W_OBJECT_IMPL(FlatCalibrationPanel)

namespace
{
CalibrationOverrideType overrideTypeForColumn(int column)
{
    // Columns: 0 group, 1 frames, 2 bias, 3 dark
    return (column == 3) ? CalibrationOverrideType::Dark : CalibrationOverrideType::Bias;
}

QString overrideTypeDisplayName(CalibrationOverrideType type)
{
    switch (type)
    {
    case CalibrationOverrideType::Bias:
        return FlatCalibrationPanel::tr("Bias");
    case CalibrationOverrideType::Dark:
        return FlatCalibrationPanel::tr("Dark");
    case CalibrationOverrideType::Flat:
        return FlatCalibrationPanel::tr("Flat");
    }
    return FlatCalibrationPanel::tr("Calibration");
}

QString overrideDialogTitle(CalibrationOverrideType type)
{
    switch (type)
    {
    case CalibrationOverrideType::Bias:
        return FlatCalibrationPanel::tr("Select Bias Master");
    case CalibrationOverrideType::Dark:
        return FlatCalibrationPanel::tr("Select Dark Master");
    case CalibrationOverrideType::Flat:
        return FlatCalibrationPanel::tr("Select Flat Master");
    }
    return FlatCalibrationPanel::tr("Select Calibration Master");
}

std::vector<std::pair<QString, QString>>
collectOverrideCandidates(IntegrationDataModel* model, const LightGroupKey& lightKey, CalibrationOverrideType type)
{
    std::vector<std::pair<QString, QString>> candidates;
    if (!model)
    {
        return candidates;
    }

    const GroupedFrameView* groups = nullptr;
    GroupingOptions displayOptions;
    if (type == CalibrationOverrideType::Bias)
    {
        groups = &model->biasGroups();
        displayOptions = model->groupingOptions(FrameType::Bias);
    }
    else
    {
        groups = &model->darkGroups();
        displayOptions = model->groupingOptions(FrameType::Dark);
    }

    if (!groups)
    {
        return candidates;
    }

    // For override candidates, use RESOLUTION-ONLY matching.
    // This allows users to manually select ANY calibration group with the same resolution,
    // not just the ones that would auto-match based on other criteria.
    const QString targetResolution = lightKey.resolution;

    FrameType groupFrameType = FrameType::Light;
    if (type == CalibrationOverrideType::Bias)
    {
        groupFrameType = FrameType::Bias;
    }
    else
    {
        groupFrameType = FrameType::Dark;
    }

    for (const auto& group : *groups)
    {
        if (group.frames.empty())
        {
            continue;
        }

        // Resolution-only check
        const auto& firstFrame = group.frames.front();
        const QString groupResolution = firstFrame.metadata.resolutionString();

        if (groupResolution != targetResolution)
        {
            continue;
        }

        // List each compatible group once using its canonical master path.
        // Groups are listed by their output name.
        const QString groupLabel = group.key.toDisplayString(displayOptions, groupFrameType);
        const QString masterPath = canonicalMasterPathForGroup(model, type, group);

        if (masterPath.trimmed().isEmpty())
        {
            continue;
        }

        candidates.emplace_back(groupLabel, masterPath);
    }

    std::sort(candidates.begin(), candidates.end(), [](const auto& a, const auto& b) {
        if (a.first == b.first)
        {
            return a.second < b.second;
        }
        return a.first < b.first;
    });
    return candidates;
}

CalibrationMatch currentEffectiveMatch(IntegrationDataModel* model, const LightGroupKey& lightKey,
                                       CalibrationOverrideType type, std::optional<double> tempC = std::nullopt)
{
    if (!model)
    {
        return {};
    }

    CalibrationMatcher matcher(model->groupingOptions(FrameType::Bias), model->groupingOptions(FrameType::Dark),
                               model->groupingOptions(FrameType::Flat), model->calibrationMatchingPolicy());
    matcher.setOverrides(model->calibrationOverrides());

    {
        std::vector<BiasCalibrationGroup> bias_groups;
        for (const auto& group : model->biasGroups())
        {
            if (group.frames.empty())
            {
                continue;
            }
            BiasCalibrationGroup entry;
            entry.key = BiasGroupKey::fromFrame(group.frames.front());
            entry.groupKey = group.key;
            entry.masterPath = canonicalMasterPathForGroup(model, CalibrationOverrideType::Bias, group);
            bias_groups.push_back(std::move(entry));
        }
        matcher.setBiasGroups(std::move(bias_groups));
    }
    {
        std::vector<DarkCalibrationGroup> dark_groups;
        for (const auto& group : model->darkGroups())
        {
            if (group.frames.empty())
            {
                continue;
            }
            DarkCalibrationGroup entry;
            entry.key = DarkGroupKey::fromFrame(group.frames.front());
            entry.groupKey = group.key;
            entry.masterPath = canonicalMasterPathForGroup(model, CalibrationOverrideType::Dark, group);
            dark_groups.push_back(std::move(entry));
        }
        matcher.setDarkGroups(std::move(dark_groups));
    }

    return (type == CalibrationOverrideType::Bias) ? matcher.findBias(lightKey) : matcher.findDark(lightKey, tempC);
}

std::optional<std::pair<GroupKey, QString>> selectedGroupFromTable(const QTableWidget* table)
{
    if (!table)
    {
        return std::nullopt;
    }

    const auto selected = table->selectionModel() ? table->selectionModel()->selectedRows() : QModelIndexList{};
    if (selected.isEmpty())
    {
        return std::nullopt;
    }

    const int row = selected.front().row();
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

    if (!item->data(Qt::UserRole + 1).toBool())
    {
        return std::nullopt;
    }

    return std::make_pair(*groupKey, item->text());
}

QString existingOverridePath(IntegrationDataModel* model, const LightGroupKey& key, CalibrationOverrideType type)
{
    if (!model)
    {
        return {};
    }

    for (const auto& overrideEntry : model->calibrationOverrides())
    {
        const auto& lhs = overrideEntry.lightKey;
        const bool sameKey = lhs.resolution == key.resolution && lhs.isoGain == key.isoGain &&
                             lhs.offset == key.offset && lhs.exposureSeconds == key.exposureSeconds &&
                             lhs.filter == key.filter && lhs.objectName == key.objectName &&
                             lhs.session == key.session && lhs.customTag == key.customTag;
        if (overrideEntry.type == type && sameKey)
        {
            return overrideEntry.masterPath;
        }
    }
    return {};
}

void setOverrideWithUndo(IntegrationDataModel* model, QUndoStack* undoStack, const LightGroupKey& key,
                         CalibrationOverrideType type, const QString& newPath)
{
    if (!model)
    {
        return;
    }

    const QString oldPath = existingOverridePath(model, key, type);
    if (undoStack)
    {
        undoStack->push(new SetCalibrationOverrideCommand(model, key, type, oldPath, newPath));
    }
    else
    {
        if (newPath.isEmpty())
        {
            model->clearCalibrationOverride(key, type);
        }
        else
        {
            model->setCalibrationOverride(key, type, newPath);
        }
    }
}
} // namespace

FlatCalibrationPanel::FlatCalibrationPanel(IntegrationDataModel* model, QWidget* parent, QUndoStack* undoStack)
    : BaseCalibrationPanel(model, parent, undoStack)
{
    initializePanelChrome(QStringLiteral("flatCalibrationPanel"), tr("FLAT Calibration Groups"));

    setupUi();
    updateTable();
}

void FlatCalibrationPanel::setupUi()
{
    m_content = contentWidget();
    auto* contentLayout = new QVBoxLayout();
    contentLayout->setContentsMargins(0, 0, 0, 0);
    setContentLayout(contentLayout);

    auto* footerLayout = new QHBoxLayout();
    m_settingsButton = new QPushButton(tr("Stacking Settings..."), m_content);
    m_settingsButton->setObjectName(QStringLiteral("btnFlatStackingSettings"));
    m_settingsButton->setProperty("anButtonVariant", QStringLiteral("secondary"));
    m_settingsButton->setToolTip(tr("Edit stacking settings for the selected group"));
    m_settingsButton->setEnabled(false);
    QObject::connect(m_settingsButton, &QPushButton::clicked, this, [this]() {
        if (!m_model || !m_table)
        {
            return;
        }

        const auto selected = selectedGroupFromTable(m_table);
        if (!selected)
        {
            return;
        }

        const auto& [key, label] = *selected;
        openStackingSettingsDialog(m_model, FrameType::Flat, key, label, this, m_undoStack);
    });
    footerLayout->addWidget(m_settingsButton);
    m_addButton = new QPushButton(tr("+ Add Master"), m_content);
    m_addButton->setProperty("anButtonVariant", QStringLiteral("primary"));
    m_addButton->setToolTip(tr("Add an existing flat master frame"));
    QObject::connect(m_addButton, &QPushButton::clicked, this, [this]() {
        const QString filter = tr("Calibration Masters (*.fit *.fits *.xisf);;All Files (*.*)");
        openCalibrationMasterFileDialog(this, tr("Select Flat Master"), filter, [this](const QString& path) {
            if (!m_model)
            {
                return;
            }
            if (const std::optional<FrameInfo> master = readExternalMaster(path, FrameType::Flat); !master.has_value())
            {
                return;
            }
            else if (m_undoStack)
            {
                m_undoStack->push(new AddExternalMasterCommand(m_model, FrameType::Flat, *master));
            }
            else
            {
                m_model->addExternalMaster(FrameType::Flat, *master);
            }
        });
    });
    footerLayout->addWidget(m_addButton);
    footerLayout->addStretch();
    // footer placed after the table so buttons sit at the bottom-left

    m_table = new CalibrationGroupTable(m_content);
    m_table->setObjectName(QStringLiteral("tblFlatCalibration"));
    m_table->setColumnCount(8);
    m_table->setHorizontalHeaderLabels({tr("Group"), tr("Frames"), tr("Bias"), tr("Dark"), tr("Master"), tr("Source"),
                                        tr("Stacking"), tr("Normalization")});
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

        openStackingSettingsDialog(m_model, FrameType::Flat, *group_key, item->text(), this, m_undoStack);
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
                const bool stackingAvailable =
                    m_table->item(selectedRows.front().row(), 0) &&
                    m_table->item(selectedRows.front().row(), 0)->data(Qt::UserRole + 1).toBool();
                m_settingsButton->setEnabled(stackingAvailable);
                m_settingsButton->setToolTip(stackingAvailable
                                                 ? tr("Edit stacking settings for the selected group")
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

    // Right-click on Bias/Dark cells to select an override.
    m_table->setContextMenuPolicy(Qt::CustomContextMenu);
    QObject::connect(m_table, &QTableWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        if (!m_model || !m_table)
        {
            return;
        }

        QTableWidgetItem* item = m_table->itemAt(pos);
        if (!item)
        {
            return;
        }

        const int row = item->row();
        const int column = item->column();

        QTableWidgetItem* groupItem = m_table->item(row, 0);
        if (!groupItem)
        {
            return;
        }

        const auto groupKey = GroupKey::fromString(groupItem->data(Qt::UserRole).toString());
        if (!groupKey)
        {
            return;
        }

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
                             openStackingSettingsDialog(m_model, FrameType::Flat, key, label, this, m_undoStack);
                         });

        auto* goToGroupAction = menu.addAction(tr("Go to Group Definition"));
        QObject::connect(goToGroupAction, &QAction::triggered, this,
                         [this, key = *groupKey]() { emit goToGroupDefinitionRequested(key); });

        menu.addSeparator();

        // Column 0: Group -> Reverse lookup
        if (column == 0)
        {
            auto* lightsMenu = menu.addMenu(tr("Lights using this Flat"));

            CalibrationMatcher matcher(m_model->groupingOptions(FrameType::Bias),
                                       m_model->groupingOptions(FrameType::Dark),
                                       m_model->groupingOptions(FrameType::Flat), m_model->calibrationMatchingPolicy());
            matcher.setBiasGroups(collectBiasGroups(m_model));
            matcher.setDarkGroups(collectDarkGroups(m_model));
            matcher.setFlatGroups(collectFlatGroups(m_model));
            matcher.setOverrides(m_model->calibrationOverrides());

            int count = 0;
            const QString flatKeyStr = groupKey->toString();
            for (const auto& group : m_model->lightGroups())
            {
                if (group.frames.empty())
                    continue;
                const auto lightKey = LightGroupKey::fromFrame(group.frames.front());
                if (matcher.findFlat(lightKey).matchedGroupKey == flatKeyStr)
                {
                    QString label =
                        group.key.toDisplayString(m_model->groupingOptions(FrameType::Light), FrameType::Light);
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

            menu.exec(m_table->viewport()->mapToGlobal(pos));
            return;
        }

        if (column != 2 && column != 3)
        {
            menu.exec(m_table->viewport()->mapToGlobal(pos));
            return;
        }

        const auto lightKeyOpt = lightKeyForFlatGroup(m_model, *groupKey);
        if (!lightKeyOpt)
        {
            auto* none = menu.addAction(tr("(No representative flat frame available)"));
            none->setEnabled(false);
            menu.exec(m_table->viewport()->mapToGlobal(pos));
            return;
        }
        const LightGroupKey lightKey = *lightKeyOpt;

        const CalibrationOverrideType type = overrideTypeForColumn(column);
        const QString title = overrideTypeDisplayName(type);

        auto* header = menu.addAction(tr("%1 override").arg(title));
        header->setEnabled(false);

        std::optional<double> tempC;
        for (const auto& g : m_model->flatGroups())
        {
            if (g.key == *groupKey)
            {
                tempC = averageTemperatureC(g.frames);
                break;
            }
        }

        const CalibrationMatch current = currentEffectiveMatch(m_model, lightKey, type, tempC);
        if (!current.masterPath.trimmed().isEmpty())
        {
            auto* currentAction = menu.addAction(tr("Current: %1").arg(QFileInfo(current.masterPath).fileName()));
            currentAction->setEnabled(false);
        }

        menu.addSeparator();

        auto* autoDetect = menu.addAction(tr("Auto-detect"));
        QObject::connect(autoDetect, &QAction::triggered, this, [this, lightKey, type]() {
            setOverrideWithUndo(m_model, m_undoStack, lightKey, type, QString());
            updateTable();
        });

        auto* browse = menu.addAction(tr("Browse file..."));
        QObject::connect(browse, &QAction::triggered, this, [this, lightKey, type]() {
            const QString filter = tr("Calibration Masters (*.fit *.fits);;All Files (*.*)");
            openCalibrationMasterFileDialog(this, overrideDialogTitle(type), filter,
                                            [this, lightKey, type](const QString& path) {
                                                setOverrideWithUndo(m_model, m_undoStack, lightKey, type, path);
                                                updateTable();
                                            });
        });

        auto* doNotUse = menu.addAction(tr("Do not use"));
        doNotUse->setToolTip(tr("Explicitly disable this calibration type for this flat group.\n"
                                "No calibration master will be applied."));
        const bool isCurrentlyUnassigned = (current.type == CalibrationMatch::MatchType::ManuallyUnassigned);
        doNotUse->setCheckable(true);
        doNotUse->setChecked(isCurrentlyUnassigned);
        QObject::connect(doNotUse, &QAction::triggered, this, [this, lightKey, type]() {
            setOverrideWithUndo(m_model, m_undoStack, lightKey, type,
                                QString::fromLatin1(kUnassignedCalibrationMarker));
            updateTable();
        });

        menu.addSeparator();

        const auto candidates = collectOverrideCandidates(m_model, lightKey, type);
        if (candidates.empty())
        {
            auto* none = menu.addAction(tr("(No compatible masters found)"));
            none->setEnabled(false);
        }
        else
        {
            for (const auto& [label, masterPath] : candidates)
            {
                const QString display = tr("%1 - %2").arg(label, QFileInfo(masterPath).fileName());
                auto* action = menu.addAction(display);
                action->setCheckable(true);
                action->setChecked(!current.masterPath.isEmpty() && current.masterPath == masterPath);
                QObject::connect(action, &QAction::triggered, this, [this, lightKey, type, masterPath]() {
                    setOverrideWithUndo(m_model, m_undoStack, lightKey, type, masterPath);
                    updateTable();
                });
            }
        }

        menu.addSeparator();
        QMenu* batchMenu = menu.addMenu(tr("Batch Actions"));

        if (!current.masterPath.isEmpty())
        {
            const QString currentName = QFileInfo(current.masterPath).fileName();
            auto* assignAll = batchMenu->addAction(tr("Assign '%1' to all groups").arg(currentName));
            QObject::connect(assignAll, &QAction::triggered, this, [this, type, path = current.masterPath]() {
                if (m_undoStack)
                {
                    m_undoStack->beginMacro(tr("Assign calibration override to all groups"));
                    for (const auto& g : m_model->flatGroups())
                    {
                        if (!g.frames.empty())
                        {
                            setOverrideWithUndo(m_model, m_undoStack, LightGroupKey::fromFrame(g.frames.front()), type,
                                                path);
                        }
                    }
                    m_undoStack->endMacro();
                }
                else
                {
                    m_model->beginBatchUpdate();
                    for (const auto& g : m_model->flatGroups())
                    {
                        if (!g.frames.empty())
                        {
                            m_model->setCalibrationOverride(LightGroupKey::fromFrame(g.frames.front()), type, path);
                        }
                    }
                    m_model->endBatchUpdate();
                }
            });
        }

        auto* clearAll =
            batchMenu->addAction(tr("Clear %1 overrides for all groups").arg(overrideTypeDisplayName(type)));
        QObject::connect(clearAll, &QAction::triggered, this, [this, type]() {
            if (m_undoStack)
            {
                m_undoStack->beginMacro(tr("Clear calibration overrides for all groups"));
                for (const auto& g : m_model->flatGroups())
                {
                    if (!g.frames.empty())
                    {
                        setOverrideWithUndo(m_model, m_undoStack, LightGroupKey::fromFrame(g.frames.front()), type,
                                            QString());
                    }
                }
                m_undoStack->endMacro();
            }
            else
            {
                m_model->beginBatchUpdate();
                for (const auto& g : m_model->flatGroups())
                {
                    if (!g.frames.empty())
                    {
                        m_model->clearCalibrationOverride(LightGroupKey::fromFrame(g.frames.front()), type);
                    }
                }
                m_model->endBatchUpdate();
            }
        });

        menu.exec(m_table->viewport()->mapToGlobal(pos));
    });
}

void FlatCalibrationPanel::refresh()
{
    updateTable();
}

void FlatCalibrationPanel::setHighlightedGroupKeys(const QSet<GroupKey>& keys)
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

void FlatCalibrationPanel::updateTable()
{
    if (!m_model || !m_table)
    {
        return;
    }

    const QString overrideHint = tr("Right-click to select an override (Auto-detect / Browse / Candidates).");

    m_table->clearEmptyMessage();

    const auto& groups = m_model->flatGroups();
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
        m_table->setEmptyMessage(tr("No flat groups available."));
        return;
    }

    CalibrationMatcher matcher(m_model->groupingOptions(FrameType::Bias), m_model->groupingOptions(FrameType::Dark),
                               m_model->groupingOptions(FrameType::Flat), m_model->calibrationMatchingPolicy());
    matcher.setBiasGroups(collectBiasGroups(m_model));
    matcher.setDarkGroups(collectDarkGroups(m_model));
    matcher.setOverrides(m_model->calibrationOverrides());

    m_table->setRowCount(rows);
    const auto& stackingSettings = m_model->stackingSettings(FrameType::Flat);

    int row = 0;
    for (const auto& group : groups)
    {
        if (group.frames.empty())
        {
            continue;
        }

        const auto& groupingOptions = m_model->groupingOptions(FrameType::Flat);
        const QString groupLabel = formatGroupLabel(group.key, tr("[Ungrouped]"), groupingOptions, FrameType::Flat);
        auto* groupItem = new QTableWidgetItem(groupLabel);
        groupItem->setData(Qt::UserRole, group.key.toString());
        groupItem->setToolTip(groupTooltip(FrameType::Flat, group.key, groupingOptions, group.frames));
        m_table->setItem(row, 0, groupItem);

        auto* framesItem = new QTableWidgetItem(QString::number(group.frames.size()));
        framesItem->setTextAlignment(Qt::AlignCenter);
        framesItem->setToolTip(framesCountTooltip(m_model, FrameType::Flat, group.frames));
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

        const bool isExternalMaster = masterFrame != nullptr && (masterSource == MasterSource::UserDefined ||
                                                                 masterSource == MasterSource::AutoDetected);
        groupItem->setData(Qt::UserRole + 1, !isExternalMaster);

        const CalibrationMatchingPolicy policy = m_model->calibrationMatchingPolicy();

        const LightGroupKey lightKey = LightGroupKey::fromFrame(group.frames.front());
        const std::optional<double> tempC = averageTemperatureC(group.frames);

        const CalibrationMatch darkMatch = matcher.findDark(lightKey, tempC);

        // A master dark holds its bias, in CCD mode too, so a flat takes the bias only when no dark is applied to it:
        // none matched, or the person unassigned it.
        CalibrationMatch biasMatch;
        const bool useBias = flatTakesBias(darkMatch);
        if (useBias)
        {
            biasMatch = matcher.findBias(lightKey);
        }

        QTableWidgetItem* biasItem = nullptr;
        if (isExternalMaster)
        {
            biasItem = new QTableWidgetItem(QStringLiteral("-"));
            biasItem->setToolTip(
                tr("Not applicable: using an existing master flat (no calibration frames are applied)."));
        }
        else if (!useBias)
        {
            biasItem = new QTableWidgetItem(QStringLiteral("-"));
            biasItem->setToolTip(tr("Bias is not used when a Dark master is available: the dark holds the bias, "
                                    "so the flat takes the dark alone. In CCD mode too, where taking the bias from the "
                                    "flat and from the dark leaves the same flat."));
        }
        else
        {
            biasItem = new QTableWidgetItem(calibrationMatchDisplayText(biasMatch, QStringLiteral("-")));
            biasItem->setIcon(calibrationMatchIcon(biasMatch, policy.warnOnFallback));
            const QString biasTip =
                calibrationMatchTooltip(biasMatch, tr("No matching master found."), policy.warnOnFallback);
            biasItem->setToolTip(biasTip.isEmpty() ? overrideHint : (biasTip + QStringLiteral("\n\n") + overrideHint));
        }
        m_table->setItem(row, 2, biasItem);

        QTableWidgetItem* darkItem = nullptr;
        if (isExternalMaster)
        {
            darkItem = new QTableWidgetItem(QStringLiteral("-"));
            darkItem->setToolTip(
                tr("Not applicable: using an existing master flat (no calibration frames are applied)."));
        }
        else if (darkMatch.type == CalibrationMatch::MatchType::None &&
                 biasMatch.type != CalibrationMatch::MatchType::None)
        {
            CalibrationMatch warning;
            warning.type = CalibrationMatch::MatchType::ClosestExposure;
            darkItem = new QTableWidgetItem(QStringLiteral("-"));
            darkItem->setIcon(calibrationMatchIcon(warning, true));
            darkItem->setToolTip(tr("No matching Dark master found. Using Bias instead.\n\nBias: %1\n\n%2")
                                     .arg(calibrationMatchDisplayText(biasMatch, QStringLiteral("-")), overrideHint));
        }
        else
        {
            darkItem = new QTableWidgetItem(calibrationMatchDisplayText(darkMatch, QStringLiteral("-")));
            darkItem->setIcon(calibrationMatchIcon(darkMatch, policy.warnOnFallback));
            const QString darkTip =
                calibrationMatchTooltip(darkMatch, tr("No matching master found."), policy.warnOnFallback);
            darkItem->setToolTip(darkTip.isEmpty() ? overrideHint : (darkTip + QStringLiteral("\n\n") + overrideHint));
        }
        m_table->setItem(row, 3, darkItem);

        if (masterFrame)
        {
            const QString fileName = QFileInfo(masterFrame->displayPath).fileName();
            auto* masterItem = new QTableWidgetItem(fileName.isEmpty() ? masterFrame->displayPath : fileName);
            masterItem->setToolTip(masterFrame->id.canonicalPath());
            m_table->setItem(row, 4, masterItem);

            auto* sourceItem = new QTableWidgetItem(masterSourceLabel(masterSource));
            sourceItem->setToolTip(masterSourceTooltip(masterSource));
            m_table->setItem(row, 5, sourceItem);
        }
        else
        {
            const QString outputDir = m_model ? m_model->outputDirectory() : QString();
            const QString outputFileName = calibrationMasterOutputFilename(CalibrationOverrideType::Flat, group.key,
                                                                           groupingOptions, FrameType::Flat);
            const QString masterPath = calibrationMasterOutputPath(m_model, CalibrationOverrideType::Flat, group.key,
                                                                   groupingOptions, FrameType::Flat);

            if (!outputDir.trimmed().isEmpty() && QFileInfo::exists(masterPath))
            {
                const QString fileName = QFileInfo(masterPath).fileName();
                auto* masterItem = new QTableWidgetItem(fileName.isEmpty() ? masterPath : fileName);
                masterItem->setToolTip(masterPath);
                m_table->setItem(row, 4, masterItem);
                auto* sourceItem = new QTableWidgetItem(masterSourceLabel(MasterSource::AutoGenerated));
                sourceItem->setToolTip(masterSourceTooltip(MasterSource::AutoGenerated));
                m_table->setItem(row, 5, sourceItem);
            }
            else
            {
                auto* masterItem = new QTableWidgetItem(outputFileName);
                masterItem->setToolTip(masterPath.trimmed().isEmpty() ? outputFileName : masterPath);
                m_table->setItem(row, 4, masterItem);

                auto* sourceItem = new QTableWidgetItem(tr("Will Generate"));
                sourceItem->setToolTip(masterSourceTooltip(MasterSource::Unknown));
                m_table->setItem(row, 5, sourceItem);
            }
        }

        const auto it = stackingSettings.find(group.key);
        const StackingSettings effective = (it != stackingSettings.end())
                                               ? normalizeCalibrationStackingSettings(FrameType::Flat, it->second)
                                               : defaultCalibrationStackingSettings(FrameType::Flat);

        const bool showStacking = group.frames.size() > 1;
        const QString tooltip = stackingSettingsTooltip(m_model, FrameType::Flat, group.key, stackingSettings);

        auto* stackingItem =
            new QTableWidgetItem(showStacking ? stackingMethodColumnText(effective) : QStringLiteral("-"));
        stackingItem->setToolTip(tooltip);
        m_table->setItem(row, 6, stackingItem);

        auto* normalizationItem =
            new QTableWidgetItem(showStacking ? normalizationColumnText(effective) : QStringLiteral("-"));
        normalizationItem->setToolTip(tooltip);
        m_table->setItem(row, 7, normalizationItem);

        ++row;
    }

    m_table->resizeColumnsToContents();
}
