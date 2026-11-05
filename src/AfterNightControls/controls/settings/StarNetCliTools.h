// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightControlsExport.h"

#include <QVersionNumber>
#include <QString>
#include <QStringList>

#include <optional>

namespace an::ui::starnet
{

struct AFTERNIGHTCONTROLS_API CliOptions
{
    QString inputPath;
    QString outputPath;
    QString maskPath;
    QString unscreenPath;
    QString weightsPath;
    int stride = 256;
    bool upsample = false;
    bool highlightProtection = true;
    bool machineProgress = false;
};

struct AFTERNIGHTCONTROLS_API MachineInfo
{
    QVersionNumber version;
    bool machineProgressSupported = false;
};

struct AFTERNIGHTCONTROLS_API ExecutableCheckResult
{
    bool exists = false;
    bool executable = false;
    bool versionDetected = false;
    bool supported = false;
    bool machineInfoDetected = false;
    bool machineProgressSupported = false;
    QString versionText;
    QString detail;
};

[[nodiscard]] AFTERNIGHTCONTROLS_API QVersionNumber minimumSupportedVersion();
[[nodiscard]] AFTERNIGHTCONTROLS_API QString minimumSupportedVersionText();
[[nodiscard]] AFTERNIGHTCONTROLS_API QStringList executableNames();
[[nodiscard]] AFTERNIGHTCONTROLS_API QStringList commonInstallDirectories();
[[nodiscard]] AFTERNIGHTCONTROLS_API std::optional<QVersionNumber> parseVersionText(const QString& output);
[[nodiscard]] AFTERNIGHTCONTROLS_API std::optional<MachineInfo> parseMachineInfoText(const QString& output);
[[nodiscard]] AFTERNIGHTCONTROLS_API bool isSupportedVersion(const QVersionNumber& version);
[[nodiscard]] AFTERNIGHTCONTROLS_API int normalizeStride(int stride);
[[nodiscard]] AFTERNIGHTCONTROLS_API QString weightsBrowseDirectory(const QString& weightsPath,
                                                                    const QString& executablePath);
[[nodiscard]] AFTERNIGHTCONTROLS_API QStringList buildArguments(const CliOptions& options);
[[nodiscard]] AFTERNIGHTCONTROLS_API QString readVersionOutput(const QString& executablePath, int timeoutMs = 5000);
[[nodiscard]] AFTERNIGHTCONTROLS_API ExecutableCheckResult checkExecutable(const QString& executablePath,
                                                                           int timeoutMs = 5000);

} // namespace an::ui::starnet
