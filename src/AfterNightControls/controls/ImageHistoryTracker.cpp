// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The undo history of one view: the timeline, the edit preparation that runs on an edit's worker, the restores read on
/// a worker and shown on the GUI thread, and the directory the history's snapshots live in.

#include "stdafx.hpp"
#include "ImageHistoryTracker.h"
#include "ImageHistoryTrackerUtils.h"
#include "controls/IAppContext.h"
#include "controls/ProcessCompanions.h"

#include <AfterNightConsumerRuntime/OperationScope.h>

#include <AfterNightEngine/Core/ErrorCode.h>

#include <QCoreApplication>
#include <QDir>
#include <QMetaObject>
#include <QThreadPool>
#include <QUuid>

#include <filesystem>
#include <mutex>
#include <set>
#include <unordered_map>
#include <utility>

namespace an::ui::history
{

namespace
{

[[nodiscard]] an::Status refusal(const an::ErrorCode code, std::string message)
{
    return an::Status::failure(code,
                               an::StatusDetail{.message = std::move(message), .operation = "ImageHistoryTracker"});
}

/// A new 128-bit name in 32 hexadecimal characters: what an artifact identity holds without truncation.
[[nodiscard]] std::string newToken()
{
    return QUuid::createUuid().toString(QUuid::Id128).toStdString();
}

[[nodiscard]] std::filesystem::path pathOf(const QString& path)
{
#ifdef _WIN32
    return std::filesystem::path(path.toStdWString());
#else
    return std::filesystem::path(path.toStdString());
#endif
}

[[nodiscard]] std::string utf8Of(const std::filesystem::path& path)
{
    const std::u8string text = path.generic_u8string();
    return std::string(text.begin(), text.end());
}

void removeFile(const std::string& path)
{
    std::error_code ignored;
    std::filesystem::remove(std::filesystem::path(std::u8string(path.begin(), path.end())), ignored);
}

/// Reports a failure the user has to hear of, from any thread: the log, and a message the dialog service shows on the
/// GUI thread.
void reportFromAnyThread(const std::string& message)
{
    LOG_ERROR("Image history: {}", message);
    if (auto* context = an::ui::appContext())
    {
        if (auto* dialogs = context->getDialogService())
        {
            dialogs->notifyErrorMessageAsync(nullptr, QStringLiteral("Image History"), QString::fromStdString(message));
        }
    }
}

/// Reads the frame at `frameIndex` of a sequence through the sequence's frame reader.
[[nodiscard]] an::Result<an::consumer::ImageAsset> readSequenceFrame(const an::consumer::EngineSession& session,
                                                                     an::ui::DocumentSequence sequence,
                                                                     const std::uint64_t frameIndex,
                                                                     const bool autoDebayer, std::stop_token stop)
{
    sequence.selectedFrames = std::vector<std::uint64_t>{frameIndex};
    an::Result<std::unique_ptr<an::ui::IProcessFrameReader>> reader =
        an::ui::openProcessFrames(session, sequence, autoDebayer);
    if (!reader.hasValue())
    {
        return reader.status();
    }
    an::Result<an::consumer::OperationScope> scope = session.admit(an::consumer::OperationRequest{
        .purpose = an::consumer::OperationPurpose::Apply, .maxConcurrency = 1u, .stopToken = std::move(stop)});
    if (!scope.hasValue())
    {
        return scope.status();
    }
    // A reader hands the selected frame out; one that hands out every frame is read until it reaches it.
    while (true)
    {
        an::Result<std::optional<an::ui::ProcessSequenceFrame>> frame =
            (*reader)->next(scope->engine(), scope->execution());
        if (!frame.hasValue())
        {
            return frame.status();
        }
        if (!frame->has_value())
        {
            return refusal(an::ErrorCode::InvalidData,
                           "the sequence holds no frame " + std::to_string(frameIndex + 1u));
        }
        if ((*frame)->index == frameIndex)
        {
            return std::move((*frame)->asset);
        }
    }
}

} // namespace

/// What one history shares with the workers it starts: the session, the backup writer, its own directory, and the edits
/// prepared on their workers. Thread-safe.
class HistoryStore
{
public:
    HistoryStore(const an::consumer::EngineSession& session, StepCodecPtr codec, const QString& baseDirectory)
        : m_Session(session), m_RunId(newToken()), m_Directory(pathOf(baseDirectory) / ("AN_HISTORY_" + m_RunId)),
          m_Queue(session, std::move(codec), [this](const Snapshot& snapshot) { onSettled(snapshot); })
    {
    }

    HistoryStore(const HistoryStore&) = delete;
    HistoryStore& operator=(const HistoryStore&) = delete;

    [[nodiscard]] const an::consumer::EngineSession& session() const
    {
        return m_Session;
    }

    [[nodiscard]] BackupQueue& queue()
    {
        return m_Queue;
    }

    [[nodiscard]] std::string directory() const
    {
        return utf8Of(m_Directory);
    }

    /// A new path in the history's directory and the identity the snapshot written there is for; nothing once the
    /// history is closed.
    [[nodiscard]] std::optional<std::pair<std::string, an::ArtifactIdentity>> newSnapshot()
    {
        std::lock_guard lock(m_Mutex);
        if (m_Closed)
        {
            return std::nullopt;
        }
        if (!m_DirectoryMade)
        {
            std::error_code error;
            std::filesystem::create_directories(m_Directory, error);
            if (error)
            {
                return std::nullopt;
            }
            m_DirectoryMade = true;
        }
        std::string artifactId = newToken();
        an::Result<an::ArtifactIdentity> identity = an::ArtifactIdentity::create(artifactId, m_RunId);
        if (!identity.hasValue())
        {
            return std::nullopt;
        }
        return std::make_pair(utf8Of(m_Directory / (artifactId + std::string(m_Queue.codec().fileExtension()))),
                              std::move(*identity));
    }

