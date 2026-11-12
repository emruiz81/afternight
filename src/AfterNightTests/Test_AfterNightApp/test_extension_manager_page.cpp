// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <gtest/gtest.h>

#include <QAction>
#include <QApplication>
#include <QByteArray>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDialog>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QMargins>
#include <QMenu>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QTabBar>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QThread>
#include <QThreadPool>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>
#include <QtGlobal>

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <string_view>
#include <tuple>
#include <vector>

#include "app/DialogOrchestrationCoordinator.h"
#include "app/ExtensionCommon.h"
#include "app/ExtensionEnvironmentManager.h"
#include "components/AnCardWidget.h"
#include "components/AnInfoBanner.h"
#include "components/AnStatusBadge.h"
#include "controls/GlobalSettingsPersistence.h"
#include "controls/PersistenceDbExecutor.h"
#include "controls/settings/ExtensionManagerPage.h"
#include "controls/settings/ExtensionStatusPage.h"
#include "controls/settings/SettingsKeys.h"
#include "theme/AnDesignTokens.h"

#include "AppTestSupport.h"

#include <AfterNightPlatform/Logging.h>

namespace
{

using an::ui::ExtensionManagerPageEnvironmentCleanupRequest;
using an::ui::ExtensionManagerPageEnvironmentCleanupResult;
using an::ui::ExtensionManagerPageInstallRequest;
using an::ui::ExtensionManagerPageInstallResult;
using an::ui::ExtensionManagerPageLocalInstallRequest;
using an::ui::ExtensionManagerPageRemoveRequest;
using an::ui::ExtensionManagerPageRemoveResult;
using an::ui::ExtensionManagerPageRepositorySnapshot;
using an::ui::extensions::ExtensionDiscoveryOptions;
using an::ui::extensions::ExtensionEnvironmentState;
using an::ui::extensions::ExtensionManager;
using an::ui::extensions::ExtensionPackageSignatureState;
using an::ui::extensions::ExtensionPackageSource;
using an::ui::extensions::ExtensionRepositoryBrowseEntry;
using an::ui::extensions::ExtensionRepositoryBrowseOptions;
using an::ui::extensions::ExtensionRepositoryClient;
using an::ui::extensions::ExtensionRepositoryIndexFetchRequest;
using an::ui::extensions::ExtensionRepositoryInstallState;
using an::ui::extensions::ExtensionSettingsSchemaState;
using an::ui::extensions::ExtensionStartupCachePrimingResult;

void ensureApp()
{
    if (QApplication::instance())
    {
        return;
    }

    static int argc = 0;
    static char* argv[] = {nullptr};
    static QApplication app(argc, argv);
    (void)app;
}

QString sanitizeObjectToken(const std::string_view value)
{
    QString sanitized;
    sanitized.reserve(static_cast<int>(value.size()));
    bool previous_was_separator = false;
    for (const char ch : value)
    {
        const unsigned char uch = static_cast<unsigned char>(ch);
        if (std::isalnum(uch) != 0)
        {
            sanitized.push_back(QChar::fromLatin1(static_cast<char>(std::tolower(uch))));
            previous_was_separator = false;
            continue;
        }

        if (!previous_was_separator)
        {
            sanitized.push_back(QChar::fromLatin1('_'));
            previous_was_separator = true;
        }
    }

    while (sanitized.endsWith(QChar::fromLatin1('_')))
    {
        sanitized.chop(1);
    }
    return sanitized.isEmpty() ? QStringLiteral("extension") : sanitized;
}

QString fieldObjectName(const std::string_view extension_id, const char* field_name)
{
    return QStringLiteral("anExtensionManager_%1_%2")
        .arg(QString::fromLatin1(field_name), sanitizeObjectToken(extension_id));
}

QString buttonObjectName(const std::string_view extension_id, const char* button_name)
{
    return QStringLiteral("anExtensionManagerButton_%1_%2")
        .arg(QString::fromLatin1(button_name), sanitizeObjectToken(extension_id));
}

QString pendingCancelButtonObjectName(const std::string_view extension_id)
{
    return QStringLiteral("anExtensionManagerButton_cancelPending_%1").arg(sanitizeObjectToken(extension_id));
}

// What a host's exit may take beyond its ordinary time while leak detection is on. A host that ran an interpreter reads
// its whole heap as it exits under the leak configuration and symbolizes what the interpreter's libraries leave before
// the suppressions set it aside, half a minute and more and past two minutes on bench-linux; an install inspects its
// extension through such a host. Leak detection is on in an address-sanitized build on Linux unless ASAN_OPTIONS turns
// it off.
#if defined(__SANITIZE_ADDRESS__)
constexpr bool kAddressSanitized = true;
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
constexpr bool kAddressSanitized = true;
#else
constexpr bool kAddressSanitized = false;
#endif
#else
constexpr bool kAddressSanitized = false;
#endif

[[nodiscard]] int hostExitAllowanceMs()
{
#ifdef __linux__
    if (kAddressSanitized && !qgetenv("ASAN_OPTIONS").contains("detect_leaks=0"))
    {
        return 300000;
    }
#endif
    return 0;
}

bool waitFor(const std::function<bool()>& predicate, const int timeout_ms = 1000)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeout_ms)
    {
        if (predicate())
        {
            return true;
        }
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        QThread::msleep(1);
    }

    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    return predicate();
}

bool isDescendantOf(const QWidget* widget, const QWidget* ancestor)
{
    for (const QWidget* current = widget; current; current = current->parentWidget())
    {
        if (current == ancestor)
        {
            return true;
        }
    }

    return false;
}

QAction* findMoreActionsMenuAction(an::ui::ExtensionManagerPage& page, const QString& text)
{
    auto* menu = page.findChild<QMenu*>(QStringLiteral("anExtensionManagerMoreActionsMenu"));
    if (!menu)
    {
        return nullptr;
    }

    const auto actions = menu->actions();
    const auto iter = std::find_if(actions.cbegin(), actions.cend(),
                                   [&](const QAction* action) { return action && action->text() == text; });
    return iter == actions.cend() ? nullptr : *iter;
}

struct FinishProviderOnExit
{
    std::atomic_bool& finish;

    ~FinishProviderOnExit()
    {
        finish.store(true);
    }
};

/// Holds the application's thread pool back. While the gate is closed the pool starts no task it is handed, so the work
/// a page queues waits until the case has looked at the page in between; the gate opens when it goes.
class ThreadPoolGate
{
public:
    ThreadPoolGate() = default;

    ~ThreadPoolGate()
    {
        open();
    }

    ThreadPoolGate(const ThreadPoolGate&) = delete;
    ThreadPoolGate& operator=(const ThreadPoolGate&) = delete;

    /// Closes the gate: every task the pool is handed from now on waits until the gate opens. A pool task may call it.
    void close()
    {
        const std::lock_guard lock(m_Mutex);
        if (m_Release)
        {
            return;
        }

        QThreadPool* const pool = QThreadPool::globalInstance();
        m_MaxThreadCount = pool->maxThreadCount();
        m_Release = std::make_shared<std::promise<void>>();
        pool->setMaxThreadCount(1);
        pool->start([released = m_Release->get_future().share()]() { released.wait(); });
    }

    /// Opens the gate and gives the pool back the threads it had.
    void open()
    {
        const std::lock_guard lock(m_Mutex);
        if (!m_Release)
        {
            return;
        }

        m_Release->set_value();
        m_Release.reset();
        QThreadPool::globalInstance()->setMaxThreadCount(m_MaxThreadCount);
    }

private:
    std::mutex m_Mutex;
    std::shared_ptr<std::promise<void>> m_Release;
    int m_MaxThreadCount = 0;
};

/// Lets the work earlier cases left on the thread pool finish and delivers what it reported.
void drainThreadPool()
{
    QThreadPool::globalInstance()->waitForDone(30000);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
}

/// Whether one record the process logger kept since `since` holds every fragment, at `level` when one is named.
bool loggedSince(const std::chrono::system_clock::time_point since, const std::vector<std::string_view>& fragments,
                 const std::optional<spdlog::level::level_enum> level = std::nullopt)
{
    LOG_FLUSH;
    for (const auto& message : an::platform::recentLogMessages(0u))
    {
        if (message.time < since || (level.has_value() && message.level != *level))
        {
            continue;
        }

        const std::string_view payload(message.payload.data(), message.payload.size());
        if (std::all_of(fragments.begin(), fragments.end(), [payload](const std::string_view fragment) {
                return payload.find(fragment) != std::string_view::npos;
            }))
        {
            return true;
        }
    }
    return false;
}

/// How many records the process logger kept since `since` hold `fragment`.
int countLoggedSince(const std::chrono::system_clock::time_point since, const std::string_view fragment)
{
    LOG_FLUSH;
    int count = 0;
    for (const auto& message : an::platform::recentLogMessages(0u))
    {
        const std::string_view payload(message.payload.data(), message.payload.size());
        if (message.time >= since && payload.find(fragment) != std::string_view::npos)
        {
            ++count;
        }
    }
    return count;
}

/// Whether a label under `root` shows `text`.
bool hasLabelContaining(const QWidget& root, const QString& text)
{
    const auto labels = root.findChildren<QLabel*>();
    return std::any_of(labels.begin(), labels.end(),
                       [&text](const QLabel* label) { return label && label->text().contains(text); });
}

/// A settings store over a database of its own in memory, as the settings pages are handed one.
class InMemorySettings
{
public:
    InMemorySettings()
        : m_Executor(QStringLiteral(":memory:"), QStringLiteral("extension_manager_page_settings_%1")
                                                     .arg(s_Sequence.fetch_add(1, std::memory_order_relaxed))),
          m_Settings(m_Executor)
    {
        m_Ready = m_Executor.isOpen() && m_Settings.initializeTable();
    }

    InMemorySettings(const InMemorySettings&) = delete;
    InMemorySettings& operator=(const InMemorySettings&) = delete;

    [[nodiscard]] bool ready() const noexcept
    {
        return m_Ready;
    }

    [[nodiscard]] GlobalSettingsPersistence* persistence() noexcept
    {
        return &m_Settings;
    }

private:
    inline static std::atomic<int> s_Sequence{0};

    PersistenceDbExecutor m_Executor;
    GlobalSettingsPersistence m_Settings;
    bool m_Ready = false;
};

/// One package of the repository snapshot cache the page keeps, as the page writes it.
QJsonObject cachedRepositoryEntry(const QString& id, const QString& name, const QString& latest_version,
                                  const QString& installed_version, const ExtensionRepositoryInstallState state)
{
    QJsonObject entry{
        {QStringLiteral("id"), id},
        {QStringLiteral("name"), name},
        {QStringLiteral("summary"), QStringLiteral("Cached repository package")},
        {QStringLiteral("description"), QStringLiteral("A package loaded from the Extension Manager cache.")},
        {QStringLiteral("author"), QStringLiteral("AfterNight")},
        {QStringLiteral("publisher_id"), QStringLiteral("afternight")},
        {QStringLiteral("latest_version"), latest_version},
        {QStringLiteral("selected_release_version"), latest_version},
        {QStringLiteral("release_runtime_targets"), QJsonArray{QStringLiteral("windows-msvc-x86_64")}},
        {QStringLiteral("selected_asset_name"), id + QStringLiteral("-") + latest_version + QStringLiteral(".tar.zst")},
        {QStringLiteral("selected_asset_download_url"),
         QStringLiteral("packages/") + id + QStringLiteral("-") + latest_version + QStringLiteral(".tar.zst")},
        {QStringLiteral("selected_asset_package_hash_sha256"), QString(64, QChar::fromLatin1('a'))},
        {QStringLiteral("selected_asset_runtime_targets"), QJsonArray{QStringLiteral("windows-msvc-x86_64")}},
        {QStringLiteral("selected_asset_signature_state"), static_cast<int>(ExtensionPackageSignatureState::Unsigned)},
        {QStringLiteral("runtime_compatible"), true},
        {QStringLiteral("install_state"), static_cast<int>(state)}};
    if (!installed_version.isEmpty())
    {
        entry.insert(QStringLiteral("installed_version"), installed_version);
        entry.insert(QStringLiteral("installed_source_available"), true);
        entry.insert(QStringLiteral("installed_source"), static_cast<int>(ExtensionPackageSource::Repository));
    }
    return entry;
}

/// Writes the repository snapshot cache the page reads at load into `directory`.
void writeRepositorySnapshotCache(const QString& directory, const QJsonArray& entries)
{
    const QJsonObject cache_root{{QStringLiteral("cache_format_version"), 1},
                                 {QStringLiteral("updated_at_utc"), QStringLiteral("2026-05-17T00:00:00Z")},
                                 {QStringLiteral("index_url"), QStringLiteral("https://example.invalid/index.json")},
                                 {QStringLiteral("entries"), entries}};

    QFile cache_file(QDir(directory).filePath(QStringLiteral("repository_snapshot.json")));
    ASSERT_TRUE(cache_file.open(QIODevice::WriteOnly));
    ASSERT_GT(cache_file.write(QJsonDocument(cache_root).toJson(QJsonDocument::Compact)), 0);
}

/// Opens the Extensions dialog as the application's main window opens it, runs `inspect` while it is shown and closes
/// it; answers whether the dialog was shown.
bool inspectExtensionsDialog(const std::function<void(QDialog&)>& inspect)
{
    QWidget main_window;
    an::ui::DialogOrchestrationCoordinator::Dependencies dependencies;
    dependencies.mainWindow = &main_window;
    const an::ui::DialogOrchestrationCoordinator coordinator(dependencies);

    bool inspected = false;
    auto attempts = std::make_shared<int>(500);
    auto poll = std::make_shared<std::function<void()>>();
    *poll = [&inspect, &inspected, attempts, weak_poll = std::weak_ptr<std::function<void()>>(poll)]() {
        for (QWidget* widget : QApplication::topLevelWidgets())
        {
            auto* dialog = qobject_cast<QDialog*>(widget);
            if (dialog && dialog->isVisible() && dialog->objectName() == QStringLiteral("extensionsDialog"))
            {
                inspect(*dialog);
                inspected = true;
                dialog->reject();
                return;
            }
        }

        if (--(*attempts) > 0)
        {
            if (const auto next = weak_poll.lock())
            {
                QTimer::singleShot(10, *next);
            }
            return;
        }

        // The dialog never showed under the name the case looks for: close whatever modal dialog holds the loop, so
        // the case fails rather than waits.
        for (QWidget* widget : QApplication::topLevelWidgets())
        {
            if (auto* dialog = qobject_cast<QDialog*>(widget); dialog && dialog->isVisible())
            {
                dialog->reject();
            }
        }
    };
    QTimer::singleShot(0, *poll);
    coordinator.openExtensions();
    return inspected;
}

ExtensionRepositoryBrowseEntry makeEntry(const std::string& id, const std::string& name, const std::string& summary,
                                         const std::string& latest_version, const ExtensionRepositoryInstallState state,
                                         const std::string& installed_version = {})
{
    ExtensionRepositoryBrowseEntry entry;
    entry.id = id;
    entry.name = name;
    entry.summary = summary;
    entry.description = summary + " description";
    entry.author = "AfterNight";
    entry.publisher_id = "official";
    entry.license = "MIT";
    entry.homepage_url = "https://example.invalid/extensions/" + id;
    entry.repository_url = "https://github.com/afternight/" + id;
    entry.support_url = "https://example.invalid/extensions/" + id + "/support";
    entry.icon_url = "assets/" + id + ".png";
    entry.tags = {"astro", "official"};
    entry.latest_version = latest_version;
    entry.installed_version = installed_version;
    entry.selected_release_version = latest_version;
    entry.release_min_app_version = "2.0.0";
    entry.release_package_format_version = 1;
    entry.release_protocol_version = 1;
    entry.release_sdk_version = 1;
    entry.release_sdk_backend = "runtime";
    entry.release_launch_mode = "single_image";
    entry.release_runtime_targets = {"linux-clang-x86_64", "windows-msvc-x86_64"};
    entry.release_changelog = "Initial repository release.";
    entry.release_published_at_utc = "2026-04-27T00:00:00Z";
    entry.selected_asset_name = id + "-linux-x64.tar.zst";
    entry.selected_asset_download_url = "packages/" + entry.selected_asset_name;
    entry.selected_asset_package_hash_sha256 = std::string(64, 'a');
    entry.selected_asset_runtime_targets = {"linux-clang-x86_64"};
    entry.selected_asset_signature_state = ExtensionPackageSignatureState::Unsigned;
    entry.runtime_compatible = state != ExtensionRepositoryInstallState::Incompatible;
    entry.compatibility_diagnostic =
        entry.runtime_compatible ? std::string() : std::string("Latest release does not support this runtime");
    entry.install_state = state;
    if (!installed_version.empty())
    {
        entry.installed_source_available = true;
        entry.installed_source = ExtensionPackageSource::Repository;
    }
    return entry;
}

void allowUnsignedExtensionInstalls(an::ui::ExtensionManagerPage& page)
{
    page.setUnsignedInstallConfirmationProvider([](QWidget*, const QString&, const QString&) { return true; });
    auto* allow_unsigned = page.findChild<QCheckBox*>(QStringLiteral("anExtensionManagerAllowUnsignedExtensionsCheck"));
    ASSERT_NE(allow_unsigned, nullptr);
    allow_unsigned->setChecked(true);
}

void allowBrowseExtensionUninstalls(an::ui::ExtensionManagerPage& page)
{
    page.setRemoveConfirmationProvider([](QWidget*, const ExtensionRepositoryBrowseEntry&) { return true; });
}

// Accepts the next message box, looking every 10 ms up to 50 times. Each look schedules the next as a copy of
// itself, so nothing outlives the last one; a closure that held itself through a shared pointer would never be freed.
struct MessageBoxAccepter
{
    int attempts = 50;

    void operator()() const
    {
        for (auto* widget : QApplication::topLevelWidgets())
        {
            if (auto* message_box = qobject_cast<QMessageBox*>(widget))
            {
                message_box->accept();
                return;
            }
        }

        if (attempts > 1)
        {
            QTimer::singleShot(10, MessageBoxAccepter{attempts - 1});
        }
    }
};

void acceptNextMessageBox()
{
    QTimer::singleShot(0, MessageBoxAccepter{});
}

void markInstalledStatus(ExtensionRepositoryBrowseEntry& entry, const ExtensionEnvironmentState state,
                         const std::string& diagnostic = {})
{
    entry.installed_status_available = true;
    entry.installed_load_status = an::ui::extensions::ExtensionLoadStatus::Ready;
    entry.installed_environment_available = true;
    entry.installed_environment_state = state;
    entry.installed_status_diagnostic = diagnostic;
    entry.installed_environment_diagnostic = diagnostic;
}

void markStartupCachesPrimed(ExtensionManagerPageInstallResult& result, const bool from_cache = false)
{
    ExtensionStartupCachePrimingResult priming;
    priming.success = true;
    priming.environment_ready = true;
    priming.settings_schema_ready = true;
    priming.settings_schema_from_cache = from_cache;
    result.transaction.startup_cache_priming = priming;
    result.transaction.package_bytecode_precompiled = true;
}

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

std::string sha256FileHex(const std::filesystem::path& path)
{
    const std::string contents = readTextFile(path);
    return QCryptographicHash::hash(QByteArray(contents.data(), static_cast<QByteArray::size_type>(contents.size())),
                                    QCryptographicHash::Sha256)
        .toHex()
        .toStdString();
}

std::string sha256BinaryFileHex(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    EXPECT_TRUE(in.is_open()) << "Failed to open " << path;

    QCryptographicHash hash(QCryptographicHash::Sha256);
    std::array<char, 1024 * 1024> buffer{};
    while (in)
    {
        in.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const std::streamsize read_count = in.gcount();
        if (read_count > 0)
        {
            hash.addData(QByteArrayView(buffer.data(), static_cast<QByteArrayView::size_type>(read_count)));
        }
    }

    return hash.result().toHex().toStdString();
}

QJsonObject readJsonObject(const std::filesystem::path& path)
{
    const std::string text = readTextFile(path);
    QJsonParseError parse_error;
    const QJsonDocument document =
        QJsonDocument::fromJson(QByteArray(text.data(), static_cast<QByteArray::size_type>(text.size())), &parse_error);
    EXPECT_EQ(parse_error.error, QJsonParseError::NoError) << path << ": " << parse_error.errorString().toStdString();
    EXPECT_TRUE(document.isObject()) << path;
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
    const bool has_payload = type == '0';
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

    std::array<char, 512> zero{};
    archive.write(zero.data(), static_cast<std::streamsize>(zero.size()));
    archive.write(zero.data(), static_cast<std::streamsize>(zero.size()));
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

ExtensionDiscoveryOptions buildUiOnlyDiscoveryOptions()
{
    ExtensionDiscoveryOptions options;
    options.context.runtime_target = "test-runtime";
    options.context.environments_root =
        std::filesystem::temp_directory_path() / "afternight-extension-manager-page-tests" / "ExtensionEnvs";
    return options;
}

void writeJsonFile(const std::filesystem::path& path, const QJsonObject& object)
{
    writeTextFile(path, QJsonDocument(object).toJson(QJsonDocument::Indented).toStdString());
}

ExtensionDiscoveryOptions buildPageE2EDiscoveryOptions(const std::filesystem::path& install_root,
                                                       const std::filesystem::path& environments_root)
{
    ExtensionDiscoveryOptions options;
    const std::filesystem::path app_dir(QCoreApplication::applicationDirPath().toStdString());
    options.repository_roots.push_back(install_root);
    options.context.runtime_target = an::ui::extensions::current_runtime_target();
    options.context.bundled_python_executable = an::ui::extensions::bundled_python_executable_for_app_dir(app_dir);
    options.context.extension_host_executable =
        an::ui::extensions::bundled_extension_host_executable_for_app_dir(app_dir);
    options.context.runtime_root = app_dir / "PythonLib";
    options.context.environments_root = environments_root;
    options.context.host_bootstrap_fingerprint = "extension-manager-page-e2e";
    options.context.eager_settings_schema_inspection = false;
    // An install reads the settings schema through a host it starts and waits for.
    options.context.settings_schema_inspection_timeout_ms += hostExitAllowanceMs();
    return options;
}

ExtensionManagerPageRepositorySnapshot buildFixtureRepositorySnapshot(const std::filesystem::path& index_path,
                                                                      const ExtensionDiscoveryOptions& options)
{
    ExtensionRepositoryIndexFetchRequest request;
    request.index_url = QUrl::fromLocalFile(QString::fromStdString(index_path.string()));

    ExtensionRepositoryClient client;
    const auto fetch = client.fetchIndex(request);

    ExtensionManagerPageRepositorySnapshot snapshot;
    snapshot.success = fetch.success;
    snapshot.diagnostic = QString::fromStdString(fetch.diagnostic);
    snapshot.indexUrl = fetch.index_url;
    if (!fetch.success)
    {
        return snapshot;
    }

    ExtensionManager manager;
    auto installed = manager.discoverExtensions(options);
    installed.erase(std::remove_if(installed.begin(), installed.end(),
                                   [](const auto& result) { return !result.manifest.has_value(); }),
                    installed.end());

    ExtensionRepositoryBrowseOptions browse_options;
    browse_options.context = options.context;
    snapshot.entries = an::ui::extensions::build_extension_repository_browse_entries(fetch.validation.index, installed,
                                                                                     browse_options);
    return snapshot;
}

std::optional<an::ui::extensions::ExtensionLoadResult>
findDiscoveredExtension(const std::vector<an::ui::extensions::ExtensionLoadResult>& discovered,
                        const std::string& extension_id)
{
    const auto it = std::find_if(discovered.begin(), discovered.end(), [&](const auto& result) {
        return result.manifest.has_value() && result.manifest->id == extension_id;
    });
    if (it == discovered.end())
    {
        return std::nullopt;
    }

    return *it;
}

} // anonymous namespace

class ExtensionManagerPageTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        ensureApp();
    }
};

class ScopedEnvironmentVariable
{
public:
    ScopedEnvironmentVariable(const char* name, const char* value)
        : m_Name(name), m_HadOriginal(qEnvironmentVariableIsSet(name))
    {
        if (m_HadOriginal)
        {
            m_OriginalValue = qgetenv(name);
        }
        qputenv(m_Name.constData(), QByteArray(value));
    }

    ~ScopedEnvironmentVariable()
    {
        if (m_HadOriginal)
        {
            qputenv(m_Name.constData(), m_OriginalValue);
            return;
        }

        qunsetenv(m_Name.constData());
    }

private:
    QByteArray m_Name;
    QByteArray m_OriginalValue;
    bool m_HadOriginal = false;
};

