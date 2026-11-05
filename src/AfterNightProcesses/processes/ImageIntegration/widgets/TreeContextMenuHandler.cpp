// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "TreeContextMenuHandler.h"
#include "FrameTreeView.h"
#include "processes/ImageIntegration/model/FrameId.h"
#include "processes/ImageIntegration/model/IntegrationDataModel.h"
#include "processes/ImageIntegration/model/UndoCommands.h"
#include "processes/ImageIntegration/ui_models/GroupTreeModel.h"
#include "processes/ImageIntegration/ImageIntegrationProcessShared.h"

#include <QMenu>
#include <QAction>
#include <QClipboard>
#include <QGuiApplication>
#include <QUrl>

#include "app/DesktopUtils.h"
#include <QFileInfo>
#include <QInputDialog>
#include <QLineEdit>
#include <QMessageBox>
#include <QPointer>
#include <QProcess>
#include <QDir>
#include <QUndoStack>

#include <functional>

#include <verdigris/wobjectimpl.h>

W_OBJECT_IMPL(TreeContextMenuHandler)

namespace
{
/// Asks for a line of text without a nested event loop; `accepted` receives it, trimmed, once the person accepts.
void askTextWithoutBlocking(QWidget* parent, const QString& title, const QString& label, const QString& initial,
                            std::function<void(const QString&)> accepted)
{
    auto* dialog = new QInputDialog(parent);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(title);
    dialog->setLabelText(label);
    dialog->setInputMode(QInputDialog::TextInput);
    dialog->setTextEchoMode(QLineEdit::Normal);
    dialog->setTextValue(initial);
    QObject::connect(dialog, &QInputDialog::textValueSelected, dialog,
                     [accepted = std::move(accepted)](const QString& text) {
                         if (accepted)
                         {
                             accepted(text.trimmed());
                         }
                     });
    dialog->open();
}

void showInFileBrowser(QWidget* parent, const QString& path)
{
    const QFileInfo fileInfo(path);
    if (!fileInfo.exists())
    {
        ImageIntegration::detail::openNonBlockingMessageBox(
            parent, QMessageBox::Warning, QObject::tr("File Not Found"),
            QObject::tr("The file does not exist on disk:\n%1").arg(path));
        return;
    }

    an::ui::revealInFileManager(fileInfo.absoluteFilePath());
}

QStringList collectSelectedFramePaths(GroupTreeModel* model, const QModelIndexList& selectedIndices)
{
    QStringList paths;
    if (!model)
    {
        return paths;
    }

    QSet<QString> seen;
    for (const auto& idx : selectedIndices)
    {
        if (!idx.isValid())
        {
            continue;
        }

        if (model->isFrame(idx))
        {
            const FrameId id = model->getFrameId(idx);
            if (id.isValid())
            {
                const QString path = id.canonicalPath();
                if (!seen.contains(path))
                {
                    seen.insert(path);
                    paths << path;
                }
            }
        }
        else if (model->isGroup(idx))
        {
            const int rows = model->rowCount(idx);
            for (int r = 0; r < rows; ++r)
            {
                const QModelIndex child = model->index(r, GroupTreeModel::ColName, idx);
                const FrameId id = model->getFrameId(child);
                if (!id.isValid())
                {
                    continue;
                }

                const QString path = id.canonicalPath();
                if (!seen.contains(path))
                {
                    seen.insert(path);
                    paths << path;
                }
            }
        }
    }

    return paths;
}

void applyFrameAttributeText(IntegrationDataModel* model, QUndoStack* undoStack, FrameType type,
                             const QStringList& framePaths, FrameAttribute attr, const QString& value)
{
    if (!model || framePaths.isEmpty())
    {
        return;
    }

    std::vector<FrameAttributeChange> changes;
    changes.reserve(framePaths.size());
    for (const auto& path : framePaths)
    {
        const auto frameOpt = model->frames(type).findById(FrameId(path));
        if (!frameOpt.has_value())
        {
            continue;
        }

        QVariant oldValue;
        switch (attr)
        {
        case FrameAttribute::UserSession:
            oldValue = frameOpt->userSession;
            break;
        case FrameAttribute::UserObjectName:
            oldValue = frameOpt->userObjectName;
            break;
        case FrameAttribute::CustomTag:
            oldValue = frameOpt->customTag;
            break;
        default:
            oldValue = QVariant{};
            break;
        }

        if (oldValue != value)
        {
            changes.push_back(FrameAttributeChange{path, oldValue, value});
        }
    }

    if (changes.empty())
    {
        return;
    }

    if (undoStack)
    {
        undoStack->push(new SetFrameAttributesCommand(model, attr, std::move(changes)));
    }
    else
    {
        model->beginBatchUpdate();
        for (const auto& change : changes)
        {
            model->updateFrameAttribute(change.path, attr, change.newValue);
        }
        model->endBatchUpdate();
    }
}

void addSessionObjectOverrideActions(QMenu& menu, QWidget* parentWidget, IntegrationDataModel* dataModel,
                                     QUndoStack* undoStack, FrameType type, GroupTreeModel* treeModel,
                                     const QModelIndexList& selectedIndices)
{
    if (!dataModel || !treeModel)
    {
        return;
    }

    const QStringList framePaths = collectSelectedFramePaths(treeModel, selectedIndices);
    if (framePaths.isEmpty())
    {
        return;
    }

    QString initialSession;
    QString initialObject;
    QString initialCustomTag;
    bool anySessionOverride = false;
    bool anyObjectOverride = false;
    bool anyCustomTag = false;

    for (const auto& path : framePaths)
    {
        const auto frameOpt = dataModel->frames(type).findById(FrameId(path));
        if (!frameOpt.has_value())
        {
            continue;
        }

        if (framePaths.size() == 1)
        {
            initialSession = frameOpt->effectiveSession();
            initialObject = frameOpt->effectiveObjectName();
            initialCustomTag = frameOpt->customTag;
        }

        if (!frameOpt->userSession.trimmed().isEmpty())
        {
            anySessionOverride = true;
        }
        if (!frameOpt->userObjectName.trimmed().isEmpty())
        {
            anyObjectOverride = true;
        }

        if (!frameOpt->customTag.trimmed().isEmpty())
        {
            anyCustomTag = true;
        }
    }

    menu.addSeparator();
    QMenu* overridesMenu = menu.addMenu(QObject::tr("Metadata Overrides"));

    auto* setSessionAction = overridesMenu->addAction(QObject::tr("Set Session..."));
    QObject::connect(setSessionAction, &QAction::triggered, [=]() {
        // Asked without a nested event loop; the frames change once the person accepts.
        const QPointer<IntegrationDataModel> dataModelGuard(dataModel);
        const QPointer<QUndoStack> undoStackGuard(undoStack);
        askTextWithoutBlocking(parentWidget, QObject::tr("Set Session"),
                               QObject::tr("Session override for selected frames.\n"
                                           "Leave empty to use the capture date."),
                               initialSession, [=](const QString& text) {
                                   if (dataModelGuard)
                                   {
                                       applyFrameAttributeText(dataModelGuard.data(), undoStackGuard.data(), type,
                                                               framePaths, FrameAttribute::UserSession, text);
                                   }
                               });
    });

    auto* resetSessionAction = overridesMenu->addAction(QObject::tr("Reset Session Override(s)"));
    resetSessionAction->setEnabled(anySessionOverride);
    QObject::connect(resetSessionAction, &QAction::triggered, [=]() {
        applyFrameAttributeText(dataModel, undoStack, type, framePaths, FrameAttribute::UserSession, QString());
    });

    auto* setObjectAction = overridesMenu->addAction(QObject::tr("Set Object..."));
    QObject::connect(setObjectAction, &QAction::triggered, [=]() {
        // Asked without a nested event loop; the frames change once the person accepts.
        const QPointer<IntegrationDataModel> dataModelGuard(dataModel);
        const QPointer<QUndoStack> undoStackGuard(undoStack);
        askTextWithoutBlocking(parentWidget, QObject::tr("Set Object"),
                               QObject::tr("Object override for selected frames.\n"
                                           "Leave empty to use the file metadata."),
                               initialObject, [=](const QString& text) {
                                   if (dataModelGuard)
                                   {
                                       applyFrameAttributeText(dataModelGuard.data(), undoStackGuard.data(), type,
                                                               framePaths, FrameAttribute::UserObjectName, text);
                                   }
                               });
    });

    auto* resetObjectAction = overridesMenu->addAction(QObject::tr("Reset Object Override(s)"));
    resetObjectAction->setEnabled(anyObjectOverride);
    QObject::connect(resetObjectAction, &QAction::triggered, [=]() {
        applyFrameAttributeText(dataModel, undoStack, type, framePaths, FrameAttribute::UserObjectName, QString());
    });

    overridesMenu->addSeparator();

    auto* setCustomTagAction = overridesMenu->addAction(QObject::tr("Set Custom Tag..."));
    setCustomTagAction->setToolTip(
        QObject::tr("Set a custom tag on the selected frames. This can be used for grouping."));
    QObject::connect(setCustomTagAction, &QAction::triggered, [=]() {
        // Asked without a nested event loop; the frames change once the person accepts.
        const QPointer<IntegrationDataModel> dataModelGuard(dataModel);
        const QPointer<QUndoStack> undoStackGuard(undoStack);
        askTextWithoutBlocking(parentWidget, QObject::tr("Set Custom Tag"),
                               QObject::tr("Custom tag for selected frames.\n"
                                           "Leave empty to clear the tag."),
                               initialCustomTag, [=](const QString& text) {
                                   if (dataModelGuard)
                                   {
                                       applyFrameAttributeText(dataModelGuard.data(), undoStackGuard.data(), type,
                                                               framePaths, FrameAttribute::CustomTag, text);
                                   }
                               });
    });

    auto* resetCustomTagAction = overridesMenu->addAction(QObject::tr("Reset Custom Tag(s)"));
    resetCustomTagAction->setEnabled(anyCustomTag);
    QObject::connect(resetCustomTagAction, &QAction::triggered, [=]() {
        applyFrameAttributeText(dataModel, undoStack, type, framePaths, FrameAttribute::CustomTag, QString());
    });

    overridesMenu->addSeparator();

    auto* resetBothAction = overridesMenu->addAction(QObject::tr("Reset Session + Object Override(s)"));
    resetBothAction->setEnabled(anySessionOverride || anyObjectOverride);
    QObject::connect(resetBothAction, &QAction::triggered, [=]() {
        if (!dataModel || framePaths.isEmpty())
        {
            return;
        }

        if (undoStack)
        {
            undoStack->beginMacro(QObject::tr("Reset metadata overrides"));
            applyFrameAttributeText(dataModel, undoStack, type, framePaths, FrameAttribute::UserSession, QString());
            applyFrameAttributeText(dataModel, undoStack, type, framePaths, FrameAttribute::UserObjectName, QString());
            undoStack->endMacro();
        }
        else
        {
            dataModel->beginBatchUpdate();
            for (const auto& path : framePaths)
            {
                dataModel->updateFrameAttribute(path, FrameAttribute::UserSession, QString());
                dataModel->updateFrameAttribute(path, FrameAttribute::UserObjectName, QString());
            }
            dataModel->endBatchUpdate();
        }
    });

    overridesMenu->addSeparator();

    auto* resetAllAction = overridesMenu->addAction(QObject::tr("Reset Overrides for All Frames..."));
    QObject::connect(resetAllAction, &QAction::triggered, [=]() {
        const QPointer<IntegrationDataModel> dataModelGuard(dataModel);
        const QPointer<QUndoStack> undoStackGuard(undoStack);
        ImageIntegration::detail::openNonBlockingQuestionBox(
            parentWidget, QObject::tr("Reset Overrides"),
            QObject::tr("Reset all Session/Object overrides for all frames in this process?\n"
                        "This does not modify files on disk."),
            [dataModelGuard, undoStackGuard](const QMessageBox::StandardButton answer, bool) {
                if (answer != QMessageBox::Yes || !dataModelGuard)
                {
                    return;
                }
                IntegrationDataModel* dataModel = dataModelGuard.data();
                QUndoStack* undoStack = undoStackGuard.data();
                if (undoStack)
                {
                    std::vector<FrameAttributeChange> sessionChanges;
                    std::vector<FrameAttributeChange> objectChanges;
                    for (FrameType frameType : {FrameType::Bias, FrameType::Dark, FrameType::Flat, FrameType::Light})
                    {
                        for (const auto& frame : dataModel->frames(frameType).frames())
                        {
                            const QString path = frame.id.canonicalPath();
                            if (!frame.userSession.isEmpty())
                            {
                                sessionChanges.push_back(FrameAttributeChange{path, frame.userSession, QString()});
                            }
                            if (!frame.userObjectName.isEmpty())
                            {
                                objectChanges.push_back(FrameAttributeChange{path, frame.userObjectName, QString()});
                            }
                        }
                    }

                    undoStack->beginMacro(QObject::tr("Reset all metadata overrides"));
                    if (!sessionChanges.empty())
                    {
                        undoStack->push(new SetFrameAttributesCommand(dataModel, FrameAttribute::UserSession,
                                                                      std::move(sessionChanges)));
                    }
                    if (!objectChanges.empty())
                    {
                        undoStack->push(new SetFrameAttributesCommand(dataModel, FrameAttribute::UserObjectName,
                                                                      std::move(objectChanges)));
                    }
                    undoStack->endMacro();
                }
                else
                {
                    dataModel->clearAllSessionAndObjectOverrides();
                }
            });
    });
}

std::optional<GroupKey> groupKeyForSelection(GroupTreeModel* model, const QModelIndex& index)
{
    if (!model || !index.isValid())
    {
        return std::nullopt;
    }

    if (model->isGroup(index))
    {
        const GroupKey key = model->getGroupKey(index);
        if (!key.toString().isEmpty())
        {
            return key;
        }
        return std::nullopt;
    }

    if (model->isFrame(index))
    {
        // Prefer the parent group key (stable for a frame).
        if (index.parent().isValid())
        {
            const GroupKey parentKey = model->getGroupKey(index.parent());
            if (!parentKey.toString().isEmpty())
            {
                return parentKey;
            }
        }

        const GroupKey frameKey = model->getGroupKey(index);
        if (!frameKey.toString().isEmpty())
        {
            return frameKey;
        }
    }

    return std::nullopt;
}
} // namespace

