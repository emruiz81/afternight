// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#define PY_SSIZE_T_CLEAN
#if defined(_MSC_VER) && defined(_DEBUG)
#define AN_RESTORE_DEBUG_AFTER_PYTHON_H
#undef _DEBUG
#endif

#include <Python.h>

#ifdef AN_RESTORE_DEBUG_AFTER_PYTHON_H
#define _DEBUG
#undef AN_RESTORE_DEBUG_AFTER_PYTHON_H
#endif

#include "AfterNightExtensionProtocol.h"
#include "ExtensionProtocolFraming.h"
#include "SharedBufferFileMapping.h"

#include <QByteArray>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QCborArray>
#include <QCborMap>
#include <QCborValue>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QLocalSocket>
#include <QString>
#include <QTimer>
#include <QUuid>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <deque>
#include <filesystem>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <cerrno>
#include <unistd.h>
#endif

namespace
{

constexpr int kDefaultIoTimeoutMs = 5000;

void printError(const std::string& message)
{
    std::cerr << message << '\n';
}

void printJson(const QJsonObject& object)
{
    std::cout << QJsonDocument(object).toJson(QJsonDocument::Compact).constData() << '\n';
}

std::vector<std::string> splitCapabilities(const QString& text)
{
    std::vector<std::string> values;
    for (const auto& piece : text.split(',', Qt::SkipEmptyParts))
    {
        values.push_back(piece.trimmed().toStdString());
    }
    return values;
}

std::filesystem::path defaultRuntimeRootForAppDir(const std::filesystem::path& app_dir)
{
    return app_dir / "PythonLib";
}

bool isRegularFile(const std::filesystem::path& path)
{
    std::error_code ec;
    return std::filesystem::is_regular_file(path, ec);
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
        if (isRegularFile(candidate))
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
        if (isRegularFile(candidate))
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

    for (const auto& probe_root : {app_dir, std::filesystem::current_path()})
    {
        const auto repo_modules = findRepositoryPythonModulesPath(probe_root);
        if (!repo_modules.empty() && std::find(paths.begin(), paths.end(), repo_modules) == paths.end())
        {
            paths.push_back(repo_modules);
        }
    }

    const auto deployed_path = runtimeHostPackagePath(runtime_root);
    if (std::filesystem::exists(deployed_path / "afternight" / "__init__.py"))
    {
        const auto normalized = deployed_path.lexically_normal();
        if (std::find(paths.begin(), paths.end(), normalized) == paths.end())
        {
            paths.push_back(normalized);
        }
    }

    return paths;
}

std::string pyStatusMessage(const PyStatus& status)
{
    if (status.err_msg)
    {
        return status.err_msg;
    }
    return "Python configuration failed";
}

bool setPyConfigString(PyConfig* config, wchar_t** target, const std::wstring& value, std::string* error)
{
    const PyStatus status = PyConfig_SetString(config, target, value.c_str());
    if (PyStatus_Exception(status))
    {
        if (error)
        {
            *error = pyStatusMessage(status);
        }
        return false;
    }
    return true;
}

bool appendPySearchPath(PyConfig* config, const std::filesystem::path& path, std::string* error)
{
    if (path.empty())
    {
        return true;
    }

    const PyStatus status = PyWideStringList_Append(&config->module_search_paths, path.wstring().c_str());
    if (PyStatus_Exception(status))
    {
        if (error)
        {
            *error = pyStatusMessage(status);
        }
        return false;
    }
    return true;
}

std::string pythonExceptionMessage()
{
    if (!PyErr_Occurred())
    {
        return {};
    }

    PyObject* type = nullptr;
    PyObject* value = nullptr;
    PyObject* traceback = nullptr;
    PyErr_Fetch(&type, &value, &traceback);
    PyErr_NormalizeException(&type, &value, &traceback);

    std::string message = "Python error";
    PyObject* text = PyObject_Str(value ? value : type);
    if (text)
    {
        if (const char* utf8 = PyUnicode_AsUTF8(text))
        {
            message = utf8;
        }
        Py_DECREF(text);
    }

    Py_XDECREF(type);
    Py_XDECREF(value);
    Py_XDECREF(traceback);
    return message;
}

bool prependSysPath(const std::filesystem::path& path, std::string* error)
{
    if (path.empty())
    {
        return true;
    }

    PyObject* sys_path = PySys_GetObject("path");
    if (!sys_path || !PyList_Check(sys_path))
    {
        if (error)
        {
            *error = "Python sys.path is unavailable";
        }
        return false;
    }

    PyObject* value = PyUnicode_FromWideChar(path.wstring().c_str(), -1);
    if (!value)
    {
        if (error)
        {
            *error = pythonExceptionMessage();
        }
        return false;
    }

    const int inserted = PyList_Insert(sys_path, 0, value);
    Py_DECREF(value);
    if (inserted != 0)
    {
        if (error)
        {
            *error = pythonExceptionMessage();
        }
        return false;
    }
    return true;
}

std::string pythonStringLiteral(std::string_view value)
{
    std::string result = "'";
    for (const char ch : value)
    {
        switch (ch)
        {
        case '\\':
            result += "\\\\";
            break;
        case '\'':
            result += "\\'";
            break;
        case '\n':
            result += "\\n";
            break;
        case '\r':
            result += "\\r";
            break;
        case '\t':
            result += "\\t";
            break;
        default:
            result += ch;
            break;
        }
    }
    result += "'";
    return result;
}

bool ensureLiteHostPythonModuleRegistered(std::string* error);

bool initializePythonForLiteHost(const std::filesystem::path& app_dir, const std::filesystem::path& runtime_root,
                                 const std::filesystem::path& environment_root,
                                 const std::filesystem::path& extension_package_root, const QString& extension_id,
                                 const QString& session_id, const QString& launch_mode, const QString& sdk_backend,
                                 const bool standalone_mode, std::string* error)
{
    if (runtime_root.empty() || environment_root.empty())
    {
        if (error)
        {
            *error = "Missing Python runtime or environment root";
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
            *error = "Could not resolve Python executables for the lite host";
        }
        return false;
    }

    qputenv("AFTERNIGHT_SDK_BACKEND", sdk_backend.toUtf8());
    qputenv("AFTERNIGHT_EXTENSION_ID", extension_id.toUtf8());
    qputenv("AFTERNIGHT_SESSION_ID", session_id.toUtf8());
    qputenv("AFTERNIGHT_LAUNCH_MODE", launch_mode.toUtf8());
    qputenv("AFTERNIGHT_IS_STANDALONE", standalone_mode ? QByteArrayLiteral("1") : QByteArrayLiteral("0"));

    if (!ensureLiteHostPythonModuleRegistered(error))
    {
        return false;
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

    bool config_ready =
        setPyConfigString(&config, &config.home, runtime_root.wstring(), error) &&
        setPyConfigString(&config, &config.program_name, env_python.wstring(), error) &&
        setPyConfigString(&config, &config.executable, env_python.wstring(), error) &&
        setPyConfigString(&config, &config.base_executable, base_python.wstring(), error) &&
        setPyConfigString(&config, &config.prefix, environment_root.wstring(), error) &&
        setPyConfigString(&config, &config.exec_prefix, environment_root.wstring(), error) &&
        setPyConfigString(&config, &config.base_prefix, runtime_root.wstring(), error) &&
        setPyConfigString(&config, &config.base_exec_prefix, runtime_root.wstring(), error) &&
        appendPySearchPath(&config, site_packages_path, error) && appendPySearchPath(&config, stdlib_zip_path, error) &&
        appendPySearchPath(&config, stdlib_path, error) && appendPySearchPath(&config, dynload_path, error);
    if (!config_ready)
    {
        PyConfig_Clear(&config);
        return false;
    }

    const PyStatus status = Py_InitializeFromConfig(&config);
    PyConfig_Clear(&config);
    if (PyStatus_Exception(status))
    {
        if (error)
        {
            *error = pyStatusMessage(status);
        }
        return false;
    }

    if (!prependSysPath(environment_root, error))
    {
        return false;
    }
    if (!extension_package_root.empty() && !prependSysPath(extension_package_root, error))
    {
        return false;
    }

    const auto import_roots = hostManagedAfternightImportRoots(app_dir, runtime_root);
    for (auto it = import_roots.rbegin(); it != import_roots.rend(); ++it)
    {
        if (!prependSysPath(*it, error))
        {
            return false;
        }
    }

    PyObject* afternight = PyImport_ImportModule("afternight");
    if (!afternight)
    {
        if (error)
        {
            *error = pythonExceptionMessage();
        }
        return false;
    }
    Py_DECREF(afternight);
    return true;
}

bool inspectLiteExtension(const std::string& entry_point, const std::string& process_class, const bool inspect_settings,
                          const bool inspect_params, QJsonObject* out_result, std::string* error,
                          const bool call_launch_hook = false)
{
    if (!out_result || entry_point.empty())
    {
        return true;
    }

    std::ostringstream script;
    script << "import importlib, json\n"
           << "import afternight\n"
           << "module = importlib.import_module(" << pythonStringLiteral(entry_point) << ")\n"
           << "instance = None\n";
    if (!process_class.empty())
    {
        script << "cls = getattr(module, " << pythonStringLiteral(process_class) << ")\n"
               << "instance = cls(afternight.ExtensionContext())\n";
    }
    script << "settings_schema = instance.get_settings_params() if (instance is not None and "
           << (inspect_settings ? "True" : "False") << ") else []\n"
           << "params_schema = instance.get_params() if (instance is not None and "
           << (inspect_params ? "True" : "False") << ") else []\n";
    if (call_launch_hook)
    {
        script << "if instance is not None and hasattr(instance, 'on_launch'):\n"
               << "    instance.on_launch()\n"
               << "if instance is not None:\n"
               << "    from afternight.lite import _run_extension_startup\n"
               << "    _run_extension_startup(instance)\n";
    }
    script << "__afternight_result_json = json.dumps({" << "'settings_schema': settings_schema, "
           << "'params_schema': params_schema, " << "'supports_ui_events': False, " << "'supports_ui_validation': False"
           << "})\n";

    PyObject* main_module = PyImport_AddModule("__main__");
    PyObject* globals = main_module ? PyModule_GetDict(main_module) : nullptr;
    PyObject* result = globals ? PyRun_String(script.str().c_str(), Py_file_input, globals, globals) : nullptr;
    if (!result)
    {
        if (error)
        {
            *error = pythonExceptionMessage();
        }
        return false;
    }
    Py_DECREF(result);

    PyObject* json_value = PyDict_GetItemString(globals, "__afternight_result_json");
    if (!json_value)
    {
        if (error)
        {
            *error = "Lite extension inspect did not produce a result";
        }
        return false;
    }
    const char* json_text = PyUnicode_AsUTF8(json_value);
    if (!json_text)
    {
        if (error)
        {
            *error = pythonExceptionMessage();
        }
        return false;
    }

    QJsonParseError parse_error;
    const QJsonDocument document = QJsonDocument::fromJson(QByteArray(json_text), &parse_error);
    if (parse_error.error != QJsonParseError::NoError || !document.isObject())
    {
        if (error)
        {
            *error = "Lite extension inspect returned invalid JSON";
        }
        return false;
    }

    const QJsonObject values = document.object();
    out_result->insert(QStringLiteral("settings_schema"), values.value(QStringLiteral("settings_schema")));
    out_result->insert(QStringLiteral("params_schema"), values.value(QStringLiteral("params_schema")));
    out_result->insert(QStringLiteral("supports_ui_events"), values.value(QStringLiteral("supports_ui_events")));
    out_result->insert(QStringLiteral("supports_ui_validation"),
                       values.value(QStringLiteral("supports_ui_validation")));
    return true;
}

bool writeProtocolMessage(QLocalSocket& socket, const an::ui::extensions::ExtensionProtocolMessage& message,
                          std::string* error)
{
    const QByteArray frame = an::ui::extensions::frameProtocolMessage(message, error);
    if (frame.isEmpty() && error && !error->empty())
    {
        return false;
    }

    if (socket.write(frame) != frame.size() || !socket.waitForBytesWritten(kDefaultIoTimeoutMs))
    {
        if (error)
        {
            *error = "Failed to write a protocol message to the host";
        }
        return false;
    }

    return true;
}

bool writeHello(QLocalSocket& socket, const an::ui::extensions::RuntimeHelloMessage& hello, std::string* error)
{
    return writeProtocolMessage(socket, an::ui::extensions::toProtocolMessage(hello), error);
}

bool writePong(QLocalSocket& socket, const an::ui::extensions::RuntimePongMessage& pong, std::string* error)
{
    return writeProtocolMessage(socket, an::ui::extensions::toProtocolMessage(pong), error);
}

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
            ::CloseHandle(inherited_handle);
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
            ::CloseHandle(inherited_handle);
            return false;
        }
    }

    ::CloseHandle(inherited_handle);
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
#else
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

QJsonObject makeInspectResult(const QString& display_name)
{
    QJsonObject runtime;
    runtime.insert(QStringLiteral("application_class"),
                   QString::fromLatin1(QCoreApplication::instance()->metaObject()->className()));
    runtime.insert(QStringLiteral("has_app_context"), false);
    runtime.insert(QStringLiteral("style_class"), QStringLiteral("none"));

    QJsonObject result;
    result.insert(QStringLiteral("ui_runtime"), runtime);
    result.insert(QStringLiteral("display_name"), display_name);
    result.insert(QStringLiteral("preview_capable"), false);
    result.insert(QStringLiteral("settings_schema"), QJsonArray{});
    result.insert(QStringLiteral("params_schema"), QJsonArray{});
    result.insert(QStringLiteral("supports_ui_events"), false);
    result.insert(QStringLiteral("supports_ui_validation"), false);
    return result;
}

std::string generateLiteOpaqueToken()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
}

PyObject* cborValueToPythonObject(const QCborValue& value);

struct PyObjectHolder
{
    PyObject* value = nullptr;

    explicit PyObjectHolder(PyObject* object = nullptr) noexcept : value(object) {}
    PyObjectHolder(const PyObjectHolder&) = delete;
    PyObjectHolder& operator=(const PyObjectHolder&) = delete;
    PyObjectHolder(PyObjectHolder&& other) noexcept : value(other.value)
    {
        other.value = nullptr;
    }
    PyObjectHolder& operator=(PyObjectHolder&& other) noexcept
    {
        if (this != &other)
        {
            Py_XDECREF(value);
            value = other.value;
            other.value = nullptr;
        }
        return *this;
    }
    ~PyObjectHolder()
    {
        Py_XDECREF(value);
    }

    [[nodiscard]] PyObject* release() noexcept
    {
        PyObject* released = value;
        value = nullptr;
        return released;
    }
};

bool setPythonDictItem(PyObject* dict, const char* key, PyObject* value)
{
    if (!dict || !key || !value)
    {
        return false;
    }
    return PyDict_SetItemString(dict, key, value) == 0;
}

bool setPythonDictItem(PyObject* dict, const QString& key, PyObject* value)
{
    const QByteArray utf8 = key.toUtf8();
    return setPythonDictItem(dict, utf8.constData(), value);
}

PyObject* cborMapToPythonDict(const QCborMap& map)
{
    PyObjectHolder dict(PyDict_New());
    if (!dict.value)
    {
        return nullptr;
    }

    for (auto it = map.cbegin(); it != map.cend(); ++it)
    {
        PyObjectHolder item(cborValueToPythonObject(it.value()));
        if (!item.value || !setPythonDictItem(dict.value, it.key().toString(), item.value))
        {
            return nullptr;
        }
    }

    return dict.release();
}

PyObject* cborValueToPythonObject(const QCborValue& value)
{
    if (value.isUndefined() || value.isNull())
    {
        Py_RETURN_NONE;
    }
    if (value.isBool())
    {
        return PyBool_FromLong(value.toBool() ? 1 : 0);
    }
    if (value.isInteger())
    {
        return PyLong_FromLongLong(value.toInteger());
    }
    if (value.isDouble())
    {
        return PyFloat_FromDouble(value.toDouble());
    }
    if (value.isString())
    {
        const QByteArray utf8 = value.toString().toUtf8();
        return PyUnicode_FromStringAndSize(utf8.constData(), utf8.size());
    }
    if (value.isArray())
    {
        const QCborArray array = value.toArray();
        PyObjectHolder list(PyList_New(array.size()));
        if (!list.value)
        {
            return nullptr;
        }
        for (qsizetype i = 0; i < array.size(); ++i)
        {
            PyObject* item = cborValueToPythonObject(array.at(i));
            if (!item)
            {
                return nullptr;
            }
            PyList_SET_ITEM(list.value, i, item);
        }
        return list.release();
    }
    if (value.isMap())
    {
        return cborMapToPythonDict(value.toMap());
    }

    Py_RETURN_NONE;
}

