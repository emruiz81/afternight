// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// The parity scenarios' driver of the classic shell: what a person does in the 1.1 window - its menus and tool bars,
/// its file dialog, its process windows and their progress windows, its status bar - done through the window itself.

#include "ShellDriver.h"

#include "AfterNightApp.h"
#include "ClassicShell.h"

#include "Test_ShellClassic/ApplicationDriving.h"
#include "Test_ShellClassic/InventoryRecords.h"

#include "actions/ActionIds.h"
#include "app/ProcessRegistry.h"
#include "controls/IAppContext.h"
#include "controls/ImageViewControl.h"
#include "controls/ProgressDialogControl.h"
#include "controls/TopLevelProcessBaseControl.h"
#include "core/IShell.h"
#include "views/DockHooks.h"

#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QContextMenuEvent>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMimeData>
#include <QPointer>
#include <QPushButton>
#include <QSet>
#include <QStatusBar>
#include <QToolBar>
#include <QUrl>

#include <algorithm>
#include <memory>
#include <optional>
#include <vector>

namespace an::tests::parity
{

class ClassicDriver final : public ShellDriver
{
public:
    explicit ClassicDriver(an::ui::shell::ClassicShell& shell) : m_shell(shell) {}

    QWidget& window() override
    {
        return *m_shell.window();
    }

    ImageViewControl* openImage(const QString& path) override
    {
        const std::vector<ImageViewControl*> before = views();
        app::Answered dialog;
        app::chooseInTheNextFileDialog({path}, &dialog);
        if (!runAction(an::ui::shell::ids::kOpen))
        {
            return nullptr;
        }
        ImageViewControl* opened = nullptr;
        (void)app::processEventsUntil(
            [&]() {
                for (ImageViewControl* view : views())
                {
                    if (std::find(before.begin(), before.end(), view) == before.end() && view->hasImage() &&
                        QDir::cleanPath(view->document().names().sourcePath) == QDir::cleanPath(path))
                    {
                        opened = view;
                    }
                }
                return opened != nullptr;
            },
            20000);
        return opened;
    }

    ImageViewControl* activeView() override
    {
        an::ui::IAppContext* const context = an::ui::appContext();
        return context != nullptr ? context->getViewRegistry()->getActiveImageViewControl() : nullptr;
    }

    QStringList viewNames() override
    {
        QStringList names;
        for (ImageViewControl* view : views())
        {
            names.append(QString::fromStdString(view->getUniqueName()));
        }
        names.sort();
        return names;
    }

    QString closeView(ImageViewControl& view) override
    {
        // The view is clicked first, so the File menu's Close acts on it.
        m_shell.viewHost().activate(view);
        const QPointer<ImageViewControl> watched(&view);
        auto question = std::make_shared<app::Answered>();
        const std::shared_ptr<app::WindowPoll> answering =
            app::answerWhenShown([question](QWidget* box) { return app::pressYesOrOk(box, question.get()); });
        if (runAction(an::ui::shell::ids::kClose))
        {
            (void)app::processEventsUntil([&]() {
                QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
                return watched.isNull();
            });
        }
        answering->ended = true;
        return question->seen ? question->title : QString();
    }

    bool canDragView(ImageViewControl& view) override
    {
        const an::ui::shell::DockTarget area = m_shell.viewHost().dockTarget(&view);
        return an::ui::shell::DockHooks::allowsDrag(
            an::ui::shell::DockDrag{.mainWindow = area.mainWindow, .dockWidget = view.getDockWidget()});
    }

    bool reaches(std::string_view actionId) override
    {
        return offered(actionId) != nullptr;
    }

    std::optional<bool> offeredEnabled(std::string_view actionId) override
    {
        const QAction* const action = offered(actionId);
        return action != nullptr ? std::optional<bool>(action->isEnabled()) : std::nullopt;
    }

    bool runAction(std::string_view actionId) override
    {
        QAction* const action = offered(actionId);
        if (action == nullptr)
        {
            return false;
        }
        action->trigger();
        return true;
    }

