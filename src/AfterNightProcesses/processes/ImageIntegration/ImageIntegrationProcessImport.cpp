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
 * pumps the progress panel, and the model is only touched once the worker has finished. The two
 * dialog entry points above it just gather paths and hand them over.
 *
 * The panel is shown in a dialog over a process that is a window of its own, which waits behind it; a process
 * shown as a page shows the panel in its page and makes its own acts wait, so the rest of the application
 * stays usable while frames are read.
 */
#include "stdafx.hpp"
#include "processes/ImageIntegration/ImageIntegrationImportSession.h"

#include "processes/ImageIntegration/ImageIntegrationConstants.h"
#include "processes/ImageIntegration/ImageIntegrationProcess.h"
#include "processes/ImageIntegration/ImageIntegrationProcessShared.h"
#include "processes/ImageIntegration/model/UndoCommands.h"
#include "processes/ImageIntegration/model/FrameMetadata.h"
#include "processes/ProcessCommonUtils.h"

#include "theme/AnDesignTokens.h"

#include <QAction>
#include <QApplication>
#include <QBoxLayout>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QTabWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <functional>
#include <utility>

namespace ImageIntegration::detail
{

void refreshImportProgressDialog(const std::shared_ptr<ImportProgressSession>& session)
{
    if (!session || !session->progress)
    {
        return;
    }

    // The dialog closes itself once every file is read; a page's panel goes when the import ends.
    if (session->dialog)
    {
        session->dialog->setProgress(session->reporter.completedFiles());
    }
    else
    {
        session->progress->setProgress(session->reporter.completedFiles());
    }
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

    // In a window of its own the process waits behind the import's dialog; as a page it shows the import in itself.
    ImageIntegration::ImportProgressDialog* dialog = nullptr;
    ImageIntegration::ImportProgressPanel* progress = nullptr;
    if (hasPageLayout())
    {
        progress = beginPageImport(total);
    }
    else
    {
        dialog = new ImageIntegration::ImportProgressDialog(total, this);
        progress = dialog->panel();
    }
    session->progress = progress;
    session->dialog = dialog;

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
        if (session->dialog)
        {
            session->dialog->close();
            session->dialog->deleteLater();
        }

        if (!process)
        {
            return;
        }

        process->endPageImport();
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
    if (dialog != nullptr)
    {
        QObject::connect(dialog, &QDialog::rejected, dialog,
                         [session]() { session->isCanceled.store(true, std::memory_order_release); });
    }
    // What holds the panel going away under the import cancels it: a page closed while its frames are read.
    QObject::connect(progress, &QObject::destroyed,
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
    if (dialog != nullptr)
    {
        dialog->open();
    }

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

ImageIntegration::ImportProgressPanel* ImageIntegrationProcess::beginPageImport(const int totalFiles)
{
    // A band over the stages, inside what the page holds, with the import's panel in it.
    QWidget* const content = rootTabWidget != nullptr ? rootTabWidget->parentWidget() : this;
    auto* band = new QWidget(content);
    band->setObjectName(QStringLiteral("importProgressBand"));
    const auto& spacing = an::ui::tokens().spacing;
    auto* bandLayout = new QVBoxLayout(band);
    bandLayout->setContentsMargins(spacing.lg, spacing.md, spacing.lg, spacing.md);
    bandLayout->setSpacing(0);
    auto* panel = new ImageIntegration::ImportProgressPanel(totalFiles, band);
    bandLayout->addWidget(panel);
    auto* const held = content != nullptr ? qobject_cast<QBoxLayout*>(content->layout()) : nullptr;
    if (held != nullptr && rootTabWidget != nullptr)
    {
        held->insertWidget((std::max)(0, held->indexOf(rootTabWidget)), band);
    }
    band->show();
    m_pageImportBand = band;
    m_pageImportPanel = panel;

    // The page's own acts wait for the import, as its window waits behind the import's dialog: the stages, the foot
    // and every entry of the menus - the header's buttons and the page's keys with them.
    if (rootTabWidget != nullptr)
    {
        rootTabWidget->setEnabled(false);
    }
    if (m_ui.bottomActionBar != nullptr)
    {
        m_ui.bottomActionBar->setEnabled(false);
    }
    m_importHeldActions.clear();
    const std::function<void(const QList<QAction*>&)> hold = [this, &hold](const QList<QAction*>& entries) {
        for (QAction* entry : entries)
        {
            if (entry->isEnabled())
            {
                m_importHeldActions.emplace_back(entry);
                entry->setEnabled(false);
            }
            if (const QMenu* menu = entry->menu())
            {
                hold(menu->actions());
            }
        }
    };
    if (mainMenuBar != nullptr)
    {
        hold(mainMenuBar->actions());
    }
    return panel;
}

void ImageIntegrationProcess::endPageImport()
{
    if (m_pageImportBand.isNull() && m_importHeldActions.empty())
    {
        return;
    }
    m_pageImportPanel = nullptr;
    if (!m_pageImportBand.isNull())
    {
        m_pageImportBand->hide();
        m_pageImportBand->deleteLater();
        m_pageImportBand = nullptr;
    }
    if (rootTabWidget != nullptr)
    {
        rootTabWidget->setEnabled(true);
    }
    if (m_ui.bottomActionBar != nullptr)
    {
        m_ui.bottomActionBar->setEnabled(true);
    }
    // Every act that waited is given back; the ones that follow the job - Execute, the removals - are then set from it.
    for (const QPointer<QAction>& entry : m_importHeldActions)
    {
        if (!entry.isNull())
        {
            entry->setEnabled(true);
        }
    }
    m_importHeldActions.clear();
    updateButtonStates();
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