bool pythonObjectToCborValue(PyObject* object, QCborValue* out_value, std::string* error, const int depth = 0)
{
    if (!out_value)
    {
        if (error)
        {
            *error = "Python to CBOR output is null";
        }
        return false;
    }
    if (depth > 64)
    {
        if (error)
        {
            *error = "Python value is nested too deeply";
        }
        return false;
    }
    if (!object || object == Py_None)
    {
        *out_value = QCborValue(nullptr);
        return true;
    }
    if (PyBool_Check(object))
    {
        *out_value = QCborValue(object == Py_True);
        return true;
    }
    if (PyLong_Check(object))
    {
        *out_value = QCborValue(static_cast<qint64>(PyLong_AsLongLong(object)));
        if (PyErr_Occurred())
        {
            if (error)
            {
                *error = pythonExceptionMessage();
            }
            return false;
        }
        return true;
    }
    if (PyFloat_Check(object))
    {
        *out_value = QCborValue(PyFloat_AsDouble(object));
        if (PyErr_Occurred())
        {
            if (error)
            {
                *error = pythonExceptionMessage();
            }
            return false;
        }
        return true;
    }
    if (PyUnicode_Check(object))
    {
        const char* text = PyUnicode_AsUTF8(object);
        if (!text)
        {
            if (error)
            {
                *error = pythonExceptionMessage();
            }
            return false;
        }
        *out_value = QString::fromUtf8(text);
        return true;
    }
    if (PyList_Check(object) || PyTuple_Check(object))
    {
        const Py_ssize_t size = PySequence_Size(object);
        QCborArray array;
        for (Py_ssize_t i = 0; i < size; ++i)
        {
            PyObjectHolder item(PySequence_GetItem(object, i));
            QCborValue value;
            if (!item.value || !pythonObjectToCborValue(item.value, &value, error, depth + 1))
            {
                return false;
            }
            array.push_back(value);
        }
        *out_value = array;
        return true;
    }
    if (PyDict_Check(object))
    {
        QCborMap map;
        PyObject* key = nullptr;
        PyObject* value = nullptr;
        Py_ssize_t pos = 0;
        while (PyDict_Next(object, &pos, &key, &value))
        {
            if (!PyUnicode_Check(key))
            {
                if (error)
                {
                    *error = "Python dict keys must be strings";
                }
                return false;
            }
            const char* key_text = PyUnicode_AsUTF8(key);
            if (!key_text)
            {
                if (error)
                {
                    *error = pythonExceptionMessage();
                }
                return false;
            }
            QCborValue cbor_value;
            if (!pythonObjectToCborValue(value, &cbor_value, error, depth + 1))
            {
                return false;
            }
            map[QString::fromUtf8(key_text)] = cbor_value;
        }
        *out_value = map;
        return true;
    }

    if (error)
    {
        *error = "Unsupported Python value for protocol serialization";
    }
    return false;
}

struct LiteImageBinding
{
    an::ui::extensions::SharedBufferDescriptor descriptor;
    an::ui::extensions::SharedBufferFileMapping mapping;
    QCborMap metadata;
    bool writable = false;
};

/// A request of the panel the application draws - a frame of its preview, or an Apply - from the time it was read to
/// its answer.
struct LitePanelRequest
{
    /// Which request it is, so a run scheduled for one never runs another.
    std::uint64_t number = 0;
    std::string request_id;
    /// "preview" or "execute".
    std::string kind;
    QCborMap params;
    std::vector<an::ui::extensions::SharedBufferDescriptor> buffers;
};

/// Text the application reads as plain: a line break stays where `keepLineBreaks` says, any other control goes; cut to
/// a bound.
[[nodiscard]] std::string plainProtocolText(const std::string& text, const std::size_t limit,
                                            const bool keepLineBreaks = false)
{
    QString plain = QString::fromStdString(text);
    plain.erase(std::remove_if(plain.begin(), plain.end(),
                               [keepLineBreaks](const QChar character) {
                                   return character.category() == QChar::Other_Control &&
                                          !(keepLineBreaks && character == QLatin1Char('\n'));
                               }),
                plain.end());
    return plain.left(static_cast<qsizetype>(limit)).trimmed().toStdString();
}

bool dispatchLiteViewNotification(const an::ui::extensions::ExtensionProtocolMessage& inbound, std::string* error);

[[nodiscard]] bool isLiteViewNotification(const std::string_view type) noexcept
{
    return type == "view.created" || type == "view.updated" || type == "view.renamed" || type == "view.destroyed";
}

[[nodiscard]] bool isProtocolErrorMessage(const std::string_view type) noexcept
{
    return type.size() >= 6 && type.substr(type.size() - 6) == ".error";
}

[[nodiscard]] bool isProtocolResponseMessage(const std::string_view type) noexcept
{
    return isProtocolErrorMessage(type) || (type.size() >= 7 && type.substr(type.size() - 7) == ".result");
}

struct LitePythonHostBridge
{
    QLocalSocket* socket = nullptr;
    QByteArray* buffered_input = nullptr;
    QString extension_id;
    QString session_id;
    QString launch_mode;
    QString sdk_backend;
    /// The `id_suffix` of the process the extension contributed that the session was started for; empty for one of its
    /// manifest's processes.
    QString contributed_process;
    bool standalone_mode = false;
    bool cancel_requested = false;
    int timeout_ms = kDefaultIoTimeoutMs;
    std::unordered_map<std::string, LiteImageBinding> image_bindings;
    std::deque<an::ui::extensions::ExtensionProtocolMessage> deferred_protocol_responses;
    /// The theme state the application sent last (`theme.describe`, `theme.changed`): what `theme_tokens` answers an
    /// extension that paints itself. Empty until one is sent.
    QCborMap theme_tokens;

    // ---- The panel the application draws

    /// Whether the application draws the session's panel: its acknowledgement stated `kHostedPanelCapability`.
    bool hosted_panel = false;
    /// What the panel's tool configuration resolved, which answers the extension's settings reads first.
    QCborMap hosted_tool_record;
    /// The image the session is bound to, as the application last named it: a request's target.
    QString bound_view_name;
    /// The request of the panel on its way; none between requests.
    std::optional<LitePanelRequest> panel_request;
    std::uint64_t panel_requests_read = 0;
    /// How long the next frame or Apply is held before it runs, as a case states it.
    int preview_delay_ms = 0;
    int execute_delay_ms = 0;
    /// What arrived while the extension ran - a request of the panel, or its own run - other than a heartbeat, a cancel
    /// or the theme: handled once the run ends, in the order it came.
    std::deque<an::ui::extensions::ExtensionProtocolMessage> kept_inbound;
    QElapsedTimer pump_clock;

    // ---- What the application is told of a run (`session.activity`)

    /// Whether the run under way is one the application is told of: an Apply of the panel, or the extension's own run
    /// at its start.
    bool reports_run = false;
    bool told_run = false;
    std::optional<int> told_percent;
    std::string told_text;

    /// The extension closed its process window: the session ends.
    bool close_requested = false;

    /// Writes a message the application is not asked to answer; false, and why, when the socket refuses it.
    bool tell(const an::ui::extensions::ExtensionProtocolMessage& message, std::string* error = nullptr);

    /// What the application is told of the run under way: that it runs, how far it has come - each whole percent, or a
    /// new text - and nothing for a run it is not told of.
    void tellRunProgress(std::optional<double> progress, const std::optional<std::string>& text)
    {
        if (!reports_run || !hasControlSocket())
        {
            return;
        }
        std::optional<int> percent;
        if (progress.has_value() && std::isfinite(*progress))
        {
            percent = static_cast<int>(std::clamp(*progress, 0.0, 1.0) * 100.0);
        }
        const std::string plain =
            text.has_value() ? plainProtocolText(*text, an::ui::extensions::kSessionActivityTextLimit) : told_text;
        if (told_run && percent == told_percent && plain == told_text)
        {
            return;
        }
        told_run = true;
        if (percent.has_value())
        {
            told_percent = percent;
        }
        told_text = plain;
        an::ui::extensions::SessionActivityMessage activity;
        activity.session_id = session_id.toStdString();
        activity.running = true;
        if (told_percent.has_value())
        {
            activity.progress = static_cast<double>(*told_percent) / 100.0;
        }
        activity.text = told_text;
        static_cast<void>(tell(an::ui::extensions::toProtocolMessage(activity)));
    }

    /// The run the application is told of ended - "succeeded", "failed" or "cancelled", and why it failed.
    void tellRunEnded(const std::string& outcome, const std::string& detail = {})
    {
        if (reports_run && hasControlSocket())
        {
            an::ui::extensions::SessionActivityMessage activity;
            activity.session_id = session_id.toStdString();
            activity.running = false;
            activity.outcome = outcome;
            activity.text = plainProtocolText(detail, an::ui::extensions::kSessionActivityTextLimit);
            static_cast<void>(tell(an::ui::extensions::toProtocolMessage(activity)));
        }
        reports_run = false;
        told_run = false;
        told_percent.reset();
        told_text.clear();
    }

    /// While the extension runs, what the application sends meanwhile is read - at most every 20 ms, never waiting: a
    /// heartbeat is answered, a cancel taken and the theme kept; anything else is kept for when the run ends.
    void pumpWhileRunning();

    /// Keeps the theme state a theme message carries; a message that is not one changes nothing.
    void takeTheme(const an::ui::extensions::ExtensionProtocolMessage& inbound)
    {
        std::string ignored;
        if (inbound.type == "theme.describe")
        {
            an::ui::extensions::ThemeDescribeMessage message;
            if (an::ui::extensions::themeDescribeFromProtocolMessage(inbound, &message, &ignored))
            {
                theme_tokens = std::move(message.tokens);
            }
        }
        else if (inbound.type == "theme.changed")
        {
            an::ui::extensions::ThemeChangedMessage message;
            if (an::ui::extensions::themeChangedFromProtocolMessage(inbound, &message, &ignored))
            {
                theme_tokens = std::move(message.tokens);
            }
        }
    }

    [[nodiscard]] bool hasControlSocket() const noexcept
    {
        return socket && socket->state() == QLocalSocket::ConnectedState && buffered_input;
    }

    [[nodiscard]] QCborMap sessionInfo() const
    {
        return QCborMap{{QStringLiteral("extension_id"), extension_id},
                        {QStringLiteral("session_id"), session_id},
                        {QStringLiteral("launch_mode"), launch_mode},
                        {QStringLiteral("sdk_backend"), sdk_backend},
                        {QStringLiteral("contributed_process"), contributed_process},
                        {QStringLiteral("is_standalone"), standalone_mode}};
    }

    [[nodiscard]] bool takeDeferredResponse(const std::string_view request_id,
                                            an::ui::extensions::ExtensionProtocolMessage* out_response)
    {
        const auto it = std::find_if(deferred_protocol_responses.begin(), deferred_protocol_responses.end(),
                                     [request_id](const an::ui::extensions::ExtensionProtocolMessage& message) {
                                         return message.request_id == request_id;
                                     });
        if (it == deferred_protocol_responses.end())
        {
            return false;
        }

        if (out_response)
        {
            *out_response = std::move(*it);
        }
        deferred_protocol_responses.erase(it);
        return true;
    }

    [[nodiscard]] bool completeProtocolResponse(an::ui::extensions::ExtensionProtocolMessage response,
                                                an::ui::extensions::ExtensionProtocolMessage* out_response,
                                                std::string* error)
    {
        if (isProtocolErrorMessage(response.type))
        {
            an::ui::extensions::ExtensionProtocolError protocol_error;
            std::string parse_error;
            if (an::ui::extensions::protocolErrorFromCborValue(response.fields.value(QStringLiteral("error")),
                                                               &protocol_error, &parse_error))
            {
                // What was refused and why, as the full host says it: an extension is told the application's reason.
                if (error)
                {
                    *error = protocol_error.message.empty() ? response.type : protocol_error.message;
                    if (!protocol_error.details.empty())
                    {
                        *error += ": " + protocol_error.details;
                    }
                }
            }
            else if (error)
            {
                *error = parse_error.empty() ? response.type : parse_error;
            }
            return false;
        }

        *out_response = std::move(response);
        return true;
    }

    [[nodiscard]] bool handleInboundWhileAwaitingResponse(const an::ui::extensions::ExtensionProtocolMessage& request,
                                                          an::ui::extensions::ExtensionProtocolMessage inbound,
                                                          an::ui::extensions::ExtensionProtocolMessage* out_response,
                                                          bool* matched_response, std::string* error)
    {
        if (!matched_response)
        {
            if (error)
            {
                *error = "Protocol response match output is null";
            }
            return false;
        }
        *matched_response = false;

        if (inbound.request_id == request.request_id)
        {
            *matched_response = true;
            return completeProtocolResponse(std::move(inbound), out_response, error);
        }

        if (inbound.type == "runtime.ping")
        {
            an::ui::extensions::RuntimePingMessage ping;
            if (!an::ui::extensions::runtimePingFromProtocolMessage(inbound, &ping, error))
            {
                return false;
            }
            an::ui::extensions::RuntimePongMessage pong;
            pong.session_id = ping.session_id;
            pong.request_id = ping.request_id;
            pong.sequence = ping.sequence;
            return writePong(*socket, pong, error);
        }

        if (inbound.type == "execution.cancel")
        {
            cancel_requested = true;
            return true;
        }

        if (inbound.type == "theme.describe" || inbound.type == "theme.changed")
        {
            takeTheme(inbound);
            return true;
        }

        if (isLiteViewNotification(inbound.type))
        {
            return dispatchLiteViewNotification(inbound, error);
        }

        if (!inbound.request_id.empty() && isProtocolResponseMessage(inbound.type))
        {
            deferred_protocol_responses.push_back(std::move(inbound));
        }
        else
        {
            // A request of the panel, a person's event or a raise that came while the extension waited is handled once
            // the session's loop turns again.
            kept_inbound.push_back(std::move(inbound));
        }
        return true;
    }

    bool sendProtocolRequestAwaitResponse(const std::string& type, const QCborMap& fields,
                                          an::ui::extensions::ExtensionProtocolMessage* out_response,
                                          std::string* error)
    {
        if (!out_response)
        {
            if (error)
            {
                *error = "Protocol response output is null";
            }
            return false;
        }
        if (!hasControlSocket())
        {
            if (error)
            {
                *error = "The lite host bridge has no active control socket";
            }
            return false;
        }

        an::ui::extensions::ExtensionProtocolMessage request;
        request.type = type;
        request.session_id = session_id.toStdString();
        request.request_id = generateLiteOpaqueToken();
        request.fields = fields;
        if (!writeProtocolMessage(*socket, request, error))
        {
            return false;
        }

        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < timeout_ms)
        {
            an::ui::extensions::ExtensionProtocolMessage deferred_response;
            if (takeDeferredResponse(request.request_id, &deferred_response))
            {
                return completeProtocolResponse(std::move(deferred_response), out_response, error);
            }

            while (true)
            {
                an::ui::extensions::ExtensionProtocolMessage inbound;
                std::string local_error;
                const auto status =
                    an::ui::extensions::tryConsumeFramedProtocolMessage(buffered_input, &inbound, &local_error);
                if (status == an::ui::extensions::ProtocolFrameDecodeStatus::NeedMoreData)
                {
                    break;
                }
                if (status == an::ui::extensions::ProtocolFrameDecodeStatus::InvalidFrame)
                {
                    if (error)
                    {
                        *error = local_error.empty() ? "Invalid protocol frame" : local_error;
                    }
                    return false;
                }
                bool matched_response = false;
                if (!handleInboundWhileAwaitingResponse(request, std::move(inbound), out_response, &matched_response,
                                                        error))
                {
                    return false;
                }
                if (matched_response)
                {
                    return true;
                }
            }

            const int remaining_ms = (std::max)(1, timeout_ms - static_cast<int>(timer.elapsed()));
            if (!socket->waitForReadyRead((std::min)(remaining_ms, 50)))
            {
                continue;
            }
            buffered_input->append(socket->readAll());
        }

