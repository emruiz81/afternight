// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The application core's marshalled events: each carries what it was asked with to the GUI thread and runs it there
/// against the core's target. The core is their receiver, so an event never outlives it unrun: Qt lets go of a
/// receiver's pending events when it goes.

#include "core/ApplicationEvents.h"

#include "core/ApplicationCore.h"
#include "services/DialogService.h"
#include "views/ViewService.h"

#include "app/AfterNightEvent.h"
#include "session/ImageDocument.h"

#include <AfterNightConsumerRuntime/ImageAsset.h>

#include <AfterNightPlatform/Logging.h>

#include <QCoreApplication>
#include <QEvent>
#include <QPointer>
#include <QWidget>

#include <exception>
#include <utility>

namespace an::ui::shell
{

namespace
{

// Makes a view of an image document on the GUI thread. The document travels in the event, so the thread that made it
// never waits for the GUI thread; when no view is made, the event closes it.
class CreateNewImageViewControlFromImageEvent : public AfterNightEvent
{
public:
    CreateNewImageViewControlFromImageEvent(ApplicationCore& core, ImageViewControl* customViewControl,
                                            an::ui::ImageDocument&& document, QWidget* parent,
                                            const std::string& historyDesc, const ViewControlMode mode,
                                            ImageViewControl* neighbour, bool keepFocusedNeighbour,
                                            const bool pendingChanges, std::atomic_bool* finished)
        : AfterNightEvent(finished), m_core(core), m_customViewControl(customViewControl),
          m_document(std::move(document)), m_parent(parent), m_historyDesc(historyDesc), m_mode(mode),
          m_neighbour(neighbour), m_keepFocusedNeighbour(keepFocusedNeighbour), m_pendingChanges(pendingChanges)
    {
    }

private:
    void runImpl() override
    {
        IApplicationEventTarget* const target = m_core.eventTarget();
        ImageViewControl* const created =
            target != nullptr
                ? target->createNewImageViewControlFromImage(m_customViewControl, std::move(m_document),
                                                             m_parent.data(), m_historyDesc, m_mode, m_neighbour,
                                                             m_keepFocusedNeighbour, m_pendingChanges)
                : nullptr;
        if (!created && m_document.valid())
        {
            m_document.close();
        }
    }

    ApplicationCore& m_core;
    ImageViewControl* m_customViewControl;
    an::ui::ImageDocument m_document;
    QPointer<QWidget> m_parent;
    std::string m_historyDesc;
    ViewControlMode m_mode;
    ImageViewControl* m_neighbour;
    bool m_keepFocusedNeighbour;
    bool m_pendingChanges;
};

// Makes a view of a sequence document on the GUI thread, as the image event does.
class CreateNewImageViewControlFromSequenceEvent : public AfterNightEvent
{
public:
    CreateNewImageViewControlFromSequenceEvent(ApplicationCore& core, ImageViewControl* customViewControl,
                                               an::ui::ImageDocument&& document, QWidget* parent,
                                               const bool autoDebayer, const std::string& historyDesc,
                                               const ViewControlMode mode, ImageViewControl* neighbour,
                                               bool keepFocusedNeighbour, const bool pendingChanges,
                                               std::atomic_bool* finished)
        : AfterNightEvent(finished), m_core(core), m_customViewControl(customViewControl),
          m_document(std::move(document)), m_parent(parent), m_autoDebayer(autoDebayer), m_historyDesc(historyDesc),
          m_mode(mode), m_neighbour(neighbour), m_keepFocusedNeighbour(keepFocusedNeighbour),
          m_pendingChanges(pendingChanges)
    {
    }

private:
    void runImpl() override
    {
        IApplicationEventTarget* const target = m_core.eventTarget();
        ImageViewControl* const created =
            target != nullptr
                ? target->createNewImageViewControlFromSequence(m_customViewControl, std::move(m_document),
                                                                m_parent.data(), m_autoDebayer, m_historyDesc, m_mode,
                                                                m_neighbour, m_keepFocusedNeighbour, m_pendingChanges)
                : nullptr;
        if (!created && m_document.valid())
        {
            m_document.close();
        }
    }

