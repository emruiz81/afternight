// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Tests for the engine-neutral benchmark common layer.
//
// The suite links EngineBenchCommon and gtest and nothing else. That is the point: if this ever
// needs an engine header to compile, the common layer has stopped being engine-neutral.

#include "BenchJson.h"
#include "BasicStatisticsOracle.h"
#include "CosmeticCorrectionOracle.h"
#include "HistogramOracle.h"
#include "HistogramStretchOracle.h"
#include "AsinhStretchOracle.h"
#include "GeneralizedHyperbolicStretchOracle.h"
#include "MidtonesTransferStretchOracle.h"
#include "StatisticalStretchOracle.h"
#include "BilinearResampleOracle.h"
#include "ColorTransformOracle.h"
#include "MedianFilterOracle.h"
#include "NoiseReductionOracle.h"
#include "OrderStatisticsOracle.h"
#include "CapturedStatistics.h"
#include "GammaTransferOracle.h"
#include "PhaseCorrelationOracle.h"
#include "PipelineFixture.h"
#include "PipelineStageFixture.h"
#include "FitsAverageOracle.h"
#include "RestorationOracle.h"
#include "CatalogFixture.h"
#include "StarReductionAlignmentFixture.h"
#include "Descriptors.h"
#include "DemosaicOracle.h"
#include "DrizzleExecutionFixture.h"
#include "StackingFixture.h"
#include "StackingOracle.h"
#include "CodecLookupFixture.h"
#include "Digest.h"
#include "Fixture.h"
#include "HostPolicy.h"
#include "ProcessClock.h"
#include "ResidentMemory.h"
#include "Results.h"
#include "RunnerHarness.h"
#include "Scenario.h"
#include "CorpusFixtures.h"
#include "RawDump.h"
#include "SyntheticFixtures.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <set>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <limits>
#include <optional>
#include <random>
#include <span>
#include <string>
#include <thread>
#include <utility>
#include <vector>

using namespace an::bench;

namespace
{

/// A unique scratch directory per process, so two concurrent runs of this suite cannot collide and
/// nothing is written anywhere near the production AfterNight storage tree (AGENTS.md).
std::filesystem::path scratchDir(const char* name)
{
    static const std::string unique =
        std::to_string(static_cast<unsigned long long>(std::chrono::steady_clock::now().time_since_epoch().count()));
    return std::filesystem::temp_directory_path() / (std::string("an-bench-") + name + "-" + unique);
}

SampleDescriptor makeDescriptor()
{
    SampleDescriptor d;
    d.width = 64;
    d.height = 32;
    d.channels = 3;
    d.sampleType = SampleType::Float32;
    d.layout = Layout::Planar;
    return d;
}

ScenarioSpec makeScenario(const std::string& id = "transfer.u16_to_f32")
{
    ScenarioSpec s;
    s.id = id;
    s.group = "transfer";
    s.operationVersion = 2;
    s.parameters["rounding"] = "nearest-even";
    s.input = makeDescriptor();
    s.corpusId = "synthetic.ramp";
    s.seed = 42;
    s.correctnessClass = CorrectnessClass::MaxUlp;
    s.tolerance = 1.0;
    s.bytesProcessed = 4096;
    s.elementsProcessed = 2048;
    s.allowedBackends = {BackendId::Scalar, BackendId::Avx2};
    return s;
}

ResultRecord makeResult(const std::string& id, double medianMs, const std::string& digest)
{
    ResultRecord r;
    r.scenarioId = id;
    r.operationVersion = 2;
    r.requestedBackend = BackendId::Scalar;
    r.actualBackend = BackendId::Scalar;
    r.threadCount = 1;
    r.timings.wallMs = {medianMs, medianMs, medianMs};
    r.timings.cpuMs = {medianMs, medianMs, medianMs};
    r.outputDigest = digest;
    r.fixtureDigest = "fixture-abc";
    r.scenarioDigest = "scenario-abc";
    r.correctnessClass = CorrectnessClass::BitExact;
    r.bytesProcessed = 4096;
    return r;
}

RunManifest makeRun(const std::string& host = "bench-win")
{
    RunManifest run;
    run.runnerVersion = "0.1";
    run.timestamp = "2026-08-22T00:00:00Z";
    run.runnerBuild.commit = "runner-abc123";
    run.runnerBuild.compiler = "MSVC 19.51";
    run.runnerBuild.standardLibrary = "MSVC STL 145";
    run.runnerBuild.linker = "MSVC link 19.51";
    run.runnerBuild.configuration = "Release";
    run.runnerBuild.flags = "arch=avx2 cpp=202002 fastmath=off ndebug=on";
    run.runnerBuild.dependencyVersions["EngineBenchCommon"] = "schema 3";
    run.build.commit = "abc123";
    run.build.compiler = "MSVC 19.51";
    run.build.standardLibrary = "MSVC STL 145";
    run.build.linker = "MSVC link 19.51";
    run.build.configuration = "Release";
    run.build.flags = "arch=avx2 cpp=202002 fastmath=off ndebug=on";
    run.build.dependencyVersions["AfterNightEngine"] = "1.0.0";
    run.dependencyLockDigest = std::string(64, 'a');
    run.environment.hostId = host;
    run.environment.cpuModel = "Ryzen 9 9950X";
    run.environment.microcodeVersion = "0x0b404032";
    run.environment.physicalCores = 16;
    run.environment.logicalCores = 32;
    run.environment.numaNodes = 1;
    run.environment.memoryBytes = 64ull * 1024ull * 1024ull * 1024ull;
    run.environment.osName = "Windows";
    run.environment.gpuModel = "not-used";
    run.environment.driverVersion = "not-used";
    run.repetitions = 3;
    return run;
}

} // namespace

// --- descriptors ------------------------------------------------------------------------------

TEST(DescriptorTest, SampleSizeMatchesRepresentation)
{
    SampleDescriptor d;
    d.sampleType = SampleType::UInt8;
    EXPECT_EQ(d.sampleSizeBytes(), 1u);
    d.sampleType = SampleType::UInt16;
    EXPECT_EQ(d.sampleSizeBytes(), 2u);
    d.sampleType = SampleType::Float32;
    EXPECT_EQ(d.sampleSizeBytes(), 4u);
    d.sampleType = SampleType::Float64;
    EXPECT_EQ(d.sampleSizeBytes(), 8u);
    d.sampleType = SampleType::Int64;
    EXPECT_EQ(d.sampleSizeBytes(), 8u);
}

TEST(DescriptorTest, TightlyPackedRowsAreDerivedWhenStrideIsUnset)
{
    SampleDescriptor d = makeDescriptor();
    EXPECT_EQ(d.rowBytes(), 64u * 4u);
    EXPECT_EQ(d.totalBytes(), 64ull * 32ull * 3ull * 4ull);
}

TEST(DescriptorTest, ExplicitStrideIsHonouredSoNoncanonicalLayoutsCanBeBenchmarked)
{
    SampleDescriptor d = makeDescriptor();
    d.rowStrideBytes = 320; // padded past the 256-byte minimum
    EXPECT_EQ(d.rowBytes(), 320u);
    EXPECT_EQ(d.totalBytes(), 320ull * 32ull * 3ull);
}

TEST(DescriptorTest, StrideNarrowerThanARowIsInvalid)
{
    SampleDescriptor d = makeDescriptor();
    d.rowStrideBytes = 4; // narrower than 64 samples
    EXPECT_FALSE(d.isValid());
}

TEST(DescriptorTest, OverflowingGeometryReportsZeroBytesRatherThanWrapping)
{
    SampleDescriptor d;
    d.width = 0xFFFFFFFFu;
    d.height = 0xFFFFFFFFu;
    d.channels = 4;
    d.sampleType = SampleType::Float64;
    EXPECT_EQ(d.totalBytes(), 0u);
    EXPECT_FALSE(d.isValid());
}

TEST(DescriptorTest, InterleavedRowArithmeticCannotWrap)
{
    SampleDescriptor d;
    d.width = 0xFFFFFFFFu;
    d.height = 1;
    d.channels = 0xFFFFFFFFu;
    d.sampleType = SampleType::Float64;
    d.layout = Layout::Interleaved;
    EXPECT_EQ(d.rowBytes(), 0u);
    EXPECT_EQ(d.totalBytes(), 0u);
    EXPECT_FALSE(d.isValid());
}

TEST(DescriptorTest, UnderspecifiedSemiPlanarLayoutIsRejected)
{
    SampleDescriptor d = makeDescriptor();
    d.layout = Layout::SemiPlanar;
    EXPECT_FALSE(d.isValid());
}

TEST(DescriptorTest, PackedCfaRequiresOnePlaneAndAPattern)
{
    SampleDescriptor d = makeDescriptor();
    d.layout = Layout::PackedCfa;
    EXPECT_FALSE(d.isValid());
    d.channels = 1;
    d.cfaPattern = "RGGB";
    EXPECT_TRUE(d.isValid());
}

TEST(DescriptorTest, ZeroExtentIsInvalid)
{
    SampleDescriptor d = makeDescriptor();
    d.width = 0;
    EXPECT_FALSE(d.isValid());
}

TEST(DescriptorTest, EnumsRoundTripThroughTheirNames)
{
    SampleType sample{};
    ASSERT_TRUE(fromString(toString(SampleType::Int16), sample));
    EXPECT_EQ(sample, SampleType::Int16);

    CorrectnessClass correctness{};
    ASSERT_TRUE(fromString(toString(CorrectnessClass::RmsPsnr), correctness));
    EXPECT_EQ(correctness, CorrectnessClass::RmsPsnr);

    BackendId backend{};
    ASSERT_TRUE(fromString(toString(BackendId::Avx512), backend));
    EXPECT_EQ(backend, BackendId::Avx512);

    EXPECT_FALSE(fromString("NotARealBackend", backend));
}

// --- digest -----------------------------------------------------------------------------------