        if (error)
        {
            *error = "Timed out waiting for " + type + " response";
        }
        return false;
    }

    LiteImageBinding* findImageBinding(const std::string& token)
    {
        const auto it = image_bindings.find(token);
        return it == image_bindings.end() ? nullptr : &it->second;
    }
};

LitePythonHostBridge* g_lite_python_bridge = nullptr;

bool LitePythonHostBridge::tell(const an::ui::extensions::ExtensionProtocolMessage& message, std::string* error)
{
    if (!hasControlSocket())
    {
        if (error)
        {
            *error = "The lite host bridge has no active control socket";
        }
        return false;
    }
    return writeProtocolMessage(*socket, message, error);
}

void LitePythonHostBridge::pumpWhileRunning()
{
    if (!hasControlSocket() || (pump_clock.isValid() && pump_clock.elapsed() < 20))
    {
        return;
    }
    pump_clock.start();
    socket->waitForReadyRead(1);
    buffered_input->append(socket->readAll());
    for (;;)
    {
        an::ui::extensions::ExtensionProtocolMessage inbound;
        std::string error;
        if (an::ui::extensions::tryConsumeFramedProtocolMessage(buffered_input, &inbound, &error) !=
            an::ui::extensions::ProtocolFrameDecodeStatus::Complete)
        {
            return;
        }
        if (inbound.type == "runtime.ping")
        {
            an::ui::extensions::RuntimePingMessage ping;
            if (an::ui::extensions::runtimePingFromProtocolMessage(inbound, &ping))
            {
                an::ui::extensions::RuntimePongMessage pong;
                pong.session_id = ping.session_id;
                pong.request_id = ping.request_id;
                pong.sequence = ping.sequence;
                static_cast<void>(writePong(*socket, pong, nullptr));
            }
            continue;
        }
        if (inbound.type == "execution.cancel")
        {
            // The run under way stops at the extension's next look; a cancel of a request not yet run is answered when
            // the run ends.
            an::ui::extensions::ExecutionCancelMessage cancel;
            const bool parsed = an::ui::extensions::executionCancelFromProtocolMessage(inbound, &cancel);
            if (!panel_request.has_value() || (parsed && cancel.target_request_id == panel_request->request_id))
            {
                cancel_requested = true;
                continue;
            }
        }
        else if (inbound.type == "theme.describe" || inbound.type == "theme.changed")
        {
            takeTheme(inbound);
            continue;
        }
        kept_inbound.push_back(std::move(inbound));
    }
}

PyObject* pyRaiseRuntimeError(const std::string& message)
{
    PyErr_SetString(PyExc_RuntimeError, message.c_str());
    return nullptr;
}

PyObject* liteHostSessionInfo(PyObject*, PyObject*)
{
    const QCborMap info = g_lite_python_bridge ? g_lite_python_bridge->sessionInfo() : QCborMap{};
    return cborMapToPythonDict(info);
}

PyObject* liteHostSessionPaths(PyObject*, PyObject*)
{
    return cborMapToPythonDict(
        QCborMap{{QStringLiteral("temp_dir"), QString{}}, {QStringLiteral("artifacts_dir"), QString{}}});
}

PyObject* liteHostHasControlSocket(PyObject*, PyObject*)
{
    if (g_lite_python_bridge && g_lite_python_bridge->hasControlSocket())
    {
        Py_RETURN_TRUE;
    }
    Py_RETURN_FALSE;
}

PyObject* liteHostLogEvent(PyObject*, PyObject* args)
{
    const char* severity = nullptr;
    const char* component = nullptr;
    const char* message = nullptr;
    if (!PyArg_ParseTuple(args, "sss:log_event", &severity, &component, &message))
    {
        return nullptr;
    }
    if (!g_lite_python_bridge || !g_lite_python_bridge->hasControlSocket())
    {
        Py_RETURN_NONE;
    }

    an::ui::extensions::ExtensionProtocolMessage response;
    std::string error;
    if (!g_lite_python_bridge->sendProtocolRequestAwaitResponse(
            "logging.event",
            QCborMap{{QStringLiteral("severity"), QString::fromUtf8(severity)},
                     {QStringLiteral("component"), QString::fromUtf8(component)},
                     {QStringLiteral("message"), QString::fromUtf8(message)}},
            &response, &error))
    {
        return pyRaiseRuntimeError(error);
    }
    Py_RETURN_NONE;
}

PyObject* liteHostListViews(PyObject*, PyObject*)
{
    if (!g_lite_python_bridge || !g_lite_python_bridge->hasControlSocket())
    {
        return PyList_New(0);
    }

    an::ui::extensions::ExtensionProtocolMessage response;
    std::string error;
    if (!g_lite_python_bridge->sendProtocolRequestAwaitResponse("views.describe", {}, &response, &error))
    {
        return pyRaiseRuntimeError(error);
    }

    PyObjectHolder views(cborValueToPythonObject(response.fields.value(QStringLiteral("views"))));
    if (!views.value)
    {
        return nullptr;
    }
    if (!PyList_Check(views.value))
    {
        return PyList_New(0);
    }
    return views.release();
}

std::size_t bytesPerPixelComponent(const an::ui::extensions::SharedBufferPixelType pixel_type) noexcept
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
    return 0;
}

const char* numpyDtypeName(const an::ui::extensions::SharedBufferPixelType pixel_type) noexcept
{
    using PixelType = an::ui::extensions::SharedBufferPixelType;
    switch (pixel_type)
    {
    case PixelType::UInt8:
        return "uint8";
    case PixelType::UInt16:
        return "uint16";
    case PixelType::Float32:
        return "float32";
    case PixelType::Float64:
        return "float64";
    }
    return nullptr;
}

std::filesystem::path liteSharedBufferRootPath()
{
    std::error_code ec;
    std::filesystem::path temp_path = std::filesystem::temp_directory_path(ec);
    if (ec || temp_path.empty())
    {
        temp_path = std::filesystem::current_path(ec);
    }
    if (ec || temp_path.empty())
    {
        temp_path = ".";
    }
    return (temp_path / "AfterNight" / "LiteSharedBuffers").lexically_normal();
}

struct ScopedPyBuffer
{
    Py_buffer view{};
    bool acquired = false;

    ScopedPyBuffer() = default;
    ScopedPyBuffer(const ScopedPyBuffer&) = delete;
    ScopedPyBuffer& operator=(const ScopedPyBuffer&) = delete;
    ~ScopedPyBuffer()
    {
        if (acquired)
        {
            PyBuffer_Release(&view);
        }
    }
};

struct LiteGeneratedImageSpec
{
    int width = 0;
    int height = 0;
    int channels = 0;
    an::ui::extensions::SharedBufferPixelType pixel_type = an::ui::extensions::SharedBufferPixelType::Float32;
    an::ui::extensions::SharedBufferPixelFormat pixel_format = an::ui::extensions::SharedBufferPixelFormat::Mono;
    an::ui::extensions::SharedBufferColorSpace color_space = an::ui::extensions::SharedBufferColorSpace::Linear;
};

std::optional<an::ui::extensions::SharedBufferPixelType>
sharedBufferPixelTypeFromPyBuffer(const Py_buffer& view) noexcept
{
    if (!view.format || std::strlen(view.format) == 0)
    {
        return std::nullopt;
    }

    switch (view.format[std::strlen(view.format) - 1])
    {
    case 'B':
        return an::ui::extensions::SharedBufferPixelType::UInt8;
    case 'H':
        return an::ui::extensions::SharedBufferPixelType::UInt16;
    case 'f':
        return an::ui::extensions::SharedBufferPixelType::Float32;
    case 'd':
        return an::ui::extensions::SharedBufferPixelType::Float64;
    default:
        break;
    }

    return std::nullopt;
}

bool acquireImageBufferView(PyObject* object, PyObjectHolder* out_owner, ScopedPyBuffer* out_buffer, std::string* error)
{
    if (!object || !out_buffer)
    {
        if (error)
        {
            *error = "ui.open_image() received a null Python object";
        }
        return false;
    }

    if (PyObject_GetBuffer(object, &out_buffer->view, PyBUF_FORMAT | PyBUF_STRIDES) == 0)
    {
        out_buffer->acquired = true;
        return true;
    }
    PyErr_Clear();

    PyObjectHolder numpy(PyImport_ImportModule("numpy"));
    if (!numpy.value)
    {
        if (error)
        {
            *error = pythonExceptionMessage();
        }
        return false;
    }
    PyObjectHolder asarray(PyObject_GetAttrString(numpy.value, "asarray"));
    if (!asarray.value)
    {
        if (error)
        {
            *error = pythonExceptionMessage();
        }
        return false;
    }
    PyObjectHolder converted(PyObject_CallFunctionObjArgs(asarray.value, object, nullptr));
    if (!converted.value)
    {
        if (error)
        {
            *error = pythonExceptionMessage();
        }
        return false;
    }
    if (PyObject_GetBuffer(converted.value, &out_buffer->view, PyBUF_FORMAT | PyBUF_STRIDES) != 0)
    {
        if (error)
        {
            *error = pythonExceptionMessage();
        }
        return false;
    }

    out_buffer->acquired = true;
    if (out_owner)
    {
        *out_owner = std::move(converted);
    }
    return true;
}

bool parseGeneratedImageSpec(const Py_buffer& view, LiteGeneratedImageSpec* out_spec, std::string* error)
{
    if (!out_spec)
    {
        if (error)
        {
            *error = "Generated image spec output is null";
        }
        return false;
    }
    if (view.ndim != 2 && view.ndim != 3)
    {
        if (error)
        {
            *error = "ui.open_image() requires a 2D mono or 3D color NumPy-compatible array";
        }
        return false;
    }
    if (!view.shape)
    {
        if (error)
        {
            *error = "ui.open_image() requires an array exposing shape information";
        }
        return false;
    }

    const auto pixel_type = sharedBufferPixelTypeFromPyBuffer(view);
    if (!pixel_type.has_value())
    {
        if (error)
        {
            *error = "ui.open_image() requires uint8/uint16/float32/float64 NumPy-compatible arrays";
        }
        return false;
    }
    const std::size_t item_size = bytesPerPixelComponent(*pixel_type);
    if (item_size == 0 || view.itemsize != static_cast<Py_ssize_t>(item_size))
    {
        if (error)
        {
            *error = "ui.open_image() array item size does not match its pixel format";
        }
        return false;
    }

    LiteGeneratedImageSpec spec;
    spec.pixel_type = *pixel_type;
    if (view.ndim == 2)
    {
        spec.height = static_cast<int>(view.shape[0]);
        spec.width = static_cast<int>(view.shape[1]);
        spec.channels = 1;
        spec.pixel_format = an::ui::extensions::SharedBufferPixelFormat::Mono;
        spec.color_space = an::ui::extensions::SharedBufferColorSpace::Grayscale;
    }
    else
    {
        const Py_ssize_t first = view.shape[0];
        const Py_ssize_t second = view.shape[1];
        const Py_ssize_t third = view.shape[2];
        if (third == 1 || third == 3)
        {
            spec.height = static_cast<int>(first);
            spec.width = static_cast<int>(second);
            spec.channels = static_cast<int>(third);
        }
        else if (first == 1 || first == 3)
        {
            spec.channels = static_cast<int>(first);
            spec.height = static_cast<int>(second);
            spec.width = static_cast<int>(third);
        }
        else
        {
            if (error)
            {
                *error = "ui.open_image() requires mono or RGB arrays in (H, W, C) or (C, H, W) layout";
            }
            return false;
        }
        spec.pixel_format = spec.channels == 1 ? an::ui::extensions::SharedBufferPixelFormat::Mono
                                               : an::ui::extensions::SharedBufferPixelFormat::RGB;
        spec.color_space = spec.channels == 1 ? an::ui::extensions::SharedBufferColorSpace::Grayscale
                                              : an::ui::extensions::SharedBufferColorSpace::Linear;
    }

    if (spec.width <= 0 || spec.height <= 0 || spec.channels <= 0)
    {
        if (error)
        {
            *error = "ui.open_image() array dimensions must be positive";
        }
        return false;
    }

    *out_spec = spec;
    return true;
}

std::array<Py_ssize_t, 3> pyBufferStrides(const Py_buffer& view)
{
    std::array<Py_ssize_t, 3> strides{0, 0, 0};
    if (view.strides)
    {
        for (int i = 0; i < view.ndim && i < static_cast<int>(strides.size()); ++i)
        {
            strides[static_cast<std::size_t>(i)] = view.strides[i];
        }
        return strides;
    }

    Py_ssize_t stride = view.itemsize;
    for (int i = view.ndim - 1; i >= 0; --i)
    {
        strides[static_cast<std::size_t>(i)] = stride;
        stride *= view.shape[i];
    }
    return strides;
}

bool copyPyBufferIntoPlanarBuffer(const Py_buffer& view, const an::ui::extensions::SharedBufferDescriptor& descriptor,
                                  std::byte* destination, std::string* error)
{
    if (!destination)
    {
        if (error)
        {
            *error = "Shared buffer destination is null";
        }
        return false;
    }

    const auto strides = pyBufferStrides(view);
    const auto item_size = static_cast<std::size_t>(view.itemsize);
    const auto expected_item_size = bytesPerPixelComponent(descriptor.pixel_type);
    const auto width = static_cast<std::size_t>(descriptor.width);
    const auto height = static_cast<std::size_t>(descriptor.height);
    const auto channels = static_cast<std::size_t>(descriptor.channels);

    if (expected_item_size == 0 || item_size != expected_item_size)
    {
        if (error)
        {
            *error = "NumPy array dtype does not match the destination image pixel type";
        }
        return false;
    }

    if (view.ndim == 2)
    {
        if (static_cast<std::size_t>(view.shape[0]) != height || static_cast<std::size_t>(view.shape[1]) != width)
        {
            if (error)
            {
                *error = "NumPy array shape does not match the destination image";
            }
            return false;
        }

        for (std::size_t y = 0; y < height; ++y)
        {
            for (std::size_t x = 0; x < width; ++x)
            {
                const auto* source = static_cast<const std::byte*>(view.buf) + static_cast<Py_ssize_t>(y) * strides[0] +
                                     static_cast<Py_ssize_t>(x) * strides[1];
                std::memcpy(destination + y * descriptor.row_stride + x * item_size, source, item_size);
            }
        }
        return true;
    }

    const bool hwc = static_cast<std::size_t>(view.shape[0]) == height &&
                     static_cast<std::size_t>(view.shape[1]) == width &&
                     static_cast<std::size_t>(view.shape[2]) == channels;
    const bool chw = static_cast<std::size_t>(view.shape[0]) == channels &&
                     static_cast<std::size_t>(view.shape[1]) == height &&
                     static_cast<std::size_t>(view.shape[2]) == width;
    if (!hwc && !chw)
    {
        if (error)
        {
            *error = "3D NumPy arrays must use (H, W, C) or (C, H, W) shape";
        }
        return false;
    }

    for (std::size_t c = 0; c < channels; ++c)
    {
        std::byte* destination_plane = destination + c * descriptor.plane_stride;
        for (std::size_t y = 0; y < height; ++y)
        {
            for (std::size_t x = 0; x < width; ++x)
            {
                const std::byte* source = static_cast<const std::byte*>(view.buf);
                if (hwc)
                {
                    source += static_cast<Py_ssize_t>(y) * strides[0] + static_cast<Py_ssize_t>(x) * strides[1] +
                              static_cast<Py_ssize_t>(c) * strides[2];
                }
                else
                {
                    source += static_cast<Py_ssize_t>(c) * strides[0] + static_cast<Py_ssize_t>(y) * strides[1] +
                              static_cast<Py_ssize_t>(x) * strides[2];
                }

                std::memcpy(destination_plane + y * descriptor.row_stride + x * item_size, source, item_size);
            }
        }
    }
    return true;
}

