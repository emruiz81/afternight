// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// What the settings know of the DeepSNR command-line program: the versions it accepts, what its machine information
/// says it supports, the arguments a run passes it and their defaults, the strides it takes, and where the weights
/// browser starts.

#include "controls/settings/DeepSNRCliTools.h"

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

// A version is read from the text the program prints, and only 1.2.0 or newer is supported.
TEST(DeepSNRCliToolsTest, VersionParsingRequires120OrNewer)
{
    const auto current = an::ui::deepsnr::parseVersionText(QStringLiteral("DeepSNR v1.2.0"));
    ASSERT_TRUE(current.has_value());
    EXPECT_TRUE(an::ui::deepsnr::isSupportedVersion(*current));

    const auto patch = an::ui::deepsnr::parseVersionText(QStringLiteral("DeepSNR version 1.2.2"));
    ASSERT_TRUE(patch.has_value());
    EXPECT_TRUE(an::ui::deepsnr::isSupportedVersion(*patch));

    const auto old = an::ui::deepsnr::parseVersionText(QStringLiteral("DeepSNR 1.1.9"));
    ASSERT_TRUE(old.has_value());
    EXPECT_FALSE(an::ui::deepsnr::isSupportedVersion(*old));

    EXPECT_EQ(an::ui::deepsnr::minimumSupportedVersionText(), QStringLiteral("1.2.0"));
}

