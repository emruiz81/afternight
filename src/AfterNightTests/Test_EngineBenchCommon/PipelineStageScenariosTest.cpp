// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Tests for the pipeline stage rows: their shared declaration, the exact-average check that scores
// them from the frames' files, and the helper that puts those files into the row's storage state.

#include "FileCache.h"
#include "FitsAverageOracle.h"
#include "PipelineScratch.h"
#include "PipelineStageFixture.h"
#include "Results.h"
#include "Scenario.h"

#include <gtest/gtest.h>

#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <vector>

using namespace an::bench;

namespace
{

/// A directory under the system temporary directory, removed however the test ends.
class TemporaryDirectory
{
public:
    TemporaryDirectory()
    {
        const auto* test = ::testing::UnitTest::GetInstance()->current_test_info();
        m_path = std::filesystem::temp_directory_path() /
                 (std::string("afternight-stage-test-") + test->test_suite_name() + "-" + test->name());
        std::filesystem::remove_all(m_path);
        std::filesystem::create_directories(m_path);
    }
    ~TemporaryDirectory()
    {
        std::error_code ignored;
        std::filesystem::remove_all(m_path, ignored);
    }
    TemporaryDirectory(const TemporaryDirectory&) = delete;
    TemporaryDirectory& operator=(const TemporaryDirectory&) = delete;

    [[nodiscard]] std::string file(const std::string& name) const
    {
        return (m_path / name).string();
    }

private:
    std::filesystem::path m_path;
};

/// One 80-byte header card.
std::string card(const std::string& keyword, const std::string& value)
{
    std::string text = keyword;
    text.resize(8u, ' ');
    if (!value.empty())
    {
        text += "= ";
        std::string field = value;
        if (field.size() < 20u)
        {
            field.insert(0u, 20u - field.size(), ' ');
        }
        text += field;
    }
    text.resize(80u, ' ');
    return text;
}

/// Writes a primary-image FITS file of stored integers, with extra cards before END.
void writeFits(const std::string& path, int bitpix, const std::string& zero, std::uint32_t width, std::uint32_t height,
               const std::vector<std::int64_t>& stored, const std::vector<std::string>& extra = {})
{
    std::string header = card("SIMPLE", "T") + card("BITPIX", std::to_string(bitpix)) + card("NAXIS", "2") +
                         card("NAXIS1", std::to_string(width)) + card("NAXIS2", std::to_string(height));
    if (!zero.empty())
    {
        header += card("BZERO", zero) + card("BSCALE", "1");
    }
    for (const std::string& line : extra)
    {
        header += line;
    }
    header += card("END", "");
    header.resize((header.size() + 2879u) / 2880u * 2880u, ' ');
    std::string data;
    const int bytes = std::abs(bitpix) / 8;
    for (const std::int64_t value : stored)
    {
        for (int shift = (bytes - 1) * 8; shift >= 0; shift -= 8)
        {
            data.push_back(static_cast<char>((static_cast<std::uint64_t>(value) >> shift) & 0xffu));
        }
    }
    data.resize((data.size() + 2879u) / 2880u * 2880u, '\0');
    std::ofstream out(path, std::ios::binary);
    out << header << data;
}

/// Writes unsigned 16-bit codes the way a camera does: signed storage with a zero point of 32768.
void writeCodes(const std::string& path, std::uint32_t width, std::uint32_t height,
                const std::vector<std::int64_t>& codes)
{
    std::vector<std::int64_t> stored;
    stored.reserve(codes.size());
    for (const std::int64_t code : codes)
    {
        stored.push_back(code - 32768);
    }
    writeFits(path, 16, "32768", width, height, stored);
}

/// Three 5x3 frames of codes: element 4 is zero in every frame, element 0 in two of them.
struct ThreeFrames
{
    std::vector<std::string> paths;
    std::vector<float> exact;
};

ThreeFrames threeFrames(const TemporaryDirectory& directory)
{
    const std::vector<std::vector<std::int64_t>> frames = {
        {0, 1000, 65535, 7, 0, 2, 3, 40000, 5, 1, 1, 1, 30000, 12345, 1},
        {9, 1001, 65535, 8, 0, 2, 4, 40001, 5, 2, 1, 2, 30001, 12346, 65535},
        {0, 1003, 65534, 9, 0, 2, 5, 40003, 6, 3, 1, 4, 30002, 12348, 1}};
    ThreeFrames out;
    for (std::size_t frame = 0u; frame < frames.size(); ++frame)
    {
        out.paths.push_back(directory.file("frame" + std::to_string(frame) + ".fits"));
        writeCodes(out.paths.back(), 5u, 3u, frames[frame]);
    }
    for (std::size_t element = 0u; element < 15u; ++element)
    {
        std::int64_t sum = 0;
        int count = 0;
        for (const auto& frame : frames)
        {
            if (frame[element] != 0)
            {
                sum += frame[element];
                ++count;
            }
        }
        out.exact.push_back(count == 0 ? 0.0f : static_cast<float>(static_cast<double>(sum) / count));
    }
    return out;
}

} // namespace

