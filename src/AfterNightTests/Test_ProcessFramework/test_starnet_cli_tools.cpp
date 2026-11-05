// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// What the settings know of the StarNet command-line program: the versions it accepts, what its machine information
/// says it supports, the arguments a run passes it, the strides it takes, and where the weights browser starts.

#include "controls/settings/StarNetCliTools.h"

#include <QDir>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QVersionNumber>

#include <gtest/gtest.h>

namespace
{

[[nodiscard]] QStringList optionKeys(const QStringList& args)
{
    QStringList keys;
    for (const QString& arg : args)
    {
        if (arg.startsWith(QStringLiteral("--")))
        {
            keys.push_back(arg);
        }
    }
    return keys;
}

[[nodiscard]] QString cleanPath(const QString& path)
{
    return QDir::cleanPath(path);
}

} // namespace

// A version is read from the text the program prints, and only 2.5.3 or newer is supported.
TEST(StarNetCliToolsTest, VersionParsingRequires253OrNewer)
{
    const auto current = an::ui::starnet::parseVersionText(QStringLiteral("StarNet2 v2.5.3"));
    ASSERT_TRUE(current.has_value());
    EXPECT_TRUE(an::ui::starnet::isSupportedVersion(*current));

    const auto patch = an::ui::starnet::parseVersionText(QStringLiteral("StarNet2 v2.6.0"));
    ASSERT_TRUE(patch.has_value());
    EXPECT_TRUE(an::ui::starnet::isSupportedVersion(*patch));

    const auto old = an::ui::starnet::parseVersionText(QStringLiteral("StarNet2 v2.5.2"));
    ASSERT_TRUE(old.has_value());
    EXPECT_FALSE(an::ui::starnet::isSupportedVersion(*old));

    EXPECT_EQ(an::ui::starnet::minimumSupportedVersionText(), QStringLiteral("2.5.3"));
}

