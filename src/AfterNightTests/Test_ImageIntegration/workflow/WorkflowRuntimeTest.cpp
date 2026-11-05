// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The workflow core on the consumer runtime: what a run lends its stages - the session, the parallel-cores setting as
/// the width of each stage operation and nothing wider, a snapshot of the RAW treatment and a registry of the run's
/// own, the run's stop - the archive it commits through, the pending files it reclaims, the counters it merges, and the
/// frame cache held to the session's derived-cache allowance.

// oneTBB before Qt: Qt defines emit as a keyword, and oneTBB declares a member of that name.
#include <tbb/info.h>
#include <tbb/task_arena.h>

#include "Test_ImageIntegration/ImportSession.h"

#include "processes/ImageIntegration/workflow/DocumentPublication.h"
#include "processes/ImageIntegration/workflow/WorkflowExecutor.h"
#include "processes/ImageIntegration/workflow/WorkflowImageCache.h"
#include "processes/ImageIntegration/workflow/WorkflowManifest.h"
#include "processes/ImageIntegration/workflow/WorkflowRawLoadPolicy.h"
#include "processes/ImageIntegration/workflow/WorkflowRunArchive.h"
#include "processes/ImageIntegration/workflow/WorkflowStageServices.h"
#include "processes/ImageIntegration/workflow/stages/IWorkflowStage.h"

#include <AfterNightConsumerRuntime/ConsumerMemory.h>
#include <AfterNightConsumerRuntime/EngineSession.h>
#include <AfterNightConsumerRuntime/ImageAsset.h>
#include <AfterNightConsumerRuntime/OperationScope.h>
#include <AfterNightEngine/IO/CodecRegistry.h>
#include <AfterNightEngine/IO/FitsCodec.h>
#include <AfterNightEngine/IO/ImageCodec.h>
#include <AfterNightEngine/Image/Image.h>
#include <AfterNightEngine/Image/ImageDescriptor.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <gtest/gtest.h>

#include <array>
#include <atomic>
#include <chrono>
#include <future>
#include <memory>
#include <optional>
#include <thread>

namespace
{

using namespace std::chrono_literals;

WorkflowManifest minimalManifest(const QString& outputDirectory)
{
    WorkflowManifest manifest;
    manifest.outputDirectory = outputDirectory;
    WorkflowManifest::RegistrationGroup registration;
    registration.groupKey = GroupKey{QStringLiteral("test|64x64|TestObject")};
    registration.resolution = QStringLiteral("64x64");
    registration.object = QStringLiteral("TestObject");
    manifest.registrationGroups.push_back(registration);
    return manifest;
}

/// What one stage saw of its run while it ran.
struct StageObservation
{
    const ImageIntegration::WorkflowStageServices* services = nullptr;
    std::uint32_t servicesWidth = 0;
    std::optional<ImageIntegration::WorkflowRawPolicy> rawPolicy;
    std::shared_ptr<const an::CodecRegistry> registry;
    std::uint32_t grantedConcurrency = 0;
    int processWideConcurrency = 0;
    bool stopObserved = false;
};

/// A stage that admits one operation through the run's services and reports what it was given.
class ProbeStage final : public IWorkflowStage
{
public:
    using WhileAdmitted = std::function<void(const an::consumer::OperationScope&)>;

    ProbeStage(StageObservation& observation, WhileAdmitted whileAdmitted = {})
        : m_observation(observation), m_whileAdmitted(std::move(whileAdmitted))
    {
    }

    [[nodiscard]] QString name() const override
    {
        return QStringLiteral("Probe");
    }

    [[nodiscard]] WorkflowStage stageId() const override
    {
        return WorkflowStage::Bias;
    }