bool pythonObjectToJsonValue(PyObject* object, QJsonValue* out_value, std::string* error, const int depth = 0)
{
    if (!out_value)
    {
        if (error)
        {
            *error = "Python to JSON output is null";
        }
        return false;
    }
    if (depth > 64)
    {
        if (error)
        {
            *error = "Python metadata value is nested too deeply";
        }
        return false;
    }
    if (!object || object == Py_None)
    {
        *out_value = QJsonValue();
        return true;
    }
    if (PyBool_Check(object))
    {
        *out_value = QJsonValue(object == Py_True);
        return true;
    }
    if (PyLong_Check(object))
    {
        const long long value = PyLong_AsLongLong(object);
        if (PyErr_Occurred())
        {
            PyErr_Clear();
            PyObjectHolder text(PyObject_Str(object));
            if (!text.value)
            {
                if (error)
                {
                    *error = pythonExceptionMessage();
                }
                return false;
            }
            const char* value_text = PyUnicode_AsUTF8(text.value);
            if (!value_text)
            {
                if (error)
                {
                    *error = pythonExceptionMessage();
                }
                return false;
            }
            *out_value = QString::fromUtf8(value_text);
            return true;
        }

        *out_value = QJsonValue(static_cast<double>(value));
        return true;
    }
    if (PyFloat_Check(object))
    {
        const double value = PyFloat_AsDouble(object);
        *out_value = std::isfinite(value) ? QJsonValue(value) : QJsonValue();
        return true;
    }
    if (PyUnicode_Check(object))
    {
        const char* value_text = PyUnicode_AsUTF8(object);
        if (!value_text)
        {
            if (error)
            {
                *error = pythonExceptionMessage();
            }
            return false;
        }
        *out_value = QString::fromUtf8(value_text);
        return true;
    }
    if (PyList_Check(object) || PyTuple_Check(object))
    {
        QJsonArray array;
        const Py_ssize_t size = PySequence_Size(object);
        for (Py_ssize_t index = 0; index < size; ++index)
        {
            PyObjectHolder item(PySequence_GetItem(object, index));
            QJsonValue value;
            if (!item.value || !pythonObjectToJsonValue(item.value, &value, error, depth + 1))
            {
                return false;
            }
            array.push_back(value);
        }
        *out_value = array;
        return true;
    }
    if (PyDict_Check(object))
    {
        QJsonObject json_object;
        PyObject* key = nullptr;
        PyObject* value = nullptr;
        Py_ssize_t pos = 0;
        while (PyDict_Next(object, &pos, &key, &value))
        {
            if (!PyUnicode_Check(key))
            {
                if (error)
                {
                    *error = "Metadata object keys must be strings";
                }
                return false;
            }

            const char* key_text = PyUnicode_AsUTF8(key);
            if (!key_text)
            {
                if (error)
                {
                    *error = pythonExceptionMessage();
                }
                return false;
            }

            QJsonValue json_value;
            if (!pythonObjectToJsonValue(value, &json_value, error, depth + 1))
            {
                return false;
            }
            json_object.insert(QString::fromUtf8(key_text), json_value);
        }
        *out_value = json_object;
        return true;
    }

    PyObjectHolder text(PyObject_Str(object));
    if (!text.value)
    {
        if (error)
        {
            *error = pythonExceptionMessage();
        }
        return false;
    }
    const char* value_text = PyUnicode_AsUTF8(text.value);
    if (!value_text)
    {
        if (error)
        {
            *error = pythonExceptionMessage();
        }
        return false;
    }
    *out_value = QString::fromUtf8(value_text);
    return true;
}

bool writeMetadataSidecarDict(const std::string& sidecar_path, PyObject* metadata, std::string* error)
{
    if (sidecar_path.empty() || !metadata || metadata == Py_None)
    {
        return true;
    }
    if (!PyDict_Check(metadata))
    {
        if (error)
        {
            *error = "ui.open_image() metadata must be a dict";
        }
        return false;
    }

    QJsonObject root;
    PyObject* key = nullptr;
    PyObject* value = nullptr;
    Py_ssize_t pos = 0;
    while (PyDict_Next(metadata, &pos, &key, &value))
    {
        if (!PyUnicode_Check(key))
        {
            if (error)
            {
                *error = "ui.open_image() metadata keys must be strings";
            }
            return false;
        }
        const char* key_text = PyUnicode_AsUTF8(key);
        if (!key_text)
        {
            if (error)
            {
                *error = pythonExceptionMessage();
            }
            return false;
        }

        QJsonValue json_value;
        if (!pythonObjectToJsonValue(value, &json_value, error))
        {
            return false;
        }
        root.insert(QString::fromUtf8(key_text), json_value);
    }

    QFile file(QString::fromStdString(sidecar_path));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        if (error)
        {
            *error = "Failed to open image metadata sidecar for writing";
        }
        return false;
    }

    const QByteArray document = QJsonDocument(root).toJson(QJsonDocument::Indented);
    if (file.write(document) != document.size())
    {
        if (error)
        {
            *error = "Failed to write image metadata sidecar";
        }
        return false;
    }
    return true;
}

bool materializeGeneratedImageBuffer(PyObject* image_object, PyObject* metadata, const std::string& buffer_suffix,
                                     const std::string& symbolic_name,
                                     const an::ui::extensions::SharedBufferRole buffer_role,
                                     an::ui::extensions::SharedBufferDescriptor* out_descriptor,
                                     an::ui::extensions::SharedBufferFileMapping* out_mapping, std::string* error)
{
    if (!g_lite_python_bridge || !out_descriptor || !out_mapping)
    {
        if (error)
        {
            *error = "The lite host bridge is unavailable";
        }
        return false;
    }

    PyObjectHolder converted_owner;
    ScopedPyBuffer source_buffer;
    if (!acquireImageBufferView(image_object, &converted_owner, &source_buffer, error))
    {
        return false;
    }

    LiteGeneratedImageSpec generated_spec;
    if (!parseGeneratedImageSpec(source_buffer.view, &generated_spec, error))
    {
        return false;
    }

    const std::string buffer_id = generateLiteOpaqueToken() + buffer_suffix;
    const std::string session_id = g_lite_python_bridge->session_id.toStdString();
    const std::filesystem::path root_path = liteSharedBufferRootPath();
    const std::filesystem::path buffer_file_path = root_path / session_id / (buffer_id + ".bin");

    an::ui::extensions::SharedBufferFileSpec output_spec;
    output_spec.session_id = session_id;
    output_spec.buffer_id = buffer_id;
    output_spec.symbolic_name = symbolic_name;
    output_spec.width = generated_spec.width;
    output_spec.height = generated_spec.height;
    output_spec.channels = generated_spec.channels;
    output_spec.pixel_type = generated_spec.pixel_type;
    output_spec.pixel_format = generated_spec.pixel_format;
    output_spec.color_space = generated_spec.color_space;
    output_spec.access_mode = an::ui::extensions::SharedBufferAccessMode::ReadWrite;
    output_spec.buffer_role = buffer_role;
    if (metadata && metadata != Py_None && PyDict_Check(metadata) && PyDict_Size(metadata) > 0)
    {
        auto sidecar_path = buffer_file_path;
        sidecar_path += ".metadata.json";
        output_spec.metadata_sidecar = sidecar_path.lexically_normal().generic_string();
    }

    an::ui::extensions::SharedBufferFileMapper mapper(root_path);
    an::ui::extensions::SharedBufferFileMapping output_mapping;
    if (!mapper.allocateBuffer(output_spec, &output_mapping, error))
    {
        return false;
    }
    if (!copyPyBufferIntoPlanarBuffer(source_buffer.view, output_mapping.descriptor(), output_mapping.bytes(), error))
    {
        return false;
    }
    if (!writeMetadataSidecarDict(output_mapping.descriptor().metadata_sidecar, metadata, error))
    {
        return false;
    }

    *out_descriptor = output_mapping.descriptor();
    *out_mapping = std::move(output_mapping);
    return true;
}

PyObject* pyTupleFromSsizeValues(const std::vector<Py_ssize_t>& values)
{
    PyObjectHolder tuple(PyTuple_New(static_cast<Py_ssize_t>(values.size())));
    if (!tuple.value)
    {
        return nullptr;
    }
    for (Py_ssize_t index = 0; index < static_cast<Py_ssize_t>(values.size()); ++index)
    {
        PyObject* value = PyLong_FromSsize_t(values[static_cast<std::size_t>(index)]);
        if (!value)
        {
            return nullptr;
        }
        PyTuple_SET_ITEM(tuple.value, index, value);
    }
    return tuple.release();
}

bool bindSnapshotBuffer(const an::ui::extensions::SharedBufferDescriptor& descriptor, const QCborMap& metadata,
                        const std::string& token, std::string* error)
{
    if (!g_lite_python_bridge)
    {
        if (error)
        {
            *error = "The lite host bridge is unavailable";
        }
        return false;
    }

    std::filesystem::path root_path;
    if (!an::ui::extensions::sharedBufferRootPathForDescriptor(descriptor, &root_path, error))
    {
        return false;
    }

    an::ui::extensions::SharedBufferFileMapper mapper(root_path);
    LiteImageBinding binding;
    binding.descriptor = descriptor;
    binding.metadata = metadata;
    binding.writable = descriptor.access_mode == an::ui::extensions::SharedBufferAccessMode::ReadWrite;
    if (!mapper.openBuffer(descriptor, &binding.mapping, error))
    {
        return false;
    }

    g_lite_python_bridge->image_bindings.insert_or_assign(token, std::move(binding));
    return true;
}

PyObject* liteHostViewSnapshot(PyObject*, PyObject* args)
{
    const char* view_name = nullptr;
    if (!PyArg_ParseTuple(args, "s:view_snapshot", &view_name))
    {
        return nullptr;
    }
    if (!g_lite_python_bridge || !g_lite_python_bridge->hasControlSocket())
    {
        return cborMapToPythonDict(QCborMap{});
    }

    QCborMap fields;
    if (view_name && std::strlen(view_name) > 0)
    {
        fields[QStringLiteral("view_name")] = QString::fromUtf8(view_name);
    }

    an::ui::extensions::ExtensionProtocolMessage response;
    std::string error;
    if (!g_lite_python_bridge->sendProtocolRequestAwaitResponse("views.snapshot", fields, &response, &error))
    {
        return pyRaiseRuntimeError(error);
    }

    an::ui::extensions::SharedBufferDescriptor source_descriptor;
    if (!an::ui::extensions::sharedBufferDescriptorFromCborValue(response.fields.value(QStringLiteral("buffer")),
                                                                 &source_descriptor, &error))
    {
        return pyRaiseRuntimeError(error.empty() ? "Host snapshot response returned an invalid buffer" : error);
    }

    const QString resolved_view_name = response.fields.value(QStringLiteral("view_name")).toString();
    const std::string source_token = "snapshot:source:" + source_descriptor.buffer_id;
    const QCborMap metadata = response.fields.value(QStringLiteral("metadata")).isMap()
                                  ? response.fields.value(QStringLiteral("metadata")).toMap()
                                  : QCborMap{};
    if (!bindSnapshotBuffer(source_descriptor, metadata, source_token, &error))
    {
        return pyRaiseRuntimeError(error.empty() ? "Failed to open snapshot source buffer" : error);
    }

    QCborMap snapshot;
    snapshot[QStringLiteral("view_id")] = resolved_view_name;
    snapshot[QStringLiteral("view_name")] = resolved_view_name;
    snapshot[QStringLiteral("source_token")] = QString::fromStdString(source_token);
    snapshot[QStringLiteral("metadata")] = metadata;
    snapshot[QStringLiteral("file_name")] = response.fields.value(QStringLiteral("file_name"));
    snapshot[QStringLiteral("width")] = source_descriptor.width;
    snapshot[QStringLiteral("height")] = source_descriptor.height;
    snapshot[QStringLiteral("channels")] = source_descriptor.channels;
    snapshot[QStringLiteral("pixel_type")] = QString::fromStdString(
        std::string(an::ui::extensions::sharedBufferPixelTypeName(source_descriptor.pixel_type)));

    QCborMap auxiliary_images;
    const QCborValue auxiliary_value = response.fields.value(QStringLiteral("auxiliary_buffers"));
    if (!auxiliary_value.isUndefined())
    {
        if (!auxiliary_value.isArray())
        {
            return pyRaiseRuntimeError("Host snapshot response returned invalid auxiliary_buffers");
        }

        const QCborArray auxiliary_array = auxiliary_value.toArray();
        for (const QCborValue& entry : auxiliary_array)
        {
            an::ui::extensions::SharedBufferDescriptor descriptor;
            if (!an::ui::extensions::sharedBufferDescriptorFromCborValue(entry, &descriptor, &error))
            {
                return pyRaiseRuntimeError(error.empty() ? "Host snapshot response returned an invalid auxiliary buffer"
                                                         : error);
            }

            const std::string symbolic_name =
                descriptor.symbolic_name.empty() ? descriptor.buffer_id : descriptor.symbolic_name;
            const std::string token = "snapshot:aux:" + descriptor.buffer_id;
            if (!bindSnapshotBuffer(descriptor, {}, token, &error))
            {
                return pyRaiseRuntimeError(error.empty() ? "Failed to open snapshot auxiliary buffer" : error);
            }

            auxiliary_images[QString::fromStdString(symbolic_name)] =
                QCborMap{{QStringLiteral("token"), QString::fromStdString(token)},
                         {QStringLiteral("buffer_role"),
                          QString::fromStdString(
                              std::string(an::ui::extensions::sharedBufferRoleName(descriptor.buffer_role)))}};
        }
    }
    snapshot[QStringLiteral("auxiliary_images")] = auxiliary_images;
    return cborMapToPythonDict(snapshot);
}

PyObject* liteHostImageToNumpy(PyObject*, PyObject* args)
{
    const char* token = nullptr;
    if (!PyArg_ParseTuple(args, "s:image_to_numpy", &token))
    {
        return nullptr;
    }
    if (!g_lite_python_bridge || !token)
    {
        return pyRaiseRuntimeError("The lite host bridge is unavailable");
    }

    LiteImageBinding* binding = g_lite_python_bridge->findImageBinding(token);
    if (!binding || !binding->mapping.valid())
    {
        return pyRaiseRuntimeError("Image binding is not backed by a valid shared buffer");
    }

    const auto& descriptor = binding->descriptor;
    const std::size_t item_size = bytesPerPixelComponent(descriptor.pixel_type);
    const char* dtype_name = numpyDtypeName(descriptor.pixel_type);
    if (item_size == 0 || !dtype_name)
    {
        return pyRaiseRuntimeError("Unsupported shared-buffer pixel type");
    }

    std::vector<Py_ssize_t> shape;
    std::vector<Py_ssize_t> strides;
    if (descriptor.channels <= 1)
    {
        shape = {static_cast<Py_ssize_t>(descriptor.height), static_cast<Py_ssize_t>(descriptor.width)};
        strides = {static_cast<Py_ssize_t>(descriptor.row_stride), static_cast<Py_ssize_t>(item_size)};
    }
    else
    {
        shape = {static_cast<Py_ssize_t>(descriptor.height), static_cast<Py_ssize_t>(descriptor.width),
                 static_cast<Py_ssize_t>(descriptor.channels)};
        strides = {static_cast<Py_ssize_t>(descriptor.row_stride), static_cast<Py_ssize_t>(item_size),
                   static_cast<Py_ssize_t>(descriptor.plane_stride)};
    }

    PyObjectHolder numpy(PyImport_ImportModule("numpy"));
    if (!numpy.value)
    {
        return nullptr;
    }
    PyObjectHolder ndarray(PyObject_GetAttrString(numpy.value, "ndarray"));
    if (!ndarray.value)
    {
        return nullptr;
    }
    PyObjectHolder empty_args(PyTuple_New(0));
    PyObjectHolder kwargs(PyDict_New());
    PyObjectHolder shape_tuple(pyTupleFromSsizeValues(shape));
    PyObjectHolder strides_tuple(pyTupleFromSsizeValues(strides));
    PyObjectHolder dtype(PyUnicode_FromString(dtype_name));
    PyObjectHolder memory(PyMemoryView_FromMemory(reinterpret_cast<char*>(binding->mapping.bytes()),
                                                  static_cast<Py_ssize_t>(binding->mapping.mapped_size()),
                                                  binding->writable ? PyBUF_WRITE : PyBUF_READ));
    if (!empty_args.value || !kwargs.value || !shape_tuple.value || !strides_tuple.value || !dtype.value ||
        !memory.value)
    {
        return nullptr;
    }
    if (PyDict_SetItemString(kwargs.value, "shape", shape_tuple.value) != 0 ||
        PyDict_SetItemString(kwargs.value, "dtype", dtype.value) != 0 ||
        PyDict_SetItemString(kwargs.value, "buffer", memory.value) != 0 ||
        PyDict_SetItemString(kwargs.value, "strides", strides_tuple.value) != 0)
    {
        return nullptr;
    }

    return PyObject_Call(ndarray.value, empty_args.value, kwargs.value);
}