TEST(DigestTest, MatchesKnownSha256Vectors)
{
    EXPECT_EQ(Sha256::of(std::string("")), "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    EXPECT_EQ(Sha256::of(std::string("abc")), "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    EXPECT_EQ(Sha256::of(std::string("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq")),
              "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
}

TEST(DigestTest, ChunkBoundariesDoNotChangeTheResult)
{
    const std::string text(1000, 'x');
    Sha256 chunked;
    for (std::size_t offset = 0; offset < text.size(); offset += 7)
    {
        chunked.update(text.data() + offset, (std::min)(std::size_t{7}, text.size() - offset));
    }
    EXPECT_EQ(chunked.hex(), Sha256::of(text));
}

TEST(DigestTest, ProcessorExtensionsProduceThePortableDigestAtEveryLengthAndChunking)
{
    std::mt19937_64 random(0x5ea1u);
    std::vector<std::uint8_t> bytes((3u << 20) + 37u);
    for (std::uint8_t& value : bytes)
        value = static_cast<std::uint8_t>(random());
    const auto portableOf = [](const std::uint8_t* data, std::size_t size) {
        Sha256 portable(Sha256Implementation::Portable);
        portable.update(data, size);
        return portable.hex();
    };
    for (std::size_t size = 0u; size <= 300u; ++size)
        EXPECT_EQ(Sha256::of(bytes.data(), size), portableOf(bytes.data(), size)) << "size " << size;
    Sha256 chunked;
    for (std::size_t offset = 0u; offset < bytes.size();)
    {
        const std::size_t take = (std::min)(bytes.size() - offset, 1u + static_cast<std::size_t>(random() % 70000u));
        chunked.update(bytes.data() + offset, take);
        offset += take;
    }
    EXPECT_EQ(chunked.hex(), portableOf(bytes.data(), bytes.size()));
    EXPECT_EQ(Sha256::of(std::string(1000000u, 'a')),
              "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
    if (!Sha256::processorExtensionsAvailable())
        GTEST_SKIP() << "this processor has no SHA extensions, so both digests took the portable path";
}

TEST(DigestTest, SpansTheLengthPaddingBoundary)
{
    // 55, 56, and 64 bytes exercise the three padding paths in the final block.
    for (std::size_t size : {std::size_t{55}, std::size_t{56}, std::size_t{64}})
    {
        const std::string text(size, 'a');
        EXPECT_EQ(Sha256::of(text).size(), 64u) << "size " << size;
        EXPECT_NE(Sha256::of(text), Sha256::of(std::string(size + 1, 'a')));
    }
}

// --- scenario validation ----------------------------------------------------------------------

TEST(ScenarioTest, WellFormedScenarioValidates)
{
    EXPECT_TRUE(makeScenario().validate().empty());
}

TEST(ScenarioTest, MissingIdIsReported)
{
    ScenarioSpec s = makeScenario();
    s.id.clear();
    EXPECT_FALSE(s.validate().empty());
}

TEST(ScenarioTest, ToleranceBearingClassRequiresANonZeroTolerance)
{
    ScenarioSpec s = makeScenario();
    s.correctnessClass = CorrectnessClass::AbsoluteRelative;
    s.tolerance = 0.0;
    EXPECT_FALSE(s.validate().empty());
}

TEST(ScenarioTest, TimedBatchIsPositiveBoundedAndDefaultsToOne)
{
    ScenarioSpec s = makeScenario();
    ASSERT_EQ(timedBatchIterations(s), 1u);

    s.parameters[kTimedBatchParameter] = "64";
    EXPECT_TRUE(s.validate().empty());
    ASSERT_EQ(timedBatchIterations(s), 64u);

    s.parameters[kTimedBatchParameter] = "100000000";
    EXPECT_TRUE(s.validate().empty());
    ASSERT_EQ(timedBatchIterations(s), 100000000u);

    for (const char* invalid : {"0", "-1", "1.5", "1000000001", "many"})
    {
        s.parameters[kTimedBatchParameter] = invalid;
        EXPECT_FALSE(s.validate().empty()) << invalid;
        EXPECT_FALSE(timedBatchIterations(s).has_value()) << invalid;
    }

    s.parameters[kTimedBatchParameter] = "2";
    s.inputSource = InputSource::CorpusFrameSet;
    EXPECT_FALSE(s.validate().empty());
}

// --- corpus manifest --------------------------------------------------------------------------

TEST(CorpusTest, ResolvesAPresentDatasetAndReportsAnAbsentOneAsSkipped)
{
    const std::filesystem::path root = scratchDir("corpus");
    std::filesystem::create_directories(root);
    {
        std::ofstream file(root / "present.fit", std::ios::binary);
        file << "data";
    }

    CorpusEntry present;
    present.id = "ds.present";
    present.relativePath = "present.fit";
    present.sha256 = std::string(64, 'a');
    present.sizeBytes = 4;
    present.redistribution = "local-only";
    present.expected = makeDescriptor();

    CorpusEntry absent = present;
    absent.id = "ds.absent";
    absent.relativePath = "absent.fit";

    CorpusManifest manifest;
    manifest.add(present);
    manifest.add(absent);

    EXPECT_TRUE(manifest.resolve("ds.present", root.string()).available);

    const auto missing = manifest.resolve("ds.absent", root.string());
    EXPECT_FALSE(missing.available);
    EXPECT_FALSE(missing.skipReason.empty()) << "an absent dataset must say why it was skipped";
    EXPECT_NE(missing.skipReason.find("ds.absent"), std::string::npos)
        << "the skip reason must name the dataset so a reader knows what was not covered";

    const auto unknown = manifest.resolve("ds.nope", root.string());
    EXPECT_FALSE(unknown.available);

    std::filesystem::remove_all(root);
}

TEST(CorpusTest, DuplicateIdsAreRejected)
{
    CorpusEntry entry;
    entry.id = "ds.dup";
    entry.relativePath = "a.fit";
    entry.sha256 = std::string(64, 'b');
    entry.sizeBytes = 1;
    entry.redistribution = "local-only";
    entry.expected = makeDescriptor();

    CorpusManifest manifest;
    manifest.add(entry);
    manifest.add(entry);
    EXPECT_FALSE(manifest.validate().empty());
}

// --- results ----------------------------------------------------------------------------------

TEST(ResultTest, MedianAndDispersionComeFromTheWholeSampleSet)
{
    TimingSamples t;
    t.wallMs = {10.0, 12.0, 11.0, 100.0};
    EXPECT_DOUBLE_EQ(t.medianWallMs(), 11.5);
    EXPECT_DOUBLE_EQ(t.minWallMs(), 10.0);
    EXPECT_DOUBLE_EQ(t.maxWallMs(), 100.0);
    EXPECT_GT(t.coefficientOfVariation(), 0.5) << "an outlier must show up as dispersion";
}

TEST(ResultTest, ThroughputIsDerivedFromTheMedian)
{
    ResultRecord r = makeResult("s", 2.0, "digest");
    r.bytesProcessed = 2000;
    EXPECT_DOUBLE_EQ(r.bytesPerSecond(), 1000000.0);
}

TEST(ResultTest, ABackendSubstitutionIsInvalid)
{
    ResultRecord r = makeResult("s", 1.0, "digest");
    r.requestedBackend = BackendId::Avx512;
    r.actualBackend = BackendId::Scalar;
    const auto problems = r.validate();
    EXPECT_FALSE(problems.empty())
        << "running a fallback while reporting the requested backend hides what was measured";
}

TEST(ResultTest, AMeasuredResultRequiresActualBackend)
{
    ResultRecord r = makeResult("s", 1.0, "digest");
    r.requestedBackend = BackendId::Unspecified;
    r.actualBackend = BackendId::Unspecified;
    const auto problems = r.validate();
    EXPECT_FALSE(problems.empty()) << "a published measurement must identify the implementation that ran";
}

TEST(ResultTest, AnUnavailableBackendMaySkipInsteadOfSubstituting)
{
    ResultRecord r = makeResult("s", 1.0, "digest");
    r.requestedBackend = BackendId::Avx512;
    r.actualBackend = BackendId::Unspecified;
    r.skipped = true;
    r.skipReason = "AVX-512 not present on this host";
    r.timings = {};
    r.outputDigest.clear();
    EXPECT_TRUE(r.validate().empty());
}

TEST(ResultTest, ASkipWithoutAReasonIsInvalid)
{
    ResultRecord r = makeResult("s", 1.0, "digest");
    r.skipped = true;
    EXPECT_FALSE(r.validate().empty());
}

TEST(ResultTest, AMeasuredResultRequiresFixtureAndScenarioDigests)
{
    ResultRecord r = makeResult("s", 1.0, "digest");
    r.fixtureDigest.clear();
    EXPECT_FALSE(r.validate().empty());

    r.fixtureDigest = "fixture";
    r.scenarioDigest.clear();
    EXPECT_FALSE(r.validate().empty());
}

TEST(ResultTest, CpuAndWallSamplesMustDescribeTheSameRepetitions)
{
    ResultRecord r = makeResult("s", 1.0, "digest");
    r.timings.cpuMs.pop_back();
    EXPECT_FALSE(r.validate().empty());
}

TEST(ResultTest, DuplicateScenarioIdsInvalidateARun)
{
    RunManifest run = makeRun();
    run.results.push_back(makeResult("duplicate", 1.0, "a"));
    run.results.push_back(makeResult("duplicate", 1.0, "b"));
    EXPECT_FALSE(run.validate().empty());
}

TEST(ResultTest, UnknownHostIdentityInvalidatesARun)
{
    RunManifest run = makeRun();
    run.environment.hostId = "unknown-host";
    EXPECT_FALSE(run.validate().empty());
}

TEST(ResultTest, PublishableRunRequiresSeparateRunnerAndEngineProvenance)
{
    RunManifest run = makeRun();
    run.runnerBuild.commit.clear();
    EXPECT_FALSE(run.validate().empty());

    run = makeRun();
    run.build.commit.clear();
    EXPECT_FALSE(run.validate().empty());

    run = makeRun();
    run.dependencyLockDigest.clear();
    EXPECT_FALSE(run.validate().empty());
}

// --- json -------------------------------------------------------------------------------------

TEST(JsonTest, TheEnvironmentPolicyFieldsSurviveARoundTrip)
{
    RunManifest original = makeRun();
    original.environment.affinityCores = 12;
    original.environment.affinityMask = "fff";
    original.environment.powerPlan = "governor=performance turbo=on";
    original.environment.backgroundLoad = 0.125;
    original.environment.cpuFrequencyMhz = {3200.0, 4100.5, 4700.0, 61};

    RunManifest parsed;
    ASSERT_TRUE(json::parseRun(json::toJson(original), parsed));
    EXPECT_EQ(parsed.environment.affinityCores, 12u);
    EXPECT_EQ(parsed.environment.affinityMask, "fff");
    EXPECT_EQ(parsed.environment.powerPlan, "governor=performance turbo=on");
    EXPECT_EQ(parsed.environment.microcodeVersion, "0x0b404032");
    EXPECT_EQ(parsed.environment.numaNodes, 1u);
    EXPECT_DOUBLE_EQ(parsed.environment.backgroundLoad, 0.125);
    EXPECT_DOUBLE_EQ(parsed.environment.cpuFrequencyMhz.minimum, 3200.0);
    EXPECT_DOUBLE_EQ(parsed.environment.cpuFrequencyMhz.mean, 4100.5);
    EXPECT_DOUBLE_EQ(parsed.environment.cpuFrequencyMhz.maximum, 4700.0);
    EXPECT_EQ(parsed.environment.cpuFrequencyMhz.samples, 61u);
}

TEST(JsonTest, RunnerAndEngineProvenanceRemainDistinctAcrossARoundTrip)
{
    RunManifest original = makeRun();
    original.runnerBuild.commit = "runner-commit";
    original.runnerBuild.dirty = true;
    original.build.commit = "pinned-engine-commit";
    original.build.dirty = false;

    RunManifest parsed;
    ASSERT_TRUE(json::parseRun(json::toJson(original), parsed));
    EXPECT_EQ(parsed.runnerBuild.commit, "runner-commit");
    EXPECT_TRUE(parsed.runnerBuild.dirty);
    EXPECT_EQ(parsed.build.commit, "pinned-engine-commit");
    EXPECT_FALSE(parsed.build.dirty);
    EXPECT_EQ(parsed.dependencyLockDigest, std::string(64, 'a'));
}

TEST(JsonTest, ScenarioRoundTrips)
{
    const ScenarioSpec original = makeScenario();
    ScenarioSpec parsed;
    ASSERT_TRUE(json::parseScenario(json::toJson(original), parsed));
    EXPECT_EQ(parsed.id, original.id);
    EXPECT_EQ(parsed.operationVersion, original.operationVersion);
    EXPECT_EQ(parsed.parameters, original.parameters);
    EXPECT_EQ(parsed.input, original.input);
    EXPECT_EQ(parsed.correctnessClass, original.correctnessClass);
    EXPECT_EQ(parsed.allowedBackends, original.allowedBackends);
    EXPECT_EQ(parsed.seed, original.seed);
}

TEST(JsonTest, RunRoundTripsIncludingResults)
{
    RunManifest original = makeRun();
    original.results.push_back(makeResult("a", 5.0, "digest-a"));
    original.results.push_back(makeResult("b", 7.0, "digest-b"));
    original.environment.isaCapabilities = {"AVX2", "AVX512F"};

    RunManifest parsed;
    ASSERT_TRUE(json::parseRun(json::toJson(original), parsed));
    ASSERT_EQ(parsed.results.size(), 2u);
    EXPECT_EQ(parsed.results[0].scenarioId, "a");
    EXPECT_EQ(parsed.results[0].timings.wallMs.size(), 3u);
    EXPECT_EQ(parsed.environment.isaCapabilities.size(), 2u);
    EXPECT_EQ(parsed.environment.cpuModel, "Ryzen 9 9950X");
}

TEST(JsonTest, EnumsAreWrittenAsNamesNotOrdinals)
{
    const std::string text = json::toJson(makeScenario());
    EXPECT_NE(text.find("Float32"), std::string::npos);
    EXPECT_NE(text.find("MaxUlp"), std::string::npos);
    EXPECT_NE(text.find("Avx2"), std::string::npos);
}

TEST(JsonTest, MalformedInputIsReportedNotThrown)
{
    RunManifest parsed;
    const auto result = json::parseRun("{ this is not json", parsed);
    EXPECT_FALSE(result.ok);
    EXPECT_FALSE(result.errors.empty());
}

TEST(JsonTest, TruncatedInputIsReportedNotThrown)
{
    RunManifest full = makeRun();
    full.results.push_back(makeResult("a", 1.0, "d"));
    const std::string text = json::toJson(full);

    RunManifest parsed;
    const auto result = json::parseRun(text.substr(0, text.size() / 2), parsed);
    EXPECT_FALSE(result.ok);
}

TEST(JsonTest, AnUnknownSchemaVersionIsRefusedRatherThanGuessedAt)
{
    RunManifest run = makeRun();
    std::string text = json::toJson(run);
    const std::string needle = "\"schemaVersion\": " + std::to_string(RunManifest::kSchemaVersion);
    const auto at = text.find(needle);
    ASSERT_NE(at, std::string::npos);
    text.replace(at, needle.size(), "\"schemaVersion\": 9999");

    RunManifest parsed;
    const auto result = json::parseRun(text, parsed);
    EXPECT_FALSE(result.ok);
}

TEST(JsonTest, ParsingRunsTheManifestValidationGate)
{
    RunManifest run = makeRun();
    run.results.push_back(makeResult("duplicate", 1.0, "a"));
    run.results.push_back(makeResult("duplicate", 1.0, "b"));

    RunManifest parsed;
    const auto result = json::parseRun(json::toJson(run), parsed);
    EXPECT_FALSE(result.ok);
    EXPECT_TRUE(std::any_of(result.errors.begin(), result.errors.end(),
                            [](const std::string& message) { return message.find("duplicate") != std::string::npos; }));
}

TEST(JsonTest, AnUnknownEnumNameIsReported)
{
    RunManifest run = makeRun();
    run.results.push_back(makeResult("a", 5.0, "digest-a"));
    std::string text = json::toJson(run);
    const auto at = text.find("\"BitExact\"");
    ASSERT_NE(at, std::string::npos);
    text.replace(at, std::string("\"BitExact\"").size(), "\"Sideways\"");

    RunManifest parsed;
    EXPECT_FALSE(json::parseRun(text, parsed).ok);
}

TEST(JsonTest, RunFileWriteIsAtomicAndReadsBack)
{
    const std::filesystem::path dir = scratchDir("json");
    std::filesystem::create_directories(dir);
    const std::filesystem::path path = dir / "run.json";

    RunManifest run = makeRun();
    run.results.push_back(makeResult("a", 3.0, "digest-a"));

    std::string error;
    ASSERT_TRUE(json::writeRunFile(path.string(), run, error)) << error;
    EXPECT_TRUE(std::filesystem::exists(path));
    EXPECT_FALSE(std::filesystem::exists(path.string() + ".pending"))
        << "the pending file must not survive a successful write";

    RunManifest parsed;
    EXPECT_TRUE(json::readRunFile(path.string(), parsed));
    ASSERT_EQ(parsed.results.size(), 1u);
    EXPECT_EQ(parsed.results[0].scenarioId, "a");

    std::filesystem::remove_all(dir);
}

TEST(JsonTest, ReadingAMissingFileIsReportedNotThrown)
{
    RunManifest parsed;
    EXPECT_FALSE(json::readRunFile("this/path/does/not/exist.json", parsed).ok);
}

// --- corpus fixtures --------------------------------------------------------------------------

namespace
{

/// Write a raw dump of `descriptor` filled with a recognisable ramp, and return its descriptor path.
std::filesystem::path writeDumpFixture(const std::filesystem::path& dir, const std::string& stem,
                                       const SampleDescriptor& descriptor)
{
    std::filesystem::create_directories(dir);
    std::vector<std::uint8_t> payload(static_cast<std::size_t>(descriptor.totalBytes()));
    for (std::size_t index = 0; index < payload.size(); ++index)
    {
        payload[index] = static_cast<std::uint8_t>(index % 251);
    }

    RawDump dump;
    dump.descriptor = descriptor;
    dump.payloadFile = stem + ".anraw";
    dump.sourcePath = "synthetic-for-test";
    dump.sourceFormat = "TEST";
    dump.exporterVersion = "test-exporter 1";

    const std::filesystem::path descriptorPath = dir / (stem + ".json");
    std::string error;
    EXPECT_TRUE(writeRawDump(descriptorPath.string(), dump, payload, error)) << error;
    return descriptorPath;
}

SampleDescriptor corpusDescriptor()
{
    SampleDescriptor d;
    d.width = 16;
    d.height = 8;
    d.channels = 1;
    d.sampleType = SampleType::UInt16;
    d.layout = Layout::Planar;
    // A raw dump is canonical little endian by contract, so a descriptor that says Native cannot
    // be written as one. The exporter enforces that; this fixture has to respect it.
    d.byteOrder = ByteOrder::Little;
    return d;
}

/// A manifest entry pointing at `relative`, with the size and digest of the file actually there.
CorpusEntry entryFor(const std::string& id, const std::filesystem::path& root, const std::string& relative)
{
    CorpusEntry entry;
    entry.id = id;
    entry.relativePath = relative;
    entry.licenseOrProvenance = "maintainer capture, authorized for benchmarking";
    entry.redistribution = "local-only";
    entry.privacyClass = "none";
    entry.sizeBytes = std::filesystem::file_size(root / relative);

    std::ifstream file((root / relative).string(), std::ios::binary);
    Sha256 digest;
    std::vector<char> buffer(64 * 1024);
    while (file.read(buffer.data(), static_cast<std::streamsize>(buffer.size())) || file.gcount() > 0)
    {
        digest.update(buffer.data(), static_cast<std::size_t>(file.gcount()));
    }
    entry.sha256 = digest.hex();
    return entry;
}

} // namespace

// --- multi-frame corpus datasets -----------------------------------------------------------

namespace
{

/// A frame set of `counts` frames per named role, with every frame written as a real dump.
CorpusFrameSet frameSetFor(const std::string& id, const std::filesystem::path& root,
                           const std::vector<std::pair<std::string, int>>& counts)
{
    CorpusFrameSet set;
    set.id = id;
    set.licenseOrProvenance = "maintainer capture, authorized for benchmarking";
    set.redistribution = "local-only";
    set.privacyClass = "none";
    set.expected = corpusDescriptor();

    for (const auto& entry : counts)
    {
        CorpusFrameRole role;
        role.role = entry.first;
        for (int index = 0; index < entry.second; ++index)
        {
            const std::string stem = entry.first + "-" + std::to_string(index);
            writeDumpFixture(root, stem, corpusDescriptor());
            const CorpusEntry member = entryFor(stem, root, stem + ".json");
            CorpusFrame frame;
            frame.relativePath = member.relativePath;
            frame.sha256 = member.sha256;
            frame.sizeBytes = member.sizeBytes;
            role.frames.push_back(frame);
        }
        set.roles.push_back(role);
    }
    return set;
}

} // namespace

TEST(CorpusFrameSetTest, ResolvesEveryFrameOfEveryRoleInOrder)
{
    const auto root = scratchDir("frameset-resolve");
    CorpusManifest manifest;
    manifest.addSet(frameSetFor("carina.red.set", root, {{"lights", 3}, {"darks", 2}}));
    ASSERT_TRUE(manifest.validate().empty());

    CorpusFixtureSource source(manifest, root.string());
    FixtureSet set;
    std::string error;
    ASSERT_TRUE(source.acquireSet("carina.red.set", corpusDescriptor(), set, error)) << error;

    ASSERT_EQ(set.roles.size(), 2u);
    EXPECT_EQ(set.roles[0].name, "lights");
    EXPECT_EQ(set.roles[0].frames.size(), 3u);
    EXPECT_EQ(set.roles[1].name, "darks");
    EXPECT_EQ(set.roles[1].frames.size(), 2u);
    EXPECT_EQ(set.frameCount(), 5u);
    ASSERT_NE(set.role("darks"), nullptr);
    EXPECT_EQ(set.role("darks")->frames.size(), 2u);
    EXPECT_EQ(set.role("flats"), nullptr);
}

TEST(CorpusFrameSetTest, ResolvingASetReadsNoPixels)
{
    // The reference dataset is 232 frames and about 21.7 GB. Materialising it as fixtures would
    // not only be absurd, it would put the loader's own footprint into the pipeline's peak-memory
    // figure. So a set hands over what to
    // read, and the frames are read one at a time by whoever consumes them.
    const auto root = scratchDir("frameset-lazy");
    CorpusManifest manifest;
    manifest.addSet(frameSetFor("lazy.set", root, {{"lights", 4}}));

    CorpusFixtureSource source(manifest, root.string());
    FixtureSet set;
    std::string error;
    ASSERT_TRUE(source.acquireSet("lazy.set", corpusDescriptor(), set, error)) << error;

    for (const FixtureFrame& frame : set.roles[0].frames)
    {
        EXPECT_FALSE(frame.absolutePath.empty());
        EXPECT_EQ(frame.sha256.size(), 64u);
        EXPECT_GT(frame.sizeBytes, 0u);
    }

    // The identity is built from the manifest's recorded digests, so a set has a fixture identity
    // before a single byte of it has been read.
    EXPECT_EQ(set.digest.size(), 64u);
}

TEST(CorpusFrameSetTest, AFrameIsVerifiedWhenItIsRead)
{
    const auto root = scratchDir("frameset-verify");
    CorpusManifest manifest;
    manifest.addSet(frameSetFor("verify.set", root, {{"lights", 2}}));

    CorpusFixtureSource source(manifest, root.string());
    FixtureSet set;
    std::string error;
    ASSERT_TRUE(source.acquireSet("verify.set", corpusDescriptor(), set, error)) << error;

    Fixture fixture;
    ASSERT_TRUE(source.loadFrame(set.roles[0].frames[0], fixture, error)) << error;
    EXPECT_EQ(fixture.bytes.size(), corpusDescriptor().totalBytes());
    EXPECT_EQ(fixture.digest.size(), 64u);
    EXPECT_FALSE(fixture.sourceId.empty());

    // Grow the second frame's descriptor. Caught on size, which is the cheap half of the check.
    {
        std::ofstream file(set.roles[0].frames[1].absolutePath, std::ios::binary | std::ios::app);
        ASSERT_TRUE(file.is_open());
        file << ' ';
    }
    Fixture second;
    EXPECT_FALSE(source.loadFrame(set.roles[0].frames[1], second, error));
    EXPECT_NE(error.find("bytes"), std::string::npos) << error;
}

TEST(CorpusFrameSetTest, AFrameRewrittenWithoutChangingItsSizeIsStillRefused)
{
    // The failure a recorded digest exists for, and the one a size check cannot see: a frame
    // re-exported or replaced in place, same name, same length, different data. Benchmarking it
    // would attribute a number to data nobody else has.
    const auto root = scratchDir("frameset-samesize");
    CorpusManifest manifest;
    manifest.addSet(frameSetFor("samesize.set", root, {{"lights", 1}}));

    CorpusFixtureSource source(manifest, root.string());
    FixtureSet set;
    std::string error;
    ASSERT_TRUE(source.acquireSet("samesize.set", corpusDescriptor(), set, error)) << error;

    const std::string path = set.roles[0].frames[0].absolutePath;
    const auto sizeBefore = std::filesystem::file_size(path);
    {
        // Flip one byte in place, well inside the file, so the length is untouched.
        std::fstream file(path, std::ios::binary | std::ios::in | std::ios::out);
        ASSERT_TRUE(file.is_open());
        file.seekg(static_cast<std::streamoff>(sizeBefore / 2));
        char original = 0;
        ASSERT_TRUE(file.read(&original, 1).good());
        const char flipped = static_cast<char>(original == 'a' ? 'b' : 'a');
        file.seekp(static_cast<std::streamoff>(sizeBefore / 2));
        ASSERT_TRUE(file.write(&flipped, 1).good());
    }
    ASSERT_EQ(std::filesystem::file_size(path), sizeBefore) << "the test changed the size after all";

    Fixture fixture;
    EXPECT_FALSE(source.loadFrame(set.roles[0].frames[0], fixture, error));
    EXPECT_NE(error.find("digest"), std::string::npos) << error;
}

TEST(CorpusFrameSetTest, AnAbsentSetSkipsByNameRatherThanPartially)
{
    // Half a calibration set is not a smaller measurement, it is a different one. A set with any
    // frame missing has to be refused whole.
    const auto root = scratchDir("frameset-absent");
    CorpusFrameSet set = frameSetFor("partial.set", root, {{"lights", 3}});
    set.roles[0].frames[1].relativePath = "not-exported.json";

    CorpusManifest manifest;
    manifest.addSet(set);
    CorpusFixtureSource source(manifest, root.string());

    FixtureSet resolved;
    std::string error;
    EXPECT_FALSE(source.acquireSet("partial.set", corpusDescriptor(), resolved, error));
    EXPECT_NE(error.find("not-exported.json"), std::string::npos) << error;
}

TEST(CorpusFrameSetTest, AnEmptyRoleOrDuplicateIdIsRejected)
{
    CorpusFrameSet set;
    set.id = "broken.set";
    set.licenseOrProvenance = "test";
    set.redistribution = "local-only";
    set.privacyClass = "none";
    CorpusFrameRole role;
    role.role = "lights";
    set.roles.push_back(role);
    EXPECT_FALSE(set.validate().empty()) << "a role with no frames was accepted";

    CorpusFrame frame;
    frame.relativePath = "a.json";
    frame.sha256 = std::string(64, 'a');
    frame.sizeBytes = 10;
    set.roles[0].frames.push_back(frame);
    EXPECT_TRUE(set.validate().empty());

    CorpusManifest manifest;
    manifest.addSet(set);
    manifest.addSet(set);
    EXPECT_FALSE(manifest.validate().empty()) << "a duplicate set id was accepted";
}

TEST(JsonTest, AFrameSetRoundTripsWithEveryMember)
{
    CorpusManifest original;
    CorpusFrameSet set;
    set.id = "carina.red.set";
    set.tags = {"mono", "uint16"};
    set.licenseOrProvenance = "maintainer capture";
    set.redistribution = "local-only";
    set.privacyClass = "none";
    set.expected = corpusDescriptor();
    set.representativeFor = {"pipelines"};
    for (const char* name : {"lights", "flats"})
    {
        CorpusFrameRole role;
        role.role = name;
        for (int index = 0; index < 3; ++index)
        {
            CorpusFrame frame;
            frame.relativePath = std::string(name) + "/" + std::to_string(index) + ".json";
            frame.sha256 = std::string(64, static_cast<char>('a' + index));
            frame.sizeBytes = 1000u + static_cast<std::uint64_t>(index);
            role.frames.push_back(frame);
        }
        set.roles.push_back(role);
    }
    original.addSet(set);

    CorpusManifest parsed;
    ASSERT_TRUE(json::parseCorpus(json::toJson(original), parsed));
    ASSERT_EQ(parsed.sets().size(), 1u);
    const CorpusFrameSet& round = parsed.sets()[0];
    EXPECT_EQ(round.id, "carina.red.set");
    EXPECT_EQ(round.frameCount(), 6u);
    EXPECT_EQ(round.totalBytes(), set.totalBytes());
    ASSERT_EQ(round.roles.size(), 2u);
    EXPECT_EQ(round.roles[1].role, "flats");
    EXPECT_EQ(round.roles[1].frames[2].sizeBytes, 1002u);
    EXPECT_EQ(round.identityDigest(), set.identityDigest());
}

TEST(CorpusFrameSetTest, IdentityChangesWhenAnyMemberDoes)
{
    CorpusFrameSet set;
    set.id = "identity.set";
    CorpusFrameRole role;
    role.role = "lights";
    for (int index = 0; index < 3; ++index)
    {
        CorpusFrame frame;
        frame.relativePath = std::to_string(index) + ".json";
        frame.sha256 = std::string(64, static_cast<char>('a' + index));
        frame.sizeBytes = 10;
        role.frames.push_back(frame);
    }
    set.roles.push_back(role);

    const std::string before = set.identityDigest();
    CorpusFrameSet reordered = set;
    std::swap(reordered.roles[0].frames[0], reordered.roles[0].frames[2]);
    EXPECT_NE(reordered.identityDigest(), before) << "frame order does not affect set identity";

    CorpusFrameSet replaced = set;
    replaced.roles[0].frames[1].sha256 = std::string(64, 'f');
    EXPECT_NE(replaced.identityDigest(), before) << "a replaced frame does not affect set identity";
}

TEST(CorpusFixtureTest, ReadsRealDataThroughItsRawDump)
{
    const auto root = scratchDir("corpus-read");
    writeDumpFixture(root, "frame", corpusDescriptor());

    CorpusManifest manifest;
    manifest.add(entryFor("carina.red", root, "frame.json"));
    CorpusFixtureSource source(manifest, root.string());

    Fixture fixture;
    std::string error;
    ASSERT_TRUE(source.acquire("carina.red", 0, {}, fixture, error)) << error;
    EXPECT_EQ(fixture.sourceId, "carina.red");
    EXPECT_EQ(fixture.descriptor.width, 16u);
    EXPECT_EQ(fixture.descriptor.sampleType, SampleType::UInt16);
    EXPECT_EQ(fixture.bytes.size(), corpusDescriptor().totalBytes());
    EXPECT_FALSE(fixture.digest.empty());
    EXPECT_EQ(fixture.generatorVersion, 0u) << "nothing generated a corpus fixture";

    std::filesystem::remove_all(root);
}

TEST(CorpusFixtureTest, AnAbsentDatasetIsReportedByNameRatherThanSubstituted)
{
    const auto root = scratchDir("corpus-absent");
    std::filesystem::create_directories(root);

    CorpusManifest manifest;
    CorpusEntry entry;
    entry.id = "eagle.ha";
    entry.relativePath = "not-copied-to-this-machine.json";
    entry.licenseOrProvenance = "maintainer capture";
    entry.redistribution = "local-only";
    manifest.add(entry);

    CorpusFixtureSource source(manifest, root.string());
    Fixture fixture;
    std::string error;
    EXPECT_FALSE(source.acquire("eagle.ha", 0, {}, fixture, error));
    EXPECT_NE(error.find("eagle.ha"), std::string::npos) << "the skip must name the dataset: " << error;
    EXPECT_TRUE(fixture.empty());

    std::filesystem::remove_all(root);
}

TEST(CorpusFixtureTest, AReplacedFileIsCaughtByItsRecordedDigest)
{
    const auto root = scratchDir("corpus-tampered");
    writeDumpFixture(root, "frame", corpusDescriptor());

    CorpusManifest manifest;
    CorpusEntry entry = entryFor("carina.red", root, "frame.json");
    entry.sha256 = std::string(64, 'a'); // what the manifest recorded, not what is on disk
    manifest.add(entry);

    CorpusFixtureSource source(manifest, root.string());
    Fixture fixture;
    std::string error;
    EXPECT_FALSE(source.acquire("carina.red", 0, {}, fixture, error));

    std::filesystem::remove_all(root);
}

TEST(CorpusFixtureTest, AManifestThatDisagreesWithItsDataIsARefusal)
{
    const auto root = scratchDir("corpus-drift");
    writeDumpFixture(root, "frame", corpusDescriptor());

    CorpusManifest manifest;
    CorpusEntry entry = entryFor("carina.red", root, "frame.json");
    entry.expected = corpusDescriptor();
    entry.expected.width = 999; // the manifest claims a shape the dump does not have
    manifest.add(entry);

    CorpusFixtureSource source(manifest, root.string());
    Fixture fixture;
    std::string error;
    EXPECT_FALSE(source.acquire("carina.red", 0, {}, fixture, error));
    EXPECT_NE(error.find("999"), std::string::npos) << error;

    std::filesystem::remove_all(root);
}

TEST(CorpusFixtureTest, AScenarioThatAsksForADifferentShapeIsARefusalNotAConversion)
{
    const auto root = scratchDir("corpus-shape");
    writeDumpFixture(root, "frame", corpusDescriptor());

    CorpusManifest manifest;
    manifest.add(entryFor("carina.red", root, "frame.json"));
    CorpusFixtureSource source(manifest, root.string());

    SampleDescriptor asked = corpusDescriptor();
    asked.sampleType = SampleType::Float32; // a scenario declaring the wrong representation

    Fixture fixture;
    std::string error;
    EXPECT_FALSE(source.acquire("carina.red", 0, asked, fixture, error));
    EXPECT_NE(error.find("scenario asks for"), std::string::npos) << error;

    std::filesystem::remove_all(root);
}

TEST(CompositeFixtureTest, RoutesSyntheticAndCorpusIdsToTheirOwnSource)
{
    const auto root = scratchDir("composite");
    writeDumpFixture(root, "frame", corpusDescriptor());

    CorpusManifest manifest;
    manifest.add(entryFor("carina.red", root, "frame.json"));

    CompositeFixtureSource source;
    EXPECT_FALSE(source.hasCorpus());
    source.useCorpus(manifest, root.string());
    EXPECT_TRUE(source.hasCorpus());

    Fixture corpusFixture;
    std::string error;
    ASSERT_TRUE(source.acquire("carina.red", 0, {}, corpusFixture, error)) << error;
    EXPECT_EQ(corpusFixture.generatorVersion, 0u);

    Fixture syntheticFixture;
    const std::string generator = SyntheticFixtureSource::generatorIds().front();
    ASSERT_TRUE(source.acquire(generator, 7, makeDescriptor(), syntheticFixture, error)) << error;
    EXPECT_GT(syntheticFixture.generatorVersion, 0u) << "a generated fixture carries its version";

    std::filesystem::remove_all(root);
}

TEST(CompositeFixtureTest, AnUnknownIdNamesEverySourceItConsulted)
{
    CompositeFixtureSource source;
    Fixture fixture;
    std::string error;

    EXPECT_FALSE(source.acquire("carina.red", 0, {}, fixture, error));
    EXPECT_NE(error.find("no corpus manifest was loaded"), std::string::npos)
        << "the usual cause is a manifest that was never passed to the runner: " << error;

    source.useCorpus(CorpusManifest{}, "");
    EXPECT_FALSE(source.acquire("carina.red", 0, {}, fixture, error));
    EXPECT_NE(error.find("not an entry in the corpus manifest"), std::string::npos) << error;
}

TEST(BasicStatisticsOracleTest, UsesAStableTwoPassReferenceAndCanonicalText)
{
    const std::array<float, 4u> samples{1.0f, 2.0f, 3.0f, 4.0f};
    const std::optional<BasicStatisticsOracle> statistics = computeBasicStatisticsOracle(samples);
    ASSERT_TRUE(statistics.has_value());
    EXPECT_EQ(statistics->count, 4u);
    EXPECT_DOUBLE_EQ(statistics->minimum, 1.0);
    EXPECT_DOUBLE_EQ(statistics->maximum, 4.0);
    EXPECT_DOUBLE_EQ(statistics->mean, 2.5);
    EXPECT_DOUBLE_EQ(statistics->populationVariance, 1.25);
    EXPECT_DOUBLE_EQ(basicStatisticsError(*statistics, *statistics), 0.0);
    EXPECT_EQ(basicStatisticsText(*statistics), "count=4\nmin=1.00000000000000000e+00\nmax=4.00000000000000000e+00\n"
                                                "mean=2.50000000000000000e+00\nvariance=1.25000000000000000e+00\n");
}

TEST(BasicStatisticsOracleTest, RejectsANonFiniteResultInsteadOfLettingMaxHideIt)
{
    BasicStatisticsOracle expected{.count = 1u, .minimum = 1.0, .maximum = 1.0, .mean = 1.0, .populationVariance = 0.0};
    BasicStatisticsOracle actual = expected;
    actual.mean = std::numeric_limits<double>::quiet_NaN();

    EXPECT_EQ(basicStatisticsError(actual, expected), (std::numeric_limits<double>::max)());
}

TEST(BasicStatisticsOracleTest, APeriodicMaskUsesFullImageCoordinatesWithoutCompactingTheRegion)
{
    const std::array<float, 12u> samples{1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f, 9.0f, 10.0f, 11.0f, 12.0f};
    const BasicStatisticsOracleWindow window{.sourceWidth = 4u,
                                             .sourceHeight = 3u,
                                             .originX = 1u,
                                             .originY = 0u,
                                             .width = 3u,
                                             .height = 3u,
                                             .excludedEvery = 4u};

    ASSERT_TRUE(window.isValid());
    EXPECT_FALSE(window.selects(1u, 0u));
    EXPECT_TRUE(window.selects(2u, 0u));
    EXPECT_FALSE(window.selects(0u, 0u));
    const std::optional<BasicStatisticsOracle> statistics = computeBasicStatisticsOracle(samples, window);
    ASSERT_TRUE(statistics.has_value());
    EXPECT_EQ(statistics->count, 6u);
    EXPECT_DOUBLE_EQ(statistics->minimum, 3.0);
    EXPECT_DOUBLE_EQ(statistics->maximum, 11.0);
    EXPECT_DOUBLE_EQ(statistics->mean, 7.0);
    EXPECT_DOUBLE_EQ(statistics->populationVariance, 52.0 / 6.0);
}

TEST(OrderStatisticsOracleTest, APeriodicMaskRanksTheSameSamplesTheBasicOracleSelects)
{
    const std::array<float, 12u> samples{1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f, 9.0f, 10.0f, 11.0f, 12.0f};
    const BasicStatisticsOracleWindow window{.sourceWidth = 4u,
                                             .sourceHeight = 3u,
                                             .originX = 1u,
                                             .originY = 0u,
                                             .width = 3u,
                                             .height = 3u,
                                             .excludedEvery = 4u};
    // The selected samples are 3, 4, 6, 8, 10 and 11: the median is halfway between 6 and 8, and
    // the deviations from it are 4, 3, 1, 1, 3 and 4.
    const std::optional<OrderStatisticsOracle> statistics = computeOrderStatisticsOracle(samples, window);
    ASSERT_TRUE(statistics.has_value());
    EXPECT_EQ(statistics->count, 6u);
    EXPECT_DOUBLE_EQ(statistics->median, 7.0);
    EXPECT_DOUBLE_EQ(statistics->medianAbsoluteDeviation, 3.0);
}

TEST(OrderStatisticsOracleTest, TheClippedOracleDiscardsAnOutlierAndReportsWhatItKept)
{
    std::vector<float> samples;
    for (std::uint32_t index = 0u; index < 99u; ++index)
    {
        samples.push_back(static_cast<float>(index));
    }
    samples.push_back(100000.0f);
    const std::optional<SigmaClippedOracle> clipped = computeSigmaClippedOracle(samples, 3.0, 3.0, 8u);
    ASSERT_TRUE(clipped.has_value());
    EXPECT_EQ(clipped->retained, 99u);
    EXPECT_DOUBLE_EQ(clipped->location, 49.0);
    EXPECT_DOUBLE_EQ(clipped->mean, 49.0);
    EXPECT_NEAR(clipped->standardDeviation, std::sqrt((99.0 * 99.0 - 1.0) / 12.0), 1e-12);
    EXPECT_NEAR(clipped->scale, 25.0 * 1.4826022185056018, 1e-12);

    SigmaClippedOracle drifted = *clipped;
    drifted.location += 1.0;
    EXPECT_GT(sigmaClippedError(drifted, *clipped), 0.0);
    drifted.retained += 1u;
    EXPECT_EQ(sigmaClippedError(drifted, *clipped), (std::numeric_limits<double>::max)());
    EXPECT_NE(sigmaClippedText(*clipped).find("retained=99"), std::string::npos);
}

TEST(CapturedStatisticsTest, EachCodeIsDividedByItsFullScaleOnceInFloat32)
{
    const std::array<std::uint16_t, 6u> codes{0u, 1u, 4u, 32768u, 65532u, 65535u};
    std::vector<std::uint8_t> bytes;
    for (const std::uint16_t code : codes)
    {
        bytes.push_back(static_cast<std::uint8_t>(code & 0xffu));
        bytes.push_back(static_cast<std::uint8_t>(code >> 8u));
    }
    const std::vector<float> samples = makeUnitIntervalCapture(bytes);
    ASSERT_EQ(samples.size(), codes.size());
    for (std::size_t index = 0u; index < codes.size(); ++index)
    {
        EXPECT_EQ(std::bit_cast<std::uint32_t>(samples[index]),
                  std::bit_cast<std::uint32_t>(static_cast<float>(codes[index]) / 65535.0f))
            << "code " << codes[index];
    }
    EXPECT_EQ(samples.front(), 0.0f);
    EXPECT_EQ(samples.back(), 1.0f);
}

TEST(CapturedStatisticsTest, CountingTheCodesRanksExactlyWhatSortingTheSamplesRanks)
{
    // Quantized codes the way a fourteen-bit sensor writes them, a few codes off that grid, and both
    // ends of the range, over odd and even populations: the counted reference must be the sorted one
    // to the bit, because the captured rows are scored against it in place of sorting.
    std::mt19937 generator(20260929u);
    std::normal_distribution<double> sky(2400.0, 180.0);
    for (const std::size_t count :
         {std::size_t{1}, std::size_t{2}, std::size_t{7}, std::size_t{4096}, std::size_t{40001}})
    {
        std::vector<std::uint8_t> bytes;
        for (std::size_t index = 0u; index < count; ++index)
        {
            std::uint32_t code = static_cast<std::uint32_t>(std::clamp(sky(generator), 0.0, 65535.0)) & ~3u;
            if (index % 97u == 5u)
            {
                code += 1u;
            }
            if (index % 1009u == 11u)
            {
                code = index % 2u == 0u ? 65535u : 0u;
            }
            bytes.push_back(static_cast<std::uint8_t>(code & 0xffu));
            bytes.push_back(static_cast<std::uint8_t>(code >> 8u));
        }
        const std::optional<OrderStatisticsOracle> counted = computeCapturedOrderStatisticsOracle(bytes);
        const std::optional<OrderStatisticsOracle> sorted =
            computeOrderStatisticsOracle(makeUnitIntervalCapture(bytes));
        ASSERT_TRUE(counted.has_value()) << count;
        ASSERT_TRUE(sorted.has_value()) << count;
        EXPECT_EQ(counted->count, sorted->count) << count;
        EXPECT_EQ(std::bit_cast<std::uint64_t>(counted->median), std::bit_cast<std::uint64_t>(sorted->median)) << count;
        EXPECT_EQ(std::bit_cast<std::uint64_t>(counted->medianAbsoluteDeviation),
                  std::bit_cast<std::uint64_t>(sorted->medianAbsoluteDeviation))
            << count;
    }
}

TEST(CapturedStatisticsTest, AnEmptyOrTornCaptureHasNoReference)
{
    EXPECT_FALSE(computeCapturedOrderStatisticsOracle(std::span<const std::uint8_t>{}).has_value());
    const std::array<std::uint8_t, 3u> torn{1u, 2u, 3u};
    EXPECT_FALSE(computeCapturedOrderStatisticsOracle(torn).has_value());
    EXPECT_TRUE(makeUnitIntervalCapture(torn).empty());
}

TEST(CapturedStatisticsTest, TheCompleteResultIsScoredOnTheFieldsBothReferencesDefine)
{
    const BasicStatisticsOracle basic{
        .count = 10u, .minimum = 0.125, .maximum = 0.875, .mean = 0.5, .populationVariance = 0.0625};
    const OrderStatisticsOracle order{.count = 10u, .median = 0.5, .medianAbsoluteDeviation = 0.25};
    CompleteStatistics complete{.count = 10u,
                                .minimum = 0.125,
                                .maximum = 0.875,
                                .mean = 0.5,
                                .populationVariance = 0.0625,
                                .median = 0.5,
                                .medianAbsoluteDeviation = 0.25,
                                .averageAbsoluteDeviation = 3.0,
                                .sumOfSquares = 4.0,
                                .meanOfSquares = 5.0,
                                .biweightMidvariance = 6.0,
                                .backgroundNoise = 7.0,
                                .location = 8.0,
                                .scale = 9.0};
    // The estimators neither reference defines do not move the score; the text still carries them.
    EXPECT_EQ(completeStatisticsError(complete, basic, order), 0.0);
    const std::string text = completeStatisticsText(complete);
    EXPECT_NE(text.find("count=10"), std::string::npos);
    EXPECT_NE(text.find("bwmv="), std::string::npos);
    EXPECT_NE(text.find("scale="), std::string::npos);
    complete.scale = 10.0;
    EXPECT_NE(completeStatisticsText(complete), text);

    complete.median = 0.5 + 1e-3;
    EXPECT_NEAR(completeStatisticsError(complete, basic, order), 1e-3, 1e-15);
    complete.median = 0.5;
    complete.mean = 0.5 + 2e-3;
    EXPECT_NEAR(completeStatisticsError(complete, basic, order), 2e-3, 1e-15);
    complete.count = 9u;
    EXPECT_EQ(completeStatisticsError(complete, basic, order), (std::numeric_limits<double>::max)());
}

TEST(MedianFilterOracleTest, TruncatedBordersAndTheirEvenPopulationsArePartOfTheOracle)
{
    const std::array<float, 9u> samples{1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f, 9.0f};
    const std::vector<MedianProbe> probes = computeMedianFilterOracle(samples, 3u, 3u, 1u, 1u);
    ASSERT_EQ(probes.size(), samples.size());
    EXPECT_EQ(probes.front().x, 0u);
    EXPECT_EQ(probes.front().y, 0u);
    EXPECT_DOUBLE_EQ(probes.front().value, 3.0);
    EXPECT_DOUBLE_EQ(probes[4].value, 5.0);
    EXPECT_DOUBLE_EQ(probes.back().value, 7.0);

    std::array<float, 9u> produced{};
    for (std::size_t index = 0u; index < probes.size(); ++index)
    {
        produced[index] = static_cast<float>(probes[index].value);
    }
    EXPECT_DOUBLE_EQ(medianFilterError(produced, 3u, probes), 0.0);
}

TEST(MedianFilterOracleTest, AStructuringElementRanksTheCellsItMarksInsideTheImage)
{
    const std::array<float, 9u> samples{1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f, 9.0f};
    const std::array<std::uint8_t, 9u> whole{1u, 1u, 1u, 1u, 1u, 1u, 1u, 1u, 1u};
    const std::vector<MedianProbe> square = computeMedianFilterOracle(samples, 3u, 3u, 1u, 1u);
    const std::vector<MedianProbe> element = computeFootprintMedianOracle(samples, 3u, 3u, 1u, whole, 1u);
    ASSERT_EQ(element.size(), square.size());
    for (std::size_t index = 0u; index < square.size(); ++index)
    {
        EXPECT_DOUBLE_EQ(element[index].value, square[index].value) << index;
    }

    // A cross: the centre sample and its four neighbours, the corners left out. At the centre that is 2, 4, 5, 6
    // and 8; at the top-left corner the three members inside, 1, 2 and 4, rank to 2.
    const std::array<std::uint8_t, 9u> cross{0u, 1u, 0u, 1u, 1u, 1u, 0u, 1u, 0u};
    const std::vector<MedianProbe> crossed = computeFootprintMedianOracle(samples, 3u, 3u, 1u, cross, 1u);
    ASSERT_EQ(crossed.size(), samples.size());
    EXPECT_DOUBLE_EQ(crossed[4].value, 5.0);
    EXPECT_DOUBLE_EQ(crossed.front().value, 2.0);

    EXPECT_TRUE(
        computeFootprintMedianOracle(samples, 3u, 3u, 1u, std::span<const std::uint8_t>(cross).first(8u), 1u).empty());
}

TEST(NoiseReductionOracleTest, TheIdentityOracleIsWhatARoundTripHasToReproduce)
{
    const std::array<float, 6u> samples{0.5f, 0.25f, 0.75f, 1.5f, -2.0f, 0.0f};
    const std::vector<PlaneProbe> probes = computeIdentityOracle(samples, 3u, 2u, 2u);
    ASSERT_EQ(probes.size(), 3u);
    EXPECT_EQ(probes[0].x, 0u);
    EXPECT_EQ(probes[0].y, 0u);
    EXPECT_DOUBLE_EQ(probes[0].value, 0.5);
    EXPECT_EQ(probes[1].x, 2u);
    EXPECT_EQ(probes[1].y, 0u);
    EXPECT_DOUBLE_EQ(probes[1].value, 0.75);
    EXPECT_EQ(probes[2].x, 1u);
    EXPECT_EQ(probes[2].y, 1u);
    EXPECT_DOUBLE_EQ(probes[2].value, -2.0);
    EXPECT_DOUBLE_EQ(planeProbeError(samples, 3u, probes), 0.0);

    // A picture the stride cannot walk produces no probes rather than an answer.
    EXPECT_TRUE(computeIdentityOracle(samples, 3u, 2u, 0u).empty());
    EXPECT_TRUE(computeIdentityOracle(samples, 4u, 2u, 1u).empty());
    EXPECT_EQ(planeProbeError(samples, 3u, {}), (std::numeric_limits<double>::max)());
}

TEST(NoiseReductionOracleTest, ABilateralWeightIsTwoGaussiansAndAFlatSurfaceSurvivesIt)
{
    const std::array<float, 25u> flat{0.375f, 0.375f, 0.375f, 0.375f, 0.375f, 0.375f, 0.375f, 0.375f, 0.375f,
                                      0.375f, 0.375f, 0.375f, 0.375f, 0.375f, 0.375f, 0.375f, 0.375f, 0.375f,
                                      0.375f, 0.375f, 0.375f, 0.375f, 0.375f, 0.375f, 0.375f};
    const std::vector<PlaneProbe> flatProbes = computeBilateralOracle(flat, 5u, 5u, 1u, 1.0, 0.1, 1u);
    ASSERT_EQ(flatProbes.size(), flat.size());
    for (const PlaneProbe& probe : flatProbes)
    {
        EXPECT_NEAR(probe.value, 0.375, 1.0e-12);
    }

    // A range sigma far below the step keeps a hard edge exactly where it was, which is the whole
    // difference between this and an ordinary blur.
    std::array<float, 25u> step{};
    for (std::uint32_t y = 0u; y < 5u; ++y)
    {
        for (std::uint32_t x = 0u; x < 5u; ++x)
        {
            step[static_cast<std::size_t>(y) * 5u + x] = x < 2u ? 0.0f : 1.0f;
        }
    }
    const std::vector<PlaneProbe> edge = computeBilateralOracle(step, 5u, 5u, 1u, 1.0, 1.0e-3, 1u);
    ASSERT_EQ(edge.size(), step.size());
    EXPECT_NEAR(edge[5u * 2u + 1u].value, 0.0, 1.0e-9);
    EXPECT_NEAR(edge[5u * 2u + 2u].value, 1.0, 1.0e-9);

    EXPECT_TRUE(computeBilateralOracle(flat, 5u, 5u, 1u, 0.0, 0.1, 1u).empty());
    EXPECT_TRUE(computeBilateralOracle(flat, 5u, 5u, 1u, 1.0, 0.0, 1u).empty());
}

TEST(NoiseReductionOracleTest, ChambollesIterationLeavesAFlatSurfaceAloneAndLowersAStepsVariation)
{
    // The fixed point of the iteration over a constant plane is that constant, whatever the weight,
    // because its total variation is already zero and there is nothing to trade fidelity for.
    std::array<float, 64u> flat{};
    flat.fill(0.375f);
    const std::vector<PlaneProbe> quiet = computeChambolleOracle(flat, 8u, 8u, 0.1, 40u, 1u);
    ASSERT_EQ(quiet.size(), flat.size());
    for (const PlaneProbe& probe : quiet)
    {
        EXPECT_NEAR(probe.value, 0.375, 1.0e-9);
    }

    // Over a noisy step the answer keeps the step and loses the noise, which is the whole reason the
    // functional is the one it is.
    std::array<float, 64u> noisy{};
    std::uint32_t state = 12345u;
    for (std::uint32_t y = 0u; y < 8u; ++y)
    {
        for (std::uint32_t x = 0u; x < 8u; ++x)
        {
            state = state * 1103515245u + 12345u;
            const float wobble = 0.08f * (static_cast<float>((state >> 16u) % 1000u) / 1000.0f - 0.5f);
            noisy[static_cast<std::size_t>(y) * 8u + x] = (x < 4u ? 0.25f : 0.75f) + wobble;
        }
    }
    const std::vector<PlaneProbe> denoised = computeChambolleOracle(noisy, 8u, 8u, 0.05, 200u, 1u);
    ASSERT_EQ(denoised.size(), noisy.size());
    EXPECT_NEAR(denoised[8u * 4u + 1u].value, 0.25, 0.03);
    EXPECT_NEAR(denoised[8u * 4u + 6u].value, 0.75, 0.03);

    std::vector<float> answer;
    answer.reserve(denoised.size());
    for (const PlaneProbe& probe : denoised)
    {
        answer.push_back(static_cast<float>(probe.value));
    }
    const double before = totalVariationEnergy(noisy, noisy, 8u, 8u, 20.0);
    const double after = totalVariationEnergy(answer, noisy, 8u, 8u, 20.0);
    EXPECT_LT(after, before);
}

TEST(NoiseReductionOracleTest, TheFunctionalIsTheVariationPlusTheWeightedDistanceFromTheInput)
{
    // A plane compared with itself has no distance term, so the functional is its total variation
    // alone: a single step of one over an eight-wide row is eight unit gradients.
    std::array<float, 64u> step{};
    for (std::uint32_t y = 0u; y < 8u; ++y)
    {
        for (std::uint32_t x = 0u; x < 8u; ++x)
        {
            step[static_cast<std::size_t>(y) * 8u + x] = x < 4u ? 0.0f : 1.0f;
        }
    }
    EXPECT_NEAR(totalVariationEnergy(step, step, 8u, 8u, 4.0), 8.0, 1.0e-9);

    // Moving every sample by a tenth adds the weighted squared distance and nothing else.
    std::array<float, 64u> moved = step;
    for (float& sample : moved)
    {
        sample += 0.1f;
    }
    EXPECT_NEAR(totalVariationEnergy(moved, step, 8u, 8u, 4.0), 8.0 + 0.5 * 4.0 * 64.0 * 0.01, 1.0e-5);
}

TEST(NoiseReductionOracleTest, TheAddedNoiseVarianceIsOfTheDifferenceAndNotOfEitherPlane)
{
    // A picture with structure and a constant added to it: the difference has a mean and no spread,
    // which is what separates this from the variance of either plane.
    std::array<float, 16u> ramp{};
    std::array<float, 16u> shifted{};
    for (std::size_t index = 0u; index < ramp.size(); ++index)
    {
        ramp[index] = static_cast<float>(index) / 16.0f;
        shifted[index] = ramp[index] + 0.25f;
    }
    EXPECT_NEAR(addedNoiseVariance(shifted, ramp), 0.0, 1.0e-12);

    // Alternating plus and minus a tenth has a variance of a hundredth, whatever it was added to.
    std::array<float, 16u> alternating{};
    for (std::size_t index = 0u; index < alternating.size(); ++index)
    {
        alternating[index] = ramp[index] + (index % 2u == 0u ? 0.1f : -0.1f);
    }
    EXPECT_NEAR(addedNoiseVariance(alternating, ramp), 0.01, 1.0e-6);

    EXPECT_NEAR(addedNoiseVariance({}, {}), 0.0, 1.0e-12);
}

TEST(NoiseReductionOracleTest, AnImpulseThresholdNamesOneSampleAndLeavesAStoredZeroAlone)
{
    std::array<float, 25u> plane{};
    plane.fill(0.2f);
    plane[12u] = 0.9f;
    const std::vector<PlaneProbe> probes = computeImpulseOracle(plane, 5u, 5u, 1u, 0.5, 1u);
    ASSERT_EQ(probes.size(), plane.size());
    // The samples are Float32, so the expected values are their Float32 representations rather
    // than the decimal literals they were written as.
    EXPECT_NEAR(probes[12u].value, 0.2, 1.0e-6);
    EXPECT_NEAR(probes[0].value, 0.2, 1.0e-6);
    // Above the deviation nothing is named.
    EXPECT_NEAR(computeImpulseOracle(plane, 5u, 5u, 1u, 0.95, 1u)[12u].value, 0.9, 1.0e-6);

    // A stored zero has no relative deviation, so it survives whatever its neighbourhood says.
    std::array<float, 25u> zeroed{};
    zeroed.fill(0.0f);
    const std::vector<PlaneProbe> quiet = computeImpulseOracle(zeroed, 5u, 5u, 1u, 0.1, 1u);
    for (const PlaneProbe& probe : quiet)
    {
        EXPECT_DOUBLE_EQ(probe.value, 0.0);
    }
    EXPECT_NE(planeProbeText(plane, 5u, probes).find("probes=25"), std::string::npos);
}

TEST(NoiseReductionOracleTest, AverageNeutralUsesAllThreePlanarChannels)
{
    const std::array<float, 12u> rgb{0.2f, 0.8f, 0.4f, 0.1f, 0.9f, 0.2f, 0.7f, 0.1f, 0.4f, 0.6f, 0.6f, 0.3f};
    const std::vector<RgbProbe> probes = computeAverageNeutralOracle(rgb, 2u, 2u, 1u);
    ASSERT_EQ(probes.size(), 4u);
    EXPECT_NEAR(probes[0u].value[0u], 0.2, 1.0e-6);
    EXPECT_NEAR(probes[0u].value[1u], 0.3, 1.0e-6);
    EXPECT_NEAR(probes[0u].value[2u], 0.4, 1.0e-6);

    std::array<float, 12u> produced{};
    for (std::size_t pixel = 0u; pixel < probes.size(); ++pixel)
    {
        for (std::size_t channel = 0u; channel < 3u; ++channel)
        {
            produced[channel * 4u + pixel] = static_cast<float>(probes[pixel].value[channel]);
        }
    }
    EXPECT_LE(rgbProbeError(produced, 2u, 2u, probes), std::numeric_limits<float>::epsilon());
    EXPECT_NE(rgbProbeText(produced, 2u, 2u, probes).find("probes=4"), std::string::npos);
    EXPECT_TRUE(computeAverageNeutralOracle(rgb, 2u, 2u, 0u).empty());
}

TEST(NoiseReductionOracleTest, HorizontalBandingLevelsRowsAgainstTheirOwnMedians)
{
    const std::array<float, 12u> rows{10.0f, 11.0f, 12.0f, 13.0f, 20.0f, 21.0f,
                                      22.0f, 23.0f, 30.0f, 31.0f, 32.0f, 33.0f};
    const std::vector<PlaneProbe> probes = computeHorizontalBandingOracle(rows, 4u, 3u, 1u);
    ASSERT_EQ(probes.size(), rows.size());
    for (std::uint32_t y = 0u; y < 3u; ++y)
    {
        for (std::uint32_t x = 0u; x < 4u; ++x)
        {
            EXPECT_DOUBLE_EQ(probes[static_cast<std::size_t>(y) * 4u + x].value, 30.0 + x);
        }
    }
    std::array<float, 12u> produced{};
    for (std::size_t index = 0u; index < probes.size(); ++index)
    {
        produced[index] = static_cast<float>(probes[index].value);
    }
    EXPECT_DOUBLE_EQ(planeProbeError(produced, 4u, probes), 0.0);
}

TEST(HistogramOracleTest, TheFullScaleReferenceGivesEverySixteenBitCodeABinOfItsOwn)
{
    const std::array<std::uint16_t, 5u> codes{0u, 7u, 7u, 65535u, 7u};
    const std::optional<HistogramOracle> histogram = computeFullScaleHistogramOracle(codes, 65536u);
    ASSERT_TRUE(histogram.has_value());
    EXPECT_EQ(histogram->counted, 5u);
    EXPECT_EQ(histogram->binCount, 65536u);
    EXPECT_EQ(histogram->largestBin, 3u);
    EXPECT_EQ(histogram->largestBinIndex, 7u);
    // Any bin count but the code space would not be one code per bin, so it is refused rather than
    // answered under a mapping the scenario did not ask for.
    EXPECT_FALSE(computeFullScaleHistogramOracle(codes, 256u).has_value());
    EXPECT_FALSE(computeFullScaleHistogramOracle(std::span<const std::uint16_t>{}, 65536u).has_value());

    HistogramOracle drifted = *histogram;
    EXPECT_DOUBLE_EQ(histogramError(drifted, *histogram), 0.0);
    drifted.checksum += 1u;
    EXPECT_EQ(histogramError(drifted, *histogram), (std::numeric_limits<double>::max)());
    EXPECT_NE(histogramText(*histogram).find("counted=5"), std::string::npos);
}

TEST(HistogramOracleTest, AMaskedRegionCountsTheClosedIntervalAndLeavesTheRestOutOfEveryBin)
{
    const std::array<float, 12u> samples{-1.0f, 0.05f, 0.15f, 2.0f,  0.25f, 0.35f,
                                         0.45f, 0.55f, 0.65f, 0.75f, 0.85f, 0.95f};
    const BasicStatisticsOracleWindow window{.sourceWidth = 4u,
                                             .sourceHeight = 3u,
                                             .originX = 0u,
                                             .originY = 0u,
                                             .width = 4u,
                                             .height = 3u,
                                             .excludedEvery = 0u};
    std::vector<std::uint64_t> counts;
    const std::optional<HistogramOracle> histogram =
        computeWindowedHistogramOracle(samples, window, 0.0, 1.0, 10u, counts);
    ASSERT_TRUE(histogram.has_value());
    // Ten of the twelve samples sit inside the closed interval, one below it and one above.
    EXPECT_EQ(histogram->counted, 10u);
    EXPECT_EQ(counts.size(), 10u);
    for (const std::uint64_t count : counts)
    {
        EXPECT_EQ(count, 1u);
    }
    EXPECT_EQ(histogramChecksum(counts), histogram->checksum);

    // The interpolated quantile of a flat distribution is the quantile itself, to bin resolution.
    EXPECT_NEAR(histogramOracleQuantile(counts, 0.0, 1.0, 0.5), 0.5, 1e-12);
    EXPECT_DOUBLE_EQ(histogramOracleQuantile(counts, 0.0, 1.0, 0.0), 0.0);
    EXPECT_DOUBLE_EQ(histogramOracleQuantile(counts, 0.0, 1.0, 1.0), 1.0);

    EXPECT_FALSE(computeWindowedHistogramOracle(samples, window, 1.0, 0.0, 10u, counts).has_value());
    EXPECT_FALSE(computeWindowedHistogramOracle(samples, window, 0.0, 1.0, 0u, counts).has_value());
}

TEST(BilinearResampleOracleTest, UsesTheHalfPixelConventionAndClampBorder)
{
    const std::array<float, 4u> source{1.0f, 2.0f, 3.0f, 5.0f};
    EXPECT_DOUBLE_EQ(bilinearResizeCoordinate(0u, 2u, 4u), -0.25);
    const std::vector<BilinearResampleProbe> probes = computeBilinearResizeOracle(source, 2u, 2u, 3u, 3u, 1u);
    ASSERT_EQ(probes.size(), 9u);
    EXPECT_DOUBLE_EQ(probes.front().value, 1.0);
    EXPECT_DOUBLE_EQ(probes[4].value, 2.75);
    EXPECT_DOUBLE_EQ(probes.back().value, 5.0);
    std::array<float, 9u> produced{};
    for (std::size_t index = 0u; index < produced.size(); ++index)
    {
        produced[index] = static_cast<float>(probes[index].value);
    }
    EXPECT_LT(bilinearResizeError(produced, 3u, probes), 1e-6);
}

TEST(GammaTransferOracleTest, RaisesEachProbedSampleToTheReciprocalGamma)
{
    const std::array<float, 6u> samples{0.0f, 0.25f, 0.5f, 1.0f, 0.75f, 0.125f};
    const std::vector<GammaTransferProbe> probes = computeGammaTransferOracle(samples, 2.0, 2u);
    ASSERT_EQ(probes.size(), 3u);
    EXPECT_EQ(probes[0].index, 0u);
    EXPECT_DOUBLE_EQ(probes[0].value, 0.0);
    EXPECT_EQ(probes[1].index, 2u);
    EXPECT_DOUBLE_EQ(probes[1].value, std::sqrt(0.5));
    EXPECT_EQ(probes[2].index, 4u);
    EXPECT_DOUBLE_EQ(probes[2].value, std::sqrt(0.75));

    std::array<float, 6u> produced{};
    for (const GammaTransferProbe& probe : probes)
    {
        produced[static_cast<std::size_t>(probe.index)] = static_cast<float>(probe.value);
    }
    EXPECT_LT(gammaTransferError(produced, probes), 1e-6);
}

TEST(GammaTransferOracleTest, RefusesAnUnusableRequestAndSkipsSamplesOutsideTheDomain)
{
    const std::array<float, 3u> samples{0.5f, 1.0f, 0.25f};
    EXPECT_TRUE(computeGammaTransferOracle(samples, 0.0, 1u).empty());
    EXPECT_TRUE(computeGammaTransferOracle(samples, 2.2, 0u).empty());
    EXPECT_TRUE(computeGammaTransferOracle(std::span<const float>{}, 2.2, 1u).empty());

    // A negative sample and a not-a-number are outside the transfer domain: the engine answers a
    // quiet not-a-number there, which cannot be compared against a tolerance, so neither is probed
    // rather than being probed and forgiven.
    const std::array<float, 4u> mixed{-1.0f, 0.25f, std::numeric_limits<float>::quiet_NaN(), 1.0f};
    const std::vector<GammaTransferProbe> probes = computeGammaTransferOracle(mixed, 2.0, 1u);
    ASSERT_EQ(probes.size(), 2u);
    EXPECT_EQ(probes[0].index, 1u);
    EXPECT_EQ(probes[1].index, 3u);

    // A produced plane that does not cover a probe, or that holds a non-finite result where the
    // oracle holds a finite one, is unusable rather than merely inaccurate.
    const std::array<float, 2u> tooShort{0.5f, 0.5f};
    EXPECT_EQ(gammaTransferError(tooShort, probes), (std::numeric_limits<double>::max)());
    const std::array<float, 4u> nonFinite{0.0f, std::numeric_limits<float>::infinity(), 0.0f, 1.0f};
    EXPECT_EQ(gammaTransferError(nonFinite, probes), (std::numeric_limits<double>::max)());
}

TEST(GammaTransferOracleTest, FusedLinearCurveOracleIsIndependentAndChecksItsShape)
{
    const std::array<float, 5u> samples{0.0f, 0.0625f, 0.25f, 1.0f, -1.0f};
    const std::vector<GammaTransferProbe> probes = computeGammaLinearCurveOracle(samples, 2.0, 0.5, 0.25, 1u);
    ASSERT_EQ(probes.size(), 4u);
    EXPECT_DOUBLE_EQ(probes[0].value, 0.0);
    EXPECT_DOUBLE_EQ(probes[1].value, 0.125);
    EXPECT_DOUBLE_EQ(probes[2].value, 0.25);
    EXPECT_DOUBLE_EQ(probes[3].value, 1.0);
    EXPECT_TRUE(computeGammaLinearCurveOracle(samples, 2.0, 0.0, 0.5, 1u).empty());
    EXPECT_TRUE(computeGammaLinearCurveOracle(samples, 2.0, 0.5, 2.0, 1u).empty());
}

TEST(ColorTransformOracleTest, ReachesThePublishedPerceptualValuesForKnownColours)
{
    // White, black, mid grey and pure red, in the packing the runner writes.
    const std::array<float, 4u> red{1.0f, 0.0f, 0.5f, 1.0f};
    const std::array<float, 4u> green{1.0f, 0.0f, 0.5f, 0.0f};
    const std::array<float, 4u> blue{1.0f, 0.0f, 0.5f, 0.0f};
    const std::vector<SrgbToLabProbe> probes = computeSrgbToLabOracle(red, green, blue, 1u);
    ASSERT_EQ(probes.size(), 4u);

    // White reaches the reference white to the accuracy the published matrix itself carries: its
    // luminance row sums to 1.0000001 rather than to one, because the standard prints seven
    // decimals. That rounding is part of the independent answer rather than something to correct.
    EXPECT_NEAR(probes[0].lightness, 1.0, 1e-6);
    EXPECT_NEAR(probes[0].greenRed, 128.0 / 255.0, 1e-6);
    EXPECT_NEAR(probes[0].blueYellow, 128.0 / 255.0, 1e-6);

    EXPECT_NEAR(probes[1].lightness, 0.0, 1e-9);
    EXPECT_NEAR(probes[1].greenRed, 128.0 / 255.0, 1e-9);

    EXPECT_NEAR(probes[2].lightness * 100.0, 53.3890, 1e-3);
    EXPECT_NEAR(probes[3].lightness * 100.0, 53.2408, 1e-3);
    EXPECT_NEAR(probes[3].greenRed * 255.0 - 128.0, 80.0925, 1e-3);
    EXPECT_NEAR(probes[3].blueYellow * 255.0 - 128.0, 67.2032, 1e-3);

    std::array<float, 4u> lightness{};
    std::array<float, 4u> greenRed{};
    std::array<float, 4u> blueYellow{};
    for (const SrgbToLabProbe& probe : probes)
    {
        const std::size_t at = static_cast<std::size_t>(probe.index);
        lightness[at] = static_cast<float>(probe.lightness);
        greenRed[at] = static_cast<float>(probe.greenRed);
        blueYellow[at] = static_cast<float>(probe.blueYellow);
    }
    EXPECT_LT(srgbToLabError(lightness, greenRed, blueYellow, probes), 1e-6);
}

TEST(ColorTransformOracleTest, RefusesAnUnusableRequestAndSkipsPixelsOutsideTheUnitInterval)
{
    const std::array<float, 3u> channel{0.25f, 0.5f, 0.75f};
    const std::array<float, 2u> shorter{0.25f, 0.5f};
    EXPECT_TRUE(computeSrgbToLabOracle(channel, channel, channel, 0u).empty());
    EXPECT_TRUE(computeSrgbToLabOracle(std::span<const float>{}, channel, channel, 1u).empty());
    EXPECT_TRUE(computeSrgbToLabOracle(channel, shorter, channel, 1u).empty());

    // A value the transfer function is not defined over is skipped rather than probed: an
    // implementation may carry the sign through or answer a not-a-number.
    const std::array<float, 4u> mixed{-0.5f, 0.25f, 1.5f, std::numeric_limits<float>::quiet_NaN()};
    const std::array<float, 4u> inside{0.25f, 0.25f, 0.25f, 0.25f};
    const std::vector<SrgbToLabProbe> probes = computeSrgbToLabOracle(mixed, inside, inside, 1u);
    ASSERT_EQ(probes.size(), 1u);
    EXPECT_EQ(probes[0].index, 1u);

    // A produced plane that does not cover a probe, or that holds a non-finite value, is unusable
    // rather than merely inaccurate.
    const std::array<float, 1u> tooShort{0.5f};
    EXPECT_EQ(srgbToLabError(tooShort, tooShort, tooShort, probes), (std::numeric_limits<double>::max)());
    const std::array<float, 4u> nonFinite{0.0f, std::numeric_limits<float>::infinity(), 0.0f, 0.0f};
    EXPECT_EQ(srgbToLabError(nonFinite, nonFinite, nonFinite, probes), (std::numeric_limits<double>::max)());
}

TEST(DemosaicOracleTest, ExactDigestCoversEveryByteOfEveryPlane)
{
    const std::array<float, 3> red = {1.0f, 2.0f, 3.0f};
    const std::array<float, 3> green = {4.0f, 5.0f, 6.0f};
    const std::array<float, 3> blue = {7.0f, 8.0f, 9.0f};
    Sha256 expected;
    expected.update(red.data(), sizeof(red));
    expected.update(green.data(), sizeof(green));
    expected.update(blue.data(), sizeof(blue));
    EXPECT_EQ(demosaicExactDigest(red, green, blue), expected.hex());

    std::array<float, 3> changed = blue;
    changed.back() = 10.0f;
    EXPECT_NE(demosaicExactDigest(red, green, blue), demosaicExactDigest(red, green, changed));
    EXPECT_TRUE(demosaicExactDigest(red, std::span<const float>{green}.first(2u), blue).empty());
}

TEST(JsonTest, MissingBooleanBuildProvenanceIsNotSilentlyDefaulted)
{
    RunManifest run = makeRun();
    std::string text = json::toJson(run);
    const std::string field = "\"dirty\": false,";
    const auto at = text.find(field);
    ASSERT_NE(at, std::string::npos);
    text.erase(at, field.size());

    RunManifest parsed;
    const auto result = json::parseRun(text, parsed);
    EXPECT_FALSE(result.ok);
    EXPECT_TRUE(std::any_of(result.errors.begin(), result.errors.end(),
                            [](const std::string& error) { return error.find("dirty") != std::string::npos; }));
}

// --- runner harness ---------------------------------------------------------------------------

namespace
{

/// A stand-in for an engine-linked adapter, so the harness can be exercised without the engine.
class FakeAdapter final : public EngineAdapter
{
public:
    int runCalls = 0;
    bool runSucceeds = false;
    std::vector<ScenarioSpec> offered;
    [[nodiscard]] std::string runnerVersion() const override
    {
        return "fake 1";
    }

    void describeEngine(BuildManifest& manifest) const override
    {
        manifest.compiler = "FakeCC 1.0";
        manifest.configuration = "Release";
    }

    [[nodiscard]] std::vector<ScenarioSpec> scenarios() const override
    {
        return offered;
    }

    bool run(const ScenarioSpec& scenario, const RunOptions& options, ResultRecord& out,
             std::string& skipReason) override
    {
        (void)scenario;
        (void)options;
        ++runCalls;
        if (!runSucceeds)
        {
            skipReason = "the fake engine declines";
            return false;
        }
        out.threadCount = 1;
        out.actualBackend = BackendId::Scalar;
        out.timings.wallMs.assign(options.repetitions, 1.0);
        out.timings.cpuMs.assign(options.repetitions, 1.0);
        out.outputDigest = "digest";
        out.fixtureDigest = "fixture";
        return true;
    }
};

/// argv in the shape parseRunOptions wants: a mutable pointer array into strings this object owns.
class Arguments
{
public:
    explicit Arguments(std::initializer_list<const char*> words)
    {
        for (const char* word : words)
        {
            m_storage.emplace_back(word);
        }
        for (std::string& word : m_storage)
        {
            m_pointers.push_back(word.data());
        }
    }

    [[nodiscard]] int count() const
    {
        return static_cast<int>(m_pointers.size());
    }

    [[nodiscard]] char** values()
    {
        return m_pointers.data();
    }

private:
    std::vector<std::string> m_storage;
    std::vector<char*> m_pointers;
};

/// Options with every identifying field already supplied, so no test reaches the detection path.
RunOptions resolvedOptions()
{
    RunOptions options;
    options.outputPath = "run.json";
    options.hostId = "bench-win";
    options.commit = "abc123";
    options.engineCommit = "engine-abc123";
    options.dependencyLockDigest = std::string(64, 'b');
    options.timestamp = "2026-08-22T00:00:00Z";
    return options;
}

} // namespace

TEST(RunnerHarnessTest, RequiresAnOutputPath)
{
    Arguments arguments{"runner", "--dry-run"};
    RunOptions options;
    std::string error;
    EXPECT_FALSE(parseRunOptions(arguments.count(), arguments.values(), options, error));
    EXPECT_NE(error.find("--out"), std::string::npos);
}

TEST(RunnerHarnessTest, ParsesEveryOption)
{
    Arguments arguments{"runner",           "--out",
                        "run.json",         "--scenarios",
                        "s.json",           "--corpus-root",
                        "corpus",           "--host-id",
                        "bench-win",        "--commit",
                        "abc123",           "--dirty",
                        "--timestamp",      "2026-08-22T00:00:00Z",
                        "--repetitions",    "9",
                        "--warmup",         "2",
                        "--threads",        "7",
                        "--power-profile",  "sustained",
                        "--otherwise-idle", "--notes",
                        "first run",        "--keep-pipeline-scratch",
                        "kept scratch",     "--dry-run"};
    RunOptions options;
    std::string error;
    ASSERT_TRUE(parseRunOptions(arguments.count(), arguments.values(), options, error)) << error;

    EXPECT_EQ(options.outputPath, "run.json");
    EXPECT_EQ(options.scenarioPath, "s.json");
    EXPECT_EQ(options.corpusRoot, "corpus");
    EXPECT_EQ(options.hostId, "bench-win");
    EXPECT_EQ(options.commit, "abc123");
    EXPECT_TRUE(options.dirty);
    EXPECT_TRUE(options.dirtyProvided);
    EXPECT_EQ(options.timestamp, "2026-08-22T00:00:00Z");
    EXPECT_EQ(options.repetitions, 9u);
    EXPECT_EQ(options.warmupIterations, 2u);
    EXPECT_EQ(options.maxThreads, 7u);
    EXPECT_FALSE(options.randomInterleaving);
    EXPECT_EQ(options.powerProfile, "sustained");
    EXPECT_TRUE(options.otherwiseIdle);
    EXPECT_EQ(options.notes, "first run");
    EXPECT_EQ(options.keptPipelineScratch, "kept scratch");
    EXPECT_TRUE(options.dryRun);
}

TEST(RunnerHarnessTest, ParsesAForcedBackend)
{
    for (const BackendId backend : {BackendId::Scalar, BackendId::Sse2, BackendId::Sse42, BackendId::Avx,
                                    BackendId::Avx2, BackendId::Avx512, BackendId::Neon, BackendId::Gpu})
    {
        const std::string name{toString(backend)};
        Arguments arguments{"runner", "--out", "run.json", "--backend", name.c_str()};
        RunOptions options;
        std::string error;
        ASSERT_TRUE(parseRunOptions(arguments.count(), arguments.values(), options, error)) << name << ": " << error;

        ASSERT_TRUE(options.forcedBackend.has_value()) << name;
        EXPECT_EQ(*options.forcedBackend, backend) << name;
    }
    EXPECT_NE(runnerUsage("runner").find("--backend"), std::string::npos);
}

TEST(RunnerHarnessTest, RejectsABackendThatCannotBeForced)
{
    for (const char* value : {"Unspecified", "CompilerVectorized", "PortableSimd", "not-a-backend"})
    {
        Arguments arguments{"runner", "--out", "run.json", "--backend", value};
        RunOptions options;
        std::string error;
        EXPECT_FALSE(parseRunOptions(arguments.count(), arguments.values(), options, error)) << value;
        EXPECT_NE(error.find("--backend"), std::string::npos) << value;
    }
}

TEST(RunnerHarnessTest, ExplicitEngineLtoReachesTheManifestWithoutChangingRunnerProvenance)
{
    FakeAdapter adapter;
    const auto runnerLto = buildRun(resolvedOptions(), {}, adapter).runnerBuild.lto;
    for (const bool enabled : {false, true})
    {
        Arguments arguments{"runner", "--out", "run.json", enabled ? "--engine-lto" : "--engine-no-lto"};
        RunOptions options = resolvedOptions();
        options.dryRun = true;
        std::string error;
        ASSERT_TRUE(parseRunOptions(arguments.count(), arguments.values(), options, error)) << error;
        const auto run = buildRun(options, {}, adapter);
        EXPECT_EQ(run.build.lto, enabled);
        EXPECT_EQ(run.runnerBuild.lto, runnerLto);
    }
}

TEST(RunnerHarnessTest, StandaloneRunnerNeverClaimsControllerInterleaving)
{
    Arguments arguments{"runner", "--out", "run.json"};
    RunOptions options;
    std::string error;
    ASSERT_TRUE(parseRunOptions(arguments.count(), arguments.values(), options, error)) << error;
    EXPECT_FALSE(options.randomInterleaving);
    EXPECT_EQ(runnerUsage("runner").find("--no-interleave"), std::string::npos);
}

TEST(RunnerHarnessTest, RejectsAnUnknownOption)
{
    Arguments arguments{"runner", "--out", "run.json", "--fast-math"};
    RunOptions options;
    std::string error;
    EXPECT_FALSE(parseRunOptions(arguments.count(), arguments.values(), options, error));
    EXPECT_NE(error.find("--fast-math"), std::string::npos);
}

TEST(RunnerHarnessTest, RejectsACountThatIsNotANumber)
{
    Arguments arguments{"runner", "--out", "run.json", "--repetitions", "many"};
    RunOptions options;
    std::string error;
    EXPECT_FALSE(parseRunOptions(arguments.count(), arguments.values(), options, error));
    EXPECT_NE(error.find("many"), std::string::npos);
}

TEST(RunnerHarnessTest, RejectsAnOptionMissingItsValue)
{
    Arguments arguments{"runner", "--out"};
    RunOptions options;
    std::string error;
    EXPECT_FALSE(parseRunOptions(arguments.count(), arguments.values(), options, error));
    EXPECT_FALSE(error.empty());
}

TEST(RunnerHarnessTest, RejectsZeroRepetitionsOutsideADryRun)
{
    // Zero repetitions would produce a result file with empty timings that still looks like a
    // measurement. Saying so is better than publishing it.
    Arguments arguments{"runner", "--out", "run.json", "--repetitions", "0"};
    RunOptions options;
    std::string error;
    EXPECT_FALSE(parseRunOptions(arguments.count(), arguments.values(), options, error));
}

TEST(RunnerHarnessTest, RejectsAZeroThreadOverride)
{
    Arguments arguments{"runner", "--out", "run.json", "--threads", "0"};
    RunOptions options;
    std::string error;
    EXPECT_FALSE(parseRunOptions(arguments.count(), arguments.values(), options, error));
    EXPECT_NE(error.find("--threads"), std::string::npos);
}

TEST(RunnerHarnessTest, RejectsAThreadOverrideOutsideUint32)
{
    Arguments arguments{"runner", "--out", "run.json", "--threads", "4294967297"};
    RunOptions options;
    std::string error;
    EXPECT_FALSE(parseRunOptions(arguments.count(), arguments.values(), options, error));
    EXPECT_NE(error.find("--threads"), std::string::npos);
}

TEST(RunnerHarnessTest, ParsesTheFractionOfAnOperationACancellationProbeStopsAt)
{
    Arguments arguments{"runner", "--out", "run.json", "--cancel-at", "0.25"};
    RunOptions options;
    std::string error;
    ASSERT_TRUE(parseRunOptions(arguments.count(), arguments.values(), options, error)) << error;
    ASSERT_TRUE(options.cancelAtFraction.has_value());
    EXPECT_EQ(*options.cancelAtFraction, 0.25);

    Arguments plain{"runner", "--out", "run.json"};
    RunOptions unprobed;
    ASSERT_TRUE(parseRunOptions(plain.count(), plain.values(), unprobed, error)) << error;
    EXPECT_FALSE(unprobed.cancelAtFraction.has_value());
    EXPECT_NE(runnerUsage("runner").find("--cancel-at"), std::string::npos);
}

TEST(RunnerHarnessTest, RejectsACancellationPointOutsideTheOperation)
{
    // A stop at the start or past the end of an operation measures nothing about its checkpoints.
    for (const char* value : {"0", "1", "1.5", "-0.25", "half", "0.5x", ""})
    {
        Arguments arguments{"runner", "--out", "run.json", "--cancel-at", value};
        RunOptions options;
        std::string error;
        EXPECT_FALSE(parseRunOptions(arguments.count(), arguments.values(), options, error)) << value;
        EXPECT_NE(error.find("--cancel-at"), std::string::npos) << value;
    }
}

TEST(RunnerHarnessTest, AcceptsZeroRepetitionsInADryRun)
{
    Arguments arguments{"runner", "--out", "run.json", "--repetitions", "0", "--dry-run"};
    RunOptions options;
    std::string error;
    EXPECT_TRUE(parseRunOptions(arguments.count(), arguments.values(), options, error)) << error;
}

TEST(RunnerHarnessTest, DryRunSkipsEveryScenarioWithoutCallingTheEngine)
{
    FakeAdapter adapter;
    adapter.runSucceeds = true;
    RunOptions options = resolvedOptions();
    options.dryRun = true;

    const RunManifest run = buildRun(options, {makeScenario("a"), makeScenario("b")}, adapter);

    EXPECT_EQ(adapter.runCalls, 0);
    ASSERT_EQ(run.results.size(), 2u);
    for (const ResultRecord& record : run.results)
    {
        EXPECT_TRUE(record.skipped);
        EXPECT_FALSE(record.skipReason.empty());
    }
    // A dry run must not claim a repetition count it did not perform.
    EXPECT_EQ(run.repetitions, 0u);
    EXPECT_EQ(run.warmupIterations, 0u);
}

TEST(RunnerHarnessTest, MeasuredRunCallsTheEngineAndKeepsItsResult)
{
    FakeAdapter adapter;
    adapter.runSucceeds = true;
    const RunManifest run = buildRun(resolvedOptions(), {makeScenario("a")}, adapter);

    EXPECT_EQ(adapter.runCalls, 1);
    ASSERT_EQ(run.results.size(), 1u);
    EXPECT_FALSE(run.results[0].skipped);
    EXPECT_EQ(run.results[0].outputDigest, "digest");
    EXPECT_EQ(run.results[0].scenarioId, "a");
}

TEST(RunnerHarnessTest, AForcedBackendIsRecordedOnlyWhenItActuallyRan)
{
    FakeAdapter adapter;
    adapter.runSucceeds = true;
    RunOptions options = resolvedOptions();
    options.forcedBackend = BackendId::Scalar;

    const RunManifest run = buildRun(options, {makeScenario("a")}, adapter);

    ASSERT_EQ(run.results.size(), 1u);
    EXPECT_FALSE(run.results[0].skipped);
    EXPECT_EQ(run.results[0].requestedBackend, BackendId::Scalar);
    EXPECT_EQ(run.results[0].actualBackend, BackendId::Scalar);
    EXPECT_TRUE(run.validate().empty());
}

TEST(RunnerHarnessTest, AnUnavailableForcedBackendRetainsItsRequestedLabel)
{
    FakeAdapter adapter;
    adapter.runSucceeds = false;
    RunOptions options = resolvedOptions();
    options.forcedBackend = BackendId::Sse2;

    const RunManifest run = buildRun(options, {makeScenario("a")}, adapter);

    ASSERT_EQ(run.results.size(), 1u);
    EXPECT_TRUE(run.results[0].skipped);
    EXPECT_EQ(run.results[0].requestedBackend, BackendId::Sse2);
    EXPECT_EQ(run.results[0].actualBackend, BackendId::Unspecified);
    EXPECT_TRUE(run.validate().empty());
}

TEST(RunnerHarnessTest, AScenarioThatSubstitutesForAForcedBackendIsSkipped)
{
    FakeAdapter adapter;
    adapter.runSucceeds = true;
    RunOptions options = resolvedOptions();
    options.forcedBackend = BackendId::Avx2;

    const RunManifest run = buildRun(options, {makeScenario("a")}, adapter);

    ASSERT_EQ(run.results.size(), 1u);
    EXPECT_TRUE(run.results[0].skipped);
    EXPECT_NE(run.results[0].skipReason.find("Avx2"), std::string::npos);
    EXPECT_TRUE(run.results[0].timings.empty());
    EXPECT_TRUE(run.results[0].outputDigest.empty());
    EXPECT_TRUE(run.validate().empty());
}

TEST(RunnerHarnessTest, EngineDecliningAScenarioBecomesASkipWithItsReason)
{
    FakeAdapter adapter;
    adapter.runSucceeds = false;
    const RunManifest run = buildRun(resolvedOptions(), {makeScenario("a")}, adapter);

    ASSERT_EQ(run.results.size(), 1u);
    EXPECT_TRUE(run.results[0].skipped);
    EXPECT_EQ(run.results[0].skipReason, "the fake engine declines");
    EXPECT_TRUE(run.validate().empty());
}

TEST(RunnerHarnessTest, InvalidScenarioIsSkippedWithoutCallingTheEngine)
{
    FakeAdapter adapter;
    adapter.runSucceeds = true;
    ScenarioSpec broken = makeScenario("broken");
    broken.input.width = 0;

    const RunManifest run = buildRun(resolvedOptions(), {broken}, adapter);

    EXPECT_EQ(adapter.runCalls, 0);
    ASSERT_EQ(run.results.size(), 1u);
    EXPECT_TRUE(run.results[0].skipped);
    EXPECT_NE(run.results[0].skipReason.find("not valid"), std::string::npos);
}

TEST(RunnerHarnessTest, ManifestRecordsWhatIdentifiesTheRun)
{
    FakeAdapter adapter;
    RunOptions options = resolvedOptions();
    options.corpusRoot = "D:/corpus";
    options.powerProfile = "sustained";
    options.otherwiseIdle = true;
    options.dirty = true;

    const RunManifest run = buildRun(options, {}, adapter);

    EXPECT_EQ(run.runnerVersion, "fake 1");
    EXPECT_EQ(run.timestamp, "2026-08-22T00:00:00Z");
    EXPECT_EQ(run.corpusRoot, "D:/corpus");
    EXPECT_EQ(run.runnerBuild.commit, "abc123");
    EXPECT_TRUE(run.runnerBuild.dirty);
    EXPECT_EQ(run.build.commit, "engine-abc123");
    EXPECT_EQ(run.build.compiler, "FakeCC 1.0");
    EXPECT_FALSE(run.build.fastMath);
    EXPECT_EQ(run.environment.hostId, "bench-win");
    EXPECT_EQ(run.environment.powerProfile, "sustained");
    EXPECT_TRUE(run.environment.otherwiseIdle);
    EXPECT_FALSE(run.environment.osName.empty());
}

TEST(RunnerHarnessTest, ProducedManifestPassesItsOwnValidationAndRoundTrips)
{
    FakeAdapter adapter;
    adapter.runSucceeds = true;
    const RunManifest run = buildRun(resolvedOptions(), {makeScenario("a")}, adapter);
    EXPECT_TRUE(run.validate().empty());

    RunManifest parsed;
    const auto result = json::parseRun(json::toJson(run), parsed);
    ASSERT_TRUE(result.ok) << (result.errors.empty() ? std::string() : result.errors.front());
    EXPECT_EQ(parsed.runnerVersion, run.runnerVersion);
    ASSERT_EQ(parsed.results.size(), 1u);
    EXPECT_EQ(parsed.results[0].outputDigest, "digest");
}

TEST(RunnerHarnessTest, TheProcessCpuClockResolutionIsMeasuredAndRecorded)
{
    // The run protocol makes process CPU time a mandatory publishable field, and on Windows the
    // process clock advances a scheduler tick at a time - about 15.6 ms. A scenario shorter than
    // that returns 0 or one whole tick, so two trials of the same work can differ several-fold
    // from quantization alone. That is a property of the clock, not of the engine, and the only
    // way a reader can tell a measured CPU time from a quantized one is if the run states the
    // resolution it was taken at.
    FakeAdapter adapter;
    adapter.runSucceeds = true;
    const RunManifest run = buildRun(resolvedOptions(), {makeScenario("a")}, adapter);
    EXPECT_GT(run.environment.cpuClockResolutionMs, 0.0) << "the process CPU clock resolution was never measured";
}

TEST(JsonTest, TheCpuClockResolutionSurvivesTheRoundTrip)
{
    RunManifest original = makeRun();
    original.environment.cpuClockResolutionMs = 15.625;

    RunManifest parsed;
    ASSERT_TRUE(json::parseRun(json::toJson(original), parsed));
    EXPECT_DOUBLE_EQ(parsed.environment.cpuClockResolutionMs, 15.625);
}

TEST(ResidentMemoryTest, TheProcessReportsANonZeroResidentSize)
{
    EXPECT_GT(currentResidentBytes(), 0u) << "the platform resident-size probe returned nothing";
}

TEST(ResidentMemoryTest, TheSamplerSeesAnAllocationMadeWhileItRuns)
{
    // The peak-memory ceiling is an acceptance target, so the number behind it has to respond to
    // memory actually being held. A sampler that always returns the resident size at one instant
    // would pass every check that does not allocate.
    const std::uint64_t before = currentResidentBytes();

    ResidentMemorySampler sampler(kResidentSampleIntervalMs);
    sampler.start();
    {
        // Touched, not merely reserved: an untouched reservation never becomes resident. The pages are written and
        // read through a volatile pointer, which no compiler may drop: Clang removes a buffer whose contents nothing
        // observes, allocation and all, and the case then passed only when the sampler's own thread grew the process.
        std::vector<unsigned char> held(64u * 1024u * 1024u);
        volatile unsigned char* const pages = held.data();
        for (std::size_t index = 0; index < held.size(); index += 4096)
        {
            pages[index] = static_cast<unsigned char>(index);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(kResidentSampleIntervalMs * 8));
        EXPECT_GE(pages[0] + pages[4096], 0);
    }
    sampler.stop();

    EXPECT_GT(sampler.peakBytes(), before) << "the sampler never saw the 64 MiB that was held";
}

TEST(ResidentMemoryTest, AStoppedSamplerKeepsItsPeakAndStartsCleanOnRestart)
{
    ResidentMemorySampler sampler(kResidentSampleIntervalMs);
    sampler.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(kResidentSampleIntervalMs * 4));
    sampler.stop();
    const std::uint64_t first = sampler.peakBytes();
    EXPECT_GT(first, 0u);

    sampler.start();
    sampler.stop();
    EXPECT_GT(sampler.peakBytes(), 0u) << "a restarted sampler reported nothing";
}

TEST(RunnerHarnessTest, EveryFieldAPublishableRunMustCarryIsFilledIn)
{
    // The run protocol makes these mandatory and says a run missing any of them is not comparable.
    // They were all empty until this was written, so a run that looked complete was not: two
    // binaries built with different standard libraries or different vector width would have
    // compared as though they matched.
    FakeAdapter adapter;
    adapter.runSucceeds = true;
    const RunManifest run = buildRun(resolvedOptions(), {makeScenario("a")}, adapter);

    EXPECT_FALSE(run.runnerBuild.compiler.empty()) << "runner compiler";
    EXPECT_FALSE(run.runnerBuild.standardLibrary.empty()) << "runner standard library";
    EXPECT_FALSE(run.runnerBuild.linker.empty()) << "runner linker";
    EXPECT_FALSE(run.runnerBuild.flags.empty()) << "runner flags";
    EXPECT_FALSE(run.build.compiler.empty()) << "engine compiler";
    EXPECT_FALSE(run.build.standardLibrary.empty()) << "engine standard library";
    EXPECT_FALSE(run.build.linker.empty()) << "engine linker";
    EXPECT_FALSE(run.build.flags.empty()) << "engine flags";
    EXPECT_EQ(run.dependencyLockDigest.size(), 64u) << "dependency lock digest";
    EXPECT_FALSE(run.environment.cpuModel.empty()) << "cpu model";
    EXPECT_GT(run.environment.logicalCores, 0u) << "logical cores";
    EXPECT_GT(run.environment.memoryBytes, 0u) << "memory";
    EXPECT_FALSE(run.environment.microcodeVersion.empty()) << "microcode";
    EXPECT_GT(run.environment.numaNodes, 0u) << "NUMA topology";
    EXPECT_FALSE(run.environment.gpuModel.empty()) << "GPU applicability";
    EXPECT_FALSE(run.environment.driverVersion.empty()) << "GPU driver applicability";
    EXPECT_GT(run.environment.cpuClockResolutionMs, 0.0) << "cpu clock resolution";

    // Measured from the processor, not inferred from the flags the runner was built with: what a
    // kernel may dispatch to is a property of the machine, and a build flag is a different claim.
    ASSERT_FALSE(run.environment.isaCapabilities.empty()) << "no instruction-set capability measured";
    const auto& isa = run.environment.isaCapabilities;
    EXPECT_NE(std::find(isa.begin(), isa.end(), "SSE2"), isa.end())
        << "SSE2 is baseline for every supported host and was not reported";
}

TEST(RunnerHarnessTest, TheFlagSummaryNamesWhatChangesTheNumbers)
{
    // Not the whole command line - the parts that move a measurement. A run compiled with a wider
    // vector width or with fast-math is not comparable with one that was not, and the summary is
    // what lets a reader of two runs see that.
    FakeAdapter adapter;
    const RunManifest run = buildRun(resolvedOptions(), {}, adapter);
    EXPECT_NE(run.build.flags.find("arch="), std::string::npos) << run.build.flags;
    EXPECT_NE(run.build.flags.find("cpp="), std::string::npos) << run.build.flags;
    EXPECT_EQ(run.build.fastMath, run.build.flags.find("fastmath=on") != std::string::npos);
}

// ------------------------------------------------------------------------------------------
// What the machine was permitted and configured to do
// ------------------------------------------------------------------------------------------

TEST(HostPolicyTest, TheAffinityMaskAgreesWithTheCountItSummarizes)
{
    // Two ways of saying the same thing, and a run whose mask and count disagree is describing a
    // machine that does not exist.
    const an::bench::HostPolicy policy = an::bench::readHostPolicy();
    ASSERT_GT(policy.affinityCores, 0u) << "the process is allowed no processor at all";
    ASSERT_FALSE(policy.affinityMask.empty());

    const std::uint64_t mask = std::stoull(policy.affinityMask, nullptr, 16);
    EXPECT_EQ(static_cast<std::uint32_t>(std::popcount(mask)), policy.affinityCores);
}

TEST(HostPolicyTest, TheProcessCannotBeAllowedMoreProcessorsThanTheMachineHas)
{
    const an::bench::HostPolicy policy = an::bench::readHostPolicy();
    EXPECT_LE(policy.affinityCores, std::thread::hardware_concurrency());
}

TEST(BackgroundLoadTest, AnIdleMachineReportsNone)
{
    EXPECT_DOUBLE_EQ(an::bench::backgroundLoadFraction(0.0, 0.0, 1000.0, 8), 0.0);
}

TEST(BackgroundLoadTest, WorkThisProcessDidIsNotBackgroundLoad)
{
    // The field answers "what else was the machine doing", so the benchmark's own work has to come
    // out of it. Left in, every run would report itself as background load and the field would say
    // nothing about the condition it exists to record.
    EXPECT_DOUBLE_EQ(an::bench::backgroundLoadFraction(8000.0, 8000.0, 1000.0, 8), 0.0);
}

TEST(BackgroundLoadTest, WorkSomethingElseDidIsAllOfIt)
{
    EXPECT_DOUBLE_EQ(an::bench::backgroundLoadFraction(8000.0, 0.0, 1000.0, 8), 1.0);
    EXPECT_DOUBLE_EQ(an::bench::backgroundLoadFraction(4000.0, 0.0, 1000.0, 8), 0.5);
}

TEST(BackgroundLoadTest, ItStaysWithinItsOwnUnits)
{
    // The two clocks are read a moment apart and at different resolutions, so their difference can
    // land just outside the range it describes. A fraction of 1.02 would be reported as a fact.
    EXPECT_DOUBLE_EQ(an::bench::backgroundLoadFraction(9000.0, 0.0, 1000.0, 8), 1.0);
    EXPECT_DOUBLE_EQ(an::bench::backgroundLoadFraction(1000.0, 2000.0, 1000.0, 8), 0.0);
}

TEST(BackgroundLoadTest, ARunWithNoDurationReportsNothingRatherThanDividingByZero)
{
    EXPECT_DOUBLE_EQ(an::bench::backgroundLoadFraction(500.0, 0.0, 0.0, 8), 0.0);
    EXPECT_DOUBLE_EQ(an::bench::backgroundLoadFraction(500.0, 0.0, 1000.0, 0), 0.0);
}

TEST(HostActivitySamplerTest, ASamplerThatNeverRanReportsNoSamplesRatherThanAZeroClock)
{
    // A run too short to sample and a machine whose clock cannot be read are different facts, and a
    // frequency of 0 MHz reported as measured would be neither.
    an::bench::HostActivitySampler sampler;
    const an::bench::SampledRange range = sampler.frequencyMhz();
    EXPECT_EQ(range.samples, 0u);
    EXPECT_DOUBLE_EQ(range.mean, 0.0);
}

TEST(HostActivitySamplerTest, ItRecordsTheShapeOfTheClockAcrossTheRun)
{
    if (an::bench::currentCpuMegahertz() <= 0.0)
    {
        GTEST_SKIP() << "this platform does not report a processor clock";
    }

    an::bench::HostActivitySampler sampler(std::chrono::milliseconds(20));
    sampler.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(120));
    sampler.stop();

    const an::bench::SampledRange range = sampler.frequencyMhz();
    ASSERT_GE(range.samples, 2u) << "the endpoints alone should have been sampled";
    EXPECT_GT(range.minimum, 0.0);
    EXPECT_LE(range.minimum, range.mean);
    EXPECT_LE(range.mean, range.maximum);
    EXPECT_GE(sampler.backgroundLoad(), 0.0);
    EXPECT_LE(sampler.backgroundLoad(), 1.0);
}

TEST(HostActivitySamplerTest, StoppingDoesNotWaitOutTheSamplingInterval)
{
    // Every runner process stops its sampler on the way out; a stop that sleeps out the interval
    // adds up to a whole interval to every process of a paired campaign.
    an::bench::HostActivitySampler sampler(std::chrono::seconds(5));
    sampler.start();
    // Let the worker reach its wait, as it has in any real run.
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    const auto stopping = std::chrono::steady_clock::now();
    sampler.stop();
    EXPECT_LT(std::chrono::steady_clock::now() - stopping, std::chrono::milliseconds(1000));
}

TEST(ProcessClockTest, ItAdvancesWithTheWorkTheProcessDoes)
{
    const double before = an::bench::processCpuMilliseconds();
    ASSERT_FALSE(std::isnan(before)) << "the platform refused to report process CPU time";

    volatile double sink = 0.0;
    const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(80);
    while (std::chrono::steady_clock::now() < until)
    {
        for (int index = 0; index < 4096; ++index)
        {
            sink = sink + static_cast<double>(index) * 1.000001;
        }
    }

    const double after = an::bench::processCpuMilliseconds();
    EXPECT_GT(after - before, 20.0) << "80 ms of busy work advanced the CPU clock by " << (after - before) << " ms";
}

TEST(ProcessClockTest, ItNeverGoesBackwards)
{
    double previous = an::bench::processCpuMilliseconds();
    for (int index = 0; index < 200; ++index)
    {
        const double current = an::bench::processCpuMilliseconds();
        ASSERT_GE(current, previous) << "sample " << index;
        previous = current;
    }
}

TEST(ProcessClockTest, ItCountsWorkerThreadsRatherThanOnlyTheCallingOne)
{
    // The property every measurement in this system rests on. A thread clock would report roughly
    // the wall time here; a process clock reports the sum across the workers, which is what makes
    // the ratio of CPU to wall a statement about parallelism instead of a tautology.
    if (std::thread::hardware_concurrency() < 4)
    {
        GTEST_SKIP() << "needs at least four hardware threads to distinguish the two clocks";
    }

    const double before = an::bench::processCpuMilliseconds();
    const auto started = std::chrono::steady_clock::now();

    std::vector<std::thread> workers;
    for (int index = 0; index < 4; ++index)
    {
        workers.emplace_back([] {
            volatile double sink = 0.0;
            const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(100);
            while (std::chrono::steady_clock::now() < until)
            {
                for (int inner = 0; inner < 4096; ++inner)
                {
                    sink = sink + static_cast<double>(inner) * 1.000001;
                }
            }
        });
    }
    for (std::thread& worker : workers)
    {
        worker.join();
    }

    const double cpu = an::bench::processCpuMilliseconds() - before;
    const double wall = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
    EXPECT_GT(cpu, wall * 1.5) << "four busy threads spent " << cpu << " ms of CPU across " << wall << " ms of wall";
}

TEST(RunnerHarnessTest, TheRunRecordsWhatTheMachineWasPermittedAndConfiguredToDo)
{
    // The run protocol lists affinity, power plan, frequency policy and background load among the
    // fields a publishable run must carry. Two runs on one machine under different power plans, or
    // with the process confined to half the cores, are not comparable and nothing else would say so.
    FakeAdapter adapter;
    adapter.runSucceeds = true;
    const RunManifest run = buildRun(resolvedOptions(), {makeScenario("a")}, adapter);

    EXPECT_GT(run.environment.affinityCores, 0u) << "affinity";
    EXPECT_FALSE(run.environment.affinityMask.empty()) << "affinity mask";
    EXPECT_GE(run.environment.backgroundLoad, 0.0);
    EXPECT_LE(run.environment.backgroundLoad, 1.0);
}

TEST(RunnerHarnessTest, TheMeasuredPowerPlanIsSeparateFromTheOperatorsLabelForIt)
{
    // `powerProfile` is what the operator says they configured; `powerPlan` is what the operating
    // system reports. Folding them into one field would lose the case worth catching: a run whose
    // label and machine disagree.
    RunOptions options = resolvedOptions();
    options.powerProfile = "ac-high-performance";
    FakeAdapter adapter;
    const RunManifest run = buildRun(options, {}, adapter);

    EXPECT_EQ(run.environment.powerProfile, "ac-high-performance");
    EXPECT_NE(&run.environment.powerPlan, &run.environment.powerProfile);
}

TEST(RunnerHarnessTest, TheResidentSampleIntervalIsRecorded)
{
    // A sampled peak can miss a spike between samples, so the interval it was taken at is part of
    // what the number means rather than an implementation detail.
    FakeAdapter adapter;
    adapter.runSucceeds = true;
    const RunManifest run = buildRun(resolvedOptions(), {makeScenario("a")}, adapter);
    EXPECT_EQ(run.residentSampleIntervalMs, kResidentSampleIntervalMs);
}

TEST(JsonTest, CountersNoEngineReportsStayNullRatherThanBecomingZero)
{
    // A zero that means "not measured" is indistinguishable from a zero that means "measured
    // zero". An engine that reports none of the four allocation counters leaves them absent, so they
    // must round-trip as absent.
    RunManifest original = makeRun();
    ResultRecord record = makeResult("a", 5.0, "digest-a");
    record.memory.peakResidentBytes = 1234u;
    record.memory.allocationCount.reset();
    record.memory.allocatedBytes.reset();
    original.results.push_back(record);

    RunManifest parsed;
    ASSERT_TRUE(json::parseRun(json::toJson(original), parsed));
    ASSERT_EQ(parsed.results.size(), 1u);
    EXPECT_EQ(parsed.results[0].memory.peakResidentBytes, 1234u);
    EXPECT_FALSE(parsed.results[0].memory.allocationCount.has_value());
    EXPECT_FALSE(parsed.results[0].memory.allocatedBytes.has_value());

    parsed.results[0].memory.allocationCount = 7u;
    RunManifest again;
    ASSERT_TRUE(json::parseRun(json::toJson(parsed), again));
    ASSERT_TRUE(again.results[0].memory.allocationCount.has_value());
    EXPECT_EQ(*again.results[0].memory.allocationCount, 7u);
}

TEST(JsonTest, AFrameSetInputSourceRoundTripsAndIsNotConfusedWithASingleFile)
{
    // A pipeline scenario is fed a set of frames, a kernel scenario a single buffer. The runner has
    // to know which before it asks for anything, so the distinction is in the scenario rather than
    // inferred from whichever kind of manifest entry happens to carry the id.
    ScenarioSpec original;
    original.id = "pipelines.integration.reference";
    original.group = "pipelines";
    original.inputSource = InputSource::CorpusFrameSet;
    original.corpusId = "carina-asi294mm-red";
    original.input = corpusDescriptor();
    original.correctnessClass = CorrectnessClass::EstimatorTolerance;
    original.tolerance = 1e-6;

    ScenarioSpec parsed;
    ASSERT_TRUE(json::parseScenario(json::toJson(original), parsed));
    EXPECT_EQ(parsed.inputSource, InputSource::CorpusFrameSet);
    EXPECT_EQ(parsed.corpusId, "carina-asi294mm-red");

    EXPECT_NE(json::toJson(original).find("CorpusFrameSet"), std::string::npos)
        << "the frame-set input source is not written by name";
}

TEST(CorpusFrameSetTest, VerificationReportsEveryDatasetAndSetInTheManifest)
{
    // Before a measurement that reads tens of gigabytes, the operator wants to know the corpus is
    // intact - and after building a manifest, that what was written resolves. Both need a mode
    // that walks the manifest without running a scenario.
    const auto root = scratchDir("corpus-verify");
    writeDumpFixture(root, "single", corpusDescriptor());

    CorpusManifest manifest;
    manifest.add(entryFor("single.frame", root, "single.json"));
    manifest.addSet(frameSetFor("two.role.set", root, {{"lights", 2}, {"darks", 1}}));

    CompositeFixtureSource source;
    source.useCorpus(manifest, root.string());

    CorpusVerification report = verifyCorpus(manifest, source);
    EXPECT_TRUE(report.ok);
    EXPECT_EQ(report.datasetsPresent, 1u);
    EXPECT_EQ(report.datasetsAbsent, 0u);
    EXPECT_EQ(report.setsPresent, 1u);
    EXPECT_EQ(report.setsAbsent, 0u);
    EXPECT_EQ(report.framesResolved, 3u);
    EXPECT_TRUE(report.problems.empty());
}

TEST(CorpusFrameSetTest, VerificationNamesWhatIsMissingRatherThanFailingWholesale)
{
    const auto root = scratchDir("corpus-verify-gap");
    CorpusFrameSet broken = frameSetFor("gap.set", root, {{"lights", 2}});
    broken.roles[0].frames[1].relativePath = "never-exported.fits";

    CorpusManifest manifest;
    manifest.addSet(broken);

    CompositeFixtureSource source;
    source.useCorpus(manifest, root.string());

    CorpusVerification report = verifyCorpus(manifest, source);
    EXPECT_FALSE(report.ok);
    EXPECT_EQ(report.setsPresent, 0u);
    EXPECT_EQ(report.setsAbsent, 1u);
    ASSERT_EQ(report.problems.size(), 1u);
    EXPECT_NE(report.problems[0].find("never-exported.fits"), std::string::npos) << report.problems[0];
}

TEST(RunnerHarnessTest, RunWithNoScenariosIsStillValid)
{
    // A run of no scenarios still produces a schema-valid protocol manifest; it measured nothing,
    // and its empty result list says so.
    FakeAdapter adapter;
    const RunManifest run = buildRun(resolvedOptions(), {}, adapter);
    EXPECT_TRUE(run.validate().empty());
    EXPECT_TRUE(run.results.empty());
}

TEST(RunnerHarnessTest, TheScenarioCatalogueCanBeHandedFromOneRunToTheNext)
{
    // The mechanism that lets later runs execute one contract: the run that owns the catalogue
    // writes it, and a later run is given the same file rather than describing its own.
    const std::filesystem::path directory = scratchDir("catalogue");
    std::filesystem::create_directories(directory);
    const std::string path = (directory / "scenarios.json").string();

    std::vector<ScenarioSpec> written = {makeScenario("a"), makeScenario("b")};
    written[1].parameters[kTimedBatchParameter] = "7";

    std::string error;
    ASSERT_TRUE(json::writeScenariosFile(path, written, error)) << error;

    std::vector<ScenarioSpec> read;
    const auto parsed = json::readScenariosFile(path, read);
    ASSERT_TRUE(parsed.ok) << (parsed.errors.empty() ? std::string() : parsed.errors.front());
    ASSERT_EQ(read.size(), 2u);
    EXPECT_EQ(read[0].id, "a");
    EXPECT_EQ(read[1].parameters, written[1].parameters);

    std::error_code code;
    std::filesystem::remove_all(directory, code);
}

TEST(RunnerHarnessTest, DumpScenariosNamesTheFileToWriteTheCatalogueTo)
{
    const char* argv[] = {"runner", "--out", "run.json", "--dump-scenarios", "catalogue.json"};
    RunOptions options;
    std::string error;
    ASSERT_TRUE(parseRunOptions(5, const_cast<char**>(argv), options, error)) << error;
    EXPECT_EQ(options.scenarioDumpPath, "catalogue.json");
}

TEST(RunnerHarnessTest, ARunWithoutACommitIsRejectedRatherThanPublished)
{
    FakeAdapter adapter;
    RunOptions options = resolvedOptions();
    options.commit.clear();

    const RunManifest run = buildRun(options, {}, adapter);
    EXPECT_FALSE(run.validate().empty());
}

TEST(RunnerHarnessTest, DetectedHostIdIsNeverEmpty)
{
    EXPECT_FALSE(detectHostId().empty());
}

TEST(RunnerHarnessTest, UsageNamesTheExecutable)
{
    EXPECT_NE(runnerUsage("AfterNightEngineBench").find("AfterNightEngineBench"), std::string::npos);
}

// --- synthetic fixtures -----------------------------------------------------------------------

namespace
{

/// A scratch directory that removes itself, so a failing assertion cannot leave one behind.
class ScratchDirectory
{
public:
    explicit ScratchDirectory(const char* name) : m_path(scratchDir(name))
    {
        std::filesystem::remove_all(m_path);
        std::filesystem::create_directories(m_path);
    }

    ~ScratchDirectory()
    {
        std::error_code ignored;
        std::filesystem::remove_all(m_path, ignored);
    }

    ScratchDirectory(const ScratchDirectory&) = delete;
    ScratchDirectory& operator=(const ScratchDirectory&) = delete;

    [[nodiscard]] const std::filesystem::path& path() const
    {
        return m_path;
    }

private:
    std::filesystem::path m_path;
};

/// Set an environment variable for this process only.
void setEnvironmentValue(const char* name, const char* value)
{
#if defined(_WIN32)
    _putenv_s(name, value == nullptr ? "" : value);
#else
    if (value == nullptr)
    {
        unsetenv(name);
    }
    else
    {
        setenv(name, value, 1);
    }
#endif
}

SampleDescriptor floatDescriptor(std::uint32_t width, std::uint32_t height, std::uint32_t channels)
{
    SampleDescriptor d;
    d.width = width;
    d.height = height;
    d.channels = channels;
    d.sampleType = SampleType::Float32;
    d.layout = Layout::Planar;
    d.byteOrder = ByteOrder::Little;
    return d;
}

Fixture acquireOrFail(FixtureSource& source, const std::string& id, std::uint64_t seed,
                      const SampleDescriptor& descriptor)
{
    Fixture fixture;
    std::string error;
    EXPECT_TRUE(source.acquire(id, seed, descriptor, fixture, error)) << id << ": " << error;
    return fixture;
}

float readFloat(const Fixture& fixture, std::size_t index)
{
    float value = 0.0f;
    std::memcpy(&value, fixture.bytes.data() + index * sizeof(float), sizeof value);
    return value;
}

} // namespace

TEST(CosmeticCorrectionOracleTest, PlantedDefectsKeepTheirMarginsAndTheirDeterminism)
{
    constexpr std::uint32_t kWidth = 256u;
    constexpr std::uint32_t kHeight = 200u;
    std::vector<float> first(static_cast<std::size_t>(kWidth) * kHeight, 0.5f);
    std::vector<float> second = first;
    const std::vector<CosmeticImplant> implantsFirst = plantCosmeticDefects(first, kWidth, kHeight);
    const std::vector<CosmeticImplant> implantsSecond = plantCosmeticDefects(second, kWidth, kHeight);
    ASSERT_FALSE(implantsFirst.empty());
    ASSERT_EQ(implantsFirst.size(), implantsSecond.size());
    EXPECT_EQ(first, second) << "the same extent plants the same defects";

    for (std::size_t index = 0u; index < implantsFirst.size(); ++index)
    {
        const CosmeticImplant& implant = implantsFirst[index];
        EXPECT_EQ(implant.x, implantsSecond[index].x);
        EXPECT_GE(implant.x, 8u);
        EXPECT_GE(implant.y, 8u);
        EXPECT_LT(implant.x, kWidth - 8u);
        EXPECT_LT(implant.y, kHeight - 8u);
        EXPECT_EQ(first[static_cast<std::size_t>(implant.y) * kWidth + implant.x], implant.value);
        EXPECT_EQ(implant.value, implant.hot ? 1.0f : 0.0f);
        for (std::size_t other = index + 1u; other < implantsFirst.size(); ++other)
        {
            const std::int64_t dx = static_cast<std::int64_t>(implant.x) - implantsFirst[other].x;
            const std::int64_t dy = static_cast<std::int64_t>(implant.y) - implantsFirst[other].y;
            EXPECT_GE((std::max)(std::abs(dx), std::abs(dy)), 8) << "two defects share a replacement window";
        }
    }

    std::vector<float> tiny(64u * 64u, 0.5f);
    EXPECT_TRUE(plantCosmeticDefects(tiny, 64u, 64u).empty()) << "a frame too small for the lattice plants nothing";
}

TEST(CosmeticCorrectionOracleTest, TheOracleStatesExactStatisticsAndSharedMedianRepairs)
{
    // A five-by-five frame around one hot implant, small enough to check every number by hand.
    constexpr std::uint32_t kSide = 5u;
    std::vector<float> samples{0.1f,  0.2f,  0.3f,  0.4f,  0.5f,  0.6f,  0.7f,  0.8f,  0.9f,  1.0f,  0.15f, 0.25f, 1.0f,
                               0.45f, 0.55f, 0.65f, 0.75f, 0.85f, 0.95f, 0.05f, 0.35f, 0.12f, 0.22f, 0.32f, 0.42f};
    const std::vector<CosmeticImplant> implants{{.x = 2u, .y = 2u, .value = 1.0f, .hot = true}};
    const CosmeticOracle oracle = computeCosmeticOracle(samples, kSide, kSide, implants, 2.0, 2.0, 1u, 2u);

    std::vector<double> sorted{samples.begin(), samples.end()};
    std::sort(sorted.begin(), sorted.end());
    EXPECT_DOUBLE_EQ(oracle.median, sorted[12]);
    EXPECT_DOUBLE_EQ(oracle.coldThreshold, oracle.median - 2.0 * oracle.deviation);
    EXPECT_DOUBLE_EQ(oracle.hotThreshold, oracle.median + 2.0 * oracle.deviation);

    // The hot window: the eight neighbours of the centre, whose even count averages its middles in
    // Float64 and narrows once - the definition the engine shares.
    std::vector<float> window{0.7f, 0.8f, 0.9f, 0.25f, 0.45f, 0.75f, 0.85f, 0.95f};
    std::sort(window.begin(), window.end());
    const double expected = static_cast<double>(
        static_cast<float>((static_cast<double>(window[3]) + static_cast<double>(window[4])) / 2.0));
    ASSERT_EQ(oracle.replacements.size(), 1u);
    EXPECT_DOUBLE_EQ(oracle.replacements[0], expected);

    // A produced frame equal to the source everywhere and the expected repair at the implant is a
    // zero-error answer; moving the repair or an untouched sample is not.
    std::vector<float> produced = samples;
    produced[12] = static_cast<float>(expected);
    EXPECT_DOUBLE_EQ(cosmeticCorrectionError(produced, samples, kSide, oracle), 0.0);
    produced[12] += 0.25f;
    EXPECT_GT(cosmeticCorrectionError(produced, samples, kSide, oracle), 0.2);
    produced[12] = static_cast<float>(expected);
    if (!oracle.untouched.empty())
    {
        produced[oracle.untouched.front()] += 1.0f;
        EXPECT_GT(cosmeticCorrectionError(produced, samples, kSide, oracle), 0.5);
    }

    const std::string text = cosmeticCorrectionText(produced, kSide, oracle);
    EXPECT_NE(text.find("h2,2="), std::string::npos);
}

TEST(CosmeticCorrectionOracleTest, DetectionErrorsCompareThresholdsAndMissedFractions)
{
    std::vector<float> samples(48u * 48u, 0.5f);
    CosmeticOracle oracle{};
    oracle.coldThreshold = 0.1;
    oracle.hotThreshold = 0.9;
    oracle.coldCount = 100u;
    oracle.hotCount = 200u;
    EXPECT_DOUBLE_EQ(cosmeticDetectionError(0.1, 0.9, 100u, 200u, oracle), 0.0);
    EXPECT_NEAR(cosmeticDetectionError(0.1, 0.9, 99u, 200u, oracle), 0.01, 1e-12)
        << "one missed sample of a hundred is the stated fraction";
    EXPECT_NEAR(cosmeticDetectionError(0.1, 0.905, 100u, 200u, oracle), 0.005, 1e-12);
    EXPECT_EQ(cosmeticDetectionError(std::numeric_limits<double>::quiet_NaN(), 0.9, 100u, 200u, oracle),
              (std::numeric_limits<double>::max)());
    const std::string text = cosmeticDetectionText(0.1, 0.9, 100u, 200u);
    EXPECT_NE(text.find("coldCount=100"), std::string::npos);
    EXPECT_NE(text.find("hotCount=200"), std::string::npos);
}

TEST(PhaseCorrelationOracleTest, ACyclicShiftMovesContentAndLosesNothing)
{
    constexpr std::uint32_t kWidth = 7u;
    constexpr std::uint32_t kHeight = 5u;
    std::vector<float> frame(static_cast<std::size_t>(kWidth) * kHeight, 0.0f);
    for (std::size_t index = 0u; index < frame.size(); ++index)
    {
        frame[index] = static_cast<float>(index);
    }
    const std::vector<float> shifted = cyclicallyShiftedFrame(frame, kWidth, kHeight, 2, -1);
    ASSERT_EQ(shifted.size(), frame.size());
    // The sample at (0, 0) moved to (2, 4): two right, one up wrapping to the bottom row.
    EXPECT_EQ(shifted[static_cast<std::size_t>(4u) * kWidth + 2u], frame[0]);
    // What leaves the right edge re-enters at the left.
    EXPECT_EQ(shifted[0], frame[static_cast<std::size_t>(1u) * kWidth + 5u]);
    std::vector<float> restored = cyclicallyShiftedFrame(shifted, kWidth, kHeight, -2, 1);
    EXPECT_EQ(restored, frame) << "the inverse shift restores the frame exactly";
}

TEST(RestorationOracleTest, TheBlurKernelSumsToOneAndIsSymmetric)
{
    std::uint32_t side = 0u;
    const std::vector<float> kernel = restorationBlurKernel(kRestorationBlurFwhm, side);
    ASSERT_EQ(side % 2u, 1u);
    ASSERT_EQ(kernel.size(), static_cast<std::size_t>(side) * side);
    double total = 0.0;
    for (const float sample : kernel)
    {
        total += static_cast<double>(sample);
    }
    EXPECT_NEAR(total, 1.0, 1.0e-6);
    for (std::uint32_t y = 0u; y < side; ++y)
    {
        for (std::uint32_t x = 0u; x < side; ++x)
        {
            EXPECT_FLOAT_EQ(kernel[static_cast<std::size_t>(y) * side + x],
                            kernel[static_cast<std::size_t>(side - 1u - y) * side + (side - 1u - x)]);
        }
    }
}

TEST(RestorationOracleTest, BlurringAFlatFrameLeavesItFlat)
{
    // A kernel that sums to one and a border rule that reflects: a constant frame has to come back
    // as itself, which is what proves the harness is measuring the deconvolution and not its own
    // border arithmetic.
    constexpr std::uint32_t kWidth = 9u;
    constexpr std::uint32_t kHeight = 7u;
    std::uint32_t side = 0u;
    const std::vector<float> kernel = restorationBlurKernel(2.0, side);
    const std::vector<float> flat(static_cast<std::size_t>(kWidth) * kHeight, 0.25f);
    const std::vector<float> blurred = restorationBlur(flat, kWidth, kHeight, kernel, side);
    for (const float sample : blurred)
    {
        EXPECT_NEAR(sample, 0.25f, 1.0e-6f);
    }
    EXPECT_DOUBLE_EQ(restorationResidual(blurred, flat), restorationResidual(blurred, flat));
    EXPECT_LT(restorationResidual(blurred, flat), 1.0e-6);
}

TEST(RestorationOracleTest, TheResidualRatioIsOneForAnEngineThatReturnsItsInput)
{
    constexpr std::uint32_t kWidth = 16u;
    constexpr std::uint32_t kHeight = 16u;
    std::vector<float> sharp(static_cast<std::size_t>(kWidth) * kHeight, 0.1f);
    sharp[static_cast<std::size_t>(8u) * kWidth + 8u] = 1.0f;
    std::uint32_t side = 0u;
    const std::vector<float> kernel = restorationBlurKernel(2.0, side);
    const std::vector<float> blurred = restorationBlur(sharp, kWidth, kHeight, kernel, side);

    EXPECT_DOUBLE_EQ(restorationResidualRatio(blurred, blurred, sharp), 1.0);
    EXPECT_DOUBLE_EQ(restorationResidualRatio(sharp, blurred, sharp), 0.0);
    std::vector<float> broken = blurred;
    broken[0] = std::numeric_limits<float>::quiet_NaN();
    EXPECT_EQ(restorationResidualRatio(broken, blurred, sharp), (std::numeric_limits<double>::max)());
}

TEST(RestorationOracleTest, TheNoiseLevelIsTheRowDifferenceDeviationOverTheSquareRootOfTwo)
{
    // An odd width leaves an even number of first differences, so the alternating steps cancel and
    // the expected deviation is exactly the step.
    constexpr std::uint32_t kWidth = 65u;
    constexpr std::uint32_t kHeight = 8u;
    constexpr float kStep = 0.25f;
    std::vector<float> frame(static_cast<std::size_t>(kWidth) * kHeight, 0.0f);
    for (std::size_t index = 0u; index < frame.size(); ++index)
    {
        frame[index] = (index % 2u == 0u) ? 0.5f : 0.5f + kStep;
    }
    EXPECT_NEAR(restorationNoiseLevel(frame, kWidth, kHeight), static_cast<double>(kStep) / std::sqrt(2.0), 1.0e-6);

    const std::vector<float> flat(frame.size(), 0.5f);
    EXPECT_DOUBLE_EQ(restorationNoiseRatio(flat, frame, kWidth, kHeight), 0.0);
    EXPECT_DOUBLE_EQ(restorationNoiseRatio(frame, frame, kWidth, kHeight), 1.0);
    EXPECT_EQ(restorationNoiseRatio(frame, flat, kWidth, kHeight), (std::numeric_limits<double>::max)());
    const std::string text = restorationDenoiseText(flat, frame, kWidth, kHeight);
    EXPECT_NE(text.find("ratio="), std::string::npos);
    EXPECT_NE(text.find("level="), std::string::npos);
}

TEST(PhaseCorrelationOracleTest, TheResidualIsTheDistanceFromTheNegatedShift)
{
    EXPECT_DOUBLE_EQ(phaseCorrelationResidual(-7.0, 4.0, 7, -4), 0.0);
    EXPECT_DOUBLE_EQ(phaseCorrelationResidual(-4.0, 0.0, 7, -4), 5.0);
    EXPECT_EQ(phaseCorrelationResidual(std::numeric_limits<double>::quiet_NaN(), 0.0, 7, -4),
              (std::numeric_limits<double>::max)());
    const std::string text = phaseCorrelationText(-7.0, 4.0, 0.5);
    EXPECT_NE(text.find("translationX="), std::string::npos);
    EXPECT_NE(text.find("response="), std::string::npos);
}

TEST(SyntheticFixtureTest, SameSeedAndVersionReproduceTheSameBytes)
{
    SyntheticFixtureSource source;
    const SampleDescriptor descriptor = floatDescriptor(37, 11, 3);

    const Fixture first = acquireOrFail(source, "synthetic.uniform_noise", 7, descriptor);
    const Fixture second = acquireOrFail(source, "synthetic.uniform_noise", 7, descriptor);

    EXPECT_EQ(first.digest, second.digest);
    EXPECT_EQ(first.bytes, second.bytes);
    EXPECT_FALSE(first.digest.empty());
}

TEST(SyntheticFixtureTest, ADifferentSeedChangesARandomGenerator)
{
    SyntheticFixtureSource source;
    const SampleDescriptor descriptor = floatDescriptor(37, 11, 1);

    EXPECT_NE(acquireOrFail(source, "synthetic.uniform_noise", 1, descriptor).digest,
              acquireOrFail(source, "synthetic.uniform_noise", 2, descriptor).digest);
}

TEST(SyntheticFixtureTest, AGeneratorVersionBumpChangesTheBytes)
{
    // The version has to be an input to generation, not a label beside it. If a bump left the
    // bytes alone, an archived digest would silently outlive the generator that produced it.
    SyntheticFixtureSource current(kSyntheticGeneratorVersion);
    SyntheticFixtureSource bumped(kSyntheticGeneratorVersion + 1);
    const SampleDescriptor descriptor = floatDescriptor(16, 16, 1);

    const Fixture before = acquireOrFail(current, "synthetic.uniform_noise", 3, descriptor);
    const Fixture after = acquireOrFail(bumped, "synthetic.uniform_noise", 3, descriptor);

    EXPECT_NE(before.digest, after.digest);
    EXPECT_EQ(before.generatorVersion, kSyntheticGeneratorVersion);
    EXPECT_EQ(after.generatorVersion, kSyntheticGeneratorVersion + 1);
}

TEST(SyntheticFixtureTest, DeterministicGeneratorsDoNotDependOnTheSeed)
{
    SyntheticFixtureSource source;
    const SampleDescriptor descriptor = floatDescriptor(16, 16, 1);

    EXPECT_EQ(acquireOrFail(source, "synthetic.ramp", 1, descriptor).digest,
              acquireOrFail(source, "synthetic.ramp", 999, descriptor).digest);
}

TEST(SyntheticFixtureTest, EveryGeneratorFillsExactlyTheDescribedBuffer)
{
    SyntheticFixtureSource source;
    const SampleDescriptor descriptor = floatDescriptor(9, 5, 3);

    for (const std::string& id : SyntheticFixtureSource::generatorIds())
    {
        const Fixture fixture = acquireOrFail(source, id, 11, descriptor);
        EXPECT_EQ(fixture.bytes.size(), descriptor.totalBytes()) << id;
        EXPECT_EQ(fixture.sourceId, id);
        EXPECT_EQ(fixture.descriptor, descriptor) << id;
        EXPECT_FALSE(fixture.digest.empty()) << id;
    }
}

TEST(SyntheticFixtureTest, PaddingLeftByAWiderStrideIsDefinedAndReproducible)
{
    SyntheticFixtureSource source;
    SampleDescriptor descriptor = floatDescriptor(8, 4, 1);
    descriptor.rowStrideBytes = 64; // 32 bytes of samples, 32 bytes of padding

    const Fixture first = acquireOrFail(source, "synthetic.ramp", 5, descriptor);
    const Fixture second = acquireOrFail(source, "synthetic.ramp", 5, descriptor);

    ASSERT_EQ(first.bytes.size(), 256u);
    EXPECT_EQ(first.digest, second.digest) << "a digest over undefined padding is not reproducible";
    for (std::size_t offset = 32; offset < 64; ++offset)
    {
        EXPECT_EQ(first.bytes[offset], 0u) << "padding byte " << offset;
    }
}

TEST(SyntheticFixtureTest, RampVariesAcrossTheRowSoAStrideErrorIsVisible)
{
    SyntheticFixtureSource source;
    const SampleDescriptor descriptor = floatDescriptor(5, 2, 1);
    const Fixture fixture = acquireOrFail(source, "synthetic.ramp", 0, descriptor);

    EXPECT_FLOAT_EQ(readFloat(fixture, 0), 0.0f);
    EXPECT_FLOAT_EQ(readFloat(fixture, 4), 1.0f);
    EXPECT_FLOAT_EQ(readFloat(fixture, 5), 0.0f) << "the second row restarts the ramp";
}

TEST(SyntheticFixtureTest, ImpulseIsASinglePointPerPlane)
{
    SyntheticFixtureSource source;
    const SampleDescriptor descriptor = floatDescriptor(5, 5, 1);
    const Fixture fixture = acquireOrFail(source, "synthetic.impulse", 0, descriptor);

    int nonZero = 0;
    for (std::size_t index = 0; index < 25; ++index)
    {
        if (readFloat(fixture, index) != 0.0f)
        {
            ++nonZero;
        }
    }
    EXPECT_EQ(nonZero, 1);
    EXPECT_FLOAT_EQ(readFloat(fixture, 2 * 5 + 2), 1.0f);
}

TEST(SyntheticFixtureTest, StarsContainExtendedPointSpreadFunctions)
{
    SyntheticFixtureSource source;
    const SampleDescriptor descriptor = floatDescriptor(64, 64, 1);
    const Fixture fixture = acquireOrFail(source, "synthetic.stars", 17, descriptor);

    bool adjacentBrightSamples = false;
    for (std::size_t y = 1; y + 1 < descriptor.height && !adjacentBrightSamples; ++y)
    {
        for (std::size_t x = 1; x + 1 < descriptor.width; ++x)
        {
            const std::size_t index = y * descriptor.width + x;
            if (readFloat(fixture, index) > 0.1f && readFloat(fixture, index + 1) > 0.1f)
            {
                adjacentBrightSamples = true;
                break;
            }
        }
    }
    EXPECT_TRUE(adjacentBrightSamples) << "a star fixture needs a PSF footprint, not isolated impulses";
}

TEST(SyntheticFixtureTest, GeneratorVersionTwoHasPortableGoldenDigests)
{
    SyntheticFixtureSource source;
    const SampleDescriptor descriptor = floatDescriptor(64, 64, 1);

    EXPECT_EQ(acquireOrFail(source, "synthetic.gaussian_noise", 17, descriptor).digest,
              "177ec58993e532f99953dce40375dc4af08bae6d06ac0e532dcad69dcba1466b");
    EXPECT_EQ(acquireOrFail(source, "synthetic.stars", 17, descriptor).digest,
              "8d27a5e7d1384cd7daf7a16ae64d634f9d6fbb5b4e87e16867f6fce4ec9dbea1");
}

TEST(SyntheticFixtureTest, NonFiniteGeneratorProducesNaNAndBothInfinities)
{
    SyntheticFixtureSource source;
    const SampleDescriptor descriptor = floatDescriptor(7, 1, 1);
    const Fixture fixture = acquireOrFail(source, "synthetic.nonfinite", 0, descriptor);

    EXPECT_TRUE(std::isnan(readFloat(fixture, 0)));
    EXPECT_TRUE(std::isinf(readFloat(fixture, 1)));
    EXPECT_GT(readFloat(fixture, 1), 0.0f);
    EXPECT_TRUE(std::isinf(readFloat(fixture, 2)));
    EXPECT_LT(readFloat(fixture, 2), 0.0f);
    EXPECT_LT(readFloat(fixture, 3), 0.0f) << "a floating model carries legitimate negatives";
    EXPECT_GT(readFloat(fixture, 4), 1.0f) << "and legitimate values above one";
}

TEST(SyntheticFixtureTest, IntegerTypesSpanTheirRange)
{
    SyntheticFixtureSource source;
    SampleDescriptor descriptor = floatDescriptor(2, 1, 1);
    descriptor.sampleType = SampleType::UInt16;

    const Fixture fixture = acquireOrFail(source, "synthetic.extremes", 0, descriptor);
    ASSERT_EQ(fixture.bytes.size(), 4u);
    std::uint16_t low = 0;
    std::uint16_t high = 0;
    std::memcpy(&low, fixture.bytes.data(), sizeof low);
    std::memcpy(&high, fixture.bytes.data() + 2, sizeof high);
    EXPECT_EQ(low, 0u);
    EXPECT_EQ(high, 65535u);
}

TEST(SyntheticFixtureTest, UnknownGeneratorIsReportedRatherThanSubstituted)
{
    SyntheticFixtureSource source;
    Fixture fixture;
    std::string error;
    EXPECT_FALSE(source.acquire("synthetic.nope", 0, floatDescriptor(4, 4, 1), fixture, error));
    EXPECT_NE(error.find("synthetic.nope"), std::string::npos);
    EXPECT_TRUE(fixture.empty());
}

TEST(SyntheticFixtureTest, InvalidDescriptorIsRefused)
{
    SyntheticFixtureSource source;
    Fixture fixture;
    std::string error;
    EXPECT_FALSE(source.acquire("synthetic.ramp", 0, floatDescriptor(0, 4, 1), fixture, error));
    EXPECT_FALSE(error.empty());
}

TEST(SyntheticFixtureTest, NonFiniteGeneratorRefusesAnIntegerType)
{
    SyntheticFixtureSource source;
    SampleDescriptor descriptor = floatDescriptor(4, 4, 1);
    descriptor.sampleType = SampleType::UInt16;

    Fixture fixture;
    std::string error;
    EXPECT_FALSE(source.acquire("synthetic.nonfinite", 0, descriptor, fixture, error));
    EXPECT_NE(error.find("UInt16"), std::string::npos);
}

TEST(SyntheticFixtureTest, UnsupportedLayoutIsRefusedRatherThanApproximated)
{
    SyntheticFixtureSource source;
    SampleDescriptor descriptor = floatDescriptor(4, 4, 3);
    descriptor.layout = Layout::SemiPlanar;

    Fixture fixture;
    std::string error;
    EXPECT_FALSE(source.acquire("synthetic.ramp", 0, descriptor, fixture, error));
    EXPECT_NE(error.find("SemiPlanar"), std::string::npos);
}

TEST(SyntheticFixtureTest, InterleavedAndPlanarAreNotTheSameBytes)
{
    SyntheticFixtureSource source;
    SampleDescriptor planar = floatDescriptor(8, 8, 3);
    SampleDescriptor interleaved = planar;
    interleaved.layout = Layout::Interleaved;

    EXPECT_NE(acquireOrFail(source, "synthetic.checkerboard", 0, planar).digest,
              acquireOrFail(source, "synthetic.checkerboard", 0, interleaved).digest);
}

// --- corpus resolution ------------------------------------------------------------------------

TEST(CorpusVerificationTest, MatchingFileResolvesAndVerifies)
{
    ScratchDirectory scratch("corpus-verify");
    const std::string payload = "raw sample bytes";
    {
        std::ofstream file(scratch.path() / "frame.fit", std::ios::binary);
        file << payload;
    }

    CorpusEntry entry;
    entry.id = "ds.frame";
    entry.relativePath = "frame.fit";
    entry.sha256 = Sha256::of(payload);
    entry.sizeBytes = payload.size();
    entry.redistribution = "local-only";
    entry.expected = makeDescriptor();

    CorpusManifest manifest;
    manifest.add(entry);

    const auto resolution = manifest.resolveVerified("ds.frame", scratch.path().string());
    EXPECT_TRUE(resolution.available) << resolution.skipReason;
    EXPECT_TRUE(resolution.skipReason.empty());
}

TEST(CorpusVerificationTest, AChangedFileIsSkippedRatherThanBenchmarked)
{
    ScratchDirectory scratch("corpus-changed");
    {
        std::ofstream file(scratch.path() / "frame.fit", std::ios::binary);
        file << "re-exported content";
    }

    CorpusEntry entry;
    entry.id = "ds.frame";
    entry.relativePath = "frame.fit";
    entry.sha256 = Sha256::of(std::string("the original content"));
    entry.redistribution = "local-only";
    entry.expected = makeDescriptor();

    CorpusManifest manifest;
    manifest.add(entry);

    const auto resolution = manifest.resolveVerified("ds.frame", scratch.path().string());
    EXPECT_FALSE(resolution.available);
    EXPECT_NE(resolution.skipReason.find("ds.frame"), std::string::npos);
    EXPECT_NE(resolution.skipReason.find("hashes to"), std::string::npos);
}

TEST(CorpusVerificationTest, AWrongSizeIsReportedBeforeTheDigest)
{
    ScratchDirectory scratch("corpus-size");
    {
        std::ofstream file(scratch.path() / "frame.fit", std::ios::binary);
        file << "four";
    }

    CorpusEntry entry;
    entry.id = "ds.frame";
    entry.relativePath = "frame.fit";
    entry.sha256 = Sha256::of(std::string("four"));
    entry.sizeBytes = 4096;
    entry.redistribution = "local-only";
    entry.expected = makeDescriptor();

    CorpusManifest manifest;
    manifest.add(entry);

    const auto resolution = manifest.resolveVerified("ds.frame", scratch.path().string());
    EXPECT_FALSE(resolution.available);
    EXPECT_NE(resolution.skipReason.find("4096"), std::string::npos);
}

TEST(CorpusVerificationTest, AnAbsentDatasetIsSkippedByNameNotFailed)
{
    ScratchDirectory scratch("corpus-absent");

    CorpusEntry entry;
    entry.id = "ds.absent";
    entry.relativePath = "absent.fit";
    entry.redistribution = "local-only";
    entry.expected = makeDescriptor();

    CorpusManifest manifest;
    manifest.add(entry);

    const auto resolution = manifest.resolveVerified("ds.absent", scratch.path().string());
    EXPECT_FALSE(resolution.available);
    EXPECT_NE(resolution.skipReason.find("ds.absent"), std::string::npos);
}

TEST(CorpusVerificationTest, AnEntryWithoutARecordedDigestIsAcceptedOnPresence)
{
    ScratchDirectory scratch("corpus-nodigest");
    {
        std::ofstream file(scratch.path() / "frame.fit", std::ios::binary);
        file << "anything";
    }

    CorpusEntry entry;
    entry.id = "ds.frame";
    entry.relativePath = "frame.fit";
    entry.redistribution = "local-only";
    entry.expected = makeDescriptor();

    CorpusManifest manifest;
    manifest.add(entry);

    EXPECT_TRUE(manifest.resolveVerified("ds.frame", scratch.path().string()).available);
}

TEST(CorpusRootTest, AnExplicitRootWinsOverTheEnvironment)
{
    setEnvironmentValue(kCorpusRootVariable, "D:/from-environment");
    EXPECT_EQ(resolveCorpusRoot("D:/explicit"), "D:/explicit");
    setEnvironmentValue(kCorpusRootVariable, "");
}

TEST(CorpusRootTest, TheEnvironmentLocatesAHostLocalCorpus)
{
    setEnvironmentValue(kCorpusRootVariable, "D:/from-environment");
    EXPECT_EQ(resolveCorpusRoot(""), "D:/from-environment");
    setEnvironmentValue(kCorpusRootVariable, "");
}

TEST(CorpusRootTest, NoCorpusConfiguredIsAnOrdinaryOutcome)
{
    setEnvironmentValue(kCorpusRootVariable, "");
    EXPECT_TRUE(resolveCorpusRoot("").empty());
}

TEST(StarReductionAlignmentFixtureTest, ReductionPairStaysInDomainAndScoresThePublishedEquation)
{
    const std::array<float, 5> entropy{0.0f, 0.1f, 0.5f, 0.9f, 1.0f};
    const StarReductionFixture fixture = makeStarReductionFixture(entropy);
    std::vector<float> reduced(entropy.size(), 0.0f);
    for (std::size_t index = 0u; index < reduced.size(); ++index)
    {
        EXPECT_GT(fixture.target[index], fixture.starless[index]);
        EXPECT_GT(fixture.starless[index], 0.0f);
        EXPECT_LT(fixture.target[index], 1.0f);
        reduced[index] =
            static_cast<float>(strongStarReductionOracle(fixture.target[index], fixture.starless[index], 2u));
    }
    EXPECT_LT(starReductionMaximumError(reduced, fixture, 2u), 3.0e-8);
    reduced[2] = std::numeric_limits<float>::quiet_NaN();
    EXPECT_EQ(starReductionMaximumError(reduced, fixture, 2u), (std::numeric_limits<double>::max)());
}

TEST(StarReductionAlignmentFixtureTest, AlignmentTruthIsDeterministicAndItsResidualBites)
{
    const StarAlignmentFixture first = makeStarAlignmentFixture(40u, 3u, 91u);
    const StarAlignmentFixture second = makeStarAlignmentFixture(40u, 3u, 91u);
    EXPECT_EQ(starAlignmentFixtureText(first), starAlignmentFixtureText(second));
    EXPECT_EQ(first.source.size(), 40u);
    EXPECT_EQ(first.reference.size(), 43u);
    EXPECT_LT(starAlignmentResidual(first.expectedTransform, first), 1.0e-12);

    std::array<double, 9> damaged = first.expectedTransform;
    damaged[2] += 1.0;
    EXPECT_NEAR(starAlignmentResidual(damaged, first), 1.0, 1.0e-12);
    EXPECT_NE(starAlignmentResultText(first.expectedTransform, 40u, 0.0), starAlignmentResultText(damaged, 40u, 1.0));
}

TEST(CatalogFixtureTest, TheTextIsDeterministicAndEveryMagnitudeIsDistinct)
{
    const std::array<float, 5> entropy{0.0f, 0.13f, 0.5f, 0.87f, 1.0f};
    const std::string first = makeCatalogFixtureText(entropy);
    const std::string second = makeCatalogFixtureText(entropy);
    EXPECT_EQ(first, second);

    std::size_t rows = 0u;
    std::size_t begin = 0u;
    std::set<std::string> magnitudes;
    while (begin < first.size())
    {
        const std::size_t end = first.find('\n', begin);
        const std::string line = first.substr(begin, end == std::string::npos ? std::string::npos : end - begin);
        begin = end == std::string::npos ? first.size() : end + 1u;
        if (line.empty() || line.front() == '#' || line.rfind("id,", 0u) == 0u)
        {
            continue;
        }
        ++rows;
        std::vector<std::string> fields;
        std::size_t field = 0u;
        while (field <= line.size())
        {
            const std::size_t comma = line.find(',', field);
            fields.push_back(line.substr(field, comma == std::string::npos ? std::string::npos : comma - field));
            if (comma == std::string::npos)
            {
                break;
            }
            field = comma + 1u;
        }
        ASSERT_EQ(fields.size(), 7u) << line;
        EXPECT_FALSE(fields[0].empty());
        const double rightAscension = std::stod(fields[3]);
        const double declination = std::stod(fields[4]);
        EXPECT_GE(rightAscension, 0.0);
        EXPECT_LT(rightAscension, 360.0);
        EXPECT_GE(declination, -90.0);
        EXPECT_LE(declination, 90.0);
        // Every magnitude is distinct, so the order of an answer cannot depend on how a tie is
        // broken.
        EXPECT_TRUE(magnitudes.insert(fields[5]).second) << fields[5];
    }
    EXPECT_EQ(rows, kCatalogFixtureEntryCount);
}

TEST(CatalogFixtureTest, TheConesAreDeterministicAndSpreadOverTheWholeSphere)
{
    const std::vector<CatalogFixtureCone> first = makeCatalogFixtureCones();
    const std::vector<CatalogFixtureCone> second = makeCatalogFixtureCones();
    ASSERT_EQ(first.size(), kCatalogFixtureQueryCount);
    ASSERT_EQ(second.size(), kCatalogFixtureQueryCount);
    double lowest = 90.0;
    double highest = -90.0;
    for (std::size_t index = 0u; index < first.size(); ++index)
    {
        EXPECT_EQ(first[index].rightAscensionDegrees, second[index].rightAscensionDegrees);
        EXPECT_EQ(first[index].declinationDegrees, second[index].declinationDegrees);
        EXPECT_GE(first[index].rightAscensionDegrees, 0.0);
        EXPECT_LT(first[index].rightAscensionDegrees, 360.0);
        EXPECT_GE(first[index].declinationDegrees, -90.0);
        EXPECT_LE(first[index].declinationDegrees, 90.0);
        lowest = (std::min)(lowest, first[index].declinationDegrees);
        highest = (std::max)(highest, first[index].declinationDegrees);
    }
    EXPECT_LT(lowest, -60.0);
    EXPECT_GT(highest, 60.0);
}

// The suite owns its own entry point, matching the other AfterNight test executables. No Qt and no
// storage-aware service is involved here, so a plain gtest main is all that is needed.
int main(int argc, char** argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

TEST(StackingFixtureTest, DeclaresEveryModeOnceAndRecognisesOnlyItsOwnRows)
{
    using namespace an::bench;
    const std::vector<ScenarioSpec> scenarios = stackingModeScenarios();
    ASSERT_EQ(scenarios.size(), kStackScenarioDefinitions.size());
    std::set<std::string> ids;
    std::set<std::string> methods;
    std::set<std::string> rejections;
    for (std::size_t index = 0u; index < scenarios.size(); ++index)
    {
        const ScenarioSpec& scenario = scenarios[index];
        EXPECT_TRUE(scenario.validate().empty()) << scenario.id;
        EXPECT_TRUE(ids.insert(scenario.id).second) << scenario.id;
        EXPECT_EQ(scenario.group, "stacking");
        EXPECT_EQ(scenario.input.width, kStackScenarioDefinitions[index].width);
        EXPECT_EQ(scenario.input.height, kStackScenarioDefinitions[index].height);
        EXPECT_EQ(scenario.input.sampleType, SampleType::Float32);
        EXPECT_EQ(scenario.parameters.at("frames"), std::to_string(kStackScenarioDefinitions[index].frames));
        EXPECT_EQ(scenario.correctnessClass, CorrectnessClass::AbsoluteRelative);
        EXPECT_GT(scenario.tolerance, 0.0);
        methods.insert(scenario.parameters.at("method"));
        rejections.insert(scenario.parameters.at("rejection"));
        const std::optional<StackScenarioDefinition> definition = stackingModeScenarioDefinition(scenario);
        ASSERT_TRUE(definition.has_value()) << scenario.id;
        EXPECT_STREQ(definition->id, kStackScenarioDefinitions[index].id);
        EXPECT_EQ(definition->mapping, kStackScenarioDefinitions[index].mapping);
        const bool lights = scenario.corpusId == kStackLightCrops;
        if (definition->rejection != std::string("None"))
        {
            EXPECT_EQ(definition->mapping,
                      lights ? StackFrameMapping::PlantedOutliersOnCorpus : StackFrameMapping::PlantedOutliers)
                << scenario.id;
            EXPECT_EQ(definition->tolerance, lights ? kStackLightRejectionTolerance : kStackRejectionTolerance)
                << scenario.id;
            EXPECT_EQ(scenario.parameters.at("fixtureMapping"),
                      lights ? "planted-outliers-corpus-v1" : "planted-outliers-v1");
        }
        if (lights)
        {
            // Every light row reads the registered crops, and its band of 64 rows a frame stays inside them.
            EXPECT_EQ(scenario.inputSource, InputSource::Corpus) << scenario.id;
            EXPECT_EQ(scenario.input.width, kStackLightCropSide) << scenario.id;
            EXPECT_LE(definition->frames * kStackOutlierRows, definition->rejection == std::string("None")
                                                                  ? (std::numeric_limits<std::uint32_t>::max)()
                                                                  : kStackLightCropSide)
                << scenario.id;
            EXPECT_EQ(scenario.elementsProcessed,
                      std::uint64_t{kStackLightCropSide} * kStackLightCropSide * definition->frames)
                << scenario.id;
        }
    }
    std::set<std::uint32_t> lightFrameCounts;
    for (const StackScenarioDefinition& definition : kStackScenarioDefinitions)
    {
        if (definition.corpusId != nullptr && std::string(definition.corpusId) == kStackLightCrops)
        {
            lightFrameCounts.insert(definition.frames);
        }
    }
    EXPECT_EQ(lightFrameCounts, (std::set<std::uint32_t>{8u, 32u, 72u}));
    // Every combination the average row does not already cover, and every rejection family.
    EXPECT_EQ(methods,
              (std::set<std::string>{"Average", "Sum", "Median", "Minimum", "Maximum", "AutoAdaptiveWeightedAverage"}));
    EXPECT_EQ(rejections, (std::set<std::string>{"None", "SigmaClipping", "MadSigmaClipping", "MedianSigmaReplacement",
                                                 "WinsorizedSigmaClipping", "LinearFitClipping", "PercentileClipping",
                                                 "GeneralizedEsdTest"}));

    // The timed batch is the runner's, and any other difference is another row.
    ScenarioSpec changed = scenarios[7];
    changed.parameters[kTimedBatchParameter] = "3";
    EXPECT_TRUE(stackingModeScenarioDefinition(changed).has_value());
    changed.parameters["rejection"] = "None";
    EXPECT_FALSE(stackingModeScenarioDefinition(changed).has_value());
    changed = scenarios[0];
    changed.seed += 1u;
    EXPECT_FALSE(stackingModeScenarioDefinition(changed).has_value());
    changed = scenarios[0];
    changed.input.height = 512u;
    EXPECT_FALSE(stackingModeScenarioDefinition(changed).has_value());
}

TEST(StackingFixtureTest, DerivedFramesPlantOneBandPerFrameAndStayInsideTheUnitInterval)
{
    using namespace an::bench;
    constexpr std::uint32_t kWidth = 4u;
    constexpr std::uint32_t kHeight = 600u;
    std::vector<float> fixture(static_cast<std::size_t>(kWidth) * kHeight);
    for (std::size_t index = 0u; index < fixture.size(); ++index)
    {
        fixture[index] = static_cast<float>(index % 97u) / 97.0f;
    }
    fixture[5] = 0.0f;
    for (std::uint32_t frame = 0u; frame < kStackScenarioFrames; ++frame)
    {
        const std::vector<float> plain = deriveStackFrame(StackFrameMapping::PerSeed, fixture, frame, kWidth, kHeight);
        EXPECT_EQ(plain, fixture);
        const std::vector<float> outliers =
            deriveStackFrame(StackFrameMapping::PlantedOutliers, fixture, frame, kWidth, kHeight);
        const std::vector<float> offset =
            deriveStackFrame(StackFrameMapping::PerFrameOffset, fixture, frame, kWidth, kHeight);
        ASSERT_EQ(outliers.size(), fixture.size());
        ASSERT_EQ(offset.size(), fixture.size());
        const StackRowRange band = stackOutlierRows(frame, kHeight);
        EXPECT_EQ(band.begin, frame * kStackOutlierRows);
        EXPECT_EQ(band.end, (frame + 1u) * kStackOutlierRows);
        const float step = (static_cast<float>(frame) - 3.5f) * kStackFrameStep;
        const float level = (static_cast<float>(frame) - 3.5f) * kStackFrameOffsetStep;
        for (std::size_t index = 0u; index < fixture.size(); ++index)
        {
            const auto row = static_cast<std::uint32_t>(index / kWidth);
            const float field = kStackBaseOffset + kStackBaseScale * fixture[index];
            if (row >= band.begin && row < band.end)
            {
                EXPECT_EQ(outliers[index], frame % 2u == 0u ? kStackHighOutlier : kStackLowOutlier);
            }
            else
            {
                EXPECT_EQ(outliers[index], field + step);
            }
            EXPECT_EQ(offset[index], field + level);
            EXPECT_GT(outliers[index], 0.0f);
            EXPECT_LT(outliers[index], 1.0f);
            EXPECT_GT(offset[index], 0.0f);
            EXPECT_LT(offset[index], 1.0f);
        }
    }
    // A frame past the last planted band plants nothing, and the band clips at the frame's end.
    EXPECT_EQ(stackOutlierRows(10u, kHeight).begin, kHeight);
    EXPECT_EQ(stackOutlierRows(10u, kHeight).end, kHeight);
    EXPECT_EQ(stackOutlierRows(9u, kHeight).begin, 576u);
    EXPECT_EQ(stackOutlierRows(9u, kHeight).end, kHeight);
    EXPECT_DOUBLE_EQ(stackExplicitWeight(0u), 1.0);
    EXPECT_DOUBLE_EQ(stackExplicitWeight(4u), 2.0);
}

TEST(StackingOracleTest, EachCombinationHasItsClosedFormAndSkipsExactZeros)
{
    using namespace an::bench;
    const std::vector<float> a{0.2f, 0.5f, 0.0f, 0.4f};
    const std::vector<float> b{0.4f, 0.1f, 0.6f, 0.4f};
    const std::vector<float> c{0.6f, 0.3f, 0.0f, 0.4f};
    const std::vector<float> d{0.0f, 0.7f, 0.0f, 0.0f};
    const std::array<std::span<const float>, 4u> frames{a, b, c, d};
    const std::vector<double> weights{1.0, 2.0, 3.0, 4.0};

    // Element 0: {0.2, 0.4, 0.6}; element 1: {0.5, 0.1, 0.3, 0.7}; element 2: {0.6}; element 3: {0.4, 0.4, 0.4}.
    const std::vector<float> average{0.4f, 0.4f, 0.6f, 0.4f};
    EXPECT_LT(averageStackResidual(frames, average), 1e-7);
    const std::vector<float> weighted{(0.2f + 0.8f + 1.8f) / 6.0f, (0.5f + 0.2f + 0.9f + 2.8f) / 10.0f, 0.6f, 0.4f};
    EXPECT_LT(weightedAverageStackResidual(frames, weights, weighted), 1e-7);
    // Sums {1.2, 1.6, 0.6, 1.2}, peak 1.6.
    const std::vector<float> sum{0.75f, 1.0f, 0.375f, 0.75f};
    EXPECT_LT(sumStackResidual(frames, sum), 1e-7);
    const std::vector<float> median{0.4f, 0.4f, 0.6f, 0.4f};
    EXPECT_LT(medianStackResidual(frames, median), 1e-7);
    const std::vector<float> minimum{0.2f, 0.1f, 0.6f, 0.4f};
    EXPECT_LT(minimumStackResidual(frames, minimum), 1e-7);
    const std::vector<float> maximum{0.6f, 0.7f, 0.6f, 0.4f};
    EXPECT_LT(maximumStackResidual(frames, maximum), 1e-7);

    // A wrong element is reported by its residual, and a size mismatch is infinite.
    std::vector<float> wrong = median;
    wrong[1] = 0.45f;
    EXPECT_NEAR(medianStackResidual(frames, wrong), 0.05, 1e-6);
    EXPECT_NEAR(sumStackResidual(frames, std::vector<float>{0.75f, 1.0f, 0.5f, 0.75f}), 0.125, 1e-6);
    EXPECT_TRUE(std::isinf(sumStackResidual(frames, std::vector<float>{0.75f})));
    EXPECT_TRUE(std::isinf(weightedAverageStackResidual(frames, std::vector<double>{1.0}, weighted)));

    // An element no frame reached is zero everywhere, and a sum with no positive peak is zero.
    const std::vector<float> zeros{0.0f, 0.0f};
    const std::array<std::span<const float>, 2u> empty{zeros, zeros};
    using Oracle = double (*)(std::span<const std::span<const float>>, std::span<const float>);
    const Oracle averageOracle = [](std::span<const std::span<const float>> set, std::span<const float> result) {
        return averageStackResidual(set, result);
    };
    const Oracle medianOracle = [](std::span<const std::span<const float>> set, std::span<const float> result) {
        return medianStackResidual(set, result);
    };
    for (const Oracle oracle : {averageOracle, Oracle{&sumStackResidual}, medianOracle, Oracle{&minimumStackResidual},
                                Oracle{&maximumStackResidual}, Oracle{&autoAdaptiveStackResidual}})
    {
        EXPECT_EQ(oracle(empty, zeros), 0.0);
        EXPECT_GT(oracle(empty, std::vector<float>{0.0f, 0.5f}), 0.49);
    }
}

TEST(StackingOracleTest, ALightRowAcceptsTheValueHeldToTheUnitIntervalAndAPlantedShiftComparesTwoStacks)
{
    using namespace an::bench;
    // A hot pixel averages above one: one, the value held to the unit interval, and the value both pass.
    const std::vector<float> a{0.5f, 3.0f}, b{0.5f, 5.0f};
    const std::array<std::span<const float>, 2u> frames{a, b};
    const std::vector<float> unclamped{0.5f, 4.0f};
    const std::vector<float> clamped{0.5f, 1.0f};
    const std::vector<float> wrong{0.5f, 2.0f};
    EXPECT_EQ(averageStackResidual(frames, unclamped, StackClamp::Accepted), 0.0);
    EXPECT_EQ(averageStackResidual(frames, clamped, StackClamp::Accepted), 0.0);
    EXPECT_EQ(averageStackResidual(frames, clamped, StackClamp::Refused), 3.0);
    EXPECT_EQ(averageStackResidual(frames, wrong, StackClamp::Accepted), 1.0);
    EXPECT_EQ(medianStackResidual(frames, clamped, StackClamp::Accepted), 0.0);
    EXPECT_EQ(weightedAverageStackResidual(frames, std::vector<double>{1.0, 1.0}, clamped, StackClamp::Accepted), 0.0);
    // Inside the interval nothing changes.
    const std::vector<float> c{0.25f, 0.75f};
    const std::array<std::span<const float>, 2u> inside{c, c};
    EXPECT_EQ(averageStackResidual(inside, std::vector<float>{0.25f, 0.5f}, StackClamp::Accepted), 0.25);

    // Three sky elements and one star core, two frames; frame 1 planted 0.95 over the first two elements. The
    // core, where 0.95 is not an outlier, and the third element, where nothing was planted, are not judged.
    const std::vector<float> f0{0.05f, 0.04f, 0.05f, 3.0f}, f1{0.06f, 0.05f, 0.06f, 4.0f};
    const std::vector<float> p1{0.95f, 0.95f, 0.06f, 0.95f};
    const std::array<std::span<const float>, 2u> captured{f0, f1};
    const std::array<std::span<const float>, 2u> plantedFrames{f0, p1};
    const std::vector<float> clean{0.055f, 0.045f, 0.055f, 3.5f};
    PlantedStackMovement movement = plantedStackMovement(clean, clean, plantedFrames, captured, 0.95f, 0.475f);
    EXPECT_EQ(movement.judged, 2u);
    EXPECT_EQ(movement.moved, 0u);
    EXPECT_EQ(movement.fraction, 0.0);
    // Keeping the outlier moves an element of two frames by at least (0.95 - 0.475) / 2; half of that is the bound.
    const std::vector<float> keptOne{0.50f, 0.045f, 0.055f, 2.0f};
    movement = plantedStackMovement(keptOne, clean, plantedFrames, captured, 0.95f, 0.475f);
    EXPECT_EQ(movement.moved, 1u);
    EXPECT_DOUBLE_EQ(movement.fraction, 0.5);
    EXPECT_NEAR(movement.largest, 0.445, 1e-6);
    const std::vector<float> small{0.17f, 0.045f, 0.055f, 3.5f};
    EXPECT_EQ(plantedStackMovement(small, clean, plantedFrames, captured, 0.95f, 0.475f).moved, 0u);
    EXPECT_TRUE(std::isinf(
        plantedStackMovement(std::vector<float>{0.1f}, clean, plantedFrames, captured, 0.95f, 0.475f).fraction));
    EXPECT_TRUE(std::isinf(plantedStackMovement(std::vector<float>{std::nanf(""), 0.045f, 0.055f, 3.5f}, clean,
                                                plantedFrames, captured, 0.95f, 0.475f)
                               .fraction));
}

TEST(StackingOracleTest, TheAutoAdaptiveMeanKeepsATightColumnAndDownWeightsAnOutlier)
{
    using namespace an::bench;
    // Four samples within four counts of 65,535: sigma below five, so the plain mean stands.
    const std::vector<float> t0{0.50000f}, t1{0.50003f}, t2{0.50001f}, t3{0.50002f};
    const std::array<std::span<const float>, 4u> tight{t0, t1, t2, t3};
    const std::vector<float> plain{(0.50000f + 0.50003f + 0.50001f + 0.50002f) / 4.0f};
    EXPECT_LT(autoAdaptiveStackResidual(tight, plain), 1e-7);

    // Three at one half and one far above: the reweighting pulls the mean towards the three, so
    // the plain mean is refused and a value near the cluster is accepted.
    const std::vector<float> o0{0.5f}, o1{0.5f}, o2{0.5f}, o3{0.9f};
    const std::array<std::span<const float>, 4u> outlier{o0, o1, o2, o3};
    EXPECT_GT(autoAdaptiveStackResidual(outlier, std::vector<float>{0.6f}), 0.05);
    EXPECT_LT(autoAdaptiveStackResidual(outlier, std::vector<float>{0.5f}), 0.05);

    // Two samples are below the minimum the algorithm reweights, so their plain mean stands.
    const std::array<std::span<const float>, 2u> pair{o0, o3};
    EXPECT_LT(autoAdaptiveStackResidual(pair, std::vector<float>{0.7f}), 1e-7);
}

TEST(StackingOracleTest, TheRejectedMeanExcludesEachFramesPlantedRowsAndAPlaneIsHeldElementForElement)
{
    using namespace an::bench;
    // Width two, three rows; frame 0 plants row 0, frame 1 plants row 2, frame 2 plants nothing.
    const std::vector<float> f0{9.0f, 9.0f, 0.2f, 0.4f, 0.6f, 0.8f};
    const std::vector<float> f1{0.4f, 0.4f, 0.4f, 0.4f, 9.0f, 9.0f};
    const std::vector<float> f2{0.6f, 0.2f, 0.0f, 0.4f, 0.6f, 0.2f};
    const std::array<std::span<const float>, 3u> frames{f0, f1, f2};
    const std::array<StackRowRange, 3u> excluded{StackRowRange{0u, 1u}, StackRowRange{2u, 3u}, StackRowRange{0u, 0u}};
    const std::vector<float> expected{0.5f, 0.3f, 0.3f, 0.4f, 0.6f, 0.5f};
    EXPECT_LT(rejectedAverageStackResidual(frames, 2u, excluded, expected), 1e-7);
    std::vector<float> wrong = expected;
    wrong[0] = 0.6f;
    EXPECT_NEAR(rejectedAverageStackResidual(frames, 2u, excluded, wrong), 0.1, 1e-6);
    EXPECT_TRUE(std::isinf(rejectedAverageStackResidual(frames, 0u, excluded, expected)));
    EXPECT_TRUE(std::isinf(rejectedAverageStackResidual(frames, 2u, std::span<const StackRowRange>{}, expected)));

    EXPECT_EQ(planeResidual(f2, f2), 0.0);
    EXPECT_NEAR(planeResidual(f2, f1), 8.8, 1e-6);
    EXPECT_TRUE(std::isinf(planeResidual(f2, expected.size() == 6u ? std::span<const float>{f2}.first(5u) : f2)));
}

TEST(DrizzleExecutionFixtureTest, RejectsChangedFrameWorkloadAndChecksEveryFrameInItsOracle)
{
    using namespace an::bench;
    auto scenarios = drizzleExecutionScenarios();
    auto changed = scenarios[0];
    changed.parameters["frameCount"] = "1";
    EXPECT_FALSE(drizzleExecutionScenarioIndex(changed).has_value());
    changed = scenarios[1];
    changed.parameters[kTimedBatchParameter] = "5";
    EXPECT_EQ(drizzleExecutionScenarioIndex(changed), 1u);
    std::vector<float> black(static_cast<std::size_t>(kDrizzleAccumulationWidth) * kDrizzleAccumulationHeight, 0.0f);
    auto frames = drizzleExecutionFrames(black);
    auto oracle = drizzleExecutionOracle(frames, 1u);
    EXPECT_DOUBLE_EQ(oracle.means.front(), 0.125);
    EXPECT_DOUBLE_EQ(oracle.means.back(), 0.125);
    EXPECT_DOUBLE_EQ(oracle.weights.front(), 3.0);
    frames.back().back() = 1.0f;
    oracle = drizzleExecutionOracle(frames, 1u);
    EXPECT_DOUBLE_EQ(oracle.means.back(), 1.125 / 3.0);
    oracle = drizzleExecutionOracle(frames, 1u, true);
    EXPECT_DOUBLE_EQ(oracle.means.back(), 1.0);
    EXPECT_DOUBLE_EQ(oracle.means.front(), 1.0 / 3.0);
    EXPECT_DOUBLE_EQ(oracle.weights.front(), 3.0);
    EXPECT_EQ(drizzleExecutionScenarioIndex(scenarios[4]), 1u);
    changed = scenarios[4];
    changed.parameters.erase("combination");
    EXPECT_FALSE(drizzleExecutionScenarioIndex(changed).has_value());
}

TEST(CodecLookupFixtureTest, PinsEveryPathAndItsExpectedContainer)
{
    auto scenario = an::bench::codecLookupScenario();
    EXPECT_TRUE(an::bench::matchesCodecLookupScenario(scenario));
    EXPECT_EQ(scenario.elementsProcessed, 32u);
    EXPECT_EQ(scenario.correctnessClass, an::bench::CorrectnessClass::ExactAfterMapping);
    scenario.parameters[an::bench::kTimedBatchParameter] = "8192";
    EXPECT_TRUE(an::bench::matchesCodecLookupScenario(scenario));
    scenario.parameters["paths"] += "new-input";
    EXPECT_FALSE(an::bench::matchesCodecLookupScenario(scenario));
    scenario = an::bench::codecLookupScenario();
    ++scenario.seed;
    EXPECT_FALSE(an::bench::matchesCodecLookupScenario(scenario));
    std::array<unsigned, 7u> counts{};
    for (const auto& item : an::bench::kCodecLookupCases)
        ++counts[item.expected];
    EXPECT_EQ(counts, (std::array<unsigned, 7u>{4u, 3u, 1u, 18u, 1u, 3u, 2u}));
}

TEST(PipelineScenarioTest, DeclaresTheReferenceRow)
{
    using namespace an::bench;
    const ScenarioSpec scenario = pipelineScenario();
    EXPECT_TRUE(scenario.validate().empty());
    EXPECT_EQ(scenario.id, kPipelineScenarioId);
    EXPECT_EQ(scenario.group, "pipelines");
    EXPECT_EQ(scenario.corpusId, kPipelineFrameSetId);
    EXPECT_EQ(scenario.inputSource, InputSource::CorpusFrameSet);
    EXPECT_EQ(scenario.input.sampleType, SampleType::UInt16);
    EXPECT_EQ(scenario.input.width, 8288u);
    EXPECT_EQ(scenario.input.height, 5644u);
    EXPECT_EQ(scenario.correctnessClass, CorrectnessClass::GeometricResidual);
    EXPECT_DOUBLE_EQ(scenario.tolerance, kPipelineResidualTolerancePixels);
    EXPECT_TRUE(scenario.allowedBackends.empty());
    EXPECT_EQ(scenario.parameters.at("lights"), "72");
    EXPECT_EQ(scenario.parameters.at("flats"), "40");
    EXPECT_EQ(scenario.parameters.at("darks"), "60");
    EXPECT_EQ(scenario.parameters.at("bias"), "60");
    // Two independent calls must be byte-identical, because every run makes one.
    EXPECT_EQ(json::toJson(scenario, false), json::toJson(pipelineScenario(), false));
}

TEST(PipelineScenarioTest, RoleLimitsSelectTheDeclaredCountAndNeverMoreThanTheSetHolds)
{
    using namespace an::bench;
    ScenarioSpec scenario = pipelineScenario();
    EXPECT_EQ(pipelineRoleLimit(scenario, "lights", 72u), 72u);
    EXPECT_EQ(pipelineRoleLimit(scenario, "lights", 8u), 8u);
    scenario.parameters["lights"] = "0";
    EXPECT_EQ(pipelineRoleLimit(scenario, "lights", 72u), 72u);
    scenario.parameters["lights"] = "not-a-number";
    EXPECT_EQ(pipelineRoleLimit(scenario, "lights", 72u), 72u);
    scenario.parameters.erase("lights");
    EXPECT_EQ(pipelineRoleLimit(scenario, "lights", 72u), 72u);
    scenario.parameters["lights"] = "4";
    EXPECT_EQ(pipelineRoleLimit(scenario, "lights", 72u), 4u);
}

TEST(PipelineScenarioTest, ResidualSummaryScoresTheMedianAndKeepsBothTails)
{
    using namespace an::bench;
    const std::array<double, 5u> odd{0.4, 0.1, 0.9, 0.3, 0.2};
    PipelineResidualSummary summary = summarizePipelineResiduals(odd);
    EXPECT_EQ(summary.count, 5u);
    EXPECT_DOUBLE_EQ(summary.medianPixels, 0.3);
    EXPECT_DOUBLE_EQ(summary.minimumPixels, 0.1);
    EXPECT_DOUBLE_EQ(summary.maximumPixels, 0.9);

    const std::array<double, 4u> even{1.0, 2.0, 4.0, 8.0};
    summary = summarizePipelineResiduals(even);
    EXPECT_DOUBLE_EQ(summary.medianPixels, 3.0);

    // A frame that reported nothing is not a residual of zero.
    const std::array<double, 3u> partial{std::numeric_limits<double>::quiet_NaN(), 0.5,
                                         std::numeric_limits<double>::infinity()};
    summary = summarizePipelineResiduals(partial);
    EXPECT_EQ(summary.count, 1u);
    EXPECT_DOUBLE_EQ(summary.medianPixels, 0.5);

    EXPECT_EQ(summarizePipelineResiduals(std::span<const double>{}).count, 0u);
}

TEST(PipelineScenarioTest, StageCountersCarryEveryStageAndTheGeometryTheRowIsScoredOn)
{
    using namespace an::bench;
    PipelineStageTimings timings;
    timings.masterBias = {1.0, 2.0};
    timings.stack = {3.0, 4.0};
    timings.framesProcessed = 232u;
    timings.lightsCalibrated = 72u;
    timings.lightsRegistered = 72u;
    timings.lightsAligned = 72u;
    timings.lightsStacked = 72u;
    const std::array<double, 3u> residuals{0.2, 0.4, 0.6};
    timings.residuals = summarizePipelineResiduals(residuals);

    ResultRecord record;
    timings.writeCounters(record);
    for (const char* stage : {"masterBias", "masterDark", "masterFlat", "calibrateLights", "detectStars",
                              "registerStars", "align", "stack"})
    {
        EXPECT_EQ(record.counters.count(std::string("stage.") + stage + "Ms"), 1u) << stage;
        EXPECT_EQ(record.counters.count(std::string("stage.") + stage + "CpuMs"), 1u) << stage;
    }
    EXPECT_DOUBLE_EQ(record.counters.at("stage.masterBiasMs"), 1.0);
    EXPECT_DOUBLE_EQ(record.counters.at("stage.stackCpuMs"), 4.0);
    EXPECT_DOUBLE_EQ(record.counters.at("framesProcessed"), 232.0);
    EXPECT_DOUBLE_EQ(record.counters.at("lightsStacked"), 72.0);
    EXPECT_DOUBLE_EQ(record.counters.at("alignmentResidualFrames"), 3.0);
    EXPECT_DOUBLE_EQ(record.counters.at("alignmentResidualMedianPx"), 0.4);
    EXPECT_DOUBLE_EQ(record.counters.at("alignmentResidualMinPx"), 0.2);
    EXPECT_DOUBLE_EQ(record.counters.at("alignmentResidualMaxPx"), 0.6);
}

namespace
{

} // namespace

TEST(StackingFixtureTest, ACapturedFramePlantsOnlyItsOwnBandOfTheHighOutlier)
{
    using namespace an::bench;
    constexpr std::uint32_t kWidth = 3u;
    constexpr std::uint32_t kHeight = 200u;
    std::vector<float> capture(static_cast<std::size_t>(kWidth) * kHeight);
    for (std::size_t index = 0u; index < capture.size(); ++index)
    {
        capture[index] = 0.03f + static_cast<float>(index % 13u) / 1000.0f;
    }
    for (std::uint32_t frame = 0u; frame < 4u; ++frame)
    {
        const std::vector<float> planted =
            deriveStackFrame(StackFrameMapping::PlantedOutliersOnCorpus, capture, frame, kWidth, kHeight);
        const StackRowRange band = stackOutlierRows(frame, kHeight);
        for (std::uint32_t row = 0u; row < kHeight; ++row)
        {
            for (std::uint32_t x = 0u; x < kWidth; ++x)
            {
                const std::size_t index = static_cast<std::size_t>(row) * kWidth + x;
                const bool inside = row >= band.begin && row < band.end;
                EXPECT_EQ(planted[index], inside ? kStackHighOutlier : capture[index]) << frame << ", " << row;
            }
        }
    }
    EXPECT_STREQ(stackFrameMappingName(StackFrameMapping::PlantedOutliersOnCorpus), "planted-outliers-corpus-v1");
    // Closed-form weights follow the row's frame count.
    EXPECT_EQ(stackScenarioWeights("Explicit", 32u).size(), 32u);
    EXPECT_DOUBLE_EQ(stackScenarioWeights("Explicit", 32u)[31], stackExplicitWeight(31u));
}
