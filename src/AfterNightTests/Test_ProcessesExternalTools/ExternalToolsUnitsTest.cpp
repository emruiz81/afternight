// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The support unit the three windows share, run directly over a stand-in program: the ten hand-off records of the
/// behaviour corpus reproduced - the image the tool is handed, read from the copy the stand-in keeps, and the image and
/// the star layer a tool that changes nothing brings back - the Float32 format where a tool takes it, the mask as the
/// tool's own answer, the shaping of a star mask, the metadata each answer carries, the requests every window spells,
/// the validity the hand-off reads under, the program's progress through the operation, and every way a program can
/// fail: refused before anything is written, could not start, crashed, exited with a code, wrote nothing, wrote the
/// wrong size, or was stopped.

#include "Test_ProcessesExternalTools/ExternalToolsTestSupport.h"

#include "controls/settings/DeepSNRCliTools.h"
#include "controls/settings/StarNetCliTools.h"
#include "processes/DeepSNRProcess.h"
#include "processes/StarnetProcess.h"
#include "processes/StarnetStarMaskProcess.h"

#include "fixtures/common/CombinationCases.h"

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/IO/ProcessingStateCards.h>
#include <AfterNightEngine/Image/ConversionPolicy.h>
#include <AfterNightEngine/Image/ImageConversion.h>
#include <AfterNightEngine/Image/SampleTraits.h>
#include <AfterNightEngine/Image/ImageMetadata.h>
#include <AfterNightEngine/Image/Provenance.h>
#include <AfterNightEngine/Image/ValidityMask.h>

#include <QCoreApplication>
#include <QFileInfo>

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

namespace
{

namespace fixtures = an::tests::fixtures;
namespace adapter = an::tests::fixtures::engine;
using namespace an::tests::processes;
using namespace an::tests::processes::externaltools;
using an::ui::processes::ExternalToolFiles;
using an::ui::processes::ExternalToolRequest;

class ExternalToolsUnitsTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        m_context = openProcessContext(4u);
        ASSERT_NE(m_context, nullptr);
        m_installed.emplace(*m_context);
    }

    void TearDown() override
    {
        m_installed.reset();
        m_context.reset();
    }

    [[nodiscard]] ProcessTestContext& context() const
    {
        return *m_context;
    }

    [[nodiscard]] const an::consumer::EngineSession& session() const
    {
        return m_context->engineSession();
    }

private:
    std::unique_ptr<ProcessTestContext> m_context;
    std::optional<InstalledContext> m_installed;
};

/// StarNet's request over a stand-in, as the window spells it.
[[nodiscard]] ExternalToolRequest starnetRequest(bool preStretch, bool stars = false)
{
    StarnetProcess::Settings settings;
    settings.preStretch = preStretch;
    settings.generateStarOutput = stars;
    settings.starOutputMode = QStringLiteral("both");
    return StarnetProcess::toolRequest(settings, StandInTools::executablePath());
}

/// The star mask's request over a stand-in.
[[nodiscard]] ExternalToolRequest starMaskRequest(bool preStretch)
{
    StarnetStarMaskProcess::Settings settings;
    settings.preStretch = preStretch;
    return StarnetStarMaskProcess::toolRequest(settings, StandInTools::executablePath());
}

/// DeepSNR's request over a stand-in, its smallest size lowered so the fixture scenes reach it.
[[nodiscard]] ExternalToolRequest deepSnrRequest(bool preStretch)
{
    DeepSNRProcess::Settings settings;
    settings.preStretch = preStretch;
    ExternalToolRequest request = DeepSNRProcess::toolRequest(settings, StandInTools::executablePath());
    request.tool.minimumWidth = 0u;
    request.tool.minimumHeight = 0u;
    return request;
}

[[nodiscard]] std::optional<fixtures::FixtureSamples> samplesOf(const an::consumer::ImageAsset& asset)
{
    std::string error;
    std::optional<fixtures::FixtureSamples> samples = adapter::readAssetSamples(asset, error);
    EXPECT_TRUE(samples.has_value()) << error;
    return samples;
}

/// The hand-off records of the corpus, each with the case that states its input and settings.
[[nodiscard]] std::vector<std::pair<fixtures::Expectation, const fixtures::CombinationCase*>> handoffRecords()
{
    std::vector<std::pair<fixtures::Expectation, const fixtures::CombinationCase*>> records;
    std::string error;
    const std::optional<std::filesystem::path> directory = fixtures::expectationDirectory(error);
    EXPECT_TRUE(directory.has_value()) << error;
    if (!directory.has_value())
    {
        return records;
    }
    const std::optional<std::vector<fixtures::Expectation>> read =
        fixtures::readExpectationFile(*directory / fixtures::kHandoffExpectationFile, error);
    EXPECT_TRUE(read.has_value()) << error;
    if (!read.has_value())
    {
        return records;
    }
    for (const fixtures::Expectation& record : *read)
    {
        const fixtures::CombinationCase* item = fixtures::combinationCase(record.id);
        EXPECT_NE(item, nullptr) << record.id;
        if (item != nullptr)
        {
            records.emplace_back(record, item);
        }
    }
    return records;
}

[[nodiscard]] bool hasCard(const an::ImageMetadata& metadata, std::string_view keyword)
{
    for (const an::CodecCard& card : metadata.codec().cards())
    {
        if (card.keyword() == keyword)
        {
            return true;
        }
    }
    return false;
}

[[nodiscard]] bool lineageNames(const an::ImageMetadata& metadata, std::string_view text)
{
    for (const an::ProvenanceEntry& entry : metadata.provenance().entries())
    {
        if (entry.operation().find(text) != std::string::npos || entry.detail().find(text) != std::string::npos)
        {
            return true;
        }
    }
    return false;
}

} // namespace