TEST(PipelineStageScenariosTest, DeclaresTheMasterBiasInBothStorageStates)
{
    const std::vector<ScenarioSpec> all = pipelineStageScenarios();
    ASSERT_EQ(all.size(), 4u);
    const std::vector<ScenarioSpec> rows(all.begin(), all.begin() + 2);
    EXPECT_EQ(rows[0].id, kMasterBiasColdScenarioId);
    EXPECT_EQ(rows[1].id, kMasterBiasHotScenarioId);
    for (const ScenarioSpec& row : rows)
    {
        EXPECT_TRUE(row.validate().empty()) << row.id;
        EXPECT_EQ(row.group, "stages");
        EXPECT_EQ(row.inputSource, InputSource::CorpusFrameSet);
        EXPECT_EQ(row.corpusId, kPipelineFrameSetId);
        EXPECT_EQ(row.input.width, kPipelineFrameWidth);
        EXPECT_EQ(row.input.height, kPipelineFrameHeight);
        EXPECT_EQ(row.input.sampleType, SampleType::UInt16);
        EXPECT_EQ(row.correctnessClass, CorrectnessClass::AbsoluteRelative);
        EXPECT_DOUBLE_EQ(row.tolerance, kMasterBiasRelativeTolerance);
        EXPECT_TRUE(isMasterBiasStageScenario(row));
        EXPECT_FALSE(isFinalStackStageScenario(row));
        EXPECT_TRUE(isPipelineStageScenario(row));
        EXPECT_EQ(pipelineRoleLimit(row, "bias", 99u), kPipelineBiasCount)
            << "the stage stacks the pipeline's bias frames and nothing more";
        EXPECT_EQ(std::string(stageFrameRole(row)), "bias");
        EXPECT_EQ(stageFrameCount(row), kPipelineBiasCount);
        EXPECT_EQ(timedBatchIterations(row).value_or(0u), 1u) << "a frame set is never batched";
    }
    EXPECT_EQ(stageCacheCondition(rows[0]), StageCacheCondition::Cold);
    EXPECT_EQ(stageCacheCondition(rows[1]), StageCacheCondition::Hot);
    EXPECT_NE(rows[0].parameters, rows[1].parameters) << "the storage state is part of the row's identity";
    EXPECT_FALSE(isMasterBiasStageScenario(pipelineScenario()));
}

TEST(PipelineStageScenariosTest, DeclaresTheFinalStackOverTheAlignedLightsInBothStorageStates)
{
    // The last stage stacks the aligned lights, which only a pipeline run produces, so its rows read a
    // frame set of their own: the pipeline's 72 lights as Float32 after alignment.
    const std::vector<ScenarioSpec> all = pipelineStageScenarios();
    ASSERT_EQ(all.size(), 4u);
    const std::vector<ScenarioSpec> rows(all.begin() + 2, all.end());
    EXPECT_EQ(rows[0].id, kFinalStackColdScenarioId);
    EXPECT_EQ(rows[1].id, kFinalStackHotScenarioId);
    for (const ScenarioSpec& row : rows)
    {
        EXPECT_TRUE(row.validate().empty()) << row.id;
        EXPECT_EQ(row.group, "stages");
        EXPECT_EQ(row.inputSource, InputSource::CorpusFrameSet);
        EXPECT_EQ(row.corpusId, kAlignedFrameSetId);
        EXPECT_NE(row.corpusId, kPipelineFrameSetId);
        EXPECT_EQ(row.input.width, kPipelineFrameWidth);
        EXPECT_EQ(row.input.height, kPipelineFrameHeight);
        EXPECT_EQ(row.input.sampleType, SampleType::Float32);
        EXPECT_EQ(row.correctnessClass, CorrectnessClass::AbsoluteRelative);
        EXPECT_DOUBLE_EQ(row.tolerance, kFinalStackRelativeTolerance);
        EXPECT_TRUE(isFinalStackStageScenario(row));
        EXPECT_FALSE(isMasterBiasStageScenario(row));
        EXPECT_TRUE(isPipelineStageScenario(row));
        EXPECT_EQ(std::string(stageFrameRole(row)), "lights");
        EXPECT_EQ(stageFrameCount(row), kPipelineLightCount);
        EXPECT_EQ(timedBatchIterations(row).value_or(0u), 1u) << "a frame set is never batched";
    }
    EXPECT_EQ(stageCacheCondition(rows[0]), StageCacheCondition::Cold);
    EXPECT_EQ(stageCacheCondition(rows[1]), StageCacheCondition::Hot);
    EXPECT_NE(rows[0].parameters, rows[1].parameters);
    EXPECT_FALSE(isPipelineStageScenario(pipelineScenario()));
    ScenarioSpec other = rows[0];
    other.group = "stacking";
    EXPECT_FALSE(isFinalStackStageScenario(other));
    EXPECT_FALSE(isPipelineStageScenario(other));
}

