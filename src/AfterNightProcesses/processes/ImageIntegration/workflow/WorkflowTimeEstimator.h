// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightProcessesExport.h"

#include "WorkflowManifest.h"
#include <QObject>

class AFTERNIGHTPROCESSES_API WorkflowTimeEstimator
{
public:
    /// The seconds a run is expected to spend in each of its stages, from the frames and the groups its manifest asks
    /// each stage to work on. The alignment has no figure of its own: it is part of what the stacking is given.
    struct StageSeconds
    {
        double bias = 0.0;
        double dark = 0.0;
        double flat = 0.0;
        double lightsCalibration = 0.0;
        double lightsRegistration = 0.0;
        double lightsNormalization = 0.0;
        double lightsDrizzle = 0.0;
        double lightsStacking = 0.0;

        /// The figure of a stage by the name it has in a manifest's stage sequence ("LightsRegistration"); nothing
        /// for a name that is none of them.
        [[nodiscard]] double of(const QString& stageToken) const;

        [[nodiscard]] double total() const;
    };

    [[nodiscard]] static StageSeconds estimateStageSeconds(const WorkflowManifest& manifest);

    static int estimateTotalSeconds(const WorkflowManifest& manifest);
    static QString formatDuration(int seconds);
};
