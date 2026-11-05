// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"

#include "app/ExtensionEnvironmentManager.h"
#include "app/ApplicationStorage.h"
#include "app/ExtensionCommon.h"

#include <AfterNightPlatform/Logging.h>

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QProcess>
#include <QProcessEnvironment>
#include <QUuid>

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cctype>
#include <condition_variable>
#include <fstream>
#include <functional>
#include <iterator>
#include <limits>
#include <map>
#include <memory>
#ifndef _WIN32
#include <signal.h>
#else
#include <windows.h>
#endif
#include <regex>
#include <set>
#include <sstream>
#include <thread>

namespace
{

using an::ui::extensions::ExtensionDependencyContext;
using an::ui::extensions::ExtensionDependencySpec;
using an::ui::extensions::ExtensionDiscoveryContext;
using an::ui::extensions::ExtensionEnvironmentInspectionMode;
using an::ui::extensions::ExtensionEnvironmentInventoryEntry;
using an::ui::extensions::ExtensionEnvironmentSnapshot;
using an::ui::extensions::ExtensionEnvironmentState;
using an::ui::extensions::ExtensionInstalledDistribution;
using an::ui::extensions::ExtensionLoadResult;
using an::ui::extensions::ExtensionManifest;
using an::ui::extensions::ExtensionOperationCancellationToken;
using an::ui::extensions::ExtensionOperationProgress;
using an::ui::extensions::ExtensionOperationProgressCallback;
using an::ui::extensions::ExtensionPackageSource;
using an::ui::extensions::ExtensionSharedHostProfileFingerprintSource;
using an::ui::extensions::isRegularFile;
using an::ui::extensions::isUrlLike;
using an::ui::extensions::readFileText;

constexpr std::string_view kReadyMarkerName = ".environment_ready";
constexpr std::string_view kProvisioningLockName = ".provisioning.lock";
constexpr std::string_view kInstallReceiptName = "install_receipt.json";
constexpr std::string_view kFailureDiagnosticName = "provision_failure.txt";
constexpr std::string_view kPipInstallReportName = ".pip_install_report.json";
constexpr int kOfflineProvisioningTimeoutMs = 2 * 60 * 1000;
constexpr int kOnlineProvisioningTimeoutMs = 10 * 60 * 1000;
constexpr int kBytecodePrecompileTimeoutMs = 5 * 60 * 1000;
constexpr int kDependencyBytecodePrecompileTimeoutMs = 10 * 60 * 1000;
constexpr std::string_view kBytecodeProgressItemPrefix = "AFTERNIGHT_BYTECODE_PROGRESS\t";
constexpr int kBytecodePrecompileMaxWorkerCount = 4;
constexpr std::string_view kBundledPySide6Version = "6.10.3";
constexpr std::string_view kPathTreeHashCacheDirectory = "_path_tree_hash_cache";
constexpr int kPathTreeHashCacheSchemaVersion = 2;
constexpr std::string_view kSharedHostProfileReceiptDirectory = "_shared_host_profile_receipts";
constexpr int kSharedHostProfileReceiptSchemaVersion = 1;
constexpr std::array<std::string_view, 2> kBundledUiRuntimePackagePrefixes = {"PySide6", "shiboken6"};
constexpr std::string_view kScientificCoreSharedHostProfile = "scientific_core";
// The scientific core is NumPy, SciPy and scikit-image with what scikit-image needs to import and run: its lazy loader,
// packaging, imageio, tifffile, Pillow and networkx, each with its distribution record.
constexpr std::array<std::string_view, 11> kScientificCoreRuntimePackagePrefixes = {
    "numpy",   "scipy",    "skimage", "scikit_image", "lazy_loader", "packaging",
    "imageio", "tifffile", "PIL",     "pillow",       "networkx"};
constexpr std::array<std::string_view, 2> kSupportedRuntimeTargets = {"linux-clang-x86_64", "windows-msvc-x86_64"};
constexpr int kProvisionProgressTotal = 1000;
constexpr int kProvisionProgressCreateEnvironmentStart = 0;
constexpr int kProvisionProgressCreateEnvironmentEnd = 50;
constexpr int kProvisionProgressStageRuntimeStart = kProvisionProgressCreateEnvironmentEnd;
constexpr int kProvisionProgressStageRuntimeEnd = 200;
constexpr int kProvisionProgressResolveDependenciesStart = kProvisionProgressStageRuntimeEnd;
constexpr int kProvisionProgressResolveDependenciesEnd = 250;
constexpr int kProvisionProgressInstallDependenciesStart = kProvisionProgressResolveDependenciesEnd;
constexpr int kProvisionProgressInstallDependenciesEnd = 650;
constexpr int kProvisionProgressPrecompileDependenciesStart = kProvisionProgressInstallDependenciesEnd;
constexpr int kProvisionProgressPrecompileDependenciesEnd = kProvisionProgressTotal;
constexpr int kFilesystemRemoveAttempts = 5;
constexpr int kFilesystemRemoveRetryDelayMs = 100;
constexpr std::size_t kFilesystemRemoveRemainingSampleLimit = 12;

struct LockedRequirement
{
    std::string name;
    std::string version;
    std::vector<std::string> sha256;
};

struct WheelArtifactMatch
{
    std::string name;
    std::string version;
    std::filesystem::path path;
    std::string sha256;
    struct ParsedWheelArtifact
    {
        std::string name;
        std::string version;
        std::vector<std::string> python_tags;
        std::vector<std::string> abi_tags;
        std::vector<std::string> platform_tags;

        [[nodiscard]] bool isPurePython() const
        {
            return std::find(abi_tags.begin(), abi_tags.end(), "none") != abi_tags.end() &&
                   std::find(platform_tags.begin(), platform_tags.end(), "any") != platform_tags.end();
        }
    } parsed;
};

bool loadInstallReceipt(const std::filesystem::path& path, QJsonObject* out_object);
[[nodiscard]] std::vector<std::filesystem::path>
sortedFilesUnder(const std::filesystem::path& root,
                 const ExtensionOperationCancellationToken* cancellation_token = nullptr);
[[nodiscard]] std::vector<std::filesystem::directory_entry> sortedDirectoryEntries(const std::filesystem::path& root);
bool removeIfExists(const std::filesystem::path& path, std::string* error);
bool writeFileText(const std::filesystem::path& path, const std::string& contents, std::string* error);
[[nodiscard]] bool isDirectory(const std::filesystem::path& path);
[[nodiscard]] std::string
hashPathTree(const std::filesystem::path& root, const ExtensionOperationCancellationToken* cancellation_token = nullptr,
             ExtensionEnvironmentInspectionMode inspection_mode = ExtensionEnvironmentInspectionMode::Routine,
             const std::filesystem::path& cache_directory = {});

[[nodiscard]] std::string trimTrailingWhitespace(std::string value)
{
    while (!value.empty())
    {
        const unsigned char ch = static_cast<unsigned char>(value.back());
        if (ch != '\r' && ch != '\n' && ch != '\t' && ch != ' ')
        {
            break;
        }

        value.pop_back();
    }

    return value;
}

struct ProvisioningLockMetadata
{
    std::optional<qint64> owner_pid;
    /// When the owner started, as `processStartStamp` reads it; a lock an earlier build wrote names none.
    std::optional<std::uint64_t> owner_started;
};

/// What every EnvironmentPreparationGuard of this process shares. Each caller constructs its own
/// ExtensionEnvironmentManager, so the environments being prepared are the process's, not one manager's.
struct EnvironmentPreparations
{
    /// Orders every read and write of `busyPaths`.
    std::mutex mutex;
    /// Notified when a guard gives its environment back, so a guard waiting for it looks again.
    std::condition_variable released;
    /// The environments an operation of this process prepares now.
    std::set<std::filesystem::path> busyPaths;
    /// The locks this process wrote and has not let go of: of the environments it provisions, refreshes, takes over
    /// or holds queued for maintenance. A lock of this process no operation of it wrote, or one it has let go of, is
    /// one left behind.
    std::set<std::filesystem::path> heldLocks;
};

EnvironmentPreparations g_environmentPreparations;

/// Whether this process wrote the lock and has not let go of it.
[[nodiscard]] bool lockHeldByThisProcess(const std::filesystem::path& lock)
{
    const std::lock_guard guard(g_environmentPreparations.mutex);
    return g_environmentPreparations.heldLocks.contains(lock.lexically_normal());
}

/// Records that this process wrote the lock, and that it has let go of it.
void noteLockHeld(const std::filesystem::path& lock)
{
    const std::lock_guard guard(g_environmentPreparations.mutex);
    g_environmentPreparations.heldLocks.insert(lock.lexically_normal());
}

void noteLockReleased(const std::filesystem::path& lock)
{
    const std::lock_guard guard(g_environmentPreparations.mutex);
    g_environmentPreparations.heldLocks.erase(lock.lexically_normal());
}

[[nodiscard]] ProvisioningLockMetadata readProvisioningLockMetadata(const std::filesystem::path& provisioning_lock)
{
    ProvisioningLockMetadata metadata;

    std::stringstream stream(readFileText(provisioning_lock));
    std::string line;
    while (std::getline(stream, line))
    {
        line = trimTrailingWhitespace(std::move(line));
        const std::size_t separator = line.find('=');
        if (separator == std::string::npos)
        {
            continue;
        }

        const std::string key = line.substr(0, separator);
        const QString value = QString::fromStdString(line.substr(separator + 1));
        bool ok = false;
        if (key == "owner_pid")
        {
            const qlonglong owner_pid = value.toLongLong(&ok);
            if (ok && owner_pid > 0)
            {
                metadata.owner_pid = owner_pid;
            }
        }
        else if (key == "owner_started")
        {
            const qulonglong owner_started = value.toULongLong(&ok);
            if (ok)
            {
                metadata.owner_started = owner_started;
            }
        }
    }

    return metadata;
}

[[nodiscard]] bool isProcessRunning(const qint64 pid)
{
    if (pid <= 0)
    {
        return false;
    }

#ifdef _WIN32
    const HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, static_cast<DWORD>(pid));
    if (!process)
    {
        return false;
    }

    const DWORD wait_result = WaitForSingleObject(process, 0);
    CloseHandle(process);
    return wait_result == WAIT_TIMEOUT;
#else
    return ::kill(static_cast<pid_t>(pid), 0) == 0 || errno == EPERM;
#endif
}

/// When a process started, as the system records it: the stamp a lock carries beside its owner's pid, so a lock whose
/// pid the system has since given to another process is not taken for that process's. Nothing when it cannot be read.
[[nodiscard]] std::optional<std::uint64_t> processStartStamp(const qint64 pid)
{
    if (pid <= 0)
    {
        return std::nullopt;
    }
#ifdef _WIN32
    const HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, static_cast<DWORD>(pid));
    if (!process)
    {
        return std::nullopt;
    }
    FILETIME created{};
    FILETIME exited{};
    FILETIME kernel{};
    FILETIME user{};
    const bool read = GetProcessTimes(process, &created, &exited, &kernel, &user) != 0;
    CloseHandle(process);
    if (!read)
    {
        return std::nullopt;
    }
    return (static_cast<std::uint64_t>(created.dwHighDateTime) << 32u) |
           static_cast<std::uint64_t>(created.dwLowDateTime);
#else
    // The start time is the 22nd field of the process's stat line, in clock ticks since boot; the command name before
    // it may hold spaces and parentheses, so the fields are counted from its closing parenthesis.
    const std::string stat = readFileText(std::filesystem::path("/proc") / std::to_string(pid) / "stat");
    const std::size_t nameEnd = stat.rfind(')');
    if (nameEnd == std::string::npos)
    {
        return std::nullopt;
    }
    std::istringstream fields(stat.substr(nameEnd + 1));
    std::string field;
    for (int index = 3; fields >> field; ++index)
    {
        if (index == 22)
        {
            bool ok = false;
            const qulonglong started = QString::fromStdString(field).toULongLong(&ok);
            return ok ? std::optional<std::uint64_t>(started) : std::nullopt;
        }
    }
    return std::nullopt;
#endif
}

/// The lines a lock names its owner by: this process's pid and, when it can be read, when this process started.
[[nodiscard]] std::string lockOwnerLines()
{
    std::ostringstream lines;
    lines << "owner_pid=" << QCoreApplication::applicationPid() << '\n';
    if (const std::optional<std::uint64_t> started = processStartStamp(QCoreApplication::applicationPid()))
    {
        lines << "owner_started=" << *started << '\n';
    }
    return lines.str();
}

/// Whether the process a lock names still runs and is the one that wrote the lock: a lock that records when its owner
/// started belongs to no process started at another time, however the pid was given out since.
[[nodiscard]] bool lockOwnerAlive(const ProvisioningLockMetadata& metadata)
{
    if (!metadata.owner_pid.has_value() || !isProcessRunning(*metadata.owner_pid))
    {
        return false;
    }
    if (!metadata.owner_started.has_value())
    {
        return true;
    }
    const std::optional<std::uint64_t> started = processStartStamp(*metadata.owner_pid);
    return !started.has_value() || *started == *metadata.owner_started;
}

[[nodiscard]] bool hasActiveProvisioningLock(const std::filesystem::path& provisioning_lock)
{
    const auto metadata = readProvisioningLockMetadata(provisioning_lock);
    if (!lockOwnerAlive(metadata))
    {
        return false;
    }
    if (*metadata.owner_pid == QCoreApplication::applicationPid())
    {
        // A lock of this process is held while the operation of this process that wrote it runs. One left behind -
        // by a refresh that could not remove it, by a failure before the environment was recorded - holds nothing:
        // waiting for it would wait for this process's own life.
        return lockHeldByThisProcess(provisioning_lock);
    }
    return true;
}

/// Whether an environment's lock is held: by a process still running, or by one writing it right now, whose lock is
/// still empty.
[[nodiscard]] bool environmentLockHeld(const std::filesystem::path& lock)
{
    if (hasActiveProvisioningLock(lock))
    {
        return true;
    }
    std::error_code ec;
    const auto written = std::filesystem::last_write_time(lock, ec);
    return !ec && readFileText(lock).empty() &&
           std::filesystem::file_time_type::clock::now() - written < std::chrono::seconds(10);
}

[[nodiscard]] bool isKnownRuntimeTargetName(const std::string_view value)
{
    return std::find(kSupportedRuntimeTargets.begin(), kSupportedRuntimeTargets.end(), value) !=
           kSupportedRuntimeTargets.end();
}

[[nodiscard]] std::string pathForLog(const std::filesystem::path& path)
{
#ifdef _WIN32
    std::filesystem::path normalized = path.lexically_normal();
    const std::wstring native = normalized.native();
    if (native.rfind(L"\\\\?\\UNC\\", 0) == 0)
    {
        normalized = std::filesystem::path(L"\\\\" + native.substr(8));
    }
    else if (native.rfind(L"\\\\?\\", 0) == 0)
    {
        normalized = std::filesystem::path(native.substr(4));
    }
    return normalized.generic_string();
#else
    return path.lexically_normal().generic_string();
#endif
}