    /// Remembers what an edit's worker prepared, until the edit is recorded or let go.
    void registerPrepared(const std::uint64_t editedIdentity, std::weak_ptr<void> prepared)
    {
        std::lock_guard lock(m_Mutex);
        m_Prepared[editedIdentity] = std::move(prepared);
    }

    /// Takes what was prepared for an edited image, if it is still held.
    [[nodiscard]] std::shared_ptr<void> takePrepared(const std::uint64_t editedIdentity)
    {
        std::lock_guard lock(m_Mutex);
        const auto found = m_Prepared.find(editedIdentity);
        if (found == m_Prepared.end())
        {
            return nullptr;
        }
        std::shared_ptr<void> prepared = found->second.lock();
        m_Prepared.erase(found);
        return prepared;
    }

    /// Forgets what was prepared for an edited image.
    void forgetPrepared(const std::uint64_t editedIdentity)
    {
        std::lock_guard lock(m_Mutex);
        const auto found = m_Prepared.find(editedIdentity);
        if (found != m_Prepared.end() && found->second.expired())
        {
            m_Prepared.erase(found);
        }
    }

    /// Lets a snapshot no step uses go: off the queue if it has not started, and its file removed now or as soon as
    /// its write ends.
    void orphan(const SnapshotPtr& snapshot)
    {
        m_Queue.discard(snapshot);
        {
            std::lock_guard lock(m_Mutex);
            if (snapshot->state() == SnapshotState::Pending)
            {
                m_Orphans.insert(snapshot->path());
                return;
            }
        }
        removeFile(snapshot->path());
    }

    /// Whether the history is gone and its directory reclaimed.
    [[nodiscard]] bool closed() const
    {
        std::lock_guard lock(m_Mutex);
        return m_Closed;
    }

    /// Stops the writer and reclaims the history's own directory - nothing else.
    void close()
    {
        {
            std::lock_guard lock(m_Mutex);
            m_Closed = true;
        }
        m_Queue.close();
        std::error_code ignored;
        std::filesystem::remove_all(m_Directory, ignored);
    }

private:
    // Called on the writer's thread once a snapshot's write has ended.
    void onSettled(const Snapshot& snapshot)
    {
        bool orphaned = false;
        bool closed = false;
        {
            std::lock_guard lock(m_Mutex);
            orphaned = m_Orphans.erase(snapshot.path()) > 0u;
            closed = m_Closed;
        }
        if (orphaned)
        {
            removeFile(snapshot.path());
            return;
        }
        if (!closed && snapshot.state() == SnapshotState::Failed)
        {
            // The step stays, and says it cannot be restored.
            reportFromAnyThread(fmt::format(
                "Could NOT keep the snapshot of {}; undo and redo cannot restore that step. Error message: {}",
                snapshot.label(), snapshot.failure()));
        }
    }

    const an::consumer::EngineSession& m_Session;
    const std::string m_RunId;
    const std::filesystem::path m_Directory;

    mutable std::mutex m_Mutex;
    bool m_DirectoryMade = false;
    bool m_Closed = false;
    std::unordered_map<std::uint64_t, std::weak_ptr<void>> m_Prepared;
    std::set<std::string> m_Orphans;

    // Last, so the writer - which calls back into this store - ends before anything else here goes.
    BackupQueue m_Queue;
};

/// What an edit's worker secured for the edit's step: the state the edit replaces written again when its first write
/// failed, and the result's copy with its admission, or the frame it changed already written.
struct PreparedEdit
{
    std::weak_ptr<HistoryStore> store;
    std::uint64_t editedIdentity = 0u;
    std::string replacedStep;
    SnapshotPtr replacedSnapshot;

    SnapshotPtr retaken;

    std::optional<an::consumer::ImageAsset> copy;
    an::consumer::MemoryReservation room;

    SnapshotPtr frame;
    std::uint64_t frameIndex = 0u;

    bool consumed = false;

    PreparedEdit() = default;
    PreparedEdit(const PreparedEdit&) = delete;
    PreparedEdit& operator=(const PreparedEdit&) = delete;

    ~PreparedEdit()
    {
        if (const std::shared_ptr<HistoryStore> held = store.lock())
        {
            held->forgetPrepared(editedIdentity);
        }
        // An edit let go keeps nothing: a file written for it is removed.
        if (!consumed)
        {
            for (const SnapshotPtr* written : {&retaken, &frame})
            {
                if (*written)
                {
                    removeFile((*written)->path());
                }
            }
        }
    }
};

} // namespace an::ui::history

namespace
{

using an::ui::history::HistoryStore;
using an::ui::history::PreparedEdit;
using an::ui::history::SnapshotState;

an::ui::IAppContext* currentAppContext()
{
    return an::ui::appContext();
}

void notifyActiveImageHistoryChanged()
{
    if (auto* context = currentAppContext())
    {
        if (auto* actionStateManager = context->getActionStateManager())
        {
            actionStateManager->updateActiveImageHistory();
        }
    }
}

/// A snapshot that never reached the writer, failed with the reason.
[[nodiscard]] an::ui::history::SnapshotPtr failedSnapshot(const std::string& label, const std::string& failure)
{
    auto snapshot = std::make_shared<an::ui::history::Snapshot>(
        std::string(), label, *an::ArtifactIdentity::create("unwritten", "unwritten"), 0u);
    snapshot->fail(failure);
    return snapshot;
}

} // namespace

