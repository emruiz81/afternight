// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"

#include "app/ExtensionManager.h"

#include "app/ApplicationStorage.h"
#include "app/ExtensionCommon.h"
#include "app/ExtensionEnvironmentManager.h"

#include <AfterNightPlatform/Logging.h>
#include <AfterNightPlatform/Sha256.h>

#include <QCoreApplication>
#include <QByteArray>
#include <QDateTime>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QSaveFile>
#include <QStringList>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>
#include <QUuid>

#include <zstd.h>
#include <sodium.h>

#include <array>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <tuple>

namespace
{

using an::ui::extensions::ExtensionCapabilities;
using an::ui::extensions::ExtensionDependencyContext;
using an::ui::extensions::ExtensionDependencySpec;
using an::ui::extensions::ExtensionDiscoveryContext;
using an::ui::extensions::ExtensionEnvironmentManager;
using an::ui::extensions::ExtensionEnvironmentState;
using an::ui::extensions::ExtensionLaunchMode;
using an::ui::extensions::ExtensionLoadResult;
using an::ui::extensions::ExtensionLoadStatus;
using an::ui::extensions::ExtensionManifest;
using an::ui::extensions::ExtensionOperationCancellationToken;
using an::ui::extensions::ExtensionOperationProgressCallback;
using an::ui::extensions::ExtensionPackageArchiveLimits;
using an::ui::extensions::ExtensionPackageExtractionOptions;
using an::ui::extensions::ExtensionPackageExtractionResult;
using an::ui::extensions::ExtensionPackageInstallRequest;
using an::ui::extensions::ExtensionPackageInstallResult;
using an::ui::extensions::ExtensionPackageProvenance;
using an::ui::extensions::ExtensionPackageRemoveRequest;
using an::ui::extensions::ExtensionPackageRemoveResult;
using an::ui::extensions::ExtensionPackageSignatureState;
using an::ui::extensions::ExtensionPackageSource;
using an::ui::extensions::ExtensionPackageValidationIssue;
using an::ui::extensions::ExtensionPackageValidationOptions;
using an::ui::extensions::ExtensionPackageValidationResult;
using an::ui::extensions::ExtensionPackageValidationSeverity;
using an::ui::extensions::ExtensionPreviewScaling;
using an::ui::extensions::extensionPreviewScalingFromName;
using an::ui::extensions::ExtensionProcessManifest;
using an::ui::extensions::ExtensionRepositoryBrowseEntry;
using an::ui::extensions::ExtensionRepositoryBrowseOptions;
using an::ui::extensions::ExtensionRepositoryIndex;
using an::ui::extensions::ExtensionRepositoryIndexFetchRequest;
using an::ui::extensions::ExtensionRepositoryIndexFetchResult;
using an::ui::extensions::ExtensionRepositoryIndexValidationResult;
using an::ui::extensions::ExtensionRepositoryInstallState;
using an::ui::extensions::ExtensionRepositoryMetadata;
using an::ui::extensions::ExtensionRepositoryPackage;
using an::ui::extensions::ExtensionRepositoryRelease;
using an::ui::extensions::ExtensionRepositoryReleaseAsset;
using an::ui::extensions::ExtensionSdkBackend;
using an::ui::extensions::ExtensionSettingsActionRequest;
using an::ui::extensions::ExtensionSettingsActionResult;
using an::ui::extensions::ExtensionSettingsSchemaSnapshot;
using an::ui::extensions::ExtensionSettingsSchemaState;
using an::ui::extensions::ExtensionSigningKey;
using an::ui::extensions::ExtensionStartupCachePrimingOptions;
using an::ui::extensions::ExtensionStartupCachePrimingResult;
using an::ui::extensions::isUrlLike;
using an::ui::extensions::kMaxExtensionPreviewRegionMargin;
using an::ui::extensions::readFileText;
using an::ui::process_registry::ProcessAvailability;
using an::ui::process_registry::ProcessDescriptor;
using an::ui::process_registry::ProcessLaunchMode;

constexpr std::string_view kDefaultPythonCompat = ">=3.14,<4";
constexpr std::string_view kUnavailableLaunchReason = "Extension process launch is not wired into this build yet.";
constexpr std::string_view kWorkflowLaunchUnavailableReason =
    "The workflow launch mode is not available in this build yet.";
constexpr std::string_view kSettingsSchemaUnavailableReason =
    "Settings schema unavailable until the extension environment is ready.";
constexpr std::string_view kSettingsSchemaCacheDirectory = "_settings_schema_cache";
constexpr std::string_view kStartupActivationCatalogName = "_startup_activation_catalog.json";
constexpr int kStartupActivationCatalogFormatVersion = 1;
constexpr std::string_view kSettingsActionUpdatePrefix = "@@AFTERNIGHT_SETTINGS_ACTION_UPDATE@@";
constexpr int kSettingsSchemaCacheFormatVersion = 3;
constexpr int kPreviousSettingsSchemaCacheFormatVersion = 2;
constexpr std::string_view kPackageProvenanceReceiptName = "extension_package_receipt.json";
constexpr std::string_view kSignaturePayloadVersion = "afternight-extension-asset-signature-v1";
constexpr std::string_view kSignatureAlgorithmEd25519 = "ed25519";
constexpr std::size_t kTarBlockSize = 512;
constexpr std::uint64_t kMaxTarPaddingBytesPerEntry = kTarBlockSize - 1;
constexpr int kInstallProgressTotal = 10;
constexpr int kInstallProgressScale = 1000;
constexpr int kInstallProvisionStartStep = 5;
constexpr int kInstallProvisionStepCount = 12;
constexpr int kInstallStartupCacheStepCount = 8;

void reportSettingsSchemaInspectionProgress(const ExtensionOperationProgressCallback& callback, std::string phase_id,
                                            std::string message, const int current, const int total,
                                            const bool indeterminate)
{
    if (!callback)
    {
        return;
    }

    if (an::platform::isLoggingInitialized())
    {
        LOG_DEBUG("Extension package operation progress phase='{}' current={} total={} indeterminate={} message='{}'",
                  phase_id, current, total, indeterminate, message);
    }
    callback({std::move(phase_id), std::move(message), current, total, indeterminate});
}

[[nodiscard]] bool cancellationRequested(const ExtensionOperationCancellationToken& cancellation_token)
{
    return cancellation_token.isCancellationRequested();
}

[[nodiscard]] std::vector<std::string> uniqueProcessClassesForInspection(const ExtensionManifest& manifest)
{
    std::vector<std::string> classes;
    std::set<std::string> inspected_classes;
    for (const auto& process : manifest.processes)
    {
        if (inspected_classes.insert(process.class_name).second)
        {
            classes.push_back(process.class_name);
        }
    }
    return classes;
}

const std::set<std::string, std::less<>> kSupportedCategories = {"background_extraction",
                                                                 "color",
                                                                 "denoising",
                                                                 "filters",
                                                                 "geometry",
                                                                 "mask_generation",
                                                                 "multi_image_stacking",
                                                                 "non_interactive",
                                                                 "sensor_calibration",
                                                                 "sharpening_enhancement",
                                                                 "star_object",
                                                                 "tone_brightness",
                                                                 "transforms"};

const std::set<std::string, std::less<>> kSupportedRuntimeTargets = {"linux-clang-x86_64", "windows-msvc-x86_64"};

[[nodiscard]] bool isSupportedCategory(const std::string& category)
{
    return !category.empty() && kSupportedCategories.contains(category);
}

[[nodiscard]] bool isSupportedRuntimeTarget(const std::string& runtime_target)
{
    return !runtime_target.empty() && kSupportedRuntimeTargets.contains(runtime_target);
}

[[nodiscard]] bool isHexSha256(const std::string& value)
{
    if (value.size() != 64)
    {
        return false;
    }

    return std::all_of(value.begin(), value.end(),
                       [](const char ch) { return std::isxdigit(static_cast<unsigned char>(ch)) != 0; });
}

[[nodiscard]] std::optional<std::string> normalizeSha256HexDigest(const std::string_view raw_value)
{
    std::string value(raw_value);
    if (value.rfind("sha256:", 0) == 0)
    {
        value.erase(0, std::string("sha256:").size());
    }
    if (!isHexSha256(value))
    {
        return std::nullopt;
    }

    std::transform(value.begin(), value.end(), value.begin(),
                   [](const unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return value;
}

[[nodiscard]] std::string sha256FileHex(const std::filesystem::path& path)
{
    const std::optional<std::array<std::uint8_t, 32>> digest = an::platform::sha256OfFile(path);
    return digest.has_value() ? an::platform::sha256Hex(*digest) : std::string();
}

[[nodiscard]] bool isSafeRelativePathString(const std::string& value)
{
    if (value.empty())
    {
        return false;
    }

    if (value.find("..") != std::string::npos)
    {
        return false;
    }

    if (value.find('\\') != std::string::npos || value.find(':') != std::string::npos)
    {
        return false;
    }

    const std::filesystem::path path(value);
    return !path.is_absolute();
}

[[nodiscard]] bool isSafeModuleName(const std::string& value)
{
    if (value.empty())
    {
        return false;
    }

    if (value.find("..") != std::string::npos || value.find('/') != std::string::npos ||
        value.find('\\') != std::string::npos)
    {
        return false;
    }

    return std::all_of(value.begin(), value.end(), [](const char ch) {
        return std::isalnum(static_cast<unsigned char>(ch)) || ch == '_' || ch == '.';
    });
}

[[nodiscard]] bool isSafeSharedGroupName(const std::string& value)
{
    if (value.empty())
    {
        return false;
    }

    return std::all_of(value.begin(), value.end(), [](const char ch) {
        return std::isalnum(static_cast<unsigned char>(ch)) || ch == '_' || ch == '-';
    });
}

[[nodiscard]] bool isSafeSharedHostProfileName(const std::string& value)
{
    return isSafeSharedGroupName(value);
}

[[nodiscard]] bool isKnownSharedHostProfileName(const std::string& value)
{
    return value == "scientific_core";
}

[[nodiscard]] bool isSafeBundledRuntimePackagePrefix(const std::string& value)
{
    if (value.empty())
    {
        return false;
    }

    return std::all_of(value.begin(), value.end(), [](const char ch) {
        return std::isalnum(static_cast<unsigned char>(ch)) || ch == '_' || ch == '-' || ch == '.';
    });
}

[[nodiscard]] std::optional<an::ui::extensions::ExtensionDependencyContext>
dependencyContextFromString(const std::string_view value)
{
    using an::ui::extensions::ExtensionDependencyContext;

    if (value == "private")
    {
        return ExtensionDependencyContext::Private;
    }
    if (value == "shared_group")
    {
        return ExtensionDependencyContext::SharedGroup;
    }
    if (value == "shared_host")
    {
        return ExtensionDependencyContext::SharedHost;
    }

    return std::nullopt;
}

[[nodiscard]] std::string sourceId(const ExtensionPackageSource source)
{
    switch (source)
    {
    case ExtensionPackageSource::Bundled:
        return "bundled";
    case ExtensionPackageSource::Repository:
        return "repository";
    case ExtensionPackageSource::LocalPackage:
        return "local_package";
    case ExtensionPackageSource::User:
        return "user";
    }

    return "user";
}

[[nodiscard]] std::optional<ExtensionPackageSource> sourceFromId(const std::string_view value)
{
    if (value == "bundled")
    {
        return ExtensionPackageSource::Bundled;
    }
    if (value == "repository")
    {
        return ExtensionPackageSource::Repository;
    }
    if (value == "local_package")
    {
        return ExtensionPackageSource::LocalPackage;
    }
    if (value == "user")
    {
        return ExtensionPackageSource::User;
    }

    return std::nullopt;
}

[[nodiscard]] std::string signatureStateId(const ExtensionPackageSignatureState state)
{
    switch (state)
    {
    case ExtensionPackageSignatureState::Unknown:
        return "unknown";
    case ExtensionPackageSignatureState::Unsigned:
        return "unsigned";
    case ExtensionPackageSignatureState::Verified:
        return "verified";
    case ExtensionPackageSignatureState::Failed:
        return "failed";
    }

    return "unknown";
}

[[nodiscard]] std::optional<ExtensionPackageSignatureState> signatureStateFromId(const std::string_view value)
{
    if (value == "unknown")
    {
        return ExtensionPackageSignatureState::Unknown;
    }
    if (value == "unsigned")
    {
        return ExtensionPackageSignatureState::Unsigned;
    }
    if (value == "verified")
    {
        return ExtensionPackageSignatureState::Verified;
    }
    if (value == "failed")
    {
        return ExtensionPackageSignatureState::Failed;
    }

    return std::nullopt;
}

[[nodiscard]] bool containsLineBreak(const std::string_view value)
{
    return value.find('\n') != std::string_view::npos || value.find('\r') != std::string_view::npos;
}

[[nodiscard]] bool isStrictBase64(const std::string_view value)
{
    if (value.empty() || value.size() % 4 != 0)
    {
        return false;
    }

    bool seen_padding = false;
    std::size_t padding_count = 0;
    for (const char ch : value)
    {
        const bool is_alnum = std::isalnum(static_cast<unsigned char>(ch)) != 0;
        if (is_alnum || ch == '+' || ch == '/')
        {
            if (seen_padding)
            {
                return false;
            }
            continue;
        }
        if (ch == '=')
        {
            seen_padding = true;
            ++padding_count;
            if (padding_count > 2)
            {
                return false;
            }
            continue;
        }
        return false;
    }

    return true;
}

[[nodiscard]] std::optional<QByteArray> decodeBase64Exact(const std::string_view value, const qsizetype expected_size,
                                                          const std::string_view label, std::string* error)
{
    if (containsLineBreak(value) || !isStrictBase64(value))
    {
        if (error)
        {
            *error = std::string(label) + " must be base64";
        }
        return std::nullopt;
    }

    const QByteArray encoded(value.data(), static_cast<QByteArray::size_type>(value.size()));
    const QByteArray decoded = QByteArray::fromBase64(encoded, QByteArray::Base64Encoding);
    if (decoded.size() != expected_size)
    {
        if (error)
        {
            *error = std::string(label) + " must decode to " + std::to_string(expected_size) + " bytes";
        }
        return std::nullopt;
    }

    return decoded;
}

/// The public key a trusted key id names among `trusted_keys`, or nothing when none of them is that id or its key is
/// not an Ed25519 public key.
[[nodiscard]] std::optional<QByteArray> trustedSigningPublicKey(const std::string_view key_id,
                                                                const std::vector<ExtensionSigningKey>& trusted_keys)
{
    for (const ExtensionSigningKey& key : trusted_keys)
    {
        if (!key.key_id.empty() && key.key_id == key_id)
        {
            std::string error;
            return decodeBase64Exact(key.public_key_base64, crypto_sign_PUBLICKEYBYTES, "extension signing public key",
                                     &error);
        }
    }
    return std::nullopt;
}

[[nodiscard]] bool appendSignaturePayloadField(std::ostringstream& out, const std::string_view name,
                                               const std::string_view value, std::string* diagnostic)
{
    if (value.empty() || containsLineBreak(value))
    {
        if (diagnostic)
        {
            *diagnostic = "Signed package metadata field '" + std::string(name) + "' must be non-empty and single-line";
        }
        return false;
    }

    out << name << '=' << value << '\n';
    return true;
}

[[nodiscard]] std::optional<std::string>
canonicalRepositoryAssetSignaturePayload(const ExtensionPackageInstallRequest& request,
                                         const std::string_view archive_hash_sha256, std::string* diagnostic)
{
    if (!isHexSha256(std::string(archive_hash_sha256)))
    {
        if (diagnostic)
        {
            *diagnostic = "Signed package payload requires a SHA-256 archive hash";
        }
        return std::nullopt;
    }

    std::vector<std::string> runtime_targets = request.selected_asset_runtime_targets;
    std::sort(runtime_targets.begin(), runtime_targets.end());
    runtime_targets.erase(std::unique(runtime_targets.begin(), runtime_targets.end()), runtime_targets.end());
    if (runtime_targets.empty())
    {
        if (diagnostic)
        {
            *diagnostic = "Verified repository package requires selected asset runtime targets";
        }
        return std::nullopt;
    }
    for (const auto& runtime_target : runtime_targets)
    {
        if (!isSupportedRuntimeTarget(runtime_target) || containsLineBreak(runtime_target))
        {
            if (diagnostic)
            {
                *diagnostic = "Verified repository package contains an unsupported runtime target";
            }
            return std::nullopt;
        }
    }

    std::ostringstream runtime_target_stream;
    for (std::size_t i = 0; i < runtime_targets.size(); ++i)
    {
        if (i > 0)
        {
            runtime_target_stream << ',';
        }
        runtime_target_stream << runtime_targets[i];
    }

    std::ostringstream payload;
    payload << kSignaturePayloadVersion << '\n';
    if (!appendSignaturePayloadField(payload, "package_id", request.expected_extension_id, diagnostic) ||
        !appendSignaturePayloadField(payload, "version", request.expected_extension_version, diagnostic) ||
        !appendSignaturePayloadField(payload, "asset_name", request.selected_release_asset, diagnostic) ||
        !appendSignaturePayloadField(payload, "package_hash", "sha256:" + std::string(archive_hash_sha256),
                                     diagnostic) ||
        !appendSignaturePayloadField(payload, "runtime_targets", runtime_target_stream.str(), diagnostic) ||
        !appendSignaturePayloadField(payload, "signature_algorithm", request.signature_algorithm, diagnostic))
    {
        return std::nullopt;
    }
    if (!appendSignaturePayloadField(payload, "signature_key_id", request.signature_key_id, diagnostic))
    {
        return std::nullopt;
    }

    std::string result = payload.str();
    if (!result.empty() && result.back() == '\n')
    {
        result.pop_back();
    }
    return result;
}

[[nodiscard]] std::optional<std::string>
verifiedSignatureMetadataDiagnostic(const ExtensionPackageInstallRequest& request,
                                    const std::vector<ExtensionSigningKey>& trusted_keys)
{
    if (request.signature_algorithm != kSignatureAlgorithmEd25519)
    {
        return std::string("Verified repository package signature_algorithm must be ed25519");
    }
    if (request.signature_key_id.empty())
    {
        return std::string("Verified repository package requires a signature_key_id");
    }
    if (!trustedSigningPublicKey(request.signature_key_id, trusted_keys).has_value())
    {
        return std::string("Verified repository package signature key is not trusted by this build");
    }
    if (request.signature.empty())
    {
        return std::string("Verified repository package requires a detached signature");
    }
    std::string signature_error;
    if (!decodeBase64Exact(request.signature, crypto_sign_BYTES, "signature", &signature_error).has_value())
    {
        return signature_error;
    }
    if (request.expected_extension_id.empty() || request.expected_extension_version.empty())
    {
        return std::string("Verified repository package requires expected extension id and version");
    }
    if (request.selected_release_asset.empty())
    {
        return std::string("Verified repository package requires the selected release asset name");
    }
    if (request.selected_asset_runtime_targets.empty())
    {
        return std::string("Verified repository package requires selected asset runtime targets");
    }

    return std::nullopt;
}

[[nodiscard]] bool verifyRepositoryAssetSignature(const ExtensionPackageInstallRequest& request,
                                                  const std::string& archive_hash_sha256,
                                                  const std::vector<ExtensionSigningKey>& trusted_keys,
                                                  std::string* diagnostic)
{
    if (const auto metadata_diagnostic = verifiedSignatureMetadataDiagnostic(request, trusted_keys);
        metadata_diagnostic.has_value())
    {
        if (diagnostic)
        {
            *diagnostic = *metadata_diagnostic;
        }
        return false;
    }

    const auto public_key = trustedSigningPublicKey(request.signature_key_id, trusted_keys);
    std::string signature_error;
    const auto signature = decodeBase64Exact(request.signature, crypto_sign_BYTES, "signature", &signature_error);
    if (!public_key.has_value() || !signature.has_value())
    {
        if (diagnostic)
        {
            *diagnostic = signature_error.empty()
                              ? "Verified repository package signature key is not trusted by this build"
                              : signature_error;
        }
        return false;
    }

    const auto payload = canonicalRepositoryAssetSignaturePayload(request, archive_hash_sha256, diagnostic);
    if (!payload.has_value())
    {
        return false;
    }

    if (sodium_init() < 0)
    {
        if (diagnostic)
        {
            *diagnostic = "Signature verification backend failed to initialize";
        }
        return false;
    }

    const auto* signature_bytes = reinterpret_cast<const unsigned char*>(signature->constData());
    const auto* payload_bytes = reinterpret_cast<const unsigned char*>(payload->data());
    const auto* public_key_bytes = reinterpret_cast<const unsigned char*>(public_key->constData());
    if (crypto_sign_verify_detached(signature_bytes, payload_bytes, static_cast<unsigned long long>(payload->size()),
                                    public_key_bytes) != 0)
    {
        if (diagnostic)
        {
            *diagnostic = "Package signature does not match the signed repository asset metadata";
        }
        return false;
    }

    return true;
}

[[nodiscard]] std::optional<std::string>
installTrustPolicyDiagnostic(const an::ui::extensions::ExtensionPackageInstallRequest& request,
                             const std::vector<ExtensionSigningKey>& trusted_keys)
{
    if (request.source == ExtensionPackageSource::Repository && request.expected_archive_hash_sha256.empty())
    {
        return std::string("Repository package installs require an authoritative SHA-256 archive hash");
    }

    switch (request.signature_state)
    {
    case ExtensionPackageSignatureState::Unknown:
        if (request.source == ExtensionPackageSource::Repository)
        {
            return std::string("Repository package signature state is unknown");
        }
        return std::nullopt;
    case ExtensionPackageSignatureState::Unsigned:
        return std::nullopt;
    case ExtensionPackageSignatureState::Verified:
        if (request.source == ExtensionPackageSource::Repository)
        {
            return verifiedSignatureMetadataDiagnostic(request, trusted_keys);
        }
        return std::nullopt;
    case ExtensionPackageSignatureState::Failed:
        return std::string("Package signature verification failed");
    }

    return std::string("Repository package signature state is unknown");
}

[[nodiscard]] bool hasConfiguredPipSettings(const an::ui::extensions::ExtensionPipSpec& pip)
{
    return !pip.find_links.empty() || !pip.index_urls.empty() || !pip.extra_index_urls.empty() || pip.require_hashes ||
           pip.no_deps;
}

[[nodiscard]] std::string sanitizeIdentifier(std::string_view value)
{
    std::string sanitized;
    sanitized.reserve(value.size());

    bool previous_was_separator = false;
    for (const char ch : value)
    {
        const unsigned char uch = static_cast<unsigned char>(ch);
        if (std::isalnum(uch))
        {
            sanitized.push_back(static_cast<char>(std::tolower(uch)));
            previous_was_separator = false;
            continue;
        }

        if (!previous_was_separator)
        {
            sanitized.push_back('_');
            previous_was_separator = true;
        }
    }

    while (!sanitized.empty() && sanitized.back() == '_')
    {
        sanitized.pop_back();
    }

    if (sanitized.empty())
    {
        return "extension";
    }

    return sanitized;
}

[[nodiscard]] std::vector<std::string> splitString(const std::string& value, const char separator)
{
    std::vector<std::string> parts;
    std::stringstream stream(value);
    std::string item;
    while (std::getline(stream, item, separator))
    {
        if (!item.empty())
        {
            parts.push_back(item);
        }
    }
    return parts;
}

[[nodiscard]] std::string trimString(std::string value)
{
    auto is_space = [](const unsigned char ch) { return std::isspace(ch) != 0; };

    value.erase(value.begin(), std::find_if(value.begin(), value.end(),
                                            [&](const char ch) { return !is_space(static_cast<unsigned char>(ch)); }));

    value.erase(std::find_if(value.rbegin(), value.rend(),
                             [&](const char ch) { return !is_space(static_cast<unsigned char>(ch)); })
                    .base(),
                value.end());

    return value;
}

[[nodiscard]] std::string toLowerCopy(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(),
                   [](const unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return value;
}

[[nodiscard]] std::string normalizedLicenseText(std::string value)
{
    value = toLowerCopy(trimString(std::move(value)));
    std::replace(value.begin(), value.end(), '_', '-');
    return value;
}

[[nodiscard]] bool isGplFamilyLicense(const std::string& license)
{
    const std::string normalized = normalizedLicenseText(license);
    if (normalized.empty())
    {
        return false;
    }

    return normalized.find("gpl") != std::string::npos && normalized.find("lgpl") == std::string::npos;
}

[[nodiscard]] bool isDeclaredNonGplLicense(const std::string& license)
{
    return !trimString(license).empty() && !isGplFamilyLicense(license);
}

[[nodiscard]] bool isNativeAfterNightControlUiValue(const std::string& value)
{
    const std::string ui = normalizedLicenseText(value);

    return ui == "native" || ui == "native-control" || ui == "native-controls" || ui == "native-process-window" ||
           ui == "process-window" || ui == "paramdef" || ui == "param-def" || ui == "after-night-controls" ||
           ui == "afternight-controls";
}

[[nodiscard]] bool requiresNativeAfterNightControls(const ExtensionCapabilities& capabilities)
{
    return isNativeAfterNightControlUiValue(capabilities.ui);
}

[[nodiscard]] bool isSharedLibraryArtifact(const std::filesystem::path& path)
{
    const std::string extension = toLowerCopy(path.extension().string());
    return extension == ".so" || extension == ".dll" || extension == ".dylib" || extension == ".pyd";
}

[[nodiscard]] bool isPathWithinRoot(const std::filesystem::path& path, const std::filesystem::path& root)
{
    std::error_code ec;
    const std::filesystem::path relative = std::filesystem::relative(path, root, ec);
    if (ec || relative.empty())
    {
        return false;
    }

    const std::string relative_text = relative.generic_string();
    return relative_text == "." || (relative_text != ".." && relative_text.rfind("../", 0) != 0);
}

[[nodiscard]] std::vector<int> parseNumericVersion(const std::string& value, bool* ok = nullptr)
{
    std::vector<int> components;
    const auto parts = splitString(value, '.');
    if (parts.empty())
    {
        if (ok)
        {
            *ok = false;
        }
        return components;
    }

    for (const std::string& part : parts)
    {
        if (part.empty() || !std::all_of(part.begin(), part.end(), [](const char ch) {
                return std::isdigit(static_cast<unsigned char>(ch)) != 0;
            }))
        {
            if (ok)
            {
                *ok = false;
            }
            return {};
        }

        components.push_back(std::stoi(part));
    }

    if (ok)
    {
        *ok = true;
    }
    return components;
}

[[nodiscard]] bool parseWildcardVersionPrefix(const std::string& value, std::vector<int>* out_prefix,
                                              bool* ok = nullptr)
{
    if (value.size() < 3 || value.substr(value.size() - 2) != ".*")
    {
        if (ok)
        {
            *ok = false;
        }
        return false;
    }

    const std::string prefix_text = value.substr(0, value.size() - 2);
    bool prefix_ok = false;
    const auto prefix_components = parseNumericVersion(prefix_text, &prefix_ok);
    if (!prefix_ok)
    {
        if (ok)
        {
            *ok = false;
        }
        return false;
    }

    if (out_prefix)
    {
        *out_prefix = prefix_components;
    }
    if (ok)
    {
        *ok = true;
    }
    return true;
}

[[nodiscard]] int compareVersionVectors(std::vector<int> lhs, std::vector<int> rhs)
{
    const std::size_t size = (std::max)(lhs.size(), rhs.size());
    lhs.resize(size, 0);
    rhs.resize(size, 0);

    for (std::size_t i = 0; i < size; ++i)
    {
        if (lhs[i] < rhs[i])
        {
            return -1;
        }
        if (lhs[i] > rhs[i])
        {
            return 1;
        }
    }

    return 0;
}

[[nodiscard]] bool versionMatchesPrefix(const std::vector<int>& version, const std::vector<int>& prefix)
{
    if (version.size() < prefix.size())
    {
        return false;
    }

    return std::equal(prefix.begin(), prefix.end(), version.begin());
}

[[nodiscard]] int compareVersionStrings(const std::string& lhs, const std::string& rhs)
{
    bool lhs_ok = false;
    bool rhs_ok = false;
    const auto lhs_components = parseNumericVersion(lhs, &lhs_ok);
    const auto rhs_components = parseNumericVersion(rhs, &rhs_ok);
    if (lhs_ok && rhs_ok)
    {
        return compareVersionVectors(lhs_components, rhs_components);
    }

    if (lhs == rhs)
    {
        return 0;
    }
    return lhs < rhs ? -1 : 1;
}

// The product's version is the application's to state; the application states it before any extension is discovered.
// A process that states none - a test, a tool - has no version to hold an extension's minimum against.
[[nodiscard]] std::string currentAfterNightVersion()
{
    return QCoreApplication::applicationVersion().trimmed().toStdString();
}

[[nodiscard]] ExtensionDiscoveryContext normalizeDiscoveryContext(ExtensionDiscoveryContext context)
{
    if (context.runtime_target.empty())
    {
        context.runtime_target = an::ui::extensions::current_runtime_target();
    }
    if (context.app_version.empty())
    {
        context.app_version = currentAfterNightVersion();
    }
    return context;
}

[[nodiscard]] bool requiresNewerAfterNightVersion(const std::string& minimum_app_version,
                                                  const std::string& current_app_version)
{
    // An unknown version is held to no minimum: nothing says it is older.
    return !minimum_app_version.empty() && !current_app_version.empty() &&
           compareVersionStrings(current_app_version, minimum_app_version) < 0;
}

[[nodiscard]] std::string minimumAfterNightVersionDiagnostic(const std::string& minimum_app_version,
                                                             const std::string& current_app_version)
{
    return "Requires AfterNight " + minimum_app_version + " or newer. Current AfterNight version is " +
           (current_app_version.empty() ? std::string("unknown") : current_app_version) + ".";
}

// Evaluates a PEP 440-style python_compat version specifier against a concrete
// Python version string (e.g. "3.14.0"). Supports relational operators
// (>=, >, <=, <, ==, !=) with numeric version components. Comma-separated
// clauses are ANDed together. Trailing wildcard prefix clauses such as
// "==3.14.*" and "!=3.14.*" are also supported.
[[nodiscard]] bool evaluatePythonCompat(const std::string& specifier, const std::string& python_version,
                                        std::string* error)
{
    const auto clauses = splitString(specifier, ',');
    if (clauses.empty())
    {
        if (error)
        {
            *error = "python_compat is empty";
        }
        return false;
    }

    bool python_version_ok = false;
    const auto python_components = parseNumericVersion(python_version, &python_version_ok);
    if (!python_version_ok)
    {
        if (error)
        {
            *error = "Bundled Python version is not parseable";
        }
        return false;
    }

    for (std::string clause : clauses)
    {
        clause = trimString(std::move(clause));
        if (clause.empty())
        {
            if (error)
            {
                *error = "python_compat contains an empty clause";
            }
            return false;
        }

        std::string op;
        if (clause.rfind(">=", 0) == 0 || clause.rfind("<=", 0) == 0 || clause.rfind("==", 0) == 0 ||
            clause.rfind("!=", 0) == 0)
        {
            op = clause.substr(0, 2);
            clause.erase(0, 2);
        }
        else if (clause.rfind(">", 0) == 0 || clause.rfind("<", 0) == 0)
        {
            op = clause.substr(0, 1);
            clause.erase(0, 1);
        }
        else
        {
            if (error)
            {
                *error = "python_compat clause is missing a supported operator";
            }
            return false;
        }

        const std::string target_version = trimString(clause);
        std::vector<int> wildcard_prefix;
        bool wildcard_ok = false;
        const bool has_wildcard = target_version.find('*') != std::string::npos;
        if (has_wildcard)
        {
            if (op != "==" && op != "!=")
            {
                if (error)
                {
                    *error = "python_compat wildcard clauses only support == or !=";
                }
                return false;
            }

            if (!parseWildcardVersionPrefix(target_version, &wildcard_prefix, &wildcard_ok) || !wildcard_ok)
            {
                if (error)
                {
                    *error = "python_compat clause contains an invalid wildcard version";
                }
                return false;
            }
        }

        if (has_wildcard)
        {
            const bool prefix_match = versionMatchesPrefix(python_components, wildcard_prefix);
            const bool match = (op == "==" && prefix_match) || (op == "!=" && !prefix_match);

            if (!match)
            {
                return false;
            }
            continue;
        }

        bool target_ok = false;
        const auto target_components = parseNumericVersion(target_version, &target_ok);
        if (!target_ok)
        {
            if (error)
            {
                *error = "python_compat clause contains an invalid version";
            }
            return false;
        }

        const int comparison = compareVersionVectors(python_components, target_components);
        const bool match = (op == ">=" && comparison >= 0) || (op == ">" && comparison > 0) ||
                           (op == "<=" && comparison <= 0) || (op == "<" && comparison < 0) ||
                           (op == "==" && comparison == 0) || (op == "!=" && comparison != 0);

        if (!match)
        {
            return false;
        }
    }

    return true;
}

bool requireString(const QJsonObject& object, const QString& key, std::string& out_value, std::string* error)
{
    const QJsonValue value = object.value(key);
    if (!value.isString())
    {
        if (error)
        {
            *error = "Missing or invalid string field: " + key.toStdString();
        }
        return false;
    }

    out_value = value.toString().toStdString();
    return true;
}

bool parseOptionalStringField(const QJsonObject& object, const QString& key, std::string& out_value, std::string* error)
{
    const QJsonValue value = object.value(key);
    if (value.isUndefined())
    {
        return true;
    }
    if (!value.isString())
    {
        if (error)
        {
            *error = key.toStdString() + " must be a string when present";
        }
        return false;
    }

    out_value = value.toString().toStdString();
    return true;
}

bool requireInt(const QJsonObject& object, const QString& key, int& out_value, std::string* error)
{
    const QJsonValue value = object.value(key);
    if (!value.isDouble())
    {
        if (error)
        {
            *error = "Missing or invalid integer field: " + key.toStdString();
        }
        return false;
    }

    out_value = value.toInt();
    return true;
}

bool parseOptionalBoolField(const QJsonObject& object, const QString& key, bool* out_value, std::string* error)
{
    const QJsonValue value = object.value(key);
    if (value.isUndefined())
    {
        return true;
    }
    if (!value.isBool())
    {
        if (error)
        {
            *error = key.toStdString() + " must be a boolean when present";
        }
        return false;
    }

    if (out_value)
    {
        *out_value = value.toBool();
    }
    return true;
}

bool parsePositiveIntArray(const QJsonObject& object, const QString& key, std::vector<int>& out_values,
                           std::string* error)
{
    const QJsonValue value = object.value(key);
    if (value.isUndefined())
    {
        return true;
    }

    if (!value.isArray())
    {
        if (error)
        {
            *error = key.toStdString() + " must be an array when present";
        }
        return false;
    }

    for (const QJsonValue& item : value.toArray())
    {
        const int channel_count = item.toInt(-1);
        if (!item.isDouble() || channel_count <= 0 || item.toDouble() != static_cast<double>(channel_count))
        {
            if (error)
            {
                *error = key.toStdString() + " must contain positive integer values only";
            }
            return false;
        }

        if (std::find(out_values.begin(), out_values.end(), channel_count) != out_values.end())
        {
            if (error)
            {
                *error = key.toStdString() + " contains duplicate values";
            }
            return false;
        }

        out_values.push_back(channel_count);
    }

    return true;
}

bool parseCapabilitiesObject(const QJsonObject& object, ExtensionCapabilities* out_capabilities, std::string* error)
{
    ExtensionCapabilities capabilities;

    if (!parseOptionalBoolField(object, QStringLiteral("execute"), &capabilities.execute, error) ||
        !parseOptionalBoolField(object, QStringLiteral("preview"), &capabilities.preview, error) ||
        !parseOptionalBoolField(object, QStringLiteral("keep_open"), &capabilities.keep_open, error) ||
        !parseOptionalBoolField(object, QStringLiteral("stateful_sequence"), &capabilities.stateful_sequence, error) ||
        !parseOptionalBoolField(object, QStringLiteral("starless_generator"), &capabilities.starless_generator,
                                error) ||
        !parseOptionalBoolField(object, QStringLiteral("star_mask_generator"), &capabilities.star_mask_generator,
                                error) ||
        !parsePositiveIntArray(object, QStringLiteral("source_channel_filter"), capabilities.source_channel_filter,
                               error))
    {
        return false;
    }

    // What a frame of a drag may run the preview over: the whole image unless the extension states otherwise.
    const QJsonValue preview_scaling = object.value(QStringLiteral("preview_scaling"));
    if (!preview_scaling.isUndefined())
    {
        const std::optional<ExtensionPreviewScaling> scaling =
            preview_scaling.isString() ? extensionPreviewScalingFromName(preview_scaling.toString().toStdString())
                                       : std::nullopt;
        if (!scaling.has_value())
        {
            if (error)
            {
                *error = "capabilities.preview_scaling must be one of full_only, decimated and native_region";
            }
            return false;
        }
        capabilities.preview_scaling = *scaling;
    }
    const QJsonValue preview_region_margin = object.value(QStringLiteral("preview_region_margin"));
    if (!preview_region_margin.isUndefined())
    {
        const double margin = preview_region_margin.toDouble(-1.0);
        if (!preview_region_margin.isDouble() || margin < 0.0 ||
            margin > static_cast<double>(kMaxExtensionPreviewRegionMargin) || margin != std::floor(margin))
        {
            if (error)
            {
                *error = "capabilities.preview_region_margin must be a whole number of pixels from 0 to " +
                         std::to_string(kMaxExtensionPreviewRegionMargin);
            }
            return false;
        }
        capabilities.preview_region_margin = static_cast<std::uint32_t>(margin);
    }

    const QJsonValue ui = object.value(QStringLiteral("ui"));
    if (ui.isString())
    {
        const std::string ui_value = ui.toString().toStdString();
        if (isNativeAfterNightControlUiValue(ui_value))
        {
            capabilities.ui = ui_value;
        }
        if (an::platform::isLoggingInitialized())
        {
            LOG_WARNING("Deprecated capabilities.ui value '{}' is ignored during extension manifest parsing", ui_value);
        }
    }
    else if (!ui.isUndefined())
    {
        if (error)
        {
            *error = "capabilities.ui must be a string when present";
        }
        return false;
    }

    if (capabilities.stateful_sequence)
    {
        if (error)
        {
            *error = "capabilities.stateful_sequence must be false in v1";
        }
        return false;
    }

    const QJsonValue masks_value = object.value(QStringLiteral("masks"));
    if (!masks_value.isUndefined())
    {
        if (!masks_value.isObject())
        {
            if (error)
            {
                *error = "capabilities.masks must be an object when present";
            }
            return false;
        }

        const QJsonObject masks = masks_value.toObject();
        if (!parseOptionalBoolField(masks, QStringLiteral("input"), &capabilities.masks_input, error) ||
            !parseOptionalBoolField(masks, QStringLiteral("output"), &capabilities.masks_output, error))
        {
            if (error && !error->empty())
            {
                *error = "capabilities.masks." + *error;
            }
            return false;
        }
    }

    const QJsonValue weights_value = object.value(QStringLiteral("weights"));
    if (!weights_value.isUndefined())
    {
        if (!weights_value.isObject())
        {
            if (error)
            {
                *error = "capabilities.weights must be an object when present";
            }
            return false;
        }

        const QJsonObject weights = weights_value.toObject();
        if (!parseOptionalBoolField(weights, QStringLiteral("input"), &capabilities.weights_input, error))
        {
            if (error && !error->empty())
            {
                *error = "capabilities.weights." + *error;
            }
            return false;
        }
    }

    if (out_capabilities)
    {
        *out_capabilities = std::move(capabilities);
    }
    return true;
}

bool parseCapabilities(const QJsonValue& value, ExtensionCapabilities* out_capabilities, std::string* error)
{
    if (value.isUndefined())
    {
        if (out_capabilities)
        {
            *out_capabilities = ExtensionCapabilities{};
        }
        return true;
    }
    if (!value.isObject())
    {
        if (error)
        {
            *error = "capabilities must be an object when present";
        }
        return false;
    }

    return parseCapabilitiesObject(value.toObject(), out_capabilities, error);
}

bool parseRuntimeTargets(const QJsonObject& object, std::vector<std::string>& runtime_targets, std::string* error)
{
    const QJsonValue value = object.value(QStringLiteral("runtime_targets"));
    if (value.isUndefined())
    {
        return true;
    }

    if (!value.isArray())
    {
        if (error)
        {
            *error = "runtime_targets must be an array when present";
        }
        return false;
    }

    const QJsonArray array = value.toArray();
    runtime_targets.reserve(array.size());
    for (const QJsonValue& item : array)
    {
        if (!item.isString())
        {
            if (error)
            {
                *error = "runtime_targets must contain strings only";
            }
            return false;
        }

        const std::string runtime_target = item.toString().toStdString();
        if (!isSupportedRuntimeTarget(runtime_target))
        {
            if (error)
            {
                *error = "runtime_targets contains an unsupported deployment target";
            }
            return false;
        }

        if (std::find(runtime_targets.begin(), runtime_targets.end(), runtime_target) != runtime_targets.end())
        {
            if (error)
            {
                *error = "runtime_targets contains duplicate deployment targets";
            }
            return false;
        }

        runtime_targets.push_back(runtime_target);
    }

    return true;
}

bool parseStringArray(const QJsonObject& object, const QString& key, std::vector<std::string>& out_values,
                      std::string* error)
{
    const QJsonValue value = object.value(key);
    if (value.isUndefined())
    {
        return true;
    }

    if (!value.isArray())
    {
        if (error)
        {
            *error = key.toStdString() + " must be an array when present";
        }
        return false;
    }

    for (const QJsonValue& item : value.toArray())
    {
        if (!item.isString())
        {
            if (error)
            {
                *error = key.toStdString() + " must contain strings only";
            }
            return false;
        }

        out_values.push_back(item.toString().toStdString());
    }

    return true;
}

bool rejectManifestBufferRoleField(const QJsonObject& object, std::string_view field_path, std::string* error)
{
    if (!object.contains(QStringLiteral("buffer_role")))
    {
        return true;
    }

    if (error)
    {
        *error = std::string(field_path) +
                 "buffer_role is controlled by the host and must not be declared in extension manifests";
    }
    return false;
}

bool parseBinaryVendorPaths(const QJsonObject& object, std::vector<std::string>& binary_vendor_paths,
                            std::string* error)
{
    if (!parseStringArray(object, QStringLiteral("binary_vendor_paths"), binary_vendor_paths, error))
    {
        return false;
    }

    for (const std::string& item : binary_vendor_paths)
    {
        if (!isSafeRelativePathString(item))
        {
            if (error)
            {
                *error = "binary_vendor_paths contains an invalid package-relative path";
            }
            return false;
        }
    }

    return true;
}

bool validateUrlArray(const std::vector<std::string>& values, std::string_view field_name, std::string* error)
{
    for (const std::string& value : values)
    {
        if (value.empty() || !isUrlLike(value))
        {
            if (error)
            {
                *error = std::string(field_name) + " must contain explicit URLs only";
            }
            return false;
        }
    }

    return true;
}

bool validateOptionalUrl(const std::string& value, std::string_view field_name, std::string* error)
{
    if (value.empty())
    {
        return true;
    }
    if (isUrlLike(value))
    {
        return true;
    }

    if (error)
    {
        *error = std::string(field_name) + " must be an explicit http(s) URL";
    }
    return false;
}

bool parseMarketplaceMetadata(const QJsonObject& root, ExtensionRepositoryMetadata& metadata, std::string* error)
{
    const QJsonValue value = root.value(QStringLiteral("marketplace"));
    if (value.isUndefined() || value.isNull())
    {
        return true;
    }
    if (!value.isObject())
    {
        if (error)
        {
            *error = "marketplace must be an object when present";
        }
        return false;
    }

    const QJsonObject marketplace = value.toObject();
    if (!parseOptionalStringField(marketplace, QStringLiteral("publisher_id"), metadata.publisher_id, error) ||
        !parseOptionalStringField(marketplace, QStringLiteral("attribution"), metadata.attribution, error) ||
        !parseOptionalStringField(marketplace, QStringLiteral("original_author"), metadata.original_author, error) ||
        !parseOptionalStringField(marketplace, QStringLiteral("original_project"), metadata.original_project, error) ||
        !parseOptionalStringField(marketplace, QStringLiteral("original_source_url"), metadata.original_source_url,
                                  error) ||
        !parseOptionalStringField(marketplace, QStringLiteral("upstream_commit"), metadata.upstream_commit, error) ||
        !parseOptionalStringField(marketplace, QStringLiteral("homepage"), metadata.homepage_url, error) ||
        !parseOptionalStringField(marketplace, QStringLiteral("homepage_url"), metadata.homepage_url, error) ||
        !parseOptionalStringField(marketplace, QStringLiteral("repository_url"), metadata.repository_url, error) ||
        !parseOptionalStringField(marketplace, QStringLiteral("support_url"), metadata.support_url, error) ||
        !parseOptionalStringField(marketplace, QStringLiteral("icon"), metadata.icon_path, error) ||
        !parseOptionalStringField(marketplace, QStringLiteral("package_hash"), metadata.package_hash_sha256, error) ||
        !parseOptionalStringField(marketplace, QStringLiteral("archive_hash"), metadata.archive_hash_sha256, error) ||
        !parseOptionalStringField(marketplace, QStringLiteral("signature_detail"), metadata.signature_detail, error) ||
        !parseStringArray(marketplace, QStringLiteral("screenshots"), metadata.screenshots, error) ||
        !parseStringArray(marketplace, QStringLiteral("tags"), metadata.tags, error))
    {
        return false;
    }

    if (marketplace.contains(QStringLiteral("repository")) &&
        !parseOptionalStringField(marketplace, QStringLiteral("repository"), metadata.repository_url, error))
    {
        return false;
    }

    std::string signature_state;
    if (!parseOptionalStringField(marketplace, QStringLiteral("signature_state"), signature_state, error))
    {
        return false;
    }
    if (!signature_state.empty())
    {
        const auto parsed = signatureStateFromId(signature_state);
        if (!parsed.has_value())
        {
            if (error)
            {
                *error = "marketplace.signature_state must be one of: unknown, unsigned, verified, failed";
            }
            return false;
        }
        metadata.signature_state = *parsed;
    }

    return true;
}

bool parseRepositoryMetadata(const QJsonObject& root, ExtensionRepositoryMetadata& metadata, std::string* error)
{
    if (!parseMarketplaceMetadata(root, metadata, error))
    {
        return false;
    }

    if (root.contains(QStringLiteral("screenshots")))
    {
        metadata.screenshots.clear();
    }
    if (root.contains(QStringLiteral("tags")))
    {
        metadata.tags.clear();
    }

    if (!parseOptionalStringField(root, QStringLiteral("summary"), metadata.summary, error) ||
        !parseOptionalStringField(root, QStringLiteral("description"), metadata.description, error) ||
        !parseOptionalStringField(root, QStringLiteral("author"), metadata.author, error) ||
        !parseOptionalStringField(root, QStringLiteral("license"), metadata.license, error) ||
        !parseOptionalStringField(root, QStringLiteral("publisher_id"), metadata.publisher_id, error) ||
        !parseOptionalStringField(root, QStringLiteral("attribution"), metadata.attribution, error) ||
        !parseOptionalStringField(root, QStringLiteral("original_author"), metadata.original_author, error) ||
        !parseOptionalStringField(root, QStringLiteral("original_project"), metadata.original_project, error) ||
        !parseOptionalStringField(root, QStringLiteral("original_source_url"), metadata.original_source_url, error) ||
        !parseOptionalStringField(root, QStringLiteral("upstream_commit"), metadata.upstream_commit, error) ||
        !parseOptionalStringField(root, QStringLiteral("homepage_url"), metadata.homepage_url, error) ||
        !parseOptionalStringField(root, QStringLiteral("support_url"), metadata.support_url, error) ||
        !parseOptionalStringField(root, QStringLiteral("icon"), metadata.icon_path, error) ||
        !parseOptionalStringField(root, QStringLiteral("package_hash"), metadata.package_hash_sha256, error) ||
        !parseOptionalStringField(root, QStringLiteral("archive_hash"), metadata.archive_hash_sha256, error) ||
        !parseOptionalStringField(root, QStringLiteral("signature_detail"), metadata.signature_detail, error) ||
        !parseStringArray(root, QStringLiteral("screenshots"), metadata.screenshots, error) ||
        !parseStringArray(root, QStringLiteral("tags"), metadata.tags, error))
    {
        return false;
    }

    if (!parseOptionalStringField(root, QStringLiteral("repository_url"), metadata.repository_url, error))
    {
        return false;
    }
    if (!root.contains(QStringLiteral("repository_url")) &&
        !parseOptionalStringField(root, QStringLiteral("repository"), metadata.repository_url, error))
    {
        return false;
    }

    if (!validateOptionalUrl(metadata.homepage_url, "homepage_url", error) ||
        !validateOptionalUrl(metadata.repository_url, "repository_url", error) ||
        !validateOptionalUrl(metadata.support_url, "support_url", error) ||
        !validateOptionalUrl(metadata.original_source_url, "original_source_url", error))
    {
        return false;
    }

    if (!metadata.icon_path.empty() && !isSafeRelativePathString(metadata.icon_path))
    {
        if (error)
        {
            *error = "icon must be a safe package-relative path";
        }
        return false;
    }

    for (const std::string& screenshot : metadata.screenshots)
    {
        if (!isSafeRelativePathString(screenshot))
        {
            if (error)
            {
                *error = "screenshots must contain safe package-relative paths only";
            }
            return false;
        }
    }

    for (const std::string& tag : metadata.tags)
    {
        if (tag.empty())
        {
            if (error)
            {
                *error = "tags must not contain empty values";
            }
            return false;
        }
    }

    if (!metadata.package_hash_sha256.empty() && !isHexSha256(metadata.package_hash_sha256))
    {
        if (error)
        {
            *error = "package_hash must be a SHA-256 hex digest";
        }
        return false;
    }
    if (!metadata.archive_hash_sha256.empty() && !isHexSha256(metadata.archive_hash_sha256))
    {
        if (error)
        {
            *error = "archive_hash must be a SHA-256 hex digest";
        }
        return false;
    }

    std::string signature_state;
    if (!parseOptionalStringField(root, QStringLiteral("signature_state"), signature_state, error))
    {
        return false;
    }
    if (!signature_state.empty())
    {
        const auto parsed = signatureStateFromId(signature_state);
        if (!parsed.has_value())
        {
            if (error)
            {
                *error = "signature_state must be one of: unknown, unsigned, verified, failed";
            }
            return false;
        }
        metadata.signature_state = *parsed;
    }

    return true;
}

bool parseDependencies(const QJsonObject& root, ExtensionDependencySpec& dependencies, std::string* error)
{
    const QJsonValue dependencies_value = root.value(QStringLiteral("dependencies"));
    if (dependencies_value.isUndefined())
    {
        return true;
    }

    if (!dependencies_value.isObject())
    {
        if (error)
        {
            *error = "dependencies must be an object when present";
        }
        return false;
    }

    const QJsonObject object = dependencies_value.toObject();
    bool dependency_context_explicit = false;
    const QJsonValue dependency_context = object.value(QStringLiteral("dependency_context"));
    if (dependency_context.isString())
    {
        const std::string dependency_context_string = dependency_context.toString().toStdString();
        const auto parsed = dependencyContextFromString(dependency_context_string);
        if (!parsed.has_value())
        {
            if (error)
            {
                *error = "dependencies.dependency_context must be one of: private, shared_group, shared_host";
            }
            return false;
        }

        dependencies.dependency_context = *parsed;
        dependency_context_explicit = true;
    }
    else if (!dependency_context.isUndefined())
    {
        if (error)
        {
            *error = "dependencies.dependency_context must be a string when present";
        }
        return false;
    }

    const QJsonValue requirements_file = object.value(QStringLiteral("requirements_file"));
    if (requirements_file.isString())
    {
        dependencies.requirements_file = requirements_file.toString().toStdString();
        if (!isSafeRelativePathString(dependencies.requirements_file))
        {
            if (error)
            {
                *error = "dependencies.requirements_file must be a safe package-relative path";
            }
            return false;
        }
    }
    else if (!requirements_file.isUndefined())
    {
        if (error)
        {
            *error = "dependencies.requirements_file must be a string when present";
        }
        return false;
    }

    const QJsonValue shared_group = object.value(QStringLiteral("shared_group"));
    if (shared_group.isString())
    {
        dependencies.shared_group = shared_group.toString().toStdString();
        if (!isSafeSharedGroupName(dependencies.shared_group))
        {
            if (error)
            {
                *error = "dependencies.shared_group must use a stable identifier";
            }
            return false;
        }
    }
    else if (!shared_group.isUndefined())
    {
        if (error)
        {
            *error = "dependencies.shared_group must be a string when present";
        }
        return false;
    }

    const QJsonValue shared_host_profile = object.value(QStringLiteral("shared_host_profile"));
    if (shared_host_profile.isString())
    {
        dependencies.shared_host_profile = shared_host_profile.toString().toStdString();
        if (!isSafeSharedHostProfileName(dependencies.shared_host_profile))
        {
            if (error)
            {
                *error = "dependencies.shared_host_profile must use a stable identifier";
            }
            return false;
        }
    }
    else if (!shared_host_profile.isUndefined())
    {
        if (error)
        {
            *error = "dependencies.shared_host_profile must be a string when present";
        }
        return false;
    }

    const QJsonValue pip_value = object.value(QStringLiteral("pip"));
    if (!pip_value.isUndefined() && !pip_value.isObject())
    {
        if (error)
        {
            *error = "dependencies.pip must be an object when present";
        }
        return false;
    }

    const QJsonObject pip = pip_value.toObject();
    if (!parseStringArray(pip, QStringLiteral("find_links"), dependencies.pip.find_links, error) ||
        !parseStringArray(pip, QStringLiteral("index_urls"), dependencies.pip.index_urls, error) ||
        !parseStringArray(pip, QStringLiteral("extra_index_urls"), dependencies.pip.extra_index_urls, error) ||
        !parseStringArray(object, QStringLiteral("bundled_runtime_packages"), dependencies.bundled_runtime_packages,
                          error))
    {
        return false;
    }

    if (!validateUrlArray(dependencies.pip.index_urls, "dependencies.pip.index_urls", error) ||
        !validateUrlArray(dependencies.pip.extra_index_urls, "dependencies.pip.extra_index_urls", error))
    {
        return false;
    }

    for (const std::string& item : dependencies.pip.find_links)
    {
        if (!isUrlLike(item) && !isSafeRelativePathString(item))
        {
            if (error)
            {
                *error = "dependencies.pip.find_links must contain explicit URLs or safe package-relative paths";
            }
            return false;
        }
    }

    for (const std::string& item : dependencies.bundled_runtime_packages)
    {
        if (!isSafeBundledRuntimePackagePrefix(item))
        {
            if (error)
            {
                *error = "dependencies.bundled_runtime_packages must contain stable package prefixes";
            }
            return false;
        }
    }

    const QJsonValue require_hashes = pip.value(QStringLiteral("require_hashes"));
    if (require_hashes.isBool())
    {
        dependencies.pip.require_hashes = require_hashes.toBool();
    }
    else if (!require_hashes.isUndefined())
    {
        if (error)
        {
            *error = "dependencies.pip.require_hashes must be a boolean when present";
        }
        return false;
    }

    const QJsonValue no_deps = pip.value(QStringLiteral("no_deps"));
    if (no_deps.isBool())
    {
        dependencies.pip.no_deps = no_deps.toBool();
    }
    else if (!no_deps.isUndefined())
    {
        if (error)
        {
            *error = "dependencies.pip.no_deps must be a boolean when present";
        }
        return false;
    }

    if (!dependency_context_explicit)
    {
        if (!dependencies.shared_group.empty() && !dependencies.shared_host_profile.empty())
        {
            if (error)
            {
                *error = "dependencies.shared_group and dependencies.shared_host_profile cannot both be present";
            }
            return false;
        }

        if (!dependencies.shared_group.empty())
        {
            dependencies.dependency_context = ExtensionDependencyContext::SharedGroup;
        }
        else if (!dependencies.shared_host_profile.empty())
        {
            dependencies.dependency_context = ExtensionDependencyContext::SharedHost;
        }
    }

    const bool has_pip_dependencies = !dependencies.requirements_file.empty() || !dependencies.pip.find_links.empty() ||
                                      !dependencies.pip.index_urls.empty() ||
                                      !dependencies.pip.extra_index_urls.empty() || dependencies.pip.require_hashes ||
                                      dependencies.pip.no_deps;
    const bool has_pip_settings = hasConfiguredPipSettings(dependencies.pip);

    switch (dependencies.dependency_context)
    {
    case ExtensionDependencyContext::Private:
        if (!dependencies.shared_group.empty())
        {
            if (error)
            {
                *error = "dependencies.shared_group requires dependencies.dependency_context = shared_group";
            }
            return false;
        }
        if (!dependencies.shared_host_profile.empty())
        {
            if (error)
            {
                *error = "dependencies.shared_host_profile requires dependencies.dependency_context = shared_host";
            }
            return false;
        }
        break;
    case ExtensionDependencyContext::SharedGroup:
        if (dependencies.shared_group.empty())
        {
            if (error)
            {
                *error = "dependencies.dependency_context = shared_group requires dependencies.shared_group";
            }
            return false;
        }
        if (!dependencies.shared_host_profile.empty())
        {
            if (error)
            {
                *error = "dependencies.dependency_context = shared_group cannot also declare "
                         "dependencies.shared_host_profile";
            }
            return false;
        }
        break;
    case ExtensionDependencyContext::SharedHost:
        if (dependencies.shared_host_profile.empty())
        {
            if (error)
            {
                *error = "dependencies.dependency_context = shared_host requires dependencies.shared_host_profile";
            }
            return false;
        }
        if (!dependencies.shared_group.empty())
        {
            if (error)
            {
                *error = "dependencies.dependency_context = shared_host cannot also declare dependencies.shared_group";
            }
            return false;
        }
        if (!isKnownSharedHostProfileName(dependencies.shared_host_profile))
        {
            if (error)
            {
                *error = "dependencies.shared_host_profile must reference a supported host-managed profile";
            }
            return false;
        }
        if (!dependencies.requirements_file.empty() || has_pip_settings ||
            !dependencies.bundled_runtime_packages.empty())
        {
            if (error)
            {
                *error = "dependencies.dependency_context = shared_host cannot declare requirements_file, pip options, "
                         "or bundled_runtime_packages";
            }
            return false;
        }
        break;
    }

    if (!dependencies.requirements_file.empty() && !dependencies.pip.require_hashes)
    {
        if (error)
        {
            *error = "dependencies.pip.require_hashes must be true for v1 dependency installs";
        }
        return false;
    }

    if (has_pip_dependencies && dependencies.requirements_file.empty())
    {
        if (error)
        {
            *error = "dependencies.requirements_file is required when dependencies are declared";
        }
        return false;
    }

    return true;
}

[[nodiscard]] ProcessLaunchMode toRegistryLaunchMode(const ExtensionLaunchMode launch_mode)
{
    switch (launch_mode)
    {
    case ExtensionLaunchMode::Workflow:
        return ProcessLaunchMode::TopLevel;
    case ExtensionLaunchMode::SingleImage:
    default:
        return ProcessLaunchMode::SingleImage;
    }
}

[[nodiscard]] bool hasHigherPriority(ExtensionPackageSource lhs, ExtensionPackageSource rhs)
{
    if (lhs == rhs)
    {
        return false;
    }

    const auto priority = [](const ExtensionPackageSource source) {
        switch (source)
        {
        case ExtensionPackageSource::Bundled:
            return 0;
        case ExtensionPackageSource::Repository:
            return 1;
        case ExtensionPackageSource::LocalPackage:
            return 2;
        case ExtensionPackageSource::User:
            return 3;
        }

        return 0;
    };

    return priority(lhs) > priority(rhs);
}

[[nodiscard]] std::string extensionDescriptorAvailabilityReason(const ExtensionLoadResult& result)
{
    if (result.status == ExtensionLoadStatus::Ready && result.environment &&
        result.environment->state == ExtensionEnvironmentState::Ready)
    {
        return {};
    }

    if (result.status == ExtensionLoadStatus::Incompatible)
    {
        return result.diagnostic.empty() ? "This extension is incompatible with the current build." : result.diagnostic;
    }

    if (result.environment)
    {
        switch (result.environment->state)
        {
        case ExtensionEnvironmentState::Bootstrapping:
        case ExtensionEnvironmentState::Failed:
        case ExtensionEnvironmentState::Missing:
        case ExtensionEnvironmentState::Stale:
            if (!result.environment->diagnostic.empty())
            {
                return result.environment->diagnostic;
            }
            break;
        case ExtensionEnvironmentState::Ready:
            break;
        }
    }

    if (!result.diagnostic.empty())
    {
        return result.diagnostic;
    }

    return std::string(kUnavailableLaunchReason);
}

[[nodiscard]] ProcessAvailability extensionProcessAvailability(const ExtensionLoadResult& result)
{
    if (result.status == ExtensionLoadStatus::Incompatible)
    {
        return ProcessAvailability::Incompatible;
    }

    if (!result.environment)
    {
        return ProcessAvailability::Unavailable;
    }

    switch (result.environment->state)
    {
    case ExtensionEnvironmentState::Ready:
        return ProcessAvailability::Available;
    case ExtensionEnvironmentState::Bootstrapping:
        return ProcessAvailability::Bootstrapping;
    case ExtensionEnvironmentState::Missing:
        return ProcessAvailability::InstallNeeded;
    case ExtensionEnvironmentState::Stale:
    case ExtensionEnvironmentState::Failed:
        return ProcessAvailability::RepairNeeded;
    }

    return ProcessAvailability::Unavailable;
}

[[nodiscard]] std::filesystem::path settingsSchemaCacheRoot(const ExtensionDiscoveryContext& context)
{
    const std::filesystem::path root = context.environments_root.empty()
                                           ? an::ui::extensions::user_extension_environments_root()
                                           : context.environments_root;
    return root / std::string(kSettingsSchemaCacheDirectory);
}

[[nodiscard]] std::filesystem::path startupActivationCatalogPath(const ExtensionDiscoveryContext& context)
{
    const std::filesystem::path environments_root = context.environments_root.empty()
                                                        ? an::ui::extensions::user_extension_environments_root()
                                                        : context.environments_root;
    return environments_root / std::string(kStartupActivationCatalogName);
}

[[nodiscard]] std::int64_t fileTimeNanoseconds(const std::filesystem::path& path)
{
    std::error_code ec;
    const auto write_time = std::filesystem::last_write_time(path, ec);
    if (ec)
    {
        return -1;
    }

    return std::chrono::duration_cast<std::chrono::nanoseconds>(write_time.time_since_epoch()).count();
}

[[nodiscard]] std::filesystem::path settingsSchemaCachePath(std::string_view extension_id,
                                                            std::string_view validation_fingerprint,
                                                            const ExtensionDiscoveryContext& context)
{
    return settingsSchemaCacheRoot(context) / sanitizeIdentifier(extension_id) /
           (std::string(validation_fingerprint) + ".json");
}

void removeSettingsSchemaCacheForExtension(const std::string_view extension_id,
                                           const ExtensionDiscoveryContext& context)
{
    if (extension_id.empty())
    {
        return;
    }

    std::error_code ec;
    const std::filesystem::path cache_root = settingsSchemaCacheRoot(context) / sanitizeIdentifier(extension_id);
    std::filesystem::remove_all(cache_root, ec);
    if (ec && an::platform::isLoggingInitialized())
    {
        LOG_WARNING("Failed to remove settings-schema cache for extension id='{}' path='{}' reason={}", extension_id,
                    cache_root.generic_string(), ec.message());
    }
}

[[nodiscard]] std::filesystem::path
environmentPythonExecutableForSchemaInspection(const std::filesystem::path& environment_root)
{
#ifdef _WIN32
    const std::vector<std::filesystem::path> candidates = {environment_root / "Scripts" / "python.exe",
                                                           environment_root / "python.exe"};
#else
    const std::vector<std::filesystem::path> candidates = {environment_root / "bin" / "python",
                                                           environment_root / "bin" / "python3"};
#endif

    for (const auto& candidate : candidates)
    {
        if (an::ui::extensions::isRegularFile(candidate))
        {
            return candidate.lexically_normal();
        }
    }

    return {};
}

[[nodiscard]] std::filesystem::path resolveSettingsSchemaHostExecutable(const ExtensionManifest& manifest,
                                                                        const ExtensionDiscoveryContext& context)
{
    if ((manifest.sdk_backend == ExtensionSdkBackend::Protocol || manifest.sdk_backend == ExtensionSdkBackend::Rpc) &&
        !context.extension_host_lite_executable.empty())
    {
        return context.extension_host_lite_executable;
    }
    if (manifest.sdk_backend == ExtensionSdkBackend::Runtime && !context.extension_host_executable.empty())
    {
        return context.extension_host_executable;
    }
    if (!QCoreApplication::instance())
    {
        return {};
    }

    const std::filesystem::path app_dir(QCoreApplication::applicationDirPath().toStdString());
    if (manifest.sdk_backend == ExtensionSdkBackend::Protocol || manifest.sdk_backend == ExtensionSdkBackend::Rpc)
    {
        return an::ui::extensions::bundled_extension_host_lite_executable_for_app_dir(app_dir);
    }
    return an::ui::extensions::bundled_extension_host_executable_for_app_dir(app_dir);
}

[[nodiscard]] std::filesystem::path resolveSettingsSchemaRuntimeRoot(const ExtensionDiscoveryContext& context)
{
    if (!context.runtime_root.empty())
    {
        return context.runtime_root;
    }
    if (!QCoreApplication::instance())
    {
        return {};
    }

    return std::filesystem::path(QCoreApplication::applicationDirPath().toStdString()) / "PythonLib";
}

[[nodiscard]] QString settingsSchemaStateName(const ExtensionSettingsSchemaState state)
{
    switch (state)
    {
    case ExtensionSettingsSchemaState::Ready:
        return QStringLiteral("ready");
    case ExtensionSettingsSchemaState::Failed:
        return QStringLiteral("failed");
    case ExtensionSettingsSchemaState::Unavailable:
    default:
        return QStringLiteral("unavailable");
    }
}

[[nodiscard]] bool parseSettingsSchemaState(const QString& value, ExtensionSettingsSchemaState* out_state)
{
    if (!out_state)
    {
        return false;
    }

    if (value == QStringLiteral("ready"))
    {
        *out_state = ExtensionSettingsSchemaState::Ready;
        return true;
    }
    if (value == QStringLiteral("failed"))
    {
        *out_state = ExtensionSettingsSchemaState::Failed;
        return true;
    }
    if (value == QStringLiteral("unavailable"))
    {
        *out_state = ExtensionSettingsSchemaState::Unavailable;
        return true;
    }

    return false;
}

[[nodiscard]] bool loadSettingsSchemaSnapshotFromCache(const std::filesystem::path& cache_path,
                                                       ExtensionSettingsSchemaSnapshot* out_snapshot,
                                                       std::string* error)
{
    if (!out_snapshot)
    {
        if (error)
        {
            *error = "Settings schema cache output is null";
        }
        return false;
    }

    const std::string raw_json = readFileText(cache_path);
    if (raw_json.empty())
    {
        if (error)
        {
            *error = "Failed to read cached settings schema";
        }
        return false;
    }

    QJsonParseError parse_error;
    const QJsonDocument document = QJsonDocument::fromJson(QByteArray::fromStdString(raw_json), &parse_error);
    if (parse_error.error != QJsonParseError::NoError || !document.isObject())
    {
        if (error)
        {
            *error = "Cached settings schema is not valid JSON";
        }
        return false;
    }

    const QJsonObject object = document.object();
    int cache_version = 0;
    std::string validation_fingerprint;
    std::string state_text;
    if (!requireInt(object, QStringLiteral("cache_format_version"), cache_version, error) ||
        !requireString(object, QStringLiteral("validation_fingerprint"), validation_fingerprint, error) ||
        !requireString(object, QStringLiteral("state"), state_text, error))
    {
        return false;
    }

    if (cache_version != kSettingsSchemaCacheFormatVersion &&
        cache_version != kPreviousSettingsSchemaCacheFormatVersion)
    {
        if (error)
        {
            *error = "Cached settings schema uses an unsupported format version";
        }
        return false;
    }

    ExtensionSettingsSchemaState state = ExtensionSettingsSchemaState::Unavailable;
    if (!parseSettingsSchemaState(QString::fromStdString(state_text), &state))
    {
        if (error)
        {
            *error = "Cached settings schema uses an unknown state";
        }
        return false;
    }

    out_snapshot->state = state;
    out_snapshot->validation_fingerprint = std::move(validation_fingerprint);
    out_snapshot->diagnostic = object.value(QStringLiteral("diagnostic")).toString().toStdString();
    out_snapshot->cache_path = cache_path;
    out_snapshot->schema = object.value(QStringLiteral("settings_schema")).toArray();
    out_snapshot->process_schemas.clear();
    out_snapshot->process_params_schemas.clear();
    out_snapshot->from_cache = true;

    const QJsonValue process_schemas_value = object.value(QStringLiteral("process_settings_schemas"));
    if (!process_schemas_value.isUndefined())
    {
        if (!process_schemas_value.isObject())
        {
            if (error)
            {
                *error = "Cached process settings schemas must be an object";
            }
            return false;
        }

        const QJsonObject process_schemas = process_schemas_value.toObject();
        for (auto it = process_schemas.constBegin(); it != process_schemas.constEnd(); ++it)
        {
            if (!it.value().isArray())
            {
                if (error)
                {
                    *error = "Cached process settings schema entries must be arrays";
                }
                return false;
            }
            out_snapshot->process_schemas.emplace(it.key().toStdString(), it.value().toArray());
        }
    }

    const QJsonValue process_params_schemas_value = object.value(QStringLiteral("process_params_schemas"));
    if (!process_params_schemas_value.isUndefined())
    {
        if (!process_params_schemas_value.isObject())
        {
            if (error)
            {
                *error = "Cached process parameter schemas must be an object";
            }
            return false;
        }

        const QJsonObject process_params_schemas = process_params_schemas_value.toObject();
        for (auto it = process_params_schemas.constBegin(); it != process_params_schemas.constEnd(); ++it)
        {
            if (!it.value().isArray())
            {
                if (error)
                {
                    *error = "Cached process parameter schema entries must be arrays";
                }
                return false;
            }
            out_snapshot->process_params_schemas.emplace(it.key().toStdString(), it.value().toArray());
        }
    }

    if (state == ExtensionSettingsSchemaState::Ready && !object.value(QStringLiteral("settings_schema")).isArray() &&
        out_snapshot->process_schemas.empty())
    {
        if (error)
        {
            *error = "Cached ready settings schema is missing schema arrays";
        }
        return false;
    }

    return true;
}

[[nodiscard]] bool writeSettingsSchemaSnapshotToCache(std::string_view extension_id,
                                                      const ExtensionSettingsSchemaSnapshot& snapshot,
                                                      std::string* error)
{
    if (snapshot.cache_path.empty())
    {
        if (error)
        {
            *error = "Settings schema cache path is empty";
        }
        return false;
    }

    std::error_code ec;
    std::filesystem::create_directories(snapshot.cache_path.parent_path(), ec);
    if (ec)
    {
        if (error)
        {
            *error = "Failed to create the settings schema cache directory";
        }
        return false;
    }

    QJsonObject process_schemas;
    for (const auto& [class_name, schema] : snapshot.process_schemas)
    {
        process_schemas.insert(QString::fromStdString(class_name), schema);
    }
    QJsonObject process_params_schemas;
    for (const auto& [class_name, schema] : snapshot.process_params_schemas)
    {
        process_params_schemas.insert(QString::fromStdString(class_name), schema);
    }

    QJsonObject root = {
        {QStringLiteral("cache_format_version"), kSettingsSchemaCacheFormatVersion},
        {QStringLiteral("extension_id"), QString::fromStdString(std::string(extension_id))},
        {QStringLiteral("validation_fingerprint"), QString::fromStdString(snapshot.validation_fingerprint)},
        {QStringLiteral("state"), settingsSchemaStateName(snapshot.state)},
        {QStringLiteral("diagnostic"), QString::fromStdString(snapshot.diagnostic)},
        {QStringLiteral("settings_schema"), snapshot.schema}};
    if (!process_schemas.isEmpty())
    {
        root.insert(QStringLiteral("process_settings_schemas"), process_schemas);
    }
    if (!process_params_schemas.isEmpty())
    {
        root.insert(QStringLiteral("process_params_schemas"), process_params_schemas);
    }

    QSaveFile file(QString::fromStdString(snapshot.cache_path.string()));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        if (error)
        {
            *error = "Failed to open the settings schema cache for writing";
        }
        return false;
    }

    const QByteArray payload = QJsonDocument(root).toJson(QJsonDocument::Indented);
    if (file.write(payload) != payload.size() || !file.commit())
    {
        if (error)
        {
            *error = "Failed to persist the settings schema cache";
        }
        return false;
    }

    return true;
}

[[nodiscard]] std::string boundedLogText(std::string text, const std::size_t max_length = 4096)
{
    std::replace(text.begin(), text.end(), '\r', ' ');
    std::replace(text.begin(), text.end(), '\n', ' ');

    if (text.size() <= max_length)
    {
        return text;
    }

    text.resize(max_length);
    text += "...[truncated]";
    return text;
}

[[nodiscard]] std::string processStreamForLog(const QByteArray& bytes)
{
    return boundedLogText(QString::fromUtf8(bytes).trimmed().toStdString());
}

[[nodiscard]] std::string inspectSettingsSchemaFailureText(const QByteArray& stderr_bytes,
                                                           const QByteArray& stdout_bytes, std::string_view fallback)
{
    const QString stderr_text = QString::fromUtf8(stderr_bytes).trimmed();
    if (!stderr_text.isEmpty())
    {
        return stderr_text.toStdString();
    }

    const QString stdout_text = QString::fromUtf8(stdout_bytes).trimmed();
    if (!stdout_text.isEmpty())
    {
        return stdout_text.toStdString();
    }

    return std::string(fallback);
}

void logSettingsSchemaInspectionFailure(const ExtensionManifest& manifest, const std::string_view process_class,
                                        const std::filesystem::path& host_executable,
                                        const std::filesystem::path& runtime_root,
                                        const std::filesystem::path& environment_root, const std::string_view reason,
                                        const bool warning, const QProcess* process,
                                        const QByteArray& stdout_bytes = {}, const QByteArray& stderr_bytes = {})
{
    if (!an::platform::isLoggingInitialized())
    {
        return;
    }

    const int exit_code = process ? process->exitCode() : -1;
    const std::string exit_status =
        process ? (process->exitStatus() == QProcess::NormalExit ? "normal" : "crash") : "not-started";
    const std::string process_error = process ? process->errorString().toStdString() : std::string{};
    const std::string reason_text = boundedLogText(std::string(reason));
    const std::string stdout_text = processStreamForLog(stdout_bytes);
    const std::string stderr_text = processStreamForLog(stderr_bytes);

    if (warning)
    {
        LOG_WARNING("Settings schema inspection warning extension id='{}' version='{}' class='{}' reason='{}' "
                    "host='{}' package_root='{}' environment_root='{}' runtime_root='{}' exit_code={} "
                    "exit_status='{}' process_error='{}' stdout='{}' stderr='{}'",
                    manifest.id, manifest.version, std::string(process_class), reason_text,
                    host_executable.generic_string(), manifest.package_root, environment_root.generic_string(),
                    runtime_root.generic_string(), exit_code, exit_status, process_error, stdout_text, stderr_text);
        return;
    }

    LOG_ERROR("Settings schema inspection failed extension id='{}' version='{}' class='{}' reason='{}' "
              "host='{}' package_root='{}' environment_root='{}' runtime_root='{}' exit_code={} "
              "exit_status='{}' process_error='{}' stdout='{}' stderr='{}'",
              manifest.id, manifest.version, std::string(process_class), reason_text, host_executable.generic_string(),
              manifest.package_root, environment_root.generic_string(), runtime_root.generic_string(), exit_code,
              exit_status, process_error, stdout_text, stderr_text);
}

[[nodiscard]] std::string sdkBackendOptionValue(const ExtensionSdkBackend backend)
{
    return an::ui::extensions::extension_sdk_backend_id(backend);
}

[[nodiscard]] std::string unavailableBackendDiagnostic(const ExtensionSdkBackend backend)
{
    switch (backend)
    {
    case ExtensionSdkBackend::Runtime:
        return "The runtime SDK backend (" + an::ui::extensions::extension_host_mode_label(backend) +
               ") is not available in this build. Full hosted extensions require AfterNightExtensionHost and the "
               "GPL-compatible direct runtime engine backend.";
    case ExtensionSdkBackend::Protocol:
        return "The protocol SDK backend (" + an::ui::extensions::extension_host_mode_label(backend) +
               ") is not available in this build. Lite hosted extensions require AfterNightExtensionHostLite and the "
               "protocol app/view service bridge.";
    case ExtensionSdkBackend::Rpc:
        return "The rpc SDK backend (" + an::ui::extensions::extension_host_mode_label(backend) +
               ") is not available in this build. Lite hosted + RPC extensions require AfterNightExtensionHostLite "
               "and the deferred AfterNightSdkHost sidecar.";
    }

    return "The requested SDK backend is not available in this build.";
}

struct InspectedExtensionProcessSchemas
{
    QJsonArray settings_schema;
    QJsonArray params_schema;
};

[[nodiscard]] bool runSettingsSchemaInspection(const std::filesystem::path& host_executable,
                                               const std::filesystem::path& runtime_root,
                                               const std::filesystem::path& environment_root,
                                               const ExtensionManifest& manifest, std::string_view process_class,
                                               const int timeout_ms, InspectedExtensionProcessSchemas* out_schemas,
                                               std::string* error,
                                               const ExtensionOperationCancellationToken& cancellation_token = {})
{
    const auto fail = [&](std::string detail, const bool warning, const QProcess* process = nullptr,
                          QByteArray stdout_bytes = {}, QByteArray stderr_bytes = {}) -> bool {
        if (error)
        {
            *error = detail;
        }
        logSettingsSchemaInspectionFailure(manifest, process_class, host_executable, runtime_root, environment_root,
                                           detail, warning, process, stdout_bytes, stderr_bytes);
        return false;
    };

    if (!out_schemas)
    {
        return fail("Settings schema output is null", false);
    }
    if (cancellationRequested(cancellation_token))
    {
        return fail("Operation cancelled.", true);
    }

    QProcess inspect;
    inspect.setProgram(QString::fromStdString(host_executable.string()));
    inspect.setArguments({QStringLiteral("--inspect"), QStringLiteral("--inspect-settings"),
                          QStringLiteral("--inspect-params"), QStringLiteral("--sdk-backend"),
                          QString::fromStdString(sdkBackendOptionValue(manifest.sdk_backend)),
                          QStringLiteral("--extension-package-root"), QString::fromStdString(manifest.package_root),
                          QStringLiteral("--entry-point"), QString::fromStdString(manifest.entry_point),
                          QStringLiteral("--process-class"), QString::fromStdString(std::string(process_class)),
                          QStringLiteral("--environment-root"), QString::fromStdString(environment_root.string()),
                          QStringLiteral("--runtime-root"), QString::fromStdString(runtime_root.string())});
    inspect.start();
    if (!inspect.waitForStarted())
    {
        return fail("Failed to start the extension-host settings schema inspection process: " +
                        inspect.errorString().toStdString(),
                    false, &inspect);
    }
    QElapsedTimer timer;
    timer.start();
    while (!inspect.waitForFinished(100))
    {
        if (cancellationRequested(cancellation_token))
        {
            inspect.kill();
            inspect.waitForFinished();
            return fail("Operation cancelled.", true, &inspect, inspect.readAllStandardOutput(),
                        inspect.readAllStandardError());
        }

        if (timeout_ms >= 0 && timer.elapsed() >= timeout_ms)
        {
            inspect.kill();
            inspect.waitForFinished();
            return fail("Timed out while waiting for settings schema inspection to finish", false, &inspect,
                        inspect.readAllStandardOutput(), inspect.readAllStandardError());
        }
    }
    if (inspect.exitStatus() != QProcess::NormalExit || inspect.exitCode() != 0)
    {
        const QByteArray stdout_bytes = inspect.readAllStandardOutput();
        const QByteArray stderr_bytes = inspect.readAllStandardError();
        return fail(inspectSettingsSchemaFailureText(stderr_bytes, stdout_bytes,
                                                     "Extension-host settings schema inspection failed"),
                    false, &inspect, stdout_bytes, stderr_bytes);
    }

    const QByteArray stdout_bytes = inspect.readAllStandardOutput().trimmed();
    const QByteArray stderr_bytes = inspect.readAllStandardError().trimmed();
    if (stdout_bytes.isEmpty())
    {
        return fail("Extension-host settings schema inspection did not return schema JSON", false, &inspect,
                    stdout_bytes, stderr_bytes);
    }

    QJsonParseError parse_error;
    const QJsonDocument document = QJsonDocument::fromJson(stdout_bytes, &parse_error);
    if (parse_error.error != QJsonParseError::NoError || !document.isObject())
    {
        return fail("Extension-host settings schema inspection returned invalid JSON: " +
                        parse_error.errorString().toStdString(),
                    false, &inspect, stdout_bytes, stderr_bytes);
    }

    const QJsonObject root = document.object();
    const QJsonValue settings_schema_value = root.value(QStringLiteral("settings_schema"));
    if (!settings_schema_value.isArray())
    {
        return fail("Extension-host settings schema inspection JSON is missing settings_schema", false, &inspect,
                    stdout_bytes, stderr_bytes);
    }

    const QJsonValue params_schema_value = root.value(QStringLiteral("params_schema"));
    if (!params_schema_value.isUndefined() && !params_schema_value.isArray())
    {
        return fail("Extension-host settings schema inspection JSON has an invalid params_schema", false, &inspect,
                    stdout_bytes, stderr_bytes);
    }

    out_schemas->settings_schema = settings_schema_value.toArray();
    out_schemas->params_schema = params_schema_value.isArray() ? params_schema_value.toArray() : QJsonArray{};
    return true;
}

[[nodiscard]] QCborMap jsonObjectToCborMap(const QJsonValue& value)
{
    if (!value.isObject())
    {
        return {};
    }
    const QCborValue cbor_value = QCborValue::fromVariant(value.toObject().toVariantMap());
    return cbor_value.isMap() ? cbor_value.toMap() : QCborMap{};
}

[[nodiscard]] ExtensionSettingsActionResult settingsActionResultFromJson(const QJsonObject& root)
{
    ExtensionSettingsActionResult result;
    result.ok = root.value(QStringLiteral("ok")).toBool(false);
    result.message = root.value(QStringLiteral("message")).toString().toStdString();

    const QString tone =
        root.value(QStringLiteral("tone")).toString(result.ok ? QStringLiteral("success") : QStringLiteral("error"));
    if (tone == QStringLiteral("info") || tone == QStringLiteral("success") || tone == QStringLiteral("warning") ||
        tone == QStringLiteral("error"))
    {
        result.tone = tone.toStdString();
    }
    else
    {
        result.tone = result.ok ? "success" : "error";
    }

    result.settings_updates = jsonObjectToCborMap(root.value(QStringLiteral("settings_updates")));
    result.transient_updates = jsonObjectToCborMap(root.value(QStringLiteral("transient_updates")));
    return result;
}

void consumeSettingsActionStandardErrorLine(const QByteArray& raw_line, const ExtensionSettingsActionRequest& request,
                                            QByteArray* diagnostic_stderr)
{
    QByteArray line = raw_line.trimmed();
    if (line.isEmpty())
    {
        return;
    }

    const QByteArray prefix(kSettingsActionUpdatePrefix.data(), static_cast<int>(kSettingsActionUpdatePrefix.size()));
    if (!line.startsWith(prefix))
    {
        if (diagnostic_stderr)
        {
            diagnostic_stderr->append(line);
            diagnostic_stderr->append('\n');
        }
        return;
    }

    if (!request.update_callback)
    {
        return;
    }

    const QByteArray payload = line.mid(prefix.size()).trimmed();
    QJsonParseError parse_error;
    const QJsonDocument document = QJsonDocument::fromJson(payload, &parse_error);
    if (parse_error.error != QJsonParseError::NoError || !document.isObject())
    {
        if (diagnostic_stderr)
        {
            diagnostic_stderr->append(line);
            diagnostic_stderr->append('\n');
        }
        return;
    }

    request.update_callback(settingsActionResultFromJson(document.object()));
}

void consumeSettingsActionStandardError(QByteArray& pending_stderr, const QByteArray& chunk,
                                        const ExtensionSettingsActionRequest& request, QByteArray* diagnostic_stderr)
{
    if (!chunk.isEmpty())
    {
        pending_stderr.append(chunk);
    }

    int newline_index = pending_stderr.indexOf('\n');
    while (newline_index >= 0)
    {
        QByteArray line = pending_stderr.left(newline_index);
        if (line.endsWith('\r'))
        {
            line.chop(1);
        }
        pending_stderr.remove(0, newline_index + 1);
        consumeSettingsActionStandardErrorLine(line, request, diagnostic_stderr);
        newline_index = pending_stderr.indexOf('\n');
    }
}

[[nodiscard]] ExtensionSettingsActionResult failedSettingsActionResult(std::string message, std::string tone = "error")
{
    ExtensionSettingsActionResult result;
    result.ok = false;
    result.message = std::move(message);
    result.tone = std::move(tone);
    return result;
}

[[nodiscard]] ExtensionSettingsActionResult runSettingsActionHostProcess(const ExtensionSettingsActionRequest& request)
{
    if (request.action_id.empty())
    {
        return failedSettingsActionResult("Missing extension settings action id");
    }
    if (!request.load_result.manifest)
    {
        return failedSettingsActionResult("Extension settings action requires manifest metadata");
    }
    if (request.load_result.status != ExtensionLoadStatus::Ready || !request.load_result.environment ||
        request.load_result.environment->state != ExtensionEnvironmentState::Ready)
    {
        return failedSettingsActionResult("Extension environment is not ready for settings actions", "warning");
    }
    if (cancellationRequested(request.cancellation_token))
    {
        ExtensionSettingsActionResult result = failedSettingsActionResult("Operation cancelled.", "warning");
        result.cancelled = true;
        return result;
    }

    const ExtensionManifest& manifest = *request.load_result.manifest;
    const std::filesystem::path host_executable = resolveSettingsSchemaHostExecutable(manifest, request.context);
    if (host_executable.empty() || !an::ui::extensions::isRegularFile(host_executable))
    {
        return failedSettingsActionResult("Extension host is unavailable for settings actions");
    }

    const std::filesystem::path runtime_root = resolveSettingsSchemaRuntimeRoot(request.context);
    if (runtime_root.empty() || !std::filesystem::exists(runtime_root))
    {
        return failedSettingsActionResult("Bundled Python runtime root is unavailable for settings actions");
    }

    const std::filesystem::path environment_root = request.load_result.environment->environment_path;
    if (environment_root.empty() || !std::filesystem::exists(environment_root))
    {
        return failedSettingsActionResult("Extension environment root is unavailable for settings actions");
    }

    QProcess process;
    process.setProgram(QString::fromStdString(host_executable.string()));
    process.setArguments(
        {QStringLiteral("--settings-action"), QString::fromStdString(request.action_id),
         QStringLiteral("--sdk-backend"), QString::fromStdString(sdkBackendOptionValue(manifest.sdk_backend)),
         QStringLiteral("--extension-package-root"), QString::fromStdString(manifest.package_root),
         QStringLiteral("--entry-point"), QString::fromStdString(manifest.entry_point),
         QStringLiteral("--process-class"),
         QString::fromStdString(manifest.processes.empty() ? std::string() : manifest.processes.front().class_name),
         QStringLiteral("--environment-root"), QString::fromStdString(environment_root.string()),
         QStringLiteral("--runtime-root"), QString::fromStdString(runtime_root.string())});
    process.start();
    if (!process.waitForStarted())
    {
        return failedSettingsActionResult("Failed to start extension settings action process");
    }

    process.write(QCborValue(request.settings_snapshot).toCbor());
    process.closeWriteChannel();

    QElapsedTimer timer;
    timer.start();
    QByteArray stdout_bytes;
    QByteArray pending_stderr;
    QByteArray diagnostic_stderr;
    const auto drain_process_output = [&]() {
        stdout_bytes.append(process.readAllStandardOutput());
        consumeSettingsActionStandardError(pending_stderr, process.readAllStandardError(), request, &diagnostic_stderr);
    };
    const int timeout_ms = request.timeout_ms > 0 ? request.timeout_ms : 60000;
    while (!process.waitForFinished(100))
    {
        drain_process_output();
        if (cancellationRequested(request.cancellation_token))
        {
            process.kill();
            process.waitForFinished();
            drain_process_output();
            ExtensionSettingsActionResult result = failedSettingsActionResult("Operation cancelled.", "warning");
            result.cancelled = true;
            return result;
        }
        if (timer.elapsed() >= timeout_ms)
        {
            process.kill();
            process.waitForFinished();
            drain_process_output();
            ExtensionSettingsActionResult result =
                failedSettingsActionResult("Timed out while waiting for the settings action to finish");
            result.timed_out = true;
            return result;
        }
    }
    drain_process_output();
    consumeSettingsActionStandardError(pending_stderr, QByteArray{}, request, &diagnostic_stderr);
    if (!pending_stderr.isEmpty())
    {
        consumeSettingsActionStandardErrorLine(pending_stderr, request, &diagnostic_stderr);
        pending_stderr.clear();
    }

    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0)
    {
        std::string message = "Extension settings action process failed";
        const QByteArray trimmed_stderr = diagnostic_stderr.trimmed();
        if (!trimmed_stderr.isEmpty())
        {
            message += ": ";
            message += QString::fromUtf8(trimmed_stderr).toStdString();
        }
        return failedSettingsActionResult(std::move(message));
    }

    const QByteArray final_stdout = stdout_bytes.trimmed();
    if (final_stdout.isEmpty())
    {
        return failedSettingsActionResult("Extension settings action did not return a result");
    }

    QJsonParseError parse_error;
    const QJsonDocument document = QJsonDocument::fromJson(final_stdout, &parse_error);
    if (parse_error.error != QJsonParseError::NoError || !document.isObject())
    {
        return failedSettingsActionResult("Extension settings action returned invalid JSON");
    }

    return settingsActionResultFromJson(document.object());
}

[[nodiscard]] std::string settingsSchemaUnavailableDiagnostic(const ExtensionLoadResult& result)
{
    if (result.status == ExtensionLoadStatus::Incompatible)
    {
        return result.diagnostic.empty() ? std::string(kSettingsSchemaUnavailableReason) : result.diagnostic;
    }
    if (result.environment && !result.environment->diagnostic.empty())
    {
        return result.environment->diagnostic;
    }
    if (!result.diagnostic.empty())
    {
        return result.diagnostic;
    }

    return std::string(kSettingsSchemaUnavailableReason);
}

ExtensionSettingsSchemaSnapshot
inspectSettingsSchemaForResult(const ExtensionLoadResult& result, const ExtensionDiscoveryContext& context,
                               const ExtensionOperationCancellationToken& cancellation_token = {},
                               const ExtensionOperationProgressCallback& progress_callback = {})
{
    ExtensionSettingsSchemaSnapshot snapshot;
    snapshot.state = ExtensionSettingsSchemaState::Unavailable;
    snapshot.diagnostic = settingsSchemaUnavailableDiagnostic(result);

    if (!result.manifest)
    {
        return snapshot;
    }

    if (result.environment)
    {
        snapshot.validation_fingerprint = result.environment->validation_fingerprint;
        if (!snapshot.validation_fingerprint.empty())
        {
            snapshot.cache_path =
                settingsSchemaCachePath(result.manifest->id, snapshot.validation_fingerprint, context);
        }
    }

    if (result.status != ExtensionLoadStatus::Ready || !result.environment ||
        result.environment->state != ExtensionEnvironmentState::Ready)
    {
        return snapshot;
    }

    if (!snapshot.cache_path.empty())
    {
        ExtensionSettingsSchemaSnapshot cached;
        std::string cache_error;
        if (std::filesystem::exists(snapshot.cache_path) &&
            loadSettingsSchemaSnapshotFromCache(snapshot.cache_path, &cached, &cache_error) &&
            cached.validation_fingerprint == snapshot.validation_fingerprint)
        {
            const bool missing_native_param_schema =
                result.manifest->sdk_backend == ExtensionSdkBackend::Runtime &&
                std::any_of(result.manifest->processes.begin(), result.manifest->processes.end(),
                            [&cached](const ExtensionProcessManifest& process) {
                                return !cached.process_params_schemas.contains(process.class_name);
                            });
            if (cached.state != ExtensionSettingsSchemaState::Failed && !missing_native_param_schema)
            {
                if (an::platform::isLoggingInitialized())
                {
                    LOG_DEBUG("Loaded settings-schema cache extension id='{}' version='{}' cache_path='{}' "
                              "process_schemas={}",
                              result.manifest->id, result.manifest->version, snapshot.cache_path.generic_string(),
                              cached.process_schemas.size());
                }
                return cached;
            }
            if (missing_native_param_schema && an::platform::isLoggingInitialized())
            {
                LOG_INFO("Refreshing settings-schema cache without native parameter schemas extension id='{}' "
                         "version='{}' cache_path='{}'",
                         result.manifest->id, result.manifest->version, snapshot.cache_path.generic_string());
            }
            else if (an::platform::isLoggingInitialized())
            {
                LOG_DEBUG("Ignoring failed settings-schema cache extension id='{}' version='{}' cache_path='{}'",
                          result.manifest->id, result.manifest->version, snapshot.cache_path.generic_string());
            }
        }
    }

    if (!context.eager_settings_schema_inspection)
    {
        snapshot.diagnostic = "Settings schema inspection is deferred for this discovery pass.";
        return snapshot;
    }

    const std::filesystem::path env_python =
        environmentPythonExecutableForSchemaInspection(result.environment->environment_path);
    if (env_python.empty())
    {
        snapshot.diagnostic =
            "Settings schema unavailable until the extension environment includes a provisioned Python interpreter.";
        return snapshot;
    }

    const std::filesystem::path host_executable = resolveSettingsSchemaHostExecutable(*result.manifest, context);
    if (host_executable.empty() || !an::ui::extensions::isRegularFile(host_executable))
    {
        snapshot.diagnostic = (result.manifest->sdk_backend == ExtensionSdkBackend::Protocol ||
                               result.manifest->sdk_backend == ExtensionSdkBackend::Rpc)
                                  ? "Settings schema inspection is unavailable because AfterNightExtensionHostLite is "
                                    "not deployed."
                                  : "Settings schema inspection is unavailable because AfterNightExtensionHost is not "
                                    "deployed.";
        return snapshot;
    }

    const std::filesystem::path runtime_root = resolveSettingsSchemaRuntimeRoot(context);
    if (runtime_root.empty() || !std::filesystem::exists(runtime_root))
    {
        snapshot.diagnostic =
            "Settings schema inspection is unavailable because the bundled Python runtime root is missing.";
        return snapshot;
    }

    QByteArray canonical_schema_json;
    QJsonArray canonical_schema;
    bool process_schemas_differ = false;
    std::map<std::string, QJsonArray> process_schemas;
    std::map<std::string, QJsonArray> process_params_schemas;
    const int timeout_ms =
        context.settings_schema_inspection_timeout_ms > 0 ? context.settings_schema_inspection_timeout_ms : 10000;
    const std::vector<std::string> process_classes = uniqueProcessClassesForInspection(*result.manifest);
    const int process_class_count = static_cast<int>(process_classes.size());
    if (process_class_count > 0)
    {
        if (an::platform::isLoggingInitialized())
        {
            LOG_DEBUG(
                "Inspecting settings schemas extension id='{}' version='{}' process_classes={} host='{}' env='{}'",
                result.manifest->id, result.manifest->version, process_class_count, host_executable.generic_string(),
                result.environment->environment_path.generic_string());
        }
        reportSettingsSchemaInspectionProgress(progress_callback, "prime_startup_caches",
                                               "Inspecting extension settings schemas", 0, process_class_count, false);
    }

    int inspected_class_count = 0;
    for (const std::string& process_class : process_classes)
    {
        if (cancellationRequested(cancellation_token))
        {
            snapshot.state = ExtensionSettingsSchemaState::Failed;
            snapshot.schema = {};
            snapshot.diagnostic = "Operation cancelled.";
            return snapshot;
        }

        const int display_index = inspected_class_count + 1;
        const std::string inspect_message = "Priming startup cache " + std::to_string(display_index) + "/" +
                                            std::to_string(process_class_count) + ": " + process_class;
        reportSettingsSchemaInspectionProgress(progress_callback, "prime_startup_caches", inspect_message,
                                               inspected_class_count, process_class_count, false);
        if (an::platform::isLoggingInitialized())
        {
            LOG_DEBUG("Inspecting settings schema extension id='{}' version='{}' class='{}' index={} total={}",
                      result.manifest->id, result.manifest->version, process_class, display_index, process_class_count);
        }

        InspectedExtensionProcessSchemas inspected_schemas;
        std::string inspection_error;
        if (!runSettingsSchemaInspection(host_executable, runtime_root, result.environment->environment_path,
                                         *result.manifest, process_class, timeout_ms, &inspected_schemas,
                                         &inspection_error, cancellation_token))
        {
            snapshot.state = ExtensionSettingsSchemaState::Failed;
            snapshot.schema = {};
            snapshot.diagnostic =
                "Settings schema inspection failed for class '" + process_class + "': " + inspection_error;
            if (an::platform::isLoggingInitialized())
            {
                LOG_WARNING("Settings schema inspection marked extension unavailable id='{}' version='{}' class='{}' "
                            "diagnostic='{}'",
                            result.manifest->id, result.manifest->version, process_class,
                            boundedLogText(snapshot.diagnostic));
            }
            if (cancellationRequested(cancellation_token))
            {
                snapshot.diagnostic = "Operation cancelled.";
                return snapshot;
            }
            if (!snapshot.cache_path.empty())
            {
                std::string cache_error;
                if (!writeSettingsSchemaSnapshotToCache(result.manifest->id, snapshot, &cache_error) &&
                    an::platform::isLoggingInitialized())
                {
                    LOG_WARNING("Failed to persist settings-schema failure cache for extension id='{}': {}",
                                result.manifest->id, cache_error);
                }
            }
            return snapshot;
        }

        const QJsonArray class_schema = inspected_schemas.settings_schema.isEmpty() ? inspected_schemas.params_schema
                                                                                    : inspected_schemas.settings_schema;
        process_schemas.emplace(process_class, class_schema);
        process_params_schemas.emplace(process_class, inspected_schemas.params_schema);
        ++inspected_class_count;
        reportSettingsSchemaInspectionProgress(progress_callback, "prime_startup_caches",
                                               "Primed startup cache " + std::to_string(inspected_class_count) + "/" +
                                                   std::to_string(process_class_count) + ": " + process_class,
                                               inspected_class_count, process_class_count, false);
        if (an::platform::isLoggingInitialized())
        {
            LOG_DEBUG("Inspected extension schemas id='{}' version='{}' class='{}' settings_fields={} "
                      "params_fields={} index={} total={}",
                      result.manifest->id, result.manifest->version, process_class, class_schema.size(),
                      inspected_schemas.params_schema.size(), inspected_class_count, process_class_count);
        }
        const QByteArray class_schema_json = QJsonDocument(class_schema).toJson(QJsonDocument::Compact);
        if (canonical_schema_json.isEmpty())
        {
            canonical_schema_json = class_schema_json;
            canonical_schema = class_schema;
            continue;
        }

        if (class_schema_json != canonical_schema_json)
        {
            process_schemas_differ = true;
        }
    }

    snapshot.state = ExtensionSettingsSchemaState::Ready;
    snapshot.process_schemas = std::move(process_schemas);
    snapshot.process_params_schemas = std::move(process_params_schemas);
    snapshot.schema = process_schemas_differ ? QJsonArray{} : std::move(canonical_schema);
    if (process_schemas_differ)
    {
        snapshot.diagnostic = "Cached per-process settings schemas for pre-launch extension configuration.";
    }
    else
    {
        snapshot.diagnostic = snapshot.schema.isEmpty()
                                  ? "Cached empty settings schema for the ready extension environment."
                                  : "Cached settings schema for pre-launch extension configuration.";
    }
    if (!snapshot.cache_path.empty())
    {
        std::string cache_error;
        if (!writeSettingsSchemaSnapshotToCache(result.manifest->id, snapshot, &cache_error) &&
            an::platform::isLoggingInitialized())
        {
            LOG_WARNING("Failed to persist settings-schema cache for extension id='{}': {}", result.manifest->id,
                        cache_error);
        }
    }
    return snapshot;
}

void eagerlyInspectSettingsSchemas(std::vector<ExtensionLoadResult>* results, const ExtensionDiscoveryContext& context,
                                   const ExtensionOperationCancellationToken& cancellation_token = {})
{
    if (!results)
    {
        return;
    }

    for (auto& result : *results)
    {
        if (cancellationRequested(cancellation_token))
        {
            return;
        }

        if (!result.manifest)
        {
            continue;
        }

        result.settings_schema = inspectSettingsSchemaForResult(result, context, cancellation_token);
    }
}

struct DisplayNameCollisionInfo
{
    int count = 0;
    std::set<std::string> extension_ids;
};

[[nodiscard]] std::map<std::string, DisplayNameCollisionInfo, std::less<>>
collectDisplayNameCollisions(const std::vector<ExtensionLoadResult>& discovered)
{
    std::map<std::string, DisplayNameCollisionInfo, std::less<>> collisions;

    for (const ExtensionLoadResult& result : discovered)
    {
        if (!result.manifest || result.status == ExtensionLoadStatus::Invalid)
        {
            continue;
        }

        for (const ExtensionProcessManifest& process : result.manifest->processes)
        {
            auto& collision = collisions[process.display_name];
            ++collision.count;
            collision.extension_ids.insert(result.manifest->id);
        }
    }

    return collisions;
}

[[nodiscard]] std::string
buildDisambiguatedDisplayName(const ExtensionManifest& manifest, const ExtensionProcessManifest& process,
                              const std::map<std::string, DisplayNameCollisionInfo, std::less<>>& collisions)
{
    const auto it = collisions.find(process.display_name);
    if (it == collisions.end() || it->second.count < 2)
    {
        return process.display_name;
    }

    const std::string& suffix = it->second.extension_ids.size() > 1 ? manifest.id : process.id;
    return process.display_name + " (" + suffix + ")";
}

bool validateDeclaredPackageArtifacts(const ExtensionManifest& manifest, std::string* error)
{
    const std::filesystem::path package_root(manifest.package_root);
    std::error_code ec;

    auto validate_relative_path = [&](const std::string& relative_path, const std::string& field_name,
                                      const std::optional<bool> require_directory = std::nullopt) -> bool {
        const std::filesystem::path path = package_root / relative_path;
        if (!std::filesystem::exists(path, ec))
        {
            if (error)
            {
                *error = field_name + " points to a missing package artifact";
            }
            return false;
        }

        if (require_directory.has_value())
        {
            const bool is_directory = std::filesystem::is_directory(path, ec);
            if (is_directory != *require_directory)
            {
                if (error)
                {
                    *error = field_name + (*require_directory ? " must point to a package directory"
                                                              : " must point to a package file");
                }
                return false;
            }
        }

        return true;
    };

    if (!manifest.dependencies.requirements_file.empty() &&
        !validate_relative_path(manifest.dependencies.requirements_file, "dependencies.requirements_file", false))
    {
        return false;
    }

    for (const std::string& path : manifest.binary_vendor_paths)
    {
        if (!validate_relative_path(path, "binary_vendor_paths", true))
        {
            return false;
        }
    }

    for (const std::string& path : manifest.dependencies.pip.find_links)
    {
        if (isUrlLike(path))
        {
            continue;
        }

        if (!validate_relative_path(path, "dependencies.pip.find_links", true))
        {
            return false;
        }
    }

    if (!manifest.repository_metadata.icon_path.empty() &&
        !validate_relative_path(manifest.repository_metadata.icon_path, "icon"))
    {
        return false;
    }

    for (const std::string& path : manifest.repository_metadata.screenshots)
    {
        if (!validate_relative_path(path, "screenshots"))
        {
            return false;
        }
    }

    if (!manifest.binary_vendor_paths.empty() && manifest.runtime_targets.empty())
    {
        if (error)
        {
            *error = "binary_vendor_paths requires runtime_targets for platform-specific helper artifacts";
        }
        return false;
    }

    std::vector<std::filesystem::path> allowed_shared_library_roots;
    for (const std::string& path : manifest.binary_vendor_paths)
    {
        allowed_shared_library_roots.push_back((package_root / path).lexically_normal());
    }
    for (const std::string& path : manifest.dependencies.pip.find_links)
    {
        if (!isUrlLike(path))
        {
            allowed_shared_library_roots.push_back((package_root / path).lexically_normal());
        }
    }

    ec.clear();
    for (std::filesystem::recursive_directory_iterator it(package_root, ec), end; it != end && !ec; it.increment(ec))
    {
        if (ec || !it->is_regular_file())
        {
            continue;
        }

        const auto& file = it->path();
        if (!isSharedLibraryArtifact(file))
        {
            continue;
        }

        const bool allowed =
            std::any_of(allowed_shared_library_roots.begin(), allowed_shared_library_roots.end(),
                        [&](const std::filesystem::path& root) { return isPathWithinRoot(file, root); });
        if (allowed)
        {
            continue;
        }

        if (error)
        {
            *error = "Package-local shared library '" + std::filesystem::relative(file, package_root).generic_string() +
                     "' is unsupported outside binary_vendor_paths or wheel sources";
        }
        return false;
    }

    return true;
}

bool parsePackageProvenanceReceipt(const std::filesystem::path& package_root, const ExtensionManifest& manifest,
                                   const ExtensionPackageSource discovered_source,
                                   ExtensionPackageProvenance& provenance, std::string* error)
{
    provenance = ExtensionPackageProvenance{};
    provenance.source = discovered_source;
    provenance.receipt_path = package_root / std::string(kPackageProvenanceReceiptName);

    std::error_code ec;
    if (!std::filesystem::exists(provenance.receipt_path, ec))
    {
        return true;
    }
    if (!std::filesystem::is_regular_file(provenance.receipt_path, ec))
    {
        if (error)
        {
            *error = "extension_package_receipt.json is not a regular file";
        }
        return false;
    }

    const std::string raw_json = readFileText(provenance.receipt_path);
    if (raw_json.empty())
    {
        if (error)
        {
            *error = "Failed to read extension_package_receipt.json";
        }
        return false;
    }

    QJsonParseError parse_error;
    const QJsonDocument document = QJsonDocument::fromJson(QByteArray::fromStdString(raw_json), &parse_error);
    if (parse_error.error != QJsonParseError::NoError || !document.isObject())
    {
        if (error)
        {
            *error = "extension_package_receipt.json is not valid JSON";
        }
        return false;
    }

    const QJsonObject object = document.object();
    int receipt_version = 0;
    if (!requireInt(object, QStringLiteral("receipt_version"), receipt_version, error))
    {
        return false;
    }
    if (receipt_version != 1)
    {
        if (error)
        {
            *error = "extension_package_receipt.json uses an unsupported receipt_version";
        }
        return false;
    }

    std::string receipt_extension_id;
    std::string receipt_extension_version;
    std::string source_tier;
    if (!requireString(object, QStringLiteral("extension_id"), receipt_extension_id, error) ||
        !requireString(object, QStringLiteral("extension_version"), receipt_extension_version, error) ||
        !requireString(object, QStringLiteral("source_tier"), source_tier, error) ||
        !parseOptionalStringField(object, QStringLiteral("source_repository_url"), provenance.source_repository_url,
                                  error) ||
        !parseOptionalStringField(object, QStringLiteral("local_package_path"), provenance.local_package_path, error) ||
        !parseOptionalStringField(object, QStringLiteral("installed_at_utc"), provenance.installed_at_utc, error) ||
        !parseOptionalStringField(object, QStringLiteral("archive_hash"), provenance.archive_hash_sha256, error) ||
        !parseOptionalStringField(object, QStringLiteral("selected_release_asset"), provenance.selected_release_asset,
                                  error) ||
        !parseOptionalStringField(object, QStringLiteral("signature_detail"), provenance.signature_detail, error) ||
        !parseOptionalStringField(object, QStringLiteral("signature_algorithm"), provenance.signature_algorithm,
                                  error) ||
        !parseOptionalStringField(object, QStringLiteral("signature_key_id"), provenance.signature_key_id, error) ||
        !parseOptionalStringField(object, QStringLiteral("host_bootstrap_fingerprint"),
                                  provenance.host_bootstrap_fingerprint, error))
    {
        return false;
    }

    if (receipt_extension_id != manifest.id || receipt_extension_version != manifest.version)
    {
        if (error)
        {
            *error = "extension_package_receipt.json does not match the loaded manifest identity";
        }
        return false;
    }

    const auto parsed_source = sourceFromId(source_tier);
    if (!parsed_source.has_value())
    {
        if (error)
        {
            *error = "extension_package_receipt.json source_tier is unsupported";
        }
        return false;
    }
    if (*parsed_source != discovered_source)
    {
        if (error)
        {
            *error = "extension_package_receipt.json source_tier does not match the discovery source";
        }
        return false;
    }

    if (!provenance.source_repository_url.empty() && !isUrlLike(provenance.source_repository_url))
    {
        if (error)
        {
            *error = "extension_package_receipt.json source_repository_url must be an http(s) URL";
        }
        return false;
    }
    if (!provenance.archive_hash_sha256.empty() && !isHexSha256(provenance.archive_hash_sha256))
    {
        if (error)
        {
            *error = "extension_package_receipt.json archive_hash must be a SHA-256 hex digest";
        }
        return false;
    }

    std::string signature_state;
    if (!parseOptionalStringField(object, QStringLiteral("signature_state"), signature_state, error))
    {
        return false;
    }
    if (!signature_state.empty())
    {
        const auto parsed_signature_state = signatureStateFromId(signature_state);
        if (!parsed_signature_state.has_value())
        {
            if (error)
            {
                *error = "extension_package_receipt.json signature_state must be one of: unknown, unsigned, verified, "
                         "failed";
            }
            return false;
        }
        provenance.signature_state = *parsed_signature_state;
    }

    return true;
}

bool parseTopLevelMetadata(const QJsonObject& root, const std::filesystem::path& manifest_path,
                           ExtensionManifest& manifest, std::string* error)
{
    if (!rejectManifestBufferRoleField(root, "", error))
    {
        return false;
    }

    if (!requireString(root, QStringLiteral("id"), manifest.id, error) ||
        !requireString(root, QStringLiteral("name"), manifest.name, error) ||
        !requireString(root, QStringLiteral("version"), manifest.version, error) ||
        !requireString(root, QStringLiteral("entry_point"), manifest.entry_point, error) ||
        !requireString(root, QStringLiteral("category"), manifest.category, error))
    {
        return false;
    }

    manifest.package_root = manifest_path.parent_path().string();
    manifest.manifest_path = manifest_path.string();

    std::string type;
    if (!requireString(root, QStringLiteral("type"), type, error))
    {
        return false;
    }

    if (!isSafeModuleName(manifest.id))
    {
        if (error)
        {
            *error = "id must be a safe extension identifier";
        }
        return false;
    }

    if (type != "python")
    {
        if (error)
        {
            *error = "Only Python extensions are supported";
        }
        return false;
    }

    if (!isSafeModuleName(manifest.entry_point))
    {
        if (error)
        {
            *error = "entry_point must be a safe Python module name";
        }
        return false;
    }

    if (!isSupportedCategory(manifest.category))
    {
        if (error)
        {
            *error = "Manifest category is not supported by the host";
        }
        return false;
    }

    int package_format_version = 0;
    int protocol_version = 0;
    int sdk_version = 0;
    if (!requireInt(root, QStringLiteral("package_format_version"), package_format_version, error) ||
        !requireInt(root, QStringLiteral("protocol_version"), protocol_version, error) ||
        !requireInt(root, QStringLiteral("sdk_version"), sdk_version, error))
    {
        return false;
    }

    if (package_format_version != 1 || protocol_version != 1 || sdk_version != 1)
    {
        if (error)
        {
            *error = "Only package/protocol/sdk version 1 manifests are supported";
        }
        return false;
    }

    manifest.python_compat =
        root.value(QStringLiteral("python_compat")).toString(QStringLiteral(">=3.14,<4")).toStdString();
    if (!parseOptionalStringField(root, QStringLiteral("min_app_version"), manifest.min_app_version, error) ||
        !parseOptionalBoolField(root, QStringLiteral("requires_exclusive_access"), &manifest.requires_exclusive_access,
                                error) ||
        !parseOptionalBoolField(root, QStringLiteral("development_only"), &manifest.development_only, error))
    {
        return false;
    }

    const std::string launch_mode = root.value(QStringLiteral("launch_mode")).toString().toStdString();
    if (launch_mode == "single_image")
    {
        manifest.launch_mode = ExtensionLaunchMode::SingleImage;
    }
    else if (launch_mode == "workflow")
    {
        manifest.launch_mode = ExtensionLaunchMode::Workflow;
    }
    else
    {
        if (error)
        {
            *error = "launch_mode must be 'single_image' or 'workflow'";
        }
        return false;
    }

    const std::string sdk_backend =
        root.value(QStringLiteral("sdk_backend")).toString(QStringLiteral("runtime")).toStdString();
    if (sdk_backend == "runtime")
    {
        manifest.sdk_backend = ExtensionSdkBackend::Runtime;
    }
    else if (sdk_backend == "protocol")
    {
        manifest.sdk_backend = ExtensionSdkBackend::Protocol;
    }
    else if (sdk_backend == "rpc")
    {
        manifest.sdk_backend = ExtensionSdkBackend::Rpc;
    }
    else
    {
        if (error)
        {
            *error = "sdk_backend must be 'runtime', 'protocol', or 'rpc'";
        }
        return false;
    }

    if (!parseRuntimeTargets(root, manifest.runtime_targets, error) ||
        !parseBinaryVendorPaths(root, manifest.binary_vendor_paths, error) ||
        !parseRepositoryMetadata(root, manifest.repository_metadata, error) ||
        !parseDependencies(root, manifest.dependencies, error))
    {
        return false;
    }

    return true;
}

bool parseSingleProcessManifest(const QJsonObject& root, const ExtensionManifest& manifest,
                                std::vector<ExtensionProcessManifest>& out_processes, std::string* error)
{
    if (root.contains(QStringLiteral("processes")))
    {
        if (error)
        {
            *error = "Manifest cannot define both process_class and processes";
        }
        return false;
    }

    std::string process_class;
    if (!requireString(root, QStringLiteral("process_class"), process_class, error))
    {
        return false;
    }
    if (!isSafeModuleName(process_class))
    {
        if (error)
        {
            *error = "process_class must be a safe Python class name";
        }
        return false;
    }

    ExtensionProcessManifest process;
    process.id = manifest.id;
    process.class_name = process_class;
    process.display_name = manifest.name;
    process.process_name = process.display_name;
    process.action_object_name = an::ui::extensions::default_extension_action_object_name(manifest.id);
    process.category = manifest.category;
    if (!parseCapabilities(root.value(QStringLiteral("capabilities")), &process.capabilities, error))
    {
        return false;
    }

    out_processes.push_back(std::move(process));
    return true;
}

bool parseMultiProcessManifest(const QJsonObject& root, const ExtensionManifest& manifest,
                               std::vector<ExtensionProcessManifest>& out_processes, std::string* error)
{
    if (root.contains(QStringLiteral("capabilities")))
    {
        if (error)
        {
            *error = "Top-level capabilities are not allowed when processes[] is used";
        }
        return false;
    }

    const QJsonValue processes_value = root.value(QStringLiteral("processes"));
    if (!processes_value.isArray())
    {
        if (error)
        {
            *error = "processes must be an array";
        }
        return false;
    }

    const QJsonArray processes = processes_value.toArray();
    if (processes.isEmpty())
    {
        if (error)
        {
            *error = "processes must not be empty";
        }
        return false;
    }

    std::set<std::string> seen_ids;
    for (const QJsonValue& value : processes)
    {
        if (!value.isObject())
        {
            if (error)
            {
                *error = "Each processes[] entry must be an object";
            }
            return false;
        }

        const QJsonObject object = value.toObject();
        if (!rejectManifestBufferRoleField(object, "processes[].", error))
        {
            return false;
        }

        ExtensionProcessManifest process;
        if (!requireString(object, QStringLiteral("id_suffix"), process.id_suffix, error) ||
            !requireString(object, QStringLiteral("class"), process.class_name, error) ||
            !requireString(object, QStringLiteral("display_name"), process.display_name, error))
        {
            return false;
        }

        if (!isSafeModuleName(process.id_suffix))
        {
            if (error)
            {
                *error = "processes[] id_suffix must be a safe identifier";
            }
            return false;
        }
        if (!isSafeModuleName(process.class_name))
        {
            if (error)
            {
                *error = "processes[] class must be a safe Python class name";
            }
            return false;
        }
        if (process.display_name.empty())
        {
            if (error)
            {
                *error = "processes[] display_name must not be empty";
            }
            return false;
        }

        process.category =
            object.value(QStringLiteral("category")).toString(QString::fromStdString(manifest.category)).toStdString();
        if (!isSupportedCategory(process.category))
        {
            if (error)
            {
                *error = "processes[] entry uses an unsupported category";
            }
            return false;
        }

        process.id = manifest.id + "." + process.id_suffix;
        if (!seen_ids.insert(process.id).second)
        {
            if (error)
            {
                *error = "processes[] contains duplicate id_suffix values";
            }
            return false;
        }

        process.process_name = object.value(QStringLiteral("process_name"))
                                   .toString(QString::fromStdString(process.display_name))
                                   .toStdString();
        process.action_object_name =
            object.value(QStringLiteral("action_object_name"))
                .toString(QString::fromStdString(
                    an::ui::extensions::default_extension_action_object_name(manifest.id, process.id_suffix)))
                .toStdString();
        if (!parseCapabilities(object.value(QStringLiteral("capabilities")), &process.capabilities, error))
        {
            return false;
        }

        out_processes.push_back(std::move(process));
    }

    return true;
}

bool normalizeProcesses(const QJsonObject& root, ExtensionManifest& manifest, std::string* error)
{
    std::vector<ExtensionProcessManifest> processes;
    if (root.contains(QStringLiteral("process_class")))
    {
        if (!parseSingleProcessManifest(root, manifest, processes, error))
        {
            return false;
        }
    }
    else if (root.contains(QStringLiteral("processes")))
    {
        if (!parseMultiProcessManifest(root, manifest, processes, error))
        {
            return false;
        }
    }
    else
    {
        if (error)
        {
            *error = "Manifest must define process_class or processes";
        }
        return false;
    }

    manifest.processes = std::move(processes);
    return true;
}

void applyCompatibilityStatus(ExtensionLoadResult& result, const ExtensionDiscoveryContext& context)
{
    if (!result.manifest)
    {
        return;
    }

    const ExtensionManifest& manifest = *result.manifest;
    const std::string current_app_version =
        context.app_version.empty() ? currentAfterNightVersion() : context.app_version;
    if (requiresNewerAfterNightVersion(manifest.min_app_version, current_app_version))
    {
        result.status = ExtensionLoadStatus::Incompatible;
        result.diagnostic = minimumAfterNightVersionDiagnostic(manifest.min_app_version, current_app_version);
        return;
    }

    std::string compat_error;
    if (!evaluatePythonCompat(manifest.python_compat, context.bundled_python_version, &compat_error))
    {
        result.status = ExtensionLoadStatus::Incompatible;
        if (!compat_error.empty())
        {
            result.diagnostic = compat_error;
        }
        else
        {
            result.diagnostic = "Extension is incompatible with the bundled Python runtime.";
        }
        return;
    }

    if (!manifest.runtime_targets.empty() && std::find(manifest.runtime_targets.begin(), manifest.runtime_targets.end(),
                                                       context.runtime_target) == manifest.runtime_targets.end())
    {
        result.status = ExtensionLoadStatus::Incompatible;
        result.diagnostic = "Extension does not support the current runtime target.";
        return;
    }

    if (manifest.launch_mode == ExtensionLaunchMode::Workflow && !context.workflow_launch_mode_available)
    {
        result.status = ExtensionLoadStatus::Incompatible;
        result.diagnostic = kWorkflowLaunchUnavailableReason.data();
        return;
    }

    switch (manifest.sdk_backend)
    {
    case ExtensionSdkBackend::Runtime:
        if (!context.runtime_backend_available)
        {
            result.status = ExtensionLoadStatus::Incompatible;
            result.diagnostic = unavailableBackendDiagnostic(manifest.sdk_backend);
            return;
        }
        break;
    case ExtensionSdkBackend::Protocol:
        if (!context.protocol_backend_available)
        {
            result.status = ExtensionLoadStatus::Incompatible;
            result.diagnostic = unavailableBackendDiagnostic(manifest.sdk_backend);
            return;
        }
        break;
    case ExtensionSdkBackend::Rpc:
        if (!context.rpc_backend_available)
        {
            result.status = ExtensionLoadStatus::Incompatible;
            result.diagnostic = unavailableBackendDiagnostic(manifest.sdk_backend);
            return;
        }
        break;
    }

    result.status = ExtensionLoadStatus::Ready;
    result.diagnostic.clear();
}

[[nodiscard]] ExtensionLoadResult loadManifestForStartupActivation(const std::filesystem::path& manifest_path,
                                                                   const ExtensionPackageSource source,
                                                                   const ExtensionDiscoveryContext& context)
{
    ExtensionLoadResult result;
    result.manifest_path = manifest_path;
    result.package_root = manifest_path.parent_path();
    result.source = source;
    result.provenance.source = source;
    result.provenance.receipt_path = result.package_root / std::string(kPackageProvenanceReceiptName);

    const std::string raw_json = readFileText(manifest_path);
    if (raw_json.empty())
    {
        result.diagnostic = "Failed to read extension.json";
        return result;
    }

    QJsonParseError parse_error;
    const QJsonDocument document = QJsonDocument::fromJson(QByteArray::fromStdString(raw_json), &parse_error);
    if (parse_error.error != QJsonParseError::NoError || !document.isObject())
    {
        result.diagnostic = "extension.json is not valid JSON";
        return result;
    }

    ExtensionManifest manifest;
    std::string error;
    const QJsonObject root = document.object();
    if (!parseTopLevelMetadata(root, manifest_path, manifest, &error) || !normalizeProcesses(root, manifest, &error) ||
        !validateDeclaredPackageArtifacts(manifest, &error))
    {
        result.diagnostic = std::move(error);
        return result;
    }

    result.manifest = std::move(manifest);
    applyCompatibilityStatus(result, context);
    return result;
}

[[nodiscard]] ExtensionLoadResult
loadExtensionManifestMetadata(const std::filesystem::path& manifest_path, const ExtensionPackageSource source,
                              const ExtensionDiscoveryContext& context,
                              const ExtensionOperationCancellationToken& cancellation_token)
{
    if (cancellationRequested(cancellation_token))
    {
        ExtensionLoadResult result;
        result.manifest_path = manifest_path;
        result.package_root = manifest_path.parent_path();
        result.source = source;
        result.provenance.source = source;
        result.provenance.receipt_path = result.package_root / std::string(kPackageProvenanceReceiptName);
        result.status = ExtensionLoadStatus::Invalid;
        result.diagnostic = "Extension manifest loading cancelled.";
        return result;
    }

    ExtensionLoadResult result = loadManifestForStartupActivation(manifest_path, source, context);
    if (!result.manifest)
    {
        return result;
    }

    std::string error;
    if (!parsePackageProvenanceReceipt(result.package_root, *result.manifest, source, result.provenance, &error))
    {
        result.status = ExtensionLoadStatus::Invalid;
        result.diagnostic = std::move(error);
        result.manifest.reset();
    }
    return result;
}

void applyDuplicatePrecedence(std::vector<ExtensionLoadResult>& results)
{
    std::map<std::string, std::size_t, std::less<>> winner_by_id;

    for (std::size_t i = 0; i < results.size(); ++i)
    {
        ExtensionLoadResult& result = results[i];
        if (!result.manifest)
        {
            continue;
        }

        const std::string& id = result.manifest->id;
        const auto it = winner_by_id.find(id);
        if (it == winner_by_id.end())
        {
            winner_by_id.emplace(id, i);
            continue;
        }

        ExtensionLoadResult& winner = results[it->second];
        if (hasHigherPriority(result.source, winner.source))
        {
            winner.status = ExtensionLoadStatus::Invalid;
            winner.diagnostic = "A higher-precedence extension package with the same id was discovered.";
            winner.manifest.reset();
            winner_by_id[id] = i;
            continue;
        }

        result.status = ExtensionLoadStatus::Invalid;
        result.diagnostic = "A higher-precedence extension package with the same id was discovered.";
        result.manifest.reset();
    }
}

void applySharedDependencyContextConsistency(std::vector<ExtensionLoadResult>& results)
{
    std::map<std::pair<std::string, std::string>, std::set<std::string>, std::less<>> fingerprints_by_context;
    std::map<std::pair<std::string, std::string>, std::vector<std::size_t>, std::less<>> indices_by_context;

    const auto shared_context_key =
        [](const ExtensionDependencySpec& dependencies) -> std::optional<std::pair<std::string, std::string>> {
        switch (dependencies.dependency_context)
        {
        case ExtensionDependencyContext::SharedGroup:
            if (!dependencies.shared_group.empty())
            {
                return std::pair{std::string("shared_group"), dependencies.shared_group};
            }
            break;
        case ExtensionDependencyContext::SharedHost:
            if (!dependencies.shared_host_profile.empty())
            {
                return std::pair{std::string("shared_host_profile"), dependencies.shared_host_profile};
            }
            break;
        case ExtensionDependencyContext::Private:
            break;
        }

        return std::nullopt;
    };

    for (std::size_t i = 0; i < results.size(); ++i)
    {
        const ExtensionLoadResult& result = results[i];
        if (!result.manifest || !result.environment || result.status == ExtensionLoadStatus::Invalid)
        {
            continue;
        }

        const auto shared_context = shared_context_key(result.manifest->dependencies);
        if (!shared_context.has_value())
        {
            continue;
        }

        fingerprints_by_context[*shared_context].insert(result.environment->dependency_fingerprint);
        indices_by_context[*shared_context].push_back(i);
    }

    for (const auto& [shared_context, fingerprints] : fingerprints_by_context)
    {
        if (fingerprints.size() < 2)
        {
            continue;
        }

        const auto& [context_kind, context_name] = shared_context;

        const std::string diagnostic =
            "Extensions in " + context_kind + " '" + context_name +
            "' declare conflicting dependency fingerprints and cannot share one environment.";

        if (an::platform::isLoggingInitialized())
        {
            LOG_WARNING("{} fingerprints={}", diagnostic, fingerprints.size());
        }

        for (const std::size_t index : indices_by_context[shared_context])
        {
            ExtensionLoadResult& result = results[index];
            result.status = ExtensionLoadStatus::Incompatible;
            result.diagnostic = diagnostic;
        }
    }
}

[[nodiscard]] std::vector<std::filesystem::path>
discoverManifestPaths(const std::vector<std::filesystem::path>& roots,
                      const ExtensionOperationCancellationToken& cancellation_token)
{
    std::vector<std::filesystem::path> manifest_paths;

    for (const std::filesystem::path& root : roots)
    {
        if (cancellationRequested(cancellation_token))
        {
            return {};
        }

        std::error_code ec;
        if (!std::filesystem::exists(root, ec) || !std::filesystem::is_directory(root, ec))
        {
            continue;
        }

        for (const auto& entry : std::filesystem::directory_iterator(root, ec))
        {
            if (cancellationRequested(cancellation_token))
            {
                return {};
            }

            if (ec || !entry.is_directory())
            {
                continue;
            }

            const auto manifest_path = entry.path() / "extension.json";
            if (std::filesystem::exists(manifest_path, ec) && std::filesystem::is_regular_file(manifest_path, ec))
            {
                manifest_paths.push_back(manifest_path);
            }
        }
    }

    std::sort(manifest_paths.begin(), manifest_paths.end());
    return manifest_paths;
}

void addValidationIssue(ExtensionPackageValidationResult& result, const ExtensionPackageValidationSeverity severity,
                        std::string code, std::string message, std::filesystem::path path = {})
{
    result.issues.push_back(
        ExtensionPackageValidationIssue{severity, std::move(code), std::move(message), std::move(path)});
}

[[nodiscard]] bool hasValidationError(const ExtensionPackageValidationResult& result)
{
    return std::any_of(result.issues.begin(), result.issues.end(),
                       [](const auto& issue) { return issue.severity == ExtensionPackageValidationSeverity::Error; });
}

void addIndexValidationIssue(ExtensionRepositoryIndexValidationResult& result,
                             const ExtensionPackageValidationSeverity severity, std::string code, std::string message,
                             std::filesystem::path path = {})
{
    if (result.diagnostic.empty())
    {
        result.diagnostic = message;
    }
    result.issues.push_back(
        ExtensionPackageValidationIssue{severity, std::move(code), std::move(message), std::move(path)});
}

[[nodiscard]] bool hasIndexValidationError(const ExtensionRepositoryIndexValidationResult& result)
{
    return std::any_of(result.issues.begin(), result.issues.end(),
                       [](const auto& issue) { return issue.severity == ExtensionPackageValidationSeverity::Error; });
}

[[nodiscard]] std::filesystem::path modulePathFromEntryPoint(const std::string& entry_point)
{
    std::filesystem::path module_path;
    for (const std::string& part : splitString(entry_point, '.'))
    {
        module_path /= part;
    }
    return module_path;
}

[[nodiscard]] bool packageContainsEntryPoint(const ExtensionManifest& manifest)
{
    const std::filesystem::path package_root(manifest.package_root);
    const std::filesystem::path module_path = modulePathFromEntryPoint(manifest.entry_point);

    std::filesystem::path module_file = package_root / module_path;
    module_file += ".py";

    std::error_code ec;
    if (std::filesystem::exists(module_file, ec) && std::filesystem::is_regular_file(module_file, ec))
    {
        return true;
    }

    const std::filesystem::path package_init = package_root / module_path / "__init__.py";
    return std::filesystem::exists(package_init, ec) && std::filesystem::is_regular_file(package_init, ec);
}

[[nodiscard]] bool isAllZeroBlock(const std::array<char, kTarBlockSize>& block)
{
    return std::all_of(block.begin(), block.end(), [](const char ch) { return ch == '\0'; });
}

[[nodiscard]] std::string tarStringField(const char* data, const std::size_t length)
{
    const char* end = static_cast<const char*>(std::memchr(data, '\0', length));
    return std::string(data, end ? end : data + length);
}

[[nodiscard]] std::optional<std::uint64_t> parseTarOctalField(const char* data, const std::size_t length)
{
    std::uint64_t value = 0;
    bool saw_digit = false;
    for (std::size_t i = 0; i < length; ++i)
    {
        const unsigned char ch = static_cast<unsigned char>(data[i]);
        if (ch == '\0' || ch == ' ')
        {
            continue;
        }
        if (ch < '0' || ch > '7')
        {
            return std::nullopt;
        }

        saw_digit = true;
        value = (value * 8u) + static_cast<std::uint64_t>(ch - '0');
    }

    return saw_digit ? std::optional<std::uint64_t>(value) : std::optional<std::uint64_t>(0);
}

[[nodiscard]] std::optional<std::map<std::string, std::string>> parsePaxKeyValueRecords(const std::string& payload)
{
    std::map<std::string, std::string> records;
    std::size_t offset = 0;
    while (offset < payload.size())
    {
        const std::size_t space = payload.find(' ', offset);
        if (space == std::string::npos || space == offset)
        {
            return std::nullopt;
        }

        std::uint64_t record_length = 0;
        for (std::size_t i = offset; i < space; ++i)
        {
            const unsigned char ch = static_cast<unsigned char>(payload[i]);
            if (!std::isdigit(ch))
            {
                return std::nullopt;
            }
            record_length = (record_length * 10u) + static_cast<std::uint64_t>(ch - '0');
        }

        if (record_length == 0u || record_length > payload.size() - offset)
        {
            return std::nullopt;
        }

        const std::size_t record_start = space + 1u;
        const std::size_t record_end = offset + static_cast<std::size_t>(record_length);
        if (record_start >= record_end)
        {
            return std::nullopt;
        }

        std::string record = payload.substr(record_start, record_end - record_start);
        if (!record.empty() && record.back() == '\n')
        {
            record.pop_back();
        }

        const std::size_t equals = record.find('=');
        if (equals == std::string::npos || equals == 0u)
        {
            return std::nullopt;
        }
        records[record.substr(0, equals)] = record.substr(equals + 1u);
        offset = record_end;
    }

    return records;
}

[[nodiscard]] std::size_t pathDepth(const std::filesystem::path& relative_path)
{
    return static_cast<std::size_t>(
        std::count_if(relative_path.begin(), relative_path.end(), [](const auto& part) { return part != "."; }));
}

[[nodiscard]] bool isArchivePathSafe(const std::string& raw_path, const std::size_t max_depth,
                                     std::filesystem::path* out_path, std::string* error)
{
    if (raw_path.empty())
    {
        if (error)
        {
            *error = "Archive entry path is empty";
        }
        return false;
    }
    if (raw_path.find('\\') != std::string::npos || raw_path.find(':') != std::string::npos)
    {
        if (error)
        {
            *error = "Archive entry path must be relative and must not contain unsupported separators or drive syntax";
        }
        return false;
    }

    const std::filesystem::path path(raw_path);
    if (path.is_absolute())
    {
        if (error)
        {
            *error = "Archive entry path must be relative";
        }
        return false;
    }

    std::filesystem::path normalized;
    for (const auto& part : path)
    {
        if (part == "." || part.empty())
        {
            continue;
        }
        if (part == "..")
        {
            if (error)
            {
                *error = "Archive entry path attempts to escape the package root";
            }
            return false;
        }
        normalized /= part;
    }

    if (normalized.empty())
    {
        if (error)
        {
            *error = "Archive entry path normalizes to an empty path";
        }
        return false;
    }
    if (pathDepth(normalized) > max_depth)
    {
        if (error)
        {
            *error = "Archive entry path exceeds the maximum supported depth";
        }
        return false;
    }

    if (out_path)
    {
        *out_path = normalized;
    }
    return true;
}

[[nodiscard]] bool isDirectoryEmpty(const std::filesystem::path& path)
{
    std::error_code ec;
    if (!std::filesystem::exists(path, ec))
    {
        return true;
    }
    if (!std::filesystem::is_directory(path, ec))
    {
        return false;
    }

    return std::filesystem::directory_iterator(path, ec) == std::filesystem::directory_iterator();
}

[[nodiscard]] bool writeTextFileAtomic(const std::filesystem::path& path, const std::string& contents,
                                       std::string* error)
{
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec)
    {
        if (error)
        {
            *error = "Failed to create destination directory";
        }
        return false;
    }

    QSaveFile file(QString::fromStdString(path.string()));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        if (error)
        {
            *error = "Failed to open file for atomic write";
        }
        return false;
    }