TEST_F(ExtensionManagerPageTest, LoadSettingsRendersRepositoryCardsFromProvider)
{
    an::ui::ExtensionManagerPage page(nullptr);
    int calls = 0;
    page.setRepositoryProvider([&]() {
        ++calls;

        ExtensionManagerPageRepositorySnapshot snapshot;
        snapshot.success = true;
        snapshot.entries = {makeEntry("sample_ai", "Sample AI", "AI-assisted background extraction", "1.1.0",
                                      ExtensionRepositoryInstallState::UpdateAvailable, "1.0.0"),
                            makeEntry("sample_tools", "Sample Tools", "Sharpening and denoise tools", "2.0.0",
                                      ExtensionRepositoryInstallState::Installed, "2.0.0")};
        snapshot.entries.front().attribution =
            "AfterNight port of Sample AI, originally authored by Sample AI Development Team.";
        snapshot.entries.front().original_author = "Sample AI Development Team";
        snapshot.entries.front().original_project = "Sample AI";
        snapshot.entries.front().original_source_url = "https://github.com/example/sample-ai-upstream";
        snapshot.entries.front().upstream_commit = "abcdef0123456789";
        return snapshot;
    });

    page.loadSettings();

    EXPECT_EQ(calls, 1);

    auto* summary_banner = dynamic_cast<an::ui::AnInfoBanner*>(
        page.findChild<QWidget*>(QStringLiteral("anExtensionManagerSummaryBanner")));
    ASSERT_NE(summary_banner, nullptr);
    EXPECT_TRUE(summary_banner->message().contains(QStringLiteral("2 package(s) available")));
    EXPECT_TRUE(summary_banner->message().contains(QStringLiteral("1 update")));
    EXPECT_EQ(summary_banner->tone(), an::ui::AnInfoBanner::Tone::Warning);

    auto* sample_ai_card = page.findChild<QWidget*>(QStringLiteral("anExtensionManagerCard_sample_ai"));
    ASSERT_NE(sample_ai_card, nullptr);
    EXPECT_TRUE(sample_ai_card->property("anCardCompact").toBool());
    EXPECT_TRUE(sample_ai_card->property("anExtensionManagerUpdateAvailable").toBool());

    auto* sample_ai_state_badge = page.findChild<QLabel*>(fieldObjectName("sample_ai", "stateBadge"));
    ASSERT_NE(sample_ai_state_badge, nullptr);
    EXPECT_EQ(sample_ai_state_badge->text(), QStringLiteral("Update available"));
    EXPECT_TRUE(sample_ai_state_badge->toolTip().contains(QStringLiteral("1.0.0")));
    EXPECT_TRUE(sample_ai_state_badge->toolTip().contains(QStringLiteral("1.1.0")));

    auto* sample_ai_status_badge = page.findChild<QLabel*>(fieldObjectName("sample_ai", "statusBadge"));
    EXPECT_EQ(sample_ai_status_badge, nullptr);

    auto* sample_ai_version_badge =
        dynamic_cast<an::ui::AnStatusBadge*>(page.findChild<QLabel*>(fieldObjectName("sample_ai", "versionBadge")));
    ASSERT_NE(sample_ai_version_badge, nullptr);
    EXPECT_EQ(sample_ai_version_badge->text(), QStringLiteral("New version 1.1.0"));
    EXPECT_EQ(sample_ai_version_badge->status(), an::ui::AnStatusBadge::Status::Warning);
    EXPECT_TRUE(sample_ai_version_badge->toolTip().contains(QStringLiteral("Latest repository version: 1.1.0")));
    EXPECT_TRUE(sample_ai_version_badge->toolTip().contains(QStringLiteral("Installed version: 1.0.0")));

    auto* sample_ai_installed_version_badge = dynamic_cast<an::ui::AnStatusBadge*>(
        page.findChild<QLabel*>(fieldObjectName("sample_ai", "installedVersionBadge")));
    EXPECT_EQ(sample_ai_installed_version_badge, nullptr);

    auto* sample_ai_update_callout = page.findChild<QLabel*>(fieldObjectName("sample_ai", "versionUpdateCallout"));
    ASSERT_NE(sample_ai_update_callout, nullptr);
    EXPECT_EQ(sample_ai_update_callout->property("anStatusTone").toString(), QStringLiteral("warning"));
    EXPECT_TRUE(sample_ai_update_callout->text().contains(QStringLiteral("1.0.0")));
    EXPECT_TRUE(sample_ai_update_callout->text().contains(QStringLiteral("1.1.0")));

    auto* sample_ai_details_panel = page.findChild<QWidget*>(fieldObjectName("sample_ai", "detailsPanel"));
    ASSERT_NE(sample_ai_details_panel, nullptr);
    EXPECT_TRUE(sample_ai_details_panel->isHidden());

    auto* sample_ai_details_button = page.findChild<QToolButton*>(buttonObjectName("sample_ai", "details"));
    ASSERT_NE(sample_ai_details_button, nullptr);
    EXPECT_EQ(sample_ai_details_button->text(), QStringLiteral("Details"));

    auto* sample_ai_state = page.findChild<QLabel*>(fieldObjectName("sample_ai", "state"));
    ASSERT_NE(sample_ai_state, nullptr);
    EXPECT_EQ(sample_ai_state->text(), QStringLiteral("Update available"));
    EXPECT_TRUE(isDescendantOf(sample_ai_state, sample_ai_details_panel));

    auto* sample_ai_version = page.findChild<QLabel*>(fieldObjectName("sample_ai", "version"));
    ASSERT_NE(sample_ai_version, nullptr);
    EXPECT_TRUE(sample_ai_version->text().contains(QStringLiteral("Latest: 1.1.0")));
    EXPECT_TRUE(sample_ai_version->text().contains(QStringLiteral("Installed: 1.0.0")));
    EXPECT_TRUE(isDescendantOf(sample_ai_version, sample_ai_details_panel));

    auto* sample_ai_homepage = page.findChild<QLabel*>(fieldObjectName("sample_ai", "homepage"));
    ASSERT_NE(sample_ai_homepage, nullptr);
    EXPECT_EQ(sample_ai_homepage->text(), QStringLiteral("https://example.invalid/extensions/sample_ai"));
    EXPECT_TRUE(isDescendantOf(sample_ai_homepage, sample_ai_details_panel));

    auto* sample_ai_attribution = page.findChild<QLabel*>(fieldObjectName("sample_ai", "attribution"));
    ASSERT_NE(sample_ai_attribution, nullptr);
    EXPECT_TRUE(sample_ai_attribution->text().contains(QStringLiteral("originally authored")));
    EXPECT_TRUE(isDescendantOf(sample_ai_attribution, sample_ai_details_panel));

    auto* sample_ai_original_author = page.findChild<QLabel*>(fieldObjectName("sample_ai", "originalAuthor"));
    ASSERT_NE(sample_ai_original_author, nullptr);
    EXPECT_EQ(sample_ai_original_author->text(), QStringLiteral("Sample AI Development Team"));

    auto* sample_ai_original_project = page.findChild<QLabel*>(fieldObjectName("sample_ai", "originalProject"));
    ASSERT_NE(sample_ai_original_project, nullptr);
    EXPECT_EQ(sample_ai_original_project->text(), QStringLiteral("Sample AI"));

    auto* sample_ai_original_source = page.findChild<QLabel*>(fieldObjectName("sample_ai", "originalSource"));
    ASSERT_NE(sample_ai_original_source, nullptr);
    EXPECT_EQ(sample_ai_original_source->text(), QStringLiteral("https://github.com/example/sample-ai-upstream"));

    auto* sample_ai_upstream_commit = page.findChild<QLabel*>(fieldObjectName("sample_ai", "upstreamCommit"));
    ASSERT_NE(sample_ai_upstream_commit, nullptr);
    EXPECT_EQ(sample_ai_upstream_commit->text(), QStringLiteral("abcdef0123456789"));

    auto* sample_ai_sdk = page.findChild<QLabel*>(fieldObjectName("sample_ai", "sdk"));
    ASSERT_NE(sample_ai_sdk, nullptr);
    EXPECT_TRUE(sample_ai_sdk->text().contains(QStringLiteral("Package 1")));
    EXPECT_TRUE(sample_ai_sdk->text().contains(QStringLiteral("runtime")));

    auto* sample_ai_runtime_targets = page.findChild<QLabel*>(fieldObjectName("sample_ai", "runtimeTargets"));
    ASSERT_NE(sample_ai_runtime_targets, nullptr);
    EXPECT_TRUE(sample_ai_runtime_targets->text().contains(QStringLiteral("linux-clang-x86_64")));

    auto* sample_ai_changelog = page.findChild<QLabel*>(fieldObjectName("sample_ai", "changelog"));
    ASSERT_NE(sample_ai_changelog, nullptr);
    EXPECT_EQ(sample_ai_changelog->text(), QStringLiteral("Initial repository release."));
    EXPECT_TRUE(isDescendantOf(sample_ai_changelog, sample_ai_details_panel));

    sample_ai_details_button->click();
    EXPECT_FALSE(sample_ai_details_panel->isHidden());
    EXPECT_EQ(sample_ai_details_button->text(), QStringLiteral("Hide details"));

    auto* sample_ai_action = page.findChild<QPushButton*>(buttonObjectName("sample_ai", "primaryAction"));
    ASSERT_NE(sample_ai_action, nullptr);
    EXPECT_EQ(sample_ai_action->text(), QStringLiteral("Update to 1.1.0"));
    EXPECT_TRUE(sample_ai_action->toolTip().contains(QStringLiteral("Update Sample AI")));
    EXPECT_TRUE(sample_ai_action->toolTip().contains(QStringLiteral("1.0.0")));
    EXPECT_TRUE(sample_ai_action->toolTip().contains(QStringLiteral("1.1.0")));

    auto* sample_tools_state = page.findChild<QLabel*>(fieldObjectName("sample_tools", "state"));
    ASSERT_NE(sample_tools_state, nullptr);
    EXPECT_EQ(sample_tools_state->text(), QStringLiteral("Repository installed"));

    auto* sample_tools_remove = page.findChild<QPushButton*>(buttonObjectName("sample_tools", "remove"));
    ASSERT_NE(sample_tools_remove, nullptr);
    EXPECT_TRUE(sample_tools_remove->toolTip().contains(QStringLiteral("Uninstall Sample Tools")));

    auto* sample_tools_version_badge =
        dynamic_cast<an::ui::AnStatusBadge*>(page.findChild<QLabel*>(fieldObjectName("sample_tools", "versionBadge")));
    ASSERT_NE(sample_tools_version_badge, nullptr);
    EXPECT_EQ(sample_tools_version_badge->text(), QStringLiteral("Latest 2.0.0"));
    EXPECT_EQ(sample_tools_version_badge->status(), an::ui::AnStatusBadge::Status::Success);
    EXPECT_TRUE(sample_tools_version_badge->toolTip().contains(QStringLiteral("Latest repository version: 2.0.0")));
}

TEST_F(ExtensionManagerPageTest, NewerAfterNightRequirementDisablesRepositoryInstallWithDiagnostic)
{
    an::ui::ExtensionManagerPage page(nullptr);
    page.setRepositoryProvider([] {
        ExtensionManagerPageRepositorySnapshot snapshot;
        snapshot.success = true;
        auto entry = makeEntry("future_host", "Future Host", "Requires a newer AfterNight build", "1.0.0",
                               ExtensionRepositoryInstallState::Incompatible);
        entry.release_min_app_version = "2.5.0";
        entry.runtime_compatible = false;
        entry.selected_asset_name.clear();
        entry.compatibility_diagnostic = "Requires AfterNight 2.5.0 or newer. Current AfterNight version is 2.4.9.";
        snapshot.entries = {entry};
        return snapshot;
    });

    page.loadSettings();

    auto* primary_action = page.findChild<QPushButton*>(buttonObjectName("future_host", "primaryAction"));
    ASSERT_NE(primary_action, nullptr);
    EXPECT_EQ(primary_action->text(), QStringLiteral("Unavailable"));
    EXPECT_FALSE(primary_action->isEnabled());
    EXPECT_TRUE(primary_action->toolTip().contains(QStringLiteral("Requires AfterNight 2.5.0")));
    EXPECT_TRUE(primary_action->toolTip().contains(QStringLiteral("2.4.9")));

    auto* state_badge = page.findChild<QLabel*>(fieldObjectName("future_host", "stateBadge"));
    ASSERT_NE(state_badge, nullptr);
    EXPECT_EQ(state_badge->text(), QStringLiteral("Incompatible"));
    EXPECT_TRUE(state_badge->toolTip().contains(QStringLiteral("Requires AfterNight 2.5.0")));

    auto* compatibility = page.findChild<QLabel*>(fieldObjectName("future_host", "compatibility"));
    ASSERT_NE(compatibility, nullptr);
    EXPECT_TRUE(compatibility->text().contains(QStringLiteral("Requires AfterNight 2.5.0")));

    auto* detail_banner =
        dynamic_cast<an::ui::AnInfoBanner*>(page.findChild<QWidget*>(fieldObjectName("future_host", "detailBanner")));
    ASSERT_NE(detail_banner, nullptr);
    EXPECT_EQ(detail_banner->tone(), an::ui::AnInfoBanner::Tone::Warning);
    EXPECT_TRUE(detail_banner->message().contains(QStringLiteral("Requires AfterNight 2.5.0")));
}

TEST_F(ExtensionManagerPageTest, SignatureStateBadgesUseTrustColorsOnRepositoryCards)
{
    an::ui::ExtensionManagerPage page(nullptr);
    page.setRepositoryProvider([] {
        ExtensionManagerPageRepositorySnapshot snapshot;
        snapshot.success = true;

        auto signed_entry = makeEntry("signed_ext", "Signed Extension", "Verified package", "1.0.0",
                                      ExtensionRepositoryInstallState::NotInstalled);
        signed_entry.selected_asset_signature_state = ExtensionPackageSignatureState::Verified;
        signed_entry.selected_asset_signature_algorithm = "ed25519";
        signed_entry.selected_asset_signature_key_id = "afternight-test-ed25519-v1";

        auto unsigned_entry = makeEntry("unsigned_ext", "Unsigned Extension", "Unsigned package", "1.0.0",
                                        ExtensionRepositoryInstallState::NotInstalled);
        unsigned_entry.selected_asset_signature_state = ExtensionPackageSignatureState::Unsigned;

        auto failed_entry = makeEntry("failed_ext", "Failed Extension", "Failed signature package", "1.0.0",
                                      ExtensionRepositoryInstallState::NotInstalled);
        failed_entry.selected_asset_signature_state = ExtensionPackageSignatureState::Failed;

        snapshot.entries = {signed_entry, unsigned_entry, failed_entry};
        return snapshot;
    });

    page.loadSettings();

    auto* signed_badge =
        dynamic_cast<an::ui::AnStatusBadge*>(page.findChild<QLabel*>(fieldObjectName("signed_ext", "signatureBadge")));
    ASSERT_NE(signed_badge, nullptr);
    EXPECT_EQ(signed_badge->text(), QStringLiteral("Signed"));
    EXPECT_EQ(signed_badge->status(), an::ui::AnStatusBadge::Status::Success);
    EXPECT_TRUE(signed_badge->toolTip().contains(QStringLiteral("verified")));
    EXPECT_TRUE(signed_badge->toolTip().contains(QStringLiteral("Signing key")));

    auto* unsigned_badge = dynamic_cast<an::ui::AnStatusBadge*>(
        page.findChild<QLabel*>(fieldObjectName("unsigned_ext", "signatureBadge")));
    ASSERT_NE(unsigned_badge, nullptr);
    EXPECT_EQ(unsigned_badge->text(), QStringLiteral("Unsigned"));
    EXPECT_EQ(unsigned_badge->status(), an::ui::AnStatusBadge::Status::Warning);
    EXPECT_TRUE(unsigned_badge->toolTip().contains(QStringLiteral("cannot be authenticated")));

    auto* failed_badge =
        dynamic_cast<an::ui::AnStatusBadge*>(page.findChild<QLabel*>(fieldObjectName("failed_ext", "signatureBadge")));
    ASSERT_NE(failed_badge, nullptr);
    EXPECT_EQ(failed_badge->text(), QStringLiteral("Signature failed"));
    EXPECT_EQ(failed_badge->status(), an::ui::AnStatusBadge::Status::Error);
    EXPECT_TRUE(failed_badge->toolTip().contains(QStringLiteral("failed"), Qt::CaseInsensitive));

    auto* failed_detail_badge =
        dynamic_cast<an::ui::AnStatusBadge*>(page.findChild<QLabel*>(fieldObjectName("failed_ext", "signature")));
    ASSERT_NE(failed_detail_badge, nullptr);
    EXPECT_EQ(failed_detail_badge->status(), an::ui::AnStatusBadge::Status::Error);
}

TEST_F(ExtensionManagerPageTest, UnsignedExtensionsControlLivesAtBottomAndWarnsWhenEnabled)
{
    an::ui::ExtensionManagerPage page(nullptr);
    page.loadSettings();

    auto* root_layout = page.findChild<QVBoxLayout*>(QStringLiteral("anExtensionManagerRootLayout"));
    ASSERT_NE(root_layout, nullptr);

    auto* cards_scroll = page.findChild<QScrollArea*>(QStringLiteral("anExtensionManagerCardsScrollArea"));
    ASSERT_NE(cards_scroll, nullptr);

    auto* warning_banner = dynamic_cast<an::ui::AnInfoBanner*>(
        page.findChild<QWidget*>(QStringLiteral("anExtensionManagerUnsignedWarningBanner")));
    ASSERT_NE(warning_banner, nullptr);
    EXPECT_TRUE(warning_banner->isHidden());

    auto* allow_unsigned = page.findChild<QCheckBox*>(QStringLiteral("anExtensionManagerAllowUnsignedExtensionsCheck"));
    ASSERT_NE(allow_unsigned, nullptr);
    EXPECT_GT(root_layout->indexOf(warning_banner), root_layout->indexOf(cards_scroll));
    EXPECT_GT(root_layout->indexOf(allow_unsigned), root_layout->indexOf(warning_banner));

    int warning_prompts = 0;
    page.setUnsignedInstallConfirmationProvider([&](QWidget*, const QString& title, const QString& message) {
        ++warning_prompts;
        EXPECT_TRUE(title.contains(QStringLiteral("Unsigned")));
        EXPECT_TRUE(message.contains(QStringLiteral("trust")));
        return false;
    });
    allow_unsigned->setChecked(true);
    EXPECT_EQ(warning_prompts, 1);
    EXPECT_FALSE(allow_unsigned->isChecked());
    EXPECT_TRUE(warning_banner->isHidden());

    page.setUnsignedInstallConfirmationProvider([&](QWidget*, const QString&, const QString&) {
        ++warning_prompts;
        return true;
    });
    allow_unsigned->setChecked(true);
    EXPECT_EQ(warning_prompts, 2);
    EXPECT_TRUE(allow_unsigned->isChecked());
    EXPECT_FALSE(warning_banner->isHidden());
    EXPECT_EQ(warning_banner->tone(), an::ui::AnInfoBanner::Tone::Warning);
    EXPECT_TRUE(warning_banner->message().contains(QStringLiteral("cannot authenticate")));

    allow_unsigned->setChecked(false);
    EXPECT_TRUE(warning_banner->isHidden());

    allow_unsigned->setChecked(true);
    EXPECT_EQ(warning_prompts, 3);
    EXPECT_TRUE(allow_unsigned->isChecked());
    EXPECT_FALSE(warning_banner->isHidden());
}

TEST_F(ExtensionManagerPageTest, ExpandedDetailsLinkUrlsExceptUnsignedPackages)
{
    an::ui::ExtensionManagerPage page(nullptr);
    page.setRepositoryProvider([] {
        ExtensionManagerPageRepositorySnapshot snapshot;
        snapshot.success = true;

        auto signed_entry = makeEntry("signed_links", "Signed Links", "Signed link package", "1.0.0",
                                      ExtensionRepositoryInstallState::NotInstalled);
        signed_entry.selected_asset_signature_state = ExtensionPackageSignatureState::Verified;
        signed_entry.description = "Docs live at https://example.invalid/extensions/signed_links/docs";
        signed_entry.original_source_url = "https://github.com/example/signed-links-upstream";
        signed_entry.homepage_url = "https://example.invalid/extensions/signed_links";
        signed_entry.repository_url = "https://github.com/afternight/signed_links";
        signed_entry.support_url = "https://example.invalid/extensions/signed_links/support";
        signed_entry.release_changelog = "See https://example.invalid/extensions/signed_links/changelog";

        auto unsigned_entry = makeEntry("unsigned_links", "Unsigned Links", "Unsigned link package", "1.0.0",
                                        ExtensionRepositoryInstallState::NotInstalled);
        unsigned_entry.selected_asset_signature_state = ExtensionPackageSignatureState::Unsigned;
        unsigned_entry.description = "Docs live at https://example.invalid/extensions/unsigned_links/docs";
        unsigned_entry.original_source_url = "https://github.com/example/unsigned-links-upstream";
        unsigned_entry.homepage_url = "https://example.invalid/extensions/unsigned_links";
        unsigned_entry.repository_url = "https://github.com/afternight/unsigned_links";
        unsigned_entry.support_url = "https://example.invalid/extensions/unsigned_links/support";
        unsigned_entry.release_changelog = "See https://example.invalid/extensions/unsigned_links/changelog";

        snapshot.entries = {signed_entry, unsigned_entry};
        return snapshot;
    });

    page.loadSettings();

    auto expect_link = [&](const std::string& extension_id, const char* field_name, const QString& url) {
        auto* label = page.findChild<QLabel*>(fieldObjectName(extension_id, field_name));
        ASSERT_NE(label, nullptr);
        EXPECT_EQ(label->textFormat(), Qt::RichText) << field_name;
        EXPECT_TRUE(label->openExternalLinks()) << field_name;
        EXPECT_TRUE(label->text().contains(QStringLiteral("<a href=\"%1\">%1</a>").arg(url))) << field_name;
    };

    expect_link("signed_links", "description", QStringLiteral("https://example.invalid/extensions/signed_links/docs"));
    expect_link("signed_links", "originalSource", QStringLiteral("https://github.com/example/signed-links-upstream"));
    expect_link("signed_links", "homepage", QStringLiteral("https://example.invalid/extensions/signed_links"));
    expect_link("signed_links", "repository", QStringLiteral("https://github.com/afternight/signed_links"));
    expect_link("signed_links", "support", QStringLiteral("https://example.invalid/extensions/signed_links/support"));
    expect_link("signed_links", "changelog",
                QStringLiteral("https://example.invalid/extensions/signed_links/changelog"));

    for (const char* field_name : {"description", "originalSource", "homepage", "repository", "support", "changelog"})
    {
        auto* label = page.findChild<QLabel*>(fieldObjectName("unsigned_links", field_name));
        ASSERT_NE(label, nullptr) << field_name;
        EXPECT_FALSE(label->openExternalLinks()) << field_name;
        EXPECT_FALSE(label->text().contains(QStringLiteral("<a href="))) << field_name;
        EXPECT_TRUE(label->text().contains(QStringLiteral("https://"))) << field_name;
    }

    auto* overview_section = page.findChild<QLabel*>(fieldObjectName("signed_links", "overviewSection"));
    ASSERT_NE(overview_section, nullptr);
    EXPECT_EQ(overview_section->text(), QStringLiteral("Overview"));

    auto* links_section = page.findChild<QLabel*>(fieldObjectName("signed_links", "linksSection"));
    ASSERT_NE(links_section, nullptr);
    EXPECT_EQ(links_section->text(), QStringLiteral("Links"));

    auto* trust_section = page.findChild<QLabel*>(fieldObjectName("signed_links", "trustSection"));
    ASSERT_NE(trust_section, nullptr);
    EXPECT_EQ(trust_section->text(), QStringLiteral("Trust"));
}

TEST_F(ExtensionManagerPageTest, UnsignedExtensionPolicyBlocksByDefaultAndAllowsAfterConfirmation)
{
    an::ui::ExtensionManagerPage page(nullptr);
    page.setRepositoryProvider([] {
        ExtensionManagerPageRepositorySnapshot snapshot;
        snapshot.success = true;
        snapshot.indexUrl = QUrl::fromLocalFile(QStringLiteral("/tmp/afternight-extension-repo/index.json"));
        snapshot.entries = {makeEntry("unsigned_ext", "Unsigned Extension", "Unsigned package", "1.0.0",
                                      ExtensionRepositoryInstallState::NotInstalled)};
        return snapshot;
    });

    int install_calls = 0;
    page.setInstallProvider([&](const ExtensionManagerPageInstallRequest&) {
        ++install_calls;
        ExtensionManagerPageInstallResult result;
        result.success = true;
        result.diagnostic = QStringLiteral("Installed unsigned extension.");
        markStartupCachesPrimed(result);
        return result;
    });

    page.loadSettings();

    auto* allow_unsigned = page.findChild<QCheckBox*>(QStringLiteral("anExtensionManagerAllowUnsignedExtensionsCheck"));
    ASSERT_NE(allow_unsigned, nullptr);
    EXPECT_FALSE(allow_unsigned->isChecked());

    auto* install_button = page.findChild<QPushButton*>(buttonObjectName("unsigned_ext", "primaryAction"));
    ASSERT_NE(install_button, nullptr);
    ASSERT_TRUE(install_button->isEnabled());

    acceptNextMessageBox();
    install_button->click();
    EXPECT_EQ(install_calls, 0);

    auto* summary_banner = dynamic_cast<an::ui::AnInfoBanner*>(
        page.findChild<QWidget*>(QStringLiteral("anExtensionManagerSummaryBanner")));
    ASSERT_NE(summary_banner, nullptr);
    EXPECT_TRUE(summary_banner->message().contains(QStringLiteral("unsigned")));

    page.setUnsignedInstallConfirmationProvider([](QWidget*, const QString&, const QString&) { return true; });
    allow_unsigned->setChecked(true);
    page.setUnsignedInstallConfirmationProvider([](QWidget*, const QString&, const QString&) { return false; });
    install_button->click();
    EXPECT_EQ(install_calls, 0);

    page.setUnsignedInstallConfirmationProvider([](QWidget*, const QString&, const QString&) { return true; });
    install_button->click();
    ASSERT_TRUE(waitFor([&]() { return install_calls == 1; }));
}

TEST_F(ExtensionManagerPageTest, UnsignedLocalPackagePolicyBlocksByDefaultAndAllowsAfterConfirmation)
{
    an::ui::ExtensionManagerPage page(nullptr);
    QTemporaryDir package_dir;
    ASSERT_TRUE(package_dir.isValid());
    const QString archive_path = package_dir.filePath(QStringLiteral("local-extension.zip"));
    {
        QFile archive_file(archive_path);
        ASSERT_TRUE(archive_file.open(QIODevice::WriteOnly));
        archive_file.write("placeholder");
    }

    int path_calls = 0;
    page.setLocalPackagePathProvider([&](QWidget*) {
        ++path_calls;
        return archive_path;
    });

    int install_calls = 0;
    page.setLocalInstallProvider([&](const ExtensionManagerPageLocalInstallRequest&) {
        ++install_calls;
        ExtensionManagerPageInstallResult result;
        result.success = true;
        result.diagnostic = QStringLiteral("Installed local extension.");
        markStartupCachesPrimed(result);
        return result;
    });

    page.loadSettings();

    auto* allow_unsigned = page.findChild<QCheckBox*>(QStringLiteral("anExtensionManagerAllowUnsignedExtensionsCheck"));
    ASSERT_NE(allow_unsigned, nullptr);
    EXPECT_FALSE(allow_unsigned->isChecked());

    auto* more_actions_button = page.findChild<QToolButton*>(QStringLiteral("anExtensionManagerMoreActionsButton"));
    ASSERT_NE(more_actions_button, nullptr);
    ASSERT_TRUE(more_actions_button->isEnabled());

    auto* local_action = findMoreActionsMenuAction(page, QStringLiteral("Install local package..."));
    ASSERT_NE(local_action, nullptr);
    ASSERT_TRUE(local_action->isEnabled());

    acceptNextMessageBox();
    local_action->trigger();
    EXPECT_EQ(path_calls, 0);
    EXPECT_EQ(install_calls, 0);

    auto* summary_banner = dynamic_cast<an::ui::AnInfoBanner*>(
        page.findChild<QWidget*>(QStringLiteral("anExtensionManagerSummaryBanner")));
    ASSERT_NE(summary_banner, nullptr);
    EXPECT_TRUE(summary_banner->message().contains(QStringLiteral("unsigned")));

    page.setUnsignedInstallConfirmationProvider([](QWidget*, const QString&, const QString&) { return true; });
    allow_unsigned->setChecked(true);
    page.setUnsignedInstallConfirmationProvider([](QWidget*, const QString&, const QString&) { return false; });
    local_action->trigger();
    EXPECT_EQ(path_calls, 0);
    EXPECT_EQ(install_calls, 0);

    page.setUnsignedInstallConfirmationProvider([](QWidget*, const QString&, const QString&) { return true; });
    local_action->trigger();
    ASSERT_TRUE(waitFor([&]() { return path_calls == 1 && install_calls == 1; }))
        << "path_calls=" << path_calls << " install_calls=" << install_calls;
}

TEST_F(ExtensionManagerPageTest, FailedSignatureExtensionsAreBlockedEvenWhenUnsignedExtensionsAreAllowed)
{
    an::ui::ExtensionManagerPage page(nullptr);
    page.setRepositoryProvider([] {
        ExtensionManagerPageRepositorySnapshot snapshot;
        snapshot.success = true;
        snapshot.indexUrl = QUrl::fromLocalFile(QStringLiteral("/tmp/afternight-extension-repo/index.json"));
        auto entry = makeEntry("failed_ext", "Failed Extension", "Failed signature package", "1.0.0",
                               ExtensionRepositoryInstallState::NotInstalled);
        entry.selected_asset_signature_state = ExtensionPackageSignatureState::Failed;
        snapshot.entries = {entry};
        return snapshot;
    });

    int install_calls = 0;
    page.setInstallProvider([&](const ExtensionManagerPageInstallRequest&) {
        ++install_calls;
        ExtensionManagerPageInstallResult result;
        result.success = true;
        markStartupCachesPrimed(result);
        return result;
    });

    allowUnsignedExtensionInstalls(page);
    page.loadSettings();

    auto* install_button = page.findChild<QPushButton*>(buttonObjectName("failed_ext", "primaryAction"));
    ASSERT_NE(install_button, nullptr);
    ASSERT_TRUE(install_button->isEnabled());

    acceptNextMessageBox();
    install_button->click();
    EXPECT_EQ(install_calls, 0);

    auto* summary_banner = dynamic_cast<an::ui::AnInfoBanner*>(
        page.findChild<QWidget*>(QStringLiteral("anExtensionManagerSummaryBanner")));
    ASSERT_NE(summary_banner, nullptr);
    EXPECT_TRUE(summary_banner->message().contains(QStringLiteral("signature verification failed")));
}

