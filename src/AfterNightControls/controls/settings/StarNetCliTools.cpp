// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "StarNetCliTools.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>

namespace
{

inline constexpr auto kMachineInfoSchema = "starnetastro.cli.machine-info.v1";
inline constexpr auto kMachineProgressSchema = "starnetastro.cli.progress.v1";

QString readCommandOutput(const QString& executablePath, const QStringList& arguments, const int timeoutMs)
{
    if (timeoutMs <= 0)
    {
        return {};
    }

    QProcess process;
    process.setProgram(executablePath);
    process.setArguments(arguments);
    process.setProcessChannelMode(QProcess::MergedChannels);

    const QFileInfo executableInfo(executablePath);
    if (executableInfo.exists())
    {
        process.setWorkingDirectory(executableInfo.absolutePath());
    }

    process.start();
    if (!process.waitForFinished(timeoutMs))
    {
        process.kill();
        process.waitForFinished(1000);
        return {};
    }

    return QString::fromUtf8(process.readAll());
}

std::optional<QString> recognizedMachineInfoProduct(const QString& output)
{
    QJsonParseError parse_error;
    const QJsonDocument document = QJsonDocument::fromJson(output.trimmed().toUtf8(), &parse_error);
    if (parse_error.error != QJsonParseError::NoError || !document.isObject())
    {
        return std::nullopt;
    }

    const QJsonObject root = document.object();
    if (root.value(QStringLiteral("schema")).toString() != QString::fromLatin1(kMachineInfoSchema))
    {
        return std::nullopt;
    }

    return root.value(QStringLiteral("product")).toString();
}

} // namespace

