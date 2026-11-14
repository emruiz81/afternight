// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// The application, from the crash handler to the last line of the log: what `main` runs.

namespace an::ui
{

/// Runs the application: the crash handler first, then Qt, the single-instance check, the logger, the product's name,
/// version and icon; the session and the core; the shell chosen among the registered ones - `--shell=<id>` among the
/// arguments as `main` received them, then `AFTERNIGHT_SHELL`, the setting `interface/shell`, the default - started in
/// the order of `startApplication` and shown; the event loop; and the end in the order of `stopApplication`.
///
/// @return the application's exit code.
int runApplication(int argc, char* argv[]);

} // namespace an::ui
