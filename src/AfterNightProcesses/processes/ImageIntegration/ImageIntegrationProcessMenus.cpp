// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/**
 * @file ImageIntegrationProcessMenus.cpp
 * @brief Menu bar construction, the recent-projects menu and the preset menu actions.
 *
 * Split out of ImageIntegrationProcess.cpp, the preset and menu helpers being the second piece lifted out of
 * that file.
 */
#include "stdafx.hpp"
#include "processes/ImageIntegration/ImageIntegrationProcess.h"

#include "app/ApplicationStorage.h"
#include "processes/ImageIntegration/ImageIntegrationConstants.h"
#include "processes/ImageIntegration/ImageIntegrationProcessShared.h"
#include "processes/ImageIntegration/model/IntegrationDataModel.h"
#include "processes/ImageIntegration/model/UndoCommands.h"
#include "processes/ImageIntegration/persistence/PresetManager.h"
#include "processes/ImageIntegration/persistence/ProjectManager.h"
#include "processes/ProcessCommonUtils.h"

#include <QAction>
#include <QDir>
#include <QFileInfo>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPointer>
#include <QSettings>

using ImageIntegration::detail::integrationIcon;
using ImageIntegration::detail::openNonBlockingMessageBox;
using ImageIntegration::detail::openNonBlockingQuestionBox;
using ImageIntegration::detail::openNonBlockingTextInput;

