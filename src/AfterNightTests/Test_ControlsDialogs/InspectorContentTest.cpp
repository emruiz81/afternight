// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// What the inspector windows show, decided from the image's typed records: the FITS header's cards, the Exif fields
/// with a continued value joined and the fields the window hides left out - and whether there is a field to show at
/// all - and the summary of an image or a sequence, read under a lease that never waits.

#include "Test_ControlsDialogs/DialogsTestSupport.h"

#include "controls/ImageInspectorContent.h"

#include <AfterNightEngine/IO/SequenceDescriptor.h>
#include <AfterNightEngine/Image/AcquisitionMetadata.h>
#include <AfterNightEngine/Image/RawFrame.h>

#include <QDate>
#include <QDateTime>
#include <QFile>
#include <QTime>
#include <QTimeZone>

#include <gtest/gtest.h>

namespace
{

using an::tests::dialogs::card;
using an::tests::dialogs::codecOf;
using an::tests::dialogs::makeCardDocument;
using an::ui::InspectorRow;

TEST(InspectorContentTest, TheFitsHeaderIsEveryFitsCardInTheOrderTheFileStatedIt)
{
    const an::CodecMetadata codec = codecOf({card(an::CodecNamespace::Fits, "SIMPLE", "T", "conforms to FITS"),
                                             card(an::CodecNamespace::Xisf, "XISF:ColorSpace", "Gray"),
                                             card(an::CodecNamespace::Fits, "OBJECT", "'M 31'", "target"),
                                             card(an::CodecNamespace::Tiff, "Exif.Image.Make", "Canon", "Ascii"),
                                             card(an::CodecNamespace::Fits, "HISTORY", "", "")});

    const std::vector<InspectorRow> rows = an::ui::fitsHeaderRows(codec);

    ASSERT_EQ(rows.size(), 3u);
    EXPECT_EQ(rows[0], (InspectorRow{"SIMPLE", "T", "conforms to FITS"}));
    EXPECT_EQ(rows[1], (InspectorRow{"OBJECT", "'M 31'", "target"}));
    EXPECT_EQ(rows[2], (InspectorRow{"HISTORY", "", ""}));
}

TEST(InspectorContentTest, AFileWithNoFitsCardHasNoHeader)
{
    EXPECT_TRUE(an::ui::fitsHeaderRows(an::CodecMetadata{}).empty());
    EXPECT_TRUE(an::ui::fitsHeaderRows(codecOf({card(an::CodecNamespace::Png, "Software", "GIMP")})).empty());
}

TEST(InspectorContentTest, TheExifFieldsAreNamedByTheirTagAndShownWholeInTheOrderTheFileStatedThem)
{
    const an::CodecMetadata codec =
        codecOf({card(an::CodecNamespace::Jpeg, "Exif.Image.Make", "Canon", "Ascii"),
                 card(an::CodecNamespace::Jpeg, "Exif.Image.ImageDescription", "a field longer ", "Ascii"),
                 card(an::CodecNamespace::Jpeg, "Exif.Image.ImageDescription", "than one card", "continued"),
                 card(an::CodecNamespace::Jpeg, "Comment", "a JPEG comment, not an Exif field"),
                 card(an::CodecNamespace::Fits, "Exif.Photo.FNumber", "a FITS card spelled like a field"),
                 card(an::CodecNamespace::CameraRaw, "Exif.Photo.ExposureTime", "1/125", "Rational")});

    const std::vector<InspectorRow> rows = an::ui::exifRows(codec);

    ASSERT_EQ(rows.size(), 3u);
    EXPECT_EQ(rows[0], (InspectorRow{"Make", "Canon", "Exif.Image.Make"}));
    EXPECT_EQ(rows[1],
              (InspectorRow{"ImageDescription", "a field longer than one card", "Exif.Image.ImageDescription"}));
    EXPECT_EQ(rows[2], (InspectorRow{"ExposureTime", "1/125", "Exif.Photo.ExposureTime"}));
}

TEST(InspectorContentTest, TheExifWindowHidesTheStructuralFieldsAndTheThumbnail)
{
    std::vector<an::CodecCard> cards;
    for (const char* hidden :
         {"Exif.Photo.MakerNote", "Exif.Iop.InteroperabilityVersion", "Exif.Iop.InteroperabilityIndex",
          "Exif.Image.StripOffsets", "Exif.Image.NewSubfileType", "Exif.Image.XMLPacket", "Exif.Image.IPTCNAA",
          "Exif.Image.ImageResources", "Exif.Image.InterColorProfile", "Exif.Thumbnail.JPEGInterchangeFormat"})
    {
        EXPECT_TRUE(an::ui::isHiddenExifField(hidden)) << hidden;
        cards.push_back(card(an::CodecNamespace::Tiff, hidden, "hidden"));
    }
    cards.push_back(card(an::CodecNamespace::Tiff, "Exif.Image.Model", "EOS 6D", "Ascii"));

    const std::vector<InspectorRow> rows = an::ui::exifRows(codecOf(std::move(cards)));

    ASSERT_EQ(rows.size(), 1u);
    EXPECT_EQ(rows[0].first, QStringLiteral("Model"));
    EXPECT_FALSE(an::ui::isHiddenExifField("Exif.Image.Model"));
}

// The Exif window has a field to show exactly when it would show a row: an Exif field of a JPEG, TIFF, PNG or camera
// RAW file that it does not hide. A PNG's Exif fields count, and a container's own entries, a JPEG comment, a hidden
// field and a FITS card spelled like a field do not: the view offered the window for any card of a JPEG, a TIFF or a
// camera RAW file and never for a PNG's, so it opened empty or not at all.
TEST(InspectorContentTest, TheExifWindowHasAFieldToShowExactlyWhenItShowsARow)
{
    const an::CodecMetadata pngExif = codecOf({card(an::CodecNamespace::Png, "BitDepth", "16"),
                                               card(an::CodecNamespace::Png, "Exif.Image.Model", "EOS 6D", "Ascii")});
    const an::CodecMetadata pngOwn =
        codecOf({card(an::CodecNamespace::Png, "BitDepth", "16"), card(an::CodecNamespace::Png, "ColorType", "2")});
    const an::CodecMetadata jpegComment =
        codecOf({card(an::CodecNamespace::Jpeg, "Comment", "a JPEG comment, not an Exif field")});
    const an::CodecMetadata hiddenOnly =
        codecOf({card(an::CodecNamespace::Tiff, "Exif.Photo.MakerNote", "hidden"),
                 card(an::CodecNamespace::Tiff, "Exif.Photo.MakerNote", " and continued", "continued"),
                 card(an::CodecNamespace::Tiff, "Exif.Thumbnail.JPEGInterchangeFormat", "hidden")});
    const an::CodecMetadata fitsSpelled =
        codecOf({card(an::CodecNamespace::Fits, "Exif.Photo.FNumber", "a FITS card spelled like a field")});
    const an::CodecMetadata cameraRaw =
        codecOf({card(an::CodecNamespace::CameraRaw, "Exif.Photo.ExposureTime", "1/125", "Rational")});
    const an::CodecMetadata unnamed = codecOf({card(an::CodecNamespace::Jpeg, "Exif.Image.", "no name")});

    EXPECT_TRUE(an::ui::hasExifFields(pngExif)) << "a PNG's Exif field";
    EXPECT_FALSE(an::ui::hasExifFields(pngOwn)) << "a PNG's own entries are not Exif";
    EXPECT_FALSE(an::ui::hasExifFields(jpegComment)) << "a JPEG comment is not Exif";
    EXPECT_FALSE(an::ui::hasExifFields(hiddenOnly)) << "fields the window hides";
    EXPECT_FALSE(an::ui::hasExifFields(fitsSpelled)) << "FITS carries no Exif";
    EXPECT_TRUE(an::ui::hasExifFields(cameraRaw)) << "a camera file's Exif field";
    EXPECT_FALSE(an::ui::hasExifFields(unnamed)) << "a field with no name";
    EXPECT_FALSE(an::ui::hasExifFields(an::CodecMetadata{}));
    for (const an::CodecMetadata* codec :
         {&pngExif, &pngOwn, &jpegComment, &hiddenOnly, &fitsSpelled, &cameraRaw, &unnamed})
    {
        EXPECT_EQ(an::ui::hasExifFields(*codec), !an::ui::exifRows(*codec).empty());
    }
}

TEST(InspectorContentTest, AContinuationOfAnotherFieldIsNotJoinedToIt)
{
    const an::CodecMetadata codec =
        codecOf({card(an::CodecNamespace::Jpeg, "Exif.Image.Artist", "first", "Ascii"),
                 card(an::CodecNamespace::Jpeg, "Exif.Image.Copyright", "second", "continued")});

    const std::vector<InspectorRow> rows = an::ui::exifRows(codec);

    ASSERT_EQ(rows.size(), 2u);
    EXPECT_EQ(rows[0].second, QStringLiteral("first"));
    EXPECT_EQ(rows[1].second, QStringLiteral("second"));
}

TEST(InspectorContentTest, ADocumentIsInspectedUnderALeaseThatNeverWaitsForItsWriter)
{
    auto context = an::tests::dialogs::openDialogsContext();
    ASSERT_NE(context, nullptr);
    std::optional<an::ui::ImageDocument> document = makeCardDocument(
        context->engineSession(), {card(an::CodecNamespace::Fits, "OBJECT", "'M 42'")}, {.displayName = "orion"});
    ASSERT_TRUE(document.has_value());

    const std::optional<an::ui::InspectedImage> inspected = an::ui::inspectDocument(*document);
    ASSERT_TRUE(inspected.has_value());
    EXPECT_EQ(inspected->descriptor.extent().width(), 16u);
    EXPECT_EQ(an::ui::fitsHeaderRows(inspected->metadata.codec()).size(), 1u);

    an::Result<an::ui::ImageDocumentWriter> writer = document->write();
    ASSERT_TRUE(writer.hasValue()) << writer.status().message();
    EXPECT_FALSE(an::ui::inspectDocument(*document).has_value());
    EXPECT_TRUE(an::ui::documentSummaryReport(*document, QStringLiteral("orion")).isEmpty());
    writer->release();
    EXPECT_TRUE(an::ui::inspectDocument(*document).has_value());
}

TEST(InspectorContentTest, AnImageSummaryNamesTheViewTheImageAndItsFile)
{
    auto context = an::tests::dialogs::openDialogsContext();
    ASSERT_NE(context, nullptr);
    an::tests::dialogs::CaseDirectory directory;
    const QString path = directory.path(QStringLiteral("orion.fits"));
    {
        QFile file(path);
        ASSERT_TRUE(file.open(QIODevice::WriteOnly));
        ASSERT_EQ(file.write(QByteArray(5760, 'x')), 5760);
    }
    std::optional<an::ui::ImageDocument> document =
        makeCardDocument(context->engineSession(), {card(an::CodecNamespace::Fits, "OBJECT", "'M 42'")},
                         {.displayName = "orion", .sourcePath = path});
    ASSERT_TRUE(document.has_value());

    const QString report = an::ui::documentSummaryReport(*document, QStringLiteral("Orion view"));

    EXPECT_TRUE(report.contains(QStringLiteral("Orion view"))) << report.toStdString();
    EXPECT_TRUE(report.contains(QStringLiteral("orion.fits"))) << report.toStdString();
    EXPECT_TRUE(report.contains(QStringLiteral("16"))) << report.toStdString();
    EXPECT_TRUE(report.contains(QStringLiteral("12"))) << report.toStdString();
}

TEST(InspectorContentTest, ASequenceSummaryIsTheSequencesAndAVideoFramesIsTheImages)
{
    auto context = an::tests::dialogs::openDialogsContext();
    ASSERT_NE(context, nullptr);
    std::optional<an::ui::ImageDocument> document =
        makeCardDocument(context->engineSession(), {}, {.displayName = "capture"});
    ASSERT_TRUE(document.has_value());

    an::ui::DocumentSequence sequence;
    sequence.sourcePath = QStringLiteral("capture.ser");
    sequence.descriptor.format = an::SequenceFormat::Ser;
    sequence.descriptor.frameCount = 7u;
    document->setSequence(sequence);
    const QString ser = an::ui::documentSummaryReport(*document, QStringLiteral("capture"));
    EXPECT_TRUE(ser.contains(QStringLiteral("SER sequence"))) << ser.toStdString();

    sequence.descriptor.format = an::SequenceFormat::ImageList;
    document->setSequence(sequence);
    const QString list = an::ui::documentSummaryReport(*document, QStringLiteral("capture"));
    EXPECT_TRUE(list.contains(QStringLiteral("Image list sequence"))) << list.toStdString();

    // A video's summary is its frame's.
    sequence.descriptor.format = an::SequenceFormat::Avi;
    document->setSequence(sequence);
    const QString video = an::ui::documentSummaryReport(*document, QStringLiteral("capture"));
    EXPECT_FALSE(video.contains(QStringLiteral("sequence"))) << video.toStdString();
    EXPECT_FALSE(video.isEmpty());
}

// A sequence's summary states what its document recorded when the sequence was opened: when it was recorded, and for
// an image list its first frame's description and the size of every file it names in place of the list file's own.
// The summary read neither, so a list showed '-' for its frames and its date and the list file's own size, and a SER
// '-' for its date.
TEST(InspectorContentTest, ASequenceSummaryStatesWhatItsDocumentRecordedWhenItWasOpened)
{
    auto context = an::tests::dialogs::openDialogsContext();
    ASSERT_NE(context, nullptr);
    std::optional<an::ui::ImageDocument> document =
        makeCardDocument(context->engineSession(), {}, {.displayName = "lights"});
    ASSERT_TRUE(document.has_value());
    const std::optional<an::ui::InspectedImage> inspected = an::ui::inspectDocument(*document);
    ASSERT_TRUE(inspected.has_value());

    an::ui::DocumentSequence sequence;
    sequence.sourcePath = QStringLiteral("lights.anseq");
    sequence.descriptor.format = an::SequenceFormat::ImageList;
    sequence.descriptor.frameCount = 3u;
    const QDateTime recorded(QDate(2026, 1, 15), QTime(3, 4, 5), QTimeZone(QTimeZone::UTC));
    sequence.recorded = an::UtcInstant{recorded.toMSecsSinceEpoch() * 1000000};
    sequence.firstFrame = an::RawFrameDescriptor::fromImageDescriptor(inspected->descriptor);
    ASSERT_TRUE(sequence.firstFrame.has_value());
    sequence.namedFilesBytes = 3u * 1048576u;
    document->setSequence(sequence);
    const QString list = an::ui::documentSummaryReport(*document, QStringLiteral("lights"));
    EXPECT_TRUE(list.contains(QStringLiteral("<strong>Total files size:</strong> 3.00 MB"))) << list.toStdString();
    EXPECT_TRUE(list.contains(QStringLiteral("<strong>Resolution (Width x Height):</strong> 16x12")))
        << list.toStdString();
    EXPECT_TRUE(list.contains(QStringLiteral("<strong>Pixel Format:</strong> 32 bits (float)"))) << list.toStdString();
    EXPECT_TRUE(list.contains(QStringLiteral("<strong>Sequence creation date:</strong> 2026-01-15 03:04:05")))
        << list.toStdString();

    // A SER states its own frame and its file's own size; its date is the one its header stated.
    sequence.sourcePath = QStringLiteral("capture.ser");
    sequence.descriptor.format = an::SequenceFormat::Ser;
    sequence.firstFrame.reset();
    sequence.namedFilesBytes.reset();
    document->setSequence(sequence);
    const QString ser = an::ui::documentSummaryReport(*document, QStringLiteral("capture"));
    EXPECT_TRUE(ser.contains(QStringLiteral("<strong>Sequence creation date:</strong> 2026-01-15 03:04:05")))
        << ser.toStdString();
}

} // namespace