PyObject* liteHostImageProperties(PyObject*, PyObject* args)
{
    const char* token = nullptr;
    if (!PyArg_ParseTuple(args, "s:image_properties", &token))
    {
        return nullptr;
    }
    if (!g_lite_python_bridge || !token)
    {
        return cborMapToPythonDict(QCborMap{});
    }

    const LiteImageBinding* binding = g_lite_python_bridge->findImageBinding(token);
    if (!binding)
    {
        return cborMapToPythonDict(QCborMap{});
    }

    const auto& descriptor = binding->descriptor;
    return cborMapToPythonDict(QCborMap{
        {QStringLiteral("width"), descriptor.width},
        {QStringLiteral("height"), descriptor.height},
        {QStringLiteral("channels"), descriptor.channels},
        {QStringLiteral("pixel_type"),
         QString::fromStdString(std::string(an::ui::extensions::sharedBufferPixelTypeName(descriptor.pixel_type)))},
        {QStringLiteral("pixel_format"),
         QString::fromStdString(std::string(an::ui::extensions::sharedBufferPixelFormatName(descriptor.pixel_format)))},
        {QStringLiteral("buffer_role"),
         QString::fromStdString(std::string(an::ui::extensions::sharedBufferRoleName(descriptor.buffer_role)))},
        {QStringLiteral("backend"), QStringLiteral("host")}});
}

PyObject* liteHostImageMetadata(PyObject*, PyObject* args)
{
    const char* token = nullptr;
    if (!PyArg_ParseTuple(args, "s:image_metadata", &token))
    {
        return nullptr;
    }
    if (!g_lite_python_bridge || !token)
    {
        return cborMapToPythonDict(QCborMap{});
    }

    const LiteImageBinding* binding = g_lite_python_bridge->findImageBinding(token);
    return cborMapToPythonDict(binding ? binding->metadata : QCborMap{});
}

PyObject* liteHostImageSetMetadata(PyObject*, PyObject* args)
{
    const char* token = nullptr;
    const char* key = nullptr;
    const char* value = nullptr;
    if (!PyArg_ParseTuple(args, "sss:image_set_metadata", &token, &key, &value))
    {
        return nullptr;
    }
    if (!g_lite_python_bridge || !token || !key)
    {
        return pyRaiseRuntimeError("The lite host bridge is unavailable");
    }

    LiteImageBinding* binding = g_lite_python_bridge->findImageBinding(token);
    if (!binding || !binding->mapping.valid())
    {
        return pyRaiseRuntimeError("Image binding is not backed by a valid shared buffer");
    }
    if (!binding->writable)
    {
        return pyRaiseRuntimeError("Metadata can only be updated on writable image handles");
    }

    binding->metadata[QString::fromUtf8(key)] = QString::fromUtf8(value ? value : "");
    Py_RETURN_NONE;
}

PyObject* liteHostImageClone(PyObject*, PyObject* args)
{
    const char* token = nullptr;
    if (!PyArg_ParseTuple(args, "s:image_clone", &token))
    {
        return nullptr;
    }
    if (!g_lite_python_bridge || !token)
    {
        return pyRaiseRuntimeError("The lite host bridge is unavailable");
    }

    const LiteImageBinding* source = g_lite_python_bridge->findImageBinding(token);
    if (!source || !source->mapping.valid())
    {
        return pyRaiseRuntimeError("Image binding is not backed by a valid shared buffer");
    }

    const auto& source_descriptor = source->descriptor;
    an::ui::extensions::SharedBufferFileSpec clone_spec;
    clone_spec.session_id = g_lite_python_bridge->session_id.toStdString();
    clone_spec.buffer_id = generateLiteOpaqueToken() + ".clone";
    clone_spec.symbolic_name =
        source_descriptor.symbolic_name.empty() ? std::string("clone") : source_descriptor.symbolic_name + ".clone";
    clone_spec.width = source_descriptor.width;
    clone_spec.height = source_descriptor.height;
    clone_spec.channels = source_descriptor.channels;
    clone_spec.pixel_type = source_descriptor.pixel_type;
    clone_spec.pixel_format = source_descriptor.pixel_format;
    clone_spec.color_space = source_descriptor.color_space;
    clone_spec.access_mode = an::ui::extensions::SharedBufferAccessMode::ReadWrite;
    clone_spec.buffer_role = an::ui::extensions::SharedBufferRole::Destination;
    clone_spec.offset_x = source_descriptor.offset_x;
    clone_spec.offset_y = source_descriptor.offset_y;
    clone_spec.source_width = source_descriptor.source_width;
    clone_spec.source_height = source_descriptor.source_height;

    an::ui::extensions::SharedBufferFileMapper mapper(liteSharedBufferRootPath());
    LiteImageBinding clone;
    clone.metadata = source->metadata;
    clone.writable = true;
    std::string error;
    if (!mapper.allocateBuffer(clone_spec, &clone.mapping, &error))
    {
        return pyRaiseRuntimeError(error.empty() ? "Failed to allocate cloned image buffer" : error);
    }
    if (clone.mapping.mapped_size() < source->mapping.mapped_size())
    {
        return pyRaiseRuntimeError("Cloned image buffer is smaller than the source binding");
    }
    std::memcpy(clone.mapping.bytes(), source->mapping.bytes(),
                static_cast<std::size_t>(source->mapping.mapped_size()));
    clone.descriptor = clone.mapping.descriptor();

    const std::string clone_token = "clone:" + clone.descriptor.buffer_id;
    g_lite_python_bridge->image_bindings.insert_or_assign(clone_token, std::move(clone));
    return PyUnicode_FromString(clone_token.c_str());
}

PyObject* liteHostImageFromNumpy(PyObject*, PyObject* args)
{
    const char* token = nullptr;
    PyObject* array = nullptr;
    if (!PyArg_ParseTuple(args, "sO:image_from_numpy", &token, &array))
    {
        return nullptr;
    }
    (void)array;
    if (!g_lite_python_bridge || !token || !g_lite_python_bridge->findImageBinding(token))
    {
        return pyRaiseRuntimeError("Image binding is not backed by a valid shared buffer");
    }

    LiteImageBinding* binding = g_lite_python_bridge->findImageBinding(token);
    if (!binding || !binding->mapping.valid())
    {
        return pyRaiseRuntimeError("Image binding is not backed by a valid shared buffer");
    }
    if (!binding->writable)
    {
        return pyRaiseRuntimeError("Destination image handle is read-only");
    }

    PyObjectHolder converted_owner;
    ScopedPyBuffer source_buffer;
    std::string error;
    if (!acquireImageBufferView(array, &converted_owner, &source_buffer, &error))
    {
        return pyRaiseRuntimeError(error.empty() ? "Failed to read ImageHandle.from_numpy() array" : error);
    }
    if (!copyPyBufferIntoPlanarBuffer(source_buffer.view, binding->descriptor, binding->mapping.bytes(), &error))
    {
        return pyRaiseRuntimeError(error.empty() ? "Failed to copy ImageHandle.from_numpy() pixels" : error);
    }
    Py_RETURN_NONE;
}

PyObject* liteHostUiOpenImage(PyObject*, PyObject* args)
{
    PyObject* image_object = nullptr;
    PyObject* metadata = nullptr;
    const char* title = nullptr;
    if (!PyArg_ParseTuple(args, "OOs:ui_open_image", &image_object, &metadata, &title))
    {
        return nullptr;
    }
    if (!g_lite_python_bridge || !g_lite_python_bridge->hasControlSocket())
    {
        Py_RETURN_FALSE;
    }

    std::string error;
    an::ui::extensions::SharedBufferFileMapping output_mapping;
    an::ui::extensions::SharedBufferDescriptor descriptor;
    if (!materializeGeneratedImageBuffer(image_object, metadata, ".ui_open_image", "generated_image",
                                         an::ui::extensions::SharedBufferRole::Destination, &descriptor,
                                         &output_mapping, &error))
    {
        return pyRaiseRuntimeError(error.empty() ? "Failed to materialize ui.open_image() shared buffer" : error);
    }

    an::ui::extensions::ExtensionProtocolMessage response;
    if (!g_lite_python_bridge->sendProtocolRequestAwaitResponse(
            "ui.open_image",
            QCborMap{{QStringLiteral("buffer"), an::ui::extensions::sharedBufferDescriptorToCborMap(descriptor)},
                     {QStringLiteral("title"), QString::fromUtf8(title ? title : "Extension Image")}},
            &response, &error))
    {
        return pyRaiseRuntimeError(error.empty() ? "ui.open_image failed" : error);
    }

    LiteImageBinding binding;
    binding.descriptor = descriptor;
    binding.mapping = std::move(output_mapping);
    binding.writable = true;
    g_lite_python_bridge->image_bindings.insert_or_assign("generated:" + descriptor.buffer_id, std::move(binding));
    Py_RETURN_TRUE;
}

PyObject* liteHostViewCommitNumpy(PyObject*, PyObject* args)
{
    PyObject* image_object = nullptr;
    PyObject* metadata = nullptr;
    const char* history_step_name = nullptr;
    const char* history_step_description = nullptr;
    int allow_resized_output = 0;
    if (!PyArg_ParseTuple(args, "OOssp:view_commit_numpy", &image_object, &metadata, &history_step_name,
                          &history_step_description, &allow_resized_output))
    {
        return nullptr;
    }
    if (!g_lite_python_bridge || !g_lite_python_bridge->hasControlSocket())
    {
        Py_RETURN_FALSE;
    }

    std::string error;
    an::ui::extensions::SharedBufferFileMapping output_mapping;
    an::ui::extensions::SharedBufferDescriptor descriptor;
    if (!materializeGeneratedImageBuffer(image_object, metadata, ".manual_commit", "destination",
                                         an::ui::extensions::SharedBufferRole::Destination, &descriptor,
                                         &output_mapping, &error))
    {
        return pyRaiseRuntimeError(error.empty() ? "Failed to materialize view.commit shared buffer" : error);
    }

    an::ui::extensions::ViewCommitMessage view_commit;
    view_commit.session_id = g_lite_python_bridge->session_id.toStdString();
    view_commit.request_id = generateLiteOpaqueToken();
    view_commit.view_id = view_commit.session_id;
    view_commit.history_step_name =
        history_step_name && std::strlen(history_step_name) > 0 ? history_step_name : "Extension Apply";
    view_commit.history_step_description = history_step_description && std::strlen(history_step_description) > 0
                                               ? history_step_description
                                               : "Applied extension execution result";
    view_commit.allow_resized_output = allow_resized_output != 0;
    view_commit.result_buffers.push_back(descriptor);

    if (!writeProtocolMessage(*g_lite_python_bridge->socket, an::ui::extensions::toProtocolMessage(view_commit),
                              &error))
    {
        return pyRaiseRuntimeError(error.empty() ? "view.commit failed" : error);
    }

    LiteImageBinding binding;
    binding.descriptor = descriptor;
    binding.mapping = std::move(output_mapping);
    binding.writable = true;
    g_lite_python_bridge->image_bindings.insert_or_assign("commit:" + descriptor.buffer_id, std::move(binding));
    Py_RETURN_TRUE;
}

PyObject* liteHostSettingsGet(PyObject*, PyObject* args)
{
    const char* key = nullptr;
    PyObject* default_object = Py_None;
    if (!PyArg_ParseTuple(args, "sO:settings_get", &key, &default_object))
    {
        return nullptr;
    }
    if (!g_lite_python_bridge || !g_lite_python_bridge->hasControlSocket())
    {
        Py_INCREF(default_object);
        return default_object;
    }
    // What the panel's tool configuration resolved answers first, as the window's always did.
    if (const auto told = g_lite_python_bridge->hosted_tool_record.constFind(QString::fromUtf8(key));
        told != g_lite_python_bridge->hosted_tool_record.constEnd())
    {
        return cborValueToPythonObject(told.value());
    }

    QCborValue default_value;
    std::string error;
    if (!pythonObjectToCborValue(default_object, &default_value, &error))
    {
        return pyRaiseRuntimeError(error);
    }

    an::ui::extensions::ExtensionProtocolMessage response;
    if (!g_lite_python_bridge->sendProtocolRequestAwaitResponse(
            "settings.get",
            QCborMap{{QStringLiteral("key"), QString::fromUtf8(key)}, {QStringLiteral("default_value"), default_value}},
            &response, &error))
    {
        return pyRaiseRuntimeError(error);
    }
    return cborValueToPythonObject(response.fields.value(QStringLiteral("value")));
}

PyObject* liteHostSettingsSet(PyObject*, PyObject* args)
{
    const char* key = nullptr;
    PyObject* value_object = nullptr;
    if (!PyArg_ParseTuple(args, "sO:settings_set", &key, &value_object))
    {
        return nullptr;
    }
    if (!g_lite_python_bridge || !g_lite_python_bridge->hasControlSocket())
    {
        Py_RETURN_NONE;
    }

    QCborValue value;
    std::string error;
    if (!pythonObjectToCborValue(value_object, &value, &error))
    {
        return pyRaiseRuntimeError(error);
    }

    an::ui::extensions::ExtensionProtocolMessage response;
    if (!g_lite_python_bridge->sendProtocolRequestAwaitResponse(
            "settings.set", QCborMap{{QStringLiteral("key"), QString::fromUtf8(key)}, {QStringLiteral("value"), value}},
            &response, &error))
    {
        return pyRaiseRuntimeError(error);
    }
    Py_RETURN_NONE;
}

PyObject* liteHostSettingsRemove(PyObject*, PyObject* args)
{
    const char* key = nullptr;
    if (!PyArg_ParseTuple(args, "s:settings_remove", &key))
    {
        return nullptr;
    }
    if (!g_lite_python_bridge || !g_lite_python_bridge->hasControlSocket())
    {
        Py_RETURN_NONE;
    }

    an::ui::extensions::ExtensionProtocolMessage response;
    std::string error;
    if (!g_lite_python_bridge->sendProtocolRequestAwaitResponse(
            "settings.remove", QCborMap{{QStringLiteral("key"), QString::fromUtf8(key)}}, &response, &error))
    {
        return pyRaiseRuntimeError(error);
    }
    Py_RETURN_NONE;
}

PyObject* liteHostSettingsKeys(PyObject*, PyObject*)
{
    if (!g_lite_python_bridge || !g_lite_python_bridge->hasControlSocket())
    {
        return PyList_New(0);
    }

    an::ui::extensions::ExtensionProtocolMessage response;
    std::string error;
    if (!g_lite_python_bridge->sendProtocolRequestAwaitResponse("settings.keys", {}, &response, &error))
    {
        return pyRaiseRuntimeError(error);
    }
    PyObjectHolder keys(cborValueToPythonObject(response.fields.value(QStringLiteral("keys"))));
    if (!keys.value)
    {
        return nullptr;
    }
    if (!PyList_Check(keys.value))
    {
        return PyList_New(0);
    }
    return keys.release();
}

PyObject* liteHostProgressSetValue(PyObject*, PyObject* args)
{
    double value = 0.0;
    if (!PyArg_ParseTuple(args, "d:progress_set_value", &value))
    {
        return nullptr;
    }
    if (g_lite_python_bridge)
    {
        g_lite_python_bridge->tellRunProgress(value, std::nullopt);
        g_lite_python_bridge->pumpWhileRunning();
    }
    Py_RETURN_NONE;
}

PyObject* liteHostProgressSetText(PyObject*, PyObject* args)
{
    const char* text = nullptr;
    if (!PyArg_ParseTuple(args, "s:progress_set_text", &text))
    {
        return nullptr;
    }
    if (g_lite_python_bridge)
    {
        g_lite_python_bridge->tellRunProgress(std::nullopt, std::string(text != nullptr ? text : ""));
        g_lite_python_bridge->pumpWhileRunning();
    }
    Py_RETURN_NONE;
}

