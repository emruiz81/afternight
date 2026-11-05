// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"

#include "app/ExtensionHostManager.h"
#include "app/ExtensionCommon.h"
#include "app/ReadRequests.h"
#include "app/ExtensionProtocolFraming.h"
#include "controls/IAppContext.h"
#include "controls/ExtensionUiSchemaUtils.h"
#include "controls/ImageCropController.h"
#include "controls/ImageViewControl.h"
#include "widgets/ImageViewWidget.h"
#include "controls/ProcessExecution.h"
#include "controls/settings/ProcessingLimits.h"
#include "session/IEngineSessionProvider.h"
#include "session/ImageDocument.h"

#include <AfterNightConsumerRuntime/EngineSession.h>
#include <AfterNightConsumerRuntime/ImageAsset.h>
#include <AfterNightConsumerRuntime/OperationScope.h>

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Image/ImageDescriptor.h>

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QLocalServer>
#include <QLocalSocket>
#include <QProcess>
#include <QProcessEnvironment>
#include <QStringList>
#include <QThreadPool>
#include <QTimer>
#include <QUuid>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <filesystem>
#include <unordered_map>
#include <unordered_set>
#include <memory>
#include <mutex>
#include <optional>
#include <string_view>
#include <utility>
#include <cstdint>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <signal.h>
#include <sys/prctl.h>
#include <unistd.h>
#endif

namespace
{

using an::ui::extensions::constantTimeStringEqual;
using an::ui::extensions::generateOpaqueToken;
using an::ui::extensions::isRegularFile;

// How long a host's runtime may take to start - the backend, the interpreter and the extension's import, which a first
// start from a freshly provisioned environment makes long - before the launch or the session gives up on it.
constexpr int kRuntimeStartTimeoutMs = 60000;

struct SeededExecutionParamField
{
    QString type;
    QCborValue default_value;
};

bool isSupportedSeededExecutionParamType(const QString& type)
{
    static const QSet<QString> kSupportedTypes{
        QStringLiteral("action"),
        QStringLiteral("bool"),
        QStringLiteral("button"),
        QStringLiteral("choice"),
        QStringLiteral("curve_editor"),
        QStringLiteral("dir_path"),
        QStringLiteral("file_path"),
        QStringLiteral("float"),
        QStringLiteral("info"),
        QStringLiteral("int"),
        QStringLiteral("int_slider"),
        QStringLiteral("manual_mask_editor"),
        QStringLiteral("meta"),
        QStringLiteral("percentage_mix_label"),
        QStringLiteral("rt_histogram"),
        QStringLiteral("section"),
        QStringLiteral("slider"),
        QStringLiteral("string"),
        QStringLiteral("tab"),
        QStringLiteral("tabs"),
        QStringLiteral("target_selector"),
        QStringLiteral("value_description_label"),
        QStringLiteral("vector_slope"),
        QStringLiteral("view_selector"),
        QStringLiteral("end_tabs"),
    };
    return kSupportedTypes.contains(type);
}

bool isHeadlessQtPlatform()
{
    const QString platform_name = QGuiApplication::platformName().trimmed().toLower();
    return platform_name == QStringLiteral("minimal") || platform_name == QStringLiteral("offscreen") ||
           platform_name == QStringLiteral("minimalegl") || platform_name == QStringLiteral("headless");
}

bool isDisplayOnlySeededExecutionParamType(const QString& type)
{
    return type == QStringLiteral("section") || type == QStringLiteral("info") || type == QStringLiteral("meta") ||
           type == QStringLiteral("button") || type == QStringLiteral("action") ||
           type == QStringLiteral("manual_mask_editor") || type == QStringLiteral("rt_histogram") ||
           type == QStringLiteral("vector_slope") || type == QStringLiteral("percentage_mix_label") ||
           type == QStringLiteral("value_description_label") || type == QStringLiteral("tab") ||
           type == QStringLiteral("tabs") || type == QStringLiteral("target_selector") ||
           type == QStringLiteral("end_tabs");
}

void applyPersistedExtensionSettingDefaultsToAutoSchema(QJsonArray* schema, const std::string_view extension_id,
                                                        an::ui::extensions::ExtensionSdkService& sdk_service)
{
    if (!schema || extension_id.empty())
    {
        return;
    }

    QStringList setting_keys;
    std::string error;
    if (!sdk_service.selectExtensionSettingKeys(extension_id, &setting_keys, &error))
    {
        return;
    }

    for (qsizetype index = 0; index < schema->size(); ++index)
    {
        QJsonObject definition = schema->at(index).toObject();
        if (definition.isEmpty())
        {
            continue;
        }

        const QString key = definition.value(QStringLiteral("id")).toString().trimmed();
        if (key.isEmpty() || !setting_keys.contains(key))
        {
            continue;
        }

        QCborValue value;
        if (!sdk_service.selectExtensionSetting(extension_id, key.toStdString(), &value, QCborValue(), &error))
        {
            continue;
        }

        if (value.isNull() || value.isUndefined())
        {
            continue;
        }

        const QString type =
            an::ui::extensions::schema_utils::normalizedParamType(definition.value(QStringLiteral("type")).toString());
        if (type.isEmpty() || isDisplayOnlySeededExecutionParamType(type))
        {
            continue;
        }

        const SeededExecutionParamField field{
            type, an::ui::extensions::schema_utils::jsonValueToCbor(definition.value(QStringLiteral("default")))};
        const QCborValue coerced_value = an::ui::extensions::schema_utils::coerceSchemaFieldValue(field, value);
        definition.insert(QStringLiteral("default"), QJsonValue::fromVariant(coerced_value.toVariant()));
        (*schema)[index] = definition;
    }
}

QString inspectSettingsSnapshotCborBase64(const std::string_view extension_id,
                                          an::ui::extensions::ExtensionSdkService& sdk_service)
{
    if (extension_id.empty())
    {
        return {};
    }

    QStringList setting_keys;
    std::string error;
    if (!sdk_service.selectExtensionSettingKeys(extension_id, &setting_keys, &error) || setting_keys.isEmpty())
    {
        return {};
    }

    QCborMap snapshot;
    for (const QString& key : setting_keys)
    {
        QCborValue value;
        if (!sdk_service.selectExtensionSetting(extension_id, key.toStdString(), &value, QCborValue(), &error))
        {
            continue;
        }

        if (!value.isUndefined())
        {
            snapshot.insert(key, value);
        }
    }

    if (snapshot.isEmpty())
    {
        return {};
    }

    const QByteArray payload =
        QCborValue(snapshot).toCbor().toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
    return QString::fromLatin1(payload);
}

std::string extensionSettingsFingerprint(an::ui::extensions::ExtensionSdkService& sdk_service,
                                         const std::string_view extension_id)
{
    const QString snapshot = inspectSettingsSnapshotCborBase64(extension_id, sdk_service);
    if (snapshot.isEmpty())
    {
        return {};
    }

    return QCryptographicHash::hash(snapshot.toUtf8(), QCryptographicHash::Sha256).toHex().toStdString();
}

bool buildSeededExecutionParamsFromAutoSchema(const QJsonArray& schema, QCborMap* out_params, std::string* error)
{
    if (!out_params)
    {
        if (error)
        {
            *error = "Seeded execution params output is null";
        }
        return false;
    }

    out_params->clear();
    for (const QJsonValue& value : schema)
    {
        if (!value.isObject())
        {
            if (error)
            {
                *error = "Extension parameter schema entries must be objects";
            }
            return false;
        }

        const QJsonObject definition = value.toObject();
        const QString id = definition.value(QStringLiteral("id")).toString().trimmed();
        const QString type =
            an::ui::extensions::schema_utils::normalizedParamType(definition.value(QStringLiteral("type")).toString());
        if (id.isEmpty() || type.isEmpty())
        {
            if (error)
            {
                *error = "Extension parameter schema entries require non-empty id and type fields";
            }
            return false;
        }
        if (!isSupportedSeededExecutionParamType(type))
        {
            if (error)
            {
                *error = "Unsupported extension parameter type: " + type.toStdString();
            }
            return false;
        }
        if (isDisplayOnlySeededExecutionParamType(type))
        {
            continue;
        }

        const SeededExecutionParamField field{
            type, an::ui::extensions::schema_utils::jsonValueToCbor(definition.value(QStringLiteral("default")))};
        out_params->insert(id, an::ui::extensions::schema_utils::coerceSchemaFieldValue(field, field.default_value));
    }

    return true;
}

constexpr std::string_view kDefaultExtensionHistoryStepName = "Extension Apply";
constexpr std::string_view kDefaultExtensionHistoryDescription = "Applied extension execution result";

bool isGenericExtensionHistoryStepName(const std::string_view value) noexcept
{
    return value.empty() || value == kDefaultExtensionHistoryStepName;
}

bool isGenericExtensionHistoryDescription(const std::string_view value) noexcept
{
    return value.empty() || value == kDefaultExtensionHistoryDescription;
}

QString cborValueToExtensionHistoryString(const QCborValue& value)
{
    if (value.isString())
    {
        return value.toString();
    }
    if (value.isBool())
    {
        return value.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    }
    if (value.isInteger())
    {
        return QString::number(value.toInteger());
    }
    if (value.isDouble())
    {
        return QString::number(value.toDouble(), 'g', 12);
    }
    if (value.isNull())
    {
        return QStringLiteral("null");
    }
    if (value.isArray() || value.isMap())
    {
        const QJsonDocument document = value.isArray() ? QJsonDocument::fromVariant(value.toVariant().toList())
                                                       : QJsonDocument::fromVariant(value.toVariant().toMap());
        return QString::fromUtf8(document.toJson(QJsonDocument::Compact));
    }
    return value.toVariant().toString();
}

std::vector<std::pair<QString, QCborValue>> visibleExtensionHistoryParams(const QCborMap& params)
{
    std::vector<std::pair<QString, QCborValue>> entries;
    entries.reserve(static_cast<std::size_t>(params.size()));
    for (auto it = params.begin(); it != params.end(); ++it)
    {
        const QString key = it.key().toString().trimmed();
        if (key.isEmpty() || key.startsWith(QStringLiteral("_")) || it.value().isUndefined())
        {
            continue;
        }

        entries.emplace_back(key, it.value());
    }

    std::sort(entries.begin(), entries.end(), [](const auto& lhs, const auto& rhs) {
        return QString::compare(lhs.first, rhs.first, Qt::CaseInsensitive) < 0;
    });
    return entries;
}

bool hasVisibleExtensionHistoryParams(const QCborMap& params)
{
    for (auto it = params.begin(); it != params.end(); ++it)
    {
        const QString key = it.key().toString().trimmed();
        if (!key.isEmpty() && !key.startsWith(QStringLiteral("_")) && !it.value().isUndefined())
        {
            return true;
        }
    }
    return false;
}

std::string formatExtensionHistoryParams(const QCborMap& params)
{
    const auto entries = visibleExtensionHistoryParams(params);
    if (entries.empty())
    {
        return "Parameters used: none";
    }

    QStringList lines;
    lines.reserve(static_cast<qsizetype>(entries.size() + 1u));
    lines.push_back(QStringLiteral("Parameters used:"));
    for (const auto& [key, value] : entries)
    {
        lines.push_back(QStringLiteral("%1: %2").arg(key, cborValueToExtensionHistoryString(value)));
    }
    return lines.join(QStringLiteral("\n")).toStdString();
}

std::string resolveExtensionHistoryStepName(const std::string& process_display_name,
                                            const std::string& committed_step_name)
{
    if (!isGenericExtensionHistoryStepName(committed_step_name))
    {
        return committed_step_name;
    }
    if (!process_display_name.empty())
    {
        return process_display_name;
    }
    return std::string(kDefaultExtensionHistoryStepName);
}

std::string resolveExtensionHistoryDescription(const std::string& committed_description, const QCborMap& params)
{
    if (isGenericExtensionHistoryDescription(committed_description))
    {
        return formatExtensionHistoryParams(params);
    }
    if (!hasVisibleExtensionHistoryParams(params) ||
        committed_description.find("Parameters used:") != std::string::npos)
    {
        return committed_description;
    }

    return committed_description + "\n\n" + formatExtensionHistoryParams(params);
}

an::ui::IDialogService* currentDialogService()
{
    if (auto* context = an::ui::appContext())
    {
        return context->getDialogService();
    }

    return nullptr;
}

an::ui::IAppEventBus* currentAppEventBus()
{
    if (auto* context = an::ui::appContext())
    {
        return context->getAppEventBus();
    }

    return nullptr;
}

// The engine session the application lends every Controls object; null before a context is installed.
const an::consumer::EngineSession* currentEngineSession()
{
    an::ui::IAppContext* const context = an::ui::appContext();
    if (!context)
    {
        return nullptr;
    }
    an::ui::IEngineSessionProvider* const provider = context->getEngineSessionProvider();
    return provider ? &provider->engineSession() : nullptr;
}

struct SessionSecretPipe
{
#ifdef _WIN32
    HANDLE read_handle = INVALID_HANDLE_VALUE;
    HANDLE write_handle = INVALID_HANDLE_VALUE;
#else
    int read_fd = -1;
    int write_fd = -1;
#endif

    ~SessionSecretPipe()
    {
#ifdef _WIN32
        if (read_handle != INVALID_HANDLE_VALUE)
        {
            ::CloseHandle(read_handle);
        }
        if (write_handle != INVALID_HANDLE_VALUE)
        {
            ::CloseHandle(write_handle);
        }
#else
        if (read_fd >= 0)
        {
            ::close(read_fd);
        }
        if (write_fd >= 0)
        {
            ::close(write_fd);
        }
#endif
    }
};

#ifdef _WIN32
void closeHandleNoThrow(HANDLE* handle) noexcept
{
    if (!handle || *handle == INVALID_HANDLE_VALUE)
    {
        return;
    }

    ::CloseHandle(*handle);
    *handle = INVALID_HANDLE_VALUE;
}

void closeJobHandleNoThrow(void** job_handle) noexcept
{
    if (!job_handle || *job_handle == nullptr)
    {
        return;
    }

    ::CloseHandle(static_cast<HANDLE>(*job_handle));
    *job_handle = nullptr;
}

bool assignProcessToKillOnCloseJob(QProcess& process, void** out_job_handle, std::string* error)
{
    if (!out_job_handle)
    {
        if (error)
        {
            *error = "Process job output handle is null";
        }
        return false;
    }

    const DWORD process_id = static_cast<DWORD>(process.processId());
    HANDLE process_handle =
        ::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_SET_QUOTA | PROCESS_TERMINATE, FALSE, process_id);
    if (process_handle == nullptr)
    {
        if (error)
        {
            *error = "Failed to open the extension host process for Job Object assignment";
        }
        return false;
    }

    HANDLE job_handle = ::CreateJobObjectW(nullptr, nullptr);
    if (job_handle == nullptr)
    {
        ::CloseHandle(process_handle);
        if (error)
        {
            *error = "Failed to create a Job Object for the extension host session";
        }
        return false;
    }

    JOBOBJECT_EXTENDED_LIMIT_INFORMATION job_info{};
    job_info.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!::SetInformationJobObject(job_handle, JobObjectExtendedLimitInformation, &job_info, sizeof(job_info)))
    {
        ::CloseHandle(process_handle);
        ::CloseHandle(job_handle);
        if (error)
        {
            *error = "Failed to configure kill-on-close semantics for the extension host Job Object";
        }
        return false;
    }

    if (!::AssignProcessToJobObject(job_handle, process_handle))
    {
        ::CloseHandle(process_handle);
        ::CloseHandle(job_handle);
        if (error)
        {
            *error = "Failed to assign the extension host process to its Job Object";
        }
        return false;
    }

    ::CloseHandle(process_handle);
    *out_job_handle = job_handle;
    return true;
}

void terminateProcessTree(QProcess& process, void** job_handle) noexcept
{
    if (job_handle && *job_handle != nullptr)
    {
        HANDLE native_job_handle = static_cast<HANDLE>(*job_handle);
        ::TerminateJobObject(native_job_handle, 1);
        ::CloseHandle(native_job_handle);
        *job_handle = nullptr;
    }

    if (process.state() == QProcess::Running)
    {
        process.waitForFinished(100);
        if (process.state() == QProcess::Running)
        {
            process.kill();
            process.waitForFinished(100);
        }
    }
}

void killProcessTreeNoWait(QProcess& process, void** job_handle) noexcept
{
    if (job_handle && *job_handle != nullptr)
    {
        HANDLE native_job_handle = static_cast<HANDLE>(*job_handle);
        ::TerminateJobObject(native_job_handle, 1);
        ::CloseHandle(native_job_handle);
        *job_handle = nullptr;
        return;
    }

    if (process.state() == QProcess::Running)
    {
        process.kill();
    }
}

bool createSessionSecretPipe(SessionSecretPipe* session_secret_pipe, std::string* error)
{
    if (!session_secret_pipe)
    {
        if (error)
        {
            *error = "Session-secret pipe output is null";
        }
        return false;
    }

    SECURITY_ATTRIBUTES attributes{};
    attributes.nLength = sizeof(attributes);
    attributes.lpSecurityDescriptor = nullptr;
    attributes.bInheritHandle = TRUE;
    if (!::CreatePipe(&session_secret_pipe->read_handle, &session_secret_pipe->write_handle, &attributes, 0))
    {
        if (error)
        {
            *error = "Failed to create the session-secret pipe";
        }
        return false;
    }

    if (!::SetHandleInformation(session_secret_pipe->write_handle, HANDLE_FLAG_INHERIT, 0))
    {
        if (error)
        {
            *error = "Failed to configure the session-secret pipe";
        }
        return false;
    }

    return true;
}

bool writeAllToHandle(const HANDLE handle, const std::string_view value, std::string* error)
{
    std::size_t written = 0;
    while (written < value.size())
    {
        DWORD chunk_written = 0;
        const DWORD chunk_size =
            static_cast<DWORD>((std::min<std::size_t>)(value.size() - written, static_cast<std::size_t>(0x7fffffff)));
        if (!::WriteFile(handle, value.data() + written, chunk_size, &chunk_written, nullptr))
        {
            if (error)
            {
                *error = "Failed to write the session secret to the inherited pipe";
            }
            return false;
        }

        written += static_cast<std::size_t>(chunk_written);
    }

    return true;
}
#else
// The extension host executable reads its session secret from file
// descriptor 3.  The parent (launchSession) creates an anonymous pipe,
// dup2()s the read end to fd 3, writes the secret to the write end,
// then closes both ends before the handshake proceeds.  This is a
// deliberate low-numbered fd so it is unlikely to collide with
// stdin/stdout/stderr and the child does not need environment variables
// or command-line arguments to locate it.
constexpr int kSessionSecretPipeDescriptor = 3;

void closeFdNoThrow(int* fd) noexcept
{
    if (!fd || *fd < 0)
    {
        return;
    }

    ::close(*fd);
    *fd = -1;
}

bool setCloseOnExecFlag(const int fd, const bool enabled, std::string* error)
{
    const int current_flags = ::fcntl(fd, F_GETFD);
    if (current_flags == -1)
    {
        if (error)
        {
            *error = "Failed to inspect the session-secret pipe descriptor";
        }
        return false;
    }

    const int next_flags = enabled ? (current_flags | FD_CLOEXEC) : (current_flags & ~FD_CLOEXEC);
    if (::fcntl(fd, F_SETFD, next_flags) == -1)
    {
        if (error)
        {
            *error = "Failed to configure the session-secret pipe descriptor";
        }
        return false;
    }

    return true;
}

bool createSessionSecretPipe(SessionSecretPipe* session_secret_pipe, std::string* error)
{
    if (!session_secret_pipe)
    {
        if (error)
        {
            *error = "Session-secret pipe output is null";
        }
        return false;
    }

    int pipe_fds[2] = {-1, -1};
    if (::pipe(pipe_fds) == -1)
    {
        if (error)
        {
            *error = "Failed to create the session-secret pipe";
        }
        return false;
    }

    session_secret_pipe->read_fd = pipe_fds[0];
    session_secret_pipe->write_fd = pipe_fds[1];
    if (!setCloseOnExecFlag(session_secret_pipe->write_fd, true, error))
    {
        return false;
    }

    return true;
}

bool writeAllToFd(const int fd, const std::string_view value, std::string* error)
{
    std::size_t written = 0;
    while (written < value.size())
    {
        const ssize_t chunk = ::write(fd, value.data() + written, value.size() - written);
        if (chunk < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }

            if (error)
            {
                *error = "Failed to write the session secret to the inherited pipe";
            }
            return false;
        }

        written += static_cast<std::size_t>(chunk);
    }

    return true;
}

void terminateProcessTree(QProcess& process) noexcept
{
    if (process.state() != QProcess::Running)
    {
        return;
    }

    const pid_t process_id = static_cast<pid_t>(process.processId());
    if (process_id > 0)
    {
        ::kill(-process_id, SIGTERM);
        if (!process.waitForFinished(100))
        {
            ::kill(-process_id, SIGKILL);
            process.waitForFinished(100);
        }
        return;
    }

    process.terminate();
    if (!process.waitForFinished(100))
    {
        process.kill();
        process.waitForFinished(100);
    }
}

void killProcessTreeNoWait(QProcess& process) noexcept
{
    if (process.state() != QProcess::Running)
    {
        return;
    }

    const pid_t process_id = static_cast<pid_t>(process.processId());
    if (process_id > 0)
    {
        ::kill(-process_id, SIGKILL);
        return;
    }

    process.kill();
}
#endif

QString commaSeparatedCapabilities(const std::vector<std::string>& capabilities)
{
    QStringList values;
    for (const auto& capability : capabilities)
    {
        values.push_back(QString::fromStdString(capability));
    }
    return values.join(QStringLiteral(","));
}

bool isSupportedSdkBackend(const std::string_view sdk_backend) noexcept
{
    return sdk_backend == "runtime" || sdk_backend == "protocol";
}

std::filesystem::path defaultExtensionHostExecutableForBackend(const std::string_view sdk_backend,
                                                               const std::filesystem::path& app_dir)
{
    if (sdk_backend == "protocol")
    {
        return an::ui::extensions::bundled_extension_host_lite_executable_for_app_dir(app_dir);
    }

    return an::ui::extensions::bundled_extension_host_executable_for_app_dir(app_dir);
}

// Whether a launch asks its host to connect before it starts the extension's runtime: the full host is asked when it
// owns the process window, as every runtime session the application makes does, and the Lite host always is. The
// launch then waits for nothing the extension's start does, and the session reports the start once it ends.
bool connectsBeforeRuntimeStart(const an::ui::extensions::ExtensionHostLaunchBinding& binding)
{
    return binding.extension_host_owns_process_window || binding.sdk_backend != "runtime";
}

std::string defaultExecuteParamsToken(const std::string_view descriptor_id)
{
    return descriptor_id.empty() ? "extension.execute" : std::string(descriptor_id) + ".execute";
}

std::string defaultPreviewParamsToken(const std::string_view descriptor_id)
{
    return descriptor_id.empty() ? "extension.preview" : std::string(descriptor_id) + ".preview";
}

bool hasCapability(const std::vector<std::string>& capabilities, const std::string_view capability)
{
    return std::find(capabilities.begin(), capabilities.end(), capability) != capabilities.end();
}

struct PreviewSubArea
{
    QRect rect;
    int source_width = 0;
    int source_height = 0;
};

std::optional<PreviewSubArea>
resolvePreviewSubArea(const an::ui::extensions::ExtensionSessionController& session, const an::ImageDescriptor& image,
                      const an::ui::extensions::ExtensionHostLaunchCoordinator::ManagedPreviewSubAreaProvider& provider)
{
    if (!provider || !session.target_control)
    {
        return std::nullopt;
    }

    const std::optional<QRect> requested = provider(session.target_control);
    if (!requested.has_value() || requested->isEmpty())
    {
        return std::nullopt;
    }

    const QRect image_bounds(0, 0, static_cast<int>(image.extent().width()), static_cast<int>(image.extent().height()));
    const QRect normalized = requested->normalized().intersected(image_bounds);
    if (normalized.isEmpty() || normalized == image_bounds)
    {
        return std::nullopt;
    }

    return PreviewSubArea{normalized, image_bounds.width(), image_bounds.height()};
}

bool parseInspectSchemaOutput(const QByteArray& stdout_bytes, const char* field_name, QJsonArray* out_schema,
                              bool* out_supports_ui_events, bool* out_supports_ui_validation, std::string* error)
{
    if (!out_schema)
    {
        if (error)
        {
            *error = "Extension inspect schema output is null";
        }
        return false;
    }
    if (stdout_bytes.isEmpty())
    {
        if (error)
        {
            *error = "Extension-host schema inspection did not return JSON";
        }
        return false;
    }

    QJsonParseError parse_error;
    const QJsonDocument document = QJsonDocument::fromJson(stdout_bytes, &parse_error);
    if (parse_error.error != QJsonParseError::NoError || !document.isObject())
    {
        if (error)
        {
            *error = "Extension-host schema inspection returned invalid JSON";
        }
        return false;
    }

    const QJsonValue schema_value = document.object().value(QString::fromLatin1(field_name));
    if (!schema_value.isArray())
    {
        if (error)
        {
            *error = std::string("Extension-host schema inspection JSON is missing ") + field_name;
        }
        return false;
    }

    *out_schema = schema_value.toArray();
    if (out_supports_ui_events)
    {
        const QJsonValue supports_ui_events_value = document.object().value(QStringLiteral("supports_ui_events"));
        if (!supports_ui_events_value.isUndefined() && !supports_ui_events_value.isBool())
        {
            if (error)
            {
                *error = "Extension-host schema inspection JSON has a non-bool supports_ui_events field";
            }
            return false;
        }
        *out_supports_ui_events = supports_ui_events_value.isBool() ? supports_ui_events_value.toBool() : true;
    }
    if (out_supports_ui_validation)
    {
        const QJsonValue supports_ui_validation_value =
            document.object().value(QStringLiteral("supports_ui_validation"));
        if (!supports_ui_validation_value.isUndefined() && !supports_ui_validation_value.isBool())
        {
            if (error)
            {
                *error = "Extension-host schema inspection JSON has a non-bool supports_ui_validation field";
            }
            return false;
        }
        *out_supports_ui_validation =
            supports_ui_validation_value.isBool() ? supports_ui_validation_value.toBool() : true;
    }
    return true;
}

struct AutoUiSchemaInspection
{
    QJsonArray schema;
    bool supports_ui_events = true;
    bool supports_ui_validation = true;
};

// The arguments of the host that reads a binding's parameter schema: it imports the extension, answers the schema on
// its output and exits.
QStringList autoUiSchemaInspectionArguments(const an::ui::extensions::ExtensionHostLaunchBinding& binding,
                                            an::ui::extensions::ExtensionSdkService& sdk_service)
{
    QStringList arguments = {QStringLiteral("--inspect"), QStringLiteral("--inspect-params"),
                             QStringLiteral("--sdk-backend"), QString::fromStdString(binding.sdk_backend)};
    for (const auto& argument : binding.host_arguments)
    {
        arguments.push_back(QString::fromStdString(argument));
    }
    const QString inspect_settings_snapshot = inspectSettingsSnapshotCborBase64(binding.extension_id, sdk_service);
    if (!inspect_settings_snapshot.isEmpty())
    {
        arguments.push_back(QStringLiteral("--inspect-settings-snapshot-cbor-base64"));
        arguments.push_back(inspect_settings_snapshot);
    }
    return arguments;
}