struct ImageHistoryTracker::Restore
{
    size_t target = 0;
    std::optional<size_t> removed;
    std::string action;
    ImageHistoryStepSPtr targetStep;
    std::uint64_t document = 0u;
    bool sequence = false;
    std::optional<an::ui::DocumentSequence> otherSequence;
    std::uint64_t frameIndex = 0u;
    std::stop_source stop;
};

ImageHistoryStep::ImageHistoryStep(const std::string& uuid, const std::string& stepName, const std::string& stepDesc)
    : m_CreationTime(QDateTime::currentDateTime()), m_StepUuid(uuid), m_StepName(stepName), m_StepDesc(stepDesc)
{
}

std::string ImageHistoryStep::getUuid() const
{
    return m_StepUuid;
}

std::string ImageHistoryStep::getStepName() const
{
    return m_StepName;
}

std::string ImageHistoryStep::getStepDesc() const
{
    return m_StepDesc;
}

an::ui::history::SnapshotState ImageHistoryStep::snapshotState() const
{
    return m_Snapshot ? m_Snapshot->state() : SnapshotState::Written;
}

an::ui::history::SnapshotState ImageHistoryStep::waitAsyncJobsDone() const
{
    return m_Snapshot ? m_Snapshot->waitUntilSettled() : SnapshotState::Written;
}

ImageHistoryTracker::ImageHistoryTracker(an::ui::history::ImageHistoryHost& host,
                                         const an::consumer::EngineSession& session,
                                         an::ui::history::StepCodecPtr codec, QString directory)
    : m_Host(host),
      m_Store(std::make_shared<HistoryStore>(
          session, std::move(codec), directory.isEmpty() ? an::ui::ensureTempDirPathOrSystemTemp() : directory)),
      m_Self(std::make_shared<ImageHistoryTracker*>(this))
{
}

void ImageHistoryTracker::registerInitialStep(const std::string& historyDesc)
{
    const ImageHistoryStepSPtr previous = m_InitialStep;
    m_InitialStep = makeStep("Initial state", historyDesc);
    describeShownState(m_InitialStep);
    if (!m_InitialStep->m_Sequence.has_value())
    {
        keepShownImage(m_InitialStep);
        if (m_InitialStep->m_Snapshot->state() == SnapshotState::Failed)
        {
            LOG_ERROR("Image history: could NOT keep the initial state of '{}': {}", m_Host.historyName(),
                      m_InitialStep->m_Snapshot->failure());
        }
    }
    if (previous)
    {
        releaseDroppedSteps({previous});
    }
    // The initial state is not in the list of steps.
    notifyHistoryUpdated();
}

void ImageHistoryTracker::registerNewImageRenameStep()
{
    auto step = makeStep("Rename", m_Host.historyName());

    // A rename changes no pixel: the step shares the snapshot, the sequence and the frame of the state it renames.
    step->m_ImageName = m_Host.historyName();
    if (const auto parentStep = getStepByIndex(m_CurrentStep))
    {
        step->m_Sequence = parentStep->m_Sequence;
        step->m_Snapshot = parentStep->m_Snapshot;
        step->m_FrameIndex = parentStep->m_FrameIndex;
    }

    annotateNewStep(step);

    notifyHistoryUpdated();
}

bool ImageHistoryTracker::registerShownState(const std::string& stepName, const std::string& stepDesc)
{
    if (!m_Host.historyAsset())
    {
        return false;
    }
    const auto currentStep = getStepByIndex(m_CurrentStep);
    auto step = makeStep(stepName, stepDesc);
    describeShownState(step);

    if (step->m_Sequence.has_value())
    {
        const bool sameSequence = currentStep && currentStep->m_Sequence.has_value() &&
                                  currentStep->m_Sequence->sourcePath == step->m_Sequence->sourcePath;
        if (sameSequence)
        {
            // The sequence file is the one the current step shows, so what changed is the frame on screen.
            keepShownImage(step);
            step->m_FrameIndex = step->m_Sequence->currentFrame;
        }
    }
    else
    {
        keepShownImage(step);
    }

    if (step->m_Snapshot && step->m_Snapshot->state() == SnapshotState::Failed)
    {
        // The view already shows the change, so the step is recorded all the same: without it the timeline would no
        // longer match what the view shows. It says it cannot be restored.
        reportFailure(
            fmt::format("keep the snapshot of {}; undo and redo cannot restore that step", snapshotLabel(stepName)),
            step->m_Snapshot->failure());
    }

    // A state shown while a restore is on its way supersedes the restore.
    if (m_Restore)
    {
        m_Restore->stop.request_stop();
        m_Restore.reset();
    }

    annotateNewStep(step);
    notifyHistoryUpdated();
    return true;
}