    ApplicationCore& m_core;
    ImageViewControl* m_customViewControl;
    an::ui::ImageDocument m_document;
    QPointer<QWidget> m_parent;
    bool m_autoDebayer;
    std::string m_historyDesc;
    ViewControlMode m_mode;
    ImageViewControl* m_neighbour;
    bool m_keepFocusedNeighbour;
    bool m_pendingChanges;
};

class AddImageViewControlAsyncEvent : public AfterNightEvent
{
public:
    AddImageViewControlAsyncEvent(ApplicationCore& core, ImageViewControl* control, std::atomic_bool* finished)
        : AfterNightEvent(finished), m_core(core), m_viewControl(control)
    {
    }

private:
    void runImpl() override
    {
        if (IApplicationEventTarget* const target = m_core.eventTarget())
        {
            target->addImageViewControl(m_viewControl);
        }
    }

    ApplicationCore& m_core;
    ImageViewControl* m_viewControl;
};

class RemoveImageViewControlAsyncEvent : public AfterNightEvent
{
public:
    RemoveImageViewControlAsyncEvent(ApplicationCore& core, ImageViewControl* control, bool deleteObject,
                                     std::atomic_bool* finished)
        : AfterNightEvent(finished), m_core(core), m_viewControl(control), m_deleteObject(deleteObject)
    {
    }

private:
    void runImpl() override
    {
        if (IApplicationEventTarget* const target = m_core.eventTarget())
        {
            target->removeImageViewControl(m_viewControl, m_deleteObject);
        }
    }

    ApplicationCore& m_core;
    ImageViewControl* m_viewControl;
    bool m_deleteObject;
};

class UpdateImageViewControlListEvent : public AfterNightEvent
{
public:
    UpdateImageViewControlListEvent(ApplicationCore& core, std::atomic_bool* finished)
        : AfterNightEvent(finished), m_core(core)
    {
    }

private:
    void runImpl() override
    {
        if (IApplicationEventTarget* const target = m_core.eventTarget())
        {
            target->updateImageViewControlList();
        }
    }

    ApplicationCore& m_core;
};

class UpdateActiveImageActionsEvent : public AfterNightEvent
{
public:
    UpdateActiveImageActionsEvent(ApplicationCore& core, std::atomic_bool* finished)
        : AfterNightEvent(finished), m_core(core)
    {
    }

private:
    void runImpl() override
    {
        if (IApplicationEventTarget* const target = m_core.eventTarget())
        {
            target->updateActiveImageActions();
        }
    }

    ApplicationCore& m_core;
};

class AddRecentFileEvent : public AfterNightEvent
{
public:
    AddRecentFileEvent(ApplicationCore& core, const std::string& fileName,
                       std::shared_ptr<const an::consumer::ImageAsset> image, std::atomic_bool* finished)
        : AfterNightEvent(finished), m_core(core), m_fileName(fileName), m_image(std::move(image))
    {
    }

private:
    void runImpl() override
    {
        if (IApplicationEventTarget* const target = m_core.eventTarget())
        {
            target->addRecentFile(m_fileName, m_image);
        }
    }

    ApplicationCore& m_core;
    std::string m_fileName;
    std::shared_ptr<const an::consumer::ImageAsset> m_image;
};

class RemoveRecentFileEvent : public AfterNightEvent
{
public:
    RemoveRecentFileEvent(ApplicationCore& core, const std::string& fileName, std::atomic_bool* finished)
        : AfterNightEvent(finished), m_core(core), m_fileName(fileName)
    {
    }

private:
    void runImpl() override
    {
        if (IApplicationEventTarget* const target = m_core.eventTarget())
        {
            target->removeRecentFile(m_fileName);
        }
    }

