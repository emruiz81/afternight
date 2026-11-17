// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// A project's document, with no widget and no service: what it states is written and read back the same; a document
// of a newer version is refused saying so; a file cut short or of another kind is an error and never a crash; a write
// that fails leaves the document as it was; and an image is told where it is from the project's folder.

#include "projects/ProjectDocument.h"

#include <gtest/gtest.h>

#include <QByteArray>
#include <QDate>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QTime>
#include <QTimeZone>

#include <functional>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace
{

using an::ui::history::StepCompression;
using an::ui::shell::ProjectDocument;
using an::ui::shell::ProjectDocumentRead;
using an::ui::shell::ProjectDocumentRefusal;
using an::ui::shell::ProjectImage;
using an::ui::shell::ProjectImageKind;
using an::ui::shell::ProjectIntegration;
using an::ui::shell::ProjectObject;
using an::ui::shell::ProjectObjectSource;
using an::ui::shell::ProjectOrigin;
using an::ui::shell::ProjectWorkspace;
using an::ui::shell::ProjectWorkspaceView;

[[nodiscard]] QDateTime utc(const int year, const int month, const int day, const int hour, const int minute)
{
    return QDateTime(QDate(year, month, day), QTime(hour, minute, 12, 345), QTimeZone::UTC);
}

/// A document with a value in every member a project states.
[[nodiscard]] ProjectDocument fullDocument()
{
    ProjectDocument document;
    document.id = QStringLiteral("5f0c2a9e-7b1d-4c3e-9a55-0d6f3f1c2b7a");
    document.name = QStringLiteral("Carina Nebula LRGB");
    document.created = utc(2026, 7, 27, 21, 4);
    document.origin = ProjectOrigin::IntegrationJob;
    document.object = ProjectObject{QStringLiteral("NGC 3372"), ProjectObjectSource::ObjectCard};
    document.historyCompression = StepCompression::Zstd;
    document.images.push_back(ProjectImage{QStringLiteral("image-1"), QStringLiteral("D:/Astro/Carina/L.fits"),
                                           QStringLiteral("../Carina/L.fits"), ProjectImageKind::Image,
                                           utc(2026, 7, 28, 1, 30)});
    document.images.push_back(ProjectImage{QStringLiteral("image-2"), QStringLiteral("E:/Captures/jupiter.ser"),
                                           QString(), ProjectImageKind::Sequence, QDateTime()});
    ProjectWorkspace workspace;
    workspace.id = QStringLiteral("workspace-1");
    workspace.name = QStringLiteral("Workspace 1");
    workspace.views.push_back(ProjectWorkspaceView{QStringLiteral("image-1"), QStringLiteral("L")});
    workspace.last = true;
    workspace.locked = true;
    document.workspaces.push_back(workspace);
    document.workspaces.push_back(ProjectWorkspace{QStringLiteral("workspace-2"), QStringLiteral("Colour"), {}, false});
    document.floatingViews.push_back(ProjectWorkspaceView{QStringLiteral("image-2"), QStringLiteral("jupiter")});
    document.integrations.push_back(ProjectIntegration{QStringLiteral("job-1"), QStringLiteral("Carina LRGB"),
                                                       QStringLiteral("integration/job-1")});
    document.lastOpened = utc(2026, 7, 28, 1, 31);
    return document;
}

/// The stored object of a document, for a case that changes one member of it.
[[nodiscard]] QJsonObject storedObject(const ProjectDocument& document)
{
    return QJsonDocument::fromJson(an::ui::shell::formatProjectDocument(document)).object();
}

[[nodiscard]] QByteArray bytesOf(const QJsonObject& stored)
{
    return QJsonDocument(stored).toJson(QJsonDocument::Compact);
}

/// A read of a stored object one of whose members a case changed.
[[nodiscard]] ProjectDocumentRead readChanged(const std::function<void(QJsonObject&)>& change)
{
    QJsonObject stored = storedObject(fullDocument());
    change(stored);
    return an::ui::shell::parseProjectDocument(bytesOf(stored));
}

void expectRefused(const ProjectDocumentRead& read, const ProjectDocumentRefusal refusal, const std::string& what)
{
    EXPECT_FALSE(read.isRead()) << what;
    ASSERT_TRUE(read.refusal.has_value()) << what;
    EXPECT_EQ(*read.refusal, refusal) << what << ": " << read.why.toStdString();
    EXPECT_FALSE(read.why.isEmpty()) << what << " is refused with a reason";
}

} // namespace

