// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <gtest/gtest.h>

#include "controls/ImageHistoryLineage.h"

#include <QDateTime>

// ---------------------------------------------------------------------------
// ImageHistoryLineage basic operations
// ---------------------------------------------------------------------------

TEST(ImageHistoryLineage, DefaultStateIsEmpty)
{
    ImageHistoryLineage lineage;
    EXPECT_TRUE(lineage.empty());
    EXPECT_TRUE(lineage.entries().empty());
}

TEST(ImageHistoryLineage, AppendEntryCausesNonEmpty)
{
    ImageHistoryLineage lineage;

    LineageEntry entry;
    entry.step_name = "Open File";
    entry.description = "Opened image.tif";
    entry.image_name = "M42";
    entry.timestamp = QDateTime::fromString("2026-04-01 12:00:00", "yyyy-MM-dd hh:mm:ss");

    lineage.appendEntry(entry);

    EXPECT_FALSE(lineage.empty());
    ASSERT_EQ(lineage.entries().size(), 1U);
    EXPECT_EQ(lineage.entries()[0].step_name, "Open File");
    EXPECT_EQ(lineage.entries()[0].image_name, "M42");
}

TEST(ImageHistoryLineage, AppendFromCopiesEntries)
{
    ImageHistoryLineage source;
    LineageEntry e1;
    e1.step_name = "Step A";
    e1.timestamp = QDateTime::currentDateTime();
    source.appendEntry(e1);

    LineageEntry e2;
    e2.step_name = "Step B";
    e2.timestamp = QDateTime::currentDateTime();
    source.appendEntry(e2);

    ImageHistoryLineage dest;
    LineageEntry e0;
    e0.step_name = "Step 0";
    e0.timestamp = QDateTime::currentDateTime();
    dest.appendEntry(e0);

    dest.appendFrom(source);

    ASSERT_EQ(dest.entries().size(), 3U);
    EXPECT_EQ(dest.entries()[0].step_name, "Step 0");
    EXPECT_EQ(dest.entries()[1].step_name, "Step A");
    EXPECT_EQ(dest.entries()[2].step_name, "Step B");
}

TEST(ImageHistoryLineage, FormatAsTextIncludesAllEntries)
{
    ImageHistoryLineage lineage;

    LineageEntry e1;
    e1.step_name = "Open File";
    e1.description = "Opened from disk";
    e1.image_name = "M42";
    e1.timestamp = QDateTime::fromString("2026-04-01 12:00:00", "yyyy-MM-dd hh:mm:ss");
    e1.source_file = "/data/M42.tif";
    lineage.appendEntry(e1);

    LineageEntry e2;
    e2.step_name = "Background Extraction";
    e2.description = "Background extraction applied";
    e2.image_name = "M42";
    e2.timestamp = QDateTime::fromString("2026-04-01 12:05:00", "yyyy-MM-dd hh:mm:ss");
    lineage.appendEntry(e2);

    const std::string text = lineage.formatAsText();

    EXPECT_NE(text.find("[1]"), std::string::npos);
    EXPECT_NE(text.find("Open File"), std::string::npos);
    EXPECT_NE(text.find("M42"), std::string::npos);
    EXPECT_NE(text.find("/data/M42.tif"), std::string::npos);
    EXPECT_NE(text.find("[2]"), std::string::npos);
    EXPECT_NE(text.find("Background Extraction"), std::string::npos);
    EXPECT_NE(text.find("Background extraction applied"), std::string::npos);
}

TEST(ImageHistoryLineage, FormatAsTextHandlesEmptyLineage)
{
    ImageHistoryLineage lineage;
    const std::string text = lineage.formatAsText();
    EXPECT_TRUE(text.empty());
}

TEST(ImageHistoryLineage, FormatAsTextOmitsEmptyOptionalFields)
{
    ImageHistoryLineage lineage;

    LineageEntry entry;
    entry.step_name = "Crop";
    entry.timestamp = QDateTime::fromString("2026-04-01 12:10:00", "yyyy-MM-dd hh:mm:ss");
    lineage.appendEntry(entry);

    const std::string text = lineage.formatAsText();

    EXPECT_NE(text.find("Crop"), std::string::npos);
    EXPECT_EQ(text.find("Source file:"), std::string::npos);
}

// ---------------------------------------------------------------------------
// ImageHistoryLineage on ImageHistoryTracker
// ---------------------------------------------------------------------------

TEST(ImageHistoryTrackerLineage, DefaultLineageIsEmpty)
{
    // ImageHistoryTracker requires a parent control, but lineage() / setLineage()
    // don't touch the parent. We can test the data accessors via the header types.
    ImageHistoryLineage lineage;
    EXPECT_TRUE(lineage.empty());

    LineageEntry entry;
    entry.step_name = "Test";
    entry.timestamp = QDateTime::currentDateTime();
    lineage.appendEntry(entry);

    ImageHistoryLineage copy;
    copy.appendFrom(lineage);

    EXPECT_FALSE(copy.empty());
    EXPECT_EQ(copy.entries().size(), 1U);
}