    ApplicationCore& m_core;
    std::string m_fileName;
};

class NotifyErrorMessageEvent : public AfterNightEvent
{
public:
    NotifyErrorMessageEvent(ApplicationCore& core, QWidget* parent, const QString& title, const QString& text,
                            std::atomic_bool* finished)
        : AfterNightEvent(finished), m_core(core), m_parent(parent), m_title(title), m_text(text)
    {
    }

private:
    void runImpl() override
    {
        if (IApplicationEventTarget* const target = m_core.eventTarget())
        {
            target->notifyErrorMessage(m_parent.data(), m_title, m_text);
        }
    }

    ApplicationCore& m_core;
    QPointer<QWidget> m_parent;
    QString m_title;
    QString m_text;
};

class NotifyWarnMessageEvent : public AfterNightEvent
{
public:
    NotifyWarnMessageEvent(ApplicationCore& core, QWidget* parent, const QString& title, const QString& text,
                           std::atomic_bool* finished)
        : AfterNightEvent(finished), m_core(core), m_parent(parent), m_title(title), m_text(text)
    {
    }

private:
    void runImpl() override
    {
        if (IApplicationEventTarget* const target = m_core.eventTarget())
        {
            target->notifyWarnMessage(m_parent.data(), m_title, m_text);
        }
    }

    ApplicationCore& m_core;
    QPointer<QWidget> m_parent;
    QString m_title;
    QString m_text;
};

class NotifyInfoMessageEvent : public AfterNightEvent
{
public:
    NotifyInfoMessageEvent(ApplicationCore& core, QWidget* parent, const QString& title, const QString& text,
                           std::atomic_bool* finished)
        : AfterNightEvent(finished), m_core(core), m_parent(parent), m_title(title), m_text(text)
    {
    }

private:
    void runImpl() override
    {
        if (IApplicationEventTarget* const target = m_core.eventTarget())
        {
            target->notifyInfoMessage(m_parent.data(), m_title, m_text);
        }
    }

    ApplicationCore& m_core;
    QPointer<QWidget> m_parent;
    QString m_title;
    QString m_text;
};

class MessageBoxYesNoEvent : public AfterNightEvent
{
public:
    MessageBoxYesNoEvent(ApplicationCore& core, QWidget* parent, const QString& title, const QString& text, bool warn,
                         int* ret, std::atomic_bool* finished)
        : AfterNightEvent(finished), m_core(core), m_parent(parent), m_title(title), m_text(text), m_warn(warn),
          m_ret(ret)
    {
    }

private:
    void runImpl() override
    {
        if (IApplicationEventTarget* const target = m_core.eventTarget())
        {
            target->messageBoxYesNo(m_parent.data(), m_title, m_text, m_warn, m_ret);
        }
    }

    ApplicationCore& m_core;
    QPointer<QWidget> m_parent;
    QString m_title;
    QString m_text;
    bool m_warn;
    int* m_ret;
};

class SetActiveViewControlEvent : public AfterNightEvent
{
public:
    SetActiveViewControlEvent(ApplicationCore& core, ImageViewControl* control, std::atomic_bool* finished)
        : AfterNightEvent(finished), m_core(core), m_control(control)
    {
    }

private:
    void runImpl() override
    {
        if (IApplicationEventTarget* const target = m_core.eventTarget())
        {
            target->setActiveImageViewControl(m_control);
        }
    }

    ApplicationCore& m_core;
    ImageViewControl* m_control;
};

class RunCustomCodeEvent : public AfterNightEvent
{
public:
    RunCustomCodeEvent(const std::function<void()>& func, std::atomic_bool* finished)
        : AfterNightEvent(finished), m_func(func)
    {
    }

private:
    void runImpl() override
    {
        if (m_func)
        {
            m_func();
        }
    }