ImageHistoryTracker::EditPreparation ImageHistoryTracker::editPreparation(const std::string& stepName)
{
    const auto currentStep = getStepByIndex(m_CurrentStep);
    const std::weak_ptr<HistoryStore> weakStore = m_Store;
    const std::string replacedStep = currentStep ? currentStep->getUuid() : std::string();
    const an::ui::history::SnapshotPtr replacedSnapshot = currentStep ? currentStep->m_Snapshot : nullptr;
    const std::string replacedLabel = currentStep ? snapshotLabel(currentStep->getStepName()) : std::string();
    const std::string label = snapshotLabel(stepName);
    const std::optional<an::ui::DocumentSequence> sequence = m_Host.historySequence();
    const bool frameEdit = sequence.has_value();
    const std::uint64_t frameIndex = sequence.has_value() ? sequence->currentFrame : 0u;

    return [weakStore, replacedStep, replacedSnapshot, replacedLabel, label, stepName, frameEdit,
            frameIndex](const an::consumer::AssetWriteLease& replaced, const an::consumer::ImageAsset& edited,
                        std::stop_token stop) -> an::Result<std::shared_ptr<void>> {
        const std::shared_ptr<HistoryStore> store = weakStore.lock();
        if (!store || store->closed())
        {
            return an::Status::failure(an::ErrorCode::Cancelled, "the view's history is closed");
        }
        const auto refuse = [&stepName](const std::string& reason, const an::Status& cause) -> an::Status {
            if (cause.code() != an::ErrorCode::Cancelled)
            {
                an::ui::history::reportFromAnyThread(fmt::format("Could NOT apply '{}', because {}. Error message: {}",
                                                                 stepName, reason, cause.message()));
            }
            return cause;
        };
        an::ui::history::BackupQueue& queue = store->queue();
        const an::consumer::EngineSession& session = store->session();

        auto prepared = std::make_shared<PreparedEdit>();
        prepared->store = store;
        prepared->editedIdentity = edited.identity();
        prepared->replacedStep = replacedStep;
        prepared->replacedSnapshot = replacedSnapshot;

        // The state the edit replaces must be kept before anything is shown over it.
        if (replacedSnapshot)
        {
            const SnapshotState replacedState = replacedSnapshot->waitUntilSettled(stop);
            if (replacedState == SnapshotState::Pending)
            {
                return an::Status::failure(an::ErrorCode::Cancelled, "the edit was stopped");
            }
            if (replacedState != SnapshotState::Written)
            {
                // Its own write was lost, and the edit's lease still holds that state: write it again, admitted before
                // it is copied.
                an::Result<an::consumer::MemoryReservation> room =
                    queue.reserve(an::ui::history::historyBytesOf(replaced), stop);
                if (!room.hasValue())
                {
                    return refuse("the state it replaces cannot be kept for undo", room.status());
                }
                an::Result<an::consumer::ImageAsset> copy = an::ui::history::copyForHistory(session, replaced);
                if (!copy.hasValue())
                {
                    return refuse("the state it replaces cannot be kept for undo", copy.status());
                }
                auto target = store->newSnapshot();
                if (!target.has_value())
                {
                    return an::Status::failure(an::ErrorCode::Cancelled, "the view's history is closed");
                }
                prepared->retaken = queue.submit(std::move(*copy), std::move(*room), std::move(target->first),
                                                 replacedLabel, std::move(target->second));
                if (prepared->retaken->waitUntilSettled(stop) != SnapshotState::Written)
                {
                    const an::Status cause =
                        stop.stop_requested()
                            ? an::Status::failure(an::ErrorCode::Cancelled, "the edit was stopped")
                            : an::Status::failure(an::ErrorCode::ExternalFailure, prepared->retaken->failure());
                    return refuse("the state it replaces cannot be kept for undo", cause);
                }
            }
        }

        // The result's copy is admitted before it is made.
        an::Result<an::consumer::AssetReadLease> lease = edited.read(stop);
        if (!lease.hasValue())
        {
            return refuse("its result cannot be read", lease.status());
        }
        an::Result<an::consumer::MemoryReservation> room = queue.reserve(an::ui::history::historyBytesOf(*lease), stop);
        if (!room.hasValue())
        {
            return refuse("its snapshot cannot be kept", room.status());
        }
        an::Result<an::consumer::ImageAsset> copy = an::ui::history::copyForHistory(session, *lease);
        lease->release();
        if (!copy.hasValue())
        {
            return refuse("its snapshot cannot be kept", copy.status());
        }

        if (frameEdit)
        {
            // The sequence file does not hold a changed frame, so the frame is written before the edit is shown.
            auto target = store->newSnapshot();
            if (!target.has_value())
            {
                return an::Status::failure(an::ErrorCode::Cancelled, "the view's history is closed");
            }
            prepared->frame = queue.submit(std::move(*copy), std::move(*room), std::move(target->first), label,
                                           std::move(target->second));
            prepared->frameIndex = frameIndex;
            if (prepared->frame->waitUntilSettled(stop) != SnapshotState::Written)
            {
                const an::Status cause =
                    stop.stop_requested()
                        ? an::Status::failure(an::ErrorCode::Cancelled, "the edit was stopped")
                        : an::Status::failure(an::ErrorCode::ExternalFailure, prepared->frame->failure());
                return refuse("the frame it changes cannot be kept", cause);
            }
        }
        else
        {
            prepared->copy.emplace(std::move(*copy));
            prepared->room = std::move(*room);
        }

        store->registerPrepared(prepared->editedIdentity, prepared);
        return std::shared_ptr<void>(prepared);
    };
}

