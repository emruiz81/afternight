// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// What the work a thread hands the GUI thread does once it gets there.

#include "AfterNightShellCoreExport.h"

#include "controls/ViewControlMode.h"

#include <QString>

#include <functional>
#include <memory>
#include <string>

class ImageViewControl;
class QWidget;

namespace an::consumer
{
class ImageAsset;
}

namespace an::ui
{
class ImageDocument;
}

namespace an::ui::shell
{

class ApplicationCore;

/// What each of the application core's marshalled events asks for, on the GUI thread: the parts of the application
/// that make, add and remove views, show messages and keep the recent files.
///
/// The core is given one target (`ApplicationCore::setEventTarget`) and runs every such event against it; while it has
/// none, an event asks nothing - a view-making event closes the document it carries. `ServiceEventTarget` answers them
/// over the application's services.
class AFTERNIGHTSHELLCORE_API IApplicationEventTarget
{
public:
    virtual ~IApplicationEventTarget() = default;

    /// Makes a view of an image document; a document no view is made for is left with the caller as it was handed in.
    virtual ImageViewControl* createNewImageViewControlFromImage(ImageViewControl* customViewControl,
                                                                 an::ui::ImageDocument&& document, QWidget* parent,
                                                                 const std::string& historyDesc, ViewControlMode mode,
                                                                 ImageViewControl* neighbour, bool keepFocusedNeighbour,
                                                                 bool pendingChanges) = 0;

    /// Makes a view of a sequence document, as `createNewImageViewControlFromImage` does an image's.
    virtual ImageViewControl* createNewImageViewControlFromSequence(ImageViewControl* customViewControl,
                                                                    an::ui::ImageDocument&& document, QWidget* parent,
                                                                    bool autoDebayer, const std::string& historyDesc,
                                                                    ViewControlMode mode, ImageViewControl* neighbour,
                                                                    bool keepFocusedNeighbour, bool pendingChanges) = 0;

    virtual void addImageViewControl(ImageViewControl* view) = 0;
    virtual void removeImageViewControl(ImageViewControl* view, bool deleteObject) = 0;
    virtual void updateImageViewControlList() = 0;
    virtual void updateActiveImageActions() = 0;
    virtual void addRecentFile(const std::string& fileName,
                               const std::shared_ptr<const an::consumer::ImageAsset>& image) = 0;
    virtual void removeRecentFile(const std::string& fileName) = 0;
    virtual void notifyErrorMessage(QWidget* parent, const QString& title, const QString& text) = 0;
    virtual void notifyWarnMessage(QWidget* parent, const QString& title, const QString& text) = 0;
    virtual void notifyInfoMessage(QWidget* parent, const QString& title, const QString& text) = 0;
    virtual void messageBoxYesNo(QWidget* parent, const QString& title, const QString& text, bool warn, int* ret) = 0;
    virtual void setActiveImageViewControl(ImageViewControl* view) = 0;

    /// Says that an event failed, with what it threw.
    virtual void reportEventFailure(const QString& title, const QString& text) = 0;
};

/// The application's services as the target of the core's marshalled events: the views are made, listed and activated
/// by the view service - a view made this way is a process's - and the messages are shown by the dialog service. What
/// is not a service's yet - the state of the actions, the recent files - is done by what the window hands it.
class AFTERNIGHTSHELLCORE_API ServiceEventTarget final : public IApplicationEventTarget
{
public:
    /// What the window still does for an event: the recent files with their thumbnails and menus. An empty one does
    /// nothing.
    struct Dependencies
    {
        std::function<void(const std::string& fileName, const std::shared_ptr<const an::consumer::ImageAsset>& image)>
            addRecentFile;
        std::function<void(const std::string& fileName)> removeRecentFile;
    };

    ServiceEventTarget(ApplicationCore& core, Dependencies dependencies);

    ImageViewControl* createNewImageViewControlFromImage(ImageViewControl* customViewControl,
                                                         an::ui::ImageDocument&& document, QWidget* parent,
                                                         const std::string& historyDesc, ViewControlMode mode,
                                                         ImageViewControl* neighbour, bool keepFocusedNeighbour,
                                                         bool pendingChanges) override;
    ImageViewControl* createNewImageViewControlFromSequence(ImageViewControl* customViewControl,
                                                            an::ui::ImageDocument&& document, QWidget* parent,
                                                            bool autoDebayer, const std::string& historyDesc,
                                                            ViewControlMode mode, ImageViewControl* neighbour,
                                                            bool keepFocusedNeighbour, bool pendingChanges) override;
    void addImageViewControl(ImageViewControl* view) override;
    void removeImageViewControl(ImageViewControl* view, bool deleteObject) override;
    void updateImageViewControlList() override;
    void updateActiveImageActions() override;
    void addRecentFile(const std::string& fileName,
                       const std::shared_ptr<const an::consumer::ImageAsset>& image) override;
    void removeRecentFile(const std::string& fileName) override;
    void notifyErrorMessage(QWidget* parent, const QString& title, const QString& text) override;
    void notifyWarnMessage(QWidget* parent, const QString& title, const QString& text) override;
    void notifyInfoMessage(QWidget* parent, const QString& title, const QString& text) override;
    void messageBoxYesNo(QWidget* parent, const QString& title, const QString& text, bool warn, int* ret) override;
    void setActiveImageViewControl(ImageViewControl* view) override;
    void reportEventFailure(const QString& title, const QString& text) override;

private:
    ApplicationCore& m_core;
    Dependencies m_dependencies;
};

} // namespace an::ui::shell