// The machine information names the program's version and whether its machine progress is stable on stderr as
// JSON lines; another product's information is not StarNet's.
TEST(StarNetCliToolsTest, MachineInfoDetectsVersionAndStableProgressCapability)
{
    const auto info = an::ui::starnet::parseMachineInfoText(QStringLiteral(R"json(
        {
          "schema": "starnetastro.cli.machine-info.v1",
          "product": "starnet2",
          "version": "2.5.4",
          "io": {
            "progress": {
              "machine": {
                "supported": true,
                "stable": true,
                "stream": "stderr",
                "format": "json_lines",
                "schema": "starnetastro.cli.progress.v1",
                "option": "machine_progress"
              }
            }
          }
        }
    )json"));

    ASSERT_TRUE(info.has_value());
    EXPECT_EQ(info->version, QVersionNumber(2, 5, 4));
    EXPECT_TRUE(info->machineProgressSupported);

    EXPECT_FALSE(an::ui::starnet::parseMachineInfoText(QStringLiteral(R"json(
        {
          "schema": "starnetastro.cli.machine-info.v1",
          "product": "deepsnr",
          "version": "1.2.3"
        }
    )json"))
                     .has_value());

    const auto wrongStream = an::ui::starnet::parseMachineInfoText(QStringLiteral(R"json(
        {
          "schema": "starnetastro.cli.machine-info.v1",
          "product": "starnet2",
          "version": "2.5.4",
          "io": {
            "progress": {
              "machine": {
                "supported": true,
                "stable": true,
                "stream": "stdout",
                "format": "json_lines",
                "schema": "starnetastro.cli.progress.v1",
                "option": "machine_progress"
              }
            }
          }
        }
    )json"));
    ASSERT_TRUE(wrongStream.has_value());
    EXPECT_FALSE(wrongStream->machineProgressSupported);
}

// A run passes every path and option by the names the supported versions take, and nothing else.
TEST(StarNetCliToolsTest, ModernArgumentsUseNamed253Options)
{
    an::ui::starnet::CliOptions options;
    options.inputPath = QStringLiteral("input.tif");
    options.outputPath = QStringLiteral("starless.tif");
    options.maskPath = QStringLiteral("mask.tif");
    options.unscreenPath = QStringLiteral("stars.tif");
    options.weightsPath = QStringLiteral("custom.onnx");
    options.stride = 384;
    options.upsample = true;
    options.highlightProtection = false;

    const QStringList args = an::ui::starnet::buildArguments(options);

    EXPECT_EQ(args.indexOf(QStringLiteral("--input")) + 1, args.indexOf(QStringLiteral("input.tif")));
    EXPECT_EQ(args.indexOf(QStringLiteral("--output")) + 1, args.indexOf(QStringLiteral("starless.tif")));
    EXPECT_EQ(args.indexOf(QStringLiteral("--mask")) + 1, args.indexOf(QStringLiteral("mask.tif")));
    EXPECT_EQ(args.indexOf(QStringLiteral("--unscreen")) + 1, args.indexOf(QStringLiteral("stars.tif")));
    EXPECT_EQ(args.indexOf(QStringLiteral("--weights")) + 1, args.indexOf(QStringLiteral("custom.onnx")));
    EXPECT_EQ(args.indexOf(QStringLiteral("--stride")) + 1, args.indexOf(QStringLiteral("384")));
    EXPECT_TRUE(args.contains(QStringLiteral("--upsample")));
    EXPECT_TRUE(args.contains(QStringLiteral("--disable-highlights-protection")));

    EXPECT_EQ(optionKeys(args),
              QStringList({QStringLiteral("--input"), QStringLiteral("--output"), QStringLiteral("--mask"),
                           QStringLiteral("--unscreen"), QStringLiteral("--weights"), QStringLiteral("--stride"),
                           QStringLiteral("--upsample"), QStringLiteral("--disable-highlights-protection")}));
}

// Machine progress is asked for only when the run asks for it.
TEST(StarNetCliToolsTest, MachineProgressArgumentIsOptIn)
{
    an::ui::starnet::CliOptions options;
    options.inputPath = QStringLiteral("input.tif");
    options.outputPath = QStringLiteral("starless.tif");

    EXPECT_FALSE(an::ui::starnet::buildArguments(options).contains(QStringLiteral("--machine-progress")));

    options.machineProgress = true;
    EXPECT_TRUE(an::ui::starnet::buildArguments(options).contains(QStringLiteral("--machine-progress")));
}

// A stride is held to an even value inside the range the program takes.
TEST(StarNetCliToolsTest, StrideIsNormalizedToCliRangeAndEvenValues)
{
    EXPECT_EQ(an::ui::starnet::normalizeStride(1), 2);
    EXPECT_EQ(an::ui::starnet::normalizeStride(3), 2);
    EXPECT_EQ(an::ui::starnet::normalizeStride(385), 384);
    EXPECT_EQ(an::ui::starnet::normalizeStride(999), 512);
}

// With no weights, or the default ones, the weights browser starts in the program's folder.
TEST(StarNetCliToolsTest, WeightsBrowserStartsAtInstallFolderForDefaultWeights)
{
    QTemporaryDir install;
    ASSERT_TRUE(install.isValid());
    const QString executablePath = QDir(install.path()).filePath(QStringLiteral("starnet2.exe"));

    EXPECT_EQ(cleanPath(an::ui::starnet::weightsBrowseDirectory(QString(), executablePath)), cleanPath(install.path()));
    EXPECT_EQ(
        cleanPath(an::ui::starnet::weightsBrowseDirectory(QStringLiteral("StarNet2_weights.onnx"), executablePath)),
        cleanPath(install.path()));
}

// With weights of the person's own, the browser starts in their folder.
TEST(StarNetCliToolsTest, WeightsBrowserStartsAtCustomWeightsFolder)
{
    QTemporaryDir root;
    ASSERT_TRUE(root.isValid());
    QDir rootDir(root.path());
    ASSERT_TRUE(rootDir.mkpath(QStringLiteral("models")));

    const QString weightsPath = rootDir.filePath(QStringLiteral("models/custom.onnx"));
    EXPECT_EQ(cleanPath(an::ui::starnet::weightsBrowseDirectory(weightsPath, QString())),
              cleanPath(rootDir.filePath(QStringLiteral("models"))));
}

// Relative weights are found from the program's folder.
TEST(StarNetCliToolsTest, WeightsBrowserResolvesRelativeWeightsFromInstallFolder)
{
    QTemporaryDir install;
    ASSERT_TRUE(install.isValid());
    QDir installDir(install.path());
    ASSERT_TRUE(installDir.mkpath(QStringLiteral("models")));
    const QString executablePath = installDir.filePath(QStringLiteral("starnet2.exe"));

    EXPECT_EQ(cleanPath(an::ui::starnet::weightsBrowseDirectory(QStringLiteral("models/custom.onnx"), executablePath)),
              cleanPath(installDir.filePath(QStringLiteral("models"))));
}