TreeContextMenuHandler::TreeContextMenuHandler(IntegrationDataModel* model, QObject* parent, QUndoStack* undoStack)
    : QObject(parent), m_model(model), m_undoStack(undoStack)
{
}

TreeContextMenuHandler::~TreeContextMenuHandler() = default;

void TreeContextMenuHandler::showContextMenu(FrameTreeView* treeView, FrameType frameType, const QPoint& globalPos,
                                             const QModelIndexList& selectedIndices)
{
    if (!treeView)
        return;

    QMenu* menu = createContextMenu(treeView, frameType, selectedIndices);
    if (!menu)
        return;

    if (!menu->isEmpty())
        menu->exec(globalPos);

    menu->deleteLater();
}

QMenu* TreeContextMenuHandler::createContextMenu(FrameTreeView* treeView, FrameType frameType,
                                                 const QModelIndexList& selectedIndices)
{
    if (!treeView)
        return nullptr;

    QMenu* menu = new QMenu(treeView);

    // Check what kind of selection we have
    GroupTreeModel* model = nullptr;
    bool hasSelection = !selectedIndices.isEmpty();
    bool allFrames = true;
    bool allGroups = true;

    if (hasSelection)
    {
        model = qobject_cast<GroupTreeModel*>(const_cast<QAbstractItemModel*>(selectedIndices.first().model()));
        for (const auto& idx : selectedIndices)
        {
            if (model)
            {
                if (!model->isFrame(idx))
                    allFrames = false;
                if (!model->isGroup(idx))
                    allGroups = false;
            }
        }
    }

    if (hasSelection && allFrames)
    {
        buildActionsForFrameType(*menu, frameType, selectedIndices);
    }
    else if (hasSelection && allGroups)
    {
        buildActionsForGroup(*menu, frameType, selectedIndices);
    }
    else if (hasSelection)
    {
        buildActionsForMixedSelection(*menu, frameType, selectedIndices);
    }

    return menu;
}