// Every hand-off record of the behaviour corpus is reproduced through the support: the handed image is the copy the
// stand-in kept of what it was given, and the restored image and star layer are what a tool that changes nothing brings
// back; each held to its record by the record's own bound.
TEST_F(ExternalToolsUnitsTest, EveryHandoffRecordIsReproducedThroughTheSupport)
{
    const std::vector<std::pair<fixtures::Expectation, const fixtures::CombinationCase*>> records = handoffRecords();
    ASSERT_EQ(records.size(), 10u);
    const ExternalTool& starnet = *toolNamed("Starnet");
    for (const auto& [record, item] : records)
    {
        SCOPED_TRACE(record.id);
        StandInTools standIn(context(), starnet);
        const std::optional<an::consumer::ImageAsset> source = sceneAsset(session(), item->input);
        ASSERT_TRUE(source.has_value());
        ExternalToolRequest request = item->handoff.stretch == "linked"
                                          ? starMaskRequest(true)
                                          : starnetRequest(item->handoff.stretch == "unlinked", true);
        if (item->handoff.stretch == "linked")
        {
            // The star mask's request reads only the mask; the handed image is what the record holds.
            request.wantsImage = false;
        }
        const an::Result<an::ui::processes::ExternalToolAnswers> answers = runSupport(session(), *source, request);
        ASSERT_TRUE(answers.hasValue()) << answers.status().message();
        std::optional<fixtures::FixtureSamples> candidate;
        if (item->handoff.output == fixtures::HandoffCaseOutput::Handed)
        {
            an::Result<adapter::DecodedFile> handed =
                adapter::decodeEngineFile(session(), std::filesystem::path(standIn.keptInputPath().toStdWString()));
            ASSERT_TRUE(handed.hasValue()) << handed.status().message();
            candidate = handed->samples;
        }
        else if (item->handoff.output == fixtures::HandoffCaseOutput::RestoredStars)
        {
            ASSERT_TRUE(answers->stars.has_value());
            // The stand-in answers the star layer at half the handed codes, truncated; the record holds the whole layer
            // brought back by the span alone, so twice the restored layer is the record within its bound and one
            // truncated code's share of the span.
            std::optional<fixtures::FixtureSamples> stars = samplesOf(*answers->stars);
            ASSERT_TRUE(stars.has_value());
            double largest = 0.0;
            for (std::uint32_t c = 0u; c < stars->channelCount(); ++c)
            {
                for (std::uint32_t y = 0u; y < stars->height(); ++y)
                {
                    for (std::uint32_t x = 0u; x < stars->width(); ++x)
                    {
                        const double layer = 2.0 * fractionOf(*stars, x, y, c);
                        const double recorded = fractionOf(record.result, x, y, c);
                        largest = (std::max)(largest, std::fabs(layer - recorded));
                    }
                }
            }
            EXPECT_LE(largest, record.absoluteTolerance + 1.0e-4) << "the star layer against its record";
            continue;
        }
        else
        {
            ASSERT_TRUE(answers->image.has_value());
            candidate = samplesOf(*answers->image);
        }
        ASSERT_TRUE(candidate.has_value());
        const fixtures::ExpectationComparison comparison = fixtures::compareWithExpectation(record, *candidate);
        EXPECT_TRUE(comparison.matched) << comparison.reason << " (largest " << comparison.largestDifference << ")";
    }
}

// A tool whose profile says it takes Float32 is handed Float32 and loses nothing: a floating scene beyond the unit
// interval comes back from a stand-in that changes nothing exactly as it was, where the sixteen-bit hand-off comes back
// within one code's share of the range.
TEST_F(ExternalToolsUnitsTest, AToolThatTakesFloat32IsHandedFloat32AndLosesNothing)
{
    const ExternalTool& starnet = *toolNamed("Starnet");
    StandInTools standIn(context(), starnet);
    const std::optional<an::consumer::ImageAsset> source = sceneAsset(session(), "scene-rgb-float32-unbounded");
    ASSERT_TRUE(source.has_value());
    const std::optional<fixtures::FixtureSamples> before = samplesOf(*source);
    ASSERT_TRUE(before.has_value());

    ExternalToolRequest request = starnetRequest(false);
    request.tool.format = an::ExternalToolFormat::Float32;
    const an::Result<an::ui::processes::ExternalToolAnswers> floating = runSupport(session(), *source, request);
    ASSERT_TRUE(floating.hasValue()) << floating.status().message();
    an::Result<adapter::DecodedFile> handed =
        adapter::decodeEngineFile(session(), std::filesystem::path(standIn.keptInputPath().toStdWString()));
    ASSERT_TRUE(handed.hasValue()) << handed.status().message();
    EXPECT_EQ(handed->samples.sampleType(), fixtures::FixtureSampleType::Float32);
    const std::optional<fixtures::FixtureSamples> back = samplesOf(*floating->image);
    ASSERT_TRUE(back.has_value());
    EXPECT_LE(largestFractionDifference(*before, *back), 1.0e-6);

    request.tool.format = an::ExternalToolFormat::UInt16;
    const an::Result<an::ui::processes::ExternalToolAnswers> integer = runSupport(session(), *source, request);
    ASSERT_TRUE(integer.hasValue()) << integer.status().message();
    handed = adapter::decodeEngineFile(session(), std::filesystem::path(standIn.keptInputPath().toStdWString()));
    ASSERT_TRUE(handed.hasValue());
    EXPECT_EQ(handed->samples.sampleType(), fixtures::FixtureSampleType::UInt16);
    const std::optional<fixtures::FixtureSamples> quantized = samplesOf(*integer->image);
    ASSERT_TRUE(quantized.has_value());
    // The scene spans about 3.1 of the unit interval, so one of 65,535 codes is about 4.8e-5 of it.
    const double difference = largestFractionDifference(*before, *quantized);
    EXPECT_GT(difference, 1.0e-6);
    EXPECT_LE(difference, 6.0e-5);
}

