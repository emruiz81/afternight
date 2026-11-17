// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// A project's document: the file `project.anproj` of a project folder, what it states, how it is read and how it is
/// written.
///
/// A project is a folder a person chooses. Its document is one JSON object, of the format `afternight-project` in
/// version 1, written whole each time it changes: the project's identity and name, when and how it was made, the
/// object it is about when it has one, how its history's steps are compressed, the images it holds - where each is,
/// and where it is from the folder, so a project moved with its images still finds them - its workspaces, each
/// with the images each showed, the images that floated in windows of their own, its integrations and when it was last
/// opened.
///
/// **Reading answers.** A document is read from bytes a person can copy, edit, truncate or keep from another version,
/// so a read returns the document or refuses with a reason and never crashes: a file that cannot be read or is larger
/// than a document is, bytes that are no JSON object, an object of another format, a version this application does not
/// know - a newer one is refused saying so; there is no older one - and a member that is missing or states what a
/// project cannot. An id is a name a folder can take, since an image's history is kept in a folder named by it. Two
/// members were stated later than the others and may be absent: a workspace's lock, read then as not locked, and the
/// images that floated in windows of their own, read then as none.
///
/// **Writing is whole, or not at all.** A document that its own reader would refuse is not written. The bytes go to a
/// file beside the document and take its place only once they are complete (`QSaveFile`), so a write that fails leaves
/// the document as it was.

#include "AfterNightShellCoreExport.h"

#include "controls/ImageHistoryStepCodec.h"

#include <QByteArray>
#include <QDateTime>
#include <QString>

#include <cstdint>
#include <optional>
#include <vector>

namespace an::ui::shell
{

/// The name of a project folder's document.
inline constexpr char kProjectFileName[] = "project.anproj";

/// The format a project document states, and the version this application writes and reads.
inline constexpr char kProjectFormat[] = "afternight-project";
inline constexpr int kProjectVersion = 1;

/// The most bytes a project document is: a larger file is not read.
inline constexpr qint64 kMaxProjectDocumentBytes = qint64{16} * 1024 * 1024;

/// How a project came to be.
enum class ProjectOrigin : std::uint8_t
{
    /// A person made it.
    User,

    /// An integration job made it in its output folder.
    IntegrationJob
};

/// Where a project's object was read from.
enum class ProjectObjectSource : std::uint8_t
{
    /// The OBJECT card of its frames.
    ObjectCard,

    /// An image's plate solution.
    PlateSolution,

    /// A person named it.
    User
};

/// What an image of a project is.
enum class ProjectImageKind : std::uint8_t
{
    Image,
    Sequence
};

/// The object a project is about.
struct ProjectObject
{
    QString name;
    ProjectObjectSource source = ProjectObjectSource::User;

    [[nodiscard]] bool operator==(const ProjectObject&) const = default;
};

/// An image a project holds: referenced where it is, never copied.
struct ProjectImage
{
    /// What the project tells the image by; its history's folder is named by it.
    QString id;

    /// Where the file is.
    QString path;

    /// Where the file is from the project's folder; empty when no relative path reaches it - another drive.
    QString relativePath;

    ProjectImageKind kind = ProjectImageKind::Image;

    /// When the image was last opened in the project; null for one never opened in it.
    QDateTime lastOpened;

    [[nodiscard]] bool operator==(const ProjectImage&) const = default;
};

/// A view a workspace of a project holds.
struct ProjectWorkspaceView
{
    /// The id of the image it shows.
    QString image;

    /// The name of the dock widget it had.
    QString dockWidget;

    [[nodiscard]] bool operator==(const ProjectWorkspaceView&) const = default;
};

/// A workspace of a project.
struct ProjectWorkspace
{
    QString id;
    QString name;
    std::vector<ProjectWorkspaceView> views;

    /// Whether it is the workspace used last; one at most is.
    bool last = false;

    /// Whether its arrangement was locked.
    bool locked = false;

    [[nodiscard]] bool operator==(const ProjectWorkspace&) const = default;
};

/// An integration of a project.
struct ProjectIntegration
{
    QString id;
    QString name;
    QString folder;

    [[nodiscard]] bool operator==(const ProjectIntegration&) const = default;
};

/// What a project's document states.
struct ProjectDocument
{
    QString id;
    QString name;
    QDateTime created;
    ProjectOrigin origin = ProjectOrigin::User;

    /// The object the project is about; none is stated as null.
    std::optional<ProjectObject> object;

    /// How the steps of the project's histories are compressed.
    an::ui::history::StepCompression historyCompression = an::ui::history::StepCompression::Lz4;

    std::vector<ProjectImage> images;
    std::vector<ProjectWorkspace> workspaces;

    /// The views that floated in windows of their own: they belong to no workspace.
    std::vector<ProjectWorkspaceView> floatingViews;

    std::vector<ProjectIntegration> integrations;
    QDateTime lastOpened;

    [[nodiscard]] bool operator==(const ProjectDocument&) const = default;
};

/// Why a project document was not read.
enum class ProjectDocumentRefusal : std::uint8_t
{
    /// The file cannot be opened or read, or is larger than a document is.
    Unreadable,

    /// The bytes are no JSON object: a file cut short, or a file of another kind.
    NotJson,

    /// A JSON object that states another format, or none.
    NotAProject,

    /// A project written by a newer AfterNight.
    NewerVersion,

    /// A member is missing, or states what a project cannot.
    Invalid
};

/// A read of a project document: the document, or why there is none.
struct ProjectDocumentRead
{
    std::optional<ProjectDocument> document;
    std::optional<ProjectDocumentRefusal> refusal;

    /// What a person is told of a refusal; empty for a document that was read.
    QString why;

    [[nodiscard]] bool isRead() const noexcept
    {
        return document.has_value();
    }
};

/// Reads a project document from its bytes.
///
/// @param bytes the file's contents.
/// @return the document, or the refusal and its reason; never both, never neither.
[[nodiscard]] AFTERNIGHTSHELLCORE_API ProjectDocumentRead parseProjectDocument(const QByteArray& bytes);

/// Reads a project document from its file.
///
/// @param filePath the document's file.
/// @return the document, or the refusal and its reason.
[[nodiscard]] AFTERNIGHTSHELLCORE_API ProjectDocumentRead readProjectDocument(const QString& filePath);

/// What is wrong with a document, as its reader would say it; empty for one that can be written and read back.
[[nodiscard]] AFTERNIGHTSHELLCORE_API QString checkProjectDocument(const ProjectDocument& document);

/// A project document as its file holds it. Date-times are written in UTC, to the millisecond.
[[nodiscard]] AFTERNIGHTSHELLCORE_API QByteArray formatProjectDocument(const ProjectDocument& document);

/// Writes a project document whole: to a file beside it that takes its place once complete. A document its reader
/// would refuse is not written, and a write that fails leaves the file as it was.
///
/// @param filePath the document's file.
/// @param document the document.
/// @return nothing when it was written; else why it was not.
[[nodiscard]] AFTERNIGHTSHELLCORE_API QString writeProjectDocument(const QString& filePath,
                                                                   const ProjectDocument& document);

/// Where a file is from a project's folder: the relative path that reaches it, with forward slashes; empty when none
/// does - a file on another drive.
[[nodiscard]] AFTERNIGHTSHELLCORE_API QString projectRelativePath(const QString& projectFolder,
                                                                  const QString& filePath);

/// Whether a text is an id a project's document takes: one to sixty-four of the letters, the digits, `-` and `_`.
[[nodiscard]] AFTERNIGHTSHELLCORE_API bool isProjectId(const QString& text);

} // namespace an::ui::shell