TEST(PipelineStageScenariosTest, DeclaresTheFinalStackOverXisfCopiesOutsideTheStageRows)
{
    // The final stack over the aligned lights rewritten as XISF reads the final stack's own frame set, read through
    // the cache, and is a row of its own: the stage rows do not include it.
    const std::vector<ScenarioSpec> rows = xisfStackStreamScenarios();
    ASSERT_EQ(rows.size(), 3u);
    // The third stacks the uncompressed copies again and writes its master into a reserved image as it is combined.
    EXPECT_EQ(rows[2].id, kXisfReservedMasterScenarioId);
    EXPECT_EQ(rows[2].parameters.at("compression"), "None");
    EXPECT_EQ(rows[2].parameters.at("output"), "reserved");
    EXPECT_TRUE(writesReservedMaster(rows[2]));
    EXPECT_FALSE(writesReservedMaster(rows[0]));
    EXPECT_FALSE(writesReservedMaster(rows[1]));
    EXPECT_NE(rows[0].parameters, rows[2].parameters) << "the output is part of the row's identity";
    EXPECT_TRUE(isXisfStackStreamScenario(rows[2]));
    EXPECT_TRUE(rows[2].validate().empty());
    EXPECT_EQ(rows[1].id, kXisfZstdStackStreamScenarioId);
    EXPECT_EQ(rows[1].parameters.at("compression"), "Zstd");
    EXPECT_NE(rows[0].parameters, rows[1].parameters) << "the compression is part of the row's identity";
    EXPECT_TRUE(isXisfStackStreamScenario(rows[1]));
    EXPECT_TRUE(rows[1].validate().empty());
    const ScenarioSpec& row = rows.front();
    EXPECT_EQ(row.id, kXisfStackStreamScenarioId);
    EXPECT_TRUE(row.validate().empty()) << row.id;
    EXPECT_EQ(row.group, "pipeline");
    EXPECT_EQ(row.inputSource, InputSource::CorpusFrameSet);
    EXPECT_EQ(row.corpusId, kAlignedFrameSetId);
    EXPECT_EQ(row.input.sampleType, SampleType::Float32);
    EXPECT_EQ(row.parameters.at("container"), "XISF");
    EXPECT_EQ(row.parameters.at("compression"), "None");
    EXPECT_EQ(stageCacheCondition(row), StageCacheCondition::Hot);
    EXPECT_EQ(pipelineRoleLimit(row, "lights", 99u), kPipelineLightCount);
    EXPECT_EQ(row.correctnessClass, CorrectnessClass::AbsoluteRelative);
    EXPECT_DOUBLE_EQ(row.tolerance, kFinalStackRelativeTolerance);
    EXPECT_TRUE(isXisfStackStreamScenario(row));
    EXPECT_FALSE(isPipelineStageScenario(row));
    EXPECT_FALSE(row.id.starts_with("stages."));
    for (const ScenarioSpec& stage : pipelineStageScenarios())
        EXPECT_FALSE(isXisfStackStreamScenario(stage)) << stage.id;
}

TEST(PipelineStageScenariosTest, APipelineStackTileStaysInTheCacheWithARowForEveryWorker)
{
    // Every frame's band of a tile is written by its read and read back by the fold once the last
    // frame's band has landed, so the tile is sized to stay in the last-level cache between the two,
    // with at least one row for each worker the fold spreads its rows over.
    constexpr std::uint32_t kFloat = 4u;
    const std::uint64_t alignedRow = std::uint64_t{kPipelineLightCount} * kPipelineFrameWidth * kFloat;
    EXPECT_EQ(pipelineStackTileRows(kPipelineLightCount, kPipelineFrameWidth, kFloat, 1u), 7u);
    EXPECT_EQ(pipelineStackTileRows(kPipelineLightCount, kPipelineFrameWidth, kFloat, 4u), 7u);
    EXPECT_EQ(pipelineStackTileRows(kPipelineLightCount, kPipelineFrameWidth, kFloat, 16u), 16u)
        << "one row per worker, over the cache target";
    EXPECT_LE(7u * alignedRow, kPipelineStackTileBytes);
    EXPECT_GT(8u * alignedRow, kPipelineStackTileBytes);
    // A tile holds Float32 samples at least, whatever the frames store.
    EXPECT_EQ(pipelineStackTileRows(kPipelineBiasCount, kPipelineFrameWidth, 2u, 1u),
              pipelineStackTileRows(kPipelineBiasCount, kPipelineFrameWidth, kFloat, 1u));
    EXPECT_EQ(pipelineStackTileRows(kPipelineBiasCount, kPipelineFrameWidth, 2u, 1u), 8u);
    EXPECT_EQ(pipelineStackTileRows(kPipelineBiasCount, kPipelineFrameWidth, 8u, 1u), 4u);
    // Never no row, however wide a row of every frame is, and never more than the cap.
    EXPECT_EQ(pipelineStackTileRows(1000u, 100000u, kFloat, 1u), 1u);
    EXPECT_EQ(pipelineStackTileRows(1u, 16u, kFloat, 1u), kPipelineStackMostTileRows);
    EXPECT_EQ(pipelineStackTileRows(0u, kPipelineFrameWidth, kFloat, 1u), kPipelineStackMostTileRows);
    EXPECT_EQ(pipelineStackTileRows(kPipelineLightCount, kPipelineFrameWidth, kFloat, 0u), 7u)
        << "no admitted width is read as one worker";
}

