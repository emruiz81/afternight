// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// oneTBB comes before anything of Qt: Qt defines `emit` as a macro and oneTBB's profiling header declares a function
// of that name, which is why the Processes precompiled header orders them the same way.
#include <tbb/concurrent_vector.h>
#include <tbb/parallel_for_each.h>

#include "WorkflowDriver.h"

#include "ResidentMemory.h"

#include "processes/ImageIntegration/ImageIntegrationImportSession.h"
#include "processes/ImageIntegration/core/FileImport.h"
#include "processes/ImageIntegration/lite/LiteIntegrationProfile.h"
#include "processes/ImageIntegration/model/IntegrationDataModel.h"
#include "processes/ImageIntegration/workflow/FrameHeaderReader.h"
#include "processes/ImageIntegration/workflow/ProgressReporter.h"
#include "processes/ImageIntegration/workflow/WorkflowPreparation.h"
#include "processes/ImageIntegration/workflow/WorkflowProgressController.h"

#include <AfterNightConsumerRuntime/ConsumerMemory.h>
#include <AfterNightConsumerRuntime/EngineSession.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/MemoryResource.h>
#include <AfterNightPlatform/Logging.h>

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTimer>

#include <array>
#include <chrono>
#include <map>
#include <thread>
#include <utility>

namespace an::measurement
{
namespace
{

using Clock = std::chrono::steady_clock;

[[nodiscard]] double millisecondsBetween(const Clock::time_point from, const Clock::time_point to)
{
    return std::chrono::duration<double, std::milli>(to - from).count();
}

[[nodiscard]] std::optional<FrameType> frameTypeOfRole(const QString& role)
{
    if (role == QStringLiteral("lights"))
    {
        return FrameType::Light;
    }
    if (role == QStringLiteral("flats"))
    {
        return FrameType::Flat;
    }
    if (role == QStringLiteral("darks"))
    {
        return FrameType::Dark;
    }
    if (role == QStringLiteral("bias"))
    {
        return FrameType::Bias;
    }
    return std::nullopt;
}

[[nodiscard]] QString fileSha256(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
    {
        return QString();
    }
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&file))
    {
        return QString();
    }
    return QString::fromLatin1(hash.result().toHex());
}

[[nodiscard]] QString stageName(const WorkflowStage stage)
{
    switch (stage)
    {
    case WorkflowStage::None:
        return QStringLiteral("execution");
    case WorkflowStage::Bias:
        return QStringLiteral("Bias");
    case WorkflowStage::Dark:
        return QStringLiteral("Dark");
    case WorkflowStage::Flat:
        return QStringLiteral("Flat");
    case WorkflowStage::LightsCalibration:
        return QStringLiteral("LightsCalibration");
    case WorkflowStage::LightsRegistration:
        return QStringLiteral("LightsRegistration");
    case WorkflowStage::LightsAlignment:
        return QStringLiteral("LightsAlignment");
    case WorkflowStage::LightsNormalization:
        return QStringLiteral("LightsNormalization");
    case WorkflowStage::LightsDrizzle:
        return QStringLiteral("LightsDrizzle");
    case WorkflowStage::LightsStacking:
        return QStringLiteral("LightsStacking");
    }
    return QStringLiteral("execution");
}

[[nodiscard]] QString dispositionName(const ImageIntegration::PreparedWorkflowDisposition disposition)
{
    switch (disposition)
    {
    case ImageIntegration::PreparedWorkflowDisposition::Failed:
        return QStringLiteral("failed");
    case ImageIntegration::PreparedWorkflowDisposition::NothingToProcess:
        return QStringLiteral("nothing-to-process");
    case ImageIntegration::PreparedWorkflowDisposition::Invalid:
        return QStringLiteral("invalid");
    case ImageIntegration::PreparedWorkflowDisposition::NoGroups:
        return QStringLiteral("no-groups");
    case ImageIntegration::PreparedWorkflowDisposition::Ready:
        return QStringLiteral("ready");
    }
    return QStringLiteral("failed");
}

[[nodiscard]] QString executionModeName(const ExecutionMode mode)
{
    return mode == ExecutionMode::Incremental ? QStringLiteral("incremental") : QStringLiteral("full");
}

[[nodiscard]] QJsonValue bytesValue(const std::uint64_t bytes)
{
    return QJsonValue(static_cast<double>(bytes));
}

[[nodiscard]] ImageIntegrationLite::ProcessingOptions liteOptions(const DriverOptions& options)
{
    ImageIntegrationLite::ProcessingOptions processing = ImageIntegrationLite::defaultProcessingOptions();
    processing.maxParallelCores = options.maxParallelCores;
    applyLiteVariant(processing, options.variant);
    return processing;
}