// The requests the windows spell: StarNet hands sixteen-bit codes stretched channel by channel when asked and reads
// its star outputs by its mode; the star mask stretches linked and reads only the mask; DeepSNR keeps the image's
// metadata and takes nothing below 512 by 512. Each spells the program's arguments as the CLI helpers do.
TEST_F(ExternalToolsUnitsTest, TheWindowsRequestsSpellTheirPrograms)
{
    StarnetProcess::Settings starnet;
    starnet.stride = 384;
    starnet.upsample = true;
    starnet.highlightProtection = false;
    starnet.weightsPath = QStringLiteral("C:/weights.onnx");
    starnet.preStretch = true;
    starnet.generateStarOutput = true;
    starnet.starOutputMode = QStringLiteral("mask");
    const ExternalToolRequest first = StarnetProcess::toolRequest(starnet, QStringLiteral("starnet2.exe"));
    EXPECT_EQ(first.tool.name, QStringLiteral("StarNet2"));
    EXPECT_EQ(first.tool.format, an::ExternalToolFormat::UInt16);
    EXPECT_EQ(first.preStretch, an::ExternalToolPreStretch::Unlinked);
    EXPECT_TRUE(first.wantsImage);
    EXPECT_TRUE(first.wantsMask);
    EXPECT_FALSE(first.wantsStars);
    ASSERT_TRUE(first.imageState.has_value());
    EXPECT_EQ(*first.imageState, "starnet-starless");
    const ExternalToolFiles files{.directory = QStringLiteral("d"),
                                  .input = QStringLiteral("d/in.tiff"),
                                  .image = QStringLiteral("d/out.tiff"),
                                  .mask = QStringLiteral("d/mask.tiff"),
                                  .stars = QStringLiteral("d/stars.tiff")};
    const QStringList arguments = first.arguments(files, true);
    EXPECT_TRUE(arguments.contains(QStringLiteral("--input")) && arguments.contains(files.input));
    EXPECT_TRUE(arguments.contains(QStringLiteral("--output")) && arguments.contains(files.image));
    EXPECT_TRUE(arguments.contains(QStringLiteral("--mask")) && arguments.contains(files.mask));
    EXPECT_FALSE(arguments.contains(QStringLiteral("--unscreen")));
    EXPECT_TRUE(arguments.contains(QStringLiteral("--stride")) && arguments.contains(QStringLiteral("384")));
    EXPECT_TRUE(arguments.contains(QStringLiteral("--upsample")));
    EXPECT_TRUE(arguments.contains(QStringLiteral("--disable-highlights-protection")));
    EXPECT_TRUE(arguments.contains(QStringLiteral("--weights")) && arguments.contains(starnet.weightsPath));
    EXPECT_TRUE(arguments.contains(QStringLiteral("--machine-progress")));
    EXPECT_FALSE(first.arguments(files, false).contains(QStringLiteral("--machine-progress")));

    starnet.starOutputMode = QStringLiteral("unscreen");
    starnet.preStretch = false;
    const ExternalToolRequest second = StarnetProcess::toolRequest(starnet, QStringLiteral("starnet2.exe"));
    EXPECT_EQ(second.preStretch, an::ExternalToolPreStretch::None);
    EXPECT_FALSE(second.wantsMask);
    EXPECT_TRUE(second.wantsStars);
    EXPECT_TRUE(second.arguments(files, false).contains(QStringLiteral("--unscreen")));
    starnet.generateStarOutput = false;
    const ExternalToolRequest third = StarnetProcess::toolRequest(starnet, QStringLiteral("starnet2.exe"));
    EXPECT_FALSE(third.wantsMask);
    EXPECT_FALSE(third.wantsStars);

    StarnetStarMaskProcess::Settings mask;
    mask.preStretch = true;
    mask.stride = 2;
    const ExternalToolRequest fourth = StarnetStarMaskProcess::toolRequest(mask, QStringLiteral("starnet2.exe"));
    EXPECT_EQ(fourth.preStretch, an::ExternalToolPreStretch::Linked);
    EXPECT_FALSE(fourth.wantsImage);
    EXPECT_TRUE(fourth.wantsMask);
    EXPECT_FALSE(fourth.wantsStars);
    EXPECT_EQ(fourth.maskState, "starnet-star-mask");
    const QStringList maskArguments = fourth.arguments(files, false);
    EXPECT_TRUE(maskArguments.contains(QStringLiteral("--mask")));
    EXPECT_TRUE(maskArguments.contains(QStringLiteral("--stride")) && maskArguments.contains(QStringLiteral("2")));

    DeepSNRProcess::Settings deepSnr;
    deepSnr.stride = 256;
    deepSnr.preStretch = true;
    const ExternalToolRequest fifth = DeepSNRProcess::toolRequest(deepSnr, QStringLiteral("deepsnr.exe"));
    EXPECT_EQ(fifth.tool.name, QStringLiteral("DeepSNR"));
    EXPECT_EQ(fifth.tool.minimumWidth, 512u);
    EXPECT_EQ(fifth.tool.minimumHeight, 512u);
    EXPECT_EQ(fifth.preStretch, an::ExternalToolPreStretch::Unlinked);
    EXPECT_TRUE(fifth.wantsImage);
    EXPECT_FALSE(fifth.wantsMask);
    EXPECT_FALSE(fifth.imageState.has_value());
    const QStringList deepArguments = fifth.arguments(files, false);
    EXPECT_TRUE(deepArguments.contains(QStringLiteral("--input")) && deepArguments.contains(files.input));
    EXPECT_TRUE(deepArguments.contains(QStringLiteral("--output")) && deepArguments.contains(files.image));
    EXPECT_TRUE(deepArguments.contains(QStringLiteral("256")));
    EXPECT_FALSE(deepArguments.contains(QStringLiteral("--mask")));
    EXPECT_EQ(DeepSNRProcess::stepDescription(deepSnr), "Denoised with DeepSNR. Stride: 256. Pre-stretch: enabled");
}

// The mask comes back as the tool's own answer, narrowed to the source's representation and neither unstretched nor
// mapped: the stand-in answers the complement of the handed codes, so the mask is one minus the stretched, mapped
// image; the star layer comes back by the span alone, so the stand-in's half is half the mapped image's span.
TEST_F(ExternalToolsUnitsTest, TheMaskIsTheToolsOwnAnswerAndTheStarLayerComesBackByTheSpan)
{
    const ExternalTool& starnet = *toolNamed("Starnet");
    StandInTools standIn(context(), starnet);
    const std::optional<an::consumer::ImageAsset> source = sceneAsset(session(), "scene-rgb-float32-unbounded");
    ASSERT_TRUE(source.has_value());
    const std::optional<fixtures::FixtureSamples> before = samplesOf(*source);
    ASSERT_TRUE(before.has_value());
    double low = 0.0;
    double high = 1.0;
    for (std::uint32_t c = 0u; c < before->channelCount(); ++c)
    {
        for (std::uint32_t y = 0u; y < before->height(); ++y)
        {
            for (std::uint32_t x = 0u; x < before->width(); ++x)
            {
                low = (std::min)(low, fractionOf(*before, x, y, c));
                high = (std::max)(high, fractionOf(*before, x, y, c));
            }
        }
    }
    const an::Result<an::ui::processes::ExternalToolAnswers> answers =
        runSupport(session(), *source, starnetRequest(false, true));
    ASSERT_TRUE(answers.hasValue()) << answers.status().message();
    ASSERT_TRUE(answers->mask.has_value() && answers->stars.has_value());
    const std::optional<fixtures::FixtureSamples> mask = samplesOf(*answers->mask);
    const std::optional<fixtures::FixtureSamples> stars = samplesOf(*answers->stars);
    ASSERT_TRUE(mask.has_value() && stars.has_value());
    EXPECT_EQ(mask->sampleType(), fixtures::FixtureSampleType::Float32);
    EXPECT_EQ(stars->sampleType(), fixtures::FixtureSampleType::Float32);
    double maskError = 0.0;
    double starsError = 0.0;
    for (std::uint32_t c = 0u; c < before->channelCount(); ++c)
    {
        for (std::uint32_t y = 0u; y < before->height(); ++y)
        {
            for (std::uint32_t x = 0u; x < before->width(); ++x)
            {
                const double mapped = (fractionOf(*before, x, y, c) - low) / (high - low);
                maskError = (std::max)(maskError, std::fabs(fractionOf(*mask, x, y, c) - (1.0 - mapped)));
                starsError =
                    (std::max)(starsError, std::fabs(fractionOf(*stars, x, y, c) - 0.5 * mapped * (high - low)));
            }
        }
    }
    EXPECT_LE(maskError, 2.0 / 65535.0);
    EXPECT_LE(starsError, (high - low) / 65535.0);
}

