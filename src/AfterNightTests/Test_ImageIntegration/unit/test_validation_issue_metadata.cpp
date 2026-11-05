// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <gtest/gtest.h>

#include "processes/ImageIntegration/model/ValidationResult.h"

TEST(ValidationIssueMetadata, HasErrorCode)
{
    const auto issue = ValidationIssue::missingOutputDirectory();
    EXPECT_EQ(issue.code, ValidationErrorCode::MissingOutputDirectory);
}

TEST(ValidationIssueMetadata, IncludesFilePathContext)
{
    const auto issue = ValidationIssue::missingCalibration(QStringLiteral("light_001.fit"), CalibrationFrameType::Dark);
    EXPECT_FALSE(issue.filePath.isEmpty());
}

TEST(ValidationIssueMetadata, HasAccessibleSeverityIcon)
{
    const auto issue = ValidationIssue::missingOutputDirectory();
    EXPECT_FALSE(issue.severityIcon().isEmpty());
}

TEST(ValidationIssueMetadata, GroupsRelatedIssues)
{
    WorkflowValidationResult result;
    result.issues.push_back(ValidationIssue::missingOutputDirectory());
    result.issues.push_back(
        ValidationIssue::missingCalibration(QStringLiteral("light_001.fit"), CalibrationFrameType::Dark));
    result.issues.push_back(
        ValidationIssue::missingCalibration(QStringLiteral("light_002.fit"), CalibrationFrameType::Flat));

    const auto grouped = result.groupedIssues();
    EXPECT_TRUE(grouped.contains(QStringLiteral("Output")));
    EXPECT_TRUE(grouped.contains(QStringLiteral("Calibration")));
    EXPECT_EQ(grouped.at(QStringLiteral("Calibration")).size(), 2u);
}
