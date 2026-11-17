// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The undo history of one view: its timeline of steps, the snapshot each step restores, the lineage it inherited, and
/// the rules for branching, rolling over and removing steps.
///
/// A step is recorded when the view's image changes and holds what restoring it needs: the view's name, the sequence a
/// sequence view shows, and a snapshot - a copy of the image taken under a lease when the step is recorded and written
/// behind it by the history's backup writer, through the step codec the history was made with
/// (`ImageHistoryStepCodec.h`). A rename shares the snapshot of the state it renames; a sequence step that changed no
/// frame has none, because its sequence file is the state.
///
/// **An edit is recorded before it is shown, and waits for the history on its own worker.** What the edit replaces
/// must be kept - written, or written again from the edit's own lease when its first write failed - and the edit's
/// result must be admitted against the session's pending-snapshot allowance before the edit may be shown. Both waits
/// happen in `editPreparation`'s preparation, on the edit's worker; the GUI thread's `registerEditBeforePublishing`
/// never waits, and refuses an edit whose step cannot be kept, which the view then does not show.
///
/// **A restore is read on a worker and shown on the GUI thread.** Undo, redo, a jump and the removal of the current
/// step read the snapshot - a new asset with a new identity, so nothing derived from the image shown is taken for it -
/// and show it only while the view shows the document the restore began on; the current step moves when it is shown.
/// One restore runs at a time, and nothing is recorded while it does.
///
/// **The history writes only into a directory it owns and reclaims only that.** Its snapshots live in a directory of
/// their own below the application's temporary directory, named for this history and removed with it; a file the
/// history did not write - a document's own file, a swap file an earlier session left - is never read as a step and
/// never removed.
///
/// **A history given a home outlives its view** (`enterHome`, `ImageHistoryIndex.h`). Its steps are then written into
/// the home's directory, listed by the index its writer keeps beside them, and left there when the tracker goes: the
/// steps still waiting are written behind it on a worker, and the GUI thread waits for none of it. A home that already
/// holds a history of the same file gives it back - the step the file equals is the current one, the earlier steps are
/// behind it and the ones left unsaved ahead of it - and one whose file has changed since is set aside. The history
/// knows which of its steps the image's file equals (`fileSaved`), and the index says so.
///
/// Every member is called on the GUI thread unless it says otherwise.
#pragma once

#include "AfterNightControlsExport.h"

#include "ImageHistoryBackupQueue.h"
#include "ImageHistoryHost.h"
#include "ImageHistoryIndex.h"
#include "ImageHistoryLineage.h"

#include <AfterNightConsumerRuntime/EngineSession.h>
#include <AfterNightConsumerRuntime/ImageAsset.h>

#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>

#include <QDateTime>
#include <QString>

#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <optional>
#include <set>
#include <stop_token>
#include <string>
#include <vector>

namespace an::ui::history
{
class HistoryStore;
} // namespace an::ui::history

class AFTERNIGHTCONTROLS_API ImageHistoryStep
{
public:
    ImageHistoryStep(const std::string& uuid, const std::string& stepName, const std::string& stepDesc);

    [[nodiscard]] std::string getUuid() const;

    [[nodiscard]] std::string getStepName() const;

    [[nodiscard]] std::string getStepDesc() const;

    /// Where the snapshot this step restores stands. A sequence step with no snapshot restores from its sequence file
    /// and reads as written.
    [[nodiscard]] an::ui::history::SnapshotState snapshotState() const;

    /// Waits until the step's snapshot is no longer pending, and returns where it ended.
    an::ui::history::SnapshotState waitAsyncJobsDone() const;

    /// The name the view was shown under in this state.
    std::string m_ImageName;

    /// The sequence a sequence view showed in this state; nothing for a single image.
    std::optional<an::ui::DocumentSequence> m_Sequence;

    /// What this step restores: the still image, or the frame a sequence step changed; null for a sequence step that
    /// changed no frame.
    an::ui::history::SnapshotPtr m_Snapshot;

    /// The zero-based frame a sequence step changed, when it changed one.
    std::optional<std::uint64_t> m_FrameIndex;

