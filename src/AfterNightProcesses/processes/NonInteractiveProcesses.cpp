// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The non-interactive processes: the image menu's actions that run at once on the active view - over its image, or
/// over every frame of its sequence - and say why when they publish nothing.

#include "stdafx.hpp"

#include "processes/NonInteractiveProcesses.h"

#include "processes/ChannelActions.h"
#include "processes/ProcessCommonUtils.h"

#include "app/FileCompanions.h"
#include "controls/ImageViewCompanions.h"
#include "controls/ImageViewControl.h"
#include "controls/ProcessCompanions.h"
#include "controls/ProcessExecution.h"
#include "controls/ProgressDialogControl.h"
#include "controls/SequenceContainers.h"
#include "session/ExecutionScope.h"
#include "session/IEngineSessionProvider.h"
#include "session/ImageDocument.h"
#include "session/ImageReads.h"

#include <AfterNightConsumerRuntime/EngineSession.h>
#include <AfterNightConsumerRuntime/ImageAsset.h>

#include <AfterNightEngine/Algorithms/Geometry/Geometry.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/IO/SequenceDescriptor.h>

#include <AfterNightPlatform/Logging.h>

#include <QCoreApplication>
#include <QMessageBox>
#include <QPointer>
#include <QString>
#include <QThreadPool>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <stop_token>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace
{

using process_utils::ChannelAction;
using process_utils::ChannelActionKind;

[[nodiscard]] an::Status refusal(an::ErrorCode code, std::string message)
{
    return an::Status::failure(code, std::move(message));
}

// ---- Reporting.

/// Says why an action published nothing - in a message titled as the action and in the log - unless the user stopped
/// it, which is only logged. GUI thread.
void reportFailure(const QString& title, const std::string& what, const an::Status& status)
{
    if (status.code() == an::ErrorCode::Cancelled)
    {
        LOG_INFO("Stopped: {}", what);
        return;
    }
    const std::string reason = std::string(status.message());
    LOG_WARNING("Could not {}: {}", what, reason);
    process_utils::notifyErrorMessage(process_utils::currentMainWindow(), title,
                                      QString::fromStdString("Could not " + what + ": " + reason));
}

// ---- Running.

/// Runs work on a worker and its completion on the GUI thread. The completion runs whatever became of the work, and
/// is dropped only once the application is gone.
template <typename WorkT, typename DoneT> void runThenPublish(WorkT work, DoneT done)
{
    QThreadPool::globalInstance()->start([work = std::move(work), done = std::move(done)]() mutable {
        auto result = std::make_shared<std::invoke_result_t<WorkT&>>(work());
        QCoreApplication* application = QCoreApplication::instance();
        if (application == nullptr)
        {
            return;
        }
        QMetaObject::invokeMethod(
            application, [result, done = std::move(done)]() mutable { done(std::move(*result)); },
            Qt::QueuedConnection);
    });
}

/// The process's engine session, or null when the application lends none.
[[nodiscard]] const an::consumer::EngineSession* currentSession()
{
    if (an::ui::IEngineSessionProvider* provider = process_utils::currentEngineSessionProvider())
    {
        return &provider->engineSession();
    }
    return nullptr;
}

/// Admits an operation for a worker of one of these actions.
[[nodiscard]] an::Result<an::ui::ExecutionScope> admit(const an::consumer::EngineSession& session, std::stop_token stop,
                                                       an::ui::ExecutionObserver observer = {},
                                                       const an::ui::ImageReads* reads = nullptr)
{
    return an::ui::ExecutionScope::admit(session,
                                         an::ui::ExecutionRequest{.purpose = an::consumer::OperationPurpose::Apply,
                                                                  .stopToken = std::move(stop),
                                                                  .reads = reads},
                                         std::move(observer));
}

/// The images an action reads beside the view's own, to be leased before it is admitted.
[[nodiscard]] std::shared_ptr<an::ui::ImageReads>
readsOf(const std::vector<std::shared_ptr<const an::consumer::ImageAsset>>& images)
{
    auto reads = std::make_shared<an::ui::ImageReads>();
    for (const std::shared_ptr<const an::consumer::ImageAsset>& image : images)
    {
        reads->name(image);
    }
    return reads;
}

/// The progress window of a run over a sequence, shown; its Cancel stops the run.
[[nodiscard]] ProgressDialogControl* showProgress(const QString& title, const QString& text)
{
    auto* progress = new ProgressDialogControl(process_utils::currentMainWindow(), title);
    progress->setText(text);
    progress->setProgressBarRange(0, 100);
    progress->show();
    return progress;
}

/// Moves a progress window to a fraction of a run, from a worker.
void postProgress(const QPointer<ProgressDialogControl>& progress, int percent)
{
    QCoreApplication* application = QCoreApplication::instance();
    if (application == nullptr)
    {
        return;
    }
    QMetaObject::invokeMethod(
        application,
        [progress, percent]() {
            if (progress)
            {
                progress->setValue(percent);
            }
        },
        Qt::QueuedConnection);
}

/// What one frame of a sequence becomes: one image per output sequence.
using FrameOperation = std::function<an::Result<std::vector<an::consumer::ImageAsset>>(
    const an::ui::ExecutionScope& scope, const an::ui::ProcessSource& frame)>;

/// What a run over a sequence came to: one sequence per output, published whole at its path, and the frame each new
/// document shows - the one the source showed, when the run read it, and else the first.
struct SequenceRun
{
    an::Status status = an::Status::success();
    std::vector<an::ui::DocumentSequence> sequences;
    std::vector<an::consumer::ImageAsset> shown;
};

/// Reads every frame of a sequence, runs an operation over each and writes what it makes into new sequences, one per
/// path, each published whole or not at all. A worker.
[[nodiscard]] SequenceRun runOverFrames(const an::consumer::EngineSession& session,
                                        const an::ui::DocumentSequence& sequence, bool autoDebayer,
                                        const std::vector<QString>& paths, std::stop_token stop,
                                        const QPointer<ProgressDialogControl>& progress,
                                        const FrameOperation& operation, an::ui::ImageReads* reads = nullptr)
{
    SequenceRun run;
    // The images the operation reads beside the frames are leased before the run is admitted: an edit of a view holds
    // its writer while it waits to be admitted.
    if (reads != nullptr)
    {
        if (const an::Status taken = reads->take(stop); taken.failed())
        {
            reads->release();
            run.status = taken;
            return run;
        }
    }
    an::Result<an::ui::ExecutionScope> scope = admit(session, stop, {}, reads);
    if (!scope.hasValue())
    {
        if (reads != nullptr)
        {
            reads->release();
        }
        run.status = scope.status();
        return run;
    }
    const auto body = [&]() -> an::Status {
        an::Result<std::unique_ptr<an::ui::IProcessFrameReader>> reader =
            an::ui::openProcessFrames(session, sequence, autoDebayer);
        if (!reader.hasValue())
        {
            return reader.status();
        }
        std::vector<std::unique_ptr<an::ui::IProcessFrameWriter>> writers;
        for (const QString& path : paths)
        {
            an::Result<std::unique_ptr<an::ui::IProcessFrameWriter>> writer =
                an::ui::createProcessFrameOutput(session, sequence, path);
            if (!writer.hasValue())
            {
                return writer.status();
            }
            writers.push_back(std::move(*writer));
        }
        const std::uint64_t total = (*reader)->frameCount();
        std::uint64_t written = 0u;
        std::uint64_t shownWritten = 0u;
        for (;;)
        {
            if (stop.stop_requested())
            {
                return refusal(an::ErrorCode::Cancelled, "the run was stopped");
            }
            an::Result<std::optional<an::ui::ProcessSequenceFrame>> frame =
                (*reader)->next(scope->engine(), scope->execution());
            if (!frame.hasValue())
            {
                return frame.status();
            }
            if (!frame->has_value())
            {
                break;
            }
            an::Result<an::consumer::AssetReadLease> lease = (**frame).asset.read(stop);
            if (!lease.hasValue())
            {
                return lease.status();
            }
            an::Result<std::vector<an::consumer::ImageAsset>> made =
                operation(*scope, an::ui::ProcessSource::of(*lease));
            if (!made.hasValue())
            {
                return made.status();
            }
            if (made->size() != writers.size())
            {
                return refusal(an::ErrorCode::Internal, "a frame made another number of images than sequences");
            }
            for (std::size_t output = 0u; output < writers.size(); ++output)
            {
                an::Result<an::consumer::AssetReadLease> image = (*made)[output].read(stop);
                if (!image.hasValue())
                {
                    return image.status();
                }
                if (an::Status appended =
                        writers[output]->write(scope->engine(), scope->execution(), (**frame).index, image->image());
                    appended.failed())
                {
                    return appended;
                }
            }
            if (run.shown.empty() || (**frame).index == sequence.currentFrame)
            {
                run.shown = std::move(*made);
                shownWritten = written;
            }
            ++written;
            postProgress(progress, total > 0u ? static_cast<int>((written * 100u) / total) : 0);
        }
        if (written == 0u)
        {
            return refusal(an::ErrorCode::EmptySelection, "the sequence held no frame");
        }
        for (const std::unique_ptr<an::ui::IProcessFrameWriter>& writer : writers)
        {
            an::Result<an::ui::DocumentSequence> finished = writer->finish(scope->engine(), scope->execution());
            if (!finished.hasValue())
            {
                return finished.status();
            }
            finished->currentFrame = shownWritten;
            run.sequences.push_back(std::move(*finished));
        }
        return an::Status::success();
    };
    run.status = body();
    if (run.status.failed())
    {
        run.shown.clear();
    }
    scope->finish(run.status);
    if (reads != nullptr)
    {
        reads->release();
    }
    return run;
}

/// Removes the sequences a run published and no view shows.
void removeSequences(const std::vector<QString>& paths, std::size_t from)
{
    for (std::size_t index = from; index < paths.size(); ++index)
    {
        if (an::Status removed = an::ui::removeSequenceFromDisk(paths[index]); removed.failed())
        {
            LOG_WARNING("Could not remove the sequence '{}': {}", paths[index].toStdString(),
                        std::string(removed.message()));
        }
    }
}

/// New temporary paths in the container of a sequence, or nothing when the application lends no temporary files.
[[nodiscard]] std::optional<std::vector<QString>> temporaryPaths(an::SequenceFormat format, std::size_t count)
{
    std::vector<QString> paths;
    for (std::size_t index = 0u; index < count; ++index)
    {
        const std::string path = process_utils::createTempSequenceFilePath(format);
        if (path.empty())
        {
            return std::nullopt;
        }
        paths.push_back(QString::fromStdString(path));
    }
    return paths;
}

/// What a sequence container is called in a history step and a message.
[[nodiscard]] const char* containerName(an::SequenceFormat format) noexcept
{
    switch (format)
    {
    case an::SequenceFormat::Ser:
        return "SER";
    case an::SequenceFormat::ImageList:
        return "Image List";
    case an::SequenceFormat::Avi:
        return "AVI";
    case an::SequenceFormat::Mp4:
        return "MP4";
    case an::SequenceFormat::Fits:
        return "FITS";
    case an::SequenceFormat::Video:
        return "video";
    case an::SequenceFormat::Dummy:
        break;
    }
    return "generated";
}

// ---- The channel and conversion actions.

[[nodiscard]] bool isConversion(const ChannelAction& action) noexcept
{
    return action.kind == ChannelActionKind::Convert;
}

/// What an action's view i is called after the source's name, and what its history calls it.
[[nodiscard]] const char* outputSuffix(const ChannelAction& action, std::size_t index) noexcept
{
    return action.kind == ChannelActionKind::Extract ? action.outputs[index].suffix : action.suffix;
}

[[nodiscard]] std::string outputHistory(const ChannelAction& action, std::size_t index, const std::string& name,
                                        const char* of)
{
    if (isConversion(action))
    {
        return std::string("Converted to ") + action.label + " from " + of + " '" + name + "'";
    }
    const char* label = action.kind == ChannelActionKind::Extract ? action.outputs[index].label : action.label;
    return std::string(label) + " extracted from " + of + " '" + name + "'";
}

/// What a failure of an action says it could not do.
[[nodiscard]] std::string actionDescription(const ChannelAction& action, const std::string& name, const char* of)
{
    if (isConversion(action))
    {
        return std::string("convert the ") + of + " '" + name + "' to " + action.label;
    }
    std::string planes;
    if (action.kind == ChannelActionKind::Extract)
    {
        for (std::uint32_t index = 0u; index < action.outputCount; ++index)
        {
            planes += (index == 0u ? "" : index + 1u == action.outputCount ? " and " : ", ");
            planes += action.outputs[index].label;
        }
    }
    else
    {
        planes = action.label;
    }
    return "extract " + planes + " from the " + of + " '" + name + "'";
}

[[nodiscard]] QString actionTitle(const ChannelAction& action, bool sequence)
{
    if (isConversion(action))
    {
        return sequence ? QStringLiteral("Convert sequence format") : QStringLiteral("Convert format");
    }
    return sequence ? QStringLiteral("Extract sequence channel") : QStringLiteral("Extract channel");
}

void runChannelActionOverImage(ImageViewControl* control, const ChannelAction& action,
                               const an::consumer::EngineSession& session)
{
    const QString title = actionTitle(action, false);
    const std::string name = control->getUniqueName();
    std::shared_ptr<const an::consumer::ImageAsset> asset = control->document().asset();
    if (asset == nullptr)
    {
        reportFailure(title, actionDescription(action, name, "image"),
                      refusal(an::ErrorCode::NotInitialized, "the view shows no image"));
        return;
    }
    const ChannelAction* row = &action;
    QPointer<ImageViewControl> guard(control);
    runThenPublish(
        [session = &session, asset, row]() -> an::Result<std::vector<an::consumer::ImageAsset>> {
            // The image is leased before the run is admitted: an edit of the view holds the writer while it waits to
            // be admitted.
            an::Result<an::consumer::AssetReadLease> lease = asset->read();
            if (!lease.hasValue())
            {
                return lease.status();
            }
            an::Result<an::ui::ExecutionScope> scope = admit(*session, {});
            if (!scope.hasValue())
            {
                return scope.status();
            }
            an::Result<std::vector<an::consumer::ImageAsset>> made =
                process_utils::runChannelAction(*scope, an::ui::ProcessSource::of(*lease), *row);
            lease->release();
            scope->finish(made.hasValue() ? an::Status::success() : made.status());
            return made;
        },
        [guard, name, row, title](an::Result<std::vector<an::consumer::ImageAsset>> made) {
            if (!made.hasValue())
            {
                reportFailure(title, actionDescription(*row, name, "image"), made.status());
                return;
            }
            an::ui::IViewFactory* factory = process_utils::currentViewFactory();
            for (std::size_t index = 0u; index < made->size(); ++index)
            {
                const std::string viewName = name + outputSuffix(*row, index);
                an::Result<an::ui::ImageDocument> document = an::ui::ImageDocument::create(
                    std::move((*made)[index]), an::ui::DocumentNames{.displayName = QString::fromStdString(viewName)});
                if (!document.hasValue())
                {
                    reportFailure(title, actionDescription(*row, name, "image"), document.status());
                    return;
                }
                ImageViewControl* view = factory != nullptr ? factory->createNewImageViewControlFromImage(
                                                                  nullptr, std::move(*document), nullptr,
                                                                  outputHistory(*row, index, name, "image"),
                                                                  ViewControlMode::standard, guard.data(), false, true)
                                                            : nullptr;
                if (view == nullptr)
                {
                    reportFailure(title, actionDescription(*row, name, "image"),
                                  refusal(an::ErrorCode::NotInitialized, "no view could be opened for " + viewName));
                    return;
                }
                LOG_INFO("{} into new image '{}'", outputHistory(*row, index, name, "image"), view->getUniqueName());
            }
        });
}

/// Whether a sequence container stores samples of a representation as they are: SER eight or sixteen unsigned bits, a
/// video eight, and an image list or a FITS container any.
[[nodiscard]] bool containerStores(an::SequenceFormat format, an::SampleType type) noexcept
{
    switch (format)
    {
    case an::SequenceFormat::Ser:
        return type == an::SampleType::UInt8 || type == an::SampleType::UInt16;
    case an::SequenceFormat::Avi:
    case an::SequenceFormat::Mp4:
    case an::SequenceFormat::Video:
        return type == an::SampleType::UInt8;
    default:
        break;
    }
    return true;
}

void runChannelActionOverSequence(ImageViewControl* control, const ChannelAction& action,
                                  const an::consumer::EngineSession& session)
{
    const QString title = actionTitle(action, true);
    const std::string name = control->getUniqueName();
    if (const std::optional<an::ui::DocumentSequence> shown = control->document().sequence();
        isConversion(action) && shown.has_value() && !containerStores(shown->descriptor.format, action.target))
    {
        // The new sequence is written in the source's container, which would store the samples converted back.
        reportFailure(title, actionDescription(action, name, "sequence"),
                      refusal(an::ErrorCode::Unsupported, std::string("a ") + containerName(shown->descriptor.format) +
                                                              " sequence cannot hold " + action.label +
                                                              "; convert it to an image list first"));
        return;
    }
    if (!control->tryRequestLockForProcessing())
    {
        control->showViewControlBusyWarning(isConversion(action) ? QStringLiteral("Convert Format")
                                                                 : QStringLiteral("Extract Channel"));
        return;
    }
    const std::optional<an::ui::DocumentSequence> sequence = control->document().sequence();
    const std::optional<std::vector<QString>> paths =
        sequence.has_value()
            ? temporaryPaths(sequence->descriptor.format, process_utils::channelActionImageCount(action))
            : std::nullopt;
    if (!sequence.has_value() || !paths.has_value())
    {
        control->releaseLockForProcessing();
        reportFailure(title, actionDescription(action, name, "sequence"),
                      refusal(an::ErrorCode::NotInitialized, sequence.has_value()
                                                                 ? "the application lends no temporary files"
                                                                 : "the view shows no sequence"));
        return;
    }
    ProgressDialogControl* progress =
        showProgress(title, QString::fromStdString(std::string(action.progressBefore) + name + action.progressAfter));
    const QPointer<ProgressDialogControl> progressGuard(progress);
    // A conversion keeps a mosaic a mosaic; an extraction reads the colour a mosaic is demosaiced to, whatever the
    // view shows.
    const bool autoDebayer = !isConversion(action);
    const ChannelAction* row = &action;
    QPointer<ImageViewControl> guard(control);
    runThenPublish(
        [session = &session, sequence = *sequence, paths = *paths, row, stop = progress->stopToken(), progressGuard,
         autoDebayer]() {
            return runOverFrames(*session, sequence, autoDebayer, paths, stop, progressGuard,
                                 [row](const an::ui::ExecutionScope& scope, const an::ui::ProcessSource& frame) {
                                     return process_utils::runChannelAction(scope, frame, *row);
                                 });
        },
        [guard, name, row, title, paths = *paths, progressGuard](SequenceRun run) {
            if (guard)
            {
                guard->releaseLockForProcessing();
            }
            if (progressGuard)
            {
                progressGuard->closeAsync();
            }
            if (run.status.failed())
            {
                removeSequences(paths, 0u);
                reportFailure(title, actionDescription(*row, name, "sequence"), run.status);
                return;
            }
            an::ui::IViewFactory* factory = process_utils::currentViewFactory();
            for (std::size_t index = 0u; index < run.sequences.size(); ++index)
            {
                const std::string viewName = name + outputSuffix(*row, index);
                an::Result<an::ui::ImageDocument> document = an::ui::ImageDocument::create(
                    std::move(run.shown[index]),
                    an::ui::DocumentNames{.displayName = QString::fromStdString(viewName), .sourcePath = paths[index]});
                ImageViewControl* view = nullptr;
                if (document.hasValue())
                {
                    document->setSequence(run.sequences[index]);
                    view = factory != nullptr ? factory->createNewImageViewControlFromSequence(
                                                    nullptr, std::move(*document), nullptr, true,
                                                    outputHistory(*row, index, name, "sequence"),
                                                    ViewControlMode::standard, guard.data(), false, true)
                                              : nullptr;
                }
                if (view == nullptr)
                {
                    removeSequences(paths, index);
                    reportFailure(title, actionDescription(*row, name, "sequence"),
                                  document.hasValue() ? refusal(an::ErrorCode::NotInitialized,
                                                                "no view could be opened for " + viewName)
                                                      : document.status());
                    return;
                }
                LOG_INFO("{} into new sequence '{}'", outputHistory(*row, index, name, "sequence"),
                         view->getUniqueName());
            }
        });
}

void launchChannelAction(ImageViewControl* control, const char* member)
{
    const ChannelAction* action = process_utils::channelActionOfMember(member);
    if (control == nullptr || action == nullptr)
    {
        return;
    }
    NonInteractiveProcesses::runChannelAction(control, *action);
}

// ---- The in-place actions.

/// Runs an in-place operation over a still image as an edit of its view: computed on a worker under the document's
/// writer, recorded in the history before the view shows it, and reported when it is not shown.
void editInPlace(
    ImageViewControl* control, const QString& title, const std::string& what, std::string stepName,
    std::function<an::Result<an::consumer::ImageAsset>(const an::ui::ExecutionScope&, const an::ui::ProcessSource&)>
        operation,
    bool updateActions, const std::vector<std::shared_ptr<const an::consumer::ImageAsset>>& images)
{
    auto failure = std::make_shared<std::optional<an::Status>>();
    const std::string name = control->getUniqueName();
    // The images the operation reads beside the view's own are leased by the edit, with the view's writer and in one
    // order, before the operation asks to be admitted.
    std::shared_ptr<an::ui::ImageReads> reads = readsOf(images);
    control->editImage(
        stepName, std::string(),
        [operation, failure, reads](const an::consumer::EngineSession& session,
                                    const an::consumer::AssetWriteLease& image,
                                    std::stop_token stop) -> an::Result<an::consumer::ImageAsset> {
            an::Result<an::ui::ExecutionScope> scope = admit(session, std::move(stop), {}, reads.get());
            if (!scope.hasValue())
            {
                *failure = scope.status();
                return scope.status();
            }
            an::Result<an::consumer::ImageAsset> made = operation(*scope, an::ui::ProcessSource::of(image));
            scope->finish(made.hasValue() ? an::Status::success() : made.status());
            if (!made.hasValue())
            {
                *failure = made.status();
            }
            return made;
        },
        [failure, title, what, name, stepName, updateActions](bool shown) {
            if (shown)
            {
                LOG_INFO("{}: image '{}'", stepName, name);
                if (updateActions)
                {
                    process_utils::updateActiveImageActionsAsync();
                }
                return;
            }
            if (failure->has_value())
            {
                reportFailure(title, what, **failure);
            }
        },
        reads);
}

/// Runs an in-place operation over every frame of a sequence and shows the sequence it makes in the view, as a step of
/// its history.
void replaceSequence(
    ImageViewControl* control, const an::consumer::EngineSession& session, const QString& title,
    const QString& busyTitle, const QString& progressText, const std::string& what, std::string stepName,
    std::string stepDescription, bool autoDebayer,
    std::function<an::Result<an::consumer::ImageAsset>(const an::ui::ExecutionScope&, const an::ui::ProcessSource&)>
        operation,
    bool updateActions, const std::vector<std::shared_ptr<const an::consumer::ImageAsset>>& images)
{
    if (!control->tryRequestLockForProcessing())
    {
        control->showViewControlBusyWarning(busyTitle);
        return;
    }
    const std::optional<an::ui::DocumentSequence> sequence = control->document().sequence();
    const std::optional<std::vector<QString>> paths =
        sequence.has_value() ? temporaryPaths(sequence->descriptor.format, 1u) : std::nullopt;
    if (!sequence.has_value() || !paths.has_value())
    {
        control->releaseLockForProcessing();
        reportFailure(title, what,
                      refusal(an::ErrorCode::NotInitialized, sequence.has_value()
                                                                 ? "the application lends no temporary files"
                                                                 : "the view shows no sequence"));
        return;
    }
    ProgressDialogControl* progress = showProgress(title, progressText);
    const QPointer<ProgressDialogControl> progressGuard(progress);
    QPointer<ImageViewControl> guard(control);
    runThenPublish(
        [session = &session, sequence = *sequence, paths = *paths, stop = progress->stopToken(), progressGuard,
         autoDebayer, operation, reads = readsOf(images)]() {
            return runOverFrames(
                *session, sequence, autoDebayer, paths, stop, progressGuard,
                [&operation](const an::ui::ExecutionScope& scope,
                             const an::ui::ProcessSource& frame) -> an::Result<std::vector<an::consumer::ImageAsset>> {
                    an::Result<an::consumer::ImageAsset> made = operation(scope, frame);
                    if (!made.hasValue())
                    {
                        return made.status();
                    }
                    std::vector<an::consumer::ImageAsset> one;
                    one.push_back(std::move(*made));
                    return one;
                },
                reads.get());
        },
        [guard, title, what, stepName = std::move(stepName), stepDescription = std::move(stepDescription),
         paths = *paths, progressGuard, updateActions](SequenceRun run) {
            if (guard)
            {
                guard->releaseLockForProcessing();
            }
            if (progressGuard)
            {
                progressGuard->closeAsync();
            }
            if (run.status.ok() && !guard)
            {
                run.status = refusal(an::ErrorCode::InvalidState, "the view closed before the sequence was made");
            }
            if (run.status.failed())
            {
                removeSequences(paths, 0u);
                reportFailure(title, what, run.status);
                return;
            }
            an::ui::DocumentNames names = guard->document().names();
            names.sourcePath = paths.front();
            an::Result<an::ui::ImageDocument> document =
                an::ui::ImageDocument::create(std::move(run.shown.front()), std::move(names));
            if (!document.hasValue())
            {
                removeSequences(paths, 0u);
                reportFailure(title, what, document.status());
                return;
            }
            document->setSequence(run.sequences.front());
            guard->setSequenceDocument(std::move(*document), guard->isSequenceAutodebayerEnabled());
            if (an::ui::IViewHistory* history = guard->viewHistory())
            {
                history->registerShownState(stepName, stepDescription);
            }
            if (updateActions)
            {
                process_utils::updateActiveImageActionsAsync();
            }
            LOG_INFO("{}: sequence '{}'", stepName, guard->getUniqueName());
        });
}

/// The mask a view applies to a process's result: the mask view's image when the view has an enabled mask, and none
/// otherwise. GUI thread.
struct ViewMask
{
    std::shared_ptr<const an::consumer::ImageAsset> asset;
    bool inverted = false;
};

[[nodiscard]] ViewMask viewMaskOf(ImageViewControl* control)
{
    ViewMask mask;
    if (control->isMasked() && control->isMaskEnabled())
    {
        if (ImageViewControl* maskView = control->getCurrentMask())
        {
            mask.asset = maskView->document().asset();
            mask.inverted = control->isMaskInverted();
        }
    }
    return mask;
}

/// The negative, weighed by a view's mask, read under its own lease for the call.
[[nodiscard]] an::Result<an::consumer::ImageAsset>
maskedNegative(const an::ui::ExecutionScope& scope, const an::ui::ProcessSource& source, const ViewMask& mask)
{
    an::ui::ProcessBlendRequest blend;
    std::optional<an::consumer::AssetReadLease> maskLease;
    if (mask.asset != nullptr)
    {
        an::Result<an::consumer::AssetReadLease> read =
            an::ui::readBeside(scope, *mask.asset, scope.execution().stopToken());
        if (!read.hasValue())
        {
            return read.status();
        }
        maskLease.emplace(std::move(*read));
        blend.mask = &maskLease->image();
        blend.invertMask = mask.inverted;
    }
    return process_utils::negativeImageOf(scope, source, blend);
}

/// The turn or mirror a still view makes itself, over every frame of a sequence.
void orientSequence(ImageViewControl* control, an::ImageOrientation orientation, const QString& title,
                    const QString& progressText, std::string stepName, std::string stepDescription)
{
    const an::consumer::EngineSession* session = currentSession();
    if (session == nullptr)
    {
        reportFailure(title, "turn the sequence '" + control->getUniqueName() + "'",
                      refusal(an::ErrorCode::NotInitialized, "no engine session is lent"));
        return;
    }
    replaceSequence(control, *session, title, QStringLiteral("Rotate Sequence"), progressText,
                    "turn the sequence '" + control->getUniqueName() + "'", std::move(stepName),
                    std::move(stepDescription), false,
                    [orientation](const an::ui::ExecutionScope& scope, const an::ui::ProcessSource& frame) {
                        return process_utils::orientedImageOf(scope, frame, orientation);
                    },
                    false, {});
}

/// Writes a sequence view's frames into another container and shows that sequence in the view.
void convertContainer(ImageViewControl* control, an::SequenceFormat target, const QString& progressText)
{
    const QString title = QStringLiteral("Convert sequence type");
    const std::string name = control->getUniqueName();
    const std::string what = std::string("convert the sequence '") + name + "' to " + containerName(target);
    const an::consumer::EngineSession* session = currentSession();
    if (session == nullptr)
    {
        reportFailure(title, what, refusal(an::ErrorCode::NotInitialized, "no engine session is lent"));
        return;
    }
    if (!control->tryRequestLockForProcessing())
    {
        control->showViewControlBusyWarning(QStringLiteral("Convert Sequence"));
        return;
    }
    const std::optional<an::ui::DocumentSequence> sequence = control->document().sequence();
    const std::optional<std::vector<QString>> paths = sequence.has_value() ? temporaryPaths(target, 1u) : std::nullopt;
    if (!sequence.has_value() || !paths.has_value())
    {
        control->releaseLockForProcessing();
        reportFailure(title, what,
                      refusal(an::ErrorCode::NotInitialized, sequence.has_value()
                                                                 ? "the application lends no temporary files"
                                                                 : "the view shows no sequence"));
        return;
    }
    const std::string description = std::string("Sequence converted from ") +
                                    containerName(sequence->descriptor.format) + " to " + containerName(target) +
                                    " format";
    ProgressDialogControl* progress = showProgress(title, progressText);
    const QPointer<ProgressDialogControl> progressGuard(progress);
    QPointer<ImageViewControl> guard(control);
    runThenPublish(
        [session, sequence = *sequence, path = paths->front(), target,
         stop = progress->stopToken()]() -> an::Result<an::ui::ImageDocument> {
            // Every frame as the file holds it: a mosaic stays a mosaic, whatever the view shows.
            if (an::Status written = an::ui::writeSequenceFile(*session, sequence, path, stop); written.failed())
            {
                return written;
            }
            return an::ui::openSequenceDocument(*session, path, target, stop);
        },
        [guard, title, what, description, path = paths->front(),
         progressGuard](an::Result<an::ui::ImageDocument> document) {
            if (guard)
            {
                guard->releaseLockForProcessing();
            }
            if (progressGuard)
            {
                progressGuard->closeAsync();
            }
            if (document.hasValue() && !guard)
            {
                document = refusal(an::ErrorCode::InvalidState, "the view closed before the sequence was written");
            }
            if (!document.hasValue())
            {
                removeSequences({path}, 0u);
                reportFailure(title, what, document.status());
                return;
            }
            an::ui::DocumentNames names = guard->document().names();
            names.sourcePath = path;
            document->setNames(std::move(names));
            guard->setSequenceDocument(std::move(*document), guard->isSequenceAutodebayerEnabled());
            if (an::ui::IViewHistory* history = guard->viewHistory())
            {
                history->registerShownState("Convert Sequence Type", description);
            }
            LOG_INFO("{}: sequence '{}'", description, guard->getUniqueName());
        });
}

/// Runs an in-place operation over the view's image or its sequence.
void runInPlace(
    ImageViewControl* control, const QString& title, const QString& busyTitle, const QString& progressText,
    const std::string& what, const std::string& stepName, bool autoDebayer,
    std::function<an::Result<an::consumer::ImageAsset>(const an::ui::ExecutionScope&, const an::ui::ProcessSource&)>
        operation,
    bool updateActions, const std::vector<std::shared_ptr<const an::consumer::ImageAsset>>& images = {})
{
    if (!control->isSequence())
    {
        editInPlace(control, title, what, stepName, std::move(operation), updateActions, images);
        return;
    }
    const an::consumer::EngineSession* session = currentSession();
    if (session == nullptr)
    {
        reportFailure(title, what, refusal(an::ErrorCode::NotInitialized, "no engine session is lent"));
        return;
    }
    replaceSequence(control, *session, title, busyTitle, progressText, what, stepName, std::string(), autoDebayer,
                    std::move(operation), updateActions, images);
}

} // namespace