// The answers' metadata: a StarNet answer is derived - the source's acquisition record, solution and lineage, the
// processing-state cards under the operation's names and no other card - and a DeepSNR answer keeps the source's
// metadata whole with every sample valid; each names the program and its arguments in its lineage.
TEST_F(ExternalToolsUnitsTest, TheAnswersCarryTheirMetadata)
{
    const ExternalTool& starnet = *toolNamed("Starnet");
    const std::optional<an::consumer::ImageAsset> plain = sceneAsset(session(), "scene-rgb-float32");
    ASSERT_TRUE(plain.has_value());
    const std::optional<an::consumer::ImageAsset> source = withMetadata(session(), *plain, describedMetadata());
    ASSERT_TRUE(source.has_value());
    {
        StandInTools standIn(context(), starnet);
        const an::Result<an::ui::processes::ExternalToolAnswers> answers =
            runSupport(session(), *source, starnetRequest(true, true));
        ASSERT_TRUE(answers.hasValue()) << answers.status().message();
        for (const auto& [asset, state] :
             {std::pair{&*answers->image, "starnet-starless"}, std::pair{&*answers->mask, "starnet-star-mask"},
              std::pair{&*answers->stars, "starnet-stars"}})
        {
            SCOPED_TRACE(state);
            const std::optional<an::ImageMetadata> metadata = assetMetadata(*asset);
            ASSERT_TRUE(metadata.has_value());
            EXPECT_EQ(metadata->acquisition().objectName(), "M 42");
            EXPECT_TRUE(metadata->astrometry().present());
            EXPECT_FALSE(hasCard(*metadata, "OBJECT"));
            const an::ProcessingStateRecord cards = an::readProcessingStateCards(metadata->codec());
            EXPECT_EQ(cards.operation, state);
            EXPECT_EQ(cards.linearity, an::RecordedLinearity::Nonlinear);
            EXPECT_EQ(cards.assignmentSource, an::StateAssignmentSource::Process);
            EXPECT_TRUE(lineageNames(*metadata, "StarNet2"));
            EXPECT_TRUE(lineageNames(*metadata, "--stride"));
            EXPECT_TRUE(lineageNames(*metadata, "pre-stretch unlinked"));
        }
    }
    {
        const ExternalTool& deepSnr = *toolNamed("DeepSnr");
        StandInTools standIn(context(), deepSnr);
        // A source whose rule calls only the finite samples valid: the answer, every pixel of which the program wrote,
        // calls every sample valid.
        an::ImageMetadata finiteOnly = describedMetadata();
        {
            an::ImageSemanticsRequest meaning = finiteOnly.semantics().toRequest();
            meaning.validity = *an::ValidityDescriptor::create({.rule = an::ValidityRule::FiniteOnly});
            an::Result<an::ImageSemantics> semantics = an::ImageSemantics::create(meaning);
            ASSERT_TRUE(semantics.hasValue());
            an::Result<an::ImageMetadata> restated = an::applyMetadataTransition(
                finiteOnly,
                an::MetadataTransition{.semantics = *semantics, .record = *an::ProvenanceEntry::create("finite", "")});
            ASSERT_TRUE(restated.hasValue());
            finiteOnly = *restated;
        }
        const std::optional<an::consumer::ImageAsset> finiteSource = withMetadata(session(), *plain, finiteOnly);
        ASSERT_TRUE(finiteSource.has_value());
        const an::Result<an::ui::processes::ExternalToolAnswers> answers =
            runSupport(session(), *finiteSource, deepSnrRequest(false));
        ASSERT_TRUE(answers.hasValue()) << answers.status().message();
        const std::optional<an::ImageMetadata> metadata = assetMetadata(*answers->image);
        ASSERT_TRUE(metadata.has_value());
        EXPECT_EQ(metadata->acquisition().objectName(), "M 42");
        EXPECT_TRUE(metadata->astrometry().present());
        EXPECT_TRUE(hasCard(*metadata, "OBJECT"));
        EXPECT_EQ(metadata->semantics().transferFunction(), an::TransferFunction::Linear);
        EXPECT_EQ(metadata->semantics().validity().rule(), an::ValidityRule::AllValid);
        EXPECT_TRUE(lineageNames(*metadata, "DeepSNR"));
        EXPECT_TRUE(lineageNames(*metadata, "pre-stretch none"));
    }
}

// An image described in its own units - a physical quantity - is handed as a picture and comes back described as it
// was, in its units: the stand-in changes nothing, so the answer is the scene within one code's share of its range.
TEST_F(ExternalToolsUnitsTest, AnImageInItsOwnUnitsComesBackInThem)
{
    const ExternalTool& deepSnr = *toolNamed("DeepSnr");
    StandInTools standIn(context(), deepSnr);
    const std::optional<an::consumer::ImageAsset> source = sceneAsset(session(), "gradient-mono-float64-physical");
    ASSERT_TRUE(source.has_value());
    const std::optional<an::ImageDescriptor> described = source->descriptor();
    ASSERT_TRUE(described.has_value());
    ASSERT_NE(described->numericDomain(), an::NumericDomain::NormalizedLinear);
    const std::optional<fixtures::FixtureSamples> before = samplesOf(*source);
    ASSERT_TRUE(before.has_value());
    const an::Result<an::ui::processes::ExternalToolAnswers> answers =
        runSupport(session(), *source, deepSnrRequest(false));
    ASSERT_TRUE(answers.hasValue()) << answers.status().message();
    const std::optional<an::ImageDescriptor> back = answers->image->descriptor();
    ASSERT_TRUE(back.has_value());
    EXPECT_EQ(back->numericDomain(), described->numericDomain());
    EXPECT_EQ(back->sampleType(), described->sampleType());
    const std::optional<fixtures::FixtureSamples> after = samplesOf(*answers->image);
    ASSERT_TRUE(after.has_value());
    double low = 0.0;
    double high = 1.0;
    for (std::uint32_t y = 0u; y < before->height(); ++y)
    {
        for (std::uint32_t x = 0u; x < before->width(); ++x)
        {
            low = (std::min)(low, before->value(x, y, 0u));
            high = (std::max)(high, before->value(x, y, 0u));
        }
    }
    EXPECT_LE(largestFractionDifference(*before, *after), (high - low) / 65535.0 + 1.0e-9);
}