    ProcessLaunch launchProcess(std::string_view processId) override
    {
        ProcessLaunch launch;
        const an::ui::process_registry::ProcessDescriptor* const descriptor =
            an::ui::process_registry::ProcessRegistry::instance().find(processId);
        if (descriptor == nullptr)
        {
            launch.refusal = QStringLiteral("the registry holds no such process");
            return launch;
        }
        QAction* const action = offered(descriptor->action_object_name);
        if (action == nullptr)
        {
            launch.refusal = QStringLiteral("the window offers the process nowhere");
            return launch;
        }
        if (!action->isEnabled())
        {
            // A process that cannot run is an entry a person cannot take, and says why where the pointer rests on it.
            launch.refusal = action->statusTip();
            return launch;
        }
        // A launch refused when it is taken says why in a message box, which is answered.
        const QSet<QWidget*> before = processWindows();
        auto said = std::make_shared<app::Answered>();
        const std::shared_ptr<app::WindowPoll> refusal =
            app::answerWhenShown([said](QWidget* box) { return app::pressYesOrOk(box, said.get()); });
        action->trigger();
        (void)app::processEventsUntil(
            [&]() {
                for (QWidget* shown : processWindows())
                {
                    if (!before.contains(shown))
                    {
                        launch.window = shown;
                    }
                }
                return launch.window != nullptr || said->seen;
            },
            5000);
        refusal->ended = true;
        if (said->seen)
        {
            launch.refusal = said->text;
        }
        return launch;
    }

    bool applyOffered(QWidget& process) override
    {
        const QAbstractButton* const apply = applyButton(process);
        return apply != nullptr && apply->isEnabled();
    }

    bool pressApply(QWidget& process) override
    {
        if (!app::processEventsUntil([&]() { return applyOffered(process); }))
        {
            return false;
        }
        applyButton(process)->click();
        return true;
    }

    bool runInProgress(QWidget& process) override
    {
        return progressWindow(process) != nullptr;
    }

    bool cancelRun(QWidget& process) override
    {
        ProgressDialogControl* const progress = progressWindow(process);
        if (progress == nullptr)
        {
            return false;
        }
        auto* const cancel = progress->findChild<QPushButton*>(QStringLiteral("cancelButton"));
        if (cancel == nullptr || !cancel->isEnabled())
        {
            return false;
        }
        cancel->click();
        return true;
    }

    QString statusText() override
    {
        QStringList parts;
        for (const QLabel* label : m_shell.window()->statusBar()->findChildren<QLabel*>())
        {
            if (label->isVisible() && !label->text().trimmed().isEmpty())
            {
                parts.append(label->text().trimmed());
            }
        }
        return parts.join(QLatin1Char(' '));
    }

    QStringList viewMenuEntries(ImageViewControl& view) override
    {
        auto shown = std::make_shared<QStringList>();
        AfterNightApp* const window = m_shell.window();
        const std::shared_ptr<app::WindowPoll> reading = app::answerWhenShown([shown, window](QWidget* widget) {
            auto* menu = qobject_cast<QMenu*>(widget);
            if (menu == nullptr)
            {
                return false;
            }
            *shown = app::viewMenuTreeEntries(*menu, *window);
            menu->close();
            return true;
        });
        const QPoint at(4, 4);
        QContextMenuEvent event(QContextMenuEvent::Mouse, at, view.mapToGlobal(at));
        QCoreApplication::sendEvent(&view, &event);
        reading->ended = true;
        return *shown;
    }

    bool dropFiles(const QStringList& files) override
    {
        QMimeData data;
        QList<QUrl> urls;
        for (const QString& file : files)
        {
            urls.append(QUrl::fromLocalFile(file));
        }
        data.setUrls(urls);
        QDragEnterEvent enter(QPoint(20, 20), Qt::CopyAction, &data, Qt::LeftButton, Qt::NoModifier);
        enter.ignore();
        QCoreApplication::sendEvent(m_shell.window(), &enter);
        if (!enter.isAccepted())
        {
            return false;
        }
        QDropEvent drop(QPointF(20, 20), Qt::CopyAction, &data, Qt::LeftButton, Qt::NoModifier);
        drop.ignore();
        QCoreApplication::sendEvent(m_shell.window(), &drop);
        return drop.isAccepted();
    }