    const QByteArray payload(contents.data(), static_cast<QByteArray::size_type>(contents.size()));
    if (file.write(payload) != payload.size() || !file.commit())
    {
        if (error)
        {
            *error = "Failed to commit atomic file write";
        }
        return false;
    }

    return true;
}

[[nodiscard]] std::filesystem::perms permissionsFromTarMode(const std::uint64_t mode)
{
    namespace fs = std::filesystem;
    fs::perms permissions =
        fs::perms::owner_read | fs::perms::owner_write | fs::perms::group_read | fs::perms::others_read;

    if ((mode & 0100u) != 0u)
    {
        permissions |= fs::perms::owner_exec;
    }
    if ((mode & 0010u) != 0u)
    {
        permissions |= fs::perms::group_exec;
    }
    if ((mode & 0001u) != 0u)
    {
        permissions |= fs::perms::others_exec;
    }

    return permissions;
}

[[nodiscard]] bool skipTarPadding(std::ifstream& archive, const std::uint64_t size)
{
    const std::uint64_t padding = (kTarBlockSize - (size % kTarBlockSize)) % kTarBlockSize;
    if (padding == 0)
    {
        return true;
    }

    archive.seekg(static_cast<std::streamoff>(padding), std::ios::cur);
    return archive.good();
}