// A pixel the source's validity rejects is handed as zero and counted out of the stretch: the same scene with its
// corner masked out is handed as zero there and stretched from the rest.
TEST_F(ExternalToolsUnitsTest, ARejectedPixelIsHandedAsZero)
{
    const ExternalTool& starnet = *toolNamed("Starnet");
    StandInTools standIn(context(), starnet);
    const std::optional<an::consumer::ImageAsset> plain = sceneAsset(session(), "stars-rgb-float32");
    ASSERT_TRUE(plain.has_value());
    an::ImageMetadata masked = describedMetadata();
    {
        an::ImageSemanticsRequest meaning = masked.semantics().toRequest();
        meaning.validity = *an::ValidityDescriptor::create({.rule = an::ValidityRule::ExplicitMask});
        an::Result<an::ImageSemantics> semantics = an::ImageSemantics::create(meaning);
        ASSERT_TRUE(semantics.hasValue());
        an::Result<an::ImageMetadata> restated = an::applyMetadataTransition(
            masked,
            an::MetadataTransition{.semantics = *semantics, .record = *an::ProvenanceEntry::create("mask", "")});
        ASSERT_TRUE(restated.hasValue());
        masked = *restated;
    }
    const std::optional<an::ImageDescriptor> described = plain->descriptor();
    ASSERT_TRUE(described.has_value());
    an::Result<an::consumer::SessionHold> hold = session().hold();
    ASSERT_TRUE(hold.hasValue());
    an::Result<an::ValidityMask> mask = an::ValidityMask::createAllValid(hold->engine(), described->extent());
    ASSERT_TRUE(mask.hasValue()) << mask.status().message();
    std::optional<an::ValidityMaskView<std::uint8_t>> bits = mask->view();
    ASSERT_TRUE(bits.has_value());
    for (std::uint32_t plane = 0u; plane < bits->planeCount(); ++plane)
    {
        for (std::uint32_t y = 0u; y < 2u; ++y)
        {
            for (std::uint32_t x = 0u; x < 2u; ++x)
            {
                EXPECT_TRUE(bits->setValid(plane, x, y, false));
            }
        }
    }
    bits.reset();
    std::optional<an::consumer::ImageAsset> source;
    {
        an::Result<an::consumer::AssetReadLease> lease = plain->read();
        ASSERT_TRUE(lease.hasValue());
        std::optional<an::consumer::OperationScope> scope = an::tests::viewer::admitted(session());
        ASSERT_TRUE(scope.has_value());
        std::optional<an::consumer::AnyImage> copy =
            lease->visit([&](const auto& typed) -> std::optional<an::consumer::AnyImage> {
                using SampleT = typename std::remove_cvref_t<decltype(typed)>::Sample;
                an::Result<an::Image<SampleT>> image =
                    an::Image<SampleT>::create(scope->engine(), *described, an::StorageArrangement::Planar,
                                               an::StorageInitialization::Zeroed, masked);
                if (!image.hasValue())
                {
                    return std::nullopt;
                }
                std::optional<an::ImageWriteAccess<SampleT>> access = image->write();
                const auto view = typed.view();
                an::ConversionCounters counters;
                (void)an::convertImageSamples(scope->execution(), an::kSampleTypeOf<SampleT>, view->data(),
                                              view->layout(), an::kSampleTypeOf<SampleT>, access->view().data(),
                                              access->view().layout(), an::ConversionPolicy{}, counters);
                access.reset();
                return an::consumer::AnyImage(std::move(*image));
            });
        lease->release();
        scope.reset();
        ASSERT_TRUE(copy.has_value());
        an::Result<an::consumer::ImageAsset> adopted =
            an::consumer::ImageAsset::adopt(session(), std::move(*copy), std::move(*mask));
        ASSERT_TRUE(adopted.hasValue()) << adopted.status().message();
        source = std::move(*adopted);
    }
    const an::Result<an::ui::processes::ExternalToolAnswers> answers =
        runSupport(session(), *source, starnetRequest(true));
    ASSERT_TRUE(answers.hasValue()) << answers.status().message();
    an::Result<adapter::DecodedFile> handed =
        adapter::decodeEngineFile(session(), std::filesystem::path(standIn.keptInputPath().toStdWString()));
    ASSERT_TRUE(handed.hasValue()) << handed.status().message();
    for (std::uint32_t c = 0u; c < handed->samples.channelCount(); ++c)
    {
        EXPECT_EQ(handed->samples.value(0u, 0u, c), 0.0);
        EXPECT_EQ(handed->samples.value(1u, 1u, c), 0.0);
        EXPECT_GT(handed->samples.value(5u, 5u, c), 0.0);
    }
}

