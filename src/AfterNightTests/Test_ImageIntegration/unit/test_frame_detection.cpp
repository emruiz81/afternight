// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// A frame's kind from its cards, its folders and the caller's fallback, in that order.

#include "processes/ImageIntegration/core/FrameDetection.h"

#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Image/CodecMetadata.h>

#include <gtest/gtest.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <memory>
#include <string>
#include <utility>
#include <vector>

using namespace ImageIntegration;

namespace
{

[[nodiscard]] an::CodecCard card(an::CodecNamespace origin, std::string keyword, std::string value)
{
    an::Result<an::CodecCard> made = an::CodecCard::create(origin, std::move(keyword), std::move(value), "");
    EXPECT_TRUE(made.hasValue()) << made.status().message();
    return made.hasValue() ? std::move(*made) : *an::CodecCard::create(an::CodecNamespace::Fits, "X", "", "");
}

[[nodiscard]] an::CodecMetadata cardsOf(std::vector<an::CodecCard> held)
{
    an::Result<an::CodecMetadata> made = an::CodecMetadata::create({}, std::move(held));
    EXPECT_TRUE(made.hasValue()) << made.status().message();
    return made.hasValue() ? std::move(*made) : an::CodecMetadata{};
}

[[nodiscard]] an::CodecMetadata fitsCard(std::string keyword, std::string value)
{
    std::vector<an::CodecCard> held;
    held.push_back(card(an::CodecNamespace::Fits, std::move(keyword), std::move(value)));
    return cardsOf(std::move(held));
}

} // namespace

class FrameDetectionTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        tempDir = std::make_unique<QTemporaryDir>();
        ASSERT_TRUE(tempDir->isValid());
    }

    QString createTestFile(const QString& subPath)
    {
        const QString fullPath = tempDir->path() + "/" + subPath;
        const QFileInfo info(fullPath);
        QDir().mkpath(info.absolutePath());

        QFile file(fullPath);
        if (file.open(QIODevice::WriteOnly))
        {
            file.write("dummy content");
            file.close();
        }

        return fullPath;
    }

    std::unique_ptr<QTemporaryDir> tempDir;
};

// Test detection from directory names (path-based fallback)
TEST_F(FrameDetectionTest, DetectsFromPath_Bias)
{
    const QString path1 = createTestFile("bias/frame_001.fit");
    const QString path2 = createTestFile("BIAS/frame_002.fit");
    const QString path3 = createTestFile("biases/frame_003.fit");
    const QString path4 = createTestFile("offset/frame_004.fit");
    const QString path5 = createTestFile("offsets/frame_005.fit");

    EXPECT_EQ(FrameDetection::detectType(path1, nullptr, FrameType::Light), FrameType::Bias);
    EXPECT_EQ(FrameDetection::detectType(path2, nullptr, FrameType::Light), FrameType::Bias);
    EXPECT_EQ(FrameDetection::detectType(path3, nullptr, FrameType::Light), FrameType::Bias);
    EXPECT_EQ(FrameDetection::detectType(path4, nullptr, FrameType::Light), FrameType::Bias);
    EXPECT_EQ(FrameDetection::detectType(path5, nullptr, FrameType::Light), FrameType::Bias);
}

TEST_F(FrameDetectionTest, DetectsFromPath_Dark)
{
    const QString path1 = createTestFile("dark/frame_001.fit");
    const QString path2 = createTestFile("DARK/frame_002.fit");
    const QString path3 = createTestFile("darks/frame_003.fit");

    EXPECT_EQ(FrameDetection::detectType(path1, nullptr, FrameType::Light), FrameType::Dark);
    EXPECT_EQ(FrameDetection::detectType(path2, nullptr, FrameType::Light), FrameType::Dark);
    EXPECT_EQ(FrameDetection::detectType(path3, nullptr, FrameType::Light), FrameType::Dark);
}

TEST_F(FrameDetectionTest, DetectsFromPath_Flat)
{
    const QString path1 = createTestFile("flat/frame_001.fit");
    const QString path2 = createTestFile("FLAT/frame_002.fit");
    const QString path3 = createTestFile("flats/frame_003.fit");

    EXPECT_EQ(FrameDetection::detectType(path1, nullptr, FrameType::Light), FrameType::Flat);
    EXPECT_EQ(FrameDetection::detectType(path2, nullptr, FrameType::Light), FrameType::Flat);
    EXPECT_EQ(FrameDetection::detectType(path3, nullptr, FrameType::Light), FrameType::Flat);
}