namespace an::ui::starnet
{

QVersionNumber minimumSupportedVersion()
{
    return QVersionNumber(2, 5, 3);
}

QString minimumSupportedVersionText()
{
    return minimumSupportedVersion().toString();
}

QStringList executableNames()
{
#if defined(Q_OS_WIN)
    return {QStringLiteral("starnet2.exe")};
#else
    return {QStringLiteral("starnet2")};
#endif
}

QStringList commonInstallDirectories()
{
#if defined(Q_OS_WIN)
    return {
        QStringLiteral("C:/Program Files/StarNet2/bin"),
        QStringLiteral("C:/Program Files/StarNet2"),
        QDir::homePath() + QStringLiteral("/StarNet2"),
        QDir::homePath() + QStringLiteral("/AppData/Local/StarNet2/bin"),
        QDir::homePath() + QStringLiteral("/AppData/Local/StarNet2"),
    };
#elif defined(Q_OS_MACOS)
    return {
        QStringLiteral("/usr/local/bin"),
        QStringLiteral("/opt/homebrew/bin"),
        QDir::homePath() + QStringLiteral("/StarNet2"),
        QDir::homePath() + QStringLiteral("/.local/bin"),
    };
#else
    return {
        QStringLiteral("/usr/bin"),
        QStringLiteral("/usr/local/bin"),
        QStringLiteral("/opt/StarNet2/bin"),
        QStringLiteral("/opt/starnet2/bin"),
        QDir::homePath() + QStringLiteral("/StarNet2"),
        QDir::homePath() + QStringLiteral("/.local/bin"),
    };
#endif
}

std::optional<QVersionNumber> parseVersionText(const QString& output)
{
    static const QRegularExpression versionRegex(
        QStringLiteral(R"((?:StarNet2|starnet2|version|v)?[\s:]*v?(\d+)\.(\d+)(?:\.(\d+))?)"),
        QRegularExpression::CaseInsensitiveOption);

    const auto match = versionRegex.match(output);
    if (!match.hasMatch())
    {
        return std::nullopt;
    }

    const int major = match.captured(1).toInt();
    const int minor = match.captured(2).toInt();
    const int patch = match.captured(3).isEmpty() ? 0 : match.captured(3).toInt();
    return QVersionNumber(major, minor, patch);
}

std::optional<MachineInfo> parseMachineInfoText(const QString& output)
{
    QJsonParseError parse_error;
    const QJsonDocument document = QJsonDocument::fromJson(output.trimmed().toUtf8(), &parse_error);
    if (parse_error.error != QJsonParseError::NoError || !document.isObject())
    {
        return std::nullopt;
    }

    const QJsonObject root = document.object();
    if (root.value(QStringLiteral("schema")).toString() != QString::fromLatin1(kMachineInfoSchema) ||
        root.value(QStringLiteral("product")).toString() != QStringLiteral("starnet2"))
    {
        return std::nullopt;
    }

    const QVersionNumber version = QVersionNumber::fromString(root.value(QStringLiteral("version")).toString());
    if (version.segmentCount() < 2)
    {
        return std::nullopt;
    }

    const QJsonObject progress =
        root.value(QStringLiteral("io")).toObject().value(QStringLiteral("progress")).toObject();
    const QJsonObject machine = progress.value(QStringLiteral("machine")).toObject();

    MachineInfo info;
    info.version = version;
    info.machineProgressSupported =
        machine.value(QStringLiteral("supported")).toBool() && machine.value(QStringLiteral("stable")).toBool() &&
        machine.value(QStringLiteral("stream")).toString() == QStringLiteral("stderr") &&
        machine.value(QStringLiteral("format")).toString() == QStringLiteral("json_lines") &&
        machine.value(QStringLiteral("schema")).toString() == QString::fromLatin1(kMachineProgressSchema) &&
        machine.value(QStringLiteral("option")).toString() == QStringLiteral("machine_progress");
    return info;
}

bool isSupportedVersion(const QVersionNumber& version)
{
    return QVersionNumber::compare(version, minimumSupportedVersion()) >= 0;
}

int normalizeStride(int stride)
{
    if (stride < 2)
    {
        stride = 2;
    }
    else if (stride > 512)
    {
        stride = 512;
    }

    if ((stride % 2) != 0)
    {
        --stride;
    }
    return stride;
}

QString weightsBrowseDirectory(const QString& weightsPath, const QString& executablePath)
{
    QString installDir;
    const QString trimmedExecutable = executablePath.trimmed();
    if (!trimmedExecutable.isEmpty())
    {
        const QFileInfo executableInfo(trimmedExecutable);
        const QString candidateDir = executableInfo.absolutePath();
        if (QDir(candidateDir).exists())
        {
            installDir = candidateDir;
        }
    }

    const QString trimmedWeights = weightsPath.trimmed();
    if (trimmedWeights.isEmpty())
    {
        return installDir;
    }

    QFileInfo weightsInfo(trimmedWeights);
    if (weightsInfo.isRelative())
    {
        if (installDir.isEmpty())
        {
            return {};
        }
        weightsInfo.setFile(QDir(installDir).filePath(trimmedWeights));
    }

    const QDir weightsDir = weightsInfo.isDir() ? QDir(weightsInfo.absoluteFilePath()) : weightsInfo.absoluteDir();
    if (weightsDir.exists())
    {
        return weightsDir.absolutePath();
    }

    return installDir;
}

QStringList buildArguments(const CliOptions& options)
{
    QStringList args;
    args << QStringLiteral("--input") << options.inputPath;
    args << QStringLiteral("--output") << options.outputPath;

    if (!options.maskPath.trimmed().isEmpty())
    {
        args << QStringLiteral("--mask") << options.maskPath;
    }

    if (!options.unscreenPath.trimmed().isEmpty())
    {
        args << QStringLiteral("--unscreen") << options.unscreenPath;
    }

    if (!options.weightsPath.trimmed().isEmpty())
    {
        args << QStringLiteral("--weights") << options.weightsPath.trimmed();
    }

    args << QStringLiteral("--stride") << QString::number(normalizeStride(options.stride));

    if (options.upsample)
    {
        args << QStringLiteral("--upsample");
    }

    if (!options.highlightProtection)
    {
        args << QStringLiteral("--disable-highlights-protection");
    }

    if (options.machineProgress)
    {
        args << QStringLiteral("--machine-progress");
    }

    return args;
}

QString readVersionOutput(const QString& executablePath, int timeoutMs)
{
    return readCommandOutput(executablePath, {QStringLiteral("--version")}, timeoutMs);
}

ExecutableCheckResult checkExecutable(const QString& executablePath, int timeoutMs)
{
    ExecutableCheckResult result;
    const QString normalizedPath = executablePath.trimmed();
    if (normalizedPath.isEmpty())
    {
        result.detail = QStringLiteral("StarNet2 executable path is empty.");
        return result;
    }

    const QFileInfo info(normalizedPath);
    result.exists = info.exists();
    if (!result.exists)
    {
        result.detail = QStringLiteral("StarNet2 executable was not found.");
        return result;
    }

    result.executable = info.isExecutable();
    if (!result.executable)
    {
        result.detail = QStringLiteral("StarNet2 path is not executable.");
        return result;
    }

    QElapsedTimer probe_timer;
    probe_timer.start();

    std::optional<QVersionNumber> version;
    const QString machine_output = readCommandOutput(normalizedPath, {QStringLiteral("--machine-info")}, timeoutMs);
    if (const auto machine_info = parseMachineInfoText(machine_output))
    {
        version = machine_info->version;
        result.machineInfoDetected = true;
        result.machineProgressSupported = machine_info->machineProgressSupported;
    }
    else if (const auto machine_product = recognizedMachineInfoProduct(machine_output))
    {
        if (*machine_product != QStringLiteral("starnet2"))
        {
            result.detail = QStringLiteral("The configured executable returned machine information for a product "
                                           "other than StarNet2.");
            return result;
        }
    }
    else
    {
        const int remaining_timeout = timeoutMs - static_cast<int>(probe_timer.elapsed());
        version = parseVersionText(readVersionOutput(normalizedPath, remaining_timeout));
    }

    result.versionDetected = version.has_value();
    if (!version)
    {
        result.detail = QStringLiteral("Could not detect StarNet2 version. StarNet2 %1 or newer is required.")
                            .arg(minimumSupportedVersionText());
        return result;
    }

    result.versionText = version->toString();
    result.supported = isSupportedVersion(*version);
    if (!result.supported)
    {
        result.detail = QStringLiteral("StarNet2 %1 detected. StarNet2 %2 or newer is required.")
                            .arg(result.versionText, minimumSupportedVersionText());
        return result;
    }

    result.detail = QStringLiteral("StarNet2 %1 detected.").arg(result.versionText);
    return result;
}

} // namespace an::ui::starnet
