// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// A history that outlives its view: the directory a project keeps an image's steps in, the index that lists them, the
/// identity of the file the image is, and what is done to such a history while no view has it open.
///
/// A history's steps are files that stand alone (`ImageHistoryStepCodec.h`), so a history is kept by keeping its
/// directory and saying, beside the steps, what they are: `history.json`.
///
/// **The index** lists the steps whose files are written - each with its id, its name and description, the file's
/// name with its extension, when it was recorded, and the identity it was written for, which reading it requires -
/// the steps the image inherited, which have no file, the **current** step, the **saved** step - the one the image's
/// file on disk equals - and the file's identity when the history last knew it. It is written whole, through a
/// pending sibling, by the history's writer: when a step's file lands, when the current step moves and when the image
/// is saved. A step whose file is still being written is not in it, so a process that dies between a step's file and
/// the index leaves a file no index lists, which is removed when the directory is next opened, with the `*.record`
/// files a killed write left (`sweepHistoryDirectory`). The index keeps no digest of a step: a step is held to what
/// reading it checks.
///
/// **The identity of a file** is its size, its modification time as the file system stores it, and the SHA-256 of
/// its first mebibyte. Three equal members are an unchanged file. Reading it reads the file, so it is read on a
/// worker.
///
/// **One history keeps a directory at a time.** A directory is claimed by the history that enters it and is that
/// history's until its last write there has landed; another history is refused it - a second view of the same file
/// keeps a history of the session's - and one that asks while the first is finishing waits, on its worker. So what a
/// history finds in its directory is never another's work in progress, and what it sweeps is never another's step.
///
/// **A history no view has open** is measured and compacted in its directory: compacting removes the files of the
/// steps it drops and rewrites the index, and cannot be undone. A history a view has open is compacted through its
/// tracker, and its directory refuses the other way.
///
/// Nothing here is called on the GUI thread but the plain values: every function that touches a file is for a worker.
#pragma once

#include "AfterNightControlsExport.h"

#include "ImageHistoryStepCodec.h"

#include <QDateTime>
#include <QString>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <stop_token>
#include <vector>

class ImageViewControl;

namespace an::ui::history
{

/// The name of a kept history's index, in its directory.
inline constexpr char kHistoryIndexFileName[] = "history.json";

/// The folder, in a kept history's directory, that holds the histories set aside when the image's file changed
/// outside the application: one folder each, named for when it was set aside.
inline constexpr char kDetachedHistoryFolder[] = "detached";

/// What tells a file from the one it was: its size, when it was modified, and a digest of its head.
struct FileIdentity
{
    /// The file's size in bytes.
    std::uint64_t size = 0u;

    /// The modification time as the file system stores it, in milliseconds since the epoch, UTC.
    qint64 modifiedMs = 0;

    /// The SHA-256 of the file's first mebibyte - of all of it when it is shorter - in lowercase hexadecimal.
    QString headSha256;

    [[nodiscard]] friend bool operator==(const FileIdentity& left, const FileIdentity& right) = default;
};

/// How much of a file's head its identity digests.
inline constexpr std::uint64_t kFileIdentityHeadBytes = std::uint64_t{1024u} * 1024u;

/// Reads a file's identity. For a worker: it reads the file's first mebibyte.
///
/// @param path the file.
/// @return the identity; nothing for a path that is no file or cannot be read.
[[nodiscard]] AFTERNIGHTCONTROLS_API std::optional<FileIdentity> readFileIdentity(const QString& path);

/// One step as a kept history's index lists it.
struct HistoryIndexStep
{
    /// The step's id, which the current and the saved step are named by.
    QString id;

    QString name;
    QString description;

    /// The name of the step's file in the history's directory, with its extension, which says which codec reads it;
    /// empty for an inherited step. A rename shares the file of the state it renames.
    QString file;

    /// The run the step's file was written by: with the file's base name, the identity a read of it requires.
    QString run;

    /// The name the view was shown under in this state.
    QString image;