[[nodiscard]] QJsonObject countersObject(const WorkflowExecutor::Result& result)
{
    QJsonObject counters;
    if (result.executionCounters.has_value())
    {
        const WorkflowExecutionCounters& c = *result.executionCounters;
        counters.insert(QStringLiteral("bytesHashed"), bytesValue(c.bytesHashed()));
        counters.insert(QStringLiteral("bytesCopied"), bytesValue(c.bytesCopied()));
        counters.insert(QStringLiteral("inputDecodeOperations"), bytesValue(c.inputDecodeOperations()));
        counters.insert(QStringLiteral("scientificOutputEncodeCompletions"),
                        bytesValue(c.scientificOutputEncodeCompletions()));
        counters.insert(QStringLiteral("logicalDecodedPixelBytes"), bytesValue(c.logicalDecodedPixelBytes()));
        counters.insert(QStringLiteral("calibrationInvocations"), bytesValue(c.calibrationInvocations()));
        counters.insert(QStringLiteral("calibratedSpillFilesCreated"), bytesValue(c.calibratedSpillFilesCreated()));
        counters.insert(QStringLiteral("calibratedSpillBytesWritten"), bytesValue(c.calibratedSpillBytesWritten()));
        counters.insert(QStringLiteral("calibratedSpillBytesRead"), bytesValue(c.calibratedSpillBytesRead()));
        counters.insert(QStringLiteral("stackLoaderRequests"), bytesValue(c.stackLoaderRequests()));
        counters.insert(QStringLiteral("roiSubsetReads"), bytesValue(c.roiSubsetReads()));
        counters.insert(QStringLiteral("fullFrameFallbackReads"), bytesValue(c.fullFrameFallbackReads()));
        counters.insert(QStringLiteral("peakLazyCoordinateBytes"), bytesValue(c.peakLazyCoordinateBytes()));
        counters.insert(QStringLiteral("sqliteStatementExecutions"), bytesValue(c.sqliteStatementExecutions()));
    }
    QJsonObject cache;
    if (result.workflowCacheDiagnostics.has_value())
    {
        const WorkflowImageCacheDiagnostics& d = *result.workflowCacheDiagnostics;
        cache.insert(QStringLiteral("physicalDecodedLoads"), bytesValue(d.physicalDecodedLoads));
        cache.insert(QStringLiteral("physicalCalibrations"), bytesValue(d.physicalCalibrations));
        cache.insert(QStringLiteral("hits"), bytesValue(d.hits));
        cache.insert(QStringLiteral("misses"), bytesValue(d.misses));
        cache.insert(QStringLiteral("evictions"), bytesValue(d.evictions));
        cache.insert(QStringLiteral("capacityBypasses"), bytesValue(d.capacityBypasses));
        cache.insert(QStringLiteral("publications"), bytesValue(d.publications));
        cache.insert(QStringLiteral("currentBytes"), bytesValue(d.currentBytes));
        cache.insert(QStringLiteral("peakBytes"), bytesValue(d.peakBytes));
    }
    return QJsonObject{{QStringLiteral("executionCounters"), counters},
                       {QStringLiteral("workflowImageCache"), cache},
                       {QStringLiteral("historySteps"), QJsonValue::Null},
                       {QStringLiteral("historyNote"),
                        QStringLiteral("a workflow run opens no image document, so it records no history step")}};
}

[[nodiscard]] QJsonArray outputsArray(const QString& outputDirectory, const WorkflowExecutor::Result& result)
{
    QJsonArray outputs;
    const QDir root(outputDirectory);
    const auto add = [&](const QString& role, const QStringList& paths) {
        for (const QString& path : paths)
        {
            const QFileInfo info(path);
            outputs.append(QJsonObject{{QStringLiteral("role"), role},
                                       {QStringLiteral("path"), root.relativeFilePath(info.absoluteFilePath())},
                                       {QStringLiteral("bytes"), static_cast<double>(info.size())},
                                       {QStringLiteral("sha256"), fileSha256(path)}});
        }
    };
    add(QStringLiteral("bias-master"), result.generatedBiasMasters);
    add(QStringLiteral("dark-master"), result.generatedDarkMasters);
    add(QStringLiteral("flat-master"), result.generatedFlatMasters);
    add(QStringLiteral("light-master"), result.generatedLightMasters);
    add(QStringLiteral("rejection-map"), result.generatedRejectionMaps);
    if (!result.htmlReportPath.isEmpty())
    {
        add(QStringLiteral("report"), {result.htmlReportPath});
    }
    return outputs;
}