/// Lets an environment's lock go once the operation that took it is done - a provisioning completed or failed, a
/// refresh, an adoption. A file another program holds open for a moment - an indexer, a virus scanner - cannot be
/// removed at once, so the removal is tried again for a while; one still held is rewritten to name no owner, which no
/// reader takes for a lock held and which the next operation that takes the lock removes. Only when neither can be
/// done is the lock left naming this process, and that is reported.
[[nodiscard]] bool releaseEnvironmentLock(const std::filesystem::path& lock, std::string* error)
{
    // Whatever becomes of the file, this process no longer claims it.
    noteLockReleased(lock);
    std::string removeError;
    for (int attempt = 0; attempt < 5; ++attempt)
    {
        if (removeIfExists(lock, &removeError))
        {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    std::string writeError;
    if (writeFileText(lock, "released=1\n", &writeError))
    {
        if (an::platform::isLoggingInitialized())
        {
            LOG_WARNING("The extension environment's lock could not be removed and was released in place: {} ({})",
                        pathForLog(lock), removeError);
        }
        return true;
    }
    if (error)
    {
        *error = removeError + "; nor could it be released in place: " + writeError;
    }
    return false;
}

[[nodiscard]] std::string formatFilesystemError(const std::string_view action, const std::filesystem::path& path,
                                                const std::error_code& ec)
{
    std::ostringstream message;
    message << action << ": path='" << pathForLog(path) << "'";
    if (ec)
    {
        message << " error='" << ec.message() << "' code=" << ec.value() << " category='" << ec.category().name()
                << "'";
    }
    return message.str();
}

[[nodiscard]] std::string shortFingerprintPathToken(const std::string_view prefix, const std::string& fingerprint)
{
    constexpr std::size_t kPathFingerprintLength = 16;
    const std::size_t length = (std::min)(kPathFingerprintLength, fingerprint.size());
    return std::string(prefix) + fingerprint.substr(0, length);
}

void setFilesystemError(std::string* error, const std::string_view action, const std::filesystem::path& path,
                        const std::error_code& ec)
{
    const std::string diagnostic = formatFilesystemError(action, path, ec);
    if (error)
    {
        *error = diagnostic;
    }
    if (an::platform::isLoggingInitialized())
    {
        LOG_WARNING("Extension environment filesystem operation failed: {}", diagnostic);
    }
}

void setFilesystemError(std::string* error, const std::string& diagnostic)
{
    if (error)
    {
        *error = diagnostic;
    }
    if (an::platform::isLoggingInitialized())
    {
        LOG_WARNING("Extension environment filesystem operation failed: {}", diagnostic);
    }
}

[[nodiscard]] std::string sha256Hex(const std::string& value)
{
    return QCryptographicHash::hash(QByteArray(value.data(), static_cast<QByteArray::size_type>(value.size())),
                                    QCryptographicHash::Sha256)
        .toHex()
        .toStdString();
}

[[nodiscard]] std::string hashFile(const std::filesystem::path& path,
                                   const ExtensionOperationCancellationToken* cancellation_token = nullptr)
{
    std::ifstream stream(path, std::ios::binary);
    QCryptographicHash hash(QCryptographicHash::Sha256);
    std::array<char, 64 * 1024> buffer{};
    while (stream)
    {
        if (cancellation_token && cancellation_token->isCancellationRequested())
        {
            return {};
        }

        stream.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const std::streamsize bytes_read = stream.gcount();
        if (bytes_read > 0)
        {
            hash.addData(QByteArrayView(buffer.data(), static_cast<qsizetype>(bytes_read)));
        }
    }
    return hash.result().toHex().toStdString();
}

[[nodiscard]] std::string toLowerCopy(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(),
                   [](const unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return value;
}

[[nodiscard]] std::string normalizeDistributionName(const std::string_view value)
{
    std::string normalized;
    normalized.reserve(value.size());
    bool previous_separator = false;
    for (const unsigned char ch : value)
    {
        if (ch == '-' || ch == '_' || ch == '.')
        {
            if (!previous_separator)
            {
                normalized.push_back('-');
                previous_separator = true;
            }
            continue;
        }

        normalized.push_back(static_cast<char>(std::tolower(ch)));
        previous_separator = false;
    }

    return normalized;
}

[[nodiscard]] std::string trimWhitespace(std::string value)
{
    auto is_space = [](const unsigned char ch) { return std::isspace(ch) != 0; };

    while (!value.empty() && is_space(static_cast<unsigned char>(value.front())))
    {
        value.erase(value.begin());
    }
    while (!value.empty() && is_space(static_cast<unsigned char>(value.back())))
    {
        value.pop_back();
    }

    return value;
}

[[nodiscard]] std::vector<std::filesystem::path> candidateParentRoots(std::filesystem::path path)
{
    std::vector<std::filesystem::path> roots;
    while (!path.empty())
    {
        roots.push_back(path);
        const auto parent = path.parent_path();
        if (parent == path)
        {
            break;
        }
        path = parent;
    }
    return roots;
}

[[nodiscard]] std::vector<std::filesystem::path> bundledPythonExecutableCandidates()
{
    std::vector<std::filesystem::path> candidates;
    if (QCoreApplication::instance())
    {
        const std::filesystem::path app_dir(QCoreApplication::applicationDirPath().toStdString());
        const auto deployed = an::ui::extensions::bundled_python_executable_for_app_dir(app_dir);
        if (!deployed.empty())
        {
            candidates.push_back(deployed);
        }
    }

    std::error_code ec;
    const auto current_root = std::filesystem::current_path(ec);
    if (!ec)
    {
        for (const auto& root : candidateParentRoots(current_root))
        {
#ifdef _WIN32
            candidates.push_back(root / "python" / "python" / "tools" / "python.exe");
            candidates.push_back(root / "python" / "python" / "tools" / "bin" / "python.exe");
#else
            candidates.push_back(root / "python" / "python" / "tools" / "bin" / "python3.14");
            candidates.push_back(root / "python" / "python" / "tools" / "bin" / "python3");
            candidates.push_back(root / "python" / "python" / "tools" / "bin" / "python");
#endif
        }
    }

    return candidates;
}

[[nodiscard]] std::filesystem::path resolveBundledPythonExecutable(const ExtensionDiscoveryContext& context)
{
    if (!context.bundled_python_executable.empty())
    {
        return context.bundled_python_executable;
    }

    for (const auto& candidate : bundledPythonExecutableCandidates())
    {
        if (isRegularFile(candidate))
        {
            return candidate.lexically_normal();
        }
    }

    return {};
}

[[nodiscard]] std::filesystem::path environmentPythonExecutable(const std::filesystem::path& environment_path)
{
#ifdef _WIN32
    return environment_path / "Scripts" / "python.exe";
#else
    return environment_path / "bin" / "python";
#endif
}

[[nodiscard]] std::filesystem::path runtimeSitePackagesPath(const std::filesystem::path& runtime_root)
{
#ifdef _WIN32
    return runtime_root / "Lib" / "site-packages";
#else
    return runtime_root / "lib" / "python3.14" / "site-packages";
#endif
}

[[nodiscard]] std::filesystem::path environmentSitePackagesPath(const std::filesystem::path& environment_root)
{
#ifdef _WIN32
    return environment_root / "Lib" / "site-packages";
#else
    return environment_root / "lib" / "python3.14" / "site-packages";
#endif
}

[[nodiscard]] const std::array<std::string_view, 11>* sharedHostProfilePackagePrefixes(const std::string_view profile)
{
    if (profile == kScientificCoreSharedHostProfile)
    {
        return &kScientificCoreRuntimePackagePrefixes;
    }

    return nullptr;
}

void appendBundledRuntimePackagePrefixFingerprint(std::ostringstream& stream, const std::filesystem::path& runtime_root,
                                                  const std::string_view package_prefix,
                                                  const ExtensionOperationCancellationToken* cancellation_token,
                                                  const ExtensionEnvironmentInspectionMode inspection_mode,
                                                  const std::filesystem::path& cache_directory)
{
    if (cancellation_token && cancellation_token->isCancellationRequested())
    {
        return;
    }

    const std::filesystem::path source_site_packages = runtimeSitePackagesPath(runtime_root);
    std::error_code ec;
    if (!std::filesystem::exists(source_site_packages, ec))
    {
        stream << "missing-site-packages:" << source_site_packages.generic_string() << '\n';
        return;
    }

    bool found = false;
    for (const auto& entry : sortedDirectoryEntries(source_site_packages))
    {
        if (cancellation_token && cancellation_token->isCancellationRequested())
        {
            return;
        }

        const std::string name = entry.path().filename().string();
        if (name.rfind(std::string(package_prefix), 0) != 0)
        {
            continue;
        }

        found = true;
        const std::string path_hash = hashPathTree(entry.path(), cancellation_token, inspection_mode, cache_directory);
        if (cancellation_token && cancellation_token->isCancellationRequested())
        {
            return;
        }
        stream << name << '\n' << path_hash << '\n';
    }

    if (!found)
    {
        stream << "missing-prefix:" << package_prefix << '\n';
    }
}

[[nodiscard]] std::string
sharedHostProfileDependencyFingerprint(const std::string_view profile, const std::filesystem::path& runtime_root,
                                       const ExtensionOperationCancellationToken* cancellation_token,
                                       const ExtensionEnvironmentInspectionMode inspection_mode,
                                       const std::filesystem::path& cache_directory)
{
    std::ostringstream stream;
    stream << profile << '\n';

    if (const auto* package_prefixes = sharedHostProfilePackagePrefixes(profile))
    {
        for (const auto package_prefix : *package_prefixes)
        {
            if (cancellation_token && cancellation_token->isCancellationRequested())
            {
                return {};
            }
            appendBundledRuntimePackagePrefixFingerprint(stream, runtime_root, package_prefix, cancellation_token,
                                                         inspection_mode, cache_directory);
        }
    }

    return sha256Hex(stream.str());
}

[[nodiscard]] std::string normalizedPathIdentity(const std::filesystem::path& path)
{
    std::error_code ec;
    const auto absolute_path = std::filesystem::absolute(path, ec);
    return (ec ? path : absolute_path).lexically_normal().generic_string();
}

// Where a bundled artifact is, as the fingerprints read it: inside a relocatable package its place in the package,
// which is the same in every run, and anywhere else its absolute path.
[[nodiscard]] std::string packagePathIdentity(const std::filesystem::path& path,
                                              const std::optional<an::ui::extensions::RelocatablePackage>& package)
{
    const std::string absolute = normalizedPathIdentity(path);
    if (package && !path.empty())
    {
        const std::filesystem::path inside =
            std::filesystem::path(absolute).lexically_relative(package->root.lexically_normal());
        if (!inside.empty() && *inside.begin() != "..")
        {
            return "package:" + inside.generic_string();
        }
    }
    return absolute;
}

[[nodiscard]] std::filesystem::path sharedHostProfileReceiptPath(const std::filesystem::path& environments_root,
                                                                 const std::string_view profile,
                                                                 const std::string_view runtime_target)
{
    return environments_root / std::string(kSharedHostProfileReceiptDirectory) / std::string(profile) /
           (std::string(runtime_target) + ".json");
}

[[nodiscard]] std::optional<std::string> loadSharedHostProfileReceipt(const std::filesystem::path& receipt_path,
                                                                      const std::string_view profile,
                                                                      const std::string_view runtime_target,
                                                                      const std::string_view host_bootstrap_fingerprint,
                                                                      const std::string_view runtime_root_identity)
{
    const std::string receipt_text = readFileText(receipt_path);
    if (receipt_text.empty())
    {
        return std::nullopt;
    }

    QJsonParseError parse_error;
    const QJsonDocument document = QJsonDocument::fromJson(QByteArray::fromStdString(receipt_text), &parse_error);
    if (parse_error.error != QJsonParseError::NoError || !document.isObject())
    {
        return std::nullopt;
    }

    const QJsonObject object = document.object();
    if (object.value(QStringLiteral("schema_version")).toInt(-1) != kSharedHostProfileReceiptSchemaVersion ||
        object.value(QStringLiteral("profile")).toString().toStdString() != profile ||
        object.value(QStringLiteral("runtime_target")).toString().toStdString() != runtime_target ||
        object.value(QStringLiteral("host_bootstrap_fingerprint")).toString().toStdString() !=
            host_bootstrap_fingerprint ||
        object.value(QStringLiteral("runtime_root")).toString().toStdString() != runtime_root_identity)
    {
        return std::nullopt;
    }

    const std::string content_fingerprint =
        object.value(QStringLiteral("content_fingerprint")).toString().toStdString();
    if (content_fingerprint.size() != 64)
    {
        return std::nullopt;
    }

    return content_fingerprint;
}

bool writeSharedHostProfileReceipt(const std::filesystem::path& receipt_path, const std::string_view profile,
                                   const std::string_view runtime_target,
                                   const std::string_view host_bootstrap_fingerprint,
                                   const std::string_view runtime_root_identity,
                                   const std::string_view content_fingerprint, std::string* error)
{
    QJsonObject object;
    object.insert(QStringLiteral("schema_version"), kSharedHostProfileReceiptSchemaVersion);
    object.insert(QStringLiteral("profile"), QString::fromUtf8(profile.data(), static_cast<qsizetype>(profile.size())));
    object.insert(QStringLiteral("runtime_target"),
                  QString::fromUtf8(runtime_target.data(), static_cast<qsizetype>(runtime_target.size())));
    object.insert(QStringLiteral("host_bootstrap_fingerprint"),
                  QString::fromUtf8(host_bootstrap_fingerprint.data(),
                                    static_cast<qsizetype>(host_bootstrap_fingerprint.size())));
    object.insert(
        QStringLiteral("runtime_root"),
        QString::fromUtf8(runtime_root_identity.data(), static_cast<qsizetype>(runtime_root_identity.size())));
    object.insert(QStringLiteral("content_fingerprint"),
                  QString::fromUtf8(content_fingerprint.data(), static_cast<qsizetype>(content_fingerprint.size())));
    return writeFileText(receipt_path, QJsonDocument(object).toJson(QJsonDocument::Indented).toStdString(), error);
}

[[nodiscard]] std::string sharedHostProfileFingerprintCacheKey(const std::string_view profile,
                                                               const std::string_view runtime_target,
                                                               const std::string_view host_bootstrap_fingerprint,
                                                               const std::string_view runtime_root_identity)
{
    std::ostringstream stream;
    stream << profile << '\n' << runtime_target << '\n' << host_bootstrap_fingerprint << '\n' << runtime_root_identity;
    return sha256Hex(stream.str());
}

void appendBootstrapArtifactIdentity(std::ostringstream& stream, const std::string_view label,
                                     const std::filesystem::path& path,
                                     const std::optional<an::ui::extensions::RelocatablePackage>& package)
{
    const std::string identity = packagePathIdentity(path, package);
    stream << label << '\n' << identity << '\n';
    std::error_code ec;
    if (path.empty() || !std::filesystem::is_regular_file(path, ec))
    {
        stream << "missing\n";
        return;
    }

    ec.clear();
    const auto size = std::filesystem::file_size(path, ec);
    stream << (ec ? -1 : static_cast<long long>(size)) << '\n';

    // Inside a relocatable package the package's identity stands for the write time, which an AppImage's runtime sets
    // to the start's own each time it extracts the package.
    if (identity.rfind("package:", 0) == 0)
    {
        return;
    }

    ec.clear();
    const auto write_time = std::filesystem::last_write_time(path, ec);
    const auto write_time_ns =
        std::chrono::duration_cast<std::chrono::nanoseconds>(write_time.time_since_epoch()).count();
    stream << (ec ? -1LL : write_time_ns) << '\n';
}

// The value of a `key = value` line of pyvenv.cfg, or nothing when the line sets another key.
[[nodiscard]] std::optional<std::string> pyvenvValue(const std::string& line, const std::string_view key)
{
    const auto equals = line.find('=');
    if (equals == std::string::npos || trimWhitespace(line.substr(0, equals)) != key)
    {
        return std::nullopt;
    }
    return trimWhitespace(line.substr(equals + 1));
}

// The Python version an environment's pyvenv.cfg says it was made over, or nothing when it says none.
[[nodiscard]] std::optional<std::string> environmentPythonVersion(const std::filesystem::path& environment_path)
{
    std::istringstream lines(readFileText(environment_path / "pyvenv.cfg"));
    std::string line;
    while (std::getline(lines, line))
    {
        if (const auto value = pyvenvValue(line, "version"); value && !value->empty())
        {
            return value;
        }
        if (const auto value = pyvenvValue(line, "version_info"); value && !value->empty())
        {
            return value;
        }
    }
    return std::nullopt;
}

#ifndef _WIN32
// An environment made from a relocatable package's Python names that interpreter where it was in the run that made
// it: the venv's link to it, and pyvenv.cfg's home, executable and command. The package's files are somewhere else on
// every run, and the fingerprints say the package is the same one, so the environment is pointed at this run's
// interpreter. A new link and the rewritten configuration are each written beside the old and renamed over it, so a
// reader sees one or the other, and nothing is written while both already name this run's interpreter.
bool repointEnvironmentInterpreter(const std::filesystem::path& environment_path,
                                   const std::filesystem::path& interpreter, std::string* error)
{
    if (interpreter.empty())
    {
        return true;
    }

    const std::string token = QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
    std::error_code ec;
    const std::filesystem::path link = environment_path / "bin" / interpreter.filename();
    if (std::filesystem::is_symlink(link, ec))
    {
        const std::filesystem::path recorded = std::filesystem::read_symlink(link, ec);
        if (ec)
        {
            setFilesystemError(error, "Failed to read the extension environment's interpreter link", link, ec);
            return false;
        }
        if (recorded != interpreter)
        {
            const std::filesystem::path replacement =
                link.parent_path() / ("." + link.filename().string() + ".repoint-" + token);
            std::filesystem::create_symlink(interpreter, replacement, ec);
            if (!ec)
            {
                std::filesystem::rename(replacement, link, ec);
            }
            if (ec)
            {
                std::error_code ignored;
                std::filesystem::remove(replacement, ignored);
                setFilesystemError(error,
                                   "Failed to point the extension environment's interpreter link at this run's "
                                   "Python",
                                   link, ec);
                return false;
            }
            if (an::platform::isLoggingInitialized())
            {
                LOG_INFO("Pointed extension environment interpreter link path='{}' at '{}', was '{}'", pathForLog(link),
                         pathForLog(interpreter), pathForLog(recorded));
            }
        }
    }

    const std::filesystem::path config = environment_path / "pyvenv.cfg";
    const std::string text = readFileText(config);
    if (text.empty())
    {
        return true;
    }

    std::vector<std::string> lines;
    std::size_t start = 0;
    while (start <= text.size())
    {
        const std::size_t end = text.find('\n', start);
        lines.push_back(text.substr(start, end == std::string::npos ? std::string::npos : end - start));
        if (end == std::string::npos)
        {
            break;
        }
        start = end + 1;
    }

    std::string previous_executable;
    for (const auto& line : lines)
    {
        if (const auto value = pyvenvValue(line, "executable"))
        {
            previous_executable = *value;
        }
    }

    const std::string executable = interpreter.string();
    for (auto& line : lines)
    {
        if (pyvenvValue(line, "home"))
        {
            line = "home = " + interpreter.parent_path().string();
        }
        else if (pyvenvValue(line, "executable"))
        {
            line = "executable = " + executable;
        }
        else if (const auto command = pyvenvValue(line, "command");
                 command && !previous_executable.empty() && command->rfind(previous_executable, 0) == 0)
        {
            line = "command = " + executable + command->substr(previous_executable.size());
        }
    }

    std::string rewritten;
    for (std::size_t index = 0; index < lines.size(); ++index)
    {
        rewritten += lines[index];
        if (index + 1 < lines.size())
        {
            rewritten += '\n';
        }
    }
    if (rewritten == text)
    {
        return true;
    }

    const std::filesystem::path replacement = environment_path / (".pyvenv.cfg.repoint-" + token);
    if (!writeFileText(replacement, rewritten, error))
    {
        std::error_code ignored;
        std::filesystem::remove(replacement, ignored);
        return false;
    }
    std::filesystem::rename(replacement, config, ec);
    if (ec)
    {
        std::error_code ignored;
        std::filesystem::remove(replacement, ignored);
        setFilesystemError(error, "Failed to point the extension environment's configuration at this run's Python",
                           config, ec);
        return false;
    }
    if (an::platform::isLoggingInitialized())
    {
        LOG_INFO("Pointed extension environment configuration path='{}' at '{}'", pathForLog(config),
                 pathForLog(interpreter));
    }
    return true;
}
#endif

#ifdef _WIN32
[[nodiscard]] std::filesystem::path windowsBundledPythonHome(const std::filesystem::path& executable)
{
    const std::filesystem::path parent = executable.parent_path();
    if (parent.filename() == "Scripts")
    {
        return parent.parent_path();
    }

    return parent;
}

[[nodiscard]] std::filesystem::path windowsExtendedLengthPath(const std::filesystem::path& path)
{
    if (path.empty())
    {
        return path;
    }

    std::filesystem::path normalized = path.lexically_normal();
    normalized.make_preferred();

    const std::wstring native = normalized.native();
    if (native.rfind(L"\\\\?\\", 0) == 0)
    {
        return normalized;
    }

    if (native.rfind(L"\\\\", 0) == 0)
    {
        return std::filesystem::path(L"\\\\?\\UNC\\" + native.substr(2));
    }

    return std::filesystem::path(L"\\\\?\\" + native);
}

[[nodiscard]] std::filesystem::path windowsProvisioningPythonHome(const std::filesystem::path& executable,
                                                                  const std::filesystem::path& runtime_root)
{
    const std::filesystem::path executable_home = windowsBundledPythonHome(executable);
    if (isDirectory(executable_home / "Lib" / "encodings"))
    {
        return executable_home;
    }

    if (!runtime_root.empty())
    {
        return runtime_root;
    }

    return executable_home;
}

bool bootstrapManagedWindowsEnvironment(const ExtensionEnvironmentSnapshot& snapshot,
                                        const std::filesystem::path& python_home, std::string* error)
{
    if (!isRegularFile(snapshot.bundled_python_executable))
    {
        if (error)
        {
            *error = "Bundled Python executable is not available for Windows environment bootstrap";
        }
        return false;
    }

    const std::filesystem::path env_scripts = snapshot.environment_path / "Scripts";
    const std::filesystem::path env_site_packages = environmentSitePackagesPath(snapshot.environment_path);
    const std::filesystem::path env_python = environmentPythonExecutable(snapshot.environment_path);

    std::error_code ec;
    std::filesystem::create_directories(windowsExtendedLengthPath(env_scripts), ec);
    if (ec)
    {
        setFilesystemError(error, "Failed to create Windows extension environment Scripts directory", env_scripts, ec);
        return false;
    }

    ec.clear();
    std::filesystem::create_directories(windowsExtendedLengthPath(env_site_packages), ec);
    if (ec)
    {
        setFilesystemError(error, "Failed to create Windows extension environment site-packages directory",
                           env_site_packages, ec);
        return false;
    }

    ec.clear();
    std::filesystem::copy_file(windowsExtendedLengthPath(snapshot.bundled_python_executable),
                               windowsExtendedLengthPath(env_python), std::filesystem::copy_options::overwrite_existing,
                               ec);
    if (ec)
    {
        setFilesystemError(error, "Failed to copy bundled Python executable into the Windows extension environment",
                           env_python, ec);
        return false;
    }

    const std::filesystem::path pythonw = python_home / "pythonw.exe";
    if (isRegularFile(pythonw))
    {
        ec.clear();
        std::filesystem::copy_file(windowsExtendedLengthPath(pythonw),
                                   windowsExtendedLengthPath(env_scripts / "pythonw.exe"),
                                   std::filesystem::copy_options::overwrite_existing, ec);
        if (ec)
        {
            setFilesystemError(error, "Failed to copy bundled pythonw.exe into the Windows extension environment",
                               env_scripts / "pythonw.exe", ec);
            return false;
        }
    }

    std::vector<std::filesystem::path> adjacent_runtime_dlls;
    for (std::filesystem::directory_iterator it(windowsExtendedLengthPath(python_home), ec), end; it != end && !ec;
         it.increment(ec))
    {
        std::error_code type_ec;
        if (!it->is_regular_file(type_ec) || type_ec)
        {
            continue;
        }

        if (toLowerCopy(it->path().extension().string()) != ".dll")
        {
            continue;
        }

        adjacent_runtime_dlls.push_back(it->path());
    }

    if (ec)
    {
        setFilesystemError(error, "Failed to enumerate bundled Python runtime DLLs", python_home, ec);
        return false;
    }

    std::sort(adjacent_runtime_dlls.begin(), adjacent_runtime_dlls.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.filename().generic_string() < rhs.filename().generic_string();
    });

    for (const auto& runtime_dll : adjacent_runtime_dlls)
    {
        ec.clear();
        std::filesystem::copy_file(windowsExtendedLengthPath(runtime_dll),
                                   windowsExtendedLengthPath(env_scripts / runtime_dll.filename()),
                                   std::filesystem::copy_options::overwrite_existing, ec);
        if (ec)
        {
            setFilesystemError(error,
                               "Failed to copy bundled Python runtime DLL into the Windows extension environment",
                               env_scripts / runtime_dll.filename(), ec);
            return false;
        }
    }

    std::ostringstream pyvenv;
    pyvenv << "home = " << python_home.string() << '\n'
           << "include-system-site-packages = false\n"
           << "version = " << snapshot.bundled_python_version << '\n'
           << "executable = " << snapshot.bundled_python_executable.string() << '\n';

    return writeFileText(snapshot.environment_path / "pyvenv.cfg", pyvenv.str(), error);
}
#endif

#ifndef _WIN32
[[nodiscard]] std::filesystem::path windowsExtendedLengthPath(const std::filesystem::path& path)
{
    return path;
}
#endif

#ifdef _WIN32
void makePathWritableForRemoval(const std::filesystem::path& path)
{
    const std::filesystem::path native_path = windowsExtendedLengthPath(path);
    const DWORD attributes = GetFileAttributesW(native_path.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_READONLY) == 0)
    {
        return;
    }

    SetFileAttributesW(native_path.c_str(), attributes & ~FILE_ATTRIBUTE_READONLY);
}
#else
void makePathWritableForRemoval(const std::filesystem::path&) {}
#endif

void makePathTreeWritableForRemoval(const std::filesystem::path& root)
{
    makePathWritableForRemoval(root);

    std::error_code ec;
    if (!std::filesystem::is_directory(windowsExtendedLengthPath(root), ec))
    {
        return;
    }

    for (std::filesystem::recursive_directory_iterator
             it(windowsExtendedLengthPath(root), std::filesystem::directory_options::skip_permission_denied, ec),
         end;
         it != end && !ec; it.increment(ec))
    {
        makePathWritableForRemoval(it->path());
    }
}

[[nodiscard]] std::vector<std::string> remainingPathSamplesForLog(const std::filesystem::path& root,
                                                                  std::size_t* out_count)
{
    std::vector<std::string> samples;
    if (out_count)
    {
        *out_count = 0;
    }

    std::error_code ec;
    const std::filesystem::path native_root = windowsExtendedLengthPath(root);
    if (!std::filesystem::exists(native_root, ec))
    {
        return samples;
    }

    const auto append_sample = [&](const std::filesystem::path& path) {
        if (out_count)
        {
            ++(*out_count);
        }
        if (samples.size() < kFilesystemRemoveRemainingSampleLimit)
        {
            samples.push_back(pathForLog(path));
        }
    };

    if (!std::filesystem::is_directory(native_root, ec))
    {
        append_sample(root);
        return samples;
    }

    for (std::filesystem::recursive_directory_iterator
             it(native_root, std::filesystem::directory_options::skip_permission_denied, ec),
         end;
         it != end && !ec; it.increment(ec))
    {
        append_sample(it->path());
    }
    return samples;
}

void appendRemainingPathDiagnostics(std::ostringstream& diagnostic, const std::filesystem::path& root)
{
    std::size_t remaining_count = 0;
    const std::vector<std::string> samples = remainingPathSamplesForLog(root, &remaining_count);
    if (remaining_count == 0)
    {
        return;
    }

    diagnostic << " remaining_entries=" << remaining_count << " remaining_sample=[";
    for (std::size_t i = 0; i < samples.size(); ++i)
    {
        if (i > 0)
        {
            diagnostic << ", ";
        }
        diagnostic << "'" << samples[i] << "'";
    }
    diagnostic << "]";
}

bool removePathTreeWithDiagnostics(const std::filesystem::path& path, const std::string_view action, std::string* error)
{
    std::error_code ec;
    if (!std::filesystem::exists(windowsExtendedLengthPath(path), ec))
    {
        if (ec)
        {
            setFilesystemError(error, action, path, ec);
            return false;
        }
        return true;
    }

    std::error_code last_error;
    for (int attempt = 1; attempt <= kFilesystemRemoveAttempts; ++attempt)
    {
        makePathTreeWritableForRemoval(path);

        last_error.clear();
        std::filesystem::remove_all(windowsExtendedLengthPath(path), last_error);

        std::error_code exists_error;
        if (!std::filesystem::exists(windowsExtendedLengthPath(path), exists_error))
        {
            if (attempt > 1 && an::platform::isLoggingInitialized())
            {
                LOG_INFO("Removed extension environment path after retry path='{}' attempts={}", pathForLog(path),
                         attempt);
            }
            return true;
        }

        if (exists_error)
        {
            last_error = exists_error;
            break;
        }

        if (attempt < kFilesystemRemoveAttempts)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(kFilesystemRemoveRetryDelayMs));
        }
    }

    std::ostringstream diagnostic;
    diagnostic << action << ": path='" << pathForLog(path) << "'";
    if (last_error)
    {
        diagnostic << " error='" << last_error.message() << "' code=" << last_error.value() << " category='"
                   << last_error.category().name() << "'";
    }
    else
    {
        diagnostic << " error='path still exists after removal attempts' code=0 category='after-night'";
    }
    diagnostic << " attempts=" << kFilesystemRemoveAttempts;
    appendRemainingPathDiagnostics(diagnostic, path);
    setFilesystemError(error, diagnostic.str());
    return false;
}

[[nodiscard]] bool isDirectory(const std::filesystem::path& path)
{
    std::error_code ec;
    return std::filesystem::is_directory(path, ec);
}

[[nodiscard]] bool runtimeRootLooksUsable(const std::filesystem::path& runtime_root)
{
    if (runtime_root.empty())
    {
        return false;
    }

#ifdef _WIN32
    if (isDirectory(runtime_root / "Lib" / "encodings"))
    {
        return true;
    }
#endif

    const std::filesystem::path lib_root = runtime_root / "lib";
    if (!isDirectory(lib_root))
    {
        return false;
    }

    for (const auto& entry : sortedDirectoryEntries(lib_root))
    {
        if (!entry.is_directory())
        {
            continue;
        }

        const std::string name = entry.path().filename().generic_string();
        if (name.rfind("python", 0) != 0)
        {
            continue;
        }

        if (isDirectory(entry.path() / "encodings"))
        {
            return true;
        }
    }

    return false;
}

[[nodiscard]] std::filesystem::path runtimeRootForBundledPython(const std::filesystem::path& executable,
                                                                const std::filesystem::path& fallback_runtime_root = {})
{
    if (executable.empty() && fallback_runtime_root.empty())
    {
        return {};
    }

    std::vector<std::filesystem::path> candidates;
    const std::filesystem::path parent = executable.parent_path();
    if (!parent.empty())
    {
        const auto parent_name = parent.filename().generic_string();
        if (parent_name == "bin" || parent_name == "Scripts")
        {
            candidates.push_back(parent.parent_path());
        }

        candidates.push_back(parent);
        candidates.push_back(parent / "PythonLib");
    }

    std::set<std::string> seen;
    for (const auto& candidate : candidates)
    {
        const auto normalized = candidate.lexically_normal();
        if (normalized.empty())
        {
            continue;
        }

        const auto key = normalized.generic_string();
        if (!seen.insert(key).second)
        {
            continue;
        }

        if (runtimeRootLooksUsable(normalized))
        {
            return normalized;
        }
    }

    if (runtimeRootLooksUsable(fallback_runtime_root))
    {
        return fallback_runtime_root.lexically_normal();
    }

    return {};
}

#ifndef _WIN32
/// The Python home a bundled interpreter prepares an environment under, as on Windows: its own, where it holds the
/// standard library, or else the runtime root the environment copies the application's packages from. A deployed
/// build's interpreter is in that runtime root, so the two are one there.
[[nodiscard]] std::filesystem::path posixProvisioningPythonHome(const std::filesystem::path& executable,
                                                                const std::filesystem::path& runtime_root)
{
    const std::filesystem::path parent = executable.parent_path();
    const std::filesystem::path executable_home = parent.filename() == "bin" ? parent.parent_path() : parent;
    if (runtimeRootLooksUsable(executable_home))
    {
        return executable_home;
    }

    return !runtime_root.empty() ? runtime_root : runtimeRootForBundledPython(executable);
}
#endif

[[nodiscard]] QProcessEnvironment isolatedPythonProcessEnvironment(const std::filesystem::path& python_home = {})
{
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.remove(QStringLiteral("PYTHONHOME"));
    environment.remove(QStringLiteral("PYTHONPATH"));
    environment.insert(QStringLiteral("PYTHONNOUSERSITE"), QStringLiteral("1"));
    if (!python_home.empty())
    {
        environment.insert(QStringLiteral("PYTHONHOME"), QString::fromStdString(python_home.string()));
    }
    return environment;
}

[[nodiscard]] std::string summarizeProcessOutput(const QByteArray& output)
{
    constexpr int kMaxChars = 600;
    const std::string text = trimWhitespace(output.toStdString());
    if (static_cast<int>(text.size()) <= kMaxChars)
    {
        return text;
    }

    return text.substr(text.size() - kMaxChars);
}

[[nodiscard]] bool cancellationRequested(const ExtensionOperationCancellationToken& cancellation_token)
{
    return cancellation_token.isCancellationRequested();
}

[[nodiscard]] bool cancellationRequested(const ExtensionOperationCancellationToken* cancellation_token)
{
    return cancellation_token != nullptr && cancellation_token->isCancellationRequested();
}

void setOperationCancelledError(std::string* error)
{
    if (error)
    {
        *error = "Operation cancelled.";
    }
}

bool runProcess(const std::filesystem::path& program, const QStringList& arguments,
                const std::filesystem::path& working_directory, const int timeout_ms,
                const QProcessEnvironment* process_environment, std::string* output, std::string* error,
                const ExtensionOperationCancellationToken* cancellation_token = nullptr,
                const std::function<void(const QByteArray&)>& output_callback = {},
                const std::function<void(qint64)>& heartbeat_callback = {})
{
    if (cancellationRequested(cancellation_token))
    {
        setOperationCancelledError(error);
        return false;
    }

    QProcess process;
    process.setProgram(QString::fromStdString(program.string()));
    process.setArguments(arguments);
    process.setProcessChannelMode(QProcess::MergedChannels);
    if (!working_directory.empty())
    {
        process.setWorkingDirectory(QString::fromStdString(working_directory.string()));
    }
    if (process_environment)
    {
        process.setProcessEnvironment(*process_environment);
    }

    process.start();
    if (!process.waitForStarted())
    {
        if (error)
        {
            *error = "Failed to start process: " + program.generic_string();
        }
        return false;
    }

    QElapsedTimer timer;
    timer.start();
    QByteArray process_bytes;
    const auto collect_output = [&]() {
        const QByteArray chunk = process.readAll();
        if (chunk.isEmpty())
        {
            return;
        }

        process_bytes += chunk;
        if (output_callback)
        {
            output_callback(chunk);
        }
    };

    while (!process.waitForFinished(100))
    {
        collect_output();
        if (heartbeat_callback)
        {
            heartbeat_callback(timer.elapsed());
        }
        if (cancellationRequested(cancellation_token))
        {
            process.kill();
            process.waitForFinished();
            collect_output();
            setOperationCancelledError(error);
            if (output)
            {
                *output = summarizeProcessOutput(process_bytes);
            }
            return false;
        }

        if (timeout_ms >= 0 && timer.elapsed() >= timeout_ms)
        {
            process.kill();
            process.waitForFinished();
            collect_output();
            if (error)
            {
                *error = "Process timed out: " + program.generic_string();
            }
            if (output)
            {
                *output = summarizeProcessOutput(process_bytes);
            }
            return false;
        }
    }

    collect_output();
    const std::string process_output = summarizeProcessOutput(process_bytes);
    if (output)
    {
        *output = process_output;
    }

    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0)
    {
        if (error)
        {
            *error = process_output.empty() ? "Process failed: " + program.generic_string() : process_output;
        }
        return false;
    }

    return true;
}

void reportOperationProgress(const ExtensionOperationProgressCallback& callback, std::string phase_id,
                             std::string message, const int current = 0, const int total = 0,
                             const bool indeterminate = true)
{
    if (!callback)
    {
        return;
    }

    if (an::platform::isLoggingInitialized())
    {
        LOG_DEBUG("Extension environment progress phase='{}' current={} total={} indeterminate={} message='{}'",
                  phase_id, current, total, indeterminate, message);
    }
    callback({std::move(phase_id), std::move(message), current, total, indeterminate});
}

[[nodiscard]] int mapProgressSubrange(const int begin, const int end, const int current, const int total)
{
    if (total <= 0 || end <= begin)
    {
        return begin;
    }

    const int clamped_current = std::clamp(current, 0, total);
    const int span = end - begin;
    return begin + static_cast<int>((static_cast<long long>(clamped_current) * span + total - 1) / total);
}

void reportProvisionProgress(const ExtensionOperationProgressCallback& callback, std::string phase_id,
                             std::string message, const int current, const bool indeterminate = false)
{
    reportOperationProgress(callback, std::move(phase_id), std::move(message),
                            std::clamp(current, 0, kProvisionProgressTotal), kProvisionProgressTotal, indeterminate);
}

void reportProvisionSubrangeProgress(const ExtensionOperationProgressCallback& callback, std::string phase_id,
                                     std::string message, const int begin, const int end, const int current,
                                     const int total, const bool indeterminate = false)
{
    reportProvisionProgress(callback, std::move(phase_id), std::move(message),
                            mapProgressSubrange(begin, end, current, total), indeterminate);
}

[[nodiscard]] std::string compactProgressPath(std::string path)
{
    constexpr std::size_t kMaxProgressPathLength = 96;
    if (path.size() <= kMaxProgressPathLength)
    {
        return path;
    }

    return "..." + path.substr(path.size() - (kMaxProgressPathLength - 3));
}

struct BytecodeSourceFile
{
    std::filesystem::path path;
    std::string display_path;
};

// The path a directory is listed through: its extended-length form where the platform has one, so a directory past the
// length the platform lists without it is listed too.
[[nodiscard]] std::filesystem::path bytecodeListingPath(const std::filesystem::path& directory)
{
    return directory.is_absolute() ? windowsExtendedLengthPath(directory) : directory;
}

// The path a source is handed to Python's compiler through. Python opens the source and writes its cache file beside it
// through that path, and without the extended-length prefix neither may reach the platform's path limit, so a source
// whose cache file would reach it is handed over prefixed.
[[nodiscard]] std::filesystem::path bytecodeSourcePathForCompiler(const std::filesystem::path& source)
{
#ifdef _WIN32
    constexpr std::size_t kWindowsPathLimit = 260;
    // A "__pycache__" directory and a ".cpython-NNN.opt-N.pyc" name beside the source's own, and the temporary name
    // Python writes that file through before it replaces it, the cache name with a numeric suffix.
    constexpr std::size_t kBytecodeCachePathAllowance = 64;
    if (source.is_absolute() && source.native().size() + kBytecodeCachePathAllowance >= kWindowsPathLimit)
    {
        return windowsExtendedLengthPath(source);
    }
#endif
    return source;
}

void logUnlistedBytecodeDirectory(const std::filesystem::path& source_root, const std::filesystem::path& directory,
                                  const std::error_code& ec)
{
    if (an::platform::isLoggingInitialized())
    {
        LOG_WARNING("Python bytecode precompilation skipped '{}', which could not be listed ({}); the rest of '{}' is "
                    "compiled",
                    pathForLog(directory), ec.message(), pathForLog(source_root));
    }
}