TEST_F(ExtensionManagerPageTest, SearchTextFiltersRenderedCardsWithoutRefetchingProvider)
{
    an::ui::ExtensionManagerPage page(nullptr);
    int calls = 0;
    page.setRepositoryProvider([&]() {
        ++calls;
        ExtensionManagerPageRepositorySnapshot snapshot;
        snapshot.success = true;
        snapshot.entries = {makeEntry("sample_ai", "Sample AI", "AI-assisted background extraction", "1.1.0",
                                      ExtensionRepositoryInstallState::NotInstalled),
                            makeEntry("sample_tools", "Sample Tools", "Sharpening and denoise tools", "2.0.0",
                                      ExtensionRepositoryInstallState::NotInstalled)};
        return snapshot;
    });

    page.loadSettings();

    auto* search_edit = page.findChild<QLineEdit*>(QStringLiteral("anExtensionManagerSearchEdit"));
    ASSERT_NE(search_edit, nullptr);
    search_edit->setText(QStringLiteral("sample ai"));
    QApplication::processEvents();

    EXPECT_EQ(calls, 1);

    EXPECT_NE(page.findChild<QWidget*>(QStringLiteral("anExtensionManagerCard_sample_ai")), nullptr);
    EXPECT_EQ(page.findChild<QWidget*>(QStringLiteral("anExtensionManagerCard_sample_tools")), nullptr);

    auto* summary_banner = dynamic_cast<an::ui::AnInfoBanner*>(
        page.findChild<QWidget*>(QStringLiteral("anExtensionManagerSummaryBanner")));
    ASSERT_NE(summary_banner, nullptr);
    EXPECT_TRUE(summary_banner->message().contains(QStringLiteral("1 package(s) available")));
}

TEST_F(ExtensionManagerPageTest, PrimaryActionInvokesInstallerWithResolvedRepositoryAsset)
{
    an::ui::ExtensionManagerPage page(nullptr);
    page.setDiscoveryOptionsProvider([]() { return buildUiOnlyDiscoveryOptions(); });
    int repository_calls = 0;
    page.setRepositoryProvider([&]() {
        ++repository_calls;
        ExtensionManagerPageRepositorySnapshot snapshot;
        snapshot.success = true;
        snapshot.indexUrl = QUrl::fromLocalFile(QStringLiteral("/tmp/afternight-extension-repo/index.json"));
        snapshot.entries = {makeEntry("sample_ai", "Sample AI", "AI-assisted background extraction", "1.1.0",
                                      ExtensionRepositoryInstallState::NotInstalled)};
        return snapshot;
    });

    std::mutex mutex;
    std::optional<ExtensionManagerPageInstallRequest> captured_request;
    page.setInstallProvider([&](const ExtensionManagerPageInstallRequest& request) {
        {
            const std::lock_guard lock(mutex);
            captured_request = request;
        }

        ExtensionManagerPageInstallResult result;
        result.success = true;
        result.diagnostic = QStringLiteral("Installed Sample AI 1.1.0.");
        markStartupCachesPrimed(result);
        return result;
    });

    allowUnsignedExtensionInstalls(page);
    page.loadSettings();

    auto* install_button = page.findChild<QPushButton*>(buttonObjectName("sample_ai", "primaryAction"));
    ASSERT_NE(install_button, nullptr);
    ASSERT_TRUE(install_button->isEnabled());

    install_button->click();

    ASSERT_TRUE(waitFor([&]() {
        const std::lock_guard lock(mutex);
        return captured_request.has_value();
    }));
    ASSERT_TRUE(waitFor([&]() { return repository_calls >= 2; }));

    ExtensionManagerPageInstallRequest request;
    {
        const std::lock_guard lock(mutex);
        request = *captured_request;
    }

    EXPECT_EQ(request.entry.id, "sample_ai");
    EXPECT_EQ(request.entry.selected_asset_name, "sample_ai-linux-x64.tar.zst");
    EXPECT_EQ(request.assetUrl.toLocalFile(),
              QStringLiteral("/tmp/afternight-extension-repo/packages/sample_ai-linux-x64.tar.zst"));
    EXPECT_EQ(request.indexUrl.toLocalFile(), QStringLiteral("/tmp/afternight-extension-repo/index.json"));
    EXPECT_FALSE(request.context.runtime_target.empty());

    auto* summary_banner = dynamic_cast<an::ui::AnInfoBanner*>(
        page.findChild<QWidget*>(QStringLiteral("anExtensionManagerSummaryBanner")));
    ASSERT_NE(summary_banner, nullptr);
    EXPECT_TRUE(summary_banner->message().contains(QStringLiteral("Installed Sample AI")));
    EXPECT_TRUE(summary_banner->message().contains(QStringLiteral("Startup caches primed")));
}

TEST_F(ExtensionManagerPageTest, SuccessfulPackageInstallNotifiesInstalledStatusCacheInvalidation)
{
    an::ui::ExtensionManagerPage page(nullptr);
    page.setRepositoryProvider([] {
        ExtensionManagerPageRepositorySnapshot snapshot;
        snapshot.success = true;
        snapshot.indexUrl = QUrl(QStringLiteral("https://extensions.example.invalid/index.json"));
        snapshot.entries = {makeEntry("sample_ai", "Sample AI", "AI-assisted background extraction", "1.1.0",
                                      ExtensionRepositoryInstallState::NotInstalled)};
        return snapshot;
    });
    allowUnsignedExtensionInstalls(page);

    int changed_count = 0;
    QObject::connect(&page, &an::ui::ExtensionManagerPage::installedExtensionsChanged, &page,
                     [&changed_count]() { ++changed_count; });
    page.setInstallProvider([](const ExtensionManagerPageInstallRequest&) {
        ExtensionManagerPageInstallResult result;
        result.success = true;
        result.diagnostic = QStringLiteral("Installed Sample AI 1.1.0.");
        markStartupCachesPrimed(result);
        return result;
    });

    page.loadSettings();
    auto* install_button = page.findChild<QPushButton*>(buttonObjectName("sample_ai", "primaryAction"));
    ASSERT_NE(install_button, nullptr);
    ASSERT_TRUE(install_button->isEnabled());
    install_button->click();

    ASSERT_TRUE(waitFor([&]() { return changed_count == 1; }));
}

TEST_F(ExtensionManagerPageTest, PrimaryActionShowsImmediateBusyStateWhileInstalling)
{
    an::ui::ExtensionManagerPage page(nullptr);
    page.setRepositoryProvider([] {
        ExtensionManagerPageRepositorySnapshot snapshot;
        snapshot.success = true;
        snapshot.indexUrl = QUrl(QStringLiteral("https://extensions.example.invalid/index.json"));
        snapshot.entries = {makeEntry("sample_ai", "Sample AI", "AI-assisted background extraction", "1.1.0",
                                      ExtensionRepositoryInstallState::NotInstalled)};
        return snapshot;
    });
    allowUnsignedExtensionInstalls(page);

    std::promise<void> release_provider;
    std::shared_future<void> release_future = release_provider.get_future().share();
    std::atomic_bool provider_started = false;
    page.setInstallProvider([&](const ExtensionManagerPageInstallRequest&) {
        provider_started = true;
        release_future.wait();
        ExtensionManagerPageInstallResult result;
        result.success = true;
        markStartupCachesPrimed(result);
        return result;
    });

    page.loadSettings();
    auto* install_button = page.findChild<QPushButton*>(buttonObjectName("sample_ai", "primaryAction"));
    auto* remove_button = page.findChild<QPushButton*>(buttonObjectName("sample_ai", "remove"));
    auto* search_edit = page.findChild<QLineEdit*>(QStringLiteral("anExtensionManagerSearchEdit"));
    auto* filter_combo = page.findChild<QComboBox*>(QStringLiteral("anExtensionManagerFilterCombo"));
    auto* sort_combo = page.findChild<QComboBox*>(QStringLiteral("anExtensionManagerSortCombo"));
    auto* allow_unsigned = page.findChild<QCheckBox*>(QStringLiteral("anExtensionManagerAllowUnsignedExtensionsCheck"));
    ASSERT_NE(install_button, nullptr);
    ASSERT_NE(remove_button, nullptr);
    ASSERT_NE(search_edit, nullptr);
    ASSERT_NE(filter_combo, nullptr);
    ASSERT_NE(sort_combo, nullptr);
    ASSERT_NE(allow_unsigned, nullptr);
    EXPECT_TRUE(install_button->toolTip().contains(QStringLiteral("Install Sample AI")));

    install_button->click();
    ASSERT_TRUE(waitFor([&]() { return provider_started.load(); }));

    EXPECT_EQ(install_button->text(), QStringLiteral("Installing..."));
    EXPECT_TRUE(install_button->toolTip().contains(QStringLiteral("Installing Sample AI")));
    EXPECT_TRUE(remove_button->toolTip().contains(QStringLiteral("Sample AI is busy")));
    EXPECT_FALSE(install_button->isEnabled());
    EXPECT_FALSE(remove_button->isEnabled());
    EXPECT_TRUE(search_edit->isEnabled());
    EXPECT_TRUE(filter_combo->isEnabled());
    EXPECT_TRUE(sort_combo->isEnabled());
    EXPECT_TRUE(allow_unsigned->isEnabled());

    release_provider.set_value();
    auto* summary_banner = dynamic_cast<an::ui::AnInfoBanner*>(
        page.findChild<QWidget*>(QStringLiteral("anExtensionManagerSummaryBanner")));
    ASSERT_NE(summary_banner, nullptr);
    ASSERT_TRUE(waitFor([&]() { return summary_banner->message().contains(QStringLiteral("Startup caches primed")); }));
}

TEST_F(ExtensionManagerPageTest, ExtensionActionsCanBeQueuedWhileAnotherOperationRuns)
{
    an::ui::ExtensionManagerPage page(nullptr);
    page.setRepositoryProvider([] {
        ExtensionManagerPageRepositorySnapshot snapshot;
        snapshot.success = true;
        snapshot.indexUrl = QUrl(QStringLiteral("https://extensions.example.invalid/index.json"));
        snapshot.entries = {makeEntry("active_install", "Active Install", "Long package install", "1.0.0",
                                      ExtensionRepositoryInstallState::NotInstalled),
                            makeEntry("queued_install", "Queued Install", "Queued package install", "1.0.0",
                                      ExtensionRepositoryInstallState::NotInstalled),
                            makeEntry("queued_remove", "Queued Remove", "Queued package removal", "1.0.0",
                                      ExtensionRepositoryInstallState::Installed, "1.0.0")};
        return snapshot;
    });

    std::promise<void> release_provider;
    std::shared_future<void> release_future = release_provider.get_future().share();
    std::atomic_bool provider_started = false;
    page.setInstallProvider([&](const ExtensionManagerPageInstallRequest& request) {
        if (request.entry.id == "active_install")
        {
            provider_started = true;
            release_future.wait();
        }

        ExtensionManagerPageInstallResult result;
        result.success = true;
        markStartupCachesPrimed(result);
        return result;
    });
    page.setRemoveProvider([](const ExtensionManagerPageRemoveRequest&) {
        ExtensionManagerPageRemoveResult result;
        result.success = true;
        result.diagnostic = QStringLiteral("Removed queued extension.");
        return result;
    });
    int uninstall_confirmation_calls = 0;
    page.setRemoveConfirmationProvider([&](QWidget*, const ExtensionRepositoryBrowseEntry& entry) {
        ++uninstall_confirmation_calls;
        EXPECT_EQ(entry.id, "queued_remove");
        return true;
    });

    allowUnsignedExtensionInstalls(page);
    page.loadSettings();

    auto* active_install = page.findChild<QPushButton*>(buttonObjectName("active_install", "primaryAction"));
    auto* queued_install = page.findChild<QPushButton*>(buttonObjectName("queued_install", "primaryAction"));
    auto* queued_install_remove = page.findChild<QPushButton*>(buttonObjectName("queued_install", "remove"));
    auto* queued_remove_primary = page.findChild<QPushButton*>(buttonObjectName("queued_remove", "primaryAction"));
    auto* queued_remove = page.findChild<QPushButton*>(buttonObjectName("queued_remove", "remove"));
    ASSERT_NE(active_install, nullptr);
    ASSERT_NE(queued_install, nullptr);
    ASSERT_NE(queued_install_remove, nullptr);
    ASSERT_NE(queued_remove_primary, nullptr);
    ASSERT_NE(queued_remove, nullptr);

    active_install->click();
    ASSERT_TRUE(waitFor([&]() { return provider_started.load(); }));

    EXPECT_TRUE(queued_install->isEnabled());
    EXPECT_TRUE(queued_remove->isEnabled());

    queued_install->click();
    EXPECT_EQ(queued_install->text(), QStringLiteral("pending..."));
    EXPECT_TRUE(queued_install->toolTip().contains(QStringLiteral("Installing Queued Install is queued")));
    EXPECT_TRUE(queued_install_remove->toolTip().contains(QStringLiteral("Installing Queued Install is queued")));
    EXPECT_FALSE(queued_install->isEnabled());
    EXPECT_FALSE(queued_install_remove->isEnabled());

    auto* queue_container = page.findChild<QWidget*>(QStringLiteral("anExtensionManagerPendingQueue"));
    auto* queue_header = page.findChild<QLabel*>(QStringLiteral("anExtensionManagerPendingQueueHeader"));
    ASSERT_NE(queue_container, nullptr);
    ASSERT_NE(queue_header, nullptr);
    EXPECT_FALSE(queue_container->isHidden());
    EXPECT_TRUE(queue_header->text().contains(QStringLiteral("1 queued")));

    queued_remove->click();
    EXPECT_EQ(uninstall_confirmation_calls, 1);
    EXPECT_EQ(queued_remove->text(), QStringLiteral("pending..."));
    EXPECT_TRUE(queued_remove->toolTip().contains(QStringLiteral("Uninstalling Queued Remove is queued")));
    EXPECT_TRUE(queued_remove_primary->toolTip().contains(QStringLiteral("Uninstalling Queued Remove is queued")));
    EXPECT_FALSE(queued_remove->isEnabled());
    EXPECT_FALSE(queued_remove_primary->isEnabled());
    queue_header = page.findChild<QLabel*>(QStringLiteral("anExtensionManagerPendingQueueHeader"));
    ASSERT_NE(queue_header, nullptr);
    EXPECT_TRUE(queue_header->text().contains(QStringLiteral("2 queued")));

    auto* cancel_queued_install = page.findChild<QPushButton*>(pendingCancelButtonObjectName("queued_install"));
    ASSERT_NE(cancel_queued_install, nullptr);
    cancel_queued_install->click();

    EXPECT_NE(queued_install->text(), QStringLiteral("pending..."));
    EXPECT_TRUE(queued_install->toolTip().contains(QStringLiteral("Install Queued Install")));
    EXPECT_TRUE(queued_install->isEnabled());
    queue_header = page.findChild<QLabel*>(QStringLiteral("anExtensionManagerPendingQueueHeader"));
    ASSERT_NE(queue_header, nullptr);
    EXPECT_TRUE(queue_header->text().contains(QStringLiteral("1 queued")));

    release_provider.set_value();
    auto* summary_banner = dynamic_cast<an::ui::AnInfoBanner*>(
        page.findChild<QWidget*>(QStringLiteral("anExtensionManagerSummaryBanner")));
    ASSERT_NE(summary_banner, nullptr);
    ASSERT_TRUE(waitFor([&]() {
        return summary_banner->message().contains(QStringLiteral("Removed queued extension.")) ||
               summary_banner->message().contains(QStringLiteral("Startup caches primed"));
    }));
}

TEST_F(ExtensionManagerPageTest, QueuedOperationsRunAfterCurrentOperationCompletes)
{
    an::ui::ExtensionManagerPage page(nullptr);
    page.setRepositoryProvider([] {
        ExtensionManagerPageRepositorySnapshot snapshot;
        snapshot.success = true;
        snapshot.indexUrl = QUrl(QStringLiteral("https://extensions.example.invalid/index.json"));
        snapshot.entries = {makeEntry("first_install", "First Install", "First package", "1.0.0",
                                      ExtensionRepositoryInstallState::NotInstalled),
                            makeEntry("second_install", "Second Install", "Second package", "1.0.0",
                                      ExtensionRepositoryInstallState::NotInstalled)};
        return snapshot;
    });

    std::atomic_bool release_first = false;
    FinishProviderOnExit finish_guard{release_first};
    std::mutex mutex;
    std::vector<std::string> install_order;
    page.setInstallProvider([&](const ExtensionManagerPageInstallRequest& request) {
        {
            const std::lock_guard lock(mutex);
            install_order.push_back(request.entry.id);
        }

        if (request.entry.id == "first_install")
        {
            while (!release_first.load())
            {
                QThread::msleep(1);
            }
        }

        ExtensionManagerPageInstallResult result;
        result.success = true;
        markStartupCachesPrimed(result);
        return result;
    });

    allowUnsignedExtensionInstalls(page);
    page.loadSettings();

    auto* first_install = page.findChild<QPushButton*>(buttonObjectName("first_install", "primaryAction"));
    auto* second_install = page.findChild<QPushButton*>(buttonObjectName("second_install", "primaryAction"));
    ASSERT_NE(first_install, nullptr);
    ASSERT_NE(second_install, nullptr);

    first_install->click();
    ASSERT_TRUE(waitFor([&]() {
        const std::lock_guard lock(mutex);
        return install_order.size() == 1u;
    }));

    second_install->click();
    EXPECT_EQ(second_install->text(), QStringLiteral("pending..."));

    release_first.store(true);
    ASSERT_TRUE(waitFor(
        [&]() {
            const std::lock_guard lock(mutex);
            return install_order.size() == 2u;
        },
        2000));

    const std::lock_guard lock(mutex);
    ASSERT_EQ(install_order.size(), 2u);
    EXPECT_EQ(install_order[0], "first_install");
    EXPECT_EQ(install_order[1], "second_install");
}

TEST_F(ExtensionManagerPageTest, CancelCurrentOperationCanCancelQueuedOperations)
{
    an::ui::ExtensionManagerPage page(nullptr);
    page.setRepositoryProvider([] {
        ExtensionManagerPageRepositorySnapshot snapshot;
        snapshot.success = true;
        snapshot.indexUrl = QUrl(QStringLiteral("https://extensions.example.invalid/index.json"));
        snapshot.entries = {makeEntry("active_install", "Active Install", "Long package install", "1.0.0",
                                      ExtensionRepositoryInstallState::NotInstalled),
                            makeEntry("queued_install", "Queued Install", "Queued package install", "1.0.0",
                                      ExtensionRepositoryInstallState::NotInstalled)};
        return snapshot;
    });

    std::atomic_bool saw_cancellation = false;
    std::mutex mutex;
    std::vector<std::string> install_ids;
    page.setInstallProvider([&](const ExtensionManagerPageInstallRequest& request) {
        {
            const std::lock_guard lock(mutex);
            install_ids.push_back(request.entry.id);
        }

        if (request.entry.id == "active_install")
        {
            while (!request.cancellation_token.isCancellationRequested())
            {
                QThread::msleep(1);
            }
            saw_cancellation.store(true);
            ExtensionManagerPageInstallResult result;
            result.cancelled = true;
            result.diagnostic = QStringLiteral("Extension package installation cancelled.");
            return result;
        }

        ExtensionManagerPageInstallResult result;
        result.success = true;
        markStartupCachesPrimed(result);
        return result;
    });

    int prompt_count = 0;
    int prompted_pending_count = 0;
    page.setPendingOperationsCancellationProvider([&](QWidget*, const int pendingCount) {
        ++prompt_count;
        prompted_pending_count = pendingCount;
        return true;
    });

    allowUnsignedExtensionInstalls(page);
    page.loadSettings();

    auto* active_install = page.findChild<QPushButton*>(buttonObjectName("active_install", "primaryAction"));
    auto* queued_install = page.findChild<QPushButton*>(buttonObjectName("queued_install", "primaryAction"));
    ASSERT_NE(active_install, nullptr);
    ASSERT_NE(queued_install, nullptr);

    active_install->click();
    ASSERT_TRUE(waitFor([&]() {
        const std::lock_guard lock(mutex);
        return install_ids.size() == 1u;
    }));

    queued_install->click();
    EXPECT_EQ(queued_install->text(), QStringLiteral("pending..."));

    auto* cancel_button = page.findChild<QPushButton*>(QStringLiteral("anExtensionManagerCancelButton"));
    ASSERT_NE(cancel_button, nullptr);
    ASSERT_TRUE(waitFor([&]() { return !cancel_button->isHidden() && cancel_button->isEnabled(); }));
    cancel_button->click();

    ASSERT_TRUE(waitFor([&]() { return saw_cancellation.load(); }));
    ASSERT_TRUE(waitFor([&]() { return prompt_count == 1; }));
    EXPECT_EQ(prompted_pending_count, 1);

    auto* queue_container = page.findChild<QWidget*>(QStringLiteral("anExtensionManagerPendingQueue"));
    ASSERT_NE(queue_container, nullptr);
    EXPECT_TRUE(queue_container->isHidden());
    queued_install = page.findChild<QPushButton*>(buttonObjectName("queued_install", "primaryAction"));
    ASSERT_NE(queued_install, nullptr);
    EXPECT_NE(queued_install->text(), QStringLiteral("pending..."));
    EXPECT_TRUE(queued_install->isEnabled());

    const std::lock_guard lock(mutex);
    ASSERT_EQ(install_ids.size(), 1u);
    EXPECT_EQ(install_ids.front(), "active_install");
}

TEST_F(ExtensionManagerPageTest, DestroyingPageCancelsActiveOperation)
{
    auto page = std::make_unique<an::ui::ExtensionManagerPage>(nullptr);
    page->setRepositoryProvider([] {
        ExtensionManagerPageRepositorySnapshot snapshot;
        snapshot.success = true;
        snapshot.indexUrl = QUrl(QStringLiteral("https://extensions.example.invalid/index.json"));
        snapshot.entries = {makeEntry("active_install", "Active Install", "Long package install", "1.0.0",
                                      ExtensionRepositoryInstallState::NotInstalled)};
        return snapshot;
    });

    std::atomic_bool saw_start = false;
    std::atomic_bool saw_cancellation = false;
    page->setInstallProvider([&](const ExtensionManagerPageInstallRequest& request) {
        saw_start.store(true);
        while (!request.cancellation_token.isCancellationRequested())
        {
            QThread::msleep(1);
        }
        saw_cancellation.store(true);

        ExtensionManagerPageInstallResult result;
        result.cancelled = true;
        result.diagnostic = QStringLiteral("Extension package installation cancelled.");
        return result;
    });

    allowUnsignedExtensionInstalls(*page);
    page->loadSettings();

    auto* active_install = page->findChild<QPushButton*>(buttonObjectName("active_install", "primaryAction"));
    ASSERT_NE(active_install, nullptr);
    active_install->click();
    ASSERT_TRUE(waitFor([&]() { return saw_start.load(); }));

    page.reset();

    EXPECT_TRUE(waitFor([&]() { return saw_cancellation.load(); }));
}

TEST_F(ExtensionManagerPageTest, CancelCurrentOperationCanKeepQueuedOperations)
{
    an::ui::ExtensionManagerPage page(nullptr);
    page.setRepositoryProvider([] {
        ExtensionManagerPageRepositorySnapshot snapshot;
        snapshot.success = true;
        snapshot.indexUrl = QUrl(QStringLiteral("https://extensions.example.invalid/index.json"));
        snapshot.entries = {makeEntry("active_install", "Active Install", "Long package install", "1.0.0",
                                      ExtensionRepositoryInstallState::NotInstalled),
                            makeEntry("queued_install", "Queued Install", "Queued package install", "1.0.0",
                                      ExtensionRepositoryInstallState::NotInstalled)};
        return snapshot;
    });

    std::mutex mutex;
    std::vector<std::string> install_ids;
    page.setInstallProvider([&](const ExtensionManagerPageInstallRequest& request) {
        {
            const std::lock_guard lock(mutex);
            install_ids.push_back(request.entry.id);
        }

        if (request.entry.id == "active_install")
        {
            while (!request.cancellation_token.isCancellationRequested())
            {
                QThread::msleep(1);
            }
            ExtensionManagerPageInstallResult result;
            result.cancelled = true;
            result.diagnostic = QStringLiteral("Extension package installation cancelled.");
            return result;
        }

        ExtensionManagerPageInstallResult result;
        result.success = true;
        markStartupCachesPrimed(result);
        return result;
    });
    page.setPendingOperationsCancellationProvider([](QWidget*, int) { return false; });

    allowUnsignedExtensionInstalls(page);
    page.loadSettings();

    auto* active_install = page.findChild<QPushButton*>(buttonObjectName("active_install", "primaryAction"));
    auto* queued_install = page.findChild<QPushButton*>(buttonObjectName("queued_install", "primaryAction"));
    ASSERT_NE(active_install, nullptr);
    ASSERT_NE(queued_install, nullptr);

    active_install->click();
    ASSERT_TRUE(waitFor([&]() {
        const std::lock_guard lock(mutex);
        return install_ids.size() == 1u;
    }));

    queued_install->click();

    auto* cancel_button = page.findChild<QPushButton*>(QStringLiteral("anExtensionManagerCancelButton"));
    ASSERT_NE(cancel_button, nullptr);
    ASSERT_TRUE(waitFor([&]() { return !cancel_button->isHidden() && cancel_button->isEnabled(); }));
    cancel_button->click();

    ASSERT_TRUE(waitFor(
        [&]() {
            const std::lock_guard lock(mutex);
            return install_ids.size() == 2u;
        },
        2000));

    const std::lock_guard lock(mutex);
    ASSERT_EQ(install_ids.size(), 2u);
    EXPECT_EQ(install_ids[0], "active_install");
    EXPECT_EQ(install_ids[1], "queued_install");
}

TEST_F(ExtensionManagerPageTest, LocalPackageActionInvokesProviderWithSelectedArchiveAndUserInstallRoot)
{
    QTemporaryDir temp_dir;
    ASSERT_TRUE(temp_dir.isValid());

    const auto temp_root = std::filesystem::path(temp_dir.path().toStdString());
    const auto archive_path = temp_root / "local-package.tar.zst";
    const auto user_root = temp_root / "user-extensions";
    writeTextFile(archive_path, "not a real package; provider is injected");

    ExtensionDiscoveryOptions discovery_options;
    discovery_options.user_roots.push_back(user_root);
    discovery_options.context.runtime_target = "test-runtime";

    an::ui::ExtensionManagerPage page(nullptr);
    page.setDiscoveryOptionsProvider([&]() { return discovery_options; });
    page.setRepositoryProvider([] {
        ExtensionManagerPageRepositorySnapshot snapshot;
        snapshot.success = true;
        return snapshot;
    });
    page.setLocalPackagePathProvider([&](QWidget*) { return QString::fromStdString(archive_path.string()); });

    std::mutex mutex;
    std::optional<ExtensionManagerPageLocalInstallRequest> captured_request;
    page.setLocalInstallProvider([&](const ExtensionManagerPageLocalInstallRequest& request) {
        {
            const std::lock_guard lock(mutex);
            captured_request = request;
        }

        ExtensionManagerPageInstallResult result;
        result.success = true;
        result.diagnostic = QStringLiteral("Installed local package.");
        markStartupCachesPrimed(result);
        return result;
    });

    allowUnsignedExtensionInstalls(page);
    page.loadSettings();

    auto* more_actions_button = page.findChild<QToolButton*>(QStringLiteral("anExtensionManagerMoreActionsButton"));
    ASSERT_NE(more_actions_button, nullptr);
    ASSERT_TRUE(more_actions_button->isEnabled());

    auto* local_action = findMoreActionsMenuAction(page, QStringLiteral("Install local package..."));
    ASSERT_NE(local_action, nullptr);
    ASSERT_TRUE(local_action->isEnabled());
    local_action->trigger();

    ASSERT_TRUE(waitFor([&]() {
        const std::lock_guard lock(mutex);
        return captured_request.has_value();
    }));

    ExtensionManagerPageLocalInstallRequest request;
    {
        const std::lock_guard lock(mutex);
        request = *captured_request;
    }

    EXPECT_EQ(request.archivePath, archive_path);
    EXPECT_EQ(request.installRoot, user_root);
    EXPECT_EQ(request.context.runtime_target, "test-runtime");

    auto* summary_banner = dynamic_cast<an::ui::AnInfoBanner*>(
        page.findChild<QWidget*>(QStringLiteral("anExtensionManagerSummaryBanner")));
    ASSERT_NE(summary_banner, nullptr);
    ASSERT_TRUE(waitFor([&]() {
        return summary_banner->message().contains(QStringLiteral("Installed local package"));
    })) << summary_banner->message().toStdString();
    EXPECT_TRUE(summary_banner->message().contains(QStringLiteral("Installed local package")));
    EXPECT_TRUE(summary_banner->message().contains(QStringLiteral("Startup caches primed")));
}