// What a schema read that has ended answered; timed_out says the read was stopped at its bound, bound_ms.
bool readAutoUiSchemaInspection(QProcess& inspect, const bool timed_out, const int bound_ms,
                                AutoUiSchemaInspection* out_inspection, std::string* error)
{
    if (timed_out)
    {
        if (error)
        {
            *error = "Extension-host params inspection did not finish within " + std::to_string(bound_ms / 1000) +
                     " seconds";
        }
        return false;
    }
    if (inspect.error() == QProcess::FailedToStart)
    {
        if (error)
        {
            *error = "Failed to start extension-host params inspection";
        }
        return false;
    }

    const QByteArray output = inspect.readAll();
    if (inspect.exitStatus() != QProcess::NormalExit || inspect.exitCode() != 0)
    {
        if (error)
        {
            *error = output.isEmpty() ? std::string("Extension-host params inspection failed")
                                      : output.trimmed().toStdString();
        }
        return false;
    }

    return parseInspectSchemaOutput(output.trimmed(), "params_schema", &out_inspection->schema,
                                    &out_inspection->supports_ui_events, &out_inspection->supports_ui_validation,
                                    error);
}

// A schema read is kept for the host and the arguments it was read with, so a launch with others reads its own.
std::string autoUiSchemaReadKey(const an::ui::extensions::ExtensionHostLaunchBinding& binding)
{
    std::string key = binding.sdk_backend;
    key.push_back('\0');
    key += binding.host_executable.generic_string();
    for (const auto& argument : binding.host_arguments)
    {
        key.push_back('\0');
        key += argument;
    }
    return key;
}

QStringList baseHostArguments(const an::ui::extensions::ExtensionHostPreparedSession& session)
{
    const QString launch_mode = session.launch_mode == an::ui::extensions::ExtensionLaunchMode::Workflow
                                    ? QStringLiteral("workflow")
                                    : QStringLiteral("single_image");
    QStringList arguments = {QStringLiteral("--endpoint-name"), QString::fromStdString(session.endpoint_name),
                             QStringLiteral("--session-id"),    QString::fromStdString(session.session_id),
                             QStringLiteral("--extension-id"),  QString::fromStdString(session.extension_id),
                             QStringLiteral("--sdk-backend"),   QString::fromStdString(session.sdk_backend),
                             QStringLiteral("--launch-mode"),   launch_mode,
                             QStringLiteral("--host-version"),  QString::fromStdString(session.host_version),
                             QStringLiteral("--capabilities"),  commaSeparatedCapabilities(session.capabilities)};

    if (!session.display_name.empty())
    {
        arguments.push_back(QStringLiteral("--display-name"));
        arguments.push_back(QString::fromStdString(session.display_name));
    }
    if (!session.inspect_settings_snapshot_cbor_base64.empty())
    {
        arguments.push_back(QStringLiteral("--inspect-settings-snapshot-cbor-base64"));
        arguments.push_back(QString::fromStdString(session.inspect_settings_snapshot_cbor_base64));
    }

    for (const auto& value : session.host_arguments)
    {
        arguments.push_back(QString::fromStdString(value));
    }

    return arguments;
}

std::optional<std::string>
cachedNativeParamSchemaArgument(const an::ui::extensions::ExtensionHostLaunchBinding& binding)
{
    if (!binding.has_cached_native_param_schema)
    {
        return std::nullopt;
    }

    const QByteArray serialized_schema =
        QJsonDocument(binding.cached_native_param_schema).toJson(QJsonDocument::Compact);
    constexpr int max_cached_schema_bytes = 16 * 1024;
    if (serialized_schema.size() > max_cached_schema_bytes)
    {
        if (an::platform::isLoggingInitialized())
        {
            LOG_WARNING("Skipping cached native extension parameter schema id='{}': {} bytes exceeds {} byte limit",
                        binding.extension_id, serialized_schema.size(), max_cached_schema_bytes);
        }
        return std::nullopt;
    }

    return serialized_schema.toBase64(QByteArray::Base64UrlEncoding).toStdString();
}

an::ui::extensions::RuntimeHelloAckMessage rejectedAck(const an::ui::extensions::ExtensionHostPreparedSession& session,
                                                       const int local_error_code, std::string message,
                                                       std::string details = {})
{
    an::ui::extensions::RuntimeHelloAckMessage ack = session.ack_template;
    ack.accepted = false;
    ack.error = an::ui::extensions::ExtensionProtocolError{
        an::ui::extensions::makeProtocolErrorCode(an::ui::extensions::ExtensionProtocolErrorDomain::Host,
                                                  local_error_code),
        std::move(message), std::move(details)};
    return ack;
}

bool writeAckMessage(QLocalSocket* socket, const int timeout_ms, const an::ui::extensions::RuntimeHelloAckMessage& ack,
                     std::string* error)
{
    if (!socket)
    {
        if (error)
        {
            *error = "Extension host acknowledgement socket is null";
        }
        return false;
    }

    std::string local_error;
    const QByteArray ack_frame =
        an::ui::extensions::frameProtocolMessage(an::ui::extensions::toProtocolMessage(ack), &local_error);
    if (ack_frame.isEmpty() && !local_error.empty())
    {
        if (error)
        {
            *error = local_error;
        }
        return false;
    }

    if (socket->write(ack_frame) != ack_frame.size() || !socket->waitForBytesWritten(timeout_ms))
    {
        if (error)
        {
            *error = "Failed to write the extension host handshake acknowledgement";
        }
        return false;
    }

    return true;
}

bool detectDuplicateConnection(QLocalServer& server, const int handshake_timeout_ms)
{
    if (server.hasPendingConnections())
    {
        return true;
    }

    const int duplicate_grace_ms = std::clamp(handshake_timeout_ms / 20, 10, 50);
    return server.waitForNewConnection(duplicate_grace_ms);
}

enum class HostConnectionWait
{
    Connected,
    Exited,
    TimedOut
};

// Waits for a launched host to connect while it runs, up to a bound: a host that exits first is answered at once rather
// than at the bound, and its output is read as it is written, so a host that prints while it starts never blocks on a
// full pipe.
HostConnectionWait waitForHostConnection(QLocalServer& server, QProcess& process, const int timeout_ms)
{
    constexpr int kPollIntervalMs = 50;
    QElapsedTimer timer;
    timer.start();
    while (true)
    {
        const int remaining_ms = (std::max)(0, timeout_ms - static_cast<int>(timer.elapsed()));
        if (server.waitForNewConnection((std::min)(remaining_ms, kPollIntervalMs)))
        {
            return HostConnectionWait::Connected;
        }
        if (process.state() == QProcess::NotRunning || process.waitForFinished(0))
        {
            return server.hasPendingConnections() ? HostConnectionWait::Connected : HostConnectionWait::Exited;
        }
        if (timer.elapsed() >= timeout_ms)
        {
            return HostConnectionWait::TimedOut;
        }
    }
}

// readFramedMessage is now provided by ExtensionProtocolFraming.h
using an::ui::extensions::readFramedMessage;

std::string processOutput(QProcess& process)
{
    return process.readAllStandardOutput().toStdString();
}

std::string processFailureContext(QProcess& process)
{
    std::string output;
    std::string error;
    if (process.processChannelMode() == QProcess::MergedChannels)
    {
        output = process.readAll().toStdString();
    }
    else
    {
        output = process.readAllStandardOutput().toStdString();
        error = process.readAllStandardError().toStdString();
    }

    const std::string exit_summary = " Exit code: " + std::to_string(process.exitCode()) + ", exit status: " +
                                     (process.exitStatus() == QProcess::NormalExit ? "normal" : "crash");

    if (output.empty() && error.empty())
    {
        return exit_summary;
    }

    std::string context = exit_summary;
    if (!output.empty())
    {
        context += " Host output: " + output;
    }
    if (!error.empty())
    {
        context += " Host error: " + error;
    }
    return context;
}

void terminateProcess(QProcess& process)
{
    terminateProcessTree(process
#ifdef _WIN32
                         ,
                         nullptr
#endif
    );
}

void closePendingConnections(QLocalServer& server)
{
    while (server.hasPendingConnections())
    {
        std::unique_ptr<QLocalSocket> extra_socket(server.nextPendingConnection());
        if (extra_socket)
        {
            extra_socket->disconnectFromServer();
            if (extra_socket->state() != QLocalSocket::UnconnectedState)
            {
                extra_socket->waitForDisconnected(100);
            }
        }
    }
}

bool validatePreparedSession(const an::ui::extensions::ExtensionHostPreparedSession& session, std::string* error)
{
    if (session.host_executable.empty())
    {
        if (error)
        {
            *error = "Extension host executable path is empty";
        }
        return false;
    }
    if (!isRegularFile(session.host_executable))
    {
        if (error)
        {
            *error = "Extension host executable does not exist: " + session.host_executable.generic_string();
        }
        return false;
    }
    if (session.session_id.empty() || session.endpoint_name.empty() || session.session_secret.empty())
    {
        if (error)
        {
            *error = "Prepared extension host session is incomplete";
        }
        return false;
    }
    if (!isSupportedSdkBackend(session.sdk_backend))
    {
        if (error)
        {
            *error = "Resolved SDK backend is unavailable for extension-host launch: " + session.sdk_backend;
        }
        return false;
    }

    return true;
}

std::string extensionLaunchModeName(const an::ui::extensions::ExtensionLaunchMode launch_mode)
{
    using an::ui::extensions::ExtensionLaunchMode;
    return launch_mode == ExtensionLaunchMode::Workflow ? "workflow" : "single_image";
}

void recordSingleImageLaunchFailure(const an::ui::extensions::ExtensionHostLaunchBinding& binding,
                                    const std::string& detail)
{
    an::ui::extensions::ExtensionRuntimeStatusRegistry::instance().recordLaunchFailed(binding.extension_id, detail);
    if (an::platform::isLoggingInitialized())
    {
        LOG_ERROR("Extension launch failed extension id='{}' descriptor id='{}' display='{}' mode='{}' "
                  "reason='{}'",
                  binding.extension_id, binding.descriptor_id, binding.display_name,
                  extensionLaunchModeName(binding.launch_mode), detail);
    }
}

void recordWorkflowLaunchFailure(const an::ui::extensions::ExtensionHostLaunchBinding& binding,
                                 const std::string& detail)
{
    an::ui::extensions::ExtensionRuntimeStatusRegistry::instance().recordLaunchFailed(binding.extension_id, detail);
    if (an::platform::isLoggingInitialized())
    {
        LOG_ERROR("Workflow extension launch failed extension id='{}' descriptor id='{}' display='{}' "
                  "reason='{}'",
                  binding.extension_id, binding.descriptor_id, binding.display_name, detail);
    }
}

void logExtensionHostLaunchFailure(const an::ui::extensions::ExtensionHostPreparedSession& session,
                                   const std::string_view reason)
{
    if (!an::platform::isLoggingInitialized())
    {
        return;
    }

    LOG_ERROR("Extension host launch failed extension id='{}' display='{}' session='{}' mode='{}' host='{}' "
              "reason='{}'",
              session.extension_id, session.display_name, session.session_id,
              extensionLaunchModeName(session.launch_mode), session.host_executable.generic_string(),
              std::string(reason));
}

void logExtensionHostLaunchSuccess(const an::ui::extensions::ExtensionHostPreparedSession& session)
{
    if (!an::platform::isLoggingInitialized())
    {
        return;
    }

    LOG_INFO("Extension host launched extension id='{}' display='{}' session='{}' mode='{}' host='{}'",
             session.extension_id, session.display_name, session.session_id,
             extensionLaunchModeName(session.launch_mode), session.host_executable.generic_string());
}

const an::ui::extensions::SharedBufferDescriptor*
findOwnedBufferDescriptor(const an::ui::extensions::ExtensionSessionController& session,
                          const an::ui::extensions::SharedBufferRole role) noexcept
{
    const auto it = std::find_if(session.owned_buffers.begin(), session.owned_buffers.end(),
                                 [role](const an::ui::extensions::SharedImageBuffer& buffer) {
                                     return buffer.valid() && buffer.descriptor().buffer_role == role;
                                 });
    return it != session.owned_buffers.end() ? &it->descriptor() : nullptr;
}

const an::ui::extensions::SharedBufferDescriptor*
findBufferDescriptorByRole(const std::vector<an::ui::extensions::SharedBufferDescriptor>& descriptors,
                           const an::ui::extensions::SharedBufferRole role) noexcept
{
    const auto it = std::find_if(descriptors.begin(), descriptors.end(),
                                 [role](const an::ui::extensions::SharedBufferDescriptor& descriptor) {
                                     return descriptor.buffer_role == role;
                                 });
    return it != descriptors.end() ? &(*it) : nullptr;
}

const an::ui::extensions::SharedBufferDescriptor*
findBufferDescriptorById(const std::vector<an::ui::extensions::SharedBufferDescriptor>& descriptors,
                         const std::string_view buffer_id) noexcept
{
    const auto it = std::find_if(descriptors.begin(), descriptors.end(),
                                 [buffer_id](const an::ui::extensions::SharedBufferDescriptor& descriptor) {
                                     return descriptor.buffer_id == buffer_id;
                                 });
    return it != descriptors.end() ? &(*it) : nullptr;
}

bool validateDescriptorMatchesExpectedDescriptor(const an::ui::extensions::SharedBufferDescriptor& actual,
                                                 const an::ui::extensions::SharedBufferDescriptor& expected,
                                                 std::string* error)
{
    const auto fail = [&error](const char* field_name) {
        if (error)
        {
            *error = std::string("Extension returned an output mask buffer with a mismatched ") + field_name;
        }
        return false;
    };

    if (actual.buffer_id != expected.buffer_id)
        return fail("buffer id");
    if (actual.symbolic_name != expected.symbolic_name)
        return fail("symbolic name");
    if (actual.mapping_name != expected.mapping_name)
        return fail("mapping name");
    if (actual.mapped_file_path != expected.mapped_file_path)
        return fail("mapped file path");
    if (actual.byte_size != expected.byte_size)
        return fail("byte size");
    if (actual.width != expected.width)
        return fail("width");
    if (actual.height != expected.height)
        return fail("height");
    if (actual.channels != expected.channels)
        return fail("channel count");
    if (actual.pixel_type != expected.pixel_type)
        return fail("pixel type");
    if (actual.pixel_format != expected.pixel_format)
        return fail("pixel format");
    if (actual.color_space != expected.color_space)
        return fail("color space");
    if (actual.row_stride != expected.row_stride)
        return fail("row stride");
    if (actual.plane_stride != expected.plane_stride)
        return fail("plane stride");
    if (actual.access_mode != expected.access_mode)
        return fail("access mode");
    if (actual.buffer_role != expected.buffer_role)
        return fail("role");
    if (actual.offset_x != expected.offset_x)
        return fail("offset_x");
    if (actual.offset_y != expected.offset_y)
        return fail("offset_y");
    if (actual.source_width != expected.source_width)
        return fail("source_width");
    if (actual.source_height != expected.source_height)
        return fail("source_height");
    if (actual.metadata_sidecar != expected.metadata_sidecar)
        return fail("metadata_sidecar");

    return true;
}

bool validateExecutionOutputDescriptor(const std::string_view request_kind, const std::string_view session_id,
                                       const an::ui::extensions::SharedBufferDescriptor* source_descriptor,
                                       const std::vector<an::ui::extensions::SharedBufferDescriptor>* request_buffers,
                                       const an::ui::extensions::SharedBufferDescriptor& output_descriptor,
                                       std::string* error, const bool allow_resized_output = false)
{
    const auto descriptor_validation = an::ui::extensions::validateSharedBufferDescriptor(output_descriptor);
    if (!descriptor_validation.ok())
    {
        if (error)
        {
            *error = "Extension returned an invalid output buffer descriptor: " + descriptor_validation.error;
        }
        return false;
    }

    if (!session_id.empty() && !output_descriptor.mapping_name.empty())
    {
        const std::string expected_prefix = std::string(session_id) + ".";
        if (!std::string_view(output_descriptor.mapping_name).starts_with(expected_prefix))
        {
            if (error)
            {
                *error = "Extension returned an output buffer for an unexpected session mapping";
            }
            return false;
        }
    }

    if (!session_id.empty() && !output_descriptor.mapped_file_path.empty())
    {
        const auto output_path = std::filesystem::path(output_descriptor.mapped_file_path).lexically_normal();
        const auto session_path = output_path.parent_path();
        if (session_path.empty() || session_path.filename() != session_id)
        {
            if (error)
            {
                *error = "Extension returned an output buffer path for an unexpected session";
            }
            return false;
        }
    }

    if (output_descriptor.buffer_role == an::ui::extensions::SharedBufferRole::Mask)
    {
        if (request_kind != "execute")
        {
            if (error)
            {
                *error = "Extension returned an output buffer with an unexpected role";
            }
            return false;
        }
        if (output_descriptor.access_mode != an::ui::extensions::SharedBufferAccessMode::ReadWrite)
        {
            if (error)
            {
                *error = "Extension returned an output mask buffer without writable access mode";
            }
            return false;
        }

        const auto* expected_output_descriptor =
            request_buffers ? findBufferDescriptorById(*request_buffers, output_descriptor.buffer_id) : nullptr;
        if (!expected_output_descriptor ||
            expected_output_descriptor->buffer_role != an::ui::extensions::SharedBufferRole::Mask ||
            expected_output_descriptor->access_mode != an::ui::extensions::SharedBufferAccessMode::ReadWrite)
        {
            if (error)
            {
                *error = "Extension returned an output buffer with an unexpected role";
            }
            return false;
        }

        return validateDescriptorMatchesExpectedDescriptor(output_descriptor, *expected_output_descriptor, error);
    }

    const auto expected_role = request_kind == "preview" ? an::ui::extensions::SharedBufferRole::Preview
                                                         : an::ui::extensions::SharedBufferRole::Destination;
    if (output_descriptor.buffer_role != expected_role)
    {
        if (error)
        {
            *error = "Extension returned an output buffer with an unexpected role";
        }
        return false;
    }
    if (!source_descriptor)
    {
        return true;
    }

    if (output_descriptor.access_mode != an::ui::extensions::SharedBufferAccessMode::ReadWrite)
    {
        if (error)
        {
            *error = "Extension returned an output buffer without writable access mode";
        }
        return false;
    }

    if (allow_resized_output && request_kind == "execute" &&
        output_descriptor.buffer_role == an::ui::extensions::SharedBufferRole::Destination)
    {
        return true;
    }

    const auto expect_same = [&error](const bool condition, const char* message) {
        if (!condition && error)
        {
            *error = message;
        }
        return condition;
    };
    const auto expect_same_optional = [&expect_same](const auto& lhs, const auto& rhs, const char* message) {
        return expect_same(lhs == rhs, message);
    };

    if (!expect_same(output_descriptor.width == source_descriptor->width,
                     "Extension returned an output buffer with a mismatched width") ||
        !expect_same(output_descriptor.height == source_descriptor->height,
                     "Extension returned an output buffer with a mismatched height") ||
        !expect_same(output_descriptor.channels == source_descriptor->channels,
                     "Extension returned an output buffer with a mismatched channel count") ||
        !expect_same(output_descriptor.pixel_type == source_descriptor->pixel_type,
                     "Extension returned an output buffer with a mismatched pixel type") ||
        !expect_same(output_descriptor.pixel_format == source_descriptor->pixel_format,
                     "Extension returned an output buffer with a mismatched pixel format") ||
        !expect_same(output_descriptor.color_space == source_descriptor->color_space,
                     "Extension returned an output buffer with a mismatched color space") ||
        !expect_same(output_descriptor.row_stride == source_descriptor->row_stride,
                     "Extension returned an output buffer with a mismatched row stride") ||
        !expect_same(output_descriptor.plane_stride == source_descriptor->plane_stride,
                     "Extension returned an output buffer with a mismatched plane stride") ||
        !expect_same(output_descriptor.byte_size == source_descriptor->byte_size,
                     "Extension returned an output buffer with a mismatched byte size") ||
        !expect_same_optional(output_descriptor.offset_x, source_descriptor->offset_x,
                              "Extension returned an output buffer with mismatched offset_x") ||
        !expect_same_optional(output_descriptor.offset_y, source_descriptor->offset_y,
                              "Extension returned an output buffer with mismatched offset_y") ||
        !expect_same_optional(output_descriptor.source_width, source_descriptor->source_width,
                              "Extension returned an output buffer with mismatched source_width") ||
        !expect_same_optional(output_descriptor.source_height, source_descriptor->source_height,
                              "Extension returned an output buffer with mismatched source_height") ||
        !expect_same(output_descriptor.metadata_sidecar == source_descriptor->metadata_sidecar,
                     "Extension returned an output buffer with mismatched metadata_sidecar"))
    {
        return false;
    }

    return true;
}

bool validateExecutionResultForSession(const an::ui::extensions::ExtensionHostRunningSession& running_session,
                                       const an::ui::extensions::ExecutionResultMessage& execution_result,
                                       std::string* error)
{
    if (running_session.in_flight_request_kind.empty())
    {
        if (error)
        {
            *error = "Extension host session has no request kind for the in-flight result";
        }
        return false;
    }
    if (execution_result.result_kind != running_session.in_flight_request_kind)
    {
        if (error)
        {
            *error = "Extension host replied with an unexpected execution result kind";
        }
        return false;
    }

    const auto* source_descriptor = findBufferDescriptorByRole(running_session.in_flight_request_buffers,
                                                               an::ui::extensions::SharedBufferRole::Source);
    for (const auto& descriptor : execution_result.output_buffers)
    {
        if (!validateExecutionOutputDescriptor(running_session.in_flight_request_kind,
                                               running_session.prepared_session.session_id, source_descriptor,
                                               &running_session.in_flight_request_buffers, descriptor, error, false))
        {
            return false;
        }
    }

    if (source_descriptor && execution_result.status == "succeeded")
    {
        const auto expected_primary_role = running_session.in_flight_request_kind == "preview"
                                               ? an::ui::extensions::SharedBufferRole::Preview
                                               : an::ui::extensions::SharedBufferRole::Destination;
        if (!findBufferDescriptorByRole(execution_result.output_buffers, expected_primary_role))
        {
            if (error)
            {
                *error = "Extension host did not return the required primary output buffer";
            }
            return false;
        }
    }

    return true;
}

bool validateViewCommitForSession(const an::ui::extensions::ExtensionHostRunningSession& running_session,
                                  const an::ui::extensions::ViewCommitMessage& view_commit, std::string* error)
{
    const bool shellless_native_commit = running_session.handshake.ack.native_process_window &&
                                         running_session.in_flight_request_kind.empty() &&
                                         running_session.in_flight_request_id.empty();
    const bool shellless_lite_commit = !running_session.handshake.ack.native_process_window &&
                                       (running_session.prepared_session.sdk_backend == "protocol" ||
                                        running_session.prepared_session.sdk_backend == "rpc") &&
                                       running_session.in_flight_request_kind.empty() &&
                                       running_session.in_flight_request_id.empty();
    const bool shellless_commit = shellless_native_commit || shellless_lite_commit;

    if (view_commit.session_id != running_session.prepared_session.session_id)
    {
        if (error)
        {
            *error = "Extension host sent view.commit for the wrong session";
        }
        return false;
    }
    if (!shellless_commit && running_session.in_flight_request_kind != "execute")
    {
        if (error)
        {
            *error = "Extension host sent view.commit without an active execute request";
        }
        return false;
    }
    if (!shellless_commit && (running_session.in_flight_request_id.empty() ||
                              view_commit.request_id != running_session.in_flight_request_id))
    {
        if (error)
        {
            *error = "Extension host sent view.commit for an unexpected execute request";
        }
        return false;
    }

    const auto* source_descriptor = findBufferDescriptorByRole(running_session.in_flight_request_buffers,
                                                               an::ui::extensions::SharedBufferRole::Source);
    for (const auto& descriptor : view_commit.result_buffers)
    {
        if (!validateExecutionOutputDescriptor("execute", running_session.prepared_session.session_id,
                                               source_descriptor, &running_session.in_flight_request_buffers,
                                               descriptor, error, view_commit.allow_resized_output))
        {
            return false;
        }
    }

    if (!findBufferDescriptorByRole(view_commit.result_buffers, an::ui::extensions::SharedBufferRole::Destination))
    {
        if (error)
        {
            *error = "Extension host did not return the required primary committed output buffer";
        }
        return false;
    }

    return true;
}

bool validateManagedExecutionOutputDescriptor(const an::ui::extensions::ExtensionSessionController& session,
                                              const std::string_view request_kind,
                                              const an::ui::extensions::SharedBufferDescriptor& output_descriptor,
                                              std::string* error, const bool allow_resized_output = false)
{
    const auto* source_descriptor = findOwnedBufferDescriptor(session, an::ui::extensions::SharedBufferRole::Source);
    if (!source_descriptor && !session.running_session.handshake.ack.native_process_window)
    {
        if (error)
        {
            *error = "Extension session does not have a source buffer descriptor";
        }
        return false;
    }

    // Native-window execute commits are produced locally by the extension host
    // and do not have a managed host-side request buffer set to compare
    // against. In that shellless path, validate the descriptor shape/session
    // without requiring a preserved source descriptor.
    return validateExecutionOutputDescriptor(request_kind, session.running_session.prepared_session.session_id,
                                             source_descriptor, nullptr, output_descriptor, error,
                                             allow_resized_output);
}

std::filesystem::path sharedBufferRootPathForDescriptor(const an::ui::extensions::SharedBufferDescriptor& descriptor)
{
    if (descriptor.mapped_file_path.empty())
    {
        return {};
    }

    const std::filesystem::path file_path = std::filesystem::path(descriptor.mapped_file_path).lexically_normal();
    const std::filesystem::path session_root = file_path.parent_path();
    const std::filesystem::path root_path = session_root.parent_path();
    if (session_root.empty() || root_path.empty())
    {
        return {};
    }

    return root_path;
}

bool validateManagedExecutionOutputDescriptor(const an::ui::extensions::ExtensionSessionController& session,
                                              const an::ui::extensions::ExecutionResultMessage& execution_result,
                                              const an::ui::extensions::SharedBufferDescriptor& output_descriptor,
                                              std::string* error)
{
    return validateManagedExecutionOutputDescriptor(session, execution_result.result_kind, output_descriptor, error,
                                                    false);
}

bool sendRuntimePing(an::ui::extensions::ExtensionHostRunningSession* running_session, std::string* error)
{
    if (!running_session || !running_session->socket)
    {
        if (error)
        {
            *error = "Extension host session has no connected control socket";
        }
        return false;
    }

    an::ui::extensions::RuntimePingMessage ping;
    ping.session_id = running_session->prepared_session.session_id;
    ping.request_id = generateOpaqueToken();
    ping.sequence = running_session->next_ping_sequence++;

    std::string local_error;
    const QByteArray frame =
        an::ui::extensions::frameProtocolMessage(an::ui::extensions::toProtocolMessage(ping), &local_error);
    if (frame.isEmpty() && !local_error.empty())
    {
        if (error)
        {
            *error = local_error;
        }
        return false;
    }

    if (running_session->socket->write(frame) != frame.size() ||
        !running_session->socket->waitForBytesWritten(running_session->prepared_session.handshake_timeout_ms))
    {
        if (error)
        {
            *error = "Failed to write runtime.ping to the extension host";
        }
        return false;
    }

    running_session->ping_outstanding = true;
    running_session->outstanding_ping_request_id = ping.request_id;
    running_session->last_ping_sent_elapsed_ms = running_session->session_clock.elapsed();
    return true;
}