void ImageIntegrationProcess::setupMenuBar()
{
    if (!mainMenuBar)
    {
        return;
    }

    const auto iconOrFallback = [](const QString& key) -> QIcon { return integrationIcon(key); };

    mainMenuBar->clear();
    mainMenuBar->setNativeMenuBar(false);

    auto* framesMenu = mainMenuBar->addMenu(tr("Frames"));
    auto* projectMenu = mainMenuBar->addMenu(tr("Project"));
    auto* editMenu = mainMenuBar->addMenu(tr("Edit"));
    auto* runMenu = mainMenuBar->addMenu(tr("Run"));
    auto* navigateMenu = mainMenuBar->addMenu(tr("Navigate"));
    auto* helpMenu = mainMenuBar->addMenu(tr("Help"));

    for (auto* menu : {framesMenu, projectMenu, editMenu, runMenu, navigateMenu, helpMenu})
    {
        if (menu)
        {
            menu->setToolTipsVisible(true);
        }
    }

    const auto setActionTooltip = [](QAction* action, const QString& tooltip) {
        if (!action)
        {
            return;
        }
        action->setToolTip(tooltip);
        action->setStatusTip(tooltip);
    };

    const auto iconForFrameType = [iconOrFallback](FrameType type) {
        switch (type)
        {
        case FrameType::Light:
            return iconOrFallback(QStringLiteral("camera"));
        case FrameType::Bias:
        case FrameType::Dark:
        case FrameType::Flat:
        default:
            return iconOrFallback(QStringLiteral("image_linear"));
        }
    };

    setActionTooltip(framesMenu->menuAction(), tr("Manage loaded frames (import, remove, and navigation)."));
    setActionTooltip(projectMenu->menuAction(), tr("Project operations (import/export, presets, reset)."));
    setActionTooltip(editMenu->menuAction(), tr("Undo/redo changes."));
    setActionTooltip(runMenu->menuAction(), tr("Execute the integration workflow."));
    setActionTooltip(navigateMenu->menuAction(), tr("Navigate between tabs quickly."));
    setActionTooltip(helpMenu->menuAction(), tr("Help and keyboard shortcuts."));

    auto* addFilesMenu = framesMenu->addMenu(tr("Add Files..."));
    addFilesMenu->setToolTipsVisible(true);
    addFilesMenu->menuAction()->setIcon(iconOrFallback(QStringLiteral("file_open")));
    setActionTooltip(addFilesMenu->menuAction(), tr("Add individual files to the project."));
    const auto addFilesForType = [this, addFilesMenu, iconForFrameType](const QString& label, FrameType type) {
        auto* action = addFilesMenu->addAction(label);
        action->setIcon(iconForFrameType(type));
        connect(action, &QAction::triggered, this, [this, type]() { importFilesDialog(type, /*forceToType=*/true); });
        return action;
    };

    setActionTooltip(addFilesForType(tr("Add Bias..."), FrameType::Bias),
                     tr("Add files and treat them as Bias frames."));
    setActionTooltip(addFilesForType(tr("Add Darks..."), FrameType::Dark),
                     tr("Add files and treat them as Dark frames."));
    setActionTooltip(addFilesForType(tr("Add Flats..."), FrameType::Flat),
                     tr("Add files and treat them as Flat frames."));
    setActionTooltip(addFilesForType(tr("Add Lights..."), FrameType::Light),
                     tr("Add files and treat them as Light frames."));
    addFilesMenu->addSeparator();

    m_addFilesAction = addFilesMenu->addAction(tr("Auto-sort by file type..."));
    m_addFilesAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+O")));
    m_addFilesAction->setIcon(iconOrFallback(QStringLiteral("file_open")));
    setActionTooltip(m_addFilesAction, tr("Add files and auto-detect each file's type (Bias/Darks/Flats/Lights)."));

    framesMenu->addSeparator();

    auto* folderThisMenu = framesMenu->addMenu(tr("Add Folder... (This Folder Only)"));
    folderThisMenu->setToolTipsVisible(true);
    folderThisMenu->menuAction()->setIcon(iconOrFallback(QStringLiteral("folder_open")));
    setActionTooltip(folderThisMenu->menuAction(), tr("Add supported files from a folder (non-recursive)."));
    framesMenu->addSeparator();

    auto* folderRecursiveMenu = framesMenu->addMenu(tr("Add Folder... (Recursive)"));
    folderRecursiveMenu->setToolTipsVisible(true);
    folderRecursiveMenu->menuAction()->setIcon(iconOrFallback(QStringLiteral("folder_open")));
    setActionTooltip(folderRecursiveMenu->menuAction(), tr("Add supported files from a folder and all subfolders."));

    const auto addFolderForType = [this, iconForFrameType](QMenu* menu, const QString& label, FrameType type,
                                                           bool recursive) {
        auto* action = menu->addAction(label);
        action->setIcon(iconForFrameType(type));
        connect(action, &QAction::triggered, this,
                [this, type, recursive]() { importFolderDialog(type, /*forceToType=*/true, recursive); });
        return action;
    };

    setActionTooltip(addFolderForType(folderThisMenu, tr("Add Bias..."), FrameType::Bias, false),
                     tr("Pick a folder and add supported files from that folder as Bias frames (non-recursive)."));
    setActionTooltip(addFolderForType(folderThisMenu, tr("Add Darks..."), FrameType::Dark, false),
                     tr("Pick a folder and add supported files from that folder as Dark frames (non-recursive)."));
    setActionTooltip(addFolderForType(folderThisMenu, tr("Add Flats..."), FrameType::Flat, false),
                     tr("Pick a folder and add supported files from that folder as Flat frames (non-recursive)."));
    setActionTooltip(addFolderForType(folderThisMenu, tr("Add Lights..."), FrameType::Light, false),
                     tr("Pick a folder and add supported files from that folder as Light frames (non-recursive)."));
    folderThisMenu->addSeparator();
    auto* autoDetectFolderThis = folderThisMenu->addAction(tr("Auto-sort by file type..."));
    autoDetectFolderThis->setIcon(iconOrFallback(QStringLiteral("folder_open")));
    connect(autoDetectFolderThis, &QAction::triggered, this,
            [this]() { importFolderDialog(currentFrameTypeForActions(), /*forceToType=*/false, /*recursive=*/false); });
    setActionTooltip(
        autoDetectFolderThis,
        tr("Pick a folder and auto-detect each file's type (Bias/Darks/Flats/Lights) from that folder only."));

    setActionTooltip(addFolderForType(folderRecursiveMenu, tr("Add Bias..."), FrameType::Bias, true),
                     tr("Pick a folder and add supported files from that folder and its subfolders as Bias frames."));
    setActionTooltip(addFolderForType(folderRecursiveMenu, tr("Add Darks..."), FrameType::Dark, true),
                     tr("Pick a folder and add supported files from that folder and its subfolders as Dark frames."));
    setActionTooltip(addFolderForType(folderRecursiveMenu, tr("Add Flats..."), FrameType::Flat, true),
                     tr("Pick a folder and add supported files from that folder and its subfolders as Flat frames."));
    setActionTooltip(addFolderForType(folderRecursiveMenu, tr("Add Lights..."), FrameType::Light, true),
                     tr("Pick a folder and add supported files from that folder and its subfolders as Light frames."));
    folderRecursiveMenu->addSeparator();
    m_addDirectoryAction = folderRecursiveMenu->addAction(tr("Auto-sort by file type..."));
    m_addDirectoryAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+O")));
    m_addDirectoryAction->setIcon(iconOrFallback(QStringLiteral("folder_open")));
    setActionTooltip(
        m_addDirectoryAction,
        tr("Pick a folder and auto-detect each file's type (Bias/Darks/Flats/Lights), including subfolders."));

    framesMenu->addSeparator();

    m_clearSelectedAction = framesMenu->addAction(tr("Remove Selected"));
    m_clearSelectedAction->setShortcut(QKeySequence::Delete);
    m_clearSelectedAction->setIcon(iconOrFallback(QStringLiteral("delete")));
    setActionTooltip(m_clearSelectedAction, tr("Remove the selected frames from the current frame list."));

    m_clearAllAction = framesMenu->addAction(tr("Remove All Frames..."));
    m_clearAllAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+Delete")));
    m_clearAllAction->setIcon(iconOrFallback(QStringLiteral("close")));
    setActionTooltip(m_clearAllAction, tr("Remove all loaded frames (Bias/Darks/Flats/Lights)."));

    framesMenu->addSeparator();
    auto* goToGroupAction = framesMenu->addAction(tr("Go to Group..."));
    goToGroupAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+G")));
    connect(goToGroupAction, &QAction::triggered, this, &ImageIntegrationProcess::onGoToGroup);
    setActionTooltip(goToGroupAction, tr("Search for a group and jump to it in the current tree view."));

    m_importProjectAction = projectMenu->addAction(tr("Import Project..."));
    m_importProjectAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+I")));
    m_importProjectAction->setIcon(iconOrFallback(QStringLiteral("file_open")));
    setActionTooltip(m_importProjectAction, tr("Load a saved project (frames, settings, and output directory)."));

    m_exportProjectAction = projectMenu->addAction(tr("Export Project..."));
    m_exportProjectAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+E")));
    m_exportProjectAction->setIcon(iconOrFallback(QStringLiteral("file_save")));
    setActionTooltip(m_exportProjectAction, tr("Save the current project to a file for later reuse."));

    m_recentProjectsMenu = projectMenu->addMenu(tr("Open Recent"));
    m_recentProjectsMenu->setToolTipsVisible(true);
    m_recentProjectsMenu->menuAction()->setIcon(iconOrFallback(QStringLiteral("history")));
    setActionTooltip(m_recentProjectsMenu->menuAction(), tr("Re-open a recently used project."));
    refreshRecentProjectsMenu();

    projectMenu->addSeparator();
    m_presetsMenu = projectMenu->addMenu(tr("Presets"));
    m_presetsMenu->setToolTipsVisible(true);
    setActionTooltip(m_presetsMenu->menuAction(), tr("Save and reuse configuration presets."));
    updatePresetsMenu();

    projectMenu->addSeparator();
    m_resetAction = projectMenu->addAction(tr("Reset Settings..."));
    m_resetAction->setObjectName(QStringLiteral("actionReset"));
    m_resetAction->setIcon(iconOrFallback(QStringLiteral("refresh")));
    setActionTooltip(m_resetAction, tr("Reset settings to defaults (keeps frames and output directory)."));

    m_undoAction = m_undoStack->createUndoAction(this, tr("&Undo"));
    editMenu->addAction(m_undoAction);
    setActionTooltip(m_undoAction, tr("Undo the last change."));

    m_redoAction = m_undoStack->createRedoAction(this, tr("&Redo"));
    editMenu->addAction(m_redoAction);
    setActionTooltip(m_redoAction, tr("Redo the last undone change."));

    m_executeAction = runMenu->addAction(tr("Execute Integration"));
    m_executeAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Enter")));
    m_executeAction->setIcon(iconOrFallback(QStringLiteral("play")));
    setActionTooltip(m_executeAction, tr("Run the integration workflow using the current settings."));

    const std::array<QString, 8> navLabels = {tr("Bias"),        tr("Darks"),        tr("Flats"),    tr("Lights"),
                                              tr("Calibration"), tr("Registration"), tr("Stacking"), tr("Summary")};
    for (int i = 0; i < static_cast<int>(navLabels.size()); ++i)
    {
        auto* action = navigateMenu->addAction(navLabels[static_cast<size_t>(i)]);
        action->setShortcut(QKeySequence(QStringLiteral("Ctrl+%1").arg(i + 1)));
        connect(action, &QAction::triggered, this, [this, i]() { setCurrentLogicalIndex(i); });
        setActionTooltip(action, tr("Switch to the %1 tab.").arg(navLabels[static_cast<size_t>(i)]));
    }

    auto* shortcutsAction = helpMenu->addAction(tr("Keyboard Shortcuts"));
    shortcutsAction->setShortcut(QKeySequence::HelpContents);
    shortcutsAction->setIcon(iconOrFallback(QStringLiteral("help")));
    connect(shortcutsAction, &QAction::triggered, this, &ImageIntegrationProcess::showKeyboardShortcutsHelp);
    setActionTooltip(shortcutsAction, tr("Show a list of available keyboard shortcuts."));

    if (m_addFilesAction)
    {
        m_addFilesAction->setEnabled(true);
    }
    if (m_addDirectoryAction)
    {
        m_addDirectoryAction->setEnabled(true);
    }
}