[[nodiscard]] bool readTarPayloadString(std::ifstream& archive, const std::uint64_t size, std::string& payload)
{
    if (size > static_cast<std::uint64_t>((std::numeric_limits<std::size_t>::max)()))
    {
        return false;
    }

    payload.assign(static_cast<std::size_t>(size), '\0');
    archive.read(payload.data(), static_cast<std::streamsize>(payload.size()));
    return archive.gcount() == static_cast<std::streamsize>(payload.size());
}

[[nodiscard]] bool isTarArchivePath(const std::filesystem::path& archive_path)
{
    return toLowerCopy(archive_path.extension().string()) == ".tar";
}

[[nodiscard]] bool isTarZstdArchivePath(const std::filesystem::path& archive_path)
{
    const std::string extension = toLowerCopy(archive_path.extension().string());
    if (extension == ".tzst")
    {
        return true;
    }
    if (extension != ".zst")
    {
        return false;
    }

    return toLowerCopy(archive_path.stem().extension().string()) == ".tar";
}

[[nodiscard]] std::uint64_t maxTemporaryTarBytes(const ExtensionPackageArchiveLimits& limits)
{
    const auto checkedAdd = [](const std::uint64_t lhs, const std::uint64_t rhs, std::uint64_t* value) {
        if (std::numeric_limits<std::uint64_t>::max() - lhs < rhs)
        {
            *value = std::numeric_limits<std::uint64_t>::max();
            return;
        }
        *value = lhs + rhs;
    };

    const std::uint64_t file_count = static_cast<std::uint64_t>(limits.max_file_count);
    std::uint64_t header_bytes = 0;
    checkedAdd(file_count, 2, &header_bytes);
    if (header_bytes <= std::numeric_limits<std::uint64_t>::max() / kTarBlockSize)
    {
        header_bytes *= kTarBlockSize;
    }
    else
    {
        header_bytes = std::numeric_limits<std::uint64_t>::max();
    }

    std::uint64_t padding_bytes = 0;
    if (file_count <= std::numeric_limits<std::uint64_t>::max() / kMaxTarPaddingBytesPerEntry)
    {
        padding_bytes = file_count * kMaxTarPaddingBytesPerEntry;
    }
    else
    {
        padding_bytes = std::numeric_limits<std::uint64_t>::max();
    }

    std::uint64_t total = limits.max_total_size_bytes;
    checkedAdd(total, header_bytes, &total);
    checkedAdd(total, padding_bytes, &total);
    return total;
}

