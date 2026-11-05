// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightControlsExport.h"

#include "app/ExtensionManager.h"

#include <cstdint>
#include <filesystem>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace an::ui::extensions
{

inline constexpr auto kAutomaticExtensionEnvironmentCleanupSetting = "extensions/automaticEnvironmentCleanup";

[[nodiscard]] AFTERNIGHTCONTROLS_API std::filesystem::path user_extension_environments_root();
[[nodiscard]] AFTERNIGHTCONTROLS_API std::filesystem::path
bundled_python_executable_for_app_dir(const std::filesystem::path& app_dir);
[[nodiscard]] AFTERNIGHTCONTROLS_API ExtensionEnvironmentCleanupPolicy automatic_extension_environment_cleanup_policy();

class AFTERNIGHTCONTROLS_API ExtensionEnvironmentManager
{
public:
    [[nodiscard]] std::string computeHostBootstrapFingerprint(const ExtensionDiscoveryContext& context) const;

    [[nodiscard]] ExtensionEnvironmentSnapshot inspectEnvironment(
        const ExtensionManifest& manifest, ExtensionPackageSource source, const ExtensionDiscoveryContext& context,
        ExtensionOperationCancellationToken cancellation_token = {},
        ExtensionEnvironmentInspectionMode inspection_mode = ExtensionEnvironmentInspectionMode::Routine) const;

    /// Points an environment made from a relocatable package's Python at `interpreter`, this run's copy of it: the
    /// venv's interpreter link and pyvenv.cfg, which name the copy of the run that made the environment. Nothing is
    /// written while both already name it, and on Windows, whose environments copy their interpreter, nothing is done.
    [[nodiscard]] bool pointEnvironmentAtInterpreter(const std::filesystem::path& environmentPath,
                                                     const std::filesystem::path& interpreter,
                                                     std::string* error = nullptr) const;

    [[nodiscard]] bool beginProvisioning(const ExtensionEnvironmentSnapshot& snapshot,
                                         std::string* error = nullptr) const;

    /// Marks a provisioned environment ready and writes its receipt. `stagedRuntimeFingerprint` is what the runtime
    /// packages copied into it held, which a refresh compares to tell whether they must be copied again; an empty one
    /// makes the first refresh copy them.
    [[nodiscard]] bool completeProvisioning(const ExtensionEnvironmentSnapshot& snapshot,
                                            const std::vector<ExtensionInstalledDistribution>& resolved_distributions,
                                            std::string* error = nullptr,
                                            std::string_view stagedRuntimeFingerprint = {}) const;

    [[nodiscard]] bool failProvisioning(const ExtensionEnvironmentSnapshot& snapshot, std::string_view diagnostic,
                                        std::string* error = nullptr) const;

    [[nodiscard]] bool removeEnvironmentDirectory(const std::filesystem::path& environment_path,
                                                  std::string* error = nullptr) const;

    [[nodiscard]] bool provisionEnvironmentForManifest(
        const ExtensionManifest& manifest, ExtensionPackageSource source, const ExtensionDiscoveryContext& context,
        ExtensionEnvironmentSnapshot* out_snapshot = nullptr,
        std::vector<ExtensionInstalledDistribution>* out_resolved_distributions = nullptr, std::string* error = nullptr,
        ExtensionOperationProgressCallback progress_callback = {},
        ExtensionOperationCancellationToken cancellation_token = {}) const;

    [[nodiscard]] bool precompilePackageBytecode(const ExtensionManifest& manifest,
                                                 const ExtensionEnvironmentSnapshot& snapshot,
                                                 std::string* error = nullptr,
                                                 ExtensionOperationProgressCallback progress_callback = {},
                                                 ExtensionOperationCancellationToken cancellation_token = {}) const;

    /// Brings a `refreshable` environment up to date in place for this run: points it at this run's interpreter,
    /// copies the runtime packages it takes from the application again when the application's copies changed, compiles
    /// what it copied, and records in its receipt the build and the package revision it was refreshed for. Nothing is
    /// installed, so nothing is downloaded. A failure leaves the environment as refreshable as it was.
    ///
    /// @param receiptUnusable set when the refresh failed because the environment's receipt cannot be read, so it can
    ///        never be refreshed; any other failure - a file or a lock another program holds - may pass.
    [[nodiscard]] bool refreshEnvironment(const ExtensionManifest& manifest,
                                          const ExtensionEnvironmentSnapshot& snapshot, std::string* error = nullptr,
                                          ExtensionOperationProgressCallback progressCallback = {},
                                          ExtensionOperationCancellationToken cancellationToken = {},
                                          bool* receiptUnusable = nullptr) const;

    /// A ready environment an earlier layout, version or fingerprint of the package left that already holds what
    /// `snapshot`'s environment installs: the same context and runtime target, built over the same Python minor
    /// version, and every requirement the lock pins installed at its pinned version and hash. In a context packages
    /// share, never one a discovered package still resolves to, and none while they are not stated
    /// (`setDiscoveredEnvironments`). Empty when none does.
    [[nodiscard]] std::filesystem::path findAdoptableEnvironment(const ExtensionManifest& manifest,
                                                                 const ExtensionEnvironmentSnapshot& snapshot) const;

    /// States the environments the discovered packages resolve to now. Taking one over moves it, so in a context the
    /// packages of a group or a host profile share, one a package still resolves to is never taken over for another,
    /// which would leave that package without its environment; a package's own private context is its alone.
    void setDiscoveredEnvironments(std::vector<std::filesystem::path> environments);

    /// The environment every discovered package resolves to, as `setDiscoveredEnvironments` takes them.
    [[nodiscard]] static std::vector<std::filesystem::path>
    discoveredEnvironmentsOf(const std::vector<ExtensionLoadResult>& discovered);

    /// Moves the environment `from` to `snapshot`'s path, refreshes it there and records it as `snapshot`'s: the
    /// distributions it holds are not installed again.
    [[nodiscard]] bool adoptEnvironment(const ExtensionManifest& manifest, const ExtensionEnvironmentSnapshot& snapshot,
                                        const std::filesystem::path& from, std::string* error = nullptr,
                                        ExtensionOperationProgressCallback progressCallback = {},
                                        ExtensionOperationCancellationToken cancellationToken = {}) const;

    /// Makes the environment `manifest` needs ready to launch from: waits while another operation prepares it, then
    /// refreshes it, adopts one an earlier layout left or provisions it, as `provisionEnvironmentForManifest` does -
    /// except that a refreshable environment whose refresh fails for a reason that may pass is tried once more and then
    /// left as it was, the failure said, rather than wiped and installed again.
    [[nodiscard]] bool prepareEnvironmentForLaunch(const ExtensionManifest& manifest, ExtensionPackageSource source,
                                                   const ExtensionDiscoveryContext& context,
                                                   ExtensionEnvironmentSnapshot* outSnapshot = nullptr,
                                                   std::string* error = nullptr,
                                                   ExtensionOperationProgressCallback progressCallback = {},
                                                   ExtensionOperationCancellationToken cancellationToken = {}) const;

    [[nodiscard]] int queueBundledBootstrap(std::vector<ExtensionLoadResult>& discovered, std::string* error = nullptr,
                                            ExtensionOperationCancellationToken cancellation_token = {}) const;

    [[nodiscard]] int processQueuedBundledBootstrap(std::vector<ExtensionLoadResult>& discovered,
                                                    std::string* error = nullptr,
                                                    ExtensionOperationCancellationToken cancellation_token = {}) const;

    /// Queues every discovered package whose environment is not ready, whatever its source, for
    /// `processQueuedEnvironmentMaintenance`, and marks it Bootstrapping: one to refresh in place, one an earlier
    /// environment can be adopted for, and one to provision. A package whose provisioning installs from an index is
    /// queued to be provisioned only when `allowNetwork` says the run may reach one; refreshing and adopting never
    /// need to.
    [[nodiscard]] int queueEnvironmentMaintenance(std::vector<ExtensionLoadResult>& discovered, bool allowNetwork,
                                                  std::string* error = nullptr,
                                                  ExtensionOperationCancellationToken cancellationToken = {}) const;

    /// Refreshes, adopts or provisions what `queueEnvironmentMaintenance` queued, once per environment, and sets each
    /// discovered result's state to what came of it.
    [[nodiscard]] int
    processQueuedEnvironmentMaintenance(std::vector<ExtensionLoadResult>& discovered, std::string* error = nullptr,
                                        ExtensionOperationCancellationToken cancellationToken = {}) const;

    /// Removes, without waiting for any grace period, every environment a ready one replaced: one of an installed
    /// package's own context that no discovered package uses while that context's current environment is ready, and
    /// that nothing is provisioning. One that cannot be removed now, in use elsewhere, is left for a later run.
    [[nodiscard]] bool removeReplacedEnvironments(const std::vector<ExtensionLoadResult>& discovered,
                                                  const ExtensionDiscoveryContext& context,
                                                  std::vector<std::filesystem::path>* removedPaths = nullptr,
                                                  std::uintmax_t* reclaimedBytes = nullptr,
                                                  ExtensionOperationCancellationToken cancellationToken = {}) const;

    [[nodiscard]] std::vector<ExtensionEnvironmentInventoryEntry>
    collectEnvironmentInventory(const std::vector<ExtensionLoadResult>& discovered,
                                const ExtensionDiscoveryContext& context,
                                ExtensionOperationCancellationToken cancellation_token = {}) const;

    [[nodiscard]] std::vector<std::filesystem::path>
    collectOrphanedEnvironmentPaths(const std::vector<ExtensionLoadResult>& discovered,
                                    const ExtensionDiscoveryContext& context) const;

    [[nodiscard]] std::vector<std::filesystem::path>
    collectUnusedEnvironmentPaths(const std::vector<ExtensionLoadResult>& discovered,
                                  const ExtensionDiscoveryContext& context,
                                  const ExtensionEnvironmentCleanupPolicy& policy) const;

    [[nodiscard]] std::vector<ExtensionEnvironmentInventoryEntry>
    collectUnusedEnvironmentInventory(const std::vector<ExtensionLoadResult>& discovered,
                                      const ExtensionDiscoveryContext& context,
                                      const ExtensionEnvironmentCleanupPolicy& policy,
                                      ExtensionOperationCancellationToken cancellation_token = {}) const;

    [[nodiscard]] ExtensionEnvironmentStorageSummary
    summarizeEnvironmentStorage(const std::vector<ExtensionLoadResult>& discovered,
                                const ExtensionDiscoveryContext& context,
                                const ExtensionEnvironmentCleanupPolicy& policy,
                                ExtensionOperationCancellationToken cancellation_token = {}) const;

    [[nodiscard]] bool removeUnusedEnvironments(const std::vector<ExtensionLoadResult>& discovered,
                                                const ExtensionDiscoveryContext& context,
                                                const ExtensionEnvironmentCleanupPolicy& policy,
                                                std::vector<std::filesystem::path>* removed_paths = nullptr,
                                                std::uintmax_t* reclaimed_bytes = nullptr, std::string* error = nullptr,
                                                ExtensionOperationCancellationToken cancellation_token = {}) const;

    [[nodiscard]] bool removeOrphanedEnvironments(const std::vector<ExtensionLoadResult>& discovered,
                                                  const ExtensionDiscoveryContext& context,
                                                  std::vector<std::filesystem::path>* removed_paths = nullptr,
                                                  std::string* error = nullptr,
                                                  ExtensionOperationCancellationToken cancellation_token = {}) const;

private:
    [[nodiscard]] int queueMaintenance(std::vector<ExtensionLoadResult>& discovered, bool bundledOnly,
                                       bool allowNetwork, std::string* error,
                                       const ExtensionOperationCancellationToken& cancellationToken) const;
    [[nodiscard]] int processMaintenance(std::vector<ExtensionLoadResult>& discovered, bool bundledOnly,
                                         std::string* error,
                                         const ExtensionOperationCancellationToken& cancellationToken) const;

    /// Lets go of every environment still queued for maintenance when the processing stops before it: the provisioning
    /// lock this process took for it when it was queued is removed, so a launch prepares it rather than wait for a
    /// lock its own process holds, and its result says it is no longer being prepared.
    void releaseQueuedMaintenance(std::vector<ExtensionLoadResult>& discovered) const;

    /// What a failed refresh of a refreshable environment leads to: provisioning it again - an install or a repair the
    /// person asked for - or, for a launch, keeping it as it was unless its receipt cannot be read.
    enum class RefreshFailure
    {
        Reprovision,
        KeepRefreshable
    };

    [[nodiscard]] bool provisionEnvironment(const ExtensionManifest& manifest, ExtensionPackageSource source,
                                            const ExtensionDiscoveryContext& context,
                                            ExtensionEnvironmentSnapshot* outSnapshot,
                                            std::vector<ExtensionInstalledDistribution>* outResolvedDistributions,
                                            std::string* error, ExtensionOperationProgressCallback progressCallback,
                                            ExtensionOperationCancellationToken cancellationToken,
                                            RefreshFailure refreshFailure) const;

    /// The directories one pass's search for environments to take over has walked, so a pass over many environments
    /// walks each context root once.
    struct AdoptionScan;

    [[nodiscard]] std::filesystem::path
    findAdoptableEnvironment(const ExtensionManifest& manifest, const ExtensionEnvironmentSnapshot& snapshot,
                             const std::optional<std::vector<std::filesystem::path>>& discovered,
                             AdoptionScan* scan = nullptr) const;

    /// The environments the discovered packages resolve to, or none stated.
    std::optional<std::vector<std::filesystem::path>> m_discoveredEnvironments;

    mutable std::mutex m_sharedHostProfileFingerprintCacheMutex;
    mutable std::map<std::string, std::string, std::less<>> m_sharedHostProfileFingerprintCache;

    /// The environments this manager queued for maintenance and has not processed yet: one another operation is
    /// preparing is Bootstrapping too, and is left to it.
    mutable std::mutex m_queuedMaintenanceMutex;
    mutable std::set<std::filesystem::path, std::less<>> m_queuedMaintenancePaths;
};

} // namespace an::ui::extensions