// Every .py file below a root whose bytecode is compiled. Each directory is listed on its own, so one that cannot be
// listed - unreadable, or past the length the platform lists - costs only its own modules, and is named once in the
// log; only a root that cannot be listed at all fails the collection.
[[nodiscard]] std::vector<BytecodeSourceFile> collectPythonBytecodeSourceFiles(const std::filesystem::path& source_root,
                                                                               const std::string_view exclude_pattern,
                                                                               std::string* error)
{
    std::optional<std::regex> exclude_regex;
    if (!exclude_pattern.empty())
    {
        try
        {
            exclude_regex.emplace(std::string(exclude_pattern));
        }
        catch (const std::regex_error& regex_error)
        {
            if (error)
            {
                *error = "Invalid Python bytecode precompilation exclude pattern: " + std::string(regex_error.what());
            }
            return {};
        }
    }

    std::vector<BytecodeSourceFile> files;
    std::vector<std::filesystem::path> pending_directories{source_root};
    while (!pending_directories.empty())
    {
        const std::filesystem::path directory = std::move(pending_directories.back());
        pending_directories.pop_back();

        std::error_code ec;
        std::filesystem::directory_iterator it(bytecodeListingPath(directory), ec);
        if (ec && directory == source_root)
        {
            setFilesystemError(error, "Failed to enumerate Python bytecode source files", source_root, ec);
            return {};
        }
        for (const std::filesystem::directory_iterator end; !ec && it != end; it.increment(ec))
        {
            const std::filesystem::path path = directory / it->path().filename();
            std::error_code type_ec;
            // Judged by the entry itself, not what it points at: a symbolic link to a directory, or an NTFS junction,
            // is not followed, so the walk never leaves the tree or loops.
            if (it->symlink_status(type_ec).type() == std::filesystem::file_type::directory)
            {
                pending_directories.push_back(path);
                continue;
            }
            if (!it->is_regular_file(type_ec) || path.extension().string() != ".py")
            {
                continue;
            }

            const std::string path_text = path.generic_string();
            if (exclude_regex.has_value() && std::regex_search(path_text, *exclude_regex))
            {
                continue;
            }

            BytecodeSourceFile source;
            source.path = bytecodeSourcePathForCompiler(path);
            const std::filesystem::path relative_path = path.lexically_relative(source_root);
            source.display_path =
                relative_path.empty() ? path.filename().generic_string() : relative_path.generic_string();
            files.push_back(std::move(source));
        }
        if (ec)
        {
            logUnlistedBytecodeDirectory(source_root, directory, ec);
        }
    }

    std::sort(files.begin(), files.end(),
              [](const auto& lhs, const auto& rhs) { return lhs.display_path < rhs.display_path; });
    return files;
}

[[nodiscard]] int bytecodePrecompileWorkerCount(const std::size_t file_count)
{
    if (file_count <= 1)
    {
        return static_cast<int>(file_count);
    }

    const unsigned int hardware_workers = std::thread::hardware_concurrency();
    const int available_workers = hardware_workers == 0 ? 2 : static_cast<int>((std::min)(hardware_workers, 8u));
    return (std::min)({static_cast<int>(file_count), available_workers, kBytecodePrecompileMaxWorkerCount});
}

[[nodiscard]] bool writeBytecodeWorkerManifest(const std::filesystem::path& manifest_path,
                                               const std::vector<BytecodeSourceFile>& files,
                                               const std::size_t begin_index, const std::size_t end_index,
                                               std::string* error)
{
    std::ostringstream manifest;
    for (std::size_t index = begin_index; index < end_index; ++index)
    {
        manifest << files[index].path.string() << '\n';
    }

    return writeFileText(manifest_path, manifest.str(), error);
}

void reportBytecodeCompletedFile(const std::string_view display_path, int* completed_count, const int total_count,
                                 const ExtensionOperationProgressCallback& callback, const std::string_view phase_id,
                                 const std::string_view message_prefix)
{
    if (!completed_count || total_count <= 0)
    {
        return;
    }

    *completed_count = (std::min)(*completed_count + 1, total_count);
    std::string message(message_prefix);
    message += " ";
    message += std::to_string(*completed_count);
    message += "/";
    message += std::to_string(total_count);
    if (!display_path.empty())
    {
        message += ": ";
        message += compactProgressPath(std::string(display_path));
    }

    reportOperationProgress(callback, std::string(phase_id), std::move(message), *completed_count, total_count, false);
}

void reportBytecodeProgressLine(const std::string& raw_line, int* completed_count, const int total_count,
                                const ExtensionOperationProgressCallback& callback, const std::string_view phase_id,
                                const std::string_view message_prefix)
{
    const std::string line = trimTrailingWhitespace(raw_line);
    if (line.rfind(std::string(kBytecodeProgressItemPrefix), 0) != 0)
    {
        return;
    }

    const std::string_view display_path(line.data() + kBytecodeProgressItemPrefix.size(),
                                        line.size() - kBytecodeProgressItemPrefix.size());
    reportBytecodeCompletedFile(display_path, completed_count, total_count, callback, phase_id, message_prefix);
}

void reportBytecodeProgressChunk(const QByteArray& chunk, std::string* pending_line, int* completed_count,
                                 const int total_count, const ExtensionOperationProgressCallback& callback,
                                 const std::string_view phase_id, const std::string_view message_prefix)
{
    if (!pending_line)
    {
        return;
    }

    pending_line->append(chunk.constData(), static_cast<std::size_t>(chunk.size()));
    std::size_t newline = pending_line->find('\n');
    while (newline != std::string::npos)
    {
        const std::string line = pending_line->substr(0, newline);
        pending_line->erase(0, newline + 1);
        reportBytecodeProgressLine(line, completed_count, total_count, callback, phase_id, message_prefix);
        newline = pending_line->find('\n');
    }
}

struct BytecodeWorkerProcess
{
    std::unique_ptr<QProcess> process;
    QByteArray output;
    std::string pending_progress_line;
    bool finished = false;
};

bool precompilePythonBytecodeTree(const std::filesystem::path& python_executable,
                                  const std::filesystem::path& source_root,
                                  const std::filesystem::path& working_directory,
                                  const QProcessEnvironment* process_environment,
                                  const std::filesystem::path& strip_prefix, const std::string_view prepend_prefix,
                                  const std::string_view exclude_pattern, std::string* error,
                                  const int timeout_ms = kBytecodePrecompileTimeoutMs,
                                  const ExtensionOperationCancellationToken& cancellation_token = {},
                                  const ExtensionOperationProgressCallback& progress_callback = {},
                                  const std::string_view progress_phase_id = "precompile_bytecode",
                                  const std::string_view progress_message_prefix = "Precompiling Python bytecode")
{
    if (cancellationRequested(cancellation_token))
    {
        setOperationCancelledError(error);
        return false;
    }
    if (source_root.empty() || !isDirectory(source_root))
    {
        if (error)
        {
            *error = "Python bytecode source directory is missing: " + source_root.generic_string();
        }
        return false;
    }
    if (python_executable.empty() || !isRegularFile(python_executable))
    {
        if (error)
        {
            *error = "Python executable is missing for bytecode precompilation";
        }
        return false;
    }

    std::string collect_error;
    const std::vector<BytecodeSourceFile> source_files =
        collectPythonBytecodeSourceFiles(source_root, exclude_pattern, &collect_error);
    if (!collect_error.empty())
    {
        if (error)
        {
            *error = collect_error;
        }
        return false;
    }

    const int total_count = static_cast<int>(source_files.size());
    reportOperationProgress(progress_callback, std::string(progress_phase_id), std::string(progress_message_prefix), 0,
                            total_count, false);
    if (source_files.empty())
    {
        return true;
    }

    static constexpr std::string_view kProgressAwareCompileallWorkerScript = R"py(
import compileall
import pathlib
import sys

root = pathlib.Path(sys.argv[1]).resolve()
stripdir = sys.argv[2] or None
prependdir = sys.argv[3] or None
manifest = pathlib.Path(sys.argv[4])

ok = True
with manifest.open("r", encoding="utf-8") as handle:
  for raw_path in handle:
    raw_path = raw_path.rstrip("\n")
    if not raw_path:
        continue
    path = pathlib.Path(raw_path)
    try:
        display = path.resolve().relative_to(root).as_posix()
    except ValueError:
        display = path.name
    compiled = compileall.compile_file(
        str(path),
        force=True,
        quiet=1,
        stripdir=stripdir,
        prependdir=prependdir,
    )
    print(f"AFTERNIGHT_BYTECODE_PROGRESS\t{display}", flush=True)
    if not compiled:
        ok = False

raise SystemExit(0 if ok else 1)
)py";

    const int worker_count = bytecodePrecompileWorkerCount(source_files.size());
    const std::filesystem::path manifest_root = (working_directory.empty() ? source_root : working_directory) /
                                                (".afternight-bytecode-" + an::ui::extensions::generateOpaqueToken());
    std::error_code ec;
    std::filesystem::create_directories(windowsExtendedLengthPath(manifest_root), ec);
    if (ec)
    {
        setFilesystemError(error, "Failed to create bytecode precompile manifest directory", manifest_root, ec);
        return false;
    }

    const auto cleanup_manifests = [&]() {
        std::error_code cleanup_ec;
        std::filesystem::remove_all(windowsExtendedLengthPath(manifest_root), cleanup_ec);
    };

    std::vector<BytecodeWorkerProcess> workers;
    workers.reserve(static_cast<std::size_t>(worker_count));
    for (int worker_index = 0; worker_index < worker_count; ++worker_index)
    {
        const std::size_t begin_index =
            source_files.size() * static_cast<std::size_t>(worker_index) / static_cast<std::size_t>(worker_count);
        const std::size_t end_index =
            source_files.size() * static_cast<std::size_t>(worker_index + 1) / static_cast<std::size_t>(worker_count);
        if (begin_index >= end_index)
        {
            continue;
        }

        const std::filesystem::path manifest_path = manifest_root / ("worker_" + std::to_string(worker_index) + ".txt");
        if (!writeBytecodeWorkerManifest(manifest_path, source_files, begin_index, end_index, error))
        {
            cleanup_manifests();
            return false;
        }

        BytecodeWorkerProcess worker;
        worker.process = std::make_unique<QProcess>();
        worker.process->setProgram(QString::fromStdString(python_executable.string()));
        worker.process->setArguments(
            {QStringLiteral("-c"), QString::fromStdString(std::string(kProgressAwareCompileallWorkerScript)),
             QString::fromStdString(source_root.string()),
             QString::fromStdString(strip_prefix.empty() ? std::string() : strip_prefix.string()),
             QString::fromStdString(std::string(prepend_prefix)), QString::fromStdString(manifest_path.string())});
        worker.process->setProcessChannelMode(QProcess::MergedChannels);
        if (!working_directory.empty())
        {
            worker.process->setWorkingDirectory(QString::fromStdString(working_directory.string()));
        }
        if (process_environment)
        {
            worker.process->setProcessEnvironment(*process_environment);
        }
        worker.process->start();
        if (!worker.process->waitForStarted())
        {
            for (auto& started_worker : workers)
            {
                if (started_worker.process && started_worker.process->state() != QProcess::NotRunning)
                {
                    started_worker.process->kill();
                    started_worker.process->waitForFinished();
                }
            }
            if (error)
            {
                *error = "Failed to start Python bytecode precompilation worker: " + python_executable.generic_string();
            }
            cleanup_manifests();
            return false;
        }

        workers.push_back(std::move(worker));
    }

    int completed_count = 0;
    int active_workers = static_cast<int>(workers.size());
    bool worker_failed = false;
    std::string worker_failure_detail;
    QElapsedTimer timer;
    timer.start();

    const auto collect_worker_output = [&](BytecodeWorkerProcess& worker) {
        const QByteArray chunk = worker.process->readAll();
        if (chunk.isEmpty())
        {
            return;
        }

        worker.output += chunk;
        reportBytecodeProgressChunk(chunk, &worker.pending_progress_line, &completed_count, total_count,
                                    progress_callback, progress_phase_id, progress_message_prefix);
    };

    const auto finish_pending_worker_progress = [&](BytecodeWorkerProcess& worker) {
        if (worker.pending_progress_line.empty())
        {
            return;
        }

        reportBytecodeProgressLine(worker.pending_progress_line, &completed_count, total_count, progress_callback,
                                   progress_phase_id, progress_message_prefix);
        worker.pending_progress_line.clear();
    };

    const auto kill_running_workers = [&]() {
        for (auto& worker : workers)
        {
            if (worker.process && worker.process->state() != QProcess::NotRunning)
            {
                worker.process->kill();
            }
        }
        for (auto& worker : workers)
        {
            if (worker.process && worker.process->state() != QProcess::NotRunning)
            {
                worker.process->waitForFinished();
                collect_worker_output(worker);
                finish_pending_worker_progress(worker);
            }
        }
    };

    while (active_workers > 0)
    {
        for (auto& worker : workers)
        {
            if (worker.finished)
            {
                continue;
            }

            collect_worker_output(worker);
            if (worker.process->state() != QProcess::NotRunning)
            {
                continue;
            }

            collect_worker_output(worker);
            finish_pending_worker_progress(worker);
            worker.finished = true;
            --active_workers;

            if (worker.process->exitStatus() != QProcess::NormalExit || worker.process->exitCode() != 0)
            {
                worker_failed = true;
                if (worker_failure_detail.empty())
                {
                    worker_failure_detail = summarizeProcessOutput(worker.output);
                }
            }
        }

        if (cancellationRequested(cancellation_token))
        {
            kill_running_workers();
            cleanup_manifests();
            setOperationCancelledError(error);
            return false;
        }

        if (timeout_ms >= 0 && timer.elapsed() >= timeout_ms)
        {
            kill_running_workers();
            cleanup_manifests();
            if (error)
            {
                *error = "Process timed out: " + python_executable.generic_string();
            }
            return false;
        }

        if (active_workers <= 0)
        {
            break;
        }

        bool waited = false;
        for (auto& worker : workers)
        {
            if (!worker.finished && worker.process->state() != QProcess::NotRunning)
            {
                worker.process->waitForFinished(50);
                waited = true;
                break;
            }
        }
        if (!waited)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }

    cleanup_manifests();
    if (worker_failed)
    {
        if (error)
        {
            *error = worker_failure_detail.empty() ? "Python bytecode precompilation failed"
                                                   : "Python bytecode precompilation failed: " + worker_failure_detail;
        }
        return false;
    }

    return true;
}

bool precompileEnvironmentSitePackagesBytecode(const ExtensionEnvironmentSnapshot& snapshot,
                                               const QProcessEnvironment& bundled_python_environment,
                                               std::string* error,
                                               const ExtensionOperationCancellationToken& cancellation_token = {},
                                               const ExtensionOperationProgressCallback& progress_callback = {})
{
    const std::filesystem::path site_packages = environmentSitePackagesPath(snapshot.environment_path);
    const std::filesystem::path python_executable = snapshot.bundled_python_executable;
    const QProcessEnvironment* process_environment = &bundled_python_environment;

    std::string precompile_error;
    if (!precompilePythonBytecodeTree(python_executable, site_packages, snapshot.environment_path, process_environment,
                                      {}, {}, R"((^|[/\\])(tests?|benchmarks?)([/\\]|$)|.*\.tmpl\.py$)",
                                      &precompile_error, kDependencyBytecodePrecompileTimeoutMs, cancellation_token,
                                      progress_callback, "precompile_dependencies", "Precompiling dependency bytecode"))
    {
        if (cancellationRequested(cancellation_token))
        {
            if (error)
            {
                *error = precompile_error;
            }
            return false;
        }

        if (an::platform::isLoggingInitialized())
        {
            LOG_WARNING("Extension dependency bytecode precompilation skipped env='{}' reason={}",
                        pathForLog(snapshot.environment_path), precompile_error);
        }
    }

    return true;
}

[[nodiscard]] std::vector<std::string> splitString(const std::string_view value, const char separator)
{
    std::vector<std::string> parts;
    std::stringstream stream{std::string(value)};
    std::string item;
    while (std::getline(stream, item, separator))
    {
        if (!item.empty())
        {
            parts.push_back(std::move(item));
        }
    }

    return parts;
}

[[nodiscard]] std::string joinStringParts(const std::vector<std::string>& parts, const std::size_t end_index)
{
    std::ostringstream stream;
    for (std::size_t index = 0; index < end_index; ++index)
    {
        if (index > 0)
        {
            stream << '-';
        }
        stream << parts[index];
    }

    return stream.str();
}

[[nodiscard]] std::optional<std::pair<int, int>> parseMajorMinorVersion(const std::string_view value)
{
    const auto parts = splitString(value, '.');
    if (parts.size() < 2)
    {
        return std::nullopt;
    }

    for (std::size_t index = 0; index < 2; ++index)
    {
        if (parts[index].empty() || !std::all_of(parts[index].begin(), parts[index].end(),
                                                 [](const unsigned char ch) { return std::isdigit(ch) != 0; }))
        {
            return std::nullopt;
        }
    }

    return std::pair{std::stoi(parts[0]), std::stoi(parts[1])};
}

[[nodiscard]] std::optional<std::pair<int, int>> parseCpythonTag(const std::string_view value)
{
    if (!value.starts_with("cp"))
    {
        return std::nullopt;
    }

    const std::string digits(value.substr(2));
    if (digits.size() < 2 ||
        !std::all_of(digits.begin(), digits.end(), [](const unsigned char ch) { return std::isdigit(ch) != 0; }))
    {
        return std::nullopt;
    }

    const int major = digits.front() - '0';
    const int minor = std::stoi(digits.substr(1));
    return std::pair{major, minor};
}

[[nodiscard]] std::optional<std::string> bundledCpythonTag(const std::string& bundled_python_version)
{
    const auto version = parseMajorMinorVersion(bundled_python_version);
    if (!version.has_value())
    {
        return std::nullopt;
    }

    return "cp" + std::to_string(version->first) + std::to_string(version->second);
}

[[nodiscard]] bool runtimeTargetMatchesWheelPlatformTag(const std::string_view runtime_target,
                                                        const std::string_view platform_tag)
{
    if (platform_tag == "any")
    {
        return true;
    }

    if (runtime_target == "linux-clang-x86_64")
    {
        return platform_tag == "linux_x86_64" ||
               (platform_tag.starts_with("manylinux") && platform_tag.ends_with("_x86_64")) ||
               (platform_tag.starts_with("musllinux") && platform_tag.ends_with("_x86_64"));
    }

    if (runtime_target == "windows-msvc-x86_64")
    {
        return platform_tag == "win_amd64";
    }

    return false;
}

[[nodiscard]] bool wheelMatchesBundledPython(const WheelArtifactMatch::ParsedWheelArtifact& wheel,
                                             const ExtensionEnvironmentSnapshot& snapshot)
{
    if (wheel.isPurePython())
    {
        return true;
    }

    const auto bundled_version = parseMajorMinorVersion(snapshot.bundled_python_version);
    const auto bundled_tag = bundledCpythonTag(snapshot.bundled_python_version);
    if (!bundled_version.has_value() || !bundled_tag.has_value())
    {
        return false;
    }

    const bool exact_abi =
        std::find(wheel.abi_tags.begin(), wheel.abi_tags.end(), *bundled_tag) != wheel.abi_tags.end();
    const bool exact_python =
        std::find(wheel.python_tags.begin(), wheel.python_tags.end(), *bundled_tag) != wheel.python_tags.end();
    const bool abi_none = std::find(wheel.abi_tags.begin(), wheel.abi_tags.end(), "none") != wheel.abi_tags.end();
    if (abi_none && (exact_python ||
                     std::find(wheel.python_tags.begin(), wheel.python_tags.end(), "py3") != wheel.python_tags.end()))
    {
        return true;
    }

    if (exact_abi && (exact_python ||
                      std::find(wheel.python_tags.begin(), wheel.python_tags.end(), "py3") != wheel.python_tags.end()))
    {
        return true;
    }

    const bool abi3 = std::find(wheel.abi_tags.begin(), wheel.abi_tags.end(), "abi3") != wheel.abi_tags.end();
    if (!abi3)
    {
        return false;
    }

    for (const auto& python_tag : wheel.python_tags)
    {
        if (python_tag == "py3")
        {
            return bundled_version->first == 3;
        }

        const auto tag_version = parseCpythonTag(python_tag);
        if (tag_version.has_value() && tag_version->first == bundled_version->first &&
            tag_version->second <= bundled_version->second)
        {
            return true;
        }
    }

    return false;
}

[[nodiscard]] bool wheelMatchesRuntimeTarget(const WheelArtifactMatch::ParsedWheelArtifact& wheel,
                                             const ExtensionEnvironmentSnapshot& snapshot)
{
    return std::any_of(wheel.platform_tags.begin(), wheel.platform_tags.end(), [&](const std::string& platform_tag) {
        return runtimeTargetMatchesWheelPlatformTag(snapshot.runtime_target, platform_tag);
    });
}

bool validateWheelCompatibility(const WheelArtifactMatch::ParsedWheelArtifact& wheel,
                                const std::string_view artifact_name, const ExtensionManifest& manifest,
                                const ExtensionEnvironmentSnapshot& snapshot, std::string* error)
{
    if (wheel.isPurePython())
    {
        return true;
    }

    if (manifest.runtime_targets.empty())
    {
        if (error)
        {
            *error = "Compiled wheel artifact '" + std::string(artifact_name) +
                     "' requires runtime_targets to be declared in the extension manifest";
        }
        return false;
    }

    if (!wheelMatchesBundledPython(wheel, snapshot))
    {
        const std::string expected_tag = bundledCpythonTag(snapshot.bundled_python_version).value_or("cp314");
        if (error)
        {
            *error = "Wheel artifact '" + std::string(artifact_name) + "' is incompatible with bundled Python " +
                     snapshot.bundled_python_version + "; expected a wheel compatible with " + expected_tag;
        }
        return false;
    }

    if (!wheelMatchesRuntimeTarget(wheel, snapshot))
    {
        if (error)
        {
            *error = "Wheel artifact '" + std::string(artifact_name) + "' does not support runtime target " +
                     snapshot.runtime_target;
        }
        return false;
    }

    return true;
}

[[nodiscard]] std::optional<WheelArtifactMatch::ParsedWheelArtifact>
parseWheelArtifact(const std::filesystem::path& path)
{
    if (path.extension() != ".whl")
    {
        return std::nullopt;
    }

    const auto parts = splitString(path.stem().string(), '-');
    if (parts.size() < 5)
    {
        return std::nullopt;
    }

    const std::size_t version_index = parts.size() == 5 ? 1 : parts.size() - 5;
    const std::size_t python_index = parts.size() - 3;
    if (version_index == 0 || python_index >= parts.size())
    {
        return std::nullopt;
    }

    WheelArtifactMatch::ParsedWheelArtifact parsed;
    parsed.name = normalizeDistributionName(joinStringParts(parts, version_index));
    parsed.version = parts[version_index];
    parsed.python_tags = splitString(parts[python_index], '.');
    parsed.abi_tags = splitString(parts[python_index + 1], '.');
    parsed.platform_tags = splitString(parts[python_index + 2], '.');
    if (parsed.name.empty() || parsed.version.empty() || parsed.python_tags.empty() || parsed.abi_tags.empty() ||
        parsed.platform_tags.empty())
    {
        return std::nullopt;
    }

    return parsed;
}

bool parseLockedRequirementLine(const std::string& raw_line, LockedRequirement* out_requirement, std::string* error)
{
    const std::string line = trimWhitespace(raw_line);
    if (line.empty() || line[0] == '#')
    {
        return true;
    }

    const std::size_t equality = line.find("==");
    if (equality == std::string::npos)
    {
        if (error)
        {
            *error = "Dependency lock entries must use exact 'name==version' pins";
        }
        return false;
    }

    LockedRequirement requirement;
    requirement.name = normalizeDistributionName(line.substr(0, equality));

    std::size_t cursor = equality + 2;
    while (cursor < line.size() && std::isspace(static_cast<unsigned char>(line[cursor])) != 0)
    {
        ++cursor;
    }

    std::size_t version_end = cursor;
    while (version_end < line.size() && std::isspace(static_cast<unsigned char>(line[version_end])) == 0)
    {
        ++version_end;
    }
    requirement.version = line.substr(cursor, version_end - cursor);

    std::size_t hash_pos = line.find("--hash=sha256:", version_end);
    if (hash_pos == std::string::npos)
    {
        if (error)
        {
            *error = "Dependency lock entries must declare a sha256 hash";
        }
        return false;
    }

    while (hash_pos != std::string::npos)
    {
        const std::size_t hash_begin = hash_pos + std::string("--hash=sha256:").size();
        std::size_t hash_end = hash_begin;
        while (hash_end < line.size() && std::isspace(static_cast<unsigned char>(line[hash_end])) == 0)
        {
            ++hash_end;
        }

        const std::string hash_value = toLowerCopy(line.substr(hash_begin, hash_end - hash_begin));
        if (hash_value.empty())
        {
            if (error)
            {
                *error = "Dependency lock entries must include non-empty sha256 hashes";
            }
            return false;
        }

        requirement.sha256.push_back(hash_value);
        hash_pos = line.find("--hash=sha256:", hash_end);
    }

    if (requirement.name.empty() || requirement.version.empty() || requirement.sha256.empty())
    {
        if (error)
        {
            *error = "Dependency lock entries must include package, version, and sha256 hash";
        }
        return false;
    }

    if (out_requirement)
    {
        *out_requirement = std::move(requirement);
    }

    return true;
}

bool parseLockedRequirements(const std::filesystem::path& requirements_path,
                             std::vector<LockedRequirement>* out_requirements, std::string* error)
{
    std::ifstream input(requirements_path);
    if (!input.is_open())
    {
        if (error)
        {
            *error = "Failed to open dependency lock file";
        }
        return false;
    }

    std::vector<LockedRequirement> requirements;
    std::string line;
    while (std::getline(input, line))
    {
        LockedRequirement requirement;
        if (!parseLockedRequirementLine(line, &requirement, error))
        {
            return false;
        }
        if (!requirement.name.empty())
        {
            requirements.push_back(std::move(requirement));
        }
    }

    if (out_requirements)
    {
        *out_requirements = std::move(requirements);
    }
    return true;
}

[[nodiscard]] std::vector<std::filesystem::path> localFindLinkDirectories(const ExtensionManifest& manifest)
{
    std::vector<std::filesystem::path> directories;
    const std::filesystem::path package_root(manifest.package_root);
    for (const auto& item : manifest.dependencies.pip.find_links)
    {
        if (isUrlLike(item))
        {
            continue;
        }

        directories.push_back((package_root / item).lexically_normal());
    }

    return directories;
}