    /// When the step was recorded.
    QDateTime time;

    /// Whether the step is one the image inherited from the image it was made from: listed, never restored.
    bool inherited = false;

    /// The size of the step's file.
    std::uint64_t bytes = 0u;

    [[nodiscard]] friend bool operator==(const HistoryIndexStep& left, const HistoryIndexStep& right) = default;
};

/// What `history.json` states.
struct HistoryIndex
{
    /// The steps, in the history's order: the inherited ones, then the image's own from the oldest it keeps.
    std::vector<HistoryIndexStep> steps;

    /// The id of the step the view showed when the index was written; empty for none.
    QString current;

    /// The id of the step the image's file equals; empty when no step does. It may name a step the index no longer
    /// lists - one the history dropped - and then the file is that state and no step is.
    QString saved;

    /// The image's file, as the history last knew it.
    QString file;

    /// The file's identity when the history last knew the file; nothing for an image that was never a file.
    std::optional<FileIdentity> identity;

    [[nodiscard]] friend bool operator==(const HistoryIndex& left, const HistoryIndex& right) = default;
};

/// The index as the text `history.json` holds: JSON, the format `afternight-history`, version 1.
[[nodiscard]] AFTERNIGHTCONTROLS_API QByteArray formatHistoryIndex(const HistoryIndex& index);

/// The index a text states; nothing for text that is no index, one of a later version, or one whose step names a
/// file outside the directory.
[[nodiscard]] AFTERNIGHTCONTROLS_API std::optional<HistoryIndex> parseHistoryIndex(const QByteArray& text);

/// Writes a directory's index whole: the file is the old one or the new one, never a part of either.
///
/// @param directory the history's directory, which exists.
/// @param index the index.
/// @return whether it was written.
[[nodiscard]] AFTERNIGHTCONTROLS_API bool writeHistoryIndex(const QString& directory, const HistoryIndex& index);

/// Reads a directory's index. An index its history is writing again reads whole - the old one or the new one: for the
/// moment the new file takes the old one's name it does not open, and it is tried again, a few milliseconds apart. For
/// a worker.
///
/// @param directory the history's directory.
/// @return the index; nothing when the directory holds none or holds one that does not read.
[[nodiscard]] AFTERNIGHTCONTROLS_API std::optional<HistoryIndex> readHistoryIndex(const QString& directory);

/// What sweeping a history's directory removed.
struct HistorySweep
{
    /// The `*.record` files a killed write left.
    std::size_t records = 0u;

    /// The step files no index entry names: a process died between the file and the index.
    std::size_t unlisted = 0u;

    /// The payloads a write had not yet published, a step's or the index's.
    std::size_t pending = 0u;
};

/// Removes from a kept history's directory what an interrupted session left: the `*.record` files, the payloads a
/// write had not yet published, and the step files `index` does not list. Called when the directory is opened, before
/// anything is written into it; folders in it - the histories set aside - are not entered.
///
/// @param directory the history's directory.
/// @param index the index read from it, or null when it holds none: every step file is then unlisted.
/// @return what was removed.
AFTERNIGHTCONTROLS_API HistorySweep sweepHistoryDirectory(const QString& directory, const HistoryIndex* index);

/// How a step's file went from one directory to another.
enum class StepFileMove : std::uint8_t
{
    /// Renamed: the file is the one that was written, at another name.
    Renamed,

    /// Copied, and the original removed once the copy was whole: the two directories are on different volumes.
    Copied
};

/// Moves a step's file. It is renamed where a rename can do it and copied otherwise, the original removed only after
/// the copy is whole; the step is not read as a step and not written again.
///
/// @param from the file.
/// @param to where it goes; its directory exists.
/// @param renameAllowed false to copy even where a rename would do, as across volumes.
/// @return how it moved; nothing, with `from` as it was and nothing left at `to`, when it could not.
[[nodiscard]] AFTERNIGHTCONTROLS_API std::optional<StepFileMove>
moveStepFile(const std::filesystem::path& from, const std::filesystem::path& to, bool renameAllowed = true);

/// What a kept history takes on disk.
struct StoredHistorySize
{
    /// The steps its index lists that have a file.
    std::size_t steps = 0u;