bool writeProtocolMessage(QLocalSocket* socket, const an::ui::extensions::ExtensionProtocolMessage& message,
                          const int timeout_ms, std::string* error)
{
    if (!socket)
    {
        if (error)
        {
            *error = "Extension host session has no connected control socket";
        }
        return false;
    }

    std::string local_error;
    const QByteArray frame = an::ui::extensions::frameProtocolMessage(message, &local_error);
    if (frame.isEmpty() && !local_error.empty())
    {
        if (error)
        {
            *error = local_error;
        }
        return false;
    }

    const qint64 written = socket->write(frame);
    if (written != frame.size() || !socket->waitForBytesWritten(timeout_ms))
    {
        if (error)
        {
            *error = "Failed to write a control message to the extension host" + std::string(" (state=") +
                     std::to_string(static_cast<int>(socket->state())) + ", error=\"" +
                     socket->errorString().toStdString() +
                     "\", bytes_written=" + std::to_string(static_cast<long long>(written)) +
                     ", frame_size=" + std::to_string(static_cast<long long>(frame.size())) +
                     ", bytes_to_write=" + std::to_string(static_cast<long long>(socket->bytesToWrite())) + ")";
        }
        return false;
    }

    return true;
}

bool dispatchSdkServiceMessage(an::ui::extensions::ExtensionSdkService* sdk_service,
                               an::ui::extensions::ExtensionHostRunningSession* running_session,
                               const an::ui::extensions::ExtensionProtocolMessage& message, std::string* error)
{
    if (!sdk_service || !running_session)
    {
        return false;
    }

    an::ui::extensions::ExtensionProtocolMessage response;
    std::string local_error;
    if (!sdk_service->dispatchProtocolMessage(message, running_session->prepared_session.extension_id,
                                              running_session->prepared_session.launch_mode, &response, &local_error))
    {
        if (error)
        {
            *error = local_error;
        }
        return false;
    }
    if (response.type.empty())
    {
        return true;
    }
    if (!writeProtocolMessage(running_session->socket.get(), response,
                              running_session->prepared_session.handshake_timeout_ms, &local_error))
    {
        if (error)
        {
            *error = local_error;
        }
        return false;
    }

    running_session->serviced_sdk_message_count += 1;
    return true;
}

bool writeSdkServiceFallbackResponse(an::ui::extensions::ExtensionHostRunningSession* running_session,
                                     const an::ui::extensions::ExtensionProtocolMessage& request, std::string* error)
{
    if (!running_session)
    {
        if (error)
        {
            *error = "Extension host running session is null";
        }
        return false;
    }

    an::ui::extensions::ExtensionProtocolMessage response;
    response.session_id = request.session_id;
    response.request_id = request.request_id;

    auto& fallback_settings_store = running_session->fallback_settings;

    if (request.type == "logging.event")
    {
        const QString severity = request.fields.value(QStringLiteral("severity")).toString();
        const QString component = request.fields.value(QStringLiteral("component")).toString();
        const QString message = request.fields.value(QStringLiteral("message")).toString();

        if (an::platform::isLoggingInitialized())
        {
            const std::string severity_value = severity.trimmed().toLower().toStdString();
            const std::string component_value = component.toStdString();
            const std::string message_value = message.toStdString();

            if (severity_value == "trace")
            {
                LOG_TRACE("Extension {}: {}", component_value.empty() ? std::string("log") : component_value,
                          message_value);
            }
            else if (severity_value == "debug")
            {
                LOG_DEBUG("Extension {}: {}", component_value.empty() ? std::string("log") : component_value,
                          message_value);
            }
            else if (severity_value == "warning")
            {
                LOG_WARNING("Extension {}: {}", component_value.empty() ? std::string("log") : component_value,
                            message_value);
            }
            else if (severity_value == "error")
            {
                LOG_ERROR("Extension {}: {}", component_value.empty() ? std::string("log") : component_value,
                          message_value);
            }
            else
            {
                LOG_INFO("Extension {}: {}", component_value.empty() ? std::string("log") : component_value,
                         message_value);
            }
        }

        response.type = "logging.event.result";
        response.fields = QCborMap{{QStringLiteral("accepted"), true}};
    }
    else if (request.type == "settings.get")
    {
        const auto key_it = request.fields.find(QStringLiteral("key"));
        if (key_it == request.fields.end() || !key_it->isString())
        {
            response.type = "settings.get.error";
            response.fields =
                QCborMap{{QStringLiteral("error"),
                          an::ui::extensions::protocolErrorToCborMap(an::ui::extensions::ExtensionProtocolError{
                              an::ui::extensions::makeProtocolErrorCode(
                                  an::ui::extensions::ExtensionProtocolErrorDomain::Settings, 1),
                              "Invalid settings.get request", "Missing key field"})}};
        }
        else
        {
            const QString key = key_it->toString();
            const auto& session_settings = fallback_settings_store[request.session_id];
            const auto setting_it = session_settings.find(key.toStdString());
            QCborValue value = request.fields.value(QStringLiteral("default_value"));
            if (value.isUndefined())
            {
                value = QCborValue(nullptr);
            }
            if (setting_it != session_settings.end())
            {
                value = setting_it->second;
            }

            response.type = "settings.get.result";
            response.fields = QCborMap{{QStringLiteral("key"), key}, {QStringLiteral("value"), value}};
        }
    }
    else if (request.type == "settings.set")
    {
        const auto key_it = request.fields.find(QStringLiteral("key"));
        const auto value_it = request.fields.find(QStringLiteral("value"));
        if (key_it == request.fields.end() || !key_it->isString() || value_it == request.fields.end())
        {
            response.type = "settings.set.error";
            response.fields =
                QCborMap{{QStringLiteral("error"),
                          an::ui::extensions::protocolErrorToCborMap(an::ui::extensions::ExtensionProtocolError{
                              an::ui::extensions::makeProtocolErrorCode(
                                  an::ui::extensions::ExtensionProtocolErrorDomain::Settings, 3),
                              "Invalid settings.set request", "Missing key or value field"})}};
        }
        else
        {
            const QString key = key_it->toString();
            fallback_settings_store[request.session_id][key.toStdString()] = value_it.value();
            response.type = "settings.set.result";
            response.fields = QCborMap{{QStringLiteral("key"), key}};
        }
    }
    else if (request.type == "settings.remove")
    {
        const auto key_it = request.fields.find(QStringLiteral("key"));
        if (key_it == request.fields.end() || !key_it->isString())
        {
            response.type = "settings.remove.error";
            response.fields =
                QCborMap{{QStringLiteral("error"),
                          an::ui::extensions::protocolErrorToCborMap(an::ui::extensions::ExtensionProtocolError{
                              an::ui::extensions::makeProtocolErrorCode(
                                  an::ui::extensions::ExtensionProtocolErrorDomain::Settings, 5),
                              "Invalid settings.remove request", "Missing key field"})}};
        }
        else
        {
            const QString key = key_it->toString();
            bool removed = false;
            if (auto session_it = fallback_settings_store.find(request.session_id);
                session_it != fallback_settings_store.end())
            {
                removed = session_it->second.erase(key.toStdString()) > 0;
            }

            response.type = "settings.remove.result";
            response.fields = QCborMap{{QStringLiteral("key"), key}, {QStringLiteral("removed"), removed}};
        }
    }
    else if (request.type == "settings.keys")
    {
        QCborArray keys;
        if (auto session_it = fallback_settings_store.find(request.session_id);
            session_it != fallback_settings_store.end())
        {
            std::vector<QString> ordered_keys;
            ordered_keys.reserve(session_it->second.size());
            for (const auto& [key, _] : session_it->second)
            {
                ordered_keys.push_back(QString::fromStdString(key));
            }
            std::sort(ordered_keys.begin(), ordered_keys.end());
            for (const QString& key : ordered_keys)
            {
                keys.push_back(key);
            }
        }

        response.type = "settings.keys.result";
        response.fields = QCborMap{{QStringLiteral("keys"), keys}};
    }
    else
    {
        response.type = request.type + ".error";
        response.fields = QCborMap{
            {QStringLiteral("error"),
             an::ui::extensions::protocolErrorToCborMap(an::ui::extensions::ExtensionProtocolError{
                 an::ui::extensions::makeProtocolErrorCode(an::ui::extensions::ExtensionProtocolErrorDomain::Host, 71),
                 "Extension SDK service is unavailable for this session", request.type})}};
    }

    std::string local_error;
    if (!writeProtocolMessage(running_session->socket.get(), response,
                              running_session->prepared_session.handshake_timeout_ms, &local_error))
    {
        if (error)
        {
            *error = local_error;
        }
        return false;
    }

    running_session->serviced_sdk_message_count += 1;
    return true;
}

bool flushDeferredSdkServiceResponses(an::ui::extensions::ExtensionSdkService* sdk_service,
                                      an::ui::extensions::ExtensionHostRunningSession* running_session,
                                      std::string* error)
{
    if (!sdk_service || !running_session)
    {
        return true;
    }

    std::string local_error;
    auto responses = sdk_service->takeDeferredProtocolResponses(running_session->prepared_session.session_id);
    for (const auto& response : responses)
    {
        if (!writeProtocolMessage(running_session->socket.get(), response,
                                  running_session->prepared_session.handshake_timeout_ms, &local_error))
        {
            if (error)
            {
                *error = local_error;
            }
            return false;
        }

        running_session->serviced_sdk_message_count += 1;
    }

    return true;
}

bool ensureInitialSdkSessionMessagesSent(an::ui::extensions::ExtensionSdkService* sdk_service,
                                         an::ui::extensions::ExtensionHostRunningSession* running_session,
                                         std::string* error)
{
    if (!sdk_service || !running_session)
    {
        return true;
    }

    std::string local_error;
    if (!running_session->initial_theme_state_sent)
    {
        an::ui::extensions::ThemeDescribeMessage theme_message;
        bool has_theme_message = false;
        if (!sdk_service->buildInitialThemeDescribeMessage(running_session->prepared_session.session_id, &theme_message,
                                                           &has_theme_message, &local_error))
        {
            if (error)
            {
                *error = local_error;
            }
            return false;
        }

        if (has_theme_message)
        {
            if (!writeProtocolMessage(running_session->socket.get(),
                                      an::ui::extensions::toProtocolMessage(theme_message),
                                      running_session->prepared_session.handshake_timeout_ms, &local_error))
            {
                if (error)
                {
                    *error = local_error;
                }
                return false;
            }
            running_session->initial_theme_state_sent = true;
        }
    }

    if (!running_session->initial_view_state_sent)
    {
        an::ui::extensions::ViewDescribeMessage view_message;
        bool has_view_message = false;
        if (!sdk_service->buildInitialViewDescribeMessage(running_session->prepared_session.session_id,
                                                          running_session->initial_view_name, &view_message,
                                                          &has_view_message, &local_error))
        {
            if (error)
            {
                *error = local_error;
            }
            return false;
        }

        if (has_view_message)
        {
            if (!writeProtocolMessage(running_session->socket.get(),
                                      an::ui::extensions::toProtocolMessage(view_message),
                                      running_session->prepared_session.handshake_timeout_ms, &local_error))
            {
                if (error)
                {
                    *error = local_error;
                }
                return false;
            }
            running_session->initial_view_state_sent = true;
        }
    }

    return true;
}

bool sendRuntimeNotificationMessage(an::ui::extensions::ExtensionSdkService* sdk_service,
                                    an::ui::extensions::ExtensionHostRunningSession* running_session,
                                    an::ui::extensions::ExtensionProtocolMessage message, std::string* error)
{
    if (!running_session)
    {
        if (error)
        {
            *error = "Extension host running session is null";
        }
        return false;
    }
    if (running_session->state != an::ui::extensions::ExtensionHostSessionState::Active)
    {
        if (error)
        {
            *error = "Extension host session is not active";
        }
        return false;
    }
    if (!running_session->runtime_ready)
    {
        if (error)
        {
            *error = "Extension host runtime is still initializing";
        }
        return false;
    }
    if (!ensureInitialSdkSessionMessagesSent(sdk_service, running_session, error))
    {
        return false;
    }

    if (message.session_id.empty())
    {
        message.session_id = running_session->prepared_session.session_id;
    }
    else if (message.session_id != running_session->prepared_session.session_id)
    {
        if (error)
        {
            *error = "Runtime notification session id does not match the active extension host session";
        }
        return false;
    }
    if (message.request_id.empty())
    {
        message.request_id = an::ui::extensions::generateOpaqueToken();
    }

    if (!writeProtocolMessage(running_session->socket.get(), message,
                              running_session->prepared_session.handshake_timeout_ms, error))
    {
        return false;
    }

    running_session->serviced_sdk_message_count += 1;
    return true;
}

void markSessionTerminal(an::ui::extensions::ExtensionHostRunningSession* running_session,
                         const an::ui::extensions::ExtensionHostSessionState state, std::string message)
{
    if (!running_session)
    {
        return;
    }

    running_session->state = state;
    running_session->last_error = std::move(message);
}

[[nodiscard]] bool sessionCanReceiveRuntimeNotifications(const an::ui::extensions::ExtensionSessionController* session)
{
    if (!session || !session->isActive())
    {
        return false;
    }

    const auto& running_session = session->running_session;
    return running_session.runtime_ready && running_session.socket &&
           running_session.socket->state() == QLocalSocket::ConnectedState && running_session.process &&
           running_session.process->state() == QProcess::Running;
}

[[nodiscard]] bool isBenignRuntimeNotificationFailure(const an::ui::extensions::ExtensionSessionController* session)
{
    if (!session)
    {
        return false;
    }

    const auto& running_session = session->running_session;
    return running_session.state == an::ui::extensions::ExtensionHostSessionState::Disconnected ||
           running_session.state == an::ui::extensions::ExtensionHostSessionState::Exited || !running_session.socket ||
           running_session.socket->state() == QLocalSocket::UnconnectedState || !running_session.process ||
           running_session.process->state() != QProcess::Running;
}

void markRuntimeNotificationFailure(an::ui::extensions::ExtensionSessionController* session,
                                    const std::string_view notification_type, const std::string& error)
{
    if (!session)
    {
        return;
    }

    if (an::platform::isLoggingInitialized())
    {
        const std::string message = error.empty() ? std::string("unknown error") : error;
        if (isBenignRuntimeNotificationFailure(session))
        {
            LOG_INFO("Extension host session id={} skipped {} notification during shutdown: {}",
                     session->running_session.prepared_session.session_id, notification_type, message);
        }
        else
        {
            LOG_WARNING("Extension host session id={} failed to send {} notification: {}",
                        session->running_session.prepared_session.session_id, notification_type, message);
        }
    }
}

void pruneExecutionGateSessions(an::ui::extensions::ExtensionHostExecutionGateState* gate_state)
{
    if (!gate_state)
    {
        return;
    }

    gate_state->active_execution_sessions.erase(
        std::remove_if(gate_state->active_execution_sessions.begin(), gate_state->active_execution_sessions.end(),
                       [](const an::ui::extensions::ExtensionHostRunningSession* session) {
                           return !session || session->state != an::ui::extensions::ExtensionHostSessionState::Active ||
                                  session->in_flight_request_id.empty();
                       }),
        gate_state->active_execution_sessions.end());

    gate_state->queued_requests.erase(
        std::remove_if(gate_state->queued_requests.begin(), gate_state->queued_requests.end(),
                       [](const an::ui::extensions::ExtensionHostQueuedExecutionRequest& request) {
                           return !request.session ||
                                  request.session->state != an::ui::extensions::ExtensionHostSessionState::Active;
                       }),
        gate_state->queued_requests.end());
}

bool hasHostArgument(const std::vector<std::string>& host_arguments, const std::string_view key)
{
    return std::find(host_arguments.begin(), host_arguments.end(), key) != host_arguments.end();
}

/// What a full host's native window previews by, for a launch whose arguments do not state it already: the rate the
/// Processing page states, as the settings last answered it, and what the extension's manifest says a frame of a drag
/// may run its preview over.
void appendNativePreviewArguments(const an::ui::extensions::ExtensionCapabilities& capabilities,
                                  const std::uint32_t frame_rate, std::vector<std::string>& host_arguments)
{
    if (hasHostArgument(host_arguments, "--preview-frame-rate") || hasHostArgument(host_arguments, "--preview-scaling"))
    {
        return;
    }
    const std::vector<std::string> preview = an::ui::extensions::nativePreviewHostArguments(capabilities, frame_rate);
    host_arguments.insert(host_arguments.end(), preview.begin(), preview.end());
}

struct PreviewSubAreaSchemaConfig
{
    QSize size{800, 600};
    QString label;
};

std::optional<PreviewSubAreaSchemaConfig> parsePreviewSubAreaSchemaConfig(const QJsonArray& schema)
{
    for (const auto& entry_value : schema)
    {
        if (!entry_value.isObject())
        {
            continue;
        }

        const QJsonObject obj = entry_value.toObject();
        const QString type = obj.value(QStringLiteral("type")).toString().trimmed().toLower();
        if (type != QLatin1String("meta") || !obj.value(QStringLiteral("preview_area")).toBool(true) ||
            !obj.value(QStringLiteral("sub_area")).toBool(false))
        {
            continue;
        }

        PreviewSubAreaSchemaConfig config;
        const QJsonArray size = obj.value(QStringLiteral("sub_area_size")).toArray();
        config.size = QSize(size.size() >= 1 ? size[0].toInt(800) : 800, size.size() >= 2 ? size[1].toInt(600) : 600);
        config.label = obj.value(QStringLiteral("sub_area_label")).toString();
        return config;
    }

    return std::nullopt;
}

an::ui::extensions::ExtensionSessionController::PrepareOutcome prepareManagedRequestBuffers(
    const an::ui::extensions::ExtensionSessionController& session, const bool for_preview,
    const an::ui::extensions::ExtensionHostLaunchCoordinator::ManagedAuxiliaryBufferProvider& auxiliary_buffer_provider,
    const an::ui::extensions::ExtensionHostLaunchCoordinator::ManagedPreviewSubAreaProvider& preview_sub_area_provider,
    std::vector<an::ui::extensions::SharedBufferDescriptor>* out_descriptors,
    std::vector<an::ui::extensions::SharedImageBuffer>* out_owned_buffers, std::string* error)
{
    using Outcome = an::ui::extensions::ExtensionSessionController::PrepareOutcome;
    const auto fail = [error](std::string message) {
        if (error)
        {
            *error = std::move(message);
        }
        return Outcome::Failed;
    };

    if (!out_descriptors || !out_owned_buffers)
    {
        return fail("Managed request buffer outputs are null");
    }

    out_descriptors->clear();
    out_owned_buffers->clear();

    if (!session.target_control || !session.target_control->hasImage())
    {
        return fail("The selected image does not contain loaded image data");
    }

    // The source is read under a lease taken without waiting: a document a worker is writing answers Busy, and the
    // caller asks again rather than blocking the GUI thread on the writer.
    const an::ui::ImageDocument& document = session.target_control->document();
    an::Result<an::consumer::AssetReadLease> source = document.tryRead();
    if (!source.hasValue())
    {
        if (source.status().code() == an::ErrorCode::Busy)
        {
            if (error)
            {
                *error = "The selected image is being written; the request waits for it";
            }
            return Outcome::Busy;
        }
        return fail(source.status().message());
    }
    const std::optional<an::ImageDescriptor> described =
        std::visit([](const auto& typed) { return typed.descriptor(); }, source->image());
    if (!described.has_value())
    {
        return fail("The selected image does not contain loaded image data");
    }

    const std::optional<PreviewSubArea> preview_sub_area =
        for_preview ? resolvePreviewSubArea(session, *described, preview_sub_area_provider) : std::nullopt;
    const std::optional<QRect> region =
        preview_sub_area.has_value() ? std::optional<QRect>(preview_sub_area->rect) : std::nullopt;

    an::ui::extensions::SharedBufferManager shared_buffer_manager;
    an::ui::extensions::ImageExportRequest source_request;
    source_request.sessionId = session.running_session.prepared_session.session_id;
    source_request.role = an::ui::extensions::SharedBufferRole::Source;
    source_request.accessMode = an::ui::extensions::SharedBufferAccessMode::ReadWrite;
    source_request.symbolicName = "source";
    source_request.region = region;
    source_request.fileName = document.names().sourcePath.toStdString();
    an::Result<an::ui::extensions::SharedImageBuffer> source_buffer =
        shared_buffer_manager.exportImage(source->image(), source_request);
    source->release();
    if (!source_buffer.hasValue())
    {
        return fail(source_buffer.status().message());
    }

    out_descriptors->push_back(source_buffer->descriptor());
    out_owned_buffers->push_back(std::move(*source_buffer));

    const bool needs_auxiliary_buffers = session.process_capabilities.masks_input ||
                                         session.process_capabilities.weights_input ||
                                         (!for_preview && session.process_capabilities.masks_output);
    if (!needs_auxiliary_buffers)
    {
        return Outcome::Ready;
    }

    if (!auxiliary_buffer_provider)
    {
        return fail("Managed auxiliary buffer provider is not configured");
    }

    std::unordered_set<std::string> input_mask_names;
    std::unordered_set<std::string> weight_names;
    std::unordered_set<std::string> output_mask_names;
    int input_mask_count = 0;
    int weight_count = 0;
    int output_mask_count = 0;

    for (const auto& auxiliary : auxiliary_buffer_provider(session.target_control, for_preview))
    {
        std::string symbolic_name = auxiliary.symbolic_name;
        std::unordered_set<std::string>* seen_names = nullptr;
        bool include_auxiliary = false;

        if (auxiliary.buffer_role == an::ui::extensions::SharedBufferRole::Mask)
        {
            if (auxiliary.access_mode == an::ui::extensions::SharedBufferAccessMode::ReadOnly)
            {
                include_auxiliary = session.process_capabilities.masks_input;
                if (symbolic_name.empty())
                {
                    symbolic_name = input_mask_count == 0
                                        ? std::string("selection")
                                        : std::string("selection_") + std::to_string(input_mask_count + 1);
                }
                ++input_mask_count;
                seen_names = &input_mask_names;
            }
            else if (auxiliary.access_mode == an::ui::extensions::SharedBufferAccessMode::ReadWrite)
            {
                include_auxiliary = !for_preview && session.process_capabilities.masks_output;
                if (symbolic_name.empty())
                {
                    symbolic_name = output_mask_count == 0
                                        ? std::string("output_mask")
                                        : std::string("output_mask_") + std::to_string(output_mask_count + 1);
                }
                ++output_mask_count;
                seen_names = &output_mask_names;
            }
        }
        else if (auxiliary.buffer_role == an::ui::extensions::SharedBufferRole::Weight)
        {
            if (auxiliary.access_mode != an::ui::extensions::SharedBufferAccessMode::ReadOnly)
            {
                return fail("Managed weight buffers must be read-only");
            }

            include_auxiliary = session.process_capabilities.weights_input;
            if (symbolic_name.empty())
            {
                symbolic_name = weight_count == 0 ? std::string("weight_map")
                                                  : std::string("weight_map_") + std::to_string(weight_count + 1);
            }
            ++weight_count;
            seen_names = &weight_names;
        }
        else
        {
            return fail("Managed auxiliary buffer provider only supports mask and weight roles");
        }

        if (!include_auxiliary)
        {
            continue;
        }
        if (!auxiliary.image)
        {
            return fail("Managed auxiliary buffer provider returned a null image");
        }
        if (seen_names && !seen_names->insert(symbolic_name).second)
        {
            return fail("Managed auxiliary buffer provider returned duplicate symbolic names");
        }

        an::Result<an::consumer::AssetReadLease> auxiliary_lease = auxiliary.image->tryRead();
        if (!auxiliary_lease.hasValue())
        {
            if (auxiliary_lease.status().code() == an::ErrorCode::Busy)
            {
                if (error)
                {
                    *error = "An auxiliary image is being written; the request waits for it";
                }
                return Outcome::Busy;
            }
            return fail(auxiliary_lease.status().message());
        }

        an::ui::extensions::ImageExportRequest auxiliary_request;
        auxiliary_request.sessionId = session.running_session.prepared_session.session_id;
        auxiliary_request.role = auxiliary.buffer_role;
        auxiliary_request.accessMode = auxiliary.access_mode;
        auxiliary_request.symbolicName = symbolic_name;
        auxiliary_request.region = region;
        an::Result<an::ui::extensions::SharedImageBuffer> buffer =
            shared_buffer_manager.exportImage(auxiliary_lease->image(), auxiliary_request);
        auxiliary_lease->release();
        if (!buffer.hasValue())
        {
            return fail(buffer.status().message());
        }

        out_descriptors->push_back(buffer->descriptor());
        out_owned_buffers->push_back(std::move(*buffer));
    }

    return Outcome::Ready;
}

an::ui::extensions::ExtensionSessionController*
findSessionController(const std::vector<std::unique_ptr<an::ui::extensions::ExtensionSessionController>>& sessions,
                      const std::string_view descriptor_id, const ImageViewControl* control)
{
    const auto it = std::find_if(
        sessions.begin(), sessions.end(),
        [descriptor_id, control](const std::unique_ptr<an::ui::extensions::ExtensionSessionController>& session) {
            return session && session->matchesDescriptor(descriptor_id, control);
        });
    return it != sessions.end() ? it->get() : nullptr;
}

an::ui::extensions::ExtensionSessionController* findSessionControllerBySessionId(
    const std::vector<std::unique_ptr<an::ui::extensions::ExtensionSessionController>>& sessions,
    const std::string_view session_id)
{
    const auto it =
        std::find_if(sessions.begin(), sessions.end(),
                     [session_id](const std::unique_ptr<an::ui::extensions::ExtensionSessionController>& session) {
                         return session && session->matchesSessionId(session_id);
                     });
    return it != sessions.end() ? it->get() : nullptr;
}

bool sendThemeChangedNotification(an::ui::extensions::ExtensionSdkService* sdk_service,
                                  an::ui::extensions::ExtensionSessionController* session, std::string* error)
{
    if (!sdk_service || !session)
    {
        return true;
    }

    an::ui::extensions::ThemeChangedMessage message;
    bool has_message = false;
    if (!sdk_service->buildThemeChangedMessage(session->running_session.prepared_session.session_id, &message,
                                               &has_message, error))
    {
        return false;
    }
    if (!has_message)
    {
        return true;
    }

    return sendRuntimeNotificationMessage(sdk_service, &session->running_session,
                                          an::ui::extensions::toProtocolMessage(message), error);
}

bool sendViewUpdatedNotification(an::ui::extensions::ExtensionSdkService* sdk_service,
                                 an::ui::extensions::ExtensionSessionController* session, std::string* error)
{
    if (!sdk_service || !session || session->target_view_name.empty())
    {
        return true;
    }

    an::ui::extensions::ViewUpdatedMessage message;
    bool has_message = false;
    if (!sdk_service->buildViewUpdatedMessage(session->running_session.prepared_session.session_id,
                                              session->currentViewId(), session->target_view_name, &message,
                                              &has_message, error))
    {
        return false;
    }
    if (!has_message)
    {
        return true;
    }

    return sendRuntimeNotificationMessage(sdk_service, &session->running_session,
                                          an::ui::extensions::toProtocolMessage(message), error);
}

void removeQueuedRequestsForSession(an::ui::extensions::ExtensionHostExecutionGateState* gate_state,
                                    const an::ui::extensions::ExtensionHostRunningSession* running_session)
{
    if (!gate_state || !running_session)
    {
        return;
    }

    gate_state->queued_requests.erase(
        std::remove_if(
            gate_state->queued_requests.begin(), gate_state->queued_requests.end(),
            [running_session](const an::ui::extensions::ExtensionHostQueuedExecutionRequest& queued_request) {
                return queued_request.session == running_session;
            }),
        gate_state->queued_requests.end());
}

void removeQueuedPreviewRequestsForSession(an::ui::extensions::ExtensionHostExecutionGateState* gate_state,
                                           const an::ui::extensions::ExtensionHostRunningSession* running_session)
{
    if (!gate_state || !running_session)
    {
        return;
    }

    gate_state->queued_requests.erase(
        std::remove_if(
            gate_state->queued_requests.begin(), gate_state->queued_requests.end(),
            [running_session](const an::ui::extensions::ExtensionHostQueuedExecutionRequest& queued_request) {
                return queued_request.session == running_session && queued_request.request_kind == "preview";
            }),
        gate_state->queued_requests.end());
}