bool resolveLocalWheelArtifacts(const ExtensionManifest& manifest, const ExtensionEnvironmentSnapshot& snapshot,
                                const std::vector<LockedRequirement>& requirements,
                                std::vector<WheelArtifactMatch>* out_matches,
                                std::vector<WheelArtifactMatch>* out_extra_matches, std::string* error)
{
    const auto wheel_directories = localFindLinkDirectories(manifest);
    std::map<std::pair<std::string, std::string>, std::vector<WheelArtifactMatch>> available;
    for (const auto& directory : wheel_directories)
    {
        for (const auto& file : sortedFilesUnder(directory))
        {
            const auto parsed = parseWheelArtifact(file);
            if (!parsed)
            {
                continue;
            }

            WheelArtifactMatch artifact;
            artifact.name = parsed->name;
            artifact.version = parsed->version;
            artifact.path = file;
            artifact.sha256 = hashFile(file);
            artifact.parsed = *parsed;
            available[{artifact.name, artifact.version}].push_back(std::move(artifact));
        }
    }

    std::vector<WheelArtifactMatch> matches;
    std::set<std::pair<std::string, std::string>> matched_keys;
    std::vector<std::string> missing;
    for (const auto& requirement : requirements)
    {
        const auto key = std::make_pair(requirement.name, requirement.version);
        const auto it = available.find(key);
        if (it == available.end())
        {
            missing.push_back(requirement.name + "==" + requirement.version);
            continue;
        }

        const auto hash_matches = [&](const WheelArtifactMatch& artifact) {
            return requirement.sha256.empty() || std::find(requirement.sha256.begin(), requirement.sha256.end(),
                                                           artifact.sha256) != requirement.sha256.end();
        };

        const WheelArtifactMatch* selected_artifact = nullptr;
        std::string compatibility_error;
        for (const auto& artifact : it->second)
        {
            if (!hash_matches(artifact))
            {
                continue;
            }

            std::string local_error;
            if (validateWheelCompatibility(artifact.parsed, artifact.path.filename().generic_string(), manifest,
                                           snapshot, &local_error))
            {
                selected_artifact = &artifact;
                break;
            }

            if (compatibility_error.empty())
            {
                compatibility_error = std::move(local_error);
            }
        }

        if (selected_artifact == nullptr)
        {
            if (!compatibility_error.empty())
            {
                if (error)
                {
                    *error = compatibility_error;
                }
                return false;
            }

            missing.push_back(requirement.name + "==" + requirement.version);
            continue;
        }

        matches.push_back(*selected_artifact);
        matched_keys.insert(key);
    }

    if (!missing.empty())
    {
        if (error)
        {
            std::ostringstream message;
            message << "Offline dependency provisioning is missing wheel artifacts for ";
            for (std::size_t index = 0; index < missing.size(); ++index)
            {
                if (index > 0)
                {
                    message << ", ";
                }
                message << missing[index];
            }
            message << ". Expected wheelhouse paths:";
            for (const auto& directory : wheel_directories)
            {
                message << ' ' << directory.generic_string();
            }
            *error = message.str();
        }
        return false;
    }

    if (out_matches)
    {
        *out_matches = std::move(matches);
    }
    if (out_extra_matches)
    {
        std::vector<WheelArtifactMatch> extra_matches;
        for (const auto& [key, artifacts] : available)
        {
            if (matched_keys.find(key) != matched_keys.end())
            {
                continue;
            }

            for (const auto& artifact : artifacts)
            {
                std::string local_error;
                if (!validateWheelCompatibility(artifact.parsed, artifact.path.filename().generic_string(), manifest,
                                                snapshot, &local_error))
                {
                    continue;
                }

                extra_matches.push_back(artifact);
                break;
            }
        }

        *out_extra_matches = std::move(extra_matches);
    }
    return true;
}

[[nodiscard]] std::vector<ExtensionInstalledDistribution>
wheelMatchesToDistributions(const std::vector<WheelArtifactMatch>& wheels)
{
    std::vector<ExtensionInstalledDistribution> distributions;
    distributions.reserve(wheels.size());
    for (const auto& wheel : wheels)
    {
        ExtensionInstalledDistribution distribution;
        distribution.name = wheel.name;
        distribution.version = wheel.version;
        distribution.source = wheel.path.generic_string();
        distribution.artifact_hash = wheel.sha256;
        distributions.push_back(std::move(distribution));
    }
    return distributions;
}

[[nodiscard]] std::string artifactFilenameFromSource(const std::string_view source)
{
    const std::size_t query_pos = source.find_first_of("?#");
    const std::string_view trimmed = query_pos == std::string_view::npos ? source : source.substr(0, query_pos);
    const std::size_t slash = trimmed.find_last_of('/');
    return slash == std::string_view::npos ? std::string(trimmed) : std::string(trimmed.substr(slash + 1));
}

bool validateResolvedWheelDistributions(const std::vector<ExtensionInstalledDistribution>& distributions,
                                        const ExtensionManifest& manifest, const ExtensionEnvironmentSnapshot& snapshot,
                                        std::string* error)
{
    for (const auto& distribution : distributions)
    {
        const std::string artifact_name = artifactFilenameFromSource(distribution.source);
        const auto parsed = parseWheelArtifact(std::filesystem::path(artifact_name));
        if (!parsed.has_value())
        {
            continue;
        }

        if (!validateWheelCompatibility(*parsed, artifact_name, manifest, snapshot, error))
        {
            return false;
        }
    }

    return true;
}

[[nodiscard]] std::vector<ExtensionInstalledDistribution>
pipReportToDistributions(const std::filesystem::path& report_path)
{
    std::vector<ExtensionInstalledDistribution> distributions;
    QJsonObject report;
    if (!loadInstallReceipt(report_path, &report))
    {
        return distributions;
    }

    const QJsonArray installs = report.value(QStringLiteral("install")).toArray();
    for (const auto& value : installs)
    {
        const QJsonObject item = value.toObject();
        const QJsonObject metadata = item.value(QStringLiteral("metadata")).toObject();
        const QJsonObject download_info = item.value(QStringLiteral("download_info")).toObject();
        const QJsonObject archive_info = download_info.value(QStringLiteral("archive_info")).toObject();

        ExtensionInstalledDistribution distribution;
        distribution.name = metadata.value(QStringLiteral("name")).toString().toStdString();
        distribution.version = metadata.value(QStringLiteral("version")).toString().toStdString();
        distribution.source = download_info.value(QStringLiteral("url")).toString().toStdString();
        distribution.artifact_hash = archive_info.value(QStringLiteral("hash")).toString().toStdString();
        if (distribution.artifact_hash.rfind("sha256=", 0) == 0)
        {
            distribution.artifact_hash.erase(0, std::string("sha256=").size());
        }

        if (!distribution.name.empty())
        {
            distributions.push_back(std::move(distribution));
        }
    }

    return distributions;
}

bool installWheelArtifacts(const std::filesystem::path& pip_driver, const QProcessEnvironment& pip_environment,
                           const std::filesystem::path& working_directory,
                           const std::filesystem::path& environment_path, const std::vector<WheelArtifactMatch>& wheels,
                           const bool offline_only, std::vector<ExtensionInstalledDistribution>* out_distributions,
                           std::string* error, const ExtensionOperationCancellationToken& cancellation_token = {})
{
    if (cancellationRequested(cancellation_token))
    {
        setOperationCancelledError(error);
        return false;
    }
    if (wheels.empty())
    {
        if (out_distributions)
        {
            out_distributions->clear();
        }
        return true;
    }

    const std::filesystem::path report_path = environment_path / (std::string(kPipInstallReportName) + ".root");
    QStringList pip_arguments = {QStringLiteral("-m"),         QStringLiteral("pip"),
                                 QStringLiteral("install"),    QStringLiteral("--disable-pip-version-check"),
                                 QStringLiteral("--no-input"), QStringLiteral("--no-compile"),
                                 QStringLiteral("--report"),   QString::fromStdString(report_path.string()),
                                 QStringLiteral("--no-deps")};
#ifdef _WIN32
    pip_arguments.push_back(QStringLiteral("--target"));
    pip_arguments.push_back(QString::fromStdString(environmentSitePackagesPath(environment_path).string()));
#endif
    if (offline_only)
    {
        pip_arguments.push_back(QStringLiteral("--no-index"));
    }
    for (const auto& wheel : wheels)
    {
        pip_arguments.push_back(QString::fromStdString(wheel.path.string()));
    }

    std::string process_output;
    if (!runProcess(pip_driver, pip_arguments, working_directory,
                    offline_only ? kOfflineProvisioningTimeoutMs : kOnlineProvisioningTimeoutMs, &pip_environment,
                    &process_output, error, &cancellation_token))
    {
        removeIfExists(report_path, nullptr);
        if (error && !process_output.empty() && *error != process_output)
        {
            *error = "Dependency provisioning failed: " + process_output;
        }
        return false;
    }

    auto installed_distributions = pipReportToDistributions(report_path);
    removeIfExists(report_path, nullptr);
    if (installed_distributions.empty())
    {
        installed_distributions = wheelMatchesToDistributions(wheels);
    }

    if (out_distributions)
    {
        *out_distributions = std::move(installed_distributions);
    }
    return true;
}

bool clearDirectoryContents(const std::filesystem::path& root, std::string* error)
{
    std::error_code ec;
    const std::filesystem::path removal_root = windowsExtendedLengthPath(root);
    if (!std::filesystem::exists(removal_root, ec))
    {
        if (ec)
        {
            setFilesystemError(error, "Failed to inspect environment directory before cleanup", root, ec);
            return false;
        }
        return true;
    }

    if (an::platform::isLoggingInitialized())
    {
        LOG_INFO("Clearing extension environment directory path='{}'", pathForLog(root));
    }

    for (std::filesystem::directory_iterator it(removal_root, ec), end; it != end && !ec; it.increment(ec))
    {
        const std::filesystem::path entry_path = root / it->path().filename();
        if (!removePathTreeWithDiagnostics(entry_path, "Failed to clear environment directory entry", error))
        {
            return false;
        }
    }

    if (ec)
    {
        setFilesystemError(error, "Failed to enumerate environment directory during cleanup", root, ec);
        return false;
    }

    return true;
}

[[nodiscard]] std::vector<std::filesystem::path>
sortedFilesUnder(const std::filesystem::path& root, const ExtensionOperationCancellationToken* cancellation_token)
{
    std::vector<std::filesystem::path> paths;
    std::error_code ec;
    if (!std::filesystem::exists(root, ec))
    {
        return paths;
    }
    if (std::filesystem::is_regular_file(root, ec))
    {
        paths.push_back(root);
        return paths;
    }

    for (std::filesystem::recursive_directory_iterator it(root, ec), end; it != end && !ec; it.increment(ec))
    {
        if (cancellation_token && cancellation_token->isCancellationRequested())
        {
            return {};
        }

        if (ec || !it->is_regular_file())
        {
            continue;
        }

        paths.push_back(it->path());
    }

    std::sort(paths.begin(), paths.end());
    return paths;
}

using FileCopyProgressCallback = std::function<void(const std::filesystem::path&, int, int)>;

[[nodiscard]] int copyProgressFileCount(const std::filesystem::path& source)
{
    if (isDirectory(source))
    {
        return static_cast<int>(sortedFilesUnder(source).size());
    }

    return isRegularFile(source) ? 1 : 0;
}

bool copyPathTree(const std::filesystem::path& source, const std::filesystem::path& destination, std::string* error,
                  const FileCopyProgressCallback& progress_callback = {})
{
    std::error_code ec;
    if (isDirectory(source))
    {
        std::filesystem::create_directories(windowsExtendedLengthPath(destination), ec);
        if (ec)
        {
            if (error)
            {
                *error = "Failed to create destination directory '" + destination.generic_string() + "'";
            }
            return false;
        }

        const auto files = sortedFilesUnder(source);
        const int total_files = static_cast<int>(files.size());
        int copied_files = 0;
        for (const auto& file : files)
        {
            ec.clear();
            const auto relative = std::filesystem::relative(file, source, ec);
            if (ec)
            {
                if (error)
                {
                    *error = "Failed to compute relative path while copying '" + file.generic_string() + "'";
                }
                return false;
            }

            const auto destination_file = destination / relative;
            std::filesystem::create_directories(windowsExtendedLengthPath(destination_file.parent_path()), ec);
            if (ec)
            {
                if (error)
                {
                    *error = "Failed to create parent directory for '" + destination_file.generic_string() + "'";
                }
                return false;
            }

            ec.clear();
            std::filesystem::copy_file(windowsExtendedLengthPath(file), windowsExtendedLengthPath(destination_file),
                                       std::filesystem::copy_options::overwrite_existing, ec);
            if (ec)
            {
                if (error)
                {
                    *error = "Failed to copy '" + file.generic_string() + "' to '" + destination_file.generic_string() +
                             "': " + ec.message();
                }
                return false;
            }

            ++copied_files;
            if (progress_callback)
            {
                progress_callback(file, copied_files, total_files);
            }
        }

        return true;
    }

    if (!isRegularFile(source))
    {
        if (error)
        {
            *error = "Source path is not a regular file or directory: " + source.generic_string();
        }
        return false;
    }

    std::filesystem::create_directories(windowsExtendedLengthPath(destination.parent_path()), ec);
    if (ec)
    {
        if (error)
        {
            *error = "Failed to create parent directory for '" + destination.generic_string() + "'";
        }
        return false;
    }

    ec.clear();
    std::filesystem::copy_file(windowsExtendedLengthPath(source), windowsExtendedLengthPath(destination),
                               std::filesystem::copy_options::overwrite_existing, ec);
    if (ec)
    {
        if (error)
        {
            *error = "Failed to copy '" + source.generic_string() + "' to '" + destination.generic_string() +
                     "': " + ec.message();
        }
        return false;
    }

    if (progress_callback)
    {
        progress_callback(source, 1, 1);
    }

    return true;
}

[[nodiscard]] std::string hashPathTree(const std::filesystem::path& root,
                                       const ExtensionOperationCancellationToken* cancellation_token,
                                       const ExtensionEnvironmentInspectionMode inspection_mode,
                                       const std::filesystem::path& cache_directory)
{
    if (cancellationRequested(cancellation_token))
    {
        return {};
    }

    std::error_code ec;
    if (!std::filesystem::exists(root, ec))
    {
        return sha256Hex({});
    }

    const auto resolved_cache_directory = [&cache_directory]() -> std::filesystem::path {
        if (!cache_directory.empty())
        {
            return cache_directory;
        }

        const QString app_data_root = an::ui::applicationLocalDataPath();
        if (app_data_root.isEmpty())
        {
            return {};
        }

        return std::filesystem::path(app_data_root.toStdString()) / std::string(kPathTreeHashCacheDirectory);
    }();

    const auto files = sortedFilesUnder(root, cancellation_token);
    if (cancellationRequested(cancellation_token))
    {
        return {};
    }

    const auto cache_guard_fingerprint = [&ec, &files, &root, cancellation_token]() -> std::string {
        std::ostringstream stream;
        for (const auto& file : files)
        {
            if (cancellationRequested(cancellation_token))
            {
                return {};
            }

            stream << std::filesystem::relative(file, root).generic_string() << '\n';

            ec.clear();
            const auto size = std::filesystem::file_size(file, ec);
            stream << (ec ? -1 : static_cast<long long>(size)) << '\n';

            ec.clear();
            const auto write_time = std::filesystem::last_write_time(file, ec);
            const auto write_time_ns =
                std::chrono::duration_cast<std::chrono::nanoseconds>(write_time.time_since_epoch()).count();
            stream << (ec ? -1LL : write_time_ns) << '\n';
        }

        return sha256Hex(stream.str());
    }();
    if (cancellationRequested(cancellation_token))
    {
        return {};
    }

    const auto cache_path = [&]() -> std::filesystem::path {
        if (resolved_cache_directory.empty())
        {
            return {};
        }

        const std::string normalized_root = root.lexically_normal().generic_string();
        if (normalized_root.empty())
        {
            return {};
        }

        return resolved_cache_directory / (sha256Hex(normalized_root) + ".json");
    }();

    if (inspection_mode == ExtensionEnvironmentInspectionMode::Routine && !cache_path.empty())
    {
        const std::string cached_text = readFileText(cache_path);
        if (!cached_text.empty())
        {
            QJsonParseError parse_error;
            const QJsonDocument document =
                QJsonDocument::fromJson(QByteArray::fromStdString(cached_text), &parse_error);
            if (parse_error.error == QJsonParseError::NoError && document.isObject())
            {
                const QJsonObject object = document.object();
                const int schema_version = object.value(QStringLiteral("schema_version")).toInt(-1);
                const std::string cached_cache_guard_fingerprint =
                    object.value(QStringLiteral("cache_guard_fingerprint")).toString().toStdString();
                const std::string cached_content_hash =
                    object.value(QStringLiteral("content_hash")).toString().toStdString();
                if (schema_version == kPathTreeHashCacheSchemaVersion && !cached_cache_guard_fingerprint.empty() &&
                    cached_cache_guard_fingerprint == cache_guard_fingerprint && !cached_content_hash.empty())
                {
                    return cached_content_hash;
                }
            }
        }
    }

    std::ostringstream stream;
    for (const auto& file : files)
    {
        if (cancellationRequested(cancellation_token))
        {
            return {};
        }

        stream << std::filesystem::relative(file, root).generic_string() << '\n';
        const std::string file_hash = hashFile(file, cancellation_token);
        if (cancellationRequested(cancellation_token))
        {
            return {};
        }
        stream << file_hash << '\n';
    }

    const std::string content_hash = sha256Hex(stream.str());

    if (!cache_path.empty())
    {
        QJsonObject object;
        object.insert(QStringLiteral("schema_version"), kPathTreeHashCacheSchemaVersion);
        object.insert(QStringLiteral("cache_guard_fingerprint"), QString::fromStdString(cache_guard_fingerprint));
        object.insert(QStringLiteral("content_hash"), QString::fromStdString(content_hash));

        std::string ignored_error;
        writeFileText(cache_path, QJsonDocument(object).toJson(QJsonDocument::Compact).toStdString(), &ignored_error);
    }

    return content_hash;
}

[[nodiscard]] std::string hashValidationSources(const ExtensionManifest& manifest,
                                                const ExtensionOperationCancellationToken* cancellation_token = nullptr)
{
    std::ostringstream stream;
    const std::filesystem::path package_root(manifest.package_root);
    std::error_code ec;
    if (!std::filesystem::exists(package_root, ec))
    {
        return sha256Hex(stream.str());
    }

    std::vector<std::filesystem::path> python_files;
    for (std::filesystem::recursive_directory_iterator it(package_root, ec), end; it != end && !ec; it.increment(ec))
    {
        if (cancellationRequested(cancellation_token))
        {
            return {};
        }

        if (ec || !it->is_regular_file())
        {
            continue;
        }

        const auto extension = it->path().extension().string();
        if (extension == ".py" || extension == ".pyi")
        {
            python_files.push_back(it->path());
        }
    }

    std::sort(python_files.begin(), python_files.end());
    for (const auto& file : python_files)
    {
        if (cancellationRequested(cancellation_token))
        {
            return {};
        }

        stream << std::filesystem::relative(file, package_root).generic_string() << '\n';
        const std::string file_hash = hashFile(file, cancellation_token);
        if (cancellationRequested(cancellation_token))
        {
            return {};
        }
        stream << file_hash << '\n';
    }

    return sha256Hex(stream.str());
}

[[nodiscard]] std::string stableManifestFingerprint(const ExtensionManifest& manifest)
{
    std::ostringstream stream;
    stream << manifest.id << '\n'
           << manifest.name << '\n'
           << manifest.version << '\n'
           << manifest.entry_point << '\n'
           << manifest.category << '\n'
           << manifest.python_compat << '\n'
           << static_cast<int>(manifest.sdk_backend) << '\n'
           << static_cast<int>(manifest.launch_mode) << '\n'
           << manifest.requires_exclusive_access << '\n';

    for (const auto& target : manifest.runtime_targets)
    {
        stream << "rt:" << target << '\n';
    }
    for (const auto& path : manifest.binary_vendor_paths)
    {
        stream << "bin:" << path << '\n';
    }
    stream << "dep.requirements:" << manifest.dependencies.requirements_file << '\n'
           << "dep.context:" << static_cast<int>(manifest.dependencies.dependency_context) << '\n'
           << "dep.shared_group:" << manifest.dependencies.shared_group << '\n'
           << "dep.shared_host_profile:" << manifest.dependencies.shared_host_profile << '\n'
           << "dep.require_hashes:" << manifest.dependencies.pip.require_hashes << '\n'
           << "dep.no_deps:" << manifest.dependencies.pip.no_deps << '\n';
    for (const auto& item : manifest.dependencies.pip.find_links)
    {
        stream << "dep.find_links:" << item << '\n';
    }
    for (const auto& item : manifest.dependencies.pip.index_urls)
    {
        stream << "dep.index_urls:" << item << '\n';
    }
    for (const auto& item : manifest.dependencies.pip.extra_index_urls)
    {
        stream << "dep.extra_index_urls:" << item << '\n';
    }
    for (const auto& item : manifest.dependencies.bundled_runtime_packages)
    {
        stream << "dep.bundled_runtime_packages:" << item << '\n';
    }

    for (const auto& process : manifest.processes)
    {
        stream << "proc:" << process.id << '\n'
               << process.id_suffix << '\n'
               << process.class_name << '\n'
               << process.display_name << '\n'
               << process.process_name << '\n'
               << process.action_object_name << '\n'
               << process.category << '\n'
               << process.capabilities.execute << '\n'
               << process.capabilities.preview << '\n'
               << process.capabilities.masks_input << '\n'
               << process.capabilities.masks_output << '\n'
               << process.capabilities.weights_input << '\n'
               << process.capabilities.starless_generator << '\n'
               << process.capabilities.star_mask_generator << '\n'
               << process.capabilities.stateful_sequence << '\n';
    }

    return sha256Hex(stream.str());
}

/// The Python minor version a bundled Python version string names - "3.14" of "3.14.4" - which is what the wheels
/// installed into an environment are built for: a patch release of the same minor keeps the environment.
[[nodiscard]] std::string pythonAbiOf(const std::string& version)
{
    const std::size_t major_end = version.find('.');
    if (major_end == std::string::npos)
    {
        return version;
    }
    const std::size_t minor_end = version.find('.', major_end + 1);
    return minor_end == std::string::npos ? version : version.substr(0, minor_end);
}

/// Where an environment lives: by its context, and inside it by what its installed contents are made from alone. A
/// private one is named `e-` and the start of its dependency fingerprint under its package's id, whatever the package's
/// version; earlier layouts named it by version and by the build it was made in too.
[[nodiscard]] std::filesystem::path resolveEnvironmentPath(const ExtensionManifest& manifest,
                                                           const std::filesystem::path& environments_root,
                                                           const std::string& dependency_fingerprint,
                                                           const std::string& runtime_target)
{
    switch (manifest.dependencies.dependency_context)
    {
    case ExtensionDependencyContext::SharedGroup:
        return environments_root / "_shared" / manifest.dependencies.shared_group / dependency_fingerprint /
               runtime_target;
    case ExtensionDependencyContext::SharedHost:
        return environments_root / "_host_shared" / manifest.dependencies.shared_host_profile / dependency_fingerprint /
               runtime_target;
    case ExtensionDependencyContext::Private:
        break;
    }

    return environments_root / manifest.id / shortFingerprintPathToken("e-", dependency_fingerprint) / runtime_target;
}

[[nodiscard]] bool usesSharedEnvironment(const ExtensionDependencyContext dependency_context)
{
    switch (dependency_context)
    {
    case ExtensionDependencyContext::SharedGroup:
    case ExtensionDependencyContext::SharedHost:
        return true;
    case ExtensionDependencyContext::Private:
        return false;
    }

    return false;
}

[[nodiscard]] bool isSharedEnvironmentPath(const ExtensionEnvironmentSnapshot& snapshot)
{
    std::error_code ec;
    const std::filesystem::path relative_path =
        std::filesystem::relative(snapshot.environment_path, snapshot.environments_root, ec);
    if (ec || relative_path.empty())
    {
        return false;
    }

    const auto it = relative_path.begin();
    if (it == relative_path.end())
    {
        return false;
    }

    const std::string root_component = it->generic_string();
    return root_component == "_shared" || root_component == "_host_shared";
}

[[nodiscard]] std::filesystem::path normalizedEnvironmentPath(const ExtensionEnvironmentSnapshot& snapshot)
{
    return snapshot.environment_path.lexically_normal();
}

[[nodiscard]] std::filesystem::path resolveEnvironmentsRoot(const ExtensionDiscoveryContext& context)
{
    return context.environments_root.empty() ? an::ui::extensions::user_extension_environments_root()
                                             : context.environments_root;
}

bool loadInstallReceipt(const std::filesystem::path& path, QJsonObject* out_object)
{
    const std::string text = readFileText(path);
    if (text.empty())
    {
        return false;
    }

    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(QByteArray::fromStdString(text), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject())
    {
        return false;
    }

    if (out_object)
    {
        *out_object = document.object();
    }
    return true;
}

bool writeFileText(const std::filesystem::path& path, const std::string& text, std::string* error)
{
    std::error_code ec;
    std::filesystem::create_directories(windowsExtendedLengthPath(path.parent_path()), ec);
    if (ec)
    {
        setFilesystemError(error, "Failed to create environment directories", path.parent_path(), ec);
        return false;
    }

    std::ofstream out(windowsExtendedLengthPath(path), std::ios::binary | std::ios::trunc);
    if (!out.is_open())
    {
        if (error)
        {
            *error = "Failed to write environment file: path='" + pathForLog(path) + "'";
        }
        if (an::platform::isLoggingInitialized())
        {
            LOG_WARNING("Extension environment filesystem operation failed: failed to open file for write path='{}'",
                        pathForLog(path));
        }
        return false;
    }

    out << text;
    if (!out.good())
    {
        if (error)
        {
            *error = "Failed to flush environment file: path='" + pathForLog(path) + "'";
        }
        if (an::platform::isLoggingInitialized())
        {
            LOG_WARNING("Extension environment filesystem operation failed: failed to flush file path='{}'",
                        pathForLog(path));
        }
        return false;
    }

    return true;
}

bool removeIfExists(const std::filesystem::path& path, std::string* error)
{
    std::error_code ec;
    std::filesystem::remove(windowsExtendedLengthPath(path), ec);
    if (ec)
    {
        setFilesystemError(error, "Failed to remove environment file", path, ec);
        return false;
    }

    return true;
}

bool clearReadyEnvironmentMarkers(const an::ui::extensions::ExtensionEnvironmentSnapshot& snapshot, std::string* error)
{
    return removeIfExists(snapshot.environment_path / std::string(kReadyMarkerName), error) &&
           removeIfExists(snapshot.environment_path / std::string(kInstallReceiptName), error);
}

[[nodiscard]] std::vector<std::filesystem::directory_entry> sortedDirectoryEntries(const std::filesystem::path& root)
{
    std::vector<std::filesystem::directory_entry> entries;
    std::error_code ec;
    if (!std::filesystem::exists(root, ec) || !std::filesystem::is_directory(root, ec))
    {
        return entries;
    }

    for (std::filesystem::directory_iterator it(root, ec), end; it != end && !ec; it.increment(ec))
    {
        if (ec || !it->is_directory())
        {
            continue;
        }

        entries.push_back(*it);
    }

    std::sort(entries.begin(), entries.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.path().filename().generic_string() < rhs.path().filename().generic_string();
    });
    return entries;
}

void appendPerExtensionInventory(const std::filesystem::path& extension_root,
                                 std::vector<ExtensionEnvironmentInventoryEntry>& out_entries)
{
    const std::string extension_id = extension_root.filename().generic_string();
    const auto append_entry = [&](const std::filesystem::path& environment_path, const std::string& extension_version,
                                  const std::string& dependency_fingerprint, const std::string& runtime_target) {
        ExtensionEnvironmentInventoryEntry entry;
        entry.environment_path = environment_path;
        entry.extension_id = extension_id;
        entry.extension_version = extension_version;
        entry.dependency_fingerprint = dependency_fingerprint;
        entry.runtime_target = runtime_target;
        out_entries.push_back(std::move(entry));
    };

    for (const auto& version_entry : sortedDirectoryEntries(extension_root))
    {
        // An environment named by its dependency fingerprint alone holds its runtime targets directly; earlier layouts
        // put it under a version and a fingerprint.
        bool holdsTargets = false;
        for (const auto& targetEntry : sortedDirectoryEntries(version_entry.path()))
        {
            const std::string targetName = targetEntry.path().filename().generic_string();
            if (isKnownRuntimeTargetName(targetName))
            {
                holdsTargets = true;
                append_entry(targetEntry.path(), std::string(), version_entry.path().filename().generic_string(),
                             targetName);
            }
        }
        if (holdsTargets)
        {
            continue;
        }

        const std::string extension_version = version_entry.path().filename().generic_string();
        for (const auto& fingerprint_entry : sortedDirectoryEntries(version_entry.path()))
        {
            const std::string dependency_fingerprint = fingerprint_entry.path().filename().generic_string();
            for (const auto& child_entry : sortedDirectoryEntries(fingerprint_entry.path()))
            {
                const std::string child_name = child_entry.path().filename().generic_string();
                if (isKnownRuntimeTargetName(child_name))
                {
                    append_entry(child_entry.path(), extension_version, dependency_fingerprint, child_name);
                    continue;
                }

                for (const auto& runtime_entry : sortedDirectoryEntries(child_entry.path()))
                {
                    const std::string runtime_target = runtime_entry.path().filename().generic_string();
                    if (!isKnownRuntimeTargetName(runtime_target))
                    {
                        continue;
                    }
                    append_entry(runtime_entry.path(), extension_version, dependency_fingerprint, runtime_target);
                }
            }
        }
    }
}

