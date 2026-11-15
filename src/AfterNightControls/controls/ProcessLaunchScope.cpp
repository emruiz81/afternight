// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"

#include "controls/ProcessLaunchScope.h"

#include "app/ProcessRegistry.h"

namespace an::ui
{

namespace
{

/// The innermost scope open now; each scope keeps the one it was opened inside. GUI thread only.
ProcessLaunchScope* g_innermost = nullptr;

} // namespace

ProcessLaunchScope::ProcessLaunchScope(const process_registry::ProcessDescriptor& descriptor)
    : m_stated{.descriptorId = descriptor.id,
               .helpPath = QString::fromStdString(descriptor.helpPath),
               .surface = descriptor.surface,
               .columnWidth = descriptor.columnWidth},
      m_outer(g_innermost)
{
    g_innermost = this;
}

ProcessLaunchScope::~ProcessLaunchScope()
{
    g_innermost = m_outer;
}

const ProcessLaunchScope::Stated* ProcessLaunchScope::current() noexcept
{
    return g_innermost != nullptr ? &g_innermost->m_stated : nullptr;
}

} // namespace an::ui