TEST_F(ExtensionManagerPageTest, LocalPackageInstallKeepsNewerVersionWhenRepositoryRefreshIsStale)
{
    QTemporaryDir temp_dir;
    ASSERT_TRUE(temp_dir.isValid());

    const auto temp_root = std::filesystem::path(temp_dir.path().toStdString());
    const auto archive_path = temp_root / "cosmic-clarity-dark-star-1.0.1.tar.zst";
    const auto user_root = temp_root / "user-extensions";
    writeTextFile(archive_path, "not a real package; provider is injected");

    ExtensionDiscoveryOptions discovery_options;
    discovery_options.user_roots.push_back(user_root);
    discovery_options.context.runtime_target = "test-runtime";

    an::ui::ExtensionManagerPage page(nullptr);
    page.setDiscoveryOptionsProvider([&]() { return discovery_options; });

    std::atomic_int repository_calls = 0;
    page.setRepositoryProvider([&]() {
        ++repository_calls;
        ExtensionManagerPageRepositorySnapshot snapshot;
        snapshot.success = true;
        snapshot.entries = {makeEntry("cosmic.clarity.dark.star", "Cosmic Clarity Dark Star", "Starless generator",
                                      "1.0.0", ExtensionRepositoryInstallState::Installed, "1.0.0")};
        return snapshot;
    });
    page.setLocalPackagePathProvider([&](QWidget*) { return QString::fromStdString(archive_path.string()); });
    page.setLocalInstallProvider([&](const ExtensionManagerPageLocalInstallRequest&) {
        ExtensionManagerPageInstallResult result;
        result.success = true;
        result.diagnostic = QStringLiteral("Installed local package Cosmic Clarity Dark Star 1.0.1.");

        an::ui::extensions::ExtensionManifest manifest;
        manifest.id = "cosmic.clarity.dark.star";
        manifest.name = "Cosmic Clarity Dark Star";
        manifest.version = "1.0.1";
        result.transaction.validation.load_result.manifest = manifest;
        result.transaction.validation.load_result.status = an::ui::extensions::ExtensionLoadStatus::Ready;
        markStartupCachesPrimed(result);
        return result;
    });

    allowUnsignedExtensionInstalls(page);
    page.loadSettings();
    ASSERT_EQ(repository_calls.load(), 1);

    auto* local_action = findMoreActionsMenuAction(page, QStringLiteral("Install local package..."));
    ASSERT_NE(local_action, nullptr);
    local_action->trigger();

    ASSERT_TRUE(waitFor([&]() { return repository_calls.load() >= 2; }));

    auto* version_label = page.findChild<QLabel*>(fieldObjectName("cosmic.clarity.dark.star", "version"));
    ASSERT_NE(version_label, nullptr);
    EXPECT_TRUE(version_label->text().contains(QStringLiteral("Installed: 1.0.1")))
        << version_label->text().toStdString();
    EXPECT_FALSE(version_label->text().contains(QStringLiteral("Installed: 1.0.0")))
        << version_label->text().toStdString();

    auto* version_badge = page.findChild<QLabel*>(fieldObjectName("cosmic.clarity.dark.star", "versionBadge"));
    ASSERT_NE(version_badge, nullptr);
    EXPECT_TRUE(version_badge->text().contains(QStringLiteral("1.0.1"))) << version_badge->text().toStdString();
}

TEST_F(ExtensionManagerPageTest, LocalPackageActionPersistsSelectedArchiveDirectory)
{
    QTemporaryDir temp_dir;
    ASSERT_TRUE(temp_dir.isValid());

    const auto temp_root = std::filesystem::path(temp_dir.path().toStdString());
    const auto archive_path = temp_root / "downloaded-packages" / "local-package.tar.zst";
    writeTextFile(archive_path, "not a real package; provider is injected");

    InMemorySettings settings;
    ASSERT_TRUE(settings.ready());
    const QString setting_key = QStringLiteral("extensions/lastLocalPackageDirectory");

    ExtensionDiscoveryOptions discovery_options;
    discovery_options.user_roots.push_back(temp_root / "user-extensions");
    discovery_options.context.runtime_target = "test-runtime";

    an::ui::ExtensionManagerPage page(settings.persistence());
    page.setDiscoveryOptionsProvider([&]() { return discovery_options; });
    page.setRepositoryProvider([] {
        ExtensionManagerPageRepositorySnapshot snapshot;
        snapshot.success = true;
        return snapshot;
    });
    page.setLocalPackagePathProvider([&](QWidget*) { return QString::fromStdString(archive_path.string()); });
    std::atomic_bool installed = false;
    page.setLocalInstallProvider([&](const ExtensionManagerPageLocalInstallRequest&) {
        installed.store(true);
        ExtensionManagerPageInstallResult result;
        result.success = true;
        result.diagnostic = QStringLiteral("Installed local package.");
        markStartupCachesPrimed(result);
        return result;
    });

    allowUnsignedExtensionInstalls(page);
    page.loadSettings();
    EXPECT_FALSE(settings.persistence()->selectOptional(setting_key).has_value());

    auto* local_action = findMoreActionsMenuAction(page, QStringLiteral("Install local package..."));
    ASSERT_NE(local_action, nullptr);
    local_action->trigger();
    ASSERT_TRUE(waitFor([&]() { return installed.load(); }));

    const QString expected_directory =
        QFileInfo(QString::fromStdString(archive_path.parent_path().string())).absoluteFilePath();
    EXPECT_EQ(QDir::cleanPath(settings.persistence()->select(setting_key)), QDir::cleanPath(expected_directory));

    auto* summary_banner = dynamic_cast<an::ui::AnInfoBanner*>(
        page.findChild<QWidget*>(QStringLiteral("anExtensionManagerSummaryBanner")));
    ASSERT_NE(summary_banner, nullptr);
    EXPECT_TRUE(
        waitFor([&]() { return summary_banner->message().contains(QStringLiteral("Installed local package")); }));
}

TEST_F(ExtensionManagerPageTest, LocalPackageFileDialogOpensInTheLastArchiveDirectory)
{
    QTemporaryDir temp_dir;
    ASSERT_TRUE(temp_dir.isValid());
    const QString last_directory = QDir(temp_dir.path()).filePath(QStringLiteral("previous-packages"));
    ASSERT_TRUE(QDir().mkpath(last_directory));

    InMemorySettings settings;
    ASSERT_TRUE(settings.ready());
    ASSERT_TRUE(settings.persistence()->upsert(QStringLiteral("extensions/lastLocalPackageDirectory"), last_directory));
    // The application's own file dialog, which a case can find and close, rather than the system's.
    ASSERT_TRUE(settings.persistence()->upsert(QString::fromLatin1(SettingsKeys::INTERFACE_USE_SYSTEM_FILE_BROWSER),
                                               QStringLiteral("0")));

    an::ui::ExtensionManagerPage page(settings.persistence());
    page.setDiscoveryOptionsProvider([]() { return buildUiOnlyDiscoveryOptions(); });
    page.setRepositoryProvider([] {
        ExtensionManagerPageRepositorySnapshot snapshot;
        snapshot.success = true;
        return snapshot;
    });
    std::atomic_bool install_requested = false;
    page.setLocalInstallProvider([&](const ExtensionManagerPageLocalInstallRequest&) {
        install_requested.store(true);
        return ExtensionManagerPageInstallResult{};
    });
    allowUnsignedExtensionInstalls(page);
    page.loadSettings();

    QString dialog_directory;
    bool dialog_found = false;
    auto attempts = std::make_shared<int>(500);
    auto poll = std::make_shared<std::function<void()>>();
    *poll = [&dialog_directory, &dialog_found, attempts, weak_poll = std::weak_ptr<std::function<void()>>(poll)]() {
        for (QWidget* widget : QApplication::topLevelWidgets())
        {
            if (auto* dialog = qobject_cast<QFileDialog*>(widget); dialog && dialog->isVisible())
            {
                dialog_directory = dialog->directory().absolutePath();
                dialog_found = true;
                dialog->reject();
                return;
            }
        }
        if (--(*attempts) > 0)
        {
            if (const auto next = weak_poll.lock())
            {
                QTimer::singleShot(10, *next);
            }
        }
    };
    QTimer::singleShot(0, *poll);

    auto* local_action = findMoreActionsMenuAction(page, QStringLiteral("Install local package..."));
    ASSERT_NE(local_action, nullptr);
    local_action->trigger();

    ASSERT_TRUE(dialog_found);
    EXPECT_EQ(QDir::cleanPath(dialog_directory), QDir::cleanPath(QFileInfo(last_directory).absoluteFilePath()));

    // Closing the dialog without choosing a package installs nothing.
    QCoreApplication::processEvents();
    EXPECT_FALSE(install_requested.load());
}

TEST_F(ExtensionManagerPageTest, PrimaryActionInstallsFromLocalFixtureRepositoryWithDefaultProvider)
{
    if (!an::ui::extensions::extension_subsystem_supported_in_current_build())
    {
        GTEST_SKIP() << "Extension runtime host support is not available in this build.";
    }

    QTemporaryDir temp_dir;
    ASSERT_TRUE(temp_dir.isValid());

    const auto temp_root = std::filesystem::path(temp_dir.path().toStdString());
    const auto repository_root = temp_root / "repository";
    const auto install_root = temp_root / "installed";
    const auto environments_root = temp_root / "ExtensionEnvs";
    const auto archive_path = repository_root / "archives" / "page_e2e-1.0.0.tar";
    const auto index_path = repository_root / "index.json";
    const std::string runtime_target = an::ui::extensions::current_runtime_target();

    const std::string manifest = R"json({
      "id": "page.e2e",
      "name": "Page E2E",
      "version": "1.0.0",
      "summary": "Extension Manager page end-to-end fixture.",
      "description": "Fixture package installed through ExtensionManagerPage's default provider.",
      "author": "AfterNight Tests",
      "license": "GPL-3.0-or-later",
      "publisher_id": "afternight.tests",
      "type": "python",
      "entry_point": "page_e2e",
      "process_class": "PageE2EExtension",
      "category": "filters",
      "launch_mode": "single_image",
      "package_format_version": 1,
      "protocol_version": 1,
      "sdk_version": 1
    })json";
    createTarArchive(archive_path, {{"page_e2e/extension.json", manifest, '0'},
                                    {"page_e2e/page_e2e.py",
                                     "import afternight\n\n"
                                     "class PageE2EExtension(afternight.ProcessExtension):\n"
                                     "    def get_settings_params(self):\n"
                                     "        return [{'id': 'gain', 'type': 'float', 'default': 1.0}]\n",
                                     '0'}});
    const std::string archive_hash = sha256FileHex(archive_path);

    QJsonObject asset;
    asset.insert(QStringLiteral("name"), QStringLiteral("page_e2e-1.0.0.tar"));
    asset.insert(QStringLiteral("download_url"),
                 QUrl::fromLocalFile(QString::fromStdString(archive_path.string())).toString());
    asset.insert(QStringLiteral("package_hash"), QString::fromStdString("sha256:" + archive_hash));
    asset.insert(QStringLiteral("runtime_targets"), jsonStringArray({runtime_target}));
    asset.insert(QStringLiteral("signature_state"), QStringLiteral("unsigned"));
    asset.insert(QStringLiteral("signature_detail"), QStringLiteral("Unsigned fixture asset; SHA-256 verified."));

    QJsonArray assets;
    assets.append(asset);

    QJsonObject release;
    release.insert(QStringLiteral("version"), QStringLiteral("1.0.0"));
    release.insert(QStringLiteral("min_app_version"), QStringLiteral("0.0.0"));
    release.insert(QStringLiteral("package_format_version"), 1);
    release.insert(QStringLiteral("protocol_version"), 1);
    release.insert(QStringLiteral("sdk_version"), 1);
    release.insert(QStringLiteral("sdk_backend"), QStringLiteral("runtime"));
    release.insert(QStringLiteral("launch_mode"), QStringLiteral("single_image"));
    release.insert(QStringLiteral("runtime_targets"), jsonStringArray({runtime_target}));
    release.insert(QStringLiteral("published_at"), QStringLiteral("2026-04-27T00:00:00Z"));
    release.insert(QStringLiteral("assets"), assets);

    QJsonArray releases;
    releases.append(release);

    QJsonObject package;
    package.insert(QStringLiteral("id"), QStringLiteral("page.e2e"));
    package.insert(QStringLiteral("name"), QStringLiteral("Page E2E"));
    package.insert(QStringLiteral("summary"), QStringLiteral("Extension Manager page end-to-end fixture."));
    package.insert(QStringLiteral("description"),
                   QStringLiteral("Fixture package installed through ExtensionManagerPage's default provider."));
    package.insert(QStringLiteral("author"), QStringLiteral("AfterNight Tests"));
    package.insert(QStringLiteral("license"), QStringLiteral("GPL-3.0-or-later"));
    package.insert(QStringLiteral("publisher_id"), QStringLiteral("afternight.tests"));
    package.insert(QStringLiteral("latest_version"), QStringLiteral("1.0.0"));
    package.insert(QStringLiteral("tags"), jsonStringArray({"fixture", "install"}));
    package.insert(QStringLiteral("releases"), releases);

    QJsonArray packages;
    packages.append(package);

    QJsonObject index;
    index.insert(QStringLiteral("schema_version"), 1);
    index.insert(QStringLiteral("repository"), QStringLiteral("afternight-extension-page-fixture"));
    index.insert(QStringLiteral("official"), true);
    index.insert(QStringLiteral("updated_at"), QStringLiteral("2026-04-27T00:00:00Z"));
    index.insert(QStringLiteral("extensions"), packages);
    writeJsonFile(index_path, index);

    ExtensionDiscoveryOptions discovery_options = buildPageE2EDiscoveryOptions(install_root, environments_root);
    AN_APP_REQUIRE_FULL_EXTENSION_HOST(discovery_options.context.extension_host_executable);
    ASSERT_TRUE(std::filesystem::exists(discovery_options.context.bundled_python_executable))
        << discovery_options.context.bundled_python_executable;
    ASSERT_TRUE(std::filesystem::exists(discovery_options.context.runtime_root))
        << discovery_options.context.runtime_root;

    an::ui::ExtensionManagerPage page(nullptr);
    page.setDiscoveryOptionsProvider([&]() { return discovery_options; });

    int repository_calls = 0;
    page.setRepositoryProvider([&]() {
        ++repository_calls;
        auto snapshot = buildFixtureRepositorySnapshot(index_path, discovery_options);
        snapshot.indexUrl =
            QUrl(QStringLiteral("https://example.invalid/afternight-extension-page-fixture/index.json"));
        return snapshot;
    });

    allowUnsignedExtensionInstalls(page);
    page.loadSettings();

    auto* install_button = page.findChild<QPushButton*>(buttonObjectName("page.e2e", "primaryAction"));
    ASSERT_NE(install_button, nullptr);
    ASSERT_TRUE(install_button->isEnabled());
    install_button->click();

    auto* summary_banner = dynamic_cast<an::ui::AnInfoBanner*>(
        page.findChild<QWidget*>(QStringLiteral("anExtensionManagerSummaryBanner")));
    ASSERT_NE(summary_banner, nullptr);
    ASSERT_TRUE(waitFor([&]() { return summary_banner->message().contains(QStringLiteral("Installed Page E2E")); },
                        60000 + hostExitAllowanceMs()))
        << summary_banner->message().toStdString();
    EXPECT_TRUE(summary_banner->message().contains(QStringLiteral("Startup caches primed")));
    EXPECT_GE(repository_calls, 2);

    auto* state_label = page.findChild<QLabel*>(fieldObjectName("page.e2e", "state"));
    ASSERT_NE(state_label, nullptr);
    EXPECT_EQ(state_label->text(), QStringLiteral("Repository installed"));

    auto* status_label = page.findChild<QLabel*>(fieldObjectName("page.e2e", "status"));
    ASSERT_NE(status_label, nullptr);
    EXPECT_EQ(status_label->text(), QStringLiteral("Ready"));

    ExtensionManager manager;
    const auto discovered = manager.discoverExtensions(discovery_options);
    ASSERT_EQ(discovered.size(), 1u);
    ASSERT_TRUE(discovered.front().manifest.has_value());
    EXPECT_EQ(discovered.front().manifest->id, "page.e2e");
    EXPECT_EQ(discovered.front().manifest->version, "1.0.0");
    ASSERT_TRUE(discovered.front().environment.has_value());
    EXPECT_EQ(discovered.front().environment->state, ExtensionEnvironmentState::Ready);
    ASSERT_TRUE(discovered.front().settings_schema.has_value());
    EXPECT_EQ(discovered.front().settings_schema->state, ExtensionSettingsSchemaState::Ready);
    EXPECT_TRUE(discovered.front().settings_schema->from_cache);
    EXPECT_EQ(discovered.front().provenance.archive_hash_sha256, archive_hash);
    EXPECT_TRUE(readTextFile(discovered.front().package_root / "extension_package_receipt.json").find(archive_hash) !=
                std::string::npos);
}

TEST_F(ExtensionManagerPageTest, LocalPackageDefaultProviderInstallsUserPackageOverRepositoryCopy)
{
    if (!an::ui::extensions::extension_subsystem_supported_in_current_build())
    {
        GTEST_SKIP() << "Extension runtime host support is not available in this build.";
    }

    QTemporaryDir temp_dir;
    ASSERT_TRUE(temp_dir.isValid());

    const auto temp_root = std::filesystem::path(temp_dir.path().toStdString());
    const auto repository_install_root = temp_root / "repository-installed";
    const auto user_install_root = temp_root / "user-installed";
    const auto environments_root = temp_root / "ExtensionEnvs";
    const auto archive_path = temp_root / "archives" / "local_e2e-1.0.0.tar";

    const std::string manifest_old = R"json({
      "id": "local.e2e",
      "name": "Local E2E",
      "version": "0.5.0",
      "summary": "Older repository copy.",
      "description": "Older repository copy.",
      "author": "AfterNight Tests",
      "license": "GPL-3.0-or-later",
      "publisher_id": "afternight.tests",
      "type": "python",
      "entry_point": "local_e2e",
      "process_class": "LocalE2EExtension",
      "category": "filters",
      "launch_mode": "single_image",
      "package_format_version": 1,
      "protocol_version": 1,
      "sdk_version": 1
    })json";
    const std::string manifest_new = R"json({
      "id": "local.e2e",
      "name": "Local E2E",
      "version": "1.0.0",
      "summary": "Local package install fixture.",
      "description": "Local package install fixture.",
      "author": "AfterNight Tests",
      "license": "GPL-3.0-or-later",
      "publisher_id": "afternight.tests",
      "type": "python",
      "entry_point": "local_e2e",
      "process_class": "LocalE2EExtension",
      "category": "filters",
      "launch_mode": "single_image",
      "package_format_version": 1,
      "protocol_version": 1,
      "sdk_version": 1
    })json";
    const std::string module = "import afternight\n\n"
                               "class LocalE2EExtension(afternight.ProcessExtension):\n"
                               "    def get_settings_params(self):\n"
                               "        return [{'id': 'gain', 'type': 'float', 'default': 1.0}]\n";

    writeTextFile(repository_install_root / "local_e2e" / "extension.json", manifest_old);
    writeTextFile(repository_install_root / "local_e2e" / "local_e2e.py", module);
    createTarArchive(archive_path,
                     {{"local_e2e/extension.json", manifest_new, '0'}, {"local_e2e/local_e2e.py", module, '0'}});
    const std::string archive_hash = sha256FileHex(archive_path);

    ExtensionDiscoveryOptions discovery_options =
        buildPageE2EDiscoveryOptions(repository_install_root, environments_root);
    discovery_options.user_roots.push_back(user_install_root);
    AN_APP_REQUIRE_FULL_EXTENSION_HOST(discovery_options.context.extension_host_executable);
    ASSERT_TRUE(std::filesystem::exists(discovery_options.context.bundled_python_executable))
        << discovery_options.context.bundled_python_executable;
    ASSERT_TRUE(std::filesystem::exists(discovery_options.context.runtime_root))
        << discovery_options.context.runtime_root;

    an::ui::ExtensionManagerPage page(nullptr);
    page.setDiscoveryOptionsProvider([&]() { return discovery_options; });
    page.setRepositoryProvider([] {
        ExtensionManagerPageRepositorySnapshot snapshot;
        snapshot.success = true;
        return snapshot;
    });
    page.setLocalPackagePathProvider([&](QWidget*) { return QString::fromStdString(archive_path.string()); });

    allowUnsignedExtensionInstalls(page);
    page.loadSettings();

    auto* more_actions_button = page.findChild<QToolButton*>(QStringLiteral("anExtensionManagerMoreActionsButton"));
    ASSERT_NE(more_actions_button, nullptr);
    ASSERT_TRUE(more_actions_button->isEnabled());

    auto* local_action = findMoreActionsMenuAction(page, QStringLiteral("Install local package..."));
    ASSERT_NE(local_action, nullptr);
    ASSERT_TRUE(local_action->isEnabled());
    local_action->trigger();

    auto* summary_banner = dynamic_cast<an::ui::AnInfoBanner*>(
        page.findChild<QWidget*>(QStringLiteral("anExtensionManagerSummaryBanner")));
    ASSERT_NE(summary_banner, nullptr);
    ASSERT_TRUE(waitFor([&]() { return summary_banner->message().contains(QStringLiteral("Installed local package")); },
                        60000 + hostExitAllowanceMs()))
        << summary_banner->message().toStdString();
    EXPECT_TRUE(summary_banner->message().contains(QStringLiteral("Local E2E")));
    EXPECT_TRUE(summary_banner->message().contains(QStringLiteral("Startup caches primed")));

    ExtensionManager manager;
    const auto discovered = manager.discoverExtensions(discovery_options);
    const auto installed = findDiscoveredExtension(discovered, "local.e2e");
    ASSERT_TRUE(installed.has_value());
    ASSERT_TRUE(installed->manifest.has_value());
    EXPECT_EQ(installed->source, ExtensionPackageSource::User);
    EXPECT_EQ(installed->manifest->version, "1.0.0");
    EXPECT_EQ(installed->provenance.local_package_path, archive_path.string());
    EXPECT_EQ(installed->provenance.archive_hash_sha256, archive_hash);
    ASSERT_TRUE(installed->environment.has_value());
    EXPECT_EQ(installed->environment->state, ExtensionEnvironmentState::Ready);
    ASSERT_TRUE(installed->settings_schema.has_value());
    EXPECT_EQ(installed->settings_schema->state, ExtensionSettingsSchemaState::Ready);
    EXPECT_TRUE(installed->settings_schema->from_cache);
    EXPECT_TRUE(std::filesystem::exists(repository_install_root / "local_e2e" / "extension.json"));
}

