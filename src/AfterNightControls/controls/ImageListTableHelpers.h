// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// What the windows that list image files share: the record one row of the list holds, the checks a list must pass
/// before its frames are made into a sequence or stacked, and keeping a table and its records in step.
///
/// A row holds what probing the file stated - its description and its metadata, read without decoding a pixel.
/// The ISO speed, the gain setting and the exposure are read here through the engine's projection of the file's cards,
/// with a camera RAW file's own record behind them.
#pragma once

#include <AfterNightEngine/IO/CodecMetadataProjection.h>
#include <AfterNightEngine/Image/ImageDescriptor.h>
#include <AfterNightEngine/Image/ImageMetadata.h>
#include <AfterNightEngine/Image/RawCameraMetadata.h>

#include <QString>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVariant>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace ImageListProcessHelpers
{

/// One file of a list: its path and what probing it stated.
struct ImageListEntry
{
    /// The file.
    QString path;

    /// What the file holds: its extent, representation, channels and filter array.
    an::ImageDescriptor descriptor;

    /// What the file states about the frame: its acquisition record and its cards.
    an::ImageMetadata metadata;
};

/// The ISO speed or gain setting a file states: whether it is an ISO speed, and the value; nothing when it states
/// neither. A file's ISO speed card comes first, then a camera RAW file's own ISO speed, then its gain card.
[[nodiscard]] inline std::optional<std::pair<bool, std::int64_t>> isoSpeedOrGainOf(const an::ImageMetadata& metadata)
{
    const an::AcquisitionProjection projected = an::projectAcquisition(metadata.codec());
    if (projected.isoSpeed.has_value())
    {
        return std::make_pair(true, *projected.isoSpeed);
    }
    const std::optional<an::RawCameraMetadata>& camera = metadata.acquisition().rawCamera();
    if (camera.has_value() && camera->isoSpeed > 0.0)
    {
        return std::make_pair(true, static_cast<std::int64_t>(std::llround(camera->isoSpeed)));
    }
    if (projected.gainSetting.has_value())
    {
        return std::make_pair(false, *projected.gainSetting);
    }
    return std::nullopt;
}

/// The exposure a file states, in seconds: its acquisition record's, then its cards'; nothing when it states none.
[[nodiscard]] inline std::optional<double> exposureSecondsOf(const an::ImageMetadata& metadata)
{
    if (const std::optional<double> recorded = metadata.acquisition().exposureSeconds(); recorded.has_value())
    {
        return recorded;
    }
    return an::projectAcquisition(metadata.codec()).acquisition.exposureSeconds;
}

/// The camera offset setting a file states, from its cards.
[[nodiscard]] inline std::optional<std::int64_t> cameraOffsetOf(const an::ImageMetadata& metadata)
{
    return an::projectAcquisition(metadata.codec()).cameraOffset;
}

/**
 * @brief Find the row index of a file in the table widget
 * @param tableWidget The table widget to search
 * @param fileName The filename to find
 * @return Row index if found, -1 otherwise
 */
inline int findItemInTable(QTableWidget* tableWidget, const QString& fileName)
{
    for (int i = 0; i < tableWidget->rowCount(); ++i)
    {
        auto item = tableWidget->item(i, 0);
        if (!item)
        {
            continue;
        }

        const QVariant storedValue = item->data(Qt::UserRole);
        const QString candidate = storedValue.isValid() ? storedValue.toString() : item->text();
        if (fileName == candidate)
        {
            return i;
        }
    }
    return -1;
}

/**
 * @brief Validate that all images in the list have the same dimensions
 * @param imageList The files to validate
 * @return true if all images have matching width and height, false otherwise
 */
inline bool validateImageListSizes(const std::vector<ImageListEntry>& imageList)
{
    if (imageList.empty())
    {
        return true;
    }

    const an::ImageExtent& first = imageList.front().descriptor.extent();
    return std::all_of(imageList.begin(), imageList.end(), [&first](const ImageListEntry& entry) {
        const an::ImageExtent& extent = entry.descriptor.extent();
        return extent.width() == first.width() && extent.height() == first.height();
    });
}

/**
 * @brief Validate that all images have the same representation and channel count
 * @param imageList The files to validate
 * @return true if all images have matching sample type and channels, false otherwise
 */
inline bool validateImageListColorDepth(const std::vector<ImageListEntry>& imageList)
{
    if (imageList.empty())
    {
        return true;
    }

    const an::ImageDescriptor& first = imageList.front().descriptor;
    return std::all_of(imageList.begin(), imageList.end(), [&first](const ImageListEntry& entry) {
        return entry.descriptor.sampleType() == first.sampleType() &&
               entry.descriptor.extent().channelCount() == first.extent().channelCount();
    });
}

/**
 * @brief Validate that all images were taken at the same ISO, or the same gain
 * @param imageList The files to validate
 * @return false when two images state different ISO speeds or gains, or one states an ISO speed and another a gain;
 *         an image whose metadata states neither is not compared
 */
inline bool validateImageListISO(const std::vector<ImageListEntry>& imageList)
{
    std::optional<std::pair<bool, std::int64_t>> first;
    for (const ImageListEntry& entry : imageList)
    {
        const std::optional<std::pair<bool, std::int64_t>> stated = isoSpeedOrGainOf(entry.metadata);
        if (!stated.has_value())
        {
            continue;
        }
        if (!first.has_value())
        {
            first = stated;
        }
        else if (*first != *stated)
        {
            return false;
        }
    }
    return true;
}

/**
 * @brief Validate that all images have the same exposure time
 * @param imageList The files to validate
 * @return false when two images state exposures that differ by more than a millionth of the longer one; an image
 *         whose metadata states no exposure is not compared
 */
inline bool validateImageListExposure(const std::vector<ImageListEntry>& imageList)
{
    std::optional<double> first;
    for (const ImageListEntry& entry : imageList)
    {
        const std::optional<double> exposure = exposureSecondsOf(entry.metadata);
        if (!exposure.has_value() || !(*exposure > 0.0))
        {
            continue;
        }
        if (!first.has_value())
        {
            first = exposure;
        }
        else if (std::abs(*first - *exposure) > 1e-6 * (std::max)(*first, *exposure))
        {
            return false;
        }
    }
    return true;
}

/**
 * @brief Clear image list data structures
 * @param tableWidget The table widget to clear
 * @param imageList The files to clear
 * @param imagePathsSet Set of image paths to clear
 * @param ignoreRowSelectedEvent Flag to temporarily ignore row selection events
 */
inline void clearImageListData(QTableWidget* tableWidget, std::vector<ImageListEntry>& imageList,
                               std::unordered_set<std::string>& imagePathsSet, bool& ignoreRowSelectedEvent)
{
    ignoreRowSelectedEvent = true;
    tableWidget->clearContents();
    tableWidget->setRowCount(0);
    imageList.clear();
    imagePathsSet.clear();
    ignoreRowSelectedEvent = false;
}

/**
 * @brief Remove items from the image list and table
 * @param items List of filenames to remove
 * @param tableWidget The table widget to update
 * @param imageList The files
 * @param imagePathsSet Set of image paths for deduplication
 */
inline void removeItemsFromImageList(const std::vector<QString>& items, QTableWidget* tableWidget,
                                     std::vector<ImageListEntry>& imageList,
                                     std::unordered_set<std::string>& imagePathsSet)
{
    for (const auto& item : items)
    {
        const int idx = findItemInTable(tableWidget, item);
        if (idx != -1)
        {
            tableWidget->removeRow(idx);
        }

        const auto it = std::find_if(imageList.begin(), imageList.end(),
                                     [&item](const ImageListEntry& entry) { return entry.path == item; });
        if (it != imageList.end())
        {
            imageList.erase(it);
        }

        imagePathsSet.erase(item.toUtf8().toStdString());
    }
}

} // namespace ImageListProcessHelpers
