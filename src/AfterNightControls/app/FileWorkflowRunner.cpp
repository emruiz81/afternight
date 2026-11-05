// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"

#include "app/FileWorkflowRunner.h"

#include "controls/GlobalSettingsPersistence.h"
#include "controls/ImageViewControl.h"
#include "controls/ImageViewFiles.h"
#include "controls/ProgressDialogControl.h"
#include "controls/settings/SettingsDefaults.h"
#include "controls/settings/SettingsKeys.h"
#include "session/IEngineSessionProvider.h"
#include "session/ImageDocument.h"

#include "app/AppContextHelpers.h"
#include "app/DemosaicTextMaps.h"
#include "app/FileCompanions.h"
#include "app/FilePolicies.h"

#include <AfterNightConsumerRuntime/EngineSession.h>
#include <AfterNightConsumerRuntime/ImageAsset.h>

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/IO/CodecFormat.h>
#include <AfterNightEngine/IO/SequenceSelection.h>
#include <AfterNightEngine/Image/OutputIntent.h>

#include <AfterNightPlatform/Logging.h>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QPointer>
#include <QThreadPool>

#include <optional>
#include <stop_token>
#include <utility>
#include <vector>

namespace an::ui
{

namespace
{

/// Runs a step on a worker and its completion on the GUI thread, for as long as the guarding object lives.
template <typename WorkT, typename DoneT> void runThenPublish(QObject* context, WorkT work, DoneT done)
{
    QPointer<QObject> guard(context);
    QThreadPool::globalInstance()->start([guard, work = std::move(work), done = std::move(done)]() mutable {
        auto result = std::make_shared<decltype(work())>(work());
        QCoreApplication* application = QCoreApplication::instance();
        if (application == nullptr)
        {
            return;
        }
        QMetaObject::invokeMethod(
            application,
            [guard, result, done = std::move(done)]() mutable {
                if (guard)
                {
                    done(std::move(*result));
                }
            },
            Qt::QueuedConnection);
    });
}

QWidget* resolveDialogParent(const FileWorkflowRunner::Dependencies& dependencies)
{
    return dependencies.dialogParent ? dependencies.dialogParent : process_utils::currentMainWindow();
}

QString selectDemosaicSetting(const FileWorkflowRunner::Dependencies& dependencies)
{
    if (dependencies.globalSettingsPersistence)
    {
        return dependencies.globalSettingsPersistence->select(SettingsKeys::RAW_DEMOSAIC_ALGORITHM,
                                                              SettingsDefaults::RAW_DEMOSAIC_ALGORITHM);
    }

    return process_utils::selectGlobalSetting(SettingsKeys::RAW_DEMOSAIC_ALGORITHM,
                                              SettingsDefaults::RAW_DEMOSAIC_ALGORITHM);
}

/// The process's session, or null when no context lends one.
const an::consumer::EngineSession* currentSession()
{
    if (auto* provider = process_utils::currentEngineSessionProvider())
    {
        return &provider->engineSession();
    }
    return nullptr;
}

/// A failure's message, with its cause when it has one, for the dialog that reports it.
QString describeFailure(const an::Status& status)
{
    QString text = QString::fromUtf8(status.message());
    if (status.hasCause())
    {
        text += QStringLiteral(": ") + QString::fromUtf8(status.cause().message());
    }
    return text;
}

/// What a worker read of one file: a sequence as its document, or an image.
struct OpenedFile
{
    std::optional<ImageDocument> sequence;
    std::optional<ViewReadResult> image;
    an::Status failure = an::Status::success();
};

/// One open of files, carried from step to step on the GUI thread.
struct OpenRun
{
    FileWorkflowRunner::Dependencies dependencies;
    QStringList files;
    QString directory;
    bool recent = false;
    QPointer<ProgressDialogControl> progress;
    std::stop_token stop;
    const an::consumer::EngineSession* session = nullptr;
    std::shared_ptr<const RawOpenPolicy> policy;
    DemosaicChoice demosaic;
    qsizetype next = 0;
    QPointer<ImageViewControl> neighbour;
    bool progressClosed = false;
    FileOpenOutcome outcome;
};

void continueOpen(const std::shared_ptr<OpenRun>& run);

void closeOpenProgress(OpenRun& run)
{
    if (!run.progressClosed && run.progress)
    {
        run.progress->closeAsync();
    }
    run.progressClosed = true;
}

void finishOpen(const std::shared_ptr<OpenRun>& run)
{
    if (!run->recent && run->dependencies.persistLastOpenDirectory)
    {
        run->dependencies.persistLastOpenDirectory(run->directory);
    }
    closeOpenProgress(*run);
    if (run->dependencies.updateImageViewControlList)
    {
        run->dependencies.updateImageViewControlList();
    }
    LOG_INFO("Open: {} file(s) shown, {} failed{}", run->outcome.opened, run->outcome.failed,
             run->outcome.cancelled ? ", stopped" : "");
    if (run->dependencies.openFinished)
    {
        run->dependencies.openFinished(run->outcome);
    }
}

void failOpen(const std::shared_ptr<OpenRun>& run, const QString& path, const QString& description)
{
    ++run->outcome.failed;
    const QString text = QStringLiteral("Could NOT load file '%1'.\nError description: %2").arg(path, description);
    LOG_ERROR(text.toStdString());
    process_utils::notifyErrorMessageAsync(resolveDialogParent(run->dependencies), QStringLiteral("File open error"),
                                           text);
    if (run->recent && run->dependencies.removeRecentFile)
    {
        run->dependencies.removeRecentFile(path.toUtf8().toStdString());
    }
}

/// Records a view the open made, and moves to the next file.
void viewMade(const std::shared_ptr<OpenRun>& run, const QString& path, ImageViewControl* view)
{
    ++run->outcome.opened;
    if (run->dependencies.switchToDockManager)
    {
        run->dependencies.switchToDockManager();
    }
    if (!run->neighbour)
    {
        run->neighbour = view;
    }
    if (run->files.size() == 1)
    {
        closeOpenProgress(*run);
    }
    if (run->recent && run->dependencies.persistLastOpenDirectory)
    {
        run->dependencies.persistLastOpenDirectory(QFileInfo(path).path());
    }
    if (run->dependencies.addRecentFile)
    {
        run->dependencies.addRecentFile(path.toUtf8().toStdString(), view->document().asset());
    }
    continueOpen(run);
}

/// Shows a still image in a new view, on the GUI thread.
void showImage(const std::shared_ptr<OpenRun>& run, const QString& path, an::consumer::ImageAsset&& asset)
{
    const QFileInfo file(path);
    an::Result<ImageDocument> document = ImageDocument::create(
        std::move(asset), DocumentNames{.displayName = file.completeBaseName(), .sourcePath = path});
    if (!document.hasValue())
    {
        failOpen(run, path, describeFailure(document.status()));
        continueOpen(run);
        return;
    }
    auto* factory = process_utils::currentViewFactory();
    ImageViewControl* view = factory != nullptr ? factory->createNewImageViewControlFromImage(
                                                      nullptr, std::move(*document), nullptr,
                                                      "Image loaded from file '" + path.toUtf8().toStdString() + "'",
                                                      ViewControlMode::standard, run->neighbour.data(), false, false)
                                                : nullptr;
    if (view == nullptr)
    {
        // An open whose view could not be made stops here.
        failOpen(run, path, QStringLiteral("the file was read but no view could be made for it"));
        finishOpen(run);
        return;
    }
    viewMade(run, path, view);
}

/// Shows a sequence in a new view, on the GUI thread.
void showSequence(const std::shared_ptr<OpenRun>& run, const QString& path, ImageDocument&& document,
                  const bool autoDebayer)
{
    auto* factory = process_utils::currentViewFactory();
    ImageViewControl* view = factory != nullptr ? factory->createNewImageViewControlFromSequence(
                                                      nullptr, std::move(document), nullptr, autoDebayer,
                                                      "Sequence loaded from file '" + path.toUtf8().toStdString() + "'",
                                                      ViewControlMode::standard, run->neighbour.data(), false, false)
                                                : nullptr;
    if (view == nullptr)
    {
        failOpen(run, path, QStringLiteral("the sequence was read but no view could be made for it"));
        finishOpen(run);
        return;
    }
    viewMade(run, path, view);
}

/// Asks whether to demosaic a mosaic before it is shown, on the GUI thread.
bool askToDemosaic(const OpenRun& run)
{
    int answer = 0;
    process_utils::messageBoxYesNo(
        resolveOpenPromptParent(run.progress.data(), resolveDialogParent(run.dependencies)), "Open CFA file",
        "The file appears to have a bayer matrix. Do you want to automatically debayer it before opening? (If no, you "
        "can always debayer it by using the Debayer process)",
        false, &answer);
    return answer == QMessageBox::Yes;
}

/// Demosaics a mosaic on a worker by the open's demosaic method, then shows it.
void demosaicThenShow(const std::shared_ptr<OpenRun>& run, const QString& path, an::consumer::ImageAsset&& mosaic,
                      std::function<void(const std::shared_ptr<OpenRun>&, an::consumer::ImageAsset&&)> show)
{
    auto held = std::make_shared<an::consumer::ImageAsset>(std::move(mosaic));
    // The method that runs, which over an X-Trans mosaic is the chosen one's tabulated counterpart.
    const std::optional<an::ImageDescriptor> recorded = held->descriptor();
    const an::DemosaicMethod applied =
        recorded.has_value() ? demosaicMethodForMosaic(run->demosaic.method, recorded->cfa()) : run->demosaic.method;
    runThenPublish(
        run->dependencies.context,
        [session = run->session, held, method = run->demosaic.method, stop = run->stop]() {
            return demosaicViewImage(*session, *held, method, stop);
        },
        [run, path, applied, show = std::move(show)](an::Result<an::consumer::ImageAsset>&& colour) {
            if (!colour.hasValue())
            {
                if (colour.status().code() == an::ErrorCode::Cancelled)
                {
                    run->outcome.cancelled = true;
                    finishOpen(run);
                    return;
                }
                failOpen(run, path, describeFailure(colour.status()));
                continueOpen(run);
                return;
            }
            LOG_INFO("File '{}' demosaiced with {}", path.toStdString(), an::demosaicMethodName(applied));
            show(run, std::move(*colour));
        });
}

/// What the GUI thread does with what a worker read of one file.
void opened(const std::shared_ptr<OpenRun>& run, const QString& path, OpenedFile&& read)
{
    if (read.failure.failed())
    {
        if (read.failure.code() == an::ErrorCode::Cancelled || run->stop.stop_requested())
        {
            LOG_INFO("Loading file '{}' cancelled by the user", path.toStdString());
            run->outcome.cancelled = true;
            finishOpen(run);
            return;
        }
        failOpen(run, path, describeFailure(read.failure));
        continueOpen(run);
        return;
    }
    if (read.sequence.has_value())
    {
        ImageDocument document = std::move(*read.sequence);
        const std::optional<an::ImageDescriptor> frame = document.descriptor();
        if (!frame.has_value() || !frame->isMosaiced() || !askToDemosaic(*run))
        {
            showSequence(run, path, std::move(document), false);
            return;
        }
        // The first frame is shown demosaiced, and the player demosaics every frame it shows after it.
        an::Result<an::consumer::ImageAsset> copy = document.snapshot(run->stop);
        if (!copy.hasValue())
        {
            failOpen(run, path, describeFailure(copy.status()));
            continueOpen(run);
            return;
        }
        auto shared = std::make_shared<ImageDocument>(std::move(document));
        demosaicThenShow(run, path, std::move(*copy),
                         [path, shared](const std::shared_ptr<OpenRun>& current, an::consumer::ImageAsset&& colour) {
                             an::Result<ImageDocumentWriter> writer = shared->tryWrite();
                             if (writer.hasValue())
                             {
                                 const an::Status replaced = writer->replace(std::move(colour));
                                 writer->release();
                                 if (replaced.failed())
                                 {
                                     failOpen(current, path, describeFailure(replaced));
                                     continueOpen(current);
                                     return;
                                 }
                             }
                             showSequence(current, path, std::move(*shared), true);
                         });
        return;
    }
    ViewReadResult& image = *read.image;
    if (image.mosaic && askToDemosaic(*run))
    {
        demosaicThenShow(run, path, std::move(image.asset),
                         [path](const std::shared_ptr<OpenRun>& current, an::consumer::ImageAsset&& colour) {
                             showImage(current, path, std::move(colour));
                         });
        return;
    }
    showImage(run, path, std::move(image.asset));
}

/// Reads the next file on a worker, or ends the open.
void continueOpen(const std::shared_ptr<OpenRun>& run)
{
    if (run->stop.stop_requested())
    {
        run->outcome.cancelled = run->next < run->files.size() || run->outcome.cancelled;
        finishOpen(run);
        return;
    }
    if (run->next >= run->files.size())
    {
        finishOpen(run);
        return;
    }
    const QString path = run->files.at(run->next);
    ++run->next;
    const QString description = buildOpenProgressDescription(
        path.toUtf8().toStdString(), static_cast<std::size_t>(run->next), static_cast<std::size_t>(run->files.size()));
    LOG_INFO(description.toStdString());
    if (run->progress)
    {
        run->progress->setTextAsync(description);
        if (run->files.size() >= 3)
        {
            run->progress->setValueAsync(static_cast<int>((100 * (run->next - 1)) / run->files.size()));
        }
    }
    const std::string enginePath = path.toUtf8().toStdString();
    const std::optional<an::SequenceFormat> sequence = an::sequenceFormatForPath(enginePath);
    runThenPublish(
        run->dependencies.context,
        [session = run->session, policy = run->policy, path, sequence, stop = run->stop]() {
            OpenedFile read;
            if (sequence.has_value())
            {
                an::Result<ImageDocument> document = openSequenceDocument(*session, path, *sequence, stop);
                if (document.hasValue())
                {
                    read.sequence.emplace(std::move(*document));
                }
                else
                {
                    read.failure = document.status();
                }
                return read;
            }
            const ViewReadRequest request = policy ? policy->readRequest(path) : ViewReadRequest{.path = path};
            an::Result<ViewReadResult> image = readViewImage(*session, request, stop, nullptr);
            if (image.hasValue())
            {
                read.image.emplace(std::move(*image));
            }
            else
            {
                read.failure = image.status();
            }
            return read;
        },
        [run, path](OpenedFile&& read) { opened(run, path, std::move(read)); });
}

/// Starts an open of files on the GUI thread.
void startOpen(const FileWorkflowRunner::Dependencies& dependencies, QStringList files, QString directory,
               ProgressDialogControl* progress, const bool recent)
{
    auto run = std::make_shared<OpenRun>();
    run->dependencies = dependencies;
    if (run->dependencies.context == nullptr)
    {
        run->dependencies.context = process_utils::currentGuiThreadTarget();
    }
    run->files = std::move(files);
    run->directory = std::move(directory);
    run->recent = recent;
    run->progress = progress;
    run->stop = progress != nullptr ? progress->stopToken() : std::stop_token{};
    run->session = currentSession();
    // What the settings say now is what every file of this open is read under.
    run->policy = dependencies.rawOpenPolicy ? dependencies.rawOpenPolicy() : nullptr;
    run->demosaic = demosaicMethodFromSettingsKey(selectDemosaicSetting(dependencies));
    if (run->session == nullptr)
    {
        for (const QString& path : run->files)
        {
            failOpen(run, path, QStringLiteral("no engine session is available"));
        }
        finishOpen(run);
        return;
    }
    continueOpen(run);
}

} // namespace

void FileWorkflowRunner::openFiles(const Dependencies& dependencies, QStringList files, QString directory,
                                   ProgressDialogControl* progress)
{
    startOpen(dependencies, std::move(files), std::move(directory), progress, false);
}

void FileWorkflowRunner::openRecentFile(const Dependencies& dependencies, QString file, ProgressDialogControl* progress)
{
    startOpen(dependencies, QStringList{std::move(file)}, QString(), progress, true);
}

void FileWorkflowRunner::saveFile(const Dependencies& dependencies, ImageViewControl& view, QStringList files,
                                  QString directory, ProgressDialogControl* progress)
{
    QObject* context = dependencies.context != nullptr ? dependencies.context : process_utils::currentGuiThreadTarget();
    QPointer<ProgressDialogControl> progressGuard(progress);
    const auto finish = [dependencies, progressGuard, directory](const FileSaveOutcome& outcome) {
        if (outcome.written > 0u && dependencies.persistLastOpenDirectory)
        {
            dependencies.persistLastOpenDirectory(directory);
        }
        if (progressGuard)
        {
            progressGuard->closeAsync();
        }
        if (dependencies.saveFinished)
        {
            dependencies.saveFinished(outcome);
        }
    };
    const auto fail = [dependencies](const QString& path, const QString& description) {
        const QString text = QStringLiteral("Could NOT save file '%1'.\nError description: %2").arg(path, description);
        LOG_ERROR(text.toStdString());
        process_utils::notifyErrorMessageAsync(resolveDialogParent(dependencies), QStringLiteral("File save error"),
                                               text);
    };

    const an::consumer::EngineSession* session = currentSession();
    ImageDocument& document = view.document();
    const std::shared_ptr<const an::consumer::ImageAsset> asset = document.asset();
    if (session == nullptr || !document.valid() || asset == nullptr || files.isEmpty())
    {
        fail(files.isEmpty() ? QString() : files.front(), QStringLiteral("the view holds no image to write"));
        finish(FileSaveOutcome{.failed = true});
        return;
    }

    // What is written is decided now: the revision the document holds, its sequence, and the intent the export
    // settings give for each file. A setting or an edit after this reaches the next save.
    const DocumentRevision revision = document.revision();
    const std::optional<DocumentSequence> sequence = view.isSequence() ? document.sequence() : std::nullopt;
    if (view.isSequence() && !sequence.has_value())
    {
        fail(files.front(), QStringLiteral("the view states no sequence to write"));
        finish(FileSaveOutcome{.failed = true});
        return;
    }
    std::vector<std::pair<QString, std::optional<an::OutputIntent>>> writes;
    if (!sequence.has_value())
    {
        const std::optional<an::ImageDescriptor> described = document.descriptor();
        auto* application = an::ui::appContext();
        auto* intents = application != nullptr ? application->getOutputIntentProvider() : nullptr;
        for (const QString& path : files)
        {
            if (!described.has_value() || intents == nullptr)
            {
                fail(path, QStringLiteral("no export settings are available"));
                finish(FileSaveOutcome{.failed = true});
                return;
            }
            an::Result<an::OutputIntent> intent =
                intents->buildOutputIntent(*described, an::codecFormatFromPath(path.toUtf8().toStdString()));
            if (!intent.hasValue())
            {
                // Refused before any work, so nothing was written and nothing is left behind.
                fail(path, describeFailure(intent.status()));
                finish(FileSaveOutcome{.failed = true});
                return;
            }
            writes.emplace_back(path, std::move(*intent));
        }
    }
    else
    {
        for (const QString& path : files)
        {
            writes.emplace_back(path, std::nullopt);
        }
    }

    const QString name = QString::fromStdString(view.getUniqueName());
    if (progress != nullptr)
    {
        progress->setTextAsync(
            QStringLiteral("Saving %1 '%2' to file '%3'...")
                .arg(sequence.has_value() ? QStringLiteral("sequence") : QStringLiteral("image"), name, files.front()));
    }
    const std::stop_token stop = progress != nullptr ? progress->stopToken() : std::stop_token{};
    QPointer<ImageViewControl> viewGuard(&view);

    struct Written
    {
        std::size_t written = 0u;
        QString failedPath;
        an::Status failure = an::Status::success();
        qint64 elapsedMs = 0;
    };
    runThenPublish(
        context,
        [session, asset, sequence, writes, stop]() {
            Written result;
            QElapsedTimer timer;
            timer.start();
            for (const auto& [path, intent] : writes)
            {
                const an::Status status = intent.has_value() ? writeViewImage(*session, *asset, path, *intent, stop)
                                                             : writeSequenceFile(*session, *sequence, path, stop);
                if (status.failed())
                {
                    result.failedPath = path;
                    result.failure = status;
                    break;
                }
                ++result.written;
            }
            result.elapsedMs = timer.elapsed();
            return result;
        },
        [dependencies, viewGuard, revision, asset, writes, name, finish, fail](Written&& result) {
            FileSaveOutcome outcome;
            outcome.written = result.written;
            if (result.failure.failed())
            {
                outcome.failed = true;
                if (result.failure.code() == an::ErrorCode::Cancelled)
                {
                    LOG_INFO("Saving file '{}' cancelled by the user", result.failedPath.toStdString());
                }
                else
                {
                    fail(result.failedPath, describeFailure(result.failure));
                }
            }
            if (result.written > 0u)
            {
                const QString last = writes[result.written - 1u].first;
                LOG_INFO("'{}' saved to '{}' in {:.2f} seconds", name.toStdString(), last.toStdString(),
                         static_cast<double>(result.elapsedMs) / 1000.0);
                // The view is told only while it lives and still shows what was written; otherwise its changes stay
                // pending, because what it shows now is not in the file.
                if (viewGuard && viewGuard->document().revision() == revision)
                {
                    DocumentNames names = viewGuard->document().names();
                    names.sourcePath = last;
                    viewGuard->document().setNames(std::move(names));
                    viewGuard->notifyImageSaved();
                    outcome.viewMarkedSaved = true;
                }
                else
                {
                    LOG_INFO("'{}' changed or closed while it was written; its changes stay pending",
                             name.toStdString());
                }
                if (dependencies.addRecentFile)
                {
                    for (std::size_t index = 0u; index < result.written; ++index)
                    {
                        dependencies.addRecentFile(writes[index].first.toUtf8().toStdString(), asset);
                    }
                }
            }
            finish(outcome);
        });
}

} // namespace an::ui
