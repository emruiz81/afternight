// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightControlsExport.h"

#include "app/ProcessRegistry.h"

#include <QCborMap>
#include <QJsonArray>
#include <QUrl>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace an::ui::extensions
{

enum class ExtensionPackageSource
{
    Bundled,
    Repository,
    LocalPackage,
    User
};

enum class ExtensionPackageSignatureState
{
    Unknown,
    Unsigned,
    Verified,
    Failed
};

enum class ExtensionSdkBackend
{
    Runtime,
    Protocol,
    Rpc
};

[[nodiscard]] AFTERNIGHTCONTROLS_API bool extension_subsystem_supported_in_current_build();
/// A key whose Ed25519 signatures a repository package or a release asset may be verified against: its id, as a signed
/// package or an index names it, and its public key in base64.
struct ExtensionSigningKey
{
    std::string key_id;
    std::string public_key_base64;
};

/// The keys this build trusts: the official extension signing key, and nothing else.
[[nodiscard]] AFTERNIGHTCONTROLS_API std::vector<ExtensionSigningKey> official_extension_signing_keys();

/// Whether this build trusts a key id - whether it is one of `official_extension_signing_keys()`.
[[nodiscard]] AFTERNIGHTCONTROLS_API bool official_extension_signing_key_trusted(std::string_view key_id);

enum class ExtensionLaunchMode
{
    SingleImage,
    Workflow
};

enum class ExtensionLoadStatus
{
    Ready,
    Incompatible,
    Invalid
};

/// What an interactive frame of an extension's real-time preview may run its `execute_preview` over, as its manifest's
/// `capabilities.preview_scaling` states it.
enum class ExtensionPreviewScaling
{
    /// `full_only`, the default: every frame over the whole image, since the host cannot know what a Python body does
    /// with the image's scale.
    FullOnly,

    /// `decimated`: a body that maps every pixel alone by a rule that depends neither on where it lies nor on the
    /// image's size, so a frame of a drag may run it over the image reduced to what the preview view paints.
    Decimated,

    /// `native_region`: a body whose answer at a pixel reads nothing farther than `preview_region_margin` pixels around
    /// it, so a frame of a drag may run it at full scale over the part of the image the preview view shows.
    NativeRegion
};

/// The manifest's word for a preview scaling: `full_only`, `decimated` or `native_region`.
[[nodiscard]] AFTERNIGHTCONTROLS_API std::string_view
extensionPreviewScalingName(ExtensionPreviewScaling scaling) noexcept;

/// The preview scaling a manifest's word names, or nothing for another word.
[[nodiscard]] AFTERNIGHTCONTROLS_API std::optional<ExtensionPreviewScaling>
extensionPreviewScalingFromName(std::string_view name) noexcept;

/// The farthest reach around a pixel a manifest may state for a `native_region` preview.
inline constexpr std::uint32_t kMaxExtensionPreviewRegionMargin = 4096u;

struct ExtensionCapabilities
{
    bool execute = true;
    bool preview = false;
    bool keep_open = false;
    /// What an interactive preview frame may run the extension over, and the reach a `native_region` body states.
    ExtensionPreviewScaling preview_scaling = ExtensionPreviewScaling::FullOnly;
    std::uint32_t preview_region_margin = 0u;
    bool masks_input = false;
    bool masks_output = false;
    bool weights_input = false;
    bool starless_generator = false;
    bool star_mask_generator = false;
    std::vector<int> source_channel_filter;
    // Deprecated compatibility metadata: parsed for backward compatibility only and
    // ignored by runtime behavior.
    std::string ui = "auto";
    bool stateful_sequence = false;
};

struct ExtensionPipSpec
{
    std::vector<std::string> find_links;
    std::vector<std::string> index_urls;
    std::vector<std::string> extra_index_urls;
    bool require_hashes = false;
    bool no_deps = false;
};

enum class ExtensionDependencyContext
{
    Private,
    SharedGroup,
    SharedHost
};

struct ExtensionDependencySpec
{
    ExtensionDependencyContext dependency_context = ExtensionDependencyContext::Private;
    std::string requirements_file;
    ExtensionPipSpec pip;
    std::string shared_group;
    std::string shared_host_profile;
    std::vector<std::string> bundled_runtime_packages;

    [[nodiscard]] bool declared() const noexcept
    {
        return dependency_context != ExtensionDependencyContext::Private || !requirements_file.empty() ||
               !pip.find_links.empty() || !pip.index_urls.empty() || !pip.extra_index_urls.empty() ||
               !shared_group.empty() || !shared_host_profile.empty() || !bundled_runtime_packages.empty();
    }
};

struct ExtensionProcessManifest
{
    std::string id;
    std::string id_suffix;
    std::string class_name;
    std::string display_name;
    std::string process_name;
    std::string action_object_name;
    std::string category;
    ExtensionCapabilities capabilities;
};

struct ExtensionRepositoryMetadata
{
    std::string summary;
    std::string description;
    std::string author;
    std::string license;
    std::string publisher_id;
    std::string attribution;
    std::string original_author;
    std::string original_project;
    std::string original_source_url;
    std::string upstream_commit;
    std::string homepage_url;
    std::string repository_url;
    std::string support_url;
    std::string icon_path;
    std::vector<std::string> screenshots;
    std::vector<std::string> tags;
    std::string package_hash_sha256;
    std::string archive_hash_sha256;
    ExtensionPackageSignatureState signature_state = ExtensionPackageSignatureState::Unknown;
    std::string signature_detail;
};

struct ExtensionManifest
{
    std::string id;
    std::string name;
    std::string version;
    std::string min_app_version;
    std::string entry_point;
    std::string category;
    std::string python_compat;
    std::string manifest_path;
    std::string package_root;
    ExtensionSdkBackend sdk_backend = ExtensionSdkBackend::Runtime;
    ExtensionLaunchMode launch_mode = ExtensionLaunchMode::SingleImage;
    bool requires_exclusive_access = false;
    bool development_only = false;
    std::vector<std::string> runtime_targets;
    std::vector<std::string> binary_vendor_paths;
    ExtensionDependencySpec dependencies;
    std::vector<ExtensionProcessManifest> processes;
    ExtensionRepositoryMetadata repository_metadata;
};

/// A package the application runs from whose files are somewhere else on every run: an AppImage, which its runtime
/// mounts at a new directory or extracts again, with that start's write times, each time it starts. The bundled Python
/// and hosts inside it are known by their place in the package and by the package's identity, not by where they are
/// in this run or when they were written.
struct RelocatablePackage
{
    std::filesystem::path root; ///< Where the package's files are in this run.
    std::string identity; ///< What tells this package from another, the same in every run.
};

struct ExtensionDiscoveryContext
{
    std::string bundled_python_version = "3.14.0";
    std::string app_version;
    std::string runtime_target;
    std::string host_bootstrap_fingerprint;
    std::filesystem::path bundled_python_executable;
    std::filesystem::path extension_host_executable;
    std::filesystem::path extension_host_lite_executable;
    std::filesystem::path runtime_root;
    bool runtime_backend_available = extension_subsystem_supported_in_current_build();
    bool protocol_backend_available = extension_subsystem_supported_in_current_build();
    bool rpc_backend_available = false;
    bool workflow_launch_mode_available = false;
    int settings_schema_inspection_timeout_ms = 10000;
    bool eager_settings_schema_inspection = true;
    std::filesystem::path environments_root;
    /// The relocatable package the bundled Python and hosts are in, or nothing for an installed or deployed tree.
    std::optional<RelocatablePackage> package;
};

struct ExtensionOperationCancellationToken
{
    std::shared_ptr<std::atomic_bool> requested = std::make_shared<std::atomic_bool>(false);

    [[nodiscard]] bool isCancellationRequested() const noexcept
    {
        return requested && requested->load(std::memory_order_acquire);
    }

    void requestCancellation() const noexcept
    {
        if (requested)
        {
            requested->store(true, std::memory_order_release);
        }
    }
};

struct ExtensionDiscoveryOptions
{
    std::vector<std::filesystem::path> bundled_roots;
    std::vector<std::filesystem::path> repository_roots;
    std::vector<std::filesystem::path> local_package_roots;
    std::vector<std::filesystem::path> user_roots;
    ExtensionDiscoveryContext context;
    bool include_development_only_extensions = false;
    ExtensionOperationCancellationToken cancellation_token;
};

enum class ExtensionEnvironmentState
{
    Ready,
    Missing,
    Stale,
    Failed,
    Bootstrapping
};

enum class ExtensionEnvironmentInspectionMode
{
    Routine,
    VerifyContents
};

enum class ExtensionSharedHostProfileFingerprintSource
{
    NotApplicable,
    DiscoveryCache,
    ProvisioningReceipt,
    ContentAudit
};

struct ExtensionEnvironmentSnapshot
{
    ExtensionEnvironmentState state = ExtensionEnvironmentState::Missing;
    /// Whether a Stale environment still holds what its package installs, and is Stale only for what the application
    /// gives it - the interpreter it names, the runtime packages it copies from the application, the bytecode they
    /// are compiled to - or for the revision of its package it was last checked for: refreshing it in place brings it
    /// up to date, and nothing is installed again.
    bool refreshable = false;
    ExtensionSharedHostProfileFingerprintSource shared_host_profile_fingerprint_source =
        ExtensionSharedHostProfileFingerprintSource::NotApplicable;
    /// The build of the application the environment is checked against: its version, its bundled Python, its hosts.
    std::string host_bootstrap_fingerprint;
    /// What the environment's installed contents are made from - the locked requirements and the sources they are
    /// installed from, the Python ABI and the runtime target - and nothing else: it names the environment, so a new
    /// build of the application or a new version of the package that installs the same keeps it.
    std::string dependency_fingerprint;
    std::string validation_fingerprint;
    std::string bundled_python_version;
    std::string runtime_target;
    std::string diagnostic;
    std::filesystem::path bundled_python_executable;
    std::filesystem::path runtime_root;
    std::filesystem::path environments_root;
    std::filesystem::path environment_path;
    std::filesystem::path receipt_path;
    std::filesystem::path shared_host_profile_receipt_path;
};

struct ExtensionEnvironmentInventoryEntry
{
    std::filesystem::path environment_path;
    std::string extension_id;
    std::string extension_version;
    std::string shared_group;
    std::string shared_host_profile;
    std::string dependency_fingerprint;
    std::string runtime_target;
    std::uintmax_t size_bytes = 0;
    std::filesystem::file_time_type last_write_time{};
    bool installed_identity_present = false;
    bool ready_replacement_available = false;
    bool provisioning = false;
    bool orphaned = false;
};

struct ExtensionEnvironmentCleanupPolicy
{
    std::chrono::seconds minimum_age = std::chrono::seconds::zero();
    bool protect_installed_without_ready_replacement = true;
    bool protect_provisioning = true;
};

struct ExtensionEnvironmentStorageSummary
{
    std::uintmax_t total_bytes = 0;
    std::uintmax_t active_bytes = 0;
    std::uintmax_t reclaimable_bytes = 0;
    std::uintmax_t protected_bytes = 0;
    std::size_t total_environment_count = 0;
    std::size_t active_environment_count = 0;
    std::size_t reclaimable_environment_count = 0;
    std::size_t protected_environment_count = 0;
};

struct ExtensionInstalledDistribution
{
    std::string name;
    std::string version;
    std::string source;
    std::string artifact_hash;
};

enum class ExtensionRuntimeLaunchOutcome
{
    Succeeded,
    Failed
};

enum class ExtensionRuntimeTerminationKind
{
    Crashed,
    ForcedStop,
    Disconnected,
    Unresponsive,
    Failed,
    Exited
};

struct ExtensionRuntimeTerminationDiagnostics
{
    std::string session_id;
    int process_exit_code = -1;
    std::int64_t last_heartbeat_elapsed_ms = -1;
    std::string in_flight_request_id;
    std::vector<std::string> outstanding_buffer_ids;
};

struct ExtensionRuntimeStatusSnapshot
{
    bool has_launch_result = false;
    ExtensionRuntimeLaunchOutcome launch_outcome = ExtensionRuntimeLaunchOutcome::Succeeded;
    std::string launch_detail;
    bool has_termination_result = false;
    ExtensionRuntimeTerminationKind termination_kind = ExtensionRuntimeTerminationKind::Failed;
    std::string termination_detail;
    ExtensionRuntimeTerminationDiagnostics termination_diagnostics;
};

enum class ExtensionSettingsSchemaState
{
    Unavailable,
    Ready,
    Failed
};

struct ExtensionSettingsSchemaSnapshot
{
    ExtensionSettingsSchemaState state = ExtensionSettingsSchemaState::Unavailable;
    std::string validation_fingerprint;
    std::string diagnostic;
    std::filesystem::path cache_path;
    QJsonArray schema;
    std::map<std::string, QJsonArray> process_schemas;
    std::map<std::string, QJsonArray> process_params_schemas;
    bool from_cache = false;
};

struct ExtensionPackageProvenance
{
    ExtensionPackageSource source = ExtensionPackageSource::User;
    std::string source_repository_url;
    std::string local_package_path;
    std::string installed_at_utc;
    std::string archive_hash_sha256;
    std::string selected_release_asset;
    ExtensionPackageSignatureState signature_state = ExtensionPackageSignatureState::Unknown;
    std::string signature_detail;
    std::string signature_algorithm;
    std::string signature_key_id;
    std::string host_bootstrap_fingerprint;
    std::filesystem::path receipt_path;
};

struct ExtensionLoadResult
{
    std::filesystem::path manifest_path;
    std::filesystem::path package_root;
    ExtensionPackageSource source = ExtensionPackageSource::User;
    ExtensionLoadStatus status = ExtensionLoadStatus::Invalid;
    std::string diagnostic;
    std::optional<ExtensionManifest> manifest;
    std::optional<ExtensionEnvironmentSnapshot> environment;
    std::optional<ExtensionSettingsSchemaSnapshot> settings_schema;
    std::optional<ExtensionRuntimeStatusSnapshot> runtime_status;
    ExtensionPackageProvenance provenance;
};

enum class ExtensionPackageValidationSeverity
{
    Warning,
    Error
};

struct ExtensionPackageValidationIssue
{
    ExtensionPackageValidationSeverity severity = ExtensionPackageValidationSeverity::Error;
    std::string code;
    std::string message;
    std::filesystem::path path;
};

struct ExtensionPackageValidationOptions
{
    ExtensionDiscoveryContext context;
    ExtensionPackageSource source = ExtensionPackageSource::LocalPackage;
    bool require_current_runtime_compatibility = false;
    bool require_repository_metadata = false;
    bool require_archive_hash = false;
};

struct ExtensionPackageValidationResult
{
    bool valid = false;
    ExtensionLoadResult load_result;
    std::vector<ExtensionPackageValidationIssue> issues;
};

struct ExtensionRepositoryReleaseAsset
{
    std::string name;
    std::string download_url;
    std::string package_hash_sha256;
    std::vector<std::string> runtime_targets;
    ExtensionPackageSignatureState signature_state = ExtensionPackageSignatureState::Unknown;
    std::string signature_detail;
    std::string signature_algorithm;
    std::string signature_key_id;
    std::string signature;
};

struct ExtensionRepositoryRelease
{
    std::string version;
    std::string min_app_version;
    int package_format_version = 0;
    int protocol_version = 0;
    int sdk_version = 0;
    std::string sdk_backend;
    std::string launch_mode;
    std::vector<std::string> runtime_targets;
    std::string changelog;
    std::string published_at_utc;
    std::vector<ExtensionRepositoryReleaseAsset> assets;
};

struct ExtensionRepositoryPackage
{
    std::string id;
    std::string name;
    std::string summary;
    std::string description;
    std::string author;
    std::string license;
    std::string publisher_id;
    std::string attribution;
    std::string original_author;
    std::string original_project;
    std::string original_source_url;
    std::string upstream_commit;
    std::string homepage_url;
    std::string repository_url;
    std::string support_url;
    std::string latest_version;
    std::vector<std::string> tags;
    std::string icon_url;
    std::vector<ExtensionRepositoryRelease> releases;
};

struct ExtensionRepositoryIndex
{
    int schema_version = 0;
    std::string repository;
    std::string updated_at_utc;
    bool official = true;
    std::vector<ExtensionRepositoryPackage> packages;
};

struct ExtensionRepositoryIndexValidationResult
{
    bool valid = false;
    std::string diagnostic;
    ExtensionRepositoryIndex index;
    std::vector<ExtensionPackageValidationIssue> issues;
};

struct ExtensionRepositoryIndexFetchRequest
{
    QUrl index_url;
    int timeout_ms = 15000;
    bool require_official = true;
    bool bypass_network_cache = true;
    ExtensionOperationCancellationToken cancellation_token;
};

struct ExtensionRepositoryIndexFetchResult
{
    bool success = false;
    std::string diagnostic;
    QUrl index_url;
    ExtensionRepositoryIndexValidationResult validation;
};

enum class ExtensionRepositoryInstallState
{
    NotInstalled,
    Installed,
    UpdateAvailable,
    Incompatible
};

struct ExtensionRepositoryBrowseOptions
{
    ExtensionDiscoveryContext context;
    std::string search_query;
};

struct ExtensionRepositoryBrowseEntry
{
    std::string id;
    std::string name;
    std::string summary;
    std::string description;
    std::string author;
    std::string publisher_id;
    std::string license;
    std::string attribution;
    std::string original_author;
    std::string original_project;
    std::string original_source_url;
    std::string upstream_commit;
    std::string homepage_url;
    std::string repository_url;
    std::string support_url;
    std::string icon_url;
    std::vector<std::string> tags;
    std::string latest_version;
    std::string installed_version;
    bool installed_source_available = false;
    ExtensionPackageSource installed_source = ExtensionPackageSource::User;
    std::string selected_release_version;
    std::string release_min_app_version;
    int release_package_format_version = 0;
    int release_protocol_version = 0;
    int release_sdk_version = 0;
    std::string release_sdk_backend;
    std::string release_launch_mode;
    std::vector<std::string> release_runtime_targets;
    std::string release_changelog;
    std::string release_published_at_utc;
    std::string selected_asset_name;
    std::string selected_asset_download_url;
    std::string selected_asset_package_hash_sha256;
    std::vector<std::string> selected_asset_runtime_targets;
    ExtensionPackageSignatureState selected_asset_signature_state = ExtensionPackageSignatureState::Unknown;
    std::string selected_asset_signature_detail;
    std::string selected_asset_signature_algorithm;
    std::string selected_asset_signature_key_id;
    std::string selected_asset_signature;
    bool runtime_compatible = false;
    std::string compatibility_diagnostic;
    ExtensionRepositoryInstallState install_state = ExtensionRepositoryInstallState::NotInstalled;
    bool installed_status_available = false;
    ExtensionLoadStatus installed_load_status = ExtensionLoadStatus::Invalid;
    bool installed_environment_available = false;
    ExtensionEnvironmentState installed_environment_state = ExtensionEnvironmentState::Missing;
    std::string installed_status_diagnostic;
    std::string installed_environment_diagnostic;
};

struct ExtensionPackageArchiveLimits
{
    std::uint64_t max_entry_size_bytes = 768ull * 1024ull * 1024ull;
    std::uint64_t max_total_size_bytes = 2ull * 1024ull * 1024ull * 1024ull;
    std::size_t max_file_count = 10000;
    std::size_t max_path_depth = 32;
};

struct ExtensionPackageExtractionOptions
{
    ExtensionPackageArchiveLimits limits;
    bool preserve_executable_permissions = true;
};

struct ExtensionPackageExtractionResult
{
    bool success = false;
    std::string diagnostic;
    std::filesystem::path destination_root;
    std::vector<std::filesystem::path> extracted_paths;
    std::uint64_t total_uncompressed_size_bytes = 0;
    std::size_t file_count = 0;
};

struct ExtensionOperationProgress
{
    std::string phase_id;
    std::string message;
    int current = 0;
    int total = 0;
    bool indeterminate = true;
};

using ExtensionOperationProgressCallback = std::function<void(const ExtensionOperationProgress&)>;

struct ExtensionSettingsActionResult
{
    bool ok = false;
    bool timed_out = false;
    bool cancelled = false;
    std::string message;
    std::string tone = "error";
    QCborMap settings_updates;
    QCborMap transient_updates;
};

using ExtensionSettingsActionUpdateCallback = std::function<void(const ExtensionSettingsActionResult&)>;

struct ExtensionSettingsActionRequest
{
    ExtensionLoadResult load_result;
    ExtensionDiscoveryContext context;
    std::string action_id;
    QCborMap settings_snapshot;
    int timeout_ms = 60000;
    ExtensionOperationCancellationToken cancellation_token;
    ExtensionSettingsActionUpdateCallback update_callback;
};

struct ExtensionStartupCachePrimingOptions
{
    ExtensionDiscoveryContext context;
    bool require_ready_environment = true;
    bool require_settings_schema = true;
    ExtensionOperationProgressCallback progress_callback;
    ExtensionOperationCancellationToken cancellation_token;
};

struct ExtensionStartupCachePrimingResult
{
    bool success = false;
    std::string diagnostic;
    ExtensionLoadResult load_result;
    bool environment_ready = false;
    bool settings_schema_ready = false;
    bool settings_schema_from_cache = false;
};

struct ExtensionPackageInstallRequest
{
    std::filesystem::path archive_path;
    std::filesystem::path install_root;
    ExtensionDiscoveryContext context;
    ExtensionPackageSource source = ExtensionPackageSource::LocalPackage;
    std::string expected_archive_hash_sha256;
    std::string expected_extension_id;
    std::string expected_extension_version;
    std::string source_repository_url;
    std::string selected_release_asset;
    std::vector<std::string> selected_asset_runtime_targets;
    ExtensionPackageSignatureState signature_state = ExtensionPackageSignatureState::Unsigned;
    std::string signature_detail;
    std::string signature_algorithm;
    std::string signature_key_id;
    std::string signature;
    bool require_repository_metadata = true;
    bool require_current_runtime_compatibility = true;
    bool provision_environment = false;
    bool prime_startup_caches = false;
    bool require_settings_schema_cache = true;
    bool precompile_python_bytecode = true;
    bool replace_existing = true;
    ExtensionPackageExtractionOptions extraction_options;
    ExtensionOperationProgressCallback progress_callback;
    ExtensionOperationCancellationToken cancellation_token;
};

struct ExtensionPackageInstallResult
{
    bool success = false;
    bool cancelled = false;
    std::string diagnostic;
    std::filesystem::path activated_package_root;
    std::filesystem::path staging_root;
    std::string archive_hash_sha256;
    ExtensionPackageValidationResult validation;
    std::optional<ExtensionEnvironmentSnapshot> environment;
    std::optional<ExtensionStartupCachePrimingResult> startup_cache_priming;
    bool package_bytecode_precompiled = false;
};

struct ExtensionPackageRemoveRequest
{
    std::filesystem::path install_root;
    std::string extension_id;
    bool require_existing = true;
    ExtensionOperationProgressCallback progress_callback;
    ExtensionOperationCancellationToken cancellation_token;
};

struct ExtensionPackageRemoveResult
{
    bool success = false;
    bool cancelled = false;
    std::string diagnostic;
    std::filesystem::path removed_package_root;
};

struct ExtensionPackageRepairRequest
{
    std::filesystem::path install_root;
    std::string extension_id;
    ExtensionDiscoveryContext context;
    ExtensionPackageSource source = ExtensionPackageSource::LocalPackage;
    bool require_repository_metadata = true;
    bool require_current_runtime_compatibility = true;
    bool provision_environment = true;
    bool prime_startup_caches = true;
    bool require_settings_schema_cache = true;
    bool precompile_python_bytecode = true;
    bool require_existing = true;
    ExtensionOperationProgressCallback progress_callback;
    ExtensionOperationCancellationToken cancellation_token;
};

struct ExtensionPackageRepairResult
{
    bool success = false;
    bool cancelled = false;
    std::string diagnostic;
    std::filesystem::path repaired_package_root;
    ExtensionPackageValidationResult validation;
    std::optional<ExtensionEnvironmentSnapshot> environment;
    std::optional<ExtensionStartupCachePrimingResult> startup_cache_priming;
    bool package_bytecode_precompiled = false;
};

class AFTERNIGHTCONTROLS_API ExtensionRuntimeStatusRegistry
{
public:
    [[nodiscard]] static ExtensionRuntimeStatusRegistry& instance();

    void clear();
    void recordLaunchSucceeded(std::string_view extension_id, std::string_view detail = {});
    void recordLaunchFailed(std::string_view extension_id, std::string_view detail = {});
    void recordTermination(std::string_view extension_id, ExtensionRuntimeTerminationKind kind,
                           std::string_view detail = {},
                           const ExtensionRuntimeTerminationDiagnostics* diagnostics = nullptr);
    [[nodiscard]] std::optional<ExtensionRuntimeStatusSnapshot>
    snapshotForExtension(std::string_view extension_id) const;
    void mergeIntoDiscoveryResults(std::vector<ExtensionLoadResult>* results) const;

private:
    mutable std::mutex mutex_;
    std::map<std::string, ExtensionRuntimeStatusSnapshot, std::less<>> statuses_;
};

[[nodiscard]] AFTERNIGHTCONTROLS_API std::string current_runtime_target();

/// Whether this run is told to reach no extension repository or package index:
/// `AFTERNIGHT_DISABLE_EXTENSION_REPOSITORY_ NETWORK` or `AFTERNIGHT_EXTENSION_REPOSITORY_OFFLINE` set to 1, true, yes
/// or on, in any case.
[[nodiscard]] AFTERNIGHTCONTROLS_API bool extensionNetworkDisabledByPolicy();

[[nodiscard]] AFTERNIGHTCONTROLS_API std::filesystem::path
bundled_extensions_root(const std::filesystem::path& app_dir);

[[nodiscard]] AFTERNIGHTCONTROLS_API std::filesystem::path user_extensions_root();

[[nodiscard]] AFTERNIGHTCONTROLS_API std::filesystem::path repository_extensions_root();

/// The AppImage an application in `appDir` runs from, given the package file and the directory holding this run's copy
/// of its files - what the AppImage runtime states as APPIMAGE and APPDIR - or nothing when the file is not one, the
/// directory is not one or the application is not inside it. Its identity is the package file's size and write time,
/// which stay the same while the same file is started, wherever it is moved.
[[nodiscard]] AFTERNIGHTCONTROLS_API std::optional<RelocatablePackage>
appImagePackageForAppDir(const std::filesystem::path& appDir, const std::filesystem::path& appImageFile,
                         const std::filesystem::path& appImageRoot);

[[nodiscard]] AFTERNIGHTCONTROLS_API ExtensionDiscoveryOptions
default_extension_discovery_options_for_app_dir(const std::filesystem::path& app_dir);

[[nodiscard]] AFTERNIGHTCONTROLS_API ExtensionDiscoveryOptions default_extension_discovery_options();

/// Persists and restores the last known ready extension set for the startup
/// process-menu fast path. Entries are accepted only when their manifest file
/// guard, runtime target, host fingerprint, and environment-ready markers
/// still match the current installation.
[[nodiscard]] AFTERNIGHTCONTROLS_API bool
store_ready_extension_startup_catalog(const std::vector<ExtensionLoadResult>& results,
                                      const ExtensionDiscoveryContext& context);

[[nodiscard]] AFTERNIGHTCONTROLS_API std::vector<ExtensionLoadResult>
load_ready_extension_startup_catalog(const ExtensionDiscoveryOptions& options);

[[nodiscard]] AFTERNIGHTCONTROLS_API std::string extension_package_source_id(ExtensionPackageSource source);

[[nodiscard]] AFTERNIGHTCONTROLS_API std::optional<ExtensionPackageSource>
extension_package_source_from_id(std::string_view source_id);

[[nodiscard]] AFTERNIGHTCONTROLS_API std::string extension_sdk_backend_id(ExtensionSdkBackend backend);

[[nodiscard]] AFTERNIGHTCONTROLS_API std::string extension_host_mode_label(ExtensionSdkBackend backend);

[[nodiscard]] AFTERNIGHTCONTROLS_API std::string extension_host_mode_description(ExtensionSdkBackend backend);

[[nodiscard]] AFTERNIGHTCONTROLS_API std::string default_extension_action_object_name(std::string_view extension_id,
                                                                                      std::string_view id_suffix = {});

[[nodiscard]] AFTERNIGHTCONTROLS_API ExtensionLoadResult load_extension_manifest_file(
    const std::filesystem::path& manifest_path, ExtensionPackageSource source, const ExtensionDiscoveryContext& context,
    ExtensionOperationCancellationToken cancellation_token = {});

[[nodiscard]] AFTERNIGHTCONTROLS_API ExtensionPackageValidationResult validate_extension_manifest_file(
    const std::filesystem::path& manifest_path, const ExtensionPackageValidationOptions& options);

[[nodiscard]] AFTERNIGHTCONTROLS_API ExtensionPackageValidationResult validate_extension_package_root(
    const std::filesystem::path& package_root, const ExtensionPackageValidationOptions& options);

/// Validates a repository index file; a verified release asset must name one of `trusted_keys`, which are the keys this
/// build trusts unless the caller states others.
[[nodiscard]] AFTERNIGHTCONTROLS_API ExtensionRepositoryIndexValidationResult validate_extension_repository_index_file(
    const std::filesystem::path& index_path,
    const std::vector<ExtensionSigningKey>& trusted_keys = official_extension_signing_keys());

[[nodiscard]] AFTERNIGHTCONTROLS_API QUrl official_extension_repository_index_url();

[[nodiscard]] AFTERNIGHTCONTROLS_API QUrl resolve_extension_repository_asset_url(const QUrl& index_url,
                                                                                 std::string_view download_url);

[[nodiscard]] AFTERNIGHTCONTROLS_API std::vector<ExtensionRepositoryBrowseEntry>
build_extension_repository_browse_entries(const ExtensionRepositoryIndex& index,
                                          const std::vector<ExtensionLoadResult>& installed_extensions,
                                          const ExtensionRepositoryBrowseOptions& options);

[[nodiscard]] AFTERNIGHTCONTROLS_API ExtensionPackageExtractionResult extract_extension_package_archive(
    const std::filesystem::path& archive_path, const std::filesystem::path& destination_root,
    const ExtensionPackageExtractionOptions& options = {});

[[nodiscard]] AFTERNIGHTCONTROLS_API ExtensionStartupCachePrimingResult prime_extension_startup_caches(
    const ExtensionLoadResult& load_result, const ExtensionStartupCachePrimingOptions& options);

[[nodiscard]] AFTERNIGHTCONTROLS_API ExtensionSettingsActionResult
run_extension_settings_action(const ExtensionSettingsActionRequest& request);

class AFTERNIGHTCONTROLS_API ExtensionPackageTransactionService
{
public:
    /// A service that verifies a signed repository package against `trusted_keys` - the keys this build trusts unless
    /// the caller states others.
    explicit ExtensionPackageTransactionService(
        std::vector<ExtensionSigningKey> trusted_keys = official_extension_signing_keys())
        : m_trusted_keys(std::move(trusted_keys))
    {
    }

    [[nodiscard]] ExtensionPackageInstallResult installArchive(const ExtensionPackageInstallRequest& request) const;

    [[nodiscard]] ExtensionPackageRemoveResult
    removeInstalledPackage(const ExtensionPackageRemoveRequest& request) const;

    [[nodiscard]] ExtensionPackageRepairResult
    repairInstalledPackage(const ExtensionPackageRepairRequest& request) const;

private:
    std::vector<ExtensionSigningKey> m_trusted_keys;
};

class AFTERNIGHTCONTROLS_API ExtensionRepositoryClient
{
public:
    [[nodiscard]] ExtensionRepositoryIndexFetchResult
    fetchIndex(const ExtensionRepositoryIndexFetchRequest& request = {}) const;
};

class AFTERNIGHTCONTROLS_API ExtensionManager
{
public:
    [[nodiscard]] std::vector<ExtensionLoadResult> discoverExtensions(const ExtensionDiscoveryOptions& options) const;

    void registerDiscoveredProcesses(const std::vector<ExtensionLoadResult>& discovered,
                                     process_registry::ProcessRegistry& registry) const;
};

} // namespace an::ui::extensions
