// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The extension manager and its environments: manifests read, normalized and refused; packages validated against the
/// licensing boundary, their runtime targets, paths, artifacts and dependency metadata; package archives extracted
/// safely; local and repository install, update, repair and remove transactions and their rollbacks; the repository
/// index, client and browse entries; discovery, its precedence, cancellation and settings-schema caches; the process
/// registry's descriptors; environment provisioning, bootstrap, shared contexts, fingerprints, inventory and cleanup;
/// and the bundled extensions as deployed. Every package, environment and archive a case makes lives under a scratch
/// directory of its own, removed when the case ends, deep virtual environment trees included; a case that inspects
/// through the full extension host runs where the host is deployed beside the suite.

#include "Test_ProcessesShared/ProcessesSharedTestSupport.h"
#include "Test_ProcessFramework/ExtensionTestSupport.h"
#include "fixtures/ScopedEnvVar.h"

#include "app/ExtensionEnvironmentManager.h"
#include "app/ExtensionHostManager.h"
#include "app/ExtensionManager.h"
#include "app/ExtensionSdkService.h"
#include "controls/GlobalSettingsPersistence.h"
#include "controls/PersistenceDbExecutor.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonObject>
#include <QProcess>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QUrl>

#include <sodium.h>
#include <zstd.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <tuple>
#include <utility>
#include <vector>

namespace
{

using an::tests::processes::framework::createVirtualEnvironment;
using an::ui::extensions::ExtensionDependencyContext;
using an::ui::extensions::ExtensionDiscoveryContext;
using an::ui::extensions::ExtensionDiscoveryOptions;
using an::ui::extensions::ExtensionEnvironmentCleanupPolicy;
using an::ui::extensions::ExtensionEnvironmentInspectionMode;
using an::ui::extensions::ExtensionEnvironmentManager;
using an::ui::extensions::ExtensionEnvironmentSnapshot;
using an::ui::extensions::ExtensionEnvironmentState;
using an::ui::extensions::ExtensionInstalledDistribution;
using an::ui::extensions::ExtensionLoadStatus;
using an::ui::extensions::ExtensionManager;
using an::ui::extensions::ExtensionManifest;
using an::ui::extensions::ExtensionOperationCancellationToken;
using an::ui::extensions::ExtensionOperationProgress;
using an::ui::extensions::ExtensionPackageSignatureState;
using an::ui::extensions::ExtensionPackageSource;
using an::ui::extensions::ExtensionPackageValidationSeverity;
using an::ui::extensions::ExtensionRepositoryInstallState;
using an::ui::extensions::ExtensionSdkBackend;
using an::ui::extensions::ExtensionSharedHostProfileFingerprintSource;
using an::ui::extensions::RelocatablePackage;
using an::ui::process_registry::ProcessAvailability;
using an::ui::process_registry::ProcessRegistry;

void writeTextFile(const std::filesystem::path& path, const std::string& contents);
std::filesystem::path writeSmallRuntimeRoot(const std::filesystem::path& root);
std::string dependencyFreeManifest(const std::string& id, const std::string& version = "1.0.0");

/// A process other than this one that lives for the case: the owner a lock of another process names. A lock of this
/// process is held only while an operation of this process prepares its environment, so this process's own pid is no
/// stand-in for a live owner.
class LiveOtherProcess
{
public:
    LiveOtherProcess()
    {
#ifdef _WIN32
        m_process.start(QStringLiteral("ping"),
                        {QStringLiteral("-n"), QStringLiteral("120"), QStringLiteral("127.0.0.1")});
#else
        m_process.start(QStringLiteral("sleep"), {QStringLiteral("120")});
#endif
        m_started = m_process.waitForStarted(5000);
    }

    ~LiveOtherProcess()
    {
        m_process.kill();
        m_process.waitForFinished(5000);
    }

    LiveOtherProcess(const LiveOtherProcess&) = delete;
    LiveOtherProcess& operator=(const LiveOtherProcess&) = delete;

    [[nodiscard]] bool started() const
    {
        return m_started;
    }

    [[nodiscard]] std::string pidLine() const
    {
        return "owner_pid=" + std::to_string(m_process.processId()) + "\n";
    }

private:
    QProcess m_process;
    bool m_started = false;
};

std::filesystem::path createPurePythonWheel(const std::filesystem::path& root, const std::string& package_name,
                                            const std::string& version, const std::vector<std::string>& requires_dist);
std::filesystem::path createPurePythonWheel(const std::filesystem::path& root, const std::string& package_name,
                                            const std::string& version, const std::vector<std::string>& requires_dist,
                                            const std::string& module_source);

bool progressContainsPhase(const std::vector<ExtensionOperationProgress>& progress_events, const std::string& phase_id)
{
    return std::any_of(progress_events.begin(), progress_events.end(),
                       [&](const ExtensionOperationProgress& progress) { return progress.phase_id == phase_id; });
}

void expectDeterminateMonotonicProgress(const std::vector<ExtensionOperationProgress>& progress_events)
{
    int last_current = -1;
    int expected_total = progress_events.empty() ? 0 : progress_events.front().total;
    for (const ExtensionOperationProgress& progress : progress_events)
    {
        EXPECT_FALSE(progress.indeterminate) << progress.phase_id << ": " << progress.message;
        EXPECT_GT(progress.total, 0) << progress.phase_id << ": " << progress.message;
        if (expected_total > 0)
        {
            EXPECT_EQ(progress.total, expected_total) << progress.phase_id << ": " << progress.message;
        }
        EXPECT_GE(progress.current, last_current) << progress.phase_id << ": " << progress.message;
        EXPECT_LE(progress.current, progress.total) << progress.phase_id << ": " << progress.message;
        last_current = (std::max)(last_current, progress.current);
    }
}

std::string environmentVariableValue(const char* const variable_name)
{
#if defined(_MSC_VER)
    char* value = nullptr;
    std::size_t value_size = 0;
    if (_dupenv_s(&value, &value_size, variable_name) != 0 || value == nullptr)
    {
        return {};
    }

    std::string result(value);
    std::free(value);
    return result;
#else
    const char* const value = std::getenv(variable_name);
    return value == nullptr ? std::string() : std::string(value);
#endif
}

std::filesystem::path findRepositoryPythonExecutable()
{
    std::error_code ec;
    auto root = std::filesystem::current_path(ec);
    if (ec)
    {
        return {};
    }

    while (!root.empty())
    {
#if defined(_WIN32)
        const auto candidate = root / "python" / "python" / "tools_msvc" / "python.exe";
#else
        const auto candidate = root / "python" / "python" / "tools_linux" / "bin" / "python3.14";
#endif
        if (std::filesystem::exists(candidate))
        {
            return candidate;
        }

        const auto parent = root.parent_path();
        if (parent == root)
        {
            break;
        }
        root = parent;
    }

    return {};
}

std::filesystem::path findRepositoryBundledExtensionsRoot()
{
    std::error_code ec;
    auto root = std::filesystem::current_path(ec);
    if (ec)
    {
        return {};
    }

    while (!root.empty())
    {
        const auto candidate = root / "extensions" / "an_echo_demo" / "extension.json";
        // Require a repo-root marker so we don't match the deployed extensions/ inside bin/
        if (std::filesystem::exists(candidate) && std::filesystem::exists(root / "AGENTS.md"))
        {
            return (root / "extensions").lexically_normal();
        }

        const auto parent = root.parent_path();
        if (parent == root)
        {
            break;
        }
        root = parent;
    }

    return {};
}

std::filesystem::path findRepositoryExtensionExamplesRoot()
{
    std::error_code ec;
    auto root = std::filesystem::current_path(ec);
    if (ec)
    {
        return {};
    }

    while (!root.empty())
    {
        const auto candidate = root / "docs" / "extension-examples" / "basic_process" / "extension.json";
        if (std::filesystem::exists(candidate) && std::filesystem::exists(root / "AGENTS.md"))
        {
            return (root / "docs" / "extension-examples").lexically_normal();
        }

        const auto parent = root.parent_path();
        if (parent == root)
        {
            break;
        }
        root = parent;
    }

    return {};
}

std::filesystem::path runtimeSitePackagesPathForTest(const std::filesystem::path& runtime_root)
{
#ifdef _WIN32
    return runtime_root / "Lib" / "site-packages";
#else
    return runtime_root / "lib" / "python3.14" / "site-packages";
#endif
}

void ensureBundledExtensionsStaged(const std::filesystem::path& deployed_root)
{
    const auto echo_manifest = deployed_root / "an_echo_demo" / "extension.json";
    const auto rt_preview_manifest = deployed_root / "an_rt_preview_demo" / "extension.json";
    const auto custom_ui_manifest = deployed_root / "an_custom_ui_demo" / "extension.json";
    const auto standalone_preview_manifest = deployed_root / "an_standalone_preview_demo" / "extension.json";

    const auto source_root = findRepositoryBundledExtensionsRoot();
    ASSERT_FALSE(source_root.empty()) << "Failed to locate repository extensions/ root";

    std::error_code ec;
    std::filesystem::create_directories(deployed_root, ec);
    ASSERT_FALSE(static_cast<bool>(ec)) << "Failed to create bundled extensions root: " << ec.message();

    for (const auto& entry : std::filesystem::directory_iterator(source_root))
    {
        const auto destination = deployed_root / entry.path().filename();
        ec.clear();
        std::filesystem::copy(
            entry.path(), destination,
            std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing, ec);
        ASSERT_FALSE(static_cast<bool>(ec)) << "Failed to stage bundled extension asset from " << entry.path() << " to "
                                            << destination << ": " << ec.message();
    }

    ASSERT_TRUE(std::filesystem::exists(echo_manifest)) << "Expected staged echo demo manifest at " << echo_manifest;
    ASSERT_TRUE(std::filesystem::exists(rt_preview_manifest))
        << "Expected staged RT preview demo manifest at " << rt_preview_manifest;
    ASSERT_TRUE(std::filesystem::exists(custom_ui_manifest))
        << "Expected staged custom UI demo manifest at " << custom_ui_manifest;
    ASSERT_TRUE(std::filesystem::exists(standalone_preview_manifest))
        << "Expected staged standalone preview demo manifest at " << standalone_preview_manifest;
}

std::string sha256FileHex(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open())
    {
        ADD_FAILURE() << "Failed to open " << path;
        return {};
    }

    std::string contents((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return QCryptographicHash::hash(QByteArray(contents.data(), static_cast<QByteArray::size_type>(contents.size())),
                                    QCryptographicHash::Sha256)
        .toHex()
        .toStdString();
}

std::filesystem::path createPurePythonWheel(const std::filesystem::path& root, const std::string& package_name,
                                            const std::string& version)
{
    return createPurePythonWheel(root, package_name, version, {});
}

std::filesystem::path createPurePythonWheel(const std::filesystem::path& root, const std::string& package_name,
                                            const std::string& version, const std::vector<std::string>& requires_dist)
{
    return createPurePythonWheel(root, package_name, version, requires_dist, "__version__ = '" + version + "'\n");
}

std::filesystem::path createPurePythonWheel(const std::filesystem::path& root, const std::string& package_name,
                                            const std::string& version, const std::vector<std::string>& requires_dist,
                                            const std::string& module_source)
{
    const auto python = findRepositoryPythonExecutable();
    if (python.empty())
    {
        ADD_FAILURE() << "Failed to locate repository Python runtime";
        return {};
    }

    const auto wheel_dir = root / "wheelhouse";
    const auto script_path = root / "make_wheel.py";
    writeTextFile(script_path,
                  R"py(import base64, csv, hashlib, io, json, pathlib, sys, zipfile
wheel_path = pathlib.Path(sys.argv[1])
package_name = sys.argv[2]
version = sys.argv[3]
requires_dist = json.loads(sys.argv[4])
module_source = sys.argv[5]
module_name = package_name.replace('-', '_')
dist_name = module_name
metadata_lines = [
    'Metadata-Version: 2.1',
    f'Name: {package_name}',
    f'Version: {version}',
    'Summary: Test package',
]
for requirement in requires_dist:
    metadata_lines.append(f'Requires-Dist: {requirement}')
files = {
    f"{module_name}/__init__.py": module_source,
    f"{dist_name}-{version}.dist-info/WHEEL": "Wheel-Version: 1.0\nGenerator: afternight-test\nRoot-Is-Purelib: true\nTag: py3-none-any\n",
    f"{dist_name}-{version}.dist-info/METADATA": '\n'.join(metadata_lines) + '\n',
}
records = []
for path, text in files.items():
    data = text.encode("utf-8")
    digest = base64.urlsafe_b64encode(hashlib.sha256(data).digest()).rstrip(b"=").decode("ascii")
    records.append((path, f"sha256={digest}", str(len(data))))
records.append((f"{dist_name}-{version}.dist-info/RECORD", "", ""))
buffer = io.StringIO()
csv.writer(buffer, lineterminator="\n").writerows(records)
files[f"{dist_name}-{version}.dist-info/RECORD"] = buffer.getvalue()
wheel_path.parent.mkdir(parents=True, exist_ok=True)
with zipfile.ZipFile(wheel_path, "w", zipfile.ZIP_DEFLATED) as archive:
    for path, text in files.items():
        archive.writestr(path, text)
)py");

    const auto final_wheel_path = wheel_dir / (QStringLiteral("%1-%2-py3-none-any.whl")
                                                   .arg(QString::fromStdString(package_name).replace('-', '_'))
                                                   .arg(QString::fromStdString(version))
                                                   .toStdString());

    QJsonArray requires_dist_json;
    for (const auto& requirement : requires_dist)
    {
        requires_dist_json.append(QString::fromStdString(requirement));
    }

    QProcess process;
    process.setProgram(QString::fromStdString(python.string()));
    process.setArguments({QString::fromStdString(script_path.string()),
                          QString::fromStdString(final_wheel_path.string()), QString::fromStdString(package_name),
                          QString::fromStdString(version),
                          QString::fromUtf8(QJsonDocument(requires_dist_json).toJson(QJsonDocument::Compact)),
                          QString::fromStdString(module_source)});
    process.start();
    if (!process.waitForStarted() || !process.waitForFinished() || process.exitStatus() != QProcess::NormalExit ||
        process.exitCode() != 0)
    {
        ADD_FAILURE() << process.readAllStandardError().toStdString();
        return {};
    }
    if (!std::filesystem::exists(final_wheel_path))
    {
        ADD_FAILURE() << "Expected wheel to be created at " << final_wheel_path;
        return {};
    }
    return final_wheel_path;
}

class ScopedTempDirectory
{
public:
    ScopedTempDirectory()
    {
        const auto base = std::filesystem::temp_directory_path();
        const auto unique_id = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        // Short prefix on purpose: these paths carry deep extension trees, and tar entry
        // names built from them are bounded by the 100-byte ustar name field.
        m_path = base / ("anext-" + unique_id);
        std::filesystem::create_directories(m_path);
    }

    // A provisioned environment's tree goes past MAX_PATH, where a plain removal stops at the first entry it cannot
    // name and leaves the rest behind; the removal names every entry through the long-path prefix, and retries while
    // a Python process the case started may still hold a file.
    ~ScopedTempDirectory()
    {
        EXPECT_TRUE(an::tests::processes::framework::removeTree(m_path))
            << "the scratch directory " << m_path.string() << " was left behind";
    }

    ScopedTempDirectory(const ScopedTempDirectory&) = delete;
    ScopedTempDirectory& operator=(const ScopedTempDirectory&) = delete;

    [[nodiscard]] const std::filesystem::path& path() const
    {
        return m_path;
    }

private:
    std::filesystem::path m_path;
};

void writeTextFile(const std::filesystem::path& path, const std::string& contents)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    ASSERT_TRUE(out.is_open()) << "Failed to open " << path;
    out << contents;
}

std::string readTextFile(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    EXPECT_TRUE(in.is_open()) << "Failed to open " << path;
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

std::filesystem::path createExtensionPackage(const std::filesystem::path& root, const std::string& folder_name,
                                             const std::string& manifest_json)
{
    const auto package_root = root / folder_name;
    std::filesystem::create_directories(package_root);
    writeTextFile(package_root / "extension.json", manifest_json);
    return package_root;
}

std::string currentRuntimeTarget()
{
#if defined(_WIN32)
    return "windows-msvc-x86_64";
#else
    return "linux-clang-x86_64";
#endif
}

std::string base64Encode(const unsigned char* data, const std::size_t size)
{
    return QByteArray(reinterpret_cast<const char*>(data), static_cast<QByteArray::size_type>(size))
        .toBase64()
        .toStdString();
}

std::string repositoryAssetSignaturePayload(const std::string& package_id, const std::string& version,
                                            const std::string& asset_name, const std::string& archive_hash,
                                            std::vector<std::string> runtime_targets, const std::string& key_id)
{
    std::sort(runtime_targets.begin(), runtime_targets.end());
    runtime_targets.erase(std::unique(runtime_targets.begin(), runtime_targets.end()), runtime_targets.end());
    std::ostringstream targets;
    for (std::size_t i = 0; i < runtime_targets.size(); ++i)
    {
        if (i > 0)
        {
            targets << ',';
        }
        targets << runtime_targets[i];
    }

    std::ostringstream payload;
    payload << "afternight-extension-asset-signature-v1\n"
            << "package_id=" << package_id << '\n'
            << "version=" << version << '\n'
            << "asset_name=" << asset_name << '\n'
            << "package_hash=sha256:" << archive_hash << '\n'
            << "runtime_targets=" << targets.str() << '\n'
            << "signature_algorithm=ed25519\n"
            << "signature_key_id=" << key_id;
    return payload.str();
}

/// The seed of the cases' own signing key, which no build trusts unless a case states it.
constexpr const char* kCaseSigningSeed = "nWGxne/9WmC6hEr0kuwsxERJxWl7MmkZcDusAxyuf2A=";

/// The cases' own signing key, as the keys a service or a validator is told to trust: the build trusts only the
/// official key, so a case that signs a package states the key it signed with.
std::vector<an::ui::extensions::ExtensionSigningKey> caseSigningKeys()
{
    const QByteArray seed = QByteArray::fromBase64(QByteArray(kCaseSigningSeed));
    EXPECT_EQ(seed.size(), crypto_sign_SEEDBYTES);
    unsigned char public_key[crypto_sign_PUBLICKEYBYTES]{};
    unsigned char secret_key[crypto_sign_SECRETKEYBYTES]{};
    EXPECT_EQ(
        crypto_sign_seed_keypair(public_key, secret_key, reinterpret_cast<const unsigned char*>(seed.constData())), 0);
    return {an::ui::extensions::ExtensionSigningKey{.key_id = "afternight-test-ed25519-v1",
                                                    .public_key_base64 =
                                                        base64Encode(public_key, crypto_sign_PUBLICKEYBYTES)}};
}

std::string signRepositoryAssetPayload(const std::string& payload)
{
    const QByteArray seed = QByteArray::fromBase64(QByteArray(kCaseSigningSeed));
    EXPECT_EQ(seed.size(), crypto_sign_SEEDBYTES);
    unsigned char public_key[crypto_sign_PUBLICKEYBYTES]{};
    unsigned char secret_key[crypto_sign_SECRETKEYBYTES]{};
    EXPECT_EQ(
        crypto_sign_seed_keypair(public_key, secret_key, reinterpret_cast<const unsigned char*>(seed.constData())), 0);
    unsigned char signature[crypto_sign_BYTES]{};
    unsigned long long signature_length = 0;
    EXPECT_EQ(crypto_sign_detached(signature, &signature_length, reinterpret_cast<const unsigned char*>(payload.data()),
                                   static_cast<unsigned long long>(payload.size()), secret_key),
              0);
    EXPECT_EQ(signature_length, crypto_sign_BYTES);
    return base64Encode(signature, crypto_sign_BYTES);
}

std::string signRepositoryAsset(const std::string& package_id, const std::string& version,
                                const std::string& asset_name, const std::string& archive_hash,
                                const std::vector<std::string>& runtime_targets)
{
    if (sodium_init() < 0)
    {
        ADD_FAILURE() << "libsodium initialization failed";
        return {};
    }
    return signRepositoryAssetPayload(repositoryAssetSignaturePayload(package_id, version, asset_name, archive_hash,
                                                                      runtime_targets, "afternight-test-ed25519-v1"));
}

void applyVerifiedRepositorySignature(an::ui::extensions::ExtensionPackageInstallRequest* request,
                                      const std::string& package_id, const std::string& version,
                                      const std::string& asset_name, const std::string& archive_hash,
                                      const std::vector<std::string>& runtime_targets)
{
    ASSERT_NE(request, nullptr);
    request->expected_extension_id = package_id;
    request->expected_extension_version = version;
    request->selected_release_asset = asset_name;
    request->selected_asset_runtime_targets = runtime_targets;
    request->signature_state = ExtensionPackageSignatureState::Verified;
    request->signature_detail = "verified test fixture";
    request->signature_algorithm = "ed25519";
    request->signature_key_id = "afternight-test-ed25519-v1";
    request->signature = signRepositoryAsset(package_id, version, asset_name, archive_hash, runtime_targets);
}

ExtensionDiscoveryContext defaultContext()
{
    ExtensionDiscoveryContext context;
    context.bundled_python_version = "3.14.4";
    context.app_version = "9.9.9";
    context.runtime_target = currentRuntimeTarget();
    context.host_bootstrap_fingerprint = "host-bootstrap-test";
    context.bundled_python_executable = findRepositoryPythonExecutable();
    if (QCoreApplication::instance())
    {
        const std::filesystem::path app_dir(QCoreApplication::applicationDirPath().toStdString());
        context.extension_host_executable = an::ui::extensions::bundled_extension_host_executable_for_app_dir(app_dir);
        context.extension_host_lite_executable =
            an::ui::extensions::bundled_extension_host_lite_executable_for_app_dir(app_dir);
        context.runtime_root = app_dir / "PythonLib";
    }
    context.runtime_backend_available = true;
    context.protocol_backend_available = true;
    context.rpc_backend_available = false;
    context.workflow_launch_mode_available = true;
    // Discovery reads a ready environment's settings schema through a host it starts and waits for; its exit is given
    // what the leak configuration makes it take.
    context.settings_schema_inspection_timeout_ms += an::tests::processes::framework::hostExitAllowanceMs();
    return context;
}

std::filesystem::path extensionHostPathForCurrentAppDir()
{
    return an::ui::extensions::bundled_extension_host_executable_for_app_dir(
        std::filesystem::path(QCoreApplication::applicationDirPath().toStdString()));
}

std::filesystem::path runtimeRootForCurrentAppDir()
{
    return std::filesystem::path(QCoreApplication::applicationDirPath().toStdString()) / "PythonLib";
}

void writeInstallReceipt(const std::filesystem::path& environment_path, const std::string& host_bootstrap_fingerprint,
                         const std::string& dependency_fingerprint, const std::string& validation_fingerprint = {})
{
    const std::string validation_field = validation_fingerprint.empty()
                                             ? std::string()
                                             : "  \"validation_fingerprint\": \"" + validation_fingerprint + "\",\n";
    writeTextFile(environment_path / "install_receipt.json", "{\n"
                                                             "  \"host_bootstrap_fingerprint\": \"" +
                                                                 host_bootstrap_fingerprint +
                                                                 "\",\n"
                                                                 "  \"dependency_fingerprint\": \"" +
                                                                 dependency_fingerprint + "\",\n" + validation_field +
                                                                 "  \"receipt_version\": 1\n"
                                                                 "}\n");
    writeTextFile(environment_path / ".environment_ready", "ready\n");
}

bool treeContainsBytecodeForModule(const std::filesystem::path& root, const std::string& module_stem)
{
    std::error_code ec;
    for (std::filesystem::recursive_directory_iterator it(root, ec), end; it != end && !ec; it.increment(ec))
    {
        if (ec || !it->is_regular_file() || it->path().extension().string() != ".pyc")
        {
            continue;
        }

        const std::string filename = it->path().filename().string();
        if (filename.rfind(module_stem + ".", 0) == 0)
        {
            return true;
        }
    }

    return false;
}

QJsonObject loadJsonFile(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    EXPECT_TRUE(in.is_open()) << "Failed to open " << path;

    std::string contents((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(QByteArray::fromStdString(contents), &error);
    EXPECT_EQ(error.error, QJsonParseError::NoError) << error.errorString().toStdString();
    EXPECT_TRUE(document.isObject());
    return document.object();
}

void writeTarOctal(char* field, const std::size_t length, const std::uint64_t value)
{
    std::memset(field, 0, length);
    const std::string text =
        QStringLiteral("%1")
            .arg(static_cast<qulonglong>(value), static_cast<int>(length - 1), 8, QChar::fromLatin1('0'))
            .right(static_cast<int>(length - 1))
            .toStdString();
    std::memcpy(field, text.data(), (std::min)(text.size(), length - 1));
}

void writeTarEntry(std::ofstream& archive, const std::string& path, const std::string& contents, const char type = '0',
                   const std::uint64_t mode = 0644)
{
    ASSERT_LE(path.size(), 100u);
    const bool has_payload = type == '0' || type == 'x' || type == 'g';
    std::array<char, 512> header{};
    std::memcpy(header.data(), path.data(), path.size());
    writeTarOctal(header.data() + 100, 8, mode);
    writeTarOctal(header.data() + 108, 8, 0);
    writeTarOctal(header.data() + 116, 8, 0);
    writeTarOctal(header.data() + 124, 12, has_payload ? contents.size() : 0);
    writeTarOctal(header.data() + 136, 12, 0);
    std::memset(header.data() + 148, ' ', 8);
    header[156] = type;
    std::memcpy(header.data() + 257, "ustar", 5);
    std::memcpy(header.data() + 263, "00", 2);

    unsigned int checksum = 0;
    for (const char ch : header)
    {
        checksum += static_cast<unsigned char>(ch);
    }
    std::snprintf(header.data() + 148, 8, "%06o", checksum);
    header[154] = '\0';
    header[155] = ' ';

    archive.write(header.data(), static_cast<std::streamsize>(header.size()));
    if (has_payload)
    {
        archive.write(contents.data(), static_cast<std::streamsize>(contents.size()));
        const std::size_t padding = (512u - (contents.size() % 512u)) % 512u;
        if (padding > 0)
        {
            std::array<char, 512> zero{};
            archive.write(zero.data(), static_cast<std::streamsize>(padding));
        }
    }
}

std::string paxRecord(const std::string& key, const std::string& value)
{
    const std::string body = key + "=" + value + "\n";
    std::string record;
    std::size_t length = body.size() + 2u;
    while (true)
    {
        record = std::to_string(length) + " " + body;
        if (record.size() == length)
        {
            return record;
        }
        length = record.size();
    }
}

void finishTarArchive(std::ofstream& archive)
{
    std::array<char, 512> zero{};
    archive.write(zero.data(), static_cast<std::streamsize>(zero.size()));
    archive.write(zero.data(), static_cast<std::streamsize>(zero.size()));
}

void createTarArchive(const std::filesystem::path& path,
                      const std::vector<std::tuple<std::string, std::string, char>>& entries)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream archive(path, std::ios::binary);
    ASSERT_TRUE(archive.is_open()) << "Failed to open " << path;
    for (const auto& [entry_path, contents, type] : entries)
    {
        writeTarEntry(archive, entry_path, contents, type);
    }
    finishTarArchive(archive);
}

void createZstdCompressedArchive(const std::filesystem::path& source_path,
                                 const std::filesystem::path& destination_path)
{
    const std::string source = readTextFile(source_path);
    std::string compressed(ZSTD_compressBound(source.size()), '\0');
    const std::size_t compressed_size =
        ZSTD_compress(compressed.data(), compressed.size(), source.data(), source.size(), 10);
    ASSERT_EQ(ZSTD_isError(compressed_size), 0u) << ZSTD_getErrorName(compressed_size);
    compressed.resize(compressed_size);
    writeTextFile(destination_path, compressed);
}

void createPackageArchiveFromDirectory(const std::filesystem::path& path, const std::filesystem::path& package_root,
                                       const std::string& archive_root_name, const bool prune_wheelhouse_files)
{
    ASSERT_TRUE(std::filesystem::is_directory(package_root)) << "Expected package directory at " << package_root;

    std::vector<std::filesystem::path> directories;
    std::vector<std::filesystem::path> files;
    for (std::filesystem::recursive_directory_iterator it(package_root), end; it != end; ++it)
    {
        const auto relative_path = std::filesystem::relative(it->path(), package_root);
        const std::string relative_name = relative_path.generic_string();

        if (it->is_directory() && it->path().filename().string() == "__pycache__")
        {
            it.disable_recursion_pending();
            continue;
        }

        if (prune_wheelhouse_files && it->is_directory() && relative_name == "wheelhouse")
        {
            directories.push_back(relative_path);
            it.disable_recursion_pending();
            continue;
        }

        if (it->is_directory())
        {
            directories.push_back(relative_path);
        }
        else if (it->is_regular_file())
        {
            files.push_back(relative_path);
        }
    }

    const auto compare_paths = [](const auto& lhs, const auto& rhs) {
        return lhs.generic_string() < rhs.generic_string();
    };
    std::sort(directories.begin(), directories.end(), compare_paths);
    std::sort(files.begin(), files.end(), compare_paths);

    std::filesystem::create_directories(path.parent_path());
    std::ofstream archive(path, std::ios::binary);
    ASSERT_TRUE(archive.is_open()) << "Failed to open " << path;

    for (const auto& directory : directories)
    {
        writeTarEntry(archive, archive_root_name + "/" + directory.generic_string(), "", '5', 0755);
    }
    for (const auto& file : files)
    {
        writeTarEntry(archive, archive_root_name + "/" + file.generic_string(), readTextFile(package_root / file), '0',
                      0644);
    }
    finishTarArchive(archive);
}

bool hasValidationIssue(const an::ui::extensions::ExtensionPackageValidationResult& result, const std::string& code,
                        const ExtensionPackageValidationSeverity severity)
{
    return std::any_of(result.issues.begin(), result.issues.end(),
                       [&](const auto& issue) { return issue.code == code && issue.severity == severity; });
}

std::string describeValidationIssues(const std::vector<an::ui::extensions::ExtensionPackageValidationIssue>& issues)
{
    std::ostringstream out;
    for (const auto& issue : issues)
    {
        out << (issue.severity == ExtensionPackageValidationSeverity::Error ? "error" : "warning") << " " << issue.code
            << ": " << issue.message;
        if (!issue.path.empty())
        {
            out << " (" << issue.path << ")";
        }
        out << '\n';
    }
    return out.str();
}

QJsonArray jsonStringArray(const std::vector<std::string>& values)
{
    QJsonArray array;
    for (const auto& value : values)
    {
        array.append(QString::fromStdString(value));
    }
    return array;
}

void writeJsonFile(const std::filesystem::path& path, const QJsonObject& object)
{
    writeTextFile(path, QJsonDocument(object).toJson(QJsonDocument::Indented).toStdString());
}

} // anonymous namespace

// A manifest that declares one process is read as one process with every default the format states.
TEST(ExtensionManagerTest, SingleProcessManifestNormalizesDefaults)
{
    ScopedTempDirectory temp_dir;
    const auto package_root = createExtensionPackage(temp_dir.path(), "my_invert",
                                                     R"json({
          "id": "my_invert",
          "name": "My Invert",
          "version": "1.0.0",
          "type": "python",
          "entry_point": "invert",
          "process_class": "InvertExtension",
          "category": "filters",
          "launch_mode": "single_image",
          "package_format_version": 1,
          "protocol_version": 1,
          "sdk_version": 1
        })json");

    const auto result = an::ui::extensions::load_extension_manifest_file(
        package_root / "extension.json", ExtensionPackageSource::User, defaultContext());

    ASSERT_TRUE(result.manifest.has_value());
    EXPECT_EQ(result.status, ExtensionLoadStatus::Ready);
    EXPECT_EQ(result.manifest->sdk_backend, ExtensionSdkBackend::Runtime);
    EXPECT_TRUE(result.manifest->min_app_version.empty());
    EXPECT_EQ(result.manifest->python_compat, ">=3.14,<4");
    ASSERT_EQ(result.manifest->processes.size(), 1u);
    EXPECT_EQ(result.manifest->processes[0].id, "my_invert");
    EXPECT_EQ(result.manifest->processes[0].display_name, "My Invert");
    EXPECT_EQ(result.manifest->processes[0].action_object_name, "actionExtension_my_invert");
}

// A manifest that asks for a newer AfterNight than the running one is incompatible, saying which version it needs.
TEST(ExtensionManagerTest, ManifestMinimumAfterNightVersionMakesOlderHostsIncompatible)
{
    ScopedTempDirectory temp_dir;
    const auto package_root = createExtensionPackage(temp_dir.path(), "future_host",
                                                     R"json({
          "id": "future_host",
          "name": "Future Host",
          "version": "1.0.0",
          "min_app_version": "2.5.0",
          "type": "python",
          "entry_point": "future_host",
          "process_class": "FutureHostExtension",
          "category": "filters",
          "launch_mode": "single_image",
          "package_format_version": 1,
          "protocol_version": 1,
          "sdk_version": 1
        })json");

    auto context = defaultContext();
    context.app_version = "2.4.9";

    const auto result = an::ui::extensions::load_extension_manifest_file(package_root / "extension.json",
                                                                         ExtensionPackageSource::User, context);

    ASSERT_TRUE(result.manifest.has_value());
    EXPECT_EQ(result.manifest->min_app_version, "2.5.0");
    EXPECT_EQ(result.status, ExtensionLoadStatus::Incompatible);
    EXPECT_NE(result.diagnostic.find("Requires AfterNight 2.5.0"), std::string::npos);
    EXPECT_NE(result.diagnostic.find("2.4.9"), std::string::npos);

    an::ui::extensions::ExtensionPackageValidationOptions options;
    options.source = ExtensionPackageSource::LocalPackage;
    options.context = context;
    options.require_current_runtime_compatibility = true;
    const auto validation = an::ui::extensions::validate_extension_package_root(package_root, options);

    EXPECT_FALSE(validation.valid);
    EXPECT_TRUE(hasValidationIssue(validation, "incompatible_runtime", ExtensionPackageValidationSeverity::Error))
        << describeValidationIssues(validation.issues);
}

// A protocol-backend extension is ready where the protocol backend is available.
TEST(ExtensionManagerTest, ProtocolBackendManifestIsReadyWhenProtocolBackendIsAvailable)
{
    ScopedTempDirectory temp_dir;
    const auto package_root = createExtensionPackage(temp_dir.path(), "my_lite_extension",
                                                     R"json({
          "id": "my_lite_extension",
          "name": "My Lite Extension",
          "version": "1.0.0",
          "type": "python",
          "entry_point": "lite",
          "process_class": "LiteExtension",
          "category": "filters",
          "launch_mode": "single_image",
          "sdk_backend": "protocol",
          "package_format_version": 1,
          "protocol_version": 1,
          "sdk_version": 1
        })json");
    writeTextFile(package_root / "lite.py", "class LiteExtension: pass\n");

    auto context = defaultContext();
    context.protocol_backend_available = true;

    const auto result = an::ui::extensions::load_extension_manifest_file(package_root / "extension.json",
                                                                         ExtensionPackageSource::User, context);

    ASSERT_TRUE(result.manifest.has_value());
    EXPECT_EQ(result.status, ExtensionLoadStatus::Ready);
    EXPECT_EQ(result.manifest->sdk_backend, ExtensionSdkBackend::Protocol);
}

// A protocol-backend extension is incompatible where the protocol backend is not, saying so.
TEST(ExtensionManagerTest, ProtocolBackendManifestIsIncompatibleWhenProtocolBackendIsUnavailable)
{
    ScopedTempDirectory temp_dir;
    const auto package_root = createExtensionPackage(temp_dir.path(), "my_lite_extension",
                                                     R"json({
          "id": "my_lite_extension",
          "name": "My Lite Extension",
          "version": "1.0.0",
          "type": "python",
          "entry_point": "lite",
          "process_class": "LiteExtension",
          "category": "filters",
          "launch_mode": "single_image",
          "sdk_backend": "protocol",
          "package_format_version": 1,
          "protocol_version": 1,
          "sdk_version": 1
        })json");
    writeTextFile(package_root / "lite.py", "class LiteExtension: pass\n");

    auto context = defaultContext();
    context.protocol_backend_available = false;

    const auto result = an::ui::extensions::load_extension_manifest_file(package_root / "extension.json",
                                                                         ExtensionPackageSource::User, context);

    ASSERT_TRUE(result.manifest.has_value());
    EXPECT_EQ(result.status, ExtensionLoadStatus::Incompatible);
    EXPECT_NE(result.diagnostic.find("protocol SDK backend"), std::string::npos);
    EXPECT_NE(result.diagnostic.find("Lite hosted"), std::string::npos);
    EXPECT_NE(result.diagnostic.find("AfterNightExtensionHostLite"), std::string::npos);
}

// Each SDK backend has its identity, the host mode label people see and a description of it.
TEST(ExtensionManagerTest, SdkBackendHostModeLabelsArePublicAndExplicit)
{
    EXPECT_EQ(an::ui::extensions::extension_sdk_backend_id(ExtensionSdkBackend::Runtime), "runtime");
    EXPECT_EQ(an::ui::extensions::extension_sdk_backend_id(ExtensionSdkBackend::Protocol), "protocol");
    EXPECT_EQ(an::ui::extensions::extension_sdk_backend_id(ExtensionSdkBackend::Rpc), "rpc");

    EXPECT_EQ(an::ui::extensions::extension_host_mode_label(ExtensionSdkBackend::Runtime), "Full hosted");
    EXPECT_EQ(an::ui::extensions::extension_host_mode_label(ExtensionSdkBackend::Protocol), "Lite hosted");
    EXPECT_EQ(an::ui::extensions::extension_host_mode_label(ExtensionSdkBackend::Rpc), "Lite hosted + RPC");

    EXPECT_NE(an::ui::extensions::extension_host_mode_description(ExtensionSdkBackend::Runtime)
                  .find("AfterNightExtensionHost"),
              std::string::npos);
    EXPECT_NE(an::ui::extensions::extension_host_mode_description(ExtensionSdkBackend::Protocol)
                  .find("AfterNightExtensionHostLite"),
              std::string::npos);
    EXPECT_NE(an::ui::extensions::extension_host_mode_description(ExtensionSdkBackend::Rpc).find("AfterNightSdkHost"),
              std::string::npos);
}

// An RPC-backend extension where RPC is unavailable is refused by naming the Lite RPC mode.
TEST(ExtensionManagerTest, RpcBackendUnavailableDiagnosticNamesLiteRpcMode)
{
    ScopedTempDirectory temp_dir;
    const auto package_root = createExtensionPackage(temp_dir.path(), "my_rpc_extension",
                                                     R"json({
          "id": "my_rpc_extension",
          "name": "My RPC Extension",
          "version": "1.0.0",
          "type": "python",
          "entry_point": "rpc_ext",
          "process_class": "RpcExtension",
          "category": "filters",
          "launch_mode": "single_image",
          "sdk_backend": "rpc",
          "package_format_version": 1,
          "protocol_version": 1,
          "sdk_version": 1
        })json");
    writeTextFile(package_root / "rpc_ext.py", "class RpcExtension: pass\n");

    auto context = defaultContext();
    context.rpc_backend_available = false;

    const auto result = an::ui::extensions::load_extension_manifest_file(package_root / "extension.json",
                                                                         ExtensionPackageSource::User, context);

    ASSERT_TRUE(result.manifest.has_value());
    EXPECT_EQ(result.status, ExtensionLoadStatus::Incompatible);
    EXPECT_NE(result.diagnostic.find("rpc SDK backend"), std::string::npos);
    EXPECT_NE(result.diagnostic.find("Lite hosted + RPC"), std::string::npos);
    EXPECT_NE(result.diagnostic.find("AfterNightSdkHost"), std::string::npos);
}

// A protocol-backend package that imports the engine-backed runtime is refused: the Lite host carries no engine.
TEST(ExtensionManagerTest, ProtocolBackendPackageValidatorRejectsEngineBackedImports)
{
    ScopedTempDirectory temp_dir;
    const auto package_root = createExtensionPackage(temp_dir.path(), "my_lite_extension",
                                                     R"json({
          "id": "my_lite_extension",
          "name": "My Lite Extension",
          "version": "1.0.0",
          "type": "python",
          "entry_point": "lite",
          "process_class": "LiteExtension",
          "category": "filters",
          "launch_mode": "single_image",
          "sdk_backend": "protocol",
          "package_format_version": 1,
          "protocol_version": 1,
          "sdk_version": 1
        })json");
    writeTextFile(package_root / "lite.py", "import afternight.core\nclass LiteExtension: pass\n");

    an::ui::extensions::ExtensionPackageValidationOptions options;
    options.source = ExtensionPackageSource::User;
    options.context = defaultContext();
    options.context.protocol_backend_available = true;

    const auto validation = an::ui::extensions::validate_extension_package_root(package_root, options);

    EXPECT_FALSE(validation.valid);
    EXPECT_TRUE(
        hasValidationIssue(validation, "protocol_backend_engine_import", ExtensionPackageValidationSeverity::Error))
        << describeValidationIssues(validation.issues);
}

// A package under a licence other than the GPL that imports the runtime backend directly is refused.
TEST(ExtensionManagerTest, NonGplPackageValidatorRejectsDirectRuntimeBackendImport)
{
    ScopedTempDirectory temp_dir;
    const auto package_root = createExtensionPackage(temp_dir.path(), "mit_runtime_import_extension",
                                                     R"json({
          "id": "mit_runtime_import_extension",
          "name": "MIT Runtime Import Extension",
          "version": "1.0.0",
          "type": "python",
          "entry_point": "lite",
          "process_class": "LiteExtension",
          "category": "filters",
          "license": "MIT",
          "launch_mode": "single_image",
          "sdk_backend": "protocol",
          "package_format_version": 1,
          "protocol_version": 1,
          "sdk_version": 1
        })json");
    writeTextFile(package_root / "lite.py", "import _afternight_runtime\nclass LiteExtension: pass\n");

    an::ui::extensions::ExtensionPackageValidationOptions options;
    options.source = ExtensionPackageSource::User;
    options.context = defaultContext();

    const auto validation = an::ui::extensions::validate_extension_package_root(package_root, options);

    EXPECT_FALSE(validation.valid);
    EXPECT_TRUE(hasValidationIssue(validation, "non_gpl_engine_import", ExtensionPackageValidationSeverity::Error))
        << describeValidationIssues(validation.issues);
    EXPECT_TRUE(
        hasValidationIssue(validation, "protocol_backend_engine_import", ExtensionPackageValidationSeverity::Error))
        << describeValidationIssues(validation.issues);
}

// A runtime-backend package must be GPL-licensed; another licence is refused.
TEST(ExtensionManagerTest, RuntimeBackendPackageValidatorRejectsNonGplLicense)
{
    ScopedTempDirectory temp_dir;
    const auto package_root = createExtensionPackage(temp_dir.path(), "mit_runtime_extension",
                                                     R"json({
          "id": "mit_runtime_extension",
          "name": "MIT Runtime Extension",
          "version": "1.0.0",
          "type": "python",
          "entry_point": "runtime_ext",
          "process_class": "RuntimeExtension",
          "category": "filters",
          "license": "MIT",
          "launch_mode": "single_image",
          "sdk_backend": "runtime",
          "package_format_version": 1,
          "protocol_version": 1,
          "sdk_version": 1
        })json");
    writeTextFile(package_root / "runtime_ext.py", "class RuntimeExtension: pass\n");

    an::ui::extensions::ExtensionPackageValidationOptions options;
    options.source = ExtensionPackageSource::User;
    options.context = defaultContext();

    const auto validation = an::ui::extensions::validate_extension_package_root(package_root, options);

    EXPECT_FALSE(validation.valid);
    EXPECT_TRUE(
        hasValidationIssue(validation, "runtime_backend_non_gpl_license", ExtensionPackageValidationSeverity::Error))
        << describeValidationIssues(validation.issues);
}

// A GPL-licensed runtime-backend package is accepted.
TEST(ExtensionManagerTest, RuntimeBackendPackageValidatorAcceptsGplLicense)
{
    ScopedTempDirectory temp_dir;
    const auto package_root = createExtensionPackage(temp_dir.path(), "gpl_runtime_extension",
                                                     R"json({
          "id": "gpl_runtime_extension",
          "name": "GPL Runtime Extension",
          "version": "1.0.0",
          "type": "python",
          "entry_point": "runtime_ext",
          "process_class": "RuntimeExtension",
          "category": "filters",
          "license": "GPL-3.0-or-later",
          "launch_mode": "single_image",
          "sdk_backend": "runtime",
          "package_format_version": 1,
          "protocol_version": 1,
          "sdk_version": 1
        })json");
    writeTextFile(package_root / "runtime_ext.py", "class RuntimeExtension: pass\n");

    an::ui::extensions::ExtensionPackageValidationOptions options;
    options.source = ExtensionPackageSource::User;
    options.context = defaultContext();

    const auto validation = an::ui::extensions::validate_extension_package_root(package_root, options);

    EXPECT_TRUE(validation.valid) << describeValidationIssues(validation.issues);
}

// A protocol-backend package under another licence that imports nothing of the engine is accepted.
TEST(ExtensionManagerTest, ProtocolBackendPackageValidatorAcceptsNonGplLicenseWithoutEngineImports)
{
    ScopedTempDirectory temp_dir;
    const auto package_root = createExtensionPackage(temp_dir.path(), "mit_protocol_extension",
                                                     R"json({
          "id": "mit_protocol_extension",
          "name": "MIT Protocol Extension",
          "version": "1.0.0",
          "type": "python",
          "entry_point": "lite_ext",
          "process_class": "LiteExtension",
          "category": "filters",
          "license": "MIT",
          "launch_mode": "single_image",
          "sdk_backend": "protocol",
          "package_format_version": 1,
          "protocol_version": 1,
          "sdk_version": 1
        })json");
    writeTextFile(package_root / "lite_ext.py", "import afternight.ui_protocol as ui\n"
                                                "class LiteExtension: pass\n");

    an::ui::extensions::ExtensionPackageValidationOptions options;
    options.source = ExtensionPackageSource::User;
    options.context = defaultContext();

    const auto validation = an::ui::extensions::validate_extension_package_root(package_root, options);

    EXPECT_TRUE(validation.valid) << describeValidationIssues(validation.issues);
}

// A protocol-backend package that asks for capabilities only native controls provide is refused.
TEST(ExtensionManagerTest, ProtocolBackendPackageValidatorRejectsNativeControlOnlyCapabilities)
{
    ScopedTempDirectory temp_dir;
    const auto package_root = createExtensionPackage(temp_dir.path(), "native_protocol_extension",
                                                     R"json({
          "id": "native_protocol_extension",
          "name": "Native Protocol Extension",
          "version": "1.0.0",
          "type": "python",
          "entry_point": "lite_ext",
          "process_class": "LiteExtension",
          "category": "filters",
          "license": "MIT",
          "launch_mode": "single_image",
          "sdk_backend": "protocol",
          "capabilities": {
            "ui": "native"
          },
          "package_format_version": 1,
          "protocol_version": 1,
          "sdk_version": 1
        })json");
    writeTextFile(package_root / "lite_ext.py", "class LiteExtension: pass\n");

    an::ui::extensions::ExtensionPackageValidationOptions options;
    options.source = ExtensionPackageSource::User;
    options.context = defaultContext();

    const auto validation = an::ui::extensions::validate_extension_package_root(package_root, options);

    EXPECT_FALSE(validation.valid);
    EXPECT_TRUE(
        hasValidationIssue(validation, "protocol_backend_native_controls", ExtensionPackageValidationSeverity::Error))
        << describeValidationIssues(validation.issues);
}

// A protocol-backend package that imports the native interface module is refused.
TEST(ExtensionManagerTest, ProtocolBackendPackageValidatorRejectsNativeUiModuleImports)
{
    ScopedTempDirectory temp_dir;
    const auto package_root = createExtensionPackage(temp_dir.path(), "native_ui_import_protocol_extension",
                                                     R"json({
          "id": "native_ui_import_protocol_extension",
          "name": "Native UI Import Protocol Extension",
          "version": "1.0.0",
          "type": "python",
          "entry_point": "lite_ext",
          "process_class": "LiteExtension",
          "category": "filters",
          "license": "MIT",
          "launch_mode": "single_image",
          "sdk_backend": "protocol",
          "package_format_version": 1,
          "protocol_version": 1,
          "sdk_version": 1
        })json");
    writeTextFile(package_root / "lite_ext.py", "import afternight.ui as ui\nclass LiteExtension: pass\n");

    an::ui::extensions::ExtensionPackageValidationOptions options;
    options.source = ExtensionPackageSource::User;
    options.context = defaultContext();

    const auto validation = an::ui::extensions::validate_extension_package_root(package_root, options);

    EXPECT_FALSE(validation.valid);
    EXPECT_TRUE(
        hasValidationIssue(validation, "protocol_backend_native_ui_import", ExtensionPackageValidationSeverity::Error))
        << describeValidationIssues(validation.issues);
    EXPECT_TRUE(hasValidationIssue(validation, "non_gpl_native_ui_import", ExtensionPackageValidationSeverity::Error))
        << describeValidationIssues(validation.issues);
}

// A manifest that does not mark itself for development only is not.
TEST(ExtensionManagerTest, DevelopmentOnlyManifestMarkerDefaultsToFalse)
{
    ScopedTempDirectory temp_dir;
    const auto package_root = createExtensionPackage(temp_dir.path(), "my_invert",
                                                     R"json({
          "id": "my_invert",
          "name": "My Invert",
          "version": "1.0.0",
          "type": "python",
          "entry_point": "invert",
          "process_class": "InvertExtension",
          "category": "filters",
          "launch_mode": "single_image",
          "package_format_version": 1,
          "protocol_version": 1,
          "sdk_version": 1
        })json");

    const auto result = an::ui::extensions::load_extension_manifest_file(
        package_root / "extension.json", ExtensionPackageSource::User, defaultContext());

    ASSERT_TRUE(result.manifest.has_value());
    EXPECT_EQ(result.status, ExtensionLoadStatus::Ready);
    EXPECT_FALSE(result.manifest->development_only);
}

// A manifest that marks itself for development only is read as one.
TEST(ExtensionManagerTest, DevelopmentOnlyManifestMarkerIsParsed)
{
    ScopedTempDirectory temp_dir;
    const auto package_root = createExtensionPackage(temp_dir.path(), "dev_only",
                                                     R"json({
          "id": "dev_only",
          "name": "Development Only",
          "version": "1.0.0",
          "development_only": true,
          "type": "python",
          "entry_point": "dev_only",
          "process_class": "DevOnlyExtension",
          "category": "filters",
          "launch_mode": "single_image",
          "package_format_version": 1,
          "protocol_version": 1,
          "sdk_version": 1
        })json");

    const auto result = an::ui::extensions::load_extension_manifest_file(
        package_root / "extension.json", ExtensionPackageSource::User, defaultContext());

    ASSERT_TRUE(result.manifest.has_value());
    EXPECT_EQ(result.status, ExtensionLoadStatus::Ready);
    EXPECT_TRUE(result.manifest->development_only);
}

// Discovery leaves out development-only extensions unless it is asked for them.
TEST(ExtensionManagerTest, DiscoverySkipsDevelopmentOnlyManifestsByDefault)
{
    ScopedTempDirectory temp_dir;
    createExtensionPackage(temp_dir.path(), "regular",
                           R"json({
          "id": "regular",
          "name": "Regular",
          "version": "1.0.0",
          "type": "python",
          "entry_point": "regular",
          "process_class": "RegularExtension",
          "category": "filters",
          "launch_mode": "single_image",
          "package_format_version": 1,
          "protocol_version": 1,
          "sdk_version": 1
        })json");
    createExtensionPackage(temp_dir.path(), "dev_only",
                           R"json({
          "id": "dev_only",
          "name": "Development Only",
          "version": "1.0.0",
          "development_only": true,
          "type": "python",
          "entry_point": "dev_only",
          "process_class": "DevOnlyExtension",
          "category": "filters",
          "launch_mode": "single_image",
          "package_format_version": 1,
          "protocol_version": 1,
          "sdk_version": 1
        })json");

    ExtensionDiscoveryOptions options;
    options.user_roots = {temp_dir.path()};
    options.context = defaultContext();

    ExtensionManager manager;
    const auto discovered = manager.discoverExtensions(options);

    ASSERT_EQ(discovered.size(), 1u);
    ASSERT_TRUE(discovered.front().manifest.has_value());
    EXPECT_EQ(discovered.front().manifest->id, "regular");
}

// Discovery asked for development-only extensions finds them.
TEST(ExtensionManagerTest, DiscoveryCanIncludeDevelopmentOnlyManifests)
{
    ScopedTempDirectory temp_dir;
    createExtensionPackage(temp_dir.path(), "dev_only",
                           R"json({
          "id": "dev_only",
          "name": "Development Only",
          "version": "1.0.0",
          "development_only": true,
          "type": "python",
          "entry_point": "dev_only",
          "process_class": "DevOnlyExtension",
          "category": "filters",
          "launch_mode": "single_image",
          "package_format_version": 1,
          "protocol_version": 1,
          "sdk_version": 1
        })json");

    ExtensionDiscoveryOptions options;
    options.user_roots = {temp_dir.path()};
    options.context = defaultContext();
    options.include_development_only_extensions = true;

    ExtensionManager manager;
    const auto discovered = manager.discoverExtensions(options);

    ASSERT_EQ(discovered.size(), 1u);
    ASSERT_TRUE(discovered.front().manifest.has_value());
    EXPECT_EQ(discovered.front().manifest->id, "dev_only");
    EXPECT_TRUE(discovered.front().manifest->development_only);
}

// Discovery whose cancellation was requested loads no manifest.
TEST(ExtensionManagerTest, DiscoveryStopsBeforeLoadingManifestsWhenCancellationWasRequested)
{
    ScopedTempDirectory temp_dir;
    createExtensionPackage(temp_dir.path(), "cancelled",
                           R"json({
          "id": "cancelled",
          "name": "Cancelled",
          "version": "1.0.0",
          "type": "python",
          "entry_point": "cancelled",
          "process_class": "CancelledExtension",
          "category": "filters",
          "launch_mode": "single_image",
          "package_format_version": 1,
          "protocol_version": 1,
          "sdk_version": 1
        })json");

    ExtensionDiscoveryOptions options;
    options.user_roots = {temp_dir.path()};
    options.context = defaultContext();
    options.cancellation_token.requestCancellation();

    ExtensionManager manager;

    EXPECT_TRUE(manager.discoverExtensions(options).empty());
}

// An environment inspection whose cancellation was requested stops before reading the environment.
TEST(ExtensionManagerTest, EnvironmentInspectionStopsWhenCancellationWasRequested)
{
    ExtensionManifest manifest;
    manifest.id = "cancelled";
    manifest.version = "1.0.0";

    ExtensionOperationCancellationToken cancellation_token;
    cancellation_token.requestCancellation();

    ExtensionEnvironmentManager environment_manager;
    const auto snapshot = environment_manager.inspectEnvironment(manifest, ExtensionPackageSource::User,
                                                                 defaultContext(), cancellation_token);

    EXPECT_EQ(snapshot.state, ExtensionEnvironmentState::Missing);
    EXPECT_EQ(snapshot.diagnostic, "Extension environment inspection cancelled.");
}

// A package with repository metadata and a provenance receipt is accepted, both read.
TEST(ExtensionManagerTest, PackageValidatorAcceptsRepositoryMetadataAndProvenanceReceipt)
{
    ScopedTempDirectory temp_dir;
    const std::string archive_hash(64, 'a');
    const auto package_root = createExtensionPackage(temp_dir.path(), "repository_ready",
                                                     R"json({
          "id": "repository_ready",
          "name": "Repository Ready",
          "version": "1.0.0",
          "summary": "Ready for local feed validation.",
          "description": "A package-format validator fixture.",
          "author": "AfterNight Tests",
          "license": "GPL-3.0-or-later",
          "publisher_id": "afternight.tests",
          "homepage_url": "https://example.com/home",
          "repository_url": "https://example.com/repo",
          "support_url": "https://example.com/support",
          "icon": "assets/icon.png",
          "screenshots": ["assets/screen.png"],
          "tags": ["fixture", "validator"],
          "archive_hash": ")json" + archive_hash + R"json(",
          "signature_state": "unsigned",
          "type": "python",
          "entry_point": "repository_ready",
          "process_class": "RepositoryReadyExtension",
          "category": "filters",
          "launch_mode": "single_image",
          "package_format_version": 1,
          "protocol_version": 1,
          "sdk_version": 1
        })json");
    writeTextFile(package_root / "repository_ready.py", "class RepositoryReadyExtension: pass\n");
    writeTextFile(package_root / "assets" / "icon.png", "icon\n");
    writeTextFile(package_root / "assets" / "screen.png", "screen\n");
    writeTextFile(package_root / "extension_package_receipt.json",
                  "{\n"
                  "  \"receipt_version\": 1,\n"
                  "  \"extension_id\": \"repository_ready\",\n"
                  "  \"extension_version\": \"1.0.0\",\n"
                  "  \"source_tier\": \"local_package\",\n"
                  "  \"source_repository_url\": \"https://example.com/repo\",\n"
                  "  \"local_package_path\": \"" +
                      package_root.generic_string() +
                      "\",\n"
                      "  \"installed_at_utc\": \"2026-04-26T00:00:00Z\",\n"
                      "  \"archive_hash\": \"" +
                      archive_hash +
                      "\",\n"
                      "  \"selected_release_asset\": \"repository_ready-1.0.0.zip\",\n"
                      "  \"signature_state\": \"unsigned\",\n"
                      "  \"signature_detail\": \"initial unsigned local fixture\",\n"
                      "  \"host_bootstrap_fingerprint\": \"host-bootstrap-test\"\n"
                      "}\n");

    an::ui::extensions::ExtensionPackageValidationOptions options;
    options.context = defaultContext();
    options.source = ExtensionPackageSource::LocalPackage;
    options.require_repository_metadata = true;
    options.require_archive_hash = true;

    const auto validation = an::ui::extensions::validate_extension_package_root(package_root, options);

    EXPECT_TRUE(validation.valid);
    EXPECT_TRUE(validation.issues.empty());
    ASSERT_TRUE(validation.load_result.manifest.has_value());
    EXPECT_EQ(validation.load_result.manifest->repository_metadata.summary, "Ready for local feed validation.");
    EXPECT_EQ(validation.load_result.manifest->repository_metadata.publisher_id, "afternight.tests");
    EXPECT_EQ(validation.load_result.provenance.source, ExtensionPackageSource::LocalPackage);
    EXPECT_EQ(validation.load_result.provenance.archive_hash_sha256, archive_hash);
    EXPECT_EQ(validation.load_result.provenance.signature_state, ExtensionPackageSignatureState::Unsigned);
}

// A package declaring runtime dependencies and binary artifacts with their targets is accepted.
TEST(ExtensionManagerTest, PackageValidatorAcceptsRuntimeDependencyAndBinaryMetadata)
{
    ScopedTempDirectory temp_dir;
    const auto package_root = createExtensionPackage(temp_dir.path(), "runtime_dependency_metadata",
                                                     R"json({
          "id": "runtime_dependency_metadata",
          "name": "Runtime Dependency Metadata",
          "version": "1.0.0",
          "type": "python",
          "entry_point": "runtime_dependency_metadata",
          "process_class": "RuntimeDependencyMetadataExtension",
          "category": "filters",
          "launch_mode": "single_image",
          "package_format_version": 1,
          "protocol_version": 1,
          "sdk_version": 1,
          "runtime_targets": [")json" + currentRuntimeTarget() +
                                                         R"json("],
          "binary_vendor_paths": ["bin"],
          "dependencies": {
            "requirements_file": "requirements.lock",
            "dependency_context": "shared_group",
            "shared_group": "metadata_suite",
            "bundled_runtime_packages": ["numpy", "scipy.linalg"],
            "pip": {
              "require_hashes": true,
              "no_deps": true,
              "find_links": ["wheelhouse"],
              "index_urls": ["https://example.invalid/simple"],
              "extra_index_urls": ["https://mirror.example.invalid/simple"]
            }
          }
        })json");
    writeTextFile(package_root / "runtime_dependency_metadata.py", "class RuntimeDependencyMetadataExtension: pass\n");
    writeTextFile(package_root / "requirements.lock",
                  "demo-pkg==1.0.0 --hash=sha256:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\n");
    writeTextFile(package_root / "wheelhouse" / ".keep", "");
    writeTextFile(package_root / "bin" / "helper.bin", "helper\n");
    writeTextFile(package_root / "bin" / "helper.so", "sidecar\n");

    an::ui::extensions::ExtensionPackageValidationOptions options;
    options.context = defaultContext();
    options.source = ExtensionPackageSource::LocalPackage;
    options.require_current_runtime_compatibility = true;

    const auto validation = an::ui::extensions::validate_extension_package_root(package_root, options);

    ASSERT_TRUE(validation.valid) << describeValidationIssues(validation.issues);
    ASSERT_TRUE(validation.load_result.manifest.has_value());
    const auto& manifest = *validation.load_result.manifest;
    EXPECT_EQ(manifest.runtime_targets, (std::vector<std::string>{currentRuntimeTarget()}));
    EXPECT_EQ(manifest.binary_vendor_paths, (std::vector<std::string>{"bin"}));
    EXPECT_EQ(manifest.dependencies.dependency_context, an::ui::extensions::ExtensionDependencyContext::SharedGroup);
    EXPECT_EQ(manifest.dependencies.shared_group, "metadata_suite");
    EXPECT_EQ(manifest.dependencies.requirements_file, "requirements.lock");
    EXPECT_TRUE(manifest.dependencies.pip.require_hashes);
    EXPECT_TRUE(manifest.dependencies.pip.no_deps);
    EXPECT_EQ(manifest.dependencies.pip.find_links, (std::vector<std::string>{"wheelhouse"}));
    EXPECT_EQ(manifest.dependencies.pip.index_urls, (std::vector<std::string>{"https://example.invalid/simple"}));
    EXPECT_EQ(manifest.dependencies.pip.extra_index_urls,
              (std::vector<std::string>{"https://mirror.example.invalid/simple"}));
    EXPECT_EQ(manifest.dependencies.bundled_runtime_packages, (std::vector<std::string>{"numpy", "scipy.linalg"}));
}

// The manifest keeps its nested marketplace metadata as written.
TEST(ExtensionManagerTest, ManifestParserPreservesNestedMarketplaceMetadata)
{
    ScopedTempDirectory temp_dir;
    const auto package_root = createExtensionPackage(temp_dir.path(), "marketplace_metadata",
                                                     R"json({
          "id": "marketplace_metadata",
          "name": "Marketplace Metadata",
          "version": "1.0.0",
          "summary": "Top-level package summary.",
          "description": "Top-level package description.",
          "author": "AfterNight Tests",
          "license": "MIT",
          "attribution": "AfterNight port of Marketplace Metadata, originally authored by Fixture Author.",
          "original_author": "Fixture Author",
          "original_project": "Fixture Suite",
          "original_source_url": "https://example.com/upstream/marketplace-metadata.py",
          "upstream_commit": "0123456789abcdef",
          "type": "python",
          "entry_point": "marketplace_metadata",
          "process_class": "MarketplaceMetadataExtension",
          "category": "filters",
          "launch_mode": "single_image",
          "sdk_backend": "protocol",
          "package_format_version": 1,
          "protocol_version": 1,
          "sdk_version": 1,
          "marketplace": {
            "publisher_id": "afternight.marketplace",
            "homepage": "https://example.com/marketplace-home",
            "repository_url": "https://example.com/marketplace-repo",
            "support_url": "https://example.com/marketplace-support",
            "icon": "assets/marketplace-icon.png",
            "screenshots": ["assets/marketplace-screen.png"],
            "tags": ["marketplace", "metadata"],
            "signature": null
          }
        })json");
    writeTextFile(package_root / "marketplace_metadata.py", "class MarketplaceMetadataExtension: pass\n");
    writeTextFile(package_root / "assets" / "marketplace-icon.png", "icon\n");
    writeTextFile(package_root / "assets" / "marketplace-screen.png", "screen\n");

    an::ui::extensions::ExtensionPackageValidationOptions options;
    options.context = defaultContext();
    options.source = ExtensionPackageSource::Repository;
    options.require_repository_metadata = true;

    const auto validation = an::ui::extensions::validate_extension_package_root(package_root, options);

    ASSERT_TRUE(validation.valid) << describeValidationIssues(validation.issues);
    ASSERT_TRUE(validation.load_result.manifest.has_value());
    const auto& metadata = validation.load_result.manifest->repository_metadata;
    EXPECT_EQ(metadata.summary, "Top-level package summary.");
    EXPECT_EQ(metadata.description, "Top-level package description.");
    EXPECT_EQ(metadata.author, "AfterNight Tests");
    EXPECT_EQ(metadata.license, "MIT");
    EXPECT_EQ(metadata.attribution, "AfterNight port of Marketplace Metadata, originally authored by Fixture Author.");
    EXPECT_EQ(metadata.original_author, "Fixture Author");
    EXPECT_EQ(metadata.original_project, "Fixture Suite");
    EXPECT_EQ(metadata.original_source_url, "https://example.com/upstream/marketplace-metadata.py");
    EXPECT_EQ(metadata.upstream_commit, "0123456789abcdef");
    EXPECT_EQ(metadata.publisher_id, "afternight.marketplace");
    EXPECT_EQ(metadata.homepage_url, "https://example.com/marketplace-home");
    EXPECT_EQ(metadata.repository_url, "https://example.com/marketplace-repo");
    EXPECT_EQ(metadata.support_url, "https://example.com/marketplace-support");
    EXPECT_EQ(metadata.icon_path, "assets/marketplace-icon.png");
    ASSERT_EQ(metadata.screenshots.size(), 1u);
    EXPECT_EQ(metadata.screenshots.front(), "assets/marketplace-screen.png");
    EXPECT_EQ(metadata.tags, (std::vector<std::string>{"marketplace", "metadata"}));
}

// A runtime target that is not one the format names is refused.
TEST(ExtensionManagerTest, PackageValidatorRejectsInvalidRuntimeTarget)
{
    ScopedTempDirectory temp_dir;
    const auto package_root = createExtensionPackage(temp_dir.path(), "invalid_runtime_target",
                                                     R"json({
          "id": "invalid_runtime_target",
          "name": "Invalid Runtime Target",
          "version": "1.0.0",
          "type": "python",
          "entry_point": "invalid_runtime_target",
          "process_class": "InvalidRuntimeTargetExtension",
          "category": "filters",
          "launch_mode": "single_image",
          "package_format_version": 1,
          "protocol_version": 1,
          "sdk_version": 1,
          "runtime_targets": ["linux-gcc-x86_64"]
        })json");
    writeTextFile(package_root / "invalid_runtime_target.py", "class InvalidRuntimeTargetExtension: pass\n");

    an::ui::extensions::ExtensionPackageValidationOptions options;
    options.context = defaultContext();
    options.source = ExtensionPackageSource::LocalPackage;

    const auto validation = an::ui::extensions::validate_extension_package_root(package_root, options);

    EXPECT_FALSE(validation.valid);
    EXPECT_TRUE(hasValidationIssue(validation, "invalid_manifest", ExtensionPackageValidationSeverity::Error));
    EXPECT_NE(validation.load_result.diagnostic.find("runtime_targets"), std::string::npos);
}

// A package for runtime targets none of which is this one is refused.
TEST(ExtensionManagerTest, PackageValidatorRejectsUnsupportedRuntimeTarget)
{
    ScopedTempDirectory temp_dir;
    const auto package_root = createExtensionPackage(temp_dir.path(), "unsupported_runtime_target",
                                                     R"json({
          "id": "unsupported_runtime_target",
          "name": "Unsupported Runtime Target",
          "version": "1.0.0",
          "type": "python",
          "entry_point": "unsupported_runtime_target",
          "process_class": "UnsupportedRuntimeTargetExtension",
          "category": "filters",
          "launch_mode": "single_image",
          "package_format_version": 1,
          "protocol_version": 1,
          "sdk_version": 1,
          "runtime_targets": ["unsupported-runtime-x86_64"]
        })json");
    writeTextFile(package_root / "unsupported_runtime_target.py", "class UnsupportedRuntimeTargetExtension: pass\n");

    an::ui::extensions::ExtensionPackageValidationOptions options;
    options.context = defaultContext();
    options.source = ExtensionPackageSource::LocalPackage;

    const auto validation = an::ui::extensions::validate_extension_package_root(package_root, options);

    EXPECT_FALSE(validation.valid);
    EXPECT_TRUE(hasValidationIssue(validation, "invalid_manifest", ExtensionPackageValidationSeverity::Error));
    EXPECT_NE(validation.load_result.diagnostic.find("runtime_targets"), std::string::npos);
}

// A package whose entry point module is missing is refused.
TEST(ExtensionManagerTest, PackageValidatorRejectsMissingEntryPointModule)
{
    ScopedTempDirectory temp_dir;
    const auto package_root = createExtensionPackage(temp_dir.path(), "missing_entry",
                                                     R"json({
          "id": "missing_entry",
          "name": "Missing Entry",
          "version": "1.0.0",
          "type": "python",
          "entry_point": "missing_entry",
          "process_class": "MissingEntryExtension",
          "category": "filters",
          "launch_mode": "single_image",
          "package_format_version": 1,
          "protocol_version": 1,
          "sdk_version": 1
        })json");

    an::ui::extensions::ExtensionPackageValidationOptions options;
    options.context = defaultContext();
    options.source = ExtensionPackageSource::LocalPackage;

    const auto validation = an::ui::extensions::validate_extension_package_root(package_root, options);

    EXPECT_FALSE(validation.valid);
    EXPECT_TRUE(hasValidationIssue(validation, "missing_entry_point", ExtensionPackageValidationSeverity::Error));
}

// A manifest that is not valid JSON is refused.
TEST(ExtensionManagerTest, PackageValidatorRejectsMalformedManifestJson)
{
    ScopedTempDirectory temp_dir;
    const auto package_root = temp_dir.path() / "malformed";
    std::filesystem::create_directories(package_root);
    writeTextFile(package_root / "extension.json", "{ this is not json\n");

    an::ui::extensions::ExtensionPackageValidationOptions options;
    options.context = defaultContext();
    options.source = ExtensionPackageSource::LocalPackage;

    const auto validation = an::ui::extensions::validate_extension_package_root(package_root, options);

    EXPECT_FALSE(validation.valid);
    EXPECT_TRUE(hasValidationIssue(validation, "invalid_manifest", ExtensionPackageValidationSeverity::Error));
    EXPECT_NE(validation.load_result.diagnostic.find("valid JSON"), std::string::npos);
}

// An extension identity that could name a path outside its package is refused.
TEST(ExtensionManagerTest, PackageValidatorRejectsUnsafeExtensionId)
{
    ScopedTempDirectory temp_dir;
    const auto package_root = createExtensionPackage(temp_dir.path(), "unsafe_id",
                                                     R"json({
          "id": "../unsafe",
          "name": "Unsafe ID",
          "version": "1.0.0",
          "type": "python",
          "entry_point": "unsafe_id",
          "process_class": "UnsafeIdExtension",
          "category": "filters",
          "launch_mode": "single_image",
          "package_format_version": 1,
          "protocol_version": 1,
          "sdk_version": 1
        })json");
    writeTextFile(package_root / "unsafe_id.py", "class UnsafeIdExtension: pass\n");

    an::ui::extensions::ExtensionPackageValidationOptions options;
    options.context = defaultContext();
    options.source = ExtensionPackageSource::LocalPackage;

    const auto validation = an::ui::extensions::validate_extension_package_root(package_root, options);

    EXPECT_FALSE(validation.valid);
    EXPECT_TRUE(hasValidationIssue(validation, "invalid_manifest", ExtensionPackageValidationSeverity::Error));
    EXPECT_NE(validation.load_result.diagnostic.find("id"), std::string::npos);
}

// A package-relative path that leaves the package is refused.
TEST(ExtensionManagerTest, PackageValidatorRejectsUnsafePackageRelativePaths)
{
    ScopedTempDirectory temp_dir;
    const auto package_root = createExtensionPackage(temp_dir.path(), "unsafe_path",
                                                     R"json({
          "id": "unsafe_path",
          "name": "Unsafe Path",
          "version": "1.0.0",
          "summary": "Unsafe path fixture.",
          "author": "AfterNight Tests",
          "license": "MIT",
          "publisher_id": "afternight.tests",
          "icon": "../icon.png",
          "type": "python",
          "entry_point": "unsafe_path",
          "process_class": "UnsafePathExtension",
          "category": "filters",
          "launch_mode": "single_image",
          "package_format_version": 1,
          "protocol_version": 1,
          "sdk_version": 1
        })json");
    writeTextFile(package_root / "unsafe_path.py", "class UnsafePathExtension: pass\n");

    an::ui::extensions::ExtensionPackageValidationOptions options;
    options.context = defaultContext();
    options.source = ExtensionPackageSource::LocalPackage;

    const auto validation = an::ui::extensions::validate_extension_package_root(package_root, options);

    EXPECT_FALSE(validation.valid);
    EXPECT_TRUE(hasValidationIssue(validation, "invalid_manifest", ExtensionPackageValidationSeverity::Error));
    EXPECT_NE(validation.load_result.diagnostic.find("icon"), std::string::npos);
}

// A package that declares an artifact it does not carry is refused.
TEST(ExtensionManagerTest, PackageValidatorRejectsMissingDeclaredArtifacts)
{
    ScopedTempDirectory temp_dir;
    const auto package_root = createExtensionPackage(temp_dir.path(), "missing_artifact",
                                                     R"json({
          "id": "missing_artifact",
          "name": "Missing Artifact",
          "version": "1.0.0",
          "type": "python",
          "entry_point": "missing_artifact",
          "process_class": "MissingArtifactExtension",
          "category": "filters",
          "launch_mode": "single_image",
          "package_format_version": 1,
          "protocol_version": 1,
          "sdk_version": 1,
          "runtime_targets": [")json" + currentRuntimeTarget() +
                                                         R"json("],
          "binary_vendor_paths": ["vendor/bin"]
        })json");
    writeTextFile(package_root / "missing_artifact.py", "class MissingArtifactExtension: pass\n");

    an::ui::extensions::ExtensionPackageValidationOptions options;
    options.context = defaultContext();
    options.source = ExtensionPackageSource::LocalPackage;

    const auto validation = an::ui::extensions::validate_extension_package_root(package_root, options);

    EXPECT_FALSE(validation.valid);
    EXPECT_TRUE(hasValidationIssue(validation, "invalid_manifest", ExtensionPackageValidationSeverity::Error));
    EXPECT_NE(validation.load_result.diagnostic.find("binary_vendor_paths"), std::string::npos);
}

// A binary vendor path that names a file rather than a directory is refused.
TEST(ExtensionManagerTest, PackageValidatorRejectsBinaryVendorFilePath)
{
    ScopedTempDirectory temp_dir;
    const auto package_root = createExtensionPackage(temp_dir.path(), "binary_vendor_file",
                                                     R"json({
          "id": "binary_vendor_file",
          "name": "Binary Vendor File",
          "version": "1.0.0",
          "type": "python",
          "entry_point": "binary_vendor_file",
          "process_class": "BinaryVendorFileExtension",
          "category": "filters",
          "launch_mode": "single_image",
          "package_format_version": 1,
          "protocol_version": 1,
          "sdk_version": 1,
          "runtime_targets": [")json" + currentRuntimeTarget() +
                                                         R"json("],
          "binary_vendor_paths": ["bin/helper.bin"]
        })json");
    writeTextFile(package_root / "binary_vendor_file.py", "class BinaryVendorFileExtension: pass\n");
    writeTextFile(package_root / "bin" / "helper.bin", "helper\n");

    an::ui::extensions::ExtensionPackageValidationOptions options;
    options.context = defaultContext();
    options.source = ExtensionPackageSource::LocalPackage;

    const auto validation = an::ui::extensions::validate_extension_package_root(package_root, options);

    EXPECT_FALSE(validation.valid);
    EXPECT_TRUE(hasValidationIssue(validation, "invalid_manifest", ExtensionPackageValidationSeverity::Error));
    EXPECT_NE(validation.load_result.diagnostic.find("binary_vendor_paths"), std::string::npos);
    EXPECT_NE(validation.load_result.diagnostic.find("directory"), std::string::npos);
}

// A shared library outside the declared binary vendor paths is refused.
TEST(ExtensionManagerTest, PackageValidatorRejectsUnsupportedSharedLibrariesOutsideVendorPaths)
{
    ScopedTempDirectory temp_dir;
    const auto package_root = createExtensionPackage(temp_dir.path(), "loose_validator_native",
                                                     R"json({
          "id": "loose_validator_native",
          "name": "Loose Validator Native",
          "version": "1.0.0",
          "type": "python",
          "entry_point": "loose_validator_native",
          "process_class": "LooseValidatorNativeExtension",
          "category": "filters",
          "launch_mode": "single_image",
          "package_format_version": 1,
          "protocol_version": 1,
          "sdk_version": 1
        })json");
    writeTextFile(package_root / "loose_validator_native.py", "class LooseValidatorNativeExtension: pass\n");
    writeTextFile(package_root / "native_module.so", "stub");

    an::ui::extensions::ExtensionPackageValidationOptions options;
    options.context = defaultContext();
    options.source = ExtensionPackageSource::LocalPackage;

    const auto validation = an::ui::extensions::validate_extension_package_root(package_root, options);

    EXPECT_FALSE(validation.valid);
    EXPECT_TRUE(hasValidationIssue(validation, "invalid_manifest", ExtensionPackageValidationSeverity::Error));
    EXPECT_NE(validation.load_result.diagnostic.find("shared librar"), std::string::npos);
}

// Dependency metadata that contradicts itself is refused.
TEST(ExtensionManagerTest, PackageValidatorRejectsInconsistentDependencyMetadata)
{
    ScopedTempDirectory temp_dir;
    const auto package_root = createExtensionPackage(temp_dir.path(), "bad_dependencies",
                                                     R"json({
          "id": "bad_dependencies",
          "name": "Bad Dependencies",
          "version": "1.0.0",
          "type": "python",
          "entry_point": "bad_dependencies",
          "process_class": "BadDependenciesExtension",
          "category": "filters",
          "launch_mode": "single_image",
          "package_format_version": 1,
          "protocol_version": 1,
          "sdk_version": 1,
          "dependencies": {
            "dependency_context": "private",
            "shared_group": "afterlight-core"
          }
        })json");
    writeTextFile(package_root / "bad_dependencies.py", "class BadDependenciesExtension: pass\n");

    an::ui::extensions::ExtensionPackageValidationOptions options;
    options.context = defaultContext();
    options.source = ExtensionPackageSource::LocalPackage;

    const auto validation = an::ui::extensions::validate_extension_package_root(package_root, options);

    EXPECT_FALSE(validation.valid);
    EXPECT_TRUE(hasValidationIssue(validation, "invalid_manifest", ExtensionPackageValidationSeverity::Error));
    EXPECT_NE(validation.load_result.diagnostic.find("shared_group"), std::string::npos);
}

// Malformed pip options in the dependency metadata are refused.
TEST(ExtensionManagerTest, PackageValidatorRejectsMalformedDependencyPipMetadata)
{
    ScopedTempDirectory temp_dir;
    const auto package_root = createExtensionPackage(temp_dir.path(), "malformed_pip_metadata",
                                                     R"json({
          "id": "malformed_pip_metadata",
          "name": "Malformed Pip Metadata",
          "version": "1.0.0",
          "type": "python",
          "entry_point": "malformed_pip_metadata",
          "process_class": "MalformedPipMetadataExtension",
          "category": "filters",
          "launch_mode": "single_image",
          "package_format_version": 1,
          "protocol_version": 1,
          "sdk_version": 1,
          "dependencies": {
            "requirements_file": "requirements.lock",
            "pip": ["not", "an", "object"]
          }
        })json");
    writeTextFile(package_root / "malformed_pip_metadata.py", "class MalformedPipMetadataExtension: pass\n");
    writeTextFile(package_root / "requirements.lock",
                  "demo-pkg==1.0.0 --hash=sha256:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\n");

    an::ui::extensions::ExtensionPackageValidationOptions options;
    options.context = defaultContext();
    options.source = ExtensionPackageSource::LocalPackage;

    const auto validation = an::ui::extensions::validate_extension_package_root(package_root, options);

    EXPECT_FALSE(validation.valid);
    EXPECT_TRUE(hasValidationIssue(validation, "invalid_manifest", ExtensionPackageValidationSeverity::Error));
    EXPECT_NE(validation.load_result.diagnostic.find("dependencies.pip"), std::string::npos);
    EXPECT_NE(validation.load_result.diagnostic.find("object"), std::string::npos);
}

// A multi-process declaration with a broken process entry is refused.
TEST(ExtensionManagerTest, PackageValidatorRejectsBrokenMultiProcessDeclarations)
{
    ScopedTempDirectory temp_dir;
    const auto package_root = createExtensionPackage(temp_dir.path(), "broken_processes",
                                                     R"json({
          "id": "broken_processes",
          "name": "Broken Processes",
          "version": "1.0.0",
          "type": "python",
          "entry_point": "broken_processes",
          "category": "filters",
          "launch_mode": "single_image",
          "package_format_version": 1,
          "protocol_version": 1,
          "sdk_version": 1,
          "processes": [
            {
              "id_suffix": "../escape",
              "class": "BrokenProcessExtension",
              "display_name": "Broken Process"
            }
          ]
        })json");
    writeTextFile(package_root / "broken_processes.py", "class BrokenProcessExtension: pass\n");

    an::ui::extensions::ExtensionPackageValidationOptions options;
    options.context = defaultContext();
    options.source = ExtensionPackageSource::LocalPackage;

    const auto validation = an::ui::extensions::validate_extension_package_root(package_root, options);

    EXPECT_FALSE(validation.valid);
    EXPECT_TRUE(hasValidationIssue(validation, "invalid_manifest", ExtensionPackageValidationSeverity::Error));
    EXPECT_NE(validation.load_result.diagnostic.find("id_suffix"), std::string::npos);
}

// Every extension the application bundles passes the package validator.
TEST(ExtensionManagerTest, PackageValidatorAcceptsBundledReferenceExtensions)
{
    const auto extensions_root = findRepositoryBundledExtensionsRoot();
    ASSERT_FALSE(extensions_root.empty()) << "Failed to locate repository extensions/ root";

    an::ui::extensions::ExtensionPackageValidationOptions options;
    options.context = defaultContext();
    options.source = ExtensionPackageSource::Bundled;

    for (const auto& entry : std::filesystem::directory_iterator(extensions_root))
    {
        if (!entry.is_directory())
        {
            continue;
        }

        const auto manifest_path = entry.path() / "extension.json";
        if (!std::filesystem::exists(manifest_path))
        {
            continue;
        }

        const auto validation = an::ui::extensions::validate_extension_package_root(entry.path(), options);
        EXPECT_TRUE(validation.valid) << entry.path() << ": "
                                      << (validation.issues.empty() ? std::string()
                                                                    : validation.issues.front().message);
    }
}

// Every example in the extension authors' documentation passes the package validator.
TEST(ExtensionManagerTest, PackageValidatorAcceptsAuthorDocumentationExamples)
{
    const auto examples_root = findRepositoryExtensionExamplesRoot();
    ASSERT_FALSE(examples_root.empty()) << "Failed to locate docs/extension-examples root";

    an::ui::extensions::ExtensionPackageValidationOptions options;
    options.context = defaultContext();
    options.source = ExtensionPackageSource::LocalPackage;
    options.require_repository_metadata = true;

    std::vector<std::filesystem::path> package_roots;
    for (const auto& entry : std::filesystem::directory_iterator(examples_root))
    {
        if (!entry.is_directory())
        {
            continue;
        }

        const auto manifest_path = entry.path() / "extension.json";
        if (!std::filesystem::exists(manifest_path))
        {
            continue;
        }

        package_roots.push_back(entry.path());
    }
    std::sort(package_roots.begin(), package_roots.end());

    ASSERT_GE(package_roots.size(), 6u) << "Expected the shipped author templates under " << examples_root;

    for (const auto& package_root : package_roots)
    {
        const auto validation = an::ui::extensions::validate_extension_package_root(package_root, options);
        EXPECT_TRUE(validation.valid) << package_root << " failed package validation:\n"
                                      << describeValidationIssues(validation.issues)
                                      << validation.load_result.diagnostic;
    }
}

// Every package of an external extension repository, where one is named, passes the package validator.
TEST(ExtensionManagerTest, ExternalExtensionRepositoryPassesAppSidePackageValidator)
{
    const std::string repository_root_env = environmentVariableValue("AFTERNIGHT_EXTENSION_REPOSITORY_ROOT");
    if (repository_root_env.empty())
    {
        GTEST_SKIP() << "Set AFTERNIGHT_EXTENSION_REPOSITORY_ROOT to validate an external extension repository";
    }

    const std::filesystem::path repository_root(repository_root_env);
    const std::filesystem::path packages_root = repository_root / "packages";
    const std::filesystem::path index_path = repository_root / "index.json";

    std::error_code ec;
    ASSERT_TRUE(std::filesystem::is_directory(repository_root, ec))
        << "Expected extension repository root at " << repository_root;
    ASSERT_TRUE(std::filesystem::is_directory(packages_root, ec))
        << "Expected extension package directory at " << packages_root;
    ASSERT_TRUE(std::filesystem::is_regular_file(index_path, ec))
        << "Expected extension repository index at " << index_path;

    an::ui::extensions::ExtensionPackageValidationOptions options;
    options.context = defaultContext();
    options.source = ExtensionPackageSource::Repository;
    options.require_repository_metadata = true;

    std::set<std::string> source_package_ids;
    std::size_t validated_package_count = 0;
    for (const auto& entry : std::filesystem::directory_iterator(packages_root))
    {
        if (!entry.is_directory())
        {
            continue;
        }

        const auto package_root = entry.path() / "package";
        if (!std::filesystem::is_regular_file(package_root / "extension.json", ec))
        {
            continue;
        }

        const auto validation = an::ui::extensions::validate_extension_package_root(package_root, options);
        EXPECT_TRUE(validation.valid) << package_root << " failed app-side package validation:\n"
                                      << describeValidationIssues(validation.issues)
                                      << validation.load_result.diagnostic;
        ASSERT_TRUE(validation.load_result.manifest.has_value()) << package_root << " did not produce a manifest";
        source_package_ids.insert(validation.load_result.manifest->id);
        ++validated_package_count;
    }

    ASSERT_GT(validated_package_count, 0u) << "Expected at least one package source under " << packages_root;

    const auto index_validation = an::ui::extensions::validate_extension_repository_index_file(index_path);
    ASSERT_TRUE(index_validation.valid) << index_path << " failed app-side index validation:\n"
                                        << describeValidationIssues(index_validation.issues)
                                        << index_validation.diagnostic;

    for (const auto& package : index_validation.index.packages)
    {
        EXPECT_TRUE(source_package_ids.contains(package.id))
            << "index.json contains package '" << package.id << "' but no matching source package was validated";
    }
}

// An archive entry that climbs out of the destination is refused and nothing is extracted.
TEST(ExtensionManagerTest, SafePackageArchiveExtractionRejectsPathTraversal)
{
    ScopedTempDirectory temp_dir;
    const auto archive_path = temp_dir.path() / "unsafe.tar";
    createTarArchive(archive_path, {{"../evil.txt", "nope\n", '0'}});

    const auto result =
        an::ui::extensions::extract_extension_package_archive(archive_path, temp_dir.path() / "extract");

    EXPECT_FALSE(result.success);
    EXPECT_NE(result.diagnostic.find("escape"), std::string::npos);
    EXPECT_FALSE(std::filesystem::exists(temp_dir.path() / "evil.txt"));
}

// An archive entry that is a link is refused.
TEST(ExtensionManagerTest, SafePackageArchiveExtractionRejectsLinks)
{
    ScopedTempDirectory temp_dir;
    const auto archive_path = temp_dir.path() / "link.tar";
    createTarArchive(archive_path, {{"package/link", "target", '2'}});

    const auto result =
        an::ui::extensions::extract_extension_package_archive(archive_path, temp_dir.path() / "extract");

    EXPECT_FALSE(result.success);
    EXPECT_NE(result.diagnostic.find("link"), std::string::npos);
}

// An archive entry that is a device file is refused.
TEST(ExtensionManagerTest, SafePackageArchiveExtractionRejectsDeviceFiles)
{
    ScopedTempDirectory temp_dir;
    const auto archive_path = temp_dir.path() / "device.tar";
    createTarArchive(archive_path, {{"package/device", "", '3'}});

    const auto result =
        an::ui::extensions::extract_extension_package_archive(archive_path, temp_dir.path() / "extract");

    EXPECT_FALSE(result.success);
    EXPECT_NE(result.diagnostic.find("device"), std::string::npos);
}

// An archive entry with an absolute path is refused.
TEST(ExtensionManagerTest, SafePackageArchiveExtractionRejectsAbsolutePaths)
{
    ScopedTempDirectory temp_dir;
    const auto archive_path = temp_dir.path() / "absolute.tar";
    const auto escaped_path = temp_dir.path() / "absolute_evil.txt";
    createTarArchive(archive_path, {{escaped_path.generic_string(), "nope\n", '0'}});

    const auto result =
        an::ui::extensions::extract_extension_package_archive(archive_path, temp_dir.path() / "extract");

    EXPECT_FALSE(result.success);
    EXPECT_NE(result.diagnostic.find("relative"), std::string::npos);
    EXPECT_FALSE(std::filesystem::exists(escaped_path));
}

// An archive entry nested deeper than the limit is refused.
TEST(ExtensionManagerTest, SafePackageArchiveExtractionRejectsExcessiveDepth)
{
    ScopedTempDirectory temp_dir;
    const auto archive_path = temp_dir.path() / "deep.tar";
    createTarArchive(archive_path, {{"package/a/b/c/file.txt", "too deep\n", '0'}});

    an::ui::extensions::ExtensionPackageExtractionOptions options;
    options.limits.max_path_depth = 4;
    const auto result =
        an::ui::extensions::extract_extension_package_archive(archive_path, temp_dir.path() / "extract", options);

    EXPECT_FALSE(result.success);
    EXPECT_NE(result.diagnostic.find("depth"), std::string::npos);
    EXPECT_FALSE(std::filesystem::exists(temp_dir.path() / "extract"));
}

// An extraction into a destination that already holds files is refused.
TEST(ExtensionManagerTest, SafePackageArchiveExtractionRejectsNonEmptyDestination)
{
    ScopedTempDirectory temp_dir;
    const auto archive_path = temp_dir.path() / "overwrite.tar";
    createTarArchive(archive_path, {{"package/file.txt", "new\n", '0'}});
    const auto destination = temp_dir.path() / "extract";
    writeTextFile(destination / "package" / "file.txt", "existing\n");

    const auto result = an::ui::extensions::extract_extension_package_archive(archive_path, destination);

    EXPECT_FALSE(result.success);
    EXPECT_NE(result.diagnostic.find("empty"), std::string::npos);
    EXPECT_TRUE(std::filesystem::exists(destination / "package" / "file.txt"));
}

// The default archive limits admit the entry and total sizes the first-party AI wheelhouses need.
TEST(ExtensionManagerTest, DefaultPackageArchiveLimitsAllowFirstPartyAiWheelhouseAssets)
{
    const an::ui::extensions::ExtensionPackageArchiveLimits limits;

    EXPECT_GE(limits.max_entry_size_bytes, 768ull * 1024ull * 1024ull);
    EXPECT_GE(limits.max_total_size_bytes, 2ull * 1024ull * 1024ull * 1024ull);
}

// A failed extraction leaves no partial package behind.
TEST(ExtensionManagerTest, SafePackageArchiveExtractionRollsBackAfterFailedExtraction)
{
    ScopedTempDirectory temp_dir;
    const auto archive_path = temp_dir.path() / "partial.tar";
    const auto destination = temp_dir.path() / "extract";
    createTarArchive(archive_path, {{"package/good.txt", "good\n", '0'}, {"package/link", "target", '2'}});

    const auto result = an::ui::extensions::extract_extension_package_archive(archive_path, destination);

    EXPECT_FALSE(result.success);
    EXPECT_NE(result.diagnostic.find("link"), std::string::npos);
    EXPECT_FALSE(std::filesystem::exists(destination));
}

// A zstd-compressed archive is extracted as its tar would be.
TEST(ExtensionManagerTest, SafePackageArchiveExtractionAcceptsZstdCompressedTar)
{
    ScopedTempDirectory temp_dir;
    const auto tar_path = temp_dir.path() / "compressed.tar";
    const auto archive_path = temp_dir.path() / "compressed.tar.zst";
    createTarArchive(tar_path,
                     {{"package/extension.json", "{}", '0'}, {"package/extension.py", "class Extension: pass\n", '0'}});
    createZstdCompressedArchive(tar_path, archive_path);

    const auto destination = temp_dir.path() / "extract";
    const auto result = an::ui::extensions::extract_extension_package_archive(archive_path, destination);

    EXPECT_TRUE(result.success) << result.diagnostic;
    EXPECT_EQ(result.file_count, 2u);
    EXPECT_EQ(result.total_uncompressed_size_bytes,
              static_cast<std::uint64_t>(2 + std::string("class Extension: pass\n").size()));
    EXPECT_TRUE(std::filesystem::exists(destination / "package" / "extension.json"));
    EXPECT_EQ(readTextFile(destination / "package" / "extension.py"), "class Extension: pass\n");
}

// An entry whose long path a PAX record carries is extracted at that path.
TEST(ExtensionManagerTest, SafePackageArchiveExtractionAcceptsPaxLongPaths)
{
    ScopedTempDirectory temp_dir;
    const auto archive_path = temp_dir.path() / "pax_long_path.tar";
    const std::string long_path =
        "package/wheelhouse/"
        "nvidia_cuda_runtime_cu12-12.9.79-py3-none-manylinux2014_x86_64.manylinux_2_17_x86_64.whl";
    const std::string payload = "wheel fixture\n";

    std::ofstream archive(archive_path, std::ios::binary);
    ASSERT_TRUE(archive.is_open()) << "Failed to open " << archive_path;
    writeTarEntry(archive, "././@PaxHeader", paxRecord("path", long_path), 'x');
    writeTarEntry(archive, "package/wheelhouse/long.whl", payload, '0');
    finishTarArchive(archive);
    archive.close();

    const auto destination = temp_dir.path() / "extract";
    const auto result = an::ui::extensions::extract_extension_package_archive(archive_path, destination);

    EXPECT_TRUE(result.success) << result.diagnostic;
    EXPECT_EQ(result.file_count, 1u);
    EXPECT_EQ(readTextFile(destination / long_path), payload);
}

// A local package install verifies, extracts, validates and activates the package and writes its receipt,
// reporting each phase.
TEST(ExtensionManagerTest, LocalPackageInstallTransactionActivatesPackageAndWritesReceipt)
{
    ScopedTempDirectory temp_dir;
    const auto tar_path = temp_dir.path() / "repo_ready.tar";
    const auto archive_path = temp_dir.path() / "repo_ready.tar.zst";
    const std::string manifest = R"json({
      "id": "repo.ready",
      "name": "Repository Ready",
      "version": "1.0.0",
      "summary": "Install transaction fixture.",
      "author": "AfterNight Tests",
      "license": "GPL-3.0-or-later",
      "publisher_id": "afternight.tests",
      "type": "python",
      "entry_point": "repo_ready",
      "process_class": "RepositoryReadyExtension",
      "category": "filters",
      "launch_mode": "single_image",
      "package_format_version": 1,
      "protocol_version": 1,
      "sdk_version": 1
    })json";
    createTarArchive(tar_path, {{"repo_ready/extension.json", manifest, '0'},
                                {"repo_ready/repo_ready.py", "class RepositoryReadyExtension: pass\n", '0'}});
    createZstdCompressedArchive(tar_path, archive_path);
    const std::string archive_hash = sha256FileHex(archive_path);

    an::ui::extensions::ExtensionPackageInstallRequest request;
    request.archive_path = archive_path;
    request.install_root = temp_dir.path() / "installed";
    request.context = defaultContext();
    request.context.environments_root = temp_dir.path() / "ExtensionEnvs";
    request.source = ExtensionPackageSource::LocalPackage;
    request.expected_archive_hash_sha256 = archive_hash;
    request.selected_release_asset = "repo_ready-1.0.0.tar.zst";
    request.signature_state = ExtensionPackageSignatureState::Unsigned;
    request.signature_detail = "unsigned local fixture";

    const auto stale_settings_cache_dir = request.context.environments_root / "_settings_schema_cache" / "repo_ready";
    std::filesystem::create_directories(stale_settings_cache_dir);
    writeTextFile(stale_settings_cache_dir / "old-fingerprint.json", "{}\n");

    std::vector<ExtensionOperationProgress> progress_events;
    request.progress_callback = [&](const ExtensionOperationProgress& progress) {
        progress_events.push_back(progress);
    };

    an::ui::extensions::ExtensionPackageTransactionService service;
    const auto result = service.installArchive(request);

    EXPECT_TRUE(result.success) << result.diagnostic;
    EXPECT_TRUE(progressContainsPhase(progress_events, "verify_archive"));
    EXPECT_TRUE(progressContainsPhase(progress_events, "extract_archive"));
    EXPECT_TRUE(progressContainsPhase(progress_events, "validate_package"));
    EXPECT_TRUE(progressContainsPhase(progress_events, "write_receipt"));
    EXPECT_TRUE(progressContainsPhase(progress_events, "activate_package"));
    EXPECT_TRUE(progressContainsPhase(progress_events, "complete"));
    EXPECT_FALSE(progress_events.empty());
    EXPECT_EQ(progress_events.front().current, 0);
    EXPECT_EQ(progress_events.front().total, 10000);
    EXPECT_EQ(progress_events.back().current, 10000);
    EXPECT_EQ(progress_events.back().total, 10000);
    EXPECT_EQ(result.archive_hash_sha256, archive_hash);
    ASSERT_FALSE(result.activated_package_root.empty());
    EXPECT_TRUE(std::filesystem::exists(result.activated_package_root / "extension.json"));
    EXPECT_FALSE(std::filesystem::exists(result.staging_root));
    EXPECT_FALSE(std::filesystem::exists(stale_settings_cache_dir));

    const QJsonObject receipt = loadJsonFile(result.activated_package_root / "extension_package_receipt.json");
    EXPECT_EQ(receipt.value(QStringLiteral("extension_id")).toString(), QStringLiteral("repo.ready"));
    EXPECT_EQ(receipt.value(QStringLiteral("source_tier")).toString(), QStringLiteral("local_package"));
    EXPECT_EQ(receipt.value(QStringLiteral("archive_hash")).toString(), QString::fromStdString(archive_hash));
    EXPECT_EQ(receipt.value(QStringLiteral("selected_release_asset")).toString(),
              QStringLiteral("repo_ready-1.0.0.tar.zst"));

    ExtensionDiscoveryOptions options;
    options.local_package_roots = {request.install_root};
    options.context = defaultContext();
    ExtensionManager manager;
    const auto discovered = manager.discoverExtensions(options);
    ASSERT_EQ(discovered.size(), 1u);
    ASSERT_TRUE(discovered.front().manifest.has_value());
    EXPECT_EQ(discovered.front().source, ExtensionPackageSource::LocalPackage);
    EXPECT_EQ(discovered.front().manifest->id, "repo.ready");
    EXPECT_EQ(discovered.front().provenance.archive_hash_sha256, archive_hash);
}

// A local package install cancelled before activation leaves nothing installed.
TEST(ExtensionManagerTest, LocalPackageInstallTransactionCanCancelBeforeActivation)
{
    ScopedTempDirectory temp_dir;
    const auto archive_path = temp_dir.path() / "repo_ready_cancel.tar";
    const std::string manifest = R"json({
      "id": "repo.ready.cancel",
      "name": "Repository Ready Cancel",
      "version": "1.0.0",
      "summary": "Install cancellation fixture.",
      "author": "AfterNight Tests",
      "license": "GPL-3.0-or-later",
      "publisher_id": "afternight.tests",
      "type": "python",
      "entry_point": "repo_ready_cancel",
      "process_class": "RepositoryReadyCancelExtension",
      "category": "filters",
      "launch_mode": "single_image",
      "package_format_version": 1,
      "protocol_version": 1,
      "sdk_version": 1
    })json";
    createTarArchive(archive_path,
                     {{"repo_ready_cancel/extension.json", manifest, '0'},
                      {"repo_ready_cancel/repo_ready_cancel.py", "class RepositoryReadyCancelExtension: pass\n", '0'}});

    an::ui::extensions::ExtensionPackageInstallRequest request;
    request.archive_path = archive_path;
    request.install_root = temp_dir.path() / "installed";
    request.context = defaultContext();
    request.source = ExtensionPackageSource::LocalPackage;
    request.expected_archive_hash_sha256 = sha256FileHex(archive_path);
    request.selected_release_asset = "repo_ready_cancel-1.0.0.tar";
    request.progress_callback = [&](const ExtensionOperationProgress& progress) {
        if (progress.phase_id == "activate_package")
        {
            request.cancellation_token.requestCancellation();
        }
    };

    an::ui::extensions::ExtensionPackageTransactionService service;
    const auto result = service.installArchive(request);

    EXPECT_FALSE(result.success);
    EXPECT_TRUE(result.cancelled);
    EXPECT_NE(result.diagnostic.find("cancelled"), std::string::npos);
    EXPECT_FALSE(std::filesystem::exists(result.staging_root));
    EXPECT_FALSE(std::filesystem::exists(result.activated_package_root));
}

// A local package install can provision the environment, precompile its bytecode and prime its caches before it
// activates the package, reporting determinate progress throughout.
TEST(ExtensionManagerTest, LocalPackageInstallTransactionCanProvisionAndPrimeBeforeActivation)
{
    const std::filesystem::path host_path = extensionHostPathForCurrentAppDir();
    const std::filesystem::path runtime_root = runtimeRootForCurrentAppDir();
    const std::filesystem::path bundled_python = findRepositoryPythonExecutable();
    AN_REQUIRE_FULL_EXTENSION_HOST(host_path);
    ASSERT_TRUE(std::filesystem::exists(runtime_root)) << runtime_root;
    ASSERT_FALSE(bundled_python.empty()) << "Repository bundled Python runtime must be available";

    ScopedTempDirectory temp_dir;
    const auto archive_path = temp_dir.path() / "repo_ready_strict.tar";
    const std::string manifest = R"json({
      "id": "repo.ready.strict",
      "name": "Repository Ready Strict",
      "version": "1.0.0",
      "summary": "Strict install transaction fixture.",
      "author": "AfterNight Tests",
      "license": "GPL-3.0-or-later",
      "publisher_id": "afternight.tests",
      "type": "python",
      "entry_point": "repo_ready_strict",
      "category": "filters",
      "launch_mode": "single_image",
      "package_format_version": 1,
      "protocol_version": 1,
      "sdk_version": 1,
      "processes": [
        {
          "id_suffix": "alpha",
          "class": "RepositoryReadyStrictAlpha",
          "display_name": "Repository Ready Strict Alpha"
        },
        {
          "id_suffix": "beta",
          "class": "RepositoryReadyStrictBeta",
          "display_name": "Repository Ready Strict Beta"
        }
      ]
    })json";
    createTarArchive(archive_path, {{"repo_ready_strict/extension.json", manifest, '0'},
                                    {"repo_ready_strict/repo_ready_strict.py",
                                     "import afternight\n\n"
                                     "class RepositoryReadyStrictAlpha(afternight.ProcessExtension):\n"
                                     "    def get_settings_params(self):\n"
                                     "        return [{'id': 'alpha_gain', 'type': 'float', 'default': 1.0}]\n\n"
                                     "class RepositoryReadyStrictBeta(afternight.ProcessExtension):\n"
                                     "    def get_settings_params(self):\n"
                                     "        return [{'id': 'beta_gain', 'type': 'float', 'default': 2.0}]\n",
                                     '0'}});
    const std::string archive_hash = sha256FileHex(archive_path);

    ExtensionDiscoveryContext context = defaultContext();
    context.environments_root = temp_dir.path() / "ExtensionEnvs";
    context.extension_host_executable = host_path;
    context.runtime_root = runtime_root;
    context.bundled_python_executable = bundled_python;

    an::ui::extensions::ExtensionPackageInstallRequest request;
    request.archive_path = archive_path;
    request.install_root = temp_dir.path() / "installed";
    request.context = context;
    request.source = ExtensionPackageSource::LocalPackage;
    request.expected_archive_hash_sha256 = archive_hash;
    request.selected_release_asset = "repo_ready_strict-1.0.0.tar";
    request.provision_environment = true;
    request.prime_startup_caches = true;

    std::vector<ExtensionOperationProgress> progress_events;
    request.progress_callback = [&](const ExtensionOperationProgress& progress) {
        progress_events.push_back(progress);
    };

    an::ui::extensions::ExtensionPackageTransactionService service;
    const auto result = service.installArchive(request);

    EXPECT_TRUE(result.success) << result.diagnostic;
    expectDeterminateMonotonicProgress(progress_events);
    EXPECT_TRUE(progressContainsPhase(progress_events, "provision_environment"));
    EXPECT_TRUE(progressContainsPhase(progress_events, "create_environment"));
    EXPECT_TRUE(progressContainsPhase(progress_events, "stage_runtime"));
    EXPECT_TRUE(progressContainsPhase(progress_events, "precompile_dependencies"));
    EXPECT_TRUE(progressContainsPhase(progress_events, "precompile_bytecode"));
    EXPECT_TRUE(progressContainsPhase(progress_events, "prime_startup_caches"));
    EXPECT_TRUE(progressContainsPhase(progress_events, "complete"));
    const auto provision_progress =
        std::find_if(progress_events.begin(), progress_events.end(), [](const ExtensionOperationProgress& progress) {
            return progress.phase_id == "provision_environment";
        });
    const auto stage_runtime_progress =
        std::find_if(progress_events.begin(), progress_events.end(),
                     [](const ExtensionOperationProgress& progress) { return progress.phase_id == "stage_runtime"; });
    const auto dependency_precompile_progress =
        std::find_if(progress_events.begin(), progress_events.end(), [](const ExtensionOperationProgress& progress) {
            return progress.phase_id == "precompile_dependencies";
        });
    const auto dependency_precompile_file_progress =
        std::find_if(progress_events.begin(), progress_events.end(), [](const ExtensionOperationProgress& progress) {
            return progress.phase_id == "precompile_dependencies" && !progress.indeterminate && progress.current > 0 &&
                   progress.total > 0 && progress.message.find('/') != std::string::npos;
        });
    const auto startup_cache_progress =
        std::find_if(progress_events.begin(), progress_events.end(), [](const ExtensionOperationProgress& progress) {
            return progress.phase_id == "prime_startup_caches";
        });
    std::vector<ExtensionOperationProgress> startup_cache_progress_events;
    std::copy_if(
        progress_events.begin(), progress_events.end(), std::back_inserter(startup_cache_progress_events),
        [](const ExtensionOperationProgress& progress) { return progress.phase_id == "prime_startup_caches"; });
    ASSERT_NE(provision_progress, progress_events.end());
    ASSERT_NE(stage_runtime_progress, progress_events.end());
    ASSERT_NE(dependency_precompile_progress, progress_events.end());
    ASSERT_NE(dependency_precompile_file_progress, progress_events.end());
    ASSERT_NE(startup_cache_progress, progress_events.end());
    ASSERT_GE(startup_cache_progress_events.size(), 3u);
    EXPECT_FALSE(stage_runtime_progress->indeterminate);
    EXPECT_FALSE(dependency_precompile_progress->indeterminate);
    EXPECT_FALSE(dependency_precompile_file_progress->indeterminate);
    EXPECT_GT(dependency_precompile_file_progress->current, 0);
    EXPECT_GE(dependency_precompile_file_progress->total, dependency_precompile_file_progress->current);
    EXPECT_GT(stage_runtime_progress->current, provision_progress->current);
    EXPECT_GT(dependency_precompile_progress->current, stage_runtime_progress->current);
    EXPECT_LT(dependency_precompile_progress->current, startup_cache_progress->current);
    EXPECT_EQ(stage_runtime_progress->total, startup_cache_progress->total);
    EXPECT_FALSE(startup_cache_progress_events.back().indeterminate);
    EXPECT_GT(startup_cache_progress_events.back().current, startup_cache_progress->current);
    EXPECT_EQ(startup_cache_progress_events.back().total, progress_events.back().total);
    ASSERT_TRUE(result.environment.has_value());
    EXPECT_EQ(result.environment->state, ExtensionEnvironmentState::Ready);
    EXPECT_TRUE(std::filesystem::exists(result.environment->environment_path / ".environment_ready"));
    EXPECT_TRUE(std::filesystem::exists(result.environment->receipt_path));
    ASSERT_TRUE(result.startup_cache_priming.has_value());
    EXPECT_TRUE(result.startup_cache_priming->success) << result.startup_cache_priming->diagnostic;
    EXPECT_TRUE(result.startup_cache_priming->environment_ready);
    EXPECT_TRUE(result.startup_cache_priming->settings_schema_ready);
    ASSERT_TRUE(result.startup_cache_priming->load_result.settings_schema.has_value());
    EXPECT_TRUE(result.startup_cache_priming->load_result.settings_schema->schema.isEmpty());
    ASSERT_EQ(result.startup_cache_priming->load_result.settings_schema->process_schemas.size(), 2u);
    EXPECT_EQ(
        result.startup_cache_priming->load_result.settings_schema->process_schemas.at("RepositoryReadyStrictAlpha")
            .at(0)
            .toObject()
            .value(QStringLiteral("id"))
            .toString(),
        QStringLiteral("alpha_gain"));
    EXPECT_EQ(result.startup_cache_priming->load_result.settings_schema->process_schemas.at("RepositoryReadyStrictBeta")
                  .at(0)
                  .toObject()
                  .value(QStringLiteral("id"))
                  .toString(),
              QStringLiteral("beta_gain"));
    EXPECT_TRUE(std::filesystem::exists(result.activated_package_root / "extension.json"));
    EXPECT_TRUE(result.package_bytecode_precompiled);
    EXPECT_TRUE(treeContainsBytecodeForModule(result.activated_package_root, "repo_ready_strict"));
    EXPECT_FALSE(std::filesystem::exists(result.staging_root));

    ExtensionDiscoveryOptions options;
    options.local_package_roots = {request.install_root};
    options.context = context;
    options.context.eager_settings_schema_inspection = false;
    ExtensionManager manager;
    const auto discovered = manager.discoverExtensions(options);
    ASSERT_EQ(discovered.size(), 1u);
    ASSERT_TRUE(discovered.front().environment.has_value());
    EXPECT_EQ(discovered.front().environment->state, ExtensionEnvironmentState::Ready);
    ASSERT_TRUE(discovered.front().settings_schema.has_value());
    EXPECT_EQ(discovered.front().settings_schema->state, an::ui::extensions::ExtensionSettingsSchemaState::Ready);
    EXPECT_TRUE(discovered.front().settings_schema->from_cache);
    EXPECT_TRUE(discovered.front().settings_schema->schema.isEmpty());
    ASSERT_EQ(discovered.front().settings_schema->process_schemas.size(), 2u);
    EXPECT_EQ(discovered.front()
                  .settings_schema->process_schemas.at("RepositoryReadyStrictAlpha")
                  .at(0)
                  .toObject()
                  .value(QStringLiteral("id"))
                  .toString(),
              QStringLiteral("alpha_gain"));
    EXPECT_EQ(discovered.front()
                  .settings_schema->process_schemas.at("RepositoryReadyStrictBeta")
                  .at(0)
                  .toObject()
                  .value(QStringLiteral("id"))
                  .toString(),
              QStringLiteral("beta_gain"));
}

// A local package whose archive does not match its stated hash is refused and nothing is left installed.
TEST(ExtensionManagerTest, LocalPackageInstallTransactionRejectsHashMismatchAndRollsBack)
{
    ScopedTempDirectory temp_dir;
    const auto archive_path = temp_dir.path() / "repo_ready.tar";
    createTarArchive(archive_path, {{"repo_ready/extension.json", "{}", '0'}});

    an::ui::extensions::ExtensionPackageInstallRequest request;
    request.archive_path = archive_path;
    request.install_root = temp_dir.path() / "installed";
    request.context = defaultContext();
    request.source = ExtensionPackageSource::LocalPackage;
    request.expected_archive_hash_sha256 = std::string(64, '0');

    an::ui::extensions::ExtensionPackageTransactionService service;
    const auto result = service.installArchive(request);

    EXPECT_FALSE(result.success);
    EXPECT_NE(result.diagnostic.find("SHA-256"), std::string::npos);
    EXPECT_FALSE(std::filesystem::exists(request.install_root));
}

// A repository install holds the archive to its hash and signature policy, refusing what the policy refuses.
TEST(ExtensionManagerTest, RepositoryInstallTransactionEnforcesHashAndSignaturePolicy)
{
    ScopedTempDirectory temp_dir;
    const auto archive_path = temp_dir.path() / "repo_signature_policy.tar";
    const std::string manifest = R"json({
      "id": "repo.signature.policy",
      "name": "Repository Signature Policy",
      "version": "1.0.0",
      "summary": "Repository trust policy fixture.",
      "description": "Fixture package for repository signature policy validation.",
      "author": "AfterNight Tests",
      "license": "GPL-3.0-or-later",
      "publisher_id": "afternight.tests",
      "type": "python",
      "entry_point": "repo_signature_policy",
      "process_class": "RepositorySignaturePolicyExtension",
      "category": "filters",
      "launch_mode": "single_image",
      "package_format_version": 1,
      "protocol_version": 1,
      "sdk_version": 1
    })json";
    createTarArchive(archive_path, {{"repo_signature_policy/extension.json", manifest, '0'},
                                    {"repo_signature_policy/repo_signature_policy.py",
                                     "class RepositorySignaturePolicyExtension: pass\n", '0'}});
    const std::string archive_hash = sha256FileHex(archive_path);

    an::ui::extensions::ExtensionPackageTransactionService service(caseSigningKeys());
    an::ui::extensions::ExtensionPackageInstallRequest request;
    request.archive_path = archive_path;
    request.context = defaultContext();
    request.source = ExtensionPackageSource::Repository;
    request.require_repository_metadata = true;
    request.require_current_runtime_compatibility = true;

    request.install_root = temp_dir.path() / "missing_hash";
    request.signature_state = ExtensionPackageSignatureState::Unsigned;
    auto result = service.installArchive(request);
    EXPECT_FALSE(result.success);
    EXPECT_NE(result.diagnostic.find("SHA-256"), std::string::npos);
    EXPECT_FALSE(std::filesystem::exists(request.install_root));

    request.expected_archive_hash_sha256 = archive_hash;
    request.install_root = temp_dir.path() / "failed_signature";
    request.signature_state = ExtensionPackageSignatureState::Failed;
    result = service.installArchive(request);
    EXPECT_FALSE(result.success);
    EXPECT_NE(result.diagnostic.find("signature"), std::string::npos);
    EXPECT_FALSE(std::filesystem::exists(request.install_root));

    request.install_root = temp_dir.path() / "verified_missing_metadata";
    request.signature_state = ExtensionPackageSignatureState::Verified;
    result = service.installArchive(request);
    EXPECT_FALSE(result.success);
    EXPECT_NE(result.diagnostic.find("signature_algorithm"), std::string::npos);
    EXPECT_FALSE(std::filesystem::exists(request.install_root));

    const std::vector<std::string> runtime_targets = {currentRuntimeTarget()};
    applyVerifiedRepositorySignature(&request, "repo.signature.policy", "1.0.0", "repo_signature_policy-1.0.0.tar",
                                     archive_hash, runtime_targets);
    // The build trusts only the official key: a service left to its defaults refuses the cases' key by name and
    // installs nothing.
    request.install_root = temp_dir.path() / "untrusted_signature";
    result = an::ui::extensions::ExtensionPackageTransactionService().installArchive(request);
    EXPECT_FALSE(result.success);
    EXPECT_NE(result.diagnostic.find("not trusted by this build"), std::string::npos) << result.diagnostic;
    EXPECT_FALSE(std::filesystem::exists(request.install_root));

    request.install_root = temp_dir.path() / "verified_signature";
    result = service.installArchive(request);
    ASSERT_TRUE(result.success) << result.diagnostic;
    const QJsonObject receipt =
        QJsonDocument::fromJson(
            QByteArray::fromStdString(readTextFile(result.activated_package_root / "extension_package_receipt.json")))
            .object();
    EXPECT_EQ(receipt.value(QStringLiteral("signature_state")).toString(), QStringLiteral("verified"));
    EXPECT_EQ(receipt.value(QStringLiteral("signature_algorithm")).toString(), QStringLiteral("ed25519"));
    EXPECT_EQ(receipt.value(QStringLiteral("signature_key_id")).toString(),
              QStringLiteral("afternight-test-ed25519-v1"));

    auto bad_signature = request;
    bad_signature.install_root = temp_dir.path() / "bad_signature";
    bad_signature.signature.back() = bad_signature.signature.back() == 'A' ? 'B' : 'A';
    result = service.installArchive(bad_signature);
    EXPECT_FALSE(result.success);
    EXPECT_NE(result.diagnostic.find("signature"), std::string::npos);
    EXPECT_FALSE(std::filesystem::exists(bad_signature.install_root));

    auto mismatched_manifest = request;
    mismatched_manifest.install_root = temp_dir.path() / "mismatched_manifest";
    mismatched_manifest.expected_extension_version = "9.9.9";
    mismatched_manifest.signature = signRepositoryAsset(
        "repo.signature.policy", "9.9.9", "repo_signature_policy-1.0.0.tar", archive_hash, runtime_targets);
    result = service.installArchive(mismatched_manifest);
    EXPECT_FALSE(result.success);
    EXPECT_NE(result.diagnostic.find("version"), std::string::npos);
    EXPECT_FALSE(std::filesystem::exists(mismatched_manifest.install_root));
}

// A local package update keeps the extension's settings and replaces the package in one step.
TEST(ExtensionManagerTest, LocalPackageUpdateTransactionPreservesSettingsAndActivatesAtomically)
{
    ScopedTempDirectory temp_dir;
    const auto install_root = temp_dir.path() / "installed";
    const std::string manifest_v1 = R"json({
      "id": "repo.ready.update.settings",
      "name": "Repository Ready Update Settings",
      "version": "1.0.0",
      "summary": "Update settings fixture.",
      "author": "AfterNight Tests",
      "license": "GPL-3.0-or-later",
      "publisher_id": "afternight.tests",
      "type": "python",
      "entry_point": "repo_ready_update_settings",
      "process_class": "RepositoryReadyUpdateSettingsExtension",
      "category": "filters",
      "launch_mode": "single_image",
      "package_format_version": 1,
      "protocol_version": 1,
      "sdk_version": 1
    })json";
    const std::string manifest_v2 = R"json({
      "id": "repo.ready.update.settings",
      "name": "Repository Ready Update Settings",
      "version": "2.0.0",
      "summary": "Update settings fixture.",
      "author": "AfterNight Tests",
      "license": "GPL-3.0-or-later",
      "publisher_id": "afternight.tests",
      "type": "python",
      "entry_point": "repo_ready_update_settings",
      "process_class": "RepositoryReadyUpdateSettingsExtension",
      "category": "filters",
      "launch_mode": "single_image",
      "package_format_version": 1,
      "protocol_version": 1,
      "sdk_version": 1
    })json";

    const auto archive_v1 = temp_dir.path() / "repo_ready_update_settings-1.0.0.tar";
    createTarArchive(archive_v1, {{"repo_ready_update_settings/extension.json", manifest_v1, '0'},
                                  {"repo_ready_update_settings/repo_ready_update_settings.py",
                                   "class RepositoryReadyUpdateSettingsExtension:\n    VALUE = 'v1'\n", '0'}});
    const auto archive_v2 = temp_dir.path() / "repo_ready_update_settings-2.0.0.tar";
    createTarArchive(archive_v2, {{"repo_ready_update_settings/extension.json", manifest_v2, '0'},
                                  {"repo_ready_update_settings/repo_ready_update_settings.py",
                                   "class RepositoryReadyUpdateSettingsExtension:\n    VALUE = 'v2'\n", '0'}});

    an::ui::extensions::ExtensionPackageTransactionService service;
    an::ui::extensions::ExtensionPackageInstallRequest request_v1;
    request_v1.archive_path = archive_v1;
    request_v1.install_root = install_root;
    request_v1.context = defaultContext();
    request_v1.source = ExtensionPackageSource::LocalPackage;
    request_v1.expected_archive_hash_sha256 = sha256FileHex(archive_v1);
    request_v1.selected_release_asset = "repo_ready_update_settings-1.0.0.tar";

    const auto result_v1 = service.installArchive(request_v1);
    ASSERT_TRUE(result_v1.success) << result_v1.diagnostic;
    const auto activated_file = result_v1.activated_package_root / "repo_ready_update_settings.py";
    ASSERT_TRUE(std::filesystem::exists(activated_file));
    EXPECT_NE(readTextFile(activated_file).find("VALUE = 'v1'"), std::string::npos);

    PersistenceDbExecutor executor(QStringLiteral(":memory:"), QStringLiteral("extension_update_settings_test"));
    GlobalSettingsPersistence settings(executor);
    an::ui::extensions::ExtensionSdkService sdk_service(&settings);
    std::string error;
    ASSERT_TRUE(
        sdk_service.upsertExtensionSetting("repo.ready.update.settings", "gain", QStringLiteral("0.75"), &error))
        << error;

    an::ui::extensions::ExtensionPackageInstallRequest request_v2 = request_v1;
    request_v2.archive_path = archive_v2;
    request_v2.expected_archive_hash_sha256 = sha256FileHex(archive_v2);
    request_v2.selected_release_asset = "repo_ready_update_settings-2.0.0.tar";

    const auto result_v2 = service.installArchive(request_v2);
    EXPECT_TRUE(result_v2.success) << result_v2.diagnostic;
    EXPECT_EQ(result_v2.activated_package_root, result_v1.activated_package_root);
    EXPECT_FALSE(std::filesystem::exists(result_v2.staging_root));
    EXPECT_NE(readTextFile(activated_file).find("VALUE = 'v2'"), std::string::npos);
    EXPECT_EQ(readTextFile(activated_file).find("VALUE = 'v1'"), std::string::npos);

    QString persisted_gain;
    ASSERT_TRUE(sdk_service.selectExtensionSetting("repo.ready.update.settings", "gain", &persisted_gain, {}, &error))
        << error;
    EXPECT_EQ(persisted_gain, QStringLiteral("0.75"));

    ExtensionDiscoveryOptions options;
    options.local_package_roots = {install_root};
    options.context = defaultContext();
    ExtensionManager manager;
    const auto discovered = manager.discoverExtensions(options);
    ASSERT_EQ(discovered.size(), 1u);
    ASSERT_TRUE(discovered.front().manifest.has_value());
    EXPECT_EQ(discovered.front().manifest->version, "2.0.0");
}

// An update whose provisioning fails leaves the previous package active.
TEST(ExtensionManagerTest, LocalPackageUpdateTransactionRollsBackOnProvisioningFailure)
{
    ScopedTempDirectory temp_dir;
    const auto install_root = temp_dir.path() / "installed";
    const std::string manifest_v1 = R"json({
      "id": "repo.ready.update.provision",
      "name": "Repository Ready Update Provision",
      "version": "1.0.0",
      "summary": "Update provisioning fixture.",
      "author": "AfterNight Tests",
      "license": "GPL-3.0-or-later",
      "publisher_id": "afternight.tests",
      "type": "python",
      "entry_point": "repo_ready_update_provision",
      "process_class": "RepositoryReadyUpdateProvisionExtension",
      "category": "filters",
      "launch_mode": "single_image",
      "package_format_version": 1,
      "protocol_version": 1,
      "sdk_version": 1
    })json";
    const std::string manifest_v2 = R"json({
      "id": "repo.ready.update.provision",
      "name": "Repository Ready Update Provision",
      "version": "2.0.0",
      "summary": "Update provisioning fixture.",
      "author": "AfterNight Tests",
      "license": "GPL-3.0-or-later",
      "publisher_id": "afternight.tests",
      "type": "python",
      "entry_point": "repo_ready_update_provision",
      "process_class": "RepositoryReadyUpdateProvisionExtension",
      "category": "filters",
      "launch_mode": "single_image",
      "package_format_version": 1,
      "protocol_version": 1,
      "sdk_version": 1
    })json";

    const auto archive_v1 = temp_dir.path() / "repo_ready_update_provision-1.0.0.tar";
    createTarArchive(archive_v1, {{"repo_ready_update_provision/extension.json", manifest_v1, '0'},
                                  {"repo_ready_update_provision/repo_ready_update_provision.py",
                                   "class RepositoryReadyUpdateProvisionExtension:\n    VALUE = 'v1'\n", '0'}});
    const auto archive_v2 = temp_dir.path() / "repo_ready_update_provision-2.0.0.tar";
    createTarArchive(archive_v2, {{"repo_ready_update_provision/extension.json", manifest_v2, '0'},
                                  {"repo_ready_update_provision/repo_ready_update_provision.py",
                                   "class RepositoryReadyUpdateProvisionExtension:\n    VALUE = 'v2'\n", '0'}});

    an::ui::extensions::ExtensionPackageTransactionService service;
    an::ui::extensions::ExtensionPackageInstallRequest request_v1;
    request_v1.archive_path = archive_v1;
    request_v1.install_root = install_root;
    request_v1.context = defaultContext();
    request_v1.source = ExtensionPackageSource::LocalPackage;
    request_v1.expected_archive_hash_sha256 = sha256FileHex(archive_v1);
    request_v1.selected_release_asset = "repo_ready_update_provision-1.0.0.tar";

    const auto result_v1 = service.installArchive(request_v1);
    ASSERT_TRUE(result_v1.success) << result_v1.diagnostic;
    const auto activated_file = result_v1.activated_package_root / "repo_ready_update_provision.py";
    ASSERT_TRUE(std::filesystem::exists(activated_file));

    an::ui::extensions::ExtensionPackageInstallRequest bad_update = request_v1;
    bad_update.archive_path = archive_v2;
    bad_update.expected_archive_hash_sha256 = sha256FileHex(archive_v2);
    bad_update.selected_release_asset = "repo_ready_update_provision-2.0.0.tar";
    bad_update.provision_environment = true;
    bad_update.context.environments_root = temp_dir.path() / "ExtensionEnvs";
    bad_update.context.bundled_python_executable = temp_dir.path() / "missing-python";

    const auto failed_update = service.installArchive(bad_update);
    EXPECT_FALSE(failed_update.success);
    EXPECT_NE(failed_update.diagnostic.find("Bundled Python"), std::string::npos);
    EXPECT_FALSE(std::filesystem::exists(failed_update.staging_root));
    EXPECT_TRUE(std::filesystem::exists(result_v1.activated_package_root / "extension.json"));
    EXPECT_NE(readTextFile(activated_file).find("VALUE = 'v1'"), std::string::npos);
    EXPECT_EQ(readTextFile(activated_file).find("VALUE = 'v2'"), std::string::npos);
}

// An update whose cache priming fails leaves the previous package active.
TEST(ExtensionManagerTest, LocalPackageUpdateTransactionRollsBackOnCachePrimingFailure)
{
    const std::filesystem::path host_path = extensionHostPathForCurrentAppDir();
    const std::filesystem::path runtime_root = runtimeRootForCurrentAppDir();
    const std::filesystem::path bundled_python = findRepositoryPythonExecutable();
    AN_REQUIRE_FULL_EXTENSION_HOST(host_path);
    ASSERT_TRUE(std::filesystem::exists(runtime_root)) << runtime_root;
    ASSERT_FALSE(bundled_python.empty()) << "Repository bundled Python runtime must be available";

    ScopedTempDirectory temp_dir;
    const auto install_root = temp_dir.path() / "installed";
    const std::string manifest_v1 = R"json({
      "id": "repo.ready.update.cache",
      "name": "Repository Ready Update Cache",
      "version": "1.0.0",
      "summary": "Update cache fixture.",
      "author": "AfterNight Tests",
      "license": "GPL-3.0-or-later",
      "publisher_id": "afternight.tests",
      "type": "python",
      "entry_point": "repo_ready_update_cache",
      "process_class": "RepositoryReadyUpdateCacheExtension",
      "category": "filters",
      "launch_mode": "single_image",
      "package_format_version": 1,
      "protocol_version": 1,
      "sdk_version": 1
    })json";
    const std::string manifest_v2 = R"json({
      "id": "repo.ready.update.cache",
      "name": "Repository Ready Update Cache",
      "version": "2.0.0",
      "summary": "Update cache fixture.",
      "author": "AfterNight Tests",
      "license": "GPL-3.0-or-later",
      "publisher_id": "afternight.tests",
      "type": "python",
      "entry_point": "repo_ready_update_cache",
      "category": "filters",
      "launch_mode": "single_image",
      "package_format_version": 1,
      "protocol_version": 1,
      "sdk_version": 1,
      "processes": [
        {
          "id_suffix": "alpha",
          "class": "RepositoryReadyUpdateCacheAlpha",
          "display_name": "Cache Alpha"
        },
        {
          "id_suffix": "beta",
          "class": "RepositoryReadyUpdateCacheBeta",
          "display_name": "Cache Beta"
        }
      ]
    })json";

    const auto archive_v1 = temp_dir.path() / "repo_ready_update_cache-1.0.0.tar";
    createTarArchive(archive_v1, {{"repo_ready_update_cache/extension.json", manifest_v1, '0'},
                                  {"repo_ready_update_cache/repo_ready_update_cache.py",
                                   "class RepositoryReadyUpdateCacheExtension:\n    VALUE = 'v1'\n", '0'}});
    const auto archive_v2 = temp_dir.path() / "repo_ready_update_cache-2.0.0.tar";
    createTarArchive(archive_v2, {{"repo_ready_update_cache/extension.json", manifest_v2, '0'},
                                  {"repo_ready_update_cache/repo_ready_update_cache.py",
                                   "import afternight\n\n"
                                   "class RepositoryReadyUpdateCacheAlpha(afternight.ProcessExtension):\n"
                                   "    def get_settings_params(self):\n"
                                   "        return [{'id': 'alpha_gain', 'type': 'float', 'default': 1.0}]\n\n"
                                   "class RepositoryReadyUpdateCacheBeta(afternight.ProcessExtension):\n"
                                   "    def get_settings_params(self):\n"
                                   "        raise RuntimeError('beta schema unavailable')\n",
                                   '0'}});

    an::ui::extensions::ExtensionPackageTransactionService service;
    ExtensionDiscoveryContext context = defaultContext();
    context.environments_root = temp_dir.path() / "ExtensionEnvs";
    context.extension_host_executable = host_path;
    context.runtime_root = runtime_root;
    context.bundled_python_executable = bundled_python;

    an::ui::extensions::ExtensionPackageInstallRequest request_v1;
    request_v1.archive_path = archive_v1;
    request_v1.install_root = install_root;
    request_v1.context = context;
    request_v1.source = ExtensionPackageSource::LocalPackage;
    request_v1.expected_archive_hash_sha256 = sha256FileHex(archive_v1);
    request_v1.selected_release_asset = "repo_ready_update_cache-1.0.0.tar";

    const auto result_v1 = service.installArchive(request_v1);
    ASSERT_TRUE(result_v1.success) << result_v1.diagnostic;
    const auto activated_file = result_v1.activated_package_root / "repo_ready_update_cache.py";
    ASSERT_TRUE(std::filesystem::exists(activated_file));

    an::ui::extensions::ExtensionPackageInstallRequest bad_update = request_v1;
    bad_update.archive_path = archive_v2;
    bad_update.expected_archive_hash_sha256 = sha256FileHex(archive_v2);
    bad_update.selected_release_asset = "repo_ready_update_cache-2.0.0.tar";
    bad_update.provision_environment = true;
    bad_update.prime_startup_caches = true;
    bad_update.require_settings_schema_cache = true;

    const auto failed_update = service.installArchive(bad_update);
    EXPECT_FALSE(failed_update.success);
    ASSERT_TRUE(failed_update.startup_cache_priming.has_value());
    EXPECT_FALSE(failed_update.startup_cache_priming->success);
    EXPECT_NE(failed_update.diagnostic.find("beta schema unavailable"), std::string::npos);
    EXPECT_FALSE(std::filesystem::exists(failed_update.staging_root));
    EXPECT_TRUE(std::filesystem::exists(result_v1.activated_package_root / "extension.json"));
    EXPECT_NE(readTextFile(activated_file).find("VALUE = 'v1'"), std::string::npos);
    EXPECT_EQ(readTextFile(activated_file).find("alpha_gain"), std::string::npos);
}

// A repair rebuilds a failed environment and primes its caches.
TEST(ExtensionManagerTest, LocalPackageRepairTransactionRebuildsFailedEnvironmentAndPrimesCaches)
{
    const std::filesystem::path host_path = extensionHostPathForCurrentAppDir();
    const std::filesystem::path runtime_root = runtimeRootForCurrentAppDir();
    const std::filesystem::path bundled_python = findRepositoryPythonExecutable();
    AN_REQUIRE_FULL_EXTENSION_HOST(host_path);
    ASSERT_TRUE(std::filesystem::exists(runtime_root)) << runtime_root;
    ASSERT_FALSE(bundled_python.empty()) << "Repository bundled Python runtime must be available";

    ScopedTempDirectory temp_dir;
    const auto archive_path = temp_dir.path() / "repo_ready_repair-1.0.0.tar";
    const std::string manifest = R"json({
      "id": "repo.ready.repair",
      "name": "Repository Ready Repair",
      "version": "1.0.0",
      "summary": "Repair transaction fixture.",
      "author": "AfterNight Tests",
      "license": "GPL-3.0-or-later",
      "publisher_id": "afternight.tests",
      "type": "python",
      "entry_point": "repo_ready_repair",
      "process_class": "RepositoryReadyRepairExtension",
      "category": "filters",
      "launch_mode": "single_image",
      "package_format_version": 1,
      "protocol_version": 1,
      "sdk_version": 1
    })json";
    createTarArchive(archive_path, {{"repo_ready_repair/extension.json", manifest, '0'},
                                    {"repo_ready_repair/repo_ready_repair.py",
                                     "import afternight\n\n"
                                     "class RepositoryReadyRepairExtension(afternight.ProcessExtension):\n"
                                     "    def get_settings_params(self):\n"
                                     "        return [{'id': 'repair_gain', 'type': 'float', 'default': 1.0}]\n",
                                     '0'}});

    ExtensionDiscoveryContext context = defaultContext();
    context.environments_root = temp_dir.path() / "ExtensionEnvs";
    context.extension_host_executable = host_path;
    context.runtime_root = runtime_root;
    context.bundled_python_executable = bundled_python;

    an::ui::extensions::ExtensionPackageTransactionService service;
    an::ui::extensions::ExtensionPackageInstallRequest install_request;
    install_request.archive_path = archive_path;
    install_request.install_root = temp_dir.path() / "installed";
    install_request.context = context;
    install_request.source = ExtensionPackageSource::LocalPackage;
    install_request.expected_archive_hash_sha256 = sha256FileHex(archive_path);
    install_request.selected_release_asset = "repo_ready_repair-1.0.0.tar";

    const auto install_result = service.installArchive(install_request);
    ASSERT_TRUE(install_result.success) << install_result.diagnostic;

    an::ui::extensions::ExtensionPackageRepairRequest bad_repair;
    bad_repair.install_root = install_request.install_root;
    bad_repair.extension_id = "repo.ready.repair";
    bad_repair.context = context;
    bad_repair.context.bundled_python_executable = temp_dir.path() / "missing-python";
    bad_repair.source = ExtensionPackageSource::LocalPackage;

    const auto failed_repair = service.repairInstalledPackage(bad_repair);
    EXPECT_FALSE(failed_repair.success);
    EXPECT_NE(failed_repair.diagnostic.find("Bundled Python"), std::string::npos);
    ASSERT_TRUE(failed_repair.environment.has_value());
    EXPECT_EQ(failed_repair.environment->state, ExtensionEnvironmentState::Failed);

    an::ui::extensions::ExtensionPackageRepairRequest repair_request = bad_repair;
    repair_request.context = context;
    std::vector<ExtensionOperationProgress> repair_progress_events;
    repair_request.progress_callback = [&](const ExtensionOperationProgress& progress) {
        repair_progress_events.push_back(progress);
    };
    const auto repair_result = service.repairInstalledPackage(repair_request);

    EXPECT_TRUE(repair_result.success) << repair_result.diagnostic;
    EXPECT_TRUE(progressContainsPhase(repair_progress_events, "validate_package"));
    EXPECT_TRUE(progressContainsPhase(repair_progress_events, "provision_environment"));
    EXPECT_TRUE(progressContainsPhase(repair_progress_events, "precompile_bytecode"));
    EXPECT_TRUE(progressContainsPhase(repair_progress_events, "prime_startup_caches"));
    EXPECT_TRUE(progressContainsPhase(repair_progress_events, "complete"));
    ASSERT_TRUE(repair_result.environment.has_value());
    EXPECT_EQ(repair_result.environment->state, ExtensionEnvironmentState::Ready);
    EXPECT_TRUE(std::filesystem::exists(repair_result.environment->environment_path / ".environment_ready"));
    EXPECT_TRUE(std::filesystem::exists(repair_result.environment->receipt_path));
    ASSERT_TRUE(repair_result.startup_cache_priming.has_value());
    EXPECT_TRUE(repair_result.startup_cache_priming->success) << repair_result.startup_cache_priming->diagnostic;
    EXPECT_TRUE(repair_result.startup_cache_priming->settings_schema_ready);
    ASSERT_TRUE(repair_result.startup_cache_priming->load_result.settings_schema.has_value());
    EXPECT_EQ(repair_result.startup_cache_priming->load_result.settings_schema->schema.size(), 1);

    ExtensionDiscoveryOptions options;
    options.local_package_roots = {install_request.install_root};
    options.context = context;
    options.context.eager_settings_schema_inspection = false;
    ExtensionManager manager;
    const auto discovered = manager.discoverExtensions(options);
    ASSERT_EQ(discovered.size(), 1u);
    ASSERT_TRUE(discovered.front().settings_schema.has_value());
    EXPECT_TRUE(discovered.front().settings_schema->from_cache);
    EXPECT_EQ(discovered.front().settings_schema->state, an::ui::extensions::ExtensionSettingsSchemaState::Ready);
}

// A repair of a package that is not installed is refused.
TEST(ExtensionManagerTest, LocalPackageRepairTransactionRejectsMissingInstalledPackage)
{
    ScopedTempDirectory temp_dir;

    an::ui::extensions::ExtensionPackageRepairRequest request;
    request.install_root = temp_dir.path() / "installed";
    request.extension_id = "missing.repair";
    request.context = defaultContext();
    request.source = ExtensionPackageSource::LocalPackage;

    an::ui::extensions::ExtensionPackageTransactionService service;
    const auto result = service.repairInstalledPackage(request);

    EXPECT_FALSE(result.success);
    EXPECT_NE(result.diagnostic.find("not found"), std::string::npos);
}

// A bundled package ships with AfterNight and goes only with it: the removal refuses AfterNight's own extensions folder
// whatever it holds. The request names a package that is not there and accepts its absence, so a removal that does not
// refuse reports success and deletes nothing of the deployment the suite runs from.
TEST(ExtensionManagerTest, TheBundledExtensionsFolderIsNoPlaceToRemoveFrom)
{
    an::ui::extensions::ExtensionPackageRemoveRequest request;
    request.install_root = an::ui::extensions::bundled_extensions_root(
        std::filesystem::path(QCoreApplication::applicationDirPath().toStdString()));
    request.extension_id = "a_package_no_deployment_holds";
    request.require_existing = false;

    an::ui::extensions::ExtensionPackageTransactionService service;
    const auto result = service.removeInstalledPackage(request);

    EXPECT_FALSE(result.success);
    EXPECT_NE(result.diagnostic.find("Bundled"), std::string::npos) << result.diagnostic;
}

// A removed package is deleted and discovery no longer finds it.
TEST(ExtensionManagerTest, LocalPackageRemoveTransactionDeletesPackageAndDiscoveryNoLongerFindsIt)
{
    ScopedTempDirectory temp_dir;
    const auto archive_path = temp_dir.path() / "repo_ready.tar";
    const std::string manifest = R"json({
      "id": "repo.ready.remove",
      "name": "Repository Ready Remove",
      "version": "1.0.0",
      "summary": "Remove transaction fixture.",
      "author": "AfterNight Tests",
      "license": "GPL-3.0-or-later",
      "publisher_id": "afternight.tests",
      "type": "python",
      "entry_point": "repo_ready_remove",
      "process_class": "RepositoryReadyRemoveExtension",
      "category": "filters",
      "launch_mode": "single_image",
      "package_format_version": 1,
      "protocol_version": 1,
      "sdk_version": 1
    })json";
    createTarArchive(archive_path,
                     {{"repo_ready_remove/extension.json", manifest, '0'},
                      {"repo_ready_remove/repo_ready_remove.py", "class RepositoryReadyRemoveExtension: pass\n", '0'}});

    an::ui::extensions::ExtensionPackageInstallRequest install_request;
    install_request.archive_path = archive_path;
    install_request.install_root = temp_dir.path() / "installed";
    install_request.context = defaultContext();
    install_request.source = ExtensionPackageSource::LocalPackage;
    install_request.expected_archive_hash_sha256 = sha256FileHex(archive_path);
    install_request.selected_release_asset = "repo_ready_remove-1.0.0.tar";

    an::ui::extensions::ExtensionPackageTransactionService service;
    const auto install_result = service.installArchive(install_request);
    ASSERT_TRUE(install_result.success) << install_result.diagnostic;

    ExtensionDiscoveryOptions options;
    options.local_package_roots = {install_request.install_root};
    options.context = defaultContext();
    ExtensionManager manager;
    ASSERT_EQ(manager.discoverExtensions(options).size(), 1u);

    an::ui::extensions::ExtensionPackageRemoveRequest remove_request;
    remove_request.install_root = install_request.install_root;
    remove_request.extension_id = "repo.ready.remove";
    std::vector<ExtensionOperationProgress> remove_progress_events;
    remove_request.progress_callback = [&](const ExtensionOperationProgress& progress) {
        remove_progress_events.push_back(progress);
    };
    const auto remove_result = service.removeInstalledPackage(remove_request);

    EXPECT_TRUE(remove_result.success) << remove_result.diagnostic;
    EXPECT_TRUE(progressContainsPhase(remove_progress_events, "locate_package"));
    EXPECT_TRUE(progressContainsPhase(remove_progress_events, "stage_removal"));
    EXPECT_TRUE(progressContainsPhase(remove_progress_events, "delete_package"));
    EXPECT_TRUE(progressContainsPhase(remove_progress_events, "complete"));
    EXPECT_FALSE(remove_progress_events.empty());
    EXPECT_EQ(remove_progress_events.front().current, 1);
    EXPECT_EQ(remove_progress_events.front().total, 4);
    EXPECT_EQ(remove_progress_events.back().current, 4);
    EXPECT_EQ(remove_progress_events.back().total, 4);
    EXPECT_FALSE(std::filesystem::exists(install_result.activated_package_root));
    EXPECT_TRUE(manager.discoverExtensions(options).empty());
}

// A removal cancelled midway restores the package it had staged for deletion.
TEST(ExtensionManagerTest, LocalPackageRemoveTransactionCanCancelAndRestoreStagedPackage)
{
    ScopedTempDirectory temp_dir;
    const auto archive_path = temp_dir.path() / "repo_ready_cancel_remove.tar";
    const std::string manifest = R"json({
      "id": "repo.ready.cancel.remove",
      "name": "Repository Ready Cancel Remove",
      "version": "1.0.0",
      "summary": "Remove cancellation fixture.",
      "author": "AfterNight Tests",
      "license": "GPL-3.0-or-later",
      "publisher_id": "afternight.tests",
      "type": "python",
      "entry_point": "repo_ready_cancel_remove",
      "process_class": "RepositoryReadyCancelRemoveExtension",
      "category": "filters",
      "launch_mode": "single_image",
      "package_format_version": 1,
      "protocol_version": 1,
      "sdk_version": 1
    })json";
    createTarArchive(archive_path, {{"repo_ready_cancel_remove/extension.json", manifest, '0'},
                                    {"repo_ready_cancel_remove/repo_ready_cancel_remove.py",
                                     "class RepositoryReadyCancelRemoveExtension: pass\n", '0'}});

    an::ui::extensions::ExtensionPackageInstallRequest install_request;
    install_request.archive_path = archive_path;
    install_request.install_root = temp_dir.path() / "installed";
    install_request.context = defaultContext();
    install_request.source = ExtensionPackageSource::LocalPackage;
    install_request.expected_archive_hash_sha256 = sha256FileHex(archive_path);
    install_request.selected_release_asset = "repo_ready_cancel_remove-1.0.0.tar";

    an::ui::extensions::ExtensionPackageTransactionService service;
    const auto install_result = service.installArchive(install_request);
    ASSERT_TRUE(install_result.success) << install_result.diagnostic;

    an::ui::extensions::ExtensionPackageRemoveRequest remove_request;
    remove_request.install_root = install_request.install_root;
    remove_request.extension_id = "repo.ready.cancel.remove";
    remove_request.progress_callback = [&](const ExtensionOperationProgress& progress) {
        if (progress.phase_id == "delete_package")
        {
            remove_request.cancellation_token.requestCancellation();
        }
    };

    const auto remove_result = service.removeInstalledPackage(remove_request);

    EXPECT_FALSE(remove_result.success);
    EXPECT_TRUE(remove_result.cancelled);
    EXPECT_TRUE(std::filesystem::exists(install_result.activated_package_root / "extension.json"));

    ExtensionDiscoveryOptions options;
    options.local_package_roots = {install_request.install_root};
    options.context = defaultContext();
    ExtensionManager manager;
    EXPECT_EQ(manager.discoverExtensions(options).size(), 1u);
}

// Removing a package orphans its private environment and keeps a shared context another package still uses.
TEST(ExtensionManagerTest, LocalPackageRemoveMarksPrivateEnvironmentOrphanAndKeepsReferencedSharedContext)
{
    ScopedTempDirectory temp_dir;
    const auto install_root = temp_dir.path() / "installed";
    const auto environments_root = temp_dir.path() / "ExtensionEnvs";

    const auto private_package_root = createExtensionPackage(install_root, "remove_private_ext",
                                                             R"json({
          "id": "remove_private_ext",
          "name": "Remove Private Extension",
          "version": "1.0.0",
          "type": "python",
          "entry_point": "remove_private_ext",
          "process_class": "RemovePrivateExtension",
          "category": "filters",
          "launch_mode": "single_image",
          "package_format_version": 1,
          "protocol_version": 1,
          "sdk_version": 1
        })json");
    writeTextFile(private_package_root / "remove_private_ext.py", "class RemovePrivateExtension: pass\n");

    const auto shared_a_root = createExtensionPackage(install_root, "shared_remove_a",
                                                      R"json({
          "id": "shared_remove_a",
          "name": "Shared Remove A",
          "version": "1.0.0",
          "type": "python",
          "entry_point": "shared_remove_a",
          "process_class": "SharedRemoveAExtension",
          "category": "filters",
          "launch_mode": "single_image",
          "package_format_version": 1,
          "protocol_version": 1,
          "sdk_version": 1,
          "dependencies": {
            "requirements_file": "requirements.lock",
            "shared_group": "shared_remove_suite",
            "pip": {
              "require_hashes": true
            }
          }
        })json");
    writeTextFile(shared_a_root / "shared_remove_a.py", "class SharedRemoveAExtension: pass\n");
    writeTextFile(shared_a_root / "requirements.lock", "numpy==2.0.0 --hash=sha256:aaa\n");

    const auto shared_b_root = createExtensionPackage(install_root, "shared_remove_b",
                                                      R"json({
          "id": "shared_remove_b",
          "name": "Shared Remove B",
          "version": "1.0.0",
          "type": "python",
          "entry_point": "shared_remove_b",
          "process_class": "SharedRemoveBExtension",
          "category": "filters",
          "launch_mode": "single_image",
          "package_format_version": 1,
          "protocol_version": 1,
          "sdk_version": 1,
          "dependencies": {
            "requirements_file": "requirements.lock",
            "shared_group": "shared_remove_suite",
            "pip": {
              "require_hashes": true
            }
          }
        })json");
    writeTextFile(shared_b_root / "shared_remove_b.py", "class SharedRemoveBExtension: pass\n");
    writeTextFile(shared_b_root / "requirements.lock", "numpy==2.0.0 --hash=sha256:aaa\n");

    ExtensionDiscoveryContext context = defaultContext();
    context.environments_root = environments_root;
    const auto private_result = an::ui::extensions::load_extension_manifest_file(
        private_package_root / "extension.json", ExtensionPackageSource::LocalPackage, context);
    const auto shared_a_result = an::ui::extensions::load_extension_manifest_file(
        shared_a_root / "extension.json", ExtensionPackageSource::LocalPackage, context);
    const auto shared_b_result = an::ui::extensions::load_extension_manifest_file(
        shared_b_root / "extension.json", ExtensionPackageSource::LocalPackage, context);
    ASSERT_TRUE(private_result.environment.has_value());
    ASSERT_TRUE(shared_a_result.environment.has_value());
    ASSERT_TRUE(shared_b_result.environment.has_value());
    EXPECT_EQ(shared_a_result.environment->environment_path, shared_b_result.environment->environment_path);

    std::filesystem::create_directories(private_result.environment->environment_path);
    std::filesystem::create_directories(shared_a_result.environment->environment_path);

    an::ui::extensions::ExtensionPackageTransactionService service;
    an::ui::extensions::ExtensionPackageRemoveRequest remove_private;
    remove_private.install_root = install_root;
    remove_private.extension_id = "remove_private_ext";
    const auto private_remove_result = service.removeInstalledPackage(remove_private);
    ASSERT_TRUE(private_remove_result.success) << private_remove_result.diagnostic;

    an::ui::extensions::ExtensionPackageRemoveRequest remove_shared_a;
    remove_shared_a.install_root = install_root;
    remove_shared_a.extension_id = "shared_remove_a";
    const auto shared_remove_result = service.removeInstalledPackage(remove_shared_a);
    ASSERT_TRUE(shared_remove_result.success) << shared_remove_result.diagnostic;

    ExtensionDiscoveryOptions options;
    options.local_package_roots = {install_root};
    options.context = context;
    ExtensionManager manager;
    const auto discovered = manager.discoverExtensions(options);
    ASSERT_EQ(discovered.size(), 1u);
    ASSERT_TRUE(discovered.front().manifest.has_value());
    EXPECT_EQ(discovered.front().manifest->id, "shared_remove_b");

    ExtensionEnvironmentManager environment_manager;
    const auto inventory = environment_manager.collectEnvironmentInventory(discovered, context);
    ASSERT_EQ(inventory.size(), 2u);

    const auto private_it = std::find_if(inventory.begin(), inventory.end(), [&](const auto& entry) {
        return entry.environment_path == private_result.environment->environment_path;
    });
    ASSERT_NE(private_it, inventory.end());
    EXPECT_TRUE(private_it->orphaned);
    EXPECT_TRUE(std::filesystem::exists(private_it->environment_path));

    const auto shared_it = std::find_if(inventory.begin(), inventory.end(), [&](const auto& entry) {
        return entry.environment_path == shared_b_result.environment->environment_path;
    });
    ASSERT_NE(shared_it, inventory.end());
    EXPECT_FALSE(shared_it->orphaned);
    EXPECT_TRUE(std::filesystem::exists(shared_it->environment_path));
}

// The repository index validator accepts the tightened asset schema and refuses an asset with no hash.
TEST(ExtensionManagerTest, RepositoryIndexValidatorAcceptsTightenedAssetSchemaAndRejectsMissingHash)
{
    ScopedTempDirectory temp_dir;
    const auto repository_root = temp_dir.path() / "repository";
    const auto archive_path = repository_root / "archives" / "repo_index_ready-1.0.0.tar";
    const std::string manifest = R"json({
      "id": "repo.index.ready",
      "name": "Repository Index Ready",
      "version": "1.0.0",
      "summary": "Repository index fixture.",
      "description": "Fixture package for index validation.",
      "author": "AfterNight Tests",
      "license": "MIT",
      "publisher_id": "afternight.tests",
      "type": "python",
      "entry_point": "repo_index_ready",
      "process_class": "RepositoryIndexReadyExtension",
      "category": "filters",
      "launch_mode": "single_image",
      "package_format_version": 1,
      "protocol_version": 1,
      "sdk_version": 1
    })json";
    createTarArchive(archive_path,
                     {{"repo_index_ready/extension.json", manifest, '0'},
                      {"repo_index_ready/repo_index_ready.py", "class RepositoryIndexReadyExtension: pass\n", '0'}});
    const std::string archive_hash = sha256FileHex(archive_path);
    const auto index_path = repository_root / "index.json";
    QJsonObject asset;
    asset.insert(QStringLiteral("name"), QStringLiteral("repo_index_ready-1.0.0.tar"));
    asset.insert(QStringLiteral("download_url"), QStringLiteral("archives/repo_index_ready-1.0.0.tar"));
    asset.insert(QStringLiteral("package_hash"), QString::fromStdString("sha256:" + archive_hash));
    asset.insert(QStringLiteral("signature_state"), QStringLiteral("unsigned"));
    asset.insert(QStringLiteral("signature_detail"), QStringLiteral("unsigned fixture"));

    QJsonArray assets;
    assets.append(asset);

    QJsonObject release_object;
    release_object.insert(QStringLiteral("version"), QStringLiteral("1.0.0"));
    release_object.insert(QStringLiteral("min_app_version"), QStringLiteral("0.0.0"));
    release_object.insert(QStringLiteral("package_format_version"), 1);
    release_object.insert(QStringLiteral("protocol_version"), 1);
    release_object.insert(QStringLiteral("sdk_version"), 1);
    release_object.insert(QStringLiteral("sdk_backend"), QStringLiteral("runtime"));
    release_object.insert(QStringLiteral("launch_mode"), QStringLiteral("single_image"));
    release_object.insert(QStringLiteral("runtime_targets"), jsonStringArray({currentRuntimeTarget()}));
    release_object.insert(QStringLiteral("published_at"), QStringLiteral("2026-04-26T00:00:00Z"));
    release_object.insert(QStringLiteral("assets"), assets);

    QJsonArray releases;
    releases.append(release_object);

    QJsonObject package_object;
    package_object.insert(QStringLiteral("id"), QStringLiteral("repo.index.ready"));
    package_object.insert(QStringLiteral("name"), QStringLiteral("Repository Index Ready"));
    package_object.insert(QStringLiteral("summary"), QStringLiteral("Repository index fixture."));
    package_object.insert(QStringLiteral("description"), QStringLiteral("Fixture package for index validation."));
    package_object.insert(QStringLiteral("author"), QStringLiteral("AfterNight Tests"));
    package_object.insert(QStringLiteral("license"), QStringLiteral("MIT"));
    package_object.insert(QStringLiteral("publisher_id"), QStringLiteral("afternight.tests"));
    package_object.insert(
        QStringLiteral("attribution"),
        QStringLiteral("AfterNight port of Repository Index Ready, originally authored by Fixture Author."));
    package_object.insert(QStringLiteral("original_author"), QStringLiteral("Fixture Author"));
    package_object.insert(QStringLiteral("original_project"), QStringLiteral("Fixture Suite"));
    package_object.insert(QStringLiteral("original_source_url"),
                          QStringLiteral("https://example.invalid/upstream/repo-index-ready.py"));
    package_object.insert(QStringLiteral("upstream_commit"), QStringLiteral("abcdef0123456789"));
    package_object.insert(QStringLiteral("latest_version"), QStringLiteral("1.0.0"));
    package_object.insert(QStringLiteral("tags"), jsonStringArray({"fixture", "install"}));
    package_object.insert(QStringLiteral("releases"), releases);

    QJsonArray packages;
    packages.append(package_object);

    QJsonObject repository_index;
    repository_index.insert(QStringLiteral("schema_version"), 1);
    repository_index.insert(QStringLiteral("repository"), QStringLiteral("afternight-extension-fixture"));
    repository_index.insert(QStringLiteral("official"), true);
    repository_index.insert(QStringLiteral("updated_at"), QStringLiteral("2026-04-26T00:00:00Z"));
    repository_index.insert(QStringLiteral("extensions"), packages);
    writeJsonFile(index_path, repository_index);

    const auto validation = an::ui::extensions::validate_extension_repository_index_file(index_path);
    ASSERT_TRUE(validation.valid) << validation.diagnostic;
    ASSERT_EQ(validation.index.packages.size(), 1u);
    const auto& package = validation.index.packages.front();
    EXPECT_EQ(package.id, "repo.index.ready");
    EXPECT_EQ(package.attribution, "AfterNight port of Repository Index Ready, originally authored by Fixture Author.");
    EXPECT_EQ(package.original_author, "Fixture Author");
    EXPECT_EQ(package.original_project, "Fixture Suite");
    EXPECT_EQ(package.original_source_url, "https://example.invalid/upstream/repo-index-ready.py");
    EXPECT_EQ(package.upstream_commit, "abcdef0123456789");
    ASSERT_EQ(package.releases.size(), 1u);
    const auto& release = package.releases.front();
    EXPECT_EQ(release.package_format_version, 1);
    EXPECT_EQ(release.protocol_version, 1);
    EXPECT_EQ(release.sdk_version, 1);
    EXPECT_EQ(release.sdk_backend, "runtime");
    EXPECT_EQ(release.launch_mode, "single_image");
    ASSERT_EQ(release.assets.size(), 1u);
    EXPECT_EQ(release.assets.front().package_hash_sha256, archive_hash);
    EXPECT_EQ(release.assets.front().runtime_targets, release.runtime_targets);

    const auto invalid_index_path = repository_root / "invalid_index.json";
    QJsonObject invalid_asset = asset;
    invalid_asset.remove(QStringLiteral("package_hash"));
    QJsonArray invalid_assets;
    invalid_assets.append(invalid_asset);

    QJsonObject invalid_release = release_object;
    invalid_release.insert(QStringLiteral("assets"), invalid_assets);
    QJsonArray invalid_releases;
    invalid_releases.append(invalid_release);

    QJsonObject invalid_package_object = package_object;
    invalid_package_object.remove(QStringLiteral("publisher_id"));
    invalid_package_object.remove(QStringLiteral("tags"));
    invalid_package_object.insert(QStringLiteral("releases"), invalid_releases);
    QJsonArray invalid_packages;
    invalid_packages.append(invalid_package_object);

    QJsonObject invalid_repository_index = repository_index;
    invalid_repository_index.remove(QStringLiteral("official"));
    invalid_repository_index.insert(QStringLiteral("extensions"), invalid_packages);
    writeJsonFile(invalid_index_path, invalid_repository_index);
    const auto invalid_validation = an::ui::extensions::validate_extension_repository_index_file(invalid_index_path);
    EXPECT_FALSE(invalid_validation.valid);
    EXPECT_NE(invalid_validation.diagnostic.find("package_hash"), std::string::npos);
}

// A correctly signed asset is accepted as verified and a malformed signature is refused.
TEST(ExtensionManagerTest, RepositoryIndexValidatorAcceptsVerifiedSignedAssetAndRejectsMalformedSignatures)
{
    ScopedTempDirectory temp_dir;
    const auto index_path = temp_dir.path() / "index.json";
    const std::string package_id = "repo.signed.index";
    const std::string version = "1.0.0";
    const QString asset_name = QStringLiteral("repo_signed_index-1.0.0.tar");
    const std::string archive_hash(64, 'a');
    const std::vector<std::string> runtime_targets = {currentRuntimeTarget()};

    QJsonObject asset;
    asset.insert(QStringLiteral("name"), asset_name);
    asset.insert(QStringLiteral("download_url"), asset_name);
    asset.insert(QStringLiteral("package_hash"), QString::fromStdString("sha256:" + archive_hash));
    asset.insert(QStringLiteral("runtime_targets"), jsonStringArray(runtime_targets));
    asset.insert(QStringLiteral("signature_state"), QStringLiteral("verified"));
    asset.insert(QStringLiteral("signature_detail"), QStringLiteral("verified fixture"));
    asset.insert(QStringLiteral("signature_algorithm"), QStringLiteral("ed25519"));
    asset.insert(QStringLiteral("signature_key_id"), QStringLiteral("afternight-test-ed25519-v1"));
    asset.insert(QStringLiteral("signature"),
                 QString::fromStdString(signRepositoryAsset(package_id, version, asset_name.toStdString(), archive_hash,
                                                            runtime_targets)));

    QJsonArray assets;
    assets.append(asset);

    QJsonObject release_object;
    release_object.insert(QStringLiteral("version"), QString::fromStdString(version));
    release_object.insert(QStringLiteral("min_app_version"), QStringLiteral("0.0.0"));
    release_object.insert(QStringLiteral("package_format_version"), 1);
    release_object.insert(QStringLiteral("protocol_version"), 1);
    release_object.insert(QStringLiteral("sdk_version"), 1);
    release_object.insert(QStringLiteral("sdk_backend"), QStringLiteral("runtime"));
    release_object.insert(QStringLiteral("launch_mode"), QStringLiteral("single_image"));
    release_object.insert(QStringLiteral("runtime_targets"), jsonStringArray(runtime_targets));
    release_object.insert(QStringLiteral("assets"), assets);

    QJsonObject package_object;
    package_object.insert(QStringLiteral("id"), QString::fromStdString(package_id));
    package_object.insert(QStringLiteral("name"), QStringLiteral("Repository Signed Index"));
    package_object.insert(QStringLiteral("summary"), QStringLiteral("Signed index fixture."));
    package_object.insert(QStringLiteral("description"), QStringLiteral("Signed index fixture description."));
    package_object.insert(QStringLiteral("author"), QStringLiteral("AfterNight Tests"));
    package_object.insert(QStringLiteral("license"), QStringLiteral("MIT"));
    package_object.insert(QStringLiteral("latest_version"), QString::fromStdString(version));
    package_object.insert(QStringLiteral("releases"), QJsonArray{release_object});

    QJsonObject repository_index;
    repository_index.insert(QStringLiteral("schema_version"), 1);
    repository_index.insert(QStringLiteral("repository"), QStringLiteral("afternight-extension-fixture"));
    repository_index.insert(QStringLiteral("official"), true);
    repository_index.insert(QStringLiteral("updated_at"), QStringLiteral("2026-04-26T00:00:00Z"));
    repository_index.insert(QStringLiteral("extensions"), QJsonArray{package_object});
    writeJsonFile(index_path, repository_index);

    // The build trusts only the official key: validated against its defaults, the index's verified asset is refused.
    const auto untrusted_validation = an::ui::extensions::validate_extension_repository_index_file(index_path);
    EXPECT_FALSE(untrusted_validation.valid);
    EXPECT_NE(untrusted_validation.diagnostic.find("not trusted"), std::string::npos)
        << untrusted_validation.diagnostic;

    const auto validation = an::ui::extensions::validate_extension_repository_index_file(index_path, caseSigningKeys());
    ASSERT_TRUE(validation.valid) << validation.diagnostic;
    const auto& signed_asset = validation.index.packages.front().releases.front().assets.front();
    EXPECT_EQ(signed_asset.signature_state, ExtensionPackageSignatureState::Verified);
    EXPECT_EQ(signed_asset.signature_algorithm, "ed25519");
    EXPECT_EQ(signed_asset.signature_key_id, "afternight-test-ed25519-v1");
    EXPECT_FALSE(signed_asset.signature.empty());

    QJsonObject unknown_key_asset = asset;
    unknown_key_asset.insert(QStringLiteral("signature_key_id"), QStringLiteral("unknown-key"));
    QJsonObject unknown_key_release = release_object;
    unknown_key_release.insert(QStringLiteral("assets"), QJsonArray{unknown_key_asset});
    QJsonObject unknown_key_package = package_object;
    unknown_key_package.insert(QStringLiteral("releases"), QJsonArray{unknown_key_release});
    QJsonObject unknown_key_index = repository_index;
    unknown_key_index.insert(QStringLiteral("extensions"), QJsonArray{unknown_key_package});
    const auto unknown_key_path = temp_dir.path() / "unknown-key-index.json";
    writeJsonFile(unknown_key_path, unknown_key_index);
    const auto unknown_key_validation =
        an::ui::extensions::validate_extension_repository_index_file(unknown_key_path, caseSigningKeys());
    EXPECT_FALSE(unknown_key_validation.valid);
    EXPECT_NE(unknown_key_validation.diagnostic.find("not trusted"), std::string::npos);

    QJsonObject malformed_asset = asset;
    malformed_asset.insert(QStringLiteral("signature"), QStringLiteral("not base64"));
    QJsonObject malformed_release = release_object;
    malformed_release.insert(QStringLiteral("assets"), QJsonArray{malformed_asset});
    QJsonObject malformed_package = package_object;
    malformed_package.insert(QStringLiteral("releases"), QJsonArray{malformed_release});
    QJsonObject malformed_index = repository_index;
    malformed_index.insert(QStringLiteral("extensions"), QJsonArray{malformed_package});
    const auto malformed_path = temp_dir.path() / "malformed-signature-index.json";
    writeJsonFile(malformed_path, malformed_index);
    const auto malformed_validation =
        an::ui::extensions::validate_extension_repository_index_file(malformed_path, caseSigningKeys());
    EXPECT_FALSE(malformed_validation.valid);
    EXPECT_NE(malformed_validation.diagnostic.find("base64"), std::string::npos);
}

// The repository index accepts a protocol-backend package.
TEST(ExtensionManagerTest, RepositoryIndexValidatorAcceptsProtocolSdkBackend)
{
    ScopedTempDirectory temp_dir;
    const auto repository_root = temp_dir.path() / "repository";
    const auto index_path = repository_root / "index.json";

    QJsonObject asset;
    asset.insert(QStringLiteral("name"), QStringLiteral("repo_protocol_ready-1.0.0.tar"));
    asset.insert(QStringLiteral("download_url"), QStringLiteral("archives/repo_protocol_ready-1.0.0.tar"));
    asset.insert(QStringLiteral("package_hash"),
                 QStringLiteral("sha256:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"));
    asset.insert(QStringLiteral("signature_state"), QStringLiteral("unsigned"));

    QJsonArray assets;
    assets.append(asset);

    QJsonObject release_object;
    release_object.insert(QStringLiteral("version"), QStringLiteral("1.0.0"));
    release_object.insert(QStringLiteral("min_app_version"), QStringLiteral("0.0.0"));
    release_object.insert(QStringLiteral("package_format_version"), 1);
    release_object.insert(QStringLiteral("protocol_version"), 1);
    release_object.insert(QStringLiteral("sdk_version"), 1);
    release_object.insert(QStringLiteral("sdk_backend"), QStringLiteral("protocol"));
    release_object.insert(QStringLiteral("launch_mode"), QStringLiteral("single_image"));
    release_object.insert(QStringLiteral("runtime_targets"), jsonStringArray({currentRuntimeTarget()}));
    release_object.insert(QStringLiteral("assets"), assets);

    QJsonArray releases;
    releases.append(release_object);

    QJsonObject package_object;
    package_object.insert(QStringLiteral("id"), QStringLiteral("repo.protocol.ready"));
    package_object.insert(QStringLiteral("name"), QStringLiteral("Repository Protocol Ready"));
    package_object.insert(QStringLiteral("summary"), QStringLiteral("Repository protocol fixture."));
    package_object.insert(QStringLiteral("description"), QStringLiteral("Repository protocol fixture description."));
    package_object.insert(QStringLiteral("author"), QStringLiteral("AfterNight Tests"));
    package_object.insert(QStringLiteral("license"), QStringLiteral("Apache-2.0"));
    package_object.insert(QStringLiteral("publisher_id"), QStringLiteral("afternight.tests"));
    package_object.insert(QStringLiteral("latest_version"), QStringLiteral("1.0.0"));
    package_object.insert(QStringLiteral("releases"), releases);

    QJsonArray packages;
    packages.append(package_object);

    QJsonObject repository_index;
    repository_index.insert(QStringLiteral("schema_version"), 1);
    repository_index.insert(QStringLiteral("repository"), QStringLiteral("afternight-extension-fixture"));
    repository_index.insert(QStringLiteral("official"), true);
    repository_index.insert(QStringLiteral("updated_at"), QStringLiteral("2026-04-26T00:00:00Z"));
    repository_index.insert(QStringLiteral("extensions"), packages);
    writeJsonFile(index_path, repository_index);

    const auto validation = an::ui::extensions::validate_extension_repository_index_file(index_path);

    ASSERT_TRUE(validation.valid) << validation.diagnostic;
    ASSERT_EQ(validation.index.packages.size(), 1u);
    ASSERT_EQ(validation.index.packages.front().releases.size(), 1u);
    EXPECT_EQ(validation.index.packages.front().releases.front().sdk_backend, "protocol");
}

// A release that names no minimum AfterNight version is compatible.
TEST(ExtensionManagerTest, RepositoryIndexValidatorTreatsMissingMinimumAfterNightVersionAsCompatible)
{
    ScopedTempDirectory temp_dir;
    const auto index_path = temp_dir.path() / "index.json";

    QJsonObject asset;
    asset.insert(QStringLiteral("name"), QStringLiteral("repo_no_minimum-1.0.0.tar"));
    asset.insert(QStringLiteral("download_url"), QStringLiteral("archives/repo_no_minimum-1.0.0.tar"));
    asset.insert(QStringLiteral("package_hash"),
                 QStringLiteral("sha256:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"));
    asset.insert(QStringLiteral("signature_state"), QStringLiteral("unsigned"));

    QJsonObject release_object;
    release_object.insert(QStringLiteral("version"), QStringLiteral("1.0.0"));
    release_object.insert(QStringLiteral("package_format_version"), 1);
    release_object.insert(QStringLiteral("protocol_version"), 1);
    release_object.insert(QStringLiteral("sdk_version"), 1);
    release_object.insert(QStringLiteral("sdk_backend"), QStringLiteral("runtime"));
    release_object.insert(QStringLiteral("launch_mode"), QStringLiteral("single_image"));
    release_object.insert(QStringLiteral("runtime_targets"), jsonStringArray({currentRuntimeTarget()}));
    release_object.insert(QStringLiteral("assets"), QJsonArray{asset});

    QJsonObject package_object;
    package_object.insert(QStringLiteral("id"), QStringLiteral("repo.no.minimum"));
    package_object.insert(QStringLiteral("name"), QStringLiteral("Repository No Minimum"));
    package_object.insert(QStringLiteral("summary"), QStringLiteral("No minimum host fixture."));
    package_object.insert(QStringLiteral("description"), QStringLiteral("No minimum host fixture description."));
    package_object.insert(QStringLiteral("author"), QStringLiteral("AfterNight Tests"));
    package_object.insert(QStringLiteral("license"), QStringLiteral("MIT"));
    package_object.insert(QStringLiteral("latest_version"), QStringLiteral("1.0.0"));
    package_object.insert(QStringLiteral("releases"), QJsonArray{release_object});

    QJsonObject repository_index;
    repository_index.insert(QStringLiteral("schema_version"), 1);
    repository_index.insert(QStringLiteral("repository"), QStringLiteral("afternight-extension-fixture"));
    repository_index.insert(QStringLiteral("official"), true);
    repository_index.insert(QStringLiteral("updated_at"), QStringLiteral("2026-04-26T00:00:00Z"));
    repository_index.insert(QStringLiteral("extensions"), QJsonArray{package_object});
    writeJsonFile(index_path, repository_index);

    const auto validation = an::ui::extensions::validate_extension_repository_index_file(index_path);
    ASSERT_TRUE(validation.valid) << validation.diagnostic;
    ASSERT_EQ(validation.index.packages.size(), 1u);
    ASSERT_EQ(validation.index.packages.front().releases.size(), 1u);
    EXPECT_TRUE(validation.index.packages.front().releases.front().min_app_version.empty());

    an::ui::extensions::ExtensionRepositoryBrowseOptions options;
    options.context = defaultContext();
    options.context.app_version = "0.1.0";
    options.context.runtime_target = currentRuntimeTarget();
    const auto entries = an::ui::extensions::build_extension_repository_browse_entries(validation.index, {}, options);

    ASSERT_EQ(entries.size(), 1u);
    EXPECT_TRUE(entries.front().runtime_compatible);
    EXPECT_EQ(entries.front().install_state, ExtensionRepositoryInstallState::NotInstalled);
}

// The repository client loads a local index and resolves each asset's address against it.
TEST(ExtensionManagerTest, OfficialRepositoryClientLoadsLocalIndexAndResolvesAssetUrls)
{
    ScopedTempDirectory temp_dir;
    const auto repository_root = temp_dir.path() / "repository";
    const auto index_path = repository_root / "index.json";

    QJsonObject asset;
    asset.insert(QStringLiteral("name"), QStringLiteral("repo_client_ready-1.0.0.tar.zst"));
    asset.insert(QStringLiteral("download_url"), QStringLiteral("archives/repo_client_ready-1.0.0.tar.zst"));
    asset.insert(QStringLiteral("package_hash"),
                 QStringLiteral("sha256:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"));
    asset.insert(QStringLiteral("signature_state"), QStringLiteral("unsigned"));

    QJsonArray assets;
    assets.append(asset);

    QJsonObject release_object;
    release_object.insert(QStringLiteral("version"), QStringLiteral("1.0.0"));
    release_object.insert(QStringLiteral("min_app_version"), QStringLiteral("0.0.0"));
    release_object.insert(QStringLiteral("package_format_version"), 1);
    release_object.insert(QStringLiteral("protocol_version"), 1);
    release_object.insert(QStringLiteral("sdk_version"), 1);
    release_object.insert(QStringLiteral("sdk_backend"), QStringLiteral("runtime"));
    release_object.insert(QStringLiteral("launch_mode"), QStringLiteral("single_image"));
    release_object.insert(QStringLiteral("runtime_targets"), jsonStringArray({currentRuntimeTarget()}));
    release_object.insert(QStringLiteral("assets"), assets);

    QJsonArray releases;
    releases.append(release_object);

    QJsonObject package_object;
    package_object.insert(QStringLiteral("id"), QStringLiteral("repo.client.ready"));
    package_object.insert(QStringLiteral("name"), QStringLiteral("Repository Client Ready"));
    package_object.insert(QStringLiteral("summary"), QStringLiteral("Repository client fixture."));
    package_object.insert(QStringLiteral("description"), QStringLiteral("Fixture package for client validation."));
    package_object.insert(QStringLiteral("author"), QStringLiteral("AfterNight Tests"));
    package_object.insert(QStringLiteral("license"), QStringLiteral("MIT"));
    package_object.insert(QStringLiteral("publisher_id"), QStringLiteral("afternight.tests"));
    package_object.insert(QStringLiteral("latest_version"), QStringLiteral("1.0.0"));
    package_object.insert(QStringLiteral("tags"), jsonStringArray({"fixture", "client"}));
    package_object.insert(QStringLiteral("releases"), releases);

    QJsonArray packages;
    packages.append(package_object);

    QJsonObject repository_index;
    repository_index.insert(QStringLiteral("schema_version"), 1);
    repository_index.insert(QStringLiteral("repository"), QStringLiteral("afternight-extension-client-fixture"));
    repository_index.insert(QStringLiteral("official"), true);
    repository_index.insert(QStringLiteral("updated_at"), QStringLiteral("2026-04-27T00:00:00Z"));
    repository_index.insert(QStringLiteral("extensions"), packages);
    writeJsonFile(index_path, repository_index);

    an::ui::extensions::ExtensionRepositoryClient client;
    an::ui::extensions::ExtensionRepositoryIndexFetchRequest request;
    request.index_url = QUrl::fromLocalFile(QString::fromStdString(index_path.string()));

    const auto result = client.fetchIndex(request);
    ASSERT_TRUE(result.success) << result.diagnostic;
    ASSERT_TRUE(result.validation.valid) << result.validation.diagnostic;
    ASSERT_EQ(result.validation.index.packages.size(), 1u);
    EXPECT_EQ(result.validation.index.packages.front().id, "repo.client.ready");

    const QUrl resolved_asset = an::ui::extensions::resolve_extension_repository_asset_url(
        result.index_url, result.validation.index.packages.front().releases.front().assets.front().download_url);
    EXPECT_EQ(resolved_asset, QUrl::fromLocalFile(QString::fromStdString(
                                  (repository_root / "archives" / "repo_client_ready-1.0.0.tar.zst").string())));
}

// A fetched index that does not validate is refused.
TEST(ExtensionManagerTest, OfficialRepositoryClientRejectsInvalidFetchedIndex)
{
    ScopedTempDirectory temp_dir;
    const auto index_path = temp_dir.path() / "index.json";
    writeTextFile(index_path, R"json({"schema_version": 1, "repository": "broken"})json");

    an::ui::extensions::ExtensionRepositoryClient client;
    an::ui::extensions::ExtensionRepositoryIndexFetchRequest request;
    request.index_url = QUrl::fromLocalFile(QString::fromStdString(index_path.string()));

    const auto result = client.fetchIndex(request);
    EXPECT_FALSE(result.success);
    EXPECT_FALSE(result.validation.valid);
    EXPECT_FALSE(result.diagnostic.empty());
}

// An index fetch asks the network for a fresh copy rather than a cached one.
TEST(ExtensionManagerTest, OfficialRepositoryClientBypassesNetworkCachesForIndexFetch)
{
    QTcpServer server;
    ASSERT_TRUE(server.listen(QHostAddress::LocalHost, 0)) << server.errorString().toStdString();

    QByteArray captured_request;
    QObject::connect(&server, &QTcpServer::newConnection, &server, [&]() {
        QTcpSocket* socket = server.nextPendingConnection();
        ASSERT_NE(socket, nullptr);

        QObject::connect(socket, &QTcpSocket::readyRead, socket, [socket, &captured_request]() {
            captured_request += socket->readAll();
            if (!captured_request.contains("\r\n\r\n"))
            {
                return;
            }

            const QByteArray body = QByteArray(R"json({
  "schema_version": 1,
  "repository": "afternight-cache-bypass-fixture",
  "official": true,
  "updated_at": "2026-04-27T00:00:00Z",
  "extensions": []
})json");
            QByteArray response;
            response += "HTTP/1.1 200 OK\r\n";
            response += "Content-Type: application/json\r\n";
            response += "Cache-Control: max-age=3600\r\n";
            response += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
            response += "Connection: close\r\n\r\n";
            response += body;
            socket->write(response);
            socket->disconnectFromHost();
        });
    });

    an::ui::extensions::ExtensionRepositoryClient client;
    an::ui::extensions::ExtensionRepositoryIndexFetchRequest request;
    request.index_url = QUrl(QStringLiteral("http://127.0.0.1:%1/index.json").arg(server.serverPort()));
    request.timeout_ms = 3000;

    const auto result = client.fetchIndex(request);
    ASSERT_TRUE(result.success) << result.diagnostic;
    EXPECT_EQ(result.index_url, request.index_url);
    ASSERT_FALSE(captured_request.isEmpty());

    const QByteArray lowered_request = captured_request.toLower();
    EXPECT_TRUE(lowered_request.startsWith("get /index.json?")) << captured_request.toStdString();
    EXPECT_TRUE(lowered_request.contains("_afternight_cache_bust=")) << captured_request.toStdString();
    EXPECT_TRUE(lowered_request.contains("cache-control: no-cache")) << captured_request.toStdString();
    EXPECT_TRUE(lowered_request.contains("pragma: no-cache")) << captured_request.toStdString();
}

// An index fetch in progress stops when it is cancelled.
TEST(ExtensionManagerTest, OfficialRepositoryClientCancelsPendingNetworkFetch)
{
    QTcpServer server;
    ASSERT_TRUE(server.listen(QHostAddress::LocalHost, 0)) << server.errorString().toStdString();

    QObject::connect(&server, &QTcpServer::newConnection, &server, [&]() {
        QTcpSocket* socket = server.nextPendingConnection();
        ASSERT_NE(socket, nullptr);
        QObject::connect(socket, &QTcpSocket::readyRead, socket, [socket]() { socket->readAll(); });
    });

    an::ui::extensions::ExtensionRepositoryClient client;
    an::ui::extensions::ExtensionRepositoryIndexFetchRequest request;
    request.index_url = QUrl(QStringLiteral("http://127.0.0.1:%1/index.json").arg(server.serverPort()));
    request.timeout_ms = 5000;

    const an::ui::extensions::ExtensionOperationCancellationToken cancellation_token;
    request.cancellation_token = cancellation_token;
    QTimer::singleShot(50, [cancellation_token]() { cancellation_token.requestCancellation(); });

    QElapsedTimer timer;
    timer.start();
    const auto result = client.fetchIndex(request);

    EXPECT_FALSE(result.success);
    EXPECT_NE(result.diagnostic.find("cancelled"), std::string::npos);
    EXPECT_LT(timer.elapsed(), 1000);
}

// An index wrapped in the GitHub contents API's envelope is decoded.
TEST(ExtensionManagerTest, OfficialRepositoryClientDecodesGitHubContentsApiWrappedIndex)
{
    QTcpServer server;
    ASSERT_TRUE(server.listen(QHostAddress::LocalHost, 0)) << server.errorString().toStdString();

    const QByteArray index_body = QByteArray(R"json({
  "schema_version": 1,
  "repository": "afternight-github-api-fixture",
  "official": true,
  "updated_at": "2026-05-09T00:00:00Z",
  "extensions": []
})json");

    QByteArray captured_request;
    QObject::connect(&server, &QTcpServer::newConnection, &server, [&]() {
        QTcpSocket* socket = server.nextPendingConnection();
        ASSERT_NE(socket, nullptr);

        QObject::connect(socket, &QTcpSocket::readyRead, socket, [socket, index_body, &captured_request]() {
            captured_request += socket->readAll();
            if (!captured_request.contains("\r\n\r\n"))
            {
                return;
            }

            QJsonObject body;
            body.insert(QStringLiteral("type"), QStringLiteral("file"));
            body.insert(QStringLiteral("encoding"), QStringLiteral("base64"));
            body.insert(QStringLiteral("name"), QStringLiteral("index.json"));
            body.insert(QStringLiteral("path"), QStringLiteral("index.json"));
            QString encoded_content = QString::fromLatin1(index_body.toBase64());
            if (encoded_content.size() > 76)
            {
                encoded_content.insert(76, QChar('\n'));
            }
            body.insert(QStringLiteral("content"), encoded_content);

            const QByteArray payload = QJsonDocument(body).toJson(QJsonDocument::Compact);
            QByteArray response;
            response += "HTTP/1.1 200 OK\r\n";
            response += "Content-Type: application/json\r\n";
            response += "Content-Length: " + QByteArray::number(payload.size()) + "\r\n";
            response += "Connection: close\r\n\r\n";
            response += payload;
            socket->write(response);
            socket->disconnectFromHost();
        });
    });

    an::ui::extensions::ExtensionRepositoryClient client;
    an::ui::extensions::ExtensionRepositoryIndexFetchRequest request;
    request.index_url = QUrl(
        QStringLiteral("http://127.0.0.1:%1/repos/owner/repo/contents/index.json?ref=live").arg(server.serverPort()));
    request.timeout_ms = 3000;

    const auto result = client.fetchIndex(request);

    ASSERT_TRUE(result.success) << result.diagnostic;
    ASSERT_TRUE(result.validation.valid) << result.validation.diagnostic;
    EXPECT_EQ(result.validation.index.repository, "afternight-github-api-fixture");
}

// The official index address is the published feed, not a branch's raw file.
TEST(ExtensionManagerTest, OfficialRepositoryIndexUrlUsesLivePublishedFeed)
{
    const QUrl url = an::ui::extensions::official_extension_repository_index_url();

    EXPECT_EQ(
        url.toString(),
        QStringLiteral("https://api.github.com/repos/emruiz81/afternight-extensions/contents/index.json?ref=live"));
    EXPECT_FALSE(url.toString().contains(QStringLiteral("/main/index.json")));
    EXPECT_FALSE(url.toString().contains(QStringLiteral("raw.githubusercontent.com")));

    const QUrl resolved_asset =
        an::ui::extensions::resolve_extension_repository_asset_url(url, "archives/example-1.0.0.tar.zst");
    EXPECT_EQ(
        resolved_asset.toString(),
        QStringLiteral(
            "https://raw.githubusercontent.com/emruiz81/afternight-extensions/live/archives/example-1.0.0.tar.zst"));
}

// A browse entry offers the asset for this runtime target and says whether it installs or updates.
TEST(ExtensionManagerTest, RepositoryBrowseEntriesSelectRuntimeAssetAndUpdateState)
{
    an::ui::extensions::ExtensionRepositoryReleaseAsset linux_asset;
    linux_asset.name = "repo_client_ready-1.1.0-linux.tar.zst";
    linux_asset.download_url = "archives/repo_client_ready-1.1.0-linux.tar.zst";
    linux_asset.package_hash_sha256 = std::string(64, 'a');
    linux_asset.runtime_targets = {"linux-clang-x86_64"};
    linux_asset.signature_state = ExtensionPackageSignatureState::Unsigned;
    linux_asset.signature_detail = "unsigned fixture";

    an::ui::extensions::ExtensionRepositoryReleaseAsset windows_asset = linux_asset;
    windows_asset.name = "repo_client_ready-1.1.0-windows.tar.zst";
    windows_asset.download_url = "archives/repo_client_ready-1.1.0-windows.tar.zst";
    windows_asset.runtime_targets = {"windows-msvc-x86_64"};

    an::ui::extensions::ExtensionRepositoryRelease release;
    release.version = "1.1.0";
    release.runtime_targets = {"linux-clang-x86_64", "windows-msvc-x86_64"};
    release.assets = {windows_asset, linux_asset};

    an::ui::extensions::ExtensionRepositoryPackage package;
    package.id = "repo.client.ready";
    package.name = "Repository Client Ready";
    package.summary = "A repository browse fixture.";
    package.description = "Repository browse fixture description.";
    package.author = "AfterNight Tests";
    package.publisher_id = "afternight.tests";
    package.license = "MIT";
    package.attribution = "AfterNight port of Repository Client Ready, originally authored by Fixture Author.";
    package.original_author = "Fixture Author";
    package.original_project = "Fixture Suite";
    package.original_source_url = "https://example.invalid/upstream/repo-client-ready.py";
    package.upstream_commit = "fedcba9876543210";
    package.homepage_url = "https://example.invalid/repo-client-ready";
    package.repository_url = "https://github.com/example/repo-client-ready";
    package.support_url = "https://example.invalid/repo-client-ready/support";
    package.icon_url = "assets/repo-client-ready.png";
    package.latest_version = "1.1.0";
    package.tags = {"fixture", "denoise"};
    package.releases = {release};
    package.releases.front().min_app_version = "2.0.0";
    package.releases.front().package_format_version = 1;
    package.releases.front().protocol_version = 1;
    package.releases.front().sdk_version = 1;
    package.releases.front().sdk_backend = "runtime";
    package.releases.front().launch_mode = "single_image";
    package.releases.front().changelog = "Adds the repository metadata fixture.";
    package.releases.front().published_at_utc = "2026-04-27T00:00:00Z";

    an::ui::extensions::ExtensionRepositoryIndex index;
    index.packages = {package};

    an::ui::extensions::ExtensionManifest installed_manifest;
    installed_manifest.id = "repo.client.ready";
    installed_manifest.version = "1.0.0";
    an::ui::extensions::ExtensionLoadResult installed;
    installed.status = ExtensionLoadStatus::Ready;
    installed.source = ExtensionPackageSource::Repository;
    installed.manifest = installed_manifest;
    ExtensionEnvironmentSnapshot installed_environment;
    installed_environment.state = ExtensionEnvironmentState::Bootstrapping;
    installed_environment.diagnostic = "Provisioning repository wheelhouse";
    installed.environment = installed_environment;

    an::ui::extensions::ExtensionRepositoryBrowseOptions options;
    options.context = defaultContext();
    options.context.runtime_target = "linux-clang-x86_64";

    const auto entries = an::ui::extensions::build_extension_repository_browse_entries(index, {installed}, options);

    ASSERT_EQ(entries.size(), 1u);
    EXPECT_EQ(entries.front().id, "repo.client.ready");
    EXPECT_TRUE(entries.front().runtime_compatible);
    EXPECT_EQ(entries.front().selected_asset_name, linux_asset.name);
    EXPECT_EQ(entries.front().selected_asset_download_url, linux_asset.download_url);
    EXPECT_EQ(entries.front().selected_asset_package_hash_sha256, linux_asset.package_hash_sha256);
    EXPECT_EQ(entries.front().selected_asset_signature_state, ExtensionPackageSignatureState::Unsigned);
    EXPECT_EQ(entries.front().attribution,
              "AfterNight port of Repository Client Ready, originally authored by Fixture Author.");
    EXPECT_EQ(entries.front().original_author, "Fixture Author");
    EXPECT_EQ(entries.front().original_project, "Fixture Suite");
    EXPECT_EQ(entries.front().original_source_url, "https://example.invalid/upstream/repo-client-ready.py");
    EXPECT_EQ(entries.front().upstream_commit, "fedcba9876543210");
    EXPECT_EQ(entries.front().homepage_url, package.homepage_url);
    EXPECT_EQ(entries.front().repository_url, package.repository_url);
    EXPECT_EQ(entries.front().support_url, package.support_url);
    EXPECT_EQ(entries.front().icon_url, package.icon_url);
    EXPECT_EQ(entries.front().release_min_app_version, "2.0.0");
    EXPECT_EQ(entries.front().release_package_format_version, 1);
    EXPECT_EQ(entries.front().release_protocol_version, 1);
    EXPECT_EQ(entries.front().release_sdk_version, 1);
    EXPECT_EQ(entries.front().release_sdk_backend, "runtime");
    EXPECT_EQ(entries.front().release_launch_mode, "single_image");
    EXPECT_EQ(entries.front().release_runtime_targets, release.runtime_targets);
    EXPECT_EQ(entries.front().release_changelog, "Adds the repository metadata fixture.");
    EXPECT_EQ(entries.front().release_published_at_utc, "2026-04-27T00:00:00Z");
    EXPECT_EQ(entries.front().selected_asset_runtime_targets, linux_asset.runtime_targets);
    EXPECT_EQ(entries.front().installed_version, "1.0.0");
    EXPECT_EQ(entries.front().install_state, an::ui::extensions::ExtensionRepositoryInstallState::UpdateAvailable);
    EXPECT_TRUE(entries.front().installed_status_available);
    EXPECT_EQ(entries.front().installed_load_status, ExtensionLoadStatus::Ready);
    EXPECT_TRUE(entries.front().installed_environment_available);
    EXPECT_EQ(entries.front().installed_environment_state, ExtensionEnvironmentState::Bootstrapping);
    EXPECT_EQ(entries.front().installed_status_diagnostic, "Provisioning repository wheelhouse");
    EXPECT_TRUE(entries.front().installed_source_available);
    EXPECT_EQ(entries.front().installed_source, ExtensionPackageSource::Repository);
}

// A browse entry says where an installed copy of the package came from.
TEST(ExtensionManagerTest, RepositoryBrowseEntriesDistinguishInstalledPackageSources)
{
    const auto make_asset = [](const std::string& id) {
        an::ui::extensions::ExtensionRepositoryReleaseAsset asset;
        asset.name = id + "-1.1.0-linux.tar.zst";
        asset.download_url = "archives/" + asset.name;
        asset.package_hash_sha256 = std::string(64, 'a');
        asset.runtime_targets = {"linux-clang-x86_64"};
        asset.signature_state = ExtensionPackageSignatureState::Unsigned;
        return asset;
    };

    const auto make_package = [&](const std::string& id) {
        an::ui::extensions::ExtensionRepositoryRelease release;
        release.version = "1.1.0";
        release.runtime_targets = {"linux-clang-x86_64"};
        release.assets = {make_asset(id)};

        an::ui::extensions::ExtensionRepositoryPackage package;
        package.id = id;
        package.name = id;
        package.summary = "Repository source model fixture.";
        package.author = "AfterNight Tests";
        package.publisher_id = "afternight.tests";
        package.license = "MIT";
        package.latest_version = "1.1.0";
        package.releases = {release};
        return package;
    };

    an::ui::extensions::ExtensionRepositoryIndex index;
    index.packages = {make_package("repo.installed"), make_package("bundled.installed"),
                      make_package("user.installed")};

    const auto make_installed = [](const std::string& id, const ExtensionPackageSource source,
                                   const ExtensionEnvironmentState environment_state) {
        an::ui::extensions::ExtensionManifest manifest;
        manifest.id = id;
        manifest.version = "1.0.0";

        an::ui::extensions::ExtensionLoadResult result;
        result.status = ExtensionLoadStatus::Ready;
        result.source = source;
        result.manifest = manifest;

        ExtensionEnvironmentSnapshot environment;
        environment.state = environment_state;
        if (environment_state == ExtensionEnvironmentState::Bootstrapping)
        {
            environment.diagnostic = "Bundled post-install bootstrap is running";
        }
        result.environment = environment;
        return result;
    };

    an::ui::extensions::ExtensionRepositoryBrowseOptions options;
    options.context = defaultContext();
    options.context.runtime_target = "linux-clang-x86_64";

    const auto entries = an::ui::extensions::build_extension_repository_browse_entries(
        index,
        {make_installed("repo.installed", ExtensionPackageSource::Repository, ExtensionEnvironmentState::Ready),
         make_installed("bundled.installed", ExtensionPackageSource::Bundled, ExtensionEnvironmentState::Bootstrapping),
         make_installed("user.installed", ExtensionPackageSource::User, ExtensionEnvironmentState::Ready)},
        options);

    ASSERT_EQ(entries.size(), 3u);
    const auto find_entry = [&](const std::string& id) {
        return std::find_if(entries.begin(), entries.end(), [&](const auto& entry) { return entry.id == id; });
    };

    const auto repo_it = find_entry("repo.installed");
    ASSERT_NE(repo_it, entries.end());
    EXPECT_EQ(repo_it->install_state, ExtensionRepositoryInstallState::UpdateAvailable);
    EXPECT_TRUE(repo_it->installed_source_available);
    EXPECT_EQ(repo_it->installed_source, ExtensionPackageSource::Repository);

    const auto bundled_it = find_entry("bundled.installed");
    ASSERT_NE(bundled_it, entries.end());
    EXPECT_EQ(bundled_it->install_state, ExtensionRepositoryInstallState::Installed);
    EXPECT_TRUE(bundled_it->installed_source_available);
    EXPECT_EQ(bundled_it->installed_source, ExtensionPackageSource::Bundled);
    EXPECT_TRUE(bundled_it->installed_environment_available);
    EXPECT_EQ(bundled_it->installed_environment_state, ExtensionEnvironmentState::Bootstrapping);
    EXPECT_EQ(bundled_it->installed_status_diagnostic, "Bundled post-install bootstrap is running");

    const auto user_it = find_entry("user.installed");
    ASSERT_NE(user_it, entries.end());
    EXPECT_EQ(user_it->install_state, ExtensionRepositoryInstallState::Installed);
    EXPECT_TRUE(user_it->installed_source_available);
    EXPECT_EQ(user_it->installed_source, ExtensionPackageSource::User);
}

// Browse entries are filtered as asked, and a package with no compatible release is marked so.
TEST(ExtensionManagerTest, RepositoryBrowseEntriesFilterAndMarkIncompatiblePackages)
{
    an::ui::extensions::ExtensionRepositoryReleaseAsset windows_asset;
    windows_asset.name = "windows_only-1.0.0-windows.tar.zst";
    windows_asset.download_url = "archives/windows_only-1.0.0-windows.tar.zst";
    windows_asset.package_hash_sha256 = std::string(64, 'b');
    windows_asset.runtime_targets = {"windows-msvc-x86_64"};

    an::ui::extensions::ExtensionRepositoryRelease windows_release;
    windows_release.version = "1.0.0";
    windows_release.runtime_targets = {"windows-msvc-x86_64"};
    windows_release.assets = {windows_asset};

    an::ui::extensions::ExtensionRepositoryPackage windows_package;
    windows_package.id = "windows.only";
    windows_package.name = "Windows Only";
    windows_package.summary = "A platform-specific repository fixture.";
    windows_package.author = "AfterNight Tests";
    windows_package.publisher_id = "afternight.tests";
    windows_package.license = "MIT";
    windows_package.latest_version = "1.0.0";
    windows_package.tags = {"platform"};
    windows_package.releases = {windows_release};

    an::ui::extensions::ExtensionRepositoryReleaseAsset linux_asset;
    linux_asset.name = "astro_tools-1.0.0-linux.tar.zst";
    linux_asset.download_url = "archives/astro_tools-1.0.0-linux.tar.zst";
    linux_asset.package_hash_sha256 = std::string(64, 'c');
    linux_asset.runtime_targets = {"linux-clang-x86_64"};

    an::ui::extensions::ExtensionRepositoryRelease linux_release;
    linux_release.version = "1.0.0";
    linux_release.runtime_targets = {"linux-clang-x86_64"};
    linux_release.assets = {linux_asset};

    an::ui::extensions::ExtensionRepositoryPackage linux_package;
    linux_package.id = "astro.tools";
    linux_package.name = "Astro Tools";
    linux_package.summary = "A searchable repository fixture.";
    linux_package.author = "Deep Sky Maintainers";
    linux_package.publisher_id = "afternight.tests";
    linux_package.license = "MIT";
    linux_package.latest_version = "1.0.0";
    linux_package.tags = {"stacking", "calibration"};
    linux_package.releases = {linux_release};

    an::ui::extensions::ExtensionRepositoryIndex index;
    index.packages = {windows_package, linux_package};

    an::ui::extensions::ExtensionRepositoryBrowseOptions options;
    options.context = defaultContext();
    options.context.runtime_target = "linux-clang-x86_64";
    options.search_query = "deep sky";

    const auto filtered_entries = an::ui::extensions::build_extension_repository_browse_entries(index, {}, options);
    ASSERT_EQ(filtered_entries.size(), 1u);
    EXPECT_EQ(filtered_entries.front().id, "astro.tools");
    EXPECT_EQ(filtered_entries.front().install_state,
              an::ui::extensions::ExtensionRepositoryInstallState::NotInstalled);

    options.search_query = "calibration";
    const auto tag_filtered_entries = an::ui::extensions::build_extension_repository_browse_entries(index, {}, options);
    ASSERT_EQ(tag_filtered_entries.size(), 1u);
    EXPECT_EQ(tag_filtered_entries.front().id, "astro.tools");

    options.search_query = "windows";
    const auto name_filtered_entries =
        an::ui::extensions::build_extension_repository_browse_entries(index, {}, options);
    ASSERT_EQ(name_filtered_entries.size(), 1u);
    EXPECT_EQ(name_filtered_entries.front().id, "windows.only");

    options.search_query.clear();
    const auto all_entries = an::ui::extensions::build_extension_repository_browse_entries(index, {}, options);
    ASSERT_EQ(all_entries.size(), 2u);
    const auto windows_it = std::find_if(all_entries.begin(), all_entries.end(),
                                         [](const auto& entry) { return entry.id == "windows.only"; });
    ASSERT_NE(windows_it, all_entries.end());
    EXPECT_FALSE(windows_it->runtime_compatible);
    EXPECT_TRUE(windows_it->selected_asset_name.empty());
    EXPECT_EQ(windows_it->install_state, an::ui::extensions::ExtensionRepositoryInstallState::Incompatible);
    EXPECT_NE(windows_it->compatibility_diagnostic.find("linux-clang-x86_64"), std::string::npos);
}

// A release that needs a newer AfterNight is shown, disabled, saying which version it needs.
TEST(ExtensionManagerTest, RepositoryBrowseEntriesShowButDisableReleasesRequiringNewerAfterNight)
{
    an::ui::extensions::ExtensionRepositoryReleaseAsset asset;
    asset.name = "future_host-1.0.0-linux.tar.zst";
    asset.download_url = "archives/future_host-1.0.0-linux.tar.zst";
    asset.package_hash_sha256 = std::string(64, 'd');
    asset.runtime_targets = {currentRuntimeTarget()};
    asset.signature_state = ExtensionPackageSignatureState::Unsigned;

    an::ui::extensions::ExtensionRepositoryRelease release;
    release.version = "1.0.0";
    release.min_app_version = "2.5.0";
    release.package_format_version = 1;
    release.protocol_version = 1;
    release.sdk_version = 1;
    release.sdk_backend = "runtime";
    release.launch_mode = "single_image";
    release.runtime_targets = {currentRuntimeTarget()};
    release.assets = {asset};

    an::ui::extensions::ExtensionRepositoryPackage package;
    package.id = "future.host";
    package.name = "Future Host";
    package.summary = "Requires a newer host.";
    package.description = "Requires a newer host description.";
    package.author = "AfterNight Tests";
    package.license = "MIT";
    package.latest_version = "1.0.0";
    package.releases = {release};

    an::ui::extensions::ExtensionRepositoryIndex index;
    index.packages = {package};

    an::ui::extensions::ExtensionRepositoryBrowseOptions options;
    options.context = defaultContext();
    options.context.app_version = "2.4.9";
    options.context.runtime_target = currentRuntimeTarget();

    const auto entries = an::ui::extensions::build_extension_repository_browse_entries(index, {}, options);

    ASSERT_EQ(entries.size(), 1u);
    EXPECT_EQ(entries.front().id, "future.host");
    EXPECT_EQ(entries.front().release_min_app_version, "2.5.0");
    EXPECT_FALSE(entries.front().runtime_compatible);
    EXPECT_TRUE(entries.front().selected_asset_name.empty());
    EXPECT_EQ(entries.front().install_state, ExtensionRepositoryInstallState::Incompatible);
    EXPECT_NE(entries.front().compatibility_diagnostic.find("Requires AfterNight 2.5.0"), std::string::npos);
    EXPECT_NE(entries.front().compatibility_diagnostic.find("2.4.9"), std::string::npos);
}

// Installing and then updating from a repository index replaces the package in one step each time.
TEST(ExtensionManagerTest, FixtureRepositoryIndexInstallAndUpdateReplacePackageAtomically)
{
    ScopedTempDirectory temp_dir;
    const auto repository_root = temp_dir.path() / "repository";
    const auto v1_archive = repository_root / "archives" / "repo_index_update-1.0.0.tar";
    const auto v2_archive = repository_root / "archives" / "repo_index_update-1.1.0.tar";
    const std::string v1_manifest = R"json({
      "id": "repo.index.update",
      "name": "Repository Index Update",
      "version": "1.0.0",
      "summary": "Repository update fixture.",
      "description": "Fixture package for update validation.",
      "author": "AfterNight Tests",
      "license": "GPL-3.0-or-later",
      "publisher_id": "afternight.tests",
      "type": "python",
      "entry_point": "repo_index_update",
      "process_class": "RepositoryIndexUpdateExtension",
      "category": "filters",
      "launch_mode": "single_image",
      "package_format_version": 1,
      "protocol_version": 1,
      "sdk_version": 1
    })json";
    const std::string v2_manifest = R"json({
      "id": "repo.index.update",
      "name": "Repository Index Update",
      "version": "1.1.0",
      "summary": "Repository update fixture.",
      "description": "Fixture package for update validation.",
      "author": "AfterNight Tests",
      "license": "GPL-3.0-or-later",
      "publisher_id": "afternight.tests",
      "type": "python",
      "entry_point": "repo_index_update",
      "process_class": "RepositoryIndexUpdateExtension",
      "category": "filters",
      "launch_mode": "single_image",
      "package_format_version": 1,
      "protocol_version": 1,
      "sdk_version": 1
    })json";
    createTarArchive(v1_archive, {{"repo_index_update/extension.json", v1_manifest, '0'},
                                  {"repo_index_update/repo_index_update.py",
                                   "class RepositoryIndexUpdateExtension:\n    VALUE = 'v1'\n", '0'}});
    createTarArchive(v2_archive, {{"repo_index_update/extension.json", v2_manifest, '0'},
                                  {"repo_index_update/repo_index_update.py",
                                   "class RepositoryIndexUpdateExtension:\n    VALUE = 'v2'\n", '0'}});
    const std::string v1_hash = sha256FileHex(v1_archive);
    const std::string v2_hash = sha256FileHex(v2_archive);
    const auto index_path = repository_root / "index.json";
    const auto make_release = [](const std::string& version, const QString& asset_name,
                                 const QString& asset_download_url, const std::string& archive_hash) {
        QJsonObject asset;
        asset.insert(QStringLiteral("name"), asset_name);
        asset.insert(QStringLiteral("download_url"), asset_download_url);
        asset.insert(QStringLiteral("package_hash"), QString::fromStdString("sha256:" + archive_hash));
        asset.insert(QStringLiteral("signature_state"), QStringLiteral("unsigned"));

        QJsonArray assets;
        assets.append(asset);

        QJsonObject release;
        release.insert(QStringLiteral("version"), QString::fromStdString(version));
        release.insert(QStringLiteral("min_app_version"), QStringLiteral("0.0.0"));
        release.insert(QStringLiteral("package_format_version"), 1);
        release.insert(QStringLiteral("protocol_version"), 1);
        release.insert(QStringLiteral("sdk_version"), 1);
        release.insert(QStringLiteral("sdk_backend"), QStringLiteral("runtime"));
        release.insert(QStringLiteral("launch_mode"), QStringLiteral("single_image"));
        release.insert(QStringLiteral("runtime_targets"), jsonStringArray({currentRuntimeTarget()}));
        release.insert(QStringLiteral("assets"), assets);
        return release;
    };

    QJsonArray releases;
    releases.append(make_release("1.0.0", QStringLiteral("repo_index_update-1.0.0.tar"),
                                 QStringLiteral("archives/repo_index_update-1.0.0.tar"), v1_hash));
    releases.append(make_release("1.1.0", QStringLiteral("repo_index_update-1.1.0.tar"),
                                 QStringLiteral("archives/repo_index_update-1.1.0.tar"), v2_hash));

    QJsonObject package_object;
    package_object.insert(QStringLiteral("id"), QStringLiteral("repo.index.update"));
    package_object.insert(QStringLiteral("name"), QStringLiteral("Repository Index Update"));
    package_object.insert(QStringLiteral("summary"), QStringLiteral("Repository update fixture."));
    package_object.insert(QStringLiteral("description"), QStringLiteral("Fixture package for update validation."));
    package_object.insert(QStringLiteral("author"), QStringLiteral("AfterNight Tests"));
    package_object.insert(QStringLiteral("license"), QStringLiteral("GPL-3.0-or-later"));
    package_object.insert(QStringLiteral("publisher_id"), QStringLiteral("afternight.tests"));
    package_object.insert(QStringLiteral("latest_version"), QStringLiteral("1.1.0"));
    package_object.insert(QStringLiteral("tags"), jsonStringArray({"fixture", "update"}));
    package_object.insert(QStringLiteral("releases"), releases);

    QJsonArray packages;
    packages.append(package_object);

    QJsonObject repository_index;
    repository_index.insert(QStringLiteral("schema_version"), 1);
    repository_index.insert(QStringLiteral("repository"), QStringLiteral("afternight-extension-fixture"));
    repository_index.insert(QStringLiteral("official"), true);
    repository_index.insert(QStringLiteral("updated_at"), QStringLiteral("2026-04-26T00:00:00Z"));
    repository_index.insert(QStringLiteral("extensions"), packages);
    writeJsonFile(index_path, repository_index);

    const auto index_validation = an::ui::extensions::validate_extension_repository_index_file(index_path);
    ASSERT_TRUE(index_validation.valid) << index_validation.diagnostic;
    ASSERT_EQ(index_validation.index.packages.size(), 1u);
    const auto& package = index_validation.index.packages.front();
    ASSERT_EQ(package.releases.size(), 2u);

    an::ui::extensions::ExtensionPackageTransactionService service;
    an::ui::extensions::ExtensionPackageInstallRequest install_request;
    install_request.install_root = temp_dir.path() / "installed";
    install_request.context = defaultContext();
    install_request.source = ExtensionPackageSource::Repository;
    install_request.source_repository_url = "https://github.com/afternight/afternight-extensions";
    install_request.require_repository_metadata = true;
    install_request.require_current_runtime_compatibility = true;

    const auto make_request_for_release = [&](const an::ui::extensions::ExtensionRepositoryRelease& release) {
        auto request = install_request;
        const auto& asset = release.assets.front();
        request.archive_path = repository_root / asset.download_url;
        request.expected_archive_hash_sha256 = asset.package_hash_sha256;
        request.selected_release_asset = asset.name;
        request.signature_state = asset.signature_state;
        request.signature_detail = asset.signature_detail;
        return request;
    };

    const auto v1_result = service.installArchive(make_request_for_release(package.releases[0]));
    ASSERT_TRUE(v1_result.success) << v1_result.diagnostic;
    EXPECT_EQ(v1_result.validation.load_result.manifest->version, "1.0.0");
    const auto activated_root = v1_result.activated_package_root;

    auto bad_update = make_request_for_release(package.releases[1]);
    bad_update.expected_archive_hash_sha256 = std::string(64, '0');
    const auto bad_update_result = service.installArchive(bad_update);
    EXPECT_FALSE(bad_update_result.success);
    EXPECT_TRUE(std::filesystem::exists(activated_root / "extension.json"));
    auto still_v1 = an::ui::extensions::load_extension_manifest_file(
        activated_root / "extension.json", ExtensionPackageSource::Repository, defaultContext());
    ASSERT_TRUE(still_v1.manifest.has_value());
    EXPECT_EQ(still_v1.manifest->version, "1.0.0");

    const auto v2_result = service.installArchive(make_request_for_release(package.releases[1]));
    ASSERT_TRUE(v2_result.success) << v2_result.diagnostic;
    EXPECT_EQ(v2_result.activated_package_root, activated_root);
    ASSERT_TRUE(v2_result.validation.load_result.manifest.has_value());
    EXPECT_EQ(v2_result.validation.load_result.manifest->version, "1.1.0");
    const QJsonObject receipt = loadJsonFile(v2_result.activated_package_root / "extension_package_receipt.json");
    EXPECT_EQ(receipt.value(QStringLiteral("extension_version")).toString(), QStringLiteral("1.1.0"));
    EXPECT_EQ(receipt.value(QStringLiteral("source_tier")).toString(), QStringLiteral("repository"));
    EXPECT_EQ(receipt.value(QStringLiteral("archive_hash")).toString(), QString::fromStdString(v2_hash));
    EXPECT_EQ(receipt.value(QStringLiteral("selected_release_asset")).toString(),
              QStringLiteral("repo_index_update-1.1.0.tar"));

    ExtensionDiscoveryOptions options;
    options.repository_roots = {install_request.install_root};
    options.context = defaultContext();
    ExtensionManager manager;
    const auto discovered = manager.discoverExtensions(options);
    ASSERT_EQ(discovered.size(), 1u);
    ASSERT_TRUE(discovered.front().manifest.has_value());
    EXPECT_EQ(discovered.front().source, ExtensionPackageSource::Repository);
    EXPECT_EQ(discovered.front().manifest->version, "1.1.0");
    EXPECT_EQ(discovered.front().provenance.archive_hash_sha256, v2_hash);
}

// A wildcard equality on the Python version matches every patch release of the bundled minor version.
TEST(ExtensionManagerTest, PythonCompatWildcardEqualityMatchesBundledPatchVersion)
{
    ScopedTempDirectory temp_dir;
    const auto package_root = createExtensionPackage(temp_dir.path(), "wildcard_eq",
                                                     R"json({
          "id": "wildcard_eq",
          "name": "Wildcard Equality",
          "version": "1.0.0",
          "type": "python",
          "entry_point": "wildcard_eq",
          "process_class": "WildcardEqualityExtension",
          "category": "filters",
          "launch_mode": "single_image",
          "package_format_version": 1,
          "protocol_version": 1,
          "sdk_version": 1,
          "python_compat": "==3.14.*"
        })json");

    const auto result = an::ui::extensions::load_extension_manifest_file(
        package_root / "extension.json", ExtensionPackageSource::User, defaultContext());

    ASSERT_TRUE(result.manifest.has_value());
    EXPECT_EQ(result.status, ExtensionLoadStatus::Ready);
}

// A wildcard equality on another minor version does not match the bundled Python.
TEST(ExtensionManagerTest, PythonCompatWildcardEqualityRejectsDifferentMinorVersion)
{
    ScopedTempDirectory temp_dir;
    const auto package_root = createExtensionPackage(temp_dir.path(), "wildcard_eq_mismatch",
                                                     R"json({
          "id": "wildcard_eq_mismatch",
          "name": "Wildcard Equality Mismatch",
          "version": "1.0.0",
          "type": "python",
          "entry_point": "wildcard_eq_mismatch",
          "process_class": "WildcardEqualityMismatchExtension",
          "category": "filters",
          "launch_mode": "single_image",
          "package_format_version": 1,
          "protocol_version": 1,
          "sdk_version": 1,
          "python_compat": "==3.14.*"
        })json");

    auto context = defaultContext();
    context.bundled_python_version = "3.15.0";

    const auto result = an::ui::extensions::load_extension_manifest_file(package_root / "extension.json",
                                                                         ExtensionPackageSource::User, context);

    ASSERT_TRUE(result.manifest.has_value());
    EXPECT_EQ(result.status, ExtensionLoadStatus::Incompatible);
}

// A wildcard inequality excludes the versions its prefix matches.
TEST(ExtensionManagerTest, PythonCompatWildcardInequalityRejectsMatchingPrefix)
{
    ScopedTempDirectory temp_dir;
    const auto package_root = createExtensionPackage(temp_dir.path(), "wildcard_ne",
                                                     R"json({
          "id": "wildcard_ne",
          "name": "Wildcard Inequality",
          "version": "1.0.0",
          "type": "python",
          "entry_point": "wildcard_ne",
          "process_class": "WildcardInequalityExtension",
          "category": "filters",
          "launch_mode": "single_image",
          "package_format_version": 1,
          "protocol_version": 1,
          "sdk_version": 1,
          "python_compat": "!=3.14.*"
        })json");

    const auto result = an::ui::extensions::load_extension_manifest_file(
        package_root / "extension.json", ExtensionPackageSource::User, defaultContext());

    ASSERT_TRUE(result.manifest.has_value());
    EXPECT_EQ(result.status, ExtensionLoadStatus::Incompatible);
}

// A wildcard with an ordered comparison is not a valid specifier and is refused.
TEST(ExtensionManagerTest, PythonCompatWildcardWithOrderedOperatorIsRejected)
{
    ScopedTempDirectory temp_dir;
    const auto package_root = createExtensionPackage(temp_dir.path(), "wildcard_invalid",
                                                     R"json({
          "id": "wildcard_invalid",
          "name": "Wildcard Invalid",
          "version": "1.0.0",
          "type": "python",
          "entry_point": "wildcard_invalid",
          "process_class": "WildcardInvalidExtension",
          "category": "filters",
          "launch_mode": "single_image",
          "package_format_version": 1,
          "protocol_version": 1,
          "sdk_version": 1,
          "python_compat": ">=3.14.*"
        })json");

    const auto result = an::ui::extensions::load_extension_manifest_file(
        package_root / "extension.json", ExtensionPackageSource::User, defaultContext());

    ASSERT_TRUE(result.manifest.has_value());
    EXPECT_EQ(result.status, ExtensionLoadStatus::Incompatible);
    EXPECT_NE(result.diagnostic.find("wildcard"), std::string::npos);
}

// The bundled interpreter is the one in the application's PythonLib directory when it is there.
TEST(ExtensionManagerTest, BundledPythonExecutableForAppDirPrefersPythonLibInterpreter)
{
    ScopedTempDirectory temp_dir;
    const auto app_dir = temp_dir.path() / "appdir";
    std::filesystem::create_directories(app_dir);

#ifdef _WIN32
    writeTextFile(app_dir / "python.exe", "root-stub");
    const auto expected = app_dir / "PythonLib" / "Scripts" / "python.exe";
#else
    writeTextFile(app_dir / "python3.14", "root-stub");
    const auto expected = app_dir / "PythonLib" / "bin" / "python3.14";
#endif
    writeTextFile(expected, "stub");

    EXPECT_EQ(an::ui::extensions::bundled_python_executable_for_app_dir(app_dir), expected);
}

// Without one in PythonLib, the interpreter deployed in the application directory is used.
TEST(ExtensionManagerTest, BundledPythonExecutableForAppDirFallsBackToDeployedRootInterpreter)
{
    ScopedTempDirectory temp_dir;
    const auto app_dir = temp_dir.path() / "appdir";
#ifdef _WIN32
    const auto expected = app_dir / "python.exe";
#else
    const auto expected = app_dir / "python3.14";
#endif
    writeTextFile(expected, "stub");

    EXPECT_EQ(an::ui::extensions::bundled_python_executable_for_app_dir(app_dir), expected);
}

// On a layout with a PythonLib bin directory, its interpreter is used.
TEST(ExtensionManagerTest, BundledPythonExecutableForAppDirFallsBackToPythonLibBin)
{
    ScopedTempDirectory temp_dir;
    const auto app_dir = temp_dir.path() / "appdir";
#ifdef _WIN32
    const auto expected = app_dir / "PythonLib" / "Scripts" / "python.exe";
#else
    const auto expected = app_dir / "PythonLib" / "bin" / "python3.14";
#endif
    writeTextFile(expected, "stub");

    EXPECT_EQ(an::ui::extensions::bundled_python_executable_for_app_dir(app_dir), expected);
}

// A multi-process RPC extension is unavailable here but keeps each normalized process.
TEST(ExtensionManagerTest, MultiProcessManifestRecognizesRpcAsUnavailableButKeepsNormalizedProcesses)
{
    ScopedTempDirectory temp_dir;
    const auto package_root = createExtensionPackage(temp_dir.path(), "multi_process_demo",
                                                     R"json({
          "id": "multi_process_demo",
          "name": "Multi Process Demo",
          "version": "1.2.3",
          "type": "python",
          "entry_point": "multi_process_demo.entry",
          "processes": [
            {
              "id_suffix": "background",
              "class": "BackgroundDemoExtension",
              "display_name": "Background Demo",
              "category": "background_extraction"
            },
            {
              "id_suffix": "denoise",
              "class": "DenoiseDemoExtension",
              "display_name": "Denoise Demo",
              "category": "denoising"
            }
          ],
          "category": "star_object",
          "launch_mode": "single_image",
          "package_format_version": 1,
          "protocol_version": 1,
          "sdk_version": 1,
          "sdk_backend": "rpc"
        })json");

    const auto result = an::ui::extensions::load_extension_manifest_file(
        package_root / "extension.json", ExtensionPackageSource::User, defaultContext());

    ASSERT_TRUE(result.manifest.has_value());
    EXPECT_EQ(result.status, ExtensionLoadStatus::Incompatible);
    EXPECT_NE(result.diagnostic.find("rpc SDK backend"), std::string::npos);
    ASSERT_EQ(result.manifest->processes.size(), 2u);
    EXPECT_EQ(result.manifest->processes[0].id, "multi_process_demo.background");
    EXPECT_EQ(result.manifest->processes[0].category, "background_extraction");
    EXPECT_EQ(result.manifest->processes[1].id, "multi_process_demo.denoise");
    EXPECT_EQ(result.manifest->processes[1].category, "denoising");
}

// Packages in one shared dependency group share one environment.
TEST(ExtensionManagerTest, SharedGroupDependencyContextReusesEnvironmentAcrossPackages)
{
    ScopedTempDirectory temp_dir;
    const auto package_root_a = createExtensionPackage(temp_dir.path(), "shared_group_a",
                                                       R"json({
                    "id": "shared_group_a",
                    "name": "Shared Group A",
                    "version": "1.0.0",
                    "type": "python",
                    "entry_point": "shared_group_a.entry",
                    "process_class": "SharedGroupAExtension",
                    "category": "filters",
                    "launch_mode": "single_image",
                    "package_format_version": 1,
                    "protocol_version": 1,
                    "sdk_version": 1,
                    "dependencies": {
                        "dependency_context": "shared_group",
                        "shared_group": "demo_suite"
                    }
                })json");
    const auto package_root_b = createExtensionPackage(temp_dir.path(), "shared_group_b",
                                                       R"json({
                    "id": "shared_group_b",
                    "name": "Shared Group B",
                    "version": "1.0.0",
                    "type": "python",
                    "entry_point": "shared_group_b.entry",
                    "process_class": "SharedGroupBExtension",
                    "category": "filters",
                    "launch_mode": "single_image",
                    "package_format_version": 1,
                    "protocol_version": 1,
                    "sdk_version": 1,
                    "dependencies": {
                        "dependency_context": "shared_group",
                        "shared_group": "demo_suite"
                    }
                })json");

    auto context = defaultContext();
    context.environments_root = temp_dir.path() / "ExtensionEnvs";

    const auto result_a = an::ui::extensions::load_extension_manifest_file(package_root_a / "extension.json",
                                                                           ExtensionPackageSource::User, context);
    const auto result_b = an::ui::extensions::load_extension_manifest_file(package_root_b / "extension.json",
                                                                           ExtensionPackageSource::User, context);

    ASSERT_TRUE(result_a.manifest.has_value());
    ASSERT_TRUE(result_b.manifest.has_value());
    ASSERT_TRUE(result_a.environment.has_value());
    ASSERT_TRUE(result_b.environment.has_value());
    EXPECT_EQ(result_a.status, ExtensionLoadStatus::Ready);
    EXPECT_EQ(result_b.status, ExtensionLoadStatus::Ready);
    EXPECT_EQ(result_a.environment->dependency_fingerprint, result_b.environment->dependency_fingerprint);
    EXPECT_EQ(result_a.environment->environment_path, result_b.environment->environment_path);
    EXPECT_NE(result_a.environment->environment_path.generic_string().find("/_shared/demo_suite/"), std::string::npos);
}

// Packages of the shared host context share its scientific core.
TEST(ExtensionManagerTest, SharedHostDependencyContextReusesScientificCoreAcrossPackages)
{
    ScopedTempDirectory temp_dir;
    const auto package_root_a = createExtensionPackage(temp_dir.path(), "shared_host_a",
                                                       R"json({
                    "id": "shared_host_a",
                    "name": "Shared Host A",
                    "version": "1.0.0",
                    "type": "python",
                    "entry_point": "shared_host_a.entry",
                    "process_class": "SharedHostAExtension",
                    "category": "filters",
                    "launch_mode": "single_image",
                    "package_format_version": 1,
                    "protocol_version": 1,
                    "sdk_version": 1,
                    "dependencies": {
                        "dependency_context": "shared_host",
                        "shared_host_profile": "scientific_core"
                    }
                })json");
    const auto package_root_b = createExtensionPackage(temp_dir.path(), "shared_host_b",
                                                       R"json({
                    "id": "shared_host_b",
                    "name": "Shared Host B",
                    "version": "1.0.0",
                    "type": "python",
                    "entry_point": "shared_host_b.entry",
                    "process_class": "SharedHostBExtension",
                    "category": "filters",
                    "launch_mode": "single_image",
                    "package_format_version": 1,
                    "protocol_version": 1,
                    "sdk_version": 1,
                    "dependencies": {
                        "dependency_context": "shared_host",
                        "shared_host_profile": "scientific_core"
                    }
                })json");

    auto context = defaultContext();
    context.environments_root = temp_dir.path() / "ExtensionEnvs";

    const auto result_a = an::ui::extensions::load_extension_manifest_file(package_root_a / "extension.json",
                                                                           ExtensionPackageSource::User, context);
    const auto result_b = an::ui::extensions::load_extension_manifest_file(package_root_b / "extension.json",
                                                                           ExtensionPackageSource::User, context);

    ASSERT_TRUE(result_a.manifest.has_value());
    ASSERT_TRUE(result_b.manifest.has_value());
    ASSERT_TRUE(result_a.environment.has_value());
    ASSERT_TRUE(result_b.environment.has_value());
    EXPECT_EQ(result_a.status, ExtensionLoadStatus::Ready);
    EXPECT_EQ(result_b.status, ExtensionLoadStatus::Ready);
    EXPECT_EQ(result_a.environment->dependency_fingerprint, result_b.environment->dependency_fingerprint);
    EXPECT_EQ(result_a.environment->environment_path, result_b.environment->environment_path);
    EXPECT_NE(result_a.environment->environment_path.generic_string().find("/_host_shared/scientific_core/"),
              std::string::npos);
}

// A shared host profile is fingerprinted from its receipt in a routine inspection and audited by content only when
// verification is asked for.
TEST(ExtensionManagerTest, SharedHostProfileReceiptDefersContentAuditUntilExplicitVerification)
{
    ScopedTempDirectory temp_dir;
    const auto runtime_root = temp_dir.path() / "PythonLib";
    const auto site_packages = runtimeSitePackagesPathForTest(runtime_root);
    const auto package_root = temp_dir.path() / "package";
    const auto numpy_source = site_packages / "numpy" / "core.py";
    writeTextFile(numpy_source, "VALUE = 1\n");
    writeTextFile(site_packages / "scipy" / "core.py", "VALUE = 1\n");
    writeTextFile(site_packages / "skimage" / "core.py", "VALUE = 1\n");
    writeTextFile(package_root / "entry.py", "VALUE = 1\n");

    ExtensionManifest manifest;
    manifest.id = "shared_host_receipt";
    manifest.version = "1.0.0";
    manifest.package_root = package_root.generic_string();
    manifest.dependencies.dependency_context = ExtensionDependencyContext::SharedHost;
    manifest.dependencies.shared_host_profile = "scientific_core";

    auto context = defaultContext();
    context.runtime_root = runtime_root;
    context.environments_root = temp_dir.path() / "ExtensionEnvs";

    ExtensionEnvironmentManager initial_manager;
    const auto initial = initial_manager.inspectEnvironment(manifest, ExtensionPackageSource::User, context, {},
                                                            ExtensionEnvironmentInspectionMode::VerifyContents);
    ASSERT_EQ(initial.shared_host_profile_fingerprint_source,
              ExtensionSharedHostProfileFingerprintSource::ContentAudit);
    ASSERT_FALSE(initial.shared_host_profile_receipt_path.empty());
    ASSERT_TRUE(std::filesystem::exists(initial.shared_host_profile_receipt_path));

    std::error_code ec;
    const auto original_write_time = std::filesystem::last_write_time(numpy_source, ec);
    ASSERT_FALSE(ec) << ec.message();
    writeTextFile(numpy_source, "VALUE = 2\n");
    std::filesystem::last_write_time(numpy_source, original_write_time, ec);
    ASSERT_FALSE(ec) << ec.message();

    ExtensionEnvironmentManager routine_manager;
    const auto routine = routine_manager.inspectEnvironment(manifest, ExtensionPackageSource::User, context);
    EXPECT_EQ(routine.shared_host_profile_fingerprint_source,
              ExtensionSharedHostProfileFingerprintSource::ProvisioningReceipt);
    EXPECT_EQ(routine.dependency_fingerprint, initial.dependency_fingerprint);

    ExtensionEnvironmentManager verification_manager;
    const auto verified = verification_manager.inspectEnvironment(manifest, ExtensionPackageSource::User, context, {},
                                                                  ExtensionEnvironmentInspectionMode::VerifyContents);
    EXPECT_EQ(verified.shared_host_profile_fingerprint_source,
              ExtensionSharedHostProfileFingerprintSource::ContentAudit);
    EXPECT_NE(verified.dependency_fingerprint, initial.dependency_fingerprint);

    ExtensionEnvironmentManager reloaded_manager;
    const auto reloaded = reloaded_manager.inspectEnvironment(manifest, ExtensionPackageSource::User, context);
    EXPECT_EQ(reloaded.shared_host_profile_fingerprint_source,
              ExtensionSharedHostProfileFingerprintSource::ProvisioningReceipt);
    EXPECT_EQ(reloaded.dependency_fingerprint, verified.dependency_fingerprint);
}

// One discovery pass fingerprints a shared host profile once for every package that uses it.
TEST(ExtensionManagerTest, DiscoveryReusesSharedHostProfileFingerprintWithinOnePass)
{
    ScopedTempDirectory temp_dir;
    const auto runtime_root = temp_dir.path() / "PythonLib";
    const auto site_packages = runtimeSitePackagesPathForTest(runtime_root);
    writeTextFile(site_packages / "numpy" / "core.py", "VALUE = 1\n");
    writeTextFile(site_packages / "scipy" / "core.py", "VALUE = 1\n");
    writeTextFile(site_packages / "skimage" / "core.py", "VALUE = 1\n");

    const auto user_root = temp_dir.path() / "user";
    const auto create_shared_host_package = [&user_root](const std::string& id) {
        createExtensionPackage(user_root, id, std::string(R"json({
              "id": ")json") + id + R"json(",
              "name": "Shared Host Extension",
              "version": "1.0.0",
              "type": "python",
              "entry_point": "shared_host.entry",
              "process_class": "SharedHostExtension",
              "category": "filters",
              "launch_mode": "single_image",
              "package_format_version": 1,
              "protocol_version": 1,
              "sdk_version": 1,
              "dependencies": {
                "dependency_context": "shared_host",
                "shared_host_profile": "scientific_core"
              }
            })json");
    };
    create_shared_host_package("shared_host_a");
    create_shared_host_package("shared_host_b");

    ExtensionDiscoveryOptions options;
    options.user_roots = {user_root};
    options.context = defaultContext();
    options.context.runtime_root = runtime_root;
    options.context.environments_root = temp_dir.path() / "ExtensionEnvs";

    ExtensionManager manager;
    const auto discovered = manager.discoverExtensions(options);
    ASSERT_EQ(discovered.size(), 2u);

    int content_audits = 0;
    int memory_cache_hits = 0;
    for (const auto& result : discovered)
    {
        ASSERT_TRUE(result.environment.has_value());
        content_audits += result.environment->shared_host_profile_fingerprint_source ==
                          ExtensionSharedHostProfileFingerprintSource::ContentAudit;
        memory_cache_hits += result.environment->shared_host_profile_fingerprint_source ==
                             ExtensionSharedHostProfileFingerprintSource::DiscoveryCache;
    }

    EXPECT_EQ(content_audits, 1);
    EXPECT_EQ(memory_cache_hits, 1);
    EXPECT_EQ(discovered[0].environment->dependency_fingerprint, discovered[1].environment->dependency_fingerprint);
}

// A shared host context refuses a package that manages its own dependencies in its manifest.
TEST(ExtensionManagerTest, SharedHostDependencyContextRejectsManifestManagedPackages)
{
    ScopedTempDirectory temp_dir;
    const auto package_root = createExtensionPackage(temp_dir.path(), "shared_host_invalid",
                                                     R"json({
                    "id": "shared_host_invalid",
                    "name": "Shared Host Invalid",
                    "version": "1.0.0",
                    "type": "python",
                    "entry_point": "shared_host_invalid.entry",
                    "process_class": "SharedHostInvalidExtension",
                    "category": "filters",
                    "launch_mode": "single_image",
                    "package_format_version": 1,
                    "protocol_version": 1,
                    "sdk_version": 1,
                    "dependencies": {
                        "dependency_context": "shared_host",
                        "shared_host_profile": "scientific_core",
                        "bundled_runtime_packages": ["numpy"]
                    }
                })json");

    const auto result = an::ui::extensions::load_extension_manifest_file(
        package_root / "extension.json", ExtensionPackageSource::User, defaultContext());

    EXPECT_EQ(result.status, ExtensionLoadStatus::Invalid);
    EXPECT_NE(result.diagnostic.find("shared_host"), std::string::npos);
}

// Bootstrapping bundled extensions provisions a shared environment once.
TEST(ExtensionManagerTest, BundledSharedHostBootstrapDeduplicatesSharedEnvironment)
{
    ScopedTempDirectory temp_dir;
    const auto package_root_a = createExtensionPackage(temp_dir.path(), "bundled_shared_host_a",
                                                       R"json({
              "id": "bundled_shared_host_a",
              "name": "Bundled Shared Host A",
              "version": "1.0.0",
              "type": "python",
              "entry_point": "bundled_shared_host_a.entry",
              "process_class": "BundledSharedHostAExtension",
              "category": "filters",
              "launch_mode": "single_image",
              "package_format_version": 1,
              "protocol_version": 1,
              "sdk_version": 1,
              "dependencies": {
                "dependency_context": "shared_host",
                "shared_host_profile": "scientific_core"
              }
            })json");
    const auto package_root_b = createExtensionPackage(temp_dir.path(), "bundled_shared_host_b",
                                                       R"json({
              "id": "bundled_shared_host_b",
              "name": "Bundled Shared Host B",
              "version": "1.0.0",
              "type": "python",
              "entry_point": "bundled_shared_host_b.entry",
              "process_class": "BundledSharedHostBExtension",
              "category": "filters",
              "launch_mode": "single_image",
              "package_format_version": 1,
              "protocol_version": 1,
              "sdk_version": 1,
              "dependencies": {
                "dependency_context": "shared_host",
                "shared_host_profile": "scientific_core"
              }
            })json");

    auto context = defaultContext();
    context.environments_root = temp_dir.path() / "ExtensionEnvs";

    std::vector<an::ui::extensions::ExtensionLoadResult> discovered;
    discovered.push_back(an::ui::extensions::load_extension_manifest_file(package_root_a / "extension.json",
                                                                          ExtensionPackageSource::Bundled, context));
    discovered.push_back(an::ui::extensions::load_extension_manifest_file(package_root_b / "extension.json",
                                                                          ExtensionPackageSource::Bundled, context));

    ASSERT_EQ(discovered.size(), 2u);
    ASSERT_TRUE(discovered[0].environment.has_value());
    ASSERT_TRUE(discovered[1].environment.has_value());
    EXPECT_EQ(discovered[0].environment->environment_path, discovered[1].environment->environment_path);

    ExtensionEnvironmentManager environment_manager;
    std::string error;
    EXPECT_EQ(environment_manager.queueBundledBootstrap(discovered, &error), 1) << error;
    ASSERT_TRUE(error.empty()) << error;
    EXPECT_EQ(discovered[0].environment->state, ExtensionEnvironmentState::Bootstrapping);
    EXPECT_EQ(discovered[1].environment->state, ExtensionEnvironmentState::Bootstrapping);

    error.clear();
    EXPECT_EQ(environment_manager.processQueuedBundledBootstrap(discovered, &error), 1) << error;
    ASSERT_TRUE(error.empty()) << error;
    EXPECT_EQ(discovered[0].environment->state, ExtensionEnvironmentState::Ready);
    EXPECT_EQ(discovered[1].environment->state, ExtensionEnvironmentState::Ready);

    const auto reloaded_a = an::ui::extensions::load_extension_manifest_file(package_root_a / "extension.json",
                                                                             ExtensionPackageSource::Bundled, context);
    const auto reloaded_b = an::ui::extensions::load_extension_manifest_file(package_root_b / "extension.json",
                                                                             ExtensionPackageSource::Bundled, context);

    ASSERT_TRUE(reloaded_a.environment.has_value());
    ASSERT_TRUE(reloaded_b.environment.has_value());
    EXPECT_EQ(reloaded_a.status, ExtensionLoadStatus::Ready);
    EXPECT_EQ(reloaded_b.status, ExtensionLoadStatus::Ready);
    EXPECT_EQ(reloaded_a.environment->state, ExtensionEnvironmentState::Ready);
    EXPECT_EQ(reloaded_b.environment->state, ExtensionEnvironmentState::Ready);
}

// A provisioning lock an earlier run left marks the environment interrupted and queues it for bootstrap again.
TEST(ExtensionManagerTest, OwnerlessProvisioningLockRequiresBundledBootstrapRepair)
{
    ScopedTempDirectory temp_dir;
    const auto package_root = createExtensionPackage(temp_dir.path(), "shared_suite_like",
                                                     R"json({
          "id": "shared_suite_like",
          "name": "Shared Suite Like",
          "version": "1.0.0",
          "type": "python",
          "entry_point": "shared_suite_like.entry",
          "process_class": "SharedSuiteLikeExtension",
          "category": "background_extraction",
          "launch_mode": "single_image",
          "package_format_version": 1,
          "protocol_version": 1,
          "sdk_version": 1,
          "dependencies": {
            "dependency_context": "shared_group",
            "shared_group": "shared_suite"
          }
        })json");

    auto context = defaultContext();
    context.environments_root = temp_dir.path() / "ExtensionEnvs";

    const auto initial = an::ui::extensions::load_extension_manifest_file(package_root / "extension.json",
                                                                          ExtensionPackageSource::Bundled, context);
    ASSERT_TRUE(initial.environment.has_value());

    const auto environment_path = initial.environment->environment_path;
    std::filesystem::create_directories(environment_path);
    writeTextFile(environment_path / ".provisioning.lock",
                  "dependency_fingerprint=" + initial.environment->dependency_fingerprint + "\n" +
                      "validation_fingerprint=" + initial.environment->validation_fingerprint + "\n");

    const auto reloaded = an::ui::extensions::load_extension_manifest_file(package_root / "extension.json",
                                                                           ExtensionPackageSource::Bundled, context);
    ASSERT_TRUE(reloaded.environment.has_value());
    EXPECT_EQ(reloaded.environment->state, ExtensionEnvironmentState::Failed);
    EXPECT_NE(reloaded.environment->diagnostic.find("interrupted"), std::string::npos);

    ExtensionEnvironmentManager environment_manager;
    std::vector<an::ui::extensions::ExtensionLoadResult> discovered = {reloaded};
    std::string error;
    EXPECT_EQ(environment_manager.queueBundledBootstrap(discovered, &error), 1) << error;
    ASSERT_TRUE(error.empty()) << error;
    ASSERT_TRUE(discovered.front().environment.has_value());
    EXPECT_EQ(discovered.front().environment->state, ExtensionEnvironmentState::Bootstrapping);
}

// A provisioning lock a running provisioning holds keeps the environment bootstrapping.
TEST(ExtensionManagerTest, LiveProvisioningLockRemainsBootstrapping)
{
    ScopedTempDirectory temp_dir;
    const auto package_root = createExtensionPackage(temp_dir.path(), "shared_suite_like",
                                                     R"json({
          "id": "shared_suite_like",
          "name": "Shared Suite Like",
          "version": "1.0.0",
          "type": "python",
          "entry_point": "shared_suite_like.entry",
          "process_class": "SharedSuiteLikeExtension",
          "category": "background_extraction",
          "launch_mode": "single_image",
          "package_format_version": 1,
          "protocol_version": 1,
          "sdk_version": 1,
          "dependencies": {
            "dependency_context": "shared_group",
            "shared_group": "shared_suite"
          }
        })json");

    auto context = defaultContext();
    context.environments_root = temp_dir.path() / "ExtensionEnvs";

    const auto initial = an::ui::extensions::load_extension_manifest_file(package_root / "extension.json",
                                                                          ExtensionPackageSource::Bundled, context);
    ASSERT_TRUE(initial.environment.has_value());

    const auto environment_path = initial.environment->environment_path;
    std::filesystem::create_directories(environment_path);
    const LiveOtherProcess owner;
    ASSERT_TRUE(owner.started());
    writeTextFile(environment_path / ".provisioning.lock",
                  "dependency_fingerprint=" + initial.environment->dependency_fingerprint + "\n" +
                      "validation_fingerprint=" + initial.environment->validation_fingerprint + "\n" + owner.pidLine());

    const auto reloaded = an::ui::extensions::load_extension_manifest_file(package_root / "extension.json",
                                                                           ExtensionPackageSource::Bundled, context);
    ASSERT_TRUE(reloaded.environment.has_value());
    EXPECT_EQ(reloaded.environment->state, ExtensionEnvironmentState::Bootstrapping);
}

// A lock that names this process is held only while an operation of this process prepares the environment. One left
// behind - a refresh that could not remove its lock, a provisioning that failed before it recorded the environment -
// holds nothing: the environment is reported as it is, to be repaired, rather than as being prepared for the life of
// the process. It was taken for a live lock, and every launch waited for it until the application was restarted.
TEST(ExtensionManagerTest, ALockOfThisProcessThatNoOperationHoldsIsNotHeld)
{
    ScopedTempDirectory temp_dir;
    const auto package_root =
        createExtensionPackage(temp_dir.path(), "own_lock_ext", dependencyFreeManifest("own_lock_ext"));
    auto context = defaultContext();
    context.environments_root = temp_dir.path() / "ExtensionEnvs";

    const auto initial = an::ui::extensions::load_extension_manifest_file(package_root / "extension.json",
                                                                          ExtensionPackageSource::User, context);
    ASSERT_TRUE(initial.environment.has_value());
    const auto environment_path = initial.environment->environment_path;
    std::filesystem::create_directories(environment_path);
    writeTextFile(environment_path / ".provisioning.lock",
                  "owner_pid=" + std::to_string(QCoreApplication::applicationPid()) + "\n");

    const auto reloaded = an::ui::extensions::load_extension_manifest_file(package_root / "extension.json",
                                                                           ExtensionPackageSource::User, context);
    ASSERT_TRUE(reloaded.environment.has_value());
    EXPECT_EQ(reloaded.environment->state, ExtensionEnvironmentState::Failed)
        << "a lock this process left behind was taken for one it holds";
}

// A lock whose owner started at another time than the process now running under its pid was left by a process that is
// gone: the pid was given out again. It is stale, however long the process now holding the pid lives.
TEST(ExtensionManagerTest, ALockWhoseOwnerStartedAtAnotherTimeIsStale)
{
    ScopedTempDirectory temp_dir;
    const auto package_root =
        createExtensionPackage(temp_dir.path(), "reused_pid_ext", dependencyFreeManifest("reused_pid_ext"));
    auto context = defaultContext();
    context.environments_root = temp_dir.path() / "ExtensionEnvs";

    const auto initial = an::ui::extensions::load_extension_manifest_file(package_root / "extension.json",
                                                                          ExtensionPackageSource::User, context);
    ASSERT_TRUE(initial.environment.has_value());
    const auto environment_path = initial.environment->environment_path;
    std::filesystem::create_directories(environment_path);
    const LiveOtherProcess owner;
    ASSERT_TRUE(owner.started());
    writeTextFile(environment_path / ".provisioning.lock", owner.pidLine() + "owner_started=1\n");

    const auto reloaded = an::ui::extensions::load_extension_manifest_file(package_root / "extension.json",
                                                                           ExtensionPackageSource::User, context);
    ASSERT_TRUE(reloaded.environment.has_value());
    EXPECT_EQ(reloaded.environment->state, ExtensionEnvironmentState::Failed)
        << "a lock of a process started at another time was taken for the live process's";
}

// The lock a manager of this process takes when it queues an environment for maintenance is held until the maintenance
// processes it or lets it go: a discovery meanwhile finds the environment being prepared, not left behind.
TEST(ExtensionManagerTest, ALockThisProcessHoldsQueuedForMaintenanceIsHeld)
{
    ScopedTempDirectory temp_dir;
    const auto runtime_root = writeSmallRuntimeRoot(temp_dir.path());
    const auto package_root =
        createExtensionPackage(temp_dir.path(), "queued_lock_ext", dependencyFreeManifest("queued_lock_ext"));
    ExtensionDiscoveryContext context = defaultContext();
    context.runtime_root = runtime_root;
    context.environments_root = temp_dir.path() / "ExtensionEnvs";

    ExtensionEnvironmentManager environment_manager;
    std::vector discovered = {an::ui::extensions::load_extension_manifest_file(package_root / "extension.json",
                                                                               ExtensionPackageSource::User, context)};
    ASSERT_EQ(environment_manager.queueEnvironmentMaintenance(discovered, false), 1);
    const auto environment_path = discovered.front().environment->environment_path;
    ASSERT_TRUE(std::filesystem::exists(environment_path / ".provisioning.lock"));

    const auto meanwhile = an::ui::extensions::load_extension_manifest_file(package_root / "extension.json",
                                                                            ExtensionPackageSource::User, context);
    ASSERT_TRUE(meanwhile.environment.has_value());
    EXPECT_EQ(meanwhile.environment->state, ExtensionEnvironmentState::Bootstrapping)
        << "the lock of a queued environment was taken for one left behind";

    ExtensionOperationCancellationToken stopped;
    stopped.requestCancellation();
    std::string error;
    EXPECT_EQ(environment_manager.processQueuedEnvironmentMaintenance(discovered, &error, stopped), 0);
    EXPECT_FALSE(std::filesystem::exists(environment_path / ".provisioning.lock"));
    const auto released = an::ui::extensions::load_extension_manifest_file(package_root / "extension.json",
                                                                           ExtensionPackageSource::User, context);
    ASSERT_TRUE(released.environment.has_value());
    EXPECT_NE(released.environment->state, ExtensionEnvironmentState::Bootstrapping)
        << "the lock let go was still taken for one held";
}

// A process's capabilities read its mask and weight inputs and outputs and ignore the deprecated interface field.
TEST(ExtensionManagerTest, SingleProcessCapabilitiesParseMasksWeightsAndIgnoreDeprecatedUiField)
{
    ScopedTempDirectory temp_dir;
    const auto package_root = createExtensionPackage(temp_dir.path(), "caps_ext",
                                                     R"json({
          "id": "caps_ext",
          "name": "Capabilities Extension",
          "version": "1.0.0",
          "type": "python",
          "entry_point": "caps_ext.entry",
          "process_class": "CapabilitiesExtension",
          "category": "filters",
          "launch_mode": "single_image",
          "package_format_version": 1,
          "protocol_version": 1,
          "sdk_version": 1,
          "capabilities": {
            "execute": true,
            "preview": true,
            "ui": "custom",
            "source_channel_filter": [3],
            "masks": { "input": true, "output": true },
            "weights": { "input": true }
          }
        })json");

    const auto result = an::ui::extensions::load_extension_manifest_file(
        package_root / "extension.json", ExtensionPackageSource::User, defaultContext());

    ASSERT_TRUE(result.manifest.has_value());
    ASSERT_EQ(result.manifest->processes.size(), 1u);
    const auto& capabilities = result.manifest->processes.front().capabilities;
    EXPECT_TRUE(capabilities.execute);
    EXPECT_TRUE(capabilities.preview);
    EXPECT_EQ(capabilities.ui, "auto");
    EXPECT_EQ(capabilities.source_channel_filter, std::vector<int>{3});
    EXPECT_TRUE(capabilities.masks_input);
    EXPECT_TRUE(capabilities.masks_output);
    EXPECT_TRUE(capabilities.weights_input);
}

// A process states what a frame of a drag may run its preview over, and the reach of a body over a part; one that
// states nothing runs every frame over the whole image.
TEST(ExtensionManagerTest, PreviewScalingIsReadFromTheCapabilitiesAndDefaultsToTheWholeImage)
{
    using an::ui::extensions::ExtensionPreviewScaling;
    struct Case
    {
        const char* fields;
        ExtensionPreviewScaling scaling;
        std::uint32_t margin;
    };
    const Case cases[] = {
        {"", ExtensionPreviewScaling::FullOnly, 0u},
        {R"("preview_scaling": "full_only",)", ExtensionPreviewScaling::FullOnly, 0u},
        {R"("preview_scaling": "decimated",)", ExtensionPreviewScaling::Decimated, 0u},
        {R"("preview_scaling": "native_region", "preview_region_margin": 12,)", ExtensionPreviewScaling::NativeRegion,
         12u},
        {R"("preview_scaling": "native_region", "preview_region_margin": 4096,)", ExtensionPreviewScaling::NativeRegion,
         4096u},
    };
    int index = 0;
    for (const Case& scenario : cases)
    {
        ScopedTempDirectory temp_dir;
        const std::string id = "scaling_ext_" + std::to_string(index++);
        const auto package_root = createExtensionPackage(temp_dir.path(), id,
                                                         R"json({
          "id": ")json" + id + R"json(",
          "name": "Scaling Extension",
          "version": "1.0.0",
          "type": "python",
          "entry_point": "scaling_ext.entry",
          "process_class": "ScalingExtension",
          "category": "filters",
          "launch_mode": "single_image",
          "package_format_version": 1,
          "protocol_version": 1,
          "sdk_version": 1,
          "capabilities": {)json" + std::string(scenario.fields) +
                                                             R"json(
            "execute": true,
            "preview": true
          }
        })json");

        const auto result = an::ui::extensions::load_extension_manifest_file(
            package_root / "extension.json", ExtensionPackageSource::User, defaultContext());
        ASSERT_TRUE(result.manifest.has_value()) << scenario.fields << ": " << result.diagnostic;
        ASSERT_EQ(result.manifest->processes.size(), 1u);
        const auto& capabilities = result.manifest->processes.front().capabilities;
        EXPECT_EQ(capabilities.preview_scaling, scenario.scaling) << scenario.fields;
        EXPECT_EQ(capabilities.preview_region_margin, scenario.margin) << scenario.fields;
    }
}

// A word that names no scaling, or a reach that is not a whole number of pixels from none to the limit, is refused,
// and the refusal names the field.
TEST(ExtensionManagerTest, AnUnknownPreviewScalingOrAnUnstatableReachIsRefused)
{
    struct Case
    {
        const char* fields;
        const char* named;
    };
    const Case cases[] = {
        {R"("preview_scaling": "fast",)", "capabilities.preview_scaling"},
        {R"("preview_scaling": 2,)", "capabilities.preview_scaling"},
        {R"("preview_scaling": "Decimated",)", "capabilities.preview_scaling"},
        {R"("preview_scaling": "native_region", "preview_region_margin": -1,)", "capabilities.preview_region_margin"},
        {R"("preview_scaling": "native_region", "preview_region_margin": 4097,)", "capabilities.preview_region_margin"},
        {R"("preview_scaling": "native_region", "preview_region_margin": 2.5,)", "capabilities.preview_region_margin"},
        {R"("preview_scaling": "native_region", "preview_region_margin": "3",)", "capabilities.preview_region_margin"},
    };
    int index = 0;
    for (const Case& scenario : cases)
    {
        ScopedTempDirectory temp_dir;
        const std::string id = "bad_scaling_ext_" + std::to_string(index++);
        const auto package_root = createExtensionPackage(temp_dir.path(), id,
                                                         R"json({
          "id": ")json" + id + R"json(",
          "name": "Bad Scaling Extension",
          "version": "1.0.0",
          "type": "python",
          "entry_point": "bad_scaling_ext.entry",
          "process_class": "BadScalingExtension",
          "category": "filters",
          "launch_mode": "single_image",
          "package_format_version": 1,
          "protocol_version": 1,
          "sdk_version": 1,
          "capabilities": {)json" + std::string(scenario.fields) +
                                                             R"json(
            "execute": true,
            "preview": true
          }
        })json");

        const auto result = an::ui::extensions::load_extension_manifest_file(
            package_root / "extension.json", ExtensionPackageSource::User, defaultContext());
        EXPECT_EQ(result.status, ExtensionLoadStatus::Invalid) << scenario.fields;
        EXPECT_NE(result.diagnostic.find(scenario.named), std::string::npos)
            << scenario.fields << ": " << result.diagnostic;
    }
}

// A starless generator's capability reaches the process registry's descriptor.
TEST(ExtensionManagerTest, StarlessGeneratorCapabilitiesPropagateToRegistryDescriptors)
{
    ScopedTempDirectory temp_dir;
    const auto package_root = createExtensionPackage(temp_dir.path(), "starless_ext",
                                                     R"json({
          "id": "starless_ext",
          "name": "Starless Extension",
          "version": "1.0.0",
          "type": "python",
          "entry_point": "starless_ext.entry",
          "process_class": "StarlessExtension",
          "category": "star_object",
          "launch_mode": "single_image",
          "package_format_version": 1,
          "protocol_version": 1,
          "sdk_version": 1,
          "capabilities": {
            "starless_generator": true,
            "star_mask_generator": true
          }
        })json");

    ExtensionManager manager;
    const auto discovered = std::vector{an::ui::extensions::load_extension_manifest_file(
        package_root / "extension.json", ExtensionPackageSource::User, defaultContext())};

    ASSERT_TRUE(discovered.front().manifest.has_value());
    ASSERT_EQ(discovered.front().manifest->processes.size(), 1u);
    const auto& capabilities = discovered.front().manifest->processes.front().capabilities;
    EXPECT_TRUE(capabilities.starless_generator);
    EXPECT_TRUE(capabilities.star_mask_generator);

    // The built-in registry is put back when the case ends, passing or failing: later cases launch through it.
    const an::tests::processes::shared::RegistrySnapshot registrySnapshot;
    ProcessRegistry& registry = ProcessRegistry::instance();
    registry.clear();
    manager.registerDiscoveredProcesses(discovered, registry);

    const auto* descriptor = registry.find("starless_ext");
    ASSERT_NE(descriptor, nullptr);
    EXPECT_TRUE(descriptor->provides_starless_output);
    EXPECT_TRUE(descriptor->provides_star_mask_output);

    registry.clear();
}

// A buffer role stated at the manifest's top level is refused.
TEST(ExtensionManagerTest, TopLevelBufferRoleFieldIsRejected)
{
    ScopedTempDirectory temp_dir;
    const auto package_root = createExtensionPackage(temp_dir.path(), "invalid_buffer_role",
                                                     R"json({
                    "id": "invalid_buffer_role",
                    "name": "Invalid Buffer Role",
                    "version": "1.0.0",
                    "type": "python",
                    "entry_point": "invalid_buffer_role.entry",
                    "process_class": "InvalidBufferRoleExtension",
                    "category": "filters",
                    "launch_mode": "single_image",
                    "package_format_version": 1,
                    "protocol_version": 1,
                    "sdk_version": 1,
                    "buffer_role": "destination"
                })json");

    const auto result = an::ui::extensions::load_extension_manifest_file(
        package_root / "extension.json", ExtensionPackageSource::User, defaultContext());

    EXPECT_EQ(result.status, ExtensionLoadStatus::Invalid);
    EXPECT_NE(result.diagnostic.find("buffer_role"), std::string::npos);
}

// A buffer role stated on a process entry is refused.
TEST(ExtensionManagerTest, ProcessEntryBufferRoleFieldIsRejected)
{
    ScopedTempDirectory temp_dir;
    const auto package_root = createExtensionPackage(temp_dir.path(), "invalid_process_buffer_role",
                                                     R"json({
                    "id": "invalid_process_buffer_role",
                    "name": "Invalid Process Buffer Role",
                    "version": "1.0.0",
                    "type": "python",
                    "entry_point": "invalid_process_buffer_role.entry",
                    "processes": [
                        {
                            "id_suffix": "main",
                            "class": "InvalidProcessBufferRoleExtension",
                            "display_name": "Invalid Process Buffer Role",
                            "buffer_role": "preview"
                        }
                    ],
                    "category": "filters",
                    "launch_mode": "single_image",
                    "package_format_version": 1,
                    "protocol_version": 1,
                    "sdk_version": 1
                })json");

    const auto result = an::ui::extensions::load_extension_manifest_file(
        package_root / "extension.json", ExtensionPackageSource::User, defaultContext());

    EXPECT_EQ(result.status, ExtensionLoadStatus::Invalid);
    EXPECT_NE(result.diagnostic.find("buffer_role"), std::string::npos);
}

// Capabilities that are not an object of the expected fields are refused.
TEST(ExtensionManagerTest, InvalidCapabilitiesStructureIsRejected)
{
    ScopedTempDirectory temp_dir;
    const auto package_root = createExtensionPackage(temp_dir.path(), "invalid_caps",
                                                     R"json({
          "id": "invalid_caps",
          "name": "Invalid Capabilities",
          "version": "1.0.0",
          "type": "python",
          "entry_point": "invalid_caps.entry",
          "process_class": "InvalidCapabilitiesExtension",
          "category": "filters",
          "launch_mode": "single_image",
          "package_format_version": 1,
          "protocol_version": 1,
          "sdk_version": 1,
          "capabilities": {
            "masks": "yes please"
          }
        })json");

    const auto result = an::ui::extensions::load_extension_manifest_file(
        package_root / "extension.json", ExtensionPackageSource::User, defaultContext());

    EXPECT_EQ(result.status, ExtensionLoadStatus::Invalid);
    EXPECT_NE(result.diagnostic.find("capabilities.masks"), std::string::npos);
}

// The stateful sequence capability cannot be turned on in this version of the format.
TEST(ExtensionManagerTest, StatefulSequenceCapabilityMustRemainDisabledInV1)
{
    ScopedTempDirectory temp_dir;
    const auto package_root = createExtensionPackage(temp_dir.path(), "stateful_seq",
                                                     R"json({
          "id": "stateful_seq",
          "name": "Stateful Sequence",
          "version": "1.0.0",
          "type": "python",
          "entry_point": "stateful_seq.entry",
          "process_class": "StatefulSequenceExtension",
          "category": "filters",
          "launch_mode": "single_image",
          "package_format_version": 1,
          "protocol_version": 1,
          "sdk_version": 1,
          "capabilities": {
            "stateful_sequence": true
          }
        })json");

    const auto result = an::ui::extensions::load_extension_manifest_file(
        package_root / "extension.json", ExtensionPackageSource::User, defaultContext());

    EXPECT_EQ(result.status, ExtensionLoadStatus::Invalid);
    EXPECT_NE(result.diagnostic.find("stateful_sequence"), std::string::npos);
}

// A user package with a bundled extension's identity takes its place.
TEST(ExtensionManagerTest, DiscoveryPrefersUserPackageOverBundledDuplicateId)
{
    ScopedTempDirectory temp_dir;
    const auto bundled_root = temp_dir.path() / "bundled";
    const auto user_root = temp_dir.path() / "user";

    createExtensionPackage(bundled_root, "shared_ext",
                           R"json({
          "id": "shared_ext",
          "name": "Bundled Version",
          "version": "1.0.0",
          "type": "python",
          "entry_point": "shared.entry",
          "process_class": "SharedExtension",
          "category": "filters",
          "launch_mode": "single_image",
          "package_format_version": 1,
          "protocol_version": 1,
          "sdk_version": 1
        })json");

    createExtensionPackage(user_root, "shared_ext",
                           R"json({
          "id": "shared_ext",
          "name": "User Version",
          "version": "2.0.0",
          "type": "python",
          "entry_point": "shared.entry",
          "process_class": "SharedExtension",
          "category": "filters",
          "launch_mode": "single_image",
          "package_format_version": 1,
          "protocol_version": 1,
          "sdk_version": 1
        })json");

    ExtensionDiscoveryOptions options;
    options.bundled_roots = {bundled_root};
    options.user_roots = {user_root};
    options.context = defaultContext();

    ExtensionManager manager;
    const auto discovered = manager.discoverExtensions(options);
    ASSERT_EQ(discovered.size(), 2u);

    int ready_count = 0;
    int invalid_count = 0;
    std::string ready_name;

    for (const auto& result : discovered)
    {
        if (result.status == ExtensionLoadStatus::Ready && result.manifest)
        {
            ++ready_count;
            ready_name = result.manifest->name;
        }
        if (result.status == ExtensionLoadStatus::Invalid)
        {
            ++invalid_count;
            EXPECT_FALSE(result.environment.has_value());
        }
    }

    EXPECT_EQ(ready_count, 1);
    EXPECT_EQ(invalid_count, 1);
    EXPECT_EQ(ready_name, "User Version");
}

// The host bootstrap fingerprint moves with the application's version and the host's artifacts.
TEST(ExtensionManagerTest, HostBootstrapFingerprintChangesWithAppVersionAndHostArtifacts)
{
    ScopedTempDirectory temp_dir;
    const auto host_executable = temp_dir.path() / "AfterNightExtensionHost";
    writeTextFile(host_executable, "host-v1");

    ExtensionDiscoveryContext context;
    context.bundled_python_version = "3.14.4";
    context.app_version = "1.0.0";
    context.runtime_target = currentRuntimeTarget();
    context.extension_host_executable = host_executable;

    ExtensionEnvironmentManager environment_manager;
    const std::string initial = environment_manager.computeHostBootstrapFingerprint(context);

    context.app_version = "1.0.1";
    const std::string app_updated = environment_manager.computeHostBootstrapFingerprint(context);
    EXPECT_NE(initial, app_updated);

    context.app_version = "1.0.0";
    writeTextFile(host_executable, "host-v2-with-a-different-size");
    const std::string host_updated = environment_manager.computeHostBootstrapFingerprint(context);
    EXPECT_NE(initial, host_updated);
}

// One run's copy of an AppImage's files: the bundled Python and both hosts where the package puts them, the same bytes
// in every copy, written at `written`.
struct PackageCopy
{
    std::filesystem::path root;
    std::filesystem::path python;
    std::filesystem::path host;
    std::filesystem::path hostLite;
    std::filesystem::path runtimeRoot;
};

PackageCopy writePackageCopy(const std::filesystem::path& root, const std::filesystem::file_time_type written)
{
    const std::filesystem::path lib = root / "usr" / "lib";
    PackageCopy copy{root, lib / "PythonLib" / "bin" / "python3.14", lib / "AfterNightExtensionHost",
                     lib / "AfterNightExtensionHostLite", lib / "PythonLib"};
    writeTextFile(copy.python, "the bundled interpreter");
    writeTextFile(copy.host, "the full host");
    writeTextFile(copy.hostLite, "the lite host");
    for (const auto& path : {copy.python, copy.host, copy.hostLite})
    {
        std::filesystem::last_write_time(path, written);
    }
    return copy;
}

ExtensionDiscoveryContext contextForPackageCopy(const PackageCopy& copy, const std::string& identity)
{
    ExtensionDiscoveryContext context;
    context.bundled_python_version = "3.14.4";
    context.app_version = "1.0.0";
    context.runtime_target = currentRuntimeTarget();
    context.bundled_python_executable = copy.python;
    context.extension_host_executable = copy.host;
    context.extension_host_lite_executable = copy.hostLite;
    context.runtime_root = copy.runtimeRoot;
    context.runtime_backend_available = true;
    context.protocol_backend_available = true;
    context.workflow_launch_mode_available = true;
    context.package = RelocatablePackage{copy.root, identity};
    return context;
}

// An AppImage's identity, as the package file's size and write time state it.
std::string appImageIdentity()
{
    return "appimage\n646431936\n1792533660000000000\n";
}

// An AppImage's runtime mounts its files at a new directory, or extracts them again with that start's write times, on
// every start. The bundled Python and hosts inside it are the same files, so the environments built on them are too.
TEST(ExtensionManagerTest, HostBootstrapFingerprintOfARelocatablePackageIsTheSameInEveryRunsCopyOfIt)
{
    ScopedTempDirectory temp_dir;
    const auto now = std::filesystem::file_time_type::clock::now();
    const PackageCopy mounted = writePackageCopy(temp_dir.path() / "mount_first", now);
    const PackageCopy extracted = writePackageCopy(temp_dir.path() / "extracted_again", now + std::chrono::hours(1));

    ExtensionEnvironmentManager environment_manager;
    const std::string first =
        environment_manager.computeHostBootstrapFingerprint(contextForPackageCopy(mounted, appImageIdentity()));
    EXPECT_EQ(first, environment_manager.computeHostBootstrapFingerprint(
                         contextForPackageCopy(extracted, appImageIdentity())));

    // Another package - a newer AppImage - is another bootstrap.
    EXPECT_NE(first, environment_manager.computeHostBootstrapFingerprint(
                         contextForPackageCopy(mounted, appImageIdentity() + "rebuilt\n")));

    // So is a host of another size inside the same package.
    writeTextFile(extracted.host, "the full host, rebuilt");
    EXPECT_NE(first, environment_manager.computeHostBootstrapFingerprint(
                         contextForPackageCopy(extracted, appImageIdentity())));

    // Outside a package the two copies are two installations, each known by its paths and write times.
    ExtensionDiscoveryContext first_installation = contextForPackageCopy(mounted, appImageIdentity());
    first_installation.package.reset();
    ExtensionDiscoveryContext second_installation = contextForPackageCopy(extracted, appImageIdentity());
    second_installation.package.reset();
    EXPECT_NE(environment_manager.computeHostBootstrapFingerprint(first_installation),
              environment_manager.computeHostBootstrapFingerprint(second_installation));
}

#ifndef _WIN32
// The environment one run of an AppImage provisioned names its interpreter where that run's mount put it - the venv's
// link and pyvenv.cfg - and that mount is gone by the next run. The next run finds the same environment ready and
// points it at its own interpreter, which the host needs to start.
TEST(ExtensionManagerTest, AnEnvironmentAnEarlierRunOfAnAppImageMadeIsReadyAndPointedAtThisRunsInterpreter)
{
    ScopedTempDirectory temp_dir;
    const auto now = std::filesystem::file_time_type::clock::now();
    const PackageCopy earlier = writePackageCopy(temp_dir.path() / "mount_earlier", now);
    const PackageCopy current = writePackageCopy(temp_dir.path() / "mount_current", now + std::chrono::minutes(5));
    const auto package_root = createExtensionPackage(temp_dir.path(), "moving_ext",
                                                     R"json({
          "id": "moving_ext",
          "name": "Moving Extension",
          "version": "1.0.0",
          "type": "python",
          "entry_point": "moving_ext.entry",
          "process_class": "MovingExtension",
          "category": "filters",
          "launch_mode": "single_image",
          "package_format_version": 1,
          "protocol_version": 1,
          "sdk_version": 1
        })json");

    ExtensionDiscoveryContext earlier_context = contextForPackageCopy(earlier, appImageIdentity());
    earlier_context.environments_root = temp_dir.path() / "ExtensionEnvs";
    const auto made = an::ui::extensions::load_extension_manifest_file(package_root / "extension.json",
                                                                       ExtensionPackageSource::User, earlier_context);
    ASSERT_TRUE(made.environment.has_value());
    const auto environment_path = made.environment->environment_path;
    writeInstallReceipt(environment_path, made.environment->host_bootstrap_fingerprint,
                        made.environment->dependency_fingerprint);
    std::filesystem::create_directories(environment_path / "bin");
    std::filesystem::create_symlink(earlier.python, environment_path / "bin" / "python3.14");
    std::filesystem::create_symlink("python3.14", environment_path / "bin" / "python");
    writeTextFile(environment_path / "pyvenv.cfg", "home = " + earlier.python.parent_path().string() +
                                                       "\n"
                                                       "include-system-site-packages = false\n"
                                                       "version = 3.14.4\n"
                                                       "executable = " +
                                                       earlier.python.string() +
                                                       "\ncommand = " + earlier.python.string() + " -m venv " +
                                                       environment_path.string() + "\n");
    std::filesystem::remove_all(earlier.root);

    ExtensionDiscoveryContext current_context = contextForPackageCopy(current, appImageIdentity());
    current_context.environments_root = earlier_context.environments_root;
    const auto reused = an::ui::extensions::load_extension_manifest_file(package_root / "extension.json",
                                                                         ExtensionPackageSource::User, current_context);
    ASSERT_TRUE(reused.environment.has_value());
    EXPECT_EQ(reused.environment->environment_path, environment_path);
    EXPECT_EQ(reused.environment->state, ExtensionEnvironmentState::Ready) << reused.environment->diagnostic;
    std::error_code ec;
    EXPECT_EQ(std::filesystem::read_symlink(environment_path / "bin" / "python3.14", ec), current.python);
    EXPECT_TRUE(std::filesystem::is_regular_file(environment_path / "bin" / "python"));

    const std::string config = readTextFile(environment_path / "pyvenv.cfg");
    EXPECT_NE(config.find("home = " + current.python.parent_path().string() + "\n"), std::string::npos) << config;
    EXPECT_NE(config.find("executable = " + current.python.string() + "\n"), std::string::npos) << config;
    EXPECT_NE(config.find("command = " + current.python.string() + " -m venv "), std::string::npos) << config;
    EXPECT_NE(config.find("include-system-site-packages = false\nversion = 3.14.4\n"), std::string::npos) << config;
    EXPECT_EQ(config.find(earlier.root.string()), std::string::npos) << config;

    // Nothing is left beside the link or the configuration, and a second look changes nothing.
    for (const auto& entry : std::filesystem::directory_iterator(environment_path / "bin"))
    {
        EXPECT_EQ(entry.path().filename().string().find(".repoint"), std::string::npos) << entry.path();
    }
    for (const auto& entry : std::filesystem::directory_iterator(environment_path))
    {
        EXPECT_EQ(entry.path().filename().string().find(".repoint"), std::string::npos) << entry.path();
    }
    const auto again = an::ui::extensions::load_extension_manifest_file(package_root / "extension.json",
                                                                        ExtensionPackageSource::User, current_context);
    ASSERT_TRUE(again.environment.has_value());
    EXPECT_EQ(again.environment->state, ExtensionEnvironmentState::Ready) << again.environment->diagnostic;
    EXPECT_EQ(readTextFile(environment_path / "pyvenv.cfg"), config);
}

// The startup catalog registers a ready extension's actions before discovery has looked at its environment, so an
// AppImage's catalog entry is pointed at this run's interpreter as it is restored, or a launch would start a host on
// the interpreter of a mount that is gone.
TEST(ExtensionManagerTest, AnAppImagesStartupCatalogEntryIsPointedAtThisRunsInterpreter)
{
    ScopedTempDirectory temp_dir;
    const auto now = std::filesystem::file_time_type::clock::now();
    const PackageCopy earlier = writePackageCopy(temp_dir.path() / "mount_earlier", now);
    const PackageCopy current = writePackageCopy(temp_dir.path() / "mount_current", now + std::chrono::minutes(5));
    const auto package_root = createExtensionPackage(temp_dir.path(), "catalog_moving_ext",
                                                     R"json({
          "id": "catalog_moving_ext",
          "name": "Catalog Moving Extension",
          "version": "1.0.0",
          "type": "python",
          "entry_point": "catalog_moving_ext.entry",
          "process_class": "CatalogMovingExtension",
          "category": "filters",
          "launch_mode": "single_image",
          "package_format_version": 1,
          "protocol_version": 1,
          "sdk_version": 1
        })json");

    ExtensionDiscoveryContext earlier_context = contextForPackageCopy(earlier, appImageIdentity());
    earlier_context.environments_root = temp_dir.path() / "ExtensionEnvs";
    earlier_context.eager_settings_schema_inspection = false;
    const auto made = an::ui::extensions::load_extension_manifest_file(package_root / "extension.json",
                                                                       ExtensionPackageSource::User, earlier_context);
    ASSERT_TRUE(made.environment.has_value());
    const auto environment_path = made.environment->environment_path;
    writeInstallReceipt(environment_path, made.environment->host_bootstrap_fingerprint,
                        made.environment->dependency_fingerprint, made.environment->validation_fingerprint);
    std::filesystem::create_directories(environment_path / "bin");
    std::filesystem::create_symlink(earlier.python, environment_path / "bin" / "python3.14");
    writeTextFile(environment_path / "pyvenv.cfg", "home = " + earlier.python.parent_path().string() +
                                                       "\nexecutable = " + earlier.python.string() + "\n");
    const auto ready = an::ui::extensions::load_extension_manifest_file(package_root / "extension.json",
                                                                        ExtensionPackageSource::User, earlier_context);
    ASSERT_EQ(ready.status, ExtensionLoadStatus::Ready);
    ASSERT_TRUE(ready.environment.has_value());
    ASSERT_EQ(ready.environment->state, ExtensionEnvironmentState::Ready) << ready.environment->diagnostic;
    ASSERT_TRUE(an::ui::extensions::store_ready_extension_startup_catalog({ready}, earlier_context));
    std::filesystem::remove_all(earlier.root);

    ExtensionDiscoveryOptions options;
    options.user_roots.push_back(temp_dir.path());
    options.context = contextForPackageCopy(current, appImageIdentity());
    options.context.environments_root = earlier_context.environments_root;
    options.context.eager_settings_schema_inspection = false;
    const auto restored = an::ui::extensions::load_ready_extension_startup_catalog(options);
    ASSERT_EQ(restored.size(), 1u);
    ASSERT_TRUE(restored.front().environment.has_value());
    EXPECT_EQ(restored.front().environment->environment_path, environment_path);
    std::error_code ec;
    EXPECT_EQ(std::filesystem::read_symlink(environment_path / "bin" / "python3.14", ec), current.python);
    EXPECT_EQ(readTextFile(environment_path / "pyvenv.cfg"),
              "home = " + current.python.parent_path().string() + "\nexecutable = " + current.python.string() + "\n");
}
#endif

// The AppImage runtime states the package file and the directory holding this run's copy of its files; an application
// inside that directory runs from the package, which is known by the file's size and write time.
TEST(ExtensionManagerTest, AnApplicationInsideAnAppImagesFilesRunsFromThatPackage)
{
    ScopedTempDirectory temp_dir;
    const auto appimage = temp_dir.path() / "AfterNight-1.0.0-x86_64.AppImage";
    writeTextFile(appimage, "an AppImage");
    const auto mount = temp_dir.path() / "mount_first";
    const auto app_dir = mount / "usr" / "lib";
    std::filesystem::create_directories(app_dir);

    const auto package = an::ui::extensions::appImagePackageForAppDir(app_dir, appimage, mount);
    ASSERT_TRUE(package.has_value());
    EXPECT_EQ(package->root, std::filesystem::weakly_canonical(mount));
    EXPECT_FALSE(package->identity.empty());

    // Another mount of the same file is the same package; the file written again is another.
    const auto other_mount = temp_dir.path() / "mount_second";
    std::filesystem::create_directories(other_mount / "usr" / "lib");
    const auto remounted =
        an::ui::extensions::appImagePackageForAppDir(other_mount / "usr" / "lib", appimage, other_mount);
    ASSERT_TRUE(remounted.has_value());
    EXPECT_EQ(remounted->identity, package->identity);
    std::filesystem::last_write_time(appimage, std::filesystem::last_write_time(appimage) + std::chrono::hours(1));
    const auto rewritten = an::ui::extensions::appImagePackageForAppDir(app_dir, appimage, mount);
    ASSERT_TRUE(rewritten.has_value());
    EXPECT_NE(rewritten->identity, package->identity);

    // An application outside the directory, or no package file or directory, runs from no AppImage.
    EXPECT_FALSE(an::ui::extensions::appImagePackageForAppDir(temp_dir.path() / "mount_first_deployed", appimage, mount)
                     .has_value());
    EXPECT_FALSE(an::ui::extensions::appImagePackageForAppDir(app_dir, {}, mount).has_value());
    EXPECT_FALSE(
        an::ui::extensions::appImagePackageForAppDir(app_dir, temp_dir.path() / "missing.AppImage", mount).has_value());
    EXPECT_FALSE(an::ui::extensions::appImagePackageForAppDir(app_dir, appimage, {}).has_value());

    // The discovery the application runs reads what the runtime states.
    qputenv("APPIMAGE", QByteArray::fromStdString(appimage.string()));
    qputenv("APPDIR", QByteArray::fromStdString(mount.string()));
    const auto options = an::ui::extensions::default_extension_discovery_options_for_app_dir(app_dir);
    qunsetenv("APPIMAGE");
    qunsetenv("APPDIR");
    ASSERT_TRUE(options.context.package.has_value());
    EXPECT_EQ(options.context.package->identity, rewritten->identity);
    const auto deployed = an::ui::extensions::default_extension_discovery_options_for_app_dir(app_dir);
    EXPECT_FALSE(deployed.context.package.has_value());
}

// Between a repository package and a local package of one identity, discovery keeps the one its precedence
// names.
TEST(ExtensionManagerTest, DiscoveryAppliesRepositoryAndLocalPackageSourcePrecedence)
{
    ScopedTempDirectory temp_dir;
    const auto bundled_root = temp_dir.path() / "bundled";
    const auto repository_root = temp_dir.path() / "repository";
    const auto local_package_root = temp_dir.path() / "local";

    createExtensionPackage(bundled_root, "shared_ext",
                           R"json({
          "id": "shared_ext",
          "name": "Bundled Version",
          "version": "1.0.0",
          "type": "python",
          "entry_point": "shared.entry",
          "process_class": "SharedExtension",
          "category": "filters",
          "launch_mode": "single_image",
          "package_format_version": 1,
          "protocol_version": 1,
          "sdk_version": 1
        })json");

    createExtensionPackage(repository_root, "shared_ext",
                           R"json({
          "id": "shared_ext",
          "name": "Repository Version",
          "version": "2.0.0",
          "type": "python",
          "entry_point": "shared.entry",
          "process_class": "SharedExtension",
          "category": "filters",
          "launch_mode": "single_image",
          "package_format_version": 1,
          "protocol_version": 1,
          "sdk_version": 1
        })json");

    createExtensionPackage(local_package_root, "shared_ext",
                           R"json({
          "id": "shared_ext",
          "name": "Local Package Version",
          "version": "3.0.0",
          "type": "python",
          "entry_point": "shared.entry",
          "process_class": "SharedExtension",
          "category": "filters",
          "launch_mode": "single_image",
          "package_format_version": 1,
          "protocol_version": 1,
          "sdk_version": 1
        })json");

    ExtensionDiscoveryOptions options;
    options.bundled_roots = {bundled_root};
    options.repository_roots = {repository_root};
    options.local_package_roots = {local_package_root};
    options.context = defaultContext();

    ExtensionManager manager;
    const auto discovered = manager.discoverExtensions(options);
    ASSERT_EQ(discovered.size(), 3u);

    int ready_count = 0;
    int invalid_count = 0;
    std::string ready_name;
    ExtensionPackageSource ready_source = ExtensionPackageSource::Bundled;
    for (const auto& result : discovered)
    {
        if (result.status == ExtensionLoadStatus::Ready && result.manifest)
        {
            ++ready_count;
            ready_name = result.manifest->name;
            ready_source = result.source;
        }
        if (result.status == ExtensionLoadStatus::Invalid)
        {
            ++invalid_count;
        }
    }

    EXPECT_EQ(ready_count, 1);
    EXPECT_EQ(invalid_count, 2);
    EXPECT_EQ(ready_name, "Local Package Version");
    EXPECT_EQ(ready_source, ExtensionPackageSource::LocalPackage);
}

// An extension that cannot run is registered disabled, its descriptor saying why.
TEST(ExtensionManagerTest, RegisterDiscoveredProcessesCreatesDisabledDescriptorsWithReasons)
{
    ScopedTempDirectory temp_dir;
    const auto package_root = createExtensionPackage(temp_dir.path(), "rpc_ext",
                                                     R"json({
          "id": "rpc_ext",
          "name": "RPC Extension",
          "version": "1.0.0",
          "type": "python",
          "entry_point": "rpc_ext.entry",
          "process_class": "RpcExtension",
          "category": "filters",
          "launch_mode": "single_image",
          "package_format_version": 1,
          "protocol_version": 1,
          "sdk_version": 1,
          "sdk_backend": "rpc",
          "capabilities": {
            "source_channel_filter": [3]
          }
        })json");

    ExtensionManager manager;
    const auto discovered = std::vector{an::ui::extensions::load_extension_manifest_file(
        package_root / "extension.json", ExtensionPackageSource::User, defaultContext())};

    // The built-in registry is put back when the case ends, passing or failing: later cases launch through it.
    const an::tests::processes::shared::RegistrySnapshot registrySnapshot;
    ProcessRegistry& registry = ProcessRegistry::instance();
    registry.clear();
    manager.registerDiscoveredProcesses(discovered, registry);

    const auto* descriptor = registry.find("rpc_ext");
    ASSERT_NE(descriptor, nullptr);
    EXPECT_EQ(descriptor->availability, ProcessAvailability::Incompatible);
    EXPECT_NE(descriptor->availability_reason.find("rpc SDK backend"), std::string::npos);
    EXPECT_EQ(descriptor->source_channel_filter, std::vector<int>{3});

    registry.clear();
}

// Registering a new discovery removes the extension descriptors the previous one registered.
TEST(ExtensionManagerTest, RegisterDiscoveredProcessesRemovesPreviouslyRegisteredExtensionDescriptors)
{
    const auto makeResult = [](const std::string& extension_id,
                               const std::string& display_name) -> an::ui::extensions::ExtensionLoadResult {
        an::ui::extensions::ExtensionProcessManifest process;
        process.id = extension_id;
        process.class_name = "TestExtension";
        process.display_name = display_name;
        process.process_name = display_name;
        process.action_object_name = an::ui::extensions::default_extension_action_object_name(extension_id);
        process.category = "filters";

        an::ui::extensions::ExtensionManifest manifest;
        manifest.id = extension_id;
        manifest.name = display_name;
        manifest.version = "1.0.0";
        manifest.entry_point = extension_id + ".entry";
        manifest.category = "filters";
        manifest.launch_mode = an::ui::extensions::ExtensionLaunchMode::SingleImage;
        manifest.processes.push_back(std::move(process));

        an::ui::extensions::ExtensionLoadResult result;
        result.status = ExtensionLoadStatus::Ready;
        result.manifest = std::move(manifest);
        return result;
    };

    // The built-in registry is put back when the case ends, passing or failing: later cases launch through it.
    const an::tests::processes::shared::RegistrySnapshot registrySnapshot;
    ProcessRegistry& registry = ProcessRegistry::instance();
    registry.clear();

    an::ui::process_registry::ProcessDescriptor builtin;
    builtin.id = "builtin.process";
    builtin.display_name = "Builtin Process";
    builtin.category = "filters";
    builtin.action_object_name = "actionBuiltinProcess";
    builtin.process_name = "Builtin Process";
    ASSERT_TRUE(registry.registerProcess(std::move(builtin)));

    ExtensionManager manager;
    manager.registerDiscoveredProcesses({makeResult("stale_ext", "Stale Extension")}, registry);

    const auto* stale_descriptor = registry.find("stale_ext");
    ASSERT_NE(stale_descriptor, nullptr);
    EXPECT_TRUE(stale_descriptor->is_extension_process);
    EXPECT_EQ(stale_descriptor->extension_id, "stale_ext");

    manager.registerDiscoveredProcesses({}, registry);

    EXPECT_EQ(registry.find("stale_ext"), nullptr);
    EXPECT_NE(registry.find("builtin.process"), nullptr);

    registry.clear();
}

// The VeraLux repository package installs, provisions and caches each of its processes' settings schemas.
TEST(ExtensionManagerTest, VeraLuxRepositoryPackageInstallsProvisionsAndCachesPerProcessSchemas)
{
    if (environmentVariableValue("AFTERNIGHT_RUN_VERALUX_PROVISIONING_SMOKE") != "1")
    {
        GTEST_SKIP()
            << "Set AFTERNIGHT_RUN_VERALUX_PROVISIONING_SMOKE=1 to run the VeraLux online provisioning smoke test";
    }

    const std::string repository_root_env = environmentVariableValue("AFTERNIGHT_EXTENSION_REPOSITORY_ROOT");
    if (repository_root_env.empty())
    {
        GTEST_SKIP() << "Set AFTERNIGHT_EXTENSION_REPOSITORY_ROOT to the afternight-extensions checkout";
    }

    const std::filesystem::path repository_root(repository_root_env);
    const std::filesystem::path package_root = repository_root / "packages" / "veralux" / "package";
    std::error_code ec;
    ASSERT_TRUE(std::filesystem::is_regular_file(package_root / "extension.json", ec))
        << "Expected VeraLux package manifest at " << package_root / "extension.json";

    const std::filesystem::path host_path = extensionHostPathForCurrentAppDir();
    const std::filesystem::path runtime_root = runtimeRootForCurrentAppDir();
    const std::filesystem::path bundled_python = findRepositoryPythonExecutable();
    AN_REQUIRE_FULL_EXTENSION_HOST(host_path);
    ASSERT_TRUE(std::filesystem::exists(runtime_root)) << runtime_root;
    ASSERT_FALSE(bundled_python.empty()) << "Repository bundled Python runtime must be available";

    ExtensionDiscoveryContext context = defaultContext();
    context.extension_host_executable = host_path;
    context.runtime_root = runtime_root;
    context.bundled_python_executable = bundled_python;

    an::ui::extensions::ExtensionPackageValidationOptions validation_options;
    validation_options.context = context;
    validation_options.source = ExtensionPackageSource::Repository;
    validation_options.require_repository_metadata = true;
    validation_options.require_current_runtime_compatibility = true;

    const auto package_validation =
        an::ui::extensions::validate_extension_package_root(package_root, validation_options);
    ASSERT_TRUE(package_validation.valid)
        << package_root << " failed app-side package validation:\n"
        << describeValidationIssues(package_validation.issues) << package_validation.load_result.diagnostic;
    ASSERT_TRUE(package_validation.load_result.manifest.has_value());
    const auto& source_manifest = *package_validation.load_result.manifest;
    ASSERT_EQ(source_manifest.id, "veralux");
    ASSERT_EQ(source_manifest.processes.size(), 8u);
    EXPECT_EQ(source_manifest.dependencies.dependency_context, ExtensionDependencyContext::Private);
    EXPECT_EQ(source_manifest.dependencies.requirements_file, "requirements.lock");
    EXPECT_EQ(source_manifest.dependencies.bundled_runtime_packages, std::vector<std::string>{"numpy"});

    ScopedTempDirectory temp_dir;
    context.environments_root = temp_dir.path() / "ExtensionEnvs";
    const auto archive_path = temp_dir.path() / "archives" / "veralux-0.1.0-all.tar";
    createPackageArchiveFromDirectory(archive_path, package_root, "veralux", false);
    const std::string archive_hash = sha256FileHex(archive_path);
    ASSERT_FALSE(archive_hash.empty());

    std::vector<ExtensionOperationProgress> progress_events;
    an::ui::extensions::ExtensionPackageInstallRequest request;
    request.archive_path = archive_path;
    request.install_root = temp_dir.path() / "installed";
    request.context = context;
    request.source = ExtensionPackageSource::Repository;
    request.source_repository_url = "https://github.com/afternight/afternight-extensions";
    request.expected_archive_hash_sha256 = archive_hash;
    request.selected_release_asset = "veralux-0.1.0-all.tar";
    request.signature_state = ExtensionPackageSignatureState::Unsigned;
    request.signature_detail = "unsigned VeraLux provisioning smoke";
    request.require_repository_metadata = true;
    request.require_current_runtime_compatibility = true;
    request.provision_environment = true;
    request.prime_startup_caches = true;
    request.require_settings_schema_cache = true;
    request.progress_callback = [&](const ExtensionOperationProgress& progress) {
        progress_events.push_back(progress);
    };

    an::ui::extensions::ExtensionPackageTransactionService service;
    const auto result = service.installArchive(request);

    ASSERT_TRUE(result.success) << result.diagnostic;
    expectDeterminateMonotonicProgress(progress_events);
    EXPECT_TRUE(progressContainsPhase(progress_events, "provision_environment"));
    EXPECT_TRUE(progressContainsPhase(progress_events, "stage_runtime"));
    EXPECT_TRUE(progressContainsPhase(progress_events, "install_dependencies"));
    EXPECT_TRUE(progressContainsPhase(progress_events, "precompile_bytecode"));
    EXPECT_TRUE(progressContainsPhase(progress_events, "prime_startup_caches"));
    const auto stage_runtime_progress =
        std::find_if(progress_events.begin(), progress_events.end(),
                     [](const ExtensionOperationProgress& progress) { return progress.phase_id == "stage_runtime"; });
    const auto install_dependencies_progress =
        std::find_if(progress_events.begin(), progress_events.end(), [](const ExtensionOperationProgress& progress) {
            return progress.phase_id == "install_dependencies";
        });
    const auto startup_cache_progress =
        std::find_if(progress_events.begin(), progress_events.end(), [](const ExtensionOperationProgress& progress) {
            return progress.phase_id == "prime_startup_caches";
        });
    std::vector<ExtensionOperationProgress> startup_cache_progress_events;
    std::copy_if(
        progress_events.begin(), progress_events.end(), std::back_inserter(startup_cache_progress_events),
        [](const ExtensionOperationProgress& progress) { return progress.phase_id == "prime_startup_caches"; });
    ASSERT_NE(stage_runtime_progress, progress_events.end());
    ASSERT_NE(install_dependencies_progress, progress_events.end());
    ASSERT_NE(startup_cache_progress, progress_events.end());
    ASSERT_GE(startup_cache_progress_events.size(), 9u);
    EXPECT_FALSE(stage_runtime_progress->indeterminate);
    EXPECT_FALSE(install_dependencies_progress->indeterminate);
    EXPECT_GT(install_dependencies_progress->current, stage_runtime_progress->current);
    EXPECT_LT(install_dependencies_progress->current, startup_cache_progress->current);
    EXPECT_EQ(install_dependencies_progress->total, startup_cache_progress->total);
    EXPECT_GT(startup_cache_progress_events.back().current, startup_cache_progress->current);
    EXPECT_EQ(startup_cache_progress_events.back().total, startup_cache_progress->total);
    ASSERT_TRUE(result.environment.has_value());
    EXPECT_EQ(result.environment->state, ExtensionEnvironmentState::Ready);
    EXPECT_TRUE(std::filesystem::exists(result.environment->environment_path / ".environment_ready"));
    EXPECT_TRUE(std::filesystem::exists(result.environment->receipt_path));
    ASSERT_TRUE(result.startup_cache_priming.has_value());
    EXPECT_TRUE(result.startup_cache_priming->success) << result.startup_cache_priming->diagnostic;
    EXPECT_TRUE(result.startup_cache_priming->environment_ready);
    EXPECT_TRUE(result.startup_cache_priming->settings_schema_ready);
    ASSERT_TRUE(result.startup_cache_priming->load_result.settings_schema.has_value());
    const auto& primed_schema = *result.startup_cache_priming->load_result.settings_schema;
    EXPECT_EQ(primed_schema.state, an::ui::extensions::ExtensionSettingsSchemaState::Ready);
    EXPECT_TRUE(primed_schema.schema.isEmpty());
    ASSERT_EQ(primed_schema.process_schemas.size(), 8u);
    for (const char* class_name :
         {"VeraLuxAlchemyExtension", "VeraLuxCurvesExtension", "VeraLuxHyperMetricStretchExtension",
          "VeraLuxNoxExtension", "VeraLuxRevelaExtension", "VeraLuxSilentiumExtension", "VeraLuxStarComposerExtension",
          "VeraLuxVectraExtension"})
    {
        ASSERT_TRUE(primed_schema.process_schemas.contains(class_name)) << class_name;
        EXPECT_FALSE(primed_schema.process_schemas.at(class_name).isEmpty()) << class_name;
    }

    const QJsonObject receipt = loadJsonFile(result.environment->receipt_path);
    const auto distributions = receipt.value(QStringLiteral("resolved_distributions")).toArray();
    const auto receipt_has_distribution = [&distributions](const char* name) {
        for (const auto& dist : distributions)
        {
            if (dist.toObject().value(QStringLiteral("name")).toString() == QString::fromLatin1(name))
            {
                return true;
            }
        }
        return false;
    };
    EXPECT_TRUE(receipt_has_distribution("numpy"));
    EXPECT_TRUE(receipt_has_distribution("opencv-python-headless"));

    ExtensionDiscoveryOptions discovery_options;
    discovery_options.repository_roots = {request.install_root};
    discovery_options.context = context;
    discovery_options.context.eager_settings_schema_inspection = false;
    ExtensionManager manager;
    const auto discovered = manager.discoverExtensions(discovery_options);
    ASSERT_EQ(discovered.size(), 1u);
    ASSERT_TRUE(discovered.front().manifest.has_value());
    EXPECT_EQ(discovered.front().manifest->id, "veralux");
    EXPECT_EQ(discovered.front().manifest->processes.size(), 8u);
    ASSERT_TRUE(discovered.front().settings_schema.has_value());
    EXPECT_EQ(discovered.front().settings_schema->state, an::ui::extensions::ExtensionSettingsSchemaState::Ready);
    EXPECT_TRUE(discovered.front().settings_schema->from_cache);
    EXPECT_EQ(discovered.front().settings_schema->process_schemas.size(), 8u);

    // The built-in registry is put back when the case ends, passing or failing: later cases launch through it.
    const an::tests::processes::shared::RegistrySnapshot registrySnapshot;
    ProcessRegistry& registry = ProcessRegistry::instance();
    registry.clear();
    manager.registerDiscoveredProcesses(discovered, registry);
    EXPECT_NE(registry.find("veralux.alchemy"), nullptr);
    EXPECT_NE(registry.find("veralux.curves"), nullptr);
    EXPECT_NE(registry.find("veralux.hypermetric_stretch"), nullptr);
    EXPECT_NE(registry.find("veralux.nox"), nullptr);
    EXPECT_NE(registry.find("veralux.revela"), nullptr);
    EXPECT_NE(registry.find("veralux.silentium"), nullptr);
    EXPECT_NE(registry.find("veralux.starcomposer"), nullptr);
    EXPECT_NE(registry.find("veralux.vectra"), nullptr);
    registry.clear();
}

// An extension with no environment is registered as needing an install.
TEST(ExtensionManagerTest, MissingEnvironmentMapsToInstallNeededDescriptorState)
{
    ScopedTempDirectory temp_dir;
    const auto package_root = createExtensionPackage(temp_dir.path(), "install_needed_ext",
                                                     R"json({
          "id": "install_needed_ext",
          "name": "Install Needed Extension",
          "version": "1.0.0",
          "type": "python",
          "entry_point": "install_needed.entry",
          "process_class": "InstallNeededExtension",
          "category": "filters",
          "launch_mode": "single_image",
          "package_format_version": 1,
          "protocol_version": 1,
          "sdk_version": 1
        })json");

    ExtensionDiscoveryContext context = defaultContext();
    context.environments_root = temp_dir.path() / "ExtensionEnvs";

    const auto result = an::ui::extensions::load_extension_manifest_file(package_root / "extension.json",
                                                                         ExtensionPackageSource::User, context);

    ASSERT_TRUE(result.environment.has_value());
    EXPECT_EQ(result.environment->state, ExtensionEnvironmentState::Missing);

    // The built-in registry is put back when the case ends, passing or failing: later cases launch through it.
    const an::tests::processes::shared::RegistrySnapshot registrySnapshot;
    ProcessRegistry& registry = ProcessRegistry::instance();
    registry.clear();
    ExtensionManager manager;
    manager.registerDiscoveredProcesses({result}, registry);

    const auto* descriptor = registry.find("install_needed_ext");
    ASSERT_NE(descriptor, nullptr);
    EXPECT_EQ(descriptor->availability, ProcessAvailability::InstallNeeded);
    EXPECT_NE(descriptor->availability_reason.find("not been provisioned"), std::string::npos);

    registry.clear();
}

// An extension with a ready environment is registered as available.
TEST(ExtensionManagerTest, ReadyEnvironmentMapsToAvailableDescriptorState)
{
    ScopedTempDirectory temp_dir;
    const auto package_root = createExtensionPackage(temp_dir.path(), "ready_ext",
                                                     R"json({
          "id": "ready_ext",
          "name": "Ready Extension",
          "version": "1.0.0",
          "type": "python",
          "entry_point": "ready_ext.entry",
          "process_class": "ReadyExtension",
          "category": "filters",
          "launch_mode": "single_image",
          "package_format_version": 1,
          "protocol_version": 1,
          "sdk_version": 1
        })json");

    ExtensionDiscoveryContext context = defaultContext();
    context.environments_root = temp_dir.path() / "ExtensionEnvs";

    const auto initial = an::ui::extensions::load_extension_manifest_file(package_root / "extension.json",
                                                                          ExtensionPackageSource::User, context);
    ASSERT_TRUE(initial.environment.has_value());

    writeInstallReceipt(initial.environment->environment_path, initial.environment->host_bootstrap_fingerprint,
                        initial.environment->dependency_fingerprint);

    const auto ready = an::ui::extensions::load_extension_manifest_file(package_root / "extension.json",
                                                                        ExtensionPackageSource::User, context);

    ASSERT_TRUE(ready.environment.has_value());
    EXPECT_EQ(ready.environment->state, ExtensionEnvironmentState::Ready);

    // The built-in registry is put back when the case ends, passing or failing: later cases launch through it.
    const an::tests::processes::shared::RegistrySnapshot registrySnapshot;
    ProcessRegistry& registry = ProcessRegistry::instance();
    registry.clear();
    ExtensionManager manager;
    manager.registerDiscoveredProcesses({ready}, registry);

    const auto* descriptor = registry.find("ready_ext");
    ASSERT_NE(descriptor, nullptr);
    EXPECT_EQ(descriptor->availability, ProcessAvailability::Available);
    EXPECT_TRUE(descriptor->availability_reason.empty());

    registry.clear();
}

// A ready extension the startup catalog recorded is restored from it while nothing about it has changed.
TEST(ExtensionManagerTest, StartupActivationCatalogRestoresUnchangedReadyExtension)
{
    ScopedTempDirectory temp_dir;
    const auto package_root = createExtensionPackage(temp_dir.path(), "startup_catalog_ext",
                                                     R"json({
          "id": "startup_catalog_ext",
          "name": "Startup Catalog Extension",
          "version": "1.0.0",
          "type": "python",
          "entry_point": "startup_catalog_ext.entry",
          "process_class": "StartupCatalogExtension",
          "category": "filters",
          "launch_mode": "single_image",
          "package_format_version": 1,
          "protocol_version": 1,
          "sdk_version": 1
        })json");

    ExtensionDiscoveryContext context = defaultContext();
    context.environments_root = temp_dir.path() / "ExtensionEnvs";

    const auto initial = an::ui::extensions::load_extension_manifest_file(package_root / "extension.json",
                                                                          ExtensionPackageSource::User, context);
    ASSERT_TRUE(initial.environment.has_value());
    writeInstallReceipt(initial.environment->environment_path, initial.environment->host_bootstrap_fingerprint,
                        initial.environment->dependency_fingerprint, initial.environment->validation_fingerprint);

    const auto ready = an::ui::extensions::load_extension_manifest_file(package_root / "extension.json",
                                                                        ExtensionPackageSource::User, context);
    ASSERT_EQ(ready.status, ExtensionLoadStatus::Ready);
    ASSERT_TRUE(ready.environment.has_value());
    ASSERT_EQ(ready.environment->state, ExtensionEnvironmentState::Ready);

    ExtensionDiscoveryOptions options;
    options.user_roots.push_back(temp_dir.path());
    options.context = context;
    options.context.eager_settings_schema_inspection = false;

    ASSERT_TRUE(an::ui::extensions::store_ready_extension_startup_catalog({ready}, options.context));
    const auto restored = an::ui::extensions::load_ready_extension_startup_catalog(options);
    ASSERT_EQ(restored.size(), 1u);
    ASSERT_TRUE(restored.front().manifest.has_value());
    ASSERT_TRUE(restored.front().environment.has_value());
    EXPECT_EQ(restored.front().manifest->id, "startup_catalog_ext");
    EXPECT_EQ(restored.front().environment->state, ExtensionEnvironmentState::Ready);

    writeTextFile(ready.environment->receipt_path, "{}");
    EXPECT_TRUE(an::ui::extensions::load_ready_extension_startup_catalog(options).empty());

    writeInstallReceipt(ready.environment->environment_path, ready.environment->host_bootstrap_fingerprint,
                        ready.environment->dependency_fingerprint, ready.environment->validation_fingerprint);
    const auto ready_after_receipt_repair = an::ui::extensions::load_extension_manifest_file(
        package_root / "extension.json", ExtensionPackageSource::User, context);
    ASSERT_EQ(ready_after_receipt_repair.status, ExtensionLoadStatus::Ready);
    ASSERT_TRUE(
        an::ui::extensions::store_ready_extension_startup_catalog({ready_after_receipt_repair}, options.context));

    writeTextFile(package_root / "extension.json", R"json({"id":"startup_catalog_ext"})json");
    EXPECT_TRUE(an::ui::extensions::load_ready_extension_startup_catalog(options).empty());
}

// Discovery inspects a ready extension's settings schema through the host and caches it.
TEST(ExtensionManagerTest, DiscoverExtensionsEagerlyInspectsAndCachesSettingsSchemaForReadyEnvironment)
{
    const std::filesystem::path host_path = extensionHostPathForCurrentAppDir();
    const std::filesystem::path runtime_root = runtimeRootForCurrentAppDir();
    const std::filesystem::path bundled_python = findRepositoryPythonExecutable();
    AN_REQUIRE_FULL_EXTENSION_HOST(host_path);
    ASSERT_TRUE(std::filesystem::exists(runtime_root)) << runtime_root;
    ASSERT_FALSE(bundled_python.empty()) << "Repository bundled Python runtime must be available";

    ScopedTempDirectory temp_dir;
    const auto package_root = createExtensionPackage(temp_dir.path(), "settings_schema_ext",
                                                     R"json({
          "id": "settings_schema_ext",
          "name": "Settings Schema Extension",
          "version": "1.0.0",
          "type": "python",
          "entry_point": "settings_schema_ext",
          "process_class": "SettingsSchemaExtension",
          "category": "filters",
          "launch_mode": "single_image",
          "package_format_version": 1,
          "protocol_version": 1,
          "sdk_version": 1
        })json");
    writeTextFile(package_root / "settings_schema_ext.py",
                  "import afternight\n\n"
                  "class SettingsSchemaExtension(afternight.ProcessExtension):\n"
                  "    def get_settings_params(self):\n"
                  "        return [\n"
                  "            {'id': 'tool_path', 'type': 'string', 'default': ''},\n"
                  "            {'id': 'gpu_enabled', 'type': 'bool', 'default': True},\n"
                  "        ]\n\n"
                  "    def get_params(self):\n"
                  "        return [\n"
                  "            {'id': 'background_model', 'type': 'choice', 'default': 'AI'},\n"
                  "        ]\n");

    ExtensionDiscoveryOptions options;
    options.user_roots.push_back(temp_dir.path());
    options.context = defaultContext();
    options.context.environments_root = temp_dir.path() / "ExtensionEnvs";
    options.context.extension_host_executable = host_path;
    options.context.runtime_root = runtime_root;

    const auto initial = an::ui::extensions::load_extension_manifest_file(
        package_root / "extension.json", ExtensionPackageSource::User, options.context);
    ASSERT_TRUE(initial.environment.has_value());

    createVirtualEnvironment(bundled_python, initial.environment->environment_path);
    writeInstallReceipt(initial.environment->environment_path, initial.environment->host_bootstrap_fingerprint,
                        initial.environment->dependency_fingerprint, initial.environment->validation_fingerprint);

    ExtensionManager manager;
    const auto first_discovery = manager.discoverExtensions(options);
    ASSERT_EQ(first_discovery.size(), 1u);
    ASSERT_TRUE(first_discovery.front().environment.has_value());
    ASSERT_TRUE(first_discovery.front().settings_schema.has_value());
    const auto& first_schema = *first_discovery.front().settings_schema;
    EXPECT_EQ(first_schema.state, an::ui::extensions::ExtensionSettingsSchemaState::Ready);
    EXPECT_FALSE(first_schema.from_cache);
    ASSERT_EQ(first_schema.schema.size(), 2);
    EXPECT_EQ(first_schema.schema.at(0).toObject().value(QStringLiteral("id")).toString(), QStringLiteral("tool_path"));
    EXPECT_EQ(first_schema.schema.at(1).toObject().value(QStringLiteral("id")).toString(),
              QStringLiteral("gpu_enabled"));
    ASSERT_FALSE(first_schema.cache_path.empty());
    ASSERT_TRUE(std::filesystem::exists(first_schema.cache_path));

    ASSERT_TRUE(first_schema.process_params_schemas.contains("SettingsSchemaExtension"));
    ASSERT_EQ(first_schema.process_params_schemas.at("SettingsSchemaExtension").size(), 1);
    EXPECT_EQ(first_schema.process_params_schemas.at("SettingsSchemaExtension")
                  .at(0)
                  .toObject()
                  .value(QStringLiteral("id"))
                  .toString(),
              QStringLiteral("background_model"));

    const QJsonObject cache_object = loadJsonFile(first_schema.cache_path);
    EXPECT_EQ(cache_object.value(QStringLiteral("state")).toString(), QStringLiteral("ready"));
    ASSERT_TRUE(cache_object.value(QStringLiteral("settings_schema")).isArray());
    EXPECT_EQ(cache_object.value(QStringLiteral("settings_schema")).toArray().size(), 2);
    const QJsonObject process_params_schemas = cache_object.value(QStringLiteral("process_params_schemas")).toObject();
    ASSERT_TRUE(process_params_schemas.value(QStringLiteral("SettingsSchemaExtension")).isArray());
    EXPECT_EQ(process_params_schemas.value(QStringLiteral("SettingsSchemaExtension"))
                  .toArray()
                  .at(0)
                  .toObject()
                  .value(QStringLiteral("id"))
                  .toString(),
              QStringLiteral("background_model"));

    QJsonObject stale_cache_object = cache_object;
    stale_cache_object.insert(QStringLiteral("cache_format_version"), 2);
    stale_cache_object.remove(QStringLiteral("process_params_schemas"));
    writeTextFile(first_schema.cache_path,
                  QJsonDocument(stale_cache_object).toJson(QJsonDocument::Indented).toStdString());

    const auto reinspected_discovery = manager.discoverExtensions(options);
    ASSERT_EQ(reinspected_discovery.size(), 1u);
    ASSERT_TRUE(reinspected_discovery.front().settings_schema.has_value());
    const auto& reinspected_schema = *reinspected_discovery.front().settings_schema;
    EXPECT_EQ(reinspected_schema.state, an::ui::extensions::ExtensionSettingsSchemaState::Ready);
    EXPECT_FALSE(reinspected_schema.from_cache);
    ASSERT_TRUE(reinspected_schema.process_params_schemas.contains("SettingsSchemaExtension"));
    EXPECT_EQ(reinspected_schema.process_params_schemas.at("SettingsSchemaExtension"),
              first_schema.process_params_schemas.at("SettingsSchemaExtension"));
    const QJsonObject reinspected_cache_object = loadJsonFile(first_schema.cache_path);
    EXPECT_EQ(reinspected_cache_object.value(QStringLiteral("cache_format_version")).toInt(), 3);
    ASSERT_TRUE(reinspected_cache_object.value(QStringLiteral("process_params_schemas")).isObject());

    const auto second_discovery = manager.discoverExtensions(options);
    ASSERT_EQ(second_discovery.size(), 1u);
    ASSERT_TRUE(second_discovery.front().settings_schema.has_value());
    const auto& second_schema = *second_discovery.front().settings_schema;
    EXPECT_EQ(second_schema.state, an::ui::extensions::ExtensionSettingsSchemaState::Ready);
    EXPECT_TRUE(second_schema.from_cache);
    EXPECT_EQ(second_schema.schema.size(), 2);
    ASSERT_TRUE(second_schema.process_params_schemas.contains("SettingsSchemaExtension"));
    EXPECT_EQ(second_schema.process_params_schemas.at("SettingsSchemaExtension"),
              first_schema.process_params_schemas.at("SettingsSchemaExtension"));
}

// A settings schema whose cached inspection failed is inspected again.
TEST(ExtensionManagerTest, DiscoverExtensionsReinspectsFailedSettingsSchemaCache)
{
    const std::filesystem::path host_path = extensionHostPathForCurrentAppDir();
    const std::filesystem::path runtime_root = runtimeRootForCurrentAppDir();
    const std::filesystem::path bundled_python = findRepositoryPythonExecutable();
    AN_REQUIRE_FULL_EXTENSION_HOST(host_path);
    ASSERT_TRUE(std::filesystem::exists(runtime_root)) << runtime_root;
    ASSERT_FALSE(bundled_python.empty()) << "Repository bundled Python runtime must be available";

    ScopedTempDirectory temp_dir;
    const auto package_root = createExtensionPackage(temp_dir.path(), "failed_schema_cache_ext",
                                                     R"json({
          "id": "failed_schema_cache_ext",
          "name": "Failed Schema Cache Extension",
          "version": "1.0.0",
          "type": "python",
          "entry_point": "failed_schema_cache_ext",
          "process_class": "FailedSchemaCacheExtension",
          "category": "filters",
          "launch_mode": "single_image",
          "package_format_version": 1,
          "protocol_version": 1,
          "sdk_version": 1
        })json");
    writeTextFile(package_root / "failed_schema_cache_ext.py",
                  "import afternight\n\n"
                  "class FailedSchemaCacheExtension(afternight.ProcessExtension):\n"
                  "    def get_settings_params(self):\n"
                  "        return [{'id': 'ready_after_failed_cache', 'type': 'bool', 'default': True}]\n");

    ExtensionDiscoveryOptions options;
    options.user_roots.push_back(temp_dir.path());
    options.context = defaultContext();
    options.context.environments_root = temp_dir.path() / "ExtensionEnvs";
    options.context.extension_host_executable = host_path;
    options.context.runtime_root = runtime_root;

    const auto initial = an::ui::extensions::load_extension_manifest_file(
        package_root / "extension.json", ExtensionPackageSource::User, options.context);
    ASSERT_TRUE(initial.environment.has_value());

    createVirtualEnvironment(bundled_python, initial.environment->environment_path);
    writeInstallReceipt(initial.environment->environment_path, initial.environment->host_bootstrap_fingerprint,
                        initial.environment->dependency_fingerprint, initial.environment->validation_fingerprint);

    const std::filesystem::path cache_path = options.context.environments_root / "_settings_schema_cache" /
                                             "failed_schema_cache_ext" /
                                             (initial.environment->validation_fingerprint + ".json");
    std::filesystem::create_directories(cache_path.parent_path());
    writeTextFile(cache_path, "{\n"
                              "  \"cache_format_version\": 2,\n"
                              "  \"state\": \"failed\",\n"
                              "  \"validation_fingerprint\": \"" +
                                  initial.environment->validation_fingerprint +
                                  "\",\n"
                                  "  \"diagnostic\": \"stale failure from an older runtime\",\n"
                                  "  \"settings_schema\": []\n"
                                  "}\n");

    ExtensionManager manager;
    const auto discovery = manager.discoverExtensions(options);
    ASSERT_EQ(discovery.size(), 1u);
    ASSERT_TRUE(discovery.front().settings_schema.has_value());
    const auto& schema = *discovery.front().settings_schema;
    EXPECT_EQ(schema.state, an::ui::extensions::ExtensionSettingsSchemaState::Ready);
    EXPECT_FALSE(schema.from_cache);
    ASSERT_EQ(schema.schema.size(), 1);
    EXPECT_EQ(schema.schema.at(0).toObject().value(QStringLiteral("id")).toString(),
              QStringLiteral("ready_after_failed_cache"));

    const QJsonObject cache_object = loadJsonFile(cache_path);
    EXPECT_EQ(cache_object.value(QStringLiteral("state")).toString(), QStringLiteral("ready"));
}

// Priming the startup caches writes a ready extension's settings schema.
TEST(ExtensionManagerTest, PrimeStartupCachesWritesSettingsSchemaForReadyEnvironment)
{
    const std::filesystem::path host_path = extensionHostPathForCurrentAppDir();
    const std::filesystem::path runtime_root = runtimeRootForCurrentAppDir();
    const std::filesystem::path bundled_python = findRepositoryPythonExecutable();
    AN_REQUIRE_FULL_EXTENSION_HOST(host_path);
    ASSERT_TRUE(std::filesystem::exists(runtime_root)) << runtime_root;
    ASSERT_FALSE(bundled_python.empty()) << "Repository bundled Python runtime must be available";

    ScopedTempDirectory temp_dir;
    const auto package_root = createExtensionPackage(temp_dir.path(), "prime_schema_ext",
                                                     R"json({
          "id": "prime_schema_ext",
          "name": "Prime Schema Extension",
          "version": "1.0.0",
          "type": "python",
          "entry_point": "prime_schema_ext",
          "process_class": "PrimeSchemaExtension",
          "category": "filters",
          "launch_mode": "single_image",
          "package_format_version": 1,
          "protocol_version": 1,
          "sdk_version": 1
        })json");
    writeTextFile(package_root / "prime_schema_ext.py",
                  "import afternight\n\n"
                  "class PrimeSchemaExtension(afternight.ProcessExtension):\n"
                  "    def get_settings_params(self):\n"
                  "        return [{'id': 'threshold', 'type': 'float', 'default': 0.5}]\n");

    ExtensionDiscoveryContext context = defaultContext();
    context.environments_root = temp_dir.path() / "ExtensionEnvs";
    context.extension_host_executable = host_path;
    context.runtime_root = runtime_root;

    const auto initial = an::ui::extensions::load_extension_manifest_file(package_root / "extension.json",
                                                                          ExtensionPackageSource::User, context);
    ASSERT_TRUE(initial.environment.has_value());

    createVirtualEnvironment(bundled_python, initial.environment->environment_path);
    writeInstallReceipt(initial.environment->environment_path, initial.environment->host_bootstrap_fingerprint,
                        initial.environment->dependency_fingerprint, initial.environment->validation_fingerprint);

    an::ui::extensions::ExtensionStartupCachePrimingOptions priming_options;
    priming_options.context = context;
    const auto primed = an::ui::extensions::prime_extension_startup_caches(initial, priming_options);

    EXPECT_TRUE(primed.success) << primed.diagnostic;
    EXPECT_TRUE(primed.environment_ready);
    EXPECT_TRUE(primed.settings_schema_ready);
    EXPECT_FALSE(primed.settings_schema_from_cache);
    ASSERT_TRUE(primed.load_result.settings_schema.has_value());
    ASSERT_FALSE(primed.load_result.settings_schema->cache_path.empty());
    EXPECT_TRUE(std::filesystem::exists(primed.load_result.settings_schema->cache_path));

    ExtensionDiscoveryOptions options;
    options.user_roots.push_back(temp_dir.path());
    options.context = context;
    options.context.eager_settings_schema_inspection = false;
    ExtensionManager manager;
    const auto cached_discovery = manager.discoverExtensions(options);
    ASSERT_EQ(cached_discovery.size(), 1u);
    ASSERT_TRUE(cached_discovery.front().settings_schema.has_value());
    EXPECT_EQ(cached_discovery.front().settings_schema->state, an::ui::extensions::ExtensionSettingsSchemaState::Ready);
    EXPECT_TRUE(cached_discovery.front().settings_schema->from_cache);
    ASSERT_EQ(cached_discovery.front().settings_schema->schema.size(), 1);
    EXPECT_EQ(cached_discovery.front().settings_schema->schema.at(0).toObject().value(QStringLiteral("id")).toString(),
              QStringLiteral("threshold"));
}

// Discovery asked to defer inspection reads settings schemas from the cache alone.
TEST(ExtensionManagerTest, DiscoverExtensionsCanDeferSettingsSchemaInspectionAndUseCacheOnly)
{
    const std::filesystem::path host_path = extensionHostPathForCurrentAppDir();
    const std::filesystem::path runtime_root = runtimeRootForCurrentAppDir();
    const std::filesystem::path bundled_python = findRepositoryPythonExecutable();
    AN_REQUIRE_FULL_EXTENSION_HOST(host_path);
    ASSERT_TRUE(std::filesystem::exists(runtime_root)) << runtime_root;
    ASSERT_FALSE(bundled_python.empty()) << "Repository bundled Python runtime must be available";

    ScopedTempDirectory temp_dir;
    const auto package_root = createExtensionPackage(temp_dir.path(), "deferred_schema_ext",
                                                     R"json({
          "id": "deferred_schema_ext",
          "name": "Deferred Schema Extension",
          "version": "1.0.0",
          "type": "python",
          "entry_point": "deferred_schema_ext",
          "process_class": "DeferredSchemaExtension",
          "category": "filters",
          "launch_mode": "single_image",
          "package_format_version": 1,
          "protocol_version": 1,
          "sdk_version": 1
        })json");
    writeTextFile(package_root / "deferred_schema_ext.py",
                  "import afternight\n\n"
                  "class DeferredSchemaExtension(afternight.ProcessExtension):\n"
                  "    def get_settings_params(self):\n"
                  "        return [\n"
                  "            {'id': 'tool_path', 'type': 'string', 'default': ''},\n"
                  "        ]\n");

    ExtensionDiscoveryOptions options;
    options.user_roots.push_back(temp_dir.path());
    options.context = defaultContext();
    options.context.environments_root = temp_dir.path() / "ExtensionEnvs";
    options.context.extension_host_executable = host_path;
    options.context.runtime_root = runtime_root;

    const auto initial = an::ui::extensions::load_extension_manifest_file(
        package_root / "extension.json", ExtensionPackageSource::User, options.context);
    ASSERT_TRUE(initial.environment.has_value());

    createVirtualEnvironment(bundled_python, initial.environment->environment_path);
    writeInstallReceipt(initial.environment->environment_path, initial.environment->host_bootstrap_fingerprint,
                        initial.environment->dependency_fingerprint, initial.environment->validation_fingerprint);

    ExtensionManager manager;

    options.context.eager_settings_schema_inspection = false;
    const auto deferred_discovery = manager.discoverExtensions(options);
    ASSERT_EQ(deferred_discovery.size(), 1u);
    ASSERT_TRUE(deferred_discovery.front().settings_schema.has_value());
    const auto& deferred_schema = *deferred_discovery.front().settings_schema;
    EXPECT_EQ(deferred_schema.state, an::ui::extensions::ExtensionSettingsSchemaState::Unavailable);
    EXPECT_FALSE(deferred_schema.from_cache);
    EXPECT_EQ(deferred_schema.schema.size(), 0);
    if (!deferred_schema.cache_path.empty())
    {
        EXPECT_FALSE(std::filesystem::exists(deferred_schema.cache_path));
    }

    options.context.eager_settings_schema_inspection = true;
    const auto eager_discovery = manager.discoverExtensions(options);
    ASSERT_EQ(eager_discovery.size(), 1u);
    ASSERT_TRUE(eager_discovery.front().settings_schema.has_value());
    const auto& eager_schema = *eager_discovery.front().settings_schema;
    EXPECT_EQ(eager_schema.state, an::ui::extensions::ExtensionSettingsSchemaState::Ready);
    EXPECT_FALSE(eager_schema.from_cache);
    ASSERT_FALSE(eager_schema.cache_path.empty());
    ASSERT_TRUE(std::filesystem::exists(eager_schema.cache_path));

    options.context.eager_settings_schema_inspection = false;
    const auto cached_discovery = manager.discoverExtensions(options);
    ASSERT_EQ(cached_discovery.size(), 1u);
    ASSERT_TRUE(cached_discovery.front().settings_schema.has_value());
    const auto& cached_schema = *cached_discovery.front().settings_schema;
    EXPECT_EQ(cached_schema.state, an::ui::extensions::ExtensionSettingsSchemaState::Ready);
    EXPECT_TRUE(cached_schema.from_cache);
    EXPECT_EQ(cached_schema.schema.size(), 1);
}

// When an extension's processes declare different settings, each process's schema is cached on its own.
TEST(ExtensionManagerTest, DiscoverExtensionsCachesPerProcessSettingsSchemasWhenProcessClassesDisagree)
{
    const std::filesystem::path host_path = extensionHostPathForCurrentAppDir();
    const std::filesystem::path runtime_root = runtimeRootForCurrentAppDir();
    const std::filesystem::path bundled_python = findRepositoryPythonExecutable();
    AN_REQUIRE_FULL_EXTENSION_HOST(host_path);
    ASSERT_TRUE(std::filesystem::exists(runtime_root)) << runtime_root;
    ASSERT_FALSE(bundled_python.empty()) << "Repository bundled Python runtime must be available";

    ScopedTempDirectory temp_dir;
    const auto package_root = createExtensionPackage(temp_dir.path(), "mismatched_schema_ext",
                                                     R"json({
          "id": "mismatched_schema_ext",
          "name": "Mismatched Schema Extension",
          "version": "1.0.0",
          "type": "python",
          "entry_point": "mismatched_schema_ext",
          "category": "filters",
          "launch_mode": "single_image",
          "package_format_version": 1,
          "protocol_version": 1,
          "sdk_version": 1,
          "processes": [
            {"id_suffix": "alpha", "class": "AlphaExtension", "display_name": "Alpha"},
            {"id_suffix": "beta", "class": "BetaExtension", "display_name": "Beta"}
          ]
        })json");
    writeTextFile(package_root / "mismatched_schema_ext.py",
                  "import afternight\n\n"
                  "class AlphaExtension(afternight.ProcessExtension):\n"
                  "    def get_settings_params(self):\n"
                  "        return [{'id': 'mode', 'type': 'string', 'default': 'fast'}]\n\n"
                  "class BetaExtension(afternight.ProcessExtension):\n"
                  "    def get_settings_params(self):\n"
                  "        return [{'id': 'strength', 'type': 'float', 'default': 0.5}]\n");

    ExtensionDiscoveryOptions options;
    options.user_roots.push_back(temp_dir.path());
    options.context = defaultContext();
    options.context.environments_root = temp_dir.path() / "ExtensionEnvs";
    options.context.extension_host_executable = host_path;
    options.context.runtime_root = runtime_root;

    const auto initial = an::ui::extensions::load_extension_manifest_file(
        package_root / "extension.json", ExtensionPackageSource::User, options.context);
    ASSERT_TRUE(initial.environment.has_value());

    createVirtualEnvironment(bundled_python, initial.environment->environment_path);
    writeInstallReceipt(initial.environment->environment_path, initial.environment->host_bootstrap_fingerprint,
                        initial.environment->dependency_fingerprint, initial.environment->validation_fingerprint);

    ExtensionManager manager;
    const auto first_discovery = manager.discoverExtensions(options);
    ASSERT_EQ(first_discovery.size(), 1u);
    ASSERT_TRUE(first_discovery.front().settings_schema.has_value());
    const auto& first_schema = *first_discovery.front().settings_schema;
    EXPECT_EQ(first_schema.state, an::ui::extensions::ExtensionSettingsSchemaState::Ready);
    EXPECT_FALSE(first_schema.from_cache);
    EXPECT_NE(first_schema.diagnostic.find("per-process settings schemas"), std::string::npos);
    EXPECT_TRUE(first_schema.schema.isEmpty());
    ASSERT_EQ(first_schema.process_schemas.size(), 2u);
    ASSERT_TRUE(first_schema.process_schemas.contains("AlphaExtension"));
    ASSERT_TRUE(first_schema.process_schemas.contains("BetaExtension"));
    EXPECT_EQ(first_schema.process_schemas.at("AlphaExtension").at(0).toObject().value(QStringLiteral("id")).toString(),
              QStringLiteral("mode"));
    EXPECT_EQ(first_schema.process_schemas.at("BetaExtension").at(0).toObject().value(QStringLiteral("id")).toString(),
              QStringLiteral("strength"));
    ASSERT_FALSE(first_schema.cache_path.empty());
    ASSERT_TRUE(std::filesystem::exists(first_schema.cache_path));

    const QJsonObject cache_object = loadJsonFile(first_schema.cache_path);
    EXPECT_EQ(cache_object.value(QStringLiteral("state")).toString(), QStringLiteral("ready"));
    EXPECT_TRUE(cache_object.value(QStringLiteral("settings_schema")).toArray().isEmpty());
    const QJsonObject process_schemas = cache_object.value(QStringLiteral("process_settings_schemas")).toObject();
    ASSERT_TRUE(process_schemas.value(QStringLiteral("AlphaExtension")).isArray());
    ASSERT_TRUE(process_schemas.value(QStringLiteral("BetaExtension")).isArray());

    const auto second_discovery = manager.discoverExtensions(options);
    ASSERT_EQ(second_discovery.size(), 1u);
    ASSERT_TRUE(second_discovery.front().settings_schema.has_value());
    const auto& second_schema = *second_discovery.front().settings_schema;
    EXPECT_EQ(second_schema.state, an::ui::extensions::ExtensionSettingsSchemaState::Ready);
    EXPECT_TRUE(second_schema.from_cache);
    ASSERT_EQ(second_schema.process_schemas.size(), 2u);
    EXPECT_TRUE(second_schema.process_schemas.contains("AlphaExtension"));
    EXPECT_TRUE(second_schema.process_schemas.contains("BetaExtension"));
}

// A cached settings schema is inspected again once the package's validation fingerprint moves.
TEST(ExtensionManagerTest, DiscoverExtensionsRefreshesSettingsSchemaCacheWhenValidationFingerprintChanges)
{
    const std::filesystem::path host_path = extensionHostPathForCurrentAppDir();
    const std::filesystem::path runtime_root = runtimeRootForCurrentAppDir();
    const std::filesystem::path bundled_python = findRepositoryPythonExecutable();
    AN_REQUIRE_FULL_EXTENSION_HOST(host_path);
    ASSERT_TRUE(std::filesystem::exists(runtime_root)) << runtime_root;
    ASSERT_FALSE(bundled_python.empty()) << "Repository bundled Python runtime must be available";

    ScopedTempDirectory temp_dir;
    const auto package_root = createExtensionPackage(temp_dir.path(), "settings_schema_refresh_ext",
                                                     R"json({
          "id": "settings_schema_refresh_ext",
          "name": "Settings Schema Refresh Extension",
          "version": "1.0.0",
          "type": "python",
          "entry_point": "settings_schema_refresh_ext",
          "process_class": "SettingsSchemaRefreshExtension",
          "category": "filters",
          "launch_mode": "single_image",
          "package_format_version": 1,
          "protocol_version": 1,
          "sdk_version": 1
        })json");
    writeTextFile(package_root / "settings_schema_refresh_ext.py",
                  "import afternight\n\n"
                  "class SettingsSchemaRefreshExtension(afternight.ProcessExtension):\n"
                  "    def get_settings_params(self):\n"
                  "        return [\n"
                  "            {'id': 'tool_path', 'type': 'string', 'default': ''},\n"
                  "        ]\n");

    ExtensionDiscoveryOptions options;
    options.user_roots.push_back(temp_dir.path());
    options.context = defaultContext();
    options.context.environments_root = temp_dir.path() / "ExtensionEnvs";
    options.context.extension_host_executable = host_path;
    options.context.runtime_root = runtime_root;

    const auto initial = an::ui::extensions::load_extension_manifest_file(
        package_root / "extension.json", ExtensionPackageSource::User, options.context);
    ASSERT_TRUE(initial.environment.has_value());

    createVirtualEnvironment(bundled_python, initial.environment->environment_path);
    writeInstallReceipt(initial.environment->environment_path, initial.environment->host_bootstrap_fingerprint,
                        initial.environment->dependency_fingerprint, initial.environment->validation_fingerprint);

    ExtensionManager manager;
    const auto first_discovery = manager.discoverExtensions(options);
    ASSERT_EQ(first_discovery.size(), 1u);
    ASSERT_TRUE(first_discovery.front().settings_schema.has_value());
    const auto& first_schema = *first_discovery.front().settings_schema;
    EXPECT_EQ(first_schema.state, an::ui::extensions::ExtensionSettingsSchemaState::Ready);
    EXPECT_FALSE(first_schema.from_cache);
    ASSERT_EQ(first_schema.schema.size(), 1);
    EXPECT_EQ(first_schema.schema.at(0).toObject().value(QStringLiteral("id")).toString(), QStringLiteral("tool_path"));
    const auto original_cache_path = first_schema.cache_path;
    const auto original_validation_fingerprint = first_schema.validation_fingerprint;
    ASSERT_FALSE(original_cache_path.empty());
    ASSERT_TRUE(std::filesystem::exists(original_cache_path));

    const auto cached_discovery = manager.discoverExtensions(options);
    ASSERT_EQ(cached_discovery.size(), 1u);
    ASSERT_TRUE(cached_discovery.front().settings_schema.has_value());
    EXPECT_TRUE(cached_discovery.front().settings_schema->from_cache);
    EXPECT_EQ(cached_discovery.front().settings_schema->cache_path, original_cache_path);

    writeTextFile(package_root / "settings_schema_refresh_ext.py",
                  "import afternight\n\n"
                  "class SettingsSchemaRefreshExtension(afternight.ProcessExtension):\n"
                  "    def get_settings_params(self):\n"
                  "        return [\n"
                  "            {'id': 'tool_path', 'type': 'string', 'default': ''},\n"
                  "            {'id': 'gpu_enabled', 'type': 'bool', 'default': True},\n"
                  "        ]\n");

    // The package's sources are not what its environment installs: it keeps its place, and is refreshed for them.
    const auto stale = an::ui::extensions::load_extension_manifest_file(package_root / "extension.json",
                                                                        ExtensionPackageSource::User, options.context);
    ASSERT_TRUE(stale.environment.has_value());
    EXPECT_EQ(stale.environment->state, ExtensionEnvironmentState::Stale);
    EXPECT_TRUE(stale.environment->refreshable);
    EXPECT_NE(stale.environment->validation_fingerprint, original_validation_fingerprint);
    EXPECT_EQ(stale.environment->environment_path, first_discovery.front().environment->environment_path);

    // What the refresh records for the package's new revision.
    writeInstallReceipt(stale.environment->environment_path, stale.environment->host_bootstrap_fingerprint,
                        stale.environment->dependency_fingerprint, stale.environment->validation_fingerprint);

    const auto refreshed_discovery = manager.discoverExtensions(options);
    ASSERT_EQ(refreshed_discovery.size(), 1u);
    ASSERT_TRUE(refreshed_discovery.front().settings_schema.has_value());
    const auto& refreshed_schema = *refreshed_discovery.front().settings_schema;
    EXPECT_EQ(refreshed_schema.state, an::ui::extensions::ExtensionSettingsSchemaState::Ready);
    EXPECT_FALSE(refreshed_schema.from_cache);
    EXPECT_NE(refreshed_schema.validation_fingerprint, original_validation_fingerprint);
    EXPECT_NE(refreshed_schema.cache_path, original_cache_path);
    ASSERT_FALSE(refreshed_schema.cache_path.empty());
    ASSERT_TRUE(std::filesystem::exists(refreshed_schema.cache_path));
    ASSERT_EQ(refreshed_schema.schema.size(), 2);
    EXPECT_EQ(refreshed_schema.schema.at(0).toObject().value(QStringLiteral("id")).toString(),
              QStringLiteral("tool_path"));
    EXPECT_EQ(refreshed_schema.schema.at(1).toObject().value(QStringLiteral("id")).toString(),
              QStringLiteral("gpu_enabled"));

    const QJsonObject original_cache_object = loadJsonFile(original_cache_path);
    ASSERT_TRUE(original_cache_object.value(QStringLiteral("settings_schema")).isArray());
    EXPECT_EQ(original_cache_object.value(QStringLiteral("settings_schema")).toArray().size(), 1);

    const QJsonObject refreshed_cache_object = loadJsonFile(refreshed_schema.cache_path);
    ASSERT_TRUE(refreshed_cache_object.value(QStringLiteral("settings_schema")).isArray());
    EXPECT_EQ(refreshed_cache_object.value(QStringLiteral("settings_schema")).toArray().size(), 2);

    const auto refreshed_cached_discovery = manager.discoverExtensions(options);
    ASSERT_EQ(refreshed_cached_discovery.size(), 1u);
    ASSERT_TRUE(refreshed_cached_discovery.front().settings_schema.has_value());
    EXPECT_TRUE(refreshed_cached_discovery.front().settings_schema->from_cache);
    EXPECT_EQ(refreshed_cached_discovery.front().settings_schema->cache_path, refreshed_schema.cache_path);
}

// A workflow extension where workflows are unavailable stays listed, incompatible.
TEST(ExtensionManagerTest, WorkflowLaunchModeRemainsVisibleButIncompatibleWhenUnavailable)
{
    ScopedTempDirectory temp_dir;
    const auto package_root = createExtensionPackage(temp_dir.path(), "workflow_ext",
                                                     R"json({
          "id": "workflow_ext",
          "name": "Workflow Extension",
          "version": "1.0.0",
          "type": "python",
          "entry_point": "workflow_ext.entry",
          "process_class": "WorkflowExtension",
          "category": "multi_image_stacking",
          "launch_mode": "workflow",
          "package_format_version": 1,
          "protocol_version": 1,
          "sdk_version": 1
        })json");

    auto context = defaultContext();
    context.workflow_launch_mode_available = false;
    const auto result = an::ui::extensions::load_extension_manifest_file(package_root / "extension.json",
                                                                         ExtensionPackageSource::User, context);

    ASSERT_TRUE(result.manifest.has_value());
    EXPECT_EQ(result.status, ExtensionLoadStatus::Incompatible);
    EXPECT_NE(result.diagnostic.find("workflow launch mode"), std::string::npos);
}

// A workflow extension where workflows are available is ready.
TEST(ExtensionManagerTest, WorkflowLaunchModeIsReadyWhenWorkflowSupportIsAvailable)
{
    ScopedTempDirectory temp_dir;
    const auto package_root = createExtensionPackage(temp_dir.path(), "workflow_ready_ext",
                                                     R"json({
          "id": "workflow_ready_ext",
          "name": "Workflow Ready Extension",
          "version": "1.0.0",
          "type": "python",
          "entry_point": "workflow_ready_ext.entry",
          "process_class": "WorkflowExtension",
          "category": "multi_image_stacking",
          "launch_mode": "workflow",
          "package_format_version": 1,
          "protocol_version": 1,
          "sdk_version": 1
        })json");

    const auto result = an::ui::extensions::load_extension_manifest_file(
        package_root / "extension.json", ExtensionPackageSource::User, defaultContext());

    ASSERT_TRUE(result.manifest.has_value());
    EXPECT_EQ(result.status, ExtensionLoadStatus::Ready);
    EXPECT_EQ(result.manifest->launch_mode, an::ui::extensions::ExtensionLaunchMode::Workflow);
}

// Two processes registered under one display name are told apart in the menu.
TEST(ExtensionManagerTest, DuplicateDisplayNamesAreDisambiguatedDuringRegistration)
{
    ScopedTempDirectory temp_dir;
    const auto first_package_root = createExtensionPackage(temp_dir.path(), "ext_alpha",
                                                           R"json({
          "id": "ext_alpha",
          "name": "Extension Alpha",
          "version": "1.0.0",
          "type": "python",
          "entry_point": "ext_alpha.entry",
          "process_class": "SharedLabelExtension",
          "category": "filters",
          "launch_mode": "single_image",
          "package_format_version": 1,
          "protocol_version": 1,
          "sdk_version": 1
        })json");

    const auto second_package_root = createExtensionPackage(temp_dir.path(), "ext_beta",
                                                            R"json({
          "id": "ext_beta",
          "name": "Extension Alpha",
          "version": "1.0.0",
          "type": "python",
          "entry_point": "ext_beta.entry",
          "process_class": "SharedLabelExtension",
          "category": "filters",
          "launch_mode": "single_image",
          "package_format_version": 1,
          "protocol_version": 1,
          "sdk_version": 1
        })json");

    ExtensionManager manager;
    const auto discovered =
        std::vector{an::ui::extensions::load_extension_manifest_file(first_package_root / "extension.json",
                                                                     ExtensionPackageSource::User, defaultContext()),
                    an::ui::extensions::load_extension_manifest_file(second_package_root / "extension.json",
                                                                     ExtensionPackageSource::User, defaultContext())};

    // The built-in registry is put back when the case ends, passing or failing: later cases launch through it.
    const an::tests::processes::shared::RegistrySnapshot registrySnapshot;
    ProcessRegistry& registry = ProcessRegistry::instance();
    registry.clear();
    manager.registerDiscoveredProcesses(discovered, registry);

    const auto* alpha = registry.find("ext_alpha");
    const auto* beta = registry.find("ext_beta");
    ASSERT_NE(alpha, nullptr);
    ASSERT_NE(beta, nullptr);
    EXPECT_EQ(alpha->display_name, "Extension Alpha (ext_alpha)");
    EXPECT_EQ(beta->display_name, "Extension Alpha (ext_beta)");
    EXPECT_EQ(alpha->process_name, "Extension Alpha");
    EXPECT_EQ(beta->process_name, "Extension Alpha");

    registry.clear();
}

// An environment's bootstrapping and ready states are read from the markers it leaves on disk.
TEST(ExtensionManagerTest, BootstrappingAndReadyEnvironmentStatesAreDetectedFromFilesystemMarkers)
{
    ScopedTempDirectory temp_dir;
    const auto package_root = createExtensionPackage(temp_dir.path(), "venv_ext",
                                                     R"json({
          "id": "venv_ext",
          "name": "Venv Extension",
          "version": "1.0.0",
          "type": "python",
          "entry_point": "venv_ext.entry",
          "process_class": "VenvExtension",
          "category": "filters",
          "launch_mode": "single_image",
          "package_format_version": 1,
          "protocol_version": 1,
          "sdk_version": 1
        })json");

    ExtensionDiscoveryContext context = defaultContext();
    context.environments_root = temp_dir.path() / "ExtensionEnvs";

    const auto initial_result = an::ui::extensions::load_extension_manifest_file(package_root / "extension.json",
                                                                                 ExtensionPackageSource::User, context);
    ASSERT_TRUE(initial_result.manifest.has_value());
    ASSERT_TRUE(initial_result.environment.has_value());

    const auto environment_path = initial_result.environment->environment_path;
    const LiveOtherProcess owner;
    ASSERT_TRUE(owner.started());
    writeTextFile(environment_path / ".provisioning.lock", owner.pidLine());

    const auto bootstrapping_result = an::ui::extensions::load_extension_manifest_file(
        package_root / "extension.json", ExtensionPackageSource::User, context);
    ASSERT_TRUE(bootstrapping_result.environment.has_value());
    EXPECT_EQ(bootstrapping_result.environment->state, ExtensionEnvironmentState::Bootstrapping);

    std::filesystem::remove(environment_path / ".provisioning.lock");
    writeInstallReceipt(environment_path, bootstrapping_result.environment->host_bootstrap_fingerprint,
                        bootstrapping_result.environment->dependency_fingerprint);

    const auto ready_result = an::ui::extensions::load_extension_manifest_file(package_root / "extension.json",
                                                                               ExtensionPackageSource::User, context);
    ASSERT_TRUE(ready_result.environment.has_value());
    EXPECT_EQ(ready_result.environment->state, ExtensionEnvironmentState::Ready);
}

// Provisioning writes the environment's receipt and moves it from bootstrapping to ready.
TEST(ExtensionManagerTest, ProvisioningLifecycleWritesReceiptAndTransitionsToReady)
{
    ScopedTempDirectory temp_dir;
    const auto package_root = createExtensionPackage(temp_dir.path(), "provision_ext",
                                                     R"json({
          "id": "provision_ext",
          "name": "Provision Extension",
          "version": "1.0.0",
          "type": "python",
          "entry_point": "provision_ext.entry",
          "process_class": "ProvisionExtension",
          "category": "filters",
          "launch_mode": "single_image",
          "package_format_version": 1,
          "protocol_version": 1,
          "sdk_version": 1
        })json");

    ExtensionDiscoveryContext context = defaultContext();
    context.environments_root = temp_dir.path() / "ExtensionEnvs";

    const auto initial = an::ui::extensions::load_extension_manifest_file(package_root / "extension.json",
                                                                          ExtensionPackageSource::User, context);
    ASSERT_TRUE(initial.environment.has_value());
    EXPECT_EQ(initial.environment->state, ExtensionEnvironmentState::Missing);

    ExtensionEnvironmentManager environment_manager;
    std::string error;
    ASSERT_TRUE(environment_manager.beginProvisioning(*initial.environment, &error)) << error;

    const auto bootstrapping = an::ui::extensions::load_extension_manifest_file(package_root / "extension.json",
                                                                                ExtensionPackageSource::User, context);
    ASSERT_TRUE(bootstrapping.environment.has_value());
    EXPECT_EQ(bootstrapping.environment->state, ExtensionEnvironmentState::Bootstrapping);

    std::vector<ExtensionInstalledDistribution> distributions = {
        {"numpy", "2.0.0", "wheelhouse:numpy-2.0.0.whl", "sha256:numpyhash"},
        {"afternight", "1.0.0", "host-runtime", "sha256:runtimehash"}};
    ASSERT_TRUE(environment_manager.completeProvisioning(*bootstrapping.environment, distributions, &error)) << error;

    const auto ready = an::ui::extensions::load_extension_manifest_file(package_root / "extension.json",
                                                                        ExtensionPackageSource::User, context);
    ASSERT_TRUE(ready.environment.has_value());
    EXPECT_EQ(ready.environment->state, ExtensionEnvironmentState::Ready);

    std::ifstream receipt(ready.environment->receipt_path, std::ios::binary);
    ASSERT_TRUE(receipt.is_open());
    std::stringstream receipt_text;
    receipt_text << receipt.rdbuf();
    EXPECT_NE(receipt_text.str().find("\"resolved_distributions\""), std::string::npos);
    EXPECT_NE(receipt_text.str().find("\"numpy\""), std::string::npos);
}

// Another program - an indexer, a virus scanner - may hold the lock open for a moment when a provisioning completes:
// the lock cannot be removed then, and the environment, complete and recorded, is not failed for it. The lock is
// released in place, which no reader takes for a lock held, so the environment reads back ready.
TEST(ExtensionManagerTest, ACompletedProvisioningWhoseLockIsHeldOpenReleasesItInPlace)
{
    ScopedTempDirectory temp_dir;
    const auto package_root = createExtensionPackage(temp_dir.path(), "held_lock_ext",
                                                     R"json({
          "id": "held_lock_ext",
          "name": "Held Lock Extension",
          "version": "1.0.0",
          "type": "python",
          "entry_point": "held_lock_ext.entry",
          "process_class": "HeldLockExtension",
          "category": "filters",
          "launch_mode": "single_image",
          "package_format_version": 1,
          "protocol_version": 1,
          "sdk_version": 1
        })json");

    ExtensionDiscoveryContext context = defaultContext();
    context.environments_root = temp_dir.path() / "ExtensionEnvs";

    const auto initial = an::ui::extensions::load_extension_manifest_file(package_root / "extension.json",
                                                                          ExtensionPackageSource::User, context);
    ASSERT_TRUE(initial.environment.has_value());
    ExtensionEnvironmentManager environment_manager;
    std::string error;
    ASSERT_TRUE(environment_manager.beginProvisioning(*initial.environment, &error)) << error;
    const auto bootstrapping = an::ui::extensions::load_extension_manifest_file(package_root / "extension.json",
                                                                                ExtensionPackageSource::User, context);
    ASSERT_TRUE(bootstrapping.environment.has_value());
    ASSERT_EQ(bootstrapping.environment->state, ExtensionEnvironmentState::Bootstrapping);

    // On Windows a file another program has open cannot be removed; elsewhere it can, and the lock simply goes.
    QFile held(QString::fromStdWString((bootstrapping.environment->environment_path / ".provisioning.lock").wstring()));
    ASSERT_TRUE(held.open(QIODevice::ReadOnly));
    EXPECT_TRUE(environment_manager.completeProvisioning(*bootstrapping.environment, {}, &error))
        << "a complete environment was failed for its lock: " << error;
    held.close();

    const auto ready = an::ui::extensions::load_extension_manifest_file(package_root / "extension.json",
                                                                        ExtensionPackageSource::User, context);
    ASSERT_TRUE(ready.environment.has_value());
    EXPECT_EQ(ready.environment->state, ExtensionEnvironmentState::Ready);
}

// A provisioning that fails while its lock is held open records its failure all the same.
TEST(ExtensionManagerTest, AFailedProvisioningWhoseLockIsHeldOpenReleasesItInPlace)
{
    ScopedTempDirectory temp_dir;
    const auto package_root = createExtensionPackage(temp_dir.path(), "held_lock_failed_ext",
                                                     R"json({
          "id": "held_lock_failed_ext",
          "name": "Held Lock Failed Extension",
          "version": "1.0.0",
          "type": "python",
          "entry_point": "held_lock_failed_ext.entry",
          "process_class": "HeldLockFailedExtension",
          "category": "filters",
          "launch_mode": "single_image",
          "package_format_version": 1,
          "protocol_version": 1,
          "sdk_version": 1
        })json");

    ExtensionDiscoveryContext context = defaultContext();
    context.environments_root = temp_dir.path() / "ExtensionEnvs";

    const auto initial = an::ui::extensions::load_extension_manifest_file(package_root / "extension.json",
                                                                          ExtensionPackageSource::User, context);
    ASSERT_TRUE(initial.environment.has_value());
    ExtensionEnvironmentManager environment_manager;
    std::string error;
    ASSERT_TRUE(environment_manager.beginProvisioning(*initial.environment, &error)) << error;

    QFile held(QString::fromStdWString((initial.environment->environment_path / ".provisioning.lock").wstring()));
    ASSERT_TRUE(held.open(QIODevice::ReadOnly));
    EXPECT_TRUE(environment_manager.failProvisioning(*initial.environment, "The wheelhouse was incomplete", &error))
        << "the failure could not be recorded for its lock: " << error;
    held.close();

    const auto failed = an::ui::extensions::load_extension_manifest_file(package_root / "extension.json",
                                                                         ExtensionPackageSource::User, context);
    ASSERT_TRUE(failed.environment.has_value());
    EXPECT_EQ(failed.environment->state, ExtensionEnvironmentState::Failed);
    EXPECT_NE(failed.environment->diagnostic.find("wheelhouse was incomplete"), std::string::npos);
}

// A failed provisioning's diagnostic is stored and read back with the environment.
TEST(ExtensionManagerTest, FailedProvisioningSurfacesStoredDiagnostic)
{
    ScopedTempDirectory temp_dir;
    const auto package_root = createExtensionPackage(temp_dir.path(), "failed_provision_ext",
                                                     R"json({
          "id": "failed_provision_ext",
          "name": "Failed Provision Extension",
          "version": "1.0.0",
          "type": "python",
          "entry_point": "failed_provision_ext.entry",
          "process_class": "FailedProvisionExtension",
          "category": "filters",
          "launch_mode": "single_image",
          "package_format_version": 1,
          "protocol_version": 1,
          "sdk_version": 1
        })json");

    ExtensionDiscoveryContext context = defaultContext();
    context.environments_root = temp_dir.path() / "ExtensionEnvs";

    const auto initial = an::ui::extensions::load_extension_manifest_file(package_root / "extension.json",
                                                                          ExtensionPackageSource::User, context);
    ASSERT_TRUE(initial.environment.has_value());

    ExtensionEnvironmentManager environment_manager;
    std::string error;
    ASSERT_TRUE(environment_manager.beginProvisioning(*initial.environment, &error)) << error;
    ASSERT_TRUE(environment_manager.failProvisioning(*initial.environment,
                                                     "Missing offline wheelhouse artifact for numpy==2.0.0", &error))
        << error;

    const auto failed = an::ui::extensions::load_extension_manifest_file(package_root / "extension.json",
                                                                         ExtensionPackageSource::User, context);
    ASSERT_TRUE(failed.environment.has_value());
    EXPECT_EQ(failed.environment->state, ExtensionEnvironmentState::Failed);
    EXPECT_NE(failed.environment->diagnostic.find("wheelhouse artifact"), std::string::npos);
}

// A dependency whose bytecode cannot be compiled does not fail the provisioning that installed it.
TEST(ExtensionManagerTest, DependencyBytecodePrecompileFailureDoesNotFailProvisioning)
{
    const std::filesystem::path runtime_root = runtimeRootForCurrentAppDir();
    const std::filesystem::path bundled_python = findRepositoryPythonExecutable();
    ASSERT_TRUE(std::filesystem::exists(runtime_root)) << runtime_root;
    ASSERT_FALSE(bundled_python.empty()) << "Repository bundled Python runtime must be available";

    ScopedTempDirectory temp_dir;
    const auto package_root = createExtensionPackage(temp_dir.path(), "dependency_precompile_warning_ext",
                                                     R"json({
          "id": "dependency_precompile_warning_ext",
          "name": "Dependency Precompile Warning Extension",
          "version": "1.0.0",
          "type": "python",
          "entry_point": "dependency_precompile_warning_ext",
          "process_class": "DependencyPrecompileWarningExtension",
          "category": "filters",
          "launch_mode": "single_image",
          "package_format_version": 1,
          "protocol_version": 1,
          "sdk_version": 1,
          "dependencies": {
            "requirements_file": "requirements.lock",
            "pip": {
              "require_hashes": true,
              "no_deps": true,
              "find_links": ["wheelhouse"]
            }
          }
        })json");
    writeTextFile(package_root / "dependency_precompile_warning_ext.py",
                  "class DependencyPrecompileWarningExtension: pass\n");

    const auto wheel_path =
        createPurePythonWheel(package_root, "broken-bytecode-dep", "1.0.0", {}, "def broken(:\n    pass\n");
    ASSERT_FALSE(wheel_path.empty());
    writeTextFile(package_root / "requirements.lock",
                  "broken-bytecode-dep==1.0.0 --hash=sha256:" + sha256FileHex(wheel_path) + "\n");

    ExtensionDiscoveryContext context = defaultContext();
    context.environments_root = temp_dir.path() / "ExtensionEnvs";
    context.runtime_root = runtime_root;
    context.bundled_python_executable = bundled_python;

    const auto load_result = an::ui::extensions::load_extension_manifest_file(
        package_root / "extension.json", ExtensionPackageSource::LocalPackage, context);
    ASSERT_TRUE(load_result.manifest.has_value()) << load_result.diagnostic;

    ExtensionEnvironmentManager environment_manager;
    ExtensionEnvironmentSnapshot snapshot;
    std::vector<ExtensionInstalledDistribution> distributions;
    std::string error;
    ASSERT_TRUE(environment_manager.provisionEnvironmentForManifest(
        *load_result.manifest, ExtensionPackageSource::LocalPackage, context, &snapshot, &distributions, &error))
        << error;

    EXPECT_EQ(snapshot.state, ExtensionEnvironmentState::Ready);
    EXPECT_TRUE(std::filesystem::exists(snapshot.environment_path / ".environment_ready"));
    EXPECT_TRUE(std::filesystem::exists(snapshot.receipt_path));
    EXPECT_FALSE(std::filesystem::exists(snapshot.environment_path / "provision_failure.txt"));
}

// Provisioning installs an extension's dependencies without compiling them and then compiles their bytecode as a step
// of its own, reporting it as determinate progress that never moves backwards, so the dependency is installed with its
// bytecode beside it when provisioning reports the environment ready.
TEST(ExtensionManagerTest, DependencyProvisioningUsesProgressAwareBytecodeCompileWork)
{
    const std::filesystem::path runtime_root = runtimeRootForCurrentAppDir();
    const std::filesystem::path bundled_python = findRepositoryPythonExecutable();
    ASSERT_TRUE(std::filesystem::exists(runtime_root)) << runtime_root;
    ASSERT_FALSE(bundled_python.empty()) << "Repository bundled Python runtime must be available";

    ScopedTempDirectory temp_dir;
    const auto package_root = createExtensionPackage(temp_dir.path(), "dependency_precompile_progress_ext",
                                                     R"json({
          "id": "dependency_precompile_progress_ext",
          "name": "Dependency Precompile Progress Extension",
          "version": "1.0.0",
          "type": "python",
          "entry_point": "dependency_precompile_progress_ext",
          "process_class": "DependencyPrecompileProgressExtension",
          "category": "filters",
          "launch_mode": "single_image",
          "package_format_version": 1,
          "protocol_version": 1,
          "sdk_version": 1,
          "dependencies": {
            "requirements_file": "requirements.lock",
            "pip": {
              "require_hashes": true,
              "no_deps": true,
              "find_links": ["wheelhouse"]
            }
          }
        })json");
    writeTextFile(package_root / "dependency_precompile_progress_ext.py",
                  "class DependencyPrecompileProgressExtension: pass\n");

    const auto wheel_path =
        createPurePythonWheel(package_root, "compiled-progress-dep", "1.0.0", {}, "def answer():\n    return 42\n");
    ASSERT_FALSE(wheel_path.empty());
    writeTextFile(package_root / "requirements.lock",
                  "compiled-progress-dep==1.0.0 --hash=sha256:" + sha256FileHex(wheel_path) + "\n");

    ExtensionDiscoveryContext context = defaultContext();
    context.environments_root = temp_dir.path() / "ExtensionEnvs";
    context.runtime_root = runtime_root;
    context.bundled_python_executable = bundled_python;

    const auto load_result = an::ui::extensions::load_extension_manifest_file(
        package_root / "extension.json", ExtensionPackageSource::LocalPackage, context);
    ASSERT_TRUE(load_result.manifest.has_value()) << load_result.diagnostic;

    std::vector<ExtensionOperationProgress> dependency_progress;
    ExtensionEnvironmentManager environment_manager;
    ExtensionEnvironmentSnapshot snapshot;
    std::vector<ExtensionInstalledDistribution> distributions;
    std::string error;
    ASSERT_TRUE(environment_manager.provisionEnvironmentForManifest(
        *load_result.manifest, ExtensionPackageSource::LocalPackage, context, &snapshot, &distributions, &error,
        [&dependency_progress](const ExtensionOperationProgress& progress) {
            if (progress.phase_id == "precompile_dependencies")
            {
                dependency_progress.push_back(progress);
            }
        }))
        << error;
    EXPECT_EQ(snapshot.state, ExtensionEnvironmentState::Ready);

    ASSERT_FALSE(dependency_progress.empty()) << "dependency bytecode is compiled as a step of its own";
    int last_current = -1;
    for (const ExtensionOperationProgress& progress : dependency_progress)
    {
        EXPECT_FALSE(progress.indeterminate) << progress.message;
        EXPECT_GT(progress.total, 0) << progress.message;
        EXPECT_GE(progress.current, last_current) << progress.message;
        last_current = (std::max)(last_current, progress.current);
    }

    // The dependency's package was compiled into its own cache directory.
    bool compiled = false;
    std::error_code ec;
    for (std::filesystem::recursive_directory_iterator it(snapshot.environment_path, ec), end; it != end && !ec;
         it.increment(ec))
    {
        if (it->is_directory() && it->path().filename() == "compiled_progress_dep" &&
            treeContainsBytecodeForModule(it->path() / "__pycache__", "__init__"))
        {
            compiled = true;
            break;
        }
    }
    EXPECT_TRUE(compiled) << "the installed dependency carries its bytecode";
}

// A directory under an environment's site-packages that cannot be listed costs only its own modules' bytecode: the
// dependency installed beside it is still compiled when provisioning reports the environment ready.
TEST(ExtensionManagerTest, DependencyBytecodePrecompilationSkipsOnlyADirectoryItCannotList)
{
#ifdef _WIN32
    GTEST_SKIP() << "Windows has no permission std::filesystem can take a directory's listing away with; the case runs "
                    "where POSIX permissions do";
#else
    const std::filesystem::path runtime_root = runtimeRootForCurrentAppDir();
    const std::filesystem::path bundled_python = findRepositoryPythonExecutable();
    ASSERT_TRUE(std::filesystem::exists(runtime_root)) << runtime_root;
    ASSERT_FALSE(bundled_python.empty()) << "Repository bundled Python runtime must be available";

    ScopedTempDirectory temp_dir;
    // Declared after the scratch directory, so the unlisted directory is listable again before the directory goes.
    std::filesystem::path unlisted_directory;
    struct RestoreListing
    {
        const std::filesystem::path& directory;
        void operator()() const
        {
            std::error_code ec;
            if (!directory.empty())
            {
                std::filesystem::permissions(directory, std::filesystem::perms::owner_all,
                                             std::filesystem::perm_options::replace, ec);
            }
        }
        ~RestoreListing()
        {
            (*this)();
        }
    } restore_listing{unlisted_directory};

    const auto package_root = createExtensionPackage(temp_dir.path(), "dependency_precompile_skip_ext",
                                                     R"json({
          "id": "dependency_precompile_skip_ext",
          "name": "Dependency Precompile Skip Extension",
          "version": "1.0.0",
          "type": "python",
          "entry_point": "dependency_precompile_skip_ext",
          "process_class": "DependencyPrecompileSkipExtension",
          "category": "filters",
          "launch_mode": "single_image",
          "package_format_version": 1,
          "protocol_version": 1,
          "sdk_version": 1,
          "dependencies": {
            "requirements_file": "requirements.lock",
            "pip": {
              "require_hashes": true,
              "no_deps": true,
              "find_links": ["wheelhouse"]
            }
          }
        })json");
    writeTextFile(package_root / "dependency_precompile_skip_ext.py",
                  "class DependencyPrecompileSkipExtension: pass\n");

    const auto wheel_path =
        createPurePythonWheel(package_root, "listed-beside-dep", "1.0.0", {}, "def answer():\n    return 42\n");
    ASSERT_FALSE(wheel_path.empty());
    writeTextFile(package_root / "requirements.lock",
                  "listed-beside-dep==1.0.0 --hash=sha256:" + sha256FileHex(wheel_path) + "\n");

    ExtensionDiscoveryContext context = defaultContext();
    context.environments_root = temp_dir.path() / "ExtensionEnvs";
    context.runtime_root = runtime_root;
    context.bundled_python_executable = bundled_python;

    const auto load_result = an::ui::extensions::load_extension_manifest_file(
        package_root / "extension.json", ExtensionPackageSource::LocalPackage, context);
    ASSERT_TRUE(load_result.manifest.has_value()) << load_result.diagnostic;

    // When the precompilation begins, the dependencies are installed: a package of the environment's site-packages is
    // made unlistable then, and listable again once the precompilation has ended.
    bool unlistable = false;
    const auto make_a_package_unlistable = [&]() {
        std::error_code ec;
        for (std::filesystem::recursive_directory_iterator it(context.environments_root, ec), end; it != end && !ec;
             it.increment(ec))
        {
            std::error_code type_ec;
            if (it->is_directory(type_ec) && it->path().filename() == "site-packages")
            {
                unlisted_directory = it->path() / "unlisted_dependency";
                break;
            }
        }
        if (unlisted_directory.empty())
        {
            return;
        }
        writeTextFile(unlisted_directory / "__init__.py", "VALUE = 1\n");
        std::filesystem::permissions(unlisted_directory, std::filesystem::perms::none,
                                     std::filesystem::perm_options::replace, ec);
        std::error_code list_ec;
        const std::filesystem::directory_iterator probe(unlisted_directory, list_ec);
        unlistable = !ec && static_cast<bool>(list_ec);
    };

    ExtensionEnvironmentManager environment_manager;
    ExtensionEnvironmentSnapshot snapshot;
    std::vector<ExtensionInstalledDistribution> distributions;
    std::string error;
    ASSERT_TRUE(environment_manager.provisionEnvironmentForManifest(
        *load_result.manifest, ExtensionPackageSource::LocalPackage, context, &snapshot, &distributions, &error,
        [&](const ExtensionOperationProgress& progress) {
            if (progress.phase_id != "precompile_dependencies")
            {
                return;
            }
            if (unlisted_directory.empty())
            {
                make_a_package_unlistable();
            }
            else if (progress.message == "Precompiled dependency bytecode")
            {
                restore_listing();
            }
        }))
        << error;
    restore_listing();
    ASSERT_FALSE(unlisted_directory.empty()) << "the environment's site-packages was not found";
    if (!unlistable)
    {
        GTEST_SKIP() << "this account lists a directory whose permissions deny it, as a privileged one does";
    }
    EXPECT_EQ(snapshot.state, ExtensionEnvironmentState::Ready);

    // The dependency beside the unlisted package was compiled into its own cache directory.
    bool compiled = false;
    std::error_code ec;
    for (std::filesystem::recursive_directory_iterator it(snapshot.environment_path, ec), end; it != end && !ec;
         it.increment(ec))
    {
        if (it->is_directory() && it->path().filename() == "listed_beside_dep" &&
            treeContainsBytecodeForModule(it->path() / "__pycache__", "__init__"))
        {
            compiled = true;
            break;
        }
    }
    EXPECT_TRUE(compiled) << "a directory the precompilation could not list cost the dependency beside it its bytecode";
    EXPECT_FALSE(treeContainsBytecodeForModule(unlisted_directory / "__pycache__", "__init__"));
#endif
}

// A bundled extension with no environment is queued for automatic bootstrap.
TEST(ExtensionManagerTest, BundledMissingEnvironmentIsQueuedForAutomaticBootstrap)
{
    ScopedTempDirectory temp_dir;
    const auto package_root = createExtensionPackage(temp_dir.path(), "bundled_bootstrap_ext",
                                                     R"json({
          "id": "bundled_bootstrap_ext",
          "name": "Bundled Bootstrap Extension",
          "version": "1.0.0",
          "type": "python",
          "entry_point": "bundled_bootstrap_ext.entry",
          "process_class": "BundledBootstrapExtension",
          "category": "filters",
          "launch_mode": "single_image",
          "package_format_version": 1,
          "protocol_version": 1,
          "sdk_version": 1
        })json");

    ExtensionDiscoveryContext context = defaultContext();
    context.environments_root = temp_dir.path() / "ExtensionEnvs";

    std::vector discovered = {an::ui::extensions::load_extension_manifest_file(
        package_root / "extension.json", ExtensionPackageSource::Bundled, context)};
    ASSERT_TRUE(discovered.front().environment.has_value());
    EXPECT_EQ(discovered.front().environment->state, ExtensionEnvironmentState::Missing);

    ExtensionEnvironmentManager environment_manager;
    const int scheduled = environment_manager.queueBundledBootstrap(discovered);
    EXPECT_EQ(scheduled, 1);
    ASSERT_TRUE(discovered.front().environment.has_value());
    EXPECT_EQ(discovered.front().environment->state, ExtensionEnvironmentState::Bootstrapping);

    const auto rediscovered = an::ui::extensions::load_extension_manifest_file(
        package_root / "extension.json", ExtensionPackageSource::Bundled, context);
    ASSERT_TRUE(rediscovered.environment.has_value());
    EXPECT_EQ(rediscovered.environment->state, ExtensionEnvironmentState::Bootstrapping);
}

// A bundled extension whose environment failed is reset and queued for bootstrap again.
TEST(ExtensionManagerTest, BundledFailedEnvironmentIsResetAndQueuedForBootstrap)
{
    ScopedTempDirectory temp_dir;
    const auto package_root = createExtensionPackage(temp_dir.path(), "bundled_repair_ext",
                                                     R"json({
          "id": "bundled_repair_ext",
          "name": "Bundled Repair Extension",
          "version": "1.0.0",
          "type": "python",
          "entry_point": "bundled_repair_ext.entry",
          "process_class": "BundledRepairExtension",
          "category": "filters",
          "launch_mode": "single_image",
          "package_format_version": 1,
          "protocol_version": 1,
          "sdk_version": 1
        })json");

    ExtensionDiscoveryContext context = defaultContext();
    context.environments_root = temp_dir.path() / "ExtensionEnvs";

    const auto initial = an::ui::extensions::load_extension_manifest_file(package_root / "extension.json",
                                                                          ExtensionPackageSource::Bundled, context);
    ASSERT_TRUE(initial.environment.has_value());

    ExtensionEnvironmentManager environment_manager;
    std::string error;
    ASSERT_TRUE(environment_manager.beginProvisioning(*initial.environment, &error)) << error;
    ASSERT_TRUE(
        environment_manager.failProvisioning(*initial.environment, "Bootstrap crashed during wheel install", &error))
        << error;

    std::vector discovered = {an::ui::extensions::load_extension_manifest_file(
        package_root / "extension.json", ExtensionPackageSource::Bundled, context)};
    ASSERT_TRUE(discovered.front().environment.has_value());
    EXPECT_EQ(discovered.front().environment->state, ExtensionEnvironmentState::Failed);

    const int scheduled = environment_manager.queueBundledBootstrap(discovered);
    EXPECT_EQ(scheduled, 1);
    EXPECT_EQ(discovered.front().environment->state, ExtensionEnvironmentState::Bootstrapping);

    const auto rediscovered = an::ui::extensions::load_extension_manifest_file(
        package_root / "extension.json", ExtensionPackageSource::Bundled, context);
    ASSERT_TRUE(rediscovered.environment.has_value());
    EXPECT_EQ(rediscovered.environment->state, ExtensionEnvironmentState::Bootstrapping);
}

// The queued bootstrap completes extensions that need no dependencies.
TEST(ExtensionManagerTest, ProcessingQueuedBundledBootstrapCompletesDependencyFreeExtensions)
{
    ScopedTempDirectory temp_dir;
    const auto package_root = createExtensionPackage(temp_dir.path(), "bundled_ready_ext",
                                                     R"json({
          "id": "bundled_ready_ext",
          "name": "Bundled Ready Extension",
          "version": "1.0.0",
          "type": "python",
          "entry_point": "bundled_ready_ext.entry",
          "process_class": "BundledReadyExtension",
          "category": "filters",
          "launch_mode": "single_image",
          "package_format_version": 1,
          "protocol_version": 1,
          "sdk_version": 1
        })json");

    ExtensionDiscoveryContext context = defaultContext();
    context.environments_root = temp_dir.path() / "ExtensionEnvs";

    std::vector discovered = {an::ui::extensions::load_extension_manifest_file(
        package_root / "extension.json", ExtensionPackageSource::Bundled, context)};

    ExtensionEnvironmentManager environment_manager;
    ASSERT_EQ(environment_manager.queueBundledBootstrap(discovered), 1);

    std::string error;
    EXPECT_EQ(environment_manager.processQueuedBundledBootstrap(discovered, &error), 1) << error;
    ASSERT_TRUE(discovered.front().environment.has_value());
    EXPECT_EQ(discovered.front().environment->state, ExtensionEnvironmentState::Ready);
    EXPECT_TRUE(std::filesystem::exists(discovered.front().environment->environment_path / "pyvenv.cfg"));

    const auto rediscovered = an::ui::extensions::load_extension_manifest_file(
        package_root / "extension.json", ExtensionPackageSource::Bundled, context);
    ASSERT_TRUE(rediscovered.environment.has_value());
    EXPECT_EQ(rediscovered.environment->state, ExtensionEnvironmentState::Ready);
}

// The queued bootstrap builds environments over the bundled runtime's interpreter.
TEST(ExtensionManagerTest, ProcessingQueuedBundledBootstrapUsesBundledRuntimeAsBaseInterpreter)
{
    const std::filesystem::path bundled_python = findRepositoryPythonExecutable();
    ASSERT_FALSE(bundled_python.empty()) << "Repository bundled Python runtime must be available";

    ScopedTempDirectory temp_dir;
    const auto package_root = createExtensionPackage(temp_dir.path(), "bundled_runtime_only_ext",
                                                     R"json({
          "id": "bundled_runtime_only_ext",
          "name": "Bundled Runtime Only Extension",
          "version": "1.0.0",
          "type": "python",
          "entry_point": "bundled_runtime_only_ext.entry",
          "process_class": "BundledRuntimeOnlyExtension",
          "category": "filters",
          "launch_mode": "single_image",
          "package_format_version": 1,
          "protocol_version": 1,
          "sdk_version": 1
        })json");
    (void)package_root;

    ExtensionDiscoveryContext context = defaultContext();
    context.environments_root = temp_dir.path() / "ExtensionEnvs";

    std::vector discovered = {an::ui::extensions::load_extension_manifest_file(
        package_root / "extension.json", ExtensionPackageSource::Bundled, context)};

    ExtensionEnvironmentManager environment_manager;
    ASSERT_EQ(environment_manager.queueBundledBootstrap(discovered), 1);

    std::string error;
    ASSERT_EQ(environment_manager.processQueuedBundledBootstrap(discovered, &error), 1) << error;
    ASSERT_TRUE(discovered.front().environment.has_value());
    ASSERT_EQ(discovered.front().environment->state, ExtensionEnvironmentState::Ready);

#ifdef _WIN32
    const auto env_python = discovered.front().environment->environment_path / "Scripts" / "python.exe";
#else
    const auto env_python = discovered.front().environment->environment_path / "bin" / "python";
#endif
    ASSERT_TRUE(std::filesystem::exists(env_python)) << env_python;

    QProcess probe;
    probe.setProgram(QString::fromStdString(env_python.string()));
    probe.setArguments(
        {QStringLiteral("-c"), QStringLiteral("import pathlib, sys; print(pathlib.Path(sys.executable).resolve()); "
                                              "print(pathlib.Path(getattr(sys, '_base_executable')).resolve())")});
    probe.start();
    ASSERT_TRUE(probe.waitForStarted());
    ASSERT_TRUE(probe.waitForFinished());

    const std::string stdout_text = probe.readAllStandardOutput().toStdString();
    const std::string stderr_text = probe.readAllStandardError().toStdString();
    ASSERT_EQ(probe.exitStatus(), QProcess::NormalExit) << stderr_text;
    ASSERT_EQ(probe.exitCode(), 0) << stderr_text;

    std::vector<std::string> lines;
    std::stringstream stream(stdout_text);
    std::string line;
    while (std::getline(stream, line))
    {
        if (!line.empty() && line.back() == '\r')
        {
            line.pop_back();
        }
        if (!line.empty())
        {
            lines.push_back(line);
        }
    }
    ASSERT_EQ(lines.size(), 2u) << stdout_text;

    std::error_code ec;
    const std::string env_python_canonical = std::filesystem::weakly_canonical(env_python, ec).string();
    EXPECT_EQ(env_python_canonical, lines[0]);
    ec.clear();
    const std::string bundled_python_canonical = std::filesystem::weakly_canonical(bundled_python, ec).string();
    EXPECT_TRUE(lines[1] == bundled_python_canonical || lines[1] == env_python_canonical)
        << "Expected sys._base_executable to resolve to either the source bundled interpreter or the copied "
           "environment interpreter, got "
        << lines[1];

    std::ifstream pyvenv_cfg(discovered.front().environment->environment_path / "pyvenv.cfg", std::ios::binary);
    ASSERT_TRUE(pyvenv_cfg.is_open());
    std::stringstream pyvenv_text;
    pyvenv_text << pyvenv_cfg.rdbuf();
    ec.clear();
    EXPECT_NE(pyvenv_text.str().find(std::filesystem::weakly_canonical(bundled_python.parent_path(), ec).string()),
              std::string::npos);
}

// The queued bootstrap stages PySide6 so the host can inspect an extension that builds its own interface.
TEST(ExtensionManagerTest, ProcessingQueuedBundledBootstrapStagesPySide6ForHostInspect)
{
#if defined(_MSC_VER) && !defined(NDEBUG)
    GTEST_SKIP() << "PySide6 wheels target the release Qt runtime and cannot attach "
                    "to an MSVC debug QApplication";
#endif

    const std::filesystem::path host_path = extensionHostPathForCurrentAppDir();
    const std::filesystem::path runtime_root = runtimeRootForCurrentAppDir();
    const std::filesystem::path bundled_python = findRepositoryPythonExecutable();
    AN_REQUIRE_FULL_EXTENSION_HOST(host_path);
    ASSERT_TRUE(std::filesystem::exists(runtime_root)) << runtime_root;
    ASSERT_FALSE(bundled_python.empty()) << "Repository bundled Python runtime must be available";

    ScopedTempDirectory temp_dir;
    const auto package_root = createExtensionPackage(temp_dir.path(), "inspect_pyside6_ext",
                                                     R"json({
          "id": "inspect_pyside6_ext",
          "name": "Inspect PySide6 Extension",
          "version": "1.0.0",
          "type": "python",
          "entry_point": "inspect_pyside6_ext",
          "process_class": "InspectPySide6Extension",
          "category": "filters",
          "launch_mode": "single_image",
          "package_format_version": 1,
          "protocol_version": 1,
          "sdk_version": 1
        })json");
    writeTextFile(package_root / "inspect_pyside6_ext.py",
                  "import afternight\n\n"
                  "class InspectPySide6Extension(afternight.ProcessExtension):\n"
                  "    def get_settings_params(self):\n"
                  "        return []\n");

    ExtensionDiscoveryContext context = defaultContext();
    context.environments_root = temp_dir.path() / "ExtensionEnvs";

    std::vector discovered = {an::ui::extensions::load_extension_manifest_file(
        package_root / "extension.json", ExtensionPackageSource::Bundled, context)};

    ExtensionEnvironmentManager environment_manager;
    ASSERT_EQ(environment_manager.queueBundledBootstrap(discovered), 1);

    std::string error;
    ASSERT_EQ(environment_manager.processQueuedBundledBootstrap(discovered, &error), 1) << error;
    ASSERT_TRUE(discovered.front().environment.has_value());
    ASSERT_EQ(discovered.front().environment->state, ExtensionEnvironmentState::Ready);

    QProcess inspect;
    inspect.setProgram(QString::fromStdString(host_path.string()));
    inspect.setProcessChannelMode(QProcess::MergedChannels);
    inspect.setArguments({QStringLiteral("--inspect"), QStringLiteral("--inspect-settings"),
                          QStringLiteral("--sdk-backend"), QStringLiteral("runtime"),
                          QStringLiteral("--extension-package-root"), QString::fromStdString(package_root.string()),
                          QStringLiteral("--entry-point"), QStringLiteral("inspect_pyside6_ext"),
                          QStringLiteral("--process-class"), QStringLiteral("InspectPySide6Extension"),
                          QStringLiteral("--environment-root"),
                          QString::fromStdString(discovered.front().environment->environment_path.string()),
                          QStringLiteral("--runtime-root"), QString::fromStdString(runtime_root.string())});
    inspect.start();
    ASSERT_TRUE(inspect.waitForStarted());
    ASSERT_TRUE(inspect.waitForFinished(an::tests::processes::framework::hostProcessWaitMs()));

    const std::string output = inspect.readAllStandardOutput().toStdString();
    EXPECT_EQ(inspect.exitStatus(), QProcess::NormalExit) << output;
    EXPECT_EQ(inspect.exitCode(), 0) << output;

    QJsonParseError parse_error;
    const QJsonDocument document = QJsonDocument::fromJson(QByteArray::fromStdString(output).trimmed(), &parse_error);
    ASSERT_EQ(parse_error.error, QJsonParseError::NoError) << output;
    ASSERT_TRUE(document.isObject()) << output;
    ASSERT_TRUE(document.object().value(QStringLiteral("ui_runtime")).isObject()) << output;

    const QJsonObject ui_runtime = document.object().value(QStringLiteral("ui_runtime")).toObject();
    EXPECT_TRUE(ui_runtime.value(QStringLiteral("has_pyside6")).toBool()) << output;
    EXPECT_TRUE(ui_runtime.value(QStringLiteral("pyside6_has_qapplication")).toBool()) << output;
    EXPECT_TRUE(ui_runtime.value(QStringLiteral("pyside6_has_afternight_style_key")).toBool()) << output;
    EXPECT_TRUE(ui_runtime.value(QStringLiteral("pyside6_can_create_afternight_style")).toBool()) << output;
    EXPECT_FALSE(ui_runtime.value(QStringLiteral("pyside6_style_class")).toString().isEmpty()) << output;
}

// The queued bootstrap uses the interpreter deployed in PythonLib.
TEST(ExtensionManagerTest, ProcessingQueuedBundledBootstrapUsesDeployedPythonLibInterpreter)
{
    const std::filesystem::path runtime_root = runtimeRootForCurrentAppDir();
    ASSERT_FALSE(runtime_root.empty()) << "Deployed PythonLib runtime root must be available";
#ifdef _WIN32
    ASSERT_TRUE(std::filesystem::exists(runtime_root / "Lib" / "site-packages")) << runtime_root;
    const std::filesystem::path deployed_python = an::ui::extensions::bundled_python_executable_for_app_dir(
        std::filesystem::path(QCoreApplication::applicationDirPath().toStdString()));
#else
    ASSERT_TRUE(std::filesystem::exists(runtime_root / "lib" / "python3.14" / "encodings")) << runtime_root;
    const std::filesystem::path deployed_python = runtime_root / "bin" / "python3.14";
#endif
    ASSERT_TRUE(std::filesystem::exists(deployed_python))
        << "Deployed PythonLib interpreter must be available at " << deployed_python;

    ScopedTempDirectory temp_dir;
    const auto package_root = createExtensionPackage(temp_dir.path(), "bundled_deployed_root_ext",
                                                     R"json({
          "id": "bundled_deployed_root_ext",
          "name": "Bundled Deployed Root Extension",
          "version": "1.0.0",
          "type": "python",
          "entry_point": "bundled_deployed_root_ext.entry",
          "process_class": "BundledDeployedRootExtension",
          "category": "filters",
          "launch_mode": "single_image",
          "package_format_version": 1,
          "protocol_version": 1,
          "sdk_version": 1
        })json");

    ExtensionDiscoveryContext context = defaultContext();
    context.environments_root = temp_dir.path() / "ExtensionEnvs";
    context.bundled_python_executable = deployed_python;
    context.runtime_root = runtime_root;

    std::vector discovered = {an::ui::extensions::load_extension_manifest_file(
        package_root / "extension.json", ExtensionPackageSource::Bundled, context)};

    ExtensionEnvironmentManager environment_manager;
    ASSERT_EQ(environment_manager.queueBundledBootstrap(discovered), 1);

    std::string error;
    ASSERT_EQ(environment_manager.processQueuedBundledBootstrap(discovered, &error), 1) << error;
    ASSERT_TRUE(discovered.front().environment.has_value());
    EXPECT_EQ(discovered.front().environment->state, ExtensionEnvironmentState::Ready);
}

// The queued bootstrap installs locked dependencies from the package's wheelhouse.
TEST(ExtensionManagerTest, ProcessingQueuedBundledBootstrapInstallsLockedWheelhouseDependencies)
{
    ScopedTempDirectory temp_dir;
    const auto package_root = createExtensionPackage(temp_dir.path(), "bundled_deps_ext",
                                                     R"json({
          "id": "bundled_deps_ext",
          "name": "Bundled Dependencies Extension",
          "version": "1.0.0",
          "type": "python",
          "entry_point": "bundled_deps_ext.entry",
          "process_class": "BundledDepsExtension",
          "category": "filters",
          "launch_mode": "single_image",
          "package_format_version": 1,
          "protocol_version": 1,
          "sdk_version": 1,
          "dependencies": {
            "requirements_file": "requirements.lock",
            "pip": {
              "require_hashes": true,
              "find_links": ["wheelhouse"]
            }
          }
        })json");
    const auto wheel_path = createPurePythonWheel(package_root, "demo-pkg", "1.0.0");
    ASSERT_FALSE(wheel_path.empty());
    const auto wheel_hash = sha256FileHex(wheel_path);
    ASSERT_FALSE(wheel_hash.empty());
    writeTextFile(package_root / "requirements.lock", "demo-pkg==1.0.0 --hash=sha256:" + wheel_hash + "\n");

    ExtensionDiscoveryContext context = defaultContext();
    context.environments_root = temp_dir.path() / "ExtensionEnvs";

    std::vector discovered = {an::ui::extensions::load_extension_manifest_file(
        package_root / "extension.json", ExtensionPackageSource::Bundled, context)};

    ExtensionEnvironmentManager environment_manager;
    ASSERT_EQ(environment_manager.queueBundledBootstrap(discovered), 1);

    std::string error;
    EXPECT_EQ(environment_manager.processQueuedBundledBootstrap(discovered, &error), 1) << error;
    ASSERT_TRUE(discovered.front().environment.has_value());
    EXPECT_EQ(discovered.front().environment->state, ExtensionEnvironmentState::Ready);

    const auto env_python =
#ifdef _WIN32
        discovered.front().environment->environment_path / "Scripts" / "python.exe";
#else
        discovered.front().environment->environment_path / "bin" / "python";
#endif
    EXPECT_TRUE(std::filesystem::exists(env_python));

    QProcess probe;
    probe.setProgram(QString::fromStdString(env_python.string()));
    probe.setArguments({QStringLiteral("-c"), QStringLiteral("import demo_pkg; print(demo_pkg.__version__)")});
    probe.start();
    ASSERT_TRUE(probe.waitForStarted());
    ASSERT_TRUE(probe.waitForFinished());
    EXPECT_EQ(probe.exitCode(), 0) << probe.readAllStandardError().toStdString();
    EXPECT_EQ(probe.readAllStandardOutput().trimmed().toStdString(), "1.0.0");

    const auto receipt = loadJsonFile(discovered.front().environment->receipt_path);
    const auto distributions = receipt.value(QStringLiteral("resolved_distributions")).toArray();
    ASSERT_GE(distributions.size(), 2);

    bool found_demo_pkg = false;
    for (const auto& dist : distributions)
    {
        const auto obj = dist.toObject();
        if (obj.value(QStringLiteral("name")).toString() == QStringLiteral("demo-pkg"))
        {
            EXPECT_EQ(obj.value(QStringLiteral("version")).toString().toStdString(), "1.0.0");
            found_demo_pkg = true;
            break;
        }
    }
    EXPECT_TRUE(found_demo_pkg) << "demo-pkg not found in resolved_distributions";

    const auto rediscovered = an::ui::extensions::load_extension_manifest_file(
        package_root / "extension.json", ExtensionPackageSource::Bundled, context);
    ASSERT_TRUE(rediscovered.environment.has_value());
    EXPECT_EQ(rediscovered.environment->state, ExtensionEnvironmentState::Ready);
}

// The queued bootstrap stages the bundled runtime packages an extension declares.
TEST(ExtensionManagerTest, ProcessingQueuedBundledBootstrapStagesDeclaredBundledRuntimePackages)
{
    const std::filesystem::path runtime_root = runtimeRootForCurrentAppDir();
    ASSERT_FALSE(runtime_root.empty()) << "Deployed PythonLib runtime root must be available";
#ifdef _WIN32
    const auto numpy_path = runtime_root / "Lib" / "site-packages" / "numpy";
#else
    const auto numpy_path = runtime_root / "lib" / "python3.14" / "site-packages" / "numpy";
#endif
    ASSERT_TRUE(std::filesystem::exists(numpy_path)) << runtime_root;

    ScopedTempDirectory temp_dir;
    const auto package_root = createExtensionPackage(temp_dir.path(), "bundled_runtime_pkg_ext",
                                                     R"json({
          "id": "bundled_runtime_pkg_ext",
          "name": "Bundled Runtime Package Extension",
          "version": "1.0.0",
          "type": "python",
          "entry_point": "bundled_runtime_pkg_ext.entry",
          "process_class": "BundledRuntimePackageExtension",
          "category": "filters",
          "launch_mode": "single_image",
          "package_format_version": 1,
          "protocol_version": 1,
          "sdk_version": 1,
          "dependencies": {
            "bundled_runtime_packages": ["numpy"]
          }
        })json");

    ExtensionDiscoveryContext context = defaultContext();
    context.environments_root = temp_dir.path() / "ExtensionEnvs";
    context.runtime_root = runtime_root;

    std::vector discovered = {an::ui::extensions::load_extension_manifest_file(
        package_root / "extension.json", ExtensionPackageSource::Bundled, context)};

    ExtensionEnvironmentManager environment_manager;
    ASSERT_EQ(environment_manager.queueBundledBootstrap(discovered), 1);

    std::string error;
    EXPECT_EQ(environment_manager.processQueuedBundledBootstrap(discovered, &error), 1) << error;
    ASSERT_TRUE(discovered.front().environment.has_value());
    EXPECT_EQ(discovered.front().environment->state, ExtensionEnvironmentState::Ready);

    const auto env_python =
#ifdef _WIN32
        discovered.front().environment->environment_path / "Scripts" / "python.exe";
#else
        discovered.front().environment->environment_path / "bin" / "python";
#endif
    EXPECT_TRUE(std::filesystem::exists(env_python));

    QProcess probe;
    probe.setProgram(QString::fromStdString(env_python.string()));
    probe.setArguments({QStringLiteral("-c"), QStringLiteral("import numpy; print(numpy.__version__)")});
    probe.start();
    ASSERT_TRUE(probe.waitForStarted());
    ASSERT_TRUE(probe.waitForFinished());
    EXPECT_EQ(probe.exitCode(), 0) << probe.readAllStandardError().toStdString();
    EXPECT_FALSE(probe.readAllStandardOutput().trimmed().isEmpty());

    const auto receipt = loadJsonFile(discovered.front().environment->receipt_path);
    const auto distributions = receipt.value(QStringLiteral("resolved_distributions")).toArray();
    ASSERT_GE(distributions.size(), 2);

    bool found_numpy = false;
    for (const auto& dist : distributions)
    {
        const auto obj = dist.toObject();
        if (obj.value(QStringLiteral("name")).toString() == QStringLiteral("numpy"))
        {
            found_numpy = true;
            break;
        }
    }
    EXPECT_TRUE(found_numpy) << "numpy not found in resolved_distributions";
}

// A no-dependencies install still receives the bundled runtime packages it declares.
TEST(ExtensionManagerTest, ProcessingQueuedBundledBootstrapSupportsNoDepsWithBundledRuntimePackages)
{
    const std::filesystem::path runtime_root = runtimeRootForCurrentAppDir();
    ASSERT_FALSE(runtime_root.empty()) << "Deployed PythonLib runtime root must be available";
#ifdef _WIN32
    const auto numpy_path = runtime_root / "Lib" / "site-packages" / "numpy";
#else
    const auto numpy_path = runtime_root / "lib" / "python3.14" / "site-packages" / "numpy";
#endif
    ASSERT_TRUE(std::filesystem::exists(numpy_path)) << runtime_root;

    ScopedTempDirectory temp_dir;
    const auto package_root = createExtensionPackage(temp_dir.path(), "bundled_no_deps_ext", std::string(R"json({
          "id": "bundled_no_deps_ext",
          "name": "Bundled No Deps Extension",
          "version": "1.0.0",
          "type": "python",
          "entry_point": "bundled_no_deps_ext.entry",
          "process_class": "BundledNoDepsExtension",
          "category": "filters",
          "launch_mode": "single_image",
          "package_format_version": 1,
          "protocol_version": 1,
          "sdk_version": 1,
          "runtime_targets": [")json") + currentRuntimeTarget() + R"json("],
          "dependencies": {
            "requirements_file": "requirements.lock",
            "bundled_runtime_packages": ["numpy"],
            "pip": {
              "require_hashes": true,
              "no_deps": true,
              "find_links": ["wheelhouse"]
            }
          }
        })json");
    const auto wheel_path = createPurePythonWheel(package_root, "demo-nodeps-pkg", "1.0.0", {"numpy<2.3"});
    ASSERT_FALSE(wheel_path.empty());
    const auto wheel_hash = sha256FileHex(wheel_path);
    ASSERT_FALSE(wheel_hash.empty());
    writeTextFile(package_root / "requirements.lock", "demo-nodeps-pkg==1.0.0 --hash=sha256:" + wheel_hash + "\n");

    ExtensionDiscoveryContext context = defaultContext();
    context.environments_root = temp_dir.path() / "ExtensionEnvs";
    context.runtime_root = runtime_root;

    std::vector discovered = {an::ui::extensions::load_extension_manifest_file(
        package_root / "extension.json", ExtensionPackageSource::Bundled, context)};

    ExtensionEnvironmentManager environment_manager;
    ASSERT_EQ(environment_manager.queueBundledBootstrap(discovered), 1);

    std::string error;
    EXPECT_EQ(environment_manager.processQueuedBundledBootstrap(discovered, &error), 1) << error;
    ASSERT_TRUE(discovered.front().environment.has_value());
    EXPECT_EQ(discovered.front().environment->state, ExtensionEnvironmentState::Ready);

    const auto env_python =
#ifdef _WIN32
        discovered.front().environment->environment_path / "Scripts" / "python.exe";
#else
        discovered.front().environment->environment_path / "bin" / "python";
#endif
    EXPECT_TRUE(std::filesystem::exists(env_python));

    QProcess probe;
    probe.setProgram(QString::fromStdString(env_python.string()));
    probe.setArguments(
        {QStringLiteral("-c"),
         QStringLiteral(
             "import demo_nodeps_pkg, numpy; print(demo_nodeps_pkg.__version__); print(numpy.__version__)")});
    probe.start();
    ASSERT_TRUE(probe.waitForStarted());
    ASSERT_TRUE(probe.waitForFinished());
    EXPECT_EQ(probe.exitCode(), 0) << probe.readAllStandardError().toStdString();
    const auto lines = probe.readAllStandardOutput().trimmed().split('\n');
    ASSERT_GE(lines.size(), 2);
    EXPECT_EQ(lines[0].trimmed().toStdString(), "1.0.0");
    EXPECT_FALSE(lines[1].trimmed().toStdString().empty());

    const auto receipt = loadJsonFile(discovered.front().environment->receipt_path);
    const auto distributions = receipt.value(QStringLiteral("resolved_distributions")).toArray();
    ASSERT_GE(distributions.size(), 3);

    bool found_demo_pkg = false;
    bool found_numpy = false;
    for (const auto& dist : distributions)
    {
        const auto obj = dist.toObject();
        if (obj.value(QStringLiteral("name")).toString() == QStringLiteral("demo-nodeps-pkg"))
        {
            found_demo_pkg = true;
        }
        if (obj.value(QStringLiteral("name")).toString() == QStringLiteral("numpy"))
        {
            found_numpy = true;
        }
    }
    EXPECT_TRUE(found_demo_pkg);
    EXPECT_TRUE(found_numpy);
}

// An offline root wheel no requirement names is installed without its dependencies.
TEST(ExtensionManagerTest, ProcessingQueuedBundledBootstrapInstallsUnmatchedOfflineRootWheelWithNoDeps)
{
    ScopedTempDirectory temp_dir;
    const auto package_root = createExtensionPackage(temp_dir.path(), "bundled_root_wheel_ext",
                                                     R"json({
          "id": "bundled_root_wheel_ext",
          "name": "Bundled Root Wheel Extension",
          "version": "1.0.0",
          "type": "python",
          "entry_point": "bundled_root_wheel_ext.entry",
          "process_class": "BundledRootWheelExtension",
          "category": "filters",
          "launch_mode": "single_image",
          "package_format_version": 1,
          "protocol_version": 1,
          "sdk_version": 1,
          "dependencies": {
            "requirements_file": "requirements.lock",
            "pip": {
              "require_hashes": true,
              "no_deps": true,
              "find_links": ["wheelhouse"]
            }
          }
        })json");
    const auto root_wheel = createPurePythonWheel(package_root, "demo-root-pkg", "1.0.0", {"missing-ui>=1.0"});
    const auto helper_wheel = createPurePythonWheel(package_root, "demo-helper-pkg", "1.0.0");
    ASSERT_FALSE(root_wheel.empty());
    ASSERT_FALSE(helper_wheel.empty());
    const auto helper_hash = sha256FileHex(helper_wheel);
    ASSERT_FALSE(helper_hash.empty());
    writeTextFile(package_root / "requirements.lock", "demo-helper-pkg==1.0.0 --hash=sha256:" + helper_hash + "\n");

    ExtensionDiscoveryContext context = defaultContext();
    context.environments_root = temp_dir.path() / "ExtensionEnvs";

    std::vector discovered = {an::ui::extensions::load_extension_manifest_file(
        package_root / "extension.json", ExtensionPackageSource::Bundled, context)};

    ExtensionEnvironmentManager environment_manager;
    ASSERT_EQ(environment_manager.queueBundledBootstrap(discovered), 1);

    std::string error;
    EXPECT_EQ(environment_manager.processQueuedBundledBootstrap(discovered, &error), 1) << error;
    ASSERT_TRUE(discovered.front().environment.has_value());
    EXPECT_EQ(discovered.front().environment->state, ExtensionEnvironmentState::Ready);

    const auto env_python =
#ifdef _WIN32
        discovered.front().environment->environment_path / "Scripts" / "python.exe";
#else
        discovered.front().environment->environment_path / "bin" / "python";
#endif
    EXPECT_TRUE(std::filesystem::exists(env_python));

    QProcess probe;
    probe.setProgram(QString::fromStdString(env_python.string()));
    probe.setArguments(
        {QStringLiteral("-c"), QStringLiteral("import demo_root_pkg, demo_helper_pkg; "
                                              "print(demo_root_pkg.__version__); print(demo_helper_pkg.__version__)")});
    probe.start();
    ASSERT_TRUE(probe.waitForStarted());
    ASSERT_TRUE(probe.waitForFinished());
    EXPECT_EQ(probe.exitCode(), 0) << probe.readAllStandardError().toStdString();
    const auto lines = probe.readAllStandardOutput().trimmed().split('\n');
    ASSERT_GE(lines.size(), 2);
    EXPECT_EQ(lines[0].trimmed().toStdString(), "1.0.0");
    EXPECT_EQ(lines[1].trimmed().toStdString(), "1.0.0");

    const auto receipt = loadJsonFile(discovered.front().environment->receipt_path);
    const auto distributions = receipt.value(QStringLiteral("resolved_distributions")).toArray();
    ASSERT_GE(distributions.size(), 3);

    bool found_root_pkg = false;
    bool found_helper_pkg = false;
    for (const auto& dist : distributions)
    {
        const auto obj = dist.toObject();
        if (obj.value(QStringLiteral("name")).toString() == QStringLiteral("demo-root-pkg"))
        {
            found_root_pkg = true;
        }
        if (obj.value(QStringLiteral("name")).toString() == QStringLiteral("demo-helper-pkg"))
        {
            found_helper_pkg = true;
        }
    }
    EXPECT_TRUE(found_root_pkg);
    EXPECT_TRUE(found_helper_pkg);
}

// A bootstrap whose offline wheelhouse lacks a locked artifact fails, saying which.
TEST(ExtensionManagerTest, ProcessingQueuedBundledBootstrapFailsWhenOfflineWheelhouseIsMissingArtifact)
{
    ScopedTempDirectory temp_dir;
    const auto package_root = createExtensionPackage(temp_dir.path(), "bundled_missing_wheel_ext",
                                                     R"json({
          "id": "bundled_missing_wheel_ext",
          "name": "Bundled Missing Wheel Extension",
          "version": "1.0.0",
          "type": "python",
          "entry_point": "bundled_missing_wheel_ext.entry",
          "process_class": "BundledMissingWheelExtension",
          "category": "filters",
          "launch_mode": "single_image",
          "package_format_version": 1,
          "protocol_version": 1,
          "sdk_version": 1,
          "dependencies": {
            "requirements_file": "requirements.lock",
            "pip": {
              "require_hashes": true,
              "find_links": ["wheelhouse"]
            }
          }
        })json");
    writeTextFile(package_root / "requirements.lock", "numpy==2.0.0 --hash=sha256:aaa\n");
    writeTextFile(package_root / "wheelhouse" / ".keep", "");

    ExtensionDiscoveryContext context = defaultContext();
    context.environments_root = temp_dir.path() / "ExtensionEnvs";

    std::vector discovered = {an::ui::extensions::load_extension_manifest_file(
        package_root / "extension.json", ExtensionPackageSource::Bundled, context)};

    ExtensionEnvironmentManager environment_manager;
    ASSERT_EQ(environment_manager.queueBundledBootstrap(discovered), 1);

    std::string error;
    EXPECT_EQ(environment_manager.processQueuedBundledBootstrap(discovered, &error), 1) << error;
    ASSERT_TRUE(discovered.front().environment.has_value());
    EXPECT_EQ(discovered.front().environment->state, ExtensionEnvironmentState::Failed);
    EXPECT_NE(discovered.front().environment->diagnostic.find("numpy==2.0.0"), std::string::npos);
    EXPECT_NE(discovered.front().environment->diagnostic.find("wheelhouse"), std::string::npos);

    const auto rediscovered = an::ui::extensions::load_extension_manifest_file(
        package_root / "extension.json", ExtensionPackageSource::Bundled, context);
    ASSERT_TRUE(rediscovered.environment.has_value());
    EXPECT_EQ(rediscovered.environment->state, ExtensionEnvironmentState::Failed);
}

// A compiled wheel in a package that declares no runtime targets is refused.
TEST(ExtensionManagerTest, ProcessingQueuedBundledBootstrapRejectsCompiledWheelsWithoutRuntimeTargets)
{
    ScopedTempDirectory temp_dir;
    const auto package_root = createExtensionPackage(temp_dir.path(), "compiled_wheel_ext",
                                                     R"json({
          "id": "compiled_wheel_ext",
          "name": "Compiled Wheel Extension",
          "version": "1.0.0",
          "type": "python",
          "entry_point": "compiled_wheel_ext.entry",
          "process_class": "CompiledWheelExtension",
          "category": "filters",
          "launch_mode": "single_image",
          "package_format_version": 1,
          "protocol_version": 1,
          "sdk_version": 1,
          "dependencies": {
            "requirements_file": "requirements.lock",
            "pip": {
              "require_hashes": true,
              "find_links": ["wheelhouse"]
            }
          }
        })json");
    const auto wheel_path = package_root / "wheelhouse" / "demo_pkg-1.0.0-cp314-cp314-manylinux_2_28_x86_64.whl";
    writeTextFile(wheel_path, "compiled-wheel");
    writeTextFile(package_root / "requirements.lock",
                  "demo-pkg==1.0.0 --hash=sha256:" + sha256FileHex(wheel_path) + "\n");

    ExtensionDiscoveryContext context = defaultContext();
    context.environments_root = temp_dir.path() / "ExtensionEnvs";

    std::vector discovered = {an::ui::extensions::load_extension_manifest_file(
        package_root / "extension.json", ExtensionPackageSource::Bundled, context)};

    ExtensionEnvironmentManager environment_manager;
    ASSERT_EQ(environment_manager.queueBundledBootstrap(discovered), 1);

    std::string error;
    EXPECT_EQ(environment_manager.processQueuedBundledBootstrap(discovered, &error), 1) << error;
    ASSERT_TRUE(discovered.front().environment.has_value());
    EXPECT_EQ(discovered.front().environment->state, ExtensionEnvironmentState::Failed);
    EXPECT_NE(discovered.front().environment->diagnostic.find("runtime_targets"), std::string::npos);
}

// A compiled wheel for another interpreter's ABI is refused.
TEST(ExtensionManagerTest, ProcessingQueuedBundledBootstrapRejectsCompiledWheelsWithIncompatibleAbi)
{
    ScopedTempDirectory temp_dir;
    const auto package_root = createExtensionPackage(temp_dir.path(), "compiled_wheel_abi_ext", std::string(R"json({
          "id": "compiled_wheel_abi_ext",
          "name": "Compiled Wheel ABI Extension",
          "version": "1.0.0",
          "type": "python",
          "entry_point": "compiled_wheel_abi_ext.entry",
          "process_class": "CompiledWheelAbiExtension",
          "category": "filters",
          "launch_mode": "single_image",
          "package_format_version": 1,
          "protocol_version": 1,
          "sdk_version": 1,
          "runtime_targets": [")json") + currentRuntimeTarget() + R"json("],
          "dependencies": {
            "requirements_file": "requirements.lock",
            "pip": {
              "require_hashes": true,
              "find_links": ["wheelhouse"]
            }
          }
        })json");
    const auto wheel_path = package_root / "wheelhouse" / "demo_pkg-1.0.0-cp313-cp313-manylinux_2_28_x86_64.whl";
    writeTextFile(wheel_path, "compiled-wheel");
    writeTextFile(package_root / "requirements.lock",
                  "demo-pkg==1.0.0 --hash=sha256:" + sha256FileHex(wheel_path) + "\n");

    ExtensionDiscoveryContext context = defaultContext();
    context.environments_root = temp_dir.path() / "ExtensionEnvs";

    std::vector discovered = {an::ui::extensions::load_extension_manifest_file(
        package_root / "extension.json", ExtensionPackageSource::Bundled, context)};

    ExtensionEnvironmentManager environment_manager;
    ASSERT_EQ(environment_manager.queueBundledBootstrap(discovered), 1);

    std::string error;
    EXPECT_EQ(environment_manager.processQueuedBundledBootstrap(discovered, &error), 1) << error;
    ASSERT_TRUE(discovered.front().environment.has_value());
    EXPECT_EQ(discovered.front().environment->state, ExtensionEnvironmentState::Failed);
    EXPECT_NE(discovered.front().environment->diagnostic.find("cp314"), std::string::npos);
}

// A shared group's fingerprint moves when its dependency inputs change.
TEST(ExtensionManagerTest, SharedGroupFingerprintChangesWhenDependencyInputsChange)
{
    ScopedTempDirectory temp_dir;
    const auto package_root = createExtensionPackage(temp_dir.path(), "shared_deps_ext",
                                                     R"json({
          "id": "shared_deps_ext",
          "name": "Shared Dependencies Extension",
          "version": "1.0.0",
          "type": "python",
          "entry_point": "shared_deps.entry",
          "process_class": "SharedDepsExtension",
          "category": "filters",
          "launch_mode": "single_image",
          "package_format_version": 1,
          "protocol_version": 1,
          "sdk_version": 1,
          "dependencies": {
            "requirements_file": "requirements.lock",
            "shared_group": "suite_shared",
            "pip": {
              "require_hashes": true,
              "find_links": ["wheels"]
            }
          }
        })json");
    writeTextFile(package_root / "requirements.lock", "numpy==2.0.0 --hash=sha256:aaa\n");
    writeTextFile(package_root / "wheels" / "numpy.whl", "wheel-v1");

    ExtensionDiscoveryContext context = defaultContext();
    context.environments_root = temp_dir.path() / "ExtensionEnvs";

    const auto first = an::ui::extensions::load_extension_manifest_file(package_root / "extension.json",
                                                                        ExtensionPackageSource::User, context);
    ASSERT_TRUE(first.environment.has_value());
    EXPECT_NE(first.environment->environment_path.generic_string().find("_shared/suite_shared"), std::string::npos);

    // Redeploy the dependency. A routine inspection recognises the change through the path-tree
    // hash cache's metadata guard - relative path, size and last-write time - so the timestamp has
    // to move for this to be a redeploy rather than the deliberate metadata-only cache hit that
    // ExplicitVerificationBypassesMetadataOnlyPathTreeCache pins. "wheel-v1" and "wheel-v2" are the
    // same length, and back-to-back writes routinely land in one file-time tick, so without the
    // explicit bump this test only passed when the first inspection happened to be slow enough.
    const auto wheel_path = package_root / "wheels" / "numpy.whl";
    std::error_code ec;
    const auto original_write_time = std::filesystem::last_write_time(wheel_path, ec);
    ASSERT_FALSE(ec) << ec.message();
    writeTextFile(wheel_path, "wheel-v2");
    std::filesystem::last_write_time(wheel_path, original_write_time + std::chrono::hours(24), ec);
    ASSERT_FALSE(ec) << ec.message();

    const auto second = an::ui::extensions::load_extension_manifest_file(package_root / "extension.json",
                                                                         ExtensionPackageSource::User, context);
    ASSERT_TRUE(second.environment.has_value());
    EXPECT_NE(first.environment->dependency_fingerprint, second.environment->dependency_fingerprint);
}

// A shared group's fingerprint stays when a redeploy changes only modification times.
TEST(ExtensionManagerTest, SharedGroupFingerprintIgnoresMtimeOnlyDependencyRedeployNoise)
{
    ScopedTempDirectory temp_dir;
    const auto package_root = createExtensionPackage(temp_dir.path(), "shared_deps_mtime_ext",
                                                     R"json({
          "id": "shared_deps_mtime_ext",
          "name": "Shared Dependencies Mtime Extension",
          "version": "1.0.0",
          "type": "python",
          "entry_point": "shared_deps_mtime.entry",
          "process_class": "SharedDepsMtimeExtension",
          "category": "filters",
          "launch_mode": "single_image",
          "package_format_version": 1,
          "protocol_version": 1,
          "sdk_version": 1,
          "dependencies": {
            "requirements_file": "requirements.lock",
            "shared_group": "suite_shared_mtime",
            "pip": {
              "require_hashes": true,
              "find_links": ["wheels"]
            }
          }
        })json");
    writeTextFile(package_root / "requirements.lock", "numpy==2.0.0 --hash=sha256:aaa\n");
    const auto wheel_path = package_root / "wheels" / "numpy.whl";
    writeTextFile(wheel_path, "wheel-v1");

    ExtensionDiscoveryContext context = defaultContext();
    context.environments_root = temp_dir.path() / "ExtensionEnvs";

    const auto first = an::ui::extensions::load_extension_manifest_file(package_root / "extension.json",
                                                                        ExtensionPackageSource::User, context);
    ASSERT_TRUE(first.environment.has_value());

    std::error_code ec;
    const auto original_write_time = std::filesystem::last_write_time(wheel_path, ec);
    ASSERT_FALSE(ec) << ec.message();
    std::filesystem::last_write_time(wheel_path, original_write_time + std::chrono::hours(24), ec);
    ASSERT_FALSE(ec) << ec.message();

    const auto second = an::ui::extensions::load_extension_manifest_file(package_root / "extension.json",
                                                                         ExtensionPackageSource::User, context);
    ASSERT_TRUE(second.environment.has_value());
    EXPECT_EQ(first.environment->dependency_fingerprint, second.environment->dependency_fingerprint);
}

// A routine inspection reuses the tree's metadata cache; an explicit verification reads the contents and sees a
// change the cache hides.
TEST(ExtensionManagerTest, ExplicitVerificationBypassesMetadataOnlyPathTreeCache)
{
    ScopedTempDirectory temp_dir;
    const auto package_root = temp_dir.path() / "package";
    const auto wheel_path = package_root / "wheels" / "demo.whl";
    writeTextFile(wheel_path, "wheel-v1");

    ExtensionManifest manifest;
    manifest.id = "shared_group_cache";
    manifest.version = "1.0.0";
    manifest.package_root = package_root.generic_string();
    manifest.dependencies.dependency_context = ExtensionDependencyContext::SharedGroup;
    manifest.dependencies.shared_group = "cache_test";
    manifest.dependencies.pip.find_links = {"wheels"};

    auto context = defaultContext();
    context.environments_root = temp_dir.path() / "ExtensionEnvs";

    ExtensionEnvironmentManager initial_manager;
    const auto initial = initial_manager.inspectEnvironment(manifest, ExtensionPackageSource::User, context);

    std::error_code ec;
    const auto original_write_time = std::filesystem::last_write_time(wheel_path, ec);
    ASSERT_FALSE(ec) << ec.message();
    writeTextFile(wheel_path, "wheel-v2");
    std::filesystem::last_write_time(wheel_path, original_write_time, ec);
    ASSERT_FALSE(ec) << ec.message();

    ExtensionEnvironmentManager routine_manager;
    const auto routine = routine_manager.inspectEnvironment(manifest, ExtensionPackageSource::User, context);
    EXPECT_EQ(routine.dependency_fingerprint, initial.dependency_fingerprint);

    ExtensionEnvironmentManager verification_manager;
    const auto verified = verification_manager.inspectEnvironment(manifest, ExtensionPackageSource::User, context, {},
                                                                  ExtensionEnvironmentInspectionMode::VerifyContents);
    EXPECT_NE(verified.dependency_fingerprint, initial.dependency_fingerprint);
}

// Packages that disagree about a shared group's dependencies are refused at discovery.
TEST(ExtensionManagerTest, SharedGroupConflictsAreRejectedDuringDiscovery)
{
    ScopedTempDirectory temp_dir;
    const auto user_root = temp_dir.path() / "user";

    const auto first_package_root = createExtensionPackage(user_root, "suite_a",
                                                           R"json({
          "id": "suite_a",
          "name": "Suite A",
          "version": "1.0.0",
          "type": "python",
          "entry_point": "suite_a.entry",
          "process_class": "SuiteAExtension",
          "category": "filters",
          "launch_mode": "single_image",
          "package_format_version": 1,
          "protocol_version": 1,
          "sdk_version": 1,
          "dependencies": {
            "requirements_file": "requirements.lock",
            "shared_group": "suite_shared",
            "pip": {
              "require_hashes": true,
              "find_links": ["wheels"]
            }
          }
        })json");
    writeTextFile(first_package_root / "requirements.lock", "numpy==2.0.0 --hash=sha256:aaa\n");
    writeTextFile(first_package_root / "wheels" / "numpy.whl", "wheel-a");

    const auto second_package_root = createExtensionPackage(user_root, "suite_b",
                                                            R"json({
          "id": "suite_b",
          "name": "Suite B",
          "version": "1.0.0",
          "type": "python",
          "entry_point": "suite_b.entry",
          "process_class": "SuiteBExtension",
          "category": "filters",
          "launch_mode": "single_image",
          "package_format_version": 1,
          "protocol_version": 1,
          "sdk_version": 1,
          "dependencies": {
            "requirements_file": "requirements.lock",
            "shared_group": "suite_shared",
            "pip": {
              "require_hashes": true,
              "find_links": ["wheels"]
            }
          }
        })json");
    writeTextFile(second_package_root / "requirements.lock", "numpy==2.0.1 --hash=sha256:bbb\n");
    writeTextFile(second_package_root / "wheels" / "numpy.whl", "wheel-b");

    ExtensionDiscoveryOptions options;
    options.user_roots = {user_root};
    options.context = defaultContext();
    options.context.environments_root = temp_dir.path() / "ExtensionEnvs";

    ExtensionManager manager;
    const auto discovered = manager.discoverExtensions(options);

    ASSERT_EQ(discovered.size(), 2u);
    for (const auto& result : discovered)
    {
        ASSERT_TRUE(result.manifest.has_value());
        EXPECT_EQ(result.status, ExtensionLoadStatus::Incompatible);
        EXPECT_NE(result.diagnostic.find("shared_group"), std::string::npos);
    }
}

// The environment inventory marks each entry orphaned, shared or versioned.
// An environment's size counts each file it holds once, where it lies. A link is not the file it points to: a venv on
// Linux links its interpreter, which lies outside the environment, three or four times over, and each link counted as
// the interpreter made an environment of under a gigabyte read as nearly one and a half.
TEST(ExtensionManagerTest, AnEnvironmentsSizeCountsNothingALinkPointsTo)
{
    ScopedTempDirectory temp_dir;
    const auto environments_root = temp_dir.path() / "ExtensionEnvs";
    const auto environment = environments_root / "linked_ext" / "1.0.0" / "fingerprint" / "linux-clang-x86_64";
    std::filesystem::create_directories(environment / "bin");
    std::filesystem::create_directories(temp_dir.path() / "runtime");
    const auto interpreter = temp_dir.path() / "runtime" / "python3.14";
    std::ofstream(interpreter, std::ios::binary) << std::string(65536u, 'p');
    std::ofstream(environment / "bin" / "activate", std::ios::binary) << std::string(4096u, 'a');

    std::error_code ec;
    std::filesystem::create_symlink(interpreter, environment / "bin" / "python3.14", ec);
    if (ec)
    {
        GTEST_SKIP() << "this account cannot create a symbolic link: " << ec.message();
    }
    std::filesystem::create_symlink(environment / "bin" / "python3.14", environment / "bin" / "python3", ec);
    ASSERT_FALSE(ec) << ec.message();
    std::filesystem::create_symlink(environment / "bin" / "activate", environment / "bin" / "activate.link", ec);
    ASSERT_FALSE(ec) << ec.message();

    ExtensionDiscoveryOptions options;
    options.context = defaultContext();
    options.context.environments_root = environments_root;
    ExtensionManager discovery_manager;
    const auto discovered = discovery_manager.discoverExtensions(options);

    ExtensionEnvironmentManager environment_manager;
    const auto inventory = environment_manager.collectEnvironmentInventory(discovered, options.context);
    ASSERT_EQ(inventory.size(), 1u);
    EXPECT_EQ(inventory.front().size_bytes, 4096u) << "the links were counted as the files they point to";
}

TEST(ExtensionManagerTest, EnvironmentInventoryMarksOrphanedSharedAndVersionedEntries)
{
    ScopedTempDirectory temp_dir;
    const auto user_root = temp_dir.path() / "user";
    const auto live_package_root = createExtensionPackage(user_root, "live_ext",
                                                          R"json({
          "id": "live_ext",
          "name": "Live Extension",
          "version": "1.0.0",
          "type": "python",
          "entry_point": "live_ext.entry",
          "process_class": "LiveExtension",
          "category": "filters",
          "launch_mode": "single_image",
          "package_format_version": 1,
          "protocol_version": 1,
          "sdk_version": 1
        })json");
    (void)live_package_root;

    const auto suite_package_root = createExtensionPackage(user_root, "suite_live",
                                                           R"json({
          "id": "suite_live",
          "name": "Suite Live",
          "version": "1.0.0",
          "type": "python",
          "entry_point": "suite_live.entry",
          "process_class": "SuiteLiveExtension",
          "category": "filters",
          "launch_mode": "single_image",
          "package_format_version": 1,
          "protocol_version": 1,
          "sdk_version": 1,
          "dependencies": {
            "requirements_file": "requirements.lock",
            "shared_group": "live_suite",
            "pip": {
              "require_hashes": true
            }
          }
        })json");
    writeTextFile(suite_package_root / "requirements.lock", "numpy==2.0.0 --hash=sha256:aaa\n");

    const auto environments_root = temp_dir.path() / "ExtensionEnvs";
    ExtensionDiscoveryOptions options;
    options.user_roots = {user_root};
    options.context = defaultContext();
    options.context.environments_root = environments_root;

    const auto live_result = an::ui::extensions::load_extension_manifest_file(
        live_package_root / "extension.json", ExtensionPackageSource::User, options.context);
    ASSERT_TRUE(live_result.environment.has_value());
    const auto suite_result = an::ui::extensions::load_extension_manifest_file(
        suite_package_root / "extension.json", ExtensionPackageSource::User, options.context);
    ASSERT_TRUE(suite_result.environment.has_value());

    std::filesystem::create_directories(live_result.environment->environment_path);
    std::filesystem::create_directories(environments_root / "orphan_ext" / "9.9.9" / "fingerprint-old" /
                                        "linux-clang-x86_64");
    std::filesystem::create_directories(suite_result.environment->environment_path);
    std::filesystem::create_directories(environments_root / "_shared" / "orphan_suite" / "fingerprint-orphan" /
                                        "linux-clang-x86_64");

    ExtensionManager discovery_manager;
    const auto discovered = discovery_manager.discoverExtensions(options);

    ExtensionEnvironmentManager environment_manager;
    const auto inventory = environment_manager.collectEnvironmentInventory(discovered, options.context);
    ASSERT_EQ(inventory.size(), 4u);

    int orphaned_count = 0;
    int live_count = 0;
    for (const auto& entry : inventory)
    {
        if (entry.orphaned)
        {
            ++orphaned_count;
        }
        else
        {
            ++live_count;
        }
    }

    EXPECT_EQ(orphaned_count, 2);
    EXPECT_EQ(live_count, 2);

    const auto orphaned_paths = environment_manager.collectOrphanedEnvironmentPaths(discovered, options.context);
    ASSERT_EQ(orphaned_paths.size(), 2u);
    EXPECT_NE(std::find_if(orphaned_paths.begin(), orphaned_paths.end(),
                           [](const auto& path) { return path.string().find("orphan_ext") != std::string::npos; }),
              orphaned_paths.end());
    EXPECT_NE(std::find_if(orphaned_paths.begin(), orphaned_paths.end(),
                           [](const auto& path) { return path.string().find("orphan_suite") != std::string::npos; }),
              orphaned_paths.end());
}

// Removing orphans deletes the orphaned environments and nothing else.
TEST(ExtensionManagerTest, RemoveOrphanedEnvironmentPathsDeletesOnlyOrphans)
{
    ScopedTempDirectory temp_dir;
    const auto user_root = temp_dir.path() / "user";
    createExtensionPackage(user_root, "live_ext",
                           R"json({
          "id": "live_ext",
          "name": "Live Extension",
          "version": "1.0.0",
          "type": "python",
          "entry_point": "live_ext.entry",
          "process_class": "LiveExtension",
          "category": "filters",
          "launch_mode": "single_image",
          "package_format_version": 1,
          "protocol_version": 1,
          "sdk_version": 1
        })json");

    const auto environments_root = temp_dir.path() / "ExtensionEnvs";
    ExtensionDiscoveryOptions options;
    options.user_roots = {user_root};
    options.context = defaultContext();
    options.context.environments_root = environments_root;

    const auto live_result = an::ui::extensions::load_extension_manifest_file(
        user_root / "live_ext" / "extension.json", ExtensionPackageSource::User, options.context);
    ASSERT_TRUE(live_result.environment.has_value());
    const auto live_path = live_result.environment->environment_path;
    const auto orphan_path = environments_root / "orphan_ext" / "9.9.9" / "fingerprint-old" / "linux-clang-x86_64";
    std::filesystem::create_directories(live_path);
    std::filesystem::create_directories(orphan_path);

    ExtensionManager discovery_manager;
    const auto discovered = discovery_manager.discoverExtensions(options);

    ExtensionEnvironmentManager environment_manager;
    std::vector<std::filesystem::path> removed_paths;
    std::string error;
    EXPECT_TRUE(environment_manager.removeOrphanedEnvironments(discovered, options.context, &removed_paths, &error))
        << error;

    EXPECT_TRUE(std::filesystem::exists(live_path));
    EXPECT_FALSE(std::filesystem::exists(orphan_path));
    ASSERT_EQ(removed_paths.size(), 1u);
    EXPECT_EQ(removed_paths.front(), orphan_path);
}

// An unused environment is removed only once a ready replacement exists, and never while one is provisioning.
TEST(ExtensionManagerTest, UnusedEnvironmentCleanupWaitsForAReadyReplacementAndSkipsProvisioning)
{
    ScopedTempDirectory temp_dir;
    const auto user_root = temp_dir.path() / "user";
    const auto package_root = createExtensionPackage(user_root, "replacement_guard_ext",
                                                     R"json({
          "id": "replacement_guard_ext",
          "name": "Replacement Guard Extension",
          "version": "2.0.0",
          "type": "python",
          "entry_point": "replacement_guard_ext.entry",
          "process_class": "ReplacementGuardExtension",
          "category": "filters",
          "launch_mode": "single_image",
          "package_format_version": 1,
          "protocol_version": 1,
          "sdk_version": 1
        })json");

    const auto environments_root = temp_dir.path() / "ExtensionEnvs";
    ExtensionDiscoveryOptions options;
    options.user_roots = {user_root};
    options.context = defaultContext();
    options.context.environments_root = environments_root;

    const auto current = an::ui::extensions::load_extension_manifest_file(
        package_root / "extension.json", ExtensionPackageSource::User, options.context);
    ASSERT_TRUE(current.environment.has_value());

    const auto old_path =
        environments_root / "replacement_guard_ext" / "1.0.0" / "old-fingerprint" / currentRuntimeTarget();
    writeTextFile(old_path / "Lib" / "site-packages" / "old_payload.bin", "old environment payload");

    ExtensionManager discovery_manager;
    auto discovered = discovery_manager.discoverExtensions(options);
    ExtensionEnvironmentManager environment_manager;
    ExtensionEnvironmentCleanupPolicy policy;
    policy.minimum_age = std::chrono::seconds::zero();

    EXPECT_TRUE(environment_manager.collectUnusedEnvironmentPaths(discovered, options.context, policy).empty());
    auto summary = environment_manager.summarizeEnvironmentStorage(discovered, options.context, policy);
    EXPECT_EQ(summary.active_environment_count, 0u);
    EXPECT_EQ(summary.reclaimable_environment_count, 0u);
    EXPECT_EQ(summary.protected_environment_count, 1u);
    EXPECT_GT(summary.protected_bytes, 0u);

    writeInstallReceipt(current.environment->environment_path, current.environment->host_bootstrap_fingerprint,
                        current.environment->dependency_fingerprint, current.environment->validation_fingerprint);
    discovered = discovery_manager.discoverExtensions(options);

    auto unused_paths = environment_manager.collectUnusedEnvironmentPaths(discovered, options.context, policy);
    ASSERT_EQ(unused_paths.size(), 1u);
    EXPECT_EQ(unused_paths.front(), old_path);
    summary = environment_manager.summarizeEnvironmentStorage(discovered, options.context, policy);
    EXPECT_EQ(summary.active_environment_count, 1u);
    EXPECT_EQ(summary.reclaimable_environment_count, 1u);
    EXPECT_EQ(summary.protected_environment_count, 0u);
    EXPECT_GT(summary.active_bytes, 0u);
    EXPECT_GT(summary.reclaimable_bytes, 0u);

    {
        const LiveOtherProcess owner;
        ASSERT_TRUE(owner.started());
        writeTextFile(old_path / ".provisioning.lock", owner.pidLine());
        EXPECT_TRUE(environment_manager.collectUnusedEnvironmentPaths(discovered, options.context, policy).empty());
    }

    writeTextFile(old_path / ".provisioning.lock", "owner_pid=0\n");
    EXPECT_EQ(environment_manager.collectUnusedEnvironmentPaths(discovered, options.context, policy),
              std::vector<std::filesystem::path>{old_path});
}

// Automatic cleanup removes unused environments older than the grace period and reports the bytes reclaimed.
TEST(ExtensionManagerTest, AutomaticUnusedEnvironmentCleanupAppliesGracePeriodAndReportsReclaimedBytes)
{
    ScopedTempDirectory temp_dir;
    const auto environments_root = temp_dir.path() / "ExtensionEnvs";
    const auto orphan_path =
        environments_root / "uninstalled_ext" / "1.0.0" / "old-fingerprint" / currentRuntimeTarget();
    writeTextFile(orphan_path / "Lib" / "site-packages" / "payload.bin", std::string(4096, 'x'));

    ExtensionDiscoveryContext context = defaultContext();
    context.environments_root = environments_root;
    ExtensionEnvironmentManager environment_manager;
    ExtensionEnvironmentCleanupPolicy policy = an::ui::extensions::automatic_extension_environment_cleanup_policy();
    ASSERT_GT(policy.minimum_age, std::chrono::seconds::zero());

    EXPECT_TRUE(environment_manager.collectUnusedEnvironmentPaths({}, context, policy).empty());

    std::error_code ec;
    (void)std::filesystem::last_write_time(
        orphan_path, std::filesystem::file_time_type::clock::now() - policy.minimum_age - std::chrono::hours(1), ec);
    ASSERT_FALSE(ec);

    const auto unused_paths = environment_manager.collectUnusedEnvironmentPaths({}, context, policy);
    ASSERT_EQ(unused_paths.size(), 1u);
    EXPECT_EQ(unused_paths.front(), orphan_path);

    std::vector<std::filesystem::path> removed_paths;
    std::uintmax_t reclaimed_bytes = 0;
    std::string error;
    EXPECT_TRUE(
        environment_manager.removeUnusedEnvironments({}, context, policy, &removed_paths, &reclaimed_bytes, &error))
        << error;
    EXPECT_TRUE(error.empty());
    ASSERT_EQ(removed_paths.size(), 1u);
    EXPECT_EQ(removed_paths.front(), orphan_path);
    EXPECT_GE(reclaimed_bytes, 4096u);
    EXPECT_FALSE(std::filesystem::exists(orphan_path));
}

// The validation fingerprint moves when a package's Python sources change.
TEST(ExtensionManagerTest, ValidationFingerprintChangesWhenPythonSourcesChange)
{
    ScopedTempDirectory temp_dir;
    const auto package_root = createExtensionPackage(temp_dir.path(), "validation_sources_ext",
                                                     R"json({
          "id": "validation_sources_ext",
          "name": "Validation Sources Extension",
          "version": "1.0.0",
          "type": "python",
          "entry_point": "validation_sources_ext.entry",
          "process_class": "ValidationSourcesExtension",
          "category": "filters",
          "launch_mode": "single_image",
          "package_format_version": 1,
          "protocol_version": 1,
          "sdk_version": 1
        })json");
    writeTextFile(package_root / "validation_sources_ext" / "__init__.py", "");
    writeTextFile(package_root / "validation_sources_ext" / "entry.py", "VALUE = 1\n");

    ExtensionDiscoveryContext context = defaultContext();
    context.environments_root = temp_dir.path() / "ExtensionEnvs";

    const auto initial = an::ui::extensions::load_extension_manifest_file(package_root / "extension.json",
                                                                          ExtensionPackageSource::User, context);
    ASSERT_TRUE(initial.environment.has_value());

    writeInstallReceipt(initial.environment->environment_path, initial.environment->host_bootstrap_fingerprint,
                        initial.environment->dependency_fingerprint, initial.environment->validation_fingerprint);

    writeTextFile(package_root / "validation_sources_ext" / "entry.py", "VALUE = 2\n");

    // The package's own sources are not what the environment installs: it keeps its place and is refreshed for them.
    const auto changed = an::ui::extensions::load_extension_manifest_file(package_root / "extension.json",
                                                                          ExtensionPackageSource::User, context);
    ASSERT_TRUE(changed.environment.has_value());
    EXPECT_NE(initial.environment->validation_fingerprint, changed.environment->validation_fingerprint);
    EXPECT_EQ(initial.environment->environment_path, changed.environment->environment_path);
    EXPECT_EQ(changed.environment->state, ExtensionEnvironmentState::Stale);
    EXPECT_TRUE(changed.environment->refreshable);
}

// A provisioning that cannot make its directory says which path and why.
TEST(ExtensionManagerTest, BeginProvisioningReportsPathAndFilesystemReason)
{
    ScopedTempDirectory temp_dir;
    const auto package_root = createExtensionPackage(temp_dir.path(), "env_error_detail_ext",
                                                     R"json({
          "id": "env_error_detail_ext",
          "name": "Environment Error Detail Extension",
          "version": "1.0.0",
          "type": "python",
          "entry_point": "env_error_detail_ext",
          "process_class": "EnvironmentErrorDetailExtension",
          "category": "filters",
          "launch_mode": "single_image",
          "package_format_version": 1,
          "protocol_version": 1,
          "sdk_version": 1
        })json");

    ExtensionDiscoveryContext context = defaultContext();
    context.environments_root = temp_dir.path() / "ExtensionEnvs";

    const auto load_result = an::ui::extensions::load_extension_manifest_file(package_root / "extension.json",
                                                                              ExtensionPackageSource::User, context);
    ASSERT_TRUE(load_result.environment.has_value());
    const auto environment_path = load_result.environment->environment_path;
    std::filesystem::create_directories(environment_path.parent_path());
    writeTextFile(environment_path, "not a directory");

    ExtensionEnvironmentManager environment_manager;
    std::string error;
    EXPECT_FALSE(environment_manager.beginProvisioning(*load_result.environment, &error));
    EXPECT_NE(error.find("Failed to create environment directory"), std::string::npos);
    EXPECT_NE(error.find(environment_path.generic_string()), std::string::npos);
    EXPECT_NE(error.find("error='"), std::string::npos);
    EXPECT_NE(error.find("code="), std::string::npos);
}

// Provisioning clears what an environment directory held before, read-only files included.
TEST(ExtensionManagerTest, BeginProvisioningClearsExistingEnvironmentContentsRobustly)
{
    ScopedTempDirectory temp_dir;
    const auto package_root = createExtensionPackage(temp_dir.path(), "env_clear_ext",
                                                     R"json({
          "id": "env_clear_ext",
          "name": "Environment Clear Extension",
          "version": "1.0.0",
          "type": "python",
          "entry_point": "env_clear_ext",
          "process_class": "EnvironmentClearExtension",
          "category": "filters",
          "launch_mode": "single_image",
          "package_format_version": 1,
          "protocol_version": 1,
          "sdk_version": 1
        })json");

    ExtensionDiscoveryContext context = defaultContext();
    context.environments_root = temp_dir.path() / "ExtensionEnvs";

    const auto load_result = an::ui::extensions::load_extension_manifest_file(package_root / "extension.json",
                                                                              ExtensionPackageSource::User, context);
    ASSERT_TRUE(load_result.environment.has_value());
    const auto environment_path = load_result.environment->environment_path;
    const auto stale_file = environment_path / "Lib" / "site-packages" / "stale.py";
    writeTextFile(stale_file, "VALUE = 1\n");

    std::error_code ec;
    std::filesystem::permissions(stale_file, std::filesystem::perms::owner_write, std::filesystem::perm_options::remove,
                                 ec);

    ExtensionEnvironmentManager environment_manager;
    std::string error;
    EXPECT_TRUE(environment_manager.beginProvisioning(*load_result.environment, &error)) << error;
    EXPECT_TRUE(error.empty());
    EXPECT_FALSE(std::filesystem::exists(stale_file));
    EXPECT_TRUE(std::filesystem::exists(environment_path / ".provisioning.lock"));
}

// Removing an environment deletes its whole nested tree.
TEST(ExtensionManagerTest, RemoveEnvironmentDirectoryDeletesNestedEnvironmentTree)
{
    ScopedTempDirectory temp_dir;
    const auto environment_path =
        temp_dir.path() / "ExtensionEnvs" / "demo" / "1.0.0" / "p-test" / currentRuntimeTarget();
    writeTextFile(environment_path / "Lib" / "site-packages" / "demo.py", "VALUE = 1\n");
    writeTextFile(environment_path / "Scripts" / "python.exe", "not really python\n");

    ExtensionEnvironmentManager environment_manager;
    std::string error;
    EXPECT_TRUE(environment_manager.removeEnvironmentDirectory(environment_path, &error)) << error;
    EXPECT_TRUE(error.empty());
    EXPECT_FALSE(std::filesystem::exists(environment_path));
}

// The validation fingerprint moves when a binary vendor artifact changes.
TEST(ExtensionManagerTest, ValidationFingerprintChangesWhenBinaryVendorArtifactsChange)
{
    ScopedTempDirectory temp_dir;
    const auto package_root = createExtensionPackage(temp_dir.path(), "binary_vendor_ext", std::string(R"json({
          "id": "binary_vendor_ext",
          "name": "Binary Vendor Extension",
          "version": "1.0.0",
          "type": "python",
          "entry_point": "binary_vendor_ext.entry",
          "process_class": "BinaryVendorExtension",
          "category": "filters",
          "launch_mode": "single_image",
          "package_format_version": 1,
          "protocol_version": 1,
          "sdk_version": 1,
          "runtime_targets": [")json") + currentRuntimeTarget() + R"json("],
          "binary_vendor_paths": ["bin"]
        })json");
    writeTextFile(package_root / "binary_vendor_ext" / "__init__.py", "");
    writeTextFile(package_root / "binary_vendor_ext" / "entry.py", "VALUE = 1\n");
    writeTextFile(package_root / "bin" / "helper.bin", "helper-v1");

    ExtensionDiscoveryContext context = defaultContext();
    context.environments_root = temp_dir.path() / "ExtensionEnvs";

    const auto initial = an::ui::extensions::load_extension_manifest_file(package_root / "extension.json",
                                                                          ExtensionPackageSource::User, context);
    ASSERT_TRUE(initial.environment.has_value());

    writeInstallReceipt(initial.environment->environment_path, initial.environment->host_bootstrap_fingerprint,
                        initial.environment->dependency_fingerprint, initial.environment->validation_fingerprint);

    writeTextFile(package_root / "bin" / "helper.bin", "helper-v2");

    // A vendored binary lives in the package, not in the environment, which keeps its place and is refreshed for it.
    const auto changed = an::ui::extensions::load_extension_manifest_file(package_root / "extension.json",
                                                                          ExtensionPackageSource::User, context);
    ASSERT_TRUE(changed.environment.has_value());
    EXPECT_NE(initial.environment->validation_fingerprint, changed.environment->validation_fingerprint);
    EXPECT_EQ(initial.environment->environment_path, changed.environment->environment_path);
    EXPECT_EQ(changed.environment->state, ExtensionEnvironmentState::Stale);
    EXPECT_TRUE(changed.environment->refreshable);
}

// A package with binary vendor paths must name its runtime targets.
TEST(ExtensionManagerTest, BinaryVendorPathsRequireRuntimeTargets)
{
    ScopedTempDirectory temp_dir;
    const auto package_root = createExtensionPackage(temp_dir.path(), "helper_ext",
                                                     R"json({
          "id": "helper_ext",
          "name": "Helper Extension",
          "version": "1.0.0",
          "type": "python",
          "entry_point": "helper_ext.entry",
          "process_class": "HelperExtension",
          "category": "filters",
          "launch_mode": "single_image",
          "package_format_version": 1,
          "protocol_version": 1,
          "sdk_version": 1,
          "binary_vendor_paths": ["bin"]
        })json");
    writeTextFile(package_root / "bin" / "helper.bin", "helper");

    const auto result = an::ui::extensions::load_extension_manifest_file(
        package_root / "extension.json", ExtensionPackageSource::User, defaultContext());

    EXPECT_EQ(result.status, ExtensionLoadStatus::Invalid);
    EXPECT_NE(result.diagnostic.find("runtime_targets"), std::string::npos);
}

// A package-local shared library outside the binary vendor paths is refused.
TEST(ExtensionManagerTest, PackageLocalSharedLibrariesOutsideBinaryVendorPathsAreRejected)
{
    ScopedTempDirectory temp_dir;
    const auto package_root = createExtensionPackage(temp_dir.path(), "loose_native_ext",
                                                     R"json({
          "id": "loose_native_ext",
          "name": "Loose Native Extension",
          "version": "1.0.0",
          "type": "python",
          "entry_point": "loose_native_ext.entry",
          "process_class": "LooseNativeExtension",
          "category": "filters",
          "launch_mode": "single_image",
          "package_format_version": 1,
          "protocol_version": 1,
          "sdk_version": 1
        })json");
    writeTextFile(package_root / "loose_native_ext" / "native_module.so", "stub");

    const auto result = an::ui::extensions::load_extension_manifest_file(
        package_root / "extension.json", ExtensionPackageSource::User, defaultContext());

    EXPECT_EQ(result.status, ExtensionLoadStatus::Invalid);
    EXPECT_NE(result.diagnostic.find("shared librar"), std::string::npos);
}

// An environment whose receipt names another build of the application keeps its place and needs only a refresh: its
// process is not launchable until then, and says the refresh installs nothing again.
TEST(ExtensionManagerTest, AnEnvironmentPreparedForAnotherBuildIsStaleButRefreshable)
{
    ScopedTempDirectory temp_dir;
    const auto package_root = createExtensionPackage(temp_dir.path(), "stale_ext",
                                                     R"json({
          "id": "stale_ext",
          "name": "Stale Extension",
          "version": "1.0.0",
          "type": "python",
          "entry_point": "stale_ext.entry",
          "process_class": "StaleExtension",
          "category": "filters",
          "launch_mode": "single_image",
          "package_format_version": 1,
          "protocol_version": 1,
          "sdk_version": 1
        })json");

    ExtensionDiscoveryContext context = defaultContext();
    context.environments_root = temp_dir.path() / "ExtensionEnvs";

    const auto initial = an::ui::extensions::load_extension_manifest_file(package_root / "extension.json",
                                                                          ExtensionPackageSource::User, context);
    ASSERT_TRUE(initial.environment.has_value());

    writeInstallReceipt(initial.environment->environment_path, "old-host-bootstrap",
                        initial.environment->dependency_fingerprint);

    const auto stale = an::ui::extensions::load_extension_manifest_file(package_root / "extension.json",
                                                                        ExtensionPackageSource::User, context);
    ASSERT_TRUE(stale.environment.has_value());
    EXPECT_EQ(stale.environment->state, ExtensionEnvironmentState::Stale);
    EXPECT_TRUE(stale.environment->refreshable);
    EXPECT_EQ(stale.environment->environment_path, initial.environment->environment_path);

    // The built-in registry is put back when the case ends, passing or failing: later cases launch through it.
    const an::tests::processes::shared::RegistrySnapshot registrySnapshot;
    ProcessRegistry& registry = ProcessRegistry::instance();
    registry.clear();
    ExtensionManager manager;
    manager.registerDiscoveredProcesses({stale}, registry);

    const auto* descriptor = registry.find("stale_ext");
    ASSERT_NE(descriptor, nullptr);
    EXPECT_EQ(descriptor->availability, ProcessAvailability::RepairNeeded);
    EXPECT_NE(descriptor->availability_reason.find("refreshed in place"), std::string::npos);
    EXPECT_NE(descriptor->availability_reason.find("without installing anything again"), std::string::npos);

    registry.clear();
}

namespace
{

/// A runtime root holding the interface packages every environment copies, small enough to copy in a case.
std::filesystem::path writeSmallRuntimeRoot(const std::filesystem::path& root)
{
    const auto runtime_root = root / "PythonLib";
    const auto site_packages = runtimeSitePackagesPathForTest(runtime_root);
    writeTextFile(site_packages / "PySide6" / "__init__.py", "VERSION = 1\n");
    writeTextFile(site_packages / "shiboken6" / "__init__.py", "VERSION = 1\n");
    return runtime_root;
}

/// The manifest of a package with no dependencies of its own, under `id`.
std::string dependencyFreeManifest(const std::string& id, const std::string& version)
{
    return R"json({
          "id": ")json" +
           id + R"json(",
          "name": "Environment Case Extension",
          "version": ")json" +
           version + R"json(",
          "type": "python",
          "entry_point": ")json" +
           id + R"json(.entry",
          "process_class": "EnvironmentCaseExtension",
          "category": "filters",
          "launch_mode": "single_image",
          "package_format_version": 1,
          "protocol_version": 1,
          "sdk_version": 1
        })json";
}

/// What a receipt says as a JSON object.
QJsonObject receiptOf(const std::filesystem::path& environment_path)
{
    return loadJsonFile(environment_path / "install_receipt.json");
}

} // namespace

// What names an environment is what it installs alone: another build of the application, a patch release of its
// Python, a new version of the package and new sources keep it, and another lock or another Python minor do not.
TEST(ExtensionManagerTest, AnEnvironmentIsNamedByWhatItInstallsAlone)
{
    ScopedTempDirectory temp_dir;
    const auto package_root = temp_dir.path() / "package";
    writeTextFile(package_root / "requirements.lock",
                  "demo-dependency==1.0.0 --hash=sha256:" + std::string(64, 'a') + "\n");
    writeTextFile(package_root / "named_ext" / "entry.py", "VALUE = 1\n");

    ExtensionManifest manifest;
    manifest.id = "named_ext";
    manifest.version = "1.0.0";
    manifest.package_root = package_root.generic_string();
    manifest.dependencies.requirements_file = "requirements.lock";
    manifest.dependencies.pip.require_hashes = true;

    ExtensionDiscoveryContext context = defaultContext();
    context.environments_root = temp_dir.path() / "ExtensionEnvs";

    ExtensionEnvironmentManager environment_manager;
    const auto path_of = [&environment_manager](const ExtensionManifest& named,
                                                const ExtensionDiscoveryContext& in_context) {
        return environment_manager.inspectEnvironment(named, ExtensionPackageSource::User, in_context).environment_path;
    };
    const auto original = path_of(manifest, context);
    EXPECT_EQ(original.parent_path().parent_path(), context.environments_root / "named_ext");
    EXPECT_EQ(original.parent_path().filename().string().rfind("e-", 0), 0u) << original;
    EXPECT_EQ(original.filename().string(), currentRuntimeTarget());

    ExtensionDiscoveryContext another_build = context;
    another_build.host_bootstrap_fingerprint = "another-build";
    another_build.app_version = "10.0.0";
    EXPECT_EQ(path_of(manifest, another_build), original);

    ExtensionDiscoveryContext patched_python = context;
    patched_python.bundled_python_version = "3.14.9";
    EXPECT_EQ(path_of(manifest, patched_python), original);

    ExtensionManifest next_version = manifest;
    next_version.version = "1.0.1";
    writeTextFile(package_root / "named_ext" / "entry.py", "VALUE = 2\n");
    EXPECT_EQ(path_of(next_version, context), original);

    ExtensionDiscoveryContext next_python = context;
    next_python.bundled_python_version = "3.15.0";
    EXPECT_NE(path_of(manifest, next_python), original);

    writeTextFile(package_root / "requirements.lock",
                  "demo-dependency==1.0.1 --hash=sha256:" + std::string(64, 'b') + "\n");
    EXPECT_NE(path_of(manifest, context), original);
}

// An environment a new build of the application finds is refreshed where it is: what it installed stays, and what it
// copies from the application is copied again only when the application's copies changed.
TEST(ExtensionManagerTest, AnEnvironmentPreparedForAnotherBuildIsRefreshedInPlaceWithoutInstallingAgain)
{
    ScopedTempDirectory temp_dir;
    const auto runtime_root = writeSmallRuntimeRoot(temp_dir.path());
    const auto package_root =
        createExtensionPackage(temp_dir.path(), "refresh_ext", dependencyFreeManifest("refresh_ext"));
    writeTextFile(package_root / "refresh_ext" / "entry.py", "VALUE = 1\n");

    ExtensionDiscoveryContext first_build = defaultContext();
    first_build.runtime_root = runtime_root;
    first_build.environments_root = temp_dir.path() / "ExtensionEnvs";
    first_build.host_bootstrap_fingerprint = "first-build";

    const auto initial = an::ui::extensions::load_extension_manifest_file(package_root / "extension.json",
                                                                          ExtensionPackageSource::User, first_build);
    ASSERT_TRUE(initial.manifest.has_value());
    ExtensionEnvironmentManager environment_manager;
    ExtensionEnvironmentSnapshot provisioned;
    std::string error;
    ASSERT_TRUE(environment_manager.provisionEnvironmentForManifest(*initial.manifest, ExtensionPackageSource::User,
                                                                    first_build, &provisioned, nullptr, &error))
        << error;
    ASSERT_EQ(provisioned.state, ExtensionEnvironmentState::Ready);

    // What the environment installed, and a mark inside what it copied from the application.
    const auto site_packages = runtimeSitePackagesPathForTest(provisioned.environment_path);
    const auto installed = site_packages / "installed_dependency" / "__init__.py";
    const auto copied_mark = site_packages / "PySide6" / "copied_mark.txt";
    writeTextFile(installed, "VALUE = 1\n");
    writeTextFile(copied_mark, "copied once\n");

    ExtensionDiscoveryContext next_build = first_build;
    next_build.host_bootstrap_fingerprint = "next-build";
    const auto stale = an::ui::extensions::load_extension_manifest_file(package_root / "extension.json",
                                                                        ExtensionPackageSource::User, next_build);
    ASSERT_TRUE(stale.environment.has_value());
    EXPECT_EQ(stale.environment->environment_path, provisioned.environment_path);
    EXPECT_EQ(stale.environment->state, ExtensionEnvironmentState::Stale);
    EXPECT_TRUE(stale.environment->refreshable);

    ExtensionEnvironmentSnapshot refreshed;
    ASSERT_TRUE(environment_manager.provisionEnvironmentForManifest(*stale.manifest, ExtensionPackageSource::User,
                                                                    next_build, &refreshed, nullptr, &error))
        << error;
    EXPECT_EQ(refreshed.state, ExtensionEnvironmentState::Ready);
    EXPECT_EQ(refreshed.environment_path, provisioned.environment_path);
    EXPECT_TRUE(std::filesystem::exists(installed));
    EXPECT_TRUE(std::filesystem::exists(copied_mark)) << "the application's copies did not change";
    EXPECT_EQ(receiptOf(refreshed.environment_path).value(QStringLiteral("host_bootstrap_fingerprint")).toString(),
              QStringLiteral("next-build"));

    // The application's copy of a runtime package changes with the next build: the refresh copies it again.
    writeTextFile(runtimeSitePackagesPathForTest(runtime_root) / "PySide6" / "__init__.py", "VERSION = 2\n");
    ExtensionDiscoveryContext third_build = first_build;
    third_build.host_bootstrap_fingerprint = "third-build";
    const auto stale_again = an::ui::extensions::load_extension_manifest_file(
        package_root / "extension.json", ExtensionPackageSource::User, third_build);
    ASSERT_TRUE(stale_again.environment.has_value());
    ASSERT_TRUE(stale_again.environment->refreshable);
    ASSERT_TRUE(environment_manager.refreshEnvironment(*stale_again.manifest, *stale_again.environment, &error))
        << error;
    EXPECT_EQ(readTextFile(site_packages / "PySide6" / "__init__.py"), "VERSION = 2\n");
    EXPECT_FALSE(std::filesystem::exists(copied_mark));
    EXPECT_TRUE(std::filesystem::exists(installed));
    EXPECT_FALSE(std::filesystem::exists(refreshed.environment_path / ".provisioning.lock"));

    const auto ready = an::ui::extensions::load_extension_manifest_file(package_root / "extension.json",
                                                                        ExtensionPackageSource::User, third_build);
    ASSERT_TRUE(ready.environment.has_value());
    EXPECT_EQ(ready.environment->state, ExtensionEnvironmentState::Ready);
}

namespace
{

/// A package whose lock pins one dependency, which no wheelhouse holds: only a taken-over environment can provide it.
std::filesystem::path createLockedPackage(const std::filesystem::path& root, const std::string& id)
{
    const auto package_root = createExtensionPackage(root, id, std::string(R"json({
          "id": ")json") + id + R"json(",
          "name": "Locked Extension",
          "version": "1.0.0",
          "type": "python",
          "entry_point": ")json" + id + R"json(.entry",
          "process_class": "LockedExtension",
          "category": "filters",
          "launch_mode": "single_image",
          "package_format_version": 1,
          "protocol_version": 1,
          "sdk_version": 1,
          "dependencies": {
            "requirements_file": "requirements.lock",
            "pip": {
              "require_hashes": true
            }
          }
        })json");
    writeTextFile(package_root / "requirements.lock",
                  "demo-dependency==1.0.0 --hash=sha256:" + std::string(64, 'a') + "\n");
    return package_root;
}

/// An environment an earlier layout left: under a version and the fingerprints of the build it was made in, with what
/// its receipt says it installed and the Python version its pyvenv.cfg names.
void writeEarlierEnvironment(const std::filesystem::path& environment_path, const std::string& python_version,
                             const std::string& installed_distributions)
{
    writeTextFile(environment_path / "pyvenv.cfg",
                  "home = earlier\ninclude-system-site-packages = false\nversion = " + python_version + "\n");
    writeTextFile(runtimeSitePackagesPathForTest(environment_path) / "demo_dependency" / "__init__.py", "VALUE = 1\n");
    writeTextFile(environment_path / "install_receipt.json",
                  R"json({
  "host_bootstrap_fingerprint": "earlier-build",
  "dependency_fingerprint": "earlier-dependencies",
  "resolved_distributions": [
    {"name": "PySide6", "version": "6.10.3", "source": "bundled-runtime", "artifact_hash": "host-managed"},
    )json" + installed_distributions +
                      R"json(
  ]
})json");
    writeTextFile(environment_path / ".environment_ready", "ready\n");
}

const std::string kPinnedDistribution =
    R"json({"name": "Demo_Dependency", "version": "1.0.0", "source": "https://example.invalid/demo_dependency.whl", )json"
    R"json("artifact_hash": ")json" +
    std::string(64, 'a') + R"json("})json";

} // namespace

// The environment an earlier layout left holding what the lock pins is taken over where the new one goes: nothing is
// installed - no wheelhouse holds the dependency - and the place it was in is removed.
TEST(ExtensionManagerTest, AnEnvironmentAnEarlierLayoutLeftHoldingWhatTheLockPinsIsTakenOver)
{
    ScopedTempDirectory temp_dir;
    const auto runtime_root = writeSmallRuntimeRoot(temp_dir.path());
    const auto package_root = createLockedPackage(temp_dir.path(), "adopt_ext");

    ExtensionDiscoveryContext context = defaultContext();
    context.runtime_root = runtime_root;
    context.environments_root = temp_dir.path() / "ExtensionEnvs";

    const auto loaded = an::ui::extensions::load_extension_manifest_file(package_root / "extension.json",
                                                                         ExtensionPackageSource::User, context);
    ASSERT_TRUE(loaded.environment.has_value());
    ASSERT_EQ(loaded.environment->state, ExtensionEnvironmentState::Missing);

    const auto earlier = (context.environments_root / "adopt_ext" / "0.9.0" / "p-0123456789abcdef-0123456789abcdef" /
                          currentRuntimeTarget())
                             .lexically_normal();
    writeEarlierEnvironment(earlier, context.bundled_python_version, kPinnedDistribution);

    ExtensionEnvironmentManager environment_manager;
    EXPECT_EQ(environment_manager.findAdoptableEnvironment(*loaded.manifest, *loaded.environment), earlier);

    ExtensionEnvironmentSnapshot adopted;
    std::string error;
    ASSERT_TRUE(environment_manager.provisionEnvironmentForManifest(*loaded.manifest, ExtensionPackageSource::User,
                                                                    context, &adopted, nullptr, &error))
        << error;
    EXPECT_EQ(adopted.state, ExtensionEnvironmentState::Ready);
    EXPECT_EQ(adopted.environment_path, loaded.environment->environment_path);
    EXPECT_TRUE(std::filesystem::exists(runtimeSitePackagesPathForTest(adopted.environment_path) / "demo_dependency" /
                                        "__init__.py"));
    EXPECT_TRUE(
        std::filesystem::exists(runtimeSitePackagesPathForTest(adopted.environment_path) / "PySide6" / "__init__.py"));
    EXPECT_FALSE(std::filesystem::exists(earlier));
    EXPECT_FALSE(std::filesystem::exists(context.environments_root / "adopt_ext" / "0.9.0"));
    const QJsonObject receipt = receiptOf(adopted.environment_path);
    EXPECT_EQ(receipt.value(QStringLiteral("dependency_fingerprint")).toString().toStdString(),
              adopted.dependency_fingerprint);
    EXPECT_EQ(receipt.value(QStringLiteral("host_bootstrap_fingerprint")).toString().toStdString(),
              adopted.host_bootstrap_fingerprint);
}

// In a group the packages share, taking an environment over moves it: one another package of the group still resolves
// to is never taken over, which would leave that package without its environment and the two moving it back and forth;
// nor is any while it is not known which the packages resolve to. Once no package resolves to it, it is taken over.
TEST(ExtensionManagerTest, AnEnvironmentAnotherPackageOfTheGroupResolvesToIsNotTakenOver)
{
    ScopedTempDirectory temp_dir;
    const auto runtime_root = writeSmallRuntimeRoot(temp_dir.path());
    const auto sharedPackage = [&](const std::string& id, const std::string& pip) {
        const auto package_root = createExtensionPackage(temp_dir.path(), id, std::string(R"json({
          "id": ")json") + id + R"json(",
          "name": "Shared Locked Extension",
          "version": "1.0.0",
          "type": "python",
          "entry_point": ")json" + id + R"json(.entry",
          "process_class": "SharedLockedExtension",
          "category": "filters",
          "launch_mode": "single_image",
          "package_format_version": 1,
          "protocol_version": 1,
          "sdk_version": 1,
          "dependencies": {
            "requirements_file": "requirements.lock",
            "dependency_context": "shared_group",
            "shared_group": "adoption_suite",
            "pip": )json" + pip + R"json(
          }
        })json");
        writeTextFile(package_root / "requirements.lock",
                      "demo-dependency==1.0.0 --hash=sha256:" + std::string(64, 'a') + "\n");
        return package_root;
    };
    const auto first_root = sharedPackage("shared_adopt_a", R"json({"require_hashes": true})json");
    const auto second_root = sharedPackage(
        "shared_adopt_b", R"json({"require_hashes": true, "index_urls": ["https://example.invalid/simple"]})json");

    ExtensionDiscoveryContext context = defaultContext();
    context.runtime_root = runtime_root;
    context.environments_root = temp_dir.path() / "ExtensionEnvs";
    const auto first = an::ui::extensions::load_extension_manifest_file(first_root / "extension.json",
                                                                        ExtensionPackageSource::User, context);
    const auto second = an::ui::extensions::load_extension_manifest_file(second_root / "extension.json",
                                                                         ExtensionPackageSource::User, context);
    ASSERT_TRUE(first.environment.has_value());
    ASSERT_TRUE(second.environment.has_value());
    const std::filesystem::path firstEnvironment = first.environment->environment_path.lexically_normal();
    const std::filesystem::path secondEnvironment = second.environment->environment_path.lexically_normal();
    ASSERT_NE(firstEnvironment, secondEnvironment) << "the packages' fingerprints differ, so their environments do";
    writeEarlierEnvironment(firstEnvironment, context.bundled_python_version, kPinnedDistribution);

    ExtensionEnvironmentManager environment_manager;
    EXPECT_TRUE(environment_manager.findAdoptableEnvironment(*second.manifest, *second.environment).empty())
        << "it is not known which environments the packages resolve to";

    environment_manager.setDiscoveredEnvironments({firstEnvironment, secondEnvironment});
    EXPECT_TRUE(environment_manager.findAdoptableEnvironment(*second.manifest, *second.environment).empty())
        << "the first package still resolves to its environment";

    environment_manager.setDiscoveredEnvironments({secondEnvironment});
    EXPECT_EQ(environment_manager.findAdoptableEnvironment(*second.manifest, *second.environment), firstEnvironment)
        << "no package resolves to it any more";
}

// An environment built over another Python minor, holding another version of a pinned dependency or a distribution
// the lock does not name, or one being provisioned, is not taken over.
TEST(ExtensionManagerTest, AnEnvironmentOfAnotherPythonOrAnotherLockIsNotTakenOver)
{
    ScopedTempDirectory temp_dir;
    const auto package_root = createLockedPackage(temp_dir.path(), "refuse_ext");

    ExtensionDiscoveryContext context = defaultContext();
    context.environments_root = temp_dir.path() / "ExtensionEnvs";
    const auto loaded = an::ui::extensions::load_extension_manifest_file(package_root / "extension.json",
                                                                         ExtensionPackageSource::User, context);
    ASSERT_TRUE(loaded.environment.has_value());

    ExtensionEnvironmentManager environment_manager;
    const auto earlier = context.environments_root / "refuse_ext" / "0.9.0" / "p-earlier" / currentRuntimeTarget();
    const auto adoptable = [&]() {
        return environment_manager.findAdoptableEnvironment(*loaded.manifest, *loaded.environment);
    };

    writeEarlierEnvironment(earlier, "3.13.2", kPinnedDistribution);
    EXPECT_TRUE(adoptable().empty()) << "another Python minor";

    writeEarlierEnvironment(earlier, context.bundled_python_version,
                            R"json({"name": "demo-dependency", "version": "0.9.0", "source": "pypi", )json"
                            R"json("artifact_hash": ""})json");
    EXPECT_TRUE(adoptable().empty()) << "another version of the pinned dependency";

    writeEarlierEnvironment(earlier, context.bundled_python_version, kPinnedDistribution + R"json(,
    {"name": "unpinned", "version": "2.0.0", "source": "pypi", "artifact_hash": ""})json");
    EXPECT_TRUE(adoptable().empty()) << "a distribution the lock does not name";

    writeEarlierEnvironment(earlier, context.bundled_python_version, kPinnedDistribution);
    EXPECT_EQ(adoptable(), earlier.lexically_normal());
    const LiveOtherProcess provisioner;
    ASSERT_TRUE(provisioner.started());
    writeTextFile(earlier / ".provisioning.lock", provisioner.pidLine());
    EXPECT_TRUE(adoptable().empty()) << "an environment being provisioned";
}

// The environments a ready one of the same context replaced are removed at once, without the grace period automatic
// cleanup gives one no installed package's context names; nothing is removed while the replacement is not ready.
TEST(ExtensionManagerTest, AReplacedEnvironmentIsRemovedOnceItsReplacementIsReady)
{
    ScopedTempDirectory temp_dir;
    const auto user_root = temp_dir.path() / "user";
    const auto package_root = createExtensionPackage(user_root, "replaced_ext", dependencyFreeManifest("replaced_ext"));

    ExtensionDiscoveryOptions options;
    options.user_roots = {user_root};
    options.context = defaultContext();
    options.context.environments_root = temp_dir.path() / "ExtensionEnvs";

    const auto current = an::ui::extensions::load_extension_manifest_file(
        package_root / "extension.json", ExtensionPackageSource::User, options.context);
    ASSERT_TRUE(current.environment.has_value());
    const auto replaced =
        options.context.environments_root / "replaced_ext" / "1.0.0" / "p-replaced" / currentRuntimeTarget();
    writeTextFile(replaced / "Lib" / "site-packages" / "payload.bin", std::string(1024, 'r'));
    const auto uninstalled =
        options.context.environments_root / "uninstalled_ext" / "1.0.0" / "p-uninstalled" / currentRuntimeTarget();
    writeTextFile(uninstalled / "Lib" / "site-packages" / "payload.bin", std::string(1024, 'u'));

    ExtensionManager discovery_manager;
    ExtensionEnvironmentManager environment_manager;
    std::vector<std::filesystem::path> removed;
    ASSERT_TRUE(environment_manager.removeReplacedEnvironments(discovery_manager.discoverExtensions(options),
                                                               options.context, &removed));
    EXPECT_TRUE(removed.empty()) << "the replacement is not ready";
    EXPECT_TRUE(std::filesystem::exists(replaced));

    writeInstallReceipt(current.environment->environment_path, current.environment->host_bootstrap_fingerprint,
                        current.environment->dependency_fingerprint, current.environment->validation_fingerprint);
    std::uintmax_t reclaimed = 0;
    ASSERT_TRUE(environment_manager.removeReplacedEnvironments(discovery_manager.discoverExtensions(options),
                                                               options.context, &removed, &reclaimed));
    EXPECT_EQ(removed, std::vector<std::filesystem::path>{replaced});
    EXPECT_GE(reclaimed, 1024u);
    EXPECT_FALSE(std::filesystem::exists(options.context.environments_root / "replaced_ext" / "1.0.0"));
    EXPECT_TRUE(std::filesystem::exists(current.environment->environment_path));
    EXPECT_TRUE(std::filesystem::exists(uninstalled)) << "no installed package's context names it";
}

// Startup maintenance prepares a package of any source: it provisions a missing environment, refreshes one another
// build left without installing again, and leaves one that installs from an index for when the network may be reached.
TEST(ExtensionManagerTest, EnvironmentMaintenancePreparesEveryInstalledPackage)
{
    ScopedTempDirectory temp_dir;
    const auto runtime_root = writeSmallRuntimeRoot(temp_dir.path());
    const auto package_root =
        createExtensionPackage(temp_dir.path(), "maintained_ext", dependencyFreeManifest("maintained_ext"));

    ExtensionDiscoveryContext context = defaultContext();
    context.runtime_root = runtime_root;
    context.environments_root = temp_dir.path() / "ExtensionEnvs";
    context.host_bootstrap_fingerprint = "first-build";

    ExtensionEnvironmentManager environment_manager;
    std::vector discovered = {an::ui::extensions::load_extension_manifest_file(package_root / "extension.json",
                                                                               ExtensionPackageSource::User, context)};
    ASSERT_EQ(environment_manager.queueEnvironmentMaintenance(discovered, false), 1);
    EXPECT_EQ(discovered.front().environment->state, ExtensionEnvironmentState::Bootstrapping);
    std::string error;
    ASSERT_EQ(environment_manager.processQueuedEnvironmentMaintenance(discovered, &error), 1) << error;
    ASSERT_EQ(discovered.front().environment->state, ExtensionEnvironmentState::Ready);

    const auto installed =
        runtimeSitePackagesPathForTest(discovered.front().environment->environment_path) / "kept" / "__init__.py";
    writeTextFile(installed, "VALUE = 1\n");

    ExtensionDiscoveryContext next_build = context;
    next_build.host_bootstrap_fingerprint = "next-build";
    discovered = {an::ui::extensions::load_extension_manifest_file(package_root / "extension.json",
                                                                   ExtensionPackageSource::User, next_build)};
    ASSERT_TRUE(discovered.front().environment->refreshable);
    ASSERT_EQ(environment_manager.queueEnvironmentMaintenance(discovered, false), 1);
    EXPECT_FALSE(std::filesystem::exists(discovered.front().environment->environment_path / ".provisioning.lock"))
        << "a refresh is queued without clearing anything";
    ASSERT_EQ(environment_manager.processQueuedEnvironmentMaintenance(discovered, &error), 1) << error;
    EXPECT_EQ(discovered.front().environment->state, ExtensionEnvironmentState::Ready);
    EXPECT_TRUE(std::filesystem::exists(installed));

    const auto index_package_root = createExtensionPackage(temp_dir.path(), "index_ext", R"json({
          "id": "index_ext",
          "name": "Index Extension",
          "version": "1.0.0",
          "type": "python",
          "entry_point": "index_ext.entry",
          "process_class": "IndexExtension",
          "category": "filters",
          "launch_mode": "single_image",
          "package_format_version": 1,
          "protocol_version": 1,
          "sdk_version": 1,
          "dependencies": {
            "requirements_file": "requirements.lock",
            "pip": {
              "require_hashes": true,
              "index_urls": ["https://pypi.org/simple"]
            }
          }
        })json");
    writeTextFile(index_package_root / "requirements.lock",
                  "demo-dependency==1.0.0 --hash=sha256:" + std::string(64, 'a') + "\n");
    std::vector index_discovered = {an::ui::extensions::load_extension_manifest_file(
        index_package_root / "extension.json", ExtensionPackageSource::Repository, context)};
    ASSERT_TRUE(index_discovered.front().environment.has_value());
    EXPECT_EQ(environment_manager.queueEnvironmentMaintenance(index_discovered, false), 0);
    EXPECT_EQ(index_discovered.front().environment->state, ExtensionEnvironmentState::Missing);
}

// Queuing an environment for maintenance takes its provisioning lock for this process. When the processing stops before
// it reaches the environment - stopped, or a failure before it - the lock is let go and the environment is no longer
// said to be prepared, so a launch prepares it at once: it waited instead, for a lock its own process held, until the
// person cancelled.
TEST(ExtensionManagerTest, MaintenanceThatStopsBeforeAnEnvironmentLetsItsLockGo)
{
    ScopedTempDirectory temp_dir;
    const auto runtime_root = writeSmallRuntimeRoot(temp_dir.path());
    const auto first_root =
        createExtensionPackage(temp_dir.path(), "stopped_first_ext", dependencyFreeManifest("stopped_first_ext"));
    const auto second_root =
        createExtensionPackage(temp_dir.path(), "stopped_second_ext", dependencyFreeManifest("stopped_second_ext"));

    ExtensionDiscoveryContext context = defaultContext();
    context.runtime_root = runtime_root;
    context.environments_root = temp_dir.path() / "ExtensionEnvs";

    ExtensionEnvironmentManager environment_manager;
    std::vector discovered = {an::ui::extensions::load_extension_manifest_file(first_root / "extension.json",
                                                                               ExtensionPackageSource::User, context),
                              an::ui::extensions::load_extension_manifest_file(second_root / "extension.json",
                                                                               ExtensionPackageSource::User, context)};
    ASSERT_EQ(environment_manager.queueEnvironmentMaintenance(discovered, false), 2);
    for (const auto& result : discovered)
    {
        ASSERT_TRUE(std::filesystem::exists(result.environment->environment_path / ".provisioning.lock"));
    }

    ExtensionOperationCancellationToken stopped;
    stopped.requestCancellation();
    std::string error;
    EXPECT_EQ(environment_manager.processQueuedEnvironmentMaintenance(discovered, &error, stopped), 0);
    for (const auto& result : discovered)
    {
        ASSERT_FALSE(std::filesystem::exists(result.environment->environment_path / ".provisioning.lock"))
            << result.manifest->id;
        EXPECT_EQ(result.environment->state, ExtensionEnvironmentState::Missing) << result.manifest->id;
    }

    // A launch prepares the environment at once rather than wait for the lock.
    ExtensionEnvironmentSnapshot prepared;
    ASSERT_TRUE(environment_manager.prepareEnvironmentForLaunch(
        *discovered.back().manifest, ExtensionPackageSource::User, context, &prepared, &error))
        << error;
    EXPECT_EQ(prepared.state, ExtensionEnvironmentState::Ready);
}

// A launch whose refresh of an environment another build left fails for a reason that may pass - here this build's
// Python cannot be found; a file another program holds is the like - leaves the environment as it was, still to be
// refreshed, and says why. It was cleared and provisioned again, which deleted an environment that works and, offline,
// left it failed.
TEST(ExtensionManagerTest, ALaunchWhoseRefreshFailsLeavesTheEnvironmentAsItWas)
{
    ScopedTempDirectory temp_dir;
    const auto runtime_root = writeSmallRuntimeRoot(temp_dir.path());
    const auto package_root =
        createExtensionPackage(temp_dir.path(), "refresh_kept_ext", dependencyFreeManifest("refresh_kept_ext"));

    ExtensionDiscoveryContext context = defaultContext();
    context.runtime_root = runtime_root;
    context.environments_root = temp_dir.path() / "ExtensionEnvs";
    context.host_bootstrap_fingerprint = "first-build";

    ExtensionEnvironmentManager environment_manager;
    std::vector discovered = {an::ui::extensions::load_extension_manifest_file(package_root / "extension.json",
                                                                               ExtensionPackageSource::User, context)};
    ASSERT_EQ(environment_manager.queueEnvironmentMaintenance(discovered, false), 1);
    std::string error;
    ASSERT_EQ(environment_manager.processQueuedEnvironmentMaintenance(discovered, &error), 1) << error;
    ASSERT_EQ(discovered.front().environment->state, ExtensionEnvironmentState::Ready);
    const auto installed =
        runtimeSitePackagesPathForTest(discovered.front().environment->environment_path) / "kept" / "__init__.py";
    writeTextFile(installed, "VALUE = 1\n");

    ExtensionDiscoveryContext next_build = context;
    next_build.host_bootstrap_fingerprint = "next-build";
    next_build.bundled_python_executable = temp_dir.path() / "no_python_here" / "python.exe";
    const auto stale = an::ui::extensions::load_extension_manifest_file(package_root / "extension.json",
                                                                        ExtensionPackageSource::User, next_build);
    ASSERT_TRUE(stale.environment.has_value());
    ASSERT_EQ(stale.environment->state, ExtensionEnvironmentState::Stale);
    ASSERT_TRUE(stale.environment->refreshable);

    ExtensionEnvironmentSnapshot after;
    EXPECT_FALSE(environment_manager.prepareEnvironmentForLaunch(*stale.manifest, ExtensionPackageSource::User,
                                                                 next_build, &after, &error));
    EXPECT_NE(error.find("left as it was"), std::string::npos) << error;
    EXPECT_TRUE(std::filesystem::exists(installed)) << "the environment was cleared";
    EXPECT_EQ(after.state, ExtensionEnvironmentState::Stale);
    EXPECT_TRUE(after.refreshable);
}

// A launch waits for the environment another operation is preparing rather than racing it, and is stopped by its
// cancellation while it waits.
TEST(ExtensionManagerTest, PreparingAnEnvironmentForALaunchWaitsForTheOneBeingPrepared)
{
    ScopedTempDirectory temp_dir;
    const auto package_root =
        createExtensionPackage(temp_dir.path(), "waiting_ext", dependencyFreeManifest("waiting_ext"));

    ExtensionDiscoveryContext context = defaultContext();
    context.runtime_root = writeSmallRuntimeRoot(temp_dir.path());
    context.environments_root = temp_dir.path() / "ExtensionEnvs";

    const auto loaded = an::ui::extensions::load_extension_manifest_file(package_root / "extension.json",
                                                                         ExtensionPackageSource::User, context);
    ASSERT_TRUE(loaded.environment.has_value());
    ExtensionEnvironmentManager environment_manager;
    std::string error;
    ASSERT_TRUE(environment_manager.beginProvisioning(*loaded.environment, &error)) << error;

    ExtensionOperationCancellationToken cancelled;
    std::thread canceller([cancelled]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        cancelled.requestCancellation();
    });
    EXPECT_FALSE(environment_manager.prepareEnvironmentForLaunch(*loaded.manifest, ExtensionPackageSource::User,
                                                                 context, nullptr, &error, {}, cancelled));
    canceller.join();

    std::thread finisher([&environment_manager, environment = *loaded.environment]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        std::string finish_error;
        EXPECT_TRUE(environment_manager.completeProvisioning(environment, {}, &finish_error)) << finish_error;
    });
    QElapsedTimer waited;
    waited.start();
    ExtensionEnvironmentSnapshot prepared;
    EXPECT_TRUE(environment_manager.prepareEnvironmentForLaunch(*loaded.manifest, ExtensionPackageSource::User, context,
                                                                &prepared, &error))
        << error;
    finisher.join();
    EXPECT_GE(waited.elapsed(), 250);
    EXPECT_EQ(prepared.state, ExtensionEnvironmentState::Ready);
}

// A ready environment another operation is refreshing reads as being prepared, and a provisioning asked for meanwhile
// leaves it whole rather than clearing it to install it again.
TEST(ExtensionManagerTest, AnEnvironmentBeingRefreshedIsLeftToTheOperationRefreshingIt)
{
    ScopedTempDirectory temp_dir;
    const auto package_root =
        createExtensionPackage(temp_dir.path(), "refreshing_ext", dependencyFreeManifest("refreshing_ext"));

    ExtensionDiscoveryContext first_build = defaultContext();
    first_build.runtime_root = writeSmallRuntimeRoot(temp_dir.path());
    first_build.environments_root = temp_dir.path() / "ExtensionEnvs";
    first_build.host_bootstrap_fingerprint = "first-build";

    const auto initial = an::ui::extensions::load_extension_manifest_file(package_root / "extension.json",
                                                                          ExtensionPackageSource::User, first_build);
    ASSERT_TRUE(initial.manifest.has_value());
    ExtensionEnvironmentManager environment_manager;
    ExtensionEnvironmentSnapshot provisioned;
    std::string error;
    ASSERT_TRUE(environment_manager.provisionEnvironmentForManifest(*initial.manifest, ExtensionPackageSource::User,
                                                                    first_build, &provisioned, nullptr, &error))
        << error;
    const auto installed = runtimeSitePackagesPathForTest(provisioned.environment_path) / "kept" / "__init__.py";
    writeTextFile(installed, "VALUE = 1\n");

    // Another live process stands for the operation refreshing it for the next build.
    const LiveOtherProcess refresher;
    ASSERT_TRUE(refresher.started());
    const auto lock = provisioned.environment_path / ".provisioning.lock";
    writeTextFile(lock, "refresh=1\n" + refresher.pidLine());
    ExtensionDiscoveryContext next_build = first_build;
    next_build.host_bootstrap_fingerprint = "next-build";
    const auto refreshing = an::ui::extensions::load_extension_manifest_file(package_root / "extension.json",
                                                                             ExtensionPackageSource::User, next_build);
    ASSERT_TRUE(refreshing.environment.has_value());
    EXPECT_EQ(refreshing.environment->state, ExtensionEnvironmentState::Bootstrapping);

    EXPECT_FALSE(environment_manager.provisionEnvironmentForManifest(*refreshing.manifest, ExtensionPackageSource::User,
                                                                     next_build, nullptr, nullptr, &error));
    EXPECT_TRUE(std::filesystem::exists(installed));
    EXPECT_TRUE(std::filesystem::exists(provisioned.environment_path / ".environment_ready"));
    EXPECT_TRUE(std::filesystem::exists(lock));

    // Once that operation is gone, the environment is refreshed where it is.
    std::filesystem::remove(lock);
    ExtensionEnvironmentSnapshot refreshed;
    ASSERT_TRUE(environment_manager.provisionEnvironmentForManifest(*refreshing.manifest, ExtensionPackageSource::User,
                                                                    next_build, &refreshed, nullptr, &error))
        << error;
    EXPECT_EQ(refreshed.state, ExtensionEnvironmentState::Ready);
    EXPECT_TRUE(std::filesystem::exists(installed));
}

// Two preparations of one environment at once take the earlier one over once: the second waits for the first and finds
// what it left ready, and the earlier environment is locked against anything else while it is taken over.
TEST(ExtensionManagerTest, TwoPreparationsOfOneEnvironmentTakeItOverOnce)
{
    ScopedTempDirectory temp_dir;
    const auto package_root = createLockedPackage(temp_dir.path(), "racing_ext");

    ExtensionDiscoveryContext context = defaultContext();
    context.runtime_root = writeSmallRuntimeRoot(temp_dir.path());
    context.environments_root = temp_dir.path() / "ExtensionEnvs";
    const auto loaded = an::ui::extensions::load_extension_manifest_file(package_root / "extension.json",
                                                                         ExtensionPackageSource::User, context);
    ASSERT_TRUE(loaded.environment.has_value());
    const auto earlier =
        (context.environments_root / "racing_ext" / "0.9.0" / "p-earlier" / currentRuntimeTarget()).lexically_normal();
    writeEarlierEnvironment(earlier, context.bundled_python_version, kPinnedDistribution);

    ExtensionEnvironmentManager first_manager;
    ExtensionEnvironmentManager second_manager;
    std::atomic_bool taking_over = false;
    std::atomic_bool first_done = false;
    bool locked_while_taken_over = false;
    bool hidden_while_taken_over = false;
    const auto first_progress = [&](const ExtensionOperationProgress& progress) {
        if (progress.phase_id != "adopt_environment")
        {
            return;
        }
        locked_while_taken_over = std::filesystem::exists(earlier / ".provisioning.lock");
        hidden_while_taken_over =
            second_manager.findAdoptableEnvironment(*loaded.manifest, *loaded.environment).empty();
        taking_over = true;
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
    };

    std::string first_error;
    bool first_ready = false;
    std::thread first([&]() {
        first_ready = first_manager.prepareEnvironmentForLaunch(*loaded.manifest, ExtensionPackageSource::User, context,
                                                                nullptr, &first_error, first_progress);
        first_done = true;
    });
    while (!taking_over && !first_done)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    std::string second_error;
    ExtensionEnvironmentSnapshot second_snapshot;
    const bool second_ready = second_manager.prepareEnvironmentForLaunch(*loaded.manifest, ExtensionPackageSource::User,
                                                                         context, &second_snapshot, &second_error);
    first.join();

    EXPECT_TRUE(first_ready) << first_error;
    EXPECT_TRUE(second_ready) << second_error;
    EXPECT_TRUE(locked_while_taken_over);
    EXPECT_TRUE(hidden_while_taken_over);
    EXPECT_EQ(second_snapshot.state, ExtensionEnvironmentState::Ready);
    EXPECT_FALSE(std::filesystem::exists(earlier));
    EXPECT_TRUE(std::filesystem::exists(runtimeSitePackagesPathForTest(second_snapshot.environment_path) /
                                        "demo_dependency" / "__init__.py"));
}

// Maintenance queued to take an earlier environment over reads it as ready when a launch took it over first, rather
// than as missing because the earlier one is gone.
TEST(ExtensionManagerTest, MaintenanceReadsAnEnvironmentALaunchTookOverFirstAsReady)
{
    ScopedTempDirectory temp_dir;
    const auto package_root = createLockedPackage(temp_dir.path(), "overtaken_ext");

    ExtensionDiscoveryContext context = defaultContext();
    context.runtime_root = writeSmallRuntimeRoot(temp_dir.path());
    context.environments_root = temp_dir.path() / "ExtensionEnvs";
    writeEarlierEnvironment(context.environments_root / "overtaken_ext" / "0.9.0" / "p-earlier" /
                                currentRuntimeTarget(),
                            context.bundled_python_version, kPinnedDistribution);

    ExtensionEnvironmentManager environment_manager;
    std::vector discovered = {an::ui::extensions::load_extension_manifest_file(package_root / "extension.json",
                                                                               ExtensionPackageSource::User, context)};
    ASSERT_TRUE(discovered.front().manifest.has_value());
    ASSERT_EQ(environment_manager.queueEnvironmentMaintenance(discovered, false), 1);

    std::string error;
    ASSERT_TRUE(environment_manager.prepareEnvironmentForLaunch(*discovered.front().manifest,
                                                                ExtensionPackageSource::User, context, nullptr, &error))
        << error;
    ASSERT_EQ(environment_manager.processQueuedEnvironmentMaintenance(discovered, &error), 1) << error;
    EXPECT_EQ(discovered.front().environment->state, ExtensionEnvironmentState::Ready)
        << discovered.front().environment->diagnostic;
}

// A run told to reach no repository or index says so to whatever would provision from one.
TEST(ExtensionManagerTest, NetworkPolicyIsReadFromEitherOfItsVariables)
{
    {
        const TestHelpers::ScopedEnvVar disabled("AFTERNIGHT_DISABLE_EXTENSION_REPOSITORY_NETWORK", "");
        const TestHelpers::ScopedEnvVar offline("AFTERNIGHT_EXTENSION_REPOSITORY_OFFLINE", "");
        EXPECT_FALSE(an::ui::extensions::extensionNetworkDisabledByPolicy());
    }
    {
        const TestHelpers::ScopedEnvVar disabled("AFTERNIGHT_DISABLE_EXTENSION_REPOSITORY_NETWORK", " Yes ");
        EXPECT_TRUE(an::ui::extensions::extensionNetworkDisabledByPolicy());
    }
    {
        const TestHelpers::ScopedEnvVar offline("AFTERNIGHT_EXTENSION_REPOSITORY_OFFLINE", "on");
        EXPECT_TRUE(an::ui::extensions::extensionNetworkDisabledByPolicy());
    }
}

// A launch mode the format does not name is refused before anything is registered.
TEST(ExtensionManagerTest, InvalidManifestLaunchModeIsRejectedBeforeRegistration)
{
    ScopedTempDirectory temp_dir;
    const auto package_root = createExtensionPackage(temp_dir.path(), "invalid_mode",
                                                     R"json({
          "id": "invalid_mode",
          "name": "Invalid Mode",
          "version": "1.0.0",
          "type": "python",
          "entry_point": "invalid_mode.entry",
          "process_class": "InvalidModeExtension",
          "category": "filters",
          "launch_mode": "top_level",
          "package_format_version": 1,
          "protocol_version": 1,
          "sdk_version": 1
        })json");

    const auto result = an::ui::extensions::load_extension_manifest_file(
        package_root / "extension.json", ExtensionPackageSource::User, defaultContext());

    EXPECT_EQ(result.status, ExtensionLoadStatus::Invalid);
    EXPECT_FALSE(result.manifest.has_value());
    EXPECT_NE(result.diagnostic.find("launch_mode"), std::string::npos);
}

// The extensions deployed beside the application include the echo demo, ready.
TEST(ExtensionManagerTest, DeployedBundledExtensionRootDiscoversEchoDemo)
{
    const auto app_dir = std::filesystem::path(QCoreApplication::applicationDirPath().toStdString());
    const auto bundled_root = an::ui::extensions::bundled_extensions_root(app_dir);

    ensureBundledExtensionsStaged(bundled_root);

    const bool has_extensions_dir =
        std::filesystem::exists(bundled_root) && std::filesystem::is_directory(bundled_root);
    ASSERT_TRUE(has_extensions_dir) << "Deployed extensions/ root not found at: " << bundled_root
                                    << " — run the deploy script before this test";

    ExtensionDiscoveryOptions options;
    options.bundled_roots = {bundled_root};
    options.context = defaultContext();
    options.context.runtime_target = an::ui::extensions::current_runtime_target();
    options.include_development_only_extensions = true;

    ExtensionManager manager;
    const auto discovered = manager.discoverExtensions(options);
    ASSERT_FALSE(discovered.empty()) << "No extensions discovered under: " << bundled_root;

    const auto echo_it = std::find_if(discovered.begin(), discovered.end(), [](const auto& result) {
        return result.manifest.has_value() && result.manifest->id == "an.demo.echo";
    });
    ASSERT_NE(echo_it, discovered.end()) << "an.demo.echo not found among " << discovered.size()
                                         << " discovered extension(s)";
    EXPECT_EQ(echo_it->status, ExtensionLoadStatus::Ready);
    EXPECT_EQ(echo_it->source, ExtensionPackageSource::Bundled);
    EXPECT_EQ(echo_it->manifest->name, "Echo Demo");
    EXPECT_EQ(echo_it->manifest->version, "0.1.0");
}

// The deployed reference demos are discovered ready with the capabilities each declares.
TEST(ExtensionManagerTest, DeployedBundledExtensionRootDiscoversReferenceDemoExtensions)
{
    const auto app_dir = std::filesystem::path(QCoreApplication::applicationDirPath().toStdString());
    const auto bundled_root = an::ui::extensions::bundled_extensions_root(app_dir);

    ensureBundledExtensionsStaged(bundled_root);

    ExtensionDiscoveryOptions options;
    options.bundled_roots = {bundled_root};
    options.context = defaultContext();
    options.context.runtime_target = an::ui::extensions::current_runtime_target();
    options.include_development_only_extensions = true;

    ExtensionManager manager;
    const auto discovered = manager.discoverExtensions(options);
    ASSERT_FALSE(discovered.empty());

    const auto find_by_id = [&discovered](const std::string_view extension_id) {
        return std::find_if(discovered.begin(), discovered.end(), [extension_id](const auto& result) {
            return result.manifest.has_value() && result.manifest->id == extension_id;
        });
    };

    const auto rt_preview_it = find_by_id("an.demo.rt_preview");
    ASSERT_NE(rt_preview_it, discovered.end());
    EXPECT_EQ(rt_preview_it->status, ExtensionLoadStatus::Ready);
    EXPECT_EQ(rt_preview_it->source, ExtensionPackageSource::Bundled);
    ASSERT_EQ(rt_preview_it->manifest->processes.size(), 1u);
    EXPECT_TRUE(rt_preview_it->manifest->processes.front().capabilities.preview);
    EXPECT_EQ(rt_preview_it->manifest->processes.front().capabilities.ui, "auto");

    const auto custom_ui_it = find_by_id("an.demo.custom_ui");
    ASSERT_NE(custom_ui_it, discovered.end());
    EXPECT_EQ(custom_ui_it->status, ExtensionLoadStatus::Ready);
    EXPECT_EQ(custom_ui_it->source, ExtensionPackageSource::Bundled);
    ASSERT_EQ(custom_ui_it->manifest->processes.size(), 1u);
    EXPECT_FALSE(custom_ui_it->manifest->processes.front().capabilities.preview);
    EXPECT_EQ(custom_ui_it->manifest->processes.front().capabilities.ui, "auto");

    const auto standalone_preview_it = find_by_id("an.demo.standalone_preview");
    ASSERT_NE(standalone_preview_it, discovered.end());
    EXPECT_EQ(standalone_preview_it->status, ExtensionLoadStatus::Ready);
    EXPECT_EQ(standalone_preview_it->source, ExtensionPackageSource::Bundled);
    ASSERT_EQ(standalone_preview_it->manifest->processes.size(), 1u);
    EXPECT_FALSE(standalone_preview_it->manifest->processes.front().capabilities.preview);
    EXPECT_TRUE(standalone_preview_it->manifest->processes.front().capabilities.masks_input);
    EXPECT_FALSE(standalone_preview_it->manifest->processes.front().capabilities.masks_output);
    EXPECT_TRUE(standalone_preview_it->manifest->processes.front().capabilities.weights_input);
    EXPECT_EQ(standalone_preview_it->manifest->processes.front().capabilities.ui, "auto");
}
