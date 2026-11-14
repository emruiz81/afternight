// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// Launching a process the registry holds, an extension's included, and what the application knows of its extensions.

#include "AfterNightShellCoreExport.h"

#include "app/ExtensionManager.h"

#include <QString>

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace an::ui::extensions
{
class ExtensionLaunchPreparations;
struct ExtensionLaunchPreparationOutcome;
} // namespace an::ui::extensions

namespace an::ui::shell
{

class ApplicationCore;

/// An extension the packages provide, as the application lists it: once, under its name, with where to read of it.
struct ExtensionFamily
{
    QString displayName;
    std::string homepageUrl;
    std::string repositoryUrl;
    std::string supportUrl;
};

/// The options the application discovers the installed extensions with.
///
/// @param eagerSettingsSchemaInspection whether each package's settings schema is read as it is found.
/// @param cancellationToken what stops a discovery under way.
[[nodiscard]] AFTERNIGHTSHELLCORE_API an::ui::extensions::ExtensionDiscoveryOptions
defaultExtensionDiscoveryOptions(bool eagerSettingsSchemaInspection = true,
                                 const an::ui::extensions::ExtensionOperationCancellationToken& cancellationToken = {});

/// Whether a package's native parameter schemas are cached for every process it provides; a package that is not run
/// by the full host needs none.
[[nodiscard]] AFTERNIGHTSHELLCORE_API bool
hasCachedNativeParameterSchemas(const an::ui::extensions::ExtensionLoadResult& result);

/// Primes the start-up caches of every ready package whose native parameter schemas are not cached - one whose
/// environment was just refreshed, taken over or provisioned has none for what it is now.
/// @return whether any cache was primed.
[[nodiscard]] AFTERNIGHTSHELLCORE_API bool
primeMissingNativeParameterSchemas(const std::vector<an::ui::extensions::ExtensionLoadResult>& discovered,
                                   const an::ui::extensions::ExtensionDiscoveryOptions& discoveryOptions);

/// Launches the processes the registry holds, from the menus, the recent and favourite lists and the shell: a process
/// with a window of its own, one that opens none, or one that acts on the image the image actions act on
/// (`ViewService::subjectView`). An extension's process whose environment is not ready is prepared first, and launched
/// once it is; what stands in the way of a launch is said over the shell's window. A process launched joins the
/// processes used last. Used on the GUI thread.
class AFTERNIGHTSHELLCORE_API ProcessLaunchService
{
public:
    /// What the application does around a launch that is not the launch's to do.
    struct Hooks
    {
        /// The processes used last changed: the lists that show them are made again.
        std::function<void()> recentProcessesChanged;
        /// The packages are as `discovered` says once an extension's environment was prepared: its launches are bound,
        /// and the actions and the menus take what the packages are now.
        std::function<void(const std::vector<an::ui::extensions::ExtensionLoadResult>& discovered)> extensionsPrepared;
    };

    explicit ProcessLaunchService(ApplicationCore& core);
    ~ProcessLaunchService();

    ProcessLaunchService(const ProcessLaunchService&) = delete;
    ProcessLaunchService& operator=(const ProcessLaunchService&) = delete;
    ProcessLaunchService(ProcessLaunchService&&) = delete;
    ProcessLaunchService& operator=(ProcessLaunchService&&) = delete;

    /// Replaces the hooks; an empty one does nothing.
    void setHooks(Hooks hooks);

    /// Launches the registry's process `processId`.
    /// @return false when the registry does not hold it, it cannot be launched, or what it acts on refused it; true
    ///         when it was launched, its environment is being prepared, or it was told there is no image to act on.
    bool launch(const std::string& processId);

    /// Completes the launch of a process whose extension's environment a launch prepared: the registry takes what the
    /// packages are now, the application is told (`Hooks::extensionsPrepared`), and the process is launched when its
    /// environment was made ready, or what still stands in the way is said once, with where to deal with it.
    void completePreparedExtensionLaunch(const std::string& processId, const std::string& extensionId,
                                         const an::ui::extensions::ExtensionLaunchPreparationOutcome& outcome);

    /// Stops every preparation a launch started, as the application's closing does.
    void cancelPreparations();

    /// Lets go of the preparations and their windows, as the shell they belong to goes.
    void releasePreparations();

    /// The registry's extension processes take the availability the packages `discovered` give them; one that is not
    /// available can be launched no more.
    static void
    refreshRegisteredExtensionDescriptors(const std::vector<an::ui::extensions::ExtensionLoadResult>& discovered);

    /// The extensions the packages `discovered` provide, each once under its name, in the order of their names; an
    /// invalid package, or one with no id, provides none.
    [[nodiscard]] static std::vector<ExtensionFamily>
    extensionFamilies(const std::vector<an::ui::extensions::ExtensionLoadResult>& discovered);

private:
    [[nodiscard]] an::ui::extensions::ExtensionLaunchPreparations& preparations();

    ApplicationCore& m_core;
    Hooks m_hooks;
    std::unique_ptr<an::ui::extensions::ExtensionLaunchPreparations> m_preparations;
};

} // namespace an::ui::shell