    /// The bytes of the files in its directory, the index included.
    std::uint64_t bytes = 0u;

    /// The bytes of the histories set aside in it.
    std::uint64_t detachedBytes = 0u;

    [[nodiscard]] friend bool operator==(const StoredHistorySize& left, const StoredHistorySize& right) = default;
};

/// Measures a kept history's directory. For a worker.
[[nodiscard]] AFTERNIGHTCONTROLS_API StoredHistorySize measureStoredHistory(const QString& directory);

/// Which steps a compaction keeps.
struct HistoryCompaction
{
    enum class Keep : std::uint8_t
    {
        /// The last `steps` steps, and the current one with those after it when it is older than that.
        LastSteps,

        /// The current state alone.
        CurrentState
    };

    Keep keep = Keep::CurrentState;

    /// How many steps `Keep::LastSteps` keeps; at least one.
    std::size_t steps = 1u;

    /// Whether the histories set aside go too.
    bool removeDetached = false;
};

/// What a compaction removes, or removed.
struct HistoryCompactionResult
{
    /// The steps that go.
    std::size_t stepsRemoved = 0u;

    /// The bytes their files, and the histories set aside when those go, take.
    std::uint64_t bytesFreed = 0u;

    /// False when the directory holds no index that reads, or - compacting - when a view's history keeps the
    /// directory or the index could not be rewritten: nothing was removed then.
    bool ok = false;

    [[nodiscard]] friend bool operator==(const HistoryCompactionResult& left,
                                         const HistoryCompactionResult& right) = default;
};

/// What compacting a kept history no view has open would remove. For a worker.
[[nodiscard]] AFTERNIGHTCONTROLS_API HistoryCompactionResult planStoredHistoryCompaction(const QString& directory,
                                                                                         const HistoryCompaction& what);

/// Compacts a kept history no view has open: rewrites the index without the steps that go, then removes their files.
/// It cannot be undone. The directory is held meanwhile, so a view that opens the image waits for it; a directory a
/// view's history keeps is refused. For a worker.
[[nodiscard]] AFTERNIGHTCONTROLS_API HistoryCompactionResult compactStoredHistory(const QString& directory,
                                                                                  const HistoryCompaction& what);

/// Where a history keeps its steps when they outlive its view: a project's folder for one image.
struct HistoryHome
{
    /// The directory the steps and their index are kept in; made when the history enters it.
    QString directory;

    /// How the steps written there are compressed: the project's.
    StepCompression compression = StepCompression::Lz4;

    /// The image's file, whose identity says whether a history kept there is this image's.
    QString file;

    /// What writes the steps recorded there and reads every one back; none for the history's codec under
    /// `compression`.
    StepCodecPtr codec;

    [[nodiscard]] friend bool operator==(const HistoryHome& left, const HistoryHome& right) = default;
};

/// What becomes of the steps a history already has when it enters a home.
enum class StepsMove : std::uint8_t
{
    /// Their files go into the home: renamed on one volume, copied across volumes.
    Move,

    /// They stay where they are and go with the session; the steps recorded from then on are the home's.
    Leave
};

/// What entering a home came to.
struct HistoryEntry
{
    /// Whether the history's steps are kept in the home from now on.
    bool entered = false;

    /// Whether a history kept there was attached around the image's file: its saved step is the current one.
    bool attached = false;

    /// Whether the history kept there was set aside because the file's identity is not the one it recorded.
    bool detached = false;

    /// The steps the kept history lists whose files are not there: left out.
    std::size_t stepsLeftOut = 0u;

    /// The steps whose files went into the home, and how many of them were copied rather than renamed.
    std::size_t stepsMoved = 0u;
    std::size_t stepsCopied = 0u;