    StageResult execute(const WorkflowManifest&, CancellationToken&, ProgressReporter&) override
    {
        m_observation.processWideConcurrency = tbb::this_task_arena::max_concurrency();
        m_observation.services = stageServices();
        if (stageServices() == nullptr)
        {
            return StageResult::success();
        }
        m_observation.servicesWidth = stageServices()->parallelCores();
        m_observation.rawPolicy = stageServices()->rawPolicy();
        m_observation.registry = stageServices()->codecRegistry();
        an::Result<an::consumer::OperationScope> scope = stageServices()->admitStageOperation();
        if (!scope.hasValue())
        {
            return StageResult::failure(QString::fromUtf8(scope.status().message()));
        }
        m_observation.grantedConcurrency = scope->grantedConcurrency();
        if (m_whileAdmitted)
        {
            m_whileAdmitted(*scope);
        }
        m_observation.stopObserved = scope->stopRequested();
        return m_observation.stopObserved ? StageResult::cancelled() : StageResult::success();
    }

private:
    StageObservation& m_observation;
    WhileAdmitted m_whileAdmitted;
};

class WorkflowRuntimeTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        ASSERT_TRUE(m_output.isValid());
        auto session = an::tests::integration::openImportSession(4u);
        ASSERT_TRUE(session.hasValue()) << session.status().message();
        m_session.emplace(std::move(session).value());
    }

    QTemporaryDir m_output;
    std::optional<an::consumer::EngineSession> m_session;
};

TEST_F(WorkflowRuntimeTest, TheParallelCoresSettingNarrowsEachStageOperationAndCapsNothingElse)
{
    if (std::thread::hardware_concurrency() < 2u)
    {
        GTEST_SKIP() << "one hardware thread leaves no narrower width to set";
    }
    const int processWide = tbb::info::default_concurrency();
    std::uint32_t otherGranted = 0;
    StageObservation observation;
    WorkflowExecutor executor;
    executor.setCodecRegistryFactory([](const ImageIntegration::WorkflowRawPolicy&) { return nullptr; });
    executor.addStage(std::make_unique<ProbeStage>(observation, [&](const an::consumer::OperationScope&) {
        // While the run's operation is admitted, another reader of the session is admitted at once beside it on the
        // threads the run does not use.
        auto other = std::async(std::launch::async, [&]() {
            an::consumer::OperationRequest request;
            request.purpose = an::consumer::OperationPurpose::Batch;
            auto scope = m_session->tryAdmit(request);
            return scope.hasValue() ? scope->grantedConcurrency() : 0u;
        });
        otherGranted = other.get();
    }));

    WorkflowManifest manifest = minimalManifest(m_output.path());
    manifest.maxParallelCores = 1;
    WorkflowExecutionOptions options;
    options.session = &*m_session;
    const WorkflowExecutor::Result result = executor.execute(manifest, {}, options);

    ASSERT_TRUE(result.success) << result.errorMessage.toStdString();
    ASSERT_NE(observation.services, nullptr);
    EXPECT_EQ(observation.servicesWidth, 1u);
    EXPECT_EQ(observation.grantedConcurrency, 1u);
    EXPECT_EQ(observation.processWideConcurrency, processWide) << "the run capped the thread pool of the process";
    EXPECT_GE(otherGranted, 2u);
}

TEST_F(WorkflowRuntimeTest, AnUnlimitedRunAsksForAsManyThreadsAsItsPurposeMayHave)
{
    StageObservation observation;
    WorkflowExecutor executor;
    executor.setCodecRegistryFactory([](const ImageIntegration::WorkflowRawPolicy&) { return nullptr; });
    executor.addStage(std::make_unique<ProbeStage>(observation));
    WorkflowExecutionOptions options;
    options.session = &*m_session;
    const WorkflowExecutor::Result result = executor.execute(minimalManifest(m_output.path()), {}, options);
    ASSERT_TRUE(result.success) << result.errorMessage.toStdString();
    EXPECT_EQ(observation.servicesWidth, 0u);
    EXPECT_EQ(observation.grantedConcurrency, m_session->width());
}