// Every member a project states is written and read back the same, and written again the same.
TEST(ProjectDocument, RoundTrip)
{
    const ProjectDocument document = fullDocument();
    ASSERT_TRUE(an::ui::shell::checkProjectDocument(document).isEmpty())
        << an::ui::shell::checkProjectDocument(document).toStdString();

    const QByteArray written = an::ui::shell::formatProjectDocument(document);
    const ProjectDocumentRead read = an::ui::shell::parseProjectDocument(written);
    ASSERT_TRUE(read.isRead()) << read.why.toStdString();
    EXPECT_FALSE(read.refusal.has_value());
    EXPECT_TRUE(read.why.isEmpty());
    EXPECT_EQ(*read.document, document);
    EXPECT_EQ(an::ui::shell::formatProjectDocument(*read.document), written);

    // A project with nothing in it yet, and no object, reads back too.
    ProjectDocument bare;
    bare.id = QStringLiteral("p1");
    bare.name = QStringLiteral("M 31");
    bare.created = utc(2026, 1, 2, 3, 4);
    bare.lastOpened = bare.created;
    const ProjectDocumentRead bareRead =
        an::ui::shell::parseProjectDocument(an::ui::shell::formatProjectDocument(bare));
    ASSERT_TRUE(bareRead.isRead()) << bareRead.why.toStdString();
    EXPECT_EQ(*bareRead.document, bare);
    EXPECT_FALSE(bareRead.document->object.has_value());
}

// The file states what it is: the format's name and its version, and the compression by the name a setting states it.
TEST(ProjectDocument, TheFileStatesItsFormatItsVersionAndItsMembersByName)
{
    const QJsonObject stored = storedObject(fullDocument());
    EXPECT_EQ(stored.value(QStringLiteral("format")).toString(), QStringLiteral("afternight-project"));
    EXPECT_EQ(stored.value(QStringLiteral("version")).toInt(), 1);
    EXPECT_EQ(stored.value(QStringLiteral("origin")).toString(), QStringLiteral("integration-job"));
    EXPECT_EQ(stored.value(QStringLiteral("history")).toObject().value(QStringLiteral("compression")).toString(),
              QStringLiteral("zstd"));
    const QJsonObject object = stored.value(QStringLiteral("object")).toObject();
    EXPECT_EQ(object.value(QStringLiteral("name")).toString(), QStringLiteral("NGC 3372"));
    EXPECT_EQ(object.value(QStringLiteral("source")).toString(), QStringLiteral("object-card"));
    const QJsonArray images = stored.value(QStringLiteral("images")).toArray();
    ASSERT_EQ(images.size(), 2);
    EXPECT_EQ(images.at(0).toObject().value(QStringLiteral("relativePath")).toString(),
              QStringLiteral("../Carina/L.fits"));
    EXPECT_EQ(images.at(1).toObject().value(QStringLiteral("kind")).toString(), QStringLiteral("sequence"));
    EXPECT_TRUE(images.at(1).toObject().value(QStringLiteral("lastOpened")).isNull())
        << "an image never opened in the project states no time";
    EXPECT_EQ(stored.value(QStringLiteral("created")).toString(), QStringLiteral("2026-07-27T21:04:12.345Z"));
    EXPECT_TRUE(
        stored.value(QStringLiteral("workspaces")).toArray().at(0).toObject().value(QStringLiteral("last")).toBool());

    // A project with no object states null.
    ProjectDocument bare = fullDocument();
    bare.object.reset();
    EXPECT_TRUE(storedObject(bare).value(QStringLiteral("object")).isNull());
    EXPECT_TRUE(storedObject(bare).contains(QStringLiteral("object")));
}