void ImageIntegrationProcess::persistRecentProjects()
{
    if (!m_projectManager)
    {
        return;
    }

    QSettings settings = an::ui::applicationSettings();
    m_projectManager->saveToSettings(settings);
}

void ImageIntegrationProcess::refreshRecentProjectsMenu()
{
    if (!m_recentProjectsMenu)
    {
        return;
    }

    m_recentProjectsMenu->clear();
    m_clearRecentProjectsAction = nullptr;

    const QStringList recents = m_projectManager ? m_projectManager->recentProjects() : QStringList{};
    if (recents.isEmpty())
    {
        auto* empty = m_recentProjectsMenu->addAction(tr("(No recent projects)"));
        empty->setEnabled(false);
    }
    else
    {
        for (int i = 0; i < recents.size(); ++i)
        {
            const QString path = recents[i];
            const QFileInfo info(path);
            const QString base = info.completeBaseName().trimmed();
            const QString label = base.isEmpty() ? info.fileName() : base;

            auto* action = m_recentProjectsMenu->addAction(tr("&%1 %2").arg(i + 1).arg(label));
            action->setToolTip(path);
            action->setStatusTip(path);
            connect(action, &QAction::triggered, this, [this, path]() { onOpenRecentProject(path); });
        }

        m_recentProjectsMenu->addSeparator();
        m_clearRecentProjectsAction = m_recentProjectsMenu->addAction(tr("Clear Recent Projects"));
        connect(m_clearRecentProjectsAction, &QAction::triggered, this, [this]() {
            if (!m_projectManager)
            {
                return;
            }
            m_projectManager->clearRecentProjects();
            persistRecentProjects();
            refreshRecentProjectsMenu();
        });
    }
}