    QDateTime m_CreationTime;

    /// Whether the step was given back by a home's index rather than recorded by this history: one whose file does
    /// not read is then left out.
    bool m_Stored = false;

private:
    friend class ImageHistoryTracker;

    std::string m_StepUuid;
    std::string m_StepName;
    std::string m_StepDesc;
};

using ImageHistoryStepSPtr = std::shared_ptr<ImageHistoryStep>;

class ImageHistoryTrackerListener
{
public:
    virtual void onImageHistoryUpdated() {}
};

class AFTERNIGHTCONTROLS_API ImageHistoryTracker
{
public:
    /// What an edit's worker runs before the edit's result reaches the GUI thread: it keeps the state the edit
    /// replaces and admits a copy of the result, waiting for the backup writer as long as it must. Called on the
    /// edit's worker, holding the document's writer; safe to call after the tracker is gone, when it refuses.
    ///
    /// @param replaced the image the edit replaces, under the lease the edit holds.
    /// @param edited the edit's result, which nothing else holds yet.
    /// @param stop stops the waits.
    /// @return what the preparation secured, held until the edit is recorded or let go; or why the edit cannot be
    ///         recorded - which the history has reported - in which case it must not be shown.
    using EditPreparation = std::function<an::Result<std::shared_ptr<void>>(
        const an::consumer::AssetWriteLease& replaced, const an::consumer::ImageAsset& edited, std::stop_token stop)>;

    /// Reads what a step shows, on the calling thread: a still step's snapshot, the frame a sequence step changed, or
    /// the frame of a sequence step read from its sequence. Safe to call on a worker after the tracker is gone.
    using StepReader = std::function<an::Result<an::consumer::ImageAsset>(std::stop_token stop)>;

    /// A history of the view `host` stands for, over the session its images belong to.
    ///
    /// @param host the view; it must outlive the tracker.
    /// @param session the session; it must outlive the tracker and every worker it starts.
    /// @param codec what writes each snapshot outside a home and reads every one back; none for the history's codec.
    /// @param directory the directory the history's own directory is made in; empty for the application's temporary
    ///        directory.
    ImageHistoryTracker(an::ui::history::ImageHistoryHost& host, const an::consumer::EngineSession& session,
                        an::ui::history::StepCodecPtr codec = an::ui::history::historyStepCodec(),
                        QString directory = {});

    ImageHistoryTracker(const ImageHistoryTracker&) = delete;
    ImageHistoryTracker& operator=(const ImageHistoryTracker&) = delete;

    /// Records the state the view was created with: a copy of its image, or its sequence.
    void registerInitialStep(const std::string& historyDesc);

    /// Records that the view was renamed: the step shares the snapshot of the state it renames.
    void registerNewImageRenameStep();

    /// Records the state the view shows after a change already shown. A copy of the shown image is taken now and
    /// admitted whatever the allowance holds, because the state cannot be refused once it is shown; a copy that
    /// cannot be taken leaves a step that says it cannot be restored. A sequence whose file changed is referenced.
    ///
    /// @return false when nothing was recorded: a restore is under way, or the view shows nothing.
    bool registerShownState(const std::string& stepName, const std::string& stepDesc);

    /// What an edit's worker runs to prepare recording its result. Captures, now, the step the edit replaces.
    ///
    /// @param stepName the edit's step name, for a failure.
    /// @return the preparation.
    [[nodiscard]] EditPreparation editPreparation(const std::string& stepName);

    /// Records `edited`, the state an edit is about to show, before the view shows it. Never waits: an edit prepared on
    /// its worker is recorded from what the preparation secured; one that was not prepared is recorded only when the
    /// state it replaces is already written and its copy is admitted at once.
    ///
    /// @return false - having recorded nothing and reported why - when the state the edit replaces cannot be restored,
    ///         `edited` cannot be kept, or a restore is under way; the caller must then not show the edit.
    [[nodiscard]] bool registerEditBeforePublishing(const std::string& stepName, const std::string& stepDesc,
                                                    const an::consumer::ImageAsset& edited);