TEST(PipelineStageScenariosTest, AStackThatCalibratesKeepsItsBandBudgetTile)
{
    // A stack that calibrates reads one frame at a time and calibrates each band on the operation's
    // workers, so its tile is what that per-band work is spread over: it keeps a band budget.
    EXPECT_EQ(pipelineCalibratingStackTileRows(kPipelineDarkCount, kPipelineFrameWidth, 2u), 202u);
    EXPECT_LE(std::uint64_t{202u} * kPipelineDarkCount * kPipelineFrameWidth * 2u, kPipelineStackBandBytes);
    EXPECT_GT(std::uint64_t{203u} * kPipelineDarkCount * kPipelineFrameWidth * 2u, kPipelineStackBandBytes);
    EXPECT_EQ(pipelineCalibratingStackTileRows(kPipelineFlatCount, kPipelineFrameWidth, 2u), 303u);
    EXPECT_EQ(pipelineCalibratingStackTileRows(1000u, 100000u, 4u), 16u) << "never fewer than sixteen rows";
    EXPECT_EQ(pipelineCalibratingStackTileRows(1u, 16u, 2u), kPipelineStackMostTileRows);
    EXPECT_EQ(pipelineCalibratingStackTileRows(0u, kPipelineFrameWidth, 2u), 16u);
}

TEST(PipelineStageScenariosTest, AnUndeclaredStorageStateIsNone)
{
    ScenarioSpec row = masterBiasStageScenario(StageCacheCondition::Cold);
    row.parameters["cache"] = "warm";
    EXPECT_FALSE(stageCacheCondition(row).has_value());
    row.parameters.erase("cache");
    EXPECT_FALSE(stageCacheCondition(row).has_value());
    row.group = "stacking";
    EXPECT_FALSE(isMasterBiasStageScenario(row));
}

TEST(FitsAverageOracleTest, TheExactQuotientPassesAndZerosAreNoData)
{
    const TemporaryDirectory directory;
    const ThreeFrames frames = threeFrames(directory);
    const FitsAverageCheck check = checkFitsAverage(frames.paths, frames.exact, 1.0);
    ASSERT_TRUE(check.valid) << check.problem;
    EXPECT_EQ(check.frames, 3u);
    EXPECT_EQ(check.elements, 15u);
    EXPECT_EQ(check.elementsWithoutSamples, 1u);
    // Rounded once to Float32, which is at most half a unit in the last place.
    EXPECT_LE(check.maximumRelative, 0.5 * std::ldexp(1.0, -23));
    EXPECT_GT(check.maximumAbsolute, 0.0) << "the thirds of element 1 are not representable";

    std::vector<float> scaled;
    for (const float value : frames.exact)
    {
        scaled.push_back(static_cast<float>(static_cast<double>(value) / 65535.0));
    }
    const FitsAverageCheck normalized = checkFitsAverage(frames.paths, scaled, 65535.0);
    ASSERT_TRUE(normalized.valid) << normalized.problem;
    EXPECT_LE(normalized.maximumRelative, 1e-6);
}

TEST(FitsAverageOracleTest, EveryElementIsCheckedAndAnyExcursionIsMeasured)
{
    const TemporaryDirectory directory;
    const ThreeFrames frames = threeFrames(directory);
    for (std::size_t element = 0u; element < frames.exact.size(); ++element)
    {
        if (frames.exact[element] == 0.0f)
        {
            continue;
        }
        std::vector<float> wrong = frames.exact;
        wrong[element] = static_cast<float>(static_cast<double>(wrong[element]) * (1.0 + 4e-6));
        const FitsAverageCheck check = checkFitsAverage(frames.paths, wrong, 1.0);
        ASSERT_TRUE(check.valid) << check.problem;
        EXPECT_GT(check.maximumRelative, kMasterBiasRelativeTolerance) << "element " << element;
    }
    // Where no frame holds a sample the only right value is zero.
    std::vector<float> invented = frames.exact;
    invented[4] = 1.0f;
    EXPECT_TRUE(std::isinf(checkFitsAverage(frames.paths, invented, 1.0).maximumRelative));
    std::vector<float> notFinite = frames.exact;
    notFinite[14] = std::numeric_limits<float>::quiet_NaN();
    EXPECT_TRUE(std::isinf(checkFitsAverage(frames.paths, notFinite, 1.0).maximumRelative));
}