void appendSharedEnvironmentInventory(const std::filesystem::path& shared_root,
                                      std::vector<ExtensionEnvironmentInventoryEntry>& out_entries)
{
    for (const auto& group_entry : sortedDirectoryEntries(shared_root))
    {
        const std::string shared_group = group_entry.path().filename().generic_string();
        for (const auto& fingerprint_entry : sortedDirectoryEntries(group_entry.path()))
        {
            const std::string dependency_fingerprint = fingerprint_entry.path().filename().generic_string();
            for (const auto& runtime_entry : sortedDirectoryEntries(fingerprint_entry.path()))
            {
                ExtensionEnvironmentInventoryEntry entry;
                entry.environment_path = runtime_entry.path();
                entry.shared_group = shared_group;
                entry.dependency_fingerprint = dependency_fingerprint;
                entry.runtime_target = runtime_entry.path().filename().generic_string();
                out_entries.push_back(std::move(entry));
            }
        }
    }
}

void appendHostSharedEnvironmentInventory(const std::filesystem::path& shared_root,
                                          std::vector<ExtensionEnvironmentInventoryEntry>& out_entries)
{
    for (const auto& profile_entry : sortedDirectoryEntries(shared_root))
    {
        const std::string shared_host_profile = profile_entry.path().filename().generic_string();
        for (const auto& fingerprint_entry : sortedDirectoryEntries(profile_entry.path()))
        {
            const std::string dependency_fingerprint = fingerprint_entry.path().filename().generic_string();
            for (const auto& runtime_entry : sortedDirectoryEntries(fingerprint_entry.path()))
            {
                ExtensionEnvironmentInventoryEntry entry;
                entry.environment_path = runtime_entry.path();
                entry.shared_host_profile = shared_host_profile;
                entry.dependency_fingerprint = dependency_fingerprint;
                entry.runtime_target = runtime_entry.path().filename().generic_string();
                out_entries.push_back(std::move(entry));
            }
        }
    }
}

[[nodiscard]] std::string environmentIdentity(const ExtensionManifest& manifest)
{
    switch (manifest.dependencies.dependency_context)
    {
    case ExtensionDependencyContext::Private:
        return "private:" + manifest.id;
    case ExtensionDependencyContext::SharedGroup:
        return "shared-group:" + manifest.dependencies.shared_group;
    case ExtensionDependencyContext::SharedHost:
        return "shared-host:" + manifest.dependencies.shared_host_profile;
    }

    return {};
}

[[nodiscard]] std::string environmentIdentity(const ExtensionEnvironmentInventoryEntry& entry)
{
    if (!entry.extension_id.empty())
    {
        return "private:" + entry.extension_id;
    }
    if (!entry.shared_group.empty())
    {
        return "shared-group:" + entry.shared_group;
    }
    if (!entry.shared_host_profile.empty())
    {
        return "shared-host:" + entry.shared_host_profile;
    }

    return {};
}

[[nodiscard]] std::uintmax_t
environmentDirectorySize(const std::filesystem::path& root,
                         const ExtensionOperationCancellationToken* cancellation_token = nullptr)
{
    std::uintmax_t size = 0;
    std::error_code ec;
    for (std::filesystem::recursive_directory_iterator
             it(windowsExtendedLengthPath(root), std::filesystem::directory_options::skip_permission_denied, ec),
         end;
         it != end && !ec; it.increment(ec))
    {
        if (cancellationRequested(cancellation_token))
        {
            break;
        }
        // A link is not the file it points to, which is counted where it lies: a venv links its interpreter, outside
        // the environment, several times over.
        if (it->is_symlink(ec) || ec || !it->is_regular_file(ec) || ec)
        {
            ec.clear();
            continue;
        }

        const std::uintmax_t file_size = it->file_size(ec);
        if (!ec)
        {
            size += file_size;
        }
        else
        {
            ec.clear();
        }
    }

    return size;
}

[[nodiscard]] std::filesystem::file_time_type environmentLastWriteTime(const std::filesystem::path& root)
{
    std::filesystem::file_time_type latest{};
    bool found = false;
    const auto update_latest = [&found, &latest](const std::filesystem::path& path) {
        std::error_code ec;
        const auto write_time = std::filesystem::last_write_time(windowsExtendedLengthPath(path), ec);
        if (!ec && (!found || write_time > latest))
        {
            latest = write_time;
            found = true;
        }
    };

    update_latest(root);
    update_latest(root / std::string(kReadyMarkerName));
    update_latest(root / std::string(kInstallReceiptName));
    update_latest(root / std::string(kProvisioningLockName));
    update_latest(root / std::string(kFailureDiagnosticName));
    return found ? latest : std::filesystem::file_time_type{};
}

[[nodiscard]] bool cleanupAgeReached(const ExtensionEnvironmentInventoryEntry& entry,
                                     const an::ui::extensions::ExtensionEnvironmentCleanupPolicy& policy)
{
    if (policy.minimum_age <= std::chrono::seconds::zero() ||
        entry.last_write_time == std::filesystem::file_time_type{})
    {
        return true;
    }

    return std::filesystem::file_time_type::clock::now() - entry.last_write_time >= policy.minimum_age;
}

[[nodiscard]] bool cleanupEligible(const ExtensionEnvironmentInventoryEntry& entry,
                                   const an::ui::extensions::ExtensionEnvironmentCleanupPolicy& policy)
{
    if (!entry.orphaned || (policy.protect_provisioning && entry.provisioning))
    {
        return false;
    }
    if (policy.protect_installed_without_ready_replacement && entry.installed_identity_present &&
        !entry.ready_replacement_available)
    {
        return false;
    }

    return cleanupAgeReached(entry, policy);
}

} // anonymous namespace

