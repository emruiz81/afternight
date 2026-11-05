// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Python.h must be included before glibc feature headers pulled in by Qt/STL headers.
#if defined(_MSC_VER) && defined(_DEBUG)
#define AN_RESTORE_DEBUG_AFTER_PYTHON_H
#undef _DEBUG
#endif

#include <Python.h>

#ifdef AN_RESTORE_DEBUG_AFTER_PYTHON_H
#define _DEBUG
#undef AN_RESTORE_DEBUG_AFTER_PYTHON_H
#endif

#include <pybind11/embed.h>
#include <pybind11/eval.h>
#include <pybind11/numpy.h>

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QApplication>
#include <QCborArray>
#include <QCborMap>
#include <QCborParserError>
#include <QCborValue>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFutureWatcher>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QLocalSocket>
#include <QMessageBox>
#include <QMetaObject>
#include <QPointer>
#include <QString>
#include <QStyleFactory>
#include <QThread>
#include <QTextStream>
#include <QThreadPool>
#include <QTimer>
#include <QUuid>
#include <QtConcurrent/QtConcurrentRun>

#include <cstdio>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cerrno>
#include <csignal>
#include <cmath>
#include <cstring>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <stop_token>
#include <string_view>
#include <thread>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#ifdef slots
#undef slots
#endif

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#include <unistd.h>
#endif

#include "ExtensionAppContext.h"
#include "ExtensionNativeProcessWindow.h"
#include "HostImageDescription.h"
#include "HostInFlightExecution.h"
#include "HostPreviewFrames.h"
#include "HostRuntimeSession.h"
#include "HostThemeSync.h"
#include "app/AfterNightExtensionProtocol.h"
#include "app/ExtensionCommon.h"
#include "app/ExtensionManager.h"
#include "app/ExtensionProtocolFraming.h"
#include "app/SharedBufferManager.h"
#include "controls/ImageViewControl.h"
#include "controls/settings/ProcessingLimits.h"
#include "session/IEngineSessionProvider.h"
#include "session/ImageDocument.h"
#include "theme/AnProxyStyle.h"
#include "widgets/ImageViewWidget.h"

#include <AfterNightConsumerRuntime/EngineSession.h>
#include <AfterNightConsumerRuntime/ImageAsset.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Image/ImageDescriptor.h>

namespace
{

namespace py = pybind11;

[[noreturn]] void simulate_crash_for_test()
{
#ifdef _WIN32
    RaiseFailFastException(nullptr, nullptr, 0);
#else
    // These synthetic test failures only need QProcess to observe CrashExit.
    // SIGKILL gives the manager that signal-terminated state without producing
    // a system coredump like SIGABRT/SIGSEGV would.
    std::raise(SIGKILL);
    _exit(128 + SIGKILL);
#endif
    std::abort();
}

constexpr const char* kSettingsActionUpdatePrefix = "@@AFTERNIGHT_SETTINGS_ACTION_UPDATE@@";
constexpr const char* kEmbeddedPythonVersion = "3.14";

struct ExtensionSessionPaths
{
    std::filesystem::path root;
    std::filesystem::path temp_dir;
    std::filesystem::path artifacts_dir;
};

std::filesystem::path executableDirectory()
{
#ifdef _WIN32
    wchar_t buffer[MAX_PATH];
    const DWORD length = ::GetModuleFileNameW(nullptr, buffer, MAX_PATH);
    if (length > 0)
    {
        return std::filesystem::path(buffer).parent_path();
    }
#else
    std::array<char, 4096> buffer{};
    const auto count = ::readlink("/proc/self/exe", buffer.data(), buffer.size() - 1);
    if (count > 0)
    {
        buffer[static_cast<std::size_t>(count)] = '\0';
        return std::filesystem::path(buffer.data()).parent_path();
    }
#endif

    return std::filesystem::current_path();
}

void configureQtPlatformForHeadlessRuntime()
{
#ifndef _WIN32
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM") && qEnvironmentVariableIsEmpty("DISPLAY") &&
        qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY"))
    {
        qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("offscreen"));
    }
#endif
}

bool isHeadlessQtPlatform()
{
    const QString platform = QGuiApplication::platformName().trimmed().toLower();
    return platform == QStringLiteral("offscreen") || platform == QStringLiteral("minimal");
}

bool isGuiThread()
{
    const QCoreApplication* app = QCoreApplication::instance();
    return !app || QThread::currentThread() == app->thread();
}

void ensureHostApplicationStyle()
{
    if (an::ui::installedAnProxyStyle())
        return;

    // Add the executable's directory to Qt's plugin search paths so that
    // the AfterNight Qt Style Plugin (styles/libqafternight*.so) is discoverable
    // via QStyleFactory without requiring a system-wide Qt plugin installation.
    QCoreApplication::addLibraryPath(QCoreApplication::applicationDirPath());

    if (auto* factory_style = QStyleFactory::create(QStringLiteral("afternight")))
    {
        // Plugin loaded successfully — Qt takes ownership via setStyle().
        // Register with the installedAnProxyStyle() tracker so downstream
        // code that calls installedAnProxyStyle() works correctly.
        QApplication::setStyle(factory_style);
        if (auto* proxy = dynamic_cast<an::ui::AnProxyStyle*>(factory_style))
        {
            an::ui::registerInstalledAnProxyStyle(proxy);
        }
    }
    else
    {
        // Plugin not found (dev run without deploy, CI, etc.) — fall back to
        // direct instantiation which always works since we link libAfterNightControls.
        an::ui::installAnProxyStyle();
    }

    if (auto* proxy_style = an::ui::installedAnProxyStyle())
    {
        proxy_style->refreshThemeTokens();
    }
}

QString currentApplicationStyleClassName()
{
    if (dynamic_cast<an::ui::AnProxyStyle*>(QApplication::style()) != nullptr)
    {
        return QStringLiteral("AnProxyStyle");
    }

    if (const auto* style = QApplication::style())
    {
        return QString::fromLatin1(style->metaObject()->className());
    }

    return {};
}

ExtensionSessionPaths materializeExtensionSessionPaths(const std::string& session_id)
{
    const std::filesystem::path root = an::ui::extensions::extensionHostSessionRootPath(session_id);
    const std::filesystem::path temp_dir = root / "temp";
    const std::filesystem::path artifacts_dir = root / "artifacts";

    std::error_code ec;
    std::filesystem::create_directories(temp_dir, ec);
    ec.clear();
    std::filesystem::create_directories(artifacts_dir, ec);

    return ExtensionSessionPaths{root, temp_dir, artifacts_dir};
}

/// The file the native window's preview frames are traced to, a line a frame, as `AFTERNIGHT_PREVIEW_FRAME_TRACE` names
/// it; empty when it names none, which traces nothing.
std::filesystem::path previewFrameTracePath()
{
    const QString named = qEnvironmentVariable("AFTERNIGHT_PREVIEW_FRAME_TRACE").trimmed();
    return named.isEmpty() ? std::filesystem::path() : std::filesystem::path(named.toStdWString());
}

std::filesystem::path defaultRuntimeRootForAppDir(const std::filesystem::path& app_dir)
{
#ifndef _WIN32
    if (const char* appdir = std::getenv("APPDIR"))
    {
        const auto candidate = std::filesystem::path(appdir) / "usr" / "lib" / "PythonLib";
        if (std::filesystem::exists(candidate))
        {
            return candidate.lexically_normal();
        }
    }
#endif

    const std::vector<std::filesystem::path> candidates = {app_dir / "PythonLib", app_dir / ".." / "PythonLib",
                                                           app_dir / ".." / ".." / "PythonLib"};

    for (const auto& candidate : candidates)
    {
        std::error_code ec;
        if (std::filesystem::exists(candidate, ec))
        {
            return candidate.lexically_normal();
        }
    }

    return {};
}

std::filesystem::path bundledPythonExecutableForRoots(const std::filesystem::path& app_dir,
                                                      const std::filesystem::path& runtime_root)
{
#ifdef _WIN32
    const std::vector<std::filesystem::path> candidates = {runtime_root / "python.exe", app_dir / "python.exe",
                                                           app_dir / "PythonLib" / "python.exe"};
#else
    const std::vector<std::filesystem::path> candidates = {runtime_root / "bin" / "python3.14",
                                                           runtime_root / "bin" / "python3",
                                                           runtime_root / "bin" / "python",
                                                           app_dir / "python3.14",
                                                           app_dir / "python3",
                                                           app_dir / "python"};
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

std::filesystem::path environmentPythonExecutable(const std::filesystem::path& environment_root)
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

std::filesystem::path environmentSitePackagesPath(const std::filesystem::path& environment_root)
{
#ifdef _WIN32
    return environment_root / "Lib" / "site-packages";
#else
    return environment_root / "lib" / "python3.14" / "site-packages";
#endif
}

std::filesystem::path runtimeStdlibPath(const std::filesystem::path& runtime_root)
{
#ifdef _WIN32
    return runtime_root / "Lib";
#else
    return runtime_root / "lib" / "python3.14";
#endif
}

std::filesystem::path runtimeStdlibZipPath(const std::filesystem::path& runtime_root)
{
#ifdef _WIN32
    return runtime_root.parent_path() / "python314.zip";
#else
    return {};
#endif
}

std::filesystem::path runtimeDynloadPath(const std::filesystem::path& runtime_root)
{
#ifdef _WIN32
    return runtime_root / "DLLs";
#else
    return runtime_root / "lib" / "python3.14" / "lib-dynload";
#endif
}

std::filesystem::path runtimeHostPackagePath(const std::filesystem::path& runtime_root)
{
#ifdef _WIN32
    return runtime_root / "Lib" / "site-packages";
#else
    return runtime_root / "lib" / "python3.14" / "site-packages";
#endif
}

std::filesystem::path findRepositoryPythonModulesPath(const std::filesystem::path& start)
{
    std::error_code ec;
    auto current = start;
    while (!current.empty())
    {
        const auto candidate = current / "python" / "modules" / "afternight" / "__init__.py";
        if (std::filesystem::exists(candidate, ec))
        {
            return (current / "python" / "modules").lexically_normal();
        }

        const auto parent = current.parent_path();
        if (parent == current)
        {
            break;
        }
        current = parent;
    }

    return {};
}

std::vector<std::filesystem::path> hostManagedAfternightImportRoots(const std::filesystem::path& app_dir,
                                                                    const std::filesystem::path& runtime_root)
{
    std::vector<std::filesystem::path> paths;
    const auto deployed_path = runtimeHostPackagePath(runtime_root);
    if (std::filesystem::exists(deployed_path / "afternight" / "__init__.py"))
    {
        paths.push_back(deployed_path.lexically_normal());
    }

    for (const auto& probe_root : {app_dir, std::filesystem::current_path()})
    {
        const auto repo_modules = findRepositoryPythonModulesPath(probe_root);
        if (!repo_modules.empty() && std::find(paths.begin(), paths.end(), repo_modules) == paths.end())
        {
            paths.push_back(repo_modules);
        }
    }

    return paths;
}

void removePySearchPathEntry(py::object sys_path, const std::string& value)
{
    while (true)
    {
        try
        {
            sys_path.attr("remove")(value);
        }
        catch (const py::error_already_set&)
        {
            break;
        }
    }
}

void eraseLoadedAfternightModules(py::module_ sys)
{
    py::dict modules = sys.attr("modules");
    py::list keys = py::list(modules.attr("keys")());
    for (py::handle key : keys)
    {
        const std::string name = py::str(key).cast<std::string>();
        if (name == "afternight" || name.rfind("afternight.", 0) == 0)
        {
            modules.attr("pop")(key, py::none());
        }
    }
}

py::module_ importHostManagedAfternight(const std::filesystem::path& app_dir, const std::filesystem::path& runtime_root)
{
    py::module_ sys = py::module_::import("sys");
    py::object sys_path = sys.attr("path");

    for (const auto& import_root : hostManagedAfternightImportRoots(app_dir, runtime_root))
    {
        if (!std::filesystem::exists(import_root / "afternight" / "__init__.py"))
        {
            continue;
        }

        const std::string root_text = import_root.string();
        removePySearchPathEntry(sys_path, root_text);
        sys_path.attr("insert")(0, root_text);
        eraseLoadedAfternightModules(sys);
        try
        {
            py::module_ afternight = py::module_::import("afternight");
            removePySearchPathEntry(sys_path, root_text);
            return afternight;
        }
        catch (...)
        {
            try
            {
                removePySearchPathEntry(sys_path, root_text);
            }
            catch (...)
            {
            }
            throw;
        }
    }

    throw std::runtime_error(
        "Host-managed afternight package is not available in the bundled runtime or repository modules");
}

bool setPyConfigString(PyConfig* config, wchar_t** target, const std::wstring& value, std::string* error)
{
    if (!config || !target)
    {
        if (error)
        {
            *error = "Python bootstrap received a null PyConfig target";
        }
        return false;
    }

    const PyStatus status = PyConfig_SetString(config, target, value.c_str());
    if (PyStatus_Exception(status))
    {
        if (error)
        {
            *error = status.err_msg ? status.err_msg : "Python bootstrap failed while populating PyConfig";
        }
        return false;
    }

    return true;
}

bool appendPySearchPath(PyConfig* config, const std::filesystem::path& path, std::string* error)
{
    if (!config || path.empty())
    {
        return true;
    }

    const PyStatus status = PyWideStringList_Append(&config->module_search_paths, path.wstring().c_str());
    if (PyStatus_Exception(status))
    {
        if (error)
        {
            *error = status.err_msg ? status.err_msg : "Failed to append a Python search path";
        }
        return false;
    }

    return true;
}

std::string pyExceptionMessage(const py::error_already_set& error)
{
    try
    {
        const std::string message = error.what();
        if (!message.empty())
        {
            return message;
        }

        py::module_ traceback = py::module_::import("traceback");
        py::object formatted = traceback.attr("format_exception")(error.type(), error.value(), error.trace());
        return py::str("").attr("join")(formatted).cast<std::string>();
    }
    catch (const std::exception& e)
    {
        return e.what();
    }
}

using an::ui::extensions::runtimeBackendModulePath;

/// What a native execute, preview or parameter action hands the extension: the image, the name its buffer is known by,
/// the file it came from, and the preview area exported of it, when there is one.
struct NativeSource
{
    std::shared_ptr<const an::consumer::ImageAsset> image;
    std::string name;
    std::string file_name;
    std::optional<QRect> region;
};

/// An image of the host's session, shared: what the host keeps and hands on is never written in place.
using SharedAsset = std::shared_ptr<const an::consumer::ImageAsset>;

/// An image of the host's session that one owner will show: held by shared ownership only so it can travel through a
/// worker's result, and moved out by the one that shows it.
using OwnedAsset = std::shared_ptr<an::consumer::ImageAsset>;

/// The session the host borrowed from the runtime backend, as its context lends it; null before it is borrowed.
const an::consumer::EngineSession* hostEngineSession()
{
    an::ui::IAppContext* context = an::ui::appContext();
    an::ui::IEngineSessionProvider* provider = context != nullptr ? context->getEngineSessionProvider() : nullptr;
    return provider != nullptr ? &provider->engineSession() : nullptr;
}

bool allocateSharedBuffer(const an::ui::extensions::SharedBufferManager& manager,
                          const an::ui::extensions::SharedImageBufferSpec& spec,
                          an::ui::extensions::SharedImageBuffer* out_buffer, std::string* error)
{
    an::Result<an::ui::extensions::SharedImageBuffer> allocated = manager.allocateBuffer(spec);
    if (!allocated.hasValue())
    {
        if (error)
        {
            *error = allocated.status().message();
        }
        return false;
    }
    *out_buffer = std::move(*allocated);
    return true;
}

bool openSharedBuffer(const an::ui::extensions::SharedBufferManager& manager,
                      const an::ui::extensions::SharedBufferDescriptor& descriptor,
                      an::ui::extensions::SharedImageBuffer* out_buffer, std::string* error)
{
    an::Result<an::ui::extensions::SharedImageBuffer> opened = manager.openBuffer(descriptor);
    if (!opened.hasValue())
    {
        if (error)
        {
            *error = opened.status().message();
        }
        return false;
    }
    *out_buffer = std::move(*opened);
    return true;
}

/// Exports an image of the host's session to a new shared buffer, read under a lease of its own.
bool exportSharedBuffer(const an::ui::extensions::SharedBufferManager& manager, const an::consumer::ImageAsset& image,
                        const an::ui::extensions::ImageExportRequest& request,
                        an::ui::extensions::SharedImageBuffer* out_buffer, std::string* error)
{
    an::Result<an::consumer::AssetReadLease> lease = image.read();
    if (!lease.hasValue())
    {
        if (error)
        {
            *error = std::string("The image could not be read for the extension: ") + lease.status().message();
        }
        return false;
    }
    an::Result<an::ui::extensions::SharedImageBuffer> exported = manager.exportImage(lease->image(), request);
    if (!exported.hasValue())
    {
        if (error)
        {
            *error = std::string("The image could not be handed to the extension: ") + exported.status().message();
        }
        return false;
    }
    *out_buffer = std::move(*exported);
    return true;
}

struct RuntimeImageBinding
{
    an::ui::extensions::SharedImageBuffer* buffer = nullptr;
    an::ui::extensions::SharedBufferDescriptor descriptor;
    bool writable = false;
    py::object metadata;
};

struct CurrentViewAuxiliaryBinding
{
    std::string token;
    an::ui::extensions::SharedBufferRole buffer_role = an::ui::extensions::SharedBufferRole::Mask;
};

constexpr int kMaxImageMetadataSidecarDepth = 64;

[[nodiscard]] bool isReservedMetadataKey(const std::string_view key)
{
    if (key.empty())
    {
        return false;
    }

    std::string upper;
    upper.reserve(key.size());
    std::transform(key.begin(), key.end(), std::back_inserter(upper),
                   [](const unsigned char ch) { return static_cast<char>(std::toupper(ch)); });

    return upper == "SIMPLE" || upper == "BITPIX" || upper == "BZERO" || upper == "BSCALE" || upper == "EXTEND" ||
           upper.rfind("NAXIS", 0) == 0;
}

[[nodiscard]] py::object jsonValueToPythonObject(const QJsonValue& value, const int depth = 0)
{
    if (depth > kMaxImageMetadataSidecarDepth)
    {
        throw std::runtime_error("Image metadata sidecar is nested too deeply");
    }

    if (value.isUndefined() || value.isNull())
    {
        return py::none();
    }
    if (value.isBool())
    {
        return py::bool_(value.toBool());
    }
    if (value.isDouble())
    {
        const double numeric = value.toDouble();
        const qint64 integer = static_cast<qint64>(numeric);
        if (std::floor(numeric) == numeric && numeric >= static_cast<double>(std::numeric_limits<qint64>::min()) &&
            numeric <= static_cast<double>(std::numeric_limits<qint64>::max()))
        {
            return py::int_(integer);
        }
        return py::float_(numeric);
    }
    if (value.isString())
    {
        return py::str(value.toString().toStdString());
    }
    if (value.isArray())
    {
        py::list array;
        for (const QJsonValue& entry : value.toArray())
        {
            array.append(jsonValueToPythonObject(entry, depth + 1));
        }
        return std::move(array);
    }
    if (value.isObject())
    {
        py::dict object;
        const QJsonObject values = value.toObject();
        for (auto it = values.begin(); it != values.end(); ++it)
        {
            object[py::str(it.key().toStdString())] = jsonValueToPythonObject(it.value(), depth + 1);
        }
        return std::move(object);
    }

    return py::none();
}

[[nodiscard]] py::dict loadMetadataSidecarDict(const std::string& sidecar_path)
{
    py::dict metadata;
    if (Py_IsInitialized() == 0)
    {
        return metadata;
    }
    if (sidecar_path.empty())
    {
        return metadata;
    }

    QFile file(QString::fromStdString(sidecar_path));
    if (!file.exists())
    {
        return metadata;
    }
    if (!file.open(QIODevice::ReadOnly))
    {
        throw std::runtime_error("Failed to open image metadata sidecar");
    }

    QJsonParseError parse_error;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parse_error);
    if (parse_error.error != QJsonParseError::NoError || !document.isObject())
    {
        throw std::runtime_error("Image metadata sidecar is not valid JSON");
    }

    const QJsonObject root = document.object();
    for (auto it = root.begin(); it != root.end(); ++it)
    {
        metadata[py::str(it.key().toStdString())] = jsonValueToPythonObject(it.value());
    }
    return metadata;
}

[[nodiscard]] py::dict bindingMetadataDict(const RuntimeImageBinding& binding)
{
    if (!binding.metadata || binding.metadata.is_none())
    {
        return py::dict();
    }
    return py::reinterpret_borrow<py::dict>(binding.metadata);
}

[[nodiscard]] py::dict ensureBindingMetadataDict(RuntimeImageBinding& binding)
{
    if (!binding.metadata || binding.metadata.is_none())
    {
        binding.metadata = py::dict();
    }
    return py::reinterpret_borrow<py::dict>(binding.metadata);
}

[[nodiscard]] QJsonValue pythonObjectToJsonValue(const py::handle& value, const int depth = 0)
{
    if (depth > kMaxImageMetadataSidecarDepth)
    {
        throw std::runtime_error("Image metadata sidecar is nested too deeply");
    }

    if (value.is_none())
    {
        return QJsonValue();
    }
    if (py::isinstance<py::bool_>(value))
    {
        return QJsonValue(value.cast<bool>());
    }
    if (py::isinstance<py::int_>(value))
    {
        return QJsonValue(static_cast<double>(value.cast<long long>()));
    }
    if (py::isinstance<py::float_>(value))
    {
        const double numeric_value = value.cast<double>();
        return std::isfinite(numeric_value) ? QJsonValue(numeric_value) : QJsonValue();
    }
    if (py::isinstance<py::str>(value))
    {
        return QJsonValue(QString::fromStdString(value.cast<std::string>()));
    }
    if (py::isinstance<py::list>(value) || py::isinstance<py::tuple>(value))
    {
        QJsonArray array;
        const py::sequence sequence = py::reinterpret_borrow<py::sequence>(value);
        for (const py::handle entry : sequence)
        {
            array.push_back(pythonObjectToJsonValue(entry, depth + 1));
        }
        return QJsonValue(array);
    }
    if (py::isinstance<py::dict>(value))
    {
        QJsonObject object;
        const py::dict dict_value = py::reinterpret_borrow<py::dict>(value);
        for (auto item : dict_value)
        {
            object.insert(QString::fromStdString(py::str(item.first).cast<std::string>()),
                          pythonObjectToJsonValue(item.second, depth + 1));
        }
        return QJsonValue(object);
    }

    return QJsonValue(QString::fromStdString(py::str(value).cast<std::string>()));
}

[[nodiscard]] bool pythonGilHeld() noexcept
{
    return Py_IsInitialized() != 0 && PyGILState_Check() != 0;
}

void writeMetadataSidecarDict(const std::string& sidecar_path, const py::dict& metadata)
{
    if (sidecar_path.empty())
    {
        return;
    }

    QFile file(QString::fromStdString(sidecar_path));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        throw std::runtime_error("Failed to open image metadata sidecar for writing");
    }

    QJsonObject root;
    for (auto item : metadata)
    {
        const std::string key = py::str(item.first).cast<std::string>();
        root.insert(QString::fromStdString(key), pythonObjectToJsonValue(item.second));
    }

    const QByteArray document = QJsonDocument(root).toJson(QJsonDocument::Indented);
    if (file.write(document) != document.size())
    {
        throw std::runtime_error("Failed to write image metadata sidecar");
    }
}

constexpr int kMaxPythonSettingsValueDepth = 64;

[[nodiscard]] QCborValue pythonObjectToCborValue(const py::handle& value, const int depth = 0)
{
    if (depth > kMaxPythonSettingsValueDepth)
    {
        throw std::runtime_error("Python settings value exceeds maximum nesting depth");
    }

    if (value.is_none())
    {
        return QCborValue(nullptr);
    }
    if (py::isinstance<py::bool_>(value))
    {
        return QCborValue(value.cast<bool>());
    }
    if (py::isinstance<py::int_>(value))
    {
        try
        {
            return QCborValue(static_cast<qint64>(value.cast<long long>()));
        }
        catch (const py::cast_error&)
        {
            throw std::runtime_error("Python settings integers must fit within signed 64-bit range");
        }
    }
    if (py::isinstance<py::float_>(value))
    {
        const double numeric_value = value.cast<double>();
        if (!std::isfinite(numeric_value))
        {
            throw std::runtime_error("Python settings numbers must be finite");
        }
        return QCborValue(numeric_value);
    }
    if (py::isinstance<py::str>(value))
    {
        return QCborValue(QString::fromStdString(value.cast<std::string>()));
    }
    if (py::isinstance<py::list>(value) || py::isinstance<py::tuple>(value))
    {
        QCborArray array;
        const py::sequence sequence = py::reinterpret_borrow<py::sequence>(value);
        for (const py::handle entry : sequence)
        {
            array.push_back(pythonObjectToCborValue(entry, depth + 1));
        }
        return QCborValue(array);
    }
    if (py::isinstance<py::dict>(value))
    {
        QCborMap map;
        const py::dict dict_value = py::reinterpret_borrow<py::dict>(value);
        for (auto item : dict_value)
        {
            if (!py::isinstance<py::str>(item.first))
            {
                throw std::runtime_error("Python settings objects require string keys");
            }

            map.insert(QString::fromStdString(item.first.cast<std::string>()),
                       pythonObjectToCborValue(item.second, depth + 1));
        }
        return QCborValue(map);
    }

    throw std::runtime_error("Python settings values must be JSON-serializable");
}

[[nodiscard]] py::object cborValueToPythonObject(const QCborValue& value, const int depth = 0)
{
    if (depth > kMaxPythonSettingsValueDepth)
    {
        throw std::runtime_error("Host settings value exceeds maximum nesting depth");
    }

    if (value.isUndefined() || value.isNull())
    {
        return py::none();
    }
    if (value.isBool())
    {
        return py::bool_(value.toBool());
    }
    if (value.isInteger())
    {
        return py::int_(value.toInteger());
    }
    if (value.isDouble())
    {
        return py::float_(value.toDouble());
    }
    if (value.isString())
    {
        return py::str(value.toString().toStdString());
    }
    if (value.isArray())
    {
        py::list array;
        const QCborArray values = value.toArray();
        for (const QCborValue& entry : values)
        {
            array.append(cborValueToPythonObject(entry, depth + 1));
        }
        return std::move(array);
    }
    if (value.isMap())
    {
        py::dict map;
        const QCborMap values = value.toMap();
        for (auto it = values.begin(); it != values.end(); ++it)
        {
            if (!it.key().isString())
            {
                throw std::runtime_error("Host settings objects require string keys");
            }

            map[py::str(it.key().toString().toStdString())] = cborValueToPythonObject(it.value(), depth + 1);
        }
        return std::move(map);
    }

    throw std::runtime_error("Host settings value is not JSON-serializable");
}

[[nodiscard]] py::dict cborMapToPythonDict(const QCborMap& values)
{
    py::dict map;
    for (auto it = values.begin(); it != values.end(); ++it)
    {
        if (!it.key().isString())
        {
            throw std::runtime_error("Host settings objects require string keys");
        }

        map[py::str(it.key().toString().toStdString())] = cborValueToPythonObject(it.value());
    }
    return map;
}

[[nodiscard]] py::list stringListToPythonList(const QStringList& values)
{
    py::list list;
    for (const QString& value : values)
    {
        list.append(py::str(value.toStdString()));
    }
    return list;
}

[[nodiscard]] QString diagnosticMessageFromMap(const QCborMap& map)
{
    for (const QString& key : {QStringLiteral("message"), QStringLiteral("detail"), QStringLiteral("text")})
    {
        const QCborValue value = map.value(key);
        if (value.isString() && !value.toString().trimmed().isEmpty())
        {
            return value.toString().trimmed();
        }
    }
    return {};
}

[[nodiscard]] bool diagnosticMapBlocksExecution(const QCborMap& map)
{
    const auto ok_it = map.find(QStringLiteral("ok"));
    if (ok_it != map.end() && ok_it.value().isBool() && !ok_it.value().toBool())
    {
        return true;
    }

    const auto valid_it = map.find(QStringLiteral("valid"));
    if (valid_it != map.end() && valid_it.value().isBool() && !valid_it.value().toBool())
    {
        return true;
    }

    const QString severity =
        map.value(QStringLiteral("severity")).toString(map.value(QStringLiteral("level")).toString()).toLower();
    return severity == QLatin1String("error") || severity == QLatin1String("fatal");
}

[[nodiscard]] std::optional<QString> blockingValidationDiagnostic(const QCborValue& diagnostics)
{
    if (diagnostics.isUndefined() || diagnostics.isNull())
    {
        return std::nullopt;
    }

    if (diagnostics.isString())
    {
        const QString message = diagnostics.toString().trimmed();
        if (!message.isEmpty())
        {
            return message;
        }
        return std::nullopt;
    }

    if (diagnostics.isArray())
    {
        const QCborArray array = diagnostics.toArray();
        for (const QCborValue& entry : array)
        {
            if (const std::optional<QString> message = blockingValidationDiagnostic(entry); message.has_value())
            {
                return message;
            }
        }
        return std::nullopt;
    }

    if (diagnostics.isMap())
    {
        const QCborMap map = diagnostics.toMap();
        if (diagnosticMapBlocksExecution(map))
        {
            const QString message = diagnosticMessageFromMap(map);
            return message.isEmpty() ? QStringLiteral("The extension reported invalid process parameters.") : message;
        }

        for (const QString& key : {QStringLiteral("diagnostics"), QStringLiteral("items"), QStringLiteral("errors")})
        {
            if (const QCborValue nested = map.value(key); !nested.isUndefined())
            {
                if (const std::optional<QString> message = blockingValidationDiagnostic(nested); message.has_value())
                {
                    return message;
                }
            }
        }
    }

    return std::nullopt;
}

[[nodiscard]] bool instanceMethodUsesImplementation(const py::object& instance, const char* method_name,
                                                    const py::object& implementation)
{
    if (!py::hasattr(instance, method_name))
    {
        return false;
    }

    const py::object method = instance.attr(method_name);
    if (!py::hasattr(method, "__func__"))
    {
        return false;
    }

    return method.attr("__func__").ptr() == implementation.ptr();
}

[[nodiscard]] bool instanceHasCallableMethod(const py::object& instance, const char* method_name)
{
    if (!py::hasattr(instance, method_name))
    {
        return false;
    }

    const py::object method = instance.attr(method_name);
    return PyCallable_Check(method.ptr()) != 0;
}

std::filesystem::path sharedBufferRootPathForDescriptor(const an::ui::extensions::SharedBufferDescriptor& descriptor)
{
    if (descriptor.mapped_file_path.empty())
    {
        return {};
    }

    const std::filesystem::path mapped_file_path =
        std::filesystem::path(descriptor.mapped_file_path).lexically_normal();
    const std::filesystem::path session_root = mapped_file_path.parent_path();
    return session_root.empty() ? std::filesystem::path{} : session_root.parent_path();
}

/// Imports a buffer as a new image of the host's session: an explicit copy of its samples, with the metadata its
/// sidecar states.
bool importSharedBuffer(const an::ui::extensions::SharedBufferDescriptor& descriptor,
                        an::ui::extensions::ImportedImage* out_image, std::string* error)
{
    if (!out_image)
    {
        if (error)
        {
            *error = "Host-local image output is null";
        }
        return false;
    }

    const an::consumer::EngineSession* session = hostEngineSession();
    if (session == nullptr)
    {
        if (error)
        {
            *error = "The extension host has no engine session to import the image into";
        }
        return false;
    }

    const std::filesystem::path shared_buffer_root = sharedBufferRootPathForDescriptor(descriptor);
    if (shared_buffer_root.empty())
    {
        if (error)
        {
            *error = "Shared buffer descriptor did not expose a managed root";
        }
        return false;
    }

    const an::ui::extensions::SharedBufferManager shared_buffer_manager(shared_buffer_root);
    an::Result<an::ui::extensions::ImportedImage> imported = shared_buffer_manager.importImage(*session, descriptor);
    if (!imported.hasValue())
    {
        if (error)
        {
            *error = imported.status().message();
        }
        return false;
    }
    *out_image = std::move(*imported);
    return true;
}

ImageViewControl* createHostLocalImageViewControlFromImage(ImageViewControl* custom_view_control,
                                                           an::ui::ImageDocument&& document, QWidget* parent,
                                                           const std::string& history_desc, const ViewControlMode mode,
                                                           ImageViewControl*, bool, bool)
{
    auto* app_context = dynamic_cast<an::ui::extensions::ExtensionAppContext*>(an::ui::appContext());
    if (!app_context || !document.valid())
    {
        return nullptr;
    }

    ImageViewControl* view_control = custom_view_control;
    if (!view_control)
    {
        view_control = new ImageViewControl(parent);
        const std::string unique_name =
            app_context->createViewControlUniqueName(document.names().displayName.toStdString());
        view_control->initialize(unique_name, mode);
    }

    view_control->setDocument(std::move(document));
    view_control->initializeNewImageHistory(history_desc.empty() ? std::string("Host synchronized image")
                                                                 : history_desc);
    view_control->displayDefaultViewChannel();
    return view_control;
}

std::optional<ImageCompositionMode> imageCompositionModeFromName(const std::string_view channel_view_mode) noexcept
{
    if (channel_view_mode == "rgb")
        return ImageCompositionMode::rgb_mode;
    if (channel_view_mode == "rgb_r")
        return ImageCompositionMode::r_mode;
    if (channel_view_mode == "rgb_g")
        return ImageCompositionMode::g_mode;
    if (channel_view_mode == "rgb_b")
        return ImageCompositionMode::b_mode;
    if (channel_view_mode == "cie_ab")
        return ImageCompositionMode::cieab_mode;
    if (channel_view_mode == "cie_l")
        return ImageCompositionMode::ciel_mode;
    if (channel_view_mode == "cie_a")
        return ImageCompositionMode::ciea_mode;
    if (channel_view_mode == "cie_b")
        return ImageCompositionMode::cieb_mode;
    if (channel_view_mode == "cie_c")
        return ImageCompositionMode::ciec_mode;
    if (channel_view_mode == "cie_h")
        return ImageCompositionMode::cieh_mode;
    if (channel_view_mode == "cie_x")
        return ImageCompositionMode::ciex_mode;
    if (channel_view_mode == "cie_y")
        return ImageCompositionMode::ciey_mode;
    if (channel_view_mode == "cie_z")
        return ImageCompositionMode::ciez_mode;
    if (channel_view_mode == "hsl_h")
        return ImageCompositionMode::hsl_h_mode;
    if (channel_view_mode == "hsl_s")
        return ImageCompositionMode::hsl_s_mode;
    if (channel_view_mode == "hsl_l")
        return ImageCompositionMode::hsl_l_mode;
    if (channel_view_mode == "hsv_h")
        return ImageCompositionMode::hsv_h_mode;
    if (channel_view_mode == "hsv_s")
        return ImageCompositionMode::hsv_s_mode;
    if (channel_view_mode == "hsv_v")
        return ImageCompositionMode::hsv_v_mode;
    if (channel_view_mode == "hsi_h")
        return ImageCompositionMode::hsi_h_mode;
    if (channel_view_mode == "hsi_s")
        return ImageCompositionMode::hsi_s_mode;
    if (channel_view_mode == "hsi_i")
        return ImageCompositionMode::hsi_i_mode;
    if (channel_view_mode == "ycbcr_y")
        return ImageCompositionMode::ycbcr_y_mode;
    if (channel_view_mode == "ycbcr_cb")
        return ImageCompositionMode::ycbcr_cb_mode;
    if (channel_view_mode == "ycbcr_cr")
        return ImageCompositionMode::ycbcr_cr_mode;
    return std::nullopt;
}

std::optional<ImageViewMode> imageViewModeFromName(const std::string_view image_display_mode) noexcept
{
    if (image_display_mode == "linear")
    {
        return ImageViewMode::LINEAR_MODE;
    }
    if (image_display_mode == "autostretch")
    {
        return ImageViewMode::AUTOSTRETCH_MODE;
    }
    return std::nullopt;
}

std::optional<MaskRenderingMode> maskRenderingModeFromName(const std::string_view mask_rendering_mode) noexcept
{
    if (mask_rendering_mode == "red")
        return MaskRenderingMode::red_mode;
    if (mask_rendering_mode == "green")
        return MaskRenderingMode::green_mode;
    if (mask_rendering_mode == "blue")
        return MaskRenderingMode::blue_mode;
    if (mask_rendering_mode == "yellow")
        return MaskRenderingMode::yellow_mode;
    if (mask_rendering_mode == "magenta")
        return MaskRenderingMode::magenta_mode;
    if (mask_rendering_mode == "cyan")
        return MaskRenderingMode::cyan_mode;
    if (mask_rendering_mode == "replace")
        return MaskRenderingMode::replace_mode;
    if (mask_rendering_mode == "multiply")
        return MaskRenderingMode::multiply_mode;
    return std::nullopt;
}