[[nodiscard]] QJsonObject outputTree(const QString& outputDirectory)
{
    qint64 files = 0;
    qint64 bytes = 0;
    QDirIterator it(outputDirectory, QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (it.hasNext())
    {
        it.next();
        ++files;
        bytes += it.fileInfo().size();
    }
    return QJsonObject{{QStringLiteral("files"), static_cast<double>(files)},
                       {QStringLiteral("bytes"), static_cast<double>(bytes)}};
}

/// What the session's engine context's memory resource counts, or nothing when the session holds no context.
[[nodiscard]] std::optional<an::MemoryCounters> engineMemoryCounters(const an::consumer::EngineSession& session)
{
    const an::Result<an::consumer::SessionHold> hold = session.hold();
    if (!hold.hasValue())
    {
        return std::nullopt;
    }
    const an::MemoryResource* resource = hold->engine().memoryResource();
    if (resource == nullptr)
    {
        return std::nullopt;
    }
    return resource->counters();
}

/// The engine's accounting across the region: its memory resource before and after, and each consumer allowance.
[[nodiscard]] QJsonObject engineAccounting(const an::consumer::EngineSession& session,
                                           const std::optional<an::MemoryCounters>& before,
                                           const std::optional<an::MemoryCounters>& after)
{
    QJsonObject accounting;
    if (before.has_value() && after.has_value())
    {
        accounting.insert(QStringLiteral("liveBytesBeforeRegion"), bytesValue(before->liveBytes));
        accounting.insert(QStringLiteral("liveBytesAfterRegion"), bytesValue(after->liveBytes));
        accounting.insert(QStringLiteral("liveAllocationsAfterRegion"), bytesValue(after->liveAllocations));
        // The resource's peak never falls, and the set-up before the region reads only headers, so the peak after the
        // region is the region's whenever it rose inside it.
        accounting.insert(QStringLiteral("peakBytes"), bytesValue(after->peakBytes));
        accounting.insert(QStringLiteral("peakBeforeRegionBytes"), bytesValue(before->peakBytes));
        accounting.insert(QStringLiteral("allocationsInRegion"),
                          bytesValue(after->totalAllocations - before->totalAllocations));
        accounting.insert(QStringLiteral("bytesAllocatedInRegion"), bytesValue(after->totalBytes - before->totalBytes));
        accounting.insert(QStringLiteral("deniedAllocationsInRegion"),
                          bytesValue(after->deniedAllocations - before->deniedAllocations));
    }
    QJsonObject allowances;
    constexpr std::array<an::consumer::MemoryCategory, an::consumer::kMemoryCategoryCount> kCategories{
        an::consumer::MemoryCategory::HistoryCopies, an::consumer::MemoryCategory::PendingSnapshots,
        an::consumer::MemoryCategory::DerivedCaches, an::consumer::MemoryCategory::DisplayBuffers};
    for (const an::consumer::MemoryCategory category : kCategories)
    {
        const an::consumer::MemoryCategoryCounters counters = session.memory().counters(category);
        allowances.insert(QString::fromLatin1(an::consumer::memoryCategoryName(category)),
                          QJsonObject{{QStringLiteral("reservedBytes"), bytesValue(counters.reservedBytes)},
                                      {QStringLiteral("peakBytes"), bytesValue(counters.peakBytes)},
                                      {QStringLiteral("admitted"), bytesValue(counters.admitted)},
                                      {QStringLiteral("refused"), bytesValue(counters.refused)},
                                      {QStringLiteral("evictedBytes"), bytesValue(counters.evictedBytes)},
                                      {QStringLiteral("overrunAdmissions"), bytesValue(counters.overrunAdmissions)}});
    }
    accounting.insert(QStringLiteral("consumerAllowances"), allowances);
    return accounting;
}

void writeResult(const QString& path, const QJsonObject& result)
{
    if (path.isEmpty())
    {
        return;
    }
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        file.write(QJsonDocument(result).toJson(QJsonDocument::Indented));
    }
}

} // namespace

QString driverUsage()
{
    return QStringLiteral(
        "AfterNightWorkflowDriver --workflow full|lite --corpus-manifest <corpus.json> --frame-set <id>\n"
        "                         --corpus-root <dir> --output <workflow output dir> --result <result.json>\n"
        "                         [--variant 0|1] [--max-parallel-cores N] [--cancel-after-ms N]\n"
        "                         [--verify-digests] [--sampling-interval-ms N]\n");
}

