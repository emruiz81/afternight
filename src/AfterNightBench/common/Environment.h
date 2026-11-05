// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNight benchmark system - reading the process environment.
//
// One place for it, because the benchmark system reads the environment in two unrelated situations
// - identifying the host and locating the host-local corpus - and because MSVC rejects `getenv`
// under warnings-as-errors, so each reader would otherwise carry its own platform branch.

#pragma once

#include <string>

namespace an::bench
{

/// The value of the environment variable `name`, or an empty string when it is unset or empty.
///
/// An unset variable and an empty one are deliberately indistinguishable here: every caller in
/// this library treats both as "not configured", and offering a difference would invite a caller
/// to depend on one that the platforms do not agree about.
[[nodiscard]] std::string environmentValue(const char* name);

} // namespace an::bench
