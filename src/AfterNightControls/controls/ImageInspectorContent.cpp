// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The rows and the report the inspector windows show, from the image's typed records.

#include "stdafx.hpp"
#include "controls/ImageInspectorContent.h"

#include "common.h"
#include "session/ImageDocument.h"

#include <AfterNightConsumerRuntime/ImageAsset.h>

#include <AfterNightEngine/IO/SequenceDescriptor.h>

#include <string>

namespace an::ui
{

namespace
{

/// The comment a card carries when it continues the value of the Exif card before it.
constexpr std::string_view kContinuedComment = "continued";

[[nodiscard]] QString text(const std::string& value)
{
    return QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size()));
}

} // namespace

std::vector<InspectorRow> fitsHeaderRows(const an::CodecMetadata& codec)
{
    std::vector<InspectorRow> rows;
    for (const an::CodecCard& card : codec.cards())
    {
        if (card.origin() == an::CodecNamespace::Fits)
        {
            rows.push_back(InspectorRow{text(card.keyword()), text(card.value()), text(card.comment())});
        }
    }
    return rows;
}

bool isHiddenExifField(const std::string_view keyword) noexcept
{
    // The table is the one the view's action reads too.
    return isHiddenExifKeyword(keyword);
}

std::vector<InspectorRow> exifRows(const an::CodecMetadata& codec)
{
    // A field longer than one card continues in the cards after it, under the same key; the value shown is the whole.
    struct Field
    {
        an::CodecNamespace origin;
        std::string keyword;
        std::string value;
    };
    std::vector<Field> fields;
    for (const an::CodecCard& card : codec.cards())
    {
        if (!isExifFieldCard(card))
        {
            continue;
        }
        if (card.comment() == kContinuedComment && !fields.empty() && fields.back().origin == card.origin() &&
            fields.back().keyword == card.keyword())
        {
            fields.back().value += card.value();
            continue;
        }
        fields.push_back(Field{card.origin(), card.keyword(), card.value()});
    }

    std::vector<InspectorRow> rows;
    rows.reserve(fields.size());
    for (const Field& field : fields)
    {
        if (isHiddenExifField(field.keyword))
        {
            continue;
        }
        // The tag's name is the key's last part: `Exif.Photo.ExposureTime` is the exposure time.
        const std::string name(exifFieldName(field.keyword));
        if (name.empty())
        {
            continue;
        }
        rows.push_back(InspectorRow{text(name), text(field.value), text(field.keyword)});
    }
    return rows;
}

bool hasExifFields(const an::CodecMetadata& codec)
{
    // Decided card by card, as the view decides its action: a field continued across cards keeps its key on each, so a
    // row exists exactly when a card of a shown field does.
    return statesShownExifField(codec);
}

std::optional<InspectedImage> inspectDocument(const ImageDocument& document)
{
    // Never waits: the GUI thread reads what no writer holds, and a window shows the rest at its next update.
    an::Result<an::consumer::AssetReadLease> lease = document.tryRead();
    if (!lease.hasValue())
    {
        return std::nullopt;
    }
    return lease->visit([](const auto& typed) -> std::optional<InspectedImage> {
        const std::optional<an::ImageDescriptor> described = typed.descriptor();
        if (!described.has_value())
        {
            return std::nullopt;
        }
        return InspectedImage{*described, typed.metadata()};
    });
}

QString documentSummaryReport(const ImageDocument& document, const QString& title)
{
    const std::string heading = title.toUtf8().toStdString();
    if (const std::optional<DocumentSequence> sequence = document.sequence(); sequence.has_value())
    {
        // The facts the document recorded when the sequence was opened; the file's times are the file system's now.
        const std::string report =
            getSequenceFullReport(sequence->descriptor, reportFileFacts(sequence->sourcePath.toUtf8().toStdString()),
                                  heading, sequence->recorded, sequence->firstFrame, sequence->namedFilesBytes);
        if (!report.empty())
        {
            return QString::fromUtf8(report);
        }
    }

    const std::optional<InspectedImage> inspected = inspectDocument(document);
    if (!inspected.has_value())
    {
        return {};
    }
    const ReportFileFacts file = reportFileFacts(document.names().sourcePath.toUtf8().toStdString());
    return QString::fromUtf8(getImageFullReport(inspected->descriptor, inspected->metadata, file, heading));
}

} // namespace an::ui