void TreeContextMenuHandler::buildActionsForFrameType(QMenu& menu, FrameType type,
                                                      const QModelIndexList& selectedIndices)
{
    bool hasSelection = !selectedIndices.isEmpty();
    bool isSingleSelection = selectedIndices.size() == 1;

    // Use QPointer to safely capture model pointer in lambdas
    QPointer<GroupTreeModel> model = nullptr;
    if (hasSelection)
    {
        model = qobject_cast<GroupTreeModel*>(const_cast<QAbstractItemModel*>(selectedIndices.first().model()));
    }

    if (!model)
        return;

    const bool singleIsFrame = isSingleSelection && model->isFrame(selectedIndices.first());

    // Frame Actions

    // Copy Path (single selection only)
    if (singleIsFrame)
    {
        auto* copyPathAction = menu.addAction(tr("Copy File Path"));
        connect(copyPathAction, &QAction::triggered, this, [model, selectedIndices]() {
            if (!model)
                return;
            const QString path = model->data(selectedIndices.first(), Qt::ToolTipRole).toString();
            QGuiApplication::clipboard()->setText(path);
        });
    }

    // Select/Deselect (Include/Exclude) via checkbox state
    {
        const QStringList framePaths = collectSelectedFramePaths(model, selectedIndices);
        bool anyExcluded = false;
        bool anyIncluded = false;
        for (const auto& path : framePaths)
        {
            const QModelIndex idx = model->findIndexForPath(path);
            if (!idx.isValid())
            {
                continue;
            }

            const int cs = model->data(idx.sibling(idx.row(), GroupTreeModel::ColName), Qt::CheckStateRole).toInt();
            if (cs == Qt::Unchecked)
            {
                anyExcluded = true;
            }
            else
            {
                anyIncluded = true;
            }
        }

        // If anything is excluded, offer "Select" (include). Otherwise offer "Deselect" (exclude).
        const bool shouldSelect = anyExcluded;
        const QString label = shouldSelect ? tr("Include in Processing") : tr("Exclude from Processing");
        auto* selectDeselectAction = menu.addAction(label);
        connect(selectDeselectAction, &QAction::triggered, this, [model, framePaths, shouldSelect]() {
            if (!model)
                return;
            // Checkbox checked == included (excluded=false).
            model->setFramesExcluded(framePaths, /*excluded=*/!shouldSelect);
        });
        selectDeselectAction->setToolTip(
            tr("Included frames are used in the workflow; excluded frames are kept for reference but ignored."));

        if (framePaths.isEmpty())
        {
            selectDeselectAction->setEnabled(false);
        }
        else if (!anyExcluded && !anyIncluded)
        {
            selectDeselectAction->setEnabled(false);
        }
    }

    // Show in File Browser
    if (singleIsFrame)
    {
        auto* showInExplorer = menu.addAction(tr("Show in File Browser"));
        connect(showInExplorer, &QAction::triggered, this, [=, this]() {
            if (model)
            {
                QString path = model->data(selectedIndices.first(), Qt::ToolTipRole).toString();
                showInFileBrowser(qobject_cast<QWidget*>(parent()), path);
            }
        });
    }

    // Navigation
    if (singleIsFrame)
    {
        const QModelIndex idx = selectedIndices.first();
        const FrameId selectedId = model->getFrameId(idx);
        const QString framePath = selectedId.isValid() ? selectedId.canonicalPath() : QString();
        const auto groupKeyOpt = groupKeyForSelection(model, idx);
        const QString groupKeyStr = groupKeyOpt.has_value() ? groupKeyOpt->toString() : QString();

        bool isExcluded = false;
        if (type == FrameType::Light && !framePath.isEmpty() && m_model)
        {
            const auto frameOpt = m_model->frames(FrameType::Light).findById(FrameId(framePath));
            if (frameOpt.has_value())
            {
                isExcluded = frameOpt->excluded;
            }
        }

        menu.addSeparator();

        auto* showCal = menu.addAction(tr("Show in Calibration Table"));
        showCal->setEnabled(!groupKeyStr.isEmpty());
        connect(showCal, &QAction::triggered, this, [this, type, groupKeyStr]() {
            if (groupKeyStr.isEmpty())
            {
                return;
            }
            emit showInCalibrationTableRequested(type, groupKeyStr);
        });

        if (type == FrameType::Light)
        {
            auto* showReg = menu.addAction(tr("Show in Registration Table"));
            showReg->setEnabled(!framePath.isEmpty());
            connect(showReg, &QAction::triggered, this, [this, framePath]() {
                if (framePath.isEmpty())
                {
                    return;
                }
                emit showInRegistrationTableRequested(framePath);
            });

            // Excluded frames are ignored by IntegrationGroupEngine and therefore won't exist
            // in the integration groups table.
            if (!isExcluded)
            {
                auto* showInt = menu.addAction(tr("Show in Integration Table"));
                showInt->setEnabled(!framePath.isEmpty());
                connect(showInt, &QAction::triggered, this, [this, framePath]() {
                    if (framePath.isEmpty())
                    {
                        return;
                    }
                    emit showInIntegrationTableRequested(framePath);
                });
            }
        }
    }

    // Blink Selected
    if (selectedIndices.size() >= 2)
    {
        auto* blinkAction = menu.addAction(tr("Blink Selected..."));
        connect(blinkAction, &QAction::triggered, this, [=, this]() {
            const QStringList paths = collectSelectedFramePaths(model, selectedIndices);
            if (!paths.isEmpty())
            {
                emit blinkRequested(type, paths);
            }
        });
    }

    menu.addSeparator();
    // Remove
    auto* removeAction = menu.addAction(tr("Remove Selected"));
    connect(removeAction, &QAction::triggered, this, [this, type, model, selectedIndices]() {
        if (!model)
            return;
        const QStringList paths = collectSelectedFramePaths(model, selectedIndices);
        if (!paths.isEmpty())
            emit removeFramesRequested(type, paths);
    });

    menu.addSeparator();

    // Change Type
    QMenu* changeTypeMenu = menu.addMenu(tr("Change Type"));
    for (auto targetType : {FrameType::Bias, FrameType::Dark, FrameType::Flat, FrameType::Light})
    {
        if (targetType == type)
            continue;

        QString typeName;
        switch (targetType)
        {
        case FrameType::Bias:
            typeName = QStringLiteral("Bias");
            break;
        case FrameType::Dark:
            typeName = QStringLiteral("Dark");
            break;
        case FrameType::Flat:
            typeName = QStringLiteral("Flat");
            break;
        case FrameType::Light:
            typeName = QStringLiteral("Light");
            break;
        }

        auto* action = changeTypeMenu->addAction(typeName);
        connect(action, &QAction::triggered, this, [this, targetType, model, selectedIndices]() {
            if (!model)
                return;
            const QStringList paths = collectSelectedFramePaths(model, selectedIndices);
            if (!paths.isEmpty())
            {
                emit frameTypeChangeRequested(paths, targetType);
            }
        });
    }

    // Master actions
    if (type != FrameType::Light)
    {
        if (singleIsFrame)
        {
            const FrameId selectedId = model->getFrameId(selectedIndices.first());
            const QString singlePath = selectedId.isValid() ? selectedId.canonicalPath() : QString();
            const bool isAlreadyMaster = selectedId.isValid() && m_model && m_model->isMasterFrame(selectedId);

            if (isAlreadyMaster)
            {
                auto* unsetMasterAction = menu.addAction(tr("Unset as Master"));
                unsetMasterAction->setToolTip(tr("Remove the master designation from this frame. "
                                                 "The group will generate a new master during stacking."));
                connect(unsetMasterAction, &QAction::triggered, this, [this, singlePath]() {
                    if (singlePath.isEmpty())
                    {
                        return;
                    }
                    emit masterStatusChangeRequested(singlePath, false);
                });
            }
            else
            {
                auto* setMasterAction = menu.addAction(tr("Set as Master"));
                setMasterAction->setToolTip(tr("Mark this frame as an existing calibration master. "
                                               "Stacking will use this file instead of generating a new master."));
                connect(setMasterAction, &QAction::triggered, this, [this, singlePath]() {
                    if (singlePath.isEmpty())
                    {
                        return;
                    }
                    emit masterStatusChangeRequested(singlePath, true);
                });
            }
        }
    }
    else
    {
        // Light actions
        if (singleIsFrame)
        {
            auto* setRefAction = menu.addAction(tr("Set as Registration Reference"));
            setRefAction->setToolTip(
                tr("Use this frame as the alignment target for all frames in the same registration group "
                   "(same dimensions, channels, and binning). This does not affect normalization."));
            connect(setRefAction, &QAction::triggered, this, [this, model, selectedIndices]() {
                if (!model)
                    return;
                QString path = model->data(selectedIndices.first(), Qt::ToolTipRole).toString();
                emit referenceFrameRequested(path);
            });
        }
    }

    addSessionObjectOverrideActions(menu, qobject_cast<QWidget*>(parent()), m_model, m_undoStack, type, model,
                                    selectedIndices);
}

