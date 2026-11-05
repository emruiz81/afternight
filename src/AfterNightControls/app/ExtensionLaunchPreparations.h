// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightControlsExport.h"

#include "app/ExtensionManager.h"

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

class ProgressDialogControl;
class QWidget;

namespace an::ui::extensions
{

/// What preparing an extension's environment for a launch came to.
struct ExtensionLaunchPreparationOutcome
{
    /// Whether the environment was made ready.
    bool prepared = false;
    /// Why it was not, when that is known.
    std::string diagnostic;
    /// The packages as they were found once the environment was prepared; empty when the preparation was stopped.
    std::vector<ExtensionLoadResult> discovered;
};

/// The launches of extensions whose environment is still to be installed, refreshed or repaired. Each extension's
/// environment is prepared on a worker while a window shows the progress, and the launch is completed - or what stands
/// in its way reported - on the GUI thread once the preparation has returned.
///
/// One preparation per extension runs at a time, and it is recorded by the extension until its work has returned, not
/// by its window: a window closed while the work still runs to its next check of the stop is gone well before the work
/// is. A launch while the preparation runs raises its window; a launch while it is being stopped - its Cancel, or its
/// window closed - is started once it has stopped, once however often it was asked for. Lives on the GUI thread.
class AFTERNIGHTCONTROLS_API ExtensionLaunchPreparations
{
public:
    using Progress = std::function<void(const ExtensionOperationProgress&)>;

    /// Prepares one extension's environment on a worker, reporting through the progress, and returns early once the
    /// cancellation is requested.
    using Work =
        std::function<ExtensionLaunchPreparationOutcome(const std::string& extensionId, const Progress& progress,
                                                        const ExtensionOperationCancellationToken& cancellation)>;

    /// Completes a launch on the GUI thread once its preparation returned without being stopped: launches the process
    /// when the outcome is prepared, or reports what stands in the way.
    using Finished = std::function<void(const std::string& processId, const std::string& extensionId,
                                        ExtensionLaunchPreparationOutcome outcome)>;

    /// @param windowParent the window a preparation's progress window belongs to.
    /// @param work what prepares an extension's environment, on a worker of the global thread pool.
    /// @param finished what completes a launch, on the GUI thread.
    ExtensionLaunchPreparations(QWidget* windowParent, Work work, Finished finished);

    /// Stops every preparation still recorded; a completion still queued then finds nothing to complete.
    ~ExtensionLaunchPreparations();

    ExtensionLaunchPreparations(const ExtensionLaunchPreparations&) = delete;
    ExtensionLaunchPreparations& operator=(const ExtensionLaunchPreparations&) = delete;

    /// Prepares the extension's environment and then completes the launch of the process; raises the window of a
    /// preparation of the extension that runs, and starts the launch once one that is being stopped has stopped.
    ///
    /// @param processId the process the launch is of.
    /// @param extensionId the extension that provides it.
    /// @param displayName the process's name, as the window's title states it.
    void launch(const std::string& processId, const std::string& extensionId, const std::string& displayName);

    /// Stops every preparation, as the application's closing does, and starts none afterwards.
    void cancelAll();

    /// Whether a preparation of the extension is recorded: running, or being stopped.
    [[nodiscard]] bool isPreparing(std::string_view extensionId) const;

    /// The window showing the extension's preparation; null when it has none.
    [[nodiscard]] ProgressDialogControl* windowOf(std::string_view extensionId) const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

/// Prepares one extension's environment for a launch, as `ExtensionLaunchPreparations` runs it: finds the extension's
/// package, prepares its environment - never taking over one another package of its shared group still uses - and,
/// unless stopped, finds the packages again, has what the caches lack primed and stores the startup catalog.
///
/// @param extensionId the extension to prepare.
/// @param discoveryOptions where the packages are found; the cancellation is set on them.
/// @param primeCaches primes the startup caches the packages found lack, and says whether it primed any; then the
///                    packages are found once more.
/// @param progress what reports the preparation's progress.
/// @param cancellation what stops the preparation.
/// @return the outcome; `discovered` is empty when the preparation was stopped.
[[nodiscard]] AFTERNIGHTCONTROLS_API ExtensionLaunchPreparationOutcome prepareExtensionEnvironmentForLaunch(
    const std::string& extensionId, ExtensionDiscoveryOptions discoveryOptions,
    const std::function<bool(const std::vector<ExtensionLoadResult>&, const ExtensionDiscoveryOptions&)>& primeCaches,
    const ExtensionLaunchPreparations::Progress& progress, const ExtensionOperationCancellationToken& cancellation);

} // namespace an::ui::extensions