// A document a newer AfterNight wrote is refused, and the reason names its version and the one this one reads.
TEST(ProjectDocument, ANewerVersionIsRefusedWithAMessage)
{
    const ProjectDocumentRead read =
        readChanged([](QJsonObject& stored) { stored.insert(QStringLiteral("version"), 2); });
    expectRefused(read, ProjectDocumentRefusal::NewerVersion, "version 2");
    EXPECT_TRUE(read.why.contains(QStringLiteral("newer"))) << read.why.toStdString();
    EXPECT_TRUE(read.why.contains(QStringLiteral("2"))) << read.why.toStdString();
    EXPECT_TRUE(read.why.contains(QStringLiteral("1"))) << read.why.toStdString();

    // There is no older version: one that states less than the first is no document at all.
    expectRefused(readChanged([](QJsonObject& stored) { stored.insert(QStringLiteral("version"), 0); }),
                  ProjectDocumentRefusal::Invalid, "version 0");
    expectRefused(readChanged([](QJsonObject& stored) { stored.insert(QStringLiteral("version"), 1.5); }),
                  ProjectDocumentRefusal::Invalid, "version 1.5");
    expectRefused(
        readChanged([](QJsonObject& stored) { stored.insert(QStringLiteral("version"), QStringLiteral("1")); }),
        ProjectDocumentRefusal::Invalid, "a version that is a text");
    expectRefused(readChanged([](QJsonObject& stored) { stored.remove(QStringLiteral("version")); }),
                  ProjectDocumentRefusal::Invalid, "no version");
}

// A document cut short anywhere is refused with a reason: no prefix of it reads as a project.
TEST(ProjectDocument, ATruncatedFileIsAnErrorNeverACrash)
{
    // The document without the line end its file closes with: every shorter prefix is cut inside it.
    const QByteArray written = an::ui::shell::formatProjectDocument(fullDocument()).trimmed();
    ASSERT_GT(written.size(), 200);
    ASSERT_TRUE(an::ui::shell::parseProjectDocument(written).isRead());
    for (qsizetype length = 0; length < written.size(); ++length)
    {
        const ProjectDocumentRead read = an::ui::shell::parseProjectDocument(written.left(length));
        ASSERT_FALSE(read.isRead()) << "the first " << length << " bytes read as a project";
        ASSERT_TRUE(read.refusal.has_value()) << length;
        ASSERT_FALSE(read.why.isEmpty()) << length;
    }
}

// A file of another kind is refused for what it is.
TEST(ProjectDocument, AWrongFileIsAnError)
{
    expectRefused(an::ui::shell::parseProjectDocument(QByteArray()), ProjectDocumentRefusal::NotJson, "an empty file");
    expectRefused(an::ui::shell::parseProjectDocument(QByteArray("\x89PNG\r\n\x1a\n\0\0\0\rIHDR", 16)),
                  ProjectDocumentRefusal::NotJson, "a picture");
    expectRefused(an::ui::shell::parseProjectDocument(QByteArrayLiteral("[1, 2, 3]")), ProjectDocumentRefusal::NotJson,
                  "a JSON array");
    expectRefused(an::ui::shell::parseProjectDocument(QByteArrayLiteral("{}")), ProjectDocumentRefusal::NotAProject,
                  "an object that states no format");
    expectRefused(an::ui::shell::parseProjectDocument(QByteArrayLiteral("{\"sessionSchema\":3,\"version\":1}")),
                  ProjectDocumentRefusal::NotAProject, "an integration session");
    expectRefused(readChanged([](QJsonObject& stored) {
                      stored.insert(QStringLiteral("format"), QStringLiteral("afternight-preset"));
                  }),
                  ProjectDocumentRefusal::NotAProject, "another format");
    expectRefused(readChanged([](QJsonObject& stored) { stored.insert(QStringLiteral("format"), 7); }),
                  ProjectDocumentRefusal::NotAProject, "a format that is a number");
}