struct ZstdDStreamDeleter
{
    void operator()(ZSTD_DStream* stream) const
    {
        ZSTD_freeDStream(stream);
    }
};

[[nodiscard]] bool decompressZstdArchiveToTar(const std::filesystem::path& archive_path,
                                              const std::filesystem::path& temporary_tar_path,
                                              const std::uint64_t max_decompressed_bytes, std::string* diagnostic)
{
    std::ifstream in(archive_path, std::ios::binary);
    if (!in.is_open())
    {
        *diagnostic = "Failed to open compressed package archive";
        return false;
    }

    std::ofstream out(temporary_tar_path, std::ios::binary);
    if (!out.is_open())
    {
        *diagnostic = "Failed to create temporary decompressed package archive";
        return false;
    }

    std::unique_ptr<ZSTD_DStream, ZstdDStreamDeleter> stream(ZSTD_createDStream());
    if (!stream)
    {
        *diagnostic = "Failed to create zstd decompressor";
        return false;
    }

    const std::size_t init_result = ZSTD_initDStream(stream.get());
    if (ZSTD_isError(init_result) != 0)
    {
        *diagnostic = std::string("Failed to initialize zstd decompressor: ") + ZSTD_getErrorName(init_result);
        return false;
    }

    std::vector<char> input_buffer(ZSTD_DStreamInSize());
    std::vector<char> output_buffer(ZSTD_DStreamOutSize());
    std::uint64_t written_bytes = 0;
    std::size_t frame_result = 1;
    bool saw_input = false;

    while (in.good())
    {
        in.read(input_buffer.data(), static_cast<std::streamsize>(input_buffer.size()));
        const std::streamsize bytes_read = in.gcount();
        if (bytes_read <= 0)
        {
            break;
        }
        saw_input = true;

        ZSTD_inBuffer input{input_buffer.data(), static_cast<std::size_t>(bytes_read), 0};
        while (input.pos < input.size)
        {
            const std::size_t previous_input_pos = input.pos;
            ZSTD_outBuffer output{output_buffer.data(), output_buffer.size(), 0};
            frame_result = ZSTD_decompressStream(stream.get(), &output, &input);
            if (ZSTD_isError(frame_result) != 0)
            {
                *diagnostic =
                    std::string("Failed to decompress zstd package archive: ") + ZSTD_getErrorName(frame_result);
                return false;
            }
            if (output.pos == 0)
            {
                if (input.pos == previous_input_pos)
                {
                    *diagnostic = "Failed to decompress zstd package archive";
                    return false;
                }
                continue;
            }
            if (output.pos > max_decompressed_bytes || written_bytes > max_decompressed_bytes - output.pos)
            {
                *diagnostic = "Compressed package archive expands beyond the extraction limit";
                return false;
            }
            out.write(output_buffer.data(), static_cast<std::streamsize>(output.pos));
            if (!out.good())
            {
                *diagnostic = "Failed to write temporary decompressed package archive";
                return false;
            }
            written_bytes += static_cast<std::uint64_t>(output.pos);
        }
    }

    if (!in.eof())
    {
        *diagnostic = "Failed while reading compressed package archive";
        return false;
    }
    if (!saw_input)
    {
        *diagnostic = "Compressed package archive is empty";
        return false;
    }
    if (frame_result != 0)
    {
        *diagnostic = "Compressed package archive ended before the zstd frame was complete";
        return false;
    }

    out.close();
    if (!out.good())
    {
        *diagnostic = "Failed to finalize temporary decompressed package archive";
        return false;
    }

    return true;
}

