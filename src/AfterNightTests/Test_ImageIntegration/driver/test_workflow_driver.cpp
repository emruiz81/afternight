// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The consumer measurement driver's own logic: its command line, the frame set it resolves and the identity it gives
/// it, the import through the session, the variant an incremental case alternates, how a result maps to an exit code,
/// the resident-memory sampler, and one measured run of the real workflow over written lights in a session of its
/// own. What it does with the workflow is the product's preparation and controller, which their own suites hold.

#include <gtest/gtest.h>

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTimeZone>

#include <chrono>
#include <cstddef>
#include <cstring>
#include <thread>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <sys/mman.h>
#endif

#include "ResidentMemory.h"
#include "WorkflowDriver.h"

#include "Test_ImageIntegration/ImportSession.h"
#include "Test_ImageIntegration/workflow/StarFieldFrames.h"

#include "processes/ImageIntegration/lite/LiteIntegrationProfile.h"
#include "processes/ImageIntegration/model/IntegrationDataModel.h"
#include "processes/ImageIntegration/workflow/WorkflowPreparation.h"

#include <AfterNightConsumerRuntime/EngineSession.h>

namespace
{

/// Memory taken from the system and given back to it when the block goes, so its release lowers the resident set
/// whichever allocator the build has: the address sanitizer's keeps a freed heap block resident in its quarantine.
class SystemBlock
{
public:
    explicit SystemBlock(std::size_t bytes) : m_bytes(bytes)
    {
#ifdef _WIN32
        m_memory = VirtualAlloc(nullptr, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
#else
        void* memory = mmap(nullptr, bytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        m_memory = memory == MAP_FAILED ? nullptr : memory;
#endif
    }
    ~SystemBlock()
    {
        if (m_memory == nullptr)
        {
            return;
        }
#ifdef _WIN32
        VirtualFree(m_memory, 0, MEM_RELEASE);
#else
        munmap(m_memory, m_bytes);
#endif
    }
    SystemBlock(const SystemBlock&) = delete;
    SystemBlock& operator=(const SystemBlock&) = delete;

    [[nodiscard]] void* data() const noexcept
    {
        return m_memory;
    }

private:
    std::size_t m_bytes = 0u;
    void* m_memory = nullptr;
};

using an::measurement::DriverOptionsParse;
using an::measurement::DriverWorkflow;

QStringList required()
{
    return {QStringLiteral("--workflow"),    QStringLiteral("full"),        QStringLiteral("--corpus-manifest"),
            QStringLiteral("corpus.json"),   QStringLiteral("--frame-set"), QStringLiteral("set"),
            QStringLiteral("--corpus-root"), QStringLiteral("root"),        QStringLiteral("--output"),
            QStringLiteral("out"),           QStringLiteral("--result"),    QStringLiteral("result.json")};
}

void writeFile(const QString& path, const QByteArray& bytes)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    file.write(bytes);
}

QByteArray sha256Hex(const QByteArray& bytes)
{
    return QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex();
}

QByteArray fileSha256Hex(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
    {
        return {};
    }
    return sha256Hex(file.readAll());
}

/// A corpus of star-field lights written by the neutral FITS writer, the shared field moved by one pixel per light,
/// and a manifest naming them as one frame set of lights.
struct StarFieldCorpus
{
    explicit StarFieldCorpus(int lights)
    {
        EXPECT_TRUE(directory.isValid());
        const std::vector<an::tests::fixtures::FixtureStar> field = an::tests::integration::starCatalogue(40u, 23u);
        QJsonArray frames;
        for (int index = 0; index < lights; ++index)
        {
            const QString relative = QStringLiteral("lights/light_%1.fits").arg(index);
            const QString path = QDir(root()).filePath(relative);
            QDir().mkpath(QFileInfo(path).absolutePath());
            const auto stars = an::tests::integration::movedStars(
                field, [index](double x, double y) { return std::pair{x + index, y + 0.5 * index}; });
            std::string error;
            EXPECT_TRUE(an::tests::integration::writeStarField(path, stars, {},
                                                               100u + static_cast<std::uint64_t>(index), error))
                << error;
            frames.append(QJsonObject{{QStringLiteral("relativePath"), relative},
                                      {QStringLiteral("sizeBytes"), static_cast<double>(QFileInfo(path).size())},
                                      {QStringLiteral("sha256"), QString::fromLatin1(fileSha256Hex(path))}});
        }
        const QJsonObject set{
            {QStringLiteral("id"), QStringLiteral("stars")},
            {QStringLiteral("roles"), QJsonArray{QJsonObject{{QStringLiteral("role"), QStringLiteral("lights")},
                                                             {QStringLiteral("frames"), frames}}}}};
        writeFile(manifest(), QJsonDocument(QJsonObject{{QStringLiteral("frameSets"), QJsonArray{set}}}).toJson());
    }