// The star mask's shaping: a colour mask reduced to its lightness is one grey plane in the unit interval; selected
// between two limits it is one inside them and zero outside; smoothed it is no longer binary; a grey mask and a mask
// with no lightness asked are left as they are.
TEST_F(ExternalToolsUnitsTest, TheStarMaskIsShapedAsAsked)
{
    const ExternalTool& starnet = *toolNamed("Starnet");
    StandInTools standIn(context(), starnet);
    const std::optional<an::consumer::ImageAsset> source = sceneAsset(session(), "scene-rgb-uint16");
    ASSERT_TRUE(source.has_value());
    const auto maskOf = [&]() -> std::optional<an::consumer::ImageAsset> {
        an::Result<an::ui::processes::ExternalToolAnswers> answers =
            runSupport(session(), *source, starMaskRequest(false));
        EXPECT_TRUE(answers.hasValue()) << answers.status().message();
        if (!answers.hasValue())
        {
            return std::nullopt;
        }
        return std::move(answers->mask);
    };
    const auto shaped =
        [&](const an::ui::processes::StarMaskShaping& shaping) -> std::optional<fixtures::FixtureSamples> {
        std::optional<an::consumer::ImageAsset> mask = maskOf();
        if (!mask.has_value())
        {
            return std::nullopt;
        }
        an::Result<an::ui::ExecutionScope> scope = an::ui::ExecutionScope::admit(
            session(), an::ui::ExecutionRequest{.purpose = an::consumer::OperationPurpose::Apply}, {});
        EXPECT_TRUE(scope.hasValue());
        an::Result<an::consumer::ImageAsset> result =
            an::ui::processes::shapeStarMask(*scope, std::move(*mask), shaping);
        scope->finish(an::Status::success());
        EXPECT_TRUE(result.hasValue()) << result.status().message();
        if (!result.hasValue())
        {
            return std::nullopt;
        }
        return samplesOf(*result);
    };
    const std::optional<fixtures::FixtureSamples> untouched = shaped({});
    ASSERT_TRUE(untouched.has_value());
    EXPECT_EQ(untouched->channelCount(), 3u);
    EXPECT_EQ(untouched->sampleType(), fixtures::FixtureSampleType::UInt16);

    const std::optional<fixtures::FixtureSamples> lightness = shaped({.lightness = true});
    ASSERT_TRUE(lightness.has_value());
    EXPECT_EQ(lightness->channelCount(), 1u);
    EXPECT_EQ(lightness->sampleType(), fixtures::FixtureSampleType::UInt16);
    bool between = true;
    for (std::uint32_t y = 0u; y < lightness->height(); ++y)
    {
        for (std::uint32_t x = 0u; x < lightness->width(); ++x)
        {
            const double value = fractionOf(*lightness, x, y, 0u);
            between = between && value >= 0.0 && value <= 1.0;
        }
    }
    EXPECT_TRUE(between);

    // The limits are the lightness's own quartiles, so samples lie below the lower limit, between and above the upper.
    std::vector<double> lights;
    for (std::uint32_t y = 0u; y < lightness->height(); ++y)
    {
        for (std::uint32_t x = 0u; x < lightness->width(); ++x)
        {
            lights.push_back(fractionOf(*lightness, x, y, 0u));
        }
    }
    std::sort(lights.begin(), lights.end());
    const double lower = lights[lights.size() / 4u];
    const double upper = lights[(3u * lights.size()) / 4u];
    ASSERT_LT(lights.front(), lower);
    ASSERT_GT(lights.back(), upper);
    const std::optional<fixtures::FixtureSamples> binary =
        shaped({.lightness = true, .binary = true, .lowerLimit = lower, .upperLimit = upper, .smoothness = 0.0});
    ASSERT_TRUE(binary.has_value());
    int ones = 0;
    int below = 0;
    int above = 0;
    for (std::uint32_t y = 0u; y < binary->height(); ++y)
    {
        for (std::uint32_t x = 0u; x < binary->width(); ++x)
        {
            const double light = fractionOf(*lightness, x, y, 0u);
            const double value = fractionOf(*binary, x, y, 0u);
            const bool inside = light >= lower && light <= upper;
            // The lightness read back is narrowed to the mask's codes; a sample within one code of a limit may have
            // fallen on either side of it before narrowing, and is not judged.
            const bool nearALimit =
                std::fabs(light - lower) <= 1.0 / 65535.0 || std::fabs(light - upper) <= 1.0 / 65535.0;
            if (!nearALimit)
            {
                EXPECT_NEAR(value, inside ? 1.0 : 0.0, 1.0 / 65535.0) << x << "," << y;
            }
            ones += inside ? 1 : 0;
            below += light < lower - 1.0 / 65535.0 ? 1 : 0;
            above += light > upper + 1.0 / 65535.0 ? 1 : 0;
        }
    }
    EXPECT_GT(ones, 0);
    EXPECT_GT(below, 0);
    EXPECT_GT(above, 0);

    const std::optional<fixtures::FixtureSamples> smooth =
        shaped({.lightness = true, .binary = true, .lowerLimit = lower, .upperLimit = upper, .smoothness = 1.0});
    ASSERT_TRUE(smooth.has_value());
    int partial = 0;
    for (std::uint32_t y = 0u; y < smooth->height(); ++y)
    {
        for (std::uint32_t x = 0u; x < smooth->width(); ++x)
        {
            const double value = fractionOf(*smooth, x, y, 0u);
            partial += value > 0.05 && value < 0.95 ? 1 : 0;
        }
    }
    EXPECT_GT(partial, 0);

    // A smoothness wider than the stencil family's widest exact kernel - the dialog offers up to 100 - smooths the mask
    // by the exact Gaussian Gaussian Blur runs, on a mask far smaller than the kernel too: every sample comes near the
    // mean of the selection, which a blur keeps but for the mirrored edges' weight.
    double selected = 0.0;
    for (std::uint32_t y = 0u; y < binary->height(); ++y)
    {
        for (std::uint32_t x = 0u; x < binary->width(); ++x)
        {
            selected += fractionOf(*binary, x, y, 0u) >= 0.5 ? 1.0 : 0.0;
        }
    }
    selected /= static_cast<double>(binary->width()) * static_cast<double>(binary->height());
    for (const double wide : {40.0, 100.0})
    {
        SCOPED_TRACE(wide);
        const std::optional<fixtures::FixtureSamples> widely =
            shaped({.lightness = true, .binary = true, .lowerLimit = lower, .upperLimit = upper, .smoothness = wide});
        ASSERT_TRUE(widely.has_value());
        double least = 1.0;
        double most = 0.0;
        double total = 0.0;
        for (std::uint32_t y = 0u; y < widely->height(); ++y)
        {
            for (std::uint32_t x = 0u; x < widely->width(); ++x)
            {
                const double value = fractionOf(*widely, x, y, 0u);
                least = (std::min)(least, value);
                most = (std::max)(most, value);
                total += value;
            }
        }
        EXPECT_GT(least, 0.0);
        EXPECT_LT(most, 1.0);
        EXPECT_LT(most - least, 0.2) << "a smoothing far wider than the mask leaves it nearly flat";
        EXPECT_NEAR(total / (static_cast<double>(widely->width()) * static_cast<double>(widely->height())), selected,
                    0.1)
            << "the selection smoothed, not the lightness";
    }

    const std::optional<an::consumer::ImageAsset> grey = sceneAsset(session(), "scene-mono-float32");
    ASSERT_TRUE(grey.has_value());
    an::Result<an::ui::processes::ExternalToolAnswers> answers = runSupport(session(), *grey, starMaskRequest(false));
    ASSERT_TRUE(answers.hasValue()) << answers.status().message();
    an::Result<an::ui::ExecutionScope> scope = an::ui::ExecutionScope::admit(
        session(), an::ui::ExecutionRequest{.purpose = an::consumer::OperationPurpose::Apply}, {});
    ASSERT_TRUE(scope.hasValue());
    an::Result<an::consumer::ImageAsset> greyMask =
        an::ui::processes::shapeStarMask(*scope, std::move(*answers->mask), {.lightness = true, .binary = true});
    scope->finish(an::Status::success());
    ASSERT_TRUE(greyMask.hasValue()) << greyMask.status().message();
    const std::optional<fixtures::FixtureSamples> greySamples = samplesOf(*greyMask);
    ASSERT_TRUE(greySamples.has_value());
    EXPECT_EQ(greySamples->channelCount(), 1u);
    EXPECT_EQ(greySamples->sampleType(), fixtures::FixtureSampleType::Float32);
}

// A colour mask reduced to its lightness names the extraction in its lineage whatever the mask's representation and
// whether or not it is then selected between limits: an integer mask narrowed back and a floating mask kept as the
// lightness record the same step.
TEST_F(ExternalToolsUnitsTest, AShapedMaskRecordsItsLightnessWhateverItsRepresentation)
{
    const ExternalTool& starnet = *toolNamed("Starnet");
    StandInTools standIn(context(), starnet);
    for (const char* scene : {"scene-rgb-uint16", "scene-rgb-float32"})
    {
        const std::optional<an::consumer::ImageAsset> source = sceneAsset(session(), scene);
        ASSERT_TRUE(source.has_value()) << scene;
        for (const bool binary : {false, true})
        {
            SCOPED_TRACE(std::string(scene) + (binary ? " selected" : " lightness only"));
            an::Result<an::ui::processes::ExternalToolAnswers> answers =
                runSupport(session(), *source, starMaskRequest(false));
            ASSERT_TRUE(answers.hasValue()) << answers.status().message();
            ASSERT_TRUE(answers->mask.has_value());
            const std::optional<an::ImageDescriptor> maskDescribed = answers->mask->descriptor();
            ASSERT_TRUE(maskDescribed.has_value());
            an::Result<an::ui::ExecutionScope> scope = an::ui::ExecutionScope::admit(
                session(), an::ui::ExecutionRequest{.purpose = an::consumer::OperationPurpose::Apply}, {});
            ASSERT_TRUE(scope.hasValue());
            an::Result<an::consumer::ImageAsset> shaped = an::ui::processes::shapeStarMask(
                *scope, std::move(*answers->mask),
                {.lightness = true, .binary = binary, .lowerLimit = 0.1, .upperLimit = 0.9, .smoothness = 0.0});
            scope->finish(an::Status::success());
            ASSERT_TRUE(shaped.hasValue()) << shaped.status().message();
            const std::optional<an::ImageDescriptor> described = shaped->descriptor();
            ASSERT_TRUE(described.has_value());
            EXPECT_EQ(described->extent().channelCount(), 1u);
            EXPECT_EQ(described->sampleType(), maskDescribed->sampleType());
            const std::optional<an::ImageMetadata> metadata = assetMetadata(*shaped);
            ASSERT_TRUE(metadata.has_value());
            EXPECT_TRUE(lineageNames(*metadata, "extractLightness"));
            EXPECT_EQ(lineageNames(*metadata, "rangeSelectImage"), binary);
        }
    }
}