// Takes a session out of the execution gate altogether: its queued requests and the slot it holds. The gate keeps the
// session by address, so a session whose controller is about to be destroyed must leave it first, or the next pump
// reads freed memory deciding whether it still counts.
void removeSessionFromExecutionGate(an::ui::extensions::ExtensionHostExecutionGateState* gate_state,
                                    const an::ui::extensions::ExtensionHostRunningSession* running_session)
{
    if (!gate_state || !running_session)
    {
        return;
    }

    removeQueuedRequestsForSession(gate_state, running_session);
    gate_state->active_execution_sessions.erase(std::remove(gate_state->active_execution_sessions.begin(),
                                                            gate_state->active_execution_sessions.end(),
                                                            running_session),
                                                gate_state->active_execution_sessions.end());
}

an::ui::extensions::ExtensionRuntimeTerminationKind
runtimeTerminationKindForState(const an::ui::extensions::ExtensionHostSessionState state)
{
    using an::ui::extensions::ExtensionHostSessionState;
    using an::ui::extensions::ExtensionRuntimeTerminationKind;

    switch (state)
    {
    case ExtensionHostSessionState::Crashed:
        return ExtensionRuntimeTerminationKind::Crashed;
    case ExtensionHostSessionState::Disconnected:
        return ExtensionRuntimeTerminationKind::Disconnected;
    case ExtensionHostSessionState::Unresponsive:
        return ExtensionRuntimeTerminationKind::Unresponsive;
    case ExtensionHostSessionState::Exited:
        return ExtensionRuntimeTerminationKind::Exited;
    case ExtensionHostSessionState::Failed:
        return ExtensionRuntimeTerminationKind::Failed;
    case ExtensionHostSessionState::Inactive:
    case ExtensionHostSessionState::Active:
    case ExtensionHostSessionState::Stopped:
        return ExtensionRuntimeTerminationKind::ForcedStop;
    }

    return ExtensionRuntimeTerminationKind::Failed;
}

an::ui::extensions::ExtensionRuntimeTerminationDiagnostics
buildTerminationDiagnostics(const an::ui::extensions::ExtensionHostRunningSession& running_session)
{
    an::ui::extensions::ExtensionRuntimeTerminationDiagnostics diagnostics;
    diagnostics.session_id = running_session.prepared_session.session_id;
    diagnostics.process_exit_code = running_session.handshake.process_exit_code;
    diagnostics.last_heartbeat_elapsed_ms = running_session.last_heartbeat_elapsed_ms;
    diagnostics.in_flight_request_id = running_session.in_flight_request_id;
    diagnostics.outstanding_buffer_ids.reserve(running_session.in_flight_request_buffers.size());
    for (const auto& descriptor : running_session.in_flight_request_buffers)
    {
        diagnostics.outstanding_buffer_ids.push_back(descriptor.buffer_id);
    }
    return diagnostics;
}

/// How many times, half a second apart, the frames a stopped host left are removed again while it still holds them.
constexpr int kPreviewFramesRootRemovalAttempts = 20;

/// Removes the directory a stopped session's host lent its frames from. The host is ended without a teardown, so on
/// Windows its mapped files cannot be removed until the process has gone, which may take longer than the stop waits:
/// what is left is tried again from the event loop, half a second apart, a bounded number of times.
void removePreviewFramesRoot(const std::filesystem::path& frames_root, const int attempts_left)
{
    std::error_code frames_error;
    std::filesystem::remove_all(frames_root, frames_error);
    std::error_code exists_error;
    if (!std::filesystem::exists(frames_root, exists_error))
    {
        return;
    }
    if (attempts_left <= 0)
    {
        if (an::platform::isLoggingInitialized())
        {
            LOG_WARNING("Failed to clean the preview frames of a stopped extension session root={} reason={}",
                        frames_root.generic_string(), frames_error.message());
        }
        return;
    }
    QTimer::singleShot(500,
                       [frames_root, attempts_left]() { removePreviewFramesRoot(frames_root, attempts_left - 1); });
}

void clearSessionControllerBuffers(an::ui::extensions::ExtensionSessionController* session) noexcept
{
    if (!session)
    {
        return;
    }

    // A session's buffers live under whichever root the host picked for it, which is not
    // necessarily the default local-data root -- the extension host allocates under the system
    // temp directory. Collect the roots this session actually used before the handles are
    // dropped; cleaning only the default root leaves full-resolution mapped files stranded.
    std::set<std::filesystem::path> buffer_roots;
    for (const auto& buffer : session->owned_buffers)
    {
        std::filesystem::path buffer_root = sharedBufferRootPathForDescriptor(buffer.descriptor());
        if (!buffer_root.empty())
        {
            buffer_roots.insert(std::move(buffer_root));
        }
    }

    session->owned_buffers.clear();
    const std::string& session_id = session->running_session.prepared_session.session_id;
    if (session_id.empty())
    {
        return;
    }

    buffer_roots.insert(an::ui::extensions::SharedBufferManager::defaultSharedBufferRootPath());

    for (const auto& buffer_root : buffer_roots)
    {
        const an::ui::extensions::SharedBufferManager shared_buffer_manager(buffer_root);
        const an::Status removed = shared_buffer_manager.removeSessionBuffers(session_id);
        if (removed.failed() && an::platform::isLoggingInitialized())
        {
            LOG_WARNING("Failed to clean shared buffers for extension session id={} root={} reason={}", session_id,
                        buffer_root.generic_string(), removed.message());
        }
    }

    // A native window's frames reuse buffers of the host's own from one frame to the next, and a host this session
    // stopped ended with them mapped: what they leave goes once the host has.
    removePreviewFramesRoot(an::ui::extensions::extensionHostPreviewFramesRootPath(session_id),
                            kPreviewFramesRootRemovalAttempts);
}

} // anonymous namespace

namespace an::ui::extensions
{

ExtensionSessionController::~ExtensionSessionController()
{
    cancelMaterialization();
}

// A schema read is a host of its own that imports the extension, so it takes as long as the import, up to the bound a
// session's runtime start has. Nothing waits for it: its process reports its end through the event loop to the
// receiver, which a coordinator going away takes with it, and the launch that asked for it goes on from there.
struct ExtensionHostLaunchCoordinator::SchemaReads
{
    struct Read
    {
        std::uint64_t id = 0u;
        ExtensionHostLaunchBinding binding;
        ImageViewControl* control = nullptr;
        ExtensionHostLaunchOptions options;
        bool workflow = false;
        // The settings the schema is read under, which a launch after it compares with its own.
        std::string settings_fingerprint;
        // The bound the read was begun under, and whether it was stopped there.
        int bound_ms = kDefaultParameterSchemaReadBoundMs;
        bool timed_out = false;
        std::unique_ptr<QProcess> process;
    };

    struct Kept
    {
        std::string read_key;
        std::string settings_fingerprint;
        AutoUiSchemaInspection inspection;
    };

    // The schema kept for the descriptor, while the binding reads it with the same host and arguments and the
    // extension's settings are those it was read under.
    [[nodiscard]] bool findKept(const ExtensionHostLaunchBinding& binding, ExtensionSdkService& sdk_service,
                                AutoUiSchemaInspection* out_inspection) const
    {
        const auto found = kept.find(binding.descriptor_id);
        if (found == kept.end() || found->second.read_key != autoUiSchemaReadKey(binding) ||
            found->second.settings_fingerprint != extensionSettingsFingerprint(sdk_service, binding.extension_id))
        {
            return false;
        }
        *out_inspection = found->second.inspection;
        applyPersistedExtensionSettingDefaultsToAutoSchema(&out_inspection->schema, binding.extension_id, sdk_service);
        return true;
    }

    // Stops a read that has not landed; its report, if one is on its way, finds no read and does nothing.
    static void end(Read& read)
    {
        if (!read.process)
        {
            return;
        }
        read.process->disconnect();
        if (read.process->state() != QProcess::NotRunning)
        {
            read.process->kill();
            read.process->waitForFinished(100);
        }
    }

