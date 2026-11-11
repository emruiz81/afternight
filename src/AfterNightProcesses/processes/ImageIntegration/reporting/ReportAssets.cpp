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
        if (!folder.isEmpty())
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
        folders.insert(withForwardSlashes(m_runDirectory.trimmed()));
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
        return relative;
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
        return relative;
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
    replaceFolder(shown, m_outputFolder, m_outputFolderName);
    return shown;
}
} // namespace ImageIntegration::Reporting
