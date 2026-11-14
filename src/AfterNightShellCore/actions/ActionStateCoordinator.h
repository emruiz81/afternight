// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// Which of the catalogue's actions are offered, checked and labelled how, from the image the image actions act on.

#include "AfterNightShellCoreExport.h"

#include "app/ActionLookup.h"
#include "controls/GlobalViewControlActions.h"

#include <QIcon>

#include <functional>
#include <string>
#include <string_view>
#include <vector>

class ImageViewControl;
class QAction;
class QObject;

namespace an::ui::shell
{

/// Whether the image actions are withdrawn: with no view to act on, or no image in it.
[[nodiscard]] inline bool shouldDisableImageActions(const bool hasActiveControl, const bool hasImage)
{
    return !hasActiveControl || !hasImage;
}

/// Whether the mask rendering menu is offered: for a masked image.
[[nodiscard]] inline bool shouldEnableMaskRenderingMenu(const bool isMasked)
{
    return isMasked;
}

/// Sets the state of the catalogue's actions - offered, checked, their labels - and of the menus that hold them, from
/// the image the image actions act on (`subjectView`) and the view the looking actions act on (`lookingView`): an image
/// action is offered while there is an image, a colour action for a colour image, a conversion for any representation
/// but the image's own, a sequence's actions for a frame of a sequence, the mask's for a masked image; Undo and Redo
/// are labelled by the steps the view's history names. A menu is told its state by its id; the shell's menu of that
/// id follows it. An extension's process action follows the guards its launch meets. Used on the GUI thread.
class AFTERNIGHTSHELLCORE_API ActionStateCoordinator
{
public:
    struct Dependencies
    {
        /// The catalogue's actions.
        an::ui::ActionLookup actions;
        /// Offers or withdraws the menu of an id.
        std::function<void(std::string_view menuId, bool enabled)> setMenuEnabled;
        /// The image the image actions act on, and the view the looking actions act on.
        std::function<ImageViewControl*()> subjectView;
        std::function<ImageViewControl*()> lookingView;
        /// The actions that launch a registry descriptor other than their own: an extension's processes.
        std::function<std::vector<QAction*>()> registryActions;
        /// What the label of Maximize View is set against, a turn of the event loop later.
        QObject* timerContext = nullptr;
        /// Whether the application is closing: nothing is set then.
        std::function<bool()> isTerminating;
        /// Whether a maximized view is on screen: the window's views cannot be grouped or split then.
        std::function<bool()> isMaximizedViewActive;
        std::function<QIcon(const std::string& key)> iconProvider;
    };

    /// What a shell answers for the coordinator: its menus' state, and whether it is closing.
    struct Hooks
    {
        std::function<void(std::string_view menuId, bool enabled)> setMenuEnabled;
        std::function<bool()> isTerminating;
    };

    explicit ActionStateCoordinator(Dependencies dependencies);

    /// Replaces the dependencies of the same names.
    void setHooks(Hooks hooks);

    /// An information action of the image actions' view - statistics, histogram, FITS header, EXIF, summary - follows
    /// what the view's own toolbar offers; a view that is not the one the image actions act on changes nothing.
    void setCustomImageActionEnabled(ImageViewControl* view, GlobalViewControlActions action, bool enable) const;

    void setEnableImageActions(bool enable) const;
    void setEnableImageColorActions(bool enable) const;
    void updateRegistryProcessActionStates(ImageViewControl* control) const;
    void updateActiveImageActions() const;
    void updateActiveImageHistory() const;

private:
    [[nodiscard]] QAction* action(std::string_view id) const;
    void setEnabled(std::string_view id, bool enabled) const;
    void setChecked(std::string_view id, bool checked) const;
    void setMenuEnabled(std::string_view menuId, bool enabled) const;

    Dependencies m_dependencies;
};

} // namespace an::ui::shell