TEST(FitsAverageOracleTest, ReadsSignedEightAndThirtyTwoBitIntegerImages)
{
    const TemporaryDirectory directory;
    const std::vector<std::string> signedPaths = {directory.file("s0.fits"), directory.file("s1.fits")};
    writeFits(signedPaths[0], 16, "", 2u, 1u, {-300, 5});
    writeFits(signedPaths[1], 16, "0", 2u, 1u, {-100, 0});
    const std::vector<float> signedMean = {-200.0f, 5.0f};
    const FitsAverageCheck signedCheck = checkFitsAverage(signedPaths, signedMean, 1.0);
    ASSERT_TRUE(signedCheck.valid) << signedCheck.problem;
    EXPECT_EQ(signedCheck.maximumRelative, 0.0);

    const std::vector<std::string> bytePaths = {directory.file("b0.fits"), directory.file("b1.fits")};
    writeFits(bytePaths[0], 8, "", 2u, 1u, {255, 1});
    writeFits(bytePaths[1], 8, "", 2u, 1u, {253, 2});
    const std::vector<float> byteMean = {254.0f, 1.5f};
    const FitsAverageCheck byteCheck = checkFitsAverage(bytePaths, byteMean, 1.0);
    ASSERT_TRUE(byteCheck.valid) << byteCheck.problem;
    EXPECT_EQ(byteCheck.maximumRelative, 0.0);

    const std::vector<std::string> widePaths = {directory.file("w0.fits"), directory.file("w1.fits")};
    writeFits(widePaths[0], 32, "2147483648", 2u, 1u, {4000000000LL - 2147483648LL, 1 - 2147483648LL});
    writeFits(widePaths[1], 32, "2147483648", 2u, 1u, {4000000002LL - 2147483648LL, 3 - 2147483648LL});
    const std::vector<float> wideMean = {4000000001.0f, 2.0f};
    const FitsAverageCheck wideCheck = checkFitsAverage(widePaths, wideMean, 1.0);
    ASSERT_TRUE(wideCheck.valid) << wideCheck.problem;
    EXPECT_LE(wideCheck.maximumRelative, 0.5 * std::ldexp(1.0, -23));
}

TEST(FitsAverageOracleTest, ReadsFloatingImagesAndReadsEitherZeroAsNoData)
{
    // Aligned lights are Float32 with a zero border where no source pixel lands. Their samples are
    // summed in double in frame order, and either signed zero is no data.
    const TemporaryDirectory directory;
    const auto bits32 = [](float value) { return static_cast<std::int64_t>(std::bit_cast<std::uint32_t>(value)); };
    const auto bits64 = [](double value) { return std::bit_cast<std::int64_t>(value); };
    const std::vector<std::vector<float>> frames = {{0.0f, 1.5f, -346707.875f, 1e-3f, 0.0f, 7.25f},
                                                    {-0.0f, 2.5f, 1007415.875f, 3e-3f, 0.0f, -7.25f},
                                                    {3.0f, 0.0f, 12.0f, 2e-3f, -0.0f, 1.0f}};
    std::vector<std::string> paths;
    std::vector<std::string> widePaths;
    for (std::size_t frame = 0u; frame < frames.size(); ++frame)
    {
        std::vector<std::int64_t> stored;
        std::vector<std::int64_t> wide;
        for (const float value : frames[frame])
        {
            stored.push_back(bits32(value));
            wide.push_back(bits64(static_cast<double>(value)));
        }
        paths.push_back(directory.file("f" + std::to_string(frame) + ".fits"));
        writeFits(paths.back(), -32, "", 3u, 2u, stored);
        widePaths.push_back(directory.file("d" + std::to_string(frame) + ".fits"));
        writeFits(widePaths.back(), -64, "", 3u, 2u, wide);
    }
    std::vector<float> exact;
    for (std::size_t element = 0u; element < 6u; ++element)
    {
        double sum = 0.0;
        int count = 0;
        for (const auto& frame : frames)
        {
            if (frame[element] != 0.0f)
            {
                sum += static_cast<double>(frame[element]);
                ++count;
            }
        }
        exact.push_back(count == 0 ? 0.0f : static_cast<float>(sum / count));
    }
    for (const auto* set : {&paths, &widePaths})
    {
        const FitsAverageCheck check = checkFitsAverage(*set, exact, 1.0);
        ASSERT_TRUE(check.valid) << check.problem;
        EXPECT_EQ(check.frames, 3u);
        EXPECT_EQ(check.elementsWithoutSamples, 1u) << "element 4 holds a zero of either sign in every frame";
        EXPECT_LE(check.maximumRelative, 0.5 * std::ldexp(1.0, -23));
        std::vector<float> wrong = exact;
        wrong[1] = static_cast<float>(static_cast<double>(wrong[1]) * (1.0 + 4e-6));
        EXPECT_GT(checkFitsAverage(*set, wrong, 1.0).maximumRelative, kFinalStackRelativeTolerance);
        std::vector<float> counted = exact;
        counted[0] = 1.0f;
        EXPECT_GT(checkFitsAverage(*set, counted, 1.0).maximumRelative, kFinalStackRelativeTolerance)
            << "a zero taken as data moves the mean";
    }

    // A sample that is not finite has no average; the check refuses the frame rather than guess.
    std::vector<std::int64_t> poisoned;
    for (const float value : frames[1])
    {
        poisoned.push_back(bits32(value));
    }
    poisoned[3] = bits32(std::numeric_limits<float>::quiet_NaN());
    writeFits(directory.file("nan.fits"), -32, "", 3u, 2u, poisoned);
    std::vector<std::string> withNan = paths;
    withNan[1] = directory.file("nan.fits");
    const FitsAverageCheck refused = checkFitsAverage(withNan, exact, 1.0);
    EXPECT_FALSE(refused.valid);
    EXPECT_NE(refused.problem.find("nan.fits"), std::string::npos) << refused.problem;
}