    QString manifest() const
    {
        return directory.filePath(QStringLiteral("corpus.json"));
    }

    QString root() const
    {
        return directory.filePath(QStringLiteral("root"));
    }

    QTemporaryDir directory;
};

// A corpus manifest naming one frame set of two roles, the files it names written under a root.
struct PlantedCorpus
{
    PlantedCorpus()
    {
        EXPECT_TRUE(directory.isValid());
        light = QByteArray(40, 'l');
        dark = QByteArray(24, 'd');
        writeFile(directory.filePath(QStringLiteral("root/lights/l0.fits")), light);
        writeFile(directory.filePath(QStringLiteral("root/darks/d0.fits")), dark);
        writeManifest(QStringLiteral("lights"));
    }

    void writeManifest(const QString& lightRole, qint64 lightSize = -1, QByteArray lightDigest = {})
    {
        const QJsonObject lights{
            {QStringLiteral("role"), lightRole},
            {QStringLiteral("frames"),
             QJsonArray{QJsonObject{
                 {QStringLiteral("relativePath"), QStringLiteral("lights/l0.fits")},
                 {QStringLiteral("sizeBytes"), static_cast<double>(lightSize < 0 ? light.size() : lightSize)},
                 {QStringLiteral("sha256"),
                  QString::fromLatin1(lightDigest.isEmpty() ? sha256Hex(light) : lightDigest)}}}}};
        const QJsonObject darks{
            {QStringLiteral("role"), QStringLiteral("darks")},
            {QStringLiteral("frames"),
             QJsonArray{QJsonObject{{QStringLiteral("relativePath"), QStringLiteral("darks/d0.fits")},
                                    {QStringLiteral("sizeBytes"), static_cast<double>(dark.size())},
                                    {QStringLiteral("sha256"), QString::fromLatin1(sha256Hex(dark))}}}}};
        const QJsonObject set{{QStringLiteral("id"), QStringLiteral("planted")},
                              {QStringLiteral("roles"), QJsonArray{lights, darks}}};
        writeFile(manifest(), QJsonDocument(QJsonObject{{QStringLiteral("frameSets"), QJsonArray{set}}}).toJson());
    }

    QString manifest() const
    {
        return directory.filePath(QStringLiteral("corpus.json"));
    }

    QString root() const
    {
        return directory.filePath(QStringLiteral("root"));
    }

