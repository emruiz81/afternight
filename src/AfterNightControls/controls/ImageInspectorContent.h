// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// What the three inspector windows over a view show, decided from the image's typed records: the FITS header cards a
/// file stated, the Exif fields a camera or a raster file stated, and the summary of the image or the sequence and the
/// file behind it.
///
/// The windows read the document on the GUI thread under a lease that never waits: what a writer holds is shown at its
/// next update instead. Everything here is a pure function of what that lease read, so the rows a window shows are
/// tested without a window.
#pragma once

#include "AfterNightControlsExport.h"

#include <AfterNightEngine/Image/CodecMetadata.h>
#include <AfterNightEngine/Image/ImageDescriptor.h>
#include <AfterNightEngine/Image/ImageMetadata.h>

#include <QString>

#include <optional>
#include <string_view>
#include <vector>

namespace an::ui
{

class ImageDocument;

/// One row of an inspector's table, in the window's column order.
struct InspectorRow
{
    /// The first column: a keyword or a tag.
    QString first;

    /// The second column: the value.
    QString second;

    /// The third column: a comment or the field's full key.
    QString third;

    /// Field-wise equality.
    [[nodiscard]] friend bool operator==(const InspectorRow& left, const InspectorRow& right) = default;
};

/// The FITS header's rows: every card of the FITS namespace - read from a FITS file, or the FITS keywords an XISF file
/// carries - as keyword, value and comment, in the order the file stated them. Cards of any other namespace are not a
/// FITS header and are not shown.
///
/// @param codec what the file said.
/// @return the rows; none for a file that stated no FITS card.
[[nodiscard]] AFTERNIGHTCONTROLS_API std::vector<InspectorRow> fitsHeaderRows(const an::CodecMetadata& codec);

/// Whether an Exif field is left out of the Exif window: the maker note, the interoperability fields, the strip
/// offsets, the subfile type, the embedded XMP, IPTC and Photoshop blocks, the colour profile and the thumbnail, which
/// the engine keeps as bytes.
///
/// @param keyword the field's full key, as `Exif.Photo.MakerNote`.
/// @return true when the window does not show it.
[[nodiscard]] AFTERNIGHTCONTROLS_API bool isHiddenExifField(std::string_view keyword) noexcept;

/// The Exif window's rows: every Exif field a JPEG, TIFF, PNG or camera RAW file stated, as the tag's name, its value
/// and its full key, in the order the file stated them, a value continued across cards joined into one. A field the
/// window hides, a field with no name and a card that is not an Exif field are left out.
///
/// @param codec what the file said.
/// @return the rows; none for a file that stated no Exif.
[[nodiscard]] AFTERNIGHTCONTROLS_API std::vector<InspectorRow> exifRows(const an::CodecMetadata& codec);

/// Whether the Exif window has anything to show: at least one row `exifRows` would make - an Exif field of a JPEG,
/// TIFF, PNG or camera RAW file that the window does not hide. A container's own entries - a PNG's bit depth, a JPEG
/// comment - are not Exif and do not count. A view offers the window by the same rule.
///
/// @param codec what the file said.
/// @return true when the Exif window shows a row.
[[nodiscard]] AFTERNIGHTCONTROLS_API bool hasExifFields(const an::CodecMetadata& codec);

/// What an inspector read of a document's image: its description and its metadata, copied under the lease.
struct InspectedImage
{
    /// What the image is.
    an::ImageDescriptor descriptor;

    /// What has been said and done to it.
    an::ImageMetadata metadata;
};

/// Reads a document's image description and metadata under a lease that never waits.
///
/// Called on the GUI thread.
///
/// @param document the view's document.
/// @return what was read, or nothing when the document holds no image or a writer holds it.
[[nodiscard]] AFTERNIGHTCONTROLS_API std::optional<InspectedImage> inspectDocument(const ImageDocument& document);

/// The summary report of a document: the sequence it shows one frame of and the file the frames are read from, or the
/// image and the file it was read from, in the report's HTML. A SER's or an image list's summary states what the
/// document recorded when the sequence was opened - when it was recorded, and for an image list its first frame and the
/// size of every file it names in place of the list file's own; a video's is its frame's.
///
/// Called on the GUI thread.
///
/// @param document the view's document.
/// @param title the view's name, the report's heading.
/// @return the report; empty when the document holds no image or a writer holds it.
[[nodiscard]] AFTERNIGHTCONTROLS_API QString documentSummaryReport(const ImageDocument& document, const QString& title);

} // namespace an::ui