bool ImageHistoryTracker::registerEditBeforePublishing(const std::string& stepName, const std::string& stepDesc,
                                                       const an::consumer::ImageAsset& edited)
{
    const auto currentStep = getStepByIndex(m_CurrentStep);

    const auto refuse = [this, &stepName](const std::string& reason, const std::string& failure) {
        reportFailure(fmt::format("apply '{}', because {}", stepName, reason), failure);
        return false;
    };

    if (!edited.valid())
    {
        return refuse("there is no edited image", {});
    }
    if (m_Restore)
    {
        return refuse("a step of its history is being restored", {});
    }

    auto step = makeStep(stepName, stepDesc);
    describeShownState(step);

    if (const std::shared_ptr<void> held = m_Store->takePrepared(edited.identity()))
    {
        auto prepared = std::static_pointer_cast<PreparedEdit>(held);
        const bool sameState =
            currentStep && (currentStep->getUuid() == prepared->replacedStep ||
                            (!prepared->retaken && currentStep->m_Snapshot == prepared->replacedSnapshot));
        if (!sameState)
        {
            return refuse("its history changed while it was prepared", {});
        }
        if (prepared->retaken)
        {
            // The state the edit replaces is kept by the snapshot its worker wrote again.
            const an::ui::history::SnapshotPtr lost = currentStep->m_Snapshot;
            for (const auto& each : m_ImageHistory)
            {
                if (each->m_Snapshot == lost)
                {
                    each->m_Snapshot = prepared->retaken;
                }
            }
            if (m_InitialStep && m_InitialStep->m_Snapshot == lost)
            {
                m_InitialStep->m_Snapshot = prepared->retaken;
            }
        }
        if (prepared->frame)
        {
            step->m_Snapshot = prepared->frame;
            step->m_FrameIndex = prepared->frameIndex;
        }
        else
        {
            auto target = m_Store->newSnapshot();
            if (!target.has_value() || !prepared->copy.has_value())
            {
                return refuse("its history is closing", {});
            }
            step->m_Snapshot =
                m_Store->queue().submit(std::move(*prepared->copy), std::move(prepared->room), std::move(target->first),
                                        snapshotLabel(stepName), std::move(target->second));
        }
        prepared->consumed = true;
        annotateNewStep(step);
        notifyHistoryUpdated();
        return true;
    }

    // An edit nobody prepared is recorded only when nothing has to be waited for.
    if (currentStep && currentStep->m_Snapshot)
    {
        const SnapshotState replacedState = currentStep->m_Snapshot->state();
        if (replacedState == SnapshotState::Pending)
        {
            return refuse("the state it replaces is still being kept for undo", {});
        }
        if (replacedState != SnapshotState::Written)
        {
            return refuse("the state it replaces cannot be kept for undo", currentStep->m_Snapshot->failure());
        }
    }
    if (step->m_Sequence.has_value())
    {
        return refuse("the frame it changes has to be kept before it is shown", {});
    }
    an::Result<an::consumer::AssetReadLease> lease = edited.tryRead();
    if (!lease.hasValue())
    {
        return refuse("its result cannot be read", lease.status().message());
    }
    an::Result<an::consumer::MemoryReservation> room =
        m_Store->queue().tryReserve(an::ui::history::historyBytesOf(*lease));
    if (!room.hasValue())
    {
        return refuse("its snapshot cannot be kept", room.status().message());
    }
    an::Result<an::consumer::ImageAsset> copy = an::ui::history::copyForHistory(m_Store->session(), *lease);
    lease->release();
    if (!copy.hasValue())
    {
        return refuse("its snapshot cannot be kept", copy.status().message());
    }
    auto target = m_Store->newSnapshot();
    if (!target.has_value())
    {
        return refuse("its history is closing", {});
    }
    step->m_Snapshot = m_Store->queue().submit(std::move(*copy), std::move(*room), std::move(target->first),
                                               snapshotLabel(stepName), std::move(target->second));

    annotateNewStep(step);
    notifyHistoryUpdated();
    return true;
}

bool ImageHistoryTracker::undoStep()
{
    if (!hasUndoSteps())
    {
        return false;
    }

    LOG_INFO("'{}' undo step '{}'", m_Host.historyName(), getPreviousStepName());
    return startRestore(m_CurrentStep - 1, std::nullopt, "undo the last step");
}

bool ImageHistoryTracker::redoStep()
{
    if (!hasRedoSteps())
    {
        return false;
    }

    LOG_INFO("'{}' redo step '{}'", m_Host.historyName(), getNextStepName());
    return startRestore(m_CurrentStep + 1, std::nullopt, "redo the next step");
}

bool ImageHistoryTracker::hasUndoSteps() const
{
    return !m_ImageHistory.empty() && m_CurrentStep > 0;
}

bool ImageHistoryTracker::hasRedoSteps() const
{
    return !m_ImageHistory.empty() && m_CurrentStep < m_ImageHistory.size();
}

bool ImageHistoryTracker::hasSteps() const
{
    return !m_ImageHistory.empty();
}

bool ImageHistoryTracker::isRestoring() const
{
    return m_Restore != nullptr;
}

std::vector<ImageHistoryStepSPtr> ImageHistoryTracker::getImageHistorySteps() const
{
    return std::vector<ImageHistoryStepSPtr>(m_ImageHistory.begin(), m_ImageHistory.end());
}

ImageHistoryStepSPtr ImageHistoryTracker::getInitialStep() const
{
    return m_InitialStep;
}

ImageHistoryTracker::StepReader ImageHistoryTracker::stepReader(const size_t stepIndex,
                                                                const std::uint64_t frameIndex) const
{
    const auto step = getStepByIndex(stepIndex);
    if (!step)
    {
        return {};
    }
    const std::weak_ptr<HistoryStore> weakStore = m_Store;
    const an::ui::history::SnapshotPtr snapshot = step->m_Snapshot;
    const std::optional<an::ui::DocumentSequence> sequence = step->m_Sequence;
    const bool autoDebayer = m_Host.historyAutoDebayer();
    const std::string name = step->getStepName();
    return [weakStore, snapshot, sequence, autoDebayer, frameIndex,
            name](std::stop_token stop) -> an::Result<an::consumer::ImageAsset> {
        const std::shared_ptr<HistoryStore> store = weakStore.lock();
        if (!store || store->closed())
        {
            return an::Status::failure(an::ErrorCode::Cancelled, "the view's history is closed");
        }
        if (snapshot)
        {
            const SnapshotState state = snapshot->waitUntilSettled(stop);
            if (state == SnapshotState::Pending)
            {
                return an::Status::failure(an::ErrorCode::Cancelled, "the read was stopped");
            }
            if (state != SnapshotState::Written)
            {
                return an::Status::failure(
                    an::ErrorCode::InvalidState,
                    fmt::format("the snapshot of step '{}' was not kept: {}", name, snapshot->failure()));
            }
            return store->queue().codec().read(store->session(), snapshot->path(), snapshot->identity(),
                                               std::move(stop));
        }
        if (sequence.has_value())
        {
            return an::ui::history::readSequenceFrame(store->session(), *sequence, frameIndex, autoDebayer,
                                                      std::move(stop));
        }
        return an::Status::failure(an::ErrorCode::InvalidState, fmt::format("step '{}' has no snapshot", name));
    };
}