bool canApplyChannelViewModeToImage(const std::optional<an::ImageDescriptor>& image,
                                    const ImageCompositionMode mode) noexcept
{
    if (!image.has_value())
    {
        return false;
    }

    const auto channels = image->extent().channelCount();
    switch (mode)
    {
    case ImageCompositionMode::rgb_mode:
    case ImageCompositionMode::r_mode:
        return channels >= 1;
    case ImageCompositionMode::g_mode:
        return channels >= 2;
    case ImageCompositionMode::b_mode:
        return channels >= 3;
    default:
        return channels >= 3;
    }
}

void applyDisplaySettingsToViewControl(ImageViewControl* control, const QCborMap& display_settings)
{
    if (!control)
    {
        return;
    }

    if (const auto it = display_settings.find(QStringLiteral("image_display_mode"));
        it != display_settings.end() && it->isString())
    {
        if (const auto mode = imageViewModeFromName(it->toString().toStdString()))
        {
            control->setViewMode(*mode);
        }
    }

    if (const auto it = display_settings.find(QStringLiteral("channel_view_mode"));
        it != display_settings.end() && it->isString())
    {
        if (const auto mode = imageCompositionModeFromName(it->toString().toStdString()))
        {
            if (canApplyChannelViewModeToImage(control->imageDescriptor(), *mode))
            {
                control->displayViewChannel(*mode);
            }
            else
            {
                control->displayDefaultViewChannel();
            }
        }
    }

    if (const auto it = display_settings.find(QStringLiteral("mask_rendering_mode"));
        it != display_settings.end() && it->isString())
    {
        if (const auto mode = maskRenderingModeFromName(it->toString().toStdString()))
        {
            control->setMaskRenderingMode(*mode);
        }
    }

    if (const auto it = display_settings.find(QStringLiteral("mask_enabled"));
        it != display_settings.end() && it->isBool())
    {
        control->setEnableMask(it->toBool());
    }
    if (const auto it = display_settings.find(QStringLiteral("show_mask"));
        it != display_settings.end() && it->isBool())
    {
        control->setShowMask(it->toBool());
    }
    if (const auto it = display_settings.find(QStringLiteral("mask_inverted"));
        it != display_settings.end() && it->isBool())
    {
        control->setInvertMask(it->toBool());
    }

    if (ImageViewWidget* const view_widget = control->getViewWidget())
    {
        if (const auto it = display_settings.find(QStringLiteral("zoom_percent"));
            it != display_settings.end() && (it->isDouble() || it->isInteger()))
        {
            view_widget->scaleImageView(it->toDouble() / 100.0, true);
        }
    }

    if (const auto it = display_settings.find(QStringLiteral("viewport_offset"));
        it != display_settings.end() && it->isMap())
    {
        const QCborMap viewport_offset = it->toMap();
        const int offset_x = viewport_offset.value(QStringLiteral("x")).toInteger();
        const int offset_y = viewport_offset.value(QStringLiteral("y")).toInteger();
        control->setViewportOffset(QPoint(offset_x, offset_y));
    }
}

bool isResponseEnvelope(const an::ui::extensions::ExtensionProtocolMessage& message);

class PythonHostBridge
{
public:
    void setAppContext(an::ui::extensions::ExtensionAppContext* app_context) noexcept
    {
        app_context_ = app_context;
    }

    void setNativeProcessWindow(an::ui::extensions::ExtensionNativeProcessWindow* window) noexcept
    {
        native_process_window_ = window;
        native_process_window_hidden_by_extension_ = false;
    }

    void setLaunchMode(std::string launch_mode) noexcept
    {
        launch_mode_ = std::move(launch_mode);
        if (launch_mode_.empty())
        {
            launch_mode_ = "single_image";
        }
    }

    void setStandalone(const bool standalone) noexcept
    {
        standalone_ = standalone;
    }

    [[nodiscard]] bool isStandalone() const noexcept
    {
        return standalone_;
    }

    void attachSocket(QLocalSocket* socket, QByteArray* buffered_input, const std::string& session_id,
                      const std::string& extension_id) noexcept
    {
        socket_ = socket;
        buffered_input_ = buffered_input;
        session_id_ = session_id;
        extension_id_ = extension_id;
        current_view_id_ = session_id;
        current_file_name_ = current_view_name_;
    }

    void detachSocket() noexcept
    {
        socket_ = nullptr;
        buffered_input_ = nullptr;
        native_process_window_hidden_by_extension_ = false;
        clearImages();
        clearLocalViews();
        clearCurrentViewBuffers();
        resetManualViewCommitTracking();
    }

    void setCurrentRequestId(const std::string& request_id) noexcept
    {
        current_request_id_ = request_id;
        current_request_params_ = QCborMap{};
    }

    void setCurrentRequestContext(const std::string& request_id, const QCborMap& params)
    {
        current_request_id_ = request_id;
        current_request_params_ = params;
    }

    void clearCurrentRequest() noexcept
    {
        current_request_id_.clear();
        current_request_params_ = QCborMap{};
        cancelled_.store(false, std::memory_order_release);
    }

    void setSettingsActionUpdateCallback(std::function<void(const QCborMap&)> callback)
    {
        settings_action_update_callback_ = std::move(callback);
    }

    void clearSettingsActionUpdateCallback()
    {
        settings_action_update_callback_ = {};
    }

    void settingsActionUpdate(const QCborValue& value)
    {
        if (!settings_action_update_callback_ || !value.isMap())
        {
            return;
        }
        settings_action_update_callback_(value.toMap());
    }

    void resetManualViewCommitTracking() noexcept
    {
        manual_view_commit_emitted_ = false;
        manual_committed_image_.reset();
    }

    [[nodiscard]] bool manualViewCommitEmitted() const noexcept
    {
        return manual_view_commit_emitted_;
    }

    [[nodiscard]] OwnedAsset takeManualCommittedImage() noexcept
    {
        manual_view_commit_emitted_ = false;
        return std::move(manual_committed_image_);
    }

    void setCurrentViewName(const std::string& view_name) noexcept
    {
        current_view_name_ = view_name;
        if (current_file_name_.empty() || current_file_name_ == "Extension")
        {
            current_file_name_ = current_view_name_;
        }
    }

    bool handleInboundHostNotification(const an::ui::extensions::ExtensionProtocolMessage& inbound, bool* handled,
                                       std::string* error)
    {
        if (!handled)
        {
            if (error)
            {
                *error = "Host notification handled flag is null";
            }
            return false;
        }

        *handled = true;
        if (inbound.type == "view.describe")
        {
            an::ui::extensions::ViewDescribeMessage message;
            if (!an::ui::extensions::viewDescribeFromProtocolMessage(inbound, &message, error))
            {
                return false;
            }
            if (socket_ == nullptr && native_process_window_ == nullptr)
            {
                applyViewState(message.view_id, message.view_name, message.file_name, message.metadata,
                               message.display_settings);
                return true;
            }
            if (!synchronizeLocalView(message.view_id, message.view_name, message.file_name, message.source_buffer,
                                      message.auxiliary_buffers, message.metadata, message.display_settings, true,
                                      error))
            {
                return false;
            }
            if (!materializeCurrentViewBuffers(message.source_buffer, message.auxiliary_buffers, error))
            {
                return false;
            }
            applyViewState(message.view_id, message.view_name, message.file_name, message.metadata,
                           message.display_settings);
            return true;
        }
        if (inbound.type == "view.updated")
        {
            an::ui::extensions::ViewUpdatedMessage message;
            if (!an::ui::extensions::viewUpdatedFromProtocolMessage(inbound, &message, error))
            {
                return false;
            }

            if (socket_ == nullptr && native_process_window_ == nullptr)
            {
                applyViewState(message.view_id, current_view_name_, current_file_name_, message.metadata,
                               message.display_settings);
                return true;
            }

            const auto local_view_it = local_views_.find(message.view_id);
            const std::string local_view_name =
                local_view_it != local_views_.end() ? local_view_it->second.view_name : current_view_name_;
            const std::string local_file_name =
                local_view_it != local_views_.end() ? local_view_it->second.file_name : current_file_name_;
            if (!synchronizeLocalView(message.view_id, local_view_name, local_file_name, message.source_buffer,
                                      message.auxiliary_buffers, message.metadata, message.display_settings,
                                      current_view_id_.empty() || message.view_id == current_view_id_, error))
            {
                return false;
            }

            if (current_view_id_.empty() || message.view_id == current_view_id_)
            {
                if (!materializeCurrentViewBuffers(message.source_buffer, message.auxiliary_buffers, error))
                {
                    return false;
                }
                applyViewState(message.view_id, current_view_name_, current_file_name_, message.metadata,
                               message.display_settings);
            }
            return true;
        }
        if (inbound.type == "view.created")
        {
            an::ui::extensions::ViewCreatedMessage message;
            if (!an::ui::extensions::viewCreatedFromProtocolMessage(inbound, &message, error))
            {
                return false;
            }

            LocalHostViewState& local_view = local_views_[message.view_id];
            if (!message.view_name.empty())
            {
                local_view.view_name = message.view_name;
            }
            if (!message.file_name.empty())
            {
                local_view.file_name = message.file_name;
            }
            if (!message.selector_tooltip.empty())
            {
                local_view.selector_tooltip = message.selector_tooltip;
            }
            if (message.channels.has_value())
            {
                local_view.channels = *message.channels;
            }
            if (message.width.has_value())
            {
                local_view.width = *message.width;
            }
            if (message.height.has_value())
            {
                local_view.height = *message.height;
            }

            if (native_process_window_ &&
                (native_process_window_->hasTargetSelector() || native_process_window_->hasViewSelectorParams()))
            {
                std::map<std::string, QVariant> target_view;
                target_view["name"] = QString::fromStdString(
                    local_view.view_name.empty() ? (message.view_name.empty() ? message.view_id : message.view_name)
                                                 : local_view.view_name);
                if (!local_view.file_name.empty())
                {
                    target_view["file_name"] = QString::fromStdString(local_view.file_name);
                }
                if (!local_view.selector_tooltip.empty())
                {
                    target_view["selector_tooltip"] = QString::fromStdString(local_view.selector_tooltip);
                }
                if (local_view.channels.has_value())
                {
                    target_view["channels"] = *local_view.channels;
                }
                if (local_view.width.has_value())
                {
                    target_view["width"] = *local_view.width;
                }
                if (local_view.height.has_value())
                {
                    target_view["height"] = *local_view.height;
                }
                native_process_window_->addOrUpdateTargetSelectorView(target_view);
                native_process_window_->addOrUpdateViewSelectorView(target_view);
            }

            if (current_view_id_.empty() || current_view_id_ == session_id_)
            {
                applyViewIdentity(message.view_id, message.view_name, message.file_name);
            }
            return true;
        }
        if (inbound.type == "view.renamed")
        {
            an::ui::extensions::ViewRenamedMessage message;
            if (!an::ui::extensions::viewRenamedFromProtocolMessage(inbound, &message, error))
            {
                return false;
            }

            LocalHostViewState& local_view = local_views_[message.view_id];
            const std::string old_selector_name = local_view.view_name.empty() ? message.view_id : local_view.view_name;
            if (!message.view_name.empty())
            {
                local_view.view_name = message.view_name;
            }
            if (!message.file_name.empty())
            {
                local_view.file_name = message.file_name;
            }
            if (!message.selector_tooltip.empty())
            {
                local_view.selector_tooltip = message.selector_tooltip;
            }
            renameLocalView(message.view_id, message.view_name, message.file_name);

            if (native_process_window_ &&
                (native_process_window_->hasTargetSelector() || native_process_window_->hasViewSelectorParams()))
            {
                const std::string new_selector_name =
                    local_view.view_name.empty() ? (message.view_name.empty() ? message.view_id : message.view_name)
                                                 : local_view.view_name;
                native_process_window_->renameTargetSelectorView(
                    QString::fromStdString(old_selector_name), QString::fromStdString(new_selector_name),
                    QString::fromStdString(local_view.file_name), QString::fromStdString(local_view.selector_tooltip));
                native_process_window_->renameViewSelectorView(
                    QString::fromStdString(old_selector_name), QString::fromStdString(new_selector_name),
                    QString::fromStdString(local_view.file_name), QString::fromStdString(local_view.selector_tooltip));
            }

            if (current_view_id_.empty() || message.view_id == current_view_id_)
            {
                applyViewIdentity(message.view_id, message.view_name, message.file_name);
            }
            return true;
        }
        if (inbound.type == "view.destroyed")
        {
            an::ui::extensions::ViewDestroyedMessage message;
            if (!an::ui::extensions::viewDestroyedFromProtocolMessage(inbound, &message, error))
            {
                return false;
            }

            const auto local_view_it = local_views_.find(message.view_id);
            const std::string selector_name =
                local_view_it != local_views_.end() ? local_view_it->second.view_name : message.view_id;

            if (native_process_window_ &&
                (native_process_window_->hasTargetSelector() || native_process_window_->hasViewSelectorParams()))
            {
                native_process_window_->removeTargetSelectorView(QString::fromStdString(selector_name));
                native_process_window_->removeViewSelectorView(QString::fromStdString(selector_name));
            }

            destroyLocalView(message.view_id);
            if (message.view_id == current_view_id_)
            {
                clearViewState();
            }
            return true;
        }
        if (inbound.type == "theme.describe")
        {
            an::ui::extensions::ThemeDescribeMessage message;
            if (!an::ui::extensions::themeDescribeFromProtocolMessage(inbound, &message, error))
            {
                return false;
            }
            applyThemeState(message.theme_name, message.tokens);
            return true;
        }
        if (inbound.type == "theme.changed")
        {
            an::ui::extensions::ThemeChangedMessage message;
            if (!an::ui::extensions::themeChangedFromProtocolMessage(inbound, &message, error))
            {
                return false;
            }
            applyThemeState(message.theme_name, message.tokens);
            return true;
        }

        *handled = false;
        return true;
    }

    void setIgnoreCancel(const bool ignore_cancel) noexcept
    {
        ignore_cancel_ = ignore_cancel;
    }

    void setInspectSettingsSnapshot(QCborMap snapshot)
    {
        inspect_settings_snapshot_ = std::move(snapshot);
    }

    void bindImage(std::string token, RuntimeImageBinding binding)
    {
        std::unique_ptr<py::gil_scoped_acquire> gil;
        if (Py_IsInitialized() != 0)
        {
            gil = std::make_unique<py::gil_scoped_acquire>();
        }
        binding.metadata = loadMetadataSidecarDict(binding.descriptor.metadata_sidecar);
        images_[std::move(token)] = std::move(binding);
    }

    void bindCurrentViewImage(std::string token, RuntimeImageBinding binding)
    {
        std::unique_ptr<py::gil_scoped_acquire> gil;
        if (Py_IsInitialized() != 0)
        {
            gil = std::make_unique<py::gil_scoped_acquire>();
        }
        binding.metadata = loadMetadataSidecarDict(binding.descriptor.metadata_sidecar);
        current_view_images_[std::move(token)] = std::move(binding);
    }

    void clearImages()
    {
        std::unique_ptr<py::gil_scoped_acquire> gil;
        if (Py_IsInitialized() != 0)
        {
            gil = std::make_unique<py::gil_scoped_acquire>();
        }
        images_.clear();
        snapshot_image_buffers_.clear();
    }

    [[nodiscard]] QCborMap currentViewMetadata() const
    {
        return current_view_metadata_;
    }

    [[nodiscard]] QCborMap currentViewDisplaySettings() const
    {
        return current_view_display_settings_;
    }

    [[nodiscard]] const std::string& currentViewSourceImageToken() const noexcept
    {
        return current_view_source_token_;
    }

    [[nodiscard]] py::dict currentViewInfo() const
    {
        py::dict info;
        info["view_name"] = py::str(current_view_name_);
        info["file_name"] = py::str(current_file_name_);

        const auto set_empty_dimensions = [&]() {
            info["width"] = py::int_(0);
            info["height"] = py::int_(0);
            info["channels"] = py::int_(0);
            info["pixel_type"] = py::str("");
        };
        const auto set_descriptor_dimensions = [&](const an::ui::extensions::SharedBufferDescriptor& descriptor) {
            info["width"] = py::int_(static_cast<int>(descriptor.width));
            info["height"] = py::int_(static_cast<int>(descriptor.height));
            info["channels"] = py::int_(static_cast<int>(descriptor.channels));
            info["pixel_type"] =
                py::str(std::string(an::ui::extensions::sharedBufferPixelTypeName(descriptor.pixel_type)));
        };

        if (current_view_source_buffer_.has_value() && current_view_source_buffer_->valid())
        {
            set_descriptor_dimensions(current_view_source_buffer_->descriptor());
            return info;
        }
        if (const auto source_it = images_.find("source"); source_it != images_.end())
        {
            set_descriptor_dimensions(source_it->second.descriptor);
            return info;
        }
        if (isGuiThread())
        {
            if (const std::optional<an::ui::extensions::HostImageDimensions> image =
                    an::ui::extensions::viewImageDimensions(nativeSourceView()))
            {
                info["width"] = py::int_(image->width);
                info["height"] = py::int_(image->height);
                info["channels"] = py::int_(image->channels);
                info["pixel_type"] = py::str(image->pixelType);
                return info;
            }
        }

        set_empty_dimensions();
        return info;
    }

    [[nodiscard]] py::dict currentViewAuxiliaryImageBindings() const
    {
        py::dict bindings;
        for (const auto& [symbolic_name, binding] : current_view_auxiliary_bindings_)
        {
            py::dict entry;
            entry[py::str("token")] = py::str(binding.token);
            entry[py::str("buffer_role")] =
                py::str(std::string(an::ui::extensions::sharedBufferRoleName(binding.buffer_role)));
            bindings[py::str(symbolic_name)] = std::move(entry);
        }
        return bindings;
    }

    py::object settingsGet(const std::string& key, const py::object& default_value)
    {
        QCborValue value;
        std::string error;
        if (!settingValue(QString::fromStdString(key), pythonObjectToCborValue(default_value), &value, &error))
        {
            throw std::runtime_error(error);
        }
        return cborValueToPythonObject(value);
    }

    void settingsSet(const std::string& key, const py::object& value)
    {
        std::string error;
        if (!setSettingValue(QString::fromStdString(key), pythonObjectToCborValue(value), &error))
        {
            throw std::runtime_error(error);
        }
    }

    bool settingValue(const QString& key, const QCborValue& default_value, QCborValue* out_value, std::string* error)
    {
        if (!out_value)
        {
            if (error)
            {
                *error = "Extension setting output is null";
            }
            return false;
        }
        if (native_process_window_)
        {
            const QCborValue transient_value =
                native_process_window_->transientExternalToolSettingValue(key, QCborValue());
            if (!transient_value.isUndefined())
            {
                *out_value = transient_value;
                return true;
            }
        }
        if (!socket_)
        {
            const auto it = inspect_settings_snapshot_.constFind(key);
            *out_value = it != inspect_settings_snapshot_.constEnd() ? it.value() : default_value;
            return true;
        }

        an::ui::extensions::ExtensionProtocolMessage request;
        request.type = "settings.get";
        request.session_id = session_id_;
        request.request_id = an::ui::extensions::generateOpaqueToken();
        request.fields = QCborMap{{QStringLiteral("key"), key}, {QStringLiteral("default_value"), default_value}};

        an::ui::extensions::ExtensionProtocolMessage response;
        if (!sendProtocolRequestAwaitResponse(request, &response, error))
        {
            return false;
        }

        *out_value = response.fields.value(QStringLiteral("value"));
        return true;
    }

    bool setSettingValue(const QString& key, const QCborValue& value, std::string* error)
    {
        if (!socket_)
        {
            return true;
        }

        an::ui::extensions::ExtensionProtocolMessage request;
        request.type = "settings.set";
        request.session_id = session_id_;
        request.request_id = an::ui::extensions::generateOpaqueToken();
        request.fields = QCborMap{{QStringLiteral("key"), key}, {QStringLiteral("value"), value}};

        an::ui::extensions::ExtensionProtocolMessage response;
        return sendProtocolRequestAwaitResponse(request, &response, error);
    }

    void settingsRemove(const std::string& key)
    {
        if (!socket_)
        {
            return;
        }

        an::ui::extensions::ExtensionProtocolMessage request;
        request.type = "settings.remove";
        request.session_id = session_id_;
        request.request_id = an::ui::extensions::generateOpaqueToken();
        request.fields = QCborMap{{QStringLiteral("key"), QString::fromStdString(key)}};

        an::ui::extensions::ExtensionProtocolMessage response;
        std::string error;
        if (!sendProtocolRequestAwaitResponse(request, &response, &error))
        {
            throw std::runtime_error(error);
        }
    }

    py::list settingsKeys()
    {
        py::list keys;
        if (!socket_)
        {
            for (auto it = inspect_settings_snapshot_.cbegin(); it != inspect_settings_snapshot_.cend(); ++it)
            {
                keys.append(py::str(it.key().toString().toStdString()));
            }
            return keys;
        }

        an::ui::extensions::ExtensionProtocolMessage request;
        request.type = "settings.keys";
        request.session_id = session_id_;
        request.request_id = an::ui::extensions::generateOpaqueToken();

        an::ui::extensions::ExtensionProtocolMessage response;
        std::string error;
        if (!sendProtocolRequestAwaitResponse(request, &response, &error))
        {
            throw std::runtime_error(error);
        }

        const QCborValue key_values = response.fields.value(QStringLiteral("keys"));
        if (!key_values.isArray())
        {
            throw std::runtime_error("settings.keys response is invalid");
        }

        const QCborArray array = key_values.toArray();
        for (const QCborValue& entry : array)
        {
            if (!entry.isString())
            {
                throw std::runtime_error("settings.keys response contains a non-string key");
            }
            keys.append(py::str(entry.toString().toStdString()));
        }

        return keys;
    }

    void logEvent(const std::string& severity, const std::string& component, const std::string& message)
    {
        if (!socket_)
        {
            return;
        }

        an::ui::extensions::ExtensionProtocolMessage request;
        request.type = "logging.event";
        request.session_id = session_id_;
        request.request_id = an::ui::extensions::generateOpaqueToken();
        request.fields = QCborMap{{QStringLiteral("severity"), QString::fromStdString(severity)},
                                  {QStringLiteral("component"), QString::fromStdString(component)},
                                  {QStringLiteral("message"), QString::fromStdString(message)}};

        an::ui::extensions::ExtensionProtocolMessage response;
        std::string error;
        if (!sendProtocolRequestAwaitResponse(request, &response, &error))
        {
            throw std::runtime_error(error);
        }
    }

    QCborMap hostProtocolRequest(const std::string& type, const QCborMap& fields = {})
    {
        if (!socket_)
        {
            throw std::runtime_error("The extension host bridge has no active control socket");
        }

        an::ui::extensions::ExtensionProtocolMessage request;
        request.type = type;
        request.session_id = session_id_;
        request.request_id = an::ui::extensions::generateOpaqueToken();
        request.fields = fields;

        an::ui::extensions::ExtensionProtocolMessage response;
        std::string error;
        if (!sendProtocolRequestAwaitResponse(request, &response, &error))
        {
            throw std::runtime_error(error.empty() ? type + " failed" : error);
        }
        return response.fields;
    }

    bool uiNotify(const std::string& type, const std::string& title, const std::string& message, std::string* error)
    {
        if (!socket_)
        {
            if (error)
            {
                *error = "The extension host bridge has no active control socket";
            }
            return false;
        }

        an::ui::extensions::ExtensionProtocolMessage request;
        request.type = type;
        request.session_id = session_id_;
        request.request_id = an::ui::extensions::generateOpaqueToken();
        request.fields = QCborMap{{QStringLiteral("title"), QString::fromStdString(title)},
                                  {QStringLiteral("message"), QString::fromStdString(message)}};

        an::ui::extensions::ExtensionProtocolMessage response;
        return sendProtocolRequestAwaitResponse(request, &response, error);
    }

    bool uiConfirm(const std::string& title, const std::string& message, const bool warn, bool* confirmed,
                   std::string* error)
    {
        if (!socket_)
        {
            if (error)
            {
                *error = "The extension host bridge has no active control socket";
            }
            return false;
        }

        an::ui::extensions::ExtensionProtocolMessage request;
        request.type = "ui.confirm";
        request.session_id = session_id_;
        request.request_id = an::ui::extensions::generateOpaqueToken();
        request.fields = QCborMap{{QStringLiteral("title"), QString::fromStdString(title)},
                                  {QStringLiteral("message"), QString::fromStdString(message)},
                                  {QStringLiteral("warn"), warn}};

        an::ui::extensions::ExtensionProtocolMessage response;
        if (!sendProtocolRequestAwaitResponse(request, &response, error))
        {
            return false;
        }

        const auto confirmed_it = response.fields.find(QStringLiteral("confirmed"));
        if (confirmed_it == response.fields.end() || !confirmed_it->isBool())
        {
            if (error)
            {
                *error = "ui.confirm result is missing the confirmed flag";
            }
            return false;
        }

        if (confirmed)
        {
            *confirmed = confirmed_it->toBool();
        }
        return true;
    }

    bool uiOpenImage(const py::array& array, py::dict metadata, const std::string& title, std::string* error)
    {
        if (!socket_)
        {
            if (error)
            {
                *error = "The extension host bridge has no active control socket";
            }
            return false;
        }

        const ParsedGeneratedImageSpec spec = parseGeneratedImageSpec(array);
        const std::string request_id = an::ui::extensions::generateOpaqueToken();
        const std::string buffer_id = request_id + ".ui_open_image";
        const auto session_paths = materializeExtensionSessionPaths(session_id_);
        const std::filesystem::path buffer_file_path = session_paths.root / (buffer_id + ".bin");

        an::ui::extensions::SharedImageBufferSpec output_spec;
        output_spec.sessionId = session_id_;
        output_spec.bufferId = buffer_id;
        output_spec.symbolicName = "generated_image";
        output_spec.width = spec.width;
        output_spec.height = spec.height;
        output_spec.channels = spec.channels;
        output_spec.pixelType = spec.pixel_type;
        output_spec.pixelFormat = spec.pixel_format;
        output_spec.colorSpace = spec.color_space;
        output_spec.accessMode = an::ui::extensions::SharedBufferAccessMode::ReadWrite;
        output_spec.bufferRole = an::ui::extensions::SharedBufferRole::Destination;
        if (py::len(metadata) > 0)
        {
            auto sidecar_path = buffer_file_path;
            sidecar_path += ".metadata.json";
            output_spec.metadataSidecar = sidecar_path.lexically_normal().generic_string();
        }

        an::ui::extensions::SharedBufferManager manager(session_paths.root.parent_path());
        an::ui::extensions::SharedImageBuffer output_buffer;
        if (!allocateSharedBuffer(manager, output_spec, &output_buffer, error))
        {
            return false;
        }

        RuntimeImageBinding binding;
        binding.buffer = &output_buffer;
        binding.descriptor = output_buffer.descriptor();
        binding.writable = true;
        binding.metadata = metadata;
        copyArrayIntoPlanarBuffer(array, binding);
        if (!binding.descriptor.metadata_sidecar.empty())
        {
            writeMetadataSidecarDict(binding.descriptor.metadata_sidecar, bindingMetadataDict(binding));
        }

        an::ui::extensions::ExtensionProtocolMessage request;
        request.type = "ui.open_image";
        request.session_id = session_id_;
        request.request_id = request_id;
        request.fields = QCborMap{
            {QStringLiteral("buffer"), an::ui::extensions::sharedBufferDescriptorToCborMap(output_buffer.descriptor())},
            {QStringLiteral("title"), QString::fromStdString(title)}};

        an::ui::extensions::ExtensionProtocolMessage response;
        return sendProtocolRequestAwaitResponse(request, &response, error);
    }

    bool hideNativeProcessWindow() noexcept
    {
        if (!native_process_window_)
        {
            return false;
        }
        if (!isGuiThread())
        {
            QPointer<an::ui::extensions::ExtensionNativeProcessWindow> window(native_process_window_);
            bool hidden = false;
            const bool invoked = QMetaObject::invokeMethod(
                native_process_window_,
                [this, window, &hidden]() {
                    if (window)
                    {
                        hidden = hideNativeProcessWindow();
                    }
                },
                Qt::BlockingQueuedConnection);
            return invoked && hidden;
        }

        native_process_window_hidden_by_extension_ = true;
        native_process_window_->hide();
        return true;
    }

    /// Query the UI host for the list of all open image views via the
    /// views.describe protocol message.  Returns a Python list of dicts
    /// with per-view metadata (name, file_name, width, height, channels,
    /// has_mask, is_active, …).
    py::list listViews()
    {
        py::list result;
        if (!socket_)
        {
            return result;
        }

        an::ui::extensions::ExtensionProtocolMessage request;
        request.type = "views.describe";
        request.session_id = session_id_;
        request.request_id = an::ui::extensions::generateOpaqueToken();

        an::ui::extensions::ExtensionProtocolMessage response;
        std::string error;
        if (!sendProtocolRequestAwaitResponse(request, &response, &error))
        {
            throw std::runtime_error(error.empty() ? "views.describe request failed" : error);
        }

        const auto views_it = response.fields.find(QStringLiteral("views"));
        if (views_it == response.fields.end() || !views_it->isArray())
        {
            return result;
        }

        const QCborArray views_array = views_it->toArray();
        for (const auto& entry : views_array)
        {
            if (!entry.isMap())
            {
                continue;
            }

            py::object py_view = cborValueToPythonObject(entry);
            result.append(py_view);
        }

        return result;
    }

    py::dict snapshotView(const std::string& view_name)
    {
        if (!socket_)
        {
            throw std::runtime_error("The extension host bridge has no active control socket");
        }

        an::ui::extensions::ExtensionProtocolMessage request;
        request.type = "views.snapshot";
        request.session_id = session_id_;
        request.request_id = an::ui::extensions::generateOpaqueToken();
        if (!view_name.empty())
        {
            request.fields = QCborMap{{QStringLiteral("view_name"), QString::fromStdString(view_name)}};
        }

        an::ui::extensions::ExtensionProtocolMessage response;
        std::string error;
        if (!sendProtocolRequestAwaitResponse(request, &response, &error))
        {
            throw std::runtime_error(error.empty() ? "views.snapshot request failed" : error);
        }
        if (response.type != "views.snapshot.result")
        {
            throw std::runtime_error("Unexpected response type for views.snapshot");
        }

        auto open_descriptor = [&](const an::ui::extensions::SharedBufferDescriptor& descriptor,
                                   an::ui::extensions::SharedImageBuffer* out_buffer) -> bool {
            if (descriptor.mapped_file_path.empty())
            {
                error = "Shared buffer descriptor did not define a mapped_file_path";
                return false;
            }

            const std::filesystem::path file_path =
                std::filesystem::path(descriptor.mapped_file_path).lexically_normal();
            const std::filesystem::path session_root = file_path.parent_path();
            const std::filesystem::path root_path = session_root.parent_path();
            if (session_root.empty() || root_path.empty())
            {
                error = "Shared buffer descriptor did not expose a usable managed root";
                return false;
            }

            an::ui::extensions::SharedBufferManager manager(root_path);
            return openSharedBuffer(manager, descriptor, out_buffer, &error);
        };

        an::ui::extensions::SharedBufferDescriptor source_descriptor;
        if (!an::ui::extensions::sharedBufferDescriptorFromCborValue(response.fields.value(QStringLiteral("buffer")),
                                                                     &source_descriptor, &error))
        {
            throw std::runtime_error(error.empty() ? "Host snapshot response returned an invalid buffer" : error);
        }

        an::ui::extensions::SharedImageBuffer source_buffer;
        if (!open_descriptor(source_descriptor, &source_buffer))
        {
            throw std::runtime_error(error.empty() ? "Failed to open snapshot source buffer" : error);
        }

        const std::string resolved_view_name =
            response.fields.value(QStringLiteral("view_name")).toString().toStdString();
        const std::string source_token = std::string("snapshot:source:") + source_descriptor.buffer_id;
        auto source_insert = snapshot_image_buffers_.insert_or_assign(source_token, std::move(source_buffer));

        RuntimeImageBinding source_binding;
        source_binding.buffer = &source_insert.first->second;
        source_binding.descriptor = source_descriptor;
        source_binding.writable = false;
        bindImage(source_token, std::move(source_binding));

        py::dict snapshot;
        snapshot[py::str("view_id")] = py::str(resolved_view_name);
        snapshot[py::str("view_name")] = py::str(resolved_view_name);
        snapshot[py::str("source_token")] = py::str(source_token);
        snapshot[py::str("metadata")] = cborValueToPythonObject(response.fields.value(QStringLiteral("metadata")));
        snapshot[py::str("file_name")] =
            py::str(response.fields.value(QStringLiteral("file_name")).toString().toStdString());
        snapshot[py::str("width")] = py::int_(static_cast<int>(source_descriptor.width));
        snapshot[py::str("height")] = py::int_(static_cast<int>(source_descriptor.height));
        snapshot[py::str("channels")] = py::int_(static_cast<int>(source_descriptor.channels));
        snapshot[py::str("pixel_type")] =
            py::str(std::string(an::ui::extensions::sharedBufferPixelTypeName(source_descriptor.pixel_type)));

        py::dict auxiliary_images;
        const QCborValue auxiliary_value = response.fields.value(QStringLiteral("auxiliary_buffers"));
        if (!auxiliary_value.isUndefined())
        {
            if (!auxiliary_value.isArray())
            {
                throw std::runtime_error("Host snapshot response returned invalid auxiliary_buffers");
            }

            const QCborArray auxiliary_array = auxiliary_value.toArray();
            for (const QCborValue& entry : auxiliary_array)
            {
                an::ui::extensions::SharedBufferDescriptor descriptor;
                if (!an::ui::extensions::sharedBufferDescriptorFromCborValue(entry, &descriptor, &error))
                {
                    throw std::runtime_error(
                        error.empty() ? "Host snapshot response returned an invalid auxiliary buffer" : error);
                }

                an::ui::extensions::SharedImageBuffer auxiliary_buffer;
                if (!open_descriptor(descriptor, &auxiliary_buffer))
                {
                    throw std::runtime_error(error.empty() ? "Failed to open snapshot auxiliary buffer" : error);
                }

                const std::string symbolic_name =
                    descriptor.symbolic_name.empty() ? descriptor.buffer_id : descriptor.symbolic_name;
                const std::string token = std::string("snapshot:aux:") + descriptor.buffer_id;
                auto auxiliary_insert = snapshot_image_buffers_.insert_or_assign(token, std::move(auxiliary_buffer));

                RuntimeImageBinding binding;
                binding.buffer = &auxiliary_insert.first->second;
                binding.descriptor = descriptor;
                binding.writable = false;
                bindImage(token, std::move(binding));

                py::dict binding_info;
                binding_info[py::str("token")] = py::str(token);
                binding_info[py::str("buffer_role")] =
                    py::str(std::string(an::ui::extensions::sharedBufferRoleName(descriptor.buffer_role)));
                auxiliary_images[py::str(symbolic_name)] = std::move(binding_info);
            }
        }
        snapshot[py::str("auxiliary_images")] = std::move(auxiliary_images);
        return snapshot;
    }

    [[nodiscard]] bool nativeProcessWindowHiddenByExtension() const noexcept
    {
        return native_process_window_hidden_by_extension_;
    }

