// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightControlsExport.h"

#include <cstddef>
#include <functional>
#include <memory>
#include <string>

#include <QString>
#include <QStringList>

namespace an::consumer
{
class ImageAsset;
} // namespace an::consumer

class GlobalSettingsPersistence;
class ImageViewControl;
class ProgressDialogControl;
class QObject;
class QWidget;

namespace an::ui
{

class IViewFactory;
class RawOpenPolicy;

inline QString buildOpenProgressDescription(const std::string& fileName, size_t index, size_t totalFiles)
{
    const QString qFileName = QString::fromUtf8(fileName);
    if (totalFiles > 1)
    {
        return QStringLiteral("Opening file %1/%2: '%3'...")
            .arg(static_cast<qulonglong>(index))
            .arg(static_cast<qulonglong>(totalFiles))
            .arg(qFileName);
    }

    return QStringLiteral("Opening file '%1'...").arg(qFileName);
}

inline QWidget* resolveOpenPromptParent(QWidget* progressDialog, QWidget* fallbackParent)
{
    return progressDialog ? progressDialog : fallbackParent;
}

/// What one open of files came to.
struct FileOpenOutcome
{
    /// Files shown in a view.
    std::size_t opened = 0u;

    /// Files that could not be read or shown.
    std::size_t failed = 0u;

    /// Whether the open was stopped before every file was read.
    bool cancelled = false;
};

/// What one save came to.
struct FileSaveOutcome
{
    /// Files written whole.
    std::size_t written = 0u;

    /// Whether a file could not be written; nothing is left at its path.
    bool failed = false;

    /// Whether the view still showed what was written when the write ended, so its pending changes were cleared.
    bool viewMarkedSaved = false;
};

/// The file menu's open and save, run as requests: every read and write runs on a worker over the process's engine
/// session, and every decision, question and view is the GUI thread's, which never waits for a worker.
///
/// An open reads each file under the RAW policy published when the open was asked for and the demosaic setting read
/// then, so a setting changed meanwhile reaches the next open and never this one. A save writes the revision the view's
/// document held when it was asked for, with the intent the export settings gave then, and clears the view's pending
/// changes only when the view still shows that revision; a view closed meanwhile is never touched.
class AFTERNIGHTCONTROLS_API FileWorkflowRunner
{
public:
    struct Dependencies
    {
        /// The object whose life bounds a run: nothing is published, and nothing below is called, once it is gone.
        QObject* context = nullptr;

        QWidget* dialogParent = nullptr;
        GlobalSettingsPersistence* globalSettingsPersistence = nullptr;

        /// The RAW policy published now; an open takes it once, when it is asked for. Without one an open reads
        /// through the session's own registry and treats no camera frame.
        std::function<std::shared_ptr<const RawOpenPolicy>()> rawOpenPolicy;

        std::function<void(const QString&)> persistLastOpenDirectory;
        std::function<void()> switchToDockManager;

        /// What makes the views an open shows; null, the application context's.
        IViewFactory* viewFactory = nullptr;

        /// Records a file in the recent list with the image it showed, for its thumbnail.
        std::function<void(const std::string&, const std::shared_ptr<const an::consumer::ImageAsset>&)> addRecentFile;

        std::function<void(const std::string&)> removeRecentFile;
        std::function<void()> updateImageViewControlList;

        /// Told what an open came to once it has ended; may be empty.
        std::function<void(const FileOpenOutcome&)> openFinished;

        /// Told what a save came to once it has ended; may be empty.
        std::function<void(const FileSaveOutcome&)> saveFinished;
    };

    /// Opens files one after another, each in a view of its own docked beside the first: a sequence file as a sequence
    /// view, any other through the RAW policy, a mosaic offered for demosaicing by the method the demosaic setting
    /// names. Called on the GUI thread; returns at once.
    ///
    /// @param dependencies what the open reports to.
    /// @param files the files.
    /// @param directory the folder remembered as the last one opened once the open ends.
    /// @param progress the progress dialog, whose stop ends the open; closed when the open ends.
    static void openFiles(const Dependencies& dependencies, QStringList files, QString directory,
                          ProgressDialogControl* progress);

    /// Opens one file of the recent list as `openFiles` opens one; its folder becomes the last one opened, and a file
    /// that cannot be read leaves the recent list.
    ///
    /// @param dependencies what the open reports to.
    /// @param file the file.
    /// @param progress the progress dialog, closed when the open ends.
    static void openRecentFile(const Dependencies& dependencies, QString file, ProgressDialogControl* progress);

    /// Writes what a view shows to files: its image through the output transaction with the intent the export settings
    /// give, or its sequence through the sequence's own writer. Called on the GUI thread; returns at once.
    ///
    /// @param dependencies what the save reports to.
    /// @param view the view; read now, and touched later only while it lives.
    /// @param files the files, each whole or absent when the save ends.
    /// @param directory the folder remembered as the last one opened once the save ends.
    /// @param progress the progress dialog, whose stop ends the save; closed when the save ends.
    static void saveFile(const Dependencies& dependencies, ImageViewControl& view, QStringList files, QString directory,
                         ProgressDialogControl* progress);
};

} // namespace an::ui
