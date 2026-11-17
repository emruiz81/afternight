// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"

#include "projects/ProjectDocument.h"

#include "controls/settings/ProjectSettings.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QSaveFile>
#include <QSet>
#include <QTimeZone>

#include <cmath>

namespace an::ui::shell
{

namespace
{

constexpr char kContext[] = "an::ui::shell::ProjectDocument";

/// The longest a name of a project, a workspace or an integration is.
constexpr qsizetype kMaxNameLength = 255;

/// The longest an id is.
constexpr qsizetype kMaxIdLength = 64;

[[nodiscard]] QString translated(const char* text)
{
    return QCoreApplication::translate(kContext, text);
}

// ---- The names the file states its choices by

[[nodiscard]] QString originName(const ProjectOrigin origin)
{
    return origin == ProjectOrigin::IntegrationJob ? QStringLiteral("integration-job") : QStringLiteral("user");
}

[[nodiscard]] std::optional<ProjectOrigin> originFromName(const QString& name)
{
    if (name == QLatin1String("user"))
    {
        return ProjectOrigin::User;
    }
    if (name == QLatin1String("integration-job"))
    {
        return ProjectOrigin::IntegrationJob;
    }
    return std::nullopt;
}

[[nodiscard]] QString sourceName(const ProjectObjectSource source)
{
    switch (source)
    {
    case ProjectObjectSource::ObjectCard:
        return QStringLiteral("object-card");
    case ProjectObjectSource::PlateSolution:
        return QStringLiteral("plate-solution");
    case ProjectObjectSource::User:
        break;
    }
    return QStringLiteral("user");
}

[[nodiscard]] std::optional<ProjectObjectSource> sourceFromName(const QString& name)
{
    if (name == QLatin1String("object-card"))
    {
        return ProjectObjectSource::ObjectCard;
    }
    if (name == QLatin1String("plate-solution"))
    {
        return ProjectObjectSource::PlateSolution;
    }
    if (name == QLatin1String("user"))
    {
        return ProjectObjectSource::User;
    }
    return std::nullopt;
}

[[nodiscard]] QString kindName(const ProjectImageKind kind)
{
    return kind == ProjectImageKind::Sequence ? QStringLiteral("sequence") : QStringLiteral("image");
}

[[nodiscard]] std::optional<ProjectImageKind> kindFromName(const QString& name)
{
    if (name == QLatin1String("image"))
    {
        return ProjectImageKind::Image;
    }
    if (name == QLatin1String("sequence"))
    {
        return ProjectImageKind::Sequence;
    }
    return std::nullopt;
}

[[nodiscard]] QString compressionName(const an::ui::history::StepCompression compression)
{
    const std::string_view name = an::ui::history::stepCompressionName(compression);
    return QString::fromLatin1(name.data(), static_cast<qsizetype>(name.size()));
}

// ---- Date-times: UTC, to the millisecond, in the years a file states again

/// Whether a date-time is one the file states and reads back the same.
[[nodiscard]] bool isStatable(const QDateTime& time)
{
    if (!time.isValid())
    {
        return false;
    }
    const int year = time.toUTC().date().year();
    return year >= 1 && year <= 9999;
}

[[nodiscard]] QString timeText(const QDateTime& time)
{
    return time.toUTC().toString(Qt::ISODateWithMs);
}

/// A date-time a text states, in UTC; invalid for a text that states none the file would state again.
[[nodiscard]] QDateTime timeFromText(const QString& text)
{
    const QDateTime time = QDateTime::fromString(text, Qt::ISODateWithMs);
    return isStatable(time) ? time.toUTC() : QDateTime();
}

// ---- What every document must hold

/// Whether a name is one a project, a workspace or an integration takes.
[[nodiscard]] bool isName(const QString& name)
{
    return !name.trimmed().isEmpty() && name.size() <= kMaxNameLength;
}

[[nodiscard]] QString problemOf(const ProjectDocument& document)
{
    if (!isProjectId(document.id))
    {
        return translated("its id is not one a project takes (member \"id\")");
    }
    if (!isName(document.name))
    {
        return translated("it has no name, or one too long (member \"name\")");
    }
    if (!isStatable(document.created))
    {
        return translated("it does not say when it was made (member \"created\")");
    }
    if (!isStatable(document.lastOpened))
    {
        return translated("it does not say when it was last opened (member \"lastOpened\")");
    }
    if (document.object.has_value() && document.object->name.trimmed().isEmpty())
    {
        return translated("its object has no name (member \"object\")");
    }

    QSet<QString> images;
    for (const ProjectImage& image : document.images)
    {
        if (!isProjectId(image.id))
        {
            return translated("an image's id is not one a project takes (member \"images\")");
        }
        if (images.contains(image.id))
        {
            return translated("two images have the id \"%1\" (member \"images\")").arg(image.id);
        }
        images.insert(image.id);
        if (image.path.isEmpty())
        {
            return translated("the image \"%1\" does not say where its file is (member \"images\")").arg(image.id);
        }
        if (!image.lastOpened.isNull() && !isStatable(image.lastOpened))
        {
            return translated("the image \"%1\" does not say when it was last opened (member \"images\")")
                .arg(image.id);
        }
    }

    QSet<QString> workspaces;
    int last = 0;
    for (const ProjectWorkspace& workspace : document.workspaces)
    {
        if (!isProjectId(workspace.id))
        {
            return translated("a workspace's id is not one a project takes (member \"workspaces\")");
        }
        if (workspaces.contains(workspace.id))
        {
            return translated("two workspaces have the id \"%1\" (member \"workspaces\")").arg(workspace.id);
        }
        workspaces.insert(workspace.id);
        if (!isName(workspace.name))
        {
            return translated("the workspace \"%1\" has no name, or one too long (member \"workspaces\")")
                .arg(workspace.id);
        }
        for (const ProjectWorkspaceView& view : workspace.views)
        {
            if (!images.contains(view.image))
            {
                return translated("the workspace \"%1\" shows an image the project does not hold (member "
                                  "\"workspaces\")")
                    .arg(workspace.id);
            }
        }
        last += workspace.last ? 1 : 0;
    }
    if (last > 1)
    {
        return translated("more than one workspace is the one used last (member \"workspaces\")");
    }

    QSet<QString> integrations;
    for (const ProjectIntegration& integration : document.integrations)
    {
        if (!isProjectId(integration.id))
        {
            return translated("an integration's id is not one a project takes (member \"integrations\")");
        }
        if (integrations.contains(integration.id))
        {
            return translated("two integrations have the id \"%1\" (member \"integrations\")").arg(integration.id);
        }
        integrations.insert(integration.id);
        if (!isName(integration.name) || integration.folder.isEmpty())
        {
            return translated("the integration \"%1\" has no name or no folder (member \"integrations\")")
                .arg(integration.id);
        }
    }
    return {};
}

// ---- Reading

[[nodiscard]] ProjectDocumentRead refused(const ProjectDocumentRefusal refusal, const QString& why)
{
    ProjectDocumentRead read;
    read.refusal = refusal;
    read.why = why;
    return read;
}

[[nodiscard]] ProjectDocumentRead invalid(const QString& what)
{
    return refused(ProjectDocumentRefusal::Invalid, translated("The project's document cannot be read: %1.").arg(what));
}

[[nodiscard]] QString missing(const char* member)
{
    return translated("the member \"%1\" is missing or is not what a project states").arg(QLatin1String(member));
}

/// A member that must be a text.
[[nodiscard]] std::optional<QString> textOf(const QJsonObject& object, const char* member)
{
    const QJsonValue value = object.value(QLatin1String(member));
    return value.isString() ? std::optional<QString>(value.toString()) : std::nullopt;
}

/// A member that must be a list of objects.
[[nodiscard]] std::optional<std::vector<QJsonObject>> objectsOf(const QJsonObject& object, const char* member)
{
    const QJsonValue value = object.value(QLatin1String(member));
    if (!value.isArray())
    {
        return std::nullopt;
    }
    std::vector<QJsonObject> objects;
    const QJsonArray entries = value.toArray();
    objects.reserve(static_cast<std::size_t>(entries.size()));
    for (const QJsonValue& entry : entries)
    {
        if (!entry.isObject())
        {
            return std::nullopt;
        }
        objects.push_back(entry.toObject());
    }
    return objects;
}

} // namespace

bool isProjectId(const QString& text)
{
    if (text.isEmpty() || text.size() > kMaxIdLength)
    {
        return false;
    }
    for (const QChar character : text)
    {
        const char16_t code = character.unicode();
        const bool letter = (code >= u'a' && code <= u'z') || (code >= u'A' && code <= u'Z');
        const bool digit = code >= u'0' && code <= u'9';
        if (!letter && !digit && code != u'-' && code != u'_')
        {
            return false;
        }
    }
    return true;
}

ProjectDocumentRead parseProjectDocument(const QByteArray& bytes)
{
    QJsonParseError error;
    const QJsonDocument parsed = QJsonDocument::fromJson(bytes, &error);
    if (error.error != QJsonParseError::NoError || !parsed.isObject())
    {
        return refused(ProjectDocumentRefusal::NotJson,
                       translated("The file is not a project's document: it is cut short, or is another kind of "
                                  "file."));
    }
    const QJsonObject stored = parsed.object();

    const QJsonValue format = stored.value(QLatin1String("format"));
    if (!format.isString() || format.toString() != QLatin1String(kProjectFormat))
    {
        return refused(ProjectDocumentRefusal::NotAProject, translated("The file is not an AfterNight project."));
    }

    const QJsonValue version = stored.value(QLatin1String("version"));
    const double stated = version.isDouble() ? version.toDouble() : 0.0;
    if (!version.isDouble() || std::floor(stated) != stated || stated < 1.0)
    {
        return invalid(missing("version"));
    }
    if (stated > static_cast<double>(kProjectVersion))
    {
        return refused(ProjectDocumentRefusal::NewerVersion,
                       translated("This project was written by a newer AfterNight: its document is version %1, and "
                                  "this AfterNight reads version %2. Open it with the version that wrote it, or a "
                                  "later one.")
                           .arg(QString::number(stated, 'f', 0), QString::number(kProjectVersion)));
    }

    ProjectDocument document;
    const std::optional<QString> id = textOf(stored, "id");
    const std::optional<QString> name = textOf(stored, "name");
    const std::optional<QString> created = textOf(stored, "created");
    const std::optional<QString> origin = textOf(stored, "origin");
    const std::optional<QString> lastOpened = textOf(stored, "lastOpened");
    if (!id.has_value())
    {
        return invalid(missing("id"));
    }
    if (!name.has_value())
    {
        return invalid(missing("name"));
    }
    if (!created.has_value() || !timeFromText(*created).isValid())
    {
        return invalid(missing("created"));
    }
    const std::optional<ProjectOrigin> readOrigin = origin.has_value() ? originFromName(*origin) : std::nullopt;
    if (!readOrigin.has_value())
    {
        return invalid(missing("origin"));
    }
    if (!lastOpened.has_value() || !timeFromText(*lastOpened).isValid())
    {
        return invalid(missing("lastOpened"));
    }
    document.id = *id;
    document.name = *name;
    document.created = timeFromText(*created);
    document.origin = *readOrigin;
    document.lastOpened = timeFromText(*lastOpened);

    // The object: null or absent for none.
    const QJsonValue object = stored.value(QLatin1String("object"));
    if (!object.isNull() && !object.isUndefined())
    {
        const QJsonObject storedObject = object.toObject();
        const std::optional<QString> objectName = textOf(storedObject, "name");
        const std::optional<QString> source = textOf(storedObject, "source");
        const std::optional<ProjectObjectSource> readSource =
            source.has_value() ? sourceFromName(*source) : std::nullopt;
        if (!object.isObject() || !objectName.has_value() || !readSource.has_value())
        {
            return invalid(missing("object"));
        }
        document.object = ProjectObject{*objectName, *readSource};
    }

    const QJsonValue history = stored.value(QLatin1String("history"));
    const std::optional<QString> compression =
        history.isObject() ? textOf(history.toObject(), "compression") : std::nullopt;
    // The file states the compression by the name a setting states it, exactly.
    const std::optional<an::ui::history::StepCompression> readCompression =
        compression.has_value() ? an::ui::stepCompressionFromName(*compression) : std::nullopt;
    if (!readCompression.has_value() || compressionName(*readCompression) != *compression)
    {
        return invalid(missing("history"));
    }
    document.historyCompression = *readCompression;

    const std::optional<std::vector<QJsonObject>> images = objectsOf(stored, "images");
    if (!images.has_value())
    {
        return invalid(missing("images"));
    }
    for (const QJsonObject& storedImage : *images)
    {
        const std::optional<QString> imageId = textOf(storedImage, "id");
        const std::optional<QString> path = textOf(storedImage, "path");
        const std::optional<QString> relativePath = textOf(storedImage, "relativePath");
        const std::optional<QString> kind = textOf(storedImage, "kind");
        const std::optional<ProjectImageKind> readKind = kind.has_value() ? kindFromName(*kind) : std::nullopt;
        const QJsonValue opened = storedImage.value(QLatin1String("lastOpened"));
        const bool openedStated = opened.isString() && timeFromText(opened.toString()).isValid();
        if (!imageId.has_value() || !path.has_value() || !relativePath.has_value() || !readKind.has_value() ||
            (!opened.isNull() && !openedStated))
        {
            return invalid(missing("images"));
        }
        document.images.push_back(ProjectImage{*imageId, *path, *relativePath, *readKind,
                                               openedStated ? timeFromText(opened.toString()) : QDateTime()});
    }

    const std::optional<std::vector<QJsonObject>> workspaces = objectsOf(stored, "workspaces");
    if (!workspaces.has_value())
    {
        return invalid(missing("workspaces"));
    }
    for (const QJsonObject& storedWorkspace : *workspaces)
    {
        const std::optional<QString> workspaceId = textOf(storedWorkspace, "id");
        const std::optional<QString> workspaceName = textOf(storedWorkspace, "name");
        const std::optional<std::vector<QJsonObject>> views = objectsOf(storedWorkspace, "views");
        const QJsonValue last = storedWorkspace.value(QLatin1String("last"));
        if (!workspaceId.has_value() || !workspaceName.has_value() || !views.has_value() || !last.isBool())
        {
            return invalid(missing("workspaces"));
        }
        ProjectWorkspace workspace;
        workspace.id = *workspaceId;
        workspace.name = *workspaceName;
        workspace.last = last.toBool();
        for (const QJsonObject& storedView : *views)
        {
            const std::optional<QString> image = textOf(storedView, "image");
            const std::optional<QString> dockWidget = textOf(storedView, "dockWidget");
            if (!image.has_value() || !dockWidget.has_value())
            {
                return invalid(missing("workspaces"));
            }
            workspace.views.push_back(ProjectWorkspaceView{*image, *dockWidget});
        }
        document.workspaces.push_back(std::move(workspace));
    }

    const std::optional<std::vector<QJsonObject>> integrations = objectsOf(stored, "integrations");
    if (!integrations.has_value())
    {
        return invalid(missing("integrations"));
    }
    for (const QJsonObject& storedIntegration : *integrations)
    {
        const std::optional<QString> integrationId = textOf(storedIntegration, "id");
        const std::optional<QString> integrationName = textOf(storedIntegration, "name");
        const std::optional<QString> folder = textOf(storedIntegration, "folder");
        if (!integrationId.has_value() || !integrationName.has_value() || !folder.has_value())
        {
            return invalid(missing("integrations"));
        }
        document.integrations.push_back(ProjectIntegration{*integrationId, *integrationName, *folder});
    }

    if (const QString problem = problemOf(document); !problem.isEmpty())
    {
        return invalid(problem);
    }
    ProjectDocumentRead read;
    read.document = std::move(document);
    return read;
}

ProjectDocumentRead readProjectDocument(const QString& filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly))
    {
        return refused(ProjectDocumentRefusal::Unreadable,
                       translated("The project's document '%1' cannot be opened: %2.")
                           .arg(QDir::toNativeSeparators(filePath), file.errorString()));
    }
    if (file.size() > kMaxProjectDocumentBytes)
    {
        return refused(ProjectDocumentRefusal::Unreadable,
                       translated("The file '%1' is larger than a project's document is.")
                           .arg(QDir::toNativeSeparators(filePath)));
    }
    // One byte past the bound tells a file that grew while it was read from one that fits.
    const QByteArray bytes = file.read(kMaxProjectDocumentBytes + 1);
    if (file.error() != QFileDevice::NoError || bytes.size() > kMaxProjectDocumentBytes)
    {
        return refused(ProjectDocumentRefusal::Unreadable,
                       translated("The project's document '%1' cannot be read: %2.")
                           .arg(QDir::toNativeSeparators(filePath), file.errorString()));
    }
    return parseProjectDocument(bytes);
}