    bool bindView(const std::string& view_name, std::string* error)
    {
        if (!socket_)
        {
            if (error)
            {
                *error = "The extension host bridge has no active control socket";
            }
            return false;
        }
        if (view_name.empty())
        {
            if (error)
            {
                *error = "Target view name is empty";
            }
            return false;
        }

        an::ui::extensions::ExtensionProtocolMessage request;
        request.type = "views.bind";
        request.session_id = session_id_;
        request.request_id = an::ui::extensions::generateOpaqueToken();
        request.fields = QCborMap{{QStringLiteral("view_name"), QString::fromStdString(view_name)}};

        an::ui::extensions::ExtensionProtocolMessage response;
        if (!sendProtocolRequestAwaitResponse(request, &response, error))
        {
            return false;
        }
        if (response.type != "views.bind.result")
        {
            if (error)
            {
                *error = "Unexpected response type for views.bind";
            }
            return false;
        }

        const QString resolved_view_id = response.fields.value(QStringLiteral("view_id")).toString();
        const QString resolved_view_name = response.fields.value(QStringLiteral("view_name")).toString();
        if (resolved_view_id.isEmpty() || resolved_view_name.isEmpty())
        {
            if (error)
            {
                *error = "Host bind response omitted the resolved view identity";
            }
            return false;
        }

        an::ui::extensions::SharedBufferDescriptor source_buffer;
        if (!an::ui::extensions::sharedBufferDescriptorFromCborValue(response.fields.value(QStringLiteral("buffer")),
                                                                     &source_buffer, error))
        {
            return false;
        }

        std::vector<an::ui::extensions::SharedBufferDescriptor> auxiliary_buffers;
        const QCborValue auxiliary_value = response.fields.value(QStringLiteral("auxiliary_buffers"));
        if (!auxiliary_value.isUndefined())
        {
            if (!auxiliary_value.isArray())
            {
                if (error)
                {
                    *error = "Host bind response returned invalid auxiliary_buffers";
                }
                return false;
            }

            const QCborArray auxiliary_array = auxiliary_value.toArray();
            auxiliary_buffers.reserve(auxiliary_array.size());
            for (const QCborValue& entry : auxiliary_array)
            {
                an::ui::extensions::SharedBufferDescriptor descriptor;
                if (!an::ui::extensions::sharedBufferDescriptorFromCborValue(entry, &descriptor, error))
                {
                    return false;
                }
                auxiliary_buffers.push_back(std::move(descriptor));
            }
        }

        QCborMap metadata;
        const QCborValue metadata_value = response.fields.value(QStringLiteral("metadata"));
        if (!metadata_value.isUndefined())
        {
            if (!metadata_value.isMap())
            {
                if (error)
                {
                    *error = "Host bind response returned invalid metadata";
                }
                return false;
            }
            metadata = metadata_value.toMap();
        }

        QCborMap display_settings;
        const QCborValue display_settings_value = response.fields.value(QStringLiteral("display_settings"));
        if (!display_settings_value.isUndefined())
        {
            if (!display_settings_value.isMap())
            {
                if (error)
                {
                    *error = "Host bind response returned invalid display settings";
                }
                return false;
            }
            display_settings = display_settings_value.toMap();
        }

        const std::string resolved_file_name =
            response.fields.value(QStringLiteral("file_name")).toString().toStdString();
        const std::string resolved_view_id_std = resolved_view_id.toStdString();
        const std::string resolved_view_name_std = resolved_view_name.toStdString();

        if (!synchronizeLocalView(resolved_view_id_std, resolved_view_name_std, resolved_file_name, source_buffer,
                                  auxiliary_buffers, metadata, display_settings, true, error))
        {
            return false;
        }
        if (!materializeCurrentViewBuffers(source_buffer, auxiliary_buffers, error))
        {
            return false;
        }

        applyViewState(resolved_view_id_std, resolved_view_name_std, resolved_file_name, metadata, display_settings);
        return true;
    }

    bool closeNativeProcessWindow() noexcept
    {
        if (!native_process_window_)
        {
            // A host with no window - one running headless - ends the session as closing the window would.
            return closeWithoutWindow();
        }
        if (!isGuiThread())
        {
            QPointer<an::ui::extensions::ExtensionNativeProcessWindow> window(native_process_window_);
            bool closed = false;
            const bool invoked = QMetaObject::invokeMethod(
                native_process_window_,
                [this, window, &closed]() {
                    if (window)
                    {
                        closed = closeNativeProcessWindow();
                    }
                },
                Qt::BlockingQueuedConnection);
            return invoked && closed;
        }

        if (native_process_window_->isExecutionInProgress())
        {
            native_process_window_->requestCloseAfterExecutionCancel();
            setCancelled(true);
            native_process_window_->showProcessMessage(QStringLiteral("Cancelling"),
                                                       QStringLiteral("Waiting for the extension to stop..."),
                                                       TopLevelProcessBaseControl::ProcessMessageTone::Info);
            return true;
        }

        native_process_window_->requestClose();
        return true;
    }

    /// What closing the process window does in a host that has none.
    void setWindowlessCloseHandler(std::function<void()> handler)
    {
        windowless_close_ = std::move(handler);
    }

    bool closeWithoutWindow() noexcept
    {
        if (!windowless_close_)
        {
            return false;
        }
        if (!isGuiThread())
        {
            bool closed = false;
            const bool invoked = QMetaObject::invokeMethod(
                qApp, [this, &closed]() { closed = closeWithoutWindow(); }, Qt::BlockingQueuedConnection);
            return invoked && closed;
        }
        windowless_close_();
        return true;
    }

    bool commitCurrentViewResult(const py::array& array, py::dict metadata, const std::string& history_step_name,
                                 const std::string& history_step_description, const bool allow_resized_output,
                                 std::string* error)
    {
        if (!socket_)
        {
            if (error)
            {
                *error = "The extension host bridge has no active control socket";
            }
            return false;
        }

        const an::ui::extensions::SharedBufferDescriptor* source_descriptor_ptr = nullptr;
        if (current_view_source_buffer_.has_value() && current_view_source_buffer_->valid())
        {
            source_descriptor_ptr = &current_view_source_buffer_->descriptor();
        }
        else if (const auto source_it = images_.find("source"); source_it != images_.end())
        {
            source_descriptor_ptr = &source_it->second.descriptor;
        }

        if (!source_descriptor_ptr)
        {
            if (error)
            {
                *error = "The current view has no source image to commit against";
            }
            return false;
        }

        const auto& source_descriptor = *source_descriptor_ptr;
        if (py::len(metadata) == 0 && !source_descriptor.metadata_sidecar.empty())
        {
            metadata = loadMetadataSidecarDict(source_descriptor.metadata_sidecar);
        }

        const std::string request_id =
            current_request_id_.empty() ? an::ui::extensions::generateOpaqueToken() : current_request_id_;
        const std::string buffer_id = an::ui::extensions::generateOpaqueToken() + ".manual_commit";
        const auto session_paths = materializeExtensionSessionPaths(session_id_);
        const std::filesystem::path buffer_file_path = session_paths.root / (buffer_id + ".bin");

        ParsedGeneratedImageSpec generated_spec;
        if (allow_resized_output)
        {
            generated_spec = parseGeneratedImageSpec(array);
        }

        an::ui::extensions::SharedImageBufferSpec output_spec;
        output_spec.sessionId = session_id_;
        output_spec.bufferId = buffer_id;
        output_spec.symbolicName =
            source_descriptor.symbolic_name.empty() ? std::string("destination") : source_descriptor.symbolic_name;
        output_spec.width = allow_resized_output ? generated_spec.width : source_descriptor.width;
        output_spec.height = allow_resized_output ? generated_spec.height : source_descriptor.height;
        output_spec.channels = allow_resized_output ? generated_spec.channels : source_descriptor.channels;
        output_spec.pixelType = allow_resized_output ? generated_spec.pixel_type : source_descriptor.pixel_type;
        output_spec.pixelFormat = allow_resized_output ? generated_spec.pixel_format : source_descriptor.pixel_format;
        output_spec.colorSpace = allow_resized_output ? generated_spec.color_space : source_descriptor.color_space;
        output_spec.accessMode = an::ui::extensions::SharedBufferAccessMode::ReadWrite;
        output_spec.bufferRole = an::ui::extensions::SharedBufferRole::Destination;
        if (py::len(metadata) > 0)
        {
            auto sidecar_path = buffer_file_path;
            sidecar_path += ".metadata.json";
            output_spec.metadataSidecar = sidecar_path.lexically_normal().generic_string();
        }

        an::ui::extensions::SharedBufferManager manager(session_paths.root.parent_path());
        an::ui::extensions::SharedImageBuffer output_buffer;
        if (!allocateSharedBuffer(manager, output_spec, &output_buffer, error))
        {
            return false;
        }

        RuntimeImageBinding binding;
        binding.buffer = &output_buffer;
        binding.descriptor = output_buffer.descriptor();
        binding.writable = true;
        binding.metadata = metadata;
        copyArrayIntoPlanarBuffer(array, binding);
        if (!binding.descriptor.metadata_sidecar.empty())
        {
            writeMetadataSidecarDict(binding.descriptor.metadata_sidecar, bindingMetadataDict(binding));
        }

        an::ui::extensions::ImportedImage committed_image;
        if (!importSharedBuffer(output_buffer.descriptor(), &committed_image, error))
        {
            return false;
        }

        an::ui::extensions::ViewCommitMessage view_commit;
        view_commit.session_id = session_id_;
        view_commit.request_id = request_id;
        view_commit.view_id = current_view_id_.empty() ? session_id_ : current_view_id_;
        view_commit.history_step_name = history_step_name.empty() ? std::string("Extension Apply") : history_step_name;
        view_commit.history_step_description = history_step_description.empty()
                                                   ? std::string("Applied extension execution result")
                                                   : history_step_description;
        view_commit.allow_resized_output = allow_resized_output;
        view_commit.params = current_request_params_;
        view_commit.result_buffers.push_back(output_buffer.descriptor());

        const QByteArray frame =
            an::ui::extensions::frameProtocolMessage(an::ui::extensions::toProtocolMessage(view_commit), error);
        if (frame.isEmpty() && error && !error->empty())
        {
            return false;
        }

        if (!writeProtocolFrame(frame, error))
        {
            return false;
        }

        manual_view_commit_emitted_ = true;
        manual_committed_image_ = std::make_shared<an::consumer::ImageAsset>(std::move(committed_image.asset));
        return true;
    }

    bool progressIsCancelled()
    {
        if (!isGuiThread())
        {
            return cancelled_.load(std::memory_order_acquire);
        }

        if (native_process_window_)
        {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 16);
        }

        std::string ignored_error;
        (void)pumpExecutionControlMessages(5, &ignored_error);
        return cancelled_.load(std::memory_order_acquire);
    }

    void resetProgressFeedback() noexcept
    {
        const std::uint64_t generation = native_progress_generation_.fetch_add(1, std::memory_order_acq_rel) + 1;
        if (native_process_window_ && !isGuiThread())
        {
            QPointer<an::ui::extensions::ExtensionNativeProcessWindow> window(native_process_window_);
            QMetaObject::invokeMethod(native_process_window_, [this, window, generation]() {
                if (!window || native_progress_generation_.load(std::memory_order_acquire) != generation)
                {
                    return;
                }
                native_progress_text_.clear();
                native_progress_value_.reset();
                native_progress_value_explicit_ = false;
            });
            return;
        }

        native_progress_text_.clear();
        native_progress_value_.reset();
        native_progress_value_explicit_ = false;
    }

    void progressSetText(const std::string& text)
    {
        const std::uint64_t generation = native_progress_generation_.load(std::memory_order_acquire);
        if (native_process_window_ && !isGuiThread())
        {
            QPointer<an::ui::extensions::ExtensionNativeProcessWindow> window(native_process_window_);
            QMetaObject::invokeMethod(native_process_window_, [this, window, text, generation]() {
                if (!window || native_progress_generation_.load(std::memory_order_acquire) != generation)
                {
                    return;
                }
                native_progress_text_ = text;
                refreshNativeProgressFeedback();
            });
            return;
        }

        native_progress_text_ = text;
        refreshNativeProgressFeedback();
    }

    void progressSetValue(const double value)
    {
        const std::uint64_t generation = native_progress_generation_.load(std::memory_order_acquire);
        const std::optional<double> progress_value =
            std::isfinite(value) ? std::optional<double>((std::clamp)(value, 0.0, 100.0)) : std::nullopt;
        if (native_process_window_ && !isGuiThread())
        {
            QPointer<an::ui::extensions::ExtensionNativeProcessWindow> window(native_process_window_);
            QMetaObject::invokeMethod(native_process_window_, [this, window, progress_value, generation]() {
                if (!window || native_progress_generation_.load(std::memory_order_acquire) != generation)
                {
                    return;
                }
                native_progress_value_ = progress_value;
                native_progress_value_explicit_ = progress_value.has_value();
                refreshNativeProgressFeedback();
            });
            return;
        }

        if (std::isfinite(value))
        {
            native_progress_value_ = (std::clamp)(value, 0.0, 100.0);
            native_progress_value_explicit_ = true;
        }
        else
        {
            native_progress_value_.reset();
            native_progress_value_explicit_ = false;
        }

        refreshNativeProgressFeedback();
    }

    void progressSetSyntheticValue(const double value)
    {
        const std::uint64_t generation = native_progress_generation_.load(std::memory_order_acquire);
        const std::optional<double> progress_value =
            std::isfinite(value) ? std::optional<double>((std::clamp)(value, 0.0, 100.0)) : std::nullopt;
        if (native_process_window_ && !isGuiThread())
        {
            QPointer<an::ui::extensions::ExtensionNativeProcessWindow> window(native_process_window_);
            QMetaObject::invokeMethod(native_process_window_, [this, window, progress_value, generation]() {
                if (!window || native_progress_generation_.load(std::memory_order_acquire) != generation ||
                    native_progress_value_explicit_)
                {
                    return;
                }
                native_progress_value_ = progress_value;
                refreshNativeProgressFeedback();
            });
            return;
        }

        if (native_progress_value_explicit_)
        {
            return;
        }
        native_progress_value_ = progress_value;
        refreshNativeProgressFeedback();
    }

    void setCancelled(const bool cancelled) noexcept
    {
        cancelled_.store(cancelled, std::memory_order_release);
    }

    [[nodiscard]] bool currentRequestCancelled() const noexcept
    {
        return cancelled_.load(std::memory_order_acquire);
    }

    [[nodiscard]] const std::string& sessionId() const noexcept
    {
        return session_id_;
    }

    [[nodiscard]] const std::string& extensionId() const noexcept
    {
        return extension_id_;
    }

    [[nodiscard]] const std::string& currentRequestId() const noexcept
    {
        return current_request_id_;
    }

    [[nodiscard]] const std::string& launchMode() const noexcept
    {
        return launch_mode_;
    }

    [[nodiscard]] const std::string& currentViewName() const noexcept
    {
        return current_view_name_;
    }

    [[nodiscard]] const std::string& currentViewId() const noexcept
    {
        return current_view_id_;
    }

    void refreshNativeProgressFeedback()
    {
        if (!native_process_window_)
        {
            return;
        }
        if (!isGuiThread())
        {
            const std::uint64_t generation = native_progress_generation_.load(std::memory_order_acquire);
            QPointer<an::ui::extensions::ExtensionNativeProcessWindow> window(native_process_window_);
            QMetaObject::invokeMethod(native_process_window_, [this, window, generation]() {
                if (window && native_progress_generation_.load(std::memory_order_acquire) == generation)
                {
                    refreshNativeProgressFeedback();
                }
            });
            return;
        }

        QString message = QString::fromStdString(native_progress_text_).trimmed();
        if (native_progress_value_.has_value())
        {
            const double value = *native_progress_value_;
            const int precision = std::abs(value - std::round(value)) < 0.05 ? 0 : 1;
            const QString percent_text = QStringLiteral("%1%").arg(QString::number(value, 'f', precision));
            message = message.isEmpty() ? QStringLiteral("%1 complete").arg(percent_text)
                                        : QStringLiteral("%1 (%2 complete)").arg(message, percent_text);
        }

        if (message.isEmpty())
        {
            native_process_window_->clearProcessMessage();
        }
        else
        {
            native_process_window_->showProcessMessage(QStringLiteral("Working"), message,
                                                       TopLevelProcessBaseControl::ProcessMessageTone::Info,
                                                       native_progress_value_);
        }

        QApplication::processEvents(QEventLoop::AllEvents, 16);
    }

    [[nodiscard]] const std::string& currentFileName() const noexcept
    {
        return current_file_name_;
    }

    [[nodiscard]] ImageViewControl* nativeSourceView() const noexcept
    {
        return native_process_window_ ? native_process_window_->sourceView() : nullptr;
    }

    [[nodiscard]] QCborMap themeTokens() const
    {
        return theme_tokens_;
    }

    py::dict imageMetadata(const std::string& token)
    {
        RuntimeImageBinding& binding = requireImage(token);
        return ensureBindingMetadataDict(binding);
    }

    py::dict imageProperties(const std::string& token)
    {
        const RuntimeImageBinding& binding = requireImage(token);
        const auto& descriptor = binding.descriptor;

        py::dict properties;
        properties[py::str("width")] = py::int_(static_cast<int>(descriptor.width));
        properties[py::str("height")] = py::int_(static_cast<int>(descriptor.height));
        properties[py::str("channels")] = py::int_(static_cast<int>(descriptor.channels));
        properties[py::str("pixel_type")] =
            py::str(std::string(an::ui::extensions::sharedBufferPixelTypeName(descriptor.pixel_type)));
        properties[py::str("pixel_format")] =
            py::str(std::string(an::ui::extensions::sharedBufferPixelFormatName(descriptor.pixel_format)));
        properties[py::str("color_space")] =
            py::str(std::string(an::ui::extensions::sharedBufferColorSpaceName(descriptor.color_space)));
        properties[py::str("buffer_role")] =
            py::str(std::string(an::ui::extensions::sharedBufferRoleName(descriptor.buffer_role)));
        properties[py::str("access_mode")] =
            py::str(std::string(an::ui::extensions::sharedBufferAccessModeName(descriptor.access_mode)));
        if (descriptor.offset_x.has_value())
        {
            properties[py::str("offset_x")] = py::int_(*descriptor.offset_x);
        }
        if (descriptor.offset_y.has_value())
        {
            properties[py::str("offset_y")] = py::int_(*descriptor.offset_y);
        }
        if (descriptor.source_width.has_value())
        {
            properties[py::str("source_width")] = py::int_(*descriptor.source_width);
        }
        if (descriptor.source_height.has_value())
        {
            properties[py::str("source_height")] = py::int_(*descriptor.source_height);
        }
        return properties;
    }

    void imageSetMetadata(const std::string& token, const std::string& key, const py::object& value)
    {
        RuntimeImageBinding& binding = requireImage(token);
        if (!binding.writable)
        {
            throw std::runtime_error("Metadata can only be updated on writable image handles");
        }
        if (isReservedMetadataKey(key))
        {
            throw std::runtime_error("Metadata key is reserved and cannot be changed: " + key);
        }
        ensureBindingMetadataDict(binding)[py::str(key)] = value;
    }

    void flushWritableMetadata()
    {
        for (auto& [token, binding] : images_)
        {
            (void)token;
            if (!binding.writable || binding.descriptor.metadata_sidecar.empty())
            {
                continue;
            }

            writeMetadataSidecarDict(binding.descriptor.metadata_sidecar, bindingMetadataDict(binding));
        }
    }

    // Returns a zero-copy NumPy view over the shared buffer.  The returned
    // array is only valid for the duration of the current execute/preview
    // call — storing it beyond that scope produces a dangling view.
    //
    // LIFETIME CONTRACT: The no-op capsule destructor means the NumPy array
    // does NOT prevent the shared buffer from being reclaimed.  Extension
    // code must NOT store the array in module-level variables, closures, or
    // any scope that outlives the execute()/execute_preview() call.
    py::array imageToNumpy(const std::string& token)
    {
        RuntimeImageBinding& binding = requireImage(token);
        if (!binding.buffer || !binding.buffer->valid())
        {
            throw std::runtime_error("Image binding is not backed by a valid shared buffer");
        }

        const auto& descriptor = binding.descriptor;
        const auto item_size = bytesPerPixelComponent(descriptor.pixel_type);
        if (item_size == 0)
        {
            throw std::runtime_error("Unsupported shared-buffer pixel type");
        }

        const py::dtype dtype = numpyDtypeForPixelType(descriptor.pixel_type);
        std::vector<py::ssize_t> shape;
        std::vector<py::ssize_t> strides;
        if (descriptor.channels <= 1)
        {
            shape = {static_cast<py::ssize_t>(descriptor.height), static_cast<py::ssize_t>(descriptor.width)};
            strides = {static_cast<py::ssize_t>(descriptor.row_stride), static_cast<py::ssize_t>(item_size)};
        }
        else
        {
            shape = {static_cast<py::ssize_t>(descriptor.height), static_cast<py::ssize_t>(descriptor.width),
                     static_cast<py::ssize_t>(descriptor.channels)};
            strides = {static_cast<py::ssize_t>(descriptor.row_stride), static_cast<py::ssize_t>(item_size),
                       static_cast<py::ssize_t>(descriptor.plane_stride)};
        }

        return py::array(dtype, shape, strides, binding.buffer->bytes(), py::capsule(binding.buffer, [](void*) {}));
    }

    void imageFromNumpy(const std::string& token, const py::array& array)
    {
        RuntimeImageBinding& binding = requireImage(token);
        if (!binding.writable)
        {
            throw std::runtime_error("Destination image handle is read-only");
        }
        if (!binding.buffer || !binding.buffer->valid())
        {
            throw std::runtime_error("Image binding is not backed by a valid shared buffer");
        }

        copyArrayIntoPlanarBuffer(array, binding);
    }

    void imageApplyLinearTransform(const std::string& source_token, const std::string& destination_token,
                                   const double scale, const double offset)
    {
        RuntimeImageBinding& source = requireImage(source_token);
        RuntimeImageBinding& destination = requireImage(destination_token);
        if (!destination.writable)
        {
            throw std::runtime_error("Destination image handle is read-only");
        }
        if (!source.buffer || !source.buffer->valid() || !destination.buffer || !destination.buffer->valid())
        {
            throw std::runtime_error("Image binding is not backed by a valid shared buffer");
        }
        if (source.descriptor.width != destination.descriptor.width ||
            source.descriptor.height != destination.descriptor.height ||
            source.descriptor.channels != destination.descriptor.channels)
        {
            throw std::runtime_error(
                "apply_linear_transform() requires source and destination images to share dimensions and channels");
        }
        if (source.descriptor.pixel_type != destination.descriptor.pixel_type)
        {
            throw std::runtime_error(
                "apply_linear_transform() currently requires source and destination images to share a pixel type");
        }

        using PixelType = an::ui::extensions::SharedBufferPixelType;
        switch (destination.descriptor.pixel_type)
        {
        case PixelType::UInt8:
            applyLinearTransformTyped<std::uint8_t>(source, destination, scale, offset);
            break;
        case PixelType::UInt16:
            applyLinearTransformTyped<std::uint16_t>(source, destination, scale, offset);
            break;
        case PixelType::Float32:
            applyLinearTransformTyped<float>(source, destination, scale, offset);
            break;
        case PixelType::Float64:
            applyLinearTransformTyped<double>(source, destination, scale, offset);
            break;
        }

        destination.metadata = source.metadata;
    }

    py::dict makeViewRef() const
    {
        py::dict result;
        result["view_id"] = current_view_id_;
        result["view_name"] = current_view_name_;
        result["file_name"] = current_file_name_;
        return result;
    }

public:
    struct LocalHostViewState
    {
        ImageViewControl* control = nullptr;
        ImageViewControl* selection_mask_control = nullptr;
        std::string view_name;
        std::string file_name;
        std::string selector_tooltip;
        std::optional<int> width;
        std::optional<int> height;
        std::optional<int> channels;
    };

    void mergeKnownViewSnapshot(const std::vector<std::map<std::string, QVariant>>& views)
    {
        for (const auto& view : views)
        {
            const auto name_it = view.find("name");
            if (name_it == view.end())
            {
                continue;
            }

            const std::string view_name = name_it->second.toString().toStdString();
            if (view_name.empty())
            {
                continue;
            }

            LocalHostViewState& local_view = local_views_[view_name];
            local_view.view_name = view_name;

            if (const auto file_it = view.find("file_name"); file_it != view.end())
            {
                local_view.file_name = file_it->second.toString().toStdString();
            }

            if (const auto tooltip_it = view.find("selector_tooltip"); tooltip_it != view.end())
            {
                local_view.selector_tooltip = tooltip_it->second.toString().toStdString();
            }

            if (const auto channels_it = view.find("channels"); channels_it != view.end())
            {
                local_view.channels = channels_it->second.toInt();
            }
            if (const auto width_it = view.find("width"); width_it != view.end())
            {
                local_view.width = width_it->second.toInt();
            }
            if (const auto height_it = view.find("height"); height_it != view.end())
            {
                local_view.height = height_it->second.toInt();
            }
        }
    }