TEST_F(ExtensionManagerPageTest, PrimaryActionUpdatesFromLocalFixtureRepositoryWithDefaultProvider)
{
    if (!an::ui::extensions::extension_subsystem_supported_in_current_build())
    {
        GTEST_SKIP() << "Extension runtime host support is not available in this build.";
    }

    QTemporaryDir temp_dir;
    ASSERT_TRUE(temp_dir.isValid());

    const auto temp_root = std::filesystem::path(temp_dir.path().toStdString());
    const auto repository_root = temp_root / "repository";
    const auto install_root = temp_root / "installed";
    const auto environments_root = temp_root / "ExtensionEnvs";
    const auto archive_v1_path = repository_root / "archives" / "page_e2e_update-1.0.0.tar";
    const auto archive_v2_path = repository_root / "archives" / "page_e2e_update-1.1.0.tar";
    const auto index_path = repository_root / "index.json";
    const std::string runtime_target = an::ui::extensions::current_runtime_target();

    // Both fixture versions deliberately share one dependency context. This test exercises
    // the repository update and cache-refresh path; duplicating the deployed PySide/Qt
    // runtime into a private environment for each version can exceed a test runner's temp quota.
    const std::string manifest_v1 = R"json({
      "id": "page.e2e.update",
      "name": "Page E2E Update",
      "version": "1.0.0",
      "summary": "Extension Manager page update fixture.",
      "description": "Fixture package updated through ExtensionManagerPage's default provider.",
      "author": "AfterNight Tests",
      "license": "GPL-3.0-or-later",
      "publisher_id": "afternight.tests",
      "type": "python",
      "entry_point": "page_e2e_update",
      "process_class": "PageE2EUpdateExtension",
      "category": "filters",
      "launch_mode": "single_image",
      "package_format_version": 1,
      "protocol_version": 1,
      "sdk_version": 1,
      "dependencies": {
        "dependency_context": "shared_group",
        "shared_group": "page_e2e_update"
      }
    })json";
    const std::string manifest_v2 = R"json({
      "id": "page.e2e.update",
      "name": "Page E2E Update",
      "version": "1.1.0",
      "summary": "Extension Manager page update fixture.",
      "description": "Fixture package updated through ExtensionManagerPage's default provider.",
      "author": "AfterNight Tests",
      "license": "GPL-3.0-or-later",
      "publisher_id": "afternight.tests",
      "type": "python",
      "entry_point": "page_e2e_update",
      "process_class": "PageE2EUpdateExtension",
      "category": "filters",
      "launch_mode": "single_image",
      "package_format_version": 1,
      "protocol_version": 1,
      "sdk_version": 1,
      "dependencies": {
        "dependency_context": "shared_group",
        "shared_group": "page_e2e_update"
      }
    })json";
    const std::string extension_v1 = "import afternight\n\n"
                                     "class PageE2EUpdateExtension(afternight.ProcessExtension):\n"
                                     "    VALUE = 'v1'\n"
                                     "    def get_settings_params(self):\n"
                                     "        return [{'id': 'gain', 'type': 'float', 'default': 1.0}]\n";
    const std::string extension_v2 = "import afternight\n\n"
                                     "class PageE2EUpdateExtension(afternight.ProcessExtension):\n"
                                     "    VALUE = 'v2'\n"
                                     "    def get_settings_params(self):\n"
                                     "        return [{'id': 'gain', 'type': 'float', 'default': 2.0}]\n";

    createTarArchive(archive_v1_path, {{"page_e2e_update/extension.json", manifest_v1, '0'},
                                       {"page_e2e_update/page_e2e_update.py", extension_v1, '0'}});
    createTarArchive(archive_v2_path, {{"page_e2e_update/extension.json", manifest_v2, '0'},
                                       {"page_e2e_update/page_e2e_update.py", extension_v2, '0'}});
    const std::string archive_v1_hash = sha256FileHex(archive_v1_path);
    const std::string archive_v2_hash = sha256FileHex(archive_v2_path);

    const auto makeRelease = [&](const QString& version, const std::filesystem::path& archive_path,
                                 const std::string& archive_hash) {
        QJsonObject asset;
        asset.insert(QStringLiteral("name"), QStringLiteral("page_e2e_update-%1.tar").arg(version));
        asset.insert(QStringLiteral("download_url"),
                     QUrl::fromLocalFile(QString::fromStdString(archive_path.string())).toString());
        asset.insert(QStringLiteral("package_hash"), QString::fromStdString("sha256:" + archive_hash));
        asset.insert(QStringLiteral("runtime_targets"), jsonStringArray({runtime_target}));
        asset.insert(QStringLiteral("signature_state"), QStringLiteral("unsigned"));
        asset.insert(QStringLiteral("signature_detail"), QStringLiteral("Unsigned fixture asset; SHA-256 verified."));

        QJsonArray assets;
        assets.append(asset);

        QJsonObject release;
        release.insert(QStringLiteral("version"), version);
        release.insert(QStringLiteral("min_app_version"), QStringLiteral("0.0.0"));
        release.insert(QStringLiteral("package_format_version"), 1);
        release.insert(QStringLiteral("protocol_version"), 1);
        release.insert(QStringLiteral("sdk_version"), 1);
        release.insert(QStringLiteral("sdk_backend"), QStringLiteral("runtime"));
        release.insert(QStringLiteral("launch_mode"), QStringLiteral("single_image"));
        release.insert(QStringLiteral("runtime_targets"), jsonStringArray({runtime_target}));
        release.insert(QStringLiteral("published_at"), QStringLiteral("2026-04-27T00:00:00Z"));
        release.insert(QStringLiteral("assets"), assets);
        return release;
    };

    const auto writeIndex = [&](const QString& latest_version, const bool include_update) {
        QJsonArray releases;
        releases.append(makeRelease(QStringLiteral("1.0.0"), archive_v1_path, archive_v1_hash));
        if (include_update)
        {
            releases.append(makeRelease(QStringLiteral("1.1.0"), archive_v2_path, archive_v2_hash));
        }

        QJsonObject package;
        package.insert(QStringLiteral("id"), QStringLiteral("page.e2e.update"));
        package.insert(QStringLiteral("name"), QStringLiteral("Page E2E Update"));
        package.insert(QStringLiteral("summary"), QStringLiteral("Extension Manager page update fixture."));
        package.insert(QStringLiteral("description"),
                       QStringLiteral("Fixture package updated through ExtensionManagerPage's default provider."));
        package.insert(QStringLiteral("author"), QStringLiteral("AfterNight Tests"));
        package.insert(QStringLiteral("license"), QStringLiteral("GPL-3.0-or-later"));
        package.insert(QStringLiteral("publisher_id"), QStringLiteral("afternight.tests"));
        package.insert(QStringLiteral("latest_version"), latest_version);
        package.insert(QStringLiteral("tags"), jsonStringArray({"fixture", "update"}));
        package.insert(QStringLiteral("releases"), releases);

        QJsonArray packages;
        packages.append(package);

        QJsonObject index;
        index.insert(QStringLiteral("schema_version"), 1);
        index.insert(QStringLiteral("repository"), QStringLiteral("afternight-extension-page-update-fixture"));
        index.insert(QStringLiteral("official"), true);
        index.insert(QStringLiteral("updated_at"), QStringLiteral("2026-04-27T00:00:00Z"));
        index.insert(QStringLiteral("extensions"), packages);
        writeJsonFile(index_path, index);
    };
    writeIndex(QStringLiteral("1.0.0"), false);

    ExtensionDiscoveryOptions discovery_options = buildPageE2EDiscoveryOptions(install_root, environments_root);
    AN_APP_REQUIRE_FULL_EXTENSION_HOST(discovery_options.context.extension_host_executable);
    ASSERT_TRUE(std::filesystem::exists(discovery_options.context.bundled_python_executable))
        << discovery_options.context.bundled_python_executable;
    ASSERT_TRUE(std::filesystem::exists(discovery_options.context.runtime_root))
        << discovery_options.context.runtime_root;

    an::ui::ExtensionManagerPage page(nullptr);
    page.setDiscoveryOptionsProvider([&]() { return discovery_options; });

    int repository_calls = 0;
    page.setRepositoryProvider([&]() {
        ++repository_calls;
        auto snapshot = buildFixtureRepositorySnapshot(index_path, discovery_options);
        snapshot.indexUrl =
            QUrl(QStringLiteral("https://example.invalid/afternight-extension-page-update-fixture/index.json"));
        return snapshot;
    });

    allowUnsignedExtensionInstalls(page);
    page.loadSettings();

    auto* install_button = page.findChild<QPushButton*>(buttonObjectName("page.e2e.update", "primaryAction"));
    ASSERT_NE(install_button, nullptr);
    ASSERT_TRUE(install_button->isEnabled());
    install_button->click();

    auto* summary_banner = dynamic_cast<an::ui::AnInfoBanner*>(
        page.findChild<QWidget*>(QStringLiteral("anExtensionManagerSummaryBanner")));
    ASSERT_NE(summary_banner, nullptr);
    ASSERT_TRUE(
        waitFor([&]() { return summary_banner->message().contains(QStringLiteral("Installed Page E2E Update")); },
                60000 + hostExitAllowanceMs()))
        << summary_banner->message().toStdString();
    EXPECT_TRUE(summary_banner->message().contains(QStringLiteral("Startup caches primed")));

    writeIndex(QStringLiteral("1.1.0"), true);
    page.loadSettings();

    auto* update_state_label = page.findChild<QLabel*>(fieldObjectName("page.e2e.update", "state"));
    ASSERT_NE(update_state_label, nullptr);
    ASSERT_EQ(update_state_label->text(), QStringLiteral("Update available"));

    auto* update_version_label = page.findChild<QLabel*>(fieldObjectName("page.e2e.update", "version"));
    ASSERT_NE(update_version_label, nullptr);
    EXPECT_TRUE(update_version_label->text().contains(QStringLiteral("Latest: 1.1.0")));
    EXPECT_TRUE(update_version_label->text().contains(QStringLiteral("Installed: 1.0.0")));

    auto* update_button = page.findChild<QPushButton*>(buttonObjectName("page.e2e.update", "primaryAction"));
    ASSERT_NE(update_button, nullptr);
    ASSERT_TRUE(update_button->isEnabled());
    ASSERT_EQ(update_button->text(), QStringLiteral("Update to 1.1.0"));
    update_button->click();

    ASSERT_TRUE(waitFor([&]() { return summary_banner->message().contains(QStringLiteral("Updated Page E2E Update")); },
                        60000 + hostExitAllowanceMs()))
        << summary_banner->message().toStdString();
    EXPECT_TRUE(summary_banner->message().contains(QStringLiteral("1.1.0")));
    EXPECT_TRUE(summary_banner->message().contains(QStringLiteral("Startup caches primed")));
    EXPECT_GE(repository_calls, 4);

    auto* installed_state_label = page.findChild<QLabel*>(fieldObjectName("page.e2e.update", "state"));
    ASSERT_NE(installed_state_label, nullptr);
    EXPECT_EQ(installed_state_label->text(), QStringLiteral("Repository installed"));

    auto* installed_status_label = page.findChild<QLabel*>(fieldObjectName("page.e2e.update", "status"));
    ASSERT_NE(installed_status_label, nullptr);
    EXPECT_EQ(installed_status_label->text(), QStringLiteral("Ready"));

    auto* installed_version_label = page.findChild<QLabel*>(fieldObjectName("page.e2e.update", "version"));
    ASSERT_NE(installed_version_label, nullptr);
    EXPECT_TRUE(installed_version_label->text().contains(QStringLiteral("Latest: 1.1.0")));
    EXPECT_TRUE(installed_version_label->text().contains(QStringLiteral("Installed: 1.1.0")));

    ExtensionManager manager;
    const auto discovered = manager.discoverExtensions(discovery_options);
    ASSERT_EQ(discovered.size(), 1u);
    ASSERT_TRUE(discovered.front().manifest.has_value());
    EXPECT_EQ(discovered.front().manifest->id, "page.e2e.update");
    EXPECT_EQ(discovered.front().manifest->version, "1.1.0");
    ASSERT_TRUE(discovered.front().environment.has_value());
    EXPECT_EQ(discovered.front().environment->state, ExtensionEnvironmentState::Ready);
    ASSERT_TRUE(discovered.front().settings_schema.has_value());
    EXPECT_EQ(discovered.front().settings_schema->state, ExtensionSettingsSchemaState::Ready);
    EXPECT_TRUE(discovered.front().settings_schema->from_cache);
    EXPECT_EQ(discovered.front().provenance.archive_hash_sha256, archive_v2_hash);

    const auto activated_module = discovered.front().package_root / "page_e2e_update.py";
    ASSERT_TRUE(std::filesystem::exists(activated_module));
    EXPECT_NE(readTextFile(activated_module).find("VALUE = 'v2'"), std::string::npos);
    EXPECT_EQ(readTextFile(activated_module).find("VALUE = 'v1'"), std::string::npos);

    const std::string receipt = readTextFile(discovered.front().package_root / "extension_package_receipt.json");
    EXPECT_NE(receipt.find(archive_v2_hash), std::string::npos);
    EXPECT_EQ(receipt.find(archive_v1_hash), std::string::npos);
}

TEST_F(ExtensionManagerPageTest, PrimaryActionShowsInstallerFailure)
{
    an::ui::ExtensionManagerPage page(nullptr);
    page.setRepositoryProvider([&]() {
        ExtensionManagerPageRepositorySnapshot snapshot;
        snapshot.success = true;
        snapshot.indexUrl = QUrl::fromLocalFile(QStringLiteral("/tmp/afternight-extension-repo/index.json"));
        snapshot.entries = {makeEntry("sample_ai", "Sample AI", "AI-assisted background extraction", "1.1.0",
                                      ExtensionRepositoryInstallState::UpdateAvailable, "1.0.0")};
        return snapshot;
    });

    page.setInstallProvider([](const ExtensionManagerPageInstallRequest&) {
        ExtensionManagerPageInstallResult result;
        result.success = false;
        result.diagnostic = QStringLiteral("Package archive SHA-256 does not match the expected hash");
        return result;
    });

    allowUnsignedExtensionInstalls(page);
    page.loadSettings();

    auto* update_button = page.findChild<QPushButton*>(buttonObjectName("sample_ai", "primaryAction"));
    ASSERT_NE(update_button, nullptr);
    ASSERT_TRUE(update_button->isEnabled());
    update_button->click();

    auto* summary_banner = dynamic_cast<an::ui::AnInfoBanner*>(
        page.findChild<QWidget*>(QStringLiteral("anExtensionManagerSummaryBanner")));
    ASSERT_NE(summary_banner, nullptr);
    ASSERT_TRUE(waitFor([&]() { return summary_banner->message().contains(QStringLiteral("SHA-256")); }));
}

TEST_F(ExtensionManagerPageTest, PrimaryActionBlocksIncompleteVerifiedSignatureMetadataBeforeInstaller)
{
    an::ui::ExtensionManagerPage page(nullptr);
    page.setRepositoryProvider([&]() {
        ExtensionManagerPageRepositorySnapshot snapshot;
        snapshot.success = true;
        snapshot.indexUrl = QUrl::fromLocalFile(QStringLiteral("/tmp/afternight-extension-repo/index.json"));
        auto entry = makeEntry("signed_ext", "Signed Extension", "Signed package", "1.0.0",
                               ExtensionRepositoryInstallState::NotInstalled);
        entry.selected_asset_signature_state = ExtensionPackageSignatureState::Verified;
        entry.selected_asset_signature_detail = "Repository says this asset is signed.";
        snapshot.entries = {entry};
        return snapshot;
    });

    int install_calls = 0;
    page.setInstallProvider([&](const ExtensionManagerPageInstallRequest&) {
        ++install_calls;
        ExtensionManagerPageInstallResult result;
        result.success = true;
        markStartupCachesPrimed(result);
        return result;
    });

    allowUnsignedExtensionInstalls(page);
    page.loadSettings();

    auto* install_button = page.findChild<QPushButton*>(buttonObjectName("signed_ext", "primaryAction"));
    ASSERT_NE(install_button, nullptr);
    ASSERT_TRUE(install_button->isEnabled());
    install_button->click();
    QApplication::processEvents();

    EXPECT_EQ(install_calls, 0);

    auto* summary_banner = dynamic_cast<an::ui::AnInfoBanner*>(
        page.findChild<QWidget*>(QStringLiteral("anExtensionManagerSummaryBanner")));
    ASSERT_NE(summary_banner, nullptr);
    EXPECT_TRUE(summary_banner->message().contains(QStringLiteral("signature metadata")));
}

TEST_F(ExtensionManagerPageTest, PrimaryActionAllowsVerifiedSignatureMetadataForTrustedKey)
{
    an::ui::ExtensionManagerPage page(nullptr);
    page.setRepositoryProvider([&]() {
        ExtensionManagerPageRepositorySnapshot snapshot;
        snapshot.success = true;
        snapshot.indexUrl = QUrl::fromLocalFile(QStringLiteral("/tmp/afternight-extension-repo/index.json"));
        auto entry = makeEntry("signed_ext", "Signed Extension", "Signed package", "1.0.0",
                               ExtensionRepositoryInstallState::NotInstalled);
        entry.selected_asset_signature_state = ExtensionPackageSignatureState::Verified;
        entry.selected_asset_signature_detail = "Verified fixture signature.";
        entry.selected_asset_signature_algorithm = "ed25519";
        // The page admits a verified package whose key this build trusts - the official key, the one key a shipped
        // build trusts; the signature itself is the installer's to verify.
        entry.selected_asset_signature_key_id = "afternight-official-ed25519-v1";
        entry.selected_asset_signature =
            "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA==";
        snapshot.entries = {entry};
        return snapshot;
    });

    int install_calls = 0;
    page.setInstallProvider([&](const ExtensionManagerPageInstallRequest& request) {
        ++install_calls;
        EXPECT_EQ(request.entry.selected_asset_signature_state, ExtensionPackageSignatureState::Verified);
        EXPECT_EQ(request.entry.selected_asset_signature_key_id, "afternight-official-ed25519-v1");
        ExtensionManagerPageInstallResult result;
        result.success = true;
        markStartupCachesPrimed(result);
        return result;
    });

    allowUnsignedExtensionInstalls(page);
    page.loadSettings();

    auto* install_button = page.findChild<QPushButton*>(buttonObjectName("signed_ext", "primaryAction"));
    ASSERT_NE(install_button, nullptr);
    ASSERT_TRUE(install_button->isEnabled());
    install_button->click();
    ASSERT_TRUE(waitFor([&]() { return install_calls == 1; }));
}

TEST_F(ExtensionManagerPageTest, PrimaryActionRequiresReadyStartupCachesBeforeSuccess)
{
    an::ui::ExtensionManagerPage page(nullptr);
    page.setRepositoryProvider([&]() {
        ExtensionManagerPageRepositorySnapshot snapshot;
        snapshot.success = true;
        snapshot.indexUrl = QUrl::fromLocalFile(QStringLiteral("/tmp/afternight-extension-repo/index.json"));
        snapshot.entries = {makeEntry("sample_ai", "Sample AI", "AI-assisted background extraction", "1.1.0",
                                      ExtensionRepositoryInstallState::NotInstalled)};
        return snapshot;
    });

    page.setInstallProvider([](const ExtensionManagerPageInstallRequest&) {
        ExtensionManagerPageInstallResult result;
        result.success = true;
        result.diagnostic = QStringLiteral("Installed Sample AI 1.1.0.");
        ExtensionStartupCachePrimingResult priming;
        priming.success = false;
        priming.environment_ready = true;
        priming.settings_schema_ready = false;
        priming.diagnostic = "Settings schema cache priming did not complete";
        result.transaction.startup_cache_priming = priming;
        result.transaction.package_bytecode_precompiled = true;
        return result;
    });

    allowUnsignedExtensionInstalls(page);
    page.loadSettings();

    auto* install_button = page.findChild<QPushButton*>(buttonObjectName("sample_ai", "primaryAction"));
    ASSERT_NE(install_button, nullptr);
    ASSERT_TRUE(install_button->isEnabled());
    install_button->click();

    auto* summary_banner = dynamic_cast<an::ui::AnInfoBanner*>(
        page.findChild<QWidget*>(QStringLiteral("anExtensionManagerSummaryBanner")));
    ASSERT_NE(summary_banner, nullptr);
    ASSERT_TRUE(waitFor([&]() { return summary_banner->message().contains(QStringLiteral("cache priming")); }));
}

TEST_F(ExtensionManagerPageTest, PrimaryActionRequiresBytecodePrecompileBeforeSuccess)
{
    an::ui::ExtensionManagerPage page(nullptr);
    page.setRepositoryProvider([&]() {
        ExtensionManagerPageRepositorySnapshot snapshot;
        snapshot.success = true;
        snapshot.indexUrl = QUrl::fromLocalFile(QStringLiteral("/tmp/afternight-extension-repo/index.json"));
        snapshot.entries = {makeEntry("sample_ai", "Sample AI", "AI-assisted background extraction", "1.1.0",
                                      ExtensionRepositoryInstallState::NotInstalled)};
        return snapshot;
    });

    page.setInstallProvider([](const ExtensionManagerPageInstallRequest&) {
        ExtensionManagerPageInstallResult result;
        result.success = true;
        result.diagnostic = QStringLiteral("Installed Sample AI 1.1.0.");
        markStartupCachesPrimed(result);
        result.transaction.package_bytecode_precompiled = false;
        return result;
    });

    allowUnsignedExtensionInstalls(page);
    page.loadSettings();

    auto* install_button = page.findChild<QPushButton*>(buttonObjectName("sample_ai", "primaryAction"));
    ASSERT_NE(install_button, nullptr);
    ASSERT_TRUE(install_button->isEnabled());
    install_button->click();

    auto* summary_banner = dynamic_cast<an::ui::AnInfoBanner*>(
        page.findChild<QWidget*>(QStringLiteral("anExtensionManagerSummaryBanner")));
    ASSERT_NE(summary_banner, nullptr);
    ASSERT_TRUE(waitFor([&]() { return summary_banner->message().contains(QStringLiteral("bytecode")); }));
}

TEST_F(ExtensionManagerPageTest, PrimaryActionReportsInstallProgressPhases)
{
    an::ui::ExtensionManagerPage page(nullptr);
    page.setRepositoryProvider([&]() {
        ExtensionManagerPageRepositorySnapshot snapshot;
        snapshot.success = true;
        snapshot.indexUrl = QUrl::fromLocalFile(QStringLiteral("/tmp/afternight-extension-repo/index.json"));
        snapshot.entries = {makeEntry("sample_ai", "Sample AI", "AI-assisted background extraction", "1.1.0",
                                      ExtensionRepositoryInstallState::NotInstalled)};
        return snapshot;
    });

    std::atomic_bool finish_provider = false;
    FinishProviderOnExit finish_guard{finish_provider};
    page.setInstallProvider([&](const ExtensionManagerPageInstallRequest& request) {
        if (request.progress_callback)
        {
            request.progress_callback({"download", "Downloading package asset", 12, 100, false});
        }
        while (!finish_provider.load())
        {
            QThread::msleep(1);
        }

        ExtensionManagerPageInstallResult result;
        result.success = true;
        result.diagnostic = QStringLiteral("Installed Sample AI 1.1.0.");
        markStartupCachesPrimed(result);
        return result;
    });

    allowUnsignedExtensionInstalls(page);
    page.loadSettings();

    auto* install_button = page.findChild<QPushButton*>(buttonObjectName("sample_ai", "primaryAction"));
    ASSERT_NE(install_button, nullptr);
    ASSERT_TRUE(install_button->isEnabled());
    install_button->click();

    auto* progress_label = page.findChild<QLabel*>(QStringLiteral("anExtensionManagerProgressLabel"));
    auto* progress_bar = page.findChild<QProgressBar*>(QStringLiteral("anExtensionManagerProgressBar"));
    ASSERT_NE(progress_label, nullptr);
    ASSERT_NE(progress_bar, nullptr);
    ASSERT_TRUE(waitFor([&]() {
        return !progress_label->isHidden() &&
               progress_label->text().contains(QStringLiteral("Downloading package asset"));
    }));
    EXPECT_FALSE(progress_bar->isHidden());
    EXPECT_EQ(progress_bar->minimum(), 0);
    EXPECT_EQ(progress_bar->maximum(), 100);
    EXPECT_EQ(progress_bar->value(), 12);

    finish_provider.store(true);
    auto* summary_banner = dynamic_cast<an::ui::AnInfoBanner*>(
        page.findChild<QWidget*>(QStringLiteral("anExtensionManagerSummaryBanner")));
    ASSERT_NE(summary_banner, nullptr);
    ASSERT_TRUE(waitFor([&]() { return summary_banner->message().contains(QStringLiteral("Installed Sample AI")); }));
}

TEST_F(ExtensionManagerPageTest, CancelButtonRequestsInstallCancellation)
{
    an::ui::ExtensionManagerPage page(nullptr);
    page.setRepositoryProvider([&]() {
        ExtensionManagerPageRepositorySnapshot snapshot;
        snapshot.success = true;
        snapshot.indexUrl = QUrl::fromLocalFile(QStringLiteral("/tmp/afternight-extension-repo/index.json"));
        snapshot.entries = {makeEntry("sample_ai", "Sample AI", "AI-assisted background extraction", "1.1.0",
                                      ExtensionRepositoryInstallState::NotInstalled)};
        return snapshot;
    });

    std::atomic_bool finish_provider = false;
    std::atomic_bool saw_cancellation = false;
    FinishProviderOnExit finish_guard{finish_provider};
    page.setInstallProvider([&](const ExtensionManagerPageInstallRequest& request) {
        if (request.progress_callback)
        {
            request.progress_callback({"download", "Downloading package asset", 0, 0, true});
        }
        while (!request.cancellation_token.isCancellationRequested() && !finish_provider.load())
        {
            QThread::msleep(1);
        }

        ExtensionManagerPageInstallResult result;
        if (request.cancellation_token.isCancellationRequested())
        {
            saw_cancellation.store(true);
            result.cancelled = true;
            result.diagnostic = QStringLiteral("Extension package installation cancelled.");
        }
        return result;
    });

    allowUnsignedExtensionInstalls(page);
    page.loadSettings();

    auto* install_button = page.findChild<QPushButton*>(buttonObjectName("sample_ai", "primaryAction"));
    ASSERT_NE(install_button, nullptr);
    ASSERT_TRUE(install_button->isEnabled());
    install_button->click();

    auto* cancel_button = page.findChild<QPushButton*>(QStringLiteral("anExtensionManagerCancelButton"));
    ASSERT_NE(cancel_button, nullptr);
    ASSERT_TRUE(waitFor([&]() { return !cancel_button->isHidden() && cancel_button->isEnabled(); }));
    cancel_button->click();

    EXPECT_TRUE(waitFor([&]() { return saw_cancellation.load(); }));
    EXPECT_FALSE(cancel_button->isEnabled());

    auto* summary_banner = dynamic_cast<an::ui::AnInfoBanner*>(
        page.findChild<QWidget*>(QStringLiteral("anExtensionManagerSummaryBanner")));
    ASSERT_NE(summary_banner, nullptr);
    ASSERT_TRUE(waitFor([&]() { return summary_banner->message().contains(QStringLiteral("cancelled")); }));
}

TEST_F(ExtensionManagerPageTest, BrowseUninstallActionRequiresConfirmation)
{
    an::ui::ExtensionManagerPage page(nullptr);
    page.setRepositoryProvider([&]() {
        ExtensionManagerPageRepositorySnapshot snapshot;
        snapshot.success = true;
        snapshot.indexUrl = QUrl::fromLocalFile(QStringLiteral("/tmp/afternight-extension-repo/index.json"));
        snapshot.entries = {makeEntry("sample_tools", "Sample Tools", "Sharpening and denoise tools", "2.0.0",
                                      ExtensionRepositoryInstallState::Installed, "2.0.0")};
        return snapshot;
    });

    int confirmation_calls = 0;
    int remove_calls = 0;
    bool confirm_uninstall = false;
    page.setRemoveConfirmationProvider([&](QWidget*, const ExtensionRepositoryBrowseEntry& entry) {
        ++confirmation_calls;
        EXPECT_EQ(entry.id, "sample_tools");
        return confirm_uninstall;
    });
    page.setRemoveProvider([&](const ExtensionManagerPageRemoveRequest&) {
        ++remove_calls;
        ExtensionManagerPageRemoveResult result;
        result.success = true;
        result.diagnostic = QStringLiteral("Removed Sample Tools.");
        return result;
    });

    page.loadSettings();

    auto* remove_button = page.findChild<QPushButton*>(buttonObjectName("sample_tools", "remove"));
    ASSERT_NE(remove_button, nullptr);
    EXPECT_EQ(remove_button->text(), QStringLiteral("Uninstall"));
    EXPECT_TRUE(remove_button->toolTip().contains(QStringLiteral("Uninstall Sample Tools")));
    ASSERT_TRUE(remove_button->isEnabled());

    remove_button->click();
    EXPECT_EQ(confirmation_calls, 1);
    EXPECT_EQ(remove_calls, 0);

    confirm_uninstall = true;
    remove_button->click();
    ASSERT_TRUE(waitFor([&]() { return remove_calls == 1; }));
    EXPECT_EQ(confirmation_calls, 2);
}

TEST_F(ExtensionManagerPageTest, RemoveActionInvokesProviderForInstalledPackage)
{
    an::ui::ExtensionManagerPage page(nullptr);
    int repository_calls = 0;
    page.setRepositoryProvider([&]() {
        ++repository_calls;
        ExtensionManagerPageRepositorySnapshot snapshot;
        snapshot.success = true;
        snapshot.indexUrl = QUrl::fromLocalFile(QStringLiteral("/tmp/afternight-extension-repo/index.json"));
        snapshot.entries = {makeEntry("sample_tools", "Sample Tools", "Sharpening and denoise tools", "2.0.0",
                                      ExtensionRepositoryInstallState::Installed, "2.0.0")};
        return snapshot;
    });

    std::mutex mutex;
    std::optional<ExtensionManagerPageRemoveRequest> captured_request;
    page.setRemoveProvider([&](const ExtensionManagerPageRemoveRequest& request) {
        {
            const std::lock_guard lock(mutex);
            captured_request = request;
        }

        ExtensionManagerPageRemoveResult result;
        result.success = true;
        result.diagnostic = QStringLiteral("Removed Sample Tools.");
        return result;
    });

    allowBrowseExtensionUninstalls(page);
    page.loadSettings();

    auto* remove_button = page.findChild<QPushButton*>(buttonObjectName("sample_tools", "remove"));
    ASSERT_NE(remove_button, nullptr);
    ASSERT_TRUE(remove_button->isEnabled());

    remove_button->click();

    ASSERT_TRUE(waitFor([&]() {
        const std::lock_guard lock(mutex);
        return captured_request.has_value();
    }));
    ASSERT_TRUE(waitFor([&]() { return repository_calls >= 2; }));

    ExtensionManagerPageRemoveRequest request;
    {
        const std::lock_guard lock(mutex);
        request = *captured_request;
    }

    EXPECT_EQ(request.entry.id, "sample_tools");
    EXPECT_EQ(request.entry.name, "Sample Tools");
    EXPECT_FALSE(request.installRoot.empty());

    auto* summary_banner = dynamic_cast<an::ui::AnInfoBanner*>(
        page.findChild<QWidget*>(QStringLiteral("anExtensionManagerSummaryBanner")));
    ASSERT_NE(summary_banner, nullptr);
    EXPECT_TRUE(summary_banner->message().contains(QStringLiteral("Removed Sample Tools")));
}

TEST_F(ExtensionManagerPageTest, SuccessfulPackageRemovalNotifiesInstalledStatusCacheInvalidation)
{
    an::ui::ExtensionManagerPage page(nullptr);
    page.setRepositoryProvider([] {
        ExtensionManagerPageRepositorySnapshot snapshot;
        snapshot.success = true;
        snapshot.entries = {makeEntry("sample_tools", "Sample Tools", "Sharpening and denoise tools", "2.0.0",
                                      ExtensionRepositoryInstallState::Installed, "2.0.0")};
        return snapshot;
    });

    int changed_count = 0;
    QObject::connect(&page, &an::ui::ExtensionManagerPage::installedExtensionsChanged, &page,
                     [&changed_count]() { ++changed_count; });
    page.setRemoveProvider([](const ExtensionManagerPageRemoveRequest&) {
        ExtensionManagerPageRemoveResult result;
        result.success = true;
        result.diagnostic = QStringLiteral("Removed Sample Tools.");
        return result;
    });

    allowBrowseExtensionUninstalls(page);
    page.loadSettings();
    auto* remove_button = page.findChild<QPushButton*>(buttonObjectName("sample_tools", "remove"));
    ASSERT_NE(remove_button, nullptr);
    ASSERT_TRUE(remove_button->isEnabled());
    remove_button->click();

    ASSERT_TRUE(waitFor([&]() { return changed_count == 1; }));
}

