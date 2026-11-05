// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/**
 * @file ImageIntegrationProcessImport.cpp
 * @brief Frame import: the batch/session helpers and the process methods that drive them.
 *
 * Split out of ImageIntegrationProcess.cpp, the import session being one of the two pieces lifted out of that
 * file.
 *
 * performImportWithProgress() is the whole of it - detection runs on a worker thread while a timer
 * pumps the progress dialog, and the model is only touched once the worker has finished. The two
 * dialog entry points above it just gather paths and hand them over.
 */
#include "stdafx.hpp"
#include "processes/ImageIntegration/ImageIntegrationImportSession.h"

#include "processes/ImageIntegration/ImageIntegrationConstants.h"
#include "processes/ImageIntegration/ImageIntegrationProcess.h"
#include "processes/ImageIntegration/ImageIntegrationProcessShared.h"
#include "processes/ImageIntegration/model/UndoCommands.h"
#include "processes/ImageIntegration/model/FrameMetadata.h"
#include "processes/ProcessCommonUtils.h"

#include <QApplication>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>

#include <algorithm>
#include <utility>

namespace ImageIntegration::detail
{

void refreshImportProgressDialog(const std::shared_ptr<ImportProgressSession>& session)
{
    if (!session || !session->progress)
    {
        return;
    }

    session->progress->setProgress(session->reporter.completedFiles());
    session->progress->setCurrentFile(session->reporter.currentFile());
    session->progress->setEtaMs(session->reporter.estimatedRemainingMs());

    const auto errors = session->reporter.errors();
    const int issueCount = static_cast<int>(errors.size());
    QStringList issueLines;
    const int start = (std::max)(0, issueCount - 5);
    for (int i = start; i < issueCount; ++i)
    {
        const auto& error = errors[static_cast<size_t>(i)];
        issueLines.append(QStringLiteral("%1: %2").arg(QFileInfo(error.path).fileName(), error.message));
    }
    session->progress->setIssueCount(issueCount);
    session->progress->setIssueDetails(issueLines);
}

} // namespace ImageIntegration::detail

using ImageIntegration::detail::applicationEngineSession;
using ImageIntegration::detail::applyImportBatch;
using ImageIntegration::detail::ImportProgressSession;
using ImageIntegration::detail::importSummaryText;
using ImageIntegration::detail::notifyErrorOrDialog;
using ImageIntegration::detail::notifyInfoOrDialog;
using ImageIntegration::detail::notifyWarnOrDialog;
using ImageIntegration::detail::openNonBlockingMessageBox;
using ImageIntegration::detail::PreparedImportBatch;
using ImageIntegration::detail::prepareImportBatch;
using ImageIntegration::detail::refreshImportProgressDialog;
using ImageIntegration::detail::rememberDirectoryForPath;
using ImageIntegration::detail::rememberDirectoryPath;
using ImageIntegration::detail::rememberedExistingPath;

