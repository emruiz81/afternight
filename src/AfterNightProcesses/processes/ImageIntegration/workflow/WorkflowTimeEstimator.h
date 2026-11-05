// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightProcessesExport.h"

#include "WorkflowManifest.h"
#include <QObject>

class AFTERNIGHTPROCESSES_API WorkflowTimeEstimator
{
public:
    static int estimateTotalSeconds(const WorkflowManifest& manifest);
    static QString formatDuration(int seconds);
};