TEST_F(ExtensionManagerPageTest, RemoveActionReportsProgressPhase)
{
    an::ui::ExtensionManagerPage page(nullptr);
    page.setRepositoryProvider([&]() {
        ExtensionManagerPageRepositorySnapshot snapshot;
        snapshot.success = true;
        snapshot.indexUrl = QUrl::fromLocalFile(QStringLiteral("/tmp/afternight-extension-repo/index.json"));
        snapshot.entries = {makeEntry("sample_tools", "Sample Tools", "Sharpening and denoise tools", "2.0.0",
                                      ExtensionRepositoryInstallState::Installed, "2.0.0")};
        return snapshot;
    });

    std::atomic_bool finish_provider = false;
    FinishProviderOnExit finish_guard{finish_provider};
    page.setRemoveProvider([&](const ExtensionManagerPageRemoveRequest& request) {
        if (request.progress_callback)
        {
            request.progress_callback({"remove", "Removing extension package", 0, 0, true});
        }
        while (!finish_provider.load())
        {
            QThread::msleep(1);
        }

        ExtensionManagerPageRemoveResult result;
        result.success = true;
        result.diagnostic = QStringLiteral("Removed Sample Tools.");
        return result;
    });

    allowBrowseExtensionUninstalls(page);
    page.loadSettings();

    auto* remove_button = page.findChild<QPushButton*>(buttonObjectName("sample_tools", "remove"));
    ASSERT_NE(remove_button, nullptr);
    ASSERT_TRUE(remove_button->isEnabled());
    remove_button->click();
    EXPECT_EQ(remove_button->text(), QStringLiteral("Uninstalling..."));
    EXPECT_TRUE(remove_button->toolTip().contains(QStringLiteral("Uninstalling Sample Tools")));

    auto* progress_label = page.findChild<QLabel*>(QStringLiteral("anExtensionManagerProgressLabel"));
    auto* progress_bar = page.findChild<QProgressBar*>(QStringLiteral("anExtensionManagerProgressBar"));
    ASSERT_NE(progress_label, nullptr);
    ASSERT_NE(progress_bar, nullptr);
    ASSERT_TRUE(waitFor([&]() {
        return !progress_label->isHidden() &&
               progress_label->text().contains(QStringLiteral("Removing extension package"));
    }));
    EXPECT_FALSE(progress_bar->isHidden());
    EXPECT_EQ(progress_bar->minimum(), 0);
    EXPECT_EQ(progress_bar->maximum(), 0);

    finish_provider.store(true);
    auto* summary_banner = dynamic_cast<an::ui::AnInfoBanner*>(
        page.findChild<QWidget*>(QStringLiteral("anExtensionManagerSummaryBanner")));
    ASSERT_NE(summary_banner, nullptr);
    ASSERT_TRUE(waitFor([&]() { return summary_banner->message().contains(QStringLiteral("Removed Sample Tools")); }));
}

TEST_F(ExtensionManagerPageTest, RemoveActionShowsProviderFailure)
{
    an::ui::ExtensionManagerPage page(nullptr);
    page.setRepositoryProvider([&]() {
        ExtensionManagerPageRepositorySnapshot snapshot;
        snapshot.success = true;
        snapshot.indexUrl = QUrl::fromLocalFile(QStringLiteral("/tmp/afternight-extension-repo/index.json"));
        snapshot.entries = {makeEntry("sample_tools", "Sample Tools", "Sharpening and denoise tools", "2.0.0",
                                      ExtensionRepositoryInstallState::Installed, "2.0.0")};
        return snapshot;
    });

    page.setRemoveProvider([](const ExtensionManagerPageRemoveRequest&) {
        ExtensionManagerPageRemoveResult result;
        result.success = false;
        result.diagnostic = QStringLiteral("Installed extension package was not found");
        return result;
    });

    allowBrowseExtensionUninstalls(page);
    page.loadSettings();

    auto* remove_button = page.findChild<QPushButton*>(buttonObjectName("sample_tools", "remove"));
    ASSERT_NE(remove_button, nullptr);
    ASSERT_TRUE(remove_button->isEnabled());
    remove_button->click();

    auto* summary_banner = dynamic_cast<an::ui::AnInfoBanner*>(
        page.findChild<QWidget*>(QStringLiteral("anExtensionManagerSummaryBanner")));
    ASSERT_NE(summary_banner, nullptr);
    ASSERT_TRUE(waitFor([&]() { return summary_banner->message().contains(QStringLiteral("not found")); }));
}

TEST_F(ExtensionManagerPageTest, CleanUnusedEnvironmentsInvokesProviderAndReportsRemovedCount)
{
    an::ui::ExtensionManagerPage page(nullptr);
    page.setDiscoveryOptionsProvider([]() { return buildUiOnlyDiscoveryOptions(); });
    int repository_calls = 0;
    page.setRepositoryProvider([&]() {
        ++repository_calls;
        ExtensionManagerPageRepositorySnapshot snapshot;
        snapshot.success = true;
        snapshot.entries = {makeEntry("sample_tools", "Sample Tools", "Sharpening and denoise tools", "2.0.0",
                                      ExtensionRepositoryInstallState::Installed, "2.0.0")};
        return snapshot;
    });

    std::mutex mutex;
    std::optional<ExtensionManagerPageEnvironmentCleanupRequest> captured_request;
    page.setEnvironmentCleanupProvider([&](const ExtensionManagerPageEnvironmentCleanupRequest& request) {
        {
            const std::lock_guard lock(mutex);
            captured_request = request;
        }

        ExtensionManagerPageEnvironmentCleanupResult result;
        result.success = true;
        result.removed_paths = {std::filesystem::path("/tmp/afternight-env-a"),
                                std::filesystem::path("/tmp/afternight-env-b")};
        return result;
    });

    page.loadSettings();

    auto* more_actions_button = page.findChild<QToolButton*>(QStringLiteral("anExtensionManagerMoreActionsButton"));
    ASSERT_NE(more_actions_button, nullptr);
    ASSERT_TRUE(more_actions_button->isEnabled());

    auto* clean_action = findMoreActionsMenuAction(page, QStringLiteral("Clean unused environments"));
    ASSERT_NE(clean_action, nullptr);
    ASSERT_TRUE(clean_action->isEnabled());
    clean_action->trigger();

    ASSERT_TRUE(waitFor([&]() {
        const std::lock_guard lock(mutex);
        return captured_request.has_value();
    }));
    ASSERT_TRUE(waitFor([&]() { return repository_calls >= 2; }));

    ExtensionManagerPageEnvironmentCleanupRequest request;
    {
        const std::lock_guard lock(mutex);
        request = *captured_request;
    }

    EXPECT_FALSE(request.discovery_options.context.environments_root.empty());
    EXPECT_FALSE(request.discovery_options.context.runtime_target.empty());

    auto* summary_banner = dynamic_cast<an::ui::AnInfoBanner*>(
        page.findChild<QWidget*>(QStringLiteral("anExtensionManagerSummaryBanner")));
    ASSERT_NE(summary_banner, nullptr);
    EXPECT_TRUE(summary_banner->message().contains(QStringLiteral("2 unused extension environment")));
}

TEST_F(ExtensionManagerPageTest, EnvironmentStorageSummaryShowsTotalAndReclaimableSpace)
{
    an::ui::ExtensionManagerPage page(nullptr);
    page.setRepositoryProvider([]() {
        ExtensionManagerPageRepositorySnapshot snapshot;
        snapshot.success = true;
        snapshot.environment_storage_available = true;
        snapshot.environment_total_bytes = 5ULL * 1024ULL * 1024ULL * 1024ULL;
        snapshot.environment_active_bytes = 3ULL * 1024ULL * 1024ULL * 1024ULL;
        snapshot.environment_reclaimable_bytes = 2ULL * 1024ULL * 1024ULL * 1024ULL;
        snapshot.environment_total_count = 3;
        snapshot.environment_reclaimable_count = 1;
        snapshot.entries = {makeEntry("sample_tools", "Sample Tools", "Sharpening and denoise tools", "2.0.0",
                                      ExtensionRepositoryInstallState::Installed, "2.0.0")};
        return snapshot;
    });

    page.loadSettings();

    auto* storage_label = page.findChild<QLabel*>(QStringLiteral("anExtensionManagerEnvironmentStorageLabel"));
    ASSERT_NE(storage_label, nullptr);
    EXPECT_FALSE(storage_label->isHidden());
    EXPECT_TRUE(storage_label->text().contains(QStringLiteral("5.0 GiB")));
    EXPECT_TRUE(storage_label->text().contains(QStringLiteral("3.0 GiB active")));
    EXPECT_TRUE(storage_label->text().contains(QStringLiteral("2.0 GiB reclaimable")));
}

TEST_F(ExtensionManagerPageTest, AutomaticEnvironmentCleanupIsEnabledByDefault)
{
    an::ui::ExtensionManagerPage page(nullptr);
    page.setRepositoryProvider([]() {
        ExtensionManagerPageRepositorySnapshot snapshot;
        snapshot.success = true;
        return snapshot;
    });
    page.loadSettings();

    auto* automatic_cleanup =
        page.findChild<QCheckBox*>(QStringLiteral("anExtensionManagerAutomaticEnvironmentCleanupCheck"));
    ASSERT_NE(automatic_cleanup, nullptr);
    EXPECT_TRUE(automatic_cleanup->isChecked());
    EXPECT_TRUE(automatic_cleanup->toolTip().contains(QStringLiteral("superseded"), Qt::CaseInsensitive));
}

TEST_F(ExtensionManagerPageTest, CleanUnusedEnvironmentsReportsProgressPhase)
{
    an::ui::ExtensionManagerPage page(nullptr);
    page.setRepositoryProvider([&]() {
        ExtensionManagerPageRepositorySnapshot snapshot;
        snapshot.success = true;
        snapshot.entries = {makeEntry("sample_tools", "Sample Tools", "Sharpening and denoise tools", "2.0.0",
                                      ExtensionRepositoryInstallState::Installed, "2.0.0")};
        return snapshot;
    });

    std::atomic_bool finish_provider = false;
    FinishProviderOnExit finish_guard{finish_provider};
    page.setEnvironmentCleanupProvider([&](const ExtensionManagerPageEnvironmentCleanupRequest& request) {
        if (request.progress_callback)
        {
            request.progress_callback({"cleanup_remove", "Removing unused extension environments", 5, 10, false});
        }
        while (!finish_provider.load())
        {
            QThread::msleep(1);
        }

        ExtensionManagerPageEnvironmentCleanupResult result;
        result.success = true;
        result.removed_paths = {std::filesystem::path("/tmp/afternight-env-a")};
        return result;
    });

    page.loadSettings();

    auto* more_actions_button = page.findChild<QToolButton*>(QStringLiteral("anExtensionManagerMoreActionsButton"));
    ASSERT_NE(more_actions_button, nullptr);
    ASSERT_TRUE(more_actions_button->isEnabled());

    auto* clean_action = findMoreActionsMenuAction(page, QStringLiteral("Clean unused environments"));
    ASSERT_NE(clean_action, nullptr);
    ASSERT_TRUE(clean_action->isEnabled());
    clean_action->trigger();

    auto* progress_label = page.findChild<QLabel*>(QStringLiteral("anExtensionManagerProgressLabel"));
    auto* progress_bar = page.findChild<QProgressBar*>(QStringLiteral("anExtensionManagerProgressBar"));
    ASSERT_NE(progress_label, nullptr);
    ASSERT_NE(progress_bar, nullptr);
    ASSERT_TRUE(waitFor([&]() {
        return !progress_label->isHidden() &&
               progress_label->text().contains(QStringLiteral("Removing unused extension environments"));
    }));
    EXPECT_FALSE(progress_bar->isHidden());
    EXPECT_EQ(progress_bar->minimum(), 0);
    EXPECT_EQ(progress_bar->maximum(), 10);
    EXPECT_EQ(progress_bar->value(), 5);

    finish_provider.store(true);
    auto* summary_banner = dynamic_cast<an::ui::AnInfoBanner*>(
        page.findChild<QWidget*>(QStringLiteral("anExtensionManagerSummaryBanner")));
    ASSERT_NE(summary_banner, nullptr);
    ASSERT_TRUE(waitFor(
        [&]() { return summary_banner->message().contains(QStringLiteral("1 unused extension environment")); }));
}

TEST_F(ExtensionManagerPageTest, CleanUnusedEnvironmentsCanBeCancelledWithSpecificTooltip)
{
    an::ui::ExtensionManagerPage page(nullptr);
    page.setRepositoryProvider([&]() {
        ExtensionManagerPageRepositorySnapshot snapshot;
        snapshot.success = true;
        snapshot.entries = {makeEntry("sample_tools", "Sample Tools", "Sharpening and denoise tools", "2.0.0",
                                      ExtensionRepositoryInstallState::Installed, "2.0.0")};
        return snapshot;
    });

    std::atomic_bool finish_provider = false;
    std::atomic_bool saw_cancellation = false;
    FinishProviderOnExit finish_guard{finish_provider};
    page.setEnvironmentCleanupProvider([&](const ExtensionManagerPageEnvironmentCleanupRequest& request) {
        if (request.progress_callback)
        {
            request.progress_callback({"cleanup_remove", "Removing unused extension environment 1 of 2", 0, 2, false});
        }
        while (!request.cancellation_token.isCancellationRequested() && !finish_provider.load())
        {
            QThread::msleep(1);
        }

        ExtensionManagerPageEnvironmentCleanupResult result;
        if (request.cancellation_token.isCancellationRequested())
        {
            saw_cancellation.store(true);
            result.cancelled = true;
            result.diagnostic = QStringLiteral("Unused extension environment cleanup cancelled.");
        }
        return result;
    });

    page.loadSettings();

    auto* clean_action = findMoreActionsMenuAction(page, QStringLiteral("Clean unused environments"));
    ASSERT_NE(clean_action, nullptr);
    clean_action->trigger();

    auto* cancel_button = page.findChild<QPushButton*>(QStringLiteral("anExtensionManagerCancelButton"));
    ASSERT_NE(cancel_button, nullptr);
    ASSERT_TRUE(waitFor([&]() { return !cancel_button->isHidden() && cancel_button->isEnabled(); }));
    EXPECT_TRUE(cancel_button->toolTip().contains(QStringLiteral("unused extension environments")));

    cancel_button->click();
    EXPECT_TRUE(waitFor([&]() { return saw_cancellation.load(); }));
    EXPECT_FALSE(cancel_button->isEnabled());

    auto* summary_banner = dynamic_cast<an::ui::AnInfoBanner*>(
        page.findChild<QWidget*>(QStringLiteral("anExtensionManagerSummaryBanner")));
    ASSERT_NE(summary_banner, nullptr);
    ASSERT_TRUE(waitFor([&]() { return summary_banner->message().contains(QStringLiteral("cancelled")); }));
}

TEST_F(ExtensionManagerPageTest, DefaultEnvironmentCleanupReportsPerEnvironmentProgressAndLogs)
{
    QTemporaryDir temp_dir;
    ASSERT_TRUE(temp_dir.isValid());

    // Two environments no installed extension uses, in the layout the environment manager makes.
    const auto temp_root = std::filesystem::path(temp_dir.path().toStdString());
    const auto environments_root = temp_root / "ExtensionEnvs";
    const std::string runtime_target = an::ui::extensions::current_runtime_target();
    const auto first_environment = environments_root / "retired_alpha" / "1.0.0" / "fingerprint-a" / runtime_target;
    const auto second_environment = environments_root / "retired_beta" / "2.0.0" / "fingerprint-b" / runtime_target;
    writeTextFile(first_environment / "Lib" / "site-packages" / "payload.bin", std::string(2048, 'a'));
    writeTextFile(second_environment / "Lib" / "site-packages" / "payload.bin", std::string(2048, 'b'));

    ExtensionDiscoveryOptions discovery_options;
    discovery_options.user_roots.push_back(temp_root / "user-extensions");
    discovery_options.context.runtime_target = runtime_target;
    discovery_options.context.environments_root = environments_root;

    an::ui::ExtensionManagerPage page(nullptr);
    page.setDiscoveryOptionsProvider([&]() { return discovery_options; });
    page.setRepositoryProvider([] {
        ExtensionManagerPageRepositorySnapshot snapshot;
        snapshot.success = true;
        snapshot.entries = {makeEntry("sample_tools", "Sample Tools", "Sharpening and denoise tools", "2.0.0",
                                      ExtensionRepositoryInstallState::Installed, "2.0.0")};
        return snapshot;
    });
    page.loadSettings();

    auto* progress_label = page.findChild<QLabel*>(QStringLiteral("anExtensionManagerProgressLabel"));
    auto* progress_bar = page.findChild<QProgressBar*>(QStringLiteral("anExtensionManagerProgressBar"));
    ASSERT_NE(progress_label, nullptr);
    ASSERT_NE(progress_bar, nullptr);
    std::vector<std::tuple<int, int, QString>> progress_steps;
    QObject::connect(progress_bar, &QProgressBar::valueChanged, &page, [&](const int value) {
        progress_steps.emplace_back(value, progress_bar->maximum(), progress_label->text());
    });

    const auto since = std::chrono::system_clock::now();
    auto* clean_action = findMoreActionsMenuAction(page, QStringLiteral("Clean unused environments"));
    ASSERT_NE(clean_action, nullptr);
    clean_action->trigger();

    auto* summary_banner = dynamic_cast<an::ui::AnInfoBanner*>(
        page.findChild<QWidget*>(QStringLiteral("anExtensionManagerSummaryBanner")));
    ASSERT_NE(summary_banner, nullptr);
    ASSERT_TRUE(waitFor(
        [&]() { return summary_banner->message().contains(QStringLiteral("Removed 2 unused extension environment")); },
        10000))
        << summary_banner->message().toStdString();

    EXPECT_FALSE(std::filesystem::exists(first_environment));
    EXPECT_FALSE(std::filesystem::exists(second_environment));

    // The bar moves once per environment removed, out of the environments found, and says which one it reached.
    const std::tuple<int, int, QString> first_removed{1, 2,
                                                      QStringLiteral("Removed unused extension environment 1 of 2")};
    const std::tuple<int, int, QString> second_removed{2, 2,
                                                       QStringLiteral("Removed unused extension environment 2 of 2")};
    EXPECT_NE(std::find(progress_steps.begin(), progress_steps.end(), first_removed), progress_steps.end());
    EXPECT_NE(std::find(progress_steps.begin(), progress_steps.end(), second_removed), progress_steps.end());

    // The log records the request and each environment removed, by its path.
    EXPECT_TRUE(loggedSince(since, {"User requested cleanup of unused extension environments"}));
    EXPECT_TRUE(loggedSince(since, {"Removing unused extension environment 1 of 2 path='", "retired_alpha"}));
    EXPECT_TRUE(loggedSince(since, {"Removing unused extension environment 2 of 2 path='", "retired_beta"}));
    EXPECT_TRUE(loggedSince(since, {"Removed 2 unused extension environment(s)"}));
}

TEST_F(ExtensionManagerPageTest, CleanUnusedEnvironmentsShowsProviderFailure)
{
    an::ui::ExtensionManagerPage page(nullptr);
    page.setRepositoryProvider([&]() {
        ExtensionManagerPageRepositorySnapshot snapshot;
        snapshot.success = true;
        snapshot.entries = {makeEntry("sample_tools", "Sample Tools", "Sharpening and denoise tools", "2.0.0",
                                      ExtensionRepositoryInstallState::Installed, "2.0.0")};
        return snapshot;
    });

    page.setEnvironmentCleanupProvider([](const ExtensionManagerPageEnvironmentCleanupRequest&) {
        ExtensionManagerPageEnvironmentCleanupResult result;
        result.success = false;
        result.diagnostic = QStringLiteral("Failed to remove orphaned environment: /tmp/locked");
        return result;
    });

    page.loadSettings();

    auto* more_actions_button = page.findChild<QToolButton*>(QStringLiteral("anExtensionManagerMoreActionsButton"));
    ASSERT_NE(more_actions_button, nullptr);
    ASSERT_TRUE(more_actions_button->isEnabled());

    auto* clean_action = findMoreActionsMenuAction(page, QStringLiteral("Clean unused environments"));
    ASSERT_NE(clean_action, nullptr);
    ASSERT_TRUE(clean_action->isEnabled());
    clean_action->trigger();

    auto* summary_banner = dynamic_cast<an::ui::AnInfoBanner*>(
        page.findChild<QWidget*>(QStringLiteral("anExtensionManagerSummaryBanner")));
    ASSERT_NE(summary_banner, nullptr);
    ASSERT_TRUE(waitFor([&]() { return summary_banner->message().contains(QStringLiteral("orphaned environment")); }));
}

TEST_F(ExtensionManagerPageTest, BrowseCardsDoNotExposeSettingsActionForInstalledPackage)
{
    an::ui::ExtensionManagerPage page(nullptr);
    page.setRepositoryProvider([&]() {
        ExtensionManagerPageRepositorySnapshot snapshot;
        snapshot.success = true;
        snapshot.entries = {makeEntry("sample_tools", "Sample Tools", "Sharpening and denoise tools", "2.0.0",
                                      ExtensionRepositoryInstallState::Installed, "2.0.0")};
        return snapshot;
    });

    page.loadSettings();

    EXPECT_EQ(page.findChild<QPushButton*>(buttonObjectName("sample_tools", "settings")), nullptr);
}

TEST_F(ExtensionManagerPageTest, OfflineSnapshotDisablesNetworkActionsButKeepsInstalledControls)
{
    an::ui::ExtensionManagerPage page(nullptr);
    page.setRepositoryProvider([&]() {
        ExtensionManagerPageRepositorySnapshot snapshot;
        snapshot.success = true;
        snapshot.network_features_disabled = true;
        snapshot.diagnostic = QStringLiteral("Repository network features are disabled by policy.");

        auto installed = makeEntry("sample_tools", "Sample Tools", "Sharpening and denoise tools", "2.0.0",
                                   ExtensionRepositoryInstallState::Installed, "2.0.0");
        markInstalledStatus(installed, ExtensionEnvironmentState::Ready);
        snapshot.entries = {makeEntry("sample_ai", "Sample AI", "AI-assisted background extraction", "1.1.0",
                                      ExtensionRepositoryInstallState::NotInstalled),
                            installed};
        return snapshot;
    });

    int install_calls = 0;
    page.setInstallProvider([&](const ExtensionManagerPageInstallRequest&) {
        ++install_calls;
        ExtensionManagerPageInstallResult result;
        result.success = true;
        markStartupCachesPrimed(result);
        return result;
    });

    page.loadSettings();

    auto* summary_banner = dynamic_cast<an::ui::AnInfoBanner*>(
        page.findChild<QWidget*>(QStringLiteral("anExtensionManagerSummaryBanner")));
    ASSERT_NE(summary_banner, nullptr);
    ASSERT_TRUE(
        waitFor([&]() { return summary_banner->message().contains(QStringLiteral("disabled by policy")); }, 5000));
    EXPECT_TRUE(summary_banner->message().contains(QStringLiteral("disabled by policy")));

    auto* refresh_button = page.findChild<QPushButton*>(QStringLiteral("anExtensionManagerRefreshButton"));
    ASSERT_NE(refresh_button, nullptr);
    EXPECT_FALSE(refresh_button->isEnabled());

    auto* more_actions_button = page.findChild<QToolButton*>(QStringLiteral("anExtensionManagerMoreActionsButton"));
    ASSERT_NE(more_actions_button, nullptr);
    EXPECT_TRUE(more_actions_button->isEnabled());

    auto* clean_action = findMoreActionsMenuAction(page, QStringLiteral("Clean unused environments"));
    ASSERT_NE(clean_action, nullptr);
    EXPECT_TRUE(clean_action->isEnabled());

    auto* local_action = findMoreActionsMenuAction(page, QStringLiteral("Install local package..."));
    ASSERT_NE(local_action, nullptr);
    EXPECT_TRUE(local_action->isEnabled());

    auto* install_button = page.findChild<QPushButton*>(buttonObjectName("sample_ai", "primaryAction"));
    ASSERT_NE(install_button, nullptr);
    EXPECT_FALSE(install_button->isEnabled());
    install_button->click();
    QApplication::processEvents();
    EXPECT_EQ(install_calls, 0);

    auto* remove_button = page.findChild<QPushButton*>(buttonObjectName("sample_tools", "remove"));
    ASSERT_NE(remove_button, nullptr);
    EXPECT_TRUE(remove_button->isEnabled());

    EXPECT_EQ(page.findChild<QPushButton*>(buttonObjectName("sample_tools", "settings")), nullptr);
}

TEST_F(ExtensionManagerPageTest, LoadSettingsUsesCachedRepositorySnapshotWhenNetworkIsOffline)
{
    QTemporaryDir cache_dir;
    ASSERT_TRUE(cache_dir.isValid());
    ScopedEnvironmentVariable cache_override("AFTERNIGHT_EXTENSION_MANAGER_CACHE_DIR",
                                             cache_dir.path().toUtf8().constData());
    ScopedEnvironmentVariable offline_policy("AFTERNIGHT_EXTENSION_REPOSITORY_OFFLINE", "1");

    const QJsonObject cached_entry{
        {QStringLiteral("id"), QStringLiteral("cached_tools")},
        {QStringLiteral("name"), QStringLiteral("Cached Tools")},
        {QStringLiteral("summary"), QStringLiteral("Cached repository package")},
        {QStringLiteral("description"), QStringLiteral("A package loaded from the Extension Manager cache.")},
        {QStringLiteral("author"), QStringLiteral("AfterNight")},
        {QStringLiteral("publisher_id"), QStringLiteral("afternight")},
        {QStringLiteral("latest_version"), QStringLiteral("1.2.3")},
        {QStringLiteral("selected_release_version"), QStringLiteral("1.2.3")},
        {QStringLiteral("release_runtime_targets"), QJsonArray{QStringLiteral("windows-msvc-x86_64")}},
        {QStringLiteral("selected_asset_name"), QStringLiteral("cached_tools-1.2.3.tar.zst")},
        {QStringLiteral("selected_asset_download_url"), QStringLiteral("packages/cached_tools-1.2.3.tar.zst")},
        {QStringLiteral("selected_asset_package_hash_sha256"), QString(64, QChar::fromLatin1('a'))},
        {QStringLiteral("selected_asset_runtime_targets"), QJsonArray{QStringLiteral("windows-msvc-x86_64")}},
        {QStringLiteral("selected_asset_signature_state"), static_cast<int>(ExtensionPackageSignatureState::Unsigned)},
        {QStringLiteral("runtime_compatible"), true},
        {QStringLiteral("install_state"), static_cast<int>(ExtensionRepositoryInstallState::NotInstalled)}};
    const QJsonObject cache_root{{QStringLiteral("cache_format_version"), 1},
                                 {QStringLiteral("updated_at_utc"), QStringLiteral("2026-05-17T00:00:00Z")},
                                 {QStringLiteral("index_url"), QStringLiteral("https://example.invalid/index.json")},
                                 {QStringLiteral("entries"), QJsonArray{cached_entry}}};

    QFile cache_file(cache_dir.filePath(QStringLiteral("repository_snapshot.json")));
    ASSERT_TRUE(cache_file.open(QIODevice::WriteOnly));
    ASSERT_GT(cache_file.write(QJsonDocument(cache_root).toJson(QJsonDocument::Compact)), 0);
    cache_file.close();

    an::ui::ExtensionManagerPage page(nullptr);
    page.loadSettings();

    auto* summary_banner = dynamic_cast<an::ui::AnInfoBanner*>(
        page.findChild<QWidget*>(QStringLiteral("anExtensionManagerSummaryBanner")));
    ASSERT_NE(summary_banner, nullptr);
    EXPECT_TRUE(summary_banner->message().contains(QStringLiteral("cached repository snapshot"), Qt::CaseInsensitive));
    EXPECT_TRUE(summary_banner->message().contains(QStringLiteral("disabled by policy"), Qt::CaseInsensitive));

    auto* cached_card = page.findChild<QWidget*>(QStringLiteral("anExtensionManagerCard_cached_tools"));
    ASSERT_NE(cached_card, nullptr);

    auto* refresh_button = page.findChild<QPushButton*>(QStringLiteral("anExtensionManagerRefreshButton"));
    ASSERT_NE(refresh_button, nullptr);
    EXPECT_FALSE(refresh_button->isEnabled());

    auto* install_button = page.findChild<QPushButton*>(buttonObjectName("cached_tools", "primaryAction"));
    ASSERT_NE(install_button, nullptr);
    EXPECT_FALSE(install_button->isEnabled());
    EXPECT_TRUE(install_button->toolTip().contains(QStringLiteral("disabled by policy"), Qt::CaseInsensitive));
}