[[nodiscard]] std::optional<std::filesystem::path>
locateExtractedPackageRoot(const std::filesystem::path& extracted_root)
{
    std::error_code ec;
    const auto root_manifest = extracted_root / "extension.json";
    if (std::filesystem::is_regular_file(root_manifest, ec))
    {
        return extracted_root;
    }

    std::optional<std::filesystem::path> package_root;
    for (const auto& entry : std::filesystem::directory_iterator(extracted_root, ec))
    {
        if (ec || !entry.is_directory())
        {
            continue;
        }

        const auto manifest_path = entry.path() / "extension.json";
        if (!std::filesystem::is_regular_file(manifest_path, ec))
        {
            continue;
        }
        if (package_root.has_value())
        {
            return std::nullopt;
        }
        package_root = entry.path();
    }

    return package_root;
}

[[nodiscard]] bool isRepositoryDownloadReferenceAllowed(const std::string& value)
{
    return isUrlLike(value) || value.rfind("file://", 0) == 0 || isSafeRelativePathString(value);
}

bool parseRequiredHashField(const QJsonObject& object, const QString& key, std::string& out_hash, std::string* error)
{
    std::string raw_hash;
    if (!requireString(object, key, raw_hash, error))
    {
        return false;
    }

    const auto parsed = normalizeSha256HexDigest(raw_hash);
    if (!parsed.has_value())
    {
        if (error)
        {
            *error = key.toStdString() + " must be a SHA-256 digest, optionally prefixed by sha256:";
        }
        return false;
    }

    out_hash = *parsed;
    return true;
}

bool parseRepositoryReleaseAsset(const QJsonObject& object, const std::vector<std::string>& release_runtime_targets,
                                 ExtensionRepositoryReleaseAsset& asset,
                                 const std::vector<ExtensionSigningKey>& trusted_keys, std::string* error)
{
    if (!requireString(object, QStringLiteral("name"), asset.name, error) ||
        !requireString(object, QStringLiteral("download_url"), asset.download_url, error) ||
        !parseRequiredHashField(object, QStringLiteral("package_hash"), asset.package_hash_sha256, error) ||
        !parseRuntimeTargets(object, asset.runtime_targets, error) ||
        !parseOptionalStringField(object, QStringLiteral("signature_detail"), asset.signature_detail, error) ||
        !parseOptionalStringField(object, QStringLiteral("signature_algorithm"), asset.signature_algorithm, error) ||
        !parseOptionalStringField(object, QStringLiteral("signature_key_id"), asset.signature_key_id, error) ||
        !parseOptionalStringField(object, QStringLiteral("signature"), asset.signature, error))
    {
        return false;
    }

    if (asset.name.empty())
    {
        if (error)
        {
            *error = "release asset name must not be empty";
        }
        return false;
    }
    if (!isRepositoryDownloadReferenceAllowed(asset.download_url))
    {
        if (error)
        {
            *error = "release asset download_url must be http(s), file://, or a safe relative fixture path";
        }
        return false;
    }
    if (asset.runtime_targets.empty())
    {
        asset.runtime_targets = release_runtime_targets;
    }
    if (asset.runtime_targets.empty())
    {
        if (error)
        {
            *error = "release asset runtime_targets must be declared or inherited from the release";
        }
        return false;
    }

    std::string signature_state;
    if (!parseOptionalStringField(object, QStringLiteral("signature_state"), signature_state, error))
    {
        return false;
    }
    if (signature_state.empty())
    {
        signature_state = "unsigned";
    }
    const auto parsed_signature_state = signatureStateFromId(signature_state);
    if (!parsed_signature_state.has_value())
    {
        if (error)
        {
            *error = "release asset signature_state must be one of: unknown, unsigned, verified, failed";
        }
        return false;
    }
    asset.signature_state = *parsed_signature_state;

    if (asset.signature_state == ExtensionPackageSignatureState::Verified)
    {
        if (asset.signature_algorithm != kSignatureAlgorithmEd25519)
        {
            if (error)
            {
                *error = "verified release asset signature_algorithm must be ed25519";
            }
            return false;
        }
        if (asset.signature_key_id.empty())
        {
            if (error)
            {
                *error = "verified release asset signature_key_id is required";
            }
            return false;
        }
        if (!trustedSigningPublicKey(asset.signature_key_id, trusted_keys).has_value())
        {
            if (error)
            {
                *error = "verified release asset signature_key_id is not trusted by this build";
            }
            return false;
        }
        if (asset.signature.empty())
        {
            if (error)
            {
                *error = "verified release asset signature is required";
            }
            return false;
        }
        std::string signature_error;
        if (!decodeBase64Exact(asset.signature, crypto_sign_BYTES, "release asset signature", &signature_error)
                 .has_value())
        {
            if (error)
            {
                *error = signature_error;
            }
            return false;
        }
    }
    else if (!asset.signature_algorithm.empty() || !asset.signature_key_id.empty() || !asset.signature.empty())
    {
        if (error)
        {
            *error = "only verified release assets may declare signature_algorithm, signature_key_id, or signature";
        }
        return false;
    }

    return true;
}

bool parseRepositoryRelease(const QJsonObject& object, ExtensionRepositoryRelease& release,
                            const std::vector<ExtensionSigningKey>& trusted_keys, std::string* error)
{
    if (!requireString(object, QStringLiteral("version"), release.version, error) ||
        !parseOptionalStringField(object, QStringLiteral("min_app_version"), release.min_app_version, error) ||
        !requireInt(object, QStringLiteral("package_format_version"), release.package_format_version, error) ||
        !requireInt(object, QStringLiteral("protocol_version"), release.protocol_version, error) ||
        !requireInt(object, QStringLiteral("sdk_version"), release.sdk_version, error) ||
        !requireString(object, QStringLiteral("sdk_backend"), release.sdk_backend, error) ||
        !requireString(object, QStringLiteral("launch_mode"), release.launch_mode, error) ||
        !parseRuntimeTargets(object, release.runtime_targets, error) ||
        !parseOptionalStringField(object, QStringLiteral("changelog"), release.changelog, error) ||
        !parseOptionalStringField(object, QStringLiteral("published_at"), release.published_at_utc, error))
    {
        return false;
    }

    if (release.version.empty())
    {
        if (error)
        {
            *error = "release version must not be empty";
        }
        return false;
    }
    if (release.package_format_version != 1 || release.protocol_version != 1 || release.sdk_version != 1)
    {
        if (error)
        {
            *error = "repository releases must declare package/protocol/sdk version 1 for this host";
        }
        return false;
    }
    if (release.sdk_backend != "runtime" && release.sdk_backend != "protocol" && release.sdk_backend != "rpc")
    {
        if (error)
        {
            *error = "repository release sdk_backend must be runtime, protocol, or rpc";
        }
        return false;
    }
    if (release.launch_mode != "single_image" && release.launch_mode != "workflow")
    {
        if (error)
        {
            *error = "repository release launch_mode must be single_image or workflow";
        }
        return false;
    }
    if (release.runtime_targets.empty())
    {
        if (error)
        {
            *error = "repository releases must declare runtime_targets";
        }
        return false;
    }

    const QJsonValue assets_value = object.value(QStringLiteral("assets"));
    if (!assets_value.isArray() || assets_value.toArray().isEmpty())
    {
        if (error)
        {
            *error = "repository releases must declare a non-empty assets array";
        }
        return false;
    }

    std::set<std::string, std::less<>> asset_names;
    for (const QJsonValue& asset_value : assets_value.toArray())
    {
        if (!asset_value.isObject())
        {
            if (error)
            {
                *error = "repository release assets must be objects";
            }
            return false;
        }

        ExtensionRepositoryReleaseAsset asset;
        if (!parseRepositoryReleaseAsset(asset_value.toObject(), release.runtime_targets, asset, trusted_keys, error))
        {
            return false;
        }
        if (!asset_names.insert(asset.name).second)
        {
            if (error)
            {
                *error = "repository release asset names must be unique per release";
            }
            return false;
        }

        release.assets.push_back(std::move(asset));
    }

    return true;
}

bool parseRepositoryPackage(const QJsonObject& object, ExtensionRepositoryPackage& package,
                            const std::vector<ExtensionSigningKey>& trusted_keys, std::string* error)
{
    if (!requireString(object, QStringLiteral("id"), package.id, error) ||
        !requireString(object, QStringLiteral("name"), package.name, error) ||
        !requireString(object, QStringLiteral("summary"), package.summary, error) ||
        !requireString(object, QStringLiteral("description"), package.description, error) ||
        !requireString(object, QStringLiteral("author"), package.author, error) ||
        !requireString(object, QStringLiteral("license"), package.license, error) ||
        !requireString(object, QStringLiteral("latest_version"), package.latest_version, error) ||
        !parseOptionalStringField(object, QStringLiteral("publisher_id"), package.publisher_id, error) ||
        !parseOptionalStringField(object, QStringLiteral("attribution"), package.attribution, error) ||
        !parseOptionalStringField(object, QStringLiteral("original_author"), package.original_author, error) ||
        !parseOptionalStringField(object, QStringLiteral("original_project"), package.original_project, error) ||
        !parseOptionalStringField(object, QStringLiteral("original_source_url"), package.original_source_url, error) ||
        !parseOptionalStringField(object, QStringLiteral("upstream_commit"), package.upstream_commit, error) ||
        !parseOptionalStringField(object, QStringLiteral("homepage_url"), package.homepage_url, error) ||
        !parseOptionalStringField(object, QStringLiteral("repository_url"), package.repository_url, error) ||
        !parseOptionalStringField(object, QStringLiteral("support_url"), package.support_url, error) ||
        !parseOptionalStringField(object, QStringLiteral("icon_url"), package.icon_url, error) ||
        !parseStringArray(object, QStringLiteral("tags"), package.tags, error))
    {
        return false;
    }

    if (!isSafeModuleName(package.id))
    {
        if (error)
        {
            *error = "repository extension id must be a safe extension identifier";
        }
        return false;
    }
    if (package.name.empty() || package.summary.empty() || package.description.empty() || package.author.empty() ||
        package.license.empty() || package.latest_version.empty())
    {
        if (error)
        {
            *error = "repository package identity and metadata fields must not be empty";
        }
        return false;
    }
    if (!validateOptionalUrl(package.homepage_url, "homepage_url", error) ||
        !validateOptionalUrl(package.repository_url, "repository_url", error) ||
        !validateOptionalUrl(package.support_url, "support_url", error) ||
        !validateOptionalUrl(package.icon_url, "icon_url", error) ||
        !validateOptionalUrl(package.original_source_url, "original_source_url", error))
    {
        return false;
    }

    const QJsonValue releases_value = object.value(QStringLiteral("releases"));
    if (!releases_value.isArray() || releases_value.toArray().isEmpty())
    {
        if (error)
        {
            *error = "repository packages must declare a non-empty releases array";
        }
        return false;
    }

    bool latest_release_found = false;
    std::set<std::string, std::less<>> release_versions;
    for (const QJsonValue& release_value : releases_value.toArray())
    {
        if (!release_value.isObject())
        {
            if (error)
            {
                *error = "repository releases must be objects";
            }
            return false;
        }

        ExtensionRepositoryRelease release;
        if (!parseRepositoryRelease(release_value.toObject(), release, trusted_keys, error))
        {
            return false;
        }
        if (!release_versions.insert(release.version).second)
        {
            if (error)
            {
                *error = "repository release versions must be unique per package";
            }
            return false;
        }
        latest_release_found = latest_release_found || release.version == package.latest_version;
        package.releases.push_back(std::move(release));
    }

    if (!latest_release_found)
    {
        if (error)
        {
            *error = "latest_version must match one release version";
        }
        return false;
    }

    return true;
}

bool parseRepositoryIndex(const QJsonObject& object, ExtensionRepositoryIndex& index,
                          const std::vector<ExtensionSigningKey>& trusted_keys, std::string* error)
{
    if (!requireInt(object, QStringLiteral("schema_version"), index.schema_version, error) ||
        !requireString(object, QStringLiteral("repository"), index.repository, error) ||
        !requireString(object, QStringLiteral("updated_at"), index.updated_at_utc, error) ||
        !parseOptionalBoolField(object, QStringLiteral("official"), &index.official, error))
    {
        return false;
    }

    if (index.schema_version != 1)
    {
        if (error)
        {
            *error = "repository index schema_version must be 1";
        }
        return false;
    }
    if (index.repository.empty() || index.updated_at_utc.empty())
    {
        if (error)
        {
            *error = "repository and updated_at must not be empty";
        }
        return false;
    }

    const QJsonValue extensions_value = object.value(QStringLiteral("extensions"));
    if (!extensions_value.isArray())
    {
        if (error)
        {
            *error = "repository index extensions must be an array";
        }
        return false;
    }

    std::set<std::string, std::less<>> package_ids;
    for (const QJsonValue& package_value : extensions_value.toArray())
    {
        if (!package_value.isObject())
        {
            if (error)
            {
                *error = "repository index extensions must contain objects";
            }
            return false;
        }

        ExtensionRepositoryPackage package;
        if (!parseRepositoryPackage(package_value.toObject(), package, trusted_keys, error))
        {
            return false;
        }
        if (!package_ids.insert(package.id).second)
        {
            if (error)
            {
                *error = "repository extension ids must be unique";
            }
            return false;
        }

        index.packages.push_back(std::move(package));
    }

    return true;
}

ExtensionRepositoryIndexValidationResult
validateRepositoryIndexJson(const QByteArray& raw_json, const std::filesystem::path& issue_path,
                            const std::vector<ExtensionSigningKey>& trusted_keys)
{
    ExtensionRepositoryIndexValidationResult validation;

    if (raw_json.isEmpty())
    {
        addIndexValidationIssue(validation, ExtensionPackageValidationSeverity::Error, "read_failed",
                                "failed to read repository index", issue_path);
        validation.valid = false;
        return validation;
    }

    QJsonParseError parse_error;
    const QJsonDocument document = QJsonDocument::fromJson(raw_json, &parse_error);
    if (parse_error.error != QJsonParseError::NoError || !document.isObject())
    {
        addIndexValidationIssue(validation, ExtensionPackageValidationSeverity::Error, "invalid_json",
                                "repository index is not valid JSON", issue_path);
        validation.valid = false;
        return validation;
    }

    std::string error;
    if (!parseRepositoryIndex(document.object(), validation.index, trusted_keys, &error))
    {
        addIndexValidationIssue(validation, ExtensionPackageValidationSeverity::Error, "invalid_index",
                                error.empty() ? "repository index failed validation" : error, issue_path);
        validation.valid = false;
        return validation;
    }

    validation.valid = !hasIndexValidationError(validation);
    return validation;
}

[[nodiscard]] bool isEngineBackedSdkImportLine(const std::string& line)
{
    const std::string text = trimString(line);
    if (text.empty() || text[0] == '#')
    {
        return false;
    }

    if (text.rfind("import _afternight_runtime", 0) == 0 || text.rfind("from _afternight_runtime import", 0) == 0)
    {
        return true;
    }

    static constexpr std::array<std::string_view, 6> kEngineBackedModules = {"core",        "io",       "registration",
                                                                             "calibration", "stacking", "runtime"};
    for (const std::string_view module : kEngineBackedModules)
    {
        const std::string dotted = std::string("afternight.") + std::string(module);
        if (text.rfind("import " + dotted, 0) == 0 || text.rfind("from " + dotted + " import", 0) == 0)
        {
            return true;
        }
        if (text.rfind("from afternight import", 0) == 0 && text.find(std::string(module)) != std::string::npos)
        {
            return true;
        }
    }

    return false;
}

[[nodiscard]] bool startsWithImportTarget(const std::string& text, const std::string_view import_target)
{
    if (text.rfind(import_target, 0) != 0)
    {
        return false;
    }
    if (text.size() == import_target.size())
    {
        return true;
    }
    const char next = text[import_target.size()];
    return std::isspace(static_cast<unsigned char>(next)) != 0 || next == ',' || next == ';';
}

[[nodiscard]] bool isNativeUiSdkImportLine(const std::string& line)
{
    const std::string text = trimString(line);
    if (text.empty() || text[0] == '#')
    {
        return false;
    }

    if (startsWithImportTarget(text, "import afternight.ui") || text.rfind("from afternight.ui import", 0) == 0)
    {
        return true;
    }

    if (text.rfind("from afternight import", 0) == 0)
    {
        std::string imported = text.substr(std::string("from afternight import").size());
        std::replace(imported.begin(), imported.end(), ',', ' ');

        std::istringstream stream(imported);
        std::string token;
        while (stream >> token)
        {
            if (token == "ui" || token == "ProcessExtension" || token == "RTPreviewExtension" ||
                token == "WorkflowExtension")
            {
                return true;
            }
        }
    }

    return false;
}

