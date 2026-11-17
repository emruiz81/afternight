// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The undo history of one view: the timeline, the edit preparation that runs on an edit's worker, the restores read on
/// a worker and shown on the GUI thread, the directory the history's snapshots live in, and the home a project gives
/// it: entered, attached to what it holds, indexed, left.

#include "stdafx.hpp"
#include "ImageHistoryTracker.h"
#include "ImageHistoryTrackerUtils.h"
#include "controls/IAppContext.h"
#include "controls/ProcessCompanions.h"

#include <AfterNightConsumerRuntime/OperationScope.h>

#include <AfterNightEngine/Core/ErrorCode.h>

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QMetaObject>
#include <QThreadPool>
#include <QUuid>

#include <algorithm>
#include <atomic>
#include <filesystem>
#include <map>
#include <mutex>
#include <set>
#include <shared_mutex>
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

[[nodiscard]] std::filesystem::path pathOfUtf8(const std::string& path)
{
    return std::filesystem::path(std::u8string(path.begin(), path.end()));
}

[[nodiscard]] QString qtPathOf(const std::filesystem::path& path)
{
    const std::u8string text = path.generic_u8string();
    return QString::fromUtf8(reinterpret_cast<const char*>(text.data()), static_cast<qsizetype>(text.size()));
}

/// A directory as two spellings of one place compare equal: normalized, and without case where the host has none.
[[nodiscard]] std::string placeKey(const std::filesystem::path& directory)
{
    QString text = QDir::cleanPath(qtPathOf(directory));
#ifdef Q_OS_WIN
    text = text.toLower();
#endif
    return text.toStdString();
}

void removeFile(const std::string& path)
{
    std::error_code ignored;
    std::filesystem::remove(pathOfUtf8(path), ignored);
}

/// One step as the GUI thread drafts the index: what the index says of it, and the snapshot whose file it names.
struct DraftStep
{
    HistoryIndexStep listed;
    SnapshotPtr snapshot;
};

/// What the index is written from: the steps in order, the step shown, the step the file equals, and the file.
struct IndexDraft
{
    std::vector<DraftStep> steps;
    QString current;
    QString saved;
    QString file;
    std::optional<FileIdentity> identity;
};

/// The index a draft comes to now, for a directory: the inherited steps, and the steps whose files are written there.
[[nodiscard]] HistoryIndex indexOf(const IndexDraft& draft, const std::filesystem::path& directory)
{
    const std::string place = placeKey(directory);
    HistoryIndex index;
    index.current = draft.current;
    index.saved = draft.saved;
    index.file = draft.file;
    index.identity = draft.identity;
    for (const DraftStep& step : draft.steps)
    {
        HistoryIndexStep listed = step.listed;
        if (!listed.inherited)
        {
            if (!step.snapshot || step.snapshot->state() != SnapshotState::Written)
            {
                continue;
            }
            const std::filesystem::path file = pathOfUtf8(step.snapshot->path());
            if (placeKey(file.parent_path()) != place)
            {
                continue;
            }
            listed.file = qtPathOf(file.filename());
            listed.run = QString::fromStdString(step.snapshot->identity().runId());
            listed.bytes = step.snapshot->fileBytes();
        }
        index.steps.push_back(std::move(listed));
    }
    return index;
}