TEST_F(WorkflowRuntimeTest, TheRunReadsUnderASnapshotOfItsRawTreatmentThroughARegistryOfItsOwn)
{
    int registriesBuilt = 0;
    std::optional<ImageIntegration::WorkflowRawPolicy> askedFor;
    std::shared_ptr<const an::CodecRegistry> built;
    StageObservation observation;
    WorkflowExecutor executor;
    executor.setCodecRegistryFactory([&](const ImageIntegration::WorkflowRawPolicy& policy) {
        ++registriesBuilt;
        askedFor = policy;
        auto fits = an::createFitsCodec();
        if (fits.hasValue())
        {
            const std::array<std::shared_ptr<const an::ImageCodec>, 1u> codecs{*fits};
            auto registry = an::CodecRegistry::create(codecs);
            built = registry.hasValue() ? std::move(registry).value() : nullptr;
        }
        return built;
    });
    executor.addStage(std::make_unique<ProbeStage>(observation));
    WorkflowExecutionOptions options;
    options.session = &*m_session;
    const WorkflowExecutor::Result result = executor.execute(minimalManifest(m_output.path()), {}, options);

    ASSERT_TRUE(result.success) << result.errorMessage.toStdString();
    EXPECT_EQ(registriesBuilt, 1);
    ASSERT_TRUE(askedFor.has_value());
    EXPECT_EQ(*askedFor, ImageIntegration::workflowRawPolicy());
    EXPECT_EQ(askedFor->cfaWhiteBalance, an::CfaWhiteBalanceMode::None);
    EXPECT_EQ(askedFor->fullColorWhiteBalance, an::RawFullColorWhiteBalance::None);
    ASSERT_TRUE(observation.rawPolicy.has_value());
    EXPECT_EQ(*observation.rawPolicy, ImageIntegration::workflowRawPolicy());
    ASSERT_NE(built, nullptr);
    EXPECT_EQ(observation.registry, built);
}

TEST_F(WorkflowRuntimeTest, TheProductRegistryReadsCameraFramesWithNoBalance)
{
    const an::RawCodecOptions options =
        ImageIntegration::workflowRawCodecOptions(ImageIntegration::workflowRawPolicy());
    EXPECT_EQ(options.fullColorWhiteBalance, an::RawFullColorWhiteBalance::None);

    StageObservation observation;
    WorkflowExecutor executor;
    executor.addStage(std::make_unique<ProbeStage>(observation));
    WorkflowExecutionOptions executionOptions;
    executionOptions.session = &*m_session;
    const WorkflowExecutor::Result result = executor.execute(minimalManifest(m_output.path()), {}, executionOptions);
    ASSERT_TRUE(result.success) << result.errorMessage.toStdString();
    EXPECT_NE(observation.registry, nullptr);
}

TEST_F(WorkflowRuntimeTest, ACancellationStopsTheRunsEngineWork)
{
    StageObservation observation;
    std::atomic<bool> admitted{false};
    WorkflowExecutor executor;
    executor.setCodecRegistryFactory([](const ImageIntegration::WorkflowRawPolicy&) { return nullptr; });
    executor.addStage(std::make_unique<ProbeStage>(observation, [&](const an::consumer::OperationScope& scope) {
        admitted = true;
        const auto deadline = std::chrono::steady_clock::now() + 10s;
        while (!scope.stopRequested() && std::chrono::steady_clock::now() < deadline)
        {
            std::this_thread::sleep_for(2ms);
        }
    }));
    std::thread canceller([&]() {
        const auto deadline = std::chrono::steady_clock::now() + 10s;
        while (!admitted && std::chrono::steady_clock::now() < deadline)
        {
            std::this_thread::sleep_for(2ms);
        }
        executor.requestCancel();
    });
    WorkflowExecutionOptions options;
    options.session = &*m_session;
    const WorkflowExecutor::Result result = executor.execute(minimalManifest(m_output.path()), {}, options);
    canceller.join();

    EXPECT_TRUE(observation.stopObserved);
    EXPECT_TRUE(result.cancelled);
    EXPECT_FALSE(result.success);
}

TEST_F(WorkflowRuntimeTest, ARunWithoutASessionLendsItsStagesNothingAndSaysSo)
{
    StageObservation observation;
    QStringList log;
    WorkflowExecutor executor;
    executor.setLogCallback([&](WorkflowLogLevel, const QString& message) { log.append(message); });
    executor.addStage(std::make_unique<ProbeStage>(observation));
    const WorkflowExecutor::Result result = executor.execute(minimalManifest(m_output.path()));
    ASSERT_TRUE(result.success) << result.errorMessage.toStdString();
    EXPECT_EQ(observation.services, nullptr);
    EXPECT_TRUE(log.join(QLatin1Char('\n')).contains(QStringLiteral("No engine session")));
    EXPECT_FALSE(result.engineMemory.has_value()) << "a run with no session counted an engine's memory";
}

/// An archive that records what the executor asked of it.
class RecordingArchive final : public IWorkflowRunArchive
{
public:
    struct Calls
    {
        int commits = 0;
        int reverts = 0;
        int timings = 0;
        int logs = 0;
        QString reportPath;
        bool succeed = true;
    };