    QObject receiver;
    std::uint64_t next_id = 1u;
    std::vector<std::unique_ptr<Read>> running;
    // By descriptor; binding a descriptor again lets its schema go, since its extension may have changed in place.
    std::unordered_map<std::string, Kept> kept;
};

struct ExtensionHostLaunchCoordinator::PreviewFrameRateRead
{
    std::atomic<std::uint32_t> rate{an::ui::readPreviewFrameRate({})};
    /// The requests for a read: one made while a read is on its way is read once more before the worker ends, so a
    /// Settings apply during a read is not lost, however the requests and the worker's end interleave.
    an::ui::ReadRequests requests;
};

ExtensionHostLaunchCoordinator::ExtensionHostLaunchCoordinator()
    : preview_frame_rate_(std::make_shared<PreviewFrameRateRead>()), schema_reads_(std::make_unique<SchemaReads>())
{
}

ExtensionHostLaunchCoordinator::~ExtensionHostLaunchCoordinator()
{
    stopAllSessions();
}

void ExtensionSessionController::detachUiHandles()
{
    clearManagedPreviewSubArea();
    preview_request_pending = false;
    execute_request_pending = false;
    last_preview_dispatch_result.reset();
    last_execute_dispatch_result.reset();
    current_process_message.reset();
    execute_enabled = false;
    forward_close_request = {};
    forward_execute_request = {};
    forward_preview_request = {};

    if (preview_control)
    {
        QPointer<ImageViewControl> preview_control_ref = preview_control;
        preview_control = nullptr;
        if (preview_control_ref)
        {
            preview_control_ref->deepClose();
        }
    }
}

void ExtensionSessionController::configureManagedPreviewSubArea()
{
    clearManagedPreviewSubArea();

    if (!managed_preview_sub_area_enabled || !target_control)
    {
        return;
    }

    auto* const view_widget = target_control->getViewWidget();
    auto* const scroll_area = target_control->getScrollArea();
    const std::optional<an::ImageDescriptor> image = target_control->imageDescriptor();
    if (!view_widget || !scroll_area || !image.has_value())
    {
        return;
    }

    QRect selection_area = view_widget->getCurrentSelectionArea();
    if (!view_widget->isSelectionAreaEnabled() || selection_area.isEmpty())
    {
        const QRect image_area(0, 0, static_cast<int>(image->extent().width()),
                               static_cast<int>(image->extent().height()));
        const QPoint center = image_area.center();
        selection_area.setTopLeft(QPoint((std::max)(0, center.x() - managed_preview_sub_area_size.width() / 2),
                                         (std::max)(0, center.y() - managed_preview_sub_area_size.height() / 2)));
        selection_area.setSize(managed_preview_sub_area_size);
        selection_area = selection_area.normalized().intersected(image_area);
    }

    if (!managed_preview_sub_area_label.isEmpty())
    {
        view_widget->setSelectionAreaText(managed_preview_sub_area_label);
    }

    managed_preview_crop_controller = std::make_unique<ImageCropController>(scroll_area, this);
    managed_preview_crop_controller->setListenerEnabled(true);
    view_widget->setImageViewSelectionStyle(ImageViewSelectionStyle::PREVIEW_STYLE);
    managed_preview_crop_controller->updateSelectionArea(selection_area);
    managed_preview_crop_controller->enableOneClickMove(true);
}

void ExtensionSessionController::clearManagedPreviewSubArea()
{
    if (!managed_preview_crop_controller && !managed_preview_sub_area_enabled)
    {
        return;
    }

    if (managed_preview_crop_controller)
    {
        managed_preview_crop_controller->destroy();
        managed_preview_crop_controller.reset();
    }

    if (target_control && target_control->getViewWidget())
    {
        auto* const view_widget = target_control->getViewWidget();
        view_widget->clearSelectionAreaText();
        view_widget->enableSelectionArea(false);
        view_widget->update();
    }
}

void ExtensionSessionController::discardMaterializedBuffers() noexcept
{
    owned_buffers.clear();
}

void ExtensionSessionController::markPreviewRefreshPending()
{
    preview_request_pending = true;
    current_process_message =
        ProcessMessageState{QStringLiteral("Extension Preview"), QStringLiteral("Preview refresh is pending."),
                            ProcessMessageState::Tone::Info, ProcessMessageState::Source::Lifecycle};
}

void ExtensionSessionController::markExecuteWaitingForImage()
{
    execute_request_pending = true;
    current_process_message = ProcessMessageState{
        QStringLiteral("Extension Waiting"), QStringLiteral("Execution waits for the image being written."),
        ProcessMessageState::Tone::Info, ProcessMessageState::Source::Lifecycle};
}

void ExtensionSessionController::reportPendingExecuteFailure(const std::string& error)
{
    execute_request_pending = false;
    current_process_message =
        ProcessMessageState{QStringLiteral("Extension Execute"), QString::fromStdString(error),
                            ProcessMessageState::Tone::Error, ProcessMessageState::Source::Lifecycle};
    if (an::platform::isLoggingInitialized())
    {
        LOG_WARNING("An extension execute that waited for its image could not be sent for descriptor id={} reason={}",
                    descriptor_id, error);
    }
    if (auto* dialog_service = currentDialogService())
    {
        dialog_service->notifyErrorMessage(nullptr, QString::fromStdString(display_name),
                                           QString::fromStdString(error));
    }
}

void ExtensionSessionController::selectionAreaUpdated() {}

void ExtensionSessionController::selectionAreaChanged()
{
    if (managed_preview_sub_area_changed)
    {
        managed_preview_sub_area_changed();
    }
}

void ExtensionSessionController::newSelectionFinished() {}

void ExtensionSessionController::markPreviewResultConsumed()
{
    if (running_session.last_preview_result.has_value())
    {
        last_consumed_preview_result_request_id = running_session.last_preview_result->target_request_id;
    }
    last_preview_dispatch_result.reset();
    if (!preview_request_pending)
    {
        clearProcessMessageForSource(ProcessMessageState::Source::Lifecycle);
    }
}

void ExtensionSessionController::markExecuteCommitConsumed()
{
    if (running_session.last_view_commit.has_value())
    {
        last_consumed_execute_commit_request_id = running_session.last_view_commit->request_id;
    }
}

void ExtensionSessionController::clearExecuteCommit() noexcept
{
    running_session.last_view_commit.reset();
    last_execute_dispatch_result.reset();
    clearProcessMessageForSource(ProcessMessageState::Source::Lifecycle);
}

void ExtensionSessionController::stopRunningSession(
    const std::function<void(ExtensionHostRunningSession*)>& stop_session)
{
    if (stop_session)
    {
        stop_session(&running_session);
    }
}

bool ExtensionSessionController::initializeSessionParams(const QJsonArray& auto_param_schema,
                                                         const std::function<bool(std::string*)>& execute_request,
                                                         const std::function<bool(std::string*)>& preview_request,
                                                         const std::function<void()>& close_session, std::string* error)
{
    seeded_execution_params = QCborMap{};
    execute_enabled = static_cast<bool>(execute_request);
    if (!auto_param_schema.isEmpty() &&
        !buildSeededExecutionParamsFromAutoSchema(auto_param_schema, &seeded_execution_params, error))
    {
        return false;
    }

    forward_execute_request = execute_request;
    forward_preview_request = preview_request;
    forward_close_request = close_session;
    return true;
}

bool ExtensionSessionController::preparePreviewSubmission(
    const std::function<void(ExtensionHostRunningSession*)>& remove_queued_preview_requests,
    const std::function<bool(ExtensionHostRunningSession*, std::string*)>& send_cancel_request,
    PreviewSubmissionAction* action, std::string* error)
{
    if (!action)
    {
        if (error)
        {
            *error = "Extension preview submission action output is null";
        }
        return false;
    }

    *action = PreviewSubmissionAction::StartPreview;
    if (remove_queued_preview_requests)
    {
        remove_queued_preview_requests(&running_session);
    }
    if (running_session.in_flight_request_id.empty())
    {
        return true;
    }

    markPreviewRefreshPending();
    *action = PreviewSubmissionAction::AwaitInFlightRequest;
    if (running_session.in_flight_request_kind != "preview" || running_session.cancel_requested)
    {
        return true;
    }
    if (!send_cancel_request)
    {
        if (error)
        {
            *error = "Managed preview refresh requires a configured cancel dispatcher";
        }
        return false;
    }

    return send_cancel_request(&running_session, error);
}

ExtensionSessionController::PrepareOutcome ExtensionSessionController::preparePreviewStart(
    const std::function<void(ExtensionHostRunningSession*)>& remove_queued_preview_requests,
    const std::function<std::vector<ManagedAuxiliaryImage>(ImageViewControl*, bool)>& auxiliary_buffer_provider,
    const std::function<std::optional<QRect>(ImageViewControl*)>& preview_sub_area_provider, QCborMap* execution_params,
    std::vector<SharedBufferDescriptor>* request_buffers, std::vector<SharedImageBuffer>* prepared_owned_buffers,
    std::string* error)
{
    if (remove_queued_preview_requests)
    {
        remove_queued_preview_requests(&running_session);
    }

    return preparePreviewRequest(auxiliary_buffer_provider, preview_sub_area_provider, execution_params,
                                 request_buffers, prepared_owned_buffers, error);
}

ExtensionSessionController::PrepareOutcome ExtensionSessionController::preparePreviewRequest(
    const std::function<std::vector<ManagedAuxiliaryImage>(ImageViewControl*, bool)>& auxiliary_buffer_provider,
    const std::function<std::optional<QRect>(ImageViewControl*)>& preview_sub_area_provider, QCborMap* execution_params,
    std::vector<SharedBufferDescriptor>* request_buffers, std::vector<SharedImageBuffer>* prepared_owned_buffers,
    std::string* error) const
{
    if (!target_control)
    {
        if (error)
        {
            *error = "Extension session controller has no target control";
        }
        return PrepareOutcome::Failed;
    }

    if (execution_params)
    {
        *execution_params = seeded_execution_params;
    }

    return prepareManagedRequestBuffers(*this, true, auxiliary_buffer_provider, preview_sub_area_provider,
                                        request_buffers, prepared_owned_buffers, error);
}

void ExtensionSessionController::applyPreviewDispatchResult(const ExtensionHostExecutionDispatchResult& dispatch_result,
                                                            std::vector<SharedImageBuffer> owned_buffers)
{
    preview_request_pending = false;
    this->owned_buffers = std::move(owned_buffers);
    last_preview_dispatch_result = dispatch_result;
    if (dispatch_result.state == ExtensionHostExecutionDispatchState::Queued)
    {
        current_process_message =
            ProcessMessageState{QStringLiteral("Extension Preview Queued"),
                                QStringLiteral("Preview is queued at position %1.").arg(dispatch_result.queue_position),
                                ProcessMessageState::Tone::Info, ProcessMessageState::Source::Lifecycle};
    }
    else if (dispatch_result.state == ExtensionHostExecutionDispatchState::Started)
    {
        current_process_message =
            ProcessMessageState{QStringLiteral("Extension Preview"), QStringLiteral("Preview is updating."),
                                ProcessMessageState::Tone::Info, ProcessMessageState::Source::Lifecycle};
    }
    else
    {
        current_process_message.reset();
    }
}

ExtensionSessionController::PrepareOutcome ExtensionSessionController::prepareExecuteRequest(
    const std::function<std::vector<ManagedAuxiliaryImage>(ImageViewControl*, bool)>& auxiliary_buffer_provider,
    const std::function<std::optional<QRect>(ImageViewControl*)>& preview_sub_area_provider, QCborMap* execution_params,
    std::vector<SharedBufferDescriptor>* request_buffers, std::vector<SharedImageBuffer>* prepared_owned_buffers,
    std::string* error) const
{
    if (!target_control)
    {
        if (error)
        {
            *error = "Extension session controller has no target control";
        }
        return PrepareOutcome::Failed;
    }

    if (execution_params)
    {
        *execution_params = seeded_execution_params;
    }

    return prepareManagedRequestBuffers(*this, false, auxiliary_buffer_provider, preview_sub_area_provider,
                                        request_buffers, prepared_owned_buffers, error);
}

void ExtensionSessionController::applyExecuteDispatchResult(const ExtensionHostExecutionDispatchResult& dispatch_result,
                                                            std::vector<SharedImageBuffer> owned_buffers)
{
    execute_request_pending = false;
    this->owned_buffers = std::move(owned_buffers);
    last_execute_dispatch_result = dispatch_result;
    execute_enabled = false;
    if (dispatch_result.state == ExtensionHostExecutionDispatchState::Queued)
    {
        current_process_message = ProcessMessageState{
            QStringLiteral("Extension Queued"),
            QStringLiteral("Execution is queued at position %1.").arg(dispatch_result.queue_position),
            ProcessMessageState::Tone::Info, ProcessMessageState::Source::Lifecycle};
    }
    else if (dispatch_result.state == ExtensionHostExecutionDispatchState::Started)
    {
        current_process_message = ProcessMessageState{
            QStringLiteral("Extension Running"), QStringLiteral("Extension execution is in progress."),
            ProcessMessageState::Tone::Info, ProcessMessageState::Source::Lifecycle};
    }
    else
    {
        current_process_message.reset();
    }
}

void ExtensionSessionController::notifyExecuteStarted() const
{
    if (target_control)
    {
        if (auto* event_bus = currentAppEventBus())
        {
            event_bus->notifyProcessExecuted(display_name, target_control);
        }
    }
}

void ExtensionSessionController::captureInitialViewStateIfAvailable(
    const std::function<bool(ExtensionViewState*, bool*, std::string*)>& describe_active_view_state)
{
    if (!describe_active_view_state)
    {
        return;
    }

    ExtensionViewState initial_view_state;
    bool has_initial_view_state = false;
    std::string initial_view_error;
    if (!describe_active_view_state(&initial_view_state, &has_initial_view_state, &initial_view_error))
    {
        if (an::platform::isLoggingInitialized())
        {
            LOG_WARNING("Failed to capture initial managed-session view identity for descriptor id={}: {}",
                        descriptor_id, initial_view_error);
        }
        return;
    }

    if (has_initial_view_state)
    {
        captureInitialViewState(initial_view_state);
    }
}

struct ExtensionSessionController::Materialization
{
    std::mutex mutex;
    // Moves at every start and every cancel, so a worker's landing is kept only for the import it answers.
    std::uint64_t generation = 0u;
    bool in_flight = false;
    std::optional<LandedMaterialization> landed;
};

namespace
{

// Imports a preview buffer and, for a sub-area, composites it into the target's image as it was when the import
// began, through the process framework's composite, under a preview execution admitted on this worker.
an::Result<an::consumer::ImageAsset>
materializePreviewOnWorker(const an::consumer::EngineSession& session, const std::filesystem::path& root,
                           const SharedBufferDescriptor& descriptor,
                           const std::shared_ptr<const an::consumer::ImageAsset>& base)
{
    const SharedBufferManager manager(root);
    an::Result<ImportedImage> imported = manager.importImage(session, descriptor);
    if (!imported.hasValue())
    {
        return imported.status();
    }
    if (!base)
    {
        return std::move(imported->asset);
    }

    // The view's image is leased before the compositing asks to be admitted: an edit of the view holds its writer while
    // it waits to be admitted.
    an::Result<an::consumer::AssetReadLease> base_lease = base->read();
    if (!base_lease.hasValue())
    {
        return base_lease.status();
    }
    an::Result<an::consumer::OperationScope> scope =
        session.admit(an::consumer::OperationRequest{.purpose = an::consumer::OperationPurpose::Preview});
    if (!scope.hasValue())
    {
        return scope.status();
    }
    an::Result<an::consumer::AssetReadLease> patch_lease = imported->asset.read();
    if (!patch_lease.hasValue())
    {
        return patch_lease.status();
    }
    const auto describe = [](const an::consumer::AnyImage& image) {
        return std::visit([](const auto& typed) { return typed.descriptor(); }, image);
    };
    const std::optional<an::ImageDescriptor> base_descriptor = describe(base_lease->image());
    const std::optional<an::ImageDescriptor> patch_descriptor = describe(patch_lease->image());
    if (!base_descriptor.has_value() || !patch_descriptor.has_value())
    {
        return an::Status::failure(an::ErrorCode::NotInitialized, "Preview compositing found an image with no samples");
    }
    if (static_cast<int>(base_descriptor->extent().width()) != *descriptor.source_width ||
        static_cast<int>(base_descriptor->extent().height()) != *descriptor.source_height)
    {
        return an::Status::failure(an::ErrorCode::IncompatibleArguments,
                                   "Preview compositing base image dimensions do not match the declared source bounds");
    }
    const std::optional<an::ImageDescriptor> composite_descriptor =
        an::ui::processCompositeBaseDescriptor(*patch_descriptor, *base_descriptor, scope->engine().limits());
    if (!composite_descriptor.has_value())
    {
        return an::Status::failure(an::ErrorCode::IncompatibleArguments,
                                   "Preview compositing cannot describe the patch at the image's size");
    }
    const QPoint origin(*descriptor.offset_x, *descriptor.offset_y);
    if (*composite_descriptor == *base_descriptor)
    {
        return an::ui::composeProcessRegion(session, scope->engine(), scope->execution(), base_lease->image(),
                                            patch_lease->image(), patch_lease->validity(), origin);
    }
    an::Result<an::consumer::ImageAsset> composite_base =
        an::ui::buildProcessCompositeBase(session, scope->engine(), scope->execution(), base_lease->image(),
                                          base_lease->validity(), *composite_descriptor);
    if (!composite_base.hasValue())
    {
        return composite_base.status();
    }
    an::Result<an::consumer::AssetReadLease> composite_lease = composite_base->read();
    if (!composite_lease.hasValue())
    {
        return composite_lease.status();
    }
    return an::ui::composeProcessRegion(session, scope->engine(), scope->execution(), composite_lease->image(),
                                        patch_lease->image(), patch_lease->validity(), origin);
}

// Imports an execute's destination buffer and each of its masks as an asset of its own.
ExtensionSessionController::LandedMaterialization
materializeExecuteOnWorker(const an::consumer::EngineSession& session, const std::filesystem::path& root,
                           const SharedBufferDescriptor& destination, const std::vector<SharedBufferDescriptor>& masks)
{
    const SharedBufferManager manager(root);
    an::Result<ImportedImage> imported = manager.importImage(session, destination);
    if (!imported.hasValue())
    {
        return {ExtensionSessionController::MaterializationKind::Execute, imported.status(), {}};
    }
    std::vector<ManagedNamedImageResult> output_masks;
    for (const SharedBufferDescriptor& mask : masks)
    {
        an::Result<ImportedImage> imported_mask = manager.importImage(session, mask);
        if (!imported_mask.hasValue())
        {
            return {ExtensionSessionController::MaterializationKind::Execute, imported_mask.status(), {}};
        }
        output_masks.push_back({mask.symbolic_name, std::move(imported_mask->asset)});
    }
    return {ExtensionSessionController::MaterializationKind::Execute, std::move(imported->asset),
            std::move(output_masks)};
}

// Marks an import as started under a new generation, so a cancel or a later start drops its landing.
std::uint64_t startMaterialization(std::shared_ptr<ExtensionSessionController::Materialization>& state)
{
    if (!state)
    {
        state = std::make_shared<ExtensionSessionController::Materialization>();
    }
    const std::lock_guard lock(state->mutex);
    state->generation += 1u;
    state->in_flight = true;
    state->landed.reset();
    return state->generation;
}

// Lands a refusal decided before any worker ran.
void landRefusal(std::shared_ptr<ExtensionSessionController::Materialization>& state,
                 const ExtensionSessionController::MaterializationKind kind, std::string message)
{
    (void)startMaterialization(state);
    const std::lock_guard lock(state->mutex);
    state->in_flight = false;
    state->landed = ExtensionSessionController::LandedMaterialization{
        kind, an::Status::failure(an::ErrorCode::InvalidArgument, std::move(message)), {}};
}

// Hands a worker's result to the GUI thread, where it is kept only while the import it answers is still wanted.
void landMaterialization(const std::shared_ptr<ExtensionSessionController::Materialization>& state,
                         const std::uint64_t generation,
                         const std::shared_ptr<ExtensionSessionController::LandedMaterialization>& result,
                         const std::shared_ptr<std::function<void()>>& wake)
{
    QCoreApplication* const application = QCoreApplication::instance();
    if (application == nullptr)
    {
        return;
    }
    QMetaObject::invokeMethod(
        application,
        [state, generation, result, wake]() {
            {
                const std::lock_guard lock(state->mutex);
                if (state->generation != generation)
                {
                    return;
                }
                state->in_flight = false;
                state->landed = std::move(*result);
            }
            if (wake && *wake)
            {
                (*wake)();
            }
        },
        Qt::QueuedConnection);
}

} // namespace

bool ExtensionSessionController::materializationInFlight() const noexcept
{
    if (!materialization)
    {
        return false;
    }
    const std::lock_guard lock(materialization->mutex);
    return materialization->in_flight;
}

bool ExtensionSessionController::hasLandedMaterialization() const noexcept
{
    if (!materialization)
    {
        return false;
    }
    const std::lock_guard lock(materialization->mutex);
    return materialization->landed.has_value();
}

std::optional<ExtensionSessionController::LandedMaterialization> ExtensionSessionController::takeLandedMaterialization()
{
    if (!materialization)
    {
        return std::nullopt;
    }
    const std::lock_guard lock(materialization->mutex);
    std::optional<LandedMaterialization> landed = std::move(materialization->landed);
    materialization->landed.reset();
    return landed;
}

void ExtensionSessionController::cancelMaterialization() noexcept
{
    if (!materialization)
    {
        return;
    }
    const std::lock_guard lock(materialization->mutex);
    materialization->generation += 1u;
    materialization->in_flight = false;
    materialization->landed.reset();
}

void ExtensionSessionController::beginPreviewMaterialization(const an::consumer::EngineSession& session,
                                                             std::function<void()> wake)
{
    if (!running_session.last_preview_result.has_value())
    {
        landRefusal(materialization, MaterializationKind::Preview,
                    "Extension preview materialization requires an execution.result payload");
        return;
    }

    const ExecutionResultMessage& preview_result = *running_session.last_preview_result;
    const SharedBufferDescriptor* const output_descriptor =
        findBufferDescriptorByRole(preview_result.output_buffers, SharedBufferRole::Preview);
    if (!output_descriptor)
    {
        landRefusal(materialization, MaterializationKind::Preview, "Extension result did not include a preview buffer");
        return;
    }
    std::string error;
    if (!validateManagedExecutionOutputDescriptor(*this, preview_result, *output_descriptor, &error))
    {
        landRefusal(materialization, MaterializationKind::Preview, std::move(error));
        return;
    }

    const bool sub_area = output_descriptor->offset_x.has_value() && output_descriptor->offset_y.has_value() &&
                          output_descriptor->source_width.has_value() && output_descriptor->source_height.has_value();
    std::shared_ptr<const an::consumer::ImageAsset> base;
    if (sub_area)
    {
        if (!target_control || !target_control->hasImage())
        {
            landRefusal(materialization, MaterializationKind::Preview,
                        "Managed preview compositing requires the target view's image");
            return;
        }
        base = target_control->document().asset();
    }

    std::filesystem::path root = sharedBufferRootPathForDescriptor(*output_descriptor);
    if (root.empty())
    {
        root = SharedBufferManager::defaultSharedBufferRootPath();
    }

    const std::uint64_t generation = startMaterialization(materialization);
    const std::shared_ptr<Materialization> state = materialization;
    const auto wake_shared = std::make_shared<std::function<void()>>(std::move(wake));
    const SharedBufferDescriptor descriptor = *output_descriptor;
    const an::consumer::EngineSession* const session_pointer = &session;
    QThreadPool::globalInstance()->start([state, generation, wake_shared, session_pointer, root, descriptor, base]() {
        auto landed = std::make_shared<LandedMaterialization>(LandedMaterialization{
            MaterializationKind::Preview, materializePreviewOnWorker(*session_pointer, root, descriptor, base), {}});
        landMaterialization(state, generation, landed, wake_shared);
    });
}

void ExtensionSessionController::beginExecuteMaterialization(const an::consumer::EngineSession& session,
                                                             std::function<void()> wake)
{
    if (!running_session.last_view_commit.has_value())
    {
        landRefusal(materialization, MaterializationKind::Execute,
                    "Extension execute materialization requires a view.commit payload");
        return;
    }

    // Consumed as it starts, so the pump starts no second import of the same commit.
    markExecuteCommitConsumed();

    const ViewCommitMessage& committed_view = *running_session.last_view_commit;
    const std::vector<SharedBufferDescriptor>& output_buffers = committed_view.result_buffers;
    const SharedBufferDescriptor* const output_descriptor =
        findBufferDescriptorByRole(output_buffers, SharedBufferRole::Destination);
    if (!output_descriptor)
    {
        landRefusal(materialization, MaterializationKind::Execute,
                    "Extension view.commit did not include a primary destination buffer");
        return;
    }
    if (!acceptsCommittedViewId(committed_view.view_id))
    {
        landRefusal(materialization, MaterializationKind::Execute,
                    "Extension view.commit targeted an unexpected host view");
        return;
    }
    const std::filesystem::path root = sharedBufferRootPathForDescriptor(*output_descriptor);
    if (root.empty())
    {
        landRefusal(materialization, MaterializationKind::Execute,
                    "Extension view.commit destination buffer did not expose a managed root");
        return;
    }
    std::string error;
    if (!validateManagedExecutionOutputDescriptor(*this, std::string_view("execute"), *output_descriptor, &error,
                                                  committed_view.allow_resized_output))
    {
        landRefusal(materialization, MaterializationKind::Execute, std::move(error));
        return;
    }

    std::vector<SharedBufferDescriptor> masks;
    for (const SharedBufferDescriptor& descriptor : output_buffers)
    {
        if (descriptor.buffer_role == SharedBufferRole::Mask)
        {
            masks.push_back(descriptor);
        }
    }

    const std::uint64_t generation = startMaterialization(materialization);
    const std::shared_ptr<Materialization> state = materialization;
    const auto wake_shared = std::make_shared<std::function<void()>>(std::move(wake));
    const SharedBufferDescriptor destination = *output_descriptor;
    const an::consumer::EngineSession* const session_pointer = &session;
    QThreadPool::globalInstance()->start([state, generation, wake_shared, session_pointer, root, destination, masks]() {
        auto landed = std::make_shared<LandedMaterialization>(
            materializeExecuteOnWorker(*session_pointer, root, destination, masks));
        landMaterialization(state, generation, landed, wake_shared);
    });
}

void ExtensionSessionController::clearProcessMessageForSource(const ProcessMessageState::Source source)
{
    if (current_process_message.has_value() && current_process_message->source == source)
    {
        current_process_message.reset();
    }
}

void ExtensionSessionController::applyPreviewPresentation(
    an::consumer::ImageAsset&& output_image,
    const std::function<ImageViewControl*(ImageViewControl*, ImageViewControl*, an::consumer::ImageAsset&&)>&
        preview_presenter)
{
    discardMaterializedBuffers();

    if (preview_presenter && target_control)
    {
        preview_control = preview_presenter(target_control, preview_control.data(), std::move(output_image));
    }
    clearProcessMessageForSource(ProcessMessageState::Source::Lifecycle);
}

void ExtensionSessionController::applyExecutePresentation(
    an::consumer::ImageAsset&& output_image, std::vector<ManagedNamedImageResult>&& output_masks,
    const std::function<void(ImageViewControl*, an::consumer::ImageAsset&&, const std::string&, const std::string&)>&
        result_presenter,
    const std::function<void(ImageViewControl*, std::vector<ManagedNamedImageResult>&&)>& output_mask_presenter,
    const std::function<ImageViewControl*(ImageViewControl*, an::consumer::ImageAsset&&, const std::string&,
                                          const std::string&)>& generated_result_presenter)
{
    markExecuteCommitConsumed();
    discardMaterializedBuffers();
    clearProcessMessageForSource(ProcessMessageState::Source::Lifecycle);

    ImageViewControl* result_control = nullptr;
    if (target_control && running_session.last_view_commit.has_value())
    {
        const auto& committed_view = *running_session.last_view_commit;
        const QCborMap& history_params = hasVisibleExtensionHistoryParams(committed_view.params) ||
                                                 !hasVisibleExtensionHistoryParams(seeded_execution_params)
                                             ? committed_view.params
                                             : seeded_execution_params;
        const std::string history_step_name =
            resolveExtensionHistoryStepName(display_name, committed_view.history_step_name);
        const std::string history_step_description =
            resolveExtensionHistoryDescription(committed_view.history_step_description, history_params);
        if (create_result_view_after_successful_execute && generated_result_presenter)
        {
            result_control = generated_result_presenter(target_control, std::move(output_image), history_step_name,
                                                        history_step_description);
        }
        else if (result_presenter)
        {
            result_presenter(target_control, std::move(output_image), history_step_name, history_step_description);
            result_control = target_control;
        }
    }
    if (output_mask_presenter && !output_masks.empty() && (result_control || target_control))
    {
        output_mask_presenter(result_control ? result_control : target_control, std::move(output_masks));
    }

    clearExecuteCommit();
}

void ExtensionSessionController::reportPreviewMaterializationFailure(const std::string& result_error) const
{
    if (an::platform::isLoggingInitialized())
    {
        LOG_WARNING("Failed to materialize extension preview buffer for descriptor id={} reason={}", descriptor_id,
                    result_error);
        if (auto* dialog_service = currentDialogService())
        {
            dialog_service->notifyErrorMessage(nullptr, QString::fromStdString(display_name),
                                               QString::fromStdString(result_error));
        }
    }
}

void ExtensionSessionController::reportExecuteMaterializationFailure(const std::string& result_error)
{
    if (an::platform::isLoggingInitialized())
    {
        LOG_WARNING("Failed to materialize extension output buffer for descriptor id={} reason={}", descriptor_id,
                    result_error);
        if (auto* dialog_service = currentDialogService())
        {
            dialog_service->notifyErrorMessage(nullptr, QString::fromStdString(display_name),
                                               QString::fromStdString(result_error));
        }
    }

    if (running_session.last_view_commit.has_value())
    {
        clearExecuteCommit();
    }
}

void ExtensionSessionController::recordPumpFailure(const std::string& error) const
{
    if (an::platform::isLoggingInitialized())
    {
        const std::string reason = error.empty() ? running_session.last_error : error;
        if (running_session.state == ExtensionHostSessionState::Disconnected ||
            running_session.state == ExtensionHostSessionState::Exited)
        {
            LOG_INFO("Extension host session for descriptor id={} transitioned to state={} reason={}", descriptor_id,
                     static_cast<int>(running_session.state), reason);
        }
        else
        {
            LOG_WARNING("Extension host session for descriptor id={} transitioned to state={} reason={}", descriptor_id,
                        static_cast<int>(running_session.state), reason);
        }
    }
}

void ExtensionSessionController::advanceActivePump(
    const std::function<bool(ExtensionHostRunningSession*, std::string*)>& pump_session)
{
    if (!isActive())
    {
        return;
    }

    std::string error;
    if (!pump_session || !pump_session(&running_session, &error))
    {
        recordPumpFailure(error);
    }
}

void ExtensionSessionController::recordTerminalTransition() const
{
    if (!shouldStopForTerminalState())
    {
        return;
    }

    const ExtensionRuntimeTerminationDiagnostics diagnostics = buildTerminationDiagnostics(running_session);
    ExtensionRuntimeStatusRegistry::instance().recordTermination(running_session.prepared_session.extension_id,
                                                                 runtimeTerminationKindForState(running_session.state),
                                                                 running_session.last_error, &diagnostics);

    // Suppress the user-facing warning for non-crash terminal states.
    // Disconnected: the extension host closed its socket (e.g. user closed
    // the native process window) — this is normal lifecycle, not an error.
    // Exited: the process terminated without crashing — also benign.
    // Crashes, failures, and unresponsive sessions still show a popup.
    const bool benign_termination = running_session.state == ExtensionHostSessionState::Disconnected ||
                                    running_session.state == ExtensionHostSessionState::Exited;

    if (!running_session.last_error.empty() && !benign_termination)
    {
        if (auto* dialog_service = currentDialogService())
        {
            dialog_service->notifyWarnMessage(nullptr, QString::fromStdString(display_name),
                                              QString::fromStdString(running_session.last_error));
        }
    }
}

bool ExtensionSessionController::shouldStopAfterSuccessfulExecutePresentation(
    const bool headless_qt_platform) const noexcept
{
    // A session whose window already closed is stopped as a closed window, which records how it ended.
    return !closedByItsWindow() && running_session.handshake.ack.native_process_window &&
           (headless_qt_platform || close_after_successful_execute);
}

ExtensionSessionController::PumpAction ExtensionSessionController::advanceAfterMaterialization(
    const std::function<bool(ExtensionSessionController*, std::string*)>& start_preview)
{
    if (shouldRestartPendingPreview())
    {
        std::string preview_error;
        if (!start_preview || !start_preview(this, &preview_error))
        {
            markSessionTerminal(&running_session, ExtensionHostSessionState::Failed,
                                preview_error.empty() ? std::string("Failed to submit extension preview request")
                                                      : preview_error);
        }
    }

    if (shouldStopForTerminalState())
    {
        // A window that closed right after it committed leaves the result to land; stopping now would remove the
        // buffers it is imported from and discard it.
        if (closedByItsWindow() && owesExecuteCommit())
        {
            return PumpAction::None;
        }
        recordTerminalTransition();
        return PumpAction::StopSession;
    }

    return PumpAction::None;
}

std::optional<QRect> normalizeManagedPreviewSubArea(const bool selection_enabled, const QSize& source_image_size,
                                                    const QRect& selection_area)
{
    if (!selection_enabled || source_image_size.width() <= 0 || source_image_size.height() <= 0)
    {
        return std::nullopt;
    }

    const QRect source_bounds(QPoint(0, 0), source_image_size);
    const QRect normalized_selection = selection_area.normalized().intersected(source_bounds);
    if (normalized_selection.isEmpty())
    {
        return std::nullopt;
    }

    return normalized_selection;
}

std::vector<std::string> nativePreviewHostArguments(const ExtensionCapabilities& capabilities,
                                                    const std::uint32_t frameRate)
{
    std::vector<std::string> arguments{"--preview-frame-rate", std::to_string(frameRate)};
    if (capabilities.preview_scaling != ExtensionPreviewScaling::FullOnly)
    {
        arguments.push_back("--preview-scaling");
        arguments.push_back(std::string(extensionPreviewScalingName(capabilities.preview_scaling)));
        if (capabilities.preview_scaling == ExtensionPreviewScaling::NativeRegion)
        {
            arguments.push_back("--preview-region-margin");
            arguments.push_back(std::to_string(capabilities.preview_region_margin));
        }
    }
    return arguments;
}

std::string localEndpointName(const std::string& name, const std::string& temporary_directory,
                              const std::vector<std::string>& short_directories)
{
    const auto under = [&name](std::string directory) {
        while (directory.size() > 1u && directory.back() == '/')
        {
            directory.pop_back();
        }
        return directory + "/" + name;
    };
    if (under(temporary_directory).size() <= kLocalSocketPathLimit)
    {
        return name;
    }
    for (const std::string& directory : short_directories)
    {
        if (directory.empty())
        {
            continue;
        }
        if (std::string path = under(directory); path.size() <= kLocalSocketPathLimit)
        {
            return path;
        }
    }
    return name;
}

void ExtensionHostLaunchCoordinator::configureSdkService(
    GlobalSettingsPersistence* global_settings_persistence, ExtensionSdkViewStateProvider view_state_provider,
    ExtensionSdkViewSnapshotProvider view_snapshot_provider, ExtensionSdkThemeSnapshotProvider theme_snapshot_provider,
    ExtensionSdkUiInteractionHandler ui_interaction_handler, ExtensionSdkViewBindingHandler view_binding_handler,
    ExtensionSdkUiOpenImageHandler ui_open_image_handler, ExtensionWorkflowOutputHandler workflow_output_handler)
{
    sdk_service_.setGlobalSettingsPersistence(global_settings_persistence);
    sdk_service_.setViewStateProvider(std::move(view_state_provider));
    sdk_service_.setViewSnapshotProvider(std::move(view_snapshot_provider));
    sdk_service_.setThemeSnapshotProvider(std::move(theme_snapshot_provider));
    sdk_service_.setUiInteractionHandler(std::move(ui_interaction_handler));
    sdk_service_.setViewBindingHandler(std::move(view_binding_handler));
    sdk_service_.setUiOpenImageHandler(std::move(ui_open_image_handler));
    sdk_service_.setWorkflowOutputHandler(std::move(workflow_output_handler));
    host_manager_.setSdkService(&sdk_service_);
}

bool ExtensionHostLaunchCoordinator::bindSessionTargetView(const std::string_view session_id,
                                                           const std::string_view view_name, std::string* error)
{
    if (view_name.empty())
    {
        if (error)
        {
            *error = "Target view name is empty";
        }
        return false;
    }

    an::ui::IAppContext* const context = an::ui::appContext();
    if (!context)
    {
        if (error)
        {
            *error = "Application context is unavailable";
        }
        return false;
    }

    an::ui::IViewControlManager* const view_control_manager = context->getViewControlManager();
    if (!view_control_manager)
    {
        if (error)
        {
            *error = "View control manager is unavailable";
        }
        return false;
    }

    ImageViewControl* const control = view_control_manager->getViewControlByName(std::string(view_name));
    if (!control)
    {
        if (error)
        {
            *error = "Requested target view no longer exists";
        }
        return false;
    }

    for (auto& session : sessions_)
    {
        if (!session || !session->matchesSessionId(session_id))
        {
            continue;
        }

        if (!session->isActive())
        {
            if (error)
            {
                *error = "Extension session is not active";
            }
            return false;
        }

        session->target_control = control;
        session->target_view_id = control->getUniqueName();
        session->target_view_name = control->getUniqueName();
        if (!session->running_session.initial_sdk_state_attempted)
        {
            session->running_session.initial_view_name = session->target_view_name;
        }
        session->target_file_name.clear();
        return true;
    }

    if (error)
    {
        *error = "Extension session was not found";
    }
    return false;
}

void ExtensionHostLaunchCoordinator::setManagedImageHooks(ManagedResultPresenter result_presenter,
                                                          ManagedPreviewPresenter preview_presenter,
                                                          ManagedAuxiliaryBufferProvider auxiliary_buffer_provider,
                                                          ManagedOutputMaskPresenter output_mask_presenter,
                                                          ManagedPreviewSubAreaProvider preview_sub_area_provider,
                                                          ManagedGeneratedResultPresenter generated_result_presenter)
{
    managed_result_presenter_ = std::move(result_presenter);
    managed_generated_result_presenter_ = std::move(generated_result_presenter);
    managed_preview_presenter_ = std::move(preview_presenter);
    managed_auxiliary_buffer_provider_ = std::move(auxiliary_buffer_provider);
    managed_output_mask_presenter_ = std::move(output_mask_presenter);
    managed_preview_sub_area_provider_ = std::move(preview_sub_area_provider);
}

void ExtensionHostLaunchCoordinator::setManagedViewNameProvider(ManagedViewNameProvider view_name_provider)
{
    managed_view_name_provider_ = std::move(view_name_provider);
}

void ExtensionHostLaunchCoordinator::setSessionPumpWakeCallback(SessionPumpWakeCallback callback)
{
    session_pump_wake_callback_ = std::move(callback);
}

void ExtensionHostLaunchCoordinator::setParameterSchemaReadBoundMs(const int bound_ms) noexcept
{
    if (bound_ms > 0)
    {
        parameter_schema_read_bound_ms_ = bound_ms;
    }
}

void ExtensionHostLaunchCoordinator::refreshPreviewFrameRate()
{
    an::ui::IAppContext* context = an::ui::appContext();
    if (context == nullptr || context->getGlobalSettings() == nullptr)
    {
        return;
    }
    if (!preview_frame_rate_->requests.request())
    {
        // The read on its way reads once more before it ends.
        return;
    }
    // The settings store answers on a thread of its own, so the read waits there, on a worker, never on the GUI thread.
    // The store is found through the application's context as the worker reads it, and only while the context the read
    // was asked under is still the one installed: the application drains the pool before its settings store goes and
    // before it lets the context go, and a read that finds the context gone reads nothing.
    QThreadPool::globalInstance()->start([context, read = preview_frame_rate_]() {
        for (;;)
        {
            const std::uint32_t answered = read->requests.begin();
            an::ui::IAppContext* installed = an::ui::appContext();
            an::ui::IGlobalSettingsProvider* settings =
                installed == context && installed != nullptr ? installed->getGlobalSettings() : nullptr;
            if (settings != nullptr)
            {
                try
                {
                    read->rate.store(
                        an::ui::readPreviewFrameRate([settings](const QString& key, const QString& fallback) {
                            return settings->select(key, fallback);
                        }));
                }
                catch (const std::exception& error)
                {
                    LOG_WARNING(
                        "The preview frame rate could not be read from the settings; the last one read is kept: {}",
                        error.what());
                }
                catch (...)
                {
                    LOG_WARNING(
                        "The preview frame rate could not be read from the settings; the last one read is kept");
                }
            }
            // Asked again while it read: read once more; a request after this ends starts a read of its own.
            if (!read->requests.end(answered))
            {
                return;
            }
        }
    });
}

std::uint32_t ExtensionHostLaunchCoordinator::previewFrameRate() const noexcept
{
    return preview_frame_rate_->rate.load();
}

void ExtensionHostLaunchCoordinator::armSessionPumpWake(ExtensionSessionController* session)
{
    if (!session || !session->running_session.socket)
    {
        return;
    }

    auto wake_pump = [this]() {
        if (session_pump_wake_callback_)
        {
            session_pump_wake_callback_();
        }
    };
    auto* socket = session->running_session.socket.get();
    QObject::connect(socket, &QLocalSocket::readyRead, socket, wake_pump);
    QObject::connect(socket, &QLocalSocket::disconnected, socket, wake_pump);
    if (socket->bytesAvailable() > 0)
    {
        wake_pump();
    }
}

ExtensionHostPreparedSession ExtensionHostManager::prepareSession(const ExtensionHostLaunchRequest& request,
                                                                  const std::filesystem::path& app_dir) const
{
    ExtensionHostPreparedSession session;
    session.extension_id = request.extension_id;
    session.display_name = request.display_name;
    session.session_id = generateOpaqueToken();
    session.endpoint_name = "AfterNightExtensionHost_" + generateOpaqueToken();
#ifndef _WIN32
    // The endpoint is a socket file under the temporary directory, whose whole path has to fit a socket address: under
    // a temporary directory too deep for it, the endpoint is a path of its own in the user's runtime directory or /tmp.
    std::vector<std::string> short_directories;
    if (const QString runtime = qEnvironmentVariable("XDG_RUNTIME_DIR"); !runtime.isEmpty())
    {
        short_directories.push_back(QFile::encodeName(QDir::cleanPath(runtime)).toStdString());
    }
    short_directories.push_back("/tmp");
    session.endpoint_name = localEndpointName(
        session.endpoint_name, QFile::encodeName(QDir::cleanPath(QDir::tempPath())).toStdString(), short_directories);
#endif
    session.session_secret = generateOpaqueToken();
    session.sdk_backend = request.sdk_backend;
    session.launch_mode = request.launch_mode;
    session.host_version = request.host_version;
    session.capabilities = request.capabilities;
    session.host_arguments = request.host_arguments;
    session.inspect_settings_snapshot_cbor_base64 = request.inspect_settings_snapshot_cbor_base64;
    session.handshake_timeout_ms = request.handshake_timeout_ms;
    session.defer_runtime_bootstrap = request.defer_runtime_bootstrap;

    std::filesystem::path resolved_app_dir = app_dir;
    if (resolved_app_dir.empty() && QCoreApplication::instance())
    {
        resolved_app_dir = std::filesystem::path(QCoreApplication::applicationDirPath().toStdString());
    }

    session.host_executable = request.host_executable.empty()
                                  ? defaultExtensionHostExecutableForBackend(session.sdk_backend, resolved_app_dir)
                                  : request.host_executable;

    session.ack_template.accepted = true;
    session.ack_template.session_id = session.session_id;
    session.ack_template.protocol_version = kExtensionProtocolVersion;
    session.ack_template.sdk_version = kExtensionSdkVersion;
    session.ack_template.host_version = request.host_version;
    session.ack_template.heartbeat_interval_ms = request.heartbeat_interval_ms;
    session.ack_template.heartbeat_miss_limit = request.heartbeat_miss_limit;
    session.ack_template.cancel_grace_period_ms = request.cancel_grace_period_ms;
    session.ack_template.capabilities = request.capabilities;

    return session;
}

void ExtensionHostManager::setSdkService(ExtensionSdkService* sdk_service) noexcept
{
    sdk_service_ = sdk_service;
}

bool ExtensionHostManager::launchSession(const ExtensionHostPreparedSession& session,
                                         ExtensionHostRunningSession* running_session, std::string* error) const
{
    const auto fail = [&](std::string detail) -> bool {
#ifdef _WIN32
        // A launch that fails has let its host go, and goes without the job it assigned the host to: the caller stops
        // no session for it, so nothing else would close the handle.
        if (running_session)
        {
            closeJobHandleNoThrow(&running_session->process_job_handle);
        }
#endif
        if (error)
        {
            *error = detail;
        }
        logExtensionHostLaunchFailure(session, detail);
        return false;
    };

    if (!running_session)
    {
        return fail("Extension host running-session output is null");
    }

    std::string local_error;
    if (!validatePreparedSession(session, &local_error))
    {
        return fail(local_error);
    }

    SessionSecretPipe session_secret_pipe;
    if (!createSessionSecretPipe(&session_secret_pipe, &local_error))
    {
        return fail(local_error);
    }

    auto server = std::make_unique<QLocalServer>();
    server->removeServer(QString::fromStdString(session.endpoint_name));
    server->setSocketOptions(QLocalServer::UserAccessOption);
    if (!server->listen(QString::fromStdString(session.endpoint_name)))
    {
        return fail("Failed to listen on the extension host endpoint: " + server->errorString().toStdString());
    }

    auto process = std::make_unique<QProcess>();
    process->setProcessChannelMode(QProcess::MergedChannels);
    process->setProgram(QString::fromStdString(session.host_executable.string()));
    QProcessEnvironment process_environment = QProcessEnvironment::systemEnvironment();
    constexpr auto defer_bootstrap_environment_variable = "AFTERNIGHT_DEFER_EXTENSION_BOOTSTRAP";
    if (session.defer_runtime_bootstrap)
    {
        process_environment.insert(QString::fromLatin1(defer_bootstrap_environment_variable), QStringLiteral("1"));
    }
    else
    {
        process_environment.remove(QString::fromLatin1(defer_bootstrap_environment_variable));
    }
    process->setProcessEnvironment(process_environment);
    QStringList host_arguments = baseHostArguments(session);
#ifndef _WIN32
    process->setInputChannelMode(QProcess::ForwardedInputChannel);
    const int session_secret_read_fd = session_secret_pipe.read_fd;
    const pid_t parent_process_id = static_cast<pid_t>(::getpid());
    process->setChildProcessModifier([process_ptr = process.get(), session_secret_read_fd, parent_process_id]() {
        if (session_secret_read_fd < 0)
        {
            process_ptr->failChildProcessModifier("Session-secret pipe is unavailable");
            return;
        }

        if (::prctl(PR_SET_PDEATHSIG, SIGTERM) == -1)
        {
            process_ptr->failChildProcessModifier("Failed to configure parent-death handling for the extension host",
                                                  errno);
            return;
        }

        if (::getppid() != parent_process_id)
        {
            process_ptr->failChildProcessModifier(
                "The parent UI process exited before the extension host finished launching");
            return;
        }

        if (::setpgid(0, 0) == -1)
        {
            process_ptr->failChildProcessModifier("Failed to place the extension host into its own process group",
                                                  errno);
            return;
        }

        if (session_secret_read_fd != kSessionSecretPipeDescriptor &&
            ::dup2(session_secret_read_fd, kSessionSecretPipeDescriptor) == -1)
        {
            process_ptr->failChildProcessModifier("Failed to duplicate the session-secret pipe into the child process",
                                                  errno);
            return;
        }

        if (session_secret_read_fd != kSessionSecretPipeDescriptor)
        {
            ::close(session_secret_read_fd);
        }
    });
#else
    // The numeric handle value is passed on the command line so the child can
    // call ReadFile on it.  Only the handle number is exposed — the actual
    // session secret travels exclusively through the inherited pipe.
    host_arguments.push_back(QStringLiteral("--session-secret-handle"));
    host_arguments.push_back(
        QString::number(static_cast<qulonglong>(reinterpret_cast<std::uintptr_t>(session_secret_pipe.read_handle))));
    process->setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* arguments) {
        if (arguments)
        {
            arguments->inheritHandles = true;
        }
    });
