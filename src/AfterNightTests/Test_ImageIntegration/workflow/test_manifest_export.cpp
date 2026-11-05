// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <gtest/gtest.h>

#include <QJsonDocument>
#include <QJsonObject>
#include <QDir>

#include "processes/ImageIntegration/model/IntegrationDataModel.h"
#include "processes/ImageIntegration/workflow/ManifestBuilder.h"
#include "processes/ImageIntegration/workflow/WorkflowManifest.h"

namespace
{
FrameInfo makeFrame(FrameType type, const QString& filename)
{
    const QString path = QDir::tempPath() + QDir::separator() + filename;
    FrameMetadata metadata(type, QSize(100, 100), 60.0, 0.0, 100, 10, QStringLiteral("L"), QStringLiteral("M42"),
                           QDateTime::currentDateTimeUtc(), QStringLiteral("Default"));

    return FrameInfo(FrameId(path), path, metadata, QStringLiteral("2025-01-01"), {});
}
} // namespace

TEST(ManifestExport, SerializesToReadableJsonWithSummary)
{
    IntegrationDataModel model;
    model.setOutputDirectory(QDir::tempPath());
    model.addFrames(FrameType::Light, {makeFrame(FrameType::Light, QStringLiteral("light.fit"))});

    ManifestBuilder builder(model);
    WorkflowManifest manifest = builder.build();

    QJsonObject json = manifest.toJson();

    // Verify summary exists and is correct
    ASSERT_TRUE(json.contains("summary"));
    QJsonObject summary = json.value("summary").toObject();

    EXPECT_EQ(summary.value("totalLightFrames").toInt(), 1);
    EXPECT_EQ(summary.value("integrationGroups").toInt(), 1);
    EXPECT_EQ(summary.value("registrationGroups").toInt(), 1);
}

TEST(ManifestExport, CanBeReloaded)
{
    IntegrationDataModel model;
    model.setOutputDirectory(QDir::tempPath());
    model.addFrames(FrameType::Light, {makeFrame(FrameType::Light, QStringLiteral("light.fit"))});

    ManifestBuilder builder(model);
    WorkflowManifest manifest = builder.build();

    QJsonObject json = manifest.toJson();
    WorkflowManifest reloaded = WorkflowManifest::fromJson(json);

    // Check key fields
    EXPECT_EQ(reloaded.integrationGroups.size(), 1);
    EXPECT_EQ(reloaded.registrationGroups.size(), 1);
    EXPECT_EQ(reloaded.outputDirectory, manifest.outputDirectory);
}
