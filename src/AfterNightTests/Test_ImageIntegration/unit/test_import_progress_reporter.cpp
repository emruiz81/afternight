// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <gtest/gtest.h>

#include "processes/ImageIntegration/core/ImportProgressReporter.h"

namespace
{
using ImageIntegration::ImportProgressReporter;
}

TEST(ImportProgressReporter, ShowsCurrentFile)
{
    ImportProgressReporter reporter;
    reporter.setTotalFiles(1);
    reporter.onFileStarted(QStringLiteral("/path/to/M42_001.fit"));

    EXPECT_EQ(reporter.currentFile().toStdString(), std::string("M42_001.fit"));
}

TEST(ImportProgressReporter, EstimatesTimeRemaining)
{
    ImportProgressReporter reporter;
    reporter.setTotalFiles(100);
    reporter.onFilesCompleted(50, 30000); // 50 files in 30s

    const auto remaining = reporter.estimatedRemainingMs();
    EXPECT_GT(remaining, 0);
    EXPECT_NEAR(static_cast<double>(remaining), 30000.0, 5000.0);
}

TEST(ImportProgressReporter, ReportsMetadataErrors)
{
    ImportProgressReporter reporter;
    reporter.setTotalFiles(1);
    reporter.onMetadataError(QStringLiteral("/path/file.fit"), QStringLiteral("Corrupt header"));

    EXPECT_EQ(reporter.errors().size(), 1u);
    EXPECT_TRUE(reporter.hasWarnings());
}

TEST(ImportProgressReporter, SupportsPauseResume)
{
    ImportProgressReporter reporter;
    reporter.pause();
    EXPECT_TRUE(reporter.isPaused());

    reporter.resume();
    EXPECT_FALSE(reporter.isPaused());
}