// Each member the document states must be there and be what a project can state; the reason names the member.
TEST(ProjectDocument, AMissingOrImpossibleMemberIsAnErrorThatNamesIt)
{
    for (const char* member :
         {"id", "name", "created", "origin", "history", "images", "workspaces", "integrations", "lastOpened"})
    {
        const QString name = QString::fromLatin1(member);
        const ProjectDocumentRead read = readChanged([&name](QJsonObject& stored) { stored.remove(name); });
        expectRefused(read, ProjectDocumentRefusal::Invalid, std::string("without ") + member);
        EXPECT_TRUE(read.why.contains(name)) << read.why.toStdString();
    }

    const auto invalid = [](const char* what, const std::function<void(QJsonObject&)>& change) {
        expectRefused(readChanged(change), ProjectDocumentRefusal::Invalid, what);
    };
    const auto changeFirst = [](const char* list, const std::function<void(QJsonObject&)>& change) {
        return [list, change](QJsonObject& stored) {
            QJsonArray entries = stored.value(QString::fromLatin1(list)).toArray();
            QJsonObject first = entries.at(0).toObject();
            change(first);
            entries.replace(0, first);
            stored.insert(QString::fromLatin1(list), entries);
        };
    };

    invalid("an empty id", [](QJsonObject& stored) { stored.insert(QStringLiteral("id"), QString()); });
    invalid("an id that is a path",
            [](QJsonObject& stored) { stored.insert(QStringLiteral("id"), QStringLiteral("../outside")); });
    invalid("a blank name", [](QJsonObject& stored) { stored.insert(QStringLiteral("name"), QStringLiteral("   ")); });
    invalid("a name that is a number", [](QJsonObject& stored) { stored.insert(QStringLiteral("name"), 4); });
    invalid("a date that is none",
            [](QJsonObject& stored) { stored.insert(QStringLiteral("created"), QStringLiteral("yesterday")); });
    invalid("a date no file states again", [](QJsonObject& stored) {
        stored.insert(QStringLiteral("created"), QStringLiteral("12026-07-27T21:04:12.345Z"));
    });
    invalid("an unknown origin",
            [](QJsonObject& stored) { stored.insert(QStringLiteral("origin"), QStringLiteral("import")); });
    invalid("an object with no name", [](QJsonObject& stored) {
        stored.insert(QStringLiteral("object"), QJsonObject{{QStringLiteral("source"), QStringLiteral("user")}});
    });
    invalid("an object from nowhere", [](QJsonObject& stored) {
        stored.insert(QStringLiteral("object"), QJsonObject{{QStringLiteral("name"), QStringLiteral("M 42")},
                                                            {QStringLiteral("source"), QStringLiteral("guess")}});
    });
    invalid("an object that is a text",
            [](QJsonObject& stored) { stored.insert(QStringLiteral("object"), QStringLiteral("M 42")); });
    invalid("an unknown compression", [](QJsonObject& stored) {
        stored.insert(QStringLiteral("history"), QJsonObject{{QStringLiteral("compression"), QStringLiteral("gzip")}});
    });
    invalid("images that are no list",
            [](QJsonObject& stored) { stored.insert(QStringLiteral("images"), QJsonObject()); });
    invalid("an image that is no object",
            [](QJsonObject& stored) { stored.insert(QStringLiteral("images"), QJsonArray{QStringLiteral("L.fits")}); });
    invalid("an image whose id is a path", changeFirst("images", [](QJsonObject& image) {
                image.insert(QStringLiteral("id"), QStringLiteral("..\\..\\Windows"));
            }));
    invalid("an image with no path",
            changeFirst("images", [](QJsonObject& image) { image.insert(QStringLiteral("path"), QString()); }));
    invalid("an image of an unknown kind", changeFirst("images", [](QJsonObject& image) {
                image.insert(QStringLiteral("kind"), QStringLiteral("video"));
            }));
    invalid("two images of one id", changeFirst("images", [](QJsonObject& image) {
                image.insert(QStringLiteral("id"), QStringLiteral("image-2"));
            }));
    invalid("a workspace's view of an image the project does not hold",
            changeFirst("workspaces", [](QJsonObject& workspace) {
                workspace.insert(QStringLiteral("views"),
                                 QJsonArray{QJsonObject{{QStringLiteral("image"), QStringLiteral("image-9")},
                                                        {QStringLiteral("dockWidget"), QStringLiteral("L")}}});
            }));
    invalid("two workspaces of one id", changeFirst("workspaces", [](QJsonObject& workspace) {
                workspace.insert(QStringLiteral("id"), QStringLiteral("workspace-2"));
            }));
    invalid("two workspaces used last", [](QJsonObject& stored) {
        QJsonArray workspaces = stored.value(QStringLiteral("workspaces")).toArray();
        QJsonObject second = workspaces.at(1).toObject();
        second.insert(QStringLiteral("last"), true);
        workspaces.replace(1, second);
        stored.insert(QStringLiteral("workspaces"), workspaces);
    });
    invalid("a lock that is no yes or no", changeFirst("workspaces", [](QJsonObject& workspace) {
                workspace.insert(QStringLiteral("locked"), QStringLiteral("yes"));
            }));
    invalid("floating views that are no list",
            [](QJsonObject& stored) { stored.insert(QStringLiteral("floating"), QStringLiteral("jupiter")); });
    invalid("a floating view that names no dock widget",
            changeFirst("floating", [](QJsonObject& view) { view.remove(QStringLiteral("dockWidget")); }));
    invalid("a floating view of an image the project does not hold", changeFirst("floating", [](QJsonObject& view) {
                view.insert(QStringLiteral("image"), QStringLiteral("image-9"));
            }));
    invalid("an integration with no folder", changeFirst("integrations", [](QJsonObject& integration) {
                integration.insert(QStringLiteral("folder"), QString());
            }));

    // What the document does not state is not an error: a member this version does not know is passed over.
    const ProjectDocumentRead extra =
        readChanged([](QJsonObject& stored) { stored.insert(QStringLiteral("colour"), QStringLiteral("teal")); });
    ASSERT_TRUE(extra.isRead()) << extra.why.toStdString();
    EXPECT_EQ(*extra.document, fullDocument());
}