    QTemporaryDir directory;
    QByteArray light;
    QByteArray dark;
};

} // namespace

TEST(WorkflowDriver, TheCommandLineReadsEveryOption)
{
    QStringList arguments = required();
    arguments << QStringLiteral("--variant") << QStringLiteral("1") << QStringLiteral("--max-parallel-cores")
              << QStringLiteral("8") << QStringLiteral("--cancel-after-ms") << QStringLiteral("2500")
              << QStringLiteral("--verify-digests") << QStringLiteral("--sampling-interval-ms") << QStringLiteral("20");
    arguments[1] = QStringLiteral("lite");
    const DriverOptionsParse parsed = an::measurement::parseDriverOptions(arguments);
    ASSERT_TRUE(parsed.options.has_value()) << parsed.error.toStdString();
    EXPECT_EQ(parsed.options->workflow, DriverWorkflow::Lite);
    EXPECT_EQ(parsed.options->corpusManifest, QStringLiteral("corpus.json"));
    EXPECT_EQ(parsed.options->frameSet, QStringLiteral("set"));
    EXPECT_EQ(parsed.options->variant, 1);
    EXPECT_EQ(parsed.options->maxParallelCores, 8);
    ASSERT_TRUE(parsed.options->cancelAfterMs.has_value());
    EXPECT_EQ(*parsed.options->cancelAfterMs, 2500);
    EXPECT_TRUE(parsed.options->verifyDigests);
    EXPECT_EQ(parsed.options->samplingIntervalMs, 20);
}

TEST(WorkflowDriver, TheCommandLineRefusesWhatItCannotRun)
{
    for (const QString& dropped :
         {QStringLiteral("--corpus-manifest"), QStringLiteral("--frame-set"), QStringLiteral("--corpus-root"),
          QStringLiteral("--output"), QStringLiteral("--result")})
    {
        QStringList arguments = required();
        const int at = static_cast<int>(arguments.indexOf(dropped));
        arguments.remove(at, 2);
        EXPECT_FALSE(an::measurement::parseDriverOptions(arguments).options.has_value()) << dropped.toStdString();
    }
    QStringList workflow = required();
    workflow[1] = QStringLiteral("everything");
    EXPECT_FALSE(an::measurement::parseDriverOptions(workflow).options.has_value());
    QStringList variant = required();
    variant << QStringLiteral("--variant") << QStringLiteral("2");
    EXPECT_FALSE(an::measurement::parseDriverOptions(variant).options.has_value());
    QStringList unknown = required();
    unknown << QStringLiteral("--turbo") << QStringLiteral("yes");
    EXPECT_FALSE(an::measurement::parseDriverOptions(unknown).options.has_value());
}

TEST(WorkflowDriver, AFrameSetHasTheBenchmarkLayersIdentity)
{
    // Computed outside the engine: SHA-256 of "lights\naa\n\x1edarks\nbb\ncc\n\x1e".
    const QJsonObject set{
        {QStringLiteral("roles"),
         QJsonArray{QJsonObject{{QStringLiteral("role"), QStringLiteral("lights")},
                                {QStringLiteral("frames"),
                                 QJsonArray{QJsonObject{{QStringLiteral("sha256"), QStringLiteral("aa")}}}}},
                    QJsonObject{{QStringLiteral("role"), QStringLiteral("darks")},
                                {QStringLiteral("frames"),
                                 QJsonArray{QJsonObject{{QStringLiteral("sha256"), QStringLiteral("bb")}},
                                            QJsonObject{{QStringLiteral("sha256"), QStringLiteral("cc")}}}}}}}};
    EXPECT_EQ(an::measurement::frameSetIdentityDigest(set),
              QStringLiteral("1aae0a248a619d8d365b59453b49620aca85ba4690d51d97f0121c6dbc60e4cb"));
}

TEST(WorkflowDriver, AFrameSetResolvesEveryMemberWithItsType)
{
    PlantedCorpus corpus;
    const an::measurement::FrameSetRead read =
        an::measurement::readFrameSet(corpus.manifest(), QStringLiteral("planted"), corpus.root(), true);
    ASSERT_TRUE(read.frameSet.has_value()) << read.error.toStdString();
    ASSERT_EQ(read.frameSet->members.size(), 2u);
    EXPECT_EQ(read.frameSet->members[0].type, FrameType::Light);
    EXPECT_EQ(read.frameSet->members[1].type, FrameType::Dark);
    EXPECT_EQ(read.frameSet->totalBytes, corpus.light.size() + corpus.dark.size());
    EXPECT_EQ(read.frameSet->identityDigest.size(), 64);
    EXPECT_EQ(read.frameSet->manifestSha256.size(), 64);
}

TEST(WorkflowDriver, AFrameSetIsRefusedWholeForOneBadMember)
{
    PlantedCorpus corpus;
    EXPECT_FALSE(an::measurement::readFrameSet(corpus.manifest(), QStringLiteral("absent"), corpus.root(), false)
                     .frameSet.has_value());

    corpus.writeManifest(QStringLiteral("lights"), corpus.light.size() + 1);
    const an::measurement::FrameSetRead wrongSize =
        an::measurement::readFrameSet(corpus.manifest(), QStringLiteral("planted"), corpus.root(), false);
    EXPECT_FALSE(wrongSize.frameSet.has_value());
    EXPECT_TRUE(wrongSize.error.contains(QStringLiteral("l0.fits")));

    corpus.writeManifest(QStringLiteral("lights"), -1, QByteArray(64, '0'));
    EXPECT_TRUE(an::measurement::readFrameSet(corpus.manifest(), QStringLiteral("planted"), corpus.root(), false)
                    .frameSet.has_value());
    EXPECT_FALSE(an::measurement::readFrameSet(corpus.manifest(), QStringLiteral("planted"), corpus.root(), true)
                     .frameSet.has_value());

    corpus.writeManifest(QStringLiteral("flatDarks"));
    EXPECT_FALSE(an::measurement::readFrameSet(corpus.manifest(), QStringLiteral("planted"), corpus.root(), false)
                     .frameSet.has_value());
}

TEST(WorkflowDriver, AFrameSetIsImportedThroughTheSessionTypedAsItsRole)
{
    StarFieldCorpus corpus(3);
    const an::measurement::FrameSetRead read =
        an::measurement::readFrameSet(corpus.manifest(), QStringLiteral("stars"), corpus.root(), true);
    ASSERT_TRUE(read.frameSet.has_value()) << read.error.toStdString();
    auto session = an::tests::integration::openImportSession();
    ASSERT_TRUE(session.hasValue()) << session.status().message();

    IntegrationDataModel model;
    const an::measurement::FrameImport imported = an::measurement::importFrameSet(model, *read.frameSet, *session);
    EXPECT_EQ(imported.imported, 3);
    EXPECT_TRUE(imported.issues.isEmpty()) << imported.issues.join(QStringLiteral("; ")).toStdString();
    // Each file was probed in the session: its size is the header's, not the zero an unprobed frame is given.
    ASSERT_EQ(model.frames(FrameType::Light).size(), 3u);
    for (const FrameInfo& frame : model.frames(FrameType::Light).frames())
    {
        EXPECT_EQ(frame.metadata.dimensions(),
                  QSize(an::tests::integration::kStarFieldWidth, an::tests::integration::kStarFieldHeight));
    }
}

TEST(WorkflowDriver, AMeasuredRunCompletesInItsSessionAndAccountsForTheEngine)
{
    StarFieldCorpus corpus(4);
    QTemporaryDir run;
    ASSERT_TRUE(run.isValid());
    auto session = an::tests::integration::openImportSession(4u);
    ASSERT_TRUE(session.hasValue()) << session.status().message();

    an::measurement::DriverOptions options;
    options.corpusManifest = corpus.manifest();
    options.frameSet = QStringLiteral("stars");
    options.corpusRoot = corpus.root();
    options.outputDirectory = run.filePath(QStringLiteral("out"));
    options.resultPath = run.filePath(QStringLiteral("result.json"));
    options.engine = QJsonObject{{QStringLiteral("generation"), QStringLiteral("next")}};
    const QJsonObject result = an::measurement::runWorkflowMeasurement(options, *session);

    const QJsonObject outcome = result.value(QStringLiteral("outcome")).toObject();
    ASSERT_TRUE(outcome.value(QStringLiteral("success")).toBool()) << QJsonDocument(result).toJson().toStdString();
    EXPECT_EQ(an::measurement::exitCodeFor(result), 0);
    EXPECT_TRUE(QFileInfo::exists(options.resultPath));
    EXPECT_EQ(result.value(QStringLiteral("dataset")).toObject().value(QStringLiteral("frames")).toInt(), 4);

    // The light stack ran and its master is listed with the digest of the file it published.
    QStringList stages;
    for (const QJsonValue& stage :
         result.value(QStringLiteral("timing")).toObject().value(QStringLiteral("stages")).toArray())
    {
        stages << stage.toObject().value(QStringLiteral("name")).toString();
    }
    EXPECT_TRUE(stages.contains(QStringLiteral("LightsStacking"))) << stages.join(QStringLiteral(", ")).toStdString();
    int lightMasters = 0;
    for (const QJsonValue& value : result.value(QStringLiteral("outputs")).toArray())
    {
        const QJsonObject output = value.toObject();
        if (output.value(QStringLiteral("role")).toString() != QStringLiteral("light-master"))
        {
            continue;
        }
        ++lightMasters;
        const QString path = QDir(options.outputDirectory).filePath(output.value(QStringLiteral("path")).toString());
        EXPECT_EQ(output.value(QStringLiteral("sha256")).toString().toLatin1(), fileSha256Hex(path));
    }
    EXPECT_EQ(lightMasters, 1);

    // The engine's own accounting over the region: what its memory resource held at most, and nothing refused.
    const QJsonObject engine = result.value(QStringLiteral("engine")).toObject();
    EXPECT_EQ(engine.value(QStringLiteral("generation")).toString(), QStringLiteral("next"));
    const QJsonObject region = engine.value(QStringLiteral("region")).toObject();
    EXPECT_GT(region.value(QStringLiteral("peakBytes")).toDouble(), 0.0);
    EXPECT_GT(region.value(QStringLiteral("allocationsInRegion")).toDouble(), 0.0);
    EXPECT_EQ(region.value(QStringLiteral("deniedAllocationsInRegion")).toDouble(), 0.0);
    EXPECT_TRUE(
        region.value(QStringLiteral("consumerAllowances")).toObject().contains(QStringLiteral("derived caches")))
        << QJsonDocument(region).toJson().toStdString();
}

TEST(WorkflowDriver, TheIncrementalVariantChangesOnlyTheLightStacksHighSigma)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    IntegrationDataModel model;
    std::vector<FrameInfo> lights;
    for (int index = 0; index < 3; ++index)
    {
        const QString path = directory.filePath(QStringLiteral("light_%1.fits").arg(index));
        writeFile(path, QByteArray("x"));
        const FrameMetadata metadata(FrameType::Light, QSize(100, 100), 60.0, -10.0, 100, 10, QStringLiteral("L"),
                                     QStringLiteral("M42"), QDateTime::fromSecsSinceEpoch(1700000000, QTimeZone::UTC),
                                     QStringLiteral("Default"));
        lights.emplace_back(FrameId(path), path, metadata, QStringLiteral("2025-01-01"), QString{});
    }
    model.addFrames(FrameType::Light, lights);
    model.setOutputDirectory(directory.filePath(QStringLiteral("output")));
    ASSERT_FALSE(model.integrationGroups().empty());

