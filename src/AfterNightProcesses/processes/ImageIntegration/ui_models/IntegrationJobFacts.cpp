// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"

#include "processes/ImageIntegration/ui_models/IntegrationJobFacts.h"

#include "processes/ImageIntegration/model/GroupingKeys.h"
#include "processes/ImageIntegration/model/IntegrationDataModel.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QMap>
#include <QSize>
#include <QStorageInfo>

#include <algorithm>
#include <cmath>
#include <optional>

namespace ImageIntegration
{

namespace
{

[[nodiscard]] QString translated(const char* text)
{
    return QCoreApplication::translate("IntegrationJobFacts", text);
}

/// The mark between two facts.
[[nodiscard]] QString dot()
{
    return QStringLiteral(" · ");
}

[[nodiscard]] bool isAverageStackMethod(const QString& method)
{
    const QString normalized = method.trimmed().toLower();
    return normalized.contains(QStringLiteral("average")) || normalized == QStringLiteral("mean");
}

/// What the files a run makes from one light weigh. Every image the run writes holds 32-bit floats: calibrated, the
/// light keeps its own channels; registered or stacked, it holds the demosaiced ones.
struct LightOutputBytes
{
    qint64 calibrated = 0;
    qint64 developed = 0;
    /// Whether the import recorded no layout, so the light counted as a colour mosaic.
    bool layoutAssumed = false;
};

[[nodiscard]] LightOutputBytes lightOutputBytes(const FrameInfo& frame)
{
    constexpr qint64 kSampleBytes = 4;
    const QSize dimensions = frame.metadata.dimensions();
    if (dimensions.width() <= 0 || dimensions.height() <= 0)
    {
        // The probe read no size: the file's own size is the only figure left.
        const QFileInfo info(frame.displayPath);
        const qint64 fileBytes = info.exists() ? info.size() : 0;
        return LightOutputBytes{.calibrated = fileBytes, .developed = fileBytes};
    }
    const qint64 plane = static_cast<qint64>(dimensions.width()) * dimensions.height() * kSampleBytes;
    const std::optional<FrameSampleLayout> layout = frame.metadata.sampleLayout();
    if (!layout.has_value())
    {
        // A project saved before the import recorded the layout: the heavier reading, a colour mosaic.
        return LightOutputBytes{.calibrated = plane, .developed = plane * 3, .layoutAssumed = true};
    }
    return LightOutputBytes{.calibrated = plane * layout->channels, .developed = plane * layout->developedChannels()};
}

/// The setup a light says: its readout mode, its size, its gain and its offset, each where it says it.
[[nodiscard]] QString setupOf(const FrameInfo& frame)
{
    QStringList parts;
    const QString readout = frame.metadata.readoutMode().trimmed();
    if (!readout.isEmpty())
    {
        parts << readout;
    }
    const QSize dimensions = frame.metadata.dimensions();
    if (dimensions.width() > 0 && dimensions.height() > 0)
    {
        parts << frame.metadata.resolutionString();
    }
    if (const std::optional<int> gain = frame.metadata.isoGain())
    {
        parts << translated("Gain %1").arg(*gain);
    }
    if (const std::optional<int> offset = frame.metadata.offset())
    {
        parts << translated("Offset %1").arg(*offset);
    }
    return parts.join(dot());
}

/// A time as a job's facts say it: "20h 2m", "4m 0s", "42s".
[[nodiscard]] QString durationText(const double seconds)
{
    const qint64 total = static_cast<qint64>(std::llround((std::max)(0.0, seconds)));
    const qint64 hours = total / 3600;
    const qint64 minutes = (total % 3600) / 60;
    const qint64 rest = total % 60;
    if (hours > 0)
    {
        return translated("%1h %2m").arg(hours).arg(minutes);
    }
    if (minutes > 0)
    {
        return translated("%1m %2s").arg(minutes).arg(rest);
    }
    return translated("%1s").arg(rest);
}

/// What files weigh, to one decimal: "62.6 GB", "1.5 MB", "12.0 KB", "512 B".
[[nodiscard]] QString weightText(const qint64 bytes)
{
    constexpr double kKiB = 1024.0;
    const double value = static_cast<double>(bytes);
    if (value >= kKiB * kKiB * kKiB)
    {
        return QStringLiteral("%1 GB").arg(value / (kKiB * kKiB * kKiB), 0, 'f', 1);
    }
    if (value >= kKiB * kKiB)
    {
        return QStringLiteral("%1 MB").arg(value / (kKiB * kKiB), 0, 'f', 1);
    }
    if (value >= kKiB)
    {
        return QStringLiteral("%1 KB").arg(value / kKiB, 0, 'f', 1);
    }
    return QStringLiteral("%1 B").arg(bytes);
}

} // namespace

IntegrationOutputEstimate estimateIntegrationOutput(const IntegrationDataModel& model)
{
    IntegrationOutputEstimate estimate;
    // What the run writes is read from each light's size and layout, not from its file: a raw file holds one mosaic
    // plane of 12 or 16 bits, its calibrated copy that plane in 32-bit floats, and its registered copy three.
    qint64 registeredLightBytes = 0;
    // A stacked image or map is as large as the largest light, demosaiced.
    qint64 largestLightBytes = 0;
    const auto frames = model.lightFrames().frames();
    for (const auto& frame : frames)
    {
        if (frame.excluded)
        {
            continue;
        }
        ++estimate.includedLights;
        const LightOutputBytes bytes = lightOutputBytes(frame);
        registeredLightBytes += bytes.developed;
        largestLightBytes = (std::max)(largestLightBytes, bytes.developed);
        estimate.layoutAssumed = estimate.layoutAssumed || bytes.layoutAssumed;
    }

    const auto& groupKeys = model.integrationGroups();
    estimate.groups = (std::max<int>)(1, static_cast<int>(groupKeys.size()));

    const auto& stackingSettings = model.stackingSettings(FrameType::Light);
    // A group with no settings of its own runs with the defaults, as the manifest builds it.
    const StackingSettings defaultSettings{};
    bool anyDrizzle = false;
    bool anyNonDrizzle = false;
    for (const auto& key : groupKeys)
    {
        const auto it = stackingSettings.find(key);
        const StackingSettings& settings = it != stackingSettings.end() ? it->second : defaultSettings;
        anyDrizzle = anyDrizzle || settings.drizzle.enabled;
        anyNonDrizzle = anyNonDrizzle || !settings.drizzle.enabled;

        if (settings.createRejectionMaps && isAverageStackMethod(settings.stackMethod))
        {
            estimate.rejectionMapFiles += settings.mergeRejectionMaps ? 1 : 2;
        }
    }

    estimate.stackedBytes = largestLightBytes * estimate.groups;
    estimate.rejectionBytes = largestLightBytes * estimate.rejectionMapFiles;

    // The calibrated lights are written only where they are kept on disk, and never for a group that is calibrated
    // already.
    if (model.lightCalibrationCacheMode() == LightCalibrationCacheMode::Disk)
    {
        const auto& lightCalibSettings = model.lightCalibrationSettings();
        for (const auto& group : model.lightGroups())
        {
            const auto calIt = lightCalibSettings.find(group.key);
            const bool isPreCalibrated = (calIt != lightCalibSettings.end() && calIt->second.preCalibrated);
            if (isPreCalibrated)
            {
                continue;
            }
            for (const auto& frame : group.frames)
            {
                if (!frame.excluded)
                {
                    ++estimate.calibratedLights;
                    estimate.calibratedBytes += lightOutputBytes(frame).calibrated;
                }
            }
        }
    }

    estimate.registeredBytes = anyNonDrizzle ? registeredLightBytes : 0;

    if (anyDrizzle)
    {
        for (const auto& frame : frames)
        {
            if (frame.excluded)
            {
                continue;
            }
            const QSize dims = frame.metadata.dimensions();
            if (dims.width() <= 0 || dims.height() <= 0)
            {
                continue;
            }
            // Pixmaps store x/y maps as 2 floats per pixel.
            estimate.drizzlePixmapBytes += static_cast<qint64>(dims.width()) * static_cast<qint64>(dims.height()) * 8;
        }
    }
    return estimate;
}

QString formatOutputBytes(const qint64 bytes)
{
    if (bytes < 1024)
    {
        return QString::number(bytes) + QStringLiteral(" B");
    }
    const double kb = static_cast<double>(bytes) / 1024.0;
    if (kb < 1024)
    {
        return QString::number(kb, 'f', 1) + QStringLiteral(" KB");
    }
    const double mb = kb / 1024.0;
    if (mb < 1024)
    {
        return QString::number(mb, 'f', 1) + QStringLiteral(" MB");
    }
    const double gb = mb / 1024.0;
    return QString::number(gb, 'f', 2) + QStringLiteral(" GB");
}

IntegrationJobFacts integrationJobFacts(const IntegrationDataModel& model)
{
    IntegrationJobFacts facts;
    const auto frames = model.lightFrames().frames();
    facts.lights = static_cast<int>(frames.size());
    facts.groups = static_cast<int>(model.lightGroups().size());
    facts.outputDirectory = model.outputDirectory().trimmed();
    facts.lightPaths.reserve(facts.lights);
    // The setups the included lights say, by how each says it, in the order they are met.
    QStringList setups;
    for (const auto& frame : frames)
    {
        facts.lightPaths.append(frame.displayPath);
        if (frame.excluded)
        {
            continue;
        }
        facts.integrationSeconds += frame.metadata.exposureSeconds();
        const QString setup = setupOf(frame);
        if (!setup.isEmpty() && !setups.contains(setup))
        {
            setups.append(setup);
        }
    }
    if (setups.size() == 1)
    {
        facts.setup = setups.front();
    }
    else if (setups.size() > 1)
    {
        facts.setup = translated("%1 setups").arg(setups.size());
    }
    facts.neededBytes = facts.lights > 0 ? estimateIntegrationOutput(model).total() : 0;
    return facts;
}

IntegrationJobDisk readIntegrationJobDisk(const QStringList& lightPaths, const QString& outputDirectory)
{
    IntegrationJobDisk disk;
    disk.lightBytes = 0;
    for (const QString& path : lightPaths)
    {
        const QFileInfo info(path);
        if (info.isFile())
        {
            disk.lightBytes += info.size();
        }
    }
    const QString folder = outputDirectory.trimmed();
    if (folder.isEmpty())
    {
        return disk;
    }
    // A folder not made yet is on the volume of the nearest folder above it that is there.
    QString at = QFileInfo(QDir::cleanPath(folder)).absoluteFilePath();
    while (!QFileInfo(at).isDir())
    {
        const QString above = QFileInfo(at).absolutePath();
        if (above.isEmpty() || above == at)
        {
            return disk;
        }
        at = above;
    }
    const QStorageInfo storage(at);
    if (storage.isValid() && storage.isReady())
    {
        disk.freeBytes = static_cast<qint64>(storage.bytesAvailable());
    }
    return disk;
}

QString integrationJobFactsText(const IntegrationJobFacts& facts, const IntegrationJobDisk& disk)
{
    QStringList said;
    said << (facts.lights == 1 ? translated("1 frame") : translated("%1 frames").arg(facts.lights));
    if (facts.lights > 0)
    {
        said << (facts.groups == 1 ? translated("1 group") : translated("%1 groups").arg(facts.groups));
        if (facts.integrationSeconds > 0.0)
        {
            said << durationText(facts.integrationSeconds);
        }
        if (disk.lightBytes > 0)
        {
            said << weightText(disk.lightBytes);
        }
        if (!facts.setup.isEmpty())
        {
            said << facts.setup;
        }
    }
    return said.join(dot());
}

QString integrationJobDiskText(const IntegrationJobFacts& facts, const IntegrationJobDisk& disk)
{
    if (disk.freeBytes < 0)
    {
        return {};
    }
    const QString free = translated("%1 free").arg(formatOutputBytes(disk.freeBytes));
    if (facts.neededBytes <= 0)
    {
        return free;
    }
    return free + dot() + translated("need ~%1").arg(formatOutputBytes(facts.neededBytes));
}

double integrationJobDiskShare(const IntegrationJobFacts& facts, const IntegrationJobDisk& disk)
{
    if (facts.neededBytes <= 0 || disk.freeBytes < 0)
    {
        return 0.0;
    }
    if (disk.freeBytes == 0 || facts.neededBytes >= disk.freeBytes)
    {
        return 1.0;
    }
    return static_cast<double>(facts.neededBytes) / static_cast<double>(disk.freeBytes);
}

bool integrationJobDiskExceeded(const IntegrationJobFacts& facts, const IntegrationJobDisk& disk)
{
    return disk.freeBytes >= 0 && facts.neededBytes > disk.freeBytes;
}

} // namespace ImageIntegration