// ---- Duplicating and extracting a frame.

void NonInteractiveProcesses::duplicateImage(ImageViewControl* control)
{
    const QString title = QStringLiteral("Duplicate");
    const std::string name = control->getUniqueName();
    const an::consumer::EngineSession* session = currentSession();
    const bool pendingChanges = control->hasPendingChanges();
    QPointer<ImageViewControl> guard(control);
    if (control->isSequence())
    {
        const std::optional<an::ui::DocumentSequence> sequence = control->document().sequence();
        const std::string what = "duplicate the sequence '" + name + "'";
        if (session == nullptr || !sequence.has_value())
        {
            reportFailure(title, what,
                          refusal(an::ErrorCode::NotInitialized,
                                  session == nullptr ? "no engine session is lent" : "the view shows no sequence"));
            return;
        }
        const bool autoDebayer = control->isSequenceAutodebayerEnabled();
        runThenPublish(
            [session, sequence = *sequence]() {
                return an::ui::openSequenceDocument(*session, sequence.sourcePath, sequence.descriptor.format, {});
            },
            [guard, title, what, name, autoDebayer, pendingChanges,
             path = sequence->sourcePath](an::Result<an::ui::ImageDocument> document) {
                if (!document.hasValue())
                {
                    reportFailure(title, what, document.status());
                    return;
                }
                document->setNames(
                    an::ui::DocumentNames{.displayName = QString::fromStdString(name), .sourcePath = path});
                an::ui::IViewFactory* factory = process_utils::currentViewFactory();
                ImageViewControl* view =
                    factory != nullptr
                        ? factory->createNewImageViewControlFromSequence(
                              nullptr, std::move(*document), nullptr, autoDebayer, "Cloned from sequence " + name,
                              ViewControlMode::standard, guard.data(), false, pendingChanges)
                        : nullptr;
                if (view == nullptr)
                {
                    reportFailure(title, what, refusal(an::ErrorCode::NotInitialized, "no view could be opened"));
                    return;
                }
                LOG_INFO("Sequence '{}' cloned into sequence '{}'", name, view->getUniqueName());
            });
        return;
    }
    const std::string what = "duplicate the image '" + name + "'";
    std::shared_ptr<const an::consumer::ImageAsset> asset = control->document().asset();
    if (asset == nullptr)
    {
        reportFailure(title, what, refusal(an::ErrorCode::NotInitialized, "the view shows no image"));
        return;
    }
    runThenPublish(
        [asset]() { return asset->snapshot(); },
        [guard, title, what, name, pendingChanges](an::Result<an::consumer::ImageAsset> copy) {
            if (!copy.hasValue())
            {
                reportFailure(title, what, copy.status());
                return;
            }
            // The copy has never been a file, so saving it cannot overwrite the original's.
            an::Result<an::ui::ImageDocument> document = an::ui::ImageDocument::create(
                std::move(*copy), an::ui::DocumentNames{.displayName = QString::fromStdString(name)});
            an::ui::IViewFactory* factory = process_utils::currentViewFactory();
            ImageViewControl* view = document.hasValue() && factory != nullptr
                                         ? factory->createNewImageViewControlFromImage(
                                               nullptr, std::move(*document), nullptr, "Cloned from image " + name,
                                               ViewControlMode::standard, guard.data(), false, pendingChanges)
                                         : nullptr;
            if (view == nullptr)
            {
                reportFailure(title, what,
                              document.hasValue() ? refusal(an::ErrorCode::NotInitialized, "no view could be opened")
                                                  : document.status());
                return;
            }
            LOG_INFO("Image '{}' cloned into image '{}'", name, view->getUniqueName());
        });
}