    const WorkflowManifest before = ImageIntegration::buildFullWorkflowManifest(model, {});
    an::measurement::applyFullVariant(model, 0);
    EXPECT_TRUE(model.stackingSettings(FrameType::Light).empty());

    an::measurement::applyFullVariant(model, 1);
    const WorkflowManifest after = ImageIntegration::buildFullWorkflowManifest(model, {});
    ASSERT_EQ(after.integrationGroups.size(), before.integrationGroups.size());
    for (std::size_t index = 0; index < after.integrationGroups.size(); ++index)
    {
        StackingSettings expected = before.integrationGroups[index].stacking;
        EXPECT_DOUBLE_EQ(expected.sigmaHigh, StackingSettings{}.sigmaHigh);
        EXPECT_DOUBLE_EQ(after.integrationGroups[index].stacking.sigmaHigh, StackingSettings{}.sigmaHigh + 0.1);
        expected.sigmaHigh = after.integrationGroups[index].stacking.sigmaHigh;
        EXPECT_EQ(expected.sigmaLow, after.integrationGroups[index].stacking.sigmaLow);
        EXPECT_EQ(expected.rejectionMethod, after.integrationGroups[index].stacking.rejectionMethod);
    }
}

TEST(WorkflowDriver, TheLiteIncrementalVariantChangesOnlyTheLightStacksWeighting)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    IntegrationDataModel model;
    model.beginBatchUpdate();
    for (const FrameType type : {FrameType::Light, FrameType::Flat, FrameType::Dark, FrameType::Bias})
    {
        std::vector<FrameInfo> frames;
        for (int index = 0; index < 3; ++index)
        {
            const QString path =
                directory.filePath(QStringLiteral("%1_%2.fits").arg(static_cast<int>(type)).arg(index));
            writeFile(path, QByteArray("x"));
            const FrameMetadata metadata(
                type, QSize(100, 100), type == FrameType::Bias ? 0.0 : 60.0, -10.0, 100, 10,
                type == FrameType::Light || type == FrameType::Flat ? QStringLiteral("L") : QString(),
                QStringLiteral("M42"), QDateTime::fromSecsSinceEpoch(1700000000, QTimeZone::UTC),
                QStringLiteral("Default"));
            frames.emplace_back(FrameId(path), path, metadata, QStringLiteral("2025-01-01"), QString{});
        }
        model.addFrames(type, frames);
    }
    model.setOutputDirectory(directory.filePath(QStringLiteral("output")));
    model.endBatchUpdate();

    ImageIntegrationLite::ProcessingOptions unchanged = ImageIntegrationLite::defaultProcessingOptions();
    an::measurement::applyLiteVariant(unchanged, 0);
    ImageIntegrationLite::ProcessingOptions changed = ImageIntegrationLite::defaultProcessingOptions();
    an::measurement::applyLiteVariant(changed, 1);

    const WorkflowManifest before = ImageIntegrationLite::prepareManifest(model.toJson(), unchanged, false);
    WorkflowManifest after = ImageIntegrationLite::prepareManifest(model.toJson(), changed, false);
    const auto canonical = [](const WorkflowManifest& manifest) {
        return QJsonDocument(manifest.toJson()).toJson(QJsonDocument::Compact);
    };
    EXPECT_NE(canonical(after), canonical(before));

    ASSERT_FALSE(after.integrationGroups.empty());
    ASSERT_EQ(after.integrationGroups.size(), before.integrationGroups.size());
    for (std::size_t index = 0; index < after.integrationGroups.size(); ++index)
    {
        EXPECT_NE(after.integrationGroups[index].stacking.weighting,
                  before.integrationGroups[index].stacking.weighting);
        after.integrationGroups[index].stacking.weighting = before.integrationGroups[index].stacking.weighting;
    }
    EXPECT_EQ(canonical(after), canonical(before));
}

