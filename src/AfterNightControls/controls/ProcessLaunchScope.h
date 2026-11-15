// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// The launch of a registered process: which descriptor the process windows made while it runs belong to.

#include "AfterNightControlsExport.h"
#include "controls/ProcessPresentation.h"

#include <QString>

#include <string>

namespace an::ui::process_registry
{
struct ProcessDescriptor;
} // namespace an::ui::process_registry

namespace an::ui
{

/// Names the descriptor of a launch while it runs, so that a process window made inside it knows which descriptor it
/// belongs to.
///
/// A process's entry point makes and shows its window and hands nothing back, so the launcher cannot tell the window
/// what it was launched as: it runs the launch inside a scope instead, and a window made inside one takes the scope's
/// descriptor id, its guide page, its surface and its column width as it is made (`TopLevelProcessBaseControl`). A
/// window made outside every scope belongs to no descriptor and is a window.
///
/// Scopes nest - the innermost names the launch - and each ends with its block, whatever the launch did: one whose
/// guards refused it, or whose process made no window, leaves nothing behind. GUI thread only.
class AFTERNIGHTCONTROLS_API ProcessLaunchScope
{
public:
    /// What a launch states of the windows made inside it.
    struct Stated
    {
        std::string descriptorId;
        QString helpPath;
        ProcessSurface surface = ProcessSurface::Window;
        ProcessColumnWidth columnWidth = ProcessColumnWidth::Standard;
    };

    /// Opens a scope that names a descriptor until it ends.
    explicit ProcessLaunchScope(const process_registry::ProcessDescriptor& descriptor);
    ~ProcessLaunchScope();

    ProcessLaunchScope(const ProcessLaunchScope&) = delete;
    ProcessLaunchScope& operator=(const ProcessLaunchScope&) = delete;
    ProcessLaunchScope(ProcessLaunchScope&&) = delete;
    ProcessLaunchScope& operator=(ProcessLaunchScope&&) = delete;

    /// What the innermost scope open now states; null outside every scope.
    [[nodiscard]] static const Stated* current() noexcept;

private:
    Stated m_stated;
    ProcessLaunchScope* m_outer = nullptr;
};

} // namespace an::ui