TEST(FitsAverageOracleTest, AStatedResultRangeDecidesOnlyTheElementsItClampsAndCountsThem)
{
    // An engine that writes its average clamped to a range is held to the clamped average: the
    // range decides the elements whose mean lies outside it, and says how many there were, and
    // decides nothing else.
    const TemporaryDirectory directory;
    const auto bits = [](float value) { return static_cast<std::int64_t>(std::bit_cast<std::uint32_t>(value)); };
    const std::vector<std::vector<float>> frames = {{-0.25f, 0.5f, 1.5f, 0.0f}, {-0.5f, 0.25f, 2.5f, 0.0f}};
    std::vector<std::string> paths;
    for (std::size_t frame = 0u; frame < frames.size(); ++frame)
    {
        std::vector<std::int64_t> stored;
        for (const float value : frames[frame])
        {
            stored.push_back(bits(value));
        }
        paths.push_back(directory.file("r" + std::to_string(frame) + ".fits"));
        writeFits(paths.back(), -32, "", 2u, 2u, stored);
    }
    const std::vector<float> exact = {-0.375f, 0.375f, 2.0f, 0.0f};
    const std::vector<float> clamped = {0.0f, 0.375f, 1.0f, 0.0f};
    const FitsAverageCheck plain = checkFitsAverage(paths, exact, 1.0);
    ASSERT_TRUE(plain.valid) << plain.problem;
    EXPECT_EQ(plain.maximumRelative, 0.0);
    EXPECT_EQ(plain.elementsClamped, 0u);

    const FitsAverageResultRange unit{0.0, 1.0};
    const FitsAverageCheck held = checkFitsAverage(paths, clamped, 1.0, unit);
    ASSERT_TRUE(held.valid) << held.problem;
    EXPECT_EQ(held.maximumRelative, 0.0);
    EXPECT_EQ(held.elementsClamped, 2u) << "elements 0 and 2 lie outside the range";
    EXPECT_GE(checkFitsAverage(paths, exact, 1.0, unit).maximumRelative, 1.0)
        << "an unclamped result fails a run that states the range";
    EXPECT_GT(checkFitsAverage(paths, clamped, 1.0).maximumRelative, 0.4)
        << "a clamped result fails a run that states none";
    std::vector<float> wrong = clamped;
    wrong[1] = 0.376f;
    EXPECT_GT(checkFitsAverage(paths, wrong, 1.0, unit).maximumRelative, 1e-3)
        << "inside the range the clamped average is the exact one";

    ResultRecord record;
    recordFitsAverageCheck(held, record);
    EXPECT_EQ(record.counters.at("stageOracleElementsClamped"), 2.0);
}