TEST_F(FrameDetectionTest, DetectsFromPath_Light)
{
    const QString path1 = createTestFile("light/frame_001.fit");
    const QString path2 = createTestFile("LIGHT/frame_002.fit");
    const QString path3 = createTestFile("lights/frame_003.fit");

    EXPECT_EQ(FrameDetection::detectType(path1, nullptr, FrameType::Bias), FrameType::Light);
    EXPECT_EQ(FrameDetection::detectType(path2, nullptr, FrameType::Bias), FrameType::Light);
    EXPECT_EQ(FrameDetection::detectType(path3, nullptr, FrameType::Bias), FrameType::Light);
}

TEST_F(FrameDetectionTest, DetectsFromPath_CaseInsensitive)
{
    const QString path1 = createTestFile("Bias/frame.fit");
    const QString path2 = createTestFile("DARK/frame.fit");
    const QString path3 = createTestFile("Flat/frame.fit");
    const QString path4 = createTestFile("Light/frame.fit");

    EXPECT_EQ(FrameDetection::detectType(path1, nullptr, FrameType::Light), FrameType::Bias);
    EXPECT_EQ(FrameDetection::detectType(path2, nullptr, FrameType::Light), FrameType::Dark);
    EXPECT_EQ(FrameDetection::detectType(path3, nullptr, FrameType::Light), FrameType::Flat);
    EXPECT_EQ(FrameDetection::detectType(path4, nullptr, FrameType::Dark), FrameType::Light);
}

TEST_F(FrameDetectionTest, ReturnsDefaultForUnknownPath)
{
    const QString path = createTestFile("random/subfolder/frame.fit");

    EXPECT_EQ(FrameDetection::detectType(path, nullptr, FrameType::Light), FrameType::Light);
    EXPECT_EQ(FrameDetection::detectType(path, nullptr, FrameType::Bias), FrameType::Bias);
    EXPECT_EQ(FrameDetection::detectType(path, nullptr, FrameType::Dark), FrameType::Dark);
    EXPECT_EQ(FrameDetection::detect(path, nullptr, FrameType::Dark).evidence, FrameTypeEvidence::Fallback);
}

TEST_F(FrameDetectionTest, HandlesEmptyPath)
{
    EXPECT_EQ(FrameDetection::detectType(QString(), nullptr, FrameType::Light), FrameType::Light);
    EXPECT_EQ(FrameDetection::detectType("", nullptr, FrameType::Dark), FrameType::Dark);
}

TEST_F(FrameDetectionTest, DetectsFromNestedPath)
{
    // Should find "bias" folder even when nested deep
    const QString path = createTestFile("session/2026-01-13/bias/M42/frame.fit");

    EXPECT_EQ(FrameDetection::detectType(path, nullptr, FrameType::Light), FrameType::Bias);
}

TEST_F(FrameDetectionTest, PrioritizesClosestFolderMatch)
{
    // When multiple type keywords in path, closest to file wins
    const QString path = createTestFile("light/dark/frame.fit");

    // "dark" is closer to the file than "light"
    EXPECT_EQ(FrameDetection::detectType(path, nullptr, FrameType::Bias), FrameType::Dark);
}

TEST(FrameDetectionFolderHint, NamesTheKindAndTheFolder)
{
    QString folder;
    EXPECT_EQ(FrameDetection::folderHint("/test/bias/file.fit", &folder), FrameType::Bias);
    EXPECT_EQ(folder, QStringLiteral("bias"));
    EXPECT_EQ(FrameDetection::folderHint("/test/Darks/file.fit", &folder), FrameType::Dark);
    EXPECT_EQ(folder, QStringLiteral("Darks"));
    EXPECT_EQ(FrameDetection::folderHint("/test/flat/file.fit"), FrameType::Flat);
    EXPECT_EQ(FrameDetection::folderHint("/test/light/file.fit"), FrameType::Light);
    EXPECT_FALSE(FrameDetection::folderHint("/test/unknown/file.fit").has_value());
}

TEST(FrameDetectionFolderHint, HandlesEmptyInput)
{
    EXPECT_FALSE(FrameDetection::folderHint(QString()).has_value());
}

// The cards are the file's own statement and outrank the folder it was filed in.
TEST_F(FrameDetectionTest, TheFrameTypeCardOutranksTheFolder)
{
    const QString lightInDarks = createTestFile("darks/frame_001.fit");
    const an::CodecMetadata light = fitsCard("IMAGETYP", "'Light Frame'");

    const FrameTypeDetection detected = FrameDetection::detect(lightInDarks, &light, FrameType::Bias);
    EXPECT_EQ(detected.type, FrameType::Light);
    EXPECT_EQ(detected.evidence, FrameTypeEvidence::Card);
    EXPECT_EQ(detected.source, QStringLiteral("IMAGETYP"));
    EXPECT_EQ(detected.spelling, QStringLiteral("'Light Frame'"));

    const QString darkInLights = createTestFile("lights/frame_002.fit");
    const an::CodecMetadata dark = fitsCard("IMAGETYP", "DARK");
    EXPECT_EQ(FrameDetection::detectType(darkInLights, &dark, FrameType::Light), FrameType::Dark);
}