    explicit RecordingArchive(Calls& calls) : m_calls(calls) {}

    CommitOutcome commit(const WorkflowManifest&, WorkflowExecutor::Result&, const CapturePhaseDuration&) override
    {
        ++m_calls.commits;
        CommitOutcome outcome;
        outcome.success = m_calls.succeed;
        outcome.reportPath = m_calls.reportPath;
        outcome.error = m_calls.succeed ? QString() : QStringLiteral("archive refused");
        return outcome;
    }

    void revertStagedFiles(const WorkflowExecutor::Result&) override
    {
        ++m_calls.reverts;
    }

    void finalizeTimingArtifacts(const WorkflowExecutor::Result&) override
    {
        ++m_calls.timings;
    }

    void finalizeLogArtifact(const WorkflowExecutor::Result&) override
    {
        ++m_calls.logs;
    }

private:
    Calls& m_calls;
};

TEST_F(WorkflowRuntimeTest, ASuccessfulRunCommitsThroughTheArchiveItsComposerNamed)
{
    RecordingArchive::Calls calls;
    const QString reportDirectory = m_output.filePath(QStringLiteral("history/run"));
    ASSERT_TRUE(QDir().mkpath(reportDirectory));
    calls.reportPath = QDir(reportDirectory).filePath(QStringLiteral("workflow_report.md"));
    QFile html(QDir(reportDirectory).filePath(QStringLiteral("workflow_report.html")));
    ASSERT_TRUE(html.open(QIODevice::WriteOnly));
    html.close();

    QString archivedFor;
    StageObservation observation;
    WorkflowExecutor executor;
    executor.setRunArchiveFactory(
        [&](const QString& outputDirectory, WorkflowExecutionCounterAccumulator*, const an::consumer::EngineSession*) {
            archivedFor = outputDirectory;
            return std::make_unique<RecordingArchive>(calls);
        });
    executor.addStage(std::make_unique<ProbeStage>(observation));
    const WorkflowExecutor::Result result = executor.execute(minimalManifest(m_output.path()));

    ASSERT_TRUE(result.success) << result.errorMessage.toStdString();
    EXPECT_EQ(archivedFor, m_output.path());
    EXPECT_EQ(calls.commits, 1);
    EXPECT_EQ(calls.reverts, 0);
    EXPECT_EQ(calls.timings, 1);
    EXPECT_EQ(calls.logs, 1);
    EXPECT_EQ(result.htmlReportPath, html.fileName());
}

TEST_F(WorkflowRuntimeTest, AnArchiveThatFailsFailsTheRunAndARunWithNoArchiveStillSucceeds)
{
    RecordingArchive::Calls calls;
    calls.succeed = false;
    StageObservation observation;
    WorkflowExecutor executor;
    executor.setRunArchiveFactory(
        [&](const QString&, WorkflowExecutionCounterAccumulator*, const an::consumer::EngineSession*) {
            return std::make_unique<RecordingArchive>(calls);
        });
    executor.addStage(std::make_unique<ProbeStage>(observation));
    const WorkflowExecutor::Result failed = executor.execute(minimalManifest(m_output.path()));
    EXPECT_FALSE(failed.success);
    EXPECT_TRUE(failed.errorMessage.contains(QStringLiteral("archive refused")));

    WorkflowExecutor bare;
    bare.addStage(std::make_unique<ProbeStage>(observation));
    const WorkflowExecutor::Result result = bare.execute(minimalManifest(m_output.path()));
    EXPECT_TRUE(result.success) << result.errorMessage.toStdString();
    EXPECT_TRUE(result.htmlReportPath.isEmpty());
}

