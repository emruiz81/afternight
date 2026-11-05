// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/**
 * @file ImageIntegrationImportSession.h
 * @brief The import batch/progress session shared by the import dialogs and the tree drag-drop.
 *
 * Split out of ImageIntegrationProcess.cpp with the import helpers that drive it.
 *
 * ImportProgressSession owns everything one import run needs while it is in flight - the worker
 * thread, the concurrent detection and issue vectors, the cancel flag and the progress dialog - so
 * it is declared here rather than left file-local: the process starts a session from the import
 * dialogs and the frame tree starts one from a drag-and-drop, and those now live in different
 * translation units.
 */
#pragma once

#include "AfterNightProcessesExport.h"

#include "processes/ImageIntegration/core/FileImport.h"
#include "processes/ImageIntegration/core/ImportProgressReporter.h"
#include "processes/ImageIntegration/dialogs/ImportProgressDialog.h"
#include "processes/ImageIntegration/model/FrameCollection.h"
#include "processes/ImageIntegration/model/FrameType.h"
#include "processes/ImageIntegration/model/IntegrationDataModel.h"

#include <QElapsedTimer>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QUndoStack>

#include <tbb/concurrent_vector.h>

#include <atomic>
#include <map>
#include <memory>
#include <thread>
#include <vector>

namespace an::consumer
{
class EngineSession;
} // namespace an::consumer

namespace ImageIntegration::detail
{

struct PreparedImportBatch
{
    std::map<FrameType, std::vector<FrameInfo>> groupedInfos;
    QStringList autoMasterCandidates;
};

struct ImportProgressSession
{
    std::vector<QString> filePaths;
    FrameType frameType = FrameType::Light;
    bool forceToType = false;
    /// The session whose codecs read the files; the application's, which outlives every import.
    const an::consumer::EngineSession* engineSession = nullptr;

    tbb::concurrent_vector<ImageIntegration::DetectedFrameInfo> detectedFrames;
    tbb::concurrent_vector<ImageIntegration::ImportIssue> importIssues;
    std::atomic<int> processed{0};
    std::atomic<bool> isCanceled{false};
    ImageIntegration::ImportProgressReporter reporter;
    QElapsedTimer importTimer;
    QPointer<ImageIntegration::ImportProgressDialog> progress;
    QPointer<QTimer> progressRefreshTimer;
    std::thread worker;
};

/// Push the session counts collected so far into its progress dialog.
void refreshImportProgressDialog(const std::shared_ptr<ImportProgressSession>& session);

/// Group detected frames by final type and pick out master candidates, without touching the model.
[[nodiscard]] AFTERNIGHTPROCESSES_API PreparedImportBatch prepareImportBatch(
    std::vector<ImageIntegration::DetectedFrameInfo> detectedFrames, FrameType requestedType, bool forceToType);

/// Commit a prepared batch to the model as one undoable command.
AFTERNIGHTPROCESSES_API void applyImportBatch(IntegrationDataModel* model, QUndoStack* undoStack,
                                              const PreparedImportBatch& batch, const QString& commandText);

} // namespace ImageIntegration::detail