TEST_F(FrameDetectionTest, ReadsEveryCardACaptureProgramWrites)
{
    const QString path = createTestFile("random/frame.fit");

    const an::CodecMetadata frame = fitsCard("FRAME", "Flat");
    const FrameTypeDetection fromFrame = FrameDetection::detect(path, &frame, FrameType::Light);
    EXPECT_EQ(fromFrame.type, FrameType::Flat);
    EXPECT_EQ(fromFrame.source, QStringLiteral("FRAME"));

    std::vector<an::CodecCard> xisf;
    xisf.push_back(card(an::CodecNamespace::Xisf, "ImageType", "Bias"));
    const an::CodecMetadata xisfCards = cardsOf(std::move(xisf));
    const FrameTypeDetection fromXisf = FrameDetection::detect(path, &xisfCards, FrameType::Light);
    EXPECT_EQ(fromXisf.type, FrameType::Bias);
    EXPECT_EQ(fromXisf.source, QStringLiteral("ImageType"));

    // A bias written as zero or offset, a master dark, a science frame.
    const an::CodecMetadata zero = fitsCard("IMAGETYP", "Zero");
    EXPECT_EQ(FrameDetection::detectType(path, &zero, FrameType::Light), FrameType::Bias);
    const an::CodecMetadata offset = fitsCard("IMAGETYP", "OFFSET");
    EXPECT_EQ(FrameDetection::detectType(path, &offset, FrameType::Light), FrameType::Bias);
    const an::CodecMetadata master = fitsCard("IMAGETYP", "Master Dark");
    EXPECT_EQ(FrameDetection::detectType(path, &master, FrameType::Light), FrameType::Dark);
    const an::CodecMetadata science = fitsCard("IMAGETYP", "SCIENCE");
    EXPECT_EQ(FrameDetection::detectType(path, &science, FrameType::Bias), FrameType::Light);
}

TEST_F(FrameDetectionTest, ACardThatNamesNoKindLeavesTheFolderToDecide)
{
    const QString path = createTestFile("flats/frame.fit");

    const an::CodecMetadata unreadable = fitsCard("IMAGETYP", "DefectMap");
    const FrameTypeDetection detected = FrameDetection::detect(path, &unreadable, FrameType::Light);
    EXPECT_EQ(detected.type, FrameType::Flat);
    EXPECT_EQ(detected.evidence, FrameTypeEvidence::Folder);
    EXPECT_EQ(detected.source, QStringLiteral("flats"));

    const an::CodecMetadata none = cardsOf({});
    EXPECT_EQ(FrameDetection::detect(path, &none, FrameType::Light).evidence, FrameTypeEvidence::Folder);

    const QString unfiled = createTestFile("random/frame.fit");
    const FrameTypeDetection fallback = FrameDetection::detect(unfiled, &unreadable, FrameType::Dark);
    EXPECT_EQ(fallback.type, FrameType::Dark);
    EXPECT_EQ(fallback.evidence, FrameTypeEvidence::Fallback);
    EXPECT_TRUE(fallback.source.isEmpty());
}

// Edge cases
TEST_F(FrameDetectionTest, HandlesPathWithSpecialCharacters)
{
    const QString path = createTestFile("bias/M42 (Ha) ñ/frame_#1.fit");

    EXPECT_EQ(FrameDetection::detectType(path, nullptr, FrameType::Light), FrameType::Bias);
}

TEST_F(FrameDetectionTest, HandlesWindowsAndUnixPaths)
{
    // Test with backslash paths (Windows-style)
    const QString winPath = createTestFile("bias/subfolder/frame.fit").replace('/', '\\');
    const QString unixPath = createTestFile("dark/subfolder/frame.fit");

    // Both should work since QRegularExpression in FrameDetection handles both separators
    EXPECT_EQ(FrameDetection::detectType(winPath, nullptr, FrameType::Light), FrameType::Bias);
    EXPECT_EQ(FrameDetection::detectType(unixPath, nullptr, FrameType::Light), FrameType::Dark);
}

TEST_F(FrameDetectionTest, IgnoresFileNameOnlyFolderMatters)
{
    // File name contains "dark" but folder is "light" - folder should win
    const QString path = createTestFile("light/dark_frame.fit");

    EXPECT_EQ(FrameDetection::detectType(path, nullptr, FrameType::Bias), FrameType::Light);
}