/// States the processes the extension adds while it runs, its whole list (`processes.contribute`), and answers how
/// many the application kept; a list the application refuses raises its reason. With no application - a standalone
/// launch - nothing keeps the list, and none is kept.
PyObject* liteHostContributeProcesses(PyObject*, PyObject* args)
{
    PyObject* entries_object = nullptr;
    if (!PyArg_ParseTuple(args, "O:contribute_processes", &entries_object))
    {
        return nullptr;
    }
    if (!g_lite_python_bridge || !g_lite_python_bridge->hasControlSocket())
    {
        return PyLong_FromLong(0);
    }

    QCborValue entries;
    std::string error;
    if (!pythonObjectToCborValue(entries_object, &entries, &error))
    {
        return pyRaiseRuntimeError(error);
    }

    an::ui::extensions::ExtensionProtocolMessage response;
    if (!g_lite_python_bridge->sendProtocolRequestAwaitResponse(
            "processes.contribute", QCborMap{{QStringLiteral("entries"), entries}}, &response, &error))
    {
        return pyRaiseRuntimeError(error);
    }
    return PyLong_FromLongLong(response.fields.value(QStringLiteral("count")).toInteger());
}

/// Asks something of the view the session is bound to (`view.overlay`, `view.selection`, `view.display`,
/// `view.history`) and answers the application's answer; a request the application refuses raises its reason. With no
/// application - a standalone launch - there is no view, and nothing is answered.
PyObject* liteHostViewRequest(PyObject*, PyObject* args)
{
    const char* type = nullptr;
    PyObject* fields_object = nullptr;
    if (!PyArg_ParseTuple(args, "sO:view_request", &type, &fields_object))
    {
        return nullptr;
    }
    if (an::ui::extensions::viewRequestCapability(type).empty())
    {
        PyErr_SetString(PyExc_ValueError, (std::string("'") + type + "' is not a request of the view").c_str());
        return nullptr;
    }
    if (!g_lite_python_bridge || !g_lite_python_bridge->hasControlSocket())
    {
        Py_RETURN_NONE;
    }

    QCborValue fields;
    std::string error;
    if (!pythonObjectToCborValue(fields_object, &fields, &error))
    {
        return pyRaiseRuntimeError(error);
    }
    if (!fields.isMap())
    {
        return pyRaiseRuntimeError(std::string(type) + " is asked with a mapping of its fields");
    }

    an::ui::extensions::ExtensionProtocolMessage response;
    if (!g_lite_python_bridge->sendProtocolRequestAwaitResponse(type, fields.toMap(), &response, &error))
    {
        return pyRaiseRuntimeError(error);
    }
    return cborValueToPythonObject(QCborValue(response.fields));
}

/// Hides the process window: with a panel the application draws, the application puts the panel's task away; with
/// none, the Lite host has no window to hide.
PyObject* liteHostProcessWindowHide(PyObject*, PyObject*)
{
    if (!g_lite_python_bridge || !g_lite_python_bridge->hosted_panel || !g_lite_python_bridge->hasControlSocket())
    {
        Py_RETURN_FALSE;
    }
    an::ui::extensions::PanelHideMessage hide;
    hide.session_id = g_lite_python_bridge->session_id.toStdString();
    std::string error;
    if (!g_lite_python_bridge->tell(an::ui::extensions::toProtocolMessage(hide), &error))
    {
        return pyRaiseRuntimeError(error);
    }
    Py_RETURN_TRUE;
}

/// Closes the process window: the session ends, as it does when a window is closed - the application is told
/// (`runtime.closed`) and the host stops once the extension's code returns.
PyObject* liteHostProcessWindowClose(PyObject*, PyObject*)
{
    if (!g_lite_python_bridge)
    {
        Py_RETURN_FALSE;
    }
    if (!g_lite_python_bridge->close_requested)
    {
        g_lite_python_bridge->close_requested = true;
        // The run the application was told of ends before the session does, which ends it on the application's side.
        if (g_lite_python_bridge->told_run && !g_lite_python_bridge->panel_request.has_value())
        {
            g_lite_python_bridge->tellRunEnded(g_lite_python_bridge->cancel_requested ? "cancelled" : "succeeded");
        }
        if (g_lite_python_bridge->hasControlSocket())
        {
            an::ui::extensions::ExtensionProtocolMessage closed;
            closed.type = "runtime.closed";
            closed.session_id = g_lite_python_bridge->session_id.toStdString();
            closed.request_id = generateLiteOpaqueToken();
            closed.fields.insert(QStringLiteral("active_work"), g_lite_python_bridge->panel_request.has_value());
            static_cast<void>(g_lite_python_bridge->tell(closed));
            g_lite_python_bridge->socket->flush();
        }
        QTimer::singleShot(0, QCoreApplication::instance(), &QCoreApplication::quit);
    }
    Py_RETURN_TRUE;
}

PyObject* liteHostThemeTokens(PyObject*, PyObject*)
{
    if (!g_lite_python_bridge)
    {
        return PyDict_New();
    }
    return cborMapToPythonDict(g_lite_python_bridge->theme_tokens);
}

PyObject* liteHostProgressIsCancelled(PyObject*, PyObject*)
{
    if (g_lite_python_bridge)
    {
        g_lite_python_bridge->pumpWhileRunning();
    }
    if (g_lite_python_bridge && g_lite_python_bridge->cancel_requested)
    {
        Py_RETURN_TRUE;
    }
    Py_RETURN_FALSE;
}

PyMethodDef kLiteHostMethods[] = {
    {"session_info", liteHostSessionInfo, METH_NOARGS, nullptr},
    {"session_paths", liteHostSessionPaths, METH_NOARGS, nullptr},
    {"has_control_socket", liteHostHasControlSocket, METH_NOARGS, nullptr},
    {"log_event", liteHostLogEvent, METH_VARARGS, nullptr},
    {"list_views", liteHostListViews, METH_NOARGS, nullptr},
    {"view_snapshot", liteHostViewSnapshot, METH_VARARGS, nullptr},
    {"image_to_numpy", liteHostImageToNumpy, METH_VARARGS, nullptr},
    {"image_clone", liteHostImageClone, METH_VARARGS, nullptr},
    {"image_from_numpy", liteHostImageFromNumpy, METH_VARARGS, nullptr},
    {"image_properties", liteHostImageProperties, METH_VARARGS, nullptr},
    {"image_metadata", liteHostImageMetadata, METH_VARARGS, nullptr},
    {"image_set_metadata", liteHostImageSetMetadata, METH_VARARGS, nullptr},
    {"ui_open_image", liteHostUiOpenImage, METH_VARARGS, nullptr},
    {"view_commit_numpy", liteHostViewCommitNumpy, METH_VARARGS, nullptr},
    {"settings_get", liteHostSettingsGet, METH_VARARGS, nullptr},
    {"settings_set", liteHostSettingsSet, METH_VARARGS, nullptr},
    {"settings_remove", liteHostSettingsRemove, METH_VARARGS, nullptr},
    {"settings_keys", liteHostSettingsKeys, METH_NOARGS, nullptr},
    {"progress_set_value", liteHostProgressSetValue, METH_VARARGS, nullptr},
    {"progress_set_text", liteHostProgressSetText, METH_VARARGS, nullptr},
    {"progress_is_cancelled", liteHostProgressIsCancelled, METH_NOARGS, nullptr},
    {"theme_tokens", liteHostThemeTokens, METH_NOARGS, nullptr},
    {"contribute_processes", liteHostContributeProcesses, METH_VARARGS, nullptr},
    {"view_request", liteHostViewRequest, METH_VARARGS, nullptr},
    {"process_window_hide", liteHostProcessWindowHide, METH_NOARGS, nullptr},
    {"process_window_close", liteHostProcessWindowClose, METH_NOARGS, nullptr},
    {nullptr, nullptr, 0, nullptr},
};

PyModuleDef kLiteHostModule = {
    PyModuleDef_HEAD_INIT,
    "_afternight_host",
    "Protocol-safe AfterNight lite extension host bridge.",
    -1,
    kLiteHostMethods,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
};

PyMODINIT_FUNC PyInit__afternight_host()
{
    return PyModule_Create(&kLiteHostModule);
}

bool ensureLiteHostPythonModuleRegistered(std::string* error)
{
    static bool registered = false;
    if (registered)
    {
        return true;
    }
    if (PyImport_AppendInittab("_afternight_host", PyInit__afternight_host) != 0)
    {
        if (error)
        {
            *error = "Failed to register the lite Python host bridge";
        }
        return false;
    }
    registered = true;
    return true;
}

bool callLiteLifecycleHook(const char* hook_name, const char* reason, std::string* error)
{
    if (!Py_IsInitialized() || !hook_name || std::strlen(hook_name) == 0)
    {
        return true;
    }

    std::ostringstream script;
    script << "instance = globals().get('instance')\n"
           << "if instance is not None and hasattr(instance, " << pythonStringLiteral(hook_name) << "):\n"
           << "    __afternight_lifecycle_hook = getattr(instance, " << pythonStringLiteral(hook_name) << ")\n";
    if (reason)
    {
        script << "    __afternight_lifecycle_hook(" << pythonStringLiteral(reason) << ")\n";
    }
    else
    {
        script << "    __afternight_lifecycle_hook()\n";
    }

    PyObject* main_module = PyImport_AddModule("__main__");
    PyObject* globals = main_module ? PyModule_GetDict(main_module) : nullptr;
    PyObject* result = globals ? PyRun_String(script.str().c_str(), Py_file_input, globals, globals) : nullptr;
    if (!result)
    {
        if (error)
        {
            *error = pythonExceptionMessage();
        }
        return false;
    }
    Py_DECREF(result);
    return true;
}

bool callLiteLaunchHook(std::string* error)
{
    if (!Py_IsInitialized())
    {
        return true;
    }

    std::ostringstream script;
    script << "instance = globals().get('instance')\n"
           << "if instance is not None:\n"
           << "    if hasattr(instance, 'on_launch'):\n"
           << "        instance.on_launch()\n"
           << "    from afternight.lite import _run_extension_startup\n"
           << "    _run_extension_startup(instance)\n";

    PyObject* main_module = PyImport_AddModule("__main__");
    PyObject* globals = main_module ? PyModule_GetDict(main_module) : nullptr;
    PyObject* result = globals ? PyRun_String(script.str().c_str(), Py_file_input, globals, globals) : nullptr;
    if (!result)
    {
        if (error)
        {
            *error = pythonExceptionMessage();
        }
        return false;
    }
    Py_DECREF(result);
    return true;
}

bool callLiteInstanceHook(const char* hook_name, PyObject* argument, std::string* error)
{
    if (!Py_IsInitialized() || !hook_name || std::strlen(hook_name) == 0)
    {
        return true;
    }

    PyObject* main_module = PyImport_AddModule("__main__");
    PyObject* globals = main_module ? PyModule_GetDict(main_module) : nullptr;
    PyObject* instance = globals ? PyDict_GetItemString(globals, "instance") : nullptr;
    if (!instance || instance == Py_None || !PyObject_HasAttrString(instance, hook_name))
    {
        return true;
    }

    PyObjectHolder callable(PyObject_GetAttrString(instance, hook_name));
    if (!callable.value || !PyCallable_Check(callable.value))
    {
        return true;
    }

    PyObjectHolder result(argument ? PyObject_CallFunctionObjArgs(callable.value, argument, nullptr)
                                   : PyObject_CallFunctionObjArgs(callable.value, nullptr));
    if (!result.value)
    {
        if (error)
        {
            *error = pythonExceptionMessage();
        }
        return false;
    }
    return true;
}

QCborMap viewNotificationFieldsForPython(const an::ui::extensions::ExtensionProtocolMessage& inbound)
{
    QCborMap fields = inbound.fields;
    fields[QStringLiteral("session_id")] = QString::fromStdString(inbound.session_id);
    fields[QStringLiteral("request_id")] = QString::fromStdString(inbound.request_id);
    if (!fields.contains(QStringLiteral("view_name")) && fields.contains(QStringLiteral("view_id")))
    {
        fields[QStringLiteral("view_name")] = fields.value(QStringLiteral("view_id"));
    }
    if (!fields.contains(QStringLiteral("name")) && fields.contains(QStringLiteral("view_name")))
    {
        fields[QStringLiteral("name")] = fields.value(QStringLiteral("view_name"));
    }
    return fields;
}

bool dispatchLiteViewNotification(const an::ui::extensions::ExtensionProtocolMessage& inbound, std::string* error)
{
    if (inbound.type == "view.created")
    {
        an::ui::extensions::ViewCreatedMessage message;
        if (!an::ui::extensions::viewCreatedFromProtocolMessage(inbound, &message, error))
        {
            return false;
        }
        PyObjectHolder view(cborMapToPythonDict(viewNotificationFieldsForPython(inbound)));
        return view.value && callLiteInstanceHook("on_view_created", view.value, error);
    }
    if (inbound.type == "view.updated")
    {
        an::ui::extensions::ViewUpdatedMessage message;
        if (!an::ui::extensions::viewUpdatedFromProtocolMessage(inbound, &message, error))
        {
            return false;
        }
        PyObjectHolder view(cborMapToPythonDict(viewNotificationFieldsForPython(inbound)));
        return view.value && callLiteInstanceHook("on_view_modified", view.value, error);
    }
    if (inbound.type == "view.renamed")
    {
        an::ui::extensions::ViewRenamedMessage message;
        if (!an::ui::extensions::viewRenamedFromProtocolMessage(inbound, &message, error))
        {
            return false;
        }
        PyObjectHolder view(cborMapToPythonDict(viewNotificationFieldsForPython(inbound)));
        return view.value && callLiteInstanceHook("on_view_renamed", view.value, error);
    }
    if (inbound.type == "view.destroyed")
    {
        an::ui::extensions::ViewDestroyedMessage message;
        if (!an::ui::extensions::viewDestroyedFromProtocolMessage(inbound, &message, error))
        {
            return false;
        }
        PyObjectHolder view_id(PyUnicode_FromString(message.view_id.c_str()));
        return view_id.value && callLiteInstanceHook("on_view_removed", view_id.value, error);
    }
    return true;
}

// ---- The panel the application draws
//
// With `kHostedPanelCapability` acknowledged, the Lite host serves the panel as the full host does: it tells the
// application the extension's parameters once its runtime is ready (`ui.describe`), hears what a person does in the
// panel (`ui.event`) and answers an action (`ui.patch`), and runs each frame of the preview and each Apply the
// application sends over the buffers it lent, writing the output it lent: a frame answered by `execution.result`, an
// Apply by `view.commit`, or by `execution.result` when it failed or was cancelled. One request at a time, run on this
// thread between turns of the event loop; a cancel that comes before a request runs answers it, and one that comes
// while it runs is read when the extension next reports its progress or asks whether it was cancelled.

/// The extension's instance, as the host made it: borrowed, `None` when there is none.
PyObject* liteInstance()
{
    PyObject* main_module = PyImport_AddModule("__main__");
    PyObject* globals = main_module ? PyModule_GetDict(main_module) : nullptr;
    PyObject* instance = globals ? PyDict_GetItemString(globals, "instance") : nullptr;
    return instance != nullptr ? instance : Py_None;
}

/// Calls a function of `afternight.lite`: the new reference it returns, or null with the Python error set.
PyObject* callLiteSdk(const char* name, PyObject* args)
{
    PyObjectHolder module(PyImport_ImportModule("afternight.lite"));
    if (!module.value)
    {
        return nullptr;
    }
    PyObjectHolder function(PyObject_GetAttrString(module.value, name));
    if (!function.value)
    {
        return nullptr;
    }
    return PyObject_CallObject(function.value, args);
}

/// The cards a buffer's sidecar holds; none when it has no sidecar, or one that cannot be read.
QCborMap loadLiteSidecar(const std::string& path)
{
    if (path.empty())
    {
        return {};
    }
    QFile file(QString::fromStdString(path));
    if (!file.open(QIODevice::ReadOnly))
    {
        return {};
    }
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    return document.isObject() ? QCborMap::fromJsonObject(document.object()) : QCborMap{};
}