    /// Starts restoring the previous step.
    ///
    /// @return false, with nothing changed, when there is none or a restore is under way; a restore that fails later
    ///         changes nothing and is reported.
    bool undoStep();

    /// Starts restoring the next step.
    ///
    /// @return false, with nothing changed, when there is none or a restore is under way.
    bool redoStep();

    [[nodiscard]] bool hasUndoSteps() const;

    [[nodiscard]] bool hasRedoSteps() const;

    [[nodiscard]] bool hasSteps() const;

    /// Whether a restore is on its way.
    [[nodiscard]] bool isRestoring() const;

    [[nodiscard]] size_t getCurrentStep() const;

    [[nodiscard]] size_t getTotalStepCount() const;

    [[nodiscard]] bool canJumpToStep(size_t stepIndex) const;

    /// Starts restoring a step.
    ///
    /// @return false, with nothing changed, when it cannot start.
    bool jumpToStep(size_t stepIndex);

    [[nodiscard]] bool canRemoveStep(size_t stepIndex) const;

    /// Removes a step. Removing the current step first restores the one before it, and the step goes when that is
    /// shown; any other step goes at once.
    ///
    /// @return false, with nothing changed, when it cannot.
    bool removeStep(size_t stepIndex);

    [[nodiscard]] std::vector<ImageHistoryStepSPtr> getImageHistorySteps() const;

    [[nodiscard]] ImageHistoryStepSPtr getInitialStep() const;

    /// What reads a step's image on a worker, for a preview or a clone.
    ///
    /// @param stepIndex the step.
    /// @param frameIndex for a sequence step that changed no frame, the frame to read from its sequence.
    /// @return the reader; an empty one for a step that does not exist.
    [[nodiscard]] StepReader stepReader(size_t stepIndex, std::uint64_t frameIndex = 0u) const;

    [[nodiscard]] std::string getPreviousStepName() const;

    [[nodiscard]] std::string getNextStepName() const;

    /// Waits until no step's snapshot is pending. For a test: it blocks the calling thread.
    void waitAllAsyncJobsDone() const;

    [[nodiscard]] const an::ui::history::BackupQueue& backupQueue() const;

    /// The directory this history writes its snapshots in: its own, which it removes when it goes, or its home's.
    [[nodiscard]] std::string snapshotDirectory() const;

    // ---- A home

    /// Keeps the steps in a home from now on (`IHistoryPlacement::enterHome`). Never waits: what the home holds is
    /// read, and the steps' files moved, on workers. Entering the home the history is in does nothing; entering
    /// another leaves the first.
    void enterHome(an::ui::history::HistoryHome home,
                   an::ui::history::StepsMove move = an::ui::history::StepsMove::Move,
                   an::ui::history::IHistoryPlacement::EnterDone done = {},
                   an::ui::history::IHistoryPlacement::MoveProgress progress = {});

    /// Keeps the steps recorded from now on in the history's own directory again; the home's index is written a last
    /// time behind it and its steps stay there.
    void leaveHome();

    /// The home the steps are kept in; nothing outside one, or until an entry has read what the home holds.
    [[nodiscard]] std::optional<an::ui::history::HistoryHome> home() const;

    /// Whether an entry into a home is on its way.
    [[nodiscard]] bool isEnteringHome() const;

    /// The image was written to a file, or read from one: the step shown is the one the file equals. In a home the
    /// file's identity is read on a worker and the index says both.
    ///
    /// @param path the file.
    void fileSaved(const QString& path);

    /// The id of the step the image's file equals; empty when no step does.
    [[nodiscard]] std::string savedStep() const;

    /// The bytes the files of the history's steps take, as their writes measured them.
    [[nodiscard]] std::uint64_t stepFileBytes() const;

    /// What a compaction would remove.
    [[nodiscard]] an::ui::history::HistoryCompactionResult
    planCompaction(const an::ui::history::HistoryCompaction& what) const;