// A workspace states its lock and the project the images that floated in windows of their own, each by the image
// it showed and the name its dock widget had. Both were stated later than the other members: a document that states
// neither reads with no workspace locked and no image floating, and is written again stating both.
TEST(ProjectDocument, AWorkspaceStatesItsLockAndTheProjectItsFloatingImages)
{
    const QJsonObject stored = storedObject(fullDocument());
    const QJsonArray workspaces = stored.value(QStringLiteral("workspaces")).toArray();
    ASSERT_EQ(workspaces.size(), 2);
    EXPECT_TRUE(workspaces.at(0).toObject().value(QStringLiteral("locked")).toBool());
    ASSERT_TRUE(workspaces.at(1).toObject().value(QStringLiteral("locked")).isBool());
    EXPECT_FALSE(workspaces.at(1).toObject().value(QStringLiteral("locked")).toBool());
    const QJsonArray floating = stored.value(QStringLiteral("floating")).toArray();
    ASSERT_EQ(floating.size(), 1);
    EXPECT_EQ(floating.at(0).toObject().value(QStringLiteral("image")).toString(), QStringLiteral("image-2"));
    EXPECT_EQ(floating.at(0).toObject().value(QStringLiteral("dockWidget")).toString(), QStringLiteral("jupiter"));

    const ProjectDocumentRead read = an::ui::shell::parseProjectDocument(bytesOf(stored));
    ASSERT_TRUE(read.isRead()) << read.why.toStdString();
    EXPECT_TRUE(read.document->workspaces.at(0).locked);
    EXPECT_FALSE(read.document->workspaces.at(1).locked);
    ASSERT_EQ(read.document->floatingViews.size(), 1u);
    EXPECT_EQ(read.document->floatingViews.front(),
              (ProjectWorkspaceView{QStringLiteral("image-2"), QStringLiteral("jupiter")}));

    // A document written before either was stated.
    const ProjectDocumentRead earlier = readChanged([](QJsonObject& document) {
        document.remove(QStringLiteral("floating"));
        QJsonArray listed = document.value(QStringLiteral("workspaces")).toArray();
        for (qsizetype index = 0; index < listed.size(); ++index)
        {
            QJsonObject workspace = listed.at(index).toObject();
            workspace.remove(QStringLiteral("locked"));
            listed.replace(index, workspace);
        }
        document.insert(QStringLiteral("workspaces"), listed);
    });
    ASSERT_TRUE(earlier.isRead()) << earlier.why.toStdString();
    EXPECT_FALSE(earlier.document->workspaces.at(0).locked);
    EXPECT_TRUE(earlier.document->floatingViews.empty());
    const QJsonObject again = storedObject(*earlier.document);
    EXPECT_TRUE(again.value(QStringLiteral("floating")).isArray());
    EXPECT_TRUE(
        again.value(QStringLiteral("workspaces")).toArray().at(0).toObject().contains(QStringLiteral("locked")));
}