/// Sets a kept history aside: everything directly in its directory goes into a folder of its own under `detached`,
/// where its steps still open as images.
[[nodiscard]] bool setHistoryAside(const QString& directory)
{
    const QDir home(directory);
    const QString aside =
        home.filePath(QString::fromLatin1(kDetachedHistoryFolder) + QLatin1Char('/') +
                      QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd-HHmmss-zzz")));
    if (!QDir().mkpath(aside))
    {
        return false;
    }
    bool moved = true;
    const QStringList files = home.entryList(QDir::Files | QDir::Hidden);
    for (const QString& name : files)
    {
        moved = QFile::rename(home.filePath(name), QDir(aside).filePath(name)) && moved;
    }
    return moved;
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

/// What one history shares with the workers it starts: the session, the backup writer, the directory its steps are
/// written in - its own, or a home's - the index of a home, and the edits prepared on their workers. Thread-safe.
class HistoryStore : public std::enable_shared_from_this<HistoryStore>
{
public:
    HistoryStore(const an::consumer::EngineSession& session, StepCodecPtr codec, const QString& baseDirectory)
        : m_Session(session), m_RunId(newToken()), m_OwnDirectory(pathOf(baseDirectory) / ("AN_HISTORY_" + m_RunId)),
          m_OutsideCodec(codec ? std::move(codec) : historyStepCodec()), m_Directory(m_OwnDirectory),
          m_Queue(session, m_OutsideCodec, [this](const Snapshot& snapshot) { onSettled(snapshot); })
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

    /// The directory a step recorded now is written in.
    [[nodiscard]] std::filesystem::path directory() const
    {
        std::lock_guard lock(m_Mutex);
        return m_Directory;
    }

    /// The directory of the history's own, which goes with it.
    [[nodiscard]] const std::filesystem::path& ownDirectory() const
    {
        return m_OwnDirectory;
    }

    /// Held shared while a step's file is read and alone while one is moved, so a read never meets half a move.
    [[nodiscard]] std::shared_mutex& files()
    {
        return m_Files;
    }

    /// A new path in the directory steps are written in and the identity the snapshot written there is for; nothing
    /// once the history is closed.
    [[nodiscard]] std::optional<std::pair<std::string, an::ArtifactIdentity>> newSnapshot()
    {
        std::lock_guard lock(m_Mutex);
        if (m_Closed)
        {
            return std::nullopt;
        }
        if (m_MadeDirectories.count(placeKey(m_Directory)) == 0u)
        {
            std::error_code error;
            std::filesystem::create_directories(m_Directory, error);
            if (error)
            {
                return std::nullopt;
            }
            m_MadeDirectories.insert(placeKey(m_Directory));
        }
        std::string artifactId = newToken();
        an::Result<an::ArtifactIdentity> identity = an::ArtifactIdentity::create(artifactId, m_RunId);
        if (!identity.hasValue())
        {
            return std::nullopt;
        }
        const StepCodecPtr codec = m_Queue.codec();
        return std::make_pair(utf8Of(m_Directory / (artifactId + std::string(codec->fileExtension()))),
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

    /// Removes a step's file, unless it is in a home the history has left: what a home holds when it is left stays.
    void removeStepFile(const std::string& path)
    {
        if (path.empty())
        {
            return;
        }
        {
            std::lock_guard lock(m_Mutex);
            if (m_LeftPlaces.count(placeKey(pathOfUtf8(path).parent_path())) != 0u)
            {
                return;
            }
        }
        removeFile(path);
    }

    /// Lets a snapshot no step uses go: off the queue if it has not started, and its file removed now or as soon as
    /// its write ends.
    void orphan(const SnapshotPtr& snapshot)
    {
        snapshot->drop();
        m_Queue.discard(snapshot);
        if (snapshot->state() != SnapshotState::Pending)
        {
            removeStepFile(snapshot->path());
        }
    }

    /// Whether the history is gone.
    [[nodiscard]] bool closed() const
    {
        std::lock_guard lock(m_Mutex);
        return m_Closed;
    }

    // ---- A home

    /// Whether the steps are kept in a home.
    [[nodiscard]] bool kept() const
    {
        std::lock_guard lock(m_Mutex);
        return m_Kept;
    }

    /// Writes the steps recorded from now on into a home, through its codec. The home's index is not written until
    /// `openIndex`: what the home held is read first.
    ///
    /// @param claim what says the home is this history's, held until the history has left it.
    void enter(const std::filesystem::path& home, StepCodecPtr codec, std::shared_ptr<void> claim)
    {
        {
            std::lock_guard lock(m_Mutex);
            m_Directory = home;
            m_Kept = true;
            m_IndexOpen = false;
            m_IndexClosed = false;
            m_LeftPlaces.erase(placeKey(home));
            m_HomeClaim = std::move(claim);
        }
        m_Queue.setCodec(std::move(codec));
    }

    /// Lets the home's index be written, and writes it.
    void openIndex()
    {
        {
            std::lock_guard lock(m_Mutex);
            if (!m_Kept)
            {
                return;
            }
            m_IndexOpen = true;
        }
        scheduleIndexWrite();
    }

    /// What the index is written from, from now on. Never waits: the index is written on a worker.
    void publish(IndexDraft draft)
    {
        bool write = false;
        {
            std::lock_guard lock(m_Mutex);
            m_Draft = std::move(draft);
            write = m_Kept && m_IndexOpen && !m_Closed;
        }
        if (write)
        {
            scheduleIndexWrite();
        }
    }

    /// Writes the steps recorded from now on into the history's own directory again. The home keeps what it holds:
    /// the steps still on their way there are written, its index is written a last time after them, on a worker, and
    /// nothing of it is removed by this history afterwards.
    void leave()
    {
        std::filesystem::path home;
        IndexDraft draft;
        bool indexed = false;
        std::shared_ptr<void> claim;
        {
            std::lock_guard lock(m_Mutex);
            if (!m_Kept)
            {
                return;
            }
            home = m_Directory;
            draft = m_Draft;
            indexed = m_IndexOpen;
            m_LeftPlaces.insert(placeKey(home));
            m_Directory = m_OwnDirectory;
            m_Kept = false;
            m_IndexOpen = false;
            claim = std::move(m_HomeClaim);
        }
        m_Queue.setCodec(m_OutsideCodec);
        if (!indexed)
        {
            // Nothing of this history was listed there: the home is free at once, as the claim goes.
            return;
        }
        // The home stays this history's until its last write there has landed.
        const QString directory = qtPathOf(home);
        detail::noteHistoryClosing(directory);
        QThreadPool::globalInstance()->start([self = shared_from_this(), home, directory, draft]() {
            const std::string place = placeKey(home);
            for (const DraftStep& step : draft.steps)
            {
                if (step.snapshot && placeKey(pathOfUtf8(step.snapshot->path()).parent_path()) == place)
                {
                    (void)step.snapshot->waitUntilSettled();
                }
            }
            {
                std::lock_guard index(self->m_IndexWrite);
                if (!writeHistoryIndex(directory, indexOf(draft, home)))
                {
                    LOG_WARNING("Image history: the index of '{}' could not be written", directory.toStdString());
                }
            }
            detail::noteHistoryClosed(directory);
        });
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
        std::filesystem::remove_all(m_OwnDirectory, ignored);
    }

    /// Ends a history whose steps are kept: nothing more is recorded, the steps still waiting are written, the index
    /// is written after the last of them, and the history's own directory goes - all on a worker, behind the caller.
    void closeKept()
    {
        std::filesystem::path home;
        {
            std::lock_guard lock(m_Mutex);
            m_Closed = true;
            home = m_Directory;
        }
        const QString directory = qtPathOf(home);
        detail::noteHistoryClosing(directory);
        QThreadPool::globalInstance()->start([self = shared_from_this(), directory]() {
            self->m_Queue.finish();
            self->writeIndex(true);
            std::error_code ignored;
            std::filesystem::remove_all(self->m_OwnDirectory, ignored);
            detail::noteHistoryClosed(directory);
        });
    }

    /// Removes the history's own directory if it holds nothing: its steps moved into a home.
    void reclaimOwnDirectoryIfEmpty()
    {
        std::error_code error;
        if (std::filesystem::is_directory(m_OwnDirectory, error) && std::filesystem::is_empty(m_OwnDirectory, error) &&
            !error)
        {
            std::filesystem::remove(m_OwnDirectory, error);
            std::lock_guard lock(m_Mutex);
            m_MadeDirectories.erase(placeKey(m_OwnDirectory));
        }
    }

private:
    void scheduleIndexWrite()
    {
        // One write waiting is enough: it writes whatever the draft is when it runs.
        if (m_IndexWriteWaiting.exchange(true))
        {
            return;
        }
        QThreadPool::globalInstance()->start([self = shared_from_this()]() { self->writeIndex(false); });
    }

    /// Writes the home's index from the draft as it is now; `last` closes it, so a write still waiting writes nothing
    /// over what a later history writes there.
    void writeIndex(const bool last)
    {
        m_IndexWriteWaiting.store(false);
        std::lock_guard index(m_IndexWrite);
        IndexDraft draft;
        std::filesystem::path home;
        {
            std::lock_guard lock(m_Mutex);
            if (!m_Kept || !m_IndexOpen || m_IndexClosed)
            {
                return;
            }
            draft = m_Draft;
            home = m_Directory;
            m_IndexClosed = last;
        }
        const QString directory = qtPathOf(home);
        if (!writeHistoryIndex(directory, indexOf(draft, home)))
        {
            LOG_WARNING("Image history: the index of '{}' could not be written", directory.toStdString());
        }
    }

    // Called on the writer's thread once a snapshot's write has ended.
    void onSettled(const Snapshot& snapshot)
    {
        bool closed = false;
        bool kept = false;
        {
            std::lock_guard lock(m_Mutex);
            closed = m_Closed;
            kept = m_Kept;
        }
        if (snapshot.dropped())
        {
            removeStepFile(snapshot.path());
            return;
        }
        if (snapshot.state() == SnapshotState::Failed && !closed)
        {
            // The step stays, and says it cannot be restored.
            reportFromAnyThread(fmt::format(
                "Could NOT keep the snapshot of {}; undo and redo cannot restore that step. Error message: {}",
                snapshot.label(), snapshot.failure()));
        }
        else if (snapshot.state() == SnapshotState::Failed && kept)
        {
            // Behind a view that closed there is nobody to tell: the step is not in the home, and its index says so
            // by not listing it. An application that is closing admits no work, so a step still waiting then ends
            // here.
            LOG_WARNING("Image history: the snapshot of {} was not kept after its view closed: {}", snapshot.label(),
                        snapshot.failure());
        }
        if (kept)
        {
            // The step's file has landed: the index lists it.
            writeIndex(false);
        }
    }

    const an::consumer::EngineSession& m_Session;
    const std::string m_RunId;
    const std::filesystem::path m_OwnDirectory;
    const StepCodecPtr m_OutsideCodec;

    mutable std::mutex m_Mutex;
    std::filesystem::path m_Directory;
    bool m_Kept = false;
    bool m_IndexOpen = false;
    bool m_IndexClosed = false;
    bool m_Closed = false;
    std::set<std::string> m_MadeDirectories;
    std::set<std::string> m_LeftPlaces;
    std::unordered_map<std::uint64_t, std::weak_ptr<void>> m_Prepared;
    IndexDraft m_Draft;
    // What says the home is this history's.
    std::shared_ptr<void> m_HomeClaim;

    // One index is written at a time, and one write waits at most.
    std::mutex m_IndexWrite;
    std::atomic<bool> m_IndexWriteWaiting{false};

    std::shared_mutex m_Files;

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
        const std::shared_ptr<HistoryStore> held = store.lock();
        if (held)
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
                    (*written)->drop();
                    if (held)
                    {
                        held->removeStepFile((*written)->path());
                    }
                    else
                    {
                        removeFile((*written)->path());
                    }
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

/// One entry into a home, from the call to its outcome.
struct ImageHistoryTracker::Entry
{
    an::ui::history::HistoryHome home;
    an::ui::history::StepsMove move = an::ui::history::StepsMove::Move;
    an::ui::history::IHistoryPlacement::EnterDone done;
    an::ui::history::IHistoryPlacement::MoveProgress progress;
    an::ui::history::HistoryEntry outcome;
    std::stop_source stop;
};

/// What a home holds when it is entered, as a worker read it.
struct ImageHistoryTracker::HomeContents
{
    /// The index of the history kept there; nothing when there is none, or when it was set aside.
    std::optional<an::ui::history::HistoryIndex> index;

    /// For each step of the index, whether its file is there; an inherited step is.
    std::vector<bool> present;

    /// The identity of the image's file now.
    std::optional<an::ui::history::FileIdentity> identity;

    /// What says the home is this history's from here on; it gives the home back when the entry is given up.
    std::shared_ptr<void> claim;

    bool detached = false;
    QString problem;
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
    // Whether this state is the one a file holds is known once what made the view has returned: a view is told it
    // shows its file after it is given its image.
    const std::shared_ptr<ImageHistoryTracker*> self = m_Self;
    if (QCoreApplication* application = QCoreApplication::instance())
    {
        QMetaObject::invokeMethod(
            application,
            [self]() {
                if (*self != nullptr && (*self)->m_ImageHistory.empty())
                {
                    (*self)->noteStateTheFileEquals();
                    (*self)->publishIndex();
                }
            },
            Qt::QueuedConnection);
    }
    publishIndex();
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
            // A step's file is not moved while it is read.
            const std::shared_lock files(store->files());
            return store->queue().codec()->read(store->session(), snapshot->path(), snapshot->identity(),
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
    publishIndex();

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
    // The state the new step follows is the one the file holds, when the view still shows its file.
    noteStateTheFileEquals();

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
    publishIndex();

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
        const an::ErrorCode code = read.status().code();
        if (restore->targetStep->m_Stored &&
            (code == an::ErrorCode::InvalidData || code == an::ErrorCode::ExternalFailure))
        {
            // A step an earlier session kept that does not read now: it leaves the history, and the others stand.
            leaveOutStoredStep(restore->targetStep, read.status().message());
        }
        else if (code != an::ErrorCode::Cancelled)
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
    publishIndex();

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
    return an::ui::history::utf8Of(m_Store->directory());
}

const ImageHistoryLineage& ImageHistoryTracker::lineage() const
{
    return m_Lineage;
}

void ImageHistoryTracker::setLineage(ImageHistoryLineage lineage)
{
    m_Lineage = std::move(lineage);
    publishIndex();
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

// ---- A home

void ImageHistoryTracker::enterHome(an::ui::history::HistoryHome home, const an::ui::history::StepsMove move,
                                    an::ui::history::IHistoryPlacement::EnterDone done,
                                    an::ui::history::IHistoryPlacement::MoveProgress progress)
{
    using namespace an::ui::history;
    // Entering the home the history is in changes nothing.
    if (!m_Entry && m_Home.has_value() && placeKey(pathOf(m_Home->directory)) == placeKey(pathOf(home.directory)))
    {
        if (done)
        {
            HistoryEntry entry;
            entry.entered = true;
            done(entry);
        }
        return;
    }
    if (m_Entry || m_Home.has_value())
    {
        leaveHome();
    }

    auto entry = std::make_shared<Entry>();
    entry->home = std::move(home);
    entry->move = move;
    entry->done = std::move(done);
    entry->progress = std::move(progress);
    m_Entry = entry;

    // What the home holds is read on a worker, and nothing of this history is written there until it has been.
    const std::shared_ptr<ImageHistoryTracker*> self = m_Self;
    QThreadPool::globalInstance()->start([self, entry]() {
        const HistoryHome& where = entry->home;
        const std::stop_token stop = entry->stop.get_token();
        auto contents = std::make_shared<HomeContents>();
        // The home is one history's at a time. A history that closed there may still be writing: what it leaves is
        // read once it has. One a view has open keeps it, and this history stays the session's.
        bool kept = false;
        contents->claim = detail::claimHistoryHome(where.directory, detail::HomeUse::History, stop, kept);
        if (!contents->claim && !kept)
        {
            return;
        }
        if (!contents->claim)
        {
            contents->problem = QStringLiteral("another view of this image keeps its history in '%1'")
                                    .arg(QDir::toNativeSeparators(where.directory));
        }
        else if (stop.stop_requested())
        {
            return;
        }
        else if (!QDir().mkpath(where.directory))
        {
            contents->problem =
                QStringLiteral("the folder '%1' could not be made").arg(QDir::toNativeSeparators(where.directory));
        }
        else
        {
            const QDir directory(where.directory);
            const bool indexThere = QFileInfo::exists(directory.filePath(QString::fromLatin1(kHistoryIndexFileName)));
            contents->index = readHistoryIndex(where.directory);
            if (!where.file.isEmpty())
            {
                contents->identity = readFileIdentity(where.file);
            }
            // A history kept for a file that is no longer that file is not this image's; neither is one whose index
            // does not read. Either is kept aside, whole.
            const bool unreadable = indexThere && !contents->index.has_value();
            const bool changed = contents->index.has_value() && contents->index->identity.has_value() &&
                                 contents->identity != contents->index->identity;
            if (unreadable || changed)
            {
                if (!setHistoryAside(where.directory))
                {
                    contents->problem = QStringLiteral("the history kept in '%1' could not be set aside")
                                            .arg(QDir::toNativeSeparators(where.directory));
                }
                contents->index.reset();
                contents->detached = true;
            }
            if (contents->problem.isEmpty())
            {
                (void)sweepHistoryDirectory(where.directory, contents->index.has_value() ? &*contents->index : nullptr);
                if (contents->index.has_value())
                {
                    for (const HistoryIndexStep& step : contents->index->steps)
                    {
                        contents->present.push_back(step.inherited ||
                                                    QFileInfo(directory.filePath(step.file)).isFile());
                    }
                }
            }
        }
        QCoreApplication* application = QCoreApplication::instance();
        if (application == nullptr)
        {
            return;
        }
        QMetaObject::invokeMethod(
            application,
            [self, entry, contents]() {
                if (*self != nullptr)
                {
                    (*self)->continueEntry(entry, std::move(*contents));
                }
            },
            Qt::QueuedConnection);
    });
}

void ImageHistoryTracker::continueEntry(const std::shared_ptr<Entry>& entry, HomeContents&& contents)
{
    using namespace an::ui::history;
    if (m_Entry != entry)
    {
        return;
    }
    if (!contents.problem.isEmpty())
    {
        m_Entry.reset();
        entry->outcome.problem = contents.problem;
        LOG_WARNING("Image history: the history of '{}' stays in the session's folder: {}", m_Host.historyName(),
                    contents.problem.toStdString());
        if (entry->done)
        {
            entry->done(entry->outcome);
        }
        return;
    }

    // From here on a step that is recorded is written into the home.
    const std::filesystem::path home = pathOf(entry->home.directory);
    m_Store->enter(home, entry->home.codec ? entry->home.codec : historyStepCodec(entry->home.compression),
                   std::move(contents.claim));
    m_Home = entry->home;
    if (!entry->home.file.isEmpty())
    {
        m_SavedFile = entry->home.file;
        m_FileIdentity = contents.identity;
        ++m_IdentityReads;
    }
    entry->outcome.detached = contents.detached;
    if (contents.index.has_value())
    {
        attachStoredHistory(*contents.index, contents.present, entry->outcome);
    }
    noteStateTheFileEquals();

    // The steps the history already has, whose files are elsewhere, go in when the caller says so.
    std::vector<SnapshotPtr> moving;
    if (entry->move == StepsMove::Move)
    {
        const std::string place = placeKey(home);
        std::set<const Snapshot*> seen;
        const auto take = [&moving, &seen, &place](const ImageHistoryStepSPtr& step) {
            if (!step || !step->m_Snapshot || !seen.insert(step->m_Snapshot.get()).second)
            {
                return;
            }
            const SnapshotState state = step->m_Snapshot->state();
            if ((state == SnapshotState::Pending || state == SnapshotState::Written) &&
                placeKey(pathOfUtf8(step->m_Snapshot->path()).parent_path()) != place)
            {
                moving.push_back(step->m_Snapshot);
            }
        };
        take(m_InitialStep);
        for (const ImageHistoryStepSPtr& step : m_ImageHistory)
        {
            take(step);
        }
    }
    if (moving.empty())
    {
        finishEntry(entry);
        return;
    }

    const std::shared_ptr<ImageHistoryTracker*> self = m_Self;
    const std::shared_ptr<HistoryStore> store = m_Store;
    QThreadPool::globalInstance()->start([self, entry, store, moving, home]() {
        const std::stop_token stop = entry->stop.get_token();
        // A step moves once its write has ended, as the file it is: renamed on one volume, copied across volumes,
        // never read as a step and never written again.
        std::uint64_t total = 0u;
        for (const SnapshotPtr& snapshot : moving)
        {
            if (snapshot->waitUntilSettled(stop) == SnapshotState::Written)
            {
                total += snapshot->fileBytes();
            }
        }
        auto moved = std::make_shared<HistoryEntry>();
        for (const SnapshotPtr& snapshot : moving)
        {
            if (stop.stop_requested())
            {
                break;
            }
            if (snapshot->state() != SnapshotState::Written || snapshot->dropped())
            {
                continue;
            }
            const std::filesystem::path from = pathOfUtf8(snapshot->path());
            const std::filesystem::path to = home / from.filename();
            std::optional<StepFileMove> how;
            {
                const std::unique_lock files(store->files());
                how = moveStepFile(from, to);
                if (how.has_value())
                {
                    snapshot->moveTo(utf8Of(to));
                }
            }
            if (!how.has_value())
            {
                moved->problem = QStringLiteral("the step file '%1' could not be moved").arg(qtPathOf(from));
                continue;
            }
            if (snapshot->dropped())
            {
                // The step went while its file moved: the file goes after it.
                store->removeStepFile(utf8Of(to));
                continue;
            }
            ++moved->stepsMoved;
            moved->stepsCopied += *how == StepFileMove::Copied ? 1u : 0u;
            moved->bytesMoved += snapshot->fileBytes();
            if (entry->progress)
            {
                entry->progress(moved->bytesMoved, total);
            }
        }
        store->reclaimOwnDirectoryIfEmpty();
        QCoreApplication* application = QCoreApplication::instance();
        if (application == nullptr)
        {
            return;
        }
        QMetaObject::invokeMethod(
            application,
            [self, entry, moved]() {
                if (*self != nullptr)
                {
                    entry->outcome.stepsMoved = moved->stepsMoved;
                    entry->outcome.stepsCopied = moved->stepsCopied;
                    entry->outcome.bytesMoved = moved->bytesMoved;
                    entry->outcome.problem = moved->problem;
                    (*self)->finishEntry(entry);
                }
            },
            Qt::QueuedConnection);
    });
}

void ImageHistoryTracker::finishEntry(const std::shared_ptr<Entry>& entry)
{
    if (m_Entry != entry)
    {
        return;
    }
    m_Entry.reset();
    entry->outcome.entered = true;
    if (!entry->outcome.problem.isEmpty())
    {
        LOG_WARNING("Image history: '{}': {}", m_Host.historyName(), entry->outcome.problem.toStdString());
    }
    // The index lists what the home holds from now on.
    m_Store->openIndex();
    publishIndex();
    if (entry->done)
    {
        entry->done(entry->outcome);
    }
}

void ImageHistoryTracker::attachStoredHistory(const an::ui::history::HistoryIndex& index,
                                              const std::vector<bool>& present, an::ui::history::HistoryEntry& outcome)
{
    using namespace an::ui::history;
    const QDir home(m_Home->directory);

    // The steps the index lists whose files are there, in its order, and where the step the file equals falls
    // among them.
    std::vector<ImageHistoryStepSPtr> stored;
    std::map<QString, SnapshotPtr> byFile;
    ImageHistoryLineage lineage;
    std::optional<std::size_t> savedAt;
    std::size_t behindSaved = 0u;
    for (std::size_t listedAt = 0u; listedAt < index.steps.size(); ++listedAt)
    {
        const HistoryIndexStep& listed = index.steps[listedAt];
        if (listed.inherited)
        {
            lineage.appendEntry(LineageEntry{.step_name = listed.name.toStdString(),
                                             .description = listed.description.toStdString(),
                                             .image_name = listed.image.toStdString(),
                                             .timestamp = listed.time.toLocalTime(),
                                             .source_file = {}});
            continue;
        }
        const bool saved = !index.saved.isEmpty() && listed.id == index.saved;
        SnapshotPtr snapshot;
        if (listedAt < present.size() && present[listedAt])
        {
            const auto shared = byFile.find(listed.file);
            if (shared != byFile.end())
            {
                snapshot = shared->second;
            }
            else
            {
                // The identity the step was written for is its file's base name and the run that wrote it.
                an::Result<an::ArtifactIdentity> identity = an::ArtifactIdentity::create(
                    QFileInfo(listed.file).completeBaseName().toStdString(), listed.run.toStdString());
                if (identity.hasValue())
                {
                    snapshot =
                        Snapshot::written(home.filePath(listed.file).toUtf8().toStdString(),
                                          snapshotLabel(listed.name.toStdString()), std::move(*identity), listed.bytes);
                    byFile.emplace(listed.file, snapshot);
                }
            }
        }
        if (!snapshot)
        {
            // Its file is gone, or names no step: the step is left out and the others stand.
            ++outcome.stepsLeftOut;
            if (saved)
            {
                behindSaved = stored.size();
            }
            continue;
        }
        auto step = std::make_shared<ImageHistoryStep>(listed.id.toStdString(), listed.name.toStdString(),
                                                       listed.description.toStdString());
        step->m_ImageName = listed.image.toStdString();
        step->m_Snapshot = snapshot;
        if (listed.time.isValid())
        {
            step->m_CreationTime = listed.time.toLocalTime();
        }
        step->m_Stored = true;
        if (saved)
        {
            savedAt = stored.size();
        }
        stored.push_back(std::move(step));
    }
    if (m_Lineage.empty() && !lineage.empty())
    {
        m_Lineage = std::move(lineage);
    }
    if (stored.empty())
    {
        return;
    }

    // Nothing is restored while the timeline is put together.
    if (m_Restore)
    {
        m_Restore->stop.request_stop();
        m_Restore.reset();
    }

    // The kept history goes around the image as it was opened: the steps up to the one the file equals behind it,
    // and the ones left unsaved ahead - unless the image was edited since, which is an edit made after an undo and
    // drops what was ahead. The state the view was opened with is the file's, so the kept step that equals the file
    // stands for it; with no such step, the opened state stands where that step was.
    const ImageHistoryStepSPtr opened = m_InitialStep;
    const std::vector<ImageHistoryStepSPtr> recorded(m_ImageHistory.begin(), m_ImageHistory.end());
    std::vector<ImageHistoryStepSPtr> timeline;
    std::vector<ImageHistoryStepSPtr> droppedSteps;
    std::size_t ahead = 0u;
    std::size_t current = m_CurrentStep;
    if (savedAt.has_value())
    {
        timeline.assign(stored.begin(), stored.begin() + static_cast<std::ptrdiff_t>(*savedAt + 1u));
        ahead = *savedAt + 1u;
        current += *savedAt;
        m_SavedStep = stored[*savedAt]->getUuid();
        if (opened)
        {
            droppedSteps.push_back(opened);
        }
    }
    else
    {
        timeline.assign(stored.begin(), stored.begin() + static_cast<std::ptrdiff_t>(behindSaved));
        ahead = behindSaved;
        current += behindSaved;
        if (opened)
        {
            timeline.push_back(opened);
            m_SavedStep = opened->getUuid();
        }
    }
    if (recorded.empty())
    {
        timeline.insert(timeline.end(), stored.begin() + static_cast<std::ptrdiff_t>(ahead), stored.end());
    }
    else
    {
        timeline.insert(timeline.end(), recorded.begin(), recorded.end());
        droppedSteps.insert(droppedSteps.end(), stored.begin() + static_cast<std::ptrdiff_t>(ahead), stored.end());
    }
    if (timeline.empty())
    {
        return;
    }
    m_InitialStep = timeline.front();
    m_ImageHistory.assign(timeline.begin() + 1, timeline.end());
    m_CurrentStep = (std::min)(current, m_ImageHistory.size());
    // The history keeps as many steps as it ever does: the oldest go first, never the one shown.
    while (m_ImageHistory.size() >= m_MaxSteps && m_CurrentStep > 0u)
    {
        droppedSteps.push_back(m_InitialStep);
        m_InitialStep = m_ImageHistory.front();
        m_ImageHistory.pop_front();
        --m_CurrentStep;
    }
    releaseDroppedSteps(droppedSteps);
    outcome.attached = true;

    m_Host.historyChanged();
    notifyActiveImageHistoryChanged();
    notifyHistoryUpdated();
}

void ImageHistoryTracker::leaveHome()
{
    if (m_Entry)
    {
        // An entry on its way is given up: what it moved so far stays where it is.
        const std::shared_ptr<Entry> entry = std::move(m_Entry);
        m_Entry.reset();
        entry->stop.request_stop();
        if (entry->done)
        {
            entry->outcome.problem = QStringLiteral("the history was given another place before it had entered");
            entry->done(entry->outcome);
        }
    }
    m_Store->leave();
    m_Home.reset();
}

std::optional<an::ui::history::HistoryHome> ImageHistoryTracker::home() const
{
    return m_Home;
}

bool ImageHistoryTracker::isEnteringHome() const
{
    return m_Entry != nullptr;
}

void ImageHistoryTracker::fileSaved(const QString& path)
{
    if (path.isEmpty())
    {
        return;
    }
    if (const ImageHistoryStepSPtr current = getStepByIndex(m_CurrentStep))
    {
        m_SavedStep = current->getUuid();
    }
    m_SavedFile = path;
    if (m_Home.has_value())
    {
        m_Home->file = path;
    }
    // The identity stated until the new one is read is the old one: a session that ends in between finds a file
    // that is not the one recorded, and keeps the history aside rather than attach it to another file.
    if (m_Home.has_value() || m_Entry)
    {
        refreshFileIdentity();
    }
    publishIndex();
}

std::string ImageHistoryTracker::savedStep() const
{
    return m_SavedStep;
}

std::uint64_t ImageHistoryTracker::stepFileBytes() const
{
    std::set<const an::ui::history::Snapshot*> seen;
    std::uint64_t bytes = 0u;
    const auto add = [&seen, &bytes](const ImageHistoryStepSPtr& step) {
        if (step && step->m_Snapshot && seen.insert(step->m_Snapshot.get()).second)
        {
            bytes += step->m_Snapshot->fileBytes();
        }
    };
    add(m_InitialStep);
    for (const ImageHistoryStepSPtr& step : m_ImageHistory)
    {
        add(step);
    }
    return bytes;
}

std::pair<size_t, size_t> ImageHistoryTracker::compactionRange(const an::ui::history::HistoryCompaction& what) const
{
    const size_t total = getTotalStepCount();
    if (total == 0u)
    {
        return {0u, 0u};
    }
    if (what.keep == an::ui::history::HistoryCompaction::Keep::CurrentState)
    {
        return {m_CurrentStep, m_CurrentStep};
    }
    // The last steps, and the one shown with those after it when it is older than that.
    const size_t kept = (std::max)(what.steps, size_t{1});
    return {(std::min)(m_CurrentStep, total > kept ? total - kept : size_t{0}), total - 1u};
}

an::ui::history::HistoryCompactionResult
ImageHistoryTracker::planCompaction(const an::ui::history::HistoryCompaction& what) const
{
    an::ui::history::HistoryCompactionResult result;
    if (!m_InitialStep)
    {
        return result;
    }
    const auto [first, last] = compactionRange(what);
    std::set<const an::ui::history::Snapshot*> keptSnapshots;
    for (size_t step = first; step <= last; ++step)
    {
        if (const ImageHistoryStepSPtr kept = getStepByIndex(step); kept && kept->m_Snapshot)
        {
            keptSnapshots.insert(kept->m_Snapshot.get());
        }
    }
    std::set<const an::ui::history::Snapshot*> freed;
    for (size_t step = 0u; step < getTotalStepCount(); ++step)
    {
        if (step >= first && step <= last)
        {
            continue;
        }
        ++result.stepsRemoved;
        const ImageHistoryStepSPtr dropped = getStepByIndex(step);
        if (dropped && dropped->m_Snapshot && keptSnapshots.count(dropped->m_Snapshot.get()) == 0u &&
            freed.insert(dropped->m_Snapshot.get()).second)
        {
            result.bytesFreed += dropped->m_Snapshot->fileBytes();
        }
    }
    result.ok = true;
    return result;
}

an::ui::history::HistoryCompactionResult ImageHistoryTracker::compact(const an::ui::history::HistoryCompaction& what)
{
    if (m_Restore || m_Entry || !m_InitialStep)
    {
        return {};
    }
    const an::ui::history::HistoryCompactionResult result = planCompaction(what);
    const auto [first, last] = compactionRange(what);
    std::vector<ImageHistoryStepSPtr> kept;
    std::vector<ImageHistoryStepSPtr> droppedSteps;
    for (size_t step = 0u; step < getTotalStepCount(); ++step)
    {
        (step >= first && step <= last ? kept : droppedSteps).push_back(getStepByIndex(step));
    }
    m_InitialStep = kept.front();
    m_ImageHistory.assign(kept.begin() + 1, kept.end());
    m_CurrentStep -= first;
    releaseDroppedSteps(droppedSteps);
    publishIndex();

    m_Host.historyChanged();
    notifyActiveImageHistoryChanged();
    notifyHistoryUpdated();
    return result;
}

void ImageHistoryTracker::publishIndex()
{
    using namespace an::ui::history;
    if (!m_Store->kept())
    {
        return;
    }
    IndexDraft draft;
    std::size_t inherited = 0u;
    for (const LineageEntry& entry : m_Lineage.entries())
    {
        HistoryIndexStep listed;
        listed.id = QStringLiteral("inherited-%1").arg(inherited++);
        listed.name = QString::fromStdString(entry.step_name);
        listed.description = QString::fromStdString(entry.description);
        listed.image = QString::fromStdString(entry.image_name);
        listed.time = entry.timestamp;
        listed.inherited = true;
        draft.steps.push_back(DraftStep{.listed = std::move(listed), .snapshot = nullptr});
    }
    const auto list = [&draft](const ImageHistoryStepSPtr& step) {
        if (!step || !step->m_Snapshot)
        {
            return;
        }
        HistoryIndexStep listed;
        listed.id = QString::fromStdString(step->getUuid());
        listed.name = QString::fromStdString(step->getStepName());
        listed.description = QString::fromStdString(step->getStepDesc());
        listed.image = QString::fromStdString(step->m_ImageName);
        listed.time = step->m_CreationTime;
        draft.steps.push_back(DraftStep{.listed = std::move(listed), .snapshot = step->m_Snapshot});
    };
    list(m_InitialStep);
    for (const ImageHistoryStepSPtr& step : m_ImageHistory)
    {
        list(step);
    }
    if (const ImageHistoryStepSPtr current = getStepByIndex(m_CurrentStep))
    {
        draft.current = QString::fromStdString(current->getUuid());
    }
    draft.saved = QString::fromStdString(m_SavedStep);
    draft.file = m_SavedFile;
    draft.identity = m_FileIdentity;
    m_Store->publish(std::move(draft));
}

void ImageHistoryTracker::refreshFileIdentity()
{
    const std::uint64_t read = ++m_IdentityReads;
    const QString path = m_SavedFile;
    const std::shared_ptr<ImageHistoryTracker*> self = m_Self;
    QThreadPool::globalInstance()->start([self, path, read]() {
        auto identity =
            std::make_shared<std::optional<an::ui::history::FileIdentity>>(an::ui::history::readFileIdentity(path));
        QCoreApplication* application = QCoreApplication::instance();
        if (application == nullptr)
        {
            return;
        }
        QMetaObject::invokeMethod(
            application,
            [self, identity, read]() {
                // A later save's reading is the one that counts.
                if (*self != nullptr && (*self)->m_IdentityReads == read)
                {
                    (*self)->m_FileIdentity = *identity;
                    (*self)->publishIndex();
                }
            },
            Qt::QueuedConnection);
    });
}

void ImageHistoryTracker::noteStateTheFileEquals()
{
    if (!m_SavedStep.empty() || !m_Host.historyShowsItsFile())
    {
        return;
    }
    if (const ImageHistoryStepSPtr current = getStepByIndex(m_CurrentStep))
    {
        m_SavedStep = current->getUuid();
    }
}

void ImageHistoryTracker::leaveOutStoredStep(const ImageHistoryStepSPtr& step, const std::string& why)
{
    const size_t total = getTotalStepCount();
    for (size_t at = 0u; at < total; ++at)
    {
        if (getStepByIndex(at) != step || at == m_CurrentStep)
        {
            continue;
        }
        if (at == 0u)
        {
            m_InitialStep = m_ImageHistory.front();
            m_ImageHistory.pop_front();
        }
        else
        {
            m_ImageHistory.erase(m_ImageHistory.begin() + static_cast<std::ptrdiff_t>(at - 1u));
        }
        if (at < m_CurrentStep)
        {
            --m_CurrentStep;
        }
        releaseDroppedSteps({step});
        publishIndex();
        break;
    }
    reportFailure(
        fmt::format("read step '{}' of the history kept for this image; the step was left out", step->getStepName()),
        why);
    m_Host.historyChanged();
    notifyActiveImageHistoryChanged();
    notifyHistoryUpdated();
}

ImageHistoryTracker::~ImageHistoryTracker()
{
    *m_Self = nullptr;
    if (m_Restore)
    {
        m_Restore->stop.request_stop();
    }
    if (m_Entry)
    {
        m_Entry->stop.request_stop();
        // What reached the home before the view closed is listed there.
        m_Store->openIndex();
    }
    if (m_Store->kept())
    {
        // The steps stay: the ones still waiting are written and the index closed behind the view, on a worker.
        publishIndex();
        m_Store->closeKept();
        return;
    }
    // Snapshots still waiting for the writer are dropped, the one being written stops and leaves no file, and the
    // history's own directory goes with everything in it.
    m_Store->close();
}