void ImageIntegrationProcess::onSavePreset()
{
    const QPointer<ImageIntegrationProcess> self(this);
    openNonBlockingTextInput(this, tr("Save Preset"), tr("Preset Name:"), [self](const QString& enteredName) {
        if (!self || !self->m_presetManager || !self->m_model)
        {
            return;
        }

        // Sanitize the name
        const QString name = PresetManager::sanitizeName(enteredName);
        if (name.isEmpty())
        {
            openNonBlockingMessageBox(self, QMessageBox::Warning, self->tr("Save Preset"),
                                      self->tr("Invalid preset name."));
            return;
        }

        const auto savePreset = [self, name]() {
            if (!self || !self->m_presetManager || !self->m_model)
            {
                return;
            }

            if (self->m_presetManager->savePreset(name, *self->m_model))
            {
                self->updatePresetsMenu();
            }
            else
            {
                openNonBlockingMessageBox(self, QMessageBox::Warning, self->tr("Save Preset"),
                                          self->tr("Failed to save preset."), self->m_presetManager->lastError());
            }
        };

        // Check for overwrite
        if (self->m_presetManager->presetExists(name))
        {
            openNonBlockingQuestionBox(
                self, self->tr("Overwrite Preset"),
                self->tr("A preset named '%1' already exists. Do you want to overwrite it?").arg(name),
                [savePreset](const QMessageBox::StandardButton button, bool) {
                    if (button == QMessageBox::Yes)
                    {
                        savePreset();
                    }
                });
            return;
        }

        savePreset();
    });
}