void NonInteractiveProcesses::extractFrame(ImageViewControl* control)
{
    const QString title = QStringLiteral("Extract Frame");
    const std::string name = control->getUniqueName();
    if (!control->isSequence())
    {
        process_utils::notifyErrorMessage(nullptr, title,
                                          QStringLiteral("Could NOT extract frame from image %1 because it is not a "
                                                         "sequence")
                                              .arg(QString::fromStdString(name)));
        return;
    }
    const std::size_t frame = control->getSequenceCurrentFrame();
    const std::string what = "extract frame " + std::to_string(frame) + " from the sequence '" + name + "'";
    std::shared_ptr<const an::consumer::ImageAsset> asset = control->document().asset();
    if (asset == nullptr)
    {
        reportFailure(title, what, refusal(an::ErrorCode::NotInitialized, "the view shows no frame"));
        return;
    }
    QPointer<ImageViewControl> guard(control);
    runThenPublish(
        [asset]() { return asset->snapshot(); },
        [guard, title, what, name, frame](an::Result<an::consumer::ImageAsset> copy) {
            if (!copy.hasValue())
            {
                reportFailure(title, what, copy.status());
                return;
            }
            const std::string viewName = name + " - FRAME_" + std::to_string(frame);
            const std::string history = "Frame " + std::to_string(frame) + " extracted from sequence '" + name + "'";
            an::Result<an::ui::ImageDocument> document = an::ui::ImageDocument::create(
                std::move(*copy), an::ui::DocumentNames{.displayName = QString::fromStdString(viewName)});
            an::ui::IViewFactory* factory = process_utils::currentViewFactory();
            ImageViewControl* view =
                document.hasValue() && factory != nullptr
                    ? factory->createNewImageViewControlFromImage(nullptr, std::move(*document), nullptr, history,
                                                                  ViewControlMode::standard, guard.data(), false, true)
                    : nullptr;
            if (view == nullptr)
            {
                reportFailure(title, what,
                              document.hasValue() ? refusal(an::ErrorCode::NotInitialized, "no view could be opened")
                                                  : document.status());
                return;
            }
            LOG_INFO("{} into new image '{}'", history, view->getUniqueName());
        });
}

