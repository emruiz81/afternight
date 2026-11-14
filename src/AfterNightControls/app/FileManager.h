// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightControlsExport.h"
#include "app/FileDialogUtils.h"
#include "app/FileWorkflowRunner.h"

#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>

#include <algorithm>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <stop_token>
#include <string>

#include <QFileDialog>
#include <QFileInfo>
#include <QSize>
#include <QString>
#include <QStringList>

namespace an::consumer
{
class EngineSession;
class ImageAsset;
} // namespace an::consumer

class QAction;
class FolderTrackerPersistence;
class GlobalSettingsPersistence;
class ImageViewControl;
class QMenu;
class QObject;
class ThumbnailPersistence;

namespace an::ui
{

class IViewFactory;
class RawOpenPolicy;

inline QString resolveSequenceSaveSuffix(const int sequenceType, const QString& sourcePath,
                                         const QString& configuredStillFormat)
{
    if (sequenceType == 1)
    {
        return QStringLiteral("ser");
    }
    if (sequenceType == 2)
    {
        return QStringLiteral("anseq");
    }
    if (sequenceType == 3)
    {
        const QString sourceSuffix = QFileInfo(sourcePath).suffix().toLower();
        static const QStringList videoSuffixes = {QStringLiteral("avi"),  QStringLiteral("mp4"), QStringLiteral("mpg"),
                                                  QStringLiteral("mpeg"), QStringLiteral("mov"), QStringLiteral("mkv"),
                                                  QStringLiteral("webm")};
        return videoSuffixes.contains(sourceSuffix) ? sourceSuffix : QStringLiteral("mp4");
    }

    const QString normalizedFormat = configuredStillFormat.toLower();
    if (normalizedFormat == QStringLiteral("jpeg"))
    {
        return QStringLiteral("jpg");
    }
    return normalizedFormat.isEmpty() ? QStringLiteral("tiff") : normalizedFormat;
}

inline bool updateRecentFileList(std::deque<std::string>& recentFiles, const std::string& normalizedFileName,
                                 const size_t recentFilesLimit)
{
    if (normalizedFileName.empty())
    {
        return false;
    }

    if (!recentFiles.empty() && recentFiles.front() == normalizedFileName)
    {
        return false;
    }

    auto it = std::find(recentFiles.begin(), recentFiles.end(), normalizedFileName);
    if (it != recentFiles.end())
    {
        recentFiles.erase(it);
    }

    recentFiles.push_front(normalizedFileName);

    while (recentFiles.size() > recentFilesLimit)
    {
        recentFiles.pop_back();
    }

    return true;
}

[[nodiscard]] inline bool isLikelySupportedImagePath(const QString& filePath)
{
    const QString suffix = QFileInfo(filePath).suffix();
    return dialogs::detail::isSupportedImageSuffix(suffix) || dialogs::detail::isSupportedSequenceSuffix(suffix);
}

inline constexpr int RECENT_FILE_THUMBNAIL_MAX_DIMENSION = 2400;

inline QSize recentFileThumbnailSize(const QSize& sourceSize)
{
    if (!sourceSize.isValid() || sourceSize.isEmpty())
    {
        return sourceSize;
    }

    const int width = sourceSize.width();
    const int height = sourceSize.height();
    if (width <= RECENT_FILE_THUMBNAIL_MAX_DIMENSION && height <= RECENT_FILE_THUMBNAIL_MAX_DIMENSION)
    {
        return sourceSize;
    }

    if (width > height)
    {
        const int scaledHeight =
            (std::max)(1,
                       static_cast<int>((static_cast<qint64>(height) * RECENT_FILE_THUMBNAIL_MAX_DIMENSION) / width));
        return QSize(RECENT_FILE_THUMBNAIL_MAX_DIMENSION, scaledHeight);
    }

    const int scaledWidth =
        (std::max)(1, static_cast<int>((static_cast<qint64>(width) * RECENT_FILE_THUMBNAIL_MAX_DIMENSION) / height));
    return QSize(scaledWidth, RECENT_FILE_THUMBNAIL_MAX_DIMENSION);
}

/// The JPEG quality a recent file's thumbnail is written with.
inline constexpr std::uint32_t kRecentFileThumbnailQuality = 95u;

/// What a recent file's thumbnail shows of an image: the image reduced so its longer side is at most
/// `RECENT_FILE_THUMBNAIL_MAX_DIMENSION`, a mosaic shown as the grey picture its codes make, and - when the image is
/// linear, its processing-state cards record it so or it is a camera RAW frame - stretched by the display
/// presentation's automatic stretch, linked over a colour image's three channels so its colour balance survives.
///
/// @param session the process's session.
/// @param image the image, read under a lease for the call.
/// @param stop the request's stop.
/// @return the thumbnail's image - `image` itself when it needs neither a reduction nor a stretch; or what the engine
///         refused.
[[nodiscard]] AFTERNIGHTCONTROLS_API an::Result<std::shared_ptr<const an::consumer::ImageAsset>>
renderRecentFileThumbnail(const an::consumer::EngineSession& session,
                          const std::shared_ptr<const an::consumer::ImageAsset>& image, std::stop_token stop);

/// Writes a recent file's thumbnail as an eight-bit JPEG through the output transaction: whole, or absent.
///
/// @param session the process's session.
/// @param image the image the thumbnail shows.
/// @param path the JPEG file.
/// @param stop the request's stop.
/// @return success, or what the rendering or the write refused.
[[nodiscard]] AFTERNIGHTCONTROLS_API an::Status
writeRecentFileThumbnail(const an::consumer::EngineSession& session,
                         const std::shared_ptr<const an::consumer::ImageAsset>& image, const QString& path,
                         std::stop_token stop);

/// The file menu's open and save and the recent-file list with its thumbnails.
///
/// Everything here is called on the GUI thread. An open or a save is handed to `FileWorkflowRunner` and returns at
/// once; a thumbnail is rendered and written on a worker and recorded on the GUI thread when it lands, unless a later
/// thumbnail of the same file was asked for meanwhile.
class AFTERNIGHTCONTROLS_API FileManager
{
public:
    struct Dependencies
    {
        QObject* signalContext = nullptr;
        /// The menu that lists the recent files; none, the files are kept but listed nowhere.
        QMenu* recentFilesMenu = nullptr;
        std::deque<std::string>* recentFiles = nullptr;
        std::string* lastOpenDirectory = nullptr;
        ThumbnailPersistence* thumbnailPersistence = nullptr;
        FolderTrackerPersistence* folderTrackerPersistence = nullptr;
        GlobalSettingsPersistence* globalSettingsPersistence = nullptr;
        std::function<void(const QString&)> persistLastOpenDirectory;
        std::function<QString()> thumbnailsDirPath;
        std::function<void()> switchToDockManager;