void TreeContextMenuHandler::buildActionsForGroup(QMenu& menu, FrameType type, const QModelIndexList& selectedIndices)
{
    // Group-level context menu actions

    // Navigation (single group only)
    {
        QPointer<GroupTreeModel> model = nullptr;
        if (!selectedIndices.isEmpty())
        {
            model = qobject_cast<GroupTreeModel*>(const_cast<QAbstractItemModel*>(selectedIndices.first().model()));
        }

        if (model && selectedIndices.size() == 1 && model->isGroup(selectedIndices.first()))
        {
            const auto groupKeyOpt = groupKeyForSelection(model, selectedIndices.first());
            const QString groupKeyStr = groupKeyOpt.has_value() ? groupKeyOpt->toString() : QString();

            auto* showCal = menu.addAction(tr("Show in Calibration Table"));
            showCal->setEnabled(!groupKeyStr.isEmpty());
            connect(showCal, &QAction::triggered, this, [this, type, groupKeyStr]() {
                if (groupKeyStr.isEmpty())
                {
                    return;
                }
                emit showInCalibrationTableRequested(type, groupKeyStr);
            });

            menu.addSeparator();
        }
    }

    // Expand All
    auto* expandAction = menu.addAction(tr("Expand All"));
    connect(expandAction, &QAction::triggered, this, [this]() { emit expandAllRequested(); });

    // Collapse All
    auto* collapseAction = menu.addAction(tr("Collapse All"));
    connect(collapseAction, &QAction::triggered, this, [this]() { emit collapseAllRequested(); });

    menu.addSeparator();

    // Select/Deselect (Include/Exclude) all frames in selected groups
    {
        QPointer<GroupTreeModel> model = nullptr;
        if (!selectedIndices.isEmpty())
        {
            model = qobject_cast<GroupTreeModel*>(const_cast<QAbstractItemModel*>(selectedIndices.first().model()));
        }

        const QStringList framePaths = collectSelectedFramePaths(model, selectedIndices);
        bool anyExcluded = false;
        bool anyIncluded = false;
        for (const auto& path : framePaths)
        {
            const QModelIndex idx = model ? model->findIndexForPath(path) : QModelIndex();
            if (!idx.isValid())
            {
                continue;
            }

            const int cs = model->data(idx.sibling(idx.row(), GroupTreeModel::ColName), Qt::CheckStateRole).toInt();
            if (cs == Qt::Unchecked)
            {
                anyExcluded = true;
            }
            else
            {
                anyIncluded = true;
            }
        }

        const bool shouldSelect = anyExcluded;
        auto* selectDeselectAction =
            menu.addAction(shouldSelect ? tr("Include in Processing") : tr("Exclude from Processing"));
        connect(selectDeselectAction, &QAction::triggered, this, [model, framePaths, shouldSelect]() {
            if (!model)
                return;
            // Checkbox checked == included (excluded=false).
            model->setFramesExcluded(framePaths, /*excluded=*/!shouldSelect);
        });
        selectDeselectAction->setToolTip(
            tr("Included frames are used in the workflow; excluded frames are kept for reference but ignored."));

        if (framePaths.isEmpty() || (!anyExcluded && !anyIncluded))
        {
            selectDeselectAction->setEnabled(false);
        }
    }

    menu.addSeparator();

    // Remove Group (removes all frames in the group)
    QPointer<GroupTreeModel> model = nullptr;
    if (!selectedIndices.isEmpty())
    {
        model = qobject_cast<GroupTreeModel*>(const_cast<QAbstractItemModel*>(selectedIndices.first().model()));
    }

    auto* removeGroupAction = menu.addAction(tr("Remove Group"));
    connect(removeGroupAction, &QAction::triggered, this, [this, type, model, selectedIndices]() {
        if (!model)
            return;

        const QStringList paths = collectSelectedFramePaths(model, selectedIndices);
        if (!paths.isEmpty())
        {
            emit removeFramesRequested(type, paths);
        }
    });

    addSessionObjectOverrideActions(menu, qobject_cast<QWidget*>(parent()), m_model, m_undoStack, type, model,
                                    selectedIndices);
}