TEST_F(ExtensionManagerPageTest, DefaultProviderHonorsOfflineEnvironmentPolicy)
{
    ScopedEnvironmentVariable offline_policy("AFTERNIGHT_EXTENSION_REPOSITORY_OFFLINE", "1");

    an::ui::ExtensionManagerPage page(nullptr);
    page.loadSettings();

    auto* summary_banner = dynamic_cast<an::ui::AnInfoBanner*>(
        page.findChild<QWidget*>(QStringLiteral("anExtensionManagerSummaryBanner")));
    ASSERT_NE(summary_banner, nullptr);
    ASSERT_TRUE(
        waitFor([&]() { return summary_banner->message().contains(QStringLiteral("disabled by policy")); }, 5000));
    EXPECT_TRUE(summary_banner->message().contains(QStringLiteral("disabled by policy")));

    auto* refresh_button = page.findChild<QPushButton*>(QStringLiteral("anExtensionManagerRefreshButton"));
    ASSERT_NE(refresh_button, nullptr);
    EXPECT_FALSE(refresh_button->isEnabled());

    auto* more_actions_button = page.findChild<QToolButton*>(QStringLiteral("anExtensionManagerMoreActionsButton"));
    ASSERT_NE(more_actions_button, nullptr);
    EXPECT_TRUE(more_actions_button->isEnabled());

    auto* clean_action = findMoreActionsMenuAction(page, QStringLiteral("Clean unused environments"));
    ASSERT_NE(clean_action, nullptr);
    EXPECT_TRUE(clean_action->isEnabled());
}

TEST_F(ExtensionManagerPageTest, RepositoryRefreshLogsTheRequestAndItsOutcome)
{
    an::ui::ExtensionManagerPage page(nullptr);
    bool fail_refresh = false;
    page.setRepositoryProvider([&fail_refresh]() {
        ExtensionManagerPageRepositorySnapshot snapshot;
        if (fail_refresh)
        {
            snapshot.success = false;
            snapshot.diagnostic = QStringLiteral("The repository index answered with a malformed document.");
            return snapshot;
        }

        snapshot.success = true;
        snapshot.entries = {
            makeEntry("install_ext", "Install Ext", "Installable package", "1.0.0",
                      ExtensionRepositoryInstallState::NotInstalled),
            makeEntry("update_ext", "Update Ext", "Updateable package", "1.1.0",
                      ExtensionRepositoryInstallState::UpdateAvailable, "1.0.0"),
            makeEntry("current_ext", "Current Ext", "Current package", "2.0.0",
                      ExtensionRepositoryInstallState::Installed, "2.0.0"),
        };
        return snapshot;
    });
    page.loadSettings();

    auto* refresh_button = page.findChild<QPushButton*>(QStringLiteral("anExtensionManagerRefreshButton"));
    ASSERT_NE(refresh_button, nullptr);
    ASSERT_TRUE(refresh_button->isEnabled());

    const auto refreshed_since = std::chrono::system_clock::now();
    refresh_button->click();
    EXPECT_TRUE(loggedSince(refreshed_since,
                            {"Extension Manager repository refresh: Refresh button requested repository update"},
                            spdlog::level::info));
    EXPECT_TRUE(loggedSince(refreshed_since,
                            {"Extension Manager repository refresh: Repository refresh completed with 3 extension(s), "
                             "1 update(s) available"},
                            spdlog::level::info));

    fail_refresh = true;
    ASSERT_TRUE(refresh_button->isEnabled());
    const auto failed_since = std::chrono::system_clock::now();
    refresh_button->click();
    EXPECT_TRUE(
        loggedSince(failed_since,
                    {"Extension Manager repository refresh: The repository index answered with a malformed document."},
                    spdlog::level::warn));
}

TEST_F(ExtensionManagerPageTest, StartupExtensionUpdateCheckLogsWhetherItRunsAndWhatItFound)
{
    InMemorySettings settings;
    ASSERT_TRUE(settings.ready());
    const QString check_setting = QString::fromLatin1(SettingsKeys::GENERAL_CHECK_EXTENSION_UPDATES_ON_STARTUP);

    QWidget main_window;
    an::ui::DialogOrchestrationCoordinator::Dependencies dependencies;
    dependencies.mainWindow = &main_window;
    dependencies.globalSettingsPersistence = [&settings]() { return settings.persistence(); };
    const an::ui::DialogOrchestrationCoordinator coordinator(dependencies);

    ASSERT_TRUE(settings.persistence()->upsert(check_setting, QStringLiteral("0")));
    const auto disabled_since = std::chrono::system_clock::now();
    coordinator.checkExtensionUpdatesOnStartup();
    EXPECT_TRUE(loggedSince(
        disabled_since, {"Startup extension update check: Startup extension update check is disabled in settings"}));
    EXPECT_FALSE(loggedSince(disabled_since, {"Startup extension update check scheduled"}));

    ASSERT_TRUE(settings.persistence()->upsert(check_setting, QStringLiteral("1")));
    const auto enabled_since = std::chrono::system_clock::now();
    coordinator.checkExtensionUpdatesOnStartup();
    EXPECT_TRUE(loggedSince(enabled_since, {"Startup extension update check: Startup extension update check scheduled"},
                            spdlog::level::debug));

    // The check runs after the startup delay. This run keeps the repository offline, so what the check finds is the
    // policy that kept it from the network, logged where it ran and again where its answer arrived.
    constexpr std::string_view offline =
        "Startup extension update check: Extension repository network features are disabled by policy.";
    EXPECT_TRUE(waitFor([&]() { return countLoggedSince(enabled_since, offline) >= 2; }, 15000));
    EXPECT_TRUE(loggedSince(enabled_since, {offline}, spdlog::level::warn));
}

TEST_F(ExtensionManagerPageTest, ManagerPageWithoutDiscoveryProviderUsesTheCanonicalDiscoveryOptions)
{
    const ExtensionDiscoveryOptions canonical = an::ui::extensions::default_extension_discovery_options();

    an::ui::ExtensionManagerPage page(nullptr);
    page.setRepositoryProvider([] {
        ExtensionManagerPageRepositorySnapshot snapshot;
        snapshot.success = true;
        snapshot.indexUrl = QUrl(QStringLiteral("https://extensions.example.invalid/index.json"));
        snapshot.entries = {makeEntry("sample_ai", "Sample AI", "AI-assisted background extraction", "1.1.0",
                                      ExtensionRepositoryInstallState::NotInstalled)};
        return snapshot;
    });

    std::mutex mutex;
    std::optional<ExtensionDiscoveryOptions> cleanup_options;
    std::optional<ExtensionManagerPageInstallRequest> install_request;
    page.setEnvironmentCleanupProvider([&](const ExtensionManagerPageEnvironmentCleanupRequest& request) {
        {
            const std::lock_guard lock(mutex);
            cleanup_options = request.discovery_options;
        }
        ExtensionManagerPageEnvironmentCleanupResult result;
        result.success = true;
        return result;
    });
    page.setInstallProvider([&](const ExtensionManagerPageInstallRequest& request) {
        {
            const std::lock_guard lock(mutex);
            install_request = request;
        }
        ExtensionManagerPageInstallResult result;
        result.success = true;
        result.diagnostic = QStringLiteral("Installed Sample AI 1.1.0.");
        markStartupCachesPrimed(result);
        return result;
    });
    allowUnsignedExtensionInstalls(page);
    page.loadSettings();

    auto* summary_banner = dynamic_cast<an::ui::AnInfoBanner*>(
        page.findChild<QWidget*>(QStringLiteral("anExtensionManagerSummaryBanner")));
    ASSERT_NE(summary_banner, nullptr);

    auto* clean_action = findMoreActionsMenuAction(page, QStringLiteral("Clean unused environments"));
    ASSERT_NE(clean_action, nullptr);
    clean_action->trigger();
    ASSERT_TRUE(waitFor([&]() {
        return summary_banner->message().contains(QStringLiteral("No unused extension environments were found"));
    }));

    auto* install_button = page.findChild<QPushButton*>(buttonObjectName("sample_ai", "primaryAction"));
    ASSERT_NE(install_button, nullptr);
    ASSERT_TRUE(install_button->isEnabled());
    install_button->click();
    ASSERT_TRUE(waitFor([&]() { return summary_banner->message().contains(QStringLiteral("Installed Sample AI")); }));

    const std::lock_guard lock(mutex);
    ASSERT_TRUE(cleanup_options.has_value());
    ASSERT_TRUE(install_request.has_value());

    // The page discovers the packages where the application installs them, the repository's install root among them.
    EXPECT_EQ(cleanup_options->bundled_roots, canonical.bundled_roots);
    EXPECT_EQ(cleanup_options->repository_roots, canonical.repository_roots);
    EXPECT_EQ(cleanup_options->user_roots, canonical.user_roots);
    EXPECT_NE(std::find(cleanup_options->repository_roots.begin(), cleanup_options->repository_roots.end(),
                        an::ui::extensions::repository_extensions_root()),
              cleanup_options->repository_roots.end());
    ASSERT_FALSE(canonical.repository_roots.empty());
    EXPECT_EQ(install_request->installRoot, canonical.repository_roots.front());

    // Every operation states the runtime the application runs extensions in, so the environments it fingerprints are
    // the ones the application would use.
    const auto expect_canonical_context = [&canonical](const an::ui::extensions::ExtensionDiscoveryContext& context,
                                                       const char* operation) {
        EXPECT_EQ(context.runtime_target, canonical.context.runtime_target) << operation;
        EXPECT_EQ(context.environments_root, canonical.context.environments_root) << operation;
        EXPECT_EQ(context.runtime_root, canonical.context.runtime_root) << operation;
        EXPECT_EQ(context.bundled_python_executable, canonical.context.bundled_python_executable) << operation;
        EXPECT_EQ(context.extension_host_executable, canonical.context.extension_host_executable) << operation;
        EXPECT_EQ(context.host_bootstrap_fingerprint, canonical.context.host_bootstrap_fingerprint) << operation;
        EXPECT_FALSE(context.eager_settings_schema_inspection) << operation;
    };
    expect_canonical_context(cleanup_options->context, "environment cleanup");
    expect_canonical_context(install_request->context, "install");
}

TEST_F(ExtensionManagerPageTest, UsesThemeTokensAndDynamicButtonRoles)
{
    an::ui::ExtensionManagerPage page(nullptr);
    page.setRepositoryProvider([] {
        ExtensionManagerPageRepositorySnapshot snapshot;
        snapshot.success = true;
        snapshot.indexUrl = QUrl(QStringLiteral("https://extensions.example.invalid/index.json"));
        snapshot.entries = {
            makeEntry("install_ext", "Install Ext", "Installable package", "1.0.0",
                      ExtensionRepositoryInstallState::NotInstalled),
            makeEntry("update_ext", "Update Ext", "Updateable package", "1.1.0",
                      ExtensionRepositoryInstallState::UpdateAvailable, "1.0.0"),
        };
        return snapshot;
    });

    page.loadSettings();

    const auto& spacing = an::ui::tokens().spacing;
    auto* root_layout = qobject_cast<QVBoxLayout*>(page.layout());
    ASSERT_NE(root_layout, nullptr);
    EXPECT_EQ(root_layout->spacing(), spacing.lg);
    EXPECT_EQ(root_layout->contentsMargins(), QMargins());

    auto* actions_layout = page.findChild<QHBoxLayout*>(QStringLiteral("anExtensionManagerActionsLayout"));
    ASSERT_NE(actions_layout, nullptr);
    EXPECT_EQ(actions_layout->spacing(), spacing.sm);
    EXPECT_EQ(actions_layout->contentsMargins(), QMargins());

    auto* cards_scroll_area = page.findChild<QScrollArea*>(QStringLiteral("anExtensionManagerCardsScrollArea"));
    ASSERT_NE(cards_scroll_area, nullptr);
    EXPECT_TRUE(cards_scroll_area->widgetResizable());
    EXPECT_EQ(cards_scroll_area->frameShape(), QFrame::NoFrame);
    EXPECT_EQ(cards_scroll_area->horizontalScrollBarPolicy(), Qt::ScrollBarAlwaysOff);

    auto* cards_container = page.findChild<QWidget*>(QStringLiteral("anExtensionManagerCardsContainer"));
    ASSERT_NE(cards_container, nullptr);
    EXPECT_EQ(cards_scroll_area->widget(), cards_container);

    auto* cards_layout = page.findChild<QVBoxLayout*>(QStringLiteral("anExtensionManagerCardsLayout"));
    ASSERT_NE(cards_layout, nullptr);
    EXPECT_EQ(cards_layout->spacing(), spacing.sm);
    EXPECT_EQ(cards_layout->contentsMargins(), QMargins());

    auto* install_card = dynamic_cast<an::ui::AnCardWidget*>(
        page.findChild<QWidget*>(QStringLiteral("anExtensionManagerCard_install_ext")));
    ASSERT_NE(install_card, nullptr);
    EXPECT_TRUE(install_card->property("anCardCompact").toBool());
    EXPECT_TRUE(install_card->property("anCardBodyPadded").toBool());

    auto* card_content_layout = page.findChild<QVBoxLayout*>(fieldObjectName("install_ext", "contentLayout"));
    ASSERT_NE(card_content_layout, nullptr);
    EXPECT_EQ(card_content_layout->spacing(), spacing.sm);
    EXPECT_EQ(card_content_layout->contentsMargins(), QMargins(spacing.sm, spacing.sm, spacing.sm, spacing.sm));

    auto* title_layout = page.findChild<QHBoxLayout*>(fieldObjectName("install_ext", "titleLayout"));
    ASSERT_NE(title_layout, nullptr);
    EXPECT_EQ(title_layout->spacing(), spacing.sm);
    EXPECT_EQ(title_layout->contentsMargins(), QMargins());

    auto* details_layout = page.findChild<QVBoxLayout*>(fieldObjectName("install_ext", "detailsLayout"));
    ASSERT_NE(details_layout, nullptr);
    EXPECT_EQ(details_layout->spacing(), spacing.sm);
    EXPECT_EQ(details_layout->contentsMargins(), QMargins());

    auto* metadata_layout = page.findChild<QGridLayout*>(fieldObjectName("install_ext", "metadataLayout"));
    ASSERT_NE(metadata_layout, nullptr);
    EXPECT_EQ(metadata_layout->horizontalSpacing(), spacing.md);
    EXPECT_EQ(metadata_layout->verticalSpacing(), spacing.sm);
    EXPECT_EQ(metadata_layout->contentsMargins(), QMargins());

    auto* card_actions_layout = page.findChild<QHBoxLayout*>(fieldObjectName("install_ext", "actionsLayout"));
    ASSERT_NE(card_actions_layout, nullptr);
    EXPECT_EQ(card_actions_layout->spacing(), spacing.sm);
    EXPECT_EQ(card_actions_layout->contentsMargins(), QMargins());

    auto* refresh_button = page.findChild<QPushButton*>(QStringLiteral("anExtensionManagerRefreshButton"));
    auto* more_actions_button = page.findChild<QToolButton*>(QStringLiteral("anExtensionManagerMoreActionsButton"));
    auto* local_package_action = findMoreActionsMenuAction(page, QStringLiteral("Install local package..."));
    auto* clean_action = findMoreActionsMenuAction(page, QStringLiteral("Clean unused environments"));
    auto* cancel_button = page.findChild<QPushButton*>(QStringLiteral("anExtensionManagerCancelButton"));
    auto* install_button = page.findChild<QPushButton*>(buttonObjectName("install_ext", "primaryAction"));
    auto* remove_button = page.findChild<QPushButton*>(buttonObjectName("update_ext", "remove"));
    ASSERT_NE(refresh_button, nullptr);
    ASSERT_NE(more_actions_button, nullptr);
    ASSERT_NE(local_package_action, nullptr);
    ASSERT_NE(clean_action, nullptr);
    ASSERT_NE(cancel_button, nullptr);
    ASSERT_NE(install_button, nullptr);
    ASSERT_NE(remove_button, nullptr);
    EXPECT_EQ(page.findChild<QPushButton*>(buttonObjectName("update_ext", "settings")), nullptr);

    EXPECT_EQ(refresh_button->property("anButtonVariant").toString(), QStringLiteral("secondary"));
    EXPECT_EQ(more_actions_button->property("anButtonVariant").toString(), QStringLiteral("secondary"));
    EXPECT_EQ(cancel_button->property("anButtonVariant").toString(), QStringLiteral("danger"));
    EXPECT_EQ(install_button->property("anButtonVariant").toString(), QStringLiteral("primary"));
    EXPECT_EQ(remove_button->property("anButtonVariant").toString(), QStringLiteral("danger"));
    EXPECT_EQ(remove_button->property("anProcessButtonRole").toString(), QStringLiteral("danger"));
    EXPECT_EQ(cancel_button->property("anProcessButtonRole").toString(), QStringLiteral("danger"));

    EXPECT_TRUE(page.styleSheet().isEmpty());
    const auto widgets = page.findChildren<QWidget*>();
    for (const auto* widget : widgets)
    {
        ASSERT_NE(widget, nullptr);
        EXPECT_TRUE(widget->styleSheet().isEmpty()) << widget->objectName().toStdString();
    }
}

TEST_F(ExtensionManagerPageTest, ExtensionsDialogTabsUseTopAccentLineThemeProperties)
{
    const bool shown = inspectExtensionsDialog([](QDialog& dialog) {
        auto* tabs = dialog.findChild<QTabWidget*>(QStringLiteral("anExtensionsTabs"));
        auto* tab_bar = dialog.findChild<QTabBar*>(QStringLiteral("anExtensionsTabBar"));
        ASSERT_NE(tabs, nullptr);
        ASSERT_NE(tab_bar, nullptr);
        EXPECT_EQ(tabs->tabBar(), tab_bar);

        // The properties the proxy style paints a tab bar's accent line and height from.
        EXPECT_EQ(tab_bar->property("anTabAccentLinePosition").toString(), QStringLiteral("top"));
        EXPECT_DOUBLE_EQ(tab_bar->property("anTabHeightScale").toDouble(), 1.0);
        EXPECT_EQ(tab_bar->property("anTabMinHeight").toInt(), 22);
        EXPECT_FALSE(tab_bar->drawBase());
        ASSERT_EQ(tab_bar->count(), 2);
        EXPECT_EQ(tab_bar->tabText(0), QStringLiteral("Browse"));
        EXPECT_EQ(tab_bar->tabText(1), QStringLiteral("Installed"));

        // Each change applies as it is made, so the footer offers a reset and a close and nothing to commit.
        QStringList footer_buttons;
        for (const QPushButton* button : dialog.findChildren<QPushButton*>())
        {
            if (!tabs->isAncestorOf(button))
            {
                footer_buttons << button->text();
            }
        }
        footer_buttons.sort();
        EXPECT_EQ(footer_buttons, (QStringList{QStringLiteral("Close"), QStringLiteral("Reset Tab")}));

        auto* close_button = dialog.findChild<QPushButton*>(QStringLiteral("anExtensionsCloseButton"));
        ASSERT_NE(close_button, nullptr);
        EXPECT_TRUE(close_button->isDefault());
        EXPECT_EQ(close_button->property("anButtonVariant").toString(), QStringLiteral("primary"));
    });
    EXPECT_TRUE(shown);
}

TEST_F(ExtensionManagerPageTest, ExtensionsDialogCachesInstalledTabUntilPackageMutation)
{
    int discoveries = 0;
    const bool shown = inspectExtensionsDialog([&discoveries](QDialog& dialog) {
        auto* tabs = dialog.findChild<QTabWidget*>(QStringLiteral("anExtensionsTabs"));
        auto* manager_page = dialog.findChild<an::ui::ExtensionManagerPage*>();
        auto* status_page = dialog.findChild<ExtensionStatusPage*>();
        ASSERT_NE(tabs, nullptr);
        ASSERT_NE(manager_page, nullptr);
        ASSERT_NE(status_page, nullptr);

        // The Installed tab discovers the packages when it is first shown, not when the dialog opens.
        EXPECT_EQ(tabs->currentWidget(), manager_page);
        EXPECT_FALSE(status_page->isSettingsLoaded());
        status_page->setDiscoveryProvider([&discoveries]() {
            ++discoveries;
            return std::vector<an::ui::extensions::ExtensionLoadResult>{};
        });

        tabs->setCurrentWidget(status_page);
        EXPECT_EQ(discoveries, 1);
        tabs->setCurrentWidget(manager_page);
        tabs->setCurrentWidget(status_page);
        EXPECT_EQ(discoveries, 1) << "showing the Installed tab again discovered the packages again";

        // A package the Browse tab installed or removed makes the Installed tab discover again when it is next shown...
        tabs->setCurrentWidget(manager_page);
        emit manager_page->installedExtensionsChanged();
        EXPECT_EQ(discoveries, 1);
        EXPECT_FALSE(status_page->isSettingsLoaded());
        tabs->setCurrentWidget(status_page);
        EXPECT_EQ(discoveries, 2);

        // ...and at once when the Installed tab is the one shown.
        emit manager_page->installedExtensionsChanged();
        EXPECT_EQ(discoveries, 3);
        EXPECT_TRUE(status_page->isSettingsLoaded());
    });
    EXPECT_TRUE(shown);
}

TEST_F(ExtensionManagerPageTest, ExtensionsDialogTellsBrowseTabOfAnUninstallFromInstalledTab)
{
    std::atomic_bool installed = true;
    std::atomic_bool provider_started = false;
    std::atomic_bool finish_provider = false;
    const FinishProviderOnExit finish_on_exit{finish_provider};

    const bool shown = inspectExtensionsDialog([&](QDialog& dialog) {
        auto* tabs = dialog.findChild<QTabWidget*>(QStringLiteral("anExtensionsTabs"));
        auto* manager_page = dialog.findChild<an::ui::ExtensionManagerPage*>();
        auto* status_page = dialog.findChild<ExtensionStatusPage*>();
        ASSERT_NE(tabs, nullptr);
        ASSERT_NE(manager_page, nullptr);
        ASSERT_NE(status_page, nullptr);

        // The repository read the dialog started as it opened lands before the case states its own repository, so it
        // cannot replace what the case shows.
        drainThreadPool();
        manager_page->setRepositoryProvider([&installed]() {
            ExtensionManagerPageRepositorySnapshot snapshot;
            snapshot.success = true;
            snapshot.entries = {installed.load()
                                    ? makeEntry("sample_tools", "Sample Tools", "Sharpening and denoise tools", "2.0.0",
                                                ExtensionRepositoryInstallState::Installed, "2.0.0")
                                    : makeEntry("sample_tools", "Sample Tools", "Sharpening and denoise tools", "2.0.0",
                                                ExtensionRepositoryInstallState::NotInstalled)};
            return snapshot;
        });
        status_page->setDiscoveryProvider([&installed]() {
            std::vector<an::ui::extensions::ExtensionLoadResult> results;
            if (installed.load())
            {
                const std::filesystem::path package_root("/tmp/extensions/sample_tools");
                an::ui::extensions::ExtensionManifest manifest;
                manifest.id = "sample_tools";
                manifest.name = "Sample Tools";
                manifest.version = "2.0.0";
                manifest.entry_point = "demo";
                manifest.category = "filters";
                manifest.manifest_path = (package_root / "extension.json").string();
                manifest.package_root = package_root.string();

                an::ui::extensions::ExtensionLoadResult result;
                result.source = ExtensionPackageSource::Repository;
                result.status = an::ui::extensions::ExtensionLoadStatus::Ready;
                result.package_root = package_root;
                result.manifest_path = package_root / "extension.json";
                result.manifest = manifest;
                results.push_back(result);
            }
            return results;
        });
        status_page->setUninstallConfirmationProvider(
            [](const an::ui::extensions::ExtensionLoadResult&) { return true; });
        status_page->setUninstallProvider([&](const an::ui::extensions::ExtensionLoadResult&) {
            provider_started = true;
            while (!finish_provider.load())
            {
                QThread::msleep(1);
            }
            installed = false;
            return an::ui::ExtensionStatusPageUninstallResult{true, {}};
        });

        const auto browse_shows_installed = [manager_page]() {
            auto* remove_button = manager_page->findChild<QPushButton*>(buttonObjectName("sample_tools", "remove"));
            return remove_button && remove_button->property("anExtensionManagerActionAvailable").toBool();
        };
        const auto browse_offers_install = [manager_page]() {
            auto* install_button =
                manager_page->findChild<QPushButton*>(buttonObjectName("sample_tools", "primaryAction"));
            return install_button && install_button->text() == QStringLiteral("Install") &&
                   install_button->property("anExtensionManagerActionAvailable").toBool();
        };
        const auto uninstall_from_installed_tab = [&]() {
            auto* uninstall_button =
                status_page->findChild<QPushButton*>(QStringLiteral("anExtensionStatusButton_uninstall_sample_tools"));
            ASSERT_NE(uninstall_button, nullptr);
            provider_started = false;
            finish_provider = false;
            uninstall_button->click();
            ASSERT_TRUE(waitFor([&]() { return provider_started.load(); }));
        };

        manager_page->loadSettings();
        ASSERT_TRUE(browse_shows_installed());

        // The uninstall finishes while the Browse tab is the one shown.
        tabs->setCurrentWidget(status_page);
        uninstall_from_installed_tab();
        tabs->setCurrentWidget(manager_page);
        EXPECT_TRUE(browse_shows_installed());
        finish_provider = true;
        EXPECT_TRUE(waitFor(browse_offers_install))
            << "the Browse tab still shows a package the Installed tab uninstalled as installed";
        EXPECT_FALSE(browse_shows_installed());

        // The uninstall finishes while the Installed tab is the one shown: Browse is right before it is shown again.
        installed = true;
        manager_page->loadSettings();
        ASSERT_TRUE(browse_shows_installed());
        tabs->setCurrentWidget(status_page);
        status_page->refreshStatus(true);
        uninstall_from_installed_tab();
        finish_provider = true;
        EXPECT_TRUE(waitFor(browse_offers_install))
            << "the Browse tab was not told of an uninstall that finished behind it";
        EXPECT_EQ(tabs->currentWidget(), status_page);
    });
    EXPECT_TRUE(shown);
}

TEST_F(ExtensionManagerPageTest, UninstallElsewhereDropsWhatAnInstallRecorded)
{
    // The repository listing never reports the package as installed, as a listing read before the install would not:
    // the page shows it installed from what the install itself recorded.
    an::ui::ExtensionManagerPage page(nullptr);
    page.setRepositoryProvider([] {
        ExtensionManagerPageRepositorySnapshot snapshot;
        snapshot.success = true;
        snapshot.indexUrl = QUrl(QStringLiteral("https://extensions.example.invalid/index.json"));
        snapshot.entries = {makeEntry("sample_ai", "Sample AI", "AI-assisted background extraction", "1.1.0",
                                      ExtensionRepositoryInstallState::NotInstalled)};
        return snapshot;
    });
    allowUnsignedExtensionInstalls(page);

    int changed_count = 0;
    QObject::connect(&page, &an::ui::ExtensionManagerPage::installedExtensionsChanged, &page,
                     [&changed_count]() { ++changed_count; });
    page.setInstallProvider([](const ExtensionManagerPageInstallRequest&) {
        ExtensionManagerPageInstallResult result;
        result.success = true;
        result.diagnostic = QStringLiteral("Installed Sample AI 1.1.0.");
        markStartupCachesPrimed(result);
        return result;
    });

    const auto shows_installed = [&page]() {
        auto* remove_button = page.findChild<QPushButton*>(buttonObjectName("sample_ai", "remove"));
        return remove_button && remove_button->property("anExtensionManagerActionAvailable").toBool();
    };

    page.loadSettings();
    auto* install_button = page.findChild<QPushButton*>(buttonObjectName("sample_ai", "primaryAction"));
    ASSERT_NE(install_button, nullptr);
    ASSERT_TRUE(install_button->isEnabled());
    install_button->click();
    ASSERT_TRUE(waitFor([&]() { return changed_count == 1; }));
    ASSERT_TRUE(shows_installed());

    page.noteExtensionUninstalled("sample_ai");

    EXPECT_FALSE(shows_installed()) << "what the install recorded outlived the package";
    install_button = page.findChild<QPushButton*>(buttonObjectName("sample_ai", "primaryAction"));
    ASSERT_NE(install_button, nullptr);
    EXPECT_EQ(install_button->text(), QStringLiteral("Install"));
}