QString checkProjectDocument(const ProjectDocument& document)
{
    return problemOf(document);
}

QByteArray formatProjectDocument(const ProjectDocument& document)
{
    QJsonObject stored;
    stored.insert(QStringLiteral("format"), QString::fromLatin1(kProjectFormat));
    stored.insert(QStringLiteral("version"), kProjectVersion);
    stored.insert(QStringLiteral("id"), document.id);
    stored.insert(QStringLiteral("name"), document.name);
    stored.insert(QStringLiteral("created"), timeText(document.created));
    stored.insert(QStringLiteral("origin"), originName(document.origin));
    stored.insert(QStringLiteral("object"),
                  document.object.has_value()
                      ? QJsonValue(QJsonObject{{QStringLiteral("name"), document.object->name},
                                               {QStringLiteral("source"), sourceName(document.object->source)}})
                      : QJsonValue(QJsonValue::Null));
    stored.insert(QStringLiteral("history"),
                  QJsonObject{{QStringLiteral("compression"), compressionName(document.historyCompression)}});

    QJsonArray images;
    for (const ProjectImage& image : document.images)
    {
        images.append(QJsonObject{{QStringLiteral("id"), image.id},
                                  {QStringLiteral("path"), image.path},
                                  {QStringLiteral("relativePath"), image.relativePath},
                                  {QStringLiteral("kind"), kindName(image.kind)},
                                  {QStringLiteral("lastOpened"), image.lastOpened.isNull()
                                                                     ? QJsonValue(QJsonValue::Null)
                                                                     : QJsonValue(timeText(image.lastOpened))}});
    }
    stored.insert(QStringLiteral("images"), images);

    QJsonArray workspaces;
    for (const ProjectWorkspace& workspace : document.workspaces)
    {
        QJsonArray views;
        for (const ProjectWorkspaceView& view : workspace.views)
        {
            views.append(
                QJsonObject{{QStringLiteral("image"), view.image}, {QStringLiteral("dockWidget"), view.dockWidget}});
        }
        workspaces.append(QJsonObject{{QStringLiteral("id"), workspace.id},
                                      {QStringLiteral("name"), workspace.name},
                                      {QStringLiteral("views"), views},
                                      {QStringLiteral("last"), workspace.last}});
    }
    stored.insert(QStringLiteral("workspaces"), workspaces);

    QJsonArray integrations;
    for (const ProjectIntegration& integration : document.integrations)
    {
        integrations.append(QJsonObject{{QStringLiteral("id"), integration.id},
                                        {QStringLiteral("name"), integration.name},
                                        {QStringLiteral("folder"), integration.folder}});
    }
    stored.insert(QStringLiteral("integrations"), integrations);
    stored.insert(QStringLiteral("lastOpened"), timeText(document.lastOpened));
    return QJsonDocument(stored).toJson(QJsonDocument::Indented);
}