// ---- The channel and conversion actions.

void NonInteractiveProcesses::runChannelAction(ImageViewControl* control, const process_utils::ChannelAction& action)
{
    const an::consumer::EngineSession* session = currentSession();
    if (session == nullptr)
    {
        reportFailure(actionTitle(action, control->isSequence()),
                      actionDescription(action, control->getUniqueName(), control->isSequence() ? "sequence" : "image"),
                      refusal(an::ErrorCode::NotInitialized, "no engine session is lent"));
        return;
    }
    if (control->isSequence())
    {
        runChannelActionOverSequence(control, action, *session);
        return;
    }
    runChannelActionOverImage(control, action, *session);
}

void NonInteractiveProcesses::extractRedChannel(ImageViewControl* control)
{
    launchChannelAction(control, "extractRedChannel");
}

void NonInteractiveProcesses::extractGreenChannel(ImageViewControl* control)
{
    launchChannelAction(control, "extractGreenChannel");
}

void NonInteractiveProcesses::extractBlueChannel(ImageViewControl* control)
{
    launchChannelAction(control, "extractBlueChannel");
}

void NonInteractiveProcesses::extractRGBChannels(ImageViewControl* control)
{
    launchChannelAction(control, "extractRGBChannels");
}

void NonInteractiveProcesses::extractCIEL(ImageViewControl* control)
{
    launchChannelAction(control, "extractCIEL");
}