        /// What makes the views a file opened is shown in; null, the application context's.
        IViewFactory* viewFactory = nullptr;
        std::function<void()> updateImageViewControlList;

        /// How many files the recent list keeps; empty, twenty.
        std::function<size_t()> recentFilesLimit;

        /// Writes the recent list where it is kept, after a change; empty, the list is kept in memory only.
        std::function<void()> saveRecentFiles;

        /// The dashboard reloads the recent files' thumbnails.
        std::function<void()> reloadRecentFilesThumbnails;

        /// The dashboard learns that the recent list was cleared.
        std::function<void()> notifyRecentFilesListDeleted;

        /// The RAW policy published now, which an open takes when it is asked for.
        std::function<std::shared_ptr<const RawOpenPolicy>()> rawOpenPolicy;

        /// Told what an open came to; may be empty.
        std::function<void(const FileOpenOutcome&)> openFinished;

        /// Told what a save came to; may be empty.
        std::function<void(const FileSaveOutcome&)> saveFinished;

        /// Told the file and thumbnail recorded once a thumbnail lands; may be empty.
        std::function<void(const std::string&, const QString&)> thumbnailRecorded;
    };

    explicit FileManager(const Dependencies& deps);
    ~FileManager();

    FileManager(const FileManager&) = delete;
    FileManager& operator=(const FileManager&) = delete;

    void setPersistenceDependencies(ThumbnailPersistence* thumbnailPersistence,
                                    FolderTrackerPersistence* folderTrackerPersistence,
                                    GlobalSettingsPersistence* globalSettingsPersistence);

    void openDroppedFiles(const QStringList& files) const;
    void openFile() const;
    void openRecentSingleFile(const QString& filePath) const;
    void saveFile() const;

    /// Opens files as the file dialog or a drop does, without a dialog.
    void openFiles(const QStringList& files, const QString& directory) const;

    /// Writes what a view shows to a file as Save does, without a dialog.
    void saveViewToFile(ImageViewControl& view, const QString& path) const;

    size_t getRecentFilesLimit() const;
    void persistRecentFiles() const;
    void updateRecentFilesMenuEntries() const;

    void addRecentFile(const std::string& fileName, const std::shared_ptr<const an::consumer::ImageAsset>& image) const;
    void removeRecentFile(const std::string& fileName) const;

    void persistRecentFileThumbnail(const std::string& fileName,
                                    const std::shared_ptr<const an::consumer::ImageAsset>& image) const;
    void removeRecentFileThumbnail(const std::string& fileName) const;
    void clearRecentFilesThumbnails() const;
    void updatePersistedRecentFileThumbnail(const std::string& fileName,
                                            const std::shared_ptr<const an::consumer::ImageAsset>& image) const;

    void clearRecentFilesList() const;

private:
    struct ThumbnailRequests;

    [[nodiscard]] FileWorkflowRunner::Dependencies workflowDependencies() const;

    Dependencies m_deps;
    std::shared_ptr<ThumbnailRequests> m_thumbnails;
};

} // namespace an::ui