TEST_F(WorkflowRuntimeTest, AnInterruptedRunsPendingFilesAreReclaimedAndNothingElse)
{
    const QDir root(m_output.path());
    ASSERT_TRUE(root.mkpath(QStringLiteral("calibrated_lights")));
    ASSERT_TRUE(root.mkpath(QStringLiteral("registered_lights/group")));
    ASSERT_TRUE(root.mkpath(QStringLiteral(".afternight/image_integration/artifacts/drizzle/group/pixmaps")));
    const QStringList pending{root.filePath(QStringLiteral("calibrated_lights/light.fits.1234.antmp")),
                              root.filePath(QStringLiteral("registered_lights/group/light_reg.fits.5.antmp")),
                              root.filePath(QStringLiteral(".afternight/image_integration/artifacts/drizzle/group/"
                                                           "pixmaps/light.andesc.abc.anpending"))};
    const QStringList committed{root.filePath(QStringLiteral("calibrated_lights/light.fits")),
                                root.filePath(QStringLiteral(".afternight/image_integration/artifacts/drizzle/group/"
                                                             "pixmaps/light.andesc"))};
    for (const QString& path : pending + committed)
    {
        QFile file(path);
        ASSERT_TRUE(file.open(QIODevice::WriteOnly)) << path.toStdString();
    }

    StageObservation observation;
    WorkflowExecutor executor;
    executor.addStage(std::make_unique<ProbeStage>(observation));
    const WorkflowExecutor::Result result = executor.execute(minimalManifest(m_output.path()));
    ASSERT_TRUE(result.success) << result.errorMessage.toStdString();
    for (const QString& path : pending)
        EXPECT_FALSE(QFileInfo::exists(path)) << path.toStdString();
    for (const QString& path : committed)
        EXPECT_TRUE(QFileInfo::exists(path)) << path.toStdString();
}

/// A stage that reports what its stack counted.
class CountingStage final : public IWorkflowStage
{
public:
    [[nodiscard]] QString name() const override
    {
        return QStringLiteral("Counting");
    }

    [[nodiscard]] WorkflowStage stageId() const override
    {
        return WorkflowStage::LightsStacking;
    }

    StageResult execute(const WorkflowManifest&, CancellationToken&, ProgressReporter&) override
    {
        StageResult result = StageResult::success();
        StackExecutionCounters counters;
        counters.inputDecodeOperations = 5;
        counters.roiSubsetReads = 7;
        result.engineExecutionCounters = counters;
        return result;
    }
};

TEST_F(WorkflowRuntimeTest, AStacksCountersReachTheRunsDiagnostics)
{
    WorkflowExecutor executor;
    executor.addStage(std::make_unique<CountingStage>());
    WorkflowExecutionOptions options;
    options.collectCounters = true;
    const WorkflowExecutor::Result result = executor.execute(minimalManifest(m_output.path()), {}, options);
    ASSERT_TRUE(result.success) << result.errorMessage.toStdString();
    ASSERT_TRUE(result.executionCounters.has_value());
    EXPECT_EQ(result.executionCounters->inputDecodeOperations(), 5u);
    EXPECT_EQ(result.executionCounters->roiSubsetReads(), 7u);
}

// ---- The run's frame cache under the session's derived-cache allowance.

WorkflowCachedImage greyImage(const an::consumer::EngineSession& session, const std::uint32_t side)
{
    auto hold = session.hold();
    EXPECT_TRUE(hold.hasValue());
    const auto extent = an::ImageExtent::create(side, side, 1u, hold->engine().limits());
    const auto descriptor =
        an::ImageDescriptor::create(an::ImageDescriptorRequest{.extent = *extent,
                                                               .sampleType = an::SampleType::Float32,
                                                               .channelSchema = an::ChannelSchema::Mono,
                                                               .colorSpace = an::ColorSpace::Unspecified,
                                                               .numericDomain = an::NumericDomain::NormalizedLinear,
                                                               .cfa = an::CfaDescriptor{},
                                                               .coordinateOrigin = an::CoordinateOrigin::TopLeft,
                                                               .pixelAspectRatio = 1.0});
    EXPECT_TRUE(descriptor.has_value());
    auto image = an::Image<float>::create(hold->engine(), *descriptor, an::StorageArrangement::Planar,
                                          an::StorageInitialization::Zeroed);
    EXPECT_TRUE(image.hasValue()) << image.status().message();
    return std::make_shared<const an::consumer::AnyImage>(std::move(image).value());
}

WorkflowImageCacheKey keyFor(const QString& name)
{
    ExecutionPreparationSnapshot::FileStamp stamp;
    stamp.canonicalPath = name;
    stamp.exists = true;
    stamp.isFile = true;
    stamp.sizeBytes = 1;
    return WorkflowImageCacheKey::decoded(FrameId(name), stamp, QStringLiteral("float"));
}

