// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <gtest/gtest.h>

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTimeZone>

#include "Test_ImageIntegration/ImportSession.h"

#include "processes/ImageIntegration/core/FileImport.h"

#include <AfterNightConsumerRuntime/EngineSession.h>

#include <optional>

namespace
{
FrameInfo makeImportedFrame(const QString& path)
{
    FrameMetadata metadata(FrameType::Light, QSize(10, 10), 30.0, std::nullopt, 100, 10, QStringLiteral("L"),
                           QStringLiteral("M42"), QDateTime::fromSecsSinceEpoch(1700000000, QTimeZone::UTC),
                           QStringLiteral("Default"));
    return FrameInfo(FrameId(path), path, metadata);
}

/// The session every case of this file reads in.
class FileImport : public ::testing::Test
{
protected:
    void SetUp() override
    {
        an::Result<an::consumer::EngineSession> opened = an::tests::integration::openImportSession();
        ASSERT_TRUE(opened.hasValue()) << opened.status().message();
        m_session.emplace(std::move(*opened));
    }

    [[nodiscard]] const an::consumer::EngineSession& importSession() const
    {
        return *m_session;
    }

    std::optional<an::consumer::EngineSession> m_session;
};
} // namespace

TEST_F(FileImport, EmptyPathReturnsError)
{
    const auto res = ImageIntegration::buildDetectedFrameInfoWithIssue(importSession(), QString(), FrameType::Light);
    EXPECT_FALSE(res.detected.has_value());
    ASSERT_TRUE(res.issue.has_value());
    EXPECT_EQ(res.issue->severity, ImageIntegration::ImportIssueSeverity::Error);
}

TEST_F(FileImport, MissingFileReturnsError)
{
    const QString path = QDir::tempPath() + QDir::separator() + QStringLiteral("__does_not_exist__12345.fit");
    const auto res = ImageIntegration::buildDetectedFrameInfoWithIssue(importSession(), path, FrameType::Light);
    EXPECT_FALSE(res.detected.has_value());
    ASSERT_TRUE(res.issue.has_value());
    EXPECT_EQ(res.issue->severity, ImageIntegration::ImportIssueSeverity::Error);
}

TEST_F(FileImport, CorruptFitsDoesNotCrash)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());

    const QString path = dir.filePath(QStringLiteral("corrupt.fit"));
    QFile f(path);
    ASSERT_TRUE(f.open(QIODevice::WriteOnly));
    f.write("NOT_A_FITS_FILE");
    f.close();

    const auto res = ImageIntegration::buildDetectedFrameInfoWithIssue(importSession(), path, FrameType::Light);
    ASSERT_TRUE(res.detected.has_value());
    if (res.issue.has_value())
    {
        EXPECT_EQ(res.issue->severity, ImageIntegration::ImportIssueSeverity::Warning);
    }
}

TEST_F(FileImport, SortDetectedFramesForImportUsesCanonicalPath)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());

    const QString firstPath = dir.filePath(QStringLiteral("a_light.fit"));
    const QString secondPath = dir.filePath(QStringLiteral("b_light.fit"));
    for (const QString& path : {firstPath, secondPath})
    {
        QFile file(path);
        ASSERT_TRUE(file.open(QIODevice::WriteOnly));
        file.write("placeholder");
    }

    std::vector<ImageIntegration::DetectedFrameInfo> detected;
    detected.push_back({FrameType::Light, makeImportedFrame(secondPath)});
    detected.push_back({FrameType::Light, makeImportedFrame(firstPath)});

    ImageIntegration::sortDetectedFramesForImport(detected);

    ASSERT_EQ(detected.size(), 2);
    EXPECT_EQ(detected[0].info.id.canonicalPath(), FrameId(firstPath).canonicalPath());
    EXPECT_EQ(detected[1].info.id.canonicalPath(), FrameId(secondPath).canonicalPath());
}