// The machine information names the program's version and whether its machine progress is stable as JSON lines;
// another product's information is not DeepSNR's.
TEST(DeepSNRCliToolsTest, MachineInfoDetectsVersionAndStableProgressCapability)
{
    const auto info = an::ui::deepsnr::parseMachineInfoText(QStringLiteral(R"json(
        {
          "schema": "starnetastro.cli.machine-info.v1",
          "product": "deepsnr",
          "version": "1.2.3",
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
    EXPECT_EQ(info->version, QVersionNumber(1, 2, 3));
    EXPECT_TRUE(info->machineProgressSupported);

    EXPECT_FALSE(an::ui::deepsnr::parseMachineInfoText(QStringLiteral(R"json(
        {
          "schema": "starnetastro.cli.machine-info.v1",
          "product": "starnet2",
          "version": "2.5.4"
        }
    )json"))
                     .has_value());

    const auto wrongFormat = an::ui::deepsnr::parseMachineInfoText(QStringLiteral(R"json(
        {
          "schema": "starnetastro.cli.machine-info.v1",
          "product": "deepsnr",
          "version": "1.2.3",
          "io": {
            "progress": {
              "machine": {
                "supported": true,
                "stable": true,
                "stream": "stderr",
                "format": "text",
                "schema": "starnetastro.cli.progress.v1",
                "option": "machine_progress"
              }
            }
          }
        }
    )json"));
    ASSERT_TRUE(wrongFormat.has_value());
    EXPECT_FALSE(wrongFormat->machineProgressSupported);
}

// A run passes every path and option by the names the program documents, and nothing else.
TEST(DeepSNRCliToolsTest, ModernArgumentsUseDocumentedOptions)
{
    an::ui::deepsnr::CliOptions options;
    options.inputPath = QStringLiteral("input.tif");
    options.outputPath = QStringLiteral("denoised.tif");
    options.weightsPath = QStringLiteral("DeepSNR_weights_v2.onnx");
    options.stride = 384;

    const QStringList args = an::ui::deepsnr::buildArguments(options);

    EXPECT_EQ(args.indexOf(QStringLiteral("--input")) + 1, args.indexOf(QStringLiteral("input.tif")));
    EXPECT_EQ(args.indexOf(QStringLiteral("--output")) + 1, args.indexOf(QStringLiteral("denoised.tif")));
    EXPECT_EQ(args.indexOf(QStringLiteral("--weights")) + 1, args.indexOf(QStringLiteral("DeepSNR_weights_v2.onnx")));
    EXPECT_EQ(args.indexOf(QStringLiteral("--stride")) + 1, args.indexOf(QStringLiteral("384")));

    EXPECT_EQ(optionKeys(args), QStringList({QStringLiteral("--input"), QStringLiteral("--output"),
                                             QStringLiteral("--weights"), QStringLiteral("--stride")}));
}

// Machine progress is asked for only when the run asks for it.
TEST(DeepSNRCliToolsTest, MachineProgressArgumentIsOptIn)
{
    an::ui::deepsnr::CliOptions options;
    options.inputPath = QStringLiteral("input.tif");
    options.outputPath = QStringLiteral("denoised.tif");

    EXPECT_FALSE(an::ui::deepsnr::buildArguments(options).contains(QStringLiteral("--machine-progress")));

    options.machineProgress = true;
    EXPECT_TRUE(an::ui::deepsnr::buildArguments(options).contains(QStringLiteral("--machine-progress")));
}

// A run left at its defaults passes the default stride and no model, quiet or eight-bit option.
TEST(DeepSNRCliToolsTest, DefaultsUseCliModelTwoAndStride480)
{
    an::ui::deepsnr::CliOptions options;
    options.inputPath = QStringLiteral("input.tif");
    options.outputPath = QStringLiteral("denoised.tif");

    const QStringList args = an::ui::deepsnr::buildArguments(options);

    EXPECT_EQ(args.indexOf(QStringLiteral("--stride")) + 1,
              args.indexOf(QString::number(an::ui::deepsnr::kDefaultStride)));
    EXPECT_FALSE(args.contains(QStringLiteral("--model")));
    EXPECT_FALSE(args.contains(QStringLiteral("--quiet")));
    EXPECT_FALSE(args.contains(QStringLiteral("--eight")));
}

// A stride is held to an even value inside the range the program takes.
TEST(DeepSNRCliToolsTest, StrideIsNormalizedToCliRangeAndEvenValues)
{
    EXPECT_EQ(an::ui::deepsnr::normalizeStride(1), 2);
    EXPECT_EQ(an::ui::deepsnr::normalizeStride(3), 2);
    EXPECT_EQ(an::ui::deepsnr::normalizeStride(481), 480);
    EXPECT_EQ(an::ui::deepsnr::normalizeStride(999), 512);
}

// With no weights, or the default ones, the weights browser starts in the installation's support folder.
TEST(DeepSNRCliToolsTest, WeightsBrowserStartsAtInstallSupportFolderForDefaultWeights)
{
    QTemporaryDir root;
    ASSERT_TRUE(root.isValid());
    QDir rootDir(root.path());
    ASSERT_TRUE(rootDir.mkpath(QStringLiteral("bin")));
    ASSERT_TRUE(rootDir.mkpath(QStringLiteral("lib/deepsnr")));

    const QString executablePath = rootDir.filePath(QStringLiteral("bin/deepsnr.exe"));
    const QString supportDir = rootDir.filePath(QStringLiteral("lib/deepsnr"));

    EXPECT_EQ(cleanPath(an::ui::deepsnr::weightsBrowseDirectory(QString(), executablePath)), cleanPath(supportDir));
    EXPECT_EQ(
        cleanPath(an::ui::deepsnr::weightsBrowseDirectory(QStringLiteral("DeepSNR_weights_v2.onnx"), executablePath)),
        cleanPath(supportDir));
}

// A portable installation with no support folder starts the browser in the program's own folder.
TEST(DeepSNRCliToolsTest, WeightsBrowserFallsBackToPortableExecutableFolder)
{
    QTemporaryDir install;
    ASSERT_TRUE(install.isValid());
    const QString executablePath = QDir(install.path()).filePath(QStringLiteral("deepsnr.exe"));

    EXPECT_EQ(cleanPath(an::ui::deepsnr::weightsBrowseDirectory(QString(), executablePath)), cleanPath(install.path()));
}

// With weights of the person's own, the browser starts in their folder.
TEST(DeepSNRCliToolsTest, WeightsBrowserStartsAtCustomWeightsFolder)
{
    QTemporaryDir root;
    ASSERT_TRUE(root.isValid());
    QDir rootDir(root.path());
    ASSERT_TRUE(rootDir.mkpath(QStringLiteral("models")));

    const QString weightsPath = rootDir.filePath(QStringLiteral("models/custom.onnx"));
    EXPECT_EQ(cleanPath(an::ui::deepsnr::weightsBrowseDirectory(weightsPath, QString())),
              cleanPath(rootDir.filePath(QStringLiteral("models"))));
}