TEST_F(ExtensionManagerPageTest, UninstallElsewhereShowsCachedPackageNotInstalledAtOnce)
{
    drainThreadPool();
    QTemporaryDir cache_dir;
    ASSERT_TRUE(cache_dir.isValid());
    ScopedEnvironmentVariable cache_override("AFTERNIGHT_EXTENSION_MANAGER_CACHE_DIR",
                                             cache_dir.path().toUtf8().constData());
    writeRepositorySnapshotCache(
        cache_dir.path(), QJsonArray{cachedRepositoryEntry(
                              QStringLiteral("cached_tools"), QStringLiteral("Cached Tools"), QStringLiteral("1.2.3"),
                              QStringLiteral("1.2.3"), ExtensionRepositoryInstallState::Installed)});

    ThreadPoolGate gate;
    an::ui::ExtensionManagerPage page(nullptr);
    page.setDiscoveryOptionsProvider([]() { return buildUiOnlyDiscoveryOptions(); });

    // The run keeps the repository offline, so the cached snapshot is all the page has to show.
    page.loadSettings();
    auto* remove_button = page.findChild<QPushButton*>(buttonObjectName("cached_tools", "remove"));
    ASSERT_NE(remove_button, nullptr);
    ASSERT_TRUE(remove_button->property("anExtensionManagerActionAvailable").toBool());

    // The gate keeps back the refresh the note starts, so the case sees what the note alone shows.
    gate.close();
    page.noteExtensionUninstalled("cached_tools");

    remove_button = page.findChild<QPushButton*>(buttonObjectName("cached_tools", "remove"));
    auto* install_button = page.findChild<QPushButton*>(buttonObjectName("cached_tools", "primaryAction"));
    ASSERT_NE(remove_button, nullptr);
    ASSERT_NE(install_button, nullptr);
    EXPECT_FALSE(remove_button->property("anExtensionManagerActionAvailable").toBool());
    EXPECT_EQ(install_button->text(), QStringLiteral("Install"));

    // The cache is what the page shows when it next loads, so it no longer names an installed version either.
    QFile cache_file(QDir(cache_dir.path()).filePath(QStringLiteral("repository_snapshot.json")));
    ASSERT_TRUE(cache_file.open(QIODevice::ReadOnly));
    const QJsonArray cached_entries =
        QJsonDocument::fromJson(cache_file.readAll()).object().value(QStringLiteral("entries")).toArray();
    ASSERT_EQ(cached_entries.size(), 1);
    const QJsonObject cached_entry = cached_entries.first().toObject();
    EXPECT_TRUE(cached_entry.value(QStringLiteral("installed_version")).toString().isEmpty());
    EXPECT_EQ(cached_entry.value(QStringLiteral("install_state")).toInt(),
              static_cast<int>(ExtensionRepositoryInstallState::NotInstalled));

    gate.open();
    drainThreadPool();
}

// A refresh asked for while another is on its way is run when that one lands, as its own caller asked for it. A
// reset of the page asks for a refresh that clears the cards; an uninstall elsewhere, noted meanwhile, shows its
// package's card again and asks for a refresh that keeps the cards. That refresh took the wish of the one it waited
// behind, and cleared them.
TEST_F(ExtensionManagerPageTest, ARefreshThatWaitedBehindAnotherKeepsTheCardsItsOwnCallerAskedToKeep)
{
    drainThreadPool();
    QTemporaryDir cache_dir;
    ASSERT_TRUE(cache_dir.isValid());
    ScopedEnvironmentVariable cache_override("AFTERNIGHT_EXTENSION_MANAGER_CACHE_DIR",
                                             cache_dir.path().toUtf8().constData());
    writeRepositorySnapshotCache(
        cache_dir.path(), QJsonArray{cachedRepositoryEntry(
                              QStringLiteral("cached_tools"), QStringLiteral("Cached Tools"), QStringLiteral("1.2.3"),
                              QStringLiteral("1.2.3"), ExtensionRepositoryInstallState::Installed)});

    ThreadPoolGate gate;
    an::ui::ExtensionManagerPage page(nullptr);
    page.setDiscoveryOptionsProvider([]() { return buildUiOnlyDiscoveryOptions(); });

    // The run keeps the repository offline, so the page shows the cached snapshot and refreshes nothing at load.
    page.loadSettings();
    const QString card_name = QStringLiteral("anExtensionManagerCard_cached_tools");
    ASSERT_NE(page.findChild<QWidget*>(card_name), nullptr);

    // The reset's refresh clears the cards, and is held back on the pool.
    gate.close();
    page.resetToDefaults();
    ASSERT_EQ(page.findChild<QWidget*>(card_name), nullptr);
    ASSERT_TRUE(hasLabelContaining(page, QStringLiteral("Checking repository packages")));
    // The uninstall noted meanwhile shows the package's card as not installed and asks for a refresh that keeps it.
    page.noteExtensionUninstalled("cached_tools");
    ASSERT_NE(page.findChild<QWidget*>(card_name), nullptr);

    // The reset's refresh runs, and the pool is held again before its answer lands: the refresh that waited is asked
    // for as that answer lands, and stays on the pool.
    gate.open();
    QThreadPool::globalInstance()->waitForDone(30000);
    gate.close();
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);

    EXPECT_NE(page.findChild<QWidget*>(card_name), nullptr) << "the refresh that waited was asked to keep the cards";
    EXPECT_FALSE(hasLabelContaining(page, QStringLiteral("Checking repository packages")))
        << "the refresh that waited cleared the page as the one before it was asked to";

    gate.open();
    // The refresh that waited lands: offline, the repository lists nothing, and only now does the card go.
    EXPECT_TRUE(waitFor([&]() { return page.findChild<QWidget*>(card_name) == nullptr; }, 10000));
    drainThreadPool();
}

TEST_F(ExtensionManagerPageTest, PostMutationRepositoryRefreshPreservesBrowseCards)
{
    drainThreadPool();
    QTemporaryDir cache_dir;
    ASSERT_TRUE(cache_dir.isValid());
    ScopedEnvironmentVariable cache_override("AFTERNIGHT_EXTENSION_MANAGER_CACHE_DIR",
                                             cache_dir.path().toUtf8().constData());
    writeRepositorySnapshotCache(
        cache_dir.path(), QJsonArray{cachedRepositoryEntry(
                              QStringLiteral("cached_tools"), QStringLiteral("Cached Tools"), QStringLiteral("1.2.3"),
                              QStringLiteral("1.2.3"), ExtensionRepositoryInstallState::Installed)});

    ThreadPoolGate gate;
    an::ui::ExtensionManagerPage page(nullptr);
    page.setDiscoveryOptionsProvider([]() { return buildUiOnlyDiscoveryOptions(); });
    page.setEnvironmentCleanupProvider([&gate](const ExtensionManagerPageEnvironmentCleanupRequest&) {
        // Holds back what the page hands the pool next - the repository refresh after the cleanup - so the case sees
        // the page between the cleanup and that refresh.
        gate.close();
        ExtensionManagerPageEnvironmentCleanupResult result;
        result.success = true;
        result.removed_paths = {std::filesystem::path("/tmp/afternight-env-a")};
        return result;
    });

    // The run keeps the repository offline, so the page shows the cached snapshot and refreshes nothing at load.
    page.loadSettings();
    const QString card_name = QStringLiteral("anExtensionManagerCard_cached_tools");
    ASSERT_NE(page.findChild<QWidget*>(card_name), nullptr);

    auto* clean_action = findMoreActionsMenuAction(page, QStringLiteral("Clean unused environments"));
    ASSERT_NE(clean_action, nullptr);
    clean_action->trigger();

    auto* summary_banner = dynamic_cast<an::ui::AnInfoBanner*>(
        page.findChild<QWidget*>(QStringLiteral("anExtensionManagerSummaryBanner")));
    ASSERT_NE(summary_banner, nullptr);
    ASSERT_TRUE(waitFor(
        [&]() { return summary_banner->message().contains(QStringLiteral("1 unused extension environment")); }));

    // The refresh the cleanup started is waiting on the pool, and the cards it will replace are still shown.
    EXPECT_NE(page.findChild<QWidget*>(card_name), nullptr)
        << "the refresh after the cleanup cleared the cards before it had anything to show";
    EXPECT_FALSE(hasLabelContaining(page, QStringLiteral("Checking repository packages")));

    gate.open();
    // The refresh lands: offline, the repository lists nothing, and only now do the cached cards go.
    EXPECT_TRUE(waitFor([&]() { return page.findChild<QWidget*>(card_name) == nullptr; }, 10000));
}

TEST_F(ExtensionManagerPageTest, CachedRepositorySnapshotRefreshesInBackground)
{
    drainThreadPool();
    QTemporaryDir cache_dir;
    ASSERT_TRUE(cache_dir.isValid());
    ScopedEnvironmentVariable cache_override("AFTERNIGHT_EXTENSION_MANAGER_CACHE_DIR",
                                             cache_dir.path().toUtf8().constData());
    writeRepositorySnapshotCache(
        cache_dir.path(), QJsonArray{cachedRepositoryEntry(QStringLiteral("cached_tools"),
                                                           QStringLiteral("Cached Tools"), QStringLiteral("1.2.3"),
                                                           QString(), ExtensionRepositoryInstallState::NotInstalled)});

    ThreadPoolGate gate;
    an::ui::ExtensionManagerPage page(nullptr);
    page.setDiscoveryOptionsProvider([]() { return buildUiOnlyDiscoveryOptions(); });
    {
        // With the repository reachable the page shows the cached snapshot and refreshes it on the pool; the gate keeps
        // that refresh waiting, and the run's offline policy is back before the gate opens, so the refresh never
        // reaches the network.
        const ScopedEnvironmentVariable online("AFTERNIGHT_EXTENSION_REPOSITORY_OFFLINE", "0");
        gate.close();
        page.loadSettings();
    }

    auto* summary_banner = dynamic_cast<an::ui::AnInfoBanner*>(
        page.findChild<QWidget*>(QStringLiteral("anExtensionManagerSummaryBanner")));
    auto* refresh_button = page.findChild<QPushButton*>(QStringLiteral("anExtensionManagerRefreshButton"));
    ASSERT_NE(summary_banner, nullptr);
    ASSERT_NE(refresh_button, nullptr);

    // The load returned with the cached packages shown while the refresh has not run.
    EXPECT_NE(page.findChild<QWidget*>(QStringLiteral("anExtensionManagerCard_cached_tools")), nullptr);
    EXPECT_TRUE(summary_banner->message().contains(QStringLiteral("cached repository snapshot"), Qt::CaseInsensitive))
        << summary_banner->message().toStdString();
    EXPECT_TRUE(summary_banner->message().contains(QStringLiteral("Refreshing the repository in the background")))
        << summary_banner->message().toStdString();
    EXPECT_FALSE(refresh_button->isEnabled());

    gate.open();
    ASSERT_TRUE(waitFor(
        [&]() {
            return !summary_banner->message().contains(QStringLiteral("Refreshing the repository in the background"));
        },
        10000))
        << summary_banner->message().toStdString();
    EXPECT_TRUE(summary_banner->message().contains(QStringLiteral("disabled by policy")))
        << summary_banner->message().toStdString();
}

TEST_F(ExtensionManagerPageTest, RepositoryRefreshAndOperationsKeepLocalBrowseControlsAvailable)
{
    const auto expect_browse_controls_available = [](an::ui::ExtensionManagerPage& page, const std::string& entry_id,
                                                     const char* while_doing) {
        auto* search_edit = page.findChild<QLineEdit*>(QStringLiteral("anExtensionManagerSearchEdit"));
        auto* filter_combo = page.findChild<QComboBox*>(QStringLiteral("anExtensionManagerFilterCombo"));
        auto* sort_combo = page.findChild<QComboBox*>(QStringLiteral("anExtensionManagerSortCombo"));
        auto* allow_unsigned =
            page.findChild<QCheckBox*>(QStringLiteral("anExtensionManagerAllowUnsignedExtensionsCheck"));
        auto* details_button = page.findChild<QToolButton*>(buttonObjectName(entry_id, "details"));
        ASSERT_NE(search_edit, nullptr) << while_doing;
        ASSERT_NE(filter_combo, nullptr) << while_doing;
        ASSERT_NE(sort_combo, nullptr) << while_doing;
        ASSERT_NE(allow_unsigned, nullptr) << while_doing;
        ASSERT_NE(details_button, nullptr) << while_doing;
        EXPECT_TRUE(search_edit->isEnabled()) << while_doing;
        EXPECT_TRUE(filter_combo->isEnabled()) << while_doing;
        EXPECT_TRUE(sort_combo->isEnabled()) << while_doing;
        EXPECT_TRUE(allow_unsigned->isEnabled()) << while_doing;
        EXPECT_TRUE(details_button->isEnabled()) << while_doing;
    };

    // While the repository refreshes in the background.
    {
        drainThreadPool();
        QTemporaryDir cache_dir;
        ASSERT_TRUE(cache_dir.isValid());
        ScopedEnvironmentVariable cache_override("AFTERNIGHT_EXTENSION_MANAGER_CACHE_DIR",
                                                 cache_dir.path().toUtf8().constData());
        writeRepositorySnapshotCache(
            cache_dir.path(),
            QJsonArray{cachedRepositoryEntry(QStringLiteral("cached_tools"), QStringLiteral("Cached Tools"),
                                             QStringLiteral("1.2.3"), QStringLiteral("1.2.3"),
                                             ExtensionRepositoryInstallState::Installed)});

        ThreadPoolGate gate;
        an::ui::ExtensionManagerPage page(nullptr);
        page.setDiscoveryOptionsProvider([]() { return buildUiOnlyDiscoveryOptions(); });
        {
            const ScopedEnvironmentVariable online("AFTERNIGHT_EXTENSION_REPOSITORY_OFFLINE", "0");
            gate.close();
            page.loadSettings();
        }

        auto* refresh_button = page.findChild<QPushButton*>(QStringLiteral("anExtensionManagerRefreshButton"));
        ASSERT_NE(refresh_button, nullptr);
        ASSERT_FALSE(refresh_button->isEnabled()) << "the background refresh is not running";
        expect_browse_controls_available(page, "cached_tools", "while the repository refreshes");

        // Searching the cached packages works while the refresh runs.
        auto* search_edit = page.findChild<QLineEdit*>(QStringLiteral("anExtensionManagerSearchEdit"));
        ASSERT_NE(search_edit, nullptr);
        search_edit->setText(QStringLiteral("no such package"));
        EXPECT_EQ(page.findChild<QWidget*>(QStringLiteral("anExtensionManagerCard_cached_tools")), nullptr);
        search_edit->setText(QStringLiteral("Cached"));
        EXPECT_NE(page.findChild<QWidget*>(QStringLiteral("anExtensionManagerCard_cached_tools")), nullptr);

        gate.open();
        auto* summary_banner = dynamic_cast<an::ui::AnInfoBanner*>(
            page.findChild<QWidget*>(QStringLiteral("anExtensionManagerSummaryBanner")));
        ASSERT_NE(summary_banner, nullptr);
        EXPECT_TRUE(
            waitFor([&]() { return summary_banner->message().contains(QStringLiteral("disabled by policy")); }, 10000));
    }

    // While a package operation runs.
    an::ui::ExtensionManagerPage page(nullptr);
    page.setRepositoryProvider([] {
        ExtensionManagerPageRepositorySnapshot snapshot;
        snapshot.success = true;
        snapshot.indexUrl = QUrl(QStringLiteral("https://extensions.example.invalid/index.json"));
        snapshot.entries = {makeEntry("sample_ai", "Sample AI", "AI-assisted background extraction", "1.1.0",
                                      ExtensionRepositoryInstallState::NotInstalled)};
        return snapshot;
    });
    allowUnsignedExtensionInstalls(page);

    std::atomic_bool provider_started = false;
    std::atomic_bool finish_provider = false;
    FinishProviderOnExit finish_guard{finish_provider};
    page.setInstallProvider([&](const ExtensionManagerPageInstallRequest&) {
        provider_started = true;
        while (!finish_provider.load())
        {
            QThread::msleep(1);
        }
        ExtensionManagerPageInstallResult result;
        result.success = true;
        markStartupCachesPrimed(result);
        return result;
    });
    page.loadSettings();

    auto* install_button = page.findChild<QPushButton*>(buttonObjectName("sample_ai", "primaryAction"));
    ASSERT_NE(install_button, nullptr);
    install_button->click();
    ASSERT_TRUE(waitFor([&]() { return provider_started.load(); }));
    EXPECT_FALSE(install_button->isEnabled());
    expect_browse_controls_available(page, "sample_ai", "while a package installs");

    finish_provider.store(true);
    auto* summary_banner = dynamic_cast<an::ui::AnInfoBanner*>(
        page.findChild<QWidget*>(QStringLiteral("anExtensionManagerSummaryBanner")));
    ASSERT_NE(summary_banner, nullptr);
    EXPECT_TRUE(waitFor([&]() { return summary_banner->message().contains(QStringLiteral("Startup caches primed")); }));
}

TEST_F(ExtensionManagerPageTest, SuccessfulInstallMarksBrowseEntryInstalledBeforeRefresh)
{
    an::ui::ExtensionManagerPage page(nullptr);
    int repository_calls = 0;
    QString version_shown_when_refresh_began;
    page.setRepositoryProvider([&]() {
        ++repository_calls;
        if (repository_calls == 2)
        {
            // The refresh after the install: what the card says before the repository answers.
            const auto* version_label = page.findChild<QLabel*>(fieldObjectName("sample_ai", "version"));
            version_shown_when_refresh_began = version_label ? version_label->text() : QStringLiteral("<no card>");
        }

        // The repository has not caught up with the install, and still lists the package as not installed.
        ExtensionManagerPageRepositorySnapshot snapshot;
        snapshot.success = true;
        snapshot.indexUrl = QUrl(QStringLiteral("https://extensions.example.invalid/index.json"));
        snapshot.entries = {makeEntry("sample_ai", "Sample AI", "AI-assisted background extraction", "1.1.0",
                                      ExtensionRepositoryInstallState::NotInstalled)};
        return snapshot;
    });
    page.setInstallProvider([](const ExtensionManagerPageInstallRequest&) {
        ExtensionManagerPageInstallResult result;
        result.success = true;
        result.diagnostic = QStringLiteral("Installed Sample AI 1.1.0.");
        markStartupCachesPrimed(result);
        return result;
    });
    allowUnsignedExtensionInstalls(page);
    page.loadSettings();

    auto* version_label = page.findChild<QLabel*>(fieldObjectName("sample_ai", "version"));
    ASSERT_NE(version_label, nullptr);
    ASSERT_FALSE(version_label->text().contains(QStringLiteral("Installed:"))) << version_label->text().toStdString();

    auto* install_button = page.findChild<QPushButton*>(buttonObjectName("sample_ai", "primaryAction"));
    ASSERT_NE(install_button, nullptr);
    install_button->click();
    ASSERT_TRUE(waitFor([&]() { return repository_calls >= 2; }));

    EXPECT_TRUE(version_shown_when_refresh_began.contains(QStringLiteral("Installed: 1.1.0")))
        << version_shown_when_refresh_began.toStdString();

    // The stale answer does not take the install back.
    version_label = page.findChild<QLabel*>(fieldObjectName("sample_ai", "version"));
    ASSERT_NE(version_label, nullptr);
    EXPECT_TRUE(version_label->text().contains(QStringLiteral("Installed: 1.1.0")))
        << version_label->text().toStdString();
}

TEST_F(ExtensionManagerPageTest, LoadSettingsDisplaysInstalledRuntimeStatus)
{
    an::ui::ExtensionManagerPage page(nullptr);
    page.setRepositoryProvider([&]() {
        ExtensionManagerPageRepositorySnapshot snapshot;
        snapshot.success = true;

        auto ready = makeEntry("ready_ext", "Ready Extension", "Ready package", "1.0.0",
                               ExtensionRepositoryInstallState::Installed, "1.0.0");
        markInstalledStatus(ready, ExtensionEnvironmentState::Ready);

        auto bootstrapping = makeEntry("boot_ext", "Bootstrapping Extension", "Bootstrapping package", "1.0.0",
                                       ExtensionRepositoryInstallState::Installed, "1.0.0");
        markInstalledStatus(bootstrapping, ExtensionEnvironmentState::Bootstrapping,
                            "Provisioning wheels in the extension environment");

        auto failed = makeEntry("failed_ext", "Failed Extension", "Failed package", "1.0.0",
                                ExtensionRepositoryInstallState::Installed, "1.0.0");
        markInstalledStatus(failed, ExtensionEnvironmentState::Failed, "Validation failed after provisioning");

        auto update = makeEntry("update_ext", "Update Extension", "Update package", "1.1.0",
                                ExtensionRepositoryInstallState::UpdateAvailable, "1.0.0");
        markInstalledStatus(update, ExtensionEnvironmentState::Ready);

        snapshot.entries = {ready, bootstrapping, failed, update};
        return snapshot;
    });

    page.loadSettings();

    auto* ready_status = page.findChild<QLabel*>(fieldObjectName("ready_ext", "status"));
    ASSERT_NE(ready_status, nullptr);
    EXPECT_EQ(ready_status->text(), QStringLiteral("Ready"));
    auto* ready_status_badge = page.findChild<QLabel*>(fieldObjectName("ready_ext", "statusBadge"));
    ASSERT_NE(ready_status_badge, nullptr);
    EXPECT_EQ(ready_status_badge->text(), QStringLiteral("Ready"));
    EXPECT_TRUE(ready_status_badge->toolTip().contains(QStringLiteral("runtime environment are ready")));

    auto* boot_status = page.findChild<QLabel*>(fieldObjectName("boot_ext", "status"));
    ASSERT_NE(boot_status, nullptr);
    EXPECT_EQ(boot_status->text(), QStringLiteral("Bootstrapping"));
    auto* boot_status_badge = page.findChild<QLabel*>(fieldObjectName("boot_ext", "statusBadge"));
    ASSERT_NE(boot_status_badge, nullptr);
    EXPECT_TRUE(boot_status_badge->toolTip().contains(QStringLiteral("Provisioning wheels")));

    auto* boot_detail = page.findChild<QLabel*>(fieldObjectName("boot_ext", "statusDetail"));
    ASSERT_NE(boot_detail, nullptr);
    EXPECT_TRUE(boot_detail->text().contains(QStringLiteral("Provisioning wheels")));

    auto* failed_status = page.findChild<QLabel*>(fieldObjectName("failed_ext", "status"));
    ASSERT_NE(failed_status, nullptr);
    EXPECT_EQ(failed_status->text(), QStringLiteral("Failed"));

    auto* update_status = page.findChild<QLabel*>(fieldObjectName("update_ext", "status"));
    ASSERT_NE(update_status, nullptr);
    EXPECT_EQ(update_status->text(), QStringLiteral("Update available"));
}

TEST_F(ExtensionManagerPageTest, InstalledSourceControlsRepositoryActionsAndBootstrappingStatus)
{
    an::ui::ExtensionManagerPage page(nullptr);
    QTemporaryDir repository_root;
    ASSERT_TRUE(repository_root.isValid());
    QTemporaryDir user_root;
    ASSERT_TRUE(user_root.isValid());

    ExtensionDiscoveryOptions discovery_options;
    discovery_options.repository_roots.push_back(std::filesystem::path(repository_root.path().toStdString()));
    discovery_options.user_roots.push_back(std::filesystem::path(user_root.path().toStdString()));
    page.setDiscoveryOptionsProvider([&]() { return discovery_options; });

    std::atomic_int repository_calls = 0;
    page.setRepositoryProvider([&]() {
        ++repository_calls;
        ExtensionManagerPageRepositorySnapshot snapshot;
        snapshot.success = true;

        auto bundled = makeEntry("bundled_ext", "Bundled Extension", "Bundled package", "1.1.0",
                                 ExtensionRepositoryInstallState::Installed, "1.0.0");
        bundled.installed_source = ExtensionPackageSource::Bundled;
        markInstalledStatus(bundled, ExtensionEnvironmentState::Bootstrapping,
                            "Bundled post-install bootstrap is running");

        auto manual = makeEntry("manual_ext", "Manual Extension", "Manual package", "1.0.0",
                                ExtensionRepositoryInstallState::Installed, "1.0.0");
        manual.installed_source = ExtensionPackageSource::User;
        markInstalledStatus(manual, ExtensionEnvironmentState::Ready);

        auto repository = makeEntry("repo_ext", "Repository Extension", "Repository package", "1.1.0",
                                    ExtensionRepositoryInstallState::UpdateAvailable, "1.0.0");
        repository.installed_source = ExtensionPackageSource::Repository;
        markInstalledStatus(repository, ExtensionEnvironmentState::Ready);

        snapshot.entries = {bundled, manual, repository};
        return snapshot;
    });

    std::mutex remove_mutex;
    std::vector<ExtensionManagerPageRemoveRequest> remove_requests;
    page.setRemoveProvider([&](const ExtensionManagerPageRemoveRequest& request) {
        {
            const std::lock_guard lock(remove_mutex);
            remove_requests.push_back(request);
        }
        ExtensionManagerPageRemoveResult result;
        result.success = true;
        result.diagnostic = QStringLiteral("Removed extension.");
        return result;
    });

    allowBrowseExtensionUninstalls(page);
    page.loadSettings();

    auto* bundled_state = page.findChild<QLabel*>(fieldObjectName("bundled_ext", "state"));
    ASSERT_NE(bundled_state, nullptr);
    EXPECT_EQ(bundled_state->text(), QStringLiteral("Bundled"));

    auto* bundled_status = page.findChild<QLabel*>(fieldObjectName("bundled_ext", "status"));
    ASSERT_NE(bundled_status, nullptr);
    EXPECT_EQ(bundled_status->text(), QStringLiteral("Bootstrapping"));

    auto* bundled_status_detail = page.findChild<QLabel*>(fieldObjectName("bundled_ext", "statusDetail"));
    ASSERT_NE(bundled_status_detail, nullptr);
    EXPECT_TRUE(bundled_status_detail->text().contains(QStringLiteral("post-install bootstrap")));

    auto* bundled_primary = page.findChild<QPushButton*>(buttonObjectName("bundled_ext", "primaryAction"));
    ASSERT_NE(bundled_primary, nullptr);
    EXPECT_FALSE(bundled_primary->isEnabled());

    auto* bundled_remove = page.findChild<QPushButton*>(buttonObjectName("bundled_ext", "remove"));
    ASSERT_NE(bundled_remove, nullptr);
    EXPECT_FALSE(bundled_remove->isEnabled());

    EXPECT_EQ(page.findChild<QPushButton*>(buttonObjectName("bundled_ext", "settings")), nullptr);

    auto* manual_state = page.findChild<QLabel*>(fieldObjectName("manual_ext", "state"));
    ASSERT_NE(manual_state, nullptr);
    EXPECT_EQ(manual_state->text(), QStringLiteral("User-installed"));

    auto* manual_remove = page.findChild<QPushButton*>(buttonObjectName("manual_ext", "remove"));
    ASSERT_NE(manual_remove, nullptr);
    EXPECT_TRUE(manual_remove->isEnabled());
    manual_remove->click();
    ASSERT_TRUE(waitFor([&]() {
        const std::lock_guard lock(remove_mutex);
        return !remove_requests.empty();
    }));
    ASSERT_TRUE(waitFor([&]() { return repository_calls.load() >= 2; }));
    {
        const std::lock_guard lock(remove_mutex);
        ASSERT_EQ(remove_requests.size(), 1u);
        EXPECT_EQ(remove_requests.front().entry.id, "manual_ext");
        EXPECT_EQ(remove_requests.front().installRoot, std::filesystem::path(user_root.path().toStdString()));
    }

    EXPECT_EQ(page.findChild<QPushButton*>(buttonObjectName("manual_ext", "settings")), nullptr);

    auto* repository_state = page.findChild<QLabel*>(fieldObjectName("repo_ext", "state"));
    ASSERT_NE(repository_state, nullptr);
    EXPECT_EQ(repository_state->text(), QStringLiteral("Update available"));

    auto* repository_primary = page.findChild<QPushButton*>(buttonObjectName("repo_ext", "primaryAction"));
    ASSERT_NE(repository_primary, nullptr);
    EXPECT_TRUE(repository_primary->isEnabled());

    auto* repository_remove = page.findChild<QPushButton*>(buttonObjectName("repo_ext", "remove"));
    ASSERT_NE(repository_remove, nullptr);
    EXPECT_TRUE(repository_remove->isEnabled());
}