namespace an::ui::extensions
{

std::filesystem::path user_extension_environments_root()
{
    const QString root = an::ui::applicationLocalDataPath();
    return std::filesystem::path(root.toStdString()) / "ExtensionEnvs";
}

std::filesystem::path bundled_python_executable_for_app_dir(const std::filesystem::path& app_dir)
{
#ifdef _WIN32
    const std::vector<std::filesystem::path> candidates = {app_dir / "PythonLib" / "Scripts" / "python.exe",
                                                           app_dir / "PythonLib" / "python.exe", app_dir / "python.exe",
                                                           app_dir / "python3.exe", app_dir / "python3.14.exe"};
#else
    const std::vector<std::filesystem::path> candidates = {app_dir / "PythonLib" / "bin" / "python3.14",
                                                           app_dir / "PythonLib" / "bin" / "python3",
                                                           app_dir / "PythonLib" / "bin" / "python",
                                                           app_dir / "python3.14",
                                                           app_dir / "python3",
                                                           app_dir / "python"};
#endif

    for (const auto& candidate : candidates)
    {
        if (isRegularFile(candidate))
        {
            return candidate.lexically_normal();
        }
    }

    return {};
}

ExtensionEnvironmentCleanupPolicy automatic_extension_environment_cleanup_policy()
{
    ExtensionEnvironmentCleanupPolicy policy;
    policy.minimum_age = std::chrono::hours(24);
    return policy;
}

std::string ExtensionEnvironmentManager::computeHostBootstrapFingerprint(const ExtensionDiscoveryContext& context) const
{
    if (!context.host_bootstrap_fingerprint.empty())
    {
        return context.host_bootstrap_fingerprint;
    }

    std::ostringstream stream;
    stream << context.app_version << '\n'
           << context.bundled_python_version << '\n'
           << context.runtime_target << '\n'
           << context.runtime_backend_available << '\n'
           << context.protocol_backend_available << '\n'
           << context.rpc_backend_available << '\n'
           << context.workflow_launch_mode_available << '\n'
           << packagePathIdentity(context.runtime_root, context.package) << '\n';
    if (context.package)
    {
        stream << "package\n" << context.package->identity << '\n';
    }
    appendBootstrapArtifactIdentity(stream, "bundled_python", context.bundled_python_executable, context.package);
    appendBootstrapArtifactIdentity(stream, "extension_host", context.extension_host_executable, context.package);
    appendBootstrapArtifactIdentity(stream, "extension_host_lite", context.extension_host_lite_executable,
                                    context.package);
    return sha256Hex(stream.str());
}

ExtensionEnvironmentSnapshot
ExtensionEnvironmentManager::inspectEnvironment(const ExtensionManifest& manifest, const ExtensionPackageSource source,
                                                const ExtensionDiscoveryContext& context,
                                                ExtensionOperationCancellationToken cancellation_token,
                                                const ExtensionEnvironmentInspectionMode inspection_mode) const
{
    ExtensionEnvironmentSnapshot snapshot;
    const auto stop_if_cancelled = [&snapshot, &cancellation_token]() {
        if (!cancellationRequested(cancellation_token))
        {
            return false;
        }

        snapshot.state = ExtensionEnvironmentState::Missing;
        snapshot.diagnostic = "Extension environment inspection cancelled.";
        return true;
    };
    if (stop_if_cancelled())
    {
        return snapshot;
    }

    snapshot.bundled_python_version = context.bundled_python_version;
    snapshot.runtime_target = context.runtime_target;
    snapshot.host_bootstrap_fingerprint = computeHostBootstrapFingerprint(context);
    snapshot.bundled_python_executable = resolveBundledPythonExecutable(context);
    snapshot.runtime_root = context.runtime_root;
    snapshot.environments_root = resolveEnvironmentsRoot(context);
    const std::filesystem::path path_tree_hash_cache_directory =
        snapshot.environments_root / std::string(kPathTreeHashCacheDirectory);

    // Only what an environment's installed contents are made from names it: what the application gives it - the
    // interpreter it names, the runtime packages it copies, their bytecode - and the revision of the package it was
    // last checked for are refreshed in place, so a new build or a new version of the package keeps the environment.
    std::ostringstream dependency_stream;
    dependency_stream << "python-abi:" << pythonAbiOf(context.bundled_python_version) << '\n'
                      << "dep.context:" << static_cast<int>(manifest.dependencies.dependency_context) << '\n';

    if (manifest.dependencies.dependency_context == ExtensionDependencyContext::Private)
    {
        dependency_stream << manifest.id << '\n';
    }

    dependency_stream << manifest.dependencies.requirements_file << '\n'
                      << manifest.dependencies.shared_group << '\n'
                      << manifest.dependencies.shared_host_profile << '\n'
                      << manifest.dependencies.pip.require_hashes << '\n'
                      << manifest.dependencies.pip.no_deps << '\n';

    if (manifest.dependencies.dependency_context == ExtensionDependencyContext::SharedHost)
    {
        const std::string& profile = manifest.dependencies.shared_host_profile;
        snapshot.shared_host_profile_receipt_path =
            sharedHostProfileReceiptPath(snapshot.environments_root, profile, snapshot.runtime_target);
        const std::string runtime_root_identity = packagePathIdentity(snapshot.runtime_root, context.package);
        const std::string cache_key = sharedHostProfileFingerprintCacheKey(
            profile, snapshot.runtime_target, snapshot.host_bootstrap_fingerprint, runtime_root_identity);

        std::string profile_fingerprint;
        if (inspection_mode == ExtensionEnvironmentInspectionMode::Routine)
        {
            const std::lock_guard lock(m_sharedHostProfileFingerprintCacheMutex);
            const auto cached = m_sharedHostProfileFingerprintCache.find(cache_key);
            if (cached != m_sharedHostProfileFingerprintCache.end())
            {
                profile_fingerprint = cached->second;
                snapshot.shared_host_profile_fingerprint_source =
                    ExtensionSharedHostProfileFingerprintSource::DiscoveryCache;
            }
        }

        if (profile_fingerprint.empty() && inspection_mode == ExtensionEnvironmentInspectionMode::Routine)
        {
            const auto receipt_fingerprint = loadSharedHostProfileReceipt(
                snapshot.shared_host_profile_receipt_path, profile, snapshot.runtime_target,
                snapshot.host_bootstrap_fingerprint, runtime_root_identity);
            if (receipt_fingerprint)
            {
                profile_fingerprint = *receipt_fingerprint;
                snapshot.shared_host_profile_fingerprint_source =
                    ExtensionSharedHostProfileFingerprintSource::ProvisioningReceipt;
            }
        }

        if (profile_fingerprint.empty())
        {
            profile_fingerprint = sharedHostProfileDependencyFingerprint(
                profile, snapshot.runtime_root, &cancellation_token, ExtensionEnvironmentInspectionMode::VerifyContents,
                path_tree_hash_cache_directory);
            if (stop_if_cancelled())
            {
                return snapshot;
            }

            snapshot.shared_host_profile_fingerprint_source = ExtensionSharedHostProfileFingerprintSource::ContentAudit;
            std::string receipt_error;
            if (!writeSharedHostProfileReceipt(snapshot.shared_host_profile_receipt_path, profile,
                                               snapshot.runtime_target, snapshot.host_bootstrap_fingerprint,
                                               runtime_root_identity, profile_fingerprint, &receipt_error) &&
                an::platform::isLoggingInitialized())
            {
                LOG_WARNING("Failed to persist shared-host profile receipt profile='{}' path='{}': {}", profile,
                            pathForLog(snapshot.shared_host_profile_receipt_path), receipt_error);
            }
        }

        {
            const std::lock_guard lock(m_sharedHostProfileFingerprintCacheMutex);
            m_sharedHostProfileFingerprintCache[cache_key] = profile_fingerprint;
        }
        dependency_stream << profile_fingerprint << '\n';
        if (stop_if_cancelled())
        {
            return snapshot;
        }
    }

    const std::filesystem::path package_root(manifest.package_root);
    if (!manifest.dependencies.requirements_file.empty())
    {
        const auto requirements_path = package_root / manifest.dependencies.requirements_file;
        dependency_stream << hashFile(requirements_path) << '\n';
    }

    for (const auto& item : manifest.dependencies.pip.find_links)
    {
        if (stop_if_cancelled())
        {
            return snapshot;
        }

        dependency_stream << item << '\n';
        if (isUrlLike(item))
        {
            continue;
        }

        const auto path = package_root / item;
        dependency_stream << hashPathTree(path, &cancellation_token, inspection_mode, path_tree_hash_cache_directory)
                          << '\n';
        if (stop_if_cancelled())
        {
            return snapshot;
        }
    }
    for (const auto& item : manifest.dependencies.pip.index_urls)
    {
        dependency_stream << item << '\n';
    }
    for (const auto& item : manifest.dependencies.pip.extra_index_urls)
    {
        dependency_stream << item << '\n';
    }
    for (const auto& item : manifest.dependencies.bundled_runtime_packages)
    {
        dependency_stream << item << '\n';
    }

    snapshot.dependency_fingerprint = sha256Hex(dependency_stream.str());

    std::ostringstream validation_stream;
    validation_stream << snapshot.host_bootstrap_fingerprint << '\n'
                      << snapshot.dependency_fingerprint << '\n'
                      << stableManifestFingerprint(manifest) << '\n'
                      << hashValidationSources(manifest, &cancellation_token) << '\n';
    if (stop_if_cancelled())
    {
        return snapshot;
    }

    for (const auto& path : manifest.binary_vendor_paths)
    {
        validation_stream << path << '\n'
                          << hashPathTree(package_root / path, &cancellation_token, inspection_mode,
                                          path_tree_hash_cache_directory)
                          << '\n';
        if (stop_if_cancelled())
        {
            return snapshot;
        }
    }

    snapshot.validation_fingerprint = sha256Hex(validation_stream.str());
    snapshot.environment_path = resolveEnvironmentPath(manifest, snapshot.environments_root,
                                                       snapshot.dependency_fingerprint, context.runtime_target);
    snapshot.receipt_path = snapshot.environment_path / std::string(kInstallReceiptName);

    const std::filesystem::path provisioning_lock = snapshot.environment_path / std::string(kProvisioningLockName);
    const std::filesystem::path ready_marker = snapshot.environment_path / std::string(kReadyMarkerName);
    const std::filesystem::path failure_diagnostic = snapshot.environment_path / std::string(kFailureDiagnosticName);

    const auto failure_message = [&]() { return trimTrailingWhitespace(readFileText(failure_diagnostic)); };

    std::error_code ec;
    if (!std::filesystem::exists(snapshot.environment_path, ec))
    {
        snapshot.state = ExtensionEnvironmentState::Missing;
        snapshot.diagnostic = "Extension environment has not been provisioned yet.";
        return snapshot;
    }

    if (!std::filesystem::exists(ready_marker, ec) || !std::filesystem::exists(snapshot.receipt_path, ec))
    {
        if (std::filesystem::exists(provisioning_lock, ec))
        {
            if (hasActiveProvisioningLock(provisioning_lock))
            {
                snapshot.state = ExtensionEnvironmentState::Bootstrapping;
                snapshot.diagnostic = source == ExtensionPackageSource::Bundled
                                          ? "Bundled extension environment bootstrap is in progress."
                                          : "Extension environment provisioning is in progress.";
                return snapshot;
            }

            snapshot.state = ExtensionEnvironmentState::Failed;
            snapshot.diagnostic = failure_message();
            if (snapshot.diagnostic.empty())
            {
                snapshot.diagnostic =
                    "Extension environment provisioning was interrupted and needs repair before launch.";
            }
            return snapshot;
        }

        snapshot.state = ExtensionEnvironmentState::Failed;
        snapshot.diagnostic = failure_message();
        if (snapshot.diagnostic.empty())
        {
            snapshot.diagnostic = "Extension environment is incomplete and needs repair before launch.";
        }
        return snapshot;
    }

    // A ready environment whose lock is held is being refreshed or taken over by another operation, which records it
    // ready again when it is done.
    if (std::filesystem::exists(provisioning_lock, ec) && environmentLockHeld(provisioning_lock))
    {
        snapshot.state = ExtensionEnvironmentState::Bootstrapping;
        snapshot.diagnostic = "Extension environment is being refreshed for this build of AfterNight.";
        return snapshot;
    }

    QJsonObject receipt;
    if (!loadInstallReceipt(snapshot.receipt_path, &receipt))
    {
        snapshot.state = ExtensionEnvironmentState::Failed;
        snapshot.diagnostic = failure_message();
        if (snapshot.diagnostic.empty())
        {
            snapshot.diagnostic = "Extension install receipt is missing or invalid; repair is required.";
        }
        return snapshot;
    }

    const QString receipt_dependency = receipt.value(QStringLiteral("dependency_fingerprint")).toString();
    if (!receipt_dependency.isEmpty() && receipt_dependency.toStdString() != snapshot.dependency_fingerprint)
    {
        snapshot.state = ExtensionEnvironmentState::Stale;
        snapshot.diagnostic = "Extension dependency fingerprint changed and the environment must be rebuilt.";
        return snapshot;
    }

    const QString receipt_bootstrap = receipt.value(QStringLiteral("host_bootstrap_fingerprint")).toString();
    if (!receipt_bootstrap.isEmpty() && receipt_bootstrap.toStdString() != snapshot.host_bootstrap_fingerprint)
    {
        snapshot.state = ExtensionEnvironmentState::Stale;
        snapshot.refreshable = true;
        snapshot.diagnostic = "Extension environment was prepared for another build of AfterNight; it is refreshed "
                              "in place before its next launch, without installing anything again.";
        return snapshot;
    }

    const QString receipt_validation = receipt.value(QStringLiteral("validation_fingerprint")).toString();
    if (!usesSharedEnvironment(manifest.dependencies.dependency_context) && !receipt_validation.isEmpty() &&
        receipt_validation.toStdString() != snapshot.validation_fingerprint)
    {
        snapshot.state = ExtensionEnvironmentState::Stale;
        snapshot.refreshable = true;
        snapshot.diagnostic = "Extension environment was prepared for another revision of its package; it is refreshed "
                              "in place before its next launch, without installing anything again.";
        return snapshot;
    }

#ifndef _WIN32
    if (context.package)
    {
        std::string repoint_error;
        if (!pointEnvironmentAtInterpreter(snapshot.environment_path, snapshot.bundled_python_executable,
                                           &repoint_error))
        {
            snapshot.state = ExtensionEnvironmentState::Stale;
            snapshot.diagnostic =
                "Extension environment could not be pointed at this run's Python and must be repaired: " +
                repoint_error;
            return snapshot;
        }
    }
#endif

    snapshot.state = ExtensionEnvironmentState::Ready;
    return snapshot;
}

bool ExtensionEnvironmentManager::pointEnvironmentAtInterpreter(
    [[maybe_unused]] const std::filesystem::path& environmentPath,
    [[maybe_unused]] const std::filesystem::path& interpreter, [[maybe_unused]] std::string* error) const
{
#ifdef _WIN32
    return true;
#else
    return repointEnvironmentInterpreter(environmentPath, interpreter, error);
#endif
}

bool ExtensionEnvironmentManager::beginProvisioning(const ExtensionEnvironmentSnapshot& snapshot,
                                                    std::string* error) const
{
    const std::filesystem::path ready_marker = snapshot.environment_path / std::string(kReadyMarkerName);
    const std::filesystem::path receipt_path = snapshot.environment_path / std::string(kInstallReceiptName);
    const std::filesystem::path provisioning_lock = snapshot.environment_path / std::string(kProvisioningLockName);

    std::error_code ec;
    std::filesystem::create_directories(windowsExtendedLengthPath(snapshot.environment_path), ec);
    if (ec)
    {
        setFilesystemError(error, "Failed to create environment directory", snapshot.environment_path, ec);
        return false;
    }

    if (std::filesystem::exists(ready_marker, ec) && std::filesystem::exists(receipt_path, ec))
    {
        if (error)
        {
            *error = "Ready environments are immutable and cannot be reprovisioned in place";
        }
        return false;
    }

    if (!clearDirectoryContents(snapshot.environment_path, error))
    {
        return false;
    }

    std::ostringstream lock_contents;
    lock_contents << "dependency_fingerprint=" << snapshot.dependency_fingerprint << '\n'
                  << "validation_fingerprint=" << snapshot.validation_fingerprint << '\n'
                  << lockOwnerLines();
    if (!writeFileText(provisioning_lock, lock_contents.str(), error))
    {
        return false;
    }
    noteLockHeld(provisioning_lock);
    return true;
}

bool ExtensionEnvironmentManager::completeProvisioning(
    const ExtensionEnvironmentSnapshot& snapshot,
    const std::vector<ExtensionInstalledDistribution>& resolved_distributions, std::string* error,
    const std::string_view stagedRuntimeFingerprint) const
{
    const std::filesystem::path ready_marker = snapshot.environment_path / std::string(kReadyMarkerName);
    const std::filesystem::path receipt_path = snapshot.environment_path / std::string(kInstallReceiptName);
    const std::filesystem::path provisioning_lock = snapshot.environment_path / std::string(kProvisioningLockName);
    const std::filesystem::path failure_diagnostic = snapshot.environment_path / std::string(kFailureDiagnosticName);

    std::error_code ec;
    if (!std::filesystem::exists(provisioning_lock, ec))
    {
        if (error)
        {
            *error = "Provisioning has not been started for this environment";
        }
        return false;
    }

    QJsonArray distributions_json;
    for (const auto& distribution : resolved_distributions)
    {
        QJsonObject item;
        item.insert(QStringLiteral("name"), QString::fromStdString(distribution.name));
        item.insert(QStringLiteral("version"), QString::fromStdString(distribution.version));
        item.insert(QStringLiteral("source"), QString::fromStdString(distribution.source));
        item.insert(QStringLiteral("artifact_hash"), QString::fromStdString(distribution.artifact_hash));
        distributions_json.push_back(item);
    }

    QJsonObject receipt;
    receipt.insert(QStringLiteral("host_bootstrap_fingerprint"),
                   QString::fromStdString(snapshot.host_bootstrap_fingerprint));
    receipt.insert(QStringLiteral("dependency_fingerprint"), QString::fromStdString(snapshot.dependency_fingerprint));
    if (!isSharedEnvironmentPath(snapshot))
    {
        receipt.insert(QStringLiteral("validation_fingerprint"),
                       QString::fromStdString(snapshot.validation_fingerprint));
    }
    if (!stagedRuntimeFingerprint.empty())
    {
        receipt.insert(QStringLiteral("staged_runtime_fingerprint"),
                       QString::fromUtf8(stagedRuntimeFingerprint.data(),
                                         static_cast<qsizetype>(stagedRuntimeFingerprint.size())));
    }
    receipt.insert(QStringLiteral("resolved_distributions"), distributions_json);

    const std::string receipt_text = QJsonDocument(receipt).toJson(QJsonDocument::Indented).toStdString();
    if (!writeFileText(receipt_path, receipt_text, error) || !writeFileText(ready_marker, "ready\n", error) ||
        !removeIfExists(failure_diagnostic, error) || !releaseEnvironmentLock(provisioning_lock, error))
    {
        return false;
    }

    return true;
}

bool ExtensionEnvironmentManager::failProvisioning(const ExtensionEnvironmentSnapshot& snapshot,
                                                   const std::string_view diagnostic, std::string* error) const
{
    const std::filesystem::path ready_marker = snapshot.environment_path / std::string(kReadyMarkerName);
    const std::filesystem::path receipt_path = snapshot.environment_path / std::string(kInstallReceiptName);
    const std::filesystem::path provisioning_lock = snapshot.environment_path / std::string(kProvisioningLockName);
    const std::filesystem::path failure_diagnostic = snapshot.environment_path / std::string(kFailureDiagnosticName);

    std::error_code ec;
    std::filesystem::create_directories(windowsExtendedLengthPath(snapshot.environment_path), ec);
    if (ec)
    {
        setFilesystemError(error, "Failed to create environment directory", snapshot.environment_path, ec);
        return false;
    }

    const std::string failure_text = diagnostic.empty()
                                         ? "Extension environment provisioning failed and repair is required."
                                         : std::string(diagnostic);

    if (!removeIfExists(ready_marker, error) || !removeIfExists(receipt_path, error) ||
        !writeFileText(failure_diagnostic, failure_text + "\n", error) ||
        !releaseEnvironmentLock(provisioning_lock, error))
    {
        return false;
    }

    return true;
}

bool ExtensionEnvironmentManager::removeEnvironmentDirectory(const std::filesystem::path& environment_path,
                                                             std::string* error) const
{
    if (environment_path.empty())
    {
        return true;
    }

    return removePathTreeWithDiagnostics(environment_path, "Failed to remove extension environment directory", error);
}

[[nodiscard]] std::vector<ExtensionInstalledDistribution> baseRuntimeDistributions()
{
    return {{"afternight", "1.0.0", "bundled-runtime", "host-managed"},
            {"PySide6", std::string(kBundledPySide6Version), "bundled-runtime", "host-managed"},
            {"PySide6_Essentials", std::string(kBundledPySide6Version), "bundled-runtime", "host-managed"},
            {"PySide6_Addons", std::string(kBundledPySide6Version), "bundled-runtime", "host-managed"},
            {"shiboken6", std::string(kBundledPySide6Version), "bundled-runtime", "host-managed"}};
}

[[nodiscard]] std::optional<ExtensionInstalledDistribution>
distributionFromDistInfoDirectory(const std::filesystem::path& path)
{
    const std::string file_name = path.filename().string();
    constexpr std::string_view kSuffix = ".dist-info";
    if (file_name.size() <= kSuffix.size() ||
        file_name.compare(file_name.size() - kSuffix.size(), kSuffix.size(), kSuffix) != 0)
    {
        return std::nullopt;
    }

    const std::string stem = file_name.substr(0, file_name.size() - kSuffix.size());
    const std::size_t version_separator = stem.rfind('-');
    if (version_separator == std::string::npos || version_separator == 0 || version_separator + 1 >= stem.size())
    {
        return std::nullopt;
    }

    ExtensionInstalledDistribution distribution;
    distribution.name = normalizeDistributionName(stem.substr(0, version_separator));
    distribution.version = stem.substr(version_separator + 1);
    distribution.source = "bundled-runtime";
    distribution.artifact_hash = hashPathTree(path);
    return distribution;
}

[[nodiscard]] int runtimePackagePrefixCopyFileCount(const std::filesystem::path& runtime_root,
                                                    const std::string_view package_prefix)
{
    const std::filesystem::path source_site_packages = runtimeSitePackagesPath(runtime_root);
    int file_count = 0;
    for (const auto& entry : sortedDirectoryEntries(source_site_packages))
    {
        const std::string name = entry.path().filename().string();
        if (name.rfind(std::string(package_prefix), 0) != 0)
        {
            continue;
        }

        file_count += copyProgressFileCount(entry.path());
    }

    return file_count;
}

[[nodiscard]] int runtimeStagingCopyFileCount(const ExtensionManifest& manifest,
                                              const std::filesystem::path& runtime_root)
{
    int file_count = 0;
    for (const std::string_view package_prefix : kBundledUiRuntimePackagePrefixes)
    {
        file_count += runtimePackagePrefixCopyFileCount(runtime_root, package_prefix);
    }
    for (const std::string& package_prefix : manifest.dependencies.bundled_runtime_packages)
    {
        file_count += runtimePackagePrefixCopyFileCount(runtime_root, package_prefix);
    }
    if (manifest.dependencies.dependency_context == ExtensionDependencyContext::SharedHost)
    {
        if (const auto* package_prefixes = sharedHostProfilePackagePrefixes(manifest.dependencies.shared_host_profile))
        {
            for (const std::string_view package_prefix : *package_prefixes)
            {
                file_count += runtimePackagePrefixCopyFileCount(runtime_root, package_prefix);
            }
        }
    }

    return file_count;
}

bool stageBundledRuntimePackagePrefix(const std::filesystem::path& runtime_root,
                                      const std::filesystem::path& environment_root,
                                      const std::string_view package_prefix,
                                      std::vector<ExtensionInstalledDistribution>* out_distributions,
                                      std::string* error, const FileCopyProgressCallback& progress_callback = {})
{
    const std::filesystem::path source_site_packages = runtimeSitePackagesPath(runtime_root);
    const std::filesystem::path destination_site_packages = environmentSitePackagesPath(environment_root);
    std::error_code ec;
    if (!std::filesystem::exists(source_site_packages, ec))
    {
        if (error)
        {
            *error = "Bundled runtime site-packages directory is missing: " + source_site_packages.generic_string();
        }
        return false;
    }

    std::filesystem::create_directories(destination_site_packages, ec);
    if (ec)
    {
        if (error)
        {
            *error = "Failed to create extension environment site-packages directory: " +
                     destination_site_packages.generic_string();
        }
        return false;
    }

    bool copied_any = false;
    std::optional<ExtensionInstalledDistribution> distribution;
    for (const auto& entry : sortedDirectoryEntries(source_site_packages))
    {
        const std::string name = entry.path().filename().string();
        if (name.rfind(std::string(package_prefix), 0) != 0)
        {
            continue;
        }

        copied_any = true;
        const auto destination = destination_site_packages / entry.path().filename();
        std::filesystem::remove_all(destination, ec);
        ec.clear();
        if (!copyPathTree(entry.path(), destination, error, progress_callback))
        {
            return false;
        }

        if (!distribution.has_value())
        {
            distribution = distributionFromDistInfoDirectory(entry.path());
        }
    }

    if (!copied_any)
    {
        if (error)
        {
            *error = "Bundled runtime package prefix '" + std::string(package_prefix) + "' is missing from " +
                     source_site_packages.generic_string();
        }
        return false;
    }

    if (out_distributions)
    {
        if (!distribution.has_value())
        {
            ExtensionInstalledDistribution fallback;
            fallback.name = normalizeDistributionName(package_prefix);
            fallback.source = "bundled-runtime";
            fallback.artifact_hash = "host-managed";
            distribution = std::move(fallback);
        }
        out_distributions->push_back(*distribution);
    }

    return true;
}

bool stageBundledRuntimeDependencyPackages(const ExtensionManifest& manifest, const std::filesystem::path& runtime_root,
                                           const std::filesystem::path& environment_root,
                                           std::vector<ExtensionInstalledDistribution>* out_distributions,
                                           std::string* error, const FileCopyProgressCallback& progress_callback = {})
{
    for (const auto& package_prefix : manifest.dependencies.bundled_runtime_packages)
    {
        if (!stageBundledRuntimePackagePrefix(runtime_root, environment_root, package_prefix, out_distributions, error,
                                              progress_callback))
        {
            return false;
        }
    }

    return true;
}

bool stageSharedHostProfilePackages(const ExtensionDependencySpec& dependencies,
                                    const std::filesystem::path& runtime_root,
                                    const std::filesystem::path& environment_root,
                                    std::vector<ExtensionInstalledDistribution>* out_distributions, std::string* error,
                                    const FileCopyProgressCallback& progress_callback = {})
{
    const auto* package_prefixes = sharedHostProfilePackagePrefixes(dependencies.shared_host_profile);
    if (!package_prefixes)
    {
        if (error)
        {
            *error = "Unknown host-managed shared dependency profile: " + dependencies.shared_host_profile;
        }
        return false;
    }

    for (const auto package_prefix : *package_prefixes)
    {
        if (!stageBundledRuntimePackagePrefix(runtime_root, environment_root, package_prefix, out_distributions, error,
                                              progress_callback))
        {
            return false;
        }
    }

    return true;
}

bool stageBundledRuntimeUiPackages(const std::filesystem::path& runtime_root,
                                   const std::filesystem::path& environment_root, std::string* error,
                                   const FileCopyProgressCallback& progress_callback = {})
{
    const std::filesystem::path source_site_packages = runtimeSitePackagesPath(runtime_root);
    const std::filesystem::path destination_site_packages = environmentSitePackagesPath(environment_root);
    std::error_code ec;
    if (!std::filesystem::exists(source_site_packages, ec))
    {
        if (error)
        {
            *error = "Bundled runtime site-packages directory is missing: " + source_site_packages.generic_string();
        }
        return false;
    }

    std::filesystem::create_directories(destination_site_packages, ec);
    if (ec)
    {
        if (error)
        {
            *error = "Failed to create extension environment site-packages directory: " +
                     destination_site_packages.generic_string();
        }
        return false;
    }

    std::array<bool, kBundledUiRuntimePackagePrefixes.size()> copied_prefixes{};
    for (const auto& entry : sortedDirectoryEntries(source_site_packages))
    {
        const std::string name = entry.path().filename().string();
        for (std::size_t index = 0; index < kBundledUiRuntimePackagePrefixes.size(); ++index)
        {
            const std::string_view prefix = kBundledUiRuntimePackagePrefixes[index];
            if (name.rfind(std::string(prefix), 0) != 0)
            {
                continue;
            }

            const auto destination = destination_site_packages / entry.path().filename();
            std::filesystem::remove_all(destination, ec);
            ec.clear();
            if (!copyPathTree(entry.path(), destination, error, progress_callback))
            {
                return false;
            }

            copied_prefixes[index] = true;
            break;
        }
    }

    for (std::size_t index = 0; index < kBundledUiRuntimePackagePrefixes.size(); ++index)
    {
        if (!copied_prefixes[index])
        {
            if (error)
            {
                *error = "Bundled runtime UI package prefix '" + std::string(kBundledUiRuntimePackagePrefixes[index]) +
                         "' is missing from " + source_site_packages.generic_string();
            }
            return false;
        }
    }

    return true;
}

bool provisionEnvironmentFromManifest(const ExtensionManifest& manifest, const ExtensionEnvironmentSnapshot& snapshot,
                                      std::vector<ExtensionInstalledDistribution>* out_distributions,
                                      std::string* error, const ExtensionOperationProgressCallback& progress_callback,
                                      const ExtensionOperationCancellationToken& cancellation_token)
{
    const auto cancel_if_requested = [&]() -> bool {
        if (!cancellationRequested(cancellation_token))
        {
            return false;
        }

        setOperationCancelledError(error);
        return true;
    };

    if (cancel_if_requested())
    {
        return false;
    }
    if (snapshot.bundled_python_executable.empty() || !isRegularFile(snapshot.bundled_python_executable))
    {
        if (error)
        {
            *error = "Bundled Python executable is not available in this build, so extension environments "
                     "cannot be provisioned yet.";
        }
        return false;
    }

    const std::filesystem::path bundled_runtime_root =
        !snapshot.runtime_root.empty()
            ? snapshot.runtime_root
            : runtimeRootForBundledPython(snapshot.bundled_python_executable, snapshot.runtime_root);
    const std::filesystem::path bundled_python_home =
#ifdef _WIN32
        windowsProvisioningPythonHome(snapshot.bundled_python_executable, bundled_runtime_root);
#else
        posixProvisioningPythonHome(snapshot.bundled_python_executable, bundled_runtime_root);
#endif
    const QProcessEnvironment bundled_python_environment = isolatedPythonProcessEnvironment(bundled_python_home);

    std::string process_output;
#ifdef _WIN32
    reportProvisionProgress(progress_callback, "create_environment", "Creating extension Python environment",
                            kProvisionProgressCreateEnvironmentStart);
    if (cancel_if_requested())
    {
        return false;
    }
    if (!bootstrapManagedWindowsEnvironment(snapshot, bundled_python_home, error))
    {
        return false;
    }
#else
    reportProvisionProgress(progress_callback, "create_environment", "Creating extension Python environment",
                            kProvisionProgressCreateEnvironmentStart);
    if (cancel_if_requested())
    {
        return false;
    }
    if (!runProcess(
            snapshot.bundled_python_executable,
            {QStringLiteral("-m"), QStringLiteral("venv"), QString::fromStdString(snapshot.environment_path.string())},
            {}, kOfflineProvisioningTimeoutMs, &bundled_python_environment, &process_output, error,
            &cancellation_token))
    {
        if (error)
        {
            std::ostringstream message;
            message << "Failed to create extension environment with bundled Python '"
                    << snapshot.bundled_python_executable.generic_string() << "'";
            if (!bundled_runtime_root.empty())
            {
                message << " using runtime root '" << bundled_runtime_root.generic_string() << "'";
            }

            const std::string detail = !process_output.empty() ? process_output : *error;
            if (!detail.empty())
            {
                message << ": " << detail;
            }
            *error = message.str();
        }
        return false;
    }
#endif
    reportProvisionProgress(progress_callback, "create_environment", "Created extension Python environment",
                            kProvisionProgressCreateEnvironmentEnd);

    std::vector<ExtensionInstalledDistribution> resolved_distributions = baseRuntimeDistributions();
    const int runtime_stage_total_files = (std::max)(runtimeStagingCopyFileCount(manifest, bundled_runtime_root), 1);
    int runtime_stage_copied_files = 0;
    const FileCopyProgressCallback runtime_stage_progress =
        progress_callback ? FileCopyProgressCallback([&](const std::filesystem::path& file, const int, const int) {
            ++runtime_stage_copied_files;
            reportProvisionSubrangeProgress(progress_callback, "stage_runtime",
                                            "Staging bundled extension runtime packages " +
                                                std::to_string(runtime_stage_copied_files) + "/" +
                                                std::to_string(runtime_stage_total_files) + ": " +
                                                compactProgressPath(file.filename().generic_string()),
                                            kProvisionProgressStageRuntimeStart, kProvisionProgressStageRuntimeEnd,
                                            runtime_stage_copied_files, runtime_stage_total_files);
        })
                          : FileCopyProgressCallback{};
    reportProvisionProgress(progress_callback, "stage_runtime", "Staging bundled extension runtime packages",
                            kProvisionProgressStageRuntimeStart);
    if (cancel_if_requested())
    {
        return false;
    }
    if (!stageBundledRuntimeUiPackages(bundled_runtime_root, snapshot.environment_path, error, runtime_stage_progress))
    {
        return false;
    }
    if (!stageBundledRuntimeDependencyPackages(manifest, bundled_runtime_root, snapshot.environment_path,
                                               &resolved_distributions, error, runtime_stage_progress))
    {
        return false;
    }
    if (manifest.dependencies.dependency_context == ExtensionDependencyContext::SharedHost &&
        !stageSharedHostProfilePackages(manifest.dependencies, bundled_runtime_root, snapshot.environment_path,
                                        &resolved_distributions, error, runtime_stage_progress))
    {
        return false;
    }
    reportProvisionProgress(progress_callback, "stage_runtime", "Staged bundled extension runtime packages",
                            kProvisionProgressStageRuntimeEnd);

    const auto finalize_provisioning = [&]() -> bool {
        reportProvisionProgress(progress_callback, "precompile_dependencies", "Precompiling dependency bytecode",
                                kProvisionProgressPrecompileDependenciesStart);
        if (cancel_if_requested())
        {
            return false;
        }
        const ExtensionOperationProgressCallback dependency_precompile_progress =
            progress_callback ? ExtensionOperationProgressCallback([&](const ExtensionOperationProgress& progress) {
                reportProvisionSubrangeProgress(progress_callback, progress.phase_id, progress.message,
                                                kProvisionProgressPrecompileDependenciesStart,
                                                kProvisionProgressPrecompileDependenciesEnd, progress.current,
                                                progress.total);
            })
                              : ExtensionOperationProgressCallback{};
        if (!precompileEnvironmentSitePackagesBytecode(snapshot, bundled_python_environment, error, cancellation_token,
                                                       dependency_precompile_progress))
        {
            return false;
        }
        reportProvisionProgress(progress_callback, "precompile_dependencies", "Precompiled dependency bytecode",
                                kProvisionProgressPrecompileDependenciesEnd);
        if (out_distributions)
        {
            *out_distributions = std::move(resolved_distributions);
        }
        return true;
    };

    if (!manifest.dependencies.declared())
    {
        return finalize_provisioning();
    }

    if (manifest.dependencies.requirements_file.empty())
    {
        return finalize_provisioning();
    }

    const std::filesystem::path requirements_path =
        std::filesystem::path(manifest.package_root) / manifest.dependencies.requirements_file;
    std::vector<LockedRequirement> locked_requirements;
    if (!parseLockedRequirements(requirements_path, &locked_requirements, error))
    {
        return false;
    }

    const bool offline_only =
        manifest.dependencies.pip.index_urls.empty() && manifest.dependencies.pip.extra_index_urls.empty();
    std::vector<WheelArtifactMatch> local_wheels;
    std::vector<WheelArtifactMatch> extra_local_wheels;
    if (offline_only &&
        !resolveLocalWheelArtifacts(manifest, snapshot, locked_requirements, &local_wheels, &extra_local_wheels, error))
    {
        return false;
    }

    const std::filesystem::path env_python = environmentPythonExecutable(snapshot.environment_path);
    if (!isRegularFile(env_python))
    {
        if (error)
        {
            *error = "Provisioned environment is missing its Python executable";
        }
        return false;
    }

    const std::filesystem::path pip_driver =
#ifdef _WIN32
        snapshot.bundled_python_executable;
#else
        env_python;
#endif
    const QProcessEnvironment pip_environment =
#ifdef _WIN32
        bundled_python_environment;
#else
        isolatedPythonProcessEnvironment();
#endif
    const std::filesystem::path report_path = snapshot.environment_path / std::string(kPipInstallReportName);
    std::vector<ExtensionInstalledDistribution> extra_wheel_distributions;
    reportProvisionProgress(progress_callback, "resolve_dependencies", "Resolving locked Python dependencies",
                            kProvisionProgressResolveDependenciesStart);
    if (cancel_if_requested())
    {
        return false;
    }
    if (!installWheelArtifacts(pip_driver, pip_environment, std::filesystem::path(manifest.package_root),
                               snapshot.environment_path, extra_local_wheels, offline_only, &extra_wheel_distributions,
                               error, cancellation_token))
    {
        return false;
    }
    reportProvisionProgress(progress_callback, "resolve_dependencies", "Resolved locked Python dependencies",
                            kProvisionProgressResolveDependenciesEnd);

    QStringList pip_arguments = {QStringLiteral("-m"),         QStringLiteral("pip"),
                                 QStringLiteral("install"),    QStringLiteral("--disable-pip-version-check"),
                                 QStringLiteral("--no-input"), QStringLiteral("--no-compile"),
                                 QStringLiteral("--report"),   QString::fromStdString(report_path.string())};
#ifdef _WIN32
    pip_arguments.push_back(QStringLiteral("--target"));
    pip_arguments.push_back(QString::fromStdString(environmentSitePackagesPath(snapshot.environment_path).string()));
#endif
    if (manifest.dependencies.pip.require_hashes)
    {
        pip_arguments.push_back(QStringLiteral("--require-hashes"));
    }
    if (manifest.dependencies.pip.no_deps)
    {
        pip_arguments.push_back(QStringLiteral("--no-deps"));
    }
    if (offline_only)
    {
        pip_arguments.push_back(QStringLiteral("--no-index"));
    }

    bool primary_index_consumed = false;
    for (const auto& item : manifest.dependencies.pip.index_urls)
    {
        pip_arguments.push_back(primary_index_consumed ? QStringLiteral("--extra-index-url")
                                                       : QStringLiteral("--index-url"));
        pip_arguments.push_back(QString::fromStdString(item));
        primary_index_consumed = true;
    }
    for (const auto& item : manifest.dependencies.pip.extra_index_urls)
    {
        pip_arguments.push_back(QStringLiteral("--extra-index-url"));
        pip_arguments.push_back(QString::fromStdString(item));
    }
    for (const auto& item : manifest.dependencies.pip.find_links)
    {
        pip_arguments.push_back(QStringLiteral("--find-links"));
        if (isUrlLike(item))
        {
            pip_arguments.push_back(QString::fromStdString(item));
            continue;
        }

        const auto absolute_path = std::filesystem::path(manifest.package_root) / item;
        pip_arguments.push_back(QString::fromStdString(absolute_path.string()));
    }

    pip_arguments.push_back(QStringLiteral("-r"));
    pip_arguments.push_back(QString::fromStdString(requirements_path.string()));

    const int timeout_ms = offline_only ? kOfflineProvisioningTimeoutMs : kOnlineProvisioningTimeoutMs;
    int last_install_dependency_progress = kProvisionProgressInstallDependenciesStart;
    qint64 last_install_dependency_report_ms = -1000;
    const auto report_install_dependency_progress = [&](const qint64 elapsed_ms, const bool force) {
        const int reserved_end =
            kProvisionProgressInstallDependenciesStart +
            (kProvisionProgressInstallDependenciesEnd - kProvisionProgressInstallDependenciesStart) * 95 / 100;
        const int estimated_current =
            kProvisionProgressInstallDependenciesStart +
            static_cast<int>((std::min)(static_cast<long long>(elapsed_ms), static_cast<long long>(timeout_ms)) *
                             (reserved_end - kProvisionProgressInstallDependenciesStart) / (std::max)(timeout_ms, 1));
        const int current = std::clamp(estimated_current, kProvisionProgressInstallDependenciesStart, reserved_end);
        if (!force &&
            (current <= last_install_dependency_progress || elapsed_ms - last_install_dependency_report_ms < 500))
        {
            return;
        }

        last_install_dependency_progress = current;
        last_install_dependency_report_ms = elapsed_ms;
        reportProvisionProgress(progress_callback, "install_dependencies", "Installing locked Python dependencies",
                                current);
    };
    reportProvisionProgress(progress_callback, "install_dependencies", "Installing locked Python dependencies",
                            kProvisionProgressInstallDependenciesStart);
    if (cancel_if_requested())
    {
        return false;
    }
    if (!runProcess(pip_driver, pip_arguments, std::filesystem::path(manifest.package_root), timeout_ms,
                    &pip_environment, &process_output, error, &cancellation_token, {},
                    [&](const qint64 elapsed_ms) { report_install_dependency_progress(elapsed_ms, false); }))
    {
        removeIfExists(report_path, nullptr);
        if (error && !process_output.empty() && *error != process_output)
        {
            *error = "Dependency provisioning failed: " + process_output;
        }
        return false;
    }
    reportProvisionProgress(progress_callback, "install_dependencies", "Installed locked Python dependencies",
                            kProvisionProgressInstallDependenciesEnd);

    auto pip_distributions = pipReportToDistributions(report_path);
    removeIfExists(report_path, nullptr);
    if (pip_distributions.empty())
    {
        pip_distributions = wheelMatchesToDistributions(local_wheels);
    }

    resolved_distributions.insert(resolved_distributions.end(),
                                  std::make_move_iterator(extra_wheel_distributions.begin()),
                                  std::make_move_iterator(extra_wheel_distributions.end()));
    resolved_distributions.insert(resolved_distributions.end(), std::make_move_iterator(pip_distributions.begin()),
                                  std::make_move_iterator(pip_distributions.end()));

    if (!validateResolvedWheelDistributions(resolved_distributions, manifest, snapshot, error))
    {
        return false;
    }

    return finalize_provisioning();
}

namespace
{

/// Where the runtime packages an environment copies from the application come from: the snapshot's runtime root, or
/// the one its bundled Python is in.
[[nodiscard]] std::filesystem::path stagingRuntimeRoot(const ExtensionEnvironmentSnapshot& snapshot)
{
    return !snapshot.runtime_root.empty()
               ? snapshot.runtime_root
               : runtimeRootForBundledPython(snapshot.bundled_python_executable, snapshot.runtime_root);
}

/// The Python home this run's interpreter is started with for a step that prepares an environment.
[[nodiscard]] std::filesystem::path provisioningPythonHome(const ExtensionEnvironmentSnapshot& snapshot)
{
#ifdef _WIN32
    return windowsProvisioningPythonHome(snapshot.bundled_python_executable, stagingRuntimeRoot(snapshot));
#else
    return posixProvisioningPythonHome(snapshot.bundled_python_executable, stagingRuntimeRoot(snapshot));
#endif
}

/// What the runtime packages an environment copies from the application hold as the application has them now: the
/// interface packages every environment takes, those its manifest names and those of its shared-host profile.
[[nodiscard]] std::string stagedRuntimeFingerprint(const ExtensionManifest& manifest,
                                                   const ExtensionEnvironmentSnapshot& snapshot,
                                                   const ExtensionOperationCancellationToken& cancellationToken)
{
    const std::filesystem::path runtimeRoot = stagingRuntimeRoot(snapshot);
    const std::filesystem::path cacheDirectory = snapshot.environments_root / std::string(kPathTreeHashCacheDirectory);
    std::ostringstream stream;
    const auto append = [&](const std::string_view prefix) {
        appendBundledRuntimePackagePrefixFingerprint(stream, runtimeRoot, prefix, &cancellationToken,
                                                     ExtensionEnvironmentInspectionMode::Routine, cacheDirectory);
    };
    for (const std::string_view prefix : kBundledUiRuntimePackagePrefixes)
    {
        append(prefix);
    }
    for (const std::string& prefix : manifest.dependencies.bundled_runtime_packages)
    {
        append(prefix);
    }
    if (manifest.dependencies.dependency_context == ExtensionDependencyContext::SharedHost)
    {
        if (const auto* prefixes = sharedHostProfilePackagePrefixes(manifest.dependencies.shared_host_profile))
        {
            for (const std::string_view prefix : *prefixes)
            {
                append(prefix);
            }
        }
    }
    return sha256Hex(stream.str());
}

/// Lets one operation of this process at a time prepare an environment - refresh it, take one over for it or provision
/// it - so another one for the same environment waits and finds what the first left rather than racing it. Another
/// process is kept off by the environment's lock.
class EnvironmentPreparationGuard
{
public:
    /// Waits, calling `onWait` once if it has to, until no other operation of this process prepares `environmentPath`,
    /// or until `cancellationToken` is cancelled, which leaves the guard not acquired.
    EnvironmentPreparationGuard(const std::filesystem::path& environmentPath,
                                const ExtensionOperationCancellationToken& cancellationToken,
                                const std::function<void()>& onWait = {})
        : m_path(environmentPath.lexically_normal())
    {
        std::unique_lock lock(g_environmentPreparations.mutex);
        if (g_environmentPreparations.busyPaths.contains(m_path))
        {
            m_waited = true;
            if (onWait)
            {
                lock.unlock();
                onWait();
                lock.lock();
            }
        }
        while (g_environmentPreparations.busyPaths.contains(m_path))
        {
            if (cancellationRequested(cancellationToken))
            {
                return;
            }
            g_environmentPreparations.released.wait_for(lock, std::chrono::milliseconds(100));
        }
        g_environmentPreparations.busyPaths.insert(m_path);
        m_acquired = true;
    }

    /// Takes the guard only if no other operation of this process prepares `environmentPath` now.
    EnvironmentPreparationGuard(const std::filesystem::path& environmentPath, std::try_to_lock_t)
        : m_path(environmentPath.lexically_normal())
    {
        const std::lock_guard lock(g_environmentPreparations.mutex);
        m_acquired = g_environmentPreparations.busyPaths.insert(m_path).second;
        m_waited = !m_acquired;
    }

    ~EnvironmentPreparationGuard()
    {
        if (!m_acquired)
        {
            return;
        }
        {
            const std::lock_guard lock(g_environmentPreparations.mutex);
            g_environmentPreparations.busyPaths.erase(m_path);
        }
        g_environmentPreparations.released.notify_all();
    }

    EnvironmentPreparationGuard(const EnvironmentPreparationGuard&) = delete;
    EnvironmentPreparationGuard& operator=(const EnvironmentPreparationGuard&) = delete;

    [[nodiscard]] bool acquired() const
    {
        return m_acquired;
    }