    /// Drops the steps a compaction does not keep and removes their files; refused, with nothing changed, while a
    /// restore or an entry is on its way.
    an::ui::history::HistoryCompactionResult compact(const an::ui::history::HistoryCompaction& what);

    [[nodiscard]] const ImageHistoryLineage& lineage() const;
    void setLineage(ImageHistoryLineage lineage);

    void addListener(ImageHistoryTrackerListener* listener);
    void removeListener(ImageHistoryTrackerListener* listener);

    void notifyHistoryUpdated();

    /// Outside a home: discards the snapshots waiting for the writer, stops the one being written, and removes the
    /// history's own directory with everything in it. In a home: leaves the home's directory, where the snapshots
    /// still waiting are written and the index closed behind it, on a worker.
    ~ImageHistoryTracker();

private:
    struct Restore;
    struct Entry;
    struct HomeContents;

    [[nodiscard]] ImageHistoryStepSPtr makeStep(const std::string& stepName, const std::string& stepDesc) const;
    [[nodiscard]] std::string snapshotLabel(const std::string& stepName) const;

    // Takes a copy of the shown image now and queues it as `step`'s snapshot, admitted whatever the allowance holds.
    void keepShownImage(const ImageHistoryStepSPtr& step);

    // The record of a step of the view as it is now: its name and, for a sequence view, its sequence.
    void describeShownState(const ImageHistoryStepSPtr& step) const;

    void annotateNewStep(const ImageHistoryStepSPtr& step);
    bool startRestore(size_t targetIndex, std::optional<size_t> removedIndex, const std::string& action);
    void finishRestore(const std::shared_ptr<Restore>& restore, an::Result<an::consumer::ImageAsset>&& read);
    void endRestore(const std::shared_ptr<Restore>& restore, const an::Status& shown);
    void releaseDroppedSteps(const std::vector<ImageHistoryStepSPtr>& droppedSteps);
    [[nodiscard]] ImageHistoryStepSPtr getStepByIndex(size_t stepIndex) const;
    [[nodiscard]] bool usesSnapshot(const an::ui::history::SnapshotPtr& snapshot) const;
    void reportFailure(const std::string& action, const std::string& details) const;

    void continueEntry(const std::shared_ptr<Entry>& entry, HomeContents&& contents);
    void finishEntry(const std::shared_ptr<Entry>& entry);
    void attachStoredHistory(const an::ui::history::HistoryIndex& index, const std::vector<bool>& present,
                             an::ui::history::HistoryEntry& outcome);
    void publishIndex();
    void refreshFileIdentity();
    void noteStateTheFileEquals();
    void leaveOutStoredStep(const ImageHistoryStepSPtr& step, const std::string& why);
    // The first step a compaction keeps and the last, as indexes among the initial state and the steps.
    [[nodiscard]] std::pair<size_t, size_t> compactionRange(const an::ui::history::HistoryCompaction& what) const;

    an::ui::history::ImageHistoryHost& m_Host;

    std::shared_ptr<an::ui::history::HistoryStore> m_Store;

    std::deque<ImageHistoryStepSPtr> m_ImageHistory;

    ImageHistoryStepSPtr m_InitialStep;

    size_t m_CurrentStep = 0;

    size_t m_MaxSteps = 20;

    ImageHistoryLineage m_Lineage;

    std::set<ImageHistoryTrackerListener*> m_Listeners;

    // The restore on its way, or null.
    std::shared_ptr<Restore> m_Restore;

    // The entry into a home on its way, or null; and the home, once the entry has read what it holds.
    std::shared_ptr<Entry> m_Entry;
    std::optional<an::ui::history::HistoryHome> m_Home;

    // The step the image's file equals, the file, and its identity as last read; the count tells a later read of
    // the identity from an earlier one.
    std::string m_SavedStep;
    QString m_SavedFile;
    std::optional<an::ui::history::FileIdentity> m_FileIdentity;
    std::uint64_t m_IdentityReads = 0u;

    // Points at this tracker until it goes, so a completion that arrives later does nothing.
    std::shared_ptr<ImageHistoryTracker*> m_Self;
};