private:
    [[nodiscard]] std::string resolveLocalViewName(const std::string& requested_name,
                                                   const LocalHostViewState& local_view) const
    {
        if (!requested_name.empty())
        {
            return requested_name;
        }
        if (!local_view.view_name.empty())
        {
            return local_view.view_name;
        }
        if (!current_view_name_.empty())
        {
            return current_view_name_;
        }
        return std::string("Extension");
    }

    [[nodiscard]] std::string resolveLocalFileName(const std::string& requested_file_name,
                                                   const LocalHostViewState& local_view,
                                                   const std::string& resolved_view_name) const
    {
        if (!requested_file_name.empty())
        {
            return requested_file_name;
        }
        if (!local_view.file_name.empty())
        {
            return local_view.file_name;
        }
        if (!current_file_name_.empty())
        {
            return current_file_name_;
        }
        return resolved_view_name;
    }

    [[nodiscard]] std::string resolveUniqueLocalViewName(const std::string& requested_name,
                                                         ImageViewControl* existing_control) const
    {
        if (!app_context_)
        {
            return requested_name;
        }

        if (existing_control && requested_name == existing_control->getUniqueName())
        {
            return requested_name;
        }

        return app_context_->createViewControlUniqueName(requested_name);
    }

    [[nodiscard]] const an::ui::extensions::SharedBufferDescriptor*
    findSelectionMaskDescriptor(const std::vector<an::ui::extensions::SharedBufferDescriptor>& auxiliary_buffers) const
    {
        for (const auto& descriptor : auxiliary_buffers)
        {
            if (descriptor.buffer_role == an::ui::extensions::SharedBufferRole::Mask &&
                descriptor.symbolic_name == "selection")
            {
                return &descriptor;
            }
        }

        for (const auto& descriptor : auxiliary_buffers)
        {
            if (descriptor.buffer_role == an::ui::extensions::SharedBufferRole::Mask)
            {
                return &descriptor;
            }
        }

        return nullptr;
    }

    void destroyLocalMask(LocalHostViewState& local_view)
    {
        if (!local_view.selection_mask_control)
        {
            return;
        }

        if (local_view.control && local_view.control->getCurrentMask() == local_view.selection_mask_control)
        {
            local_view.control->removeCurrentMask();
        }

        delete local_view.selection_mask_control;
        local_view.selection_mask_control = nullptr;
    }

    void destroyLocalViewState(LocalHostViewState& local_view)
    {
        destroyLocalMask(local_view);
        if (!local_view.control)
        {
            return;
        }

        if (native_process_window_ && native_process_window_->sourceView() == local_view.control)
        {
            native_process_window_->setSourceView(nullptr);
        }

        if (app_context_)
        {
            app_context_->removeViewControl(local_view.control, false);
        }
        delete local_view.control;
        local_view.control = nullptr;
    }

    void destroyLocalView(const std::string& view_id)
    {
        const auto it = local_views_.find(view_id);
        if (it == local_views_.end())
        {
            return;
        }

        destroyLocalViewState(it->second);
        local_views_.erase(it);
    }

    void clearLocalViews()
    {
        for (auto& [view_id, local_view] : local_views_)
        {
            (void)view_id;
            destroyLocalViewState(local_view);
        }
        local_views_.clear();
    }

    bool synchronizeLocalSelectionMask(LocalHostViewState& local_view,
                                       const std::vector<an::ui::extensions::SharedBufferDescriptor>& auxiliary_buffers,
                                       std::string* error)
    {
        if (!app_context_ || !local_view.control)
        {
            return true;
        }

        const auto* selection_descriptor = findSelectionMaskDescriptor(auxiliary_buffers);
        if (!selection_descriptor)
        {
            destroyLocalMask(local_view);
            return true;
        }

        an::ui::extensions::ImportedImage selection_image;
        if (!importSharedBuffer(*selection_descriptor, &selection_image, error))
        {
            return false;
        }

        const std::string selection_name = selection_descriptor->symbolic_name.empty()
                                               ? std::string("selection")
                                               : selection_descriptor->symbolic_name;
        an::Result<an::ui::ImageDocument> selection_document = an::ui::ImageDocument::create(
            std::move(selection_image.asset), an::ui::DocumentNames{QString::fromStdString(selection_name), {}});
        if (!selection_document.hasValue())
        {
            if (error)
            {
                *error = selection_document.status().message();
            }
            return false;
        }

        if (!local_view.selection_mask_control)
        {
            local_view.selection_mask_control = app_context_->createNewImageViewControlFromImage(
                nullptr, std::move(*selection_document), nullptr, "Host synchronized mask", ViewControlMode::st_preview,
                nullptr, false, false);
            if (!local_view.selection_mask_control)
            {
                if (error)
                {
                    *error = "Failed to create a host-local selection-mask control";
                }
                return false;
            }
        }
        else
        {
            local_view.selection_mask_control->setDocument(std::move(*selection_document));
        }

        if (local_view.control->getCurrentMask() != local_view.selection_mask_control)
        {
            local_view.control->setCurrentMask(local_view.selection_mask_control);
        }

        return true;
    }

    bool synchronizeLocalView(const std::string& view_id, const std::string& view_name, const std::string& file_name,
                              const an::ui::extensions::SharedBufferDescriptor& source_buffer,
                              const std::vector<an::ui::extensions::SharedBufferDescriptor>& auxiliary_buffers,
                              const QCborMap& metadata, const QCborMap& display_settings, const bool make_active,
                              std::string* error)
    {
        if (!app_context_ || source_buffer.mapped_file_path.empty())
        {
            return true;
        }

        const bool created_state = local_views_.find(view_id) == local_views_.end();
        LocalHostViewState& local_view = local_views_[view_id];
        const std::string resolved_view_name = resolveLocalViewName(view_name, local_view);
        const std::string resolved_file_name = resolveLocalFileName(file_name, local_view, resolved_view_name);

        const bool needs_local_view_widget = native_process_window_ != nullptr || local_view.control != nullptr;
        if (!needs_local_view_widget)
        {
            local_view.view_name = resolved_view_name;
            local_view.file_name = resolved_file_name;
            local_view.channels = static_cast<int>(source_buffer.channels);
            return true;
        }

        an::ui::extensions::ImportedImage source_image;
        if (!importSharedBuffer(source_buffer, &source_image, error))
        {
            if (created_state)
            {
                local_views_.erase(view_id);
            }
            return false;
        }

        // The mirror's image carries the cards its buffer's sidecar states; the view description's own metadata
        // reaches Python as the current view's metadata.
        (void)metadata;
        const std::string local_unique_name = resolveUniqueLocalViewName(resolved_view_name, local_view.control);
        const std::optional<an::ImageDescriptor> source_described = source_image.asset.descriptor();
        an::Result<an::ui::ImageDocument> source_document = an::ui::ImageDocument::create(
            std::move(source_image.asset), an::ui::DocumentNames{QString::fromStdString(local_unique_name),
                                                                 QString::fromStdString(resolved_file_name)});
        if (!source_document.hasValue())
        {
            if (error)
            {
                *error = source_document.status().message();
            }
            if (created_state)
            {
                local_views_.erase(view_id);
            }
            return false;
        }

        if (!local_view.control)
        {
            // The native process window embeds a lightweight mirror of the
            // selected source image. Dedicated RT preview interaction now
            // lives on the separate preview pane below it.
            local_view.control = app_context_->createNewImageViewControlFromImage(
                nullptr, std::move(*source_document), nullptr, "Host synchronized view", ViewControlMode::st_preview,
                nullptr, false, false);
            if (!local_view.control)
            {
                if (error)
                {
                    *error = "Failed to create a host-local view control";
                }
                local_views_.erase(view_id);
                return false;
            }
            local_view.control->setMinimumWidth(240);
            local_view.control->setMinimumHeight(180);
        }
        else
        {
            if (!local_unique_name.empty() && local_unique_name != local_view.control->getUniqueName())
            {
                local_view.control->setUniqueName(local_unique_name);
            }
            // The mirror shows the new document at once: the window below caches the asset it now shows.
            local_view.control->setDocument(std::move(*source_document));
        }

        local_view.view_name = local_view.control->getUniqueName();
        local_view.file_name = resolved_file_name;
        local_view.channels =
            source_described.has_value() ? static_cast<int>(source_described->extent().channelCount()) : 0;
        if (!synchronizeLocalSelectionMask(local_view, auxiliary_buffers, error))
        {
            return false;
        }

        applyDisplaySettingsToViewControl(local_view.control, display_settings);
        if (make_active)
        {
            app_context_->setActiveImageViewControl(local_view.control);
        }

        if (native_process_window_ && make_active)
        {
            // The mirror's image is what previews and executes start from: the window follows its view.
            native_process_window_->setSourceView(local_view.control);
        }

        return true;
    }

    void renameLocalView(const std::string& view_id, const std::string& view_name, const std::string& file_name)
    {
        const auto it = local_views_.find(view_id);
        if (it == local_views_.end())
        {
            return;
        }

        LocalHostViewState& local_view = it->second;
        if (!file_name.empty())
        {
            local_view.file_name = file_name;
        }

        if (!local_view.control)
        {
            if (!view_name.empty())
            {
                local_view.view_name = view_name;
            }
            return;
        }

        const std::string requested_name = resolveLocalViewName(view_name, local_view);
        const std::string local_unique_name = resolveUniqueLocalViewName(requested_name, local_view.control);
        if (!local_unique_name.empty() && local_unique_name != local_view.control->getUniqueName())
        {
            local_view.control->setUniqueName(local_unique_name);
        }

        local_view.view_name = local_view.control->getUniqueName();
        if (an::ui::ImageDocument& document = local_view.control->document(); document.valid())
        {
            document.setNames(an::ui::DocumentNames{QString::fromStdString(local_view.view_name),
                                                    QString::fromStdString(local_view.file_name)});
        }
    }

    void applyViewIdentity(const std::string& view_id, const std::string& view_name, const std::string& file_name)
    {
        if (!view_id.empty())
        {
            current_view_id_ = view_id;
        }
        if (!view_name.empty())
        {
            current_view_name_ = view_name;
        }
        current_file_name_ = file_name.empty() ? current_view_name_ : file_name;
    }

    void applyViewState(const std::string& view_id, const std::string& view_name, const std::string& file_name,
                        const QCborMap& metadata, const QCborMap& display_settings)
    {
        applyViewIdentity(view_id, view_name, file_name);
        current_view_metadata_ = metadata;
        current_view_display_settings_ = display_settings;
    }

    void clearViewState()
    {
        current_view_id_ = session_id_;
        current_view_name_ = "Extension";
        current_file_name_ = current_view_name_;
        current_view_metadata_.clear();
        current_view_display_settings_.clear();
        clearCurrentViewBuffers();
    }

    void applyThemeState(const std::string& theme_name, const QCborMap& tokens)
    {
        theme_name_ = theme_name;
        theme_tokens_ = tokens;

        // Keep the host-local Qt token state aligned with the
        // application theme snapshot so synchronized ImageViewControl clones
        // repaint against the correct light/dark token set.
        (void)an::ui::extensions::applyHostThemeSnapshot(theme_name_, theme_tokens_);
        if (auto* proxy_style = an::ui::installedAnProxyStyle())
        {
            proxy_style->refreshThemeTokens();
        }

        // Reload icons with updated theme colors.
        if (auto* ctx = dynamic_cast<an::ui::extensions::ExtensionAppContext*>(an::ui::appContext()))
        {
            ctx->reloadIcons(an::ui::isDarkTheme());
        }

        for (auto& [view_id, local_view] : local_views_)
        {
            (void)view_id;
            if (local_view.control)
            {
                local_view.control->refreshTheme();
            }
            if (local_view.selection_mask_control)
            {
                local_view.selection_mask_control->refreshTheme();
            }
        }

        if (native_process_window_)
        {
            native_process_window_->refreshTheme();
        }
    }

    bool materializeCurrentViewBuffers(const an::ui::extensions::SharedBufferDescriptor& source_buffer,
                                       const std::vector<an::ui::extensions::SharedBufferDescriptor>& auxiliary_buffers,
                                       std::string* error)
    {
        clearCurrentViewBuffers();

        auto open_descriptor = [&](const an::ui::extensions::SharedBufferDescriptor& descriptor,
                                   an::ui::extensions::SharedImageBuffer* out_buffer) -> bool {
            if (descriptor.mapped_file_path.empty())
            {
                if (error)
                {
                    *error = "Shared buffer descriptor did not define a mapped_file_path";
                }
                return false;
            }

            const std::filesystem::path file_path =
                std::filesystem::path(descriptor.mapped_file_path).lexically_normal();
            const std::filesystem::path session_root = file_path.parent_path();
            const std::filesystem::path root_path = session_root.parent_path();
            if (session_root.empty() || root_path.empty())
            {
                if (error)
                {
                    *error = "Shared buffer descriptor did not expose a usable managed root";
                }
                return false;
            }

            an::ui::extensions::SharedBufferManager manager(root_path);
            return openSharedBuffer(manager, descriptor, out_buffer, error);
        };

        if (source_buffer.mapped_file_path.empty())
        {
            return auxiliary_buffers.empty();
        }

        an::ui::extensions::SharedImageBuffer opened_source_buffer;
        if (!open_descriptor(source_buffer, &opened_source_buffer))
        {
            clearCurrentViewBuffers();
            return false;
        }

        current_view_source_buffer_ = std::move(opened_source_buffer);
        current_view_source_token_ = std::string("view:source:") + source_buffer.buffer_id;

        RuntimeImageBinding source_binding;
        source_binding.buffer = &*current_view_source_buffer_;
        source_binding.descriptor = source_buffer;
        source_binding.writable = false;
        bindCurrentViewImage(current_view_source_token_, std::move(source_binding));

        current_view_auxiliary_buffers_.reserve(auxiliary_buffers.size());
        for (const auto& descriptor : auxiliary_buffers)
        {
            an::ui::extensions::SharedImageBuffer auxiliary_buffer;
            if (!open_descriptor(descriptor, &auxiliary_buffer))
            {
                clearCurrentViewBuffers();
                return false;
            }

            current_view_auxiliary_buffers_.push_back(std::move(auxiliary_buffer));
            const std::string symbolic_name =
                descriptor.symbolic_name.empty() ? descriptor.buffer_id : descriptor.symbolic_name;
            const std::string token = std::string("view:aux:") + descriptor.buffer_id;

            RuntimeImageBinding binding;
            binding.buffer = &current_view_auxiliary_buffers_.back();
            binding.descriptor = descriptor;
            binding.writable = false;
            bindCurrentViewImage(token, std::move(binding));

            if (!current_view_auxiliary_bindings_
                     .emplace(symbolic_name, CurrentViewAuxiliaryBinding{token, descriptor.buffer_role})
                     .second)
            {
                if (error)
                {
                    *error = "Duplicate current-view auxiliary symbolic name: " + symbolic_name;
                }
                clearCurrentViewBuffers();
                return false;
            }
        }

        return true;
    }

    void clearCurrentViewBuffers()
    {
        current_view_source_token_.clear();
        current_view_auxiliary_bindings_.clear();
        std::unique_ptr<py::gil_scoped_acquire> gil;
        if (Py_IsInitialized() != 0)
        {
            gil = std::make_unique<py::gil_scoped_acquire>();
        }
        current_view_images_.clear();
        current_view_auxiliary_buffers_.clear();
        current_view_source_buffer_.reset();
    }

    RuntimeImageBinding& requireImage(const std::string& token)
    {
        const auto it = images_.find(token);
        if (it != images_.end())
        {
            return it->second;
        }

        const auto current_view_it = current_view_images_.find(token);
        if (current_view_it != current_view_images_.end())
        {
            return current_view_it->second;
        }

        if (it == images_.end())
        {
            throw std::runtime_error("Unknown image handle token: " + token);
        }

        return it->second;
    }

    static std::size_t bytesPerPixelComponent(const an::ui::extensions::SharedBufferPixelType pixel_type) noexcept
    {
        using PixelType = an::ui::extensions::SharedBufferPixelType;
        switch (pixel_type)
        {
        case PixelType::UInt8:
            return sizeof(std::uint8_t);
        case PixelType::UInt16:
            return sizeof(std::uint16_t);
        case PixelType::Float32:
            return sizeof(float);
        case PixelType::Float64:
            return sizeof(double);
        }
        return 0u;
    }

    static py::dtype numpyDtypeForPixelType(const an::ui::extensions::SharedBufferPixelType pixel_type)
    {
        using PixelType = an::ui::extensions::SharedBufferPixelType;
        switch (pixel_type)
        {
        case PixelType::UInt8:
            return py::dtype::of<std::uint8_t>();
        case PixelType::UInt16:
            return py::dtype::of<std::uint16_t>();
        case PixelType::Float32:
            return py::dtype::of<float>();
        case PixelType::Float64:
            return py::dtype::of<double>();
        }

        throw std::runtime_error("Unsupported shared-buffer pixel type");
    }

    struct ParsedGeneratedImageSpec
    {
        int width = 0;
        int height = 0;
        int channels = 0;
        an::ui::extensions::SharedBufferPixelType pixel_type = an::ui::extensions::SharedBufferPixelType::Float32;
        an::ui::extensions::SharedBufferPixelFormat pixel_format = an::ui::extensions::SharedBufferPixelFormat::Mono;
        an::ui::extensions::SharedBufferColorSpace color_space = an::ui::extensions::SharedBufferColorSpace::Linear;
    };

    static an::ui::extensions::SharedBufferPixelType sharedBufferPixelTypeFromArray(const py::array& array)
    {
        const py::dtype dtype = py::dtype::from_args(array.attr("dtype"));
        if (dtype.is(py::dtype::of<std::uint8_t>()))
        {
            return an::ui::extensions::SharedBufferPixelType::UInt8;
        }
        if (dtype.is(py::dtype::of<std::uint16_t>()))
        {
            return an::ui::extensions::SharedBufferPixelType::UInt16;
        }
        if (dtype.is(py::dtype::of<float>()))
        {
            return an::ui::extensions::SharedBufferPixelType::Float32;
        }
        if (dtype.is(py::dtype::of<double>()))
        {
            return an::ui::extensions::SharedBufferPixelType::Float64;
        }

        throw std::runtime_error("ui.open_image() requires uint8/uint16/float32/float64 NumPy arrays");
    }

    static ParsedGeneratedImageSpec parseGeneratedImageSpec(const py::array& array)
    {
        const py::buffer_info info = array.request();
        ParsedGeneratedImageSpec spec;
        spec.pixel_type = sharedBufferPixelTypeFromArray(array);

        if (info.ndim == 2)
        {
            spec.height = static_cast<int>(info.shape[0]);
            spec.width = static_cast<int>(info.shape[1]);
            spec.channels = 1;
            spec.pixel_format = an::ui::extensions::SharedBufferPixelFormat::Mono;
            spec.color_space = an::ui::extensions::SharedBufferColorSpace::Grayscale;
            return spec;
        }

        if (info.ndim != 3)
        {
            throw std::runtime_error("ui.open_image() requires a 2D mono or 3D color NumPy array");
        }

        const std::size_t first = static_cast<std::size_t>(info.shape[0]);
        const std::size_t second = static_cast<std::size_t>(info.shape[1]);
        const std::size_t third = static_cast<std::size_t>(info.shape[2]);
        if (third == 1u || third == 3u)
        {
            spec.height = static_cast<int>(first);
            spec.width = static_cast<int>(second);
            spec.channels = static_cast<int>(third);
        }
        else if (first == 1u || first == 3u)
        {
            spec.channels = static_cast<int>(first);
            spec.height = static_cast<int>(second);
            spec.width = static_cast<int>(third);
        }
        else
        {
            throw std::runtime_error("ui.open_image() requires mono or RGB arrays in (H, W, C) or (C, H, W) layout");
        }

        spec.pixel_format = spec.channels == 1 ? an::ui::extensions::SharedBufferPixelFormat::Mono
                                               : an::ui::extensions::SharedBufferPixelFormat::RGB;
        spec.color_space = spec.channels == 1 ? an::ui::extensions::SharedBufferColorSpace::Grayscale
                                              : an::ui::extensions::SharedBufferColorSpace::Linear;
        return spec;
    }

    template <typename T> static T clampLinearTransformValue(const double value)
    {
        if constexpr (std::numeric_limits<T>::is_integer)
        {
            const double min_value = static_cast<double>(std::numeric_limits<T>::lowest());
            const double max_value = static_cast<double>(std::numeric_limits<T>::max());
            return static_cast<T>(std::clamp(value, min_value, max_value));
        }

        return static_cast<T>(value);
    }

    // TODO: Parallelize the inner loops with tbb::parallel_for or
    // std::execution::par_unseq for large images (e.g. 4K×4K×3 float32).
    template <typename T>
    static void applyLinearTransformTyped(const RuntimeImageBinding& source, RuntimeImageBinding& destination,
                                          const double scale, const double offset)
    {
        const auto* source_data = reinterpret_cast<const T*>(source.buffer->bytes());
        auto* destination_data = reinterpret_cast<T*>(destination.buffer->bytes());
        const auto width = static_cast<std::size_t>(source.descriptor.width);
        const auto height = static_cast<std::size_t>(source.descriptor.height);
        const auto channels = static_cast<std::size_t>(source.descriptor.channels);
        const auto source_row_stride = static_cast<std::size_t>(source.descriptor.row_stride / sizeof(T));
        const auto source_plane_stride = static_cast<std::size_t>(source.descriptor.plane_stride / sizeof(T));
        const auto destination_row_stride = static_cast<std::size_t>(destination.descriptor.row_stride / sizeof(T));
        const auto destination_plane_stride = static_cast<std::size_t>(destination.descriptor.plane_stride / sizeof(T));

        for (std::size_t channel = 0; channel < channels; ++channel)
        {
            const T* source_plane = source_data + channel * source_plane_stride;
            T* destination_plane = destination_data + channel * destination_plane_stride;
            for (std::size_t y = 0; y < height; ++y)
            {
                for (std::size_t x = 0; x < width; ++x)
                {
                    const double transformed =
                        static_cast<double>(source_plane[y * source_row_stride + x]) * scale + offset;
                    destination_plane[y * destination_row_stride + x] = clampLinearTransformValue<T>(transformed);
                }
            }
        }
    }

    // TODO: For contiguous row-major HWC input, consider a batch memcpy-per-row
    // path instead of the element-by-element copy — only relevant for large images.
    template <typename T>
    static void copyTypedArrayIntoPlanarBuffer(const py::buffer_info& info, RuntimeImageBinding& binding)
    {
        auto* destination = reinterpret_cast<T*>(binding.buffer->bytes());
        const auto& descriptor = binding.descriptor;
        const std::size_t width = static_cast<std::size_t>(descriptor.width);
        const std::size_t height = static_cast<std::size_t>(descriptor.height);
        const std::size_t channels = static_cast<std::size_t>(descriptor.channels);
        const auto row_stride = static_cast<std::size_t>(descriptor.row_stride / sizeof(T));
        const auto plane_stride = static_cast<std::size_t>(descriptor.plane_stride / sizeof(T));

        if (info.ndim == 2)
        {
            if (static_cast<std::size_t>(info.shape[0]) != height || static_cast<std::size_t>(info.shape[1]) != width)
            {
                throw std::runtime_error("NumPy array shape does not match the destination image");
            }

            for (std::size_t c = 0; c < channels; ++c)
            {
                T* destination_plane = destination + c * plane_stride;
                for (std::size_t y = 0; y < height; ++y)
                {
                    for (std::size_t x = 0; x < width; ++x)
                    {
                        const auto* source = reinterpret_cast<const T*>(static_cast<const char*>(info.ptr) +
                                                                        static_cast<py::ssize_t>(y) * info.strides[0] +
                                                                        static_cast<py::ssize_t>(x) * info.strides[1]);
                        destination_plane[y * row_stride + x] = *source;
                    }
                }
            }
            return;
        }

        if (info.ndim != 3)
        {
            throw std::runtime_error("ImageHandle.from_numpy() requires a 2D or 3D NumPy array");
        }

        const bool hwc = static_cast<std::size_t>(info.shape[0]) == height &&
                         static_cast<std::size_t>(info.shape[1]) == width &&
                         static_cast<std::size_t>(info.shape[2]) == channels;
        const bool chw = static_cast<std::size_t>(info.shape[0]) == channels &&
                         static_cast<std::size_t>(info.shape[1]) == height &&
                         static_cast<std::size_t>(info.shape[2]) == width;
        if (!hwc && !chw)
        {
            throw std::runtime_error("3D NumPy arrays must use (H, W, C) or (C, H, W) shape");
        }

        for (std::size_t c = 0; c < channels; ++c)
        {
            T* destination_plane = destination + c * plane_stride;
            for (std::size_t y = 0; y < height; ++y)
            {
                for (std::size_t x = 0; x < width; ++x)
                {
                    const char* element_ptr = static_cast<const char*>(info.ptr);
                    if (hwc)
                    {
                        element_ptr += static_cast<py::ssize_t>(y) * info.strides[0] +
                                       static_cast<py::ssize_t>(x) * info.strides[1] +
                                       static_cast<py::ssize_t>(c) * info.strides[2];
                    }
                    else
                    {
                        element_ptr += static_cast<py::ssize_t>(c) * info.strides[0] +
                                       static_cast<py::ssize_t>(y) * info.strides[1] +
                                       static_cast<py::ssize_t>(x) * info.strides[2];
                    }

                    destination_plane[y * row_stride + x] = *reinterpret_cast<const T*>(element_ptr);
                }
            }
        }
    }

    static void copyArrayIntoPlanarBuffer(const py::array& array, RuntimeImageBinding& binding)
    {
        const py::buffer_info info = array.request();
        using PixelType = an::ui::extensions::SharedBufferPixelType;
        switch (binding.descriptor.pixel_type)
        {
        case PixelType::UInt8: {
            py::array_t<std::uint8_t, py::array::forcecast> typed(array);
            copyTypedArrayIntoPlanarBuffer<std::uint8_t>(typed.request(), binding);
            return;
        }
        case PixelType::UInt16: {
            py::array_t<std::uint16_t, py::array::forcecast> typed(array);
            copyTypedArrayIntoPlanarBuffer<std::uint16_t>(typed.request(), binding);
            return;
        }
        case PixelType::Float32: {
            py::array_t<float, py::array::forcecast> typed(array);
            copyTypedArrayIntoPlanarBuffer<float>(typed.request(), binding);
            return;
        }
        case PixelType::Float64: {
            py::array_t<double, py::array::forcecast> typed(array);
            copyTypedArrayIntoPlanarBuffer<double>(typed.request(), binding);
            return;
        }
        }

        throw std::runtime_error("Unsupported shared-buffer pixel type");
    }

    bool sendProtocolRequestAwaitResponse(const an::ui::extensions::ExtensionProtocolMessage& request,
                                          an::ui::extensions::ExtensionProtocolMessage* out_response,
                                          std::string* error)
    {
        if (!isGuiThread())
        {
            QCoreApplication* app = QCoreApplication::instance();
            if (!app)
            {
                if (error)
                {
                    *error = "Cannot marshal protocol request without a Qt application";
                }
                return false;
            }

            bool ok = false;
            bool invoked = false;
            std::string local_error;
            an::ui::extensions::ExtensionProtocolMessage response;
            std::optional<py::gil_scoped_release> gil_release;
            if (pythonGilHeld())
            {
                gil_release.emplace();
            }
            invoked = QMetaObject::invokeMethod(
                app,
                [&]() {
                    ok = sendProtocolRequestAwaitResponse(request, out_response ? &response : nullptr, &local_error);
                },
                Qt::BlockingQueuedConnection);
            if (!invoked)
            {
                if (error)
                {
                    *error = "Failed to marshal protocol request to the UI thread";
                }
                return false;
            }
            if (!ok)
            {
                if (error)
                {
                    *error = std::move(local_error);
                }
                return false;
            }
            if (out_response)
            {
                *out_response = std::move(response);
            }
            return true;
        }

        if (!socket_)
        {
            if (error)
            {
                *error = "The extension host bridge has no active control socket";
            }
            return false;
        }

        std::optional<py::gil_scoped_release> gil_release;
        if (pythonGilHeld())
        {
            gil_release.emplace();
        }

        const QByteArray frame = an::ui::extensions::frameProtocolMessage(request, error);
        if (frame.isEmpty() && error && !error->empty())
        {
            return false;
        }
        if (socket_->write(frame) != frame.size() || !socket_->waitForBytesWritten(5000))
        {
            if (error)
            {
                *error = "Failed to write a protocol request to the UI host";
            }
            return false;
        }

        // A message that is neither this response nor something answered here - an execute the application sent
        // while the extension waited, as one sent as soon as a launch returns arrives while the launch hook logs -
        // belongs to the session loop, which reads the same buffer: it is put back in front of that buffer, in the
        // order it came, on every way out of the wait.
        QByteArray kept_for_session;
        struct KeptForSessionRestore
        {
            QByteArray* buffer = nullptr;
            const QByteArray* frames = nullptr;
            ~KeptForSessionRestore()
            {
                if (buffer && frames && !frames->isEmpty())
                {
                    buffer->prepend(*frames);
                }
            }
        } restore_kept{buffered_input_, &kept_for_session};

        while (true)
        {
            if (buffered_input_)
            {
                buffered_input_->append(socket_->readAll());
            }
            else
            {
                socket_->readAll();
            }

            if (buffered_input_ && buffered_input_->isEmpty())
            {
                if (!socket_->waitForReadyRead(5000))
                {
                    if (error)
                    {
                        *error = "Timed out waiting for the UI host protocol response";
                    }
                    return false;
                }
                buffered_input_->append(socket_->readAll());
            }

            an::ui::extensions::ExtensionProtocolMessage response;
            if (!buffered_input_)
            {
                if (error)
                {
                    *error = "Missing buffered input state for protocol response";
                }
                return false;
            }

            const auto decode_status =
                an::ui::extensions::tryConsumeFramedProtocolMessage(buffered_input_, &response, error);
            if (decode_status == an::ui::extensions::ProtocolFrameDecodeStatus::NeedMoreData)
            {
                continue;
            }
            if (decode_status == an::ui::extensions::ProtocolFrameDecodeStatus::InvalidFrame)
            {
                return false;
            }

            if (response.type == "runtime.ping")
            {
                an::ui::extensions::RuntimePingMessage ping;
                if (!an::ui::extensions::runtimePingFromProtocolMessage(response, &ping, error))
                {
                    return false;
                }

                an::ui::extensions::RuntimePongMessage pong;
                pong.session_id = ping.session_id;
                pong.request_id = ping.request_id;
                pong.sequence = ping.sequence;
                const QByteArray pong_frame =
                    an::ui::extensions::frameProtocolMessage(an::ui::extensions::toProtocolMessage(pong), error);
                if (pong_frame.isEmpty() && error && !error->empty())
                {
                    return false;
                }
                if (socket_->write(pong_frame) != pong_frame.size() || !socket_->waitForBytesWritten(5000))
                {
                    if (error)
                    {
                        *error = "Failed to reply to a runtime.ping while awaiting protocol response";
                    }
                    return false;
                }
                continue;
            }

            if (response.type == "execution.cancel")
            {
                an::ui::extensions::ExecutionCancelMessage cancel;
                if (!an::ui::extensions::executionCancelFromProtocolMessage(response, &cancel, error))
                {
                    return false;
                }

                if (!ignore_cancel_ && !current_request_id_.empty() && cancel.target_request_id == current_request_id_)
                {
                    cancelled_.store(true, std::memory_order_release);
                }
                continue;
            }

            bool handled_host_notification = false;
            if (!handleInboundHostNotification(response, &handled_host_notification, error))
            {
                return false;
            }
            if (handled_host_notification)
            {
                continue;
            }

            if (response.request_id != request.request_id)
            {
                if (!isResponseEnvelope(response))
                {
                    std::string frame_error;
                    const QByteArray kept = an::ui::extensions::frameProtocolMessage(response, &frame_error);
                    if (kept.isEmpty())
                    {
                        if (error)
                        {
                            *error = frame_error.empty() ? std::string("Failed to keep a message for the session loop")
                                                         : frame_error;
                        }
                        return false;
                    }
                    kept_for_session.append(kept);
                }
                continue;
            }

            if (response.type == request.type + ".error")
            {
                an::ui::extensions::ExtensionProtocolError protocol_error;
                std::string local_error;
                if (an::ui::extensions::protocolErrorFromCborValue(response.fields.value(QStringLiteral("error")),
                                                                   &protocol_error, &local_error))
                {
                    if (error)
                    {
                        *error = protocol_error.message +
                                 (protocol_error.details.empty() ? std::string{} : ": " + protocol_error.details);
                    }
                }
                else if (error)
                {
                    *error = local_error.empty() ? "Protocol request failed" : local_error;
                }
                return false;
            }

            if (out_response)
            {
                *out_response = std::move(response);
            }
            return true;
        }
    }

    bool writeProtocolFrame(QByteArray frame, std::string* error)
    {
        if (!isGuiThread())
        {
            QCoreApplication* app = QCoreApplication::instance();
            if (!app)
            {
                if (error)
                {
                    *error = "Cannot marshal protocol write without a Qt application";
                }
                return false;
            }

            bool ok = false;
            bool invoked = false;
            std::string local_error;
            std::optional<py::gil_scoped_release> gil_release;
            if (pythonGilHeld())
            {
                gil_release.emplace();
            }
            invoked = QMetaObject::invokeMethod(
                app, [&]() { ok = writeProtocolFrame(frame, &local_error); }, Qt::BlockingQueuedConnection);
            if (!invoked)
            {
                if (error)
                {
                    *error = "Failed to marshal protocol write to the UI thread";
                }
                return false;
            }
            if (!ok && error)
            {
                *error = std::move(local_error);
            }
            return ok;
        }

        if (!socket_)
        {
            if (error)
            {
                *error = "The extension host bridge has no active control socket";
            }
            return false;
        }

        std::optional<py::gil_scoped_release> gil_release;
        if (pythonGilHeld())
        {
            gil_release.emplace();
        }

        if (socket_->write(frame) != frame.size() || !socket_->waitForBytesWritten(5000))
        {
            if (error)
            {
                *error = "Failed to write a protocol message to the UI host";
            }
            return false;
        }
        return true;
    }

    bool pumpExecutionControlMessages(const int wait_for_ready_read_ms, std::string* error)
    {
        if (!socket_ || !buffered_input_)
        {
            return true;
        }

        if (buffered_input_->isEmpty() && wait_for_ready_read_ms > 0)
        {
            (void)socket_->waitForReadyRead(wait_for_ready_read_ms);
        }
        buffered_input_->append(socket_->readAll());

        while (true)
        {
            an::ui::extensions::ExtensionProtocolMessage inbound;
            std::string local_error;
            const auto status =
                an::ui::extensions::tryConsumeFramedProtocolMessage(buffered_input_, &inbound, &local_error);
            if (status == an::ui::extensions::ProtocolFrameDecodeStatus::NeedMoreData)
            {
                return true;
            }
            if (status == an::ui::extensions::ProtocolFrameDecodeStatus::InvalidFrame)
            {
                if (error)
                {
                    *error = std::move(local_error);
                }
                return false;
            }

            if (inbound.type == "runtime.ping")
            {
                an::ui::extensions::RuntimePingMessage ping;
                if (!an::ui::extensions::runtimePingFromProtocolMessage(inbound, &ping, &local_error))
                {
                    if (error)
                    {
                        *error = std::move(local_error);
                    }
                    return false;
                }

                an::ui::extensions::RuntimePongMessage pong;
                pong.session_id = ping.session_id;
                pong.request_id = ping.request_id;
                pong.sequence = ping.sequence;
                const QByteArray pong_frame =
                    an::ui::extensions::frameProtocolMessage(an::ui::extensions::toProtocolMessage(pong), &local_error);
                if ((pong_frame.isEmpty() && !local_error.empty()) || socket_->write(pong_frame) != pong_frame.size() ||
                    !socket_->waitForBytesWritten(5000))
                {
                    if (error)
                    {
                        *error = local_error.empty()
                                     ? std::string("Failed to reply to a runtime.ping during Python execution")
                                     : std::move(local_error);
                    }
                    return false;
                }
                continue;
            }

            if (inbound.type == "execution.cancel")
            {
                an::ui::extensions::ExecutionCancelMessage cancel;
                if (!an::ui::extensions::executionCancelFromProtocolMessage(inbound, &cancel, &local_error))
                {
                    if (error)
                    {
                        *error = std::move(local_error);
                    }
                    return false;
                }

                if (!ignore_cancel_ && !current_request_id_.empty() && cancel.target_request_id == current_request_id_)
                {
                    cancelled_.store(true, std::memory_order_release);
                }
                continue;
            }

            bool handled_host_notification = false;
            if (!handleInboundHostNotification(inbound, &handled_host_notification, &local_error))
            {
                if (error)
                {
                    *error = std::move(local_error);
                }
                return false;
            }
            if (handled_host_notification)
            {
                continue;
            }

            // Ignore any unrelated control traffic here. Cooperative cancel only
            // requires ping/cancel servicing while Python execution is active.
        }
    }

    QLocalSocket* socket_ = nullptr;
    QByteArray* buffered_input_ = nullptr;
    std::string session_id_;
    std::string extension_id_;
    std::string current_view_id_;
    std::string current_file_name_ = "Extension";
    std::string current_request_id_;
    QCborMap current_request_params_;
    std::string current_view_name_ = "Extension";
    std::string launch_mode_ = "single_image";
    std::string theme_name_;
    std::atomic_bool cancelled_{false};
    bool ignore_cancel_ = false;
    std::string native_progress_text_;
    std::optional<double> native_progress_value_;
    bool native_progress_value_explicit_ = false;
    bool standalone_ = false;
    std::atomic_uint64_t native_progress_generation_{0};
    QCborMap current_view_metadata_;
    QCborMap current_view_display_settings_;
    QCborMap theme_tokens_;
    QCborMap inspect_settings_snapshot_;
    an::ui::extensions::ExtensionAppContext* app_context_ = nullptr;
    an::ui::extensions::ExtensionNativeProcessWindow* native_process_window_ = nullptr;
    bool native_process_window_hidden_by_extension_ = false;
    std::function<void()> windowless_close_;
    std::unordered_map<std::string, RuntimeImageBinding> images_;
    std::unordered_map<std::string, an::ui::extensions::SharedImageBuffer> snapshot_image_buffers_;
    std::unordered_map<std::string, RuntimeImageBinding> current_view_images_;
    std::unordered_map<std::string, LocalHostViewState> local_views_;
    std::optional<an::ui::extensions::SharedImageBuffer> current_view_source_buffer_;
    std::vector<an::ui::extensions::SharedImageBuffer> current_view_auxiliary_buffers_;
    std::string current_view_source_token_;
    std::unordered_map<std::string, CurrentViewAuxiliaryBinding> current_view_auxiliary_bindings_;
    bool manual_view_commit_emitted_ = false;
    OwnedAsset manual_committed_image_;
    std::function<void(const QCborMap&)> settings_action_update_callback_;
};

// Python execution can run on a Qt worker thread while the host UI thread keeps
// servicing the process window.  Keep the active bridge binding thread-local so
// Python callbacks made by worker-side execution and UI-thread PySide timers do
// not overwrite each other.
PythonHostBridge*& currentPythonHostBridge()
{
    thread_local PythonHostBridge* bridge = nullptr;
    return bridge;
}

an::ui::extensions::ExtensionAppContext::DialogCallbacks makeExtensionDialogCallbacks()
{
    auto notify = [](const char* type, QWidget*, const QString& title, const QString& text) {
        if (!currentPythonHostBridge())
        {
            return;
        }

        std::string error;
        (void)currentPythonHostBridge()->uiNotify(type, title.toStdString(), text.toStdString(), &error);
    };

    an::ui::extensions::ExtensionAppContext::DialogCallbacks callbacks;
    callbacks.message_box_yes_no = [](QWidget*, const QString& title, const QString& text, const bool warn, int* ret) {
        bool confirmed = false;
        if (currentPythonHostBridge())
        {
            std::string error;
            if (currentPythonHostBridge()->uiConfirm(title.toStdString(), text.toStdString(), warn, &confirmed, &error))
            {
                if (ret)
                {
                    *ret = confirmed ? QMessageBox::Yes : QMessageBox::No;
                }
                return;
            }
        }

        if (ret)
        {
            *ret = QMessageBox::No;
        }
    };
    callbacks.notify_error = [notify](QWidget* parent, const QString& title, const QString& text) {
        notify("ui.show_error", parent, title, text);
    };
    callbacks.notify_error_async = callbacks.notify_error;
    callbacks.notify_warn = [notify](QWidget* parent, const QString& title, const QString& text) {
        notify("ui.show_warning", parent, title, text);
    };
    callbacks.notify_warn_async = callbacks.notify_warn;
    callbacks.notify_info = [notify](QWidget* parent, const QString& title, const QString& text) {
        notify("ui.show_info", parent, title, text);
    };
    callbacks.notify_info_async = callbacks.notify_info;
    return callbacks;
}

struct ScopedPythonHostBridgeBinding
{
    explicit ScopedPythonHostBridgeBinding(PythonHostBridge* bridge) : previous_(currentPythonHostBridge())
    {
        currentPythonHostBridge() = bridge;
    }

    ~ScopedPythonHostBridgeBinding()
    {
        currentPythonHostBridge() = previous_;
    }

    PythonHostBridge* previous_ = nullptr;
};

PYBIND11_EMBEDDED_MODULE(_afternight_host, m)
{
    m.def("settings_get", [](const std::string& key, const py::object& default_value) {
        if (!currentPythonHostBridge())
        {
            return default_value;
        }
        return currentPythonHostBridge()->settingsGet(key, default_value);
    });
    m.def("settings_set", [](const std::string& key, const py::object& value) {
        if (currentPythonHostBridge())
        {
            currentPythonHostBridge()->settingsSet(key, value);
        }
    });
    m.def("settings_remove", [](const std::string& key) {
        if (currentPythonHostBridge())
        {
            currentPythonHostBridge()->settingsRemove(key);
        }
    });
    m.def("settings_keys",
          []() { return currentPythonHostBridge() ? currentPythonHostBridge()->settingsKeys() : py::list(); });
    m.def("log_event", [](const std::string& severity, const std::string& component, const std::string& message) {
        if (currentPythonHostBridge())
        {
            currentPythonHostBridge()->logEvent(severity, component, message);
        }
    });
    m.def("settings_action_update", [](const py::object& value) {
        if (currentPythonHostBridge())
        {
            currentPythonHostBridge()->settingsActionUpdate(pythonObjectToCborValue(value));
        }
    });
    m.def("progress_is_cancelled",
          []() { return currentPythonHostBridge() && currentPythonHostBridge()->progressIsCancelled(); });
    m.def("progress_set_text", [](const std::string& text) {
        if (currentPythonHostBridge())
        {
            currentPythonHostBridge()->progressSetText(text);
        }
    });
    m.def("progress_set_value", [](const double value) {
        if (currentPythonHostBridge())
        {
            currentPythonHostBridge()->progressSetValue(value);
        }
    });
    m.def("image_to_numpy", [](const std::string& token) {
        if (!currentPythonHostBridge())
        {
            throw std::runtime_error("AfterNight host bridge is unavailable");
        }
        return currentPythonHostBridge()->imageToNumpy(token);
    });
    m.def("image_from_numpy", [](const std::string& token, const py::array& value) {
        if (!currentPythonHostBridge())
        {
            throw std::runtime_error("AfterNight host bridge is unavailable");
        }
        currentPythonHostBridge()->imageFromNumpy(token, value);
    });
    m.def("image_apply_linear_transform", [](const std::string& source_token, const std::string& destination_token,
                                             const double scale, const double offset) {
        if (!currentPythonHostBridge())
        {
            throw std::runtime_error("AfterNight host bridge is unavailable");
        }
        currentPythonHostBridge()->imageApplyLinearTransform(source_token, destination_token, scale, offset);
    });
    m.def("image_metadata", [](const std::string& token) {
        if (!currentPythonHostBridge())
        {
            return py::dict();
        }
        return currentPythonHostBridge()->imageMetadata(token);
    });
    m.def("image_properties", [](const std::string& token) {
        if (!currentPythonHostBridge())
        {
            return py::dict();
        }
        return currentPythonHostBridge()->imageProperties(token);
    });
    m.def("image_set_metadata", [](const std::string& token, const std::string& key, const py::object& value) {
        if (currentPythonHostBridge())
        {
            currentPythonHostBridge()->imageSetMetadata(token, key, value);
        }
    });
    m.def("session_info", []() {
        py::dict info;
        if (!currentPythonHostBridge())
        {
            return info;
        }

        info[py::str("session_id")] = py::str(currentPythonHostBridge()->sessionId());
        info[py::str("extension_id")] = py::str(currentPythonHostBridge()->extensionId());
        info[py::str("request_id")] = py::str(currentPythonHostBridge()->currentRequestId());
        info[py::str("launch_mode")] = py::str(currentPythonHostBridge()->launchMode());
        info[py::str("view_name")] = py::str(currentPythonHostBridge()->currentViewName());
        info[py::str("is_standalone")] = currentPythonHostBridge()->isStandalone();
        return info;
    });
    m.def("session_paths", []() {
        py::dict paths;
        const auto session_paths = materializeExtensionSessionPaths(
            currentPythonHostBridge() ? currentPythonHostBridge()->sessionId() : std::string());
        paths[py::str("temp_dir")] = py::str(session_paths.temp_dir.string());
        paths[py::str("artifacts_dir")] = py::str(session_paths.artifacts_dir.string());
        return paths;
    });
    m.def("workflow_get_working_dir", []() {
        if (!currentPythonHostBridge())
        {
            throw std::runtime_error("AfterNight host bridge is unavailable");
        }

        const QCborMap fields = currentPythonHostBridge()->hostProtocolRequest("workflow.get_working_dir");
        return fields.value(QStringLiteral("path")).toString().toStdString();
    });
    m.def("workflow_set_working_dir", [](const std::string& path) {
        if (!currentPythonHostBridge())
        {
            throw std::runtime_error("AfterNight host bridge is unavailable");
        }

        const QCborMap fields = currentPythonHostBridge()->hostProtocolRequest(
            "workflow.set_working_dir", QCborMap{{QStringLiteral("path"), QString::fromStdString(path)}});
        return fields.value(QStringLiteral("path")).toString().toStdString();
    });
    m.def("workflow_temp_dir", []() {
        if (!currentPythonHostBridge())
        {
            throw std::runtime_error("AfterNight host bridge is unavailable");
        }

        const QCborMap fields = currentPythonHostBridge()->hostProtocolRequest("workflow.temp_dir");
        return fields.value(QStringLiteral("path")).toString().toStdString();
    });
    m.def("workflow_output_path", [](const std::string& name) {
        if (!currentPythonHostBridge())
        {
            throw std::runtime_error("AfterNight host bridge is unavailable");
        }

        const QCborMap fields = currentPythonHostBridge()->hostProtocolRequest(
            "workflow.output_path", QCborMap{{QStringLiteral("name"), QString::fromStdString(name)}});
        return fields.value(QStringLiteral("path")).toString().toStdString();
    });
    m.def("workflow_get_sequences", []() -> py::object {
        if (!currentPythonHostBridge())
        {
            return py::list();
        }

        const QCborMap fields = currentPythonHostBridge()->hostProtocolRequest("workflow.get_sequences");
        return cborValueToPythonObject(fields.value(QStringLiteral("sequences")));
    });
    m.def("workflow_open_result_sequence", [](const std::string& path) {
        if (!currentPythonHostBridge())
        {
            throw std::runtime_error("AfterNight host bridge is unavailable");
        }

        const QCborMap fields = currentPythonHostBridge()->hostProtocolRequest(
            "workflow.open_result_sequence", QCborMap{{QStringLiteral("path"), QString::fromStdString(path)}});
        return fields.value(QStringLiteral("accepted")).toBool();
    });
    m.def("theme_tokens", []() -> py::object {
        if (!currentPythonHostBridge())
        {
            return py::dict();
        }

        return cborValueToPythonObject(QCborValue(currentPythonHostBridge()->themeTokens()));
    });
    m.def("current_view_metadata", []() -> py::object {
        if (!currentPythonHostBridge())
        {
            return py::dict();
        }

        return cborValueToPythonObject(QCborValue(currentPythonHostBridge()->currentViewMetadata()));
    });
    m.def("current_view_display_settings", []() -> py::object {
        if (!currentPythonHostBridge())
        {
            return py::dict();
        }

        return cborValueToPythonObject(QCborValue(currentPythonHostBridge()->currentViewDisplaySettings()));
    });
    m.def("current_view_source_image_token", []() -> py::object {
        if (!currentPythonHostBridge())
        {
            return py::none();
        }

        const std::string& token = currentPythonHostBridge()->currentViewSourceImageToken();
        if (token.empty())
        {
            return py::none();
        }

        return py::str(token);
    });
    m.def("current_view_info", []() -> py::object {
        if (!currentPythonHostBridge())
        {
            return py::dict();
        }

        return currentPythonHostBridge()->currentViewInfo();
    });
    m.def("current_view_auxiliary_image_bindings", []() -> py::object {
        if (!currentPythonHostBridge())
        {
            return py::dict();
        }

        return currentPythonHostBridge()->currentViewAuxiliaryImageBindings();
    });
    m.def("ui_show_error", [](const std::string& title, const std::string& message) {
        if (!currentPythonHostBridge())
        {
            return false;
        }

        std::string error;
        if (!currentPythonHostBridge()->uiNotify("ui.show_error", title, message, &error))
        {
            throw std::runtime_error(error.empty() ? "ui.show_error failed" : error);
        }
        return true;
    });
    m.def("ui_show_warning", [](const std::string& title, const std::string& message) {
        if (!currentPythonHostBridge())
        {
            return false;
        }

        std::string error;
        if (!currentPythonHostBridge()->uiNotify("ui.show_warning", title, message, &error))
        {
            throw std::runtime_error(error.empty() ? "ui.show_warning failed" : error);
        }
        return true;
    });
    m.def("ui_show_info", [](const std::string& title, const std::string& message) {
        if (!currentPythonHostBridge())
        {
            return false;
        }

        std::string error;
        if (!currentPythonHostBridge()->uiNotify("ui.show_info", title, message, &error))
        {
            throw std::runtime_error(error.empty() ? "ui.show_info failed" : error);
        }
        return true;
    });
    m.def("ui_confirm", [](const std::string& title, const std::string& message, const bool warn) {
        if (!currentPythonHostBridge())
        {
            return false;
        }

        bool confirmed = false;
        std::string error;
        if (!currentPythonHostBridge()->uiConfirm(title, message, warn, &confirmed, &error))
        {
            throw std::runtime_error(error.empty() ? "ui.confirm failed" : error);
        }
        return confirmed;
    });
    m.def("ui_open_image", [](const py::array& value, const py::dict& metadata, const std::string& title) {
        if (!currentPythonHostBridge())
        {
            return false;
        }

        std::string error;
        if (!currentPythonHostBridge()->uiOpenImage(value, metadata, title, &error))
        {
            throw std::runtime_error(error.empty() ? "ui.open_image failed" : error);
        }
        return true;
    });
    m.def("list_views", []() {
        if (!currentPythonHostBridge())
        {
            return py::list();
        }
        return currentPythonHostBridge()->listViews();
    });
    m.def("view_snapshot", [](const std::string& view_name) {
        if (!currentPythonHostBridge())
        {
            throw std::runtime_error("AfterNight host bridge is unavailable");
        }
        return currentPythonHostBridge()->snapshotView(view_name);
    });
    m.def("process_window_hide",
          []() { return currentPythonHostBridge() && currentPythonHostBridge()->hideNativeProcessWindow(); });
    m.def("process_window_close",
          []() { return currentPythonHostBridge() && currentPythonHostBridge()->closeNativeProcessWindow(); });
    m.def(
        "view_commit_numpy",
        [](const py::array& value, const py::dict& metadata, const std::string& history_step_name,
           const std::string& history_step_description, const bool allow_resized_output) {
            if (!currentPythonHostBridge())
            {
                throw std::runtime_error("AfterNight host bridge is unavailable");
            }

            std::string error;
            if (!currentPythonHostBridge()->commitCurrentViewResult(
                    value, metadata, history_step_name, history_step_description, allow_resized_output, &error))
            {
                throw std::runtime_error(error.empty() ? "view.commit failed" : error);
            }
            return true;
        },
        py::arg("value"), py::arg("metadata"), py::arg("history_step_name"), py::arg("history_step_description"),
        py::arg("allow_resized_output") = false);
}