DriverOptionsParse parseDriverOptions(const QStringList& arguments)
{
    DriverOptions options;
    std::map<QString, QString> values;
    for (int index = 0; index < arguments.size(); ++index)
    {
        const QString& argument = arguments[index];
        if (argument == QStringLiteral("--verify-digests"))
        {
            options.verifyDigests = true;
            continue;
        }
        if (!argument.startsWith(QStringLiteral("--")) || index + 1 >= arguments.size())
        {
            return {std::nullopt, QStringLiteral("unexpected argument %1").arg(argument)};
        }
        values[argument] = arguments[++index];
    }
    const auto take = [&values](const char* name) {
        const auto found = values.find(QString::fromLatin1(name));
        if (found == values.end())
        {
            return QString();
        }
        QString value = found->second;
        values.erase(found);
        return value;
    };
    const QString workflow = take("--workflow");
    if (workflow == QStringLiteral("full"))
    {
        options.workflow = DriverWorkflow::Full;
    }
    else if (workflow == QStringLiteral("lite"))
    {
        options.workflow = DriverWorkflow::Lite;
    }
    else
    {
        return {std::nullopt, QStringLiteral("--workflow must be full or lite")};
    }
    options.corpusManifest = take("--corpus-manifest");
    options.frameSet = take("--frame-set");
    options.corpusRoot = take("--corpus-root");
    options.outputDirectory = take("--output");
    options.resultPath = take("--result");
    for (const auto& [name, value] :
         {std::pair{"--corpus-manifest", options.corpusManifest}, std::pair{"--frame-set", options.frameSet},
          std::pair{"--corpus-root", options.corpusRoot}, std::pair{"--output", options.outputDirectory},
          std::pair{"--result", options.resultPath}})
    {
        if (value.isEmpty())
        {
            return {std::nullopt, QStringLiteral("%1 is required").arg(QString::fromLatin1(name))};
        }
    }
    const auto integer = [&take](const char* name, int fallback, int lowest, int highest, bool& ok) {
        const QString text = take(name);
        if (text.isEmpty())
        {
            return fallback;
        }
        const int value = text.toInt(&ok);
        ok = ok && value >= lowest && value <= highest;
        return value;
    };
    bool ok = true;
    options.variant = integer("--variant", 0, 0, 1, ok);
    if (!ok)
    {
        return {std::nullopt, QStringLiteral("--variant must be 0 or 1")};
    }
    options.maxParallelCores = integer("--max-parallel-cores", 0, 0, 4096, ok);
    if (!ok)
    {
        return {std::nullopt, QStringLiteral("--max-parallel-cores must be a count, 0 for the default")};
    }
    options.samplingIntervalMs = integer("--sampling-interval-ms", 10, 1, 1000, ok);
    if (!ok)
    {
        return {std::nullopt, QStringLiteral("--sampling-interval-ms must be 1 to 1000")};
    }
    const int cancelAfter = integer("--cancel-after-ms", -1, 0, 86'400'000, ok);
    if (!ok)
    {
        return {std::nullopt, QStringLiteral("--cancel-after-ms must be a number of milliseconds")};
    }
    if (cancelAfter >= 0)
    {
        options.cancelAfterMs = cancelAfter;
    }
    if (!values.empty())
    {
        return {std::nullopt, QStringLiteral("unknown option %1").arg(values.begin()->first)};
    }
    return {options, QString()};
}

QString frameSetIdentityDigest(const QJsonObject& frameSetEntry)
{
    // Over the role names and the members' recorded digests, in order, with separators that cannot occur inside
    // either - the benchmark layer's rule, so a result and a benchmark name the same set by the same identity.
    QCryptographicHash hash(QCryptographicHash::Sha256);
    for (const QJsonValue& roleValue : frameSetEntry.value(QStringLiteral("roles")).toArray())
    {
        const QJsonObject role = roleValue.toObject();
        hash.addData(role.value(QStringLiteral("role")).toString().toUtf8());
        hash.addData(QByteArrayView("\n", 1));
        for (const QJsonValue& frame : role.value(QStringLiteral("frames")).toArray())
        {
            hash.addData(frame.toObject().value(QStringLiteral("sha256")).toString().toUtf8());
            hash.addData(QByteArrayView("\n", 1));
        }
        hash.addData(QByteArrayView("\x1e", 1));
    }
    return QString::fromLatin1(hash.result().toHex());
}