#endif
    process->setArguments(host_arguments);
    process->start();
    if (!process->waitForStarted(session.handshake_timeout_ms))
    {
        return fail("Failed to start the extension host process: " + process->errorString().toStdString());
    }

#ifdef _WIN32
    if (!assignProcessToKillOnCloseJob(*process, &running_session->process_job_handle, &local_error))
    {
        terminateProcess(*process);
        return fail(local_error);
    }
#endif

#ifndef _WIN32
    if (!writeAllToFd(session_secret_pipe.write_fd, session.session_secret, &local_error))
    {
        terminateProcess(*process);
        return fail(local_error);
    }
    closeFdNoThrow(&session_secret_pipe.write_fd);
    closeFdNoThrow(&session_secret_pipe.read_fd);
#else
    if (!writeAllToHandle(session_secret_pipe.write_handle, session.session_secret, &local_error))
    {
        terminateProcess(*process);
        return fail(local_error);
    }
    closeHandleNoThrow(&session_secret_pipe.write_handle);
    closeHandleNoThrow(&session_secret_pipe.read_handle);
#endif

    // A host asked to connect first does so before it starts its runtime, within the handshake's bound. One that starts
    // the runtime and imports the extension before it connects takes as long as that start does - long on a first start
    // from a freshly provisioned environment - and is waited for while it runs, up to the runtime's start bound.
    const int connect_timeout_ms = session.defer_runtime_bootstrap
                                       ? session.handshake_timeout_ms
                                       : (std::max)(session.handshake_timeout_ms, kRuntimeStartTimeoutMs);
    switch (waitForHostConnection(*server, *process, connect_timeout_ms))
    {
    case HostConnectionWait::Connected:
        break;
    case HostConnectionWait::Exited:
        return fail("The extension host exited before it connected" + processFailureContext(*process));
    case HostConnectionWait::TimedOut:
        terminateProcess(*process);
        return fail("Timed out waiting for the extension host to connect" + processFailureContext(*process));
    }

    std::unique_ptr<QLocalSocket> socket(server->nextPendingConnection());
    if (!socket)
    {
        terminateProcess(*process);
        return fail("Extension host connected without a pending socket");
    }
    socket->setParent(nullptr);
    if (detectDuplicateConnection(*server, session.handshake_timeout_ms))
    {
        closePendingConnections(*server);
        writeAckMessage(socket.get(), session.handshake_timeout_ms,
                        rejectedAck(session, 10, "Duplicate local client connection",
                                    "Only one authenticated extension-host client may connect per session."),
                        nullptr);
        terminateProcess(*process);
        return fail("Rejected duplicate local client connections for the prepared session" +
                    processFailureContext(*process));
    }

    ExtensionProtocolMessage hello_message;
    QByteArray handshake_leftover;
    if (!readFramedMessage(socket.get(), session.handshake_timeout_ms, &hello_message, &local_error,
                           &handshake_leftover))
    {
        terminateProcess(*process);
        return fail(local_error);
    }

    RuntimeHelloMessage hello;
    if (!runtimeHelloFromProtocolMessage(hello_message, &hello, &local_error))
    {
        terminateProcess(*process);
        return fail(local_error);
    }

    const auto compatibility = negotiateProtocolVersion(kExtensionProtocolVersion, hello.protocol_version);
    if (!compatibility.accepted())
    {
        writeAckMessage(socket.get(), session.handshake_timeout_ms,
                        rejectedAck(session, 11, "Protocol version mismatch", compatibility.diagnostic), nullptr);
        terminateProcess(*process);
        return fail(compatibility.diagnostic);
    }
    if (hello.sdk_version != kExtensionSdkVersion)
    {
        const std::string details =
            "Extension host requested unsupported SDK version " + std::to_string(hello.sdk_version);
        writeAckMessage(socket.get(), session.handshake_timeout_ms,
                        rejectedAck(session, 12, "SDK version mismatch", details), nullptr);
        terminateProcess(*process);
        return fail(details);
    }
    if (hello.sdk_backend != session.sdk_backend || !isSupportedSdkBackend(hello.sdk_backend))
    {
        const std::string details = "Expected backend '" + session.sdk_backend +
                                    "' but the extension host requested '" + hello.sdk_backend + "'";
        writeAckMessage(socket.get(), session.handshake_timeout_ms,
                        rejectedAck(session, 13, "Unsupported SDK backend negotiation", details), nullptr);
        terminateProcess(*process);
        return fail(details);
    }

    // Use constant-time comparison for the session secret to avoid
    // timing side-channel leakage, even though the attack surface is
    // local-only and the tokens are UUID-strength.
    const bool secret_match = constantTimeStringEqual(session.session_secret, hello.session_secret_proof);
    if (hello.session_id != session.session_id || hello.extension_id != session.extension_id || !secret_match)
    {
        writeAckMessage(
            socket.get(), session.handshake_timeout_ms,
            rejectedAck(session, 14, "Session authentication failed",
                        "The extension host handshake did not match the prepared session identity or secret."),
            nullptr);
        terminateProcess(*process);
        return fail("Extension host handshake proof did not match the prepared session");
    }

    // Mirror the extension host's native_process_window capability back in the ack
    // so both sides agree on the session window model.
    RuntimeHelloAckMessage accepted_ack = session.ack_template;
    accepted_ack.native_process_window = hello.native_process_window;
    accepted_ack.bootstrap_status_messages = hello.bootstrap_status_messages;

    if (!writeAckMessage(socket.get(), session.handshake_timeout_ms, accepted_ack, &local_error))
    {
        terminateProcess(*process);
        return fail(local_error);
    }

    server->close();
    running_session->prepared_session = session;
    running_session->handshake.hello = std::move(hello);
    running_session->handshake.ack = std::move(accepted_ack);
    running_session->process = std::move(process);
    running_session->socket = std::move(socket);
    running_session->buffered_input = std::move(handshake_leftover);
    running_session->session_clock.start();
    running_session->state = ExtensionHostSessionState::Active;
    running_session->last_heartbeat_elapsed_ms = 0;
    running_session->last_ping_sent_elapsed_ms = -1;
    running_session->successful_heartbeat_count = 0;
    running_session->missed_heartbeats = 0;
    running_session->next_ping_sequence = 1;
    running_session->ping_outstanding = false;
    running_session->outstanding_ping_request_id.clear();
    running_session->in_flight_request_id.clear();
    running_session->in_flight_request_kind.clear();
    running_session->in_flight_params_token.clear();
    running_session->in_flight_request_buffers.clear();
    running_session->cancel_requested_elapsed_ms = -1;
    running_session->cancel_requested = false;
    running_session->completed_request_count = 0;
    running_session->serviced_sdk_message_count = 0;
    running_session->initial_view_state_sent = false;
    running_session->initial_theme_state_sent = false;
    running_session->initial_sdk_state_attempted = false;
    running_session->initial_view_name.clear();
    running_session->runtime_ready = !running_session->handshake.ack.bootstrap_status_messages;
    running_session->runtime_bootstrap_state_ready = false;
    running_session->last_preview_result.reset();
    running_session->last_view_commit.reset();
    running_session->latest_ui_validation_request_id.clear();
    running_session->last_ui_validation_result.reset();
    running_session->last_error.clear();

    if (sdk_service_ && hasHostArgument(session.host_arguments, "--stay-alive-ms") &&
        !running_session->handshake.ack.bootstrap_status_messages)
    {
        if (!ensureInitialSdkSessionMessagesSent(sdk_service_, running_session, &local_error))
        {
            terminateProcess(*running_session->process);
            return fail(local_error);
        }
        running_session->initial_sdk_state_attempted = true;
    }

    if (running_session->process->state() != QProcess::Running)
    {
        running_session->handshake.process_exit_code = running_session->process->exitCode();
        running_session->handshake.process_output = processOutput(*running_session->process);
        markSessionTerminal(running_session, ExtensionHostSessionState::Exited,
                            "Extension host process exited immediately after handshake" +
                                processFailureContext(*running_session->process));
    }

    logExtensionHostLaunchSuccess(session);
    return true;
}

bool ExtensionHostManager::launchAndHandshake(const ExtensionHostPreparedSession& session,
                                              ExtensionHostHandshakeResult* result, std::string* error) const
{
    ExtensionHostRunningSession running_session;
    if (!launchSession(session, &running_session, error))
    {
        return false;
    }

    if (!running_session.runtime_ready && hasHostArgument(session.host_arguments, "--stay-alive-ms"))
    {
        QElapsedTimer bootstrap_timer;
        bootstrap_timer.start();
        std::string bootstrap_error;
        while (!running_session.runtime_ready && running_session.state == ExtensionHostSessionState::Active &&
               bootstrap_timer.elapsed() < kRuntimeStartTimeoutMs)
        {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 1);
            if (!pumpSession(&running_session, 20, &bootstrap_error))
            {
                if (error)
                {
                    *error = bootstrap_error;
                }
                return false;
            }
        }
        if (!running_session.runtime_ready)
        {
            stopSession(&running_session);
            if (error)
            {
                *error = "Timed out waiting for the extension runtime to finish bootstrapping";
            }
            return false;
        }
    }

    if (running_session.socket)
    {
        running_session.socket->disconnectFromServer();
        if (running_session.socket->state() != QLocalSocket::UnconnectedState)
        {
            running_session.socket->waitForDisconnected((std::max)(1, session.handshake_timeout_ms / 2));
        }
    }

    if (running_session.process->state() == QProcess::Running &&
        !running_session.process->waitForFinished(session.handshake_timeout_ms))
    {
        terminateProcess(*running_session.process);
        const std::string detail = "Timed out waiting for the extension host process to finish" +
                                   processFailureContext(*running_session.process);
        logExtensionHostLaunchFailure(session, detail);
        if (error)
        {
            *error = detail;
        }
        return false;
    }

    if (running_session.process->exitStatus() != QProcess::NormalExit || running_session.process->exitCode() != 0)
    {
        const std::string detail =
            "Extension host process exited with an error" + processFailureContext(*running_session.process);
        logExtensionHostLaunchFailure(session, detail);
        if (error)
        {
            *error = detail;
        }
        return false;
    }

    running_session.handshake.process_exit_code = running_session.process->exitCode();
    running_session.handshake.process_output = processOutput(*running_session.process);
    if (result)
    {
        *result = running_session.handshake;
    }

    return true;
}

bool ExtensionHostManager::pumpSession(ExtensionHostRunningSession* running_session, const int timeout_ms,
                                       std::string* error) const
{
    if (!running_session)
    {
        if (error)
        {
            *error = "Extension host running session is null";
        }
        return false;
    }
    if (running_session->state != ExtensionHostSessionState::Active)
    {
        if (error)
        {
            *error = running_session->last_error.empty() ? "Extension host session is not active"
                                                         : running_session->last_error;
        }
        return false;
    }
    if (!running_session->process || !running_session->socket)
    {
        markSessionTerminal(running_session, ExtensionHostSessionState::Failed,
                            "Extension host session is missing process or socket state");
        if (error)
        {
            *error = running_session->last_error;
        }
        return false;
    }

    auto discard_in_flight_workflow_session = [&]() {
        if (!sdk_service_ || running_session->in_flight_request_kind != "workflow")
        {
            return;
        }

        std::string cleanup_error;
        if (!sdk_service_->discardWorkflowSession(running_session->prepared_session.session_id, &cleanup_error) &&
            an::platform::isLoggingInitialized())
        {
            LOG_WARNING("Failed to discard workflow session id={} after terminal state: {}",
                        running_session->prepared_session.session_id, cleanup_error);
        }
    };

    auto fail_session = [&](const ExtensionHostSessionState state, std::string message) -> bool {
        if (state == ExtensionHostSessionState::Disconnected || state == ExtensionHostSessionState::Exited)
        {
            LOG_INFO("Extension host session id={} state={} reason={}", running_session->prepared_session.session_id,
                     static_cast<int>(state), message);
        }
        else
        {
            LOG_WARNING("Extension host session id={} state={} reason={}", running_session->prepared_session.session_id,
                        static_cast<int>(state), message);
        }
        // For Disconnected, the extension host is already shutting down on its
        // own — killing it here would block the UI thread needlessly.  The
        // subsequent stopSession() call handles final cleanup.
        if (state == ExtensionHostSessionState::Crashed || state == ExtensionHostSessionState::Unresponsive ||
            state == ExtensionHostSessionState::Failed)
        {
            if (running_session->process->state() == QProcess::Running)
            {
                terminateProcessTree(*running_session->process
#ifdef _WIN32
                                     ,
                                     &running_session->process_job_handle
#endif
                );
            }
#ifdef _WIN32
            else
            {
                closeJobHandleNoThrow(&running_session->process_job_handle);
            }
#endif
        }
        discard_in_flight_workflow_session();
        running_session->handshake.process_exit_code = running_session->process->exitCode();
        running_session->handshake.process_output = processOutput(*running_session->process);
        if (!running_session->handshake.process_output.empty())
        {
            message += " output: ";
            message += running_session->handshake.process_output;
        }
        markSessionTerminal(running_session, state, std::move(message));
        if (error)
        {
            *error = running_session->last_error;
        }
        return false;
    };

    std::string local_error;

    const int bounded_timeout_ms = std::clamp(timeout_ms, 0, 1000);
    if (running_session->socket->bytesAvailable() == 0 && bounded_timeout_ms > 0 &&
        running_session->socket->state() == QLocalSocket::ConnectedState)
    {
        running_session->socket->waitForReadyRead(bounded_timeout_ms);
    }
    running_session->buffered_input.append(running_session->socket->readAll());

    while (true)
    {
        ExtensionProtocolMessage message;
        const auto status = tryConsumeFramedProtocolMessage(&running_session->buffered_input, &message, &local_error);
        if (status == ProtocolFrameDecodeStatus::NeedMoreData)
        {
            break;
        }
        if (status == ProtocolFrameDecodeStatus::InvalidFrame)
        {
            return fail_session(ExtensionHostSessionState::Failed,
                                "Extension host sent an invalid framed control message: " + local_error);
        }

        if (message.type == "runtime.ready")
        {
            if (message.session_id != running_session->prepared_session.session_id || running_session->runtime_ready)
            {
                return fail_session(ExtensionHostSessionState::Failed,
                                    "Extension host sent an invalid or duplicate runtime.ready message");
            }
            running_session->runtime_ready = true;
            running_session->session_clock.restart();
            running_session->last_heartbeat_elapsed_ms = 0;
            running_session->last_ping_sent_elapsed_ms = -1;
            running_session->missed_heartbeats = 0;
            continue;
        }

        if (message.type == "runtime.bootstrap_state_ready")
        {
            if (message.session_id != running_session->prepared_session.session_id || running_session->runtime_ready ||
                running_session->runtime_bootstrap_state_ready)
            {
                return fail_session(ExtensionHostSessionState::Failed,
                                    "Extension host sent an invalid runtime.bootstrap_state_ready message");
            }
            running_session->runtime_bootstrap_state_ready = true;
            continue;
        }

        if (message.type == "runtime.bootstrap_failed")
        {
            if (message.session_id != running_session->prepared_session.session_id || running_session->runtime_ready)
            {
                return fail_session(ExtensionHostSessionState::Failed,
                                    "Extension host sent an invalid runtime.bootstrap_failed message");
            }
            const QString detail = message.fields.value(QStringLiteral("detail")).toString().trimmed();
            return fail_session(ExtensionHostSessionState::Failed,
                                detail.isEmpty() ? std::string("Extension runtime bootstrap failed")
                                                 : "Extension runtime bootstrap failed: " + detail.toStdString());
        }

        if (message.type == "runtime.pong")
        {
            RuntimePongMessage pong;
            if (!runtimePongFromProtocolMessage(message, &pong, &local_error))
            {
                return fail_session(ExtensionHostSessionState::Failed,
                                    "Extension host sent an invalid runtime.pong message: " + local_error);
            }
            if (!running_session->ping_outstanding || pong.request_id != running_session->outstanding_ping_request_id)
            {
                return fail_session(ExtensionHostSessionState::Failed,
                                    "Extension host replied with an unexpected runtime.pong request id");
            }

            running_session->ping_outstanding = false;
            running_session->outstanding_ping_request_id.clear();
            running_session->missed_heartbeats = 0;
            running_session->successful_heartbeat_count += 1;
            running_session->last_heartbeat_elapsed_ms = running_session->session_clock.elapsed();
            continue;
        }

        if (message.type == "runtime.closed")
        {
            running_session->native_window_close_requested = true;
            if (running_session->process && running_session->process->state() == QProcess::Running)
            {
                killProcessTreeNoWait(*running_session->process
#ifdef _WIN32
                                      ,
                                      &running_session->process_job_handle
#endif
                );
            }
            return fail_session(ExtensionHostSessionState::Disconnected, "Extension host native process window closed");
        }

        if (message.type == "execution.result")
        {
            ExecutionResultMessage execution_result;
            if (!executionResultFromProtocolMessage(message, &execution_result, &local_error))
            {
                return fail_session(ExtensionHostSessionState::Failed,
                                    "Extension host sent an invalid execution.result message: " + local_error);
            }
            const bool execute_result = execution_result.result_kind == "execute";
            if (running_session->in_flight_request_id.empty())
            {
                return fail_session(
                    ExtensionHostSessionState::Failed,
                    execute_result
                        ? std::string("Extension host sent execute completion via execution.result; execute must "
                                      "complete via view.commit")
                        : std::string("Extension host replied with an unexpected execution result target id"));
            }
            if (execute_result)
            {
                return fail_session(ExtensionHostSessionState::Failed,
                                    "Extension host sent execute completion via execution.result; execute must "
                                    "complete via view.commit");
            }
            if (execution_result.target_request_id != running_session->in_flight_request_id)
            {
                return fail_session(ExtensionHostSessionState::Failed,
                                    "Extension host replied with an unexpected execution result target id");
            }
            if (!validateExecutionResultForSession(*running_session, execution_result, &local_error))
            {
                return fail_session(ExtensionHostSessionState::Failed,
                                    "Extension host sent an invalid execution.result message: " + local_error);
            }

            if (execution_result.result_kind == "workflow" && sdk_service_)
            {
                if (execution_result.status == "succeeded")
                {
                    std::string presentation_error;
                    if (!sdk_service_->presentWorkflowOutputs(running_session->prepared_session.session_id,
                                                              &presentation_error) &&
                        an::platform::isLoggingInitialized())
                    {
                        LOG_WARNING("Failed to present workflow outputs for session id={}: {}",
                                    running_session->prepared_session.session_id, presentation_error);
                    }
                }
                else
                {
                    std::string cleanup_error;
                    if (!sdk_service_->discardWorkflowSession(running_session->prepared_session.session_id,
                                                              &cleanup_error) &&
                        an::platform::isLoggingInitialized())
                    {
                        LOG_WARNING("Failed to discard workflow session id={} after status {}: {}",
                                    running_session->prepared_session.session_id, execution_result.status,
                                    cleanup_error);
                    }
                }
            }

            running_session->last_preview_result = execution_result;
            running_session->completed_request_count += 1;
            running_session->in_flight_request_id.clear();
            running_session->in_flight_request_kind.clear();
            running_session->in_flight_params_token.clear();
            running_session->in_flight_request_buffers.clear();
            running_session->cancel_requested = false;
            running_session->cancel_requested_elapsed_ms = -1;
            continue;
        }

        if (message.type == "view.commit")
        {
            ViewCommitMessage view_commit;
            if (!viewCommitFromProtocolMessage(message, &view_commit, &local_error))
            {
                return fail_session(ExtensionHostSessionState::Failed,
                                    "Extension host sent an invalid view.commit message: " + local_error);
            }
            if (!validateViewCommitForSession(*running_session, view_commit, &local_error))
            {
                return fail_session(ExtensionHostSessionState::Failed,
                                    "Extension host sent an invalid view.commit message: " + local_error);
            }
            running_session->last_view_commit = std::move(view_commit);
            running_session->completed_request_count += 1;
            running_session->in_flight_request_id.clear();
            running_session->in_flight_request_kind.clear();
            running_session->in_flight_params_token.clear();
            running_session->in_flight_request_buffers.clear();
            running_session->cancel_requested = false;
            running_session->cancel_requested_elapsed_ms = -1;
            continue;
        }

        if (message.type == "ui.validate.result")
        {
            // A declarative UI validation result is not acted on; its state is cleared.
            running_session->latest_ui_validation_request_id.clear();
            running_session->last_ui_validation_result.reset();
            continue;
        }

        // Attempt SDK service dispatch for messages with a request_id.
        if (!message.request_id.empty())
        {
            const bool handled_by_sdk_service =
                sdk_service_ && dispatchSdkServiceMessage(sdk_service_, running_session, message, &local_error);
            if (handled_by_sdk_service)
            {
                continue;
            }

            if (!local_error.empty())
            {
                return fail_session(ExtensionHostSessionState::Failed,
                                    "Extension host sent an invalid sdk service request: " + local_error);
            }

            if ((message.type.rfind("logging.", 0) == 0) || (message.type.rfind("settings.", 0) == 0) ||
                (message.type.rfind("ui.", 0) == 0) || (message.type.rfind("views.", 0) == 0) ||
                (message.type.rfind("diagnostics.", 0) == 0))
            {
                if (!writeSdkServiceFallbackResponse(running_session, message, &local_error))
                {
                    return fail_session(ExtensionHostSessionState::Failed,
                                        "Failed to write sdk service fallback response: " + local_error);
                }
                continue;
            }
        }

        // Forward-compatible: silently skip unrecognized message types (e.g.
        // future execution.progress, logging.event) instead of killing the session.
        continue;
    }

    if ((running_session->runtime_ready || running_session->runtime_bootstrap_state_ready) &&
        !running_session->initial_sdk_state_attempted)
    {
        if (!ensureInitialSdkSessionMessagesSent(sdk_service_, running_session, &local_error))
        {
            return fail_session(ExtensionHostSessionState::Failed,
                                "Failed to send initial sdk session state: " + local_error);
        }
        if (!running_session->runtime_ready)
        {
            ExtensionProtocolMessage bootstrap_state_complete;
            bootstrap_state_complete.type = "runtime.bootstrap_state_complete";
            bootstrap_state_complete.session_id = running_session->prepared_session.session_id;
            if (!writeProtocolMessage(running_session->socket.get(), bootstrap_state_complete,
                                      running_session->prepared_session.handshake_timeout_ms, &local_error))
            {
                return fail_session(ExtensionHostSessionState::Failed,
                                    "Failed to complete extension bootstrap state transfer: " + local_error);
            }
        }
        running_session->initial_sdk_state_attempted = true;
    }

    if (!flushDeferredSdkServiceResponses(sdk_service_, running_session, &local_error))
    {
        return fail_session(ExtensionHostSessionState::Failed,
                            "Failed to write deferred sdk service response: " + local_error);
    }

    if (running_session->socket->state() == QLocalSocket::UnconnectedState)
    {
        if (running_session->process->state() == QProcess::Running)
        {
            running_session->process->waitForFinished(50);
        }

        const bool native_window_close = running_session->handshake.ack.native_process_window &&
                                         (running_session->native_window_close_requested ||
                                          (running_session->native_process_window_rt_preview &&
                                           running_session->process->state() != QProcess::Running &&
                                           running_session->process->exitStatus() == QProcess::CrashExit &&
                                           running_session->in_flight_request_id.empty()));
        const bool crashed = running_session->process->state() != QProcess::Running &&
                             running_session->process->exitStatus() == QProcess::CrashExit && !native_window_close;
        std::string message = native_window_close ? "Extension host native process window closed"
                                                  : (crashed ? "Extension host process crashed"
                                                             : "Extension host control channel disconnected");
        if (running_session->process->state() != QProcess::Running && !native_window_close)
        {
            message += processFailureContext(*running_session->process);
        }
        return fail_session(crashed ? ExtensionHostSessionState::Crashed : ExtensionHostSessionState::Disconnected,
                            std::move(message));
    }
    if (running_session->process->state() != QProcess::Running)
    {
        const bool native_window_close = running_session->handshake.ack.native_process_window &&
                                         (running_session->native_window_close_requested ||
                                          (running_session->native_process_window_rt_preview &&
                                           running_session->process->exitStatus() == QProcess::CrashExit &&
                                           running_session->in_flight_request_id.empty()));
        const bool crashed = running_session->process->exitStatus() == QProcess::CrashExit && !native_window_close;
        std::string message = native_window_close
                                  ? "Extension host native process window closed"
                                  : (crashed ? "Extension host process crashed" : "Extension host process exited");
        if (!native_window_close)
        {
            message += processFailureContext(*running_session->process);
        }
        return fail_session(crashed ? ExtensionHostSessionState::Crashed : ExtensionHostSessionState::Exited,
                            std::move(message));
    }

    if (!running_session->runtime_ready)
    {
        if (running_session->session_clock.elapsed() >= kRuntimeStartTimeoutMs)
        {
            return fail_session(ExtensionHostSessionState::Unresponsive,
                                "Extension runtime did not finish bootstrapping within 60 seconds");
        }
        return true;
    }

    const qint64 elapsed_ms = running_session->session_clock.elapsed();
    const int heartbeat_interval_ms = (std::max)(1, running_session->handshake.ack.heartbeat_interval_ms);
    const int heartbeat_miss_limit = (std::max)(1, running_session->handshake.ack.heartbeat_miss_limit);

    if (!running_session->ping_outstanding &&
        elapsed_ms - running_session->last_heartbeat_elapsed_ms >= heartbeat_interval_ms)
    {
        if (!sendRuntimePing(running_session, &local_error))
        {
            return fail_session(ExtensionHostSessionState::Failed, local_error);
        }
    }

    if (running_session->ping_outstanding &&
        elapsed_ms - running_session->last_ping_sent_elapsed_ms >= heartbeat_interval_ms)
    {
        running_session->missed_heartbeats += 1;
        running_session->last_ping_sent_elapsed_ms = elapsed_ms;
        if (running_session->missed_heartbeats >= heartbeat_miss_limit)
        {
            return fail_session(ExtensionHostSessionState::Unresponsive,
                                "Extension host missed heartbeat responses for " +
                                    std::to_string(running_session->missed_heartbeats) + " consecutive intervals");
        }
    }

    if (running_session->cancel_requested && running_session->cancel_requested_elapsed_ms >= 0 &&
        elapsed_ms - running_session->cancel_requested_elapsed_ms >=
            running_session->handshake.ack.cancel_grace_period_ms)
    {
        return fail_session(ExtensionHostSessionState::Failed,
                            "Extension host did not honor cooperative cancel within the configured grace period");
    }

    return true;
}