TEST(FitsAverageOracleTest, AResidualIsMeasuredAgainstTheMagnitudeOfWhatWasAveraged)
{
    // A floating sum is accurate to the magnitude of its terms, not of its result, so where samples of
    // both signs nearly cancel a residual is measured against their average magnitude. For samples of
    // one sign that is the mean itself.
    const TemporaryDirectory directory;
    const auto bits = [](float value) { return static_cast<std::int64_t>(std::bit_cast<std::uint32_t>(value)); };
    const std::vector<std::string> paths = {directory.file("c0.fits"), directory.file("c1.fits")};
    writeFits(paths[0], -32, "", 2u, 1u, {bits(1000.0f), bits(3.0f)});
    writeFits(paths[1], -32, "", 2u, 1u, {bits(-999.75f), bits(5.0f)});
    // Element 0 averages to 0.125 from terms of magnitude near 1000; element 1 to 4 from 3 and 5.
    const FitsAverageCheck close = checkFitsAverage(paths, std::vector<float>{0.125f + 1e-4f, 4.0f}, 1.0);
    ASSERT_TRUE(close.valid) << close.problem;
    EXPECT_NEAR(close.maximumRelative, 1e-4 / 999.875, 1e-9) << "against 999.875, not against 0.125";
    EXPECT_LT(close.maximumRelative, kFinalStackRelativeTolerance);
    EXPECT_GT(checkFitsAverage(paths, std::vector<float>{0.125f + 1e-2f, 4.0f}, 1.0).maximumRelative,
              kFinalStackRelativeTolerance);
    const FitsAverageCheck oneSign = checkFitsAverage(paths, std::vector<float>{0.125f, 4.0f + 4e-5f}, 1.0);
    EXPECT_NEAR(oneSign.maximumRelative, 1e-5, 1e-7) << "one sign: the residual against the mean";
}

TEST(FitsAverageOracleTest, RefusesWhatItDoesNotReadExactly)
{
    const TemporaryDirectory directory;
    const ThreeFrames frames = threeFrames(directory);
    const auto refused = [&](const std::string& name) {
        std::vector<std::string> paths = frames.paths;
        paths[1] = directory.file(name);
        const FitsAverageCheck check = checkFitsAverage(paths, frames.exact, 1.0);
        EXPECT_FALSE(check.valid) << name;
        EXPECT_NE(check.problem.find(name), std::string::npos) << check.problem;
    };
    const std::vector<std::int64_t> fifteen(15u, 7);
    writeFits(directory.file("scaled.fits"), 16, "32768", 5u, 3u, fifteen, {card("BSCALE", "2")});
    refused("scaled.fits");
    writeFits(directory.file("float.fits"), -32, "1", 5u, 3u, fifteen);
    refused("float.fits");
    writeFits(directory.file("floatscaled.fits"), -32, "", 5u, 3u, fifteen, {card("BSCALE", "2")});
    refused("floatscaled.fits");
    writeFits(directory.file("sixtyfour.fits"), 64, "", 5u, 3u, fifteen);
    refused("sixtyfour.fits");
    writeFits(directory.file("offset.fits"), 16, "100", 5u, 3u, fifteen);
    refused("offset.fits");
    writeFits(directory.file("smaller.fits"), 16, "32768", 5u, 2u, std::vector<std::int64_t>(10u, 7));
    refused("smaller.fits");
    // Header intact and payload cut short: fifteen samples need thirty bytes and ten remain.
    writeFits(directory.file("truncated.fits"), 16, "32768", 5u, 3u, fifteen);
    std::filesystem::resize_file(directory.file("truncated.fits"), 2880u + 10u);
    refused("truncated.fits");
    refused("missing.fits");
    EXPECT_FALSE(checkFitsAverage({}, frames.exact, 1.0).valid);
}

TEST(FitsAverageOracleTest, EvidenceIsCompleteOnlyForTheRowsFramesAndGeometry)
{
    const ScenarioSpec row = masterBiasStageScenario(StageCacheCondition::Hot);
    FitsAverageCheck check;
    check.valid = true;
    check.frames = kPipelineBiasCount;
    check.elements = static_cast<std::uint64_t>(kPipelineFrameWidth) * kPipelineFrameHeight;
    check.maximumRelative = 3e-8;
    ResultRecord record;
    recordFitsAverageCheck(check, record);
    ASSERT_TRUE(record.correctnessMetric.has_value());
    EXPECT_DOUBLE_EQ(*record.correctnessMetric, 3e-8);
    EXPECT_TRUE(fitsAverageEvidenceValid(row, kPipelineBiasCount, record));
    EXPECT_FALSE(fitsAverageEvidenceValid(row, kPipelineBiasCount - 1u, record)) << "a different frame count";

    const auto broken = [&](const char* counter, double value) {
        ResultRecord changed = record;
        changed.counters[counter] = value;
        EXPECT_FALSE(fitsAverageEvidenceValid(row, kPipelineBiasCount, changed)) << counter;
    };
    broken("stageOracleVersion", kFitsAverageOracleVersion + 1.0);
    broken("stageOracleValid", 0.0);
    broken("stageOracleElements", 12.0);
    broken("stageOracleMaxRelative", 2e-6);
    ResultRecord missing = record;
    missing.counters.erase("stageOracleMaxRelative");
    EXPECT_FALSE(fitsAverageEvidenceValid(row, kPipelineBiasCount, missing));
    ResultRecord mismatched = record;
    mismatched.correctnessMetric = 0.0;
    EXPECT_FALSE(fitsAverageEvidenceValid(row, kPipelineBiasCount, mismatched))
        << "the metric is the check's own maximum";
    ResultRecord skipped = record;
    skipped.skipped = true;
    EXPECT_FALSE(fitsAverageEvidenceValid(row, kPipelineBiasCount, skipped));

    check.valid = false;
    check.maximumRelative = std::numeric_limits<double>::infinity();
    ResultRecord invalid;
    recordFitsAverageCheck(check, invalid);
    EXPECT_FALSE(invalid.correctnessMetric.has_value());
    EXPECT_TRUE(std::isfinite(invalid.counters.at("stageOracleMaxRelative")));
    EXPECT_FALSE(fitsAverageEvidenceValid(row, kPipelineBiasCount, invalid));
}