void ImageIntegrationProcess::onLoadPreset(const QString& name)
{
    if (!m_model || !m_presetManager)
    {
        return;
    }

    const auto presetState = m_presetManager->buildAppliedState(name, *m_model);
    if (!presetState.has_value())
    {
        const QString reason = m_presetManager->lastError();
        QString text = tr("Failed to load preset '%1'.").arg(name);
        if (!reason.isEmpty())
        {
            text += QStringLiteral("\n\n") + reason;
        }
        openNonBlockingMessageBox(this, QMessageBox::Warning, tr("Load Preset"), text);
        return;
    }

    const QJsonObject oldState = m_model->toJson();
    if (oldState == *presetState)
    {
        openNonBlockingMessageBox(this, QMessageBox::Information, tr("Load Preset"),
                                  tr("Preset '%1' is already applied.").arg(name));
        return;
    }

    if (m_undoStack)
    {
        m_undoStack->push(new LoadPresetCommand(m_model.get(), oldState, *presetState, name));
        openNonBlockingMessageBox(this, QMessageBox::Information, tr("Load Preset"),
                                  tr("Preset '%1' loaded successfully.").arg(name));
    }
    else
    {
        m_model->fromJson(*presetState);
        openNonBlockingMessageBox(this, QMessageBox::Information, tr("Load Preset"),
                                  tr("Preset '%1' loaded successfully.").arg(name));
    }
}

void ImageIntegrationProcess::onDeletePreset(const QString& name)
{
    const QPointer<ImageIntegrationProcess> self(this);
    openNonBlockingQuestionBox(this, tr("Delete Preset"),
                               tr("Are you sure you want to delete the preset '%1'?").arg(name),
                               [self, name](const QMessageBox::StandardButton button, bool) {
                                   if (button != QMessageBox::Yes || !self || !self->m_presetManager)
                                   {
                                       return;
                                   }

                                   if (self->m_presetManager->deletePreset(name))
                                   {
                                       self->updatePresetsMenu();
                                   }
                                   else
                                   {
                                       openNonBlockingMessageBox(self, QMessageBox::Warning, self->tr("Delete Preset"),
                                                                 self->tr("Failed to delete preset '%1'.").arg(name));
                                   }
                               });
}

void ImageIntegrationProcess::updatePresetsMenu()
{
    if (!m_presetsMenu)
        return;

    m_presetsMenu->clear();

    m_presetsMenu->setToolTipsVisible(true);

    const auto setActionTooltip = [](QAction* action, const QString& tooltip) {
        if (!action)
        {
            return;
        }
        action->setToolTip(tooltip);
        action->setStatusTip(tooltip);
    };

    auto* saveAction = m_presetsMenu->addAction(tr("Save Preset..."));
    setActionTooltip(saveAction, tr("Save the current settings as a named preset."));
    connect(saveAction, &QAction::triggered, this, &ImageIntegrationProcess::onSavePreset);

    auto* loadMenu = m_presetsMenu->addMenu(tr("Load Preset..."));
    loadMenu->setToolTipsVisible(true);
    setActionTooltip(loadMenu->menuAction(), tr("Load a saved preset."));
    auto* deleteMenu = m_presetsMenu->addMenu(tr("Delete Preset..."));
    deleteMenu->setToolTipsVisible(true);
    setActionTooltip(deleteMenu->menuAction(), tr("Delete a saved preset."));

    QStringList presets = m_presetManager->listPresets();
    if (presets.isEmpty())
    {
        auto* emptyLoad = loadMenu->addAction(tr("No presets saved"));
        emptyLoad->setEnabled(false);
        auto* emptyDelete = deleteMenu->addAction(tr("No presets saved"));
        emptyDelete->setEnabled(false);
    }
    else
    {
        for (const auto& name : presets)
        {
            auto* loadAction = loadMenu->addAction(name);
            setActionTooltip(loadAction, tr("Load preset \"%1\".").arg(name));
            connect(loadAction, &QAction::triggered, this, [this, name]() { onLoadPreset(name); });

            auto* deleteAction = deleteMenu->addAction(name);
            setActionTooltip(deleteAction, tr("Delete preset \"%1\".").arg(name));
            connect(deleteAction, &QAction::triggered, this, [this, name]() { onDeletePreset(name); });
        }
    }
}
