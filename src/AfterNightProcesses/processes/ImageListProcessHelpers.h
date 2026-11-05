// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// What the windows that work on a list of image files share beyond the list's own rows: the list as the paths a
/// window persists, the check a calibration master passes against the frames it calibrates, the move buttons and table
/// cells the windows keep in step, and adding a folder of images.
///
/// The image calibration, stacking, registration and list analyzer windows hold their files as
/// `ImageListProcessHelpers::ImageListEntry` rows (`controls/ImageListTableHelpers.h`): a path beside the description
/// and metadata probing it stated. A master's description is checked against the frames, and a mismatch is a returned
/// failure the window shows.
#pragma once

#include "app/FileDialogUtils.h"
#include "controls/IAppContext.h"
#include "controls/ImageListTableHelpers.h"
#include "theme/AnDesignTokens.h"

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Image/ImageDescriptor.h>

#include <nlohmann/json.hpp>

#include <QCheckBox>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDirIterator>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QString>
#include <QTableWidget>
#include <QVBoxLayout>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ImageListProcessHelpers
{

/// The paths of a list's files, in the list's order, as the windows persist them; a row with no path is left out.
///
/// @param imageList the list.
/// @return a JSON array of the paths, as UTF-8 strings.
[[nodiscard]] inline nlohmann::json imageFilePathsToJson(const std::vector<ImageListEntry>& imageList)
{
    nlohmann::json paths = nlohmann::json::array();
    for (const ImageListEntry& entry : imageList)
    {
        if (!entry.path.isEmpty())
        {
            paths.push_back(entry.path.toStdString());
        }
    }
    return paths;
}

/// The paths a window persisted, in their order: every string of the array, trimmed, an empty one left out; nothing
/// for a value that is not an array.
///
/// @param value the persisted value.
/// @return the paths.
[[nodiscard]] inline QStringList imageFilePathsFromJson(const nlohmann::json& value)
{
    QStringList paths;
    if (!value.is_array())
    {
        return paths;
    }

    paths.reserve(static_cast<qsizetype>(value.size()));
    for (const auto& item : value)
    {
        if (!item.is_string())
        {
            continue;
        }

        const QString path = QString::fromStdString(item.get<std::string>()).trimmed();
        if (!path.isEmpty())
        {
            paths.push_back(path);
        }
    }
    return paths;
}

/// The file name the list analyzer gives an optimized proxy of a source: its base name and the first sixteen hex digits
/// of its path's SHA-256, so two sources of one name in two folders never share a proxy.
///
/// @param sourcePath the source's path.
/// @return the proxy's file name, a FITS file.
[[nodiscard]] inline QString optimizedProxyFileName(const QString& sourcePath)
{
    const QString source_hash =
        QString::fromLatin1(QCryptographicHash::hash(sourcePath.toUtf8(), QCryptographicHash::Sha256).toHex().left(16));
    return QStringLiteral("%1_%2.fits").arg(QFileInfo(sourcePath).completeBaseName(), source_hash);
}

/// Whether a calibration master fits the frames it calibrates: the same width and height as the first frame of the
/// list, and as many channels.
///
/// @param master what reading the master stated, or nothing when it could not be read.
/// @param imageList the frames, in the list's order.
/// @param calibrationType what the master is - "bias", "dark", "flat" - for the message.
/// @return success; `ErrorCode::NotInitialized` when the master could not be read; `ErrorCode::InvalidArgument` for an
///         empty list; or `ErrorCode::IncompatibleArguments` when its size or its depth is not the frames'.
[[nodiscard]] inline an::Status checkCalibrationMaster(const std::optional<an::ImageDescriptor>& master,
                                                       const std::vector<ImageListEntry>& imageList,
                                                       std::string_view calibrationType)
{
    if (!master.has_value())
    {
        return an::Status::failure(an::ErrorCode::NotInitialized,
                                   "Could NOT load calibration " + std::string(calibrationType) + " frame");
    }

    if (imageList.empty())
    {
        return an::Status::failure(an::ErrorCode::InvalidArgument,
                                   "Cannot validate calibration image: target image list is empty");
    }

    const an::ImageExtent& masterExtent = master->extent();
    const an::ImageExtent& frameExtent = imageList.front().descriptor.extent();
    if (masterExtent.width() != frameExtent.width() || masterExtent.height() != frameExtent.height())
    {
        return an::Status::failure(an::ErrorCode::IncompatibleArguments,
                                   "Calibration " + std::string(calibrationType) +
                                       " file size is different than the target images");
    }

    if (masterExtent.channelCount() != frameExtent.channelCount())
    {
        return an::Status::failure(an::ErrorCode::IncompatibleArguments,
                                   "Calibration " + std::string(calibrationType) +
                                       " file depth is different than the target images");
    }

    return an::Status::success();
}

/**
 * @brief Update move button states based on current selection
 * @param moveUpButton Qt button for moving items up
 * @param moveDownButton Qt button for moving items down
 * @param currentRow Currently selected row index
 * @param totalRows Total number of rows in the list
 *
 * Enables/disables move up/down buttons based on whether the current
 * selection can be moved in that direction.
 */
inline void updateMoveButtonStates(QPushButton* moveUpButton, QPushButton* moveDownButton, int currentRow,
                                   size_t totalRows)
{
    if (currentRow < 0 || totalRows == 0)
    {
        moveUpButton->setEnabled(false);
        moveDownButton->setEnabled(false);
        return;
    }

    moveUpButton->setEnabled(currentRow > 0);
    moveDownButton->setEnabled(static_cast<size_t>(currentRow) < totalRows - 1);
}

/**
 * @brief Set a table item with non-editable text
 * @param table The table widget to update
 * @param row Row index
 * @param col Column index
 * @param text Text to set
 *
 * Creates a QTableWidgetItem with the specified text and makes it non-editable.
 * If the cell already exists, updates its text; otherwise creates a new item.
 */
inline void setTableItemText(QTableWidget* table, int row, int col, const QString& text)
{
    auto item = table->item(row, col);
    if (!item)
    {
        item = new QTableWidgetItem(text);
        item->setFlags(item->flags() & ~Qt::ItemIsEditable);
        table->setItem(row, col, item);
    }
    else
    {
        item->setText(text);
    }
}

/**
 * @brief Result of a folder scan initiated by showAddFolderDialog().
 */
struct AddFolderResult
{
    QStringList files;
    QString typeFilter;
    QString filterName;
    bool accepted = false;
};

/**
 * @brief User-selected folder import options without performing the scan.
 */
struct AddFolderSelection
{
    QString directory;
    bool recursive = true;
    QString typeFilter;
    QString filterName;
    bool accepted = false;
};

/**
 * @brief Collect folder import options from the user without scanning the filesystem.
 * @param parent Parent widget for dialogs
 * @param contextKey Persistence key for remembering the last folder
 * @return AddFolderSelection with accepted=true on success
 */
inline AddFolderSelection showAddFolderOptionsDialog(QWidget* parent, const QString& contextKey)
{
    AddFolderSelection result;

    auto* context = an::ui::appContext();
    auto* pathMemoryService = context ? context->getPathMemoryService() : nullptr;
    auto* globalSettings = context ? context->getGlobalSettings() : nullptr;

    QString initialDir;
    if (pathMemoryService)
    {
        initialDir = pathMemoryService->selectPath(contextKey);
        if (initialDir.isEmpty() || !QDir(initialDir).exists())
        {
            initialDir = QString();
        }
    }

    QString dir = an::ui::dialogs::getExistingDirectory(globalSettings, parent, QObject::tr("Select Folder"),
                                                        initialDir, QFileDialog::ShowDirsOnly);
    if (dir.isEmpty())
        return result;

    if (pathMemoryService)
    {
        pathMemoryService->upsertPath(contextKey, dir);
    }

    QDialog optionsDialog(parent);
    optionsDialog.setWindowTitle(QObject::tr("Add Folder Options"));
    optionsDialog.setMinimumWidth(350);

    auto* layout = new QVBoxLayout(&optionsDialog);
    const auto& t = an::ui::tokens();
    layout->setSpacing(t.spacing.md);
    layout->setContentsMargins(t.spacing.lg, t.spacing.lg, t.spacing.lg, t.spacing.lg);

    auto* recursiveCheck = new QCheckBox(QObject::tr("Include subfolders (recursive)"), &optionsDialog);
    recursiveCheck->setChecked(true);
    layout->addWidget(recursiveCheck);

    auto* formLayout = new QFormLayout();
    formLayout->setSpacing(t.spacing.sm);

    auto* typeCombo = new QComboBox(&optionsDialog);
    typeCombo->addItem(QObject::tr("All"), QString());
    typeCombo->addItem(QObject::tr("Light"), QStringLiteral("LIGHT"));
    typeCombo->addItem(QObject::tr("Dark"), QStringLiteral("DARK"));
    typeCombo->addItem(QObject::tr("Flat"), QStringLiteral("FLAT"));
    typeCombo->addItem(QObject::tr("Bias"), QStringLiteral("BIAS"));
    typeCombo->addItem(QObject::tr("Dark Flat"), QStringLiteral("DARKFLAT"));
    formLayout->addRow(QObject::tr("Frame Type:"), typeCombo);

    auto* filterEdit = new QLineEdit(&optionsDialog);
    filterEdit->setPlaceholderText(QObject::tr("e.g. Ha, L, R, G, B"));
    formLayout->addRow(QObject::tr("Filter Name:"), filterEdit);

    layout->addLayout(formLayout);

    auto* dlgButtons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &optionsDialog);
    QObject::connect(dlgButtons, &QDialogButtonBox::accepted, &optionsDialog, &QDialog::accept);
    QObject::connect(dlgButtons, &QDialogButtonBox::rejected, &optionsDialog, &QDialog::reject);
    layout->addWidget(dlgButtons);

    if (optionsDialog.exec() != QDialog::Accepted)
        return result;

    result.directory = dir;
    result.recursive = recursiveCheck->isChecked();
    result.typeFilter = typeCombo->currentData().toString();
    result.filterName = filterEdit->text().trimmed();
    result.accepted = true;
    return result;
}