bool ExtensionHostManager::sendExecuteRequest(ExtensionHostRunningSession* running_session,
                                              const std::string& params_token,
                                              const std::vector<SharedBufferDescriptor>& buffers,
                                              std::string* request_id, std::string* error) const
{
    return sendExecuteRequest(running_session, params_token, QCborMap{}, buffers, request_id, error);
}

bool ExtensionHostManager::sendExecuteRequest(ExtensionHostRunningSession* running_session,
                                              const std::string& params_token, const QCborMap& params,
                                              const std::vector<SharedBufferDescriptor>& buffers,
                                              std::string* request_id, std::string* error) const
{
    if (!running_session)
    {
        if (error)
        {
            *error = "Extension host running session is null";
        }
        return false;
    }
    if (running_session->state != ExtensionHostSessionState::Active)
    {
        if (error)
        {
            *error = "Extension host session is not active";
        }
        return false;
    }
    if (!running_session->runtime_ready)
    {
        if (error)
        {
            *error = "Extension host runtime is still initializing";
        }
        return false;
    }
    if (!running_session->in_flight_request_id.empty())
    {
        if (error)
        {
            *error = "Extension host session already has an in-flight execution request";
        }
        return false;
    }
    if (!ensureInitialSdkSessionMessagesSent(sdk_service_, running_session, error))
    {
        return false;
    }

    ExecutionExecuteMessage execute;
    execute.session_id = running_session->prepared_session.session_id;
    execute.request_id = generateOpaqueToken();
    execute.params_token = params_token;
    execute.params = params;
    execute.buffers = buffers;

    if (!writeProtocolMessage(running_session->socket.get(), toProtocolMessage(execute),
                              running_session->prepared_session.handshake_timeout_ms, error))
    {
        return false;
    }

    running_session->in_flight_request_id = execute.request_id;
    running_session->in_flight_request_kind =
        running_session->prepared_session.launch_mode == ExtensionLaunchMode::Workflow ? "workflow" : "execute";
    running_session->in_flight_params_token = execute.params_token;
    running_session->in_flight_request_buffers = buffers;
    running_session->last_view_commit.reset();
    running_session->cancel_requested = false;
    running_session->cancel_requested_elapsed_ms = -1;
    if (request_id)
    {
        *request_id = execute.request_id;
    }
    return true;
}

bool ExtensionHostManager::sendPreviewRequest(ExtensionHostRunningSession* running_session,
                                              const std::string& params_token,
                                              const std::vector<SharedBufferDescriptor>& buffers,
                                              std::string* request_id, std::string* error) const
{
    return sendPreviewRequest(running_session, params_token, QCborMap{}, buffers, request_id, error);
}

bool ExtensionHostManager::sendPreviewRequest(ExtensionHostRunningSession* running_session,
                                              const std::string& params_token, const QCborMap& params,
                                              const std::vector<SharedBufferDescriptor>& buffers,
                                              std::string* request_id, std::string* error) const
{
    if (!running_session)
    {
        if (error)
        {
            *error = "Extension host running session is null";
        }
        return false;
    }
    if (running_session->state != ExtensionHostSessionState::Active)
    {
        if (error)
        {
            *error = "Extension host session is not active";
        }
        return false;
    }
    if (!running_session->runtime_ready)
    {
        if (error)
        {
            *error = "Extension host runtime is still initializing";
        }
        return false;
    }
    if (!running_session->in_flight_request_id.empty())
    {
        if (error)
        {
            *error = "Extension host session already has an in-flight execution request";
        }
        return false;
    }
    if (!ensureInitialSdkSessionMessagesSent(sdk_service_, running_session, error))
    {
        return false;
    }

    ExecutionPreviewMessage preview;
    preview.session_id = running_session->prepared_session.session_id;
    preview.request_id = generateOpaqueToken();
    preview.params_token = params_token;
    preview.params = params;
    preview.buffers = buffers;

    if (!writeProtocolMessage(running_session->socket.get(), toProtocolMessage(preview),
                              running_session->prepared_session.handshake_timeout_ms, error))
    {
        return false;
    }

    running_session->in_flight_request_id = preview.request_id;
    running_session->in_flight_request_kind = "preview";
    running_session->in_flight_params_token = preview.params_token;
    running_session->in_flight_request_buffers = buffers;
    running_session->last_view_commit.reset();
    running_session->cancel_requested = false;
    running_session->cancel_requested_elapsed_ms = -1;
    if (request_id)
    {
        *request_id = preview.request_id;
    }
    return true;
}

bool ExtensionHostManager::sendCancelRequest(ExtensionHostRunningSession* running_session, std::string* request_id,
                                             std::string* error) const
{
    if (!running_session)
    {
        if (error)
        {
            *error = "Extension host running session is null";
        }
        return false;
    }
    if (running_session->state != ExtensionHostSessionState::Active)
    {
        if (error)
        {
            *error = "Extension host session is not active";
        }
        return false;
    }
    if (running_session->in_flight_request_id.empty())
    {
        if (error)
        {
            *error = "Extension host session has no in-flight execution request to cancel";
        }
        return false;
    }

    ExecutionCancelMessage cancel;
    cancel.session_id = running_session->prepared_session.session_id;
    cancel.request_id = generateOpaqueToken();
    cancel.target_request_id = running_session->in_flight_request_id;

    if (!writeProtocolMessage(running_session->socket.get(), toProtocolMessage(cancel),
                              running_session->prepared_session.handshake_timeout_ms, error))
    {
        return false;
    }

    running_session->cancel_requested = true;
    running_session->cancel_requested_elapsed_ms = running_session->session_clock.elapsed();
    if (request_id)
    {
        *request_id = cancel.request_id;
    }
    return true;
}

bool ExtensionHostManager::submitExecuteOrQueue(ExtensionHostExecutionGateState* gate_state,
                                                ExtensionHostRunningSession* running_session,
                                                const std::string& params_token,
                                                const std::vector<SharedBufferDescriptor>& buffers,
                                                ExtensionHostExecutionDispatchResult* dispatch_result,
                                                std::string* error) const
{
    return submitExecuteOrQueue(gate_state, running_session, params_token, QCborMap{}, buffers, dispatch_result, error);
}

bool ExtensionHostManager::submitExecuteOrQueue(ExtensionHostExecutionGateState* gate_state,
                                                ExtensionHostRunningSession* running_session,
                                                const std::string& params_token, const QCborMap& params,
                                                const std::vector<SharedBufferDescriptor>& buffers,
                                                ExtensionHostExecutionDispatchResult* dispatch_result,
                                                std::string* error) const
{
    if (!gate_state)
    {
        if (error)
        {
            *error = "Extension host execution gate state is null";
        }
        return false;
    }
    if (!running_session)
    {
        if (error)
        {
            *error = "Extension host running session is null";
        }
        return false;
    }
    if (!running_session->runtime_ready)
    {
        if (error)
        {
            *error = "Extension host runtime is still initializing";
        }
        return false;
    }

    pruneExecutionGateSessions(gate_state);

    ExtensionHostExecutionDispatchResult result;
    const bool should_queue =
        !gate_state->queued_requests.empty() || static_cast<int>(gate_state->active_execution_sessions.size()) >=
                                                    (std::max)(1, gate_state->max_concurrent_executions);
    if (should_queue)
    {
        if (!gate_state->queue_when_saturated)
        {
            if (error)
            {
                *error = "Extension host execution limit reached and queueing is disabled";
            }
            result.state = ExtensionHostExecutionDispatchState::Rejected;
            if (dispatch_result)
            {
                *dispatch_result = std::move(result);
            }
            return false;
        }

        ExtensionHostQueuedExecutionRequest queued_request;
        queued_request.queue_id = generateOpaqueToken();
        queued_request.session = running_session;
        queued_request.request_kind = "execute";
        queued_request.params_token = params_token;
        queued_request.params = params;
        queued_request.buffers = buffers;
        gate_state->queued_requests.push_back(queued_request);

        result.state = ExtensionHostExecutionDispatchState::Queued;
        result.queue_id = queued_request.queue_id;
        result.queue_position = static_cast<int>(gate_state->queued_requests.size());
        if (dispatch_result)
        {
            *dispatch_result = std::move(result);
        }
        return true;
    }

    std::string request_id;
    if (!sendExecuteRequest(running_session, params_token, params, buffers, &request_id, error))
    {
        return false;
    }

    gate_state->active_execution_sessions.push_back(running_session);
    result.state = ExtensionHostExecutionDispatchState::Started;
    result.request_id = request_id;
    if (dispatch_result)
    {
        *dispatch_result = std::move(result);
    }
    return true;
}

bool ExtensionHostManager::submitPreviewOrQueue(ExtensionHostExecutionGateState* gate_state,
                                                ExtensionHostRunningSession* running_session,
                                                const std::string& params_token,
                                                const std::vector<SharedBufferDescriptor>& buffers,
                                                ExtensionHostExecutionDispatchResult* dispatch_result,
                                                std::string* error) const
{
    return submitPreviewOrQueue(gate_state, running_session, params_token, QCborMap{}, buffers, dispatch_result, error);
}

bool ExtensionHostManager::submitPreviewOrQueue(ExtensionHostExecutionGateState* gate_state,
                                                ExtensionHostRunningSession* running_session,
                                                const std::string& params_token, const QCborMap& params,
                                                const std::vector<SharedBufferDescriptor>& buffers,
                                                ExtensionHostExecutionDispatchResult* dispatch_result,
                                                std::string* error) const
{
    if (!gate_state)
    {
        if (error)
        {
            *error = "Extension host execution gate state is null";
        }
        return false;
    }
    if (!running_session)
    {
        if (error)
        {
            *error = "Extension host running session is null";
        }
        return false;
    }
    if (!running_session->runtime_ready)
    {
        if (error)
        {
            *error = "Extension host runtime is still initializing";
        }
        return false;
    }

    pruneExecutionGateSessions(gate_state);

    ExtensionHostExecutionDispatchResult result;
    const bool should_queue =
        !gate_state->queued_requests.empty() || static_cast<int>(gate_state->active_execution_sessions.size()) >=
                                                    (std::max)(1, gate_state->max_concurrent_executions);
    if (should_queue)
    {
        if (!gate_state->queue_when_saturated)
        {
            if (error)
            {
                *error = "Extension host execution limit reached and queueing is disabled";
            }
            result.state = ExtensionHostExecutionDispatchState::Rejected;
            if (dispatch_result)
            {
                *dispatch_result = std::move(result);
            }
            return false;
        }

        ExtensionHostQueuedExecutionRequest queued_request;
        queued_request.queue_id = generateOpaqueToken();
        queued_request.session = running_session;
        queued_request.request_kind = "preview";
        queued_request.params_token = params_token;
        queued_request.params = params;
        queued_request.buffers = buffers;
        gate_state->queued_requests.push_back(queued_request);

        result.state = ExtensionHostExecutionDispatchState::Queued;
        result.queue_id = queued_request.queue_id;
        result.queue_position = static_cast<int>(gate_state->queued_requests.size());
        if (dispatch_result)
        {
            *dispatch_result = std::move(result);
        }
        return true;
    }

    std::string request_id;
    if (!sendPreviewRequest(running_session, params_token, params, buffers, &request_id, error))
    {
        return false;
    }

    gate_state->active_execution_sessions.push_back(running_session);
    result.state = ExtensionHostExecutionDispatchState::Started;
    result.request_id = request_id;
    if (dispatch_result)
    {
        *dispatch_result = std::move(result);
    }
    return true;
}

bool ExtensionHostManager::pumpManagedSession(ExtensionHostExecutionGateState* gate_state,
                                              ExtensionHostRunningSession* running_session, const int timeout_ms,
                                              std::string* error) const
{
    if (!gate_state)
    {
        if (error)
        {
            *error = "Extension host execution gate state is null";
        }
        return false;
    }
    if (!pumpSession(running_session, timeout_ms, error))
    {
        pruneExecutionGateSessions(gate_state);
        return false;
    }

    pruneExecutionGateSessions(gate_state);
    while (!gate_state->queued_requests.empty() && static_cast<int>(gate_state->active_execution_sessions.size()) <
                                                       (std::max)(1, gate_state->max_concurrent_executions))
    {
        ExtensionHostQueuedExecutionRequest queued_request = gate_state->queued_requests.front();
        gate_state->queued_requests.erase(gate_state->queued_requests.begin());

        if (!queued_request.session || queued_request.session->state != ExtensionHostSessionState::Active)
        {
            continue;
        }
        if (!queued_request.session->in_flight_request_id.empty())
        {
            gate_state->queued_requests.push_back(std::move(queued_request));
            break;
        }

        std::string request_id;
        bool started = false;
        if (queued_request.request_kind == "execute")
        {
            started = sendExecuteRequest(queued_request.session, queued_request.params_token, queued_request.params,
                                         queued_request.buffers, &request_id, error);
        }
        else if (queued_request.request_kind == "preview")
        {
            started = sendPreviewRequest(queued_request.session, queued_request.params_token, queued_request.params,
                                         queued_request.buffers, &request_id, error);
        }
        else
        {
            if (error)
            {
                *error = "Queued execution request had an unknown kind: " + queued_request.request_kind;
            }
            return false;
        }

        if (!started)
        {
            return false;
        }

        gate_state->active_execution_sessions.push_back(queued_request.session);
    }

    return true;
}

void ExtensionHostManager::stopSession(ExtensionHostRunningSession* running_session) const
{
    if (!running_session)
    {
        return;
    }

    if (an::platform::isLoggingInitialized())
    {
        LOG_INFO("Stopping extension host session id='{}' extension id='{}' display='{}'",
                 running_session->prepared_session.session_id, running_session->prepared_session.extension_id,
                 running_session->prepared_session.display_name);
    }

    if (running_session->socket)
    {
        running_session->socket->disconnectFromServer();
        running_session->socket.reset();
    }

    if (running_session->process)
    {
        if (running_session->process->state() == QProcess::Running)
        {
            terminateProcessTree(*running_session->process
#ifdef _WIN32
                                 ,
                                 &running_session->process_job_handle
#endif
            );
        }
#ifdef _WIN32
        else
        {
            closeJobHandleNoThrow(&running_session->process_job_handle);
        }
#endif
        running_session->handshake.process_exit_code = running_session->process->exitCode();
        running_session->handshake.process_output = processOutput(*running_session->process);
        running_session->process.reset();
    }

    running_session->state = ExtensionHostSessionState::Stopped;
    if (an::platform::isLoggingInitialized())
    {
        LOG_INFO("Stopped extension host session id='{}' extension id='{}' exit_code={}",
                 running_session->prepared_session.session_id, running_session->prepared_session.extension_id,
                 running_session->handshake.process_exit_code);
    }
}

void ExtensionHostLaunchCoordinator::bindSingleImageDescriptor(process_registry::ProcessDescriptor* descriptor,
                                                               const ExtensionHostLaunchBinding& binding)
{
    if (!descriptor)
    {
        return;
    }
    schema_reads_->kept.erase(binding.descriptor_id);

    if (descriptor->availability != process_registry::ProcessAvailability::Available)
    {
        descriptor->launch_single_image_checked = {};
        descriptor->launch_single_image_transient_checked = {};
        return;
    }

    if (binding.host_executable.empty() || !isRegularFile(binding.host_executable))
    {
        descriptor->availability = process_registry::ProcessAvailability::Unavailable;
        descriptor->availability_reason = "The extension host executable is not available in this build.";
        descriptor->launch_single_image_checked = {};
        descriptor->launch_single_image_transient_checked = {};
        return;
    }

    descriptor->launch_single_image_checked = [this, binding](ImageViewControl* control, std::string* error) -> bool {
        ExtensionHostLaunchOptions options;
        if (control && managed_view_name_provider_)
        {
            options.initial_view_name = managed_view_name_provider_(control);
        }
        return launchBoundDescriptor(binding, control, options, error);
    };
    if (binding.process_capabilities.starless_generator)
    {
        descriptor->launch_single_image_transient_checked = [this, binding](ImageViewControl* control,
                                                                            std::string* error) -> bool {
            ExtensionHostLaunchOptions options;
            options.close_after_successful_execute = true;
            options.create_result_view_after_successful_execute = true;
            if (control && managed_view_name_provider_)
            {
                options.initial_view_name = managed_view_name_provider_(control);
            }
            return launchBoundDescriptor(binding, control, options, error);
        };
    }
    else
    {
        descriptor->launch_single_image_transient_checked = {};
    }
}

void ExtensionHostLaunchCoordinator::bindWorkflowDescriptor(process_registry::ProcessDescriptor* descriptor,
                                                            const ExtensionHostLaunchBinding& binding)
{
    if (!descriptor)
    {
        return;
    }
    schema_reads_->kept.erase(binding.descriptor_id);

    if (descriptor->availability != process_registry::ProcessAvailability::Available)
    {
        descriptor->launch_top_level = {};
        return;
    }

    if (binding.host_executable.empty() || !isRegularFile(binding.host_executable))
    {
        descriptor->availability = process_registry::ProcessAvailability::Unavailable;
        descriptor->availability_reason = "The extension host executable is not available in this build.";
        descriptor->launch_top_level = {};
        return;
    }

    descriptor->launch_top_level = [this, binding]() {
        std::string error;
        if (!launchWorkflowDescriptor(binding, &error) && an::platform::isLoggingInitialized())
        {
            LOG_WARNING("Workflow extension launch failed for '{}': {}", binding.extension_id, error);
        }
    };
}

bool ExtensionHostLaunchCoordinator::launchBoundDescriptor(const ExtensionHostLaunchBinding& binding,
                                                           ImageViewControl* control, std::string* error)
{
    return launchBoundDescriptor(binding, control, ExtensionHostLaunchOptions{}, error);
}

bool ExtensionHostLaunchCoordinator::launchBoundDescriptor(const ExtensionHostLaunchBinding& binding,
                                                           ImageViewControl* control,
                                                           const ExtensionHostLaunchOptions& options,
                                                           std::string* error)
{
    auto record_launch_failure = [&binding](const std::string& detail) {
        recordSingleImageLaunchFailure(binding, detail);
    };

    if (!control)
    {
        if (error)
        {
            *error = "No selected image to process";
        }
        record_launch_failure(error ? *error : "No selected image to process");
        return false;
    }

    if (binding.extension_id.empty() || binding.descriptor_id.empty())
    {
        if (error)
        {
            *error = "Extension launch binding is incomplete";
        }
        record_launch_failure(error ? *error : "Extension launch binding is incomplete");
        return false;
    }

    if (const auto* descriptor = process_registry::ProcessRegistry::instance().find(binding.descriptor_id))
    {
        process_registry::LaunchGuardInput input;
        const std::optional<an::ImageDescriptor> image = control->imageDescriptor();
        input.has_active_image = image.has_value();
        if (image.has_value())
        {
            input.active_image_channels = static_cast<int>(image->extent().channelCount());
        }

        const auto guard_result = process_registry::evaluateSingleImageLaunchGuards(*descriptor, input);
        if (!guard_result.ok())
        {
            if (error)
            {
                *error = guard_result.message;
            }
            record_launch_failure(error ? *error : guard_result.message);
            return false;
        }
    }

    if (binding.host_executable.empty() || !isRegularFile(binding.host_executable))
    {
        if (error)
        {
            *error = "Extension host executable is not available: " + binding.host_executable.generic_string();
        }
        record_launch_failure(error ? *error : "Extension host executable is not available");
        return false;
    }

    if (!sessions_.empty())
    {
        // A native host window can close between pump ticks. Prune any
        // session that has already disconnected/exited before we apply the
        // duplicate-session gate for a new launch request.
        pumpSessions();
    }

    if (hasActiveSessionForDescriptor(binding.descriptor_id, control))
    {
        if (error)
        {
            *error = "This extension already has an active host session for the selected image.";
        }
        record_launch_failure(error ? *error
                                    : "This extension already has an active host session for the selected image.");
        return false;
    }

    AutoUiSchemaInspection auto_ui_inspection;
    const bool can_seed_param_schema = !binding.extension_host_owns_process_window &&
                                       hasHostArgument(binding.host_arguments, "--entry-point") &&
                                       hasHostArgument(binding.host_arguments, "--process-class") &&
                                       hasHostArgument(binding.host_arguments, "--environment-root");
    if (can_seed_param_schema && !schema_reads_->findKept(binding, sdk_service_, &auto_ui_inspection))
    {
        beginSchemaRead(binding, control, options, false);
        return true;
    }

    ExtensionHostLaunchRequest request;
    request.extension_id = binding.extension_id;
    request.display_name = binding.display_name;
    request.sdk_backend = binding.sdk_backend;
    request.launch_mode = ExtensionLaunchMode::SingleImage;
    request.host_version = binding.host_version;
    request.capabilities = binding.capabilities;
    request.host_executable = binding.host_executable;
    request.host_arguments = binding.host_arguments;
    request.defer_runtime_bootstrap = connectsBeforeRuntimeStart(binding);
    if (const auto cached_schema = cachedNativeParamSchemaArgument(binding))
    {
        request.host_arguments.push_back("--cached-param-schema");
        request.host_arguments.push_back(*cached_schema);
    }
    request.inspect_settings_snapshot_cbor_base64 =
        inspectSettingsSnapshotCborBase64(binding.extension_id, sdk_service_).toStdString();
    if (binding.cancel_grace_period_ms > 0)
    {
        request.cancel_grace_period_ms = binding.cancel_grace_period_ms;
    }
    if (!hasHostArgument(request.host_arguments, "--stay-alive-ms"))
    {
        request.host_arguments.push_back("--stay-alive-ms");
        request.host_arguments.push_back("86400000");
    }
    if (binding.extension_host_owns_process_window)
    {
        // The rate the settings answered last, on a worker; they are asked again, off this thread, for the launches
        // after this one.
        appendNativePreviewArguments(binding.process_capabilities, previewFrameRate(), request.host_arguments);
        refreshPreviewFrameRate();
    }

    ExtensionSessionController session_controller;
    session_controller.descriptor_id = binding.descriptor_id;
    session_controller.display_name = binding.display_name;
    session_controller.target_control = control;
    session_controller.process_capabilities = binding.process_capabilities;
    session_controller.close_after_successful_execute = options.close_after_successful_execute;
    session_controller.create_result_view_after_successful_execute =
        options.create_result_view_after_successful_execute;
    if (!options.initial_view_name.empty())
    {
        session_controller.target_view_id = options.initial_view_name;
        session_controller.target_view_name = options.initial_view_name;
        session_controller.running_session.initial_view_name = options.initial_view_name;
    }
    if (const auto preview_sub_area_config = parsePreviewSubAreaSchemaConfig(auto_ui_inspection.schema))
    {
        session_controller.managed_preview_sub_area_enabled = true;
        session_controller.managed_preview_sub_area_size = preview_sub_area_config->size;
        session_controller.managed_preview_sub_area_label = preview_sub_area_config->label;
        session_controller.managed_preview_sub_area_changed = [this, control]() { handleViewControlUpdated(control); };
    }

    if (options.initial_view_name.empty())
    {
        session_controller.captureInitialViewStateIfAvailable([this](ExtensionViewState* initial_view_state,
                                                                     bool* has_initial_view_state,
                                                                     std::string* initial_view_error) -> bool {
            return sdk_service_.describeActiveViewState(initial_view_state, has_initial_view_state, initial_view_error);
        });
    }

    const ExtensionHostPreparedSession prepared_session = host_manager_.prepareSession(request);
    if (!host_manager_.launchSession(prepared_session, &session_controller.running_session, error))
    {
        record_launch_failure(error ? *error : "Extension host session launch failed");
        return false;
    }
    session_controller.running_session.initial_view_name = session_controller.target_view_name;

    if (session_controller.running_session.handshake.ack.native_process_window)
    {
        session_controller.running_session.native_process_window_rt_preview = binding.process_capabilities.preview;
        session_controller.managed_preview_sub_area_enabled = false;
        session_controller.managed_preview_sub_area_size = QSize();
        session_controller.managed_preview_sub_area_label.clear();
        session_controller.managed_preview_sub_area_changed = {};
    }

    if (!an::ui::appContext())
    {
        session_controller.stopRunningSession(
            [this](ExtensionHostRunningSession* running_session) { host_manager_.stopSession(running_session); });
        if (error)
        {
            *error = "Application app-context is unavailable for extension process launch";
        }
        record_launch_failure(error ? *error : "Application app-context is unavailable for extension process launch");
        return false;
    }

    if (!session_controller.initializeSessionParams(
            auto_ui_inspection.schema,
            [this, descriptor_id = binding.descriptor_id, control](std::string* execute_error) -> bool {
                return submitExecuteForDescriptor(descriptor_id, control, nullptr, execute_error);
            },
            [this, descriptor_id = binding.descriptor_id, control](std::string* preview_error) -> bool {
                return submitPreviewForDescriptor(descriptor_id, control, preview_error);
            },
            [this, session_id = session_controller.sessionId()] { stopManagedSessionBySessionId(session_id); }, error))
    {
        session_controller.stopRunningSession(
            [this](ExtensionHostRunningSession* running_session) { host_manager_.stopSession(running_session); });
        record_launch_failure(error ? *error : "Extension process-control shell setup failed");
        return false;
    }

    sessions_.push_back(std::make_unique<ExtensionSessionController>(std::move(session_controller)));
    armSessionPumpWake(sessions_.back().get());
    if (sessions_.back()->managed_preview_sub_area_enabled)
    {
        sessions_.back()->configureManagedPreviewSubArea();
    }
    recordReadySessionLaunch(sessions_.back().get());
    return true;
}

void ExtensionHostLaunchCoordinator::beginSchemaRead(const ExtensionHostLaunchBinding& binding,
                                                     ImageViewControl* control,
                                                     const ExtensionHostLaunchOptions& options, const bool workflow)
{
    auto read = std::make_unique<SchemaReads::Read>();
    read->id = schema_reads_->next_id++;
    read->binding = binding;
    read->control = control;
    read->options = options;
    read->workflow = workflow;
    read->settings_fingerprint = extensionSettingsFingerprint(sdk_service_, binding.extension_id);
    read->process = std::make_unique<QProcess>();

    QProcess* process = read->process.get();
    process->setProgram(QString::fromStdString(binding.host_executable.string()));
    process->setProcessChannelMode(QProcess::MergedChannels);
    process->setArguments(autoUiSchemaInspectionArguments(binding, sdk_service_));

    // Queued, so a read lands from the event loop rather than inside its process's report.
    const std::uint64_t read_id = read->id;
    QObject::connect(
        process, &QProcess::finished, &schema_reads_->receiver, [this, read_id]() { landSchemaRead(read_id); },
        Qt::QueuedConnection);
    QObject::connect(
        process, &QProcess::errorOccurred, &schema_reads_->receiver,
        [this, read_id](const QProcess::ProcessError process_error) {
            if (process_error == QProcess::FailedToStart)
            {
                landSchemaRead(read_id);
            }
        },
        Qt::QueuedConnection);
    read->bound_ms = parameter_schema_read_bound_ms_;
    QTimer::singleShot(read->bound_ms, process, [this, read_id, process]() {
        for (const auto& running : schema_reads_->running)
        {
            if (running->id == read_id)
            {
                running->timed_out = true;
            }
        }
        process->kill();
    });

    if (an::platform::isLoggingInitialized())
    {
        LOG_INFO("Reading the parameter schema of extension id='{}' descriptor id='{}' before its launch",
                 binding.extension_id, binding.descriptor_id);
    }
    schema_reads_->running.push_back(std::move(read));
    process->start();
}