void ImageIntegrationProcess::performImportWithProgress(const std::vector<QString>& filePaths, FrameType frameType,
                                                        bool forceToType)
{
    if (filePaths.empty())
    {
        return;
    }

    if (m_importInProgress)
    {
        notifyWarnOrDialog(this, tr("Import In Progress"), tr("Another import is already running."));
        return;
    }

    const an::consumer::EngineSession* engineSession = applicationEngineSession();
    if (engineSession == nullptr)
    {
        notifyErrorOrDialog(this, tr("Import Failed"), tr("No engine session is available to read the files."));
        return;
    }

    m_importInProgress = true;

    const int total = static_cast<int>(filePaths.size());
    auto session = std::make_shared<ImportProgressSession>();
    session->filePaths = filePaths;
    session->frameType = frameType;
    session->forceToType = forceToType;
    session->engineSession = engineSession;
    session->reporter.setTotalFiles(total);

    auto* progress = new ImageIntegration::ImportProgressDialog(total, this);
    session->progress = progress;

    auto* progressRefreshTimer = new QTimer(progress);
    session->progressRefreshTimer = progressRefreshTimer;
    progressRefreshTimer->setInterval(100);

    QPointer<ImageIntegrationProcess> self(this);
    const auto finishImport = [self, session]() {
        auto* process = self.data();
        if (session->progressRefreshTimer)
        {
            session->progressRefreshTimer->stop();
        }
        if (session->worker.joinable())
        {
            session->worker.join();
        }
        refreshImportProgressDialog(session);

        const bool importCancelled = session->isCanceled.load(std::memory_order_acquire) ||
                                     (session->progress && session->progress->isCancelled());
        if (session->progress)
        {
            session->progress->close();
            session->progress->deleteLater();
        }

        if (!process)
        {
            return;
        }

        process->m_importInProgress = false;
        if (importCancelled)
        {
            return;
        }

        ImageIntegration::ImportResult importResult;
        importResult.importedCount = static_cast<int>(session->detectedFrames.size());
        importResult.skippedCount = static_cast<int>(session->filePaths.size()) - importResult.importedCount;
        importResult.issues.assign(session->importIssues.begin(), session->importIssues.end());

        if (importResult.hasIssues())
        {
            const QString title = process->tr("Import Issues");
            const QString summary = importResult.summaryMessage();
            openNonBlockingMessageBox(process, importResult.hasErrors() ? QMessageBox::Critical : QMessageBox::Warning,
                                      title, summary, importResult.detailedText());
        }

        if (session->detectedFrames.empty())
        {
            return;
        }

        PreparedImportBatch batch =
            prepareImportBatch(std::vector<ImageIntegration::DetectedFrameInfo>(session->detectedFrames.begin(),
                                                                                session->detectedFrames.end()),
                               session->frameType, session->forceToType);
        applyImportBatch(process->m_model.get(), process->m_undoStack, batch, process->tr("Import frames"));

        const QString title = process->tr("Import Complete");
        const QString summary = importSummaryText(batch.groupedInfos, importResult.skippedCount);
        notifyInfoOrDialog(process, title, summary);
    };

    // Propagate cancel/pause to worker threads
    QObject::connect(progress->cancelButton(), &QPushButton::clicked, progress,
                     [session]() { session->isCanceled.store(true, std::memory_order_release); });
    QObject::connect(progress, &QDialog::rejected, progress,
                     [session]() { session->isCanceled.store(true, std::memory_order_release); });
    QObject::connect(progress->pauseButton(), &QPushButton::clicked, progress, [session]() {
        if (!session->progress)
        {
            return;
        }

        if (session->progress->isPaused())
        {
            session->reporter.pause();
        }
        else
        {
            session->reporter.resume();
        }
    });
    QObject::connect(progressRefreshTimer, &QTimer::timeout, progress,
                     [session]() { refreshImportProgressDialog(session); });
    progressRefreshTimer->start();
    progress->open();

    // Run TBB in a worker thread to keep the dialog responsive. UI updates are polled from
    // the main thread so the worker never queues lambdas against a dialog that might be closing.
    QTimer::singleShot(0, progress, [session, finishImport]() {
        session->importTimer.start();
        session->worker = std::thread([session, finishImport]() {
            tbb::parallel_for_each(
                session->filePaths.begin(), session->filePaths.end(), [session](const QString& path) {
                    if (session->isCanceled.load(std::memory_order_acquire))
                    {
                        return;
                    }

                    while (session->reporter.isPaused() && !session->isCanceled.load(std::memory_order_acquire))
                    {
                        std::this_thread::sleep_for(std::chrono::milliseconds(50));
                    }

                    session->reporter.onFileStarted(path);

                    const auto res = ImageIntegration::buildDetectedFrameInfoWithIssue(*session->engineSession, path,
                                                                                       session->frameType);
                    if (res.issue)
                    {
                        session->importIssues.push_back(*res.issue);
                        session->reporter.onMetadataError(path, res.issue->message);
                    }
                    if (res.detected)
                    {
                        session->detectedFrames.push_back(std::move(*res.detected));
                    }

                    const int current = session->processed.fetch_add(1) + 1;
                    session->reporter.onFilesCompleted(current, static_cast<long long>(session->importTimer.elapsed()));
                });

            if (QObject* app = QCoreApplication::instance())
            {
                QMetaObject::invokeMethod(app, finishImport, Qt::QueuedConnection);
            }
            else
            {
                session->worker.detach();
            }
        });
    });
}

void ImageIntegrationProcess::importFilesDialog(FrameType frameType, bool forceToType)
{
    static const QString contextKey = QStringLiteral("__IMAGE_INTEGRATION_IMPORT__");
    const QString initialDir = rememberedExistingPath(contextKey);

    QStringList files =
        an::ui::dialogs::getOpenFileNames(process_utils::currentGlobalSettings(), this, tr("Select Files"), initialDir,
                                          an::ui::dialogs::supportedImageOpenNameFilter());
    if (files.isEmpty())
    {
        return;
    }

    rememberDirectoryForPath(contextKey, files.constFirst());

    std::vector<QString> filePaths;
    filePaths.reserve(static_cast<size_t>(files.size()));
    for (const auto& f : files)
    {
        if (ImageIntegration::detail::isReadableImagePath(f))
        {
            filePaths.push_back(f);
        }
    }

    performImportWithProgress(filePaths, frameType, forceToType);
}

void ImageIntegrationProcess::importFolderDialog(FrameType frameType, bool forceToType, bool recursive)
{
    static const QString contextKey = QStringLiteral("__IMAGE_INTEGRATION_IMPORT__");
    const QString initialDir = rememberedExistingPath(contextKey);

    QString dir = an::ui::dialogs::getExistingDirectory(process_utils::currentGlobalSettings(), this,
                                                        tr("Select Directory"), initialDir);
    if (dir.isEmpty())
    {
        return;
    }

    rememberDirectoryPath(contextKey, dir);

    const QDirIterator::IteratorFlags flags = recursive ? QDirIterator::Subdirectories : QDirIterator::NoIteratorFlags;
    QDirIterator it(dir, QDir::Files, flags);
    std::vector<QString> filePaths;

    while (it.hasNext())
    {
        QString path = it.next();
        if (ImageIntegration::detail::isReadableImagePath(path))
        {
            filePaths.push_back(std::move(path));
        }
    }

    if (filePaths.empty())
    {
        openNonBlockingMessageBox(this, QMessageBox::Information, tr("No Files Found"),
                                  tr("No supported image files found in the selected directory."));
        return;
    }

    performImportWithProgress(filePaths, frameType, forceToType);
}

void ImageIntegrationProcess::onAddFiles()
{
    importFilesDialog(currentFrameTypeForActions(), /*forceToType=*/false);
}

void ImageIntegrationProcess::onAddDirectory()
{
    importFolderDialog(currentFrameTypeForActions(), /*forceToType=*/false, /*recursive=*/true);
}
