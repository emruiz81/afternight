// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// What the shared process helpers' cases share: the process windows the launcher's entry points have opened, the
/// registry the processes library registers at start-up kept as it was across a case that changes it, and the process
/// context - one engine session, the views it makes, every message - the launcher and the registry run under.
#pragma once

#include "Test_ControlsProcessFramework/ProcessFrameworkTestSupport.h"

#include "app/ProcessRegistry.h"
#include "controls/TopLevelProcessBaseControl.h"

#include <QApplication>
#include <QCoreApplication>
#include <QPointer>
#include <QWidget>

#include <cstdlib>
#include <memory>
#include <string>
#include <typeinfo>
#include <vector>

#if !defined(_MSC_VER)
#include <cxxabi.h>
#endif

class ImageViewControl;

namespace an::tests::processes::shared
{

/// A process window open now: what the launcher's entry point made when a case launched a process.
struct OpenedProcessWindow
{
    /// The window's class, unqualified - `HistogramTransformationProcess`.
    std::string className;

    /// The window.
    TopLevelProcessBaseControl* window = nullptr;

    /// The view a single-image process targets; null for a top-level window.
    ImageViewControl* view = nullptr;
};

/// A widget's most derived class, unqualified: what `typeid` names, demangled where the compiler mangles it. A process
/// window declares no meta-object of its own, so its meta-object names a base.
[[nodiscard]] inline std::string classNameOf(const QWidget& widget)
{
    std::string name = typeid(widget).name();
#if !defined(_MSC_VER)
    int status = 0;
    const std::unique_ptr<char, void (*)(void*)> demangled(abi::__cxa_demangle(name.c_str(), nullptr, nullptr, &status),
                                                           std::free);
    if (status == 0 && demangled)
    {
        name = demangled.get();
    }
#endif
    for (const char* prefix : {"class ", "struct "})
    {
        if (name.rfind(prefix, 0) == 0)
        {
            name.erase(0, std::char_traits<char>::length(prefix));
        }
    }
    const std::size_t scope = name.rfind("::");
    return scope == std::string::npos ? name : name.substr(scope + 2u);
}

/// Every process window shown now, in no particular order - what the launcher's entry points open, each shown as it is
/// made; with a class name, only the windows of that class. A window a case made and never showed is not counted, and
/// neither is one that has closed. GUI thread only.
[[nodiscard]] inline std::vector<OpenedProcessWindow> openedProcessWindows(const std::string& className = {})
{
    std::vector<OpenedProcessWindow> opened;
    for (QWidget* widget : QApplication::allWidgets())
    {
        auto* window = dynamic_cast<TopLevelProcessBaseControl*>(widget);
        if (window == nullptr || !window->isVisible())
        {
            continue;
        }
        const std::string name = classNameOf(*window);
        if (!className.empty() && name != className)
        {
            continue;
        }
        auto* attached = dynamic_cast<ImageViewControlListener*>(window);
        opened.push_back(OpenedProcessWindow{name, window, attached ? attached->getParentViewControl() : nullptr});
    }
    return opened;
}

/// Closes every process window shown now and waits until each has gone: a window deletes itself when it closes, and
/// one with a run on its way is hidden and closes when the run lands, staying attached to its view until then. What a
/// case that launched a process does before it ends - or before it launches another that needs its view to itself - so
/// no window outlives the case that opened it. GUI thread only.
inline void closeOpenedProcessWindows()
{
    std::vector<QPointer<QWidget>> closing;
    for (const OpenedProcessWindow& opened : openedProcessWindows())
    {
        closing.emplace_back(opened.window);
        opened.window->close();
    }
    const bool gone = an::tests::processes::pumpUntil([&closing] {
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        for (const QPointer<QWidget>& window : closing)
        {
            if (!window.isNull())
            {
                return false;
            }
        }
        return true;
    });
    EXPECT_TRUE(gone) << "a process window the case opened did not go when it was closed";
}

/// Closes every process window shown when it is made and again when it goes: declared after the context a case
/// installs, so the windows the case launched close before that context does, passing or failing.
class ClosesOpenedWindows
{
public:
    ClosesOpenedWindows()
    {
        closeOpenedProcessWindows();
    }
    ~ClosesOpenedWindows()
    {
        closeOpenedProcessWindows();
    }
    ClosesOpenedWindows(const ClosesOpenedWindows&) = delete;
    ClosesOpenedWindows& operator=(const ClosesOpenedWindows&) = delete;
};

/// The registry the processes library registered at start-up, put back as it was when the case ends, passing or
/// failing: the menu's wiring names descriptors, and a case may register or remove its own.
class RegistrySnapshot
{
public:
    RegistrySnapshot() : m_descriptors(an::ui::process_registry::ProcessRegistry::instance().all()) {}

    ~RegistrySnapshot()
    {
        an::ui::process_registry::ProcessRegistry& registry = an::ui::process_registry::ProcessRegistry::instance();
        registry.clear();
        for (const an::ui::process_registry::ProcessDescriptor& descriptor : m_descriptors)
        {
            (void)registry.registerProcess(descriptor);
        }
    }

    RegistrySnapshot(const RegistrySnapshot&) = delete;
    RegistrySnapshot& operator=(const RegistrySnapshot&) = delete;

private:
    std::vector<an::ui::process_registry::ProcessDescriptor> m_descriptors;
};

/// The registered descriptor of an id, or null.
[[nodiscard]] inline const an::ui::process_registry::ProcessDescriptor* registered(const std::string& id)
{
    return an::ui::process_registry::ProcessRegistry::instance().find(id);
}

} // namespace an::tests::processes::shared