/// The request a native run's source is exported under: the extension may write it, and a preview area is the export's
/// region.
an::ui::extensions::ImageExportRequest sourceExportRequest(const std::string& session, const std::string& request_id,
                                                           const NativeSource& source)
{
    an::ui::extensions::ImageExportRequest request;
    request.sessionId = session;
    request.role = an::ui::extensions::SharedBufferRole::Source;
    request.accessMode = an::ui::extensions::SharedBufferAccessMode::ReadWrite;
    request.bufferId = request_id + ".src";
    request.symbolicName = source.name;
    request.region = source.region;
    request.fileName = source.file_name;
    return request;
}

/// The request a native run's manual mask is exported under: read-only, known as the selection, over the same area
/// as its source.
an::ui::extensions::ImageExportRequest maskExportRequest(const std::string& session, const std::string& request_id,
                                                         const std::optional<QRect>& region)
{
    an::ui::extensions::ImageExportRequest request;
    request.sessionId = session;
    request.role = an::ui::extensions::SharedBufferRole::Mask;
    request.accessMode = an::ui::extensions::SharedBufferAccessMode::ReadOnly;
    request.bufferId = request_id + ".selection";
    request.symbolicName = "selection";
    request.region = region;
    return request;
}

class EmbeddedExtensionRuntime
{
public:
    ~EmbeddedExtensionRuntime()
    {
        shutdown();
    }

    [[nodiscard]] PythonHostBridge* bridge() noexcept
    {
        return &bridge_;
    }

    void setAppContext(an::ui::extensions::ExtensionAppContext* app_context) noexcept
    {
        app_context_ = app_context;
        bridge_.setAppContext(app_context);
    }

    void setNativeProcessWindow(an::ui::extensions::ExtensionNativeProcessWindow* window) noexcept
    {
        bridge_.setNativeProcessWindow(window);
    }

    void setWindowlessCloseHandler(std::function<void()> handler)
    {
        bridge_.setWindowlessCloseHandler(std::move(handler));
    }

    void setIgnoreCancel(const bool ignore_cancel) noexcept
    {
        bridge_.setIgnoreCancel(ignore_cancel);
    }

    void requestCurrentExecutionCancel() noexcept
    {
        bridge_.setCancelled(true);
        interruptActivePythonExecution();
    }

    void setInspectSettingsSnapshot(QCborMap snapshot)
    {
        bridge_.setInspectSettingsSnapshot(std::move(snapshot));
    }

    bool handleInboundHostNotification(const an::ui::extensions::ExtensionProtocolMessage& inbound, bool* handled,
                                       std::string* error)
    {
        return bridge_.handleInboundHostNotification(inbound, handled, error);
    }

    [[nodiscard]] bool lastRequestCancelled() const noexcept
    {
        return last_request_cancelled_;
    }

    [[nodiscard]] const std::string& currentViewId() const noexcept
    {
        return bridge_.currentViewId();
    }

    bool initialize(const std::filesystem::path& app_dir, const std::filesystem::path& runtime_root,
                    const std::filesystem::path& environment_root, const std::filesystem::path& extension_package_root,
                    const std::string& entry_point, const std::string& process_class, const std::string& sdk_backend,
                    const bool inspect_settings, const bool inspect_params,
                    const std::optional<QJsonArray>& cached_params_schema, std::string* error)
    {
        shutdown();
        entry_point_ = entry_point;
        process_class_ = process_class;
        settings_schema_ = QJsonArray{};
        params_schema_ = cached_params_schema.value_or(QJsonArray{});

        if (runtime_root.empty())
        {
            if (error)
            {
                *error = "Missing Python runtime root for the extension host";
            }
            return false;
        }
        if (environment_root.empty())
        {
            if (error)
            {
                *error = "Missing extension environment root for the extension host";
            }
            return false;
        }
        if (!std::filesystem::exists(environment_root))
        {
            if (error)
            {
                *error = "Extension environment root does not exist";
            }
            return false;
        }

        const auto env_python = environmentPythonExecutable(environment_root);
        const auto base_python = bundledPythonExecutableForRoots(app_dir, runtime_root);
        if (env_python.empty() || base_python.empty())
        {
            if (error)
            {
                *error = "Could not resolve the environment/base Python executables for the extension host";
            }
            return false;
        }

        if (sdk_backend == "runtime")
        {
            const auto module_path = runtimeBackendModulePath(app_dir);
            if (module_path.empty())
            {
                if (error)
                {
                    *error = "Runtime backend module is not available next to the extension host";
                }
                return false;
            }
            if (const an::Status loaded = runtime_backend_.load(module_path); loaded.failed())
            {
                if (error)
                {
                    *error = loaded.message();
                }
                return false;
            }
        }

        PyConfig config;
        PyConfig_InitIsolatedConfig(&config);
        config.parse_argv = 0;
        config.install_signal_handlers = 1;
        config.site_import = 0;
        config.user_site_directory = 0;
        config.safe_path = 1;
        config.module_search_paths_set = 1;

        const auto stdlib_path = runtimeStdlibPath(runtime_root);
        const auto stdlib_zip_path = runtimeStdlibZipPath(runtime_root);
        const auto dynload_path = runtimeDynloadPath(runtime_root);
        const auto site_packages_path = environmentSitePackagesPath(environment_root);

        bool config_ready = setPyConfigString(&config, &config.home, runtime_root.wstring(), error) &&
                            setPyConfigString(&config, &config.program_name, env_python.wstring(), error) &&
                            setPyConfigString(&config, &config.executable, env_python.wstring(), error) &&
                            setPyConfigString(&config, &config.base_executable, base_python.wstring(), error) &&
                            setPyConfigString(&config, &config.prefix, environment_root.wstring(), error) &&
                            setPyConfigString(&config, &config.exec_prefix, environment_root.wstring(), error) &&
                            setPyConfigString(&config, &config.base_prefix, runtime_root.wstring(), error) &&
                            setPyConfigString(&config, &config.base_exec_prefix, runtime_root.wstring(), error) &&
                            appendPySearchPath(&config, site_packages_path, error) &&
                            appendPySearchPath(&config, stdlib_zip_path, error) &&
                            appendPySearchPath(&config, stdlib_path, error) &&
                            appendPySearchPath(&config, dynload_path, error);

        if (!config_ready)
        {
            PyConfig_Clear(&config);
            return false;
        }

        try
        {
            constexpr int argc = 0;
            const char* const* argv = nullptr;
            py::initialize_interpreter(&config, argc, argv, false);
            python_initialized_ = true;
        }
        catch (const std::exception& e)
        {
            PyConfig_Clear(&config);
            if (error)
            {
                *error = e.what();
            }
            return false;
        }
        PyConfig_Clear(&config);

        try
        {
            py::gil_scoped_acquire gil;
            py::module_ sys = py::module_::import("sys");
            py::object sys_path = sys.attr("path");
            sys_path.attr("insert")(0, environment_root.string());
            if (!extension_package_root.empty())
            {
                sys_path.attr("insert")(0, extension_package_root.string());
            }

            importHostManagedAfternight(app_dir, runtime_root);
        }
        catch (const py::error_already_set& e)
        {
            if (error)
            {
                *error = pyExceptionMessage(e);
            }
            shutdown();
            return false;
        }
        catch (const std::exception& e)
        {
            if (error)
            {
                *error = e.what();
            }
            shutdown();
            return false;
        }

        if (sdk_backend == "runtime")
        {
            if (const an::Status probed = runtime_backend_.probe(); probed.failed())
            {
                if (error)
                {
                    *error = probed.message();
                }
                shutdown();
                return false;
            }
            if (const an::Status initialized = runtime_backend_.initialize(); initialized.failed())
            {
                if (error)
                {
                    *error = initialized.message();
                }
                shutdown();
                return false;
            }

            // The backend's session is the one the native windows work in: borrowed through the embedded module's
            // checked capsule and lent to the host's context before any native window exists. A missing or
            // mismatched capsule stops the start.
            an::Status borrowed = an::Status::success();
            {
                py::gil_scoped_acquire gil;
                borrowed = host_session_.borrowFromInterpreter();
            }
            if (borrowed.failed())
            {
                if (error)
                {
                    *error = std::string("The runtime backend's engine session could not be borrowed: ") +
                             borrowed.message();
                }
                shutdown();
                return false;
            }
            if (app_context_)
            {
                app_context_->setEngineSessionProvider(&host_session_);
            }
        }

        if (!entry_point.empty())
        {
            if (!loadExtensionDefinition(inspect_settings, inspect_params && !cached_params_schema.has_value(), error))
            {
                shutdown();
                return false;
            }
        }

        releaseMainThreadGil();
        return true;
    }

    /// Takes the runtime down in the host's order: admission closed; the native windows the caller owns destroyed,
    /// then the host-local views and what the bridge holds; the thread pool drained; the borrow let go; the interpreter
    /// finalized; the backend asked to shut its session down, and unloaded when it accepted. A backend that still sees
    /// an owner keeps its session and stays loaded until the process ends, and says why in the log.
    ///
    /// @param destroy_native_windows destroys the native windows; may be empty.
    void shutdown(const std::function<void()>& destroy_native_windows = {}) noexcept
    {
        an::ui::extensions::HostTeardownSteps steps;
        steps.destroyNativeWindows = [&]() {
            if (destroy_native_windows)
            {
                destroy_native_windows();
            }
            // Every frame has landed once its window is gone: nothing they kept is lent any more.
            closePreviewFrameBuffers();
            bridge_.clearCurrentRequest();
            bridge_.detachSocket();
            if (app_context_)
            {
                app_context_->setEngineSessionProvider(nullptr);
            }
        };
        steps.finalizeInterpreter = [&]() {
            if (python_initialized_ && Py_IsInitialized())
            {
                restoreMainThreadGil();
                extension_instance_ = py::object();
                extension_class_ = py::object();
                afternight_module_ = py::object();
                bridge_.clearImages();
                py::finalize_interpreter();
            }
            python_initialized_ = false;
            main_thread_state_ = nullptr;
        };
        try
        {
            if (const an::Status shut = an::ui::extensions::shutDownHostRuntime(host_session_, runtime_backend_, steps);
                shut.failed())
            {
                LOG_WARNING("The runtime backend kept its engine session at shutdown: {}", shut.message());
            }
        }
        catch (const std::exception& failure)
        {
            LOG_ERROR("The extension runtime did not shut down cleanly: {}", failure.what());
        }
    }

    void attachSessionSocket(QLocalSocket* socket, QByteArray* buffered_input, const std::string& session_id,
                             const std::string& extension_id, std::string* error)
    {
        bridge_.attachSocket(socket, buffered_input, session_id, extension_id);
        if (!extension_class_ || extension_instance_)
        {
            return;
        }

        try
        {
            py::gil_scoped_acquire gil;
            ScopedPythonHostBridgeBinding bridge_binding(&bridge_);
            py::object settings = afternight_module_.attr("Settings")();
            py::object context = afternight_module_.attr("ExtensionContext")(settings);
            extension_instance_ = extension_class_(context);
        }
        catch (const py::error_already_set& e)
        {
            const std::string message = reportPythonFailure(e);
            if (error)
            {
                *error = message;
            }
        }
        catch (const std::exception& e)
        {
            const std::string message = e.what();
            try
            {
                bridge_.logEvent("error", "extension.runtime", message);
            }
            catch (...)
            {
            }
            if (error)
            {
                *error = message;
            }
        }
    }

    [[nodiscard]] bool hasExecutableExtension() const noexcept
    {
        return extension_instance_ && !extension_instance_.is_none();
    }

    [[nodiscard]] bool hasPreviewableExtension() const noexcept
    {
        // Preview capability is determined from the loaded class definition
        // during bootstrap and must be available before the live instance is
        // attached so the native window can build the correct view area.
        return hasLoadedExtensionDefinition() && is_previewable_;
    }

    [[nodiscard]] bool hasLoadedExtensionDefinition() const noexcept
    {
        return extension_class_ && !extension_class_.is_none();
    }

    [[nodiscard]] bool nativeProcessWindowHiddenByExtension() const noexcept
    {
        return bridge_.nativeProcessWindowHiddenByExtension();
    }

    bool notifyProcessLaunch(std::string* error)
    {
        return callOptionalExtensionHook("on_process_launch", error, [](const py::object& hook) { hook(); });
    }

    bool notifyProcessClose(const std::string& reason, std::string* error)
    {
        return callOptionalExtensionHook("on_process_close", error,
                                         [&](const py::object& hook) { hook(py::str(reason)); });
    }

    bool notifyInputChanged(std::string* error)
    {
        return callOptionalExtensionHook("on_input_changed", error,
                                         [&](const py::object& hook) { hook(currentInputMetadataDict()); });
    }

    bool notifyParametersChanged(const QCborMap& params, const QStringList& changed_keys, const QString& reason,
                                 std::string* error)
    {
        return callOptionalExtensionHook("on_parameters_changed", error, [&](const py::object& hook) {
            hook(cborMapToPythonDict(params), stringListToPythonList(changed_keys), py::str(reason.toStdString()));
        });
    }

    bool notifyExecutionCancelled(std::string* error)
    {
        return callOptionalExtensionHook("on_execution_cancelled", error, [](const py::object& hook) { hook(); });
    }

    bool notifyPresetLoaded(const QCborMap& params, const QCborMap& preset_metadata, std::string* error)
    {
        return callOptionalExtensionHook("on_preset_loaded", error, [&](const py::object& hook) {
            hook(cborMapToPythonDict(params), cborMapToPythonDict(preset_metadata));
        });
    }

    bool notifyPresetSaved(const QCborMap& params, const QCborMap& preset_metadata, std::string* error)
    {
        return callOptionalExtensionHook("on_preset_saved", error, [&](const py::object& hook) {
            hook(cborMapToPythonDict(params), cborMapToPythonDict(preset_metadata));
        });
    }

    bool validateProcess(const QCborMap& params, QCborValue* out_diagnostics, std::string* error)
    {
        if (out_diagnostics)
        {
            *out_diagnostics = QCborValue(QCborArray{});
        }
        if (!hasExecutableExtension())
        {
            return true;
        }

        try
        {
            py::gil_scoped_acquire gil;
            ScopedPythonHostBridgeBinding bridge_binding(&bridge_);
            if (!py::hasattr(extension_instance_, "validate_process"))
            {
                return true;
            }

            const py::object result =
                extension_instance_.attr("validate_process")(cborMapToPythonDict(params), currentInputMetadataDict());
            if (out_diagnostics && !result.is_none())
            {
                *out_diagnostics = pythonObjectToCborValue(result);
            }
            return true;
        }
        catch (const py::error_already_set& e)
        {
            const std::string message = reportPythonFailure(e);
            if (error)
            {
                *error = message;
            }
            return false;
        }
        catch (const std::exception& e)
        {
            const std::string message = e.what();
            try
            {
                bridge_.logEvent("error", "extension.runtime", message);
            }
            catch (...)
            {
            }
            if (error)
            {
                *error = message;
            }
            return false;
        }
    }

    /// Execute the loaded Python extension against the image currently shown in
    /// the native process window.  A pair of temporary shared-memory buffers is
    /// created, the extension's execute() method is called, and the resulting
    /// image is returned via @p out_result_image.
    ///
    /// When @p source_image_override is non-null it is used as the source
    /// instead of reading from the native source view.  This lets the execute
    /// callback use a cached pristine source even after previews have
    /// overwritten the view image.
    bool executeNative(const std::string& session_id, const std::string& request_id, const QCborMap& params,
                       OwnedAsset* out_result_image, std::string* error, const NativeSource& source,
                       const bool reset_cancel_at_start = true, const SharedAsset& manual_mask_image = nullptr)
    {
        if (!hasExecutableExtension())
        {
            if (error)
            {
                *error = "No executable extension is loaded";
            }
            return false;
        }

        if (!source.image)
        {
            if (error)
            {
                *error = "Source view has no image to execute against";
            }
            return false;
        }

        // Use a temporary buffer root scoped to this session so that the
        // file-backed shared memory doesn't collide with socket-driven buffers.
        const auto session_paths = materializeExtensionSessionPaths(session_id + ".native");
        an::ui::extensions::SharedBufferManager manager(session_paths.root.parent_path());

        const std::string native_session = session_id + ".native";
        an::ui::extensions::SharedImageBuffer source_buffer;
        an::ui::extensions::SharedImageBuffer output_buffer;
        std::vector<an::ui::extensions::SharedImageBuffer> auxiliary_buffers;
        // Nothing reads what the run was lent once it has returned and its result is copied out.
        const an::ui::extensions::LentBuffersRemoval removal({&source_buffer, &output_buffer}, &auxiliary_buffers);
        if (!exportSharedBuffer(manager, *source.image, sourceExportRequest(native_session, request_id, source),
                                &source_buffer, error))
        {
            return false;
        }

        const auto& src_desc = source_buffer.descriptor();
        an::ui::extensions::SharedImageBufferSpec output_spec;
        output_spec.sessionId = native_session;
        output_spec.bufferId = request_id + ".dst";
        output_spec.symbolicName = "destination";
        output_spec.width = src_desc.width;
        output_spec.height = src_desc.height;
        output_spec.channels = src_desc.channels;
        output_spec.pixelType = src_desc.pixel_type;
        output_spec.pixelFormat = src_desc.pixel_format;
        output_spec.colorSpace = src_desc.color_space;
        output_spec.accessMode = an::ui::extensions::SharedBufferAccessMode::ReadWrite;
        output_spec.bufferRole = an::ui::extensions::SharedBufferRole::Destination;
        output_spec.metadataSidecar = src_desc.metadata_sidecar;

        if (!allocateSharedBuffer(manager, output_spec, &output_buffer, error))
        {
            return false;
        }

        if (manual_mask_image)
        {
            an::ui::extensions::SharedImageBuffer mask_buffer;
            if (!exportSharedBuffer(manager, *manual_mask_image,
                                    maskExportRequest(native_session, request_id, std::nullopt), &mask_buffer, error))
            {
                return false;
            }
            auxiliary_buffers.push_back(std::move(mask_buffer));
        }
        auto* auxiliary_ptr = auxiliary_buffers.empty() ? nullptr : &auxiliary_buffers;

        if (!execute(session_id, request_id, request_id, params, "execute", &source_buffer, &output_buffer,
                     auxiliary_ptr, error, reset_cancel_at_start))
        {
            return false;
        }

        if (out_result_image)
        {
            an::ui::extensions::ImportedImage result_image;
            if (!importSharedBuffer(output_buffer.descriptor(), &result_image, error))
            {
                return false;
            }
            *out_result_image = std::make_shared<an::consumer::ImageAsset>(std::move(result_image.asset));
        }

        return true;
    }

    bool runNativeParamAction(const std::string& session_id, const std::string& request_id,
                              const std::string& action_id, const QCborMap& params, QCborMap* out_updates,
                              std::string* error, const NativeSource& source)
    {
        if (out_updates)
        {
            *out_updates = QCborMap{};
        }

        if (!hasExecutableExtension())
        {
            if (error)
            {
                *error = "No executable extension is loaded";
            }
            return false;
        }
        if (!source.image)
        {
            if (error)
            {
                *error = "Source view has no image for the parameter action";
            }
            return false;
        }

        const auto session_paths = materializeExtensionSessionPaths(session_id + ".action");
        an::ui::extensions::SharedBufferManager manager(session_paths.root.parent_path());

        const std::string action_session = session_id + ".action";
        an::ui::extensions::SharedImageBuffer source_buffer;
        const an::ui::extensions::LentBuffersRemoval removal({&source_buffer}, nullptr);
        if (!exportSharedBuffer(manager, *source.image, sourceExportRequest(action_session, request_id, source),
                                &source_buffer, error))
        {
            return false;
        }

        // The action binds its image on the bridge, as a run does: it waits for the run in the lane, which the window
        // stopped first.
        const std::lock_guard lane(execution_lane_);
        last_request_cancelled_ = false;
        try
        {
            py::gil_scoped_acquire gil;
            if (!py::hasattr(extension_instance_, "handle_param_action"))
            {
                if (error)
                {
                    *error = "The extension does not implement parameter actions";
                }
                return false;
            }

            ScopedPythonHostBridgeBinding bridge_binding(&bridge_);
            bridge_.clearImages();
            bridge_.setCurrentRequestId(request_id);
            bridge_.setCancelled(false);
            bridge_.resetProgressFeedback();

            RuntimeImageBinding src_binding;
            src_binding.buffer = &source_buffer;
            src_binding.descriptor = source_buffer.descriptor();
            src_binding.writable = false;
            bridge_.bindImage("source", std::move(src_binding));

            py::object image_type = afternight_module_.attr("ImageHandle");
            py::object view_type = afternight_module_.attr("ViewRef");
            py::object src_image = image_type("source");
            py::dict py_params;
            for (auto it = params.begin(); it != params.end(); ++it)
            {
                py_params[py::str(it.key().toString().toStdString())] = cborValueToPythonObject(it.value());
            }
            py_params[py::str("_params_token")] = request_id;
            py::object view_ref =
                view_type(bridge_.currentViewId(), bridge_.currentViewName(), bridge_.currentFileName());

            const py::object result =
                extension_instance_.attr("handle_param_action")(py::str(action_id), view_ref, src_image, py_params);
            const QCborValue result_value = pythonObjectToCborValue(result);
            if (!result_value.isMap())
            {
                throw std::runtime_error("handle_param_action() must return an object");
            }

            if (out_updates)
            {
                *out_updates = result_value.toMap();
            }

            last_request_cancelled_ = bridge_.currentRequestCancelled();
            bridge_.clearImages();
            bridge_.clearCurrentRequest();
            return true;
        }
        catch (const py::error_already_set& e)
        {
            const std::string message = reportPythonFailure(e);
            if (error)
            {
                *error = message;
            }
        }
        catch (const std::exception& e)
        {
            const std::string message = e.what();
            try
            {
                bridge_.logEvent("error", "extension.runtime", message);
            }
            catch (...)
            {
            }
            if (error)
            {
                *error = message;
            }
        }

        last_request_cancelled_ = bridge_.currentRequestCancelled();
        bridge_.clearImages();
        bridge_.clearCurrentRequest();
        return false;
    }

    bool runSettingsAction(const std::string& action_id, const QCborMap& settings_snapshot, QCborMap* out_result,
                           std::string* error, std::function<void(const QCborMap&)> update_callback = {})
    {
        if (out_result)
        {
            *out_result = QCborMap{};
        }

        if (!hasLoadedExtensionDefinition())
        {
            if (error)
            {
                *error = "No extension class is loaded";
            }
            return false;
        }

        try
        {
            py::gil_scoped_acquire gil;
            ScopedPythonHostBridgeBinding bridge_binding(&bridge_);
            bridge_.setSettingsActionUpdateCallback(std::move(update_callback));
            struct SettingsActionUpdateGuard
            {
                PythonHostBridge* bridge = nullptr;
                ~SettingsActionUpdateGuard()
                {
                    if (bridge)
                    {
                        bridge->clearSettingsActionUpdateCallback();
                    }
                }
            } update_guard{&bridge_};

            py::object settings = afternight_module_.attr("Settings")();
            py::object context = afternight_module_.attr("ExtensionContext")(settings);
            py::object instance = extension_class_(context);
            if (!py::hasattr(instance, "handle_settings_action"))
            {
                if (error)
                {
                    *error = "The extension does not implement settings actions";
                }
                return false;
            }

            const py::object py_snapshot = cborValueToPythonObject(QCborValue(settings_snapshot));
            const py::object result = instance.attr("handle_settings_action")(py::str(action_id), py_snapshot);
            const QCborValue result_value = pythonObjectToCborValue(result);
            if (!result_value.isMap())
            {
                throw std::runtime_error("handle_settings_action() must return an object");
            }

            QCborMap result_map = result_value.toMap();
            if (!result_map.contains(QStringLiteral("ok")))
            {
                result_map.insert(QStringLiteral("ok"), false);
            }
            if (!result_map.contains(QStringLiteral("tone")))
            {
                result_map.insert(QStringLiteral("tone"), result_map.value(QStringLiteral("ok")).toBool(false)
                                                              ? QStringLiteral("success")
                                                              : QStringLiteral("error"));
            }
            if (out_result)
            {
                *out_result = std::move(result_map);
            }
            return true;
        }
        catch (const py::error_already_set& e)
        {
            if (error)
            {
                *error = pyExceptionMessage(e);
            }
        }
        catch (const std::exception& e)
        {
            if (error)
            {
                *error = e.what();
            }
        }

        return false;
    }

    bool executeWorkflow(const std::string& session_id, const std::string& request_id, const QCborMap& params_snapshot,
                         std::string* error, const bool reset_cancel_at_start = true)
    {
        last_request_cancelled_ = false;
        if (!hasExecutableExtension())
        {
            if (error)
            {
                *error = "No executable workflow extension is loaded";
            }
            return false;
        }

        try
        {
            py::gil_scoped_acquire gil;
            ScopedActivePythonExecution active_python_execution(this);
            ScopedPythonHostBridgeBinding bridge_binding(&bridge_);
            bridge_.clearImages();
            bridge_.setCurrentRequestContext(request_id, params_snapshot);
            if (reset_cancel_at_start)
            {
                bridge_.setCancelled(false);
            }
            bridge_.resetProgressFeedback();
            bridge_.resetManualViewCommitTracking();

            py::dict params;
            for (auto it = params_snapshot.begin(); it != params_snapshot.end(); ++it)
            {
                params[py::str(it.key().toString().toStdString())] = cborValueToPythonObject(it.value());
            }
            params[py::str("_params_token")] = request_id;

            py::object workflow = afternight_module_.attr("WorkflowContext")();
            py::object progress = afternight_module_.attr("WorkflowProgress")();
            extension_instance_.attr("execute_workflow")(workflow, params, progress);

            last_request_cancelled_ = bridge_.currentRequestCancelled();
            bridge_.clearImages();
            bridge_.clearCurrentRequest();
            return true;
        }
        catch (const py::error_already_set& e)
        {
            const std::string message = reportPythonFailure(e);
            if (error)
            {
                *error = message;
            }
        }
        catch (const std::exception& e)
        {
            const std::string message = e.what();
            try
            {
                bridge_.logEvent("error", "extension.runtime", message);
            }
            catch (...)
            {
            }
            if (error)
            {
                *error = message;
            }
        }

        last_request_cancelled_ = bridge_.currentRequestCancelled();
        bridge_.clearImages();
        bridge_.clearCurrentRequest();
        return false;
    }

    [[nodiscard]] const QJsonArray& inspectedSettingsSchema() const noexcept
    {
        return settings_schema_;
    }

    [[nodiscard]] const QJsonArray& inspectedParamsSchema() const noexcept
    {
        return params_schema_;
    }

    void inspectPySide6UiRuntime(QJsonObject* out) const
    {
        if (!out)
        {
            return;
        }

        try
        {
            py::gil_scoped_acquire gil;
            py::module_ qtwidgets = py::module_::import("PySide6.QtWidgets");
            out->insert(QStringLiteral("has_pyside6"), true);

            const py::object app_instance = qtwidgets.attr("QApplication").attr("instance")();
            out->insert(QStringLiteral("pyside6_has_qapplication"), !app_instance.is_none());

            const py::list style_keys = qtwidgets.attr("QStyleFactory").attr("keys")();
            bool has_afternight_style_key = false;
            QJsonArray style_key_array;
            for (const py::handle item : style_keys)
            {
                const QString key = QString::fromStdString(py::str(item).cast<std::string>());
                style_key_array.append(key);
                if (key.compare(QStringLiteral("afternight"), Qt::CaseInsensitive) == 0)
                {
                    has_afternight_style_key = true;
                }
            }
            out->insert(QStringLiteral("pyside6_style_keys"), style_key_array);
            out->insert(QStringLiteral("pyside6_has_afternight_style_key"), has_afternight_style_key);

            const py::object style = qtwidgets.attr("QStyleFactory").attr("create")(std::string("afternight"));
            out->insert(QStringLiteral("pyside6_can_create_afternight_style"), !style.is_none());
            if (!style.is_none())
            {
                out->insert(QStringLiteral("pyside6_style_class"),
                            QString::fromStdString(style.attr("metaObject")().attr("className")().cast<std::string>()));
            }
        }
        catch (const py::error_already_set& e)
        {
            out->insert(QStringLiteral("has_pyside6"), false);
            out->insert(QStringLiteral("pyside6_error"), QString::fromStdString(pyExceptionMessage(e)));
        }
        catch (const std::exception& e)
        {
            out->insert(QStringLiteral("has_pyside6"), false);
            out->insert(QStringLiteral("pyside6_error"), QString::fromStdString(e.what()));
        }
    }

    /// Runs the extension's execute or execute_preview over buffers already lent, in the lane every such run takes in
    /// turn: the bridge binds one run's images at a time.
    bool execute(const std::string& session_id, const std::string& request_id, const std::string& params_token,
                 const QCborMap& params_snapshot, const std::string& request_kind,
                 an::ui::extensions::SharedImageBuffer* source_buffer,
                 an::ui::extensions::SharedImageBuffer* destination_buffer,
                 std::vector<an::ui::extensions::SharedImageBuffer>* auxiliary_buffers, std::string* error,
                 const bool reset_cancel_at_start = true)
    {
        const std::lock_guard lane(execution_lane_);
        return executeInLane(session_id, request_id, params_token, params_snapshot, request_kind, source_buffer,
                             destination_buffer, auxiliary_buffers, error, reset_cancel_at_start);
    }

    /// Runs the extension's execute_preview over one frame of the native window's preview, on the frame's worker: the
    /// frame's source - the image, a reduction of it or a part of it - and the manual mask laid over it are lent at the
    /// frame's own size, the extension's run is stopped when the frame is, and what it wrote is taken back as a new
    /// asset of the session; the frame's buffers go back to the window's pool once it is done, for the next frame of
    /// their shape. A frame taken back is traced when the host was asked to (`AFTERNIGHT_PREVIEW_FRAME_TRACE`).
    ///
    /// @return the asset; `ErrorCode::Cancelled` for a frame stopped before or while it ran; or why the extension
    ///         or the seam refused it.
    an::Result<an::consumer::ImageAsset> previewFrame(const std::string& session_id,
                                                      const an::ui::extensions::ExtensionPreviewFrame& frame)
    {
        const auto started = std::chrono::steady_clock::now();
        an::ui::extensions::PreviewFrameSpans spans;
        if (!hasExecutableExtension())
        {
            return an::Status::failure(an::ErrorCode::NotInitialized, "No executable extension is loaded");
        }
        const an::consumer::EngineSession* session = hostEngineSession();
        if (session == nullptr)
        {
            return an::Status::failure(an::ErrorCode::NotInitialized, "the host has no engine session");
        }

        const std::shared_ptr<an::ui::extensions::PreviewFrameBufferPool> buffers = previewFrameBuffers(session_id);
        const std::string request_id = an::ui::extensions::generateOpaqueToken();
        spans.prepare = std::chrono::steady_clock::now() - started;
        an::Result<an::ui::extensions::PreviewFrameBuffers> lent =
            an::ui::extensions::lendPreviewFrame(*buffers, frame, &spans);
        if (!lent.hasValue())
        {
            return lent.status();
        }

        {
            const std::lock_guard lane(execution_lane_);
            if (frame.stop.stop_requested())
            {
                return an::Status::failure(an::ErrorCode::Cancelled, "the preview frame was stopped");
            }
            // What the extension says of these parameters, as before every preview, asked in the lane the frame's run
            // holds: a validation of the next frame never runs between this frame's calls, where an extension that
            // keeps what it derives on itself would paint this frame with the next one's values. A blocking diagnostic
            // is the frame's failure, which the window reports once in its banner.
            QCborValue diagnostics;
            std::string validation_error;
            if (!validateProcess(frame.params, &diagnostics, &validation_error))
            {
                return an::Status::failure(an::ErrorCode::ExternalFailure, validation_error);
            }
            if (const std::optional<QString> blocking = blockingValidationDiagnostic(diagnostics); blocking.has_value())
            {
                return an::Status::failure(an::ErrorCode::InvalidArgument, blocking->toStdString());
            }
            // The frame's stop reaches the extension's run as the window's Cancel does, for as long as it runs.
            bridge_.setCancelled(false);
            const std::stop_callback stopping(frame.stop, [this]() { requestCurrentExecutionCancel(); });
            std::string error;
            auto* auxiliary = lent->auxiliary.empty() ? nullptr : &lent->auxiliary;
            const bool ran = executeInLane(session_id, request_id, request_id, frame.params, "preview", &lent->source,
                                           &lent->output, auxiliary, &error, false, &spans);
            if (last_request_cancelled_ || frame.stop.stop_requested())
            {
                return an::Status::failure(an::ErrorCode::Cancelled, "the preview frame was stopped");
            }
            if (!ran)
            {
                return an::Status::failure(an::ErrorCode::ExternalFailure,
                                           error.empty() ? std::string("the extension's preview failed") : error);
            }
        }
        an::Result<an::consumer::ImageAsset> taken =
            an::ui::extensions::takePreviewFrameResult(buffers->manager(), *session, *lent, &spans);
        lent->release(&spans);
        if (taken.hasValue())
        {
            preview_trace_.write(spans);
        }
        return taken;
    }

    /// Ends what the native window's frames keep: every buffer they wait in is removed now, one a frame still holds
    /// once it gives it back, and a later frame keeps nothing. The window's session ends with it.
    void closePreviewFrameBuffers() noexcept
    {
        std::shared_ptr<an::ui::extensions::PreviewFrameBufferPool> buffers;
        {
            const std::lock_guard lock(preview_buffers_mutex_);
            buffers = preview_buffers_;
        }
        if (buffers)
        {
            buffers->close();
        }
    }

private:
    /// The buffers the native window's frames reuse, those of the session they are lent in: made by the session's first
    /// frame, under the directory named after it; one of another session closes the last.
    [[nodiscard]] std::shared_ptr<an::ui::extensions::PreviewFrameBufferPool>
    previewFrameBuffers(const std::string& session_id)
    {
        const std::lock_guard lock(preview_buffers_mutex_);
        if (!preview_buffers_ || preview_buffers_session_ != session_id)
        {
            if (preview_buffers_)
            {
                preview_buffers_->close();
            }
            const std::filesystem::path frames_root =
                an::ui::extensions::extensionHostPreviewFramesRootPath(session_id);
            preview_buffers_ = std::make_shared<an::ui::extensions::PreviewFrameBufferPool>(
                an::ui::extensions::SharedBufferManager(frames_root.parent_path()), frames_root.filename().string());
            preview_buffers_session_ = session_id;
        }
        return preview_buffers_;
    }