/// The image files of a folder the product reads, in the order the folder lists them: a file whose extension is one of
/// the still-image extensions the open dialogs offer, compared without regard to case.
///
/// @param dir the folder.
/// @param recursive whether the files of its subfolders are included.
/// @return the files' paths.
inline QStringList scanFolderForSupportedImages(const QString& dir, const bool recursive)
{
    QStringList files;

    const QDirIterator::IteratorFlags flags = recursive ? QDirIterator::Subdirectories : QDirIterator::NoIteratorFlags;
    QDirIterator it(dir, QDir::Files, flags);

    while (it.hasNext())
    {
        QString path = it.next();
        if (an::ui::dialogs::detail::isSupportedImageSuffix(QFileInfo(path).suffix()))
        {
            files.append(path);
        }
    }

    return files;
}

/**
 * @brief Show the "Add Folder" options dialog (recursive, type filter, filter name),
 *        scan the selected directory, and return matching image file paths.
 * @param parent Parent widget for dialogs
 * @param contextKey Persistence key for remembering the last folder
 * @return AddFolderResult with accepted=true and populated files on success
 */
inline AddFolderResult showAddFolderDialog(QWidget* parent, const QString& contextKey)
{
    AddFolderResult result;

    const AddFolderSelection selection = showAddFolderOptionsDialog(parent, contextKey);
    if (!selection.accepted)
    {
        return result;
    }

    result.files = scanFolderForSupportedImages(selection.directory, selection.recursive);
    result.typeFilter = selection.typeFilter;
    result.filterName = selection.filterName;

    if (result.files.isEmpty())
    {
        QMessageBox::information(parent, QObject::tr("No Files Found"),
                                 QObject::tr("No supported image files found in the selected folder."));
        return result;
    }

    result.accepted = true;
    return result;
}

} // namespace ImageListProcessHelpers
