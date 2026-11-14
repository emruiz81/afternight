// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// What the catalogue's actions do when they are triggered, and the icons they show.

#include "AfterNightShellCoreExport.h"

#include <QIcon>
#include <QString>

#include <functional>
#include <string>
#include <string_view>

class QAction;
class QObject;

namespace an::ui
{
class ImageOperationsCoordinator;
class MaskOverlayCoordinator;
} // namespace an::ui

namespace an::ui::shell
{

class ActionCatalogue;

/// Connects the catalogue's actions to what they do, once, on the GUI thread: the files' actions to the file workflows,
/// the image's to the image operations and the mask overlay, a process's to the registry's descriptor it launches, and
/// the rest to the targets the application gives. A target is asked when its action is triggered, so the targets can be
/// given and changed after the actions are connected; an action whose target is empty does nothing.
///
/// Binding a process's action names its registry descriptor by the action's text - the name the menus give a process -
/// and leaves the name its process attaches to a view under, unless that name was the action's own; the action is
/// offered while the descriptor is available, or its environment can be prepared, and says why it is not.
class AFTERNIGHTSHELLCORE_API ActionBindings
{
public:
    /// What the actions act on.
    struct Targets
    {
        std::function<void()> openFile;
        /// Save as and Save File.
        std::function<void()> saveFile;
        std::function<void()> newSequence;
        /// Exit: the shell's window closes, asking first as it does.
        std::function<void()> exit;
        std::function<an::ui::ImageOperationsCoordinator*()> imageOperations;
        std::function<an::ui::MaskOverlayCoordinator*()> maskOverlay;
        /// Launches a registry descriptor; whether it was launched.
        std::function<bool(const std::string& descriptorId)> launch;
        /// The Session Console shown or hidden, as the action's check says.
        std::function<void(bool visible)> sessionConsole;
        std::function<void()> userGuide;
        std::function<void()> about;
        std::function<void()> systemInformation;
        std::function<void()> settings;
        std::function<void()> extensionManager;
        std::function<void()> lockLayout;
        std::function<void()> unlockLayout;
        std::function<void()> groupAll;
        std::function<void()> splitAll;
    };

    /// @param signalContext what the connections live as long as: the actions stop reaching their targets when it goes.
    ActionBindings(ActionCatalogue& catalogue, QObject& signalContext);

    ActionBindings(const ActionBindings&) = delete;
    ActionBindings& operator=(const ActionBindings&) = delete;
    ActionBindings(ActionBindings&&) = delete;
    ActionBindings& operator=(ActionBindings&&) = delete;

    /// Replaces the targets.
    void setTargets(Targets targets);

    /// Connects every action but the registry's processes, the first time it is asked.
    void connectActions();

    /// Binds every process action of the catalogue to its registry descriptor, the first time it is asked.
    void connectProcessActions();

    /// Binds an action to the registry's descriptor `descriptorId` - or, when none is given, to the one its
    /// `anRegistryDescriptorId` property names, or else `<category>.<object name>` - naming the descriptor by the
    /// action's text; an action bound already is not connected again, only its state is brought up to date. An action
    /// whose descriptor the registry does not hold is withdrawn and says it is still loading.
    void bindRegistryAction(QAction* action, std::string_view category, std::string_view descriptorId = {}) const;

    /// Withdraws an action whose descriptor the registry does not hold yet, saying it is still loading.
    static void markRegistryActionPending(QAction* action);

    /// Gives every action of the catalogue the icon of its key; an action of no key shows none.
    void setActionIcons(const std::function<QIcon(const QString& key)>& icons) const;

private:
    void launch(const std::string& descriptorId) const;

    ActionCatalogue& m_catalogue;
    QObject& m_signalContext;
    Targets m_targets;
    bool m_actionsConnected = false;
    bool m_processActionsConnected = false;
};

} // namespace an::ui::shell
