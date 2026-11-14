// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightControlsExport.h"

#include <functional>
#include <memory>
#include <string>

#include <QIcon>

class ImageViewControl;
class ImageViewControlManager;
class QScrollArea;
class QWidget;

#include "controls/ViewControlMode.h"

namespace KDDockWidgets::QtWidgets
{
class MainWindow;
}

namespace an::ui
{

class ImageDocument;

struct NewViewLayoutTransitionDecision
{
    bool shouldSwitchToDockManager = false;
    bool shouldRestoreMaximizedActiveView = false;
};

inline NewViewLayoutTransitionDecision
decideNewViewLayoutTransition(const bool standardMode, const bool currentWidgetIsMainDashboard,
                              const bool currentWidgetIsDockManager, const bool hasActiveView,
                              const bool activeViewInFloatingWindow, const bool activeViewMaximized)
{
    NewViewLayoutTransitionDecision decision;
    if (!standardMode)
    {
        return decision;
    }

    if (currentWidgetIsMainDashboard)
    {
        decision.shouldSwitchToDockManager = true;
        return decision;
    }

    if (!currentWidgetIsDockManager)
    {
        if (!hasActiveView)
        {
            decision.shouldSwitchToDockManager = true;
        }
        else
        {
            decision.shouldSwitchToDockManager = !activeViewInFloatingWindow;
            decision.shouldRestoreMaximizedActiveView = decision.shouldSwitchToDockManager && activeViewMaximized;
        }
    }

    return decision;
}

inline bool shouldRollbackCreatedControlAfterLoadFailure(const bool standardMode)
{
    return standardMode;
}

inline std::string resolveCreatedViewHistoryDescription(const std::string& requestedDescription,
                                                        const bool fromSequence)
{
    if (!requestedDescription.empty())
    {
        return requestedDescription;
    }

    return fromSequence ? "Sequence created internally" : "Image created internally";
}

inline const char* iconNameForCreatedView(const bool fromSequence)
{
    return fromSequence ? "film" : "picture";
}

/// Docks a new view as the dock area always took one: as a tab beside `neighbour` when one is given, otherwise on the
/// right of `dockMainWindow`; a view with no dock widget, or with nowhere to go, is left where it is.
AFTERNIGHTCONTROLS_API void dockCreatedView(ImageViewControl& view, ImageViewControl* neighbour,
                                            KDDockWidgets::QtWidgets::MainWindow* dockMainWindow);

class AFTERNIGHTCONTROLS_API ViewCreationCoordinator
{
public:
    struct Dependencies
    {
        ImageViewControlManager* imageViewControlManager = nullptr;
        QScrollArea** mainDashboardScrollArea = nullptr;
        KDDockWidgets::QtWidgets::MainWindow** dockMainWindow = nullptr;

        std::function<QWidget*()> currentCentralWidget;
        std::function<void()> switchToDockManager;
        std::function<QIcon(const std::string&)> iconProvider;
        std::function<void()> updateActiveImageActions;
        std::function<void()> updateActiveImageHistory;
        std::function<void()> updateImageInfoInStatusBar;
        std::function<void(const ImageViewControl*)> notifyViewControlCreated;

        /// Places a view made in the standard mode: beside `neighbour` when one is given, otherwise where the shell
        /// puts a new view. When empty the coordinator docks the view itself, beside its neighbour or on the right of
        /// the dock area, and shows the dock area.
        std::function<void(ImageViewControl& view, ImageViewControl* neighbour, bool keepFocusedNeighbour)> placeView;
    };

    explicit ViewCreationCoordinator(const Dependencies& dependencies);

    ImageViewControl* createNewImageViewControl(const std::string& name, QWidget* parent, ViewControlMode mode,
                                                bool pendingChanges) const;

    // The view factory's two entry points: the view made owns the document, and a document no view is made for is
    // left with the caller.
    ImageViewControl* createNewImageViewControlFromImage(ImageViewControl* customViewControl, ImageDocument&& document,
                                                         QWidget* parent, const std::string& historyDesc,
                                                         ViewControlMode mode, ImageViewControl* neighbour,
                                                         bool keepFocusedNeighbour, bool pendingChanges) const;

    ImageViewControl* createNewImageViewControlFromSequence(ImageViewControl* customViewControl,
                                                            ImageDocument&& document, QWidget* parent, bool autoDebayer,
                                                            const std::string& historyDesc, ViewControlMode mode,
                                                            ImageViewControl* neighbour, bool keepFocusedNeighbour,
                                                            bool pendingChanges) const;

private:
    Dependencies m_dependencies;
};

} // namespace an::ui