std::string ImageHistoryTracker::getPreviousStepName() const
{
    if (hasUndoSteps())
    {
        return m_ImageHistory.at(m_CurrentStep - 1)->getStepName();
    }

    return std::string();
}

size_t ImageHistoryTracker::getCurrentStep() const
{
    return m_CurrentStep;
}

size_t ImageHistoryTracker::getTotalStepCount() const
{
    return m_InitialStep ? m_ImageHistory.size() + 1 : 0;
}

bool ImageHistoryTracker::canJumpToStep(const size_t stepIndex) const
{
    return !m_Restore && an::ui::history::canJumpToStep(getTotalStepCount(), m_CurrentStep, stepIndex);
}

bool ImageHistoryTracker::jumpToStep(const size_t stepIndex)
{
    if (!an::ui::history::canJumpToStep(getTotalStepCount(), m_CurrentStep, stepIndex))
    {
        return false;
    }
    if (const auto targetStep = getStepByIndex(stepIndex))
    {
        LOG_INFO("'{}' jump to step '{}'", m_Host.historyName(), targetStep->getStepName());
    }
    return startRestore(stepIndex, std::nullopt, "jump to the selected history step");
}

bool ImageHistoryTracker::canRemoveStep(const size_t stepIndex) const
{
    return !m_Restore && an::ui::history::canRemoveStep(getTotalStepCount(), stepIndex);
}

bool ImageHistoryTracker::removeStep(const size_t stepIndex)
{
    const size_t totalSteps = getTotalStepCount();
    if (m_Restore || !an::ui::history::canRemoveStep(totalSteps, stepIndex))
    {
        return false;
    }

    const auto removalPlan = an::ui::history::planStepRemoval(totalSteps, m_CurrentStep, stepIndex);
    const auto removedStep = m_ImageHistory.at(stepIndex - 1);

    LOG_INFO("'{}' remove step '{}'", m_Host.historyName(), removedStep->getStepName());

    if (removalPlan.should_restore_after_removal)
    {
        // The state before it is shown first; the step goes when that is shown, so a restore that fails removes
        // nothing.
        return startRestore(removalPlan.next_current_step, stepIndex, "remove the selected history step");
    }

    m_ImageHistory.erase(m_ImageHistory.begin() + static_cast<std::ptrdiff_t>(stepIndex - 1));
    m_CurrentStep = removalPlan.next_current_step;
    releaseDroppedSteps({removedStep});

    m_Host.historyChanged();
    notifyActiveImageHistoryChanged();
    notifyHistoryUpdated();
    return true;
}

ImageHistoryStepSPtr ImageHistoryTracker::makeStep(const std::string& stepName, const std::string& stepDesc) const
{
    const auto uuid = QUuid::createUuid();
    return std::make_shared<ImageHistoryStep>(uuid.toString().toUtf8().toStdString(), stepName, stepDesc);
}

std::string ImageHistoryTracker::snapshotLabel(const std::string& stepName) const
{
    return fmt::format("step '{}' of '{}'", stepName, m_Host.historyName());
}

void ImageHistoryTracker::describeShownState(const ImageHistoryStepSPtr& step) const
{
    step->m_ImageName = m_Host.historyName();
    step->m_Sequence = m_Host.historySequence();
}

void ImageHistoryTracker::keepShownImage(const ImageHistoryStepSPtr& step)
{
    const std::string label = snapshotLabel(step->getStepName());
    const std::shared_ptr<const an::consumer::ImageAsset> shown = m_Host.historyAsset();
    if (!shown)
    {
        step->m_Snapshot = failedSnapshot(label, "the view shows no image");
        return;
    }
    // The copy is taken now, under a lease, whatever happens to the shown image afterwards; the file is written
    // behind it. The state is already shown, so its copy is admitted whatever the allowance holds and counted.
    an::Result<an::consumer::AssetReadLease> lease = shown->tryRead();
    if (!lease.hasValue())
    {
        step->m_Snapshot =
            failedSnapshot(label, std::string("the shown image is being written: ") + lease.status().message());
        return;
    }
    an::Result<an::consumer::MemoryReservation> room =
        m_Store->queue().tryReserve(an::ui::history::historyBytesOf(*lease), an::consumer::ReservationKind::Required);
    if (!room.hasValue())
    {
        step->m_Snapshot = failedSnapshot(label, room.status().message());
        return;
    }
    an::Result<an::consumer::ImageAsset> copy = an::ui::history::copyForHistory(m_Store->session(), *lease);
    lease->release();
    if (!copy.hasValue())
    {
        step->m_Snapshot = failedSnapshot(label, copy.status().message());
        return;
    }
    auto target = m_Store->newSnapshot();
    if (!target.has_value())
    {
        step->m_Snapshot = failedSnapshot(label, "the history's directory cannot be made");
        return;
    }
    step->m_Snapshot = m_Store->queue().submit(std::move(*copy), std::move(*room), std::move(target->first), label,
                                               std::move(target->second));
}