void TreeContextMenuHandler::buildActionsForMixedSelection(QMenu& menu, FrameType type,
                                                           const QModelIndexList& selectedIndices)
{
    QPointer<GroupTreeModel> model = nullptr;
    if (!selectedIndices.isEmpty())
    {
        model = qobject_cast<GroupTreeModel*>(const_cast<QAbstractItemModel*>(selectedIndices.first().model()));
    }

    if (!model)
    {
        return;
    }

    const QStringList framePaths = collectSelectedFramePaths(model, selectedIndices);
    bool anyExcluded = false;
    bool anyIncluded = false;
    for (const auto& path : framePaths)
    {
        const QModelIndex idx = model->findIndexForPath(path);
        if (!idx.isValid())
        {
            continue;
        }

        const int cs = model->data(idx.sibling(idx.row(), GroupTreeModel::ColName), Qt::CheckStateRole).toInt();
        if (cs == Qt::Unchecked)
        {
            anyExcluded = true;
        }
        else
        {
            anyIncluded = true;
        }
    }

    const bool shouldSelect = anyExcluded;
    auto* selectDeselectAction =
        menu.addAction(shouldSelect ? tr("Include in Processing") : tr("Exclude from Processing"));
    connect(selectDeselectAction, &QAction::triggered, this, [model, framePaths, shouldSelect]() {
        if (!model)
            return;
        model->setFramesExcluded(framePaths, /*excluded=*/!shouldSelect);
    });
    selectDeselectAction->setToolTip(
        tr("Included frames are used in the workflow; excluded frames are kept for reference but ignored."));

    if (framePaths.isEmpty() || (!anyExcluded && !anyIncluded))
    {
        selectDeselectAction->setEnabled(false);
    }

    menu.addSeparator();

    auto* removeAction = menu.addAction(tr("Remove Selected"));
    connect(removeAction, &QAction::triggered, this, [this, type, framePaths]() {
        if (!framePaths.isEmpty())
        {
            emit removeFramesRequested(type, framePaths);
        }
    });
    if (framePaths.isEmpty())
    {
        removeAction->setEnabled(false);
    }

    menu.addSeparator();

    QMenu* changeTypeMenu = menu.addMenu(tr("Change Type"));
    for (auto targetType : {FrameType::Bias, FrameType::Dark, FrameType::Flat, FrameType::Light})
    {
        if (targetType == type)
            continue;

        QString typeName;
        switch (targetType)
        {
        case FrameType::Bias:
            typeName = QStringLiteral("Bias");
            break;
        case FrameType::Dark:
            typeName = QStringLiteral("Dark");
            break;
        case FrameType::Flat:
            typeName = QStringLiteral("Flat");
            break;
        case FrameType::Light:
            typeName = QStringLiteral("Light");
            break;
        }

        auto* action = changeTypeMenu->addAction(typeName);
        connect(action, &QAction::triggered, this, [this, framePaths, targetType]() {
            if (!framePaths.isEmpty())
            {
                emit frameTypeChangeRequested(framePaths, targetType);
            }
        });
        if (framePaths.isEmpty())
        {
            action->setEnabled(false);
        }
    }

    addSessionObjectOverrideActions(menu, qobject_cast<QWidget*>(parent()), m_model, m_undoStack, type, model,
                                    selectedIndices);
}