/// Tells the application the extension's parameters (`ui.describe`): the list its `get_params()` returns.
bool describeLitePanel(std::string* error)
{
    if (!g_lite_python_bridge || !Py_IsInitialized())
    {
        return true;
    }
    PyObject* main_module = PyImport_AddModule("__main__");
    PyObject* globals = main_module ? PyModule_GetDict(main_module) : nullptr;
    PyObject* result = globals ? PyRun_String("import json as __afternight_json\n"
                                              "instance = globals().get('instance')\n"
                                              "__afternight_panel_json = __afternight_json.dumps(\n"
                                              "    list(instance.get_params()) if instance is not None else [])\n",
                                              Py_file_input, globals, globals)
                               : nullptr;
    if (!result)
    {
        if (error)
        {
            *error = "The extension's parameters could not be read: " + pythonExceptionMessage();
        }
        return false;
    }
    Py_DECREF(result);
    PyObject* json_value = PyDict_GetItemString(globals, "__afternight_panel_json");
    const char* json_text = json_value != nullptr ? PyUnicode_AsUTF8(json_value) : nullptr;
    const QJsonDocument document = QJsonDocument::fromJson(QByteArray(json_text != nullptr ? json_text : "[]"));
    if (!document.isArray())
    {
        if (error)
        {
            *error = "The extension's parameters are not a list";
        }
        return false;
    }
    return g_lite_python_bridge->tell(
        an::ui::extensions::hostedPanelDescription(g_lite_python_bridge->session_id.toStdString(), document.array()),
        error);
}

/// What a person did in the panel: the parameters' change and the tool's record told to the extension, an action run
/// and its answer sent back. A failure of the extension's is answered or written, never fatal to the session.
bool serveLitePanelEvent(const an::ui::extensions::ExtensionProtocolMessage& message)
{
    an::ui::extensions::HostedPanelEvent event;
    std::string error;
    if (!an::ui::extensions::hostedPanelEventFrom(message, &event, &error))
    {
        printError("A hosted panel's event was refused: " + error);
        return true;
    }
    switch (event.kind)
    {
    case an::ui::extensions::HostedPanelEvent::Kind::ToolRecord:
        g_lite_python_bridge->hosted_tool_record = event.toolRecord;
        return true;
    case an::ui::extensions::HostedPanelEvent::Kind::ParametersChanged: {
        PyObject* instance = liteInstance();
        if (instance == Py_None || !PyObject_HasAttrString(instance, "on_parameters_changed"))
        {
            return true;
        }
        PyObjectHolder params(cborMapToPythonDict(event.params));
        PyObjectHolder changed(PyList_New(0));
        for (const QString& key : event.changedKeys)
        {
            PyObjectHolder name(PyUnicode_FromString(key.toUtf8().constData()));
            if (changed.value && name.value)
            {
                PyList_Append(changed.value, name.value);
            }
        }
        PyObjectHolder reason(PyUnicode_FromString(event.reason.toUtf8().constData()));
        PyObjectHolder hook(PyObject_GetAttrString(instance, "on_parameters_changed"));
        PyObjectHolder result(
            hook.value && params.value && changed.value && reason.value
                ? PyObject_CallFunctionObjArgs(hook.value, params.value, changed.value, reason.value, nullptr)
                : nullptr);
        if (!result.value)
        {
            printError("Extension on_parameters_changed() failed: " + pythonExceptionMessage());
        }
        return true;
    }
    case an::ui::extensions::HostedPanelEvent::Kind::Action:
        break;
    }

    QCborMap values;
    std::string action_error;
    PyObjectHolder params(cborMapToPythonDict(event.params));
    PyObjectHolder args(params.value ? Py_BuildValue("(OsO)", liteInstance(), event.actionId.c_str(), params.value)
                                     : nullptr);
    PyObjectHolder answer(args.value ? callLiteSdk("_serve_panel_action", args.value) : nullptr);
    QCborValue answered;
    if (!answer.value)
    {
        action_error = pythonExceptionMessage();
    }
    else if (!pythonObjectToCborValue(answer.value, &answered, &action_error) || !answered.isMap())
    {
        action_error = action_error.empty() ? std::string("handle_param_action() must return a dict") : action_error;
    }
    else
    {
        values = answered.toMap();
    }
    if (!action_error.empty())
    {
        printError("A hosted panel's action '" + event.actionId + "' failed: " + action_error);
    }
    return g_lite_python_bridge->tell(
        an::ui::extensions::hostedActionAnswer(g_lite_python_bridge->session_id.toStdString(), event.actionId, values,
                                               action_error),
        &error);
}

/// Answers the request of the panel on its way and lets it go.
bool answerLitePanelRequest(const std::string& status, const std::string& detail,
                            const an::ui::extensions::SharedBufferDescriptor* output)
{
    const LitePanelRequest request = std::move(*g_lite_python_bridge->panel_request);
    g_lite_python_bridge->panel_request.reset();
    g_lite_python_bridge->cancel_requested = false;
    const std::string session_id = g_lite_python_bridge->session_id.toStdString();
    if (request.kind == "execute")
    {
        g_lite_python_bridge->tellRunEnded(status, status == "failed" ? detail : std::string());
    }
    std::string error;
    if (request.kind == "execute" && status == "succeeded" && output != nullptr)
    {
        an::ui::extensions::ViewCommitMessage commit;
        commit.session_id = session_id;
        commit.request_id = request.request_id;
        commit.view_id = g_lite_python_bridge->bound_view_name.isEmpty()
                             ? session_id
                             : g_lite_python_bridge->bound_view_name.toStdString();
        commit.history_step_name = "Extension Apply";
        commit.history_step_description = "Applied extension execution result";
        commit.params = request.params;
        commit.result_buffers = {*output};
        return g_lite_python_bridge->tell(an::ui::extensions::toProtocolMessage(commit), &error);
    }
    an::ui::extensions::ExecutionResultMessage result;
    result.session_id = session_id;
    result.request_id = generateLiteOpaqueToken();
    result.target_request_id = request.request_id;
    result.result_kind = request.kind;
    result.status = status;
    result.detail = plainProtocolText(detail, an::ui::extensions::kPanelPatchErrorLimit, true);
    if (status == "succeeded" && output != nullptr)
    {
        result.output_buffers = {*output};
    }
    return g_lite_python_bridge->tell(an::ui::extensions::toProtocolMessage(result), &error);
}

bool handleInboundMessage(QLocalSocket& socket, const an::ui::extensions::ExtensionProtocolMessage& inbound,
                          std::string* error);

/// Runs the request of the panel on its way, when it is still the one a run was scheduled for: the buffers it lent
/// bound to the extension's images, its `execute_preview()` or `execute()` run over them, its answer sent, and what
/// arrived meanwhile handled.
void runLitePanelRequest(const std::uint64_t number)
{
    LitePythonHostBridge* const bridge = g_lite_python_bridge;
    if (bridge == nullptr || !bridge->panel_request.has_value() || bridge->panel_request->number != number)
    {
        return;
    }
    const LitePanelRequest& request = *bridge->panel_request;
    const auto source = std::find_if(request.buffers.begin(), request.buffers.end(), [](const auto& buffer) {
        return buffer.buffer_role == an::ui::extensions::SharedBufferRole::Source;
    });
    const an::ui::extensions::SharedBufferDescriptor* lent =
        an::ui::extensions::lentOutputOf(request.kind, request.buffers);
    if (source == request.buffers.end() || lent == nullptr)
    {
        static_cast<void>(answerLitePanelRequest("failed", "The request lent no source or no output", nullptr));
        return;
    }
    const an::ui::extensions::SharedBufferDescriptor output = *lent;

    // The images the request lent, by tokens of their own; a mask or a weight map by its name.
    std::vector<std::string> tokens;
    QCborMap masks;
    QCborMap weights;
    std::string error;
    const QCborMap source_cards = loadLiteSidecar(source->metadata_sidecar);
    bool bound = bindSnapshotBuffer(*source, source_cards, "panel:source", &error) &&
                 bindSnapshotBuffer(output, source_cards, "panel:output", &error);
    tokens = {"panel:source", "panel:output"};
    for (const auto& buffer : request.buffers)
    {
        const bool mask = buffer.buffer_role == an::ui::extensions::SharedBufferRole::Mask;
        if (!bound || (!mask && buffer.buffer_role != an::ui::extensions::SharedBufferRole::Weight))
        {
            continue;
        }
        const std::string name = buffer.symbolic_name.empty() ? buffer.buffer_id : buffer.symbolic_name;
        const std::string token = std::string(mask ? "panel:mask:" : "panel:weight:") + name;
        bound = bindSnapshotBuffer(buffer, {}, token, &error);
        tokens.push_back(token);
        (mask ? masks : weights).insert(QString::fromStdString(name), QString::fromStdString(token));
    }

    std::string status = "failed";
    std::string detail = error;
    if (bound)
    {
        // An Apply's progress is told to the application as the extension reports it.
        bridge->reports_run = request.kind == "execute";
        bridge->pump_clock.invalidate();
        PyObjectHolder params(cborMapToPythonDict(request.params));
        PyObjectHolder mask_tokens(cborMapToPythonDict(masks));
        PyObjectHolder weight_tokens(cborMapToPythonDict(weights));
        const QByteArray view_name = bridge->bound_view_name.toUtf8();
        PyObjectHolder args(params.value && mask_tokens.value && weight_tokens.value
                                ? Py_BuildValue("(OsssOsOO)", liteInstance(), request.kind.c_str(), "panel:source",
                                                "panel:output", params.value, view_name.constData(), mask_tokens.value,
                                                weight_tokens.value)
                                : nullptr);
        PyObjectHolder answered(args.value ? callLiteSdk("_serve_panel_request", args.value) : nullptr);
        const char* told = answered.value != nullptr ? PyUnicode_AsUTF8(answered.value) : nullptr;
        if (told != nullptr)
        {
            status = told;
            detail = status == "cancelled" ? "Cancelled by host request" : std::string();
        }
        else
        {
            detail = pythonExceptionMessage();
            detail = detail.empty() ? std::string("The extension's run failed") : detail;
        }
        // An Apply's cards go into the output's sidecar before the application reads it.
        if (status == "succeeded" && request.kind == "execute" && !output.metadata_sidecar.empty())
        {
            if (LiteImageBinding* written = bridge->findImageBinding("panel:output"))
            {
                PyObjectHolder cards(cborMapToPythonDict(written->metadata));
                std::string sidecar_error;
                if (!cards.value || !writeMetadataSidecarDict(output.metadata_sidecar, cards.value, &sidecar_error))
                {
                    status = "failed";
                    detail =
                        sidecar_error.empty() ? std::string("The result's cards could not be written") : sidecar_error;
                }
            }
        }
    }
    // Every file the request lent is let go before it is answered, so the application may remove it once it has heard.
    for (const std::string& token : tokens)
    {
        bridge->image_bindings.erase(token);
    }
    if (!answerLitePanelRequest(status, detail, &output))
    {
        printError("A hosted panel's answer could not be sent");
    }

    // What arrived while the extension ran is handled now, in the order it came.
    std::deque<an::ui::extensions::ExtensionProtocolMessage> kept = std::move(bridge->kept_inbound);
    bridge->kept_inbound.clear();
    for (const auto& message : kept)
    {
        std::string kept_error;
        if (bridge->socket != nullptr && !handleInboundMessage(*bridge->socket, message, &kept_error))
        {
            printError(kept_error);
            QCoreApplication::quit();
            return;
        }
    }
}

/// Reads a frame or an Apply of the panel: held as the request on its way, and run once its delay - none outside a case
/// that states one - has passed. A second request while one is on its way breaks the protocol.
bool takeLitePanelRequest(const an::ui::extensions::ExtensionProtocolMessage& inbound, std::string* error)
{
    LitePanelRequest request;
    if (inbound.type == "execution.preview")
    {
        an::ui::extensions::ExecutionPreviewMessage preview;
        if (!an::ui::extensions::executionPreviewFromProtocolMessage(inbound, &preview, error))
        {
            return false;
        }
        request.request_id = preview.request_id;
        request.kind = "preview";
        request.params = preview.params;
        request.buffers = std::move(preview.buffers);
    }
    else
    {
        an::ui::extensions::ExecutionExecuteMessage execute;
        if (!an::ui::extensions::executionExecuteFromProtocolMessage(inbound, &execute, error))
        {
            return false;
        }
        request.request_id = execute.request_id;
        request.kind = "execute";
        request.params = execute.params;
        request.buffers = std::move(execute.buffers);
    }
    if (g_lite_python_bridge->panel_request.has_value())
    {
        if (error)
        {
            *error = "Received " + inbound.type + " while another request was still in flight";
        }
        return false;
    }
    request.number = ++g_lite_python_bridge->panel_requests_read;
    const int delay_ms =
        request.kind == "preview" ? g_lite_python_bridge->preview_delay_ms : g_lite_python_bridge->execute_delay_ms;
    const std::uint64_t number = request.number;
    g_lite_python_bridge->cancel_requested = false;
    g_lite_python_bridge->panel_request = std::move(request);
    QTimer::singleShot((std::max)(0, delay_ms), QCoreApplication::instance(),
                       [number]() { runLitePanelRequest(number); });
    return true;
}

bool handleInboundMessage(QLocalSocket& socket, const an::ui::extensions::ExtensionProtocolMessage& inbound,
                          std::string* error)
{
    if (g_lite_python_bridge && (inbound.type == "view.describe" || inbound.type == "view.bound"))
    {
        const QString named = inbound.fields.value(QStringLiteral("view_name")).toString();
        g_lite_python_bridge->bound_view_name =
            named.isEmpty() ? inbound.fields.value(QStringLiteral("view_id")).toString() : named;
    }
    if (inbound.type == "window.raise")
    {
        // A person asked to see the extension's window: an extension that owns one raises it.
        std::string raise_error;
        if (!callLiteLifecycleHook("on_raise_window", nullptr, &raise_error))
        {
            printError("Extension on_raise_window() failed: " + raise_error);
        }
        return true;
    }
    if (g_lite_python_bridge && g_lite_python_bridge->hosted_panel)
    {
        if (inbound.type == "execution.preview" || inbound.type == "execution.execute")
        {
            return takeLitePanelRequest(inbound, error);
        }
        if (inbound.type == "execution.cancel" && g_lite_python_bridge->panel_request.has_value())
        {
            // The request has not run yet: it is answered cancelled, never run.
            an::ui::extensions::ExecutionCancelMessage cancel;
            if (!an::ui::extensions::executionCancelFromProtocolMessage(inbound, &cancel, error))
            {
                return false;
            }
            if (cancel.target_request_id == g_lite_python_bridge->panel_request->request_id)
            {
                std::string hook_error;
                if (!callLiteLifecycleHook("on_cancel", nullptr, &hook_error))
                {
                    printError("Extension on_cancel() failed: " + hook_error);
                }
                return answerLitePanelRequest("cancelled", "Cancelled by host request", nullptr);
            }
            return true;
        }
        if (inbound.type == "ui.event")
        {
            return serveLitePanelEvent(inbound);
        }
    }

    if (inbound.type != "runtime.ping")
    {
        if (inbound.type == "execution.cancel" && g_lite_python_bridge)
        {
            g_lite_python_bridge->cancel_requested = true;
            if (!callLiteLifecycleHook("on_cancel", nullptr, error))
            {
                return false;
            }
        }
        if (inbound.type == "view.created" || inbound.type == "view.updated" || inbound.type == "view.renamed" ||
            inbound.type == "view.destroyed")
        {
            return dispatchLiteViewNotification(inbound, error);
        }
        if ((inbound.type == "theme.describe" || inbound.type == "theme.changed") && g_lite_python_bridge)
        {
            g_lite_python_bridge->takeTheme(inbound);
        }
        return true;
    }

    an::ui::extensions::RuntimePingMessage ping;
    if (!an::ui::extensions::runtimePingFromProtocolMessage(inbound, &ping, error))
    {
        return false;
    }

    an::ui::extensions::RuntimePongMessage pong;
    pong.session_id = ping.session_id;
    pong.request_id = ping.request_id;
    pong.sequence = ping.sequence;
    return writePong(socket, pong, error);
}

} // namespace

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("AfterNightExtensionHostLite"));
    app.setQuitLockEnabled(false);

    QCommandLineParser parser;
    parser.addHelpOption();

    const QCommandLineOption endpoint_option(QStringLiteral("endpoint-name"), QStringLiteral("Endpoint name."),
                                             QStringLiteral("endpoint"));
    const QCommandLineOption session_option(QStringLiteral("session-id"), QStringLiteral("Session id."),
                                            QStringLiteral("session"));
    const QCommandLineOption extension_option(QStringLiteral("extension-id"), QStringLiteral("Extension id."),
                                              QStringLiteral("extension"));
    const QCommandLineOption backend_option(QStringLiteral("sdk-backend"), QStringLiteral("SDK backend."),
                                            QStringLiteral("backend"), QStringLiteral("protocol"));
    const QCommandLineOption launch_mode_option(QStringLiteral("launch-mode"), QStringLiteral("Launch mode."),
                                                QStringLiteral("mode"), QStringLiteral("single_image"));
    const QCommandLineOption host_version_option(QStringLiteral("host-version"), QStringLiteral("Host version."),
                                                 QStringLiteral("version"), QStringLiteral("dev"));
    const QCommandLineOption capabilities_option(QStringLiteral("capabilities"), QStringLiteral("Capabilities."),
                                                 QStringLiteral("capabilities"));
    const QCommandLineOption display_name_option(QStringLiteral("display-name"), QStringLiteral("Display name."),
                                                 QStringLiteral("name"));
    const QCommandLineOption inspect_option(QStringLiteral("inspect"), QStringLiteral("Inspect package metadata."));
    const QCommandLineOption inspect_settings_option(QStringLiteral("inspect-settings"),
                                                     QStringLiteral("Accepted for inspect compatibility."));
    const QCommandLineOption inspect_params_option(QStringLiteral("inspect-params"),
                                                   QStringLiteral("Accepted for inspect compatibility."));
    const QCommandLineOption inspect_settings_snapshot_option(QStringLiteral("inspect-settings-snapshot-cbor-base64"),
                                                              QStringLiteral("Accepted for inspect compatibility."),
                                                              QStringLiteral("snapshot"));
    const QCommandLineOption extension_package_root_option(
        QStringLiteral("extension-package-root"), QStringLiteral("Extension package root."), QStringLiteral("path"));
    const QCommandLineOption environment_root_option(QStringLiteral("environment-root"),
                                                     QStringLiteral("Environment root."), QStringLiteral("path"));
    const QCommandLineOption runtime_root_option(QStringLiteral("runtime-root"), QStringLiteral("Runtime root."),
                                                 QStringLiteral("path"));
    const QCommandLineOption entry_point_option(QStringLiteral("entry-point"), QStringLiteral("Entry point."),
                                                QStringLiteral("module"));
    const QCommandLineOption process_class_option(QStringLiteral("process-class"), QStringLiteral("Process class."),
                                                  QStringLiteral("class"));
    const QCommandLineOption contributed_process_option(
        QStringLiteral("contributed-process"),
        QStringLiteral("The process the extension contributed that the session is started for, by its id_suffix."),
        QStringLiteral("id_suffix"));