void ImageHistoryTracker::annotateNewStep(const ImageHistoryStepSPtr& step)
{
    std::vector<ImageHistoryStepSPtr> droppedSteps;

    if (hasRedoSteps())
    {
        // The steps after the current one are the branch the new step replaces
        droppedSteps.assign(m_ImageHistory.begin() + m_CurrentStep, m_ImageHistory.end());
        m_ImageHistory.erase(m_ImageHistory.begin() + m_CurrentStep, m_ImageHistory.end());
    }

    m_ImageHistory.push_back(step);

    if (m_ImageHistory.size() >= m_MaxSteps)
    {
        // The oldest recorded step becomes the initial state, and the previous initial state goes
        droppedSteps.push_back(m_InitialStep);
        m_InitialStep = m_ImageHistory.front();
        m_ImageHistory.pop_front();
        m_CurrentStep--;
    }

    releaseDroppedSteps(droppedSteps);

    m_CurrentStep++;

    m_Host.historyChanged();
    notifyActiveImageHistoryChanged();
}

bool ImageHistoryTracker::startRestore(const size_t targetIndex, const std::optional<size_t> removedIndex,
                                       const std::string& action)
{
    if (m_Restore)
    {
        LOG_INFO("'{}' could not {}: a step is still being restored", m_Host.historyName(), action);
        return false;
    }
    const auto leavingStep = getStepByIndex(m_CurrentStep);
    const auto targetStep = getStepByIndex(targetIndex);
    if (!leavingStep || !targetStep)
    {
        return false;
    }

    auto restore = std::make_shared<Restore>();
    restore->target = targetIndex;
    restore->removed = removedIndex;
    restore->action = action;
    restore->targetStep = targetStep;
    restore->document = m_Host.historyDocument();

    const std::optional<an::ui::DocumentSequence> shownSequence = m_Host.historySequence();
    restore->sequence = shownSequence.has_value();

    bool readsImage = false;
    std::uint64_t frameIndex = 0u;
    if (restore->sequence)
    {
        const bool sequenceChanged = targetStep->m_Sequence.has_value() && leavingStep->m_Sequence.has_value() &&
                                     targetStep->m_Sequence->sourcePath != leavingStep->m_Sequence->sourcePath;
        const bool frameChanged = targetStep->m_Snapshot != leavingStep->m_Snapshot;
        // A step that changed a frame is shown at that frame; leaving one, the view goes back to the frame it changed.
        frameIndex = shownSequence->currentFrame;
        if (targetStep->m_FrameIndex.has_value())
        {
            frameIndex = *targetStep->m_FrameIndex;
        }
        else if (leavingStep->m_FrameIndex.has_value())
        {
            frameIndex = *leavingStep->m_FrameIndex;
        }
        restore->frameIndex = frameIndex;
        if (sequenceChanged)
        {
            restore->otherSequence = targetStep->m_Sequence;
        }
        readsImage = sequenceChanged || frameChanged;
    }
    else
    {
        readsImage = targetStep->m_Snapshot != leavingStep->m_Snapshot;
    }

    if (readsImage && !targetStep->m_Snapshot && !restore->sequence)
    {
        reportFailure(action, fmt::format("step '{}' has no snapshot", targetStep->getStepName()));
        return false;
    }
    if (readsImage && targetStep->m_Snapshot)
    {
        const SnapshotState state = targetStep->m_Snapshot->state();
        if (state == SnapshotState::Failed || state == SnapshotState::Discarded)
        {
            // Nothing changes: the view, its name and the current step stay as they are.
            reportFailure(action, fmt::format("the snapshot of step '{}' was not kept: {}", targetStep->getStepName(),
                                              targetStep->m_Snapshot->failure()));
            return false;
        }
    }

    m_Restore = restore;
    if (!readsImage)
    {
        // The step shares its image with the one shown: only its name is restored.
        endRestore(restore, an::Status::success());
        return true;
    }

    StepReader read = stepReader(targetIndex, frameIndex);
    if (!read)
    {
        m_Restore.reset();
        return false;
    }
    const std::shared_ptr<ImageHistoryTracker*> self = m_Self;
    QThreadPool::globalInstance()->start([self, restore, read = std::move(read)]() {
        auto result = std::make_shared<an::Result<an::consumer::ImageAsset>>(read(restore->stop.get_token()));
        QCoreApplication* application = QCoreApplication::instance();
        if (application == nullptr)
        {
            return;
        }
        QMetaObject::invokeMethod(
            application,
            [self, restore, result]() {
                if (*self != nullptr)
                {
                    (*self)->finishRestore(restore, std::move(*result));
                }
            },
            Qt::QueuedConnection);
    });
    return true;
}

void ImageHistoryTracker::finishRestore(const std::shared_ptr<Restore>& restore,
                                        an::Result<an::consumer::ImageAsset>&& read)
{
    if (m_Restore != restore)
    {
        return;
    }
    if (!read.hasValue())
    {
        m_Restore.reset();
        if (read.status().code() != an::ErrorCode::Cancelled)
        {
            reportFailure(restore->action, read.status().message());
        }
        return;
    }
    if (m_Host.historyDocument() != restore->document)
    {
        m_Restore.reset();
        reportFailure(restore->action, "the view showed another image before the step was read");
        return;
    }

    const std::shared_ptr<ImageHistoryTracker*> self = m_Self;
    an::ui::history::RestoreDone done = [self, restore](const an::Status& shown) {
        if (*self != nullptr)
        {
            (*self)->endRestore(restore, shown);
        }
    };
    if (restore->sequence)
    {
        m_Host.restoreHistorySequence(restore->otherSequence, restore->frameIndex, std::move(*read), std::move(done));
    }
    else
    {
        m_Host.restoreHistoryImage(std::move(*read), std::move(done));
    }
}

