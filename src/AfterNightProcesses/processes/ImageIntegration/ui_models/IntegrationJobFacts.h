// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// What an integration job says of itself in one line and one measure: its lights, their groups, the time they
/// integrate, what its files weigh, the setup they were taken with, and the disk its runs write to - what is free there
/// and what a run would write.
///
/// The facts of the model are read on the GUI thread, from memory. What the disk adds - the bytes of the lights' files,
/// the room free where the runs write - is read off it (`readIntegrationJobDisk`), so a slow volume holds no window.

#include "AfterNightProcessesExport.h"

#include <QString>
#include <QStringList>
#include <QtGlobal>

class IntegrationDataModel;

namespace ImageIntegration
{

/// What a run of a job would write into its output folder, by what it is made of. Every image a run writes holds
/// 32-bit floats: a calibrated light keeps its own channels, a registered or stacked one the demosaiced ones. A
/// drizzled group weighs its stacked image alone: it writes what describes where each light lands, not a map of its
/// pixels.
struct IntegrationOutputEstimate
{
    /// The lights that are included, and the ones of them whose calibrated copy and whose registered copy are
    /// written.
    int includedLights = 0;
    int calibratedLights = 0;
    int registeredLights = 0;

    /// The integration groups, one stacked image each; at least one.
    int groups = 1;

    /// The rejection maps the groups ask for.
    int rejectionMapFiles = 0;

    qint64 stackedBytes = 0;
    qint64 rejectionBytes = 0;
    qint64 calibratedBytes = 0;
    qint64 registeredBytes = 0;

    /// Whether a light whose colour layout the import did not record was counted as a colour mosaic.
    bool layoutAssumed = false;

    /// Everything a run would write.
    [[nodiscard]] qint64 total() const noexcept
    {
        return stackedBytes + rejectionBytes + calibratedBytes + registeredBytes;
    }

    [[nodiscard]] bool operator==(const IntegrationOutputEstimate&) const = default;
};

/// What a run of a job would write, from each included light's size and layout - not from its file - and from what
/// each of its groups asks for, group by group: a stacked image, as many times larger as its scale squared where the
/// group is drizzled, and its rejection maps; the calibrated lights where they are kept on disk; and the registered
/// lights of a group that keeps them or stacks a comet. A group that is drizzled places its lights itself, and one
/// that lets its registered lights go has them warped as its stack reads them: neither writes one. A reprocessing
/// run keeps them too, which the model does not say. GUI thread.
[[nodiscard]] AFTERNIGHTPROCESSES_API IntegrationOutputEstimate
estimateIntegrationOutput(const IntegrationDataModel& model);

/// A size as the integration's windows write what a run needs and what is free: "62.56 GB", "431.2 MB", "12.0 KB".
[[nodiscard]] AFTERNIGHTPROCESSES_API QString formatOutputBytes(qint64 bytes);

/// What a job says of itself, read from its model.
struct IntegrationJobFacts
{
    /// Its light frames, the excluded ones among them.
    int lights = 0;

    /// The groups its lights fall in.
    int groups = 0;

    /// What its included lights integrate, in seconds.
    double integrationSeconds = 0.0;

    /// The setup its included lights say - the readout mode, the frame's size, the gain and the offset, each where the
    /// frames say it - or how many setups they are of when they do not agree; empty when they say nothing.
    QString setup;

    /// What a run would write (`estimateIntegrationOutput`).
    qint64 neededBytes = 0;

    /// The files of its lights and the folder its runs write to, for the disk to be read.
    QStringList lightPaths;
    QString outputDirectory;

    [[nodiscard]] bool operator==(const IntegrationJobFacts&) const = default;
};

/// What the disk adds to a job's facts; a figure not read is negative.
struct IntegrationJobDisk
{
    /// What the files of its lights weigh together: the ones that are there.
    qint64 lightBytes = -1;

    /// What is free where its runs write.
    qint64 freeBytes = -1;

    [[nodiscard]] bool operator==(const IntegrationJobDisk&) const = default;
};

/// A job's facts, from its model. GUI thread.
[[nodiscard]] AFTERNIGHTPROCESSES_API IntegrationJobFacts integrationJobFacts(const IntegrationDataModel& model);

/// Reads what the disk adds: the sizes of the lights' files and the room free in the output folder - or, while the
/// folder is not made yet, where it will be made. An empty folder name reads no room. It reads the file system: it is
/// called off the GUI thread.
[[nodiscard]] AFTERNIGHTPROCESSES_API IntegrationJobDisk readIntegrationJobDisk(const QStringList& lightPaths,
                                                                                const QString& outputDirectory);

/// A job's facts on one line, a middle dot between them: "718 frames · 11 groups · 20h 2m · 62.6 GB · 8288x5644 ·
/// Gain 120 · Offset 30". The groups and the time are said once there are lights, the bytes once the disk was read,
/// the setup where the frames say one.
[[nodiscard]] AFTERNIGHTPROCESSES_API QString integrationJobFactsText(const IntegrationJobFacts& facts,
                                                                      const IntegrationJobDisk& disk);

/// The disk's line: "253.75 GB free · need ~62.56 GB", and what is free alone for a job that needs nothing yet. Empty
/// while what is free is not read.
[[nodiscard]] AFTERNIGHTPROCESSES_API QString integrationJobDiskText(const IntegrationJobFacts& facts,
                                                                     const IntegrationJobDisk& disk);

/// The share of what is free that the job needs, from 0 to 1: 1 once it needs as much or more.
[[nodiscard]] AFTERNIGHTPROCESSES_API double integrationJobDiskShare(const IntegrationJobFacts& facts,
                                                                     const IntegrationJobDisk& disk);

/// Whether the job needs more than is free.
[[nodiscard]] AFTERNIGHTPROCESSES_API bool integrationJobDiskExceeded(const IntegrationJobFacts& facts,
                                                                      const IntegrationJobDisk& disk);

} // namespace ImageIntegration