// What a run refuses before anything is written: a mosaic, an image of two or four channels, one below the tool's
// smallest size, a program the product does not support, and a request that spells no arguments; nothing was run.
TEST_F(ExternalToolsUnitsTest, ARunIsRefusedBeforeAnythingIsWritten)
{
    const ExternalTool& starnet = *toolNamed("Starnet");
    StandInTools standIn(context(), starnet);
    const std::optional<an::consumer::ImageAsset> mosaic = sceneAsset(session(), "mosaic-rggb-uint16");
    ASSERT_TRUE(mosaic.has_value());
    an::Result<an::ui::processes::ExternalToolAnswers> refused = runSupport(session(), *mosaic, starnetRequest(false));
    ASSERT_FALSE(refused.hasValue());
    EXPECT_EQ(refused.status().code(), an::ErrorCode::Unsupported);
    EXPECT_NE(std::string(refused.status().message()).find("demosaic"), std::string::npos);

    const std::optional<an::consumer::ImageAsset> rgba =
        madeAsset(session(), 8u, 6u, 4u, an::SampleType::Float32,
                  [](std::uint32_t x, std::uint32_t, std::uint32_t) { return x / 8.0; });
    ASSERT_TRUE(rgba.has_value());
    refused = runSupport(session(), *rgba, starnetRequest(false));
    ASSERT_FALSE(refused.hasValue());
    EXPECT_EQ(refused.status().code(), an::ErrorCode::Unsupported);
    EXPECT_NE(std::string(refused.status().message()).find("grayscale and RGB"), std::string::npos);

    const std::optional<an::consumer::ImageAsset> small = sceneAsset(session(), "scene-rgb-float32");
    ASSERT_TRUE(small.has_value());
    ExternalToolRequest sized = starnetRequest(false);
    sized.tool.minimumWidth = 512u;
    sized.tool.minimumHeight = 512u;
    refused = runSupport(session(), *small, sized);
    ASSERT_FALSE(refused.hasValue());
    EXPECT_EQ(refused.status().code(), an::ErrorCode::Unsupported);
    EXPECT_NE(std::string(refused.status().message()).find("512x512"), std::string::npos);

    ExternalToolRequest missing = starnetRequest(false);
    missing.tool.executablePath = QStringLiteral("Z:/nowhere/starnet2.exe");
    refused = runSupport(session(), *small, missing);
    ASSERT_FALSE(refused.hasValue());
    EXPECT_EQ(refused.status().code(), an::ErrorCode::ExternalFailure);
    EXPECT_NE(std::string(refused.status().message()).find("not found"), std::string::npos);

    ExternalToolRequest unspelled = starnetRequest(false);
    unspelled.arguments = {};
    refused = runSupport(session(), *small, unspelled);
    ASSERT_FALSE(refused.hasValue());
    EXPECT_EQ(refused.status().code(), an::ErrorCode::InvalidArgument);

    EXPECT_TRUE(standIn.recordedRuns().isEmpty()) << "nothing was run";
}

// Every way the program itself can fail is a failure by name: an exit code with what it printed, a crash, an answer it
// did not write, and an answer of another size; a program that reports machine-readable progress is read on its error
// stream.
TEST_F(ExternalToolsUnitsTest, EveryWayTheProgramCanFailIsSaid)
{
    const ExternalTool& starnet = *toolNamed("Starnet");
    StandInTools standIn(context(), starnet);
    const std::optional<an::consumer::ImageAsset> source = sceneAsset(session(), "scene-rgb-float32");
    ASSERT_TRUE(source.has_value());

    standIn.setBehaviour("exit-code");
    an::Result<an::ui::processes::ExternalToolAnswers> failed = runSupport(session(), *source, starnetRequest(false));
    ASSERT_FALSE(failed.hasValue());
    EXPECT_EQ(failed.status().code(), an::ErrorCode::ExternalFailure);
    EXPECT_NE(std::string(failed.status().message()).find("StarNet2 exited with code 3"), std::string::npos);
    EXPECT_NE(std::string(failed.status().message()).find("bad input"), std::string::npos);

    standIn.setBehaviour("crash");
    failed = runSupport(session(), *source, starnetRequest(false));
    ASSERT_FALSE(failed.hasValue());
    EXPECT_EQ(failed.status().code(), an::ErrorCode::ExternalFailure);
    EXPECT_NE(std::string(failed.status().message()).find("crashed"), std::string::npos);

    standIn.setBehaviour("no-output");
    failed = runSupport(session(), *source, starnetRequest(false));
    ASSERT_FALSE(failed.hasValue());
    EXPECT_EQ(failed.status().code(), an::ErrorCode::ExternalFailure);
    EXPECT_NE(std::string(failed.status().message()).find("wrote no image"), std::string::npos);

    standIn.setBehaviour("wrong-size");
    failed = runSupport(session(), *source, starnetRequest(false));
    ASSERT_FALSE(failed.hasValue());
    EXPECT_EQ(failed.status().code(), an::ErrorCode::IncompatibleArguments);
    EXPECT_NE(std::string(failed.status().message()).find("6x8"), std::string::npos);

    standIn.setBehaviour("machine");
    const an::Result<an::ui::processes::ExternalToolAnswers> machine =
        runSupport(session(), *source, starnetRequest(false));
    ASSERT_TRUE(machine.hasValue()) << machine.status().message();
    EXPECT_TRUE(machine->machineProgress);
    EXPECT_TRUE(machine->arguments.contains(QStringLiteral("--machine-progress")));

    standIn.setBehaviour("identity");
    const an::Result<an::ui::processes::ExternalToolAnswers> printed =
        runSupport(session(), *source, starnetRequest(false));
    ASSERT_TRUE(printed.hasValue()) << printed.status().message();
    EXPECT_FALSE(printed->machineProgress);
    EXPECT_FALSE(printed->arguments.contains(QStringLiteral("--machine-progress")));
}