void ImageHistoryTracker::endRestore(const std::shared_ptr<Restore>& restore, const an::Status& shown)
{
    if (m_Restore != restore)
    {
        return;
    }
    m_Restore.reset();
    if (shown.failed())
    {
        reportFailure(restore->action, shown.message());
        return;
    }

    // The restored state is shown under the name it was recorded with, made unique among the views now open.
    const ImageHistoryStepSPtr& targetStep = restore->targetStep;
    if (targetStep->m_ImageName != m_Host.historyName())
    {
        std::string validatedName = targetStep->m_ImageName;
        if (auto* context = currentAppContext())
        {
            if (auto* viewControlManager = context->getViewControlManager())
            {
                validatedName = viewControlManager->createViewControlUniqueName(targetStep->m_ImageName);
            }
        }
        targetStep->m_ImageName = validatedName;
        m_Host.setHistoryName(validatedName);
    }

    if (restore->removed.has_value())
    {
        const auto removedStep = m_ImageHistory.at(*restore->removed - 1);
        m_ImageHistory.erase(m_ImageHistory.begin() + static_cast<std::ptrdiff_t>(*restore->removed - 1));
        releaseDroppedSteps({removedStep});
    }
    m_CurrentStep = restore->target;

    m_Host.historyChanged();
    notifyActiveImageHistoryChanged();
    notifyHistoryUpdated();
    m_Host.activateForHistory();
}

void ImageHistoryTracker::releaseDroppedSteps(const std::vector<ImageHistoryStepSPtr>& droppedSteps)
{
    std::set<an::ui::history::Snapshot*> released;
    for (const auto& step : droppedSteps)
    {
        if (!step || !step->m_Snapshot || usesSnapshot(step->m_Snapshot) ||
            !released.insert(step->m_Snapshot.get()).second)
        {
            continue;
        }
        // A snapshot still waiting for the writer is dropped from the queue; one being written is removed when its
        // write ends. Nothing here waits for it.
        m_Store->orphan(step->m_Snapshot);
    }
}

void ImageHistoryTracker::waitAllAsyncJobsDone() const
{
    for (const auto& step : m_ImageHistory)
    {
        step->waitAsyncJobsDone();
    }

    if (m_InitialStep)
    {
        m_InitialStep->waitAsyncJobsDone();
    }
}

const an::ui::history::BackupQueue& ImageHistoryTracker::backupQueue() const
{
    return m_Store->queue();
}

std::string ImageHistoryTracker::snapshotDirectory() const
{
    return m_Store->directory();
}

const ImageHistoryLineage& ImageHistoryTracker::lineage() const
{
    return m_Lineage;
}

void ImageHistoryTracker::setLineage(ImageHistoryLineage lineage)
{
    m_Lineage = std::move(lineage);
}

std::string ImageHistoryTracker::getNextStepName() const
{
    if (hasRedoSteps())
    {
        return m_ImageHistory.at(m_CurrentStep)->getStepName();
    }

    return std::string();
}

ImageHistoryStepSPtr ImageHistoryTracker::getStepByIndex(const size_t stepIndex) const
{
    if (!m_InitialStep)
    {
        return nullptr;
    }

    if (stepIndex == 0)
    {
        return m_InitialStep;
    }

    if (stepIndex > m_ImageHistory.size())
    {
        return nullptr;
    }

    return m_ImageHistory.at(stepIndex - 1);
}

bool ImageHistoryTracker::usesSnapshot(const an::ui::history::SnapshotPtr& snapshot) const
{
    if (m_InitialStep && m_InitialStep->m_Snapshot == snapshot)
    {
        return true;
    }
    for (const auto& step : m_ImageHistory)
    {
        if (step->m_Snapshot == snapshot)
        {
            return true;
        }
    }
    return false;
}

void ImageHistoryTracker::reportFailure(const std::string& action, const std::string& details) const
{
    const std::string message = details.empty() ? fmt::format("Could NOT {}. Unknown error.", action)
                                                : fmt::format("Could NOT {}. Error message: {}", action, details);

    LOG_ERROR("History operation failed for '{}': {}", m_Host.historyName(), message);

    auto* parent = m_Host.historyDialogParent();
    if (!parent)
    {
        return;
    }
    if (auto* context = currentAppContext())
    {
        if (auto* dialogs = context->getDialogService())
        {
            dialogs->notifyErrorMessage(parent, QStringLiteral("Image History"), QString::fromStdString(message));
        }
    }
}

void ImageHistoryTracker::addListener(ImageHistoryTrackerListener* listener)
{
    m_Listeners.insert(listener);
}

void ImageHistoryTracker::removeListener(ImageHistoryTrackerListener* listener)
{
    m_Listeners.erase(listener);
}

void ImageHistoryTracker::notifyHistoryUpdated()
{
    // A listener may stop listening while it is told.
    const std::set<ImageHistoryTrackerListener*> listeners = m_Listeners;
    for (auto* listener : listeners)
    {
        if (m_Listeners.count(listener) != 0u)
        {
            listener->onImageHistoryUpdated();
        }
    }
}

ImageHistoryTracker::~ImageHistoryTracker()
{
    *m_Self = nullptr;
    if (m_Restore)
    {
        m_Restore->stop.request_stop();
    }
    // Snapshots still waiting for the writer are dropped, the one being written stops and leaves no file, and the
    // history's own directory goes with everything in it.
    m_Store->close();
}