// The reader of a file: the document, a file that is not there, and one larger than a document is.
TEST(ProjectDocument, AFileIsReadWholeOrRefused)
{
    QTemporaryDir folder;
    ASSERT_TRUE(folder.isValid());
    const QString path = QDir(folder.path()).filePath(QString::fromLatin1(an::ui::shell::kProjectFileName));

    expectRefused(an::ui::shell::readProjectDocument(path), ProjectDocumentRefusal::Unreadable, "no file");

    const ProjectDocument document = fullDocument();
    ASSERT_TRUE(an::ui::shell::writeProjectDocument(path, document).isEmpty());
    const ProjectDocumentRead read = an::ui::shell::readProjectDocument(path);
    ASSERT_TRUE(read.isRead()) << read.why.toStdString();
    EXPECT_EQ(*read.document, document);

    // A file larger than a document is - a picture renamed, a log - is refused before it is read whole.
    {
        QFile large(path);
        ASSERT_TRUE(large.open(QIODevice::WriteOnly | QIODevice::Truncate));
        ASSERT_TRUE(large.resize(an::ui::shell::kMaxProjectDocumentBytes + 1));
    }
    expectRefused(an::ui::shell::readProjectDocument(path), ProjectDocumentRefusal::Unreadable, "an oversized file");
}

// A document its own reader would refuse is not written, and the file stays what it was.
TEST(ProjectDocument, ADocumentThatWouldNotReadBackIsNotWritten)
{
    QTemporaryDir folder;
    ASSERT_TRUE(folder.isValid());
    const QString path = QDir(folder.path()).filePath(QString::fromLatin1(an::ui::shell::kProjectFileName));
    const ProjectDocument document = fullDocument();
    ASSERT_TRUE(an::ui::shell::writeProjectDocument(path, document).isEmpty());

    ProjectDocument broken = document;
    broken.images[1].id = broken.images[0].id;
    EXPECT_FALSE(an::ui::shell::checkProjectDocument(broken).isEmpty());
    EXPECT_FALSE(an::ui::shell::writeProjectDocument(path, broken).isEmpty());

    const ProjectDocumentRead read = an::ui::shell::readProjectDocument(path);
    ASSERT_TRUE(read.isRead()) << read.why.toStdString();
    EXPECT_EQ(*read.document, document);
}