TEST(PipelineScratchTest, ATemporaryRootIsCreatedEmptyAndRemovedWithEverythingInIt)
{
    std::filesystem::path root;
    {
        const PipelineScratch scratch("afternight-scratch-test-", "");
        ASSERT_TRUE(scratch.ok()) << scratch.error();
        EXPECT_FALSE(scratch.kept());
        root = scratch.path();
        EXPECT_TRUE(std::filesystem::is_directory(root));
        EXPECT_TRUE(std::filesystem::is_empty(root));
        EXPECT_TRUE(std::filesystem::equivalent(root.parent_path(), std::filesystem::temp_directory_path()));
        EXPECT_EQ(root.filename().string().rfind("afternight-scratch-test-", 0), 0u);
        std::filesystem::create_directories(root / "aligned");
        std::ofstream(root / "aligned" / "frame.fits", std::ios::binary) << "frame";
    }
    EXPECT_FALSE(std::filesystem::exists(root)) << "a temporary root goes with its run";
}

TEST(PipelineScratchTest, AKeptRootIsTheCallersAndOutlivesTheRun)
{
    // A new directory and an empty one are both accepted, and whatever the run writes stays.
    const TemporaryDirectory directory;
    for (const bool existing : {false, true})
    {
        const std::string kept = directory.file(existing ? "empty" : "new");
        if (existing)
        {
            std::filesystem::create_directories(kept);
        }
        {
            const PipelineScratch scratch("afternight-scratch-test-", kept);
            ASSERT_TRUE(scratch.ok()) << scratch.error();
            EXPECT_TRUE(scratch.kept());
            EXPECT_EQ(std::filesystem::path(scratch.path()), std::filesystem::path(kept));
            std::filesystem::create_directories(std::filesystem::path(kept) / "aligned");
            std::ofstream(std::filesystem::path(kept) / "aligned" / "frame.fits", std::ios::binary) << "frame";
        }
        EXPECT_TRUE(std::filesystem::exists(std::filesystem::path(kept) / "aligned" / "frame.fits")) << kept;
    }
}

TEST(PipelineScratchTest, AKeptRootThatHoldsAnythingIsRefusedAndLeftAlone)
{
    const TemporaryDirectory directory;
    const std::string kept = directory.file("used");
    std::filesystem::create_directories(kept);
    std::ofstream(std::filesystem::path(kept) / "earlier.fits", std::ios::binary) << "earlier";
    {
        const PipelineScratch scratch("afternight-scratch-test-", kept);
        EXPECT_FALSE(scratch.ok());
        EXPECT_NE(scratch.error().find("used"), std::string::npos) << scratch.error();
    }
    EXPECT_TRUE(std::filesystem::exists(std::filesystem::path(kept) / "earlier.fits"))
        << "a refused root is never emptied or removed";
}

TEST(FileCacheTest, DropsAndReadsNamedFilesAndNamesAMissingOne)
{
    const TemporaryDirectory directory;
    std::vector<std::string> paths = {directory.file("a.bin"), directory.file("b.bin")};
    for (const std::string& path : paths)
    {
        std::ofstream(path, std::ios::binary) << std::string(3u * 1024u * 1024u + 17u, 'x');
    }
    std::string error;
    EXPECT_TRUE(dropFilesFromCache(paths, error)) << error;
    EXPECT_TRUE(readFilesThroughCache(paths, error)) << error;
    EXPECT_TRUE(dropFilesFromCache(std::vector<std::string>{}, error));
    paths.push_back(directory.file("missing.bin"));
    EXPECT_FALSE(dropFilesFromCache(paths, error));
    EXPECT_NE(error.find("missing.bin"), std::string::npos) << error;
    error.clear();
    EXPECT_FALSE(readFilesThroughCache(paths, error));
    EXPECT_NE(error.find("missing.bin"), std::string::npos) << error;
}