void NonInteractiveProcesses::extractCIEA(ImageViewControl* control)
{
    launchChannelAction(control, "extractCIEA");
}

void NonInteractiveProcesses::extractCIEB(ImageViewControl* control)
{
    launchChannelAction(control, "extractCIEB");
}

void NonInteractiveProcesses::extractCIEAB(ImageViewControl* control)
{
    launchChannelAction(control, "extractCIEAB");
}

void NonInteractiveProcesses::extractCIELAB(ImageViewControl* control)
{
    launchChannelAction(control, "extractCIELAB");
}

void NonInteractiveProcesses::extractCIEC(ImageViewControl* control)
{
    launchChannelAction(control, "extractCIEC");
}

void NonInteractiveProcesses::extractCIEH(ImageViewControl* control)
{
    launchChannelAction(control, "extractCIEH");
}

void NonInteractiveProcesses::extractCIELCH(ImageViewControl* control)
{
    launchChannelAction(control, "extractCIELCH");
}

void NonInteractiveProcesses::extractCIEX(ImageViewControl* control)
{
    launchChannelAction(control, "extractCIEX");
}

void NonInteractiveProcesses::extractCIEY(ImageViewControl* control)
{
    launchChannelAction(control, "extractCIEY");
}

void NonInteractiveProcesses::extractCIEZ(ImageViewControl* control)
{
    launchChannelAction(control, "extractCIEZ");
}

