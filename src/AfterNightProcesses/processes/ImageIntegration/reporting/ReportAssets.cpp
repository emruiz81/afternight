// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "processes/ImageIntegration/reporting/ReportAssets.h"

#include "processes/ImageIntegration/reporting/ReportModel.h"

#include <QBuffer>
#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QImageWriter>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>
#include <utility>

namespace ImageIntegration::Reporting
{
namespace
{
/// A path with forward slashes and no trailing one.
[[nodiscard]] QString withForwardSlashes(QString path)
{
    path.replace(QLatin1Char('\\'), QLatin1Char('/'));
    while (path.size() > 1 && path.endsWith(QLatin1Char('/')))
    {
        path.chop(1);
    }
    return path;
}

/// The folder a file's path names, with forward slashes; empty for a bare name.
[[nodiscard]] QString folderOf(const QString& path)
{
    const QString normal = withForwardSlashes(path.trimmed());
    const qsizetype slash = normal.lastIndexOf(QLatin1Char('/'));
    return slash > 0 ? normal.left(slash) : QString();
}

/// Replaces every occurrence of a folder's path, in either spelling of the separator, without regard to case: a
/// message may spell a path as the host does or as Qt does, and a drive's letter in either case.
void replaceFolder(QString& text, const QString& folder, const QString& replacement)
{
    if (folder.isEmpty())
    {
        return;
    }
    QString native = folder;
    native.replace(QLatin1Char('/'), QLatin1Char('\\'));
    text.replace(folder, replacement, Qt::CaseInsensitive);
    if (native != folder)
    {
        text.replace(native, replacement, Qt::CaseInsensitive);
    }
}

/// A relative path as a link's value. A browser reads `#` as the start of a fragment, `?` as the start of a query
/// and `%` as the start of an escape, and a folder named after a target - "Panel #2" - holds them: each is escaped,
/// so the path names the file it names on disk.
[[nodiscard]] QString linkOf(QString path)
{
    path.replace(QLatin1Char('%'), QStringLiteral("%25"));
    path.replace(QLatin1Char('#'), QStringLiteral("%23"));
    path.replace(QLatin1Char('?'), QStringLiteral("%3F"));
    return path;
}

/// Takes out of a text the folders of the paths the run wrote into it that the report's own files do not name: a
/// catalog's folder, a solver's temporary file, the application's own. The folder of a person's profile goes first
/// and whole, whatever is in its name, since it is their name; of any other absolute path what stays is its last
/// part, as far as the path can be told from the words around it - a folder with a space in its name ends the part
/// that is taken out. The text is a sentence or a log, so this is a rule over words, not over files: it is for a
/// document meant to be sent, and errs towards taking out.
void hideOtherFolders(QString& text)
{
    // A profile: `C:\Users\<name>\`, `/home/<name>/`, `/Users/<name>/`.
    static const QRegularExpression windowsProfile(
        QStringLiteral(R"((?<![A-Za-z0-9_])[A-Za-z]:[\\/]+Users[\\/]+[^\\/:*?"<>|\r\n]+[\\/]+)"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression windowsProfileAlone(
        QStringLiteral(R"((?<![A-Za-z0-9_])[A-Za-z]:[\\/]+Users[\\/]+[^\\/:*?"<>|'\s]+)"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression posixProfile(
        QStringLiteral(R"((?<![A-Za-z0-9_.~/\\:<])/(?:home|Users)/[^/\s:*?"<>|']+)"));
    text.replace(windowsProfile, QStringLiteral("~/"));
    text.replace(windowsProfileAlone, QStringLiteral("~"));
    text.replace(posixProfile, QStringLiteral("~"));

    // What is left of an absolute path: a drive's, a profile's, a share's, a root's. Its folders go, its last part
    // stays.
    static const QRegularExpression driveFolders(
        QStringLiteral(R"((?<![A-Za-z0-9_])(?:[A-Za-z]:|~)[\\/]+(?:[^\\/:*?"<>|'\s]+[\\/]+)*)"));
    static const QRegularExpression shareFolders(
        QStringLiteral(R"((?<![A-Za-z0-9_:\\])\\\\[^\\/:*?"<>|'\s]+\\(?:[^\\/:*?"<>|'\s]+\\)*)"));
    static const QRegularExpression rootFolders(QStringLiteral(R"((?<![A-Za-z0-9_.~/\\:<])/(?:[^/\s:*?"<>|']+/)+)"));
    text.remove(driveFolders);
    text.remove(shareFolders);
    text.remove(rootFolders);
}
} // namespace

QByteArray boundedJpeg(const QString& file, const int longestSide, const int quality)
{
    QImageReader reader(file);
    reader.setAutoTransform(true);
    QImage image = reader.read();
    if (image.isNull() || longestSide <= 0)
    {
        return {};
    }
    if ((std::max)(image.width(), image.height()) > longestSide)
    {
        image = image.scaled(longestSide, longestSide, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }
    // A JPEG holds no transparency; a picture with any is laid over black, as a viewer shows it.
    if (image.hasAlphaChannel())
    {
        image = image.convertToFormat(QImage::Format_RGB32);
    }
    QByteArray bytes;
    QBuffer buffer(&bytes);
    if (!buffer.open(QIODevice::WriteOnly))
    {
        return {};
    }
    QImageWriter writer(&buffer, "jpeg");
    writer.setQuality(std::clamp(quality, 0, 100));
    if (!writer.write(image))
    {
        return {};
    }
    return bytes;
}

ReportAssets::ReportAssets(const ReportForm form, QString runDirectory)
    : m_form(form), m_runDirectory(std::move(runDirectory))
{
}

void ReportAssets::hideFoldersOf(const ReportModel& model)
{
    if (m_form != ReportForm::Embedded)
    {
        return;
    }
    m_outputFolder = withForwardSlashes(model.run.outputFolder.trimmed());
    m_outputFolderName = QFileInfo(m_outputFolder).fileName();

    QSet<QString> folders;
    const auto add = [&folders](const QString& file) {
        const QString folder = folderOf(file);
        // A file in a drive's root has no folder to drop: the drive alone ("E:") has no name, and taking it out
        // of a text would take the same two letters out of every word that ends in them before a colon.
        if (!folder.isEmpty() && !QFileInfo(folder).fileName().isEmpty())
        {
            folders.insert(folder);
        }
    };
    const auto addFrames = [&add](const std::vector<ReportFrame>& frames) {
        for (const ReportFrame& frame : frames)
        {
            add(frame.path);
        }
    };
    for (const ReportMaster& master : model.masters)
    {
        add(master.file);
        addFrames(master.frames);
        for (const ReportMap& map : master.maps)
        {
            add(map.file);
        }
    }
    for (const ReportGroupNotStacked& group : model.groupsNotStacked)
    {
        addFrames(group.frames);
    }
    for (const ReportCalibrationMaster& master : model.calibrationMasters)
    {
        add(master.file);
        for (const QString& input : master.inputFiles)
        {
            add(input);
        }
    }
    if (!m_runDirectory.trimmed().isEmpty())
    {
        add(withForwardSlashes(m_runDirectory.trimmed()) + QStringLiteral("/."));
    }
    // The output folder keeps its name in a message, so it is replaced on its own and is no folder to drop.
    folders.remove(m_outputFolder);

    m_hiddenFolders = QStringList(folders.begin(), folders.end());
    // The longest first, so a folder inside another is dropped whole before its parent is looked for.
    std::sort(m_hiddenFolders.begin(), m_hiddenFolders.end(), [](const QString& left, const QString& right) {
        return left.size() != right.size() ? left.size() > right.size() : left < right;
    });
}

QString ReportAssets::image(const QString& relativePath)
{
    const QString relative = withForwardSlashes(relativePath.trimmed());
    if (relative.isEmpty())
    {
        return {};
    }
    if (m_form == ReportForm::Linked)
    {
        return linkOf(relative);
    }
    return embedded(relative, kEmbeddedImageLongestSide);
}

QString ReportAssets::thumbnail(const QString& relativePath)
{
    const QString relative = withForwardSlashes(relativePath.trimmed());
    if (relative.isEmpty())
    {
        return {};
    }
    if (m_form == ReportForm::Linked)
    {
        return linkOf(relative);
    }
    return embedded(relative, kEmbeddedThumbnailLongestSide);
}

QString ReportAssets::embedded(const QString& relative, const int longestSide)
{
    const QString key = QString::number(longestSide) + QLatin1Char('|') + relative;
    const auto known = m_embedded.constFind(key);
    if (known != m_embedded.constEnd())
    {
        return *known;
    }
    QString source;
    if (!m_runDirectory.trimmed().isEmpty())
    {
        const QByteArray jpeg =
            boundedJpeg(QDir(m_runDirectory).filePath(relative), longestSide, kEmbeddedImageQuality);
        if (!jpeg.isEmpty())
        {
            source = QStringLiteral("data:image/jpeg;base64,") + QString::fromLatin1(jpeg.toBase64());
        }
    }
    m_embedded.insert(key, source);
    return source;
}

QString ReportAssets::path(const QString& path) const
{
    if (m_form == ReportForm::Linked)
    {
        return path;
    }
    const QString normal = withForwardSlashes(path.trimmed());
    return normal.mid(normal.lastIndexOf(QLatin1Char('/')) + 1);
}

QString ReportAssets::message(const QString& text) const
{
    if (m_form == ReportForm::Linked)
    {
        return text;
    }
    QString shown = text;
    for (const QString& folder : m_hiddenFolders)
    {
        // The folder goes with the separator after it, so the file's name is what stays.
        replaceFolder(shown, folder + QLatin1Char('/'), QString());
        replaceFolder(shown, folder, QFileInfo(folder).fileName());
    }
    // An output folder that is a drive's root has no name to stand for it; the rule below takes the drive out.
    if (!m_outputFolderName.isEmpty())
    {
        replaceFolder(shown, m_outputFolder, m_outputFolderName);
    }
    // What the run wrote may name folders none of the report's files is in.
    hideOtherFolders(shown);
    return shown;
}
} // namespace ImageIntegration::Reporting
