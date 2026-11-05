// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <gtest/gtest.h>

#include <QDateTime>
#include <QDir>
#include <QTimeZone>

#include <optional>

#include "processes/ImageIntegration/model/IntegrationDataModel.h"

namespace
{
FrameInfo makeFrame(FrameType type, const QString& filename, const QSize& size, double exposure, const QString& session)
{
    const QString path = QDir::tempPath() + QDir::separator() + filename;
    FrameMetadata metadata(type, size, exposure, std::nullopt, 100, 10, QStringLiteral("L"), QStringLiteral("M42"),
                           QDateTime::fromSecsSinceEpoch(1700000000, QTimeZone::UTC), QStringLiteral("Default"));

    return FrameInfo(FrameId(path), path, metadata, session, QString());
}

bool hasMissingType(const WorkflowValidationResult& result, CalibrationFrameType type)
{
    for (const auto& issue : result.issues)
    {
        if (issue.code != ValidationErrorCode::MissingCalibrationMatch)
        {
            continue;
        }
        if (issue.missingCalibrationType.has_value() && issue.missingCalibrationType.value() == type)
        {
            return true;
        }
    }
    return false;
}

bool hasFallbackType(const WorkflowValidationResult& result, CalibrationFrameType type)
{
    for (const auto& issue : result.issues)
    {
        if (issue.code != ValidationErrorCode::FallbackCalibrationMatch)
        {
            continue;
        }
        if (issue.missingCalibrationType.has_value() && issue.missingCalibrationType.value() == type)
        {
            return true;
        }
    }
    return false;
}
} // namespace

TEST(CalibrationRequirementsValidation, FlatOnlyWarnsMissingDarkOrBias)
{
    IntegrationDataModel model;
    model.setOutputDirectory(QDir::tempPath());

    model.addFrames(FrameType::Light, {makeFrame(FrameType::Light, QStringLiteral("light.fit"), QSize(100, 100), 10.0,
                                                 QStringLiteral("2025-01-01"))});
    model.addFrames(FrameType::Flat, {makeFrame(FrameType::Flat, QStringLiteral("flat.fit"), QSize(100, 100), 1.0,
                                                QStringLiteral("2025-01-01"))});

    const auto validation = model.validate();

    EXPECT_FALSE(validation.hasErrors());
    EXPECT_TRUE(hasMissingType(validation, CalibrationFrameType::DarkOrBias));
    EXPECT_FALSE(hasMissingType(validation, CalibrationFrameType::Flat));
}

TEST(CalibrationRequirementsValidation, BiasOnlyWarnsMissingFlat)
{
    IntegrationDataModel model;
    model.setOutputDirectory(QDir::tempPath());

    model.addFrames(FrameType::Light, {makeFrame(FrameType::Light, QStringLiteral("light.fit"), QSize(100, 100), 10.0,
                                                 QStringLiteral("2025-01-01"))});
    model.addFrames(FrameType::Bias, {makeFrame(FrameType::Bias, QStringLiteral("bias.fit"), QSize(100, 100), 0.0,
                                                QStringLiteral("2025-01-01"))});

    const auto validation = model.validate();

    EXPECT_FALSE(validation.hasErrors());
    EXPECT_TRUE(hasMissingType(validation, CalibrationFrameType::Flat));
    EXPECT_FALSE(hasMissingType(validation, CalibrationFrameType::DarkOrBias));
}

TEST(CalibrationRequirementsValidation, FlatAndBiasHasNoMissingWarnings)
{
    IntegrationDataModel model;
    model.setOutputDirectory(QDir::tempPath());

    model.addFrames(FrameType::Light, {makeFrame(FrameType::Light, QStringLiteral("light.fit"), QSize(100, 100), 10.0,
                                                 QStringLiteral("2025-01-01"))});
    model.addFrames(FrameType::Flat, {makeFrame(FrameType::Flat, QStringLiteral("flat.fit"), QSize(100, 100), 1.0,
                                                QStringLiteral("2025-01-01"))});
    model.addFrames(FrameType::Bias, {makeFrame(FrameType::Bias, QStringLiteral("bias.fit"), QSize(100, 100), 0.0,
                                                QStringLiteral("2025-01-01"))});

    const auto validation = model.validate();

    EXPECT_FALSE(validation.hasErrors());
    EXPECT_FALSE(hasMissingType(validation, CalibrationFrameType::Flat));
    EXPECT_FALSE(hasMissingType(validation, CalibrationFrameType::DarkOrBias));
}

TEST(CalibrationRequirementsValidation, ManualOverrideSuppressesFallbackWarning)
{
    IntegrationDataModel model;
    model.setOutputDirectory(QDir::tempPath());

    const FrameInfo light =
        makeFrame(FrameType::Light, QStringLiteral("light.fit"), QSize(100, 100), 10.0, QStringLiteral("2025-01-02"));
    model.addFrames(FrameType::Light, {light});

    model.addFrames(FrameType::Bias, {makeFrame(FrameType::Bias, QStringLiteral("bias.fit"), QSize(100, 100), 0.0,
                                                QStringLiteral("2025-01-02"))});

    // Flat exists but is from a different session, so the matcher should fall back (ClosestDate).
    model.addFrames(FrameType::Flat, {makeFrame(FrameType::Flat, QStringLiteral("flat.fit"), QSize(100, 100), 1.0,
                                                QStringLiteral("2025-01-01"))});

    const auto before = model.validate();
    EXPECT_FALSE(before.hasErrors());
    EXPECT_TRUE(hasFallbackType(before, CalibrationFrameType::Flat));

    model.setCalibrationOverride(LightGroupKey::fromFrame(light), CalibrationOverrideType::Flat,
                                 QDir::tempPath() + QDir::separator() + QStringLiteral("manual_flat_master.fit"));

    const auto after = model.validate();
    EXPECT_FALSE(after.hasErrors());
    EXPECT_FALSE(hasFallbackType(after, CalibrationFrameType::Flat));
    EXPECT_FALSE(hasMissingType(after, CalibrationFrameType::Flat));
    EXPECT_FALSE(hasMissingType(after, CalibrationFrameType::DarkOrBias));
}