// ---------------------------------------------------------------------------
// formatHistoryLog (basic smoke test without a full tracker)
// Note: Full integration test would require mocking ImageViewControl.
// We verify the lineage portion by checking formatAsText output.
// ---------------------------------------------------------------------------

TEST(FormatHistoryLog, LineageFormatContainsExpectedStructure)
{
    ImageHistoryLineage lineage;

    LineageEntry e1;
    e1.step_name = "Initial state";
    e1.description = "Loaded from /data/M42.fits";
    e1.image_name = "M42";
    e1.timestamp = QDateTime::fromString("2026-04-01 10:00:00", "yyyy-MM-dd hh:mm:ss");
    e1.source_file = "/data/M42.fits";
    lineage.appendEntry(e1);

    LineageEntry e2;
    e2.step_name = "Background Extraction";
    e2.description = "";
    e2.image_name = "M42";
    e2.timestamp = QDateTime::fromString("2026-04-01 10:05:00", "yyyy-MM-dd hh:mm:ss");
    lineage.appendEntry(e2);

    LineageEntry e3;
    e3.step_name = "Extract Red Channel";
    e3.description = "Extracted from 'M42'";
    e3.image_name = "M42 - RED";
    e3.timestamp = QDateTime::fromString("2026-04-01 10:10:00", "yyyy-MM-dd hh:mm:ss");
    lineage.appendEntry(e3);

    const std::string text = lineage.formatAsText();

    // Should have 3 numbered entries
    EXPECT_NE(text.find("[1]"), std::string::npos);
    EXPECT_NE(text.find("[2]"), std::string::npos);
    EXPECT_NE(text.find("[3]"), std::string::npos);
    EXPECT_NE(text.find("Initial state"), std::string::npos);
    EXPECT_NE(text.find("Background Extraction"), std::string::npos);
    EXPECT_NE(text.find("Extract Red Channel"), std::string::npos);
    EXPECT_NE(text.find("Source file: /data/M42.fits"), std::string::npos);

    // Should have image group headers
    EXPECT_NE(text.find("Inherited from \"M42\""), std::string::npos);
    EXPECT_NE(text.find("Inherited from \"M42 - RED\""), std::string::npos);
}

TEST(FormatHistoryLog, ChainedLineageGroupsByImageName)
{
    // Simulates: Image1 -> clone -> Image2 -> clone -> Image3
    // Image3's lineage should have entries from Image1 and Image2 grouped by name
    ImageHistoryLineage lineage;

    // Entries from Image1
    LineageEntry e1;
    e1.step_name = "Open File";
    e1.image_name = "Image1";
    e1.timestamp = QDateTime::fromString("2026-04-01 10:00:00", "yyyy-MM-dd hh:mm:ss");
    lineage.appendEntry(e1);

    LineageEntry e2;
    e2.step_name = "Process A";
    e2.image_name = "Image1";
    e2.timestamp = QDateTime::fromString("2026-04-01 10:01:00", "yyyy-MM-dd hh:mm:ss");
    lineage.appendEntry(e2);

    // Entries from Image2
    LineageEntry e3;
    e3.step_name = "Cloned from 'Image1'";
    e3.image_name = "Image2";
    e3.timestamp = QDateTime::fromString("2026-04-01 10:02:00", "yyyy-MM-dd hh:mm:ss");
    lineage.appendEntry(e3);

    LineageEntry e4;
    e4.step_name = "Process B";
    e4.image_name = "Image2";
    e4.timestamp = QDateTime::fromString("2026-04-01 10:03:00", "yyyy-MM-dd hh:mm:ss");
    lineage.appendEntry(e4);

    const std::string text = lineage.formatAsText();

    // Should have group headers for both images
    EXPECT_NE(text.find("Inherited from \"Image1\""), std::string::npos);
    EXPECT_NE(text.find("Inherited from \"Image2\""), std::string::npos);

    // Image1 header should appear before Image2 header
    const auto pos1 = text.find("Inherited from \"Image1\"");
    const auto pos2 = text.find("Inherited from \"Image2\"");
    EXPECT_LT(pos1, pos2);

    // All entries present
    EXPECT_NE(text.find("Open File"), std::string::npos);
    EXPECT_NE(text.find("Process A"), std::string::npos);
    EXPECT_NE(text.find("Cloned from 'Image1'"), std::string::npos);
    EXPECT_NE(text.find("Process B"), std::string::npos);
}