QString writeProjectDocument(const QString& filePath, const ProjectDocument& document)
{
    if (const QString problem = problemOf(document); !problem.isEmpty())
    {
        return translated("The project's document was not written: %1.").arg(problem);
    }
    const QByteArray bytes = formatProjectDocument(document);
    const auto failed = [&filePath](const QString& reason) {
        return translated("The project's document '%1' could not be written: %2. The document is as it was.")
            .arg(QDir::toNativeSeparators(filePath), reason);
    };
    // The bytes are written beside the document and take its place once whole; nothing is written in place.
    QSaveFile file(filePath);
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly))
    {
        return failed(file.errorString());
    }
    if (file.write(bytes) != bytes.size())
    {
        const QString reason = file.errorString();
        file.cancelWriting();
        return failed(reason);
    }
    if (!file.commit())
    {
        return failed(file.errorString());
    }
    return {};
}

QString projectRelativePath(const QString& projectFolder, const QString& filePath)
{
    if (projectFolder.trimmed().isEmpty() || filePath.trimmed().isEmpty())
    {
        return {};
    }
    const QString folder = QDir::cleanPath(QDir::fromNativeSeparators(projectFolder));
    const QString file = QDir::cleanPath(QDir::fromNativeSeparators(filePath));
    const QString relative = QDir(folder).relativeFilePath(file);
    // No relative path reaches another drive: the answer is then the file's own path.
    return QDir::isAbsolutePath(relative) ? QString() : relative;
}

} // namespace an::ui::shell