// A write that cannot finish leaves the document as it was, and no part of the new one in its place.
TEST(ProjectDocument, AFailedWriteLeavesTheOldFile)
{
    QTemporaryDir folder;
    ASSERT_TRUE(folder.isValid());
    const QString path = QDir(folder.path()).filePath(QString::fromLatin1(an::ui::shell::kProjectFileName));
    const ProjectDocument document = fullDocument();
    ASSERT_TRUE(an::ui::shell::writeProjectDocument(path, document).isEmpty());
    const QByteArray before = an::ui::shell::formatProjectDocument(document);

    ProjectDocument changed = document;
    changed.name = QStringLiteral("Another name");

#ifdef _WIN32
    // Another program holds the file and shares it for reading only: the new bytes cannot take its place.
    const HANDLE held =
        CreateFileW(reinterpret_cast<const wchar_t*>(QDir::toNativeSeparators(path).utf16()), GENERIC_READ,
                    FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    ASSERT_NE(held, INVALID_HANDLE_VALUE);
    const QString why = an::ui::shell::writeProjectDocument(path, changed);
    CloseHandle(held);
#else
    if (geteuid() == 0)
    {
        GTEST_SKIP() << "the folder cannot be closed to its owner's writes when the owner is root";
    }
    // The folder takes no new file: the new bytes have nowhere to be written beside the document.
    ASSERT_EQ(chmod(folder.path().toLocal8Bit().constData(), S_IRUSR | S_IXUSR), 0);
    const QString why = an::ui::shell::writeProjectDocument(path, changed);
    ASSERT_EQ(chmod(folder.path().toLocal8Bit().constData(), S_IRWXU), 0);
#endif

    EXPECT_FALSE(why.isEmpty()) << "the write is said to have failed";
    QFile file(path);
    ASSERT_TRUE(file.open(QIODevice::ReadOnly));
    EXPECT_EQ(file.readAll(), before) << "the document is what it was";
    file.close();
    // Nothing of the write that failed is left beside it.
    EXPECT_EQ(QDir(folder.path()).entryList(QDir::Files | QDir::Hidden),
              QStringList{QString::fromLatin1(an::ui::shell::kProjectFileName)});

    // Once the file is free the same write lands.
    ASSERT_TRUE(an::ui::shell::writeProjectDocument(path, changed).isEmpty());
    EXPECT_EQ(an::ui::shell::readProjectDocument(path).document->name, QStringLiteral("Another name"));
}

// An image is told where it is from the project's folder, so a project moved with its images finds them.
TEST(ProjectDocument, RelativePaths)
{
    QTemporaryDir root;
    ASSERT_TRUE(root.isValid());
    const QString project = QDir(root.path()).filePath(QStringLiteral("Projects/Carina"));

    EXPECT_EQ(an::ui::shell::projectRelativePath(project, project + QStringLiteral("/lights/L.fits")),
              QStringLiteral("lights/L.fits"));
    EXPECT_EQ(
        an::ui::shell::projectRelativePath(project, QDir(root.path()).filePath(QStringLiteral("Captures/L.fits"))),
        QStringLiteral("../../Captures/L.fits"));
    // Native separators and a folder written with a trailing one name the same place.
    EXPECT_EQ(an::ui::shell::projectRelativePath(QDir::toNativeSeparators(project) + QDir::separator(),
                                                 QDir::toNativeSeparators(project + QStringLiteral("/a/b.xisf"))),
              QStringLiteral("a/b.xisf"));
    EXPECT_TRUE(an::ui::shell::projectRelativePath(project, QString()).isEmpty());
    EXPECT_TRUE(an::ui::shell::projectRelativePath(QString(), project + QStringLiteral("/L.fits")).isEmpty());
#ifdef _WIN32
    // No relative path reaches another drive.
    EXPECT_TRUE(
        an::ui::shell::projectRelativePath(QStringLiteral("C:/Projects/Carina"), QStringLiteral("D:/Captures/L.fits"))
            .isEmpty());
#endif
}

// An id is a name a folder can take: an image's history is kept in a folder named by it.
TEST(ProjectDocument, AnIdIsANameAFolderCanTake)
{
    EXPECT_TRUE(an::ui::shell::isProjectId(QStringLiteral("5f0c2a9e-7b1d-4c3e-9a55-0d6f3f1c2b7a")));
    EXPECT_TRUE(an::ui::shell::isProjectId(QStringLiteral("image_1")));
    EXPECT_FALSE(an::ui::shell::isProjectId(QString()));
    EXPECT_FALSE(an::ui::shell::isProjectId(QStringLiteral("a/b")));
    EXPECT_FALSE(an::ui::shell::isProjectId(QStringLiteral("a\\b")));
    EXPECT_FALSE(an::ui::shell::isProjectId(QStringLiteral("..")));
    EXPECT_FALSE(an::ui::shell::isProjectId(QStringLiteral("a b")));
    EXPECT_FALSE(an::ui::shell::isProjectId(QStringLiteral("caf\u00e9")));
    EXPECT_FALSE(an::ui::shell::isProjectId(QString(65, QLatin1Char('a'))));
    EXPECT_TRUE(an::ui::shell::isProjectId(QString(64, QLatin1Char('a'))));
}