#ifdef _WIN32
    const QCommandLineOption session_secret_handle_option(QStringLiteral("session-secret-handle"),
                                                          QStringLiteral("Inherited session-secret handle."),
                                                          QStringLiteral("handle"));
#endif
    const QCommandLineOption stay_alive_option(QStringLiteral("stay-alive-ms"),
                                               QStringLiteral("Keep the control channel alive after handshake."),
                                               QStringLiteral("ms"), QStringLiteral("0"));
    const QCommandLineOption standalone_option(
        QStringLiteral("standalone"),
        QStringLiteral("Run without a host control socket using mock app/view protocol services."));
    const QCommandLineOption preview_delay_option(
        QStringLiteral("preview-delay-ms"), QStringLiteral("Hold each frame of a panel's preview before it runs."),
        QStringLiteral("ms"), QStringLiteral("0"));
    const QCommandLineOption execute_delay_option(QStringLiteral("execute-delay-ms"),
                                                  QStringLiteral("Hold each Apply of a panel before it runs."),
                                                  QStringLiteral("ms"), QStringLiteral("0"));

    parser.addOption(endpoint_option);
    parser.addOption(session_option);
    parser.addOption(extension_option);
    parser.addOption(backend_option);
    parser.addOption(launch_mode_option);
    parser.addOption(host_version_option);
    parser.addOption(capabilities_option);
    parser.addOption(display_name_option);
    parser.addOption(inspect_option);
    parser.addOption(inspect_settings_option);
    parser.addOption(inspect_params_option);
    parser.addOption(inspect_settings_snapshot_option);
    parser.addOption(extension_package_root_option);
    parser.addOption(environment_root_option);
    parser.addOption(runtime_root_option);
    parser.addOption(entry_point_option);
    parser.addOption(process_class_option);
    parser.addOption(contributed_process_option);
#ifdef _WIN32
    parser.addOption(session_secret_handle_option);
#endif
    parser.addOption(stay_alive_option);
    parser.addOption(standalone_option);
    parser.addOption(preview_delay_option);
    parser.addOption(execute_delay_option);
    parser.process(app);

    const QString display_name = parser.value(display_name_option).trimmed();
    const QString endpoint_name = parser.value(endpoint_option).trimmed();
    QString session_id = parser.value(session_option).trimmed();
    QString extension_id = parser.value(extension_option).trimmed();
    const QString sdk_backend = parser.value(backend_option).trimmed();
    const QString launch_mode = parser.value(launch_mode_option).trimmed();
    const bool standalone_mode = parser.isSet(standalone_option);
    if (standalone_mode)
    {
        if (session_id.isEmpty())
        {
            session_id = QStringLiteral("lite-standalone-session");
        }
        if (extension_id.isEmpty())
        {
            extension_id = QStringLiteral("lite.standalone.extension");
        }
    }
    const std::filesystem::path app_dir = std::filesystem::path(QCoreApplication::applicationDirPath().toStdString());
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

    LitePythonHostBridge python_bridge;
    python_bridge.extension_id = extension_id;
    python_bridge.session_id = session_id;
    python_bridge.launch_mode = launch_mode;
    python_bridge.sdk_backend = sdk_backend;
    python_bridge.contributed_process = parser.value(contributed_process_option).trimmed();
    python_bridge.standalone_mode = standalone_mode;
    python_bridge.preview_delay_ms = (std::max)(0, parser.value(preview_delay_option).trimmed().toInt());
    python_bridge.execute_delay_ms = (std::max)(0, parser.value(execute_delay_option).trimmed().toInt());
    g_lite_python_bridge = &python_bridge;

    if (parser.isSet(inspect_option))
    {
        QJsonObject result = makeInspectResult(display_name);
        if (!environment_root.empty() || !entry_point.empty())
        {
            std::string error;
            if (!initializePythonForLiteHost(app_dir, runtime_root, environment_root, extension_package_root,
                                             extension_id, session_id, launch_mode, sdk_backend, false, &error) ||
                !inspectLiteExtension(entry_point, process_class, parser.isSet(inspect_settings_option),
                                      parser.isSet(inspect_params_option), &result, &error))
            {
                printError(error);
                if (Py_IsInitialized())
                {
                    Py_Finalize();
                }
                g_lite_python_bridge = nullptr;
                return 3;
            }
        }
        printJson(result);
        if (Py_IsInitialized())
        {
            Py_Finalize();
        }
        g_lite_python_bridge = nullptr;
        return 0;
    }

    if (!standalone_mode && (endpoint_name.isEmpty() || session_id.isEmpty() || extension_id.isEmpty()))
    {
        printError("Missing required lite host handshake arguments");
        return 2;
    }
    if (sdk_backend != QStringLiteral("protocol") && sdk_backend != QStringLiteral("rpc"))
    {
        printError("Lite host only supports protocol and rpc SDK backends");
        return 2;
    }

    const bool starts_extension = !environment_root.empty() || !entry_point.empty();
    const auto start_extension = [&](std::string* error) {
        QJsonObject unused_inspect_result;
        return initializePythonForLiteHost(app_dir, runtime_root, environment_root, extension_package_root,
                                           extension_id, session_id, launch_mode, sdk_backend, standalone_mode,
                                           error) &&
               inspectLiteExtension(entry_point, process_class, false, false, &unused_inspect_result, error,
                                    standalone_mode);
    };
    const auto end_python = [&]() {
        if (Py_IsInitialized())
        {
            Py_Finalize();
        }
        g_lite_python_bridge = nullptr;
    };

    // A managed session the application asked to connect first starts Python and imports the extension after the
    // handshake, and reports the start with runtime.ready or runtime.bootstrap_failed; so the application waits for the
    // connection only as long as the host takes to start, never as long as the extension's import does.
    const int stay_alive_ms = parser.value(stay_alive_option).trimmed().toInt();
    const bool deferred_managed_start = !standalone_mode && stay_alive_ms > 0 &&
                                        qEnvironmentVariableIntValue("AFTERNIGHT_DEFER_EXTENSION_BOOTSTRAP") == 1;

    if (starts_extension && !deferred_managed_start)
    {
        std::string error;
        if (!start_extension(&error))
        {
            printError(error);
            end_python();
            return 3;
        }
    }

    if (standalone_mode)
    {
        int exit_code = 0;
        if (stay_alive_ms > 0)
        {
            QTimer::singleShot(stay_alive_ms, &app, &QCoreApplication::quit);
            exit_code = app.exec();
        }
        if (Py_IsInitialized())
        {
            std::string close_error;
            callLiteLifecycleHook("on_close", "standalone_finished", &close_error);
            Py_Finalize();
        }
        g_lite_python_bridge = nullptr;
        return exit_code;
    }

    std::string session_secret;
    std::string error;
#ifdef _WIN32
    if (!readSessionSecretFromInheritedHandle(parser.value(session_secret_handle_option), &session_secret, &error))
#else
    if (!readSessionSecretFromInheritedPipe(&session_secret, &error))
#endif
    {
        printError(error);
        return 4;
    }

    QLocalSocket socket;
    socket.connectToServer(endpoint_name);
    if (!socket.waitForConnected(kDefaultIoTimeoutMs))
    {
        printError("Failed to connect to the prepared extension host endpoint");
        return 5;
    }

    an::ui::extensions::RuntimeHelloMessage hello;
    hello.extension_id = extension_id.toStdString();
    hello.session_id = session_id.toStdString();
    hello.protocol_version = an::ui::extensions::kExtensionProtocolVersion;
    hello.sdk_version = an::ui::extensions::kExtensionSdkVersion;
    hello.sdk_backend = sdk_backend.toStdString();
    hello.host_version = parser.value(host_version_option).trimmed().toStdString();
    hello.capabilities = splitCapabilities(parser.value(capabilities_option));
    // What the Lite host serves beside what it was launched with: the panel the application draws, the raise of the
    // extension's own window, where the run it is told of stands, the processes the extension contributes, and what it
    // asks of its view.
    for (const std::string_view served :
         {an::ui::extensions::kHostedPanelCapability, an::ui::extensions::kWindowRaiseCapability,
          an::ui::extensions::kSessionActivityCapability, an::ui::extensions::kContributedProcessesCapability,
          an::ui::extensions::kViewOverlayCapability, an::ui::extensions::kViewSelectionCapability,
          an::ui::extensions::kViewDisplayCapability, an::ui::extensions::kViewHistoryCapability})
    {
        if (std::find(hello.capabilities.begin(), hello.capabilities.end(), served) == hello.capabilities.end())
        {
            hello.capabilities.emplace_back(served);
        }
    }
    hello.session_secret_proof = session_secret;
    hello.native_process_window = false;
    hello.bootstrap_status_messages = deferred_managed_start;

    if (!writeHello(socket, hello, &error))
    {
        printError(error);
        return 7;
    }

    an::ui::extensions::ExtensionProtocolMessage ack_message;
    QByteArray buffered_input;
    if (!an::ui::extensions::readFramedMessage(&socket, kDefaultIoTimeoutMs, &ack_message, &error, &buffered_input))
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
        printError("Host rejected the lite host handshake");
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
        if (!writeProtocolMessage(socket, message, &lifecycle_error))
        {
            printError(lifecycle_error);
            return false;
        }
        return true;
    };

    if (deferred_managed_start && starts_extension && !start_extension(&error))
    {
        if (ack.bootstrap_status_messages)
        {
            write_runtime_lifecycle("runtime.bootstrap_failed", error);
        }
        printError(error);
        end_python();
        return 3;
    }

    python_bridge.socket = &socket;
    python_bridge.buffered_input = &buffered_input;
    python_bridge.extension_id = extension_id;
    python_bridge.session_id = session_id;
    python_bridge.launch_mode = launch_mode;
    python_bridge.sdk_backend = sdk_backend;
    python_bridge.standalone_mode = false;
    // The application draws the session's panel itself: the Lite host serves it.
    python_bridge.hosted_panel = an::ui::extensions::servesHostedPanel(ack);

    // Ready once the extension is imported, as it was at the acknowledgement when it was imported first: its launch
    // hook runs with the application already answering the session's notifications and requests.
    if (ack.bootstrap_status_messages && !write_runtime_lifecycle("runtime.ready"))
    {
        end_python();
        return 13;
    }

    if (Py_IsInitialized())
    {
        // The extension's own run at its start - an extension with no interface does its work there - is told to the
        // application as it reports its progress, and its end once it reported any.
        python_bridge.reports_run = true;
        std::string launch_error;
        if (!callLiteLaunchHook(&launch_error))
        {
            if (python_bridge.told_run)
            {
                python_bridge.tellRunEnded("failed", launch_error);
            }
            printError(launch_error);
            Py_Finalize();
            g_lite_python_bridge = nullptr;
            return 12;
        }
        if (python_bridge.told_run)
        {
            python_bridge.tellRunEnded(python_bridge.cancel_requested ? "cancelled" : "succeeded");
        }
        python_bridge.reports_run = false;

        // The panel the application draws starts from the parameters the extension describes.
        std::string describe_error;
        if (python_bridge.hosted_panel && !python_bridge.close_requested && !describeLitePanel(&describe_error))
        {
            printError(describe_error);
        }
    }

    if (stay_alive_ms <= 0)
    {
        socket.disconnectFromServer();
        if (Py_IsInitialized())
        {
            std::string close_error;
            callLiteLifecycleHook("on_close", "session_finished", &close_error);
            Py_Finalize();
        }
        g_lite_python_bridge = nullptr;
        return 0;
    }

    QObject::connect(&socket, &QLocalSocket::disconnected, &app, &QCoreApplication::quit);
    // What the application sent: what was kept while the extension waited or ran first, since it came first, then what
    // the socket holds now.
    const auto serve_inbound = [&socket, &buffered_input, &app, &python_bridge]() {
        buffered_input.append(socket.readAll());
        while (true)
        {
            an::ui::extensions::ExtensionProtocolMessage inbound;
            std::string local_error;
            if (!python_bridge.kept_inbound.empty())
            {
                inbound = std::move(python_bridge.kept_inbound.front());
                python_bridge.kept_inbound.pop_front();
            }
            else
            {
                const auto status =
                    an::ui::extensions::tryConsumeFramedProtocolMessage(&buffered_input, &inbound, &local_error);
                if (status == an::ui::extensions::ProtocolFrameDecodeStatus::NeedMoreData)
                {
                    return;
                }
                if (status == an::ui::extensions::ProtocolFrameDecodeStatus::InvalidFrame)
                {
                    printError(local_error.empty() ? "Invalid protocol frame" : local_error);
                    app.quit();
                    return;
                }
            }
            if (!handleInboundMessage(socket, inbound, &local_error))
            {
                printError(local_error);
                app.quit();
                return;
            }
        }
    };
    // Served from the loop, never inside the socket's own signal: the extension's code a message runs may ask the
    // application something and wait on this socket for the answer, which a wait nested in the socket's signal never
    // reads on Windows' named pipes.
    bool serving_scheduled = false;
    const auto schedule_serving = [&app, &serving_scheduled, serve_inbound]() {
        if (serving_scheduled)
        {
            return;
        }
        serving_scheduled = true;
        QTimer::singleShot(0, &app, [&serving_scheduled, serve_inbound]() {
            serving_scheduled = false;
            serve_inbound();
        });
    };
    QObject::connect(&socket, &QLocalSocket::readyRead, &app, schedule_serving);
    // What arrived before the loop listened - while the extension started - is served as the loop starts.
    schedule_serving();

    QTimer::singleShot(stay_alive_ms, &app, &QCoreApplication::quit);
    const int exit_code = app.exec();
    if (Py_IsInitialized())
    {
        std::string close_error;
        callLiteLifecycleHook("on_close", "session_finished", &close_error);
        Py_Finalize();
    }
    g_lite_python_bridge = nullptr;
    return exit_code;
}