    std::function<void()> m_func;
};

} // namespace

void ApplicationCore::postEvent(AfterNightEvent* event, const int priority)
{
    if (event != nullptr)
    {
        QCoreApplication::postEvent(this, event, priority);
    }
}

void ApplicationCore::discardPendingEvents()
{
    QCoreApplication::removePostedEvents(this, AfterNightEvent::getEventType());
}

void ApplicationCore::customEvent(QEvent* ev)
{
    if (ev->type() == AfterNightEvent::getEventType())
    {
        ev->accept();
        auto customEvent = static_cast<AfterNightEvent*>(ev);

        try
        {
            customEvent->run();
        }
        catch (std::exception& e)
        {
            std::string msg = fmt::format("Exception running async operation: {}", e.what());
            LOG_ERROR("{}", msg);
            if (IApplicationEventTarget* const target = eventTarget())
            {
                target->reportEventFailure("Async Operation Error", QString::fromStdString(msg));
            }
        }
    }
}

void ApplicationCore::createNewImageViewControlFromImageAsync(ImageViewControl* customViewControl,
                                                              an::ui::ImageDocument&& document, QWidget* parent,
                                                              const std::string& historyDesc,
                                                              const ViewControlMode mode, ImageViewControl* neighbour,
                                                              bool keepFocusedNeighbour, const bool pendingChanges,
                                                              std::atomic_bool* finished)
{
    postEvent(new CreateNewImageViewControlFromImageEvent(*this, customViewControl, std::move(document), parent,
                                                          historyDesc, mode, neighbour, keepFocusedNeighbour,
                                                          pendingChanges, finished));
}

void ApplicationCore::createNewImageViewControlFromSequenceAsync(ImageViewControl* customViewControl,
                                                                 an::ui::ImageDocument&& document, QWidget* parent,
                                                                 const bool autoDebayer, const std::string& historyDesc,
                                                                 const ViewControlMode mode,
                                                                 ImageViewControl* neighbour, bool keepFocusedNeighbour,
                                                                 const bool pendingChanges, std::atomic_bool* finished)
{
    postEvent(new CreateNewImageViewControlFromSequenceEvent(*this, customViewControl, std::move(document), parent,
                                                             autoDebayer, historyDesc, mode, neighbour,
                                                             keepFocusedNeighbour, pendingChanges, finished));
}

void ApplicationCore::addImageViewControlAsync(ImageViewControl* view)
{
    postEvent(new AddImageViewControlAsyncEvent(*this, view, nullptr));
}

void ApplicationCore::removeImageViewControlAsync(ImageViewControl* view, bool deleteObject)
{
    postEvent(new RemoveImageViewControlAsyncEvent(*this, view, deleteObject, nullptr));
}

void ApplicationCore::updateImageViewControlListAsync(std::atomic_bool* finished)
{
    postEvent(new UpdateImageViewControlListEvent(*this, finished));
}

void ApplicationCore::updateActiveImageActionsAsync()
{
    postEvent(new UpdateActiveImageActionsEvent(*this, nullptr));
}

void ApplicationCore::addRecentFileAsync(const std::string& fileName,
                                         std::shared_ptr<const an::consumer::ImageAsset> image,
                                         std::atomic_bool* finished)
{
    postEvent(new AddRecentFileEvent(*this, fileName, std::move(image), finished));
}

void ApplicationCore::removeRecentFileAsync(const std::string& fileName, std::atomic_bool* finished)
{
    postEvent(new RemoveRecentFileEvent(*this, fileName, finished));
}

void ApplicationCore::notifyErrorMessageAsync(QWidget* parent, const QString& title, const QString& text)
{
    postEvent(new NotifyErrorMessageEvent(*this, parent, title, text, nullptr));
}

void ApplicationCore::notifyWarnMessageAsync(QWidget* parent, const QString& title, const QString& text)
{
    postEvent(new NotifyWarnMessageEvent(*this, parent, title, text, nullptr));
}

void ApplicationCore::notifyInfoMessageAsync(QWidget* parent, const QString& title, const QString& text)
{
    postEvent(new NotifyInfoMessageEvent(*this, parent, title, text, nullptr));
}

void ApplicationCore::messageBoxYesNoAsync(QWidget* parent, const QString& title, const QString& text, bool warn,
                                           int* ret, std::atomic_bool* finished)
{
    if (finished != nullptr)
    {
        *finished = false;
    }
    postEvent(new MessageBoxYesNoEvent(*this, parent, title, text, warn, ret, finished));
}

void ApplicationCore::setActiveImageViewControlAsync(ImageViewControl* view, std::atomic_bool* finished)
{
    postEvent(new SetActiveViewControlEvent(*this, view, finished));
}

void ApplicationCore::runCustomCodeAsync(const std::function<void()>& func)
{
    postEvent(new RunCustomCodeEvent(func, nullptr));
}

ServiceEventTarget::ServiceEventTarget(ApplicationCore& core, Dependencies dependencies)
    : m_core(core), m_dependencies(std::move(dependencies))
{
}

ImageViewControl* ServiceEventTarget::createNewImageViewControlFromImage(
    ImageViewControl* customViewControl, an::ui::ImageDocument&& document, QWidget* parent,
    const std::string& historyDesc, const ViewControlMode mode, ImageViewControl* neighbour,
    const bool keepFocusedNeighbour, const bool pendingChanges)
{
    return m_core.viewService().createNewImageViewControlFromImage(customViewControl, std::move(document), parent,
                                                                   historyDesc, mode, neighbour, keepFocusedNeighbour,
                                                                   pendingChanges);
}

ImageViewControl* ServiceEventTarget::createNewImageViewControlFromSequence(
    ImageViewControl* customViewControl, an::ui::ImageDocument&& document, QWidget* parent, const bool autoDebayer,
    const std::string& historyDesc, const ViewControlMode mode, ImageViewControl* neighbour,
    const bool keepFocusedNeighbour, const bool pendingChanges)
{
    return m_core.viewService().createNewImageViewControlFromSequence(customViewControl, std::move(document), parent,
                                                                      autoDebayer, historyDesc, mode, neighbour,
                                                                      keepFocusedNeighbour, pendingChanges);
}

void ServiceEventTarget::addImageViewControl(ImageViewControl* view)
{
    m_core.viewService().addViewControl(view);
}

void ServiceEventTarget::removeImageViewControl(ImageViewControl* view, const bool deleteObject)
{
    m_core.viewService().removeViewControl(view, deleteObject);
}

void ServiceEventTarget::updateImageViewControlList()
{
    m_core.viewService().refreshViewList();
}

void ServiceEventTarget::updateActiveImageActions()
{
    if (m_dependencies.updateActiveImageActions)
    {
        m_dependencies.updateActiveImageActions();
    }
}

void ServiceEventTarget::addRecentFile(const std::string& fileName,
                                       const std::shared_ptr<const an::consumer::ImageAsset>& image)
{
    if (m_dependencies.addRecentFile)
    {
        m_dependencies.addRecentFile(fileName, image);
    }
}

void ServiceEventTarget::removeRecentFile(const std::string& fileName)
{
    if (m_dependencies.removeRecentFile)
    {
        m_dependencies.removeRecentFile(fileName);
    }
}

void ServiceEventTarget::notifyErrorMessage(QWidget* parent, const QString& title, const QString& text)
{
    m_core.dialogService().notifyErrorMessage(parent, title, text);
}

void ServiceEventTarget::notifyWarnMessage(QWidget* parent, const QString& title, const QString& text)
{
    m_core.dialogService().notifyWarnMessage(parent, title, text);
}

void ServiceEventTarget::notifyInfoMessage(QWidget* parent, const QString& title, const QString& text)
{
    m_core.dialogService().notifyInfoMessage(parent, title, text);
}

void ServiceEventTarget::messageBoxYesNo(QWidget* parent, const QString& title, const QString& text, const bool warn,
                                         int* ret)
{
    m_core.dialogService().messageBoxYesNo(parent, title, text, warn, ret);
}

void ServiceEventTarget::setActiveImageViewControl(ImageViewControl* view)
{
    m_core.viewService().setActiveImageViewControl(view);
}

void ServiceEventTarget::reportEventFailure(const QString& title, const QString& text)
{
    m_core.dialogService().notifyErrorMessage(nullptr, title, text);
}

} // namespace an::ui::shell