    /// The bytes of the files that went into the home.
    std::uint64_t bytesMoved = 0u;

    /// Why the home was not entered, or which step's file could not be moved; empty otherwise.
    QString problem;
};

/// Where a view's history keeps its steps, as what gives a history its home reaches it: the project the view's image
/// belongs to. Every member is called on the GUI thread, and none waits.
class IHistoryPlacement
{
public:
    virtual ~IHistoryPlacement() = default;

    /// Told on the GUI thread what entering a home came to.
    using EnterDone = std::function<void(const HistoryEntry& entry)>;

    /// Told, on a worker, how much of the steps' files has gone into the home.
    using MoveProgress = std::function<void(std::uint64_t movedBytes, std::uint64_t totalBytes)>;

    /// Keeps the history's steps in a home from now on. What the home holds is read on a worker: a history kept there
    /// for the same file is attached around the image - its saved step the current one, the earlier steps behind it
    /// and the ones left unsaved ahead - and one kept for a file that has changed since is set aside. The steps the
    /// history already has go in as `move` says, on a worker.
    ///
    /// @param home the home.
    /// @param move what becomes of the steps the history already has.
    /// @param done told what it came to; may be empty.
    /// @param progress told how much has moved; may be empty.
    virtual void enterHome(const HistoryHome& home, StepsMove move, EnterDone done = {},
                           MoveProgress progress = {}) = 0;

    /// Keeps the steps recorded from now on in a directory of the session's again. The steps the home holds stay
    /// there, listed by its index, and are still what undo restores.
    virtual void leaveHome() = 0;

    /// The home the history's steps are kept in; nothing outside one.
    [[nodiscard]] virtual std::optional<HistoryHome> home() const = 0;

    /// The bytes the files of the history's steps take.
    [[nodiscard]] virtual std::uint64_t stepFileBytes() const = 0;

    /// What a compaction would remove.
    [[nodiscard]] virtual HistoryCompactionResult planCompaction(const HistoryCompaction& what) const = 0;

    /// Drops the steps a compaction does not keep, and their files. It cannot be undone.
    virtual HistoryCompactionResult compact(const HistoryCompaction& what) = 0;
};

/// The placement of a view's history; null for a view with no history, or one whose states are not files of their
/// own - a sequence's.
[[nodiscard]] AFTERNIGHTCONTROLS_API IHistoryPlacement* historyPlacementOf(::ImageViewControl& view);

/// Waits until no history that kept its steps in `directory` is still finishing its writes there - a view that closed
/// lets them finish behind it - and no worker holds it, or until that is so of every directory, when `directory` is
/// empty. For a worker or a test: it blocks.
///
/// @param directory the history's directory, or empty for all.
/// @param timeoutMs how long to wait; negative waits without bound.
/// @return whether none is finishing.
AFTERNIGHTCONTROLS_API bool waitForClosingHistories(const QString& directory = {}, int timeoutMs = -1);

namespace detail
{

/// Who claims a history's directory.
enum class HomeUse : std::uint8_t
{
    /// A view's history, which keeps the directory until its last write there has landed.
    History,

    /// A worker that changes a history no view has open, for as long as that takes.
    Worker
};

/// Claims a kept history's directory, waiting while a history is finishing its writes there or a worker holds it.
///
/// @param directory the directory.
/// @param use who claims it.
/// @param stop stops the wait.
/// @param kept set when the directory is refused because a view's history keeps it.
/// @return the claim, which gives the directory back when it goes unless the history has said it is closing; null
///         when the directory is refused or the wait was stopped.
[[nodiscard]] std::shared_ptr<void> claimHistoryHome(const QString& directory, HomeUse use, const std::stop_token& stop,
                                                     bool& kept);

/// Says the history that claimed a directory is finishing its writes there behind the view that had it;
/// `noteHistoryClosed` says it has, and gives the directory back.
void noteHistoryClosing(const QString& directory);
void noteHistoryClosed(const QString& directory);

} // namespace detail

} // namespace an::ui::history