    /// Whether another operation prepared the environment while this one waited, so what it found before may be gone.
    [[nodiscard]] bool waited() const
    {
        return m_waited;
    }

private:
    std::filesystem::path m_path;
    bool m_acquired = false;
    bool m_waited = false;
};

[[nodiscard]] QJsonObject distributionToJson(const ExtensionInstalledDistribution& distribution)
{
    QJsonObject item;
    item.insert(QStringLiteral("name"), QString::fromStdString(distribution.name));
    item.insert(QStringLiteral("version"), QString::fromStdString(distribution.version));
    item.insert(QStringLiteral("source"), QString::fromStdString(distribution.source));
    item.insert(QStringLiteral("artifact_hash"), QString::fromStdString(distribution.artifact_hash));
    return item;
}

/// Takes an environment's lock for an operation on it: the file is made only where none is, so two operations
/// never both hold it. One whose owner has stopped no longer holds the environment and is taken over; one being
/// written right now, still empty, does.
bool acquireEnvironmentLock(const std::filesystem::path& lock, const std::string& contents, std::string* error)
{
    for (int attempt = 0; attempt < 2; ++attempt)
    {
        QFile file(QString::fromStdWString(lock.wstring()));
        if (file.open(QIODevice::WriteOnly | QIODevice::NewOnly))
        {
            const QByteArray bytes = QByteArray::fromStdString(contents);
            const bool written = file.write(bytes) == bytes.size() && file.flush();
            file.close();
            if (!written)
            {
                file.remove();
                if (error)
                {
                    *error = "Failed to write the extension environment's lock: " + pathForLog(lock);
                }
                return false;
            }
            noteLockHeld(lock);
            return true;
        }

        if (environmentLockHeld(lock))
        {
            if (error)
            {
                *error = "Extension environment provisioning is already in progress.";
            }
            return false;
        }
        std::error_code ec;
        std::filesystem::remove(lock, ec);
    }

    if (error)
    {
        *error = "Failed to lock the extension environment: " + pathForLog(lock);
    }
    return false;
}

/// Writes a receipt beside the one it replaces and renames it over it, so a reader sees the one or the other whole.
bool replaceReceipt(const std::filesystem::path& receiptPath, const QJsonObject& receipt, std::string* error)
{
    const std::filesystem::path replacement =
        receiptPath.parent_path() /
        ("." + receiptPath.filename().string() + "." + QUuid::createUuid().toString(QUuid::Id128).toStdString());
    if (!writeFileText(replacement, QJsonDocument(receipt).toJson(QJsonDocument::Indented).toStdString(), error))
    {
        std::error_code ignored;
        std::filesystem::remove(replacement, ignored);
        return false;
    }

    std::error_code ec;
    std::filesystem::rename(replacement, receiptPath, ec);
    if (ec)
    {
        std::error_code ignored;
        std::filesystem::remove(replacement, ignored);
        setFilesystemError(error, "Failed to replace the extension environment's receipt", receiptPath, ec);
        return false;
    }
    return true;
}

/// Whether what a receipt says an environment installed is what a lock pins: every requirement it pins at its pinned
/// version - and at one of its hashes, where both say one - and nothing the lock does not name. What the environment
/// copies from the application is not installed, and is copied again when it is taken over.
[[nodiscard]] bool receiptInstallsWhatTheLockPins(const QJsonObject& receipt,
                                                  const std::vector<LockedRequirement>& locked)
{
    std::map<std::string, QJsonObject, std::less<>> installed;
    for (const QJsonValue& value : receipt.value(QStringLiteral("resolved_distributions")).toArray())
    {
        const QJsonObject item = value.toObject();
        if (item.value(QStringLiteral("source")).toString() == QStringLiteral("bundled-runtime"))
        {
            continue;
        }
        installed.emplace(normalizeDistributionName(item.value(QStringLiteral("name")).toString().toStdString()), item);
    }
    if (installed.size() != locked.size())
    {
        return false;
    }

    for (const LockedRequirement& requirement : locked)
    {
        const auto it = installed.find(requirement.name);
        if (it == installed.end() ||
            it->second.value(QStringLiteral("version")).toString().toStdString() != requirement.version)
        {
            return false;
        }
        const std::string hash =
            toLowerCopy(it->second.value(QStringLiteral("artifact_hash")).toString().toStdString());
        if (!hash.empty() && !requirement.sha256.empty() &&
            std::find(requirement.sha256.begin(), requirement.sha256.end(), hash) == requirement.sha256.end())
        {
            return false;
        }
    }
    return true;
}

/// The directory every environment of `manifest`'s context lives under, whatever its layout.
[[nodiscard]] std::filesystem::path environmentContextRoot(const ExtensionManifest& manifest,
                                                           const std::filesystem::path& environmentsRoot)
{
    switch (manifest.dependencies.dependency_context)
    {
    case ExtensionDependencyContext::SharedGroup:
        return environmentsRoot / "_shared" / manifest.dependencies.shared_group;
    case ExtensionDependencyContext::SharedHost:
        return environmentsRoot / "_host_shared" / manifest.dependencies.shared_host_profile;
    case ExtensionDependencyContext::Private:
        break;
    }
    return environmentsRoot / manifest.id;
}

/// Removes the directories an environment's removal or move left empty above it, up to the environments root.
void removeEmptyEnvironmentParents(const std::filesystem::path& environmentPath,
                                   const std::filesystem::path& environmentsRoot)
{
    const std::filesystem::path root = environmentsRoot.lexically_normal();
    std::filesystem::path parent = environmentPath.lexically_normal().parent_path();
    while (!parent.empty() && parent != root && parent.parent_path() != parent)
    {
        const std::filesystem::path inside = parent.lexically_relative(root);
        if (inside.empty() || *inside.begin() == "..")
        {
            return;
        }
        std::error_code ec;
        if (!std::filesystem::is_directory(parent, ec) || !std::filesystem::is_empty(parent, ec) ||
            !std::filesystem::remove(parent, ec))
        {
            return;
        }
        parent = parent.parent_path();
    }
}

/// What an environment's lock says while it is refreshed or taken over: that it is, for what, and by which process.
[[nodiscard]] std::string environmentRefreshLockContents(const ExtensionEnvironmentSnapshot& snapshot)
{
    std::ostringstream contents;
    contents << "refresh=1\n"
             << "dependency_fingerprint=" << snapshot.dependency_fingerprint << '\n'
             << lockOwnerLines();
    return contents.str();
}

/// Whether an environment is already what `snapshot` asks of it - ready, and recorded for the same build, dependencies
/// and package revision - as another operation that prepared it since `snapshot` was taken leaves it.
[[nodiscard]] bool alreadyPreparedFor(const ExtensionEnvironmentSnapshot& snapshot)
{
    QJsonObject receipt;
    if (!isRegularFile(snapshot.environment_path / std::string(kReadyMarkerName)) ||
        !loadInstallReceipt(snapshot.receipt_path, &receipt))
    {
        return false;
    }
    const auto recorded = [&receipt](const QString& key) { return receipt.value(key).toString().toStdString(); };
    return recorded(QStringLiteral("dependency_fingerprint")) == snapshot.dependency_fingerprint &&
           recorded(QStringLiteral("host_bootstrap_fingerprint")) == snapshot.host_bootstrap_fingerprint &&
           (isSharedEnvironmentPath(snapshot) ||
            recorded(QStringLiteral("validation_fingerprint")) == snapshot.validation_fingerprint);
}

/// Refreshes an environment whose lock the caller took, as `refreshEnvironment` says, and gives the lock up.
bool refreshLockedEnvironment(const ExtensionManifest& manifest, const ExtensionEnvironmentSnapshot& snapshot,
                              std::string* error, const ExtensionOperationProgressCallback& progressCallback,
                              const ExtensionOperationCancellationToken& cancellationToken,
                              bool* receiptUnusable = nullptr)
{
    const std::filesystem::path lock = snapshot.environment_path / std::string(kProvisioningLockName);
    constexpr int kRefreshProgressTotal = 4;
    bool restaged = false;
    const bool refreshed = [&]() {
        if (snapshot.bundled_python_executable.empty() || !isRegularFile(snapshot.bundled_python_executable))
        {
            if (error)
            {
                *error = "Bundled Python executable is not available in this build, so the extension environment "
                         "cannot be refreshed.";
            }
            return false;
        }
        QJsonObject receipt;
        if (!loadInstallReceipt(snapshot.receipt_path, &receipt))
        {
            if (error)
            {
                *error = "Extension install receipt is missing or invalid, so the environment cannot be refreshed.";
            }
            if (receiptUnusable != nullptr)
            {
                *receiptUnusable = true;
            }
            return false;
        }

        reportOperationProgress(progressCallback, "refresh_interpreter",
                                "Pointing the extension environment at this build's Python", 1, kRefreshProgressTotal,
                                false);
#ifdef _WIN32
        if (!bootstrapManagedWindowsEnvironment(snapshot, provisioningPythonHome(snapshot), error))
        {
            return false;
        }
#else
        if (!repointEnvironmentInterpreter(snapshot.environment_path, snapshot.bundled_python_executable, error))
        {
            return false;
        }
#endif

        const std::string staged = stagedRuntimeFingerprint(manifest, snapshot, cancellationToken);
        if (cancellationRequested(cancellationToken))
        {
            setOperationCancelledError(error);
            return false;
        }

        QJsonArray distributions = receipt.value(QStringLiteral("resolved_distributions")).toArray();
        if (receipt.value(QStringLiteral("staged_runtime_fingerprint")).toString().toStdString() != staged)
        {
            // The application's copies changed, or the receipt predates recording them: what the environment took from
            // the application is copied again and compiled, and the installed distributions are left as they are.
            reportOperationProgress(progressCallback, "refresh_runtime_packages",
                                    "Copying the runtime packages the extension takes from AfterNight", 2,
                                    kRefreshProgressTotal, false);
            const std::filesystem::path runtimeRoot = stagingRuntimeRoot(snapshot);
            std::vector<ExtensionInstalledDistribution> stagedDistributions = baseRuntimeDistributions();
            if (!stageBundledRuntimeUiPackages(runtimeRoot, snapshot.environment_path, error) ||
                !stageBundledRuntimeDependencyPackages(manifest, runtimeRoot, snapshot.environment_path,
                                                       &stagedDistributions, error) ||
                (manifest.dependencies.dependency_context == ExtensionDependencyContext::SharedHost &&
                 !stageSharedHostProfilePackages(manifest.dependencies, runtimeRoot, snapshot.environment_path,
                                                 &stagedDistributions, error)))
            {
                return false;
            }

            reportOperationProgress(progressCallback, "refresh_bytecode", "Precompiling dependency bytecode", 3,
                                    kRefreshProgressTotal, false);
            if (!precompileEnvironmentSitePackagesBytecode(
                    snapshot, isolatedPythonProcessEnvironment(provisioningPythonHome(snapshot)), error,
                    cancellationToken))
            {
                return false;
            }

            QJsonArray refreshedDistributions;
            for (const ExtensionInstalledDistribution& distribution : stagedDistributions)
            {
                refreshedDistributions.push_back(distributionToJson(distribution));
            }
            for (const QJsonValue& value : distributions)
            {
                if (value.toObject().value(QStringLiteral("source")).toString() != QStringLiteral("bundled-runtime"))
                {
                    refreshedDistributions.push_back(value);
                }
            }
            distributions = refreshedDistributions;
            restaged = true;
        }

        reportOperationProgress(progressCallback, "refresh_receipt", "Recording the refreshed extension environment", 4,
                                kRefreshProgressTotal, false);
        receipt.insert(QStringLiteral("host_bootstrap_fingerprint"),
                       QString::fromStdString(snapshot.host_bootstrap_fingerprint));
        receipt.insert(QStringLiteral("dependency_fingerprint"),
                       QString::fromStdString(snapshot.dependency_fingerprint));
        if (!isSharedEnvironmentPath(snapshot))
        {
            receipt.insert(QStringLiteral("validation_fingerprint"),
                           QString::fromStdString(snapshot.validation_fingerprint));
        }
        receipt.insert(QStringLiteral("staged_runtime_fingerprint"), QString::fromStdString(staged));
        receipt.insert(QStringLiteral("resolved_distributions"), distributions);
        if (!replaceReceipt(snapshot.receipt_path, receipt, error))
        {
            return false;
        }
        // A diagnostic an earlier provisioning left no longer describes the environment.
        return removeIfExists(snapshot.environment_path / std::string(kFailureDiagnosticName), error);
    }();

    std::string unlockError;
    if (!releaseEnvironmentLock(lock, &unlockError) && refreshed)
    {
        if (error)
        {
            *error = unlockError;
        }
        return false;
    }
    if (refreshed && an::platform::isLoggingInitialized())
    {
        LOG_INFO("Refreshed extension environment id='{}' env='{}' runtime_packages_copied_again={}", manifest.id,
                 pathForLog(snapshot.environment_path), restaged);
    }
    return refreshed;
}

} // namespace

bool ExtensionEnvironmentManager::refreshEnvironment(const ExtensionManifest& manifest,
                                                     const ExtensionEnvironmentSnapshot& snapshot, std::string* error,
                                                     ExtensionOperationProgressCallback progressCallback,
                                                     ExtensionOperationCancellationToken cancellationToken,
                                                     bool* receiptUnusable) const
{
    if (cancellationRequested(cancellationToken))
    {
        setOperationCancelledError(error);
        return false;
    }
    // The lock keeps cleanup away from the environment and tells another preparation to wait for this one.
    if (!acquireEnvironmentLock(snapshot.environment_path / std::string(kProvisioningLockName),
                                environmentRefreshLockContents(snapshot), error))
    {
        return false;
    }
    return refreshLockedEnvironment(manifest, snapshot, error, progressCallback, cancellationToken, receiptUnusable);
}

std::filesystem::path
ExtensionEnvironmentManager::findAdoptableEnvironment(const ExtensionManifest& manifest,
                                                      const ExtensionEnvironmentSnapshot& snapshot) const
{
    return findAdoptableEnvironment(manifest, snapshot, m_discoveredEnvironments);
}

void ExtensionEnvironmentManager::setDiscoveredEnvironments(std::vector<std::filesystem::path> environments)
{
    for (std::filesystem::path& environment : environments)
    {
        environment = environment.lexically_normal();
    }
    m_discoveredEnvironments = std::move(environments);
}

std::vector<std::filesystem::path>
ExtensionEnvironmentManager::discoveredEnvironmentsOf(const std::vector<ExtensionLoadResult>& discovered)
{
    std::vector<std::filesystem::path> environments;
    for (const ExtensionLoadResult& result : discovered)
    {
        if (result.environment.has_value() && !result.environment->environment_path.empty())
        {
            environments.push_back(result.environment->environment_path.lexically_normal());
        }
    }
    return environments;
}

/// The directories a pass's search for environments to take over has walked, by context root and runtime target.
struct ExtensionEnvironmentManager::AdoptionScan
{
    std::map<std::filesystem::path, std::vector<std::filesystem::path>> candidatesUnder;
};

namespace
{

/// The environment directories of a runtime target under a context's root: every layout keeps an environment within
/// three levels of the root - under a version and a fingerprint, under a fingerprint alone, or under a fingerprint and
/// a label - so the walk stops there.
[[nodiscard]] std::vector<std::filesystem::path> candidateEnvironmentsUnder(const std::filesystem::path& contextRoot,
                                                                            const std::string& runtimeTarget)
{
    std::vector<std::filesystem::path> candidates;
    std::error_code ec;
    for (std::filesystem::recursive_directory_iterator it(contextRoot, ec), end; it != end && !ec; it.increment(ec))
    {
        if (it.depth() >= 3)
        {
            it.disable_recursion_pending();
        }
        std::error_code entryError;
        if (!it->is_directory(entryError) || it->path().filename() != runtimeTarget)
        {
            continue;
        }
        it.disable_recursion_pending();
        candidates.push_back(it->path().lexically_normal());
    }
    return candidates;
}

} // namespace

std::filesystem::path ExtensionEnvironmentManager::findAdoptableEnvironment(
    const ExtensionManifest& manifest, const ExtensionEnvironmentSnapshot& snapshot,
    const std::optional<std::vector<std::filesystem::path>>& discovered, AdoptionScan* scan) const
{
    // Taking an environment over moves it. In a context packages share, one a package still resolves to is left to it,
    // and none is taken over while it is not known which they resolve to.
    const bool shared = manifest.dependencies.dependency_context != ExtensionDependencyContext::Private;
    if (shared && !discovered.has_value())
    {
        return {};
    }
    const auto resolvedByAPackage = [&](const std::filesystem::path& candidate) {
        return shared && std::find(discovered->begin(), discovered->end(), candidate) != discovered->end();
    };

    const std::filesystem::path contextRoot = environmentContextRoot(manifest, snapshot.environments_root);
    std::error_code ec;
    if (!std::filesystem::is_directory(contextRoot, ec))
    {
        return {};
    }

    std::vector<LockedRequirement> locked;
    if (!manifest.dependencies.requirements_file.empty())
    {
        std::string ignored;
        if (!parseLockedRequirements(std::filesystem::path(manifest.package_root) /
                                         manifest.dependencies.requirements_file,
                                     &locked, &ignored))
        {
            return {};
        }
    }

    const std::string abi = pythonAbiOf(snapshot.bundled_python_version);
    const std::filesystem::path current = snapshot.environment_path.lexically_normal();
    std::filesystem::path chosen;
    std::filesystem::file_time_type chosenWritten{};
    // A pass over many environments walks each root once: the scan keeps what it found there.
    std::vector<std::filesystem::path> walked;
    const std::vector<std::filesystem::path>* candidates = &walked;
    if (scan != nullptr)
    {
        const std::filesystem::path key = contextRoot / snapshot.runtime_target;
        auto found = scan->candidatesUnder.find(key);
        if (found == scan->candidatesUnder.end())
        {
            found = scan->candidatesUnder.emplace(key, candidateEnvironmentsUnder(contextRoot, snapshot.runtime_target))
                        .first;
        }
        candidates = &found->second;
    }
    else
    {
        walked = candidateEnvironmentsUnder(contextRoot, snapshot.runtime_target);
    }
    for (const std::filesystem::path& candidate : *candidates)
    {
        std::error_code entryError;
        const std::filesystem::path candidateLock = candidate / std::string(kProvisioningLockName);
        if (candidate == current || resolvedByAPackage(candidate) ||
            !isRegularFile(candidate / std::string(kReadyMarkerName)) ||
            !isRegularFile(candidate / std::string(kInstallReceiptName)) ||
            (isRegularFile(candidateLock) && environmentLockHeld(candidateLock)))
        {
            continue;
        }
        const std::optional<std::string> version = environmentPythonVersion(candidate);
        QJsonObject receipt;
        if (!version || pythonAbiOf(*version) != abi ||
            !loadInstallReceipt(candidate / std::string(kInstallReceiptName), &receipt) ||
            !receiptInstallsWhatTheLockPins(receipt, locked))
        {
            continue;
        }

        const auto written = std::filesystem::last_write_time(candidate / std::string(kInstallReceiptName), entryError);
        if (chosen.empty() || (!entryError && written > chosenWritten))
        {
            chosen = candidate;
            chosenWritten = entryError ? std::filesystem::file_time_type{} : written;
        }
    }
    return chosen;
}

bool ExtensionEnvironmentManager::adoptEnvironment(const ExtensionManifest& manifest,
                                                   const ExtensionEnvironmentSnapshot& snapshot,
                                                   const std::filesystem::path& from, std::string* error,
                                                   ExtensionOperationProgressCallback progressCallback,
                                                   ExtensionOperationCancellationToken cancellationToken) const
{
    if (cancellationRequested(cancellationToken))
    {
        setOperationCancelledError(error);
        return false;
    }

    std::error_code ec;
    if (std::filesystem::exists(snapshot.environment_path, ec))
    {
        // An empty directory an interrupted provisioning made is no environment to keep.
        if (!std::filesystem::is_directory(snapshot.environment_path, ec) ||
            !std::filesystem::is_empty(snapshot.environment_path, ec) ||
            !std::filesystem::remove(snapshot.environment_path, ec))
        {
            if (error)
            {
                *error = "An extension environment is already where the adopted one would go: " +
                         pathForLog(snapshot.environment_path);
            }
            return false;
        }
    }

    // The lock taken in the environment taken over moves with it, so cleanup and another preparation leave it alone
    // until it is refreshed where it goes.
    const std::filesystem::path fromLock = from / std::string(kProvisioningLockName);
    if (!acquireEnvironmentLock(fromLock, environmentRefreshLockContents(snapshot), error))
    {
        return false;
    }
    reportOperationProgress(progressCallback, "adopt_environment",
                            "Taking over the extension environment an earlier version left", 0, 0, true);
    std::filesystem::create_directories(snapshot.environment_path.parent_path(), ec);
    if (!ec)
    {
        std::filesystem::rename(windowsExtendedLengthPath(from), windowsExtendedLengthPath(snapshot.environment_path),
                                ec);
    }
    if (ec)
    {
        std::string ignored;
        static_cast<void>(releaseEnvironmentLock(fromLock, &ignored));
        removeEmptyEnvironmentParents(snapshot.environment_path, snapshot.environments_root);
        setFilesystemError(error, "Failed to take over the extension environment an earlier version left", from, ec);
        return false;
    }
    // The lock moved with the environment: it is held where the environment now is.
    noteLockReleased(fromLock);
    noteLockHeld(snapshot.environment_path / std::string(kProvisioningLockName));
    removeEmptyEnvironmentParents(from, snapshot.environments_root);
    if (an::platform::isLoggingInitialized())
    {
        LOG_INFO("Adopted extension environment id='{}' from='{}' env='{}'", manifest.id, pathForLog(from),
                 pathForLog(snapshot.environment_path));
    }

    return refreshLockedEnvironment(manifest, snapshot, error, progressCallback, cancellationToken);
}

bool ExtensionEnvironmentManager::prepareEnvironmentForLaunch(
    const ExtensionManifest& manifest, const ExtensionPackageSource source, const ExtensionDiscoveryContext& context,
    ExtensionEnvironmentSnapshot* outSnapshot, std::string* error, ExtensionOperationProgressCallback progressCallback,
    ExtensionOperationCancellationToken cancellationToken) const
{
    // Another operation preparing the environment - the startup's maintenance, an install, another launch - is waited
    // for rather than raced: what it leaves is refreshed, adopted or provisioned only if it is still not ready.
    bool reportedWait = false;
    for (;;)
    {
        if (cancellationRequested(cancellationToken))
        {
            setOperationCancelledError(error);
            return false;
        }

        const ExtensionEnvironmentSnapshot current = inspectEnvironment(manifest, source, context, cancellationToken);
        const std::filesystem::path lock = current.environment_path / std::string(kProvisioningLockName);
        if (current.state != ExtensionEnvironmentState::Bootstrapping &&
            !(isRegularFile(lock) && environmentLockHeld(lock)))
        {
            break;
        }
        if (!reportedWait)
        {
            reportOperationProgress(progressCallback, "wait_environment",
                                    "Waiting for the extension environment being prepared");
            reportedWait = true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
    }

    return provisionEnvironment(manifest, source, context, outSnapshot, nullptr, error, std::move(progressCallback),
                                std::move(cancellationToken), RefreshFailure::KeepRefreshable);
}

bool ExtensionEnvironmentManager::provisionEnvironmentForManifest(
    const ExtensionManifest& manifest, const ExtensionPackageSource source, const ExtensionDiscoveryContext& context,
    ExtensionEnvironmentSnapshot* out_snapshot, std::vector<ExtensionInstalledDistribution>* out_resolved_distributions,
    std::string* error, ExtensionOperationProgressCallback progress_callback,
    ExtensionOperationCancellationToken cancellation_token) const
{
    return provisionEnvironment(manifest, source, context, out_snapshot, out_resolved_distributions, error,
                                std::move(progress_callback), std::move(cancellation_token),
                                RefreshFailure::Reprovision);
}

bool ExtensionEnvironmentManager::provisionEnvironment(
    const ExtensionManifest& manifest, const ExtensionPackageSource source, const ExtensionDiscoveryContext& context,
    ExtensionEnvironmentSnapshot* out_snapshot, std::vector<ExtensionInstalledDistribution>* out_resolved_distributions,
    std::string* error, ExtensionOperationProgressCallback progress_callback,
    ExtensionOperationCancellationToken cancellation_token, const RefreshFailure refreshFailure) const
{
    if (cancellationRequested(cancellation_token))
    {
        setOperationCancelledError(error);
        return false;
    }

    ExtensionDiscoveryContext resolved_context = context;
    if (resolved_context.runtime_target.empty())
    {
        resolved_context.runtime_target = an::ui::extensions::current_runtime_target();
    }

    ExtensionEnvironmentSnapshot snapshot = inspectEnvironment(manifest, source, resolved_context, cancellation_token,
                                                               ExtensionEnvironmentInspectionMode::VerifyContents);
    if (cancellationRequested(cancellation_token))
    {
        if (out_snapshot)
        {
            *out_snapshot = snapshot;
        }
        setOperationCancelledError(error);
        return false;
    }
    const auto refresh_output_snapshot = [&]() {
        const ExtensionEnvironmentSnapshot refreshed =
            inspectEnvironment(manifest, source, resolved_context, cancellation_token);
        if (out_snapshot)
        {
            *out_snapshot = refreshed;
        }
        return refreshed;
    };

    // A ready environment is used as it is, and one another operation is preparing is left to it.
    const auto settled = [&](const ExtensionEnvironmentSnapshot& current) -> std::optional<bool> {
        if (current.state == ExtensionEnvironmentState::Ready)
        {
            if (out_snapshot)
            {
                *out_snapshot = current;
            }
            if (out_resolved_distributions)
            {
                out_resolved_distributions->clear();
            }
            return true;
        }
        if (current.state == ExtensionEnvironmentState::Bootstrapping)
        {
            if (out_snapshot)
            {
                *out_snapshot = current;
            }
            if (error)
            {
                *error = current.diagnostic.empty() ? "Extension environment provisioning is already in progress."
                                                    : current.diagnostic;
            }
            return false;
        }
        return std::nullopt;
    };
    if (const auto outcome = settled(snapshot))
    {
        return *outcome;
    }

    // Another operation of this process preparing the environment is waited for, and what it left is looked at again.
    const EnvironmentPreparationGuard guard(snapshot.environment_path, cancellation_token, [&progress_callback]() {
        reportOperationProgress(progress_callback, "wait_environment",
                                "Waiting for the extension environment being prepared");
    });
    if (!guard.acquired())
    {
        refresh_output_snapshot();
        setOperationCancelledError(error);
        return false;
    }
    const auto inspect_again = [&]() {
        snapshot = inspectEnvironment(manifest, source, resolved_context, cancellation_token,
                                      ExtensionEnvironmentInspectionMode::VerifyContents);
        return settled(snapshot);
    };
    if (guard.waited())
    {
        if (const auto outcome = inspect_again())
        {
            return *outcome;
        }
    }

    // What already holds what the package installs is kept: a refreshable environment is brought up to date in place,
    // and one an earlier layout left holding it is taken over. Neither installs anything; only when that fails is the
    // environment provisioned again.
    const auto ready_without_installing = [&]() {
        const ExtensionEnvironmentSnapshot kept = refresh_output_snapshot();
        if (kept.state != ExtensionEnvironmentState::Ready)
        {
            return false;
        }
        if (out_resolved_distributions)
        {
            out_resolved_distributions->clear();
        }
        return true;
    };
    if (snapshot.state == ExtensionEnvironmentState::Stale && snapshot.refreshable)
    {
        // A launch tries a refresh that failed for a reason that may pass - a file or a lock another program holds -
        // once more, and then leaves the environment as refreshable as it was and says why: provisioning it again would
        // wipe one that works and, offline, leave it failed. Only one whose receipt cannot be read is provisioned again
        // there; an install or a repair the person asked for provisions it again whatever the refresh failed of.
        const int attempts = refreshFailure == RefreshFailure::KeepRefreshable ? 2 : 1;
        std::string refresh_error;
        bool receipt_unusable = false;
        for (int attempt = 0; attempt < attempts; ++attempt)
        {
            if (refreshEnvironment(manifest, snapshot, &refresh_error, progress_callback, cancellation_token,
                                   &receipt_unusable) &&
                ready_without_installing())
            {
                return true;
            }
            if (cancellationRequested(cancellation_token))
            {
                refresh_output_snapshot();
                setOperationCancelledError(error);
                return false;
            }
            // Another process may hold the environment, or have refreshed it meanwhile: nothing is cleared before it
            // is looked at again.
            if (const auto outcome = inspect_again())
            {
                return *outcome;
            }
            if (receipt_unusable || snapshot.state != ExtensionEnvironmentState::Stale || !snapshot.refreshable)
            {
                break;
            }
        }
        if (refreshFailure == RefreshFailure::KeepRefreshable && !receipt_unusable &&
            snapshot.state == ExtensionEnvironmentState::Stale && snapshot.refreshable)
        {
            if (an::platform::isLoggingInitialized())
            {
                LOG_WARNING("Extension environment refresh failed and the environment was left as it was for "
                            "extension id='{}' env='{}' reason={}",
                            manifest.id, pathForLog(snapshot.environment_path), refresh_error);
            }
            refresh_output_snapshot();
            if (error)
            {
                *error = "The extension environment could not be brought up to date for this build of AfterNight (" +
                         refresh_error +
                         "). It was left as it was and is refreshed again at the next launch; Repair builds it again.";
            }
            return false;
        }
        if (an::platform::isLoggingInitialized())
        {
            LOG_WARNING("Extension environment refresh failed, so it is provisioned again for extension id='{}' "
                        "env='{}' reason={}",
                        manifest.id, pathForLog(snapshot.environment_path), refresh_error);
        }
    }
    else if (snapshot.state == ExtensionEnvironmentState::Missing)
    {
        if (const std::filesystem::path adoptable = findAdoptableEnvironment(manifest, snapshot); !adoptable.empty())
        {
            std::string adopt_error;
            if (adoptEnvironment(manifest, snapshot, adoptable, &adopt_error, progress_callback, cancellation_token) &&
                ready_without_installing())
            {
                return true;
            }
            if (cancellationRequested(cancellation_token))
            {
                refresh_output_snapshot();
                setOperationCancelledError(error);
                return false;
            }
            if (an::platform::isLoggingInitialized())
            {
                LOG_WARNING("Extension environment adoption failed, so it is provisioned for extension id='{}' "
                            "from='{}' env='{}' reason={}",
                            manifest.id, pathForLog(adoptable), pathForLog(snapshot.environment_path), adopt_error);
            }
            // A move that went through leaves the adopted environment, not a missing one, where it would be made, and
            // another process may have taken the environment over meanwhile.
            if (const auto outcome = inspect_again())
            {
                return *outcome;
            }
        }
    }

    std::string local_error;
    if (snapshot.state == ExtensionEnvironmentState::Stale && !clearReadyEnvironmentMarkers(snapshot, &local_error))
    {
        if (out_snapshot)
        {
            *out_snapshot = snapshot;
        }
        if (error)
        {
            *error = local_error;
        }
        return false;
    }

    if (!beginProvisioning(snapshot, &local_error))
    {
        refresh_output_snapshot();
        if (an::platform::isLoggingInitialized())
        {
            LOG_WARNING(
                "Extension environment provisioning could not start for extension id='{}' version='{}' env='{}' "
                "dependency_fingerprint='{}' validation_fingerprint='{}' reason={}",
                manifest.id, manifest.version, pathForLog(snapshot.environment_path), snapshot.dependency_fingerprint,
                snapshot.validation_fingerprint, local_error);
        }
        if (error)
        {
            *error = local_error;
        }
        return false;
    }

    std::vector<ExtensionInstalledDistribution> resolved_distributions;
    if (!provisionEnvironmentFromManifest(manifest, snapshot, &resolved_distributions, &local_error, progress_callback,
                                          cancellation_token))
    {
        if (an::platform::isLoggingInitialized())
        {
            LOG_WARNING("Extension environment provisioning failed for extension id='{}' version='{}' env='{}' "
                        "dependency_fingerprint='{}' validation_fingerprint='{}' reason={}",
                        manifest.id, manifest.version, pathForLog(snapshot.environment_path),
                        snapshot.dependency_fingerprint, snapshot.validation_fingerprint, local_error);
        }
        std::string persist_error;
        const bool persisted_failure = failProvisioning(snapshot, local_error, &persist_error);
        refresh_output_snapshot();
        if (error)
        {
            *error = local_error.empty() ? persist_error : local_error;
            if (!persisted_failure && !persist_error.empty() && *error != persist_error)
            {
                *error += "; additionally failed to persist provisioning failure: " + persist_error;
            }
        }
        return false;
    }

    if (!completeProvisioning(snapshot, resolved_distributions, &local_error,
                              stagedRuntimeFingerprint(manifest, snapshot, cancellation_token)))
    {
        if (an::platform::isLoggingInitialized())
        {
            LOG_WARNING(
                "Extension environment provisioning could not be finalized for extension id='{}' version='{}' env='{}' "
                "dependency_fingerprint='{}' validation_fingerprint='{}' reason={}",
                manifest.id, manifest.version, pathForLog(snapshot.environment_path), snapshot.dependency_fingerprint,
                snapshot.validation_fingerprint, local_error);
        }
        std::string persist_error;
        const bool persisted_failure = failProvisioning(snapshot, local_error, &persist_error);
        refresh_output_snapshot();
        if (error)
        {
            *error = local_error.empty() ? persist_error : local_error;
            if (!persisted_failure && !persist_error.empty() && *error != persist_error)
            {
                *error += "; additionally failed to persist provisioning failure: " + persist_error;
            }
        }
        return false;
    }

    const ExtensionEnvironmentSnapshot ready_snapshot = refresh_output_snapshot();
    if (ready_snapshot.state != ExtensionEnvironmentState::Ready)
    {
        if (an::platform::isLoggingInitialized())
        {
            LOG_WARNING(
                "Extension environment provisioning completed without a ready environment for extension id='{}' "
                "version='{}' env='{}' state={} diagnostic={}",
                manifest.id, manifest.version, pathForLog(ready_snapshot.environment_path),
                static_cast<int>(ready_snapshot.state), ready_snapshot.diagnostic);
        }
        if (error)
        {
            *error = ready_snapshot.diagnostic.empty()
                         ? "Extension environment provisioning did not produce a ready environment."
                         : ready_snapshot.diagnostic;
        }
        return false;
    }

    if (out_resolved_distributions)
    {
        *out_resolved_distributions = std::move(resolved_distributions);
    }
    return true;
}

bool ExtensionEnvironmentManager::precompilePackageBytecode(
    const ExtensionManifest& manifest, const ExtensionEnvironmentSnapshot& snapshot, std::string* error,
    ExtensionOperationProgressCallback progress_callback, ExtensionOperationCancellationToken cancellation_token) const
{
    if (cancellationRequested(cancellation_token))
    {
        setOperationCancelledError(error);
        return false;
    }

    const std::filesystem::path package_root(manifest.package_root);
    if (package_root.empty() || !isDirectory(package_root))
    {
        if (error)
        {
            *error = "Extension package root is missing for bytecode precompilation";
        }
        return false;
    }

#ifdef _WIN32
    const std::filesystem::path python_executable = snapshot.bundled_python_executable;
    const QProcessEnvironment process_environment =
        isolatedPythonProcessEnvironment(windowsProvisioningPythonHome(python_executable, snapshot.runtime_root));
    const QProcessEnvironment* process_environment_ptr = &process_environment;
#else
    const std::filesystem::path python_executable = snapshot.bundled_python_executable;
    const std::filesystem::path python_home = posixProvisioningPythonHome(python_executable, snapshot.runtime_root);
    const QProcessEnvironment process_environment = isolatedPythonProcessEnvironment(python_home);
    const QProcessEnvironment* process_environment_ptr = &process_environment;
#endif

    return precompilePythonBytecodeTree(python_executable, package_root, package_root, process_environment_ptr,
                                        package_root, manifest.id, {}, error, kBytecodePrecompileTimeoutMs,
                                        cancellation_token, progress_callback, "precompile_bytecode",
                                        "Precompiling extension Python bytecode");
}

namespace
{

/// Whether provisioning a package's environment installs from an index or a link on the network.
[[nodiscard]] bool provisioningReachesTheNetwork(const ExtensionManifest& manifest)
{
    if (manifest.dependencies.requirements_file.empty())
    {
        return false;
    }
    const auto& pip = manifest.dependencies.pip;
    return !pip.index_urls.empty() || !pip.extra_index_urls.empty() ||
           std::any_of(pip.find_links.begin(), pip.find_links.end(),
                       [](const std::string& link) { return isUrlLike(link); });
}

} // namespace

int ExtensionEnvironmentManager::queueMaintenance(std::vector<ExtensionLoadResult>& discovered, const bool bundledOnly,
                                                  const bool allowNetwork, std::string* error,
                                                  const ExtensionOperationCancellationToken& cancellationToken) const
{
    int queuedCount = 0;
    std::map<std::filesystem::path, std::string, std::less<>> queuedDiagnostics;
    // What the packages resolve to and the roots walked for environments to take over are found once for the pass.
    const std::vector<std::filesystem::path> resolved = discoveredEnvironmentsOf(discovered);
    AdoptionScan scan;
    for (auto& result : discovered)
    {
        if (cancellationRequested(cancellationToken))
        {
            return queuedCount;
        }

        if ((bundledOnly && result.source != ExtensionPackageSource::Bundled) ||
            result.status != an::ui::extensions::ExtensionLoadStatus::Ready || !result.manifest || !result.environment)
        {
            continue;
        }

        auto& environment = *result.environment;
        if (environment.state == ExtensionEnvironmentState::Ready ||
            environment.state == ExtensionEnvironmentState::Bootstrapping)
        {
            continue;
        }

        const std::filesystem::path environmentPath = normalizedEnvironmentPath(environment);
        if (const auto queued = queuedDiagnostics.find(environmentPath); queued != queuedDiagnostics.end())
        {
            environment.state = ExtensionEnvironmentState::Bootstrapping;
            environment.diagnostic = queued->second;
            continue;
        }

        std::string diagnostic;
        if (environment.state == ExtensionEnvironmentState::Stale && environment.refreshable)
        {
            diagnostic = "Extension environment is being refreshed for this build of AfterNight.";
        }
        else if (environment.state == ExtensionEnvironmentState::Missing &&
                 !findAdoptableEnvironment(*result.manifest, environment, resolved, &scan).empty())
        {
            diagnostic = "Extension environment is being taken over from an earlier version of the extension.";
        }
        else
        {
            if (!allowNetwork && provisioningReachesTheNetwork(*result.manifest))
            {
                continue;
            }

            // One an install or a launch of this process is preparing already is left to it, not cleared under it.
            const EnvironmentPreparationGuard guard(environment.environment_path, std::try_to_lock);
            if (!guard.acquired())
            {
                continue;
            }
            std::string localError;
            if (environment.state == ExtensionEnvironmentState::Stale &&
                !clearReadyEnvironmentMarkers(environment, &localError))
            {
                if (error)
                {
                    *error = localError;
                }
                return queuedCount;
            }

            if (!beginProvisioning(environment, &localError))
            {
                if (an::platform::isLoggingInitialized())
                {
                    LOG_WARNING(
                        "Extension environment provisioning could not be queued for extension id='{}' path='{}' "
                        "reason={}",
                        result.manifest->id, environment.environment_path.generic_string(), localError);
                }
                if (error)
                {
                    *error = localError;
                }
                return queuedCount;
            }
            diagnostic = result.source == ExtensionPackageSource::Bundled
                             ? "Bundled extension environment bootstrap is in progress."
                             : "Extension environment provisioning is in progress.";
        }

        environment.state = ExtensionEnvironmentState::Bootstrapping;
        environment.diagnostic = diagnostic;
        queuedDiagnostics.emplace(environmentPath, std::move(diagnostic));
        {
            const std::lock_guard lock(m_queuedMaintenanceMutex);
            m_queuedMaintenancePaths.insert(environmentPath);
        }
        ++queuedCount;
    }

    return queuedCount;
}

int ExtensionEnvironmentManager::processMaintenance(std::vector<ExtensionLoadResult>& discovered,
                                                    const bool bundledOnly, std::string* error,
                                                    const ExtensionOperationCancellationToken& cancellationToken) const
{
    int processedCount = 0;
    std::map<std::filesystem::path, std::pair<ExtensionEnvironmentState, std::string>, std::less<>> processedStates;
    // What the packages resolve to is found once for the pass; the roots are walked afresh for each environment taken
    // over, since taking one over moves it.
    const std::vector<std::filesystem::path> resolved = discoveredEnvironmentsOf(discovered);
    for (auto& result : discovered)
    {
        if (cancellationRequested(cancellationToken))
        {
            releaseQueuedMaintenance(discovered);
            return processedCount;
        }

        if ((bundledOnly && result.source != ExtensionPackageSource::Bundled) ||
            result.status != an::ui::extensions::ExtensionLoadStatus::Ready || !result.manifest ||
            !result.environment || result.environment->state != ExtensionEnvironmentState::Bootstrapping)
        {
            continue;
        }

        ExtensionEnvironmentSnapshot& environment = *result.environment;
        const std::filesystem::path environmentPath = normalizedEnvironmentPath(environment);
        if (const auto processed = processedStates.find(environmentPath); processed != processedStates.end())
        {
            environment.state = processed->second.first;
            environment.diagnostic = processed->second.second;
            continue;
        }
        {
            const std::lock_guard lock(m_queuedMaintenanceMutex);
            if (m_queuedMaintenancePaths.erase(environmentPath) == 0)
            {
                continue;
            }
        }

        // A launch preparing the same environment meanwhile is waited for: what it leaves may already be ready.
        const EnvironmentPreparationGuard guard(environment.environment_path, cancellationToken);
        if (!guard.acquired())
        {
            releaseQueuedMaintenance(discovered);
            return processedCount;
        }

        std::string localError;
        const std::filesystem::path lock = environment.environment_path / std::string(kProvisioningLockName);
        if (environment.refreshable || !isRegularFile(lock))
        {
            // Nothing is installed: the environment is refreshed in place, or the one an earlier version of the
            // extension left is taken over. One that cannot be is left for a launch or the next run to try again.
            bool kept = false;
            if (alreadyPreparedFor(environment))
            {
                kept = true;
            }
            else if (environment.refreshable)
            {
                kept = refreshEnvironment(*result.manifest, environment, &localError, {}, cancellationToken);
            }
            else if (const std::filesystem::path adoptable =
                         findAdoptableEnvironment(*result.manifest, environment, resolved);
                     !adoptable.empty())
            {
                kept = adoptEnvironment(*result.manifest, environment, adoptable, &localError, {}, cancellationToken);
            }
            else
            {
                localError = "The extension environment an earlier version of the extension left is no longer there.";
            }
            if (cancellationRequested(cancellationToken))
            {
                releaseQueuedMaintenance(discovered);
                return processedCount;
            }

            if (kept)
            {
                environment.state = ExtensionEnvironmentState::Ready;
                environment.refreshable = false;
                environment.diagnostic.clear();
            }
            else
            {
                if (an::platform::isLoggingInitialized())
                {
                    LOG_WARNING("Extension environment maintenance failed for extension id='{}' path='{}' reason={}",
                                result.manifest->id, environment.environment_path.generic_string(), localError);
                }
                environment.state =
                    environment.refreshable ? ExtensionEnvironmentState::Stale : ExtensionEnvironmentState::Missing;
                environment.diagnostic = localError;
            }
            processedStates.emplace(environmentPath, std::pair{environment.state, environment.diagnostic});
            ++processedCount;
            continue;
        }

        const char* const operation = result.source == ExtensionPackageSource::Bundled
                                          ? "Bundled extension bootstrap"
                                          : "Extension environment provisioning";
        std::vector<ExtensionInstalledDistribution> resolvedDistributions;
        if (!provisionEnvironmentFromManifest(*result.manifest, environment, &resolvedDistributions, &localError, {},
                                              cancellationToken))
        {
            if (an::platform::isLoggingInitialized())
            {
                LOG_WARNING("{} failed for extension id='{}' path='{}' reason={}", operation, result.manifest->id,
                            environment.environment_path.generic_string(), localError);
            }
            if (!failProvisioning(environment, localError, &localError))
            {
                if (an::platform::isLoggingInitialized())
                {
                    LOG_WARNING("{} failure could not be persisted for extension id='{}' reason={}", operation,
                                result.manifest->id, localError);
                }
                if (error)
                {
                    *error = localError;
                }
                releaseQueuedMaintenance(discovered);
                return processedCount;
            }

            environment.state = ExtensionEnvironmentState::Failed;
            environment.diagnostic = localError;
            processedStates.emplace(environmentPath, std::pair{environment.state, environment.diagnostic});
            ++processedCount;
            continue;
        }

        if (!completeProvisioning(environment, resolvedDistributions, &localError,
                                  stagedRuntimeFingerprint(*result.manifest, environment, cancellationToken)))
        {
            if (an::platform::isLoggingInitialized())
            {
                LOG_WARNING("{} could not finalize for extension id='{}' path='{}' reason={}", operation,
                            result.manifest->id, environment.environment_path.generic_string(), localError);
            }
            if (error)
            {
                *error = localError;
            }
            std::string failureError;
            (void)failProvisioning(environment, localError, &failureError);
            environment.state = ExtensionEnvironmentState::Failed;
            environment.diagnostic = localError;
            releaseQueuedMaintenance(discovered);
            return processedCount;
        }

        environment.state = ExtensionEnvironmentState::Ready;
        environment.diagnostic.clear();
        processedStates.emplace(environmentPath, std::pair{environment.state, environment.diagnostic});
        ++processedCount;
    }

    return processedCount;
}

void ExtensionEnvironmentManager::releaseQueuedMaintenance(std::vector<ExtensionLoadResult>& discovered) const
{
    std::set<std::filesystem::path, std::less<>> queued;
    {
        const std::lock_guard lock(m_queuedMaintenanceMutex);
        queued.swap(m_queuedMaintenancePaths);
    }
    if (queued.empty())
    {
        return;
    }
    for (const std::filesystem::path& environmentPath : queued)
    {
        const std::filesystem::path lock = environmentPath / std::string(kProvisioningLockName);
        if (const auto metadata = readProvisioningLockMetadata(lock);
            metadata.owner_pid.has_value() && *metadata.owner_pid == QCoreApplication::applicationPid())
        {
            std::error_code ec;
            std::filesystem::remove(lock, ec);
        }
        noteLockReleased(lock);
    }
    for (auto& result : discovered)
    {
        if (result.environment.has_value() && result.environment->state == ExtensionEnvironmentState::Bootstrapping &&
            queued.find(normalizedEnvironmentPath(*result.environment)) != queued.end())
        {
            result.environment->state = ExtensionEnvironmentState::Missing;
            result.environment->diagnostic =
                "Extension environment maintenance stopped before this environment was prepared.";
        }
    }
}

int ExtensionEnvironmentManager::queueBundledBootstrap(std::vector<ExtensionLoadResult>& discovered, std::string* error,
                                                       ExtensionOperationCancellationToken cancellation_token) const
{
    return queueMaintenance(discovered, true, true, error, cancellation_token);
}

int ExtensionEnvironmentManager::processQueuedBundledBootstrap(
    std::vector<ExtensionLoadResult>& discovered, std::string* error,
    ExtensionOperationCancellationToken cancellation_token) const
{
    return processMaintenance(discovered, true, error, cancellation_token);
}

int ExtensionEnvironmentManager::queueEnvironmentMaintenance(
    std::vector<ExtensionLoadResult>& discovered, const bool allowNetwork, std::string* error,
    ExtensionOperationCancellationToken cancellationToken) const
{
    return queueMaintenance(discovered, false, allowNetwork, error, cancellationToken);
}

int ExtensionEnvironmentManager::processQueuedEnvironmentMaintenance(
    std::vector<ExtensionLoadResult>& discovered, std::string* error,
    ExtensionOperationCancellationToken cancellationToken) const
{
    return processMaintenance(discovered, false, error, cancellationToken);
}

std::vector<ExtensionEnvironmentInventoryEntry>
ExtensionEnvironmentManager::collectEnvironmentInventory(const std::vector<ExtensionLoadResult>& discovered,
                                                         const ExtensionDiscoveryContext& context,
                                                         ExtensionOperationCancellationToken cancellation_token) const
{
    std::vector<ExtensionEnvironmentInventoryEntry> entries;
    const std::filesystem::path environments_root = resolveEnvironmentsRoot(context);

    for (const auto& root_entry : sortedDirectoryEntries(environments_root))
    {
        if (root_entry.path().filename() == "_shared")
        {
            appendSharedEnvironmentInventory(root_entry.path(), entries);
            continue;
        }
        if (root_entry.path().filename() == "_host_shared")
        {
            appendHostSharedEnvironmentInventory(root_entry.path(), entries);
            continue;
        }

        appendPerExtensionInventory(root_entry.path(), entries);
    }

    std::set<std::filesystem::path> referenced_environment_paths;
    std::set<std::string, std::less<>> installed_identities;
    std::set<std::string, std::less<>> ready_identities;
    for (const ExtensionLoadResult& result : discovered)
    {
        if (cancellationRequested(cancellation_token) || !result.manifest)
        {
            continue;
        }

        const std::string identity = environmentIdentity(*result.manifest);
        if (!identity.empty())
        {
            installed_identities.insert(identity);
            if (result.status == an::ui::extensions::ExtensionLoadStatus::Ready && result.environment &&
                result.environment->state == ExtensionEnvironmentState::Ready)
            {
                ready_identities.insert(identity);
            }
        }

        if (result.status == an::ui::extensions::ExtensionLoadStatus::Invalid)
        {
            continue;
        }

        const ExtensionEnvironmentSnapshot snapshot =
            result.environment ? *result.environment
                               : inspectEnvironment(*result.manifest, result.source, context, cancellation_token);
        if (!snapshot.environment_path.empty())
        {
            referenced_environment_paths.insert(snapshot.environment_path.lexically_normal());
        }
    }

    for (auto& entry : entries)
    {
        if (cancellationRequested(cancellation_token))
        {
            break;
        }

        const std::string identity = environmentIdentity(entry);
        entry.orphaned = !referenced_environment_paths.contains(entry.environment_path.lexically_normal());
        entry.installed_identity_present = !identity.empty() && installed_identities.contains(identity);
        entry.ready_replacement_available = !identity.empty() && ready_identities.contains(identity);
        std::error_code ec;
        const std::filesystem::path provisioning_lock = entry.environment_path / std::string(kProvisioningLockName);
        entry.provisioning = std::filesystem::is_regular_file(windowsExtendedLengthPath(provisioning_lock), ec) &&
                             hasActiveProvisioningLock(provisioning_lock);
        entry.last_write_time = environmentLastWriteTime(entry.environment_path);
        entry.size_bytes = environmentDirectorySize(entry.environment_path, &cancellation_token);
    }

    return entries;
}

std::vector<std::filesystem::path>
ExtensionEnvironmentManager::collectOrphanedEnvironmentPaths(const std::vector<ExtensionLoadResult>& discovered,
                                                             const ExtensionDiscoveryContext& context) const
{
    std::vector<std::filesystem::path> orphaned_paths;
    for (const auto& entry : collectEnvironmentInventory(discovered, context))
    {
        if (entry.orphaned)
        {
            orphaned_paths.push_back(entry.environment_path);
        }
    }

    return orphaned_paths;
}

std::vector<std::filesystem::path>
ExtensionEnvironmentManager::collectUnusedEnvironmentPaths(const std::vector<ExtensionLoadResult>& discovered,
                                                           const ExtensionDiscoveryContext& context,
                                                           const ExtensionEnvironmentCleanupPolicy& policy) const
{
    std::vector<std::filesystem::path> unused_paths;
    for (const auto& entry : collectUnusedEnvironmentInventory(discovered, context, policy))
    {
        unused_paths.push_back(entry.environment_path);
    }

    return unused_paths;
}

std::vector<ExtensionEnvironmentInventoryEntry> ExtensionEnvironmentManager::collectUnusedEnvironmentInventory(
    const std::vector<ExtensionLoadResult>& discovered, const ExtensionDiscoveryContext& context,
    const ExtensionEnvironmentCleanupPolicy& policy, ExtensionOperationCancellationToken cancellation_token) const
{
    std::vector<ExtensionEnvironmentInventoryEntry> unused_entries;
    for (auto& entry : collectEnvironmentInventory(discovered, context, cancellation_token))
    {
        if (cleanupEligible(entry, policy))
        {
            unused_entries.push_back(std::move(entry));
        }
    }

    return unused_entries;
}

ExtensionEnvironmentStorageSummary ExtensionEnvironmentManager::summarizeEnvironmentStorage(
    const std::vector<ExtensionLoadResult>& discovered, const ExtensionDiscoveryContext& context,
    const ExtensionEnvironmentCleanupPolicy& policy, ExtensionOperationCancellationToken cancellation_token) const
{
    ExtensionEnvironmentStorageSummary summary;
    for (const auto& entry : collectEnvironmentInventory(discovered, context, cancellation_token))
    {
        if (cancellationRequested(cancellation_token))
        {
            break;
        }

        ++summary.total_environment_count;
        summary.total_bytes += entry.size_bytes;
        if (!entry.orphaned)
        {
            ++summary.active_environment_count;
            summary.active_bytes += entry.size_bytes;
        }
        else if (cleanupEligible(entry, policy))
        {
            ++summary.reclaimable_environment_count;
            summary.reclaimable_bytes += entry.size_bytes;
        }
        else
        {
            ++summary.protected_environment_count;
            summary.protected_bytes += entry.size_bytes;
        }
    }

    return summary;
}

bool ExtensionEnvironmentManager::removeUnusedEnvironments(const std::vector<ExtensionLoadResult>& discovered,
                                                           const ExtensionDiscoveryContext& context,
                                                           const ExtensionEnvironmentCleanupPolicy& policy,
                                                           std::vector<std::filesystem::path>* removed_paths,
                                                           std::uintmax_t* reclaimed_bytes, std::string* error,
                                                           ExtensionOperationCancellationToken cancellation_token) const
{
    if (reclaimed_bytes)
    {
        *reclaimed_bytes = 0;
    }

    const auto inventory = collectEnvironmentInventory(discovered, context, cancellation_token);
    for (const auto& entry : inventory)
    {
        if (cancellationRequested(cancellation_token))
        {
            setOperationCancelledError(error);
            return false;
        }
        if (!cleanupEligible(entry, policy))
        {
            continue;
        }

        if (!removePathTreeWithDiagnostics(entry.environment_path, "Failed to remove unused environment", error))
        {
            return false;
        }
        removeEmptyEnvironmentParents(entry.environment_path, resolveEnvironmentsRoot(context));
        if (removed_paths)
        {
            removed_paths->push_back(entry.environment_path);
        }
        if (reclaimed_bytes)
        {
            *reclaimed_bytes += entry.size_bytes;
        }
    }

    return true;
}

bool ExtensionEnvironmentManager::removeReplacedEnvironments(
    const std::vector<ExtensionLoadResult>& discovered, const ExtensionDiscoveryContext& context,
    std::vector<std::filesystem::path>* removedPaths, std::uintmax_t* reclaimedBytes,
    ExtensionOperationCancellationToken cancellationToken) const
{
    if (reclaimedBytes)
    {
        *reclaimedBytes = 0;
    }

    const std::filesystem::path environmentsRoot = resolveEnvironmentsRoot(context);
    for (const auto& entry : collectEnvironmentInventory(discovered, context, cancellationToken))
    {
        if (cancellationRequested(cancellationToken))
        {
            return false;
        }
        if (!entry.orphaned || entry.provisioning || !entry.installed_identity_present ||
            !entry.ready_replacement_available)
        {
            continue;
        }

        std::string removalError;
        if (!removePathTreeWithDiagnostics(entry.environment_path, "Failed to remove a replaced environment",
                                           &removalError))
        {
            if (an::platform::isLoggingInitialized())
            {
                LOG_WARNING("A replaced extension environment could not be removed now and is left for a later run: {}",
                            removalError);
            }
            continue;
        }
        removeEmptyEnvironmentParents(entry.environment_path, environmentsRoot);
        if (an::platform::isLoggingInitialized())
        {
            LOG_INFO("Removed replaced extension environment path='{}' bytes={}", pathForLog(entry.environment_path),
                     entry.size_bytes);
        }
        if (removedPaths)
        {
            removedPaths->push_back(entry.environment_path);
        }
        if (reclaimedBytes)
        {
            *reclaimedBytes += entry.size_bytes;
        }
    }

    return true;
}

bool ExtensionEnvironmentManager::removeOrphanedEnvironments(
    const std::vector<ExtensionLoadResult>& discovered, const ExtensionDiscoveryContext& context,
    std::vector<std::filesystem::path>* removed_paths, std::string* error,
    ExtensionOperationCancellationToken cancellation_token) const
{
    const auto orphaned_paths = collectOrphanedEnvironmentPaths(discovered, context);
    for (const auto& path : orphaned_paths)
    {
        if (cancellationRequested(cancellation_token))
        {
            setOperationCancelledError(error);
            return false;
        }

        if (!removePathTreeWithDiagnostics(path, "Failed to remove orphaned environment", error))
        {
            return false;
        }
        removeEmptyEnvironmentParents(path, resolveEnvironmentsRoot(context));

        if (removed_paths)
        {
            removed_paths->push_back(path);
        }
    }

    return true;
}

} // namespace an::ui::extensions