FrameSetRead readFrameSet(const QString& corpusManifest, const QString& frameSetId, const QString& corpusRoot,
                          const bool verifyDigests)
{
    QFile file(corpusManifest);
    if (!file.open(QIODevice::ReadOnly))
    {
        return {std::nullopt, QStringLiteral("the corpus manifest %1 cannot be read").arg(corpusManifest)};
    }
    const QByteArray bytes = file.readAll();
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(bytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
    {
        return {std::nullopt, QStringLiteral("the corpus manifest is not JSON: %1").arg(parseError.errorString())};
    }
    QJsonObject entry;
    for (const QJsonValue& value : document.object().value(QStringLiteral("frameSets")).toArray())
    {
        if (value.toObject().value(QStringLiteral("id")).toString() == frameSetId)
        {
            entry = value.toObject();
        }
    }
    if (entry.isEmpty())
    {
        return {std::nullopt, QStringLiteral("the corpus manifest names no frame set %1").arg(frameSetId)};
    }
    FrameSet set;
    set.id = frameSetId;
    set.identityDigest = frameSetIdentityDigest(entry);
    set.manifestSha256 = QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
    const QDir root(corpusRoot);
    for (const QJsonValue& roleValue : entry.value(QStringLiteral("roles")).toArray())
    {
        const QJsonObject role = roleValue.toObject();
        const QString roleName = role.value(QStringLiteral("role")).toString();
        const std::optional<FrameType> type = frameTypeOfRole(roleName);
        if (!type.has_value())
        {
            return {std::nullopt,
                    QStringLiteral("the frame set has a role %1 the workflow has no frame type for").arg(roleName)};
        }
        for (const QJsonValue& frameValue : role.value(QStringLiteral("frames")).toArray())
        {
            const QJsonObject frame = frameValue.toObject();
            FrameSetMember member;
            member.type = *type;
            member.role = roleName;
            member.path = root.filePath(frame.value(QStringLiteral("relativePath")).toString());
            member.sizeBytes = static_cast<qint64>(frame.value(QStringLiteral("sizeBytes")).toDouble());
            member.sha256 = frame.value(QStringLiteral("sha256")).toString();
            const QFileInfo info(member.path);
            if (!info.isFile() || info.size() != member.sizeBytes)
            {
                return {std::nullopt, QStringLiteral("%1 is missing or not %2 bytes, so the set is refused whole")
                                          .arg(member.path)
                                          .arg(member.sizeBytes)};
            }
            if (verifyDigests && fileSha256(member.path) != member.sha256)
            {
                return {std::nullopt, QStringLiteral("%1 does not have its recorded digest").arg(member.path)};
            }
            set.totalBytes += member.sizeBytes;
            set.members.push_back(std::move(member));
        }
    }
    if (set.members.empty())
    {
        return {std::nullopt, QStringLiteral("the frame set %1 has no frame").arg(frameSetId)};
    }
    return {set, QString()};
}

FrameImport importFrameSet(IntegrationDataModel& model, const FrameSet& frameSet,
                           const an::consumer::EngineSession& session)
{
    FrameImport outcome;
    std::map<FrameType, std::vector<QString>> pathsByType;
    for (const FrameSetMember& member : frameSet.members)
    {
        pathsByType[member.type].push_back(member.path);
    }
    for (const auto& [type, paths] : pathsByType)
    {
        tbb::concurrent_vector<ImageIntegration::DetectedFrameInfo> detected;
        tbb::concurrent_vector<QString> issues;
        tbb::parallel_for_each(paths.begin(), paths.end(), [&, frameType = type](const QString& path) {
            const ImageIntegration::BuildDetectedFrameInfoResult result =
                ImageIntegration::buildDetectedFrameInfoWithIssue(session, path, frameType,
                                                                  ImageIntegration::ImportAdmission::Wait);
            if (result.issue.has_value())
            {
                issues.push_back(QStringLiteral("%1: %2").arg(result.issue->path, result.issue->message));
            }
            if (result.detected.has_value())
            {
                detected.push_back(*result.detected);
            }
        });
        const ImageIntegration::detail::PreparedImportBatch batch = ImageIntegration::detail::prepareImportBatch(
            std::vector<ImageIntegration::DetectedFrameInfo>(detected.begin(), detected.end()), type, true);
        ImageIntegration::detail::applyImportBatch(&model, nullptr, batch, QString());
        outcome.imported += static_cast<int>(detected.size());
        for (const QString& issue : issues)
        {
            outcome.issues << issue;
        }
    }
    return outcome;
}

void applyFullVariant(IntegrationDataModel& model, const int variant)
{
    if (variant == 0)
    {
        return;
    }
    const StackingSettings defaults;
    const StackingSettingsMap& existing = model.stackingSettings(FrameType::Light);
    const std::vector<GroupKey> groups = model.integrationGroups();
    for (const GroupKey& group : groups)
    {
        const auto found = existing.find(group);
        StackingSettings settings = found != existing.end() ? found->second : defaults;
        settings.sigmaHigh = defaults.sigmaHigh + 0.1;
        model.setStackingSettings(FrameType::Light, group, settings);
    }
}

void applyLiteVariant(ImageIntegrationLite::ProcessingOptions& options, const int variant)
{
    if (variant == 0)
    {
        return;
    }
    // Lite resolves an automatic rejection per group, over any sigma the options state, and gives the calibration
    // masters the options' rejection too; the weighting reaches the light stack alone.
    options.weighting = options.weighting.compare(QStringLiteral("Noise"), Qt::CaseInsensitive) == 0
                            ? QStringLiteral("None")
                            : QStringLiteral("Noise");
}

QJsonObject runWorkflowMeasurement(const DriverOptions& options, const an::consumer::EngineSession& session)
{
    QJsonObject result;
    result.insert(QStringLiteral("schema"), 1);
    result.insert(QStringLiteral("driver"), QStringLiteral("AfterNightWorkflowDriver"));
    result.insert(QStringLiteral("engine"), options.engine);
    result.insert(
        QStringLiteral("request"),
        QJsonObject{{QStringLiteral("workflow"),
                     options.workflow == DriverWorkflow::Full ? QStringLiteral("full") : QStringLiteral("lite")},
                    {QStringLiteral("variant"), options.variant},
                    {QStringLiteral("maxParallelCores"), options.maxParallelCores},
                    {QStringLiteral("cancelAfterMs"),
                     options.cancelAfterMs ? QJsonValue(*options.cancelAfterMs) : QJsonValue::Null},
                    {QStringLiteral("outputDirectory"), QDir(options.outputDirectory).absolutePath()},
                    {QStringLiteral("hardwareThreads"), static_cast<int>(std::thread::hardware_concurrency())}});

    const auto fail = [&](const QString& stage, const QString& message) {
        result.insert(QStringLiteral("outcome"), QJsonObject{{QStringLiteral("success"), false},
                                                             {QStringLiteral("failedAt"), stage},
                                                             {QStringLiteral("errorMessage"), message}});
        LOG_ERROR("AfterNightWorkflowDriver: {} failed: {}", stage.toStdString(), message.toStdString());
        writeResult(options.resultPath, result);
        return result;
    };

    // ---- Set-up, outside the measured region: the session a user would have open before pressing Execute.
    const Clock::time_point setupStarted = Clock::now();
    const FrameSetRead read =
        readFrameSet(options.corpusManifest, options.frameSet, options.corpusRoot, options.verifyDigests);
    if (!read.frameSet.has_value())
    {
        return fail(QStringLiteral("dataset"), read.error);
    }
    const FrameSet& frameSet = *read.frameSet;
    std::map<QString, int> roleCounts;
    for (const FrameSetMember& member : frameSet.members)
    {
        ++roleCounts[member.role];
    }
    QJsonObject roles;
    for (const auto& [role, count] : roleCounts)
    {
        roles.insert(role, count);
    }
    result.insert(QStringLiteral("dataset"),
                  QJsonObject{{QStringLiteral("frameSet"), frameSet.id},
                              {QStringLiteral("identityDigest"), frameSet.identityDigest},
                              {QStringLiteral("corpusManifestSha256"), frameSet.manifestSha256},
                              {QStringLiteral("frames"), static_cast<int>(frameSet.members.size())},
                              {QStringLiteral("roles"), roles},
                              {QStringLiteral("bytes"), static_cast<double>(frameSet.totalBytes)},
                              {QStringLiteral("digestsVerified"), options.verifyDigests}});

    if (!QDir().mkpath(options.outputDirectory))
    {
        return fail(QStringLiteral("output"), QStringLiteral("the output directory cannot be created"));
    }
    IntegrationDataModel model;
    const FrameImport imported = importFrameSet(model, frameSet, session);
    model.setOutputDirectory(QDir(options.outputDirectory).absolutePath());
    if (options.workflow == DriverWorkflow::Full)
    {
        applyFullVariant(model, options.variant);
    }
    result.insert(QStringLiteral("session"),
                  QJsonObject{{QStringLiteral("imported"), imported.imported},
                              {QStringLiteral("importIssues"), QJsonArray::fromStringList(imported.issues)},
                              {QStringLiteral("readyForIntegration"), model.isReadyForIntegration()},
                              {QStringLiteral("integrationGroups"), static_cast<int>(model.integrationGroups().size())},
                              {QStringLiteral("setupMs"), millisecondsBetween(setupStarted, Clock::now())}});
    if (imported.imported != static_cast<int>(frameSet.members.size()))
    {
        return fail(
            QStringLiteral("import"),
            QStringLiteral("%1 of %2 frames were imported").arg(imported.imported).arg(frameSet.members.size()));
    }
    const QJsonObject snapshot = model.toJson();
    QCoreApplication::processEvents();

    // ---- The measured region: from the accepted execution request to the controller's finished signal.
    const ResidentMemoryReading beforeRegion = readResidentMemory();
    const std::optional<an::MemoryCounters> engineBefore = engineMemoryCounters(session);
    const bool highWaterReset = resetResidentHighWater();
    ResidentMemorySampler sampler(std::chrono::milliseconds(options.samplingIntervalMs));
    sampler.setPhase("preparation");
    sampler.start();
    const Clock::time_point requestAccepted = Clock::now();

    const ImageIntegrationLite::ProcessingOptions lite = liteOptions(options);
    const auto never = []() { return false; };
    const ImageIntegration::FrameHeaderReader headerReader = ImageIntegration::sessionFrameHeaderReader(session);
    ImageIntegration::PreparedWorkflowExecution prepared =
        options.workflow == DriverWorkflow::Full
            ? ImageIntegration::preparePlannedWorkflowExecution(
                  [&snapshot, &options]() {
                      IntegrationDataModel preparedModel;
                      preparedModel.fromJson(snapshot);
                      return ImageIntegration::buildFullWorkflowManifest(preparedModel,
                                                                         {false, options.maxParallelCores, false});
                  },
                  std::nullopt, never, {}, headerReader)
            : ImageIntegration::prepareDirectWorkflowExecution(
                  [&snapshot, &lite]() { return ImageIntegrationLite::prepareManifest(snapshot, lite, false); }, never,
                  headerReader);
    const Clock::time_point preparationFinished = Clock::now();
    const ImageIntegration::PreparedWorkflowDisposition disposition =
        options.workflow == DriverWorkflow::Full ? ImageIntegration::plannedDisposition(prepared)
                                                 : ImageIntegration::directDisposition(prepared);

    QJsonObject preparation{
        {QStringLiteral("disposition"), dispositionName(disposition)},
        {QStringLiteral("errorMessage"), prepared.errorMessage},
        {QStringLiteral("manifestBuildMs"), static_cast<double>(prepared.preparationTiming.manifestBuildMs)},
        {QStringLiteral("planAndValidationMs"), static_cast<double>(prepared.preparationTiming.planAndValidationMs)},
        {QStringLiteral("executionMode"), executionModeName(prepared.manifest.executionMode)},
        {QStringLiteral("stageSequence"), QJsonArray::fromStringList(prepared.manifest.stageSequence)},
        {QStringLiteral("integrationGroups"), static_cast<int>(prepared.manifest.integrationGroups.size())},
        {QStringLiteral("incrementalIntegrationGroups"),
         static_cast<int>(prepared.manifest.incrementalIntegrationGroups.size())},
        {QStringLiteral("plan"), options.workflow == DriverWorkflow::Full
                                     ? prepared.plan.toSummaryText()
                                     : QStringLiteral("no plan: a direct preparation")}};

    WorkflowExecutor::Result execution;
    bool executed = false;
    std::optional<Clock::time_point> cancelRequested;
    int progressEvents = 0;
    int logMessages = 0;
    if (disposition == ImageIntegration::PreparedWorkflowDisposition::Ready)
    {
        WorkflowProgressController controller(&model, &session);
        QEventLoop loop;
        QObject::connect(&controller, &WorkflowProgressController::finished, &loop,
                         [&](const WorkflowExecutor::Result& finished) {
                             execution = finished;
                             executed = true;
                             loop.quit();
                         });
        QObject::connect(&controller, &WorkflowProgressController::progressUpdated, &loop,
                         [&](const WorkflowProgress& progress) {
                             ++progressEvents;
                             sampler.setPhase(stageName(progress.currentStage).toStdString());
                         });
        QObject::connect(&controller, &WorkflowProgressController::logMessage, &loop,
                         [&](WorkflowLogLevel, const QString&) { ++logMessages; });
        sampler.setPhase("execution");
        controller.start(prepared.manifest, prepared.preparationTiming, prepared.preparationSnapshot);
        if (options.cancelAfterMs.has_value())
        {
            QTimer::singleShot(*options.cancelAfterMs, &loop, [&]() {
                cancelRequested = Clock::now();
                controller.cancel();
            });
        }
        if (!executed)
        {
            loop.exec();
        }
    }
    const Clock::time_point completed = Clock::now();
    sampler.setPhase("completed");
    const ResidentMemorySampler::Summary sampled = sampler.stop();
    const ResidentMemoryReading afterRegion = readResidentMemory();
    const std::optional<an::MemoryCounters> engineAfter = engineMemoryCounters(session);

    // ---- What the region cost.
    std::uint64_t residentPeak = sampled.peakBytes;
    QString peakSource = QStringLiteral("the sampler's largest reading");
    if (highWaterReset && afterRegion.valid)
    {
        residentPeak = (std::max)(residentPeak, afterRegion.highWaterBytes);
        peakSource = QStringLiteral("the system high-water mark, reset at the region's start");
    }
    else if (afterRegion.valid && afterRegion.highWaterBytes > beforeRegion.highWaterBytes)
    {
        residentPeak = (std::max)(residentPeak, afterRegion.highWaterBytes);
        peakSource = QStringLiteral("the process's high-water mark, which rose inside the region");
    }
    QJsonObject phasePeaks;
    for (const auto& [phase, bytes] : sampled.phasePeaks)
    {
        phasePeaks.insert(QString::fromStdString(phase), bytesValue(bytes));
    }
    result.insert(QStringLiteral("memory"),
                  QJsonObject{{QStringLiteral("metric"), QString::fromStdString(residentMemoryMetric())},
                              {QStringLiteral("samplingIntervalMs"), sampled.intervalMs},
                              {QStringLiteral("samples"), static_cast<double>(sampled.samples)},
                              {QStringLiteral("idleBaselineBytes"), bytesValue(beforeRegion.residentBytes)},
                              {QStringLiteral("residentPeakBytes"), bytesValue(residentPeak)},
                              {QStringLiteral("residentPeakSource"), peakSource},
                              {QStringLiteral("sampledPeakBytes"), bytesValue(sampled.peakBytes)},
                              {QStringLiteral("sampledPeakAtMs"), sampled.peakAtMs},
                              {QStringLiteral("sampledPeakPhase"), QString::fromStdString(sampled.peakPhase)},
                              {QStringLiteral("phasePeaks"), phasePeaks},
                              {QStringLiteral("highWaterResetAtRegionStart"), highWaterReset},
                              {QStringLiteral("processHighWaterBytes"),
                               bytesValue((std::max)(beforeRegion.highWaterBytes, afterRegion.highWaterBytes))},
                              {QStringLiteral("highWaterBeforeRegionBytes"), bytesValue(beforeRegion.highWaterBytes)}});

    QJsonArray stages;
    for (const WorkflowExecutor::Result::StageTiming& stage : execution.stageResults)
    {
        stages.append(QJsonObject{{QStringLiteral("name"), stage.stageName},
                                  {QStringLiteral("status"), stage.status},
                                  {QStringLiteral("ms"), stage.durationMs},
                                  {QStringLiteral("groups"), stage.groupsProcessed}});
    }
    result.insert(
        QStringLiteral("timing"),
        QJsonObject{
            {QStringLiteral("totalMs"), millisecondsBetween(requestAccepted, completed)},
            {QStringLiteral("preparationMs"), millisecondsBetween(requestAccepted, preparationFinished)},
            {QStringLiteral("executionMs"), millisecondsBetween(preparationFinished, completed)},
            {QStringLiteral("executorTotalMs"), execution.totalDurationMs},
            {QStringLiteral("stageExecutionMs"), static_cast<double>(execution.executorTiming.stageExecutionMs)},
            {QStringLiteral("stateCommitMs"), static_cast<double>(execution.executorTiming.stateCommitMs)},
            {QStringLiteral("archiveMs"), static_cast<double>(execution.executorTiming.archiveMs)},
            {QStringLiteral("reportMs"), static_cast<double>(execution.executorTiming.reportMs)},
            {QStringLiteral("stages"), stages}});
    result.insert(QStringLiteral("preparation"), preparation);
    result.insert(QStringLiteral("consumer"), countersObject(execution));
    QJsonObject engine = options.engine;
    engine.insert(QStringLiteral("region"), engineAccounting(session, engineBefore, engineAfter));
    result.insert(QStringLiteral("engine"), engine);
    result.insert(QStringLiteral("events"),
                  QJsonObject{{QStringLiteral("progress"), progressEvents}, {QStringLiteral("log"), logMessages}});

    QJsonObject outcome{{QStringLiteral("disposition"), dispositionName(disposition)},
                        {QStringLiteral("executed"), executed},
                        {QStringLiteral("success"), executed && execution.success},
                        {QStringLiteral("cancelled"), execution.cancelled},
                        {QStringLiteral("partialSuccess"), execution.partialSuccess},
                        {QStringLiteral("errorMessage"), executed ? execution.errorMessage : prepared.errorMessage},
                        {QStringLiteral("errors"), static_cast<int>(execution.errors.size())},
                        {QStringLiteral("warnings"), execution.warningCount}};
    if (cancelRequested.has_value())
    {
        outcome.insert(QStringLiteral("cancelRequestedAtMs"), millisecondsBetween(requestAccepted, *cancelRequested));
        outcome.insert(QStringLiteral("cancellationLatencyMs"), millisecondsBetween(*cancelRequested, completed));
    }
    result.insert(QStringLiteral("outcome"), outcome);

    // ---- After the region: what was published, for the comparison to hold to its contract.
    result.insert(QStringLiteral("outputs"), outputsArray(options.outputDirectory, execution));
    result.insert(QStringLiteral("outputTree"), outputTree(options.outputDirectory));
    writeResult(options.resultPath, result);
    return result;
}

int exitCodeFor(const QJsonObject& result)
{
    const QJsonObject outcome = result.value(QStringLiteral("outcome")).toObject();
    if (outcome.value(QStringLiteral("success")).toBool())
    {
        return 0;
    }
    const bool cancelAsked =
        !result.value(QStringLiteral("request")).toObject().value(QStringLiteral("cancelAfterMs")).isNull();
    if (cancelAsked && outcome.value(QStringLiteral("cancelled")).toBool())
    {
        return 3;
    }
    return 2;
}

} // namespace an::measurement