    bool closeWindow() override
    {
        app::answerEveryMessageBox(std::make_shared<app::Answered>());
        return m_shell.window()->close();
    }

    void reset() override
    {
        ++app::g_caseGeneration;
        for (QWidget* widget : QApplication::topLevelWidgets())
        {
            if (widget != nullptr && widget != m_shell.window() && widget->isVisible() &&
                (app::isA<QDialog>(widget) || app::isA<TopLevelProcessBaseControl>(widget)))
            {
                widget->close();
            }
        }
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QCoreApplication::processEvents();
        for (ImageViewControl* view : views())
        {
            for (QDialog* owned : view->findChildren<QDialog*>(QString(), Qt::FindDirectChildrenOnly))
            {
                delete owned;
            }
            m_shell.window()->removeImageViewControl(view, true);
        }
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QCoreApplication::processEvents();
        if (QAction* const unlock = offered(an::ui::shell::ids::kUnlockLayout);
            unlock != nullptr && unlock->isEnabled())
        {
            unlock->trigger();
        }
    }

private:
    [[nodiscard]] static std::vector<ImageViewControl*> views()
    {
        std::vector<ImageViewControl*> found;
        if (an::ui::IAppContext* const context = an::ui::appContext())
        {
            for (ImageViewControl* view : context->getViewControlManager()->getListOfViewControls())
            {
                if (view != nullptr)
                {
                    found.push_back(view);
                }
            }
        }
        return found;
    }

    [[nodiscard]] static QSet<QWidget*> processWindows()
    {
        QSet<QWidget*> windows;
        for (QWidget* widget : QApplication::topLevelWidgets())
        {
            if (widget != nullptr && widget->isVisible() && app::isA<TopLevelProcessBaseControl>(widget))
            {
                windows.insert(widget);
            }
        }
        return windows;
    }

    /// The Apply - or, in a window that has none, the Ok - of the process window's own buttons.
    [[nodiscard]] static QAbstractButton* applyButton(QWidget& process)
    {
        for (QDialogButtonBox* box : process.findChildren<QDialogButtonBox*>())
        {
            if (box->window() != process.window() || !box->isVisible())
            {
                continue;
            }
            QAbstractButton* button = box->button(QDialogButtonBox::Apply);
            if (button == nullptr)
            {
                button = box->button(QDialogButtonBox::Ok);
            }
            if (button != nullptr && button->isVisible())
            {
                return button;
            }
        }
        return nullptr;
    }

    /// The progress window a run of the process shows while it is in progress.
    [[nodiscard]] static ProgressDialogControl* progressWindow(QWidget& process)
    {
        // The window declares no meta-object of its own, so it is told by its dynamic type.
        for (QDialog* dialog : process.findChildren<QDialog*>())
        {
            auto* const progress = dynamic_cast<ProgressDialogControl*>(dialog);
            if (progress != nullptr && progress->isVisible())
            {
                return progress;
            }
        }
        return nullptr;
    }

    /// The action under `actionId` where a person reaches it in the window: the menu bar's menus and the tool bars.
    [[nodiscard]] QAction* offered(std::string_view actionId) const
    {
        const QString name = QString::fromLatin1(actionId.data(), static_cast<qsizetype>(actionId.size()));
        QSet<QAction*> reached;
        collect(m_shell.window()->menuBar()->actions(), reached);
        for (const QToolBar* bar : m_shell.window()->findChildren<QToolBar*>())
        {
            collect(bar->actions(), reached);
        }
        const auto found = std::find_if(reached.cbegin(), reached.cend(),
                                        [&name](const QAction* action) { return action->objectName() == name; });
        return found != reached.cend() ? *found : nullptr;
    }

    static void collect(const QList<QAction*>& entries, QSet<QAction*>& reached)
    {
        for (QAction* entry : entries)
        {
            if (entry == nullptr || entry->isSeparator() || reached.contains(entry))
            {
                continue;
            }
            reached.insert(entry);
            if (const QMenu* const menu = entry->menu())
            {
                collect(menu->actions(), reached);
            }
        }
    }

    an::ui::shell::ClassicShell& m_shell;
};

} // namespace an::tests::parity
