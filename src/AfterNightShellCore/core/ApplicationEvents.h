// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// What the work a thread hands the GUI thread does once it gets there.

#include "AfterNightShellCoreExport.h"

#include "controls/ViewControlMode.h"

#include <QString>

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

/// What each of the application core's marshalled events asks for, on the GUI thread: the parts of the application
/// that make, add and remove views, show messages and keep the recent files.
///
/// The core is given one target (`ApplicationCore::setEventTarget`) and runs every such event against it; while it has
/// none, an event asks nothing - a view-making event closes the document it carries. The target is the window's until
/// the services that answer these move out of it.
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

} // namespace an::ui::shell