void NonInteractiveProcesses::extractCIEXYZ(ImageViewControl* control)
{
    launchChannelAction(control, "extractCIEXYZ");
}

void NonInteractiveProcesses::extractHSL_H(ImageViewControl* control)
{
    launchChannelAction(control, "extractHSL_H");
}

void NonInteractiveProcesses::extractHSL_S(ImageViewControl* control)
{
    launchChannelAction(control, "extractHSL_S");
}

void NonInteractiveProcesses::extractHSL_L(ImageViewControl* control)
{
    launchChannelAction(control, "extractHSL_L");
}

void NonInteractiveProcesses::extractHSL(ImageViewControl* control)
{
    launchChannelAction(control, "extractHSL");
}

void NonInteractiveProcesses::extractHSV_H(ImageViewControl* control)
{
    launchChannelAction(control, "extractHSV_H");
}

void NonInteractiveProcesses::extractHSV_S(ImageViewControl* control)
{
    launchChannelAction(control, "extractHSV_S");
}

void NonInteractiveProcesses::extractHSV_V(ImageViewControl* control)
{
    launchChannelAction(control, "extractHSV_V");
}

void NonInteractiveProcesses::extractHSV(ImageViewControl* control)
{
    launchChannelAction(control, "extractHSV");
}

void NonInteractiveProcesses::extractHSI_H(ImageViewControl* control)
{
    launchChannelAction(control, "extractHSI_H");
}