[[nodiscard]] std::optional<std::filesystem::path>
findProtocolBackendEngineImport(const std::filesystem::path& package_root)
{
    std::error_code ec;
    for (std::filesystem::recursive_directory_iterator it(package_root, ec), end; it != end && !ec; it.increment(ec))
    {
        if (ec)
        {
            break;
        }
        if (it->is_directory())
        {
            const std::string name = it->path().filename().string();
            if (name == "__pycache__" || name == ".git" || name == ".venv" || name == "venv")
            {
                it.disable_recursion_pending();
            }
            continue;
        }
        if (!it->is_regular_file() || it->path().extension() != ".py")
        {
            continue;
        }

        std::istringstream stream(readFileText(it->path()));
        std::string line;
        while (std::getline(stream, line))
        {
            if (isEngineBackedSdkImportLine(line))
            {
                return it->path();
            }
        }
    }

    return std::nullopt;
}

[[nodiscard]] std::optional<std::filesystem::path> findNativeUiSdkImport(const std::filesystem::path& package_root)
{
    std::error_code ec;
    for (std::filesystem::recursive_directory_iterator it(package_root, ec), end; it != end && !ec; it.increment(ec))
    {
        if (ec)
        {
            break;
        }
        if (it->is_directory())
        {
            const std::string name = it->path().filename().string();
            if (name == "__pycache__" || name == ".git" || name == ".venv" || name == "venv")
            {
                it.disable_recursion_pending();
            }
            continue;
        }
        if (!it->is_regular_file() || it->path().extension() != ".py")
        {
            continue;
        }

        std::istringstream stream(readFileText(it->path()));
        std::string line;
        while (std::getline(stream, line))
        {
            if (isNativeUiSdkImportLine(line))
            {
                return it->path();
            }
        }
    }

    return std::nullopt;
}

void validateRepositoryReadinessFields(ExtensionPackageValidationResult& result,
                                       const ExtensionPackageValidationOptions& options)
{
    if (!result.load_result.manifest.has_value())
    {
        return;
    }

    const ExtensionManifest& manifest = *result.load_result.manifest;
    const ExtensionRepositoryMetadata& metadata = manifest.repository_metadata;
    const bool declared_non_gpl_license = isDeclaredNonGplLicense(metadata.license);
    const auto engine_import_path = findProtocolBackendEngineImport(std::filesystem::path(manifest.package_root));
    const auto native_ui_import_path = findNativeUiSdkImport(std::filesystem::path(manifest.package_root));

    if (!packageContainsEntryPoint(manifest))
    {
        addValidationIssue(result, ExtensionPackageValidationSeverity::Error, "missing_entry_point",
                           "entry_point does not resolve to a package-local Python module", manifest.package_root);
    }

    if (options.require_repository_metadata)
    {
        if (metadata.summary.empty())
        {
            addValidationIssue(result, ExtensionPackageValidationSeverity::Error, "missing_summary",
                               "repository-ready packages must declare summary");
        }
        if (metadata.author.empty())
        {
            addValidationIssue(result, ExtensionPackageValidationSeverity::Error, "missing_author",
                               "repository-ready packages must declare author");
        }
        if (metadata.license.empty())
        {
            addValidationIssue(result, ExtensionPackageValidationSeverity::Error, "missing_license",
                               "repository-ready packages must declare license");
        }
        if (metadata.publisher_id.empty())
        {
            addValidationIssue(result, ExtensionPackageValidationSeverity::Error, "missing_publisher",
                               "repository-ready packages must declare publisher_id");
        }
    }

    if (declared_non_gpl_license && manifest.sdk_backend == ExtensionSdkBackend::Runtime)
    {
        addValidationIssue(result, ExtensionPackageValidationSeverity::Error, "runtime_backend_non_gpl_license",
                           "non-GPL packages must not declare sdk_backend = runtime because the full hosted runtime "
                           "backend links direct Engine/native-control GPL components; use sdk_backend = protocol "
                           "unless the package is GPL-3.0-or-later/GPL-compatible");
    }

    if (declared_non_gpl_license && engine_import_path.has_value())
    {
        addValidationIssue(result, ExtensionPackageValidationSeverity::Error, "non_gpl_engine_import",
                           "non-GPL packages must not import _afternight_runtime or Engine-backed afternight modules",
                           *engine_import_path);
    }

    if (declared_non_gpl_license && native_ui_import_path.has_value())
    {
        addValidationIssue(result, ExtensionPackageValidationSeverity::Error, "non_gpl_native_ui_import",
                           "non-GPL packages must not import the native afternight.ui surface; use "
                           "afternight.ui_protocol for protocol-safe result/dialog helpers",
                           *native_ui_import_path);
    }

    const auto native_control_process =
        std::find_if(manifest.processes.begin(), manifest.processes.end(), [](const ExtensionProcessManifest& process) {
            return requiresNativeAfterNightControls(process.capabilities);
        });
    if (native_control_process != manifest.processes.end())
    {
        if (manifest.sdk_backend == ExtensionSdkBackend::Protocol)
        {
            addValidationIssue(result, ExtensionPackageValidationSeverity::Error, "protocol_backend_native_controls",
                               "protocol backend packages must not declare native-control-only capabilities; lite "
                               "hosted extensions must own their UI without AfterNight native process windows");
        }
        if (declared_non_gpl_license)
        {
            addValidationIssue(result, ExtensionPackageValidationSeverity::Error, "non_gpl_native_controls",
                               "non-GPL packages must not require native AfterNight controls; use sdk_backend = "
                               "protocol with extension-owned UI or choose a GPL-compatible license");
        }
    }

    if (options.require_archive_hash)
    {
        const bool has_manifest_hash = !metadata.archive_hash_sha256.empty();
        const bool has_receipt_hash = !result.load_result.provenance.archive_hash_sha256.empty();
        if (!has_manifest_hash && !has_receipt_hash)
        {
            addValidationIssue(
                result, ExtensionPackageValidationSeverity::Error, "missing_archive_hash",
                "repository-ready packages must carry an archive SHA-256 hash in metadata or provenance");
        }
    }

    if (manifest.sdk_backend == ExtensionSdkBackend::Protocol)
    {
        if (engine_import_path.has_value())
        {
            addValidationIssue(result, ExtensionPackageValidationSeverity::Error, "protocol_backend_engine_import",
                               "protocol backend packages must not import _afternight_runtime or Engine-backed "
                               "afternight modules",
                               *engine_import_path);
        }
        if (native_ui_import_path.has_value())
        {
            addValidationIssue(result, ExtensionPackageValidationSeverity::Error, "protocol_backend_native_ui_import",
                               "protocol backend packages must not import the native afternight.ui surface; use "
                               "afternight.ui_protocol for lite hosted app/view helpers",
                               *native_ui_import_path);
        }
    }
}

} // anonymous namespace

namespace an::ui::extensions
{

ExtensionRuntimeStatusRegistry& ExtensionRuntimeStatusRegistry::instance()
{
    static ExtensionRuntimeStatusRegistry registry;
    return registry;
}

void ExtensionRuntimeStatusRegistry::clear()
{
    const std::lock_guard<std::mutex> lock(mutex_);
    statuses_.clear();
}

void ExtensionRuntimeStatusRegistry::recordLaunchSucceeded(const std::string_view extension_id,
                                                           const std::string_view detail)
{
    if (extension_id.empty())
    {
        return;
    }

    const std::lock_guard<std::mutex> lock(mutex_);
    auto& snapshot = statuses_[std::string(extension_id)];
    snapshot.has_launch_result = true;
    snapshot.launch_outcome = ExtensionRuntimeLaunchOutcome::Succeeded;
    snapshot.launch_detail = std::string(detail);
}

void ExtensionRuntimeStatusRegistry::recordLaunchFailed(const std::string_view extension_id,
                                                        const std::string_view detail)
{
    if (extension_id.empty())
    {
        return;
    }

    const std::lock_guard<std::mutex> lock(mutex_);
    auto& snapshot = statuses_[std::string(extension_id)];
    snapshot.has_launch_result = true;
    snapshot.launch_outcome = ExtensionRuntimeLaunchOutcome::Failed;
    snapshot.launch_detail = std::string(detail);
}

void ExtensionRuntimeStatusRegistry::recordTermination(const std::string_view extension_id,
                                                       const ExtensionRuntimeTerminationKind kind,
                                                       const std::string_view detail,
                                                       const ExtensionRuntimeTerminationDiagnostics* diagnostics)
{
    if (extension_id.empty())
    {
        return;
    }

    const std::lock_guard<std::mutex> lock(mutex_);
    auto& snapshot = statuses_[std::string(extension_id)];
    snapshot.has_termination_result = true;
    snapshot.termination_kind = kind;
    snapshot.termination_detail = std::string(detail);
    snapshot.termination_diagnostics = diagnostics != nullptr ? *diagnostics : ExtensionRuntimeTerminationDiagnostics{};
}

std::optional<ExtensionRuntimeStatusSnapshot>
ExtensionRuntimeStatusRegistry::snapshotForExtension(const std::string_view extension_id) const
{
    if (extension_id.empty())
    {
        return std::nullopt;
    }

    const std::lock_guard<std::mutex> lock(mutex_);
    const auto it = statuses_.find(extension_id);
    if (it == statuses_.end())
    {
        return std::nullopt;
    }

    return it->second;
}

void ExtensionRuntimeStatusRegistry::mergeIntoDiscoveryResults(std::vector<ExtensionLoadResult>* results) const
{
    if (!results)
    {
        return;
    }

    const std::lock_guard<std::mutex> lock(mutex_);
    for (auto& result : *results)
    {
        if (!result.manifest)
        {
            continue;
        }

        const auto it = statuses_.find(result.manifest->id);
        if (it != statuses_.end())
        {
            result.runtime_status = it->second;
        }
    }
}

ExtensionSettingsActionResult run_extension_settings_action(const ExtensionSettingsActionRequest& request)
{
    return runSettingsActionHostProcess(request);
}

std::string current_runtime_target()
{
#if defined(_WIN32)
    return "windows-msvc-x86_64";
#else
    return "linux-clang-x86_64";
#endif
}

bool extensionNetworkDisabledByPolicy()
{
    const auto truthy = [](const char* name) {
        const QString value = qEnvironmentVariable(name).trimmed().toLower();
        return value == QStringLiteral("1") || value == QStringLiteral("true") || value == QStringLiteral("yes") ||
               value == QStringLiteral("on");
    };
    return truthy("AFTERNIGHT_DISABLE_EXTENSION_REPOSITORY_NETWORK") ||
           truthy("AFTERNIGHT_EXTENSION_REPOSITORY_OFFLINE");
}

bool extension_subsystem_supported_in_current_build()
{
    return true;
}

std::vector<ExtensionSigningKey> official_extension_signing_keys()
{
    return {ExtensionSigningKey{.key_id = "afternight-official-ed25519-v1",
                                .public_key_base64 = "iGiqSWok7L4DqGKUU4BIE/rMg63MgAIJvm0Pp09WK6Y="}};
}

bool official_extension_signing_key_trusted(const std::string_view key_id)
{
    return trustedSigningPublicKey(key_id, official_extension_signing_keys()).has_value();
}

std::string_view extensionPreviewScalingName(const ExtensionPreviewScaling scaling) noexcept
{
    switch (scaling)
    {
    case ExtensionPreviewScaling::Decimated:
        return "decimated";
    case ExtensionPreviewScaling::NativeRegion:
        return "native_region";
    case ExtensionPreviewScaling::FullOnly:
        break;
    }
    return "full_only";
}

std::optional<ExtensionPreviewScaling> extensionPreviewScalingFromName(const std::string_view name) noexcept
{
    for (const ExtensionPreviewScaling scaling :
         {ExtensionPreviewScaling::FullOnly, ExtensionPreviewScaling::Decimated, ExtensionPreviewScaling::NativeRegion})
    {
        if (name == extensionPreviewScalingName(scaling))
        {
            return scaling;
        }
    }
    return std::nullopt;
}

std::filesystem::path bundled_extensions_root(const std::filesystem::path& app_dir)
{
    return app_dir / "extensions";
}

std::filesystem::path user_extensions_root()
{
    const QString root = an::ui::applicationLocalDataPath();
    return std::filesystem::path(root.toStdString()) / "extensions";
}

std::filesystem::path repository_extensions_root()
{
    return user_extensions_root() / "_repository";
}

std::optional<RelocatablePackage> appImagePackageForAppDir(const std::filesystem::path& appDir,
                                                           const std::filesystem::path& appImageFile,
                                                           const std::filesystem::path& appImageRoot)
{
    std::error_code ec;
    if (appDir.empty() || appImageFile.empty() || appImageRoot.empty() ||
        !std::filesystem::is_regular_file(appImageFile, ec) || !std::filesystem::is_directory(appImageRoot, ec))
    {
        return std::nullopt;
    }

    const std::filesystem::path root = std::filesystem::weakly_canonical(appImageRoot, ec);
    if (ec)
    {
        return std::nullopt;
    }
    const std::filesystem::path application = std::filesystem::weakly_canonical(appDir, ec);
    if (ec)
    {
        return std::nullopt;
    }
    const std::filesystem::path inside = application.lexically_relative(root);
    if (inside.empty() || *inside.begin() == "..")
    {
        return std::nullopt;
    }

    const auto size = std::filesystem::file_size(appImageFile, ec);
    if (ec)
    {
        return std::nullopt;
    }
    const auto written = std::filesystem::last_write_time(appImageFile, ec);
    if (ec)
    {
        return std::nullopt;
    }
    std::ostringstream identity;
    identity << "appimage\n"
             << size << '\n'
             << std::chrono::duration_cast<std::chrono::nanoseconds>(written.time_since_epoch()).count() << '\n';
    return RelocatablePackage{root, identity.str()};
}

ExtensionDiscoveryOptions default_extension_discovery_options_for_app_dir(const std::filesystem::path& app_dir)
{
    ExtensionDiscoveryOptions options;
    options.bundled_roots.push_back(bundled_extensions_root(app_dir));
    options.repository_roots.push_back(repository_extensions_root());
    options.user_roots.push_back(user_extensions_root());
    options.context.runtime_target = current_runtime_target();
    options.context.app_version = currentAfterNightVersion();
    options.context.bundled_python_executable = bundled_python_executable_for_app_dir(app_dir);
    options.context.extension_host_executable = bundled_extension_host_executable_for_app_dir(app_dir);
    options.context.runtime_root = app_dir / "PythonLib";
    options.context.environments_root = user_extension_environments_root();
    options.context.eager_settings_schema_inspection = false;
    options.context.workflow_launch_mode_available = true;
    // An AppImage's runtime states the package file and where this run's copy of its files is.
    options.context.package =
        appImagePackageForAppDir(app_dir, std::filesystem::path(qEnvironmentVariable("APPIMAGE").toStdString()),
                                 std::filesystem::path(qEnvironmentVariable("APPDIR").toStdString()));

    ExtensionEnvironmentManager environment_manager;
    options.context.host_bootstrap_fingerprint = environment_manager.computeHostBootstrapFingerprint(options.context);
    return options;
}

ExtensionDiscoveryOptions default_extension_discovery_options()
{
    const std::filesystem::path app_dir(QCoreApplication::applicationDirPath().toStdString());
    return default_extension_discovery_options_for_app_dir(app_dir);
}

bool store_ready_extension_startup_catalog(const std::vector<ExtensionLoadResult>& results,
                                           const ExtensionDiscoveryContext& context)
{
    const ExtensionDiscoveryContext normalized_context = normalizeDiscoveryContext(context);
    const std::filesystem::path catalog_path = startupActivationCatalogPath(normalized_context);

    ExtensionEnvironmentManager environment_manager;
    const std::string host_bootstrap_fingerprint =
        environment_manager.computeHostBootstrapFingerprint(normalized_context);

    QJsonArray entries;
    for (const ExtensionLoadResult& result : results)
    {
        if (result.status != ExtensionLoadStatus::Ready || !result.manifest || !result.environment ||
            result.environment->state != ExtensionEnvironmentState::Ready)
        {
            continue;
        }

        const ExtensionEnvironmentSnapshot& environment = *result.environment;
        const std::filesystem::path receipt_path = environment.receipt_path.empty()
                                                       ? environment.environment_path / "install_receipt.json"
                                                       : environment.receipt_path;

        std::error_code ec;
        const auto manifest_size = std::filesystem::file_size(result.manifest_path, ec);
        if (ec || fileTimeNanoseconds(result.manifest_path) == -1 ||
            !std::filesystem::exists(environment.environment_path) ||
            !std::filesystem::exists(environment.environment_path / ".environment_ready") ||
            !std::filesystem::is_regular_file(receipt_path))
        {
            continue;
        }

        const auto receipt_size = std::filesystem::file_size(receipt_path, ec);
        if (ec || fileTimeNanoseconds(receipt_path) == -1)
        {
            continue;
        }

        QJsonObject entry;
        entry.insert(QStringLiteral("manifest_path"), QString::fromStdString(result.manifest_path.generic_string()));
        entry.insert(QStringLiteral("manifest_size"), QString::number(static_cast<qulonglong>(manifest_size)));
        entry.insert(QStringLiteral("manifest_mtime_ns"), QString::number(fileTimeNanoseconds(result.manifest_path)));
        entry.insert(QStringLiteral("source"), QString::fromStdString(sourceId(result.source)));
        entry.insert(QStringLiteral("environment_path"),
                     QString::fromStdString(environment.environment_path.generic_string()));
        entry.insert(QStringLiteral("receipt_path"), QString::fromStdString(receipt_path.generic_string()));
        entry.insert(QStringLiteral("receipt_size"), QString::number(static_cast<qulonglong>(receipt_size)));
        entry.insert(QStringLiteral("receipt_mtime_ns"), QString::number(fileTimeNanoseconds(receipt_path)));
        entry.insert(QStringLiteral("dependency_fingerprint"),
                     QString::fromStdString(environment.dependency_fingerprint));
        entry.insert(QStringLiteral("validation_fingerprint"),
                     QString::fromStdString(environment.validation_fingerprint));
        entry.insert(QStringLiteral("bundled_python_version"),
                     QString::fromStdString(environment.bundled_python_version));
        entry.insert(QStringLiteral("runtime_target"), QString::fromStdString(environment.runtime_target));
        entry.insert(QStringLiteral("host_bootstrap_fingerprint"),
                     QString::fromStdString(environment.host_bootstrap_fingerprint));
        entries.push_back(entry);
    }

    if (entries.isEmpty())
    {
        std::error_code ec;
        std::filesystem::remove(catalog_path, ec);
        return !ec;
    }

    std::error_code ec;
    std::filesystem::create_directories(catalog_path.parent_path(), ec);
    if (ec)
    {
        return false;
    }

    QJsonObject catalog;
    catalog.insert(QStringLiteral("format_version"), kStartupActivationCatalogFormatVersion);
    catalog.insert(QStringLiteral("runtime_target"), QString::fromStdString(normalized_context.runtime_target));
    catalog.insert(QStringLiteral("host_bootstrap_fingerprint"), QString::fromStdString(host_bootstrap_fingerprint));
    catalog.insert(QStringLiteral("entries"), entries);

    QSaveFile output(QString::fromStdString(catalog_path.generic_string()));
    if (!output.open(QIODevice::WriteOnly) || output.write(QJsonDocument(catalog).toJson(QJsonDocument::Compact)) < 0)
    {
        return false;
    }

    return output.commit();
}

std::vector<ExtensionLoadResult> load_ready_extension_startup_catalog(const ExtensionDiscoveryOptions& options)
{
    const ExtensionDiscoveryContext context = normalizeDiscoveryContext(options.context);
    const std::filesystem::path catalog_path = startupActivationCatalogPath(context);
    const std::string catalog_text = readFileText(catalog_path);
    if (catalog_text.empty())
    {
        return {};
    }

    QJsonParseError parse_error;
    const QJsonDocument document = QJsonDocument::fromJson(QByteArray::fromStdString(catalog_text), &parse_error);
    if (parse_error.error != QJsonParseError::NoError || !document.isObject())
    {
        return {};
    }

    const QJsonObject catalog = document.object();
    ExtensionEnvironmentManager environment_manager;
    const std::string host_bootstrap_fingerprint = environment_manager.computeHostBootstrapFingerprint(context);
    if (catalog.value(QStringLiteral("format_version")).toInt() != kStartupActivationCatalogFormatVersion ||
        catalog.value(QStringLiteral("runtime_target")).toString().toStdString() != context.runtime_target ||
        catalog.value(QStringLiteral("host_bootstrap_fingerprint")).toString().toStdString() !=
            host_bootstrap_fingerprint)
    {
        return {};
    }

    std::vector<ExtensionLoadResult> results;
    const QJsonArray entries = catalog.value(QStringLiteral("entries")).toArray();
    results.reserve(static_cast<std::size_t>(entries.size()));
    for (const QJsonValue& value : entries)
    {
        if (!value.isObject())
        {
            continue;
        }

        const QJsonObject entry = value.toObject();
        const auto source = sourceFromId(entry.value(QStringLiteral("source")).toString().toStdString());
        const std::filesystem::path manifest_path(
            entry.value(QStringLiteral("manifest_path")).toString().toStdString());
        const std::filesystem::path environment_path(
            entry.value(QStringLiteral("environment_path")).toString().toStdString());
        const std::filesystem::path receipt_path(entry.value(QStringLiteral("receipt_path")).toString().toStdString());
        if (!source || manifest_path.empty() || environment_path.empty() || receipt_path.empty())
        {
            continue;
        }

        std::error_code ec;
        const auto manifest_size = std::filesystem::file_size(manifest_path, ec);
        if (ec ||
            QString::number(static_cast<qulonglong>(manifest_size)) !=
                entry.value(QStringLiteral("manifest_size")).toString() ||
            QString::number(fileTimeNanoseconds(manifest_path)) !=
                entry.value(QStringLiteral("manifest_mtime_ns")).toString() ||
            !std::filesystem::is_regular_file(environment_path / ".environment_ready") ||
            !std::filesystem::is_regular_file(receipt_path))
        {
            continue;
        }

        const auto receipt_size = std::filesystem::file_size(receipt_path, ec);
        if (ec ||
            QString::number(static_cast<qulonglong>(receipt_size)) !=
                entry.value(QStringLiteral("receipt_size")).toString() ||
            QString::number(fileTimeNanoseconds(receipt_path)) !=
                entry.value(QStringLiteral("receipt_mtime_ns")).toString())
        {
            continue;
        }

        ExtensionLoadResult result = loadManifestForStartupActivation(manifest_path, *source, context);
        if (result.status != ExtensionLoadStatus::Ready || !result.manifest)
        {
            continue;
        }

        const std::string dependency_fingerprint =
            entry.value(QStringLiteral("dependency_fingerprint")).toString().toStdString();
        const std::string validation_fingerprint =
            entry.value(QStringLiteral("validation_fingerprint")).toString().toStdString();
        const std::string bundled_python_version =
            entry.value(QStringLiteral("bundled_python_version")).toString().toStdString();
        const std::string runtime_target = entry.value(QStringLiteral("runtime_target")).toString().toStdString();
        const std::string entry_bootstrap_fingerprint =
            entry.value(QStringLiteral("host_bootstrap_fingerprint")).toString().toStdString();
        if (dependency_fingerprint.empty() || validation_fingerprint.empty() ||
            bundled_python_version != context.bundled_python_version || runtime_target != context.runtime_target ||
            entry_bootstrap_fingerprint != host_bootstrap_fingerprint)
        {
            continue;
        }

        const std::string receipt_text = readFileText(receipt_path);
        QJsonParseError receipt_parse_error;
        const QJsonDocument receipt_document =
            QJsonDocument::fromJson(QByteArray::fromStdString(receipt_text), &receipt_parse_error);
        if (receipt_parse_error.error != QJsonParseError::NoError || !receipt_document.isObject())
        {
            continue;
        }

        const QJsonObject receipt = receipt_document.object();
        const auto receipt_value_matches = [&receipt](const char* key, const std::string& expected) {
            const QString value = receipt.value(QString::fromLatin1(key)).toString();
            return value.isEmpty() || value.toStdString() == expected;
        };
        if (!receipt_value_matches("host_bootstrap_fingerprint", host_bootstrap_fingerprint) ||
            !receipt_value_matches("dependency_fingerprint", dependency_fingerprint) ||
            !receipt_value_matches("validation_fingerprint", validation_fingerprint))
        {
            continue;
        }

        ExtensionEnvironmentSnapshot environment;
        environment.state = ExtensionEnvironmentState::Ready;
        environment.host_bootstrap_fingerprint = std::move(entry_bootstrap_fingerprint);
        environment.dependency_fingerprint = std::move(dependency_fingerprint);
        environment.validation_fingerprint = std::move(validation_fingerprint);
        environment.bundled_python_version = std::move(bundled_python_version);
        environment.runtime_target = std::move(runtime_target);
        environment.bundled_python_executable = context.bundled_python_executable;
        environment.runtime_root = context.runtime_root;
        environment.environments_root = context.environments_root;
        environment.environment_path = environment_path;
        environment.receipt_path = receipt_path;
        // An AppImage's environment names the interpreter of the run that made it, and a host started from this
        // entry before discovery has looked at it needs this run's.
        if (context.package &&
            !environment_manager.pointEnvironmentAtInterpreter(environment_path, context.bundled_python_executable))
        {
            continue;
        }
        result.environment = std::move(environment);
        result.settings_schema = inspectSettingsSchemaForResult(result, context);
        results.push_back(std::move(result));
    }

    applyDuplicatePrecedence(results);
    applySharedDependencyContextConsistency(results);
    ExtensionRuntimeStatusRegistry::instance().mergeIntoDiscoveryResults(&results);
    return results;
}

std::string extension_package_source_id(const ExtensionPackageSource source)
{
    return sourceId(source);
}

std::optional<ExtensionPackageSource> extension_package_source_from_id(const std::string_view source_id)
{
    return sourceFromId(source_id);
}

std::string extension_sdk_backend_id(const ExtensionSdkBackend backend)
{
    switch (backend)
    {
    case ExtensionSdkBackend::Rpc:
        return "rpc";
    case ExtensionSdkBackend::Protocol:
        return "protocol";
    case ExtensionSdkBackend::Runtime:
    default:
        return "runtime";
    }
}

std::string extension_host_mode_label(const ExtensionSdkBackend backend)
{
    switch (backend)
    {
    case ExtensionSdkBackend::Runtime:
        return "Full hosted";
    case ExtensionSdkBackend::Protocol:
        return "Lite hosted";
    case ExtensionSdkBackend::Rpc:
        return "Lite hosted + RPC";
    }

    return "Full hosted";
}

std::string extension_host_mode_description(const ExtensionSdkBackend backend)
{
    switch (backend)
    {
    case ExtensionSdkBackend::Runtime:
        return "Full hosted extensions run in AfterNightExtensionHost, may use direct runtime Engine bindings and "
               "native AfterNight controls, and must be GPL-compatible when distributed.";
    case ExtensionSdkBackend::Protocol:
        return "Lite hosted extensions run in AfterNightExtensionHostLite, use only protocol app/view services, own "
               "their UI, and do not load direct Engine bindings or native AfterNight controls.";
    case ExtensionSdkBackend::Rpc:
        return "Lite hosted + RPC extensions run in AfterNightExtensionHostLite and require the deferred "
               "AfterNightSdkHost sidecar for Engine-domain RPC calls.";
    }

    return "Unknown extension host mode.";
}

std::string default_extension_action_object_name(std::string_view extension_id, std::string_view id_suffix)
{
    std::string action_name = "actionExtension_";
    action_name += sanitizeIdentifier(extension_id);
    if (!id_suffix.empty())
    {
        action_name.push_back('_');
        action_name += sanitizeIdentifier(id_suffix);
    }
    return action_name;
}

ExtensionLoadResult load_extension_manifest_file(const std::filesystem::path& manifest_path,
                                                 const ExtensionPackageSource source,
                                                 const ExtensionDiscoveryContext& context,
                                                 ExtensionOperationCancellationToken cancellation_token)
{
    const ExtensionDiscoveryContext normalized_context = normalizeDiscoveryContext(context);
    ExtensionLoadResult result =
        loadExtensionManifestMetadata(manifest_path, source, normalized_context, cancellation_token);
    if (result.manifest && result.status == ExtensionLoadStatus::Ready)
    {
        ExtensionEnvironmentManager environment_manager;
        result.environment =
            environment_manager.inspectEnvironment(*result.manifest, source, normalized_context, cancellation_token);
    }
    return result;
}

bool manifestFileDeclaresDevelopmentOnly(const std::filesystem::path& manifest_path)
{
    const std::string raw_json = readFileText(manifest_path);
    if (raw_json.empty())
    {
        return false;
    }

    QJsonParseError parse_error;
    const QJsonDocument document = QJsonDocument::fromJson(QByteArray::fromStdString(raw_json), &parse_error);
    if (parse_error.error != QJsonParseError::NoError || !document.isObject())
    {
        return false;
    }

    const QJsonValue value = document.object().value(QStringLiteral("development_only"));
    return value.isBool() && value.toBool();
}

ExtensionPackageValidationResult validate_extension_manifest_file(const std::filesystem::path& manifest_path,
                                                                  const ExtensionPackageValidationOptions& options)
{
    ExtensionPackageValidationResult validation;

    ExtensionDiscoveryContext context = normalizeDiscoveryContext(options.context);

    validation.load_result = load_extension_manifest_file(manifest_path, options.source, context);

    if (validation.load_result.status == ExtensionLoadStatus::Invalid)
    {
        addValidationIssue(validation, ExtensionPackageValidationSeverity::Error, "invalid_manifest",
                           validation.load_result.diagnostic.empty() ? "extension package failed manifest validation"
                                                                     : validation.load_result.diagnostic,
                           manifest_path);
        validation.valid = false;
        return validation;
    }

    if (validation.load_result.status == ExtensionLoadStatus::Incompatible)
    {
        addValidationIssue(validation,
                           options.require_current_runtime_compatibility ? ExtensionPackageValidationSeverity::Error
                                                                         : ExtensionPackageValidationSeverity::Warning,
                           "incompatible_runtime",
                           validation.load_result.diagnostic.empty()
                               ? "extension package is incompatible with the current runtime"
                               : validation.load_result.diagnostic,
                           manifest_path);
    }

    validateRepositoryReadinessFields(validation, options);
    validation.valid = !hasValidationError(validation);
    return validation;
}

ExtensionPackageValidationResult validate_extension_package_root(const std::filesystem::path& package_root,
                                                                 const ExtensionPackageValidationOptions& options)
{
    ExtensionPackageValidationResult validation;
    std::error_code ec;
    if (!std::filesystem::exists(package_root, ec) || !std::filesystem::is_directory(package_root, ec))
    {
        addValidationIssue(validation, ExtensionPackageValidationSeverity::Error, "missing_package_root",
                           "extension package root does not exist or is not a directory", package_root);
        validation.valid = false;
        return validation;
    }

    const std::filesystem::path manifest_path = package_root / "extension.json";
    if (!std::filesystem::exists(manifest_path, ec) || !std::filesystem::is_regular_file(manifest_path, ec))
    {
        addValidationIssue(validation, ExtensionPackageValidationSeverity::Error, "missing_manifest",
                           "extension package root must contain extension.json", manifest_path);
        validation.valid = false;
        return validation;
    }

    return validate_extension_manifest_file(manifest_path, options);
}

ExtensionRepositoryIndexValidationResult
validate_extension_repository_index_file(const std::filesystem::path& index_path,
                                         const std::vector<ExtensionSigningKey>& trusted_keys)
{
    std::error_code ec;
    if (!std::filesystem::is_regular_file(index_path, ec))
    {
        ExtensionRepositoryIndexValidationResult validation;
        addIndexValidationIssue(validation, ExtensionPackageValidationSeverity::Error, "missing_index",
                                "repository index does not exist or is not a regular file", index_path);
        validation.valid = false;
        return validation;
    }

    const std::string raw_json = readFileText(index_path);
    return validateRepositoryIndexJson(QByteArray::fromStdString(raw_json), index_path, trusted_keys);
}

QUrl official_extension_repository_index_url()
{
    return QUrl(
        QStringLiteral("https://api.github.com/repos/emruiz81/afternight-extensions/contents/index.json?ref=live"));
}

QUrl networkCacheBypassUrl(const QUrl& url)
{
    QUrl fresh_url(url);
    QUrlQuery query(fresh_url);
    query.removeAllQueryItems(QStringLiteral("_afternight_cache_bust"));
    query.addQueryItem(QStringLiteral("_afternight_cache_bust"), QUuid::createUuid().toString(QUuid::WithoutBraces));
    fresh_url.setQuery(query);
    return fresh_url;
}

[[nodiscard]] std::optional<QUrl> githubContentsApiUrlForRawRepositoryContentUrl(const QUrl& url)
{
    if ((url.scheme() != QStringLiteral("http") && url.scheme() != QStringLiteral("https")) ||
        url.host().compare(QStringLiteral("raw.githubusercontent.com"), Qt::CaseInsensitive) != 0)
    {
        return std::nullopt;
    }

    const QStringList path_segments = url.path().split(QChar('/'), Qt::SkipEmptyParts);
    if (path_segments.size() < 4)
    {
        return std::nullopt;
    }

    const QString owner = path_segments.at(0);
    const QString repo = path_segments.at(1);
    const QString ref = path_segments.at(2);
    const QString repository_path = path_segments.mid(3).join(QChar('/'));

    QUrl api_url;
    api_url.setScheme(QStringLiteral("https"));
    api_url.setHost(QStringLiteral("api.github.com"));
    api_url.setPath(QStringLiteral("/repos/%1/%2/contents/%3").arg(owner, repo, repository_path));

    QUrlQuery query;
    query.addQueryItem(QStringLiteral("ref"), ref);
    api_url.setQuery(query);
    return api_url;
}

[[nodiscard]] std::optional<QUrl> rawRepositoryContentUrlForGithubContentsApiUrl(const QUrl& url)
{
    if ((url.scheme() != QStringLiteral("http") && url.scheme() != QStringLiteral("https")) ||
        url.host().compare(QStringLiteral("api.github.com"), Qt::CaseInsensitive) != 0)
    {
        return std::nullopt;
    }

    const QStringList path_segments = url.path().split(QChar('/'), Qt::SkipEmptyParts);
    if (path_segments.size() < 5 || path_segments.at(0) != QStringLiteral("repos") ||
        path_segments.at(3) != QStringLiteral("contents"))
    {
        return std::nullopt;
    }

    const QString ref = QUrlQuery(url).queryItemValue(QStringLiteral("ref"));
    if (ref.isEmpty())
    {
        return std::nullopt;
    }

    const QString owner = path_segments.at(1);
    const QString repo = path_segments.at(2);
    const QString repository_path = path_segments.mid(4).join(QChar('/'));

    QUrl raw_url;
    raw_url.setScheme(QStringLiteral("https"));
    raw_url.setHost(QStringLiteral("raw.githubusercontent.com"));
    raw_url.setPath(QStringLiteral("/%1/%2/%3/%4").arg(owner, repo, ref, repository_path));
    return raw_url;
}

[[nodiscard]] bool isGithubContentsApiUrl(const QUrl& url)
{
    return rawRepositoryContentUrlForGithubContentsApiUrl(url).has_value();
}

[[nodiscard]] std::optional<QByteArray> decodeGithubContentsApiJsonPayload(const QByteArray& payload)
{
    QJsonParseError parse_error;
    const QJsonDocument document = QJsonDocument::fromJson(payload, &parse_error);
    if (parse_error.error != QJsonParseError::NoError || !document.isObject())
    {
        return std::nullopt;
    }

    const QJsonObject object = document.object();
    if (object.value(QStringLiteral("type")).toString() != QStringLiteral("file") ||
        object.value(QStringLiteral("encoding")).toString() != QStringLiteral("base64"))
    {
        return std::nullopt;
    }

    QByteArray encoded = object.value(QStringLiteral("content")).toString().toLatin1();
    encoded.replace("\r", "");
    encoded.replace("\n", "");
    encoded.replace(" ", "");
    if (encoded.isEmpty())
    {
        return QByteArray();
    }

    const QByteArray decoded = QByteArray::fromBase64(encoded, QByteArray::Base64Encoding);
    if (decoded.isEmpty())
    {
        return std::nullopt;
    }
    return decoded;
}

[[nodiscard]] ExtensionRepositoryIndexValidationResult validateFetchedRepositoryIndexPayload(const QByteArray& payload)
{
    ExtensionRepositoryIndexValidationResult validation =
        validateRepositoryIndexJson(payload, {}, official_extension_signing_keys());
    if (validation.valid)
    {
        return validation;
    }

    const std::optional<QByteArray> decoded_payload = decodeGithubContentsApiJsonPayload(payload);
    if (!decoded_payload.has_value())
    {
        return validation;
    }

    return validateRepositoryIndexJson(*decoded_payload, {}, official_extension_signing_keys());
}

QUrl resolve_extension_repository_asset_url(const QUrl& index_url, const std::string_view download_url)
{
    const QUrl asset_url(QString::fromUtf8(download_url.data(), static_cast<qsizetype>(download_url.size())));
    if (!asset_url.isRelative())
    {
        return asset_url;
    }

    const QUrl asset_base_url = rawRepositoryContentUrlForGithubContentsApiUrl(index_url).value_or(index_url);
    return asset_base_url.adjusted(QUrl::RemoveFilename).resolved(asset_url);
}

std::vector<ExtensionRepositoryBrowseEntry>
build_extension_repository_browse_entries(const ExtensionRepositoryIndex& index,
                                          const std::vector<ExtensionLoadResult>& installed_extensions,
                                          const ExtensionRepositoryBrowseOptions& options)
{
    const ExtensionDiscoveryContext context = normalizeDiscoveryContext(options.context);
    const std::string& runtime_target = context.runtime_target;
    const std::string& app_version = context.app_version;

    std::map<std::string, const ExtensionLoadResult*, std::less<>> installed_by_id;
    for (const auto& installed : installed_extensions)
    {
        if (!installed.manifest.has_value())
        {
            continue;
        }
        installed_by_id[installed.manifest->id] = &installed;
    }

    const std::string query = toLowerCopy(trimString(options.search_query));
    std::vector<ExtensionRepositoryBrowseEntry> entries;
    for (const auto& package : index.packages)
    {
        const auto release_it =
            std::find_if(package.releases.begin(), package.releases.end(),
                         [&](const auto& release) { return release.version == package.latest_version; });
        if (release_it == package.releases.end())
        {
            continue;
        }

        ExtensionRepositoryBrowseEntry entry;
        entry.id = package.id;
        entry.name = package.name;
        entry.summary = package.summary;
        entry.description = package.description;
        entry.author = package.author;
        entry.publisher_id = package.publisher_id;
        entry.license = package.license;
        entry.attribution = package.attribution;
        entry.original_author = package.original_author;
        entry.original_project = package.original_project;
        entry.original_source_url = package.original_source_url;
        entry.upstream_commit = package.upstream_commit;
        entry.homepage_url = package.homepage_url;
        entry.repository_url = package.repository_url;
        entry.support_url = package.support_url;
        entry.icon_url = package.icon_url;
        entry.tags = package.tags;
        entry.latest_version = package.latest_version;
        entry.selected_release_version = release_it->version;
        entry.release_min_app_version = release_it->min_app_version;
        entry.release_package_format_version = release_it->package_format_version;
        entry.release_protocol_version = release_it->protocol_version;
        entry.release_sdk_version = release_it->sdk_version;
        entry.release_sdk_backend = release_it->sdk_backend;
        entry.release_launch_mode = release_it->launch_mode;
        entry.release_runtime_targets = release_it->runtime_targets;
        entry.release_changelog = release_it->changelog;
        entry.release_published_at_utc = release_it->published_at_utc;

        const auto installed_it = installed_by_id.find(package.id);
        if (installed_it != installed_by_id.end() && installed_it->second)
        {
            const ExtensionLoadResult& installed = *installed_it->second;
            entry.installed_version = installed.manifest->version;
            entry.installed_source_available = true;
            entry.installed_source = installed.source;
            entry.installed_status_available = true;
            entry.installed_load_status = installed.status;
            entry.installed_status_diagnostic = installed.diagnostic;
            if (installed.environment.has_value())
            {
                entry.installed_environment_available = true;
                entry.installed_environment_state = installed.environment->state;
                entry.installed_environment_diagnostic = installed.environment->diagnostic;
                if (entry.installed_status_diagnostic.empty())
                {
                    entry.installed_status_diagnostic = installed.environment->diagnostic;
                }
            }
        }

        const auto matches_runtime = [&](const auto& targets) {
            return std::find(targets.begin(), targets.end(), runtime_target) != targets.end();
        };

        if (requiresNewerAfterNightVersion(release_it->min_app_version, app_version))
        {
            entry.compatibility_diagnostic =
                minimumAfterNightVersionDiagnostic(release_it->min_app_version, app_version);
        }
        else if (!matches_runtime(release_it->runtime_targets))
        {
            entry.compatibility_diagnostic = "Latest release does not support runtime target '" + runtime_target + "'";
        }
        else
        {
            const auto asset_it =
                std::find_if(release_it->assets.begin(), release_it->assets.end(),
                             [&](const auto& asset) { return matches_runtime(asset.runtime_targets); });
            if (asset_it == release_it->assets.end())
            {
                entry.compatibility_diagnostic =
                    "Latest release has no asset for runtime target '" + runtime_target + "'";
            }
            else
            {
                entry.runtime_compatible = true;
                entry.selected_asset_name = asset_it->name;
                entry.selected_asset_download_url = asset_it->download_url;
                entry.selected_asset_package_hash_sha256 = asset_it->package_hash_sha256;
                entry.selected_asset_runtime_targets = asset_it->runtime_targets;
                entry.selected_asset_signature_state = asset_it->signature_state;
                entry.selected_asset_signature_detail = asset_it->signature_detail;
                entry.selected_asset_signature_algorithm = asset_it->signature_algorithm;
                entry.selected_asset_signature_key_id = asset_it->signature_key_id;
                entry.selected_asset_signature = asset_it->signature;
            }
        }

        if (!entry.runtime_compatible)
        {
            entry.install_state = ExtensionRepositoryInstallState::Incompatible;
        }
        else if (entry.installed_version.empty())
        {
            entry.install_state = ExtensionRepositoryInstallState::NotInstalled;
        }
        else if (entry.installed_source == ExtensionPackageSource::Repository &&
                 compareVersionStrings(entry.installed_version, entry.latest_version) < 0)
        {
            entry.install_state = ExtensionRepositoryInstallState::UpdateAvailable;
        }
        else
        {
            entry.install_state = ExtensionRepositoryInstallState::Installed;
        }

        if (!query.empty())
        {
            std::string searchable = toLowerCopy(
                entry.id + " " + entry.name + " " + entry.summary + " " + entry.description + " " + entry.author + " " +
                entry.publisher_id + " " + entry.license + " " + entry.attribution + " " + entry.original_author + " " +
                entry.original_project + " " + entry.original_source_url + " " + entry.upstream_commit + " " +
                entry.homepage_url + " " + entry.repository_url + " " + entry.support_url + " " +
                entry.release_sdk_backend + " " + entry.release_launch_mode + " " + entry.release_changelog);
            for (const auto& tag : entry.tags)
            {
                searchable += " " + toLowerCopy(tag);
            }
            for (const auto& target : entry.release_runtime_targets)
            {
                searchable += " " + toLowerCopy(target);
            }
            if (searchable.find(query) == std::string::npos)
            {
                continue;
            }
        }

        entries.push_back(std::move(entry));
    }

    std::sort(entries.begin(), entries.end(),
              [](const auto& lhs, const auto& rhs) { return toLowerCopy(lhs.name) < toLowerCopy(rhs.name); });
    return entries;
}

ExtensionRepositoryIndexFetchResult
ExtensionRepositoryClient::fetchIndex(const ExtensionRepositoryIndexFetchRequest& request) const
{
    ExtensionRepositoryIndexFetchResult result;
    result.index_url = request.index_url.isEmpty() ? official_extension_repository_index_url() : request.index_url;
    if (cancellationRequested(request.cancellation_token))
    {
        result.diagnostic = "repository index fetch cancelled";
        return result;
    }

    if (an::platform::isLoggingInitialized())
    {
        LOG_DEBUG("Fetching extension repository index url='{}' timeout_ms={} bypass_cache={} require_official={}",
                  result.index_url.toString().toStdString(), request.timeout_ms, request.bypass_network_cache,
                  request.require_official);
    }

    const QString scheme = result.index_url.scheme().toLower();
    if (scheme.isEmpty() || result.index_url.isLocalFile())
    {
        const std::filesystem::path index_path =
            result.index_url.isLocalFile() ? std::filesystem::path(result.index_url.toLocalFile().toStdString())
                                           : std::filesystem::path(result.index_url.toString().toStdString());
        result.validation = validate_extension_repository_index_file(index_path);
        result.success = result.validation.valid;
        result.diagnostic = result.validation.diagnostic;
    }
    else if (scheme == QStringLiteral("http") || scheme == QStringLiteral("https"))
    {
        QNetworkAccessManager network_manager;
        const QUrl fetch_url =
            githubContentsApiUrlForRawRepositoryContentUrl(result.index_url).value_or(result.index_url);
        QNetworkRequest network_request(request.bypass_network_cache ? networkCacheBypassUrl(fetch_url) : fetch_url);
        network_request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("AfterNight Extension Manager"));
        network_request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                                     QNetworkRequest::NoLessSafeRedirectPolicy);
        if (isGithubContentsApiUrl(fetch_url))
        {
            network_request.setRawHeader("Accept", "application/vnd.github.raw+json");
            network_request.setRawHeader("X-GitHub-Api-Version", "2022-11-28");
        }
        if (request.bypass_network_cache)
        {
            network_request.setRawHeader("Cache-Control", "no-cache, no-store, max-age=0");
            network_request.setRawHeader("Pragma", "no-cache");
            network_request.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);
            network_request.setAttribute(QNetworkRequest::CacheSaveControlAttribute, false);
        }

        QNetworkReply* reply = network_manager.get(network_request);
        QEventLoop loop;
        QTimer timer;
        QTimer cancellation_timer;
        timer.setSingleShot(true);
        cancellation_timer.setInterval(25);
        QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
        QObject::connect(&cancellation_timer, &QTimer::timeout, &loop, [&]() {
            if (cancellationRequested(request.cancellation_token))
            {
                loop.quit();
            }
        });
        timer.start((std::max)(request.timeout_ms, 1));
        cancellation_timer.start();
        loop.exec();
        cancellation_timer.stop();

        if (cancellationRequested(request.cancellation_token))
        {
            timer.stop();
            if (reply->isRunning())
            {
                reply->abort();
            }
            result.diagnostic = "repository index fetch cancelled";
            result.success = false;
            reply->deleteLater();
            return result;
        }

        if (timer.isActive())
        {
            timer.stop();
        }
        else if (reply->isRunning())
        {
            reply->abort();
            result.diagnostic = "repository index fetch timed out";
            result.success = false;
            if (an::platform::isLoggingInitialized())
            {
                LOG_WARNING("Extension repository index fetch timed out url='{}' timeout_ms={}",
                            result.index_url.toString().toStdString(), request.timeout_ms);
            }
            reply->deleteLater();
            return result;
        }

        if (reply->error() != QNetworkReply::NoError)
        {
            result.diagnostic = "repository index fetch failed: " + reply->errorString().toStdString();
            result.success = false;
            if (an::platform::isLoggingInitialized())
            {
                LOG_WARNING("Extension repository index fetch failed url='{}' error='{}'",
                            result.index_url.toString().toStdString(), reply->errorString().toStdString());
            }
            reply->deleteLater();
            return result;
        }

        const QVariant status_code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
        if (status_code.isValid() && status_code.toInt() >= 400)
        {
            result.diagnostic = "repository index fetch failed with HTTP status " + std::to_string(status_code.toInt());
            result.success = false;
            if (an::platform::isLoggingInitialized())
            {
                LOG_WARNING("Extension repository index fetch failed url='{}' http_status={}",
                            result.index_url.toString().toStdString(), status_code.toInt());
            }
            reply->deleteLater();
            return result;
        }

        result.validation = validateFetchedRepositoryIndexPayload(reply->readAll());
        result.success = result.validation.valid;
        result.diagnostic = result.validation.diagnostic;
        reply->deleteLater();
    }
    else
    {
        result.diagnostic = "unsupported repository index URL scheme";
        result.success = false;
    }

    if (result.success && request.require_official && !result.validation.index.official)
    {
        result.success = false;
        result.diagnostic = "repository index is not marked official";
    }

    if (an::platform::isLoggingInitialized())
    {
        if (result.success)
        {
            LOG_INFO("Extension repository index fetched url='{}' packages={}",
                     result.index_url.toString().toStdString(),
                     static_cast<int>(result.validation.index.packages.size()));
        }
        else
        {
            LOG_WARNING("Extension repository index fetch rejected url='{}' diagnostic='{}'",
                        result.index_url.toString().toStdString(), result.diagnostic);
        }
    }

    return result;
}

