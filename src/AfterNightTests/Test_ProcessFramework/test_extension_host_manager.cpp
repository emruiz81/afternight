// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The extension host manager and its launch coordinator against the hosts themselves: the hosts' executables found
/// beside the application, sessions prepared, launched and handshaken, heartbeats, the Lite host's managed and
/// standalone sessions, inspection of an extension's settings and parameters, execute, preview and cancel requests
/// over shared buffers, the execution gate, the coordinator's bound and workflow sessions, view and theme
/// notifications, crashes and their reports, and the session controller's request preparation and presentation. Every
/// image is an asset of the suite's session, exported to and imported from shared buffers through the extension buffer
/// seam. Every package, environment and buffer root a case makes lives under the suite's temporary directory and is
/// removed when the case ends; the main logger is read from its ring buffer. A case that launches the full extension
/// host runs where the host is deployed beside the suite.

#include "Test_ProcessFramework/ExtensionHostTestSupport.h"
#include "Test_ProcessFramework/ExtensionTestSupport.h"
#include "Test_ProcessFramework/FrameworkCaseSupport.h"
#include "Test_ProcessFramework/ProcessFrameworkSuiteSupport.h"
#include "fixtures/TestImageGenerators.h"

#include "app/ExtensionCommon.h"
#include "app/ExtensionEnvironmentManager.h"
#include "app/ExtensionHostManager.h"
#include "app/ExtensionManager.h"
#include "app/ExtensionSdkService.h"
#include "app/SharedBufferManager.h"
#include "components/AnButtonGroup.h"
#include "components/AnCardWidget.h"
#include "components/AnCollapsibleSection.h"
#include "components/AnInfoBanner.h"
#include "components/AnInputField.h"
#include "components/AnSectionHeader.h"
#include "components/AnStyledTable.h"
#include "controls/GlobalSettingsPersistence.h"
#include "controls/ImageViewControl.h"
#include "controls/settings/SettingsKeys.h"
#include "session/ImageDocument.h"

#include <AfterNightConsumerRuntime/EngineSession.h>
#include <AfterNightConsumerRuntime/ImageAsset.h>

#include <AfterNightEngine/Image/ImageDescriptor.h>
#include <AfterNightEngine/Image/SampleType.h>

#include <AfterNightPlatform/Logging.h>

#include <QApplication>
#include <QByteArrayView>
#include <QCborArray>
#include <QCborMap>
#include <QCborValue>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QEvent>
#include <QFile>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QLocalServer>
#include <QLocalSocket>
#include <QProcess>
#include <QProcessEnvironment>
#include <QPushButton>
#include <QRect>
#include <QSize>
#include <QSpinBox>
#include <QStandardPaths>
#include <QStringList>
#include <QTemporaryDir>
#include <QThread>
#include <QThreadPool>
#include <QTimer>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <tuple>
#include <utility>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <signal.h>
#endif

namespace
{

using an::tests::processes::framework::authorTemplatesRoot;
using an::tests::processes::framework::bundledPythonPathForCurrentAppDir;
using an::tests::processes::framework::channelsOf;
using an::tests::processes::framework::createVirtualEnvironment;
using an::tests::processes::framework::discoverAuthorTemplates;
using an::tests::processes::framework::environmentSitePackagesPath;
using an::tests::processes::framework::expectedCustomUiDemoValue;
using an::tests::processes::framework::expectedRtPreviewDemoValue;
using an::tests::processes::framework::exportImageOrReport;
using an::tests::processes::framework::extensionHostLitePathForCurrentAppDir;
using an::tests::processes::framework::extensionHostPathForCurrentAppDir;
using an::tests::processes::framework::findRepositoryBundledExtensionsRoot;
using an::tests::processes::framework::findRepositoryRootForTests;
using an::tests::processes::framework::hasSampleType;
using an::tests::processes::framework::heightOf;
using an::tests::processes::framework::hostExitAllowanceMs;
using an::tests::processes::framework::hostProcessWaitMs;
using an::tests::processes::framework::importBufferOrReport;
using an::tests::processes::framework::InMemorySettings;
using an::tests::processes::framework::kIpcTimingMultiplier;
using an::tests::processes::framework::mainLogContains;
using an::tests::processes::framework::makeImageBuffer;
using an::tests::processes::framework::makeMonoFloatImage;
using an::tests::processes::framework::makeMonoUInt16Image;
using an::tests::processes::framework::makeRgbGradientImage;
using an::tests::processes::framework::provisionBundledRuntimePackageIntoEnvironment;
using an::tests::processes::framework::pumpCoordinatorUntil;
using an::tests::processes::framework::pumpCoordinatorUntilNativeSessionReady;
using an::tests::processes::framework::pumpUntilRequestCompletes;
using an::tests::processes::framework::pumpUntilStateChanges;
using an::tests::processes::framework::removeAllNoThrow;
using an::tests::processes::framework::RemovePathOnExit;
using an::tests::processes::framework::runProcessExpectSuccess;
using an::tests::processes::framework::runtimeRootForCurrentAppDir;
using an::tests::processes::framework::runtimeSitePackagesPath;
using an::tests::processes::framework::sampleAt;
using an::tests::processes::framework::SessionGuard;
using an::tests::processes::framework::shortExtensionEnvironmentsRoot;
using an::tests::processes::framework::stopCoordinatorAndWaitForSessionAbsence;
using an::tests::processes::framework::targetViewOf;
using an::tests::processes::framework::testSession;
using an::tests::processes::framework::uniqueExtensionRuntimeRootForTest;
using an::tests::processes::framework::uniqueSharedBufferRootForTest;
using an::tests::processes::framework::widthOf;

std::string quotedPythonString(const std::string_view value)
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

QTimer* findChildTimerByInterval(QObject* object, const int interval_ms)
{
    if (!object)
    {
        return nullptr;
    }

    const auto timers = object->findChildren<QTimer*>();
    const auto it = std::find_if(timers.begin(), timers.end(),
                                 [interval_ms](QTimer* timer) { return timer && timer->interval() == interval_ms; });
    return it != timers.end() ? *it : nullptr;
}

std::filesystem::path repositoryBundledPythonPath()
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
        const std::array<std::filesystem::path, 1> candidates = {
            root / "python" / "python" / "tools_msvc" / "python.exe",
        };
#else
        const std::array<std::filesystem::path, 4> candidates = {
            root / "python" / "python" / "tools_linux" / "bin" / "python3.14",
            root / "python" / "python" / "tools_linux" / "bin" / "python3",
        };
#endif
        for (const auto& candidate : candidates)
        {
            if (std::filesystem::exists(candidate))
            {
                return candidate;
            }
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

double expectedStandalonePreviewDemoValue(const double source_value, const double selection_value,
                                          const double weight_value, const double strength, const double lift,
                                          const QString& mode, const bool apply_mask, const bool apply_weight,
                                          const double source_scale = 1.0)
{
    double gamma = 1.0 / (1.0 + strength);
    if (mode == QStringLiteral("contrast"))
    {
        gamma *= 0.85;
    }
    else if (mode == QStringLiteral("gentle"))
    {
        gamma = 1.0 / (1.0 + strength * 0.5);
    }

    double clamped = (std::max)(source_value, 0.0);
    if (source_scale > 1.0)
    {
        clamped = std::clamp(clamped / source_scale, 0.0, 1.0);
    }
    double result = std::pow(clamped, gamma) + lift;
    if (apply_mask)
    {
        const double extra_glow = std::pow(clamped, gamma * 0.7) - std::pow(clamped, gamma);
        result += selection_value * extra_glow * (0.3 + strength * 0.5);
    }
    if (apply_weight)
    {
        result += weight_value * strength * 0.15;
    }

    if (source_scale > 1.0)
    {
        result = std::clamp(result, 0.0, 1.0) * source_scale;
    }

    return result;
}

double expectedStandalonePreviewOutputMaskValue(const double source_value, const double strength, const QString& mode,
                                                const double source_scale = 1.0)
{
    double gamma = 1.0 / (1.0 + strength);
    if (mode == QStringLiteral("contrast"))
        gamma *= 0.85;
    else if (mode == QStringLiteral("gentle"))
        gamma = 1.0 / (1.0 + strength * 0.5);

    double clamped = (std::max)(source_value, 0.0);
    if (source_scale > 1.0)
    {
        clamped = std::clamp(clamped / source_scale, 0.0, 1.0);
    }
    const double enhancement = std::abs(std::pow(clamped, gamma) - clamped);
    return enhancement / (1.0 + enhancement);
}

std::filesystem::path environmentPythonPath(const std::filesystem::path& environment_root)
{
#ifdef _WIN32
    return environment_root / "Scripts" / "python.exe";
#else
    return environment_root / "bin" / "python";
#endif
}

std::string trimWhitespace(std::string value)
{
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())) != 0)
    {
        value.erase(value.begin());
    }
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())) != 0)
    {
        value.pop_back();
    }
    return value;
}

void writeTextFile(const std::filesystem::path& path, const std::string& contents)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    ASSERT_TRUE(out.is_open()) << "Failed to open " << path;
    out << contents;
}

#ifndef _WIN32
std::filesystem::path findCCompiler()
{
    for (const QString& candidate : {QStringLiteral("clang"), QStringLiteral("cc"), QStringLiteral("gcc")})
    {
        const QString executable = QStandardPaths::findExecutable(candidate);
        if (!executable.isEmpty())
        {
            return std::filesystem::path(executable.toStdString());
        }
    }

    return {};
}

std::string pythonSysconfigVar(const std::filesystem::path& python, const std::string_view variable_name)
{
    std::string output;
    if (!runProcessExpectSuccess(
            python,
            {QStringLiteral("-c"), QString::fromStdString("import sysconfig; print(sysconfig.get_config_var('" +
                                                          std::string(variable_name) + "') or '')")},
            {}, &output))
    {
        ADD_FAILURE() << output;
        return {};
    }

    return trimWhitespace(output);
}

std::filesystem::path bundledPythonIncludePath(const std::filesystem::path& python)
{
    const auto tools_root = python.parent_path().parent_path();
    for (const auto& candidate : {tools_root / "include" / "python3.14", tools_root / "include"})
    {
        if (std::filesystem::exists(candidate / "Python.h"))
        {
            return candidate.lexically_normal();
        }
    }

    return {};
}

std::filesystem::path buildNativeHelperExecutable(const std::filesystem::path& package_root)
{
    const std::filesystem::path compiler = findCCompiler();
    if (compiler.empty())
    {
        ADD_FAILURE() << "Failed to locate a C compiler for native helper smoke coverage";
        return {};
    }

    const auto bin_dir = package_root / "bin";
    const auto dependency_source = bin_dir / "helper_dep.c";
    const auto executable_source = bin_dir / "helper_runner.c";
    const auto dependency_library = bin_dir / "libhelper_dep.so";
    const auto helper_executable = bin_dir / "helper_runner";

    writeTextFile(dependency_source, "int helper_dep_value(void) { return 23; }\n");
    writeTextFile(executable_source, "#include <stdio.h>\n"
                                     "int helper_dep_value(void);\n"
                                     "int main(void)\n"
                                     "{\n"
                                     "    printf(\"%d\\n\", helper_dep_value());\n"
                                     "    return 0;\n"
                                     "}\n");

    std::string output;
    if (!runProcessExpectSuccess(compiler,
                                 {QStringLiteral("-shared"), QStringLiteral("-fPIC"),
                                  QString::fromStdString(dependency_source.string()),
                                  QStringLiteral("-Wl,-soname,libhelper_dep.so"), QStringLiteral("-o"),
                                  QString::fromStdString(dependency_library.string())},
                                 {}, &output))
    {
        ADD_FAILURE() << output;
        return {};
    }

    if (!runProcessExpectSuccess(compiler,
                                 {QString::fromStdString(executable_source.string()), QStringLiteral("-L"),
                                  QString::fromStdString(bin_dir.string()), QStringLiteral("-lhelper_dep"),
                                  QStringLiteral("-Wl,-rpath,$ORIGIN"), QStringLiteral("-o"),
                                  QString::fromStdString(helper_executable.string())},
                                 {}, &output))
    {
        ADD_FAILURE() << output;
        return {};
    }

    return helper_executable;
}

std::filesystem::path createCompiledNativeWheel(const std::filesystem::path& root, const std::string_view package_name,
                                                const std::string_view version)
{
    const std::filesystem::path python = repositoryBundledPythonPath();
    const std::filesystem::path compiler = findCCompiler();
    if (python.empty())
    {
        ADD_FAILURE() << "Failed to locate the repository bundled Python runtime";
        return {};
    }
    if (compiler.empty())
    {
        ADD_FAILURE() << "Failed to locate a C compiler for compiled-wheel smoke coverage";
        return {};
    }

    std::string include_dir = pythonSysconfigVar(python, "INCLUDEPY");
    const std::string ext_suffix = pythonSysconfigVar(python, "EXT_SUFFIX");
    if (include_dir.empty() || !std::filesystem::exists(std::filesystem::path(include_dir) / "Python.h"))
    {
        const auto fallback_include_dir = bundledPythonIncludePath(python);
        if (!fallback_include_dir.empty())
        {
            include_dir = fallback_include_dir.string();
        }
    }
    if (include_dir.empty() || ext_suffix.empty())
    {
        ADD_FAILURE() << "Failed to query bundled Python build metadata for compiled-wheel smoke coverage";
        return {};
    }

    const std::string package_name_string(package_name);
    const std::string version_string(version);
    const std::string extension_module_name = package_name_string + "_ext";
    const std::string dependency_symbol = package_name_string + "_dep_value";
    const std::string dependency_library_name = "lib" + package_name_string + "_dep.so";

    const auto staging_root = root / "compiled_wheel_staging";
    const auto package_dir = staging_root / package_name_string;
    const auto dist_info_dir = staging_root / (package_name_string + "-" + version_string + ".dist-info");
    const auto dependency_source = package_dir / (package_name_string + "_dep.c");
    const auto extension_source = package_dir / (extension_module_name + ".c");
    const auto dependency_library = package_dir / dependency_library_name;
    const auto extension_binary = package_dir / (extension_module_name + ext_suffix);
    const auto wheel_script = root / "make_compiled_wheel.py";
    const auto wheel_path =
        root / "wheelhouse" / (package_name_string + "-" + version_string + "-cp314-cp314-manylinux_2_28_x86_64.whl");

    writeTextFile(package_dir / "__init__.py", "from ." + extension_module_name +
                                                   " import get_value\n"
                                                   "__all__ = ['get_value']\n");
    writeTextFile(dependency_source, "int " + dependency_symbol + "(void) { return 41; }\n");
    writeTextFile(extension_source,
                  "#include <Python.h>\n"
                  "int " +
                      dependency_symbol +
                      "(void);\n"
                      "static PyObject* get_value(PyObject*, PyObject*)\n"
                      "{\n"
                      "    return PyLong_FromLong(" +
                      dependency_symbol +
                      "());\n"
                      "}\n"
                      "static PyMethodDef methods[] = {\n"
                      "    {\"get_value\", get_value, METH_NOARGS, \"Return the native smoke-test value.\"},\n"
                      "    {NULL, NULL, 0, NULL}\n"
                      "};\n"
                      "static struct PyModuleDef module = {\n"
                      "    PyModuleDef_HEAD_INIT,\n"
                      "    \"" +
                      extension_module_name +
                      "\",\n"
                      "    NULL,\n"
                      "    -1,\n"
                      "    methods\n"
                      "};\n"
                      "PyMODINIT_FUNC PyInit_" +
                      extension_module_name +
                      "(void)\n"
                      "{\n"
                      "    return PyModule_Create(&module);\n"
                      "}\n");
    writeTextFile(dist_info_dir / "WHEEL", "Wheel-Version: 1.0\n"
                                           "Generator: afternight-test\n"
                                           "Root-Is-Purelib: false\n"
                                           "Tag: cp314-cp314-manylinux_2_28_x86_64\n");
    writeTextFile(dist_info_dir / "METADATA", "Metadata-Version: 2.1\n"
                                              "Name: " +
                                                  package_name_string +
                                                  "\n"
                                                  "Version: " +
                                                  version_string +
                                                  "\n"
                                                  "Summary: Native wheel smoke test\n");
    writeTextFile(wheel_script,
                  R"py(import base64, csv, hashlib, io, pathlib, sys, zipfile
staging_root = pathlib.Path(sys.argv[1])
wheel_path = pathlib.Path(sys.argv[2])
dist_info_dir = sys.argv[3]
files = sorted(path for path in staging_root.rglob("*") if path.is_file())
records = []
wheel_path.parent.mkdir(parents=True, exist_ok=True)
with zipfile.ZipFile(wheel_path, "w", zipfile.ZIP_DEFLATED) as archive:
    for file in files:
        rel = file.relative_to(staging_root).as_posix()
        data = file.read_bytes()
        digest = base64.urlsafe_b64encode(hashlib.sha256(data).digest()).rstrip(b"=").decode("ascii")
        records.append((rel, f"sha256={digest}", str(len(data))))
        archive.writestr(rel, data)
    record_path = f"{dist_info_dir}/RECORD"
    records.append((record_path, "", ""))
    buffer = io.StringIO()
    csv.writer(buffer, lineterminator="\n").writerows(records)
    archive.writestr(record_path, buffer.getvalue().encode("utf-8"))
)py");

    std::string output;
    if (!runProcessExpectSuccess(compiler,
                                 {QStringLiteral("-shared"), QStringLiteral("-fPIC"),
                                  QString::fromStdString(dependency_source.string()),
                                  QStringLiteral("-Wl,-soname,") + QString::fromStdString(dependency_library_name),
                                  QStringLiteral("-o"), QString::fromStdString(dependency_library.string())},
                                 {}, &output))
    {
        ADD_FAILURE() << output;
        return {};
    }

    if (!runProcessExpectSuccess(compiler,
                                 {QStringLiteral("-shared"), QStringLiteral("-fPIC"),
                                  QString::fromStdString(extension_source.string()), QStringLiteral("-I"),
                                  QString::fromStdString(include_dir), QStringLiteral("-L"),
                                  QString::fromStdString(package_dir.string()),
                                  QStringLiteral("-l") + QString::fromStdString(package_name_string + "_dep"),
                                  QStringLiteral("-Wl,-rpath,$ORIGIN"), QStringLiteral("-o"),
                                  QString::fromStdString(extension_binary.string())},
                                 {}, &output))
    {
        ADD_FAILURE() << output;
        return {};
    }

    if (!runProcessExpectSuccess(
            python,
            {QString::fromStdString(wheel_script.string()), QString::fromStdString(staging_root.string()),
             QString::fromStdString(wheel_path.string()), QString::fromStdString(dist_info_dir.filename().string())},
            {}, &output))
    {
        ADD_FAILURE() << output;
        return {};
    }

    if (!std::filesystem::exists(wheel_path))
    {
        ADD_FAILURE() << "Expected compiled wheel to be created at " << wheel_path;
        return {};
    }

    return wheel_path;
}

std::filesystem::path createPlatformTaggedPy3NativeWheel(const std::filesystem::path& root,
                                                         const std::string_view package_name,
                                                         const std::string_view version,
                                                         const std::string_view platform_tag)
{
    const std::filesystem::path python = repositoryBundledPythonPath();
    const std::filesystem::path compiler = findCCompiler();
    if (python.empty())
    {
        ADD_FAILURE() << "Failed to locate the repository bundled Python runtime";
        return {};
    }
    if (compiler.empty())
    {
        ADD_FAILURE() << "Failed to locate a C compiler for platform-wheel smoke coverage";
        return {};
    }

    const std::string package_name_string(package_name);
    const std::string version_string(version);
    const std::string dependency_symbol = package_name_string + "_dep_value";
    const std::string dependency_library_name = "lib" + package_name_string + "_dep.so";

    const auto staging_root = root / "platform_tagged_native_wheel_staging";
    const auto package_dir = staging_root / package_name_string;
    const auto dist_info_dir = staging_root / (package_name_string + "-" + version_string + ".dist-info");
    const auto dependency_source = package_dir / (package_name_string + "_dep.c");
    const auto dependency_library = package_dir / dependency_library_name;
    const auto wheel_script = root / "make_platform_tagged_native_wheel.py";
    const auto wheel_path =
        root / "wheelhouse" /
        (package_name_string + "-" + version_string + "-py3-none-" + std::string(platform_tag) + ".whl");

    writeTextFile(package_dir / "__init__.py", "import ctypes\n"
                                               "import pathlib\n\n"
                                               "_lib = ctypes.CDLL(str(pathlib.Path(__file__).with_name('" +
                                                   dependency_library_name +
                                                   "')))\n"
                                                   "_lib." +
                                                   dependency_symbol +
                                                   ".restype = ctypes.c_int\n\n"
                                                   "def get_value():\n"
                                                   "    return int(_lib." +
                                                   dependency_symbol + "())\n");
    writeTextFile(dependency_source, "int " + dependency_symbol + "(void) { return 41; }\n");
    writeTextFile(dist_info_dir / "WHEEL", "Wheel-Version: 1.0\n"
                                           "Generator: afternight-test\n"
                                           "Root-Is-Purelib: false\n"
                                           "Tag: py3-none-" +
                                               std::string(platform_tag) + "\n");
    writeTextFile(dist_info_dir / "METADATA", "Metadata-Version: 2.1\n"
                                              "Name: " +
                                                  package_name_string +
                                                  "\n"
                                                  "Version: " +
                                                  version_string +
                                                  "\n"
                                                  "Summary: Platform-tagged native wheel smoke test\n");
    writeTextFile(wheel_script,
                  R"py(import base64, csv, hashlib, io, pathlib, sys, zipfile
staging_root = pathlib.Path(sys.argv[1])
wheel_path = pathlib.Path(sys.argv[2])
dist_info_dir = sys.argv[3]
files = sorted(path for path in staging_root.rglob("*") if path.is_file())
records = []
wheel_path.parent.mkdir(parents=True, exist_ok=True)
with zipfile.ZipFile(wheel_path, "w", zipfile.ZIP_DEFLATED) as archive:
    for file in files:
        rel = file.relative_to(staging_root).as_posix()
        data = file.read_bytes()
        digest = base64.urlsafe_b64encode(hashlib.sha256(data).digest()).rstrip(b"=").decode("ascii")
        records.append((rel, f"sha256={digest}", str(len(data))))
        archive.writestr(rel, data)
    record_path = f"{dist_info_dir}/RECORD"
    records.append((record_path, "", ""))
    buffer = io.StringIO()
    csv.writer(buffer, lineterminator="\n").writerows(records)
    archive.writestr(record_path, buffer.getvalue().encode("utf-8"))
)py");

    std::string output;
    if (!runProcessExpectSuccess(compiler,
                                 {QStringLiteral("-shared"), QStringLiteral("-fPIC"),
                                  QString::fromStdString(dependency_source.string()),
                                  QStringLiteral("-Wl,-soname,") + QString::fromStdString(dependency_library_name),
                                  QStringLiteral("-o"), QString::fromStdString(dependency_library.string())},
                                 {}, &output))
    {
        ADD_FAILURE() << output;
        return {};
    }

    if (!runProcessExpectSuccess(
            python,
            {QString::fromStdString(wheel_script.string()), QString::fromStdString(staging_root.string()),
             QString::fromStdString(wheel_path.string()), QString::fromStdString(dist_info_dir.filename().string())},
            {}, &output))
    {
        ADD_FAILURE() << output;
        return {};
    }

    if (!std::filesystem::exists(wheel_path))
    {
        ADD_FAILURE() << "Expected platform-tagged native wheel to be created at " << wheel_path;
        return {};
    }

    removeAllNoThrow(staging_root);
    removeAllNoThrow(wheel_script);

    return wheel_path;
}

std::string sha256HexForFile(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open())
    {
        return {};
    }

    QCryptographicHash hash(QCryptographicHash::Sha256);
    std::array<char, 64 * 1024> buffer{};
    while (in)
    {
        in.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const std::streamsize count = in.gcount();
        if (count > 0)
        {
            hash.addData(QByteArrayView(buffer.data(), static_cast<qsizetype>(count)));
        }
    }

    return hash.result().toHex().toStdString();
}
#endif

an::ui::extensions::ExtensionHostLaunchRequest makeLaunchRequest(const std::filesystem::path& host_path,
                                                                 const std::vector<std::string>& host_arguments = {})
{
    an::ui::extensions::ExtensionHostLaunchRequest request;
    request.extension_id = "an.demo";
    request.sdk_backend = "runtime";
    request.host_version = "test-host";
    request.capabilities = {"execute", "preview"};
    request.host_executable = host_path;
    request.host_arguments = host_arguments;
    return request;
}

an::ui::extensions::ExtensionProtocolMessage makeDeclarativeUiDescribeRequest(const std::string_view session_id,
                                                                              const std::string_view request_id,
                                                                              const bool supports_ui_events,
                                                                              const bool supports_ui_validation)
{
    an::ui::extensions::ExtensionProtocolMessage request;
    request.type = "ui.describe";
    request.session_id = std::string(session_id);
    request.request_id = std::string(request_id);
    request.fields =
        QCborMap{{QStringLiteral("tree"),
                  QCborMap{{QStringLiteral("root"),
                            QCborMap{{QStringLiteral("widget"), QStringLiteral("column")},
                                     {QStringLiteral("children"),
                                      QCborArray{QCborMap{{QStringLiteral("id"), QStringLiteral("strength")},
                                                          {QStringLiteral("widget"), QStringLiteral("slider")},
                                                          {QStringLiteral("label"), QStringLiteral("Strength")},
                                                          {QStringLiteral("min"), 0.0},
                                                          {QStringLiteral("max"), 1.0},
                                                          {QStringLiteral("step"), 0.05},
                                                          {QStringLiteral("value"), 0.5}}}}}}}},
                 {QStringLiteral("supports_ui_events"), supports_ui_events},
                 {QStringLiteral("supports_ui_validation"), supports_ui_validation}};
    return request;
}

an::ui::extensions::ExtensionProtocolMessage makeDeclarativeUiPatchRequest(const std::string_view session_id,
                                                                           const std::string_view request_id)
{
    an::ui::extensions::ExtensionProtocolMessage request;
    request.type = "ui.patch";
    request.session_id = std::string(session_id);
    request.request_id = std::string(request_id);
    request.fields =
        QCborMap{{QStringLiteral("patches"), QCborArray{QCborMap{{QStringLiteral("id"), QStringLiteral("strength")},
                                                                 {QStringLiteral("prop"), QStringLiteral("value")},
                                                                 {QStringLiteral("value"), 0.8}}}}};
    return request;
}

bool pumpManagedSessions(an::ui::extensions::ExtensionHostManager& manager,
                         an::ui::extensions::ExtensionHostExecutionGateState* gate_state,
                         const std::vector<an::ui::extensions::ExtensionHostRunningSession*>& sessions,
                         const int timeout_ms, std::string* error)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeout_ms)
    {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 1);
        for (auto* session : sessions)
        {
            if (session && session->state == an::ui::extensions::ExtensionHostSessionState::Active)
            {
                if (!manager.pumpManagedSession(gate_state, session, 20, error))
                {
                    return false;
                }
            }
        }
    }

    return true;
}

bool pumpCoordinatorUntilSessionCount(an::ui::extensions::ExtensionHostLaunchCoordinator& coordinator,
                                      const std::size_t expected_count, const int timeout_ms)
{
    // A session leaves the count once its host has exited.
    const int bound_ms = timeout_ms + hostExitAllowanceMs();
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < bound_ms)
    {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 1);
        coordinator.pumpSessions();
        if (coordinator.activeSessionCount() == expected_count)
        {
            return true;
        }
        QThread::msleep(20);
    }

    return coordinator.activeSessionCount() == expected_count;
}

// A view a session can be bound to that shows no image, for the cases that observe a session's lifecycle rather
// than its requests: one view for the whole run, made when a case first asks for it and deleted once every case has
// run, so a coordinator a case let go never names a view that is gone. A request prepared for it is refused because
// it shows no image.
class ImagelessTargetView final : public ::testing::Environment
{
public:
    [[nodiscard]] ImageViewControl* view()
    {
        if (m_view == nullptr)
        {
            m_view = new ImageViewControl();
            m_view->initialize("extension-imageless-target", ViewControlMode::standard);
        }
        return m_view;
    }

    void TearDown() override
    {
        delete m_view;
        m_view = nullptr;
    }

private:
    ImageViewControl* m_view = nullptr;
};

ImagelessTargetView* const g_imagelessTarget =
    static_cast<ImagelessTargetView*>(::testing::AddGlobalTestEnvironment(new ImagelessTargetView()));

ImageViewControl* fakeTargetControl()
{
    return g_imagelessTarget->view();
}

// Runs an execute result's import to its landing on the GUI thread, as the coordinator's pump does.
[[nodiscard]] std::optional<an::ui::extensions::ExtensionSessionController::LandedMaterialization>
landExecuteMaterialization(an::ui::extensions::ExtensionSessionController& controller)
{
    controller.beginExecuteMaterialization(testSession(), []() {});
    EXPECT_TRUE(an::tests::processes::pumpUntil([&controller]() { return controller.hasLandedMaterialization(); }));
    return controller.takeLandedMaterialization();
}

bool pathMissingOrEmpty(const std::filesystem::path& path)
{
    std::error_code ec;
    if (!std::filesystem::exists(path, ec))
    {
        return true;
    }
    return std::filesystem::is_directory(path, ec) &&
           std::filesystem::directory_iterator(path, ec) == std::filesystem::directory_iterator();
}

bool readPidFile(const std::filesystem::path& path, qint64* pid)
{
    if (pid)
    {
        *pid = -1;
    }

    std::ifstream in(path, std::ios::binary);
    if (!in.is_open())
    {
        return false;
    }

    std::string text;
    std::getline(in, text);
    text = trimWhitespace(std::move(text));
    if (text.empty())
    {
        return false;
    }

    try
    {
        const qint64 value = static_cast<qint64>(std::stoll(text));
        if (value <= 0)
        {
            return false;
        }
        if (pid)
        {
            *pid = value;
        }
        return true;
    }
    catch (...)
    {
        return false;
    }
}

bool waitForPidFile(const std::filesystem::path& path, const int timeout_ms, qint64* pid)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeout_ms)
    {
        if (readPidFile(path, pid))
        {
            return true;
        }
        QThread::msleep(20);
    }

    return readPidFile(path, pid);
}

bool processExists(const qint64 pid)
{
    if (pid <= 0)
    {
        return false;
    }

#ifdef _WIN32
    HANDLE process = ::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, static_cast<DWORD>(pid));
    if (!process)
    {
        return false;
    }

    DWORD exit_code = 0;
    const bool running = ::GetExitCodeProcess(process, &exit_code) != 0 && exit_code == STILL_ACTIVE;
    ::CloseHandle(process);
    return running;
#else
    return ::kill(static_cast<pid_t>(pid), 0) == 0 || errno == EPERM;
#endif
}

bool waitForProcessExit(const qint64 pid, const int timeout_ms)
{
    const int bound_ms = timeout_ms + hostExitAllowanceMs();
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < bound_ms)
    {
        if (!processExists(pid))
        {
            return true;
        }
        QThread::msleep(20);
    }

    return !processExists(pid);
}

bool tryWriteTextFile(const std::filesystem::path& path, const std::string& contents)
{
    std::error_code ec;
    const auto parent = path.parent_path();
    if (!parent.empty())
    {
        std::filesystem::create_directories(parent, ec);
        if (ec)
        {
            return false;
        }
    }

    std::ofstream out(path, std::ios::binary);
    if (!out.is_open())
    {
        return false;
    }

    out << contents;
    return out.good();
}

std::string readTextFileForTests(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open())
    {
        return {};
    }

    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

std::filesystem::path sequenceWorkflowFixtureContractPath()
{
    const auto repository_root = findRepositoryRootForTests();
    if (repository_root.empty())
    {
        ADD_FAILURE() << "Failed to locate repository root for sequence workflow fixtures";
        return {};
    }

    return repository_root / "src" / "AfterNightTests" / "fixtures" / "workflow" / "sequence_workflow_fixtures.json";
}

QJsonObject loadSequenceWorkflowFixtureContract()
{
    const auto path = sequenceWorkflowFixtureContractPath();
    if (path.empty())
    {
        return {};
    }

    const std::string contents = readTextFileForTests(path);
    if (contents.empty())
    {
        ADD_FAILURE() << "Failed to read sequence workflow fixture contract: " << path;
        return {};
    }

    QJsonParseError parse_error;
    const QJsonDocument document = QJsonDocument::fromJson(QByteArray::fromStdString(contents), &parse_error);
    if (parse_error.error != QJsonParseError::NoError)
    {
        ADD_FAILURE() << "Invalid sequence workflow fixture JSON at " << path << ": "
                      << parse_error.errorString().toStdString();
        return {};
    }
    if (!document.isObject())
    {
        ADD_FAILURE() << "Sequence workflow fixture contract must be a JSON object: " << path;
        return {};
    }

    return document.object();
}

QJsonObject findSequenceWorkflowDataset(const QJsonObject& fixture_contract, const QString& dataset_id)
{
    for (const auto& dataset_value : fixture_contract.value(QStringLiteral("datasets")).toArray())
    {
        const QJsonObject dataset = dataset_value.toObject();
        if (dataset.value(QStringLiteral("id")).toString() == dataset_id)
        {
            return dataset;
        }
    }

    return {};
}

bool jsonStringArrayContains(const QJsonArray& array, const QString& value)
{
    return std::any_of(array.begin(), array.end(),
                       [&value](const QJsonValue& entry) { return entry.toString() == value; });
}

QJsonDocument inspectExtensionProcessSchema(const std::filesystem::path& host_path,
                                            const std::filesystem::path& runtime_root,
                                            const std::filesystem::path& package_root,
                                            const std::filesystem::path& env_root, const QString& entry_point,
                                            const QString& process_class,
                                            const QProcessEnvironment& process_environment, const bool inspect_settings,
                                            const bool inspect_params, std::string* output)
{
    QProcess inspect;
    inspect.setProgram(QString::fromStdString(host_path.string()));
    inspect.setProcessChannelMode(QProcess::MergedChannels);
    inspect.setProcessEnvironment(process_environment);

    QStringList arguments;
    arguments << QStringLiteral("--inspect");
    if (inspect_settings)
    {
        arguments << QStringLiteral("--inspect-settings");
    }
    if (inspect_params)
    {
        arguments << QStringLiteral("--inspect-params");
    }
    arguments << QStringLiteral("--sdk-backend") << QStringLiteral("runtime")
              << QStringLiteral("--extension-package-root") << QString::fromStdString(package_root.string())
              << QStringLiteral("--entry-point") << entry_point << QStringLiteral("--process-class") << process_class
              << QStringLiteral("--environment-root") << QString::fromStdString(env_root.string())
              << QStringLiteral("--runtime-root") << QString::fromStdString(runtime_root.string());
    inspect.setArguments(arguments);

    inspect.start();
    if (!inspect.waitForStarted() || !inspect.waitForFinished(hostProcessWaitMs()))
    {
        if (output != nullptr)
        {
            *output = inspect.readAllStandardOutput().toStdString();
        }
        return {};
    }

    const std::string merged_output = inspect.readAllStandardOutput().toStdString();
    if (output != nullptr)
    {
        *output = merged_output;
    }

    if (inspect.exitStatus() != QProcess::NormalExit || inspect.exitCode() != 0)
    {
        return {};
    }

    QJsonParseError parse_error;
    const QJsonDocument document =
        QJsonDocument::fromJson(QByteArray::fromStdString(merged_output).trimmed(), &parse_error);
    if (parse_error.error != QJsonParseError::NoError || !document.isObject())
    {
        return {};
    }

    return document;
}

QJsonObject findSchemaEntryById(const QJsonArray& schema, const QString& id)
{
    const auto it = std::find_if(schema.begin(), schema.end(), [&id](const QJsonValue& value) {
        return value.toObject().value(QStringLiteral("id")).toString() == id;
    });
    return it != schema.end() ? it->toObject() : QJsonObject{};
}

QJsonArray choiceOptionValues(const QJsonObject& schema_entry)
{
    QJsonArray values;
    for (const auto& option_value : schema_entry.value(QStringLiteral("options")).toArray())
    {
        const QJsonArray option = option_value.toArray();
        if (option.size() >= 2)
        {
            values.append(option.at(1));
        }
    }
    return values;
}

QJsonArray choiceOptionLabels(const QJsonObject& schema_entry)
{
    QJsonArray labels;
    for (const auto& option_value : schema_entry.value(QStringLiteral("options")).toArray())
    {
        const QJsonArray option = option_value.toArray();
        if (!option.isEmpty())
        {
            labels.append(option.at(0));
        }
    }
    return labels;
}

struct ScopedEnvironmentVariable
{
    explicit ScopedEnvironmentVariable(const char* key, const QByteArray& value)
        : key_(key), original_value_(qgetenv(key)), had_original_(qEnvironmentVariableIsSet(key))
    {
        qputenv(key_, value);
    }

    ~ScopedEnvironmentVariable()
    {
        if (had_original_)
        {
            qputenv(key_, original_value_);
        }
        else
        {
            qunsetenv(key_);
        }
    }

private:
    const char* key_ = nullptr;
    QByteArray original_value_;
    bool had_original_ = false;
};

std::string environmentVariableValue(const char* key)
{
    const QByteArray value = qgetenv(key);
    return value.isEmpty() ? std::string{} : value.toStdString();
}

// Allocates a buffer under a manager's root, reporting a refusal as text.
bool allocateBufferOrReport(const an::ui::extensions::SharedBufferManager& manager,
                            const an::ui::extensions::SharedImageBufferSpec& spec,
                            an::ui::extensions::SharedImageBuffer* out_buffer, std::string* error)
{
    an::Result<an::ui::extensions::SharedImageBuffer> buffer = manager.allocateBuffer(spec);
    if (!buffer.hasValue())
    {
        if (error)
        {
            *error = buffer.status().message();
        }
        return false;
    }
    *out_buffer = std::move(*buffer);
    return true;
}

// Opens a buffer a descriptor names under a manager's root, reporting a refusal as text.
bool openBufferOrReport(const an::ui::extensions::SharedBufferManager& manager,
                        const an::ui::extensions::SharedBufferDescriptor& descriptor,
                        an::ui::extensions::SharedImageBuffer* out_buffer, std::string* error)
{
    an::Result<an::ui::extensions::SharedImageBuffer> buffer = manager.openBuffer(descriptor);
    if (!buffer.hasValue())
    {
        if (error)
        {
            *error = buffer.status().message();
        }
        return false;
    }
    *out_buffer = std::move(*buffer);
    return true;
}

// A grey Float32 image of one level of the suite's session; null when it cannot be made.
[[nodiscard]] std::shared_ptr<an::consumer::ImageAsset>
makeGreyFloatImage(const std::uint32_t width, const std::uint32_t height, const double level)
{
    an::Result<an::consumer::ImageAsset> made =
        an::tests::fixtures::createGreyFloatAsset(testSession(), width, height, level);
    EXPECT_TRUE(made.hasValue()) << made.status().message();
    return made.hasValue() ? std::make_shared<an::consumer::ImageAsset>(std::move(*made)) : nullptr;
}

an::ui::extensions::SharedImageBuffer makeSourceBuffer(const std::filesystem::path& root_path,
                                                       const std::string& session_id, std::string* error)
{
    an::ui::extensions::SharedBufferManager manager(root_path);
    an::ui::extensions::SharedImageBufferSpec spec;
    spec.sessionId = session_id;
    spec.bufferId = "source-buffer";
    spec.symbolicName = "source";
    spec.width = 5;
    spec.height = 3;
    spec.channels = 1;
    spec.pixelType = an::ui::extensions::SharedBufferPixelType::UInt8;
    spec.pixelFormat = an::ui::extensions::SharedBufferPixelFormat::Mono;
    spec.colorSpace = an::ui::extensions::SharedBufferColorSpace::Grayscale;
    spec.accessMode = an::ui::extensions::SharedBufferAccessMode::ReadWrite;
    spec.bufferRole = an::ui::extensions::SharedBufferRole::Source;

    an::ui::extensions::SharedImageBuffer buffer;
    if (!allocateBufferOrReport(manager, spec, &buffer, error))
    {
        return {};
    }

    auto* bytes = reinterpret_cast<std::uint8_t*>(buffer.bytes());
    if (!bytes)
    {
        if (error)
        {
            *error = "Source shared buffer bytes were null";
        }
        return {};
    }

    for (std::uint64_t i = 0; i < buffer.mappedSize(); ++i)
    {
        bytes[i] = static_cast<std::uint8_t>((11u + i) % 251u);
    }

    return buffer;
}

bool writeMetadataSidecar(const std::filesystem::path& path, const QJsonObject& metadata, std::string* error)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    if (!out.is_open())
    {
        if (error)
        {
            *error = "Failed to open metadata sidecar for writing";
        }
        return false;
    }

    out << QJsonDocument(metadata).toJson(QJsonDocument::Indented).toStdString();
    return true;
}

std::filesystem::path managedMetadataSidecarPath(const an::ui::extensions::SharedBufferDescriptor& descriptor)
{
    const std::filesystem::path mapped_file_path =
        std::filesystem::path(descriptor.mapped_file_path).lexically_normal();
    if (mapped_file_path.empty())
    {
        return {};
    }

    return mapped_file_path.parent_path() / (mapped_file_path.filename().string() + ".metadata.json");
}

an::ui::extensions::SharedImageBuffer
makeSourceBufferWithMetadata(const std::filesystem::path& root_path, const std::string& session_id,
                             const QJsonObject& metadata, std::filesystem::path* out_sidecar_path, std::string* error)
{
    an::ui::extensions::SharedBufferManager manager(root_path);
    an::ui::extensions::SharedImageBufferSpec spec;
    spec.sessionId = session_id;
    spec.bufferId = "source-buffer";
    spec.symbolicName = "source";
    spec.width = 5;
    spec.height = 3;
    spec.channels = 1;
    spec.pixelType = an::ui::extensions::SharedBufferPixelType::UInt8;
    spec.pixelFormat = an::ui::extensions::SharedBufferPixelFormat::Mono;
    spec.colorSpace = an::ui::extensions::SharedBufferColorSpace::Grayscale;
    spec.accessMode = an::ui::extensions::SharedBufferAccessMode::ReadWrite;
    spec.bufferRole = an::ui::extensions::SharedBufferRole::Source;
    spec.metadataSidecar = (root_path / session_id / "source-buffer.bin.metadata.json").generic_string();

    an::ui::extensions::SharedImageBuffer buffer;
    if (!allocateBufferOrReport(manager, spec, &buffer, error))
    {
        return {};
    }
    if (!writeMetadataSidecar(std::filesystem::path(spec.metadataSidecar), metadata, error))
    {
        return {};
    }

    auto* bytes = reinterpret_cast<std::uint8_t*>(buffer.bytes());
    if (!bytes)
    {
        if (error)
        {
            *error = "Source shared buffer bytes were null";
        }
        return {};
    }

    for (std::uint64_t i = 0; i < buffer.mappedSize(); ++i)
    {
        bytes[i] = static_cast<std::uint8_t>((11u + i) % 251u);
    }

    if (out_sidecar_path)
    {
        *out_sidecar_path = std::filesystem::path(spec.metadataSidecar);
    }
    return buffer;
}

// A colour Float32 gradient of the suite's session exported as a source buffer; the image is handed back for the case
// to compare the extension's answer with.
an::ui::extensions::SharedImageBuffer
makeLargeRgbSourceBuffer(const std::filesystem::path& root_path, const std::string& session_id, const uint32_t width,
                         const uint32_t height, std::shared_ptr<an::consumer::ImageAsset>* image, std::string* error)
{
    an::Result<an::consumer::ImageAsset> made =
        an::tests::fixtures::createRgbGradientAsset(testSession(), width, height);
    if (!made.hasValue())
    {
        if (error)
        {
            *error = "Failed to create large RGB source image: " + std::string(made.status().message());
        }
        return {};
    }
    auto source_image = std::make_shared<an::consumer::ImageAsset>(std::move(*made));

    an::ui::extensions::SharedBufferManager manager(root_path);
    an::ui::extensions::SharedImageBuffer buffer;
    if (!exportImageOrReport(manager, *source_image, session_id, an::ui::extensions::SharedBufferRole::Source, &buffer,
                             error, "large-source-buffer", "source"))
    {
        return {};
    }

    if (image)
    {
        *image = std::move(source_image);
    }
    return buffer;
}

} // anonymous namespace

// The full host is found beside the application in its directory's root.
TEST(ExtensionHostManagerTest, BundledExtensionHostExecutableForAppDirPrefersRootBinary)
{
    const an::tests::processes::framework::ScratchDirectory scratch("ext-host-appdir");
    const std::filesystem::path app_dir = scratch.path();

#ifdef _WIN32
    const auto expected = app_dir / "AfterNightExtensionHost.exe";
#else
    const auto expected = app_dir / "AfterNightExtensionHost";
#endif

    {
        std::ofstream out(expected);
        out << "stub";
    }

    EXPECT_EQ(an::ui::extensions::bundled_extension_host_executable_for_app_dir(app_dir), expected);
}

// The Lite host is found beside the application in its directory's root.
TEST(ExtensionHostManagerTest, BundledExtensionHostLiteExecutableForAppDirPrefersRootBinary)
{
    const an::tests::processes::framework::ScratchDirectory scratch("ext-lite-appdir");
    const std::filesystem::path app_dir = scratch.path();

#ifdef _WIN32
    const auto expected = app_dir / "AfterNightExtensionHostLite.exe";
#else
    const auto expected = app_dir / "AfterNightExtensionHostLite";
#endif

    {
        std::ofstream out(expected);
        out << "stub";
    }

    EXPECT_EQ(an::ui::extensions::bundled_extension_host_lite_executable_for_app_dir(app_dir), expected);
}

// Every prepared session has its own identity, endpoint and secret, and its acknowledgement names that identity.
// A full host's native window is launched with the rate the Processing page states, which the host's settings never
// hold, and with what the manifest lets a frame of a drag run the extension's preview over; an extension that states
// nothing runs every frame over the whole image, the host's default, and is told no scaling.
TEST(ExtensionHostManagerTest, ANativeWindowIsLaunchedWithTheFrameRateAndThePreviewScalingTheManifestStates)
{
    using an::ui::extensions::ExtensionCapabilities;
    using an::ui::extensions::ExtensionPreviewScaling;
    using an::ui::extensions::nativePreviewHostArguments;

    ExtensionCapabilities capabilities;
    EXPECT_EQ(nativePreviewHostArguments(capabilities, 30u), (std::vector<std::string>{"--preview-frame-rate", "30"}));

    capabilities.preview_scaling = ExtensionPreviewScaling::Decimated;
    capabilities.preview_region_margin = 7u;
    EXPECT_EQ(nativePreviewHostArguments(capabilities, 12u),
              (std::vector<std::string>{"--preview-frame-rate", "12", "--preview-scaling", "decimated"}))
        << "a reach is stated only for a body over a part";

    capabilities.preview_scaling = ExtensionPreviewScaling::NativeRegion;
    EXPECT_EQ(nativePreviewHostArguments(capabilities, 60u),
              (std::vector<std::string>{"--preview-frame-rate", "60", "--preview-scaling", "native_region",
                                        "--preview-region-margin", "7"}));
}

TEST(ExtensionHostManagerTest, PrepareSessionGeneratesUniqueHandshakeInputs)
{
    an::ui::extensions::ExtensionHostManager manager;
    an::ui::extensions::ExtensionHostLaunchRequest request;
    request.extension_id = "an.demo";
    request.host_version = "test-host";

    const auto first = manager.prepareSession(request);
    const auto second = manager.prepareSession(request);

    EXPECT_FALSE(first.session_id.empty());
    EXPECT_FALSE(first.endpoint_name.empty());
    EXPECT_FALSE(first.session_secret.empty());
    EXPECT_EQ(first.ack_template.session_id, first.session_id);
    EXPECT_NE(first.session_id, second.session_id);
    EXPECT_NE(first.endpoint_name, second.endpoint_name);
    EXPECT_NE(first.session_secret, second.session_secret);
}

// A prepared session keeps the settings snapshot its host needs to inspect parameters before launch.
TEST(ExtensionHostManagerTest, PrepareSessionCarriesSettingsSnapshotForEarlyParamInspection)
{
    an::ui::extensions::ExtensionHostManager manager;
    an::ui::extensions::ExtensionHostLaunchRequest request;
    request.extension_id = "an.demo";
    request.host_version = "test-host";
    request.inspect_settings_snapshot_cbor_base64 = "synthetic-private-settings-snapshot";

    const auto session = manager.prepareSession(request);

    EXPECT_EQ(session.inspect_settings_snapshot_cbor_base64, request.inspect_settings_snapshot_cbor_base64);
}

// A launched host is started with the settings snapshot on its command line.
TEST(ExtensionHostManagerTest, LiveLaunchPassesSettingsSnapshotToExtensionHostStartup)
{
    const std::filesystem::path host_path = extensionHostLitePathForCurrentAppDir();
    ASSERT_FALSE(host_path.empty()) << "AfterNightExtensionHostLite must be built before this test runs";
    ASSERT_TRUE(std::filesystem::exists(host_path)) << host_path;

    an::ui::extensions::ExtensionHostManager manager;
    auto request = makeLaunchRequest(host_path, {"--stay-alive-ms", std::to_string(2000 * kIpcTimingMultiplier)});
    request.extension_id = "an.demo.lite.settings_snapshot";
    request.sdk_backend = "protocol";
    request.inspect_settings_snapshot_cbor_base64 = "c3ludGhldGljLXNldHRpbmdzLXNuYXBzaG90";

    const auto session = manager.prepareSession(request);
    an::ui::extensions::ExtensionHostRunningSession running_session;
    std::string error;
    ASSERT_TRUE(manager.launchSession(session, &running_session, &error)) << error;
    SessionGuard guard{manager, running_session};

    ASSERT_NE(running_session.process, nullptr);
    const QStringList arguments = running_session.process->arguments();
    const qsizetype option = arguments.indexOf(QStringLiteral("--inspect-settings-snapshot-cbor-base64"));
    ASSERT_GE(option, 0) << arguments.join(QLatin1Char(' ')).toStdString();
    ASSERT_LT(option + 1, arguments.size());
    EXPECT_EQ(arguments.at(option + 1).toStdString(), request.inspect_settings_snapshot_cbor_base64);
}

// A protocol-backend session with no host named runs the Lite host found beside the application.
TEST(ExtensionHostManagerTest, PrepareSessionDefaultsProtocolBackendToLiteHostExecutable)
{
    const an::tests::processes::framework::ScratchDirectory scratch("ext-lite-prepare");
    const std::filesystem::path app_dir = scratch.path();

#ifdef _WIN32
    const auto lite_host = app_dir / "AfterNightExtensionHostLite.exe";
#else
    const auto lite_host = app_dir / "AfterNightExtensionHostLite";
#endif

    {
        std::ofstream out(lite_host);
        out << "stub";
    }

    an::ui::extensions::ExtensionHostManager manager;
    an::ui::extensions::ExtensionHostLaunchRequest request;
    request.extension_id = "an.demo.lite";
    request.host_version = "test-host";
    request.sdk_backend = "protocol";

    const auto session = manager.prepareSession(request, app_dir);

    EXPECT_EQ(session.sdk_backend, "protocol");
    EXPECT_EQ(session.host_executable, lite_host);
}

// The full host launches, proves the session secret in its hello and is acknowledged.
TEST(ExtensionHostManagerTest, LaunchAndHandshakeWithRealExtensionHostBinary)
{
    const std::filesystem::path host_path = extensionHostPathForCurrentAppDir();
    AN_REQUIRE_FULL_EXTENSION_HOST(host_path);

    an::ui::extensions::ExtensionHostManager manager;
    const auto request = makeLaunchRequest(host_path);

    const auto session = manager.prepareSession(request);
    ASSERT_EQ(session.host_executable, host_path);

    an::ui::extensions::ExtensionHostHandshakeResult result;
    std::string error;
    ASSERT_TRUE(manager.launchAndHandshake(session, &result, &error)) << error;
    EXPECT_EQ(result.hello.extension_id, request.extension_id);
    EXPECT_EQ(result.hello.session_id, session.session_id);
    EXPECT_EQ(result.hello.sdk_backend, request.sdk_backend);
    EXPECT_EQ(result.hello.session_secret_proof, session.session_secret);
    EXPECT_EQ(result.ack.session_id, session.session_id);
    EXPECT_TRUE(result.ack.accepted);
    EXPECT_EQ(result.process_exit_code, 0);
}

// The Lite host launches, proves the session secret, offers no native window and is acknowledged.
TEST(ExtensionHostManagerTest, LaunchAndHandshakeWithRealExtensionHostLiteBinary)
{
    const std::filesystem::path host_path = extensionHostLitePathForCurrentAppDir();
    ASSERT_FALSE(host_path.empty()) << "AfterNightExtensionHostLite must be built before this test runs";
    ASSERT_TRUE(std::filesystem::exists(host_path)) << host_path;

    an::ui::extensions::ExtensionHostManager manager;
    auto request = makeLaunchRequest(host_path);
    request.extension_id = "an.demo.lite";
    request.sdk_backend = "protocol";

    const auto session = manager.prepareSession(request);
    ASSERT_EQ(session.host_executable, host_path);

    an::ui::extensions::ExtensionHostHandshakeResult result;
    std::string error;
    ASSERT_TRUE(manager.launchAndHandshake(session, &result, &error)) << error;
    EXPECT_EQ(result.hello.extension_id, request.extension_id);
    EXPECT_EQ(result.hello.session_id, session.session_id);
    EXPECT_EQ(result.hello.sdk_backend, "protocol");
    EXPECT_EQ(result.hello.session_secret_proof, session.session_secret);
    EXPECT_FALSE(result.ack.native_process_window);
    EXPECT_EQ(result.ack.session_id, session.session_id);
    EXPECT_TRUE(result.ack.accepted);
    EXPECT_EQ(result.process_exit_code, 0);
}

// The Lite host answers the manager's heartbeat pings.
TEST(ExtensionHostManagerTest, ExtensionHostLiteRepliesToHeartbeatPings)
{
    const std::filesystem::path host_path = extensionHostLitePathForCurrentAppDir();
    ASSERT_FALSE(host_path.empty()) << "AfterNightExtensionHostLite must be built before this test runs";
    ASSERT_TRUE(std::filesystem::exists(host_path)) << host_path;

    an::ui::extensions::ExtensionHostManager manager;
    auto request = makeLaunchRequest(host_path, {"--stay-alive-ms", std::to_string(2000 * kIpcTimingMultiplier)});
    request.extension_id = "an.demo.lite";
    request.sdk_backend = "protocol";
    request.heartbeat_interval_ms = 50 * kIpcTimingMultiplier;
    request.heartbeat_miss_limit = 3;

    const auto session = manager.prepareSession(request);
    an::ui::extensions::ExtensionHostRunningSession running_session;
    std::string error;
    ASSERT_TRUE(manager.launchSession(session, &running_session, &error)) << error;
    SessionGuard guard{manager, running_session};

    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 1000 * kIpcTimingMultiplier && running_session.successful_heartbeat_count == 0)
    {
        ASSERT_TRUE(manager.pumpSession(&running_session, 25 * kIpcTimingMultiplier, &error)) << error;
        QCoreApplication::processEvents(QEventLoop::AllEvents, 1);
    }

    EXPECT_GT(running_session.successful_heartbeat_count, 0);
    EXPECT_FALSE(running_session.ping_outstanding);
}

// Both hosts complete the handshake and answer heartbeats over the same protocol.
TEST(ExtensionHostManagerTest, ProtocolHeartbeatFixtureRoundTripsAgainstFullAndLiteHosts)
{
    struct HostFixture
    {
        const char* name = "";
        std::filesystem::path host_path;
        std::string extension_id;
        std::string sdk_backend;
        std::vector<std::string> capabilities;
    };

    const std::vector<HostFixture> fixtures = {
        {"full",
         extensionHostPathForCurrentAppDir(),
         "an.demo.full.protocol_fixture",
         "runtime",
         {"execute", "preview"}},
        {"lite",
         extensionHostLitePathForCurrentAppDir(),
         "an.demo.lite.protocol_fixture",
         "protocol",
         {"app", "views", "execute"}},
    };

    AN_REQUIRE_FULL_EXTENSION_HOST(fixtures.front().host_path);
    for (const auto& fixture : fixtures)
    {
        SCOPED_TRACE(fixture.name);
        ASSERT_FALSE(fixture.host_path.empty()) << "Extension host binary must be built before this test runs";
        ASSERT_TRUE(std::filesystem::exists(fixture.host_path)) << fixture.host_path;

        an::ui::extensions::ExtensionHostManager manager;
        auto request =
            makeLaunchRequest(fixture.host_path, {"--stay-alive-ms", std::to_string(2000 * kIpcTimingMultiplier)});
        request.extension_id = fixture.extension_id;
        request.sdk_backend = fixture.sdk_backend;
        request.capabilities = fixture.capabilities;
        request.heartbeat_interval_ms = 50 * kIpcTimingMultiplier;
        request.heartbeat_miss_limit = 3;

        const auto session = manager.prepareSession(request);
        an::ui::extensions::ExtensionHostRunningSession running_session;
        std::string error;
        ASSERT_TRUE(manager.launchSession(session, &running_session, &error)) << error;
        SessionGuard guard{manager, running_session};

        EXPECT_EQ(running_session.handshake.hello.extension_id, fixture.extension_id);
        EXPECT_EQ(running_session.handshake.hello.session_id, session.session_id);
        EXPECT_EQ(running_session.handshake.hello.sdk_backend, fixture.sdk_backend);
        EXPECT_EQ(running_session.handshake.ack.session_id, session.session_id);
        EXPECT_TRUE(running_session.handshake.ack.accepted);

        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < 1000 * kIpcTimingMultiplier && running_session.successful_heartbeat_count == 0)
        {
            ASSERT_TRUE(manager.pumpSession(&running_session, 25 * kIpcTimingMultiplier, &error)) << error;
            QCoreApplication::processEvents(QEventLoop::AllEvents, 1);
        }

        EXPECT_GT(running_session.successful_heartbeat_count, 0);
        EXPECT_FALSE(running_session.ping_outstanding);
        EXPECT_EQ(running_session.state, an::ui::extensions::ExtensionHostSessionState::Active);
    }
}

// The Lite host inspects a protocol extension's settings and parameters without the runtime backend, and the
// extension sees no engine domain.
TEST(ExtensionHostManagerTest, ExtensionHostLiteInspectImportsProtocolExtensionWithoutRuntimeBackend)
{
    const std::filesystem::path host_path = extensionHostLitePathForCurrentAppDir();
    const std::filesystem::path runtime_root = runtimeRootForCurrentAppDir();
    const std::filesystem::path bundled_python = repositoryBundledPythonPath();
    ASSERT_FALSE(host_path.empty()) << "AfterNightExtensionHostLite must be built before this test runs";
    ASSERT_TRUE(std::filesystem::exists(host_path)) << host_path;
    ASSERT_TRUE(std::filesystem::exists(runtime_root)) << runtime_root;
    ASSERT_FALSE(bundled_python.empty()) << "Repository bundled Python runtime must be available for lite inspect";

    const auto temp_root = uniqueExtensionRuntimeRootForTest("lite-protocol-inspect");
    removeAllNoThrow(temp_root);
    RemovePathOnExit cleanup{temp_root};
    std::filesystem::create_directories(temp_root);

    const auto env_root = temp_root / "env";
    const auto package_root = temp_root / "lite_ext";
    std::filesystem::create_directories(package_root);
    createVirtualEnvironment(bundled_python, env_root);

    {
        std::ofstream out(package_root / "lite_ext.py", std::ios::binary);
        ASSERT_TRUE(out.is_open());
        out << "import afternight\n"
            << "from afternight.lite import LiteProcessExtension\n\n"
            << "class LiteExtension(LiteProcessExtension):\n"
            << "    def __init__(self, context):\n"
            << "        super().__init__(context)\n"
            << "        info = self.session_info()\n"
            << "        assert info.sdk_backend == 'protocol'\n"
            << "        assert self.list_views() == []\n"
            << "        assert self.active_view() is None\n"
            << "        self.log_info('lite protocol import ok')\n"
            << "        try:\n"
            << "            afternight.core.available_pixel_types()\n"
            << "        except afternight.UnsupportedBackendError:\n"
            << "            pass\n"
            << "        else:\n"
            << "            raise AssertionError('protocol backend unexpectedly exposed engine domains')\n\n"
            << "    def get_settings_params(self):\n"
            << "        return [{'id': 'lite_label', 'type': 'string', 'default': 'protocol'}]\n\n"
            << "    def get_params(self):\n"
            << "        return [{'id': 'lite_strength', 'type': 'float', 'default': 0.5}]\n";
    }

    QProcess inspect;
    inspect.setProgram(QString::fromStdString(host_path.string()));
    inspect.setProcessChannelMode(QProcess::MergedChannels);
    inspect.setArguments(
        {QStringLiteral("--inspect"), QStringLiteral("--inspect-settings"), QStringLiteral("--inspect-params"),
         QStringLiteral("--sdk-backend"), QStringLiteral("protocol"), QStringLiteral("--extension-package-root"),
         QString::fromStdString(package_root.string()), QStringLiteral("--entry-point"), QStringLiteral("lite_ext"),
         QStringLiteral("--process-class"), QStringLiteral("LiteExtension"), QStringLiteral("--environment-root"),
         QString::fromStdString(env_root.string()), QStringLiteral("--runtime-root"),
         QString::fromStdString(runtime_root.string())});
    inspect.start();
    ASSERT_TRUE(inspect.waitForStarted());
    ASSERT_TRUE(inspect.waitForFinished(hostProcessWaitMs()));

    const std::string output = inspect.readAll().toStdString();
    EXPECT_EQ(inspect.exitStatus(), QProcess::NormalExit) << output;
    EXPECT_EQ(inspect.exitCode(), 0) << output;

    QJsonParseError parse_error;
    const QJsonDocument document = QJsonDocument::fromJson(QByteArray::fromStdString(output).trimmed(), &parse_error);
    ASSERT_EQ(parse_error.error, QJsonParseError::NoError) << output;
    ASSERT_TRUE(document.isObject()) << output;
    const QJsonArray settings_schema = document.object().value(QStringLiteral("settings_schema")).toArray();
    ASSERT_EQ(settings_schema.size(), 1) << output;
    EXPECT_EQ(settings_schema.at(0).toObject().value(QStringLiteral("id")).toString(), QStringLiteral("lite_label"));
    const QJsonArray params_schema = document.object().value(QStringLiteral("params_schema")).toArray();
    ASSERT_EQ(params_schema.size(), 1) << output;
    EXPECT_EQ(params_schema.at(0).toObject().value(QStringLiteral("id")).toString(), QStringLiteral("lite_strength"));
}

// A standalone Lite session runs a protocol extension's launch hook with no application behind it and no engine.
TEST(ExtensionHostManagerTest, ExtensionHostLiteStandaloneImportsProtocolExtensionWithoutEngineLoopback)
{
    const std::filesystem::path host_path = extensionHostLitePathForCurrentAppDir();
    const std::filesystem::path runtime_root = runtimeRootForCurrentAppDir();
    const std::filesystem::path bundled_python = repositoryBundledPythonPath();
    ASSERT_FALSE(host_path.empty()) << "AfterNightExtensionHostLite must be built before this test runs";
    ASSERT_TRUE(std::filesystem::exists(host_path)) << host_path;
    ASSERT_TRUE(std::filesystem::exists(runtime_root)) << runtime_root;
    ASSERT_FALSE(bundled_python.empty()) << "Repository bundled Python runtime must be available for lite standalone";

    const auto temp_root = uniqueExtensionRuntimeRootForTest("lite-protocol-standalone");
    removeAllNoThrow(temp_root);
    RemovePathOnExit cleanup{temp_root};
    std::filesystem::create_directories(temp_root);

    const auto env_root = temp_root / "env";
    const auto package_root = temp_root / "lite_standalone_ext";
    const auto marker_path = temp_root / "standalone_marker.json";
    std::filesystem::create_directories(package_root);
    createVirtualEnvironment(bundled_python, env_root);

    {
        std::ofstream out(package_root / "lite_standalone_ext.py", std::ios::binary);
        ASSERT_TRUE(out.is_open());
        out << "import json\n"
            << "import afternight\n"
            << "from afternight.lite import LiteProcessExtension\n\n"
            << "MARKER = " << quotedPythonString(marker_path.generic_string()) << "\n\n"
            << "class LiteStandaloneExtension(LiteProcessExtension):\n"
            << "    def __init__(self, context):\n"
            << "        super().__init__(context)\n"
            << "        info = self.session_info()\n"
            << "        assert info.sdk_backend == 'protocol'\n"
            << "        assert info.is_standalone is True\n"
            << "        assert info.session_id == 'lite-standalone-session'\n"
            << "        assert self.list_views() == []\n"
            << "        assert self.active_view() is None\n"
            << "        try:\n"
            << "            afternight.core.available_pixel_types()\n"
            << "        except afternight.UnsupportedBackendError:\n"
            << "            pass\n"
            << "        else:\n"
            << "            raise AssertionError('lite standalone unexpectedly exposed engine domains')\n\n"
            << "    def on_launch(self):\n"
            << "        info = self.session_info()\n"
            << "        with open(MARKER, 'w', encoding='utf-8') as marker:\n"
            << "            json.dump({'session_id': info.session_id, 'standalone': info.is_standalone}, marker)\n";
    }

    QProcess process;
    process.setProgram(QString::fromStdString(host_path.string()));
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.setArguments({QStringLiteral("--standalone"), QStringLiteral("--sdk-backend"), QStringLiteral("protocol"),
                          QStringLiteral("--session-id"), QStringLiteral("lite-standalone-session"),
                          QStringLiteral("--extension-id"), QStringLiteral("an.demo.lite_standalone"),
                          QStringLiteral("--extension-package-root"), QString::fromStdString(package_root.string()),
                          QStringLiteral("--entry-point"), QStringLiteral("lite_standalone_ext"),
                          QStringLiteral("--process-class"), QStringLiteral("LiteStandaloneExtension"),
                          QStringLiteral("--environment-root"), QString::fromStdString(env_root.string()),
                          QStringLiteral("--runtime-root"), QString::fromStdString(runtime_root.string())});
    process.start();
    ASSERT_TRUE(process.waitForStarted());
    // A standalone host ends the case by exiting.
    ASSERT_TRUE(process.waitForFinished(10000 * kIpcTimingMultiplier + hostExitAllowanceMs()));

    const std::string output = process.readAll().toStdString();
    EXPECT_EQ(process.exitStatus(), QProcess::NormalExit) << output;
    EXPECT_EQ(process.exitCode(), 0) << output;
    EXPECT_TRUE(std::filesystem::exists(marker_path)) << output;
}

// A standalone Lite session runs the extension's own launch and run hooks in order.
TEST(ExtensionHostManagerTest, ExtensionHostLiteStandaloneRunsExtensionOwnedStartup)
{
    const std::filesystem::path host_path = extensionHostLitePathForCurrentAppDir();
    const std::filesystem::path runtime_root = runtimeRootForCurrentAppDir();
    const std::filesystem::path bundled_python = repositoryBundledPythonPath();
    ASSERT_FALSE(host_path.empty()) << "AfterNightExtensionHostLite must be built before this test runs";
    ASSERT_TRUE(std::filesystem::exists(host_path)) << host_path;
    ASSERT_TRUE(std::filesystem::exists(runtime_root)) << runtime_root;
    ASSERT_FALSE(bundled_python.empty()) << "Repository bundled Python runtime must be available for lite standalone";

    const auto temp_root = uniqueExtensionRuntimeRootForTest("lite-protocol-standalone-run");
    removeAllNoThrow(temp_root);
    RemovePathOnExit cleanup{temp_root};
    std::filesystem::create_directories(temp_root);

    const auto env_root = temp_root / "env";
    const auto package_root = temp_root / "lite_startup_ext";
    const auto marker_path = temp_root / "startup_marker.json";
    std::filesystem::create_directories(package_root);
    createVirtualEnvironment(bundled_python, env_root);

    {
        std::ofstream out(package_root / "lite_startup_ext.py", std::ios::binary);
        ASSERT_TRUE(out.is_open());
        out << "import json\n"
            << "from afternight.lite import LiteProcessExtension\n\n"
            << "MARKER = " << quotedPythonString(marker_path.generic_string()) << "\n\n"
            << "class LiteStartupExtension(LiteProcessExtension):\n"
            << "    def __init__(self, context):\n"
            << "        super().__init__(context)\n"
            << "        self.launch_seen = False\n\n"
            << "    def on_launch(self):\n"
            << "        self.launch_seen = True\n\n"
            << "    def run(self, params=None, progress=None):\n"
            << "        assert self.launch_seen is True\n"
            << "        assert params == {}, params\n"
            << "        assert progress is self.progress\n"
            << "        info = self.session_info()\n"
            << "        with open(MARKER, 'w', encoding='utf-8') as marker:\n"
            << "            json.dump({'session_id': info.session_id, 'standalone': info.is_standalone}, marker)\n";
    }

    QProcess process;
    process.setProgram(QString::fromStdString(host_path.string()));
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.setArguments({QStringLiteral("--standalone"), QStringLiteral("--sdk-backend"), QStringLiteral("protocol"),
                          QStringLiteral("--session-id"), QStringLiteral("lite-startup-session"),
                          QStringLiteral("--extension-id"), QStringLiteral("an.demo.lite_startup"),
                          QStringLiteral("--extension-package-root"), QString::fromStdString(package_root.string()),
                          QStringLiteral("--entry-point"), QStringLiteral("lite_startup_ext"),
                          QStringLiteral("--process-class"), QStringLiteral("LiteStartupExtension"),
                          QStringLiteral("--environment-root"), QString::fromStdString(env_root.string()),
                          QStringLiteral("--runtime-root"), QString::fromStdString(runtime_root.string())});
    process.start();
    ASSERT_TRUE(process.waitForStarted());
    // A standalone host ends the case by exiting.
    ASSERT_TRUE(process.waitForFinished(10000 * kIpcTimingMultiplier + hostExitAllowanceMs()));

    const std::string output = process.readAll().toStdString();
    EXPECT_EQ(process.exitStatus(), QProcess::NormalExit) << output;
    EXPECT_EQ(process.exitCode(), 0) << output;
    EXPECT_TRUE(std::filesystem::exists(marker_path)) << output;
}

// A managed Lite session's launch hook reaches the application's views, settings and logger through the SDK.
TEST(ExtensionHostManagerTest, ExtensionHostLiteManagedSessionDispatchesProtocolSdkCallsAfterLaunch)
{
    const std::filesystem::path host_path = extensionHostLitePathForCurrentAppDir();
    const std::filesystem::path runtime_root = runtimeRootForCurrentAppDir();
    const std::filesystem::path bundled_python = repositoryBundledPythonPath();
    ASSERT_FALSE(host_path.empty()) << "AfterNightExtensionHostLite must be built before this test runs";
    ASSERT_TRUE(std::filesystem::exists(host_path)) << host_path;
    ASSERT_TRUE(std::filesystem::exists(runtime_root)) << runtime_root;
    ASSERT_FALSE(bundled_python.empty()) << "Repository bundled Python runtime must be available for lite managed";

    const auto temp_root = uniqueExtensionRuntimeRootForTest("lite-protocol-managed");
    removeAllNoThrow(temp_root);
    RemovePathOnExit cleanup{temp_root};
    std::filesystem::create_directories(temp_root);

    const auto env_root = temp_root / "env";
    const auto package_root = temp_root / "lite_managed_ext";
    std::filesystem::create_directories(package_root);
    createVirtualEnvironment(bundled_python, env_root);

    {
        std::ofstream out(package_root / "lite_managed_ext.py", std::ios::binary);
        ASSERT_TRUE(out.is_open());
        out << "import afternight\n"
            << "from afternight.lite import LiteProcessExtension\n\n"
            << "class LiteManagedExtension(LiteProcessExtension):\n"
            << "    def __init__(self, context):\n"
            << "        super().__init__(context)\n\n"
            << "    def on_launch(self):\n"
            << "        info = self.session_info()\n"
            << "        assert info.sdk_backend == 'protocol'\n"
            << "        assert info.is_standalone is False\n"
            << "        views = self.list_views()\n"
            << "        assert len(views) == 1, views\n"
            << "        view = dict(views[0])\n"
            << "        assert view['name'] == 'lite-live-view', view\n"
            << "        assert view['width'] == 32, view\n"
            << "        self.settings.set('launch_view_name', view['name'])\n"
            << "        self.settings.set('launch_view_size', {'width': view['width'], 'height': view['height']})\n"
            << "        self.log_info('lite managed launch view=' + view['name'], component='lite.managed.test')\n";
    }

    InMemorySettings settings_store;
    GlobalSettingsPersistence& settings = settings_store.repository();
    an::ui::extensions::ExtensionSdkService sdk_service(&settings);
    an::ui::extensions::ExtensionViewState active_view;
    active_view.view_name = "lite-live-view";
    active_view.file_name = "/tmp/lite-live-view.fit";
    active_view.width = 32;
    active_view.height = 18;
    active_view.channels = 3;
    active_view.zoom_percent = 125.0;
    active_view.is_active = true;
    active_view.has_image = false;
    sdk_service.setViewStateProvider(
        [active_view]() { return std::vector<an::ui::extensions::ExtensionViewState>{active_view}; });

    an::ui::extensions::ExtensionHostManager manager;
    manager.setSdkService(&sdk_service);

    an::ui::extensions::ExtensionHostLaunchRequest request;
    request.extension_id = "an.demo.lite.managed";
    request.sdk_backend = "protocol";
    request.host_executable = host_path;
    request.capabilities = {"app", "views"};
    request.host_arguments = {
        "--extension-package-root", package_root.string(), "--entry-point",
        "lite_managed_ext",         "--process-class",     "LiteManagedExtension",
        "--environment-root",       env_root.string(),     "--runtime-root",
        runtime_root.string(),      "--stay-alive-ms",     std::to_string(5000 * kIpcTimingMultiplier)};

    std::string error;
    auto prepared = manager.prepareSession(request);
    an::ui::extensions::ExtensionHostRunningSession running_session;
    ASSERT_TRUE(manager.launchSession(prepared, &running_session, &error)) << error;
    SessionGuard session_guard{manager, running_session};

    QElapsedTimer timer;
    timer.start();
    bool observed_launch = false;
    while (timer.elapsed() < 5000 * kIpcTimingMultiplier)
    {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 1);
        ASSERT_TRUE(manager.pumpSession(&running_session, 20, &error)) << error;

        QString launch_view_name;
        QCborValue launch_view_size;
        const bool has_name = sdk_service.selectExtensionSetting(request.extension_id, "launch_view_name",
                                                                 &launch_view_name, {}, nullptr);
        const bool has_size = sdk_service.selectExtensionSetting(request.extension_id, "launch_view_size",
                                                                 &launch_view_size, QCborValue(), nullptr) &&
                              launch_view_size.isMap();
        const auto& events = sdk_service.recentLogEvents();
        const bool has_log = std::any_of(events.begin(), events.end(), [](const auto& event) {
            return event.component == "lite.managed.test" &&
                   event.message.find("lite managed launch view=lite-live-view") != std::string::npos;
        });
        if (has_name && launch_view_name == QStringLiteral("lite-live-view") && has_size && has_log)
        {
            observed_launch = true;
            break;
        }
    }

    ASSERT_TRUE(observed_launch) << "Timed out waiting for lite on_launch SDK protocol calls";
    EXPECT_GE(running_session.serviced_sdk_message_count, 4);

    QCborValue launch_view_size;
    ASSERT_TRUE(sdk_service.selectExtensionSetting(request.extension_id, "launch_view_size", &launch_view_size,
                                                   QCborValue(), &error))
        << error;
    ASSERT_TRUE(launch_view_size.isMap());
    const QCborMap size = launch_view_size.toMap();
    EXPECT_EQ(size.value(QStringLiteral("width")).toInteger(), 32);
    EXPECT_EQ(size.value(QStringLiteral("height")).toInteger(), 18);
}

// A Lite extension reads a view snapshot's shared buffer as an array of the buffer's shape and type.
TEST(ExtensionHostManagerTest, ExtensionHostLiteManagedSessionReadsSnapshotSharedBuffer)
{
    const std::filesystem::path host_path = extensionHostLitePathForCurrentAppDir();
    const std::filesystem::path runtime_root = runtimeRootForCurrentAppDir();
    const std::filesystem::path bundled_python = repositoryBundledPythonPath();
    ASSERT_FALSE(host_path.empty()) << "AfterNightExtensionHostLite must be built before this test runs";
    ASSERT_TRUE(std::filesystem::exists(host_path)) << host_path;
    ASSERT_TRUE(std::filesystem::exists(runtime_root)) << runtime_root;
    ASSERT_FALSE(bundled_python.empty()) << "Repository bundled Python runtime must be available for lite snapshots";

    const auto temp_root = uniqueExtensionRuntimeRootForTest("lite-protocol-snapshot");
    removeAllNoThrow(temp_root);
    RemovePathOnExit cleanup{temp_root};
    std::filesystem::create_directories(temp_root);

    const auto env_root = temp_root / "env";
    const auto package_root = temp_root / "lite_snapshot_ext";
    const auto shared_buffer_root = temp_root / "shared_buffers";
    std::filesystem::create_directories(package_root);
    createVirtualEnvironment(bundled_python, env_root);
    if (!provisionBundledRuntimePackageIntoEnvironment(runtime_root, env_root, "numpy"))
    {
        GTEST_SKIP() << "numpy not available in bundled runtime";
    }

    {
        std::ofstream out(package_root / "lite_snapshot_ext.py", std::ios::binary);
        ASSERT_TRUE(out.is_open());
        out << "import numpy as np\n"
            << "from afternight.lite import LiteProcessExtension\n\n"
            << "class LiteSnapshotExtension(LiteProcessExtension):\n"
            << "    def __init__(self, context):\n"
            << "        super().__init__(context)\n\n"
            << "    def on_launch(self):\n"
            << "        snapshot = self.snapshot_view('lite-snapshot-view')\n"
            << "        image = snapshot.source_image\n"
            << "        assert image is not None\n"
            << "        array = image.to_numpy()\n"
            << "        props = image.properties\n"
            << "        self.settings.set('snapshot_shape', [int(value) for value in array.shape])\n"
            << "        self.settings.set('snapshot_dtype', str(array.dtype))\n"
            << "        self.settings.set('snapshot_sum', float(np.sum(array)))\n"
            << "        self.settings.set('snapshot_props', {\n"
            << "            'width': int(props.get('width', 0)),\n"
            << "            'height': int(props.get('height', 0)),\n"
            << "            'channels': int(props.get('channels', 0)),\n"
            << "            'pixel_type': str(props.get('pixel_type', '')),\n"
            << "        })\n";
    }

    InMemorySettings settings_store;
    GlobalSettingsPersistence& settings = settings_store.repository();
    an::ui::extensions::ExtensionSdkService sdk_service(&settings);
    an::ui::extensions::ExtensionViewState active_view;
    active_view.view_name = "lite-snapshot-view";
    active_view.file_name = "/tmp/lite-snapshot-view.fit";
    active_view.width = 3;
    active_view.height = 2;
    active_view.channels = 1;
    active_view.is_active = true;
    active_view.has_image = true;
    sdk_service.setViewStateProvider(
        [active_view]() { return std::vector<an::ui::extensions::ExtensionViewState>{active_view}; });
    sdk_service.setViewSnapshotProvider(
        [shared_buffer_root](const std::string_view session_id, const std::string_view request_id,
                             const std::string_view view_name, an::ui::extensions::ExtensionViewSnapshot* out_snapshot,
                             std::string* error) -> bool {
            if (!out_snapshot)
            {
                if (error)
                {
                    *error = "snapshot output is null";
                }
                return false;
            }

            an::ui::extensions::SharedImageBufferSpec spec;
            spec.sessionId = std::string(session_id);
            spec.bufferId = std::string(request_id) + ".lite_snapshot";
            spec.symbolicName = std::string(view_name) + ".snapshot";
            spec.width = 3;
            spec.height = 2;
            spec.channels = 1;
            spec.pixelType = an::ui::extensions::SharedBufferPixelType::Float32;
            spec.pixelFormat = an::ui::extensions::SharedBufferPixelFormat::Mono;
            spec.colorSpace = an::ui::extensions::SharedBufferColorSpace::Grayscale;
            spec.accessMode = an::ui::extensions::SharedBufferAccessMode::ReadOnly;
            spec.bufferRole = an::ui::extensions::SharedBufferRole::Source;

            an::ui::extensions::SharedBufferManager manager(shared_buffer_root);
            an::ui::extensions::SharedImageBuffer buffer;
            if (!allocateBufferOrReport(manager, spec, &buffer, error))
            {
                return false;
            }

            auto* pixels = reinterpret_cast<float*>(buffer.bytes());
            for (int index = 0; index < 6; ++index)
            {
                pixels[index] = static_cast<float>(index + 1);
            }

            *out_snapshot = an::ui::extensions::ExtensionViewSnapshot{};
            out_snapshot->source_buffer = buffer.descriptor();
            return true;
        });

    an::ui::extensions::ExtensionHostManager manager;
    manager.setSdkService(&sdk_service);

    an::ui::extensions::ExtensionHostLaunchRequest request;
    request.extension_id = "an.demo.lite.snapshot";
    request.sdk_backend = "protocol";
    request.host_executable = host_path;
    request.capabilities = {"app", "views"};
    request.host_arguments = {
        "--extension-package-root", package_root.string(), "--entry-point",
        "lite_snapshot_ext",        "--process-class",     "LiteSnapshotExtension",
        "--environment-root",       env_root.string(),     "--runtime-root",
        runtime_root.string(),      "--stay-alive-ms",     std::to_string(5000 * kIpcTimingMultiplier)};

    std::string error;
    auto prepared = manager.prepareSession(request);
    an::ui::extensions::ExtensionHostRunningSession running_session;
    ASSERT_TRUE(manager.launchSession(prepared, &running_session, &error)) << error;
    SessionGuard session_guard{manager, running_session};

    QElapsedTimer timer;
    timer.start();
    bool observed_snapshot = false;
    while (timer.elapsed() < 5000 * kIpcTimingMultiplier)
    {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 1);
        ASSERT_TRUE(manager.pumpSession(&running_session, 20, &error)) << error;

        QCborValue snapshot_shape;
        const bool has_shape = sdk_service.selectExtensionSetting(request.extension_id, "snapshot_shape",
                                                                  &snapshot_shape, QCborValue(), nullptr) &&
                               snapshot_shape.isArray();
        QCborValue snapshot_sum;
        const bool has_sum = sdk_service.selectExtensionSetting(request.extension_id, "snapshot_sum", &snapshot_sum,
                                                                QCborValue(), nullptr) &&
                             snapshot_sum.isDouble();
        QCborValue props_value;
        const bool has_props = sdk_service.selectExtensionSetting(request.extension_id, "snapshot_props", &props_value,
                                                                  QCborValue(), nullptr) &&
                               props_value.isMap();
        if (has_shape && has_sum && snapshot_sum.toDouble() == 21.0 && has_props)
        {
            observed_snapshot = true;
            break;
        }
    }

    ASSERT_TRUE(observed_snapshot) << "Timed out waiting for lite snapshot shared-buffer access";

    QCborValue snapshot_shape;
    ASSERT_TRUE(sdk_service.selectExtensionSetting(request.extension_id, "snapshot_shape", &snapshot_shape,
                                                   QCborValue(), &error))
        << error;
    ASSERT_TRUE(snapshot_shape.isArray());
    const QCborArray shape = snapshot_shape.toArray();
    ASSERT_EQ(shape.size(), 2);
    EXPECT_EQ(shape.at(0).toInteger(), 2);
    EXPECT_EQ(shape.at(1).toInteger(), 3);

    QString dtype;
    ASSERT_TRUE(sdk_service.selectExtensionSetting(request.extension_id, "snapshot_dtype", &dtype, {}, &error))
        << error;
    EXPECT_EQ(dtype, QStringLiteral("float32"));

    QCborValue props_value;
    ASSERT_TRUE(
        sdk_service.selectExtensionSetting(request.extension_id, "snapshot_props", &props_value, QCborValue(), &error))
        << error;
    ASSERT_TRUE(props_value.isMap());
    const QCborMap props = props_value.toMap();
    EXPECT_EQ(props.value(QStringLiteral("width")).toInteger(), 3);
    EXPECT_EQ(props.value(QStringLiteral("height")).toInteger(), 2);
    EXPECT_EQ(props.value(QStringLiteral("channels")).toInteger(), 1);
    EXPECT_EQ(props.value(QStringLiteral("pixel_type")).toString(), QStringLiteral("float32"));
}

// A Lite extension's writable clone of the source, filled from an array, is committed back as its samples.
TEST(ExtensionHostManagerTest, ExtensionHostLiteManagedSessionWritesClonedImageHandleFromNumpy)
{
    const std::filesystem::path host_path = extensionHostLitePathForCurrentAppDir();
    const std::filesystem::path runtime_root = runtimeRootForCurrentAppDir();
    const std::filesystem::path bundled_python = repositoryBundledPythonPath();
    ASSERT_FALSE(host_path.empty()) << "AfterNightExtensionHostLite must be built before this test runs";
    ASSERT_TRUE(std::filesystem::exists(host_path)) << host_path;
    ASSERT_TRUE(std::filesystem::exists(runtime_root)) << runtime_root;
    ASSERT_FALSE(bundled_python.empty()) << "Repository bundled Python runtime must be available for lite clone";

    const auto temp_root = uniqueExtensionRuntimeRootForTest("lite-protocol-clone-from-numpy");
    removeAllNoThrow(temp_root);
    RemovePathOnExit cleanup{temp_root};
    std::filesystem::create_directories(temp_root);

    const auto env_root = temp_root / "env";
    const auto package_root = temp_root / "lite_clone_image_ext";
    std::filesystem::create_directories(package_root);
    createVirtualEnvironment(bundled_python, env_root);
    if (!provisionBundledRuntimePackageIntoEnvironment(runtime_root, env_root, "numpy"))
    {
        GTEST_SKIP() << "numpy not available in bundled runtime";
    }

    {
        std::ofstream out(package_root / "lite_clone_image_ext.py", std::ios::binary);
        ASSERT_TRUE(out.is_open());
        out << "import afternight.ui_protocol as ui\n"
            << "from afternight.lite import LiteProcessExtension\n\n"
            << "class LiteCloneImageExtension(LiteProcessExtension):\n"
            << "    def __init__(self, context):\n"
            << "        super().__init__(context)\n\n"
            << "    def on_launch(self):\n"
            << "        view = self.active_view()\n"
            << "        source = view.source_image\n"
            << "        clone = source.clone()\n"
            << "        clone.from_numpy(source.to_numpy() + 10.0)\n"
            << "        clone.set_metadata('STEP', 'lite.clone.from_numpy')\n"
            << "        assert ui.commit_image(clone, history_step_name='Lite Clone Commit', "
               "history_step_description='Committed writable clone', allow_resized_output=True)\n"
            << "        self.settings.set('clone_commit_done', True)\n";
    }

    InMemorySettings settings_store;
    GlobalSettingsPersistence& settings = settings_store.repository();
    an::ui::extensions::ExtensionSdkService sdk_service(&settings);
    std::string error;
    an::ui::extensions::ExtensionViewState active_view;
    active_view.view_name = "lite-clone-view";
    active_view.file_name = "/tmp/lite-clone-view.fit";
    active_view.width = 3;
    active_view.height = 2;
    active_view.channels = 1;
    active_view.is_active = true;
    active_view.has_image = true;
    sdk_service.setViewStateProvider(
        [active_view]() { return std::vector<an::ui::extensions::ExtensionViewState>{active_view}; });

    const auto shared_buffer_root = uniqueSharedBufferRootForTest("lite_clone_snapshot_source");
    removeAllNoThrow(shared_buffer_root);
    RemovePathOnExit shared_cleanup{shared_buffer_root};
    sdk_service.setViewSnapshotProvider(
        [shared_buffer_root](const std::string_view session_id, const std::string_view request_id,
                             const std::string_view view_name, an::ui::extensions::ExtensionViewSnapshot* out_snapshot,
                             std::string* error) -> bool {
            if (!out_snapshot)
            {
                if (error)
                {
                    *error = "snapshot output is null";
                }
                return false;
            }

            an::ui::extensions::SharedImageBufferSpec spec;
            spec.sessionId = std::string(session_id);
            spec.bufferId = std::string(request_id) + ".lite_clone_snapshot";
            spec.symbolicName = std::string(view_name) + ".snapshot";
            spec.width = 3;
            spec.height = 2;
            spec.channels = 1;
            spec.pixelType = an::ui::extensions::SharedBufferPixelType::Float32;
            spec.pixelFormat = an::ui::extensions::SharedBufferPixelFormat::Mono;
            spec.colorSpace = an::ui::extensions::SharedBufferColorSpace::Grayscale;
            spec.accessMode = an::ui::extensions::SharedBufferAccessMode::ReadOnly;
            spec.bufferRole = an::ui::extensions::SharedBufferRole::Source;

            an::ui::extensions::SharedBufferManager manager(shared_buffer_root);
            an::ui::extensions::SharedImageBuffer buffer;
            if (!allocateBufferOrReport(manager, spec, &buffer, error))
            {
                return false;
            }

            auto* pixels = reinterpret_cast<float*>(buffer.bytes());
            for (int index = 0; index < 6; ++index)
            {
                pixels[index] = static_cast<float>(index + 1);
            }

            *out_snapshot = an::ui::extensions::ExtensionViewSnapshot{};
            out_snapshot->source_buffer = buffer.descriptor();
            return true;
        });

    an::ui::extensions::ExtensionHostManager manager;
    manager.setSdkService(&sdk_service);

    an::ui::extensions::ExtensionHostLaunchRequest request;
    request.extension_id = "an.demo.lite.clone_from_numpy";
    request.sdk_backend = "protocol";
    request.host_executable = host_path;
    request.capabilities = {"app", "views", "execute"};
    request.host_arguments = {
        "--extension-package-root", package_root.string(), "--entry-point",
        "lite_clone_image_ext",     "--process-class",     "LiteCloneImageExtension",
        "--environment-root",       env_root.string(),     "--runtime-root",
        runtime_root.string(),      "--stay-alive-ms",     std::to_string(5000 * kIpcTimingMultiplier)};

    auto prepared = manager.prepareSession(request);
    an::ui::extensions::ExtensionHostRunningSession running_session;
    ASSERT_TRUE(manager.launchSession(prepared, &running_session, &error)) << error;
    SessionGuard session_guard{manager, running_session};

    QElapsedTimer timer;
    timer.start();
    bool observed_commit = false;
    while (timer.elapsed() < 5000 * kIpcTimingMultiplier)
    {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 1);
        ASSERT_TRUE(manager.pumpSession(&running_session, 20, &error)) << error;

        QCborValue done;
        const bool has_done = sdk_service.selectExtensionSetting(request.extension_id, "clone_commit_done", &done,
                                                                 QCborValue(false), nullptr) &&
                              done.toBool();
        if (has_done && running_session.last_view_commit.has_value())
        {
            observed_commit = true;
            break;
        }
    }

    ASSERT_TRUE(observed_commit) << "Timed out waiting for lite writable clone commit";
    ASSERT_TRUE(running_session.last_view_commit.has_value());
    const auto& view_commit = *running_session.last_view_commit;
    EXPECT_EQ(view_commit.history_step_name, "Lite Clone Commit");
    ASSERT_EQ(view_commit.result_buffers.size(), 1u);

    const auto& buffer = view_commit.result_buffers.front();
    const std::filesystem::path file_path = std::filesystem::path(buffer.mapped_file_path).lexically_normal();
    const std::filesystem::path result_root = file_path.parent_path().parent_path();
    an::ui::extensions::SharedBufferManager result_manager(result_root);
    std::optional<an::consumer::ImageAsset> committed_image;
    ASSERT_TRUE(importBufferOrReport(result_manager, buffer, &committed_image, &error)) << error;
    ASSERT_TRUE(committed_image);
    EXPECT_EQ(widthOf(*committed_image), 3u);
    EXPECT_EQ(heightOf(*committed_image), 2u);
    EXPECT_NEAR(sampleAt(*committed_image, 0, 0, 0), 11.0, 1e-6);
    EXPECT_NEAR(sampleAt(*committed_image, 2, 1, 0), 16.0, 1e-6);
}

// An image a Lite extension generates is handed to the application to open, with its title and samples.
TEST(ExtensionHostManagerTest, ExtensionHostLiteManagedSessionPresentsGeneratedImageThroughUiOpenImage)
{
    const std::filesystem::path host_path = extensionHostLitePathForCurrentAppDir();
    const std::filesystem::path runtime_root = runtimeRootForCurrentAppDir();
    const std::filesystem::path bundled_python = repositoryBundledPythonPath();
    ASSERT_FALSE(host_path.empty()) << "AfterNightExtensionHostLite must be built before this test runs";
    ASSERT_TRUE(std::filesystem::exists(host_path)) << host_path;
    ASSERT_TRUE(std::filesystem::exists(runtime_root)) << runtime_root;
    ASSERT_FALSE(bundled_python.empty()) << "Repository bundled Python runtime must be available for lite ui";

    const auto temp_root = uniqueExtensionRuntimeRootForTest("lite-protocol-ui-open-image");
    removeAllNoThrow(temp_root);
    RemovePathOnExit cleanup{temp_root};
    std::filesystem::create_directories(temp_root);

    const auto env_root = temp_root / "env";
    const auto package_root = temp_root / "lite_ui_open_image_ext";
    std::filesystem::create_directories(package_root);
    createVirtualEnvironment(bundled_python, env_root);
    if (!provisionBundledRuntimePackageIntoEnvironment(runtime_root, env_root, "numpy"))
    {
        GTEST_SKIP() << "numpy not available in bundled runtime";
    }

    {
        std::ofstream out(package_root / "lite_ui_open_image_ext.py", std::ios::binary);
        ASSERT_TRUE(out.is_open());
        out << "import afternight.ui_protocol as ui\n"
            << "import numpy as np\n"
            << "from afternight.lite import LiteProcessExtension\n\n"
            << "class LiteUiOpenImageExtension(LiteProcessExtension):\n"
            << "    def __init__(self, context):\n"
            << "        super().__init__(context)\n\n"
            << "    def on_launch(self):\n"
            << "        generated = np.array([[1, 2, 3], [4, 5, 6]], dtype=np.float32)\n"
            << "        assert ui.open_image(generated, title='Lite Generated Image', metadata={'STEP': "
               "'lite.ui.open_image'})\n"
            << "        self.settings.set('open_image_done', True)\n";
    }

    InMemorySettings settings_store;
    GlobalSettingsPersistence& settings = settings_store.repository();
    an::ui::extensions::ExtensionSdkService sdk_service(&settings);
    QString opened_title;
    std::optional<an::consumer::ImageAsset> opened_image;
    sdk_service.setUiOpenImageHandler([&](const std::string_view, const std::string_view,
                                          const an::ui::extensions::SharedBufferDescriptor& buffer,
                                          const QString& title, std::string* handler_error) -> bool {
        const std::filesystem::path file_path = std::filesystem::path(buffer.mapped_file_path).lexically_normal();
        const std::filesystem::path root_path = file_path.parent_path().parent_path();
        an::ui::extensions::SharedBufferManager shared_buffer_manager(root_path);
        if (!importBufferOrReport(shared_buffer_manager, buffer, &opened_image, handler_error))
        {
            return false;
        }
        opened_title = title;
        return true;
    });

    an::ui::extensions::ExtensionHostManager manager;
    manager.setSdkService(&sdk_service);

    an::ui::extensions::ExtensionHostLaunchRequest request;
    request.extension_id = "an.demo.lite.ui_open_image";
    request.sdk_backend = "protocol";
    request.host_executable = host_path;
    request.capabilities = {"app", "views"};
    request.host_arguments = {
        "--extension-package-root", package_root.string(), "--entry-point",
        "lite_ui_open_image_ext",   "--process-class",     "LiteUiOpenImageExtension",
        "--environment-root",       env_root.string(),     "--runtime-root",
        runtime_root.string(),      "--stay-alive-ms",     std::to_string(5000 * kIpcTimingMultiplier)};

    std::string error;
    auto prepared = manager.prepareSession(request);
    an::ui::extensions::ExtensionHostRunningSession running_session;
    ASSERT_TRUE(manager.launchSession(prepared, &running_session, &error)) << error;
    SessionGuard session_guard{manager, running_session};

    QElapsedTimer timer;
    timer.start();
    bool observed_open_image = false;
    while (timer.elapsed() < 5000 * kIpcTimingMultiplier)
    {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 1);
        ASSERT_TRUE(manager.pumpSession(&running_session, 20, &error)) << error;

        QCborValue done;
        const bool has_done = sdk_service.selectExtensionSetting(request.extension_id, "open_image_done", &done,
                                                                 QCborValue(false), nullptr) &&
                              done.toBool();
        if (has_done && opened_image)
        {
            observed_open_image = true;
            break;
        }
    }

    ASSERT_TRUE(observed_open_image) << "Timed out waiting for lite ui.open_image result";
    ASSERT_TRUE(opened_image);
    EXPECT_EQ(opened_title, QStringLiteral("Lite Generated Image"));
    EXPECT_EQ(widthOf(*opened_image), 3u);
    EXPECT_EQ(heightOf(*opened_image), 2u);
    EXPECT_EQ(channelsOf(*opened_image), 1u);
    EXPECT_NEAR(sampleAt(*opened_image, 0, 0, 0), 1.0, 1e-6);
    EXPECT_NEAR(sampleAt(*opened_image, 2, 1, 0), 6.0, 1e-6);
}

// An image a Lite extension generates is committed to the view with its history step and samples.
TEST(ExtensionHostManagerTest, ExtensionHostLiteManagedSessionCommitsGeneratedImageThroughViewCommit)
{
    const std::filesystem::path host_path = extensionHostLitePathForCurrentAppDir();
    const std::filesystem::path runtime_root = runtimeRootForCurrentAppDir();
    const std::filesystem::path bundled_python = repositoryBundledPythonPath();
    ASSERT_FALSE(host_path.empty()) << "AfterNightExtensionHostLite must be built before this test runs";
    ASSERT_TRUE(std::filesystem::exists(host_path)) << host_path;
    ASSERT_TRUE(std::filesystem::exists(runtime_root)) << runtime_root;
    ASSERT_FALSE(bundled_python.empty()) << "Repository bundled Python runtime must be available for lite commit";

    const auto temp_root = uniqueExtensionRuntimeRootForTest("lite-protocol-view-commit");
    removeAllNoThrow(temp_root);
    RemovePathOnExit cleanup{temp_root};
    std::filesystem::create_directories(temp_root);

    const auto env_root = temp_root / "env";
    const auto package_root = temp_root / "lite_view_commit_ext";
    std::filesystem::create_directories(package_root);
    createVirtualEnvironment(bundled_python, env_root);
    if (!provisionBundledRuntimePackageIntoEnvironment(runtime_root, env_root, "numpy"))
    {
        GTEST_SKIP() << "numpy not available in bundled runtime";
    }

    {
        std::ofstream out(package_root / "lite_view_commit_ext.py", std::ios::binary);
        ASSERT_TRUE(out.is_open());
        out << "import afternight.ui_protocol as ui\n"
            << "import numpy as np\n"
            << "from afternight.lite import LiteProcessExtension\n\n"
            << "class LiteViewCommitExtension(LiteProcessExtension):\n"
            << "    def __init__(self, context):\n"
            << "        super().__init__(context)\n\n"
            << "    def on_launch(self):\n"
            << "        generated = np.array([[10, 20], [30, 40]], dtype=np.float32)\n"
            << "        assert ui.commit_image(generated, history_step_name='Lite Commit', "
               "history_step_description='Committed by lite host', metadata={'STEP': 'lite.view.commit'}, "
               "allow_resized_output=True)\n"
            << "        self.settings.set('commit_done', True)\n";
    }

    InMemorySettings settings_store;
    GlobalSettingsPersistence& settings = settings_store.repository();
    an::ui::extensions::ExtensionSdkService sdk_service(&settings);
    an::ui::extensions::ExtensionHostManager manager;
    manager.setSdkService(&sdk_service);

    an::ui::extensions::ExtensionHostLaunchRequest request;
    request.extension_id = "an.demo.lite.view_commit";
    request.sdk_backend = "protocol";
    request.host_executable = host_path;
    request.capabilities = {"app", "views", "execute"};
    request.host_arguments = {
        "--extension-package-root", package_root.string(), "--entry-point",
        "lite_view_commit_ext",     "--process-class",     "LiteViewCommitExtension",
        "--environment-root",       env_root.string(),     "--runtime-root",
        runtime_root.string(),      "--stay-alive-ms",     std::to_string(5000 * kIpcTimingMultiplier)};

    std::string error;
    auto prepared = manager.prepareSession(request);
    an::ui::extensions::ExtensionHostRunningSession running_session;
    ASSERT_TRUE(manager.launchSession(prepared, &running_session, &error)) << error;
    SessionGuard session_guard{manager, running_session};

    QElapsedTimer timer;
    timer.start();
    bool observed_commit = false;
    while (timer.elapsed() < 5000 * kIpcTimingMultiplier)
    {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 1);
        ASSERT_TRUE(manager.pumpSession(&running_session, 20, &error)) << error;

        QCborValue done;
        const bool has_done = sdk_service.selectExtensionSetting(request.extension_id, "commit_done", &done,
                                                                 QCborValue(false), nullptr) &&
                              done.toBool();
        if (has_done && running_session.last_view_commit.has_value())
        {
            observed_commit = true;
            break;
        }
    }

    ASSERT_TRUE(observed_commit) << "Timed out waiting for lite view.commit result";
    ASSERT_TRUE(running_session.last_view_commit.has_value());
    const auto& view_commit = *running_session.last_view_commit;
    EXPECT_EQ(view_commit.history_step_name, "Lite Commit");
    EXPECT_EQ(view_commit.history_step_description, "Committed by lite host");
    EXPECT_TRUE(view_commit.allow_resized_output);
    ASSERT_EQ(view_commit.result_buffers.size(), 1u);

    const auto& buffer = view_commit.result_buffers.front();
    const std::filesystem::path file_path = std::filesystem::path(buffer.mapped_file_path).lexically_normal();
    const std::filesystem::path root_path = file_path.parent_path().parent_path();
    an::ui::extensions::SharedBufferManager shared_buffer_manager(root_path);
    std::optional<an::consumer::ImageAsset> committed_image;
    ASSERT_TRUE(importBufferOrReport(shared_buffer_manager, buffer, &committed_image, &error)) << error;
    ASSERT_TRUE(committed_image);
    EXPECT_EQ(widthOf(*committed_image), 2u);
    EXPECT_EQ(heightOf(*committed_image), 2u);
    EXPECT_EQ(channelsOf(*committed_image), 1u);
    EXPECT_NEAR(sampleAt(*committed_image, 0, 0, 0), 10.0, 1e-6);
    EXPECT_NEAR(sampleAt(*committed_image, 1, 1, 0), 40.0, 1e-6);
}

// A Lite extension hears of views created, renamed and removed through its lifecycle callbacks.
TEST(ExtensionHostManagerTest, ExtensionHostLiteDispatchesViewLifecycleCallbacks)
{
    const std::filesystem::path host_path = extensionHostLitePathForCurrentAppDir();
    const std::filesystem::path runtime_root = runtimeRootForCurrentAppDir();
    const std::filesystem::path bundled_python = repositoryBundledPythonPath();
    ASSERT_FALSE(host_path.empty()) << "AfterNightExtensionHostLite must be built before this test runs";
    ASSERT_TRUE(std::filesystem::exists(host_path)) << host_path;
    ASSERT_TRUE(std::filesystem::exists(runtime_root)) << runtime_root;
    ASSERT_FALSE(bundled_python.empty()) << "Repository bundled Python runtime must be available for lite callbacks";

    const auto temp_root = uniqueExtensionRuntimeRootForTest("lite-protocol-callbacks");
    removeAllNoThrow(temp_root);
    RemovePathOnExit cleanup{temp_root};
    std::filesystem::create_directories(temp_root);

    const auto env_root = temp_root / "env";
    const auto package_root = temp_root / "lite_callback_ext";
    std::filesystem::create_directories(package_root);
    createVirtualEnvironment(bundled_python, env_root);

    {
        std::ofstream out(package_root / "lite_callback_ext.py", std::ios::binary);
        ASSERT_TRUE(out.is_open());
        out << "from afternight.lite import LiteProcessExtension\n\n"
            << "class LiteCallbackExtension(LiteProcessExtension):\n"
            << "    def __init__(self, context):\n"
            << "        super().__init__(context)\n\n"
            << "    def on_launch(self):\n"
            << "        self.settings.set('launch_seen', True)\n"
            << "        while not bool(self.settings.get('continue_launch', False)):\n"
            << "            pass\n\n"
            << "    def on_view_created(self, view):\n"
            << "        data = dict(view or {})\n"
            << "        self.settings.set('created_view', {\n"
            << "            'id': data.get('view_id', ''),\n"
            << "            'name': data.get('name', ''),\n"
            << "            'file_name': data.get('file_name', ''),\n"
            << "            'width': int(data.get('width', 0)),\n"
            << "        })\n\n"
            << "    def on_view_renamed(self, view):\n"
            << "        data = dict(view or {})\n"
            << "        self.settings.set('renamed_view', {\n"
            << "            'id': data.get('view_id', ''),\n"
            << "            'name': data.get('name', ''),\n"
            << "            'file_name': data.get('file_name', ''),\n"
            << "        })\n\n"
            << "    def on_view_removed(self, view_id):\n"
            << "        self.settings.set('removed_view_id', str(view_id))\n";
    }

    InMemorySettings settings_store;
    GlobalSettingsPersistence& settings = settings_store.repository();
    an::ui::extensions::ExtensionSdkService settings_reader(&settings);
    an::tests::processes::framework::CaseLaunchCoordinator coordinator;
    coordinator.configureSdkService(&settings, []() -> std::vector<an::ui::extensions::ExtensionViewState> {
        an::ui::extensions::ExtensionViewState view_state;
        view_state.view_name = "target-alpha";
        view_state.file_name = "/tmp/target-alpha.fit";
        view_state.width = 24;
        view_state.height = 12;
        view_state.channels = 3;
        view_state.is_active = true;
        view_state.has_image = false;
        return {view_state};
    });

    an::ui::extensions::ExtensionHostLaunchBinding binding;
    binding.descriptor_id = "lite.callback.descriptor";
    binding.extension_id = "an.demo.lite.callbacks";
    binding.display_name = "Lite Callback Probe";
    binding.sdk_backend = "protocol";
    binding.launch_mode = an::ui::extensions::ExtensionLaunchMode::SingleImage;
    binding.host_executable = host_path;
    binding.capabilities = {"app", "views"};
    binding.host_arguments = {
        "--extension-package-root", package_root.string(), "--entry-point",
        "lite_callback_ext",        "--process-class",     "LiteCallbackExtension",
        "--environment-root",       env_root.string(),     "--runtime-root",
        runtime_root.string(),      "--stay-alive-ms",     std::to_string(5000 * kIpcTimingMultiplier)};

    std::string error;
    ASSERT_TRUE(coordinator.launchBoundDescriptor(binding, fakeTargetControl(), &error)) << error;
    // The Lite host connects before it starts Python, and the session is ready once the extension is imported.
    ASSERT_TRUE(
        pumpCoordinatorUntilNativeSessionReady(coordinator, binding.descriptor_id, 5000 * kIpcTimingMultiplier));
    ASSERT_TRUE(pumpCoordinatorUntil(coordinator, 3000 * kIpcTimingMultiplier, [&settings_reader, &binding]() {
        QCborValue launch_seen;
        return settings_reader.selectExtensionSetting(binding.extension_id, "launch_seen", &launch_seen,
                                                      QCborValue(false), nullptr) &&
               launch_seen.toBool();
    }));

    coordinator.handleViewControlCreated(nullptr, "new-view-id", "New View", "/tmp/new-view.fit");
    ASSERT_TRUE(pumpCoordinatorUntil(coordinator, 3000 * kIpcTimingMultiplier, [&settings_reader, &binding]() {
        QCborValue created_view;
        return settings_reader.selectExtensionSetting(binding.extension_id, "created_view", &created_view, QCborValue(),
                                                      nullptr) &&
               created_view.isMap();
    }));

    coordinator.handleViewControlRenamed("target-beta", "target-alpha", fakeTargetControl(), "/tmp/target-beta.fit");
    ASSERT_TRUE(pumpCoordinatorUntil(coordinator, 3000 * kIpcTimingMultiplier, [&settings_reader, &binding]() {
        QCborValue renamed_view;
        return settings_reader.selectExtensionSetting(binding.extension_id, "renamed_view", &renamed_view, QCborValue(),
                                                      nullptr) &&
               renamed_view.isMap();
    }));

    coordinator.handleViewControlDestroyed(fakeTargetControl(), "target-beta");
    ASSERT_TRUE(pumpCoordinatorUntil(coordinator, 3000 * kIpcTimingMultiplier, [&settings_reader, &binding]() {
        QString removed_id;
        return settings_reader.selectExtensionSetting(binding.extension_id, "removed_view_id", &removed_id, {},
                                                      nullptr) &&
               !removed_id.isEmpty();
    }));

    ASSERT_TRUE(settings_reader.upsertExtensionSetting(binding.extension_id, "continue_launch", true, &error)) << error;

    QCborValue created_value;
    ASSERT_TRUE(settings_reader.selectExtensionSetting(binding.extension_id, "created_view", &created_value,
                                                       QCborValue(), &error))
        << error;
    ASSERT_TRUE(created_value.isMap());
    const QCborMap created = created_value.toMap();
    EXPECT_EQ(created.value(QStringLiteral("id")).toString(), QStringLiteral("new-view-id"));
    EXPECT_EQ(created.value(QStringLiteral("name")).toString(), QStringLiteral("New View"));
    EXPECT_EQ(created.value(QStringLiteral("file_name")).toString(), QStringLiteral("/tmp/new-view.fit"));

    QCborValue renamed_value;
    ASSERT_TRUE(settings_reader.selectExtensionSetting(binding.extension_id, "renamed_view", &renamed_value,
                                                       QCborValue(), &error))
        << error;
    ASSERT_TRUE(renamed_value.isMap());
    const QCborMap renamed = renamed_value.toMap();
    EXPECT_EQ(renamed.value(QStringLiteral("name")).toString(), QStringLiteral("target-beta"));
    EXPECT_EQ(renamed.value(QStringLiteral("file_name")).toString(), QStringLiteral("/tmp/target-alpha.fit"));

    QString removed_id;
    ASSERT_TRUE(
        settings_reader.selectExtensionSetting(binding.extension_id, "removed_view_id", &removed_id, {}, &error))
        << error;
    EXPECT_EQ(removed_id, QStringLiteral("target-alpha"));

    ASSERT_TRUE(stopCoordinatorAndWaitForSessionAbsence(coordinator, 1500 * kIpcTimingMultiplier));
}

// A protocol extension whose import outlasts the handshake's bound, as a heavy one's first start from a freshly
// provisioned environment does, launches without the launch waiting for the import: the Lite host connects before it
// starts Python, and the session is ready, and the extension's launch hook runs, once the import ends.
TEST(ExtensionHostManagerTest, AProtocolExtensionWhoseImportOutlastsTheHandshakeLaunchesWithoutWaitingForIt)
{
    const std::filesystem::path host_path = extensionHostLitePathForCurrentAppDir();
    const std::filesystem::path runtime_root = runtimeRootForCurrentAppDir();
    const std::filesystem::path bundled_python = repositoryBundledPythonPath();
    ASSERT_FALSE(host_path.empty()) << "AfterNightExtensionHostLite must be built before this test runs";
    ASSERT_TRUE(std::filesystem::exists(host_path)) << host_path;
    ASSERT_TRUE(std::filesystem::exists(runtime_root)) << runtime_root;
    ASSERT_FALSE(bundled_python.empty()) << "Repository bundled Python runtime must be available";

    const auto temp_root = uniqueExtensionRuntimeRootForTest("lite-slow-import");
    removeAllNoThrow(temp_root);
    RemovePathOnExit cleanup{temp_root};
    std::filesystem::create_directories(temp_root);

    const auto env_root = temp_root / "env";
    const auto package_root = temp_root / "lite_slow_import_ext";
    std::filesystem::create_directories(package_root);
    createVirtualEnvironment(bundled_python, env_root);

    // The import is slow in a live session only: the parameter schema the launch reads first is read with no session,
    // as one read on an earlier launch would be, so what the case times is the launch's own wait.
    constexpr int kImportSeconds = 7;
    ASSERT_GT(kImportSeconds * 1000, an::ui::extensions::ExtensionHostLaunchRequest{}.handshake_timeout_ms);
    {
        std::ofstream out(package_root / "lite_slow_import_ext.py", std::ios::binary);
        ASSERT_TRUE(out.is_open());
        out << "import os\n"
            << "import time\n"
            << "from afternight.lite import LiteProcessExtension\n\n"
            << "if os.environ.get('AFTERNIGHT_SESSION_ID'):\n"
            << "    time.sleep(" << kImportSeconds << ")\n\n"
            << "class LiteSlowImportExtension(LiteProcessExtension):\n"
            << "    def on_launch(self):\n"
            << "        self.settings.set('launched', True)\n";
    }

    InMemorySettings settings_store;
    GlobalSettingsPersistence& settings = settings_store.repository();
    an::ui::extensions::ExtensionSdkService settings_reader(&settings);
    an::tests::processes::framework::CaseLaunchCoordinator coordinator;
    coordinator.configureSdkService(&settings, []() { return std::vector<an::ui::extensions::ExtensionViewState>{}; });

    an::ui::extensions::ExtensionHostLaunchBinding binding;
    binding.descriptor_id = "lite.slow.import.descriptor";
    binding.extension_id = "an.demo.lite.slow.import";
    binding.display_name = "Lite Slow Import Probe";
    binding.sdk_backend = "protocol";
    binding.launch_mode = an::ui::extensions::ExtensionLaunchMode::SingleImage;
    binding.host_executable = host_path;
    binding.capabilities = {"app", "views"};
    binding.host_arguments = {
        "--extension-package-root", package_root.string(), "--entry-point",
        "lite_slow_import_ext",     "--process-class",     "LiteSlowImportExtension",
        "--environment-root",       env_root.string(),     "--runtime-root",
        runtime_root.string(),      "--stay-alive-ms",     std::to_string(20000 * kIpcTimingMultiplier)};

    std::string error;
    QElapsedTimer launch_timer;
    launch_timer.start();
    ASSERT_TRUE(coordinator.launchBoundDescriptor(binding, fakeTargetControl(), &error)) << error;
    EXPECT_LT(launch_timer.elapsed(), kImportSeconds * 1000) << "the launch waited for the extension's import";

    ASSERT_TRUE(pumpCoordinatorUntilNativeSessionReady(coordinator, binding.descriptor_id,
                                                       (kImportSeconds * 1000 + 10000) * kIpcTimingMultiplier));
    ASSERT_TRUE(pumpCoordinatorUntil(coordinator, 3000 * kIpcTimingMultiplier, [&settings_reader, &binding]() {
        QCborValue launched;
        return settings_reader.selectExtensionSetting(binding.extension_id, "launched", &launched, QCborValue(false),
                                                      nullptr) &&
               launched.toBool();
    })) << "the extension's launch hook did not run once its import ended";

    ASSERT_TRUE(stopCoordinatorAndWaitForSessionAbsence(coordinator, 1500 * kIpcTimingMultiplier));
}

// An extension's parameter schema is read, before its first session in a run, by a host of its own that imports it, and
// a launch does not wait for that read, which takes as long as the import: a second launch while it runs is the first
// one's, the session starts once the schema lands, and a launch after it, under the same settings, starts with the
// schema that read kept. The extension writes no setting, since a launch under changed settings reads the schema again.
TEST(ExtensionHostManagerTest, AnExtensionWhoseParameterSchemaIsSlowToReadLaunchesWithoutWaitingForTheRead)
{
    const std::filesystem::path host_path = extensionHostLitePathForCurrentAppDir();
    const std::filesystem::path runtime_root = runtimeRootForCurrentAppDir();
    const std::filesystem::path bundled_python = repositoryBundledPythonPath();
    ASSERT_FALSE(host_path.empty()) << "AfterNightExtensionHostLite must be built before this test runs";
    ASSERT_TRUE(std::filesystem::exists(host_path)) << host_path;
    ASSERT_TRUE(std::filesystem::exists(runtime_root)) << runtime_root;
    ASSERT_FALSE(bundled_python.empty()) << "Repository bundled Python runtime must be available";

    const auto temp_root = uniqueExtensionRuntimeRootForTest("lite-slow-schema");
    removeAllNoThrow(temp_root);
    RemovePathOnExit cleanup{temp_root};
    std::filesystem::create_directories(temp_root);

    const auto env_root = temp_root / "env";
    const auto package_root = temp_root / "lite_slow_schema_ext";
    std::filesystem::create_directories(package_root);
    createVirtualEnvironment(bundled_python, env_root);

    // The import is slow in the host that reads the schema only, which runs with no session, so what the case times
    // is the launch's wait for that read.
    constexpr int kReadSeconds = 4;
    {
        std::ofstream out(package_root / "lite_slow_schema_ext.py", std::ios::binary);
        ASSERT_TRUE(out.is_open());
        out << "import os\n"
            << "import time\n"
            << "from afternight.lite import LiteProcessExtension\n\n"
            << "if not os.environ.get('AFTERNIGHT_SESSION_ID'):\n"
            << "    time.sleep(" << kReadSeconds << ")\n\n"
            << "class LiteSlowSchemaExtension(LiteProcessExtension):\n"
            << "    pass\n";
    }

    InMemorySettings settings_store;
    an::tests::processes::framework::CaseLaunchCoordinator coordinator;
    coordinator.configureSdkService(&settings_store.repository(),
                                    []() { return std::vector<an::ui::extensions::ExtensionViewState>{}; });

    an::ui::extensions::ExtensionHostLaunchBinding binding;
    binding.descriptor_id = "lite.slow.schema.descriptor";
    binding.extension_id = "an.demo.lite.slow.schema";
    binding.display_name = "Lite Slow Schema Probe";
    binding.sdk_backend = "protocol";
    binding.launch_mode = an::ui::extensions::ExtensionLaunchMode::SingleImage;
    binding.host_executable = host_path;
    binding.capabilities = {"app", "views"};
    binding.host_arguments = {
        "--extension-package-root", package_root.string(), "--entry-point",
        "lite_slow_schema_ext",     "--process-class",     "LiteSlowSchemaExtension",
        "--environment-root",       env_root.string(),     "--runtime-root",
        runtime_root.string(),      "--stay-alive-ms",     std::to_string(20000 * kIpcTimingMultiplier)};

    std::string error;
    QElapsedTimer launch_timer;
    launch_timer.start();
    ASSERT_TRUE(coordinator.launchBoundDescriptor(binding, fakeTargetControl(), &error)) << error;
    EXPECT_LT(launch_timer.elapsed(), kReadSeconds * 1000) << "the launch waited for its parameter schema's read";

    std::string duplicate_error;
    EXPECT_FALSE(coordinator.launchBoundDescriptor(binding, fakeTargetControl(), &duplicate_error));
    EXPECT_NE(duplicate_error.find("already has an active host session"), std::string::npos) << duplicate_error;

    ASSERT_TRUE(pumpCoordinatorUntilNativeSessionReady(coordinator, binding.descriptor_id,
                                                       (kReadSeconds * 1000 + 10000) * kIpcTimingMultiplier))
        << "the session did not start once its schema was read";
    ASSERT_TRUE(stopCoordinatorAndWaitForSessionAbsence(coordinator, 1500 * kIpcTimingMultiplier));

    launch_timer.restart();
    ASSERT_TRUE(coordinator.launchBoundDescriptor(binding, fakeTargetControl(), &error)) << error;
    EXPECT_TRUE(pumpCoordinatorUntil(coordinator, (kReadSeconds * 1000) / 2, [&coordinator, &binding]() {
        return coordinator.hasReadySessionForDescriptor(binding.descriptor_id);
    })) << "the second launch read the parameter schema again";
    ASSERT_TRUE(stopCoordinatorAndWaitForSessionAbsence(coordinator, 1500 * kIpcTimingMultiplier));
}

// A launch whose parameter schema cannot be read starts no session and is told to the person once the read has failed,
// as the menu tells a launch refused at once.
TEST(ExtensionHostManagerTest, ALaunchWhoseParameterSchemaCannotBeReadIsToldOnceTheReadFails)
{
    const std::filesystem::path host_path = extensionHostLitePathForCurrentAppDir();
    const std::filesystem::path runtime_root = runtimeRootForCurrentAppDir();
    const std::filesystem::path bundled_python = repositoryBundledPythonPath();
    ASSERT_FALSE(host_path.empty()) << "AfterNightExtensionHostLite must be built before this test runs";
    ASSERT_TRUE(std::filesystem::exists(host_path)) << host_path;
    ASSERT_TRUE(std::filesystem::exists(runtime_root)) << runtime_root;
    ASSERT_FALSE(bundled_python.empty()) << "Repository bundled Python runtime must be available";

    const auto temp_root = uniqueExtensionRuntimeRootForTest("lite-refused-schema");
    removeAllNoThrow(temp_root);
    RemovePathOnExit cleanup{temp_root};
    std::filesystem::create_directories(temp_root);

    const auto env_root = temp_root / "env";
    const auto package_root = temp_root / "lite_refused_schema_ext";
    std::filesystem::create_directories(package_root);
    createVirtualEnvironment(bundled_python, env_root);

    const std::string token = "parameter schema refused for this case";
    {
        std::ofstream out(package_root / "lite_refused_schema_ext.py", std::ios::binary);
        ASSERT_TRUE(out.is_open());
        out << "import os\n"
            << "from afternight.lite import LiteProcessExtension\n\n"
            << "if not os.environ.get('AFTERNIGHT_SESSION_ID'):\n"
            << "    raise RuntimeError(" << quotedPythonString(token) << ")\n\n"
            << "class LiteRefusedSchemaExtension(LiteProcessExtension):\n"
            << "    pass\n";
    }

    InMemorySettings settings_store;
    an::tests::processes::framework::CaseLaunchCoordinator coordinator;
    coordinator.configureSdkService(&settings_store.repository(),
                                    []() { return std::vector<an::ui::extensions::ExtensionViewState>{}; });

    an::ui::extensions::ExtensionHostLaunchBinding binding;
    binding.descriptor_id = "lite.refused.schema.descriptor";
    binding.extension_id = "an.demo.lite.refused.schema";
    binding.display_name = "Lite Refused Schema Probe";
    binding.sdk_backend = "protocol";
    binding.launch_mode = an::ui::extensions::ExtensionLaunchMode::SingleImage;
    binding.host_executable = host_path;
    binding.capabilities = {"app", "views"};
    binding.host_arguments = {
        "--extension-package-root", package_root.string(), "--entry-point",
        "lite_refused_schema_ext",  "--process-class",     "LiteRefusedSchemaExtension",
        "--environment-root",       env_root.string(),     "--runtime-root",
        runtime_root.string(),      "--stay-alive-ms",     std::to_string(5000 * kIpcTimingMultiplier)};

    auto& context = an::tests::processes::framework::suiteContext();
    const auto errors_before = context.errorTexts.size();
    std::string error;
    ASSERT_TRUE(coordinator.launchBoundDescriptor(binding, fakeTargetControl(), &error))
        << "the launch waited for its parameter schema's read: " << error;

    ASSERT_TRUE(pumpCoordinatorUntil(coordinator, 10000 * kIpcTimingMultiplier, [&context, errors_before]() {
        return context.errorTexts.size() > errors_before;
    })) << "the failed read was not told";
    EXPECT_EQ(context.errorTexts.size(), errors_before + 1);
    EXPECT_EQ(context.messages.back(), QStringLiteral("error:") + QString::fromStdString(binding.display_name));
    EXPECT_TRUE(context.errorTexts.back().contains(QString::fromStdString(token)))
        << context.errorTexts.back().toStdString();
    EXPECT_EQ(coordinator.activeSessionCount(), 0u);
    EXPECT_FALSE(coordinator.hasActiveSessionForDescriptor(binding.descriptor_id, fakeTargetControl()));
}

// A parameter schema read that runs past the bound its coordinator states is stopped there and the launch refused,
// naming the bound: the bound is the caller's to state, and a case under the leak configuration states a longer one.
TEST(ExtensionHostManagerTest, AReadThatRunsPastTheStatedBoundIsStoppedAndTheLaunchRefusedNamingIt)
{
    const std::filesystem::path host_path = extensionHostLitePathForCurrentAppDir();
    const std::filesystem::path runtime_root = runtimeRootForCurrentAppDir();
    const std::filesystem::path bundled_python = repositoryBundledPythonPath();
    ASSERT_FALSE(host_path.empty()) << "AfterNightExtensionHostLite must be built before this test runs";
    ASSERT_TRUE(std::filesystem::exists(host_path)) << host_path;
    ASSERT_TRUE(std::filesystem::exists(runtime_root)) << runtime_root;
    ASSERT_FALSE(bundled_python.empty()) << "Repository bundled Python runtime must be available";

    const auto temp_root = uniqueExtensionRuntimeRootForTest("lite-bounded-schema");
    removeAllNoThrow(temp_root);
    RemovePathOnExit cleanup{temp_root};
    std::filesystem::create_directories(temp_root);

    const auto env_root = temp_root / "env";
    const auto package_root = temp_root / "lite_bounded_schema_ext";
    std::filesystem::create_directories(package_root);
    createVirtualEnvironment(bundled_python, env_root);
    {
        std::ofstream out(package_root / "lite_bounded_schema_ext.py", std::ios::binary);
        ASSERT_TRUE(out.is_open());
        out << "import os\n"
            << "import time\n"
            << "from afternight.lite import LiteProcessExtension\n\n"
            << "if not os.environ.get('AFTERNIGHT_SESSION_ID'):\n"
            << "    time.sleep(30)\n\n"
            << "class LiteBoundedSchemaExtension(LiteProcessExtension):\n"
            << "    pass\n";
    }

    InMemorySettings settings_store;
    an::ui::extensions::ExtensionHostLaunchCoordinator coordinator;
    coordinator.configureSdkService(&settings_store.repository(),
                                    []() { return std::vector<an::ui::extensions::ExtensionViewState>{}; });
    coordinator.setParameterSchemaReadBoundMs(1000);

    an::ui::extensions::ExtensionHostLaunchBinding binding;
    binding.descriptor_id = "lite.bounded.schema.descriptor";
    binding.extension_id = "an.demo.lite.bounded.schema";
    binding.display_name = "Lite Bounded Schema Probe";
    binding.sdk_backend = "protocol";
    binding.launch_mode = an::ui::extensions::ExtensionLaunchMode::SingleImage;
    binding.host_executable = host_path;
    binding.capabilities = {"app", "views"};
    binding.host_arguments = {
        "--extension-package-root", package_root.string(), "--entry-point",
        "lite_bounded_schema_ext",  "--process-class",     "LiteBoundedSchemaExtension",
        "--environment-root",       env_root.string(),     "--runtime-root",
        runtime_root.string(),      "--stay-alive-ms",     std::to_string(5000 * kIpcTimingMultiplier)};

    auto& context = an::tests::processes::framework::suiteContext();
    const auto errors_before = context.errorTexts.size();
    std::string error;
    QElapsedTimer read_timer;
    read_timer.start();
    ASSERT_TRUE(coordinator.launchBoundDescriptor(binding, fakeTargetControl(), &error)) << error;
    ASSERT_TRUE(pumpCoordinatorUntil(coordinator, 20000 * kIpcTimingMultiplier, [&context, errors_before]() {
        return context.errorTexts.size() > errors_before;
    })) << "the read that ran past its bound was not told";
    EXPECT_LT(read_timer.elapsed(), 25000) << "the read ran to its host's end rather than to its bound";
    EXPECT_TRUE(context.errorTexts.back().contains(QStringLiteral("did not finish within 1 seconds")))
        << context.errorTexts.back().toStdString();
    EXPECT_FALSE(coordinator.hasActiveSessionForDescriptor(binding.descriptor_id, fakeTargetControl()));
}

// The parameter schema a launch read is kept for the descriptor until the descriptor is bound again, as an extension
// discovered again is, whose package may have changed in place under the same arguments: the next launch reads it
// again.
TEST(ExtensionHostManagerTest, BindingADescriptorAgainLetsTheParameterSchemaItsLaunchReadGo)
{
    const std::filesystem::path host_path = extensionHostLitePathForCurrentAppDir();
    const std::filesystem::path runtime_root = runtimeRootForCurrentAppDir();
    const std::filesystem::path bundled_python = repositoryBundledPythonPath();
    ASSERT_FALSE(host_path.empty()) << "AfterNightExtensionHostLite must be built before this test runs";
    ASSERT_TRUE(std::filesystem::exists(host_path)) << host_path;
    ASSERT_TRUE(std::filesystem::exists(runtime_root)) << runtime_root;
    ASSERT_FALSE(bundled_python.empty()) << "Repository bundled Python runtime must be available";

    const auto temp_root = uniqueExtensionRuntimeRootForTest("lite-rebound-schema");
    removeAllNoThrow(temp_root);
    RemovePathOnExit cleanup{temp_root};
    std::filesystem::create_directories(temp_root);

    const auto env_root = temp_root / "env";
    const auto package_root = temp_root / "lite_rebound_schema_ext";
    std::filesystem::create_directories(package_root);
    createVirtualEnvironment(bundled_python, env_root);
    {
        std::ofstream out(package_root / "lite_rebound_schema_ext.py", std::ios::binary);
        ASSERT_TRUE(out.is_open());
        out << "from afternight.lite import LiteProcessExtension\n\n"
            << "class LiteReboundSchemaExtension(LiteProcessExtension):\n"
            << "    pass\n";
    }

    InMemorySettings settings_store;
    an::tests::processes::framework::CaseLaunchCoordinator coordinator;
    coordinator.configureSdkService(&settings_store.repository(),
                                    []() { return std::vector<an::ui::extensions::ExtensionViewState>{}; });

    an::ui::extensions::ExtensionHostLaunchBinding binding;
    binding.descriptor_id = "lite.rebound.schema.descriptor";
    binding.extension_id = "an.demo.lite.rebound.schema";
    binding.display_name = "Lite Rebound Schema Probe";
    binding.sdk_backend = "protocol";
    binding.launch_mode = an::ui::extensions::ExtensionLaunchMode::SingleImage;
    binding.host_executable = host_path;
    binding.capabilities = {"app", "views"};
    binding.host_arguments = {
        "--extension-package-root", package_root.string(), "--entry-point",
        "lite_rebound_schema_ext",  "--process-class",     "LiteReboundSchemaExtension",
        "--environment-root",       env_root.string(),     "--runtime-root",
        runtime_root.string(),      "--stay-alive-ms",     std::to_string(5000 * kIpcTimingMultiplier)};

    const auto launch_and_stop = [&coordinator, &binding](const char* which) -> bool {
        std::string error;
        const bool launched = coordinator.launchBoundDescriptor(binding, fakeTargetControl(), &error);
        EXPECT_TRUE(launched) << which << ": " << error;
        const bool reads = coordinator.isReadingParameterSchemaFor(binding.descriptor_id);
        EXPECT_TRUE(launched && pumpCoordinatorUntilNativeSessionReady(coordinator, binding.descriptor_id,
                                                                       3000 * kIpcTimingMultiplier))
            << which;
        EXPECT_TRUE(stopCoordinatorAndWaitForSessionAbsence(coordinator, 1500 * kIpcTimingMultiplier)) << which;
        return reads;
    };

    EXPECT_TRUE(launch_and_stop("the first launch")) << "the first launch did not read the parameter schema";
    EXPECT_FALSE(launch_and_stop("the second launch")) << "the second launch read the schema the first kept again";

    an::ui::process_registry::ProcessDescriptor descriptor;
    descriptor.id = binding.descriptor_id;
    coordinator.bindSingleImageDescriptor(&descriptor, binding);
    EXPECT_TRUE(launch_and_stop("the launch after the binding"))
        << "the launch after the descriptor was bound again started with the schema read before it";
}

// A standalone full-host session runs a runtime extension's launch hook with the engine's domains available.
TEST(ExtensionHostManagerTest, ExtensionHostStandaloneImportsRuntimeExtensionWithDirectEngineBackend)
{
    const std::filesystem::path host_path = extensionHostPathForCurrentAppDir();
    const std::filesystem::path runtime_root = runtimeRootForCurrentAppDir();
    const std::filesystem::path bundled_python = repositoryBundledPythonPath();
    AN_REQUIRE_FULL_EXTENSION_HOST(host_path);
    ASSERT_TRUE(std::filesystem::exists(runtime_root)) << runtime_root;
    ASSERT_FALSE(bundled_python.empty()) << "Repository bundled Python runtime must be available for full standalone";

    const auto temp_root = uniqueExtensionRuntimeRootForTest("full-runtime-standalone");
    removeAllNoThrow(temp_root);
    RemovePathOnExit cleanup{temp_root};
    std::filesystem::create_directories(temp_root);

    const auto env_root = temp_root / "env";
    const auto package_root = temp_root / "full_standalone_ext";
    const auto marker_path = temp_root / "standalone_marker.json";
    std::filesystem::create_directories(package_root);
    createVirtualEnvironment(bundled_python, env_root);

    {
        std::ofstream out(package_root / "full_standalone_ext.py", std::ios::binary);
        ASSERT_TRUE(out.is_open());
        out << "import json\n"
            << "import afternight\n\n"
            << "MARKER = " << quotedPythonString(marker_path.generic_string()) << "\n\n"
            << "class FullStandaloneExtension(afternight.ProcessExtension):\n"
            << "    def __init__(self, context):\n"
            << "        super().__init__(context)\n\n"
            << "    def get_params(self):\n"
            << "        return []\n\n"
            << "    def on_process_launch(self):\n"
            << "        info = afternight.current_session()\n"
            << "        assert info.is_standalone is True\n"
            << "        assert info.session_id == 'full-standalone-session'\n"
            << "        assert afternight.list_views() == []\n"
            << "        assert 'float32' in afternight.core.available_pixel_types()\n"
            << "        with open(MARKER, 'w', encoding='utf-8') as marker:\n"
            << "            json.dump({'session_id': info.session_id, 'standalone': info.is_standalone}, marker)\n\n"
            << "    def execute(self, target, src_image, dst_image, params, progress, masks=None, weights=None, "
               "output_masks=None):\n"
            << "        raise NotImplementedError\n";
    }

    QProcess process;
    process.setProgram(QString::fromStdString(host_path.string()));
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.setArguments({QStringLiteral("--standalone"), QStringLiteral("--sdk-backend"), QStringLiteral("runtime"),
                          QStringLiteral("--session-id"), QStringLiteral("full-standalone-session"),
                          QStringLiteral("--extension-id"), QStringLiteral("an.demo.full_standalone"),
                          QStringLiteral("--extension-package-root"), QString::fromStdString(package_root.string()),
                          QStringLiteral("--entry-point"), QStringLiteral("full_standalone_ext"),
                          QStringLiteral("--process-class"), QStringLiteral("FullStandaloneExtension"),
                          QStringLiteral("--environment-root"), QString::fromStdString(env_root.string()),
                          QStringLiteral("--runtime-root"), QString::fromStdString(runtime_root.string())});
    process.start();
    ASSERT_TRUE(process.waitForStarted());
    // A standalone host ends the case by exiting.
    ASSERT_TRUE(process.waitForFinished(10000 * kIpcTimingMultiplier + hostExitAllowanceMs()));

    const std::string output = process.readAll().toStdString();
    EXPECT_EQ(process.exitStatus(), QProcess::NormalExit) << output;
    EXPECT_EQ(process.exitCode(), 0) << output;
    EXPECT_TRUE(std::filesystem::exists(marker_path)) << output;
}

// The full host exits cleanly when its control channel goes away.
TEST(ExtensionHostManagerTest, ExtensionHostExitsWhenControlSocketDisconnects)
{
    const std::filesystem::path host_path = extensionHostPathForCurrentAppDir();
    AN_REQUIRE_FULL_EXTENSION_HOST(host_path);

    an::ui::extensions::ExtensionHostManager manager;
    const auto request = makeLaunchRequest(host_path, {"--stay-alive-ms", std::to_string(5000 * kIpcTimingMultiplier)});

    const auto session = manager.prepareSession(request);
    an::ui::extensions::ExtensionHostRunningSession running_session;
    std::string error;
    ASSERT_TRUE(manager.launchSession(session, &running_session, &error)) << error;
    SessionGuard guard{manager, running_session};

    ASSERT_NE(running_session.process, nullptr);
    ASSERT_NE(running_session.socket, nullptr);
    const qint64 host_pid = running_session.process->processId();
    ASSERT_GT(host_pid, 0);

    running_session.socket->disconnectFromServer();
    if (running_session.socket->state() != QLocalSocket::UnconnectedState)
    {
        running_session.socket->waitForDisconnected(1000 * kIpcTimingMultiplier);
    }
    running_session.socket.reset();

    ASSERT_TRUE(waitForProcessExit(host_pid, 2000 * kIpcTimingMultiplier));
    ASSERT_TRUE(running_session.process->waitForFinished(1000 * kIpcTimingMultiplier));
    EXPECT_EQ(running_session.process->exitStatus(), QProcess::NormalExit);
    EXPECT_EQ(running_session.process->exitCode(), 0);
}

// Measures how long the full host takes from spawn to an acknowledged handshake.
TEST(ExtensionHostManagerTest, PreliminarySpawnToHandshakeBaseline)
{
    const std::filesystem::path host_path = extensionHostPathForCurrentAppDir();
    AN_REQUIRE_FULL_EXTENSION_HOST(host_path);

    constexpr int kSampleCount = 7;

    an::ui::extensions::ExtensionHostManager manager;
    std::vector<qint64> launch_durations_ms;
    launch_durations_ms.reserve(kSampleCount);

    for (int sample_index = 0; sample_index < kSampleCount; ++sample_index)
    {
        auto request = makeLaunchRequest(host_path, {"--stay-alive-ms", std::to_string(250 * kIpcTimingMultiplier)});
        request.heartbeat_interval_ms = 250 * kIpcTimingMultiplier;

        const auto session = manager.prepareSession(request);
        an::ui::extensions::ExtensionHostRunningSession running_session;
        std::string error;
        QElapsedTimer timer;
        timer.start();
        ASSERT_TRUE(manager.launchSession(session, &running_session, &error)) << error;
        launch_durations_ms.push_back(timer.nsecsElapsed() / 1000000);

        SessionGuard guard{manager, running_session};
        QCoreApplication::processEvents(QEventLoop::AllEvents, 1);
    }

    ASSERT_EQ(launch_durations_ms.size(), static_cast<size_t>(kSampleCount));

    std::vector<qint64> sorted_durations = launch_durations_ms;
    std::sort(sorted_durations.begin(), sorted_durations.end());
    const qint64 min_ms = sorted_durations.front();
    const qint64 median_ms = sorted_durations[sorted_durations.size() / 2u];
    const qint64 p95_ms = sorted_durations[(sorted_durations.size() * 95u + 99u) / 100u - 1u];
    const qint64 max_ms = sorted_durations.back();

    std::cout << "[ PERF     ] Extension host spawn->handshake baseline (" << host_path.filename().string()
              << ", samples=" << kSampleCount << "): min=" << min_ms << " ms median=" << median_ms
              << " ms p95=" << p95_ms << " ms max=" << max_ms << " ms" << std::endl;

    EXPECT_GT(min_ms, 0);
}

// Measures how long a bundled extension takes from launch to a ready native session.
TEST(ExtensionHostManagerTest, PreliminaryEndToEndColdStartBaseline)
{
    const std::filesystem::path host_path = extensionHostPathForCurrentAppDir();
    const std::filesystem::path runtime_root = runtimeRootForCurrentAppDir();
    const std::filesystem::path bundled_python = repositoryBundledPythonPath();
    const std::filesystem::path extensions_root = findRepositoryBundledExtensionsRoot();
    AN_REQUIRE_FULL_EXTENSION_HOST(host_path);
    ASSERT_FALSE(runtime_root.empty()) << "Deployed PythonLib runtime root must be available";
    ASSERT_TRUE(std::filesystem::exists(runtime_root)) << runtime_root;
    ASSERT_FALSE(bundled_python.empty()) << "Repository bundled Python runtime must be available";
    ASSERT_TRUE(std::filesystem::exists(bundled_python)) << bundled_python;
    ASSERT_FALSE(extensions_root.empty()) << "Repository bundled extension roots must be available";

    const std::filesystem::path rt_preview_package_root = extensions_root / "an_rt_preview_demo";
    const std::filesystem::path custom_ui_package_root = extensions_root / "an_custom_ui_demo";
    ASSERT_TRUE(std::filesystem::exists(rt_preview_package_root / "extension.json")) << rt_preview_package_root;
    ASSERT_TRUE(std::filesystem::exists(custom_ui_package_root / "extension.json")) << custom_ui_package_root;

    constexpr int kSampleCount = 5;
    constexpr qint64 kColdStartBudgetMs = 500;

    const auto temp_root = uniqueExtensionRuntimeRootForTest("cold-start-baseline");
    removeAllNoThrow(temp_root);
    RemovePathOnExit temp_root_guard{temp_root};
    std::error_code ec;
    std::filesystem::create_directories(temp_root, ec);
    ASSERT_FALSE(static_cast<bool>(ec)) << "Failed to create temp benchmark root: " << ec.message();

    const std::filesystem::path env_root = temp_root / "env";
    createVirtualEnvironment(bundled_python, env_root);

    InMemorySettings settings_store;

    auto summarize_and_print = [kColdStartBudgetMs](const std::string_view label,
                                                    const std::vector<qint64>& durations_ms) {
        ASSERT_FALSE(durations_ms.empty());

        std::vector<qint64> sorted_durations = durations_ms;
        std::sort(sorted_durations.begin(), sorted_durations.end());
        const qint64 min_ms = sorted_durations.front();
        const qint64 median_ms = sorted_durations[sorted_durations.size() / 2u];
        const qint64 p95_ms = sorted_durations[(sorted_durations.size() * 95u + 99u) / 100u - 1u];
        const qint64 max_ms = sorted_durations.back();
        const double budget_fraction =
            (static_cast<double>(median_ms) * 100.0) / static_cast<double>(kColdStartBudgetMs);

        std::cout << "[ PERF     ] Extension end-to-end cold-start baseline (" << label
                  << ", samples=" << durations_ms.size() << "): min=" << min_ms << " ms median=" << median_ms
                  << " ms p95=" << p95_ms << " ms max=" << max_ms << " ms budget=" << budget_fraction << "% of "
                  << kColdStartBudgetMs << " ms" << std::endl;
    };

    auto measure_binding = [&](const std::string_view label,
                               const an::ui::extensions::ExtensionHostLaunchBinding& binding,
                               std::vector<qint64>* out_durations_ms) -> bool {
        if (!out_durations_ms)
        {
            ADD_FAILURE() << "Benchmark durations output is null for " << label;
            return false;
        }

        out_durations_ms->clear();
        out_durations_ms->reserve(kSampleCount);
        for (int sample_index = 0; sample_index < kSampleCount; ++sample_index)
        {
            an::tests::processes::framework::CaseLaunchCoordinator coordinator;
            coordinator.configureSdkService(&settings_store.repository());

            std::string error;
            QElapsedTimer timer;
            timer.start();
            if (!coordinator.launchBoundDescriptor(binding, fakeTargetControl(), &error))
            {
                ADD_FAILURE() << label << " sample " << sample_index << " failed to launch: " << error;
                coordinator.stopAllSessions();
                return false;
            }

            if (!pumpCoordinatorUntilNativeSessionReady(coordinator, binding.descriptor_id, 750 * kIpcTimingMultiplier))
            {
                ADD_FAILURE() << label << " sample " << sample_index << " did not reach native-session ready state";
                coordinator.stopAllSessions();
                return false;
            }

            out_durations_ms->push_back(timer.nsecsElapsed() / 1000000);
            if (!stopCoordinatorAndWaitForSessionAbsence(coordinator, 750 * kIpcTimingMultiplier))
            {
                ADD_FAILURE() << label << " sample " << sample_index << " did not tear down cleanly";
                return false;
            }
        }

        return true;
    };

    an::ui::extensions::ExtensionHostLaunchBinding rt_preview_binding;
    rt_preview_binding.descriptor_id = "filters.benchmark_rt_preview";
    rt_preview_binding.extension_id = "an.demo.rt_preview";
    rt_preview_binding.display_name = "RT Preview Demo";
    rt_preview_binding.host_executable = host_path;
    rt_preview_binding.capabilities = {"execute", "preview"};
    rt_preview_binding.process_capabilities.preview = true;
    rt_preview_binding.process_capabilities.ui = "auto";
    rt_preview_binding.host_arguments = {"--extension-package-root",
                                         rt_preview_package_root.string(),
                                         "--entry-point",
                                         "an_rt_preview_demo",
                                         "--process-class",
                                         "RTPreviewDemoExtension",
                                         "--environment-root",
                                         env_root.string(),
                                         "--runtime-root",
                                         runtime_root.string(),
                                         "--stay-alive-ms",
                                         std::to_string(2500 * kIpcTimingMultiplier)};

    an::ui::extensions::ExtensionHostLaunchBinding custom_ui_binding;
    custom_ui_binding.descriptor_id = "filters.benchmark_custom_ui";
    custom_ui_binding.extension_id = "an.demo.custom_ui";
    custom_ui_binding.display_name = "Custom UI Demo";
    custom_ui_binding.host_executable = host_path;
    custom_ui_binding.capabilities = {"execute"};
    custom_ui_binding.process_capabilities.ui = "custom";
    custom_ui_binding.host_arguments = {"--extension-package-root",
                                        custom_ui_package_root.string(),
                                        "--entry-point",
                                        "an_custom_ui_demo",
                                        "--process-class",
                                        "CustomUiDemoExtension",
                                        "--environment-root",
                                        env_root.string(),
                                        "--runtime-root",
                                        runtime_root.string(),
                                        "--stay-alive-ms",
                                        std::to_string(2500 * kIpcTimingMultiplier)};

    std::vector<qint64> rt_preview_durations_ms;
    ASSERT_TRUE(measure_binding("an_rt_preview_demo/native-window", rt_preview_binding, &rt_preview_durations_ms));
    summarize_and_print("an_rt_preview_demo/native-window", rt_preview_durations_ms);

    std::vector<qint64> custom_ui_durations_ms;
    ASSERT_TRUE(measure_binding("an_custom_ui_demo/native-window", custom_ui_binding, &custom_ui_durations_ms));
    summarize_and_print("an_custom_ui_demo/native-window", custom_ui_durations_ms);

    EXPECT_GT(*std::min_element(rt_preview_durations_ms.begin(), rt_preview_durations_ms.end()), 0);
    EXPECT_GT(*std::min_element(custom_ui_durations_ms.begin(), custom_ui_durations_ms.end()), 0);
}

// The bundled real-time preview demo commits the transform its parameters state.
TEST(ExtensionHostManagerTest, BundledRtPreviewDemoExecuteCommitsExpectedResult)
{
    const std::filesystem::path host_path = extensionHostPathForCurrentAppDir();
    const std::filesystem::path runtime_root = runtimeRootForCurrentAppDir();
    const std::filesystem::path bundled_python = repositoryBundledPythonPath();
    const std::filesystem::path extensions_root = findRepositoryBundledExtensionsRoot();
    AN_REQUIRE_FULL_EXTENSION_HOST(host_path);
    ASSERT_TRUE(std::filesystem::exists(runtime_root)) << runtime_root;
    ASSERT_FALSE(bundled_python.empty()) << "Repository bundled Python runtime must be available";
    ASSERT_FALSE(extensions_root.empty()) << "Repository bundled extension roots must be available";

    const std::filesystem::path package_root = extensions_root / "an_rt_preview_demo";
    ASSERT_TRUE(std::filesystem::exists(package_root / "extension.json")) << package_root;

    const auto temp_root = uniqueExtensionRuntimeRootForTest("bundled-rt-preview-demo-full");
    removeAllNoThrow(temp_root);
    const RemovePathOnExit remove_temp_root{temp_root};
    std::filesystem::create_directories(temp_root);

    const auto env_root = temp_root / "env";
    createVirtualEnvironment(bundled_python, env_root);

    const auto source_image = makeRgbGradientImage(5, 4);
    ASSERT_TRUE(source_image);

    const double strength = 0.45;
    const double lift = 0.0;
    const QString mode = QStringLiteral("balanced");
    const bool preserve_background = true;

    const auto root = uniqueSharedBufferRootForTest("bundled-rt-preview-demo-direct");
    removeAllNoThrow(root);
    const RemovePathOnExit remove_root{root};

    an::ui::extensions::ExtensionHostManager manager;
    an::ui::extensions::ExtensionHostLaunchRequest request;
    request.extension_id = "an.demo.rt_preview";
    request.sdk_backend = "runtime";
    request.host_executable = host_path;
    request.capabilities = {"execute", "preview"};
    request.host_arguments = {
        "--extension-package-root", package_root.string(), "--entry-point",
        "an_rt_preview_demo",       "--process-class",     "RTPreviewDemoExtension",
        "--environment-root",       env_root.string(),     "--runtime-root",
        runtime_root.string(),      "--stay-alive-ms",     std::to_string(4000 * kIpcTimingMultiplier)};

    std::string error;
    auto prepared = manager.prepareSession(request);
    an::ui::extensions::ExtensionHostRunningSession running_session;
    ASSERT_TRUE(manager.launchSession(prepared, &running_session, &error)) << error;
    SessionGuard session_guard{manager, running_session};
    EXPECT_TRUE(running_session.handshake.ack.native_process_window);

    auto source_buffer = makeImageBuffer(root, running_session.prepared_session.session_id, source_image,
                                         an::ui::extensions::SharedBufferRole::Source,
                                         an::ui::extensions::SharedBufferAccessMode::ReadWrite, "source", &error);
    ASSERT_TRUE(source_buffer.valid()) << error;

    std::string request_id;
    ASSERT_TRUE(manager.sendExecuteRequest(&running_session, "bundled-rt-preview-demo", {source_buffer.descriptor()},
                                           &request_id, &error))
        << error;
    ASSERT_FALSE(request_id.empty());
    ASSERT_TRUE(pumpUntilRequestCompletes(manager, &running_session, 3000 * kIpcTimingMultiplier, &error)) << error;

    EXPECT_FALSE(running_session.last_preview_result.has_value());
    ASSERT_TRUE(running_session.last_view_commit.has_value());
    ASSERT_EQ(running_session.last_view_commit->result_buffers.size(), 1u);

    an::ui::extensions::SharedBufferManager shared_buffer_manager(root);
    std::optional<an::consumer::ImageAsset> execute_image;
    ASSERT_TRUE(importBufferOrReport(shared_buffer_manager, running_session.last_view_commit->result_buffers.front(),
                                     &execute_image, &error))
        << error;
    ASSERT_TRUE(execute_image);

    const auto executeMatchesExpectedSamples = [&]() -> bool {
        if (!execute_image || widthOf(*execute_image) != widthOf(*source_image) ||
            heightOf(*execute_image) != heightOf(*source_image) ||
            channelsOf(*execute_image) != channelsOf(*source_image))
        {
            return false;
        }

        const std::array<std::tuple<size_t, size_t, size_t>, 4> sample_points = {
            {{0u, 0u, 0u}, {2u, 1u, 1u}, {4u, 3u, 2u}, {1u, 2u, 0u}}};
        for (const auto& [x, y, channel] : sample_points)
        {
            const double source_value = sampleAt(*source_image, x, y, channel);
            const double expected_value =
                expectedRtPreviewDemoValue(source_value, strength, lift, mode, preserve_background);
            if (std::fabs(sampleAt(*execute_image, x, y, channel) - expected_value) > 1e-6)
            {
                return false;
            }
        }

        return true;
    };
    EXPECT_TRUE(executeMatchesExpectedSamples());
    removeAllNoThrow(root);
    removeAllNoThrow(temp_root);
}

// The bundled custom-interface demo commits the transform its default parameters state.
TEST(ExtensionHostManagerTest, BundledCustomUiDemoExecuteCommitsExpectedResult)
{
    const std::filesystem::path host_path = extensionHostPathForCurrentAppDir();
    const std::filesystem::path runtime_root = runtimeRootForCurrentAppDir();
    const std::filesystem::path bundled_python = repositoryBundledPythonPath();
    const std::filesystem::path extensions_root = findRepositoryBundledExtensionsRoot();
    AN_REQUIRE_FULL_EXTENSION_HOST(host_path);
    ASSERT_TRUE(std::filesystem::exists(runtime_root)) << runtime_root;
    ASSERT_FALSE(bundled_python.empty()) << "Repository bundled Python runtime must be available";
    ASSERT_FALSE(extensions_root.empty()) << "Repository bundled extension roots must be available";

    const std::filesystem::path package_root = extensions_root / "an_custom_ui_demo";
    ASSERT_TRUE(std::filesystem::exists(package_root / "extension.json")) << package_root;

    const auto temp_root = uniqueExtensionRuntimeRootForTest("bundled-custom-ui-demo");
    removeAllNoThrow(temp_root);
    const RemovePathOnExit remove_temp_root{temp_root};
    std::filesystem::create_directories(temp_root);

    const auto env_root = temp_root / "env";
    createVirtualEnvironment(bundled_python, env_root);

    const auto source_image = makeRgbGradientImage(5, 4);
    ASSERT_TRUE(source_image);

    const auto root = uniqueSharedBufferRootForTest("bundled-custom-ui-demo-direct");
    removeAllNoThrow(root);
    const RemovePathOnExit remove_root{root};

    an::ui::extensions::ExtensionHostManager manager;
    an::ui::extensions::ExtensionHostLaunchRequest request;
    request.extension_id = "an.demo.custom_ui";
    request.sdk_backend = "runtime";
    request.host_executable = host_path;
    request.capabilities = {"execute"};
    request.host_arguments = {
        "--extension-package-root", package_root.string(), "--entry-point",
        "an_custom_ui_demo",        "--process-class",     "CustomUiDemoExtension",
        "--environment-root",       env_root.string(),     "--runtime-root",
        runtime_root.string(),      "--stay-alive-ms",     std::to_string(4000 * kIpcTimingMultiplier)};

    std::string error;
    auto prepared = manager.prepareSession(request);
    an::ui::extensions::ExtensionHostRunningSession running_session;
    ASSERT_TRUE(manager.launchSession(prepared, &running_session, &error)) << error;
    SessionGuard session_guard{manager, running_session};
    EXPECT_TRUE(running_session.handshake.ack.native_process_window);

    auto source_buffer = makeImageBuffer(root, running_session.prepared_session.session_id, source_image,
                                         an::ui::extensions::SharedBufferRole::Source,
                                         an::ui::extensions::SharedBufferAccessMode::ReadWrite, "source", &error);
    ASSERT_TRUE(source_buffer.valid()) << error;

    std::string request_id;
    ASSERT_TRUE(manager.sendExecuteRequest(&running_session, "bundled-custom-ui-demo", {source_buffer.descriptor()},
                                           &request_id, &error))
        << error;
    ASSERT_FALSE(request_id.empty());
    ASSERT_TRUE(pumpUntilRequestCompletes(manager, &running_session, 3000 * kIpcTimingMultiplier, &error)) << error;

    EXPECT_FALSE(running_session.last_preview_result.has_value());
    ASSERT_TRUE(running_session.last_view_commit.has_value());
    ASSERT_EQ(running_session.last_view_commit->result_buffers.size(), 1u);

    an::ui::extensions::SharedBufferManager shared_buffer_manager(root);
    std::optional<an::consumer::ImageAsset> execute_image;
    ASSERT_TRUE(importBufferOrReport(shared_buffer_manager, running_session.last_view_commit->result_buffers.front(),
                                     &execute_image, &error))
        << error;
    ASSERT_TRUE(execute_image);

    const auto executeMatchesExpectedSamples = [&]() -> bool {
        if (!execute_image || widthOf(*execute_image) != widthOf(*source_image) ||
            heightOf(*execute_image) != heightOf(*source_image) ||
            channelsOf(*execute_image) != channelsOf(*source_image))
        {
            return false;
        }

        const std::array<std::tuple<size_t, size_t, size_t>, 4> sample_points = {
            {{0u, 0u, 0u}, {2u, 1u, 1u}, {4u, 3u, 2u}, {1u, 2u, 0u}}};
        for (const auto& [x, y, channel] : sample_points)
        {
            const double source_value = sampleAt(*source_image, x, y, channel);
            const double expected_value = expectedCustomUiDemoValue(source_value, 0.5, 1.2, 4, QStringLiteral("fast"));
            if (std::fabs(sampleAt(*execute_image, x, y, channel) - expected_value) > 1e-6)
            {
                return false;
            }
        }

        return true;
    };
    EXPECT_TRUE(executeMatchesExpectedSamples());
    removeAllNoThrow(root);
    removeAllNoThrow(temp_root);
}

// ---- The author templates of docs/extension-examples, each run on the host its backend names.

namespace
{

// What an author template is run for: a template whose process executes is sent an execute and commits a result of its
// source's shape; the protocol template, which owns its interface, runs its launch on the Lite host with no engine.
struct AuthorTemplateRun
{
    std::string directory;
    bool executes = true;
};

// Runs one author template on the host its backend names, as the application launches it.
void runAuthorTemplate(const AuthorTemplateRun& run)
{
    const std::filesystem::path host_path = extensionHostPathForCurrentAppDir();
    const std::filesystem::path lite_host_path = extensionHostLitePathForCurrentAppDir();
    const std::filesystem::path runtime_root = runtimeRootForCurrentAppDir();
    AN_REQUIRE_FULL_EXTENSION_HOST(host_path);
    ASSERT_FALSE(lite_host_path.empty()) << "AfterNightExtensionHostLite must be built before this test runs";
    ASSERT_TRUE(std::filesystem::exists(runtime_root)) << runtime_root;
    const std::filesystem::path templates_root = authorTemplatesRoot();
    ASSERT_TRUE(std::filesystem::exists(templates_root / run.directory / "extension.json")) << templates_root;

    std::string error;
    const std::vector<an::ui::extensions::ExtensionLoadResult> discovered =
        discoverAuthorTemplates(host_path, runtime_root, &error);
    ASSERT_TRUE(error.empty()) << error;
    const auto found = std::find_if(discovered.begin(), discovered.end(), [&run](const auto& result) {
        return result.manifest.has_value() &&
               std::filesystem::path(result.manifest->package_root).filename() == std::filesystem::path(run.directory);
    });
    ASSERT_NE(found, discovered.end()) << run.directory << " was not discovered";
    ASSERT_EQ(found->status, an::ui::extensions::ExtensionLoadStatus::Ready) << found->diagnostic;
    ASSERT_TRUE(found->environment.has_value());
    ASSERT_EQ(found->environment->state, an::ui::extensions::ExtensionEnvironmentState::Ready);
    const an::ui::extensions::ExtensionManifest& manifest = *found->manifest;
    ASSERT_FALSE(manifest.processes.empty());
    const bool protocol = manifest.sdk_backend == an::ui::extensions::ExtensionSdkBackend::Protocol;

    if (!run.executes)
    {
        // The template owns its interface: launched on the Lite host as the application launches it, its launch hook
        // runs with no engine behind it and logs the session it was given, which reaches the application's log.
        ASSERT_TRUE(protocol);
        InMemorySettings settings_store;
        an::ui::extensions::ExtensionSdkService sdk_service(&settings_store.repository());
        an::ui::extensions::ExtensionHostManager manager;
        manager.setSdkService(&sdk_service);
        an::ui::extensions::ExtensionHostLaunchRequest request;
        request.extension_id = manifest.id;
        request.sdk_backend = "protocol";
        request.host_executable = lite_host_path;
        request.capabilities = {"execute"};
        request.host_arguments = {"--extension-package-root",
                                  manifest.package_root,
                                  "--entry-point",
                                  manifest.entry_point,
                                  "--process-class",
                                  manifest.processes.front().class_name,
                                  "--environment-root",
                                  found->environment->environment_path.string(),
                                  "--runtime-root",
                                  runtime_root.string(),
                                  "--stay-alive-ms",
                                  std::to_string(4000 * kIpcTimingMultiplier)};
        const auto prepared = manager.prepareSession(request);
        an::ui::extensions::ExtensionHostRunningSession running_session;
        ASSERT_TRUE(manager.launchSession(prepared, &running_session, &error)) << error;
        SessionGuard session_guard{manager, running_session};
        const std::string launched = "Lite protocol launch session=" + running_session.prepared_session.session_id;
        QElapsedTimer timer;
        timer.start();
        bool logged = false;
        while (!logged && timer.elapsed() < 8000 * kIpcTimingMultiplier)
        {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 1);
            if (!manager.pumpSession(&running_session, 20, &error))
            {
                break;
            }
            logged = mainLogContains(launched);
        }
        EXPECT_TRUE(logged) << "the template's launch never logged its session: " << error;
        return;
    }

    an::ui::extensions::ExtensionHostManager manager;
    an::ui::extensions::ExtensionHostLaunchRequest request;
    request.extension_id = manifest.id;
    request.sdk_backend = protocol ? "protocol" : "runtime";
    request.host_executable = protocol ? lite_host_path : host_path;
    request.capabilities = {"execute"};
    request.host_arguments = {"--extension-package-root",
                              manifest.package_root,
                              "--entry-point",
                              manifest.entry_point,
                              "--process-class",
                              manifest.processes.front().class_name,
                              "--environment-root",
                              found->environment->environment_path.string(),
                              "--runtime-root",
                              runtime_root.string(),
                              "--stay-alive-ms",
                              std::to_string(8000 * kIpcTimingMultiplier)};

    const auto prepared = manager.prepareSession(request);
    an::ui::extensions::ExtensionHostRunningSession running_session;
    ASSERT_TRUE(manager.launchSession(prepared, &running_session, &error)) << error;
    SessionGuard session_guard{manager, running_session};

    const auto root = uniqueSharedBufferRootForTest("author-template-" + run.directory);
    removeAllNoThrow(root);
    const RemovePathOnExit remove_root{root};
    const auto source_image = makeRgbGradientImage(6, 4);
    ASSERT_TRUE(source_image);
    auto source_buffer = makeImageBuffer(root, running_session.prepared_session.session_id, source_image,
                                         an::ui::extensions::SharedBufferRole::Source,
                                         an::ui::extensions::SharedBufferAccessMode::ReadWrite, "source", &error);
    ASSERT_TRUE(source_buffer.valid()) << error;

    std::string request_id;
    ASSERT_TRUE(manager.sendExecuteRequest(&running_session, "author-template-" + run.directory,
                                           {source_buffer.descriptor()}, &request_id, &error))
        << error;
    ASSERT_FALSE(request_id.empty());
    ASSERT_TRUE(pumpUntilRequestCompletes(manager, &running_session, 8000 * kIpcTimingMultiplier, &error)) << error;

    ASSERT_TRUE(running_session.last_view_commit.has_value());
    ASSERT_FALSE(running_session.last_view_commit->result_buffers.empty());
    an::ui::extensions::SharedBufferManager shared_buffer_manager(root);
    std::optional<an::consumer::ImageAsset> result;
    ASSERT_TRUE(importBufferOrReport(shared_buffer_manager, running_session.last_view_commit->result_buffers.front(),
                                     &result, &error))
        << error;
    ASSERT_TRUE(result);
    EXPECT_EQ(widthOf(*result), widthOf(*source_image));
    EXPECT_EQ(heightOf(*result), heightOf(*source_image));
    EXPECT_EQ(channelsOf(*result), channelsOf(*source_image));
}

} // namespace

TEST(AuthorTemplateExecution, TheBasicProcessTemplateExecutesOnTheFullHost)
{
    runAuthorTemplate({.directory = "basic_process"});
}

TEST(AuthorTemplateExecution, TheAuxiliaryBuffersTemplateExecutesOnTheFullHost)
{
    runAuthorTemplate({.directory = "auxiliary_buffers_process"});
}

TEST(AuthorTemplateExecution, TheLockedWheelhouseTemplateExecutesOnTheFullHostFromItsInstalledWheelhouse)
{
    runAuthorTemplate({.directory = "locked_wheelhouse_process"});
}

TEST(AuthorTemplateExecution, TheRealTimePreviewTemplateExecutesOnTheFullHost)
{
    runAuthorTemplate({.directory = "rt_preview_process"});
}

TEST(AuthorTemplateExecution, TheSettingsDiagnosticsTemplateExecutesOnTheFullHost)
{
    runAuthorTemplate({.directory = "settings_diagnostics_process"});
}

TEST(AuthorTemplateExecution, TheStandalonePreviewTemplateExecutesOnTheFullHost)
{
    runAuthorTemplate({.directory = "standalone_preview_process"});
}

TEST(AuthorTemplateExecution, TheLiteProtocolTemplateLaunchesOnTheLiteHostWithNoEngine)
{
    runAuthorTemplate({.directory = "lite_protocol_process", .executes = false});
}

// Inspecting the echo demo reports its settings and its compatibility banner, with events and validation off.
TEST(ExtensionHostManagerTest, HostInspectBundledEchoDemoKeepsSettingsAndCompatibilityBanner)
{
    const std::filesystem::path host_path = extensionHostPathForCurrentAppDir();
    const std::filesystem::path runtime_root = runtimeRootForCurrentAppDir();
    const std::filesystem::path bundled_python = repositoryBundledPythonPath();
    const std::filesystem::path extensions_root = findRepositoryBundledExtensionsRoot();
    AN_REQUIRE_FULL_EXTENSION_HOST(host_path);
    ASSERT_TRUE(std::filesystem::exists(runtime_root)) << runtime_root;
    ASSERT_FALSE(bundled_python.empty()) << "Repository bundled Python runtime must be available";
    ASSERT_FALSE(extensions_root.empty()) << "Repository bundled extension roots must be available";

    const std::filesystem::path package_root = extensions_root / "an_echo_demo";
    ASSERT_TRUE(std::filesystem::exists(package_root / "extension.json")) << package_root;

    const auto temp_root = uniqueExtensionRuntimeRootForTest("inspect-bundled-echo-compat");
    removeAllNoThrow(temp_root);
    const RemovePathOnExit remove_temp_root{temp_root};
    std::filesystem::create_directories(temp_root);

    const auto env_root = temp_root / "env";
    createVirtualEnvironment(bundled_python, env_root);

    QProcess inspect;
    inspect.setProgram(QString::fromStdString(host_path.string()));
    inspect.setProcessChannelMode(QProcess::MergedChannels);
    inspect.setArguments(
        {QStringLiteral("--inspect"), QStringLiteral("--inspect-settings"), QStringLiteral("--inspect-params"),
         QStringLiteral("--sdk-backend"), QStringLiteral("runtime"), QStringLiteral("--extension-package-root"),
         QString::fromStdString(package_root.string()), QStringLiteral("--entry-point"), QStringLiteral("an_echo_demo"),
         QStringLiteral("--process-class"), QStringLiteral("EchoDemoExtension"), QStringLiteral("--environment-root"),
         QString::fromStdString(env_root.string()), QStringLiteral("--runtime-root"),
         QString::fromStdString(runtime_root.string())});
    inspect.start();
    ASSERT_TRUE(inspect.waitForStarted());
    ASSERT_TRUE(inspect.waitForFinished(hostProcessWaitMs()));

    const std::string output = inspect.readAllStandardOutput().toStdString();
    EXPECT_EQ(inspect.exitStatus(), QProcess::NormalExit) << output;
    EXPECT_EQ(inspect.exitCode(), 0) << output;

    QJsonParseError parse_error;
    const QJsonDocument document = QJsonDocument::fromJson(QByteArray::fromStdString(output).trimmed(), &parse_error);
    ASSERT_EQ(parse_error.error, QJsonParseError::NoError) << output;
    ASSERT_TRUE(document.isObject()) << output;

    const QJsonArray settings_schema = document.object().value(QStringLiteral("settings_schema")).toArray();
    ASSERT_EQ(settings_schema.size(), 1) << output;
    EXPECT_EQ(settings_schema.at(0).toObject().value(QStringLiteral("id")).toString(), QStringLiteral("label"));
    const QJsonArray params_schema = document.object().value(QStringLiteral("params_schema")).toArray();
    ASSERT_EQ(params_schema.size(), 2) << output;
    EXPECT_EQ(params_schema.at(0).toObject().value(QStringLiteral("id")).toString(),
              QStringLiteral("compatibility_banner"));
    EXPECT_EQ(params_schema.at(0).toObject().value(QStringLiteral("type")).toString(), QStringLiteral("section"));
    EXPECT_EQ(params_schema.at(1).toObject().value(QStringLiteral("id")).toString(),
              QStringLiteral("compatibility_summary"));
    EXPECT_EQ(params_schema.at(1).toObject().value(QStringLiteral("type")).toString(), QStringLiteral("info"));
    EXPECT_FALSE(document.object().value(QStringLiteral("supports_ui_events")).toBool()) << output;
    EXPECT_FALSE(document.object().value(QStringLiteral("supports_ui_validation")).toBool()) << output;

    removeAllNoThrow(temp_root);
}

// The echo demo launches against a view as the application presents one and stays active.
TEST(ExtensionHostManagerTest, BundledEchoDemoLaunchesWithAppLikeViewAndRemainsActive)
{
    const std::filesystem::path host_path = extensionHostPathForCurrentAppDir();
    const std::filesystem::path runtime_root = runtimeRootForCurrentAppDir();
    const std::filesystem::path app_dir(QCoreApplication::applicationDirPath().toStdString());
    const std::filesystem::path bundled_root = an::ui::extensions::bundled_extensions_root(app_dir);
    AN_REQUIRE_FULL_EXTENSION_HOST(host_path);
    ASSERT_TRUE(std::filesystem::exists(runtime_root)) << runtime_root;
    ASSERT_TRUE(std::filesystem::exists(bundled_root / "an_echo_demo" / "extension.json")) << bundled_root;

    an::ui::extensions::ExtensionDiscoveryOptions options;
    options.bundled_roots = {bundled_root};
    options.context.runtime_target = an::ui::extensions::current_runtime_target();
    options.context.runtime_root = runtime_root;
    options.context.bundled_python_executable = bundledPythonPathForCurrentAppDir();
    options.context.extension_host_executable = host_path;
    options.include_development_only_extensions = true;
    options.context.environments_root = shortExtensionEnvironmentsRoot();

    an::ui::extensions::ExtensionManager extension_manager;
    auto discovered = extension_manager.discoverExtensions(options);
    ASSERT_FALSE(discovered.empty());

    an::ui::extensions::ExtensionEnvironmentManager environment_manager;
    std::string bootstrap_error;
    const int queued = environment_manager.queueBundledBootstrap(discovered, &bootstrap_error);
    ASSERT_TRUE(bootstrap_error.empty()) << bootstrap_error;
    if (queued > 0)
    {
        ASSERT_EQ(environment_manager.processQueuedBundledBootstrap(discovered, &bootstrap_error), queued)
            << bootstrap_error;
    }

    const auto echo_it = std::find_if(discovered.begin(), discovered.end(), [](const auto& result) {
        return result.manifest.has_value() && result.manifest->id == "an.demo.echo";
    });
    ASSERT_NE(echo_it, discovered.end()) << "Echo demo was not discovered";
    ASSERT_TRUE(echo_it->manifest.has_value());
    ASSERT_TRUE(echo_it->environment.has_value());
    ASSERT_EQ(echo_it->status, an::ui::extensions::ExtensionLoadStatus::Ready);
    ASSERT_EQ(echo_it->environment->state, an::ui::extensions::ExtensionEnvironmentState::Ready);

    const auto source_image = makeRgbGradientImage(16, 12);
    ASSERT_TRUE(source_image);

    auto& runtime_status_registry = an::ui::extensions::ExtensionRuntimeStatusRegistry::instance();
    runtime_status_registry.clear();

    std::string current_view_name = "echo-active-view";
    std::string current_file_name = "/tmp/echo-active-view.fit";

    an::tests::processes::ViewHandle target = targetViewOf(current_view_name, source_image, current_file_name);
    ASSERT_NE(target, nullptr);

    an::tests::processes::framework::CaseLaunchCoordinator coordinator;
    coordinator.configureSdkService(
        nullptr,
        [&]() -> std::vector<an::ui::extensions::ExtensionViewState> {
            an::ui::extensions::ExtensionViewState view_state;
            view_state.view_name = current_view_name;
            view_state.file_name = current_file_name;
            view_state.view_mode = "standard";
            view_state.channel_view_mode = "rgb";
            view_state.zoom_percent = 150.0;
            view_state.viewport_offset_x = 8;
            view_state.viewport_offset_y = 13;
            view_state.is_active = true;
            view_state.has_image = true;
            view_state.width = static_cast<int>(widthOf(*source_image));
            view_state.height = static_cast<int>(heightOf(*source_image));
            view_state.channels = static_cast<int>(channelsOf(*source_image));
            return {view_state};
        },
        [&](const std::string_view session_id, const std::string_view request_id, const std::string_view view_name,
            an::ui::extensions::ExtensionViewSnapshot* out_snapshot, std::string* error) -> bool {
            if (!out_snapshot)
            {
                if (error)
                {
                    *error = "Extension SDK snapshot output is null";
                }
                return false;
            }
            EXPECT_EQ(view_name, current_view_name);

            *out_snapshot = an::ui::extensions::ExtensionViewSnapshot{};
            an::ui::extensions::SharedBufferManager shared_buffer_manager;
            an::ui::extensions::SharedImageBuffer buffer;
            if (!exportImageOrReport(shared_buffer_manager, *source_image, std::string(session_id),
                                     an::ui::extensions::SharedBufferRole::Preview, &buffer, error,
                                     std::string(request_id) + ".echo_app_like_snapshot",
                                     std::string(view_name) + ".snapshot",
                                     an::ui::extensions::SharedBufferAccessMode::ReadOnly))
            {
                return false;
            }

            out_snapshot->source_buffer = buffer.descriptor();
            return true;
        },
        []() -> an::ui::extensions::ExtensionThemeSnapshot {
            an::ui::extensions::ExtensionThemeSnapshot snapshot;
            snapshot.theme_name = "AfterNight Dark";
            snapshot.tokens = QCborMap{{QStringLiteral("is_dark"), true},
                                       {QStringLiteral("spacing"), QCborMap{{QStringLiteral("md"), 12}}}};
            return snapshot;
        });
    coordinator.setManagedImageHooks(
        [](ImageViewControl*, an::consumer::ImageAsset&&, const std::string&, const std::string&) {});

    an::ui::extensions::ExtensionHostLaunchBinding binding;
    binding.descriptor_id = "filters.echo_app_like";
    binding.extension_id = echo_it->manifest->id;
    binding.display_name = "Echo Demo";
    binding.host_executable = host_path;
    binding.capabilities = {"execute"};
    binding.process_capabilities = echo_it->manifest->processes.front().capabilities;
    binding.host_arguments = {"--extension-package-root",
                              echo_it->manifest->package_root,
                              "--entry-point",
                              echo_it->manifest->entry_point,
                              "--process-class",
                              echo_it->manifest->processes.front().class_name,
                              "--environment-root",
                              echo_it->environment->environment_path.string(),
                              "--runtime-root",
                              runtime_root.string(),
                              "--stay-alive-ms",
                              std::to_string(5000 * kIpcTimingMultiplier)};

    std::string error;
    ASSERT_TRUE(coordinator.launchBoundDescriptor(binding, target.get(), &error)) << error;
    ASSERT_TRUE(
        pumpCoordinatorUntilNativeSessionReady(coordinator, binding.descriptor_id, 1500 * kIpcTimingMultiplier));

    ASSERT_TRUE(pumpCoordinatorUntil(coordinator, 1500 * kIpcTimingMultiplier, [&coordinator, &binding, &target]() {
        return coordinator.hasActiveSessionForDescriptor(binding.descriptor_id, target.get());
    }));

    const auto snapshot = runtime_status_registry.snapshotForExtension(binding.extension_id);
    ASSERT_TRUE(!snapshot.has_value() || !snapshot->has_termination_result)
        << (snapshot.has_value() ? snapshot->termination_detail : std::string("unexpected termination"));

    ASSERT_TRUE(stopCoordinatorAndWaitForSessionAbsence(coordinator, 750 * kIpcTimingMultiplier));
}

// The echo demo launches against a grey view whose channel mode it does not support and stays active.
TEST(ExtensionHostManagerTest, BundledEchoDemoLaunchesWithMonoSourceAndUnsupportedChannelMode)
{
    const std::filesystem::path host_path = extensionHostPathForCurrentAppDir();
    const std::filesystem::path runtime_root = runtimeRootForCurrentAppDir();
    const std::filesystem::path app_dir(QCoreApplication::applicationDirPath().toStdString());
    const std::filesystem::path bundled_root = an::ui::extensions::bundled_extensions_root(app_dir);
    AN_REQUIRE_FULL_EXTENSION_HOST(host_path);
    ASSERT_TRUE(std::filesystem::exists(runtime_root)) << runtime_root;
    ASSERT_TRUE(std::filesystem::exists(bundled_root / "an_echo_demo" / "extension.json")) << bundled_root;

    an::ui::extensions::ExtensionDiscoveryOptions options;
    options.bundled_roots = {bundled_root};
    options.context.runtime_target = an::ui::extensions::current_runtime_target();
    options.context.runtime_root = runtime_root;
    options.context.bundled_python_executable = bundledPythonPathForCurrentAppDir();
    options.context.extension_host_executable = host_path;
    options.include_development_only_extensions = true;
    options.context.environments_root = shortExtensionEnvironmentsRoot();

    an::ui::extensions::ExtensionManager extension_manager;
    auto discovered = extension_manager.discoverExtensions(options);
    ASSERT_FALSE(discovered.empty());

    an::ui::extensions::ExtensionEnvironmentManager environment_manager;
    std::string bootstrap_error;
    const int queued = environment_manager.queueBundledBootstrap(discovered, &bootstrap_error);
    ASSERT_TRUE(bootstrap_error.empty()) << bootstrap_error;
    if (queued > 0)
    {
        ASSERT_EQ(environment_manager.processQueuedBundledBootstrap(discovered, &bootstrap_error), queued)
            << bootstrap_error;
    }

    const auto echo_it = std::find_if(discovered.begin(), discovered.end(), [](const auto& result) {
        return result.manifest.has_value() && result.manifest->id == "an.demo.echo";
    });
    ASSERT_NE(echo_it, discovered.end()) << "Echo demo was not discovered";
    ASSERT_TRUE(echo_it->manifest.has_value());
    ASSERT_TRUE(echo_it->environment.has_value());
    ASSERT_EQ(echo_it->status, an::ui::extensions::ExtensionLoadStatus::Ready);
    ASSERT_EQ(echo_it->environment->state, an::ui::extensions::ExtensionEnvironmentState::Ready);

    const auto source_image = makeGreyFloatImage(16, 12, 0.35);
    ASSERT_TRUE(source_image);

    auto& runtime_status_registry = an::ui::extensions::ExtensionRuntimeStatusRegistry::instance();
    runtime_status_registry.clear();

    std::string current_view_name = "echo-mono-view";
    std::string current_file_name = "/tmp/echo-mono-view.fit";

    an::tests::processes::ViewHandle target = targetViewOf(current_view_name, source_image, current_file_name);
    ASSERT_NE(target, nullptr);

    an::tests::processes::framework::CaseLaunchCoordinator coordinator;
    coordinator.configureSdkService(
        nullptr,
        [&]() -> std::vector<an::ui::extensions::ExtensionViewState> {
            an::ui::extensions::ExtensionViewState view_state;
            view_state.view_name = current_view_name;
            view_state.file_name = current_file_name;
            view_state.view_mode = "standard";
            view_state.channel_view_mode = "rgb_g";
            view_state.zoom_percent = 110.0;
            view_state.viewport_offset_x = 4;
            view_state.viewport_offset_y = 7;
            view_state.is_active = true;
            view_state.has_image = true;
            view_state.width = static_cast<int>(widthOf(*source_image));
            view_state.height = static_cast<int>(heightOf(*source_image));
            view_state.channels = static_cast<int>(channelsOf(*source_image));
            return {view_state};
        },
        [&](const std::string_view session_id, const std::string_view request_id, const std::string_view view_name,
            an::ui::extensions::ExtensionViewSnapshot* out_snapshot, std::string* error) -> bool {
            if (!out_snapshot)
            {
                if (error)
                {
                    *error = "Extension SDK snapshot output is null";
                }
                return false;
            }
            EXPECT_EQ(view_name, current_view_name);

            *out_snapshot = an::ui::extensions::ExtensionViewSnapshot{};
            an::ui::extensions::SharedBufferManager shared_buffer_manager;
            an::ui::extensions::SharedImageBuffer buffer;
            if (!exportImageOrReport(shared_buffer_manager, *source_image, std::string(session_id),
                                     an::ui::extensions::SharedBufferRole::Preview, &buffer, error,
                                     std::string(request_id) + ".echo_mono_snapshot",
                                     std::string(view_name) + ".snapshot",
                                     an::ui::extensions::SharedBufferAccessMode::ReadOnly))
            {
                return false;
            }

            out_snapshot->source_buffer = buffer.descriptor();
            return true;
        },
        []() -> an::ui::extensions::ExtensionThemeSnapshot {
            an::ui::extensions::ExtensionThemeSnapshot snapshot;
            snapshot.theme_name = "AfterNight Dark";
            snapshot.tokens = QCborMap{{QStringLiteral("is_dark"), true},
                                       {QStringLiteral("spacing"), QCborMap{{QStringLiteral("md"), 12}}}};
            return snapshot;
        });
    coordinator.setManagedImageHooks(
        [](ImageViewControl*, an::consumer::ImageAsset&&, const std::string&, const std::string&) {});

    an::ui::extensions::ExtensionHostLaunchBinding binding;
    binding.descriptor_id = "filters.echo_mono_view";
    binding.extension_id = echo_it->manifest->id;
    binding.display_name = "Echo Demo";
    binding.host_executable = host_path;
    binding.capabilities = {"execute"};
    binding.process_capabilities = echo_it->manifest->processes.front().capabilities;
    binding.host_arguments = {"--extension-package-root",
                              echo_it->manifest->package_root,
                              "--entry-point",
                              echo_it->manifest->entry_point,
                              "--process-class",
                              echo_it->manifest->processes.front().class_name,
                              "--environment-root",
                              echo_it->environment->environment_path.string(),
                              "--runtime-root",
                              runtime_root.string(),
                              "--stay-alive-ms",
                              std::to_string(5000 * kIpcTimingMultiplier)};

    std::string error;
    ASSERT_TRUE(coordinator.launchBoundDescriptor(binding, target.get(), &error)) << error;
    ASSERT_TRUE(
        pumpCoordinatorUntilNativeSessionReady(coordinator, binding.descriptor_id, 1500 * kIpcTimingMultiplier));

    ASSERT_TRUE(pumpCoordinatorUntil(coordinator, 1500 * kIpcTimingMultiplier, [&coordinator, &binding, &target]() {
        return coordinator.hasActiveSessionForDescriptor(binding.descriptor_id, target.get());
    }));

    const auto snapshot = runtime_status_registry.snapshotForExtension(binding.extension_id);
    ASSERT_TRUE(!snapshot.has_value() || !snapshot->has_termination_result)
        << (snapshot.has_value() ? snapshot->termination_detail : std::string("unexpected termination"));

    ASSERT_TRUE(stopCoordinatorAndWaitForSessionAbsence(coordinator, 750 * kIpcTimingMultiplier));
}

// The echo demo inverts a sixteen-bit image over its full range rather than clamping black.
TEST(ExtensionHostManagerTest, BundledEchoDemoExecuteInvertsUInt16SourceWithoutClampingBlack)
{
    const std::filesystem::path bundled_python = repositoryBundledPythonPath();
    const std::filesystem::path extensions_root = findRepositoryBundledExtensionsRoot();
    ASSERT_FALSE(bundled_python.empty()) << "Repository bundled Python runtime must be available";
    ASSERT_FALSE(extensions_root.empty()) << "Repository bundled extension roots must be available";

    const std::filesystem::path package_root = extensions_root / "an_echo_demo";
    ASSERT_TRUE(std::filesystem::exists(package_root / "extension.json")) << package_root;

    const auto temp_root = uniqueExtensionRuntimeRootForTest("bundled-echo-demo-uint16-script");
    removeAllNoThrow(temp_root);
    const RemovePathOnExit remove_temp_root{temp_root};
    std::filesystem::create_directories(temp_root);

    const auto script_path = temp_root / "echo_demo_uint16_regression.py";
    writeTextFile(script_path, "import importlib.util\n"
                               "import json\n"
                               "import pathlib\n"
                               "import sys\n"
                               "import types\n\n"
                               "extension_path = pathlib.Path(r'" +
                                   (package_root / "an_echo_demo.py").string() +
                                   "')\n"
                                   "calls = []\n\n"
                                   "afternight = types.ModuleType('afternight')\n"
                                   "ui = types.ModuleType('afternight.ui')\n\n"
                                   "class _Session:\n"
                                   "    session_id = 'test-session'\n"
                                   "    extension_id = 'an.demo.echo'\n\n"
                                   "class _ProcessWindow:\n"
                                   "    def __init__(self, context):\n"
                                   "        self.context = context\n"
                                   "        self.settings = dict(getattr(context, 'settings', {}) or {})\n\n"
                                   "class _Core:\n"
                                   "    @staticmethod\n"
                                   "    def apply_linear_transform(src_image, dst_image, scale, offset):\n"
                                   "        calls.append({'scale': scale, 'offset': offset})\n\n"
                                   "afternight.current_session = lambda: _Session()\n"
                                   "afternight.log_info = lambda *args, **kwargs: None\n"
                                   "afternight.core = _Core\n"
                                   "afternight.ui = ui\n"
                                   "ui.ProcessWindow = _ProcessWindow\n"
                                   "sys.modules['afternight'] = afternight\n"
                                   "sys.modules['afternight.ui'] = ui\n\n"
                                   "spec = importlib.util.spec_from_file_location('an_echo_demo', extension_path)\n"
                                   "module = importlib.util.module_from_spec(spec)\n"
                                   "spec.loader.exec_module(module)\n\n"
                                   "class _Context:\n"
                                   "    settings = {'label': 'Echo Demo'}\n\n"
                                   "class _Target:\n"
                                   "    view_name = 'echo-uint16-view'\n"
                                   "    file_name = '/tmp/echo-uint16-view.fit'\n"
                                   "    width = 7\n"
                                   "    height = 5\n"
                                   "    channels = 1\n"
                                   "    pixel_type = '16 bits (unsigned integer)'\n\n"
                                   "class _Progress:\n"
                                   "    def __init__(self):\n"
                                   "        self.text = ''\n"
                                   "    def set_text(self, value):\n"
                                   "        self.text = value\n\n"
                                   "class _Image:\n"
                                   "    def __init__(self):\n"
                                   "        self.metadata = {}\n"
                                   "    def set_metadata(self, key, value):\n"
                                   "        self.metadata[key] = value\n\n"
                                   "dst = _Image()\n"
                                   "progress = _Progress()\n"
                                   "extension = module.EchoDemoExtension(_Context())\n"
                                   "extension.execute(_Target(), object(), dst, {}, progress)\n"
                                   "print(json.dumps({\n"
                                   "    'offset': calls[-1]['offset'],\n"
                                   "    'scale': calls[-1]['scale'],\n"
                                   "    'progress_text': progress.text,\n"
                                   "    'pixel_type': dst.metadata.get('an.demo.echo.pixel_type'),\n"
                                   "    'label': dst.metadata.get('an.demo.echo.label')\n"
                                   "}))\n");

    std::string output;
    ASSERT_TRUE(
        runProcessExpectSuccess(bundled_python, {QString::fromStdString(script_path.string())}, temp_root, &output))
        << output;

    QJsonParseError parse_error;
    const QJsonDocument document =
        QJsonDocument::fromJson(QByteArray::fromStdString(trimWhitespace(output)), &parse_error);
    ASSERT_EQ(parse_error.error, QJsonParseError::NoError) << output;
    ASSERT_TRUE(document.isObject()) << output;

    const QJsonObject object = document.object();
    EXPECT_DOUBLE_EQ(object.value(QStringLiteral("offset")).toDouble(), 65535.0);
    EXPECT_DOUBLE_EQ(object.value(QStringLiteral("scale")).toDouble(), -1.0);
    EXPECT_EQ(object.value(QStringLiteral("progress_text")).toString(),
              QStringLiteral("Creating a negative of the active image"));
    EXPECT_EQ(object.value(QStringLiteral("pixel_type")).toString(), QStringLiteral("16 bits (unsigned integer)"));
    EXPECT_EQ(object.value(QStringLiteral("label")).toString(), QStringLiteral("Echo Demo"));

    removeAllNoThrow(temp_root);
}

// The standalone workflow demo launches a native session.
TEST(ExtensionHostManagerTest, BundledStandaloneWorkflowDemoLaunchesNativeSession)
{
#if defined(_MSC_VER) && !defined(NDEBUG)
    GTEST_SKIP() << "PySide6 wheels target the release Qt runtime and cannot attach "
                    "to an MSVC debug QApplication";
#endif

    const std::filesystem::path host_path = extensionHostPathForCurrentAppDir();
    const std::filesystem::path runtime_root = runtimeRootForCurrentAppDir();
    const std::filesystem::path bundled_python = repositoryBundledPythonPath();
    const std::filesystem::path extensions_root = findRepositoryBundledExtensionsRoot();
    AN_REQUIRE_FULL_EXTENSION_HOST(host_path);
    ASSERT_TRUE(std::filesystem::exists(runtime_root)) << runtime_root;
    ASSERT_FALSE(bundled_python.empty()) << "Repository bundled Python runtime must be available";
    ASSERT_FALSE(extensions_root.empty()) << "Repository bundled extension roots must be available";

    const std::filesystem::path package_root = extensions_root / "an_standalone_preview_demo";
    ASSERT_TRUE(std::filesystem::exists(package_root / "extension.json")) << package_root;

    const auto temp_root = uniqueExtensionRuntimeRootForTest("bundled-standalone-workflow-demo");
    removeAllNoThrow(temp_root);
    const RemovePathOnExit remove_temp_root{temp_root};
    std::filesystem::create_directories(temp_root);

    const auto env_root = temp_root / "env";
    createVirtualEnvironment(bundled_python, env_root);
    if (!provisionBundledRuntimePackageIntoEnvironment(runtime_root, env_root, "PySide6") ||
        !provisionBundledRuntimePackageIntoEnvironment(runtime_root, env_root, "pyside6") ||
        !provisionBundledRuntimePackageIntoEnvironment(runtime_root, env_root, "shiboken6"))
    {
        GTEST_SKIP() << "PySide6 runtime artifacts are not available in the bundled runtime";
    }

    an::ui::extensions::ExtensionHostManager manager;
    an::ui::extensions::ExtensionHostLaunchRequest request;
    request.extension_id = "an.demo.standalone_preview";
    request.sdk_backend = "runtime";
    request.host_executable = host_path;
    request.capabilities = {"execute"};
    request.host_arguments = {
        "--extension-package-root",   package_root.string(), "--entry-point",
        "an_standalone_preview_demo", "--process-class",     "StandalonePreviewDemoExtension",
        "--environment-root",         env_root.string(),     "--runtime-root",
        runtime_root.string(),        "--stay-alive-ms",     std::to_string(5000 * kIpcTimingMultiplier)};

    std::string error;
    auto prepared = manager.prepareSession(request);
    an::ui::extensions::ExtensionHostRunningSession running_session;
    ASSERT_TRUE(manager.launchSession(prepared, &running_session, &error)) << error;
    SessionGuard session_guard{manager, running_session};
    EXPECT_TRUE(running_session.handshake.ack.native_process_window);

    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 250 * kIpcTimingMultiplier)
    {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 1);
        ASSERT_TRUE(manager.pumpSession(&running_session, 20, &error)) << error;
        QThread::msleep(20);
    }

    EXPECT_EQ(running_session.state, an::ui::extensions::ExtensionHostSessionState::Active);
    EXPECT_FALSE(running_session.last_preview_result.has_value());
    EXPECT_FALSE(running_session.last_view_commit.has_value());

    removeAllNoThrow(temp_root);
}

// Every sequence workflow dataset the fixtures describe names the outputs it expects.
TEST(ExtensionHostManagerTest, SequenceWorkflowFixtureDatasetsDefineExpectedOutputs)
{
    const QJsonObject fixture_contract = loadSequenceWorkflowFixtureContract();
    ASSERT_FALSE(fixture_contract.isEmpty());
    EXPECT_EQ(fixture_contract.value(QStringLiteral("schema_version")).toInt(), 1);

    const QJsonArray datasets = fixture_contract.value(QStringLiteral("datasets")).toArray();
    ASSERT_GE(datasets.size(), 4);

    QStringList dataset_ids;
    for (const auto& dataset_value : datasets)
    {
        ASSERT_TRUE(dataset_value.isObject());
        const QJsonObject dataset = dataset_value.toObject();
        const QString id = dataset.value(QStringLiteral("id")).toString();
        EXPECT_FALSE(id.isEmpty());
        EXPECT_FALSE(dataset_ids.contains(id)) << id.toStdString();
        dataset_ids.push_back(id);
        EXPECT_FALSE(dataset.value(QStringLiteral("kind")).toString().isEmpty()) << id.toStdString();
        EXPECT_TRUE(dataset.contains(QStringLiteral("expected"))) << id.toStdString();
    }

    for (const QString& required_id :
         {QStringLiteral("synthetic_sequence_workflow_smoke"), QStringLiteral("calibrate_register_stack_lrgb_v1"),
          QStringLiteral("workflow_cancellation_smoke"), QStringLiteral("workflow_output_cleanup_smoke")})
    {
        EXPECT_TRUE(dataset_ids.contains(required_id)) << required_id.toStdString();
    }

    const QJsonObject synthetic =
        findSequenceWorkflowDataset(fixture_contract, QStringLiteral("synthetic_sequence_workflow_smoke"));
    ASSERT_FALSE(synthetic.isEmpty());
    const QJsonObject synthetic_input = synthetic.value(QStringLiteral("input")).toObject();
    const QJsonObject synthetic_expected = synthetic.value(QStringLiteral("expected")).toObject();
    EXPECT_EQ(synthetic_input.value(QStringLiteral("synthetic_frames")).toInt(), 4);
    EXPECT_EQ(synthetic_input.value(QStringLiteral("output_name")).toString(),
              QStringLiteral("reports/workflow_demo.txt"));
    EXPECT_EQ(synthetic_expected.value(QStringLiteral("workflow_output_kind")).toString(), QStringLiteral("sequence"));
    EXPECT_EQ(synthetic_expected.value(QStringLiteral("report_filename")).toString(),
              QStringLiteral("workflow_demo.txt"));
    EXPECT_EQ(synthetic_expected.value(QStringLiteral("included_frame_count")).toInt(), 4);

    const QJsonArray stages = synthetic_expected.value(QStringLiteral("stages")).toArray();
    ASSERT_EQ(stages.size(), 5);
    EXPECT_EQ(stages.first().toString(), QStringLiteral("Preparing synthetic sequence"));
    EXPECT_EQ(stages.last().toString(), QStringLiteral("Workflow complete"));

    const QJsonObject preprocessing =
        findSequenceWorkflowDataset(fixture_contract, QStringLiteral("calibrate_register_stack_lrgb_v1"));
    ASSERT_FALSE(preprocessing.isEmpty());
    const QJsonArray frame_roles = preprocessing.value(QStringLiteral("frames")).toArray();
    ASSERT_EQ(frame_roles.size(), 4);
    bool has_light_role = false;
    for (const auto& role_value : frame_roles)
    {
        const QJsonObject role = role_value.toObject();
        if (role.value(QStringLiteral("role")).toString() == QStringLiteral("light"))
        {
            has_light_role = true;
            EXPECT_EQ(role.value(QStringLiteral("count")).toInt(), 4);
            EXPECT_TRUE(role.contains(QStringLiteral("registration")));
        }
    }
    EXPECT_TRUE(has_light_role);

    const QJsonObject preprocessing_expected = preprocessing.value(QStringLiteral("expected")).toObject();
    const QJsonArray artifacts = preprocessing_expected.value(QStringLiteral("artifacts")).toArray();
    EXPECT_TRUE(jsonStringArrayContains(artifacts, QStringLiteral("masters/master_bias_L.xisf")));
    EXPECT_TRUE(jsonStringArrayContains(artifacts, QStringLiteral("registered/L.sequence.json")));
    EXPECT_TRUE(jsonStringArrayContains(artifacts, QStringLiteral("stacks/stacked_L.fit")));
    const QJsonArray report_fields = preprocessing_expected.value(QStringLiteral("report_fields")).toArray();
    EXPECT_TRUE(jsonStringArrayContains(report_fields, QStringLiteral("calibration_summary")));
    EXPECT_TRUE(jsonStringArrayContains(report_fields, QStringLiteral("registration_summary")));
    EXPECT_TRUE(jsonStringArrayContains(report_fields, QStringLiteral("stacking_summary")));
    EXPECT_TRUE(jsonStringArrayContains(report_fields, QStringLiteral("output_artifacts")));

    const QJsonObject cancellation =
        findSequenceWorkflowDataset(fixture_contract, QStringLiteral("workflow_cancellation_smoke"));
    ASSERT_FALSE(cancellation.isEmpty());
    const QJsonObject cancellation_expected = cancellation.value(QStringLiteral("expected")).toObject();
    EXPECT_EQ(cancellation_expected.value(QStringLiteral("status")).toString(), QStringLiteral("cancelled"));
    EXPECT_EQ(cancellation_expected.value(QStringLiteral("opened_outputs")).toInt(), 0);
    EXPECT_FALSE(cancellation_expected.value(QStringLiteral("retained_temp_files")).toBool(true));

    const QJsonObject cleanup =
        findSequenceWorkflowDataset(fixture_contract, QStringLiteral("workflow_output_cleanup_smoke"));
    ASSERT_FALSE(cleanup.isEmpty());
    const QJsonObject cleanup_expected = cleanup.value(QStringLiteral("expected")).toObject();
    EXPECT_TRUE(cleanup_expected.value(QStringLiteral("success_opens_outputs")).toBool());
    EXPECT_TRUE(cleanup_expected.value(QStringLiteral("cancel_discards_outputs")).toBool());
    EXPECT_TRUE(cleanup_expected.value(QStringLiteral("crash_discards_outputs")).toBool());
    EXPECT_EQ(cleanup_expected.value(QStringLiteral("output_kind")).toString(), QStringLiteral("sequence"));
}

// The sequence workflow demo runs a workflow over a dataset and produces the outputs it names.
TEST(ExtensionHostManagerTest, BundledSequenceWorkflowDemoExecutesWorkflowContext)
{
    const QJsonObject fixture_contract = loadSequenceWorkflowFixtureContract();
    ASSERT_FALSE(fixture_contract.isEmpty());
    const QJsonObject synthetic_fixture =
        findSequenceWorkflowDataset(fixture_contract, QStringLiteral("synthetic_sequence_workflow_smoke"));
    ASSERT_FALSE(synthetic_fixture.isEmpty());
    const QJsonObject synthetic_input = synthetic_fixture.value(QStringLiteral("input")).toObject();
    const QJsonObject synthetic_expected = synthetic_fixture.value(QStringLiteral("expected")).toObject();
    const QJsonObject preprocessing_fixture =
        findSequenceWorkflowDataset(fixture_contract, QStringLiteral("calibrate_register_stack_lrgb_v1"));
    ASSERT_FALSE(preprocessing_fixture.isEmpty());
    const QJsonObject preprocessing_expected = preprocessing_fixture.value(QStringLiteral("expected")).toObject();
    const int synthetic_frames = synthetic_input.value(QStringLiteral("synthetic_frames")).toInt();
    const QString output_name = synthetic_input.value(QStringLiteral("output_name")).toString();
    ASSERT_GT(synthetic_frames, 0);
    ASSERT_FALSE(output_name.isEmpty());

    const std::filesystem::path host_path = extensionHostPathForCurrentAppDir();
    const std::filesystem::path runtime_root = runtimeRootForCurrentAppDir();
    const std::filesystem::path bundled_python = repositoryBundledPythonPath();
    const std::filesystem::path extensions_root = findRepositoryBundledExtensionsRoot();
    AN_REQUIRE_FULL_EXTENSION_HOST(host_path);
    ASSERT_TRUE(std::filesystem::exists(runtime_root)) << runtime_root;
    ASSERT_FALSE(bundled_python.empty()) << "Repository bundled Python runtime must be available";
    ASSERT_FALSE(extensions_root.empty()) << "Repository bundled extension roots must be available";

    std::filesystem::path package_root = extensions_root / "an_sequence_workflow_demo";
    if (!std::filesystem::exists(package_root / "extension.json"))
    {
        package_root = findRepositoryRootForTests() / "extensions" / "an_sequence_workflow_demo";
    }
    ASSERT_TRUE(std::filesystem::exists(package_root / "extension.json")) << package_root;

    const auto temp_root = uniqueExtensionRuntimeRootForTest("bundled-sequence-workflow-demo");
    removeAllNoThrow(temp_root);
    const RemovePathOnExit remove_temp_root{temp_root};
    std::filesystem::create_directories(temp_root);

    const auto env_root = temp_root / "env";
    createVirtualEnvironment(bundled_python, env_root);
    if (!provisionBundledRuntimePackageIntoEnvironment(runtime_root, env_root, "numpy"))
    {
        GTEST_SKIP() << "numpy not available in bundled runtime";
    }

    InMemorySettings settings_store;
    GlobalSettingsPersistence& settings = settings_store.repository();
    an::ui::extensions::ExtensionSdkService sdk_service(&settings);
    an::ui::extensions::ExtensionHostManager manager;
    manager.setSdkService(&sdk_service);

    an::ui::extensions::ExtensionHostLaunchRequest request;
    request.extension_id = "an.demo.sequence_workflow";
    request.display_name = "Sequence Workflow Demo";
    request.sdk_backend = "runtime";
    request.launch_mode = an::ui::extensions::ExtensionLaunchMode::Workflow;
    request.host_executable = host_path;
    request.capabilities = {"execute", "workflow"};
    request.host_arguments = {
        "--extension-package-root",  package_root.string(), "--entry-point",
        "an_sequence_workflow_demo", "--process-class",     "SequenceWorkflowDemoExtension",
        "--environment-root",        env_root.string(),     "--runtime-root",
        runtime_root.string(),       "--stay-alive-ms",     std::to_string(5000 * kIpcTimingMultiplier)};

    std::string error;
    auto prepared = manager.prepareSession(request);
    an::ui::extensions::ExtensionHostRunningSession running_session;
    ASSERT_TRUE(manager.launchSession(prepared, &running_session, &error)) << error;
    SessionGuard session_guard{manager, running_session};
    EXPECT_TRUE(running_session.handshake.ack.native_process_window);

    std::string request_id;
    ASSERT_TRUE(manager.sendExecuteRequest(
        &running_session, "workflow-demo-token",
        QCborMap{{QStringLiteral("synthetic_frames"), synthetic_frames}, {QStringLiteral("output_name"), output_name}},
        {}, &request_id, &error))
        << error;
    ASSERT_FALSE(request_id.empty());

    ASSERT_TRUE(pumpUntilRequestCompletes(manager, &running_session, 5000 * kIpcTimingMultiplier, &error)) << error;

    EXPECT_EQ(running_session.state, an::ui::extensions::ExtensionHostSessionState::Active);
    ASSERT_TRUE(running_session.last_preview_result.has_value());
    EXPECT_EQ(running_session.last_preview_result->result_kind, "workflow");
    EXPECT_EQ(running_session.last_preview_result->status, "succeeded") << running_session.last_preview_result->detail;

    ASSERT_EQ(sdk_service.workflowOutputs().size(), 1u);
    const auto& output = sdk_service.workflowOutputs().front();
    EXPECT_EQ(output.kind, synthetic_expected.value(QStringLiteral("workflow_output_kind")).toString().toStdString());
    EXPECT_TRUE(std::filesystem::exists(output.path)) << output.path;
    EXPECT_EQ(output.path.filename().string(),
              synthetic_expected.value(QStringLiteral("report_filename")).toString().toStdString());
    const std::string report_contents = readTextFileForTests(output.path);
    ASSERT_FALSE(report_contents.empty()) << output.path;

    for (const auto& field_value : preprocessing_expected.value(QStringLiteral("report_fields")).toArray())
    {
        const std::string field = field_value.toString().toStdString();
        EXPECT_NE(report_contents.find(field), std::string::npos) << field;
    }

    const std::filesystem::path output_root = output.path.parent_path().parent_path();
    for (const auto& artifact_value : preprocessing_expected.value(QStringLiteral("artifacts")).toArray())
    {
        const std::filesystem::path artifact_path = output_root / artifact_value.toString().toStdString();
        EXPECT_TRUE(std::filesystem::exists(artifact_path)) << artifact_path;
    }

    QCborValue stored_frame_count;
    ASSERT_TRUE(sdk_service.selectExtensionSetting("an.demo.sequence_workflow", "last_sequence_frames",
                                                   &stored_frame_count, {}, &error))
        << error;
    EXPECT_EQ(stored_frame_count.toInteger(), synthetic_expected.value(QStringLiteral("settings"))
                                                  .toObject()
                                                  .value(QStringLiteral("last_sequence_frames"))
                                                  .toInt());

    QString pipeline_status;
    ASSERT_TRUE(sdk_service.selectExtensionSetting("an.demo.sequence_workflow", "last_pipeline_status",
                                                   &pipeline_status, {}, &error))
        << error;
    EXPECT_EQ(pipeline_status, synthetic_expected.value(QStringLiteral("settings"))
                                   .toObject()
                                   .value(QStringLiteral("last_pipeline_status"))
                                   .toString());

    removeAllNoThrow(temp_root);
}

// A workflow's registered outputs are presented once it completes.
TEST(ExtensionHostManagerTest, WorkflowExecutionPresentsOutputsAfterSuccessfulCompletion)
{
    const QJsonObject fixture_contract = loadSequenceWorkflowFixtureContract();
    ASSERT_FALSE(fixture_contract.isEmpty());
    const QJsonObject cleanup_fixture =
        findSequenceWorkflowDataset(fixture_contract, QStringLiteral("workflow_output_cleanup_smoke"));
    ASSERT_FALSE(cleanup_fixture.isEmpty());
    const QJsonObject cleanup_expected = cleanup_fixture.value(QStringLiteral("expected")).toObject();
    ASSERT_TRUE(cleanup_expected.value(QStringLiteral("success_opens_outputs")).toBool());

    const std::filesystem::path host_path = extensionHostPathForCurrentAppDir();
    const std::filesystem::path runtime_root = runtimeRootForCurrentAppDir();
    const std::filesystem::path bundled_python = repositoryBundledPythonPath();
    AN_REQUIRE_FULL_EXTENSION_HOST(host_path);
    ASSERT_TRUE(std::filesystem::exists(runtime_root)) << runtime_root;
    ASSERT_FALSE(bundled_python.empty()) << "Repository bundled Python runtime must be available";

    const auto temp_root = uniqueExtensionRuntimeRootForTest("workflow-output-present");
    removeAllNoThrow(temp_root);
    const RemovePathOnExit remove_temp_root{temp_root};
    std::filesystem::create_directories(temp_root);

    const auto env_root = temp_root / "env";
    const auto package_root = temp_root / "workflow_output_present_ext";
    createVirtualEnvironment(bundled_python, env_root);
    std::filesystem::create_directories(package_root);
    writeTextFile(package_root / "workflow_output_present_ext.py",
                  "import afternight\n"
                  "import pathlib\n\n"
                  "class WorkflowOutputPresentExtension(afternight.WorkflowExtension):\n"
                  "    def execute_workflow(self, workflow, params, progress):\n"
                  "        output_path = pathlib.Path(workflow.output_path('success/result.anseq'))\n"
                  "        output_path.parent.mkdir(parents=True, exist_ok=True)\n"
                  "        output_path.write_text('{\"sequence\": true}', encoding='utf-8')\n"
                  "        workflow.open_result_sequence(str(output_path))\n"
                  "        progress.set_stage('Workflow complete')\n");

    InMemorySettings settings_store;
    GlobalSettingsPersistence& settings = settings_store.repository();
    an::ui::extensions::ExtensionSdkService sdk_service(&settings);
    int presentation_count = 0;
    std::string presented_session_id;
    std::vector<an::ui::extensions::ExtensionWorkflowOutput> presented_outputs;
    sdk_service.setWorkflowOutputHandler([&](const std::string_view session_id,
                                             const std::vector<an::ui::extensions::ExtensionWorkflowOutput>& outputs,
                                             std::string*) -> bool {
        ++presentation_count;
        presented_session_id = std::string(session_id);
        presented_outputs = outputs;
        return true;
    });

    an::ui::extensions::ExtensionHostManager manager;
    manager.setSdkService(&sdk_service);

    an::ui::extensions::ExtensionHostLaunchRequest request;
    request.extension_id = "an.demo.workflow_output_present";
    request.display_name = "Workflow Output Present";
    request.sdk_backend = "runtime";
    request.launch_mode = an::ui::extensions::ExtensionLaunchMode::Workflow;
    request.host_executable = host_path;
    request.capabilities = {"execute", "workflow"};
    request.host_arguments = {
        "--extension-package-root",    package_root.string(), "--entry-point",
        "workflow_output_present_ext", "--process-class",     "WorkflowOutputPresentExtension",
        "--environment-root",          env_root.string(),     "--runtime-root",
        runtime_root.string(),         "--stay-alive-ms",     std::to_string(5000 * kIpcTimingMultiplier)};

    std::string error;
    const auto prepared = manager.prepareSession(request);
    an::ui::extensions::ExtensionHostRunningSession running_session;
    ASSERT_TRUE(manager.launchSession(prepared, &running_session, &error)) << error;
    SessionGuard session_guard{manager, running_session};

    std::string request_id;
    ASSERT_TRUE(manager.sendExecuteRequest(&running_session, "workflow-output-present-token", QCborMap{}, {},
                                           &request_id, &error))
        << error;
    ASSERT_FALSE(request_id.empty());

    ASSERT_TRUE(pumpUntilRequestCompletes(manager, &running_session, 5000 * kIpcTimingMultiplier, &error)) << error;

    EXPECT_EQ(running_session.state, an::ui::extensions::ExtensionHostSessionState::Active);
    ASSERT_TRUE(running_session.last_preview_result.has_value());
    EXPECT_EQ(running_session.last_preview_result->target_request_id, request_id);
    EXPECT_EQ(running_session.last_preview_result->result_kind, "workflow");
    EXPECT_EQ(running_session.last_preview_result->status, "succeeded");
    EXPECT_EQ(presentation_count, 1);
    EXPECT_EQ(presented_session_id, running_session.prepared_session.session_id);
    ASSERT_EQ(presented_outputs.size(), 1u);
    EXPECT_EQ(presented_outputs.front().kind,
              cleanup_expected.value(QStringLiteral("output_kind")).toString().toStdString());
    EXPECT_EQ(presented_outputs.front().path.filename().string(), "result.anseq");
    EXPECT_TRUE(std::filesystem::exists(presented_outputs.front().path)) << presented_outputs.front().path;
    EXPECT_TRUE(sdk_service.workflowOutputs().empty());

    ASSERT_TRUE(manager.pumpSession(&running_session, 20, &error)) << error;
    EXPECT_EQ(presentation_count, 1);

    removeAllNoThrow(temp_root);
}

// A workflow whose host terminates after registering outputs presents none of them.
TEST(ExtensionHostManagerTest, WorkflowExecutionDiscardsOutputsWhenHostTerminatesAfterRegistration)
{
    const QJsonObject fixture_contract = loadSequenceWorkflowFixtureContract();
    ASSERT_FALSE(fixture_contract.isEmpty());
    const QJsonObject cleanup_fixture =
        findSequenceWorkflowDataset(fixture_contract, QStringLiteral("workflow_output_cleanup_smoke"));
    ASSERT_FALSE(cleanup_fixture.isEmpty());
    const QJsonObject cleanup_expected = cleanup_fixture.value(QStringLiteral("expected")).toObject();
    ASSERT_TRUE(cleanup_expected.value(QStringLiteral("crash_discards_outputs")).toBool());

    const std::filesystem::path host_path = extensionHostPathForCurrentAppDir();
    const std::filesystem::path runtime_root = runtimeRootForCurrentAppDir();
    const std::filesystem::path bundled_python = repositoryBundledPythonPath();
    AN_REQUIRE_FULL_EXTENSION_HOST(host_path);
    ASSERT_TRUE(std::filesystem::exists(runtime_root)) << runtime_root;
    ASSERT_FALSE(bundled_python.empty()) << "Repository bundled Python runtime must be available";

    const auto temp_root = uniqueExtensionRuntimeRootForTest("workflow-output-crash-cleanup");
    removeAllNoThrow(temp_root);
    const RemovePathOnExit remove_temp_root{temp_root};
    std::filesystem::create_directories(temp_root);

    const auto env_root = temp_root / "env";
    const auto package_root = temp_root / "workflow_output_crash_ext";
    createVirtualEnvironment(bundled_python, env_root);
    std::filesystem::create_directories(package_root);
    writeTextFile(package_root / "workflow_output_crash_ext.py",
                  "import afternight\n"
                  "import os\n"
                  "import pathlib\n"
                  "import time\n\n"
                  "class WorkflowOutputCrashExtension(afternight.WorkflowExtension):\n"
                  "    def execute_workflow(self, workflow, params, progress):\n"
                  "        output_path = pathlib.Path(workflow.output_path('crashed/should_not_survive.anseq'))\n"
                  "        output_path.parent.mkdir(parents=True, exist_ok=True)\n"
                  "        output_path.write_text('{\"partial\": true}', encoding='utf-8')\n"
                  "        workflow.open_result_sequence(str(output_path))\n"
                  "        progress.set_stage('About to terminate')\n"
                  "        time.sleep(1.0)\n"
                  "        os._exit(42)\n");

    InMemorySettings settings_store;
    GlobalSettingsPersistence& settings = settings_store.repository();
    an::ui::extensions::ExtensionSdkService sdk_service(&settings);
    an::ui::extensions::ExtensionHostManager manager;
    manager.setSdkService(&sdk_service);

    an::ui::extensions::ExtensionHostLaunchRequest request;
    request.extension_id = "an.demo.workflow_output_crash";
    request.display_name = "Workflow Output Crash";
    request.sdk_backend = "runtime";
    request.launch_mode = an::ui::extensions::ExtensionLaunchMode::Workflow;
    request.host_executable = host_path;
    request.capabilities = {"execute", "workflow"};
    request.host_arguments = {
        "--extension-package-root",  package_root.string(), "--entry-point",
        "workflow_output_crash_ext", "--process-class",     "WorkflowOutputCrashExtension",
        "--environment-root",        env_root.string(),     "--runtime-root",
        runtime_root.string(),       "--stay-alive-ms",     std::to_string(5000 * kIpcTimingMultiplier)};
    request.heartbeat_interval_ms = 100 * kIpcTimingMultiplier;
    request.heartbeat_miss_limit = 10;

    std::string error;
    const auto prepared = manager.prepareSession(request);
    an::ui::extensions::ExtensionHostRunningSession running_session;
    ASSERT_TRUE(manager.launchSession(prepared, &running_session, &error)) << error;
    SessionGuard session_guard{manager, running_session};

    ASSERT_TRUE(
        manager.sendExecuteRequest(&running_session, "workflow-output-crash-token", QCborMap{}, {}, nullptr, &error))
        << error;

    const auto workflow_root =
        an::ui::extensions::extensionHostSessionRootPath(running_session.prepared_session.session_id) / "workflow";

    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 2500 * kIpcTimingMultiplier &&
           running_session.state == an::ui::extensions::ExtensionHostSessionState::Active &&
           sdk_service.workflowOutputs().empty())
    {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 1);
        if (!manager.pumpSession(&running_session, 20, &error))
        {
            break;
        }
        QThread::msleep(20);
    }

    ASSERT_EQ(sdk_service.workflowOutputs().size(), 1u) << error;
    const std::filesystem::path partial_output_path = sdk_service.workflowOutputs().front().path;
    ASSERT_TRUE(std::filesystem::exists(partial_output_path)) << partial_output_path;

    std::string terminal_error;
    EXPECT_FALSE(pumpUntilStateChanges(manager, &running_session, 4000 * kIpcTimingMultiplier, &terminal_error));
    EXPECT_NE(running_session.state, an::ui::extensions::ExtensionHostSessionState::Active);
    EXPECT_TRUE(sdk_service.workflowOutputs().empty());
    EXPECT_FALSE(std::filesystem::exists(partial_output_path)) << partial_output_path;
    EXPECT_TRUE(pathMissingOrEmpty(workflow_root)) << workflow_root;

    removeAllNoThrow(temp_root);
}

// A workflow cancelled cooperatively stops and presents none of its outputs.
TEST(ExtensionHostManagerTest, WorkflowExecutionCooperativelyCancelsAndDiscardsSessionOutputs)
{
    const QJsonObject fixture_contract = loadSequenceWorkflowFixtureContract();
    ASSERT_FALSE(fixture_contract.isEmpty());
    const QJsonObject cancellation_fixture =
        findSequenceWorkflowDataset(fixture_contract, QStringLiteral("workflow_cancellation_smoke"));
    ASSERT_FALSE(cancellation_fixture.isEmpty());
    const QJsonObject cancellation_trigger = cancellation_fixture.value(QStringLiteral("trigger")).toObject();
    const QJsonObject cancellation_expected = cancellation_fixture.value(QStringLiteral("expected")).toObject();
    const int cancel_grace_period_ms =
        cancellation_trigger.value(QStringLiteral("cancel_grace_period_ms")).toInt(1200) * kIpcTimingMultiplier;
    const int cancel_after_ms =
        cancellation_trigger.value(QStringLiteral("cancel_after_ms")).toInt(250) * kIpcTimingMultiplier;

    const std::filesystem::path host_path = extensionHostPathForCurrentAppDir();
    const std::filesystem::path runtime_root = runtimeRootForCurrentAppDir();
    const std::filesystem::path bundled_python = repositoryBundledPythonPath();
    AN_REQUIRE_FULL_EXTENSION_HOST(host_path);
    ASSERT_TRUE(std::filesystem::exists(runtime_root)) << runtime_root;
    ASSERT_FALSE(bundled_python.empty()) << "Repository bundled Python runtime must be available";

    const auto temp_root = uniqueExtensionRuntimeRootForTest("workflow-cooperative-cancel");
    removeAllNoThrow(temp_root);
    const RemovePathOnExit remove_temp_root{temp_root};
    std::filesystem::create_directories(temp_root);

    const auto env_root = temp_root / "env";
    const auto package_root = temp_root / "workflow_cancel_ext";
    createVirtualEnvironment(bundled_python, env_root);
    std::filesystem::create_directories(package_root);
    writeTextFile(package_root / "workflow_cancel_ext.py",
                  "import afternight\n"
                  "import pathlib\n"
                  "import time\n\n"
                  "class WorkflowCancelExtension(afternight.WorkflowExtension):\n"
                  "    def execute_workflow(self, workflow, params, progress):\n"
                  "        temp_dir = pathlib.Path(workflow.temp_dir())\n"
                  "        marker = temp_dir / 'cancel_marker.txt'\n"
                  "        marker.parent.mkdir(parents=True, exist_ok=True)\n"
                  "        marker.write_text('started', encoding='utf-8')\n"
                  "        output_path = pathlib.Path(workflow.output_path('cancelled/should_not_survive.txt'))\n"
                  "        output_path.parent.mkdir(parents=True, exist_ok=True)\n"
                  "        output_path.write_text('partial', encoding='utf-8')\n"
                  "        workflow.open_result_sequence(str(output_path))\n"
                  "        stage = progress.set_stage('Registering')\n"
                  "        for index in range(400):\n"
                  "            stage.set_value(index / 400.0)\n"
                  "            if progress.is_cancelled() or stage.is_cancelled():\n"
                  "                self.settings.set('cancelled', True)\n"
                  "                return\n"
                  "            time.sleep(0.01)\n"
                  "        self.settings.set('completed_without_cancel', True)\n"
                  "        workflow.open_result_sequence(str(output_path))\n");

    InMemorySettings settings_store;
    GlobalSettingsPersistence& settings = settings_store.repository();
    an::ui::extensions::ExtensionSdkService sdk_service(&settings);
    an::ui::extensions::ExtensionHostManager manager;
    manager.setSdkService(&sdk_service);

    an::ui::extensions::ExtensionHostLaunchRequest request;
    request.extension_id = "an.demo.workflow_cancel";
    request.display_name = "Workflow Cancel";
    request.sdk_backend = "runtime";
    request.launch_mode = an::ui::extensions::ExtensionLaunchMode::Workflow;
    request.host_executable = host_path;
    request.capabilities = {"execute", "workflow"};
    request.host_arguments = {
        "--extension-package-root", package_root.string(), "--entry-point",
        "workflow_cancel_ext",      "--process-class",     "WorkflowCancelExtension",
        "--environment-root",       env_root.string(),     "--runtime-root",
        runtime_root.string(),      "--stay-alive-ms",     std::to_string(5000 * kIpcTimingMultiplier)};
    request.heartbeat_interval_ms = 100 * kIpcTimingMultiplier;
    request.cancel_grace_period_ms = cancel_grace_period_ms;

    std::string error;
    const auto prepared = manager.prepareSession(request);
    an::ui::extensions::ExtensionHostRunningSession running_session;
    ASSERT_TRUE(manager.launchSession(prepared, &running_session, &error)) << error;
    SessionGuard session_guard{manager, running_session};

    std::string request_id;
    ASSERT_TRUE(
        manager.sendExecuteRequest(&running_session, "workflow-cancel-token", QCborMap{}, {}, &request_id, &error))
        << error;
    ASSERT_FALSE(request_id.empty());

    const auto workflow_root =
        an::ui::extensions::extensionHostSessionRootPath(running_session.prepared_session.session_id) / "workflow";

    QElapsedTimer timer;
    timer.start();
    const int output_registration_timeout_ms = (std::max)(cancel_after_ms, 1500 * kIpcTimingMultiplier);
    while (timer.elapsed() < output_registration_timeout_ms && sdk_service.workflowOutputs().empty())
    {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 1);
        ASSERT_TRUE(manager.pumpSession(&running_session, 20, &error)) << error;
        QThread::msleep(20);
    }
    ASSERT_EQ(sdk_service.workflowOutputs().size(), 1u);
    const std::filesystem::path partial_output_path = sdk_service.workflowOutputs().front().path;
    ASSERT_TRUE(std::filesystem::exists(partial_output_path)) << partial_output_path;

    ASSERT_TRUE(manager.sendCancelRequest(&running_session, nullptr, &error)) << error;
    ASSERT_TRUE(pumpUntilRequestCompletes(manager, &running_session,
                                          cancel_grace_period_ms + 3000 * kIpcTimingMultiplier, &error))
        << error;

    ASSERT_TRUE(running_session.last_preview_result.has_value());
    EXPECT_EQ(running_session.last_preview_result->target_request_id, request_id);
    EXPECT_EQ(running_session.last_preview_result->result_kind, "workflow");
    EXPECT_EQ(running_session.last_preview_result->status,
              cancellation_expected.value(QStringLiteral("status")).toString().toStdString());
    EXPECT_EQ(static_cast<int>(running_session.last_preview_result->output_buffers.size()),
              cancellation_expected.value(QStringLiteral("opened_outputs")).toInt());
    EXPECT_EQ(static_cast<int>(sdk_service.workflowOutputs().size()),
              cancellation_expected.value(QStringLiteral("registered_outputs")).toInt());
    EXPECT_FALSE(std::filesystem::exists(partial_output_path)) << partial_output_path;
    if (!cancellation_expected.value(QStringLiteral("retained_temp_files")).toBool(true))
    {
        EXPECT_TRUE(pathMissingOrEmpty(workflow_root)) << workflow_root;
    }

    QCborValue cancelled_value;
    ASSERT_TRUE(
        sdk_service.selectExtensionSetting("an.demo.workflow_cancel", "cancelled", &cancelled_value, false, &error))
        << error;
    EXPECT_TRUE(cancelled_value.toBool());

    QCborValue completed_value;
    ASSERT_TRUE(sdk_service.selectExtensionSetting("an.demo.workflow_cancel", "completed_without_cancel",
                                                   &completed_value, false, &error))
        << error;
    EXPECT_FALSE(completed_value.toBool());

    removeAllNoThrow(temp_root);
}

// A workflow that ignores its cancellation is stopped once the grace period passes.
TEST(ExtensionHostManagerTest, WorkflowExecutionEscalatesWhenCancelIsIgnored)
{
    const std::filesystem::path host_path = extensionHostPathForCurrentAppDir();
    const std::filesystem::path runtime_root = runtimeRootForCurrentAppDir();
    const std::filesystem::path bundled_python = repositoryBundledPythonPath();
    AN_REQUIRE_FULL_EXTENSION_HOST(host_path);
    ASSERT_TRUE(std::filesystem::exists(runtime_root)) << runtime_root;
    ASSERT_FALSE(bundled_python.empty()) << "Repository bundled Python runtime must be available";

    const auto temp_root = uniqueExtensionRuntimeRootForTest("workflow-ignored-cancel");
    removeAllNoThrow(temp_root);
    const RemovePathOnExit remove_temp_root{temp_root};
    std::filesystem::create_directories(temp_root);

    const auto env_root = temp_root / "env";
    const auto package_root = temp_root / "workflow_ignore_cancel_ext";
    createVirtualEnvironment(bundled_python, env_root);
    std::filesystem::create_directories(package_root);
    writeTextFile(package_root / "workflow_ignore_cancel_ext.py",
                  "import afternight\n"
                  "import pathlib\n"
                  "import time\n\n"
                  "class WorkflowIgnoreCancelExtension(afternight.WorkflowExtension):\n"
                  "    def execute_workflow(self, workflow, params, progress):\n"
                  "        temp_dir = pathlib.Path(workflow.temp_dir())\n"
                  "        marker = temp_dir / 'ignored_cancel_marker.txt'\n"
                  "        marker.parent.mkdir(parents=True, exist_ok=True)\n"
                  "        marker.write_text('started', encoding='utf-8')\n"
                  "        progress.set_stage('Registering')\n"
                  "        time.sleep(5.0)\n");

    InMemorySettings settings_store;
    GlobalSettingsPersistence& settings = settings_store.repository();
    an::ui::extensions::ExtensionSdkService sdk_service(&settings);
    an::ui::extensions::ExtensionHostManager manager;
    manager.setSdkService(&sdk_service);

    an::ui::extensions::ExtensionHostLaunchRequest request;
    request.extension_id = "an.demo.workflow_ignore_cancel";
    request.display_name = "Workflow Ignore Cancel";
    request.sdk_backend = "runtime";
    request.launch_mode = an::ui::extensions::ExtensionLaunchMode::Workflow;
    request.host_executable = host_path;
    request.capabilities = {"execute", "workflow"};
    request.host_arguments = {
        "--extension-package-root",   package_root.string(), "--entry-point",
        "workflow_ignore_cancel_ext", "--process-class",     "WorkflowIgnoreCancelExtension",
        "--environment-root",         env_root.string(),     "--runtime-root",
        runtime_root.string(),        "--stay-alive-ms",     std::to_string(6000 * kIpcTimingMultiplier)};
    request.heartbeat_interval_ms = 1000 * kIpcTimingMultiplier;
    request.heartbeat_miss_limit = 10;
    request.cancel_grace_period_ms = 250 * kIpcTimingMultiplier;

    std::string error;
    const auto prepared = manager.prepareSession(request);
    an::ui::extensions::ExtensionHostRunningSession running_session;
    ASSERT_TRUE(manager.launchSession(prepared, &running_session, &error)) << error;
    SessionGuard session_guard{manager, running_session};

    ASSERT_TRUE(
        manager.sendExecuteRequest(&running_session, "workflow-ignore-cancel-token", QCborMap{}, {}, nullptr, &error))
        << error;

    ASSERT_TRUE(pumpUntilStateChanges(manager, &running_session, 250 * kIpcTimingMultiplier, &error)) << error;
    ASSERT_TRUE(manager.sendCancelRequest(&running_session, nullptr, &error)) << error;
    EXPECT_FALSE(pumpUntilStateChanges(manager, &running_session, 1500 * kIpcTimingMultiplier, &error));
    EXPECT_EQ(running_session.state, an::ui::extensions::ExtensionHostSessionState::Failed);
    EXPECT_NE(error.find("grace period"), std::string::npos);
    EXPECT_FALSE(running_session.last_preview_result.has_value());

    const auto workflow_root =
        an::ui::extensions::extensionHostSessionRootPath(running_session.prepared_session.session_id) / "workflow";
    EXPECT_TRUE(pathMissingOrEmpty(workflow_root)) << workflow_root;

    removeAllNoThrow(temp_root);
}

// A preview over a cached source array, a mask and a weight map produces the transform the extension states.
TEST(ExtensionHostManagerTest, StandaloneCachedArrayPreviewProducesExpectedResult)
{
    const std::filesystem::path host_path = extensionHostPathForCurrentAppDir();
    const std::filesystem::path runtime_root = runtimeRootForCurrentAppDir();
    const std::filesystem::path bundled_python = repositoryBundledPythonPath();
    AN_REQUIRE_FULL_EXTENSION_HOST(host_path);
    ASSERT_TRUE(std::filesystem::exists(runtime_root)) << runtime_root;
    ASSERT_FALSE(bundled_python.empty()) << "Repository bundled Python runtime must be available";

    const auto temp_root = uniqueExtensionRuntimeRootForTest("standalone-cached-array-preview");
    removeAllNoThrow(temp_root);
    const RemovePathOnExit remove_temp_root{temp_root};
    std::filesystem::create_directories(temp_root);

    const auto env_root = temp_root / "env";
    const auto package_root = temp_root / "standalone_cached_array_preview_ext";
    createVirtualEnvironment(bundled_python, env_root);
    if (!provisionBundledRuntimePackageIntoEnvironment(runtime_root, env_root, "numpy"))
        GTEST_SKIP() << "numpy not available in bundled runtime";

    std::filesystem::create_directories(package_root);
    writeTextFile(
        package_root / "standalone_cached_array_preview_ext.py",
        "import afternight\n"
        "import numpy as np\n\n"
        "def _finite_array(array):\n"
        "    return np.nan_to_num(np.array(array, copy=False), nan=0.0, posinf=0.0, neginf=0.0)\n\n"
        "def _unit_float_array(array):\n"
        "    result = _finite_array(array)\n"
        "    if np.issubdtype(result.dtype, np.integer):\n"
        "        info = np.iinfo(result.dtype)\n"
        "        scale = float(max(abs(int(info.min)), abs(int(info.max)), 1))\n"
        "        return result.astype(np.float32) / scale\n"
        "    return result.astype(np.float32, copy=False)\n\n"
        "def _working_source_array(source_image):\n"
        "    source = _finite_array(source_image)\n"
        "    if np.issubdtype(source.dtype, np.integer):\n"
        "        info = np.iinfo(source.dtype)\n"
        "        scale = float(max(abs(int(info.min)), abs(int(info.max)), 1))\n"
        "        return source.astype(np.float32) / scale, scale, source.dtype\n"
        "    return source.astype(np.float32, copy=False), 1.0, source.dtype\n\n"
        "def _restore_source_array_scale(result, source_scale, source_dtype):\n"
        "    restored = np.nan_to_num(result, nan=0.0, posinf=0.0, neginf=0.0)\n"
        "    if np.issubdtype(source_dtype, np.integer):\n"
        "        restored = np.clip(restored, 0.0, 1.0) * source_scale\n"
        "        return restored.astype(source_dtype, copy=False)\n"
        "    return restored.astype(np.float32, copy=False)\n\n"
        "def _mono_or_matching_array(image_handle, source_array):\n"
        "    if image_handle is None:\n"
        "        return None\n"
        "    array = _unit_float_array(image_handle.to_numpy())\n"
        "    if source_array.ndim == 3 and array.ndim == 2:\n"
        "        return array[:, :, None]\n"
        "    return array\n\n"
        "class StandaloneCachedArrayPreviewExtension(afternight.RTPreviewExtension):\n"
        "    def __init__(self, settings):\n"
        "        super().__init__(settings)\n"
        "        self._preview_source = None\n\n"
        "    def _cached_source(self, src_image):\n"
        "        if self._preview_source is None:\n"
        "            self._preview_source = _finite_array(src_image.to_numpy()).copy()\n"
        "        return self._preview_source\n\n"
        "    def _gamma(self, params):\n"
        "        strength = float(params.get('strength', 0.45))\n"
        "        mode = str(params.get('mode', 'balanced'))\n"
        "        gamma = 1.0 / (1.0 + strength)\n"
        "        if mode == 'contrast':\n"
        "            gamma *= 0.85\n"
        "        elif mode == 'gentle':\n"
        "            gamma = 1.0 / (1.0 + strength * 0.5)\n"
        "        return gamma\n\n"
        "    def _render_array(self, source_image, params, masks, weights):\n"
        "        source, source_scale, source_dtype = _working_source_array(source_image.to_numpy() if "
        "hasattr(source_image, 'to_numpy') else source_image)\n"
        "        gamma = self._gamma(params)\n"
        "        lift = float(params.get('lift', 0.05))\n"
        "        clipped = np.clip(source, 0.0, None)\n"
        "        result = np.power(clipped, gamma) + lift\n"
        "        if bool(params.get('apply_mask', True)) and masks and 'selection' in masks:\n"
        "            selection = _mono_or_matching_array(masks['selection'], source)\n"
        "            if selection is not None:\n"
        "                extra_glow = np.power(clipped, gamma * 0.7) - np.power(clipped, gamma)\n"
        "                strength = float(params.get('strength', 0.45))\n"
        "                result += selection * extra_glow * (0.3 + strength * 0.5)\n"
        "        if bool(params.get('apply_weight', True)) and weights and 'weight_map' in weights:\n"
        "            weight_map = _mono_or_matching_array(weights['weight_map'], source)\n"
        "            if weight_map is not None:\n"
        "                strength = float(params.get('strength', 0.45))\n"
        "                result += weight_map * strength * 0.15\n"
        "        return _restore_source_array_scale(result, source_scale, source_dtype)\n\n"
        "    def execute_preview(self, target, src_image, preview_image, params, progress, masks=None, weights=None):\n"
        "        preview_image.from_numpy(self._render_array(self._cached_source(src_image), params, masks, "
        "weights))\n\n"
        "    def execute(self, target, src_image, dst_image, params, progress, masks=None, weights=None, "
        "output_masks=None):\n"
        "        dst_image.from_numpy(self._render_array(src_image, params, masks, weights))\n");

    const auto source_image = makeMonoUInt16Image("standalone-preview-source", 5, 4, 2048u, 73u);
    const auto selection_image = makeMonoFloatImage("selection", 5, 4, 0.2f, 0.05f);
    const auto weight_image = makeMonoFloatImage("weight_map", 5, 4, 0.4f, 0.08f);
    ASSERT_TRUE(source_image);
    ASSERT_TRUE(selection_image);
    ASSERT_TRUE(weight_image);

    const auto root = uniqueSharedBufferRootForTest("standalone-cached-array-preview");
    removeAllNoThrow(root);
    const RemovePathOnExit remove_root{root};

    an::ui::extensions::ExtensionHostManager manager;
    an::ui::extensions::ExtensionHostLaunchRequest request;
    request.extension_id = "an.demo.cached_array_preview";
    request.sdk_backend = "runtime";
    request.host_executable = host_path;
    request.capabilities = {"execute", "preview"};
    request.host_arguments = {"--extension-package-root",
                              package_root.string(),
                              "--entry-point",
                              "standalone_cached_array_preview_ext",
                              "--process-class",
                              "StandaloneCachedArrayPreviewExtension",
                              "--environment-root",
                              env_root.string(),
                              "--runtime-root",
                              runtime_root.string(),
                              "--stay-alive-ms",
                              std::to_string(5000 * kIpcTimingMultiplier)};

    std::string error;
    auto prepared = manager.prepareSession(request);
    an::ui::extensions::ExtensionHostRunningSession running_session;
    ASSERT_TRUE(manager.launchSession(prepared, &running_session, &error)) << error;
    SessionGuard session_guard{manager, running_session};

    auto source_buffer = makeImageBuffer(root, running_session.prepared_session.session_id, source_image,
                                         an::ui::extensions::SharedBufferRole::Source,
                                         an::ui::extensions::SharedBufferAccessMode::ReadOnly, "source", &error);
    auto selection_buffer = makeImageBuffer(root, running_session.prepared_session.session_id, selection_image,
                                            an::ui::extensions::SharedBufferRole::Mask,
                                            an::ui::extensions::SharedBufferAccessMode::ReadOnly, "selection", &error);
    auto weight_buffer = makeImageBuffer(root, running_session.prepared_session.session_id, weight_image,
                                         an::ui::extensions::SharedBufferRole::Weight,
                                         an::ui::extensions::SharedBufferAccessMode::ReadOnly, "weight_map", &error);
    ASSERT_TRUE(source_buffer.valid()) << error;
    ASSERT_TRUE(selection_buffer.valid()) << error;
    ASSERT_TRUE(weight_buffer.valid()) << error;

    const double preview_strength = 0.45;
    const double preview_lift = 0.05;
    const QString preview_mode = QStringLiteral("balanced");
    const QCborMap preview_params = {
        {QStringLiteral("preview_enabled"), true}, {QStringLiteral("strength"), preview_strength},
        {QStringLiteral("lift"), preview_lift},    {QStringLiteral("mode"), preview_mode},
        {QStringLiteral("apply_mask"), true},      {QStringLiteral("apply_weight"), true},
    };

    std::string request_id;
    ASSERT_TRUE(manager.sendPreviewRequest(
        &running_session, "standalone-cached-array-preview", preview_params,
        {source_buffer.descriptor(), selection_buffer.descriptor(), weight_buffer.descriptor()}, &request_id, &error))
        << error;
    ASSERT_FALSE(request_id.empty());

    ASSERT_TRUE(pumpUntilRequestCompletes(manager, &running_session, 5000 * kIpcTimingMultiplier, &error)) << error;

    ASSERT_TRUE(running_session.last_preview_result.has_value());
    ASSERT_EQ(running_session.last_preview_result->status, "succeeded");
    ASSERT_EQ(running_session.last_preview_result->output_buffers.size(), 1u);

    an::ui::extensions::SharedBufferManager shared_buffer_manager(root);
    std::optional<an::consumer::ImageAsset> preview_image;
    ASSERT_TRUE(importBufferOrReport(shared_buffer_manager, running_session.last_preview_result->output_buffers.front(),
                                     &preview_image, &error))
        << error;
    ASSERT_TRUE(preview_image);
    ASSERT_TRUE(hasSampleType(*preview_image, an::SampleType::UInt16));

    for (std::size_t y = 0; y < heightOf(*source_image); ++y)
    {
        for (std::size_t x = 0; x < widthOf(*source_image); ++x)
        {
            EXPECT_NEAR(static_cast<double>(sampleAt(*preview_image, x, y, 0)),
                        expectedStandalonePreviewDemoValue(static_cast<double>(sampleAt(*source_image, x, y, 0)),
                                                           sampleAt(*selection_image, x, y, 0),
                                                           sampleAt(*weight_image, x, y, 0), preview_strength,
                                                           preview_lift, preview_mode, true, true, 65535.0),
                        1.0)
                << "preview mismatch at x=" << x << " y=" << y;
        }
    }

    removeAllNoThrow(root);
    removeAllNoThrow(temp_root);
}

// A slow preview cancelled cooperatively answers cancelled with no output.
TEST(ExtensionHostManagerTest, StandalonePreviewDemoCooperativelyCancelsSlowPreviewRendering)
{
    const std::filesystem::path host_path = extensionHostPathForCurrentAppDir();
    const std::filesystem::path runtime_root = runtimeRootForCurrentAppDir();
    const std::filesystem::path bundled_python = repositoryBundledPythonPath();
    AN_REQUIRE_FULL_EXTENSION_HOST(host_path);
    ASSERT_TRUE(std::filesystem::exists(runtime_root)) << runtime_root;
    ASSERT_FALSE(bundled_python.empty()) << "Repository bundled Python runtime must be available";

    const auto temp_root = uniqueExtensionRuntimeRootForTest("bundled-standalone-preview-cancel");
    removeAllNoThrow(temp_root);
    const RemovePathOnExit remove_temp_root{temp_root};
    std::filesystem::create_directories(temp_root);

    const auto env_root = temp_root / "env";
    const auto package_root = temp_root / "standalone_cancel_preview_ext";
    createVirtualEnvironment(bundled_python, env_root);
    if (!provisionBundledRuntimePackageIntoEnvironment(runtime_root, env_root, "numpy"))
        GTEST_SKIP() << "numpy not available in bundled runtime";

    std::filesystem::create_directories(package_root);
    writeTextFile(
        package_root / "standalone_cancel_preview_ext.py",
        "import afternight\n"
        "import time\n\n"
        "class StandaloneCancelPreviewExtension(afternight.RTPreviewExtension):\n"
        "    def __init__(self, settings):\n"
        "        super().__init__(settings)\n"
        "        self._cached = None\n\n"
        "    def _cached_source(self, src_image):\n"
        "        if self._cached is None:\n"
        "            self._cached = src_image.clone()\n"
        "        return self._cached\n\n"
        "    def execute_preview(self, target, src_image, preview_image, params, progress, masks=None, weights=None):\n"
        "        cached = self._cached_source(src_image)\n"
        "        for _ in range(200):\n"
        "            if progress.is_cancelled():\n"
        "                return\n"
        "            time.sleep(0.01)\n"
        "        preview_image.copy_from(cached)\n\n"
        "    def execute(self, target, src_image, dst_image, params, progress, masks=None, weights=None, "
        "output_masks=None):\n"
        "        dst_image.copy_from(self._cached_source(src_image))\n");

    an::ui::extensions::ExtensionHostManager manager;
    an::ui::extensions::ExtensionHostLaunchRequest request;
    request.extension_id = "an.demo.standalone_preview_cancel";
    request.sdk_backend = "runtime";
    request.host_executable = host_path;
    request.capabilities = {"execute", "preview"};
    request.host_arguments = {"--extension-package-root",
                              package_root.string(),
                              "--entry-point",
                              "standalone_cancel_preview_ext",
                              "--process-class",
                              "StandaloneCancelPreviewExtension",
                              "--environment-root",
                              env_root.string(),
                              "--runtime-root",
                              runtime_root.string(),
                              "--stay-alive-ms",
                              std::to_string(5000 * kIpcTimingMultiplier)};
    request.heartbeat_interval_ms = 100 * kIpcTimingMultiplier;
    request.cancel_grace_period_ms = 1200 * kIpcTimingMultiplier;

    const auto prepared_session = manager.prepareSession(request);
    an::ui::extensions::ExtensionHostRunningSession running_session;
    std::string error;
    ASSERT_TRUE(manager.launchSession(prepared_session, &running_session, &error)) << error;
    SessionGuard session_guard{manager, running_session};

    const auto root = uniqueSharedBufferRootForTest("standalone-preview-cancel");
    removeAllNoThrow(root);
    const RemovePathOnExit remove_root{root};

    const auto source_image = makeMonoFloatImage("source", 5, 4, 1.0f, 0.2f);
    const auto selection_image = makeMonoFloatImage("selection", 5, 4, 0.2f, 0.05f);
    const auto weight_image = makeMonoFloatImage("weight_map", 5, 4, 0.4f, 0.08f);
    ASSERT_TRUE(source_image);
    ASSERT_TRUE(selection_image);
    ASSERT_TRUE(weight_image);

    auto source_buffer = makeImageBuffer(root, running_session.prepared_session.session_id, source_image,
                                         an::ui::extensions::SharedBufferRole::Source,
                                         an::ui::extensions::SharedBufferAccessMode::ReadOnly, "source", &error);
    auto selection_buffer = makeImageBuffer(root, running_session.prepared_session.session_id, selection_image,
                                            an::ui::extensions::SharedBufferRole::Mask,
                                            an::ui::extensions::SharedBufferAccessMode::ReadOnly, "selection", &error);
    auto weight_buffer = makeImageBuffer(root, running_session.prepared_session.session_id, weight_image,
                                         an::ui::extensions::SharedBufferRole::Weight,
                                         an::ui::extensions::SharedBufferAccessMode::ReadOnly, "weight_map", &error);
    ASSERT_TRUE(source_buffer.valid()) << error;
    ASSERT_TRUE(selection_buffer.valid()) << error;
    ASSERT_TRUE(weight_buffer.valid()) << error;

    std::string preview_request_id;
    ASSERT_TRUE(manager.sendPreviewRequest(
        &running_session, "standalone-preview-cancel-token", QCborMap{},
        {source_buffer.descriptor(), selection_buffer.descriptor(), weight_buffer.descriptor()}, &preview_request_id,
        &error))
        << error;
    ASSERT_FALSE(preview_request_id.empty());

    ASSERT_TRUE(pumpUntilStateChanges(manager, &running_session, 400 * kIpcTimingMultiplier, &error)) << error;
    ASSERT_TRUE(manager.sendCancelRequest(&running_session, nullptr, &error)) << error;

    ASSERT_TRUE(pumpUntilRequestCompletes(manager, &running_session, 4000 * kIpcTimingMultiplier, &error)) << error;
    ASSERT_TRUE(running_session.last_preview_result.has_value());
    EXPECT_EQ(running_session.last_preview_result->target_request_id, preview_request_id);
    EXPECT_EQ(running_session.last_preview_result->result_kind, "preview");
    EXPECT_EQ(running_session.last_preview_result->status, "cancelled");
    EXPECT_TRUE(running_session.last_preview_result->output_buffers.empty());

    source_buffer = an::ui::extensions::SharedImageBuffer{};
    selection_buffer = an::ui::extensions::SharedImageBuffer{};
    weight_buffer = an::ui::extensions::SharedImageBuffer{};
    removeAllNoThrow(root);
    removeAllNoThrow(temp_root);
}

// A preview that returns a cached clone of a sixteen-bit source returns its samples unchanged.
TEST(ExtensionHostManagerTest, CachedClonePreviewRoundTripsUint16PixelsFromRealExtensionHost)
{
    const std::filesystem::path host_path = extensionHostPathForCurrentAppDir();
    const std::filesystem::path runtime_root = runtimeRootForCurrentAppDir();
    const std::filesystem::path bundled_python = repositoryBundledPythonPath();
    AN_REQUIRE_FULL_EXTENSION_HOST(host_path);
    ASSERT_TRUE(std::filesystem::exists(runtime_root)) << runtime_root;
    ASSERT_FALSE(bundled_python.empty()) << "Repository bundled Python runtime must be available";

    const auto temp_root = uniqueExtensionRuntimeRootForTest("cached-clone-preview-uint16");
    removeAllNoThrow(temp_root);
    const RemovePathOnExit remove_temp_root{temp_root};
    std::filesystem::create_directories(temp_root);

    const auto env_root = temp_root / "env";
    const auto package_root = temp_root / "cached_clone_preview_ext";
    createVirtualEnvironment(bundled_python, env_root);
    if (!provisionBundledRuntimePackageIntoEnvironment(runtime_root, env_root, "numpy"))
        GTEST_SKIP() << "numpy not available in bundled runtime";

    std::filesystem::create_directories(package_root);
    writeTextFile(
        package_root / "cached_clone_preview_ext.py",
        "import afternight\n\n"
        "class CachedClonePreviewExtension(afternight.RTPreviewExtension):\n"
        "    def __init__(self, settings):\n"
        "        super().__init__(settings)\n"
        "        self._cached = None\n\n"
        "    def _cached_source(self, src_image):\n"
        "        if self._cached is None:\n"
        "            self._cached = src_image.clone()\n"
        "        return self._cached\n\n"
        "    def execute_preview(self, target, src_image, preview_image, params, progress, masks=None, weights=None):\n"
        "        preview_image.copy_from(self._cached_source(src_image))\n\n"
        "    def execute(self, target, src_image, dst_image, params, progress, masks=None, weights=None, "
        "output_masks=None):\n"
        "        dst_image.copy_from(self._cached_source(src_image))\n");

    an::ui::extensions::ExtensionHostManager manager;
    an::ui::extensions::ExtensionHostLaunchRequest request;
    request.extension_id = "an.demo.cached_clone_preview_uint16";
    request.sdk_backend = "runtime";
    request.host_executable = host_path;
    request.capabilities = {"execute", "preview"};
    request.host_arguments = {
        "--extension-package-root", package_root.string(), "--entry-point",
        "cached_clone_preview_ext", "--process-class",     "CachedClonePreviewExtension",
        "--environment-root",       env_root.string(),     "--runtime-root",
        runtime_root.string(),      "--stay-alive-ms",     std::to_string(5000 * kIpcTimingMultiplier)};

    auto prepared = manager.prepareSession(request);
    an::ui::extensions::ExtensionHostRunningSession running_session;
    std::string error;
    ASSERT_TRUE(manager.launchSession(prepared, &running_session, &error)) << error;
    SessionGuard session_guard{manager, running_session};

    const auto root = uniqueSharedBufferRootForTest("cached-clone-preview-uint16");
    removeAllNoThrow(root);
    const RemovePathOnExit remove_root{root};

    const auto source_image = makeMonoUInt16Image("source_uint16", 5, 4, 2048u, 73u);
    ASSERT_TRUE(source_image);

    auto source_buffer = makeImageBuffer(root, running_session.prepared_session.session_id, source_image,
                                         an::ui::extensions::SharedBufferRole::Source,
                                         an::ui::extensions::SharedBufferAccessMode::ReadOnly, "source", &error);
    ASSERT_TRUE(source_buffer.valid()) << error;

    std::string request_id;
    ASSERT_TRUE(manager.sendPreviewRequest(&running_session, "cached-clone-preview-uint16",
                                           {source_buffer.descriptor()}, &request_id, &error))
        << error;
    ASSERT_FALSE(request_id.empty());

    ASSERT_TRUE(pumpUntilRequestCompletes(manager, &running_session, 5000 * kIpcTimingMultiplier, &error)) << error;

    ASSERT_TRUE(running_session.last_preview_result.has_value());
    ASSERT_EQ(running_session.last_preview_result->status, "succeeded");
    ASSERT_EQ(running_session.last_preview_result->output_buffers.size(), 1u);

    an::ui::extensions::SharedBufferManager shared_buffer_manager(root);
    std::optional<an::consumer::ImageAsset> preview_image;
    ASSERT_TRUE(importBufferOrReport(shared_buffer_manager, running_session.last_preview_result->output_buffers.front(),
                                     &preview_image, &error))
        << error;
    ASSERT_TRUE(preview_image);
    ASSERT_TRUE(hasSampleType(*preview_image, an::SampleType::UInt16));
    ASSERT_EQ(widthOf(*preview_image), widthOf(*source_image));
    ASSERT_EQ(heightOf(*preview_image), heightOf(*source_image));

    for (std::size_t y = 0; y < heightOf(*source_image); ++y)
    {
        for (std::size_t x = 0; x < widthOf(*source_image); ++x)
        {
            EXPECT_EQ(sampleAt(*preview_image, x, y, 0), sampleAt(*source_image, x, y, 0))
                << "preview clone mismatch at x=" << x << " y=" << y;
        }
    }

    removeAllNoThrow(root);
    removeAllNoThrow(temp_root);
}

// A session for a backend the build does not offer is refused before any host is started. The host named is a file that
// exists - this suite's own executable - so the refusal is the backend's, and nothing is started.
TEST(ExtensionHostManagerTest, LaunchAndHandshakeRejectsUnsupportedBackendBeforeSpawn)
{
    an::ui::extensions::ExtensionHostManager manager;
    an::ui::extensions::ExtensionHostLaunchRequest request;
    request.extension_id = "an.demo";
    request.sdk_backend = "rpc";
    request.host_executable = std::filesystem::path(QCoreApplication::applicationFilePath().toStdWString());

    const auto session = manager.prepareSession(request);

    std::string error;
    EXPECT_FALSE(manager.launchAndHandshake(session, nullptr, &error));
    EXPECT_NE(error.find("unavailable"), std::string::npos) << error;
}

// A host speaking another protocol version is refused at the handshake.
TEST(ExtensionHostManagerTest, LaunchAndHandshakeRejectsProtocolVersionMismatchFromRealExtensionHost)
{
    const std::filesystem::path host_path = extensionHostPathForCurrentAppDir();
    AN_REQUIRE_FULL_EXTENSION_HOST(host_path);

    an::ui::extensions::ExtensionHostManager manager;
    const auto session = manager.prepareSession(makeLaunchRequest(
        host_path, {"--hello-protocol-version", std::to_string(an::ui::extensions::kExtensionProtocolVersion + 1)}));

    std::string error;
    EXPECT_FALSE(manager.launchAndHandshake(session, nullptr, &error));
    EXPECT_NE(error.find("protocol version"), std::string::npos);
}

// A host that cannot prove the session secret is refused at the handshake.
TEST(ExtensionHostManagerTest, LaunchAndHandshakeRejectsAuthenticationMismatchFromRealExtensionHost)
{
    const std::filesystem::path host_path = extensionHostPathForCurrentAppDir();
    AN_REQUIRE_FULL_EXTENSION_HOST(host_path);

    an::ui::extensions::ExtensionHostManager manager;
    const auto session = manager.prepareSession(makeLaunchRequest(host_path, {"--hello-secret-proof", "wrong-proof"}));

    std::string error;
    EXPECT_FALSE(manager.launchAndHandshake(session, nullptr, &error));
    EXPECT_NE(error.find("did not match"), std::string::npos);
}

// A host announcing another backend than the session's is refused at the handshake.
TEST(ExtensionHostManagerTest, LaunchAndHandshakeRejectsBackendMismatchFromRealExtensionHost)
{
    const std::filesystem::path host_path = extensionHostPathForCurrentAppDir();
    AN_REQUIRE_FULL_EXTENSION_HOST(host_path);

    an::ui::extensions::ExtensionHostManager manager;
    const auto session = manager.prepareSession(makeLaunchRequest(host_path, {"--hello-sdk-backend", "rpc"}));

    std::string error;
    EXPECT_FALSE(manager.launchAndHandshake(session, nullptr, &error));
    EXPECT_NE(error.find("Expected backend"), std::string::npos);
}

// A second connection to a session's endpoint is refused.
TEST(ExtensionHostManagerTest, LaunchAndHandshakeRejectsDuplicateConnectionsFromRealExtensionHost)
{
    const std::filesystem::path host_path = extensionHostPathForCurrentAppDir();
    AN_REQUIRE_FULL_EXTENSION_HOST(host_path);

    an::ui::extensions::ExtensionHostManager manager;
    const auto session = manager.prepareSession(makeLaunchRequest(host_path, {"--duplicate-connect"}));

    std::string error;
    EXPECT_FALSE(manager.launchAndHandshake(session, nullptr, &error));
    EXPECT_NE(error.find("duplicate"), std::string::npos);
}

// A launched session counts the full host's heartbeat answers.
TEST(ExtensionHostManagerTest, LaunchSessionProcessesHeartbeatPongFromRealExtensionHost)
{
    const std::filesystem::path host_path = extensionHostPathForCurrentAppDir();
    AN_REQUIRE_FULL_EXTENSION_HOST(host_path);

    an::ui::extensions::ExtensionHostManager manager;
    auto request = makeLaunchRequest(host_path, {"--stay-alive-ms", "250"});
    request.heartbeat_interval_ms = 40;
    request.heartbeat_miss_limit = 2;
    const auto session = manager.prepareSession(request);

    an::ui::extensions::ExtensionHostRunningSession running_session;
    std::string error;
    ASSERT_TRUE(manager.launchSession(session, &running_session, &error)) << error;
    SessionGuard guard{manager, running_session};
    EXPECT_EQ(running_session.state, an::ui::extensions::ExtensionHostSessionState::Active);

    ASSERT_TRUE(pumpUntilStateChanges(manager, &running_session, 180, &error)) << error;
    EXPECT_GT(running_session.successful_heartbeat_count, 0);
    EXPECT_EQ(running_session.state, an::ui::extensions::ExtensionHostSessionState::Active);
}

// A session whose runtime bootstraps later sends the theme and the active view once it is ready, not before.
TEST(ExtensionHostManagerTest, LaunchSessionDeliversInitialThemeAndViewMessagesToRealExtensionHost)
{
    const std::filesystem::path host_path = extensionHostPathForCurrentAppDir();
    AN_REQUIRE_FULL_EXTENSION_HOST(host_path);

    InMemorySettings settings_store;
    GlobalSettingsPersistence& settings = settings_store.repository();
    an::ui::extensions::ExtensionSdkService sdk_service(&settings);
    sdk_service.setThemeSnapshotProvider([]() {
        an::ui::extensions::ExtensionThemeSnapshot snapshot;
        snapshot.theme_name = "AfterNight Dark";
        snapshot.tokens =
            QCborMap{{QStringLiteral("colors"), QCborMap{{QStringLiteral("textPrimary"), QStringLiteral("#fffef8")}}},
                     {QStringLiteral("spacing"), QCborMap{{QStringLiteral("md"), 12}}}};
        return snapshot;
    });

    an::ui::extensions::ExtensionViewState active_view;
    active_view.view_name = "alpha";
    active_view.file_name = "/tmp/alpha.fit";
    active_view.view_mode = "standard";
    active_view.channel_view_mode = "rgb";
    active_view.zoom_percent = 150.0;
    active_view.viewport_offset_x = 8;
    active_view.viewport_offset_y = 13;
    active_view.is_active = true;
    active_view.has_image = true;

    const auto shared_buffer_root = uniqueSharedBufferRootForTest("initial-session-view");
    removeAllNoThrow(shared_buffer_root);
    const RemovePathOnExit remove_shared_buffer_root{shared_buffer_root};
    const auto initial_view_image = makeRgbGradientImage(16, 9);
    ASSERT_TRUE(initial_view_image);

    std::string setup_error;
    auto initial_view_buffer = makeImageBuffer(
        shared_buffer_root, "initial-session-view", initial_view_image, an::ui::extensions::SharedBufferRole::Preview,
        an::ui::extensions::SharedBufferAccessMode::ReadOnly, "active_view", &setup_error);
    ASSERT_TRUE(initial_view_buffer.valid()) << setup_error;

    sdk_service.setViewStateProvider(
        [active_view]() { return std::vector<an::ui::extensions::ExtensionViewState>{active_view}; });
    sdk_service.setViewSnapshotProvider(
        [&initial_view_buffer](const std::string_view session_id, const std::string_view request_id,
                               const std::string_view view_name,
                               an::ui::extensions::ExtensionViewSnapshot* out_snapshot, std::string* error) -> bool {
            if (!out_snapshot)
            {
                if (error)
                {
                    *error = "snapshot output is null";
                }
                return false;
            }

            EXPECT_FALSE(session_id.empty());
            EXPECT_FALSE(request_id.empty());
            EXPECT_EQ(view_name, "alpha");
            out_snapshot->source_buffer = initial_view_buffer.descriptor();
            return true;
        });

    an::ui::extensions::ExtensionHostManager manager;
    manager.setSdkService(&sdk_service);

    auto request = makeLaunchRequest(host_path, {"--stay-alive-ms", "500"});
    request.heartbeat_interval_ms = 100;
    request.heartbeat_miss_limit = 3;
    request.defer_runtime_bootstrap = true;
    const auto session = manager.prepareSession(request);

    an::ui::extensions::ExtensionHostRunningSession running_session;
    std::string error;
    ASSERT_TRUE(manager.launchSession(session, &running_session, &error)) << error;
    SessionGuard guard{manager, running_session};

    EXPECT_FALSE(running_session.initial_theme_state_sent);
    EXPECT_FALSE(running_session.initial_view_state_sent);

    ASSERT_TRUE(pumpUntilStateChanges(manager, &running_session, 320, &error)) << error;
    EXPECT_EQ(running_session.state, an::ui::extensions::ExtensionHostSessionState::Active);
    EXPECT_TRUE(running_session.initial_theme_state_sent);
    EXPECT_TRUE(running_session.initial_view_state_sent);

    initial_view_buffer = an::ui::extensions::SharedImageBuffer{};
    removeAllNoThrow(shared_buffer_root);
}

// The coordinator tells a running extension of theme changes, a renamed and an updated target, keeps the target's
// identity across them, and presents nothing to a target destroyed while an execute ran.
TEST(ExtensionHostManagerTest, LaunchCoordinatorPropagatesLiveViewIdentityNotifications)
{
    const std::filesystem::path host_path = extensionHostPathForCurrentAppDir();
    const std::filesystem::path runtime_root = runtimeRootForCurrentAppDir();
    const std::filesystem::path bundled_python = repositoryBundledPythonPath();
    AN_REQUIRE_FULL_EXTENSION_HOST(host_path);
    ASSERT_TRUE(std::filesystem::exists(runtime_root)) << runtime_root;
    ASSERT_FALSE(bundled_python.empty()) << "Repository bundled Python runtime must be available";

    const auto temp_root = uniqueExtensionRuntimeRootForTest("live-view-theme-notifications");
    removeAllNoThrow(temp_root);
    const RemovePathOnExit remove_temp_root{temp_root};
    std::filesystem::create_directories(temp_root);

    const auto env_root = temp_root / "env";
    const auto package_root = temp_root / "notification_probe_ext";
    std::filesystem::create_directories(package_root);
    createVirtualEnvironment(bundled_python, env_root);
    if (!provisionBundledRuntimePackageIntoEnvironment(runtime_root, env_root, "numpy"))
        GTEST_SKIP() << "numpy not available in bundled runtime";

    {
        std::ofstream out(package_root / "notification_probe_ext.py", std::ios::binary);
        ASSERT_TRUE(out.is_open());
        out << "import afternight\n"
            << "import numpy as np\n\n"
            << "class NotificationProbeExtension(afternight.ProcessExtension):\n"
            << "    def execute(self, target, src_image, dst_image, params, progress, masks=None, weights=None, "
               "output_masks=None):\n"
            << "        session = afternight.current_session()\n"
            << "        theme_tokens = afternight.ui.tokens()\n"
            << "        spacing = theme_tokens.get('spacing', {}) if isinstance(theme_tokens, dict) else {}\n"
            << "        target_metadata = dict(getattr(target, 'metadata', {}) or {})\n"
            << "        target_display_settings = dict(getattr(target, 'display_settings', {}) or {})\n"
            << "        target_source_image = getattr(target, 'source_image', None)\n"
            << "        target_masks = dict(getattr(target, 'masks', {}) or {})\n"
            << "        selection_mask = target_masks.get('selection')\n"
            << "        self.settings.set('last_session_id', session.session_id)\n"
            << "        self.settings.set('last_target_view_id', getattr(target, 'view_id', ''))\n"
            << "        self.settings.set('last_target_view_name', getattr(target, 'view_name', ''))\n"
            << "        self.settings.set('last_target_file_name', getattr(target, 'file_name', ''))\n"
            << "        self.settings.set('last_target_object', str(target_metadata.get('OBJECT', '')))\n"
            << "        self.settings.set('last_target_zoom_percent', "
               "float(target_display_settings.get('zoom_percent', -1.0)))\n"
            << "        self.settings.set('last_target_mask_enabled', bool(target_display_settings.get('mask_enabled', "
               "False)))\n"
            << "        self.settings.set('last_target_has_source_image', target_source_image is not None)\n"
            << "        self.settings.set('last_target_source_pixel_000', float(target_source_image.to_numpy()[0, 0, "
               "0]) if target_source_image is not None else -1.0)\n"
            << "        self.settings.set('last_target_has_selection_mask', selection_mask is not None)\n"
            << "        self.settings.set('last_target_selection_pixel_00', float(selection_mask.to_numpy()[0, 0]) if "
               "selection_mask is not None else -1.0)\n"
            << "        self.settings.set('last_theme_spacing_md', int(spacing.get('md', -1)))\n"
            << "        self.settings.set('last_theme_is_dark', bool(theme_tokens.get('is_dark', False)))\n"
            << "        dst_image.from_numpy(np.array(src_image.to_numpy(), copy=True))\n";
    }

    std::shared_ptr<an::consumer::ImageAsset> current_source_image = makeRgbGradientImage(5, 4);
    ASSERT_TRUE(current_source_image);

    std::optional<an::consumer::ImageAsset> updated =
        an::tests::processes::makeAsset<float>(testSession(), 5u, 4u, an::tests::processes::Channels::Rgb,
                                               [](std::uint32_t x, std::uint32_t y, std::uint32_t channel) {
                                                   return x == 0u && y == 0u && channel == 0u ? 0.875f : 0.25f;
                                               });
    ASSERT_TRUE(updated.has_value());
    const std::shared_ptr<an::consumer::ImageAsset> updated_source_image =
        std::make_shared<an::consumer::ImageAsset>(std::move(*updated));

    const std::shared_ptr<an::consumer::ImageAsset> initial_selection_image =
        makeMonoFloatImage("selection-alpha", 5, 4, 0.2f, 0.05f);
    const std::shared_ptr<an::consumer::ImageAsset> updated_selection_image =
        makeMonoFloatImage("selection-beta", 5, 4, 0.6f, 0.02f);
    ASSERT_TRUE(initial_selection_image);
    ASSERT_TRUE(updated_selection_image);

    std::string current_view_name = "alpha";
    std::string current_file_name = "/tmp/alpha.fit";
    std::string current_object_name = "M31";
    std::shared_ptr<an::consumer::ImageAsset> current_selection_image = initial_selection_image;
    int snapshot_export_count = 0;
    an::ui::extensions::ExtensionThemeSnapshot current_theme_snapshot;
    current_theme_snapshot.theme_name = "AfterNight Dark";
    current_theme_snapshot.tokens =
        QCborMap{{QStringLiteral("spacing"), QCborMap{{QStringLiteral("md"), 12}}}, {QStringLiteral("is_dark"), true}};

    InMemorySettings settings_store;
    GlobalSettingsPersistence& settings = settings_store.repository();
    an::ui::extensions::ExtensionSdkService settings_reader(&settings);
    auto& runtime_status_registry = an::ui::extensions::ExtensionRuntimeStatusRegistry::instance();
    runtime_status_registry.clear();
    an::tests::processes::ViewHandle target = targetViewOf(current_view_name, current_source_image, current_file_name);
    ASSERT_NE(target, nullptr);

    an::tests::processes::framework::CaseLaunchCoordinator coordinator;
    coordinator.configureSdkService(
        &settings,
        [&]() -> std::vector<an::ui::extensions::ExtensionViewState> {
            an::ui::extensions::ExtensionViewState view_state;
            view_state.view_name = current_view_name;
            view_state.file_name = current_file_name;
            view_state.view_mode = "standard";
            view_state.channel_view_mode = "rgb";
            view_state.zoom_percent = 125.0;
            view_state.viewport_offset_x = 3;
            view_state.viewport_offset_y = 4;
            view_state.is_active = true;
            view_state.has_image = current_source_image != nullptr;
            view_state.has_mask = current_selection_image != nullptr;
            view_state.mask_enabled = current_selection_image != nullptr;
            view_state.show_mask = current_selection_image != nullptr;
            return {view_state};
        },
        [&](const std::string_view session_id, const std::string_view request_id, const std::string_view view_name,
            an::ui::extensions::ExtensionViewSnapshot* out_snapshot, std::string* error) -> bool {
            if (!out_snapshot)
            {
                if (error)
                {
                    *error = "snapshot output is null";
                }
                return false;
            }
            if (!current_source_image)
            {
                if (error)
                {
                    *error = "current source image is unavailable";
                }
                return false;
            }

            EXPECT_FALSE(session_id.empty());
            EXPECT_FALSE(request_id.empty());
            EXPECT_EQ(view_name, current_view_name);

            an::ui::extensions::SharedBufferManager shared_buffer_manager;
            an::ui::extensions::SharedImageBuffer buffer;
            ++snapshot_export_count;
            if (!exportImageOrReport(shared_buffer_manager, *current_source_image, session_id,
                                     an::ui::extensions::SharedBufferRole::Preview, &buffer, error,
                                     std::string(request_id) + ".notification_probe",
                                     std::string(view_name) + ".snapshot"))
            {
                return false;
            }

            out_snapshot->source_buffer = buffer.descriptor();
            const auto metadata_sidecar = managedMetadataSidecarPath(out_snapshot->source_buffer);
            if (!writeMetadataSidecar(
                    metadata_sidecar,
                    QJsonObject{{QStringLiteral("OBJECT"), QString::fromStdString(current_object_name)}}, error))
            {
                return false;
            }
            out_snapshot->source_buffer.metadata_sidecar = metadata_sidecar.lexically_normal().generic_string();

            if (current_selection_image)
            {
                an::ui::extensions::SharedImageBuffer selection_buffer;
                if (!exportImageOrReport(shared_buffer_manager, *current_selection_image, session_id,
                                         an::ui::extensions::SharedBufferRole::Mask, &selection_buffer, error,
                                         std::string(request_id) + ".notification_probe.selection", "selection",
                                         an::ui::extensions::SharedBufferAccessMode::ReadOnly))
                {
                    return false;
                }

                out_snapshot->auxiliary_buffers.push_back(selection_buffer.descriptor());
            }
            return true;
        },
        [&]() -> an::ui::extensions::ExtensionThemeSnapshot { return current_theme_snapshot; });

    coordinator.setManagedImageHooks([&](ImageViewControl* control, an::consumer::ImageAsset&&, const std::string&,
                                         const std::string&) { EXPECT_EQ(control, target.get()); });

    an::ui::extensions::ExtensionHostLaunchBinding binding;
    binding.descriptor_id = "filters.notification_probe";
    binding.extension_id = "an.demo.notification_probe";
    binding.display_name = "Notification Probe";
    binding.host_executable = host_path;
    binding.capabilities = {"execute"};
    binding.host_arguments = {
        "--extension-package-root", package_root.string(), "--entry-point",
        "notification_probe_ext",   "--process-class",     "NotificationProbeExtension",
        "--environment-root",       env_root.string(),     "--runtime-root",
        runtime_root.string(),      "--stay-alive-ms",     std::to_string(5000 * kIpcTimingMultiplier)};

    std::string error;
    ASSERT_TRUE(coordinator.launchBoundDescriptor(binding, target.get(), &error)) << error;
    ASSERT_TRUE(pumpCoordinatorUntilNativeSessionReady(coordinator, binding.descriptor_id, 750 * kIpcTimingMultiplier));
    EXPECT_EQ(snapshot_export_count, 1);

    current_theme_snapshot.theme_name = "AfterNight Light";
    current_theme_snapshot.tokens =
        QCborMap{{QStringLiteral("spacing"), QCborMap{{QStringLiteral("md"), 24}}}, {QStringLiteral("is_dark"), false}};
    coordinator.handleThemeChanged();

    current_view_name = "beta";
    current_file_name = "/tmp/beta.fit";
    coordinator.handleViewControlRenamed("beta", "alpha", target.get());

    current_source_image = updated_source_image;
    current_selection_image = updated_selection_image;
    current_object_name = "M42";
    coordinator.handleViewControlUpdated(target.get());
    EXPECT_EQ(snapshot_export_count, 2);
    int notification_pumps = 0;
    ASSERT_TRUE(
        pumpCoordinatorUntil(coordinator, 1200 * kIpcTimingMultiplier, [&]() { return ++notification_pumps >= 20; }));

    an::ui::extensions::ExtensionHostExecutionDispatchResult dispatch_result;
    ASSERT_TRUE(coordinator.submitExecuteForDescriptor(binding.descriptor_id, target.get(), &dispatch_result, &error))
        << error;
    EXPECT_EQ(dispatch_result.state, an::ui::extensions::ExtensionHostExecutionDispatchState::Started);

    const auto runtimeStatusDetail = [&]() -> std::string {
        const auto snapshot = runtime_status_registry.snapshotForExtension(binding.extension_id);
        if (!snapshot.has_value())
        {
            return {};
        }

        std::string detail;
        if (snapshot->has_launch_result)
        {
            detail += " launch=";
            detail += snapshot->launch_detail;
        }
        if (snapshot->has_termination_result)
        {
            detail += " termination=";
            detail += snapshot->termination_detail;
        }
        return detail;
    };

    ASSERT_TRUE(pumpCoordinatorUntil(coordinator, 3000 * kIpcTimingMultiplier,
                                     [&]() {
                                         QCborValue poll_value;
                                         if (!settings_reader.selectExtensionSetting(
                                                 "an.demo.notification_probe", "last_target_view_name", &poll_value,
                                                 QCborValue(), nullptr))
                                         {
                                             return false;
                                         }
                                         return poll_value.toString() == QStringLiteral("beta");
                                     }))
        << "active_sessions=" << coordinator.activeSessionCount() << runtimeStatusDetail();

    QCborValue setting_value;
    ASSERT_TRUE(settings_reader.selectExtensionSetting("an.demo.notification_probe", "last_session_id", &setting_value,
                                                       QCborValue(), &error))
        << error;
    const QString first_session_id = setting_value.toString();
    EXPECT_FALSE(first_session_id.isEmpty());

    ASSERT_TRUE(settings_reader.selectExtensionSetting("an.demo.notification_probe", "last_target_view_id",
                                                       &setting_value, QCborValue(), &error))
        << error;
    EXPECT_EQ(setting_value.toString(), QStringLiteral("alpha"));

    ASSERT_TRUE(settings_reader.selectExtensionSetting("an.demo.notification_probe", "last_target_view_name",
                                                       &setting_value, QCborValue(), &error))
        << error;
    EXPECT_EQ(setting_value.toString(), QStringLiteral("beta"));

    ASSERT_TRUE(stopCoordinatorAndWaitForSessionAbsence(coordinator, 750 * kIpcTimingMultiplier));

    current_view_name = "gamma";
    current_file_name = "/tmp/gamma.fit";
    current_source_image = updated_source_image;
    current_selection_image = updated_selection_image;
    current_object_name = "NGC7000";

    an::tests::processes::framework::CaseLaunchCoordinator destroy_coordinator;
    destroy_coordinator.configureSdkService(
        &settings,
        [&]() -> std::vector<an::ui::extensions::ExtensionViewState> {
            an::ui::extensions::ExtensionViewState view_state;
            view_state.view_name = current_view_name;
            view_state.file_name = current_file_name;
            view_state.view_mode = "standard";
            view_state.channel_view_mode = "rgb";
            view_state.zoom_percent = 125.0;
            view_state.viewport_offset_x = 3;
            view_state.viewport_offset_y = 4;
            view_state.is_active = true;
            view_state.has_image = current_source_image != nullptr;
            view_state.has_mask = current_selection_image != nullptr;
            view_state.mask_enabled = current_selection_image != nullptr;
            view_state.show_mask = current_selection_image != nullptr;
            return {view_state};
        },
        [&](const std::string_view session_id, const std::string_view request_id, const std::string_view view_name,
            an::ui::extensions::ExtensionViewSnapshot* out_snapshot, std::string* error) -> bool {
            if (!out_snapshot)
            {
                if (error)
                {
                    *error = "snapshot output is null";
                }
                return false;
            }
            if (!current_source_image)
            {
                if (error)
                {
                    *error = "current source image is unavailable";
                }
                return false;
            }

            an::ui::extensions::SharedBufferManager shared_buffer_manager;
            an::ui::extensions::SharedImageBuffer buffer;
            if (!exportImageOrReport(shared_buffer_manager, *current_source_image, session_id,
                                     an::ui::extensions::SharedBufferRole::Preview, &buffer, error,
                                     std::string(request_id) + ".notification_probe",
                                     std::string(view_name) + ".snapshot"))
            {
                return false;
            }

            out_snapshot->source_buffer = buffer.descriptor();
            const auto metadata_sidecar = managedMetadataSidecarPath(out_snapshot->source_buffer);
            if (!writeMetadataSidecar(
                    metadata_sidecar,
                    QJsonObject{{QStringLiteral("OBJECT"), QString::fromStdString(current_object_name)}}, error))
            {
                return false;
            }
            out_snapshot->source_buffer.metadata_sidecar = metadata_sidecar.lexically_normal().generic_string();

            if (current_selection_image)
            {
                an::ui::extensions::SharedImageBuffer selection_buffer;
                if (!exportImageOrReport(shared_buffer_manager, *current_selection_image, session_id,
                                         an::ui::extensions::SharedBufferRole::Mask, &selection_buffer, error,
                                         std::string(request_id) + ".notification_probe.selection", "selection",
                                         an::ui::extensions::SharedBufferAccessMode::ReadOnly))
                {
                    return false;
                }

                out_snapshot->auxiliary_buffers.push_back(selection_buffer.descriptor());
            }
            return true;
        },
        [&]() -> an::ui::extensions::ExtensionThemeSnapshot { return current_theme_snapshot; });

    bool destroyed_target_result_presented = false;
    destroy_coordinator.setManagedImageHooks(
        [&](ImageViewControl* control, an::consumer::ImageAsset&&, const std::string&, const std::string&) {
            destroyed_target_result_presented = true;
            EXPECT_EQ(control, target.get());
        });

    auto destroy_binding = binding;
    destroy_binding.capabilities.push_back("keep_open");
    destroy_binding.host_arguments.push_back("--execute-delay-ms");
    destroy_binding.host_arguments.push_back(std::to_string(250 * kIpcTimingMultiplier));
    ASSERT_TRUE(destroy_coordinator.launchBoundDescriptor(destroy_binding, target.get(), &error)) << error;
    ASSERT_TRUE(
        pumpCoordinatorUntilNativeSessionReady(destroy_coordinator, binding.descriptor_id, 750 * kIpcTimingMultiplier));

    an::ui::extensions::ExtensionHostExecutionDispatchResult destroy_dispatch_result;
    ASSERT_TRUE(destroy_coordinator.submitExecuteForDescriptor(binding.descriptor_id, target.get(),
                                                               &destroy_dispatch_result, &error))
        << error;
    EXPECT_EQ(destroy_dispatch_result.state, an::ui::extensions::ExtensionHostExecutionDispatchState::Started);
    destroy_coordinator.handleViewControlDestroyed(target.get());
    EXPECT_FALSE(destroy_coordinator.hasActiveSessionForDescriptor(binding.descriptor_id, target.get()));
    EXPECT_TRUE(destroy_coordinator.hasActiveSessionForDescriptor(binding.descriptor_id, nullptr));
    int destroy_notification_pumps = 0;
    ASSERT_TRUE(pumpCoordinatorUntil(destroy_coordinator, 1200 * kIpcTimingMultiplier,
                                     [&]() { return ++destroy_notification_pumps >= 50; }));
    EXPECT_FALSE(destroyed_target_result_presented);

    ASSERT_TRUE(stopCoordinatorAndWaitForSessionAbsence(destroy_coordinator, 750 * kIpcTimingMultiplier));

    removeAllNoThrow(temp_root);
}

// The session secret reaches the host through the platform's private channel, not its command line.
TEST(ExtensionHostManagerTest, LaunchSessionConfiguresPlatformSecretTransport)
{
    const std::filesystem::path host_path = extensionHostPathForCurrentAppDir();
    AN_REQUIRE_FULL_EXTENSION_HOST(host_path);

    an::ui::extensions::ExtensionHostManager manager;
    auto request = makeLaunchRequest(host_path, {"--stay-alive-ms", "250"});
    request.heartbeat_interval_ms = 80;
    const auto session = manager.prepareSession(request);

    an::ui::extensions::ExtensionHostRunningSession running_session;
    std::string error;
    ASSERT_TRUE(manager.launchSession(session, &running_session, &error)) << error;
    SessionGuard guard{manager, running_session};

    ASSERT_NE(running_session.process, nullptr);
#ifdef _WIN32
    EXPECT_EQ(running_session.process->inputChannelMode(), QProcess::ManagedInputChannel);
    EXPECT_TRUE(static_cast<bool>(running_session.process->createProcessArgumentsModifier()));
#else
    EXPECT_EQ(running_session.process->inputChannelMode(), QProcess::ForwardedInputChannel);
#endif
}

// A session whose host closes the control channel is marked disconnected.
TEST(ExtensionHostManagerTest, LaunchSessionDetectsDisconnectedControlChannel)
{
    const std::filesystem::path host_path = extensionHostPathForCurrentAppDir();
    AN_REQUIRE_FULL_EXTENSION_HOST(host_path);

    an::ui::extensions::ExtensionHostManager manager;
    auto request = makeLaunchRequest(host_path, {"--stay-alive-ms", "250", "--disconnect-after-ack-ms", "40"});
    request.heartbeat_interval_ms = 200;
    request.heartbeat_miss_limit = 3;
    const auto session = manager.prepareSession(request);

    an::ui::extensions::ExtensionHostRunningSession running_session;
    std::string error;
    ASSERT_TRUE(manager.launchSession(session, &running_session, &error)) << error;
    SessionGuard guard{manager, running_session};

    EXPECT_FALSE(pumpUntilStateChanges(manager, &running_session, 180, &error));
    EXPECT_EQ(running_session.state, an::ui::extensions::ExtensionHostSessionState::Disconnected);
    EXPECT_NE(error.find("disconnected"), std::string::npos);
}

// A host that misses the allowed heartbeats is marked unresponsive.
TEST(ExtensionHostManagerTest, LaunchSessionMarksHostUnresponsiveAfterMissedHeartbeats)
{
    const std::filesystem::path host_path = extensionHostPathForCurrentAppDir();
    AN_REQUIRE_FULL_EXTENSION_HOST(host_path);

    an::ui::extensions::ExtensionHostManager manager;
    auto request = makeLaunchRequest(host_path, {"--stay-alive-ms", "400", "--ignore-pings"});
    request.heartbeat_interval_ms = 40;
    request.heartbeat_miss_limit = 2;
    const auto session = manager.prepareSession(request);

    an::ui::extensions::ExtensionHostRunningSession running_session;
    std::string error;
    ASSERT_TRUE(manager.launchSession(session, &running_session, &error)) << error;
    SessionGuard guard{manager, running_session};

    EXPECT_FALSE(pumpUntilStateChanges(manager, &running_session, 220, &error));
    EXPECT_EQ(running_session.state, an::ui::extensions::ExtensionHostSessionState::Unresponsive);
    EXPECT_NE(error.find("heartbeat"), std::string::npos);
    ASSERT_NE(running_session.process, nullptr);
    EXPECT_NE(running_session.process->state(), QProcess::Running);
}

// An execute request completes with the host's view commit and its default history step.
TEST(ExtensionHostManagerTest, SendExecuteRequestReceivesViewCommitFromRealExtensionHost)
{
    const std::filesystem::path host_path = extensionHostPathForCurrentAppDir();
    AN_REQUIRE_FULL_EXTENSION_HOST(host_path);

    const auto shared_buffer_root = uniqueSharedBufferRootForTest("execute_default_view_commit");
    removeAllNoThrow(shared_buffer_root);
    const RemovePathOnExit remove_shared_buffer_root{shared_buffer_root};

    an::ui::extensions::ExtensionHostManager manager;
    auto request = makeLaunchRequest(
        host_path, {"--stay-alive-ms", std::to_string(1000 * kIpcTimingMultiplier), "--execute-delay-ms", "40"});
    request.heartbeat_interval_ms = 100 * kIpcTimingMultiplier;
    const auto session = manager.prepareSession(request);

    std::string error;
    auto source_buffer = makeSourceBuffer(shared_buffer_root, session.session_id, &error);
    ASSERT_TRUE(source_buffer.valid()) << error;

    an::ui::extensions::ExtensionHostRunningSession running_session;
    ASSERT_TRUE(manager.launchSession(session, &running_session, &error)) << error;
    SessionGuard guard{manager, running_session};

    std::string request_id;
    ASSERT_TRUE(manager.sendExecuteRequest(&running_session, "params-execute", {source_buffer.descriptor()},
                                           &request_id, &error))
        << error;
    EXPECT_EQ(running_session.in_flight_request_kind, "execute");

    ASSERT_TRUE(pumpUntilRequestCompletes(manager, &running_session, 500 * kIpcTimingMultiplier, &error)) << error;
    EXPECT_FALSE(running_session.last_preview_result.has_value());
    ASSERT_TRUE(running_session.last_view_commit.has_value());
    EXPECT_EQ(running_session.last_view_commit->request_id, request_id);
    EXPECT_EQ(running_session.last_view_commit->view_id, session.session_id);
    EXPECT_EQ(running_session.last_view_commit->history_step_name, "Extension Apply");
    EXPECT_EQ(running_session.last_view_commit->history_step_description, "Applied extension execution result");

    source_buffer = an::ui::extensions::SharedImageBuffer{};
    removeAllNoThrow(shared_buffer_root);
}

// A host's settings requests are serviced by the SDK service while the session is pumped.
TEST(ExtensionHostManagerTest, PumpSessionDispatchesSettingsServiceMessagesThroughExtensionSdkService)
{
    const std::filesystem::path host_path = extensionHostPathForCurrentAppDir();
    AN_REQUIRE_FULL_EXTENSION_HOST(host_path);

    InMemorySettings settings_store;
    GlobalSettingsPersistence& settings = settings_store.repository();
    an::ui::extensions::ExtensionSdkService sdk_service(&settings);
    an::ui::extensions::ExtensionHostManager manager;
    manager.setSdkService(&sdk_service);

    auto request = makeLaunchRequest(host_path, {"--stay-alive-ms", std::to_string(1000 * kIpcTimingMultiplier)});
    request.heartbeat_interval_ms = 200 * kIpcTimingMultiplier;
    const auto session = manager.prepareSession(request);

    an::ui::extensions::ExtensionHostRunningSession running_session;
    std::string error;
    ASSERT_TRUE(manager.launchSession(session, &running_session, &error)) << error;
    SessionGuard guard{manager, running_session};

    an::ui::extensions::ExtensionProtocolMessage settings_request;
    settings_request.type = "settings.set";
    settings_request.session_id = session.session_id;
    settings_request.request_id = "settings-request-1";
    settings_request.fields = QCborMap{{QStringLiteral("key"), QStringLiteral("tools/executable_path")},
                                       {QStringLiteral("value"), QStringLiteral("/opt/demo")}};

    const QByteArray frame = an::ui::extensions::frameProtocolMessage(settings_request, &error);
    ASSERT_FALSE(frame.isEmpty()) << error;
    running_session.buffered_input.append(frame);

    ASSERT_TRUE(manager.pumpSession(&running_session, 20, &error)) << error;
    EXPECT_EQ(running_session.serviced_sdk_message_count, 1);
    EXPECT_EQ(running_session.state, an::ui::extensions::ExtensionHostSessionState::Active);
    EXPECT_EQ(settings.select(QStringLiteral("extension/an.demo/tools/executable_path")), QStringLiteral("/opt/demo"));
}

// Validation results for an interface that turned validation off are not kept.
TEST(ExtensionHostManagerTest, PumpSessionIgnoresUiValidationResultsWhenDescriptionDisablesValidation)
{
    const std::filesystem::path host_path = extensionHostPathForCurrentAppDir();
    AN_REQUIRE_FULL_EXTENSION_HOST(host_path);

    an::ui::extensions::ExtensionSdkService sdk_service;
    an::ui::extensions::ExtensionHostManager manager;
    manager.setSdkService(&sdk_service);

    auto request = makeLaunchRequest(host_path, {"--stay-alive-ms", std::to_string(1000 * kIpcTimingMultiplier)});
    request.heartbeat_interval_ms = 200 * kIpcTimingMultiplier;
    const auto session = manager.prepareSession(request);

    an::ui::extensions::ExtensionHostRunningSession running_session;
    std::string error;
    ASSERT_TRUE(manager.launchSession(session, &running_session, &error)) << error;
    SessionGuard guard{manager, running_session};

    an::ui::extensions::ExtensionProtocolMessage describe_response;
    ASSERT_TRUE(sdk_service.dispatchProtocolMessage(
        makeDeclarativeUiDescribeRequest(session.session_id, "ui-describe-disabled-validation", true, false),
        session.extension_id, session.launch_mode, &describe_response, &error))
        << error;
    ASSERT_TRUE(sdk_service.latestUiDescription().has_value());
    EXPECT_FALSE(sdk_service.latestUiDescription()->supports_ui_validation);

    an::ui::extensions::UiValidateResultMessage stale_validation_result;
    stale_validation_result.session_id = session.session_id;
    stale_validation_result.request_id = "stale-ui-validate-request";
    stale_validation_result.valid = false;
    stale_validation_result.errors = QCborMap{{QStringLiteral("strength"), QStringLiteral("stale validation")}};
    running_session.latest_ui_validation_request_id = "ui-validate-request";
    running_session.last_ui_validation_result = stale_validation_result;

    an::ui::extensions::UiValidateResultMessage validation_result;
    validation_result.session_id = session.session_id;
    validation_result.request_id = "ui-validate-request";
    validation_result.valid = false;
    validation_result.errors = QCborMap{{QStringLiteral("strength"), QStringLiteral("Must be <= 0.50")}};

    const QByteArray frame =
        an::ui::extensions::frameProtocolMessage(an::ui::extensions::toProtocolMessage(validation_result), &error);
    ASSERT_FALSE(frame.isEmpty()) << error;
    running_session.buffered_input.append(frame);

    ASSERT_TRUE(manager.pumpSession(&running_session, 20, &error)) << error;
    EXPECT_TRUE(running_session.latest_ui_validation_request_id.empty());
    EXPECT_FALSE(running_session.last_ui_validation_result.has_value());
    EXPECT_EQ(running_session.state, an::ui::extensions::ExtensionHostSessionState::Active);
}

// A confirmation waiting for the person does not stop the session's heartbeats.
TEST(ExtensionHostManagerTest, DeferredUiConfirmDoesNotBlockHeartbeatProcessing)
{
    const std::filesystem::path host_path = extensionHostPathForCurrentAppDir();
    AN_REQUIRE_FULL_EXTENSION_HOST(host_path);

    an::ui::extensions::ExtensionSdkService sdk_service;
    an::ui::extensions::ExtensionSdkUiConfirmCompletion completion;
    bool confirm_requested = false;

    an::ui::extensions::ExtensionSdkUiInteractionHandler handler;
    handler.confirm = [&](const std::string_view, const std::string_view, const QString&, const QString&, const bool,
                          an::ui::extensions::ExtensionSdkUiConfirmCompletion deferred_completion,
                          std::string*) -> bool {
        confirm_requested = true;
        completion = std::move(deferred_completion);
        return true;
    };
    sdk_service.setUiInteractionHandler(std::move(handler));

    an::ui::extensions::ExtensionHostManager manager;
    manager.setSdkService(&sdk_service);

    auto request = makeLaunchRequest(
        host_path, {"--stay-alive-ms", std::to_string(1500 * kIpcTimingMultiplier), "--emit-ui-confirm-demo"});
    request.heartbeat_interval_ms = 120 * kIpcTimingMultiplier;
    request.heartbeat_miss_limit = 3;
    const auto session = manager.prepareSession(request);

    an::ui::extensions::ExtensionHostRunningSession running_session;
    std::string error;
    ASSERT_TRUE(manager.launchSession(session, &running_session, &error)) << error;
    SessionGuard guard{manager, running_session};

    QElapsedTimer confirm_timer;
    confirm_timer.start();
    while (!confirm_requested && confirm_timer.elapsed() < 1000 * kIpcTimingMultiplier)
    {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 1);
        ASSERT_TRUE(manager.pumpSession(&running_session, 20, &error)) << error;
        QThread::msleep(20);
    }

    ASSERT_TRUE(confirm_requested);
    ASSERT_TRUE(static_cast<bool>(completion));
    EXPECT_EQ(running_session.state, an::ui::extensions::ExtensionHostSessionState::Active);
    EXPECT_EQ(running_session.serviced_sdk_message_count, 0);

    const int heartbeat_count_before_wait = running_session.successful_heartbeat_count;
    ASSERT_TRUE(pumpUntilStateChanges(manager, &running_session, 500 * kIpcTimingMultiplier, &error)) << error;
    EXPECT_EQ(running_session.state, an::ui::extensions::ExtensionHostSessionState::Active);
    EXPECT_GT(running_session.successful_heartbeat_count, heartbeat_count_before_wait);
    EXPECT_EQ(running_session.serviced_sdk_message_count, 0);

    completion(true, {});

    QElapsedTimer response_timer;
    response_timer.start();
    while (running_session.serviced_sdk_message_count == 0 && response_timer.elapsed() < 1000 * kIpcTimingMultiplier)
    {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 1);
        ASSERT_TRUE(manager.pumpSession(&running_session, 20, &error)) << error;
        QThread::msleep(20);
    }

    EXPECT_EQ(running_session.state, an::ui::extensions::ExtensionHostSessionState::Active);
    EXPECT_EQ(running_session.serviced_sdk_message_count, 1);
}

// Opening the extension's settings is answered only once the application's handler completes it.
TEST(ExtensionHostManagerTest, SdkServiceDefersOpenExtensionSettingsResult)
{
    an::ui::extensions::ExtensionSdkService sdk_service;
    an::ui::extensions::ExtensionSdkOpenExtensionSettingsCompletion completion;
    std::string requested_extension_id;

    an::ui::extensions::ExtensionSdkUiInteractionHandler handler;
    handler.open_extension_settings =
        [&](const std::string_view, const std::string_view, const std::string_view extension_id,
            an::ui::extensions::ExtensionSdkOpenExtensionSettingsCompletion deferred_completion, std::string*) -> bool {
        requested_extension_id = std::string(extension_id);
        completion = std::move(deferred_completion);
        return true;
    };
    sdk_service.setUiInteractionHandler(std::move(handler));

    an::ui::extensions::ExtensionProtocolMessage request;
    request.type = "ui.open_extension_settings";
    request.session_id = "settings-session";
    request.request_id = "settings-request";

    an::ui::extensions::ExtensionProtocolMessage response;
    std::string error;
    ASSERT_TRUE(sdk_service.dispatchProtocolMessage(
        request, "an.demo", an::ui::extensions::ExtensionLaunchMode::SingleImage, &response, &error))
        << error;
    EXPECT_EQ(requested_extension_id, "an.demo");
    ASSERT_TRUE(static_cast<bool>(completion));
    EXPECT_TRUE(sdk_service.takeDeferredProtocolResponses(request.session_id).empty());

    completion({});

    const std::vector<an::ui::extensions::ExtensionProtocolMessage> responses =
        sdk_service.takeDeferredProtocolResponses(request.session_id);
    ASSERT_EQ(responses.size(), 1u);
    EXPECT_EQ(responses[0].type, "ui.open_extension_settings.result");
    EXPECT_EQ(responses[0].request_id, request.request_id);
    EXPECT_TRUE(responses[0].fields.value(QStringLiteral("accepted")).toBool());
}

// A preview request completes with the host's preview result.
TEST(ExtensionHostManagerTest, SendPreviewRequestReceivesPreviewResultFromRealExtensionHost)
{
    const std::filesystem::path host_path = extensionHostPathForCurrentAppDir();
    AN_REQUIRE_FULL_EXTENSION_HOST(host_path);

    an::ui::extensions::ExtensionHostManager manager;
    auto request = makeLaunchRequest(
        host_path, {"--stay-alive-ms", std::to_string(2000 * kIpcTimingMultiplier), "--preview-delay-ms", "30"});
    request.heartbeat_interval_ms = 200 * kIpcTimingMultiplier;
    const auto session = manager.prepareSession(request);

    an::ui::extensions::ExtensionHostRunningSession running_session;
    std::string error;
    ASSERT_TRUE(manager.launchSession(session, &running_session, &error)) << error;
    SessionGuard guard{manager, running_session};

    ASSERT_TRUE(manager.sendPreviewRequest(&running_session, "params-preview", {}, nullptr, &error)) << error;
    ASSERT_TRUE(pumpUntilRequestCompletes(manager, &running_session, 1000 * kIpcTimingMultiplier, &error)) << error;
    ASSERT_TRUE(running_session.last_preview_result.has_value());
    EXPECT_EQ(running_session.last_preview_result->result_kind, "preview");
    EXPECT_EQ(running_session.last_preview_result->status, "succeeded");
}

// A preview of a sub-area answers a buffer that keeps the sub-area's offset and source size.
TEST(ExtensionHostManagerTest, SendPreviewRequestPreservesSubAreaDescriptorFieldsFromRealExtensionHost)
{
    const std::filesystem::path host_path = extensionHostPathForCurrentAppDir();
    AN_REQUIRE_FULL_EXTENSION_HOST(host_path);

    const auto shared_buffer_root = uniqueSharedBufferRootForTest("preview-subarea");
    removeAllNoThrow(shared_buffer_root);
    const RemovePathOnExit remove_shared_buffer_root{shared_buffer_root};

    an::ui::extensions::ExtensionHostManager manager;
    auto request = makeLaunchRequest(
        host_path, {"--stay-alive-ms", std::to_string(1000 * kIpcTimingMultiplier), "--preview-delay-ms", "20"});
    request.heartbeat_interval_ms = 100 * kIpcTimingMultiplier;
    const auto session = manager.prepareSession(request);

    std::string error;
    const auto cropped_image = makeMonoFloatImage("preview_crop", 2, 2, 1.0f, 0.25f);
    auto source_buffer = makeImageBuffer(shared_buffer_root, session.session_id, cropped_image,
                                         an::ui::extensions::SharedBufferRole::Source,
                                         an::ui::extensions::SharedBufferAccessMode::ReadOnly, "source", &error);
    ASSERT_TRUE(source_buffer.valid()) << error;

    auto source_descriptor = source_buffer.descriptor();
    source_descriptor.offset_x = 1;
    source_descriptor.offset_y = 2;
    source_descriptor.source_width = 5;
    source_descriptor.source_height = 6;

    an::ui::extensions::ExtensionHostRunningSession running_session;
    ASSERT_TRUE(manager.launchSession(session, &running_session, &error)) << error;
    SessionGuard guard{manager, running_session};

    ASSERT_TRUE(manager.sendPreviewRequest(&running_session, "params-preview", {source_descriptor}, nullptr, &error))
        << error;
    ASSERT_TRUE(pumpUntilRequestCompletes(manager, &running_session, 1000 * kIpcTimingMultiplier, &error)) << error;
    ASSERT_TRUE(running_session.last_preview_result.has_value());
    ASSERT_EQ(running_session.last_preview_result->result_kind, "preview");
    ASSERT_EQ(running_session.last_preview_result->status, "succeeded");
    ASSERT_EQ(running_session.last_preview_result->output_buffers.size(), 1u);

    const auto& output_descriptor = running_session.last_preview_result->output_buffers.front();
    EXPECT_EQ(output_descriptor.buffer_role, an::ui::extensions::SharedBufferRole::Preview);
    ASSERT_TRUE(output_descriptor.offset_x.has_value());
    ASSERT_TRUE(output_descriptor.offset_y.has_value());
    ASSERT_TRUE(output_descriptor.source_width.has_value());
    ASSERT_TRUE(output_descriptor.source_height.has_value());
    EXPECT_EQ(*output_descriptor.offset_x, 1);
    EXPECT_EQ(*output_descriptor.offset_y, 2);
    EXPECT_EQ(*output_descriptor.source_width, 5);
    EXPECT_EQ(*output_descriptor.source_height, 6);

    removeAllNoThrow(shared_buffer_root);
}

// An execute answers a destination buffer holding the source's bytes, which imports as the source's samples.
TEST(ExtensionHostManagerTest, SendExecuteRequestRoundTripsSharedBufferDescriptorsFromRealExtensionHost)
{
    const std::filesystem::path host_path = extensionHostPathForCurrentAppDir();
    AN_REQUIRE_FULL_EXTENSION_HOST(host_path);

    const auto shared_buffer_root = uniqueSharedBufferRootForTest("execute");
    removeAllNoThrow(shared_buffer_root);
    const RemovePathOnExit remove_shared_buffer_root{shared_buffer_root};

    an::ui::extensions::ExtensionHostManager manager;
    auto request = makeLaunchRequest(
        host_path, {"--stay-alive-ms", std::to_string(1000 * kIpcTimingMultiplier), "--execute-delay-ms", "40"});
    request.heartbeat_interval_ms = 100 * kIpcTimingMultiplier;
    const auto session = manager.prepareSession(request);

    std::string error;
    auto source_buffer = makeSourceBuffer(shared_buffer_root, session.session_id, &error);
    ASSERT_TRUE(source_buffer.valid()) << error;

    an::ui::extensions::ExtensionHostRunningSession running_session;
    ASSERT_TRUE(manager.launchSession(session, &running_session, &error)) << error;
    SessionGuard guard{manager, running_session};

    const std::vector<an::ui::extensions::SharedBufferDescriptor> buffers = {source_buffer.descriptor()};
    std::string request_id;
    ASSERT_TRUE(manager.sendExecuteRequest(&running_session, "params-execute", buffers, &request_id, &error)) << error;

    ASSERT_TRUE(pumpUntilRequestCompletes(manager, &running_session, 500 * kIpcTimingMultiplier, &error)) << error;
    EXPECT_FALSE(running_session.last_preview_result.has_value());
    ASSERT_TRUE(running_session.last_view_commit.has_value());
    ASSERT_EQ(running_session.last_view_commit->result_buffers.size(), 1u);

    const auto& output_descriptor = running_session.last_view_commit->result_buffers.front();
    EXPECT_EQ(output_descriptor.buffer_role, an::ui::extensions::SharedBufferRole::Destination);

    an::ui::extensions::SharedBufferManager shared_buffer_manager(shared_buffer_root);
    an::ui::extensions::SharedImageBuffer output_buffer;
    ASSERT_TRUE(openBufferOrReport(shared_buffer_manager, output_descriptor, &output_buffer, &error)) << error;
    ASSERT_TRUE(output_buffer.valid());
    ASSERT_EQ(output_buffer.mappedSize(), source_buffer.mappedSize());

    const auto* source_bytes = reinterpret_cast<const std::uint8_t*>(source_buffer.bytes());
    const auto* output_bytes = reinterpret_cast<const std::uint8_t*>(output_buffer.bytes());
    ASSERT_NE(source_bytes, nullptr);
    ASSERT_NE(output_bytes, nullptr);
    for (std::uint64_t i = 0; i < source_buffer.mappedSize(); ++i)
    {
        EXPECT_EQ(output_bytes[i], source_bytes[i]) << "byte index " << i;
    }

    std::optional<an::consumer::ImageAsset> output_image;
    ASSERT_TRUE(importBufferOrReport(shared_buffer_manager, output_descriptor, &output_image, &error)) << error;
    ASSERT_TRUE(output_image);
    EXPECT_EQ(widthOf(*output_image), 5u);
    EXPECT_EQ(heightOf(*output_image), 3u);
    EXPECT_EQ(channelsOf(*output_image), 1u);
    EXPECT_EQ(sampleAt(*output_image, 0, 0, 0), source_bytes[0]);
    EXPECT_EQ(sampleAt(*output_image, 4, 2, 0), source_bytes[14]);

    output_buffer = an::ui::extensions::SharedImageBuffer{};
    source_buffer = an::ui::extensions::SharedImageBuffer{};
    removeAllNoThrow(shared_buffer_root);
}

// An execute's view commit keeps the history step the host names.
TEST(ExtensionHostManagerTest, SendExecuteRequestCapturesViewCommitFromRealExtensionHost)
{
    const std::filesystem::path host_path = extensionHostPathForCurrentAppDir();
    AN_REQUIRE_FULL_EXTENSION_HOST(host_path);

    const auto shared_buffer_root = uniqueSharedBufferRootForTest("execute_view_commit");
    removeAllNoThrow(shared_buffer_root);
    const RemovePathOnExit remove_shared_buffer_root{shared_buffer_root};

    const std::string expected_step_name = "View Commit Apply";
    const std::string expected_step_description = "Applied via view.commit";

    an::ui::extensions::ExtensionHostManager manager;
    auto request =
        makeLaunchRequest(host_path, {"--stay-alive-ms", std::to_string(1000 * kIpcTimingMultiplier),
                                      "--execute-delay-ms", "40", "--view-commit-history-step-name", expected_step_name,
                                      "--view-commit-history-step-description", expected_step_description});
    request.heartbeat_interval_ms = 100 * kIpcTimingMultiplier;
    const auto session = manager.prepareSession(request);

    std::string error;
    auto source_buffer = makeSourceBuffer(shared_buffer_root, session.session_id, &error);
    ASSERT_TRUE(source_buffer.valid()) << error;

    an::ui::extensions::ExtensionHostRunningSession running_session;
    ASSERT_TRUE(manager.launchSession(session, &running_session, &error)) << error;
    SessionGuard guard{manager, running_session};

    std::string request_id;
    ASSERT_TRUE(manager.sendExecuteRequest(&running_session, "params-execute", {source_buffer.descriptor()},
                                           &request_id, &error))
        << error;

    ASSERT_TRUE(pumpUntilRequestCompletes(manager, &running_session, 500 * kIpcTimingMultiplier, &error)) << error;
    EXPECT_FALSE(running_session.last_preview_result.has_value());
    ASSERT_TRUE(running_session.last_view_commit.has_value());
    EXPECT_EQ(running_session.last_view_commit->request_id, request_id);
    EXPECT_EQ(running_session.last_view_commit->view_id, session.session_id);
    EXPECT_EQ(running_session.last_view_commit->history_step_name, expected_step_name);
    EXPECT_EQ(running_session.last_view_commit->history_step_description, expected_step_description);
    ASSERT_EQ(running_session.last_view_commit->result_buffers.size(), 1u);
    EXPECT_EQ(running_session.last_view_commit->result_buffers.front().buffer_role,
              an::ui::extensions::SharedBufferRole::Destination);

    source_buffer = an::ui::extensions::SharedImageBuffer{};
    removeAllNoThrow(shared_buffer_root);
}

// A success result arriving after the view commit fails the session: the commit is the only completion.
TEST(ExtensionHostManagerTest, SendExecuteRequestRejectsTrailingSuccessExecutionResultAfterViewCommit)
{
    const std::filesystem::path host_path = extensionHostPathForCurrentAppDir();
    AN_REQUIRE_FULL_EXTENSION_HOST(host_path);

    const auto shared_buffer_root = uniqueSharedBufferRootForTest("execute_view_commit_compat");
    removeAllNoThrow(shared_buffer_root);
    const RemovePathOnExit remove_shared_buffer_root{shared_buffer_root};

    const std::string expected_step_name = "Commit Wins";
    const std::string expected_step_description = "view.commit remains authoritative";

    an::ui::extensions::ExtensionHostManager manager;
    auto request =
        makeLaunchRequest(host_path, {"--stay-alive-ms", std::to_string(1000 * kIpcTimingMultiplier),
                                      "--execute-delay-ms", "40", "--view-commit-history-step-name", expected_step_name,
                                      "--view-commit-history-step-description", expected_step_description});
    request.heartbeat_interval_ms = 100 * kIpcTimingMultiplier;
    const auto session = manager.prepareSession(request);

    std::string error;
    auto source_buffer = makeSourceBuffer(shared_buffer_root, session.session_id, &error);
    ASSERT_TRUE(source_buffer.valid()) << error;

    an::ui::extensions::ExtensionHostRunningSession running_session;
    ASSERT_TRUE(manager.launchSession(session, &running_session, &error)) << error;
    SessionGuard guard{manager, running_session};

    std::string request_id;
    ASSERT_TRUE(manager.sendExecuteRequest(&running_session, "params-execute-compat", {source_buffer.descriptor()},
                                           &request_id, &error))
        << error;

    ASSERT_TRUE(pumpUntilRequestCompletes(manager, &running_session, 500 * kIpcTimingMultiplier, &error)) << error;
    EXPECT_FALSE(running_session.last_preview_result.has_value());
    ASSERT_TRUE(running_session.last_view_commit.has_value());
    EXPECT_EQ(running_session.completed_request_count, 1);
    EXPECT_EQ(running_session.last_view_commit->request_id, request_id);
    EXPECT_EQ(running_session.last_view_commit->history_step_name, expected_step_name);
    EXPECT_EQ(running_session.last_view_commit->history_step_description, expected_step_description);

    an::ui::extensions::ExecutionResultMessage trailing_result;
    trailing_result.session_id = session.session_id;
    trailing_result.request_id = "late-success-after-commit";
    trailing_result.target_request_id = request_id;
    trailing_result.result_kind = "execute";
    trailing_result.status = "succeeded";
    trailing_result.detail = "Processed execute for params-execute-compat";
    trailing_result.history_step_name = expected_step_name;
    trailing_result.history_step_description = expected_step_description;
    trailing_result.output_buffers = running_session.last_view_commit->result_buffers;

    const QByteArray frame =
        an::ui::extensions::frameProtocolMessage(an::ui::extensions::toProtocolMessage(trailing_result), &error);
    ASSERT_FALSE(frame.isEmpty()) << error;
    running_session.buffered_input.append(frame);

    EXPECT_FALSE(manager.pumpSession(&running_session, 20, &error));
    EXPECT_EQ(running_session.state, an::ui::extensions::ExtensionHostSessionState::Failed);
    EXPECT_NE(error.find("view.commit"), std::string::npos);

    source_buffer = an::ui::extensions::SharedImageBuffer{};
    removeAllNoThrow(shared_buffer_root);
}

// An execute that completes with no view commit fails the session, saying so.
TEST(ExtensionHostManagerTest, SendExecuteRequestRejectsExecuteCompletionWithoutViewCommitAtSessionBoundary)
{
    const std::filesystem::path host_path = extensionHostPathForCurrentAppDir();
    AN_REQUIRE_FULL_EXTENSION_HOST(host_path);

    const auto shared_buffer_root = uniqueSharedBufferRootForTest("execute_bad_role");
    removeAllNoThrow(shared_buffer_root);
    const RemovePathOnExit remove_shared_buffer_root{shared_buffer_root};

    an::ui::extensions::ExtensionHostManager manager;
    auto request = makeLaunchRequest(host_path, {"--stay-alive-ms", std::to_string(1000 * kIpcTimingMultiplier),
                                                 "--execute-delay-ms", "40", "--result-buffer-role", "mask"});
    request.heartbeat_interval_ms = 100 * kIpcTimingMultiplier;
    const auto session = manager.prepareSession(request);

    std::string error;
    auto source_buffer = makeSourceBuffer(shared_buffer_root, session.session_id, &error);
    ASSERT_TRUE(source_buffer.valid()) << error;

    an::ui::extensions::ExtensionHostRunningSession running_session;
    ASSERT_TRUE(manager.launchSession(session, &running_session, &error)) << error;
    SessionGuard guard{manager, running_session};

    const std::vector<an::ui::extensions::SharedBufferDescriptor> buffers = {source_buffer.descriptor()};
    ASSERT_TRUE(manager.sendExecuteRequest(&running_session, "params-execute", buffers, nullptr, &error)) << error;

    EXPECT_FALSE(pumpUntilRequestCompletes(manager, &running_session, 500 * kIpcTimingMultiplier, &error));
    EXPECT_EQ(running_session.state, an::ui::extensions::ExtensionHostSessionState::Failed);
    EXPECT_FALSE(running_session.last_preview_result.has_value());
    EXPECT_NE(error.find("view.commit"), std::string::npos);

    source_buffer = an::ui::extensions::SharedImageBuffer{};
    removeAllNoThrow(shared_buffer_root);
}

// A large colour Float32 image crosses to the host and back with its samples intact.
TEST(ExtensionHostManagerTest, SendExecuteRequestRoundTripsLargeRgbFloatBufferFromRealExtensionHost)
{
    const std::filesystem::path host_path = extensionHostPathForCurrentAppDir();
    AN_REQUIRE_FULL_EXTENSION_HOST(host_path);

    const auto shared_buffer_root = uniqueSharedBufferRootForTest("execute_large_rgb");
    removeAllNoThrow(shared_buffer_root);
    const RemovePathOnExit remove_shared_buffer_root{shared_buffer_root};

    an::ui::extensions::ExtensionHostManager manager;
    auto request = makeLaunchRequest(
        host_path, {"--stay-alive-ms", std::to_string(1500 * kIpcTimingMultiplier), "--execute-delay-ms", "40"});
    request.heartbeat_interval_ms = 100 * kIpcTimingMultiplier;
    const auto session = manager.prepareSession(request);

    std::string error;
    constexpr uint32_t kWidth = 2048;
    constexpr uint32_t kHeight = 1536;
    std::shared_ptr<an::consumer::ImageAsset> source_image;
    auto source_buffer =
        makeLargeRgbSourceBuffer(shared_buffer_root, session.session_id, kWidth, kHeight, &source_image, &error);
    ASSERT_TRUE(source_buffer.valid()) << error;
    ASSERT_TRUE(source_image);

    an::ui::extensions::ExtensionHostRunningSession running_session;
    ASSERT_TRUE(manager.launchSession(session, &running_session, &error)) << error;
    SessionGuard guard{manager, running_session};

    const std::vector<an::ui::extensions::SharedBufferDescriptor> buffers = {source_buffer.descriptor()};
    ASSERT_TRUE(manager.sendExecuteRequest(&running_session, "params-large-execute", buffers, nullptr, &error))
        << error;

    ASSERT_TRUE(pumpUntilRequestCompletes(manager, &running_session, 2000 * kIpcTimingMultiplier, &error)) << error;
    EXPECT_FALSE(running_session.last_preview_result.has_value());
    ASSERT_TRUE(running_session.last_view_commit.has_value());
    ASSERT_EQ(running_session.last_view_commit->result_buffers.size(), 1u);

    const auto& output_descriptor = running_session.last_view_commit->result_buffers.front();
    EXPECT_EQ(output_descriptor.width, static_cast<int>(kWidth));
    EXPECT_EQ(output_descriptor.height, static_cast<int>(kHeight));
    EXPECT_EQ(output_descriptor.channels, 3);
    EXPECT_EQ(output_descriptor.pixel_type, an::ui::extensions::SharedBufferPixelType::Float32);
    EXPECT_EQ(output_descriptor.pixel_format, an::ui::extensions::SharedBufferPixelFormat::RGB);

    an::ui::extensions::SharedBufferManager shared_buffer_manager(shared_buffer_root);
    std::optional<an::consumer::ImageAsset> output_image;
    ASSERT_TRUE(importBufferOrReport(shared_buffer_manager, output_descriptor, &output_image, &error)) << error;
    ASSERT_TRUE(output_image);
    EXPECT_EQ(widthOf(*output_image), kWidth);
    EXPECT_EQ(heightOf(*output_image), kHeight);
    EXPECT_EQ(channelsOf(*output_image), 3u);

    const std::array<std::pair<uint32_t, uint32_t>, 4> sample_points = {
        {{0u, 0u}, {kWidth / 2u, kHeight / 2u}, {kWidth - 1u, kHeight - 1u}, {211u, 977u}}};
    for (const auto& [x, y] : sample_points)
    {
        EXPECT_NEAR(sampleAt(*output_image, x, y, 0), sampleAt(*source_image, x, y, 0), 1e-6)
            << "red sample at " << x << "," << y;
        EXPECT_NEAR(sampleAt(*output_image, x, y, 1), sampleAt(*source_image, x, y, 1), 1e-6)
            << "green sample at " << x << "," << y;
        EXPECT_NEAR(sampleAt(*output_image, x, y, 2), sampleAt(*source_image, x, y, 2), 1e-6)
            << "blue sample at " << x << "," << y;
    }

    source_buffer = an::ui::extensions::SharedImageBuffer{};
    removeAllNoThrow(shared_buffer_root);
}

// A cancelled execute that sends no view commit fails the session.
TEST(ExtensionHostManagerTest, SendCancelRequestRejectsExecuteCancelledResultWithoutViewCommit)
{
    const std::filesystem::path host_path = extensionHostPathForCurrentAppDir();
    AN_REQUIRE_FULL_EXTENSION_HOST(host_path);

    an::ui::extensions::ExtensionHostManager manager;
    auto request = makeLaunchRequest(
        host_path, {"--stay-alive-ms", std::to_string(1000 * kIpcTimingMultiplier), "--execute-delay-ms", "150"});
    request.heartbeat_interval_ms = 100 * kIpcTimingMultiplier;
    request.cancel_grace_period_ms = 200 * kIpcTimingMultiplier;
    const auto session = manager.prepareSession(request);

    an::ui::extensions::ExtensionHostRunningSession running_session;
    std::string error;
    ASSERT_TRUE(manager.launchSession(session, &running_session, &error)) << error;
    SessionGuard guard{manager, running_session};

    std::string execute_request_id;
    ASSERT_TRUE(manager.sendExecuteRequest(&running_session, "params-execute", {}, &execute_request_id, &error))
        << error;
    ASSERT_FALSE(execute_request_id.empty());
    ASSERT_TRUE(manager.sendCancelRequest(&running_session, nullptr, &error)) << error;
    EXPECT_FALSE(pumpUntilRequestCompletes(manager, &running_session, 500 * kIpcTimingMultiplier, &error));
    EXPECT_EQ(running_session.state, an::ui::extensions::ExtensionHostSessionState::Failed);
    EXPECT_FALSE(running_session.last_preview_result.has_value());
    EXPECT_NE(error.find("view.commit"), std::string::npos);
}

// An execute that completes with neither a view commit nor an output buffer fails the session.
TEST(ExtensionHostManagerTest, SendExecuteRequestRejectsExecuteCompletionWithoutViewCommitWhenNoOutputBuffersExist)
{
    const std::filesystem::path host_path = extensionHostPathForCurrentAppDir();
    AN_REQUIRE_FULL_EXTENSION_HOST(host_path);

    an::ui::extensions::ExtensionHostManager manager;
    auto request = makeLaunchRequest(
        host_path, {"--stay-alive-ms", std::to_string(1000 * kIpcTimingMultiplier), "--execute-delay-ms", "40"});
    request.heartbeat_interval_ms = 100 * kIpcTimingMultiplier;
    const auto session = manager.prepareSession(request);

    an::ui::extensions::ExtensionHostRunningSession running_session;
    std::string error;
    ASSERT_TRUE(manager.launchSession(session, &running_session, &error)) << error;
    SessionGuard guard{manager, running_session};

    std::string execute_request_id;
    ASSERT_TRUE(manager.sendExecuteRequest(&running_session, "params-execute", {}, &execute_request_id, &error))
        << error;
    ASSERT_FALSE(execute_request_id.empty());

    EXPECT_FALSE(pumpUntilRequestCompletes(manager, &running_session, 500 * kIpcTimingMultiplier, &error));
    EXPECT_EQ(running_session.state, an::ui::extensions::ExtensionHostSessionState::Failed);
    EXPECT_FALSE(running_session.last_preview_result.has_value());
    EXPECT_NE(error.find("view.commit"), std::string::npos);
}

// A host that ignores a cancellation past its grace period is stopped.
TEST(ExtensionHostManagerTest, SendCancelRequestEscalatesWhenHostIgnoresCancelGracePeriod)
{
    const std::filesystem::path host_path = extensionHostPathForCurrentAppDir();
    AN_REQUIRE_FULL_EXTENSION_HOST(host_path);

    an::ui::extensions::ExtensionHostManager manager;
    auto request = makeLaunchRequest(host_path, {"--stay-alive-ms", std::to_string(3000 * kIpcTimingMultiplier),
                                                 "--execute-delay-ms", std::to_string(1200 * kIpcTimingMultiplier),
                                                 "--ignore-cancel"});
    request.heartbeat_interval_ms = 200 * kIpcTimingMultiplier;
    request.cancel_grace_period_ms = 200 * kIpcTimingMultiplier;
    const auto session = manager.prepareSession(request);

    an::ui::extensions::ExtensionHostRunningSession running_session;
    std::string error;
    ASSERT_TRUE(manager.launchSession(session, &running_session, &error)) << error;
    SessionGuard guard{manager, running_session};

    ASSERT_TRUE(manager.sendExecuteRequest(&running_session, "params-execute", {}, nullptr, &error)) << error;
    ASSERT_TRUE(manager.sendCancelRequest(&running_session, nullptr, &error)) << error;
    EXPECT_FALSE(pumpUntilStateChanges(manager, &running_session, 800 * kIpcTimingMultiplier, &error));
    EXPECT_EQ(running_session.state, an::ui::extensions::ExtensionHostSessionState::Failed);
    EXPECT_NE(error.find("grace period"), std::string::npos);
}

// An execute beyond the concurrent limit is queued and starts when a running one completes.
TEST(ExtensionHostManagerTest, SubmitExecuteOrQueueQueuesWhenConcurrentLimitIsReached)
{
    const std::filesystem::path host_path = extensionHostPathForCurrentAppDir();
    AN_REQUIRE_FULL_EXTENSION_HOST(host_path);

    an::ui::extensions::ExtensionHostManager manager;
    auto request_a = makeLaunchRequest(
        host_path, {"--stay-alive-ms", std::to_string(1500 * kIpcTimingMultiplier), "--execute-delay-ms", "140"});
    auto request_b = makeLaunchRequest(
        host_path, {"--stay-alive-ms", std::to_string(1500 * kIpcTimingMultiplier), "--execute-delay-ms", "40"});
    request_a.heartbeat_interval_ms = 100 * kIpcTimingMultiplier;
    request_b.heartbeat_interval_ms = 100 * kIpcTimingMultiplier;

    an::ui::extensions::ExtensionHostRunningSession session_a;
    an::ui::extensions::ExtensionHostRunningSession session_b;
    std::string error;
    ASSERT_TRUE(manager.launchSession(manager.prepareSession(request_a), &session_a, &error)) << error;
    ASSERT_TRUE(manager.launchSession(manager.prepareSession(request_b), &session_b, &error)) << error;
    SessionGuard guard_a{manager, session_a};
    SessionGuard guard_b{manager, session_b};

    const auto shared_buffer_root = uniqueSharedBufferRootForTest("queued_execute");
    removeAllNoThrow(shared_buffer_root);
    const RemovePathOnExit remove_shared_buffer_root{shared_buffer_root};
    auto source_buffer_a = makeSourceBuffer(shared_buffer_root, session_a.prepared_session.session_id, &error);
    ASSERT_TRUE(source_buffer_a.valid()) << error;
    auto source_buffer_b = makeSourceBuffer(shared_buffer_root, session_b.prepared_session.session_id, &error);
    ASSERT_TRUE(source_buffer_b.valid()) << error;

    an::ui::extensions::ExtensionHostExecutionGateState gate_state;
    gate_state.max_concurrent_executions = 1;

    an::ui::extensions::ExtensionHostExecutionDispatchResult dispatch_a;
    ASSERT_TRUE(manager.submitExecuteOrQueue(&gate_state, &session_a, "params-a", {source_buffer_a.descriptor()},
                                             &dispatch_a, &error))
        << error;
    EXPECT_EQ(dispatch_a.state, an::ui::extensions::ExtensionHostExecutionDispatchState::Started);
    EXPECT_FALSE(dispatch_a.request_id.empty());

    an::ui::extensions::ExtensionHostExecutionDispatchResult dispatch_b;
    ASSERT_TRUE(manager.submitExecuteOrQueue(&gate_state, &session_b, "params-b", {source_buffer_b.descriptor()},
                                             &dispatch_b, &error))
        << error;
    EXPECT_EQ(dispatch_b.state, an::ui::extensions::ExtensionHostExecutionDispatchState::Queued);
    EXPECT_FALSE(dispatch_b.queue_id.empty());
    EXPECT_EQ(dispatch_b.queue_position, 1);
    EXPECT_TRUE(session_b.in_flight_request_id.empty());
    EXPECT_EQ(gate_state.queued_requests.size(), 1u);

    ASSERT_TRUE(pumpManagedSessions(manager, &gate_state, {&session_a, &session_b}, 500 * kIpcTimingMultiplier, &error))
        << error;
    ASSERT_TRUE(session_a.last_view_commit.has_value());
    ASSERT_TRUE(session_b.last_view_commit.has_value());
    EXPECT_TRUE(gate_state.queued_requests.empty());
    EXPECT_FALSE(session_b.last_view_commit->request_id.empty());

    source_buffer_a = an::ui::extensions::SharedImageBuffer{};
    source_buffer_b = an::ui::extensions::SharedImageBuffer{};
    removeAllNoThrow(shared_buffer_root);
}

// Previews and executes share one execution gate.
TEST(ExtensionHostManagerTest, SubmitPreviewOrQueueUsesSameExecutionLaneAsExecute)
{
    const std::filesystem::path host_path = extensionHostPathForCurrentAppDir();
    AN_REQUIRE_FULL_EXTENSION_HOST(host_path);

    an::ui::extensions::ExtensionHostManager manager;
    auto request_a = makeLaunchRequest(
        host_path, {"--stay-alive-ms", std::to_string(1500 * kIpcTimingMultiplier), "--execute-delay-ms", "120"});
    auto request_b = makeLaunchRequest(
        host_path, {"--stay-alive-ms", std::to_string(1500 * kIpcTimingMultiplier), "--preview-delay-ms", "30"});

    an::ui::extensions::ExtensionHostRunningSession session_a;
    an::ui::extensions::ExtensionHostRunningSession session_b;
    std::string error;
    ASSERT_TRUE(manager.launchSession(manager.prepareSession(request_a), &session_a, &error)) << error;
    ASSERT_TRUE(manager.launchSession(manager.prepareSession(request_b), &session_b, &error)) << error;
    SessionGuard guard_a{manager, session_a};
    SessionGuard guard_b{manager, session_b};

    const auto shared_buffer_root = uniqueSharedBufferRootForTest("queued_execute_preview_lane");
    removeAllNoThrow(shared_buffer_root);
    const RemovePathOnExit remove_shared_buffer_root{shared_buffer_root};
    auto source_buffer_a = makeSourceBuffer(shared_buffer_root, session_a.prepared_session.session_id, &error);
    ASSERT_TRUE(source_buffer_a.valid()) << error;

    an::ui::extensions::ExtensionHostExecutionGateState gate_state;
    gate_state.max_concurrent_executions = 1;

    an::ui::extensions::ExtensionHostExecutionDispatchResult execute_dispatch;
    ASSERT_TRUE(manager.submitExecuteOrQueue(&gate_state, &session_a, "params-a", {source_buffer_a.descriptor()},
                                             &execute_dispatch, &error))
        << error;
    an::ui::extensions::ExtensionHostExecutionDispatchResult preview_dispatch;
    ASSERT_TRUE(manager.submitPreviewOrQueue(&gate_state, &session_b, "params-preview", {}, &preview_dispatch, &error))
        << error;
    EXPECT_EQ(preview_dispatch.state, an::ui::extensions::ExtensionHostExecutionDispatchState::Queued);

    ASSERT_TRUE(pumpManagedSessions(manager, &gate_state, {&session_a, &session_b}, 500 * kIpcTimingMultiplier, &error))
        << error;
    ASSERT_TRUE(session_a.last_view_commit.has_value());
    ASSERT_TRUE(session_b.last_preview_result.has_value());
    EXPECT_TRUE(gate_state.queued_requests.empty());
    EXPECT_EQ(session_b.last_preview_result->result_kind, "preview");

    source_buffer_a = an::ui::extensions::SharedImageBuffer{};
    removeAllNoThrow(shared_buffer_root);
}

// With queueing off, an execute beyond the concurrent limit is refused.
TEST(ExtensionHostManagerTest, SubmitExecuteOrQueueRejectsWhenQueueingIsDisabled)
{
    const std::filesystem::path host_path = extensionHostPathForCurrentAppDir();
    AN_REQUIRE_FULL_EXTENSION_HOST(host_path);

    an::ui::extensions::ExtensionHostManager manager;
    auto request_a = makeLaunchRequest(
        host_path, {"--stay-alive-ms", std::to_string(1000 * kIpcTimingMultiplier), "--execute-delay-ms", "120"});
    auto request_b = makeLaunchRequest(
        host_path, {"--stay-alive-ms", std::to_string(1000 * kIpcTimingMultiplier), "--execute-delay-ms", "40"});

    an::ui::extensions::ExtensionHostRunningSession session_a;
    an::ui::extensions::ExtensionHostRunningSession session_b;
    std::string error;
    ASSERT_TRUE(manager.launchSession(manager.prepareSession(request_a), &session_a, &error)) << error;
    ASSERT_TRUE(manager.launchSession(manager.prepareSession(request_b), &session_b, &error)) << error;
    SessionGuard guard_a{manager, session_a};
    SessionGuard guard_b{manager, session_b};

    an::ui::extensions::ExtensionHostExecutionGateState gate_state;
    gate_state.max_concurrent_executions = 1;
    gate_state.queue_when_saturated = false;

    an::ui::extensions::ExtensionHostExecutionDispatchResult dispatch_a;
    ASSERT_TRUE(manager.submitExecuteOrQueue(&gate_state, &session_a, "params-a", {}, &dispatch_a, &error)) << error;

    an::ui::extensions::ExtensionHostExecutionDispatchResult dispatch_b;
    EXPECT_FALSE(manager.submitExecuteOrQueue(&gate_state, &session_b, "params-b", {}, &dispatch_b, &error));
    EXPECT_EQ(dispatch_b.state, an::ui::extensions::ExtensionHostExecutionDispatchState::Rejected);
    EXPECT_NE(error.find("disabled"), std::string::npos);
}

// A descriptor whose host executable is missing is unavailable, saying so, and has no launch action.
TEST(ExtensionHostManagerTest, BindSingleImageDescriptorMarksDescriptorUnavailableWhenHostBinaryIsMissing)
{
    an::tests::processes::framework::CaseLaunchCoordinator coordinator;
    an::ui::process_registry::ProcessDescriptor descriptor;
    descriptor.id = "filters.demo";
    descriptor.display_name = "Demo";
    descriptor.category = "filters";
    descriptor.action_object_name = "actionDemo";
    descriptor.process_name = "Demo";
    descriptor.availability = an::ui::process_registry::ProcessAvailability::Available;

    an::ui::extensions::ExtensionHostLaunchBinding binding;
    binding.descriptor_id = descriptor.id;
    binding.extension_id = "an.demo";
    binding.display_name = descriptor.display_name;
    binding.host_executable = std::filesystem::temp_directory_path() / "missing_afternight_extension_host";

    coordinator.bindSingleImageDescriptor(&descriptor, binding);

    EXPECT_EQ(descriptor.availability, an::ui::process_registry::ProcessAvailability::Unavailable);
    EXPECT_NE(descriptor.availability_reason.find("host executable"), std::string::npos);
    EXPECT_FALSE(static_cast<bool>(descriptor.launch_single_image_checked));
}

// A bound descriptor's launch action starts one tracked session for its target.
TEST(ExtensionHostManagerTest, BindSingleImageDescriptorLaunchesAndTracksRealExtensionHostSession)
{
    const std::filesystem::path host_path = extensionHostPathForCurrentAppDir();
    AN_REQUIRE_FULL_EXTENSION_HOST(host_path);

    an::tests::processes::framework::CaseLaunchCoordinator coordinator;
    coordinator.setManagedViewNameProvider([](ImageViewControl* control) {
        EXPECT_EQ(control, fakeTargetControl());
        return std::string("launch-target");
    });
    an::ui::process_registry::ProcessDescriptor descriptor;
    descriptor.id = "filters.demo";
    descriptor.display_name = "Demo";
    descriptor.category = "filters";
    descriptor.action_object_name = "actionDemo";
    descriptor.process_name = "Demo";
    descriptor.availability = an::ui::process_registry::ProcessAvailability::Available;

    an::ui::extensions::ExtensionHostLaunchBinding binding;
    binding.descriptor_id = descriptor.id;
    binding.extension_id = "an.demo";
    binding.display_name = descriptor.display_name;
    binding.host_executable = host_path;
    binding.capabilities = {"execute", "preview"};

    coordinator.bindSingleImageDescriptor(&descriptor, binding);
    ASSERT_TRUE(static_cast<bool>(descriptor.launch_single_image_checked));

    std::string error;
    ASSERT_TRUE(descriptor.launch_single_image_checked(fakeTargetControl(), &error)) << error;
    ASSERT_TRUE(pumpCoordinatorUntilNativeSessionReady(coordinator, descriptor.id, 750));
    EXPECT_TRUE(coordinator.hasActiveSessionForDescriptor(descriptor.id, fakeTargetControl()));
    EXPECT_EQ(coordinator.activeSessionCount(), 1u);

    for (int i = 0; i < 5; ++i)
    {
        coordinator.pumpSessions();
        QThread::msleep(20);
    }

    EXPECT_EQ(coordinator.activeSessionCount(), 1u);
    coordinator.stopAllSessions();
    ASSERT_TRUE(pumpCoordinatorUntilSessionCount(coordinator, 0u, 500));
    EXPECT_EQ(coordinator.activeSessionCount(), 0u);
}

// A native launch returns before the extension's parameters are read, refuses an execute until they are, installs
// the view it was launched for and reads the parameters once.
TEST(ExtensionHostManagerTest, NativeProcessLaunchEvaluatesParamSchemaOnlyOnce)
{
    const std::filesystem::path host_path = extensionHostPathForCurrentAppDir();
    const std::filesystem::path runtime_root = runtimeRootForCurrentAppDir();
    const std::filesystem::path bundled_python = repositoryBundledPythonPath();
    AN_REQUIRE_FULL_EXTENSION_HOST(host_path);
    ASSERT_FALSE(runtime_root.empty()) << "Deployed PythonLib runtime root must be available";
    ASSERT_TRUE(std::filesystem::exists(runtime_root)) << runtime_root;
    ASSERT_FALSE(bundled_python.empty()) << "Repository bundled Python runtime must be available";
    ASSERT_TRUE(std::filesystem::exists(bundled_python)) << bundled_python;

    const auto temp_root = uniqueExtensionRuntimeRootForTest("auto-ui-schema-cache");
    removeAllNoThrow(temp_root);
    RemovePathOnExit temp_root_guard{temp_root};
    std::error_code ec;
    std::filesystem::create_directories(temp_root, ec);
    ASSERT_FALSE(static_cast<bool>(ec)) << "Failed to create temp extension root: " << ec.message();

    const std::filesystem::path env_root = temp_root / "env";
    createVirtualEnvironment(bundled_python, env_root);

    const std::filesystem::path package_root = temp_root / "single_schema_inspection_extension";
    writeTextFile(package_root / "single_schema_inspection_extension.py",
                  R"PY(from pathlib import Path
import time

from afternight import ui


CALL_COUNT_PATH = Path(__file__).with_name("get_params_call_count.txt")
SCHEMA_STARTED_PATH = Path(__file__).with_name("get_params_started.txt")
SCHEMA_RELEASE_PATH = Path(__file__).with_name("release_get_params.txt")
SCHEMA_WAIT_TIMEOUT_PATH = Path(__file__).with_name("get_params_wait_timed_out.txt")
SCHEMA_COMPLETED_PATH = Path(__file__).with_name("get_params_completed.txt")
INPUT_HOOK_PATH = Path(__file__).with_name("input_hook_view_ids.txt")


class SingleSchemaInspectionExtension(ui.ProcessWindow):
    def get_params(self):
        call_count = int(CALL_COUNT_PATH.read_text(encoding="utf-8")) if CALL_COUNT_PATH.exists() else 0
        CALL_COUNT_PATH.write_text(str(call_count + 1), encoding="utf-8")
        SCHEMA_STARTED_PATH.write_text("started", encoding="utf-8")
        deadline = time.monotonic() + 5.0
        while not SCHEMA_RELEASE_PATH.exists() and time.monotonic() < deadline:
            time.sleep(0.01)
        if not SCHEMA_RELEASE_PATH.exists():
            SCHEMA_WAIT_TIMEOUT_PATH.write_text("timed out", encoding="utf-8")
        SCHEMA_COMPLETED_PATH.write_text("completed", encoding="utf-8")
        return [
            {
                "id": "window_meta",
                "type": "meta",
                "panel_variant": "native",
                "target_selector": True,
                "target_channel_filter": [1, 3],
            },
            {
                "id": "strength",
                "type": "float",
                "label": "Strength",
                "default": 0.5,
                "min": 0.0,
                "max": 1.0,
                "step": 0.05,
            },
        ]

    def on_input_changed(self, input_metadata):
        with INPUT_HOOK_PATH.open("a", encoding="utf-8") as marker:
            marker.write(str(input_metadata.get("view_id", "")) + "\n")

    def execute(self, target, src_image, dst_image, params, progress, masks=None, weights=None,
                output_masks=None):
        progress.set_text("noop")
)PY");

    auto& runtime_status_registry = an::ui::extensions::ExtensionRuntimeStatusRegistry::instance();
    runtime_status_registry.clear();

    const auto initial_view_image = makeRgbGradientImage(8, 6);
    ASSERT_TRUE(initial_view_image);
    std::string setup_error;
    auto initial_view_buffer =
        makeImageBuffer(temp_root / "initial-view-buffers", "bootstrap-target", initial_view_image,
                        an::ui::extensions::SharedBufferRole::Source,
                        an::ui::extensions::SharedBufferAccessMode::ReadOnly, "active_view", &setup_error);
    ASSERT_TRUE(initial_view_buffer.valid()) << setup_error;

    int initial_view_state_requests = 0;
    std::string active_view_name = "alpha";
    std::string exported_initial_view_name;
    an::tests::processes::framework::CaseLaunchCoordinator coordinator;
    coordinator.configureSdkService(
        nullptr,
        [&initial_view_state_requests, &active_view_name]() {
            ++initial_view_state_requests;
            an::ui::extensions::ExtensionViewState alpha;
            alpha.view_name = "alpha";
            alpha.file_name = "/tmp/alpha.fit";
            alpha.is_active = active_view_name == alpha.view_name;
            alpha.has_image = true;
            an::ui::extensions::ExtensionViewState beta;
            beta.view_name = "beta";
            beta.file_name = "/tmp/beta.fit";
            beta.is_active = active_view_name == beta.view_name;
            beta.has_image = true;
            return std::vector<an::ui::extensions::ExtensionViewState>{alpha, beta};
        },
        [&initial_view_buffer,
         &exported_initial_view_name](const std::string_view, const std::string_view, const std::string_view view_name,
                                      an::ui::extensions::ExtensionViewSnapshot* out_snapshot, std::string*) {
            exported_initial_view_name = std::string(view_name);
            out_snapshot->source_buffer = initial_view_buffer.descriptor();
            return true;
        });
    an::ui::extensions::ExtensionHostLaunchBinding binding;
    binding.descriptor_id = "filters.single_schema_inspection";
    binding.extension_id = "an.demo.single_schema_inspection";
    binding.display_name = "Single Schema Inspection";
    binding.host_executable = host_path;
    binding.capabilities = {"execute"};
    binding.process_capabilities.execute = true;
    binding.process_capabilities.ui = "auto";
    binding.extension_host_owns_process_window = true;
    binding.host_arguments = {"--extension-package-root",
                              package_root.string(),
                              "--entry-point",
                              "single_schema_inspection_extension",
                              "--process-class",
                              "SingleSchemaInspectionExtension",
                              "--environment-root",
                              env_root.string(),
                              "--runtime-root",
                              runtime_root.string(),
                              "--stay-alive-ms",
                              std::to_string(2500 * kIpcTimingMultiplier)};

    std::string error;
    QElapsedTimer launch_timer;
    launch_timer.start();
    ASSERT_TRUE(coordinator.launchBoundDescriptor(binding, fakeTargetControl(), &error)) << error;
    EXPECT_LT(launch_timer.elapsed(), 750 * kIpcTimingMultiplier)
        << "A native extension launch must return after the lightweight host handshake instead of waiting for Python "
           "imports and parameter discovery.";
    active_view_name = "beta";
    ASSERT_TRUE(pumpCoordinatorUntil(coordinator, 1500 * kIpcTimingMultiplier, [&package_root]() {
        return std::filesystem::exists(package_root / "get_params_started.txt");
    }));
    ASSERT_FALSE(std::filesystem::exists(package_root / "get_params_wait_timed_out.txt"))
        << "The UI host blocked waiting for Python parameter bootstrap before accepting the live session.";
    EXPECT_EQ(initial_view_state_requests, 1)
        << "The initial image must not be exported while the extension runtime is still bootstrapping.";
    const auto bootstrapping_status = runtime_status_registry.snapshotForExtension(binding.extension_id);
    EXPECT_TRUE(!bootstrapping_status.has_value() || !bootstrapping_status->has_launch_result)
        << "A successful launch must not be recorded until Python bootstrap and lifecycle hooks complete.";
    an::ui::extensions::ExtensionHostExecutionDispatchResult early_dispatch;
    error.clear();
    EXPECT_FALSE(
        coordinator.submitExecuteForDescriptor(binding.descriptor_id, fakeTargetControl(), &early_dispatch, &error));
    EXPECT_NE(error.find("still initializing"), std::string::npos);

    writeTextFile(package_root / "release_get_params.txt", "continue");
    ASSERT_TRUE(pumpCoordinatorUntil(coordinator, 1500 * kIpcTimingMultiplier, [&package_root]() {
        return std::filesystem::exists(package_root / "get_params_completed.txt");
    }));
    ASSERT_TRUE(pumpCoordinatorUntil(coordinator, 1500 * kIpcTimingMultiplier, [&]() {
        const auto status = runtime_status_registry.snapshotForExtension(binding.extension_id);
        return status.has_value() && status->has_launch_result &&
               status->launch_outcome == an::ui::extensions::ExtensionRuntimeLaunchOutcome::Succeeded;
    }));
    EXPECT_GT(initial_view_state_requests, 1);
    EXPECT_EQ(exported_initial_view_name, "alpha") << "Deferred bootstrap must export the view selected when the "
                                                      "extension was launched, not a tab selected later.";
    EXPECT_EQ(QString::fromStdString(readTextFileForTests(package_root / "input_hook_view_ids.txt")).trimmed(),
              QStringLiteral("alpha"))
        << "The initial view must be installed before on_input_changed runs, and the hook must not receive a duplicate "
           "empty-input notification during bootstrap.";
    coordinator.stopAllSessions();
    ASSERT_TRUE(pumpCoordinatorUntilSessionCount(coordinator, 0u, 1000 * kIpcTimingMultiplier));

    EXPECT_EQ(readTextFileForTests(package_root / "get_params_call_count.txt"), "1")
        << "A native launch must use the schema loaded by its live host instead of starting a disposable "
           "preflight host that imports the extension a second time.";
    runtime_status_registry.clear();
}

// A native launch with a cached parameter schema does not ask the extension for its parameters.
TEST(ExtensionHostManagerTest, NativeProcessLaunchUsesCachedParameterSchemaWithoutCallingGetParams)
{
    const std::filesystem::path host_path = extensionHostPathForCurrentAppDir();
    const std::filesystem::path runtime_root = runtimeRootForCurrentAppDir();
    const std::filesystem::path bundled_python = repositoryBundledPythonPath();
    AN_REQUIRE_FULL_EXTENSION_HOST(host_path);
    ASSERT_TRUE(std::filesystem::exists(runtime_root)) << runtime_root;
    ASSERT_FALSE(bundled_python.empty()) << "Repository bundled Python runtime must be available";

    const auto temp_root = uniqueExtensionRuntimeRootForTest("cached-native-param-schema");
    removeAllNoThrow(temp_root);
    const RemovePathOnExit cleanup_temp_root{temp_root};
    std::error_code ec;
    std::filesystem::create_directories(temp_root, ec);
    ASSERT_FALSE(static_cast<bool>(ec)) << "Failed to create temp extension root: " << ec.message();

    const std::filesystem::path env_root = temp_root / "env";
    const std::filesystem::path package_root = temp_root / "cached_native_param_schema_extension";
    createVirtualEnvironment(bundled_python, env_root);
    writeTextFile(package_root / "cached_native_param_schema_extension.py",
                  R"PY(from pathlib import Path

from afternight import ui


GET_PARAMS_CALLED_PATH = Path(__file__).with_name("get_params_called.txt")


class CachedNativeParamSchemaExtension(ui.ProcessWindow):
    def get_params(self):
        GET_PARAMS_CALLED_PATH.write_text("called", encoding="utf-8")
        raise RuntimeError("The cached schema should avoid get_params() during live startup")
)PY");

    an::tests::processes::framework::CaseLaunchCoordinator coordinator;
    QObject pump_context;
    bool pump_scheduled = false;
    int pump_wake_count = 0;
    coordinator.setSessionPumpWakeCallback([&]() {
        ++pump_wake_count;
        if (pump_scheduled)
        {
            return;
        }
        pump_scheduled = true;
        QTimer::singleShot(0, &pump_context, [&]() {
            pump_scheduled = false;
            coordinator.pumpSessions();
        });
    });
    an::ui::extensions::ExtensionHostLaunchBinding binding;
    binding.descriptor_id = "filters.cached_native_param_schema";
    binding.extension_id = "an.demo.cached_native_param_schema";
    binding.display_name = "Cached Native Param Schema";
    binding.host_executable = host_path;
    binding.capabilities = {"execute"};
    binding.process_capabilities.execute = true;
    binding.process_capabilities.ui = "auto";
    binding.extension_host_owns_process_window = true;
    binding.has_cached_native_param_schema = true;
    binding.cached_native_param_schema =
        QJsonArray{QJsonObject{{QStringLiteral("id"), QStringLiteral("window_meta")},
                               {QStringLiteral("type"), QStringLiteral("meta")},
                               {QStringLiteral("panel_variant"), QStringLiteral("native")},
                               {QStringLiteral("target_selector"), true}},
                   QJsonObject{{QStringLiteral("id"), QStringLiteral("strength")},
                               {QStringLiteral("type"), QStringLiteral("float")},
                               {QStringLiteral("label"), QStringLiteral("Strength")},
                               {QStringLiteral("default"), 0.5},
                               {QStringLiteral("min"), 0.0},
                               {QStringLiteral("max"), 1.0},
                               {QStringLiteral("step"), 0.05}}};
    binding.host_arguments = {"--extension-package-root",
                              package_root.string(),
                              "--entry-point",
                              "cached_native_param_schema_extension",
                              "--process-class",
                              "CachedNativeParamSchemaExtension",
                              "--environment-root",
                              env_root.string(),
                              "--runtime-root",
                              runtime_root.string(),
                              "--stay-alive-ms",
                              std::to_string(2500 * kIpcTimingMultiplier)};

    std::string error;
    ASSERT_TRUE(coordinator.launchBoundDescriptor(binding, fakeTargetControl(), &error)) << error;
    QElapsedTimer ready_timer;
    ready_timer.start();
    while (!coordinator.hasReadySessionForDescriptor(binding.descriptor_id) &&
           ready_timer.elapsed() < 1500 * kIpcTimingMultiplier)
    {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        QThread::msleep(1);
    }
    ASSERT_TRUE(coordinator.hasReadySessionForDescriptor(binding.descriptor_id))
        << "Socket activity must wake the coordinator without waiting for the application's 100 ms fallback timer.";
    EXPECT_GT(pump_wake_count, 0);
    EXPECT_FALSE(std::filesystem::exists(package_root / "get_params_called.txt"))
        << "A validated cached schema must prevent the live host from calling get_params() again.";

    coordinator.stopAllSessions();
    ASSERT_TRUE(pumpCoordinatorUntilSessionCount(coordinator, 0u, 1000 * kIpcTimingMultiplier));
}

// A native launch whose bootstrap fails records the launch as failed.
TEST(ExtensionHostManagerTest, NativeProcessBootstrapFailureRecordsLaunchFailure)
{
    const std::filesystem::path host_path = extensionHostPathForCurrentAppDir();
    const std::filesystem::path runtime_root = runtimeRootForCurrentAppDir();
    const std::filesystem::path bundled_python = repositoryBundledPythonPath();
    AN_REQUIRE_FULL_EXTENSION_HOST(host_path);
    ASSERT_TRUE(std::filesystem::exists(runtime_root)) << runtime_root;
    ASSERT_FALSE(bundled_python.empty()) << "Repository bundled Python runtime must be available";

    const auto temp_root = uniqueExtensionRuntimeRootForTest("bootstrap-failure-status");
    removeAllNoThrow(temp_root);
    const RemovePathOnExit cleanup_temp_root{temp_root};
    std::filesystem::create_directories(temp_root);

    const std::filesystem::path env_root = temp_root / "env";
    const std::filesystem::path package_root = temp_root / "failing_bootstrap_extension";
    createVirtualEnvironment(bundled_python, env_root);
    writeTextFile(package_root / "failing_bootstrap_extension.py",
                  R"PY(import time

from afternight import ui


class FailingBootstrapExtension(ui.ProcessWindow):
    def get_params(self):
        time.sleep(0.25)
        raise RuntimeError("intentional bootstrap failure")
)PY");

    auto& runtime_status_registry = an::ui::extensions::ExtensionRuntimeStatusRegistry::instance();
    runtime_status_registry.clear();

    an::tests::processes::framework::CaseLaunchCoordinator coordinator;
    an::ui::extensions::ExtensionHostLaunchBinding binding;
    binding.descriptor_id = "filters.failing_bootstrap";
    binding.extension_id = "an.demo.failing_bootstrap";
    binding.display_name = "Failing Bootstrap";
    binding.host_executable = host_path;
    binding.capabilities = {"execute"};
    binding.process_capabilities.execute = true;
    binding.process_capabilities.ui = "auto";
    binding.extension_host_owns_process_window = true;
    binding.host_arguments = {
        "--extension-package-root",    package_root.string(), "--entry-point",
        "failing_bootstrap_extension", "--process-class",     "FailingBootstrapExtension",
        "--environment-root",          env_root.string(),     "--runtime-root",
        runtime_root.string(),         "--stay-alive-ms",     std::to_string(2500 * kIpcTimingMultiplier)};

    std::string error;
    ASSERT_TRUE(coordinator.launchBoundDescriptor(binding, fakeTargetControl(), &error)) << error;
    ASSERT_TRUE(pumpCoordinatorUntil(coordinator, 2500 * kIpcTimingMultiplier, [&]() {
        const auto status = runtime_status_registry.snapshotForExtension(binding.extension_id);
        return coordinator.activeSessionCount() == 0u && status.has_value() && status->has_launch_result &&
               status->launch_outcome == an::ui::extensions::ExtensionRuntimeLaunchOutcome::Failed;
    }));

    const auto status = runtime_status_registry.snapshotForExtension(binding.extension_id);
    ASSERT_TRUE(status.has_value());
    EXPECT_EQ(status->launch_outcome, an::ui::extensions::ExtensionRuntimeLaunchOutcome::Failed);
    EXPECT_NE(status->launch_detail.find("intentional bootstrap failure"), std::string::npos);
    runtime_status_registry.clear();
}

// A native launch whose target view is destroyed during bootstrap is cancelled.
TEST(ExtensionHostManagerTest, NativeProcessBootstrapCancelsWhenLaunchTargetIsDestroyed)
{
    const std::filesystem::path host_path = extensionHostPathForCurrentAppDir();
    AN_REQUIRE_FULL_EXTENSION_HOST(host_path);

    auto& runtime_status_registry = an::ui::extensions::ExtensionRuntimeStatusRegistry::instance();
    runtime_status_registry.clear();

    an::tests::processes::framework::CaseLaunchCoordinator coordinator;
    an::ui::extensions::ExtensionHostLaunchBinding binding;
    binding.descriptor_id = "filters.bootstrap_target_destroyed";
    binding.extension_id = "an.demo.bootstrap_target_destroyed";
    binding.display_name = "Bootstrap Target Destroyed";
    binding.host_executable = host_path;
    binding.capabilities = {"execute"};
    binding.process_capabilities.execute = true;
    binding.extension_host_owns_process_window = true;
    binding.host_arguments = {"--stay-alive-ms", std::to_string(2500 * kIpcTimingMultiplier)};

    std::string error;
    ASSERT_TRUE(coordinator.launchBoundDescriptor(binding, fakeTargetControl(), &error)) << error;
    EXPECT_EQ(coordinator.activeSessionCount(), 1u);

    coordinator.handleViewControlDestroyed(fakeTargetControl(), "destroyed-during-bootstrap");
    EXPECT_EQ(coordinator.activeSessionCount(), 0u);
    coordinator.pumpSessions();

    const auto status = runtime_status_registry.snapshotForExtension(binding.extension_id);
    EXPECT_TRUE(!status.has_value() || !status->has_launch_result)
        << "Closing the launch target while the loading shell is visible is a cancellation, not a launch failure.";
    runtime_status_registry.clear();
}

namespace
{

// The application's settings as a worker reads them: kept under a lock, and every read counted by the thread that
// asked for it.
class ThreadCountingSettingsContext final : public an::tests::processes::framework::WindowSettingsContext
{
public:
    using WindowSettingsContext::select;
    using WindowSettingsContext::upsert;
    using WindowSettingsContext::WindowSettingsContext;

    bool upsert(const QString& key, const QString& value) override
    {
        const std::lock_guard lock(m_mutex);
        return WindowSettingsContext::upsert(key, value);
    }
    [[nodiscard]] QString select(const QString& key, const QString& defaultValue = {}) override
    {
        const std::lock_guard lock(m_mutex);
        countRead();
        return WindowSettingsContext::select(key, defaultValue);
    }
    [[nodiscard]] std::optional<QString> selectOptional(const QString& key) override
    {
        const std::lock_guard lock(m_mutex);
        countRead();
        return WindowSettingsContext::selectOptional(key);
    }

    /// How many reads the GUI thread asked for.
    [[nodiscard]] int guiThreadReads() const
    {
        const std::lock_guard lock(m_mutex);
        return m_guiThreadReads;
    }

    /// How many reads other threads asked for.
    [[nodiscard]] int workerReads() const
    {
        const std::lock_guard lock(m_mutex);
        return m_workerReads;
    }

private:
    void countRead()
    {
        ++(QThread::currentThread() == QCoreApplication::instance()->thread() ? m_guiThreadReads : m_workerReads);
    }

    mutable std::mutex m_mutex;
    int m_guiThreadReads = 0;
    int m_workerReads = 0;
};

// The application's settings read through a gate: a read takes what is stored and then waits, while the gate is
// closed, before it answers - a read on its way, which the case holds there.
class GatedSettingsContext final : public an::tests::processes::framework::WindowSettingsContext
{
public:
    using WindowSettingsContext::select;
    using WindowSettingsContext::upsert;
    using WindowSettingsContext::WindowSettingsContext;

    bool upsert(const QString& key, const QString& value) override
    {
        const std::lock_guard lock(m_dataMutex);
        return WindowSettingsContext::upsert(key, value);
    }
    [[nodiscard]] QString select(const QString& key, const QString& defaultValue = {}) override
    {
        QString value;
        {
            const std::lock_guard lock(m_dataMutex);
            value = WindowSettingsContext::select(key, defaultValue);
        }
        std::unique_lock lock(m_gateMutex);
        ++m_readsBegun;
        m_changed.notify_all();
        m_changed.wait(lock, [this]() { return m_open; });
        return value;
    }

    /// Closes the gate: every read from now waits at it.
    void close()
    {
        const std::lock_guard lock(m_gateMutex);
        m_open = false;
    }

    /// Opens the gate: every read waiting answers.
    void open()
    {
        {
            const std::lock_guard lock(m_gateMutex);
            m_open = true;
        }
        m_changed.notify_all();
    }

    /// Waits until `count` reads have begun, or a while.
    [[nodiscard]] bool waitForReads(const int count)
    {
        std::unique_lock lock(m_gateMutex);
        return m_changed.wait_for(lock, std::chrono::seconds(10), [this, count]() { return m_readsBegun >= count; });
    }

private:
    std::mutex m_dataMutex;
    std::mutex m_gateMutex;
    std::condition_variable m_changed;
    bool m_open = true;
    int m_readsBegun = 0;
};

} // namespace

// A native window's launch passes its host the frame rate the settings answered on a worker: the launch asks the
// settings store nothing on the GUI thread - which would wait there for the store's own thread - and asks again, off
// it, for the launches after it, so a rate stored since is the one the next launch passes.
TEST(ExtensionHostManagerTest, ANativeWindowLaunchReadsThePreviewFrameRateOffTheGuiThread)
{
    const std::filesystem::path host_path = extensionHostPathForCurrentAppDir();
    AN_REQUIRE_FULL_EXTENSION_HOST(host_path);
    // The run's one target view, made before the reads are counted: a view reads its own settings when it is made.
    ImageViewControl* const target = fakeTargetControl();

    ThreadCountingSettingsContext settings(an::tests::processes::framework::suiteContext());
    const an::tests::processes::framework::ScopedAppContext installed(settings);
    const QString rate = QString::fromLatin1(SettingsKeys::PROCESSING_PREVIEW_FRAME_RATE);
    ASSERT_TRUE(settings.upsert(rate, QStringLiteral("12")));

    an::tests::processes::framework::CaseLaunchCoordinator coordinator;
    EXPECT_EQ(coordinator.previewFrameRate(), 30u) << "before the settings answer, a launch passes the default";
    coordinator.refreshPreviewFrameRate();
    EXPECT_TRUE(pumpCoordinatorUntil(coordinator, 2000, [&coordinator]() {
        return coordinator.previewFrameRate() == 12u;
    })) << "the stored rate was never read";
    EXPECT_GE(settings.workerReads(), 1);
    EXPECT_EQ(settings.guiThreadReads(), 0) << "the rate was read on the GUI thread";

    ASSERT_TRUE(settings.upsert(rate, QStringLiteral("24")));
    an::ui::extensions::ExtensionHostLaunchBinding binding;
    binding.descriptor_id = "filters.frame_rate_off_the_gui_thread";
    binding.extension_id = "an.demo.frame_rate_off_the_gui_thread";
    binding.display_name = "Frame Rate Off The GUI Thread";
    binding.host_executable = host_path;
    binding.capabilities = {"execute"};
    binding.process_capabilities.execute = true;
    binding.extension_host_owns_process_window = true;
    binding.host_arguments = {"--stay-alive-ms", std::to_string(2500 * kIpcTimingMultiplier)};

    std::string error;
    ASSERT_TRUE(coordinator.launchBoundDescriptor(binding, target, &error)) << error;
    EXPECT_EQ(settings.guiThreadReads(), 0) << "the launch read the settings on the GUI thread";
    EXPECT_TRUE(pumpCoordinatorUntil(coordinator, 2000, [&coordinator]() {
        return coordinator.previewFrameRate() == 24u;
    })) << "the launch did not ask the settings again for the launches after it";
    EXPECT_EQ(settings.guiThreadReads(), 0);
    ASSERT_TRUE(stopCoordinatorAndWaitForSessionAbsence(coordinator, 1000 * kIpcTimingMultiplier));
}

// The frame rate asked for while a read of it is on its way is not lost: the read reads once more before it ends, so a
// rate the Settings apply stored during it is the one the next launch passes. The request was refused, and nothing
// asked again until the next native launch.
TEST(ExtensionHostManagerTest, AFrameRateAskedForWhileAReadIsOnItsWayIsReadOnceMore)
{
    GatedSettingsContext settings(an::tests::processes::framework::suiteContext());
    const an::tests::processes::framework::ScopedAppContext installed(settings);
    const QString rate = QString::fromLatin1(SettingsKeys::PROCESSING_PREVIEW_FRAME_RATE);
    ASSERT_TRUE(settings.upsert(rate, QStringLiteral("12")));
    settings.close();

    an::tests::processes::framework::CaseLaunchCoordinator coordinator;
    coordinator.refreshPreviewFrameRate();
    ASSERT_TRUE(settings.waitForReads(1)) << "the read did not begin";
    ASSERT_TRUE(settings.upsert(rate, QStringLiteral("24")));
    coordinator.refreshPreviewFrameRate();
    settings.open();
    EXPECT_TRUE(
        pumpCoordinatorUntil(coordinator, 2000, [&coordinator]() { return coordinator.previewFrameRate() == 24u; }))
        << "the request made while the read was on its way was lost; the rate is " << coordinator.previewFrameRate();
    QThreadPool::globalInstance()->waitForDone();
}

// A read finds the settings through the application's context as it reads, and reads nothing once the context it was
// asked under is no longer the one installed: the store it would read may have gone with it.
TEST(ExtensionHostManagerTest, AFrameRateReadFindsNothingOnceTheContextItWasAskedUnderIsGone)
{
    GatedSettingsContext settings(an::tests::processes::framework::suiteContext());
    const an::tests::processes::framework::ScopedAppContext installed(settings);
    const QString rate = QString::fromLatin1(SettingsKeys::PROCESSING_PREVIEW_FRAME_RATE);
    ASSERT_TRUE(settings.upsert(rate, QStringLiteral("12")));
    settings.close();

    an::tests::processes::framework::CaseLaunchCoordinator coordinator;
    coordinator.refreshPreviewFrameRate();
    ASSERT_TRUE(settings.waitForReads(1)) << "the read did not begin";
    ASSERT_TRUE(settings.upsert(rate, QStringLiteral("24")));
    coordinator.refreshPreviewFrameRate();
    {
        an::tests::processes::framework::WindowSettingsContext other(an::tests::processes::framework::suiteContext());
        ASSERT_TRUE(other.upsert(rate, QStringLiteral("30")));
        const an::tests::processes::framework::ScopedAppContext replaced(other);
        settings.open();
        QThreadPool::globalInstance()->waitForDone();
        EXPECT_EQ(coordinator.previewFrameRate(), 12u)
            << "the read asked for again read a context other than the one it was asked under";
    }
}

// A second launch of one descriptor for one target is refused while the first runs.
TEST(ExtensionHostManagerTest, LaunchCoordinatorRejectsDuplicateDescriptorSessionForSameTarget)
{
    const std::filesystem::path host_path = extensionHostPathForCurrentAppDir();
    AN_REQUIRE_FULL_EXTENSION_HOST(host_path);

    an::tests::processes::framework::CaseLaunchCoordinator coordinator;
    an::ui::extensions::ExtensionHostLaunchBinding binding;
    binding.descriptor_id = "filters.demo";
    binding.extension_id = "an.demo";
    binding.display_name = "Demo";
    binding.host_executable = host_path;
    binding.capabilities = {"execute"};

    std::string error;
    ASSERT_TRUE(coordinator.launchBoundDescriptor(binding, fakeTargetControl(), &error)) << error;

    EXPECT_FALSE(coordinator.launchBoundDescriptor(binding, fakeTargetControl(), &error));
    EXPECT_NE(error.find("already has an active host session"), std::string::npos);

    ASSERT_TRUE(stopCoordinatorAndWaitForSessionAbsence(coordinator, 500 * kIpcTimingMultiplier));
}

// A disconnected session does not block launching its descriptor again.
TEST(ExtensionHostManagerTest, LaunchCoordinatorPrunesDisconnectedSessionBeforeDuplicateCheck)
{
    const std::filesystem::path host_path = extensionHostPathForCurrentAppDir();
    AN_REQUIRE_FULL_EXTENSION_HOST(host_path);

    an::tests::processes::framework::CaseLaunchCoordinator coordinator;
    an::ui::extensions::ExtensionHostLaunchBinding binding;
    binding.descriptor_id = "filters.demo_prune_before_duplicate";
    binding.extension_id = "an.demo.prune_before_duplicate";
    binding.display_name = "Demo Prune Before Duplicate";
    binding.host_executable = host_path;
    binding.capabilities = {"execute"};
    binding.host_arguments = {"--stay-alive-ms", std::to_string(1000 * kIpcTimingMultiplier),
                              "--disconnect-after-ack-ms", "0"};

    std::string error;
    ASSERT_TRUE(coordinator.launchBoundDescriptor(binding, fakeTargetControl(), &error)) << error;

    QElapsedTimer disconnect_wait;
    disconnect_wait.start();
    while (disconnect_wait.elapsed() < 150 * kIpcTimingMultiplier)
    {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 1);
        QThread::msleep(10);
    }

    EXPECT_EQ(coordinator.activeSessionCount(), 1u)
        << "Without an explicit coordinator pump the stale disconnected session should still be tracked.";

    error.clear();
    EXPECT_TRUE(coordinator.launchBoundDescriptor(binding, fakeTargetControl(), &error)) << error;
    EXPECT_EQ(coordinator.activeSessionCount(), 1u);

    ASSERT_TRUE(stopCoordinatorAndWaitForSessionAbsence(coordinator, 500 * kIpcTimingMultiplier));
}

// A session that disconnects is recorded as terminated by disconnection.
TEST(ExtensionHostManagerTest, LaunchCoordinatorRecordsRuntimeTerminationStatusForDisconnectedSession)
{
    const std::filesystem::path host_path = extensionHostPathForCurrentAppDir();
    AN_REQUIRE_FULL_EXTENSION_HOST(host_path);

    auto& runtime_status_registry = an::ui::extensions::ExtensionRuntimeStatusRegistry::instance();
    runtime_status_registry.clear();

    an::tests::processes::framework::CaseLaunchCoordinator coordinator;
    an::ui::extensions::ExtensionHostLaunchBinding binding;
    binding.descriptor_id = "filters.demo_runtime_status";
    binding.extension_id = "an.demo.runtime_status";
    binding.display_name = "Demo Runtime Status";
    binding.host_executable = host_path;
    binding.capabilities = {"execute"};
    binding.host_arguments = {"--stay-alive-ms", std::to_string(2500 * kIpcTimingMultiplier),
                              "--disconnect-after-ack-ms", "0"};

    std::string error;
    ASSERT_TRUE(coordinator.launchBoundDescriptor(binding, fakeTargetControl(), &error)) << error;

    ASSERT_TRUE(pumpCoordinatorUntil(
        coordinator, 2500 * kIpcTimingMultiplier, [&coordinator, &runtime_status_registry, &binding]() {
            if (coordinator.activeSessionCount() != 0u)
            {
                return false;
            }

            const auto snapshot = runtime_status_registry.snapshotForExtension(binding.extension_id);
            return snapshot.has_value() && snapshot->has_termination_result;
        }));

    const auto snapshot = runtime_status_registry.snapshotForExtension(binding.extension_id);
    ASSERT_TRUE(snapshot.has_value());
    EXPECT_TRUE(snapshot->has_launch_result);
    EXPECT_EQ(snapshot->launch_outcome, an::ui::extensions::ExtensionRuntimeLaunchOutcome::Succeeded);
    EXPECT_TRUE(snapshot->has_termination_result);
    EXPECT_EQ(snapshot->termination_kind, an::ui::extensions::ExtensionRuntimeTerminationKind::Disconnected);
    EXPECT_NE(snapshot->termination_detail.find("control channel disconnected"), std::string::npos);

    runtime_status_registry.clear();
}

// A session whose host crashes is recorded as crashed.
TEST(ExtensionHostManagerTest, LaunchCoordinatorRecordsRuntimeTerminationStatusForCrashedSession)
{
    const std::filesystem::path host_path = extensionHostPathForCurrentAppDir();
    AN_REQUIRE_FULL_EXTENSION_HOST(host_path);

    auto& runtime_status_registry = an::ui::extensions::ExtensionRuntimeStatusRegistry::instance();
    runtime_status_registry.clear();

    an::tests::processes::framework::CaseLaunchCoordinator coordinator;
    an::ui::extensions::ExtensionHostLaunchBinding binding;
    binding.descriptor_id = "filters.demo_runtime_status_crash";
    binding.extension_id = "an.demo.runtime_status_crash";
    binding.display_name = "Demo Runtime Status Crash";
    binding.host_executable = host_path;
    binding.capabilities = {"execute"};
    binding.host_arguments = {"--stay-alive-ms", std::to_string(1500 * kIpcTimingMultiplier), "--crash-after-ack-ms",
                              std::to_string(75 * kIpcTimingMultiplier)};

    std::string error;
    ASSERT_TRUE(coordinator.launchBoundDescriptor(binding, fakeTargetControl(), &error)) << error;

    ASSERT_TRUE(pumpCoordinatorUntil(coordinator, 1500 * kIpcTimingMultiplier,
                                     [&coordinator]() { return coordinator.activeSessionCount() == 0u; }));

    const auto snapshot = runtime_status_registry.snapshotForExtension(binding.extension_id);
    ASSERT_TRUE(snapshot.has_value());
    EXPECT_TRUE(snapshot->has_launch_result);
    EXPECT_EQ(snapshot->launch_outcome, an::ui::extensions::ExtensionRuntimeLaunchOutcome::Succeeded);
    EXPECT_TRUE(snapshot->has_termination_result);
    EXPECT_EQ(snapshot->termination_kind, an::ui::extensions::ExtensionRuntimeTerminationKind::Crashed);
    EXPECT_NE(snapshot->termination_detail.find("crashed"), std::string::npos);
    EXPECT_FALSE(snapshot->termination_diagnostics.session_id.empty());

    runtime_status_registry.clear();
}

// A host that exits after its native window was closed is not reported as a crash.
TEST(ExtensionHostManagerTest, NativeWindowCloseIntentSuppressesPostCloseCrashWarning)
{
    const std::filesystem::path host_path = extensionHostPathForCurrentAppDir();
    AN_REQUIRE_FULL_EXTENSION_HOST(host_path);

    auto& runtime_status_registry = an::ui::extensions::ExtensionRuntimeStatusRegistry::instance();
    runtime_status_registry.clear();

    an::tests::processes::framework::CaseLaunchCoordinator coordinator;
    an::ui::extensions::ExtensionHostLaunchBinding binding;
    binding.descriptor_id = "filters.demo_native_close_intent";
    binding.extension_id = "an.demo.native_close_intent";
    binding.display_name = "Demo Native Close Intent";
    binding.host_executable = host_path;
    binding.capabilities = {"execute"};
    binding.host_arguments = {"--stay-alive-ms", std::to_string(1500 * kIpcTimingMultiplier),
                              "--runtime-closed-crash-after-ack-ms", std::to_string(75 * kIpcTimingMultiplier)};

    std::string error;
    ASSERT_TRUE(coordinator.launchBoundDescriptor(binding, fakeTargetControl(), &error)) << error;

    ASSERT_TRUE(pumpCoordinatorUntil(coordinator, 1500 * kIpcTimingMultiplier,
                                     [&coordinator]() { return coordinator.activeSessionCount() == 0u; }));

    const auto snapshot = runtime_status_registry.snapshotForExtension(binding.extension_id);
    ASSERT_TRUE(snapshot.has_value());
    EXPECT_TRUE(snapshot->has_termination_result);
    EXPECT_EQ(snapshot->termination_kind, an::ui::extensions::ExtensionRuntimeTerminationKind::Disconnected);
    EXPECT_NE(snapshot->termination_detail.find("native process window closed"), std::string::npos);
    EXPECT_EQ(snapshot->termination_detail.find("process crashed"), std::string::npos);
    EXPECT_EQ(snapshot->termination_detail.find("exit status: crash"), std::string::npos);

    runtime_status_registry.clear();
}

// A real-time preview host that exits idle after its window closed is not reported as a crash.
TEST(ExtensionHostManagerTest, NativeRtPreviewIdleCrashSuppressesPostCloseCrashWarning)
{
    const std::filesystem::path host_path = extensionHostPathForCurrentAppDir();
    AN_REQUIRE_FULL_EXTENSION_HOST(host_path);

    auto& runtime_status_registry = an::ui::extensions::ExtensionRuntimeStatusRegistry::instance();
    runtime_status_registry.clear();

    an::tests::processes::framework::CaseLaunchCoordinator coordinator;
    an::ui::extensions::ExtensionHostLaunchBinding binding;
    binding.descriptor_id = "filters.demo_native_rt_preview_idle_crash";
    binding.extension_id = "an.demo.native_rt_preview_idle_crash";
    binding.display_name = "Demo Native RT Preview Idle Crash";
    binding.host_executable = host_path;
    binding.capabilities = {"execute", "preview"};
    binding.process_capabilities.preview = true;
    binding.host_arguments = {"--stay-alive-ms", std::to_string(1500 * kIpcTimingMultiplier), "--crash-after-ack-ms",
                              std::to_string(75 * kIpcTimingMultiplier)};

    std::string error;
    ASSERT_TRUE(coordinator.launchBoundDescriptor(binding, fakeTargetControl(), &error)) << error;

    ASSERT_TRUE(pumpCoordinatorUntil(coordinator, 1500 * kIpcTimingMultiplier,
                                     [&coordinator]() { return coordinator.activeSessionCount() == 0u; }));

    const auto snapshot = runtime_status_registry.snapshotForExtension(binding.extension_id);
    ASSERT_TRUE(snapshot.has_value());
    EXPECT_TRUE(snapshot->has_termination_result);
    EXPECT_EQ(snapshot->termination_kind, an::ui::extensions::ExtensionRuntimeTerminationKind::Disconnected);
    EXPECT_NE(snapshot->termination_detail.find("native process window closed"), std::string::npos);
    EXPECT_EQ(snapshot->termination_detail.find("process crashed"), std::string::npos);
    EXPECT_EQ(snapshot->termination_detail.find("exit status: crash"), std::string::npos);

    runtime_status_registry.clear();
}

// A host that crashes during an execute presents nothing and leaves no shared buffer behind.
TEST(ExtensionHostManagerTest, CrashedExecuteSessionCleansSharedBuffers)
{
    const std::filesystem::path host_path = extensionHostPathForCurrentAppDir();
    AN_REQUIRE_FULL_EXTENSION_HOST(host_path);

    auto& runtime_status_registry = an::ui::extensions::ExtensionRuntimeStatusRegistry::instance();
    runtime_status_registry.clear();
    const auto shared_buffer_root = an::ui::extensions::SharedBufferManager::defaultSharedBufferRootPath();
    const auto source_image = makeRgbGradientImage(5, 4);
    ASSERT_TRUE(source_image);

    an::tests::processes::ViewHandle target = targetViewOf("execute-crash-cleanup", source_image);
    ASSERT_NE(target, nullptr);

    an::tests::processes::framework::CaseLaunchCoordinator coordinator;
    bool result_presented = false;
    coordinator.setManagedImageHooks([&](ImageViewControl* control, an::consumer::ImageAsset&& image,
                                         const std::string& history_step_name,
                                         const std::string& history_step_description) {
        result_presented = true;
        EXPECT_EQ(control, target.get());
        EXPECT_TRUE(image.descriptor().has_value());
        EXPECT_FALSE(history_step_name.empty());
        EXPECT_FALSE(history_step_description.empty());
    });

    an::ui::extensions::ExtensionHostLaunchBinding binding;
    binding.descriptor_id = "filters.execute_crash_cleanup";
    binding.extension_id = "an.demo.execute_crash_cleanup";
    binding.display_name = "Execute Crash Cleanup";
    binding.host_executable = host_path;
    binding.capabilities = {"execute"};
    binding.host_arguments = {"--stay-alive-ms",      std::to_string(1500 * kIpcTimingMultiplier),
                              "--execute-delay-ms",   std::to_string(1200 * kIpcTimingMultiplier),
                              "--crash-after-ack-ms", std::to_string(250 * kIpcTimingMultiplier)};

    std::string error;
    ASSERT_TRUE(coordinator.launchBoundDescriptor(binding, target.get(), &error)) << error;
    ASSERT_TRUE(pumpCoordinatorUntilNativeSessionReady(coordinator, binding.descriptor_id, 750 * kIpcTimingMultiplier));

    an::ui::extensions::ExtensionHostExecutionDispatchResult dispatch_result;
    ASSERT_TRUE(coordinator.submitExecuteForDescriptor(binding.descriptor_id, target.get(), &dispatch_result, &error))
        << error;
    EXPECT_EQ(dispatch_result.state, an::ui::extensions::ExtensionHostExecutionDispatchState::Started);

    ASSERT_TRUE(pumpCoordinatorUntil(coordinator, 3000 * kIpcTimingMultiplier, [&]() {
        const auto snapshot = runtime_status_registry.snapshotForExtension(binding.extension_id);
        return coordinator.activeSessionCount() == 0u && snapshot.has_value() && snapshot->has_termination_result &&
               snapshot->termination_kind == an::ui::extensions::ExtensionRuntimeTerminationKind::Crashed &&
               !snapshot->termination_diagnostics.session_id.empty();
    })) << error;

    const auto snapshot = runtime_status_registry.snapshotForExtension(binding.extension_id);
    ASSERT_TRUE(snapshot.has_value());
    EXPECT_TRUE(snapshot->has_launch_result);
    EXPECT_EQ(snapshot->launch_outcome, an::ui::extensions::ExtensionRuntimeLaunchOutcome::Succeeded);
    EXPECT_TRUE(snapshot->has_termination_result);
    EXPECT_EQ(snapshot->termination_kind, an::ui::extensions::ExtensionRuntimeTerminationKind::Crashed);
    EXPECT_FALSE(result_presented);

    const auto session_buffer_root = shared_buffer_root / snapshot->termination_diagnostics.session_id;
    EXPECT_TRUE(pathMissingOrEmpty(session_buffer_root));

    runtime_status_registry.clear();
}

// A host the application stops is ended at once, with whatever buffers its native window's frames hold mapped: once it
// has stopped the host, the application removes the directory those frames were lent from, whatever is left in it.
TEST(ExtensionHostManagerTest, AStoppedSessionLeavesNoFileOfItsNativeWindowsFramesBehind)
{
    const std::filesystem::path host_path = extensionHostPathForCurrentAppDir();
    AN_REQUIRE_FULL_EXTENSION_HOST(host_path);

    const auto source_image = makeRgbGradientImage(8, 6);
    ASSERT_TRUE(source_image);
    InMemorySettings settings_store;
    GlobalSettingsPersistence& settings = settings_store.repository();
    const std::string view_name = "stopped-session-frames";
    an::tests::processes::ViewHandle target = targetViewOf(view_name, source_image);
    ASSERT_NE(target, nullptr);

    std::string session_id;
    an::tests::processes::framework::CaseLaunchCoordinator coordinator;
    coordinator.configureSdkService(
        &settings,
        [&]() -> std::vector<an::ui::extensions::ExtensionViewState> {
            an::ui::extensions::ExtensionViewState view_state;
            view_state.view_name = view_name;
            view_state.view_mode = "standard";
            view_state.channel_view_mode = "rgb";
            view_state.is_active = true;
            view_state.has_image = true;
            view_state.width = 8;
            view_state.height = 6;
            view_state.channels = 3;
            return {view_state};
        },
        [&](const std::string_view session, const std::string_view request_id, const std::string_view named,
            an::ui::extensions::ExtensionViewSnapshot* out_snapshot, std::string* error) -> bool {
            session_id = std::string(session);
            *out_snapshot = an::ui::extensions::ExtensionViewSnapshot{};
            an::ui::extensions::SharedBufferManager shared_buffer_manager;
            an::ui::extensions::SharedImageBuffer buffer;
            if (!exportImageOrReport(
                    shared_buffer_manager, *source_image, session_id, an::ui::extensions::SharedBufferRole::Preview,
                    &buffer, error, std::string(request_id) + ".stopped_session_snapshot",
                    std::string(named) + ".snapshot", an::ui::extensions::SharedBufferAccessMode::ReadOnly))
            {
                return false;
            }
            out_snapshot->source_buffer = buffer.descriptor();
            return true;
        },
        []() { return an::ui::extensions::ExtensionThemeSnapshot{}; });
    coordinator.setManagedViewNameProvider([&view_name](ImageViewControl*) { return view_name; });

    an::ui::extensions::ExtensionHostLaunchBinding binding;
    binding.descriptor_id = "filters.stopped_session_frames";
    binding.extension_id = "an.demo.stopped_session_frames";
    binding.display_name = "Stopped Session Frames";
    binding.host_executable = host_path;
    binding.capabilities = {"execute"};
    binding.host_arguments = {"--stay-alive-ms", std::to_string(5000 * kIpcTimingMultiplier)};

    std::string error;
    an::ui::extensions::ExtensionHostLaunchOptions options;
    options.initial_view_name = view_name;
    ASSERT_TRUE(coordinator.launchBoundDescriptor(binding, target.get(), options, &error)) << error;
    ASSERT_TRUE(pumpCoordinatorUntilNativeSessionReady(coordinator, binding.descriptor_id, 750 * kIpcTimingMultiplier));
    ASSERT_TRUE(pumpCoordinatorUntil(coordinator, 3000 * kIpcTimingMultiplier, [&]() { return !session_id.empty(); }))
        << "the session never described its view";

    // What a host ended in the middle of its window's frames leaves: the buffers its frames' pool kept, and a frame's.
    const std::filesystem::path frames_root = an::ui::extensions::extensionHostPreviewFramesRootPath(session_id);
    const RemovePathOnExit remove_frames_root{frames_root};
    std::filesystem::create_directories(frames_root);
    for (const char* name :
         {"source.0.bin", "source.0.bin.metadata.json", "output.1.bin", "output.1.bin.metadata.json"})
    {
        std::ofstream(frames_root / name, std::ios::binary) << "kept";
    }
    // One of them is still held open when the session stops, as a host that has not yet gone holds its mappings: on
    // Windows it cannot be removed until its holder lets go, and the application tries again from the event loop.
    std::ofstream held(frames_root / "output.1.bin", std::ios::binary | std::ios::app);
    ASSERT_TRUE(held.is_open());

    ASSERT_TRUE(stopCoordinatorAndWaitForSessionAbsence(coordinator, 3000 * kIpcTimingMultiplier));
    held.close();
    EXPECT_TRUE(pumpCoordinatorUntil(coordinator, 5000 * kIpcTimingMultiplier,
                                     [&frames_root]() { return !std::filesystem::exists(frames_root); }))
        << frames_root << " was left behind once its holder had gone";
}

// Discarding an active session's buffers lets the mappings go but keeps its directory until the session ends.
TEST(ExtensionHostManagerTest, MaterializedBufferDiscardDefersActiveSessionDirectoryRemoval)
{
    an::ui::extensions::ExtensionSessionController controller;
    controller.running_session.state = an::ui::extensions::ExtensionHostSessionState::Active;
    controller.running_session.prepared_session.session_id =
        "defer-active-cleanup-" + an::ui::extensions::generateOpaqueToken();

    const auto shared_buffer_root = an::ui::extensions::SharedBufferManager::defaultSharedBufferRootPath();
    const auto session_buffer_root = shared_buffer_root / controller.running_session.prepared_session.session_id;
    removeAllNoThrow(session_buffer_root);
    const RemovePathOnExit cleanup_session_buffers{session_buffer_root};

    const auto source_image = makeRgbGradientImage(5, 4);
    ASSERT_TRUE(source_image);

    an::ui::extensions::SharedBufferManager shared_buffer_manager;
    an::ui::extensions::SharedImageBuffer source_buffer;
    std::string error;
    ASSERT_TRUE(exportImageOrReport(
        shared_buffer_manager, *source_image, controller.running_session.prepared_session.session_id,
        an::ui::extensions::SharedBufferRole::Source, &source_buffer, &error, "deferred-cleanup-source", "source"))
        << error;

    ASSERT_TRUE(source_buffer.valid());
    controller.owned_buffers.push_back(std::move(source_buffer));
    ASSERT_FALSE(pathMissingOrEmpty(session_buffer_root));

    controller.discardMaterializedBuffers();

    EXPECT_TRUE(controller.owned_buffers.empty());
    EXPECT_FALSE(pathMissingOrEmpty(session_buffer_root));

    const an::Status removed =
        shared_buffer_manager.removeSessionBuffers(controller.running_session.prepared_session.session_id);
    EXPECT_TRUE(removed.ok()) << removed.message();
}

// A native session stops after a successful execute when it was asked to close or runs headless.
TEST(ExtensionHostManagerTest, TransientGeneratorSessionStopsNativeWindowAfterSuccessfulExecute)
{
    an::ui::extensions::ExtensionSessionController controller;
    controller.running_session.handshake.ack.native_process_window = true;

    EXPECT_FALSE(controller.shouldStopAfterSuccessfulExecutePresentation(false));
    EXPECT_TRUE(controller.shouldStopAfterSuccessfulExecutePresentation(true));

    controller.close_after_successful_execute = true;
    EXPECT_TRUE(controller.shouldStopAfterSuccessfulExecutePresentation(false));

    controller.running_session.handshake.ack.native_process_window = false;
    EXPECT_FALSE(controller.shouldStopAfterSuccessfulExecutePresentation(true));

    // A session whose window closed as it committed is stopped as a closed window once its result is shown.
    controller.running_session.handshake.ack.native_process_window = true;
    controller.running_session.native_window_close_requested = true;
    controller.running_session.state = an::ui::extensions::ExtensionHostSessionState::Disconnected;
    EXPECT_FALSE(controller.shouldStopAfterSuccessfulExecutePresentation(true));
}

// A starless generator's descriptor offers a transient launch beside the normal one; another descriptor does not.
TEST(ExtensionHostManagerTest, StarlessGeneratorBindingAddsTransientLaunchPath)
{
    const an::tests::processes::framework::ScratchDirectory scratch("transient-host");
    const auto host_path = scratch.path() / "an_extension_transient_host.exe";
    {
        std::ofstream out(host_path, std::ios::binary);
        out << "host";
    }

    an::ui::extensions::ExtensionHostLaunchBinding binding;
    binding.descriptor_id = "test.starless_generator";
    binding.extension_id = "test_starless_generator";
    binding.display_name = "Test Starless Generator";
    binding.host_executable = host_path;
    binding.process_capabilities.starless_generator = true;

    an::ui::process_registry::ProcessDescriptor descriptor;
    descriptor.availability = an::ui::process_registry::ProcessAvailability::Available;

    an::tests::processes::framework::CaseLaunchCoordinator coordinator;
    coordinator.bindSingleImageDescriptor(&descriptor, binding);

    EXPECT_TRUE(static_cast<bool>(descriptor.launch_single_image_checked));
    EXPECT_TRUE(static_cast<bool>(descriptor.launch_single_image_transient_checked));

    binding.process_capabilities.starless_generator = false;
    an::ui::process_registry::ProcessDescriptor normal_descriptor;
    normal_descriptor.availability = an::ui::process_registry::ProcessAvailability::Available;
    coordinator.bindSingleImageDescriptor(&normal_descriptor, binding);

    EXPECT_TRUE(static_cast<bool>(normal_descriptor.launch_single_image_checked));
    EXPECT_FALSE(static_cast<bool>(normal_descriptor.launch_single_image_transient_checked));
}

// Stopping every session stops the processes a host started too, and leaves no shared buffer behind.
TEST(ExtensionHostManagerTest, StopAllSessionsTerminatesSpawnedHelperProcessTree)
{
    const std::filesystem::path host_path = extensionHostPathForCurrentAppDir();
    const std::filesystem::path runtime_root = runtimeRootForCurrentAppDir();
    const std::filesystem::path bundled_python = repositoryBundledPythonPath();
    AN_REQUIRE_FULL_EXTENSION_HOST(host_path);
    ASSERT_TRUE(std::filesystem::exists(runtime_root)) << runtime_root;
    ASSERT_FALSE(bundled_python.empty()) << "Repository bundled Python runtime must be available";

    auto& runtime_status_registry = an::ui::extensions::ExtensionRuntimeStatusRegistry::instance();
    runtime_status_registry.clear();
    const auto temp_root = uniqueExtensionRuntimeRootForTest("helper-process-tree");
    removeAllNoThrow(temp_root);
    const RemovePathOnExit remove_temp_root{temp_root};
    std::filesystem::create_directories(temp_root);

    const auto env_root = temp_root / "env";
    const auto package_root = temp_root / "helper_process_tree_ext";
    const auto pid_file = temp_root / "spawned_helper.pid";
    createVirtualEnvironment(bundled_python, env_root);
    std::filesystem::create_directories(package_root);
    writeTextFile(package_root / "helper_process_tree_ext.py",
                  "import afternight\n"
                  "import pathlib\n"
                  "import subprocess\n"
                  "import sys\n"
                  "import time\n\n"
                  "PID_FILE = '" +
                      pid_file.generic_string() +
                      "'\n\n"
                      "class HelperProcessTreeExtension(afternight.ProcessExtension):\n"
                      "    def get_params(self):\n"
                      "        return []\n\n"
                      "    def execute(self, target, src_image, dst_image, params, progress, masks=None, weights=None, "
                      "output_masks=None):\n"
                      "        helper = subprocess.Popen([sys.executable, '-c', 'import time; time.sleep(60)'])\n"
                      "        pathlib.Path(PID_FILE).write_text(str(helper.pid), encoding='utf-8')\n"
                      "        while True:\n"
                      "            time.sleep(0.05)\n");

    const auto source_image = makeMonoFloatImage("helper-process-tree-source", 3, 3, 1.0f, 0.0f);
    ASSERT_TRUE(source_image);

    InMemorySettings settings_store;
    an::tests::processes::ViewHandle target = targetViewOf("helper-process-tree", source_image);
    ASSERT_NE(target, nullptr);

    an::tests::processes::framework::CaseLaunchCoordinator coordinator;
    coordinator.configureSdkService(&settings_store.repository());
    coordinator.setManagedImageHooks(
        [](ImageViewControl*, an::consumer::ImageAsset&&, const std::string&, const std::string&) {}, {});

    an::ui::extensions::ExtensionHostLaunchBinding binding;
    binding.descriptor_id = "filters.helper_process_tree";
    binding.extension_id = "an.demo.helper_process_tree";
    binding.display_name = "Helper Process Tree";
    binding.host_executable = host_path;
    binding.capabilities = {"execute"};
    binding.host_arguments = {
        "--extension-package-root", package_root.string(), "--entry-point",
        "helper_process_tree_ext",  "--process-class",     "HelperProcessTreeExtension",
        "--environment-root",       env_root.string(),     "--runtime-root",
        runtime_root.string(),      "--stay-alive-ms",     std::to_string(10000 * kIpcTimingMultiplier)};

    std::string error;
    ASSERT_TRUE(coordinator.launchBoundDescriptor(binding, target.get(), &error)) << error;
    ASSERT_TRUE(pumpCoordinatorUntilNativeSessionReady(coordinator, binding.descriptor_id, 500 * kIpcTimingMultiplier));

    an::ui::extensions::ExtensionHostExecutionDispatchResult dispatch_result;
    ASSERT_TRUE(coordinator.submitExecuteForDescriptor(binding.descriptor_id, target.get(), &dispatch_result, &error))
        << error;
    EXPECT_EQ(dispatch_result.state, an::ui::extensions::ExtensionHostExecutionDispatchState::Started);

    qint64 helper_pid = -1;
    ASSERT_TRUE(waitForPidFile(pid_file, 5000 * kIpcTimingMultiplier, &helper_pid));
    EXPECT_TRUE(processExists(helper_pid));

    ASSERT_TRUE(stopCoordinatorAndWaitForSessionAbsence(coordinator, 2000 * kIpcTimingMultiplier));
    EXPECT_TRUE(waitForProcessExit(helper_pid, 5000 * kIpcTimingMultiplier));

    const auto snapshot = runtime_status_registry.snapshotForExtension(binding.extension_id);
    ASSERT_TRUE(snapshot.has_value());
    EXPECT_TRUE(snapshot->has_termination_result);
    ASSERT_FALSE(snapshot->termination_diagnostics.session_id.empty());
    const auto session_buffer_root = an::ui::extensions::SharedBufferManager::defaultSharedBufferRootPath() /
                                     snapshot->termination_diagnostics.session_id;
    EXPECT_TRUE(pathMissingOrEmpty(session_buffer_root));

    runtime_status_registry.clear();
    removeAllNoThrow(temp_root);
}

// An execute exports the target view's document as its source; a target showing no image is refused.
TEST(ExtensionHostManagerTest, LaunchCoordinatorSubmitExecuteExportsTheTargetViewsDocument)
{
    const std::filesystem::path host_path = extensionHostPathForCurrentAppDir();
    AN_REQUIRE_FULL_EXTENSION_HOST(host_path);

    const auto source_image = makeRgbGradientImage(5, 4);
    an::tests::processes::ViewHandle target = targetViewOf("execute-source", source_image);
    ASSERT_NE(target, nullptr);

    an::tests::processes::framework::CaseLaunchCoordinator coordinator;
    coordinator.setManagedImageHooks([&](ImageViewControl* control, an::consumer::ImageAsset&& image,
                                         const std::string& history_step_name,
                                         const std::string& history_step_description) {
        EXPECT_EQ(control, target.get());
        EXPECT_TRUE(image.descriptor().has_value());
        EXPECT_FALSE(history_step_name.empty());
        EXPECT_FALSE(history_step_description.empty());
    });

    an::ui::extensions::ExtensionHostLaunchBinding binding;
    binding.descriptor_id = "filters.demo";
    binding.extension_id = "an.demo";
    binding.display_name = "Demo";
    binding.host_executable = host_path;
    binding.capabilities = {"execute"};

    std::string error;
    ASSERT_TRUE(coordinator.launchBoundDescriptor(binding, target.get(), &error)) << error;
    ASSERT_TRUE(pumpCoordinatorUntilNativeSessionReady(coordinator, binding.descriptor_id, 500 * kIpcTimingMultiplier));

    an::ui::extensions::ExtensionHostExecutionDispatchResult dispatch_result;
    ASSERT_TRUE(coordinator.submitExecuteForDescriptor(binding.descriptor_id, target.get(), &dispatch_result, &error))
        << error;
    EXPECT_EQ(dispatch_result.state, an::ui::extensions::ExtensionHostExecutionDispatchState::Started);

    // A target that shows no image has nothing to export, and the request is refused rather than sent.
    ASSERT_TRUE(stopCoordinatorAndWaitForSessionAbsence(coordinator, 500 * kIpcTimingMultiplier));
    ASSERT_TRUE(coordinator.launchBoundDescriptor(binding, fakeTargetControl(), &error)) << error;
    ASSERT_TRUE(pumpCoordinatorUntilNativeSessionReady(coordinator, binding.descriptor_id, 500 * kIpcTimingMultiplier));
    error.clear();
    EXPECT_FALSE(coordinator.submitExecuteForDescriptor(binding.descriptor_id, fakeTargetControl(), nullptr, &error));
    EXPECT_FALSE(error.empty());

    ASSERT_TRUE(stopCoordinatorAndWaitForSessionAbsence(coordinator, 500 * kIpcTimingMultiplier));
}

// An execute asked for while a worker writes the target image waits for the writer, as its text says, and the first
// pump after the writer lets go sends it. It failed at once while saying it waited, and nothing asked again.
TEST(ExtensionHostManagerTest, LaunchCoordinatorExecuteAskedWhileTheTargetIsWrittenIsSentOnceTheWriterLetsGo)
{
    const std::filesystem::path host_path = extensionHostPathForCurrentAppDir();
    AN_REQUIRE_FULL_EXTENSION_HOST(host_path);

    const auto source_image = makeRgbGradientImage(5, 4);
    ASSERT_TRUE(source_image);
    an::tests::processes::ViewHandle target = targetViewOf("execute-waits-for-writer", source_image);
    ASSERT_NE(target, nullptr);

    an::tests::processes::framework::CaseLaunchCoordinator coordinator;
    int presenter_count = 0;
    coordinator.setManagedImageHooks(
        [&](ImageViewControl* control, an::consumer::ImageAsset&& image, const std::string&, const std::string&) {
            ++presenter_count;
            EXPECT_EQ(control, target.get());
            EXPECT_TRUE(image.descriptor().has_value());
        });

    an::ui::extensions::ExtensionHostLaunchBinding binding;
    binding.descriptor_id = "filters.demo";
    binding.extension_id = "an.demo";
    binding.display_name = "Demo";
    binding.host_executable = host_path;
    binding.capabilities = {"execute"};
    binding.host_arguments = {"--execute-delay-ms", "40", "--stay-alive-ms",
                              std::to_string(3000 * kIpcTimingMultiplier)};

    std::string error;
    ASSERT_TRUE(coordinator.launchBoundDescriptor(binding, target.get(), &error)) << error;
    ASSERT_TRUE(pumpCoordinatorUntilNativeSessionReady(coordinator, binding.descriptor_id, 500 * kIpcTimingMultiplier));

    {
        an::Result<an::ui::ImageDocumentWriter> writer = target->document().tryWrite();
        ASSERT_TRUE(writer.hasValue()) << writer.status().message();

        an::ui::extensions::ExtensionHostExecutionDispatchResult dispatch_result;
        ASSERT_TRUE(
            coordinator.submitExecuteForDescriptor(binding.descriptor_id, target.get(), &dispatch_result, &error))
            << error;
        EXPECT_EQ(dispatch_result.state, an::ui::extensions::ExtensionHostExecutionDispatchState::Waiting);
        EXPECT_TRUE(error.empty()) << error;

        // Pumped while the writer holds the image, the execute is asked again at every pump and still waits.
        QElapsedTimer held;
        held.start();
        ASSERT_TRUE(pumpCoordinatorUntil(coordinator, 1000 * kIpcTimingMultiplier,
                                         [&]() { return held.elapsed() >= 200 * kIpcTimingMultiplier; }));
        EXPECT_EQ(presenter_count, 0) << "nothing is sent while the image is written";
        EXPECT_EQ(coordinator.activeSessionCount(), 1u);
    }

    ASSERT_TRUE(pumpCoordinatorUntil(coordinator, 2000 * kIpcTimingMultiplier, [&]() { return presenter_count == 1; }))
        << "the first pump after the writer lets go sends the execute";
    EXPECT_EQ(presenter_count, 1);

    ASSERT_TRUE(stopCoordinatorAndWaitForSessionAbsence(coordinator, 500 * kIpcTimingMultiplier));
}

// A result reaches the target with the history step the view commit names.
TEST(ExtensionHostManagerTest, LaunchCoordinatorExecutePrefersViewCommitHistoryMetadataToManagedResultPresenter)
{
    const std::filesystem::path host_path = extensionHostPathForCurrentAppDir();
    AN_REQUIRE_FULL_EXTENSION_HOST(host_path);

    const auto source_image = makeRgbGradientImage(5, 4);
    ASSERT_TRUE(source_image);

    const std::string expected_step_name = "Committed Extension Apply";
    const std::string expected_step_description = "Applied extension result via view.commit";

    an::tests::processes::ViewHandle target = targetViewOf("view-commit-history", source_image);
    ASSERT_NE(target, nullptr);

    an::tests::processes::framework::CaseLaunchCoordinator coordinator;
    int presenter_count = 0;
    std::string presented_step_name;
    std::string presented_step_description;
    coordinator.setManagedImageHooks([&](ImageViewControl* control, an::consumer::ImageAsset&& image,
                                         const std::string& history_step_name,
                                         const std::string& history_step_description) {
        ++presenter_count;
        EXPECT_EQ(control, target.get());
        EXPECT_TRUE(image.descriptor().has_value());
        presented_step_name = history_step_name;
        presented_step_description = history_step_description;
    });

    an::ui::extensions::ExtensionHostLaunchBinding binding;
    binding.descriptor_id = "filters.demo";
    binding.extension_id = "an.demo";
    binding.display_name = "Demo";
    binding.host_executable = host_path;
    binding.capabilities = {"execute"};
    binding.host_arguments = {"--execute-delay-ms",
                              "40",
                              "--view-commit-history-step-name",
                              expected_step_name,
                              "--view-commit-history-step-description",
                              expected_step_description,
                              "--stay-alive-ms",
                              std::to_string(1500 * kIpcTimingMultiplier)};

    std::string error;
    ASSERT_TRUE(coordinator.launchBoundDescriptor(binding, target.get(), &error)) << error;
    ASSERT_TRUE(pumpCoordinatorUntilNativeSessionReady(coordinator, binding.descriptor_id, 500 * kIpcTimingMultiplier));

    an::ui::extensions::ExtensionHostExecutionDispatchResult dispatch_result;
    ASSERT_TRUE(coordinator.submitExecuteForDescriptor(binding.descriptor_id, target.get(), &dispatch_result, &error))
        << error;
    EXPECT_EQ(dispatch_result.state, an::ui::extensions::ExtensionHostExecutionDispatchState::Started);

    ASSERT_TRUE(pumpCoordinatorUntil(coordinator, 1500 * kIpcTimingMultiplier,
                                     [&]() { return coordinator.activeSessionCount() == 0u && presenter_count == 1; }));

    EXPECT_EQ(presenter_count, 1);
    EXPECT_EQ(presented_step_name, expected_step_name);
    EXPECT_EQ(presented_step_description, expected_step_description);

    ASSERT_TRUE(stopCoordinatorAndWaitForSessionAbsence(coordinator, 500 * kIpcTimingMultiplier));
}

// A process window that does not keep open closes right after its Apply commits: the host sends the commit, says the
// window closed and exits, and the result still reaches the target before the session ends and its buffers go.
TEST(ExtensionHostManagerTest, LaunchCoordinatorLandsTheResultAWindowCommittedAsItClosed)
{
    const std::filesystem::path host_path = extensionHostPathForCurrentAppDir();
    AN_REQUIRE_FULL_EXTENSION_HOST(host_path);

    const auto source_image = makeRgbGradientImage(5, 4);
    ASSERT_TRUE(source_image);
    an::tests::processes::ViewHandle target = targetViewOf("closed-after-commit", source_image);
    ASSERT_NE(target, nullptr);

    auto& runtime_status_registry = an::ui::extensions::ExtensionRuntimeStatusRegistry::instance();
    runtime_status_registry.clear();

    an::tests::processes::framework::CaseLaunchCoordinator coordinator;
    int presenter_count = 0;
    coordinator.setManagedImageHooks([&](ImageViewControl* control, an::consumer::ImageAsset&& image,
                                         const std::string& history_step_name, const std::string&) {
        ++presenter_count;
        EXPECT_EQ(control, target.get());
        EXPECT_TRUE(image.descriptor().has_value());
        EXPECT_FALSE(history_step_name.empty());
    });

    an::ui::extensions::ExtensionHostLaunchBinding binding;
    binding.descriptor_id = "filters.demo";
    binding.extension_id = "an.demo.closed_after_commit";
    binding.display_name = "Demo";
    binding.host_executable = host_path;
    binding.capabilities = {"execute"};
    binding.host_arguments = {"--execute-delay-ms", "40", "--close-after-view-commit"};

    std::string error;
    ASSERT_TRUE(coordinator.launchBoundDescriptor(binding, target.get(), &error)) << error;
    ASSERT_TRUE(pumpCoordinatorUntilNativeSessionReady(coordinator, binding.descriptor_id, 500 * kIpcTimingMultiplier));

    an::ui::extensions::ExtensionHostExecutionDispatchResult dispatch_result;
    ASSERT_TRUE(coordinator.submitExecuteForDescriptor(binding.descriptor_id, target.get(), &dispatch_result, &error))
        << error;
    EXPECT_EQ(dispatch_result.state, an::ui::extensions::ExtensionHostExecutionDispatchState::Started);

    EXPECT_TRUE(pumpCoordinatorUntil(coordinator, 1500 * kIpcTimingMultiplier,
                                     [&]() { return coordinator.activeSessionCount() == 0u && presenter_count == 1; }))
        << "the result committed as the window closed was presented " << presenter_count << " times";
    EXPECT_EQ(presenter_count, 1);

    // The session ended as a closed window does - no crash, no forced stop - and took its buffers with it.
    const auto snapshot = runtime_status_registry.snapshotForExtension(binding.extension_id);
    ASSERT_TRUE(snapshot.has_value());
    EXPECT_TRUE(snapshot->has_termination_result);
    EXPECT_EQ(snapshot->termination_kind, an::ui::extensions::ExtensionRuntimeTerminationKind::Disconnected);
    ASSERT_FALSE(snapshot->termination_diagnostics.session_id.empty());
    const auto session_buffer_root = an::ui::extensions::SharedBufferManager::defaultSharedBufferRootPath() /
                                     snapshot->termination_diagnostics.session_id;
    EXPECT_TRUE(pathMissingOrEmpty(session_buffer_root));

    ASSERT_TRUE(stopCoordinatorAndWaitForSessionAbsence(coordinator, 500 * kIpcTimingMultiplier));
    runtime_status_registry.clear();
}

// A managed execute hands the target the host's result with the source's samples.
TEST(ExtensionHostManagerTest, ManagedExecuteRoundTripsSharedBufferPixelsEndToEnd)
{
    const std::filesystem::path host_path = extensionHostPathForCurrentAppDir();
    AN_REQUIRE_FULL_EXTENSION_HOST(host_path);

    constexpr uint32_t kWidth = 256;
    constexpr uint32_t kHeight = 192;
    const auto source_image = makeRgbGradientImage(kWidth, kHeight);
    ASSERT_TRUE(source_image);

    an::tests::processes::ViewHandle target = targetViewOf("e2e-roundtrip", source_image);
    ASSERT_NE(target, nullptr);

    an::tests::processes::framework::CaseLaunchCoordinator coordinator;
    int presenter_count = 0;
    std::optional<an::consumer::ImageAsset> presented_image;
    coordinator.setManagedImageHooks([&](ImageViewControl* control, an::consumer::ImageAsset&& image,
                                         const std::string& history_step_name,
                                         const std::string& history_step_description) {
        ++presenter_count;
        EXPECT_EQ(control, target.get());
        EXPECT_FALSE(history_step_name.empty());
        EXPECT_FALSE(history_step_description.empty());
        presented_image.emplace(std::move(image));
    });

    an::ui::extensions::ExtensionHostLaunchBinding binding;
    binding.descriptor_id = "filters.e2e_roundtrip";
    binding.extension_id = "an.demo";
    binding.display_name = "E2E Roundtrip";
    binding.host_executable = host_path;
    binding.capabilities = {"execute"};
    binding.host_arguments = {"--execute-delay-ms", "40", "--stay-alive-ms",
                              std::to_string(2000 * kIpcTimingMultiplier)};

    std::string error;
    ASSERT_TRUE(coordinator.launchBoundDescriptor(binding, target.get(), &error)) << error;
    ASSERT_TRUE(pumpCoordinatorUntilNativeSessionReady(coordinator, binding.descriptor_id, 500 * kIpcTimingMultiplier));

    an::ui::extensions::ExtensionHostExecutionDispatchResult dispatch_result;
    ASSERT_TRUE(coordinator.submitExecuteForDescriptor(binding.descriptor_id, target.get(), &dispatch_result, &error))
        << error;
    EXPECT_EQ(dispatch_result.state, an::ui::extensions::ExtensionHostExecutionDispatchState::Started);

    ASSERT_TRUE(pumpCoordinatorUntil(coordinator, 2000 * kIpcTimingMultiplier, [&]() { return presenter_count == 1; }))
        << "Timed out waiting for result presenter callback";

    ASSERT_EQ(presenter_count, 1);
    ASSERT_TRUE(presented_image);
    EXPECT_EQ(widthOf(*presented_image), kWidth);
    EXPECT_EQ(heightOf(*presented_image), kHeight);
    EXPECT_EQ(channelsOf(*presented_image), 3u);

    const std::array<std::pair<uint32_t, uint32_t>, 5> sample_points = {
        {{0u, 0u}, {kWidth / 2u, kHeight / 2u}, {kWidth - 1u, kHeight - 1u}, {37u, 113u}, {200u, 50u}}};
    for (const auto& [x, y] : sample_points)
    {
        EXPECT_NEAR(sampleAt(*presented_image, x, y, 0), sampleAt(*source_image, x, y, 0), 1e-6)
            << "red channel mismatch at (" << x << "," << y << ")";
        EXPECT_NEAR(sampleAt(*presented_image, x, y, 1), sampleAt(*source_image, x, y, 1), 1e-6)
            << "green channel mismatch at (" << x << "," << y << ")";
        EXPECT_NEAR(sampleAt(*presented_image, x, y, 2), sampleAt(*source_image, x, y, 2), 1e-6)
            << "blue channel mismatch at (" << x << "," << y << ")";
    }

    ASSERT_TRUE(stopCoordinatorAndWaitForSessionAbsence(coordinator, 500 * kIpcTimingMultiplier));
}

// Parameter inspection ignores a `build_ui` method an extension defines.
TEST(ExtensionHostManagerTest, HostInspectIgnoresABuildUiMethodForParamInspection)
{
    const std::filesystem::path host_path = extensionHostPathForCurrentAppDir();
    const std::filesystem::path runtime_root = runtimeRootForCurrentAppDir();
    const std::filesystem::path bundled_python = repositoryBundledPythonPath();
    AN_REQUIRE_FULL_EXTENSION_HOST(host_path);
    ASSERT_TRUE(std::filesystem::exists(runtime_root)) << runtime_root;
    ASSERT_FALSE(bundled_python.empty()) << "Repository bundled Python runtime must be available for inspect coverage";

    const auto temp_root = uniqueExtensionRuntimeRootForTest("inspect-build-ui-ignored");
    removeAllNoThrow(temp_root);
    const RemovePathOnExit remove_temp_root{temp_root};
    std::filesystem::create_directories(temp_root);

    const auto env_root = temp_root / "env";
    const auto package_root = temp_root / "inspect_build_ui_ignored_ext";
    std::filesystem::create_directories(package_root);
    createVirtualEnvironment(bundled_python, env_root);

    {
        std::ofstream out(package_root / "inspect_build_ui_ignored_ext.py", std::ios::binary);
        ASSERT_TRUE(out.is_open());
        out << "import afternight\n\n"
            << "class InspectBuildUiIgnoredExtension(afternight.ProcessExtension):\n"
            << "    def get_params(self):\n"
            << "        return [\n"
            << "            {'id': 'strength', 'type': 'double', 'label': 'Strength', 'default': 0.45, 'min': 0.0, "
               "'max': 1.0, 'step': 0.05},\n"
            << "            {'id': 'mode', 'type': 'choice', 'label': 'Mode', 'default': 'balanced', 'options': "
               "[('Balanced', 'balanced'), ('Gentle', 'gentle')]},\n"
            << "        ]\n\n"
            << "    def build_ui(self, ui):\n"
            << "        raise RuntimeError('build_ui should not affect inspect when build_params_ui is "
               "absent')\n\n"
            << "    def execute(self, target, src_image, dst_image, params, progress, masks=None, weights=None, "
               "output_masks=None):\n"
            << "        progress.set_text('Copying image')\n"
            << "        dst_image.copy_from(src_image)\n";
    }

    QProcess inspect;
    inspect.setProgram(QString::fromStdString(host_path.string()));
    inspect.setProcessChannelMode(QProcess::MergedChannels);
    inspect.setArguments({QStringLiteral("--inspect"), QStringLiteral("--inspect-params"),
                          QStringLiteral("--sdk-backend"), QStringLiteral("runtime"),
                          QStringLiteral("--extension-package-root"), QString::fromStdString(package_root.string()),
                          QStringLiteral("--entry-point"), QStringLiteral("inspect_build_ui_ignored_ext"),
                          QStringLiteral("--process-class"), QStringLiteral("InspectBuildUiIgnoredExtension"),
                          QStringLiteral("--environment-root"), QString::fromStdString(env_root.string()),
                          QStringLiteral("--runtime-root"), QString::fromStdString(runtime_root.string())});
    inspect.start();
    ASSERT_TRUE(inspect.waitForStarted());
    ASSERT_TRUE(inspect.waitForFinished(hostProcessWaitMs()));

    const std::string output = inspect.readAllStandardOutput().toStdString();
    EXPECT_EQ(inspect.exitStatus(), QProcess::NormalExit) << output;
    EXPECT_EQ(inspect.exitCode(), 0) << output;

    QJsonParseError parse_error;
    const QJsonDocument document = QJsonDocument::fromJson(QByteArray::fromStdString(output).trimmed(), &parse_error);
    ASSERT_EQ(parse_error.error, QJsonParseError::NoError) << output;
    ASSERT_TRUE(document.isObject()) << output;

    const QJsonArray schema = document.object().value(QStringLiteral("params_schema")).toArray();
    ASSERT_EQ(schema.size(), 2) << output;
    EXPECT_EQ(schema.at(0).toObject().value(QStringLiteral("id")).toString(), QStringLiteral("strength"));
    EXPECT_EQ(schema.at(0).toObject().value(QStringLiteral("type")).toString(), QStringLiteral("double"));
    EXPECT_EQ(schema.at(1).toObject().value(QStringLiteral("id")).toString(), QStringLiteral("mode"));
    EXPECT_EQ(schema.at(1).toObject().value(QStringLiteral("type")).toString(), QStringLiteral("choice"));
    EXPECT_FALSE(document.object().value(QStringLiteral("supports_ui_events")).toBool()) << output;
    EXPECT_FALSE(document.object().value(QStringLiteral("supports_ui_validation")).toBool()) << output;

    removeAllNoThrow(temp_root);
}

// Parameter inspection reads the schema even when the extension builds its own parameter interface.
TEST(ExtensionHostManagerTest, HostInspectKeepsParamSchemaInspectionWhenBuildParamsUiExists)
{
    const std::filesystem::path host_path = extensionHostPathForCurrentAppDir();
    const std::filesystem::path runtime_root = runtimeRootForCurrentAppDir();
    const std::filesystem::path bundled_python = repositoryBundledPythonPath();
    AN_REQUIRE_FULL_EXTENSION_HOST(host_path);
    ASSERT_TRUE(std::filesystem::exists(runtime_root)) << runtime_root;
    ASSERT_FALSE(bundled_python.empty()) << "Repository bundled Python runtime must be available";

    const auto temp_root = uniqueExtensionRuntimeRootForTest("inspect-build-params-ui-present");
    removeAllNoThrow(temp_root);
    const RemovePathOnExit remove_temp_root{temp_root};
    std::filesystem::create_directories(temp_root);

    const auto env_root = temp_root / "env";
    const auto package_root = temp_root / "inspect_build_params_ui_present_ext";
    std::filesystem::create_directories(package_root);
    createVirtualEnvironment(bundled_python, env_root);

    {
        std::ofstream out(package_root / "inspect_build_params_ui_present_ext.py", std::ios::binary);
        ASSERT_TRUE(out.is_open());
        out << "import afternight\n\n"
            << "class InspectBuildParamsUiPresentExtension(afternight.ProcessExtension):\n"
            << "    def get_params(self):\n"
            << "        return [\n"
            << "            {'id': 'strength', 'type': 'double', 'label': 'Strength', 'default': 0.5, 'min': 0.0, "
               "'max': 1.0, 'step': 0.05},\n"
            << "            {'id': 'mode', 'type': 'choice', 'label': 'Mode', 'default': 'balanced', 'options': "
               "[('Balanced', 'balanced'), ('Gentle', 'gentle')]},\n"
            << "        ]\n\n"
            << "    def build_params_ui(self, _ui_builder):\n"
            << "        raise RuntimeError('build_params_ui should not affect inspect on the native process-window "
               "path')\n\n"
            << "    def execute(self, target, src_image, dst_image, params, progress, masks=None, weights=None, "
               "output_masks=None):\n"
            << "        dst_image.copy_from(src_image)\n";
    }

    QProcess inspect;
    inspect.setProgram(QString::fromStdString(host_path.string()));
    inspect.setProcessChannelMode(QProcess::MergedChannels);
    inspect.setArguments({QStringLiteral("--inspect"), QStringLiteral("--inspect-params"),
                          QStringLiteral("--sdk-backend"), QStringLiteral("runtime"),
                          QStringLiteral("--extension-package-root"), QString::fromStdString(package_root.string()),
                          QStringLiteral("--entry-point"), QStringLiteral("inspect_build_params_ui_present_ext"),
                          QStringLiteral("--process-class"), QStringLiteral("InspectBuildParamsUiPresentExtension"),
                          QStringLiteral("--environment-root"), QString::fromStdString(env_root.string()),
                          QStringLiteral("--runtime-root"), QString::fromStdString(runtime_root.string())});
    inspect.start();
    ASSERT_TRUE(inspect.waitForStarted());
    ASSERT_TRUE(inspect.waitForFinished(hostProcessWaitMs()));

    const std::string output = inspect.readAllStandardOutput().toStdString();
    EXPECT_EQ(inspect.exitStatus(), QProcess::NormalExit) << output;
    EXPECT_EQ(inspect.exitCode(), 0) << output;

    QJsonParseError parse_error;
    const QJsonDocument document = QJsonDocument::fromJson(QByteArray::fromStdString(output).trimmed(), &parse_error);
    ASSERT_EQ(parse_error.error, QJsonParseError::NoError) << output;
    ASSERT_TRUE(document.isObject()) << output;

    const QJsonArray schema = document.object().value(QStringLiteral("params_schema")).toArray();
    ASSERT_EQ(schema.size(), 2) << output;
    EXPECT_EQ(schema.at(0).toObject().value(QStringLiteral("id")).toString(), QStringLiteral("strength"));
    EXPECT_EQ(schema.at(1).toObject().value(QStringLiteral("id")).toString(), QStringLiteral("mode"));
    EXPECT_FALSE(document.object().value(QStringLiteral("supports_ui_events")).toBool()) << output;
    EXPECT_FALSE(document.object().value(QStringLiteral("supports_ui_validation")).toBool()) << output;

    removeAllNoThrow(temp_root);
}

// A native window launch does not call the extension's parameter-interface hook.
TEST(ExtensionHostManagerTest, LaunchCoordinatorIgnoresBuildParamsUiCompatibilityHookOnNativeProcessWindowPath)
{
    const std::filesystem::path host_path = extensionHostPathForCurrentAppDir();
    const std::filesystem::path runtime_root = runtimeRootForCurrentAppDir();
    const std::filesystem::path bundled_python = repositoryBundledPythonPath();
    AN_REQUIRE_FULL_EXTENSION_HOST(host_path);
    ASSERT_TRUE(std::filesystem::exists(runtime_root)) << runtime_root;
    ASSERT_FALSE(bundled_python.empty()) << "Repository bundled Python runtime must be available";

    const auto temp_root = uniqueExtensionRuntimeRootForTest("native-ignore-build-params-ui");
    removeAllNoThrow(temp_root);
    const RemovePathOnExit remove_temp_root{temp_root};
    std::filesystem::create_directories(temp_root);

    const auto env_root = temp_root / "env";
    const auto package_root = temp_root / "ignore_build_params_ui_ext";
    std::filesystem::create_directories(package_root);
    createVirtualEnvironment(bundled_python, env_root);

    {
        std::ofstream out(package_root / "ignore_build_params_ui_ext.py", std::ios::binary);
        ASSERT_TRUE(out.is_open());
        out << "import afternight\n\n"
            << "class IgnoreBuildParamsUiExtension(afternight.ProcessExtension):\n"
            << "    def get_params(self):\n"
            << "        return [\n"
            << "            {'id': 'strength', 'type': 'double', 'label': 'Strength', 'default': 0.5, 'min': 0.0, "
               "'max': 1.0, 'step': 0.05}\n"
            << "        ]\n\n"
            << "    def build_params_ui(self, _ui_builder):\n"
            << "        raise RuntimeError('build_params_ui should not be called on the native process-window "
               "path')\n\n"
            << "    def execute(self, target, src_image, dst_image, params, progress, masks=None, weights=None, "
               "output_masks=None):\n"
            << "        dst_image.copy_from(src_image)\n";
    }

    an::tests::processes::framework::CaseLaunchCoordinator coordinator;
    an::ui::extensions::ExtensionHostLaunchBinding binding;
    binding.descriptor_id = "filters.ignore_build_params_ui";
    binding.extension_id = "an.demo.ignore_build_params_ui";
    binding.display_name = "Ignore Build Params UI";
    binding.host_executable = host_path;
    binding.capabilities = {"execute"};
    binding.process_capabilities.ui = "custom";
    binding.host_arguments = {
        "--extension-package-root",   package_root.string(), "--entry-point",
        "ignore_build_params_ui_ext", "--process-class",     "IgnoreBuildParamsUiExtension",
        "--environment-root",         env_root.string(),     "--runtime-root",
        runtime_root.string(),        "--stay-alive-ms",     std::to_string(3000 * kIpcTimingMultiplier)};

    std::string error;
    ASSERT_TRUE(coordinator.launchBoundDescriptor(binding, fakeTargetControl(), &error)) << error;
    ASSERT_TRUE(pumpCoordinatorUntilNativeSessionReady(coordinator, binding.descriptor_id, 750 * kIpcTimingMultiplier));

    ASSERT_TRUE(stopCoordinatorAndWaitForSessionAbsence(coordinator, 500 * kIpcTimingMultiplier));
    removeAllNoThrow(temp_root);
}

// A native window launch does not call a `build_ui` method the extension defines.
TEST(ExtensionHostManagerTest, LaunchCoordinatorIgnoresABuildUiMethodOnNativeProcessWindowPath)
{
    const std::filesystem::path host_path = extensionHostPathForCurrentAppDir();
    const std::filesystem::path runtime_root = runtimeRootForCurrentAppDir();
    const std::filesystem::path bundled_python = repositoryBundledPythonPath();
    AN_REQUIRE_FULL_EXTENSION_HOST(host_path);
    ASSERT_TRUE(std::filesystem::exists(runtime_root)) << runtime_root;
    ASSERT_FALSE(bundled_python.empty()) << "Repository bundled Python runtime must be available";

    const auto temp_root = uniqueExtensionRuntimeRootForTest("native-ignore-build-ui");
    removeAllNoThrow(temp_root);
    const RemovePathOnExit remove_temp_root{temp_root};
    std::filesystem::create_directories(temp_root);

    const auto env_root = temp_root / "env";
    const auto package_root = temp_root / "ignore_build_ui_ext";
    std::filesystem::create_directories(package_root);
    createVirtualEnvironment(bundled_python, env_root);

    {
        std::ofstream out(package_root / "ignore_build_ui_ext.py", std::ios::binary);
        ASSERT_TRUE(out.is_open());
        out << "import afternight\n\n"
            << "class IgnoreBuildUiExtension(afternight.ProcessExtension):\n"
            << "    def get_params(self):\n"
            << "        return [\n"
            << "            {'id': 'strength', 'type': 'double', 'label': 'Strength', 'default': 0.45, 'min': 0.0, "
               "'max': 1.0, 'step': 0.05}\n"
            << "        ]\n\n"
            << "    def build_ui(self, _ui_builder):\n"
            << "        raise RuntimeError('build_ui should not be called on the native process-window "
               "path')\n\n"
            << "    def execute(self, target, src_image, dst_image, params, progress, masks=None, weights=None, "
               "output_masks=None):\n"
            << "        dst_image.copy_from(src_image)\n";
    }

    an::tests::processes::framework::CaseLaunchCoordinator coordinator;
    an::ui::extensions::ExtensionHostLaunchBinding binding;
    binding.descriptor_id = "filters.ignore_build_ui";
    binding.extension_id = "an.demo.ignore_build_ui";
    binding.display_name = "Ignore Build UI";
    binding.host_executable = host_path;
    binding.capabilities = {"execute"};
    binding.process_capabilities.ui = "custom";
    binding.host_arguments = {
        "--extension-package-root", package_root.string(), "--entry-point",
        "ignore_build_ui_ext",      "--process-class",     "IgnoreBuildUiExtension",
        "--environment-root",       env_root.string(),     "--runtime-root",
        runtime_root.string(),      "--stay-alive-ms",     std::to_string(3000 * kIpcTimingMultiplier)};

    std::string error;
    ASSERT_TRUE(coordinator.launchBoundDescriptor(binding, fakeTargetControl(), &error)) << error;
    ASSERT_TRUE(pumpCoordinatorUntilNativeSessionReady(coordinator, binding.descriptor_id, 750 * kIpcTimingMultiplier));

    ASSERT_TRUE(stopCoordinatorAndWaitForSessionAbsence(coordinator, 500 * kIpcTimingMultiplier));
    removeAllNoThrow(temp_root);
}

// An execute of a process-window extension runs with its parameters' defaults.
TEST(ExtensionHostManagerTest, LaunchCoordinatorExecutesProcessWindowCompatibilitySubclassWithDefaultParams)
{
    const std::filesystem::path host_path = extensionHostPathForCurrentAppDir();
    const std::filesystem::path runtime_root = runtimeRootForCurrentAppDir();
    const std::filesystem::path bundled_python = repositoryBundledPythonPath();
    AN_REQUIRE_FULL_EXTENSION_HOST(host_path);
    ASSERT_TRUE(std::filesystem::exists(runtime_root)) << runtime_root;
    ASSERT_FALSE(bundled_python.empty()) << "Repository bundled Python runtime must be available";

    const auto temp_root = uniqueExtensionRuntimeRootForTest("process-window-default-params");
    removeAllNoThrow(temp_root);
    const RemovePathOnExit remove_temp_root{temp_root};
    std::filesystem::create_directories(temp_root);

    const auto env_root = temp_root / "env";
    const auto package_root = temp_root / "process_window_default_params_ext";
    std::filesystem::create_directories(package_root);
    createVirtualEnvironment(bundled_python, env_root);

    {
        std::ofstream out(package_root / "process_window_default_params_ext.py", std::ios::binary);
        ASSERT_TRUE(out.is_open());
        out << "from afternight import ui\n\n"
            << "class ProcessWindowDefaultParamsExtension(ui.ProcessWindow):\n"
            << "    def get_params(self):\n"
            << "        return [\n"
            << "            {'id': 'strength', 'type': 'double', 'label': 'Strength', 'default': 0.6, 'min': 0.0, "
               "'max': 1.0, 'step': 0.05},\n"
            << "            {'id': 'apply_mask', 'type': 'bool', 'label': 'Apply Mask', 'default': False},\n"
            << "            {'id': 'mode', 'type': 'choice', 'label': 'Mode', 'default': 'balanced', 'options': "
               "[('Balanced', 'balanced'), ('Gentle', 'gentle')]},\n"
            << "            {'id': 'note', 'type': 'string', 'label': 'Note', 'default': 'auto-generated', "
               "'placeholder': 'Enter note'},\n"
            << "        ]\n\n"
            << "    def build_params_ui(self, _ui_builder):\n"
            << "        raise RuntimeError('build_params_ui should not be called for ProcessWindow subclasses on the "
               "native path')\n\n"
            << "    def execute(self, target, src_image, dst_image, params, progress, masks=None, weights=None, "
               "output_masks=None):\n"
            << "        self.settings.set('last_params', params)\n"
            << "        dst_image.copy_from(src_image)\n";
    }

    const auto source_image = makeRgbGradientImage(5, 4);
    ASSERT_TRUE(source_image);

    InMemorySettings settings_store;
    GlobalSettingsPersistence& settings = settings_store.repository();
    an::ui::extensions::ExtensionSdkService sdk_service(&settings);
    an::tests::processes::ViewHandle target = targetViewOf("process-window-default-params", source_image);
    ASSERT_NE(target, nullptr);

    an::tests::processes::framework::CaseLaunchCoordinator coordinator;
    coordinator.configureSdkService(&settings);

    coordinator.setManagedImageHooks([&](ImageViewControl* control, an::consumer::ImageAsset&& image,
                                         const std::string& history_step_name,
                                         const std::string& history_step_description) {
        EXPECT_EQ(control, target.get());
        EXPECT_TRUE(image.descriptor().has_value());
        EXPECT_FALSE(history_step_name.empty());
        EXPECT_FALSE(history_step_description.empty());
    });

    an::ui::extensions::ExtensionHostLaunchBinding binding;
    binding.descriptor_id = "filters.process_window_default_params";
    binding.extension_id = "an.demo.process_window_default_params";
    binding.display_name = "ProcessWindow Default Params";
    binding.host_executable = host_path;
    binding.capabilities = {"execute"};
    binding.host_arguments = {"--extension-package-root",
                              package_root.string(),
                              "--entry-point",
                              "process_window_default_params_ext",
                              "--process-class",
                              "ProcessWindowDefaultParamsExtension",
                              "--environment-root",
                              env_root.string(),
                              "--runtime-root",
                              runtime_root.string(),
                              "--stay-alive-ms",
                              std::to_string(3000 * kIpcTimingMultiplier)};

    std::string error;
    ASSERT_TRUE(coordinator.launchBoundDescriptor(binding, target.get(), &error)) << error;
    ASSERT_TRUE(pumpCoordinatorUntilNativeSessionReady(coordinator, binding.descriptor_id, 750 * kIpcTimingMultiplier));

    an::ui::extensions::ExtensionHostExecutionDispatchResult dispatch_result;
    ASSERT_TRUE(coordinator.submitExecuteForDescriptor(binding.descriptor_id, target.get(), &dispatch_result, &error))
        << error;
    EXPECT_EQ(dispatch_result.state, an::ui::extensions::ExtensionHostExecutionDispatchState::Started);

    ASSERT_TRUE(pumpCoordinatorUntil(coordinator, 3000 * kIpcTimingMultiplier, [&]() {
        QCborValue params_value;
        return sdk_service.selectExtensionSetting("an.demo.process_window_default_params", "last_params", &params_value,
                                                  QCborValue(), nullptr) &&
               params_value.isMap();
    })) << error;

    QCborValue params_value;
    ASSERT_TRUE(sdk_service.selectExtensionSetting("an.demo.process_window_default_params", "last_params",
                                                   &params_value, QCborValue(), &error))
        << error;
    ASSERT_TRUE(params_value.isMap());

    const QCborMap params = params_value.toMap();
    EXPECT_DOUBLE_EQ(params.value(QStringLiteral("strength")).toDouble(), 0.6);
    EXPECT_FALSE(params.value(QStringLiteral("apply_mask")).toBool());
    EXPECT_EQ(params.value(QStringLiteral("mode")).toString(), QStringLiteral("balanced"));
    EXPECT_EQ(params.value(QStringLiteral("note")).toString(), QStringLiteral("auto-generated"));

    ASSERT_TRUE(stopCoordinatorAndWaitForSessionAbsence(coordinator, 500 * kIpcTimingMultiplier));
    removeAllNoThrow(temp_root);
}

// A changed extension setting refreshes the cached parameter schema at the next launch.
TEST(ExtensionHostManagerTest, LaunchCoordinatorRefreshesCachedAutoUiSchemaWhenExtensionSettingsChange)
{
    const std::filesystem::path host_path = extensionHostPathForCurrentAppDir();
    const std::filesystem::path runtime_root = runtimeRootForCurrentAppDir();
    const std::filesystem::path bundled_python = repositoryBundledPythonPath();
    AN_REQUIRE_FULL_EXTENSION_HOST(host_path);
    ASSERT_TRUE(std::filesystem::exists(runtime_root)) << runtime_root;
    ASSERT_FALSE(bundled_python.empty()) << "Repository bundled Python runtime must be available";

    const auto temp_root = uniqueExtensionRuntimeRootForTest("process-window-settings-refresh");
    removeAllNoThrow(temp_root);
    const RemovePathOnExit remove_temp_root{temp_root};
    std::filesystem::create_directories(temp_root);

    const auto env_root = temp_root / "env";
    const auto package_root = temp_root / "process_window_settings_refresh_ext";
    std::filesystem::create_directories(package_root);
    createVirtualEnvironment(bundled_python, env_root);

    {
        std::ofstream out(package_root / "process_window_settings_refresh_ext.py", std::ios::binary);
        ASSERT_TRUE(out.is_open());
        out << "from afternight import ui\n\n"
            << "class ProcessWindowSettingsRefreshExtension(ui.ProcessWindow):\n"
            << "    def get_params(self):\n"
            << "        selected = str(self.settings.get('model_version', 'latest') or 'latest')\n"
            << "        return [\n"
            << "            {'id': 'ai_version', 'type': 'choice', 'label': 'AI Model Version', 'default': selected, "
               "'options': [('Latest (auto)', 'latest'), ('1.0.1', '1.0.1'), ('1.0.0', '1.0.0')]},\n"
            << "        ]\n\n"
            << "    def execute(self, target, src_image, dst_image, params, progress, masks=None, weights=None, "
               "output_masks=None):\n"
            << "        self.settings.set('last_params', params)\n"
            << "        dst_image.copy_from(src_image)\n";
    }

    const auto source_image = makeRgbGradientImage(5, 4);
    ASSERT_TRUE(source_image);

    InMemorySettings settings_store;
    GlobalSettingsPersistence& settings = settings_store.repository();
    an::ui::extensions::ExtensionSdkService sdk_service(&settings);
    an::tests::processes::ViewHandle target = targetViewOf("process-window-settings-refresh", source_image);
    ASSERT_NE(target, nullptr);

    an::tests::processes::framework::CaseLaunchCoordinator coordinator;
    coordinator.configureSdkService(&settings);

    coordinator.setManagedImageHooks([&](ImageViewControl* control, an::consumer::ImageAsset&& image,
                                         const std::string& history_step_name,
                                         const std::string& history_step_description) {
        EXPECT_EQ(control, target.get());
        EXPECT_TRUE(image.descriptor().has_value());
        EXPECT_FALSE(history_step_name.empty());
        EXPECT_FALSE(history_step_description.empty());
    });

    an::ui::extensions::ExtensionHostLaunchBinding binding;
    binding.descriptor_id = "filters.process_window_settings_refresh";
    binding.extension_id = "an.demo.process_window_settings_refresh";
    binding.display_name = "ProcessWindow Settings Refresh";
    binding.host_executable = host_path;
    binding.capabilities = {"execute"};
    binding.host_arguments = {"--extension-package-root",
                              package_root.string(),
                              "--entry-point",
                              "process_window_settings_refresh_ext",
                              "--process-class",
                              "ProcessWindowSettingsRefreshExtension",
                              "--environment-root",
                              env_root.string(),
                              "--runtime-root",
                              runtime_root.string(),
                              "--stay-alive-ms",
                              std::to_string(3000 * kIpcTimingMultiplier)};

    const auto run_and_read_ai_version = [&](const QString& expected_value) {
        std::string error;
        ASSERT_TRUE(coordinator.launchBoundDescriptor(binding, target.get(), &error)) << error;
        ASSERT_TRUE(
            pumpCoordinatorUntilNativeSessionReady(coordinator, binding.descriptor_id, 750 * kIpcTimingMultiplier));

        an::ui::extensions::ExtensionHostExecutionDispatchResult dispatch_result;
        ASSERT_TRUE(
            coordinator.submitExecuteForDescriptor(binding.descriptor_id, target.get(), &dispatch_result, &error))
            << error;
        EXPECT_EQ(dispatch_result.state, an::ui::extensions::ExtensionHostExecutionDispatchState::Started);

        ASSERT_TRUE(pumpCoordinatorUntil(coordinator, 3000 * kIpcTimingMultiplier, [&]() {
            QCborValue params_value;
            if (!sdk_service.selectExtensionSetting(binding.extension_id, "last_params", &params_value, QCborValue(),
                                                    nullptr) ||
                !params_value.isMap())
            {
                return false;
            }

            return params_value.toMap().value(QStringLiteral("ai_version")).toString() == expected_value;
        })) << error;

        QCborValue params_value;
        ASSERT_TRUE(sdk_service.selectExtensionSetting(binding.extension_id, "last_params", &params_value, QCborValue(),
                                                       &error))
            << error;
        ASSERT_TRUE(params_value.isMap());
        EXPECT_EQ(params_value.toMap().value(QStringLiteral("ai_version")).toString(), expected_value);

        ASSERT_TRUE(stopCoordinatorAndWaitForSessionAbsence(coordinator, 500 * kIpcTimingMultiplier));
    };

    run_and_read_ai_version(QStringLiteral("latest"));

    std::string error;
    ASSERT_TRUE(sdk_service.upsertExtensionSetting(binding.extension_id, "model_version",
                                                   QCborValue(QStringLiteral("1.0.0")), &error))
        << error;

    run_and_read_ai_version(QStringLiteral("1.0.0"));

    removeAllNoThrow(temp_root);
}

// Closing a native window during an execute cancels it and presents nothing.
TEST(ExtensionHostManagerTest, LaunchCoordinatorCancelsNativeWindowExecuteWhenProcessWindowCloses)
{
    const std::filesystem::path host_path = extensionHostPathForCurrentAppDir();
    const std::filesystem::path runtime_root = runtimeRootForCurrentAppDir();
    const std::filesystem::path bundled_python = repositoryBundledPythonPath();
    AN_REQUIRE_FULL_EXTENSION_HOST(host_path);
    ASSERT_TRUE(std::filesystem::exists(runtime_root)) << runtime_root;
    ASSERT_FALSE(bundled_python.empty()) << "Repository bundled Python runtime must be available";

    const QString platform = QGuiApplication::platformName().trimmed().toLower();
    if (platform == QStringLiteral("offscreen") || platform == QStringLiteral("minimal"))
    {
        GTEST_SKIP() << "Native process windows are not created on headless Qt platforms";
    }

    const auto temp_root = uniqueExtensionRuntimeRootForTest("process-window-close-cancel");
    removeAllNoThrow(temp_root);
    const RemovePathOnExit remove_temp_root{temp_root};
    std::filesystem::create_directories(temp_root);

    const auto env_root = temp_root / "env";
    const auto package_root = temp_root / "process_window_close_cancel_ext";
    std::filesystem::create_directories(package_root);
    createVirtualEnvironment(bundled_python, env_root);

    {
        std::ofstream out(package_root / "process_window_close_cancel_ext.py", std::ios::binary);
        ASSERT_TRUE(out.is_open());
        out << "import time\n"
            << "from afternight import ui\n\n"
            << "class ProcessWindowCloseCancelExtension(ui.ProcessWindow):\n"
            << "    def get_params(self):\n"
            << "        return [\n"
            << "            {'id': 'strength', 'type': 'double', 'label': 'Strength', 'default': 0.5, 'min': 0.0, "
               "'max': 1.0, 'step': 0.05},\n"
            << "        ]\n\n"
            << "    def execute(self, target, src_image, dst_image, params, progress, masks=None, weights=None, "
               "output_masks=None):\n"
            << "        self.settings.set('close_requested', bool(ui.close_process_window()))\n"
            << "        for _ in range(200):\n"
            << "            if progress.is_cancelled():\n"
            << "                self.settings.set('cancelled', True)\n"
            << "                return\n"
            << "            time.sleep(0.01)\n"
            << "        self.settings.set('completed_without_cancel', True)\n"
            << "        dst_image.copy_from(src_image)\n";
    }

    const auto source_image = makeRgbGradientImage(5, 4);
    ASSERT_TRUE(source_image);

    InMemorySettings settings_store;
    GlobalSettingsPersistence& settings = settings_store.repository();
    an::ui::extensions::ExtensionSdkService sdk_service(&settings);
    an::tests::processes::ViewHandle target = targetViewOf("process-window-close-cancel", source_image);
    ASSERT_NE(target, nullptr);

    an::tests::processes::framework::CaseLaunchCoordinator coordinator;
    coordinator.configureSdkService(&settings);

    int presenter_count = 0;
    coordinator.setManagedImageHooks([&](ImageViewControl*, an::consumer::ImageAsset&&, const std::string&,
                                         const std::string&) { ++presenter_count; });

    an::ui::extensions::ExtensionHostLaunchBinding binding;
    binding.descriptor_id = "filters.process_window_close_cancel";
    binding.extension_id = "an.demo.process_window_close_cancel." + an::ui::extensions::generateOpaqueToken();
    binding.display_name = "ProcessWindow Close Cancel";
    binding.host_executable = host_path;
    binding.capabilities = {"execute"};
    binding.host_arguments = {"--extension-package-root",
                              package_root.string(),
                              "--entry-point",
                              "process_window_close_cancel_ext",
                              "--process-class",
                              "ProcessWindowCloseCancelExtension",
                              "--environment-root",
                              env_root.string(),
                              "--runtime-root",
                              runtime_root.string(),
                              "--stay-alive-ms",
                              std::to_string(3000 * kIpcTimingMultiplier)};

    static_cast<void>(sdk_service.removeExtensionSetting(binding.extension_id, "close_requested", nullptr));
    static_cast<void>(sdk_service.removeExtensionSetting(binding.extension_id, "cancelled", nullptr));
    static_cast<void>(sdk_service.removeExtensionSetting(binding.extension_id, "completed_without_cancel", nullptr));

    std::string error;
    ASSERT_TRUE(coordinator.launchBoundDescriptor(binding, target.get(), &error)) << error;
    ASSERT_TRUE(pumpCoordinatorUntilNativeSessionReady(coordinator, binding.descriptor_id, 750 * kIpcTimingMultiplier));

    an::ui::extensions::ExtensionHostExecutionDispatchResult dispatch_result;
    ASSERT_TRUE(coordinator.submitExecuteForDescriptor(binding.descriptor_id, target.get(), &dispatch_result, &error))
        << error;
    EXPECT_EQ(dispatch_result.state, an::ui::extensions::ExtensionHostExecutionDispatchState::Started);

    ASSERT_TRUE(pumpCoordinatorUntil(coordinator, 3000 * kIpcTimingMultiplier, [&]() {
        QCborValue cancelled_value;
        QCborValue close_requested_value;
        return coordinator.activeSessionCount() == 0u &&
               sdk_service.selectExtensionSetting(binding.extension_id, "close_requested", &close_requested_value,
                                                  QCborValue(), nullptr) &&
               close_requested_value.toBool() &&
               sdk_service.selectExtensionSetting(binding.extension_id, "cancelled", &cancelled_value, QCborValue(),
                                                  nullptr) &&
               cancelled_value.toBool();
    })) << error;

    EXPECT_EQ(presenter_count, 0);

    QCborValue completed_without_cancel;
    ASSERT_TRUE(sdk_service.selectExtensionSetting(binding.extension_id, "completed_without_cancel",
                                                   &completed_without_cancel, QCborValue(), nullptr));
    EXPECT_TRUE(completed_without_cancel.isUndefined());

    removeAllNoThrow(temp_root);
}

// A native window previews through the extension's execute_preview: its first frame, settled, lends the extension the
// image at its own size through the shared buffers and shows what it wrote; an Apply then runs the extension's execute
// over the image itself, which the application is handed.
TEST(ExtensionHostManagerTest, ANativeWindowPreviewsThroughTheExtensionAndAppliesOverTheImage)
{
    const std::filesystem::path host_path = extensionHostPathForCurrentAppDir();
    const std::filesystem::path runtime_root = runtimeRootForCurrentAppDir();
    const std::filesystem::path bundled_python = repositoryBundledPythonPath();
    AN_REQUIRE_FULL_EXTENSION_HOST(host_path);
    ASSERT_TRUE(std::filesystem::exists(runtime_root)) << runtime_root;
    ASSERT_FALSE(bundled_python.empty()) << "Repository bundled Python runtime must be available";

    const QString platform = QGuiApplication::platformName().trimmed().toLower();
    if (platform == QStringLiteral("offscreen") || platform == QStringLiteral("minimal"))
    {
        GTEST_SKIP() << "Native process windows are not created on headless Qt platforms";
    }

    const auto temp_root = uniqueExtensionRuntimeRootForTest("native-window-frames");
    removeAllNoThrow(temp_root);
    const RemovePathOnExit remove_temp_root{temp_root};
    std::filesystem::create_directories(temp_root);

    const auto env_root = temp_root / "env";
    const auto package_root = temp_root / "native_window_frames_ext";
    std::filesystem::create_directories(package_root);
    createVirtualEnvironment(bundled_python, env_root);

    {
        std::ofstream out(package_root / "native_window_frames_ext.py", std::ios::binary);
        ASSERT_TRUE(out.is_open());
        out << "import afternight\n"
            << "from afternight import ui\n\n"
            << "class NativeWindowFramesExtension(ui.RTPreviewProcess):\n"
            << "    def get_params(self):\n"
            << "        return [\n"
            << "            {'id': 'strength', 'type': 'double', 'label': 'Strength', 'default': 0.5, 'min': 0.0, "
               "'max': 1.0, 'step': 0.05},\n"
            << "        ]\n\n"
            << "    def execute_preview(self, target, src_image, preview_image, params, progress, masks=None, "
               "weights=None):\n"
            << "        source = src_image.properties\n"
            << "        output = preview_image.properties\n"
            << "        self.settings.set('preview_source', [int(source['width']), int(source['height'])])\n"
            << "        self.settings.set('preview_output', [int(output['width']), int(output['height'])])\n"
            << "        afternight.core.apply_linear_transform(src_image, preview_image, 1.0, 0.25)\n\n"
            << "    def execute(self, target, src_image, dst_image, params, progress, masks=None, weights=None, "
               "output_masks=None):\n"
            << "        source = src_image.properties\n"
            << "        self.settings.set('execute_source', [int(source['width']), int(source['height'])])\n"
            << "        afternight.core.apply_linear_transform(src_image, dst_image, 1.0, 0.25)\n";
    }

    constexpr std::uint32_t kWidth = 64u;
    constexpr std::uint32_t kHeight = 48u;
    const auto source_image = makeRgbGradientImage(kWidth, kHeight);
    ASSERT_TRUE(source_image);

    InMemorySettings settings_store;
    GlobalSettingsPersistence& settings = settings_store.repository();
    an::ui::extensions::ExtensionSdkService sdk_service(&settings);
    const std::string view_name = "native-window-frames";
    an::tests::processes::ViewHandle target = targetViewOf(view_name, source_image);
    ASSERT_NE(target, nullptr);

    // The application describes the view the window is launched over, as it does for every session: the host mirrors
    // its image, which the window previews and applies to.
    std::string previewed_session;
    an::tests::processes::framework::CaseLaunchCoordinator coordinator;
    coordinator.configureSdkService(
        &settings,
        [&]() -> std::vector<an::ui::extensions::ExtensionViewState> {
            an::ui::extensions::ExtensionViewState view_state;
            view_state.view_name = view_name;
            view_state.view_mode = "standard";
            view_state.channel_view_mode = "rgb";
            view_state.is_active = true;
            view_state.has_image = true;
            view_state.width = static_cast<int>(kWidth);
            view_state.height = static_cast<int>(kHeight);
            view_state.channels = 3;
            return {view_state};
        },
        [&](const std::string_view session_id, const std::string_view request_id, const std::string_view named,
            an::ui::extensions::ExtensionViewSnapshot* out_snapshot, std::string* error) -> bool {
            EXPECT_EQ(named, view_name);
            previewed_session = std::string(session_id);
            *out_snapshot = an::ui::extensions::ExtensionViewSnapshot{};
            an::ui::extensions::SharedBufferManager shared_buffer_manager;
            an::ui::extensions::SharedImageBuffer buffer;
            if (!exportImageOrReport(shared_buffer_manager, *source_image, std::string(session_id),
                                     an::ui::extensions::SharedBufferRole::Preview, &buffer, error,
                                     std::string(request_id) + ".native_window_frames_snapshot",
                                     std::string(named) + ".snapshot",
                                     an::ui::extensions::SharedBufferAccessMode::ReadOnly))
            {
                return false;
            }
            out_snapshot->source_buffer = buffer.descriptor();
            return true;
        },
        []() { return an::ui::extensions::ExtensionThemeSnapshot{}; });
    coordinator.setManagedViewNameProvider([&view_name](ImageViewControl*) { return view_name; });

    std::optional<an::consumer::ImageAsset> applied;
    coordinator.setManagedImageHooks([&](ImageViewControl*, an::consumer::ImageAsset&& image, const std::string&,
                                         const std::string&) { applied.emplace(std::move(image)); });

    an::ui::extensions::ExtensionHostLaunchBinding binding;
    binding.descriptor_id = "filters.native_window_frames";
    binding.extension_id = "an.demo.native_window_frames." + an::ui::extensions::generateOpaqueToken();
    binding.display_name = "Native Window Frames";
    binding.host_executable = host_path;
    binding.capabilities = {"execute", "preview"};
    binding.process_capabilities.preview = true;
    binding.process_capabilities.preview_scaling = an::ui::extensions::ExtensionPreviewScaling::Decimated;
    binding.host_arguments = {
        "--extension-package-root", package_root.string(), "--entry-point",
        "native_window_frames_ext", "--process-class",     "NativeWindowFramesExtension",
        "--environment-root",       env_root.string(),     "--runtime-root",
        runtime_root.string(),      "--stay-alive-ms",     std::to_string(8000 * kIpcTimingMultiplier)};

    const auto setting = [&](const char* key) {
        QCborValue value;
        return sdk_service.selectExtensionSetting(binding.extension_id, key, &value, QCborValue(), nullptr)
                   ? value
                   : QCborValue();
    };
    const auto size_of = [](const QCborValue& value) {
        const QCborArray pair = value.toArray();
        return pair.size() == 2
                   ? QSize(static_cast<int>(pair.at(0).toInteger()), static_cast<int>(pair.at(1).toInteger()))
                   : QSize();
    };

    std::string error;
    an::ui::extensions::ExtensionHostLaunchOptions options;
    options.initial_view_name = view_name;
    ASSERT_TRUE(coordinator.launchBoundDescriptor(binding, target.get(), options, &error)) << error;
    ASSERT_TRUE(pumpCoordinatorUntilNativeSessionReady(coordinator, binding.descriptor_id, 750 * kIpcTimingMultiplier));

    // The first frame is settled: the extension is lent the image whole, and writes an output of its size.
    ASSERT_TRUE(pumpCoordinatorUntil(coordinator, 5000 * kIpcTimingMultiplier, [&]() {
        return setting("preview_output").isArray();
    })) << "the native window's first frame never reached the extension";
    EXPECT_EQ(size_of(setting("preview_source")), QSize(kWidth, kHeight));
    EXPECT_EQ(size_of(setting("preview_output")), QSize(kWidth, kHeight));

    an::ui::extensions::ExtensionHostExecutionDispatchResult dispatch_result;
    ASSERT_TRUE(coordinator.submitExecuteForDescriptor(binding.descriptor_id, target.get(), &dispatch_result, &error))
        << error;
    ASSERT_TRUE(pumpCoordinatorUntil(coordinator, 5000 * kIpcTimingMultiplier, [&]() { return applied.has_value(); }))
        << "the Apply's result never came back";
    EXPECT_EQ(size_of(setting("execute_source")), QSize(kWidth, kHeight)) << "the Apply ran over the image itself";
    const std::optional<an::ImageDescriptor> described = applied->descriptor();
    ASSERT_TRUE(described.has_value());
    EXPECT_EQ(described->extent().width(), kWidth);
    EXPECT_EQ(described->extent().height(), kHeight);
    for (const auto& [x, y] : {std::pair{0u, 0u}, std::pair{31u, 17u}, std::pair{63u, 47u}})
    {
        EXPECT_NEAR(sampleAt(*applied, x, y, 0u), sampleAt(*source_image, x, y, 0u) + 0.25, 1e-5) << x << "," << y;
    }

    ASSERT_TRUE(stopCoordinatorAndWaitForSessionAbsence(coordinator, 3000 * kIpcTimingMultiplier));
    // Nothing the window's frames were lent is left once its session has ended.
    ASSERT_FALSE(previewed_session.empty());
    EXPECT_TRUE(pathMissingOrEmpty(an::ui::extensions::extensionHostPreviewFramesRootPath(previewed_session)));
}

// What each frame of a real host's native window costs on its way to the extension and back, read from the host's own
// trace: run only when AN_BENCH_HOST_FRAMES names how many frames to trace. The window's image is replaced that many
// times - a new gradient of AN_BENCH_HOST_SIZE, `<width>x<height>`, 640x480 by default, small enough that what every
// frame pays whatever its size dominates - and each replacement reaches the host as the application sends it, which
// mirrors it and asks for a settled frame of it. The mean of each span over the replacements' frames is printed and
// recorded as the case's properties.
TEST(ExtensionHostManagerTest, TheFramesOfANativeWindowInARealHostAreTraced)
{
    bool counted = false;
    const int frames_asked = qEnvironmentVariable("AN_BENCH_HOST_FRAMES").toInt(&counted);
    if (!counted || frames_asked <= 0)
    {
        GTEST_SKIP() << "set AN_BENCH_HOST_FRAMES to a number of frames to trace a real host's native window";
    }
    std::uint32_t width = 640u;
    std::uint32_t height = 480u;
    if (const QString named = qEnvironmentVariable("AN_BENCH_HOST_SIZE"); !named.isEmpty())
    {
        const QStringList size = named.split(QLatin1Char('x'));
        ASSERT_EQ(size.size(), 2) << named.toStdString();
        width = size[0].toUInt();
        height = size[1].toUInt();
        ASSERT_GE(width, 2u);
        ASSERT_GE(height, 2u);
    }

    const std::filesystem::path host_path = extensionHostPathForCurrentAppDir();
    const std::filesystem::path runtime_root = runtimeRootForCurrentAppDir();
    const std::filesystem::path bundled_python = repositoryBundledPythonPath();
    AN_REQUIRE_FULL_EXTENSION_HOST(host_path);
    ASSERT_TRUE(std::filesystem::exists(runtime_root)) << runtime_root;
    ASSERT_FALSE(bundled_python.empty()) << "Repository bundled Python runtime must be available";

    const QString platform = QGuiApplication::platformName().trimmed().toLower();
    if (platform == QStringLiteral("offscreen") || platform == QStringLiteral("minimal"))
    {
        GTEST_SKIP() << "Native process windows are not created on headless Qt platforms";
    }

    const auto temp_root = uniqueExtensionRuntimeRootForTest("native-window-trace");
    removeAllNoThrow(temp_root);
    const RemovePathOnExit remove_temp_root{temp_root};
    std::filesystem::create_directories(temp_root);
    const auto env_root = temp_root / "env";
    const auto package_root = temp_root / "native_window_trace_ext";
    std::filesystem::create_directories(package_root);
    createVirtualEnvironment(bundled_python, env_root);
    {
        std::ofstream out(package_root / "native_window_trace_ext.py", std::ios::binary);
        ASSERT_TRUE(out.is_open());
        out << "import afternight\n"
            << "from afternight import ui\n\n"
            << "class NativeWindowTraceExtension(ui.RTPreviewProcess):\n"
            << "    def get_params(self):\n"
            << "        return [\n"
            << "            {'id': 'strength', 'type': 'double', 'label': 'Strength', 'default': 0.5, 'min': 0.0, "
               "'max': 1.0, 'step': 0.05},\n"
            << "        ]\n\n"
            << "    def execute_preview(self, target, src_image, preview_image, params, progress, masks=None, "
               "weights=None):\n"
            << "        afternight.core.apply_linear_transform(src_image, preview_image, 1.0, 0.25)\n"
            << "        self._frames = getattr(self, '_frames', 0) + 1\n"
            << "        self.settings.set('preview_frames', self._frames)\n\n"
            << "    def execute(self, target, src_image, dst_image, params, progress, masks=None, weights=None, "
               "output_masks=None):\n"
            << "        afternight.core.apply_linear_transform(src_image, dst_image, 1.0, 0.25)\n";
    }

    // The host reads where to trace from the environment it is started with.
    const std::filesystem::path trace_path = temp_root / "frames.csv";
    const ScopedEnvironmentVariable trace_variable("AFTERNIGHT_PREVIEW_FRAME_TRACE",
                                                   QFile::encodeName(QString::fromStdWString(trace_path.wstring())));

    std::shared_ptr<an::consumer::ImageAsset> current_image = makeRgbGradientImage(width, height);
    ASSERT_TRUE(current_image);
    InMemorySettings settings_store;
    GlobalSettingsPersistence& settings = settings_store.repository();
    an::ui::extensions::ExtensionSdkService sdk_service(&settings);
    const std::string view_name = "native-window-trace";
    an::tests::processes::ViewHandle target = targetViewOf(view_name, current_image);
    ASSERT_NE(target, nullptr);

    std::string traced_session;
    an::tests::processes::framework::CaseLaunchCoordinator coordinator;
    coordinator.configureSdkService(
        &settings,
        [&]() -> std::vector<an::ui::extensions::ExtensionViewState> {
            an::ui::extensions::ExtensionViewState view_state;
            view_state.view_name = view_name;
            view_state.view_mode = "standard";
            view_state.channel_view_mode = "rgb";
            view_state.is_active = true;
            view_state.has_image = true;
            view_state.width = static_cast<int>(width);
            view_state.height = static_cast<int>(height);
            view_state.channels = 3;
            return {view_state};
        },
        [&](const std::string_view session_id, const std::string_view request_id, const std::string_view named,
            an::ui::extensions::ExtensionViewSnapshot* out_snapshot, std::string* error) -> bool {
            traced_session = std::string(session_id);
            *out_snapshot = an::ui::extensions::ExtensionViewSnapshot{};
            an::ui::extensions::SharedBufferManager shared_buffer_manager;
            an::ui::extensions::SharedImageBuffer buffer;
            if (!exportImageOrReport(shared_buffer_manager, *current_image, std::string(session_id),
                                     an::ui::extensions::SharedBufferRole::Preview, &buffer, error,
                                     std::string(request_id) + ".native_window_trace_snapshot",
                                     std::string(named) + ".snapshot",
                                     an::ui::extensions::SharedBufferAccessMode::ReadOnly))
            {
                return false;
            }
            out_snapshot->source_buffer = buffer.descriptor();
            return true;
        },
        []() { return an::ui::extensions::ExtensionThemeSnapshot{}; });
    coordinator.setManagedViewNameProvider([&view_name](ImageViewControl*) { return view_name; });

    an::ui::extensions::ExtensionHostLaunchBinding binding;
    binding.descriptor_id = "filters.native_window_trace";
    binding.extension_id = "an.demo.native_window_trace." + an::ui::extensions::generateOpaqueToken();
    binding.display_name = "Native Window Trace";
    binding.host_executable = host_path;
    binding.capabilities = {"execute", "preview"};
    binding.process_capabilities.preview = true;
    binding.host_arguments = {
        "--extension-package-root", package_root.string(), "--entry-point",
        "native_window_trace_ext",  "--process-class",     "NativeWindowTraceExtension",
        "--environment-root",       env_root.string(),     "--runtime-root",
        runtime_root.string(),      "--stay-alive-ms",     std::to_string(60000 * kIpcTimingMultiplier)};

    const auto frames_seen = [&]() {
        QCborValue value;
        return sdk_service.selectExtensionSetting(binding.extension_id, "preview_frames", &value, QCborValue(), nullptr)
                   ? static_cast<int>(value.toInteger())
                   : 0;
    };
    const auto trace_lines = [&trace_path]() {
        QFile file(QString::fromStdWString(trace_path.wstring()));
        if (!file.open(QIODevice::ReadOnly))
        {
            return QList<QByteArray>();
        }
        QList<QByteArray> lines = file.readAll().split('\n');
        lines.removeAll(QByteArray());
        return lines;
    };

    std::string error;
    an::ui::extensions::ExtensionHostLaunchOptions options;
    options.initial_view_name = view_name;
    ASSERT_TRUE(coordinator.launchBoundDescriptor(binding, target.get(), options, &error)) << error;
    ASSERT_TRUE(pumpCoordinatorUntilNativeSessionReady(coordinator, binding.descriptor_id, 750 * kIpcTimingMultiplier));
    ASSERT_TRUE(pumpCoordinatorUntil(coordinator, 10000 * kIpcTimingMultiplier, [&]() { return frames_seen() >= 1; }))
        << "the native window's first frame never reached the extension";

    for (int replacement = 1; replacement <= frames_asked; ++replacement)
    {
        const int before = frames_seen();
        current_image = makeRgbGradientImage(width, height);
        ASSERT_TRUE(current_image);
        coordinator.handleViewControlUpdated(target.get());
        ASSERT_TRUE(
            pumpCoordinatorUntil(coordinator, 10000 * kIpcTimingMultiplier, [&]() { return frames_seen() > before; }))
            << "replacement " << replacement << " was never previewed";
    }
    // A frame's line is written once its buffers are let go, after the extension counted it.
    const int counted_frames = frames_seen();
    ASSERT_TRUE(pumpCoordinatorUntil(coordinator, 5000 * kIpcTimingMultiplier, [&]() {
        return trace_lines().size() >= counted_frames + 1;
    })) << "the trace holds fewer frames than the extension ran";
    ASSERT_TRUE(stopCoordinatorAndWaitForSessionAbsence(coordinator, 3000 * kIpcTimingMultiplier));
    ASSERT_FALSE(traced_session.empty());
    EXPECT_TRUE(pathMissingOrEmpty(an::ui::extensions::extensionHostPreviewFramesRootPath(traced_session)))
        << "a file the window's frames were lent was left behind";

    const QList<QByteArray> lines = trace_lines();
    ASSERT_GE(lines.size(), 3);
    const QList<QByteArray> columns = lines.front().split(',');
    ASSERT_EQ(columns.size(), 14) << lines.front().toStdString();
    // The first frame pays what the window's first preview pays once; the replacements' frames are what is reported.
    std::vector<double> sums(columns.size(), 0.0);
    int traced = 0;
    for (qsizetype index = 2; index < lines.size(); ++index)
    {
        const QList<QByteArray> fields = lines[index].split(',');
        ASSERT_EQ(fields.size(), columns.size()) << lines[index].toStdString();
        EXPECT_EQ(fields[0].toInt(), static_cast<int>(width));
        EXPECT_EQ(fields[1].toInt(), static_cast<int>(height));
        for (qsizetype column = 5; column < columns.size(); ++column)
        {
            sums[static_cast<std::size_t>(column)] += fields[column].toDouble();
        }
        ++traced;
    }
    ASSERT_GT(traced, 0);
    std::printf("[ host trace ] %d frames of %ux%u RGB Float32 traced through a real host\n", traced, width, height);
    ::testing::Test::RecordProperty("frames_traced", std::to_string(traced));
    double seam_ms = 0.0;
    for (qsizetype column = 5; column < columns.size(); ++column)
    {
        const double mean_ms = sums[static_cast<std::size_t>(column)] / static_cast<double>(traced) / 1000.0;
        std::string name = columns[column].toStdString();
        name = name.substr(0, name.size() - 3) + "_mean_ms";
        std::printf("  %-24s %.3f\n", name.c_str(), mean_ms);
        ::testing::Test::RecordProperty(name, std::to_string(mean_ms));
        if (columns[column] != QByteArrayLiteral("run_us"))
        {
            seam_ms += mean_ms;
        }
    }
    std::printf("  %-24s %.3f\n", "seam_mean_ms", seam_ms);
    ::testing::Test::RecordProperty("seam_mean_ms", std::to_string(seam_ms));
    std::fflush(stdout);
}

// A preview sub-area exists only for an enabled selection inside an image.
TEST(ExtensionHostManagerTest, NormalizeManagedPreviewSubAreaRequiresEnabledSelection)
{
    EXPECT_FALSE(
        an::ui::extensions::normalizeManagedPreviewSubArea(false, QSize(120, 80), QRect(10, 12, 30, 20)).has_value());
    EXPECT_FALSE(an::ui::extensions::normalizeManagedPreviewSubArea(true, QSize(), QRect(10, 12, 30, 20)).has_value());
    EXPECT_FALSE(
        an::ui::extensions::normalizeManagedPreviewSubArea(true, QSize(120, 80), QRect(200, 12, 30, 20)).has_value());
}

// A preview sub-area is the selection clipped to the image.
TEST(ExtensionHostManagerTest, NormalizeManagedPreviewSubAreaClipsSelectionToImageBounds)
{
    const auto preview_sub_area =
        an::ui::extensions::normalizeManagedPreviewSubArea(true, QSize(120, 80), QRect(-5, 10, 30, 100));
    ASSERT_TRUE(preview_sub_area.has_value());
    EXPECT_EQ(*preview_sub_area, QRect(0, 10, 25, 70));
}

// A managed execute that completes with no view commit ends the session and presents nothing.
TEST(ExtensionHostManagerTest, LaunchCoordinatorRejectsManagedExecuteCompletionWithoutViewCommit)
{
    const std::filesystem::path host_path = extensionHostPathForCurrentAppDir();
    AN_REQUIRE_FULL_EXTENSION_HOST(host_path);

    const auto source_image = makeRgbGradientImage(5, 4);
    an::tests::processes::ViewHandle target = targetViewOf("managed-execute-without-commit", source_image);
    ASSERT_NE(target, nullptr);

    an::tests::processes::framework::CaseLaunchCoordinator coordinator;

    int presenter_count = 0;
    coordinator.setManagedImageHooks([&](ImageViewControl* control, an::consumer::ImageAsset&& image,
                                         const std::string& history_step_name,
                                         const std::string& history_step_description) {
        ++presenter_count;
        EXPECT_EQ(control, target.get());
        EXPECT_TRUE(image.descriptor().has_value());
        EXPECT_FALSE(history_step_name.empty());
        EXPECT_FALSE(history_step_description.empty());
    });

    an::ui::extensions::ExtensionHostLaunchBinding binding;
    binding.descriptor_id = "filters.demo";
    binding.extension_id = "an.demo";
    binding.display_name = "Demo";
    binding.host_executable = host_path;
    binding.capabilities = {"execute"};
    binding.host_arguments = {"--result-buffer-role", "mask",
                              "--execute-delay-ms",   "40",
                              "--stay-alive-ms",      std::to_string(1000 * kIpcTimingMultiplier)};

    std::string error;
    ASSERT_TRUE(coordinator.launchBoundDescriptor(binding, target.get(), &error)) << error;
    ASSERT_TRUE(pumpCoordinatorUntilNativeSessionReady(coordinator, binding.descriptor_id, 500 * kIpcTimingMultiplier));

    an::ui::extensions::ExtensionHostExecutionDispatchResult dispatch_result;
    ASSERT_TRUE(coordinator.submitExecuteForDescriptor(binding.descriptor_id, target.get(), &dispatch_result, &error))
        << error;
    EXPECT_EQ(dispatch_result.state, an::ui::extensions::ExtensionHostExecutionDispatchState::Started);

    ASSERT_TRUE(pumpCoordinatorUntilSessionCount(coordinator, 0u, 1500 * kIpcTimingMultiplier));
    EXPECT_EQ(presenter_count, 0);

    ASSERT_TRUE(stopCoordinatorAndWaitForSessionAbsence(coordinator, 500 * kIpcTimingMultiplier));
}

// Inspection bootstraps the extension's own environment and imports its entry point there.
TEST(ExtensionHostManagerTest, HostInspectBootstrapsIsolatedVenvAndImportsExtensionEntrypoint)
{
    const std::filesystem::path host_path = extensionHostPathForCurrentAppDir();
    const std::filesystem::path runtime_root = runtimeRootForCurrentAppDir();
    const std::filesystem::path bundled_python = repositoryBundledPythonPath();
    AN_REQUIRE_FULL_EXTENSION_HOST(host_path);
    ASSERT_TRUE(std::filesystem::exists(runtime_root)) << runtime_root;
    ASSERT_FALSE(bundled_python.empty()) << "Repository bundled Python runtime must be available for inspect coverage";

    const auto temp_root = uniqueExtensionRuntimeRootForTest("inspect");
    removeAllNoThrow(temp_root);
    const RemovePathOnExit remove_temp_root{temp_root};
    std::filesystem::create_directories(temp_root);

    const auto env_root = temp_root / "env";
    const auto package_root = temp_root / "inspect_ext";
    std::filesystem::create_directories(package_root);

    ASSERT_NO_FATAL_FAILURE(createVirtualEnvironment(bundled_python, env_root));

#ifdef _WIN32
    const auto site_packages = env_root / "Lib" / "site-packages";
#else
    const auto site_packages = env_root / "lib" / "python3.14" / "site-packages";
#endif
    std::filesystem::create_directories(site_packages / "demo_pkg");
    {
        std::ofstream out(site_packages / "demo_pkg" / "__init__.py", std::ios::binary);
        ASSERT_TRUE(out.is_open());
        out << "__version__ = '1.0.0'\n";
    }
    std::filesystem::create_directories(site_packages / "demo_pkg-1.0.0.dist-info");
    {
        std::ofstream out(site_packages / "demo_pkg-1.0.0.dist-info" / "METADATA", std::ios::binary);
        ASSERT_TRUE(out.is_open());
        out << "Metadata-Version: 2.1\nName: demo-pkg\nVersion: 1.0.0\n";
    }

    {
        std::ofstream out(package_root / "inspect_ext.py", std::ios::binary);
        ASSERT_TRUE(out.is_open());
        out << "import importlib.metadata\n"
            << "import pathlib\n"
            << "import sys\n"
            << "import afternight\n"
            << "import demo_pkg\n\n"
            << "EXPECTED_ENV = pathlib.Path(r'" << env_root.generic_string() << "')\n"
            << "EXPECTED_BASE = pathlib.Path(r'" << runtime_root.generic_string() << "')\n"
            << "EXPECTED_PACKAGE_ROOT = pathlib.Path(r'" << package_root.generic_string() << "')\n\n"
            << "class InspectExtension(afternight.ProcessExtension):\n"
            << "    def __init__(self, settings):\n"
            << "        super().__init__(settings)\n"
            << "        assert pathlib.Path(sys.prefix) == EXPECTED_ENV\n"
            << "        assert pathlib.Path(sys.exec_prefix) == EXPECTED_ENV\n"
            << "        assert pathlib.Path(sys.base_prefix) == EXPECTED_BASE\n"
            << "        assert pathlib.Path(sys.base_exec_prefix) == EXPECTED_BASE\n"
            << "        assert pathlib.Path(sys.path[0]) == EXPECTED_PACKAGE_ROOT\n"
            << "        assert demo_pkg.__version__ == '1.0.0'\n"
            << "        assert importlib.metadata.version('demo-pkg') == '1.0.0'\n\n"
            << "    def get_settings_params(self):\n"
            << "        return [{'id': 'tool_path', 'type': 'string', 'default': ''}]\n";
    }

    QProcess inspect;
    inspect.setProgram(QString::fromStdString(host_path.string()));
    inspect.setProcessChannelMode(QProcess::MergedChannels);
    inspect.setArguments({QStringLiteral("--inspect"), QStringLiteral("--inspect-settings"),
                          QStringLiteral("--sdk-backend"), QStringLiteral("runtime"),
                          QStringLiteral("--extension-package-root"), QString::fromStdString(package_root.string()),
                          QStringLiteral("--entry-point"), QStringLiteral("inspect_ext"),
                          QStringLiteral("--process-class"), QStringLiteral("InspectExtension"),
                          QStringLiteral("--environment-root"), QString::fromStdString(env_root.string()),
                          QStringLiteral("--runtime-root"), QString::fromStdString(runtime_root.string())});
    inspect.start();
    ASSERT_TRUE(inspect.waitForStarted());
    ASSERT_TRUE(inspect.waitForFinished(hostProcessWaitMs()));

    const std::string output = inspect.readAllStandardOutput().toStdString();
    EXPECT_EQ(inspect.exitStatus(), QProcess::NormalExit) << output;
    EXPECT_EQ(inspect.exitCode(), 0) << output;

    QJsonParseError parse_error;
    const QJsonDocument document = QJsonDocument::fromJson(QByteArray::fromStdString(output).trimmed(), &parse_error);
    ASSERT_EQ(parse_error.error, QJsonParseError::NoError) << output;
    ASSERT_TRUE(document.isObject()) << output;
    ASSERT_TRUE(document.object().value(QStringLiteral("ui_runtime")).isObject()) << output;
    ASSERT_TRUE(document.object().value(QStringLiteral("settings_schema")).isArray()) << output;

    const QJsonObject ui_runtime = document.object().value(QStringLiteral("ui_runtime")).toObject();
    EXPECT_EQ(ui_runtime.value(QStringLiteral("application_class")).toString(), QStringLiteral("QApplication"));
    EXPECT_TRUE(ui_runtime.value(QStringLiteral("has_app_context")).toBool()) << output;
    EXPECT_TRUE(ui_runtime.value(QStringLiteral("has_proxy_style")).toBool()) << output;
    EXPECT_EQ(ui_runtime.value(QStringLiteral("style_class")).toString(), QStringLiteral("AnProxyStyle"));
    EXPECT_FALSE(ui_runtime.value(QStringLiteral("temp_dir")).toString().isEmpty()) << output;
    EXPECT_FALSE(ui_runtime.value(QStringLiteral("artifacts_dir")).toString().isEmpty()) << output;

    const QJsonArray schema = document.object().value(QStringLiteral("settings_schema")).toArray();
    ASSERT_EQ(schema.size(), 1);
    EXPECT_EQ(schema.at(0).toObject().value(QStringLiteral("id")).toString(), QStringLiteral("tool_path"));
    EXPECT_EQ(schema.at(0).toObject().value(QStringLiteral("type")).toString(), QStringLiteral("string"));

    removeAllNoThrow(temp_root);
}

// An extension's environment does not see the bundled runtime's third-party packages.
TEST(ExtensionHostManagerTest, HostInspectDoesNotInheritBundledRuntimeThirdPartyPackages)
{
    const std::filesystem::path host_path = extensionHostPathForCurrentAppDir();
    const std::filesystem::path runtime_root = runtimeRootForCurrentAppDir();
    const std::filesystem::path bundled_python = repositoryBundledPythonPath();
    AN_REQUIRE_FULL_EXTENSION_HOST(host_path);
    ASSERT_TRUE(std::filesystem::exists(runtime_root)) << runtime_root;
    ASSERT_FALSE(bundled_python.empty()) << "Repository bundled Python runtime must be available for inspect coverage";

    const auto temp_root = uniqueExtensionRuntimeRootForTest("inspect-no-inherit");
    removeAllNoThrow(temp_root);
    const RemovePathOnExit remove_temp_root{temp_root};
    std::filesystem::create_directories(temp_root);

    const auto env_root = temp_root / "env";
    const auto package_root = temp_root / "inspect_no_inherit_ext";
    std::filesystem::create_directories(package_root);

    ASSERT_NO_FATAL_FAILURE(createVirtualEnvironment(bundled_python, env_root));

    const auto runtime_site_packages = runtimeSitePackagesPath(runtime_root);
    const auto bundled_only_package = runtime_site_packages / "bundled_base_runtime_only_pkg";
    const auto bundled_only_dist_info = runtime_site_packages / "bundled_base_runtime_only_pkg-9.9.9.dist-info";
    RemovePathOnExit package_cleanup{bundled_only_package};
    RemovePathOnExit dist_info_cleanup{bundled_only_dist_info};
    removeAllNoThrow(bundled_only_package);
    removeAllNoThrow(bundled_only_dist_info);

    std::filesystem::create_directories(bundled_only_package);
    {
        std::ofstream out(bundled_only_package / "__init__.py", std::ios::binary);
        ASSERT_TRUE(out.is_open());
        out << "__version__ = '9.9.9'\n";
    }
    std::filesystem::create_directories(bundled_only_dist_info);
    {
        std::ofstream out(bundled_only_dist_info / "METADATA", std::ios::binary);
        ASSERT_TRUE(out.is_open());
        out << "Metadata-Version: 2.1\nName: bundled-base-runtime-only-pkg\nVersion: 9.9.9\n";
    }

    {
        std::ofstream out(package_root / "inspect_no_inherit_ext.py", std::ios::binary);
        ASSERT_TRUE(out.is_open());
        out << "import importlib.metadata\n"
            << "import pathlib\n"
            << "import sys\n"
            << "import afternight\n\n"
            << "EXPECTED_BASE_SITE = pathlib.Path(r'" << runtime_site_packages.generic_string() << "').resolve()\n\n"
            << "class InspectNoInheritExtension(afternight.ProcessExtension):\n"
            << "    def __init__(self, settings):\n"
            << "        super().__init__(settings)\n"
            << "        normalized_path = {str(pathlib.Path(p).resolve()) for p in sys.path if p}\n"
            << "        assert str(EXPECTED_BASE_SITE) not in normalized_path\n"
            << "        try:\n"
            << "            import bundled_base_runtime_only_pkg  # noqa: F401\n"
            << "        except ModuleNotFoundError:\n"
            << "            pass\n"
            << "        else:\n"
            << "            raise AssertionError('Bundled base-runtime package leaked into the extension "
               "environment')\n"
            << "        try:\n"
            << "            importlib.metadata.version('bundled-base-runtime-only-pkg')\n"
            << "        except importlib.metadata.PackageNotFoundError:\n"
            << "            pass\n"
            << "        else:\n"
            << "            raise AssertionError('Bundled base-runtime distribution metadata leaked into the extension "
               "environment')\n\n"
            << "    def get_settings_params(self):\n"
            << "        return []\n";
    }

    QProcess inspect;
    inspect.setProgram(QString::fromStdString(host_path.string()));
    inspect.setProcessChannelMode(QProcess::MergedChannels);
    inspect.setArguments({QStringLiteral("--inspect"), QStringLiteral("--inspect-settings"),
                          QStringLiteral("--sdk-backend"), QStringLiteral("runtime"),
                          QStringLiteral("--extension-package-root"), QString::fromStdString(package_root.string()),
                          QStringLiteral("--entry-point"), QStringLiteral("inspect_no_inherit_ext"),
                          QStringLiteral("--process-class"), QStringLiteral("InspectNoInheritExtension"),
                          QStringLiteral("--environment-root"), QString::fromStdString(env_root.string()),
                          QStringLiteral("--runtime-root"), QString::fromStdString(runtime_root.string())});
    inspect.start();
    ASSERT_TRUE(inspect.waitForStarted());
    ASSERT_TRUE(inspect.waitForFinished(hostProcessWaitMs()));

    const std::string output = inspect.readAllStandardOutput().toStdString();
    EXPECT_EQ(inspect.exitStatus(), QProcess::NormalExit) << output;
    EXPECT_EQ(inspect.exitCode(), 0) << output;

    QJsonParseError parse_error;
    const QJsonDocument document = QJsonDocument::fromJson(QByteArray::fromStdString(output).trimmed(), &parse_error);
    ASSERT_EQ(parse_error.error, QJsonParseError::NoError) << output;
    ASSERT_TRUE(document.isObject()) << output;
    ASSERT_TRUE(document.object().value(QStringLiteral("settings_schema")).isArray()) << output;
    EXPECT_TRUE(document.object().value(QStringLiteral("settings_schema")).toArray().isEmpty()) << output;

    removeAllNoThrow(temp_root);
}

// Inspection reads a process-window extension's parameter schema.
TEST(ExtensionHostManagerTest, HostInspectKeepsParamsSchemaForProcessWindowCompatibilitySubclass)
{
    const std::filesystem::path host_path = extensionHostPathForCurrentAppDir();
    const std::filesystem::path runtime_root = runtimeRootForCurrentAppDir();
    const std::filesystem::path bundled_python = repositoryBundledPythonPath();
    AN_REQUIRE_FULL_EXTENSION_HOST(host_path);
    ASSERT_TRUE(std::filesystem::exists(runtime_root)) << runtime_root;
    ASSERT_FALSE(bundled_python.empty()) << "Repository bundled Python runtime must be available for inspect coverage";

    const auto temp_root = uniqueExtensionRuntimeRootForTest("inspect-processwindow-compat-params");
    removeAllNoThrow(temp_root);
    const RemovePathOnExit remove_temp_root{temp_root};
    std::filesystem::create_directories(temp_root);

    const auto env_root = temp_root / "env";
    const auto package_root = temp_root / "inspect_processwindow_compat_params_ext";
    std::filesystem::create_directories(package_root);
    createVirtualEnvironment(bundled_python, env_root);

    {
        std::ofstream out(package_root / "inspect_processwindow_compat_params_ext.py", std::ios::binary);
        ASSERT_TRUE(out.is_open());
        out << "from afternight import ui\n\n"
            << "class InspectProcessWindowCompatExtension(ui.ProcessWindow):\n"
            << "    def get_params(self):\n"
            << "        return [\n"
            << "            {'id': 'strength', 'type': 'double', 'label': 'Strength', 'default': 0.55, 'min': 0.0, "
               "'max': 1.0, 'step': 0.05},\n"
            << "            {'id': 'mode', 'type': 'choice', 'label': 'Mode', 'default': 'balanced', 'options': "
               "[('Balanced', 'balanced'), ('Gentle', 'gentle')]},\n"
            << "        ]\n\n"
            << "    def execute(self, target, src_image, dst_image, params, progress, masks=None, weights=None, "
               "output_masks=None):\n"
            << "        progress.set_text('Copying image')\n"
            << "        dst_image.copy_from(src_image)\n";
    }

    QProcess inspect;
    inspect.setProgram(QString::fromStdString(host_path.string()));
    inspect.setProcessChannelMode(QProcess::MergedChannels);
    inspect.setArguments({QStringLiteral("--inspect"), QStringLiteral("--inspect-params"),
                          QStringLiteral("--sdk-backend"), QStringLiteral("runtime"),
                          QStringLiteral("--extension-package-root"), QString::fromStdString(package_root.string()),
                          QStringLiteral("--entry-point"), QStringLiteral("inspect_processwindow_compat_params_ext"),
                          QStringLiteral("--process-class"), QStringLiteral("InspectProcessWindowCompatExtension"),
                          QStringLiteral("--environment-root"), QString::fromStdString(env_root.string()),
                          QStringLiteral("--runtime-root"), QString::fromStdString(runtime_root.string())});
    inspect.start();
    ASSERT_TRUE(inspect.waitForStarted());
    ASSERT_TRUE(inspect.waitForFinished(hostProcessWaitMs()));

    const std::string output = inspect.readAllStandardOutput().toStdString();
    EXPECT_EQ(inspect.exitStatus(), QProcess::NormalExit) << output;
    EXPECT_EQ(inspect.exitCode(), 0) << output;

    QJsonParseError parse_error;
    const QJsonDocument document = QJsonDocument::fromJson(QByteArray::fromStdString(output).trimmed(), &parse_error);
    ASSERT_EQ(parse_error.error, QJsonParseError::NoError) << output;
    ASSERT_TRUE(document.isObject()) << output;

    const QJsonArray schema = document.object().value(QStringLiteral("params_schema")).toArray();
    ASSERT_EQ(schema.size(), 2) << output;
    EXPECT_EQ(schema.at(0).toObject().value(QStringLiteral("id")).toString(), QStringLiteral("strength"));
    EXPECT_EQ(schema.at(0).toObject().value(QStringLiteral("type")).toString(), QStringLiteral("double"));
    EXPECT_EQ(schema.at(1).toObject().value(QStringLiteral("id")).toString(), QStringLiteral("mode"));
    EXPECT_EQ(schema.at(1).toObject().value(QStringLiteral("type")).toString(), QStringLiteral("choice"));
    EXPECT_FALSE(document.object().value(QStringLiteral("supports_ui_events")).toBool()) << output;
    EXPECT_FALSE(document.object().value(QStringLiteral("supports_ui_validation")).toBool()) << output;

    removeAllNoThrow(temp_root);
}

// Inspection reads the real-time preview demo's parameter schema.
TEST(ExtensionHostManagerTest, HostInspectBundledRtPreviewDemoKeepsParamsSchema)
{
    const std::filesystem::path host_path = extensionHostPathForCurrentAppDir();
    const std::filesystem::path runtime_root = runtimeRootForCurrentAppDir();
    const std::filesystem::path bundled_python = repositoryBundledPythonPath();
    const std::filesystem::path extensions_root = findRepositoryBundledExtensionsRoot();
    AN_REQUIRE_FULL_EXTENSION_HOST(host_path);
    ASSERT_TRUE(std::filesystem::exists(runtime_root)) << runtime_root;
    ASSERT_FALSE(bundled_python.empty()) << "Repository bundled Python runtime must be available for inspect coverage";
    ASSERT_FALSE(extensions_root.empty()) << "Repository bundled extension roots must be available";

    const auto package_root = extensions_root / "an_rt_preview_demo";
    ASSERT_TRUE(std::filesystem::exists(package_root / "extension.json")) << package_root;

    const auto temp_root = uniqueExtensionRuntimeRootForTest("inspect-bundled-rtpreview-compat");
    removeAllNoThrow(temp_root);
    const RemovePathOnExit remove_temp_root{temp_root};
    std::filesystem::create_directories(temp_root);

    const auto env_root = temp_root / "env";
    createVirtualEnvironment(bundled_python, env_root);

    QProcess inspect;
    inspect.setProgram(QString::fromStdString(host_path.string()));
    inspect.setProcessChannelMode(QProcess::MergedChannels);
    inspect.setArguments({QStringLiteral("--inspect"), QStringLiteral("--inspect-params"),
                          QStringLiteral("--sdk-backend"), QStringLiteral("runtime"),
                          QStringLiteral("--extension-package-root"), QString::fromStdString(package_root.string()),
                          QStringLiteral("--entry-point"), QStringLiteral("an_rt_preview_demo"),
                          QStringLiteral("--process-class"), QStringLiteral("RTPreviewDemoExtension"),
                          QStringLiteral("--environment-root"), QString::fromStdString(env_root.string()),
                          QStringLiteral("--runtime-root"), QString::fromStdString(runtime_root.string())});
    inspect.start();
    ASSERT_TRUE(inspect.waitForStarted());
    ASSERT_TRUE(inspect.waitForFinished(hostProcessWaitMs()));

    const std::string output = inspect.readAllStandardOutput().toStdString();
    EXPECT_EQ(inspect.exitStatus(), QProcess::NormalExit) << output;
    EXPECT_EQ(inspect.exitCode(), 0) << output;

    QJsonParseError parse_error;
    const QJsonDocument document = QJsonDocument::fromJson(QByteArray::fromStdString(output).trimmed(), &parse_error);
    ASSERT_EQ(parse_error.error, QJsonParseError::NoError) << output;
    ASSERT_TRUE(document.isObject()) << output;

    const QJsonArray schema = document.object().value(QStringLiteral("params_schema")).toArray();
    ASSERT_EQ(schema.size(), 7) << output;
    EXPECT_EQ(schema.at(0).toObject().value(QStringLiteral("id")).toString(), QStringLiteral("window_meta"));
    EXPECT_EQ(schema.at(0).toObject().value(QStringLiteral("type")).toString(), QStringLiteral("meta"));
    EXPECT_EQ(schema.at(1).toObject().value(QStringLiteral("id")).toString(), QStringLiteral("preview_banner"));
    EXPECT_EQ(schema.at(1).toObject().value(QStringLiteral("type")).toString(), QStringLiteral("section"));
    EXPECT_EQ(schema.at(2).toObject().value(QStringLiteral("id")).toString(), QStringLiteral("preview_summary"));
    EXPECT_EQ(schema.at(2).toObject().value(QStringLiteral("type")).toString(), QStringLiteral("info"));
    EXPECT_EQ(schema.at(3).toObject().value(QStringLiteral("id")).toString(), QStringLiteral("strength"));
    EXPECT_EQ(schema.at(4).toObject().value(QStringLiteral("id")).toString(), QStringLiteral("lift"));
    EXPECT_EQ(schema.at(5).toObject().value(QStringLiteral("id")).toString(), QStringLiteral("mode"));
    EXPECT_EQ(schema.at(6).toObject().value(QStringLiteral("id")).toString(), QStringLiteral("preserve_background"));
    EXPECT_TRUE(document.object().value(QStringLiteral("preview_capable")).toBool()) << output;
    EXPECT_FALSE(document.object().value(QStringLiteral("supports_ui_events")).toBool()) << output;
    EXPECT_FALSE(document.object().value(QStringLiteral("supports_ui_validation")).toBool()) << output;

    removeAllNoThrow(temp_root);
}

// Inspection reads the custom-interface demo's parameter schema.
TEST(ExtensionHostManagerTest, HostInspectBundledCustomUiDemoReportsParamSchema)
{
    const std::filesystem::path host_path = extensionHostPathForCurrentAppDir();
    const std::filesystem::path runtime_root = runtimeRootForCurrentAppDir();
    const std::filesystem::path bundled_python = repositoryBundledPythonPath();
    const std::filesystem::path extensions_root = findRepositoryBundledExtensionsRoot();
    AN_REQUIRE_FULL_EXTENSION_HOST(host_path);
    ASSERT_TRUE(std::filesystem::exists(runtime_root)) << runtime_root;
    ASSERT_FALSE(bundled_python.empty()) << "Repository bundled Python runtime must be available for inspect coverage";
    ASSERT_FALSE(extensions_root.empty()) << "Repository bundled extension roots must be available";

    const auto package_root = extensions_root / "an_custom_ui_demo";
    ASSERT_TRUE(std::filesystem::exists(package_root / "extension.json")) << package_root;

    const auto temp_root = uniqueExtensionRuntimeRootForTest("inspect-bundled-customui-compat");
    removeAllNoThrow(temp_root);
    const RemovePathOnExit remove_temp_root{temp_root};
    std::filesystem::create_directories(temp_root);

    const auto env_root = temp_root / "env";
    createVirtualEnvironment(bundled_python, env_root);

    QProcess inspect;
    inspect.setProgram(QString::fromStdString(host_path.string()));
    inspect.setProcessChannelMode(QProcess::MergedChannels);
    inspect.setArguments({QStringLiteral("--inspect"), QStringLiteral("--inspect-params"),
                          QStringLiteral("--sdk-backend"), QStringLiteral("runtime"),
                          QStringLiteral("--extension-package-root"), QString::fromStdString(package_root.string()),
                          QStringLiteral("--entry-point"), QStringLiteral("an_custom_ui_demo"),
                          QStringLiteral("--process-class"), QStringLiteral("CustomUiDemoExtension"),
                          QStringLiteral("--environment-root"), QString::fromStdString(env_root.string()),
                          QStringLiteral("--runtime-root"), QString::fromStdString(runtime_root.string())});
    inspect.start();
    ASSERT_TRUE(inspect.waitForStarted());
    ASSERT_TRUE(inspect.waitForFinished(hostProcessWaitMs()));

    const std::string output = inspect.readAllStandardOutput().toStdString();
    EXPECT_EQ(inspect.exitStatus(), QProcess::NormalExit) << output;
    EXPECT_EQ(inspect.exitCode(), 0) << output;

    QJsonParseError parse_error;
    const QJsonDocument document = QJsonDocument::fromJson(QByteArray::fromStdString(output).trimmed(), &parse_error);
    ASSERT_EQ(parse_error.error, QJsonParseError::NoError) << output;
    ASSERT_TRUE(document.isObject()) << output;

    const QJsonArray params_schema = document.object().value(QStringLiteral("params_schema")).toArray();
    ASSERT_EQ(params_schema.size(), 13) << output;
    EXPECT_EQ(params_schema.at(0).toObject().value(QStringLiteral("id")).toString(), QStringLiteral("window_meta"));
    EXPECT_EQ(params_schema.at(0).toObject().value(QStringLiteral("type")).toString(), QStringLiteral("meta"));
    EXPECT_EQ(params_schema.at(1).toObject().value(QStringLiteral("id")).toString(), QStringLiteral("panel_banner"));
    EXPECT_EQ(params_schema.at(1).toObject().value(QStringLiteral("type")).toString(), QStringLiteral("section"));
    EXPECT_EQ(params_schema.at(2).toObject().value(QStringLiteral("id")).toString(), QStringLiteral("panel_summary"));
    EXPECT_EQ(params_schema.at(2).toObject().value(QStringLiteral("type")).toString(), QStringLiteral("info"));
    EXPECT_EQ(params_schema.at(3).toObject().value(QStringLiteral("id")).toString(), QStringLiteral("strength"));
    EXPECT_EQ(params_schema.at(4).toObject().value(QStringLiteral("id")).toString(), QStringLiteral("iterations"));
    EXPECT_EQ(params_schema.at(5).toObject().value(QStringLiteral("id")).toString(), QStringLiteral("sigma"));
    EXPECT_EQ(params_schema.at(6).toObject().value(QStringLiteral("id")).toString(), QStringLiteral("use_mask"));
    EXPECT_EQ(params_schema.at(7).toObject().value(QStringLiteral("id")).toString(), QStringLiteral("mode"));
    EXPECT_EQ(params_schema.at(8).toObject().value(QStringLiteral("id")).toString(), QStringLiteral("output_section"));
    EXPECT_EQ(params_schema.at(8).toObject().value(QStringLiteral("type")).toString(), QStringLiteral("section"));
    EXPECT_EQ(params_schema.at(9).toObject().value(QStringLiteral("id")).toString(), QStringLiteral("output_hint"));
    EXPECT_EQ(params_schema.at(9).toObject().value(QStringLiteral("type")).toString(), QStringLiteral("info"));
    EXPECT_EQ(params_schema.at(10).toObject().value(QStringLiteral("id")).toString(), QStringLiteral("output_name"));
    EXPECT_EQ(params_schema.at(11).toObject().value(QStringLiteral("id")).toString(), QStringLiteral("export_dir"));
    EXPECT_EQ(params_schema.at(12).toObject().value(QStringLiteral("id")).toString(), QStringLiteral("quality_preset"));
    EXPECT_FALSE(document.object().value(QStringLiteral("preview_capable")).toBool()) << output;
    EXPECT_FALSE(document.object().value(QStringLiteral("supports_ui_events")).toBool()) << output;
    EXPECT_FALSE(document.object().value(QStringLiteral("supports_ui_validation")).toBool()) << output;

    removeAllNoThrow(temp_root);
}

// Inspection reads the standalone preview demo's parameter schema.
TEST(ExtensionHostManagerTest, HostInspectBundledStandalonePreviewDemoReportsParamSchema)
{
    const std::filesystem::path host_path = extensionHostPathForCurrentAppDir();
    const std::filesystem::path runtime_root = runtimeRootForCurrentAppDir();
    const std::filesystem::path bundled_python = repositoryBundledPythonPath();
    const std::filesystem::path extensions_root = findRepositoryBundledExtensionsRoot();
    AN_REQUIRE_FULL_EXTENSION_HOST(host_path);
    ASSERT_TRUE(std::filesystem::exists(runtime_root)) << runtime_root;
    ASSERT_FALSE(bundled_python.empty()) << "Repository bundled Python runtime must be available for inspect coverage";
    ASSERT_FALSE(extensions_root.empty()) << "Repository bundled extension roots must be available";

    const auto package_root = extensions_root / "an_standalone_preview_demo";
    ASSERT_TRUE(std::filesystem::exists(package_root / "extension.json")) << package_root;

    const auto temp_root = uniqueExtensionRuntimeRootForTest("inspect-bundled-standalone-preview-compat");
    removeAllNoThrow(temp_root);
    const RemovePathOnExit remove_temp_root{temp_root};
    std::filesystem::create_directories(temp_root);

    const auto env_root = temp_root / "env";
    createVirtualEnvironment(bundled_python, env_root);
    if (!provisionBundledRuntimePackageIntoEnvironment(runtime_root, env_root, "PySide6") ||
        !provisionBundledRuntimePackageIntoEnvironment(runtime_root, env_root, "pyside6") ||
        !provisionBundledRuntimePackageIntoEnvironment(runtime_root, env_root, "shiboken6"))
    {
        GTEST_SKIP() << "PySide6 runtime artifacts are not available in the bundled runtime";
    }

    QProcess inspect;
    inspect.setProgram(QString::fromStdString(host_path.string()));
    inspect.setProcessChannelMode(QProcess::MergedChannels);
    inspect.setArguments({QStringLiteral("--inspect"), QStringLiteral("--inspect-settings"),
                          QStringLiteral("--inspect-params"), QStringLiteral("--sdk-backend"),
                          QStringLiteral("runtime"), QStringLiteral("--extension-package-root"),
                          QString::fromStdString(package_root.string()), QStringLiteral("--entry-point"),
                          QStringLiteral("an_standalone_preview_demo"), QStringLiteral("--process-class"),
                          QStringLiteral("StandalonePreviewDemoExtension"), QStringLiteral("--environment-root"),
                          QString::fromStdString(env_root.string()), QStringLiteral("--runtime-root"),
                          QString::fromStdString(runtime_root.string())});
    inspect.start();
    ASSERT_TRUE(inspect.waitForStarted());
    ASSERT_TRUE(inspect.waitForFinished(hostProcessWaitMs()));

    const std::string output = inspect.readAllStandardOutput().toStdString();
    EXPECT_EQ(inspect.exitStatus(), QProcess::NormalExit) << output;
    EXPECT_EQ(inspect.exitCode(), 0) << output;

    QJsonParseError parse_error;
    const QJsonDocument document = QJsonDocument::fromJson(QByteArray::fromStdString(output).trimmed(), &parse_error);
    ASSERT_EQ(parse_error.error, QJsonParseError::NoError) << output;
    ASSERT_TRUE(document.isObject()) << output;

    const QJsonArray settings_schema = document.object().value(QStringLiteral("settings_schema")).toArray();
    ASSERT_EQ(settings_schema.size(), 1) << output;
    EXPECT_EQ(settings_schema.at(0).toObject().value(QStringLiteral("id")).toString(), QStringLiteral("window_title"));
    const QJsonArray params_schema = document.object().value(QStringLiteral("params_schema")).toArray();
    ASSERT_EQ(params_schema.size(), 3) << output;
    EXPECT_EQ(params_schema.at(0).toObject().value(QStringLiteral("id")).toString(), QStringLiteral("window_meta"));
    EXPECT_EQ(params_schema.at(0).toObject().value(QStringLiteral("type")).toString(), QStringLiteral("meta"));
    EXPECT_EQ(params_schema.at(1).toObject().value(QStringLiteral("id")).toString(),
              QStringLiteral("standalone_section"));
    EXPECT_EQ(params_schema.at(1).toObject().value(QStringLiteral("type")).toString(), QStringLiteral("section"));
    EXPECT_EQ(params_schema.at(2).toObject().value(QStringLiteral("id")).toString(),
              QStringLiteral("standalone_summary"));
    EXPECT_EQ(params_schema.at(2).toObject().value(QStringLiteral("type")).toString(), QStringLiteral("info"));
    EXPECT_FALSE(document.object().value(QStringLiteral("preview_capable")).toBool()) << output;
    EXPECT_FALSE(document.object().value(QStringLiteral("supports_ui_events")).toBool()) << output;
    EXPECT_FALSE(document.object().value(QStringLiteral("supports_ui_validation")).toBool()) << output;

    removeAllNoThrow(temp_root);
}

#ifndef _WIN32
// An extension's packaged native helper runs with the loader paths its package declares.
TEST(ExtensionHostManagerTest, HostInspectExecutesNativeHelperWithPackagedLoaderPaths)
{
    const std::filesystem::path host_path = extensionHostPathForCurrentAppDir();
    const std::filesystem::path runtime_root = runtimeRootForCurrentAppDir();
    const std::filesystem::path bundled_python = repositoryBundledPythonPath();
    AN_REQUIRE_FULL_EXTENSION_HOST(host_path);
    ASSERT_TRUE(std::filesystem::exists(runtime_root)) << runtime_root;
    ASSERT_FALSE(bundled_python.empty()) << "Repository bundled Python runtime must be available for inspect coverage";

    const auto temp_root = uniqueExtensionRuntimeRootForTest("inspect-native-helper");
    removeAllNoThrow(temp_root);
    const RemovePathOnExit remove_temp_root{temp_root};
    std::filesystem::create_directories(temp_root);

    const auto env_root = temp_root / "env";
    const auto package_root = temp_root / "inspect_native_helper_ext";
    std::filesystem::create_directories(package_root);
    createVirtualEnvironment(bundled_python, env_root);

    const auto helper_executable = buildNativeHelperExecutable(package_root);
    ASSERT_FALSE(helper_executable.empty()) << "Failed to build packaged native helper";
    ASSERT_TRUE(std::filesystem::exists(helper_executable)) << helper_executable;

    writeTextFile(package_root / "inspect_native_helper_ext.py",
                  "import pathlib\n"
                  "import subprocess\n"
                  "import afternight\n\n"
                  "PACKAGE_ROOT = pathlib.Path(__file__).resolve().parent\n"
                  "HELPER = PACKAGE_ROOT / 'bin' / 'helper_runner'\n\n"
                  "class InspectNativeHelperExtension(afternight.ProcessExtension):\n"
                  "    def get_settings_params(self):\n"
                  "        result = subprocess.run([str(HELPER)], check=True, capture_output=True, text=True)\n"
                  "        return [{'id': 'helper_value', 'type': 'string', 'default': result.stdout.strip()}]\n");

    QProcess inspect;
    inspect.setProgram(QString::fromStdString(host_path.string()));
    inspect.setProcessChannelMode(QProcess::MergedChannels);
    inspect.setArguments({QStringLiteral("--inspect"), QStringLiteral("--inspect-settings"),
                          QStringLiteral("--sdk-backend"), QStringLiteral("runtime"),
                          QStringLiteral("--extension-package-root"), QString::fromStdString(package_root.string()),
                          QStringLiteral("--entry-point"), QStringLiteral("inspect_native_helper_ext"),
                          QStringLiteral("--process-class"), QStringLiteral("InspectNativeHelperExtension"),
                          QStringLiteral("--environment-root"), QString::fromStdString(env_root.string()),
                          QStringLiteral("--runtime-root"), QString::fromStdString(runtime_root.string())});
    inspect.start();
    ASSERT_TRUE(inspect.waitForStarted());
    ASSERT_TRUE(inspect.waitForFinished(hostProcessWaitMs()));

    const std::string output = inspect.readAllStandardOutput().toStdString();
    EXPECT_EQ(inspect.exitStatus(), QProcess::NormalExit) << output;
    EXPECT_EQ(inspect.exitCode(), 0) << output;

    QJsonParseError parse_error;
    const QJsonDocument document = QJsonDocument::fromJson(QByteArray::fromStdString(output).trimmed(), &parse_error);
    ASSERT_EQ(parse_error.error, QJsonParseError::NoError) << output;
    ASSERT_TRUE(document.isObject()) << output;

    const QJsonArray schema = document.object().value(QStringLiteral("settings_schema")).toArray();
    ASSERT_EQ(schema.size(), 1);
    EXPECT_EQ(schema.at(0).toObject().value(QStringLiteral("id")).toString(), QStringLiteral("helper_value"));
    EXPECT_EQ(schema.at(0).toObject().value(QStringLiteral("default")).toString(), QStringLiteral("23"));

    removeAllNoThrow(temp_root);
}

// An extension imports a compiled wheel whose libraries its package declares.
TEST(ExtensionHostManagerTest, HostInspectImportsCompiledWheelWithPackagedLoaderPaths)
{
    const std::filesystem::path host_path = extensionHostPathForCurrentAppDir();
    const std::filesystem::path runtime_root = runtimeRootForCurrentAppDir();
    const std::filesystem::path bundled_python = repositoryBundledPythonPath();
    AN_REQUIRE_FULL_EXTENSION_HOST(host_path);
    ASSERT_TRUE(std::filesystem::exists(runtime_root)) << runtime_root;
    ASSERT_FALSE(bundled_python.empty()) << "Repository bundled Python runtime must be available for inspect coverage";

    const auto temp_root = uniqueExtensionRuntimeRootForTest("inspect-compiled-wheel");
    removeAllNoThrow(temp_root);
    const RemovePathOnExit remove_temp_root{temp_root};
    std::filesystem::create_directories(temp_root);

    const auto env_root = temp_root / "env";
    const auto package_root = temp_root / "inspect_compiled_wheel_ext";
    std::filesystem::create_directories(package_root);
    createVirtualEnvironment(bundled_python, env_root);

    const auto wheel_path = createCompiledNativeWheel(temp_root, "native_demo", "1.0.0");
    ASSERT_FALSE(wheel_path.empty()) << "Failed to build compiled native wheel";
    ASSERT_TRUE(std::filesystem::exists(wheel_path)) << wheel_path;

    const auto env_python = environmentPythonPath(env_root);
    std::string pip_output;
    ASSERT_TRUE(runProcessExpectSuccess(env_python,
                                        {QStringLiteral("-m"), QStringLiteral("pip"), QStringLiteral("install"),
                                         QStringLiteral("--no-deps"), QStringLiteral("--no-index"),
                                         QString::fromStdString(wheel_path.string())},
                                        {}, &pip_output))
        << pip_output;

    writeTextFile(
        package_root / "inspect_compiled_wheel_ext.py",
        "import afternight\n"
        "import native_demo\n\n"
        "class InspectCompiledWheelExtension(afternight.ProcessExtension):\n"
        "    def get_settings_params(self):\n"
        "        return [{'id': 'native_value', 'type': 'string', 'default': str(native_demo.get_value())}]\n");

    QProcess inspect;
    inspect.setProgram(QString::fromStdString(host_path.string()));
    inspect.setProcessChannelMode(QProcess::MergedChannels);
    inspect.setArguments({QStringLiteral("--inspect"), QStringLiteral("--inspect-settings"),
                          QStringLiteral("--sdk-backend"), QStringLiteral("runtime"),
                          QStringLiteral("--extension-package-root"), QString::fromStdString(package_root.string()),
                          QStringLiteral("--entry-point"), QStringLiteral("inspect_compiled_wheel_ext"),
                          QStringLiteral("--process-class"), QStringLiteral("InspectCompiledWheelExtension"),
                          QStringLiteral("--environment-root"), QString::fromStdString(env_root.string()),
                          QStringLiteral("--runtime-root"), QString::fromStdString(runtime_root.string())});
    inspect.start();
    ASSERT_TRUE(inspect.waitForStarted());
    ASSERT_TRUE(inspect.waitForFinished(hostProcessWaitMs()));

    const std::string output = inspect.readAllStandardOutput().toStdString();
    EXPECT_EQ(inspect.exitStatus(), QProcess::NormalExit) << output;
    EXPECT_EQ(inspect.exitCode(), 0) << output;

    QJsonParseError parse_error;
    const QJsonDocument document = QJsonDocument::fromJson(QByteArray::fromStdString(output).trimmed(), &parse_error);
    ASSERT_EQ(parse_error.error, QJsonParseError::NoError) << output;
    ASSERT_TRUE(document.isObject()) << output;

    const QJsonArray schema = document.object().value(QStringLiteral("settings_schema")).toArray();
    ASSERT_EQ(schema.size(), 1);
    EXPECT_EQ(schema.at(0).toObject().value(QStringLiteral("id")).toString(), QStringLiteral("native_value"));
    EXPECT_EQ(schema.at(0).toObject().value(QStringLiteral("default")).toString(), QStringLiteral("41"));

    removeAllNoThrow(temp_root);
}

// Bootstrap accepts a platform-specific wheel that targets every Python 3.
TEST(ExtensionHostManagerTest, BundledBootstrapAcceptsPlatformSpecificPy3NoneWheelArtifacts)
{
    const std::filesystem::path host_path = extensionHostPathForCurrentAppDir();
    const std::filesystem::path runtime_root = runtimeRootForCurrentAppDir();
    const std::filesystem::path bundled_python = repositoryBundledPythonPath();
    AN_REQUIRE_FULL_EXTENSION_HOST(host_path);
    ASSERT_TRUE(std::filesystem::exists(runtime_root)) << runtime_root;
    ASSERT_FALSE(bundled_python.empty()) << "Repository bundled Python runtime must be available";

    const auto temp_root = uniqueExtensionRuntimeRootForTest("bundled-py3-none-wheel-bootstrap");
    removeAllNoThrow(temp_root);
    const RemovePathOnExit remove_temp_root{temp_root};
    std::filesystem::create_directories(temp_root);

    const auto bundled_root = temp_root / "extensions";
    const auto package_root = bundled_root / "an_py3none_bootstrap_demo";
    std::filesystem::create_directories(package_root / "wheelhouse");

    const auto wheel_path =
        createPlatformTaggedPy3NativeWheel(package_root, "native_py3none", "1.0.0", "manylinux_2_28_x86_64");
    ASSERT_FALSE(wheel_path.empty()) << "Failed to build py3-none native wheel";
    ASSERT_TRUE(std::filesystem::exists(wheel_path)) << wheel_path;

    const std::string wheel_hash = sha256HexForFile(wheel_path);
    ASSERT_FALSE(wheel_hash.empty()) << "Failed to hash " << wheel_path;

    const std::string unique_suffix = temp_root.filename().generic_string();
    std::string extension_id_suffix = unique_suffix;
    std::replace(extension_id_suffix.begin(), extension_id_suffix.end(), '-', '_');
    const std::string extension_id = "an.demo.py3none." + extension_id_suffix;
    const std::string shared_group = "py3none-wheel-" + unique_suffix;

    writeTextFile(package_root / "requirements.lock", "native-py3none==1.0.0 --hash=sha256:" + wheel_hash + "\n");
    writeTextFile(package_root / "py3none_bootstrap_ext.py",
                  "import afternight\n\n"
                  "class Py3NoneWheelExtension(afternight.ProcessExtension):\n"
                  "    pass\n");
    writeTextFile(package_root / "extension.json",
                  "{\n"
                  "  \"id\": \"" +
                      extension_id +
                      "\",\n"
                      "  \"name\": \"Py3 None Wheel Demo\",\n"
                      "  \"version\": \"0.1.0\",\n"
                      "  \"summary\": \"Bootstrap regression for platform-tagged py3-none wheels.\",\n"
                      "  \"type\": \"python\",\n"
                      "  \"entry_point\": \"py3none_bootstrap_ext\",\n"
                      "  \"process_class\": \"Py3NoneWheelExtension\",\n"
                      "  \"category\": \"filters\",\n"
                      "  \"launch_mode\": \"single_image\",\n"
                      "  \"sdk_backend\": \"runtime\",\n"
                      "  \"package_format_version\": 1,\n"
                      "  \"protocol_version\": 1,\n"
                      "  \"sdk_version\": 1,\n"
                      "  \"python_compat\": \">=3.14,<4\",\n"
                      "  \"runtime_targets\": [\"" +
                      an::ui::extensions::current_runtime_target() +
                      "\"],\n"
                      "  \"dependencies\": {\n"
                      "    \"dependency_context\": \"shared_group\",\n"
                      "    \"shared_group\": \"" +
                      shared_group +
                      "\",\n"
                      "    \"requirements_file\": \"requirements.lock\",\n"
                      "    \"pip\": {\n"
                      "      \"require_hashes\": true,\n"
                      "      \"no_deps\": true,\n"
                      "      \"find_links\": [\"wheelhouse\"]\n"
                      "    }\n"
                      "  }\n"
                      "}\n");

    an::ui::extensions::ExtensionDiscoveryOptions options;
    options.bundled_roots = {bundled_root};
    options.context.runtime_target = an::ui::extensions::current_runtime_target();
    options.context.runtime_root = runtime_root;
    options.context.bundled_python_executable = bundledPythonPathForCurrentAppDir();
    options.context.extension_host_executable = host_path;

    an::ui::extensions::ExtensionManager extension_manager;
    auto discovered = extension_manager.discoverExtensions(options);
    ASSERT_FALSE(discovered.empty());

    const auto extension_it = std::find_if(discovered.begin(), discovered.end(), [&](const auto& result) {
        return result.manifest.has_value() && result.manifest->id == extension_id;
    });
    ASSERT_NE(extension_it, discovered.end()) << "Py3-none bootstrap demo was not discovered";
    ASSERT_TRUE(extension_it->environment.has_value());
    ASSERT_EQ(extension_it->source, an::ui::extensions::ExtensionPackageSource::Bundled);
    ASSERT_EQ(extension_it->status, an::ui::extensions::ExtensionLoadStatus::Ready);
    removeAllNoThrow(extension_it->environment->environment_path);
    extension_it->environment->state = an::ui::extensions::ExtensionEnvironmentState::Missing;
    extension_it->environment->diagnostic.clear();

    an::ui::extensions::ExtensionEnvironmentManager environment_manager;
    std::string bootstrap_error;
    const int queued = environment_manager.queueBundledBootstrap(discovered, &bootstrap_error);
    ASSERT_TRUE(bootstrap_error.empty()) << bootstrap_error;
    ASSERT_GT(queued, 0);
    ASSERT_EQ(environment_manager.processQueuedBundledBootstrap(discovered, &bootstrap_error), queued)
        << bootstrap_error;
    ASSERT_TRUE(extension_it->environment.has_value());
    ASSERT_EQ(extension_it->status, an::ui::extensions::ExtensionLoadStatus::Ready);
    ASSERT_EQ(extension_it->environment->state, an::ui::extensions::ExtensionEnvironmentState::Ready);

    const auto env_python = environmentPythonPath(extension_it->environment->environment_path);
    std::string import_output;
    ASSERT_TRUE(runProcessExpectSuccess(
        env_python, {QStringLiteral("-c"), QStringLiteral("import native_py3none; print(native_py3none.get_value())")},
        {}, &import_output))
        << import_output;
    EXPECT_NE(import_output.find("41"), std::string::npos) << import_output;

    removeAllNoThrow(extension_it->environment->environment_path);
    removeAllNoThrow(temp_root);
}
#endif // !_WIN32

// Every VeraLux process, where the extension repository is named, changes an image within the unit range.
TEST(ExtensionHostManagerTest, VeraLuxRepositoryProcessesExecuteThroughRuntimeHost)
{
    if (environmentVariableValue("AFTERNIGHT_RUN_VERALUX_EXECUTION_SMOKE") != "1")
    {
        GTEST_SKIP() << "Set AFTERNIGHT_RUN_VERALUX_EXECUTION_SMOKE=1 to run the VeraLux execution smoke test";
    }

    const std::string repository_root_env = environmentVariableValue("AFTERNIGHT_EXTENSION_REPOSITORY_ROOT");
    if (repository_root_env.empty())
    {
        GTEST_SKIP() << "Set AFTERNIGHT_EXTENSION_REPOSITORY_ROOT to the afternight-extensions checkout";
    }

    const std::filesystem::path repository_root(repository_root_env);
    std::filesystem::path package_root = repository_root / "packages" / "veralux" / "package";
    const std::string package_root_override = environmentVariableValue("AFTERNIGHT_VERALUX_PACKAGE_ROOT");
    if (!package_root_override.empty())
    {
        package_root = package_root_override;
    }
    const auto package_source = package_root_override.empty() ? an::ui::extensions::ExtensionPackageSource::Repository
                                                              : an::ui::extensions::ExtensionPackageSource::User;
    std::error_code ec;
    ASSERT_TRUE(std::filesystem::is_regular_file(package_root / "extension.json", ec))
        << "Expected VeraLux package manifest at " << package_root / "extension.json";

    const std::filesystem::path host_path = extensionHostPathForCurrentAppDir();
    const std::filesystem::path runtime_root = runtimeRootForCurrentAppDir();
    const std::filesystem::path bundled_python = repositoryBundledPythonPath();
    AN_REQUIRE_FULL_EXTENSION_HOST(host_path);
    ASSERT_TRUE(std::filesystem::exists(runtime_root)) << runtime_root;
    ASSERT_FALSE(bundled_python.empty()) << "Repository bundled Python runtime must be available";

    const auto temp_root = uniqueExtensionRuntimeRootForTest("veralux-execute");
    removeAllNoThrow(temp_root);
    RemovePathOnExit temp_guard{temp_root};
    ASSERT_TRUE(std::filesystem::create_directories(temp_root, ec) || std::filesystem::exists(temp_root));

    an::ui::extensions::ExtensionDiscoveryContext context;
    context.bundled_python_version = "3.14.4";
    context.runtime_target = an::ui::extensions::current_runtime_target();
    context.host_bootstrap_fingerprint = "host-bootstrap-test";
    context.bundled_python_executable = bundled_python;
    context.extension_host_executable = host_path;
    context.runtime_root = runtime_root;
    context.runtime_backend_available = true;
    context.workflow_launch_mode_available = true;
    context.settings_schema_inspection_timeout_ms += hostExitAllowanceMs();
    // Under the run's short root, as discovery provisions: under this case's own root the environment's deepest
    // paths pass the length Windows opens a file from without its extended-length form.
    context.environments_root = shortExtensionEnvironmentsRoot();

    const auto load_result =
        an::ui::extensions::load_extension_manifest_file(package_root / "extension.json", package_source, context);
    ASSERT_TRUE(load_result.manifest.has_value()) << load_result.diagnostic;
    ASSERT_EQ(load_result.status, an::ui::extensions::ExtensionLoadStatus::Ready) << load_result.diagnostic;

    const auto& manifest = *load_result.manifest;
    ASSERT_EQ(manifest.id, "veralux");
    ASSERT_EQ(manifest.processes.size(), 8u);

    an::ui::extensions::ExtensionEnvironmentSnapshot environment;
    std::string error;
    const std::string environment_root_override = environmentVariableValue("AFTERNIGHT_VERALUX_ENVIRONMENT_ROOT");
    if (!environment_root_override.empty())
    {
        environment.environment_path = environment_root_override;
        environment.state = an::ui::extensions::ExtensionEnvironmentState::Ready;
    }
    else
    {
        an::ui::extensions::ExtensionEnvironmentManager environment_manager;
        std::vector<an::ui::extensions::ExtensionInstalledDistribution> distributions;
        ASSERT_TRUE(environment_manager.provisionEnvironmentForManifest(manifest, package_source, context, &environment,
                                                                        &distributions, &error))
            << error;
    }
    ASSERT_EQ(environment.state, an::ui::extensions::ExtensionEnvironmentState::Ready) << environment.diagnostic;
    ASSERT_TRUE(std::filesystem::exists(environment.environment_path)) << environment.environment_path;
    ASSERT_TRUE(std::filesystem::exists(environment.environment_path / ".environment_ready"));

    constexpr uint32_t kWidth = 64;
    constexpr uint32_t kHeight = 48;
    const auto clamp01 = [](const double value) { return (std::min)(1.0, (std::max)(0.0, value)); };
    std::optional<an::consumer::ImageAsset> made = an::tests::processes::makeAsset<float>(
        testSession(), kWidth, kHeight, an::tests::processes::Channels::Rgb,
        [&](std::uint32_t x, std::uint32_t y, std::uint32_t channel) {
            const double fx = static_cast<double>(x) / static_cast<double>(kWidth - 1);
            const double fy = static_cast<double>(y) / static_cast<double>(kHeight - 1);
            const double checker = ((x / 4u + y / 3u) % 2u == 0u) ? 0.08 : -0.04;
            const double value = channel == 0u   ? clamp01(0.18 + 0.48 * fx + checker)
                                 : channel == 1u ? clamp01(0.16 + 0.52 * fy - checker * 0.5)
                                                 : clamp01(0.22 + 0.36 * (1.0 - fx) + checker * 0.25);
            return static_cast<float>(value);
        });
    ASSERT_TRUE(made.has_value());
    const auto source_image = std::make_shared<an::consumer::ImageAsset>(std::move(*made));

    // StarComposer composes the source, its starless base, with a stars image it reads from a view the application
    // lends, as its window's Stars selection names one: a dark field with a few bright points.
    constexpr const char* kStarsViewName = "veralux-stars";
    std::optional<an::consumer::ImageAsset> stars_made =
        an::tests::processes::makeAsset<float>(testSession(), kWidth, kHeight, an::tests::processes::Channels::Rgb,
                                               [](std::uint32_t x, std::uint32_t y, std::uint32_t) {
                                                   return ((x % 11u == 5u) && (y % 9u == 4u)) ? 0.9f : 0.01f;
                                               });
    ASSERT_TRUE(stars_made.has_value());
    const auto stars_image = std::make_shared<an::consumer::ImageAsset>(std::move(*stars_made));

    struct VeraLuxRuntimeExecutionSpec
    {
        const char* process_id = nullptr;
        const char* process_class = nullptr;
        const char* buffer_suffix = nullptr;
        QCborMap params;
        double min_abs_delta = 1e-4;
    };

    const std::vector<VeraLuxRuntimeExecutionSpec> process_specs = {
        {"veralux.alchemy", "VeraLuxAlchemyExtension", "alchemy",
         QCborMap{{QStringLiteral("quantum_unmix"), false},
                  {QStringLiteral("bg_align"), true},
                  {QStringLiteral("auto_fit"), true},
                  {QStringLiteral("boost"), 1.35},
                  {QStringLiteral("mix_r"), 0.85},
                  {QStringLiteral("mix_g"), 0.35},
                  {QStringLiteral("mix_b"), 1.0},
                  {QStringLiteral("sensor_profile"), QStringLiteral("Generic OSC")}}},
        {"veralux.curves", "VeraLuxCurvesExtension", "curves",
         QCborMap{{QStringLiteral("domain"), QStringLiteral("RGB/K")},
                  {QStringLiteral("black_point"), 0.04},
                  {QStringLiteral("shadow_lift"), 0.06},
                  {QStringLiteral("midtone_input"), 0.42},
                  {QStringLiteral("midtone_output"), 0.64},
                  {QStringLiteral("highlight_compression"), 0.10},
                  {QStringLiteral("white_point"), 0.96},
                  {QStringLiteral("range_enabled"), false}}},
        {"veralux.hypermetric_stretch", "VeraLuxHyperMetricStretchExtension", "hypermetric-stretch",
         QCborMap{{QStringLiteral("processing_mode"), QStringLiteral("ready_to_use")},
                  {QStringLiteral("working_space"), QStringLiteral("Rec.709 (Recommended)")},
                  {QStringLiteral("target_bg"), 0.22},
                  {QStringLiteral("use_adaptive_anchor"), true},
                  {QStringLiteral("auto_log_d"), false},
                  {QStringLiteral("log_d"), 2.2},
                  {QStringLiteral("protect_b"), 5.0},
                  {QStringLiteral("convergence_power"), 3.5},
                  {QStringLiteral("color_strategy"), 0.30},
                  {QStringLiteral("linear_expansion"), 0.08},
                  {QStringLiteral("color_grip"), 0.80},
                  {QStringLiteral("shadow_convergence"), 0.18}}},
        {"veralux.nox", "VeraLuxNoxExtension", "nox",
         QCborMap{{QStringLiteral("auto_tune"), false},
                  {QStringLiteral("auto_mask"), false},
                  {QStringLiteral("stiffness"), 1.5},
                  {QStringLiteral("rejection_power"), 45.0},
                  {QStringLiteral("model_grid"), 16.0},
                  {QStringLiteral("correction_strength"), 1.0},
                  {QStringLiteral("output_model"), false}}},
        {"veralux.starcomposer", "VeraLuxStarComposerExtension", "starcomposer",
         QCborMap{{QStringLiteral("stars_view"), QString::fromLatin1(kStarsViewName)},
                  {QStringLiteral("working_space"), QStringLiteral("Rec.709 (Recommended)")},
                  {QStringLiteral("use_adaptive_anchor"), false},
                  {QStringLiteral("log_d"), 2.0},
                  {QStringLiteral("profile_hardness"), 70.0},
                  {QStringLiteral("color_grip"), 0.80},
                  {QStringLiteral("shadow_convergence"), 0.20},
                  {QStringLiteral("large_structure_rejection"), 0.35},
                  {QStringLiteral("star_reduction"), 0.45},
                  {QStringLiteral("optical_healing"), 8.0}}},
        {"veralux.vectra", "VeraLuxVectraExtension", "vectra",
         QCborMap{{QStringLiteral("red_hue"), 28.0},
                  {QStringLiteral("red_saturation"), 0.45},
                  {QStringLiteral("green_hue"), -18.0},
                  {QStringLiteral("green_saturation"), 0.25},
                  {QStringLiteral("blue_hue"), 22.0},
                  {QStringLiteral("blue_saturation"), 0.50},
                  {QStringLiteral("cyan_hue"), -12.0},
                  {QStringLiteral("cyan_saturation"), 0.20},
                  {QStringLiteral("magenta_hue"), 16.0},
                  {QStringLiteral("magenta_saturation"), 0.30},
                  {QStringLiteral("yellow_hue"), -14.0},
                  {QStringLiteral("yellow_saturation"), 0.25},
                  {QStringLiteral("shadow_authority"), 0.0},
                  {QStringLiteral("protect_stars"), false}}},
        {"veralux.revela", "VeraLuxRevelaExtension", "revela",
         QCborMap{{QStringLiteral("texture"), 1.0},
                  {QStringLiteral("structure"), 1.0},
                  {QStringLiteral("shadow_authority"), 0.0},
                  {QStringLiteral("protect_stars"), false},
                  {QStringLiteral("show_mask"), false}}},
        {"veralux.silentium", "VeraLuxSilentiumExtension", "silentium",
         QCborMap{{QStringLiteral("noise_intensity"), 70.0},
                  {QStringLiteral("adaptive_noise"), false},
                  {QStringLiteral("detail_guard"), 20.0},
                  {QStringLiteral("shadow_smoothness"), 35.0},
                  {QStringLiteral("enable_chroma"), true},
                  {QStringLiteral("chroma_strength"), 45.0},
                  {QStringLiteral("protect_highlights"), false}}}};

    std::size_t executed_process_count = 0;
    for (const auto& spec : process_specs)
    {
        SCOPED_TRACE(spec.process_id);
        const auto spec_process_it =
            std::find_if(manifest.processes.begin(), manifest.processes.end(),
                         [&spec](const an::ui::extensions::ExtensionProcessManifest& process) {
                             return process.id == spec.process_id && process.class_name == spec.process_class;
                         });
        ASSERT_NE(spec_process_it, manifest.processes.end());

        const auto shared_buffer_root = uniqueSharedBufferRootForTest(std::string("veralux-") + spec.buffer_suffix);
        removeAllNoThrow(shared_buffer_root);
        RemovePathOnExit shared_buffer_guard{shared_buffer_root};

        InMemorySettings settings_store;
        GlobalSettingsPersistence& settings = settings_store.repository();
        an::ui::extensions::ExtensionSdkService sdk_service(&settings);
        sdk_service.setViewStateProvider([&]() {
            an::ui::extensions::ExtensionViewState stars_view;
            stars_view.view_name = kStarsViewName;
            stars_view.width = static_cast<int>(kWidth);
            stars_view.height = static_cast<int>(kHeight);
            stars_view.channels = 3;
            stars_view.has_image = true;
            return std::vector<an::ui::extensions::ExtensionViewState>{stars_view};
        });
        std::vector<an::ui::extensions::SharedImageBuffer> lent_views;
        sdk_service.setViewSnapshotProvider(
            [&](const std::string_view session_id, const std::string_view request_id, const std::string_view view_name,
                an::ui::extensions::ExtensionViewSnapshot* out_snapshot, std::string* snapshot_error) -> bool {
                if (view_name != kStarsViewName || !out_snapshot)
                {
                    if (snapshot_error)
                    {
                        *snapshot_error = "no such view";
                    }
                    return false;
                }
                an::ui::extensions::SharedBufferManager lending_manager(shared_buffer_root);
                an::ui::extensions::SharedImageBuffer lent;
                if (!exportImageOrReport(lending_manager, *stars_image, session_id,
                                         an::ui::extensions::SharedBufferRole::Preview, &lent, snapshot_error,
                                         std::string(request_id) + ".stars", "stars"))
                {
                    return false;
                }
                out_snapshot->source_buffer = lent.descriptor();
                lent_views.push_back(std::move(lent));
                return true;
            });

        an::ui::extensions::ExtensionHostManager manager;
        manager.setSdkService(&sdk_service);
        an::ui::extensions::ExtensionHostLaunchRequest request;
        request.extension_id = spec_process_it->id;
        request.display_name = spec_process_it->display_name;
        request.sdk_backend = "runtime";
        request.launch_mode = manifest.launch_mode;
        request.host_executable = host_path;
        request.capabilities = {"execute"};
        request.host_arguments = {"--extension-package-root",
                                  manifest.package_root,
                                  "--entry-point",
                                  manifest.entry_point,
                                  "--process-class",
                                  spec_process_it->class_name,
                                  "--environment-root",
                                  environment.environment_path.string(),
                                  "--runtime-root",
                                  runtime_root.string(),
                                  "--stay-alive-ms",
                                  std::to_string(15000 * kIpcTimingMultiplier)};

        auto prepared = manager.prepareSession(request);
        an::ui::extensions::ExtensionHostRunningSession running_session;
        ASSERT_TRUE(manager.launchSession(prepared, &running_session, &error)) << error;
        SessionGuard session_guard{manager, running_session};

        an::ui::extensions::SharedBufferManager shared_buffer_manager(shared_buffer_root);
        an::ui::extensions::SharedImageBuffer source_buffer;
        ASSERT_TRUE(exportImageOrReport(shared_buffer_manager, *source_image,
                                        running_session.prepared_session.session_id,
                                        an::ui::extensions::SharedBufferRole::Source, &source_buffer, &error,
                                        std::string(spec.buffer_suffix) + "-source-buffer", "source"))
            << error;

        std::string request_id;
        ASSERT_TRUE(manager.sendExecuteRequest(&running_session, "veralux-" + std::string(spec.buffer_suffix),
                                               spec.params, {source_buffer.descriptor()}, &request_id, &error))
            << error;
        ASSERT_FALSE(request_id.empty());

        ASSERT_TRUE(pumpUntilRequestCompletes(manager, &running_session, 20000 * kIpcTimingMultiplier, &error))
            << error;

        EXPECT_FALSE(running_session.last_preview_result.has_value());
        ASSERT_TRUE(running_session.last_view_commit.has_value());
        ASSERT_EQ(running_session.last_view_commit->result_buffers.size(), 1u);
        EXPECT_EQ(running_session.last_view_commit->request_id, request_id);

        std::optional<an::consumer::ImageAsset> output_image;
        ASSERT_TRUE(importBufferOrReport(
            shared_buffer_manager, running_session.last_view_commit->result_buffers.front(), &output_image, &error))
            << error;
        ASSERT_TRUE(output_image);
        ASSERT_EQ(widthOf(*output_image), widthOf(*source_image));
        ASSERT_EQ(heightOf(*output_image), heightOf(*source_image));
        ASSERT_EQ(channelsOf(*output_image), channelsOf(*source_image));
        ASSERT_TRUE(hasSampleType(*output_image, an::SampleType::Float32));

        double max_abs_delta = 0.0;
        for (std::size_t channel = 0; channel < channelsOf(*source_image); ++channel)
        {
            for (std::size_t y = 0; y < heightOf(*source_image); ++y)
            {
                for (std::size_t x = 0; x < widthOf(*source_image); ++x)
                {
                    const double source_value = sampleAt(*source_image, x, y, channel);
                    const double output_value = sampleAt(*output_image, x, y, channel);
                    ASSERT_TRUE(std::isfinite(output_value))
                        << "Non-finite VeraLux output at channel=" << channel << " x=" << x << " y=" << y;
                    ASSERT_GE(output_value, -1e-5)
                        << "Negative VeraLux output at channel=" << channel << " x=" << x << " y=" << y;
                    ASSERT_LE(output_value, 1.00001)
                        << "Out-of-range VeraLux output at channel=" << channel << " x=" << x << " y=" << y;
                    max_abs_delta = (std::max)(max_abs_delta, std::fabs(output_value - source_value));
                }
            }
        }
        EXPECT_GT(max_abs_delta, spec.min_abs_delta);
        ++executed_process_count;
    }
    EXPECT_EQ(executed_process_count, process_specs.size());
}

// The VeraLux Silentium native window, where asked for, starts against a large grey view and stays active.
TEST(ExtensionHostManagerTest, VeraLuxSilentiumNativeWindowStartupSmoke)
{
    if (environmentVariableValue("AFTERNIGHT_RUN_VERALUX_NATIVE_WINDOW_SMOKE") != "1")
    {
        GTEST_SKIP() << "Set AFTERNIGHT_RUN_VERALUX_NATIVE_WINDOW_SMOKE=1 to run the VeraLux native-window smoke test";
    }

    const std::string repository_root_env = environmentVariableValue("AFTERNIGHT_EXTENSION_REPOSITORY_ROOT");
    if (repository_root_env.empty())
    {
        GTEST_SKIP() << "Set AFTERNIGHT_EXTENSION_REPOSITORY_ROOT to the afternight-extensions checkout";
    }

    const std::filesystem::path repository_root(repository_root_env);
    std::filesystem::path package_root = repository_root / "packages" / "veralux" / "package";
    const std::string package_root_override = environmentVariableValue("AFTERNIGHT_VERALUX_PACKAGE_ROOT");
    if (!package_root_override.empty())
    {
        package_root = package_root_override;
    }
    const auto package_source = package_root_override.empty() ? an::ui::extensions::ExtensionPackageSource::Repository
                                                              : an::ui::extensions::ExtensionPackageSource::User;
    std::error_code ec;
    ASSERT_TRUE(std::filesystem::is_regular_file(package_root / "extension.json", ec))
        << "Expected VeraLux package manifest at " << package_root / "extension.json";

    const std::filesystem::path host_path = extensionHostPathForCurrentAppDir();
    const std::filesystem::path runtime_root = runtimeRootForCurrentAppDir();
    const std::filesystem::path bundled_python = repositoryBundledPythonPath();
    AN_REQUIRE_FULL_EXTENSION_HOST(host_path);
    ASSERT_TRUE(std::filesystem::exists(runtime_root)) << runtime_root;
    ASSERT_FALSE(bundled_python.empty()) << "Repository bundled Python runtime must be available";

    const auto temp_root = uniqueExtensionRuntimeRootForTest("veralux-native-window");
    removeAllNoThrow(temp_root);
    RemovePathOnExit temp_guard{temp_root};
    ASSERT_TRUE(std::filesystem::create_directories(temp_root, ec) || std::filesystem::exists(temp_root));

    an::ui::extensions::ExtensionDiscoveryContext context;
    context.bundled_python_version = "3.14.4";
    context.runtime_target = an::ui::extensions::current_runtime_target();
    context.host_bootstrap_fingerprint = "host-bootstrap-test";
    context.bundled_python_executable = bundled_python;
    context.extension_host_executable = host_path;
    context.runtime_root = runtime_root;
    context.runtime_backend_available = true;
    context.workflow_launch_mode_available = true;
    context.settings_schema_inspection_timeout_ms += hostExitAllowanceMs();
    // Under the run's short root, as discovery provisions: under this case's own root the environment's deepest
    // paths pass the length Windows opens a file from without its extended-length form.
    context.environments_root = shortExtensionEnvironmentsRoot();

    const auto load_result =
        an::ui::extensions::load_extension_manifest_file(package_root / "extension.json", package_source, context);
    ASSERT_TRUE(load_result.manifest.has_value()) << load_result.diagnostic;
    ASSERT_EQ(load_result.status, an::ui::extensions::ExtensionLoadStatus::Ready) << load_result.diagnostic;

    const auto& manifest = *load_result.manifest;
    const auto process_it = std::find_if(manifest.processes.begin(), manifest.processes.end(), [](const auto& process) {
        return process.id == "veralux.silentium" && process.class_name == "VeraLuxSilentiumExtension";
    });
    ASSERT_NE(process_it, manifest.processes.end());

    an::ui::extensions::ExtensionEnvironmentSnapshot environment;
    std::string error;
    const std::string environment_root_override = environmentVariableValue("AFTERNIGHT_VERALUX_ENVIRONMENT_ROOT");
    if (!environment_root_override.empty())
    {
        environment.environment_path = environment_root_override;
        environment.state = an::ui::extensions::ExtensionEnvironmentState::Ready;
    }
    else
    {
        an::ui::extensions::ExtensionEnvironmentManager environment_manager;
        std::vector<an::ui::extensions::ExtensionInstalledDistribution> distributions;
        ASSERT_TRUE(environment_manager.provisionEnvironmentForManifest(manifest, package_source, context, &environment,
                                                                        &distributions, &error))
            << error;
    }
    ASSERT_EQ(environment.state, an::ui::extensions::ExtensionEnvironmentState::Ready) << environment.diagnostic;
    ASSERT_TRUE(std::filesystem::exists(environment.environment_path)) << environment.environment_path;

    constexpr uint32_t kWidth = 3008;
    constexpr uint32_t kHeight = 3004;
    std::optional<an::consumer::ImageAsset> made = an::tests::processes::makeAsset<float>(
        testSession(), kWidth, kHeight, an::tests::processes::Channels::Grey,
        [&](std::uint32_t x, std::uint32_t y, std::uint32_t) {
            const double fx = static_cast<double>(x) / static_cast<double>(kWidth - 1);
            const double fy = static_cast<double>(y) / static_cast<double>(kHeight - 1);
            const double ripple = ((x / 5u + y / 4u) % 2u == 0u) ? 0.025 : -0.015;
            return static_cast<float>((std::clamp)(0.18 + 0.42 * fx + 0.25 * fy + ripple, 0.0, 1.0));
        });
    ASSERT_TRUE(made.has_value());
    const auto source_image = std::make_shared<an::consumer::ImageAsset>(std::move(*made));

    std::string current_view_name = "veralux-silentium-smoke";
    std::string current_file_name = "/tmp/veralux-silentium-smoke.xisf";

    InMemorySettings settings_store;
    GlobalSettingsPersistence& settings = settings_store.repository();
    an::tests::processes::ViewHandle target = targetViewOf("veralux-silentium-smoke", source_image);
    ASSERT_NE(target, nullptr);

    an::tests::processes::framework::CaseLaunchCoordinator coordinator;
    coordinator.configureSdkService(
        &settings,
        [&]() -> std::vector<an::ui::extensions::ExtensionViewState> {
            an::ui::extensions::ExtensionViewState view_state;
            view_state.view_name = current_view_name;
            view_state.file_name = current_file_name;
            view_state.view_mode = "standard";
            view_state.channel_view_mode = "gray";
            view_state.zoom_percent = 100.0;
            view_state.is_active = true;
            view_state.has_image = true;
            view_state.width = static_cast<int>(widthOf(*source_image));
            view_state.height = static_cast<int>(heightOf(*source_image));
            view_state.channels = static_cast<int>(channelsOf(*source_image));
            return {view_state};
        },
        [&](const std::string_view session_id, const std::string_view request_id, const std::string_view view_name,
            an::ui::extensions::ExtensionViewSnapshot* out_snapshot, std::string* snapshot_error) -> bool {
            if (!out_snapshot)
            {
                if (snapshot_error)
                {
                    *snapshot_error = "Extension SDK snapshot output is null";
                }
                return false;
            }
            EXPECT_EQ(view_name, current_view_name);

            *out_snapshot = an::ui::extensions::ExtensionViewSnapshot{};
            an::ui::extensions::SharedBufferManager shared_buffer_manager;
            an::ui::extensions::SharedImageBuffer buffer;
            if (!exportImageOrReport(shared_buffer_manager, *source_image, std::string(session_id),
                                     an::ui::extensions::SharedBufferRole::Preview, &buffer, snapshot_error,
                                     std::string(request_id) + ".silentium_snapshot",
                                     std::string(view_name) + ".snapshot",
                                     an::ui::extensions::SharedBufferAccessMode::ReadOnly))
            {
                return false;
            }

            out_snapshot->source_buffer = buffer.descriptor();
            return true;
        },
        []() -> an::ui::extensions::ExtensionThemeSnapshot {
            an::ui::extensions::ExtensionThemeSnapshot snapshot;
            snapshot.theme_name = "AfterNight Dark";
            snapshot.tokens = QCborMap{{QStringLiteral("is_dark"), true},
                                       {QStringLiteral("spacing"), QCborMap{{QStringLiteral("md"), 12}}}};
            return snapshot;
        });
    coordinator.setManagedImageHooks(
        [](ImageViewControl*, an::consumer::ImageAsset&&, const std::string&, const std::string&) {});

    an::ui::extensions::ExtensionHostLaunchBinding binding;
    binding.descriptor_id = process_it->id;
    binding.extension_id = manifest.id;
    binding.display_name = process_it->display_name;
    binding.sdk_backend = "runtime";
    binding.launch_mode = manifest.launch_mode;
    binding.host_executable = host_path;
    binding.capabilities = {"execute", "preview"};
    binding.process_capabilities = process_it->capabilities;
    binding.host_arguments = {"--extension-package-root",
                              manifest.package_root,
                              "--entry-point",
                              manifest.entry_point,
                              "--process-class",
                              process_it->class_name,
                              "--environment-root",
                              environment.environment_path.string(),
                              "--runtime-root",
                              runtime_root.string(),
                              "--stay-alive-ms",
                              std::to_string(15000 * kIpcTimingMultiplier)};

    auto& runtime_status_registry = an::ui::extensions::ExtensionRuntimeStatusRegistry::instance();
    runtime_status_registry.clear();

    ASSERT_TRUE(coordinator.launchBoundDescriptor(binding, target.get(), &error)) << error;
    ASSERT_TRUE(
        pumpCoordinatorUntilNativeSessionReady(coordinator, binding.descriptor_id, 2500 * kIpcTimingMultiplier));

    coordinator.handleViewControlUpdated(target.get());
    QElapsedTimer preview_startup_timer;
    preview_startup_timer.start();
    while (preview_startup_timer.elapsed() < 6000 * kIpcTimingMultiplier)
    {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 1);
        coordinator.pumpSessions();
        QThread::msleep(20);
    }
    const auto snapshot = runtime_status_registry.snapshotForExtension(binding.extension_id);
    ASSERT_TRUE(!snapshot.has_value() || !snapshot->has_termination_result)
        << (snapshot.has_value() ? snapshot->termination_detail : std::string("unexpected termination"));
    ASSERT_TRUE(coordinator.hasActiveSessionForDescriptor(binding.descriptor_id, target.get()));

    ASSERT_TRUE(stopCoordinatorAndWaitForSessionAbsence(coordinator, 2500 * kIpcTimingMultiplier));
}

// A Python extension's execute reads and writes its settings, logs through the SDK and changes the shared buffer.
TEST(ExtensionHostManagerTest, RealPythonExtensionExecuteUsesSdkSettingsLoggingAndSharedBuffers)
{
    const std::filesystem::path host_path = extensionHostPathForCurrentAppDir();
    const std::filesystem::path runtime_root = runtimeRootForCurrentAppDir();
    const std::filesystem::path bundled_python = repositoryBundledPythonPath();
    AN_REQUIRE_FULL_EXTENSION_HOST(host_path);
    ASSERT_TRUE(std::filesystem::exists(runtime_root)) << runtime_root;
    ASSERT_FALSE(bundled_python.empty()) << "Repository bundled Python runtime must be available";

    const auto temp_root = uniqueExtensionRuntimeRootForTest("execute");
    removeAllNoThrow(temp_root);
    const RemovePathOnExit remove_temp_root{temp_root};
    std::filesystem::create_directories(temp_root);

    const auto env_root = temp_root / "env";
    const auto package_root = temp_root / "runtime_exec_ext";
    std::filesystem::create_directories(package_root);

    ASSERT_NO_FATAL_FAILURE(createVirtualEnvironment(bundled_python, env_root));

    if (!provisionBundledRuntimePackageIntoEnvironment(runtime_root, env_root, "numpy"))
        GTEST_SKIP() << "numpy not available in bundled runtime";

    {
        std::ofstream out(package_root / "runtime_exec_ext.py", std::ios::binary);
        ASSERT_TRUE(out.is_open());
        out << "import afternight\n"
            << "import numpy as np\n\n"
            << "class RuntimeExecuteExtension(afternight.ProcessExtension):\n"
            << "    def execute(self, target, src_image, dst_image, params, progress, masks=None, weights=None, "
               "output_masks=None):\n"
            << "        config = self.settings.get('config', {'enabled': False, 'labels': []})\n"
            << "        increment = int(self.settings.get('increment', 3))\n"
            << "        scratch = self.settings.get('scratch', None)\n"
            << "        keys_before = sorted(self.settings.keys())\n"
            << "        assert config['enabled'] is True\n"
            << "        assert config['labels'] == ['alpha', 'beta']\n"
            << "        assert scratch['ephemeral'] is True\n"
            << "        assert keys_before == ['config', 'increment', 'scratch']\n"
            << "        self.settings.set('last_token', params.get('_params_token', ''))\n"
            << "        self.settings.set('last_summary', {\n"
            << "            'increment': increment,\n"
            << "            'token': params.get('_params_token', ''),\n"
            << "            'enabled': config['enabled'],\n"
            << "            'labels': config['labels'],\n"
            << "        })\n"
            << "        self.settings.remove('scratch')\n"
            << "        assert sorted(self.settings.keys()) == ['config', 'increment', 'last_summary', 'last_token']\n"
            << "        label_count = len(config['labels'])\n"
            << "        afternight.log_info(f'increment={increment};labels={label_count}', "
               "component='runtime.execute.test')\n"
            << "        progress.set_text('Applying increment')\n"
            << "        dst_image.from_numpy(src_image.to_numpy() + increment)\n";
    }

    InMemorySettings settings_store;
    GlobalSettingsPersistence& settings = settings_store.repository();
    an::ui::extensions::ExtensionSdkService sdk_service(&settings);
    std::string error;
    ASSERT_TRUE(sdk_service.upsertExtensionSetting("an.demo.runtime", "increment", QCborValue(3), &error)) << error;
    ASSERT_TRUE(sdk_service.upsertExtensionSetting(
        "an.demo.runtime", "config",
        QCborMap{{QStringLiteral("enabled"), true},
                 {QStringLiteral("labels"), QCborArray{QStringLiteral("alpha"), QStringLiteral("beta")}}},
        &error))
        << error;
    ASSERT_TRUE(sdk_service.upsertExtensionSetting("an.demo.runtime", "scratch",
                                                   QCborMap{{QStringLiteral("ephemeral"), true}}, &error))
        << error;

    an::ui::extensions::ExtensionHostManager manager;
    manager.setSdkService(&sdk_service);

    an::ui::extensions::ExtensionHostLaunchRequest request;
    request.extension_id = "an.demo.runtime";
    request.sdk_backend = "runtime";
    request.host_executable = host_path;
    request.capabilities = {"execute"};
    request.host_arguments = {
        "--extension-package-root", package_root.string(), "--entry-point",
        "runtime_exec_ext",         "--process-class",     "RuntimeExecuteExtension",
        "--environment-root",       env_root.string(),     "--runtime-root",
        runtime_root.string(),      "--stay-alive-ms",     std::to_string(5000 * kIpcTimingMultiplier)};

    auto prepared = manager.prepareSession(request);
    an::ui::extensions::ExtensionHostRunningSession running_session;
    ASSERT_TRUE(manager.launchSession(prepared, &running_session, &error)) << error;
    SessionGuard session_guard{manager, running_session};

    const auto root = uniqueSharedBufferRootForTest("python-execute");
    removeAllNoThrow(root);
    const RemovePathOnExit remove_root{root};
    auto source_buffer = makeSourceBuffer(root, running_session.prepared_session.session_id, &error);
    ASSERT_TRUE(source_buffer.valid()) << error;

    std::string request_id;
    ASSERT_TRUE(
        manager.sendExecuteRequest(&running_session, "exec-token", {source_buffer.descriptor()}, &request_id, &error))
        << error;
    ASSERT_FALSE(request_id.empty());

    ASSERT_TRUE(pumpUntilRequestCompletes(manager, &running_session, 3000 * kIpcTimingMultiplier, &error)) << error;

    EXPECT_FALSE(running_session.last_preview_result.has_value());
    ASSERT_TRUE(running_session.last_view_commit.has_value());
    ASSERT_EQ(running_session.last_view_commit->result_buffers.size(), 1u);
    EXPECT_GE(running_session.serviced_sdk_message_count, 2);

    an::ui::extensions::SharedBufferManager shared_buffer_manager(root);
    an::ui::extensions::SharedImageBuffer output_buffer;
    ASSERT_TRUE(openBufferOrReport(shared_buffer_manager, running_session.last_view_commit->result_buffers.front(),
                                   &output_buffer, &error))
        << error;

    const auto* src_bytes = reinterpret_cast<const std::uint8_t*>(source_buffer.bytes());
    const auto* out_bytes = reinterpret_cast<const std::uint8_t*>(output_buffer.bytes());
    ASSERT_NE(src_bytes, nullptr);
    ASSERT_NE(out_bytes, nullptr);
    for (std::uint64_t i = 0; i < source_buffer.mappedSize(); ++i)
    {
        EXPECT_EQ(out_bytes[i], static_cast<std::uint8_t>(src_bytes[i] + 3u));
    }

    QString stored_token;
    ASSERT_TRUE(sdk_service.selectExtensionSetting("an.demo.runtime", "last_token", &stored_token, {}, &error))
        << error;
    EXPECT_EQ(stored_token, QStringLiteral("exec-token"));

    QCborValue summary_value;
    ASSERT_TRUE(
        sdk_service.selectExtensionSetting("an.demo.runtime", "last_summary", &summary_value, QCborValue(), &error))
        << error;
    ASSERT_TRUE(summary_value.isMap());

    const QCborMap summary = summary_value.toMap();
    EXPECT_EQ(summary.value(QStringLiteral("increment")).toInteger(), 3);
    EXPECT_EQ(summary.value(QStringLiteral("token")).toString(), QStringLiteral("exec-token"));
    EXPECT_TRUE(summary.value(QStringLiteral("enabled")).toBool());

    const QCborArray labels = summary.value(QStringLiteral("labels")).toArray();
    ASSERT_EQ(labels.size(), 2);
    EXPECT_EQ(labels.at(0).toString(), QStringLiteral("alpha"));
    EXPECT_EQ(labels.at(1).toString(), QStringLiteral("beta"));

    QCborValue removed_scratch;
    ASSERT_TRUE(sdk_service.selectExtensionSetting("an.demo.runtime", "scratch", &removed_scratch,
                                                   QCborValue(QStringLiteral("missing")), &error))
        << error;
    ASSERT_TRUE(removed_scratch.isString());
    EXPECT_EQ(removed_scratch.toString(), QStringLiteral("missing"));

    QStringList setting_keys;
    ASSERT_TRUE(sdk_service.selectExtensionSettingKeys("an.demo.runtime", &setting_keys, &error)) << error;
    const QStringList expected_keys = {QStringLiteral("config"), QStringLiteral("increment"),
                                       QStringLiteral("last_summary"), QStringLiteral("last_token")};
    EXPECT_EQ(setting_keys, expected_keys);

    const auto& events = sdk_service.recentLogEvents();
    ASSERT_FALSE(events.empty());
    EXPECT_EQ(events.back().component, "runtime.execute.test");
    EXPECT_NE(events.back().message.find("increment=3;labels=2"), std::string::npos);

    removeAllNoThrow(root);
    removeAllNoThrow(temp_root);
}

// A runtime extension whose import outlasts the handshake's bound, as a heavy one's first start from a freshly
// provisioned environment does, still launches, connects and executes: a host that starts its runtime before it
// connects is waited for while it runs, up to the runtime's start bound.
TEST(ExtensionHostManagerTest, ARuntimeExtensionWhoseImportOutlastsTheHandshakeLaunchesAndExecutes)
{
    const std::filesystem::path host_path = extensionHostPathForCurrentAppDir();
    const std::filesystem::path runtime_root = runtimeRootForCurrentAppDir();
    const std::filesystem::path bundled_python = repositoryBundledPythonPath();
    AN_REQUIRE_FULL_EXTENSION_HOST(host_path);
    ASSERT_TRUE(std::filesystem::exists(runtime_root)) << runtime_root;
    ASSERT_FALSE(bundled_python.empty()) << "Repository bundled Python runtime must be available";

    const auto temp_root = uniqueExtensionRuntimeRootForTest("slow-import");
    removeAllNoThrow(temp_root);
    const RemovePathOnExit remove_temp_root{temp_root};
    std::filesystem::create_directories(temp_root);

    const auto env_root = temp_root / "env";
    const auto package_root = temp_root / "slow_import_ext";
    std::filesystem::create_directories(package_root);
    createVirtualEnvironment(bundled_python, env_root);
    if (!provisionBundledRuntimePackageIntoEnvironment(runtime_root, env_root, "numpy"))
        GTEST_SKIP() << "numpy not available in bundled runtime";

    an::ui::extensions::ExtensionHostLaunchRequest request;
    constexpr int kImportSeconds = 7;
    ASSERT_GT(kImportSeconds * 1000, request.handshake_timeout_ms);
    {
        std::ofstream out(package_root / "slow_import_ext.py", std::ios::binary);
        ASSERT_TRUE(out.is_open());
        out << "import time\n"
            << "import afternight\n"
            << "import numpy as np\n\n"
            << "time.sleep(" << kImportSeconds << ")\n\n"
            << "class SlowImportExtension(afternight.ProcessExtension):\n"
            << "    def execute(self, target, src_image, dst_image, params, progress, masks=None, weights=None, "
               "output_masks=None):\n"
            << "        dst_image.from_numpy(src_image.to_numpy() + 1)\n";
    }

    an::ui::extensions::ExtensionHostManager manager;
    request.extension_id = "an.demo.slow.import";
    request.sdk_backend = "runtime";
    request.host_executable = host_path;
    request.capabilities = {"execute"};
    request.host_arguments = {
        "--extension-package-root", package_root.string(), "--entry-point",
        "slow_import_ext",          "--process-class",     "SlowImportExtension",
        "--environment-root",       env_root.string(),     "--runtime-root",
        runtime_root.string(),      "--stay-alive-ms",     std::to_string(5000 * kIpcTimingMultiplier)};

    auto prepared = manager.prepareSession(request);
    an::ui::extensions::ExtensionHostRunningSession running_session;
    std::string error;
    ASSERT_TRUE(manager.launchSession(prepared, &running_session, &error)) << error;
    SessionGuard session_guard{manager, running_session};
    EXPECT_TRUE(running_session.runtime_ready);

    const auto root = uniqueSharedBufferRootForTest("slow-import-execute");
    removeAllNoThrow(root);
    const RemovePathOnExit remove_root{root};
    auto source_buffer = makeSourceBuffer(root, running_session.prepared_session.session_id, &error);
    ASSERT_TRUE(source_buffer.valid()) << error;

    std::string request_id;
    ASSERT_TRUE(manager.sendExecuteRequest(&running_session, "slow-import-token", {source_buffer.descriptor()},
                                           &request_id, &error))
        << error;
    ASSERT_TRUE(pumpUntilRequestCompletes(manager, &running_session, 3000 * kIpcTimingMultiplier, &error)) << error;
    ASSERT_TRUE(running_session.last_view_commit.has_value());
    ASSERT_EQ(running_session.last_view_commit->result_buffers.size(), 1u);

    an::ui::extensions::SharedBufferManager shared_buffer_manager(root);
    an::ui::extensions::SharedImageBuffer output_buffer;
    ASSERT_TRUE(openBufferOrReport(shared_buffer_manager, running_session.last_view_commit->result_buffers.front(),
                                   &output_buffer, &error))
        << error;
    const auto* src_bytes = reinterpret_cast<const std::uint8_t*>(source_buffer.bytes());
    const auto* out_bytes = reinterpret_cast<const std::uint8_t*>(output_buffer.bytes());
    ASSERT_NE(src_bytes, nullptr);
    ASSERT_NE(out_bytes, nullptr);
    for (std::uint64_t i = 0; i < source_buffer.mappedSize(); ++i)
    {
        EXPECT_EQ(out_bytes[i], static_cast<std::uint8_t>(src_bytes[i] + 1u));
    }
}

// An extension's constructor can queue work on the host's event loop - the standalone author template hides its process
// window from a zero-delay timer - and the host constructs it before it has taken its bootstrap state, whose transfer
// runs that loop. The work reaches the host there as it does from the session's own loop: its log line arrives, and a
// hide the same timer asked for would hide the window.
TEST(ExtensionHostManagerTest, WorkAnExtensionsConstructorQueuesOnTheHostsLoopReachesTheHostDuringItsBootstrap)
{
#if defined(_MSC_VER) && !defined(NDEBUG)
    GTEST_SKIP() << "PySide6 wheels target the release Qt runtime and cannot attach to an MSVC debug QApplication";
#endif
    const std::filesystem::path host_path = extensionHostPathForCurrentAppDir();
    const std::filesystem::path runtime_root = runtimeRootForCurrentAppDir();
    const std::filesystem::path bundled_python = repositoryBundledPythonPath();
    AN_REQUIRE_FULL_EXTENSION_HOST(host_path);
    ASSERT_TRUE(std::filesystem::exists(runtime_root)) << runtime_root;
    ASSERT_FALSE(bundled_python.empty()) << "Repository bundled Python runtime must be available";

    const auto temp_root = uniqueExtensionRuntimeRootForTest("constructor-timer");
    removeAllNoThrow(temp_root);
    const RemovePathOnExit remove_temp_root{temp_root};
    std::filesystem::create_directories(temp_root);

    const auto env_root = temp_root / "env";
    const auto package_root = temp_root / "constructor_timer_ext";
    std::filesystem::create_directories(package_root);
    createVirtualEnvironment(bundled_python, env_root);
    if (!provisionBundledRuntimePackageIntoEnvironment(runtime_root, env_root, "numpy") ||
        !provisionBundledRuntimePackageIntoEnvironment(runtime_root, env_root, "PySide6") ||
        !provisionBundledRuntimePackageIntoEnvironment(runtime_root, env_root, "pyside6") ||
        !provisionBundledRuntimePackageIntoEnvironment(runtime_root, env_root, "shiboken6"))
    {
        GTEST_SKIP() << "numpy and PySide6 runtime artifacts are not available in the bundled runtime";
    }

    const std::string token = "constructor-timer-" + std::to_string(QCoreApplication::applicationPid());
    {
        std::ofstream out(package_root / "constructor_timer_ext.py", std::ios::binary);
        ASSERT_TRUE(out.is_open());
        out << "import afternight\n"
            << "from PySide6 import QtCore\n\n"
            << "class ConstructorTimerExtension(afternight.ProcessExtension):\n"
            << "    def __init__(self, context):\n"
            << "        super().__init__(context)\n"
            << "        QtCore.QTimer.singleShot(0, self._first)\n\n"
            << "    def _first(self):\n"
            << "        afternight.log_info('" << token << "', component='constructor.timer')\n\n"
            << "    def execute(self, target, src_image, dst_image, params, progress, masks=None, weights=None, "
               "output_masks=None):\n"
            << "        dst_image.from_numpy(src_image.to_numpy())\n";
    }

    InMemorySettings settings_store;
    GlobalSettingsPersistence& settings = settings_store.repository();
    an::ui::extensions::ExtensionSdkService sdk_service(&settings);
    an::ui::extensions::ExtensionHostManager manager;
    manager.setSdkService(&sdk_service);

    an::ui::extensions::ExtensionHostLaunchRequest request;
    request.extension_id = "an.demo.constructor.timer";
    request.sdk_backend = "runtime";
    request.host_executable = host_path;
    request.capabilities = {"execute"};
    request.defer_runtime_bootstrap = true;
    request.host_arguments = {
        "--extension-package-root", package_root.string(), "--entry-point",
        "constructor_timer_ext",    "--process-class",     "ConstructorTimerExtension",
        "--environment-root",       env_root.string(),     "--runtime-root",
        runtime_root.string(),      "--stay-alive-ms",     std::to_string(5000 * kIpcTimingMultiplier)};

    auto prepared = manager.prepareSession(request);
    an::ui::extensions::ExtensionHostRunningSession running_session;
    std::string error;
    ASSERT_TRUE(manager.launchSession(prepared, &running_session, &error)) << error;
    SessionGuard session_guard{manager, running_session};

    const auto logged = [&sdk_service, &token]() {
        const auto& events = sdk_service.recentLogEvents();
        return std::any_of(events.begin(), events.end(), [&token](const auto& event) {
            return event.message.find(token) != std::string::npos && event.component == "constructor.timer";
        });
    };
    QElapsedTimer timer;
    timer.start();
    while (!logged() && timer.elapsed() < 3000 * kIpcTimingMultiplier)
    {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 1);
        ASSERT_TRUE(manager.pumpSession(&running_session, 20, &error)) << error;
    }
    EXPECT_TRUE(logged()) << "the constructor's zero-delay timer did not reach the host";
}

// An execute the application sends as soon as a launch returns - as a transient generator's or a workflow's launch does
// - can reach the host while the extension's launch hook waits for the application to take its log line. The host
// keeps it for its session loop rather than passing over it as not the answer it waits for, so it runs and lands.
TEST(ExtensionHostManagerTest, AnExecuteThatArrivesWhileTheLaunchHookWaitsForTheApplicationRunsAndLands)
{
    const std::filesystem::path host_path = extensionHostPathForCurrentAppDir();
    const std::filesystem::path runtime_root = runtimeRootForCurrentAppDir();
    const std::filesystem::path bundled_python = repositoryBundledPythonPath();
    AN_REQUIRE_FULL_EXTENSION_HOST(host_path);
    ASSERT_TRUE(std::filesystem::exists(runtime_root)) << runtime_root;
    ASSERT_FALSE(bundled_python.empty()) << "Repository bundled Python runtime must be available";

    const auto temp_root = uniqueExtensionRuntimeRootForTest("launch-logging");
    removeAllNoThrow(temp_root);
    const RemovePathOnExit remove_temp_root{temp_root};
    std::filesystem::create_directories(temp_root);

    const auto env_root = temp_root / "env";
    const auto package_root = temp_root / "launch_logging_ext";
    std::filesystem::create_directories(package_root);
    createVirtualEnvironment(bundled_python, env_root);
    if (!provisionBundledRuntimePackageIntoEnvironment(runtime_root, env_root, "numpy"))
        GTEST_SKIP() << "numpy not available in bundled runtime";

    {
        std::ofstream out(package_root / "launch_logging_ext.py", std::ios::binary);
        ASSERT_TRUE(out.is_open());
        out << "import afternight\n\n"
            << "class LaunchLoggingExtension(afternight.ProcessExtension):\n"
            << "    def on_process_launch(self):\n"
            << "        afternight.log_info('launched', component='launch.logging')\n"
            << "        afternight.log_info('ready', component='launch.logging')\n\n"
            << "    def execute(self, target, src_image, dst_image, params, progress, masks=None, weights=None, "
               "output_masks=None):\n"
            << "        dst_image.from_numpy(src_image.to_numpy() + 1)\n";
    }

    an::ui::extensions::ExtensionHostManager manager;
    an::ui::extensions::ExtensionHostLaunchRequest request;
    request.extension_id = "an.demo.launch.logging";
    request.sdk_backend = "runtime";
    request.host_executable = host_path;
    request.capabilities = {"execute"};
    request.host_arguments = {
        "--extension-package-root", package_root.string(), "--entry-point",
        "launch_logging_ext",       "--process-class",     "LaunchLoggingExtension",
        "--environment-root",       env_root.string(),     "--runtime-root",
        runtime_root.string(),      "--stay-alive-ms",     std::to_string(10000 * kIpcTimingMultiplier)};

    auto prepared = manager.prepareSession(request);
    an::ui::extensions::ExtensionHostRunningSession running_session;
    std::string error;
    ASSERT_TRUE(manager.launchSession(prepared, &running_session, &error)) << error;
    SessionGuard session_guard{manager, running_session};

    const auto root = uniqueSharedBufferRootForTest("launch-logging-execute");
    removeAllNoThrow(root);
    const RemovePathOnExit remove_root{root};
    auto source_buffer = makeSourceBuffer(root, running_session.prepared_session.session_id, &error);
    ASSERT_TRUE(source_buffer.valid()) << error;

    // Sent before anything has answered the launch hook's log line.
    std::string request_id;
    ASSERT_TRUE(manager.sendExecuteRequest(&running_session, "launch-logging-token", {source_buffer.descriptor()},
                                           &request_id, &error))
        << error;
    ASSERT_TRUE(pumpUntilRequestCompletes(manager, &running_session, 5000 * kIpcTimingMultiplier, &error))
        << "the execute sent during the launch hook never completed: " << error;
    ASSERT_TRUE(running_session.last_view_commit.has_value());
    ASSERT_EQ(running_session.last_view_commit->request_id, request_id);
    ASSERT_EQ(running_session.last_view_commit->result_buffers.size(), 1u);

    an::ui::extensions::SharedBufferManager shared_buffer_manager(root);
    an::ui::extensions::SharedImageBuffer output_buffer;
    ASSERT_TRUE(openBufferOrReport(shared_buffer_manager, running_session.last_view_commit->result_buffers.front(),
                                   &output_buffer, &error))
        << error;
    const auto* src_bytes = reinterpret_cast<const std::uint8_t*>(source_buffer.bytes());
    const auto* out_bytes = reinterpret_cast<const std::uint8_t*>(output_buffer.bytes());
    ASSERT_NE(src_bytes, nullptr);
    ASSERT_NE(out_bytes, nullptr);
    for (std::uint64_t i = 0; i < source_buffer.mappedSize(); ++i)
    {
        EXPECT_EQ(out_bytes[i], static_cast<std::uint8_t>(src_bytes[i] + 1u));
    }
}

// A host that exits before it connects is reported at once, with what it printed, and not after the wait for its
// connection - which, for a host that starts its runtime first, is as long as a start may take.
TEST(ExtensionHostManagerTest, AHostThatExitsBeforeItConnectsIsReportedWithWhatItPrinted)
{
    const std::filesystem::path host_path = extensionHostPathForCurrentAppDir();
    const std::filesystem::path runtime_root = runtimeRootForCurrentAppDir();
    AN_REQUIRE_FULL_EXTENSION_HOST(host_path);

    const auto temp_root = uniqueExtensionRuntimeRootForTest("exits-before-connecting");
    removeAllNoThrow(temp_root);
    const RemovePathOnExit remove_temp_root{temp_root};

    an::ui::extensions::ExtensionHostManager manager;
    an::ui::extensions::ExtensionHostLaunchRequest request;
    request.extension_id = "an.demo.exits.early";
    request.sdk_backend = "runtime";
    request.host_executable = host_path;
    request.capabilities = {"execute"};
    request.host_arguments = {"--environment-root", (temp_root / "missing-environment").string(),
                              "--runtime-root",     runtime_root.string(),
                              "--stay-alive-ms",    "5000"};

    an::ui::extensions::ExtensionHostRunningSession running_session;
    std::string error;
    EXPECT_FALSE(manager.launchSession(manager.prepareSession(request), &running_session, &error));
    EXPECT_NE(error.find("exited before it connected"), std::string::npos) << error;
    EXPECT_NE(error.find("Extension environment root does not exist"), std::string::npos) << error;
#ifdef _WIN32
    EXPECT_EQ(running_session.process_job_handle, nullptr) << "the failed launch kept its host's job handle";
#endif
}

// A launch refused after its host started - here at the handshake, the host speaking another protocol version - lets
// its host go and keeps nothing of it: on Windows, not the job the host was assigned to.
TEST(ExtensionHostManagerTest, ALaunchRefusedAfterItsHostStartedKeepsNoJobHandle)
{
    const std::filesystem::path host_path = extensionHostPathForCurrentAppDir();
    AN_REQUIRE_FULL_EXTENSION_HOST(host_path);

    an::ui::extensions::ExtensionHostManager manager;
    const auto session = manager.prepareSession(makeLaunchRequest(
        host_path, {"--hello-protocol-version", std::to_string(an::ui::extensions::kExtensionProtocolVersion + 1)}));

    an::ui::extensions::ExtensionHostRunningSession running_session;
    std::string error;
    EXPECT_FALSE(manager.launchSession(session, &running_session, &error));
    EXPECT_NE(error.find("protocol version"), std::string::npos) << error;
#ifdef _WIN32
    EXPECT_EQ(running_session.process_job_handle, nullptr) << "the refused launch kept its host's job handle";
#endif
}

// Where local sockets are files, an endpoint's name is resolved under the temporary directory and the whole path has
// to fit a socket address. Under a directory where it fits the name stays as it is; under one too deep for it the
// endpoint is its path under the first short directory where that fits; and a name that fits nowhere is left for the
// listen to refuse.
TEST(ExtensionHostManagerTest, AnEndpointUnderATemporaryDirectoryTooDeepForASocketAddressMovesWhereItFits)
{
    using an::ui::extensions::kLocalSocketPathLimit;
    using an::ui::extensions::localEndpointName;
    const std::string name = "AfterNightExtensionHost_" + std::string(36u, 'a');
    const std::string deep = "/home/someone/a-scratch-directory/" + std::string(30u, 'd');
    ASSERT_GT(deep.size() + 1u + name.size(), kLocalSocketPathLimit);

    EXPECT_EQ(localEndpointName(name, "/tmp", {"/run/user/1000", "/tmp"}), name);
    EXPECT_EQ(localEndpointName(name, deep, {"/run/user/1000", "/tmp"}), "/run/user/1000/" + name);
    EXPECT_EQ(localEndpointName(name, deep, {"/" + std::string(60u, 'r'), "/tmp"}), "/tmp/" + name)
        << "a short directory too deep itself is passed over";
    EXPECT_EQ(localEndpointName(name, deep + "/", {"/tmp/"}), "/tmp/" + name);
    EXPECT_EQ(localEndpointName(name, deep, {}), name);
    for (const std::string& directory : {std::string("/run/user/1000"), std::string("/tmp")})
    {
        EXPECT_LE(localEndpointName(name, deep, {directory}).size(), kLocalSocketPathLimit);
    }
}

#ifndef _WIN32
// A session prepared under a temporary directory too deep for a socket address still listens: the launch failed with a
// name error under any directory longer than forty-six characters.
TEST(ExtensionHostManagerTest, ASessionPreparedUnderADeepTemporaryDirectoryListens)
{
    QTemporaryDir parent;
    ASSERT_TRUE(parent.isValid());
    const QString deep = parent.filePath(QStringLiteral("a-temporary-directory-deeper-than-a-socket-address-holds"));
    ASSERT_TRUE(QDir().mkpath(deep));
    const ScopedEnvironmentVariable temporary("TMPDIR", QFile::encodeName(deep));
    ASSERT_EQ(QDir::tempPath(), QDir::cleanPath(deep));

    an::ui::extensions::ExtensionHostManager manager;
    const auto session = manager.prepareSession(makeLaunchRequest("/nonexistent/extension-host"));
    QLocalServer server;
    server.setSocketOptions(QLocalServer::UserAccessOption);
    EXPECT_TRUE(server.listen(QString::fromStdString(session.endpoint_name)))
        << session.endpoint_name << ": " << server.errorString().toStdString();
    server.close();
}
#endif

// A Python extension's logging, warnings and exceptions reach the SDK's records and the application's logger.
TEST(ExtensionHostManagerTest, RealPythonExtensionExecuteRoutesPythonLoggingAndExceptionsToSdkAndMainLogger)
{
    const std::filesystem::path host_path = extensionHostPathForCurrentAppDir();
    const std::filesystem::path runtime_root = runtimeRootForCurrentAppDir();
    const std::filesystem::path bundled_python = repositoryBundledPythonPath();
    AN_REQUIRE_FULL_EXTENSION_HOST(host_path);
    ASSERT_TRUE(std::filesystem::exists(runtime_root)) << runtime_root;
    ASSERT_FALSE(bundled_python.empty()) << "Repository bundled Python runtime must be available";

    const std::string token = an::ui::extensions::generateOpaqueToken();
    const std::string stdlib_warning = "python-logging-warning-" + token;
    const std::string captured_warning = "python-warning-" + token;
    const std::string afternight_info = "afternight-info-" + token;
    const std::string exception_text = "python-exception-" + token;

    const auto temp_root = uniqueExtensionRuntimeRootForTest("python-runtime-logging");
    removeAllNoThrow(temp_root);
    const RemovePathOnExit remove_temp_root{temp_root};
    std::filesystem::create_directories(temp_root);

    const auto env_root = temp_root / "env";
    const auto package_root = temp_root / "runtime_logging_ext";
    std::filesystem::create_directories(package_root);
    createVirtualEnvironment(bundled_python, env_root);
    if (!provisionBundledRuntimePackageIntoEnvironment(runtime_root, env_root, "numpy"))
        GTEST_SKIP() << "numpy not available in bundled runtime";

    {
        std::ofstream out(package_root / "runtime_logging_ext.py", std::ios::binary);
        ASSERT_TRUE(out.is_open());
        out << "import logging\n"
            << "import warnings\n"
            << "import afternight\n\n"
            << "class RuntimeLoggingExtension(afternight.ProcessExtension):\n"
            << "    def execute(self, target, src_image, dst_image, params, progress, masks=None, weights=None, "
               "output_masks=None):\n"
            << "        logging.getLogger('runtime.logging.stdlib').warning('" << stdlib_warning << "')\n"
            << "        warnings.warn('" << captured_warning << "', RuntimeWarning)\n"
            << "        afternight.log_info('" << afternight_info << "', component='runtime.logging.afternight')\n"
            << "        raise RuntimeError('" << exception_text << "')\n";
    }

    InMemorySettings settings_store;
    GlobalSettingsPersistence& settings = settings_store.repository();
    an::ui::extensions::ExtensionSdkService sdk_service(&settings);
    an::ui::extensions::ExtensionHostManager manager;
    manager.setSdkService(&sdk_service);

    an::ui::extensions::ExtensionHostLaunchRequest request;
    request.extension_id = "an.demo.runtime.logging";
    request.sdk_backend = "runtime";
    request.host_executable = host_path;
    request.capabilities = {"execute"};
    request.host_arguments = {
        "--extension-package-root", package_root.string(), "--entry-point",
        "runtime_logging_ext",      "--process-class",     "RuntimeLoggingExtension",
        "--environment-root",       env_root.string(),     "--runtime-root",
        runtime_root.string(),      "--stay-alive-ms",     std::to_string(5000 * kIpcTimingMultiplier)};

    auto prepared = manager.prepareSession(request);
    an::ui::extensions::ExtensionHostRunningSession running_session;
    std::string error;
    ASSERT_TRUE(manager.launchSession(prepared, &running_session, &error)) << error;
    SessionGuard session_guard{manager, running_session};

    const auto root = uniqueSharedBufferRootForTest("python-runtime-logging");
    removeAllNoThrow(root);
    const RemovePathOnExit remove_root{root};
    auto source_buffer = makeSourceBuffer(root, running_session.prepared_session.session_id, &error);
    ASSERT_TRUE(source_buffer.valid()) << error;

    std::string request_id;
    ASSERT_TRUE(manager.sendExecuteRequest(&running_session, "exec-logging-token", {source_buffer.descriptor()},
                                           &request_id, &error))
        << error;
    ASSERT_FALSE(request_id.empty());

    EXPECT_FALSE(pumpUntilRequestCompletes(manager, &running_session, 3000 * kIpcTimingMultiplier, &error));
    EXPECT_EQ(running_session.state, an::ui::extensions::ExtensionHostSessionState::Failed);
    EXPECT_FALSE(error.empty());

    const auto& events = sdk_service.recentLogEvents();
    auto contains_message = [&](const std::string& fragment, const std::string& component = std::string()) {
        return std::any_of(events.begin(), events.end(), [&](const auto& event) {
            return event.message.find(fragment) != std::string::npos &&
                   (component.empty() || event.component == component);
        });
    };

    EXPECT_TRUE(contains_message(stdlib_warning, "runtime.logging.stdlib"));
    EXPECT_TRUE(contains_message(captured_warning, "py.warnings"));
    EXPECT_TRUE(contains_message(afternight_info, "runtime.logging.afternight"));
    EXPECT_TRUE(contains_message(exception_text, "python.exception"));

    // The same records reach the application's own logger.
    EXPECT_TRUE(mainLogContains(stdlib_warning));
    EXPECT_TRUE(mainLogContains(captured_warning));
    EXPECT_TRUE(mainLogContains(afternight_info));
    EXPECT_TRUE(mainLogContains(exception_text));

    removeAllNoThrow(root);
    removeAllNoThrow(temp_root);
}

// A stop the person asked for reaches the extension's code as KeyboardInterrupt, which is how the host stops it: it is
// logged once at info level as the stop it is, and never as an error carrying the interrupted line's traceback.
TEST(ExtensionHostManagerTest, AStopTheProcessWindowAskedForIsNotLoggedAsAnError)
{
    const std::filesystem::path host_path = extensionHostPathForCurrentAppDir();
    const std::filesystem::path runtime_root = runtimeRootForCurrentAppDir();
    const std::filesystem::path bundled_python = repositoryBundledPythonPath();
    AN_REQUIRE_FULL_EXTENSION_HOST(host_path);
    ASSERT_TRUE(std::filesystem::exists(runtime_root)) << runtime_root;
    ASSERT_FALSE(bundled_python.empty()) << "Repository bundled Python runtime must be available";

    const auto temp_root = uniqueExtensionRuntimeRootForTest("python-requested-stop");
    removeAllNoThrow(temp_root);
    const RemovePathOnExit remove_temp_root{temp_root};
    std::filesystem::create_directories(temp_root);
    const auto env_root = temp_root / "env";
    const auto package_root = temp_root / "requested_stop_ext";
    std::filesystem::create_directories(package_root);
    createVirtualEnvironment(bundled_python, env_root);
    writeTextFile(package_root / "requested_stop_ext.py",
                  "import time\n"
                  "import afternight\n\n"
                  "class RequestedStopExtension(afternight.ProcessExtension):\n"
                  "    def execute(self, target, src_image, dst_image, params, progress, masks=None, weights=None, "
                  "output_masks=None):\n"
                  "        # Never asks whether it was cancelled: only the host's interrupt stops it.\n"
                  "        for _ in range(1000):\n"
                  "            time.sleep(0.01)\n");

    InMemorySettings settings_store;
    GlobalSettingsPersistence& settings = settings_store.repository();
    an::ui::extensions::ExtensionSdkService sdk_service(&settings);
    an::ui::extensions::ExtensionHostManager manager;
    manager.setSdkService(&sdk_service);

    an::ui::extensions::ExtensionHostLaunchRequest request;
    request.extension_id = "an.demo.requested_stop";
    request.sdk_backend = "runtime";
    request.host_executable = host_path;
    request.capabilities = {"execute"};
    request.host_arguments = {"--extension-package-root",
                              package_root.string(),
                              "--entry-point",
                              "requested_stop_ext",
                              "--process-class",
                              "RequestedStopExtension",
                              "--environment-root",
                              env_root.string(),
                              "--runtime-root",
                              runtime_root.string(),
                              "--interrupt-execute-after-ms",
                              "300",
                              "--stay-alive-ms",
                              std::to_string(5000 * kIpcTimingMultiplier)};

    auto prepared = manager.prepareSession(request);
    an::ui::extensions::ExtensionHostRunningSession running_session;
    std::string error;
    ASSERT_TRUE(manager.launchSession(prepared, &running_session, &error)) << error;
    SessionGuard session_guard{manager, running_session};

    const auto root = uniqueSharedBufferRootForTest("python-requested-stop");
    removeAllNoThrow(root);
    const RemovePathOnExit remove_root{root};
    auto source_buffer = makeSourceBuffer(root, running_session.prepared_session.session_id, &error);
    ASSERT_TRUE(source_buffer.valid()) << error;
    std::string request_id;
    ASSERT_TRUE(manager.sendExecuteRequest(&running_session, "exec-stop-token", {source_buffer.descriptor()},
                                           &request_id, &error))
        << error;

    QElapsedTimer timer;
    timer.start();
    const auto& events = sdk_service.recentLogEvents();
    const auto logged = [&events](const std::string& component) {
        return std::any_of(events.begin(), events.end(),
                           [&component](const auto& event) { return event.component == component; });
    };
    while (timer.elapsed() < 5000 * kIpcTimingMultiplier && !logged("python.cancelled") && !logged("python.exception"))
    {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 1);
        static_cast<void>(manager.pumpSession(&running_session, 20, &error));
        QThread::msleep(20);
    }
    // What comes after the stop - the result and the session's end - is pumped through too.
    for (int pump = 0; pump < 10; ++pump)
    {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 1);
        static_cast<void>(manager.pumpSession(&running_session, 20, &error));
    }

    EXPECT_TRUE(logged("python.cancelled")) << "the stop is said once";
    for (const auto& event : events)
    {
        EXPECT_FALSE(event.component == "python.exception")
            << event.severity << " " << event.component << ": " << event.message;
        EXPECT_EQ(event.message.find("KeyboardInterrupt"), std::string::npos)
            << event.severity << " " << event.component << ": " << event.message;
        if (event.component == "python.cancelled")
        {
            EXPECT_EQ(event.severity, "info");
        }
    }
    removeAllNoThrow(root);
    removeAllNoThrow(temp_root);
}

// An image a Python extension generates during an execute is handed to the application to open.
TEST(ExtensionHostManagerTest, RealPythonExtensionExecuteSupportsUiOpenImageGeneratedBuffers)
{
    const std::filesystem::path host_path = extensionHostPathForCurrentAppDir();
    const std::filesystem::path runtime_root = runtimeRootForCurrentAppDir();
    const std::filesystem::path bundled_python = repositoryBundledPythonPath();
    AN_REQUIRE_FULL_EXTENSION_HOST(host_path);
    ASSERT_TRUE(std::filesystem::exists(runtime_root)) << runtime_root;
    ASSERT_FALSE(bundled_python.empty()) << "Repository bundled Python runtime must be available";

    const auto temp_root = uniqueExtensionRuntimeRootForTest("ui-open-image");
    removeAllNoThrow(temp_root);
    const RemovePathOnExit remove_temp_root{temp_root};
    std::filesystem::create_directories(temp_root);

    const auto env_root = temp_root / "env";
    const auto package_root = temp_root / "runtime_open_image_ext";
    std::filesystem::create_directories(package_root);
    createVirtualEnvironment(bundled_python, env_root);
    if (!provisionBundledRuntimePackageIntoEnvironment(runtime_root, env_root, "numpy"))
        GTEST_SKIP() << "numpy not available in bundled runtime";

    {
        std::ofstream out(package_root / "runtime_open_image_ext.py", std::ios::binary);
        ASSERT_TRUE(out.is_open());
        out << "import afternight\n"
            << "import afternight.ui as ui\n"
            << "import numpy as np\n\n"
            << "class RuntimeOpenImageExtension(afternight.ProcessExtension):\n"
            << "    def execute(self, target, src_image, dst_image, params, progress, masks=None, weights=None, "
               "output_masks=None):\n"
            << "        source = src_image.to_numpy()\n"
            << "        generated = source + np.uint8(7)\n"
            << "        assert ui.open_image(generated, title='Generated Open Image', metadata={'STEP': "
               "'ui.open_image'})\n"
            << "        dst_image.from_numpy(source)\n";
    }

    InMemorySettings settings_store;
    GlobalSettingsPersistence& settings = settings_store.repository();
    an::ui::extensions::ExtensionSdkService sdk_service(&settings);
    QString opened_title;
    std::optional<an::consumer::ImageAsset> opened_image;
    sdk_service.setUiOpenImageHandler([&](const std::string_view, const std::string_view,
                                          const an::ui::extensions::SharedBufferDescriptor& buffer,
                                          const QString& title, std::string* handler_error) -> bool {
        an::ui::extensions::SharedBufferManager shared_buffer_manager(
            an::ui::extensions::extensionHostManagedRootPath());
        if (!importBufferOrReport(shared_buffer_manager, buffer, &opened_image, handler_error))
        {
            return false;
        }

        opened_title = title;
        return true;
    });

    an::ui::extensions::ExtensionHostManager manager;
    manager.setSdkService(&sdk_service);

    an::ui::extensions::ExtensionHostLaunchRequest request;
    request.extension_id = "an.demo.runtime.open_image";
    request.sdk_backend = "runtime";
    request.host_executable = host_path;
    request.capabilities = {"execute"};
    request.host_arguments = {
        "--extension-package-root", package_root.string(), "--entry-point",
        "runtime_open_image_ext",   "--process-class",     "RuntimeOpenImageExtension",
        "--environment-root",       env_root.string(),     "--runtime-root",
        runtime_root.string(),      "--stay-alive-ms",     std::to_string(5000 * kIpcTimingMultiplier)};

    std::string session_id;
    const auto source_root = uniqueSharedBufferRootForTest("ui-open-image-source");
    removeAllNoThrow(source_root);
    const RemovePathOnExit remove_source_root{source_root};
    std::optional<an::consumer::ImageAsset> source_image;

    {
        auto prepared = manager.prepareSession(request);
        an::ui::extensions::ExtensionHostRunningSession running_session;
        std::string error;
        ASSERT_TRUE(manager.launchSession(prepared, &running_session, &error)) << error;
        SessionGuard session_guard{manager, running_session};
        session_id = running_session.prepared_session.session_id;

        auto source_buffer = makeSourceBuffer(source_root, session_id, &error);
        ASSERT_TRUE(source_buffer.valid()) << error;

        an::ui::extensions::SharedBufferManager source_manager(source_root);
        ASSERT_TRUE(importBufferOrReport(source_manager, source_buffer.descriptor(), &source_image, &error)) << error;
        ASSERT_TRUE(source_image);

        std::string request_id;
        ASSERT_TRUE(manager.sendExecuteRequest(&running_session, "exec-open-image-token", {source_buffer.descriptor()},
                                               &request_id, &error))
            << error;
        ASSERT_FALSE(request_id.empty());

        ASSERT_TRUE(pumpUntilRequestCompletes(manager, &running_session, 3000 * kIpcTimingMultiplier, &error)) << error;
    }

    ASSERT_TRUE(source_image);
    ASSERT_TRUE(opened_image);
    EXPECT_EQ(opened_title, QStringLiteral("Generated Open Image"));
    EXPECT_EQ(widthOf(*opened_image), widthOf(*source_image));
    EXPECT_EQ(heightOf(*opened_image), heightOf(*source_image));

    for (std::size_t y = 0; y < heightOf(*source_image); ++y)
    {
        for (std::size_t x = 0; x < widthOf(*source_image); ++x)
        {
            EXPECT_NEAR(sampleAt(*opened_image, x, y, 0), sampleAt(*source_image, x, y, 0) + 7.0, 1e-6)
                << "generated image mismatch at x=" << x << " y=" << y;
        }
    }

    removeAllNoThrow(an::ui::extensions::extensionHostSessionRootPath(session_id));
    removeAllNoThrow(source_root);
    removeAllNoThrow(temp_root);
}

// A large colour Float32 image crosses to a Python extension as an array and back, transformed, within budget.
TEST(ExtensionHostManagerTest, RealPythonExtensionImageHandleLargeNumpyRoundTripPreservesRgbFloatPixels)
{
    const std::filesystem::path host_path = extensionHostPathForCurrentAppDir();
    const std::filesystem::path runtime_root = runtimeRootForCurrentAppDir();
    const std::filesystem::path bundled_python = repositoryBundledPythonPath();
    AN_REQUIRE_FULL_EXTENSION_HOST(host_path);
    ASSERT_TRUE(std::filesystem::exists(runtime_root)) << runtime_root;
    ASSERT_FALSE(bundled_python.empty()) << "Repository bundled Python runtime must be available";

    const auto temp_root = uniqueExtensionRuntimeRootForTest("large-numpy-roundtrip");
    removeAllNoThrow(temp_root);
    const RemovePathOnExit remove_temp_root{temp_root};
    std::filesystem::create_directories(temp_root);

    const auto env_root = temp_root / "env";
    const auto package_root = temp_root / "large_numpy_roundtrip_ext";
    std::filesystem::create_directories(package_root);
    createVirtualEnvironment(bundled_python, env_root);
    if (!provisionBundledRuntimePackageIntoEnvironment(runtime_root, env_root, "numpy"))
        GTEST_SKIP() << "numpy not available in bundled runtime";

    {
        std::ofstream out(package_root / "large_numpy_roundtrip_ext.py", std::ios::binary);
        ASSERT_TRUE(out.is_open());
        out << "import time\n"
            << "import afternight\n"
            << "import numpy as np\n\n"
            << "class LargeNumpyRoundTripExtension(afternight.ProcessExtension):\n"
            << "    def execute(self, target, src_image, dst_image, params, progress, masks=None, weights=None, "
               "output_masks=None):\n"
            << "        start = time.perf_counter()\n"
            << "        source = src_image.to_numpy()\n"
            << "        to_numpy_ms = (time.perf_counter() - start) * 1000.0\n"
            << "        assert source.shape == (512, 8192, 3)\n"
            << "        assert source.dtype == np.float32\n"
            << "        assert not source.flags['C_CONTIGUOUS']\n"
            << "        assert abs(float(source[0, 0, 0]) - 0.0) < 1e-6, float(source[0, 0, 0])\n"
            << "        assert abs(float(source[511, 8191, 0]) - 1.0) < 1e-6, float(source[511, 8191, 0])\n"
            << "        assert abs(float(source[511, 8191, 1]) - 1.0) < 1e-6, float(source[511, 8191, 1])\n"
            << "        assert abs(float(source[511, 8191, 2]) - 0.5) < 1e-6, float(source[511, 8191, 2])\n"
            << "        assert abs(float(source[311, 4095, 0]) - 4095.0 / 8191.0) < 1e-6, float(source[311, 4095, 0])\n"
            << "        assert abs(float(source[311, 4095, 1]) - 311.0 / 511.0) < 1e-6, float(source[311, 4095, 1])\n"
            << "        start = time.perf_counter()\n"
            << "        dst_image.from_numpy(source)\n"
            << "        view_import_ms = (time.perf_counter() - start) * 1000.0\n"
            << "        result = np.empty(source.shape, dtype=np.float32)\n"
            << "        result[..., 0] = source[..., 0] * np.float32(0.5) + np.float32(0.125)\n"
            << "        result[..., 1] = source[..., 1] * np.float32(0.25) + np.float32(0.25)\n"
            << "        result[..., 2] = source[..., 2] * np.float32(0.75) + np.float32(0.0625)\n"
            << "        start = time.perf_counter()\n"
            << "        dst_image.from_numpy(result)\n"
            << "        contiguous_import_ms = (time.perf_counter() - start) * 1000.0\n"
            << "        self.settings.set('last_shape', '512x8192x3')\n"
            << "        self.settings.set('last_dtype', str(source.dtype))\n"
            << "        self.settings.set('source_is_noncontiguous', True)\n"
            << "        self.settings.set('last_to_numpy_ms', to_numpy_ms)\n"
            << "        self.settings.set('last_view_import_ms', view_import_ms)\n"
            << "        self.settings.set('last_contiguous_import_ms', contiguous_import_ms)\n";
    }

    InMemorySettings settings_store;
    GlobalSettingsPersistence& settings = settings_store.repository();
    an::ui::extensions::ExtensionSdkService sdk_service(&settings);
    an::ui::extensions::ExtensionHostManager manager;
    manager.setSdkService(&sdk_service);

    an::ui::extensions::ExtensionHostLaunchRequest request;
    request.extension_id = "an.demo.runtime.large_numpy_roundtrip";
    request.sdk_backend = "runtime";
    request.host_executable = host_path;
    request.capabilities = {"execute"};
    request.host_arguments = {
        "--extension-package-root",  package_root.string(), "--entry-point",
        "large_numpy_roundtrip_ext", "--process-class",     "LargeNumpyRoundTripExtension",
        "--environment-root",        env_root.string(),     "--runtime-root",
        runtime_root.string(),       "--stay-alive-ms",     std::to_string(15000 * kIpcTimingMultiplier)};

    auto prepared = manager.prepareSession(request);
    an::ui::extensions::ExtensionHostRunningSession running_session;
    std::string error;
    ASSERT_TRUE(manager.launchSession(prepared, &running_session, &error)) << error;
    SessionGuard session_guard{manager, running_session};

    const auto root = uniqueSharedBufferRootForTest("large-numpy-roundtrip-source");
    removeAllNoThrow(root);
    const RemovePathOnExit remove_root{root};

    constexpr uint32_t kWidth = 8192;
    constexpr uint32_t kHeight = 512;
    std::shared_ptr<an::consumer::ImageAsset> source_image;
    auto source_buffer = makeLargeRgbSourceBuffer(root, running_session.prepared_session.session_id, kWidth, kHeight,
                                                  &source_image, &error);
    ASSERT_TRUE(source_buffer.valid()) << error;
    ASSERT_TRUE(source_image);

    std::string request_id;
    ASSERT_TRUE(manager.sendExecuteRequest(&running_session, "large-numpy-roundtrip-token",
                                           {source_buffer.descriptor()}, &request_id, &error))
        << error;
    ASSERT_FALSE(request_id.empty());

    ASSERT_TRUE(pumpUntilRequestCompletes(manager, &running_session, 20000 * kIpcTimingMultiplier, &error)) << error;

    EXPECT_FALSE(running_session.last_preview_result.has_value());
    ASSERT_TRUE(running_session.last_view_commit.has_value());
    ASSERT_EQ(running_session.last_view_commit->result_buffers.size(), 1u);

    an::ui::extensions::SharedBufferManager shared_buffer_manager(root);
    std::optional<an::consumer::ImageAsset> output_image;
    ASSERT_TRUE(importBufferOrReport(shared_buffer_manager, running_session.last_view_commit->result_buffers.front(),
                                     &output_image, &error))
        << error;
    ASSERT_TRUE(output_image);
    ASSERT_EQ(widthOf(*output_image), kWidth);
    ASSERT_EQ(heightOf(*output_image), kHeight);
    ASSERT_EQ(channelsOf(*output_image), 3u);
    ASSERT_TRUE(hasSampleType(*output_image, an::SampleType::Float32));

    const std::array<std::pair<uint32_t, uint32_t>, 5> sample_points = {
        {{0u, 0u}, {1u, 0u}, {kWidth / 2u, kHeight / 2u}, {kWidth - 1u, kHeight - 1u}, {4095u, 311u}}};
    for (const auto& [x, y] : sample_points)
    {
        EXPECT_NEAR(sampleAt(*output_image, x, y, 0), sampleAt(*source_image, x, y, 0) * 0.5 + 0.125, 1e-5)
            << "red transformed sample at " << x << "," << y;
        EXPECT_NEAR(sampleAt(*output_image, x, y, 1), sampleAt(*source_image, x, y, 1) * 0.25 + 0.25, 1e-5)
            << "green transformed sample at " << x << "," << y;
        EXPECT_NEAR(sampleAt(*output_image, x, y, 2), sampleAt(*source_image, x, y, 2) * 0.75 + 0.0625, 1e-5)
            << "blue transformed sample at " << x << "," << y;
    }

    QString shape;
    ASSERT_TRUE(
        sdk_service.selectExtensionSetting("an.demo.runtime.large_numpy_roundtrip", "last_shape", &shape, {}, &error))
        << error;
    EXPECT_EQ(shape, QStringLiteral("512x8192x3"));

    QString dtype;
    ASSERT_TRUE(
        sdk_service.selectExtensionSetting("an.demo.runtime.large_numpy_roundtrip", "last_dtype", &dtype, {}, &error))
        << error;
    EXPECT_EQ(dtype, QStringLiteral("float32"));

    QCborValue noncontiguous_value;
    ASSERT_TRUE(sdk_service.selectExtensionSetting("an.demo.runtime.large_numpy_roundtrip", "source_is_noncontiguous",
                                                   &noncontiguous_value, QCborValue(false), &error))
        << error;
    EXPECT_TRUE(noncontiguous_value.toBool());

    const auto expectTimingWithinBudget = [&](const char* key, const double budget_ms) {
        QCborValue value;
        ASSERT_TRUE(sdk_service.selectExtensionSetting("an.demo.runtime.large_numpy_roundtrip", key, &value,
                                                       QCborValue(-1.0), &error))
            << error;
        ASSERT_TRUE(value.isDouble() || value.isInteger()) << key;
        const double elapsed_ms = value.isDouble() ? value.toDouble() : static_cast<double>(value.toInteger());
        EXPECT_GE(elapsed_ms, 0.0) << key;
        EXPECT_LT(elapsed_ms, budget_ms) << key << "=" << elapsed_ms << "ms";
    };
    expectTimingWithinBudget("last_to_numpy_ms", 1000.0);
    expectTimingWithinBudget("last_view_import_ms", 5000.0);
    expectTimingWithinBudget("last_contiguous_import_ms", 5000.0);

    removeAllNoThrow(root);
    removeAllNoThrow(temp_root);
}

// A Python extension may commit an image of another size when it says so.
TEST(ExtensionHostManagerTest, RealPythonExtensionExecuteSupportsResizeAwareViewCommit)
{
    const std::filesystem::path host_path = extensionHostPathForCurrentAppDir();
    const std::filesystem::path runtime_root = runtimeRootForCurrentAppDir();
    const std::filesystem::path bundled_python = repositoryBundledPythonPath();
    AN_REQUIRE_FULL_EXTENSION_HOST(host_path);
    ASSERT_TRUE(std::filesystem::exists(runtime_root)) << runtime_root;
    ASSERT_FALSE(bundled_python.empty()) << "Repository bundled Python runtime must be available";

    const auto temp_root = uniqueExtensionRuntimeRootForTest("resize-aware-commit");
    removeAllNoThrow(temp_root);
    const RemovePathOnExit remove_temp_root{temp_root};
    std::filesystem::create_directories(temp_root);

    const auto env_root = temp_root / "env";
    const auto package_root = temp_root / "resize_aware_commit_ext";
    std::filesystem::create_directories(package_root);
    createVirtualEnvironment(bundled_python, env_root);
    if (!provisionBundledRuntimePackageIntoEnvironment(runtime_root, env_root, "numpy"))
        GTEST_SKIP() << "numpy not available in bundled runtime";

    {
        std::ofstream out(package_root / "resize_aware_commit_ext.py", std::ios::binary);
        ASSERT_TRUE(out.is_open());
        out << "import afternight\n"
            << "import afternight.ui as ui\n"
            << "import numpy as np\n\n"
            << "class ResizeAwareCommitExtension(afternight.ProcessExtension):\n"
            << "    def execute(self, target, src_image, dst_image, params, progress, masks=None, weights=None, "
               "output_masks=None):\n"
            << "        source = src_image.to_numpy()\n"
            << "        resized = np.zeros((source.shape[0] * 2, source.shape[1] * 2), dtype=source.dtype)\n"
            << "        resized[::2, ::2] = source\n"
            << "        assert ui.commit_image(resized, history_step_name='Resize Commit', metadata={'STEP': "
               "'resize'}, allow_resized_output=True)\n";
    }

    InMemorySettings settings_store;
    GlobalSettingsPersistence& settings = settings_store.repository();
    an::ui::extensions::ExtensionSdkService sdk_service(&settings);
    an::ui::extensions::ExtensionHostManager manager;
    manager.setSdkService(&sdk_service);

    an::ui::extensions::ExtensionHostLaunchRequest request;
    request.extension_id = "an.demo.resize_aware_commit";
    request.sdk_backend = "runtime";
    request.host_executable = host_path;
    request.capabilities = {"execute"};
    request.host_arguments = {
        "--extension-package-root", package_root.string(), "--entry-point",
        "resize_aware_commit_ext",  "--process-class",     "ResizeAwareCommitExtension",
        "--environment-root",       env_root.string(),     "--runtime-root",
        runtime_root.string(),      "--stay-alive-ms",     std::to_string(5000 * kIpcTimingMultiplier)};

    const auto source_root = uniqueSharedBufferRootForTest("resize-aware-commit-source");
    removeAllNoThrow(source_root);
    const RemovePathOnExit remove_source_root{source_root};
    std::string session_id;

    {
        auto prepared = manager.prepareSession(request);
        an::ui::extensions::ExtensionHostRunningSession running_session;
        std::string error;
        ASSERT_TRUE(manager.launchSession(prepared, &running_session, &error)) << error;
        SessionGuard session_guard{manager, running_session};
        session_id = running_session.prepared_session.session_id;

        auto source_buffer = makeSourceBuffer(source_root, session_id, &error);
        ASSERT_TRUE(source_buffer.valid()) << error;

        std::string request_id;
        ASSERT_TRUE(manager.sendExecuteRequest(&running_session, "exec-resize-aware-token",
                                               {source_buffer.descriptor()}, &request_id, &error))
            << error;
        ASSERT_FALSE(request_id.empty());

        ASSERT_TRUE(pumpUntilRequestCompletes(manager, &running_session, 3000 * kIpcTimingMultiplier, &error)) << error;

        ASSERT_TRUE(running_session.last_view_commit.has_value());
        const auto& view_commit = *running_session.last_view_commit;
        EXPECT_TRUE(view_commit.allow_resized_output);
        ASSERT_EQ(view_commit.result_buffers.size(), 1u);

        const auto& descriptor = view_commit.result_buffers.front();
        EXPECT_EQ(descriptor.width, source_buffer.descriptor().width * 2);
        EXPECT_EQ(descriptor.height, source_buffer.descriptor().height * 2);

        an::ui::extensions::SharedBufferManager shared_buffer_manager(
            an::ui::extensions::extensionHostManagedRootPath());
        std::optional<an::consumer::ImageAsset> committed_image;
        ASSERT_TRUE(importBufferOrReport(shared_buffer_manager, descriptor, &committed_image, &error)) << error;
        ASSERT_TRUE(committed_image);
        EXPECT_EQ(static_cast<int>(widthOf(*committed_image)), descriptor.width);
        EXPECT_EQ(static_cast<int>(heightOf(*committed_image)), descriptor.height);
    }

    removeAllNoThrow(an::ui::extensions::extensionHostSessionRootPath(session_id));
    removeAllNoThrow(source_root);
    removeAllNoThrow(temp_root);
}

// An automatic interface executes with its parameters' defaults, leaving out display-only fields.
TEST(ExtensionHostManagerTest, LaunchCoordinatorAutoUiExecutesWithSeededDefaultParams)
{
    const std::filesystem::path host_path = extensionHostPathForCurrentAppDir();
    const std::filesystem::path runtime_root = runtimeRootForCurrentAppDir();
    const std::filesystem::path bundled_python = repositoryBundledPythonPath();
    AN_REQUIRE_FULL_EXTENSION_HOST(host_path);
    ASSERT_TRUE(std::filesystem::exists(runtime_root)) << runtime_root;
    ASSERT_FALSE(bundled_python.empty()) << "Repository bundled Python runtime must be available";

    const auto temp_root = uniqueExtensionRuntimeRootForTest("auto-ui");
    removeAllNoThrow(temp_root);
    const RemovePathOnExit remove_temp_root{temp_root};
    std::filesystem::create_directories(temp_root);

    const auto env_root = temp_root / "env";
    const auto package_root = temp_root / "auto_ui_ext";
    std::filesystem::create_directories(package_root);
    createVirtualEnvironment(bundled_python, env_root);
    if (!provisionBundledRuntimePackageIntoEnvironment(runtime_root, env_root, "numpy"))
        GTEST_SKIP() << "numpy not available in bundled runtime";

    {
        std::ofstream out(package_root / "auto_ui_ext.py", std::ios::binary);
        ASSERT_TRUE(out.is_open());
        out << "import afternight\n"
            << "import numpy as np\n\n"
            << "class AutoUiExecuteExtension(afternight.ProcessExtension):\n"
            << "    def get_params(self):\n"
            << "        return [\n"
            << "            {'id': 'panel_section', 'type': 'section', 'label': 'Preview Panel'},\n"
            << "            {'id': 'panel_summary', 'type': 'info', 'text': 'Display-only helpers should not block "
               "seeded defaults.'},\n"
            << "            {'id': 'auto_calculate', 'type': 'button', 'label': 'Auto Calculate'},\n"
            << "            {'id': 'strength', 'type': 'float', 'label': 'Strength', 'default': 0.5, 'min': 0.0, "
               "'max': 1.0, 'step': 0.05},\n"
            << "            {'id': 'mode', 'type': 'choice', 'label': 'Mode', 'default': 'fast', 'options': [['Fast', "
               "'fast'], ['Quality', 'quality']]},\n"
            << "            {'id': 'use_mask', 'type': 'bool', 'label': 'Use Mask', 'default': False, 'group': "
               "'Advanced'},\n"
            << "            {'id': 'radius', 'type': 'int', 'label': 'Radius', 'default': 3, 'min': 1, 'max': 9, "
               "'group': 'Advanced', 'visible_when': \"mode == 'quality'\", 'enabled_when': 'use_mask == true'}\n"
            << "        ]\n\n"
            << "    def execute(self, target, src_image, dst_image, params, progress, masks=None, weights=None, "
               "output_masks=None):\n"
            << "        self.settings.set('last_params', params)\n"
            << "        self.settings.set('last_token', params.get('_params_token', ''))\n"
            << "        increment = int(params['radius']) if params['mode'] == 'quality' else 1\n"
            << "        dst_image.from_numpy(src_image.to_numpy() + increment)\n";
    }

    const auto source_image = makeRgbGradientImage(5, 4);
    ASSERT_TRUE(source_image);

    InMemorySettings settings_store;
    GlobalSettingsPersistence& settings = settings_store.repository();
    an::ui::extensions::ExtensionSdkService sdk_service(&settings);
    an::tests::processes::ViewHandle target = targetViewOf("auto-ui-source", source_image);
    ASSERT_NE(target, nullptr);

    an::tests::processes::framework::CaseLaunchCoordinator coordinator;
    coordinator.configureSdkService(&settings);
    coordinator.setManagedImageHooks(
        [&](ImageViewControl*, an::consumer::ImageAsset&& image, const std::string&, const std::string&) {
            const bool stored_increment = sdk_service.upsertExtensionSetting(
                "an.demo.auto_ui", "presented_increment",
                QCborValue(sampleAt(image, 0, 0, 0) - sampleAt(*source_image, 0, 0, 0)), nullptr);
            EXPECT_TRUE(stored_increment);
        });

    an::ui::extensions::ExtensionHostLaunchBinding binding;
    binding.descriptor_id = "filters.auto_ui";
    binding.extension_id = "an.demo.auto_ui";
    binding.display_name = "Auto UI";
    binding.host_executable = host_path;
    binding.capabilities = {"execute"};
    binding.process_capabilities.ui = "auto";
    binding.host_arguments = {"--extension-package-root",
                              package_root.string(),
                              "--entry-point",
                              "auto_ui_ext",
                              "--process-class",
                              "AutoUiExecuteExtension",
                              "--environment-root",
                              env_root.string(),
                              "--runtime-root",
                              runtime_root.string(),
                              "--execute-delay-ms",
                              "120",
                              "--stay-alive-ms",
                              std::to_string(5000 * kIpcTimingMultiplier)};

    std::string error;
    ASSERT_TRUE(coordinator.launchBoundDescriptor(binding, target.get(), &error)) << error;
    ASSERT_TRUE(pumpCoordinatorUntilNativeSessionReady(coordinator, binding.descriptor_id, 750 * kIpcTimingMultiplier));

    an::ui::extensions::ExtensionHostExecutionDispatchResult dispatch_result;
    ASSERT_TRUE(coordinator.submitExecuteForDescriptor(binding.descriptor_id, target.get(), &dispatch_result, &error))
        << error;
    EXPECT_EQ(dispatch_result.state, an::ui::extensions::ExtensionHostExecutionDispatchState::Started);

    ASSERT_TRUE(pumpCoordinatorUntil(coordinator, 3000 * kIpcTimingMultiplier, [&sdk_service]() {
        QCborValue params_value;
        if (!sdk_service.selectExtensionSetting("an.demo.auto_ui", "last_params", &params_value, QCborValue(),
                                                nullptr) ||
            !params_value.isMap())
        {
            return false;
        }

        QString last_token;
        if (!sdk_service.selectExtensionSetting("an.demo.auto_ui", "last_token", &last_token, {}, nullptr) ||
            last_token.isEmpty())
        {
            return false;
        }

        QCborValue presented_increment;
        return sdk_service.selectExtensionSetting("an.demo.auto_ui", "presented_increment", &presented_increment,
                                                  QCborValue(), nullptr) &&
               (presented_increment.isDouble() || presented_increment.isInteger());
    })) << error;

    QCborValue params_value;
    ASSERT_TRUE(
        sdk_service.selectExtensionSetting("an.demo.auto_ui", "last_params", &params_value, QCborValue(), &error))
        << error;
    ASSERT_TRUE(params_value.isMap());

    const QCborMap params = params_value.toMap();
    EXPECT_NEAR(params.value(QStringLiteral("strength")).toDouble(), 0.5, 1e-9);
    EXPECT_EQ(params.value(QStringLiteral("mode")).toString(), QStringLiteral("fast"));
    EXPECT_FALSE(params.value(QStringLiteral("use_mask")).toBool());
    EXPECT_EQ(params.value(QStringLiteral("radius")).toInteger(), 3);
    EXPECT_FALSE(params.contains(QStringLiteral("panel_section")));
    EXPECT_FALSE(params.contains(QStringLiteral("panel_summary")));
    EXPECT_FALSE(params.contains(QStringLiteral("auto_calculate")));
    EXPECT_FALSE(params.contains(QStringLiteral("presented_increment")));

    QString last_token;
    ASSERT_TRUE(sdk_service.selectExtensionSetting("an.demo.auto_ui", "last_token", &last_token, {}, &error)) << error;
    EXPECT_FALSE(last_token.isEmpty());

    QCborValue presented_increment;
    ASSERT_TRUE(sdk_service.selectExtensionSetting("an.demo.auto_ui", "presented_increment", &presented_increment,
                                                   QCborValue(), &error))
        << error;
    ASSERT_TRUE(presented_increment.isDouble() || presented_increment.isInteger());
    EXPECT_NEAR(presented_increment.isDouble() ? presented_increment.toDouble()
                                               : static_cast<double>(presented_increment.toInteger()),
                1.0, 1e-9);

    ASSERT_TRUE(stopCoordinatorAndWaitForSessionAbsence(coordinator, 500 * kIpcTimingMultiplier));
    removeAllNoThrow(temp_root);
}

// A Python extension can use the runtime's core and file bindings during an execute.
TEST(ExtensionHostManagerTest, RealPythonExtensionExecuteCanUseRuntimeCoreAndIoDomainBindings)
{
    const std::filesystem::path host_path = extensionHostPathForCurrentAppDir();
    const std::filesystem::path runtime_root = runtimeRootForCurrentAppDir();
    const std::filesystem::path bundled_python = repositoryBundledPythonPath();
    AN_REQUIRE_FULL_EXTENSION_HOST(host_path);
    ASSERT_TRUE(std::filesystem::exists(runtime_root)) << runtime_root;
    ASSERT_FALSE(bundled_python.empty()) << "Repository bundled Python runtime must be available";

    const auto temp_root = uniqueExtensionRuntimeRootForTest("runtime-domains");
    removeAllNoThrow(temp_root);
    const RemovePathOnExit remove_temp_root{temp_root};
    std::filesystem::create_directories(temp_root);

    const auto env_root = temp_root / "env";
    const auto package_root = temp_root / "runtime_domain_ext";
    std::filesystem::create_directories(package_root);

    ASSERT_NO_FATAL_FAILURE(createVirtualEnvironment(bundled_python, env_root));

    if (!provisionBundledRuntimePackageIntoEnvironment(runtime_root, env_root, "numpy"))
        GTEST_SKIP() << "numpy not available in bundled runtime";

    {
        std::ofstream out(package_root / "runtime_domain_ext.py", std::ios::binary);
        ASSERT_TRUE(out.is_open());
        out << "import os\n"
            << "import afternight\n\n"
            << "class RuntimeDomainExtension(afternight.ProcessExtension):\n"
            << "    def execute(self, target, src_image, dst_image, params, progress, masks=None, weights=None, "
               "output_masks=None):\n"
            << "        assert 'float32' in afternight.core.available_pixel_types()\n"
            << "        runtime_source = afternight.core.convert_pixel_type(src_image, 'float32')\n"
            << "        red, green, blue = afternight.core.extract_channels(runtime_source)\n"
            << "        recomposed = afternight.core.compose_rgb(red, green, blue)\n"
            << "        roundtrip = afternight.core.from_numpy(recomposed.to_numpy(), {'AUTHOR': "
               "'RuntimeDomainTest'})\n"
            << "        roundtrip.set_metadata('PIPELINE', 'runtime-domain-roundtrip')\n"
            << "        output_path = os.path.join(os.path.dirname(__file__), 'runtime_roundtrip.fit')\n"
            << "        assert afternight.io.is_supported_path(output_path)\n"
            << "        afternight.io.save(roundtrip, output_path, {\n"
            << "            'embed_processing_history': True,\n"
            << "            'processing_history': ['runtime-domain-roundtrip'],\n"
            << "        })\n"
            << "        loaded = afternight.io.load(output_path)\n"
            << "        props = loaded.properties\n"
            << "        author = str(loaded.metadata.get('AUTHOR', ''))\n"
            << "        pipeline = str(loaded.metadata.get('PIPELINE', ''))\n"
            << "        assert 'RuntimeDomainTest' in author\n"
            << "        assert 'runtime-domain-roundtrip' in pipeline\n"
            << "        self.settings.set('saved_path', output_path)\n"
            << "        self.settings.set('saved_exists', os.path.exists(output_path))\n"
            << "        self.settings.set('last_runtime_width', props['width'])\n"
            << "        self.settings.set('last_runtime_channels', props['channels'])\n"
            << "        self.settings.set('last_runtime_author', author)\n"
            << "        self.settings.set('last_runtime_pipeline', pipeline)\n"
            << "        afternight.log_info(f\"runtime-domains width={props['width']} channels={props['channels']}\", "
               "component='runtime.domain.test')\n"
            << "        dst_image.copy_from(loaded)\n";
    }

    InMemorySettings settings_store;
    GlobalSettingsPersistence& settings = settings_store.repository();
    an::ui::extensions::ExtensionSdkService sdk_service(&settings);

    an::ui::extensions::ExtensionHostManager manager;
    manager.setSdkService(&sdk_service);

    an::ui::extensions::ExtensionHostLaunchRequest request;
    request.extension_id = "an.demo.runtime.domains";
    request.sdk_backend = "runtime";
    request.host_executable = host_path;
    request.capabilities = {"execute"};
    request.host_arguments = {
        "--extension-package-root", package_root.string(), "--entry-point",
        "runtime_domain_ext",       "--process-class",     "RuntimeDomainExtension",
        "--environment-root",       env_root.string(),     "--runtime-root",
        runtime_root.string(),      "--stay-alive-ms",     std::to_string(5000 * kIpcTimingMultiplier)};

    std::string error;
    auto prepared = manager.prepareSession(request);
    an::ui::extensions::ExtensionHostRunningSession running_session;
    ASSERT_TRUE(manager.launchSession(prepared, &running_session, &error)) << error;
    SessionGuard session_guard{manager, running_session};

    const auto root = uniqueSharedBufferRootForTest("python-runtime-domains");
    removeAllNoThrow(root);
    const RemovePathOnExit remove_root{root};

    std::shared_ptr<an::consumer::ImageAsset> source_image;
    auto source_buffer =
        makeLargeRgbSourceBuffer(root, running_session.prepared_session.session_id, 16, 12, &source_image, &error);
    ASSERT_TRUE(source_buffer.valid()) << error;
    ASSERT_TRUE(source_image);

    std::string request_id;
    ASSERT_TRUE(manager.sendExecuteRequest(&running_session, "runtime-domains-token", {source_buffer.descriptor()},
                                           &request_id, &error))
        << error;
    ASSERT_FALSE(request_id.empty());

    ASSERT_TRUE(pumpUntilRequestCompletes(manager, &running_session, 5000 * kIpcTimingMultiplier, &error)) << error;

    EXPECT_FALSE(running_session.last_preview_result.has_value());
    ASSERT_TRUE(running_session.last_view_commit.has_value());
    ASSERT_EQ(running_session.last_view_commit->result_buffers.size(), 1u);

    an::ui::extensions::SharedBufferManager shared_buffer_manager(root);
    std::optional<an::consumer::ImageAsset> output_image;
    ASSERT_TRUE(importBufferOrReport(shared_buffer_manager, running_session.last_view_commit->result_buffers.front(),
                                     &output_image, &error))
        << error;
    ASSERT_TRUE(output_image);
    ASSERT_EQ(widthOf(*output_image), widthOf(*source_image));
    ASSERT_EQ(heightOf(*output_image), heightOf(*source_image));
    ASSERT_EQ(channelsOf(*output_image), channelsOf(*source_image));
    ASSERT_TRUE(hasSampleType(*output_image, an::SampleType::Float32));

    for (std::size_t channel = 0; channel < channelsOf(*source_image); ++channel)
    {
        for (std::size_t y = 0; y < heightOf(*source_image); ++y)
        {
            for (std::size_t x = 0; x < widthOf(*source_image); ++x)
            {
                EXPECT_NEAR(sampleAt(*output_image, x, y, channel), sampleAt(*source_image, x, y, channel), 1e-6)
                    << "Mismatch at channel=" << channel << " x=" << x << " y=" << y;
            }
        }
    }

    QCborValue saved_exists;
    ASSERT_TRUE(sdk_service.selectExtensionSetting("an.demo.runtime.domains", "saved_exists", &saved_exists,
                                                   QCborValue(false), &error))
        << error;
    EXPECT_TRUE(saved_exists.toBool());

    QCborValue saved_width;
    ASSERT_TRUE(sdk_service.selectExtensionSetting("an.demo.runtime.domains", "last_runtime_width", &saved_width,
                                                   QCborValue(), &error))
        << error;
    EXPECT_EQ(saved_width.toInteger(), 16);

    QCborValue saved_channels;
    ASSERT_TRUE(sdk_service.selectExtensionSetting("an.demo.runtime.domains", "last_runtime_channels", &saved_channels,
                                                   QCborValue(), &error))
        << error;
    EXPECT_EQ(saved_channels.toInteger(), 3);

    QString saved_path;
    ASSERT_TRUE(sdk_service.selectExtensionSetting("an.demo.runtime.domains", "saved_path", &saved_path, {}, &error))
        << error;
    EXPECT_TRUE(std::filesystem::exists(std::filesystem::path(saved_path.toStdString())));

    QString author_value;
    ASSERT_TRUE(
        sdk_service.selectExtensionSetting("an.demo.runtime.domains", "last_runtime_author", &author_value, {}, &error))
        << error;
    EXPECT_NE(author_value.toStdString().find("RuntimeDomainTest"), std::string::npos);

    QString pipeline_value;
    ASSERT_TRUE(sdk_service.selectExtensionSetting("an.demo.runtime.domains", "last_runtime_pipeline", &pipeline_value,
                                                   {}, &error))
        << error;
    EXPECT_NE(pipeline_value.toStdString().find("runtime-domain-roundtrip"), std::string::npos);

    const auto& events = sdk_service.recentLogEvents();
    ASSERT_FALSE(events.empty());
    EXPECT_EQ(events.back().component, "runtime.domain.test");
    EXPECT_NE(events.back().message.find("runtime-domains width=16 channels=3"), std::string::npos);

    removeAllNoThrow(root);
    removeAllNoThrow(temp_root);
}

// A Python extension's metadata changes reach the sidecar, and a reserved card fails the execute.
TEST(ExtensionHostManagerTest, RealPythonExtensionMetadataRoundTripRejectsReservedKeys)
{
    const std::filesystem::path host_path = extensionHostPathForCurrentAppDir();
    const std::filesystem::path runtime_root = runtimeRootForCurrentAppDir();
    const std::filesystem::path bundled_python = repositoryBundledPythonPath();
    AN_REQUIRE_FULL_EXTENSION_HOST(host_path);
    ASSERT_TRUE(std::filesystem::exists(runtime_root)) << runtime_root;
    ASSERT_FALSE(bundled_python.empty()) << "Repository bundled Python runtime must be available";

    const auto temp_root = uniqueExtensionRuntimeRootForTest("metadata");
    removeAllNoThrow(temp_root);
    const RemovePathOnExit remove_temp_root{temp_root};
    std::filesystem::create_directories(temp_root);

    const auto env_root = temp_root / "env";
    const auto package_root = temp_root / "runtime_metadata_ext";
    std::filesystem::create_directories(package_root);

    ASSERT_NO_FATAL_FAILURE(createVirtualEnvironment(bundled_python, env_root));

    if (!provisionBundledRuntimePackageIntoEnvironment(runtime_root, env_root, "numpy"))
        GTEST_SKIP() << "numpy not available in bundled runtime";

    {
        std::ofstream out(package_root / "runtime_metadata_ext.py", std::ios::binary);
        ASSERT_TRUE(out.is_open());
        out << "import afternight\n\n"
            << "class RuntimeMetadataExtension(afternight.ProcessExtension):\n"
            << "    def execute(self, target, src_image, dst_image, params, progress, masks=None, weights=None, "
               "output_masks=None):\n"
            << "        metadata = src_image.metadata\n"
            << "        assert metadata['OBJECT'] == 'M42'\n"
            << "        assert metadata['FILTER'] == 'L'\n"
            << "        dst_image.copy_from(src_image)\n"
            << "        dst_image.set_metadata('OBJECT', 'M31')\n"
            << "        dst_image.set_metadata('OBSERVER', 'AfterNight Test')\n"
            << "        if params.get('_params_token') == 'reserved-check':\n"
            << "            dst_image.set_metadata('BITPIX', '-32')\n";
    }

    an::ui::extensions::ExtensionHostManager manager;

    an::ui::extensions::ExtensionHostLaunchRequest request;
    request.extension_id = "an.demo.metadata";
    request.sdk_backend = "runtime";
    request.host_executable = host_path;
    request.capabilities = {"execute"};
    request.host_arguments = {
        "--extension-package-root", package_root.string(), "--entry-point",
        "runtime_metadata_ext",     "--process-class",     "RuntimeMetadataExtension",
        "--environment-root",       env_root.string(),     "--runtime-root",
        runtime_root.string(),      "--stay-alive-ms",     std::to_string(15000 * kIpcTimingMultiplier)};

    auto prepared = manager.prepareSession(request);
    an::ui::extensions::ExtensionHostRunningSession running_session;
    std::string error;
    ASSERT_TRUE(manager.launchSession(prepared, &running_session, &error)) << error;
    SessionGuard session_guard{manager, running_session};

    const auto root = uniqueSharedBufferRootForTest("python-metadata");
    removeAllNoThrow(root);
    const RemovePathOnExit remove_root{root};
    std::filesystem::path metadata_sidecar_path;
    auto source_buffer =
        makeSourceBufferWithMetadata(root, running_session.prepared_session.session_id,
                                     QJsonObject{{QStringLiteral("OBJECT"), QStringLiteral("M42")},
                                                 {QStringLiteral("FILTER"), QStringLiteral("L")},
                                                 {QStringLiteral("FILENAME"), QStringLiteral("input.fit")}},
                                     &metadata_sidecar_path, &error);
    ASSERT_TRUE(source_buffer.valid()) << error;

    std::string request_id;
    ASSERT_TRUE(manager.sendExecuteRequest(&running_session, "metadata-roundtrip", {source_buffer.descriptor()},
                                           &request_id, &error))
        << error;
    ASSERT_FALSE(request_id.empty());
    ASSERT_TRUE(pumpUntilRequestCompletes(manager, &running_session, 3000 * kIpcTimingMultiplier, &error)) << error;

    EXPECT_FALSE(running_session.last_preview_result.has_value());
    ASSERT_TRUE(running_session.last_view_commit.has_value());

    {
        std::ifstream in(metadata_sidecar_path, std::ios::binary);
        ASSERT_TRUE(in.is_open());
        const std::string json_text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        QJsonParseError parse_error;
        const QJsonDocument document = QJsonDocument::fromJson(QByteArray::fromStdString(json_text), &parse_error);
        ASSERT_EQ(parse_error.error, QJsonParseError::NoError);
        ASSERT_TRUE(document.isObject());
        const QJsonObject object = document.object();
        EXPECT_EQ(object.value(QStringLiteral("OBJECT")).toString(), QStringLiteral("M31"));
        EXPECT_EQ(object.value(QStringLiteral("OBSERVER")).toString(), QStringLiteral("AfterNight Test"));
        EXPECT_EQ(object.value(QStringLiteral("FILTER")).toString(), QStringLiteral("L"));
        EXPECT_EQ(object.value(QStringLiteral("FILENAME")).toString(), QStringLiteral("input.fit"));
        EXPECT_FALSE(object.contains(QStringLiteral("BITPIX")));
    }

    ASSERT_TRUE(writeMetadataSidecar(metadata_sidecar_path,
                                     QJsonObject{{QStringLiteral("OBJECT"), QStringLiteral("M42")},
                                                 {QStringLiteral("FILTER"), QStringLiteral("L")},
                                                 {QStringLiteral("FILENAME"), QStringLiteral("input.fit")}},
                                     &error))
        << error;

    ASSERT_TRUE(manager.sendExecuteRequest(&running_session, "reserved-check", {source_buffer.descriptor()},
                                           &request_id, &error))
        << error;
    ASSERT_FALSE(request_id.empty());
    EXPECT_FALSE(pumpUntilRequestCompletes(manager, &running_session, 3000 * kIpcTimingMultiplier, &error));
    EXPECT_EQ(running_session.state, an::ui::extensions::ExtensionHostSessionState::Failed);
    EXPECT_NE(error.find("view.commit"), std::string::npos);

    removeAllNoThrow(root);
    removeAllNoThrow(temp_root);
}

// A Python extension reads mask and weight buffers and writes an output mask beside its result.
TEST(ExtensionHostManagerTest, RealPythonExtensionExecutePropagatesMaskWeightAndOutputMaskBuffers)
{
    const std::filesystem::path host_path = extensionHostPathForCurrentAppDir();
    const std::filesystem::path runtime_root = runtimeRootForCurrentAppDir();
    const std::filesystem::path bundled_python = repositoryBundledPythonPath();
    AN_REQUIRE_FULL_EXTENSION_HOST(host_path);
    ASSERT_TRUE(std::filesystem::exists(runtime_root)) << runtime_root;
    ASSERT_FALSE(bundled_python.empty()) << "Repository bundled Python runtime must be available";

    const auto temp_root = uniqueExtensionRuntimeRootForTest("aux-buffers");
    removeAllNoThrow(temp_root);
    const RemovePathOnExit remove_temp_root{temp_root};
    std::filesystem::create_directories(temp_root);

    const auto env_root = temp_root / "env";
    const auto package_root = temp_root / "aux_buffers_ext";
    std::filesystem::create_directories(package_root);

    ASSERT_NO_FATAL_FAILURE(createVirtualEnvironment(bundled_python, env_root));

    if (!provisionBundledRuntimePackageIntoEnvironment(runtime_root, env_root, "numpy"))
        GTEST_SKIP() << "numpy not available in bundled runtime";

    {
        std::ofstream out(package_root / "aux_buffers_ext.py", std::ios::binary);
        ASSERT_TRUE(out.is_open());
        out << "import afternight\n\n"
            << "class AuxiliaryBuffersExtension(afternight.ProcessExtension):\n"
            << "    def execute(self, target, src_image, dst_image, params, progress, masks=None, weights=None, "
               "output_masks=None):\n"
            << "        assert sorted(masks.keys()) == ['selection']\n"
            << "        assert sorted(weights.keys()) == ['weight_map']\n"
            << "        assert sorted(output_masks.keys()) == ['output_mask']\n"
            << "        src = src_image.to_numpy()\n"
            << "        selection = masks['selection'].to_numpy()\n"
            << "        weight_map = weights['weight_map'].to_numpy()\n"
            << "        dst_image.from_numpy(src + selection + weight_map)\n"
            << "        output_masks['output_mask'].from_numpy(selection * 2.0 + weight_map)\n";
    }

    an::ui::extensions::ExtensionHostManager manager;
    an::ui::extensions::ExtensionHostLaunchRequest request;
    request.extension_id = "an.demo.aux_buffers";
    request.sdk_backend = "runtime";
    request.host_executable = host_path;
    request.capabilities = {"execute"};
    request.host_arguments = {
        "--extension-package-root", package_root.string(), "--entry-point",
        "aux_buffers_ext",          "--process-class",     "AuxiliaryBuffersExtension",
        "--environment-root",       env_root.string(),     "--runtime-root",
        runtime_root.string(),      "--stay-alive-ms",     std::to_string(5000 * kIpcTimingMultiplier)};

    auto prepared = manager.prepareSession(request);
    an::ui::extensions::ExtensionHostRunningSession running_session;
    std::string error;
    ASSERT_TRUE(manager.launchSession(prepared, &running_session, &error)) << error;
    SessionGuard session_guard{manager, running_session};

    const auto root = uniqueSharedBufferRootForTest("python-aux-buffers");
    removeAllNoThrow(root);
    const RemovePathOnExit remove_root{root};

    const auto source_image = makeMonoFloatImage("source", 5, 4, 1.0f, 0.25f);
    const auto selection_image = makeMonoFloatImage("selection", 5, 4, 0.2f, 0.05f);
    const auto weight_image = makeMonoFloatImage("weight_map", 5, 4, 0.5f, 0.1f);
    const auto output_mask_template = makeMonoFloatImage("output_mask", 5, 4, 0.0f, 0.0f);

    auto source_buffer = makeImageBuffer(root, running_session.prepared_session.session_id, source_image,
                                         an::ui::extensions::SharedBufferRole::Source,
                                         an::ui::extensions::SharedBufferAccessMode::ReadWrite, "source", &error);
    auto selection_buffer = makeImageBuffer(root, running_session.prepared_session.session_id, selection_image,
                                            an::ui::extensions::SharedBufferRole::Mask,
                                            an::ui::extensions::SharedBufferAccessMode::ReadOnly, "selection", &error);
    auto weight_buffer = makeImageBuffer(root, running_session.prepared_session.session_id, weight_image,
                                         an::ui::extensions::SharedBufferRole::Weight,
                                         an::ui::extensions::SharedBufferAccessMode::ReadOnly, "weight_map", &error);
    auto output_mask_buffer =
        makeImageBuffer(root, running_session.prepared_session.session_id, output_mask_template,
                        an::ui::extensions::SharedBufferRole::Mask,
                        an::ui::extensions::SharedBufferAccessMode::ReadWrite, "output_mask", &error);
    ASSERT_TRUE(source_buffer.valid()) << error;
    ASSERT_TRUE(selection_buffer.valid()) << error;
    ASSERT_TRUE(weight_buffer.valid()) << error;
    ASSERT_TRUE(output_mask_buffer.valid()) << error;

    std::string request_id;
    ASSERT_TRUE(manager.sendExecuteRequest(&running_session, "aux-buffers-token",
                                           {source_buffer.descriptor(), selection_buffer.descriptor(),
                                            weight_buffer.descriptor(), output_mask_buffer.descriptor()},
                                           &request_id, &error))
        << error;
    ASSERT_FALSE(request_id.empty());

    ASSERT_TRUE(pumpUntilRequestCompletes(manager, &running_session, 5000 * kIpcTimingMultiplier, &error)) << error;

    EXPECT_FALSE(running_session.last_preview_result.has_value());
    ASSERT_TRUE(running_session.last_view_commit.has_value());
    ASSERT_EQ(running_session.last_view_commit->result_buffers.size(), 2u);

    const an::ui::extensions::SharedBufferDescriptor* destination_descriptor = nullptr;
    const an::ui::extensions::SharedBufferDescriptor* output_mask_descriptor = nullptr;
    for (const auto& descriptor : running_session.last_view_commit->result_buffers)
    {
        if (descriptor.buffer_role == an::ui::extensions::SharedBufferRole::Destination)
        {
            destination_descriptor = &descriptor;
        }
        else if (descriptor.buffer_role == an::ui::extensions::SharedBufferRole::Mask &&
                 descriptor.symbolic_name == "output_mask")
        {
            output_mask_descriptor = &descriptor;
        }
    }
    ASSERT_NE(destination_descriptor, nullptr);
    ASSERT_NE(output_mask_descriptor, nullptr);

    an::ui::extensions::SharedBufferManager shared_buffer_manager(root);
    std::optional<an::consumer::ImageAsset> output_image;
    ASSERT_TRUE(importBufferOrReport(shared_buffer_manager, *destination_descriptor, &output_image, &error)) << error;
    ASSERT_TRUE(output_image);

    std::optional<an::consumer::ImageAsset> output_mask_image;
    ASSERT_TRUE(importBufferOrReport(shared_buffer_manager, *output_mask_descriptor, &output_mask_image, &error))
        << error;
    ASSERT_TRUE(output_mask_image);

    for (std::size_t y = 0; y < heightOf(*source_image); ++y)
    {
        for (std::size_t x = 0; x < widthOf(*source_image); ++x)
        {
            const double source = sampleAt(*source_image, x, y, 0);
            const double selection = sampleAt(*selection_image, x, y, 0);
            const double weight = sampleAt(*weight_image, x, y, 0);
            EXPECT_NEAR(sampleAt(*output_image, x, y, 0), source + selection + weight, 1e-6)
                << "destination mismatch at x=" << x << " y=" << y;
            EXPECT_NEAR(sampleAt(*output_mask_image, x, y, 0), selection * 2.0 + weight, 1e-6)
                << "output mask mismatch at x=" << x << " y=" << y;
        }
    }

    removeAllNoThrow(root);
    removeAllNoThrow(temp_root);
}

// A managed execute lends the target's mask and weight and hands the output mask to the application by name.
TEST(ExtensionHostManagerTest, LaunchCoordinatorExecutePropagatesMaskWeightAndOutputMaskBuffers)
{
    const std::filesystem::path host_path = extensionHostPathForCurrentAppDir();
    const std::filesystem::path runtime_root = runtimeRootForCurrentAppDir();
    const std::filesystem::path bundled_python = repositoryBundledPythonPath();
    AN_REQUIRE_FULL_EXTENSION_HOST(host_path);
    ASSERT_TRUE(std::filesystem::exists(runtime_root)) << runtime_root;
    ASSERT_FALSE(bundled_python.empty()) << "Repository bundled Python runtime must be available";

    const auto temp_root = uniqueExtensionRuntimeRootForTest("managed-aux-buffers");
    removeAllNoThrow(temp_root);
    const RemovePathOnExit remove_temp_root{temp_root};
    std::filesystem::create_directories(temp_root);

    const auto env_root = temp_root / "env";
    const auto package_root = temp_root / "managed_aux_buffers_ext";
    std::filesystem::create_directories(package_root);

    ASSERT_NO_FATAL_FAILURE(createVirtualEnvironment(bundled_python, env_root));

    if (!provisionBundledRuntimePackageIntoEnvironment(runtime_root, env_root, "numpy"))
        GTEST_SKIP() << "numpy not available in bundled runtime";

    {
        std::ofstream out(package_root / "managed_aux_buffers_ext.py", std::ios::binary);
        ASSERT_TRUE(out.is_open());
        out << "import afternight\n\n"
            << "class ManagedAuxiliaryBuffersExtension(afternight.ProcessExtension):\n"
            << "    def execute(self, target, src_image, dst_image, params, progress, masks=None, weights=None, "
               "output_masks=None):\n"
            << "        assert sorted(masks.keys()) == ['selection']\n"
            << "        assert sorted(weights.keys()) == ['weight_map']\n"
            << "        assert sorted(output_masks.keys()) == ['output_mask']\n"
            << "        src = src_image.to_numpy()\n"
            << "        selection = masks['selection'].to_numpy()\n"
            << "        weight_map = weights['weight_map'].to_numpy()\n"
            << "        dst_image.from_numpy(src + selection + weight_map)\n"
            << "        output_masks['output_mask'].from_numpy(selection * 2.0 + weight_map)\n";
    }

    const auto source_image = makeMonoFloatImage("source", 5, 4, 1.0f, 0.25f);
    const auto selection_image = makeMonoFloatImage("selection", 5, 4, 0.2f, 0.05f);
    const auto weight_image = makeMonoFloatImage("weight_map", 5, 4, 0.5f, 0.1f);
    const auto output_mask_template = makeMonoFloatImage("output_mask", 5, 4, 0.0f, 0.0f);

    an::tests::processes::ViewHandle target = targetViewOf("managed-aux-buffers", source_image);
    ASSERT_NE(target, nullptr);

    an::tests::processes::framework::CaseLaunchCoordinator coordinator;
    int presenter_count = 0;
    int output_mask_presenter_count = 0;
    std::optional<an::consumer::ImageAsset> presented_image;
    std::vector<an::ui::extensions::ManagedNamedImageResult> presented_masks;
    coordinator.setManagedImageHooks(
        [&](ImageViewControl* control, an::consumer::ImageAsset&& image, const std::string& history_step_name,
            const std::string& history_step_description) {
            ++presenter_count;
            EXPECT_EQ(control, target.get());
            EXPECT_FALSE(history_step_name.empty());
            EXPECT_FALSE(history_step_description.empty());
            presented_image.emplace(std::move(image));
        },
        {},
        [&](ImageViewControl* control,
            const bool for_preview) -> std::vector<an::ui::extensions::ManagedAuxiliaryImage> {
            if (control != target.get() || for_preview)
            {
                return {};
            }

            return {{an::ui::extensions::SharedBufferRole::Mask, an::ui::extensions::SharedBufferAccessMode::ReadOnly,
                     "selection", selection_image},
                    {an::ui::extensions::SharedBufferRole::Weight, an::ui::extensions::SharedBufferAccessMode::ReadOnly,
                     "weight_map", weight_image},
                    {an::ui::extensions::SharedBufferRole::Mask, an::ui::extensions::SharedBufferAccessMode::ReadWrite,
                     "output_mask", output_mask_template}};
        },
        [&](ImageViewControl* control, std::vector<an::ui::extensions::ManagedNamedImageResult>&& output_masks) {
            ++output_mask_presenter_count;
            EXPECT_EQ(control, target.get());
            presented_masks = std::move(output_masks);
        });

    an::ui::extensions::ExtensionHostLaunchBinding binding;
    binding.descriptor_id = "filters.aux_buffers";
    binding.extension_id = "an.demo.managed_aux_buffers";
    binding.display_name = "Managed Auxiliary Buffers";
    binding.host_executable = host_path;
    binding.capabilities = {"execute"};
    binding.process_capabilities.masks_input = true;
    binding.process_capabilities.masks_output = true;
    binding.process_capabilities.weights_input = true;
    binding.host_arguments = {
        "--extension-package-root", package_root.string(), "--entry-point",
        "managed_aux_buffers_ext",  "--process-class",     "ManagedAuxiliaryBuffersExtension",
        "--environment-root",       env_root.string(),     "--runtime-root",
        runtime_root.string(),      "--stay-alive-ms",     std::to_string(5000 * kIpcTimingMultiplier)};

    std::string error;
    ASSERT_TRUE(coordinator.launchBoundDescriptor(binding, target.get(), &error)) << error;
    ASSERT_TRUE(pumpCoordinatorUntilNativeSessionReady(coordinator, binding.descriptor_id, 500 * kIpcTimingMultiplier));

    an::ui::extensions::ExtensionHostExecutionDispatchResult dispatch_result;
    ASSERT_TRUE(coordinator.submitExecuteForDescriptor(binding.descriptor_id, target.get(), &dispatch_result, &error))
        << error;
    EXPECT_EQ(dispatch_result.state, an::ui::extensions::ExtensionHostExecutionDispatchState::Started);

    ASSERT_TRUE(pumpCoordinatorUntil(coordinator, 5000 * kIpcTimingMultiplier, [&]() {
        return presenter_count == 1 && output_mask_presenter_count == 1 && coordinator.activeSessionCount() == 0u;
    }));

    ASSERT_EQ(presenter_count, 1);
    ASSERT_EQ(output_mask_presenter_count, 1);
    ASSERT_TRUE(presented_image);
    ASSERT_EQ(presented_masks.size(), 1u);
    ASSERT_TRUE(presented_masks.front().image.descriptor().has_value());
    EXPECT_EQ(presented_masks.front().symbolic_name, "output_mask");

    for (std::size_t y = 0; y < heightOf(*source_image); ++y)
    {
        for (std::size_t x = 0; x < widthOf(*source_image); ++x)
        {
            const double source = sampleAt(*source_image, x, y, 0);
            const double selection = sampleAt(*selection_image, x, y, 0);
            const double weight = sampleAt(*weight_image, x, y, 0);
            EXPECT_NEAR(sampleAt(*presented_image, x, y, 0), source + selection + weight, 1e-6)
                << "managed destination mismatch at x=" << x << " y=" << y;
            EXPECT_NEAR(sampleAt(presented_masks.front().image, x, y, 0), selection * 2.0 + weight, 1e-6)
                << "managed output mask mismatch at x=" << x << " y=" << y;
        }
    }

    ASSERT_TRUE(stopCoordinatorAndWaitForSessionAbsence(coordinator, 500 * kIpcTimingMultiplier));

    removeAllNoThrow(temp_root);
}
// Consuming a preview result keeps a validation message on the window.
TEST(ExtensionHostManagerTest, MarkPreviewResultConsumedPreservesValidationMessage)
{
    an::ui::extensions::ExtensionSessionController controller;
    controller.descriptor_id = "filters.preview_preserves_validation_message";
    controller.display_name = "Preview Preserves Validation Message";
    controller.running_session.prepared_session.session_id = "preview-preserves-validation-message-session";

    controller.current_process_message = an::ui::extensions::ExtensionSessionController::ProcessMessageState{
        QStringLiteral("Extension Validation"), QStringLiteral("strength: Must be <= 0.50"),
        an::ui::extensions::ExtensionSessionController::ProcessMessageState::Tone::Warning,
        an::ui::extensions::ExtensionSessionController::ProcessMessageState::Source::Validation};

    an::ui::extensions::ExecutionResultMessage preview_result;
    preview_result.session_id = controller.running_session.prepared_session.session_id;
    preview_result.target_request_id = "preview-request-id";
    preview_result.result_kind = "preview";
    controller.running_session.last_preview_result = preview_result;

    controller.markPreviewResultConsumed();

    ASSERT_TRUE(controller.current_process_message.has_value());
    EXPECT_EQ(controller.current_process_message->source,
              an::ui::extensions::ExtensionSessionController::ProcessMessageState::Source::Validation);
    EXPECT_EQ(controller.current_process_message->message, QStringLiteral("strength: Must be <= 0.50"));
}

// A native session's preview and execute requests carry the parameters' defaults, leaving out display-only fields,
// and export the target view's document.
TEST(ExtensionHostManagerTest, ShelllessRequestPrepUsesSeededAutoParamDefaults)
{
    const auto source_image = makeRgbGradientImage(5, 4);
    ASSERT_TRUE(source_image);
    an::tests::processes::ViewHandle target = targetViewOf("shellless-seeded-source", source_image);
    ASSERT_NE(target, nullptr);

    const std::string session_id = "shellless-seeded-session-" + an::ui::extensions::generateOpaqueToken();
    const RemovePathOnExit remove_session_buffers{
        an::ui::extensions::SharedBufferManager::defaultSharedBufferRootPath() / session_id};

    an::ui::extensions::ExtensionSessionController controller;
    controller.descriptor_id = "filters.shellless_seeded_params";
    controller.display_name = "Shellless Seeded Params";
    controller.target_control = target.get();
    controller.running_session.prepared_session.session_id = session_id;
    controller.running_session.handshake.ack.native_process_window = true;

    const QJsonArray auto_param_schema{
        QJsonObject{{QStringLiteral("id"), QStringLiteral("strength")},
                    {QStringLiteral("type"), QStringLiteral("float")},
                    {QStringLiteral("default"), 0.25}},
        QJsonObject{{QStringLiteral("id"), QStringLiteral("iterations")},
                    {QStringLiteral("type"), QStringLiteral("int")},
                    {QStringLiteral("default"), 3}},
        QJsonObject{{QStringLiteral("id"), QStringLiteral("preserve_background")},
                    {QStringLiteral("type"), QStringLiteral("bool")},
                    {QStringLiteral("default"), true}},
        QJsonObject{{QStringLiteral("id"), QStringLiteral("mode")},
                    {QStringLiteral("type"), QStringLiteral("choice")},
                    {QStringLiteral("default"), QStringLiteral("gentle")}},
        QJsonObject{{QStringLiteral("id"), QStringLiteral("auto_calculate")},
                    {QStringLiteral("type"), QStringLiteral("button")},
                    {QStringLiteral("label"), QStringLiteral("Auto Calculate")}},
        QJsonObject{{QStringLiteral("id"), QStringLiteral("strength_readout")},
                    {QStringLiteral("type"), QStringLiteral("value_description_label")},
                    {QStringLiteral("source"), QStringLiteral("strength")},
                    {QStringLiteral("text"), QStringLiteral("0.25 - Gentle")}},
        QJsonObject{{QStringLiteral("id"), QStringLiteral("manual_mask")},
                    {QStringLiteral("type"), QStringLiteral("manual_mask_editor")},
                    {QStringLiteral("display_param"), QStringLiteral("mode")},
                    {QStringLiteral("display_value"), QStringLiteral("protected_source")}},
        QJsonObject{{QStringLiteral("id"), QStringLiteral("preview_histogram")},
                    {QStringLiteral("type"), QStringLiteral("rt_histogram")},
                    {QStringLiteral("label"), QStringLiteral("Histogram")}},
        QJsonObject{{QStringLiteral("id"), QStringLiteral("vector_slope")},
                    {QStringLiteral("type"), QStringLiteral("vector_slope")},
                    {QStringLiteral("label"), QStringLiteral("Vector Slope")}},
        QJsonObject{{QStringLiteral("id"), QStringLiteral("vector_tabs")},
                    {QStringLiteral("type"), QStringLiteral("tabs")}},
        QJsonObject{{QStringLiteral("id"), QStringLiteral("primary_vectors")},
                    {QStringLiteral("type"), QStringLiteral("tab")},
                    {QStringLiteral("label"), QStringLiteral("Primary Vectors")}},
        QJsonObject{{QStringLiteral("id"), QStringLiteral("secondary_action")},
                    {QStringLiteral("type"), QStringLiteral("action")},
                    {QStringLiteral("label"), QStringLiteral("Secondary Action")}},
        QJsonObject{{QStringLiteral("id"), QStringLiteral("end_vector_tabs")},
                    {QStringLiteral("type"), QStringLiteral("end_tabs")}},
        QJsonObject{
            {QStringLiteral("id"), QStringLiteral("curve_points")},
            {QStringLiteral("type"), QStringLiteral("curve_editor")},
            {QStringLiteral("default"), QJsonArray{QJsonArray{0.0, 0.0}, QJsonArray{0.5, 0.6}, QJsonArray{1.0, 1.0}}}},
        QJsonObject{{QStringLiteral("id"), QStringLiteral("note")},
                    {QStringLiteral("type"), QStringLiteral("string")},
                    {QStringLiteral("default"), QStringLiteral("seeded")}}};

    std::string error;
    ASSERT_TRUE(controller.initializeSessionParams(auto_param_schema, {}, {}, {}, &error)) << error;

    QCborMap preview_params;
    std::vector<an::ui::extensions::SharedBufferDescriptor> preview_buffers;
    std::vector<an::ui::extensions::SharedImageBuffer> preview_owned_buffers;
    ASSERT_EQ(
        controller.preparePreviewRequest({}, {}, &preview_params, &preview_buffers, &preview_owned_buffers, &error),
        an::ui::extensions::ExtensionSessionController::PrepareOutcome::Ready)
        << error;
    EXPECT_DOUBLE_EQ(preview_params.value(QStringLiteral("strength")).toDouble(), 0.25);
    EXPECT_EQ(preview_params.value(QStringLiteral("iterations")).toInteger(), 3);
    EXPECT_TRUE(preview_params.value(QStringLiteral("preserve_background")).toBool());
    EXPECT_EQ(preview_params.value(QStringLiteral("mode")).toString(), QStringLiteral("gentle"));
    EXPECT_EQ(preview_params.value(QStringLiteral("note")).toString(), QStringLiteral("seeded"));
    const QCborArray preview_curve_points = preview_params.value(QStringLiteral("curve_points")).toArray();
    ASSERT_EQ(preview_curve_points.size(), 3);
    EXPECT_DOUBLE_EQ(preview_curve_points.at(1).toArray().at(0).toDouble(), 0.5);
    EXPECT_DOUBLE_EQ(preview_curve_points.at(1).toArray().at(1).toDouble(), 0.6);
    EXPECT_FALSE(preview_params.contains(QStringLiteral("auto_calculate")));
    EXPECT_FALSE(preview_params.contains(QStringLiteral("strength_readout")));
    EXPECT_FALSE(preview_params.contains(QStringLiteral("manual_mask")));
    EXPECT_FALSE(preview_params.contains(QStringLiteral("preview_histogram")));
    EXPECT_FALSE(preview_params.contains(QStringLiteral("vector_slope")));
    EXPECT_FALSE(preview_params.contains(QStringLiteral("secondary_action")));
    EXPECT_FALSE(preview_params.contains(QStringLiteral("vector_tabs")));
    EXPECT_FALSE(preview_params.contains(QStringLiteral("primary_vectors")));
    EXPECT_FALSE(preview_params.contains(QStringLiteral("end_vector_tabs")));
    EXPECT_FALSE(preview_buffers.empty());
    EXPECT_FALSE(preview_owned_buffers.empty());

    QCborMap execute_params;
    std::vector<an::ui::extensions::SharedBufferDescriptor> execute_buffers;
    std::vector<an::ui::extensions::SharedImageBuffer> execute_owned_buffers;
    ASSERT_EQ(
        controller.prepareExecuteRequest({}, {}, &execute_params, &execute_buffers, &execute_owned_buffers, &error),
        an::ui::extensions::ExtensionSessionController::PrepareOutcome::Ready)
        << error;
    EXPECT_EQ(execute_params, preview_params);
    EXPECT_FALSE(execute_buffers.empty());
    EXPECT_FALSE(execute_owned_buffers.empty());
}

// A native result's history step is named after the process and lists the visible parameters.
TEST(ExtensionHostManagerTest, ShelllessExecutePresentationUsesProcessNameAndParametersForHistory)
{
    an::ui::extensions::ExtensionSessionController controller;
    controller.descriptor_id = "filters.shellless_history";
    controller.display_name = "VeraLux HyperMetric Stretch";
    controller.target_control = fakeTargetControl();
    controller.running_session.prepared_session.session_id = "shellless-history-session";
    controller.running_session.handshake.ack.native_process_window = true;

    an::ui::extensions::ViewCommitMessage view_commit;
    view_commit.session_id = controller.running_session.prepared_session.session_id;
    view_commit.request_id = "shellless-history-commit";
    view_commit.view_id = controller.running_session.prepared_session.session_id;
    view_commit.history_step_name = "Extension Apply";
    view_commit.history_step_description = "Applied extension execution result";
    view_commit.params.insert(QStringLiteral("log_d"), 7.5);
    view_commit.params.insert(QStringLiteral("mode"), QStringLiteral("Scientific"));
    view_commit.params.insert(QStringLiteral("target_background"), 0.1);
    view_commit.params.insert(QStringLiteral("_params_token"), QStringLiteral("internal-token"));
    controller.running_session.last_view_commit = view_commit;

    const auto output_image = makeRgbGradientImage(5, 4);
    ASSERT_TRUE(output_image);
    const std::uint64_t output_identity = output_image->identity();

    int presenter_count = 0;
    std::string presented_step_name;
    std::string presented_step_description;
    controller.applyExecutePresentation(std::move(*output_image), {},
                                        [&](ImageViewControl* control, an::consumer::ImageAsset&& image,
                                            const std::string& history_step_name,
                                            const std::string& history_step_description) {
                                            ++presenter_count;
                                            EXPECT_EQ(control, fakeTargetControl());
                                            EXPECT_EQ(image.identity(), output_identity);
                                            presented_step_name = history_step_name;
                                            presented_step_description = history_step_description;
                                        },
                                        {}, {});

    EXPECT_EQ(presenter_count, 1);
    EXPECT_EQ(presented_step_name, "VeraLux HyperMetric Stretch");
    EXPECT_NE(presented_step_description.find("Parameters used:"), std::string::npos);
    EXPECT_NE(presented_step_description.find("log_d: 7.5"), std::string::npos);
    EXPECT_NE(presented_step_description.find("mode: Scientific"), std::string::npos);
    EXPECT_NE(presented_step_description.find("target_background: 0.1"), std::string::npos);
    EXPECT_EQ(presented_step_description.find("_params_token"), std::string::npos);
}

// A transient result opens in a view of its own, and its output masks go to that view.
TEST(ExtensionHostManagerTest, TransientExecutePresentationCreatesSeparateResultView)
{
    an::ui::extensions::ExtensionSessionController controller;
    controller.descriptor_id = "filters.transient_starless";
    controller.display_name = "Transient Starless";
    controller.target_control = fakeTargetControl();
    controller.create_result_view_after_successful_execute = true;
    controller.running_session.prepared_session.session_id = "transient-starless-session";
    controller.running_session.handshake.ack.native_process_window = true;

    an::ui::extensions::ViewCommitMessage view_commit;
    view_commit.session_id = controller.running_session.prepared_session.session_id;
    view_commit.request_id = "transient-starless-commit";
    view_commit.view_id = controller.running_session.prepared_session.session_id;
    view_commit.history_step_name = "Dark Star";
    view_commit.history_step_description = "Generated starless image";
    controller.running_session.last_view_commit = view_commit;

    const auto output_image = makeRgbGradientImage(5, 4);
    const auto output_mask = makeRgbGradientImage(5, 4);
    ASSERT_TRUE(output_image);
    ASSERT_TRUE(output_mask);
    const std::uint64_t output_identity = output_image->identity();
    const std::uint64_t output_mask_identity = output_mask->identity();
    std::vector<an::ui::extensions::ManagedNamedImageResult> masks_to_present;
    masks_to_present.push_back({"output_mask", std::move(*output_mask)});

    ImageViewControl* const generated_control = reinterpret_cast<ImageViewControl*>(static_cast<std::uintptr_t>(0x2));
    int target_presenter_count = 0;
    int generated_presenter_count = 0;
    int output_mask_presenter_count = 0;
    ImageViewControl* output_mask_target = nullptr;

    controller.applyExecutePresentation(
        std::move(*output_image), std::move(masks_to_present),
        [&](ImageViewControl*, an::consumer::ImageAsset&&, const std::string&, const std::string&) {
            ++target_presenter_count;
        },
        [&](ImageViewControl* control, std::vector<an::ui::extensions::ManagedNamedImageResult>&& masks) {
            ++output_mask_presenter_count;
            output_mask_target = control;
            ASSERT_EQ(masks.size(), 1u);
            EXPECT_EQ(masks.front().image.identity(), output_mask_identity);
        },
        [&](ImageViewControl* control, an::consumer::ImageAsset&& image, const std::string& history_step_name,
            const std::string& history_step_description) -> ImageViewControl* {
            ++generated_presenter_count;
            EXPECT_EQ(control, fakeTargetControl());
            EXPECT_EQ(image.identity(), output_identity);
            EXPECT_EQ(history_step_name, "Dark Star");
            EXPECT_EQ(history_step_description, "Generated starless image");
            return generated_control;
        });

    EXPECT_EQ(target_presenter_count, 0);
    EXPECT_EQ(generated_presenter_count, 1);
    EXPECT_EQ(output_mask_presenter_count, 1);
    EXPECT_EQ(output_mask_target, generated_control);
}

// A native session's view commit is imported even with no source buffer of its own.
TEST(ExtensionHostManagerTest, ShelllessNativeViewCommitMaterializesWithoutSourceDescriptor)
{
    an::ui::extensions::ExtensionSessionController controller;
    controller.descriptor_id = "filters.shellless_materialize_execute";
    controller.display_name = "Shellless Materialize Execute";
    controller.target_control = fakeTargetControl();
    controller.running_session.prepared_session.session_id = "shellless-materialize-session";
    controller.running_session.handshake.ack.native_process_window = true;

    const auto output_image_expected = makeRgbGradientImage(5, 4);
    ASSERT_TRUE(output_image_expected);

    const auto root = uniqueSharedBufferRootForTest("shellless_materialize_execute");
    removeAllNoThrow(root);
    const RemovePathOnExit remove_root{root};

    an::ui::extensions::SharedBufferManager shared_buffer_manager(root);
    an::ui::extensions::SharedImageBuffer output_buffer;
    std::string error;
    ASSERT_TRUE(exportImageOrReport(shared_buffer_manager, *output_image_expected,
                                    controller.running_session.prepared_session.session_id,
                                    an::ui::extensions::SharedBufferRole::Destination, &output_buffer, &error,
                                    "shellless-execute-output", "destination"))
        << error;

    an::ui::extensions::ViewCommitMessage view_commit;
    view_commit.session_id = controller.running_session.prepared_session.session_id;
    view_commit.request_id = "shellless-commit-request";
    view_commit.view_id = controller.running_session.prepared_session.session_id;
    view_commit.result_buffers = {output_buffer.descriptor()};
    controller.running_session.last_view_commit = view_commit;

    std::optional<an::ui::extensions::ExtensionSessionController::LandedMaterialization> landed =
        landExecuteMaterialization(controller);
    ASSERT_TRUE(landed.has_value());
    ASSERT_TRUE(landed->image.hasValue()) << landed->image.status().message();
    EXPECT_EQ(widthOf(*landed->image), widthOf(*output_image_expected));
    EXPECT_EQ(heightOf(*landed->image), heightOf(*output_image_expected));
    EXPECT_EQ(channelsOf(*landed->image), channelsOf(*output_image_expected));

    output_buffer = an::ui::extensions::SharedImageBuffer{};

    const an::Status removed =
        shared_buffer_manager.removeSessionBuffers(controller.running_session.prepared_session.session_id);
    EXPECT_TRUE(removed.ok()) << removed.message();
}

// A result of another size than the source is refused unless the commit allows it.
TEST(ExtensionHostManagerTest, ResizeAwareViewCommitMaterializesDifferentGeometry)
{
    an::ui::extensions::ExtensionSessionController controller;
    controller.descriptor_id = "filters.resize_aware_materialize_execute";
    controller.display_name = "Resize Aware Materialize Execute";
    controller.target_control = fakeTargetControl();
    controller.running_session.prepared_session.session_id = "resize-aware-materialize-session";

    const auto root = uniqueSharedBufferRootForTest("resize_aware_materialize_execute");
    removeAllNoThrow(root);
    const RemovePathOnExit remove_root{root};

    std::string error;
    auto source_buffer = makeSourceBuffer(root, controller.running_session.prepared_session.session_id, &error);
    ASSERT_TRUE(source_buffer.valid()) << error;
    controller.owned_buffers.push_back(std::move(source_buffer));

    const auto resized_output_image = makeRgbGradientImage(9, 7);
    ASSERT_TRUE(resized_output_image);

    an::ui::extensions::SharedBufferManager shared_buffer_manager(root);
    an::ui::extensions::SharedImageBuffer output_buffer;
    ASSERT_TRUE(exportImageOrReport(shared_buffer_manager, *resized_output_image,
                                    controller.running_session.prepared_session.session_id,
                                    an::ui::extensions::SharedBufferRole::Destination, &output_buffer, &error,
                                    "resize-aware-output", "destination"))
        << error;

    an::ui::extensions::ViewCommitMessage view_commit;
    view_commit.session_id = controller.running_session.prepared_session.session_id;
    view_commit.request_id = "resize-aware-commit-request";
    view_commit.view_id = controller.running_session.prepared_session.session_id;
    view_commit.result_buffers = {output_buffer.descriptor()};
    controller.running_session.last_view_commit = view_commit;

    std::optional<an::ui::extensions::ExtensionSessionController::LandedMaterialization> refused =
        landExecuteMaterialization(controller);
    ASSERT_TRUE(refused.has_value());
    ASSERT_FALSE(refused->image.hasValue());
    EXPECT_NE(std::string(refused->image.status().message()).find("mismatched"), std::string::npos)
        << refused->image.status().message();

    view_commit.allow_resized_output = true;
    controller.running_session.last_view_commit = view_commit;
    std::optional<an::ui::extensions::ExtensionSessionController::LandedMaterialization> landed =
        landExecuteMaterialization(controller);
    ASSERT_TRUE(landed.has_value());
    ASSERT_TRUE(landed->image.hasValue()) << landed->image.status().message();
    EXPECT_EQ(widthOf(*landed->image), widthOf(*resized_output_image));
    EXPECT_EQ(heightOf(*landed->image), heightOf(*resized_output_image));
    EXPECT_EQ(channelsOf(*landed->image), channelsOf(*resized_output_image));

    output_buffer = an::ui::extensions::SharedImageBuffer{};
    controller.owned_buffers.clear();

    const an::Status removed =
        shared_buffer_manager.removeSessionBuffers(controller.running_session.prepared_session.session_id);
    EXPECT_TRUE(removed.ok()) << removed.message();
}

// A dispatched execute is shown as running until the window's handles are detached.
TEST(ExtensionHostManagerTest, ApplyExecuteDispatchResultCachesAndClearsStateWithoutProcessControlOnDetach)
{
    an::ui::extensions::ExtensionSessionController controller;
    controller.descriptor_id = "filters.shellless_execute_dispatch_state";
    controller.display_name = "Shellless Execute Dispatch State";
    controller.running_session.prepared_session.session_id = "shellless-execute-dispatch-session";

    an::ui::extensions::ExtensionHostExecutionDispatchResult dispatch_result;
    dispatch_result.state = an::ui::extensions::ExtensionHostExecutionDispatchState::Started;
    dispatch_result.request_id = "execute-request-id";

    controller.applyExecuteDispatchResult(dispatch_result, {});

    ASSERT_TRUE(controller.last_execute_dispatch_result.has_value());
    EXPECT_EQ(controller.last_execute_dispatch_result->state,
              an::ui::extensions::ExtensionHostExecutionDispatchState::Started);
    EXPECT_EQ(controller.last_execute_dispatch_result->request_id, dispatch_result.request_id);
    EXPECT_FALSE(controller.execute_enabled);
    ASSERT_TRUE(controller.current_process_message.has_value());
    EXPECT_EQ(controller.current_process_message->title, QStringLiteral("Extension Running"));
    EXPECT_EQ(controller.current_process_message->message, QStringLiteral("Extension execution is in progress."));
    EXPECT_EQ(controller.current_process_message->tone,
              an::ui::extensions::ExtensionSessionController::ProcessMessageState::Tone::Info);

    controller.detachUiHandles();

    EXPECT_FALSE(controller.last_execute_dispatch_result.has_value());
    EXPECT_FALSE(controller.current_process_message.has_value());
}

// A pending preview shows its message until a preview is presented.
TEST(ExtensionHostManagerTest, MarkPreviewRefreshPendingAndPresentationManageMessageStateWithoutProcessControl)
{
    an::ui::extensions::ExtensionSessionController controller;
    controller.descriptor_id = "filters.shellless_preview_message_state";
    controller.display_name = "Shellless Preview Message State";
    controller.target_control = fakeTargetControl();
    controller.running_session.prepared_session.session_id = "shellless-preview-message-session";

    controller.markPreviewRefreshPending();

    ASSERT_TRUE(controller.current_process_message.has_value());
    EXPECT_EQ(controller.current_process_message->title, QStringLiteral("Extension Preview"));
    EXPECT_EQ(controller.current_process_message->message, QStringLiteral("Preview refresh is pending."));
    EXPECT_EQ(controller.current_process_message->tone,
              an::ui::extensions::ExtensionSessionController::ProcessMessageState::Tone::Info);
    EXPECT_TRUE(controller.preview_request_pending);

    const auto output_image = makeRgbGradientImage(4, 3);
    ASSERT_TRUE(output_image);

    controller.applyPreviewPresentation(std::move(*output_image), {});

    EXPECT_FALSE(controller.current_process_message.has_value());
}

// An execute that waits for its image says so, is asked again only while its session is active and nothing is in
// flight, stops waiting once it is dispatched or could not be sent, and is let go with the session.
TEST(ExtensionHostManagerTest, AWaitingExecuteIsRetriedOnlyWhileItsSessionCanTakeItAndEndsWithTheSession)
{
    an::ui::extensions::ExtensionSessionController controller;
    controller.descriptor_id = "filters.shellless_execute_waiting";
    controller.display_name = "Shellless Execute Waiting";
    controller.target_control = fakeTargetControl();
    controller.running_session.prepared_session.session_id = "shellless-execute-waiting-session";
    controller.running_session.state = an::ui::extensions::ExtensionHostSessionState::Active;

    EXPECT_FALSE(controller.shouldRetryPendingExecute()) << "no execute was asked for";

    controller.markExecuteWaitingForImage();
    EXPECT_TRUE(controller.execute_request_pending);
    EXPECT_TRUE(controller.shouldRetryPendingExecute());
    ASSERT_TRUE(controller.current_process_message.has_value());
    EXPECT_EQ(controller.current_process_message->title, QStringLiteral("Extension Waiting"));
    EXPECT_EQ(controller.current_process_message->message,
              QStringLiteral("Execution waits for the image being written."));
    EXPECT_EQ(controller.current_process_message->tone,
              an::ui::extensions::ExtensionSessionController::ProcessMessageState::Tone::Info);

    controller.running_session.in_flight_request_id = "preview-in-flight";
    EXPECT_FALSE(controller.shouldRetryPendingExecute()) << "the host takes no execute while a request is in flight";
    controller.running_session.in_flight_request_id.clear();

    controller.running_session.state = an::ui::extensions::ExtensionHostSessionState::Unresponsive;
    EXPECT_FALSE(controller.shouldRetryPendingExecute()) << "a session that is not active is sent nothing";
    controller.running_session.state = an::ui::extensions::ExtensionHostSessionState::Active;
    EXPECT_TRUE(controller.shouldRetryPendingExecute());

    an::ui::extensions::ExtensionHostExecutionDispatchResult started;
    started.state = an::ui::extensions::ExtensionHostExecutionDispatchState::Started;
    started.request_id = "execute-request-id";
    controller.applyExecuteDispatchResult(started, {});
    EXPECT_FALSE(controller.execute_request_pending);
    EXPECT_FALSE(controller.shouldRetryPendingExecute()) << "a dispatched execute no longer waits";

    an::tests::processes::ProcessTestContext& context = an::tests::processes::framework::suiteContext();
    const qsizetype errors_before = context.errorTexts.size();
    controller.markExecuteWaitingForImage();
    controller.reportPendingExecuteFailure("The selected image does not contain loaded image data");
    EXPECT_FALSE(controller.shouldRetryPendingExecute()) << "an execute that could not be sent no longer waits";
    ASSERT_TRUE(controller.current_process_message.has_value());
    EXPECT_EQ(controller.current_process_message->tone,
              an::ui::extensions::ExtensionSessionController::ProcessMessageState::Tone::Error);
    ASSERT_EQ(context.errorTexts.size(), errors_before + 1) << "the failure is said to the person";
    EXPECT_EQ(context.errorTexts.back(), QStringLiteral("The selected image does not contain loaded image data"));

    controller.markExecuteWaitingForImage();
    controller.detachUiHandles();
    EXPECT_FALSE(controller.execute_request_pending);
    EXPECT_FALSE(controller.shouldRetryPendingExecute()) << "a stopped session sends nothing";
}

// Consuming a preview result while another is queued keeps the pending message.
TEST(ExtensionHostManagerTest, MarkPreviewResultConsumedPreservesPendingRefreshMessageWhenRestartQueued)
{
    an::ui::extensions::ExtensionSessionController controller;
    controller.descriptor_id = "filters.shellless_preview_pending_refresh";
    controller.display_name = "Shellless Preview Pending Refresh";
    controller.running_session.prepared_session.session_id = "shellless-preview-pending-refresh-session";

    an::ui::extensions::ExtensionHostExecutionDispatchResult dispatch_result;
    dispatch_result.state = an::ui::extensions::ExtensionHostExecutionDispatchState::Started;
    dispatch_result.request_id = "preview-request-id";
    controller.applyPreviewDispatchResult(dispatch_result, {});

    controller.markPreviewRefreshPending();
    ASSERT_TRUE(controller.current_process_message.has_value());
    EXPECT_EQ(controller.current_process_message->message, QStringLiteral("Preview refresh is pending."));
    EXPECT_TRUE(controller.preview_request_pending);

    an::ui::extensions::ExecutionResultMessage preview_result;
    preview_result.session_id = controller.running_session.prepared_session.session_id;
    preview_result.target_request_id = dispatch_result.request_id;
    preview_result.result_kind = "preview";
    controller.running_session.last_preview_result = preview_result;

    controller.markPreviewResultConsumed();

    EXPECT_FALSE(controller.last_preview_dispatch_result.has_value());
    ASSERT_TRUE(controller.current_process_message.has_value());
    EXPECT_EQ(controller.current_process_message->message, QStringLiteral("Preview refresh is pending."));
}
