// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// The application's views: the list of them, the view an action acts on, how a new one is made, and what the shell is
/// told of each.

#include "AfterNightShellCoreExport.h"

#include "controls/IAppContext.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

class ImageViewControl;
class ImageViewControlManager;
class QContextMenuEvent;
class QRecursiveMutex;
class QWidget;

namespace an::ui::shell
{

class ApplicationCore;

/// The views the application holds, which it answers as the view factory, the view registry and the view control
/// manager of the application context; the shell is told of each through its `IViewHost` and shows the active view's
/// state through its `IStatusDisplay`.
///
/// **Who asked for a view.** A view made through the service as `IViewFactory` is a process's, a run's or an
/// extension's (`ViewOrigin::Process`): the shell keeps its place on screen. A view made through `userViews()` - the
/// factory the file workflows and the actions are handed - is the person's (`ViewOrigin::User`): the shell is asked to
/// show its views right after it is told of the view. A view the shell is told of outside a factory's call is a
/// process's.
///
/// **What an action acts on.** `subjectView()` is the image the image actions act on and `lookingView()` the view the
/// looking actions act on: both the active view, unless a shell showing a task named the task's target and canvas.
///
/// Used on the GUI thread; the list is changed under a lock, which a reader on another thread takes.
class AFTERNIGHTSHELLCORE_API ViewService final : public an::ui::IViewFactory,
                                                  public an::ui::IViewControlManager,
                                                  public an::ui::IViewRegistry
{
public:
    /// What a shell's window does around a view beyond what the shell contract asks: the views' list as the window
    /// shows it, what it shows of a view that changed, and the view's context menu. The state of the actions and the
    /// extension sessions follow the views through the core's own units.
    struct Hooks
    {
        /// The window's list of the views is made again from the service's.
        std::function<void()> refreshViewList;
        std::function<void(const ImageViewControl*)> viewUpdated;
        std::function<void(ImageViewControl*, QContextMenuEvent*)> contextMenu;
        /// Whether the application is closing: a view closed then is not logged.
        std::function<bool()> isTerminating;
    };

    explicit ViewService(ApplicationCore& core);
    ~ViewService() override;

    ViewService(const ViewService&) = delete;
    ViewService& operator=(const ViewService&) = delete;
    ViewService(ViewService&&) = delete;
    ViewService& operator=(ViewService&&) = delete;

    /// Replaces the hooks; an empty one does nothing.
    void setHooks(Hooks hooks);

    /// The list of views.
    [[nodiscard]] ImageViewControlManager& manager() noexcept;

    /// The lock the list is changed under, which a reader of another thread takes.
    [[nodiscard]] QRecursiveMutex& managerLock() noexcept;

    /// The factory of the views the person asks for: the file workflows' and the actions'.
    [[nodiscard]] an::ui::IViewFactory& userViews() noexcept;

    /// Makes the active view the one named; a name no view has changes nothing.
    void setActiveViewByName(const std::string& name);

    void updateActiveImageActions();
    void updateActiveImageHistory();
    void refreshViewList();

    // ---- What an action acts on

    /// The view the image actions act on.
    [[nodiscard]] ImageViewControl* subjectView();

    /// The view the looking actions act on.
    [[nodiscard]] ImageViewControl* lookingView();

    /// Names the target and the canvas of the task a shell shows; both stay the subject and the looking view until the
    /// shell clears them, or one of them goes.
    void setTaskSubject(ImageViewControl* target, ImageViewControl* canvas);

    /// The subject and the looking view are the active view again.
    void clearTaskSubject();

    // ---- IViewFactory: a process's views
    [[nodiscard]] ImageViewControl*
    createNewImageViewControlFromImage(ImageViewControl* customViewControl, an::ui::ImageDocument&& document,
                                       QWidget* parent, const std::string& historyDesc, ViewControlMode mode,
                                       ImageViewControl* neighbour, bool keepFocusedNeighbour,
                                       bool pendingChanges) override;
    [[nodiscard]] ImageViewControl*
    createNewImageViewControlFromSequence(ImageViewControl* customViewControl, an::ui::ImageDocument&& document,
                                          QWidget* parent, bool autoDebayer, const std::string& historyDesc,
                                          ViewControlMode mode, ImageViewControl* neighbour, bool keepFocusedNeighbour,
                                          bool pendingChanges) override;

    // ---- IViewRegistry
    [[nodiscard]] ImageViewControl* getActiveImageViewControl() override;
    void setActiveImageViewControl(ImageViewControl* view) override;

    // ---- IViewControlManager
    void addViewControl(ImageViewControl* view) override;
    void removeViewControl(ImageViewControl* view, bool deleteObject) override;
    void renameViewControl(ImageViewControl* view) override;
    void maximizeViewControl(ImageViewControl* ctrl) override;
    void restoreViewControl(ImageViewControl* ctrl) override;
    void notifyViewControlUpdated(const ImageViewControl* ctrl) override;
    void createContextMenuForViewControl(ImageViewControl* view, QContextMenuEvent* event) override;
    [[nodiscard]] bool supportsDockWidgets() const override;
    [[nodiscard]] ImageViewControl* getViewControlByName(const std::string& name) override;
    [[nodiscard]] std::vector<ImageViewControl*> getListOfViewControls() override;
    [[nodiscard]] std::string createViewControlUniqueName(const std::string& name) override;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace an::ui::shell