TEST(WorkflowDriver, TheExitCodeSaysWhatBecameOfTheRun)
{
    const auto result = [](bool success, bool cancelled, QJsonValue cancelAfter) {
        return QJsonObject{{QStringLiteral("request"), QJsonObject{{QStringLiteral("cancelAfterMs"), cancelAfter}}},
                           {QStringLiteral("outcome"), QJsonObject{{QStringLiteral("success"), success},
                                                                   {QStringLiteral("cancelled"), cancelled}}}};
    };
    EXPECT_EQ(an::measurement::exitCodeFor(result(true, false, QJsonValue::Null)), 0);
    EXPECT_EQ(an::measurement::exitCodeFor(result(false, true, 2000)), 3);
    EXPECT_EQ(an::measurement::exitCodeFor(result(false, true, QJsonValue::Null)), 2);
    EXPECT_EQ(an::measurement::exitCodeFor(result(false, false, 2000)), 2);
    EXPECT_EQ(an::measurement::exitCodeFor(QJsonObject{}), 2);
}

TEST(WorkflowDriver, TheSamplerPlacesAResidentPeakInItsPhase)
{
    using an::measurement::ResidentMemorySampler;
    const an::measurement::ResidentMemoryReading idle = an::measurement::readResidentMemory();
    ASSERT_TRUE(idle.valid);

    ResidentMemorySampler sampler(std::chrono::milliseconds(2));
    sampler.setPhase("quiet");
    sampler.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    constexpr std::size_t kBytes = 256u * 1024u * 1024u;
    {
        sampler.setPhase("allocating");
        const SystemBlock block(kBytes);
        ASSERT_NE(block.data(), nullptr);
        // Through a pointer the optimizer cannot see into: a block written and never read is otherwise removed with
        // its allocation, as Clang does in Release, and the resident set never holds it.
        void* (*volatile touch)(void*, int, std::size_t) = std::memset;
        touch(block.data(), 1, kBytes);
        std::this_thread::sleep_for(std::chrono::milliseconds(60));
        sampler.setPhase("released");
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    const ResidentMemorySampler::Summary summary = sampler.stop();

    EXPECT_GT(summary.samples, 10u);
    EXPECT_EQ(summary.peakPhase, "allocating");
    EXPECT_GE(summary.peakBytes, idle.residentBytes + kBytes / 2u);
    ASSERT_EQ(summary.phasePeaks.count("quiet"), 1u);
    EXPECT_LT(summary.phasePeaks.at("quiet") + kBytes / 2u, summary.peakBytes);
    // The system's mark holds the block. Linux updates VmHWM only when memory is unmapped, from the same approximate
    // counters as VmRSS, so it can trail a reading the sampler took earlier by a few pages.
    EXPECT_GE(an::measurement::readResidentMemory().highWaterBytes, idle.residentBytes + kBytes / 2u);
    EXPECT_FALSE(an::measurement::residentMemoryMetric().empty());
}