void NonInteractiveProcesses::extractHSI_S(ImageViewControl* control)
{
    launchChannelAction(control, "extractHSI_S");
}

void NonInteractiveProcesses::extractHSI_I(ImageViewControl* control)
{
    launchChannelAction(control, "extractHSI_I");
}

void NonInteractiveProcesses::extractHSI(ImageViewControl* control)
{
    launchChannelAction(control, "extractHSI");
}

void NonInteractiveProcesses::extractYCbCr_Y(ImageViewControl* control)
{
    launchChannelAction(control, "extractYCbCr_Y");
}

void NonInteractiveProcesses::extractYCbCr_Cb(ImageViewControl* control)
{
    launchChannelAction(control, "extractYCbCr_Cb");
}

void NonInteractiveProcesses::extractYCbCr_Cr(ImageViewControl* control)
{
    launchChannelAction(control, "extractYCbCr_Cr");
}

void NonInteractiveProcesses::extractYCbCr(ImageViewControl* control)
{
    launchChannelAction(control, "extractYCbCr");
}

void NonInteractiveProcesses::convertTo8Bit(ImageViewControl* control)
{
    launchChannelAction(control, "convertTo8Bit");
}

void NonInteractiveProcesses::convertToSigned16Bit(ImageViewControl* control)
{
    launchChannelAction(control, "convertToSigned16Bit");
}