ExtensionPackageExtractionResult extract_extension_package_archive(const std::filesystem::path& archive_path,
                                                                   const std::filesystem::path& destination_root,
                                                                   const ExtensionPackageExtractionOptions& options)
{
    ExtensionPackageExtractionResult result;
    result.destination_root = destination_root;

    bool cleanup_destination = false;
    bool cleanup_temporary_root = false;
    std::filesystem::path temporary_root;
    const auto fail = [&](std::string diagnostic) {
        if (cleanup_temporary_root)
        {
            std::error_code remove_ec;
            std::filesystem::remove_all(temporary_root, remove_ec);
        }
        if (cleanup_destination)
        {
            std::error_code remove_ec;
            std::filesystem::remove_all(destination_root, remove_ec);
        }
        result.success = false;
        result.diagnostic = std::move(diagnostic);
        return result;
    };

    std::error_code ec;
    if (!std::filesystem::is_regular_file(archive_path, ec))
    {
        return fail("Package archive does not exist or is not a regular file");
    }
    const bool is_tar_archive = isTarArchivePath(archive_path);
    const bool is_tar_zstd_archive = isTarZstdArchivePath(archive_path);
    if (!is_tar_archive && !is_tar_zstd_archive)
    {
        return fail("Unsupported package archive format; supported formats are .tar and .tar.zst");
    }
    if (!isDirectoryEmpty(destination_root))
    {
        result.success = false;
        result.diagnostic = "Archive extraction destination must not exist or must be empty";
        return result;
    }

    std::filesystem::path extraction_archive_path = archive_path;
    if (is_tar_zstd_archive)
    {
        const std::string temporary_name = "afternight-extension-archive-" +
                                           std::to_string(QDateTime::currentMSecsSinceEpoch()) + "-" +
                                           std::to_string(reinterpret_cast<std::uintptr_t>(&result));

        temporary_root = std::filesystem::temp_directory_path(ec) / temporary_name;
        if (ec)
        {
            return fail("Failed to resolve temporary directory for compressed package archive");
        }
        if (std::filesystem::exists(temporary_root, ec))
        {
            return fail("Temporary directory for compressed package archive already exists");
        }
        if (ec)
        {
            return fail("Failed to inspect temporary directory for compressed package archive");
        }
        std::filesystem::create_directory(temporary_root, ec);
        if (ec)
        {
            return fail("Failed to create temporary directory for compressed package archive");
        }
        cleanup_temporary_root = true;

        const std::filesystem::path temporary_tar_path = temporary_root / "package.tar";
        std::string decompression_diagnostic;
        if (!decompressZstdArchiveToTar(archive_path, temporary_tar_path, maxTemporaryTarBytes(options.limits),
                                        &decompression_diagnostic))
        {
            return fail(decompression_diagnostic);
        }
        extraction_archive_path = temporary_tar_path;
    }

    std::filesystem::create_directories(destination_root, ec);
    if (ec)
    {
        return fail("Failed to create archive extraction destination");
    }
    cleanup_destination = true;

    std::ifstream archive(extraction_archive_path, std::ios::binary);
    if (!archive.is_open())
    {
        return fail("Failed to open package archive");
    }

    std::set<std::string, std::less<>> seen_paths;
    std::array<char, kTarBlockSize> header{};
    std::optional<std::string> pending_pax_path;
    while (archive.read(header.data(), static_cast<std::streamsize>(header.size())))
    {
        if (isAllZeroBlock(header))
        {
            if (pending_pax_path.has_value())
            {
                return fail("Archive ended after a PAX header without a following entry");
            }
            if (cleanup_temporary_root)
            {
                std::error_code remove_ec;
                std::filesystem::remove_all(temporary_root, remove_ec);
                cleanup_temporary_root = false;
            }
            result.success = true;
            return result;
        }

        const auto stored_checksum = parseTarOctalField(header.data() + 148, 8);
        if (!stored_checksum.has_value())
        {
            return fail("Archive entry has an invalid checksum field");
        }

        std::uint64_t computed_checksum = 0;
        for (std::size_t i = 0; i < header.size(); ++i)
        {
            computed_checksum +=
                (i >= 148 && i < 156) ? static_cast<unsigned char>(' ') : static_cast<unsigned char>(header[i]);
        }
        if (*stored_checksum != computed_checksum)
        {
            return fail("Archive entry checksum mismatch");
        }

        const auto parsed_mode = parseTarOctalField(header.data() + 100, 8);
        const auto parsed_size = parseTarOctalField(header.data() + 124, 12);
        if (!parsed_mode.has_value() || !parsed_size.has_value())
        {
            return fail("Archive entry has invalid mode or size metadata");
        }

        const char type = header[156] == '\0' ? '0' : header[156];
        if (type == 'x' || type == 'g')
        {
            if (*parsed_size > options.limits.max_entry_size_bytes)
            {
                return fail("Archive PAX header exceeds the maximum entry size");
            }

            std::string pax_payload;
            if (!readTarPayloadString(archive, *parsed_size, pax_payload))
            {
                return fail("Archive ended before a PAX header payload was complete");
            }
            if (!skipTarPadding(archive, *parsed_size))
            {
                return fail("Failed to skip archive PAX header padding");
            }

            const auto pax_records = parsePaxKeyValueRecords(pax_payload);
            if (!pax_records.has_value())
            {
                return fail("Archive PAX header is malformed");
            }

            if (type == 'x')
            {
                const auto path_it = pax_records->find("path");
                pending_pax_path =
                    path_it != pax_records->end() ? std::optional<std::string>(path_it->second) : std::nullopt;
            }
            continue;
        }

        std::string entry_name = tarStringField(header.data(), 100);
        const std::string prefix = tarStringField(header.data() + 345, 155);
        if (!prefix.empty())
        {
            entry_name = prefix + "/" + entry_name;
        }
        if (pending_pax_path.has_value())
        {
            entry_name = *pending_pax_path;
            pending_pax_path.reset();
        }

        std::filesystem::path relative_path;
        std::string path_error;
        if (!isArchivePathSafe(entry_name, options.limits.max_path_depth, &relative_path, &path_error))
        {
            return fail(path_error);
        }

        const std::string normalized_entry = relative_path.generic_string();
        if (!seen_paths.insert(normalized_entry).second)
        {
            return fail("Archive contains duplicate entries");
        }

        if (type == '2' || type == '1' || type == '3' || type == '4' || type == '6')
        {
            return fail("Archive entry uses a link, device, or FIFO type that is not supported");
        }
        if (type != '0' && type != '5')
        {
            return fail("Archive entry type is not supported");
        }

        if (result.file_count + 1 > options.limits.max_file_count)
        {
            return fail("Archive exceeds the maximum file count");
        }
        if (*parsed_size > options.limits.max_entry_size_bytes)
        {
            return fail("Archive entry exceeds the maximum entry size");
        }
        if (result.total_uncompressed_size_bytes + *parsed_size > options.limits.max_total_size_bytes)
        {
            return fail("Archive exceeds the maximum total uncompressed size");
        }

        const std::filesystem::path destination = (destination_root / relative_path).lexically_normal();
        if (!isPathWithinRoot(destination, destination_root.lexically_normal()))
        {
            return fail("Archive entry escapes the extraction destination");
        }
        if (std::filesystem::exists(destination, ec))
        {
            return fail("Archive extraction refuses to overwrite existing files");
        }

        if (type == '5')
        {
            std::filesystem::create_directories(destination, ec);
            if (ec)
            {
                return fail("Failed to create archive directory entry");
            }
            if (!skipTarPadding(archive, *parsed_size))
            {
                return fail("Failed to skip archive directory padding");
            }
            result.extracted_paths.push_back(destination);
            ++result.file_count;
            continue;
        }

        std::filesystem::create_directories(destination.parent_path(), ec);
        if (ec)
        {
            return fail("Failed to create archive file parent directory");
        }

        std::ofstream out(destination, std::ios::binary);
        if (!out.is_open())
        {
            return fail("Failed to create archive file entry");
        }

        std::uint64_t remaining = *parsed_size;
        std::array<char, 64 * 1024> buffer{};
        while (remaining > 0)
        {
            const std::size_t chunk = static_cast<std::size_t>((std::min<std::uint64_t>)(remaining, buffer.size()));
            archive.read(buffer.data(), static_cast<std::streamsize>(chunk));
            if (archive.gcount() != static_cast<std::streamsize>(chunk))
            {
                return fail("Archive ended before an entry payload was complete");
            }
            out.write(buffer.data(), static_cast<std::streamsize>(chunk));
            if (!out.good())
            {
                return fail("Failed to write archive file entry");
            }
            remaining -= chunk;
        }
        out.close();
        if (!out.good())
        {
            return fail("Failed to finalize archive file entry");
        }

        if (options.preserve_executable_permissions)
        {
            std::filesystem::permissions(destination, permissionsFromTarMode(*parsed_mode),
                                         std::filesystem::perm_options::replace, ec);
            if (ec)
            {
                return fail("Failed to apply archive file permissions");
            }
        }

        if (!skipTarPadding(archive, *parsed_size))
        {
            return fail("Failed to skip archive file padding");
        }

        result.extracted_paths.push_back(destination);
        result.total_uncompressed_size_bytes += *parsed_size;
        ++result.file_count;
    }

    if (!archive.eof())
    {
        return fail("Failed while reading package archive");
    }

    return fail("Package archive ended without a terminating block");
}

void reportOperationProgress(const ExtensionOperationProgressCallback& callback, std::string phase_id,
                             std::string message, const int current, const int total, const bool indeterminate)
{
    if (!callback)
    {
        return;
    }

    if (an::platform::isLoggingInitialized())
    {
        LOG_DEBUG("Extension package operation progress phase='{}' current={} total={} indeterminate={} message='{}'",
                  phase_id, current, total, indeterminate, message);
    }
    callback({std::move(phase_id), std::move(message), current, total, indeterminate});
}

void markInstallCancelled(ExtensionPackageInstallResult* result)
{
    if (!result)
    {
        return;
    }

    result->cancelled = true;
    result->diagnostic = "Extension package installation cancelled.";
}

void markRemoveCancelled(ExtensionPackageRemoveResult* result)
{
    if (!result)
    {
        return;
    }

    result->cancelled = true;
    result->diagnostic = "Extension package removal cancelled.";
}

void markRepairCancelled(ExtensionPackageRepairResult* result)
{
    if (!result)
    {
        return;
    }

    result->cancelled = true;
    result->diagnostic = "Installed extension package repair cancelled.";
}

ExtensionStartupCachePrimingResult prime_extension_startup_caches(const ExtensionLoadResult& load_result,
                                                                  const ExtensionStartupCachePrimingOptions& options)
{
    ExtensionStartupCachePrimingResult result;
    result.load_result = load_result;
    if (cancellationRequested(options.cancellation_token))
    {
        result.diagnostic = "Operation cancelled.";
        return result;
    }

    if (!result.load_result.manifest.has_value())
    {
        result.diagnostic = "Cannot prime startup caches without a valid extension manifest";
        return result;
    }
    if (result.load_result.status != ExtensionLoadStatus::Ready)
    {
        result.diagnostic = result.load_result.diagnostic.empty()
                                ? "Cannot prime startup caches for an extension that is not ready"
                                : result.load_result.diagnostic;
        return result;
    }

    ExtensionDiscoveryContext context = normalizeDiscoveryContext(options.context);
    context.eager_settings_schema_inspection = true;

    if (an::platform::isLoggingInitialized())
    {
        LOG_INFO("Priming extension startup caches id='{}' version='{}' source='{}' require_settings_schema={}",
                 result.load_result.manifest->id, result.load_result.manifest->version,
                 sourceId(result.load_result.source), options.require_settings_schema);
    }

    ExtensionEnvironmentManager environment_manager;
    result.load_result.environment = environment_manager.inspectEnvironment(
        *result.load_result.manifest, result.load_result.source, context, options.cancellation_token);
    result.environment_ready = result.load_result.environment.has_value() &&
                               result.load_result.environment->state == ExtensionEnvironmentState::Ready;

    if (!result.environment_ready)
    {
        result.diagnostic =
            result.load_result.environment.has_value() && !result.load_result.environment->diagnostic.empty()
                ? result.load_result.environment->diagnostic
                : "Cannot prime startup caches until the extension environment is ready";
        return result;
    }

    result.load_result.settings_schema = inspectSettingsSchemaForResult(
        result.load_result, context, options.cancellation_token, options.progress_callback);
    result.settings_schema_ready = result.load_result.settings_schema.has_value() &&
                                   result.load_result.settings_schema->state == ExtensionSettingsSchemaState::Ready;
    result.settings_schema_from_cache =
        result.load_result.settings_schema.has_value() && result.load_result.settings_schema->from_cache;

    if (options.require_settings_schema && !result.settings_schema_ready)
    {
        if (cancellationRequested(options.cancellation_token))
        {
            result.diagnostic = "Operation cancelled.";
            return result;
        }

        result.diagnostic =
            result.load_result.settings_schema.has_value() && !result.load_result.settings_schema->diagnostic.empty()
                ? result.load_result.settings_schema->diagnostic
                : "Settings schema cache priming did not complete";
        return result;
    }

    result.success = true;
    result.diagnostic = "Startup caches are primed for the ready extension environment.";
    if (an::platform::isLoggingInitialized())
    {
        LOG_INFO("Primed extension startup caches id='{}' version='{}' settings_schema_ready={} "
                 "settings_schema_from_cache={}",
                 result.load_result.manifest->id, result.load_result.manifest->version, result.settings_schema_ready,
                 result.settings_schema_from_cache);
    }
    return result;
}