    /// @param spans where a preview states how long it bound its buffers, ran and flushed its output's metadata; may
    ///        be null.
    bool executeInLane(const std::string& session_id, const std::string& request_id, const std::string& params_token,
                       const QCborMap& params_snapshot, const std::string& request_kind,
                       an::ui::extensions::SharedImageBuffer* source_buffer,
                       an::ui::extensions::SharedImageBuffer* destination_buffer,
                       std::vector<an::ui::extensions::SharedImageBuffer>* auxiliary_buffers, std::string* error,
                       const bool reset_cancel_at_start, an::ui::extensions::PreviewFrameSpans* spans = nullptr)
    {
        last_request_cancelled_ = false;
        if (!hasExecutableExtension())
        {
            return false;
        }
        if (!source_buffer || !destination_buffer)
        {
            if (error)
            {
                *error = "Missing source or destination shared buffer for Python execution";
            }
            return false;
        }

        try
        {
            py::gil_scoped_acquire gil;
            const auto binding_started = std::chrono::steady_clock::now();
            ScopedActivePythonExecution active_python_execution(this);
            ScopedPythonHostBridgeBinding bridge_binding(&bridge_);
            bridge_.clearImages();
            bridge_.setCurrentRequestContext(request_id, params_snapshot);
            if (reset_cancel_at_start)
            {
                bridge_.setCancelled(false);
            }
            bridge_.resetProgressFeedback();
            bridge_.resetManualViewCommitTracking();

            RuntimeImageBinding src_binding;
            src_binding.buffer = source_buffer;
            src_binding.descriptor = source_buffer->descriptor();
            src_binding.writable = false;
            bridge_.bindImage("source", std::move(src_binding));

            RuntimeImageBinding dst_binding;
            dst_binding.buffer = destination_buffer;
            dst_binding.descriptor = destination_buffer->descriptor();
            dst_binding.writable = true;
            bridge_.bindImage(request_kind == "preview" ? "preview" : "destination", std::move(dst_binding));

            py::object image_type = afternight_module_.attr("ImageHandle");
            py::object view_type = afternight_module_.attr("ViewRef");
            py::object progress_type = afternight_module_.attr("Progress");

            py::object src_image = image_type("source");
            py::object dst_image = image_type(request_kind == "preview" ? "preview" : "destination");
            const auto build_image_dict = [&](const char* collection_name, const auto& predicate,
                                              const bool writable) -> py::object {
                if (!auxiliary_buffers)
                {
                    return py::none();
                }

                py::dict images;
                std::unordered_set<std::string> seen_names;
                for (auto& buffer : *auxiliary_buffers)
                {
                    const auto& descriptor = buffer.descriptor();
                    if (!predicate(descriptor))
                    {
                        continue;
                    }

                    const std::string& symbolic_name = descriptor.symbolic_name;
                    if (!seen_names.insert(symbolic_name).second)
                    {
                        throw std::runtime_error(std::string("Duplicate ") + collection_name +
                                                 " symbolic name: " + symbolic_name);
                    }

                    RuntimeImageBinding binding;
                    binding.buffer = &buffer;
                    binding.descriptor = descriptor;
                    binding.writable = writable;

                    const std::string token = std::string(collection_name) + ":" + descriptor.buffer_id;
                    bridge_.bindImage(token, std::move(binding));
                    images[py::str(symbolic_name)] = image_type(token);
                }

                if (py::len(images) == 0)
                {
                    return py::none();
                }
                return std::move(images);
            };

            py::object masks = build_image_dict(
                "mask",
                [](const an::ui::extensions::SharedBufferDescriptor& descriptor) {
                    return descriptor.buffer_role == an::ui::extensions::SharedBufferRole::Mask &&
                           descriptor.access_mode == an::ui::extensions::SharedBufferAccessMode::ReadOnly;
                },
                false);
            py::object weights = build_image_dict(
                "weight",
                [](const an::ui::extensions::SharedBufferDescriptor& descriptor) {
                    return descriptor.buffer_role == an::ui::extensions::SharedBufferRole::Weight &&
                           descriptor.access_mode == an::ui::extensions::SharedBufferAccessMode::ReadOnly;
                },
                false);
            py::object output_masks =
                request_kind == "preview"
                    ? py::none()
                    : build_image_dict(
                          "output_mask",
                          [](const an::ui::extensions::SharedBufferDescriptor& descriptor) {
                              return descriptor.buffer_role == an::ui::extensions::SharedBufferRole::Mask &&
                                     descriptor.access_mode == an::ui::extensions::SharedBufferAccessMode::ReadWrite;
                          },
                          true);
            py::dict params;
            for (auto it = params_snapshot.begin(); it != params_snapshot.end(); ++it)
            {
                params[py::str(it.key().toString().toStdString())] = cborValueToPythonObject(it.value());
            }
            params[py::str("_params_token")] = params_token;
            py::object view_ref =
                view_type(bridge_.currentViewId(), bridge_.currentViewName(), bridge_.currentFileName());
            py::object progress = progress_type();

            const auto run_started = std::chrono::steady_clock::now();
            if (request_kind == "preview" && py::hasattr(extension_instance_, "execute_preview"))
            {
                extension_instance_.attr("execute_preview")(view_ref, src_image, dst_image, params, progress, masks,
                                                            weights);
            }
            else
            {
                extension_instance_.attr("execute")(view_ref, src_image, dst_image, params, progress, masks, weights,
                                                    output_masks);
            }

            const auto flush_started = std::chrono::steady_clock::now();
            last_request_cancelled_ = bridge_.currentRequestCancelled();
            bridge_.flushWritableMetadata();
            bridge_.clearImages();
            bridge_.clearCurrentRequest();
            if (spans != nullptr)
            {
                spans->bind = run_started - binding_started;
                spans->run = flush_started - run_started;
                spans->flush = std::chrono::steady_clock::now() - flush_started;
            }
            return true;
        }
        catch (const py::error_already_set& e)
        {
            const std::string message = reportPythonFailure(e);
            if (error)
            {
                *error = message;
            }
        }
        catch (const std::exception& e)
        {
            const std::string message = e.what();
            try
            {
                bridge_.logEvent("error", "extension.runtime", message);
            }
            catch (...)
            {
            }
            if (error)
            {
                *error = message;
            }
        }

        last_request_cancelled_ = bridge_.currentRequestCancelled();
        bridge_.clearImages();
        bridge_.clearCurrentRequest();
        return false;
    }

private:
    template <typename Invoker>
    bool callOptionalExtensionHook(const char* hook_name, std::string* error, Invoker&& invoker)
    {
        if (!hasExecutableExtension())
        {
            return true;
        }

        try
        {
            py::gil_scoped_acquire gil;
            ScopedPythonHostBridgeBinding bridge_binding(&bridge_);
            if (py::hasattr(extension_instance_, hook_name))
            {
                invoker(extension_instance_.attr(hook_name));
            }
            return true;
        }
        catch (const py::error_already_set& e)
        {
            const std::string message = reportPythonFailure(e);
            if (error)
            {
                *error = message;
            }
            return false;
        }
        catch (const std::exception& e)
        {
            const std::string message = e.what();
            try
            {
                bridge_.logEvent("error", "extension.runtime", message);
            }
            catch (...)
            {
            }
            if (error)
            {
                *error = message;
            }
            return false;
        }
    }

    [[nodiscard]] py::dict currentInputMetadataDict()
    {
        py::dict input = bridge_.currentViewInfo();
        input[py::str("view_id")] = py::str(bridge_.currentViewId());
        input[py::str("metadata")] = cborValueToPythonObject(QCborValue(bridge_.currentViewMetadata()));
        input[py::str("display_settings")] = cborValueToPythonObject(QCborValue(bridge_.currentViewDisplaySettings()));
        return input;
    }

    bool loadExtensionDefinition(const bool inspect_settings, const bool inspect_params, std::string* error)
    {
        try
        {
            py::gil_scoped_acquire gil;
            afternight_module_ = py::module_::import("afternight");
            py::module_ module = py::module_::import(entry_point_.c_str());
            if (process_class_.empty())
            {
                return true;
            }

            if (!py::hasattr(module, process_class_.c_str()))
            {
                if (error)
                {
                    *error = "The extension entry point does not export the requested process class";
                }
                return false;
            }

            extension_class_ = module.attr(process_class_.c_str());
            const bool is_process =
                PyObject_IsSubclass(extension_class_.ptr(), afternight_module_.attr("ProcessExtension").ptr()) == 1;
            const bool is_preview =
                PyObject_IsSubclass(extension_class_.ptr(), afternight_module_.attr("RTPreviewExtension").ptr()) == 1;
            const bool is_workflow =
                PyObject_IsSubclass(extension_class_.ptr(), afternight_module_.attr("WorkflowExtension").ptr()) == 1;
            is_previewable_ = is_preview;
            if (!is_process && !is_preview && !is_workflow)
            {
                if (error)
                {
                    *error = "The extension process class must inherit from an afternight base class";
                }
                return false;
            }

            if (inspect_settings || inspect_params)
            {
                ScopedPythonHostBridgeBinding bridge_binding(&bridge_);
                py::object settings = afternight_module_.attr("Settings")();
                py::object context = afternight_module_.attr("ExtensionContext")(settings);
                py::object instance = extension_class_(context);
                const auto inspect_schema = [&](const char* method_name, QJsonArray* destination) -> bool {
                    py::object schema = instance.attr(method_name)();
                    const QCborValue schema_value = pythonObjectToCborValue(schema);
                    if (!schema_value.isArray())
                    {
                        if (error)
                        {
                            *error = std::string(method_name) + "() must return a list";
                        }
                        return false;
                    }

                    const QJsonDocument document = QJsonDocument::fromVariant(schema_value.toVariant().toList());
                    if (!document.isArray())
                    {
                        if (error)
                        {
                            *error = std::string(method_name) + "() must be JSON-serializable";
                        }
                        return false;
                    }

                    *destination = document.array();
                    return true;
                };

                if (inspect_settings && !inspect_schema("get_settings_params", &settings_schema_))
                {
                    return false;
                }
                if (inspect_params && !inspect_schema("get_params", &params_schema_))
                {
                    return false;
                }
            }

            return true;
        }
        catch (const py::error_already_set& e)
        {
            const std::string message = reportPythonFailure(e);
            if (error)
            {
                *error = message;
            }
        }
        catch (const std::exception& e)
        {
            const std::string message = e.what();
            try
            {
                bridge_.logEvent("error", "extension.runtime", message);
            }
            catch (...)
            {
            }
            if (error)
            {
                *error = message;
            }
        }

        return false;
    }

    void releaseMainThreadGil() noexcept
    {
        if (python_initialized_ && Py_IsInitialized() && !main_thread_state_)
        {
            main_thread_state_ = PyEval_SaveThread();
        }
    }

    void restoreMainThreadGil() noexcept
    {
        if (main_thread_state_)
        {
            PyEval_RestoreThread(main_thread_state_);
            main_thread_state_ = nullptr;
        }
    }

    class ScopedActivePythonExecution
    {
    public:
        explicit ScopedActivePythonExecution(EmbeddedExtensionRuntime* runtime) noexcept : runtime_(runtime)
        {
            if (runtime_)
            {
                previous_thread_id_ =
                    runtime_->active_python_thread_id_.exchange(PyThread_get_thread_ident(), std::memory_order_acq_rel);
            }
        }

        ScopedActivePythonExecution(const ScopedActivePythonExecution&) = delete;
        ScopedActivePythonExecution& operator=(const ScopedActivePythonExecution&) = delete;

        ~ScopedActivePythonExecution()
        {
            if (runtime_)
            {
                runtime_->active_python_thread_id_.store(previous_thread_id_, std::memory_order_release);
            }
        }

    private:
        EmbeddedExtensionRuntime* runtime_ = nullptr;
        unsigned long previous_thread_id_ = 0;
    };

    /// Reports what an extension raised, once, and says it. A stop the person asked for reaches the extension's code
    /// as KeyboardInterrupt - that is how the host stops it - and is not a failure: it is logged at info level as the
    /// stop it is, without the traceback. Anything else is an error, logged with what Python said.
    ///
    /// @param error what the extension raised.
    /// @return what to say about it.
    [[nodiscard]] std::string reportPythonFailure(const py::error_already_set& error)
    {
        bool stopped = false;
        try
        {
            py::gil_scoped_acquire gil;
            stopped = error.matches(PyExc_KeyboardInterrupt) && bridge_.currentRequestCancelled();
        }
        catch (...)
        {
        }
        const std::string message =
            stopped ? std::string("The extension stopped, as the person asked.") : pyExceptionMessage(error);
        try
        {
            bridge_.logEvent(stopped ? "info" : "error", stopped ? "python.cancelled" : "python.exception", message);
        }
        catch (...)
        {
        }
        return message;
    }

    void interruptActivePythonExecution() noexcept
    {
        const unsigned long target_thread_id = active_python_thread_id_.load(std::memory_order_acquire);
        if (target_thread_id == 0 || !python_initialized_ || !Py_IsInitialized())
        {
            return;
        }

        try
        {
            py::gil_scoped_acquire gil;
            const int affected = PyThreadState_SetAsyncExc(target_thread_id, PyExc_KeyboardInterrupt);
            if (affected > 1)
            {
                PyThreadState_SetAsyncExc(target_thread_id, nullptr);
            }
        }
        catch (...)
        {
        }
    }

    bool python_initialized_ = false;
    an::ui::extensions::RuntimeBackendModule runtime_backend_;
    an::ui::extensions::HostEngineSession host_session_;
    an::ui::extensions::ExtensionAppContext* app_context_ = nullptr;
    PythonHostBridge bridge_;
    std::string entry_point_;
    std::string process_class_;
    py::object afternight_module_;
    py::object extension_class_;
    py::object extension_instance_;
    QJsonArray settings_schema_;
    QJsonArray params_schema_;
    bool is_previewable_ = false;
    std::atomic<bool> last_request_cancelled_{false};
    PyThreadState* main_thread_state_ = nullptr;
    std::atomic<unsigned long> active_python_thread_id_{0};

    /// Every run that binds images on the bridge - an execute, a preview frame, a parameter action - takes it in turn:
    /// the bridge holds one run's images at a time. Taken before the interpreter's lock, never while holding it.
    std::mutex execution_lane_;

    /// Where the native window's preview frames are traced, when the host was asked to trace them.
    an::ui::extensions::PreviewFrameTrace preview_trace_{previewFrameTracePath()};

    /// What the native window's frames are lent from, and the session it serves: taken by each frame's worker in turn.
    std::mutex preview_buffers_mutex_;
    std::shared_ptr<an::ui::extensions::PreviewFrameBufferPool> preview_buffers_;
    std::string preview_buffers_session_;
};

/// Runs the host's teardown when it goes out of scope.
class HostRuntimeTeardown
{
public:
    explicit HostRuntimeTeardown(std::function<void()> teardown) : teardown_(std::move(teardown)) {}

    ~HostRuntimeTeardown()
    {
        if (teardown_)
        {
            teardown_();
        }
    }

    HostRuntimeTeardown(const HostRuntimeTeardown&) = delete;
    HostRuntimeTeardown& operator=(const HostRuntimeTeardown&) = delete;

private:
    std::function<void()> teardown_;
};

class ScopedNativeProcessWindowBinding
{
public:
    explicit ScopedNativeProcessWindowBinding(EmbeddedExtensionRuntime* runtime) noexcept : runtime_(runtime) {}

    ScopedNativeProcessWindowBinding(const ScopedNativeProcessWindowBinding&) = delete;
    ScopedNativeProcessWindowBinding& operator=(const ScopedNativeProcessWindowBinding&) = delete;

    ScopedNativeProcessWindowBinding(ScopedNativeProcessWindowBinding&& other) noexcept : runtime_(other.runtime_)
    {
        other.runtime_ = nullptr;
    }

    ScopedNativeProcessWindowBinding& operator=(ScopedNativeProcessWindowBinding&& other) noexcept
    {
        if (this == &other)
        {
            return *this;
        }

        reset();
        runtime_ = other.runtime_;
        other.runtime_ = nullptr;
        return *this;
    }

    ~ScopedNativeProcessWindowBinding()
    {
        reset();
    }

    void reset() noexcept
    {
        if (runtime_)
        {
            runtime_->setNativeProcessWindow(nullptr);
            runtime_ = nullptr;
        }
    }

private:
    EmbeddedExtensionRuntime* runtime_ = nullptr;
};

class ScopedNativeProcessWindowExecutionLock
{
public:
    explicit ScopedNativeProcessWindowExecutionLock(an::ui::extensions::ExtensionNativeProcessWindow* window) noexcept
        : window_(window)
    {
        if (window_)
        {
            // The run takes the extension's lane on the GUI thread once the frame on its way has landed: the host's
            // loop pauses the frames and waits for that over its own turns before it takes the lock.
            window_->setExecutionInProgress(true);
        }
    }

    ScopedNativeProcessWindowExecutionLock(const ScopedNativeProcessWindowExecutionLock&) = delete;
    ScopedNativeProcessWindowExecutionLock& operator=(const ScopedNativeProcessWindowExecutionLock&) = delete;

    ~ScopedNativeProcessWindowExecutionLock()
    {
        if (window_)
        {
            window_->setExecutionInProgress(false);
        }
    }

private:
    an::ui::extensions::ExtensionNativeProcessWindow* window_ = nullptr;
};

/// Holds a native window's frames while the GUI thread runs the extension itself, and lets them go when it is done. It
/// waits for nothing: the holder runs its work through the window's `whenRunIdle`, once the frame on its way has
/// landed.
class ScopedNativeProcessWindowFramePause
{
public:
    explicit ScopedNativeProcessWindowFramePause(an::ui::extensions::ExtensionNativeProcessWindow* window) noexcept
        : window_(window)
    {
        if (window_)
        {
            window_->pausePreviews();
        }
    }

    ScopedNativeProcessWindowFramePause(const ScopedNativeProcessWindowFramePause&) = delete;
    ScopedNativeProcessWindowFramePause& operator=(const ScopedNativeProcessWindowFramePause&) = delete;

    ~ScopedNativeProcessWindowFramePause()
    {
        if (window_)
        {
            window_->resumePreviews();
        }
    }

private:
    an::ui::extensions::ExtensionNativeProcessWindow* window_ = nullptr;
};

/// A flag set for a scope: that a parameter action runs, that an Apply is being validated.
class ScopedFlag
{
public:
    explicit ScopedFlag(bool* flag) noexcept : flag_(flag)
    {
        *flag_ = true;
    }

    ScopedFlag(const ScopedFlag&) = delete;
    ScopedFlag& operator=(const ScopedFlag&) = delete;

    ~ScopedFlag()
    {
        *flag_ = false;
    }

private:
    bool* flag_;
};

/// Runs what binds the target chosen meanwhile when the scope ends - after the flags the scope set are cleared, so it
/// is declared before them.
class RebindTargetAfter
{
public:
    explicit RebindTargetAfter(const std::function<void()>& rebind) noexcept : rebind_(rebind) {}

    RebindTargetAfter(const RebindTargetAfter&) = delete;
    RebindTargetAfter& operator=(const RebindTargetAfter&) = delete;

    ~RebindTargetAfter()
    {
        if (rebind_)
        {
            rebind_();
        }
    }

private:
    const std::function<void()>& rebind_;
};

/// The image a native run starts from: what the window's source view shows - which a preview never writes - with the
/// view's names and the area asked for.
NativeSource nativeSourceOf(const an::ui::extensions::ExtensionNativeProcessWindow& window, std::optional<QRect> region)
{
    NativeSource source;
    source.image = window.sourceImage();
    if (ImageViewControl* view = window.sourceView())
    {
        source.name = view->getUniqueName();
        source.file_name = view->getFileName();
    }
    source.region = std::move(region);
    return source;
}

struct NativeExecuteRunResult
{
    std::string request_id;
    QCborMap params;
    bool workflow = false;
    bool ok = false;
    bool cancelled = false;
    bool manual_commit = false;
    OwnedAsset result_image;
    OwnedAsset committed_image;
    std::optional<an::ui::extensions::SharedBufferDescriptor> commit_descriptor;
    std::string error;
};

#ifdef _WIN32
bool readSessionSecretFromInheritedHandle(const QString& handle_text, std::string* out_secret, std::string* error)
{
    if (!out_secret)
    {
        if (error)
        {
            *error = "Inherited session-secret output is null";
        }
        return false;
    }

    bool handle_ok = false;
    const qulonglong raw_handle = handle_text.trimmed().toULongLong(&handle_ok, 0);
    if (!handle_ok || raw_handle == 0)
    {
        if (error)
        {
            *error = "Missing or invalid inherited session-secret handle";
        }
        return false;
    }

    HANDLE inherited_handle = reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(raw_handle));
    out_secret->clear();
    std::array<char, 128> buffer{};
    const auto close_inherited_handle = [&inherited_handle]() {
        if (inherited_handle && inherited_handle != INVALID_HANDLE_VALUE)
        {
            ::CloseHandle(inherited_handle);
            inherited_handle = INVALID_HANDLE_VALUE;
        }
    };

    while (true)
    {
        DWORD bytes_read = 0;
        if (!::ReadFile(inherited_handle, buffer.data(), static_cast<DWORD>(buffer.size()), &bytes_read, nullptr))
        {
            if (::GetLastError() == ERROR_BROKEN_PIPE)
            {
                break;
            }
            if (error)
            {
                *error = "Failed to read the inherited session secret";
            }
            close_inherited_handle();
            return false;
        }
        if (bytes_read == 0)
        {
            break;
        }

        out_secret->append(buffer.data(), static_cast<std::size_t>(bytes_read));
        if (out_secret->size() > 4096u)
        {
            if (error)
            {
                *error = "Inherited session secret exceeded the maximum supported size";
            }
            close_inherited_handle();
            return false;
        }
    }

    if (out_secret->empty())
    {
        if (error)
        {
            *error = "Missing session secret on the inherited pipe";
        }
        close_inherited_handle();
        return false;
    }

    close_inherited_handle();
    return true;
}
#else
// The parent process (ExtensionHostManager::launchSession) creates an
// anonymous pipe and dup2()s the read end to fd 3 before exec.  This is
// the well-known descriptor the extension host reads its one-time session
// secret from.  See ExtensionHostManager.cpp for the write side.
constexpr int kSessionSecretPipeDescriptor = 3;

bool readSessionSecretFromInheritedPipe(std::string* out_secret, std::string* error)
{
    if (!out_secret)
    {
        if (error)
        {
            *error = "Inherited session-secret output is null";
        }
        return false;
    }

    out_secret->clear();
    std::array<char, 128> buffer{};
    while (true)
    {
        const ssize_t bytes_read =
            ::read(kSessionSecretPipeDescriptor, buffer.data(), static_cast<unsigned long>(buffer.size()));
        if (bytes_read == 0)
        {
            break;
        }
        if (bytes_read < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }

            if (error)
            {
                *error = "Failed to read the inherited session secret";
            }
            return false;
        }

        out_secret->append(buffer.data(), static_cast<std::size_t>(bytes_read));
        if (out_secret->size() > 4096u)
        {
            if (error)
            {
                *error = "Inherited session secret exceeded the maximum supported size";
            }
            return false;
        }
    }

    if (out_secret->empty())
    {
        if (error)
        {
            *error = "Missing session secret on the inherited pipe";
        }
        return false;
    }

    return true;
}
#endif

// readFramedMessage is now provided by ExtensionProtocolFraming.h
using an::ui::extensions::readFramedMessage;

std::vector<std::string> splitCapabilities(const QString& text)
{
    std::vector<std::string> values;
    for (const auto& piece : text.split(',', Qt::SkipEmptyParts))
    {
        values.push_back(piece.trimmed().toStdString());
    }
    return values;
}

std::optional<an::ui::extensions::SharedBufferRole> parseSharedBufferRole(const QString& text)
{
    const QString normalized = text.trimmed().toLower();
    if (normalized == QStringLiteral("source"))
    {
        return an::ui::extensions::SharedBufferRole::Source;
    }
    if (normalized == QStringLiteral("destination"))
    {
        return an::ui::extensions::SharedBufferRole::Destination;
    }
    if (normalized == QStringLiteral("preview"))
    {
        return an::ui::extensions::SharedBufferRole::Preview;
    }
    if (normalized == QStringLiteral("mask"))
    {
        return an::ui::extensions::SharedBufferRole::Mask;
    }
    if (normalized == QStringLiteral("weight"))
    {
        return an::ui::extensions::SharedBufferRole::Weight;
    }

    return std::nullopt;
}

bool writeHello(QLocalSocket& socket, const an::ui::extensions::RuntimeHelloMessage& hello, std::string* error)
{
    const QByteArray framed_hello =
        an::ui::extensions::frameProtocolMessage(an::ui::extensions::toProtocolMessage(hello), error);
    if (framed_hello.isEmpty() && error && !error->empty())
    {
        return false;
    }

    if (socket.write(framed_hello) != framed_hello.size() || !socket.waitForBytesWritten(5000))
    {
        if (error)
        {
            *error = "Failed to write runtime.hello to the host";
        }
        return false;
    }

    return true;
}

bool writePong(QLocalSocket& socket, const an::ui::extensions::RuntimePongMessage& pong, std::string* error)
{
    const QByteArray framed_pong =
        an::ui::extensions::frameProtocolMessage(an::ui::extensions::toProtocolMessage(pong), error);
    if (framed_pong.isEmpty() && error && !error->empty())
    {
        return false;
    }

    if (socket.write(framed_pong) != framed_pong.size() || !socket.waitForBytesWritten(5000))
    {
        if (error)
        {
            *error = "Failed to write runtime.pong to the host";
        }
        return false;
    }

    return true;
}

bool writeExecutionResult(QLocalSocket& socket, const an::ui::extensions::ExecutionResultMessage& result,
                          std::string* error)
{
    const QByteArray framed_result =
        an::ui::extensions::frameProtocolMessage(an::ui::extensions::toProtocolMessage(result), error);
    if (framed_result.isEmpty() && error && !error->empty())
    {
        return false;
    }

    if (socket.write(framed_result) != framed_result.size() || !socket.waitForBytesWritten(5000))
    {
        if (error)
        {
            *error = "Failed to write execution.result to the host";
        }
        return false;
    }

    return true;
}

bool writeProtocolEnvelope(QLocalSocket& socket, const an::ui::extensions::ExtensionProtocolMessage& message,
                           std::string* error)
{
    const QByteArray framed_message = an::ui::extensions::frameProtocolMessage(message, error);
    if (framed_message.isEmpty() && error && !error->empty())
    {
        return false;
    }

    if (socket.write(framed_message) != framed_message.size() || !socket.waitForBytesWritten(5000))
    {
        if (error)
        {
            *error = "Failed to write a protocol message to the host";
        }
        return false;
    }

    return true;
}

/// Sends what is still queued on the socket before the host lets it go. A write is on its way until the socket has
/// sent it, and a socket closed or destroyed first - or a process that ends first - drops it: the window's close sent
/// last would never reach the application, which then ends the session as a lost connection and discards the result
/// the window committed as it closed. On Windows, where a pipe write completes later, that happened whenever the
/// machine was busy. Each wait has the bound every message write has.
void sendQueuedWrites(QLocalSocket& socket)
{
    while (socket.state() == QLocalSocket::ConnectedState && socket.bytesToWrite() > 0 &&
           socket.waitForBytesWritten(5000))
    {
    }
}

using an::ui::extensions::generateOpaqueToken;

an::ui::extensions::ExtensionProtocolMessage makeUiConfirmDemoRequest(const std::string& session_id)
{
    an::ui::extensions::ExtensionProtocolMessage message;
    message.type = "ui.confirm";
    message.session_id = session_id;
    message.request_id = generateOpaqueToken();
    message.fields = QCborMap{{QStringLiteral("title"), QStringLiteral("Replace Preview")},
                              {QStringLiteral("message"), QStringLiteral("Overwrite the active preview image?")},
                              {QStringLiteral("warn"), true}};
    return message;
}

bool isResponseEnvelope(const an::ui::extensions::ExtensionProtocolMessage& message)
{
    const QString type = QString::fromStdString(message.type);
    return type.endsWith(QStringLiteral(".result")) || type.endsWith(QStringLiteral(".error"));
}

void printError(const std::string& message)
{
    std::fprintf(stderr, "%s\n", message.c_str());
}

void printJson(const QJsonObject& object)
{
    const QByteArray payload = QJsonDocument(object).toJson(QJsonDocument::Compact);
    std::fwrite(payload.constData(), 1, static_cast<std::size_t>(payload.size()), stdout);
    std::fputc('\n', stdout);
}

void printSettingsActionUpdate(const QCborMap& update)
{
    const QJsonDocument document = QJsonDocument::fromVariant(QCborValue(update).toVariant());
    if (!document.isObject())
    {
        return;
    }

    const QByteArray payload = document.toJson(QJsonDocument::Compact);
    std::fwrite(kSettingsActionUpdatePrefix, 1, std::strlen(kSettingsActionUpdatePrefix), stderr);
    std::fwrite(payload.constData(), 1, static_cast<std::size_t>(payload.size()), stderr);
    std::fputc('\n', stderr);
    std::fflush(stderr);
}

std::optional<std::filesystem::path>
sharedBufferRootForDescriptor(const an::ui::extensions::SharedBufferDescriptor& descriptor)
{
    if (descriptor.mapped_file_path.empty())
    {
        return std::nullopt;
    }

    const std::filesystem::path file_path = std::filesystem::path(descriptor.mapped_file_path).lexically_normal();
    const auto session_path = file_path.parent_path();
    const auto root_path = session_path.parent_path();
    if (session_path.empty() || root_path.empty())
    {
        return std::nullopt;
    }

    return root_path;
}

struct PreparedExecutionBuffers
{
    an::ui::extensions::SharedImageBuffer source_buffer;
    an::ui::extensions::SharedImageBuffer output_buffer;
    std::vector<an::ui::extensions::SharedImageBuffer> auxiliary_buffers;
};

bool prepareExecutionBuffersForRequest(const std::string& session_id, const std::string& request_kind,
                                       const std::vector<an::ui::extensions::SharedBufferDescriptor>& input_buffers,
                                       const std::optional<an::ui::extensions::SharedBufferRole>& forced_buffer_role,
                                       PreparedExecutionBuffers* prepared_buffers, std::string* error)
{
    if (input_buffers.empty())
    {
        return true;
    }

    if (prepared_buffers)
    {
        *prepared_buffers = PreparedExecutionBuffers{};
    }

    const auto source_it = std::find_if(
        input_buffers.begin(), input_buffers.end(), [](const an::ui::extensions::SharedBufferDescriptor& descriptor) {
            return descriptor.buffer_role == an::ui::extensions::SharedBufferRole::Source;
        });
    if (source_it == input_buffers.end())
    {
        return true;
    }

    const auto root_path = sharedBufferRootForDescriptor(*source_it);
    if (!root_path.has_value())
    {
        if (error)
        {
            *error = "Execution source buffer descriptor did not expose a usable managed root";
        }
        return false;
    }

    an::ui::extensions::SharedBufferManager manager(*root_path);
    an::ui::extensions::SharedImageBuffer source_buffer;
    if (!openSharedBuffer(manager, *source_it, &source_buffer, error))
    {
        return false;
    }

    an::ui::extensions::SharedImageBufferSpec output_spec;
    output_spec.sessionId = session_id;
    output_spec.bufferId = source_it->buffer_id + "." + request_kind + ".output";
    output_spec.symbolicName = request_kind == "preview" ? "preview" : "destination";
    output_spec.width = source_it->width;
    output_spec.height = source_it->height;
    output_spec.channels = source_it->channels;
    output_spec.pixelType = source_it->pixel_type;
    output_spec.pixelFormat = source_it->pixel_format;
    output_spec.colorSpace = source_it->color_space;
    output_spec.accessMode = an::ui::extensions::SharedBufferAccessMode::ReadWrite;
    output_spec.bufferRole = request_kind == "preview" ? an::ui::extensions::SharedBufferRole::Preview
                                                       : an::ui::extensions::SharedBufferRole::Destination;
    if (forced_buffer_role.has_value())
    {
        output_spec.bufferRole = *forced_buffer_role;
    }
    output_spec.offsetX = source_it->offset_x;
    output_spec.offsetY = source_it->offset_y;
    output_spec.sourceWidth = source_it->source_width;
    output_spec.sourceHeight = source_it->source_height;
    output_spec.metadataSidecar = source_it->metadata_sidecar;

    an::ui::extensions::SharedImageBuffer output_buffer;
    if (!allocateSharedBuffer(manager, output_spec, &output_buffer, error))
    {
        return false;
    }

    if (!source_buffer.valid() || !output_buffer.valid() || source_buffer.mappedSize() != output_buffer.mappedSize())
    {
        if (error)
        {
            *error = "Execution buffer transport could not create matching output mapping";
        }
        return false;
    }

    std::vector<an::ui::extensions::SharedImageBuffer> auxiliary_buffers;
    auxiliary_buffers.reserve(input_buffers.size());
    for (const auto& descriptor : input_buffers)
    {
        if (descriptor.buffer_role == an::ui::extensions::SharedBufferRole::Source)
        {
            continue;
        }
        if (descriptor.buffer_role == an::ui::extensions::SharedBufferRole::Mask &&
            descriptor.access_mode == an::ui::extensions::SharedBufferAccessMode::ReadWrite &&
            request_kind != "execute")
        {
            if (error)
            {
                *error = "Preview requests do not support writable output mask buffers";
            }
            return false;
        }
        if (descriptor.buffer_role == an::ui::extensions::SharedBufferRole::Weight &&
            descriptor.access_mode != an::ui::extensions::SharedBufferAccessMode::ReadOnly)
        {
            if (error)
            {
                *error = "Weight buffers must be read-only";
            }
            return false;
        }
        if (descriptor.buffer_role != an::ui::extensions::SharedBufferRole::Mask &&
            descriptor.buffer_role != an::ui::extensions::SharedBufferRole::Weight)
        {
            continue;
        }
        if (descriptor.symbolic_name.empty())
        {
            if (error)
            {
                *error = "Auxiliary shared buffer descriptors require a symbolic_name";
            }
            return false;
        }

        an::ui::extensions::SharedImageBuffer auxiliary_buffer;
        if (!openSharedBuffer(manager, descriptor, &auxiliary_buffer, error))
        {
            return false;
        }

        auxiliary_buffers.push_back(std::move(auxiliary_buffer));
    }

    std::memcpy(output_buffer.bytes(), source_buffer.bytes(), source_buffer.mappedSize());
    if (prepared_buffers)
    {
        prepared_buffers->source_buffer = std::move(source_buffer);
        prepared_buffers->output_buffer = std::move(output_buffer);
        prepared_buffers->auxiliary_buffers = std::move(auxiliary_buffers);
    }
    return true;
}

} // anonymous namespace

int main(int argc, char* argv[])
{
    configureQtPlatformForHeadlessRuntime();

    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("AfterNightExtensionHost"));
    app.setQuitOnLastWindowClosed(false);

    QCommandLineParser parser;
    parser.addHelpOption();

    const QCommandLineOption endpoint_option(QStringLiteral("endpoint-name"),
                                             QStringLiteral("QLocalServer endpoint name."), QStringLiteral("endpoint"));
    const QCommandLineOption session_option(QStringLiteral("session-id"),
                                            QStringLiteral("Prepared session identifier."), QStringLiteral("session"));
    const QCommandLineOption extension_option(QStringLiteral("extension-id"), QStringLiteral("Extension identifier."),
                                              QStringLiteral("extension"));
    const QCommandLineOption extension_package_root_option(
        QStringLiteral("extension-package-root"),
        QStringLiteral("Extension package root containing the Python entry point."), QStringLiteral("path"));
    const QCommandLineOption entry_point_option(QStringLiteral("entry-point"),
                                                QStringLiteral("Python module entry point to import."),
                                                QStringLiteral("module"));
    const QCommandLineOption process_class_option(QStringLiteral("process-class"),
                                                  QStringLiteral("Extension process class to resolve."),
                                                  QStringLiteral("class"));
    const QCommandLineOption environment_root_option(QStringLiteral("environment-root"),
                                                     QStringLiteral("Resolved extension environment root."),
                                                     QStringLiteral("path"));
    const QCommandLineOption runtime_root_option(
        QStringLiteral("runtime-root"), QStringLiteral("Bundled Python runtime root."), QStringLiteral("path"));
    const QCommandLineOption inspect_option(
        QStringLiteral("inspect"),
        QStringLiteral("Bootstrap the runtime and validate the extension import without opening a control channel."));
    const QCommandLineOption standalone_option(
        QStringLiteral("standalone"),
        QStringLiteral("Run without a host control socket for extension development and smoke tests."));
    const QCommandLineOption inspect_settings_option(
        QStringLiteral("inspect-settings"), QStringLiteral("Call get_settings_params() during --inspect validation."));
    const QCommandLineOption inspect_params_option(QStringLiteral("inspect-params"),
                                                   QStringLiteral("Call get_params() during --inspect validation."));
    const QCommandLineOption cached_params_schema_option(
        QStringLiteral("cached-param-schema"),
        QStringLiteral("Base64url-encoded cached get_params() schema for a managed live session."),
        QStringLiteral("schema"));
    const QCommandLineOption inspect_settings_snapshot_option(
        QStringLiteral("inspect-settings-snapshot-cbor-base64"),
        QStringLiteral("Base64url-encoded CBOR map of persisted extension settings for inspect mode."),
        QStringLiteral("snapshot"));
    const QCommandLineOption settings_action_option(
        QStringLiteral("settings-action"),
        QStringLiteral(
            "Call handle_settings_action(action_id, settings_snapshot) with a CBOR snapshot read from stdin."),
        QStringLiteral("action"));
    const QCommandLineOption backend_option(QStringLiteral("sdk-backend"), QStringLiteral("Resolved SDK backend."),
                                            QStringLiteral("backend"), QStringLiteral("runtime"));
    const QCommandLineOption launch_mode_option(QStringLiteral("launch-mode"), QStringLiteral("Extension launch mode."),
                                                QStringLiteral("mode"), QStringLiteral("single_image"));
    const QCommandLineOption display_name_option(
        QStringLiteral("display-name"), QStringLiteral("Human-readable process display name."), QStringLiteral("name"));
    const QCommandLineOption host_version_option(QStringLiteral("host-version"), QStringLiteral("Host version string."),
                                                 QStringLiteral("version"), QStringLiteral("dev"));
    const QCommandLineOption capabilities_option(QStringLiteral("capabilities"),
                                                 QStringLiteral("Comma-separated capability list."),
                                                 QStringLiteral("capabilities"));
    const QCommandLineOption hello_protocol_option(QStringLiteral("hello-protocol-version"),
                                                   QStringLiteral("Override runtime.hello protocol_version."),
                                                   QStringLiteral("version"));
    const QCommandLineOption hello_sdk_version_option(QStringLiteral("hello-sdk-version"),
                                                      QStringLiteral("Override runtime.hello sdk_version."),
                                                      QStringLiteral("version"));
    const QCommandLineOption hello_backend_option(QStringLiteral("hello-sdk-backend"),
                                                  QStringLiteral("Override runtime.hello sdk_backend."),
                                                  QStringLiteral("backend"));
    const QCommandLineOption hello_secret_option(QStringLiteral("hello-secret-proof"),
                                                 QStringLiteral("Override runtime.hello session_secret_proof."),
                                                 QStringLiteral("proof"));
