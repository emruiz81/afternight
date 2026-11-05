// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The host's startup and teardown scenarios: each loads the runtime backend module and, where it needs one, starts the
/// interpreter as the full host does, borrows the backend's session through the host's own code, and takes it all down
/// through `shutDownHostRuntime`, in a process of its own. `HostRuntimeLifecycleTest` runs each and judges its exit
/// code.
#pragma once

#include <string>

namespace an::tests::host_native
{

/// Runs the scenario this process was started for.
///
/// @param name the scenario.
/// @return the process's exit code: 0 when every check held, 2 for an unknown scenario, 1 otherwise.
[[nodiscard]] int runHostScenario(const std::string& name);

} // namespace an::tests::host_native
