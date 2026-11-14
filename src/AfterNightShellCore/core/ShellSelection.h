// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// The choice of a shell when the application starts.

#include "AfterNightShellCoreExport.h"
#include "theme/StyleProfile.h"

#include <QProcessEnvironment>
#include <QString>
#include <QStringList>

namespace an::ui
{
class IGlobalSettingsProvider;
} // namespace an::ui

namespace an::ui::shell
{

class ShellRegistry;
struct ShellDescriptor;

/// Where the chosen shell was named.
enum class ShellChoiceSource
{
    CommandLine, ///< `--shell=<id>` among the arguments the process was started with
    Environment, ///< the environment variable `AFTERNIGHT_SHELL`
    Setting, ///< the setting `interface/shell`, which the user chooses and which lasts
    Default, ///< none of them: the default shell
};

/// The outcome of a choice.
struct ShellChoice
{
    /// The chosen shell; null when no source, the default included, names a registered shell.
    const ShellDescriptor* descriptor = nullptr;
    /// Where it was named. `Default` when nothing was chosen.
    ShellChoiceSource source = ShellChoiceSource::Default;
    /// The style profile the shell starts under: the one its descriptor names, unless the developer switch
    /// `AFTERNIGHT_STYLE_PROFILE` names another. `Classic` when no shell was chosen.
    an::ui::StyleProfile styleProfile = an::ui::StyleProfile::Classic;
    /// One line for each source that was passed over and each option that was not read, in the order they were met.
    /// Each was logged as a warning when the choice was made.
    QStringList warnings;
};

/// Chooses the one shell a run of the application makes.
///
/// The first of these that names a registered shell is taken: the option `--shell=<id>` among the arguments, the
/// environment variable `AFTERNIGHT_SHELL`, the setting `interface/shell`, the default. A source that names a shell
/// the registry does not hold, and an option with no value, are passed over with a warning: a mistyped option never
/// stops the application from starting. A value is read without the spaces around it, and an empty one is no choice.
///
/// The chosen shell starts under the style profile its descriptor names. The environment variable
/// `AFTERNIGHT_STYLE_PROFILE` - `classic` or `observatory`, in any case - starts it under another: a developer aid, to
/// look at a profile in a shell that does not name it. A value that names no profile is passed over with a warning.
class AFTERNIGHTSHELLCORE_API ShellSelection
{
public:
    /// The option, written `--shell=<id>`: the value is joined to it, so the argument after it is never read as one.
    static constexpr char kOption[] = "--shell";
    static constexpr char kEnvironmentVariable[] = "AFTERNIGHT_SHELL";
    /// The developer switch that starts the chosen shell under another style profile.
    static constexpr char kStyleProfileVariable[] = "AFTERNIGHT_STYLE_PROFILE";

    ShellSelection() = delete;

    /// Chooses a shell.
    ///
    /// @param arguments   the process's own arguments as `main` received them, the program first. They are the
    ///                    caller's to hand over: `QCoreApplication::arguments()` is never read, because on Windows
    ///                    the application gives `QApplication` an argument list of its own.
    /// @param environment the process's environment.
    /// @param settings    the global settings store, open; null passes the setting over.
    /// @param registry    the shells that can be made.
    [[nodiscard]] static ShellChoice resolve(const QStringList& arguments, const QProcessEnvironment& environment,
                                             IGlobalSettingsProvider* settings, const ShellRegistry& registry);
};

} // namespace an::ui::shell