#ifdef _WIN32
    const QCommandLineOption session_secret_handle_option(
        QStringLiteral("session-secret-handle"), QStringLiteral("Inherited handle value for the session secret pipe."),
        QStringLiteral("handle"));
#endif
    const QCommandLineOption duplicate_connect_option(
        QStringLiteral("duplicate-connect"),
        QStringLiteral("Open a second control-channel connection before authentication."));
    const QCommandLineOption stay_alive_option(QStringLiteral("stay-alive-ms"),
                                               QStringLiteral("Keep the control channel alive after handshake."),
                                               QStringLiteral("ms"), QStringLiteral("0"));
    const QCommandLineOption ignore_pings_option(QStringLiteral("ignore-pings"),
                                                 QStringLiteral("Ignore runtime.ping heartbeats instead of replying."));
    const QCommandLineOption disconnect_after_ack_option(
        QStringLiteral("disconnect-after-ack-ms"),
        QStringLiteral("Disconnect the control channel after the given delay."), QStringLiteral("ms"));
    const QCommandLineOption crash_after_ack_option(
        QStringLiteral("crash-after-ack-ms"),
        QStringLiteral("Abort the extension host after the given post-handshake delay."), QStringLiteral("ms"));
    const QCommandLineOption runtime_closed_crash_after_ack_option(
        QStringLiteral("runtime-closed-crash-after-ack-ms"),
        QStringLiteral("Send runtime.closed, then abort the extension host after the given post-handshake delay."),
        QStringLiteral("ms"));
    const QCommandLineOption close_after_view_commit_option(
        QStringLiteral("close-after-view-commit"),
        QStringLiteral("After committing an execute's result, close the session at once, as a native window that "
                       "does not keep open closes after its Apply."));
    const QCommandLineOption interrupt_execute_after_option(
        QStringLiteral("interrupt-execute-after-ms"),
        QStringLiteral("Stop each execute and preview the given time after it starts, as the process window's Cancel "
                       "stops one."),
        QStringLiteral("ms"), QStringLiteral("-1"));
    const QCommandLineOption execute_delay_option(QStringLiteral("execute-delay-ms"),
                                                  QStringLiteral("Delay before replying to execution.execute."),
                                                  QStringLiteral("ms"), QStringLiteral("0"));
    const QCommandLineOption preview_delay_option(QStringLiteral("preview-delay-ms"),
                                                  QStringLiteral("Delay before replying to execution.preview."),
                                                  QStringLiteral("ms"), QStringLiteral("0"));
    const QCommandLineOption emit_ui_confirm_demo_option(
        QStringLiteral("emit-ui-confirm-demo"), QStringLiteral("Send a demo ui.confirm request after handshake."));
    const QCommandLineOption view_commit_history_step_name_option(
        QStringLiteral("view-commit-history-step-name"),
        QStringLiteral("Override the history step name used for mirrored view.commit messages."),
        QStringLiteral("name"));
    const QCommandLineOption view_commit_history_step_description_option(
        QStringLiteral("view-commit-history-step-description"),
        QStringLiteral("Override the history step description used for mirrored view.commit messages."),
        QStringLiteral("description"));
    const QCommandLineOption result_buffer_role_option(QStringLiteral("result-buffer-role"),
                                                       QStringLiteral("Override the emitted output buffer role."),
                                                       QStringLiteral("role"));
    const QCommandLineOption ignore_cancel_option(QStringLiteral("ignore-cancel"),
                                                  QStringLiteral("Ignore execution.cancel requests."));
    const QCommandLineOption preview_frame_rate_option(
        QStringLiteral("preview-frame-rate"),
        QStringLiteral("The previews a second the native window shows while a control is dragged, as the "
                       "application's Processing page states it."),
        QStringLiteral("rate"));
    const QCommandLineOption preview_scaling_option(
        QStringLiteral("preview-scaling"),
        QStringLiteral("What a frame of a drag may run the extension's preview over, as its manifest states it: "
                       "full_only, decimated or native_region."),
        QStringLiteral("scaling"), QStringLiteral("full_only"));
    const QCommandLineOption preview_region_margin_option(
        QStringLiteral("preview-region-margin"),
        QStringLiteral("How far a native_region preview's answer at a pixel reads around it, in pixels."),
        QStringLiteral("pixels"), QStringLiteral("0"));
    parser.addOption(endpoint_option);
    parser.addOption(session_option);
    parser.addOption(extension_option);
    parser.addOption(extension_package_root_option);
    parser.addOption(entry_point_option);
    parser.addOption(process_class_option);
    parser.addOption(environment_root_option);
    parser.addOption(runtime_root_option);
    parser.addOption(inspect_option);
    parser.addOption(standalone_option);
    parser.addOption(inspect_settings_option);
    parser.addOption(inspect_params_option);
    parser.addOption(cached_params_schema_option);
    parser.addOption(inspect_settings_snapshot_option);
    parser.addOption(settings_action_option);
    parser.addOption(backend_option);
    parser.addOption(launch_mode_option);
    parser.addOption(display_name_option);
    parser.addOption(host_version_option);
    parser.addOption(capabilities_option);
    parser.addOption(hello_protocol_option);
    parser.addOption(hello_sdk_version_option);
    parser.addOption(hello_backend_option);
    parser.addOption(hello_secret_option);
#ifdef _WIN32
    parser.addOption(session_secret_handle_option);
#endif
    parser.addOption(duplicate_connect_option);
    parser.addOption(stay_alive_option);
    parser.addOption(ignore_pings_option);
    parser.addOption(disconnect_after_ack_option);
    parser.addOption(crash_after_ack_option);
    parser.addOption(runtime_closed_crash_after_ack_option);
    parser.addOption(close_after_view_commit_option);
    parser.addOption(execute_delay_option);
    parser.addOption(interrupt_execute_after_option);
    parser.addOption(preview_delay_option);
    parser.addOption(emit_ui_confirm_demo_option);
    parser.addOption(view_commit_history_step_name_option);
    parser.addOption(view_commit_history_step_description_option);
    parser.addOption(result_buffer_role_option);
    parser.addOption(ignore_cancel_option);
    parser.addOption(preview_frame_rate_option);
    parser.addOption(preview_scaling_option);
    parser.addOption(preview_region_margin_option);
    parser.process(app);

    const QString endpoint_name = parser.value(endpoint_option).trimmed();
    QString session_id = parser.value(session_option).trimmed();
    QString extension_id = parser.value(extension_option).trimmed();
    const QString display_name = parser.value(display_name_option).trimmed();
    const bool inspect_only = parser.isSet(inspect_option);
    const bool standalone_mode = parser.isSet(standalone_option);
    const QString settings_action_id = parser.value(settings_action_option).trimmed();
    const bool settings_action_mode = !settings_action_id.isEmpty();
    if (standalone_mode)
    {
        if (session_id.isEmpty())
        {
            session_id = QStringLiteral("standalone-session");
        }
        if (extension_id.isEmpty())
        {
            extension_id = QStringLiteral("standalone.extension");
        }
    }
    if (settings_action_mode)
    {
        if (session_id.isEmpty())
        {
            session_id = QStringLiteral("settings-action-session");
        }
        if (extension_id.isEmpty())
        {
            extension_id = QStringLiteral("settings.action");
        }
    }
    if (!inspect_only && !standalone_mode && !settings_action_mode &&
        (endpoint_name.isEmpty() || session_id.isEmpty() || extension_id.isEmpty()))
    {
        printError("Missing required extension host handshake arguments");
        return 2;
    }

    const auto session_paths = materializeExtensionSessionPaths(session_id.toStdString());
    an::ui::extensions::ExtensionAppContext extension_app_context;
    extension_app_context.setSessionDirectories(session_paths.temp_dir, session_paths.artifacts_dir);
    extension_app_context.setDialogCallbacks(makeExtensionDialogCallbacks());
    extension_app_context.setViewFactoryCallback(createHostLocalImageViewControlFromImage);
    // The native window's frames are paced at the rate the application's Processing page states, which the host's own
    // settings store holds from here on.
    if (parser.isSet(preview_frame_rate_option))
    {
        // A rate that is no whole number leaves the default rather than the range's lowest end.
        if (const std::optional<std::uint32_t> rate =
                an::ui::parsePreviewFrameRate(parser.value(preview_frame_rate_option));
            rate.has_value())
        {
            an::ui::writePreviewFrameRate(*rate, [&extension_app_context](const QString& key, const QString& value) {
                (void)extension_app_context.upsert(key, value);
            });
        }
        else
        {
            LOG_WARNING("Extension host: the preview frame rate '{}' is not a whole number; the default is kept",
                        parser.value(preview_frame_rate_option).toStdString());
        }
    }
    an::ui::setAppContext(&extension_app_context);
    ensureHostApplicationStyle();

    // Load icons and set window icon for the extension host process.
    {
        const bool dark_mode = an::ui::isDarkTheme();
        extension_app_context.reloadIcons(dark_mode);

        QIcon app_icon;
        app_icon.addFile(QStringLiteral(":/AfterNight/icons/PNG/afternight_icon_512x512_blue.png"));
        app_icon.addFile(QStringLiteral(":/AfterNight/icons/PNG/afternight_icon_256x256_blue.png"));
        app_icon.addFile(QStringLiteral(":/AfterNight/icons/PNG/afternight_icon_128x128_blue.png"));
        app_icon.addFile(QStringLiteral(":/AfterNight/icons/PNG/afternight_icon_64x64_blue.png"));
        app_icon.addFile(QStringLiteral(":/AfterNight/icons/PNG/afternight_icon_32x32_blue.png"));
        app_icon.addFile(QStringLiteral(":/AfterNight/icons/PNG/afternight_icon_16x16_blue.png"));
        app.setWindowIcon(app_icon);
    }

    const std::filesystem::path app_dir = executableDirectory();
    const std::filesystem::path extension_package_root =
        std::filesystem::path(parser.value(extension_package_root_option).trimmed().toStdString());
    const std::filesystem::path environment_root =
        std::filesystem::path(parser.value(environment_root_option).trimmed().toStdString());
    const std::filesystem::path runtime_root =
        parser.value(runtime_root_option).trimmed().isEmpty()
            ? defaultRuntimeRootForAppDir(app_dir)
            : std::filesystem::path(parser.value(runtime_root_option).trimmed().toStdString());
    const std::string entry_point = parser.value(entry_point_option).trimmed().toStdString();
    const std::string process_class = parser.value(process_class_option).trimmed().toStdString();
    const std::string sdk_backend = parser.value(backend_option).trimmed().toStdString();
    const std::string launch_mode = parser.value(launch_mode_option).trimmed().toStdString() == "workflow"
                                        ? std::string("workflow")
                                        : std::string("single_image");
    const bool is_workflow_session = launch_mode == "workflow";

    QCborMap inspect_settings_snapshot;
    const QString inspect_settings_snapshot_text = parser.value(inspect_settings_snapshot_option).trimmed();
    if (!inspect_settings_snapshot_text.isEmpty())
    {
        const QByteArray snapshot_payload =
            QByteArray::fromBase64(inspect_settings_snapshot_text.toLatin1(), QByteArray::Base64UrlEncoding);
        if (snapshot_payload.isEmpty())
        {
            printError("Inspect settings snapshot is invalid base64");
            return 2;
        }

        QCborParserError parser_error;
        const QCborValue snapshot_value = QCborValue::fromCbor(snapshot_payload, &parser_error);
        if (parser_error.error != QCborError::NoError || !snapshot_value.isMap())
        {
            printError("Inspect settings snapshot is invalid CBOR");
            return 2;
        }
        inspect_settings_snapshot = snapshot_value.toMap();
    }

    std::optional<QJsonArray> cached_params_schema;
    if (!inspect_only && !settings_action_mode && parser.isSet(cached_params_schema_option))
    {
        const QByteArray encoded_schema = parser.value(cached_params_schema_option).toLatin1();
        const QByteArray schema_payload = QByteArray::fromBase64(encoded_schema, QByteArray::Base64UrlEncoding);
        QJsonParseError parse_error;
        const QJsonDocument schema_document = QJsonDocument::fromJson(schema_payload, &parse_error);
        if (schema_payload.isEmpty() || parse_error.error != QJsonParseError::NoError || !schema_document.isArray())
        {
            printError("Cached extension parameter schema is invalid");
            return 2;
        }
        cached_params_schema = schema_document.array();
    }
    if (settings_action_mode)
    {
        QFile stdin_file;
        if (!stdin_file.open(stdin, QIODevice::ReadOnly))
        {
            printError("Failed to open stdin for settings action snapshot");
            return 2;
        }
        const QByteArray snapshot_payload = stdin_file.readAll();
        QCborParserError parser_error;
        const QCborValue snapshot_value = QCborValue::fromCbor(snapshot_payload, &parser_error);
        if (parser_error.error != QCborError::NoError || !snapshot_value.isMap())
        {
            printError("Settings action snapshot is invalid CBOR");
            return 2;
        }
        inspect_settings_snapshot = snapshot_value.toMap();
    }

    const bool headless_qt_platform = isHeadlessQtPlatform();
    const bool managed_live_session = !inspect_only && !standalone_mode && !settings_action_mode;
    const bool deferred_managed_bootstrap = managed_live_session &&
                                            qEnvironmentVariableIntValue("AFTERNIGHT_DEFER_EXTENSION_BOOTSTRAP") == 1 &&
                                            parser.value(stay_alive_option).trimmed().toInt() > 0;
    const auto resolved_process_display_name = [&]() {
        if (!display_name.isEmpty())
        {
            return display_name.toStdString();
        }
        if (!process_class.empty())
        {
            return process_class;
        }
        return extension_id.toStdString();
    };

    std::unique_ptr<an::ui::extensions::ExtensionNativeProcessWindow> native_process_window;

    EmbeddedExtensionRuntime extension_runtime;
    std::optional<ScopedNativeProcessWindowBinding> native_window_binding;
    // Declared after everything it takes down, so it runs first on every way out of main: the native windows and their
    // views go, then the runtime in the host's order, then the context.
    const HostRuntimeTeardown runtime_teardown([&]() {
        native_window_binding.reset();
        extension_runtime.shutdown([&]() {
            if (native_process_window)
            {
                // A frame's worker calls into the window: the frames are stopped - the extension's run with them - and
                // every worker has returned before the window goes, served by the event loop meanwhile, since a run
                // may be asking the GUI thread for something.
                native_process_window->stopPreviews();
                while (!QThreadPool::globalInstance()->waitForDone(10))
                {
                    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
                }
            }
            native_process_window.reset();
        });
        an::ui::clearAppContext();
    });
    extension_runtime.setAppContext(&extension_app_context);
    extension_runtime.setInspectSettingsSnapshot(inspect_settings_snapshot);
    extension_runtime.bridge()->setLaunchMode(launch_mode);
    extension_runtime.bridge()->setStandalone(standalone_mode);
    std::string extension_runtime_initialization_error;
    const auto initialize_extension_runtime = [&]() {
        if (environment_root.empty() && entry_point.empty() && !inspect_only)
        {
            return true;
        }

        // A fingerprinted startup cache can seed a native process window with
        // the schema discovered during install/update. Fall back to get_params()
        // when the cache is absent, or for explicit --inspect validation.
        const bool need_params = parser.isSet(inspect_params_option) ||
                                 (!inspect_only && !process_class.empty() && !cached_params_schema.has_value());

        if (extension_runtime.initialize(app_dir, runtime_root, environment_root, extension_package_root, entry_point,
                                         process_class, sdk_backend, parser.isSet(inspect_settings_option), need_params,
                                         cached_params_schema, &extension_runtime_initialization_error))
        {
            return true;
        }
        return false;
    };

    // Inspect, settings-action, and standalone modes have no live host to
    // authenticate with, so bootstrap them immediately. Managed sessions do
    // this only after the lightweight control-channel handshake below.
    if ((!managed_live_session || !deferred_managed_bootstrap) && !initialize_extension_runtime())
    {
        printError(extension_runtime_initialization_error);
        return 3;
    }

    if (settings_action_mode)
    {
        QCborMap action_result;
        std::string action_error;
        if (!extension_runtime.runSettingsAction(settings_action_id.toStdString(), inspect_settings_snapshot,
                                                 &action_result, &action_error, printSettingsActionUpdate))
        {
            printError(action_error.empty() ? std::string("Extension settings action failed") : action_error);
            return 13;
        }

        const QJsonDocument document = QJsonDocument::fromVariant(QCborValue(action_result).toVariant());
        if (!document.isObject())
        {
            printError("Extension settings action result is not JSON-serializable");
            return 13;
        }
        printJson(document.object());
        return 0;
    }

    if (inspect_only)
    {
        QJsonObject inspect_result;
        QJsonObject ui_runtime;
        ui_runtime.insert(QStringLiteral("application_class"),
                          QString::fromLatin1(QCoreApplication::instance()->metaObject()->className()));
        ui_runtime.insert(QStringLiteral("has_app_context"), an::ui::appContext() != nullptr);
        ui_runtime.insert(QStringLiteral("has_proxy_style"), an::ui::installedAnProxyStyle() != nullptr);
        ui_runtime.insert(QStringLiteral("style_class"), currentApplicationStyleClassName());
        ui_runtime.insert(QStringLiteral("temp_dir"), extension_app_context.getTempDirPath());
        ui_runtime.insert(QStringLiteral("artifacts_dir"), extension_app_context.artifactsDirPath());
        extension_runtime.inspectPySide6UiRuntime(&ui_runtime);
        inspect_result.insert(QStringLiteral("ui_runtime"), ui_runtime);
        if (parser.isSet(inspect_settings_option))
        {
            inspect_result.insert(QStringLiteral("settings_schema"), extension_runtime.inspectedSettingsSchema());
        }
        if (parser.isSet(inspect_params_option))
        {
            inspect_result.insert(QStringLiteral("params_schema"), extension_runtime.inspectedParamsSchema());
        }
        inspect_result.insert(QStringLiteral("display_name"), display_name);
        inspect_result.insert(QStringLiteral("preview_capable"), extension_runtime.hasPreviewableExtension());
        printJson(inspect_result);
        return 0;
    }

    if (standalone_mode)
    {
        QByteArray standalone_buffer;
        if (extension_runtime.hasLoadedExtensionDefinition())
        {
            std::string attach_error;
            extension_runtime.attachSessionSocket(nullptr, &standalone_buffer, session_id.toStdString(),
                                                  extension_id.toStdString(), &attach_error);
            if (!attach_error.empty())
            {
                printError(attach_error);
                return 13;
            }

            std::string launch_error;
            if (!extension_runtime.notifyProcessLaunch(&launch_error))
            {
                printError("Extension on_process_launch() failed: " + launch_error);
                return 13;
            }
        }

        const int stay_alive_ms = (std::max)(0, parser.value(stay_alive_option).trimmed().toInt());
        if (stay_alive_ms > 0)
        {
            QTimer::singleShot(stay_alive_ms, &app, &QCoreApplication::quit);
            return app.exec();
        }
        return 0;
    }

    std::string session_secret;
    std::string session_secret_error;
#ifndef _WIN32
    if (!readSessionSecretFromInheritedPipe(&session_secret, &session_secret_error))
    {
        printError(session_secret_error);
        return 4;
    }
#else
    if (!readSessionSecretFromInheritedHandle(parser.value(session_secret_handle_option), &session_secret,
                                              &session_secret_error))
    {
        printError(session_secret_error);
        return 4;
    }