TEST_F(WorkflowRuntimeTest, AFrameTheAllowanceRefusesIsHandedBackUncached)
{
    an::consumer::MemoryAllowances allowances = m_session->memory().allowances();
    allowances.derivedCaches.bytes = 1024u;
    m_session->memory().setAllowances(allowances);

    WorkflowImageCache cache(64ull * 1024ull * 1024ull, {}, &m_session->memory());
    int loads = 0;
    const auto loader = [&]() {
        ++loads;
        return greyImage(*m_session, 64u);
    };
    const WorkflowCachedImage first = cache.load(keyFor(QStringLiteral("D:/a.fits")), loader);
    const WorkflowCachedImage second = cache.load(keyFor(QStringLiteral("D:/a.fits")), loader);
    ASSERT_NE(first, nullptr);
    ASSERT_NE(second, nullptr);
    EXPECT_EQ(loads, 2);
    EXPECT_EQ(cache.diagnostics().allowanceRefusals, 2u);
    EXPECT_EQ(cache.diagnostics().publications, 0u);
}

TEST_F(WorkflowRuntimeTest, TheSessionMayAskTheCacheToMakeRoom)
{
    const std::uint64_t imageBytes = 64u * 64u * sizeof(float);
    an::consumer::MemoryAllowances allowances = m_session->memory().allowances();
    allowances.derivedCaches.bytes = 3u * imageBytes;
    m_session->memory().setAllowances(allowances);

    WorkflowImageCache cache(64ull * 1024ull * 1024ull, {}, &m_session->memory());
    const auto loader = [&]() { return greyImage(*m_session, 64u); };
    (void)cache.load(keyFor(QStringLiteral("D:/a.fits")), loader);
    (void)cache.load(keyFor(QStringLiteral("D:/b.fits")), loader);
    ASSERT_EQ(cache.diagnostics().publications, 2u);

    // Another owner of the allowance asks for more than is left: the cache evicts what nobody borrows.
    auto reservation = m_session->memory().reserve(an::consumer::MemoryCategory::DerivedCaches, 2u * imageBytes);
    ASSERT_TRUE(reservation.hasValue()) << reservation.status().message();
    EXPECT_GE(cache.diagnostics().allowanceEvictions, 1u);
    EXPECT_LT(cache.diagnostics().lookupEntries, 2u);
}

TEST_F(WorkflowRuntimeTest, ARunCountsTheEngineMemoryItsStagesUsedAndLogsIt)
{
    constexpr std::uint32_t kSide = 512u;
    constexpr std::uint64_t kImageBytes = std::uint64_t{kSide} * kSide * sizeof(float);
    StageObservation observation;
    QStringList log;
    WorkflowExecutor executor;
    executor.setCodecRegistryFactory([](const ImageIntegration::WorkflowRawPolicy&) { return nullptr; });
    executor.setLogCallback([&](WorkflowLogLevel, const QString& message) { log.append(message); });
    executor.addStage(std::make_unique<ProbeStage>(observation, [&](const an::consumer::OperationScope&) {
        // The image is made and let go inside the run, so only the run's own counters can show it.
        const WorkflowCachedImage image = greyImage(*m_session, kSide);
        EXPECT_NE(image, nullptr);
    }));
    WorkflowExecutionOptions options;
    options.session = &*m_session;
    const WorkflowExecutor::Result result = executor.execute(minimalManifest(m_output.path()), {}, options);

    ASSERT_TRUE(result.success) << result.errorMessage.toStdString();
    ASSERT_TRUE(result.engineMemory.has_value());
    EXPECT_GE(result.engineMemory->allocations, 1u);
    EXPECT_GE(result.engineMemory->bytesAllocated, kImageBytes);
    EXPECT_GE(result.engineMemory->peakBytes, kImageBytes);
    EXPECT_GE(result.engineMemory->peakBytes, result.engineMemory->peakBytesBeforeRun);
    EXPECT_EQ(result.engineMemory->deniedAllocations, 0u);
    const QString joined = log.join(QLatin1Char('\n'));
    EXPECT_TRUE(joined.contains(QStringLiteral("Engine memory"))) << joined.toStdString();
    EXPECT_TRUE(joined.contains(QString::number(result.engineMemory->peakBytes))) << joined.toStdString();
}

} // namespace