void NonInteractiveProcesses::convertToUnsigned16Bit(ImageViewControl* control)
{
    launchChannelAction(control, "convertToUnsigned16Bit");
}

void NonInteractiveProcesses::convertToSigned32Bit(ImageViewControl* control)
{
    launchChannelAction(control, "convertToSigned32Bit");
}

void NonInteractiveProcesses::convertToUnsigned32Bit(ImageViewControl* control)
{
    launchChannelAction(control, "convertToUnsigned32Bit");
}

void NonInteractiveProcesses::convertToFloat32(ImageViewControl* control)
{
    launchChannelAction(control, "convertToFloat32");
}

void NonInteractiveProcesses::convertToFloat64(ImageViewControl* control)
{
    launchChannelAction(control, "convertToFloat64");
}

// ---- Sequence containers.

void NonInteractiveProcesses::convertToSERSequence(ImageViewControl* control)
{
    convertContainer(control, an::SequenceFormat::Ser,
                     QString::fromStdString("Converting sequence " + control->getUniqueName() + " to SER format"));
}

void NonInteractiveProcesses::convertToImageListSequence(ImageViewControl* control)
{
    convertContainer(
        control, an::SequenceFormat::ImageList,
        QString::fromStdString("Converting sequence " + control->getUniqueName() + " to image list format"));
}

// ---- Mirrors and turns: a still image is the view's own edit, a sequence every frame turned the same way.

void NonInteractiveProcesses::horizontalMirror(ImageViewControl* control)
{
    if (!control->isSequence())
    {
        control->horizontalMirror();
        return;
    }
    orientSequence(control, an::ImageOrientation::MirrorHorizontal, QStringLiteral("Flip sequence"),
                   QString::fromStdString("Horizontally flipping sequence " + control->getUniqueName()),
                   "Sequence Horizontal Mirror", std::string());
}

void NonInteractiveProcesses::verticalMirror(ImageViewControl* control)
{
    if (!control->isSequence())
    {
        control->verticalMirror();
        return;
    }
    orientSequence(control, an::ImageOrientation::MirrorVertical, QStringLiteral("Flip sequence"),
                   QString::fromStdString("Vertically flipping sequence " + control->getUniqueName()),
                   "Sequence Vertical Mirror", std::string());
}

void NonInteractiveProcesses::rotate180Degree(ImageViewControl* control)
{
    if (!control->isSequence())
    {
        control->rotate180Degree();
        return;
    }
    orientSequence(control, an::ImageOrientation::Rotate180, QStringLiteral("Sequence Rotation"),
                   QString::fromStdString("Rotating sequence " + control->getUniqueName() + " 180 degrees"),
                   "Sequence Rotation", "180 degrees");
}

void NonInteractiveProcesses::rotate90DegreeRight(ImageViewControl* control)
{
    if (!control->isSequence())
    {
        control->rotate90DegreeRight();
        return;
    }
    orientSequence(control, an::ImageOrientation::Rotate90Clockwise, QStringLiteral("Sequence Rotation"),
                   QString::fromStdString("Rotating sequence " + control->getUniqueName() + " 90 degrees right"),
                   "Sequence Rotation", "90 degrees right");
}

void NonInteractiveProcesses::rotate90DegreeLeft(ImageViewControl* control)
{
    if (!control->isSequence())
    {
        control->rotate90DegreeLeft();
        return;
    }
    orientSequence(control, an::ImageOrientation::Rotate270Clockwise, QStringLiteral("Sequence Rotation"),
                   QString::fromStdString("Rotating sequence " + control->getUniqueName() + " 90 degrees left"),
                   "Sequence Rotation", "90 degrees left");
}

// ---- Grey, colour and the negative.

void NonInteractiveProcesses::convertToGrayscale(ImageViewControl* control)
{
    const std::string name = control->getUniqueName();
    runInPlace(
        control, QStringLiteral("Convert to grayscale"), QStringLiteral("Convert To Grayscale"),
        QString::fromStdString("Converting RGB sequence " + name + " to monochrome"),
        std::string(control->isSequence() ? "convert the sequence '" : "convert the image '") + name + "' to grayscale",
        "Convert To Grayscale", control->isSequence() && control->isSequenceAutodebayerEnabled(),
        [](const an::ui::ExecutionScope& scope, const an::ui::ProcessSource& source) {
            return process_utils::greyImageOf(scope, source);
        },
        true);
}

void NonInteractiveProcesses::convertToRGB(ImageViewControl* control)
{
    if (control->isCFA())
    {
        int answer = QMessageBox::Yes;
        process_utils::messageBoxYesNo(
            nullptr, QStringLiteral("Convert CFA to RGB"),
            QStringLiteral(
                "The file appears to have a bayer matrix. Running this process in a CFA matrix will produce undesired "
                "results. Please consider running a debayer process first.\nDo you want to run this process anyway?"),
            true, &answer);
        if (answer == QMessageBox::No)
        {
            return;
        }
    }
    const std::string name = control->getUniqueName();
    runInPlace(
        control, QStringLiteral("Convert to RGB"), QStringLiteral("Convert to RGB"),
        QString::fromStdString("Converting grayscale sequence " + name + " to RGB colorspace"),
        std::string(control->isSequence() ? "convert the sequence '" : "convert the image '") + name + "' to RGB",
        "Convert To RGB", control->isSequence() && control->isSequenceAutodebayerEnabled(),
        [](const an::ui::ExecutionScope& scope, const an::ui::ProcessSource& source) {
            return process_utils::colourImageOf(scope, source);
        },
        true);
}

void NonInteractiveProcesses::negativeTransformation(ImageViewControl* control)
{
    const std::string name = control->getUniqueName();
    const ViewMask mask = viewMaskOf(control);
    runInPlace(control, QStringLiteral("Negative Transformation"), QStringLiteral("Negative Transformation"),
               QString::fromStdString("Applying negative transformation to sequence " + name),
               std::string(control->isSequence() ? "apply the negative to the sequence '"
                                                 : "apply the negative to the image '") +
                   name + "'",
               "Negative Transform", control->isSequence() && control->isSequenceAutodebayerEnabled(),
               [mask](const an::ui::ExecutionScope& scope, const an::ui::ProcessSource& source) {
                   return maskedNegative(scope, source, mask);
               },
               false, {mask.asset});
}