void ExtensionHostLaunchCoordinator::landSchemaRead(const std::uint64_t read_id)
{
    auto& running = schema_reads_->running;
    const auto found =
        std::find_if(running.begin(), running.end(), [read_id](const auto& read) { return read->id == read_id; });
    if (found == running.end() || (*found)->process->state() != QProcess::NotRunning)
    {
        return;
    }
    const std::unique_ptr<SchemaReads::Read> read = std::move(*found);
    running.erase(found);

    std::string error;
    AutoUiSchemaInspection inspection;
    if (!readAutoUiSchemaInspection(*read->process, read->timed_out, read->bound_ms, &inspection, &error))
    {
        if (read->workflow)
        {
            recordWorkflowLaunchFailure(read->binding, error);
            return;
        }
        recordSingleImageLaunchFailure(read->binding, error);
        if (auto* dialog_service = currentDialogService())
        {
            dialog_service->notifyErrorMessageAsync(nullptr, QString::fromStdString(read->binding.display_name),
                                                    QString::fromStdString(error));
        }
        return;
    }
    schema_reads_->kept[read->binding.descriptor_id] =
        SchemaReads::Kept{autoUiSchemaReadKey(read->binding), read->settings_fingerprint, std::move(inspection)};

    // The launch is asked again, so what it checks - the view, the guards, a session started meanwhile - is checked as
    // it stands now, and it finds the schema it kept unless the settings changed while it was read.
    if (read->workflow)
    {
        static_cast<void>(launchWorkflowDescriptor(read->binding, &error));
        return;
    }
    if (!launchBoundDescriptor(read->binding, read->control, read->options, &error))
    {
        if (auto* dialog_service = currentDialogService())
        {
            dialog_service->notifyErrorMessageAsync(nullptr, QString::fromStdString(read->binding.display_name),
                                                    QString::fromStdString(error));
        }
    }
}

void ExtensionHostLaunchCoordinator::endSchemaReads(const ImageViewControl* control)
{
    auto& running = schema_reads_->running;
    for (auto& read : running)
    {
        if (!control || read->control == control)
        {
            SchemaReads::end(*read);
            read.reset();
        }
    }
    running.erase(std::remove(running.begin(), running.end(), nullptr), running.end());
}

bool ExtensionHostLaunchCoordinator::isReadingParameterSchemaFor(const std::string_view descriptor_id) const
{
    return std::any_of(schema_reads_->running.begin(), schema_reads_->running.end(),
                       [descriptor_id](const auto& read) { return read->binding.descriptor_id == descriptor_id; });
}

bool ExtensionHostLaunchCoordinator::launchWorkflowDescriptor(const ExtensionHostLaunchBinding& binding,
                                                              std::string* error)
{
    auto record_launch_failure = [&binding](const std::string& detail) {
        recordWorkflowLaunchFailure(binding, detail);
    };

    if (binding.extension_id.empty() || binding.descriptor_id.empty())
    {
        if (error)
        {
            *error = "Workflow extension launch binding is incomplete";
        }
        record_launch_failure(error ? *error : "Workflow extension launch binding is incomplete");
        return false;
    }

    if (binding.host_executable.empty() || !isRegularFile(binding.host_executable))
    {
        if (error)
        {
            *error = "Extension host executable is not available: " + binding.host_executable.generic_string();
        }
        record_launch_failure(error ? *error : "Extension host executable is not available");
        return false;
    }

    if (!sessions_.empty())
    {
        pumpSessions();
    }

    if (hasActiveSessionForDescriptor(binding.descriptor_id, nullptr))
    {
        if (error)
        {
            *error = "This workflow extension already has an active host session.";
        }
        record_launch_failure(error ? *error : "This workflow extension already has an active host session.");
        return false;
    }

    AutoUiSchemaInspection auto_ui_inspection;
    const bool can_seed_param_schema = !binding.extension_host_owns_process_window &&
                                       hasHostArgument(binding.host_arguments, "--entry-point") &&
                                       hasHostArgument(binding.host_arguments, "--process-class") &&
                                       hasHostArgument(binding.host_arguments, "--environment-root");
    if (can_seed_param_schema && !schema_reads_->findKept(binding, sdk_service_, &auto_ui_inspection))
    {
        beginSchemaRead(binding, nullptr, ExtensionHostLaunchOptions{}, true);
        return true;
    }

    ExtensionHostLaunchRequest request;
    request.extension_id = binding.extension_id;
    request.display_name = binding.display_name;
    request.sdk_backend = binding.sdk_backend;
    request.launch_mode = ExtensionLaunchMode::Workflow;
    request.host_version = binding.host_version;
    request.capabilities = binding.capabilities;
    if (std::find(request.capabilities.begin(), request.capabilities.end(), "workflow") == request.capabilities.end())
    {
        request.capabilities.push_back("workflow");
    }
    request.host_executable = binding.host_executable;
    request.host_arguments = binding.host_arguments;
    request.defer_runtime_bootstrap = connectsBeforeRuntimeStart(binding);
    if (const auto cached_schema = cachedNativeParamSchemaArgument(binding))
    {
        request.host_arguments.push_back("--cached-param-schema");
        request.host_arguments.push_back(*cached_schema);
    }
    request.inspect_settings_snapshot_cbor_base64 =
        inspectSettingsSnapshotCborBase64(binding.extension_id, sdk_service_).toStdString();
    if (binding.cancel_grace_period_ms > 0)
    {
        request.cancel_grace_period_ms = binding.cancel_grace_period_ms;
    }
    if (!hasHostArgument(request.host_arguments, "--stay-alive-ms"))
    {
        request.host_arguments.push_back("--stay-alive-ms");
        request.host_arguments.push_back("86400000");
    }

    ExtensionSessionController session_controller;
    session_controller.descriptor_id = binding.descriptor_id;
    session_controller.display_name = binding.display_name;
    session_controller.target_control = nullptr;
    session_controller.process_capabilities = binding.process_capabilities;

    const ExtensionHostPreparedSession prepared_session = host_manager_.prepareSession(request);
    if (!host_manager_.launchSession(prepared_session, &session_controller.running_session, error))
    {
        record_launch_failure(error ? *error : "Workflow extension host session launch failed");
        return false;
    }

    const std::string launched_session_id = session_controller.sessionId();
    if (!session_controller.initializeSessionParams(
            auto_ui_inspection.schema,
            [this, launched_session_id](std::string* execute_error) -> bool {
                auto* session = findSessionControllerBySessionId(sessions_, launched_session_id);
                if (!session)
                {
                    if (execute_error)
                    {
                        *execute_error = "No active workflow extension session exists";
                    }
                    return false;
                }

                std::string request_id;
                return host_manager_.sendExecuteRequest(&session->running_session, "workflow",
                                                        session->seeded_execution_params, {}, &request_id,
                                                        execute_error);
            },
            {}, [this, launched_session_id] { stopManagedSessionBySessionId(launched_session_id); }, error))
    {
        session_controller.stopRunningSession(
            [this](ExtensionHostRunningSession* running_session) { host_manager_.stopSession(running_session); });
        record_launch_failure(error ? *error : "Workflow process-control setup failed");
        return false;
    }

    sessions_.push_back(std::make_unique<ExtensionSessionController>(std::move(session_controller)));
    armSessionPumpWake(sessions_.back().get());
    recordReadySessionLaunch(sessions_.back().get());
    return true;
}

bool ExtensionHostLaunchCoordinator::submitExecuteForDescriptor(const std::string_view descriptor_id,
                                                                ImageViewControl* control,
                                                                ExtensionHostExecutionDispatchResult* dispatch_result,
                                                                std::string* error)
{
    if (!control)
    {
        if (error)
        {
            *error = "No selected image to process";
        }
        return false;
    }

    auto* session_controller = findSessionController(sessions_, descriptor_id, control);
    if (!session_controller)
    {
        if (error)
        {
            *error = "No active extension session exists for the selected descriptor and image";
        }
        return false;
    }

    ExtensionHostExecutionDispatchResult local_dispatch_result;
    if (!startExecuteForSession(session_controller, &local_dispatch_result, error))
    {
        return false;
    }

    if (dispatch_result)
    {
        *dispatch_result = std::move(local_dispatch_result);
    }
    return true;
}

bool ExtensionHostLaunchCoordinator::startExecuteForSession(ExtensionSessionController* session,
                                                            ExtensionHostExecutionDispatchResult* dispatch_result,
                                                            std::string* error)
{
    if (!session)
    {
        if (error)
        {
            *error = "Extension session controller is null";
        }
        return false;
    }
    if (!session->running_session.runtime_ready)
    {
        if (error)
        {
            *error = "Extension host runtime is still initializing";
        }
        return false;
    }

    QCborMap execution_params;
    std::vector<SharedBufferDescriptor> request_buffers;
    std::vector<SharedImageBuffer> owned_buffers;
    switch (session->prepareExecuteRequest(managed_auxiliary_buffer_provider_, managed_preview_sub_area_provider_,
                                           &execution_params, &request_buffers, &owned_buffers, error))
    {
    case ExtensionSessionController::PrepareOutcome::Ready:
        break;
    case ExtensionSessionController::PrepareOutcome::Busy:
        // The image is being written: the execute waits, and the first pump after the writer lets go sends it.
        session->markExecuteWaitingForImage();
        if (dispatch_result)
        {
            *dispatch_result = ExtensionHostExecutionDispatchResult{};
            dispatch_result->state = ExtensionHostExecutionDispatchState::Waiting;
        }
        if (error)
        {
            error->clear();
        }
        return true;
    case ExtensionSessionController::PrepareOutcome::Failed:
        return false;
    }

    ExtensionHostExecutionDispatchResult local_dispatch_result;
    if (!host_manager_.submitExecuteOrQueue(&execution_gate_, &session->running_session,
                                            defaultExecuteParamsToken(session->descriptor_id), execution_params,
                                            request_buffers, &local_dispatch_result, error))
    {
        return false;
    }

    session->applyExecuteDispatchResult(local_dispatch_result, std::move(owned_buffers));
    // The execute is sent or queued now, whether it was asked for just now or waited for its image.
    session->notifyExecuteStarted();
    if (dispatch_result)
    {
        *dispatch_result = std::move(local_dispatch_result);
    }
    return true;
}

bool ExtensionHostLaunchCoordinator::submitPreviewForDescriptor(const std::string_view descriptor_id,
                                                                ImageViewControl* control, std::string* error)
{
    if (!control)
    {
        if (error)
        {
            *error = "No selected image to preview";
        }
        return false;
    }

    ExtensionSessionController* const session_controller = findSessionController(sessions_, descriptor_id, control);
    if (!session_controller)
    {
        if (error)
        {
            *error = "No active extension session exists for the selected descriptor and image";
        }
        return false;
    }

    ExtensionSessionController::PreviewSubmissionAction action;
    if (!session_controller->preparePreviewSubmission(
            [this](ExtensionHostRunningSession* running_session) {
                removeQueuedPreviewRequestsForSession(&execution_gate_, running_session);
            },
            [this](ExtensionHostRunningSession* running_session, std::string* cancel_error) -> bool {
                return host_manager_.sendCancelRequest(running_session, nullptr, cancel_error);
            },
            &action, error))
    {
        return false;
    }
    if (action == ExtensionSessionController::PreviewSubmissionAction::AwaitInFlightRequest)
    {
        return true;
    }

    return startPreviewForSession(session_controller, error);
}

bool ExtensionHostLaunchCoordinator::startPreviewForSession(ExtensionSessionController* session, std::string* error)
{
    if (!session)
    {
        if (error)
        {
            *error = "Extension session controller is null";
        }
        return false;
    }
    if (!session->running_session.runtime_ready)
    {
        if (error)
        {
            *error = "Extension host runtime is still initializing";
        }
        return false;
    }

    QCborMap execution_params;
    std::vector<SharedBufferDescriptor> request_buffers;
    std::vector<SharedImageBuffer> owned_buffers;
    switch (session->preparePreviewStart(
        [this](ExtensionHostRunningSession* running_session) {
            removeQueuedPreviewRequestsForSession(&execution_gate_, running_session);
        },
        managed_auxiliary_buffer_provider_, managed_preview_sub_area_provider_, &execution_params, &request_buffers,
        &owned_buffers, error))
    {
    case ExtensionSessionController::PrepareOutcome::Ready:
        break;
    case ExtensionSessionController::PrepareOutcome::Busy:
        // The image is being written: the refresh stays pending, and the next pump asks again.
        session->markPreviewRefreshPending();
        return true;
    case ExtensionSessionController::PrepareOutcome::Failed:
        return false;
    }

    ExtensionHostExecutionDispatchResult dispatch_result;
    if (!host_manager_.submitPreviewOrQueue(&execution_gate_, &session->running_session,
                                            defaultPreviewParamsToken(session->descriptor_id), execution_params,
                                            request_buffers, &dispatch_result, error))
    {
        return false;
    }

    session->applyPreviewDispatchResult(dispatch_result, std::move(owned_buffers));

    return true;
}

void ExtensionHostLaunchCoordinator::stopManagedSession(ExtensionSessionController* session,
                                                        const bool record_forced_stop)
{
    if (!session)
    {
        return;
    }

    session->detachUiHandles();
    session->cancelMaterialization();

    if (!session->launch_outcome_recorded)
    {
        recordReadySessionLaunch(session);
        if (!session->launch_outcome_recorded)
        {
            // Closing the target image during bootstrap is a normal
            // cancellation, not an extension launch failure.
            session->launch_outcome_recorded = true;
        }
    }

    if (record_forced_stop && session->isActive())
    {
        const ExtensionRuntimeTerminationDiagnostics diagnostics =
            buildTerminationDiagnostics(session->running_session);
        ExtensionRuntimeStatusRegistry::instance().recordTermination(
            session->running_session.prepared_session.extension_id, ExtensionRuntimeTerminationKind::ForcedStop,
            "Extension host session was stopped by the host.", &diagnostics);
    }

    removeSessionFromExecutionGate(&execution_gate_, &session->running_session);
    host_manager_.stopSession(&session->running_session);
    clearSessionControllerBuffers(session);
}

void ExtensionHostLaunchCoordinator::stopManagedSessionBySessionId(const std::string_view session_id)
{
    stopManagedSession(findSessionControllerBySessionId(sessions_, session_id), true);
}

void ExtensionHostLaunchCoordinator::recordReadySessionLaunch(ExtensionSessionController* session)
{
    if (!session)
    {
        return;
    }
    if (session->launch_outcome_recorded || !session->isActive() || !session->running_session.runtime_ready)
    {
        return;
    }

    const bool workflow = session->running_session.prepared_session.launch_mode == ExtensionLaunchMode::Workflow;
    session->launch_outcome_recorded = true;
    ExtensionRuntimeStatusRegistry::instance().recordLaunchSucceeded(
        session->running_session.prepared_session.extension_id,
        workflow ? "Workflow host session initialized successfully." : "Host session initialized successfully.");

    if (session->target_control)
    {
        if (auto* event_bus = currentAppEventBus())
        {
            event_bus->notifyProcessOpened(session->display_name, session->target_control);
        }
    }
}

void ExtensionHostLaunchCoordinator::pumpSessions()
{
    // Pump active sessions (heartbeat, message dispatch, queue drain).
    for (auto& session : sessions_)
    {
        if (!session)
        {
            continue;
        }

        session->advanceActivePump([this](ExtensionHostRunningSession* running_session, std::string* error) {
            return host_manager_.pumpManagedSession(&execution_gate_, running_session, 0, error);
        });

        if (!session->launch_outcome_recorded && !session->isActive())
        {
            const auto state = session->running_session.state;
            const bool explicitly_cancelled =
                session->running_session.native_window_close_requested || state == ExtensionHostSessionState::Stopped;
            const bool bootstrap_failed = state == ExtensionHostSessionState::Crashed ||
                                          state == ExtensionHostSessionState::Unresponsive ||
                                          state == ExtensionHostSessionState::Failed ||
                                          (!session->running_session.runtime_ready && !explicitly_cancelled);
            if (bootstrap_failed)
            {
                ExtensionRuntimeStatusRegistry::instance().recordLaunchFailed(
                    session->running_session.prepared_session.extension_id,
                    session->running_session.last_error.empty() ? "Extension runtime failed during bootstrap."
                                                                : session->running_session.last_error);
            }
            session->launch_outcome_recorded = true;
        }
        else
        {
            recordReadySessionLaunch(session.get());
        }

        // A completed result is imported on a worker, one import at a time, and presented once it lands.
        const auto wake_pump = [this]() {
            if (session_pump_wake_callback_)
            {
                session_pump_wake_callback_();
            }
        };
        if (session->hasPendingPreviewMaterialization() && !session->materializationInFlight())
        {
            const bool should_apply_preview = session->shouldApplyPreviewResult();
            session->markPreviewResultConsumed();
            if (should_apply_preview)
            {
                if (const an::consumer::EngineSession* const engine_session = currentEngineSession(); engine_session)
                {
                    session->beginPreviewMaterialization(*engine_session, wake_pump);
                }
                else
                {
                    session->reportPreviewMaterializationFailure("Application context lends no engine session");
                    stopManagedSession(session.get(), true);
                }
            }
            else
            {
                session->discardMaterializedBuffers();
            }
        }

        if (session->hasPendingExecuteMaterialization() && !session->materializationInFlight())
        {
            if (const an::consumer::EngineSession* const engine_session = currentEngineSession(); engine_session)
            {
                session->beginExecuteMaterialization(*engine_session, wake_pump);
            }
            else
            {
                session->reportExecuteMaterializationFailure("Application context lends no engine session");
                stopManagedSession(session.get(), true);
            }
        }

        if (session->hasLandedMaterialization())
        {
            std::optional<ExtensionSessionController::LandedMaterialization> landed =
                session->takeLandedMaterialization();
            if (landed.has_value() && landed->kind == ExtensionSessionController::MaterializationKind::Preview)
            {
                if (landed->image.hasValue())
                {
                    session->applyPreviewPresentation(std::move(*landed->image), managed_preview_presenter_);
                }
                else
                {
                    session->reportPreviewMaterializationFailure(landed->image.status().message());
                    stopManagedSession(session.get(), true);
                }
            }
            else if (landed.has_value())
            {
                if (landed->image.hasValue())
                {
                    session->applyExecutePresentation(std::move(*landed->image), std::move(landed->masks),
                                                      managed_result_presenter_, managed_output_mask_presenter_,
                                                      managed_generated_result_presenter_);

                    if (session->shouldStopAfterSuccessfulExecutePresentation(isHeadlessQtPlatform()))
                    {
                        stopManagedSession(session.get(), false);
                    }
                }
                else
                {
                    session->reportExecuteMaterializationFailure(landed->image.status().message());
                    stopManagedSession(session.get(), true);
                }
            }
        }

        // An execute that waited for its image is sent before a pending preview restart can take the request slot.
        if (session->shouldRetryPendingExecute())
        {
            std::string execute_error;
            if (!startExecuteForSession(session.get(), nullptr, &execute_error))
            {
                session->reportPendingExecuteFailure(execute_error);
            }
        }

        if (session->advanceAfterMaterialization([this](ExtensionSessionController* controller, std::string* error) {
                return startPreviewForSession(controller, error);
            }) == ExtensionSessionController::PumpAction::StopSession)
        {
            stopManagedSession(session.get(), true);
        }
    }

    // Prune fully stopped sessions from the managed list, each out of the execution gate before it is destroyed.
    sessions_.erase(std::remove_if(sessions_.begin(), sessions_.end(),
                                   [this](const std::unique_ptr<ExtensionSessionController>& session) {
                                       if (!session)
                                       {
                                           return true;
                                       }
                                       if (!session->shouldPrune())
                                       {
                                           return false;
                                       }
                                       removeSessionFromExecutionGate(&execution_gate_, &session->running_session);
                                       return true;
                                   }),
                    sessions_.end());
}

void ExtensionHostLaunchCoordinator::stopAllSessions()
{
    endSchemaReads(nullptr);
    for (auto& session : sessions_)
    {
        if (session)
        {
            stopManagedSession(session.get(), true);
        }
    }
    sessions_.clear();
}

bool ExtensionHostLaunchCoordinator::hasActiveSessionForDescriptor(const std::string_view descriptor_id,
                                                                   const ImageViewControl* control) const
{
    return std::any_of(sessions_.begin(), sessions_.end(),
                       [descriptor_id, control](const std::unique_ptr<ExtensionSessionController>& session) {
                           return session && session->matchesDescriptor(descriptor_id, control);
                       }) ||
           std::any_of(schema_reads_->running.begin(), schema_reads_->running.end(),
                       [descriptor_id, control](const auto& read) {
                           return read->binding.descriptor_id == descriptor_id && read->control == control;
                       });
}

bool ExtensionHostLaunchCoordinator::hasReadySessionForDescriptor(const std::string_view descriptor_id,
                                                                  const ImageViewControl* control) const
{
    return std::any_of(sessions_.begin(), sessions_.end(),
                       [descriptor_id, control](const std::unique_ptr<ExtensionSessionController>& session) {
                           return session && session->matchesDescriptor(descriptor_id, control) &&
                                  session->running_session.runtime_ready;
                       });
}

bool ExtensionHostLaunchCoordinator::hasReadySessionForDescriptor(const std::string_view descriptor_id) const
{
    return std::any_of(sessions_.begin(), sessions_.end(),
                       [descriptor_id](const std::unique_ptr<ExtensionSessionController>& session) {
                           return session && session->descriptor_id == descriptor_id && session->isActive() &&
                                  session->running_session.runtime_ready;
                       });
}

std::size_t ExtensionHostLaunchCoordinator::activeSessionCount() const noexcept
{
    return static_cast<std::size_t>(std::count_if(sessions_.begin(), sessions_.end(),
                                                  [](const std::unique_ptr<ExtensionSessionController>& session) {
                                                      return session && session->isActive();
                                                  })) +
           schema_reads_->running.size();
}

void ExtensionHostLaunchCoordinator::handleThemeChanged()
{
    for (auto& session : sessions_)
    {
        if (!sessionCanReceiveRuntimeNotifications(session.get()))
        {
            continue;
        }

        std::string error;
        if (!sendThemeChangedNotification(&sdk_service_, session.get(), &error))
        {
            markRuntimeNotificationFailure(session.get(), "theme.changed", error);
        }
    }
}

void ExtensionHostLaunchCoordinator::handleViewControlCreated(const ImageViewControl* control,
                                                              const std::string_view view_id,
                                                              const std::string_view view_name,
                                                              const std::string_view file_name,
                                                              const std::string_view selector_tooltip)
{
    if (!control && view_id.empty() && view_name.empty() && file_name.empty())
    {
        return;
    }

    for (auto& session : sessions_)
    {
        if (!sessionCanReceiveRuntimeNotifications(session.get()))
        {
            continue;
        }

        ViewCreatedMessage message;
        message.session_id = session->running_session.prepared_session.session_id;
        message.request_id = generateOpaqueToken();
        message.view_id = std::string(view_id);
        message.view_name = view_name.empty() ? std::string(view_id) : std::string(view_name);
        message.file_name = std::string(file_name);
        message.selector_tooltip = std::string(selector_tooltip);
        if (control)
        {
            if (const std::optional<an::ImageDescriptor> image = control->imageDescriptor(); image.has_value())
            {
                message.width = static_cast<int>(image->extent().width());
                message.height = static_cast<int>(image->extent().height());
                message.channels = static_cast<int>(image->extent().channelCount());
            }
        }

        std::string error;
        if (!sendRuntimeNotificationMessage(&sdk_service_, &session->running_session, toProtocolMessage(message),
                                            &error))
        {
            markRuntimeNotificationFailure(session.get(), "view.created", error);
        }
    }
}

void ExtensionHostLaunchCoordinator::handleViewControlDestroyed(const ImageViewControl* control,
                                                                const std::string_view view_id)
{
    if (!control)
    {
        return;
    }

    const std::string fallback_view_id(view_id);

    // A launch still reading its schema for the view has nothing to start once the view is gone.
    endSchemaReads(control);

    for (auto& session : sessions_)
    {
        const bool destroys_bound_target = session && session->isActive() && session->isBoundTo(control);
        if (destroys_bound_target && !session->running_session.runtime_ready)
        {
            stopManagedSession(session.get(), false);
            session->target_control = nullptr;
            continue;
        }
        if (!sessionCanReceiveRuntimeNotifications(session.get()))
        {
            if (destroys_bound_target)
            {
                session->clearManagedPreviewSubArea();
                session->target_control = nullptr;
            }
            continue;
        }

        ViewDestroyedMessage message;
        message.session_id = session->running_session.prepared_session.session_id;
        message.request_id = generateOpaqueToken();
        message.view_id = session->resolveNotificationViewId(control, fallback_view_id);

        if (message.view_id.empty())
        {
            if (destroys_bound_target)
            {
                session->clearManagedPreviewSubArea();
                session->target_control = nullptr;
            }
            continue;
        }

        std::string error;
        if (!sendRuntimeNotificationMessage(&sdk_service_, &session->running_session, toProtocolMessage(message),
                                            &error))
        {
            markRuntimeNotificationFailure(session.get(), "view.destroyed", error);
        }
        if (destroys_bound_target)
        {
            session->clearManagedPreviewSubArea();
            session->target_control = nullptr;
        }
    }
}

void ExtensionHostLaunchCoordinator::handleViewControlUpdated(const ImageViewControl* control)
{
    if (!control)
    {
        return;
    }

    for (auto& session : sessions_)
    {
        if (!sessionCanReceiveRuntimeNotifications(session.get()) || !session->isBoundTo(control))
        {
            continue;
        }

        std::string error;
        if (!sendViewUpdatedNotification(&sdk_service_, session.get(), &error))
        {
            markRuntimeNotificationFailure(session.get(), "view.updated", error);
        }
    }
}

void ExtensionHostLaunchCoordinator::handleViewControlRenamed(const std::string& new_name, const std::string& old_name,
                                                              const ImageViewControl* control,
                                                              const std::string_view file_name,
                                                              const std::string_view selector_tooltip)
{
    if (!control && new_name.empty() && old_name.empty())
    {
        return;
    }

    const std::string fallback_file_name(file_name);

    for (auto& session : sessions_)
    {
        if (!sessionCanReceiveRuntimeNotifications(session.get()))
        {
            if (session && session->isActive() && control && session->isBoundTo(control))
            {
                session->updateBoundViewIdentity(new_name, fallback_file_name);
            }
            continue;
        }

        const std::string_view fallback_view_id =
            old_name.empty() ? std::string_view(new_name) : std::string_view(old_name);
        ViewRenamedMessage message;
        message.session_id = session->running_session.prepared_session.session_id;
        message.request_id = generateOpaqueToken();
        message.view_name = new_name;
        message.view_id = session->resolveNotificationViewId(control, fallback_view_id);
        message.file_name = session->resolveNotificationFileName(control, fallback_file_name);
        message.selector_tooltip = std::string(selector_tooltip);

        if (message.view_id.empty())
        {
            continue;
        }

        std::string error;
        if (!sendRuntimeNotificationMessage(&sdk_service_, &session->running_session, toProtocolMessage(message),
                                            &error))
        {
            markRuntimeNotificationFailure(session.get(), "view.renamed", error);
            continue;
        }

        if (session->isBoundTo(control))
        {
            session->updateBoundViewIdentity(new_name, message.file_name);
        }
    }
}

} // namespace an::ui::extensions