// A stop kills the program and answers Cancelled, nothing published: one asked before the run starts, and one asked
// while the program is running, which ends it within the read wait rather than when it would have finished.
TEST_F(ExternalToolsUnitsTest, AStopKillsTheProgram)
{
    const ExternalTool& starnet = *toolNamed("Starnet");
    StandInTools standIn(context(), starnet, "slow");
    qputenv(kStandInSlowMsVariable, QByteArray("20000"));
    const std::optional<an::consumer::ImageAsset> source = sceneAsset(session(), "scene-rgb-float32");
    ASSERT_TRUE(source.has_value());

    an::Result<an::ui::processes::ExternalToolAnswers> stopped =
        runSupport(session(), *source, starnetRequest(false), true);
    ASSERT_FALSE(stopped.hasValue());
    EXPECT_EQ(stopped.status().code(), an::ErrorCode::Cancelled);
    EXPECT_FALSE(QFileInfo::exists(standIn.keptInputPath())) << "nothing was handed to the program";
    EXPECT_TRUE(standIn.recordedRuns().isEmpty()) << "the program never ran";

    an::Result<an::consumer::AssetReadLease> lease = source->read();
    ASSERT_TRUE(lease.hasValue());
    an::Result<an::ui::ExecutionScope> scope = an::ui::ExecutionScope::admit(
        session(), an::ui::ExecutionRequest{.purpose = an::consumer::OperationPurpose::Apply}, {});
    ASSERT_TRUE(scope.hasValue());
    std::thread stopper([&scope]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(1500));
        scope->requestStop();
    });
    const auto started = std::chrono::steady_clock::now();
    stopped = an::ui::processes::runExternalTool(*scope, an::ui::ProcessSource::of(*lease), starnetRequest(false));
    const auto elapsed = std::chrono::steady_clock::now() - started;
    stopper.join();
    scope->finish(stopped.hasValue() ? an::Status::success() : stopped.status());
    ASSERT_FALSE(stopped.hasValue());
    EXPECT_EQ(stopped.status().code(), an::ErrorCode::Cancelled);
    EXPECT_LT(elapsed, std::chrono::seconds(12));
}

// The program's progress reaches the operation's progress sink from ten to ninety percent, printed or machine-readable,
// never backwards, with the run's own stages before and after; the reports are coalesced on their way to the GUI
// thread, so the run goes on a worker while the case turns the event loop, as a window's run does.
TEST_F(ExternalToolsUnitsTest, TheProgramsProgressReachesTheOperation)
{
    const ExternalTool& starnet = *toolNamed("Starnet");
    for (const char* behaviour : {"slow", "machine-slow"})
    {
        SCOPED_TRACE(behaviour);
        StandInTools standIn(context(), starnet, behaviour);
        qputenv(kStandInSlowMsVariable, QByteArray("1600"));
        const std::optional<an::consumer::ImageAsset> source = sceneAsset(session(), "scene-rgb-float32");
        ASSERT_TRUE(source.has_value());
        std::vector<an::ui::ExecutionProgress> reports;
        an::ui::ExecutionObserver observer;
        observer.receiver = QCoreApplication::instance();
        observer.progress = [&reports](const an::ui::ExecutionProgress& progress) { reports.push_back(progress); };
        an::Result<an::ui::ExecutionScope> scope = an::ui::ExecutionScope::admit(
            session(), an::ui::ExecutionRequest{.purpose = an::consumer::OperationPurpose::Apply}, observer);
        ASSERT_TRUE(scope.hasValue());
        std::atomic_bool finished{false};
        an::Result<an::ui::processes::ExternalToolAnswers> answers =
            an::Status::failure(an::ErrorCode::Internal, "not run");
        std::thread worker([&]() {
            an::Result<an::consumer::AssetReadLease> lease = source->read();
            if (lease.hasValue())
            {
                answers = an::ui::processes::runExternalTool(*scope, an::ui::ProcessSource::of(*lease),
                                                             starnetRequest(false));
            }
            finished = true;
        });
        ASSERT_TRUE(pumpUntil([&finished]() { return finished.load(); }));
        worker.join();
        scope->finish(answers.hasValue() ? an::Status::success() : answers.status());
        ASSERT_TRUE(answers.hasValue()) << answers.status().message();
        ASSERT_TRUE(pumpUntil([&reports]() {
            return !reports.empty() && reports.back().total != 0u && reports.back().completed == reports.back().total;
        }));
        // The run's own reports are percentages on a scale above any row count a codec reports through the same
        // execution, so every report that reached the sink is the run's and none moves backwards.
        std::uint64_t last = 0u;
        bool sawToolShare = false;
        bool sawPreparing = false;
        std::optional<double> firstExecuting;
        for (const an::ui::ExecutionProgress& report : reports)
        {
            EXPECT_GE(report.completed, last);
            last = report.completed;
            EXPECT_EQ(report.total, 100u * an::ui::processes::kExternalToolProgressUnitsPerPercent);
            const double fraction = static_cast<double>(report.completed) / static_cast<double>(report.total);
            sawToolShare = sawToolShare || (fraction > 0.1 && fraction < 0.9);
            sawPreparing = sawPreparing || report.stage == "Preparing StarNet2 execution...";
            if (report.stage == "Executing StarNet2..." && !firstExecuting.has_value())
            {
                firstExecuting = fraction;
            }
            if (fraction > 0.1 && fraction < 0.9)
            {
                // The program's own reports keep the executing stage's words.
                EXPECT_EQ(report.stage, "Executing StarNet2...");
            }
        }
        // The program's share begins at ten percent, where the executing stage is first reported.
        ASSERT_TRUE(firstExecuting.has_value());
        EXPECT_NEAR(*firstExecuting, an::ui::processes::kExternalToolProgressStart / 100.0, 1.0e-9);
        EXPECT_TRUE(sawToolShare) << "the tool's own progress moved the bar";
        EXPECT_TRUE(sawPreparing) << "the run's first stage was reported";
        EXPECT_EQ(reports.back().completed, reports.back().total);
    }
}

// The program is probed as the product's settings page probes it: a stand-in that answers machine information
// reports machine-readable progress, one that answers only its version does not, and a missing or unknown tool is
// said.
TEST_F(ExternalToolsUnitsTest, TheProgramIsProbedAsThePageProbesIt)
{
    const ExternalTool& starnet = *toolNamed("Starnet");
    StandInTools standIn(context(), starnet, "machine");
    an::ui::processes::ExternalToolProfile profile;
    profile.name = QStringLiteral("StarNet2");
    profile.executablePath = StandInTools::executablePath();
    an::ui::processes::ExternalToolCheck check = an::ui::processes::checkExternalTool(profile);
    EXPECT_TRUE(check.supported) << check.detail.toStdString();
    EXPECT_TRUE(check.machineProgress);
    EXPECT_EQ(check.version, QStringLiteral("2.5.3"));

    standIn.setBehaviour("identity");
    check = an::ui::processes::checkExternalTool(profile);
    EXPECT_TRUE(check.supported) << check.detail.toStdString();
    EXPECT_FALSE(check.machineProgress);

    profile.name = QStringLiteral("Nobody");
    check = an::ui::processes::checkExternalTool(profile);
    EXPECT_FALSE(check.supported);
    EXPECT_NE(check.detail.indexOf(QStringLiteral("Nobody")), -1);

    profile.name = QStringLiteral("DeepSNR");
    profile.executablePath = QString();
    check = an::ui::processes::checkExternalTool(profile);
    EXPECT_FALSE(check.supported);
    EXPECT_NE(check.detail.indexOf(QStringLiteral("empty")), -1);
}