ExtensionPackageInstallResult
ExtensionPackageTransactionService::installArchive(const ExtensionPackageInstallRequest& request) const
{
    const int startup_cache_progress_step_count = request.prime_startup_caches ? kInstallStartupCacheStepCount : 0;
    const int provision_progress_step_count = request.provision_environment ? kInstallProvisionStepCount : 0;
    const int install_progress_total_units =
        kInstallProgressTotal + provision_progress_step_count + startup_cache_progress_step_count;
    const int install_progress_total = install_progress_total_units * kInstallProgressScale;
    const int precompile_bytecode_step = 6 + provision_progress_step_count;
    const int prime_startup_caches_step = 7 + provision_progress_step_count;
    const int activate_package_step = 8 + provision_progress_step_count + startup_cache_progress_step_count;
    const int refresh_installed_package_step = 9 + provision_progress_step_count + startup_cache_progress_step_count;
    const int complete_step = kInstallProgressTotal + provision_progress_step_count + startup_cache_progress_step_count;
    const std::filesystem::path install_root =
        request.install_root.empty() ? repository_extensions_root() : request.install_root;
    const auto report = [&](const int current, std::string phase_id, std::string message) {
        reportOperationProgress(request.progress_callback, std::move(phase_id), std::move(message),
                                current * kInstallProgressScale, install_progress_total, false);
    };
    const ExtensionOperationProgressCallback provisioning_progress_callback =
        request.progress_callback
            ? ExtensionOperationProgressCallback([&](const an::ui::extensions::ExtensionOperationProgress& progress) {
                  int mapped_current = kInstallProvisionStartStep * kInstallProgressScale;
                  if (progress.total > 0)
                  {
                      const int current = std::clamp(progress.current, 0, progress.total);
                      mapped_current += static_cast<int>(
                          (static_cast<long long>(current) * kInstallProvisionStepCount * kInstallProgressScale +
                           progress.total - 1) /
                          progress.total);
                  }

                  reportOperationProgress(
                      request.progress_callback, progress.phase_id, progress.message,
                      std::clamp(mapped_current, kInstallProvisionStartStep * kInstallProgressScale,
                                 (kInstallProvisionStartStep + kInstallProvisionStepCount) * kInstallProgressScale),
                      install_progress_total, false);
              })
            : ExtensionOperationProgressCallback{};
    const ExtensionOperationProgressCallback package_precompile_progress_callback =
        request.progress_callback
            ? ExtensionOperationProgressCallback([&](const an::ui::extensions::ExtensionOperationProgress& progress) {
                  const int precompile_bytecode_start = precompile_bytecode_step * kInstallProgressScale;
                  int mapped_current = precompile_bytecode_start;
                  if (progress.total > 0)
                  {
                      const int current = std::clamp(progress.current, 0, progress.total);
                      mapped_current += static_cast<int>(
                          (static_cast<long long>(current) * kInstallProgressScale + progress.total - 1) /
                          progress.total);
                  }

                  reportOperationProgress(request.progress_callback, progress.phase_id, progress.message,
                                          std::clamp(mapped_current, precompile_bytecode_start,
                                                     precompile_bytecode_start + kInstallProgressScale),
                                          install_progress_total, false);
              })
            : ExtensionOperationProgressCallback{};
    const ExtensionOperationProgressCallback startup_cache_progress_callback =
        request.progress_callback
            ? ExtensionOperationProgressCallback([&](const an::ui::extensions::ExtensionOperationProgress& progress) {
                  const int prime_startup_caches_start = prime_startup_caches_step * kInstallProgressScale;
                  int mapped_current = prime_startup_caches_start;
                  if (!progress.indeterminate && progress.total > 0)
                  {
                      const int current = std::clamp(progress.current, 0, progress.total);
                      mapped_current += static_cast<int>(
                          (static_cast<long long>(current) * kInstallStartupCacheStepCount * kInstallProgressScale +
                           progress.total - 1) /
                          progress.total);
                  }

                  reportOperationProgress(
                      request.progress_callback, progress.phase_id, progress.message,
                      std::clamp(mapped_current, prime_startup_caches_start,
                                 prime_startup_caches_start + kInstallStartupCacheStepCount * kInstallProgressScale),
                      install_progress_total, false);
              })
            : ExtensionOperationProgressCallback{};

    ExtensionPackageInstallResult result;
    if (an::platform::isLoggingInitialized())
    {
        LOG_INFO("Starting extension package install source='{}' archive='{}' install_root='{}' selected_asset='{}' "
                 "repository_url='{}' provision_environment={} prime_startup_caches={}",
                 sourceId(request.source), request.archive_path.generic_string(), install_root.generic_string(),
                 request.selected_release_asset, request.source_repository_url, request.provision_environment,
                 request.prime_startup_caches);
    }

    const auto cancel_if_requested = [&]() -> bool {
        if (!cancellationRequested(request.cancellation_token))
        {
            return false;
        }

        markInstallCancelled(&result);
        return true;
    };

    if (cancel_if_requested())
    {
        return result;
    }

    report(0, "start_install", "Starting extension package installation");
    report(1, "verify_archive", "Verifying package archive");
    if (cancel_if_requested())
    {
        return result;
    }
    result.archive_hash_sha256 = sha256FileHex(request.archive_path);
    if (result.archive_hash_sha256.empty())
    {
        result.diagnostic = "Failed to hash package archive";
        return result;
    }

    if (!request.expected_archive_hash_sha256.empty() &&
        result.archive_hash_sha256 != request.expected_archive_hash_sha256)
    {
        result.diagnostic = "Package archive SHA-256 does not match the expected hash";
        return result;
    }

    if (const auto trust_diagnostic = installTrustPolicyDiagnostic(request, m_trusted_keys);
        trust_diagnostic.has_value())
    {
        result.diagnostic = *trust_diagnostic;
        return result;
    }
    if (request.source == ExtensionPackageSource::Repository &&
        request.signature_state == ExtensionPackageSignatureState::Verified)
    {
        std::string signature_diagnostic;
        if (!verifyRepositoryAssetSignature(request, result.archive_hash_sha256, m_trusted_keys, &signature_diagnostic))
        {
            result.diagnostic =
                signature_diagnostic.empty() ? "Package signature verification failed" : signature_diagnostic;
            return result;
        }
    }

    std::error_code ec;
    const bool install_root_preexisted = std::filesystem::exists(install_root, ec);
    ec.clear();
    std::filesystem::create_directories(install_root, ec);
    if (ec)
    {
        result.diagnostic = "Failed to create extension install root";
        return result;
    }

    const std::string token = generateOpaqueToken();
    result.staging_root = install_root / ("._staging_" + token);
    const std::filesystem::path extracted_root = result.staging_root / "payload";
    const auto cleanup_failed_staging = [&]() {
        std::filesystem::remove_all(result.staging_root, ec);
        if (!install_root_preexisted)
        {
            std::error_code cleanup_ec;
            if (std::filesystem::exists(install_root, cleanup_ec) &&
                std::filesystem::is_empty(install_root, cleanup_ec))
            {
                std::filesystem::remove(install_root, cleanup_ec);
            }
        }
    };

    report(2, "extract_archive", "Extracting package archive");
    if (cancel_if_requested())
    {
        cleanup_failed_staging();
        return result;
    }
    const auto extraction =
        extract_extension_package_archive(request.archive_path, extracted_root, request.extraction_options);
    if (!extraction.success)
    {
        result.diagnostic = extraction.diagnostic;
        cleanup_failed_staging();
        return result;
    }

    const auto package_root = locateExtractedPackageRoot(extracted_root);
    if (!package_root.has_value())
    {
        result.diagnostic = "Package archive must contain exactly one extension.json package root";
        cleanup_failed_staging();
        return result;
    }

    report(3, "validate_package", "Validating extension package");
    if (cancel_if_requested())
    {
        cleanup_failed_staging();
        return result;
    }
    ExtensionPackageValidationOptions validation_options;
    validation_options.context = request.context;
    validation_options.source = request.source;
    validation_options.require_repository_metadata = request.require_repository_metadata;
    validation_options.require_current_runtime_compatibility = request.require_current_runtime_compatibility;
    result.validation = validate_extension_package_root(*package_root, validation_options);
    if (!result.validation.valid || !result.validation.load_result.manifest.has_value())
    {
        result.diagnostic =
            result.validation.issues.empty() ? "Package validation failed" : result.validation.issues.front().message;
        cleanup_failed_staging();
        return result;
    }

    const ExtensionManifest& manifest = *result.validation.load_result.manifest;
    if (request.source == ExtensionPackageSource::Repository)
    {
        if (!request.expected_extension_id.empty() && manifest.id != request.expected_extension_id)
        {
            result.diagnostic = "Package manifest id does not match the repository index entry";
            cleanup_failed_staging();
            return result;
        }
        if (!request.expected_extension_version.empty() && manifest.version != request.expected_extension_version)
        {
            result.diagnostic = "Package manifest version does not match the repository index entry";
            cleanup_failed_staging();
            return result;
        }
    }
    const std::filesystem::path activated_root = install_root / sanitizeIdentifier(manifest.id);
    const std::filesystem::path backup_root = install_root / (sanitizeIdentifier(manifest.id) + "._backup_" + token);
    result.activated_package_root = activated_root;
    if (an::platform::isLoggingInitialized())
    {
        LOG_DEBUG("Validated extension package install id='{}' version='{}' package_root='{}' source='{}'", manifest.id,
                  manifest.version, package_root->generic_string(), sourceId(request.source));
    }

    const std::string source_tier = sourceId(request.source);
    QJsonObject receipt;
    receipt.insert(QStringLiteral("receipt_version"), 1);
    receipt.insert(QStringLiteral("extension_id"), QString::fromStdString(manifest.id));
    receipt.insert(QStringLiteral("extension_version"), QString::fromStdString(manifest.version));
    receipt.insert(QStringLiteral("source_tier"), QString::fromStdString(source_tier));
    receipt.insert(QStringLiteral("source_repository_url"), QString::fromStdString(request.source_repository_url));
    receipt.insert(QStringLiteral("local_package_path"), QString::fromStdString(request.archive_path.string()));
    receipt.insert(QStringLiteral("installed_at_utc"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    receipt.insert(QStringLiteral("archive_hash"), QString::fromStdString(result.archive_hash_sha256));
    receipt.insert(QStringLiteral("selected_release_asset"), QString::fromStdString(request.selected_release_asset));
    receipt.insert(QStringLiteral("signature_state"),
                   QString::fromStdString(signatureStateId(request.signature_state)));
    receipt.insert(QStringLiteral("signature_detail"), QString::fromStdString(request.signature_detail));
    receipt.insert(QStringLiteral("signature_algorithm"), QString::fromStdString(request.signature_algorithm));
    receipt.insert(QStringLiteral("signature_key_id"), QString::fromStdString(request.signature_key_id));
    ExtensionEnvironmentManager environment_manager;
    receipt.insert(QStringLiteral("host_bootstrap_fingerprint"),
                   QString::fromStdString(environment_manager.computeHostBootstrapFingerprint(request.context)));

    report(4, "write_receipt", "Writing package provenance receipt");
    if (cancel_if_requested())
    {
        cleanup_failed_staging();
        return result;
    }
    const std::string receipt_text = QJsonDocument(receipt).toJson(QJsonDocument::Indented).toStdString();
    std::string write_error;
    if (!writeTextFileAtomic(*package_root / std::string(kPackageProvenanceReceiptName), receipt_text, &write_error))
    {
        result.diagnostic = write_error;
        cleanup_failed_staging();
        return result;
    }

    const auto remove_staging_root = [&]() { std::filesystem::remove_all(result.staging_root, ec); };
    const auto remove_private_environment_after_failed_gate =
        [&](const std::optional<an::ui::extensions::ExtensionEnvironmentSnapshot>& snapshot) {
            if (!snapshot.has_value() ||
                manifest.dependencies.dependency_context != ExtensionDependencyContext::Private)
            {
                return;
            }

            std::string remove_error;
            if (!environment_manager.removeEnvironmentDirectory(snapshot->environment_path, &remove_error) &&
                an::platform::isLoggingInitialized())
            {
                LOG_WARNING(
                    "Failed to remove private extension environment after install rollback extension id='{}' env='{}' "
                    "reason={}",
                    manifest.id, snapshot->environment_path.generic_string(), remove_error);
            }
        };

    if (request.provision_environment)
    {
        report(5, "provision_environment", "Creating extension environment and installing dependencies");
        if (cancel_if_requested())
        {
            remove_staging_root();
            return result;
        }
        std::vector<an::ui::extensions::ExtensionInstalledDistribution> resolved_distributions;
        an::ui::extensions::ExtensionEnvironmentSnapshot provisioned_environment;
        std::string provision_error;
        if (!environment_manager.provisionEnvironmentForManifest(
                manifest, request.source, request.context, &provisioned_environment, &resolved_distributions,
                &provision_error, provisioning_progress_callback, request.cancellation_token))
        {
            result.environment = provisioned_environment;
            if (an::platform::isLoggingInitialized())
            {
                LOG_WARNING(
                    "Extension package install failed during environment provisioning for extension id='{}' "
                    "version='{}' archive='{}' env='{}' dependency_fingerprint='{}' validation_fingerprint='{}' "
                    "reason={}",
                    manifest.id, manifest.version, request.archive_path.generic_string(),
                    provisioned_environment.environment_path.generic_string(),
                    provisioned_environment.dependency_fingerprint, provisioned_environment.validation_fingerprint,
                    provision_error);
            }
            if (cancellationRequested(request.cancellation_token))
            {
                markInstallCancelled(&result);
            }
            else
            {
                result.diagnostic =
                    provision_error.empty() ? "Extension environment provisioning failed" : provision_error;
            }
            remove_private_environment_after_failed_gate(result.environment);
            remove_staging_root();
            return result;
        }

        result.environment = provisioned_environment;
        result.validation.load_result.environment = provisioned_environment;
    }
    else if (result.validation.load_result.environment.has_value())
    {
        result.environment = result.validation.load_result.environment;
    }

    if (request.precompile_python_bytecode && result.environment.has_value() &&
        result.environment->state == ExtensionEnvironmentState::Ready)
    {
        report(precompile_bytecode_step, "precompile_bytecode", "Precompiling extension Python bytecode");
        if (cancel_if_requested())
        {
            if (request.provision_environment)
            {
                remove_private_environment_after_failed_gate(result.environment);
            }
            remove_staging_root();
            return result;
        }
        std::string precompile_error;
        if (!environment_manager.precompilePackageBytecode(manifest, *result.environment, &precompile_error,
                                                           package_precompile_progress_callback,
                                                           request.cancellation_token))
        {
            if (cancellationRequested(request.cancellation_token))
            {
                markInstallCancelled(&result);
            }
            else
            {
                result.diagnostic =
                    precompile_error.empty() ? "Extension package bytecode precompilation failed" : precompile_error;
            }
            if (request.provision_environment)
            {
                remove_private_environment_after_failed_gate(result.environment);
            }
            remove_staging_root();
            return result;
        }
        result.package_bytecode_precompiled = true;
    }

    removeSettingsSchemaCacheForExtension(manifest.id, request.context);

    if (request.prime_startup_caches)
    {
        report(prime_startup_caches_step, "prime_startup_caches", "Priming extension startup caches");
        if (cancel_if_requested())
        {
            if (request.provision_environment)
            {
                remove_private_environment_after_failed_gate(result.environment);
            }
            remove_staging_root();
            return result;
        }
        ExtensionStartupCachePrimingOptions priming_options;
        priming_options.context = request.context;
        priming_options.require_ready_environment = true;
        priming_options.require_settings_schema = request.require_settings_schema_cache;
        priming_options.progress_callback = startup_cache_progress_callback;
        priming_options.cancellation_token = request.cancellation_token;
        result.startup_cache_priming = prime_extension_startup_caches(result.validation.load_result, priming_options);

        if (!result.startup_cache_priming->success)
        {
            if (cancellationRequested(request.cancellation_token))
            {
                markInstallCancelled(&result);
            }
            else
            {
                result.diagnostic = result.startup_cache_priming->diagnostic.empty()
                                        ? "Extension startup cache priming failed"
                                        : result.startup_cache_priming->diagnostic;
            }
            if (request.provision_environment)
            {
                remove_private_environment_after_failed_gate(result.environment);
            }
            remove_staging_root();
            return result;
        }

        result.validation.load_result = result.startup_cache_priming->load_result;
        if (result.validation.load_result.environment.has_value())
        {
            result.environment = result.validation.load_result.environment;
        }
    }

    bool backup_created = false;
    if (cancel_if_requested())
    {
        remove_staging_root();
        return result;
    }
    if (std::filesystem::exists(activated_root, ec))
    {
        if (!request.replace_existing)
        {
            result.diagnostic = "Extension package is already installed";
            remove_staging_root();
            return result;
        }

        std::filesystem::rename(activated_root, backup_root, ec);
        if (ec)
        {
            result.diagnostic = "Failed to stage existing extension package for replacement";
            remove_staging_root();
            return result;
        }
        backup_created = true;
    }

    report(activate_package_step, "activate_package", "Activating extension package");
    if (cancel_if_requested())
    {
        if (backup_created)
        {
            std::error_code restore_ec;
            std::filesystem::rename(backup_root, activated_root, restore_ec);
        }
        remove_staging_root();
        return result;
    }
    std::filesystem::rename(*package_root, activated_root, ec);
    if (ec)
    {
        result.diagnostic = "Failed to activate extracted extension package";
        if (backup_created)
        {
            std::error_code restore_ec;
            std::filesystem::rename(backup_root, activated_root, restore_ec);
        }
        remove_staging_root();
        return result;
    }

    report(refresh_installed_package_step, "refresh_installed_package", "Verifying installed extension package");
    result.validation = validate_extension_package_root(
        activated_root, ExtensionPackageValidationOptions{request.context, request.source,
                                                          request.require_current_runtime_compatibility,
                                                          request.require_repository_metadata, false});
    if (!result.validation.valid)
    {
        result.diagnostic = result.validation.issues.empty() ? "Activated package failed validation"
                                                             : result.validation.issues.front().message;
        std::filesystem::remove_all(activated_root, ec);
        if (backup_created)
        {
            std::error_code restore_ec;
            std::filesystem::rename(backup_root, activated_root, restore_ec);
        }
        remove_staging_root();
        return result;
    }

    if (result.validation.load_result.environment.has_value())
    {
        result.environment = result.validation.load_result.environment;
    }

    remove_staging_root();
    if (backup_created)
    {
        std::filesystem::remove_all(backup_root, ec);
    }

    report(complete_step, "complete", "Extension package installation complete");
    if (an::platform::isLoggingInitialized())
    {
        LOG_INFO("Completed extension package install id='{}' version='{}' source='{}' path='{}' archive_hash='{}'",
                 manifest.id, manifest.version, sourceId(request.source),
                 result.activated_package_root.generic_string(), result.archive_hash_sha256);
    }
    result.success = true;
    return result;
}

ExtensionPackageRemoveResult
ExtensionPackageTransactionService::removeInstalledPackage(const ExtensionPackageRemoveRequest& request) const
{
    constexpr int kRemoveProgressTotal = 4;
    const auto report = [&](const int current, std::string phase_id, std::string message) {
        reportOperationProgress(request.progress_callback, std::move(phase_id), std::move(message), current,
                                kRemoveProgressTotal, false);
    };

    ExtensionPackageRemoveResult result;
    const auto cancel_if_requested = [&]() -> bool {
        if (!cancellationRequested(request.cancellation_token))
        {
            return false;
        }

        markRemoveCancelled(&result);
        return true;
    };

    if (cancel_if_requested())
    {
        return result;
    }

    report(1, "locate_package", "Locating installed extension package");
    if (cancel_if_requested())
    {
        return result;
    }
    const std::filesystem::path install_root =
        request.install_root.empty() ? repository_extensions_root() : request.install_root;

    if (request.extension_id.empty())
    {
        result.diagnostic = "Extension id is required for package removal";
        return result;
    }
    if (an::platform::isLoggingInitialized())
    {
        LOG_INFO("Starting extension package uninstall id='{}' install_root='{}' require_existing={}",
                 request.extension_id, install_root.generic_string(), request.require_existing);
    }

    const std::filesystem::path package_root = install_root / sanitizeIdentifier(request.extension_id);
    result.removed_package_root = package_root;

    std::error_code ec;
    if (!std::filesystem::exists(package_root, ec))
    {
        if (request.require_existing)
        {
            result.diagnostic = "Installed extension package was not found";
            return result;
        }

        result.success = true;
        result.diagnostic = "Installed extension package was already absent.";
        if (an::platform::isLoggingInitialized())
        {
            LOG_INFO("Extension package uninstall skipped id='{}' path='{}' reason='already absent'",
                     request.extension_id, package_root.generic_string());
        }
        return result;
    }
    if (!std::filesystem::is_directory(package_root, ec))
    {
        result.diagnostic = "Installed extension package path is not a directory";
        return result;
    }

    const std::filesystem::path manifest_path = package_root / "extension.json";
    if (!std::filesystem::is_regular_file(manifest_path, ec))
    {
        result.diagnostic = "Installed extension package is missing extension.json";
        return result;
    }

    const std::filesystem::path quarantine_root =
        install_root / ("._remove_" + sanitizeIdentifier(request.extension_id) + "_" + generateOpaqueToken());
    report(2, "stage_removal", "Staging extension package removal");
    if (cancel_if_requested())
    {
        return result;
    }
    std::filesystem::rename(package_root, quarantine_root, ec);
    if (ec)
    {
        result.diagnostic = "Failed to stage installed extension package for removal";
        return result;
    }

    report(3, "delete_package", "Deleting extension package files");
    if (cancel_if_requested())
    {
        std::error_code restore_ec;
        std::filesystem::rename(quarantine_root, package_root, restore_ec);
        return result;
    }
    std::filesystem::remove_all(quarantine_root, ec);
    if (ec)
    {
        std::error_code restore_ec;
        std::filesystem::rename(quarantine_root, package_root, restore_ec);
        result.diagnostic = "Failed to delete installed extension package";
        return result;
    }

    report(4, "complete", "Extension package removal complete");
    result.success = true;
    result.diagnostic = "Installed extension package removed.";
    if (an::platform::isLoggingInitialized())
    {
        LOG_INFO("Completed extension package uninstall id='{}' path='{}'", request.extension_id,
                 package_root.generic_string());
    }
    return result;
}

ExtensionPackageRepairResult
ExtensionPackageTransactionService::repairInstalledPackage(const ExtensionPackageRepairRequest& request) const
{
    constexpr int kRepairProgressTotal = 5;
    const auto report = [&](const int current, std::string phase_id, std::string message) {
        reportOperationProgress(request.progress_callback, std::move(phase_id), std::move(message), current,
                                kRepairProgressTotal, false);
    };

    ExtensionPackageRepairResult result;
    const auto cancel_if_requested = [&]() -> bool {
        if (!cancellationRequested(request.cancellation_token))
        {
            return false;
        }

        markRepairCancelled(&result);
        return true;
    };

    if (cancel_if_requested())
    {
        return result;
    }

    report(1, "validate_package", "Validating installed extension package");
    if (cancel_if_requested())
    {
        return result;
    }
    const std::filesystem::path install_root =
        request.install_root.empty() ? repository_extensions_root() : request.install_root;

    if (request.extension_id.empty())
    {
        result.diagnostic = "Extension id is required for package repair";
        return result;
    }

    const std::filesystem::path package_root = install_root / sanitizeIdentifier(request.extension_id);
    result.repaired_package_root = package_root;

    std::error_code ec;
    if (!std::filesystem::exists(package_root, ec))
    {
        if (request.require_existing)
        {
            result.diagnostic = "Installed extension package was not found";
            return result;
        }

        result.success = true;
        result.diagnostic = "Installed extension package was already absent.";
        return result;
    }
    if (!std::filesystem::is_directory(package_root, ec))
    {
        result.diagnostic = "Installed extension package path is not a directory";
        return result;
    }

    const std::filesystem::path manifest_path = package_root / "extension.json";
    if (!std::filesystem::is_regular_file(manifest_path, ec))
    {
        result.diagnostic = "Installed extension package is missing extension.json";
        return result;
    }

    ExtensionPackageValidationOptions validation_options;
    validation_options.context = request.context;
    validation_options.source = request.source;
    validation_options.require_repository_metadata = request.require_repository_metadata;
    validation_options.require_current_runtime_compatibility = request.require_current_runtime_compatibility;
    result.validation = validate_extension_package_root(package_root, validation_options);
    if (!result.validation.valid || !result.validation.load_result.manifest.has_value())
    {
        result.diagnostic =
            result.validation.issues.empty() ? "Package validation failed" : result.validation.issues.front().message;
        return result;
    }

    const ExtensionManifest& manifest = *result.validation.load_result.manifest;
    if (manifest.id != request.extension_id)
    {
        result.diagnostic = "Installed extension package id does not match the repair request";
        return result;
    }

    ExtensionEnvironmentManager environment_manager;
    if (request.provision_environment)
    {
        report(2, "provision_environment", "Repairing extension environment");
        if (cancel_if_requested())
        {
            return result;
        }
        std::vector<an::ui::extensions::ExtensionInstalledDistribution> resolved_distributions;
        an::ui::extensions::ExtensionEnvironmentSnapshot provisioned_environment;
        std::string provision_error;
        if (!environment_manager.provisionEnvironmentForManifest(
                manifest, request.source, request.context, &provisioned_environment, &resolved_distributions,
                &provision_error, request.progress_callback, request.cancellation_token))
        {
            result.environment = provisioned_environment;
            if (cancellationRequested(request.cancellation_token))
            {
                markRepairCancelled(&result);
            }
            else
            {
                result.diagnostic = provision_error.empty() ? "Extension environment repair failed" : provision_error;
            }
            return result;
        }

        result.environment = provisioned_environment;
        result.validation.load_result.environment = provisioned_environment;
    }
    else if (result.validation.load_result.environment.has_value())
    {
        result.environment = result.validation.load_result.environment;
    }

    if (request.precompile_python_bytecode && result.environment.has_value() &&
        result.environment->state == ExtensionEnvironmentState::Ready)
    {
        report(3, "precompile_bytecode", "Precompiling extension Python bytecode");
        if (cancel_if_requested())
        {
            return result;
        }
        std::string precompile_error;
        if (!environment_manager.precompilePackageBytecode(manifest, *result.environment, &precompile_error, {},
                                                           request.cancellation_token))
        {
            if (cancellationRequested(request.cancellation_token))
            {
                markRepairCancelled(&result);
            }
            else
            {
                result.diagnostic =
                    precompile_error.empty() ? "Extension package bytecode precompilation failed" : precompile_error;
            }
            return result;
        }
        result.package_bytecode_precompiled = true;
    }

    if (request.prime_startup_caches)
    {
        report(4, "prime_startup_caches", "Priming extension startup caches");
        if (cancel_if_requested())
        {
            return result;
        }
        ExtensionStartupCachePrimingOptions priming_options;
        priming_options.context = request.context;
        priming_options.require_ready_environment = true;
        priming_options.require_settings_schema = request.require_settings_schema_cache;
        priming_options.cancellation_token = request.cancellation_token;
        result.startup_cache_priming = prime_extension_startup_caches(result.validation.load_result, priming_options);

        if (!result.startup_cache_priming->success)
        {
            if (cancellationRequested(request.cancellation_token))
            {
                markRepairCancelled(&result);
            }
            else
            {
                result.diagnostic = result.startup_cache_priming->diagnostic.empty()
                                        ? "Extension startup cache priming failed"
                                        : result.startup_cache_priming->diagnostic;
            }
            return result;
        }

        result.validation.load_result = result.startup_cache_priming->load_result;
        if (result.validation.load_result.environment.has_value())
        {
            result.environment = result.validation.load_result.environment;
        }
    }

    report(5, "complete", "Installed extension package repair complete");
    result.success = true;
    result.diagnostic = "Installed extension package repaired.";
    return result;
}

std::vector<ExtensionLoadResult> ExtensionManager::discoverExtensions(const ExtensionDiscoveryOptions& options) const
{
    const auto cancellation_requested = [&options]() { return cancellationRequested(options.cancellation_token); };
    if (cancellation_requested())
    {
        return {};
    }

    ExtensionDiscoveryContext context = normalizeDiscoveryContext(options.context);

    std::vector<ExtensionLoadResult> results;
    const auto append_result_if_allowed = [&results, &options](ExtensionLoadResult result) {
        if (result.manifest.has_value() && result.manifest->development_only &&
            !options.include_development_only_extensions)
        {
            return;
        }

        results.push_back(std::move(result));
    };
    const auto should_skip_development_only = [&options](const std::filesystem::path& manifest_path) {
        return !options.include_development_only_extensions && manifestFileDeclaresDevelopmentOnly(manifest_path);
    };

    const auto bundled_manifests = discoverManifestPaths(options.bundled_roots, options.cancellation_token);
    for (const auto& manifest_path : bundled_manifests)
    {
        if (cancellation_requested())
        {
            return {};
        }
        if (should_skip_development_only(manifest_path))
        {
            continue;
        }
        append_result_if_allowed(loadExtensionManifestMetadata(manifest_path, ExtensionPackageSource::Bundled, context,
                                                               options.cancellation_token));
    }

    const auto repository_manifests = discoverManifestPaths(options.repository_roots, options.cancellation_token);
    for (const auto& manifest_path : repository_manifests)
    {
        if (cancellation_requested())
        {
            return {};
        }
        if (should_skip_development_only(manifest_path))
        {
            continue;
        }
        append_result_if_allowed(loadExtensionManifestMetadata(manifest_path, ExtensionPackageSource::Repository,
                                                               context, options.cancellation_token));
    }

    const auto local_package_manifests = discoverManifestPaths(options.local_package_roots, options.cancellation_token);
    for (const auto& manifest_path : local_package_manifests)
    {
        if (cancellation_requested())
        {
            return {};
        }
        if (should_skip_development_only(manifest_path))
        {
            continue;
        }
        append_result_if_allowed(loadExtensionManifestMetadata(manifest_path, ExtensionPackageSource::LocalPackage,
                                                               context, options.cancellation_token));
    }

    const auto user_manifests = discoverManifestPaths(options.user_roots, options.cancellation_token);
    for (const auto& manifest_path : user_manifests)
    {
        if (cancellation_requested())
        {
            return {};
        }
        if (should_skip_development_only(manifest_path))
        {
            continue;
        }
        append_result_if_allowed(loadExtensionManifestMetadata(manifest_path, ExtensionPackageSource::User, context,
                                                               options.cancellation_token));
    }

    if (cancellation_requested())
    {
        return {};
    }

    applyDuplicatePrecedence(results);

    ExtensionEnvironmentManager environment_manager;
    for (auto& result : results)
    {
        if (cancellation_requested())
        {
            return {};
        }
        if (!result.manifest || result.status != ExtensionLoadStatus::Ready)
        {
            continue;
        }

        result.environment = environment_manager.inspectEnvironment(*result.manifest, result.source, context,
                                                                    options.cancellation_token);
    }

    applySharedDependencyContextConsistency(results);
    eagerlyInspectSettingsSchemas(&results, context, options.cancellation_token);
    if (cancellation_requested())
    {
        return {};
    }
    ExtensionRuntimeStatusRegistry::instance().mergeIntoDiscoveryResults(&results);
    return results;
}

void ExtensionManager::registerDiscoveredProcesses(const std::vector<ExtensionLoadResult>& discovered,
                                                   process_registry::ProcessRegistry& registry) const
{
    registry.removeExtensionProcesses();

    const auto display_name_collisions = collectDisplayNameCollisions(discovered);
    for (const auto& [display_name, collision] : display_name_collisions)
    {
        if (collision.count < 2)
        {
            continue;
        }

        if (an::platform::isLoggingInitialized())
        {
            LOG_WARNING("Disambiguating duplicate extension process label='{}' across {} manifests", display_name,
                        collision.count);
        }
    }

    for (const ExtensionLoadResult& result : discovered)
    {
        if (!result.manifest || result.status == ExtensionLoadStatus::Invalid)
        {
            continue;
        }

        for (const ExtensionProcessManifest& process : result.manifest->processes)
        {
            ProcessDescriptor descriptor;
            descriptor.id = process.id;
            descriptor.display_name = buildDisambiguatedDisplayName(*result.manifest, process, display_name_collisions);
            descriptor.category = process.category;
            descriptor.action_object_name = process.action_object_name;
            descriptor.process_name = process.process_name;
            descriptor.launch_mode = toRegistryLaunchMode(result.manifest->launch_mode);
            descriptor.requires_exclusive_access = result.manifest->requires_exclusive_access;
            descriptor.source_channel_filter = process.capabilities.source_channel_filter;
            descriptor.sort_order = 1000;
            descriptor.availability = extensionProcessAvailability(result);
            descriptor.availability_reason = extensionDescriptorAvailabilityReason(result);
            descriptor.is_extension_process = true;
            descriptor.extension_id = result.manifest->id;
            descriptor.provides_starless_output = process.capabilities.starless_generator;
            descriptor.provides_star_mask_output = process.capabilities.star_mask_generator;

            if (!registry.registerProcess(std::move(descriptor)))
            {
                LOG_WARNING("Extension process registration skipped duplicate descriptor id={}", process.id);
            }
        }
    }
}

} // namespace an::ui::extensions