#endif

    QLocalSocket socket;
    socket.connectToServer(endpoint_name);
    if (!socket.waitForConnected(5000))
    {
        printError("Failed to connect to the prepared extension host endpoint");
        return 5;
    }

    an::ui::extensions::RuntimeHelloMessage hello;
    hello.extension_id = extension_id.toStdString();
    hello.session_id = session_id.toStdString();
    hello.protocol_version = parser.isSet(hello_protocol_option) ? parser.value(hello_protocol_option).trimmed().toInt()
                                                                 : an::ui::extensions::kExtensionProtocolVersion;
    hello.sdk_version = parser.isSet(hello_sdk_version_option)
                            ? parser.value(hello_sdk_version_option).trimmed().toInt()
                            : an::ui::extensions::kExtensionSdkVersion;
    hello.sdk_backend = parser.isSet(hello_backend_option) ? parser.value(hello_backend_option).trimmed().toStdString()
                                                           : parser.value(backend_option).trimmed().toStdString();
    hello.host_version = parser.value(host_version_option).trimmed().toStdString();
    hello.capabilities = splitCapabilities(parser.value(capabilities_option));
    hello.session_secret_proof =
        parser.isSet(hello_secret_option) ? parser.value(hello_secret_option).trimmed().toStdString() : session_secret;
    // Managed single-image sessions always use the
    // extension-host-owned native process window path, including in headless
    // test environments.
    hello.native_process_window = true;
    hello.bootstrap_status_messages = deferred_managed_bootstrap;

    QLocalSocket duplicate_socket;
    if (parser.isSet(duplicate_connect_option))
    {
        duplicate_socket.connectToServer(endpoint_name);
        if (!duplicate_socket.waitForConnected(5000))
        {
            printError("Failed to connect the duplicate extension-host client");
            return 6;
        }
    }

    std::string error;
    if (!writeHello(socket, hello, &error))
    {
        printError(error);
        return 7;
    }
    if (duplicate_socket.state() == QLocalSocket::ConnectedState && !writeHello(duplicate_socket, hello, &error))
    {
        printError(error);
        return 8;
    }

    an::ui::extensions::ExtensionProtocolMessage ack_message;
    QByteArray handshake_leftover;
    if (!readFramedMessage(&socket, 5000, &ack_message, &error, &handshake_leftover))
    {
        printError(error);
        return 9;
    }

    an::ui::extensions::RuntimeHelloAckMessage ack;
    if (!an::ui::extensions::runtimeHelloAckFromProtocolMessage(ack_message, &ack, &error))
    {
        printError(error);
        return 10;
    }

    if (!ack.accepted || ack.session_id != hello.session_id)
    {
        std::string message = "Host rejected the extension host handshake";
        if (ack.error.has_value())
        {
            message += ": " + ack.error->message;
            if (!ack.error->details.empty())
            {
                message += " (" + ack.error->details + ")";
            }
        }
        printError(message);
        return 11;
    }

    const auto write_runtime_lifecycle = [&](const std::string_view type, const std::string_view detail = {}) {
        an::ui::extensions::ExtensionProtocolMessage message;
        message.type = std::string(type);
        message.session_id = hello.session_id;
        if (!detail.empty())
        {
            message.fields.insert(QStringLiteral("detail"), QString::fromUtf8(detail.data(), detail.size()));
        }

        std::string lifecycle_error;
        if (!writeProtocolEnvelope(socket, message, &lifecycle_error))
        {
            printError(lifecycle_error);
            return false;
        }
        return true;
    };
    const auto write_runtime_bootstrap_failure = [&](const std::string_view detail) {
        return !ack.bootstrap_status_messages || write_runtime_lifecycle("runtime.bootstrap_failed", detail);
    };

    if (deferred_managed_bootstrap && !initialize_extension_runtime())
    {
        write_runtime_bootstrap_failure(extension_runtime_initialization_error);
        printError(extension_runtime_initialization_error);
        return 3;
    }

    // Complete the native process window binding when the host confirms the mode.
    if (ack.native_process_window && !headless_qt_platform)
    {
        if (!native_process_window)
        {
            native_process_window = std::make_unique<an::ui::extensions::ExtensionNativeProcessWindow>(
                resolved_process_display_name(), hello.session_id, nullptr);
        }
        if (!native_window_binding.has_value())
        {
            extension_runtime.setNativeProcessWindow(native_process_window.get());
            native_window_binding.emplace(&extension_runtime);
        }

        const bool keep_open =
            std::find(hello.capabilities.begin(), hello.capabilities.end(), "keep_open") != hello.capabilities.end();

        // Hide the view area for non-preview processes so they show only the
        // parameter panel and action buttons without reserving space for a
        // source image that is never interactively previewed.
        if (!extension_runtime.hasPreviewableExtension())
        {
            native_process_window->setViewAreaVisible(false);
        }

        // What a frame of a drag may run the extension over is its manifest's to say; the whole image by default.
        const std::optional<an::ui::extensions::ExtensionPreviewScaling> declared_scaling =
            an::ui::extensions::extensionPreviewScalingFromName(
                parser.value(preview_scaling_option).trimmed().toLower().toStdString());
        const std::uint32_t region_margin = static_cast<std::uint32_t>(
            std::clamp(parser.value(preview_region_margin_option).trimmed().toInt(), 0,
                       static_cast<int>(an::ui::extensions::kMaxExtensionPreviewRegionMargin)));
        switch (declared_scaling.value_or(an::ui::extensions::ExtensionPreviewScaling::FullOnly))
        {
        case an::ui::extensions::ExtensionPreviewScaling::Decimated:
            native_process_window->setPreviewScaling(an::ui::PreviewScaling::Decimated);
            break;
        case an::ui::extensions::ExtensionPreviewScaling::NativeRegion:
            native_process_window->setPreviewScaling(an::ui::PreviewScaling::NativeRegion, region_margin);
            break;
        case an::ui::extensions::ExtensionPreviewScaling::FullOnly:
            native_process_window->setPreviewScaling(an::ui::PreviewScaling::FullOnly);
            break;
        }

        // Configure button bar mode based on the keep_open capability.
        // When keep_open is true, "Apply" resets params and stays open.
        native_process_window->setKeepOpenMode(keep_open);
    }

    const int stay_alive_ms = (std::max)(0, parser.value(stay_alive_option).trimmed().toInt());
    const bool ignore_pings = parser.isSet(ignore_pings_option);
    const int disconnect_after_ack_ms = parser.isSet(disconnect_after_ack_option)
                                            ? (std::max)(0, parser.value(disconnect_after_ack_option).trimmed().toInt())
                                            : -1;
    const auto forced_result_buffer_role = parser.isSet(result_buffer_role_option)
                                               ? parseSharedBufferRole(parser.value(result_buffer_role_option))
                                               : std::nullopt;
    if (parser.isSet(result_buffer_role_option) && !forced_result_buffer_role.has_value())
    {
        write_runtime_bootstrap_failure("Unknown result buffer role");
        printError("Unknown result buffer role");
        return 12;
    }

    const bool emit_ui_confirm_demo = parser.isSet(emit_ui_confirm_demo_option);
    const std::string view_commit_history_step_name_override =
        parser.value(view_commit_history_step_name_option).trimmed().toStdString();
    const std::string view_commit_history_step_description_override =
        parser.value(view_commit_history_step_description_option).trimmed().toStdString();
    const int crash_after_ack_ms = parser.isSet(crash_after_ack_option)
                                       ? (std::max)(0, parser.value(crash_after_ack_option).trimmed().toInt())
                                       : -1;
    const bool close_after_view_commit = parser.isSet(close_after_view_commit_option);
    const int runtime_closed_crash_after_ack_ms =
        parser.isSet(runtime_closed_crash_after_ack_option)
            ? (std::max)(0, parser.value(runtime_closed_crash_after_ack_option).trimmed().toInt())
            : -1;

    if (stay_alive_ms > 0)
    {
        if (emit_ui_confirm_demo)
        {
            if (!writeProtocolEnvelope(socket, makeUiConfirmDemoRequest(hello.session_id), &error))
            {
                write_runtime_bootstrap_failure(error);
                printError(error);
                return 13;
            }
        }

        // Seed the read buffer with any bytes the handshake consumed beyond the
        // ack message — without this, an execute message that arrives during the
        // handshake read would be silently discarded.
        QByteArray buffered = std::move(handshake_leftover);
        if (extension_runtime.hasLoadedExtensionDefinition())
        {
            std::string attach_error;
            extension_runtime.attachSessionSocket(&socket, &buffered, hello.session_id, hello.extension_id,
                                                  &attach_error);
            if (!attach_error.empty())
            {
                write_runtime_bootstrap_failure(attach_error);
                printError(attach_error);
                return 13;
            }

            if (ack.bootstrap_status_messages)
            {
                if (!write_runtime_lifecycle("runtime.bootstrap_state_ready"))
                {
                    return 13;
                }

                QElapsedTimer bootstrap_state_timer;
                bootstrap_state_timer.start();
                constexpr qint64 bootstrap_state_timeout_ms = 60000;
                bool bootstrap_state_complete = false;
                while (!bootstrap_state_complete && bootstrap_state_timer.elapsed() < bootstrap_state_timeout_ms)
                {
                    if (socket.state() != QLocalSocket::ConnectedState)
                    {
                        error = "The UI host disconnected during extension bootstrap state transfer";
                        break;
                    }
                    if (socket.bytesAvailable() == 0)
                    {
                        (void)socket.waitForReadyRead(50);
                    }
                    buffered.append(socket.readAll());

                    while (!bootstrap_state_complete)
                    {
                        an::ui::extensions::ExtensionProtocolMessage inbound;
                        std::string decode_error;
                        const auto decode_status =
                            an::ui::extensions::tryConsumeFramedProtocolMessage(&buffered, &inbound, &decode_error);
                        if (decode_status == an::ui::extensions::ProtocolFrameDecodeStatus::NeedMoreData)
                        {
                            break;
                        }
                        if (decode_status == an::ui::extensions::ProtocolFrameDecodeStatus::InvalidFrame)
                        {
                            error = decode_error.empty() ? "Invalid extension bootstrap state message" : decode_error;
                            break;
                        }
                        if (inbound.session_id != hello.session_id)
                        {
                            error = "Extension bootstrap state message used the wrong session id";
                            break;
                        }
                        if (inbound.type == "runtime.bootstrap_state_complete")
                        {
                            bootstrap_state_complete = true;
                            break;
                        }

                        bool handled_host_notification = false;
                        if (!extension_runtime.handleInboundHostNotification(inbound, &handled_host_notification,
                                                                             &error))
                        {
                            break;
                        }
                        if (!handled_host_notification && !isResponseEnvelope(inbound))
                        {
                            error = "Received unexpected control message during extension bootstrap state transfer";
                            break;
                        }
                    }
                    if (!error.empty())
                    {
                        break;
                    }
                    // The extension is constructed by now, and what its constructor queued on the loop - a timer
                    // that hides its process window, as the standalone template's does - runs here, so the bridge
                    // is bound for it as it is around the session loop's events.
                    {
                        ScopedPythonHostBridgeBinding bootstrap_bridge(extension_runtime.bridge());
                        QCoreApplication::processEvents(QEventLoop::AllEvents, 1);
                    }
                }

                if (!bootstrap_state_complete)
                {
                    if (error.empty())
                    {
                        error = "Timed out waiting for extension bootstrap state";
                    }
                    write_runtime_bootstrap_failure(error);
                    printError(error);
                    return 13;
                }
            }

            std::string launch_error;
            if (!extension_runtime.notifyProcessLaunch(&launch_error))
            {
                write_runtime_bootstrap_failure(launch_error);
                printError("Extension on_process_launch() failed: " + launch_error);
                return 13;
            }
            std::string input_error;
            if (!extension_runtime.notifyInputChanged(&input_error))
            {
                LOG_WARNING("Extension on_input_changed() failed: {}", input_error);
            }
        }
        QElapsedTimer lifetime;
        lifetime.start();
        bool disconnected_after_ack = false;
        // The request the application has in flight, and the window's frames held for its run.
        an::ui::extensions::HostInFlightExecution in_flight;
        const int execute_delay_ms = (std::max)(0, parser.value(execute_delay_option).trimmed().toInt());
        const int interrupt_execute_after_ms = parser.value(interrupt_execute_after_option).trimmed().toInt();
        const int preview_delay_ms = (std::max)(0, parser.value(preview_delay_option).trimmed().toInt());
        const bool ignore_cancel = parser.isSet(ignore_cancel_option);
        extension_runtime.setIgnoreCancel(ignore_cancel);
        bool native_window_closed = false;
        bool native_window_close_requested = false;
        bool native_async_execution_in_progress = false;
        bool native_execution_cancel_requested = false;
        bool native_execution_cancel_hook_notified = false;
        bool native_hard_shutdown_scheduled = false;
        bool native_closed_message_sent = false;
        bool native_process_close_notified = false;
        bool native_param_action_running = false;
        // The target being bound again, once the frame on its way has landed, and the latest one asked for meanwhile.
        bool native_target_rebinding = false;
        std::optional<std::string> native_pending_target;
        // Binds the target chosen meanwhile once the frame on its way has landed; set with the window's callbacks.
        std::function<void()> rebind_target_when_frames_land;
        bool runtime_closed_crash_emitted = false;
        // A frame of the native window's preview on its way - every frame holds the window's run in flight until its
        // result has landed on the GUI thread: its worker may be running the extension's preview.
        const auto native_preview_frames_running = [&]() {
            return native_process_window != nullptr && !native_process_window->isRunIdle();
        };
        const auto send_runtime_closed_message = [&](const bool active_work) {
            if (socket.state() != QLocalSocket::ConnectedState)
            {
                return;
            }

            an::ui::extensions::ExtensionProtocolMessage closed_message;
            closed_message.type = "runtime.closed";
            closed_message.session_id = hello.session_id;
            closed_message.request_id = generateOpaqueToken();
            closed_message.fields.insert(QStringLiteral("active_work"), QCborValue(active_work));
            std::string close_message_error;
            const QByteArray frame = an::ui::extensions::frameProtocolMessage(closed_message, &close_message_error);
            if (frame.isEmpty())
            {
                return;
            }

            socket.write(frame);
            socket.flush();
        };
        const auto send_runtime_closed_once = [&](const bool active_work) {
            if (native_closed_message_sent)
            {
                return;
            }

            native_closed_message_sent = true;
            send_runtime_closed_message(active_work);
        };
        const auto schedule_native_hard_shutdown = [&]() {
            if (native_hard_shutdown_scheduled)
            {
                return;
            }

            native_hard_shutdown_scheduled = true;
            QTimer::singleShot(0, qApp, [&socket]() {
                sendQueuedWrites(socket);
                std::_Exit(EXIT_SUCCESS);
            });
        };
        const auto request_native_window_shutdown = [&](const bool force_active_work) {
            native_window_close_requested = true;
            native_window_closed = true;
            extension_runtime.bridge()->setCancelled(true);
            if (native_process_window)
            {
                // Nothing on its way is shown any more, and the extension's run over the frame on its way stops.
                native_process_window->stopPreviews();
            }
            // The window's session ends here, and the host may end with it at once: what its frames keep goes now.
            extension_runtime.closePreviewFrameBuffers();

            const bool active_work =
                in_flight.active() || native_async_execution_in_progress || native_preview_frames_running();
            if (!active_work && !native_process_close_notified)
            {
                native_process_close_notified = true;
                std::string close_hook_error;
                if (!extension_runtime.notifyProcessClose("closed", &close_hook_error))
                {
                    LOG_WARNING("Extension on_process_close() failed: {}", close_hook_error);
                }
            }
            send_runtime_closed_once(active_work);

            if (force_active_work)
            {
                schedule_native_hard_shutdown();
            }
        };
        const auto notify_execution_cancelled_once = [&]() {
            if (native_execution_cancel_hook_notified)
            {
                return;
            }

            native_execution_cancel_hook_notified = true;
            std::string cancel_hook_error;
            if (!extension_runtime.notifyExecutionCancelled(&cancel_hook_error))
            {
                LOG_WARNING("Extension on_execution_cancelled() failed: {}", cancel_hook_error);
            }
        };
        const auto request_native_execution_cancel = [&]() {
            if (!native_async_execution_in_progress && !in_flight.active())
            {
                return;
            }

            native_execution_cancel_requested = true;
            extension_runtime.requestCurrentExecutionCancel();
            notify_execution_cancelled_once();
            if (native_process_window)
            {
                native_process_window->markExecutionCancelRequested();
                native_process_window->showProcessMessage(QStringLiteral("Cancelling"),
                                                          QStringLiteral("Waiting for the extension to stop..."),
                                                          TopLevelProcessBaseControl::ProcessMessageTone::Info);
            }
        };
        const auto validate_native_process = [&](const QCborMap& params, const QString& title) -> bool {
            QCborValue diagnostics;
            std::string validation_error;
            if (!extension_runtime.validateProcess(params, &diagnostics, &validation_error))
            {
                LOG_WARNING("Extension validate_process() failed: {}", validation_error);
                if (native_process_window)
                {
                    native_process_window->showProcessMessage(title, QString::fromStdString(validation_error),
                                                              TopLevelProcessBaseControl::ProcessMessageTone::Error);
                }
                return false;
            }

            if (const std::optional<QString> blocking_message = blockingValidationDiagnostic(diagnostics);
                blocking_message.has_value())
            {
                LOG_WARNING("Native process validation blocked execution: {}", blocking_message->toStdString());
                if (native_process_window)
                {
                    native_process_window->showProcessMessage(title, *blocking_message,
                                                              TopLevelProcessBaseControl::ProcessMessageTone::Error);
                }
                return false;
            }
            return true;
        };

        // Wire the native process window Execute button to run the Python
        // extension locally and send view.commit on success.
        if (native_process_window && extension_runtime.hasExecutableExtension())
        {
            native_process_window->setSettingsCallbacks(
                [&](const QString& key, const QCborValue& default_value) {
                    const QCborValue transient_value =
                        native_process_window->transientExternalToolSettingValue(key, QCborValue());
                    if (!transient_value.isUndefined())
                    {
                        return transient_value;
                    }
                    QCborValue value = default_value;
                    std::string settings_error;
                    if (!extension_runtime.bridge()->settingValue(key, default_value, &value, &settings_error))
                    {
                        LOG_WARNING("Failed to read extension setting '{}': {}", key.toStdString(), settings_error);
                    }
                    return value;
                },
                [&](const QString& key, const QCborValue& value, std::string* settings_error) {
                    return extension_runtime.bridge()->setSettingValue(key, value, settings_error);
                });
            native_process_window->setOpenSettingsCallback([&](std::string* settings_error) {
                try
                {
                    (void)extension_runtime.bridge()->hostProtocolRequest("ui.open_extension_settings");
                    return true;
                }
                catch (const std::exception& exc)
                {
                    if (settings_error)
                    {
                        *settings_error = exc.what();
                    }
                    return false;
                }
            });

            // The bridge's view is what a frame's run reads, on its worker: it is bound again only once the frames are
            // paused and the one on its way has landed, and never under a run the GUI thread makes itself - an action,
            // an Apply's validation, the Apply's run - which binds the target chosen meanwhile when it ends. The latest
            // target chosen is the one bound; one frame follows. Nothing here turns the event loop: what waits for the
            // frame on its way runs when it lands.
            rebind_target_when_frames_land = [&]() {
                if (!native_pending_target.has_value() || native_target_rebinding || native_param_action_running ||
                    native_async_execution_in_progress)
                {
                    return;
                }
                native_target_rebinding = true;
                native_process_window->pausePreviews();
                native_process_window->whenRunIdle([&]() {
                    while (native_pending_target.has_value())
                    {
                        const std::string target = *native_pending_target;
                        native_pending_target.reset();

                        std::string bind_error;
                        if (!extension_runtime.bridge()->bindView(target, &bind_error))
                        {
                            LOG_WARNING("Failed to bind native target view '{}': {}", target, bind_error);
                            continue;
                        }

                        std::string input_error;
                        if (!extension_runtime.notifyInputChanged(&input_error))
                        {
                            LOG_WARNING("Extension on_input_changed() failed: {}", input_error);
                        }
                    }
                    native_target_rebinding = false;
                    native_process_window->resumePreviews();
                });
            };

            // What the GUI thread runs on the extension once the frame on its way has landed - an action, an Apply - is
            // deferred one way: the frames are paused, `again` is made once the frame has landed, with the frames still
            // held, and the frames are let go and the target chosen meanwhile bound afterwards - whatever `again` then
            // did, since one it refuses binds nothing itself.
            const auto when_frames_landed = [&](std::function<void()> again) {
                native_param_action_running = true;
                native_process_window->pausePreviews();
                native_process_window->whenRunIdle([&, again = std::move(again)]() {
                    native_param_action_running = false;
                    again();
                    native_process_window->resumePreviews();
                    rebind_target_when_frames_land();
                });
            };

            // A parameter action: it reads the source as the frames do and binds it as they do, on the GUI thread, so
            // the frames are paused and the one on its way lands first - the action is run again then, with the frames
            // still held, and the window refuses a second click meanwhile - before the action takes the extension's
            // lane. One frame follows it.
            std::function<void(const std::string&, const QCborMap&,
                               an::ui::extensions::ExtensionNativeProcessWindow::ParamActionCompletion)>
                run_param_action;
            run_param_action = [&](const std::string& action_id, const QCborMap& params,
                                   an::ui::extensions::ExtensionNativeProcessWindow::ParamActionCompletion done) {
                if (in_flight.active() || native_async_execution_in_progress || native_param_action_running ||
                    native_target_rebinding)
                {
                    done(QCborMap{});
                    return;
                }
                if (!native_process_window->isRunIdle())
                {
                    when_frames_landed([&, action_id, params, done = std::move(done)]() mutable {
                        run_param_action(action_id, params, std::move(done));
                    });
                    return;
                }

                const RebindTargetAfter rebind_after(rebind_target_when_frames_land);
                const ScopedFlag action_running(&native_param_action_running);
                const ScopedNativeProcessWindowFramePause frames_paused(native_process_window.get());

                const NativeSource action_source = nativeSourceOf(*native_process_window, std::nullopt);

                const std::string req_id = generateOpaqueToken();
                QCborMap updates;
                std::string action_error;
                if (!extension_runtime.runNativeParamAction(hello.session_id, req_id, action_id, params, &updates,
                                                            &action_error, action_source))
                {
                    if (!native_window_close_requested && !extension_runtime.lastRequestCancelled())
                    {
                        LOG_WARNING("Native parameter action '{}' failed: {}", action_id, action_error);
                        native_process_window->showProcessMessage(
                            QStringLiteral("Action failed"), QString::fromStdString(action_error),
                            TopLevelProcessBaseControl::ProcessMessageTone::Error);
                    }
                    done(QCborMap{});
                    return;
                }

                const QString action_name = QString::fromStdString(action_id);
                if (action_name.contains(QStringLiteral("preset"), Qt::CaseInsensitive))
                {
                    QCborMap loaded_params = params;
                    for (auto it = updates.begin(); it != updates.end(); ++it)
                    {
                        loaded_params.insert(it.key(), it.value());
                    }
                    QCborMap preset_metadata;
                    preset_metadata.insert(QStringLiteral("action_id"), action_name);
                    preset_metadata.insert(QStringLiteral("source"), QStringLiteral("param_action"));
                    std::string preset_error;
                    if (!extension_runtime.notifyPresetLoaded(loaded_params, preset_metadata, &preset_error))
                    {
                        LOG_WARNING("Extension on_preset_loaded() failed: {}", preset_error);
                    }
                }

                extension_runtime.bridge()->resetProgressFeedback();
                native_process_window->clearProcessMessage();
                done(updates);
            };
            native_process_window->setParamActionCallback(run_param_action);

            native_process_window->setParamChangedCallback(
                [&](const QCborMap& params, const QStringList& changed_keys, const QString& reason) {
                    std::string params_error;
                    if (!extension_runtime.notifyParametersChanged(params, changed_keys, reason, &params_error))
                    {
                        LOG_WARNING("Extension on_parameters_changed() failed: {}", params_error);
                    }
                });

            std::function<void()> execute_native;
            execute_native = [&]() {
                // A parameter action waiting for the frames to land holds the lane: an Apply clicked meanwhile would
                // take the extension's lane on a worker and leave the action, which takes the lane on the GUI thread
                // next, blocked on it - and the run's own calls to the GUI thread blocked in turn.
                if (in_flight.active() || native_async_execution_in_progress || native_param_action_running ||
                    native_target_rebinding || socket.state() != QLocalSocket::ConnectedState)
                {
                    return;
                }
                // The extension is asked about the parameters with no frame of the window in it: a frame's run holds
                // the interpreter, which the GUI thread would wait for, and a validation between a frame's calls would
                // change what the extension keeps for that frame. The frame on its way lands first - the Apply is made
                // again then, with the frames still held, and the window refuses a second click meanwhile - and a
                // target chosen during the Apply is bound once it is done; one frame follows.
                if (!native_process_window->isRunIdle())
                {
                    when_frames_landed([&]() { execute_native(); });
                    return;
                }
                const RebindTargetAfter rebind_after(rebind_target_when_frames_land);

                const std::string req_id = generateOpaqueToken();
                const QCborMap execute_params = native_process_window->currentParams();
                bool valid = false;
                {
                    const ScopedFlag validating(&native_param_action_running);
                    const ScopedNativeProcessWindowFramePause frames_paused(native_process_window.get());
                    valid = validate_native_process(execute_params, QStringLiteral("Validation failed"));
                }
                if (!valid)
                {
                    return;
                }
                NativeSource exec_source;
                if (!is_workflow_session)
                {
                    // Use the cached original source so preview refreshes do
                    // not feed back into the execute path. Lazily cache if
                    // not yet available.
                    exec_source = nativeSourceOf(*native_process_window, std::nullopt);
                }
                const SharedAsset exec_manual_mask =
                    is_workflow_session ? SharedAsset{} : native_process_window->manualMaskImage();

                native_async_execution_in_progress = true;
                native_execution_cancel_requested = false;
                native_execution_cancel_hook_notified = false;
                extension_runtime.bridge()->setCancelled(false);
                extension_runtime.bridge()->resetProgressFeedback();
                const bool execute_header_progress_was_visible = native_process_window->isHeaderProgressVisible();
                native_process_window->setExecutionInProgress(true);
                native_process_window->setHeaderProgressVisible(true);
                const std::string execute_progress_text =
                    is_workflow_session ? "Running workflow..." : "Running extension...";
                native_process_window->showProcessMessage(QStringLiteral("Working"),
                                                          QString::fromStdString(execute_progress_text),
                                                          TopLevelProcessBaseControl::ProcessMessageTone::Info);
                extension_runtime.bridge()->progressSetText(execute_progress_text);
                extension_runtime.bridge()->progressSetSyntheticValue(0.0);

                auto* execute_progress_timer = new QTimer(native_process_window.get());
                execute_progress_timer->setInterval(250);
                const auto execute_progress_elapsed = std::make_shared<QElapsedTimer>();
                execute_progress_elapsed->start();
                QObject::connect(execute_progress_timer, &QTimer::timeout, native_process_window.get(),
                                 [&, execute_progress_elapsed]() {
                                     if (!native_async_execution_in_progress || native_window_close_requested)
                                     {
                                         return;
                                     }

                                     const double elapsed_ms = static_cast<double>(execute_progress_elapsed->elapsed());
                                     const double progress = (std::min)(95.0, 1.0 + elapsed_ms / 750.0);
                                     extension_runtime.bridge()->progressSetSyntheticValue(progress);
                                 });
                execute_progress_timer->start();

                auto* watcher = new QFutureWatcher<NativeExecuteRunResult>(native_process_window.get());
                QPointer<an::ui::extensions::ExtensionNativeProcessWindow> window_guard(native_process_window.get());
                QPointer<QTimer> execute_progress_timer_guard(execute_progress_timer);
                const std::string session_id = hello.session_id;
                QObject::connect(
                    watcher, &QFutureWatcher<NativeExecuteRunResult>::finished, native_process_window.get(),
                    [&, watcher, window_guard, execute_progress_timer_guard, execute_header_progress_was_visible]() {
                        const NativeExecuteRunResult result = watcher->result();
                        watcher->deleteLater();
                        if (execute_progress_timer_guard)
                        {
                            execute_progress_timer_guard->stop();
                            execute_progress_timer_guard->deleteLater();
                        }
                        native_async_execution_in_progress = false;
                        if (window_guard)
                        {
                            window_guard->setExecutionInProgress(false);
                            window_guard->setHeaderProgressVisible(execute_header_progress_was_visible);
                            // A target chosen while the run was on its way is bound now that it has landed.
                            rebind_target_when_frames_land();
                        }

                        extension_runtime.bridge()->resetProgressFeedback();
                        if (!window_guard)
                        {
                            return;
                        }
                        if (native_window_close_requested || native_execution_cancel_requested ||
                            window_guard->executionCancelRequested() || result.cancelled)
                        {
                            const bool close_after_cancel = window_guard->shouldCloseAfterExecutionCancel();
                            native_execution_cancel_requested = false;
                            window_guard->clearExecutionCancelRequested();
                            window_guard->clearProcessMessage();
                            if (close_after_cancel)
                            {
                                window_guard->hide();
                                request_native_window_shutdown(false);
                            }
                            return;
                        }

                        if (!result.ok)
                        {
                            const QString title = result.workflow ? QStringLiteral("Workflow failed")
                                                                  : QStringLiteral("Execution failed");
                            LOG_WARNING("Native {} failed: {}", result.workflow ? "workflow execute" : "execute",
                                        result.error);
                            window_guard->showProcessMessage(title, QString::fromStdString(result.error),
                                                             TopLevelProcessBaseControl::ProcessMessageTone::Error);
                            return;
                        }

                        if (result.workflow)
                        {
                            window_guard->showProcessMessage(QStringLiteral("Workflow complete"),
                                                             QStringLiteral("The workflow finished successfully."),
                                                             TopLevelProcessBaseControl::ProcessMessageTone::Info);
                            if (window_guard->shouldCloseAfterExecute())
                            {
                                window_guard->close();
                            }
                            else
                            {
                                window_guard->resetParamsToDefaults();
                            }
                            return;
                        }

                        if (result.manual_commit)
                        {
                            window_guard->clearProcessMessage();
                            if (result.committed_image)
                            {
                                window_guard->applyCommittedResult(std::move(*result.committed_image));
                            }

                            if (window_guard->shouldCloseAfterExecute())
                            {
                                window_guard->close();
                            }
                            else
                            {
                                window_guard->resetParamsToDefaults();
                            }
                            return;
                        }

                        if (!result.result_image)
                        {
                            window_guard->showProcessMessage(
                                QStringLiteral("Execution failed"),
                                QStringLiteral("The extension did not return an output image."),
                                TopLevelProcessBaseControl::ProcessMessageTone::Error);
                            return;
                        }

                        if (!result.commit_descriptor.has_value())
                        {
                            window_guard->showProcessMessage(
                                QStringLiteral("Execution failed"),
                                QStringLiteral("The extension did not prepare an output image buffer."),
                                TopLevelProcessBaseControl::ProcessMessageTone::Error);
                            return;
                        }

                        an::ui::extensions::ViewCommitMessage view_commit;
                        view_commit.session_id = hello.session_id;
                        view_commit.request_id = result.request_id;
                        view_commit.view_id = extension_runtime.currentViewId().empty()
                                                  ? hello.session_id
                                                  : extension_runtime.currentViewId();
                        view_commit.history_step_name = view_commit_history_step_name_override.empty()
                                                            ? "Extension Apply"
                                                            : view_commit_history_step_name_override;
                        view_commit.history_step_description = view_commit_history_step_description_override.empty()
                                                                   ? "Applied extension execution result"
                                                                   : view_commit_history_step_description_override;
                        view_commit.params = result.params;
                        view_commit.result_buffers.push_back(*result.commit_descriptor);

                        std::string commit_error;
                        if (!writeProtocolEnvelope(socket, an::ui::extensions::toProtocolMessage(view_commit),
                                                   &commit_error))
                        {
                            LOG_WARNING("Failed to send view.commit: {}", commit_error);
                            window_guard->showProcessMessage(QStringLiteral("Execution failed"),
                                                             QString::fromStdString(commit_error),
                                                             TopLevelProcessBaseControl::ProcessMessageTone::Error);
                            return;
                        }

                        window_guard->clearProcessMessage();
                        window_guard->applyCommittedResult(std::move(*result.result_image));

                        if (window_guard->shouldCloseAfterExecute())
                        {
                            window_guard->close();
                        }
                        else
                        {
                            window_guard->resetParamsToDefaults();
                        }
                    });

                watcher->setFuture(
                    QtConcurrent::run([&, req_id, execute_params, exec_source, exec_manual_mask, session_id]() {
                        NativeExecuteRunResult result;
                        result.request_id = req_id;
                        result.params = execute_params;
                        result.workflow = is_workflow_session;
                        if (result.workflow)
                        {
                            result.ok =
                                extension_runtime.executeWorkflow(session_id, req_id, execute_params, &result.error);
                        }
                        else
                        {
                            result.ok = extension_runtime.executeNative(session_id, req_id, execute_params,
                                                                        &result.result_image, &result.error,
                                                                        exec_source, false, exec_manual_mask);
                            result.manual_commit = extension_runtime.bridge()->manualViewCommitEmitted();
                            if (result.manual_commit)
                            {
                                result.committed_image = extension_runtime.bridge()->takeManualCommittedImage();
                            }
                            else if (result.ok && result.result_image)
                            {
                                const auto session_paths = materializeExtensionSessionPaths(session_id);
                                an::ui::extensions::SharedBufferManager manager(session_paths.root.parent_path());
                                an::ui::extensions::SharedImageBuffer result_buffer;
                                std::string alloc_error;
                                an::ui::extensions::ImageExportRequest commit_request;
                                commit_request.sessionId = session_id;
                                commit_request.role = an::ui::extensions::SharedBufferRole::Destination;
                                commit_request.accessMode = an::ui::extensions::SharedBufferAccessMode::ReadWrite;
                                commit_request.bufferId = result.request_id + ".commit";
                                commit_request.symbolicName = "destination";
                                if (!exportSharedBuffer(manager, *result.result_image, commit_request, &result_buffer,
                                                        &alloc_error))
                                {
                                    LOG_WARNING("Native commit buffer allocation failed: {}", alloc_error);
                                    result.ok = false;
                                    result.error = std::move(alloc_error);
                                }
                                else
                                {
                                    result.commit_descriptor = result_buffer.descriptor();
                                }
                            }
                        }
                        result.cancelled = extension_runtime.lastRequestCancelled();
                        return result;
                    }));
            };
            native_process_window->setExecuteCallback(execute_native);

            // Populate the native param panel with the extension's parameter schema.
            native_process_window->setParamDefs(extension_runtime.inspectedParamsSchema());

            if (native_process_window->hasPendingTargetSelector() || native_process_window->hasViewSelectorParams())
            {
                py::gil_scoped_acquire gil;
                ScopedPythonHostBridgeBinding list_views_bridge(extension_runtime.bridge());
                py::list py_views = extension_runtime.bridge()->listViews();
                std::vector<std::map<std::string, QVariant>> views;
                for (const auto& item : py_views)
                {
                    py::dict d = item.cast<py::dict>();
                    std::map<std::string, QVariant> view_map;
                    for (const auto& [k, v] : d)
                    {
                        const std::string key = k.cast<std::string>();
                        if (py::isinstance<py::str>(v))
                        {
                            view_map[key] = QString::fromStdString(v.cast<std::string>());
                        }
                        else if (py::isinstance<py::int_>(v))
                        {
                            view_map[key] = v.cast<int>();
                        }
                        else if (py::isinstance<py::bool_>(v))
                        {
                            view_map[key] = v.cast<bool>();
                        }
                        else if (py::isinstance<py::float_>(v))
                        {
                            view_map[key] = v.cast<double>();
                        }
                    }
                    views.push_back(std::move(view_map));
                }
                extension_runtime.bridge()->mergeKnownViewSnapshot(views);
                if (native_process_window->hasPendingTargetSelector())
                {
                    native_process_window->enableTargetSelector(views,
                                                                native_process_window->pendingTargetChannelFilter());
                    native_process_window->clearPendingTargetSelector();
                }
                else
                {
                    native_process_window->setAvailableViewSelectorViews(views);
                }
            }

            // The native window previews as every process window does - frames of a drag and a settled one after it,
            // paced at the application's rate - and each frame's worker hands what the frame runs over to the
            // extension's execute_preview at the frame's own size.
            if (extension_runtime.hasPreviewableExtension())
            {
                const std::string preview_session_id = hello.session_id;
                native_process_window->setPreviewBody(
                    [&extension_runtime, preview_session_id](const an::ui::extensions::ExtensionPreviewFrame& frame) {
                        return extension_runtime.previewFrame(preview_session_id, frame);
                    });
            }

            native_process_window->setTargetChangedCallback([&](const std::string& view_name) {
                if (view_name.empty())
                {
                    return;
                }
                native_pending_target = view_name;
                rebind_target_when_frames_land();
            });
        }

        // Headless, a native-mode host makes no window; an extension closing its process window still ends its
        // session, as closing the window does.
        if (ack.native_process_window && !native_process_window)
        {
            extension_runtime.setWindowlessCloseHandler([&]() { request_native_window_shutdown(true); });
        }

        // Wire the close callback so that closing the native window exits the
        // session loop and lets the host manager detect the disconnect.
        if (native_process_window)
        {
            native_process_window->setCloseCallback([&]() { request_native_window_shutdown(true); });
            native_process_window->setCancelCallback([&]() { request_native_execution_cancel(); });

            if (!headless_qt_platform && !extension_runtime.nativeProcessWindowHiddenByExtension())
            {
                native_process_window->show();
                native_process_window->raise();
                native_process_window->activateWindow();
            }
        }

        if (ack.bootstrap_status_messages && !write_runtime_lifecycle("runtime.ready"))
        {
            return 13;
        }

        // Ends the request in flight, however it ended - its result written, a cancel, a refusal: the frames held for
        // its run are let go, and the target chosen while a run of the GUI thread's own held them is bound.
        const auto end_in_flight = [&]() {
            in_flight.clear();
            if (native_process_window && !native_window_close_requested && rebind_target_when_frames_land)
            {
                rebind_target_when_frames_land();
            }
        };

        while ((!native_window_closed && lifetime.elapsed() < stay_alive_ms) || native_async_execution_in_progress ||
               native_preview_frames_running())
        {
            // Drive the Qt event loop to deliver Windows named pipe overlapped IO
            // completions.  QLocalSocket::waitForReadyRead() alone is unreliable on
            // Windows named pipes — data written by the host manager may sit in the
            // pipe kernel buffer without being delivered.  Pumping the event loop
            // ensures the internal QWindowsPipeReader picks up completions.
            //
            // Bind the host bridge for the duration of processEvents() so that
            // PySide6 timer callbacks (e.g. standalone-workflow view polling)
            // can reach the host bridge through currentPythonHostBridge().
            {
                ScopedPythonHostBridgeBinding event_loop_bridge(extension_runtime.bridge());
                QCoreApplication::processEvents(QEventLoop::AllEvents, 16);
            }

            if (crash_after_ack_ms >= 0 && lifetime.elapsed() >= crash_after_ack_ms)
            {
                simulate_crash_for_test();
            }

            if (!runtime_closed_crash_emitted && runtime_closed_crash_after_ack_ms >= 0 &&
                lifetime.elapsed() >= runtime_closed_crash_after_ack_ms)
            {
                runtime_closed_crash_emitted = true;
                send_runtime_closed_message(false);
                simulate_crash_for_test();
            }

            if (!disconnected_after_ack && disconnect_after_ack_ms >= 0 &&
                lifetime.elapsed() >= disconnect_after_ack_ms)
            {
                socket.disconnectFromServer();
                if (socket.state() != QLocalSocket::UnconnectedState)
                {
                    socket.waitForDisconnected(1000);
                }
                disconnected_after_ack = true;
            }

            if (disconnected_after_ack || socket.state() == QLocalSocket::UnconnectedState)
            {
                // The extension host is session-scoped. Once the control
                // channel disappears, exit promptly so a crashed UI process
                // cannot leave an orphaned extension runtime behind.
                if (!native_async_execution_in_progress && !native_preview_frames_running())
                {
                    break;
                }
                extension_runtime.bridge()->setCancelled(true);
                if (native_process_window)
                {
                    native_process_window->stopPreviews();
                }
            }

            if (socket.state() == QLocalSocket::ConnectedState)
            {
                buffered.append(socket.readAll());
                if (buffered.isEmpty())
                {
                    socket.waitForReadyRead(1);
                    buffered.append(socket.readAll());
                }
            }

            while (true)
            {
                an::ui::extensions::ExtensionProtocolMessage inbound;
                const auto status = an::ui::extensions::tryConsumeFramedProtocolMessage(&buffered, &inbound, &error);
                if (status == an::ui::extensions::ProtocolFrameDecodeStatus::NeedMoreData)
                {
                    break;
                }
                if (status == an::ui::extensions::ProtocolFrameDecodeStatus::InvalidFrame)
                {
                    printError(error);
                    return 11;
                }

                if (inbound.type == "runtime.ping")
                {
                    an::ui::extensions::RuntimePingMessage ping;
                    if (!an::ui::extensions::runtimePingFromProtocolMessage(inbound, &ping, &error))
                    {
                        printError(error);
                        return 14;
                    }

                    if (ignore_pings)
                    {
                        continue;
                    }

                    an::ui::extensions::RuntimePongMessage pong;
                    pong.session_id = ping.session_id;
                    pong.request_id = ping.request_id;
                    pong.sequence = ping.sequence;
                    if (!writePong(socket, pong, &error))
                    {
                        printError(error);
                        return 15;
                    }
                    continue;
                }

                if (inbound.type == "execution.execute")
                {
                    an::ui::extensions::ExecutionExecuteMessage execute;
                    if (!an::ui::extensions::executionExecuteFromProtocolMessage(inbound, &execute, &error))
                    {
                        printError(error);
                        return 16;
                    }
                    if (in_flight.active())
                    {
                        printError("Received execution.execute while another request was still in flight");
                        return 17;
                    }
                    in_flight.begin(execute.request_id, is_workflow_session ? "workflow" : "execute",
                                    execute.params_token, execute.params, std::move(execute.buffers),
                                    lifetime.elapsed() + execute_delay_ms);
                    native_execution_cancel_hook_notified = false;
                    continue;
                }

                if (inbound.type == "execution.preview")
                {
                    an::ui::extensions::ExecutionPreviewMessage preview;
                    if (!an::ui::extensions::executionPreviewFromProtocolMessage(inbound, &preview, &error))
                    {
                        printError(error);
                        return 18;
                    }
                    if (in_flight.active())
                    {
                        printError("Received execution.preview while another request was still in flight");
                        return 19;
                    }
                    in_flight.begin(preview.request_id, "preview", preview.params_token, preview.params,
                                    std::move(preview.buffers), lifetime.elapsed() + preview_delay_ms);
                    native_execution_cancel_hook_notified = false;
                    continue;
                }

                if (inbound.type == "execution.cancel")
                {
                    an::ui::extensions::ExecutionCancelMessage cancel;
                    if (!an::ui::extensions::executionCancelFromProtocolMessage(inbound, &cancel, &error))
                    {
                        printError(error);
                        return 20;
                    }
                    if (!in_flight.active() || cancel.target_request_id != in_flight.requestId())
                    {
                        printError("Received execution.cancel for an unknown in-flight request");
                        return 21;
                    }
                    if (ignore_cancel)
                    {
                        continue;
                    }

                    notify_execution_cancelled_once();

                    an::ui::extensions::ExecutionResultMessage result;
                    result.session_id = cancel.session_id;
                    result.request_id = cancel.request_id;
                    result.target_request_id = cancel.target_request_id;
                    result.result_kind = in_flight.kind();
                    result.status = "cancelled";
                    result.detail = "Cancelled by host request";
                    if (!writeExecutionResult(socket, result, &error))
                    {
                        printError(error);
                        return 22;
                    }

                    // The request ends here, its run never made: the frames held for it are let go, and the target
                    // chosen while they were held is bound.
                    end_in_flight();
                    continue;
                }

                bool handled_host_notification = false;
                if (!extension_runtime.handleInboundHostNotification(inbound, &handled_host_notification, &error))
                {
                    printError(error);
                    return 23;
                }
                if (handled_host_notification)
                {
                    if (native_process_window && (inbound.type == "view.bound" || inbound.type == "view.updated" ||
                                                  inbound.type == "view.renamed" || inbound.type == "view.destroyed"))
                    {
                        std::string input_error;
                        if (!extension_runtime.notifyInputChanged(&input_error))
                        {
                            LOG_WARNING("Extension on_input_changed() failed: {}", input_error);
                        }
                    }
                    continue;
                }

                if (isResponseEnvelope(inbound))
                {
                    continue;
                }

                printError("Received unexpected control message after handshake");
                return 27;
            }

            if (in_flight.active() && in_flight.dueElapsedMs() >= 0 && lifetime.elapsed() >= in_flight.dueElapsedMs() &&
                socket.state() == QLocalSocket::ConnectedState)
            {
                // The run is the GUI thread's own, with no frame of the window in the extension: the frames are held
                // and the one on its way lands first, served by this loop's next turns rather than by a nested one.
                in_flight.holdFrames(native_process_window.get());
                if (native_process_window && !native_process_window->isRunIdle())
                {
                    continue;
                }
                PreparedExecutionBuffers prepared_buffers;
                an::ui::extensions::ExecutionResultMessage result;
                result.session_id = hello.session_id;
                result.request_id = generateOpaqueToken();
                result.target_request_id = in_flight.requestId();
                result.result_kind = in_flight.kind();
                result.status = "succeeded";
                result.detail = "Processed " + in_flight.kind() + " for " + in_flight.paramsToken();
                const QString validation_title = in_flight.kind() == "preview" ? QStringLiteral("Preview unavailable")
                                                                               : QStringLiteral("Validation failed");
                // Asked with no frame of a native window in the extension, as an Apply's is.
                const bool valid = validate_native_process(in_flight.params(), validation_title);
                if (!valid)
                {
                    result.status = "failed";
                    result.detail = "Validation failed";
                    if (!writeExecutionResult(socket, result, &error))
                    {
                        printError(error);
                        return 30;
                    }

                    end_in_flight();
                    continue;
                }
                if (in_flight.kind() == "workflow")
                {
                    if (extension_runtime.hasExecutableExtension())
                    {
                        ScopedNativeProcessWindowExecutionLock execution_lock(native_process_window.get());
                        if (!extension_runtime.executeWorkflow(hello.session_id, in_flight.requestId(),
                                                               in_flight.params(), &error))
                        {
                            result.status = "failed";
                            result.detail = error;
                        }
                        else if (extension_runtime.lastRequestCancelled())
                        {
                            result.status = "cancelled";
                            result.detail = "Cancelled by host request";
                        }
                        else
                        {
                            result.detail = "Workflow completed";
                        }
                    }
                    in_flight.releaseFrames();
                    if (!writeExecutionResult(socket, result, &error))
                    {
                        printError(error);
                        return 30;
                    }

                    const bool close_after_execution_cancel =
                        native_process_window && native_process_window->shouldCloseAfterExecutionCancel();
                    end_in_flight();
                    if (close_after_execution_cancel)
                    {
                        native_process_window->clearExecutionCancelRequested();
                        native_process_window->hide();
                        request_native_window_shutdown(false);
                    }
                    continue;
                }
                if (!prepareExecutionBuffersForRequest(hello.session_id, in_flight.kind(), in_flight.buffers(),
                                                       forced_result_buffer_role, &prepared_buffers, &error))
                {
                    printError(error);
                    return 28;
                }

                if (prepared_buffers.output_buffer.valid())
                {
                    if (extension_runtime.hasExecutableExtension())
                    {
                        const auto run_extension = [&]() {
                            // What the process window's Cancel does, asked for from another thread while the run
                            // holds this one, when a case states when.
                            std::jthread interrupter;
                            if (interrupt_execute_after_ms >= 0)
                            {
                                interrupter = std::jthread(
                                    [&extension_runtime, interrupt_execute_after_ms](const std::stop_token& stop) {
                                        const auto until = std::chrono::steady_clock::now() +
                                                           std::chrono::milliseconds(interrupt_execute_after_ms);
                                        while (!stop.stop_requested() && std::chrono::steady_clock::now() < until)
                                        {
                                            std::this_thread::sleep_for(std::chrono::milliseconds(5));
                                        }
                                        if (!stop.stop_requested())
                                        {
                                            extension_runtime.requestCurrentExecutionCancel();
                                        }
                                    });
                            }
                            return extension_runtime.execute(
                                hello.session_id, in_flight.requestId(), in_flight.paramsToken(), in_flight.params(),
                                in_flight.kind(), &prepared_buffers.source_buffer, &prepared_buffers.output_buffer,
                                &prepared_buffers.auxiliary_buffers, &error);
                        };
                        bool extension_ok = false;
                        if (in_flight.kind() == "preview")
                        {
                            extension_ok = run_extension();
                        }
                        else
                        {
                            ScopedNativeProcessWindowExecutionLock execution_lock(native_process_window.get());
                            extension_ok = run_extension();
                        }

                        if (!extension_ok)
                        {
                            result.status = "failed";
                            result.detail = error;
                        }
                        else if (extension_runtime.lastRequestCancelled())
                        {
                            result.status = "cancelled";
                            result.detail = "Cancelled by host request";
                        }
                        else
                        {
                            result.output_buffers.push_back(prepared_buffers.output_buffer.descriptor());
                            for (const auto& auxiliary_buffer : prepared_buffers.auxiliary_buffers)
                            {
                                const auto& descriptor = auxiliary_buffer.descriptor();
                                if (descriptor.buffer_role == an::ui::extensions::SharedBufferRole::Mask &&
                                    descriptor.access_mode == an::ui::extensions::SharedBufferAccessMode::ReadWrite)
                                {
                                    result.output_buffers.push_back(descriptor);
                                }
                            }
                        }
                    }
                    else
                    {
                        result.output_buffers.push_back(prepared_buffers.output_buffer.descriptor());
                    }
                }
                // The run is over: the frames are let go before the commit, which may close the window and its buffers.
                in_flight.releaseFrames();
                const bool manual_view_commit_emitted = extension_runtime.bridge()->manualViewCommitEmitted();
                bool emitted_view_commit =
                    manual_view_commit_emitted && result.status == "succeeded" && in_flight.kind() == "execute";
                if (!emitted_view_commit && result.status == "succeeded" && in_flight.kind() == "execute")
                {
                    result.history_step_name = "Extension Apply";
                    result.history_step_description = "Applied extension execution result";

                    if (std::any_of(result.output_buffers.begin(), result.output_buffers.end(),
                                    [](const an::ui::extensions::SharedBufferDescriptor& descriptor) {
                                        return descriptor.buffer_role ==
                                               an::ui::extensions::SharedBufferRole::Destination;
                                    }))
                    {
                        an::ui::extensions::ViewCommitMessage view_commit;
                        view_commit.session_id = hello.session_id;
                        view_commit.request_id = in_flight.requestId();
                        view_commit.view_id = extension_runtime.currentViewId().empty()
                                                  ? hello.session_id
                                                  : extension_runtime.currentViewId();
                        view_commit.history_step_name = view_commit_history_step_name_override.empty()
                                                            ? result.history_step_name
                                                            : view_commit_history_step_name_override;
                        view_commit.history_step_description = view_commit_history_step_description_override.empty()
                                                                   ? result.history_step_description
                                                                   : view_commit_history_step_description_override;
                        view_commit.params = in_flight.params();
                        view_commit.result_buffers = result.output_buffers;
                        if (!writeProtocolEnvelope(socket, an::ui::extensions::toProtocolMessage(view_commit), &error))
                        {
                            printError(error);
                            return 29;
                        }
                        emitted_view_commit = true;
                        if (close_after_view_commit)
                        {
                            request_native_window_shutdown(true);
                        }
                    }
                }
                if (!emitted_view_commit && !writeExecutionResult(socket, result, &error))
                {
                    printError(error);
                    return 30;
                }

                const bool close_after_execution_cancel =
                    native_process_window && native_process_window->shouldCloseAfterExecutionCancel();
                end_in_flight();
                if (close_after_execution_cancel)
                {
                    native_process_window->clearExecutionCancelRequested();
                    native_process_window->hide();
                    request_native_window_shutdown(false);
                }
            }
        }

        if (native_process_window && !native_process_close_notified)
        {
            native_process_close_notified = true;
            std::string close_hook_error;
            if (!extension_runtime.notifyProcessClose("session_ended", &close_hook_error))
            {
                LOG_WARNING("Extension on_process_close() failed: {}", close_hook_error);
            }
        }
    }

    sendQueuedWrites(socket);
    socket.disconnectFromServer();
    if (duplicate_socket.state() != QLocalSocket::UnconnectedState)
    {
        duplicate_socket.disconnectFromServer();
    }
    return 0;
}
