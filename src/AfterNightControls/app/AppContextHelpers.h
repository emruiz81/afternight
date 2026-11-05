// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "controls/IAppContext.h"
#include "controls/ViewControlMode.h"

#include <QCoreApplication>
#include <QMetaObject>
#include <QObject>
#include <QString>
#include <QThread>
#include <QWidget>

#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

class ImageViewControl;

namespace an::ui
{
class IEngineSessionProvider;
class ImageDocument;
} // namespace an::ui

namespace process_utils
{

[[nodiscard]] inline an::ui::IIconProviderPtr currentIconProvider()
{
    if (auto* context = an::ui::appContext())
    {
        return context->getIconProvider();
    }

    return {};
}

[[nodiscard]] inline an::ui::IDialogService* currentDialogService()
{
    if (auto* context = an::ui::appContext())
    {
        return context->getDialogService();
    }

    return nullptr;
}

[[nodiscard]] inline an::ui::IAppEventBus* currentAppEventBus()
{
    if (auto* context = an::ui::appContext())
    {
        return context->getAppEventBus();
    }

    return nullptr;
}

[[nodiscard]] inline an::ui::IGlobalSettingsProvider* currentGlobalSettings()
{
    if (auto* context = an::ui::appContext())
    {
        return context->getGlobalSettings();
    }

    return nullptr;
}

[[nodiscard]] inline an::ui::IEngineSessionProvider* currentEngineSessionProvider()
{
    if (auto* context = an::ui::appContext())
    {
        return context->getEngineSessionProvider();
    }

    return nullptr;
}

[[nodiscard]] inline an::ui::IViewFactory* currentViewFactory()
{
    if (auto* context = an::ui::appContext())
    {
        return context->getViewFactory();
    }

    return nullptr;
}

[[nodiscard]] inline an::ui::IViewRegistry* currentViewRegistry()
{
    if (auto* context = an::ui::appContext())
    {
        return context->getViewRegistry();
    }

    return nullptr;
}

[[nodiscard]] inline an::ui::IViewControlManager* currentViewControlManager()
{
    if (auto* context = an::ui::appContext())
    {
        return context->getViewControlManager();
    }

    return nullptr;
}

[[nodiscard]] inline QWidget* currentMainWindow()
{
    if (auto* context = an::ui::appContext())
    {
        return context->getMainWindow();
    }

    return nullptr;
}

[[nodiscard]] inline QObject* currentGuiThreadTarget()
{
    if (auto* mainWindow = currentMainWindow())
    {
        return mainWindow;
    }

    return QCoreApplication::instance();
}

[[nodiscard]] inline std::vector<ImageViewControl*> listViewControls()
{
    if (auto* viewControlManager = currentViewControlManager())
    {
        return viewControlManager->getListOfViewControls();
    }

    return {};
}

[[nodiscard]] inline ImageViewControl* currentActiveViewControl()
{
    if (auto* viewRegistry = currentViewRegistry())
    {
        return viewRegistry->getActiveImageViewControl();
    }

    return nullptr;
}

inline void invokeOnObjectThread(QObject* target, const std::function<void()>& func,
                                 const Qt::ConnectionType connectionType = Qt::QueuedConnection)
{
    if (!target)
    {
        return;
    }

    if (QThread::currentThread() == target->thread())
    {
        func();
        return;
    }

    QMetaObject::invokeMethod(target, func, connectionType);
}

inline void messageBoxYesNo(QWidget* parent, const QString& title, const QString& text, const bool warn, int* ret)
{
    if (auto* dialogService = currentDialogService())
    {
        dialogService->messageBoxYesNo(parent, title, text, warn, ret);
    }
}

// Makes a view of a document on the GUI thread, from whichever thread asks: a worker waits while the GUI thread makes
// it. The view owns the document once it is made; when none is made - no context, or a factory that refuses - the
// document is left with the caller. The document is moved from on the GUI thread while the caller waits, which is why
// it is captured by reference. Never call it from a task on the global thread pool: the application's closing waits
// for that pool on the GUI thread, so a task waiting there for the GUI thread is never answered and the closing hangs.
inline ImageViewControl*
createImageViewOnUiThread(ImageViewControl* customViewControl, an::ui::ImageDocument&& document,
                          QWidget* parent = nullptr, const std::string& historyDesc = "",
                          const ViewControlMode mode = ViewControlMode::standard, ImageViewControl* neighbour = nullptr,
                          const bool keepFocusedNeighbour = false, const bool pendingChanges = true)
{
    ImageViewControl* createdView = nullptr;

    invokeOnObjectThread(
        currentGuiThreadTarget(),
        [&createdView, &document, customViewControl, parent, historyDesc, mode, neighbour, keepFocusedNeighbour,
         pendingChanges]() {
            if (auto* viewFactory = currentViewFactory())
            {
                createdView = viewFactory->createNewImageViewControlFromImage(customViewControl, std::move(document),
                                                                              parent, historyDesc, mode, neighbour,
                                                                              keepFocusedNeighbour, pendingChanges);
            }
        },
        Qt::BlockingQueuedConnection);

    return createdView;
}

// Makes a view of a sequence on the GUI thread, as createImageViewOnUiThread does: the document states the sequence it
// shows a frame of and holds that frame.
inline ImageViewControl* createSequenceViewOnUiThread(
    ImageViewControl* customViewControl, an::ui::ImageDocument&& document, QWidget* parent = nullptr,
    const bool autoDebayer = false, const std::string& historyDesc = "",
    const ViewControlMode mode = ViewControlMode::standard, ImageViewControl* neighbour = nullptr,
    const bool keepFocusedNeighbour = false, const bool pendingChanges = true)
{
    ImageViewControl* createdView = nullptr;

    invokeOnObjectThread(
        currentGuiThreadTarget(),
        [&createdView, &document, customViewControl, parent, autoDebayer, historyDesc, mode, neighbour,
         keepFocusedNeighbour, pendingChanges]() {
            if (auto* viewFactory = currentViewFactory())
            {
                createdView = viewFactory->createNewImageViewControlFromSequence(
                    customViewControl, std::move(document), parent, autoDebayer, historyDesc, mode, neighbour,
                    keepFocusedNeighbour, pendingChanges);
            }
        },
        Qt::BlockingQueuedConnection);

    return createdView;
}

inline void notifyErrorMessage(QWidget* parent, const QString& title, const QString& text)
{
    if (auto* dialogService = currentDialogService())
    {
        dialogService->notifyErrorMessage(parent, title, text);
    }
}

inline void notifyErrorMessageAsync(QWidget* parent, const QString& title, const QString& text)
{
    if (auto* dialogService = currentDialogService())
    {
        dialogService->notifyErrorMessageAsync(parent, title, text);
    }
}

[[nodiscard]] inline QString selectGlobalSetting(const QString& key, const QString& defaultValue = {})
{
    if (auto* globalSettings = currentGlobalSettings())
    {
        return globalSettings->select(key, defaultValue);
    }

    return defaultValue;
}

} // namespace process_utils
