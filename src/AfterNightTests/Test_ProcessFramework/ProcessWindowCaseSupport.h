// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// What this suite's process window cases share: a view of a Float32 image registered with the suite's context and
/// made its active view, a process window opened over a view as the launcher's entry point opens one and deleted with
/// the case once its runs have landed, a widget of a window by its object name, the event loop turned until the windows
/// and the views are idle, and the application's process menus over the record's actions, each process action asking
/// for the registry descriptor the processes library registered for it.
#pragma once

#include "Test_ProcessFramework/ProcessFrameworkSuiteSupport.h"

#include "app/AfterNightAppUiBuilder.h"
#include "app/ProcessRegistry.h"
#include "controls/ImageProcessControl.h"
#include "controls/ImageRTPreviewControl.h"
#include "controls/ImageViewControl.h"
#include "theme/AnDesignTokens.h"

#include "fixtures/RecordedActions.h"

#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDoubleSpinBox>
#include <QGroupBox>
#include <QLayout>
#include <QMainWindow>
#include <QMenu>
#include <QMenuBar>
#include <QPointer>
#include <QSpinBox>
#include <QString>
#include <QThreadPool>
#include <QWidget>

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace an::tests::processes::framework
{

/// A view registered with a context and made its active view, as the application registers the view it opens; taken
/// back from the context and deleted with the case, once every worker has finished.
class ContextView
{
public:
    ContextView(ProcessTestContext& context, ViewHandle view) : m_context(&context), m_view(std::move(view))
    {
        if (m_view == nullptr)
        {
            return;
        }
        const std::vector<ImageViewControl*> listed = context.getListOfViewControls();
        if (std::find(listed.begin(), listed.end(), m_view.get()) == listed.end())
        {
            context.addViewControl(m_view.get());
        }
        context.setActiveImageViewControl(m_view.get());
    }
    ~ContextView()
    {
        if (m_view == nullptr)
        {
            return;
        }
        QThreadPool::globalInstance()->waitForDone();
        QCoreApplication::processEvents();
        m_context->removeViewControl(m_view.get(), false);
        m_view.reset();
        QCoreApplication::processEvents();
    }
    ContextView(const ContextView&) = delete;
    ContextView& operator=(const ContextView&) = delete;

    [[nodiscard]] ImageViewControl* get() const noexcept
    {
        return m_view.get();
    }
    [[nodiscard]] ImageViewControl& operator*() const noexcept
    {
        return *m_view;
    }
    [[nodiscard]] ImageViewControl* operator->() const noexcept
    {
        return m_view.get();
    }
    [[nodiscard]] explicit operator bool() const noexcept
    {
        return m_view != nullptr;
    }

private:
    ProcessTestContext* m_context = nullptr;
    ViewHandle m_view;
};

/// A view of a new Float32 image of the suite's session, each sample `value(x, y, channel)`, registered with the
/// suite's context and made active; empty, with the case failed, when it cannot be made.
[[nodiscard]] inline std::unique_ptr<ContextView>
floatView(const std::string& name, std::uint32_t width, std::uint32_t height, Channels channels,
          const std::function<float(std::uint32_t, std::uint32_t, std::uint32_t)>& value)
{
    ProcessTestContext& context = suiteContext();
    std::optional<an::ui::ImageDocument> document =
        makeDocument<float>(context.engineSession(), width, height, channels, value,
                            an::ui::DocumentNames{.displayName = QString::fromStdString(name)});
    if (!document.has_value())
    {
        return nullptr;
    }
    return std::make_unique<ContextView>(context, makeTargetView(name, std::move(*document)));
}

/// Runs a function when it goes, passing or failing: what a case that holds a run at a gate does to let it go.
class AtCaseEnd
{
public:
    explicit AtCaseEnd(std::function<void()> action) : m_action(std::move(action)) {}
    ~AtCaseEnd()
    {
        if (m_action)
        {
            m_action();
        }
    }
    AtCaseEnd(const AtCaseEnd&) = delete;
    AtCaseEnd& operator=(const AtCaseEnd&) = delete;

private:
    std::function<void()> m_action;
};

/// Whether nothing a window started is on its way: no run of a process window and no preview of a real-time one.
[[nodiscard]] inline bool windowIdle(const QWidget& window)
{
    if (const auto* preview = dynamic_cast<const ImageRTPreviewControl*>(&window))
    {
        return preview->isProcessIdle();
    }
    if (const auto* process = dynamic_cast<const ImageProcessControl*>(&window))
    {
        return process->isRunIdle();
    }
    return true;
}

/// A process window, deleted with the case once its runs and its preview's work have landed.
struct WindowDeleter
{
    void operator()(QWidget* window) const
    {
        if (window == nullptr)
        {
            return;
        }
        (void)pumpUntil([window]() { return windowIdle(*window); });
        delete window;
        QCoreApplication::processEvents();
    }
};
template <typename WindowT> using WindowHandle = std::unique_ptr<WindowT, WindowDeleter>;

/// A process window over a target, as the launcher's entry point opens one, without the delete-on-close the window
/// sets so the case decides when it goes.
template <typename WindowT> [[nodiscard]] WindowHandle<WindowT> openOver(ImageViewControl& target)
{
    WindowHandle<WindowT> window(new WindowT(&target));
    window->setAttribute(Qt::WA_DeleteOnClose, false);
    window->resize(900, 640);
    return window;
}

/// A widget of a window by its object name; fails the case when the window has none.
template <typename WidgetT> [[nodiscard]] WidgetT* widgetNamed(const QWidget& window, const char* name)
{
    auto* widget = window.findChild<WidgetT*>(QString::fromLatin1(name));
    EXPECT_NE(widget, nullptr) << "the window has no " << name;
    return widget;
}

/// Checks or clears a checkable group box as a click on its title does: its state and the click it emits.
inline void clickGroup(const QWidget& window, const char* name, bool checked)
{
    if (auto* group = widgetNamed<QGroupBox>(window, name))
    {
        group->setChecked(checked);
        emit group->clicked(checked);
    }
}

/// Chooses a combo box's entry by its text; fails the case when it offers none.
inline void chooseText(const QWidget& window, const char* name, const QString& text)
{
    if (auto* combo = widgetNamed<QComboBox>(window, name))
    {
        const int index = combo->findText(text);
        EXPECT_GE(index, 0) << name << " offers no " << text.toStdString();
        if (index >= 0)
        {
            combo->setCurrentIndex(index);
        }
    }
}

/// Turns the event loop until a window's runs, the suite's views and the workers have all landed.
[[nodiscard]] inline bool settle(const QWidget& window, const std::vector<ImageViewControl*>& views = {})
{
    const auto idle = [&window, &views]() {
        if (!windowIdle(window) || QThreadPool::globalInstance()->activeThreadCount() != 0)
        {
            return false;
        }
        return std::all_of(views.begin(), views.end(), [](const ImageViewControl* view) { return view->isWorkIdle(); });
    };
    if (!pumpUntil(idle))
    {
        return false;
    }
    pumpQuietly();
    return idle();
}

/// Turns the event loop until a window deleted later is gone.
[[nodiscard]] inline bool awaitDeleted(const QPointer<QWidget>& window)
{
    return pumpUntil([&window]() {
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        return window.isNull();
    });
}

/// Whether no widget under a root, the root included, carries a style sheet: every one is painted by the
/// application's style from the design tokens.
inline void expectNoStyleSheet(QWidget& root)
{
    EXPECT_TRUE(root.styleSheet().isEmpty()) << root.objectName().toStdString();
    for (const QWidget* child : root.findChildren<QWidget*>())
    {
        EXPECT_TRUE(child->styleSheet().isEmpty()) << child->objectName().toStdString();
    }
}

/// The menu titled `title` among the entries of a menu bar or a menu, or null.
[[nodiscard]] inline QMenu* submenuTitled(const QList<QAction*>& entries, const QString& title)
{
    for (QAction* entry : entries)
    {
        QMenu* menu = entry->menu();
        if (menu != nullptr && menu->title() == title)
        {
            return menu;
        }
    }
    return nullptr;
}

/// The application's main window with its menus, built over the actions the record of the interface holds, each
/// process action asking for the registry descriptor the processes library registered under its name, and every launch
/// it asks for recorded rather than run. The registry is put back as it was when the menus go, passing or failing.
class ProcessMenus
{
public:
    ProcessMenus() : m_actions(m_window), m_ui(an::ui::app::buildAfterNightAppUi(&m_window, m_actions.lookup()))
    {
        for (const an::ui::process_registry::ProcessDescriptor& descriptor :
             an::ui::process_registry::ProcessRegistry::instance().all())
        {
            if (descriptor.is_extension_process)
            {
                continue;
            }
            QAction* const action = m_actions.action(descriptor.action_object_name);
            QObject::connect(action, &QAction::triggered, &m_window,
                             [this, id = descriptor.id]() { launched.push_back(id); });
        }
    }
    ProcessMenus(const ProcessMenus&) = delete;
    ProcessMenus& operator=(const ProcessMenus&) = delete;

    /// The entry of the Process menu's submenu `submenu` that reads `text`, or null.
    [[nodiscard]] QAction* entry(const QString& submenu, const QString& text) const
    {
        QMenu* const process = submenuTitled(m_window.menuBar()->actions(), QStringLiteral("Process"));
        QMenu* const menu = process != nullptr ? submenuTitled(process->actions(), submenu) : nullptr;
        if (menu == nullptr)
        {
            return nullptr;
        }
        for (QAction* candidate : menu->actions())
        {
            if (candidate->text() == text)
            {
                return candidate;
            }
        }
        return nullptr;
    }

    /// The menus' elements.
    [[nodiscard]] const an::ui::app::AfterNightAppUiElements& ui() const noexcept
    {
        return m_ui;
    }

    /// The registry ids every launch the actions asked for named, in order.
    std::vector<std::string> launched;

private:
    shared::RegistrySnapshot m_registry;
    QMainWindow m_window;
    an::tests::fixtures::RecordedActions m_actions;
    an::ui::app::AfterNightAppUiElements m_ui;
};

} // namespace an::tests::processes::framework
