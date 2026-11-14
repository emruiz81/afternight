// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "services/FileDialogService.h"

#include "core/ApplicationCore.h"
#include "services/DialogService.h"

#include "app/FileDialogUtils.h"
#include "app/MessageBoxes.h"
#include "controls/FolderTrackerPersistence.h"
#include "controls/GlobalSettingsPersistence.h"
#include "controls/settings/SettingsDefaults.h"
#include "controls/settings/SettingsKeys.h"

#include <QDialog>
#include <QDir>
#include <QStandardPaths>
#include <QUrl>

#include <filesystem>
#include <string>

namespace an::ui::shell
{

namespace
{

/// Sets up a dialog that opens or saves images: where it opens, the name filters of the formats it offers and the one
/// selected, and - to save - the suffix the export setting chooses.
///
/// @param onlySequences 0: images and sequences as `acceptSequences` says; 1: SER only; 2: image lists only; 3: AVI
/// only.
void initializeImageFileDialog(GlobalSettingsPersistence* settings, const std::string& defaultDir,
                               const std::string& defaultFile, QFileDialog& dialog,
                               const QFileDialog::AcceptMode acceptMode, const bool acceptSequences,
                               const int onlySequences, const bool acceptMultipleFiles)
{
    if (acceptMode == QFileDialog::AcceptOpen)
    {
        an::ui::dialogs::applyCompactOpenFilterPresentation(dialog);
    }

    if (defaultDir.empty())
    {
        const QStringList picturesLocations = QStandardPaths::standardLocations(QStandardPaths::PicturesLocation);
        dialog.setDirectory(picturesLocations.isEmpty() ? QDir::currentPath() : picturesLocations.last());
    }
    else
    {
        dialog.setDirectory(QString::fromUtf8(defaultDir));
    }

    dialog.setFileMode(acceptMultipleFiles ? QFileDialog::ExistingFiles : QFileDialog::ExistingFile);

    QStringList allSupportedPatterns;
    QStringList nameFilters;

    if (!onlySequences)
    {
        // Common formats
        nameFilters.append("PNG files (*.png)");
        nameFilters.append("JPEG files (*.jpg *.jpeg)");
        nameFilters.append("TIFF files (*.tif *.tiff)");
        nameFilters.append("FITS files (*.fit *.fits)");
        nameFilters.append("XISF files (*.xisf)");

        allSupportedPatterns << "*.png" << "*.jpg" << "*.jpeg" << "*.tif" << "*.tiff" << "*.fit" << "*.fits"
                             << "*.xisf";

        if (acceptMode != QFileDialog::AcceptSave)
        {
            // RAW formats
            nameFilters.append("Canon RAW (*.crw *.cr2 *.cr3)");
            nameFilters.append("Nikon RAW (*.nef)");
            nameFilters.append("Sony RAW (*.arw *.sr2 *.srf)");
            nameFilters.append("Adobe DNG (*.dng)");
            nameFilters.append("Kodak RAW (*.dcr *.k25 *.kdc)");
            nameFilters.append("Epson RAW (*.erf)");
            nameFilters.append("Minolta RAW (*.mrw)");
            nameFilters.append("Olympus RAW (*.orf)");
            nameFilters.append("Pentax RAW (*.pef)");
            nameFilters.append("Fuji RAW (*.raf)");
            nameFilters.append("Panasonic RAW (*.raw)");
            nameFilters.append("Sigma RAW (*.x3f)");

            allSupportedPatterns << "*.crw" << "*.cr2" << "*.cr3" << "*.nef" << "*.dng" << "*.arw" << "*.sr2" << "*.srf"
                                 << "*.dcr" << "*.k25" << "*.kdc" << "*.erf" << "*.mrw" << "*.orf" << "*.pef" << "*.raf"
                                 << "*.raw" << "*.x3f";
        }
    }

    if (acceptSequences)
    {
        // TODO: Replace this with a more generic way to handle sequences
        // For example, we should be able to chose the destination sequence type (SER, AVI, MP4, Image list, etc.)
        // from any source sequence. Maybe show a dialog to chose the codec
        if (acceptMode == QFileDialog::AcceptSave)
        {
            if (onlySequences == 1)
                nameFilters.append("SER sequence (*.ser)");
            else if (onlySequences == 2)
                nameFilters.append("AfterNight sequence (*.anseq)");
            else if (onlySequences == 3)
            {
                // TODO: Show a selection dialog to chose the codec (RAW, H264, H265, etc.)
                nameFilters.append("RAW AVI Sequence (*.avi)");
                nameFilters.append("MP4 sequence (*.mp4)");
            }
        }
        else
        {
            nameFilters.append("SER sequence (*.ser)");
            nameFilters.append("AVI sequence (*.avi)");
            nameFilters.append("MP4 sequence (*.mp4)");
            nameFilters.append("MOV sequence (*.mov)");
            nameFilters.append("MKV sequence (*.mkv)");
            nameFilters.append("MPG sequence (*.mpg)");
            nameFilters.append("WEBM sequence (*.webm)");
            nameFilters.append("MPEG sequence (*.mpeg)");
            nameFilters.append("AfterNight sequence (*.anseq)");
        }

        allSupportedPatterns << "*.ser" << "*.anseq" << "*.mp4" << "*.avi" << "*.mov" << "*.mkv" << "*.mpg" << "*.mpeg"
                             << "*.webm";
    }

    const QString allSupportedFilter = an::ui::dialogs::supportedFormatsNameFilter(allSupportedPatterns);
    nameFilters.prepend(allSupportedFilter);

    dialog.setNameFilters(nameFilters);
    dialog.selectNameFilter(allSupportedFilter);
    dialog.setAcceptMode(acceptMode);

    if (acceptMode == QFileDialog::AcceptSave)
    {
        if (onlySequences == 1)
        {
            dialog.setDefaultSuffix("ser");
        }
        else if (onlySequences == 2)
        {
            dialog.setDefaultSuffix("anseq");
        }
        else
        {
            // Get default save format from settings
            QString defaultFormat = QString(SettingsDefaults::GENERAL_DEFAULT_SAVE_FORMAT);
            if (settings)
            {
                defaultFormat = settings->select(SettingsKeys::GENERAL_DEFAULT_SAVE_FORMAT,
                                                 QString(SettingsDefaults::GENERAL_DEFAULT_SAVE_FORMAT));
            }

            // Map format setting to file suffix and filter selection
            QString suffix = "tiff";
            QString filterToSelect;

            if (defaultFormat == "tiff")
            {
                suffix = "tiff";
                filterToSelect = "TIFF files (*.tif *.tiff)";
            }
            else if (defaultFormat == "fits")
            {
                suffix = "fits";
                filterToSelect = "FITS files (*.fit *.fits)";
            }
            else if (defaultFormat == "png")
            {
                suffix = "png";
                filterToSelect = "PNG files (*.png)";
            }
            else if (defaultFormat == "xisf")
            {
                suffix = "xisf";
                filterToSelect = "XISF files (*.xisf)";
            }
            else if (defaultFormat == "jpeg" || defaultFormat == "jpg")
            {
                suffix = "jpg";
                filterToSelect = "JPEG files (*.jpg *.jpeg)";
            }

            dialog.setDefaultSuffix(suffix);

            // Select the matching filter if found
            if (!filterToSelect.isEmpty())
            {
                dialog.selectNameFilter(filterToSelect);
            }
        }

        dialog.selectFile(QString::fromUtf8(defaultFile));
    }
}

/// The folder a context remembers, when it still exists; empty otherwise.
[[nodiscard]] QString rememberedFolder(FolderTrackerPersistence* folders, const QString& folderContext)
{
    if (folders == nullptr || folderContext.isEmpty())
    {
        return {};
    }
    const QString savedFolder = folders->select(folderContext);
    return !savedFolder.isEmpty() && QDir(savedFolder).exists() ? savedFolder : QString();
}

} // namespace

FileDialogService::FileDialogService(ApplicationCore& core) : m_core(core) {}

QStringList FileDialogService::selectImageFiles(QWidget* parent, const QString& title, const QString& folderContext,
                                                const bool supportMultipleFiles)
{
    std::string folder = m_core.lastOpenDirectory();
    if (const QString savedFolder = rememberedFolder(m_core.folderStore(), folderContext); !savedFolder.isEmpty())
    {
        folder = savedFolder.toUtf8().toStdString();
    }

    QFileDialog dialog(dialogParentFor(m_core, parent), title);
    initializeImageFileDialog(m_core.globalSettingsStore(), folder, std::string(), dialog, QFileDialog::AcceptOpen,
                              false, 0, supportMultipleFiles);
    an::ui::dialogs::applyDialogBackendPreference(dialog, m_core.globalSettingsStore());

    {
        const an::ui::ScopedTopLevelFlagRestorer restorer;
        if (dialog.exec() == QDialog::Rejected)
        {
            return QStringList();
        }
    }

    if (!folderContext.isEmpty() && m_core.folderStore() != nullptr)
    {
        const QDir dir = dialog.directory();
        if (dir != QString::fromUtf8(folder))
        {
            m_core.folderStore()->upsert(folderContext, dir.path());
        }
    }

    return an::ui::dialogs::filterSupportedImageOpenPaths(dialog.selectedFiles());
}

QStringList FileDialogService::selectFiles(QWidget* parent, const QString& title, const QString& folderContext,
                                           const QStringList& fileExtensions)
{
    std::string folder = m_core.lastOpenDirectory();
    if (const QString savedFolder = rememberedFolder(m_core.folderStore(), folderContext); !savedFolder.isEmpty())
    {
        folder = savedFolder.toUtf8().toStdString();
    }

    QFileDialog dialog(dialogParentFor(m_core, parent), title);
    const QString initialFolder = QString::fromUtf8(folder);
    if (!initialFolder.isEmpty() && QDir(initialFolder).exists())
    {
        dialog.setDirectoryUrl(QUrl::fromLocalFile(initialFolder));
    }
    an::ui::dialogs::applyDialogBackendPreference(dialog, m_core.globalSettingsStore());

#ifdef WIN32
    dialog.setFilter(QDir::Executable | QDir::Files);
#endif

    if (!fileExtensions.isEmpty())
    {
        dialog.setNameFilters(fileExtensions);
    }

    {
        const an::ui::ScopedTopLevelFlagRestorer restorer;

        dialog.setWindowFlags(dialog.windowFlags() | Qt::WindowStaysOnTopHint | Qt::Window);

        if (dialog.exec() == QDialog::Rejected)
        {
            return QStringList();
        }
    }

    if (!folderContext.isEmpty() && m_core.folderStore() != nullptr)
    {
        const QDir dir = dialog.directory();
        if (dir != QString::fromUtf8(folder))
        {
            m_core.folderStore()->upsert(folderContext, dir.path());
        }
    }

    return dialog.selectedFiles();
}

QStringList FileDialogService::saveFiles(QWidget* parent, const QString& title, const QString& defaultDir,
                                         const QString& folderContext, const QStringList& fileExtensions)
{
    QFileDialog dialog(dialogParentFor(m_core, parent), title);
    an::ui::dialogs::applyDialogBackendPreference(dialog, m_core.globalSettingsStore());

    const std::string defaultDirText = defaultDir.toUtf8().toStdString();
    std::error_code error;
    if (!defaultDirText.empty() && std::filesystem::is_directory(defaultDirText, error))
    {
        dialog.setDirectory(defaultDir);
    }
    else if (const QString savedFolder = rememberedFolder(m_core.folderStore(), folderContext); !savedFolder.isEmpty())
    {
        dialog.setDirectory(savedFolder);
    }

    dialog.setFilter(QDir::Executable | QDir::Files);

    dialog.setAcceptMode(QFileDialog::AcceptSave);

    if (!fileExtensions.isEmpty())
    {
        dialog.setNameFilters(fileExtensions);
    }

    {
        const an::ui::ScopedTopLevelFlagRestorer restorer;

        if (dialog.exec() == QDialog::Rejected)
        {
            return QStringList();
        }
    }

    if (!folderContext.isEmpty() && m_core.folderStore() != nullptr)
    {
        m_core.folderStore()->upsert(folderContext, dialog.directory().path());
    }

    return dialog.selectedFiles();
}

QString FileDialogService::selectDirectory(QWidget* parent, const QString& title, const QString& dir,
                                           const QFileDialog::Options options)
{
    const an::ui::ScopedTopLevelFlagRestorer restorer;
    return an::ui::dialogs::getExistingDirectory(m_core.globalSettingsStore(), dialogParentFor(m_core, parent), title,
                                                 dir, options);
}

} // namespace an::ui::shell
